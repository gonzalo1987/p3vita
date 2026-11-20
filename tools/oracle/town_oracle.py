"""Oráculo de las ciudades: carga una partida con el código de Patrician3.exe en
Unicorn (ver save_oracle.py), hace pasar los días con el tick de ciudad del
propio juego (0x51BA10) y graba «sondas»: cada vez que se entra en una
función elegida, el estado de antes y el de después. tools/oracle/tick_probe.cpp
repite cada llamada con src/p3data y compara (ver docs/oracle.md).

El estado son copias de la memoria del juego, por regiones:
  G      globales de 0x6DD000 a 0x6E0000 (mundo 0x6DE4A0, barcos 0x6DD7A0...)
  A      ajustes de la partida (*0x6CC3E8, 0x4C bytes)
  T O M  ciudades (0x9F8), oficinas (0x44C) y comerciantes (0x650)
  ML     the merchants' ship lists that aren't empty (u16 arrays at [+0x624 + 4·k]):
         u16 merchant, u8 list, u16 length and its u16s (empty in the saves)
  SN     the ship names the players wrote (the names object 0x6DDAA0's second
         list: its buffer [+0xB8] up to the offset [+0xCC][n] of its n names)
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
  L      the 16-byte heap record of each letter of types 0x3C..0x40 and 0x71
         ([+0x8]): u16 letter and the 16 bytes
  LB     the heap buffer of each letter: types 0x29, 0x47, 0x63 and 0x83
         ([+0x8], [+0xC] bytes) and 0x3C..0x40 and 0x71 ([+0xC], record
         [+0x0] bytes): u16 letter, u32 size and the bytes
  TK     the ticker lines sent to the local player since the start (0x548CA0,
         silenced, with the merchant = *0x6DFC14 and a kind that has a line):
         u8 kind and u16 argument each (0 for a kind of a fixed text)
  Y      tiempos de viaje entre ciudades ([+0x6C] del mundo, n × n bytes)
  V      convoyes (*0x6DD7A8, tantos registros de 0x3C como [0x6DD896])
  Q      rutas de los barcos: por cada barco con [+0x24], u16 barco, u32 nº de
         puntos y los puntos (i16 x, i16 y)
  K0..K7 los 40 bytes de cada hueco del objeto 0x6DE588 (*[0x6DEF90 + 4·i]; vacío si no hay)
  Z0..Z4 los 0x18 bytes de los 5 piratas del objeto de los barcos (*[0x6DD7AC + 4·i]; vacío si no hay)
  BA     the address of each block of BT in the emulator (for the probes)
  BT     the battles: the object of 0x6E55D0 and every heap block that hangs from
         it, pointers inside turned into BATTLE_POINTER | block << 16 | offset
         (see TownOracle.battle_graph)
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
       [--sin-comerciantes] [--resto PIEZAS | --sin-resto] [--operaciones CÓDIGOS] [--no-storms]
       [--no-pirates] [--no-disband] [--no-statistics]] [--pst FILE] [--write OUT]
       [--letters connected|all|none]
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
(0x54DE90, the daily statistics, after 5315c0 unless --no-statistics; always recorded.) El juego ejecuta
la cola entre dos llamadas a advance_time, que hace hasta 8 ticks según el
tiempo real; aquí y en p3vita, tras cada tick. La cola de eventos necesita lo
de scripts/ (--cpr). Para repetir el mundo con tick_probe --continuo hay que
grabar también 4f7bb0 y las piezas que se ejecuten:
51ba10,4f7bb0,506720,535760,5315c0,535450,52d7d0,6349f0,633a90,4d85c0
(535760, el ejecutor, o 537670, solo las órdenes de zarpar, pero no las dos).
With --write OUT, after the days it runs the game's save code on the state
reached and writes the uncompressed .pat (write_oracle.py), to compare with
tick_probe --write; and the .pst (0x54D870) to OUT.pst. With --pst FILE, an
uncompressed .pst is read after the save (0x54DA60).
Con --sin-ia, los barcos como en sail_oracle.py: en puerto solo los estados
1, 2, 3 y el 0 de los jugadores humanos, y las operaciones encoladas no se
ejecutan.
"""
import argparse
import atexit
import ctypes
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from save_oracle import (EXPEDITIONS, FILE, SENTINEL, SEPARATOR, SHIPS, STACK, STACK_SIZE, TOWN_TABLE, WORLD,  # noqa: E402
                         SaveOracle)
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP  # noqa: E402

TOWN_TICK = 0x51BA10
MERCHANT_TICK = 0x4F7BB0  # thiscall sobre el comerciante, con su índice
UPDATE_SHIPS = 0x506720
SET_SAIL = 0x537670  # operación 0x00
EXECUTOR = 0x535760  # ejecuta una operación encolada (thiscall sobre ella)
EXECUTOR_MAGIC = 0x504F3350  # «P3OP»: tras la operación en la pila, la marca de las llamadas del oráculo
OPERATIONS = (0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x12, 0x13, 0x1E, 0x1F, 0x22, 0x23, 0x24, 0x27, 0x28, 0x29, 0x2C, 0x37, 0x38, 0x3B, 0x3F, 0x46, 0x48, 0x4B, 0x60, 0x61, 0x64,
              0x65, 0x67, 0x6E, 0x6F, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x83, 0x84, 0x85, 0x86,
              0x87, 0x8E, 0xB8, 0x58, 0x59, 0xC0,
              0x0C, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x20, 0x2D, 0x2E, 0x2F,
              0x2A, 0x2B, 0x25, 0x26, 0x70, 0x71, 0x0D,
              0x9F,
              0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D,
              0x82)  # las que se ejecutan por omisión (0x9F: queued by the chase that catches, 0x50B250; 0x68..0x6D: the trade routes; 0x82: a pirate for a mission, queued by the interpreter's 0xF9, 0xE9, 0xE7)
UPDATE_DATE = 0x5310D0
RAND = 0x639C03
MERGE_TOWN_FLAGS = 0x5348B0  # la carga lo llama con 0x6DF278 tras leer el mundo
SETTINGS_POINTER = 0x6CC3E8
SETTINGS_SIZE = 0x4C
PLAYER = 0x6DFC14
SIEGES = 0x6DF270
EVENTS = 0x6DD73C  # cola de eventos: puntero al array, cabeza u16 en +8, número u16 en +0xC
MAIL = 0x6DD730  # buzón de los avisos: puntero a las entradas de 16 bytes, número u16 en +6
TRADE_ROUTES = 0x6DD728  # the trade routes' pool: u16 free head, u16 count, the array of stops
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
LOADED_ROUTE = 0x6DFC20  # the queue object 0x6DF2F0's [+0x930]: a route loaded from a file (0x5492D0)
OFFERS = 0x6DE584  # the offers between merchants: a list of 0x14-byte nodes, the next at [+0x0] (SaveGame::offers)
MISSIONS = 0x6DD8B4  # the missions' list (0x511000): [+0x4] its entries {u32 program, u16 number, u16 count}, [+0x8] u16 how many
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
# The daily statistics (0x54DE90 on 0x6DFC90; unless --no-statistics): after 0x5315C0, as
# advance_time does. Its lists are the regions S0..S4 (see regions()).
STATISTICS = 0x54DE90
STATISTICS_OBJECT = 0x6DFC90
STATISTICS_TRAMPOLINE = SENTINEL + 0x170
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
# the navigation object). Their attack on a target (0x507BE9 → 0x50BC40, a
# sea battle) runs, with the battles' clock (BATTLE_STEPS; the sea battles are
# rewritten in p3vita, docs/cloud/battles-2.md §4). The end of the route
# (0x507C0B → 0x507C4C, a town battle) runs since the battles' piece 6, part
# 3 (2026-11-07); before, it was jumped over (the convoy went on as if it
# still sailed).
# The ships in state 0x11 (lost: in port 0x506AF2, at sea 0x507022) and the
# pirates alone at sea (0x12, 0x507099) go through too.
# A convoy at its nest is disbanded (0x514B80, rewritten in p3vita); with
# --no-disband it is not (NO_DISBAND_JUMP, 0x505FF1 → 0x506029 in 0x505C90:
# as if it were elsewhere), as before cloud/battles-2.
# At a sea battle's end (0x60D299) the news ticker's window (*0x6CC7E8,
# 0x469380 and 0x469AF0) is jumped over (0x60EB55 → 0x60EBAC); the tick in
# the battles object's [+0x408] is kept.
PIRATE_JUMPS = {0x60EB55: 0x60EBAC}
# 0x506720's local [esp+0x18] is never initialized: a raider's attack on a
# town (0x506F41) passes it to 0x5062F0 as the head of a list, so the new
# convoy's next [+0x8] is whatever the stack held (then the local keeps the
# battle's slot for the next attack in the same call). At the function's
# entry the oracle sets it (entry esp − 0xC) to 0xFFFF, as p3vita
# (Sailing::attack_head_).
UPDATE_SHIPS_ENTRY = 0x506720
ATTACK_HEAD = 0xFFFF
# The same in the event 0x32's handler 0x4EAFE4: the attack on a nest of a
# ship alone (0x4EBB4A, the Hanse task of type 3) passes its local [esp+0x14]
# to 0x5062F0 as a list head; only the convoy's path writes it (an earlier
# ship's convoy that wasn't at sea). At the function's entry the oracle sets
# it (entry esp − 0x40) to 0xFFFF, as p3vita (Sailing::nest_attack).
NEST_TASK_ENTRY = 0x4EAFE4
NEST_ATTACK_HEAD_AT = 0x40
# The news ticker's window (*0x6CC7E8, 0x469380) opens at once only when no
# sea battle is open ([0x6E59CC] = 0xFF and [0x6E59CD] = 0, which the battles'
# reader 0x611AE0 sets at the load); otherwise the message waits ([+0x3EA] =
# 1). The emulator has no window, so the message always waits (0x46938F →
# 0x4693A1), as before the oracle read the battles: without it, the player's
# expired road task (retouch_unchecked.py road_expired) jumped to 0 in
# 0x4B4E30. Always on, as the window can open from any part.
WINDOW_JUMPS = {0x46938F: 0x4693A1}
# Two of the CRT's static initializers fill tables of text pointers at start
# (0x6A0BC8.., among them the monument letter's adjectives 0x6A0C18, and
# 0x6A1778.., the battles' «Your snaikka»...): only constant stores, so the
# oracle runs them (GameTexts reads them as well, static_slots).
TEXT_INITIALIZERS = (0x46BD80, 0x46BFC0)
# The battle's question to the player («Steer manually»; in Spanish «¿Quieres
# dirigir manualmente a los barcos en la batalla naval?», 0x41D9B0 on
# *0x6CBB10), which the emulator doesn't have, as p3vita (part 7 of the
# battles). With --pirates: at a battle's setup (0x605420), a human side of
# the player always takes the path of a player busy with another battle
# window (0x60659B -> 0x606BAE: operation 0x96 with 1, so the computer fights
# it); a step never opens the window of a battle that asks for it ([+0x12E9]
# 1, from a save: 0x606E7E -> 0x606EA5, 0x606FEA -> 0x607011), nor does a
# boarding with a human side of the player (0x60CA4E -> 0x60CA6F), skipping
# the window's writes too.
BATTLE_WINDOW_JUMPS = {0x60659B: 0x606BAE, 0x606E7E: 0x606EA5, 0x606FEA: 0x607011, 0x60CA4E: 0x60CA6F,
                       # a town battle's setup 0x5FBFE0 (piece 6): the same busy path (0x96 with 1)
                       0x5FD0E7: 0x5FD483}
# The town battle's end window for a human side of the local player (0x5FE7E0: 0x5FF5B6, past the side's
# checks; the text written into [+0x1310] at 0x5FF770, then the window's description [+0x12EC], 0x41D9B0 on
# *0x6CBB10, [+0x12E9], [+0x12EA] and [0x6E59CD]). p3vita composes the text for the viewer only
# (Sailing::town_end_message) and writes none of it until piece 7, so with --pirates the oracle prints the
# game's text («town battle end window: ...», to compare with tick_probe --messages), puts [+0x1310] back as it
# was at 0x5FF5B6 and goes on at 0x5FF810, the next side (a battle over at its setup reaches it with the
# player's raider at an undefended town).
TOWN_END_WINDOW = (0x5FF5B6, 0x5FF770, 0x5FF810)
# The sea map's battle icon (the battles' piece 7; the user's choice, 2026-11-07: the game's memory as on the PC):
# when a ship of the player is in a sea battle ([+0x1E92]), the game tells its sea map (*0x6CBDD8, which it always
# has: 0x457C90 with the battle's slot [+0x662], x [+0xAD6], y [+0xADA]) and clears the flag, at the setup
# (0x605910) and in each step (0x606E37). The emulator has no sea map: with --pirates the oracle records the icon
# (sea_map_icons) and goes on at the clear, as p3vita (Sailing::tell_sea_map).
SEA_MAP_ICON = {0x605910: 0x605939, 0x606E37: 0x606E60}
# The battles' piece 7: the battles' window (*0x6CBB10, the game's dialog: a battle's question, «Steer manually», and
# its end). The scenarios of the window's path (TownOracle.use_battle_window) give the emulator a stand-in: an object
# whose vtable stops the emulator but for its methods +0xC8 (whether open: [+0x48], as the game's 0x4B2200) and
# +0x118 (closed with the result 0, as 0x41E670), with its result at [+0x5A4]; 0x41D9B0 opens it (an open one closed
# first; the description copied to [+0x5B8]) and the scenario closes it (close_battle_window: 10, [0x66BDC0], its
# button; 0 otherwise). Then the battles' BATTLE_WINDOW_JUMPS (the sea's and the town battle's question) and
# TOWN_END_WINDOW are off and the interface of the human side's path is jumped over (HUMAN_UI_JUMPS: 0x608230's and
# 0x600660's town view, sea map and clouds; [0x6E59CC] is kept). The sound (0x443690
# on *0x6CBDB8, which the emulator doesn't have: the variants of a sound, played only with the battle on the screen)
# answers SOUND_VARIANTS (0: the sound off, no rand(); a scenario may switch it on). Region WN: u8 open, u32 result,
# u32 the sounds' variants (SaveGame::battle_window).
BATTLE_WINDOW_POINTER = 0x6CBB10
BATTLE_WINDOW = 0x0510_0000  # its own mapping: the object, the vtable, the two methods and the trap
BATTLE_WINDOW_VTABLE = BATTLE_WINDOW + 0x1000  # 0x100 entries, all the trap but +0xC8 and +0x118
BATTLE_WINDOW_IS_OPEN = BATTLE_WINDOW + 0x1800  # mov al, [ecx+0x48]; ret
BATTLE_WINDOW_CLOSE = BATTLE_WINDOW + 0x1810  # mov byte [ecx+0x48], 0; mov dword [ecx+0x5A4], 0; ret
BATTLE_WINDOW_TRAP = BATTLE_WINDOW + 0x1F00
BATTLE_WINDOW_SIZE = 0x2000
OPEN_WINDOW = 0x41D9B0  # thiscall on the window (a description of 0x20 bytes); ret 4, al
WINDOW_BUTTON = 10  # [0x66BDC0]: the result of its first button
SEA_WINDOW_JUMPS = (0x60659B, 0x606E7E, 0x606FEA, 0x60CA4E)
TOWN_WINDOW_JUMPS = (0x5FD0E7,)  # a town battle's question (piece 7: off with the stand-in, as TOWN_END_WINDOW)
HUMAN_UI_JUMPS = {0x608296: 0x608307,  # 0x608230: the views; [0x6E59CC] kept
                  0x6006E9: 0x600755,  # 0x600660 (a town battle): the town shown, the sea map, clouds; [0x6E59CC] kept
                  0x611EEA: 0x611F9B,  # 0x611E50 (after a load): the views of the battle put on the screen
                  0x602D0C: 0x602D7A,  # a ball's constructor on the screen: its sprite
                  0x6031BF: 0x6032F8}  # a ball landing on the screen: the town view's cell under it (the water's sound)
# The same where the stack must be put right: address → (where to go on, bytes to drop).
HUMAN_UI_SKIPS = {0x61030D: (0x610371, 8),  # 0x6102A9 (the player takes a battle): the views, the clouds and rocks
                  # the animations of the town view (0x590610, 7 arguments) of a ball landing, hitting a ship
                  # (flames, a splash) and of its smoke (the rand() of each puff runs)
                  0x603357: (0x60335C, 0x1C), 0x603689: (0x60368E, 0x1C), 0x60372A: (0x60372F, 0x1C),
                  0x603835: (0x60383A, 0x1C),
                  # a town battle's: a ball hitting a tower and its smoke (0x5FF8E0), a tower falling (0x60130B)
                  0x5FFABC: (0x5FFAC1, 0x1C), 0x5FFBD1: (0x5FFBD6, 0x1C), 0x601536: (0x60153B, 0x1C),
                  # 0x6016CB (a touch on a tower): the town view's 0x590960 (the touched cell to its building's)
                  0x60171E: (0x601723, 0xC)}
SOUND_LOOKUP = 0x443690  # thiscall on the sound (*0x6CBDB8), the sound's id; ret 4: its variants
SOUND_VARIANTS = 0
TOWN_END_TEXT = 0x1310
TOWN_END_TEXT_SIZE = 0x200
NO_DISBAND_JUMP = {0x505FF1: 0x506029}
# 0x50D0C0 (thiscall on the ships object, a ship): the ship back in its place
# in the list by x. A pirate convoy left without a guide (its guide taken in a
# sea battle, or its pirate gone) sorts ship 0xFFFF in 0x507A65 (0x507B68),
# outside the ships: in Windows it reads whatever memory is there, in the
# emulator zeros, which make the list a ring and the sort never ends. With
# --pirates the call returns at once for a ship that doesn't exist, as
# p3vita leaves it out (pirate_convoy in battle/pirates.cpp).
SORT_X = 0x50D0C0
# With --pirates, the pirate objects' daily update runs (0x514000, otherwise
# quiet) and the operation it queues is executed: 0xB5, a convoy leaves the
# nest (0x514D40).
PIRATE_UPDATE = 0x514000
# Operation 0x96 (0x60FEC9) hands a merchant's sides of a battle to the
# computer: the battles' setup and step queue it for the player's human sides.
PIRATE_OPERATIONS = (0xB5, 0x96, 0xB4,  # 0xB4: a new pirate when a nest is destroyed (0x514920)
                     0x92, 0x93, 0x95, 0x97, 0x98, 0x9A, 0x9B, 0xA0)  # the player's orders in a battle (piece 7)
PIRATE_WAYPOINTS = 0x448780
# The battles (docs/cloud/battles-2.md §3): the object of 0x6E55D0 and its
# heap blocks, recorded as the region BT (TownOracle.battle_graph).
BATTLES = 0x6E55D0
READ_BATTLES = 0x611AE0  # thiscall on BATTLES, (data, version) → end
SEPARATOR_BYTES = b'\xfe\xaf\x00\x00'
BATTLES_SIZE = 0xC48
BATTLE_COUNT = BATTLES + 0x404  # u8: slots in use
BATTLE_POINTER = 0xF0000000  # | block << 16 | offset: a pointer inside the graph
HEAP_BEGIN, HEAP_END = 0x1000_0000, 0x1800_0000
# The words of a battle block that are numbers even when they look like a
# pointer, by (size, vtable): a scene's ship's place and speeds in 16.16 (x ·
# 65536 can be the address of a heap block: at the start of a battle the
# places are whole and a block can start at 0x10xx0000) and its marks of
# the 256 directions [+0x27] (the words from [+0x24] to [+0x124]: bytes 00 00
# 00 10 are the heap's first block, 0x10000000), a town battle's ship's map of
# distances [+0x164] (64 × 64 bytes) too, a battle's generator. As
# BattleGraph::number_field.
# [+0x20]: its goal's x and y (u16 each, the battles' piece 7: the player's ships' goals such as (0x10A9, 0x1028)).
SCENE_SHIP_NUMBERS = frozenset((0x8, 0xC, 0x14, 0x18, 0x1C, 0x20, *range(0x24, 0x128, 4)))
BATTLE_NUMBERS = {(0x164, 0x67B0D4): SCENE_SHIP_NUMBERS, (0x1E98, 0x67AD44): (0xADC,),
                  # a town battle (0x611D80) and its scene's ships (0x5FBFE0)
                  (0x1168, 0x67AAD0): SCENE_SHIP_NUMBERS | frozenset(range(0x164, 0x1164, 4)),
                  (0x2F24, 0x67AAA0): (0xADC,)}
BATTLE_BLOCKS = ((0x1E98, 0x67AD44), (0x2F24, 0x67AAA0))  # a sea battle, a town battle
# A sea battle's scene's arrays of numbers (0x61A1D0): bytes [+0x18], u16
# [+0x1C] and the free list [+0x24], and the items' boxes [+0x28], [+0x2C],
# [+0x30], [+0x38], [+0x3C], [+0x40] (x1, x2 and y1, y2 per item).
SCENE_NUMBER_ARRAYS = (0x18, 0x1C, 0x24, 0x28, 0x2C, 0x30, 0x38, 0x3C, 0x40)
# With --pirates, after each tick with battles the oracle executes operation
# 0x94 with BATTLE_STEPS steps (the clock rule of docs/oracle.md: the game's
# real-time driver 0x612DB0 queues it with the steps of the real clock, about
# 84.4 per tick). (Until cloud/battles-2 this needed --battles, and without
# it the attack 0x507BE9 was jumped over too.)
BATTLE_STEPS = 84
# The town view (*0x6E51AC, the interface's class of 0x5871xx–0x59Fxxx), which
# the game always has and the emulator didn't: the battles read its geometry
# (a sea battle's rocks, 0x60F479: [+0x510]; a town battle, piece 6: the
# point → cell 0x58D030 and back 0x58CFB0, and the attacked town's map file,
# 0x58E760). With the pirates (the battles), the oracle gives it one with what
# the game puts in it at start (0x5871xx: scripts/iso.ini [LIGHTNINGVIEW0],
# TileSize → [+0x510], [+0x514], [+0x518]; DownVector → [+0x4D0], [+0x4D4];
# TownPath and TownFormat → the CStrings [+0x4C4], [+0x4C8]; 0x5894F0 locks
# the texture DetectBitmap (scripts/textures.ini) as 8-bit palette indices,
# the hit mask, into [+0xC6D8] with its pitch at [+0xC6E8]) and when it shows
# a town (0x589DE0, 0x58B680: the map's cells [+0xC314] × [+0xC318] and
# pixels [+0xC31C] × [+0xC320]; every iso/towns/*.town is 128 × 226), showing
# none ([+0xC324] = 0xFF, as 0x589DE0 leaves it): the simulation's branches
# «if the view shows this town» (0x4E1CC9, 0x521A3C, 0x5240F5... about 20)
# only redraw it, and with 0xFF they go as without a view. Its vtable and its
# animations' list's ([+0x3F8], an embedded object) stop the emulator: only
# the interface calls them. The rest is 0, as the page 0 gave before for the
# fields below 0x1000 (the only one the simulation read: [+0x510] in 0x60F479).
TOWN_VIEW_POINTER = 0x6E51AC
TOWN_VIEW = 0x0500_0000  # its own mapping: the object, then the mask, the strings, the vtable and the trap
TOWN_VIEW_OBJECT_SIZE = 0xD000
TOWN_VIEW_MASK = TOWN_VIEW + 0xD000
TOWN_VIEW_STRINGS = TOWN_VIEW + 0xE000
TOWN_VIEW_VTABLE = TOWN_VIEW + 0xF000  # 0x100 entries, all the trap
TOWN_VIEW_TRAP = TOWN_VIEW + 0xFF00
TOWN_VIEW_SIZE = 0x10000
TOWN_MAP_CELLS = (128, 226)
# What the game does only with a town view that changes nothing of the world
# (single player), jumped over as without it: 0x4D7B10 (a letter's
# announcement on the local player's screen, from 0x4D6530: the game
# screen's message 0x42BB20 with the view's vtable, a sound; in a network
# game also the letter's [+0x1] |= 0x40, 0x4D7B76).
TOWN_VIEW_JUMPS = {0x4D7B34: 0x4D7D33}
EPILOGUE = 0x5079E8
DOCKED_STATES = (0, 1, 2, 3, 4, 5)

# Funciones de interfaz que no cambian el estado del mundo: dirección →
# bytes de argumentos que sacan al volver.
QUIET = {
    0x548CA0: 0x18,  # mensaje al jugador
    0x613920: 4,  # escena del puerto (0x6E6220)
    0x48FD90: 0,  # the trade route window after operation 0x6B (*0x6CE6D0)
    0x6141D0: 8,  # the port scene shows another town (operations 0x16, 0x17)
    0x4D6530: 8,  # mensaje a uno o varios comerciantes (buzón 0x6DD730)
}
# Sending a letter (0x4D6530 on the mailbox 0x6DD730, docs/formats/mail.md)
# runs only from the calls p3vita has connected (`letters='connected'`, the
# default): from the others it is silenced as above, so that the world stays
# comparable while the calls are connected group by group. `--letters all`
# lets every call through, `none` silences them all (as before 2026-11-03).
MAIL_SEND = 0x4D6530
# The ticker lines (0x548CA0 on the real-time object, docs/formats/mail.md §6):
# silenced, but recorded (region TK) when they go to the local player and
# their kind has a line (0x5483D0 shows nothing for these).
TICKER_SEND = 0x548CA0
TICKER_SILENT = (0x02, 0x0F, 0x11, 0x13, 0x14, 0x17, 0x18, 0x1A, 0x1E, 0x1F, 0x26, 0x27)
TICKER_FIXED = (0x09, 0x0C, 0x10, 0x12, 0x19)  # a fixed text: the argument recorded as 0
MAIL_CALLS = (
    # A new human merchant (0x530470 with kind 0, the new game's player 0xA7,
    # new_game.cpp): the chronicle's 0x0E.
    0x530625,
    # The merchant's tick (0x4F7BB0, merchants.cpp): letters 0x0F, 0x10, 0x11
    # to the merchant and 0x06 (a promotion) to the chronicle.
    0x4F7CB0, 0x4F7CF6, 0x4F7D33, 0x4F82D9,
    # The end of a sea battle (0x60D299, battle/battle_end.cpp): the report
    # 0x29 to each side of the computer, a pirate's win to the chronicle
    # (0x2A) and the letters 0x72.
    0x60DD93, 0x60EB42, 0x60EC1B, 0x60EC83,
    # The rest of the sea battles: a convoy after its battle (0x50BFD0,
    # battle_end.cpp: 0x54 and the news 0x2A of a pirate's captain), the
    # boarding (0x60C1C9, capture.cpp: 0x2A, 0x54, the share 0x30), the
    # take-over (operation 0x91, 0x540520: 0x54, 0x30), the privateer going
    # home (0x514D40, pirate_objects.cpp: 0x2A) and a pirate's ship sunk
    # (0x61F96F, ship_step.cpp: 0x2A).
    0x50C274, 0x50C326, 0x50C853, 0x60C31B, 0x60C383, 0x60D189, 0x540A8C, 0x540DDF, 0x514F79, 0x61FC80,
    # The end of a town battle (0x5FE7E0, battle/town_end.cpp): a coward 0x3B,
    # the news 0x3A or 0x39 to the chronicle and the report 0x29; the plunder's
    # share 0x6A (0x633DB0 with a convoy, sieges.cpp).
    0x5FEA54, 0x5FEEAA, 0x5FF55F, 0x6345D3,
    # merchants.cpp: 0x4F7510 (0x5E, 0x5F), 0x4F75F0 (0x6D, 0x82).
    0x4F75AD, 0x4F75C2, 0x4F7827, 0x4F793D,
    # events_big.cpp: a merchant's task, event 0x05 (0x4E5B84: 0x19 or 0x0A,
    # 0x4A, 0x4B), and event 0x16 (0x4E8234: 0x21).
    0x4E5D4D, 0x4E5F2A, 0x4E6286, 0x4E8613,
    # facilities.cpp (0x00 launching, 0x03 works done), offices.cpp (0x60),
    # core.cpp (0x35 famine), events.cpp (0x81 the hulk), construction.cpp
    # (0x0C the guild, 0x2F the town wall's bill), convoys_members.cpp (0x32,
    # 0x31 a convoy's fees).
    0x5084A5, 0x5202EF, 0x500280, 0x527F53, 0x4E22A8, 0x4F8639, 0x523702, 0x50A966, 0x50AA02,
    # events_rare.cpp: the pirate hunt of event 0x1E (0x4DFD44: 0x41, 0x42,
    # 0x43), the plague (0x4E9094: 0x58), the council (0x4E9A94: 0x6B x2),
    # the Hanse Day's task (0x4EA594: 0x77 x5, 0x78), the extra tax
    # (0x4EAB94: 0x6C), the theft (0x4EC934: 0x73), the monument (0x4ECB14:
    # 0x7B x2), the expedition's report (0x4E6A54: 0x63, with its text), its
    # losses (0x502B20: 0x34) and discoveries (0x52D0B0: 0x1C x2; 0x5319D0:
    # 0x56).
    0x4E0065, 0x4E00CF, 0x4E02B6, 0x4E9389, 0x4E9F8F, 0x4E9FBD, 0x4EA72B, 0x4EA77F, 0x4EA825, 0x4EA8B0,
    0x4EA979, 0x4EAB45, 0x4EAE01, 0x4ECAEC, 0x4ECDAE, 0x4ECDBF, 0x502CDF, 0x52D1E7, 0x52D1F8, 0x531B46,
    0x4E79A3,
    # events_towns.cpp: the debts' notices (0x4DB4E0: 0x26, 0x79, 0x12), the
    # feast (0x4E23A4: 0x24), the wedding (0x4E7C74: 0x23 to the chronicle,
    # 0x25 the dowry), the town's fire (0x4E9564: 0x59) and a burnt store
    # (0x500C30: 0x75), the impounding (0x4F6570: 0x13 x4), a new office
    # (0x5308A0: 0x0D to the chronicle) and a captain's progress (0x538A80:
    # 0x4D).
    0x4DB5BE, 0x4DB652, 0x4DB6C7, 0x4E2570, 0x4E7DA1, 0x4E80E5, 0x4E998A, 0x500E8F, 0x4F6618, 0x4F66FA,
    0x4F6818, 0x4F68A3, 0x530C53, 0x538C16,
    # events_mayor.cpp: the Hanse vote's result (0x4DC1F0: 0x0B), the
    # auctions (0x4E2CD4: 0x2C/0x2D, 0x5D, 0x2E, 0x76/0x7D, 0x7F, 0x7E), the
    # mayor's election (0x528E60: 0x47 with its text, 0x55 to the chronicle)
    # and the Alderman's (0x5299F0: 0x47 with its text, 0x57).
    0x4DC53D, 0x4E32E9, 0x4E3378, 0x4E33FC, 0x4E3598, 0x4E363A, 0x4E36B3, 0x5294F7, 0x5295BD, 0x529CA0,
    0x529D8A,
    # The player's operations (town_tick/ship_operations.cpp): a new captain
    # (0x537EE0: 0x04), no repair (0x52ACD0: 0x02), a ship's order cancelled
    # (0x52B110: 0x14).
    0x537FC6, 0x52AEDD, 0x52B288,
    # alderman.cpp: a Hanse task of a merchant expired (0x4EB0EB: 0x70),
    # started (0x4EBF54: 0x80 the road, 0x6E and 0x84 the pirate nest) and
    # done (0x4EC354: 0x6F to the chronicle and to the merchant).
    0x4EB32B, 0x4EC18F, 0x4EC230, 0x4EC2B2, 0x4EC4FC, 0x4EC51C,
    # sieges.cpp: the siege's news to the chronicle, 0x38 when the town pays
    # the ransom (0x6281F0: 0x628A52) or the army leaves (0x629A2B), 0x53 a
    # new army (0x629C60), and the prince's payment 0x85 (0x636230). (The 0x6A
    # of 0x633DB0, only with a convoy, goes with the town battle's end above.)
    0x628A52, 0x629A2B, 0x629F63, 0x6364E9,
    # operations.cpp: a captain retires (operation 0x13, 0x538C40: 0x52), a
    # wedding arranged (0x60, 0x53DA90: 0x33), a manager's progress (0x67,
    # 0x53DDF0: 0x7C), a child born (0x87, 0x53FEA0: 0x7A).
    0x538D76, 0x53DBC7, 0x53DED7, 0x53FFF4,
    # diet.cpp: the Hanse's missions (0x4E09D4): 0x48 in phase 0, 0x4C in
    # phase 2, 0x5C when the fleet disbands.
    0x4E0CEE, 0x4E0F4C, 0x4E1371,
    # sailing.cpp: a human owner's ship sunk (0x519D90 from the ships'
    # update 0x506D3D: 0x1B).
    0x519E87,
    # events_towns.cpp: the dividends of company shares (event 0x18, 0x4E8804:
    # 0x62 to the owner, 0x28 when he can't pay, 0x27 to the holder).
    0x4E8975, 0x4E8A49, 0x4E8AD2,
)
MAIL_CALLS += (
    # The trade routes (trade_routes.cpp): the report 0x07 of a ship alone (0x4D5200) and of a convoy (0x503230).
    0x4D55F9, 0x5035CD,
)
MAIL_CALLS += (
    # The letters' buttons: a suitor paid too late (operation 0x72, 0x53EAC0, operations.cpp: 0x2B) and the proposal
    # (event 0x17, 0x4E8684, events_big.cpp: 0x22).
    0x53EBC8, 0x4E87CF,
    # The prince's request in a siege (operation 0x8B, 0x635B80, sieges.cpp: 0x64) and its acceptance (0x8C,
    # 0x635CD0: 0x65).
    0x635C9F, 0x635DA9,
    # The impounding (operation 0x77, 0x53EEC0, operations.cpp: 0x83 with its text).
    0x53F6D6,
)
MAIL_CALLS += (
    # An offer between merchants (operation 0x5F, 0x5338A0, town_tick/offers.cpp): the memo 0x1E to A and the offer
    # 0x1F to B.
    0x533C9B, 0x533CB5,
)
MAIL_CALLS += (
    # The tavern's traveller (town_tick/tavern.cpp): his letter 0x17 (operation 0x53, 0x53C840) and the thanks 0x18
    # of the event 0x10 (0x4E63E4: at his destination, and past the day elsewhere).
    0x53C9B5, 0x4E65CC, 0x4E6691,
    # The tavern's burglar's event 0x0F (0x4E5664, tavern.cpp): caught, 0x74 to the victim; failed, 0x16 to who hired
    # him; the burglary, 0x20 to the victim and 0x15 to him.
    0x4E5925, 0x4E594D, 0x4E5AFF, 0x4E5B1E,
    # A cart convoy by land, the event 0x0C (0x4E38B4, land_transport.cpp): 0x67 at a besieged town (leaving, arriving),
    # 0x68 an ambush, 0x69 lost, 0x66 arrived.
    0x4E3B4B, 0x4E3CFC, 0x4E3C6E, 0x4E3D43, 0x4E3E95,
    # The Hanse tasks window's developers' tools (operation 0x4A, 0x53B1F0, events_rare.cpp): the meeting's 0x78.
    0x53B40C,
    # An order of the town hall (town_hall.cpp): taken, 0x1A (operation 0x3D, 0x53A620); at its deadline, the event
    # 0x11 (0x4E6744): 0x5B «Dishonoured», 0x5A «Fulfilled».
    0x53A794, 0x4E6878, 0x4E6987,
    # A merchant moves his home town (operation 0x3E, 0x53A810, town_hall.cpp): 0x0E «Move» to the chronicle.
    0x53A910,
    # The sieges' operations (sieges.cpp): 0x8F (0x540240) the news 0x38 and the extra tax 0x37; 0x90 (0x634660) 0x38.
    0x5402BF, 0x5403DB, 0x63477B,
)
MAIL_CALLS += (
    # The mission interpreter's letter, its instruction 0xF7 (0x4ED4A0, mission_script.cpp): to the owners of a town's
    # offices (a dead branch: the memset before it), to each human merchant, or to the addressee as it is.
    0x4EDDEB, 0x4EDE69, 0x4EDEAA,
    # Its 0xF6 (0x4EDF3F): the treasure map's letter 0x45 or 0x46; its 0x68 (0x4F286E, no file uses it): a letter 0x22.
    0x4EDFED, 0x4F2A67,
    # A harvest, the event 0x09 that its 0xFB makes (0x4E2824, town_tick/events_towns.cpp): the letters 0x08 to the
    # town's producers and 0x09 to the chronicle, at its start and its end.
    0x4E2941, 0x4E299D, 0x4E2A9E, 0x4E2B04,
)
MAIL_SITES = {call + 5 for call in MAIL_CALLS}  # return addresses
# With the environment variable P3_MAIL_STATS, each call to 0x4D6530 is
# counted by its call address and printed to stderr at the end (whether it
# ran or was silenced).
MAIL_STATS = {} if os.environ.get('P3_MAIL_STATS') else None


def _print_mail_stats():
    if MAIL_STATS:
        print('letters by call: ' + ' '.join(f'{c:#x}{"" if c in MAIL_CALLS else "(quiet)"}:{n}'
                                             for c, n in sorted(MAIL_STATS.items())), file=sys.stderr)


atexit.register(_print_mail_stats)
# Con barcos, además (ver sail_oracle.py):
QUIET_SHIPS = {
    0x44F940: 4,  # avisa a la interfaz de que un barco del jugador ha cambiado
    0x453DF0: 0,  # interfaz
    0x6132C0: 4, 0x6135C0: 8,  # escena del puerto
    0x514000: 4,  # objetos del final de la sección de barcos
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
    # The mission interpreter's 0xF7 (0x4ED4A0) with its ticker line to the player: the messages window told
    # (0x47D1F0 on 0x6CC960, the interface's) when no battle window is open (0x4F3940).
    0x4EDF27: 0x4EDF31,
    # Its 0xF5 (0x4EE012, no file uses it): the game's message window to the player (0x41D9B0 on *0x6CBB10).
    0x4EE042: 0x4EE085,
}
# Functions that read a local before writing it (a game bug: the value is
# whatever an earlier call left on the stack). p3vita takes it as 0, which is
# what the oracle's stack had until the letters (0x4D6530) ran real code
# deeper in it; so their locals (the size, below the return address) are set
# to 0 at their entry.
ZEROED_FRAMES = {
    0x4F5A20: 0x44,  # the best rent [esp+0x2C] (town_tick/events.cpp, fn_4f5a20)
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


def lz_decompress(blob, expected):
    """A block of the game's LZ (src/p3data/lz.cpp): s32 n, 0 empty, −n bytes
    stored, n bytes compressed in an LSB-first bit stream of literals (1 bit 0,
    8 bits) and matches (1 bit 1, a 3-bit class c and c + 1 bits of distance,
    a length of 2 plus fields of 2, 3, 4... bits while a field is all ones)."""
    n = struct.unpack_from('<i', blob, 0)[0]
    if n <= 0:
        return bytes(blob[4:4 - n])[:expected]
    acc, have, p, out = 0, 0, 4, bytearray()

    def bits(count):
        nonlocal acc, have, p
        while have < count:
            acc |= blob[p] << have
            p, have = p + 1, have + 8
        value = acc & ((1 << count) - 1)
        acc, have = acc >> count, have - count
        return value

    while len(out) < min(n, expected):
        if not bits(1):
            out.append(bits(8))
            continue
        c = bits(3)
        distance = bits(c + 1) + (2 << c) - 1
        length, width = 2, 2
        while True:
            value = bits(width)
            length += value
            if value != (1 << width) - 1:
                break
            width += 1
        for _ in range(length):
            out.append(out[-distance])
    return bytes(out[:expected])


def aim_indices(data):
    """An AIM of type 34 (8-bit indexed, docs/formats/aim.md §4.1): (width,
    height, stride, the indices in rows of `stride` bytes), whose rows AIM.dll
    gives for the encoding 1 (PALETTE8). The root chunk (type 3) holds the
    layer after its 0x25 bytes."""
    pos = 4
    while data[pos] != 3:
        pos += struct.unpack_from('<I', data, pos + 1)[0]
    layer = pos + 0x25
    if data[layer] != 34:
        raise RuntimeError(f'AIM layer of type {data[layer]}, not 34')
    width, height, stride = struct.unpack_from('<III', data, layer + 5 + 4)
    p = layer + 5 + 16
    size = struct.unpack_from('<I', data, p)[0]
    p += 4 + size  # the palette
    indices = bytearray()
    while len(indices) < stride * height:
        size = struct.unpack_from('<I', data, p)[0]
        indices += lz_decompress(data[p + 4:p + 4 + size], min(0x10000, stride * height - len(indices)))
        p += 4 + size
    return width, height, stride, bytes(indices)


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
                 operations=OPERATIONS, storms=True, pirates=True, statistics=False, last_steps=True,
                 disband=True, battles=False, letters='connected', missions=False):
        # missions: the load runs 0x511270 (the missions' list) and 0x511690 (each script's program) as the game's,
        # reading scripts/missions_eng.ini and missions_addon/ (DATOS or --cpr); without it (as before 2026-11-19)
        # 0x511270 is skipped and the scripts in the events 0x1B have no program (the interpreter ends them).
        self.missions = missions
        self.pirates = pirates and navdata is not None and convoys
        self.battles = self.pirates  # `battles` is kept for older scripts.
        self.letters = letters
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
        self.statistics = statistics
        self.last_steps = last_steps
        _, build = trampoline(STATISTICS, 0)
        self.uc.mem_write(STATISTICS_TRAMPOLINE, build(STATISTICS_TRAMPOLINE))
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
                    jumps = dict(PIRATE_JUMPS if disband else {**PIRATE_JUMPS, **NO_DISBAND_JUMP})
                    jumps.update(BATTLE_WINDOW_JUMPS)
                    self.uc.hook_add(UC_HOOK_CODE, self._sort_x_guard, begin=SORT_X, end=SORT_X)
                    self.uc.hook_add(UC_HOOK_CODE, self._attack_head, begin=UPDATE_SHIPS_ENTRY, end=UPDATE_SHIPS_ENTRY)
                    self.uc.hook_add(UC_HOOK_CODE, self._nest_attack_head, begin=NEST_TASK_ENTRY, end=NEST_TASK_ENTRY)
                    self.window_jump_hooks = []
                    for address, target in jumps.items():
                        h = self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=address, end=address, user_data=target)
                        if address in SEA_WINDOW_JUMPS or address in TOWN_WINDOW_JUMPS:
                            self.window_jump_hooks.append(h)
                    save_at, text_at, _ = TOWN_END_WINDOW
                    for at in (save_at, text_at):
                        self.window_jump_hooks.append(
                            self.uc.hook_add(UC_HOOK_CODE, self._town_end_window, begin=at, end=at))
                    self.sea_map_icons = []
                    for address, target in SEA_MAP_ICON.items():
                        self.uc.hook_add(UC_HOOK_CODE, self._sea_map_icon, begin=address, end=address, user_data=target)
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
        for f in TEXT_INITIALIZERS:
            self.call(f, [])
        for address, target in WINDOW_JUMPS.items():
            self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=address, end=address, user_data=target)
        for address, size in ZEROED_FRAMES.items():
            self.uc.hook_add(UC_HOOK_CODE, self._zero_frame, begin=address, end=address, user_data=size)
        if 0x4D85C0 in self.rest:
            quiet.update(QUIET_EVENTS)
            for address, target in EVENT_JUMPS.items():
                self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=address, end=address, user_data=target)
        for address, pop in quiet.items():
            self.skipped[address] = pop
            self.uc.hook_add(UC_HOOK_CODE, self._quiet, begin=address, end=address)
        if self.pirates:
            self.setup_town_view()
        if self.missions:
            # The missions read their ini (0x4BE060) at a load and at a game's start (the operation 0xC0); their
            # files go through Archiver.dll (_import).
            self.uc.hook_add(UC_HOOK_CODE, self._ini_string, begin=INI_STRING, end=INI_STRING)
        self.uc.hook_add(UC_HOOK_CODE, self._rand, begin=RAND, end=RAND)
        # push dword [esp+4]; call 0x51BA10; ret 4
        call = struct.pack('<i', TOWN_TICK - (TRAMPOLINE + 4 + 5))
        self.uc.mem_write(TRAMPOLINE, bytes([0xFF, 0x74, 0x24, 0x04, 0xE8]) + call + bytes([0xC2, 0x04, 0x00]))

    def _quiet(self, uc, address, size, user_data):
        if address == MAIL_SEND and self.letters != 'none':
            ret = self.u32(uc.reg_read(UC_X86_REG_ESP))
            if MAIL_STATS is not None:
                MAIL_STATS[ret - 5] = MAIL_STATS.get(ret - 5, 0) + 1
            if self.letters == 'all' or ret in MAIL_SITES:
                return  # the game's 0x4D6530 runs
        if address == TICKER_SEND:
            # The ticker line (silenced, the interface's): what the local player
            # would see, for tick_probe (region TK, SaveGame::player_ticker).
            kind = self._arg(4) & 0xFF
            if self._arg(0) & 0xFFFF == self.u32(PLAYER) and kind not in TICKER_SILENT:
                e = 0 if kind in TICKER_FIXED else self._arg(5) & 0xFFFF
                self.__dict__.setdefault('ticker', []).append(struct.pack('<BH', kind, e))
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

    def _town_end_window(self, uc, address, size, user_data):
        # TOWN_END_WINDOW: keep [+0x1310] at the human side's branch; at the text, print it, put it back, skip
        # the window.
        save_at, text_at, next_side = TOWN_END_WINDOW
        b = uc.reg_read(UC_X86_REG_ESI)
        if address == save_at:
            self._town_end_kept = bytes(uc.mem_read(b + TOWN_END_TEXT, TOWN_END_TEXT_SIZE))
            return
        text = bytes(uc.mem_read(b + TOWN_END_TEXT, TOWN_END_TEXT_SIZE)).split(b'\0')[0].decode('latin-1')
        print(f'town battle end window: {text!r}')
        uc.mem_write(b + TOWN_END_TEXT, self._town_end_kept)
        uc.reg_write(UC_X86_REG_EIP, next_side)

    def use_battle_window(self, sound_variants=SOUND_VARIANTS):
        """The stand-in battles' window (BATTLE_WINDOW): the sea battles' question and end take the game's path
        with a window (the scenarios of the battles' piece 7); the sounds' variants (SOUND_LOOKUP: with any, each
        sound of the battle on the screen takes the world's rand())."""
        self.sound_variants = sound_variants
        uc = self.uc
        for h in self.window_jump_hooks:
            uc.hook_del(h)
        self.window_jump_hooks = []
        uc.mem_map(BATTLE_WINDOW, BATTLE_WINDOW_SIZE)
        uc.mem_write(BATTLE_WINDOW_VTABLE, struct.pack('<I', BATTLE_WINDOW_TRAP) * 0x100)
        self.write_u32(BATTLE_WINDOW_VTABLE + 0xC8, BATTLE_WINDOW_IS_OPEN)
        self.write_u32(BATTLE_WINDOW_VTABLE + 0x118, BATTLE_WINDOW_CLOSE)
        uc.mem_write(BATTLE_WINDOW_IS_OPEN, b'\x8a\x41\x48\xc3')
        uc.mem_write(BATTLE_WINDOW_CLOSE, b'\xc6\x41\x48\x00\xc7\x81\xa4\x05\x00\x00\x00\x00\x00\x00\xc3')
        uc.mem_write(BATTLE_WINDOW_TRAP, b'\xc3')
        uc.hook_add(UC_HOOK_CODE, self._battle_window_trap, begin=BATTLE_WINDOW_TRAP, end=BATTLE_WINDOW_TRAP)
        self.write_u32(BATTLE_WINDOW, BATTLE_WINDOW_VTABLE)
        self.write_u32(BATTLE_WINDOW_POINTER, BATTLE_WINDOW)
        uc.hook_add(UC_HOOK_CODE, self._open_window, begin=OPEN_WINDOW, end=OPEN_WINDOW)
        uc.hook_add(UC_HOOK_CODE, self._sound_variants, begin=SOUND_LOOKUP, end=SOUND_LOOKUP)
        for address, target in HUMAN_UI_JUMPS.items():
            uc.hook_add(UC_HOOK_CODE, self._jump, begin=address, end=address, user_data=target)
        for address, skip in HUMAN_UI_SKIPS.items():
            uc.hook_add(UC_HOOK_CODE, self._skip, begin=address, end=address, user_data=skip)
        uc.ctl_remove_cache(0x400000, 0x680000)
        self.battle_window = True
        self.windows_opened = []

    def _skip(self, uc, address, size, skip):
        target, drop = skip
        uc.reg_write(UC_X86_REG_ESP, uc.reg_read(UC_X86_REG_ESP) + drop)
        uc.reg_write(UC_X86_REG_EIP, target)

    def close_battle_window(self, result=0):
        """The player closes the battles' window: WINDOW_BUTTON with its button, 0 otherwise."""
        self.uc.mem_write(BATTLE_WINDOW + 0x48, b'\0')
        self.write_u32(BATTLE_WINDOW + 0x5A4, result)

    def _open_window(self, uc, address, size, user_data):
        # OPEN_WINDOW on the stand-in: an open one closed first (result 0), the description copied, open; ret 4, al 1.
        esp = uc.reg_read(UC_X86_REG_ESP)
        window, desc = uc.reg_read(UC_X86_REG_ECX), self.u32(esp + 4)
        if window != BATTLE_WINDOW:
            raise RuntimeError(f'0x41D9B0 on {window:#x}, called from {self.u32(esp) - 5:#x}')
        if self.u8(window + 0x48):
            self.write_u32(window + 0x5A4, 0)
        uc.mem_write(window + 0x5B8, bytes(uc.mem_read(desc, 0x20)))
        uc.mem_write(window + 0x48, b'\x01')
        text = bytes(uc.mem_read(self.u32(desc), 0x200)).split(b'\0')[0].decode('latin-1')
        self.windows_opened.append(text)
        print(f'battle window: {text!r}')
        uc.reg_write(UC_X86_REG_EAX, 1)
        uc.reg_write(UC_X86_REG_ESP, esp + 8)
        uc.reg_write(UC_X86_REG_EIP, self.u32(esp))

    def _battle_window_trap(self, uc, address, size, user_data):
        ret = self.u32(uc.reg_read(UC_X86_REG_ESP))
        raise RuntimeError(f'a virtual method of the battles\' window called from {ret - 6:#x}')

    def _sound_variants(self, uc, address, size, user_data):
        # SOUND_LOOKUP: the variants of a sound (SOUND_VARIANTS); ret 4.
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EAX, self.sound_variants)
        uc.reg_write(UC_X86_REG_ESP, esp + 8)
        uc.reg_write(UC_X86_REG_EIP, self.u32(esp))

    def _sea_map_icon(self, uc, address, size, target):
        # SEA_MAP_ICON: the sea map's battle icon (the game's 0x457C90), then the flag's clear.
        b = uc.reg_read(UC_X86_REG_ESI)
        icon = (self.u16(b + 0x662), struct.unpack('<h', bytes(uc.mem_read(b + 0xAD6, 2)))[0],
                struct.unpack('<h', bytes(uc.mem_read(b + 0xADA, 2)))[0])
        self.sea_map_icons.append(icon)
        uc.reg_write(UC_X86_REG_EIP, target)

    def _attack_head(self, uc, address, size, user_data):
        # 0x506720's local list head of a raider's attack (ATTACK_HEAD), only that one.
        uc.mem_write(uc.reg_read(UC_X86_REG_ESP) - 0xC, struct.pack('<I', ATTACK_HEAD))

    def _nest_attack_head(self, uc, address, size, user_data):
        # 0x4EAFE4's local list head of a ship alone attacking a nest (NEST_TASK_ENTRY).
        uc.mem_write(uc.reg_read(UC_X86_REG_ESP) - NEST_ATTACK_HEAD_AT, struct.pack('<I', ATTACK_HEAD))

    def _zero_frame(self, uc, address, size, frame):
        # ZEROED_FRAMES: the function's locals start at 0.
        uc.mem_write(uc.reg_read(UC_X86_REG_ESP) - frame, bytes(frame))

    def _sort_x_guard(self, uc, address, size, user_data):
        # SORT_X with a ship that doesn't exist returns at once (ret 4).
        esp = uc.reg_read(UC_X86_REG_ESP)
        if self.u16(esp + 4) >= self.u16(SHIPS + 0xF4):
            uc.reg_write(UC_X86_REG_ESP, esp + 8)
            uc.reg_write(UC_X86_REG_EIP, self.u32(esp))

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
        # textos del disco (0x511270) que aquí se salta (the missions' list: with `missions` it runs).
        self.skipped[0x511270] = 4
        hook = None if self.missions else self.uc.hook_add(UC_HOOK_CODE, self._stub, begin=0x511270, end=0x511270)
        version = struct.unpack_from('<I', self.save, 0)[0] >> 16
        pos = self.ships_end + 4
        pos = self.call(0x4D6350, [FILE + pos], ecx=0x6DD730) - FILE + 4
        pos = self.call(0x4D4BC0, [FILE + pos], ecx=0x6DD728) - FILE + 4
        pos = self.call(0x4D81E0, [FILE + pos, version], ecx=EVENTS) - FILE
        if hook is not None:
            self.uc.hook_del(hook)
        if self.game_file('scripts/Namen_eng.ini') is not None:
            self.load_names(pos, version)
        self.call(MERGE_TOWN_FLAGS, [], ecx=0x6DF278)
        self.call(0x52E1A0, [block[0x10]], ecx=0x6DE3D8)  # dificultad del comercio
        _, _, merchants, _ = self.counts()
        player = max((i for i in range(merchants) if self.u16(self.merchant(i) + 8) == 0), default=0)
        self.write_u32(PLAYER, player)
        self.load_battles()

    def load_battles(self):
        """The battles of the save (0x611AE0 on 0x6E55D0, the third section
        from the end; 0x549F40 reads it after the events, and nothing read in
        between uses it): saves 6, 7 and tut4 start with a sea battle
        (docs/cloud/battles-2.md §5). Once (write_oracle.prepare_game_object
        doesn't read it again)."""
        separators = []
        pos = len(self.save) - 4
        while pos > 0 and len(separators) < 3:
            if self.save[pos:pos + 4] == SEPARATOR_BYTES:
                separators.append(pos)
            pos -= 1
        start, end = separators[2] + 4, separators[1]
        version = struct.unpack_from('<I', self.save, 0)[0] >> 16
        # The game's load resets the battles first (0x611840 from 0x54A31B): no battle on the screen ([+0x3FC]
        # 0xFF; the emulator's 0 would put slot 0 on it while a cannon ball is read, 0x602A90's sprite).
        self.uc.mem_write(BATTLES + 0x3FC, b'\xff')
        got = self.call(READ_BATTLES, [FILE + start, version], ecx=BATTLES) - FILE
        if got != end:
            raise RuntimeError(f'the battles end at {got}, not at {end}')
        self.battles_loaded = True

    def setup_town_view(self):
        """The town view of TOWN_VIEW_POINTER (see TOWN_VIEW), from the game's
        data: scripts/iso.ini, scripts/textures.ini and the DetectBitmap's AIM
        (DATOS, which re/emu/sesion6/run.sh fills, or --cpr)."""
        section = 'LIGHTNINGVIEW0'

        def ini(key, path='scripts/iso.ini', sect=section):
            value = self.ini_value(path, sect, key)
            if value is None:
                raise RuntimeError(f'the town view needs {key} of {path} (DATOS or --cpr)')
            return value

        def ints(key):
            return [int(v, 0) for v in ini(key).split()]

        uc, view = self.uc, TOWN_VIEW
        uc.mem_map(TOWN_VIEW, TOWN_VIEW_SIZE)
        tile, down = ints('TileSize'), ints('DownVector')
        uc.mem_write(view + 0x510, struct.pack('<3i', *tile[:3]))
        uc.mem_write(view + 0x4D0, struct.pack('<2i', *down[:2]))
        # The CStrings (MFC: the characters after {nRefs, nDataLength,
        # nAllocLength}; −1 references, a locked string nobody frees).
        at = TOWN_VIEW_STRINGS
        for offset, key in ((0x4C4, 'TownPath'), (0x4C8, 'TownFormat')):
            text = ini(key).encode('latin-1')
            uc.mem_write(at, struct.pack('<iii', -1, len(text), len(text)) + text + b'\0')
            self.write_u32(view + offset, at + 12)
            at += (12 + len(text) + 1 + 3) & ~3
        width, height = TOWN_MAP_CELLS
        uc.mem_write(view + 0xC314, struct.pack('<4I', width, height, width * tile[0], (height >> 1) * (tile[1] + 1)))
        self.write_u32(view + 0xC324, 0xFF)
        texture = ini('TextureFile', 'scripts/textures.ini', 'TEX' + ini('DetectBitmap'))
        data = self.game_file(texture)
        if data is None:
            raise RuntimeError(f'the town view needs {texture} (DATOS or --cpr)')
        mask_width, mask_height, stride, indices = aim_indices(data)
        pitch = (mask_width + 3) & ~3  # PALETTE8's rows (docs/formats/aim.md)
        for y in range(mask_height):
            uc.mem_write(TOWN_VIEW_MASK + y * pitch, indices[y * stride:y * stride + mask_width])
        self.write_u32(view + 0xC6D8, TOWN_VIEW_MASK)
        self.write_u32(view + 0xC6E8, pitch)
        uc.mem_write(TOWN_VIEW_VTABLE, struct.pack('<I', TOWN_VIEW_TRAP) * 0x100)
        uc.mem_write(TOWN_VIEW_TRAP, b'\xc3')  # a block of its own (zeros would run to the mapping's end)
        self.write_u32(view, TOWN_VIEW_VTABLE)
        self.write_u32(view + 0x3F8, TOWN_VIEW_VTABLE)
        uc.hook_add(UC_HOOK_CODE, self._town_view_trap, begin=TOWN_VIEW_TRAP, end=TOWN_VIEW_TRAP)
        for address, target in TOWN_VIEW_JUMPS.items():
            uc.hook_add(UC_HOOK_CODE, self._jump, begin=address, end=address, user_data=target)
        self.write_u32(TOWN_VIEW_POINTER, view)

    def _town_view_trap(self, uc, address, size, user_data):
        ret = self.u32(uc.reg_read(UC_X86_REG_ESP))
        raise RuntimeError(f'a virtual method of the town view (or of its animations) called from {ret - 6:#x}')

    def setup_imports(self, exe, constructors=(0x64F005, 0x64F034, 0x64F066, 0x64F098)):
        """Las funciones importadas se llaman por la tabla de importaciones, que
        en el emulador tiene lo que trae el fichero (la dirección relativa del
        nombre de cada función): se reserva esa zona y se pone un gancho en cada
        una. Las que no se hacen aquí paran la emulación con su nombre.
        `constructors`: the CString allocators' static constructors to run
        (none for spanish_exe.SpanishExe, which copies them already built)."""
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
        for f in constructors:
            self.call(f, [])

    def _import(self, uc, address, size, user_data):
        dll, name = self.imports.get(address, ('?', f'{address:#x}'))
        a = self._arg
        if name == 'lstrlenA':
            result, pop = self._lstrlen(a(0)), 4
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
            # The old block's requested bytes are kept and the rest is zero, as
            # alloc gives zeros (before, the copy went up to the old block's
            # rounded size, bringing the slack's stale bytes; the battles'
            # page arrays read them: docs/cloud/battles-2.md §4).
            kept = self.__dict__.get('requested', {}).get(old, self.sizes.get(old, 0))
            result = self.alloc(size)
            self.uc.mem_write(result, bytes(self.uc.mem_read(old, min(size, kept))))
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
        elif name == 'arc_Seek':
            # (handle, position from the start) → the position; the game's
            # wrapper 0x4027D0 turns its other two modes into this one with
            # arc_GetPos and arc_FileLength. A town battle's map file (0x58E760).
            f = self.files.get(a(0))
            if f:
                f[1] = a(1)
            result, pop = a(1) if f else 0, 0
        elif name == 'arc_GetPos':
            result, pop = self.files[a(0)][1] if a(0) in self.files else 0, 0
        else:
            raise RuntimeError(f'función importada sin hacer: {dll}!{name}')
        self._returns(uc, result, pop)

    def _lstrlen(self, p):
        """Windows' lstrlenA: 0 for a pointer it can't read (it catches the
        fault), and below 0x10000 nothing is mapped in a Windows process (the
        emulator has the page of fs:0 at 0). The game's sprintf (0x64D410)
        measures, in a first pass, each conversion's argument in the order
        the conversions come, not by their %$N$: with «%$2$s ... %$1$i» it
        measures the number as a string (docs/oracle.md, «The letters' texts»)."""
        if p < 0x10000:
            return 0
        try:
            return len(self.cstring(p))
        except UcError:
            return 0

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

    def loader_last_steps(self, version):
        """What 0x549F40 does after reading the sections (0x54A1CD..0x54A2FA):
        0x5131B0 on the names object for each ship of each merchant (from the
        last one; his list from [+0xE] by the [+0x4] of each ship) and of each
        pirate (from [+0xC] of the 5 objects of [0x6DD7AC]), with the ship's
        [+0x15E], its name [+0x160], its type & 3 and the version; then
        0x5048C0 (0x517C10 for each ship, from the last one) and 0x4D60B0 on
        the trade routes (0x6DD728)."""
        ships = self.u16(SHIPS + 0xF4)

        def register(first):
            i = first
            while i < self.u16(SHIPS + 0xF4):
                ship = self.ship(i)
                nxt = self.u16(ship + 4)
                self.call(0x5131B0, [ship + 0x15E, ship + 0x160, self.u8(ship + 0xE) & 3, version], ecx=NAMES)
                i = nxt
        for m in range(self.u16(WORLD + 0xA) - 1, -1, -1):
            register(self.u16(self.merchant(m) + 0xE))
        for k in range(5):
            pirate = self.u32(SHIPS + 0xC + 4 * k)
            if pirate:
                register(self.u16(pirate + 0xC))
        if ships and self.navdata:
            # Only with the navigation (the routes of the ships), as in p3vita
            # (Sailing::assign_loaded_routes).
            self.call(0x5048C0, [], ecx=SHIPS)
        self.call(0x4D60B0, [], ecx=0x6DD728)

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
            (INI_STRING, self._ini_string)) if a != INI_STRING or not getattr(self, 'missions', False)]
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
        if self.last_steps:
            self.loader_last_steps(version)
        self.call(LOAD_NAMES, [], ecx=NAMES)
        for h in hooks:
            self.uc.hook_del(h)

    def town_tick(self, i):
        self.call(TRAMPOLINE, [self.u16(WORLD + 4)], ecx=self.town(i))

    def cap_money(self):
        """The end of advance_time (0x53106F): no merchant above 999 999 999 coins."""
        for m in range(self.u16(WORLD + 0xA)):
            if self.s32(self.merchant(m)) > 999999999:
                self.write_u32(self.merchant(m), 999999999)

    def advance_tick(self, defer=False):
        """Un tick de advance_time (0x530E80) con la fecha y las ciudades.
        With `defer` (a long advance_time, a new game's 0xC4) the operations it
        queues stay in self.tick_operations and the battles' clock doesn't run."""
        tick = (self.u32(WORLD + 0x14) + 1) & 0xFFFFFFFF
        self.write_u32(WORLD + 0x14, tick)
        t = tick & 0xFF
        if t == 0:
            self.call(UPDATE_DATE, [], ecx=WORLD)
            self.run_rest(0x5315C0, [1 if tick % 0x700 == 0 else 0], WORLD)
            if self.statistics:
                self.call(STATISTICS_TRAMPOLINE, [], ecx=STATISTICS_OBJECT)
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
            if defer:
                return tick
            self.cap_money()
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
        if self.battles and self.u8(BATTLE_COUNT):
            # The clock rule: operation 0x94 with BATTLE_STEPS steps after the
            # tick's other operations, when there are battles.
            op = struct.pack('<5I', 0x94, BATTLE_STEPS, 0, 0, 0)
            buffer = self.alloc(OPERATION_SIZE)
            self.uc.mem_write(buffer, op)
            self.call(EXECUTOR_TRAMPOLINE, list(struct.unpack('<5I', op)) + [EXECUTOR_MAGIC], ecx=buffer)
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
        # The merchants' 11 ship lists (u16 arrays at [+0x624 + 4·k], their
        # lengths the 15 low bits of [+0x60C + 2·k]): for each one not empty,
        # u16 merchant, u8 list, u16 length and its u16s (tick_probe:
        # SaveOpaque::merchant_lists). Empty in the saves.
        lists = []
        merchant_block = out['M'][1]
        for i in range(merchants):
            for k in range(11):
                n = unpack('<H', merchant_block, 0x650 * i + 0x60C + 2 * k)[0] & 0x7FFF
                pointer = unpack('<I', merchant_block, 0x650 * i + 0x624 + 4 * k)[0]
                if n and pointer:
                    lists.append(struct.pack('<HBH', i, k, n) + bytes(read(pointer, 2 * n)))
        out['ML'] = (0, b''.join(lists))
        # Each merchant's route by points on the sea map (operation 0x0B: the heap buffer [+0xEC],
        # SavedMerchant::route_points): u16 merchant, u16 its size, u16 the u16s used and the points from the u16 4 on
        # (the 2 and 3, the ship's place, are written only when it sails, the heap's leftovers before: checked through
        # its route, Q).
        buffers = []
        for i in range(merchants):
            pointer = unpack('<I', merchant_block, 0x650 * i + 0xEC)[0]
            if pointer:
                size, used = unpack('<HH', read(pointer, 4))
                n = max(0, min(used, size) - 4)
                buffers.append(struct.pack('<HHH', i, size, used) + bytes(read(pointer + 8, 2 * n)))
        out['MR'] = (0, b''.join(buffers))
        # The ship names the players wrote (SaveGame::player_ship_names): the
        # texts of the names object's second list, each with its 0 (empty
        # after a load; operations 0x2D and 0x2E add them).
        names = b''
        offsets = gu32(NAMES + 0xCC)
        n = (gu16(NAMES + 0xDA) - gu16(NAMES + 0xD8)) & 0xFFFF
        if offsets and n:
            end = unpack('<H', read(offsets + 2 * n, 2))[0]
            names = bytes(block(gu32(NAMES + 0xB8), end))
        out['SN'] = (0, names)
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
        # The missions (tick_probe load_missions): MS the list, MV each event 0x1B's script, MF whether the load ran
        # 0x511270. A program goes by its size (the heap's requested bytes).
        requested = self.__dict__.get('requested', {})
        manager = gu32(MISSIONS)
        ms = [struct.pack('<B', 1 if manager else 0)]
        listed = set()
        if manager:
            entries, count = unpack('<IH', read(manager + 4, 6))
            for k in range(count if entries else 0):
                program, number, count = unpack('<IHH', read(entries + 8 * k, 8))
                ms.append(struct.pack('<HHI', number, count, requested.get(program, 0) if program else 0))
                listed.add(program)
        out['MS'] = (manager, b''.join(ms))
        mv, mp, ev = [], [], out['E'][1]
        for i in range(len(ev) // 0x18):
            if unpack('<H', ev, 0x18 * i + 6)[0] != 0x1B:
                continue
            program, variables, _, n = unpack('<IIHH', ev, 0x18 * i + 8)
            data = bytes(read(variables, 4 * n)) if variables and n else b''
            mv.append(struct.pack('<HIBH', i, requested.get(program, 0) if program else 0, 1 if variables else 0,
                                  len(data) // 4) + data)
            size = requested.get(program, 0) if program and program not in listed else 0
            if size:
                # MP: a script's program that no entry of the list has (a retouched scenario's own:
                # retouch_unchecked.py mission_script): u16 event, u32 size and its bytes, for tick_probe to run.
                mp.append(struct.pack('<HI', i, size) + bytes(read(program, size)))
        out['MV'] = (0, b''.join(mv))
        out['MP'] = (0, b''.join(mp))
        out['MF'] = (0, struct.pack('<B', 1 if getattr(self, 'missions', False) else 0))
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
        # The heap buffer of each letter that has one: u16 letter, u32 size
        # and the bytes (tick_probe compares them with SaveGame::mail_buffers).
        letter_buffers = []
        for i in range(len(letters) // 0x10):
            kind = letters[0x10 * i + 4]
            pointer, size = unpack('<II', letters, 0x10 * i + 8)
            if kind in (0x29, 0x47, 0x63, 0x83):
                data = bytes(read(pointer, size)) if pointer and size else b''
            elif (0x3C <= kind <= 0x40 or kind == 0x71) and pointer:
                n = unpack('<I', read(pointer, 4))[0]
                data = bytes(read(size, n)) if size and n else b''
            else:
                continue
            letter_buffers.append(struct.pack('<HI', i, len(data)))
            letter_buffers.append(data)
        out['LB'] = (0, b''.join(letter_buffers))
        out['TK'] = (0, b''.join(self.__dict__.get('ticker', [])))
        if self.__dict__.get('battle_window'):
            out['WN'] = (BATTLE_WINDOW, struct.pack('<BII', self.u8(BATTLE_WINDOW + 0x48), self.u32(BATTLE_WINDOW + 0x5A4),
                                                    self.sound_variants))
        times = gu32(WORLD + 0x6C)
        out['Y'] = (times, block(times, towns * towns if times else 0))
        convoys = gu32(SHIPS + 8)
        out['V'] = (convoys, block(convoys, gu16(SHIPS + 0xF6) * 0x3C if convoys else 0))
        stops = gu32(TRADE_ROUTES + 4)  # the trade routes' stops (0xDC bytes, as many as the u16 [+0x2])
        out['AU'] = (stops, block(stops, gu16(TRADE_ROUTES + 2) * 0xDC if stops else 0))
        # A route loaded from a file (the queue object's [+0x930], 0x4D5EE0): its stops up to the first whose +0x0 is
        # 0, as 0x5492D0 walks them (SaveGame::loaded_route).
        loaded = gu32(LOADED_ROUTE)
        records = []
        while loaded and len(records) < 0x1000:
            r = bytes(read(loaded + 0xDC * len(records), 0xDC))
            records.append(r)
            if unpack('<H', r, 0)[0] == 0:
                break
        out['AR'] = (loaded, b''.join(records))
        # The offers between merchants (0x5338A0): the nodes from the head, their pointer to the next ([+0x0]) as 0.
        offers, node = [], gu32(OFFERS)
        while node and len(offers) < 0x1000:
            r = bytes(read(node, 0x14))
            offers.append(bytes(4) + r[4:])
            node = unpack('<I', r, 0)[0]
        out['OF'] = (gu32(OFFERS), b''.join(offers))
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
        graph, addresses = self.battle_graph(read, {a for a, _ in out.values() if a})
        out['BT'] = (BATTLES, graph)
        out['BA'] = (0, addresses)
        # The statistics (0x6DFC90): S0..S3 the records of the towns of its
        # lists [+0xC] (days), [+0x10] (weeks), [+0x14] (months) and [+0x18]
        # (years), newest first: u32 number and the blocks of 0x204 bytes of
        # each record. S4 the objects of the merchants ([+0x1C], by [+0x230C]):
        # the 0x2310 bytes with the links of its rings ([+0x6C] of each record
        # of 0x70) as offsets in the object and [+0x2308], [+0x230C] at 0, then
        # u32 number of years and their records (the link at 0).
        for k in range(4):
            records = []
            node = gu32(STATISTICS_OBJECT + 0xC + 4 * k)
            while node:
                count, data, node = unpack('<III', read(node, 12))
                records.append(struct.pack('<I', count))
                records.append(block(data, count * 0x204 if data else 0))
            out[f'S{k}'] = (0, b''.join(records))
        objects = []
        node = gu32(STATISTICS_OBJECT + 0x1C)
        while node:
            piece = bytearray(read(node, 0x2310))
            for first, count in ((0x8, 32), (0xE08, 24), (0x1888, 24)):
                for r in range(count):
                    at = first + 0x70 * r + 0x6C
                    link = unpack('<I', piece, at)[0]
                    piece[at:at + 4] = struct.pack('<I', link - node if node <= link < node + 0x2310 else 0)
            year = unpack('<I', piece, 0x2308)[0]
            following = unpack('<I', piece, 0x230C)[0]
            piece[0x2308:0x2310] = bytes(8)
            years = []
            while year:
                record = bytearray(read(year, 0x70))
                year = unpack('<I', record, 0x6C)[0]
                record[0x6C:0x70] = bytes(4)
                years.append(bytes(record))
            objects.append(bytes(piece) + struct.pack('<I', len(years)) + b''.join(years))
            node = following
        out['S4'] = (0, b''.join(objects))
        out['r'] = (0, struct.pack('<I', self.rand_state))
        return out

    def battle_graph(self, read, world):
        """The battles as one block graph (region BT): block 0 is the battles
        object 0x6E55D0 (BATTLES_SIZE bytes: the 255 slots, the count
        [+0x404], the timers [+0xC30], the clock [+0xC44]...), then every heap
        block reachable from it through a pointer to the block's start (not
        the world's blocks: the regions), in the order of a walk by offsets.
        Each pointer into one of these blocks is replaced by BATTLE_POINTER |
        block << 16 | offset; other pointers (the navigation's points...) stay.
        Bytes: u32 number of blocks and, for each, u32 size and the bytes. Also
        the emulator's address of each block (region BA: tick_probe's probes
        find a block by the pointer a function receives; not compared)."""
        unpack = struct.unpack_from
        sizes = self.sizes
        requested = self.__dict__.get('requested', {})
        # The blocks of numbers only of a sea battle: the pages of its bits
        # (its page lists [+0x48], [+0x4C], [+0x50]), every word a mask of bits
        # (bit 28 is the heap's first block), and the scene's arrays of bytes
        # and u16 (SCENE_NUMBER_ARRAYS: two coordinates of an item's box can
        # look like a heap pointer). As BattleGraph::bit_pages.
        lists, pages = set(), set()
        def numbers(address, data):
            if address in pages:
                return range(0, len(data), 4)
            vtable = unpack('<I', data, 0)[0] if len(data) >= 4 else 0
            return BATTLE_NUMBERS.get((len(data), vtable), ())

        blocks = [(BATTLES, read(BATTLES, BATTLES_SIZE))]
        index = {BATTLES: 0}
        i = 0
        while i < len(blocks):
            address, data = blocks[i]
            if len(data) >= 4 and (len(data), unpack('<I', data, 0)[0]) in BATTLE_BLOCKS:
                lists.update(unpack('<I', data, o)[0] for o in (0x48, 0x4C, 0x50))
                pages.update(unpack('<I', data, o)[0] for o in SCENE_NUMBER_ARRAYS)
            elif address in lists:
                pages.update(unpack(f'<{len(data) // 4}I', data))
            skip = numbers(address, data)
            for k, v in enumerate(unpack(f'<{len(data) // 4}I', data)):
                if 4 * k in skip:
                    continue
                if HEAP_BEGIN <= v < HEAP_END and v not in index and v in sizes and v not in world:
                    index[v] = len(blocks)
                    blocks.append((v, read(v, requested.get(v, sizes[v]))))
            i += 1
        spans = [(a, a + len(d), j) for j, (a, d) in enumerate(blocks)]
        parts = [struct.pack('<I', len(blocks))]
        for a, data in blocks:
            words = list(unpack(f'<{len(data) // 4}I', data))
            changed = False
            skip = numbers(a, data)
            for k, v in enumerate(words):
                if 4 * k in skip:
                    continue
                if HEAP_BEGIN <= v < HEAP_END or v == BATTLES or BATTLES < v < BATTLES + BATTLES_SIZE:
                    for lo, hi, j in spans:
                        # A block of 0 bytes (an array without items: a sea
                        # battle's [+0x664] and [+0x668] in save 7) has
                        # lo == hi: a pointer to its start is converted too.
                        if lo <= v < hi or v == lo:
                            words[k] = BATTLE_POINTER | j << 16 | (v - lo)
                            changed = True
                            break
            if changed:
                data = struct.pack(f'<{len(words)}I', *words) + data[len(words) * 4:]
            parts.append(struct.pack('<I', len(data)))
            parts.append(data)
        return b''.join(parts), struct.pack(f'<{len(blocks)}I', *(a for a, _ in blocks))


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

    def __init__(self, oracle, functions, out, limit, wide=False):
        # wide: samples «T» with 24 u32 of the stack instead of 8 («S»):
        # newgame_oracle.py, where 0x544E50 keeps each operation it runs in its frame.
        self.wide = wide
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
        stack = bytes(uc.mem_read(esp, 100 if self.wide else 36))  # the return address and 8 (24) arguments
        ret = struct.unpack_from('<I', stack)[0]
        header = (b'T' if self.wide else b'S') + struct.pack('<IIII', address, o.u32(WORLD + 0x14),
                                                             uc.reg_read(UC_X86_REG_ECX), esp) + stack[4:]
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
    p.add_argument('--no-pirates', action='store_true',
                   help='with --barcos, without the pirates: their convoys stay still and nothing attacks '
                        '(by default they move and attack, with their sea battles; see PIRATE_JUMPS)')
    p.add_argument('--pirates', action='store_true', help='on by default now; kept for older scripts')
    p.add_argument('--battles', action='store_true',
                   help='on with the pirates now (the pirates attack, 0x50BC40, and operation 0x94 steps the '
                        'battles); kept for older scripts')
    p.add_argument('--no-disband', action='store_true',
                   help='with the pirates, a convoy at its nest is not disbanded (0x514B80; NO_DISBAND_JUMP)')
    p.add_argument('--pst', metavar='FILE', help='an uncompressed .pst to read after the save (0x54DA60)')
    p.add_argument('--no-statistics', action='store_true',
                   help='without the daily statistics (0x54DE90, after 0x5315C0; regions S0..S4), which run by default')
    p.add_argument('--statistics', action='store_true', help='on by default now; kept for older scripts')
    p.add_argument('--resto', help='con --barcos, solo estas piezas del resto de advance_time (5315c0,535450,...); '
                   'por omisión, todas')
    p.add_argument('--sin-resto', action='store_true', help='con --barcos, sin el resto de advance_time')
    p.add_argument('--cpr', help='archivo de datos (p2arch0_eng.cpr) del que leer scripts/Namen*.txt')
    p.add_argument('--misiones', action='store_true',
                   help='the load runs 0x511270 and each script gets its program (missions_addon/, from --cpr): the '
                        'missions run (TownOracle missions)')
    p.add_argument('--write', metavar='OUT', help='at the end, the uncompressed .pat the game writes for the state '
                   'reached (write_oracle.py)')
    p.add_argument('--letters', choices=('connected', 'all', 'none'), default='connected',
                   help='which calls to 0x4D6530 (send a letter) run: the ones p3vita has connected (MAIL_CALLS), '
                   'all or none')
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
                   not a.no_pirates, not a.no_statistics, disband=not a.no_disband, battles=a.battles,
                   letters=a.letters, missions=a.misiones)
    o.load()
    if a.pst:
        # The .pst next to the save (uncompressed), as 0x5476E0 reads it.
        from write_oracle import read_game_statistics
        read_game_statistics(o, open(a.pst, 'rb').read())
    functions = [int(x, 16) for x in a.functions.split(',')]
    if not a.no_statistics and STATISTICS not in functions:
        # tick_probe --continuo repeats only what is recorded: with the
        # statistics, 0x54DE90 always is (it changes towns and merchants).
        functions.append(STATISTICS)
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
        print(f'save written in {a.write} (and {a.write}.pst)')


if __name__ == '__main__':
    main()
