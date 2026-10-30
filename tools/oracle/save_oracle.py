"""Oráculo de partidas y comercio: ejecuta en Unicorn el código de Patrician3.exe
que lee una partida y el que compra y vende entre un barco y una ciudad.

Carga el mundo (0x52FB60) y los barcos (0x504230) de una partida ya
descomprimida tal como lo hace el juego, vuelca comerciantes, oficinas y barcos,
y después ejecuta una serie de compras y ventas aleatorias con los manejadores
de las operaciones 0x02 (0x537450) y 0x01 (0x537340), anotando el estado tras
cada una. tools/oracle/compare_save.cpp lee el CSV y comprueba que p3data
obtiene lo mismo (ver docs/oracle.md).

Solo se emula la CPU con las secciones del ejecutable en memoria. operator
new/delete se sustituyen por un montón propio y se saltan dos funciones que no
leen nada del fichero (la matriz de distancias entre ciudades y el cálculo de
la ruta de un barco por el mar).

uso: python3 save_oracle.py <Patrician3.exe> <partida.raw> <salida.csv> [operaciones]
La partida descomprimida se obtiene con `p3save <archivo.cpr> missav/1.pat raw <partida.raw>`.
"""
import random
import site
import struct
import sys

sys.path.append(site.getusersitepackages())
from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UC_MODE_32, Uc, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP,  # noqa: E402
                               UC_X86_REG_FPCW)

OPERATOR_NEW = 0x64F7B9
OPERATOR_DELETE = 0x64F7E2
MALLOC = 0x63CB41
FREE = 0x63A1B6
READ_WORLD = 0x52FB60
READ_SHIPS = 0x504230
BUY_TO_SHIP = 0x537450  # operación 0x02
SELL_FROM_SHIP = 0x537340  # operación 0x01
SKIPPED = {0x4482C0: 0, 0x445010: 8}  # dirección: bytes de argumentos que saca al volver
WORLD = 0x6DE4A0
SHIPS = 0x6DD7A0
SELL_CONTEXT = 0x6DE3D8  # el float en +0x64 es el factor de dificultad del comercio
EXPEDITIONS = 0x6DF2E0  # scripts/Expeditionen.dat; el constructor del comerciante lo carga si está vacío
DIFFICULTY_FACTORS = (2.2, 2.0, 1.8)
SEPARATOR = 0xAFFE
TRADE_WARES = 20
LOAD_WARES = (0, 1, 2, 11, 14, 16, 17, 19)

STACK = 0x0100_0000
SENTINEL = 0x0300_0000
FILE = 0x0400_0000
HEAP = 0x1000_0000
HEAP_SIZE = 0x0800_0000


class SaveOracle:
    def __init__(self, exe, save):
        data = open(exe, 'rb').read()
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        sections = struct.unpack_from('<H', data, pe + 6)[0]
        optional_size = struct.unpack_from('<H', data, pe + 20)[0]
        base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
        image_size = struct.unpack_from('<I', data, pe + 24 + 56)[0]
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(0, 0x1000)  # fs:0 apunta aquí: cadena de manejadores SEH vacía
        uc.mem_write(0, struct.pack('<I', 0xFFFFFFFF))
        uc.mem_map(base, (image_size + 0xFFF) & ~0xFFF)
        uc.mem_write(base, data[:0x1000])
        for i in range(sections):
            o = pe + 24 + optional_size + i * 40
            vsize, va, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
            uc.mem_write(base + va, data[raw:raw + min(rsize, max(vsize, rsize))])
        uc.mem_map(STACK, 0x10_0000)
        uc.mem_map(SENTINEL, 0x1000)
        uc.mem_map(HEAP, HEAP_SIZE)
        uc.mem_map(FILE, (len(save) + 0xFFFF) & ~0xFFF)
        uc.mem_write(FILE, save)
        self.save = save
        self.heap_top = HEAP
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._unmapped)
        for address in (OPERATOR_NEW, MALLOC, OPERATOR_DELETE, FREE, *SKIPPED):
            uc.hook_add(UC_HOOK_CODE, self._stub, begin=address, end=address)
        self.write_u32(EXPEDITIONS, self.alloc(0x1000))
        # El mundo no se ha construido: las std::list de sus 15 objetos de 0x2C
        # bytes (en +0xB10, cabeza en +0x24) necesitan su nodo centinela.
        for i in range(15):
            node = self.alloc(0x18)
            self.uc.mem_write(node, struct.pack('<II', node, node))
            self.uc.mem_write(WORLD + 0xB10 + i * 0x2C + 0x24, struct.pack('<II', node, 0))
        self.set_difficulty(1)

    def _unmapped(self, uc, access, address, size, value, user_data):
        print(f'acceso fuera de memoria en {address:#x} (eip {uc.reg_read(UC_X86_REG_EIP):#x})', file=sys.stderr)
        return False

    def _arg(self, n):
        return self.u32(self.uc.reg_read(UC_X86_REG_ESP) + 4 + 4 * n)

    def _stub(self, uc, address, size, user_data):
        if address in (OPERATOR_NEW, MALLOC):
            result, pop = self.alloc(self._arg(0)), 0
        elif address in SKIPPED:
            result, pop = 0, SKIPPED[address]
        else:
            result, pop = 0, 0
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EAX, result)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + pop)
        uc.reg_write(UC_X86_REG_EIP, self.u32(esp))

    def alloc(self, n):
        p = self.heap_top
        self.heap_top = (p + n + 0x1F) & ~0xF
        if self.heap_top > HEAP + HEAP_SIZE:
            raise RuntimeError('montón agotado')
        self.uc.mem_write(p, b'\0' * n)
        return p

    def call(self, address, args, ecx=0):
        uc = self.uc
        esp = STACK + 0x10_0000 - 0x1000
        frame = struct.pack('<I', SENTINEL) + b''.join(struct.pack('<I', a & 0xFFFFFFFF) for a in args)
        esp -= len(frame)
        uc.mem_write(esp, frame)
        uc.reg_write(UC_X86_REG_ESP, esp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        # El runtime de MSVC deja la FPU a 53 bits (0x27F); el emulador arranca con 64.
        uc.reg_write(UC_X86_REG_FPCW, 0x27F)
        try:
            uc.emu_start(address, SENTINEL, count=500_000_000)
        except UcError as e:
            raise RuntimeError(f'la emulación falló en {uc.reg_read(UC_X86_REG_EIP):#x}: {e}') from e
        if uc.reg_read(UC_X86_REG_EIP) != SENTINEL:
            raise RuntimeError(f'la emulación se paró en {uc.reg_read(UC_X86_REG_EIP):#x}')
        return uc.reg_read(UC_X86_REG_EAX)

    def u8(self, a):
        return self.uc.mem_read(a, 1)[0]

    def u16(self, a):
        return struct.unpack('<H', self.uc.mem_read(a, 2))[0]

    def u32(self, a):
        return struct.unpack('<I', self.uc.mem_read(a, 4))[0]

    def s32(self, a):
        return struct.unpack('<i', self.uc.mem_read(a, 4))[0]

    def write_u32(self, a, v):
        self.uc.mem_write(a, struct.pack('<I', v & 0xFFFFFFFF))

    def set_difficulty(self, difficulty):
        """0 baja, 1 normal, 2 alta."""
        self.uc.mem_write(SELL_CONTEXT + 0x64, struct.pack('<f', DIFFICULTY_FACTORS[difficulty]))

    def load(self):
        version = struct.unpack_from('<I', self.save, 0)[0] >> 16
        end = self.call(READ_WORLD, [FILE + 0x60, version], ecx=WORLD) - FILE
        if struct.unpack_from('<I', self.save, end)[0] != SEPARATOR:
            raise RuntimeError(f'el mundo acaba en {end}, sin separador')
        end = self.call(READ_SHIPS, [FILE + end + 4], ecx=SHIPS) - FILE
        if struct.unpack_from('<I', self.save, end)[0] != SEPARATOR:
            raise RuntimeError(f'los barcos acaban en {end}, sin separador')

    # Acceso a lo cargado.
    def counts(self):
        return self.u32(WORLD + 0x10), self.u16(WORLD + 8), self.u16(WORLD + 0xA), self.u16(SHIPS + 0xF4)

    def town(self, i):
        return self.u32(WORLD + 0x68) + i * 0x9F8

    def office(self, i):
        return self.u32(WORLD + 0x74) + i * 0x44C

    def merchant(self, i):
        return self.u32(WORLD + 0x78) + i * 0x650

    def ship(self, i):
        return self.u32(SHIPS + 4) + i * 0x180

    def ship_state(self, i, ware):
        s = self.ship(i)
        return [self.s32(s + 0x54 + 4 * ware), f'{self.u32(s + 0xB4 + 4 * ware):08x}', self.s32(s + 0x118),
                self.s32(s + 0x11C)]

    def trade(self, function, ship, town, ware, amount):
        # Operación de 20 bytes: código, cantidad, mercancía, barco, ?, ciudad.
        op = self.alloc(20)
        self.uc.mem_write(op, struct.pack('<IiHHHH', 2 if function == BUY_TO_SHIP else 1, amount, ware, ship, 0,
                                          town) + b'\0' * 4)
        self.call(function, [], ecx=op)


def dump_state(o, out):
    towns, offices, merchants, ships = o.counts()
    for i in range(towns):
        t = o.town(i)
        stock = [o.u32(t + 4 + 4 * w) for w in range(24)]
        thresholds = [o.u32(t + 0x4F0 + 4 * k) for k in range(96)]
        out.write(f'ciudad,{i},{",".join(map(str, stock + thresholds))}\n')
    for i in range(merchants):
        m = o.merchant(i)
        out.write(f'comerciante,{i},{o.s32(m)},{o.u16(m + 8)},{o.u16(m + 0xC)},{o.u16(m + 0xE)},{o.u8(m + 0x19)},'
                  f'{o.u8(m + 0x1A)},{o.u8(m + 0x1C) & 0x7F},{o.u8(m + 0x1D)},{o.u32(m + 0x608)}\n')
    for i in range(offices):
        f = o.office(i)
        owner = o.u16(f + 0x2C4)
        wares = [o.u32(f + 4 + 4 * w) for w in range(24)] if owner != 0xFFFF else [0] * 24
        town = o.u8(f + 0x2C6) if owner != 0xFFFF else 0xFF
        out.write(f'oficina,{i},{owner},{town},{o.u16(f + 0x2CA)},{",".join(map(str, wares))}\n')
    for i in range(ships):
        s = o.ship(i)
        status = o.u16(s + 0x134)
        if status >= 0x17:
            out.write(f'barco,{i},{status}\n')
            continue
        cargo = ','.join(str(o.s32(s + 0x54 + 4 * w)) for w in range(24))
        prices = ','.join(f'{o.u32(s + 0xB4 + 4 * w):08x}' for w in range(24))
        name = bytes(o.uc.mem_read(s + 0x160, 32)).split(b'\0')[0].decode('latin-1')
        out.write(f'barco,{i},{status},{o.u8(s)},{o.u8(s + 0xE)},{o.u8(s + 0xF)},{o.u16(s + 4)},{o.s32(s + 0x10)},'
                  f'{o.s32(s + 0x18)},{o.s32(s + 0x14)},{o.s32(s + 0x1C)},{o.s32(s + 0x20)},{o.u8(s + 0x37)},'
                  f'{o.u8(s + 0x38)},{o.u8(s + 0x39)},{o.u16(s + 0x40)},{o.s32(s + 0x118)},{o.s32(s + 0x11C)},'
                  f'{cargo},{prices},{name}\n')


def random_amount(rng, ware):
    unit = 2000 if ware in LOAD_WARES else 200
    return rng.choice([unit, unit * rng.randint(1, 10), unit * rng.randint(10, 200), rng.randint(1, 5 * unit),
                       10 ** 7])


def run_trades(o, count, out, seed=1300):
    """Compras y ventas aleatorias con los barcos atracados de comerciantes."""
    rng = random.Random(seed)
    towns, _, merchants, ships = o.counts()
    docked = []
    for i in range(ships):
        s = o.ship(i)
        if o.u16(s + 0x134) not in (0x0F, 0x12) and o.u16(s + 0x134) < 0x17 and o.u8(s + 0x39) < towns \
                and o.u8(s) < merchants:
            docked.append(i)
    for _ in range(count):
        ship = rng.choice(docked)
        owner = o.u8(o.ship(ship))
        town = o.u8(o.ship(ship) + 0x39)
        if rng.random() < 0.03:
            town = rng.randrange(towns)  # comprar no debe funcionar fuera de su puerto
        ware = rng.randrange(TRADE_WARES)
        if rng.random() < 0.1:
            money = rng.choice([0, rng.randint(1, 2000), rng.randint(2000, 50_000)])
            o.write_u32(o.merchant(owner), money)
            out.write(f'dinero,{owner},{money}\n')
        amount = random_amount(rng, ware)
        cargo = o.s32(o.ship(ship) + 0x54 + 4 * ware)
        stock = o.s32(o.town(town) + 4 + 4 * ware)
        money = o.s32(o.merchant(owner))
        if cargo > 0 and rng.random() < 0.45:
            difficulty = rng.randrange(3)
            o.set_difficulty(difficulty)
            amount = rng.choice([amount, cargo, rng.randint(1, cargo)])
            o.trade(SELL_FROM_SHIP, ship, town, ware, amount)
            kind = 'venta'
        else:
            difficulty = 1
            o.trade(BUY_TO_SHIP, ship, town, ware, amount)
            kind = 'compra'
        # Lo que se ha comerciado de verdad: lo que cambian el almacén y el dinero.
        new_stock = o.s32(o.town(town) + 4 + 4 * ware)
        new_money = o.s32(o.merchant(owner))
        state = [abs(new_stock - stock), abs(new_money - money), new_money, new_stock] + o.ship_state(ship, ware)
        out.write(f'{kind},{ship},{town},{ware},{difficulty},{amount},{",".join(map(str, state))}\n')


if __name__ == '__main__':
    if len(sys.argv) not in (4, 5):
        print(__doc__)
        sys.exit(2)
    oracle = SaveOracle(sys.argv[1], open(sys.argv[2], 'rb').read())
    oracle.load()
    with open(sys.argv[3], 'w', encoding='utf-8') as f:
        dump_state(oracle, f)
        run_trades(oracle, int(sys.argv[4]) if len(sys.argv) == 5 else 2000, f)
    print('estado y operaciones escritos en', sys.argv[3])
