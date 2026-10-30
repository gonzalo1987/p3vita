"""Oráculo de precios: ejecuta get_buy_price / get_sell_price de Patrician3.exe en Unicorn.

Solo se emula la CPU con las secciones del ejecutable en memoria: no se carga
Windows ni ninguna DLL, y si el código sale de las funciones (por ejemplo, hacia
una importación) la emulación se aborta. Sirve para obtener la verdad del juego
y compararla con src/p3data/prices.cpp (ver docs/oracle.md).

uso: python3 price_oracle.py <Patrician3.exe> <casos> <salida.csv>
Requiere el paquete unicorn (pip install --user unicorn) y el ejecutable de la
versión GOG 1.1 (MD5 baf7e0f931ce3d35c1ba3f5d81eb990a).
"""
import random
import site
import struct
import sys

sys.path.append(site.getusersitepackages())
from unicorn import UC_ARCH_X86, UC_HOOK_MEM_UNMAPPED, UC_MODE_32, Uc, UcError  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_FPCW  # noqa: E402

GET_BUY_PRICE = 0x52E430
GET_SELL_PRICE = 0x52E1D0
TOWNS_POINTER = 0x6DE508  # game_world + 0x68: puntero al array de ciudades
SELL_CONTEXT = 0x6DE3D8   # objeto que get_sell_price recibe en ecx; factor de dificultad en +0x64
TOWN_SIZE = 0x9F8
DIFFICULTY_FACTORS = (2.2, 2.0, 1.8)
LOAD_WARES = (0, 1, 2, 11, 14, 16, 17, 19)  # el resto se cuenta en barriles (o piezas)

STACK = 0x0100_0000
TOWNS = 0x0200_0000
SENTINEL = 0x0300_0000  # dirección de retorno: al llegar aquí, la función ha terminado


class PriceOracle:
    def __init__(self, exe):
        data = open(exe, 'rb').read()
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        sections = struct.unpack_from('<H', data, pe + 6)[0]
        optional_size = struct.unpack_from('<H', data, pe + 20)[0]
        base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
        image_size = struct.unpack_from('<I', data, pe + 24 + 56)[0]
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(base, (image_size + 0xFFF) & ~0xFFF)
        self.uc.mem_write(base, data[:0x1000])
        for i in range(sections):
            o = pe + 24 + optional_size + i * 40
            vsize, va, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
            self.uc.mem_write(base + va, data[raw:raw + min(rsize, max(vsize, rsize))])
        self.uc.mem_map(STACK, 0x10_0000)
        self.uc.mem_map(TOWNS, 0x10_0000)
        self.uc.mem_map(SENTINEL, 0x1000)
        self.uc.mem_write(TOWNS_POINTER, struct.pack('<I', TOWNS))
        self.uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._unmapped)
        self.set_difficulty(1)

    def _unmapped(self, uc, access, address, size, value, user_data):
        raise RuntimeError(f'acceso fuera de memoria en {address:#x}')

    def set_difficulty(self, difficulty):
        """0 baja, 1 normal, 2 alta."""
        self.uc.mem_write(SELL_CONTEXT + 0x64, struct.pack('<f', DIFFICULTY_FACTORS[difficulty]))

    def _set_town(self, ware, stock, thresholds, market, taxed):
        town = bytearray(TOWN_SIZE)
        struct.pack_into('<I', town, 0x76C, 0x100 if market else 0)
        town[0x6F0] = ware if taxed else 0xFF  # mercancía con recargo del 15 % (0xFF = ninguna)
        struct.pack_into('<i', town, 0x4 + ware * 4, stock)
        struct.pack_into('<4I', town, 0x4F0 + ware * 16, *thresholds)
        self.uc.mem_write(TOWNS, bytes(town))

    def _call(self, address, args, ecx=0):
        esp = STACK + 0x10_0000 - 0x1000
        frame = struct.pack('<I', SENTINEL) + b''.join(struct.pack('<I', a & 0xFFFFFFFF) for a in args)
        esp -= len(frame)
        self.uc.mem_write(esp, frame)
        self.uc.reg_write(UC_X86_REG_ESP, esp)
        self.uc.reg_write(UC_X86_REG_ECX, ecx)
        # El runtime de MSVC deja la FPU a 53 bits (0x27F); el emulador arranca con 64.
        self.uc.reg_write(UC_X86_REG_FPCW, 0x27F)
        try:
            self.uc.emu_start(address, SENTINEL, count=1_000_000)
        except UcError as e:
            raise RuntimeError(f'la emulación falló: {e}') from e
        return struct.unpack('<i', struct.pack('<I', self.uc.reg_read(UC_X86_REG_EAX)))[0]

    def buy(self, ware, stock, amount, thresholds, market=True, taxed=False):
        self._set_town(ware, stock, thresholds, market, taxed)
        return self._call(GET_BUY_PRICE, [ware, 0, amount])

    def sell(self, ware, stock, amount, thresholds, market=True):
        self._set_town(ware, stock, thresholds, market, False)
        return self._call(GET_SELL_PRICE, [ware, 0, amount], ecx=SELL_CONTEXT)


def random_thresholds(rng, unit):
    t0 = rng.randint(unit // 2, 60 * unit)
    t1 = t0 + rng.randint(1, 120 * unit)
    t2 = t1 + rng.randint(1, 80 * unit)
    return (t0, t1, t2, t2 + rng.randint(1, 80 * unit))


def write_cases(oracle, count, path, seed=1300):
    """CSV que lee tools/oracle/compare_prices.cpp."""
    rng = random.Random(seed)
    with open(path, 'w') as out:
        out.write('tipo,mercancia,dificultad,mercado,tasa,existencias,cantidad,t0,t1,t2,t3,precio\n')
        for n in range(count):
            ware = rng.randrange(24)
            unit = 2000 if ware in LOAD_WARES else 200
            th = random_thresholds(rng, unit)
            stock = rng.choice([rng.randint(0, th[3] * 2), rng.randint(0, th[0]), unit * rng.randint(0, 50),
                                rng.randint(0, unit * 2)])
            amount = rng.choice([unit, unit * rng.randint(1, 30), rng.randint(1, 5 * unit),
                                 stock + rng.randint(1, 3 * unit)])
            market = rng.random() > 0.03
            if n % 2 == 0:
                taxed = rng.random() < 0.1
                price = oracle.buy(ware, stock, amount, th, market, taxed)
                out.write(f'compra,{ware},1,{int(market)},{int(taxed)},{stock},{amount},{",".join(map(str, th))},'
                          f'{price}\n')
            else:
                difficulty = rng.randrange(3)
                oracle.set_difficulty(difficulty)
                price = oracle.sell(ware, stock, amount, th, market)
                out.write(f'venta,{ware},{difficulty},{int(market)},0,{stock},{amount},{",".join(map(str, th))},'
                          f'{price}\n')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(2)
    write_cases(PriceOracle(sys.argv[1]), int(sys.argv[2]), sys.argv[3])
    print('casos escritos en', sys.argv[3])
