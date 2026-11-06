# Inventario de la simulación

Qué le falta a p3vita para hacer todo lo que hace la simulación del juego,
sacado del ejecutable y no de lo que sale en las partidas. Hasta ahora se
reescribía lo que las partidas y los estados retocados recorren (cada
función entera, con las ramas que no salen marcadas «Sin comprobar con el
oráculo»); pero había tipos de evento, operaciones y funciones enteras a las
que no se llegaba, y p3vita los saltaba en silencio.

## Cómo se ha hecho

`re/emu/sesion5/callgraph.py` (fuera del repositorio, Python de Windows) lee
el desensamblado entero (`re/save_re/p3.asm`) y las funciones de p3vita
(`fn_XXXXXX` definidas en `src/`, «reescritas»; direcciones en comentarios,
«nombradas»), y escribe en `re/emu/sesion5/inventario/`:

| Fichero | Qué |
|-|-|
| `callgraph.tsv` | las funciones alcanzables desde advance_time (0x530E80) y el ejecutor de operaciones (0x535760): tamaño, quién las llama, si p3vita las tiene |
| `callgraph_ops.tsv` | los 193 códigos de operación: su manejador, a qué llama y cuánto hay debajo sin reescribir |
| `callgraph_encolar.tsv` | las 363 llamadas a 0x54AA70 (encolar): la función, si se llega a ella desde advance_time y por dónde, y el código (adivinado; los dudosos, leídos a mano o de los recuentos del oráculo) |
| `callgraph_eventos.tsv` | los 58 tipos de evento: su manejador y quién los crea (0x4D8CF0 y el tipo en [+6]) |
| `callgraph_olvidadas.tsv` | las llamadas desde lo que p3vita ya tiene a funciones que p3vita ni nombra |
| `callgraph_puertas.tsv`, `_pendiente.tsv` | las ramas sin reescribir por tamaño (con interfaz que se cuela: orientativo) |

El grafo es estático y heurístico: no sigue las llamadas indirectas (tablas
de métodos) y la poda de la interfaz (llamadas con `ecx` de un objeto de la
interfaz: *0x6E51AC, *0x6CC7E8, *0x6CBB40, *0x6CBDD8, *0x6E55D0, y una
lista) deja pasar cosas. Lo de abajo está revisado a mano.

## A. Eventos que p3vita crea y no atiende

Huecos reales: el código de p3vita los crea como el juego, pero al vencer
el despachador los quita sin hacer lo suyo. No ha salido en las
comparaciones porque tardan en vencer o porque su efecto tarda.

- [x] 0x23, 0x24 y 0x34 (0x4E09D4): las misiones que aprueba la dieta de la Hansa (los crea el evento 0x21), con la flota de la Hansa (0x50AC40: barcos de guerra nuevos de los comerciantes de la IA): la caza de piratas (0x34), la protección de una ciudad (0x24) y los encargos a los jugadores humanos de una ciudad (0x23, la operación 0x81). `town_tick/diet.cpp`. Escenario `dieta` de `retouch.py` (partidas 1, 3, 5 y 7, días 1 a 12 y 36 a 52): las 11 fases iguales al juego. La lista de avisos del 0x34 ([+0x8]) y su carta final no se reproducen
- [x] 0x25 (0x4E1B84) y 0x26 (0x4E1E54): los incendios del asedio (los crea 0x6281F0 al caer un objetivo y al llegar un grupo): el edificio arde (100 de vida, 51 o 1 cada 5 ticks) y al final una torre queda en ruinas (0x36) y lo demás se borra (0x521240, con 0x4FACD0 ya para todos los tipos; 0x4DB100 lo quita de las subastas). Los gráficos solo con la vista de la ciudad. Escenario `incendio` (partidas 3 a 7) y los asedios de verdad de las partidas 3, 4, 5 y 7 (el primer incendio, el día 13 de la 7), iguales. Falta la guarida pirata (ciudad 0xFE − k, 0x515C00), que llega desde las batallas
- [x] 0x27 (0x4DDC00): un capitán de la IA que se retira (lo crea el evento 0x03): deja el barco (0x5098B0) según el estado de este. Escenario `capitanes` de `retouch.py`: iguales en las partidas 1 a 7
- [x] 0x31 (0x4EAB94): el impuesto extraordinario que aprueba el consejo de una ciudad (lo crea el evento 0x2E): lo pagan los comerciantes con oficina, con deudas si no pueden, y puede haber queja a la Hansa (evento 0x30). Escenario `impuesto`: el estado igual en las 7; en 2 de ellas la IA de un comerciante endeudado elige después distinto por un fallo del juego (una variable sin inicializar en 0x4F5A20; ver el roadmap)
- [x] 0x38 (0x4ECB14): el monumento al alcalde reelegido (lo crea 0x528E60): el edificio 0x2D de la ciudad pasa a ser suyo cuando la muralla está. Escenario `monumento`: iguales en las 7

## B. Operaciones que encola la simulación y p3vita descarta

Las que encolan funciones a las que se llega desde advance_time pasando
solo por lo que ya tiene p3vita. Casi todas son de unos cientos de bytes a
3 KB.

- [x] 0x0A (in 0x535866; queued only by the interface, 0x4768B0): **a ship is deleted** (0x507FB0, `Sailing::fn_507fb0`): route, captain, list by x, owner's list, port/sea/shipyard list, convoy, free list. Not reproduced: 0x4D4DD0 (trade routes). The owner's 11 ship lists ([+0x60C], heap arrays, empty in the saves): 0x4F5900 since 2026-11-04 (scenario `player_pirate_lists`). p3vita still lacks 0x507FB0 in three places it can now use it: AI ships sunk (`Sailing::sink`), ships left in a convoy that is deleted (`fn_50a3d0`) and the ship states without handler (`update_docked`). The first and the last are the default entry of the state tables in port (0x506BE5) and at sea (0x5070E5), reached through the ship states 6 to 0x11 that `sailing.cpp` does not reproduce yet (shipyard, sunk...): to connect together with those states. Scenario `desguace`: equal in saves 1, 3, 5 and 7
- [x] 0x13 (0x538C40; queued by 0x4DCEA0 in event 0x03): **the captain of a human player's ship retires**: it creates event 0x27 (already handled) on the next tick and sets the captain's [+0xE] to 1 (for AI ships, 0x4DCEA0 creates the event directly). It does *not* create event 0x28, as the call graph said: event 0x28 (0x4DDF40) is created by 0x538DA0, which sits right after 0x538C40 and is never called (no direct call, no address in the data): event 0x28 has no reachable creator, like 0x0B and 0x29. Scenario `capitan_jugador`: equal in saves 1, 3, 5 and 7
- [x] 0x28 (0x53D190, con 0x4DAB00; la encola la IA de un comerciante con deudas, 0x4F5A20, que casi nunca lo hace por el fallo de la variable sin inicializar): **un comerciante pone en venta un edificio**: lo busca por su celda en las listas de su oficina y de la ciudad y abre o amplía una subasta (evento 0x36: en fase 1 con el byte [+0xD] de los ajustes a 0, en fase 2, o nueva en un día sin subastas cerca); si no lo encuentra, recuenta la oficina (0x4FF2D0). `operations.cpp` y `events.cpp`. Escenarios `subasta` y `subasta_ajuste` (las tres maneras, sin el bit de la ciudad, una celda vacía): iguales en las partidas 3, 5 y 7, también 30 días hasta que se venden
- [x] 0x2C (en 0x535C70; 0x52A360 sobre la ciudad): **un barco encargado al astillero**. La encola el evento 0x2B (0x4DE790) para el comerciante de la IA con menos barcos cuando hay más de 400 habitantes por barco. Los materiales salen de su oficina, de sus barcos en la ciudad (también los de ruta automática, que 0x52B2C0 no cuenta) y lo que falta se compra a la ciudad; el barco nuevo queda en el astillero (0x507D60), al principio de la lista del dueño, con nombre (0x509A50; para un humano, «snaikka», «crayer», «cog» o «hulk»). `operations.cpp` y `events_towns.cpp`. Escenarios `astillero` (siete encargos: de la IA con y sin oficina, del jugador y cuatro que no se pueden) y `astillero_ciudad` (el evento 0x2B adelantado): iguales en las partidas 1, 3, 5 y 7, y 8 días después con el tick de ciudad y los barcos (el barco en obras)
- [x] 0x38 (en 0x535E3B; la encola 0x4DD400): **una subasta de barco**, el evento 0x0A en fase 0 en una ciudad, si es al menos 40 días más tarde. Escenario `eventos_op` (con 0x7C, 0x7F, 0x80 y 0x83): iguales en las partidas 1, 3, 5 y 7
- [x] 0x48 (0x53B010; la encolan las obras, 0x51E070 y 0x51E270): **una propuesta para una ciudad**: con consejo, la votación (evento 0x2E, en el tick que ajusta 0x4D9FC0); sin consejo se aplica ya (el impuesto extraordinario, evento 0x31, o el impuesto por cabeza, 0x52A150). Escenario `propuestas` (con 0x84 y 0x67): iguales en las partidas 1 a 7
- [x] 0x60 (0x53DA90) and event 0x15 (0x4E7C74): **a merchant's wedding**. The operation creates the event on the given day and clears the marks of the merchant's letters (0x4D6FD0); the event records the bride in the merchant ([+0x27..+0x33]), gives one or two dowry ships by the size of his fleet (0x50E000: a ship given in a town, with cargo) and becomes the wedding feast (event 0x07). Only the mission interpreter (0x4D6C90) and the interface queue it: the branch of event 0x16 that would queue it for the AI is dead code (`mov edx,0x4F3690; test edx,edx; jne`). `operations.cpp`, `events_towns.cpp`. Scenario `boda`: equal in saves 1, 3, 5 and 7, also the ships 2 days later
- [x] 0x61 (0x53DBF0) y el evento 0x07 (0x4E23A4): **la fiesta de un comerciante en una ciudad**. La encola la fiesta de la ciudad (0x5316E0, en el evento 0x03) para un comerciante de la IA al azar de los de sus oficinas, a los 10 a 30 días; la operación cobra el precio (0 desde la simulación; compara sin signo: con deudas, también) y crea el evento. Al vencer, para la IA la ciudad apunta la fiesta ([0x2D0], que cuenta en la satisfacción); para un humano (la fiesta que da el jugador desde la interfaz), la oficina gasta lo de la fiesta según los invitados (0x5004C0) y su reputación sube (0x4F8EB0). `operations.cpp` y `events_towns.cpp`. Escenarios `fiesta`, `fiesta_vacia` y `fiesta_ciudad` (32 días, la 0x61 que encola 0x5316E0 y su evento): iguales en las partidas 1, 3, 5 y 7
- [x] 0x65 (0x53DD70 → 0x4F5D80; queued by the AI of a merchant under −50000 who cannot borrow, 0x4F4E50): **an AI merchant goes bankrupt** and becomes a new merchant: his debts go to the town or to nobody, his statistics and reputations restart (0x4F72C0), 1000000 coins, a new life (0x4F68D0), a new first name and surname, and he stops being mayor; ships, offices and buildings stay. Game bug reproduced: the surname is taken modulo the surname index of the last merchant, not the number of surnames (in the roadmap). The graph records (0x54E1F0/0x54E170) are not reproduced. `merchants.cpp`. Scenario `quiebra`: equal in saves 1, 3, 5 and 7, also the merchants' ticks after it
- [x] 0x67 (0x53DDF0; la encola 0x4DCEA0): **un administrador mejora** su habilidad y su sueldo (0x4FE160), si sigue en la oficina. `propuestas` (partidas 4, 5 y 6, las que tienen una oficina con administrador)
- [x] 0x7C (en 0x536642; la encola 0x4DCA90): **la peste**, el evento 0x1C el día del año pedido (o al empezar el siguiente, si ya ha pasado). `eventos_op`
- [x] 0x7D (en 0x5366A0; la encola 0x4DCBF0) y el evento 0x1D (0x4E9564): **un incendio en una ciudad**: cada 64 ticks el fuego pasa al edificio inflamable más cercano (de la ciudad o de las oficinas) hasta un número; lo que arde pierde vida cada 8 ticks y se borra (0x521240); la ciudad lleva el bit 0x8000 mientras dura. Si arde un almacén, la oficina pierde mercancía (0x500C30). `events_towns.cpp`. Escenario `incendio_ciudad` (en un almacén, una casa y un edificio de la ciudad): iguales en las partidas 1, 3, 5 y 7
- [x] 0x7E (0x53F8B0; la encola el evento 0x1F al acabar la temporada): **la temporada de piratas que viene**, el evento 0x1F el 1 de marzo con las ciudades sin el bit 0x40 barajadas. Escenario `temporada`: iguales en las partidas 1, 3, 5 y 7
- [x] 0x7F (en 0x5366E8; la encola el evento 0x0D, 0x4E3F44): hasta tres ciudades con el bit 0x4000000 de [0x2C8] y un evento 0x35 cada una, que se lo quita. `eventos_op`
- [x] 0x80 (en 0x536756; la encola el evento 0x05 en partidas en red): dos campos del registro 0x80 de la cola, si es un encargo (tipo 5). Fallo del juego: la operación lleva en [+4] el registro que toca y el manejador usa siempre el 0x80 (en el roadmap). `eventos_op`
- [x] 0x83 (en 0x5367B5; la encola el evento 0x00 en partidas en red): un evento 0x00 (grano) con sus datos. `eventos_op`
- [x] 0x84 (0x53FCA0; la encolan el torneo, 0x4DD530, y la temporada de piratas, evento 0x1F): **el encargo del jefe de la Hansa**, el evento 0x30. `propuestas`
- [x] 0x87 (0x53FEA0): **a child is born to a married merchant** (279 days after the wedding and after the previous child): a 16-byte record from the captains' list (0x5097C0) at the head of his children's list [+0x34], [+0x36] + 1. Scenario `hijo`: equal in saves 1, 3, 5 and 7. Its enqueuer, 0x4F69B0 (event 0x03), is now rewritten too: up to (length of the father's first name % 3) + 2 children; the first names are now in `SaveGame::first_names` (split as the game does, 0x5121C0) and their length is 0x512A60. Scenario `hijo_natural`: equal in saves 1, 3, 5 and 7 (the names written by the players, in the 20 slots after each list, only with `--carga`: tick_probe per call does not have them)
- [x] 0x9F (in 0x536CD5; queued by the chase 0x50B250 with the number of ships that join, which p3vita doesn't have yet): {0x9F, i < 0xFF, u16} → 0x611309 (192 bytes) on the sea battle *(0x6E55D0 + 4i), if there is one: unless [+0xAD2] = 3, its music level [+0x1E94] goes down as at a ship's end once per bit set in the u16 (factor 950/1000 for bit 0, each next the square of the one before). Game bug, unreachable in practice: the loop compares the bit in 16 bits, so with bit 15 set it never ends. `battle/battle_end.cpp` `fn_611309`. Scenario `retouch_unchecked.py battle_join` (on the battle saves 6, 7 and tut4 start with: 1, 0x2D, 0x7FFF and 0 ships, a slot without battle, slot 0xFF, [+0xAD2] = 3): equal, also the save written after it
- [x] 0xB5 (0x514000, barcos; 0x514D40), done in `cloud/battles-2` (`battle/pirate_objects.cpp`, with 0x515750 and 0x5185C0; the oracle runs 0x514000 and executes 0xB5; retouched `daily`, `leave`, `retire` of `retouch_pirates.py`: equal). Seen: {0xB5, i < 5, u16, u16} → 0x514D40 (2.4 KB with 0x515750, 0x50AAE0, 0x5062F0 and the game's generator 0x46C320..) on pirate object i ([0x6DD7AC + 4i]): **a pirate fleet leaves its lair**: it goes through the ships in the lair ([+0x12], next [+0x6]), frees captains that are no longer fit (0x4FE150, 0x5098B0), deletes captured ships (0x507FB0) with a letter and a ransom event 0x05 to their merchant, forms a convoy with the ready ones (0x5062F0) at the lair's position (StadtDaten.ini, [+0xE]) in state 0x12. Queued only by 0x514000, the pirate objects' update called from the ships update (0x506720), which neither p3vita nor the oracle runs (QUIET_SHIPS): to do together with it, in the pirates' block (E)
- [x] 0xB8 (0x542F10; queued by event 0x03 in network games): **the network games' sync checks**: it queues operations that do nothing when run (0xB9 per ship, 0xBA per town, 0xBB, 0xBC, 0xBE per merchant, 0xD4) with checksums of the state, and sets [0x6DFBF8] to 0. Scenarios `red` and `red_activa` (all kinds; with [0x6DF31C] = 1): equal in saves 1, 3, 5 and 7, also the operations it queues

Already reproduced (`run_operation`, 2026-11-04): 0x00, 0x0A, 0x12, 0x13, 0x1E, 0x1F,
0x21, 0x22, 0x23, 0x24, 0x27, 0x28, 0x29, 0x2C, 0x37, 0x38, 0x3B, 0x3F, 0x46, 0x48, 0x4B,
0x4E, 0x5D, 0x60, 0x61, 0x64, 0x65, 0x67, 0x6E, 0x6F, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D,
0x7E, 0x7F, 0x80, 0x81, 0x83, 0x84, 0x85, 0x86, 0x87, 0x8E, 0x91, 0x94, 0x96, 0x9F, 0xB5
and 0xB8.

## C. Calls from rewritten code to functions p3vita doesn't name

From `callgraph_olvidadas.tsv` (70 calls), without the 16..32-byte accessors
p3vita does inline (0x5303B0, 0x46C330, 0x4F3630..0x4F3680, 0x621750...) and
the event handlers (A and D). **Reviewed one by one** (2026-11-04); three
were gaps, rewritten and checked (one of them a player's action):

- [x] **Gap, done:** 0x528070 → 0x636920. During a siege (bit 0x10 of
  [+0x2C8], with the siege objects in place) the price thresholds add the
  pitch the besieging army uses each tick (0x636920 on the town's siege
  object: 0x632690, defenders of types 1..3 against besiegers of type 0, · 40,
  and 0x6326E0, 400 against a gate 0x34/0x35) to pitch: t0 += 7·X, t1 += 14·X
  (`thresholds.cpp`, `ThresholdInputs::siege_pitch`; `sieges.cpp`
  `fn_636920`). Not when a save is loaded (*0x6DF270 doesn't exist yet).
  Scenario `retouch_unchecked.py siege_pitch`.
- [x] **Gap, done:** 0x53CE30 → 0x51B890, and operation 0x5D (0x5362AE) →
  0x51B890: a merchant opens an office in a town (the costs of the type 0 by
  0x51F3A0, what he lacks from the town's market, the rest from his docked
  ships by 0x4F5450, the payment, 0x5308A0 with create = 1). 0x53CE30 calls it
  only for the operation 0x21 (`request`, queued by the town view 0x590840)
  with the type 0x2C; 0x21 maps any other type through the table 0x672FC0.
  Nothing in the exe writes the code 0x5D before queuing: only the interface
  could. Both operations are now in `run_operation` (`construction.cpp`
  `fn_51b890`, `fn_5362ae`, `fn_53ce30` with `request`). Scenario
  `retouch_unchecked.py office_ops`.
- [x] **Gap, a player's action, done:** 0x509250 → 0x513720 is in the branch
  of 0x509250 with a winner that isn't a merchant, which p3vita didn't
  rewrite: the ship joins the pirate whose nest is nearest to the town or
  makes one in slot 0 (0x513720, 0x513D80), enters it (0x515EA0), gets a
  course to the town's first waypoint (0x5172A0) and is left without owner in
  state 2. Only the operation 0x4E (0x53B850, a player's ship turns pirate;
  queued by the window 0x5D5430) and the mission interpreter (0x4ECF64, at
  0x4F2121) call it so; the auctions (0x4E2CD4) only sell to merchants. Now
  rewritten with the operation 0x4E (`operations.cpp` `fn_53b850`,
  `pirate_objects.cpp` `fn_509250_pirates`) and 0x4F5900, the merchant's ship
  lists (`events_mayor.cpp`, also in 0x507FB0). Scenarios
  `retouch_unchecked.py player_pirate` and `player_pirate_new`.
- [x] Interface: the siege's graphics. 0x6281F0 → 0x62BE90, 0x62DD70,
  0x62E970, 0x58CFB0 and 0x632780 → 0x62DD70 run only with [+0x77] of the
  army (the town view open; 0x6284DA, 0x628792, 0x628882, 0x6329B5...;
  0x62E970 checks it itself). With it they write fields of the army
  ([+0x5A0], [+0x10], [+0x12]...): roadmap, phase 2, «El asedio en la vista».
- [x] Interface: founding a town (0x531D50) → 0x46F560 grows a table of 12
  bytes per town in an interface object (*0x6CC940, made at 0x424E2C; the
  «109 KB below» were the table's error: the function ends at 0x46F676).
  0x545CB0 copies a town (0x9F8 bytes) when the towns' array grows, as
  `towns.resize` does.
- [x] Interface: the end of the game (0x4E4A44) → 0x4220B0, on *0x6CBB30: the
  merchants' ranking of the end window (0x4220D0, with 0x4F9430 and 0x4F73D0);
  it writes only its own object. Roadmap, phase 5.
- [x] Interface: 0x4EC354 → 0x5E94B0 and 0x4EAFE4 → 0x4B9650 redraw the
  rectangle of the task window (*0x6E558C) on the screen manager 0x6DA5F0.
- [x] Done before: 0x4EBF54 → 0x5333C0 (the letter 0x80, `alderman.cpp`; its
  other call, 0x4ED61F, is in the mission interpreter), 0x4E6A54 and 0x509A50
  → 0x512B20 (a ship's name: the letter 0x63 and `fn_509a50`), 0x4D6780 →
  0x51AAB0 (part D).
- [x] Inline in p3vita: 0x4FA1D0 → 0x503DB0, 0x503D80 and 0x522B20 → 0x503D50,
  0x503D40 (the heap list of the wall's pieces, local to 0x522B20: a
  `std::vector`); 0x52C6C0 → 0x52DE10, 0x52DE40 (a node of a world object's
  list: `world_object_lists`); 0x508AA0 → 0x51A3A0 (a ship's index from its
  address).
- [x] Not a call: 0x5462F0 → 0x546450. 0x546420, right after 0x5462F0, is the
  static initializer of the network object 0x6DF2F0 (its constructor
  0x546450; see oracle.md, «The game's static initializers»).

## D. Lo que marca el propio código

- [x] «Por reescribir» and «Por traducir» (reviewed 2026-11-04; 16 marks then): one gap, rewritten: 0x633AE0 (is a siege going on?, used by a building's sale price 0x4DAF10) answered always «no» with an army; now 0x629C30 on it (`events.cpp`; scenario `retouch_unchecked.py siege_sale`, saves 3 and 5: equal, the price halved while the army besieges). Stale, corrected: the start of the Hanse task of type 0 (0x531D50, done) and 0x514B80 (done). For the new game (roadmap, «Partida nueva desde cero»; 0x547F4F → 0x544E50, operation 0xAF with [+0xF] = 1): 0x545CE0 and 0x545F50 in 0x5458D0, the mayor's election of a Hanse town (0x532063..0x5321C5) and 0x4F99B0 without a template file (`new_town.cpp`). With the battles (E): the end by a storm or two pirates (0x606C28), the cannon balls (`battle_step.cpp`), the attack on the pirate nest (`alderman.cpp`) and the town battle 0x515C00 (`events_rare.cpp`). With the mission interpreter (E): `events.cpp` (0x4ECF64 with a program) and `diet.cpp` (its letters). A player's action: the letters 0x1E/0x1F (operation 0x5F)
- [x] «no se reproduce» and «no se traduce» (88, reviewed 2026-11-04): none changes the state without being a piece already in the roadmap. The ones that touch the state belong to the trade routes (0x4D4DD0, 0x503230, 0x518860: phase 6), the battles (town battles, the chase, cannon balls, the human side), the mission interpreter, the new game, the final letter 0x3D of event 0x34 with its notices' list [+0x8] (0x4F3980..0x4F3CE0, 0x4F3AF0; one of mail.md's 55 calls not connected) and the ship states p3vita doesn't reproduce (roadmap, part D: the tables 0x507CB0 and 0x507D14, with deleting the sunk AI ships, 0x507FB0). Stale, corrected: the bankruptcy's statistics records (done), the player's convoys' counters (done in `convoys.cpp`) and the daily statistics (done). Some notes on the ticker lines are stale too (now connected), but those are the interface's
- [x] «Sin comprobar con el oráculo» (347; closed 2026-11-05): ramas escritas que ninguna partida recorre; probarlas con estados retocados (`tools/oracle/retouch.py`), empezando por las de A y B. Done for the 11 town-tick files (alderman, construction, new_town, facilities, offices, businesses, population, merchants, diet, grid, sieges): of their 213 marks, 188 checked with the oracle (`tools/oracle/retouch_unchecked.py`, `unchecked.sh`), 23 explained as unreachable, 1 not reached and 1 where the game fails ([cloud/unchecked-branches.md](cloud/unchecked-branches.md)). 2026-11-04: `operations.cpp`, its 30 marks checked with 4 scenarios (`operation_*`); `events.cpp`, 43 (36 checked with 5 scenarios, 7 unreachable); `events_towns.cpp`, 45 (40 checked with 7 scenarios, 5 unreachable); `events_mayor.cpp`, 31 (28 checked with 4 scenarios, 3 not reproduced or unreachable); `events_rare.cpp`, 66: 62 checked with 5 scenarios, 3 unreachable and 1 where the game fails (a division by 0); one wrong in p3vita, fixed; `mail.cpp`, `events_big.cpp`, `merchants.cpp`, `ship_trade.cpp` and `ship_ai.cpp`, 17: 11 checked with 4 scenarios, 5 unreachable and 1 where the game fails; `sailing.cpp`, 6: 5 checked with 3 scenarios, 1 not reproduced (a ship's trade route, phase 6); the convoys (`convoys.cpp`, `convoys_sea.cpp`, `convoys_port.cpp`, `convoys_members.cpp`), 14: 10 checked with 2 scenarios, 3 unreachable, 1 not reproduced; the battles (`battle/`), 8: 2 checked with 1 scenario, 6 unreachable (one of them a game bug: the letters about a sunk pirate captain are never sent) ([oracle.md](oracle.md), «Part D: the unchecked branches of …»). None is left

## E. Subsistemas aparte

- [ ] **Batallas y encuentros** (0x5FBFE0, 0x6130B0, 0x611D80, 0x60234C, 0x50B250, 0x5053C0, 0x50BA80; con la interfaz, cerca de 1 MB): las persecuciones, los ataques de los piratas a barcos y ciudades, la guarida pirata. Encolan las operaciones 0x92–0xA0 y 0xB4. Ya en el roadmap («Piratas, persecuciones, encuentros y batallas»). Studied in [cloud/battles.md](cloud/battles.md): without the interface the battles are 249 functions (148 KB) at sea and 41 (50 KB) in towns, not 1 MB; they are stepped by operation 0x94 from the real-time driver (0x546620 → 0x612DB0), not by advance_time (0x6127C0 only counts timers); 0x9C–0x9E are unused. 0x5053C0 is **the storms**, not encounters: done (`battle/storms.cpp`, on by default). The plan, in 7 pieces, in the roadmap. From part D, the attack on a ship 0x5063C0 goes with piece 2 (pirates moving) and the attack on the pirate nest (`alderman.cpp`) and 0x515C00 with piece 6 (the town battle).
  - Done in `cloud/battles-2` ([cloud/battles-2.md](cloud/battles-2.md)), on by default since 2026-11-01: the pirates' pool and nest map as game memory (`SaveGame::pirate_nests`); 0x514B80, 0x515DA0, 0x513BF0, 0x514000, 0x513EC0, 0x514D40 (operation 0xB5), 0x515750, 0x5185C0, 0x5062F0, 0x517170, the states 0x11 and 0x12 of 0x506720 (0x506AF2, 0x507022, 0x507099), the pirate branch of 0x5017F0, and 0x5063C0, 0x513D80, 0x515EA0 (the attack of event 0x1E, connected on 2026-11-01 in `events_rare.cpp`, states 2 and 3 of 0x4DFD44, with the pirates on; scenario `event_attack` of `retouch_pirates.py`, saves 1, 3, 5: equal).
  - The automatic sea battle: 0x50BC40, 0x611CD0, 0x603A70, 0x618F10, 0x605420 (automatic path), 0x608230, 0x6079D0, 0x60F479, 0x619450, 0x61A1D0, 0x6197A0, 0x61A4E0, 0x62125B, 0x61A750, 0x503AC0, 0x50CFB0, 0x5176C0, 0x505B60, operations 0x94 and 0x96, 0x612C90, 0x612D20; the step (0x606CC0, method +0x14) and what it calls (0x6113C9, 0x6197F0, 0x608850, 0x61A9E0 with 0x6264C0, 0x61F1BF, 0x6271D7...); the end (0x60D299, 0x611740 → 0x603BC0, 0x50BFD0, 0x60C1C9). The save: 0x611AE0, 0x604600, 0x61A880, 0x6263D0 (reading) and 0x611950, 0x604220, 0x61A790, 0x626310 (writing), for sea battles.
  - A battle of the player's ship (2026-11-03): operation 0x96 executed (0x60FEC9, `Sailing::fn_60fec9`: a merchant's sides to the computer or back), the setup's path of a player busy with another battle (0x606BAE, without the battle window), the boarding's message to the player (0x60C3D6..0x60CA6F) with the game's texts, and the names 0x512A60 and 0x512AE0 (`game_first_name`, `game_family_name`). See [oracle.md](oracle.md), «A battle of the player's ship».
  - Left: cannon balls and the human side (part 7: the battle window and the question, the setup's message to the player 0x60659B..0x606B00, the end's messages 0x60E03B..0x60E8CE), rocks and islets as obstacles, 0x608550, 0x6132C0, the collisions' AC8 = 0 branches, operation 0x9F, the chase 0x50B250 and the Hanse fleet 0x50BA80 (piece 4), town battles and the nest (piece 6)
- [ ] **Guiones de los textos y misiones**: 0x511270 interpreta los textos del juego y, con el comando 0xFD, programa eventos 0x1B con un guion que ejecuta 0x4ECF64 (el intérprete). Se arranca desde la interfaz (la operación 0xC0 y otros dos caminos), así que va con la interfaz; en las partidas los 0x1B llegan sin guion. El oráculo salta 0x511270 al cargar
- [ ] **La vista de la ciudad y los gráficos** (0x599040 y los métodos de *0x6E51AC, 0x62C730 en el asedio...): ver el roadmap, fase 2, «La ciudad viva en pantalla»
- [ ] **The player's actions**: the table of the 193 operation codes, up to date (2026-11-05), is in [formats/operations.md](formats/operations.md): each code's handler, what it does, who queues it (the interface's window or the simulation's function), what is left below it and its group. 105 are the player's (queued by the interface; 43 done: 0x00..0x0A, 0x0C, 0x0D, 0x15..0x1D, 0x20, 0x21, 0x25, 0x26, 0x29..0x2B, 0x2D..0x2F, 0x3B, 0x3F, 0x4B, 0x4E, 0x58, 0x59, 0x5D, 0x6E..0x71), 37 the simulation's (all done), 14 the battles', 15 the network game's, 11 the new game's, 1 the mission interpreter's and 10 unused. Each action is rewritten with its window, towards the playable milestone (roadmap, phase 5; the user chose on 2026-11-05 four batches: the ship, the office, the shipyard and the convoys, the loans and building). Done: buying and selling (0x02, 0x01), queued by the viewer as the market window builds them (`player_actions.hpp`) and run by `run_operation` (`trade.cpp`, [oracle.md](oracle.md), «The player's actions»); the crew, the captain and the repairs (0x04, 0x05, 0x06, 0x07, 0x03, 0x1D, `town_tick/ship_operations.cpp`), in the engine and in the viewer (2026-11-05: the tavern, the shipyard and the ship's window, entered as in the game by the town's buildings; what the windows list and offer, 0x504AC0, 0x5269A0, 0x4F6CA0 and the tavern's `rand()`, in `player_actions.cpp`, checked with the scenario `ship_windows`); the office (0x08, 0x09, 0x58, 0x59 in `trade.cpp`, scenarios `office_trade` and `office_artillery`; in the viewer, the office's kontor is drawn and opens the market's office modes; 0x5D has no window: an office is built with 0x21); the shipyard and the convoys (2026-11-05): 0x0C (upgrade, 0x508660 with 0x52B670 and 0x52AAD0), 0x2D and 0x2E (a ship's name, 0x53CCE0 with the names object's 0x512D40, 0x512FC0 and 0x512B20: the ship names the players write, `SaveGame::player_ship_names`, also written into the save as the game does) and 0x2F (cancel an order, 0x52B110) in `town_tick/ship_operations.cpp`; 0x15 (0x509B90), 0x16, 0x17 (0x539270), 0x18 (0x509D10, with 0x5039F0 and 0x4F8D20), 0x1C and 0x20 (0x539CF0) in `convoys_members.cpp` and `simulation.cpp`; 0x19, 0x1A (0x539300) and 0x1B (0x5396B0) in `trade.cpp`; what the convoy window offers (0x40C170), the market window's convoy (0x4AA4A0) and the shipyard's pages in `player_actions.cpp`; scenarios `convoy_ops`, `convoy_trade` and `shipyard_ops`. In the viewer, the shipyard's pages (L/R) and the convoys from the ship's window and the market; the moneylender (2026-11-05): 0x2A (a loan paid off, 0x539F30) and 0x2B (company shares, 0x4D98F0) in `operations.cpp`, with the event 0x18 they make (0x4E8804: the shares and their dividends each quarter) in `events_towns.cpp`; what its window offers and lists (0x51CE80, 0x51D160, 0x51D270, 0x4D9100, 0x4D9210, 0x4D9320, 0x4D9620) in `player_actions.cpp`; scenarios `lender_loans` and `lender_shares`. In the viewer, the moneylender's building opens its six pages (L/R); demolishing (0x26, 0x53CF50) and streets (0x25, 0x5244F0 with 0x4FB580) in the engine (`construction.cpp`, `grid.cpp`; scenario `build_player_ops`); the build window (2026-11-05, night): its menu (0x47AB20), whether a building fits (0x47B8C0 with 0x4FD020), where it fits (0x47B830: 0x4FD0D0, 0x4FD4A0, 0x4FD5E0, 0x5241E0), the town view's 0x21 (0x590840) and the window's 0x22, 0x24, 0x25, and the player's building's window (0x26, 0x28) in `player_actions.cpp`; scenarios `build_menus` and `build_places`. In the viewer (2026-11-05, late night) the game's side panel on the right (minimap, icons, ship buttons, the ship list) with the build menu in it and the touch placement in the town (the green marks of 0x47B830); a building of the player's opens its window (demolish, auction); and the rest of the panel (2026-11-06): the ship menu (SIDEMENU_SHIP: what it shows, 0x517F70, 0x5185C0, 0x52AAD0, 0x5187F0, 0x4901C0, in `player_actions.cpp`; «Pirate» / «Civil», the operation 0x0D, 0x5386C0, and whether it can be pressed, 0x518750; scenarios `pirate_ops` and `pirate_sailing`; the viewer leaves the button off until the battles' pieces 6 and 7), the messages (SIDEMENU_INFOS) with the letters' settings window (0x408F50: the operations 0x70, 0x4D6AF0, and 0x71, 0x4D7960, in `mail.cpp`; scenario `letter_ops`), the panel on the sea map, the game's top bar and the buttons' tooltips (the exe's string table). The statistics (SIDEMENU_STATISTIK, 2026-11-06): the company overview (0x4A2F60 the balance, 0x4A22A0 and 0x505A10 the graphs, 0x4A0F10's Suministro) in `statistics_window.cpp`, checked with the scenario `statistics_company`; and the competition (2026-11-06: 0x4A19C0 the graphs and the averages, the merchants the window picks when it opens in 0x49D380, the list of a merchant's button 0x49E850 with the list's sorts 0x4720D0 and 0x471EA0), checked with the scenario `statistics_competition`; and the town comparison (2026-11-06: 0x4A3BB0 the residents' table, 0x4A3FA0 with 0x4A2AB0 for the entries 7..10, the town buttons' lists 0x49E850 with 0x471FF0, the ware button's 0x49FC1C with 0x471F10, the start of 0x49D380), checked with the scenario `statistics_comparison`; left: the town data (0x4A3FA0 for the entries 0..5). Left of the panel: the minimap's arrow between the town and the sea, the minimap's red attackers, the top bar's own buildings and population

## F. The mailbox: letters and notices

Inventoried on 2026-11-03 ([formats/mail.md](formats/mail.md), with the
tables). The game sends a letter with 0x4D6530 (on the mailbox 0x6DD730)
from **154 calls in 84 functions**. 152 are reached from the simulation
(advance_time, the operations' executor, the battles' methods) and 2 from
an interface window (0x419EB0: letters 0x45 and 0x21 to the local player,
built with rand()). The letter is copied, with its buffers, into the chain
of each **human** merchant the player's settings let it through (classes
1..4 by type, table 0x6C0198; the merchant's bytes [+0x38..+0x3A]), or into
the chronicle (merchant −2, always). A letter to a merchant of the computer
is dropped, so in the saves only the player's chain and the chronicle
change.

- [x] **0x4D6530 itself** and what it calls: 0x4D6680 (the filter and the
  chain), 0x4D6B40 (growing by 200), 0x51AB20 / 0x51AAB0 (copy and release
  with the buffers of types 0x29, 0x47, 0x63, 0x83 and the records of
  0x3C..0x40 and 0x71). 0x4D7B10 is the interface's notification (sound and
  a line), nothing in the oracle. Done on 2026-11-03 (`src/p3data/mail.cpp`;
  the oracle runs it only from the connected calls, `MAIL_CALLS`, and
  records the buffers as region `LB`; scenarios `grow` and `promotion` of
  `tools/oracle/retouch_mail.py`). Connected: the merchant's tick 0x4F7BB0
  (0x0F, 0x10, 0x11, 0x06).
- [x] **The 97 calls in functions p3vita has rewritten** (before skipped,
  «A letter (0x4D6530): not rewritten» / «no se traduce»): `events_rare.cpp`
  21, `events_towns.cpp` 14, `events_mayor.cpp` 9, `merchants.cpp` 8,
  `battle_end.cpp` 7 (the battle report 0x29 of 0x60D299, built in
  0x60D5FA..0x60DD93, the 0x2A news, the 0x72 letters, the 0x54 ones of
  0x50BFD0), `alderman.cpp` 6, `capture.cpp` 5, `sieges.cpp` 5,
  `events_big.cpp` 4, `operations.cpp` 4, `diet.cpp` 3, `facilities.cpp` 2,
  `construction.cpp` 2, `convoys_members.cpp` 2, `core.cpp`, `offices.cpp`,
  `events.cpp`, `pirate_objects.cpp`, `ship_step.cpp`.
  Connected (2026-11-03): the merchant's tick (4: 0x0F, 0x10, 0x11, 0x06)
  and the sea battles (14: `battle_end.cpp` 7, `capture.cpp` 5,
  `pirate_objects.cpp`, `ship_step.cpp`; the report 0x29 with the game's
  texts), `merchants.cpp` (4 more: 0x5E, 0x5F, 0x6D, 0x82) and
  `events_big.cpp` (4: event 0x05 and 0x16), and the small files
  (`facilities.cpp`, `offices.cpp`, `core.cpp`, `events.cpp`,
  `construction.cpp`, `convoys_members.cpp`: 9) and `events_rare.cpp` (21: the
  pirate hunt, the plague, the council, the Hanse leader's task and meeting,
  the extra tax, the theft, the monument and the expedition, with its report
  0x63 in the game's texts) and `events_towns.cpp` (14: the debts' notices and
  the impounding, the feast, the wedding, the fires, a new office, a captain)
  and `events_mayor.cpp` (9, the elections, the Hanse vote and the auctions,
  plus the 2 of the Alderman's notice 0x5299F0, rewritten now) and
  `alderman.cpp` (6: a Hanse task started, expired and done) and
  `sieges.cpp` (4: the news 0x38 and 0x53, the prince's payment 0x85; the
  fifth, 0x6A of 0x633DB0, is in its convoy branch, reached only from the
  battle in a town 0x5FEAC9, so it goes with that battle) and
  `operations.cpp` (4: 0x52, 0x33, 0x7C, 0x7A) and `diet.cpp` (3: the
  Hanse's missions, 0x48, 0x4C, 0x5C). **All 97 done** but that 0x6A.
- [ ] **10 in functions p3vita names but skipped whole** (8 left): the mission
  interpreter 0x4ECF64 (5; three with a letter the script builds), the
  alderman's notification 0x5299F0 (2, done with events_mayor.cpp), the convoy's automatic route
  0x503230, the sinking message 0x519D90, 0x4F3AF0.
- [ ] **47 in events and operations p3vita doesn't have**: events 0x09,
  0x0C, 0x0F, 0x10, 0x11, 0x17, 0x18 and 0x28 (created only by player
  actions or the mission interpreter), the player's operations 0x03, 0x06,
  0x2F, 0x3D, 0x3E, 0x4A, 0x53, 0x5F, 0x72, 0x77, 0x8B, 0x8C, 0x8F, 0x90,
  the town battle (0x5FE7E0, 3), a new game (0x530470) and the end of a
  trade route (0x4D5200). They go with those subsystems (E).
- [ ] **The other methods** the simulation uses: 0x4D6FD0 (done, operation
  0x60), 0x4D6780 (done, the old letters), 0x4D7AC0 (the 0x5F check of
  0x4F7510), 0x4D64B0 (reset), and the ones of the player's operations and
  the mission interpreter: 0x4D69D0 (operation 0x6E, read: done, the
  viewer's mailbox), 0x4D6A50 (0x6F, done; 0x75 and event 0x13 use it too,
  still to come), 0x4D6AF0 (0x70, the settings: done), 0x4D7960
  (0x71, repair: done), 0x4D74C0 (0x55), 0x4D7340 (0x54), 0x4D7870 (0x76),
  0x4D7900 (0x52), 0x4D7030, 0x4D7720, 0x4D7770, 0x4D75D0. Dead code:
  0x4D6930, 0x4D7140, 0x4D7200, 0x4D73F0.
- [ ] **The interface**: done on 2026-11-03 with the game's texts and
  checked against the game in both languages: the list (0x47C520, 0x47D570,
  titles 0x6A52D0, tabs 0x6B2DD8), each letter's text (0x47F3B0, all the
  types but the offers 0x1E/0x1F; with the picture of 0x46 and 0x6E,
  0x51AC60, since 2026-11-04), the
  chronicle (0x47DD40, the −2 list), the new letter's line (0x4D7B10) and the
  text box's codes (0x462540), in the viewer's mailbox. Left: the answers
  (0x4D6C90: operations 0x60, 0x72, 0x74, 0x75, 0x77, 0x8C, the letters'
  buttons), with the player's actions.
- [x] **The other channel, 0x548CA0** (60 calls, 47 in rewritten
  functions): the ticker lines for the local player («%s interrupts trade
  journey.», «Your ship %s has sunk.»...), composed in 0x5483D0 by kind
  (0..0x2A). It doesn't touch the simulation (only the real-time object
  0x6DF2F0 and the interface). Done on 2026-11-04 ([formats/mail.md](formats/mail.md)
  §6): the composer (`compose_ticker`, checked in both languages) and every
  call with a line in rewritten code (`send_ticker`); the oracle records the
  player's as region `TK`. Left with their functions: 0x518860, 0x503230,
  0x4D5200, 0x508E69, 0x5FBFE0, 0x4E3EC2, 0x4E7C3D, the mission interpreter.

## Orden propuesto

1. A: los eventos que p3vita ya crea (empezando por la dieta, 0x4E09D4).
2. B: las operaciones de la simulación, con los eventos que crean.
3. C y los «Por reescribir» de D.
4. Las ramas «Sin comprobar» de lo anterior, con estados retocados.
5. E, cada subsistema cuando toque su parte del juego.
