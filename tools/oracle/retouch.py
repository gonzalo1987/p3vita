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

Los encargos ya dados (el evento 0x32 con comerciante): estos escenarios
hacen candidato a un comerciante con el código del juego al cargar (la
candidatura y sus operaciones) y colocan cada encargo en una rama; graban
535760 y 4d85c0. Se quitan los de los tipos que p3vita aún no tiene.
  encargo_nuevo     los encargos 2 y 4 maduros: se dan y se empiezan
  encargo_cumplido  los encargos 2 y 4 dados, empezados y cumplidos
  encargo_vencido   los encargos 2 y 4 dados, empezados y con el plazo cerca
  barco_perdido     el encargo 2 con un barco que no es pirata: fallido
  jugador_coge      el jugador coge el encargo 4 con la operación 0x4B y no
                    puede coger el 2
  jugador_vencido   el jugador con el encargo 3 (o el 2) vencido
La carretera (tipo 1; la IA nunca aporta materiales: solo el jugador, con la
operación 0x3B desde la interfaz):
  carretera_ia               sin materiales y con el plazo cerca: vence
  carretera_tramos           gasta un tramo (con los bits altos) y alarga el plazo
  carretera_ultimo           gasta el último tramo y construye los edificios 0x27
  carretera_obra             los dos edificios en obras con un día: se terminan y se cumple
  carretera_hecha            los dos edificios 0x27 terminados: cumplida
  jugador_carretera_vencida  el jugador: el juego crea un encargo igual
  jugador_materiales         el jugador aporta materiales (operación 0x3B)
La guarida pirata (tipo 3; el ataque, un barco justo en la guarida, acaba en
una batalla que el emulador no ejecuta):
  guarida_lejos    un barco lejos: espera
  guarida_cerca    un barco cerca: fase 1 y los piratas se refuerzan
  guarida_rondando fase 1 con un barco navegando junto a ella: las esperas
  guarida_hecha    fase 2: cumplida
  guarida_fallida  fase 3: fallida
Fundar una ciudad (tipo 0):
  fundar           el encargo dado sin empezar: se crea la ciudad
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


# --- Encargos ya dados (el evento 0x32 con comerciante) ---

def run_candidacy(o, m):
    """Hace candidato al comerciante m con el código del juego: alcalde con
    120 (make_candidate), 0x4F75F0 sobre su ciudad y las operaciones que
    encola, con el ejecutor. Devuelve {tipo: (índice, dirección)} de los
    encargos sin dar que quedan."""
    town = make_candidate(o, m)
    o.tick_operations = []
    o.call(0x4F75F0, [town], ecx=o.merchant(m))
    ops, o.tick_operations = o.tick_operations, []
    for op in ops:
        buffer = o.alloc(to.OPERATION_SIZE)
        o.uc.mem_write(buffer, op)
        o.call(to.EXECUTOR, [], ecx=buffer)
    merchants = o.u16(WORLD + 0xA)
    return {o.u8(a + 0xC): (i, a) for i, a in events(o) if o.u16(a + 6) == 0x32 and o.u8(a + 0xD) >= merchants}


def give(o, task, m, deadline=None, **fields):
    """El encargo, dado al comerciante m y empezado ([+0xF] = 1), con el
    plazo `deadline` (ticks desde ahora) y bytes cambiados (f16=1 → [+0x16])."""
    i, a = task
    w8(o, a + 0xD, m)
    w8(o, a + 0xF, 1)
    if deadline is not None:
        w32(o, a + 8, o.u32(WORLD + 0x14) + deadline)
    for name, value in fields.items():
        offset = int(name[1:], 16)
        (w16 if name[0] == 'w' else w8)(o, a + offset, value)
    print(f'encargo {i}: tipo {o.u8(a + 0xC)} para {m}, plazo {o.u32(a + 8):#x}')


def drop(o, tasks, *kinds):
    """Los encargos de esos tipos pasan al tipo 5 (se quitan al atenderlos)."""
    for k in kinds:
        if k in tasks:
            w8(o, tasks[k][1] + 0xC, 5)


def encargo_nuevo(o):
    """Candidato de la IA con los encargos de cazar el barco (2) y de la
    ciudad (4) maduros ([+0xE] = 0x3F): se le dan, se empiezan (0x4EBF54) y
    el del barco mira cada 4 días. Graba 6 días."""
    m = ai_with_office(o)[0]
    tasks = run_candidacy(o, m)
    drop(o, tasks, 0, 1, 3)
    for k in (2, 4):
        if k in tasks:
            w8(o, tasks[k][1] + 0xE, 0x3F)


def encargo_cumplido(o):
    """Candidato de la IA con los encargos 2 y 4 dados, empezados y ya
    cumplidos (0x4EC354): [+0x16] = 1 en el del barco y 0 habitantes en el
    de la ciudad."""
    m = ai_with_office(o)[0]
    tasks = run_candidacy(o, m)
    drop(o, tasks, 0, 1, 3)
    if 2 in tasks:
        give(o, tasks[2], m, f16=1)
    if 4 in tasks:
        give(o, tasks[4], m, w10=0)


def encargo_vencido(o):
    """Candidato de la IA con los encargos 2 y 4 dados y empezados, con el
    plazo cerca: el barco se mira una vez y vence; la ciudad no llega y vence
    (0x4F9170, tipo 5). Graba 6 días."""
    m = ai_with_office(o)[0]
    tasks = run_candidacy(o, m)
    drop(o, tasks, 0, 1, 3)
    if 2 in tasks:
        give(o, tasks[2], m, deadline=0x500, f16=0)
    if 4 in tasks:
        give(o, tasks[4], m, deadline=0x300, w10=60000)


def barco_perdido(o):
    """Candidato de la IA con el encargo del barco dado y empezado, pero el
    barco 0 (que no es pirata): la primera mirada lo da por fallido ([+0x16]
    = 2) y a los 4 días se quita. Graba 6 días."""
    m = ai_with_office(o)[0]
    tasks = run_candidacy(o, m)
    drop(o, tasks, 0, 1, 3, 4)
    give(o, tasks[2], m, w10=0, f16=0)


def jugador_coge(o):
    """El jugador humano, candidato, coge el encargo de la ciudad (operación
    0x4B) y después intenta coger el del barco (no puede: ya tiene uno)."""
    m = o.u32(to.PLAYER)
    tasks = run_candidacy(o, m)
    drop(o, tasks, 0, 1, 3)
    for k in (4, 2):
        if k in tasks:
            o.tick_operations.append(struct.pack('<5I', 0x4B, m, tasks[k][0], 0, 0))
            print(f'operación 0x4B: el jugador {m} coge el encargo {tasks[k][0]} (tipo {k})')


def jugador_vencido(o):
    """El jugador humano con el encargo de la guarida (3) vencido: además del
    castigo, su [+0xE1] = 0xFF. Sin guarida en la partida, el del barco."""
    m = o.u32(to.PLAYER)
    tasks = run_candidacy(o, m)
    k = 3 if 3 in tasks else 2
    drop(o, tasks, *(x for x in (0, 1, 2, 3, 4) if x != k))
    give(o, tasks[k], m, deadline=0x80)


# --- La carretera (tipo 1) ---

def road_task(o, m):
    """Candidato m con solo el encargo de la carretera; devuelve (índice, dirección)."""
    tasks = run_candidacy(o, m)
    drop(o, tasks, 0, 2, 3, 4)
    return tasks[1]


def carretera_ia(o):
    """La IA con la carretera empezada, sin materiales y el plazo cerca:
    mira cada día y medio y vence. Graba 4 días."""
    m = ai_with_office(o)[0]
    give(o, road_task(o, m), m, deadline=0x400)


def carretera_tramos(o):
    """La carretera con 3 tramos por hacer y materiales para los 3 (el
    primero con 0x101, que usa los bits de [+0x13]) y el plazo cerca: gasta un
    tramo y alarga el plazo. Graba 2 días."""
    m = ai_with_office(o)[0]
    task = road_task(o, m)
    give(o, task, m, deadline=0x200, f12=3, f13=0x01, f14=0x01, f15=12, f16=3, f17=6)


def carretera_ultimo(o):
    """La carretera con un tramo por hacer y sus materiales: lo gasta y
    construye el edificio 0x27 en las dos ciudades (0x4F9C80, 0x521900)."""
    m = ai_with_office(o)[0]
    task = road_task(o, m)
    give(o, task, m, f12=1, f13=0, f14=3, f15=4, f16=1, f17=2)


def carretera_obra(o):
    """La carretera sin tramos por hacer y sus dos edificios 0x27 en obras,
    construidos al cargar con el código del juego (0x4F9C80 y 0x521900) y con
    un día de obra: el tick de la ciudad los termina y el encargo se cumple.
    Graba 51ba10 y 4d85c0 3 días."""
    m = ai_with_office(o)[0]
    task = road_task(o, m)
    give(o, task, m, f12=0)
    point = o.alloc(8)
    o.uc.mem_write(point, bytes([0xFF, 0, 0, 0, 0xFF, 0, 0, 0]))
    for k in (0x10, 0x11):
        town = o.u8(task[1] + k)
        t = o.town(town)
        o.call(0x4F9C80, [point, point + 4, 0x27, 0, 1], ecx=t + 0x7A4)
        x, y = o.u8(point), o.u8(point + 4)
        built = o.call(0x521900, [0xFF, x, y, 0x27, 0], ecx=t) & 0xFF
        r = o.u16(t + 0x762)
        while r < o.u16(t + 0x776):
            rec = o.u32(t + 0x75C) + 8 * r
            if o.u8(rec + 3) == 0x27:
                w8(o, rec + 6, 1)
            r = o.u16(rec + 4)
        print(f'ciudad {town}: edificio 0x27 en ({x}, {y}): {built}')


def jugador_materiales(o):
    """El jugador con la carretera (5 tramos) en la ciudad de su primera
    oficina, que tiene 7,25 cargas de madera; su primer barco, atracado allí
    con 5. Operaciones 0x3B: 10 cargas de madera (7 de la oficina y 3 del
    barco), 3 barriles de herramientas (no hay) y 2 de ladrillos pedidos por
    su mercancía (0x13; no hay). Graba 535760 y 4d85c0 2 días."""
    m = o.u32(to.PLAYER)
    task = road_task(o, m)
    give(o, task, m, f12=5)
    first = o.u16(o.merchant(m) + 0xC)
    town = next(i for i in range(o.u32(WORLD + 0x10)) if first in office_list(o, i))
    w8(o, task[1] + 0x10, town)
    timber = 0x0B
    w32(o, o.office(first) + 0x4 + 4 * timber, 7 * 2000 + 500)
    ship = o.u16(o.merchant(m) + 0xE)
    w8(o, o.ship(ship) + 0x39, town)
    w8(o, o.ship(ship) + 0x136, o.u8(o.ship(ship) + 0x136) & ~1)
    w32(o, o.ship(ship) + 0x54 + 4 * timber, 5 * 2000)
    for amount, material in ((10, 0), (3, 2), (2, 0x13)):
        o.tick_operations.append(struct.pack('<IHHBBBBQ', 0x3B, task[0], amount, town, m, material, 0, 0))
    print(f'ciudad {town}, oficina {first}, barco {ship}')


# --- La guarida pirata (tipo 3) ---

def docked_candidate(o):
    """(comerciante, barco): el primer comerciante de la IA con oficina que
    tiene un barco atracado (estados 0..5 de [+0x134]), que no se mueve."""
    ships = o.counts()[3]
    for m in ai_with_office(o):
        s = o.u16(o.merchant(m) + 0xE)
        while s < ships:
            if o.u16(o.ship(s) + 0x134) <= 5:
                return m, s
            s = o.u16(o.ship(s) + 0x4)
    raise RuntimeError('ningún comerciante de la IA tiene un barco atracado')


def nest_task(o, near=None):
    """Un candidato de la IA con un barco atracado y solo el encargo de la
    guarida, dado y empezado; con `near` = (dx, dy), la guarida (la posición
    de StadtDaten.ini del número [+0x11], en los globales) se pone a esa
    distancia del barco ([+0x1E], [+0x22])."""
    m, ship = docked_candidate(o)
    tasks = run_candidacy(o, m)
    drop(o, tasks, 0, 1, 2, 4)
    task = tasks[3]
    give(o, task, m, deadline=0x3000, f12=0)
    nest = o.u8(task[1] + 0x11)
    if near is not None:
        x = struct.unpack('<h', o.uc.mem_read(o.ship(ship) + 0x1E, 2))[0] - near[0]
        y = struct.unpack('<h', o.uc.mem_read(o.ship(ship) + 0x22, 2))[0] - near[1]
        w32(o, to.TOWN_TABLE + 0x34 * nest + 0x20, x)
        w32(o, to.TOWN_TABLE + 0x34 * nest + 0x24, y)
        print(f'guarida {nest} en ({x}, {y}), a ({near[0]}, {near[1]}) del barco {ship}')
    return task


def guarida_lejos(o):
    """La IA con la guarida y un barco a distancia² 0x300: no llega (espera
    12 ticks entre miradas). Graba 2 días."""
    nest_task(o, near=(16, 16))


def guarida_cerca(o):
    """La IA con la guarida y un barco a distancia² 25: pasa a la fase 1,
    apunta la guarida en su [+0xE1] y los piratas se refuerzan (0x5144A0,
    barcos nuevos con 0x516170). Graba 2 días."""
    nest_task(o, near=(3, 4))


def guarida_rondando(o):
    """La guarida en la fase 1 y un barco del comerciante navegando junto a
    ella: el mundo pasa sin grabar hasta el tick antes de mirar el encargo y
    entonces la guarida se pone a (3, 4) del barco; al alejarse, las esperas
    pasan por 1, & 3 y & 7. Graba 2 días."""
    ships = o.counts()[3]
    for m in ai_with_office(o):
        s = o.u16(o.merchant(m) + 0xE)
        while s < ships and o.u16(o.ship(s) + 0x134) != 0xF:
            s = o.u16(o.ship(s) + 0x4)
        if s < ships:
            break
    tasks = run_candidacy(o, m)
    drop(o, tasks, 0, 1, 2, 4)
    task = tasks[3]
    give(o, task, m, deadline=0x3000, f12=1, w14=0xFFFF)
    while o.u32(WORLD + 0x14) + 1 < o.u32(task[1]):
        o.advance_tick()
    nest = o.u8(task[1] + 0x11)
    x = struct.unpack('<h', o.uc.mem_read(o.ship(s) + 0x1E, 2))[0] - 3
    y = struct.unpack('<h', o.uc.mem_read(o.ship(s) + 0x22, 2))[0] - 4
    w32(o, to.TOWN_TABLE + 0x34 * nest + 0x20, x)
    w32(o, to.TOWN_TABLE + 0x34 * nest + 0x24, y)
    print(f'comerciante {m}: guarida {nest} en ({x}, {y}), junto al barco {s} (en el mar), tick {o.u32(WORLD + 0x14):#x}')


def guarida_hecha(o):
    """La guarida en la fase 2 (destruida): cumplida. Y en otra partida la
    fase 3 (fallida). Graba 2 días."""
    task = nest_task(o)
    w8(o, task[1] + 0x12, 2)


def guarida_fallida(o):
    task = nest_task(o)
    w8(o, task[1] + 0x12, 3)


# --- Fundar una ciudad (tipo 0) ---

def fundar(o):
    """La IA con el encargo de fundar una ciudad dado y sin empezar: al
    mirarlo, 0x4EBF54 crea la ciudad (0x531D50, con la plantilla
    iso/towns/stadtplatz%.2u.tow del archivo). Graba 2 días."""
    m = ai_with_office(o)[0]
    tasks = run_candidacy(o, m)
    drop(o, tasks, 1, 2, 3, 4)
    i, a = tasks[0]
    w8(o, a + 0xD, m)
    print(f'encargo {i}: fundar la ciudad {o.u8(a + 0x14)} para {m}; {o.u32(WORLD + 0x10)} ciudades')


def office_list(o, town):
    """Las oficinas de la ciudad (lista [0x784], siguiente en [+0x2CA])."""
    out, i = [], o.u16(o.town(town) + 0x784)
    while i < o.u16(WORLD + 8):
        out.append(i)
        i = o.u16(o.office(i) + 0x2CA)
    return out


def carretera_hecha(o):
    """La carretera sin tramos por hacer y un edificio 0x27 terminado en las
    dos ciudades (el primero de su lista [0x764] cambiado de tipo): cumplida."""
    m = ai_with_office(o)[0]
    task = road_task(o, m)
    give(o, task, m, f12=0)
    for k in (0x10, 0x11):
        t = o.town(o.u8(task[1] + k))
        head = o.u16(t + 0x764)
        if head < o.u16(t + 0x776):
            w8(o, o.u32(t + 0x75C) + 8 * head + 3, 0x27)
            print(f'ciudad {o.u8(task[1] + k)}: registro {head} pasa a ser del tipo 0x27')


def jugador_carretera_vencida(o):
    """El jugador con la carretera vencida: el juego crea un encargo igual con
    la ciudad [+0x10] como comerciante, que sigue avanzando. Graba 2 días."""
    m = o.u32(to.PLAYER)
    give(o, road_task(o, m), m, deadline=0x80)


SCENARIOS = {f.__name__: f for f in (candidato, jugador, dos_candidatos, sin_carretera, encargos_viejos,
                                     carretera_ia, carretera_tramos, carretera_ultimo, carretera_obra, carretera_hecha,
                                     jugador_materiales, guarida_lejos, guarida_cerca, guarida_rondando, guarida_hecha, fundar,
                                     guarida_fallida,
                                     jugador_carretera_vencida,
                                     encargo_nuevo, encargo_cumplido, encargo_vencido, barco_perdido,
                                     jugador_coge, jugador_vencido)}


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
