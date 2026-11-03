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

- [x] 0x0A (in 0x535866; queued only by the interface, 0x4768B0): **a ship is deleted** (0x507FB0, `Sailing::fn_507fb0`): route, captain, list by x, owner's list, port/sea/shipyard list, convoy, free list. Not reproduced: 0x4D4DD0 (trade routes) and 0x4F5900 when the owner's 11 ship lists ([+0x60C], heap arrays) hold ships (empty in the saves). p3vita still lacks 0x507FB0 in three places it can now use it: AI ships sunk (`Sailing::sink`), ships left in a convoy that is deleted (`fn_50a3d0`) and the ship states without handler (`update_docked`). The first and the last are the default entry of the state tables in port (0x506BE5) and at sea (0x5070E5), reached through the ship states 6 to 0x11 that `sailing.cpp` does not reproduce yet (shipyard, sunk...): to connect together with those states. Scenario `desguace`: equal in saves 1, 3, 5 and 7
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
- [ ] 0x9F (0x50B250, barcos): seen: {0x9F, i < 0xFF, u16} → 0x611309 (192 bytes) on the battle object *(0x6E55D0 + 4i), which only exists during a battle: for each bit of the u16 it lowers [+0x1E94] (factor 950/1000, squared at each bit), unless [+0xAD2] = 3. Without battle objects it does nothing, as in p3vita now. To do with the battles (E)
- [x] 0xB5 (0x514000, barcos; 0x514D40), done in `cloud/battles-2` (`battle/pirate_objects.cpp`, with 0x515750 and 0x5185C0; the oracle runs 0x514000 and executes 0xB5; retouched `daily`, `leave`, `retire` of `retouch_pirates.py`: equal). Seen: {0xB5, i < 5, u16, u16} → 0x514D40 (2.4 KB with 0x515750, 0x50AAE0, 0x5062F0 and the game's generator 0x46C320..) on pirate object i ([0x6DD7AC + 4i]): **a pirate fleet leaves its lair**: it goes through the ships in the lair ([+0x12], next [+0x6]), frees captains that are no longer fit (0x4FE150, 0x5098B0), deletes captured ships (0x507FB0) with a letter and a ransom event 0x05 to their merchant, forms a convoy with the ready ones (0x5062F0) at the lair's position (StadtDaten.ini, [+0xE]) in state 0x12. Queued only by 0x514000, the pirate objects' update called from the ships update (0x506720), which neither p3vita nor the oracle runs (QUIET_SHIPS): to do together with it, in the pirates' block (E)
- [x] 0xB8 (0x542F10; queued by event 0x03 in network games): **the network games' sync checks**: it queues operations that do nothing when run (0xB9 per ship, 0xBA per town, 0xBB, 0xBC, 0xBE per merchant, 0xD4) with checksums of the state, and sets [0x6DFBF8] to 0. Scenarios `red` and `red_activa` (all kinds; with [0x6DF31C] = 1): equal in saves 1, 3, 5 and 7, also the operations it queues

Ya se reproducen: 0x00, 0x0A, 0x12, 0x13, 0x1E, 0x1F, 0x22, 0x23, 0x24, 0x27, 0x28,
0x29, 0x2C, 0x37, 0x38, 0x3B, 0x46, 0x48, 0x4B, 0x60, 0x61, 0x64, 0x65, 0x67, 0x78, 0x79,
0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x83, 0x84, 0x85, 0x86, 0x87, 0x8E y 0xB8.

## C. Llamadas desde lo reescrito a funciones que p3vita no nombra

De `callgraph_olvidadas.tsv` (70 llamadas), quitando los accesos de 16 a 32
bytes que p3vita hace en línea (0x5303B0, 0x46C330, 0x4F3630..0x4F3680,
0x621750...) y los manejadores de eventos (A y D). Revisar una a una si son
interfaz o un hueco:

- [ ] el tick del asedio (0x6281F0) → 0x62BE90, 0x62DD70, 0x62E970 y 0x58CFB0, y 0x632780 → 0x62DD70: leen la vista de la ciudad; casi seguro gráficos
- [ ] la candidatura (0x531D50) → 0x46F560 (109 KB debajo) y 0x545CB0
- [ ] el final de la partida (0x4E4A44) → 0x4220B0 (la ventana, interfaz)
- [ ] 0x4EC354 → 0x5E94B0; 0x4EAFE4 → 0x4B9650; 0x4EBF54 → 0x5333C0
- [ ] 0x5462F0 → 0x546450
- [ ] la operación 0x22 (0x53CE30) → 0x51B890 (también la operación 0x5D)
- [ ] 0x4FA1D0 → 0x503DB0 y 0x503D80; la muralla (0x522B20) → 0x503D50 y 0x503D40
- [ ] 0x528070 → 0x636920; 0x4E6A54 y 0x509A50 → 0x512B20; 0x4D6780 → 0x51AAB0; 0x509250 → 0x513720; 0x52C6C0 → 0x52DE10 y 0x52DE40; 0x508AA0 → 0x51A3A0

## D. Lo que marca el propio código

- [ ] «Por reescribir» y «Por traducir» (14; hecho ya 0x4FACD0 para los tipos de menos de 0x32): el principio del encargo de fundar una ciudad y el ataque a la guarida pirata (`alderman.cpp`), 0x4FF2D0 (el almacén del comerciante, `construction.cpp`; el comentario parece viejo: `fn_4ff2d0` ya está en `facilities.cpp`, recontar la oficina, y la usa la 0x28), 0x629C30 sobre [+0x8] y el intérprete de misiones (`events.cpp`), el ataque al barco 0x5063C0 (`events_rare.cpp`), [x] the old letters (0x4D6780, done: the keeping time by letter type and the merchant's settings; type 0x71 needs the letter's heap record, now loaded into `SaveGame::mail_records` and recorded by the oracle as region L; scenarios `fiesta_ciudad` and `cartas`: equal) y una rama sin longitud (`events_towns.cpp`), 0x4FACD0 para los tipos de menos de 0x32 (`grid.cpp`), 0x4F99B0 sin plantilla, 0x545CE0 y 0x545F50, y la elección del alcalde en la fundación (`new_town.cpp`)
- [ ] «no se reproduce» y «no se traduce» (115): casi todo avisos, cartas, ventanas y gráficos; repasar que ninguno toque el estado
- [ ] «Sin comprobar con el oráculo» (347): ramas escritas que ninguna partida recorre; probarlas con estados retocados (`tools/oracle/retouch.py`), empezando por las de A y B. Done for the 11 town-tick files (alderman, construction, new_town, facilities, offices, businesses, population, merchants, diet, grid, sieges): of their 213 marks, 188 checked with the oracle (`tools/oracle/retouch_unchecked.py`, `unchecked.sh`), 23 explained as unreachable, 1 not reached and 1 where the game fails ([cloud/unchecked-branches.md](cloud/unchecked-branches.md)). 250 are left, in events*, operations, the ships and the convoys

## E. Subsistemas aparte

- [ ] **Batallas y encuentros** (0x5FBFE0, 0x6130B0, 0x611D80, 0x60234C, 0x50B250, 0x5053C0, 0x50BA80; con la interfaz, cerca de 1 MB): las persecuciones, los ataques de los piratas a barcos y ciudades, la guarida pirata. Encolan las operaciones 0x92–0xA0 y 0xB4. Ya en el roadmap («Piratas, persecuciones, encuentros y batallas»). Studied in [cloud/battles.md](cloud/battles.md): without the interface the battles are 249 functions (148 KB) at sea and 41 (50 KB) in towns, not 1 MB; they are stepped by operation 0x94 from the real-time driver (0x546620 → 0x612DB0), not by advance_time (0x6127C0 only counts timers); 0x9C–0x9E are unused. 0x5053C0 is **the storms**, not encounters: done (`battle/storms.cpp`, on by default). The plan, in 7 pieces, in the roadmap. From part D, the attack on a ship 0x5063C0 goes with piece 2 (pirates moving) and the attack on the pirate nest (`alderman.cpp`) and 0x515C00 with piece 6 (the town battle).
  - Done in `cloud/battles-2` ([cloud/battles-2.md](cloud/battles-2.md)), on by default since 2026-11-01: the pirates' pool and nest map as game memory (`SaveGame::pirate_nests`); 0x514B80, 0x515DA0, 0x513BF0, 0x514000, 0x513EC0, 0x514D40 (operation 0xB5), 0x515750, 0x5185C0, 0x5062F0, 0x517170, the states 0x11 and 0x12 of 0x506720 (0x506AF2, 0x507022, 0x507099), the pirate branch of 0x5017F0, and 0x5063C0, 0x513D80, 0x515EA0 (the attack of event 0x1E, connected on 2026-11-01 in `events_rare.cpp`, states 2 and 3 of 0x4DFD44, with the pirates on; scenario `event_attack` of `retouch_pirates.py`, saves 1, 3, 5: equal).
  - The automatic sea battle: 0x50BC40, 0x611CD0, 0x603A70, 0x618F10, 0x605420 (automatic path), 0x608230, 0x6079D0, 0x60F479, 0x619450, 0x61A1D0, 0x6197A0, 0x61A4E0, 0x62125B, 0x61A750, 0x503AC0, 0x50CFB0, 0x5176C0, 0x505B60, operations 0x94 and 0x96, 0x612C90, 0x612D20; the step (0x606CC0, method +0x14) and what it calls (0x6113C9, 0x6197F0, 0x608850, 0x61A9E0 with 0x6264C0, 0x61F1BF, 0x6271D7...); the end (0x60D299, 0x611740 → 0x603BC0, 0x50BFD0, 0x60C1C9). The save: 0x611AE0, 0x604600, 0x61A880, 0x6263D0 (reading) and 0x611950, 0x604220, 0x61A790, 0x626310 (writing), for sea battles.
  - A battle of the player's ship (2026-11-03): operation 0x96 executed (0x60FEC9, `Sailing::fn_60fec9`: a merchant's sides to the computer or back), the setup's path of a player busy with another battle (0x606BAE, without the battle window), the boarding's message to the player (0x60C3D6..0x60CA6F) with the game's texts, and the names 0x512A60 and 0x512AE0 (`game_first_name`, `game_family_name`). See [oracle.md](oracle.md), «A battle of the player's ship».
  - Left: cannon balls and the human side (part 7: the battle window and the question, the setup's message to the player 0x60659B..0x606B00, the end's messages 0x60E03B..0x60E8CE), rocks and islets as obstacles, 0x608550, 0x6132C0, the collisions' AC8 = 0 branches, operation 0x9F, the chase 0x50B250 and the Hanse fleet 0x50BA80 (piece 4), town battles and the nest (piece 6)
- [ ] **Guiones de los textos y misiones**: 0x511270 interpreta los textos del juego y, con el comando 0xFD, programa eventos 0x1B con un guion que ejecuta 0x4ECF64 (el intérprete). Se arranca desde la interfaz (la operación 0xC0 y otros dos caminos), así que va con la interfaz; en las partidas los 0x1B llegan sin guion. El oráculo salta 0x511270 al cargar
- [ ] **La vista de la ciudad y los gráficos** (0x599040 y los métodos de *0x6E51AC, 0x62C730 en el asedio...): ver el roadmap, fase 2, «La ciudad viva en pantalla»
- [ ] **Las acciones del jugador**: el resto de los 193 códigos de operación los encola la interfaz (comprar, vender, construir, contratar...). Van con la interfaz (fase 5)

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
- [ ] **The 97 calls in functions p3vita has rewritten** (today skipped,
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
  plus the 2 of the Alderman's notice 0x5299F0, rewritten now).
  18 left.
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
  the mission interpreter: 0x4D69D0 (operation 0x6E, read), 0x4D6A50 (0x6F,
  0x75 and event 0x13, delete), 0x4D6AF0 (0x70, the settings), 0x4D7960
  (0x71, repair), 0x4D74C0 (0x55), 0x4D7340 (0x54), 0x4D7870 (0x76),
  0x4D7900 (0x52), 0x4D7030, 0x4D7720, 0x4D7770, 0x4D75D0. Dead code:
  0x4D6930, 0x4D7140, 0x4D7200, 0x4D73F0.
- [ ] **The interface**: the list (0x47D570, titles 0x6A52D0), each
  letter's text (0x47F3B0, a switch on the 134 types, about 28 KB), the
  chronicle (0x47DD40, the −2 list), the answers (0x4D6C90: operations
  0x60, 0x72, 0x74, 0x75, 0x77, 0x8C). With the game's texts.
- [ ] **The other channel, 0x548CA0** (60 calls, 47 in rewritten
  functions): the ticker lines for the local player («%s interrupts trade
  journey.», «Your ship %s has sunk.»...), composed in 0x5483D0 by kind
  (0..0x2A). It doesn't touch the simulation (only the real-time object
  0x6DF2F0 and the interface); the oracle silences it.

## Orden propuesto

1. A: los eventos que p3vita ya crea (empezando por la dieta, 0x4E09D4).
2. B: las operaciones de la simulación, con los eventos que crean.
3. C y los «Por reescribir» de D.
4. Las ramas «Sin comprobar» de lo anterior, con estados retocados.
5. E, cada subsistema cuando toque su parte del juego.
