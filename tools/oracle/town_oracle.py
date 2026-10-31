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
  Ji Ui  el ejército que sitia la ciudad i ([+0x8] de su asedio, 0x650 bytes;
         vacío si no hay) y sus objetivos ([+0x250] del ejército, 12 bytes
         cada uno: todos los reservados, también los que ya no cuenta [+0x242])
  Li Hi  las listas [+0x101C] y [+0x1020] del asedio de la ciudad i: los 8 y
         16 primeros bytes de cada nodo, en orden
  E      cola de eventos (*0x6DD73C, tantos registros de 0x18 como [0x6DD748])
  B      buzón de los avisos (*0x6DD730, tantas entradas de 16 bytes como [0x6DD736])
  Y      tiempos de viaje entre ciudades ([+0x6C] del mundo, n × n bytes)
  V      convoyes (*0x6DD7A8, tantos registros de 0x3C como [0x6DD896])
  Q      rutas de los barcos: por cada barco con [+0x24], u16 barco, u32 nº de
         puntos y los puntos (i16 x, i16 y)
  K0..K7 los 40 bytes de cada hueco del objeto 0x6DE588 (*[0x6DEF90 + 4·i]; vacío si no hay)
  Z0..Z4 los 0x18 bytes de los 5 piratas del objeto de los barcos (*[0x6DD7AC + 4·i]; vacío si no hay)
  ZMi ZDi ZPi  what pirate i points to: the map of its nest ([+0x0], 0x6C bytes),
         its cells ([+0x68] of the map, width × height) and the pool ([+0x4],
         [+0x8] entries of 8 bytes); empty if it has none
  F0..F14 los encargos de los 15 objetos del mundo (lista de [+0x24] de 0x6DEFB0 + 0x2C·i):
         los 16 bytes de datos de cada nodo, en orden
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
       [--desde DÍA] [--max N] [--semilla S] [--cpr p2arch0_eng.cpr] [--barcos DATOS [--sin-ia] [--sin-convoyes]
       [--sin-comerciantes] [--resto PIEZAS | --sin-resto] [--operaciones CÓDIGOS] [--no-storms] [--pirates [--no-disband]]] [--write OUT]
La función 51ba10 graba los ticks de ciudad enteros.

Con --cpr (o si DATOS/scripts tiene los ficheros), la carga crea también el
objeto de los nombres (0x6DDAA0) con el código del juego: las listas de
scripts/Namen*.txt con sus números de scripts/Namen_eng.ini (0x5121C0) y los
nombres escritos por los jugadores que guarda la partida (0x511E30). La cola
de eventos saca nombres al azar con esos números. 0x5121C0 llena también la
tabla de las ciudades de scripts/StadtDaten.ini (0x6DDB90, 0x51E440), y se
leen los puntos de scripts/Expeditionen.dat (*0x6DF2E0, como 0x5346A0). Los
textos de los mensajes (CString de MFC, sprintf) se ejecutan: las pocas
funciones de KERNEL32 que usan se hacen en Python (setup_imports).

Con --barcos (la carpeta de --navdata de save_oracle.py), cada tick hace
además la actualización de los barcos del juego (0x506720) con los estados
que reproduce p3vita (en puerto, 0 a 5; en el mar, 0x0F; los convoyes de los
comerciantes, pero no los de los piratas, que se quedan quietos, ni los
encuentros de 0x5053C0) y, al final del tick, ejecuta con el ejecutor del
juego (0x535760) las operaciones que se han encolado, en su orden, de los
códigos de --operaciones (por omisión 0,12,1e,1f,22,23,24,27,3b,46,4b,78,79,7a,7b,85,86:
zarpar, los capitanes, las expediciones, un barco nuevo, construir con sus materiales y murallas, la dieta de la Hansa, los
capitanes y administradores que buscan trabajo, el alcalde de una ciudad nueva y los encargos de la Hansa: materiales de la
carretera, cogerlos, crearlos y revisarlos); las demás se descartan. El ejecutor se llama por un
trampolín que le pone en la pila los 0x14 bytes de la operación y una marca:
al grabar 535760, tick_probe la lee de los argumentos (y salta las llamadas
que hace el propio juego, sin la marca). Con --sin-convoyes se salta
todo el recorrido de los convoyes. También hace el tick de cada comerciante
(0x4F7BB0), como advance_time: en los ticks con t & 7 == 1 el comerciante
t >> 3 y con t & 7 == 6 el (t + 0x100) >> 3, antes de los barcos
(--sin-comerciantes lo salta).

Con --barcos hace además el resto de advance_time, en su orden (--resto
PIEZAS, direcciones separadas por comas, solo las pedidas; --sin-resto,
ninguna):
  5315c0  al empezar el día, tras la fecha: lo diario (semanal si el tick es
          múltiplo de 0x700), y el [+0x8B] del mundo baja 1 si no es 0
  535450  al empezar el día: las tareas programadas ([+0xE8] del mundo, con el tick)
  52d7d0  t & 7 == 5: el objeto t >> 3 de los 15 del mundo ([+0xB10])
  6349f0  t & 7 == 7: el asedio de la ciudad t >> 3, tras su tick
  4d85c0  tras los barcos: la cola de eventos (con el tick)
  633a90  tras la cola de eventos: el asedio de cada ciudad
(0x54DE90, las estadísticas de cada día, no se ejecuta.) El juego ejecuta
la cola entre dos llamadas a advance_time, que hace hasta 8 ticks según el
tiempo real; aquí y en p3vita, tras cada tick. La cola de eventos necesita lo
de scripts/ (--cpr). Para repetir el mundo con tick_probe --continuo hay que
grabar también 4f7bb0 y las piezas que se ejecuten:
51ba10,4f7bb0,506720,535760,5315c0,535450,52d7d0,6349f0,633a90,4d85c0
(535760, el ejecutor, o 537670, solo las órdenes de zarpar, pero no las dos).
With --write OUT, after the days it runs the game's save code on the state
reached and writes the uncompressed .pat (write_oracle.py), to compare with
tick_probe --write.
Con --sin-ia, los barcos como en sail_oracle.py: en puerto solo los estados
1, 2, 3 y el 0 de los jugadores humanos, y las operaciones encoladas no se
ejecutan.
"""
import argparse
import ctypes
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from save_oracle import (EXPEDITIONS, FILE, SENTINEL, SEPARATOR, SHIPS, STACK, STACK_SIZE, TOWN_TABLE, WORLD,  # noqa: E402
                         SaveOracle)
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

TOWN_TICK = 0x51BA10
MERCHANT_TICK = 0x4F7BB0  # thiscall sobre el comerciante, con su índice
UPDATE_SHIPS = 0x506720
SET_SAIL = 0x537670  # operación 0x00
EXECUTOR = 0x535760  # ejecuta una operación encolada (thiscall sobre ella)
EXECUTOR_MAGIC = 0x504F3350  # «P3OP»: tras la operación en la pila, la marca de las llamadas del oráculo
OPERATIONS = (0x00, 0x0A, 0x12, 0x13, 0x1E, 0x1F, 0x22, 0x23, 0x24, 0x27, 0x28, 0x29, 0x2C, 0x37, 0x38, 0x3B, 0x46, 0x48, 0x4B, 0x60, 0x61, 0x64,
              0x65, 0x67, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x83, 0x84, 0x85, 0x86,
              0x87, 0x8E, 0xB8)  # las que se ejecutan por omisión
UPDATE_DATE = 0x5310D0
RAND = 0x639C03
MERGE_TOWN_FLAGS = 0x5348B0  # la carga lo llama con 0x6DF278 tras leer el mundo
SETTINGS_POINTER = 0x6CC3E8
SETTINGS_SIZE = 0x4C
PLAYER = 0x6DFC14
SIEGES = 0x6DF270
EVENTS = 0x6DD73C  # cola de eventos: puntero al array, cabeza u16 en +8, número u16 en +0xC
MAIL = 0x6DD730  # buzón de los avisos: puntero a las entradas de 16 bytes, número u16 en +6
PLAYER_TOWNS = 0x6DEF90  # 8 punteros a 40 bytes (un byte por ciudad) del objeto 0x6DE588
GLOBALS = (0x6DD000, 0x6E0000)
# Objeto de los nombres (0x5121C0 lo carga al arrancar el juego): punteros a
# los textos de cada lista en +0xA8 (hombres), +0xAC (mujeres), +0xB0
# (apellidos y piratas) y +0xB4 (barcos), a sus offsets (u16 por nombre) en
# +0xBC.. +0xC8, y los números u16: +0xD0 hombres, +0xD2 mujeres, +0xD4
# apellidos, +0xD6 apellidos y piratas, +0xD8 y +0xDA barcos. Tras cada lista
# de personas, 20 huecos de 24 bytes para los nombres escritos por los jugadores.
NAMES = 0x6DDAA0
LOAD_NAMES = 0x5121C0
READ_SAVE_NAMES = 0x511E30  # los nombres de la partida (los de los jugadores)
ARCHIVE_READ = 0x6389EE  # importada: (archivo, ruta, &datos, &tamaño) → asa (0: no está); cdecl
ARCHIVE_FREE = 0x6389F4  # importada: (archivo, asa); cdecl
INI_INT = 0x4BE0B0  # thiscall (sección, clave, por omisión, fichero) → entero; ret 0x10
INI_STRING = 0x4BE060  # thiscall (sección, clave, por omisión, búfer, tamaño, fichero) → longitud; ret 0x18
# Tabla de las 40 ciudades (0x34 bytes por ciudad, por su número de
# StadtDaten.ini): la llena 0x51E440 desde 0x5121C0. En +0x28, un CString.
TOWN_TABLE_SIZE = 0x34
EMPTY_CSTRING = 0x6C7CCC  # puntero a la cadena vacía de MFC con el que empieza un CString
ENQUEUE = 0x54AA70  # encola una operación: se anota y no se ejecuta
OPERATION_SIZE = 0x14  # lo que copia 0x543F10; lo que sigue en la pila es basura
# El tick de ciudad se llama por este trampolín para que vuelva a una
# instrucción de verdad y no a SENTINEL (donde la emulación se para sin pasar
# por los ganchos): si no, Recorder no ve la salida de 0x51BA10.
TRAMPOLINE = SENTINEL + 0x10
SHIPS_TRAMPOLINE = SENTINEL + 0x20
MERCHANT_TRAMPOLINE = SENTINEL + 0x40
# El resto de advance_time (con --barcos; --resto, --sin-resto): función → bytes de argumentos.
REST = {0x5315C0: 1, 0x535450: 1, 0x52D7D0: 0, 0x6349F0: 0, 0x4D85C0: 1, 0x633A90: 0}
REST_TRAMPOLINES = SENTINEL + 0x60  # uno cada 0x20 bytes, en el orden de REST
EXECUTOR_TRAMPOLINE = SENTINEL + 0x140  # tras los de REST; 0x23 bytes: ver executor_trampoline
# Actualización de barcos (ver sail_oracle.py): saltos por estado y convoyes.
DOCKED_DISPATCH = 0x5067C0
DOCKED_SKIP = 0x5068C6
SAILING_DISPATCH = 0x506C4C
SAILING_SKIP = 0x506CC3
CONVOYS = 0x5070FD
CONVOY_DISPATCH = 0x50719D  # salto por el estado del convoy (ebp: el convoy)
CONVOY_SKIP = 0x507612  # el estado 0x13: pasa al siguiente convoy
ENCOUNTERS = 0x5079DA  # cada 8 ticks, 0x5053C0 (encuentros): se salta
# Unless --no-storms, 0x5053C0 (the storms) runs; its two writes to the sea map
# (*0x6CBDD8 + 0x2B22, UI) are jumped over.
STORM_UI_JUMPS = {0x50596F: 0x505983, 0x5059CA: 0x5059E0}
# With --pirates, the pirate convoys move (their state 0x12, 0x507A65) and the
# per-town waypoints they choose from are computed at the load (0x448780, on
# the navigation object). Until the battles are rewritten, the two places
# where a battle would start are jumped over: the attack on the target
# (0x507BE9 → 0x50BC40) goes on as if there were no target, and the end of the
# route (0x507C0B → a town battle) goes on as if the convoy still sailed.
# The ships in state 0x11 (lost: in port 0x506AF2, at sea 0x507022) and the
# pirates alone at sea (0x12, 0x507099) go through too.
# A convoy at its nest is disbanded (0x514B80, rewritten in p3vita); with
# --no-disband it is not (NO_DISBAND_JUMP, 0x505FF1 → 0x506029 in 0x505C90:
# as if it were elsewhere), as before cloud/battles-2.
PIRATE_JUMPS = {0x507BE9: 0x507B23, 0x507C0B: 0x507B5F}
NO_DISBAND_JUMP = {0x505FF1: 0x506029}
# With --pirates, the pirate objects' daily update runs (0x514000, otherwise
# quiet) and the operation it queues is executed: 0xB5, a convoy leaves the
# nest (0x514D40).
PIRATE_UPDATE = 0x514000
PIRATE_OPERATIONS = (0xB5,)
PIRATE_WAYPOINTS = 0x448780
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
# Con la cola de eventos (--resto 4d85c0), además, interfaz:
QUIET_EVENTS = {
    0x5C6BA0: 0, 0x5C6BF0: 0,  # vuelve a dibujar una zona (*0x6E5558)
    0x494990: 8, 0x4778A0: 8,  # avisos de un barco que cambia de dueño (*0x6CE6D0, *0x6CC950)
    0x5C6740: 0x14,  # interfaz (objeto en ecx, que aquí es 0)
}
# Saltos sobre trozos de interfaz: dirección → adónde se salta.
EVENT_JUMPS = {
    0x4DC326: 0x4DC377,  # 0x4DC1F0: ventana de la interfaz (*0x6CC7E8, 0x469380 y 0x469AF0)
    0x52590C: 0x5259B8,  # 0x5256F0: la ciudad que se ve (*0x6E51AC, [+0xC324]) no es nunca la del edificio
    # 0x629C60 (el ejército sitiador nuevo, operación 0x8E): la ventana al
    # jugador si es su ciudad (*0x6CC7E8, 0x469380 y 0x469AF0) y los gráficos
    # si es la ciudad que se ve (*0x6E51AC, [+0xC324]; 0x4B2200, 0x62C730).
    0x629CEA: 0x629D3B,
    0x62B239: 0x62B261,
    0x4ECD3E: 0x4ECD71,  # 0x4ECB14 (evento 0x38): el monumento, si es la ciudad que se ve (0x590680)
    0x4E0DB9: 0x4E0E13,  # 0x4E09D4 (evento 0x24, fase 1): la ventana (*0x6CC7E8, 0x469380 y 0x469AF0)
}
# Los mensajes llevan textos (CString de MFC, sprintf del CRT) que usan unas
# pocas funciones de KERNEL32: se hacen aquí (ver TownOracle.setup_imports).


def fast_reader(uc):
    """A function (address, size) → bytes that reads the emulator's memory
    like uc.mem_read, but straight into one reused buffer and with a single
    copy: the binding's mem_read allocates and zeroes a buffer per call and
    copies it twice. Errors go through uc.mem_read, so they are the same."""
    try:
        from unicorn.unicorn_py3.unicorn import uclib
        handle, uc_mem_read = uc._uch, uclib.uc_mem_read
    except (ImportError, AttributeError):  # another version of the binding
        return lambda address, size: bytes(uc.mem_read(address, size))
    buffers = [ctypes.create_string_buffer(1 << 16)]
    string_at = ctypes.string_at

    def read(address, size):
        buffer = buffers[0]
        if size > len(buffer):
            buffer = buffers[0] = ctypes.create_string_buffer(size)
        if uc_mem_read(handle, address, buffer, size):
            return bytes(uc.mem_read(address, size))  # raises UcError
        return string_at(buffer, size)
    return read


def read_cpr(path):
    """Índice de un archivo CPR (docs/formats/cpr.md): ruta normalizada → (offset, tamaño)."""
    data = open(path, 'rb').read()
    files = {}
    chunk = 0x20
    while chunk + 0x10 <= len(data):
        index_size, _, count, data_size = struct.unpack_from('<IIII', data, chunk)
        o = chunk + 0x10
        for _ in range(count):
            offset, size, _ = struct.unpack_from('<III', data, o)
            end = data.index(b'\0', o + 12)
            files[normalize(data[o + 12:end].decode('latin-1'))] = (offset, size)
            o = end + 1
        if index_size == 0:
            break
        chunk += index_size + data_size
    return data, files


def normalize(path):
    path = path.replace('\\', '/').lower()
    while path.startswith('./'):
        path = path[2:]
    return path


def trampoline(target, args):
    """push dword [esp+4·args] (args veces); call target; ret 4·args"""
    code = bytes([0xFF, 0x74, 0x24, 4 * args]) * args
    return code, lambda at: code + bytes([0xE8]) + struct.pack('<i', target - (at + len(code) + 5)) + \
        bytes([0xC2, 4 * args, 0x00])


def executor_trampoline(at):
    """push dword [esp+0x18] (6 veces); call 0x535760; add esp, 0x18; ret 0x18.
    El ejecutor no tiene argumentos (no los saca de la pila): los 5 u32 de la
    operación y EXECUTOR_MAGIC quedan como sus «argumentos» para que el
    Recorder los grabe. El juego también llama a 0x535760 desde dentro (el
    evento 0x03, los barcos), con la operación en su pila: esas muestras no
    llevan la marca y tick_probe las salta."""
    code = bytes([0xFF, 0x74, 0x24, 0x18]) * 6
    return code + bytes([0xE8]) + struct.pack('<i', EXECUTOR - (at + len(code) + 5)) + \
        bytes([0x83, 0xC4, 0x18, 0xC2, 0x18, 0x00])


class TownOracle(SaveOracle):
    def __init__(self, exe, save, seed=1, navdata=None, ai=True, convoys=True, merchants=True, rest=(), cpr=None,
                 operations=OPERATIONS, storms=True, pirates=False, disband=True):
        self.pirates = pirates and navdata is not None and convoys
        if self.pirates:
            operations = tuple(operations) + PIRATE_OPERATIONS
        super().__init__(exe, save, navdata)
        self.fast_read = fast_reader(self.uc)
        self.navdata = navdata
        self.operations = set(operations)
        self.cpr = read_cpr(cpr) if cpr else None
        self.ai = ai
        self.merchants = merchants and navdata is not None
        self.rest = set(rest) if navdata is not None else set()
        self.rest_trampolines = {}
        for k, (target, args) in enumerate(REST.items()):
            at = REST_TRAMPOLINES + 0x20 * k
            _, build = trampoline(target, args)
            self.uc.mem_write(at, build(at))
            self.rest_trampolines[target] = at
        self.rand_state = seed
        self.queued = []  # para las muestras
        self.tick_operations = []  # las órdenes que se ejecutan al final del tick
        self.ships = navdata is not None
        quiet = {**QUIET, ENQUEUE: 4}
        if self.ships:
            quiet.update(QUIET_SHIPS)
            if self.pirates:
                del quiet[PIRATE_UPDATE]
            self.uc.hook_add(UC_HOOK_CODE, self._docked, begin=DOCKED_DISPATCH, end=DOCKED_DISPATCH)
            self.uc.hook_add(UC_HOOK_CODE, self._sailing, begin=SAILING_DISPATCH, end=SAILING_DISPATCH)
            if convoys:
                self.uc.hook_add(UC_HOOK_CODE, self._convoy, begin=CONVOY_DISPATCH, end=CONVOY_DISPATCH)
                if self.pirates:
                    for address, target in (PIRATE_JUMPS if disband else {**PIRATE_JUMPS, **NO_DISBAND_JUMP}).items():
                        self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=address, end=address, user_data=target)
                if storms:
                    for address, target in STORM_UI_JUMPS.items():
                        self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=address, end=address, user_data=target)
                else:
                    self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=ENCOUNTERS, end=ENCOUNTERS, user_data=EPILOGUE)
            else:
                self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=CONVOYS, end=CONVOYS, user_data=EPILOGUE)
            for at, target, args in ((SHIPS_TRAMPOLINE, UPDATE_SHIPS, 1), (MERCHANT_TRAMPOLINE, MERCHANT_TICK, 1)):
                _, build = trampoline(target, args)
                self.uc.mem_write(at, build(at))
            self.uc.mem_write(EXECUTOR_TRAMPOLINE, executor_trampoline(EXECUTOR_TRAMPOLINE))
        self.setup_imports(exe)
        if 0x4D85C0 in self.rest:
            quiet.update(QUIET_EVENTS)
            for address, target in EVENT_JUMPS.items():
                self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=address, end=address, user_data=target)
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

    def alloc(self, n):
        # Se apunta lo pedido: los objetivos del ejército sitiador ([+0x250])
        # se graban enteros (el juego sigue leyendo los que ya no cuenta [+0x242]).
        p = super().alloc(n)
        self.__dict__.setdefault('requested', {})[p] = n
        return p

    def _docked(self, uc, address, size, user_data):
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESI
        status = uc.reg_read(UC_X86_REG_ECX)
        if self.pirates and status == 0x11:
            allowed = True  # 0x506AF2, a lost ship (with 0x517170)
        elif self.ai:
            allowed = status in DOCKED_STATES
        else:
            owner = self.u8(uc.reg_read(UC_X86_REG_ESI))
            human = owner < self.counts()[2] and self.u16(self.merchant(owner) + 8) == 0
            allowed = status in (1, 2, 3) or (status == 0 and human)
        if not allowed:
            uc.reg_write(UC_X86_REG_EIP, DOCKED_SKIP)

    def _sailing(self, uc, address, size, user_data):
        from unicorn.x86_const import UC_X86_REG_ESI
        status = self.u16(uc.reg_read(UC_X86_REG_ESI) + 0x134)
        # With --pirates, also 0x11 (a lost ship, 0x507022) and 0x12 (a pirate
        # alone, 0x507099: it becomes a convoy).
        if status != 0x0F and not (self.pirates and status in (0x11, 0x12)):
            uc.reg_write(UC_X86_REG_EIP, SAILING_SKIP)

    def _convoy(self, uc, address, size, user_data):
        # Los convoyes de los piratas (el que los guía no es de un comerciante)
        # se quedan quietos: sus ataques acaban en batallas, que el emulador no
        # puede ejecutar.
        from unicorn.x86_const import UC_X86_REG_EBP
        if not self.pirates and self.u8(uc.reg_read(UC_X86_REG_EBP)) >= self.counts()[2]:
            uc.reg_write(UC_X86_REG_EIP, CONVOY_SKIP)

    def _stub(self, uc, address, size, user_data):
        # With --pirates, 0x448780 runs (save_oracle.py skips it).
        if address == PIRATE_WAYPOINTS and getattr(self, 'pirates', False):
            return
        super()._stub(uc, address, size, user_data)

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
        pos = self.call(0x4D81E0, [FILE + pos, version], ecx=EVENTS) - FILE
        self.uc.hook_del(hook)
        if self.game_file('scripts/Namen_eng.ini') is not None:
            self.load_names(pos, version)
        self.call(MERGE_TOWN_FLAGS, [], ecx=0x6DF278)
        self.call(0x52E1A0, [block[0x10]], ecx=0x6DE3D8)  # dificultad del comercio
        _, _, merchants, _ = self.counts()
        player = max((i for i in range(merchants) if self.u16(self.merchant(i) + 8) == 0), default=0)
        self.write_u32(PLAYER, player)

    def setup_imports(self, exe):
        """Las funciones importadas se llaman por la tabla de importaciones, que
        en el emulador tiene lo que trae el fichero (la dirección relativa del
        nombre de cada función): se reserva esa zona y se pone un gancho en cada
        una. Las que no se hacen aquí paran la emulación con su nombre."""
        data = open(exe, 'rb').read()
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        optional_size = struct.unpack_from('<H', data, pe + 20)[0]
        sections = [struct.unpack_from('<IIII', data, pe + 24 + optional_size + 40 * i + 8)
                    for i in range(struct.unpack_from('<H', data, pe + 6)[0])]

        def offset(rva):
            for vsize, va, rsize, raw in sections:
                if va <= rva < va + max(vsize, rsize):
                    return raw + rva - va

        self.imports = {}
        o = offset(struct.unpack_from('<I', data, pe + 24 + 104)[0])
        while struct.unpack_from('<I', data, o + 12)[0]:
            lookup, _, _, name, thunks = struct.unpack_from('<IIIII', data, o)
            p = offset(name)
            dll = data[p:data.index(b'\0', p)].decode()
            t, k = offset(lookup or thunks), 0
            while struct.unpack_from('<I', data, t + 4 * k)[0]:
                v = struct.unpack_from('<I', data, t + 4 * k)[0]
                if not v & 0x80000000:
                    p = offset(v) + 2
                    iat = struct.unpack_from('<I', data, offset(thunks) + 4 * k)[0]
                    self.imports[iat] = (dll, data[p:data.index(b'\0', p)].decode())
                k += 1
            o += 20
        low, high = min(self.imports) & ~0xFFF, (max(self.imports) + 0x1000) & ~0xFFF
        self.uc.mem_map(low, high - low)
        self.tls = {}
        self.files = {}  # asa → [datos, posición] (Archiver.dll)
        self.files_open = 0
        self.uc.hook_add(UC_HOOK_CODE, self._import, begin=low, end=high - 1)
        # Los constructores estáticos de los repartidores de memoria de los
        # CString (CFixedAlloc de 0x50, 0x90, 0x110 y 0x210 bytes), que el CRT
        # ejecuta al arrancar.
        for f in (0x64F005, 0x64F034, 0x64F066, 0x64F098):
            self.call(f, [])

    def _import(self, uc, address, size, user_data):
        dll, name = self.imports.get(address, ('?', f'{address:#x}'))
        a = self._arg
        if name == 'lstrlenA':
            result, pop = (len(self.cstring(a(0))) if a(0) else 0), 4
        elif name in ('InterlockedIncrement', 'InterlockedDecrement'):
            result = (self.u32(a(0)) + (1 if name == 'InterlockedIncrement' else -1)) & 0xFFFFFFFF
            self.write_u32(a(0), result)
            pop = 4
        elif name in ('EnterCriticalSection', 'LeaveCriticalSection', 'InitializeCriticalSection',
                      'DeleteCriticalSection'):
            result, pop = 0, 4
        elif name == 'GetLastError':
            result, pop = 0, 0
        elif name == 'SetLastError':
            result, pop = 0, 4
        elif name == 'GetCurrentThreadId':
            result, pop = 1, 0
        elif name == 'TlsGetValue':
            result, pop = self.tls.get(a(0), 0), 4
        elif name == 'TlsSetValue':
            self.tls[a(0)] = a(1)
            result, pop = 1, 8
        elif name == 'HeapAlloc':
            result, pop = self.alloc(a(2)), 12
        elif name == 'HeapFree':
            self.release(a(2))
            result, pop = 1, 12
        elif name == 'HeapReAlloc':
            old, size = a(2), a(3)
            result = self.alloc(size)
            self.uc.mem_write(result, bytes(self.uc.mem_read(old, min(size, self.sizes.get(old, 0)))))
            self.release(old)
            pop = 16
        elif name == 'arc_CreateFile':
            # Archiver.dll (cdecl): (contenedor, ruta) → asa o 0; el fichero, de
            # los datos del juego (DATOS o --cpr). Lo usa la carga de la
            # plantilla de una ciudad nueva (0x4F9B40).
            data = self.game_file(self.cstring(a(1)))
            result = 0
            if data is not None:
                self.files_open += 1
                result = 0x7F000000 + self.files_open
                self.files[result] = [data, 0]
            pop = 0
        elif name == 'arc_FileLength':
            result, pop = len(self.files[a(0)][0]) if a(0) in self.files else 0, 0
        elif name == 'arc_Read':
            # (asa, destino, bytes) → leídos.
            f = self.files.get(a(0))
            chunk = f[0][f[1]:f[1] + a(2)] if f else b''
            if chunk:
                self.uc.mem_write(a(1), chunk)
                f[1] += len(chunk)
            result, pop = len(chunk), 0
        elif name == 'arc_ReleaseFile':
            self.files.pop(a(0), None)
            result, pop = 0, 0
        else:
            raise RuntimeError(f'función importada sin hacer: {dll}!{name}')
        self._returns(uc, result, pop)

    def game_file(self, path):
        """Un fichero de los datos del juego: de DATOS (--barcos) o del archivo (--cpr)."""
        if self.navdata:
            local = os.path.join(self.navdata, *normalize(path).split('/'))
            for name in (local, os.path.join(self.navdata, path)):
                if os.path.isfile(name):
                    return open(name, 'rb').read()
        if self.cpr:
            data, files = self.cpr
            entry = files.get(normalize(path))
            if entry:
                return data[entry[0]:entry[0] + entry[1]]
        return None

    def cstring(self, a):
        out = bytearray()
        while True:
            c = self.u8(a + len(out))
            if c == 0:
                return out.decode('latin-1')
            out.append(c)

    def _returns(self, uc, value, pop):
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EAX, value & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + pop)
        uc.reg_write(UC_X86_REG_EIP, self.u32(esp))

    def _archive_read(self, uc, address, size, user_data):
        data = self.game_file(self.cstring(self._arg(1)))
        if data is None:
            return self._returns(uc, 0, 0)
        p = self.alloc(len(data))
        self.uc.mem_write(p, data)
        self.write_u32(self._arg(2), p)
        self.write_u32(self._arg(3), len(data))
        self._returns(uc, p, 0)

    def _archive_free(self, uc, address, size, user_data):
        self.release(self._arg(1))
        self._returns(uc, 0, 0)

    def ini_value(self, path, section, key):
        """El texto de una clave de un .ini de los datos, o None."""
        data = self.game_file(path)
        current = None
        for line in (data or b'').decode('latin-1').splitlines():
            line = line.strip()
            if line.startswith('['):
                current = line[1:line.find(']')].lower()
            elif current == section.lower() and '=' in line and line.split('=', 1)[0].strip().lower() == key.lower():
                return line.split('=', 1)[1].strip()
        return None

    def _ini_int(self, uc, address, size, user_data):
        # La clave de la sección leída con sscanf("%i"), o el valor por omisión.
        text = self.ini_value(self.cstring(self._arg(3)), self.cstring(self._arg(0)), self.cstring(self._arg(1)))
        value = self._arg(2)
        if text is not None:
            t = text.lstrip()
            sign = -1 if t[:1] == '-' else 1
            t = t[1:] if t[:1] in '+-' else t
            base, digits = 10, '0123456789'
            if t[:2].lower() == '0x' and t[2:3].lower() in '0123456789abcdef' and t[2:3]:
                base, digits, t = 16, '0123456789abcdef', t[2:]
            elif t[:1] == '0':
                base, digits = 8, '01234567'
            n, k = 0, 0
            while k < len(t) and t[k].lower() in digits:
                n = n * base + int(t[k], 16)
                k += 1
            if k:
                value = sign * n
        self._returns(uc, value, 0x10)

    def _ini_string(self, uc, address, size, user_data):
        # La clave, o el texto por omisión, en el búfer; devuelve su longitud.
        text = self.ini_value(self.cstring(self._arg(5)), self.cstring(self._arg(0)), self.cstring(self._arg(1)))
        value = (text.encode('latin-1') if text is not None else self.cstring(self._arg(2)).encode('latin-1'))
        value = value[:max(self._arg(4) - 1, 0)]
        self.uc.mem_write(self._arg(3), value + b'\0')
        self._returns(uc, len(value), 0x18)

    def load_names(self, events_end, version):
        """Crea el objeto de los nombres como el juego: 0x5121C0 al arrancar
        (ficheros de scripts/), los nombres de la partida (0x511E30) y otra vez
        0x5121C0, que la carga llama al final (0x54A3E2) y deshace los nombres
        de barco de la partida.

        Los nombres son la última sección del fichero (tras ellos, un u32 y el
        separador final). Entre la cola de eventos y ellos va la de 0x611AE0
        (objeto 0x6E55D0), que crea objetos con vtable y aquí no se lee: la
        sección se busca hacia atrás desde el final, como en savegame.cpp."""
        if struct.unpack_from('<I', self.save, events_end)[0] != SEPARATOR:
            raise RuntimeError(f'la cola de eventos acaba en {events_end}, sin separador')
        pos = len(self.save) - 8
        while pos > events_end and struct.unpack_from('<I', self.save, pos)[0] != SEPARATOR:
            pos -= 1
        if pos <= events_end or struct.unpack_from('<I', self.save, len(self.save) - 4)[0] != SEPARATOR:
            raise RuntimeError('no se encuentra la sección de los nombres')
        hooks = [self.uc.hook_add(UC_HOOK_CODE, f, begin=a, end=a) for a, f in (
            (ARCHIVE_READ, self._archive_read), (ARCHIVE_FREE, self._archive_free), (INI_INT, self._ini_int),
            (INI_STRING, self._ini_string))]
        # La primera vez, 0x5121C0 lee también los nombres de las ciudades y la
        # tabla de las ciudades (0x51E440, scripts/StadtDaten.ini); su CString
        # lo inicializa el constructor estático de la tabla.
        for i in range(40):
            self.write_u32(TOWN_TABLE + i * TOWN_TABLE_SIZE + 0x28, self.u32(EMPTY_CSTRING))
        self.call(LOAD_NAMES, [], ecx=NAMES)
        # 0x5346A0 (constructor del objeto 0x6DF278) lee scripts/Expeditionen.dat
        # si aún no lo ha hecho: u32 [0x6DF2E4] puntos (u16 x, u16 y) en *0x6DF2E0.
        expeditions = self.game_file('scripts/Expeditionen.dat')
        if expeditions is not None:
            table = self.alloc(len(expeditions))
            self.uc.mem_write(table, expeditions)
            self.write_u32(EXPEDITIONS, table)
            self.write_u32(EXPEDITIONS + 4, (len(expeditions) >> 2) & 0xFFFF)
        end = self.call(READ_SAVE_NAMES, [FILE + pos + 4, version], ecx=NAMES) - FILE
        if end + 4 != len(self.save):
            raise RuntimeError(f'los nombres acaban en {end} y el fichero en {len(self.save)}')
        self.call(LOAD_NAMES, [], ecx=NAMES)
        for h in hooks:
            self.uc.hook_del(h)

    def town_tick(self, i):
        self.call(TRAMPOLINE, [self.u16(WORLD + 4)], ecx=self.town(i))

    def advance_tick(self):
        """Un tick de advance_time (0x530E80) con la fecha y las ciudades."""
        tick = (self.u32(WORLD + 0x14) + 1) & 0xFFFFFFFF
        self.write_u32(WORLD + 0x14, tick)
        t = tick & 0xFF
        if t == 0:
            self.call(UPDATE_DATE, [], ecx=WORLD)
            self.run_rest(0x5315C0, [1 if tick % 0x700 == 0 else 0], WORLD)
            self.run_rest(0x535450, [tick], WORLD + 0xE8)
            if 0x5315C0 in self.rest and self.u8(WORLD + 0x8B) > 0:
                self.uc.mem_write(WORLD + 0x8B, bytes([self.u8(WORLD + 0x8B) - 1]))
        town = t >> 3 if t & 7 == 7 else (t + 0xFF) >> 3 if t & 7 == 3 else None
        if town is not None and town < self.u32(WORLD + 0x10):
            self.town_tick(town)
            if t & 7 == 7 and self.u32(SIEGES):
                self.run_rest(0x6349F0, [], self.u32(SIEGES) + town * 0x1024)
        if t & 7 == 5 and t >> 3 < 15:
            self.run_rest(0x52D7D0, [], WORLD + 0xB10 + 0x2C * (t >> 3))
        merchant = t >> 3 if t & 7 == 1 else (t + 0x100) >> 3 if t & 7 == 6 else None
        if self.merchants and merchant is not None and merchant < self.u16(WORLD + 0xA):
            self.call(MERCHANT_TRAMPOLINE, [merchant], ecx=self.merchant(merchant))
        if self.ships:
            self.call(SHIPS_TRAMPOLINE, [tick], ecx=SHIPS)
            self.run_rest(0x4D85C0, [tick], EVENTS)
            if self.u32(SIEGES):
                for i in range(self.u32(WORLD + 0x10)):
                    self.run_rest(0x633A90, [], self.u32(SIEGES) + i * 0x1024)
            # Las operaciones encoladas en este tick, al final y en su orden,
            # con el ejecutor del juego: las de self.operations (las demás,
            # como las obras de las ciudades, se descartan).
            operations, self.tick_operations = self.tick_operations, []
            for op in operations if self.ai else []:
                if struct.unpack_from('<I', op, 0)[0] in self.operations:
                    buffer = self.alloc(OPERATION_SIZE)
                    self.uc.mem_write(buffer, op)
                    self.call(EXECUTOR_TRAMPOLINE, list(struct.unpack('<5I', op)) + [EXECUTOR_MAGIC], ecx=buffer)
            self.tick_operations = []
        return tick

    def run_rest(self, target, args, ecx):
        if target in self.rest:
            self.call(self.rest_trampolines[target], args, ecx=ecx)

    # Estado por regiones.
    def regions(self):
        """{nombre: (dirección, bytes)}

        This is the slowest part of recording (two snapshots per recorded
        call), so each block is read from the emulator once: the pointers and
        counts come from the blocks already copied (G holds all the globals,
        T the towns, P the ships, X the sieges, J the armies), and each list
        node is read in one piece (its data and the next pointer). The result
        is the same, byte for byte, as reading every field from memory.
        """
        read = self.fast_read
        unpack = struct.unpack_from
        g0 = GLOBALS[0]
        g = bytes(read(g0, GLOBALS[1] - g0))

        def gu32(a):
            return unpack('<I', g, a - g0)[0]

        def gu16(a):
            return unpack('<H', g, a - g0)[0]

        def block(address, size):
            return read(address, size) if size else b''

        towns, offices, merchants, ships = gu32(WORLD + 0x10), gu16(WORLD + 8), gu16(WORLD + 0xA), gu16(SHIPS + 0xF4)
        out = {'G': (g0, g)}
        settings = unpack('<I', read(SETTINGS_POINTER, 4))[0]
        out['A'] = (settings, block(settings, SETTINGS_SIZE))
        first_town = gu32(WORLD + 0x68)
        town_block = block(first_town, towns * 0x9F8)
        out['T'] = (first_town, town_block)
        first_office = gu32(WORLD + 0x74)
        out['O'] = (first_office, block(first_office, offices * 0x44C))
        first_merchant = gu32(WORLD + 0x78)
        out['M'] = (first_merchant, block(first_merchant, merchants * 0x650))
        first_ship = gu32(SHIPS + 4)
        ship_block = block(first_ship, ships * 0x180)
        out['P'] = (first_ship, ship_block)
        captains = gu32(SHIPS)
        out['C'] = (captains, block(captains, gu16(SHIPS + 0xF2) * 16))
        records = gu32(WORLD + 0x70)
        out['W'] = (records, block(records, gu16(WORLD + 6) * 0x14))
        sieges = gu32(SIEGES)
        if sieges:
            siege_block = block(sieges, towns * 0x1024)
            out['X'] = (sieges, siege_block)
            requested = self.__dict__.get('requested', {})
            for i in range(towns):
                siege = i * 0x1024
                army = unpack('<I', siege_block, siege + 8)[0]
                army_block = block(army, 0x650 if army else 0)
                out[f'J{i}'] = (army, army_block)
                targets = unpack('<I', army_block, 0x250)[0] if army else 0
                if targets:
                    size = requested.get(targets)
                    if size is None:
                        size = 12 * unpack('<H', army_block, 0x242)[0]
                    out[f'U{i}'] = (targets, block(targets, size // 12 * 12))
                else:
                    out[f'U{i}'] = (targets, b'')
                for name, head, data in (('L', 0x101C, 8), ('H', 0x1020, 0x10)):
                    nodes = []
                    first = node = unpack('<I', siege_block, siege + head)[0]
                    while node:  # los datos y, detrás, el siguiente
                        piece = read(node, data + 4)
                        nodes.append(piece[:data])
                        node = unpack('<I', piece, data)[0]
                    out[f'{name}{i}'] = (first, b''.join(nodes))
        events = gu32(EVENTS)
        out['E'] = (events, block(events, gu16(EVENTS + 0xC) * 0x18 if events else 0))
        mail = gu32(MAIL)
        out['B'] = (mail, block(mail, gu16(MAIL + 6) * 0x10 if mail else 0))
        # The 16-byte heap record of the letters of types 0x3C..0x40 and 0x71
        # (pointer in [+0x8]): u16 letter and the 16 bytes, for each.
        letter_records = []
        letters = out['B'][1]
        for i in range(len(letters) // 0x10):
            kind, pointer = letters[0x10 * i + 4], unpack('<I', letters, 0x10 * i + 8)[0]
            if pointer and (0x3C <= kind <= 0x40 or kind == 0x71):
                letter_records.append(struct.pack('<H', i))
                letter_records.append(bytes(read(pointer, 0x10)))
        out['L'] = (0, b''.join(letter_records))
        times = gu32(WORLD + 0x6C)
        out['Y'] = (times, block(times, towns * towns if times else 0))
        convoys = gu32(SHIPS + 8)
        out['V'] = (convoys, block(convoys, gu16(SHIPS + 0xF6) * 0x3C if convoys else 0))
        routes = []
        for i in range(ships):
            route = unpack('<I', ship_block, i * 0x180 + 0x24)[0]
            if route:
                points, count = unpack('<II', read(route, 8))
                routes.append(struct.pack('<HI', i, count))
                routes.append(block(points, 4 * count))
        out['Q'] = (0, b''.join(routes))
        for i in range(8):
            p = gu32(PLAYER_TOWNS + 4 * i)
            out[f'K{i}'] = (p, block(p, 40 if p else 0))
        for i in range(5):
            p = gu32(SHIPS + 0xC + 4 * i)
            pirate = block(p, 0x18 if p else 0)
            out[f'Z{i}'] = (p, pirate)
            # What the pirate points to: the map of its nest ([+0x0], 0x6C
            # bytes, and its cells, [+0x68]) and the pool ([+0x4], [+0x8]
            # entries of 8 bytes).
            nest_map, pool = unpack('<II', pirate, 0) if p else (0, 0)
            map_block = block(nest_map, 0x6C if nest_map else 0)
            out[f'ZM{i}'] = (nest_map, map_block)
            cells = unpack('<I', map_block, 0x68)[0] if nest_map else 0
            width, height = unpack('<ii', map_block, 4) if cells else (0, 0)
            out[f'ZD{i}'] = (cells, block(cells, width * height if cells and width > 0 and height > 0 else 0))
            out[f'ZP{i}'] = (pool, block(pool, 8 * unpack('<H', pirate, 8)[0] if pool else 0))
        for i in range(15):
            head = gu32(WORLD + 0xB10 + 0x2C * i + 0x24)
            nodes = []
            node = unpack('<I', read(head, 4))[0] if head else head
            while node != head:  # nodos de 0x18 bytes: siguiente, anterior y los datos
                piece = read(node, 0x18)
                nodes.append(piece[8:])
                node = unpack('<I', piece, 0)[0]
            out[f'F{i}'] = (head, b''.join(nodes))
        for i in range(towns):
            t = i * 0x9F8
            p = unpack('<I', town_block, t + 0x75C)[0]
            out[f'R{i}'] = (p, block(p, unpack('<H', town_block, t + 0x776)[0] * 8))
            p = unpack('<I', town_block, t + 0x80C)[0]
            width, height = unpack('<II', town_block, t + 0x7A8)
            out[f'D{i}'] = (p, block(p, width * height))
            nodes = []
            first = node = unpack('<I', town_block, t + 0x9B4)[0]
            while node:  # 0x14-byte nodes: the next one and the data
                piece = read(node, 0x14)
                nodes.append(struct.pack('<I', node))
                nodes.append(piece[4:])
                node = unpack('<I', piece, 0)[0]
            out[f'N{i}'] = (first, b''.join(nodes))
        out['r'] = (0, struct.pack('<I', self.rand_state))
        return out


def write_regions(f, regions):
    f.write(struct.pack('<I', len(regions)))
    for name, (address, data) in regions.items():
        f.write(bytes([len(name)]) + name.encode() + struct.pack('<II', address, len(data)) + data)


DIFF_CHUNK = 64
DIFF_BLOCK = 4096  # a multiple of DIFF_CHUNK


def changed_runs(prev, data):
    """The runs of consecutive 64-byte chunks that differ, as (start, end).

    Equal 4 KB blocks are skipped with one comparison; only the blocks that
    differ are compared chunk by chunk. The runs are the same as comparing
    every chunk: a run that reaches the end of a block goes on into the next
    one if its first chunk differs too.
    """
    runs = []
    start = None
    n = len(data)
    for b in range(0, n, DIFF_BLOCK):
        if prev[b:b + DIFF_BLOCK] == data[b:b + DIFF_BLOCK]:
            if start is not None:
                runs.append((start, b))
                start = None
            continue
        for i in range(b, min(b + DIFF_BLOCK, n), DIFF_CHUNK):
            if prev[i:i + DIFF_CHUNK] == data[i:i + DIFF_CHUNK]:
                if start is not None:
                    runs.append((start, i))
                    start = None
            elif start is None:
                start = i
    if start is not None:
        runs.append((start, n))
    return runs


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
        for start, end in changed_runs(prev, data):
            chunks.append((name, address, start, data[start:end]))
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
        self.check = bool(os.environ.get('P3_ORACLE_CHECK'))
        self.skipped = 0
        self._count_changes(oracle.uc)
        self.unchanged = None
        self.state = oracle.regions()
        write_regions(out, self.state)
        for f in functions:
            oracle.uc.hook_add(UC_HOOK_CODE, self._enter, begin=f, end=f)
            # Unicorn no aplica un gancho nuevo al código que ya ha traducido.
            oracle.uc.ctl_remove_cache(f, f + 1)

    # Taking the snapshot before each call is half the cost of recording. It
    # is skipped when nothing outside the stack can have changed since the
    # last snapshot (self.state):
    # - that one was taken when a function returned to code that only returns
    #   (ret, or add esp and ret: the end of the oracle's trampolines) and
    #   returns to SENTINEL, so that emulator run ended without running
    #   anything else;
    # - since then there has been only one emulator run, the current one; its
    #   code up to the call to this function is only push dword [esp+n] (a
    #   trampoline), and this is the first recorded function entered in it;
    # - Python has written nothing outside the stack, and rand() hasn't moved.
    # That is what happens between consecutive calls from Python, like
    # 0x633A90 for each town (6144 of the 6770 calls of a day with world.sh's
    # FUNCS). No region is on the stack. With P3_ORACLE_CHECK=1 the snapshot
    # is taken anyway and compared.
    def _count_changes(self, uc):
        """Counts the emulator runs and the writes from Python outside the stack."""
        self.changes = changes = [0, 0]
        self.run_begin = None  # where the current emulator run started
        self.entered_run = None  # the last emulator run in which a recorded function was entered
        self.returns_at_once = {}  # return address → the code there (4 bytes)
        self.pushes_then_call = {}  # (run start, return address, function) → only pushes and the call
        emu_start, mem_write = uc.emu_start, uc.mem_write

        def counted_emu_start(*args, **kwargs):
            changes[0] += 1
            self.run_begin = args[0] if args else kwargs.get('begin')
            return emu_start(*args, **kwargs)

        def counted_mem_write(address, data):
            if not STACK <= address < STACK + STACK_SIZE:
                changes[1] += 1
            return mem_write(address, data)

        uc.emu_start = counted_emu_start
        uc.mem_write = counted_mem_write

    def _run_ends(self, ret, esp):
        """True if the code at ret only returns (ret [n], or add esp, n and
        ret [n]) and returns to SENTINEL, where the emulator run ends."""
        code = self.returns_at_once.get(ret)
        if code is None:
            code = self.returns_at_once[ret] = bytes(self.o.uc.mem_read(ret, 4))
        if code[:2] == b'\x83\xC4' and code[3] in (0xC2, 0xC3):
            esp += code[2] - (0x100 if code[2] & 0x80 else 0)
        elif code[0] not in (0xC2, 0xC3):
            return False
        return self.o.u32(esp) == SENTINEL

    def _only_pushes_before(self, ret, function):
        """True if the current emulator run started with push dword [esp+n]
        instructions and then the call to function that returns to ret."""
        key = (self.run_begin, ret, function)
        known = self.pushes_then_call.get(key)
        if known is None:
            begin = self.run_begin
            known = begin is not None and 5 <= ret - begin <= 0x100 and (ret - begin - 5) % 4 == 0
            if known:
                code = bytes(self.o.uc.mem_read(begin, ret - begin))
                known = (all(code[i:i + 3] == b'\xFF\x74\x24' for i in range(0, len(code) - 5, 4))
                         and code[-5] == 0xE8 and (ret + struct.unpack_from('<i', code, len(code) - 4)[0]) & 0xFFFFFFFF
                         == function)
            self.pushes_then_call[key] = known
        return known

    def _enter(self, uc, address, size, user_data):
        if not self.active or self.count >= self.limit:
            return
        o = self.o
        esp = uc.reg_read(UC_X86_REG_ESP)
        stack = bytes(uc.mem_read(esp, 36))  # the return address and 8 arguments
        ret = struct.unpack_from('<I', stack)[0]
        header = b'S' + struct.pack('<IIII', address, o.u32(WORLD + 0x14), uc.reg_read(UC_X86_REG_ECX), esp) + stack[4:]
        first_in_run = self.entered_run != self.changes[0]
        self.entered_run = self.changes[0]
        if (first_in_run and self.unchanged is not None
                and self.unchanged == (self.changes[0] - 1, self.changes[1], o.rand_state)
                and self._only_pushes_before(ret, address)):
            before = self.state
            self.skipped += 1
            if self.check and o.regions() != before:
                raise RuntimeError(f'skipped snapshot differs from memory at the call to {address:#x}')
        else:
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
            # See _count_changes.
            ends = self._run_ends(ret, uc2.reg_read(UC_X86_REG_ESP))
            self.unchanged = (self.changes[0], self.changes[1], o.rand_state) if ends else None
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
    p.add_argument('--no-storms', action='store_true',
                   help='with --barcos, without the storms (0x5053C0, every 8 ticks), which run by default')
    p.add_argument('--storms', action='store_true', help='on by default now; kept for older scripts')
    p.add_argument('--pirates', action='store_true',
                   help='with --barcos, the pirate convoys move (without battles yet; see PIRATE_JUMPS)')
    p.add_argument('--no-disband', action='store_true',
                   help='with --pirates, a convoy at its nest is not disbanded (0x514B80; NO_DISBAND_JUMP)')
    p.add_argument('--resto', help='con --barcos, solo estas piezas del resto de advance_time (5315c0,535450,...); '
                   'por omisión, todas')
    p.add_argument('--sin-resto', action='store_true', help='con --barcos, sin el resto de advance_time')
    p.add_argument('--cpr', help='archivo de datos (p2arch0_eng.cpr) del que leer scripts/Namen*.txt')
    p.add_argument('--write', metavar='OUT', help='at the end, the uncompressed .pat the game writes for the state '
                   'reached (write_oracle.py)')
    p.add_argument('--operaciones', default=','.join(f'{x:x}' for x in OPERATIONS),
                   help='con --barcos, códigos (hex) de las operaciones encoladas que se ejecutan')
    a = p.parse_args()
    if a.sin_resto:
        rest = []
    elif a.resto is None:
        rest = list(REST)
    else:
        rest = [int(x, 16) for x in a.resto.split(',') if x]
    o = TownOracle(a.exe, open(a.save, 'rb').read(), a.semilla, a.barcos, not a.sin_ia, not a.sin_convoyes,
                   not a.sin_comerciantes, rest, a.cpr, [int(x, 16) for x in a.operaciones.split(',') if x], not a.no_storms,
                   a.pirates, not a.no_disband)
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
    if a.write:
        # What the load does and the oracle leaves out (the 12 bytes after the
        # settings, the battles), only now, so that the days run as without
        # --write; then the game's save code.
        from write_oracle import prepare_game_object, write_files
        prepare_game_object(o)
        write_files(o, a.write)
        print(f'save written in {a.write}')


if __name__ == '__main__':
    main()
