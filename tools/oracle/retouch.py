"""Estados retocados: graba como town_oracle.py --barcos partiendo de la
partida cambiada tras la carga por un escenario (abajo), para recorrer ramas
que las partidas no recorren solas. tick_probe repite cada llamada partiendo
del estado del juego justo antes de ella (sin --continuo ni --carga: p3vita
no conoce el retoque).

uso: python3 retouch.py <Patrician3.exe> <partida.raw> <salida.probe> <días> <función>[,<función>...]
       <escenario> --barcos DATOS --cpr p2arch0_eng.cpr [--desde DÍA] [--max N] [--semilla S]

Escenarios (la candidatura a Gobernador, docs/formats/world_tick.md §8):
  candidato       un comerciante de la IA con oficina pasa a ser alcalde de la
                  primera ciudad que mira su tick (0x4F7BB0 → 0x4F75F0) en su
                  primer tick, con [+0x37] = 0 y reputación de sobra para optar
                  (el tercer componente a 20 en todas las ciudades): la
                  candidatura (0x5326E0) sale en ese tick y sus operaciones
                  0x86 y 0x85 se ejecutan al final
  jugador         lo mismo con el jugador humano
  dos_candidatos  dos comerciantes de la IA, cada uno en su ciudad: la
                  segunda candidatura encuentra los encargos de la primera
                  (las 0x85 no crean nada)
  sin_carretera   candidato y todas las ciudades con el bit 0x8000 de [0x76C]:
                  no hay encargo de carretera
  encargos_viejos para la partida 2, que guarda 4 encargos sin dar: candidato,
                  el de la carretera deja de valer (0x8000 en una de sus
                  ciudades: la 0x86 lo anula) y el de la ciudad tiene
                  comerciante (la 0x86 no lo mira). Ese encargo pasa por la
                  parte del evento 0x32 con comerciante, que p3vita aún no
                  tiene: aquí no se graba 4d85c0
Graba, por ejemplo, 4f7bb0,5326e0,535760,4d85c0 durante 3 días. Al final
escribe los encargos de la Hansa (eventos 0x32) que quedan.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import town_oracle as to  # noqa: E402

WORLD = to.WORLD
TOWN_SIZE = 0x9F8


def w8(o, a, v):
    o.uc.mem_write(a, bytes([v & 0xFF]))


def w16(o, a, v):
    o.uc.mem_write(a, struct.pack('<H', v & 0xFFFF))


def w32(o, a, v):
    o.uc.mem_write(a, struct.pack('<I', v & 0xFFFFFFFF))


def wf(o, a, v):
    o.uc.mem_write(a, struct.pack('<f', v))


def events(o):
    """(índice, dirección) de cada evento de la cola, en su orden."""
    base, head, count = o.u32(to.EVENTS), o.u16(to.EVENTS + 8), o.u16(to.EVENTS + 0xC)
    out, i = [], head
    while i < count:
        out.append((i, base + 0x18 * i))
        i = o.u16(base + 0x18 * i + 4)
    return out


def owner(o, m):
    """El comerciante como lo ve 0x4F75F0: el dueño de su primera oficina, o 0."""
    first = o.u16(o.merchant(m) + 0xC)
    return o.u8(o.office(first) + 0x2C4) if first < o.u16(WORLD + 8) else 0


def ai_with_office(o):
    """Comerciantes de la IA (sin [+0x8] & 6) cuya primera oficina es suya."""
    merchants = o.u16(WORLD + 0xA)
    return [m for m in range(merchants) if o.u16(o.merchant(m) + 8) and not o.u16(o.merchant(m) + 8) & 6
            and owner(o, m) == m]


def first_rank_town(o, m):
    """La primera ciudad que mira el tick del comerciante m (0x4F7BB0) en su
    próximo tick: (día del año + m) % ciudades, o la de enfrente si es la natal."""
    towns = o.u32(WORLD + 0x10)
    t = o.u32(WORLD + 0x14) & 0xFF
    own = 8 * m + 1 if m < 32 else 8 * (m - 32) + 6
    day = o.u16(WORLD + 4) + (0 if t < own else 1)
    if day >= 365:
        day -= 365
    town = (day + m) % towns
    if town == o.u8(o.merchant(m) + 0x19):
        town += towns >> 1
        if town >= towns:
            town -= towns
    return town


def make_candidate(o, m):
    """El comerciante m, alcalde de la primera ciudad que mira su próximo
    tick, con [+0x37] = 0 y el tercer componente de su reputación a 20."""
    ma = o.merchant(m)
    town = first_rank_town(o, m)
    w8(o, o.town(town) + 0x6F1, owner(o, m))
    w8(o, ma + 0x37, 0)
    for i in range(o.u32(WORLD + 0x10)):
        wf(o, ma + 0x124 + 12 * i, 20.0)
    print(f'comerciante {m} ([+8] {o.u16(ma + 8):#x}, natal {o.u8(ma + 0x19)}) alcalde de {town}')
    return town


def candidato(o):
    make_candidate(o, ai_with_office(o)[0])


def jugador(o):
    make_candidate(o, o.u32(to.PLAYER))


def dos_candidatos(o):
    ais = ai_with_office(o)
    first = make_candidate(o, ais[0])
    for m in ais[1:]:
        if first_rank_town(o, m) != first:
            make_candidate(o, m)
            return
    raise RuntimeError('no hay un segundo comerciante para otra ciudad')


def sin_carretera(o):
    candidato(o)
    for i in range(o.u32(WORLD + 0x10)):
        w32(o, o.town(i) + 0x76C, o.u32(o.town(i) + 0x76C) | 0x8000)


def encargos_viejos(o):
    candidato(o)
    tasks = [(i, a) for i, a in events(o) if o.u16(a + 6) == 0x32 and o.u8(a + 0xD) >= o.u16(WORLD + 0xA)]
    road = next((i, a) for i, a in tasks if o.u8(a + 0xC) == 1)
    town = o.u8(road[1] + 0x10)
    w32(o, o.town(town) + 0x76C, o.u32(o.town(town) + 0x76C) | 0x8000)
    grow = next((i, a) for i, a in tasks if o.u8(a + 0xC) == 4)
    w8(o, grow[1] + 0xD, ai_with_office(o)[1])
    print(f'carretera: evento {road[0]} (ciudad {town} con 0x8000); ciudad: evento {grow[0]} con comerciante')


SCENARIOS = {f.__name__: f for f in (candidato, jugador, dos_candidatos, sin_carretera, encargos_viejos)}


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('exe')
    p.add_argument('save')
    p.add_argument('out')
    p.add_argument('days', type=int)
    p.add_argument('functions')
    p.add_argument('scenario', choices=sorted(SCENARIOS))
    p.add_argument('--barcos', metavar='DATOS', required=True)
    p.add_argument('--cpr', required=True)
    p.add_argument('--desde', type=int, default=0)
    p.add_argument('--max', type=int, default=1 << 30)
    p.add_argument('--semilla', type=int, default=1)
    a = p.parse_args()
    o = to.TownOracle(a.exe, open(a.save, 'rb').read(), a.semilla, a.barcos, rest=list(to.REST), cpr=a.cpr)
    o.load()
    SCENARIOS[a.scenario](o)
    with open(a.out, 'wb') as f:
        f.write(b'P3PROBE1')
        rec = to.Recorder(o, [int(x, 16) for x in a.functions.split(',')], f, a.max)
        for day in range(a.days):
            rec.active = day >= a.desde
            for _ in range(256):
                o.advance_tick()
            if rec.count >= a.max:
                break
        f.write(b'E')
    print(f'{rec.count} muestras en {a.out}')
    for i, e in events(o):
        if o.u16(e + 6) == 0x32:
            raw = bytes(o.uc.mem_read(e, 0x18))
            print(f'encargo {i}: tipo {raw[0xC]} comerciante {raw[0xD]} plazo {o.u32(e + 8):#x} datos {raw[0x10:].hex()}')


if __name__ == '__main__':
    main()
