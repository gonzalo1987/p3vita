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

## Dónde estamos (14-11-2026)

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
  las estadísticas; la puerta de la ciudad (los materiales de la carretera); las batallas que dirige el jugador;
  guardar y cargar; las opciones (idioma,
  corregir los fallos del juego). Las ventanas de los edificios, pasando a las del juego (apartado 4): su marco, su
  imagen con sus animaciones y sus gestos, su menú lateral en el panel (ya en todos) y sus páginas (hechas las de la
  taberna, el prestamista, la iglesia, el gremio, la herrería y los avisos del ayuntamiento).
- **Lo último** (13 y 14-11-2026, commits c578774 .. e3bd503): las ventanas de los edificios como las dibuja el juego
  (apartado 4). El 14-11-2026, con cuatro agentes en paralelo (`re/emu/sesion34/`): lo que faltaba del gremio (las
  «Rutas comerciales» con el mapa de lo descubierto, el edificio en su subasta, la media bala de los convoyes, la sala
  de subastas de la partida en red, los contratos en el orden del exe español), la página de rezar de la iglesia y
  las caras del cura (con las páginas de después de donar), las flechas del interés del prestamista, el texto del
  informante con su barra, los gestos de todos los interiores y el menú lateral de todos los edificios, cada uno con
  su disposición del juego. Una segunda tanda (oficina, lonja y otros, astillero, ayuntamiento) se paró a medias; de
  ella, la lonja la acabé después: sus tres páginas de información (existencias con los precios, producción y consumo
  de la semana), con sus cifras iguales a las del juego en el emulador. Y la fecha del menú del astillero escribe la
  partida como el juego (decisión del usuario). Y L y R cambian la velocidad del tiempo con un edificio abierto;
  arriba y abajo de la cruceta eligen el botón de su menú lateral. Y la oficina entera como el juego: sus siete páginas
  (balance, mercancías, armas, almacén, las órdenes del administrador, la ficha personal y el traslado), comprobadas
  contra el juego en el emulador donde se pudo; y las páginas del baño (el baño, cada concejal con su saludo, la caja
  del soborno y su respuesta) y de la caballeriza (las mercancías, la escolta y el convoy enviado) como las del juego.
- **Antes** (13-11-2026): el grupo 5 del encargo en el motor (sin el visor aún): los pedidos del ayuntamiento
  (0x3D y su evento 0x11, con sus cartas «Pedido», «Deshonrado» y «Cumplido»), mudar la ciudad natal (0x3E), la milicia
  (0x40 y 0x41), un barco al servicio de la ciudad (0x44), el dinero con la caja de la ciudad (0x47), el voto en el
  consejo (0x49) y las ventanas del asedio (0x88, 0x89, 0x8A, 0x8D, 0x90 y la 0x8F, que nadie encola); escenarios
  `town_orders`, `home_move`, `militia_ops`, `guard_ship`, `treasury_council`, `siege_window`. Antes, acabado el grupo
  4 del encargo. En el visor, el soplón de la taberna (su noticia, que
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
- **Hecho el 15-11-2026 (tercera sesión, `re/emu/sesion38/`)**: los restos pequeños de las vistas: el fundido del
  libro del astillero entre dos barcos o tipos (con el fallo de sus títulos corregido detrás del ajuste), la ventanita de
  mensaje del juego con su tamaño (0x41D9B0, `game_message`), los textos centrados «\c» revisados en las vistas, la
  sombra del edificio en su subasta (las variantes de los `.let` ya se leen) y las capitulares de las cartas.
- **Hecho el 17-11-2026 (`re/emu/sesion39/`)**: las preguntas del convoy con la ventanita del juego («Deshacer
  convoy», «Abandonar convoy» y el aviso «No es posible formar un convoy»), y con ellas la alineación y las imágenes de
  la caja de texto de la ventanita (commit 64c2eb6). Después, las sombras de los edificios y objetos y la nieve de la
  ciudad por estaciones, como el juego (apartado 4; commit 97e004c).
- **Hecho el 17-11-2026 (segunda sesión, `re/emu/sesion40/`)**: la lista de barcos como la ventana de pergamino del
  juego («Información de la flota»), con sus letras, textos, columnas, órdenes y botones, abierta desde el icono del
  panel y desde el faro de la ciudad (los barcos de su puerto, también los de otros comerciantes) (apartado 4); y,
  ya en el menú del barco, el barco y el convoy de otro comerciante y varios barcos a la vez (18-11-2026).
- **Hecho el 18-11-2026 (`re/emu/sesion41/`)**: en el menú del barco, la cabecera del barco propio como la del juego,
  el convoy propio (su estado 2) y el nombre del pirata; de paso, la sombra de la tabla en lo alto de todos los menús
  laterales, el borde rasgado bajo la fila de iconos y las texturas que se mezclan multiplicando (apartado 4).
- **Hecho el 18-11-2026 (quinta sesión, `re/emu/sesion43/`)**: las estadísticas como las del juego, su menú lateral
  y su ventana de pergamino con todas sus entradas, trazadas en el emulador (apartado 4).
- **Hecho el 19-11-2026 (`re/emu/sesion43/`, «Part 2»)**: los mensajes como los del juego: su menú lateral, la carta
  y la crónica en la ventanita de mensaje del juego y la ventana de ajustes de las cartas (apartado 4). Después
  (`re/emu/sesion44/`), el panel de las batallas repasado con el código: el menú del barco sobre la selección de la
  batalla con «ver convoy», la fila de acciones en sus sitios y el minimapa con los colores del juego, y la barra de
  arriba como la del juego: el panel lateral con las letras y los textos del juego, acabado (apartado 4).
- **Hecho el 19-11-2026 (séptima sesión, `re/emu/sesion45/`)**: la base de las misiones (apartado 3): leer el ini y
  los `.p2m` del juego, la lista de misiones, la operación 0xC0 que las arranca al empezar una partida, cargar y guardar
  los guiones en curso con sus variables (antes se tiraban, en p3vita y en el oráculo) y el bucle del intérprete con sus
  esperas; comprobada contra el juego (la carga en las 12 partidas, la 0xC0 en todos los modos, la partida nueva
  entera). Faltan las 138 instrucciones del intérprete.
- **Hecho el 20-11-2026 (en paralelo, a petición del usuario, `re/emu/sesion51/`)**: la base del sonido (apartado 5,
  parte 1): el mezclador con el audio de SDL en el PC y en la Vita, la música MP3 del juego con sus fundidos, los
  efectos WAV y el `sound.ini` leídos, los volúmenes de Música y Sonido de las Opciones y la música del menú principal.
  Después la parte 2: la música en la partida (el director de música del juego, sus atmósferas, el tema de cada ciudad,
  la música de batalla y la de los edificios), y la 3: los sonidos de la interfaz (los botones, las cartas, el baño, la
  oración, el escupitajo e «Invertir estéreo»).
- **Siguiente**, en el orden que eligió el usuario el 17-11-2026 (abajo, «Decisiones del usuario»): acabados los restos
  de las pantallas (las preguntas del convoy, las sombras y la nieve, el panel lateral con las letras y los textos del
  juego), el intérprete de misiones (apartado 3: la base hecha; ahora sus instrucciones); luego el final de la partida
  (apartados 1 y 4), la ciudad viva (apartado 4), el sonido (apartado 5), los fallos del juego con su pantalla de
  ajustes (apartado 7) y la partida en red (apartado 8). Antes: las páginas como las del juego de todos los edificios (la última tanda, el
  15-11-2026: las siete del ayuntamiento con su texto tras una misión fallida, la puerta de la ciudad con su «Ruta
  terrestre» y sus siete páginas de asedio, y el cliente de un pedido de la taberna; el panel de la Hansa propio del
  visor, quitado): el grupo 5 en el visor, hecho.
- **En la Vita**: lo último que el usuario probó en la consola fueron las cargas más ligeras (04-11-2026). Todo lo de
  después está probado en el PC sin pantalla, con capturas, y compila para la Vita, pero no consta probado en ella
  (apartado 6).

## Cuánto falta (medido el 13-11-2026)

Un solo porcentaje engaña: el motor está casi entero, pero lo que falta es sobre todo lo que hace el jugador y lo que
se ve, que no se mide en las mismas unidades. Por eso, cada parte con su propia cuenta:

| Parte | Hecho | Lo que falta |
|-|-|-|
| La simulación sola (sin el jugador) | del código del juego al que llega, el 87 % reescrito con su nombre y otro 12 % hecho sin él (cargar y guardar partidas, la navegación) o saltado a propósito (medida abajo, 20-11-2026) | el 1 % sin nada son los manejadores del evento de la red y de dos que nadie crea; además, los estados raros del barco y unas ramas sueltas (apartado 2) |
| Las misiones (20-11-2026) | la base (la lista, la 0xC0, cargar y guardar los guiones, el bucle del intérprete y sus 4 esperas), las 138 instrucciones del intérprete, las operaciones que encola (la 0x82 incluida), el evento 0x09 que crea y las 4 operaciones del jugador sobre sus cartas | la carta final 0x3D, el visor, las campañas (apartado 3) |
| Tipos de evento (58) | 54 (el 0x09 de las misiones, 20-11-2026) | 4: 1 la partida en red y 3 nadie |
| Operaciones (193 códigos) | 173: las 37 de la simulación, las 106 del jugador (la 0x82 de las misiones y las 0x54, 0x55, 0x74 y 0x76 de sus cartas, 20-11-2026), 12 de las 14 de las batallas, las 8 de la partida nueva que se usan, 8 de las 15 de la red, la 0x8F (el impuesto extraordinario del asedio, sin quien la encole) y la 0xC0 (el arranque de las misiones, 19-11-2026) | 7 de la sala de la partida en red (apartado 8); las otras 14, sin uso: 6 no hacen nada y a 8 no se les ha encontrado quien las encole |
| Cartas (las 154 llamadas del juego que mandan una) | 148 (las del intérprete y del evento 0x09, 20-11-2026) | 6: la carta final 0x3D del evento 0x34, 3 de un evento que nadie crea y 2 de una ventana de pruebas |
| Edificios con vista propia en el juego (13) | Las 13 con ventana en el visor (lonja, taberna, astillero con el muelle, oficina, prestamista, iglesia, baño, gremio, herrería, el granero —la caballeriza del transporte por tierra, «Overland Trade Office»—, la armería de la milicia, el ayuntamiento y la puerta de la ciudad; y la oficina de otro comerciante, con las ofertas). Con la vista del juego (marco, imagen y gestos, menú lateral; o el rollo de papel sobre la ciudad, como en el juego, la oficina de otro comerciante y la armería): las 13; con todas sus páginas como las del juego: la taberna (con el cliente de un pedido desde el 15-11-2026), el prestamista, la iglesia, el gremio, la herrería, la lonja, la oficina, el baño, la caballeriza, la oficina de otro comerciante (con la lista «Escoge oferta»), la armería el astillero con el muelle (sus seis páginas, la lista de reparaciones incluida) y el ayuntamiento (sus ocho páginas); y la fiesta en los puestos del mercado | Ninguna (las ocho páginas de la puerta de la ciudad, también las del asedio, desde el 15-11-2026) |
| El panel lateral y sus ventanas como los del juego (19-11-2026) | Las 7 partes: la lista de barcos, el menú del barco entero (con la ventana de formar convoy y las de la ruta), el menú de construir, las estadísticas (su menú y su ventana con las 19 entradas de sus cuatro páginas; las 9 de depuración el juego no las enseña), los mensajes (su menú, la carta y la crónica en la ventanita del juego, la ventana de ajustes), el panel de las batallas (el menú del barco sobre la selección de la batalla, «ver convoy», la fila de acciones y el minimapa como el juego) y la barra de arriba | Nada |
| La ciudad en pantalla | los edificios, los kontors, los iconos de suceso, el minimapa | la gente y los carros, los barcos del puerto, el agua animada, las obras, el fuego y el humo, el asedio (apartado 4) |
| Batallas | enteras, en el motor y en pantalla | corregir con el ajuste los fallos apuntados (apartado 7) |
| Sonido, música y vídeos (20-11-2026) | la base: el mezclador (PC y Vita), los 123 MP3 y los 178 WAV del juego leídos, el `sound.ini`, los volúmenes de las Opciones y la música del menú principal; la música en la partida (el director, las atmósferas, el tema de cada ciudad, las batallas, los edificios); los efectos de la interfaz (botones, cartas, baño, oración, escupitajo) | los efectos de la ciudad, el mar, las batallas y el asedio, y los vídeos (apartado 5) |
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
| Cola de eventos (0x4D85C0) | 415 | 193 | 88 % | 11 % | 1 % |
| Operaciones que encola la simulación | 172 | 72 | 79 % | 21 % | 1 % |
| Camino a Gobernador | 223 | 93 | 81 % | 18 % | 1 % |
| Cargar partida (0x549F40) | 103 | 42 | 16 % | 80 % | 4 % |
| Guardar partida (0x549CC0) | 38 | 11 | 0 % | 96 % | 4 % |
| Tormentas (0x5053C0) | 4 | 5 | 97 % | 3 % | 0 % |
| Batallas y encuentros | 58 | 32 | 72 % | 28 % | 0 % |
| Guiones de misiones (0x4ECF64, 0x511270) | 73 | 23 | 29 % | 35 % | 36 % |
| **Toda la simulación** (advance_time y sus operaciones) | 597 | 287 | **87 %** | **12 %** | **1 %** |

- Cargar y guardar partida salen «nombrados», pero están hechos e idénticos al juego en las 12 partidas:
  `savegame.cpp` y `savegame_write.cpp` los reescriben por estructuras, sin funciones `fn_`.
- Las batallas, por sus tablas de métodos, son mucho más que esta fila: sin la interfaz, 249 funciones (148 KB) en el
  mar y 41 (50 KB) en las ciudades ([cloud/battles.md](cloud/battles.md)), todas hechas.
- Lo «sin nada» de toda la simulación son los manejadores de los eventos que aún no se atienden (0x4E7BB4, el 0x14
  de la red; apartado 1) y del 0x28 y el 0x29, que nadie crea (recalculado el 13-11-2026, con el 0x11 del ayuntamiento
  ya hecho; el 0x09 de las misiones, 0x4E2824, hecho el 20-11-2026).
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
- [ ] **El ayuntamiento** (.let 0x03, la ventana *0x6E5500). **El motor, hecho** (13-11-2026; `town_tick/town_hall.cpp`,
  `town_tick/sieges.cpp`, `simulation.cpp`; escenarios `town_orders`, `home_move`, `militia_ops`, `guard_ship`,
  `treasury_council`, `siege_window`): **los pedidos**, 0x3D (uno de los siete del mundo, 0x6DE530: el hueco libre
  [+0x830]/[+0x832] de la ciudad, el **evento 0x11** en su plazo y la carta 0x1A «Pedido») con el evento 0x11 (0x4E6744:
  al llegar el plazo juzga al comerciante, «Deshonrado» y «Cumplido», su reputación [+0x22], y luego da a la ciudad
  un barril o una carga al día); 0x3E mudar la ciudad natal (la carta 0x0E a la crónica, el alcalde que deja su
  puesto, los objetos del asedio 0x635040, la ciudad del Gobernador); 0x44 un barco al servicio de la ciudad (el
  estado 8, el pago en [+0x990]); 0x40 y 0x41 la milicia (0x51D410, 0x51D4C0; las encola la armería, .let 0x0B,
  *0x6E552C, 0x5B0930); 0x47 el dinero entre un comerciante y la caja de la ciudad; 0x49 el voto de un jugador en el
  consejo (en el evento 0x2E del día, 0x4DA060); los asedios 0x88 (la caja paga), 0x89 (vender mercancía a la ciudad
  sitiada), 0x8A (prometer entregas), 0x8D (entregar lo que pidió el príncipe), 0x90 (pagar el rescate y las órdenes
  del ejército) y la 0x8F (el impuesto extraordinario, cartas 0x38 y 0x37; nadie la encola, reescrita igual). La
  candidatura a alcalde (la pregunta del ayuntamiento, 0x45) quedó hecha con el grupo 4. En el visor (13-11-2026,
  `town_hall_window.cpp` con su ctest): la armería (.let 0x0B: formar y despedir unidades, 0x41 y 0x40, solo para el
  alcalde) y la página 0 del ayuntamiento (.let 0x03), los avisos (los pedidos del mundo y el decreto del príncipe;
  «Aceptar misión», 0x3D). **El visor de las otras siete páginas, hecho** (15-11-2026; `re/emu/sesion36/
  notes_townhall.md`): barco patrulla (0x44; sus condiciones comprobadas con el juego, escenario `guard_ship_window`),
  alcalde (la candidatura 0x45, las propuestas al consejo 0x48 y los impuestos del regidor de una ciudad sin consejo),
  información de la Hansa, sala de conferencias (la elección 0x3F, la Dieta 0x3F, el consejo 0x49 y sus resultados),
  casa de la cerveza, arcas (0x47) y oficina del Gobernador (las misiones 0x4B y las reuniones 0x4A); el menú lateral
  como 0x5A9780; el texto 0x6A9680 de la oficina tras una misión fallida (15-11-2026, estado `failed-task`). **La
  puerta de la ciudad** (.let 0x2F, 15-11-2026; `re/emu/sesion37/notes_gate.md`): su menú lateral como el del juego
  (0x5ABFA0 con sus condiciones de asedio y la nota bajo el título, `town_gate_window.{hpp,cpp}` con su ctest) y su
  página «Ruta terrestre» (0x5F2A20, 0x5F0DF0: los materiales de la carretera, la 0x3B; estado `road-task`); el panel
  de la Hansa propio del visor, quitado a petición del usuario. **El cliente de un pedido** en la taberna (15-11-2026;
  `re/emu/sesion37/notes_client.md`): su botón con su apellido, su página 0 (el saludo, lo que hay, la cantidad,
  «Entregar suministros»: la 0x51) y la página 16 tras la entrega en sus cuatro casos (todo o parte, por unidad o con
  prima), como las del juego (`tavern_order`, `order_client_stock`, `order_delivery_operation` con su ctest; estados
  `order-client`, `order-premium`). **Los asedios** en la puerta (15-11-2026; `notes_gate.md`): sus siete
  páginas como las del juego (Presentes 0x88, Venta de mercancías 0x89, Contratos de entrega 0x8A, Préstamo 0x8D,
  Atacar, Rendirse y Comprar libertad 0x90, con sus mensajes; `town_gate_window` con su ctest; estados `siege`,
  `siege-prince`, `prince-loan`) y la puerta cerrada del asedio en su interior. Los restos pequeños de las vistas,
  hechos el 15-11-2026 (`re/emu/sesion38/`). (Mudar la ciudad natal, 0x5DD520, quedó
  hecho en la página «Trasladarse» de la oficina, 14-11-2026.)
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
- [x] **Los eventos que crean**: todos (13-11-2026, el último el 0x11: op 0x3D, 0x4E6744, «Deshonrado», «Cumplido»). Hechos el 0x0C, el 0x0F,
  el 0x10, el 0x13 (op 0x5F, la oferta), el 0x17 (op 0x72, 0x4E8684: «Propuesta») y el 0x2D (op 0x35, 0x4E2BC4: la puja
  automática). Aparte: 0x14 (op 0xA4, la sala de la partida en red), y 0x0B (0x4E37F4), 0x29 (0x4DDE70) y 0x28
  (0x4DDF40, cuyo único creador, 0x538DA0, es código muerto), que no crea nadie.
- [ ] **La ventana del final de la partida**: la clasificación de los comerciantes (0x4220B0 → 0x4220D0, sobre
  *0x6CBB30, con el título 0x4F9430 y 0x4F73D0). El objetivo de la partida (evento 0x0E) ya se comprueba.
- [x] **Las ventanas de operaciones que el motor ya tiene** pero que el visor aún no encolaba (14-11-2026, noche): 0x61
  dar una fiesta (su ventana 0x5BAE40, en los puestos del mercado) y 0x0A borrar un barco (no es una ventana: el doble
  clic sobre un barco perdido, estado 0x11, en la lista de barcos, 0x4768B0; en el visor, su botón de la fila, X o un
  segundo toque en ella; la lista entera como la ventana del juego desde el 17-11-2026).
- [ ] La ventana de los desarrolladores 0x419EB0 encola de todo y manda dos cartas (0x45 y 0x21 con `rand()`): no hace
  falta salvo que se quiera.

Cada grupo entra en el visor con su edificio (apartado 4): sin la pantalla no se puede usar.

### 2. Lo que queda del motor

- [ ] **Una diferencia en las batallas a los ~50 días** (20-11-2026, encontrada al pasar las misiones a 60 días,
  `re/emu/sesion48/README.md`): en la partida 4, la operación 0x94 (los pasos de las batallas, 0x536BBF) del tick
  126953125 deja distintos los campos +0x8, +0xC y +0x10 de un bloque de 0x164 bytes de la batalla (bloque 44 de la
  región BT); sale igual sin las misiones. `world.sh` mira 7 días y no llega. Buscar qué es ese bloque y qué paso de la
  batalla lo escribe distinto (`worldmd.sh` con `FUNCS=4d85c0,535760 SAVES=4 MISSIONS=0` lo repite en ~150 s).
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
  (0x4ECF64: 0x28, 0x7C, 0x7D y 0x82; la 0x8E de la instrucción 0xFA, hecha); los gráficos de la ciudad (0x599040,
  desde el evento 0x25 y la muralla: 0x08, 0x09, 0x0A, 0x2F, 0x39, 0x52, 0x56–0x59, 0x61 y 0x93); y 0x5C7810, desde los
  objetos del mundo (0x33, 0x34, 0x36, 0x37 y 0x98). Mirar si alguna de esas ramas es simulación.
- [ ] **La partida nueva**: 0x545650, que la ventana llama al cambiar el año (21 ciudades en 1300 y una más cada 20
  años: rehace los huecos y sortea las producciones; sin leer aún cuándo se usa: las partidas nuevas de otros años ya
  salen iguales al juego). 0x4F99B0 (una ciudad sin plantilla de mapa) no se usa con los datos del juego.
- [ ] **El encargo de fundar una ciudad**: se cumple cuando la ciudad tiene mercado, y eso solo lo da el evento 0x2F con
  su primer edificio municipal y más de 1000 habitantes. En el emulador (`grow.sh`, partida 1) la ciudad se queda en
  272 habitantes hasta el día 520, y el plazo es de unos dos años. Medir más allá de 550 días (`--desde`) y ver si en
  el juego de verdad crece porque alguien le lleva mercancías. La ciudad nueva, sin probar en el visor de la Vita.
- [ ] Partidas de la versión 0x4D (`missav/0100–0204.pat`, de las que parten las campañas, apartado 3): el lector
  solo lee la 0x82 y posteriores.
- [ ] El objeto opcional del mundo (0x533DA0): ninguna partida lo tiene y p3vita lo rechaza al leer. Solo si aparece
  una partida que lo tenga.
- [ ] Sin comprobar porque no se pueden preparar: las dos persecuciones que alcanzan sin hueco para la batalla (harían
  falta 255 batallas abiertas).

### 3. El intérprete de misiones

Las misiones son programas pequeños (`missions_addon/*.p2m`, 94 ficheros, con los textos de sus cartas dentro, en
inglés y en español) que `scripts/missions_eng.ini` reparte por modos de juego: en la partida de un jugador, los 48
sucesos históricos (1305..1669) y las 18 misiones que van saliendo (la tripulación borracha, la boda, el contrabandista,
el mapa del tesoro...); en red, además, el duende y 5 de competición; y las campañas. Cada guion es un evento 0x1B que
ejecuta el intérprete 0x4ECF64, una instrucción por sección. Detalle: [formats/missions.md](formats/missions.md).

- [x] **La base** (19-11-2026, `missions.cpp`, `mission_script.cpp`; `re/emu/sesion45/`): leer el ini y los `.p2m`
  (los españoles con el archivo español), la lista de misiones del juego (*0x6DD8B4: 0x511270, 0x5110E0, 0x5116F0,
  0x511690, 0x4F37E0), la operación **0xC0** entera (0x543810: los rivales de 0x532420, con sus fallos —corregidos
  detrás del ajuste, como los otros dos de las misiones, abajo—, la cuenta atrás de
  los asedios de 0x635170, las misiones del modo con sus eventos 0x1B), cargar y guardar los guiones en curso con sus
  variables, y el bucle del intérprete con sus esperas (0x00, 0xFD, 0xFE, 0xFF). Comprobado: la carga con las misiones
  igual al juego en las 12 partidas (el oráculo con `--misiones`: cada partida lleva de 49 a 75 guiones), el escenario
  `mission_start` (la 0xC0 en todos los modos), la partida nueva entera con la 0xC0, las partidas escritas, ctest
  `missions`. Hasta que estén sus instrucciones, un guion termina en la primera que no está (como antes, que se
  tiraban), y el oráculo y las comprobaciones de siempre siguen sin las misiones.
- [x] **Las instrucciones del intérprete**: **las 138 con manejador** (20-11-2026) (tabla en formats/missions.md, con cuántas
  misiones usan cada una). Hechas (20-11-2026, `re/emu/sesion47/README.md` y `re/emu/sesion48/README.md`): 0x04..0x07,
  0x09..0x0E, 0x11, 0x12, 0x15, 0x17..0x1E, 0x20, 0x21, 0x23, 0x24, 0x26/0x27, 0x29 (con 0x533210, la ciudad más
  cercana), 0x2F (la mercancía que más le falta a una ciudad, 0x51BEB0), 0x35, 0x37 (la ventana de un suceso
  histórico: de la interfaz, para el visor), 0x38, 0x39, 0x3F, 0x40, 0x43, 0x44, 0x45, 0x4B (un precio de venta), 0x4F,
  0x53, 0x6B (el nivel de la muralla), 0xF0 (un tesoro encontrado: `treasure_found`, 0x4D7030), 0xF7 (la carta de
  misión, con su texto y sus códigos «%»: `mission_letter_text`), 0xFA (un asedio: encola la 0x8E), 0xF8 (dinero a
  un comerciante), 0xF9/0xE9/0xE7 (encolan la 0x82, el ataque pirata de una misión), 0xE8 (la línea de noticias de un
  comerciante), 0xF1..0xF4 (las cargas que no son mercancía de un barco, su [+0x157], su [+0x3F], su carga) y 0xF6 (la
  carta del mapa del tesoro, 0x45 o 0x46), 0x3A (lo que retienen las misiones de un barco), 0x3B, 0x28, 0x2A, 0x42
  (los marineros, el dueño, el sitio y el casco de un barco), 0x51 (hundir el pirata de una misión) y 0x6E (el barco
  con más sitio de un comerciante en una ciudad), 0x22 (un número por debajo de 100), 0x1F (un barco cerca de un
  sitio del tesoro), 0x2C (el estado del capitán pirata de una misión, 0x4D7770), 0x41 (el casco de un barco),
  0x46 (el evento de un encargo), 0x47 y 0x48 (las existencias de una oficina y su [+0x2E8]) y 0x49 (lo que valen unas
  mercancías para los piratas), 0x4C y 0xEE (meter o sacar existencias de una ciudad o de una oficina), 0x4D (un
  float del comerciante), 0x4E (la bodega ocupada), 0x50 (las sumas de las cartas 0x72 de un comerciante desde una
  fecha, 0x531260), 0x54 (un barco que pueda guiar un convoy), 0x61 (el [+0x16] de un comerciante), 0x69 (un evento
  0x0F), 0x6A (el comerciante n-ésimo con oficina en una ciudad), 0x6C (un bit de un barco), 0x6D (el barco mejor
  armado) y 0x6F (la ciudad siguiente de un barco, con 0x533210); las de la partida en red: 0x08 (el humano que va
  primero), 0x2E (una reputación), 0x36 (un barco al azar de un comerciante), 0x3E (la carga de un barco), 0x56 (la
  ciudad de la Hansa), 0x57 y 0x5A (el barco más rápido o con más de una mercancía), 0x58 y 0x59 (las variables por
  número) y 0xEB (los marineros de un barco); las de las campañas: 0x01, 0x2D, 0x31 (datos de un comerciante), 0x03,
  0x33, 0x2B, 0x70, 0x72 (sus barcos), 0x34, 0x60, 0x62, 0xE6 (datos de una ciudad), 0x5B (la ciudad más cercana por
  mar), 0x5C (buscar en la crónica), 0x5D (vender un barco, con 0x509250), 0x5E, 0x5F, 0x63 (los edificios de una
  oficina: contarlos, venderlos con 0x5256F0, ponerlos a la venta con la 0x28), 0x64 (marcar barcos de la IA), 0x65 (una
  ciudad por su nombre), 0x66 (el barco de un capitán pirata), 0x67 (dar un barco, con 0x50E000), 0xE5 y 0xED (un
  incendio: la 0x7D, con 0x525B90), 0xEC (la peste: la 0x7C), 0xFB (el evento 0x09) y 0xEF (una ventana); y las 19
  que no usa ningún fichero (0x02 0x0F 0x10 0x13 0x14 0x16 0x25 0x32 0x3C 0x4A 0x52 0x55 0x68 0x71 0x73 0x74 0xE4 0xEA
  0xF5). **Comprobado: el
  mundo entero 30 días con las misiones igual al juego en las 12 partidas**, llamada a llamada
  (`re/emu/sesion48/worldmd.sh`, por tramos), y la cola de eventos y las operaciones 60 días (igual salvo una
  diferencia de las batallas en la partida 4 que sale también sin las misiones: apartado 2); 7 días también en continuo
  desde la carga (`worldm.sh`); tick_probe marca las llamadas con una instrucción sin escribir. Son todas las que
  ejecutan las partidas en 60 días (`opcount.sh`). Las ramas a las que no llegan las partidas, con guiones propios
  en un estado retocado (`retouch_unchecked.py mission_branches`, partidas 3, 5 y 7: 0x07, 0x26 con todos los
  humanos, 0x27, el paso errante de 0x29, los modos de 0x40 y 0x4F, 0x44 con varias oficinas, las ramas de 0xF7 y
  todos los códigos «%» de sus cartas; `mission_ships_money`, `mission_ships_state`, `mission_misc`,
  `mission_last`, `mission_network`, `mission_campaign_a`, `mission_campaign_b` y `mission_unused`, partidas 1, 3, 5
  y 7: las noventa y tres nuevas con sus ramas): iguales (`docs/oracle.md`). Sigue lo que cuelga del intérprete (abajo);
  luego, el oráculo con las misiones por omisión (y `world.sh` con ellas).
- [ ] **Lo que cuelga del intérprete**: el evento 0x09 está hecho desde el 20-11-2026 (0x4E2824, la buena o mala
  cosecha de una ciudad que crea la instrucción 0xFB: la marca de la ciudad [+0x2C8] 0x4000 o 0x2000, las cartas 0x08 a
  los dueños de las oficinas que producen trigo, miel, vino o cáñamo y 0x09 a la crónica, al empezar y al acabar, y la
  vuelta al año siguiente; `events_towns_detail::fn_4e2824`, comprobado con `harvest_event`); la operación 0x82, el
  ataque pirata de una misión, también (20-11-2026, `missions_detail::fn_53fb00`, comprobada con `mission_attack`);
  las demás que encola el intérprete, 0x28, 0x7C, 0x7D y 0x8E, también; las cartas que compone, la 0xF7 con sus textos,
  la del mapa 0x45/0x46 y la 0x22, también; y las cuatro operaciones del jugador sobre sus cartas (20-11-2026,
  comprobadas con `mission_letter_ops`): 0x74 (los botones de las cartas de misión 0x3C..0x40: la respuesta a la
  variable del guion y la carta sin plazo, `missions_detail::fn_53ec30`), 0x54 (quitar una carta 0x44 y apuntar el
  barco en el guion, 0x53CA30 con 0x4D7340; las cartas 0x44 solo las crea código muerto), 0x55 (pagar el trozo de mapa
  del tesoro, 0x4D74C0) y 0x76 (aceptar la carta de misión de la taberna 0x71, 0x53EDF0 con 0x4D7870). Falta la carta
  final 0x3D del evento 0x34 (0x4F3AF0, con su lista de avisos [+0x8], 0x4F3980..0x4F3CE0), que es de la dieta (la caza
  de piratas, `diet.cpp`) y no de las misiones, pero es una carta del tipo de las de misión (registro y texto).
- [ ] **En el visor**: las cartas de misión con sus botones (0x74), la carta de misión de la taberna (0x5D8100, las
  0x71), el trozo de mapa del tesoro (0x55 del soplón), lo que enseñe cada misión (la ventana de los sucesos
  históricos de la instrucción 0x37: 0x469AF0 con 0x16, el título y el texto del programa).
- [ ] **Las campañas y el tutorial**: su menú de inicio (`[Campaign_0..7]` del ini: el tutorial, «El ascenso», «El
  comerciante volante», «El auge de la Hansa» y los escenarios del saneamiento, la mala cosecha, el incendio y la
  peste) y las partidas de la versión 0x4D de las que parten (`missav/0100..0204.pat`, apartado 2); el tutorial va
  aparte (`scripts/tutorial.ini`, `missav/Tut*.pst`).

### 4. Las pantallas

Cada pantalla se adapta a la Vita y se comprueba a ojo frente al juego original (capturas del juego en el PC), con sus
gráficos y sus textos; las reglas de lo que enseña y las órdenes que encola, contra el juego como siempre. Las normas
del visor (tocar los edificios, táctil primero, las ventanas emergentes), abajo en «Decisiones del usuario».

- [x] **Las ventanas que siguen siendo del visor** (acabadas el 20-11-2026, salvo la del final, que va con el final de
  la partida; 19-11-2026: el usuario vio la de comerciar, «no guarda la estética
  del resto», y pidió rehacerlas como las del juego; inventario en `re/emu/sesion45/README.md`, capturas
  `re/emu/sesion31/shots/v1..v8.png`). Con su recuadro oscuro y la letra del visor:
  - [x] **La ventana de comerciar** (19-11-2026, `re/emu/sesion45/README.md`): como la del juego (*0x6D9584, su dibujo
    0x4A7390 trazado en el emulador): el pergamino con el dibujo de los diálogos, las dos imágenes de los lados, el
    título, la línea de información, la tabla [CHART1] con los botones de comprar y vender de cada fila con sus precios
    como 0x4AAC00, los botones de la cantidad «1», «5», «50», «Máx.», la mano con la moneda y el precio del último
    trato y el botón de cerrar; en todos sus modos, en los dos idiomas (tres textos emparejados: caché 42). La tabla,
    adaptada a petición del usuario: con el mismo margen a los dos lados (la del juego llega al marco) y, en el modo de
    las armas, sin la columna del precio medio para que quepan los nombres. Lo que quedaba, leído en el juego y
    trazado (19-11-2026, `re/emu/sesion46/README.md`): los botones de mover con la oficina como los del juego («<» de la
    oficina al barco o al convoy en la columna de comprar, «>» al revés), la mano de pagar (su fotograma 1) y la de
    cobrar (el 0), la moneda detrás de cada precio medio, el precio medio del convoy (una media ponderada por sus
    barcos, 0x4AA4A0, comprobada en el emulador: `market_convoy_averages`), la carga de la oficina con su [+0x2E8], la
    cubierta del barco con sus piezas en el modo de las armas (0x495210). Adaptado: el juego enseña abajo lo que
    costaría o daría el botón que tiene el ratón encima (y en rojo si no hay dinero o sitio); sin ratón, el visor enseña
    lo que costó o dio el último trato.
  - [x] **El menú de Start** del visor (19-11-2026, `re/emu/sesion46/README.md`): ya es la pantalla de opciones del
    juego (*0x6CBDF0, options.ini, a pantalla completa: la abren Start, el engranaje del panel y las «Opciones» del
    libro), con su pregunta para abandonar; sus dos primeras filas son el idioma y los ajustes de los fallos del visor,
    las que el visor aún no hace salen con sus botones apagados. Y su pantalla de partidas (*0x6CBB7C: **guardar y
    cargar**, con sus fondos, la lista, los detalles de la partida elegida y sus preguntas): las partidas, por nombre
    como las del juego (decisión del usuario, 19-11-2026: `savegames/ein/NOMBRE.pat`; los huecos viejos siguen en la
    lista; el nombre, con el teclado de la Vita; con muchas, la barra de desplazamiento del juego). Las campañas, con
    su botón apagado, llegan con ellas (apartado 3).
  - [x] **Las ventanas de una batalla** (19-11-2026, `re/emu/sesion46/README.md`): la pregunta, el final, el abordaje
    y los mensajes de las batallas en la ventana de mensajes del juego (*0x6CBB10, 0x41D9B0) con su descripción
    (oficial, con los rollos, la proporción 0,7071); la del icono del mapa del mar como la del juego (no oficial, ancho
    fijo 200, proporción 1,375, sin botón: un fallo del juego, abajo); la de dejar la batalla (del visor) con la misma
    ventana. Se quedan, por decisión del usuario del 08-11-2026, el segundo botón de la pregunta («Que luche el
    ordenador»: en el juego, solo el cierre) y su barra del tiempo (improvisada sobre los botones). El abordaje que
    dirige el jugador ya no sale dos veces (la ventana de la batalla y el recuadro de mensajes).
  - [x] **Los avisos** de arriba a la izquierda y **el nombre del edificio señalado** (20-11-2026,
    `re/emu/sesion47/README.md`): ya son las líneas de noticias de la pantalla del juego (*0x6CBB40, 0x42B4C0,
    0x42B6A0, 0x42BB20, su dibujo 0x421040): dos tandas de cinco tablillas de images/text.aim colgadas con sus ganchos
    bajo la barra de arriba, cada una 12 s con su texto negro en la fuente 0; las del juego («El Hyphen está siendo
    atacado», partidas en trozos si no caben) a la izquierda de la vista y las de las cartas («Información comercial:
    Factura») a su derecha, con el botón de los mensajes parpadeando 4 s (NewsAnimID 78). Debajo de las ventanas, como
    en el juego. El nombre del edificio, con la ayuda emergente del juego (la cadena 30000 + su tipo, 0x593C50: textos
    caché 45), en el cursor tras 250 ms quieto; todas las ayudas emergentes del visor, con el recuadro del juego
    (blanco con alfa 200, borde negro, fuente 0: 0x4B5A90). Quitados los avisos del visor que el juego no tiene (lo
    comprado o movido en el mercado, «sigue la ruta», «zarpa hacia», «Partida guardada», el convoy deshecho, el
    objetivo elegido, la tripulación contratada); la velocidad de L/R, en la ayuda del reloj de arena; el aviso de una
    oferta desde los barcos, en la ventana de mensajes del juego (0x5BE1A6). Se quedan como líneas los avisos del visor
    que hacen falta (una orden del mapa del mar que no se puede, la carga del mapa del mar, la elección de Gobernador
    hasta el final de la partida).
  - [x] **El pie del mapa del mar** (20-11-2026, `re/emu/sesion47/README.md`): quitado, como en el juego. El barco al
    que van las órdenes es el del menú del barco del panel (Cuadrado elige el siguiente y abre su menú, que dice qué
    hace) y la velocidad sale en la ayuda del reloj de arena. Los dos botones de la ruta por puntos (del visor: el juego
    la marca con el ratón), como botones del juego con su tecla dentro, solos y discretos abajo en el centro (el usuario
    vio un papel de diálogo debajo «demasiado cantoso»): «○ Borrar» a la izquierda y «X Zarpar» a la derecha; con la
    ruta empezada, la barra de arriba no dice X ni ○.
  - [x] **El buzón a pantalla completa** (20-11-2026, `re/emu/sesion47/README.md`): quitado. En el juego no hay buzón
    durante una batalla (su panel, el de la batalla, no tiene el icono de los mensajes): solo el reloj de arena de la
    barra abre la crónica, en su ventana (0x47DD40). En el visor, igual: sobre una batalla, la crónica en la ventana de
    mensajes del juego (la misma que en la ciudad), que se abre desde arriba cada vez y se cierra con Círculo o tocando
    fuera de ella (sin dar órdenes a los barcos). El aviso «Cargando la batalla...», como línea de noticias mientras
    cargan los barcos (como la carga del mapa del mar).
  - (La ventana del final, `draw_end_window`, va con el final de la partida.)
- [ ] **Las vistas de dentro de los edificios** (lo pidió el usuario el 05-11-2026; en marcha desde el 13-11-2026 con
  el encargo de las vistas, acabado el 14-11-2026, después con `ENCARGO_GRUPO5_VISOR_2.md` y `ENCARGO_VISTAS_RESTOS.md` (acabados el 15-11-2026); lo leído del juego,
  en `re/emu/sesion31/views_notes.md`, y las fotos de la
  pantalla del juego que mandó el usuario, en `re/emu/sesion31/pc_shots/`).
  - [x] **El marco común** (`src/app/game_window.cpp`): la ventana del juego entera (451 × 537, como 0x465BA0) dibujada a
    su tamaño en una textura y ajustada al alto de la Vita (×0,95), centrada en la vista de la ciudad: el marco de
    madera, la imagen interior de cada edificio, el pergamino desde la línea de cada página ([+0xEC]) con sus 160
    líneas de difuminado, la banderola del título, el botón de cerrar, los botones y el papel del juego. Sin las
    cadenas (no caben). Las fuentes del juego español (las TTF de `ttffonts.ini` con su ancho y alto, medidas contra
    las fotos).
  - [x] **Las animaciones de los interiores** (`animation.ini`, 0x466800): las que arranca cada edificio al abrirse y
    los gestos al azar de cada uno, leídos del juego (14-11-2026, `re/emu/sesion34/notes_gestures.md`): el
    ayuntamiento, la taberna, el gremio, el prestamista, la oficina, la lonja, el baño (con quién está en el baño, que
    su menú decide al entrar: el jugador y cada concejal con un 7 %), la caballeriza (el caballo) y la puerta de la
    ciudad (el guardia, o la puerta cerrada en un asedio); la iglesia no tiene gestos al azar: sus estatuas, velas y
    altar salen según lo alimentados que estén sus pobres. La gente de la taberna no se dibuja aparte: ya está en la
    imagen y sus gestos salen esté quien esté, como en el juego. Las tiras de más de 4096 píxeles (el prestamista, la
    oficina, la lonja, el baño, el fuego del gremio, el agua del astillero), que la Vita no admite en una textura, van
    ahora con una textura por fotograma, y las imágenes sin canal alfa (el vapor y el agua del baño) se ven. La puerta
    de la ciudad ya se abre (15-11-2026), su guardia hace sus gestos y, en un asedio, sale cerrada con el capitán.
  - [x] **El menú de cada edificio en el panel lateral** (el rollo de pergamino, el título en Elgreco, los botones):
    al entrar se ve la imagen sola y la página se elige ahí; en todos los edificios desde el 14-11-2026 (la lonja, el
    baño con un botón por concejal en el baño, la oficina, la caballeriza, el muelle con el menú del astillero y sus
    textos; la oficina de otro comerciante no tiene menú en el juego), cada menú con su primera posición y su paso,
    como el código del juego (`re/emu/sesion34/notes_menus.md`; antes todos usaban el de la taberna). Los botones de
    las páginas que el visor aún no tiene, atenuados. Con los botones de la consola (14-11-2026, como aprobó el
    usuario): arriba y abajo de la cruceta eligen el botón del menú cuando la página no tiene filas propias (o se ve
    la imagen sola), y L y R cambian la velocidad del tiempo (más lenta, más rápida; el aviso de la velocidad nueva
    sustituye al anterior). La ventana de dos barcos pasa al otro barco con Select (o tocando su título).
  - [x] **Páginas ya como las del juego**: los avisos del ayuntamiento (un aviso cada vez, en su papel); en la
    taberna, los retratos con su cara animada y su marco rasgado, y las páginas del pirata, los marineros (con su
    escupitajo), el capitán, el informante (con su nombre en el título), el viajero, el ladrón (con su lista de
    víctimas en la misma página), el comerciante y el traficante de armas (su tabla del juego; la fila del barco, que
    el código pone encima de la tabla, subida porque no hay foto: decisión del usuario, improvisar), y las de después
    de cada trato (capitán contratado, pirata, ladrón, viajero, comerciante); los gestos al azar de la gente de la
    taberna; el texto partido como la caja del juego (sus saltos y la línea en blanco tras el saludo), centrado bajo el
    retrato cuando es más estrecho (lo pidió el usuario). El prestamista: su menú lateral y sus seis páginas como las del
    juego (la de conceder préstamo y la de vender acciones, comparadas con las fotos), con la del interés (la 9 del
    juego) detrás de «Ofrecer préstamo». Los menús
    laterales con la letra de sus botones (la 1) y colocados como los del juego (13-11-2026, noche; commit 8d31da6), y
    los de la iglesia, el gremio, la herrería y el astillero (los botones de las páginas aún por hacer, atenuados). Las páginas de
    la herrería (armas de mano, como la foto, y de barco) y de la iglesia (alimentar a los pobres, como la foto,
    donaciones y ampliación, con el retrato del cura) (13-11-2026, noche; commit 2aa4ac2). El gremio (14-11-2026): su
    menú lateral con sus 7 botones; «Miembros» como la foto (la lista contra el juego: escenario `guild_members`);
    las subastas previstas en sus tablas de madera colgadas, con «Aviso» y el barco a inscribir; la sala de subastas
    (la lista de lotes, el lote en puja con el libro de barcos, las pujas); el fin de la subasta con lo vendido (que el
    evento de la subasta deja en `SaveGame::auction_results`, como el juego en su ventana); la bienvenida al ingresar;
    los convoyes públicos (su orden contra el juego: escenario `guild_convoys`) y los contratos del Mediterráneo. Y,
    en todas las páginas, la caja de texto del juego con sus líneas de 15 (antes 20) y los títulos de los avisos
    centrados. Las páginas sin foto, con la disposición que da su dibujo ejecutado en el emulador
    (`re/emu/sesion33/drawtrace.py`). El 14-11-2026 (commit e3bd503, `re/emu/sesion34/`): en el gremio, las «Rutas
    comerciales» (el mapa con la niebla de lo no descubierto, hecha como el juego, 0x534A40, comprobada contra él; un
    enclave elegido tocándolo, con quién lo descubrió y qué pide y ofrece), el dibujo del edificio en su subasta
    (sacado de la vista de la ciudad del visor), la media bala de cañón de los convoyes, la sala de subastas de la
    partida en red (0x5C3640, solo en ella) y los contratos con su frase en el orden del exe español; en la iglesia,
    «Rezar» (la imagen sola, como el juego, sin el sonido aún), las caras del cura (habla y da las gracias tras un
    donativo; con altar cuando los pobres están bien alimentados) y las dos páginas de después de donar; en el
    prestamista, «–» y «+» del interés en su sitio; en la taberna, el texto del informante con la barra del juego. La
    lonja: sus tres páginas de información («Mercancías» con lo que cuesta comprar y vender una unidad, «Producción
    semanal» de la ciudad y de las oficinas, «Consumo semanal» de los ciudadanos y las empresas;
    `market_hall_window.cpp` con su ctest; las cifras, iguales a las del juego en el emulador en la partida 3). La
    oficina (14-11-2026, `re/emu/sesion34/notes_office.md`): sus siete páginas como las del juego: el balance de la
    semana con su previsión, los edificios y el administrador (contratarlo o despedirlo con la pregunta del juego;
    cifras iguales a las del juego en la partida 6), las mercancías (en rojo las que no llegan a la semana y las de
    empresas sin obreros), las armas, el almacén con sus guardias («–» y «+», como el juego; sin el «Comerciar» que el
    visor tenía: se comercia desde el mercado, como en el juego), las órdenes del administrador en una tabla (precio,
    existencias a guardar y candado en cada fila; sus columnas, improvisadas), la ficha personal (con el cónyuge y los
    hijos, si los hay) y el traslado de la ciudad natal (0x3E). El baño (el baño con la frase por los días desde el
    último, cada concejal con su saludo según con quién va, la caja del soborno con sus flechas y su respuesta) y la
    caballeriza (las 20 mercancías con su «–» y «+», la escolta con sus costes y su consejo, el convoy enviado; los
    botones del paso 1, 5, 50 y todo, improvisados).
  - [x] Las ventanas de papel del juego sobre la ciudad (14-11-2026, noche; su clase base 0x41C5A0 / 0x41CAA0: el rollo
    TEX16001 en baldosas de 32 con los rodillos, sorteadas como el juego y guardadas por ventana): la oficina de otro
    comerciante (los mercaderes de la ciudad ordenados por apellido como el juego, con sus casas y edificios; la carta
    de la oferta con «Oferta», la cantidad y el precio —hasta 1 000 000, como el juego, no 9 999 900— y su lista
    «Escoge oferta» encima: oro, mercancías con sus unidades, barcos; se cierra al enviarla), la armería (su tabla,
    el coste semanal con la moneda, el aviso, «Formar unidad» y «Despedir unidad» encendidos como el juego, ninguna fila
    elegida al abrir) y la fiesta (los puestos del mercado, .let 0x06: la fecha propuesta por la tabla de la ciudad, el
    coste, «Anunciar» → 0x61, y el texto de la fiesta ya anunciada). Comparadas con la foto de la armería. Improvisado:
    el sitio de la lista «Escoge oferta» (el juego la pone según la pantalla) y los nombres largos de mercancías en
    español, cortados antes de sus unidades (el juego los monta encima). Notas: `re/emu/sesion35/notes_paper.md`.
  - [x] El astillero y el muelle (14-11-2026, noche): sus seis páginas como las del juego, trazadas en el emulador: el
    libro del barco con sus esquinas y la fila de cifras con sus dibujos; reparar (las sumas de un convoy), encargar
    (la tabla de materias primas, y tras «Ejecutar» la página del encargo con sus fechas), la lista de contratos (con
    la pregunta del juego antes de cancelar), la lista de reparaciones (nueva: «Parar reparaciones», la 0x00), mejorar
    (el nivel entre «-» y «+», su tabla) y el nombre; la barra de las listas largas; la ventanita de la pregunta con su
    tamaño del juego (15-11-2026, 0x41D9B0 trazada en el emulador: el ancho por el texto con la proporción 0,6, sus
    botones por el ancho de su texto y el de cerrar; centrada en la ventana del edificio y no en la pantalla). Notas: `re/emu/sesion35/notes_shipyard.md`. El
    fundido del libro entre dos barcos o dos tipos (15-11-2026, `re/emu/sesion38/`): medio segundo, el anterior
    debajo y el nuevo encima, como el juego (comprobado en el emulador; su fallo de los títulos, abajo).
  - [x] Las siete páginas del ayuntamiento que faltaban (15-11-2026; `re/emu/sesion36/notes_townhall.md`).
  - [x] El cliente de un pedido en la taberna (15-11-2026; `re/emu/sesion37/notes_client.md`). Improvisado (sin foto):
    en español «Mercancía» llega más allá de «Almacén» (el juego los escribe uno encima del otro): cada cabecera
    empieza al menos 6 después de la anterior; y una frase española más larga que su sitio pasa a la línea siguiente y
    baja las de debajo.
  - [x] Los textos centrados («\c»): todos los de las vistas de los edificios salen centrados (revisado el 15-11-2026
    con `re/emu/sesion38/cscan.py`, en los dos exes; las cartas y las batallas ya siguen su alineación).
  - [x] Las preguntas del convoy con la ventanita de mensaje del juego (17-11-2026, `re/emu/sesion39/`): el botón
    «abandonar convoy» del menú del barco no abre página, pregunta (0x494DC0: «Deshacer convoy» si el barco es el
    guía, si no «Abandonar convoy», con «Sí» y «No»; «Sí» encola la 0x16 o la 0x17, 0x4950D0), y «formar convoy» sin
    nada que elegir no abre la ventana del convoy, avisa (0x40C470: «No es posible formar un convoy», con la imagen de
    lo que necesita el guía, `konvoi_min.tga`). Las dos sin rodillos, la pregunta con la proporción 0,4 y el aviso con
    1,375, centradas en la pantalla (`game_message_layout`, `convoy_question`, `convoy_cannot_form`); sus tamaños y
    sitios, iguales a los de 0x41D9B0 en el emulador con las dos proporciones, en la pantalla del PC y en la de la Vita.
    La caja de texto de la ventanita sigue ahora la alineación del juego («\c», «\l», «\r») y pone a su tamaño una
    imagen sola en su línea (0x462F00). Arreglado de paso, en todas las pantallas: las fuentes de las ventanas del
    juego se cargan la primera vez que se piden (una ventanita dispuesta antes de dibujar ninguna se medía sin ellas)
    y los botones de la ventanita crecen para su tecla, como los de las páginas (también «○ No» del astillero).
  - [x] La sombra del edificio en su subasta (15-11-2026, `re/emu/sesion38/`): la imagen de la sombra del `.let`
    (sus variantes ya se leen: `docs/formats/town.md`) en negro con alfa 100 bajo el edificio, como 0x551F70.
  - [ ] Por hacer como el juego: el sonido de rezar
    (con el sonido, apartado 5). Lo que listan y mandan (`player_actions.cpp` y los `*_window.cpp`) no cambia.
- [x] **La puerta de la ciudad** (.let 0x2F, `Stadttor`; 15-11-2026, `re/emu/sesion37/notes_gate.md`): abre su menú
  lateral como el del juego (ocho botones con sus condiciones, la nota bajo el título; la nota del juego, de tres
  líneas, pisa los dos primeros botones: el visor los pone debajo, algo más juntos, improvisado) y su página «Ruta
  terrestre» (los materiales de una carretera de la Hansa) y las siete del asedio (Presentes, Venta de mercancías,
  Contratos de entrega, Préstamo, Atacar, Rendirse, Comprar libertad), trazadas en el emulador (`gatetrace.sh`). (Los demás edificios con vista propia ya tienen
  la suya: el transporte por tierra resultó ser de la caballeriza, 13-11-2026.)
- [ ] **El panel lateral con las letras y los textos del juego** (con los restos de las pantallas, por decisión del
  usuario del 17-11-2026): hoy sus páginas se dibujan con la letra del visor. El encargo `ENCARGO_PANEL_JUEGO.md`
  tiene el detalle. Cómo (lo aprobó el usuario): cada página como la dibuja el juego, su composición trazada en el
  emulador con su propio código (`re/emu/sesion33/drawtrace.py`, que ya ejecuta el arranque de una ventana con sus
  `.ini`), con las fuentes del juego a su tamaño (el panel del juego mide unos 282 de ancho, el de la Vita 270) y la
  altura adaptada. Por partes y en este orden:
  - [x] La lista de barcos (17-11-2026, `re/emu/sesion40/README.md`; las reglas en `ship_list_window.cpp` con su
    ctest): la ventana de pergamino del juego sobre la ciudad o el mapa del mar (parchment.ini [ShipList0], [CHART2]),
    con su tamaño y sitio (0x4751D0), su título, sus dos tablas (los barcos propios; los de otros comerciantes en un
    puerto, con sus cifras aproximadas como el juego) con sus siete columnas en los dos modos (mercancías y valor en
    monedas, o tripulación con el capitán y cañonazos), los iconos de estado y de convoy (las banderas teñidas), sus
    cinco órdenes, sus botones (cerrar, columnas, «Ordenar por», y en cada fila «ir al barco» o «borrar» el barco
    perdido, 0x0A) y sus clics (elegir el barco, el doble toque); abierta desde el icono del panel y desde el **faro**
    de la ciudad (.let 0x15: los barcos de su puerto). Comprobada en el emulador (`shiplisttrace.sh`, los dos modos) y
    con capturas en los dos idiomas. Improvisado para la Vita: el botón de las columnas algo a la izquierda (la tecla ○
    del de cerrar), el desplazamiento por filas enteras, la fila de la cruceta como la fila bajo el ratón, un toque
    fuera la cierra, abrirla cierra la ventana de un edificio. Lo que queda de ella:
    - [x] Las aperturas del mapa del mar (17-11-2026): tocar barcos del jugador en el mar elige uno o abre la lista
      con ellos (0x44FB40, tipo 1, el primero elegido), y tocar una ciudad con sus barcos en el puerto elige uno o abre
      la lista de su puerto sin los ajenos (0x44F980; la ciudad queda elegida como antes). No está el recuadro
      arrastrado (0x44D780 → 0x44DC5F, todos elegidos a la vez): en la Vita arrastrar mueve el mapa; y tocar el mar
      vacío sigue añadiendo un punto de ruta (el botón derecho del juego en el visor) en vez de vaciar la selección.
    - [x] Elegir un barco de otro comerciante (17-11-2026): el menú del barco en su estado 3 como el juego, su cabecera
      con el dueño, la fila de iconos con las cifras aproximadas y el barco en un catalejo (0x491240, 0x491CA0,
      0x48D8C0); y un barco perdido del jugador en su menú de siempre (el estado 1).
    - [x] El convoy de otro comerciante (18-11-2026): un barco suyo en convoy elegido desde la lista enseña antes su
      convoy (0x495190: el estado 4, «Convoy …» con sus sumas aproximadas y el número de barcos, 0x48FE80, 0x4906E0,
      0x492150), tocado otra vez el barco solo; la lista marca entonces todos los barcos del convoy.
    - [x] Varios barcos a la vez (18-11-2026): una pulsación larga en una fila de la lista los añade o los quita, como
      el Ctrl del juego (decisión del usuario); el menú del barco enseña entonces su estado 0, la lista de los elegidos
      ([Chart0] de sidemenu.ini), con la barra del juego donde no caben.
    - [x] El convoy del jugador y el nombre del pirata (18-11-2026, `re/emu/sesion41/README.md`): el estado 2 del menú
      del barco (su cabecera «Convoy …» con las sumas del convoy, también la del equipo, y la lista de sus barcos; las
      mercancías del primero de la lista; su ruta; «deshacer convoy»), al que se llega como en el juego: tocando en la
      lista un barco suyo en convoy (otra vez, el barco solo), con los botones de convoy del panel o con «ver convoy»;
      los botones de cada estado como el juego (en el estado 1 con el barco en convoy, sin ruta comercial: es la del
      convoy); cada elección nueva vuelve a la página de mercancías (0x488080). El nombre del pirata (0x612EE0): el de su
      batalla, o el del capitán del barco o del guía de su convoy, o «Pirata desconocido» (texto nuevo emparejado;
      estado de depuración `raiding-ship`).
    - [x] La lista de barcos con el mando como con el dedo (18-11-2026; el usuario dejó la decisión a lo más usable):
      X elige el barco de la fila sin cerrar la lista, X otra vez va a él (el doble clic del juego), Select lo añade o
      lo quita de los elegidos (el Ctrl del juego); L y R, la velocidad, como en la ventana de un edificio. El recuadro arrastrado del mapa del mar no está: en la Vita arrastrar
      mueve el mapa (para la pasada de usabilidad, si hace falta).
    - [ ] «Ir al barco» centrado en su barco del puerto (0x476A30: la lista 0x6E2D50): va con los barcos en el puerto
      de la ciudad viva. (El de un barco en construcción, estado 0xE, ya centra el astillero de su ciudad.)
  - [x] El menú del barco: su cabecera y sus cinco páginas. **Hecha la cabecera del barco propio** (18-11-2026,
    0x48EE90 → 0x4916E0, trazada en el emulador): el nombre con sus marcas en la letra 2, la línea de estado como el
    juego (los días, el icono, el sitio), la fila de iconos con el borde rasgado del pergamino debajo, y el barril
    con la bodega y el equipo (el equipo abajo, en rojo solo la capacidad si se pasa); en la página de mercancías y,
    con el barco ya en convoy, en la de convoy. Los botones de las páginas a las x y tamaños del juego (0x493320: van
    en una fila 50 por encima del menú, donde en la Vita están los botones de barcos del panel: arriba del pergamino,
    improvisado). Arreglos comunes de paso (`re/emu/sesion41/README.md`, «Corrections»): la sombra de la tabla en lo
    alto de **todos** los menús laterales (TEX20005, que el visor no dibujaba en ninguno; se ve en la foto del
    ayuntamiento del usuario), el separador TEX20007 bajo la fila de iconos (antes la sombra: estaban cambiadas), las
    cabeceras 6 más arriba, y las texturas `Blendmode=multiply` de textures.ini (el pirata del catalejo salía como un
    cuadrado blanco). **Hechas también las páginas** (18-11-2026, `re/emu/sesion41/README.md`, «The ship menu's
    pages»): mercancías (0x48F510: cantidad, barril o fardo, mercancía y precio con su moneda, con la barra del
    juego), tripulación (0x48DF90: moral y sueldo, la fila de despedir marineros, las armas de mano, el capitán con sus
    habilidades y sus dos botones al pie; los botones con el ancho que da su texto), cubierta (0x48E700: la cuerda de
    las velas y la cubierta con sus piezas; algo más arriba para que quepa) y ruta comercial (0x48F2C0 bajo la cabecera
    del barco o del convoy: «Ruta comercial», «Archivo», «Activa» y las filas de las paradas). **Hecha la ventana de
    formar convoy** (18-11-2026, `re/emu/sesion42/README.md`): la ventana de pergamino del juego sobre la ciudad o el
    mapa del mar (*0x6CBA98: su disposición 0x40C5F0 trazada en el emulador, su dibujo 0x40CDA0): «Elige una
    acción», las opciones de 0x40C170 (convoy privado, público, unirse a un convoy de la ciudad con su fecha de salida
    y su destino si es abierto), «OK» y cerrar; el destino del convoy público en la lista de ciudades del juego (la
    ventanita 0x46F760 en su modo 1, con sus rodillos, que también usa ya la página de la ruta comercial para elegir
    la ciudad de una parada) y, después, el aviso del juego («El convoy público … zarpará en 5 días hacia …»). El
    estado 0 (varios barcos) con su fila de botones como el juego (todos apagados menos «formar convoy», encendido si
    todos están libres en el mismo puerto, 0x48FC00) y la cabecera del barco de la fila (la del ratón en el juego).
    Con el mando: Cuadrado pulsa el botón del convoy del menú (formar, o abandonar/deshacer: antes el mando no podía
    pulsar ninguno de los dos); en la ventana, arriba/abajo y X («OK»). Improvisado: un toque fuera la cierra, y
    abrirla cierra la lista de barcos (y al revés). Arreglado de paso en todas las ventanitas del juego: la barra de
    arriba enseñaba sus teclas debajo. **Hechas también las dos ventanas de la página de la ruta
    comercial** que eran del visor: la lista de archivos de rutas («Archivo»: la ventanita 0x46F760 en su modo 4, con
    «Cargar», «Guardar», «Borrar», el cuadro del nombre y sus preguntas de sobrescribir y borrar con la ventanita de
    mensaje del juego) y la ventana de mercancías de una parada (*0x6CBA74: el dibujo del juego con su marco y sus
    sombras, la tabla [CHART0] con sus 20 filas, la leyenda y la fila de abajo como el código y la foto del usuario;
    escalada al alto de la Vita, ×0,904, como las de los edificios: adaptado). Las listas de selección van donde las
    pone el juego, con su último cuarto sobre el panel (lo confirma la foto del usuario). Y el menú del barco
    en la batalla (18-11-2026): su página de cubierta como la del juego (la cabecera de la página de cubierta, no la
    vieja del visor), el botón de disparar en su sitio y la cuerda con sus tres marcas (el barco en las velas que
    lleva, las pedidas al 50 % hasta que llega, el nudo bajo el dedo); su fila de botones y «ver convoy», con el panel
    de las batallas (abajo). **El menú del barco está acabado.**
  - [x] El menú de construir (18-11-2026, `re/emu/sesion42/README.md`): como el del juego, trazado en el emulador: las
    cinco categorías repartidas en la fila de arriba, el título de la categoría en la letra 3, los botones de los
    edificios (Button4) centrados, **el aviso de la reputación** centrado en la caja de texto del juego, y la página
    de un edificio con su tabla (costes, duración, materiales con sus columnas y sus fardos y barriles, coste de las
    mercancías, el motivo en rojo, el porcentaje del pozo y de las calles, nuevo). En inglés la categoría 0 se llama
    «trading office» como la oficina: es el texto del propio juego. Al colocar un edificio con el mando, el joystick
    derecho lo mueve celda a celda (con la vista siguiéndolo) y el izquierdo mueve la vista: idea del usuario
    (18-11-2026), para aprovechar los dos joysticks de la Vita.
  - [x] Los márgenes de los botones (18-11-2026, lo vio el usuario en la ventana de la parada): los botones del juego
    tienen el ancho de su estilo aunque su texto no quepa («Máx.» en 32, «Deshaz» en 48, en español casi todos); en
    todas las pantallas, un botón cuyo texto deja menos de 6 por lado crece desde su centro hasta dejar libres sus
    extremos (adaptado; los redondos de «-» y «+» no cambian), y la fila de botones de la lista de archivos se recoloca
    y ensancha la ventana si no cabe.
  - [x] Las estadísticas (18-11-2026, quinta sesión, `re/emu/sesion43/README.md`): el menú lateral como el del juego,
    trazado en el emulador (las cuatro pestañas en la fila de arriba en el orden del manual: negocios, competencia,
    ciudades y comparativa; el título de la página en la letra 3; los botones de las entradas, Button4, centrados cada
    27, el de la entrada que se ve pulsado; el juego no enciende ninguna pestaña: la de la página, en su dibujo «encima»,
    adaptado como en el menú de construir) y su ventana como el pergamino del juego (16 × 17 baldosas con los extremos
    de los rollos, dibujada a su tamaño y ajustada al alto de la Vita, ×0,9375: adaptado): el balance con sus filas y
    sus fechas, las gráficas con sus ejes y su sombra, la rejilla y las fechas de cada periodo (días cada 5, semanas,
    iniciales de los meses, años), la escala como el juego (1, 2, 5, 10... con la «t» de los miles de oro y la unidad
    arriba; «mín.»/«máx.», «exp.»/«imp.»), las líneas de cada serie, los nombres de las series en su color, la leyenda
    con su moneda, barril, fardo o «%»; el suministro con sus columnas, sus dibujos y su lista de ciudades («Seleccionar
    ciudad», «<», «>» y el botón de ver la ciudad, que lleva a ella); los habitantes con su barra; los botones de
    mercaderes, ciudades y mercancías en columna como el juego (los de la competencia con su texto negro, como el
    juego: el color solo en las líneas) y la ventanita de selección del juego en sus modos 0, 1 y 2 («Nombre Apellido
    (Ciudad)», el jugador en azul, «Ninguno», el botón de ordenar en la línea del de cerrar). Con el mando, L y R pasan
    a ser la velocidad como en las demás ventanas sobre la ciudad; Triángulo cambia el periodo (en el suministro, la
    ciudad) y, en las listas, el orden; Cuadrado y X, los botones bajo la gráfica y su lista (decisión de manejo, para
    que las teclas coincidan con el resto del visor).
  - [x] Los mensajes en el panel (19-11-2026, `re/emu/sesion43/README.md`, «Part 2»): el menú lateral como el del juego
    (SIDEMENU_INFOS, trazado en el emulador: sus cinco botones en la fila de arriba —oficiales, personales, comercio,
    ajustes y borrar—, el título de la pestaña en la letra 3, las filas con la fecha, el asunto en negro si no se ha
    leído y en marrón si sí, y la ciudad, la banda de la carta que se ve y la barra); la carta y la crónica en la
    ventanita de mensaje del juego (proporción 1,375, con rollos las cartas oficiales y la crónica, las letras del juego
    en cada línea con sus capitulares y el mapa, sus botones con X y Triángulo, el de cerrar con Círculo y la barra si
    no cabe); la crónica aparte de las pestañas, con el reloj de arena de la barra de arriba, como el juego; y la
    ventana de ajustes de las cartas como la del juego (352 × 544, leída a mano: sus «−» y «+», sus casillas con la
    marca, la notificación de sucesos; al cerrarla se guardan y se miran las cartas, 0x70 y 0x71, que borran las
    vencidas como el juego). Con el mando, L y R la velocidad, los joysticks mueven la carta, izquierda y derecha las
    pestañas; en los ajustes, izquierda es «−» y derecha «+», X cambia la fila (los días dan la vuelta). El buzón a
    pantalla completa (sin panel: en una batalla, desde el menú de Start) sigue siendo el del visor: su texto, con las
    letras del juego en claro; con él abierto, sus teclas y sus toques van antes que los de la batalla (antes se
    quedaba sin poder cerrarse con el mando, y el toque que lo cerraba daba una orden a los barcos).
  - [x] El panel de las batallas (19-11-2026, `re/emu/sesion44/README.md`): repasado con el código. Su menú es el menú
    del barco del juego sobre la selección de la batalla (0x495190 elige con la marca 2, la de la cubierta): un barco
    del jugador en su estado 1 con la cubierta y sus botones de página con las reglas de siempre (mercancías,
    tripulación y cubierta encendidos; un barco en batalla, estado 0x14, no puede formar ni abandonar un convoy; la
    sesión 42 los había apagado todos, por leer el estado 3, el de un barco de otro mercader), sus demás páginas
    abiertas desde sus botones, varios barcos su lista (estado 0) y **«ver convoy»** su estado 2: la selección de la
    batalla pasa a ser el convoy entero (0x60EDF0 toma la selección entera) y una fila elige su barco solo, con su
    cubierta (0x48D586). Un cambio de la selección en la batalla (un toque, «seleccionar todos», L o R) vuelve a
    elegir en el menú. Cuadrado es «ver convoy», como fuera de la batalla; las ventanas de la página de la ruta se
    abren también sobre la batalla. La fila de acciones en los sitios del juego (0x42A3F7..0x42A52A): la bandera
    blanca en el cuarto y huir en el quinto, con «Terminar» en el quinto cuando el enemigo se ha ido (el visor lo
    ponía en el cuarto), y el cruce de sus dibujos que tiene el juego (abajo, «Corregidos con el ajuste»). El minimapa
    con los colores del juego (0x59C590, 0x59CA00: el agua, los arrecifes grises y los bancos de arena amarillos, el
    marco de la vista gris, puntos de 3 × 3 azul puro, rojo y gris oscuro). Estado de depuración nuevo
    `convoy-attack` (un convoy del jugador atacado por piratas).
  - [x] La barra de arriba (19-11-2026, `re/emu/sesion44/README.md`, «The top bar»): como la del juego, leída en su
    código (0x4285A0..0x428C07, los textos 0x423D30..) y en screenGame.ini: el trozo derecho de 284 sobre el panel,
    con su pilar tallado sobre la columna del panel, y el izquierdo al resto (alto 34 en la Vita: adaptado); la letra
    2 del juego en negro; el hombre en 26 con los habitantes detrás, el nombre de la ciudad centrado sobre la vista,
    la casa 52 antes del final del trozo izquierdo con sus edificios delante, la moneda en 32 del trozo derecho con el
    dinero detrás, el reloj de arena en 252 con la fecha corta del juego delante («1.abr. 1362», 0x6964B4). En el mapa
    del mar el juego no pone título: el visor deja «Mapa del mar - ciudad» en su sitio (adaptado). Las teclas del
    visor van en los huecos a los lados del nombre (partidas por sus separaciones si no caben enteras): ya no se meten
    bajo el pilar. El trozo izquierdo es el de 1280 sin estirar (su pilar estrecho, el de la foto del usuario: el hombre
    queda fuera de él, como en el juego). El reloj de arena abre la crónica también sobre una batalla (en el buzón a pantalla completa); de
    paso, el buzón sobre una batalla ya no se cierra solo (lo cerraba la regla de los mensajes del panel, desde el
    commit 0609801).
  - **El panel lateral con las letras y los textos del juego está acabado** (las siete partes de
    `ENCARGO_PANEL_JUEGO.md`, 17..19-11-2026).
- [x] **El panel de la Hansa** propio del visor (Cuadrado en una ciudad), quitado el 15-11-2026 a petición del
  usuario: sus encargos y elecciones son las páginas del ayuntamiento, y los materiales de la carretera, la página
  «Ruta terrestre» de la puerta de la ciudad, como las del juego.
- **La ciudad viva**: la simulación cambia la ciudad como el juego y el visor ya dibuja su estado actual (sus listas de
  edificios, obras y fortificaciones, y las de cada oficina), pero solo los edificios:
  - [ ] Antes de seguir, mirar en el juego original qué se ve y qué cambia: si la ciudad se redibuja en cuanto se
    construye algo, qué se anima (gente, carros, agua, humo...), cómo se ve un asedio.
  - [ ] Los habitantes y los carros por las calles, y las animaciones de los edificios.
  - [ ] Los barcos en el puerto (la lista *0x6E2D50, el objeto 0x6E6220 al que la simulación avisa al atracar y
    zarpar), también como puntos en el minimapa con el color de su dueño (0x56C730: azul los tuyos, gris oscuro los
    piratas, blanco los demás, rojo los de otro jugador humano; el dibujo del minimapa, 0x59C590). Con ellos, el «ir al
    barco» de la lista de barcos centrado en su barco (0x476A30).
  - [ ] El agua animada (`WaterSparkleTexID`; la de la batalla ya existe).
  - [x] Las sombras y la nieve de los tiles (17-11-2026, `re/emu/sesion39/README.md`; las variantes del `.let`, con
    la clase 1 de la obra y la 2 de la nieve corregidas en `docs/formats/town.md`). **Sombras**: como la pasada del
    juego (0x591C50), todas tras el suelo y antes de los objetos, cada una desde la celda de la franja `cols − 1` de su
    tile (0x551F70), en negro con alfa 100 (el juego, modo de mezcla 2 de la librería gráfica: una multiplicación; el
    resultado, el suelo al 61 %, como en las fotos del juego del usuario), también bajo una batalla en la ciudad.
    **Nieve**: el byte [0x9BC] de la ciudad en pantalla (ya en el motor) da el modo; cada tipo de tile cambia su atlas
    por el de nieve de su nivel (0x552090) y los árboles por el de su estación (el otoño, la nieve); las paletas que
    0x54F4F0 retoca no las marca ningún `.let` de los datos (no cambia nada); estado de depuración `season-N`. Los
    árboles: sus dibujos `_01` son el otoño, `_02` la floración (los de hoja caduca) o la nieve ligera (los pinos) y
    `_03` el invierno (pelados o muy nevados). Como en las fotos del juego del usuario (Edimburgo el 3 de febrero y el
    17 de marzo de 1301, `re/emu/sesion31/pc_shots/`), y no como lee el código (con la primavera lee más allá de su
    tabla de atlas): en primavera los de hoja caduca en flor y los pinos sin nieve. Los dos atlas de nieve con el
    fondo en 254, 0, 254 (la oficina y el ayuntamiento grande), sin marco rosa, que el usuario no ha visto nunca. Las
    sombras de los árboles son las de siempre también en invierno (su `WinterSchatten`, clase 4, el juego no la lee).
    Por mirar en el juego: a principios del invierno (el nivel de hielo 2), el código pone a los árboles de hoja
    caduca su floración.
  - [ ] Los árboles: su clase propia (0x676B30) los mece con el viento (0x583080; 0x581710 pinta encima el mismo
    rectángulo con la textura *0x6E3390 de la tabla 0x6E335C y un alfa que sigue al viento); hoy se dibujan quietos
    con su primer fotograma. Va con la ciudad animada.
  - [ ] Las obras: hoy lo que está en obras ([0x762] con `pending` ≠ 0) se dibuja terminado; faltan los andamios y los
    bordes de calle de las celdas 0x81. Los andamios y cimientos son la variante de clase 1 del `.let` ([data+0x18]),
    que 0x550770 dibuja con el avance de la obra (17-11-2026, `re/emu/sesion39/README.md`).
  - [ ] El fuego ([0x76A]): falta el humo y las llamas (los incendios de la ciudad, de un asedio, y la torre que cae en
    una batalla y arde unos 100 ticks como edificio, el evento 0x25; 0x5891C0, 0x599040). Decisión del usuario
    (10-11-2026): con la ciudad animada.
  - [ ] El asedio en la vista: el campamento, las tropas y el ataque a la puerta (0x62C730 y los objetos del ejército
    [+0x1F8], [+0x574], [+0x5DC..]) y el aviso al jugador; su icono de suceso ya sale.
  - [ ] Coste en la Vita: cada redibujado rehace la ciudad entera (2 ms en el PC más la lista de sprites); si da tirones
    en avance rápido, redibujar solo las celdas que cambian. Y la nieve: al cambiar de estación (o al entrar en una
    ciudad en invierno) se decodifican los atlas de nieve de sus tiles además de los normales, y se guardan; mirar el
    tirón y la memoria en la Vita. Y quitar `SavedTown::drawn_lists` y `SavedTown::fixed`, que
    ya no se usan para dibujar.
- [x] **Los iconos e imágenes de los textos del juego** (12-11-2026, lo vio el usuario en la carta del embargo): la
  moneda, el barril y el fardo (`\C`, `\B`, `\L`) y las imágenes por número (`\tN_`: el adorno de la crónica, los
  iconos del barco, las balas de las vistas de los edificios, los de las votaciones...) se dibujan con los gráficos
  del juego (`textures.ini`, cargados la primera vez) en todas las pantallas, no solo en las cartas; antes, fuera de
  ellas se perdían y en ellas eran dibujos a mano.
- [x] **Las capitulares de las cartas** (15-11-2026, `re/emu/sesion38/`): `\d` y una mayúscula (0x4626C5) ponen la
  imagen de esa letra (parchment.ini [TEXCOL0], `images/grossbuchstaben`, la letra − «A»; 39 × 39) al principio del
  párrafo en lugar de la letra, y las líneas a su lado (las del párrafo y las siguientes, hasta pasar su borde de abajo)
  empiezan su ancho y 4 más adentro, como la caja de texto del juego.
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
  letra, accesos rápidos), sin cambiar lo que hace el motor: el usuario habló el 18-11-2026 de una buena pasada de
  usabilidad o una «UX 2.0»; mientras, las decisiones de manejo buscan lo más usable (por ejemplo, el mando imita al
  dedo). Ideas apuntadas para entonces: elegir varios barcos en el mapa del mar (el recuadro arrastrado del juego);
  ver las teclas de la lista de barcos (X, Select) en pantalla.

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

Lo que hay y cómo lo decide el juego: [formats/sound.md](formats/sound.md). Por partes (plan del 20-11-2026):

- [x] **Parte 1, la base** (20-11-2026, `re/emu/sesion51/`): el objeto de sonido del juego (*0x6CBDB8) sobre el audio
  de SDL (`src/app/audio.hpp`): la música y la atmósfera con las que se apagan, sus fundidos de 5 s (0x444030), tocar
  una música (0x4426C0) y pararlas todas (0x442D10); los MP3 con dr_mp3 (`third_party/dr_libs`), los WAV IMA ADPCM y
  el `sound.ini` (`src/p3data/sound.hpp`, test `sound`); los volúmenes de Música y Sonido en las Opciones (se oyen al
  moverlos; «Cancelar» los devuelve; `p3vita-settings.txt`) y la música del menú principal (para al empezar o cargar
  una partida, como el juego). La música va en `ux0:data/p3vita/Musik/` (el LEEME de `ux0`).
- [x] **Parte 2, la música en la partida** (20-11-2026, `re/emu/sesion51/`): el director de música
  (`src/app/music_director.hpp`: 0x42C080 cada fotograma de la pantalla del juego y de las Opciones) con la música de
  la región de la ciudad, la del mapa del mar (también en las Opciones) y sus atmósferas entre pieza y pieza (0x442A10:
  la ciudad en verano, en invierno, con tormenta o con una batalla; el mar en verano o en noviembre y diciembre); sus
  sucesos (0x42C770: la ciudad mostrada, el mapa del mar mostrado, la música de una batalla), el tema de una ciudad al
  entrar en ella desde el mapa del mar (0x42CD90) y la música de los 12 edificios con ventana (0x4436E0: la de la
  región baja a 3, la atmósfera con el paso bajo a 400 Hz del juego); el «OK» de las Opciones con el volumen de la
  música a 0 o desde 0 (0x46010A). Con un azar aparte (apartado 8). Tres fallos que solo cambian lo que se oye,
  corregidos con el ajuste (abajo, «Lo que suena»). Falta lo que el visor aún no dibuja: el oscurecimiento del tiempo
  en la ciudad (sus tintes [+0xC178], [+0xC1A8], que callan la atmósfera) y la pantalla 0x6CBA64 (de 0x278 bytes, sin
  identificar), que también elige la música del mar.
- [x] **Parte 3, los sonidos de la interfaz** (20-11-2026, `re/emu/sesion51/`): los 20 canales de efectos del juego
  (0x443150: su volumen, su posición y «Invertir estéreo», ya vivo en las Opciones; las listas cargadas por pantalla,
  0x4439B0) y los 4 de la interfaz (0x4C45A0); el menú principal (8000), el clic de los botones de las Opciones y de las
  ventanas de partida nueva (`PSoundID=1`), las cartas nuevas y las no leídas al cargar (6000, con «Reproducir sonido»
  de sus ajustes), el baño (7000), la oración de la iglesia con sus botones apagados mientras suena (7100) y el marinero
  de la taberna que escupe (7200). Un fallo que solo cambia lo que se oye, corregido con el ajuste (abajo, «Lo que
  suena»). La casilla «Notificación de sucesos» de los ajustes de las cartas no la usa ningún efecto: va con los
  vídeos (parte 6). El pitido del juego al no poder cerrar una ventana (8001) no tiene caso en el visor (su Círculo
  abre las Opciones).
- [ ] **Parte 4, los sonidos de la ciudad**: los de cada edificio según dónde mira la cámara (iso.ini:
  `GebaeudeSoundListBase`, `SoundsProGebaeude`, `PanDist`, `VolDist`, los tiempos de espera), las campanas (12000),
  los barcos que entran y salen (13000), el fuego (10000), un edificio que se cae (9000) y el filtro del juego.
- [ ] **Parte 5, el mar y las batallas**: el mapa del mar (elegir un barco 1000, la alarma 2000, la tormenta 3000, los
  icebergs 3200, los barcos 4000, las aves 5000), las batallas en el mar (60000..60100) y los asedios (61000..61100).
- [ ] Al poner el sonido, el visor tiene que decir al motor si está encendido (`Sailing::set_battle_sound`): con la
  batalla en pantalla, cada sonido de la batalla gasta un `rand()` del mundo, como el juego. Y el sonido del abordaje
  (0x61F1BF) tendrá que guardar en [+0x14A] lo que devuelve la vista de la ciudad del PC con la vista abierta (1,
  suponiendo un hueco de sonido libre); hoy guarda 0, como el emulador.
- [ ] **Parte 6, los vídeos**: los de los sucesos (`Videos/*.sks`: imágenes JPEG en pase con su música de
  `Musik/Rendering`, [Videos]), el final de Gobernador (`Eldermann.sks`) y la introducción (Bink, `Intro_2.bik`: con
  FFmpeg u otro descodificador).

### 6. Probar en la Vita

- [ ] Todo lo hecho desde el 04-11-2026 (el usuario lo probará cuando esté más avanzado, 17-11-2026; mientras, cada
  parte se compila para la Vita): el buzón y las líneas de aviso, las tandas del barco, la oficina, el astillero
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
- [ ] **Más adelante, cada corrección por separado** (idea del usuario, 05-11-2026; el 17-11-2026, una pantalla de
  ajustes para encender o apagar ciertas cosas, cada corrección por separado o como complementos): un inventario de los
  fallos y rarezas y una pantalla en las Opciones para encender o apagar cada corrección, en vez del ajuste único. Por eso cada
  fallo de la lista de abajo tiene que quedar bien identificado: dónde está, qué hace mal y qué cambia su corrección.
- [ ] **La pantalla de ajustes, como la de las cartas** (idea del usuario, 19-11-2026): la ventana de ajustes de las
  notificaciones del juego (352 × 544, sus filas con «−» y «+» y sus casillas con la marca; `draw_mail_options`,
  re/emu/sesion43/README.md) puede servir más adelante de modelo para la configuración del juego: las correcciones de
  los fallos una a una y los ajustes del visor.
- [ ] La fiesta de la IA que nunca ocurre, como opción aparte (abajo, «No se corrigen»).

### 8. Multijugador con el juego original (al final de todo)

La razón de fondo para reescribir el motor idéntico: si p3vita se comporta byte a byte como el juego, una Vita puede
jugar en red con el juego original en un PC. Las acciones del jugador no cambian la partida directamente: son órdenes
que van a una cola y ejecuta la simulación, y en red solo viajan esas órdenes.

- [ ] La simulación entera idéntica, no solo lo que sale en las partidas de prueba (las misiones, los estados raros del
  barco, las ramas del apartado 2...). Un solo byte distinto y las dos partidas se separan.
- [ ] Ojo: el juego tira los gestos al azar de los interiores (el ayuntamiento, 0x5A94FA, y los demás edificios) con el
  mismo `rand()` de la CRT que usa la simulación: en el PC, tener un edificio abierto cambia los números al azar de la
  partida. El visor usa un azar aparte para esos gestos. Para jugar en red habrá que ver cómo lo resuelve el juego.
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
- **El azar de la música**: el director de música del juego (0x42C080) elige cada pieza y el punto donde empieza cada
  atmósfera con el `rand()` del mundo (0x639C03), fuera de la simulación: en el PC, con la música encendida, cambia
  los números al azar de la partida. El visor usa un azar aparte (como para los gestos de los edificios).
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
crónica con el aviso de carta nueva y las líneas de aviso; la puerta de la ciudad; las batallas en pantalla (la
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

- **El orden de lo que falta** (17-11-2026): 1) los restos de las pantallas ya empezados (las preguntas del convoy, las
  sombras y la nieve de los tiles, y el panel lateral con las letras y los textos del juego, que el usuario metió en
  este punto); 2) el intérprete de misiones, porque las misiones salen durante la partida, antes de
  llegar a Gobernador; 3) el final de la partida (la clasificación y el vídeo de Gobernador); 4) la ciudad viva; 5) el
  sonido, la música y los vídeos; 6) los fallos del juego, con una pantalla de ajustes para encender o apagar cosas (cada
  corrección por separado, o como complementos); 7) la partida en red. La prueba en la Vita, más adelante, cuando esté
  más avanzado: mientras, cada parte se sigue compilando para la Vita.

- **Sin flecos** (14-11-2026: «No quiero dejar cosas atrás, hay que acabar lo que falta primero … no dejar flecos
  por todos lados»): lo que falte de algo ya empezado se termina antes de empezar nada nuevo; lo que una sesión deje sin
  hacer pasa a lo primero de la lista, no al final.

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
- **L y R son la velocidad del juego** (más lenta, más rápida, con su aviso): en las ventanas sobre la ciudad y, desde
  el 19-11-2026 (a petición del usuario), también en la ciudad y en el mapa del mar, donde cambiaban de ciudad (de poco
  servía: en el mapa del mar una ciudad se elige tocándola). En una batalla siguen pasando de barco.
- **Las ventanas emergentes**: cada botón táctil con su tecla (X la acción, ○ cerrar), sin notas que expliquen lo
  obvio, lo que no se puede hacer atenuado y sin poder pulsarse, y con la ventana abierta la barra de arriba no enseña
  sus acciones.
- **Los textos**: los del juego, leídos del exe del jugador, en el idioma elegido en las Opciones (el CPR y el exe de
  ese idioma en la carpeta de datos); las frases que el juego no tiene, en los dos idiomas (`tr()`). Los términos de la
  versión española (Gobernador, no Alderman).
- **En la Vita**: nada de ficheros que el usuario no haya pedido en `ux0:data/p3vita/`; cargas a demanda con aviso
  mejor que precargas de fondo que den tirones; el usuario mide los tiempos a ojo.

- **El panel de la Hansa del visor, fuera** (15-11-2026): quitado; lo suyo, en las ventanas del juego (el ayuntamiento y
  la puerta de la ciudad).

## Los fallos del juego encontrados

Cada uno con su dirección, qué hace y su estado. «Seguro»: leído en el código o visto en el oráculo; «parece»: puede
ser a propósito. El detalle, en el comentario del código y en [oracle.md](oracle.md).

### Reproducidos, sin corregir aún

- **La carta de misión a los dueños de las oficinas de una ciudad** (instrucción 0xF7 del intérprete, 0x4ED4A0;
  20-11-2026): con respuestas, el destinatario −3 debería mandar una copia a cada dueño de una oficina de la ciudad
  de la carta, pero antes de mirarlo un `memset` pone a −1 las ocho variables desde la del destinatario, así que la
  carta va a todos los humanos (la rama del −3, en 0x4EDD57, no se ejecuta nunca). Ninguna misión de las del juego
  parece usarla así; pendiente de comprobar en las campañas antes de corregirlo.
- **La mercancía que más le falta a una ciudad** (0x51BEB0, la usa la instrucción 0x2F del intérprete; 20-11-2026,
  parece): de cada mercancía mira los obreros de la instalación con el número de la mercancía ([+0x844 + 16 ·
  mercancía]) en vez de la de la empresa que la produce (lee ese tipo de 0x672C88 y no lo usa; la rama del aceite de
  ballena queda muerta), como sí hace la lonja (0x5DE3xx). Cambia qué mercancía pide la misión 6 (y su cantidad):
  preguntar antes de corregirlo.
- **La instrucción 0xE8 con un comerciante que no existe** (0x4ED086; 20-11-2026, seguro, leído en el código): salta a
  la misma instrucción sin avanzar (0x4F2C97) y el juego se queda colgado. p3vita termina el guion. Ninguna misión del
  juego le da un comerciante fuera de la cuenta.
- **Las cargas que no son mercancía de un barco** (0x518640 y 0x5186D0, las usa la instrucción 0xF1; 20-11-2026,
  parece): al subirlas, la bodega ocupada [+0x118] suma las cargas nuevas · 2000, no las añadidas; al bajarlas, resta
  las que quedan, no las quitadas. Solo se nota si ya había cargas al subir o si quedan al bajar, y se arregla en el
  siguiente recálculo de la bodega (0x5182B0). Comprobado con el oráculo (`mission_ships_money`).
- **El barco más rápido de un comerciante en una ciudad** (instrucción 0x57 del intérprete, 0x4F01AA, solo en las
  misiones de la partida en red; 20-11-2026, parece): compara la velocidad de cada barco (0x5170F0) con la marca, pero
  guarda como marca su potencia de fuego [+0x120], así que el elegido no es el más rápido. Reproducido
  (`mission_network`).
- **El incendio de la instrucción 0xED** (0x4EE567, campañas; 20-11-2026, parece): busca la ciudad y su sitio con el
  número del operando (el de la variable) y no con su valor, que sí pone en la operación 0x7D; 0xE5, su pareja, usa el
  valor. Reproducido (`mission_campaign_b`).
- **Vender un almacén con 0x5256F0** (la instrucción 0x5F, campañas; 20-11-2026, seguro, leído): el almacén (0x1E)
  está en la lista de empresas de la oficina [+0x2CE], pero 0x5256F0 lo trata como una casa por su tipo (lo busca en
  [+0x2D0]) y el recuento 0x4FF2D0 lee fuera de la tabla de las casas. Ninguna venta del juego llega (las subastas no
  venden almacenes); en p3vita salta la aserción de `fn_4ff2d0`. Si alguna misión lo hace, hay que reproducirlo sin
  la aserción.

**«El carabela»** (las frases del barco patrulla en español, 0x5E1F01; 15-11-2026, seguro, solo texto): el artículo va
por el tipo de barco (0 y 2 «La», 1 y 3 «El») y la carabela es el tipo 1. Reproducido tal cual.

**«no action»** (la Dieta extraordinaria en la información de la Hansa, 0x5E3FBC; 15-11-2026, seguro, solo texto): el
juego une «no action» al nombre de la ciudad sin espacio («no actionLübeck»), porque su texto no lo lleva (los otros
sí). Reproducido tal cual.

**El balance de la oficina** (la página «Total», 0x5DC2A0; 14-11-2026, seguro, solo lo que se ve): los totales de la
semana pasada y de la previsión no cuentan la fila del administrador (su sueldo), aunque esa fila sale en la página.
Reproducido; sin decidir si corregirlo.

**Las mercancías de la oficina** (la página «Mercancías», 0x5DC0E4; 14-11-2026, parece, solo lo que se ve): una
pesquería con obreros de menos marca en rojo, además del pescado, el cáñamo (0x11), cuando la otra mercancía de una
pesquería es el aceite de ballena (0x0A). Reproducido.

**El nombre del cónyuge** (0x4F6470, en la ficha de la oficina; 14-11-2026, seguro): al dibujar el nombre, el juego
escribe en la partida el bit 7 del [+0x30] del comerciante según su sexo. El visor lo escribe igual (como la fecha del
astillero); no cambia nada si el bit ya estaba bien.

**La fecha del astillero en su menú lateral** (0x52AFC0, llamada desde el menú en 0x5AE0EC y 0x5AE18A; 14-11-2026,
seguro): al calcular cuándo acaba el astillero para enseñarlo, el juego pone a 0 el [+0x18] de los barcos encargados
que lo tengan por debajo de 0: abrir el menú cambia la partida (el [+0x18] es lo construido del barco). Reproducido a
petición del usuario, como el original (`shipyard_done_tick`, `player_actions.cpp`, con su ctest): el visor lo escribe
al enseñar las fechas del menú (cada vez que lo dibuja; el juego, al abrirlo). Sin ver aún si en una partida llega a
haber un barco encargado con el [+0x18] negativo; las páginas del astillero que enseñen fechas (0x5F6CAF, 0x5F9ACA)
tendrán que llamarlo también.

**El respeto de los miembros del gremio** (la página «Miembros», 0x5C67A0; 14-11-2026, seguro, solo el texto): la
ciudad donde más respeta al miembro se busca entre las que le dan más de 0, así que si no tiene respeto positivo en
ninguna, la página dice que «goza del máximo respeto» de la primera ciudad de la partida (Edimburgo en las del
visor). Reproducido; sin decidir cómo corregirlo (¿su ciudad natal?).

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

**El ayuntamiento y el asedio** (13-11-2026):
- El voto en el consejo (0x49, 0x53B0D0; parece): cada asiento de concejal del jugador suma un voto seguro a favor
  ([+0xE]) si vota sí, pero si vota no suma uno más a los que quedan por repartir al azar ([+0xF]), no uno en contra
  ([+0x11]). Puede ser a propósito.
- La entrega al príncipe (0x8D, 0x635DE0; seguro en el código): sin oficina del comerciante en la ciudad, el juego lee
  en la dirección 0 (se colgaría); p3vita no hace nada. Sin comprobar si la ventana lo impide.

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

**Las ventanas (solo lo que enseñan)** (13-11-2026, noche):
- La lista de quienes piden un préstamo (0x5B4180, la página 2 del prestamista) pone el artículo y el rango de cada uno
  según el sexo del comerciante número 10 (0x5B42C9: 0x5303C0(10)), no el del que lo pide (seguro). Corrección: el
  suyo.
- En la taberna, la entrega del comerciante deja un espacio antes de su «."» final y el viajero sube a bordo sin punto
  (textos del juego; parecen descuidos). En el exe español, el hueco de «and set sail.» del pirata no apunta a ningún
  texto: el visor acaba la frase con su punto.

### Corregidos con el ajuste

**Lo que suena** (20-11-2026, leídos en el código del juego, `re/emu/sesion51/`; solo cambian lo que se oye, como los
de los textos: corregidos sin preguntar): la atmósfera de la ciudad al mostrarla (0x42C770 con 0: 0x42CCD1) sale la de
verano con el hielo y la de invierno sin él (la duración que calcula antes es la de la buena); el director de música
(0x42C080) busca la región de la ciudad en la tabla de StadtDaten.ini por el índice de la ciudad en la partida, no por
su número (0x6DE4B8), y elige la música de otra región; el tema de Rügenwald es `Ruegenwald.mp3` en StadtDaten.ini y el
fichero se llama `Rügenwald.mp3`, así que no suena (`music_director.cpp`, `main.cpp`); la lista 7000 (el baño, la
oración, el escupitajo) solo se carga al abrir el baño (0x5B1D41), así que hasta entonces ni la oración de la iglesia
ni el marinero de la taberna suenan (`main.cpp`: con el ajuste se carga antes de tocarlos).

**El rival de la ciudad del jugador** (0x532420, la operación 0xC0 al empezar una partida; 19-11-2026, seguro:
escenario `mission_start`, roto a propósito, y las 12 partidas lo muestran): por cada comerciante humano con un
comerciante de la IA del tipo 1 en su ciudad, el juego quiere hacer de ese uno de los rivales (el bit 8 de [+0x8] y el
factor de reputación [+0x464] a 2), pero lo escribe en el comerciante 0 (0x532500 usa la base de la tabla): en todas
las partidas el comerciante 0 (de la ciudad 0) sale como rival y el de la ciudad del jugador no. Corregido a petición
del usuario (19-11-2026): el rival es el comerciante que encuentra (`missions.cpp` `fn_532420`; con
`tick_probe --fix-game-bugs`, el escenario cambia el comerciante 0 por el 28 en la partida 3).

**El factor de reputación de la dificultad, perdido** (0x532420; 19-11-2026, seguro: `re/emu/sesion45/ngfactor.sh`, la
partida nueva del juego con la dificultad que da 1,3 acaba con el jugador a 1): la 0xA7 pone el factor [+0x464] del
jugador según la dificultad (0x4F9550: 1,3, 1 o 0,7) y la 0xC0 que encola a continuación lo vuelve a poner a 1 con el
de todos; nada lo recalcula después (0x4F42B0, desde el evento 0x04, solo a los de la IA). En una partida de un jugador
la dificultad no cambia lo que sube su reputación. Corregido a petición del usuario: el de los humanos se queda.

**Una misión de la campaña 3 que no se carga** (`scripts/missions_eng.ini` y `missions_spa.ini`, 19-11-2026, seguro:
el fichero no está con ese nombre en el archivo): `[Missions] 308=Kampagne3_Entfuehrung.p2m`, pero el fichero se llama
`Kampagne3_Entführung.p2m`: el secuestro de «El auge de la Hansa» nunca sale (0x5116F0 no lo encuentra y 0x511270 deja
su cuenta a 0). Corregido a petición del usuario: el fichero también con la «ü» (`load_mission_files`, `mission_file`).

**Los guiones que se descartan al empezar** (0x511270, 19-11-2026, seguro, sin efecto con los datos del juego): al
arrancar varios guiones de una misma misión cuyo tick ya ha pasado, el bucle cuenta hacia arriba mientras la cuenta de
la lista baja con cada uno que descarta, así que de tres solo se van dos y el programa se queda con uno de más.
Corregido a petición del usuario: tantas vueltas como guiones tenía.

**La ventana del icono de una batalla en el mapa del mar, sin su botón de dirigirla** (0x611FC0, 0x6128E3; 19-11-2026,
`re/emu/sesion46/README.md`): al cerrarse la ventana, su actualización toma la batalla (el método +0x24 de la batalla,
0x6102A9: dirigirla en pantalla) si se cerró con su primer botón ([+0x5A4] == [0x66BDC0]), pero su descripción le pone
0 botones (0x612769), y su cierre deja el resultado 0 (0x41E670): una batalla que lleva el ordenador ya no se puede
dirigir. Reproducido (la ventana solo con su cierre; estado `pirate-attack`, el ordenador lucha y se toca el icono);
con el ajuste lleva «Dirigir manualmente» (0x6B2BC0, el texto de la pregunta), que toma la batalla como haría ese
código. Antes el visor lo tenía siempre (leído mal en la sesión 21).

**Los botones de huir y de rendirse del panel de la batalla, cruzados** (la fila de acciones, 0x425A5E..0x425A9F;
19-11-2026, seguro: solo cambia lo que se ve): el panel crea su cuarto botón, el de la bandera blanca (sus bits 0x1000
y 0x2000 en 0x424382, su clic 0x610FD9 en 0x42D499), con FliehenBtnID (Button29: el barco que huye y el aviso «Huir»,
20051), y el quinto, el de huir (los bits 0x1 y 0x2, el clic 0x610C09 en 0x42D436), con AufgebenBtnID (Button30: la
bandera blanca y «Rendirse», 20052). En el juego, el botón que se ve y se anuncia como «Huir» rinde los barcos y el de
«Rendirse» los hace huir. Reproducido (`battle_button_state`, estado `convoy-attack`); con el ajuste, cada botón lleva
el dibujo y el aviso de lo que hace.

**Los títulos del libro del astillero al pasar de un barco a otro** (0x5F8910, las páginas de reparar, mejorar y el
nombre; 15-11-2026, seguro: comprobado en el emulador, solo cambia lo que se ve medio segundo): el libro funde el barco
anterior con el nuevo, pero entre barcos compara los títulos solo por «Convoy» (0x5F8BC1, 0x5F8E93): de un convoy a un
barco suelto el título que aparece es el tipo del convoy (0x5F8EB4: «crayer» en vez de «snaikka», comprobado en la
partida 4) hasta que acaba el fundido; entre dos convoyes no sale ningún título; entre dos barcos sueltos de tipos
distintos el título anterior desaparece de golpe. Corregido sin preguntar (es solo de lo que se ve; `yard_book`): con
el ajuste, los títulos se comparan por sus palabras, como entre los tipos de «Construcción».

**La prima del pedido en la taberna, en español** (la página tras una entrega a medias con prima, 0x5D4BA6 del exe
español; 15-11-2026, seguro, solo cambia lo que se ve): el código español pone el precio en una línea propia, «de %u»
(0x69D4C4), y coloca la moneda tras el ancho de la cadena «de %u» y no del precio escrito, así que la moneda pisa la
última cifra. Corregido (`order_client_page`): con el ajuste, tras el ancho del texto escrito.

**Los botones de las reuniones en la oficina del Gobernador** (la página 7 del ayuntamiento, 0x5E6730; 15-11-2026,
seguro, solo cambia lo que se ve): tras una misión fallida (el tipo 5, que dura 28 días), la página enseña «Después de tu
último fracaso…» (0x6A9680) y salta al final sin esconder los cinco botones de las reuniones (0x5E6A9A → 0x5E76A5; las
demás ramas pasan por 0x5E6E4D, que los esconde). El Gobernador que pasa a «Misiones» desde una reunión elegida sigue
viendo «Ninguna acción», «Multa leve», «Multa moderada», «Multa elevada» y «Bloqueo», que ahí no hacen nada.
Corregido a petición del usuario (`town_hall_office_view`, estado `failed-task`): con el ajuste, escondidos.

**La «Ruta terrestre» de cualquier puerta** (la página 4 de la puerta de la ciudad, 0x5F2A20; 15-11-2026, seguro: el
código y el visor): la página sale en la puerta de cualquier ciudad mientras el jugador tenga una carretera de la
Hansa, con sus «->» encendidos si tiene materiales allí, pero el ejecutor de la 0x3B (0x53A3E0) solo acepta las dos
ciudades de la carretera: en las demás, pulsar no entrega nada (sin aviso). Corregido a petición del usuario
(`road_window_page`, con su ctest): con el ajuste, la página solo se abre en las dos ciudades de la carretera (en las
demás se cierra como sin carretera).

**El botón «Avisos» del ayuntamiento** (el menú lateral, 0x5A9780; 15-11-2026, seguro: comprobado en el emulador,
`re/emu/sesion36/halltrace.sh 0 1 --menu`; corregido a petición del usuario): con un pedido en el mundo y sin decreto
del Príncipe, el juego enciende el botón y en la misma pasada lo vuelve a apagar (se le olvida salir del bucle,
0x5A97F2), así que «Avisos» solo se puede pulsar con el decreto. Corregido (`town_hall_menu`, con su ctest): con el
ajuste, un pedido en el mundo también lo enciende.

**El informante de la taberna se queda quieto** (0x466800: 0x46691D y 0x46694C; 14-11-2026, seguro, solo cambia lo que
se ve: corregido a petición del usuario): una animación de ida y vuelta, al volver, se queda para siempre en su primer
fotograma, porque el juego pone el fotograma a 0 antes de mirar si ha llegado al principio. La única así es el
informante dándose la vuelta: tras la primera vez no vuelve a hacer sus otros gestos en toda la partida. Corregido
(`src/app/game_window.cpp`, `update_animations`): con el ajuste, se mira antes de poner el fotograma a 0.

**La producción de la ciudad en la lonja** (la página «Producción semanal», 0x5DE960; 14-11-2026, seguro, solo cambia
lo que se ve): la página suma la producción de un día de cada oficina por 7, pero la de la ciudad (también de un día:
el tick la pone a cero cada día) la enseña sin multiplicar, una séptima parte de la semana. Corregido
(`market_hall_window.cpp`): con el ajuste, también por 7.

**La situación social en la ficha de la oficina** (la página «Personal», 0x5DB4E0; 14-11-2026, seguro, solo el
texto): el juego escribe el rango en «Situación social en esta ciudad: %s», pero el texto del exe español no tiene el
«%s» y el rango se pierde. Corregido (`office_personal_page`, `src/app/main.cpp`): con el ajuste, «: <rango>» detrás.

**El aviso 5 de la caballeriza** (su menú lateral, 0x5A9D5A..; 14-11-2026, seguro, solo el texto): para el aviso 5 el
juego enseña el texto de «Continuar» (0x6AFA34) en vez de un aviso. Corregido (`land_trade_view`, `src/app/main.cpp`):
con el ajuste, una frase del visor.

**Los resultados de la subasta en el gremio** (la página «Fin de la subasta», 0x5C2F90; 14-11-2026, seguro, solo cambia
lo que se ve: corregido sin preguntar, como los textos): el evento de la subasta pasa a la ventana del gremio (0x5C6740,
llamada en 0x4E3417 y 0x4E36C1) la ciudad y la marca de «edificios» en el orden cambiado, así que la ventana toma la
ciudad como marca y la marca como ciudad: la lista de lo vendido solo sale en la primera ciudad de la partida (subastas
de barcos) o en la segunda (de edificios), y en las demás solo «Fin de la subasta». Y un lote sin vender también se
apunta, con el comprador 0xFF, fuera de la lista de comerciantes (el juego lee lo que haya allí). Corregido
(`town_tick/events_mayor.cpp` `record_auction_result`, la página en `src/app/main.cpp` `guild_results_view`): con el
ajuste, la ciudad y la marca donde van y sin los lotes no vendidos.

**La venta a una ciudad sitiada** (0x89, 0x635990; 13-11-2026, seguro, corregido a petición del usuario): al
comerciante se le paga solo el último lote que se le quita (el de la oficina o el del último de sus barcos), no todo
lo vendido; y si no tiene oficina allí ni barcos que den nada, se le paga todo lo pedido sin quitarle nada (vender 10
cargas a 100 con 6 en la oficina y 4 en un barco: se lleva las 10 y paga 400). Corregido (`town_tick/sieges.cpp`
`fn_635990`): se paga lo vendido; con el ajuste, el escenario `siege_window` sale distinto del juego solo en el dinero
de las tres ventas (7 cargas a 120: 840 en vez de 240; el comerciante sin oficina ni barcos, 0 en vez de 500).

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
  (0x471EA0) y el título de los habitantes en inglés sin espacio; las fechas de los días de la gráfica (0x49FD70,
  0x49FF58): al volver atrás pasando dos veces de mes (de los primeros días de marzo a enero) toma la duración del
  mes anterior al actual, y las fechas de enero salen tres días corridas (solo el dibujo; corregido detrás del ajuste,
  `statistics_x_labels`).
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
