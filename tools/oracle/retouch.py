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
  fundar_aceite    lo mismo con la máscara de cuando falta aceite: el nodo de 0x5462F0
El desarrollo de la ciudad nueva (evento 0x2F, 0x4EA0D4), adelantado al tick
siguiente; graban 535760 y 4d85c0. Los cuatro primeros fundan la ciudad sin
grabar; los otros crean el evento en ciudades que ya existen:
  nueva_sin_marca       sin el bit 1 de [0x76C]: espera
  nueva_alcalde_ia      alcalde de la IA: su oficina; con la bandera 0x80 y menos de 500
  nueva_alcalde_humano  el jugador de alcalde: sin oficina; 700 habitantes
  nueva_sin_alcalde     la operación 0x46 y, a los 14 días, la oficina (16 días)
  poblada               más de 1000 habitantes con cada muralla (0x5252F0): 1 o 2
                        (el mercado y después el prestamista), 0xFF, 0 con tramo y 0 sin él
  rica                  la fase 2: con el prestamista (0x800) y con 1500 sin pasar
Los robos en las oficinas (operación 0x64, evento 0x33); graban 535760 y 4d85c0:
  robo                  dos robos al jugador: en su oficina con más mercancía
                        (lo que hay, lo que no, 0xFF y el tope de tres) y en
                        una ciudad sin oficina suya
Los eventos que p3vita creaba y no atendía; graban 4d85c0:
  capitanes             0x27: capitanes de la IA que se retiran, uno por estado de barco
  impuesto              0x31: el impuesto extraordinario (deudas y queja a la Hansa)
  monumento             0x38: el monumento al alcalde reelegido (alcaldes de la IA a mano)
  incendio              0x25 y 0x26: arden una torre, una pieza de muralla, una empresa, una
                        casa y un edificio de la ciudad (graba 2 días)
  dieta                 0x34, 0x24 y 0x23: las misiones de la dieta de la Hansa (0x4E09D4)
Las operaciones de la simulación que p3vita descartaba (inventario, parte B); graban 535760 y 4d85c0:
  fiesta                0x61 y el evento 0x07: fiestas de la IA (con precio, sin dinero, con
                        deudas, sin oficina) y del jugador con la oficina llena
  fiesta_vacia          la del jugador con una sola mercancía de la fiesta
  fiesta_ciudad         la fiesta de la ciudad (0x5316E0 en el evento 0x03, adelantado)
  astillero             0x2C: barcos encargados al astillero (materiales de la oficina, de los
                        barcos y de la ciudad; sin oficina, sin astillero, sin dinero; el jugador)
  astillero_ciudad      el evento 0x2B adelantado con la ciudad 0 muy poblada: encola la 0x2C
  eventos_op            0x38, 0x7C, 0x7F, 0x80 y 0x83: las que solo crean o tocan eventos
  propuestas            0x84 (el encargo del jefe de la Hansa), 0x67 (un administrador) y 0x48
                        (la votación del consejo o, sin consejo, los impuestos)
  subasta               0x28: edificios en venta (una subasta en fase 2, una nueva, sin el bit de
                        la ciudad, una celda vacía)
  subasta_ajuste        lo mismo con el byte [+0xD] de los ajustes a 0: la subasta en fase 1
  temporada             0x7E: la temporada de piratas que viene (evento 0x1F)
  incendio_ciudad       0x7D y el evento 0x1D: incendios en un almacén, una casa y un edificio de la ciudad
  capitan_jugador       0x13: el capitán de un barco del jugador se retira (evento 0x27)
  boda                  0x60 and event 0x15: weddings with their dowry ships and feast
  quiebra               0x65: an AI merchant goes bankrupt (debts, mayor, new name)
  hijo                  0x87: a child is born to a married merchant
Los asedios (operación 0x8E); graban 535760 y 633a90:
  asedio                uno a una ciudad sin muralla (rescate sin atacar) y
                        otro en la región de la ciudad natal del jugador, con
                        rango 5 en ella: le piden ayuda (operación 0x8B)
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
    return i, a


def fundar_aceite(o):
    """fundar con la máscara de mercancías que deja 0x532E30 cuando falta
    aceite (| 0x40020): la ciudad nueva tiene el nodo de 0x5462F0 (con 4) en
    su lista [0x9B4]. Graba 4ebf54 2 días y, aparte, el mundo 4 días."""
    i, a = fundar(o)
    w32(o, a + 0x10, o.u32(a + 0x10) | 0x40020)
    print(f'máscara {o.u32(a + 0x10):#x}')


# --- El desarrollo de la ciudad nueva (evento 0x2F, 0x4EA0D4) ---

def found_town(o):
    """fundar y el mundo sin grabar hasta que 0x4EBF54 crea la ciudad; su índice."""
    fundar(o)
    towns = o.u32(WORLD + 0x10)
    for _ in range(256 * 8):
        o.advance_tick()
        if o.u32(WORLD + 0x10) > towns:
            print(f'ciudad {towns} fundada en el tick {o.u32(WORLD + 0x14):#x}')
            return towns
    raise RuntimeError('no se ha fundado la ciudad en 8 días')


def development(o, town, phases, set_bits=0, clear_bits=0):
    """El evento 0x2F de la ciudad (el de la fundación o uno nuevo, 0x4D8CF0)
    con las fases `phases` en [+0x10], para el tick siguiente: se desengancha
    de la cola, se pone en cabeza y 0x4D8DD0 lo coloca. Cambia los bits de
    [0x76C] de la ciudad."""
    now = o.u32(WORLD + 0x14)
    found = [(i, a) for i, a in events(o) if o.u16(a + 6) == 0x2F and o.u32(a + 0xC) == town]
    if found:
        i, a = found[0]
        order = [j for j, _ in events(o)]
        k = order.index(i)
        link = to.EVENTS + 8 if k == 0 else o.u32(to.EVENTS) + 0x18 * order[k - 1] + 4
        w16(o, link, o.u16(a + 4))
        w16(o, a + 4, o.u16(to.EVENTS + 8))
        w16(o, to.EVENTS + 8, i)
        w32(o, a, now + 1)
        o.call(0x4D8DD0, [], ecx=to.EVENTS)
    else:
        a = o.call(0x4D8CF0, [now + 1], ecx=to.EVENTS)  # devuelve el evento
        i = (a - o.u32(to.EVENTS)) // 0x18
        w16(o, a + 6, 0x2F)
        w32(o, a + 8, 0)
        w32(o, a + 0xC, town)
        w32(o, a + 0x14, now)
    w32(o, a + 0x10, phases)
    t = o.town(town)
    w32(o, t + 0x76C, (o.u32(t + 0x76C) | set_bits) & ~clear_bits)
    print(f'evento {i}: 0x2F de la ciudad {town} en el tick {o.u32(a):#x}, fases {phases:#x}, [0x76C] {o.u32(t + 0x76C):#x},'
          f' {o.u32(t + 0x2D4)} habitantes, alcalde {o.u8(t + 0x6F1)}')


def set_population(o, town, n):
    """n habitantes: la diferencia, en los pobres ([0x2E0])."""
    t = o.town(town)
    w32(o, t + 0x2E0, o.u32(t + 0x2E0) + n - o.u32(t + 0x2D4))
    w32(o, t + 0x2D4, n)


def nueva_sin_marca(o):
    """La ciudad nueva sin el bit 1 de [0x76C]: vuelve a los 21 días. Graba 2 días."""
    development(o, found_town(o), 0, clear_bits=1)


def nueva_alcalde_ia(o):
    """La ciudad nueva con el bit 1, con el fundador (de la IA) de alcalde,
    como la deja la fundación, y la bandera 0x80: su oficina (0x5308A0), fase
    1 y, al dieciseisavo de día, 4 pobres más y menos de 500 habitantes.
    Graba 2 días."""
    development(o, found_town(o), 0x80, set_bits=1)


def nueva_alcalde_humano(o):
    """La ciudad nueva con el bit 1, el jugador de alcalde y 700 habitantes:
    sin oficina, fase 1 y después 0x88DB − 35 por habitante. Graba 2 días."""
    town = found_town(o)
    w8(o, o.town(town) + 0x6F1, o.u32(to.PLAYER))
    set_population(o, town, 700)
    development(o, town, 0, set_bits=1)


def nueva_sin_alcalde(o):
    """La ciudad nueva con el bit 1 y sin alcalde: la operación 0x46 hace
    alcalde al comerciante de la IA con más rango (0x535FD6) y, a los 14
    días, su oficina. Graba 16 días."""
    town = found_town(o)
    w8(o, o.town(town) + 0x6F1, 0xFF)
    development(o, town, 0, set_bits=1)


def wall(o, town):
    return o.call(0x5252F0, [], ecx=o.town(town)) & 0xFF


def poblada(o):
    """Cuatro ciudades de más de 1000 habitantes en la fase 1 de 0x2F, sin
    mercado (bit 0x100) ni prestamista (0x800), según su muralla (0x5252F0,
    dirigida con los puntos de muralla [0x7B0 + 2·i]): 1 o 2, el mercado y,
    a los 5 ticks, la fase 2 y, al día, el prestamista (0x51FAB0, 0x25); 0xFF,
    28 días; 0 con un tramo de la ciudad en obras (el último de la lista
    [0x762] pasa a 0x36 con dueño 0xFF), 14 días; 0 sin él, el mercado y 28
    días. Graba 3 días. Los puntos solo cuentan sin piezas de muralla
    pendientes ([0x766]): en las partidas que tienen piezas en todas las
    ciudades (la 3) no hay muralla 0xFF."""
    def works(i):
        t, out = o.town(i), []
        r = o.u16(t + 0x762)
        while r < o.u16(t + 0x776):
            out.append(r)
            r = o.u16(o.u32(t + 0x75C) + 8 * r + 4)
        return out

    def by_points(i):
        return o.u16(o.town(i) + 0x766) >= o.u16(o.town(i) + 0x776)

    towns = [i for i in range(o.u32(WORLD + 0x10)) if o.u32(o.town(i) + 0x2D4) >= 1000]
    natural = [i for i in towns if wall(o, i) in (1, 2)]
    a = natural[0] if natural else next(i for i in towns if wall(o, i) == 0 and by_points(i))
    if not natural:
        w16(o, o.town(a) + 0x7B4, 0xFFFF)  # el punto 2 fuera: 2
    zero = [i for i in towns if i != a and wall(o, i) == 0]
    chosen = [a]
    c = max((i for i in zero if works(i)), key=lambda i: len(works(i)), default=None)
    if c is not None:
        t = o.town(c)
        rec = o.u32(t + 0x75C) + 8 * works(c)[-1]
        w8(o, rec + 2, 0xFF)
        w8(o, rec + 3, 0x36)
        print(f'ciudad {c}: la última de sus {len(works(c))} obras pasa a ser un tramo de muralla de la ciudad')
        chosen.append(c)
    b = next((i for i in zero if i not in chosen and by_points(i)), None)
    if b is not None:
        t = o.town(b)
        o.uc.mem_write(t + 0x7B0, bytes(o.uc.mem_read(t + 0x7B2, 2)))  # el punto 0 dentro: 0xFF
        chosen.append(b)
    chosen.append(next(i for i in zero if i not in chosen))
    for town in chosen:
        print(f'ciudad {town}: muralla {wall(o, town):#x}')
        development(o, town, 1, clear_bits=0x900)


def rica(o):
    """Dos ciudades en la fase 2 de 0x2F: una con el prestamista (bit 0x800):
    fase 3 y a los 21 días; otra sin él y con 3 · 400 pobres + 300
    acomodados = 1500 (no pasa de 1500): 28 días. Graba 2 días."""
    development(o, 0, 0x101)
    t = o.town(1)
    w32(o, t + 0x2E0, 400)
    w32(o, t + 0x2DC, 300)
    development(o, 1, 0x101, clear_bits=0x800)


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


def siege_ready(o, i):
    """¿Puede la ciudad i recibir un ejército (0x633AF0)? Sin ejército, con el
    bit 0x100 de [0x76C] y tres puertas o torres de muralla."""
    t = o.town(i)
    if not o.u32(t + 0x76C) & 0x100 or o.u32(o.u32(to.SIEGES) + 0x1024 * i + 8):
        return False
    count, records = o.u16(t + 0x776), o.u32(t + 0x75C)

    def types(head):
        out, r = [], head
        while r < count:
            out.append(o.u8(records + 8 * r + 3))
            r = o.u16(records + 8 * r + 4)
        return out

    gates = sum(1 for k in types(o.u16(t + 0x768)) if k >= 0x34 and k != 0x36)
    if gates < 3:
        gates += sum(1 for k in types(o.u16(t + 0x762)) if k == 0x34)
    return gates >= 3


def region(o, i):
    return o.u8(0x6DDB90 + 0x34 * o.u8(WORLD + 0x18 + i) + 0x30)


def asedio(o):
    """Dos asedios (operaciones 0x8E al final del primer tick, 0x633AF0 y
    0x629C60): uno en una ciudad sin muralla (0x5252F0 da 0xFF con el punto 0
    de la muralla dentro: no ataca y cobra el rescate) y otro en una ciudad de
    la región de la ciudad natal del jugador, con rango 5 en ella ([+0x39C]):
    le pide ayuda (operación 0x8B). Graba 1 día."""
    m = o.u32(to.PLAYER)
    home = o.u8(o.merchant(m) + 0x19)
    towns = [i for i in range(o.u32(WORLD + 0x10)) if siege_ready(o, i)]
    a = next(i for i in towns if wall(o, i) == 0 and o.u16(o.town(i) + 0x766) >= o.u16(o.town(i) + 0x776))
    t = o.town(a)
    o.uc.mem_write(t + 0x7B0, bytes(o.uc.mem_read(t + 0x7B2, 2)))
    print(f'ciudad {a}: muralla {wall(o, a):#x}')
    b = next(i for i in towns if i not in (a, home) and region(o, i) == region(o, home))
    w8(o, o.merchant(m) + 0x39C + home, 5)
    print(f'ciudad {b}: muralla {wall(o, b):#x}, de la región de {home}, la ciudad natal del jugador {m}')
    for town, kind, r in ((a, 1, 0x80), (b, 2, 0x31)):
        o.tick_operations.append(struct.pack('<5I', 0x8E, town, kind, r, 1))


BARREL = (0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1)  # 0x672C14


def robo(o):
    """Dos robos en oficinas del jugador: operaciones 0x64 al final del primer
    tick, con sus eventos 0x33 (0x4EC934) a los 16 y 32 ticks. En la oficina
    con más mercancía: más de lo que hay de una (se lleva lo que hay), una que
    no tiene, una 0xFF y tres que sí (de la última ya no se lleva nada: van
    tres). En una ciudad sin oficina suya, nada. Graba 1 día. Devuelve lo que
    escribe al final: lo que queda en la oficina."""
    m = o.u32(to.PLAYER)
    offices, i = [], o.u16(o.merchant(m) + 0xC)
    while i < o.u16(WORLD + 8):
        offices.append(i)
        i = o.u16(o.office(i) + 0x2C8)

    def stock(office, w):
        return struct.unpack('<i', bytes(o.uc.mem_read(o.office(office) + 4 + 4 * w, 4)))[0]

    office = max(offices, key=lambda f: sum(max(stock(f, w), 0) for w in range(20)))
    unit = [200 if BARREL[w] else 2000 for w in range(24)]
    have = [w for w in range(20) if stock(office, w) >= unit[w]]
    short = next((w for w in have if stock(office, w) // unit[w] < 255), None)
    empty = next(w for w in range(20) if stock(office, w) <= 0)
    pairs = [(short, stock(office, short) // unit[short] + 1)] if short is not None else []
    pairs += [(empty, 5), (0xFF, 7)] + [(w, 1) for w in have if w != short][:3]
    pairs += [(0xFF, 0)] * (6 - len(pairs))
    town = o.u8(o.office(office) + 0x2C6)
    op = struct.pack('<I6B2B6BH', 0x64, *[w for w, _ in pairs], m, town, *[n for _, n in pairs], 0x10)
    o.tick_operations.append(op)
    print(f'oficina {office} del jugador {m} en la ciudad {town}: (mercancía, pedido, hay)'
          f' {[(w, n, stock(office, w) if w < 20 else 0) for w, n in pairs]}')
    towns = {o.u8(o.office(f) + 0x2C6) for f in offices}
    other = next(t for t in range(o.u32(WORLD + 0x10)) if t not in towns)
    o.tick_operations.append(struct.pack('<I6B2B6BH', 0x64, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, m, other,
                                         1, 0, 0, 0, 0, 0, 0x20))
    print(f'ciudad {other} sin oficina del jugador')
    return lambda: print(f'después: {[(w, stock(office, w) if w < 20 else 0) for w, _ in pairs]}')


def capitanes(o):
    """Eventos 0x27 (0x4DDC00: un capitán de la IA se retira) a los ticks
    siguientes, uno por cada estado de barco ([+0x134]) distinto entre los
    barcos de la IA con capitán, y uno más con un guía de convoy. Graba 1 día.
    Devuelve lo que escribe al final: el capitán y el estado de esos barcos."""
    ships, captains = o.u16(to.SHIPS + 0xF4), o.u16(to.SHIPS + 0xF2)
    merchants = o.u16(WORLD + 0xA)
    chosen, statuses = [], set()
    for s in range(ships):
        p = o.ship(s)
        owner, captain, status = o.u8(p), o.u16(p + 0x42), o.u16(p + 0x134)
        if owner >= merchants or not o.u16(o.merchant(owner) + 8) or captain >= captains:
            continue
        convoy = o.u16(p + 8)
        lead = convoy < o.u16(to.SHIPS + 0xF6) and o.u16(o.u32(to.SHIPS + 8) + 0x3C * convoy + 0x10) == s
        if status not in statuses or (lead and not any(c[3] for c in chosen)):
            statuses.add(status)
            chosen.append((s, captain, status, lead))
    now = o.u32(WORLD + 0x14)
    for k, (s, captain, status, lead) in enumerate(chosen):
        a = o.call(0x4D8CF0, [now + 1 + k], ecx=to.EVENTS)
        w16(o, a + 6, 0x27)
        w32(o, a + 8, s)
        w32(o, a + 0xC, captain)
    print(f'eventos 0x27 (barco, capitán, estado, guía): {chosen}')
    return lambda: print(f'después (capitán, estado): {[(o.u16(o.ship(s) + 0x42), o.u16(o.ship(s) + 0x134)) for s, *_ in chosen]}')


def impuesto(o):
    """Eventos 0x31 (0x4EAB94: el impuesto extraordinario de una ciudad) a los
    ticks siguientes: en una ciudad con oficinas, 20000 (pagan todos); en
    otra, con el bit 0x100 de [0x76C], 3000000 (deudas en trozos de 250000 y
    la queja a la Hansa, un evento 0x30); en una sin oficinas, nada. Para la
    queja, la primera ciudad con alcalde de la IA o sin alcalde comerciante
    (de las demás) lleva el bit 0x40 de [0x2C8]. Graba 2 días."""
    towns, merchants = o.u32(WORLD + 0x10), o.u16(WORLD + 0xA)
    offices = [i for i in range(towns) if office_list(o, i)]
    a, b = offices[0], offices[1]
    w32(o, o.town(b) + 0x76C, o.u32(o.town(b) + 0x76C) | 0x100)

    def leader(i):
        mayor = o.u8(o.town(i) + 0x6F1)
        return mayor >= merchants or o.u16(o.merchant(mayor) + 8) != 0

    c = next(i for i in range(towns) if i not in (a, b) and leader(i))
    w32(o, o.town(c) + 0x2C8, o.u32(o.town(c) + 0x2C8) | 0x40)
    empty = [i for i in range(towns) if i not in offices]
    now = o.u32(WORLD + 0x14)
    for k, (town, amount) in enumerate([(a, 20000), (b, 3000000)] + [(t, 5000) for t in empty[:1]]):
        e = o.call(0x4D8CF0, [now + 1 + k], ecx=to.EVENTS)
        w16(o, e + 6, 0x31)
        w32(o, e + 8, amount)
        w8(o, e + 0xC, town)
    print(f'impuesto en {a} (20000), {b} (3000000, bit 0x100), {empty[:1]} sin oficinas;'
          f' preside {c} (alcalde {o.u8(o.town(c) + 0x6F1)})')


def monumento(o):
    """Eventos 0x38 (0x4ECB14: el monumento al alcalde reelegido) a los ticks
    siguientes: en cada ciudad con alcalde comerciante (las cuatro primeras
    lo pasan a tener, de la IA), a su nombre, y uno a nombre de otro (no es
    su alcalde: nada). Graba 1 día. Devuelve lo que
    escribe al final: el dueño y la variante del primer edificio 0x2D de cada
    una."""
    towns, merchants = o.u32(WORLD + 0x10), o.u16(WORLD + 0xA)

    def monument(i):
        t = o.town(i)
        r = o.u16(t + 0x764)
        while r < o.u16(t + 0x776):
            rec = o.u32(t + 0x75C) + 8 * r
            if o.u8(rec + 3) == 0x2D:
                return rec
            r = o.u16(rec + 4)
        return None

    ai = ai_with_office(o)
    for k, i in enumerate(range(min(4, towns))):
        w8(o, o.town(i) + 0x6F1, ai[k % len(ai)])  # alcalde de la IA
    cases = [(i, o.u8(o.town(i) + 0x6F1)) for i in range(towns) if o.u8(o.town(i) + 0x6F1) < merchants]
    if cases:
        cases.append((cases[0][0], (cases[0][1] + 1) % merchants))
    now = o.u32(WORLD + 0x14)
    for k, (town, m) in enumerate(cases):
        e = o.call(0x4D8CF0, [now + 1 + k], ecx=to.EVENTS)
        w16(o, e + 6, 0x38)
        w8(o, e + 8, town)
        w8(o, e + 9, m)
    info = [(i, m, (o.u8(monument(i) + 2), o.u8(monument(i) + 7)) if monument(i) else None) for i, m in cases]
    print(f'eventos 0x38 (ciudad, comerciante, monumento (dueño, variante)): {info}')
    return lambda: print(f'después: {[(i, (o.u8(monument(i) + 2), o.u8(monument(i) + 7)) if monument(i) else None) for i, _ in cases]}')


def incendio(o):
    """Los incendios del asedio en una ciudad: eventos 0x25 (0x4E1B84) sobre
    una torre (0x32, 0x33: queda en ruinas, 0x36) y sobre otra pieza de la
    lista de murallas [0x768] (se borra), y 0x26 (0x4E1E54) sobre una empresa
    ([+0x2CE] de una oficina) y una casa ([+0x2D0]) de comerciantes y un
    edificio de la ciudad ([0x764]): arden y se borran (0x521240, 0x4FACD0).
    Graba 2 días. Devuelve lo que escribe al final: tipo, obras y dueño de
    cada uno."""
    towns, merchants = o.u32(WORLD + 0x10), o.u16(WORLD + 0xA)

    def walk(t, head):
        out, r = [], o.u16(head)
        while r < o.u16(t + 0x776):
            out.append(r)
            r = o.u16(o.u32(t + 0x75C) + 8 * r + 4)
        return out

    def rec(t, r):
        return o.u32(t + 0x75C) + 8 * r

    for town in range(towns):
        t = o.town(town)
        walls = walk(t, t + 0x768)
        towers = [r for r in walls if o.u8(rec(t, r) + 3) in (0x32, 0x33)]
        others = [r for r in walls if o.u8(rec(t, r) + 3) not in (0x32, 0x33)]
        offices = office_list(o, town)
        firms = [r for f in offices for r in walk(t, o.office(f) + 0x2CE) if o.u8(rec(t, r) + 3) not in (0x28, 0x29)]
        houses = [r for f in offices for r in walk(t, o.office(f) + 0x2D0)]
        city = [r for r in walk(t, t + 0x764) if o.u8(rec(t, r) + 2) >= merchants and o.u8(rec(t, r) + 3) not in (0x28, 0x32, 0x33)]
        if towers and others and firms and houses and city:
            break
    else:
        raise RuntimeError('ninguna ciudad con todo')
    cases = [(0x25, towers[0]), (0x25, others[0]), (0x26, firms[0]), (0x26, houses[0]), (0x26, city[0])]
    now = o.u32(WORLD + 0x14)
    for k, (kind, r) in enumerate(cases):
        e = o.call(0x4D8CF0, [now + 1 + k], ecx=to.EVENTS)
        w16(o, e + 6, kind)
        if kind == 0x25:
            w16(o, e + 8, 0xFFFF)
            w16(o, e + 0xA, 0)
            w16(o, e + 0xC, o.u8(rec(t, r)))
            w16(o, e + 0xE, o.u8(rec(t, r) + 1))
        else:
            w16(o, e + 8, r)
        w8(o, e + 0x10, town)
        w8(o, e + 0x11, 1)

    def show():
        return [(kind, r, o.u8(rec(t, r) + 3), o.u8(rec(t, r) + 6), o.u8(rec(t, r) + 2)) for kind, r in cases]

    print(f'ciudad {town}: (evento, registro, tipo, obras, dueño) {show()}')
    return lambda: print(f'después: {show()}')


def dieta(o):
    """Las misiones que aprueba la dieta de la Hansa (0x4E09D4), como las deja
    el evento 0x21 al aprobar las propuestas 0, 1 y 2: eventos 0x34 (la caza
    de piratas, en la ciudad del alderman), 0x24 y 0x23 (en otras dos
    ciudades), a los ticks siguientes, en la fase 0 sin participantes ni
    flota. Graba, por ejemplo, 12 días de 4d85c0 y 535760. Devuelve lo que
    escribe al final: la fase, el convoy y su estado."""
    towns = o.u32(WORLD + 0x10)
    hanse = o.u8(0x6DE52E)
    hanse = hanse if hanse < towns else 0
    others = [i for i in range(towns) if i != hanse][:2]
    now = o.u32(WORLD + 0x14)
    created = []
    for k, (kind, town) in enumerate([(0x34, hanse), (0x24, others[0]), (0x23, others[1])]):
        e = o.call(0x4D8CF0, [now + 1 + k], ecx=to.EVENTS)
        w16(o, e + 6, kind)
        o.uc.mem_write(e + 8, bytes([0xFF] * 8))
        w32(o, e + 0x10, 0)
        w8(o, e + 0x14, town)
        w8(o, e + 0x15, 0)
        w16(o, e + 0x16, 0xFFFF)
        created.append((kind, town))
    print(f'misiones (tipo, ciudad): {created}')

    def show():
        out = []
        for i, a in events(o):
            kind = o.u16(a + 6)
            if kind in (0x23, 0x24, 0x34):
                convoy = o.u16(a + 0x16)
                state = o.u16(o.u32(to.SHIPS + 8) + 0x3C * convoy + 0x12) if convoy < o.u16(to.SHIPS + 0xF6) else None
                out.append((hex(kind), o.u8(a + 0x15), convoy, state, bytes(o.uc.mem_read(a + 8, 8)).hex()))
        return out
    return lambda: print(f'después (tipo, fase, convoy, estado, [+0x8]): {show()}')


FEAST = (3, 2, 2, 2, 0, 1, 0, 2)  # 0x6734E8: lo de cada mercancía por invitado


def merchant_offices(o, m):
    """Las oficinas del comerciante (lista [+0xC], siguiente en [+0x2C8])."""
    out, i = [], o.u16(o.merchant(m) + 0xC)
    while i < o.u16(WORLD + 8):
        out.append(i)
        i = o.u16(o.office(i) + 0x2C8)
    return out


def move_event(o, i, a, tick):
    """El evento i (en a) pasa al tick: se desengancha de la cola, se pone en
    cabeza y 0x4D8DD0 lo coloca."""
    order = [j for j, _ in events(o)]
    k = order.index(i)
    link = to.EVENTS + 8 if k == 0 else o.u32(to.EVENTS) + 0x18 * order[k - 1] + 4
    w16(o, link, o.u16(a + 4))
    w16(o, a + 4, o.u16(to.EVENTS + 8))
    w16(o, to.EVENTS + 8, i)
    w32(o, a, tick)
    o.call(0x4D8DD0, [], ecx=to.EVENTS)


def feast_op(o, m, tick, town, price):
    o.tick_operations.append(struct.pack('<5I', 0x61, m, tick, town, price))


def feast_guests(o, m, town):
    """Los invitados de la fiesta de un humano (0x4E23A4): habitantes · k / 100."""
    rep = struct.unpack('<f', bytes(o.uc.mem_read(o.merchant(m) + 0x2FC + 4 * town, 4)))[0]
    k = min(max(int(rep) + 0x27, 0), 0x63)
    return o.u32(o.town(town) + 0x2D4) * k // 100


def show_feast(o, towns, merchants, office):
    """Lo que cambia una fiesta: [0x2D0] de las ciudades, el dinero y [+0x4BC]
    de los comerciantes y lo de la fiesta en la oficina."""
    def s32(a):
        return struct.unpack('<i', bytes(o.uc.mem_read(a, 4)))[0]
    return lambda: print(f'[0x2D0] {[(t, hex(o.u32(o.town(t) + 0x2D0))) for t in towns]};'
                         f' dinero {[(m, s32(o.merchant(m)), s32(o.merchant(m) + 0x4BC)) for m in merchants]};'
                         f' oficina {office}: {[s32(o.office(office) + 4 + 4 * w) for w in range(8)]}')


def fiesta(o):
    """Fiestas (operación 0x61, 0x53DBF0, y el evento 0x07, 0x4E23A4): al final
    del primer tick, operaciones 0x61 con sus eventos a los 16, 32... ticks:
    un comerciante de la IA en la ciudad de su oficina, con precio; otro sin
    dinero para el precio (no hay fiesta) y otro con deudas (el juego compara
    sin signo: la hay); uno sin oficina en la ciudad (nada); un comerciante
    que no existe; y el jugador, con reputación 100 y la oficina llena (salvo
    la mercancía 7: se cuenta pero no llega) en una ciudad con el bit 0x1000
    de [0x2C8]. Graba 1 día. Devuelve lo que escribe al final."""
    now, towns = o.u32(WORLD + 0x14), o.u32(WORLD + 0x10)
    ai = ai_with_office(o)
    a, b, c = ai[0], ai[1], ai[2]
    ta, tb, tc = (o.u8(o.office(merchant_offices(o, m)[0]) + 0x2C6) for m in (a, b, c))
    w32(o, o.merchant(c), -5000)
    mine = {o.u8(o.office(f) + 0x2C6) for f in merchant_offices(o, a)}
    none = next(t for t in range(towns) if t not in mine)
    money_b = o.u32(o.merchant(b))
    feast_op(o, a, now + 0x10, ta, 1000)
    feast_op(o, b, now + 0x20, tb, money_b + 1)
    feast_op(o, c, now + 0x30, tc, 1000)
    feast_op(o, a, now + 0x40, none, 0)
    feast_op(o, 0xFF, now + 0x50, ta, 0)
    p = o.u32(to.PLAYER)
    office = merchant_offices(o, p)[0]
    tp = o.u8(o.office(office) + 0x2C6)
    wf(o, o.merchant(p) + 0x2FC + 4 * tp, 100.0)
    w32(o, o.town(tp) + 0x2C8, o.u32(o.town(tp) + 0x2C8) | 0x1000)
    guests = feast_guests(o, p, tp)
    for w in range(8):
        if FEAST[w]:
            w32(o, o.office(office) + 4 + 4 * w, max(50000, FEAST[w] * guests * 4 + 4000))
    w32(o, o.office(office) + 4 + 4 * 7, 2 * guests + 1)
    feast_op(o, p, now + 0x60, tp, 500)
    print(f'IA {a} en {ta}, {b} en {tb} (dinero {money_b}), {c} en {tc} (deudas), {a} en {none} (sin oficina);'
          f' jugador {p} en {tp} (oficina {office}, {guests} invitados)')
    return show_feast(o, [ta, tb, tc, none, tp], [a, b, c, p], office)


def fiesta_vacia(o):
    """La fiesta del jugador (operación 0x61 y evento 0x07) con una sola de las
    mercancías de la fiesta en su oficina: la ciudad no la apunta y su
    reputación baja 1. Graba 1 día."""
    now = o.u32(WORLD + 0x14)
    p = o.u32(to.PLAYER)
    office = merchant_offices(o, p)[0]
    tp = o.u8(o.office(office) + 0x2C6)
    guests = feast_guests(o, p, tp)
    for w in range(8):
        w32(o, o.office(office) + 4 + 4 * w, 0)
    w32(o, o.office(office) + 4, 3 * guests + 2000)
    feast_op(o, p, now + 0x10, tp, 0)
    print(f'jugador {p} en {tp} (oficina {office}, {guests} invitados)')
    return show_feast(o, [tp], [p], office)


def fiesta_ciudad(o):
    """La fiesta de la ciudad ([+0x84] y [+0x88] del mundo) pasa en la segunda
    ciudad con oficinas de comerciantes a los que puede invitar y el evento
    0x03 se adelanta al tick siguiente: 0x5316E0 encola la operación 0x61 de
    un comerciante de la IA al azar, con su evento 0x07 a los 10 a 30 días.
    Graba hasta el evento."""
    now, towns = o.u32(WORLD + 0x14), o.u32(WORLD + 0x10)

    def guest(f):  # los que mira 0x531470: de la IA y sin el bit 4 de [+0x8]
        m = o.merchant(o.u16(o.office(f) + 0x2C4))
        return o.u16(m + 8) and not o.u8(m + 8) & 4
    town = [t for t in range(towns) if any(guest(f) for f in office_list(o, t))][1]
    w32(o, WORLD + 0x84, now)
    w8(o, WORLD + 0x88, town)
    i, a = next((i, a) for i, a in events(o) if o.u16(a + 6) == 3)
    move_event(o, i, a, now + 1)
    pop = o.u32(o.town(town) + 0x2D4)
    print(f'fiesta en la ciudad {town} ({pop} habitantes): el evento 0x07 hacia el día'
          f' {((pop % 11 + 5) << 9) // 256 + 1}')
    return show_feast(o, [town], [], 0)


def yard_op(o, merchant, town, kind, force):
    o.tick_operations.append(struct.pack('<5I', 0x2C, kind, merchant, town, force))


def ships_in(o, m, town):
    """Los barcos del comerciante en la ciudad que mira 0x52A360 (estado < 4)."""
    out, s = [], o.u16(o.merchant(m) + 0xE)
    while s < o.u16(to.SHIPS + 0xF4):
        if o.u8(o.ship(s) + 0x39) == town and o.u16(o.ship(s) + 0x134) < 4:
            out.append(s)
        s = o.u16(o.ship(s) + 4)
    return out


def show_yard(o, merchants):
    """El primer barco de cada comerciante, su nombre y su dinero."""
    def line(m):
        s = o.u16(o.merchant(m) + 0xE)
        name = bytes(o.uc.mem_read(o.ship(s) + 0x160, 24)).split(b'\0')[0] if s < o.u16(to.SHIPS + 0xF4) else b''
        return m, s, name.decode('latin-1'), struct.unpack('<i', bytes(o.uc.mem_read(o.merchant(m), 4)))[0]
    return lambda: print(f'(comerciante, primer barco, nombre, dinero): {[line(m) for m in merchants]}')


YARD_WARES = ((0xB, 2000), (8, 200), (0xC, 200), (0x11, 2000), (0xF, 200))  # los materiales de los barcos


def yard_materials(o, t, n=40):
    """n unidades (cargas o barriles) de cada material de los barcos en la ciudad."""
    for w, unit in YARD_WARES:
        w32(o, o.town(t) + 4 + 4 * w, n * unit)


def astillero(o):
    """Barcos encargados al astillero (operación 0x2C, 0x52A360), al final del
    primer tick, en la ciudad A con astillero para craieres (con 150 de cada
    material) que más comerciantes de la IA con oficina y barcos tiene: uno
    de ellos (con paño negativo en la oficina y, si tiene barco allí, con
    ruta automática y madera, brea y cáñamo) encarga un craier; otro sin
    oficina allí, una coca (o un craier, si el astillero no las hace); un
    tipo grande en una ciudad con [0x2C8] & 1 (no); una ciudad sin astillero
    (no); uno sin dinero y sin forzar (no); el jugador, con dinero, una
    snaikka (o un craier) sin forzar; y otro craier en una ciudad con
    astillero y sin materiales (no). Graba 1 día."""
    towns, merchants = o.u32(WORLD + 0x10), o.u16(WORLD + 0xA)
    p = o.u32(to.PLAYER)

    def yard(t, kinds):
        return o.u32(o.town(t) + 0x76C) & 0x20000 and all(o.u8(o.town(t) + 0x824 + k) < 0x80 for k in kinds)

    def owners(t):
        return [m for m in sorted({o.u16(o.office(f) + 0x2C4) for f in office_list(o, t)})
                if m < merchants and m != p and o.u16(o.merchant(m) + 8)]
    a = max((t for t in range(towns) if yard(t, (1,)) and owners(t)),
            key=lambda t: (sum(1 for m in owners(t) if ships_in(o, m, t)), len(owners(t))))
    yard_materials(o, a, 150)
    big = 3 if yard(a, (3,)) else 1
    small = 0 if yard(a, (0,)) else 1
    m1 = max(owners(a), key=lambda m: len(ships_in(o, m, a)))
    office = next(f for f in office_list(o, a) if o.u16(o.office(f) + 0x2C4) == m1)
    w32(o, o.office(office) + 4 + 4 * 8, -500)
    ships = ships_in(o, m1, a)
    if ships:
        s = ships[0]
        w8(o, o.ship(s) + 0x136, o.u8(o.ship(s) + 0x136) | 1)
        for w, n in ((0xB, 6000), (0xF, 1000), (0x11, 4100)):
            w32(o, o.ship(s) + 0x54 + 4 * w, n)
    here = set(owners(a)) | {o.u16(o.office(f) + 0x2C4) for f in office_list(o, a)}
    m2 = next(m for m in range(merchants) if m not in here and m != p and o.u16(o.merchant(m) + 8))
    c = next(t for t in range(towns) if t != a and yard(t, (2,)))
    w32(o, o.town(c) + 0x2C8, o.u32(o.town(c) + 0x2C8) | 1)
    d = next(t for t in range(towns) if not o.u32(o.town(t) + 0x76C) & 0x20000)
    e = next(t for t in range(towns) if t not in (a, c) and yard(t, (1,)))
    for w, _ in YARD_WARES:
        w32(o, o.town(e) + 4 + 4 * w, 0)
    m3 = next(m for m in range(merchants) if m not in (m1, m2, p) and o.u16(o.merchant(m) + 8))
    w32(o, o.merchant(m3), 100)
    w32(o, o.merchant(p), 500000)
    yard_op(o, m1, a, 1, 1)
    yard_op(o, m2, a, big, 1)
    yard_op(o, m1, c, 2, 1)
    yard_op(o, m1, d, 1, 1)
    yard_op(o, m3, a, big, 0)
    yard_op(o, p, a, small, 0)
    yard_op(o, m1, e, 1, 1)
    print(f'ciudad A {a}: {m1} (oficina {office}, barcos {ships}), {m2} sin oficina; ciudad pequeña {c}, sin astillero {d},'
          f' sin materiales {e}; sin dinero {m3}; jugador {p}')
    return show_yard(o, [m1, m2, m3, p])


def astillero_ciudad(o):
    """El evento 0x2B (0x4DE790) adelantado al tick siguiente con la ciudad 0
    muy poblada (más de 400 habitantes por barco, contados como el evento) y
    40 de cada material en las ciudades con astillero: el comerciante de la IA
    con menos barcos encarga uno (operación 0x2C). Graba 1 día."""
    i, a = next((i, a) for i, a in events(o) if o.u16(a + 6) == 0x2B)
    move_event(o, i, a, o.u32(WORLD + 0x14) + 1)
    towns, merchants = o.u32(WORLD + 0x10), o.u16(WORLD + 0xA)
    total = sum(o.u32(o.town(t) + 0x2D4) for t in range(towns))
    ships = 0
    for m in range(merchants):
        flags = o.u16(o.merchant(m) + 8)
        if flags == 0 or flags & 4:
            ships += o.u16(o.merchant(m) + 0x478)
        else:
            s = o.u16(o.merchant(m) + 0xE)
            while s < o.u16(to.SHIPS + 0xF4):
                ships += 1
                s = o.u16(o.ship(s) + 4)
    set_population(o, 0, o.u32(o.town(0) + 0x2D4) + max(0, ships * 400 - total) + 1000)
    for t in range(towns):
        if o.u32(o.town(t) + 0x76C) & 0x20000:
            yard_materials(o, t)
    print(f'{total} habitantes, {ships} barcos: la ciudad 0 con {o.u32(o.town(0) + 0x2D4)}')
    return show_yard(o, range(merchants))


def eventos_op(o):
    """Las operaciones que solo crean o tocan eventos, al final del primer
    tick: 0x38 (subasta de barco, evento 0x0A, a 40 días en la ciudad 30 %
    ciudades; y otra a menos de 40 días: no), 0x7C (la peste, evento 0x1C,
    mañana y con un día que ya ha pasado: al empezar el siguiente), 0x7F (las
    ciudades 0, 1 y una que no existe con 0x4000000 y eventos 0x35 a los 128
    ticks; y otra con un tick pasado: no), 0x80 (el registro 0x80 de la cola;
    si está libre, con el tipo 5) y 0x83 (un evento 0x00 con grano para la
    ciudad 2, como en una partida en red). Graba 2 días. Devuelve lo que
    escribe al final."""
    now, day = o.u32(WORLD + 0x14), o.u16(WORLD + 4)
    base = o.u32(to.EVENTS)
    r80 = base + 0x18 * 0x80
    kind = o.u16(r80 + 6) if o.u16(to.EVENTS + 0xC) > 0x80 else None
    if kind == 0xFFFF:
        w16(o, r80 + 6, 5)
    ops = [(0x80, 0, 0x1234, 0x5678, 0), (0x38, now + 0x2810, 30, 0, 0), (0x38, now + 0x100, 1, 0, 0),
           (0x7F, 0, 1, 0xFF, now + 0x80), (0x7F, 2, 3, 4, now), (0x83, now + 0x40, 20000, 2, 0)]
    for op in ops:
        o.tick_operations.append(struct.pack('<5I', *op))
    # La peste como la encola 0x4DCA90: 4 bytes, el fin, 0, habitantes / 40, el día y la ciudad.
    for town, when in ((3, day + 1), (4, max(day - 1, 0))):
        o.tick_operations.append(struct.pack('<I4BIHHHBB', 0x7C, 0x31, 0x40, 0x45, 0x50, now + 0x900, 0,
                                             o.u32(o.town(town) + 0x2D4) // 40, when, town, 0))
    print(f'día {day}; registro 0x80: tipo {kind if kind is None else hex(kind)}')
    return lambda: print(f'[0x2C8] de las ciudades 0..4: {[hex(o.u32(o.town(t) + 0x2C8)) for t in range(5)]};'
                         f' registro 0x80: {bytes(o.uc.mem_read(r80, 0x18)).hex()}')


def propuestas(o):
    """Al final del primer tick: 0x84 (el encargo del jefe de la Hansa, evento
    0x30: uno de tipo 5 pedido antes de 0x100 ticks, que se retrasa, y otro de
    tipo 2 a los 3 días); 0x67 (el administrador de una oficina mejora; otra
    con un administrador que no es el suyo y otra con un comerciante que no
    existe; si alguna oficina tiene administrador); y 0x48 (la ampliación de una ciudad con consejo, como 0x51E270:
    la votación 0x2E; y en la ciudad 5 sin consejo, sin los bits 0xE0 de
    [0x2C8]: el impuesto extraordinario, evento 0x31, y el impuesto por cabeza
    con 7 y con 25, que se queda en 10). Graba 4 días. Devuelve lo que
    escribe al final."""
    now = o.u32(WORLD + 0x14)
    towns, merchants = o.u32(WORLD + 0x10), o.u16(WORLD + 0xA)
    captains = o.u16(to.SHIPS + 0xF2)
    for kind, when in ((5, now + 0x50), (2, now + 0x300)):
        o.tick_operations.append(struct.pack('<II4B6BH', 0x84, when, 3, kind, 0x41, 0x42, 1, 2, 3, 4, 5, 6, 0))
    office = next((f for f in range(o.u16(WORLD + 8)) if o.u16(o.office(f) + 0x2F2) < captains), None)
    if office is not None:
        admin, m, town = o.u16(o.office(office) + 0x2F2), o.u16(o.office(office) + 0x2C4), o.u8(o.office(office) + 0x2C6)
        for a, mm in ((admin, m), ((admin + 1) % captains, m), (admin, merchants + 3)):
            o.tick_operations.append(struct.pack('<5I', 0x67, a, mm, town, 0x60))
        print(f'oficina {office} del comerciante {m} en {town}: administrador {admin}')
    else:
        print('ninguna oficina con administrador: sin 0x67')
    o.tick_operations.append(struct.pack('<IIIBBBBBBH', 0x48, now + 0x700, 0, 1, 1, 0, 1, 0xFF, 0, 0))
    w32(o, o.town(5) + 0x2C8, o.u32(o.town(5) + 0x2C8) & ~0xE0)
    for kind, x in ((0, 0), (3, 7), (3, 25), (2, 9)):
        o.tick_operations.append(struct.pack('<IIIBBBBBBH', 0x48, now + 0x100, 5000, 5, kind, x, 0, 0, 0, 0))
    print(f'impuesto por cabeza de la ciudad 5: {o.u8(o.town(5) + 0x6F8)}')
    return lambda: print(f'impuesto por cabeza de la ciudad 5: {o.u8(o.town(5) + 0x6F8)}')


def records_of(o, t, head):
    """(registro, x, y) de una lista de edificios de la ciudad ([0x75C], siguiente en [+4])."""
    out, r = [], head
    base, n = o.u32(o.town(t) + 0x75C), o.u16(o.town(t) + 0x776)
    while r < n:
        out.append((r, o.u8(base + 8 * r), o.u8(base + 8 * r + 1)))
        r = o.u16(base + 8 * r + 4)
    return out


def subasta(o):
    """Edificios en venta (operación 0x28, 0x53D190 con 0x4DAB00), al final del
    primer tick, de comerciantes de la IA con oficina y al menos dos empresas
    en una ciudad: en la ciudad A hay una subasta en fase 2 sin edificios (a
    16 días): uno se añade a ella; en la B, uno abre una subasta nueva y el
    segundo se añade a esa (fase 1); en la C su dueño no tiene el bit de la
    ciudad en [+0x468] (no); y una celda vacía de la B (no está: recuenta la
    oficina). Graba 2 días."""
    now = o.u32(WORLD + 0x14)
    found = []
    for f in range(o.u16(WORLD + 8)):
        m, t = o.u16(o.office(f) + 0x2C4), o.u8(o.office(f) + 0x2C6)
        if m >= o.u16(WORLD + 0xA) or not o.u16(o.merchant(m) + 8) or t in [x[1] for x in found]:
            continue
        recs = records_of(o, t, o.u16(o.office(f) + 0x2CE))
        if len(recs) >= 2 and o.u32(o.merchant(m) + 0x468) & (1 << (t & 31)):
            found.append((m, t, recs))
    (ma, ta, ra), (mb, tb, rb), (mc, tc, rc) = found[:3]
    a = o.call(0x4D8CF0, [now + 0x1000], ecx=to.EVENTS)
    w16(o, a + 6, 0x36)
    for k in range(3):
        w16(o, a + 8 + 2 * k, 0xFFFF)
    w16(o, a + 0xE, 0x1000)
    w32(o, a + 0x10, 0)
    for off, v in ((0x14, 2), (0x15, ta), (0x16, 0), (0x17, 0xFF)):
        w8(o, a + off, v)
    w32(o, o.merchant(mc) + 0x468, o.u32(o.merchant(mc) + 0x468) & ~(1 << (tc & 31)))
    for m, t, (r, x, y) in ((ma, ta, ra[0]), (mb, tb, rb[0]), (mb, tb, rb[1]), (mc, tc, rc[0]), (mb, tb, (0, 0, 0))):
        o.tick_operations.append(struct.pack('<5I', 0x28, t, m, x, y))
    print(f'A: {ma} en {ta} {ra[:1]}; B: {mb} en {tb} {rb[:2]}; C: {mc} en {tc} {rc[:1]}; ajustes [+0xD]'
          f' {o.u8(o.u32(to.SETTINGS_POINTER) + 0xD)}')

    def show():
        for i, e in events(o):
            if o.u16(e + 6) == 0x36:
                print(f'subasta {i}: {bytes(o.uc.mem_read(e, 0x18)).hex()}')
    return show


def subasta_ajuste(o):
    """subasta con el byte [+0xD] de los ajustes a 0 (en las partidas vale 3):
    el segundo edificio de la B se añade a la subasta en fase 1 que abre el
    primero. Graba 2 días."""
    w8(o, o.u32(to.SETTINGS_POINTER) + 0xD, 0)
    return subasta(o)


def temporada(o):
    """La temporada de piratas que viene (operación 0x7E, 0x53F8B0, el evento
    0x1F), al final del primer tick: con los bits 0..2 a 0 en los cuatro u32
    (todos los cambios) y con 7, 5, 3, 6. Graba 1 día. Devuelve lo que
    escribe al final."""
    for words in ((0x12340008, 0x56780010, 0x9ABC0018, 0xDEF00020), (0x11111117, 0x22222225, 0x33333333, 0x44444446)):
        o.tick_operations.append(struct.pack('<5I', 0x7E, *words))

    def show():
        for i, e in events(o):
            if o.u16(e + 6) == 0x1F:
                print(f'evento {i}: {bytes(o.uc.mem_read(e, 0x18)).hex()}')
    return show


FLAMMABLE = {t for t in range(4, 0x1F)} | {0x29, 0x2C}  # 0x672B3C


def incendio_ciudad(o):
    """Incendios en las ciudades (operación 0x7D, el evento 0x1D 0x4E9564), al
    final del primer tick, cada uno en su ciudad y con su evento a tick
    múltiplo de 64 (se extiende enseguida): uno que empieza en un almacén
    (0x1E) de un comerciante (0x500C30) y puede arder 4 edificios; otro en una
    casa de una oficina ([+0x2D0]), 5; y otro en un edificio de la ciudad
    ([0x764]), 3. Graba 4 días. Devuelve lo que escribe al final."""
    now, towns = o.u32(WORLD + 0x14), o.u32(WORLD + 0x10)
    base = lambda t: o.u32(o.town(t) + 0x75C)
    kind = lambda t, r: o.u8(base(t) + 8 * r + 3)
    used, fires = set(), []

    def first(lists, test):
        for t, head in lists:
            if t in used:
                continue
            for r, x, y in records_of(o, t, head):
                if test(t, r):
                    used.add(t)
                    return t, r, x, y
        return None
    offices = [(o.u8(o.office(f) + 0x2C6), f) for f in range(o.u16(WORLD + 8))]
    for lists, test, top in (([(t, o.u16(o.office(f) + 0x2CE)) for t, f in offices], lambda t, r: kind(t, r) == 0x1E, 4),
                             ([(t, o.u16(o.office(f) + 0x2D0)) for t, f in offices], lambda t, r: kind(t, r) in FLAMMABLE, 5),
                             ([(t, o.u16(o.town(t) + 0x764)) for t in range(towns)], lambda t, r: kind(t, r) in FLAMMABLE, 3)):
        hit = first(lists, test)
        if hit:
            t, r, x, y = hit
            fires.append((t, r, kind(t, r)))
            o.tick_operations.append(struct.pack('<IIHHBBBBI', 0x7D, (now + 0x40) & ~0x3F, 0, top, x, y, t, 0, 0))
    print(f'incendios (ciudad, registro, tipo): {fires}')
    return lambda: print(f'después: {[(t, hex(o.u32(o.town(t) + 0x2C8)), o.u16(o.town(t) + 0x76A)) for t, _, _ in fires]}')


def capitan_jugador(o):
    """El capitán de un barco del jugador se retira (operación 0x13, 0x538C40,
    y el evento 0x27 al tick siguiente), al final del primer tick; y otra con
    un capitán que no existe (nada). Graba 1 día. Devuelve lo que escribe al
    final."""
    p, ships, captains = o.u32(to.PLAYER), o.u16(to.SHIPS + 0xF4), o.u16(to.SHIPS + 0xF2)
    s = o.u16(o.merchant(p) + 0xE)
    while s < ships and o.u16(o.ship(s) + 0x42) >= captains:
        s = o.u16(o.ship(s) + 4)
    c = o.u16(o.ship(s) + 0x42)
    o.tick_operations.append(struct.pack('<5I', 0x13, s, c, 0, 0))
    o.tick_operations.append(struct.pack('<5I', 0x13, s, captains + 5, 0, 0))
    print(f'barco {s} del jugador {p}, capitán {c}, estado {o.u16(o.ship(s) + 0x134)}')
    return lambda: print(f'después: capitán {o.u16(o.ship(s) + 0x42)}, estado {o.u16(o.ship(s) + 0x134)}')


def boda(o):
    """Weddings (operation 0x60, 0x53DA90, and event 0x15, 0x4E7C74), queued at
    the end of the first tick for the next day: the AI merchant with the
    biggest fleet (its first ship with 240 more loads of hold: a cog and a
    crayer), in a town with [0x2C8] & 1 (the cog becomes a crayer), wares 0xFF (the second one becomes tools); the player, wares
    0x31, in his home town (his letters' marks are cleared); a merchant whose
    [+0x28] has passed (nothing); and one with a past tick (nothing). Each
    wedding ends in a feast (event 0x07). Records 2 days. Returns what it
    prints at the end."""
    now, towns, merchants = o.u32(WORLD + 0x14), o.u32(WORLD + 0x10), o.u16(WORLD + 0xA)
    p = o.u32(to.PLAYER)
    when = (now + 0x180) & ~0xFF

    def fleet(m):
        total, s = 0, o.u16(o.merchant(m) + 0xE)
        while s < o.u16(to.SHIPS + 0xF4):
            total += struct.unpack('<i', bytes(o.uc.mem_read(o.ship(s) + 0x10, 4)))[0] // 40000
            s = o.u16(o.ship(s) + 4)
        return total
    ai = [m for m in range(merchants) if o.u16(o.merchant(m) + 8) and not o.u8(o.merchant(m) + 8) & 4]
    big = max(ai, key=fleet)
    other = next(m for m in ai if m != big)
    small = next((t for t in range(towns) if o.u32(o.town(t) + 0x2C8) & 1), 0)
    for m in (big, p):
        w32(o, o.merchant(m) + 0x28, 0xFFFFFFFF)
    first = o.u16(o.merchant(big) + 0xE)  # a bigger hold: more than 10 points, a cog and a crayer
    w32(o, o.ship(first) + 0x10, o.u32(o.ship(first) + 0x10) + 480000)
    w32(o, o.merchant(other) + 0x28, now)
    for m, town, wares in ((big, small, 0xFF), (p, o.u8(o.merchant(p) + 0x19), 0x31), (other, 0, 0x22)):
        o.tick_operations.append(struct.pack('<IIHBBBBBBBBH', 0x60, when, 5500, wares, 7, 0x85, 3, town, 1, 0, m, 0))
    o.tick_operations.append(struct.pack('<IIHBBBBBBBBH', 0x60, now, 5500, 0x22, 7, 0x85, 3, 0, 1, 0, big, 0))
    print(f'boda de {big} (flota {fleet(big)}) en {small}, del jugador {p} (flota {fleet(p)}), {other} sin plazo')

    def show():
        for m in (big, p, other):
            s = o.u16(o.merchant(m) + 0xE)
            print(f'{m}: [+0x28] {o.u32(o.merchant(m) + 0x28):#x}, [+0x478] {o.u16(o.merchant(m) + 0x478)}, primer barco {s}'
                  f' tipo {o.u8(o.ship(s) + 0xE)} carga {[x for x in struct.unpack("<24i", bytes(o.uc.mem_read(o.ship(s) + 0x54, 96))) if x]}')
    return show


def quiebra(o):
    """Bankruptcies (operation 0x65, 0x4F5D80) at the end of the first tick: an
    AI merchant made mayor of his home town, with two debts (events 0x01 at
    16 days: one owed to him, one owed by him); and the player (nothing).
    Records 2 days. Returns what it prints at the end."""
    now = o.u32(WORLD + 0x14)
    m = ai_with_office(o)[0]
    p = o.u32(to.PLAYER)
    home = o.u8(o.merchant(m) + 0x19)
    w8(o, o.town(home) + 0x6F1, m)
    for creditor, debtor in ((m, (m + 1) % o.u16(WORLD + 0xA)), ((m + 2) % o.u16(WORLD + 0xA), m)):
        a = o.call(0x4D8CF0, [now + 0x1000], ecx=to.EVENTS)
        w16(o, a + 6, 1)
        w32(o, a + 8, 20000)
        w32(o, a + 0xC, now & ~0xFF)
        for off, v in ((0x10, creditor), (0x11, debtor), (0x12, 0), (0x13, 3), (0x14, home)):
            w8(o, a + off, v)
    for who in (m, p):
        o.tick_operations.append(struct.pack('<5I', 0x65, who, 0, 0, 0))
    print(f'quiebra de {m} (alcalde de {home}), y del jugador {p}')

    def show():
        mm = o.merchant(m)
        print(f'{m}: dinero {o.u32(mm)}, nombre {o.u8(mm + 0x1C)}/{o.u8(mm + 0x1D)}, alcalde de {home}:'
              f' {o.u8(o.town(home) + 0x6F1)}; deudas: {[bytes(o.uc.mem_read(a + 0xC, 9)).hex() for _, a in events(o) if o.u16(a + 6) == 1][:4]}')
    return show


def hijo(o):
    """Children (operation 0x87, 0x53FEA0) at the end of the first tick: an AI
    merchant married 300 days ago gets one, and a second one right after
    does not (the first was just born); an unmarried merchant and one married
    100 days ago get none. Records 1 day. Returns what it prints at the end."""
    now, towns = o.u32(WORLD + 0x14), o.u32(WORLD + 0x10)
    a, b, c = ai_with_office(o)[:3]
    for m, days in ((a, 300), (c, 100)):
        mm = o.merchant(m)
        w8(o, mm + 0x31, o.u8(mm + 0x19))
        w32(o, mm + 0x28, now - 18 * 365 * 256)
        w32(o, mm + 0x2C, now - days * 256)
    w8(o, o.merchant(b) + 0x31, 0xFF)
    for m, name in ((a, 11), (a, 12), (b, 13), (c, 14)):
        o.tick_operations.append(struct.pack('<IBB4B10x', 0x87, m, name, 1, 2, 3, 4))
    print(f'casado {a} (300 días), {c} (100 días), soltero {b}')
    return lambda: print(f'hijos: {[(m, o.u8(o.merchant(m) + 0x36), o.u16(o.merchant(m) + 0x34)) for m in (a, b, c)]}')


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
                                     jugador_coge, jugador_vencido,
                                     nueva_sin_marca, nueva_alcalde_ia, nueva_alcalde_humano, nueva_sin_alcalde,
                                     poblada, rica, fundar_aceite, robo, asedio, capitanes, impuesto, monumento, incendio, dieta,
                                     fiesta, fiesta_vacia, fiesta_ciudad, astillero, astillero_ciudad, eventos_op,
                                     propuestas, subasta, subasta_ajuste, temporada, incendio_ciudad,
                                     capitan_jugador, boda, quiebra, hijo)}


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
    after = SCENARIOS[a.scenario](o)
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
    if callable(after):
        after()
    last = o.u32(WORLD + 0x10) - 1
    nodes, node = [], o.u32(o.town(last) + 0x9B4)
    while node:
        nodes.append(bytes(o.uc.mem_read(node + 4, 0x10)).hex())
        node = o.u32(node)
    print(f'nodos de la ciudad {last}: {nodes}')
    for i, e in events(o):
        if o.u16(e + 6) == 0x32:
            raw = bytes(o.uc.mem_read(e, 0x18))
            print(f'encargo {i}: tipo {raw[0xC]} comerciante {raw[0xD]} plazo {o.u32(e + 8):#x} datos {raw[0x10:].hex()}')
        elif o.u16(e + 6) == 0x2F:
            t = o.town(o.u32(e + 0xC))
            print(f'evento {i}: 0x2F de la ciudad {o.u32(e + 0xC)} en el tick {o.u32(e):#x} (ahora {o.u32(WORLD + 0x14):#x}),'
                  f' fases {o.u32(e + 0x10):#x}, [0x76C] {o.u32(t + 0x76C):#x}, {o.u32(t + 0x2D4)} habitantes,'
                  f' alcalde {o.u8(t + 0x6F1)}, oficinas {office_list(o, o.u32(e + 0xC))}')


if __name__ == '__main__':
    main()
