# Hoja de ruta

p3vita reescribe en C++ el motor de Patrician III (Ascaron, 2003) para la PS Vita, leyendo los datos de una copia del
juego (la versión GOG 1.1, la que documenta [P3Modding](https://p3modding.github.io/)). No hay código fuente del
original: cada función se lee en el ejecutable y se reescribe.

**El motor respeta fielmente el modo original.** Todo lo que decide qué pasa en la partida (la simulación y las órdenes
que encola la interfaz) se reescribe idéntico al juego, byte a byte, y se comprueba contra el propio juego ejecutado en
un emulador (el oráculo, [oracle.md](oracle.md)). Así p3vita carga y guarda las partidas del original y, al final de
todo, podrá jugar en red con él (apartado 8). Los fallos del juego se reproducen tal cual y se corrigen aparte, detrás
de un ajuste que se puede apagar (apartado 7). Lo que se ve, en cambio, se adapta a la pantalla de la Vita y se
comprueba a ojo, con los gráficos y los textos del juego.

Cómo leerla:
- Arriba: dónde estamos, cuánto falta (medido por partes) y la lista de lo que falta, con lo necesario para empezar
  cada cosa. Abajo: lo hecho, resumido, las decisiones del usuario que siguen valiendo y los fallos del juego
  encontrados.
- El detalle de lo hecho está en los documentos: [oracle.md](oracle.md) (cada comprobación, por tema), [formats/](formats/)
  (los formatos y los sistemas del juego), [inventario.md](inventario.md) (la lista cerrada de lo que hace la simulación,
  sacada del ejecutable), [formats/operations.md](formats/operations.md) (los 193 códigos de operación),
  [formats/mail.md](formats/mail.md) (las cartas) y [cloud/](cloud/) (las notas de las sesiones en la nube, ya
  terminadas). Las notas y los scripts de cada sesión, fuera del repositorio, en `re/emu/sesionN/` (con su README).
- `[ ]` pendiente; `[x]` hecho y comprobado. Este documento se queda en español: lo lee el usuario.

## Dónde estamos (13-11-2026)

- **El motor**: todo lo que hace una partida por sí sola está reescrito e idéntico al juego: las ciudades, los
  comerciantes, los barcos y los convoyes de la IA, la cola de eventos, los asedios, las tormentas, los piratas, todas
  las batallas (en el mar y en una ciudad, automáticas y dirigidas por el jugador), el camino a Gobernador, el buzón,
  las estadísticas, cargar y guardar partidas, la partida nueva y las rutas comerciales automáticas. Comprobado: el
  mundo entero 60 días en las 12 partidas de prueba sin un byte distinto, y más de cien escenarios con estados
  retocados para las ramas a las que las partidas no llegan.
- **El visor** (lo que se juega en la Vita): la pantalla de inicio y la partida nueva; la ciudad con el panel lateral
  del juego; el mapa del mar; el mercado, la taberna, el astillero y el muelle, la oficina y el prestamista; construir
  y derribar tocando la ciudad; el menú del barco, con las rutas comerciales y la ventana de dos barcos; el gremio, la
  herrería, la iglesia, el baño, la oficina de otro comerciante y la caballeriza (el transporte por tierra); el buzón;
  las estadísticas; el panel de la Hansa; las batallas que dirige el jugador; guardar y cargar; las opciones (idioma,
  corregir los fallos del juego).
- **Lo último** (13-11-2026): acabado el grupo 4 del encargo. En el visor, el soplón de la taberna (su noticia, que
  prepara la ciudad, 0x526B40, con el texto del rumor; la 0x4C, nueva en el motor) y la ventana del transporte por
  tierra en la caballeriza (`land_trade_window.cpp`: las mercancías de la oficina a los carros, la escolta y su coste,
  el sorteo de la emboscada y su pérdida repartida por mercancías, como el juego; escenario `land_trade_window`, ctest
  `land_trade_window`). Antes, el ladrón y el pirata de la taberna (y los botones de acción de las
  ventanas de lugar ocupan todo el ancho cuando no hay − y +; tocar «○ cerrar» cierra). Antes, la taberna en el visor:
  una fila por persona presente (quién está, como el juego; hablar
  con una encola la 0x52 como su menú y salir de la taberna la de tipo 10), el comerciante (su oferta, «Aceptar
  oferta», a qué barco u oficina y «Cargar mercancías»), el traficante de armas (sus precios, el paso 1/5/50/todo,
  comprar y vender) y el viajero (su encargo con fecha, el barco, «Aceptar encargo»); `tavern_window.cpp`, escenario
  `tavern_window` (46 objetivos), ctest `tavern_window`. Corregido detrás del ajuste, a petición del usuario, el fallo
  del transporte por tierra con la oficina corta. Antes, el grupo 4 en el motor: las personas de la taberna (0x52: quién habla con quién, con
  sus encargos), el viajero (0x53 y su evento 0x10), el ladrón (0x4F y su evento 0x0F, con el robo), el transporte por
  tierra (0x3A y su evento 0x0C), la entrega de un pedido del ayuntamiento (0x51), la candidatura a alcalde (0x45), las
  herramientas de los desarrolladores de la ventana de encargos de la Hansa (0x4A) y los grupos de barcos de otro
  jugador (0xBF, para la partida en red): 13 cartas más conectadas. Antes, el resto del grupo 3 en el visor: la herrería (artillería y armas de mano, con la 0x57
  nueva: la prioridad del alcalde), la ventana de dos barcos (capitanes, marineros, armas de mano), el objetivo de un
  barco en el mar (tocar el barco), las existencias a guardar del administrador y un segundo botón táctil (Cuadrado)
  en las ventanas; las cartas de las ofertas comparadas con las del juego; y, por decisión del usuario, corregido con
  el ajuste el fallo de las mercancías de las ofertas. Antes, el mismo día, la segunda parte del encargo de las
  acciones del jugador, su grupo 3 en el motor: las subastas del
  gremio (pujar, avisarme, subastar un barco, la puja automática con su evento 0x2D) con la ventana del gremio en el
  visor; la oficina (el almacén con los guardias, el administrador y sus órdenes por mercancía) y, en la ventana de un
  edificio propio, los trabajadores de una empresa y el alquiler de una casa; en el motor también la herrería (armas
  de mano y artillería), el comerciante de la taberna (con su fallo corregido detrás del ajuste) y el traspaso entre
  dos barcos (marineros, armas de mano, capitanes) y el objetivo de un barco en el mar; las ofertas a otro comerciante
  (0x5F, 0x75 y el evento 0x13, con sus cartas y, en el visor, la ventana de la oficina de otro comerciante) y la ruta
  por puntos del mapa del mar (0x0B), esta también en el visor (tocar el mar añade puntos; X zarpa, ○ los borra).
  Antes, la primera parte: la reparación de convoyes, los botones de las cartas, la iglesia y el baño.
- **Siguiente**: el encargo `ENCARGO_ACCIONES_3.md` (junto al HANDOFF), con su grupo 5 (apartado 1): el
  ayuntamiento, la milicia y los asedios (con la votación del consejo 0x49 y el cliente de un pedido de la taberna).
- **En la Vita**: lo último que el usuario probó en la consola fueron las cargas más ligeras (04-11-2026). Todo lo de
  después está probado en el PC sin pantalla, con capturas, y compila para la Vita, pero no consta probado en ella
  (apartado 6).

## Cuánto falta (medido el 12-11-2026)

Un solo porcentaje engaña: el motor está casi entero, pero lo que falta es sobre todo lo que hace el jugador y lo que
se ve, que no se mide en las mismas unidades. Por eso, cada parte con su propia cuenta:

| Parte | Hecho | Lo que falta |
|-|-|-|
| La simulación sola (sin el jugador) | del código del juego al que llega, el 84 % reescrito con su nombre y otro 12 % hecho sin él (cargar y guardar partidas, la navegación) o saltado a propósito (medida abajo) | el 4 % sin nada son los manejadores de los tipos de evento que crean las acciones del jugador; además, los estados raros del barco y unas ramas sueltas (apartado 2) |
| El intérprete de misiones (aparte) | 18 % | casi entero (apartado 3) |
| Tipos de evento (58) | 52 | 6: 1 lo crea una acción del jugador que falta (el 0x11, los pedidos del ayuntamiento), 1 el intérprete de misiones, 1 la partida en red y 3 nadie |
| Operaciones (193 códigos) | 154: las 37 de la simulación, 89 de las 106 del jugador, 12 de las 14 de las batallas, las 8 de la partida nueva que se usan y 8 de las 15 de la red | **17 del jugador** (apartado 1; 5 de ellas, 0x54, 0x55, 0x74, 0x76 y 0x82, van con el intérprete de misiones), 1 del intérprete de misiones y 7 de la sala de la partida en red (apartado 8); las otras 15, sin uso: 6 no hacen nada y a 9 no se les ha encontrado quien las encole (una, la 0x8F, manda dos cartas del asedio) |
| Cartas (las 154 llamadas del juego que mandan una) | 132 | 22: 5 de operaciones del jugador, 2 del evento 0x11 que crea una de ellas, 10 del intérprete de misiones y de la dieta, 3 de un evento que nadie crea y 2 de una ventana de pruebas |
| Edificios con vista propia en el juego (13) | 10 con ventana en el visor (lonja, taberna, astillero con el muelle, oficina, prestamista, iglesia, baño, gremio, herrería y el granero —la caballeriza del transporte por tierra, «Overland Trade Office»—; y la oficina de otro comerciante, con las ofertas), todas provisionales: paneles planos | 3 sin ventana (ayuntamiento, armería —la de la milicia— y puerta de la ciudad) y rehacer las 10 con la vista del juego (apartado 4) |
| La ciudad en pantalla | los edificios, los kontors, los iconos de suceso, el minimapa | la gente y los carros, los barcos del puerto, el agua animada, las obras, el fuego y el humo, el asedio (apartado 4) |
| Batallas | enteras, en el motor y en pantalla | corregir con el ajuste los fallos apuntados (apartado 7) |
| Sonido, música y vídeos | nada | todo (apartado 5) |
| Jugar en red con el juego de PC | el motor idéntico, que es la base | todo lo demás (apartado 8) |

### La medida del código de la simulación

`re/emu/sesion5/coverage_parts.py` (Python de Windows: `python coverage_parts.py RUTA/p3vita/src`; con un número de
parte detrás, lista lo que le falta): para cada parte, sus funciones de entrada del juego y todo lo que cuelga de ellas
por llamadas directas, sin la interfaz, siguiendo el cuerpo real de cada función. **Reescrito**: funciones `fn_XXXXXX`
de p3vita. **Nombrado**: la dirección solo sale en un comentario; casi siempre está hecho sin ese nombre (cargar y
guardar partidas, la navegación, el buzón), a veces saltado a propósito (ventanas). **Sin nada**: p3vita no la tiene.
Las partes se solapan, y el grafo es estático (no sigue las tablas de métodos de las batallas), así que es orientativo.

| Parte | Funciones | KB | Reescrito | Nombrado | Sin nada |
|-|-|-|-|-|-|
| Tick de ciudad (0x51BA10) | 155 | 67 | 96 % | 3 % | 0 % |
| Tick de cada comerciante (0x4F7BB0) | 25 | 10 | 93 % | 6 % | 0 % |
| Barcos: navegación, IA y convoyes (0x506720) | 152 | 62 | 69 % | 31 % | 0 % |
| Lo diario, tareas programadas y objetos del mundo | 12 | 2 | 97 % | 3 % | 0 % |
| Asedios (0x6349F0, 0x633A90) | 28 | 15 | 89 % | 11 % | 0 % |
| Cola de eventos (0x4D85C0) | 415 | 193 | 87 % | 11 % | 2 % |
| Operaciones que encola la simulación | 172 | 72 | 79 % | 21 % | 0 % |
| Camino a Gobernador | 223 | 93 | 81 % | 18 % | 1 % |
| Cargar partida (0x549F40) | 103 | 42 | 16 % | 79 % | 4 % |
| Guardar partida (0x549CC0) | 38 | 11 | 0 % | 96 % | 4 % |
| Tormentas (0x5053C0) | 4 | 5 | 97 % | 3 % | 0 % |
| Batallas y encuentros | 58 | 32 | 72 % | 28 % | 0 % |
| Guiones de misiones (0x4ECF64, 0x511270) | 73 | 23 | 18 % | 32 % | 49 % |
| **Toda la simulación** (advance_time y sus operaciones) | 597 | 287 | **86 %** | **12 %** | **2 %** |

- Cargar y guardar partida salen «nombrados», pero están hechos e idénticos al juego en las 12 partidas:
  `savegame.cpp` y `savegame_write.cpp` los reescriben por estructuras, sin funciones `fn_`.
- Las batallas, por sus tablas de métodos, son mucho más que esta fila: sin la interfaz, 249 funciones (148 KB) en el
  mar y 41 (50 KB) en las ciudades ([cloud/battles.md](cloud/battles.md)), todas hechas.
- Lo «sin nada» de toda la simulación son los manejadores de los eventos que aún no se atienden (0x4E6744, el 0x11
  del ayuntamiento; 0x4E2824, el 0x09 de las misiones; 0x4E7BB4, el 0x14 de la red; apartado 1) y del 0x28 y el 0x29,
  que nadie crea (recalculado el 13-11-2026).
- **Todo el ejecutable** (`coverage.py`): un 18 % reescrito con los nombres del juego y otro 16 % nombrado. No mide el
  avance de la interfaz: el resto es casi todo código de ventanas y de dibujo, que p3vita no reescribe instrucción a
  instrucción sino que dibuja a su manera; lo que cuenta de ella son las órdenes que encola (las operaciones, arriba) y
  las reglas de lo que enseña (las ventanas reescritas en `player_actions.cpp`, `statistics_window.cpp`,
  `trade_route_window.cpp`...).

## Lo que falta

### 1. Las acciones del jugador y el resto del buzón (lo siguiente)

El encargo `ENCARGO_ACCIONES_3.md` tiene el detalle (el método, cómo se comprueba, el orden que eligió el usuario:
la reparación de convoyes, los botones de las cartas, la iglesia y el baño, las subastas, la oficina y el barco, la
taberna y los eventos, la milicia y los asedios; la 0x4A también, en el motor). Lo que falta del jugador, por edificio
(los manejadores, en [formats/operations.md](formats/operations.md); varios están solo deducidos y hay que leerlos
enteros antes de reescribirlos):

- [x] **Los botones de las cartas** (12-11-2026; `letter_window.cpp`, [formats/mail.md](formats/mail.md) §4): qué
  cartas los tienen y hasta cuándo, y lo que encola cada uno (0x4D6C90), en el visor con su tecla (X el primero,
  Triángulo el segundo) y su toque; la boda (0x72 y el evento 0x17), la petición del príncipe (0x8B y 0x8C) y el
  embargo (0x77, con la carta 0x83 y su texto). La **0x75** (contestar las ofertas 0x1E/0x1F) está en el motor desde
  el 12-11-2026 (abajo, la oficina de otro comerciante). Queda la **0x74** (los botones de las cartas de las
  misiones), con el intérprete de misiones (apartado 3).
- [x] **La iglesia y los pobres** (12-11-2026; `core.cpp`, las reglas de su ventana en `building_windows.cpp`): 0x30
  dar de comer a los pobres (0x4FE557), 0x31 donar para la ampliación (0x4FE420), 0x32 donar a la iglesia (0x4FE2D0);
  en el visor, la iglesia (.let 0x04) con sus tres páginas.
- [x] **El baño** (12-11-2026; `operations.cpp`, `building_windows.cpp`): 0x39 bañarse (el tick del último baño), 0x42
  y 0x43 un soborno que sale bien o mal según el precio del concejal (0x529E20, con `rand()`); en el visor, el baño
  (.let 0x09) con sus dos páginas.
- [x] **Las subastas** (12-11-2026; el gremio, .let 0x07; `operations.cpp`, `events.cpp`, `events_mayor.cpp`, las
  reglas de su ventana en `guild_window.cpp`): 0x33 pujar, 0x34 avisarme de una subasta, 0x35 la puja automática con
  su evento 0x2D (solo con el byte [+0xD] de los ajustes a 0: no en una partida de un jugador), 0x36 subastar un
  barco; en el visor, el gremio con el ingreso (0x37), las subastas que vienen y la subasta de la ciudad.
- [ ] **El ayuntamiento** (.let 0x03, la ventana *0x6E5500): **los pedidos**, 0x3D (aceptar uno de los siete
  pedidos del mundo, 0x6DE530, que rehace el evento 0x22; 0x53A620: los dos huecos [+0x830]/[+0x832] de la ciudad, el
  evento 0x11 en su plazo y la carta 0x1A «Pedido»; lo encola 0x5E7877) con el **evento 0x11** que crea (0x4E6744:
  «Deshonrado», «Cumplido»); 0x3E mudarse de ciudad natal (carta 0x0E a la crónica, 0x53A810 con 0x4F9430,
  0x4F75F0, 0x635040; la ventana 0x5DD520, por confirmar que sea la del ayuntamiento); 0x44 un barco del jugador al
  servicio de la ciudad (0x53AD90: el [+0x994] de la ciudad, el barco al estado 8 y el pago en [+0x990]; su ventana
  0x5E7AB0); 0x40 y 0x41 despedir y formar una compañía de la milicia (0x51D410, 0x51D4C0; las encola la ventana de la
  armería, .let 0x0B, *0x6E552C, su actualización 0x5B0930); los asedios
  0x88..0x8D y 0x90 (los objetos del asedio: 0x635990, 0x6356D0, 0x635DE0, 0x634660), 0x8F (0x540240: las cartas 0x38
  y 0x37 «Impuesto extraordinario»; no se le ha encontrado quien la encole); 0x47 (un gasto de la ciudad, 0x529F90).
  Y **la candidatura a alcalde**: la pregunta «¿Aceptas la candidatura?» del ayuntamiento de la ciudad natal
  (0x5E387E, botones [+0x604] y [+0x6EC]; parece el evento 0x02, sus candidatos [0x6D4..0x6E7] y sus apoyos [0x6DC]);
  no se sabe aún qué encolan. Confirmar de paso que nada de la simulación encola la 0x40 ni la 0x41.
- [x] **La oficina y su administrador** (12-11-2026; `operations.cpp`, `office_window.cpp`): 0x5A el porcentaje de
  trabajadores de una empresa, 0x5B la orden del administrador para una mercancía, 0x5E contratarlo o despedirlo, 0x62
  el alquiler de una clase de casas, 0x63 los guardias, 0x66 reservar las existencias de una mercancía; en el visor,
  la oficina con su almacén (y «Comerciar»), su administrador y, en una tercera página, las existencias a guardar de
  cada orden, y la ventana de una empresa y de una casa propias.
- [x] **La herrería** (.let 0x08, «Maestro armero»; la ventana *0x6E55A8, su actualización 0x5ECFA0; 12-11-2026,
  `trade.cpp`, las reglas en `smithy_window.cpp`, escenario `smithy_window`): 0x50 armas de mano (la ventana compra una
  para la oficina), 0x56 artillería con la oficina (comprar y vender, al precio de 0x5285B0) y la **0x57** nueva (el
  arma de mano que la herrería fabrica primero: la «Prioridad», solo para el alcalde de la ciudad); en el visor, sus
  dos páginas con los textos del juego. (Lo que las notas llamaban «la armería» era la herrería; la armería, .let 0x0B,
  es la de la milicia: va con el ayuntamiento.) El comerciante de la taberna (0x4D, en el motor; la oferta la prepara
  0x526650) y el armero de la taberna (0x50) tienen su ventana con la taberna.
- [x] **La oficina de otro comerciante: las ofertas** (.let 0x2D, la ventana *0x6E5550, `BuildingParchment.ini`;
  12-11-2026): en el motor (`town_tick/offers.cpp`, escenario `offer_ops`) 0x5F ofrecerle un trato (dinero, un barco o
  mercancías, que se retienen: 0x53DA30 → 0x5338A0, un nodo de la lista de ofertas *0x6DE584, que no se guarda en la
  partida: `SaveGame::offers`, región OF del oráculo), el **evento 0x13** que crea (0x4E7A14: mientras sigan las dos
  cartas, otra vez en 0x40 ticks; si no, las borra y quita la oferta, 0x5340D0), la **0x75** del botón de sus cartas
  (aceptar, 0x534600; rechazar) y las cartas 0x1E y 0x1F; las reglas de su ventana (`offer_window.cpp`, ctest
  `offer_window`: los comerciantes de las oficinas de la ciudad, lo que se puede ofrecer y sus máximos, los bytes) y,
  en el visor, la ventana (el kontor rival de la ciudad: la lista de comerciantes y la oferta, con los textos del
  juego) y el texto de las dos cartas en el compositor (0x480868 y 0x533F20), comparado con el del juego en inglés y en
  español (`letters-check.sh` con `P3_OFFERS=1`: los dos lados hacen las mismas cinco ofertas antes de componer). Un
  comerciante de la IA no guarda cartas: su oferta acaba en el primer evento; es sobre todo para jugar en red.
- [x] **El barco** (12-11-2026, `ship_operations.cpp`; las reglas en `ship_pair_window.cpp`): 0x0F dos barcos se cambian
  los capitanes, 0x10 y 0x11 pasar marineros y armas de mano de uno a otro (la ventana de dos barcos en un mismo sitio,
  0x40EEB0: en el visor, «Otro barco» en la página de la tripulación del menú del barco); 0x0E el objetivo de un barco
  que navega (el botón derecho del mapa del mar sobre uno de los dos barcos que la simulación le guarda cerca, sus
  [+0x50] y [+0x52]: en el visor, tocarlo; sin ver aún en el visor, porque cuesta preparar el momento). Hecha la **0x0B**,
  la ruta por puntos del mapa del mar (0x538DE0: los puntos en un búfer del comerciante, [+0xEC], que no va en la
  partida: `SavedMerchant::route_points`, región MR; la ruta de tres puntos o más de la navegación, 0x445010:
  `NavData::route_through`; escenario `route_points_ops`), también en el visor. La 0x44 resultó ser del ayuntamiento
  (un barco al servicio de la ciudad: el estado 8; abajo).
- [x] **La taberna** (el motor hecho el 12/13-11-2026; `operations.cpp`, `town_tick/tavern.cpp`; escenarios
  `tavern_people`, `traveller`, `burglar`): 0x52 las personas de la taberna (quién habla con quién: el comerciante
  [+0x83E], el viajero [+0x83F], el ladrón [+0x83C], [+0x83D] y los capitanes; tres pueden dar un encargo, el evento
  0x05), 0x53 el viajero con su **evento 0x10** (cartas 0x17 y 0x18) y 0x4F el ladrón con su **evento 0x0F** (atrapado:
  un encargo y la carta 0x74 a la víctima; fallido: 0x16; el robo de dinero y mercancías: 0x20 y 0x15). En el visor
  (13-11-2026; `tavern_window.cpp`, escenario `tavern_window`, ctest `tavern_window`): el menú lateral del juego
  (0x5A66C0; quién está por 0x5A6E60: 0x526880, 0x5265E0, 0x4F6CA0, 0x5269A0, [+0x82C], 0x5261A0, 0x526280, 0x526120,
  0x5261D0) como filas, con la 0x52 al hablar (0x5A7A60, 0x5A7BA0) y al salir (0x5A6590); las ventanas del comerciante
  (0x5D6FC0: su oferta 0x526650, sus barcos 0x5D7DE0, la 0x4D), del traficante de armas (0x5D5C60: los precios
  0x526470, la 0x50 con los pasos 1, 5, 50 y todo), del viajero (0x5D6AE0: la 0x53), del ladrón (0x5D68E0: sus
  víctimas 0x531470, la 0x4F; la fila del juego lleva además un adjetivo del comerciante, de 0x4F9430, que falta) y
  del pirata (0x5D5430: sus dos botones, la 0x4E) y del soplón (0x5D5970: encola tal cual la operación que le prepara
  la ciudad al abrirse la taberna, 0x526B40, la **0x4C**, ahora en el motor: 0x53B4D0, con el texto del rumor de su
  región: los cambios de las ciudades que el jugador no sabe, la mercancía que más falta y la que sobra, los piratas
  avistados; con las misiones, la 0x55 del trozo de mapa del tesoro, que va con el intérprete de misiones). El cliente
  de un pedido (0x5D7760, la 0x51) va con los pedidos del ayuntamiento (grupo 5); la carta de misión (0x5D8100, las
  0x71), con el intérprete de misiones.
- [x] **La caballeriza: el transporte por tierra** (el granero de las vistas del juego, «Overland Trade Office»; .let
  0x33, edificio 0x27; el motor hecho el 13-11-2026, `town_tick/land_transport.cpp`, escenario `land_transport`): 0x3A
  crea el **evento 0x0C** y ejecuta la cola en el acto; el evento (0x4E38B4) sale de la oficina, espera un día, viaja
  por una de las 7 carreteras (0x672BF8), puede sufrir una emboscada, vuelve ante una ciudad sitiada y llega (cartas
  0x66..0x69, línea de aviso 0x29); su fallo con la oficina corta, corregido detrás del ajuste (abajo). Su ventana
  (13-11-2026; `land_trade_window.cpp`, escenario `land_trade_window`, ctest `land_trade_window`): la abre el menú
  lateral si 0x5336B0 lo deja (la caballeriza construida y ninguna de las dos ciudades sitiada; si no, su aviso);
  página 0 las mercancías de la oficina a los carros (pasos 1, 5, 50 y todo; 100 de sitio, un barril 1, una carga 10),
  página 1 la escolta (baja, media, alta: 50, 200 u 800 por cada 10 de sitio) con el consejo por el valor de la carga,
  y «Enviar» (0x5EB3C0): el sorteo de la emboscada, su pérdida repartida por las mercancías ordenadas por su valor
  (con los índices raros del juego) y una 0x3A por mercancía; página 2 el convoy en marcha.
- [x] **Otras** (13-11-2026): 0x45 la candidatura a alcalde (el [+0x118]; `town_hall.cpp`), 0x51 la entrega de un pedido
  (el evento 0x11; `town_hall.cpp`, escenario `order_delivery`), 0xBF los grupos de barcos de otro jugador (la partida
  en red; escenario `ship_groups`) y 0x4A, las herramientas de los desarrolladores de la ventana de encargos de la
  Hansa (carta 0x78 «Día de la Hansa»; el usuario pidió el 11-11-2026 reescribirla en el motor aunque el visor no la
  ofrezca; escenario `hanse_dev_candidacy`). La 0x49 (un voto en el consejo, el evento 0x2E) va con el ayuntamiento;
  la 0x82 (un ataque pirata para una misión) escribe en las variables de los guiones: va con el intérprete de misiones.
- [ ] **Los eventos que crean**: queda el 0x11 (op 0x3D, 0x4E6744: «Deshonrado», «Cumplido»). Hechos el 0x0C, el 0x0F,
  el 0x10, el 0x13 (op 0x5F, la oferta), el 0x17 (op 0x72, 0x4E8684: «Propuesta») y el 0x2D (op 0x35, 0x4E2BC4: la puja
  automática). Aparte: 0x14 (op 0xA4, la sala de la partida en red), y 0x0B (0x4E37F4), 0x29 (0x4DDE70) y 0x28
  (0x4DDF40, cuyo único creador, 0x538DA0, es código muerto), que no crea nadie.
- [ ] **La ventana del final de la partida**: la clasificación de los comerciantes (0x4220B0 → 0x4220D0, sobre
  *0x6CBB30, con el título 0x4F9430 y 0x4F73D0). El objetivo de la partida (evento 0x0E) ya se comprueba.
- [ ] **Las ventanas de operaciones que el motor ya tiene** pero que el visor aún no encola, porque falta la ventana:
  0x61 dar una fiesta (0x5BAE40) y 0x0A borrar un barco (0x4768B0).
- [ ] La ventana de los desarrolladores 0x419EB0 encola de todo y manda dos cartas (0x45 y 0x21 con `rand()`): no hace
  falta salvo que se quiera.

Cada grupo entra en el visor con su edificio (apartado 4): sin la pantalla no se puede usar.

### 2. Lo que queda del motor

- [ ] **Los estados del barco que p3vita no reproduce** (tablas 0x507CB0 en puerto y 0x507D14 en el mar). En puerto:
  los estados 8 y 9 (0x506B8E: un contador [+0x138] hasta 0x20) y el 10 (0x5067C7: pasa a 0 y sigue como atracado); el
  12 (en subasta, 0x5069E2) no hace nada, como en p3vita; los 6, 7, 11 y 13 a 16 van al caso por omisión (0x506BE5), que
  borra el barco (0x507FB0). En el mar: con manejador propio, 9 (0x507852), 0xA (0x507865), 0xF y 0x16 (0x50787F); por
  omisión (0x507C7F), 5 a 8, 0xC a 0xE, 0x10, 0x11, 0x14 y 0x15. Mirar qué pone cada estado y reescribir sus
  manejadores, y con ellos el borrado de los barcos de la IA hundidos (`Sailing::sink`), el de los barcos de un convoy
  que se deshace (`fn_50a3d0`) y el de los estados sin manejador (`update_docked`).
- [ ] **La venta de un barco** (0x509250, `events_mayor.cpp`): dos ramas sin reescribir, el barco en un convoy (cambia
  el dueño del convoy y, en el mar, lo manda a la ciudad natal con una orden de zarpar en el acto) y el barco en el mar
  sin ciudad (la misma orden). Se llega subastando un barco.
- [ ] **El convoy asaltante busca su batalla con otro barco** (0x50797E, 0x507995): 0x6130B0 recibe la ciudad y el
  dueño del último barco que tocó el bucle de convoyes (esi no se pone en ese camino), no los del guía. p3vita usa el
  guía: habría que seguir esi por todo el bucle de 0x506720. Solo cambia algo si ese barco tiene una batalla en marcha
  en su ciudad, pero para jugar en red hace falta.
- [ ] **El asedio con la vista de la ciudad abierta**: con la vista abierta ([+0x77] del ejército), su tick llama
  además a 0x62BE90, 0x62DD70, 0x62E970, 0x58CFB0 y 0x62B860 (y 0x632780 a 0x62DD70), que escriben campos del ejército
  ([+0x5A0], [+0x10], [+0x12]...). Mirar si alguno cambia lo que decide la simulación (para jugar en red cuenta que el
  jugador del PC tenga abierta la ciudad sitiada). Va con el asedio en la vista (apartado 4).
- [ ] **Las operaciones a las que se llega desde advance_time por partes sin reescribir**: el intérprete de misiones
  (0x4ECF64: 0x28, 0x7C, 0x7D, 0x82 y 0x8E); los gráficos de la ciudad (0x599040, desde el evento 0x25 y la muralla:
  0x08, 0x09, 0x0A, 0x2F, 0x39, 0x52, 0x56–0x59, 0x61 y 0x93); y 0x5C7810, desde los objetos del mundo (0x33, 0x34,
  0x36, 0x37 y 0x98). Mirar si alguna de esas ramas es simulación.
- [ ] **La partida nueva**: 0x545650, que la ventana llama al cambiar el año (21 ciudades en 1300 y una más cada 20
  años: rehace los huecos y sortea las producciones; sin leer aún cuándo se usa: las partidas nuevas de otros años ya
  salen iguales al juego). 0x4F99B0 (una ciudad sin plantilla de mapa) no se usa con los datos del juego.
- [ ] **El encargo de fundar una ciudad**: se cumple cuando la ciudad tiene mercado, y eso solo lo da el evento 0x2F con
  su primer edificio municipal y más de 1000 habitantes. En el emulador (`grow.sh`, partida 1) la ciudad se queda en
  272 habitantes hasta el día 520, y el plazo es de unos dos años. Medir más allá de 550 días (`--desde`) y ver si en
  el juego de verdad crece porque alguien le lleva mercancías. La ciudad nueva, sin probar en el visor de la Vita.
- [ ] Partidas de la versión 0x4D (`missav/0100–0204.pat`): el lector solo lee la 0x82 y posteriores.
- [ ] El objeto opcional del mundo (0x533DA0): ninguna partida lo tiene y p3vita lo rechaza al leer. Solo si aparece
  una partida que lo tenga.
- [ ] Sin comprobar porque no se pueden preparar: las dos persecuciones que alcanzan sin hueco para la batalla (harían
  falta 255 batallas abiertas).

### 3. El intérprete de misiones

- [ ] 0x511270 interpreta los textos del juego y, con el comando 0xFD, programa eventos 0x1B con un guion que ejecuta
  0x4ECF64. Se arranca desde la interfaz (la operación 0xC0, al empezar una partida con la 0xA7, y otros dos caminos);
  en las partidas de prueba los 0x1B llegan sin guion, y el oráculo se salta 0x511270 al cargar. 73 funciones, 23 KB.
  Con él: el evento 0x09 (0x4E2824, cartas de buena y mala cosecha), sus 5 cartas (tres hechas por el guion, la carta
  del mapa 0x46 o 0x45, la 0x22), las operaciones que encola (0x1B, 0x28, 0x4E, 0x7C, 0x7D, 0x82, 0x8E) y la carta final
  0x3D del evento 0x34 (0x4F3AF0, con su lista de avisos [+0x8], 0x4F3980..0x4F3CE0).
  Y las cuatro operaciones del jugador que escriben en las variables de su guion (el [+0xC] de un evento 0x1B, que
  p3vita aún no tiene) o trabajan con las cartas que solo manda él (0x3C..0x40, 0x44, 0x45/0x46, 0x71): 0x74 (los
  botones de las cartas de misión, 0x53EC30; sus botones y bytes ya están en `letter_window.cpp`), 0x54 (quitar una
  carta 0x44, 0x53CA30 con 0x4D7340), 0x55 (pagar una carta 0x45/0x46, 0x4D74C0) y 0x76 (quitar una carta 0x71,
  0x53EDF0 con 0x4D7870). Lo que hay en el juego de esto
  (qué misiones y cuándo) está por mirar.

### 4. Las pantallas

Cada pantalla se adapta a la Vita y se comprueba a ojo frente al juego original (capturas del juego en el PC), con sus
gráficos y sus textos; las reglas de lo que enseña y las órdenes que encola, contra el juego como siempre. Las normas
del visor (tocar los edificios, táctil primero, las ventanas emergentes), abajo en «Decisiones del usuario».

- [ ] **Las vistas de dentro de los edificios** (lo pidió el usuario el 05-11-2026). Las ventanas que abre ahora el
  visor al entrar en un edificio (la lonja con el mercado, la taberna, el astillero y el muelle, la oficina, el
  prestamista) son paneles planos provisionales. El juego tiene para cada edificio su vista, con sus dibujos y animaciones, en `images/Gebaeude_innen/`
  del CPR: la taberna `Kneipe_innen.jpg` y `Kneipe/` (el tabernero, el capitán, el informante, el ladrón...: 34
  ficheros), el astillero `innenbild_werft01.jpg` y `werft/`, la lonja `markthalle.jpg` y `markt/`, la oficina
  `innenbild_kontor01.aim` y `Kontor/`, el prestamista `darlehensgeber01.jpg` y `bank/`, el ayuntamiento `rathaus.jpg`
  y `rathaus/`, el gremio `gildenhaus.jpg` y `Gilde/`, la iglesia `innenkirche01.jpg` y `Kirche/`, el baño `badhaus.jpg`
  y `Bad/`, la herrería `schmied.jpg`, el granero `Scheune.aim` y `Scheune/`, la puerta de la ciudad `Stadttor.aim` y
  `stadttor/`; los rostros en `images/Gesichter/`. Rehacer cada ventana con su vista a medida que se hagan las acciones
  de cada edificio (lo que listan y mandan, en `player_actions.cpp`, no cambia).
- [ ] **Los edificios que aún no tienen ventana**: el ayuntamiento y la armería de la milicia (con sus acciones,
  apartado 1, grupo 5) y la puerta de la ciudad (`Stadttor`: qué hace, por leer en el código; el transporte por tierra
  resultó ser de la caballeriza, el granero de las vistas, que ya tiene la suya, 13-11-2026, como el gremio, la
  herrería, la iglesia y el baño). Se añaden a `kPlaces` de `src/app/main.cpp` por su tipo de
  `.let`.
- [ ] **El panel de la Hansa** (Cuadrado en una ciudad) es una ventana propia del visor (lo que lista y encola sí es
  como el juego); el juego tiene las suyas (la de encargos 0x5E87D0, la de la carretera 0x5F2A20). Por decidir con el
  usuario si se rehace como el juego, como las demás.
- **La ciudad viva**: la simulación cambia la ciudad como el juego y el visor ya dibuja su estado actual (sus listas de
  edificios, obras y fortificaciones, y las de cada oficina), pero solo los edificios:
  - [ ] Antes de seguir, mirar en el juego original qué se ve y qué cambia: si la ciudad se redibuja en cuanto se
    construye algo, qué se anima (gente, carros, agua, humo...), cómo se ve un asedio.
  - [ ] Los habitantes y los carros por las calles, y las animaciones de los edificios.
  - [ ] Los barcos en el puerto (la lista *0x6E2D50, el objeto 0x6E6220 al que la simulación avisa al atracar y
    zarpar), también como puntos en el minimapa con el color de su dueño (0x56C730: azul los tuyos, gris oscuro los
    piratas, blanco los demás, rojo los de otro jugador humano; el dibujo del minimapa, 0x59C590).
  - [ ] El agua animada (`WaterSparkleTexID`; la de la batalla ya existe) y las variantes de los tiles (sombras, nieve).
  - [ ] Las obras: hoy lo que está en obras ([0x762] con `pending` ≠ 0) se dibuja terminado; faltan los andamios y los
    bordes de calle de las celdas 0x81.
  - [ ] El fuego ([0x76A]): falta el humo y las llamas (los incendios de la ciudad, de un asedio, y la torre que cae en
    una batalla y arde unos 100 ticks como edificio, el evento 0x25; 0x5891C0, 0x599040). Decisión del usuario
    (10-11-2026): con la ciudad animada.
  - [ ] El asedio en la vista: el campamento, las tropas y el ataque a la puerta (0x62C730 y los objetos del ejército
    [+0x1F8], [+0x574], [+0x5DC..]) y el aviso al jugador; su icono de suceso ya sale.
  - [ ] Coste en la Vita: cada redibujado rehace la ciudad entera (2 ms en el PC más la lista de sprites); si da tirones
    en avance rápido, redibujar solo las celdas que cambian. Y quitar `SavedTown::drawn_lists` y `SavedTown::fixed`, que
    ya no se usan para dibujar.
- [x] **Los iconos e imágenes de los textos del juego** (12-11-2026, lo vio el usuario en la carta del embargo): la
  moneda, el barril y el fardo (`\C`, `\B`, `\L`) y las imágenes por número (`\tN_`: el adorno de la crónica, los
  iconos del barco, las balas de las vistas de los edificios, los de las votaciones...) se dibujan con los gráficos
  del juego (`textures.ini`, cargados la primera vez) en todas las pantallas, no solo en las cartas; antes, fuera de
  ellas se perdían y en ellas eran dibujos a mano.
- [ ] **Las capitulares de las cartas** (`\d` y una mayúscula: la primera letra adornada, 0x462540 con el juego de
  imágenes *0x6CC3D4 y `DecoLettersCol` de `parchment.ini`): hoy sale la letra normal. Leer de dónde salen las letras
  adornadas y dibujarlas.
- [ ] **Los gráficos**: decidir la transparencia del motor (magenta, alfa 0 de los `_TP`, sombras con alfa 100); subir
  los DXT3 a la Vita como texturas BC2 sin descomprimir.
- [ ] **Detalles del mapa del mar**: los nombres de las ciudades se ven blandos (stb_truetype sin ajuste a los píxeles,
  marrón oscuro sin contorno: igualarlo con una captura del juego); una franja en los bordes de arriba y abajo de
  `Vollansichtskarte.bmp`, que el juego seguramente tapa: parar la cámara antes.
- [ ] **Las estadísticas en una partida nueva**: hasta la primera carga el juego nombra bien las ciudades (su tabla va
  por índice); sin el ajuste, el visor las saca de su lista por número (UTF-8) y no de `SaveOpaque::town_name_table`
  (Latin-1), como ya hacen las cartas y la crónica.
- [ ] **El final del juego**: el vídeo `eldermann` al llegar a Gobernador (con los vídeos, apartado 5) y la ventana de
  la clasificación (apartado 1).
- [ ] Icono y LiveArea propios del VPK (sin usar arte del juego).
- [ ] Más adelante, con el juego más avanzado: mejoras de manejo pensadas para la Vita (táctil, botones, tamaño de
  letra, accesos rápidos), sin cambiar lo que hace el motor.

#### Mirar en el juego original (el usuario, en su PC)

- Lo que se ve y se anima en la ciudad (arriba).
- La imagen de las cartas 0x46 y 0x6E (el mapa con la cruz): p3vita supone el cuarto byte de la paleta del mapa a 0xFF
  (sale casi opaco); con 0 saldría mucho más tenue. Si se ve distinta, es lo primero que mirar.
- Las armas del mercado de la oficina (página de armas, 0 espadas, pulsar pasar al barco): si cambian las armas de
  mano, el fallo de 0x4A8F99 (apartado 7) se puede dar.
- El nombre del primer barco del jugador en la partida 1: desde el cambio del cargador «ship names» (91b958b) sale
  «Constance» en vez de «Ermanrich». Ver cuál enseña el juego.
- Los nombres de las ciudades del mapa del mar (letra, tamaño, color, contorno).

### 5. Sonido, música y vídeos

- [ ] El audio con SDL2 en el PC y en la Vita: no hay nada. Los sonidos y la música del juego (formatos por leer; MP3).
- [ ] Al poner el sonido, el visor tiene que decir al motor si está encendido (`Sailing::set_battle_sound`): con la
  batalla en pantalla, cada sonido de la batalla gasta un `rand()` del mundo, como el juego. Y el sonido del abordaje
  (0x61F1BF) tendrá que guardar en [+0x14A] lo que devuelve la vista de la ciudad del PC con la vista abierta (1,
  suponiendo un hueco de sonido libre); hoy guarda 0, como el emulador.
- [ ] Los vídeos Bink (con FFmpeg u otro descodificador): la introducción, el final de Gobernador (`eldermann`)...

### 6. Probar en la Vita

- [ ] Todo lo hecho desde el 04-11-2026: el buzón y las líneas de aviso, las tandas del barco, la oficina, el astillero
  y los convoyes, el prestamista, construir y el panel lateral, las estadísticas, las batallas en pantalla, la partida
  nueva, guardar y cargar, las rutas comerciales. En especial el teclado de la Vita (el IME de SDL: nombres de barco,
  de jugador, de ficheros de ruta), que no se puede probar sin ella. Cada VPK nuevo: `bash scripts/build-vita.sh` →
  `dist/p3vita.vpk`; lo que va en `ux0:data/p3vita/`, en `Patrician III\ux0\data\p3vita\` (la caché de textos hay que
  rehacerla una vez con el exe en esa carpeta cuando cambia su versión).

### 7. Corregir los fallos del juego

El ajuste existe: `fix_game_bugs()` (`src/p3data/game_fixes.hpp`); cada corrección va con `if (fix_game_bugs())` junto
al comportamiento original. Apagado en la librería (el oráculo, tick_probe y los tests comparan con el original),
encendido por omisión en el visor (Opciones; `p3vita-settings.txt`; `--fix-game-bugs on|off`), y apagado siempre en
una partida en red. `tick_probe --fix-game-bugs` enseña lo que cambian. Las correcciones que solo cambian un texto se
hacen sin preguntar; las que tocan la partida o el equilibrio, se preguntan antes, una a una.

Pendiente:
- [ ] **Decidir y corregir los fallos reproducidos que aún no tienen corrección** (lista abajo, «Reproducidos, sin
  corregir»): los de los piratas y las batallas en el mar, los de la batalla en una ciudad, los de la batalla que
  dirige el jugador, construir en el borde de la rejilla, el reloj de las batallas y los de las cartas.
- [ ] **Más adelante, cada corrección por separado** (idea del usuario, 05-11-2026): un inventario de los fallos y
  rarezas y una pantalla en las Opciones para encender o apagar cada corrección, en vez del ajuste único. Por eso cada
  fallo de la lista de abajo tiene que quedar bien identificado: dónde está, qué hace mal y qué cambia su corrección.
- [ ] La fiesta de la IA que nunca ocurre, como opción aparte (abajo, «No se corrigen»).

### 8. Multijugador con el juego original (al final de todo)

La razón de fondo para reescribir el motor idéntico: si p3vita se comporta byte a byte como el juego, una Vita puede
jugar en red con el juego original en un PC. Las acciones del jugador no cambian la partida directamente: son órdenes
que van a una cola y ejecuta la simulación, y en red solo viajan esas órdenes.

- [ ] La simulación entera idéntica, no solo lo que sale en las partidas de prueba (las misiones, los estados raros del
  barco, las ramas del apartado 2...). Un solo byte distinto y las dos partidas se separan.
- [ ] Todas las acciones del jugador como las mismas órdenes que encola el juego.
- [ ] La red del juego: `Schnelltransfer.dll` (de Ascaron, sobre Winsock: buscar partidas en la red local, unirse por
  IP, mandar y recibir; sus funciones `Network_*`). Estudiar su protocolo y los mensajes de la partida en red, y
  reescribirlos con los sockets de la Vita. La sala de la partida en red: las operaciones 0xA2..0xA6, 0xA8 y 0xA9, el
  evento 0x14 y las del bucle de tiempo real por encima de 0xC0 ([formats/operations.md](formats/operations.md)).
- [ ] El ajuste de corregir los fallos del juego, apagado siempre en red.
- [ ] Los números con decimales iguales en la Vita (ARM) que en el PC (x86): hoy lo están en todo lo comprobado
  (`-ffp-contract=off`).
- [ ] Probarlo: una Vita y el juego original en un PC en la misma red.

Lo que ya se sabe que puede separar las partidas:
- **El azar de los sonidos**: con la batalla en pantalla y el sonido encendido, los sonidos gastan el `rand()` del
  mundo (apartado 5); el dibujo de una batalla también gasta `rand()` por fotogramas (0x62018B, 0x62F0E2), fuera del
  motor: con una batalla a la vista y sonido, no podrá ir exacto.
- **El asedio con la vista abierta** y **el convoy asaltante que busca su batalla con otro barco** (apartado 2).
- **El barco nuevo de una guarida sin barcos** toma su fuerza de lo que haya en la memoria (abajo, en los fallos de la
  batalla en una ciudad).
- **Construir pegado al borde de la rejilla**: el juego escribe fuera de ella (abajo); quizá el visor deba impedirlo.
- **El reloj de las batallas** depende del reloj real: p3vita fija una operación 0x94 de 84 pasos tras cada tick con
  batallas (el resultado de una batalla automática no cambia; solo el tick en que acaba).

### 9. Herramientas y comprobaciones

- [ ] tick_probe: en los globales (G) y los ejércitos (J) sigue saltándose los valores que parecen punteros; haría
  falta la lista de los campos de puntero de las dos regiones (en las demás ya se compara todo, `pointer_field`).
- [ ] `town_oracle.py` sin `--barcos` (como lo lanza `scripts/tick-probe.sh` sin `P3_SHIPS`) se para con
  `UC_ERR_READ_UNMAPPED` en las rutas (región Q): grabar Q solo con `--barcos`, o limpiar el [+0x24] de los barcos al
  cargar sin navegación.
- [ ] Más velocidad para el oráculo ([cloud/oracle-speed.md](cloud/oracle-speed.md)): `regions()` en C (un 25–30 %),
  tick_probe a la vez que el oráculo por una tubería (un 15 %), saltarse la foto de después en las llamadas que no
  escriben, comparar solo las regiones que cambian.
- [ ] Contar con `re/emu/sesion18/connect_count.sh` cuántos días tarda alguna partida en empezar sola una batalla en
  una ciudad (en 7 días, ninguna de las 12).
- [ ] Que `coverage_parts.py` siga también las tablas de métodos, como el grafo de las batallas (`re/battles/graph.py`
  de p3vita-datos).
- [ ] Los documentos al día: `docs/formats/mail.md` y `docs/formats/operations.md` llevan algún dato atrasado cada vez
  que se conecta algo; las listas buenas son `MAIL_CALLS` (`tools/oracle/town_oracle.py`) y `run_operation`
  (`src/p3data/simulation.cpp`).
- [ ] Pasar al inglés lo que queda en español en el repositorio (menos este documento): poco a poco, al tocar cada
  fichero por otra razón.

## Hecho (resumen)

Lo que sigue está comprobado contra el juego; dónde, en [oracle.md](oracle.md).

**Datos y formatos** ([formats/](formats/)): el CPR (`cprtool`), las imágenes BMP, TGA y JPG y el formato AIM (por
ingeniería inversa de `AIM.dll`), los mapas de ciudad y sus tiles (con tres correcciones a P3Modding), las animaciones
`.lcr`, las fuentes del juego con el español montado sobre el inglés, los textos de la interfaz leídos del exe de cada
idioma (en p3vita solo se guarda dónde está cada texto; el visor los guarda en una caché en la carpeta de datos), las
partidas `.pat` (leídas desde la versión 0x82; escritas en la 0x85 con el compresor del juego, iguales byte a byte) y
las estadísticas `.pst`.

**El motor, idéntico al juego** (`src/p3data`):
- el comercio, los precios y sus umbrales; el tick de cada ciudad (0x51BA10) y de cada comerciante (0x4F7BB0); lo
  diario y semanal, las tareas programadas, los 15 objetos del mundo y las estadísticas diarias;
- la navegación (las rutas entre ciudades, el movimiento, la salida y la llegada, la orden de zarpar), los barcos y los
  convoyes de la IA (comercio, destino, tripulación, armas, astillero), la reparación de barcos y convoyes en el
  astillero, las obras y las murallas de las ciudades;
- la cola de eventos (46 de los 58 tipos: todos los que se dan sin el jugador) y las 37 operaciones que encola la
  simulación; el inventario de [inventario.md](inventario.md), partes A a D cerradas (D, salvo los estados del barco);
- los asedios, las tormentas, los piratas (encendidos por omisión), y **las batallas** enteras: automáticas y
  dirigidas por el jugador, en el mar y en una ciudad o una guarida, con sus balas, sus choques con rocas e islotes,
  sus órdenes, sus ventanas y su tiempo (un tick cada 3375 ms mientras el jugador dirige), guardadas en la partida
  (notas: [cloud/battles.md](cloud/battles.md), [cloud/battles-2.md](cloud/battles-2.md),
  `re/emu/sesion21/player_battle_notes.md`);
- **el camino a Gobernador** ([formats/world_tick.md](formats/world_tick.md) §7 y §8): la candidatura, los encargos de
  la Hansa de los cinco tipos (fundar una ciudad, la carretera, cazar un pirata, la ciudad que crece, la guarida), las
  elecciones con el voto del jugador (0x3F), la reunión de la Hansa y la ventana del final;
- **el buzón** ([formats/mail.md](formats/mail.md)): mandar y guardar las cartas como el juego (117 de las 154
  llamadas), el texto de cada carta (el compositor 0x47F3B0) y la crónica (0x47DD40) en los dos idiomas, las líneas de
  aviso (0x548CA0), leer, borrar y los ajustes (0x6E, 0x6F, 0x70, 0x71), y las dos cartas con imagen;
- **las acciones del jugador** (76 de las 105 operaciones): comprar y vender, la tripulación y el capitán, reparar, la
  oficina, el astillero (encargar, cancelar, mejorar, cambiar el nombre), los convoyes, los préstamos y las acciones
  de empresa, construir, derribar, las calles y subastar un edificio, hacerse pirata, los encargos de la Hansa, las
  rutas comerciales (0x68..0x6D) y sus ficheros `.rou`, los botones de las cartas (la boda, la petición del príncipe
  en un asedio y el embargo de un préstamo que no se devuelve: 0x72 con el evento 0x17, 0x8B, 0x8C y 0x77), la iglesia
  (0x30..0x32) y el baño (0x39, 0x42, 0x43), las subastas (0x33..0x36 con el evento 0x2D), la oficina (0x5A, 0x5B, 0x5E,
  0x62, 0x63, 0x66), la herrería (0x50, 0x56, 0x57), la taberna (sus personas y la 0x52; el comerciante, 0x4D; el
  traficante de armas, 0x50; el viajero, 0x53), las ofertas a otro
  comerciante (0x5F, 0x75 con el evento 0x13), la ruta por puntos (0x0B) y dos barcos (0x0E, 0x0F, 0x10, 0x11); y las
  reglas de las ventanas que las encolan (`player_actions.cpp`, `statistics_window.cpp`, `trade_route_window.cpp`,
  `new_game.cpp`, `letter_window.cpp`, `building_windows.cpp`, `guild_window.cpp`, `office_window.cpp`,
  `offer_window.cpp`, `smithy_window.cpp`, `ship_pair_window.cpp`, `tavern_window.cpp`);
- **la partida nueva** (`new_game.cpp`; notas en `re/emu/sesion24/new_game_notes.md`): el mapa, 0x544E50 y sus
  operaciones, los 70 días de antes de jugar, el jugador y sus ajustes: el `.pat` que escribe p3vita es igual byte a
  byte al del juego, también con otros años, otros jugadores y mapas propios;
- **las rutas comerciales** (`trade_routes.cpp`; notas en `re/emu/sesion25/trade_routes_notes.md`): las paradas, los
  barcos y convoyes recorriéndolas, y los ficheros.

**El visor** (`src/app`; en el PC con SDL2 de escritorio y sin ventana con `--steps` y capturas, `p3vita --help`):
la pantalla de inicio (el libro del menú principal) y la ventana «Configuración del juego» entera; la ciudad con su
estado actual, los kontors y los iconos de suceso; el panel lateral del juego (minimapa, iconos, botones y lista de
barcos, el menú del barco con sus seis páginas y la ruta comercial, los mensajes, construir con la colocación táctil,
las estadísticas enteras, la barra de arriba con el hombre y la casa); el mapa del mar con su panel; el mercado en sus
modos (barco, oficina, convoy, armas); la taberna, el astillero y el muelle, la oficina, el prestamista, la iglesia y
el baño; el buzón (con los botones de las cartas) y la
crónica con el aviso de carta nueva y las líneas de aviso; el panel de la Hansa; las batallas en pantalla (la
pregunta, la batalla en el mar, en una ciudad y en una guarida, con su panel y sus órdenes táctiles); guardar y cargar
en 8 ranuras; las opciones (idioma español o inglés, corregir los fallos del juego). Las cargas en la Vita, más ligeras
y a demanda con aviso.

**Herramientas** (`tools/`, `scripts/`): el oráculo (`tools/oracle/town_oracle.py`, que ejecuta el juego en Unicorn
sobre una partida cargada; `newgame_oracle.py`, la partida nueva; `letter_oracle.py`, `chronicle_oracle.py`,
`ticker_oracle.py`), `tick_probe` (compara p3vita con lo grabado, función a función), los escenarios con estados
retocados (`retouch_unchecked.py` y los anteriores), `world.sh 7` (el mundo 7 días en las 12 partidas), las
comprobaciones de escritura de partidas, de cartas (`letters-check.sh`) y de la Vita (`vita_check.sh`). Cómo se usa
cada una: el HANDOFF y [oracle.md](oracle.md).

## Decisiones del usuario que siguen valiendo

- **El motor idéntico al original**, comprobado contra el juego, aunque vaya más lento (04-11-2026): para jugar en red
  con el PC al final. Lo visual se comprueba a ojo.
- **Los fallos del juego**: reproducidos tal cual, apuntados aquí y corregidos detrás de `fix_game_bugs()`, encendido
  por omisión en el visor. Las correcciones que cambian el equilibrio se preguntan una a una.
- **No todo lo que salen en las partidas**: el inventario de la simulación sale del ejecutable, con estados retocados
  para lo que las partidas no alcanzan.
- **Las batallas avanzan con una operación 0x94 de 84 pasos tras cada tick con batallas**, en p3vita y en el oráculo
  (en el juego depende del reloj real). Los piratas y la batalla automática, encendidos por omisión también en el
  visor.
- **El visor se maneja como el juego**: a cada acción se llega tocando su edificio (o el barco desde el panel); el
  panel lateral del juego, fijo a la derecha, abierto por defecto y plegable, con botones más grandes para el dedo.
- **Táctil primero**: tocar y arrastrar sobre lo que se ve, con lo posible resaltado (construir: las zonas donde cabe
  en verde, el edificio en verde o rojo bajo el dedo); nunca un cursor fijo en el centro para nada nuevo (la cruz para
  entrar en los edificios se aceptó «de momento»; si se rehace, preguntar).
- **Las ventanas emergentes**: cada botón táctil con su tecla (X la acción, ○ cerrar), sin notas que expliquen lo
  obvio, lo que no se puede hacer atenuado y sin poder pulsarse, y con la ventana abierta la barra de arriba no enseña
  sus acciones.
- **Los textos**: los del juego, leídos del exe del jugador, en el idioma elegido en las Opciones (el CPR y el exe de
  ese idioma en la carpeta de datos); las frases que el juego no tiene, en los dos idiomas (`tr()`). Los términos de la
  versión española (Gobernador, no Alderman).
- **En la Vita**: nada de ficheros que el usuario no haya pedido en `ux0:data/p3vita/`; cargas a demanda con aviso
  mejor que precargas de fondo que den tirones; el usuario mide los tiempos a ojo.

## Los fallos del juego encontrados

Cada uno con su dirección, qué hace y su estado. «Seguro»: leído en el código o visto en el oráculo; «parece»: puede
ser a propósito. El detalle, en el comentario del código y en [oracle.md](oracle.md).

### Reproducidos, sin corregir aún

**Los piratas y las batallas en el mar** ([cloud/battles-2.md](cloud/battles-2.md), «Game bugs»):
- Los edificios de la guarida nunca se ponen en su mapa (seguro): 0x515DA0 llama a 0x4F9C80 con el tipo y la
  orientación cambiados de sitio. Corrección: cambiarlos.
- 0x514B80 mejora al capitán del guía una vez por cada barco con capitán (seguro): encola la 0x12 para el guía, no
  para cada barco.
- Parecen, sin comprobar: 0x61A9E0 mira el borde de arriba con la x; 0x605420 y 0x606CC0 leen el primer barco del
  convoy de un lado sin mirar si existe (p3vita no cuenta ninguno); 0x60ADBF deja sin poner el lado que 0x608F70 lee de
  la pila (p3vita pone 0); 0x60BCDA se puede colgar con un radio de 0; el bucle de nombres de 0x5063C0 no acaba con
  menos de 307 nombres; el convoy pirata sin guía (los piratas apagados con un convoy en el mar, o el jugador que
  captura el guía) hace que 0x507A65 lea fuera de la lista de barcos (p3vita no hace nada con un convoy sin guía; el
  oráculo vuelve en seguida de 0x50D0C0).
- El reloj de las batallas (0x612DB0; sin confirmar): avanza su tiempo [+0xC44] 40 ms por cada paso en vez de por cada
  40 ms consumidos: las batallas van a la mitad de lo previsto y dependen de los fotogramas. Solo cambia lo que duran.

**La batalla en una ciudad** (`battle/town_step.cpp`, `town_ship_step.cpp`, `town_battle.cpp`):
- La torre mira su nave amiga con (dx, dx) (0x5FE246; seguro): la nave «tapa» en una de dos direcciones fijas.
  Corrección: (dx, dy).
- La batalla releída pone torre en todos los huecos (0x5FD57F; seguro, visto en `town_step_branches`): comprueba la
  dirección de la fuerza de la torre, no su valor.
- La nave que se suma busca hueco con el paso de x (0x602923; seguro): en el borde de arriba o abajo se va saliendo.
- El defensor solo huye mientras está la torre 0 (0x622C7E; seguro): mira cuatro veces la torre 0.
- [+0xA9C] no se borra entre subpasos (0x5FD510; seguro): la mayor fuerza de abordaje es la de toda la batalla.
- 0x60234C hace las naves nuevas siempre del lado 0 (0x6025CC; seguro): no pasa, solo se suma el dueño del lado A.
- El defensor humano que gana sin atacante corta el bucle de los partes (0x5FF68A → 0x5FF829; seguro): los lados de
  detrás no reciben la carta 0x29. No se alcanza.
- La fuerza del barco nuevo de una guarida sin barcos depende de la memoria (0x514797; seguro): la mitad alta de eax
  es lo que hubiera (con mapa, la dirección del mapa en el montón). p3vita usa siempre la buena; para jugar en red,
  este caso daría barcos distintos.
- El barco nuevo de la guarida no la defiende (0x514820; parece): lo mete en el convoy defensor y lo saca; la batalla
  queda solo con las torres.
- La torre de una guarida que se quema pierde su entrada del «pool» (0x515C00; parece): no vuelve a la lista de libres.
- Sin efecto visible: la cabeza de lista sin inicializar de la nave asaltante (0x506F41) y del ataque a la guarida
  (0x4EBB4A) (el oráculo y p3vita la fijan a 0xFFFF).
- No reproducidos: el convoy asaltante busca su batalla con otro barco (0x507995; apartado 2); la guarida sin convoy
  borra el convoy 0xFFFF (0x5149E2; no se alcanza); el convoy atacante con barcos de otro dueño cuelga el juego
  (0x5FCA7A; p3vita lo evita siempre).

**La batalla que dirige el jugador** (`battle/orders.cpp`, `battle_end.cpp`, `battle_setup.cpp`, `balls.cpp`):
- La orden de atacar con una marca sin barco borra un bit de la dirección 0x12A (0x541050; seguro). No se alcanza.
- «Has derrotado a…» usa el dueño del barco vencido como número de lado (0x60DDEC; seguro): con un pirata lee fuera.
- «… te ha derrotado» mira el guía del convoy del jugador en vez del del pirata (0x60E5E5; seguro).
- Los textos del final con la guarida salen con «%s» sin rellenar.
- El tipo del barco leído como el byte entero [+0xE] (0x60E07D, 0x60A73C; seguro): ningún barco pasa de 3.
- El capitán de los piratas de un barco solo (0x605420; seguro): lee el guía sin mirar el convoy; no se alcanza.
- En la ciudad, la cuenta del lado sigue bajando tras acabar (0x5FD98B; seguro): sin efecto.
- Guardar cambia la batalla (0x5FBEA6; seguro): con la cuenta del defensor en marcha, el escritor pasa su lado al
  ordenador en la propia batalla. Seguramente a propósito.
- La orden de atacar una torre pierde memoria (0x6016CB; seguro): p3vita no lo reproduce (no cambia el resultado).

**Construir en el borde de la rejilla** (0x4FB9C0 y su huella; seguro, visto en `build_places`): la ventana de
construir (0x47B8C0) acepta una casa de pescador en el agua de la columna 1 y el juego escribe en la columna −1 (en
Windows, el montón antes de la rejilla). Las marcas de dónde cabe (0x4FD0D0, 0x4FD4A0, 0x5241E0) también escriben fuera
cerca de los bordes, y 0x5241E0 guarda en la pila sitio para 24. p3vita solo escribe dentro, con el ajuste y sin él.

**La iglesia y el baño** (12-11-2026): los dos de los sobornos (0x42, 0x43), que el usuario decidió dejar como el
original (abajo, «No se corrigen»); el de la ventana de dar de comer, corregido (abajo).

**Fallos que la ventana del juego ya impide** (12-11-2026; no se dan jugando: la ventana comprueba bien lo que la
operación comprueba mal; se dejan como el original):
- Pujar (0x33, 0x53A010) compara el dinero sin signo (un comerciante con deudas podría pujar); la ventana lo mira con
  signo.
- Vender armas de mano (0x50, 0x53BC90; el armero de la taberna) cobra por cada barco todo lo que queda por vender, venda
  lo que venda; la ventana nunca pide más de lo que lleva el barco elegido, así que el cobro sale bien.
- La artillería con la herrería (0x56, 0x53CAF0) no mira si hay oficina; la ventana solo sale con oficina (p3vita no
  hace nada sin ella).
- Pasar armas de mano entre barcos (0x11, 0x5389C0) compara el sitio sin signo (un barco sobrecargado las recibiría
  todas); la ventana pide sitio para 10.

**Las cartas** (12-11-2026; la reparación de convoyes cancelada y el botón de la propuesta, corregidos: abajo):
- La carta 0x83 del embargo sin barco ni oficina dice «Las siguientes mercancías fueron embargadas y entregadas en tu
  oficina» y no lista ninguna: se cobran en dinero (0x53F2CF, 0x53F326; solo el texto).
- La 0x74 (los botones de las cartas de misión) deja escribir la variable número [+0x12] del guion, una más allá de
  las que tiene (0x53EC61: `jb` en vez de `jbe`; parece; va con el intérprete de misiones).

**Los grupos de barcos** (13-11-2026):
- Añadir barcos a un grupo marcado de otro jugador (la 0xBF, 0x5436D0; seguro, solo en la partida en red): copia la
  longitud entera con su bit 15, así que lee 0x8000 entradas más allá y pone los barcos nuevos detrás de ellas. p3vita
  los pone detrás de los que había y deja la longitud del juego (el escenario `ship_groups` no lo compara).

**La ventana del transporte por tierra** (13-11-2026; rarezas que no hacen daño, se reproducen): el orden de las
mercancías para repartir la pérdida de una emboscada lee el valor del puesto donde cayó la anterior, no el de cada
mercancía (siempre sale el mismo orden), y la pérdida de cada una usa el valor de su puesto; un coste por encima del
del peligro (no lo deja la ventana) daría la vuelta a la pérdida.

**De cargar y guardar**: el `.pst` tiene un byte de más cuando hay meses (sin ajuste: nadie lo lee); la lista de nodos
de cada ciudad ([0x9B4]) da la vuelta en cada guardado y carga (sin confirmar; si el orden no importa, no hace daño).

### Corregidos con el ajuste

**La caballeriza de la otra ciudad** (el menú lateral de la caballeriza, 0x5336B0; 13-11-2026, seguro, corregido a
petición del usuario): para el aviso «La caballeriza de la ciudad de %s está todavía en construcción» (su resultado 2)
el juego busca otra vez la caballeriza en la lista de la propia ciudad (el desplazamiento de la ciudad que guardó en
la pila, no el de la otra), así que ese aviso no sale nunca y se puede mandar un convoy a una ciudad cuya caballeriza
no está acabada (la carga llega igual: al llegar solo se mira la oficina). Corregido (`land_trade_window.cpp`
`fn_5336b0`): se mira la de la otra ciudad y el visor enseña el aviso; con el ajuste, el escenario `land_trade_window`
sale distinto del juego solo en esas llamadas (2 en vez de 0: sin la caballeriza de Hamburgo, y con otra ciudad dada
que no la tiene).

**El transporte por tierra con la oficina corta** (el evento 0x0C al salir, 0x4E38B4; 13-11-2026, a petición del
usuario): si la oficina no tiene todo lo pedido, pierde todas sus cargas, barriles o piezas enteras, pero el convoy
lleva su número dividido otra vez entre 2000 (casi siempre 0): la mercancía desaparece. Corregido
(`town_tick/land_transport.cpp`): el convoy lleva las unidades enteras que salen de la oficina; con el ajuste, el
escenario `land_transport` sale distinto del juego solo en esa cantidad (3 cargas en vez de 0).

**El comerciante de la taberna** (0x4D, 0x53B5E0; 12-11-2026, a petición del usuario): las mercancías que ofrece son
el excedente de la ciudad (0x526650), pero al comprarlas la ciudad pierde lo que NO te llevaste: con la oficina todo
(bien), con un barco solo lo que no cupo (si cabe todo, la ciudad no pierde nada y la mercancía sale de la nada).
Corregido (`trade.cpp` `fn_53b5e0`): la ciudad pierde lo que te llevas (con el ajuste, el escenario `tavern_trader`
sale distinto del juego solo en las existencias de la ciudad, en los casos con barco). (Pagar al vendedor «si su número
es menor que el de ciudades» no es un fallo: los comerciantes 0..23 son los de las ciudades, cada uno con el número
de la suya.)

**Las ofertas a otro comerciante** (0x5F, 0x53DA30; 12-11-2026, visto en el escenario `offer_ops`; corregido a
petición del usuario): al ofrecer mercancías, la oferta apunta la cantidad pedida aunque el comerciante tenga menos (se
retiene solo lo que tiene); si el otro la acepta, recibe la cantidad apuntada entera, y si la rechaza (un comerciante
de la IA la deja caducar a los 5 ticks), se le devuelve entera al que ofrecía: en los dos casos sale mercancía de la
nada. La ventana del juego (0x5BDAFA) lo deja pasar: su máximo suma la oficina y los barcos de la ciudad antes de
redondear a barriles o cargas (150 en la oficina y 50 en un barco dan 1 barril; se retienen solo los 50 del barco,
porque la oficina no llega a un barril entero) y cuenta los barcos de la ciudad en cualquier estado (uno en el
astillero también), mientras que la retención solo toma de los atracados (estado menor que 4). Corregido
(`town_tick/offers.cpp` `fn_53da30`, `offer_window.cpp`): la oferta apunta los barriles o cargas enteros que se pueden
retener (`holdable_units`), y la ventana no deja ofrecer más (ctest `offer_window`); con el ajuste, el escenario
`offer_ops` sale distinto del juego solo en la cantidad de las dos ofertas que pedían más de lo que había.

**Del astillero, las cartas y la iglesia** (12-11-2026, a petición del usuario; ctest `game_fixes`):
- La reparación de un convoy cancelada (las órdenes 1 de su guía, 0x508D7E..0x508E00; parecía): el juego deja todos sus
  barcos con el casco entero aunque el trabajo no se hizo (un barco solo, cancelado, conserva el suyo). Corregido: cada
  barco se queda con el casco que tenga (`fn_508aa0`; con el ajuste, el escenario `convoy_repair` sale distinto del
  juego solo en ese casco).
- El botón «Aceptar» de la propuesta 0x22 (0x480D91; parecía): el día de la carta (de 1 a 31) sobre los días de antes
  de su mes cuenta uno de más, así que tras aceptarla (la 0x60 pone su [+0xF] a 0) el botón sigue hasta el final del
  día y se podía aceptar otra vez (otra 0x60, otra boda); el plazo de 14 días, uno más. Corregido en la regla de la
  ventana (`letter_window.cpp`): el día contado desde 0.
- La ventana de dar de comer a los pobres (0x5CAF83; parecía) valora su quinta fila como sal (la mercancía 4) aunque se
  reparte vino (la 7), y con ese valor calcula el «tipo» que da la reputación. Corregido (`building_windows.cpp`):
  valorada como vino.

**La simulación**:
- La carretera vencida del jugador (0x4EAFE4, en 0x4EB25F; seguro): el juego crea un encargo igual con un índice de
  ciudad como comerciante: no vence nunca, la carretera se construye sola y al cumplirse sube el [+0x37] (la
  candidatura) del comerciante con ese índice. Corregido: la copia sin comerciante (0xFF) (`road_expired`). Rarezas del
  mismo encargo, reproducidas: el plazo inicial (0x5326E0) cuenta días como meses y lee fuera de la tabla 0x672D78; la
  0x3B con un comerciante que no existe (0x53A4CA) deja «contador + cantidad − la dirección del evento» (corregido: no
  toca el encargo).
- Las mejoras de las piezas de muralla (la 0x22 de los tipos 0x34 y 0x35, 0x523D70; seguro los dos primeros): las
  herramientas se sacan con la cantidad de madera, se pagan a 1,278 · 0 (la constante 0x66BC6C) y al comerciante no se
  le paga. Corregido: como las torres (`gate_costs`, `tower_ops`).
- Los del tick de ciudad ([formats/town_tick.md](formats/town_tick.md) §8): el administrador de 0x51C360 solo cuenta la
  primera oficina; 0x5109B0 deja negativo [+0xC]; la granja de grano compara al revés al contratar; 0x524920 escribe
  fuera de la rejilla con un muelle en la última columna. Los tres primeros se notan jugando (con el ajuste, 7 de las
  12 partidas cambian en 7 días). `town_tick_fixes`, `pier_edge`.
- La IA con deudas (0x4F5A20, operación 0x28): la renta del mejor ([esp+0x2C]) sin inicializar (el oráculo la pone a 0,
  `ZEROED_FRAMES`). Corregido: el primer edificio con unidades es el mejor.
- El capitán de la IA que se retira (evento 0x27, 0x4DDC00): lo busca solo en el primer barco de cada comerciante y
  si no está, bucle sin fin (0x4DDCB0); p3vita quita el evento. Corregido: lo busca en todos.
- La operación 0x80 (0x536756; solo en red): escribe en el registro 0x80 en vez del de [+4]. Corregido.
- Las cartas antiguas (0x4D6780): una carta de tipo 0x86 o más corta la cadena y puede perder la lista libre.
- El apellido de un comerciante en quiebra (0x4F5D80, operación 0x65): módulo el índice del último apellido marcado
  (puede dividir entre 0 o no acabar). Corregido: módulo el número de apellidos ([0x6DDB74]).
- El ejército sitiador nuevo (0x629C60): no mira cuántos grupos caben (desde el 101 pisa el ejército), sin las tres
  puertas lee la dirección 0 y suma objetivos sin llenar. Corregido (`siege_fixes`).
- Los piratas ante una ciudad en el estado 4 (evento 0x1E, 0x4DFD44): si vuelve a estar vigente, la cola lo atiende
  sin fin y el juego se cuelga. Corregido: el evento se quita al acabar el estado 4.
- Las cartas del capitán pirata hundido por un convoy de comerciante (0x50BFD0, 0x50C1C7..0x50C20B; seguro): el barco
  se borra antes de mirar su capitán: la carta 0x54 y la noticia 0x2A no salen y la fama baja como sin capitán.
  Corregido (decisión del usuario, 05-11-2026, sabiendo que cambia la fama).
- Reparar en el astillero (0x03 y 0x1D: 0x52ACD0, 0x539990): la carta 0x01 se prepara y no se manda (seguro); la tarifa
  0x52A9E0 vuelve a subir a 285 por encima del nivel 20 (parece la comparación de 0x52AA87 al revés). Corregidos los dos
  (a petición del usuario).
- Las acciones de empresa (0x2B, 0x4D98F0, y el evento 0x18, 0x4E8804): comprar del fondo no recorta a lo que tiene
  (se crean acciones de la nada) y el dinero del comprador se compara sin signo (0x4D994F). Corregidos los dos (el
  tercero, abajo, no).
- Las rutas comerciales (`trade_routes.cpp`): quitar una parada lee fuera del almacén (0x4D4E80, 0x4D4ED0); una ruta
  nueva se corta si el almacén crece a mitad (0x6C, 0x53E96D); la parada siguiente ante un puerto cerrado puede colgar
  el juego (0x4D5900: su último recurso no mira si el puerto está cerrado). Las comprobaciones con `ja` por `jae` de la
  0x69 y la 0x6B, y la 0x69 sin mirar el comerciante: la ventana no las manda así; p3vita se para ahí.
- El barco de la IA ante un puerto cerrado (0x51A2C0): el barco solo va a la primera ciudad abierta por número (recibe
  su ciudad ya borrada) y la comprobación del barco grande está al revés (`jb` por `jae`). Corregidos los dos. Sin
  ninguna ciudad abierta va a la 0 aunque esté cerrada (no se corrige).
- La formación de la batalla en una ciudad mira los barcos del lado D en los sitios del lado A (0x600060, 0x6005D2;
  seguro); con más barcos en el D que en el A el juego se puede colgar: p3vita no se cuelga nunca (mira el sitio de su
  propio barco, como con el ajuste).

**Al guardar y cargar**:
- El nombre de la ciudad natal al guardar (0x549D7D): la tabla 0x6DDA00, que va por número de ciudad, con el índice.
  Corregido: por el número (tabla 0x6DE4B8).
- El relleno de las batallas (0x611950): 3 bytes por entrada sin inicializar. Corregido: a cero.
- Los nodos del evento 0x34 cambian en cada carga y guardado (0x4F3A40, 0x4F39E0; parece). Corregido: se guardan como
  se leyeron.
- Leer un `.pst` pone a cero los contadores del comerciante 0. Corregido (`read_statistics`).

**Los textos** (solo cambian lo que lee el jugador; `tick_probe --fix-game-bugs` en los escenarios):
- El nombre de una ciudad sacado de la tabla 0x6DDA00 (por número) con el índice de la ciudad: el informe de la
  expedición (0x4E7198), las cartas de las elecciones (0x5292A3, 0x5299F0), la cabecera de cada carta (0x47F534), la
  crónica, las estadísticas, el parte de la batalla en una ciudad (0x5FE7E0), el rumor del soplón de la taberna
  (0x526B40). Solo pasa tras cargar una partida (al final de la carga 0x5121C0 deja la tabla por número). Corregido:
  por el número de la ciudad.
- El rumor del soplón de la taberna (0x526B40; 13-11-2026): una epidemia o una hambruna en una ciudad de su región, o
  su final, las cuenta con los textos del asedio («La ciudad de %s está sitiada por un ejército», «El asedio de %s ha
  finalizado»). Corregido: con sus propios textos, que el juego tiene sin usar (0x6C2624/28, 0x6C2670/74).
- El informe de la expedición pierde su final (0x4E7306..0x4E745B: «, » sin contar); las compras de la expedición
  cuentan barriles o cargas por la última mercancía (0x52C5FE).
- La carta del encargo del jefe de la Hansa humano (0x77 del tipo 2, 0x4EA6A8): la comprobación del objeto pirata al
  revés (sin ninguno, el juego se cerraría).
- El embargo (0x13, 0x4F6626) y el pago del príncipe (0x85, 0x636230): solo los 16 bits bajos del dinero.
- La noticia de la oficina nueva (0x0D, 0x530C20; parece): solo con un índice no menor que el número de comerciantes.
- La carta de «no se puede reparar» (0x02, 0x47F684): el nombre de otro barco en vez de la ciudad.
- El bloqueo rechazado, en inglés (0x0B, 0x47FB67): la dirección del nombre de la ciudad como primer número.
- El accionista de tu empresa (0x62, 0x48425C): su apellido buscado entre los nombres de pila.
- La crónica: el adjetivo del encargo cumplido (0x6F, 0x47F118: la tabla 0x69EE38 en vez de 0x69EE48) y el apellido
  del pirata detenido (0x47F1BC: [+0xE] en vez de [+0xF]).
- Las estadísticas: los comerciantes de Competencia al abrir (0x49D380, 0x49DB27..0x49DB67), el orden alfabético con
  tildes (0x4720D0, 0x471FF0, 0x471F10, 0x639CF0: corregido como un diccionario), ordenar por riqueza con negativos
  (0x471EA0) y el título de los habitantes en inglés sin espacio.
- La línea del bloqueo de la Hansa (0x5483D0, clase 0x2A, 0x548766): la comprobación al revés, nunca sale jugando; y
  en red se manda al comerciante 0 (0x4E0D86).
- La pestaña de la crónica en español (0x6B2DD8 repite «Notificaciones oficiales»): «Noticias públicas».
- Los valores de la ventana «Detalles» de la partida nueva (0x49B4DF): las seis primeras filas con la tabla de los
  piratas y la riqueza con esa tabla al revés. Corregido en el visor (cada fila con la suya; los barcos, como el juego).
- En el plano de guarida 253 no se puede atacar una torre tocándola (0x6016CB no quita los números del final del
  fichero, (1, 4)). Corregido en el visor; no cambia el motor.
- Sin texto que poner, p3vita no escribe nada con el ajuste ni sin él: el convoy sin reparar en español (la tabla
  0x6C1350 no tiene quinto texto: el juego español casi seguro se cierra), la carta del fin de la caza de piratas
  (0x482653 copia desde la dirección 0) y una venta de menos de una unidad en el informe (0x52C5B6 divide entre 0).
- Sin efecto en p3vita (usa cadenas): el informe de la expedición en 4000 bytes (0x4E70E0), los búferes del parte de la
  batalla (0x60D299), las cartas con búfer muy largo (0x481172); la página de depuración de las estadísticas
  (0x4A19C0) no se puede abrir; la tabla de los habitantes con una ciudad fundada tras la última semana (0x4A3BB0) lee
  la pila (p3vita pone 0); los títulos por sexo de la crónica (0x39, 0x3A) usan la mitad equivocada, pero son iguales.

**La interfaz que protege**: las armas en la ventana del mercado (modo 5; 0x4A8F99, 0x4A903A): pulsar pasar espadas,
arcos, ballestas o mosquetones sin nada que pasar manda una 0x09 de armas de mano con la cuenta de los cañones. El
visor no deja pulsar esos botones; `player_actions.cpp` guarda la reproducción detrás del ajuste. No se sabe si en el
juego se puede pulsar (apartado 4, «Mirar en el juego original»).

### No se corrigen

- **Los sobornos del baño** (12-11-2026; parecen fallos): el que sale bien (0x42, 0x53AC50) quería no dar más de dos
  de los cuatro concejales de una ciudad al mismo comerciante, pero lee cuatro veces el mismo hueco y nunca limita; el
  que sale mal (0x43, 0x53AD10) pasa el concejal, si era del comerciante o de nadie, al comerciante número 1 (un hueco
  libre sería 0xFF). Decisión del usuario (12-11-2026): dejarlos como el original, porque cambian la partida.
- **La ruta por puntos de un barco que sale o entra en un puerto** (0x0B, 0x538DE0; 12-11-2026; parece un fallo, de
  poco efecto): si ninguno de los puntos es una ciudad, el juego le pasa a «zarpar» (0x537670) como destino el punto
  de llegada de la ciudad 0 o de la 1 (según el número de la ciudad del barco sea impar o par), no el de la suya.
  «Zarpar» solo compara ese punto con el de la ciudad de la que vino el barco: si está entrando en un puerto y viene
  justo de esa ciudad (de la 1 a una ciudad par, o de la 0 a una impar), no da la vuelta, aunque se le pone la ruta
  por puntos; en los demás casos da la vuelta, como debe. Decisión del usuario (12-11-2026): dejarlo como el original
  (no está claro qué quería el juego).
- **Las fiestas de la IA no ocurren nunca** (0x5316E0; seguro por el código): la ciudad de la próxima fiesta ([+0x88]
  del mundo) empieza a 0xFF (el constructor 0x52F520) y solo la cambia la propia fiesta. Decisión del usuario
  (03-11-2026): no se corrige, ni con el ajuste, porque cambiaría el equilibrio; podría ser una opción aparte. (Una
  fiesta de la IA sube la satisfacción +4, +3, +2 y +1 las 4 semanas siguientes.)
- **La tarifa del astillero en la mejora de un barco** (0x52B670, 0x52B95E..0x52B9D7; parece): calcula una tarifa por
  el nivel del astillero y la pisa con 405 por carga. Decisión del usuario (05-11-2026): «déjalo».
- **Los dividendos de las acciones sin vender** (evento 0x18): los paga el dueño y no los cobra nadie. Decisión del
  usuario (05-11-2026): se queda como el original.
- Sin efecto en una partida o ya resueltos en p3vita: la temporada de piratas no mira cuántas ciudades caben (0x7E,
  0x53F91E..0x53F941: desde la 17.ª pisa la cola; no pasa con 24 ciudades); el edificio en venta no mira si la celda
  cae en la rejilla (0x28, 0x53D213); 0x62B2B0 no mira cuántos objetivos caben (p3vita agranda el vector); 0x4D8DB0 da
  también el registro siguiente al último de la cola; la 0x9F no acaba con 32 768 barcos o más (0x611309); un convoy
  en el «otro estado» del bucle con un estado que no sea el 5 se quedaría dando vueltas; las divisiones por 0 de
  0x52F030 y del informe de la expedición, que no se dan con los datos del juego.
