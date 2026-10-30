"""Oráculo de navegación: carga una partida con el código de Patrician3.exe en
Unicorn (ver save_oracle.py) y hace pasar el tiempo con la actualización de
barcos del propio juego (0x506720), dando de vez en cuando órdenes de zarpar
(operación 0x00, 0x537670). Anota el estado de movimiento de los barcos para
que tools/oracle/compare_sail.cpp lo compare con src/p3data/sailing.cpp.

De la actualización de barcos solo se ejecutan los estados que reproduce
p3vita: en la lista de barcos en puerto, 1 (esperando por un bloqueo),
2 (saliendo del puerto), 3 (entrando) y 0 (atracado) si el dueño es un
jugador humano; en la de barcos en el mar, 0x0F (navegando). El resto (la IA
de los comerciantes, piratas, convoyes, batallas...) se salta, y también los
convoyes que la función recorre después. Las llamadas a la interfaz, a la
escena del puerto, a la cola de mensajes y a las decisiones de la IA se
sustituyen por funciones vacías; las de la IA quedan anotadas como `stub`.

uso: python3 sail_oracle.py <Patrician3.exe> <partida.raw> <datos> <salida.csv> [ticks] [cada]
<datos> es la carpeta de --navdata de save_oracle.py (navdata/ y
scripts/StadtDaten.ini). Se vuelca el estado de todos los barcos cada `cada`
ticks (64 por omisión) y tras cada orden.
"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from save_oracle import SHIPS, WORLD, SaveOracle  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESI  # noqa: E402

UPDATE_SHIPS = 0x506720
UPDATE_DATE = 0x5310D0
SET_SAIL = 0x537670  # operación 0x00
DOCKED_DISPATCH = 0x5067C0  # jmp por estado de la lista de barcos en puerto
DOCKED_SKIP = 0x5068C6  # lea ebx,[esi+6]; jmp al siguiente
SAILING_DISPATCH = 0x506C4C  # jmp por estado de la lista de barcos en el mar
SAILING_SKIP = 0x506CC3
CONVOYS = 0x5070FD  # tras las dos listas: los convoyes
EPILOGUE = 0x5079E8

# Funciones que no cambian nada de lo que se compara: dirección → bytes de
# argumentos que sacan al volver.
QUIET = {
    0x44F940: 4,  # avisa a la interfaz de que un barco del jugador ha cambiado
    0x453DF0: 0,  # interfaz
    0x548CA0: 0x18,  # mensaje al jugador
    0x6132C0: 4, 0x6135C0: 8, 0x613920: 4,  # escena del puerto (0x6E6220)
    0x514000: 4,  # objetos del final de la sección de barcos
}
# Decisiones de la IA y rutas automáticas: se saltan y se anotan.
AI = {
    0x54AA70: 4,  # encola una operación (un barco de la IA sin ruta pide una)
    0x51A2C0: 8,  # nueva ruta de la IA ante un puerto bloqueado
    0x4D5900: 8,
    0x518860: 4,  # ruta automática de un barco del jugador
}
DOCKED_STATES = (1, 2, 3)


class SailOracle(SaveOracle):
    def __init__(self, exe, save, navdata):
        super().__init__(exe, save, navdata)
        self.events = []
        matrix = open(os.path.join(navdata, 'navdata', 'nav_matrix.dat'), 'rb').read()
        width, height = struct.unpack_from('<HH', matrix, 0)
        self.water = [(i % width, i // width) for i in range(width * height) if matrix[4 + i] == 0]
        for address, pop in {**QUIET, **AI}.items():
            self.skipped[address] = pop
            self.uc.hook_add(UC_HOOK_CODE, self._stub_logged, begin=address, end=address)
        self.uc.hook_add(UC_HOOK_CODE, self._dispatch_docked, begin=DOCKED_DISPATCH, end=DOCKED_DISPATCH)
        self.uc.hook_add(UC_HOOK_CODE, self._dispatch_sailing, begin=SAILING_DISPATCH, end=SAILING_DISPATCH)
        self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=CONVOYS, end=CONVOYS, user_data=EPILOGUE)

    def _stub_logged(self, uc, address, size, user_data):
        if address in AI:
            self.events.append(address)
        self._stub(uc, address, size, user_data)

    def human(self, owner):
        return owner < self.counts()[2] and self.u16(self.merchant(owner) + 8) == 0

    def _dispatch_docked(self, uc, address, size, user_data):
        ship = uc.reg_read(UC_X86_REG_ESI)
        status = uc.reg_read(UC_X86_REG_ECX)
        if status not in DOCKED_STATES and not (status == 0 and self.human(self.u8(ship))):
            uc.reg_write(UC_X86_REG_EIP, DOCKED_SKIP)

    def _dispatch_sailing(self, uc, address, size, user_data):
        if self.u16(uc.reg_read(UC_X86_REG_ESI) + 0x134) != 0x0F:
            uc.reg_write(UC_X86_REG_EIP, SAILING_SKIP)

    def _jump(self, uc, address, size, target):
        uc.reg_write(UC_X86_REG_EIP, target)

    def tick(self):
        """Un tick de advance_time (0x530E80), solo con la fecha y los barcos."""
        tick = (self.u32(WORLD + 0x14) + 1) & 0xFFFFFFFF
        self.write_u32(WORLD + 0x14, tick)
        if tick & 0xFF == 0:
            self.call(UPDATE_DATE, [], ecx=WORLD)
        self.call(UPDATE_SHIPS, [tick], ecx=SHIPS)
        return tick

    def set_sail(self, ship, town, x=0, y=0):
        op = self.alloc(20)
        self.uc.mem_write(op, struct.pack('<IIIii', 0, ship, town, x, y))
        self.call(SET_SAIL, [], ecx=op)

    def route(self, s):
        pointer = self.u32(s + 0x24)
        if pointer == 0:
            return None
        points, count = self.u32(pointer), self.u32(pointer + 4)
        return [v for k in range(count) for v in struct.unpack('<hh', self.uc.mem_read(points + 4 * k, 4))]


def ship_line(o, tick, i):
    s = o.ship(i)
    route = o.route(s)
    fields = [o.u16(s + 0x134), o.s32(s + 0x1C), o.s32(s + 0x20), o.s32(s + 0x28), o.s32(s + 0x2C), o.s32(s + 0x30),
              o.u16(s + 0x34), o.u16(s + 2), o.u8(s + 1), o.s32(s + 0x18), o.u8(s + 0x37), o.u8(s + 0x38),
              o.u8(s + 0x39), o.u8(s + 0x36), o.u16(s + 0x138), o.u32(s + 0x44), o.u32(s + 0x48), o.u8(s + 0x15C),
              o.u16(s + 0x3E), o.u16(s + 6), o.u16(s + 0x3A), f'{o.u32(s + 0x12C):08x}', o.u16(s + 0x130),
              -1 if route is None else len(route) // 2]
    return f'nav,{tick},{i},{",".join(map(str, fields))}\n', route


class Dumper:
    def __init__(self, oracle, out):
        self.o = oracle
        self.out = out
        self.routes = {}

    def ship(self, tick, i):
        line, route = ship_line(self.o, tick, i)
        self.out.write(line)
        if route is not None and self.routes.get(i) != route:
            self.out.write(f'ruta,{tick},{i},{",".join(map(str, route))}\n')
        self.routes[i] = route

    def all(self, tick):
        o = self.o
        towns, _, _, ships = o.counts()
        self.out.write(f'listas,{tick},{o.u16(SHIPS + 0xE8)},{o.u16(SHIPS + 0xEA)}\n')
        for i in range(ships):
            if o.u16(o.ship(i) + 0x134) < 0x17:
                self.ship(tick, i)
        for t in range(towns):
            base = o.town(t)
            values = [o.s32(base + 0x370 + 4 * w) for w in range(24)] + [o.s32(base + 0x670 + 4 * w)
                                                                          for w in range(24)]
            self.out.write(f'encamino,{tick},{t},{",".join(map(str, values))}\n')


def run(o, ticks, every, out, seed=1300):
    rng = random.Random(seed)
    dump = Dumper(o, out)
    towns, _, merchants, ships = o.counts()
    start = o.u32(WORLD + 0x14)
    out.write(f'inicio,{start}\n')
    dump.all(start)
    for _ in range(ticks):
        tick = o.tick()
        for address in o.events:
            out.write(f'stub,{tick},{address:#x}\n')
        o.events.clear()
        if (tick - start) % every == 0:
            dump.all(tick)
        # De vez en cuando, una orden de zarpar a un barco de un comerciante
        # que no va en convoy ni está en la lista de su ciudad (estados 6 y 8),
        # casi siempre hacia una ciudad y a veces hacia un punto del mar.
        if rng.random() < 1 / 24:
            candidates = [i for i in range(ships) if o.u16(o.ship(i) + 0x134) not in (6, 8)
                          and o.u16(o.ship(i) + 0x134) < 0x17 and o.u8(o.ship(i)) < merchants
                          and o.u16(o.ship(i) + 8) >= o.u16(SHIPS + 0xF6)]
            ship = rng.choice(candidates)
            if rng.random() < 0.9:
                town, x, y = rng.randrange(towns), 0, 0
            else:
                # Un punto de agua: si no hay agua cerca, el juego no da ruta y al
                # salir del puerto busca la ciudad 0xFF fuera de su tabla.
                cx, cy = rng.choice(o.water)
                town, x, y = 0xFF, (cx * 3536 + 500) // 1000 + 0x44, (cy * 3536 + 500) // 1000 + 0x44
            o.set_sail(ship, town, x, y)
            out.write(f'orden,{tick},{ship},{town},{x},{y}\n')
            dump.ship(tick, ship)


if __name__ == '__main__':
    if len(sys.argv) not in (5, 6, 7):
        print(__doc__)
        sys.exit(2)
    oracle = SailOracle(sys.argv[1], open(sys.argv[2], 'rb').read(), sys.argv[3])
    oracle.load()
    with open(sys.argv[4], 'w', encoding='utf-8') as f:
        run(oracle, int(sys.argv[5]) if len(sys.argv) > 5 else 2048, int(sys.argv[6]) if len(sys.argv) > 6 else 64, f)
    print('navegación escrita en', sys.argv[4])
