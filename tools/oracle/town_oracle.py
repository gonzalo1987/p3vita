"""Oráculo de las ciudades: carga una partida con el código de Patrician3.exe en
Unicorn (ver save_oracle.py), hace pasar los días con el tick de ciudad del
propio juego (0x51BA10) y graba «sondas»: cada vez que se entra en una
función elegida, el estado de antes y el de después. tools/oracle/tick_probe.cpp
repite cada llamada con src/p3data y compara (ver docs/oracle.md).

El estado son copias de la memoria del juego, por regiones:
  G      globales de 0x6DD000 a 0x6E0000 (mundo 0x6DE4A0, barcos 0x6DD7A0...)
  A      ajustes de la partida (*0x6CC3E8, 0x4C bytes)
  T O M  ciudades (0x9F8), oficinas (0x44C) y comerciantes (0x650)
  P C    barcos (0x180) y capitanes (16)
  W      registros de 0x14 bytes del mundo (*0x6DE510, tantos como [0x6DE4A6])
  X      objetos de asedio de cada ciudad (*0x6DF270, 0x1024)
  E      cola de eventos (*0x6DD73C, tantos registros de 0x18 como [0x6DD748])
  Y      tiempos de viaje entre ciudades ([+0x6C] del mundo, n × n bytes)
  V      convoyes (*0x6DD7A8, tantos registros de 0x3C como [0x6DD896])
  Q      rutas de los barcos: por cada barco con [+0x24], u16 barco, u32 nº de
         puntos y los puntos (i16 x, i16 y)
  K0..K7 los 40 bytes de cada hueco del objeto 0x6DE588 (*[0x6DEF90 + 4·i]; vacío si no hay)
  Ri Di Ni  registros de edificio, rejilla y nodos (0x14) de la ciudad i; en
         cada nodo, el puntero al siguiente se cambia por su propia dirección
  r      estado de rand()

Formato del fichero (enteros little endian):
  "P3PROBE1", u32 nº de regiones y cada una: nombre (u8 n + texto), u32
  dirección en el emulador, u32 tamaño, bytes.
  Después, muestras: u8 'S', u32 función, u32 tick, u32 ecx, u32 esp (con
  la dirección de retorno), 8 × u32 argumentos de la pila, diferencias hasta el estado de antes, u32 eax,
  diferencias hasta el de después, u32 nº de operaciones encoladas y cada una
  (u32 n + bytes). Diferencias: u32 nº y cada una nombre, u32 dirección,
  u32 offset (0xFFFFFFFF: región entera nueva), u32 tamaño, bytes.
  Al final, u8 'E'.

uso: python3 town_oracle.py <Patrician3.exe> <partida.raw> <salida.probe> <días> <función>[,<función>...]
       [--desde DÍA] [--max N] [--semilla S] [--barcos DATOS [--sin-ia] [--sin-convoyes] [--sin-comerciantes]]
La función 51ba10 graba los ticks de ciudad enteros.

Con --barcos (la carpeta de --navdata de save_oracle.py), cada tick hace
además la actualización de los barcos del juego (0x506720) con los estados
que reproduce p3vita (en puerto, 0 a 5; en el mar, 0x0F; los convoyes de los
comerciantes, pero no los de los piratas, que se quedan quietos, ni los
encuentros de 0x5053C0) y, al final del tick, ejecuta las órdenes de zarpar
(operación 0x00, 0x537670) que se han encolado. Con --sin-convoyes se salta
todo el recorrido de los convoyes. También hace el tick de cada comerciante
(0x4F7BB0), como advance_time: en los ticks con t & 7 == 1 el comerciante
t >> 3 y con t & 7 == 6 el (t + 0x100) >> 3, antes de los barcos
(--sin-comerciantes lo salta). Para repetir el mundo con tick_probe
--continuo hay que grabar también 4f7bb0. El juego ejecuta la cola entre dos llamadas a advance_time,
que hace hasta 8 ticks según el tiempo real; aquí y en p3vita, tras cada tick.
Las funciones 506720 y 537670 también se pueden grabar. Con --sin-ia, los
barcos como en sail_oracle.py: en puerto solo los estados 1, 2, 3 y el 0 de
los jugadores humanos, y las órdenes encoladas no se ejecutan.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from save_oracle import FILE, SENTINEL, SHIPS, WORLD, SaveOracle  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

TOWN_TICK = 0x51BA10
MERCHANT_TICK = 0x4F7BB0  # thiscall sobre el comerciante, con su índice
UPDATE_SHIPS = 0x506720
SET_SAIL = 0x537670  # operación 0x00
UPDATE_DATE = 0x5310D0
RAND = 0x639C03
MERGE_TOWN_FLAGS = 0x5348B0  # la carga lo llama con 0x6DF278 tras leer el mundo
SETTINGS_POINTER = 0x6CC3E8
SETTINGS_SIZE = 0x4C
PLAYER = 0x6DFC14
SIEGES = 0x6DF270
EVENTS = 0x6DD73C  # cola de eventos: puntero al array, cabeza u16 en +8, número u16 en +0xC
PLAYER_TOWNS = 0x6DEF90  # 8 punteros a 40 bytes (un byte por ciudad) del objeto 0x6DE588
GLOBALS = (0x6DD000, 0x6E0000)
ENQUEUE = 0x54AA70  # encola una operación: se anota y no se ejecuta
OPERATION_SIZE = 0x14  # lo que copia 0x543F10; lo que sigue en la pila es basura
# El tick de ciudad se llama por este trampolín para que vuelva a una
# instrucción de verdad y no a SENTINEL (donde la emulación se para sin pasar
# por los ganchos): si no, Recorder no ve la salida de 0x51BA10.
TRAMPOLINE = SENTINEL + 0x10
SHIPS_TRAMPOLINE = SENTINEL + 0x20
SAIL_TRAMPOLINE = SENTINEL + 0x30
MERCHANT_TRAMPOLINE = SENTINEL + 0x40
# Actualización de barcos (ver sail_oracle.py): saltos por estado y convoyes.
DOCKED_DISPATCH = 0x5067C0
DOCKED_SKIP = 0x5068C6
SAILING_DISPATCH = 0x506C4C
SAILING_SKIP = 0x506CC3
CONVOYS = 0x5070FD
CONVOY_DISPATCH = 0x50719D  # salto por el estado del convoy (ebp: el convoy)
CONVOY_SKIP = 0x507612  # el estado 0x13: pasa al siguiente convoy
ENCOUNTERS = 0x5079DA  # cada 8 ticks, 0x5053C0 (encuentros): se salta
EPILOGUE = 0x5079E8
DOCKED_STATES = (0, 1, 2, 3, 4, 5)

# Funciones de interfaz que no cambian el estado del mundo: dirección →
# bytes de argumentos que sacan al volver.
QUIET = {
    0x548CA0: 0x18,  # mensaje al jugador
    0x613920: 4,  # escena del puerto (0x6E6220)
    0x4D6530: 8,  # mensaje a uno o varios comerciantes (buzón 0x6DD730)
}
# Con barcos, además (ver sail_oracle.py):
QUIET_SHIPS = {
    0x44F940: 4,  # avisa a la interfaz de que un barco del jugador ha cambiado
    0x453DF0: 0,  # interfaz
    0x6132C0: 4, 0x6135C0: 8,  # escena del puerto
    0x514000: 4,  # objetos del final de la sección de barcos
    0x51A2C0: 8, 0x4D5900: 8, 0x518860: 4,  # IA ante un puerto bloqueado; ruta automática (no se reproducen)
}


def trampoline(target, args):
    """push dword [esp+4·args] (args veces); call target; ret 4·args"""
    code = bytes([0xFF, 0x74, 0x24, 4 * args]) * args
    return code, lambda at: code + bytes([0xE8]) + struct.pack('<i', target - (at + len(code) + 5)) + \
        bytes([0xC2, 4 * args, 0x00])


class TownOracle(SaveOracle):
    def __init__(self, exe, save, seed=1, navdata=None, ai=True, convoys=True, merchants=True):
        super().__init__(exe, save, navdata)
        self.ai = ai
        self.merchants = merchants and navdata is not None
        self.rand_state = seed
        self.queued = []  # para las muestras
        self.tick_operations = []  # las órdenes que se ejecutan al final del tick
        self.ships = navdata is not None
        quiet = {**QUIET, ENQUEUE: 4}
        if self.ships:
            quiet.update(QUIET_SHIPS)
            self.uc.hook_add(UC_HOOK_CODE, self._docked, begin=DOCKED_DISPATCH, end=DOCKED_DISPATCH)
            self.uc.hook_add(UC_HOOK_CODE, self._sailing, begin=SAILING_DISPATCH, end=SAILING_DISPATCH)
            if convoys:
                self.uc.hook_add(UC_HOOK_CODE, self._convoy, begin=CONVOY_DISPATCH, end=CONVOY_DISPATCH)
                self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=ENCOUNTERS, end=ENCOUNTERS, user_data=EPILOGUE)
            else:
                self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=CONVOYS, end=CONVOYS, user_data=EPILOGUE)
            for at, target, args in ((SHIPS_TRAMPOLINE, UPDATE_SHIPS, 1), (SAIL_TRAMPOLINE, SET_SAIL, 0),
                                     (MERCHANT_TRAMPOLINE, MERCHANT_TICK, 1)):
                _, build = trampoline(target, args)
                self.uc.mem_write(at, build(at))
        for address, pop in quiet.items():
            self.skipped[address] = pop
            self.uc.hook_add(UC_HOOK_CODE, self._quiet, begin=address, end=address)
        self.uc.hook_add(UC_HOOK_CODE, self._rand, begin=RAND, end=RAND)
        # push dword [esp+4]; call 0x51BA10; ret 4
        call = struct.pack('<i', TOWN_TICK - (TRAMPOLINE + 4 + 5))
        self.uc.mem_write(TRAMPOLINE, bytes([0xFF, 0x74, 0x24, 0x04, 0xE8]) + call + bytes([0xC2, 0x04, 0x00]))

    def _quiet(self, uc, address, size, user_data):
        if address == ENQUEUE:
            op = bytes(uc.mem_read(self._arg(0), OPERATION_SIZE))
            self.queued.append(op)
            self.tick_operations.append(op)
        self._stub(uc, address, size, user_data)

    def _docked(self, uc, address, size, user_data):
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESI
        status = uc.reg_read(UC_X86_REG_ECX)
        if self.ai:
            allowed = status in DOCKED_STATES
        else:
            owner = self.u8(uc.reg_read(UC_X86_REG_ESI))
            human = owner < self.counts()[2] and self.u16(self.merchant(owner) + 8) == 0
            allowed = status in (1, 2, 3) or (status == 0 and human)
        if not allowed:
            uc.reg_write(UC_X86_REG_EIP, DOCKED_SKIP)

    def _sailing(self, uc, address, size, user_data):
        from unicorn.x86_const import UC_X86_REG_ESI
        if self.u16(uc.reg_read(UC_X86_REG_ESI) + 0x134) != 0x0F:
            uc.reg_write(UC_X86_REG_EIP, SAILING_SKIP)

    def _convoy(self, uc, address, size, user_data):
        # Los convoyes de los piratas (el que los guía no es de un comerciante)
        # se quedan quietos: sus ataques acaban en batallas, que el emulador no
        # puede ejecutar.
        from unicorn.x86_const import UC_X86_REG_EBP
        if self.u8(uc.reg_read(UC_X86_REG_EBP)) >= self.counts()[2]:
            uc.reg_write(UC_X86_REG_EIP, CONVOY_SKIP)

    def _jump(self, uc, address, size, target):
        uc.reg_write(UC_X86_REG_EIP, target)

    def _rand(self, uc, address, size, user_data):
        # rand() de MSVC (0x639C03): el estado es por hilo y en el emulador no
        # hay hilos, así que se lleva aquí.
        self.rand_state = (self.rand_state * 0x343FD + 0x269EC3) & 0xFFFFFFFF
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EAX, (self.rand_state >> 16) & 0x7FFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, self.u32(esp))

    def load(self):
        """Lo que hace la carga de una partida (0x549F40 y su llamador) con lo
        que usan los ticks de ciudad."""
        settings = self.alloc(0x100)
        block = bytearray(self.save[4:4 + SETTINGS_SIZE])
        block[0x3C:0x4C] = bytes(0x10)
        if block[0xF] != 6 and block[8] > 14:
            block[8] = 14
        self.uc.mem_write(settings, bytes(block))
        self.write_u32(SETTINGS_POINTER, settings)
        super().load()
        # Lo que la carga lee después de los barcos: encargos (0x4D6350),
        # 0x4D4BC0 y la cola de eventos (0x4D81E0). Esta carga una tabla de
        # textos del disco (0x511270) que aquí se salta.
        self.skipped[0x511270] = 4
        hook = self.uc.hook_add(UC_HOOK_CODE, self._stub, begin=0x511270, end=0x511270)
        version = struct.unpack_from('<I', self.save, 0)[0] >> 16
        pos = self.ships_end + 4
        pos = self.call(0x4D6350, [FILE + pos], ecx=0x6DD730) - FILE + 4
        pos = self.call(0x4D4BC0, [FILE + pos], ecx=0x6DD728) - FILE + 4
        self.call(0x4D81E0, [FILE + pos, version], ecx=EVENTS)
        self.uc.hook_del(hook)
        self.call(MERGE_TOWN_FLAGS, [], ecx=0x6DF278)
        self.call(0x52E1A0, [block[0x10]], ecx=0x6DE3D8)  # dificultad del comercio
        _, _, merchants, _ = self.counts()
        player = max((i for i in range(merchants) if self.u16(self.merchant(i) + 8) == 0), default=0)
        self.write_u32(PLAYER, player)

    def town_tick(self, i):
        self.call(TRAMPOLINE, [self.u16(WORLD + 4)], ecx=self.town(i))

    def advance_tick(self):
        """Un tick de advance_time (0x530E80) con la fecha y las ciudades."""
        tick = (self.u32(WORLD + 0x14) + 1) & 0xFFFFFFFF
        self.write_u32(WORLD + 0x14, tick)
        t = tick & 0xFF
        if t == 0:
            self.call(UPDATE_DATE, [], ecx=WORLD)
        town = t >> 3 if t & 7 == 7 else (t + 0xFF) >> 3 if t & 7 == 3 else None
        if town is not None and town < self.u32(WORLD + 0x10):
            self.town_tick(town)
        merchant = t >> 3 if t & 7 == 1 else (t + 0x100) >> 3 if t & 7 == 6 else None
        if self.merchants and merchant is not None and merchant < self.u16(WORLD + 0xA):
            self.call(MERCHANT_TRAMPOLINE, [merchant], ecx=self.merchant(merchant))
        if self.ships:
            self.call(SHIPS_TRAMPOLINE, [tick], ecx=SHIPS)
            # Las órdenes de zarpar encoladas en este tick (las de las obras de
            # las ciudades no se ejecutan).
            operations, self.tick_operations = self.tick_operations, []
            for op in operations if self.ai else []:
                if struct.unpack_from('<I', op, 0)[0] == 0:
                    buffer = self.alloc(OPERATION_SIZE)
                    self.uc.mem_write(buffer, op)
                    self.call(SAIL_TRAMPOLINE, [], ecx=buffer)
            self.tick_operations = []
        return tick

    # Estado por regiones.
    def regions(self):
        """{nombre: (dirección, bytes)}"""
        towns, offices, merchants, ships = self.counts()
        out = {}

        def add(name, address, size):
            out[name] = (address, bytes(self.uc.mem_read(address, size)) if size else b'')

        add('G', GLOBALS[0], GLOBALS[1] - GLOBALS[0])
        add('A', self.u32(SETTINGS_POINTER), SETTINGS_SIZE)
        add('T', self.town(0), towns * 0x9F8)
        add('O', self.office(0), offices * 0x44C)
        add('M', self.merchant(0), merchants * 0x650)
        add('P', self.ship(0), ships * 0x180)
        add('C', self.u32(SHIPS), self.u16(SHIPS + 0xF2) * 16)
        add('W', self.u32(WORLD + 0x70), self.u16(WORLD + 6) * 0x14)
        if self.u32(SIEGES):
            add('X', self.u32(SIEGES), towns * 0x1024)
        add('E', self.u32(EVENTS), self.u16(EVENTS + 0xC) * 0x18 if self.u32(EVENTS) else 0)
        add('Y', self.u32(WORLD + 0x6C), towns * towns if self.u32(WORLD + 0x6C) else 0)
        add('V', self.u32(SHIPS + 8), self.u16(SHIPS + 0xF6) * 0x3C if self.u32(SHIPS + 8) else 0)
        routes = bytearray()
        for i in range(ships):
            route = self.u32(self.ship(i) + 0x24)
            if route:
                points, count = self.u32(route), self.u32(route + 4)
                routes += struct.pack('<HI', i, count) + bytes(self.uc.mem_read(points, 4 * count))
        out['Q'] = (0, bytes(routes))
        for i in range(8):
            p = self.u32(PLAYER_TOWNS + 4 * i)
            add(f'K{i}', p, 40 if p else 0)
        for i in range(towns):
            t = self.town(i)
            add(f'R{i}', self.u32(t + 0x75C), self.u16(t + 0x776) * 8)
            add(f'D{i}', self.u32(t + 0x80C), self.u32(t + 0x7A8) * self.u32(t + 0x7AC))
            nodes = bytearray()
            node = self.u32(t + 0x9B4)
            while node:
                nodes += struct.pack('<I', node) + bytes(self.uc.mem_read(node + 4, 0x10))
                node = self.u32(node)
            out[f'N{i}'] = (self.u32(t + 0x9B4), bytes(nodes))
        out['r'] = (0, struct.pack('<I', self.rand_state))
        return out


def write_regions(f, regions):
    f.write(struct.pack('<I', len(regions)))
    for name, (address, data) in regions.items():
        f.write(bytes([len(name)]) + name.encode() + struct.pack('<II', address, len(data)) + data)


def write_diff(f, old, new):
    """Diferencias entre dos estados, en trozos de 64 bytes."""
    chunks = []
    for name, (address, data) in new.items():
        before = old.get(name)
        if before is None or len(before[1]) != len(data) or before[0] != address:
            chunks.append((name, address, 0xFFFFFFFF, data))
            continue
        prev = before[1]
        if prev == data:
            continue
        start = None
        for i in range(0, len(data), 64):
            same = prev[i:i + 64] == data[i:i + 64]
            if not same and start is None:
                start = i
            elif same and start is not None:
                chunks.append((name, address, start, data[start:i]))
                start = None
        if start is not None:
            chunks.append((name, address, start, data[start:]))
    f.write(struct.pack('<I', len(chunks)))
    for name, address, offset, data in chunks:
        f.write(bytes([len(name)]) + name.encode() + struct.pack('<III', address, offset, len(data)) + data)


class Recorder:
    """Graba una muestra en cada entrada a las funciones elegidas.

    Cada muestra se escribe entera al volver de la función (antes, después y
    las operaciones encoladas entre medias), así que también se pueden grabar
    a la vez una función y otras que ella llama: las de dentro salen antes en
    el fichero. Las diferencias son bytes absolutos, así que da igual que el
    estado de antes de la de fuera sea anterior al de después de la de dentro.
    """

    def __init__(self, oracle, functions, out, limit):
        self.o = oracle
        self.out = out
        self.limit = limit
        self.count = 0
        self.active = False
        self.pending = 0
        self.state = oracle.regions()
        write_regions(out, self.state)
        for f in functions:
            oracle.uc.hook_add(UC_HOOK_CODE, self._enter, begin=f, end=f)
            # Unicorn no aplica un gancho nuevo al código que ya ha traducido.
            oracle.uc.ctl_remove_cache(f, f + 1)

    def _enter(self, uc, address, size, user_data):
        if not self.active or self.count >= self.limit:
            return
        o = self.o
        esp = uc.reg_read(UC_X86_REG_ESP)
        ret = o.u32(esp)
        header = b'S' + struct.pack('<IIII8I', address, o.u32(WORLD + 0x14), uc.reg_read(UC_X86_REG_ECX), esp,
                                    *(o.u32(esp + 4 + 4 * k) for k in range(8)))
        before = o.regions()
        if self.pending == 0:
            o.queued.clear()
        first_operation = len(o.queued)
        self.pending += 1
        self.count += 1
        hook = []

        def leave(uc2, address2, size2, user_data2):
            # Vuelve cuando la pila está por encima de la dirección de retorno.
            if uc2.reg_read(UC_X86_REG_ESP) <= esp:
                return
            after = o.regions()
            self.out.write(header)
            write_diff(self.out, self.state, before)
            self.out.write(struct.pack('<I', uc2.reg_read(UC_X86_REG_EAX)))
            write_diff(self.out, before, after)
            self.state = after
            operations = o.queued[first_operation:]
            self.out.write(struct.pack('<I', len(operations)))
            for op in operations:
                self.out.write(struct.pack('<I', len(op)) + op)
            self.pending -= 1
            uc2.hook_del(hook[0])

        hook.append(uc.hook_add(UC_HOOK_CODE, leave, begin=ret, end=ret))
        uc.ctl_remove_cache(ret, ret + 1)


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('exe')
    p.add_argument('save')
    p.add_argument('out')
    p.add_argument('days', type=int)
    p.add_argument('functions')
    p.add_argument('--desde', type=int, default=0, help='primer día en el que se graba')
    p.add_argument('--max', type=int, default=1 << 30, help='muestras como mucho')
    p.add_argument('--semilla', type=int, default=1)
    p.add_argument('--barcos', metavar='DATOS', help='también los barcos (carpeta con navdata/ y scripts/)')
    p.add_argument('--sin-ia', action='store_true', help='con --barcos, sin las decisiones de la IA en puerto')
    p.add_argument('--sin-convoyes', action='store_true', help='con --barcos, sin el recorrido de los convoyes')
    p.add_argument('--sin-comerciantes', action='store_true', help='con --barcos, sin el tick de los comerciantes')
    a = p.parse_args()
    o = TownOracle(a.exe, open(a.save, 'rb').read(), a.semilla, a.barcos, not a.sin_ia, not a.sin_convoyes,
                   not a.sin_comerciantes)
    o.load()
    functions = [int(x, 16) for x in a.functions.split(',')]
    with open(a.out, 'wb') as f:
        f.write(b'P3PROBE1')
        rec = Recorder(o, functions, f, a.max)
        for day in range(a.days):
            rec.active = day >= a.desde
            for _ in range(256):
                o.advance_tick()
            if rec.count >= a.max:
                break
        f.write(b'E')
    print(f'{rec.count} muestras en {a.out}')


if __name__ == '__main__':
    main()
