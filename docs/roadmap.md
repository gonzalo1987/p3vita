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

## En marcha (31-10-2026)

- Sesión local: la parte B del inventario está hecha salvo la 0x9F y la 0xB5, que van con las batallas. Lo siguiente es la parte C y después la D.
- No se lanzan más sesiones en la nube: queda poco crédito (66 de 250 el 31-10-2026). Lo que tenga cada sesión al acabar se junta como siempre.
- Batallas: la pieza 2 está en `main`. Lo siguiente es `tasks/battles-2.md` (el «pool» de los piratas, deshacer el convoy en la guarida y la infraestructura de la pieza 3); todavía no está lanzada.

## Estado por partes (medido)

Cuánto código del juego tiene p3vita en cada parte de la simulación, medido el 31-10-2026 sobre `main` (con `cloud/savegames` ya junta) con `re/emu/sesion5/coverage_parts.py` (Python de Windows; para repetirlo: `python coverage_parts.py RUTA/p3vita/src`).

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
| Barcos: navegación, IA y convoyes (0x506720) | 152 | 62 | 32 (52 %) | 24 (39 %) | 5 (8 %) |
| Lo diario, tareas programadas y objetos del mundo | 12 | 2 | 2 (97 %) | 0 (3 %) | 0 (0 %) |
| Asedios (0x6349F0, 0x633A90) | 28 | 15 | 13 (89 %) | 2 (11 %) | 0 (0 %) |
| Cola de eventos (0x4D85C0) | 415 | 193 | 153 (79 %) | 26 (14 %) | 14 (7 %) |
| Operaciones que encola la simulación | 172 | 72 | 53 (74 %) | 15 (21 %) | 3 (5 %) |
| Camino a Gobernador | 223 | 93 | 70 (75 %) | 21 (22 %) | 2 (2 %) |
| Cargar partida (0x549F40) | 103 | 42 | 5 (13 %) | 34 (81 %) | 3 (6 %) |
| Guardar partida (0x549CC0) | 38 | 11 | 0 (0 %) | 10 (96 %) | 0 (4 %) |
| Tormentas (0x5053C0) | 4 | 5 | 5 (97 %) | 0 (3 %) | 0 (0 %) |
| Guiones de misiones (0x4ECF64, 0x511270) | 73 | 23 | 3 (14 %) | 7 (32 %) | 12 (54 %) |
| **Toda la simulación** (advance_time y sus operaciones) | 597 | 287 | 221 (77 %) | 45 (16 %) | 20 (7 %) |

Notas:
- **Barcos**: los piratas que se mueven ya están (pieza 2 de las batallas, detrás de `--pirates`). Lo que falta son las rutas comerciales (0x4D5200, 0x52ACD0, 0x4D4C90...), la actualización de los objetos piratas (0x514000 → 0x513EC0) y la guarida (0x515DA0 → 0x513BF0).
- **Cola de eventos**: lo que falta son los tipos que p3vita aún no atiende (0x4E5664, 0x4E38B4, 0x4DDF40, 0x4E7A14...).
- **Cargar y guardar partida**: aunque en la tabla salgan como «nombrado», están hechos e idénticos al juego en las 12 partidas. `savegame.cpp` y `savegame_write.cpp` los reescriben por estructuras y no con funciones `fn_`, y algunas direcciones (0x504230, los barcos) ni siquiera salen en los comentarios, así que la medida se queda corta.
- **Batallas**: no van en la tabla, porque usan sobre todo llamadas por tablas de métodos, que este grafo no sigue. Su estudio, que sí las sigue ([cloud/battles.md](cloud/battles.md)), cuenta sin la interfaz 249 funciones (148 KB) en el mar y 41 (50 KB) en las ciudades. De eso solo están hechas las tormentas.
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
2. [ ] Los piratas que se mueven, sin batallas. Hecho en su mayor parte, detrás de `Sailing::set_pirates`, `town_oracle.py --pirates` y `tick_probe --pirates`, que de momento van apagados por omisión ([cloud/battles.md](cloud/battles.md) §7; `src/p3data/battle/pirates.cpp`):
   - [x] el estado 0x12 del convoy pirata (0x507A65), con el nuevo rumbo (0x505C90), la elección del blanco (0x50D9E0), la decisión de atacar (0x515360), la velocidad de batalla (0x612930), los bajos (0x50E340) y 0x5159C0;
   - [x] los puntos de cada ciudad entre los que eligen adónde ir (0x448780, al cargar). El oráculo se lo saltaba; con `--pirates` lo ejecuta (2,8 s más por carga) y p3vita calcula lo mismo;
   - comprobado: 0 diferencias llamada a llamada en las 12 partidas durante 7 días (9455 llamadas a 0x505C90, 8064 a 0x50D9E0, 5965 a 0x515360 y 3265 a 0x612930), y el mundo entero con los piratas encendidos, 0 diferencias en las 12 partidas;
   - regla provisional, igual en p3vita y en el oráculo, mientras no haya batallas: un pirata que iría a atacar (0x507BE9 → 0x50BC40) sigue como si no tuviera blanco; un convoy que acaba su ruta en una ciudad (0x507C0B) sigue como si navegara; y uno que llega a su guarida no se deshace (0x505FF1 → 0x506029). `--pirates` se queda apagado hasta que la pieza 3 quite el primer salto;
   - [ ] deshacer el convoy en la guarida (0x514B80). Mueve los barcos al «pool» de cada pirata, y 0x515DA0 planifica en él los edificios de la guarida; ese «pool» todavía no lo guardan ni `SaveGame` ni las regiones del oráculo. Hacerlo antes de la pieza 3;
   - [ ] estados retocados para las ramas que las partidas no recorren;
   - [ ] los estados 0x11 y 0x12 en el mar (0x507022 y 0x507099), 0x517170, la rama pirata de 0x5017F0, el ataque del evento 0x1E (0x5063C0) y la actualización de los objetos piratas (0x514000), con la operación 0xB5: lo que de esto no haya quedado ya cubierto.
3. [ ] La batalla automática en el mar. Ya se sabe cómo es en memoria una batalla viva: el objeto de 0x1E98 bytes, una copia de 0x180 bytes de cada barco que lucha, varias listas y una docena de bloques de la escena ([cloud/battles.md](cloud/battles.md) §8). Antes de reescribirla hay que:
   - hacer que el oráculo grabe todo eso como regiones, cambiando los punteros por índices;
   - crear un `SaveGame::battles` que tick_probe lea y compare.

   Después se reescriben unas 15 000 instrucciones, en el orden de §8:
   - empezar y acabar: 0x50BC40, 0x605420, 0x50BFD0, 0x6079D0... (unas 7500 instrucciones);
   - cada paso, la operación 0x94 con el método +0x14: 0x606CC0, 0x608F70, 0x6197F0... (unas 7000);
   - un `SaveGame::battles` como memoria del juego, y las regiones de las batallas en el oráculo;
   - la operación 0x9F.
4. [ ] Las otras batallas en el mar: la persecución que alcanza al barco (0x50B250, también la operación 0x99) y la flota de la Hansa en una ciudad protegida (0x50BA80).
5. [ ] Las batallas en la partida guardada: 0x611AE0 (leer), 0x611950 (escribir) y los métodos +0x10 (0x604600), +0x18 (0x608230) y +0xC (0x604220). Ahora p3vita guarda las batallas tal como las lee. **La partida 7 tiene una batalla en el mar**, con 3 barcos, y una tabla de 3 huecos y 4 entradas: es la única de las 12 con algo que cargar (lo encontró `cloud/statistics`; las notas de las batallas decían que ninguna tenía). Con el ajuste, el relleno sin inicializar ya se escribe a cero (fallos del juego, abajo).
6. [ ] La batalla en una ciudad (los piratas contra una ciudad y la guarida pirata):
   - 41 funciones (0x5FBFE0, 0x621770, 0x5FE7E0, 0x624950...);
   - las operaciones que encola: 0x92, 0x95–0x98, 0x9A, 0x9B, 0xA0 y 0xB4;
   - la geometría de la vista de la ciudad (0x58D030, 0x58B740), que el emulador no tiene;
   - lo que cuelga de ella: la guarida 0x515C00, los edificios quemados de los eventos 0x25/0x26 y el ataque del encargo de la Hansa de tipo 3.
7. [ ] La batalla que dirige el jugador: la ventana (0x41D9B0), el método +0x10 y 0x61A880 con sus controles, y las órdenes (operaciones 0x92, 0x93, 0x98 y 0xA0 desde la interfaz). Va con la interfaz.

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
- [x] **La IA de los comerciantes con deudas** (0x4F5A20, al elegir qué edificio poner en venta, operación 0x28). La renta del mejor ([esp+0x2C]) no se inicializa, así que lo que hace depende de lo que haya quedado en la pila de la llamada anterior. En el oráculo suele ser 0 y nunca pone nada en venta; tras un impuesto extraordinario (evento 0x31) en el mismo tick, sí. p3vita usa 0. **Corregido con el ajuste**: el primer edificio con unidades pasa a ser el mejor, y los demás se comparan por renta por unidad. Con el ajuste encendido, en la partida tut0 un comerciante de la IA con deudas pone en venta un edificio que el original no ponía (operación 0x28 antes del préstamo 0x29) (`events.cpp`)
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
- [ ] **La batalla en una ciudad lee la vista de la ciudad** (*0x6E51AC, 0x58D030; sin confirmar). Toma su geometría de la ciudad que mira el jugador, no necesariamente de la atacada. Falta ver qué hace el juego cuando no coinciden
- [x] **0x62B2B0 no mira cuántos objetivos caben** (seguro por el código, 0x62B2F8..0x62B3B7). Reserva [+0x250] para [+0x242] entradas y luego escribe una por cada torre 0x32/0x33 cerca de la puerta, sin mirar ese tamaño: con más, escribe fuera de su bloque. Solo lo llama el cargador, y en las partidas [+0x242] siempre es 0. p3vita agranda el vector, que ya es la corrección (`sieges.cpp`)
- [x] **El nombre de la ciudad natal al guardar** (0x549D7D). El escritor copia en los ajustes de la partida ([+0x27]) el nombre de la tabla 0x6DDA00, que va por número de ciudad (0..39), pero usa el índice en la partida de la ciudad natal del jugador. En `1.pat` escribe «Cologne» (número 9) por Lübeck (índice 9, número 14). Seguramente es el nombre que sale en el menú de cargar; no comprobado. Reproducido en `update_settings`. **Corregido con el ajuste**: el nombre se toma por el número de la ciudad (tabla 0x6DE4B8): «Luebeck» en la partida 1, «Rostock» en la 3, «Gdansk» en la 7, la ciudad natal del jugador en las 12
- [x] **El relleno de las batallas** (0x611950). Cada entrada de la tabla del final de las batallas ocupa 8 bytes en el fichero; el lector solo guarda 5 (0x612A20 lee dos u32 y se queda con el byte bajo del segundo), y el escritor escribe 8, así que 3 bytes salen de lo que hubiera en la memoria (0xCD en la partida 7, 0 en el emulador). **Corregido con el ajuste**: se escriben a cero, recorriendo la sección como la escribe el juego (0x604220); si el recorrido no cuadra, no se toca nada. En la partida 7, los 12 bytes coinciden ya con los del emulador
- [x] **Los nodos del evento 0x34 cambian en cada carga y guardado** (0x4F3A40/0x4F39E0). El lector rellena dos veces el u16 [+0x6] con el tercer byte del nodo y se salta el cuarto; el escritor escribe el signo del byte bajo de [+0x6] y ese byte. Un nodo `b0 b1 b2 b3` se guarda como `b0 b1 signo(b2) b2`, y la lista siempre acaba en 0. Reproducido en `write_event_nodes`, igual que el juego. **Corregido con el ajuste**: cada nodo se guarda tal como se leyó (por el código, lector y escritor querían un u16 en big-endian; parece fallo, no es seguro)
- [ ] **El tamaño del `.pst`** tiene un byte de más cuando hay meses, y **leer un `.pst` pone a cero los contadores del comerciante 0** (`cloud/statistics`, §2). Los dos se reproducen tal cual. El de la lectura es el único que se nota al jugar: merece una corrección detrás del ajuste (`savegame_load.cpp`)
- [ ] **La lista de nodos de cada ciudad** ([0x9B4]) da la vuelta en cada guardado y carga (sin confirmar): el escritor la escribe en orden y el lector pone cada nodo el primero. Si el orden no importa, no hace daño

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
  - [ ] el resto del visor (mercado, barra, mapa del mar), que sigue con textos propios en español; con el exe inglés el panel mezcla idiomas. Hacerlo igual, buscando cada texto en las dos versiones.
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
  - [ ] las dos llamadas de la quiebra de un comerciante (operación 0x65, `fn_4f5d80` en `merchants.cpp`): 0x54E1F0 en 0x4F5DA5 y 0x54E170 en 0x4F5F8E. Las funciones ya existen (`remove_merchant_statistics`, `add_merchant_statistics`); faltan las dos líneas, al juntar `cloud/unchecked-branches`, que es la dueña del fichero. Las operaciones 0xA7 (0x541D70) y 0xB3 (0x542A30) también llaman a 0x54E170, pero p3vita aún no las tiene
- [ ] Vídeos Bink (FFmpeg) y audio MP3

## Herramientas y comprobaciones

- [x] Oráculo unas 4 veces más rápido (`cloud/oracle-speed`, [cloud/oracle-speed.md](cloud/oracle-speed.md)):
  - `regions()` lee cada bloque una vez, y `write_diff` compara por bloques de 4 KB;
  - el Recorder se salta la foto de antes de una llamada cuando nada puede haber cambiado (`P3_ORACLE_CHECK=1` la hace igualmente y la compara);
  - tick_probe compara con `memcmp`;
  - el PC se compila con `-O2 -fwrapv -fno-strict-aliasing` por omisión.

  `world.sh 7` en las 12 partidas: 98 s (antes, 392 s), con las grabaciones idénticas byte a byte
- [x] Medir el estado de cada parte: `re/emu/sesion5/coverage_parts.py` (la tabla de «Estado por partes», arriba). A diferencia de `callgraph.py`, sigue el cuerpo real de cada función, y además poda los gráficos del asedio (0x62BE90, 0x62C730...) y la ventana del final (0x4220B0). Con `coverage_parts.py RUTA PARTE` lista las entradas más grandes a lo que falta. Pendiente: que siga también las tablas de métodos, como el grafo de las batallas (`re/battles/graph.py` de p3vita-datos)
- [ ] **El punto ciego de tick_probe**: nunca compara las palabras de 4 bytes que parecen punteros (0x01000000..0x010FFFFF, 0x04000000.., 0x10000000..0x17FFFFFF). En un evento 0x32 empezado, [+0xC..+0xF] vale `01 comerciante 00 01`, así que un comerciante equivocado en [+0xD] no se veía (lo encontró `cloud/unchecked-branches`). Arreglo: no tomar por punteros las palabras de las regiones que no tienen punteros (E, B, W...). Después, repasar `alderman.cpp` ([+0xD] de un encargo empezado con [+0xE] < 0x10)
- [ ] Arreglar el modo «solo ciudades» de `town_oracle.py`. Sin `--barcos` (como lo lanza `scripts/tick-probe.sh` sin `P3_SHIPS`) se para en la primera foto con `UC_ERR_READ_UNMAPPED` en las rutas (región Q). Arreglo: grabar Q solo con `--barcos`, o limpiar el [+0x24] de los barcos al cargar sin navegación
- [ ] Más velocidad para el oráculo (estimaciones en [cloud/oracle-speed.md](cloud/oracle-speed.md)):
  - `regions()` en C: un 25–30 % menos de oráculo;
  - lanzar tick_probe a la vez que el oráculo por una tubería en `world.sh`: un 15 % menos en el PC;
  - saltarse la foto de después en las llamadas que no escriben nada;
  - comparar en tick_probe solo las regiones que cambian.
- [ ] Pasar al inglés lo que aún está en español en el repositorio, menos este documento: los docs, las opciones y mensajes de las herramientas, los nombres de los escenarios, los scripts de `re/` y el HANDOFF. La tarea es `tasks/english.md` de p3vita-datos, cuando estén juntas todas las ramas
