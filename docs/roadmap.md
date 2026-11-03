# Hoja de ruta

Patrician III no tiene el código fuente publicado, así que esto es una
reimplementación del motor que lee los datos originales del juego (versión GOG
1.1, la misma que documenta [P3Modding](https://p3modding.github.io/)).

Cada fase termina en un hito que se puede comprobar a simple vista.

Cómo leerla:
- `[x]` hecho y comprobado; `[ ]` pendiente.
- «(en marcha: `cloud/…`)»: lo está haciendo una sesión en la nube; todavía no está en `main`.
- Lo terminado va resumido. El detalle está en los documentos enlazados:
  [formats/](formats/), [oracle.md](oracle.md), [inventario.md](inventario.md)
  (todo lo que hace la simulación del juego, sacado del ejecutable) y las notas
  de las sesiones en la nube, [cloud/](cloud/).
- Este documento se queda en español: lo lee el usuario.

## En marcha (02-11-2026)

- Las sesiones en la nube se acabaron: el crédito se agotó y todas las ramas `cloud/*` están juntas en `main`. Sus notas siguen en [cloud/](cloud/).
- Hecho (03-11-2026): un barco del jugador atacado ya no se queda parado. Su batalla la lucha el ordenador, como en el juego cuando el jugador no la dirige (la operación 0x96), y los avisos del abordaje se escriben con los textos del juego (abajo, pieza 3). En el visor salen ya el aviso del ataque y los del abordaje, en español. Lo siguiente: el resultado de la batalla, que el juego manda por carta (el parte de 0x60D299), y la batalla en el mapa del mar.
- En marcha (03-11-2026): **el buzón de cartas y avisos** (fase 4, «Guardar las cartas y avisos nuevos…»), con el parte de la batalla. Hecho el inventario ([formats/mail.md](formats/mail.md)); lo siguiente, reescribir el envío de cartas y conectar las llamadas.
- Después: la parte C del inventario y luego la D. La parte B está hecha salvo la 0x9F y la 0xB5, que van con las batallas.
- Lo que queda en español en el repositorio (menos este documento) se pasa al inglés poco a poco, al tocar cada fichero.

## Estado por partes (medido)

Cuánto código del juego tiene p3vita en cada parte de la simulación, medido el 01-11-2026 sobre `main` (con `cloud/battles-2` ya junta) con `re/emu/sesion5/coverage_parts.py` (Python de Windows; para repetirlo: `python coverage_parts.py RUTA/p3vita/src`).

Cómo se mide:
- Para cada parte se toman sus funciones de entrada del juego y todo lo que cuelga de ellas, sin la interfaz.
- Se usa el cuerpo real de cada función, siguiendo sus saltos.
- **Reescrito**: funciones `fn_XXXXXX` de p3vita.
- **Nombrado**: la dirección solo sale en un comentario de p3vita. Puede estar reescrita sin ese nombre (la carga de partidas, la navegación) o saltada a propósito (cartas, ventanas).
- **Sin nada**: p3vita no la tiene.
- Las partes se solapan: una función que usan varias cuenta en cada una.
- Es un grafo estático, sin las llamadas indirectas, así que las cifras son orientativas.

| Parte | Funciones | KB | Reescrito | Nombrado | Sin nada |
|-|-|-|-|-|-|
| Tick de ciudad (0x51BA10) | 155 | 67 | 64 (96 %) | 2 (3 %) | 0 (1 %) |
| Tick de cada comerciante (0x4F7BB0) | 25 | 10 | 9 (93 %) | 1 (6 %) | 0 (0 %) |
| Barcos: navegación, IA y convoyes (0x506720) | 152 | 62 | 35 (57 %) | 22 (36 %) | 4 (7 %) |
| Lo diario, tareas programadas y objetos del mundo | 12 | 2 | 2 (97 %) | 0 (3 %) | 0 (0 %) |
| Asedios (0x6349F0, 0x633A90) | 28 | 15 | 13 (89 %) | 2 (11 %) | 0 (0 %) |
| Cola de eventos (0x4D85C0) | 415 | 193 | 156 (81 %) | 25 (13 %) | 13 (6 %) |
| Operaciones que encola la simulación | 172 | 72 | 56 (78 %) | 15 (21 %) | 1 (1 %) |
| Camino a Gobernador | 223 | 93 | 70 (76 %) | 20 (22 %) | 2 (2 %) |
| Cargar partida (0x549F40) | 103 | 42 | 6 (15 %) | 34 (81 %) | 2 (5 %) |
| Guardar partida (0x549CC0) | 38 | 11 | 0 (0 %) | 10 (96 %) | 0 (4 %) |
| Tormentas (0x5053C0) | 4 | 5 | 5 (97 %) | 0 (3 %) | 0 (0 %) |
| Batallas y encuentros | 58 | 32 | 11 (35 %) | 19 (59 %) | 2 (6 %) |
| Guiones de misiones (0x4ECF64, 0x511270) | 73 | 23 | 4 (18 %) | 7 (32 %) | 11 (50 %) |
| **Toda la simulación** (advance_time y sus operaciones) | 597 | 287 | 228 (79 %) | 42 (15 %) | 17 (6 %) |

Notas:
- **Barcos**: los piratas ya están enteros (pieza 2 de las batallas, encendidos por omisión), también los objetos piratas (0x514000 → 0x513EC0) y la guarida (0x515DA0 → 0x513BF0). Lo que falta son sobre todo las rutas comerciales (0x4D5200, 0x52ACD0, 0x4D4C90...).
- **Cola de eventos**: lo que falta son los tipos que p3vita aún no atiende (0x4E5664, 0x4E38B4, 0x4DDF40, 0x4E7A14...).
- **Cargar y guardar partida**: aunque en la tabla salgan como «nombrado», están hechos e idénticos al juego en las 12 partidas. `savegame.cpp` y `savegame_write.cpp` los reescriben por estructuras y no con funciones `fn_`, y algunas direcciones (0x504230, los barcos) ni siquiera salen en los comentarios, así que la medida se queda corta.
- **Batallas**: la fila se queda muy corta, porque las batallas van sobre todo por tablas de métodos, que este grafo no sigue: no cuenta el paso de la batalla (0x606CC0 y lo que cuelga, unas 7000 instrucciones), que ya está reescrito. Su estudio, que sí las sigue ([cloud/battles.md](cloud/battles.md)), cuenta sin la interfaz 249 funciones (148 KB) en el mar y 41 (50 KB) en las ciudades. Está hecha la batalla automática en el mar (empezar, cada paso y acabar) y guardarla y cargarla; faltan el lado humano, las balas de cañón, las rocas como obstáculos y las batallas en las ciudades.
- **Todo el ejecutable**, con la interfaz y las batallas: p3vita reescribe un 11 % (`coverage.py`). La mayor parte del resto es interfaz.

## Fase 1: entorno y datos

- [x] Repositorio y estructura CMake
- [x] Datos del juego extraídos con `innoextract` (fuera del repositorio)
- [x] VitaSDK instalado en WSL (`scripts/setup-vitasdk.sh`)
- [x] VPK de prueba que lee el CPR desde `ux0:data/p3vita/` y muestra BMP del juego (`scripts/build-vita.sh`)
- [x] Probado en una Vita real (botones, táctil y colores correctos)
- [ ] Icono y LiveArea propios (sin usar arte del juego)
- [x] Lector de archivos CPR (`src/p3data/cpr.*`), herramienta `cprtool` y test
- [x] Formato CPR documentado en [formats/cpr.md](formats/cpr.md)
- [x] Repositorios privados en GitHub (`p3vita` y `p3vita-datos`) y entorno de las sesiones en la nube. Los datos del juego van solo en `p3vita-datos`, nunca en `p3vita`

## Fase 2: herramientas de datos y dibujo de las ciudades

- [x] BMP, TGA y JPG con stb_image (1100 de 1100 ficheros, inglés y español) y herramienta `p3img`
- [x] Formato AIM documentado ([formats/aim.md](formats/aim.md)) por ingeniería inversa de `AIM.dll`
- [x] Decodificador AIM en C++: 3409 de 3409 ficheros, idéntico a la referencia salvo los 5 JPEG
- [ ] Decidir la semántica de transparencia del motor (magenta, alfa 0 de los `_TP`, sombras con alfa 100)
- [ ] Subir los DXT3 a la Vita como texturas BC2 sin descomprimir
- [x] Mapas de ciudad y tiles ([formats/town.md](formats/town.md)), con tres correcciones a P3Modding
- [x] `p3town`: dibuja cualquier ciudad en un PNG

**Hito:** ver una ciudad dibujada en el PC. ✔

- [x] Ciudades con edificios desde los `.editor` (superconjunto: todas las murallas a la vez)
- [x] Partidas guardadas leídas ([formats/savegame.md](formats/savegame.md)): edificios, objetos fijos y plantilla de cada ciudad
- [x] Ciudades tal como empiezan (`missav/1.pat`, año 1300), con el estilo de su región
- [x] Comerciantes (dinero, nombre, cuál es el jugador), oficinas y barcos (posición, carga, bodega) de las partidas, comprobados con el oráculo
- [ ] Partida nueva desde cero: `stadtplatz*.tow` y la generación de las casas (función sin localizar)
- [ ] Dibujar los kontors (las oficinas ya se leen), los andamios de las obras y los bordes de calle de las celdas 0x81
- [ ] Partidas de la versión 0x4D (`missav/0100–0204.pat`)
- [ ] Variantes de los tiles: sombras, nieve y andamios
- [ ] Agua animada (`WaterSparkleTexID`)

### La ciudad viva en pantalla

La simulación cambia la ciudad como el juego (edificios nuevos, obras, murallas, asedios), y la vista tiene que seguirla.

- [x] Dibujar desde el estado actual de la ciudad, no desde la copia hecha al cargar la partida: las listas [0x762] (obras), [0x764] (edificios), [0x768] (fortificaciones) y [0x76A] (lo que arde), los puntos fijos [0x7B0..0x80B], [0x76C] y la iglesia [0x7A3]. Nada más cargar, las 285 ciudades de las 12 partidas salen byte a byte igual que antes ([cloud/desktop-viewer.md](cloud/desktop-viewer.md))
- [x] Redibujarla cuando cambian sus edificios, sin mover la cámara, y también al volver a entrar
- [ ] Antes de seguir, contrastar con el juego original qué se ve y qué cambia: si la ciudad se redibuja en cuanto se construye algo, qué se anima en tiempo real (gente, carros, agua, humo...), cómo se ve un asedio y si los edificios atacados cambian. Del código solo se sabe que el ejército sitiador tiene objetos gráficos propios y que el juego prepara sus gráficos (0x62C730) si la ciudad sitiada es la que se está viendo
- [ ] Andamios: lo que está en obras ([0x762], con `pending` ≠ 0) se dibuja como el edificio terminado (las casas, con la variante 0). Mirar en el juego original qué dibuja y cómo avanza
- [ ] El fuego ([0x76A]) se dibuja como el edificio normal: falta el humo y las llamas
- [ ] El asedio en la vista: el campamento, las tropas y el ataque a la puerta (lo que la simulación salta: 0x62C730 y los objetos del ejército [+0x1F8], [+0x574], [+0x5DC..]) y el aviso al jugador
- [ ] Los habitantes y los carros por las calles, las animaciones de los edificios y los barcos en el puerto
- [ ] Coste en la Vita: cada redibujado rehace la ciudad entera (2 ms de colocar en el PC, más rehacer la lista de sprites). Si da tirones en avance rápido, redibujar solo las celdas que cambian
- [ ] Quitar `SavedTown::drawn_lists` y `SavedTown::fixed`, que ya no se usan para dibujar (el cargador los sigue llenando)

## Fase 3: esqueleto del motor

- [ ] Plataforma común con SDL2 (ventana, entrada, audio) para PC y Vita. La ventana y la entrada ya funcionan en los dos (el visor corre en el PC con SDL2 de escritorio); falta el audio
- [x] Render isométrico con cámara (visor de ciudades en `src/app`)
- [x] Entrada táctil y con sticks en la Vita
- [x] Probado en una Vita real: el desplazamiento va fluido

**Hito:** mover la cámara por una ciudad (Lübeck) en la Vita. ✔

- [x] El visor en el PC con SDL2 de escritorio (`libsdl2-dev`, SDL 2.0.20 o posterior): el ratón hace de pantalla táctil, los errores salen por consola y `--cpr ARCHIVO` elige los datos (el CPR español se monta si está al lado del inglés)
- [x] Ejecuciones sin ventana, con una lista de pasos y capturas (`--steps`, `--screenshot`; `p3vita --help`), iguales fotograma a fotograma: la interfaz se puede ver sin sacar un VPK
- [x] Compilación para la Vita: `vita-elf-create` fallaba («segment 1 overlaps») según el tamaño del código. Se arregla dejando 0x1000 bytes de margen (`__sce_headroom`, en `src/app/CMakeLists.txt`). p3data se compila con `-fwrapv` y `-fno-strict-aliasing` en todas las plataformas, que es lo que se comprueba con el oráculo

## Fase 4: simulación

### Hecho e idéntico al juego

Comprobado byte a byte con el oráculo, que ejecuta el código del juego en un emulador de CPU ([oracle.md](oracle.md)).

- [x] Mercancías, almacenes y precios; los umbrales de precio de cada ciudad (`update_town_price_thresholds`), como al cargar la partida
- [x] Comprar y vender entre un barco y la ciudad: 24 000 operaciones iguales. Las estadísticas de compra y venta del comerciante y su reputación en la ciudad, también
- [x] Partidas cargadas en memoria igual que el juego (ciudades, oficinas, comerciantes, cola de eventos...)
- [x] El tick de ciudad completo (0x51BA10: población, satisfacción, producción de las empresas, consumo, milicia, astillero, iglesia y oficinas de los comerciantes; [formats/town_tick.md](formats/town_tick.md)): 23 040 ticks partiendo de las 12 partidas, hasta 240 días
- [x] El mapa del mar con las ciudades y los barcos; las rutas entre ciudades (6633 rutas iguales); el movimiento de los barcos, la salida y llegada a puerto y la orden de zarpar ([formats/navigation.md](formats/navigation.md))
- [x] El paso del tiempo al ritmo del juego (234 ms por tick a velocidad normal) y la fecha
- [x] El tick de cada comerciante (0x4F7BB0): contabilidad, valor de la compañía, reputación y rangos
- [x] Lo diario y semanal, las tareas programadas, los 15 objetos del mundo y los asedios ([formats/world_tick.md](formats/world_tick.md))
- [x] La cola de eventos (0x4D85C0): todos los tipos que salen en las partidas (elecciones, subastas, deudas, encargos, expediciones, piratas, peste, la Hansa...) ([formats/world_tick.md](formats/world_tick.md) §6)
- [x] Los barcos de la IA en puerto (comercio con las ciudades y sus oficinas, elección de destino, tripulación, armas, reparación en el astillero, creación de convoyes; [formats/navigation.md](formats/navigation.md) §8), los barcos como memoria del juego y los convoyes de los comerciantes (§9)
- [x] El ejecutor de las operaciones encoladas (0x535760, tabla 0x537030; `run_operation` y el oráculo), con 0x00, 0x0A, 0x12, 0x13, 0x1E, 0x1F, 0x22, 0x23, 0x24, 0x27, 0x28, 0x29, 0x2C, 0x37, 0x38, 0x3B, 0x46, 0x48, 0x4B, 0x60, 0x61, 0x64, 0x65, 0x67, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x83, 0x84, 0x85, 0x86, 0x87, 0x8E y 0xB8 (qué hace cada una: [inventario.md](inventario.md), parte B). Las demás aún se descartan; la 0x8B, la petición de ayuda en un asedio, es de la interfaz
- [x] Las obras de las ciudades:
  - la operación 0x22 (construir: la rejilla 0x4FB9C0/0x4FCBD0, el registro 0x521900 y las torres 0x523830/0x523D70);
  - la 0x27 (los materiales que recibe la ciudad), la 0x23 (empezar una muralla) y la 0x24 (cada pieza de muralla y su cierre).

  Con ellas, la ciudad fundada levanta sus edificios municipales, acaba su muralla y empieza a crecer (272 habitantes el día 420, igual que el juego)
- [x] Todas las operaciones que cambian el mundo en las partidas y con la ciudad fundada:
  - 0x7B, 0x79 y 0x7A;
  - 0x1E y 0x1F, con la rama de convoy de la orden de zarpar;
  - 0x78, 0x12, la 0x64 con el evento 0x33, la 0x8E (los asedios nuevos);
  - 0x29, 0x81, 0x37, la 0x61 con el evento 0x07, y la 0x2C.
- [x] **El mundo entero** (advance_time sin la interfaz) idéntico al juego byte a byte 60 días en las 12 partidas. La comprobación de 7 días en las 12 partidas (`world.sh 7`) tarda 98 s
- [x] Las tormentas (0x5053C0 con 0x505290 y el desvío de la ruta 0x447580, cada 8 ticks; `src/p3data/battle/storms.cpp`). Comprobadas llamada a llamada en las 12 partidas (10 días: 3840, 350 y 48 llamadas) y con estados retocados que recorren todos los bloques alcanzables. Están encendidas por omisión en p3vita y en el oráculo; `--no-storms` las apaga. El mundo entero sale a 0 diferencias en las 12 partidas con ellas y sin ellas. Bajan el casco de los barcos en 8 de las 12 partidas en 10 días

### El inventario de la simulación

La lista cerrada, sacada del ejecutable y no de lo que sale en las partidas, está en [inventario.md](inventario.md). Se hizo con el grafo de llamadas estático (`re/emu/sesion5/callgraph.py`; las tablas, en `re/emu/sesion5/inventario/`). El grafo parte de advance_time (0x530E80) y de sus piezas (0x51BA10, 0x4F7BB0, 0x506720, 0x535760, 0x5315C0, 0x535450, 0x52D7D0, 0x6349F0, 0x633A90 y 0x4D85C0), y separa la interfaz. Incluye:
- la tabla de eventos 0x4D8A28, con sus 58 tipos y quién crea cada uno (0x4D8CF0 con el tipo en [+6], `callgraph_eventos.tsv`);
- la tabla de operaciones 0x537030, con sus 193 códigos;
- las 363 llamadas a 0x54AA70, la función que encola (`callgraph_encolar.tsv`).

- [x] A: los eventos que p3vita creaba y no atendía (0x23, 0x24, 0x34, 0x25, 0x26, 0x27, 0x31 y 0x38)
- [ ] B: las operaciones que encola la simulación. Hechas todas salvo:
  - [ ] 0x9F, que va con la batalla en el mar (pieza 3 de las batallas);
  - [ ] 0xB5, una flota pirata que sale de su guarida. Solo la encola la actualización de los objetos piratas (0x514000), que aún no se ejecuta (pieza 2).
- [ ] Los tipos de evento que p3vita todavía no atiende:
  - solo los crean el intérprete de misiones, la interfaz o las acciones del jugador: 0x09 (0x4ECF64), 0x0C (operación 0x3A), 0x0F (0x4F), 0x10 (0x53), 0x11 (0x3D), 0x13 (0x5F), 0x14 (0xA4), 0x17 (0x72), 0x18 (0x2B) y 0x2D (0x35);
  - sin creador encontrado: 0x0B (0x4E37F4) y 0x29 (0x4DDE70); también 0x28 (0x4DDF40), cuyo único creador, 0x538DA0, es código muerto.
- [ ] Las demás operaciones a las que se llega desde advance_time pasan por partes aún sin reescribir. Hay que mirar si alguna de esas ramas es simulación:
  - las batallas (0x5FBFE0: 0x92–0xA0 y 0xB4);
  - el intérprete de misiones (0x4ECF64: 0x28, 0x7C, 0x7D, 0x82 y 0x8E; y por la interfaz 0x60–0x77, 0x8C, 0xA2–0xA8 y 0xBF);
  - los gráficos de la ciudad (0x599040, desde el evento 0x25 y la muralla: 0x08, 0x09, 0x0A, 0x2F, 0x39, 0x52, 0x56–0x59, 0x61 y 0x93);
  - 0x5C7810, desde los objetos del mundo (0x33, 0x34, 0x36, 0x37 y 0x98).
- [ ] C: las llamadas desde lo reescrito a funciones que p3vita no nombra (la lista, en el inventario)
- [ ] D: lo que marca el propio código:
  - [ ] «Por reescribir» (14). Ya están 0x4FF2D0, el almacén nuevo (tipo 0x1E) de un comerciante con oficina, que recuenta su oficina, y 0x51FAB0 construyendo de golpe (`construction.cpp`; `cloud/unchecked-branches`);
  - [ ] «no se reproduce» (115): repasar que ninguno toque el estado;
  - [ ] «Sin comprobar con el oráculo»: **quedan 250**. Hechas las 213 de los 11 ficheros del tick de ciudad (alderman, construction, new_town, facilities, offices, businesses, population, merchants, diet, grid y sieges; [cloud/unchecked-branches.md](cloud/unchecked-branches.md)):
    - 188 comprobadas con el oráculo, con 42 escenarios de `tools/oracle/retouch_unchecked.py`, que se lanzan con `tools/oracle/unchecked.sh`;
    - 23 explicadas en el código como inalcanzables;
    - 1 a la que no se llega (un redondeo en `businesses.cpp`);
    - 1 donde el propio juego falla (una división entre cero).

    Las que quedan están en events_rare (68), events_towns (45), events (45), operations (32), events_mayor (30), los barcos y los convoyes (28), y 2 más.
- [ ] Confirmar que nada de la simulación encola la operación 0x25 (0x535B7B → 0x5244F0, que llama a 0x51F3A0 y 0x4F8A50 sin `count`) ni la 0x40 y la 0x41 (despedir y reclutar soldados, 0x51D410 y 0x51D4C0, de la interfaz del alcalde). p3vita no las tiene, y hasta ahora no ha salido nadie que las encole
- [ ] E: los subsistemas aparte: las batallas (abajo), los guiones de misiones (fase 5), la vista de la ciudad (fase 2) y las acciones del jugador (fase 5)
- [x] Los tres pasos del final del cargador del juego (en 0x549F40): registrar los nombres de barco (0x5131B0), revisar la ruta de cada barco (0x5048C0, 0x517C10) y revisar las rutas comerciales (0x4D60B0). `tick_probe --carga` da 0 diferencias en las 12 partidas y en estados retocados ([cloud/statistics.md](cloud/statistics.md))
- [ ] El comercio con el kontor (operación 0x1B)
- [ ] Guardar las cartas y avisos nuevos en el buzón de cada comerciante como el juego (0x4D6530). El buzón ya se lee de la partida (`SaveGame::mail`, con los registros de las cartas antiguas en `SaveGame::mail_records`), y las cartas caducadas ya se quitan (0x4D6780). Pero las nuevas no se guardan, y la simulación lo consulta: el evento 0x16 no repite un aviso que siga vigente (42 días), así que sin esto llega antes de tiempo. Hacerlo junto con las cartas de la interfaz
  - [x] **El inventario** (03-11-2026; [formats/mail.md](formats/mail.md) con las tablas, resumen en [inventario.md](inventario.md), parte F):
    - el juego manda cartas desde **154 sitios** (84 funciones): 152 de la simulación y 2 de una ventana de la interfaz. Son 117 tipos de carta distintos, de los 134 que hay (0x00..0x85); cada tipo tiene su título («Naval battle», «Pirates», «Elections»…) y su texto en el exe;
    - **solo se guardan las cartas de los comerciantes humanos** (en las partidas, el jugador) y las de la **crónica**, la lista de noticias para todos (el comerciante −2: ascensos, fiestas, asedios, la peste, las elecciones…). Las cartas a la IA se tiran. Cada tipo tiene una clase (escrito oficial, carta personal, información comercial, aviso), y los ajustes del jugador dicen si la recibe y cuántos días la guarda (siempre, 40, 20, 10 o 5 días, o no recibirla);
    - de las 154, **97 están en funciones que p3vita ya reescribió** (hoy se las salta), 10 en funciones que p3vita nombra pero se salta enteras (el intérprete de misiones, el aviso del Gobernador, la ruta automática del convoy, el hundimiento) y 47 en eventos y operaciones del jugador que p3vita aún no tiene;
    - el parte de la batalla (0x29) lleva su texto en un búfer aparte, como las cartas 0x47 (elecciones), 0x63 (expedición) y 0x83 (embargo); las 0x3C..0x40 y 0x71 (de las misiones) llevan un registro con su búfer;
    - además de mandar, el buzón tiene otros 20 métodos: marcar leída, borrar, los ajustes de avisos, responder a una carta (aceptar o rechazar una oferta), reparar las cadenas… Los usan operaciones del jugador (0x6E, 0x6F, 0x70, 0x71, 0x52, 0x54, 0x55, 0x75, 0x76), el evento 0x13 y el intérprete de misiones;
    - la interfaz: la lista (0x47D570), el texto de cada carta (0x47F3B0, un `switch` por tipo de unos 28 KB) y la crónica (0x47DD40);
    - **el otro canal, 0x548CA0** (60 sitios): las líneas de aviso al jugador («%s interrumpe su viaje comercial», «Tu barco %s se ha hundido»…), 43 clases. No toca la simulación, solo la interfaz.
  - [x] **Reescribir 0x4D6530 y lo que llama** (03-11-2026; `src/p3data/mail.cpp`): mandar una carta, el filtro de los comerciantes humanos y de los ajustes, la cadena de cada comerciante y la crónica, ensanchar el buzón de 200 en 200, y copiar y vaciar las cartas con sus búferes (ahora estado vivo de la partida, `SaveGame::mail_buffers`). El oráculo deja pasar las cartas solo desde las llamadas ya conectadas (`MAIL_CALLS`) y graba los búferes (región `LB`). Conectadas ya las del tick del comerciante: las cuentas de cada semana (0x0F, 0x10 y 0x11) y el ascenso en su ciudad natal (0x06, a la crónica). Comprobado: el mundo entero 7 días en las 12 partidas, 0 diferencias (el jugador recibe ya sus cartas de cada semana); los escenarios `grow` (el buzón sin entradas libres crece en 200) y `promotion` (el jugador asciende el día 1) en las partidas 1, 3 y 5, iguales; guardar tras 8 días, los mismos bytes que el juego; y un test nuevo (`tests/mail_test.cpp`, 12 tests)
  - [ ] Conectar las otras 93 llamadas de lo ya reescrito, por grupos, comprobando cada grupo con el oráculo:
    - [x] **Las batallas en el mar** (03-11-2026; 14 llamadas): el parte de la batalla (0x60D299, la carta 0x29, con los textos del juego en el idioma del exe: el resultado, «Barcos propios», «Barcos capturados», «Barcos enemigos» y cada barco con su casco y si huyó, fue saqueado, tomado o hundido) a cada bando del ordenador, que hoy incluye al jugador porque su batalla la lucha el ordenador; la noticia del pirata vencedor en la crónica (0x2A); las cartas 0x72 de lo hundido, tomado y saqueado; las del capitán pirata hundido o tomado (0x54 y 0x2A); la paga del saqueo de un corsario (0x30); el corsario que vuelve a casa y el barco pirata hundido (0x2A). Comprobado: el mundo entero 7 días en las 12 partidas, 0 diferencias (en tut4 el jugador recibe el parte del «Viking»); los escenarios de piratas en las partidas 1, 3 y 5 y uno nuevo, `privateer` (los piratas pasan a ser corsarios del jugador, para que le llegue la paga), en 9 partidas: todas las llamadas iguales; guardar tras 8 días en las partidas 6, 7 y tut4: los mismos bytes que el juego. Tres ramas no salen ni con estados retocados y quedan marcadas «sin comprobar»: la 0x72 al bando atacante (solo si ataca un comerciante: la flota de la Hansa o el jugador, parte 7), las 0x54 y 0x2A de un convoy de comerciante que hunde un barco pirata con capitán, y la paga de un corsario que se queda con un barco (operación 0x91)
    - [x] **El tick del comerciante y los encargos** (03-11-2026; 8 llamadas): el aviso de reputación sin el valor de compañía suficiente (0x5E y 0x5F, que no se repiten si ya hay un 0x5F de ese rango), la candidatura a Gobernador (0x6D), el permiso para tener negocios en otra ciudad (0x82), el encargo de un comerciante (evento 0x05: al empezar 0x19 o 0x0A según el tipo, el fracaso 0x4A y el resultado 0x4B con la paga, la mercancía o el castigo) y el pretendiente (0x21 del evento 0x16, que ahora se guarda y el evento no se repite antes de tiempo). Comprobado: el mundo entero 7 días en las 12 partidas, 0 diferencias; escenarios nuevos `commission` (34 encargos del jugador: los 16 tipos al empezar, los 16 al acabar y 2 fallidos) y `reputation`, y el `jugador` de `retouch.py` (la candidatura), todos iguales
    - [x] **Los ficheros pequeños** (03-11-2026; 9 llamadas): la botadura de un barco (0x00) y el final de una obra (0x03, solo si el edificio sigue siendo del comerciante), la oficina terminada (0x60), la hambruna en la crónica (0x35), la nao que ya se puede construir (0x81), el ingreso en el gremio en la crónica (0x0C), la parte de la muralla que paga cada oficina (0x2F) y lo que cobra y paga un convoy al llegar (0x31, 0x32). Comprobado: el mundo entero 7 días en las 12 partidas, 0 diferencias; escenarios nuevos `hulk`, `wall_bill` y `convoy_fees` (llamadas directas), iguales
    - [x] **Los eventos raros** (`events_rare.cpp`, 03-11-2026; 21 llamadas): la caza de piratas ante una ciudad (0x41 al empezar, 0x42 con la recompensa de 4000, 0x43 a cada dueño de oficina que les da sus mercancías), la peste en la crónica (0x58), el resultado de la votación del consejo al alcalde y al que la pidió (0x6B), el encargo del jefe de la Hansa (0x77 a todos o al jefe, sus recordatorios y su fracaso; 0x78, la reunión), el impuesto extraordinario a cada oficina (0x6C, con lo que debe), el robo en una oficina (0x73, con lo robado), el monumento al alcalde (0x7B), y la expedición: el barco perdido (0x34), el descubrimiento (0x1C, 0x56) y **el informe** (0x63), un texto con lo encontrado, lo vendido y comprado en cada sitio, los contratos firmados o cumplidos, los piratas o la tormenta y el dinero, con los textos del juego en el idioma del exe (el exe español pasa los datos del contrato nuevo en otro orden, y se respeta). Hacía falta 0x515AE0 (el apellido del capitán de un objeto pirata). Comprobado: el mundo entero 7 días en las 12 partidas, 0 diferencias (la peste y la reunión salen solas); escenarios nuevos en `retouch_mail.py`: `pirate_hunt`, `council`, `hanse_task` (jefe humano), `hanse_task_ai`, `expedition`, `expedition_lost`, `expedition_four`, `expedition_villages`, `expedition_new_world`, `expedition_nothing` y `expedition_contract` (en los tutoriales, los únicos con una expedición en camino), y `robo`, `impuesto` y `monumento` de `retouch.py`: todas las llamadas iguales. Del informe no salen: dos poblados a la vez, un contrato cumplido del todo, el convoy sin guía, la expedición sin dinero, ni el orden del exe español (el oráculo usa el inglés). Sin comprobar con el oráculo
    - [x] **Los eventos de las ciudades** (`events_towns.cpp`, 03-11-2026; 14 llamadas): los avisos de una deuda (0x12 al deudor, 0x26 cuando se le devuelve al acreedor, 0x79), el embargo que cobra una deuda (0x13: con dinero, con la mercancía de su oficina, con la carga de un barco en el puerto o en parte), la fiesta del jugador (0x24), la boda (0x23 en la crónica y la dote 0x25, con los barcos y lo que llevan), el incendio de la ciudad en la crónica (0x59) y lo perdido en un almacén que arde (0x75), la oficina nueva en la crónica (0x0D) y el capitán que mejora (0x4D; la operación 0x12 usa ahora la misma función). Comprobado: el mundo entero 7 días en las 12 partidas, 0 diferencias (salen solos la oficina nueva y el embargo con dinero); escenarios nuevos `debts_notices`, `debts_office`, `debts_ship`, `debts_partial` y `captain` en `retouch_mail.py`, y `fiesta`, `incendio_ciudad` y `boda` de `retouch.py`: todas las llamadas iguales
    - [x] **Las elecciones, la Hansa y las subastas** (`events_mayor.cpp`, 03-11-2026; 9 llamadas y las 2 del aviso del Gobernador, 0x5299F0, que se saltaba entera y ahora está reescrita): la elección del alcalde (la carta 0x47 a los jugadores de esa ciudad, con el texto de los candidatos y sus votos, y la noticia 0x55 en la crónica), la del Gobernador (la 0x47 a todos con su texto, y la noticia 0x57), el resultado de una votación de la Hansa (0x0B, a todos) y el final de una subasta de barcos (0x2C o 0x2D al dueño, 0x5D al ganador, 0x2E si no se vende) o de edificios (0x76 o 0x7D al dueño, 0x7F al ganador, 0x7E si no se vende). Comprobado: el mundo entero 7 días en las 12 partidas, 0 diferencias (la elección del alcalde sale en las partidas 7 y tut1, y la del Gobernador en tut3); escenarios nuevos `mayor_election`, `alderman_merchant`, `alderman_citizen`, `hanse_vote` y `auction_end` en `retouch_mail.py`: todas las llamadas iguales. Al probarlo salió una rama que p3vita aún no reproduce y no es del buzón: subastar un barco que va en un convoy (0x509250); está marcada en el código y el escenario la evita
    - [x] **Los encargos de la Hansa** (`alderman.cpp`, 03-11-2026; 6 llamadas): al empezar uno, la carta de la carretera (0x80, con el plazo) o las dos de la guarida pirata (0x6E con su sitio y 0x84 con el plazo); el encargo vencido (0x70) y el cumplido (0x6F, a la crónica y al comerciante, con 0x80 en su tipo cuando llega a los 4 encargos). Comprobado: el mundo entero 7 días en las 12 partidas, 0 diferencias; los escenarios de `retouch.py` (`encargo_nuevo`, `encargo_cumplido`, `encargo_vencido`, `jugador_coge`, `jugador_vencido`) y uno nuevo, `hanse_task_letters` (los encargos de una candidatura dados al jugador): todas las llamadas iguales
    - [ ] Las demás, fichero a fichero (12)
  - [ ] Guardar y cargar las cartas nuevas
  - [ ] El buzón en el visor, con los textos del juego en español

### Llegar a Gobernador

El cargo más alto de la Liga Hanseática («Alderman» en la versión inglesa) es la meta del juego. El camino entero está en [formats/world_tick.md](formats/world_tick.md) §7 y §8: un alcalde con 120 de reputación pasa a candidato, la Hansa le ofrece encargos y, con tres cumplidos, se presenta a la elección.

- [x] La candidatura: el tick del comerciante (0x4F75F0) pone [+0x37] = 1, y 0x5326E0 (con 0x532E30 y 0x515A60) encola una operación 0x86 y hasta cinco 0x85, una por tipo de encargo. Idéntica al juego con estados retocados en las 12 partidas (`tools/oracle/retouch.py`)
- [x] Las operaciones 0x85 (0x53FD10: crea el encargo, un evento 0x32 sin comerciante, si no hay ya uno igual) y 0x86 (quita los encargos sin comerciante que ya no valen)
- [x] Los encargos sin comerciante (evento 0x32): maduran unos 64 días y se dan a un comerciante de la IA candidato
- [ ] Los encargos con comerciante (evento 0x32, 0x4EB0EB..0x4EBF0F, `alderman.cpp`): el principio (0x4EBF54), la comprobación hasta el plazo y, al cumplirse, [+0x37] + 1 (0x4EC354); con 4 se puede presentar
  - [x] lo común (el principio, el plazo vencido con su castigo de reputación 0x4F9170, el encargo fallido) y los tipos 2 (cazar un barco pirata: se cumple cuando la batalla pone [+0x16] = 1) y 4 (una ciudad pequeña que llegue a N habitantes)
  - [x] el tipo 1 (carretera entre dos ciudades), con los edificios 0x27 (0x521900 y 0x4FCBD0, solo ese camino). La IA nunca aporta materiales, así que su carretera siempre vence
  - [ ] el tipo 3 (guarida pirata): hecho el refuerzo de los piratas (barcos nuevos, 0x5144A0 y 0x516170). Falta el ataque: la batalla al llegar un barco a la guarida (0x6130B0, 0x611D80, 0x5FBFE0), que el emulador no ejecuta. Va con la pieza 6 de las batallas
  - [x] el tipo 0 (fundar una ciudad, `new_town.cpp`): al empezar crea una ciudad entera en plena partida (0x531D50: la plantilla `iso/towns/stadtplatz%.2u.tow`, el constructor, los candidatos, los capitanes, el astillero, las rutas y la matriz de tiempos de viaje) y su muralla (evento 0x39). La ciudad nueva sigue igual que en el juego los días siguientes
  - [x] el desarrollo de la ciudad nueva (evento 0x2F, 0x4EA0D4, a los 180 días de fundarla). Con su primer edificio municipal llega la oficina de su alcalde si es de la IA o, sin alcalde, la operación 0x46 (0x535FD6), que hace alcalde al comerciante de la IA con más rango. Después, con 1000 habitantes y según su muralla, le da el mercado, y con muchos pobres encarga un prestamista. Idéntico al juego con estados retocados dirigidos a cada rama ([oracle.md](oracle.md), «Los encargos de la Hansa»)
  - [ ] que se cumpla el de fundar. Se cumple cuando la ciudad tiene mercado, y eso solo lo da el evento 0x2F cuando la ciudad tiene su primer edificio municipal y pasa de 1000 habitantes.
    - p3vita lo hace todo como el juego hasta el día 420: edificios municipales hacia el día 40, muralla, y empieza a crecer (272 habitantes).
    - Medido con el código del juego en el emulador (`grow.sh`, partida 1): la ciudad se queda en 272 habitantes hasta el día 520 y sin edificios nuevos, también ejecutando todas las operaciones (empieza una segunda muralla que se atasca sin herramientas). El plazo del encargo es de unos dos años, así que en el emulador no parece llegar a cumplirse.
    - Pendiente: medir más allá de 550 días (con el oráculo rápido ya cabe mejor; usar `--desde`) y ver si en el juego de verdad crece porque alguien le lleva mercancías.
  - [ ] la ciudad nueva en el visor de la Vita (sin probar)
  - [ ] por reescribir dentro de la fundación (`new_town.cpp`, marcadas «Por reescribir»):
    - [x] 0x5462F0: el nodo de una instalación en la lista [0x9B4] de la ciudad (0x50FCA0, 0x50FE00). Era la única a la que se llega al fundar: cuando el aceite (mercancía 0xA) es de las que más faltan al elegir el sitio (0x532E30 añade el bit 0x40000). Idéntica al juego con el escenario `fundar_aceite`
    - [ ] 0x545CE0 y 0x545F50: preparar una ciudad que ya existe (sus edificios y su mercado). Al fundar no se llega ([+0xF] = 0 en el registro)
    - [ ] 0x4F99B0: la ciudad sin plantilla de mapa. Las 28 plantillas existen
    - [ ] la elección del alcalde dentro de 0x531D50 (0x532063..0x5321C5, con el bit 0x40 de la situación). Al fundar la situación es 0
- [x] Que el jugador coja un encargo (operación 0x4B, 0x53B440) y aporte materiales a la carretera (operación 0x3B, 0x53A3E0)
- [x] La parte del jugador en el visor ([cloud/alderman-viewer.md](cloud/alderman-viewer.md); `src/p3data/hanse_tasks.cpp`). Con Cuadrado en una ciudad se abre el panel de la Hansa, que muestra:
  - quién es Gobernador y la fecha de la próxima elección;
  - si el jugador es candidato o qué le falta (ser alcalde y llegar a 120 de reputación);
  - los encargos con su plazo, para coger uno (operación 0x4B, con los mismos bytes que la ventana del juego 0x5E87D0: {0x4B, jugador, evento});
  - el avance del encargo cogido;
  - en las ciudades de la carretera, lo que falta y lo que hay de cada material, para entregarlo (operación 0x3B, como la ventana 0x5F2A20: {0x3B, u16 evento, u16 cantidad, ciudad, jugador, mercancía}; la cantidad es la menor entre la elegida (1, 5, 50 o todo), lo que hay y lo que falta);
  - un aviso con el resultado de cada elección.

  Funciona con botones, teclado y táctil. `--debug-state mayor|candidate|materials|level4|alderman` pone al jugador en los estados a los que no llegan las partidas. Comprobado con 8 escenarios de `tools/oracle/retouch_player.py`, que encolan los bytes del visor en el juego (coger cada tipo de encargo, un segundo encargo rechazado, un jugador que no es candidato, las entregas con cada cantidad): 0 diferencias en las partidas 1 y 3. Compila para la Vita; falta probarlo en ella
- [x] Comprobar la elección con el jugador de candidato. Con el jugador a nivel 4 y la elección adelantada al día siguiente, gana en las partidas 1 y 3, igual en el juego que en p3vita: 0 diferencias en 0x4D85C0 y 0x535760 (escenario `election_level4` de `retouch_player.py`). Antes perdía porque la elección llegaba 117 días después, con el estado ya cambiado. En el visor: `--debug-state election-tomorrow` ([cloud/alderman-viewer-2.md](cloud/alderman-viewer-2.md))
- [x] El voto del jugador: la operación 0x3F (0x53AA10, `town_tick/votes.cpp`), que la ventana de la Hansa encola como {0x3F, jugador, fila}; sirve también para las votaciones de la dieta. Está en `run_operation` y en `OPERATIONS` del oráculo. Comprobado con 10 escenarios × 2 partidas, a 0 diferencias: votarse a sí mismo, a otro, dos veces, el día antes, el día después, con rango 3, una fila que no vale, y en la dieta sí, no y abstención
- [x] La lista de candidatos: la página «elección» del panel (Select). Antes del día, con la misma función del juego (0x529700); el día de la elección, con sus votos. X vota
- [ ] La candidatura a alcalde del jugador. «Aceptar la candidatura» no existe para Gobernador: el juego hace candidato al alcalde solo. La pregunta «Accept candidature?» es del ayuntamiento de la ciudad natal (0x5E387E, botones [+0x604] y [+0x6EC]) y parece la candidatura a alcalde (el evento 0x02, sus candidatos [0x6D4..0x6E7] y sus apoyos [0x6DC]). No se sabe aún qué encolan esos botones. Sin tarea por ahora
- [x] La elección en la reunión de la Hansa (evento 0x20)
- [ ] El final. Hecha la ventana, con los textos del juego («Oficina del Gobernador» y «… ha sido elegido Gobernador de la Liga Hanseática por N votos.»). Falta el vídeo `eldermann`, con los vídeos Bink. El objetivo de la partida (evento 0x0E) ya se comprueba
- Las partidas de prueba no llegan a esto en 60 días (ningún alcalde comerciante llega a 120), así que se comprueba con estados retocados, unos pocos dirigidos a cada rama

### Piratas, persecuciones, encuentros y batallas

El estudio está en [cloud/battles.md](cloud/battles.md).
- Hay dos clases de batalla: en el mar (0x1E98 bytes) y en una ciudad, que incluye la guarida pirata (0x2F24). Caben hasta 255, en 0x6E55D0, y se guardan en la partida.
- Sin la interfaz son 249 funciones (148 KB) en el mar y 41 (50 KB) en la ciudad.
- advance_time solo las empieza. Las hace avanzar la operación 0x94, que encola el bucle de tiempo real (0x546620 → 0x612DB0) con pasos sacados del reloj: de media, 84 por tick a cualquier velocidad.

Decidido: p3vita y el oráculo harán avanzar las batallas con una operación 0x94 de **84 pasos después de cada tick en que haya batallas**. El juego no es determinista aquí, porque depende del reloj real, así que cualquier regla fija es una elección. El resultado de una batalla automática no depende de cómo se repartan los pasos; solo cambia el tick en que acaba.

El plan, por piezas, cada una detrás de su ajuste hasta comprobarla:
1. [x] Las tormentas (arriba).
2. [x] Los piratas que se mueven (`cloud/battles` y `cloud/battles-2`, [cloud/battles-2.md](cloud/battles-2.md) §1–2; `src/p3data/battle/pirates.cpp`, `pirate_nests.cpp`, `pirate_objects.cpp`). **Encendidos por omisión desde el 01-11-2026** (decisión del usuario), también en el visor: `Sailing::set_pirates`, y `--no-pirates` en `town_oracle.py` y `tick_probe` para apagarlos:
   - el estado 0x12 del convoy pirata (0x507A65), con el nuevo rumbo (0x505C90), la elección del blanco (0x50D9E0), la decisión de atacar (0x515360), la velocidad de batalla (0x612930), los bajos (0x50E340), 0x5159C0 y los puntos de cada ciudad (0x448780, al cargar);
   - el «pool» y el mapa de la guarida de cada pirata, como memoria del juego (`SaveGame::pirate_nests`; regiones `ZMi`, `ZDi`, `ZPi` del oráculo), leídos y escritos como el juego;
   - deshacer el convoy en la guarida (0x514B80, 0x515DA0, 0x513BF0), el barco perdido y el pirata solo (estados 0x11 y 0x12: 0x506AF2, 0x517170, 0x507022, 0x507099, 0x5062F0), la rama pirata de 0x5017F0, el día de los objetos pirata (0x514000, 0x513EC0) con la operación 0xB5 (0x514D40, 0x515750, 0x5185C0);
   - el ataque del evento 0x1E (0x5063C0, 0x513D80, 0x515EA0), conectado el 01-11-2026 en `events_rare.cpp` (estados 2 y 3 de 0x4DFD44, con los piratas encendidos). Ninguna partida llega ahí: escenario `event_attack` de `retouch_pirates.py` (un barco del jugador zarpa y dos eventos 0x1E, en los estados 2 y 3, saltan en el mismo tick), partidas 1, 3 y 5: 66 llamadas iguales de 0x4D85C0, 2 de 0x5063C0 y 66 de 0x506720, 0 distintas. El tick siguiente ya es una batalla con un lado humano (parte 7), que el oráculo no puede seguir;
   - comprobado: llamada a llamada en las 12 partidas × 7 días y con 12 escenarios retocados de `tools/oracle/retouch_pirates.py` (todas las ramas que se pueden alcanzar), 0 diferencias; el mundo entero con los piratas, 0 diferencias en las 12 partidas;
   - de la regla provisional solo queda el segundo salto: un convoy que acaba su ruta en una ciudad sigue como si navegara, en vez de atacarla (la batalla en una ciudad, pieza 6).
3. [x] La batalla automática en el mar ([cloud/battles-2.md](cloud/battles-2.md) §3–4; `src/p3data/battle/`):
   - el oráculo graba las batallas como una región `BT` (el objeto de 0x6E55D0 y todos los bloques que cuelgan de él, con los punteros cambiados por índices; `BA`, sus direcciones) y p3vita las tiene en `SaveGame::battles`, que tick_probe compara;
   - la regla del reloj: una operación 0x94 de 84 pasos después de cada tick con batallas (`Sailing::step_battles`);
   - empezar (el ataque 0x50BC40 y la preparación 0x605420), **cada paso** (la operación 0x94 con el método +0x14, 0x606CC0: el movimiento, el blanco, el abordaje, la captura, los disparos, los choques, el viento) y **acabar** (0x60D299, el destructor 0x611740/0x603BC0, los convoyes después 0x50BFD0, la tabla de los barcos que se encontraron 0x612BD0): iguales llamada a llamada (0x606CC0, 407 llamadas en 6 partidas × 7 días);
   - el aviso al mapa del mar de que hay un barco del jugador en la batalla ([+0x1E92]), reescrito;
   - [x] **La batalla de un barco del jugador** (03-11-2026). Al empezar una batalla contra el jugador, el juego le pregunta «¿Quieres dirigir manualmente a los barcos en la batalla naval?» (en inglés solo «Navigate manually»). Si no la dirige, o si ya tiene otra batalla abierta, la operación 0x96 (0x60FEC9) pasa sus lados al ordenador ([+0x5D] = 1) y la batalla queda automática ([+0xAC8] = 1). p3vita aún no tiene esa ventana, así que hace lo mismo que el juego cuando el jugador está ocupado con otra batalla (0x606BAE): la 0x96 con 1 en seguida. El paso de la batalla ya la encolaba y nadie la ejecutaba; por eso el barco se quedaba parado. Ahora está en `run_operation` y en el oráculo, que salta la ventana igual que p3vita (`BATTLE_WINDOW_JUMPS`). Además:
     - el aviso del abordaje y del saqueo, que el juego escribe en la batalla ([+0x1310]) cuando hay un barco del jugador: «La tripulación del "%s" ha capturado / saqueado la nao "%s" del pirata desconocido» o «Tu barco "%s" ha sido apresado por / fue saqueado por…», con el dueño del otro barco (el pirata con su nombre y apellido, el pirata desconocido, o el comerciante). Con los textos del exe (`GameTexts::raw`, `Sailing::set_texts`): en inglés en las comprobaciones y en el idioma de la instalación en el visor. Los nombres y apellidos, exactos como el juego (0x512A60 y 0x512AE0, `game_first_name` y `game_family_name`);
     - comprobado: el mundo entero 7 días en las 12 partidas, 0 diferencias (la batalla guardada de tut4, con un barco del jugador, ya se resuelve sola: el «Viking» captura una nao pirata); el ataque del evento 0x1E (`event_attack`) seguido 3 días en las partidas 1, 3 y 5, 0 diferencias; los 12 escenarios de `retouch_pirates.py` y los 19 del jugador, como antes;
     - los avisos del final de la batalla en [+0x1310] (0x60E03B..0x60E8CE: «Has derrotado a…», «… te ha derrotado», «… ha huido») solo salen con un lado humano, es decir, en la batalla manual (parte 7). Con la batalla automática, el parte le llega al jugador como carta (0x4D6530), que p3vita aún no guarda (el buzón, arriba);
     - el tiempo de fuera durante una batalla manual: en la parte 7;
     - [x] **en el visor** (03-11-2026): una ventana con los avisos del juego para el jugador, uno tras otro (X para seguir). Al empezar una batalla contra el jugador sale el aviso del ataque, el texto de la pregunta del juego sin la pregunta: «Batalla naval: la carabela "Gelve Swan" del pirata Dietrich Beringer está atacando a tu goleta "Constance".» (`Sailing::attack_message`, 0x60659B..0x606AF5). Con cada abordaje de un barco del jugador sale el aviso del abordaje o del saqueo. Los dos, con los textos del exe de la instalación; p3vita los deja en `Sailing::take_player_messages`, sin tocar la memoria del juego. El juego no enseña el del abordaje en una batalla automática (lo escribe, pero solo abre la ventana con un lado humano); el visor sí lo enseña, para que se sepa qué ha pasado;
     - comprobado el aviso del ataque contra el juego: con el camino de la ventana en el oráculo (solo para la prueba), el juego escribe su texto en [+0x1310], y es el mismo que el de p3vita en las partidas 1, 3 y 5 (`event_attack`; un barco solo, uno en convoy, un nombre con «ö»). Ojo: el oráculo no ejecuta el inicializador 0x46BFC0, que rellena al arrancar la tabla de «Your snaikka», «Your convoy», «the convoy» (0x6A1778..0x6A17A0), y sin ella el `sprintf` del juego escribe «(null)». En el exe español esa tabla ya viene escrita;
     - para probarlo: `--debug-state pirate-attack` (el primer barco del jugador zarpa y los piratas lo atacan dos días después, como el escenario `event_attack`) y `--save missav/tut4.pat` (empieza con la batalla guardada del «Viking»). `tick_probe --messages` saca los avisos de p3vita;
     - [ ] falta el resultado. Con la batalla automática el juego manda al jugador el parte por carta (0x60D299: «Barcos propios:», «Barcos enemigos:», cada barco con su casco y si fue saqueado o tomado; 0x4D6530 con el tipo 0x29). Hay que reescribir ese parte y la carta, junto con el buzón (fase 4, «Guardar las cartas...»). Y la batalla en el mapa del mar ([+0x1E92]: el juego avisa al mapa, 0x457C90 con la posición [+0xAD6], [+0xADA], y el mapa borra la marca cuando la ha visto);
   - falta, porque las partidas no lo alcanzan: las balas de cañón y el lado humano (parte 7; la batalla de tut4 es el caso para empezar), las rocas e islotes como obstáculos y algunas ramas de los choques (§4);
   - [ ] **las rocas dependen de la vista de la ciudad**: 0x60F479 multiplica sus cajas por la mitad del ancho de la vista de la ciudad (*0x6E51AC, interfaz), que el emulador no tiene; ahí valen 0, y p3vita hace lo mismo (`kTownViewHalf`). El oráculo y p3vita coinciden entre sí, pero no con el juego. Recomendación de la sesión: dar al oráculo una vista de la ciudad con el `[+0x510]` del juego cuando se sepa su valor, y usar el mismo en p3vita;
   - [ ] **el aviso al mapa del mar** ([+0x1E92]): el juego lo pone cuando un barco del jugador está en la batalla y solo lo borra si tiene ventana principal. En la simulación se queda puesto. Propuesta de la sesión: que lo borre el visor al enseñar la batalla (va con la parte 7). Pendiente de explicárselo mejor al usuario;
   - la operación 0x9F, sin hacer.
4. [ ] Las otras batallas en el mar: la persecución que alcanza al barco (0x50B250, también la operación 0x99) y la flota de la Hansa en una ciudad protegida (0x50BA80).
5. [x] Las batallas en la partida guardada, las del mar ([cloud/battles-2.md](cloud/battles-2.md) §5): leer como 0x611AE0 (0x604600, 0x61A880) y escribir como 0x611950 (0x604220, 0x61A790), en p3vita y también en el oráculo, que antes no las leía. **Tres partidas tienen una batalla guardada**, todas de piratas contra un comerciante: la 6 y la 7 (3 barcos, automáticas) y tut4 (4 barcos, con un lado humano: espera la ventana del jugador, la operación 0x96 en cada paso). Las tres empiezan ya con su batalla. Comprobado: escritas tras 1–2 días, con y sin piratas, y en mitad de una batalla (partida 6 a los 3 y 20 ticks, la 7 a los 3), los mismos bytes que el juego, y el juego vuelve a leer lo que escribe p3vita igual que p3vita. Faltan las batallas en las ciudades y las balas de cañón guardadas.
6. [ ] La batalla en una ciudad (los piratas contra una ciudad y la guarida pirata):
   - 41 funciones (0x5FBFE0, 0x621770, 0x5FE7E0, 0x624950...);
   - las operaciones que encola: 0x92, 0x95–0x98, 0x9A, 0x9B, 0xA0 y 0xB4;
   - la geometría de la vista de la ciudad (0x58D030, 0x58B740), que el emulador no tiene;
   - lo que cuelga de ella: la guarida 0x515C00, los edificios quemados de los eventos 0x25/0x26 y el ataque del encargo de la Hansa de tipo 3.
7. [ ] La batalla que dirige el jugador: la ventana (0x41D9B0), el método +0x10 y 0x61A880 con sus controles, y las órdenes (operaciones 0x92, 0x93, 0x98 y 0xA0 desde la interfaz). Va con la interfaz.
   - **El tiempo de fuera durante la batalla** (visto en el código, sin comprobar jugando): mientras alguna batalla tiene un lado humano ([+0xAC8] = 0), el juego encola la operación 0xC8 con 2 (0x612C90) y el bucle de tiempo real (0x546620) pasa al «modo batalla» ([0x6DF2F0 + 0x92C] = 2, manejador 0x546DCF): el mundo avanza como mucho un tick cada 3375 ms ([0x673CF8]), sea cual sea la velocidad elegida (a velocidad normal, 234 ms), unas 14 veces más lento, y no hay guardado automático (la operación 0xC2 no se encola). Cuando ya ninguna tiene lado humano, la 0xC8 con 0 (0x612D20) vuelve a la velocidad elegida. p3vita ya encola las dos 0xC8; el visor tendrá que aplicarlas.
   - **Hasta que esté**, la batalla de un barco del jugador la lucha el ordenador, como cuando el jugador contesta que no a la pregunta (pieza 3, arriba). Con la ventana habrá que hacer la pregunta con sus dos respuestas (la 0x96 con 0 o con 1) y lo que ahora se salta: el texto del ataque en [+0x1310], la descripción de la ventana ([+0x12EC]), [+0x12E9], [+0x12EA] y [0x6E59CD].

### Corregir los fallos del juego (opcional)

Un ajuste (feature flag) **activado por omisión** con el que p3vita corrige los fallos que vayamos encontrando. Apagado, se comporta como el original, para jugar a la versión original y para las comprobaciones con el oráculo (que comparan con el juego tal cual).

- [x] El ajuste:
  - `fix_game_bugs()`, en `src/p3data/game_fixes.hpp`. Cada corrección va con `if (fix_game_bugs())` al lado del comportamiento original.
  - En la librería está apagado, así que el oráculo, tick_probe y los tests comparan con el original sin tocar nada.
  - El visor lo enciende por omisión desde su pantalla de **Opciones** (Start en la Vita, Escape en el PC), que también tiene «Salir del juego». Se guarda en `p3vita-settings.txt`, en la carpeta de datos (`ux0:data/p3vita/` en la Vita), y `--fix-game-bugs on|off` lo cambia por una vez.
  - `tick_probe --fix-game-bugs` enseña qué cambian las correcciones frente al juego.
  - Comprobado: con el ajuste apagado, el mundo entero da 0 diferencias en las 12 partidas. Encendido, solo cambia lo que corrigen (abajo). `tests/game_fixes_test.cpp` prueba las correcciones sin datos del juego.
- Cada fallo se corrige en el fichero de la sesión que lo lleva; las que están en marcha lo hacen en sus ficheros.

- [x] **La carretera vencida del jugador** (0x4EAFE4, en 0x4EB25F). **Corregido con el ajuste** (`alderman.cpp`, escenario `road_expired`). La copia se crea sin comerciante (0xFF) y el propio juego la quita en el tick siguiente (0x532C50), así que la cola de eventos queda igual que en el juego. La carretera entre esas dos ciudades no se vuelve a ofrecer, igual que cuando la IA falla una (el bit 0x8000 sigue); para volver a ofrecerla habría que quitar también ese bit. Lo que hace el juego: cuando al jugador humano se le vence el encargo de la carretera, el juego crea un encargo igual, ya empezado. Como su comerciante [+0xD] pone la ciudad [+0x10], que es un **índice de ciudad, no de comerciante**, y en sus materiales 0xFF y 0xFFFFFFFF (1023 de cada uno). Si ese índice es menor que el número de comerciantes (lo normal: 24 ciudades y unos 37 comerciantes):
  - ese encargo no vence nunca: los tramos se hacen solos cada día y medio y, cerca del plazo, siempre tiene materiales para lo que falta, así que el plazo se alarga;
  - la ciudad acaba con la carretera construida (los edificios 0x27 en las dos ciudades) sin que nadie haya aportado nada;
  - al cumplirse, 0x4EC354 sube el [+0x37] del comerciante que tenga ese índice. Un comerciante cualquiera avanza en la candidatura a Gobernador sin haber hecho el encargo; si es el jugador, le cuenta un encargo que no hizo. Si ese comerciante no es candidato, pasa de 0 a 1 sin haber sido alcalde.

  Si el índice no es menor que el número de comerciantes, el encargo cuenta como sin dar y se quita enseguida (las dos ciudades ya tienen el bit 0x8000). Corrección posible: no crear la copia, o crearla sin comerciante (0xFF). Otras rarezas del mismo encargo, también reproducidas: el plazo inicial (0x5326E0) cuenta los días como meses y lee la tabla de los meses (0x672D78) fuera de sus límites, hasta la entrada 129. Además, la operación 0x3B con un comerciante que no existe (0x53A4CA) no saca nada de oficinas ni barcos, y deja en el contador del material del encargo «contador + cantidad − la dirección del evento». Ahora se reproduce con el ajuste apagado; encendido, deja el encargo como estaba.
- [x] **Las mejoras de las piezas de muralla** (la operación 0x22 de los tipos 0x34 y 0x35, 0x523D70, `construction.cpp`). **Corregido con el ajuste**: hace las cuentas como con las torres, las herramientas con su cantidad y su precio, y se le paga al comerciante (escenarios `gate_costs` y `tower_ops`). Comparada con la de las torres (0x523830), que hace bien las cuentas:
  1. Las herramientas se sacan de la oficina del comerciante con la cantidad de **madera** que falta: baja el contador de la madera, y la ciudad pierde de su almacén todas las herramientas y las paga enteras.
  2. Lo que se le paga por ellas es 1,278 · 0: multiplica por la constante 0x66BC6C, que vale 0, en lugar de por lo sacado.
  3. Al comerciante no se le paga nada de lo que sale de su oficina, aunque la ciudad lo apunta como gasto (0x529F90).

  Los dos primeros son fallos casi seguro; el tercero lo parece. No sale en 30 días de las 12 partidas; comprobado con estados retocados (`gate_costs`).
- [x] **Los del tick de ciudad** ([formats/town_tick.md](formats/town_tick.md) §8). **Corregidos con el ajuste** (escenarios `town_tick_fixes` y `pier_edge`): el administrador cuenta las empresas de todas las oficinas; 0x5109B0 resta una sola vez lo que se va; la granja de grano compara como las demás instalaciones; el muelle solo marca las celdas que caen dentro de la rejilla. Los tres primeros actúan jugando normal: con el ajuste encendido, el mundo cambia en 7 de las 12 partidas en 7 días. Lo que hace el juego:
  - el administrador de 0x51C360 solo cuenta la primera oficina;
  - 0x5109B0 deja negativo [+0xC];
  - la granja de grano compara al revés al contratar;
  - 0x524920 escribe fuera de la rejilla con un muelle en la última columna.
- [x] **La IA de los comerciantes con deudas** (0x4F5A20, al elegir qué edificio poner en venta, operación 0x28). La renta del mejor ([esp+0x2C]) no se inicializa, así que lo que hace depende de lo que haya quedado en la pila de la llamada anterior. En el oráculo suele ser 0 y nunca pone nada en venta; tras un impuesto extraordinario (evento 0x31) en el mismo tick, sí. p3vita usa 0. Desde que las cartas ejecutan 0x4D6530 de verdad (03-11-2026) dejan otros restos en la pila (la del impuesto, por ejemplo), así que el oráculo pone a 0 los locales de 0x4F5A20 al entrar (`ZEROED_FRAMES` en `town_oracle.py`) para comparar siempre con lo que hace p3vita. **Corregido con el ajuste**: el primer edificio con unidades pasa a ser el mejor, y los demás se comparan por renta por unidad. Con el ajuste encendido, en la partida tut0 un comerciante de la IA con deudas pone en venta un edificio que el original no ponía (operación 0x28 antes del préstamo 0x29) (`events.cpp`)
- [x] **El capitán de la IA que se retira** (evento 0x27, 0x4DDC00). Si ya no está en su barco, el juego lo busca solo en el primer barco de cada comerciante y, si no es ese, se queda en un bucle sin fin (0x4DDCB0). p3vita quita el evento. **Corregido con el ajuste**: busca al capitán en todos los barcos de cada comerciante ([+0xE] y luego [+0x4]) (`events_towns.cpp`)
- [x] **La operación 0x80** (en 0x536756; solo en partidas en red, la encola el evento 0x05 al empezar un encargo). Lleva en [+4] el registro de la cola que toca (la cabeza, el encargo que empieza) y los dos números al azar para sus [+0x12] y [+0x14]. Pero el manejador escribe siempre en el registro 0x80 (0x4D8DB0 con la constante 0x80), y solo si ese es un encargo, así que el encargo que empieza se queda con 0xFFFF en los dos. **Corregido con el ajuste**: usa el registro de [+4] (`operations.cpp`)
- [x] **Las cartas antiguas** (0x4D6780). Una carta de tipo 0x86 o mayor corta la cadena del comerciante: el enlace a ella pasa a 0xFFFF. Si además es lo bastante vieja para quitarla, se pierde la lista libre del buzón: su cabeza pasa a 0xFFFF, el registro que tenía el enlace cortado. No sale en las partidas. **Corregido con el ajuste**: no se corta la cadena, así que la carta quitada va a la lista libre (`events_towns.cpp`; probado en `game_fixes_test`)
- [x] **El apellido de un comerciante en quiebra** (0x4F5D80, operación 0x65). **Corregido con el ajuste** (`merchants.cpp`): módulo el número de apellidos, sin contar al propio comerciante y como mucho una vuelta. Además, la búsqueda puede no acabar nunca: el apellido del propio comerciante ya es 0 (0x4F68D0), así que, si están cogidos todos hasta el divisor, da vueltas sin fin (p3vita lo reproduce con el ajuste apagado). Lo que hace el juego: El apellido nuevo es ([0x6DD888] + día) módulo el índice del apellido del *último* comerciante (el registro que queda del bucle que marca los apellidos en uso), no módulo el número de apellidos. Si ese índice es 0, el juego divide entre cero (p3vita deja el apellido viejo), y la búsqueda de uno libre da la vuelta en ese índice. Corrección posible: usar el número de apellidos ([0x6DDB74])
- [x] **El ejército sitiador nuevo** (0x629C60). **Corregido con el ajuste** (`sieges.cpp`, escenario `siege_fixes`): como mucho 100 grupos, no cuenta las puertas que no encuentra y solo suma los objetivos que de verdad se llenaron.
  - Corrección del análisis: los grupos ocupan 8 bytes desde [+0x254], así que desde el grupo 101 ya pisan los campos del propio ejército ([+0x574], las fuerzas, la ciudad [+0x5D3]...), no a partir de 0x7F. En el oráculo el juego ni siquiera marca su ciudad como sitiada.
  - Con el ajuste apagado, p3vita guarda los grupos que no caben en un almacén aparte, porque no puede reproducir esa corrupción.
  - Con la puerta no encontrada, p3vita lee (0x30, 0xEF), que es lo que tiene el oráculo en la dirección 0 en ese momento; en Windows el juego falla ahí.

  Lo que hace el juego:
  - no mira cuántos grupos caben, y con más de 0x7F se sale del ejército;
  - si no encuentra las tres puertas, lee el registro de la dirección 0 (en el juego, un cuelgue);
  - al sumar la fuerza de los objetivos cuenta las torres cerca de la puerta en obras, aunque 0x62B2B0 no las haya puesto de objetivo (lee entradas sin llenar).

  No pasa en las partidas; p3vita lee un registro a 0 y no escribe fuera.
- [ ] **El reloj de las batallas** (0x612DB0; sin confirmar). Mueve su tiempo [+0xC44] 40 ms por cada *paso* en lugar de por cada 40 ms consumidos, así que las batallas avanzan a la mitad de lo previsto (unos 50 pasos por tick a 60 fotogramas por segundo y 40 a 25) y su velocidad depende de los fotogramas. No cambia el resultado, solo lo que dura
- [ ] **Los de los piratas y las batallas en el mar** (`cloud/battles-2`, [cloud/battles-2.md](cloud/battles-2.md), «Game bugs»). Todos reproducidos tal cual; ninguno corregido aún con el ajuste. Seguros:
  - **los edificios de la guarida no se ponen nunca en su mapa**: 0x515DA0 llama a 0x4F9C80 con el tipo y la orientación cambiados de sitio, así que busca un solar de un tipo imposible y nunca lo encuentra; solo queda la entrada en el «pool». Arreglo: cambiar de sitio esos dos argumentos;
  - **0x514B80 mejora al capitán del guía una vez por cada barco con capitán** (encola la operación 0x12 para el guía, no para cada barco): el capitán del guía mejora hasta n veces y los demás nunca.

  Parecen fallos, sin comprobar con el juego:
  - 0x61A9E0 comprueba el borde de arriba con la x en vez de la y: cerca del borde izquierdo el barco que persigue evita también subir, y cerca del de arriba no;
  - 0x605420 y 0x606CC0 leen el primer barco del convoy de un lado sin mirar si el convoy existe: con un barco solo leen fuera de la lista de convoyes (p3vita no cuenta ningún barco);
  - 0x60ADBF deja sin poner el lado que encontró y 0x608F70 lo lee de la pila sin inicializar (p3vita pone 0);
  - 0x60BCDA se puede colgar con un radio de 0 (p3vita para los bucles);
  - el bucle de nombres de 0x5063C0 no acaba con menos de 307 nombres (con los del juego no pasa);
  - el convoy de un pirata que se va (piratas apagados en las opciones con un convoy en el mar) se queda sin guía y 0x507A65 lee fuera de la lista de barcos (p3vita se salta esa ordenación). **También pasa jugando** (03-11-2026): cuando el barco del jugador captura el guía de un convoy pirata en la batalla (`event_attack`, partida 1). El juego sigue con el barco 0xFFFF, fuera de la lista, hasta ordenarlo por x (0x50D0C0). En Windows lee la memoria que haya ahí; en el emulador son ceros, que convierten la lista en un anillo, y el oráculo se quedaba en un bucle sin fin. Ahora el oráculo vuelve en seguida de 0x50D0C0 con un barco que no existe, y p3vita no hace nada con un convoy sin guía (antes leía fuera del vector, lo que en la Vita podía cerrar el visor).
- [ ] **La batalla en una ciudad lee la vista de la ciudad** (*0x6E51AC, 0x58D030; sin confirmar). Toma su geometría de la ciudad que mira el jugador, no necesariamente de la atacada. Falta ver qué hace el juego cuando no coinciden
- [x] **0x62B2B0 no mira cuántos objetivos caben** (seguro por el código, 0x62B2F8..0x62B3B7). Reserva [+0x250] para [+0x242] entradas y luego escribe una por cada torre 0x32/0x33 cerca de la puerta, sin mirar ese tamaño: con más, escribe fuera de su bloque. Solo lo llama el cargador, y en las partidas [+0x242] siempre es 0. p3vita agranda el vector, que ya es la corrección (`sieges.cpp`)
- [x] **El nombre de la ciudad natal al guardar** (0x549D7D). El escritor copia en los ajustes de la partida ([+0x27]) el nombre de la tabla 0x6DDA00, que va por número de ciudad (0..39), pero usa el índice en la partida de la ciudad natal del jugador. En `1.pat` escribe «Cologne» (número 9) por Lübeck (índice 9, número 14). Seguramente es el nombre que sale en el menú de cargar; no comprobado. Reproducido en `update_settings`. **Corregido con el ajuste**: el nombre se toma por el número de la ciudad (tabla 0x6DE4B8): «Luebeck» en la partida 1, «Rostock» en la 3, «Gdansk» en la 7, la ciudad natal del jugador en las 12
- [x] **El relleno de las batallas** (0x611950). Cada entrada de la tabla del final de las batallas ocupa 8 bytes en el fichero; el lector solo guarda 5 (0x612A20 lee dos u32 y se queda con el byte bajo del segundo), y el escritor escribe 8, así que 3 bytes salen de lo que hubiera en la memoria (0xCD en la partida 7, 0 en el emulador). **Corregido con el ajuste**: se escriben a cero, recorriendo la sección como la escribe el juego (0x604220); si el recorrido no cuadra, no se toca nada. En la partida 7, los 12 bytes coinciden ya con los del emulador
- [x] **Los nodos del evento 0x34 cambian en cada carga y guardado** (0x4F3A40/0x4F39E0). El lector rellena dos veces el u16 [+0x6] con el tercer byte del nodo y se salta el cuarto; el escritor escribe el signo del byte bajo de [+0x6] y ese byte. Un nodo `b0 b1 b2 b3` se guarda como `b0 b1 signo(b2) b2`, y la lista siempre acaba en 0. Reproducido en `write_event_nodes`, igual que el juego. **Corregido con el ajuste**: cada nodo se guarda tal como se leyó (por el código, lector y escritor querían un u16 en big-endian; parece fallo, no es seguro)
- [x] **Leer un `.pst` pone a cero los contadores del comerciante 0** (`cloud/statistics`, §2). El juego construye cada objeto de estadísticas con el registro del día del comerciante 0, y ese registro pone a cero sus 15 contadores [0x484..0x4CC]; luego el fichero sobrescribe el registro entero. **Corregido con el ajuste** (`read_statistics` en `statistics.cpp`): el registro se toma sin poner a cero los contadores; el objeto leído sale igual byte a byte. Con el ajuste apagado, las comprobaciones de escritura salen igual que antes; encendido, la recarga tras 4 días de las partidas 1 y 3 da 6 diferencias, todas en contadores del comerciante 0 (el juego los deja a 0, p3vita los conserva). `P3_FIX_GAME_BUGS=1` en `write-after-days.sh` lo aplica solo a la recarga de p3vita
- [ ] **Las fiestas de la IA no ocurren nunca** (0x5316E0; seguro por el código, sin comprobar jugando). **No se corrige, ni con el ajuste** (decisión del usuario, 03-11-2026: cambiaría el equilibrio del juego original). La ciudad de la próxima fiesta, [+0x88] del mundo, la deja a 0xFF el constructor del mundo (0x52F520, un inicializador que el juego ejecuta al arrancar). No se guarda en la partida, y ni la carga ni el reinicio de partida (0x530230) la tocan. Solo la cambia la propia fiesta, después de celebrar una, y para eso tiene que ser ya una ciudad. Así que en el juego no hay nunca fiestas de la IA. p3vita y el oráculo empezaban con 0 y sí las celebraban (la primera en la ciudad 0 al poco de cargar): ahora empiezan con 0xFF, como el juego. Lo que hace una fiesta de la IA: solo apunta la fecha en la ciudad ([0x2D0]), y eso sube la satisfacción de sus habitantes +4, +3, +2 y +1 las 4 semanas siguientes; el anfitrión no gasta ni gana reputación (eso es solo en las fiestas del jugador). Podría ser una opción aparte más adelante
- [ ] **El tamaño del `.pst`** tiene un byte de más cuando hay meses. Se reproduce tal cual y sin ajuste: nadie lo lee y arreglarlo solo cambiaría el tamaño del fichero (recomendación de `cloud/statistics`: dejarlo así)
- [ ] **La lista de nodos de cada ciudad** ([0x9B4]) da la vuelta en cada guardado y carga (sin confirmar): el escritor la escribe en orden y el lector pone cada nodo el primero. Si el orden no importa, no hace daño
- [x] **Los textos de las cartas** (03-11-2026, al conectar el buzón). Reproducidos tal cual con el ajuste apagado; los que dicen «Corregido», **corregidos con el ajuste** (`events_rare.cpp`, `events_towns.cpp`; `tick_probe --fix-game-bugs` en los escenarios de la expedición solo cambia el informe: sus comas y el nombre de la ciudad). Solo cambian el texto que lee el jugador, no la partida:
  - **el informe de la expedición pierde su final** (0x4E7306..0x4E745B; seguro y visto en el oráculo). Entre los nombres de 3 o 4 lugares descubiertos escribe «, » sin sumar esos 2 bytes al tamaño de la carta, y la copia de la carta (0x51AB20) se queda con ese tamaño: se pierden los 2 o 4 últimos bytes del texto, con su 0 final («... He returns with 29860\»). El visor tendrá que cerrar el texto. Corregido: se cuentan;
  - **el informe nombra otra ciudad** (0x4E7198; seguro). El nombre de la ciudad a la que vuelve el convoy sale de la tabla 0x6DDA00, que va por número de ciudad, con el índice de la ciudad (el mismo fallo que el del nombre al guardar). Lo mismo en las cartas de las elecciones: la ciudad del alcalde (0x5292A3) y la del Consejo de la Liga y de cada candidato a Gobernador (0x5299F0). Corregido: por el número de la ciudad (tabla 0x6DE4B8);
  - **la carta del encargo del jefe de la Hansa humano** (0x77 del tipo 2, 0x4EA6A8; seguro por el código). La comprobación del objeto pirata está al revés: si existe, pone 0x19 donde iría el apellido de su capitán (0x515AE0), y si no hay ningún objeto pirata llamaría a 0x515AE0 con un puntero nulo y el juego se cerraría. Sin jefe humano (0x4EA949) lo hace bien. Corregido: el apellido del capitán del objeto pirata, y 0x19 si no hay ninguno;
  - **las compras de la expedición** (0x52C5FE; seguro por el código): cuenta barriles o cargas según la última mercancía que miró, no la de cada lado; si las dos son de tipos distintos, la cantidad sale mal. Corregido: por la mercancía de cada lado;
  - **una venta de menos de un barril o una carga** (0x52C58F; sin comprobar): el informe divide por 0 y el juego se cerraría. p3vita se salta esa línea, con el ajuste y sin él;
  - **el informe de la expedición en 4000 bytes** (0x4E70E0; sin comprobar): el búfer es de 0xFA0 bytes y no se mira; con muchas ventas, compras y contratos podría pasarse. p3vita usa cadenas;
  - **los seis búferes del parte de la batalla** (0x60D299) son de ([+0x14] + 8) · 64 bytes; con muchos barcos de nombre largo y capturas, el texto podría pasarse. No se ha visto pasar; p3vita usa cadenas;
  - **el embargo** (0x13, 0x4F6626; seguro por el código): cuando el deudor paga una parte con su dinero, la carta apunta solo sus 16 bits bajos (con 70 000, «4464»). Corregido: todo el dinero;
  - **la noticia de la oficina nueva** (0x0D, 0x530C20; parece un fallo): solo se escribe si el índice de la oficina no es menor que el número de comerciantes, como si fuera un comerciante, así que las oficinas de índice bajo no salen en la crónica. Corregido: sale sea cual sea el índice
- [x] **Los piratas ante una ciudad, en el estado 4** (evento 0x1E, 0x4DFD44; visto con un estado retocado, sin comprobar jugando). Ese estado no mueve el tick del evento. Si cuando le toca sigue «vigente» (el plazo [+0xC] o [+0x8] por delante, y la satisfacción de las tres primeras clases por encima de −30), la cola lo vuelve a atender en el mismo tick sin fin y el juego se cuelga. Al estado 4 se llega cuando deja de estar vigente, pero en los 5 ticks hasta que le toca la satisfacción puede volver a subir. p3vita hace lo mismo con el ajuste apagado. **Corregido con el ajuste**: el evento se quita al acabar el estado 4, que es lo que pasa en el juego cuando ya no está vigente (lo normal), así que no cambia el equilibrio: los piratas solo se llevan las mercancías una vez

- [ ] Tests que comparen con el comportamiento documentado del original

## Fase 5: interfaz

- [x] Archivos por idioma montados como el juego: el español encima del inglés ([formats/fonts.md](formats/fonts.md))
- [x] Texto con las fuentes TrueType del juego (Latin-1, con ñ) y nombres de ciudad en el idioma de los datos
- [x] Barra superior en el visor con ciudad, año y habitantes
- [x] Mercado: existencias, producción y consumo diarios de cada ciudad (de la partida)
- [x] Precios de compra y venta con los umbrales guardados en la partida
- [x] Precios idénticos al juego, comprobados con el oráculo
- [x] Mercado interactivo en la Vita: elegir mercancía y cantidad, comprar y vender con el barco del jugador
- [x] Dificultad de comercio de la partida
- [x] Mapa del mar: mandar el barco del jugador a una ciudad, verlo navegar con su ruta y comerciar al llegar
- [x] Revisado con capturas en el PC lo que faltaba por confirmar en la Vita: los edificios de la partida, la barra de texto (cabe, con tildes), el panel del mercado (cabe; también el táctil), el mapa del mar y la navegación (ruta, llegada prevista, aviso al llegar). Falta verlo en la Vita
- [ ] Rehacer la interfaz para 960×544 (16:9) con soporte táctil
- [ ] Los nombres de las ciudades en el mapa del mar se ven blandos. Se dibujan con stb_truetype (suavizado, sin ajustar las letras a los píxeles) en marrón oscuro y sin contorno sobre un fondo con textura. Comparar con una captura del juego original (letra, tamaño, color, contorno, suavizado) e igualarlo
- [ ] Una franja en los bordes de arriba y abajo del mapa del mar (parte de `Vollansichtskarte.bmp` que el juego seguramente tapa con su interfaz): parar la cámara antes
- [x] El panel de la Hansa para el camino a Gobernador (fase 4, «Llegar a Gobernador»: lo hecho y lo que falta)
- [ ] Los textos de la interfaz del juego. No están en el CPR, que solo tiene los nombres de ciudades, personas y barcos: están dentro de `Patrician3.exe`, en el idioma de cada instalación (el exe español es otro, de 2003). `src/p3data/game_texts.cpp` los lee al arrancar del exe de la instalación (reconoce el inglés de GOG 1.1 y el español) y en p3vita solo guarda dónde está cada texto, nunca el texto (`tests/game_texts_test.cpp`). En la Vita hay que copiar el exe a `ux0:data/p3vita/` (README).
  - [x] el panel de la Hansa: los encargos y su descripción, la respuesta de la Hansa, la elección y la ventana del final;
  - [x] el resto del visor (`cloud/viewer-texts`, [cloud/viewer-texts.md](cloud/viewer-texts.md)): en el mercado, los 20 nombres de mercancía, las cabeceras, las cantidades («%.0f toneladas», «barriles», «piezas»), los tipos de barco y «¡No tienes dinero suficiente!»; en la barra, la fecha con el formato y los meses del juego y «%.0f habitantes»; en el mapa del mar, los tipos de barco, «En el mar», «hundido», «El %s ha llegado a su destino.», los cuatro motivos para no zarpar y la velocidad del juego con sus seis nombres; en el panel de la Hansa, las mercancías, cantidades y fechas. Sin el exe, el visor usa sus textos de antes. Con el exe español los barcos se llaman ahora Goleta, Carabela, Nao y Galeón, como en el juego. Compila para la Vita; falta probarlo en una;
  - [ ] **Elegir el idioma en las Opciones** (idea del usuario, 02-11-2026). Por omisión, los textos del juego en español. Como tenemos los ficheros originales de cada idioma (el exe y el CPR inglés y español, y los que haya), el visor podría dejar elegir el idioma: leer los textos del exe de ese idioma (`game_texts.cpp` ya reconoce el inglés y el español) y montar su CPR. En la Vita harían falta los ficheros de cada idioma en `ux0:data/p3vita/`. Ojo: las frases no se corresponden una a una entre idiomas (la pregunta de la batalla es «Navigate manually» en inglés y «¿Quieres dirigir manualmente a los barcos en la batalla naval?» en español) y el español usa argumentos con posición (`%$3$s`);
  - [ ] las frases que el juego no tiene (las pistas de botones, «Mapa del mar», los mensajes de compraventa, la actividad del barco) siguen en español: con el exe inglés sale «Compra: 5 load de fish por 2.984 monedas». Recomendación de la sesión: dejarlas en español; si importa la versión inglesa, una tabla pequeña de p3vita con las dos lenguas, elegida según el exe.
- [ ] **El nombre del primer barco del jugador en la partida 1**: desde el cambio «ship names» del cargador (91b958b, `cloud/savegames`) sale «Constance» en vez de «Ermanrich», con o sin exe. Comprobarlo contra el juego (lo vio `cloud/viewer-texts`)
- [ ] Las acciones del jugador: la interfaz encola el resto de los 193 códigos de operación (comprar, vender, construir, contratar...)
- [ ] Los guiones de los textos y las misiones. 0x511270 interpreta los textos del juego y, con el comando 0xFD, programa eventos 0x1B con un guion que ejecuta 0x4ECF64 (el intérprete). Se arranca desde la interfaz (la operación 0xC0 y otros dos caminos); en las partidas, los 0x1B llegan sin guion. El oráculo salta 0x511270 al cargar

## Fase 6: resto del juego

- [ ] Las batallas navales (fase 4, «Piratas, persecuciones, encuentros y batallas»)
- [ ] Las rutas comerciales automáticas, del jugador y de la IA (0x4D4DD0; tampoco se reproduce al borrar un barco, operación 0x0A)
- [ ] Misiones, cartas y eventos (las cartas, con el buzón de la fase 4)
- [x] Guardar partidas como el juego ([cloud/savegames.md](cloud/savegames.md); `src/p3data/savegame_write.cpp`, `lz_compress.cpp`; `p3save write` y `p3save compress`; `--write` en `town_oracle.py` y `tick_probe`):
  - el `.pat` en la versión 0x85, la misma que escribe el juego, comprimido con el compresor del juego reescrito (0x54C410); el `.pst`, con las estadísticas vacías;
  - comprobado contra el escritor del juego (0x549CC0), byte a byte: 0 bytes distintos en 11 de las 12 partidas. En la 7 quedan 12 bytes de relleno de las batallas, que el juego escribe sin inicializar (ver los fallos del juego);
  - lo mismo después de jugar 4 días;
  - ida y vuelta: el lector del juego y el de p3vita cargan lo que escribe p3vita, con 0 diferencias;
  - el compresor da los mismos bytes que el del juego en las 12 partidas;
  - estados retocados para lo que las partidas no tienen: listas de asedio, el mapa de un pirata, encargos, eventos 0x34, rutas comerciales y buzones;
  - de paso, dos fallos de p3vita al leer, arreglados: saltaba 16 bytes por entrada en los eventos 0x1B en vez de 4, y dejaba vacíos los nombres de los jugadores en 10 de las 12 partidas.

  Falta:
  - [x] las estadísticas de verdad del `.pst` (versión 0xB): los mismos bytes que el escritor del juego en las partidas 1, 3 y 7, también tras pasar al 30 de diciembre (semanas, meses y un año), y el juego carga el `.pat` y el `.pst` de p3vita igual que p3vita;
  - [ ] los nombres de barco que añada el jugador durante la partida;
  - [ ] el objeto opcional del mundo (0x533DA0): ninguna partida lo tiene y p3vita lo rechaza al leer. Reescribirlo solo si aparece una partida que lo tenga
- [x] Las estadísticas diarias (0x54DE90, `src/p3data/statistics.cpp`), con 0x54E170 y 0x54E1F0 (las de un comerciante) y 0x54DDF0. No son solo para las gráficas: cada día ponen a cero contadores de las ciudades y de los comerciantes. Encendidas por omisión en p3vita (`SaveGame::record_statistics`) y en el oráculo (regiones S0..S4; `--no-statistics` las apaga): el mundo entero 7 días en las 12 partidas, 0 diferencias. Hacen el oráculo un 25–37 % más lento. Se guardan en el `.pst`
  - [x] las dos llamadas de la quiebra de un comerciante (operación 0x65, `fn_4f5d80` en `merchants.cpp`): 0x54E1F0 en 0x4F5DA5 y 0x54E170 en 0x4F5F8E, puestas al juntar `cloud/unchecked-branches` (ca3adbd). Las operaciones 0xA7 (0x541D70) y 0xB3 (0x542A30) también llaman a 0x54E170, pero p3vita aún no las tiene
- [ ] Vídeos Bink (FFmpeg) y audio MP3

## Herramientas y comprobaciones

- [x] Oráculo unas 4 veces más rápido (`cloud/oracle-speed`, [cloud/oracle-speed.md](cloud/oracle-speed.md)):
  - `regions()` lee cada bloque una vez, y `write_diff` compara por bloques de 4 KB;
  - el Recorder se salta la foto de antes de una llamada cuando nada puede haber cambiado (`P3_ORACLE_CHECK=1` la hace igualmente y la compara);
  - tick_probe compara con `memcmp`;
  - el PC se compila con `-O2 -fwrapv -fno-strict-aliasing` por omisión.

  `world.sh 7` en las 12 partidas: 98 s (antes, 392 s), con las grabaciones idénticas byte a byte
- [x] Cambios en el oráculo al juntar `cloud/battles-2` (01-11-2026):
  - `HeapReAlloc` copia solo los bytes pedidos del bloque viejo y deja el resto a cero, como `alloc`. Antes copiaba hasta el tamaño redondeado y arrastraba bytes viejos (una tabla de páginas de una batalla recibía así un puntero a otro bloque). El mundo por omisión, igual;
  - lee las batallas de la partida al cargar (0x611AE0). Con eso, dos bytes del objeto de las batallas ([0x6E59CC], [0x6E59CD]) quedan como en el juego, y la ventana de avisos al jugador (*0x6CC7E8, 0x469380) intentaba abrirse en el acto y se caía en el emulador. Ahora el aviso siempre queda pendiente (`WINDOW_JUMPS`, 0x46938F → 0x4693A1), como antes. Lo encontró el escenario `road_expired` al juntar, no las partidas;
  - las tablas de bits de cada batalla pueden valer 0x10000000, que parece un puntero al montón: el oráculo, p3vita y tick_probe las tratan como números;
  - la ventana de noticias al acabar una batalla se salta (0x60EB55 → 0x60EBAC).
- [x] Cambios en el oráculo para la batalla del jugador (03-11-2026):
  - ejecuta la operación 0x96 con los piratas (`PIRATE_OPERATIONS`);
  - salta la ventana de la batalla, que el emulador no tiene, igual que p3vita (`BATTLE_WINDOW_JUMPS`): en la preparación, el camino del jugador ocupado con otra batalla (0x60659B → 0x606BAE); en el paso y en el abordaje, la ventana y lo que escribe con ella;
  - 0x50D0C0 con un barco que no existe vuelve en seguida (el convoy pirata sin guía, arriba);
  - los arrays de números de la escena de una batalla ([+0x18], [+0x1C], [+0x24] y las cajas [+0x28]..[+0x40]) se tratan como números, igual que las tablas de bits: dos coordenadas de 16 bits pueden parecer un puntero al montón, y el oráculo convertía una (partida 5, `event_attack`). También en p3vita (`BattleGraph::bit_pages`) y tick_probe;
  - tick_probe lee los textos del `Patrician3.exe` que está junto al CPR (`--exe` para otro), para los avisos de las batallas.
- [x] **Los inicializadores del arranque del juego** (03-11-2026). El juego ejecuta al arrancar unos 550 inicializadores (las tablas de `_initterm`, 0x692000..0x69288C y 0x692890..0x6928A4), y el oráculo no ejecuta ninguno (solo los 4 repartidores de las `CString`). Comprobado ejecutándolos todos en el emulador antes de cargar: la simulación solo lee dos valores distintos de los que ponen:
  - el estado de la red de la sesión ([0x6DF2F0 + 0x2C], 0x6DF31C: 0xFFFFFFFF al arrancar, 0 cuando corre una partida; si no fuera 0, las ciudades no construirían ni la IA daría órdenes), que en el oráculo y en p3vita ya es 0;
  - la ciudad de la próxima fiesta de la IA ([+0x88] del mundo, 0xFF): ahora el oráculo y p3vita la ponen como el juego (los fallos del juego, arriba).

  Con eso, el oráculo con todos los inicializadores y la sesión empezada da el mundo entero, 7 días en las 12 partidas, 0 diferencias con p3vita. Los scripts, en el scratchpad de la sesión (`init_oracle.py`, `world_init.sh`). Una tabla de textos de la batalla que rellena un inicializador (0x46BFC0) no llega a leerla la simulación (ver la pieza 3 de las batallas)
- [x] Los escenarios de `retouch.py` en la partida 7 daban diferencias en las batallas al comparar llamada a llamada (`ret2f.sh`). La batalla guardada tiene dos arrays sin elementos, [+0x664] y [+0x668] (el segundo, el de las balas de cañón), que son bloques de 0 bytes. El oráculo no convertía un puntero a un bloque vacío (`lo <= v < hi` con `lo == hi`), así que se quedaba como dirección en bruto, y p3vita, al rehacer el estado del juego, no llegaba a esos bloques. Venía de antes (igual con el oráculo de `0196f45`). En el mundo entero no se notaba porque allí p3vita usa sus propios punteros y la comparación se salta las direcciones en bruto. **Arreglado** (03-11-2026): el oráculo convierte también el puntero al principio de un bloque de 0 bytes. Comprobado: los escenarios de fiestas en las partidas 1, 3, 5 y 7, 0 diferencias; el mundo entero 7 días en las 12 partidas, 0 (ahora se comparan también esos dos campos); `event_attack` y los ataques de `retouch_pirates.py`, 0; la escritura de partidas tras 4 días (1, 3, 7), igual
- [x] Medir el estado de cada parte: `re/emu/sesion5/coverage_parts.py` (la tabla de «Estado por partes», arriba). A diferencia de `callgraph.py`, sigue el cuerpo real de cada función, y además poda los gráficos del asedio (0x62BE90, 0x62C730...) y la ventana del final (0x4220B0). Con `coverage_parts.py RUTA PARTE` lista las entradas más grandes a lo que falta. Pendiente: que siga también las tablas de métodos, como el grafo de las batallas (`re/battles/graph.py` de p3vita-datos)
- [x] **El punto ciego de tick_probe** (`cloud/unchecked-branches`, [cloud/unchecked-branches.md](cloud/unchecked-branches.md)). Antes no comparaba, en ninguna región, las palabras de 4 bytes que parecen punteros (la pila del oráculo 0x01000000..0x010FFFFF, el fichero 0x04000000.. y el montón 0x10000000..0x17FFFFFF). Eso escondía mucho más que los eventos: justo después de cargar las partidas 3 y tut4, miles de palabras de las rejillas (0x10101010...), cientos de registros de edificios (con el tipo 0x10..0x17 en el byte alto), unos 100 campos de las ciudades, campos de oficinas y comerciantes (nombres, rangos), la plantilla de las empresas, los eventos (`01 comerciante 00 01` de un encargo de calle empezado) y las rutas.
  - Ahora (`pointer_field` en `tools/oracle/tick_probe.cpp`) solo se saltan los campos que son punteros de verdad: los registros, la rejilla y los nodos de las ciudades, los nombres de los comerciantes, la ruta de los barcos, el ejército y las listas de los asedios, la dirección de cada nodo, el objeto de cada pirata, y los búferes que el lector deja a 0 en las cartas y en los eventos, según el tipo. Todo lo demás se compara.
  - Comprobado: el mundo entero 7 días en las 12 partidas sigue a 0 diferencias (no escondía ninguna); todos los escenarios de `unchecked.sh` y los de `alderman.cpp` dan las mismas llamadas iguales, y `--fix-game-bugs` las mismas diferencias. Lo de [+0xD] de los encargos empezados en `alderman.cpp` estaba bien. `road_expired` ya no necesita el retoque [+0xE] = 0x20.
  - [ ] Falta: en los globales (G) y los ejércitos (J) sigue la regla antigua, porque no se conocen todos sus punteros; allí un valor que parezca puntero se sigue saltando. Para quitarla haría falta la lista de los campos de puntero de las dos regiones
- [ ] Arreglar el modo «solo ciudades» de `town_oracle.py`. Sin `--barcos` (como lo lanza `scripts/tick-probe.sh` sin `P3_SHIPS`) se para en la primera foto con `UC_ERR_READ_UNMAPPED` en las rutas (región Q). Arreglo: grabar Q solo con `--barcos`, o limpiar el [+0x24] de los barcos al cargar sin navegación
- [ ] Más velocidad para el oráculo (estimaciones en [cloud/oracle-speed.md](cloud/oracle-speed.md)):
  - `regions()` en C: un 25–30 % menos de oráculo;
  - lanzar tick_probe a la vez que el oráculo por una tubería en `world.sh`: un 15 % menos en el PC;
  - saltarse la foto de después en las llamadas que no escriben nada;
  - comparar en tick_probe solo las regiones que cambian.
- [ ] Pasar al inglés lo que aún está en español en el repositorio, menos este documento: los docs, las opciones y mensajes de las herramientas, los nombres de los escenarios, los scripts de `re/` y el HANDOFF. Sin tarea aparte: poco a poco, al tocar cada fichero por otra razón (la tarea `tasks/english.md` de p3vita-datos queda descartada)
