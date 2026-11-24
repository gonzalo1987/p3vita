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
  [formats/mail.md](formats/mail.md) (las cartas), [formats/missions.md](formats/missions.md) (las misiones),
  [formats/sound.md](formats/sound.md) (el sonido) y [cloud/](cloud/) (las notas de las sesiones en la nube, ya
  terminadas). Las notas y los scripts de cada sesión, fuera del repositorio, en `re/emu/sesionN/` (con su README).
- `[ ]` pendiente; `[x]` hecho y comprobado. Este documento se queda en español: lo lee el usuario.

## Dónde estamos (24-11-2026)

- **El proyecto entero**: alrededor del 70 % (una estimación por bloques: abajo, «Cuánto falta»). El motor está casi
  acabado; lo que más falta es la partida en red, el sonido y los vídeos, probar en la Vita y el final de la ciudad
  viva.
- **El motor**: todo lo que hace una partida por sí sola está reescrito e idéntico al juego: las ciudades, los
  comerciantes, los barcos y los convoyes de la IA, la cola de eventos, los asedios, las tormentas, los piratas, todas
  las batallas (en el mar y en una ciudad, automáticas y dirigidas por el jugador), el camino a Gobernador, el buzón,
  las estadísticas, cargar y guardar partidas, la partida nueva, las rutas comerciales automáticas, las misiones (el
  intérprete entero), las campañas y el tutorial; y todas las acciones del jugador. Comprobado: el mundo entero 60 días
  en las 12 partidas de prueba sin un byte distinto (con las misiones, 30 días llamada a llamada y 60 la cola de
  eventos y las operaciones), y más de cien escenarios con estados retocados para las ramas a las que las partidas no
  llegan. Queda lo del apartado 2 (casi todo cuenta para jugar en red) y la sala de la partida en red (apartado 8).
- **El visor** (lo que se juega en la Vita): la pantalla de inicio, la partida nueva, las campañas y el tutorial,
  guardar y cargar, las opciones; la ciudad con el panel lateral del juego entero; el mapa del mar; los 13 edificios con
  vista propia, con todas sus páginas como las del juego; las batallas que dirige el jugador. La ciudad viva, partes 1
  a 8 de 11 (apartado 4), y el sonido, partes 1 a 4 (apartado 5).
- **En curso, en paralelo** (cada uno con su encargo en la carpeta del proyecto, fuera del repositorio):
  - La ciudad viva, parte 9, el asedio (apartado 4): casi hecha y **sin commit**, en
    `re/emu/sesion50/wip/living_part9a.patch` (contra ec0d456); cómo seguir, en `ENCARGO_CIUDAD_VIVA.md`.
  - El sonido (apartado 5): el mar y las batallas, los vídeos y la voz del tutorial, en `ENCARGO_SONIDO.md`.
- **Lo último en `main`** (19..22-11-2026, hasta ec0d456): las misiones con las campañas y el tutorial, la
  carta final de la caza de piratas de la dieta (270ecbe), el sonido, partes 1 a 4 (39aae4e), y la ciudad viva, partes
  1 a 8 (de b54f737 a ec0d456).
- **Siguiente**, en el orden que eligió el usuario el 17-11-2026 (abajo, «Decisiones del usuario»): hechos los restos de
  las pantallas y las misiones, toca **el final de la partida** (apartado 1); siguen en paralelo la ciudad viva y el
  sonido; luego los fallos del juego con su pantalla de ajustes (apartado 7) y la partida en red (apartado 8).
- **En la Vita**: lo último que el usuario probó en la consola fueron las cargas más ligeras (04-11-2026). Todo lo de
  después está probado en el PC sin pantalla, con capturas, y compila para la Vita, pero no consta probado en ella
  (apartado 6).

## Cuánto falta (24-11-2026)

**El proyecto entero: alrededor del 70 %** (estimación del 24-11-2026, a petición del usuario). Se calcula por bloques:
cada uno con un peso, lo que se estima que es del trabajo total, y su avance; el total es la suma de peso × avance. Los
pesos son una estimación (el más dudoso, la red: su protocolo está sin leer) y se pueden ajustar; los avances salen de
la tabla de abajo.

| Bloque | Peso | Avance | Aporta | Por qué ese avance |
|-|-|-|-|-|
| El motor (la simulación, las acciones del jugador, las misiones, las batallas, cargar y guardar, la partida nueva) | 40 % | 99 % | 39,6 | solo queda el apartado 2 |
| Las pantallas (las ventanas del juego, el panel, la ciudad viva, el mapa del mar) | 25 % | 90 % | 22,5 | hechas todas las ventanas menos la del final; la ciudad viva, 8 de 11 partes con la 9 casi hecha; quedan el coste en la Vita, el mapa del mar y detalles (apartados 1 y 4) |
| Sonido, música y vídeos | 8 % | 60 % | 4,8 | 4 de 6 partes; las que quedan (el mar, las batallas, el asedio y los vídeos, con el descodificador de la introducción) son de las grandes |
| Probar en la Vita y arreglar lo que salga | 7 % | 15 % | 1,05 | todo compila para la Vita, pero no se prueba en ella desde el 04-11-2026 |
| Los fallos del juego con su pantalla de ajustes | 5 % | 50 % | 2,5 | el ajuste y muchas correcciones hechas; quedan por decidir los reproducidos y la pantalla de cada corrección |
| Jugar en red con el juego de PC | 15 % | 5 % | 0,75 | solo la base (el motor idéntico) y unas operaciones de la red |
| **Total** | 100 % | | **≈ 71** | |

No cuenta la pasada de usabilidad («UX 2.0», apartado 4), que el usuario dejó para más adelante.

**Lo grande que falta**, en el orden del usuario: el final de la partida (la ventana de la clasificación al llegar a
Gobernador y su vídeo, apartado 1); la ciudad viva (el asedio, casi hecho; el coste en la Vita; el mapa del mar,
apartado 4); el sonido (el mar, las batallas y el asedio; los vídeos; la voz del tutorial, apartado 5); probar todo en
la Vita (apartado 6); los fallos del juego con su pantalla de ajustes (apartado 7); y la partida en red, entera
(apartado 8).

Cada parte con su propia cuenta (las cifras, contadas en las fuentes: las operaciones en `run_operation`,
`src/p3data/simulation.cpp`; las cartas en `MAIL_CALLS`, `tools/oracle/town_oracle.py`; los eventos en los `case` de
`events*.cpp`):

| Parte | Avance | Hecho | Lo que falta |
|-|-|-|-|
| La simulación sola (sin el jugador) | 99 % de su código | todas sus partes, idénticas al juego (abajo, «La simulación por partes») | 3 manejadores de eventos (el 0x14 de la red; el 0x28 y el 0x29, que nadie crea); los estados raros del barco y unas ramas sueltas (apartado 2) |
| Las acciones del jugador | las 106 operaciones del jugador | todas: los edificios, el buzón, el barco, las ofertas, la taberna, el ayuntamiento y el asedio, en el motor y en el visor | tres detalles (apartado 3) |
| El final de la partida (llegar a Gobernador) | el motor, entero; la pantalla, por hacer | el camino a Gobernador en el motor (la candidatura, los encargos, las elecciones, la reunión de la Hansa) y el objetivo de la partida | la ventana de la clasificación como la del juego y el vídeo de Gobernador (apartado 1) |
| Las misiones | enteras, salvo el sonido | la base, las 138 instrucciones del intérprete, lo que encola y lo que crea, las operaciones del jugador sobre sus cartas, el visor, las campañas y el tutorial (19..21-11-2026) | la voz del tutorial y las ventanas con vídeo (apartado 5) |
| Tipos de evento | 54 de 58 | todos los que se dan en una partida de un jugador | 4: el 0x14 (la sala de la partida en red) y 3 que nadie crea (0x0B, 0x28 y 0x29) |
| Operaciones | 174 de 193 | las 37 de la simulación, las 106 del jugador, 12 de las 14 de las batallas, las 8 de la partida nueva que se usan, 9 de las 15 de la red (la 0xA8, con las campañas, 21-11-2026), la 0x8F (el impuesto extraordinario del asedio, sin quien la encole) y la 0xC0 (el arranque de las misiones) | 6 de la sala de la partida en red (0xA2..0xA6 y 0xA9; y la 0xA8 en la sala; apartado 8); la 0xBD (abre la ventana de la batalla: interfaz); sin uso, 6 que no hacen nada y 6 a las que no se les ha encontrado quien las encole |
| Cartas (las llamadas del juego que mandan una) | 149 de 154 | todas las que se dan jugando | 5: 3 de un evento que nadie crea y 2 de una ventana de pruebas |
| Edificios con vista propia en el juego | 13 de 13 | con su ventana como la del juego y todas sus páginas | nada |
| El panel lateral y sus ventanas como los del juego | 7 de 7 partes | (17..19-11-2026) | nada |
| La ciudad en pantalla (la ciudad viva) | 8 de 11 partes (la 9, casi hecha, sin commit) | la gente, los animales, las animaciones de los edificios y los árboles, los barcos del puerto, el agua, el tiempo, las obras y el fuego | el asedio, el coste en la Vita y el mapa del mar (apartado 4) |
| Batallas | enteras | en el motor y en pantalla | corregir con el ajuste los fallos apuntados (apartado 7) |
| Sonido, música y vídeos | 4 de 6 partes | la base, la música en la partida, los sonidos de la interfaz y los de la ciudad | el mar, las batallas y el asedio; los vídeos; la voz del tutorial (apartado 5) |
| Probar en la Vita | nada probado desde el 04-11-2026 | todo compila para la Vita | probarlo en la consola (apartado 6) |
| Los fallos del juego | el ajuste, con muchos ya corregidos | `fix_game_bugs()` y las correcciones de la lista «Corregidos con el ajuste» | decidir los reproducidos sin corregir; la pantalla para encender o apagar cada corrección (apartado 7) |
| Jugar en red con el juego de PC | nada, salvo la base | el motor idéntico | todo lo demás: la red del juego, la sala, probarlo (apartado 8) |

### La simulación por partes

Solo la simulación (lo que hace una partida por sí sola, sin el jugador ni lo que se ve o se oye): cada parte, con
sus funciones de entrada en el juego y todo lo que cuelga de ellas sin la interfaz.
Las 13 están hechas y comprobadas contra el juego (el mundo entero idéntico en las 12 partidas y los escenarios
retocados); lo que se sabe que queda es el apartado 2.

| Parte | Funciones | KB | Hecho | Lo que falta |
|-|-|-|-|-|
| Tick de ciudad (0x51BA10) | 155 | 67 | **100 %** | nada |
| Tick de cada comerciante (0x4F7BB0) | 25 | 10 | **100 %** | nada |
| Barcos: navegación, IA y convoyes (0x506720) | 152 | 62 | **100 %** de las funciones (la navegación, reescrita por estructuras sin los nombres del juego) | ramas por dentro: los estados raros del barco y el convoy asaltante (apartado 2) |
| Lo diario, tareas programadas y objetos del mundo | 12 | 2 | **100 %** | nada |
| Asedios (0x6349F0, 0x633A90) | 28 | 15 | **100 %** | nada (la vista del asedio, que cambia la partida en tres sitios: apartado 2) |
| Cola de eventos (0x4D85C0) | 415 | 193 | **99 %** | 3 manejadores: 0x4E7BB4 (el 0x14, la red), 0x4DDF40 y 0x4DDE70 (el 0x28 y el 0x29, que nadie crea); dos ramas de la venta de un barco (apartado 2) |
| Operaciones que encola la simulación | 172 | 72 | **100 %** | nada conocido |
| Camino a Gobernador | 223 | 93 | **100 %** | nada conocido |
| Cargar partida (0x549F40) | 103 | 42 | **100 %** (idéntica en las 12 partidas; reescrita por estructuras, `savegame.cpp`) | el objeto opcional del mundo, que ninguna partida tiene (apartado 2) |
| Guardar partida (0x549CC0) | 38 | 11 | **100 %** (la partida escrita, igual byte a byte; por estructuras, `savegame_write.cpp`) | nada |
| Tormentas (0x5053C0) | 4 | 5 | **100 %** | nada |
| Batallas y encuentros | 58 | 32 | **100 %** (por sus tablas de métodos son 249 funciones, 148 KB, en el mar y 41, 50 KB, en las ciudades, sin la interfaz: [cloud/battles.md](cloud/battles.md)) | nada |
| Guiones de misiones (0x4ECF64, 0x511270) | 73 | 23 | **100 %** | nada |
| **Toda la simulación** (advance_time y sus operaciones; no el proyecto) | 597 | 287 | **99 %** | los 3 manejadores de eventos y las ramas del apartado 2 |

Cómo se mide: «Hecho» sale de repasar, función a función, lo que da `re/emu/sesion5/coverage_parts.py` (Python de
Windows: `python coverage_parts.py RUTA/p3vita/src`; con un número de parte detrás, lista lo que no encuentra), que
recorre el grafo de llamadas directas del juego desde las entradas de cada parte, sin la interfaz (los porcentajes son
de los bytes). El script solo no sabe aún qué está hecho: cuenta como reescrito lo que en p3vita se llama
`fn_XXXXXX`, y lo hecho con otro nombre o por estructuras (cargar y guardar, la navegación) le sale como «nombrado» (la
dirección en un comentario) o «sin nada»; por eso sus cifras no se copian aquí. Cuando cada función lleve su marca
(apartado 9), dará «Hecho» por sí mismo. Lo que no encuentra en p3vita y está hecho: en las misiones, el lector de ini
del juego (0x4BE060 → 0x4BE1E0, 8 KB), que p3vita hace a su manera; y en todas, funciones pequeñas (de menos de 300
bytes: accesos, cadenas) metidas en las que las llaman, que solo se podrán dar por seguras cuando estén marcadas. El
grafo es estático (no sigue las tablas de métodos de las batallas) y las partes se solapan. Medido el 24-11-2026
(igual que el 20-11-2026: desde entonces solo ha cambiado la vista). Para todo el ejecutable (`coverage.py`) no hay
medida de avance que valga: fuera de la simulación casi todo es código de ventanas y de dibujo, que p3vita no reescribe
instrucción a instrucción sino que dibuja a su manera; lo que cuenta de la interfaz son las órdenes que encola (las
operaciones) y las reglas de lo que enseña (`player_actions.cpp` y los `*_window.cpp`).

## Lo que falta

### 1. El final de la partida (lo siguiente)

- [ ] **La ventana del final de la partida**: la clasificación de los comerciantes (0x4220B0 → 0x4220D0, sobre
  *0x6CBB30, con el título 0x4F9430 y 0x4F73D0), como la del juego. Hoy el visor tiene una suya (`draw_end_window`),
  y la elección de Gobernador se avisa, hasta entonces, con una línea de noticias del visor. El objetivo de la partida
  (evento 0x0E) ya se comprueba.
- [ ] **El vídeo de Gobernador** (`Eldermann.sks`, el código 0x25 de la ventana de los vídeos de sucesos): con los
  vídeos (apartado 5).

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
  sin ciudad (la misma orden). Se llega subastando un barco y con la instrucción 0x5D de las campañas.
- [ ] **El convoy asaltante busca su batalla con otro barco** (0x50797E, 0x507995): 0x6130B0 recibe la ciudad y el
  dueño del último barco que tocó el bucle de convoyes (esi no se pone en ese camino), no los del guía. p3vita usa el
  guía: habría que seguir esi por todo el bucle de 0x506720. Solo cambia algo si ese barco tiene una batalla en marcha
  en su ciudad, pero para jugar en red hace falta.
- [ ] **El asedio con la vista de la ciudad abierta**: con la vista abierta ([+0x77] del ejército, que pone 0x62B860),
  su tick llama además a 0x62BE90, 0x62DD70, 0x62E970, 0x58CFB0 y 0x62B860 (y 0x632780 a 0x62DD70). Leído con la parte
  9 de la ciudad viva (22..23-11-2026, `re/emu/sesion50/README.md`, «Part 9, the reading so far» y «batch 5»): esas
  ramas son de la vista y, al menos en la llegada, siguen por la misma simulación que sin ella; pero **la vista del
  juego cambia la partida en tres sitios**, que el visor deja como sin vista: un soldado que sale del mapa al principio
  de la carretera en una retirada quita uno de las tropas del ejército ([+0x21E + 4 · tipo], 0x55F5C4); la escena
  construida mientras llega el ejército pone a 0 su [+0x3C] (0x62C927, la cuenta del rescate, que entonces ya es 0); y
  un espadachín de la ciudad que destroza el ariete pone a 0 la munición [+0x4] del ejército (0x577E77). Y la vista
  gasta el `rand()` del mundo, como todas las criaturas. Para jugar en red cuenta que el jugador del PC tenga abierta la
  ciudad sitiada (apartado 8).
- [ ] **Las operaciones a las que se llega desde advance_time por partes sin reescribir**: los gráficos de la ciudad
  (0x599040, el derrumbe de un edificio en la vista, desde el evento 0x25 y la muralla: 0x08, 0x09, 0x0A, 0x2F, 0x39,
  0x52, 0x56–0x59, 0x61 y 0x93) y 0x5C7810, desde los objetos del mundo (0x33, 0x34, 0x36, 0x37 y 0x98). Mirar si
  alguna de esas ramas es simulación. (Las del intérprete de misiones, 0x28, 0x7C, 0x7D, 0x82 y 0x8E, hechas.)
- [ ] **La partida nueva**: 0x545650, que la ventana llama al cambiar el año (21 ciudades en 1300 y una más cada 20
  años: rehace los huecos y sortea las producciones; sin leer aún cuándo se usa: las partidas nuevas de otros años ya
  salen iguales al juego). 0x4F99B0 (una ciudad sin plantilla de mapa) no se usa con los datos del juego.
- [ ] **El encargo de fundar una ciudad**: se cumple cuando la ciudad tiene mercado, y eso solo lo da el evento 0x2F con
  su primer edificio municipal y más de 1000 habitantes. En el emulador (`grow.sh`, partida 1) la ciudad se queda en
  272 habitantes hasta el día 520, y el plazo es de unos dos años. Medir más allá de 550 días (`--desde`) y ver si en
  el juego de verdad crece porque alguien le lleva mercancías. La ciudad nueva, sin probar en el visor de la Vita.
- [ ] El objeto opcional del mundo (0x533DA0): ninguna partida lo tiene y p3vita lo rechaza al leer. Solo si aparece
  una partida que lo tenga.
- [ ] Sin comprobar porque no se pueden preparar: las dos persecuciones que alcanzan sin hueco para la batalla (harían
  falta 255 batallas abiertas).

### 3. Lo que queda de las acciones del jugador

Detalles sueltos; los manejadores de todas las operaciones, en [formats/operations.md](formats/operations.md).

- [ ] Ver en el visor el objetivo de un barco que navega (0x0E: tocar uno de los dos barcos que la simulación le guarda
  cerca, sus [+0x50] y [+0x52]): está hecho, pero cuesta preparar el momento.
- [ ] La fila del ladrón en la taberna: la del juego lleva además un adjetivo del comerciante (de 0x4F9430), que falta.
- [ ] La ventana de los desarrolladores 0x419EB0 encola de todo y manda dos cartas (0x45 y 0x21 con `rand()`): no hace
  falta salvo que se quiera.

### 4. Las pantallas

Cada pantalla se adapta a la Vita y se comprueba a ojo frente al juego original (su código y sus `.ini`, y las fotos
del juego del usuario, `re/emu/sesion31/pc_shots/`), con sus gráficos y sus textos; las reglas de lo que enseña y las
órdenes que encola, contra el juego como siempre. Las normas del visor (tocar los edificios, táctil primero, las
ventanas emergentes), abajo en «Decisiones del usuario». Las ventanas del juego que se juegan, ya como las del juego
(resumen abajo, en «Hecho»), menos la del final de la partida (apartado 1).

- [ ] **La ciudad viva**: todo lo que se mueve o se muestra en la vista de la ciudad (las «criaturas» del juego, sus
  clases, los ficheros `.nodes`, `.spawn` y `.locked` de cada ciudad, las animaciones de los edificios, el tiempo...;
  el inventario y las notas de cada parte, en `re/emu/sesion50/README.md`). La simulación cambia la ciudad como el juego
  y el visor dibuja su estado actual; lo vivo va en `src/p3data/town_life.{hpp,cpp}` (TownLife), `port_scene.{hpp,cpp}`
  y el TownView del visor, con el azar del visor y no el del mundo (apartado 8). El orden que aceptó el usuario el
  20-11-2026: 1) la gente por las calles, 2) los animales, 3) las animaciones de los edificios y los árboles, 4) los
  barcos en el puerto, 5) el agua, 6) el tiempo, 7) las obras, 8) el fuego, 9) el asedio, 10) el coste en la Vita,
  11) el mapa del mar; de la 1 a la 8, en `main` (resumen abajo, en «Hecho»). Encargo: `ENCARGO_CIUDAD_VIVA.md`.
  - Avisos para lo que queda: el juego no tiene noche (lo que parecía «de noche» en la taberna, el faro y el astillero
    es su dibujo sobre la niebla); los barcos del puerto salen de los estados de cada tick y no de los avisos del juego,
    así que dos barcos que cambian en el mismo tick toman sus huecos por su número y no por el orden del juego; el
    derrumbe lo avisa el motor donde el juego llama a 0x599040 (`SaveGame::building_fallen`), sin cambiar lo que
    calcula; la vista llama a los métodos de la segunda tabla de cada clase de casilla, en [+0x3C] (la primera llevó a
    tomar una clase equivocada para los árboles).
  - [ ] **Parte 9, el asedio** (0x62C730 y los objetos del ejército): **casi hecha y sin commit**, en
    `re/emu/sesion50/wip/living_part9a.patch` (contra ec0d456; el README, «Part 9, ...»). En el parche: el motor da al
    visor los puntos del tick del ejército (`SaveGame::siege_view_point`, sin cambiar lo que calcula: `world_tree.sh 7`
    y los escenarios del asedio iguales); la escena del ejército (el campo de distancias, la disposición, las tiendas,
    las filas, la marcha, los tiradores y sus disparos, las escaleras, los proyectiles, el fuego de las torres); los
    soldados de la ciudad (los espadachines, los tiradores en la muralla, la salida por las puertas); el impacto y las
    muertes; el espadachín en su casilla; lo alto de la escalera (con el fallo «La escalera que nadie sube», corregido
    con el ajuste). Falta: el trebuchet, el ariete, la puerta y el humo y el fuego del objetivo; los paseantes en un
    asedio (corren y huyen del ejército: 0x55A2C0), el príncipe y sus huellas, las huellas de los soldados en la
    muralla, el cartel de aviso, el escudero, los guardias de la ciudad en paz ([0x998], la clase 8), las torres en la
    vista de una batalla en el mar; y la ciudad viva en la vista de una batalla (0x58A310, 0x568C80: el humo de las
    llamas y de los derrumbes, que allí el visor no mueve). Los sonidos del asedio, en el apartado 5.
  - [ ] **Parte 10, el coste en la Vita**: cada redibujado rehace la ciudad entera (2 ms en el PC más la lista de
    sprites): si da tirones en avance rápido, redibujar solo las celdas que cambian. Las criaturas se buscan unas a
    otras recorriéndolas (n² con los soldados de un asedio) y preguntan al campo de distancias y a la rejilla en cada
    paso; mirar también las animaciones de los tiles, la escena del puerto, las olas, el oleaje, el pase de niebla, las
    224 nubes de una batalla en el mar y `update_sites`. La nieve: al cambiar de estación (o al entrar en una ciudad en
    invierno) se decodifican los atlas de nieve de sus tiles además de los normales, y se guardan: mirar el tirón y la
    memoria. Y quitar `SavedTown::drawn_lists` y `SavedTown::fixed`, que ya no se usan para dibujar.
  - [ ] **Parte 11, las animaciones del mapa del mar** (`images/Animationen_Seekarte`, `scrollmaps.ini` [Wetter]:
    tormentas, calma, icebergs, la sombra de la tormenta, ballena, peces, gaviotas, olas).
  - Los flecos menores de cada parte (las opciones de las animaciones y del tiempo de P2.cfg, la tecla de ver a través,
    los bancos de niebla, el orden de los barcos del puerto, las figuras del puerto en la vista de batalla, el recuadro
    arrastrado para elegir barcos...), en el README («Part 8, what was left» y el final de cada parte) y en el encargo.
- [ ] **Las filas de la pantalla de opciones que el visor aún no hace** (salen con su valor y sus botones apagados): el
  detalle y el tiempo (los de P2.cfg: con los flecos de la ciudad viva), el volumen de los vídeos y los vídeos de
  sucesos (con los vídeos, apartado 5), los barcos de los convoyes y el guardado automático.
- [ ] **Los gráficos**: decidir la transparencia del motor (magenta, alfa 0 de los `_TP`, sombras con alfa 100); subir
  los DXT3 a la Vita como texturas BC2 sin descomprimir.
- [ ] **Detalles del mapa del mar**: los nombres de las ciudades se ven blandos (stb_truetype sin ajuste a los píxeles,
  marrón oscuro sin contorno: igualarlo con una captura del juego); una franja en los bordes de arriba y abajo de
  `Vollansichtskarte.bmp`, que el juego seguramente tapa: parar la cámara antes.
- [ ] **Las estadísticas en una partida nueva**: hasta la primera carga el juego nombra bien las ciudades (su tabla va
  por índice); sin el ajuste, el visor las saca de su lista por número (UTF-8) y no de `SaveOpaque::town_name_table`
  (Latin-1), como ya hacen las cartas y la crónica.
- [ ] Icono y LiveArea propios del VPK (sin usar arte del juego).
- [ ] Más adelante, con el juego más avanzado: mejoras de manejo pensadas para la Vita (táctil, botones, tamaño de
  letra, accesos rápidos), sin cambiar lo que hace el motor: el usuario habló el 18-11-2026 de una buena pasada de
  usabilidad o una «UX 2.0»; mientras, las decisiones de manejo buscan lo más usable (por ejemplo, el mando imita al
  dedo). Ideas apuntadas para entonces: elegir varios barcos en el mapa del mar (el recuadro arrastrado del juego, 0x44D780;
  en la Vita arrastrar mueve el mapa, y tocar el mar vacío añade un punto de ruta en vez de vaciar la selección); ver
  las teclas de la lista de barcos (X, Select) en pantalla.

#### Mirar en el juego original (el usuario, en su PC)

- Lo que se ve y se anima en la ciudad, sobre todo en un asedio, y las animaciones del mapa del mar (no hay fotos).
- La imagen de las cartas 0x46 y 0x6E (el mapa con la cruz): p3vita supone el cuarto byte de la paleta del mapa a 0xFF
  (sale casi opaco); con 0 saldría mucho más tenue. Si se ve distinta, es lo primero que mirar.
- Las armas del mercado de la oficina (página de armas, 0 espadas, pulsar pasar al barco): si cambian las armas de
  mano, el fallo de 0x4A8F99 (apartado 7) se puede dar.
- El nombre del primer barco del jugador en la partida 1: desde el cambio del cargador «ship names» (91b958b) sale
  «Constance» en vez de «Ermanrich». Ver cuál enseña el juego.
- Los nombres de las ciudades del mapa del mar (letra, tamaño, color, contorno).
- Lo improvisado sin foto del juego (si llega una foto, compararlo): en la taberna, la fila del barco del comerciante y
  del traficante de armas (subida sobre la tabla); en la oficina, las columnas de la tabla de las órdenes del
  administrador; en la caballeriza, los botones del paso 1, 5, 50 y todo; en la oficina de otro comerciante, el sitio
  de la lista «Escoge oferta» y los nombres largos de mercancías en español, cortados antes de sus unidades (el juego
  los monta encima); en el cliente de un pedido, las cabeceras españolas separadas y las frases largas partidas; en la
  puerta de la ciudad, los dos primeros botones bajo la nota de tres líneas (en el juego la nota los pisa); en el menú
  del barco, la fila de los botones de sus páginas, sobre el pergamino.

### 5. Sonido, música y vídeos

Lo que hay y cómo lo decide el juego: [formats/sound.md](formats/sound.md); las notas, en `re/emu/sesion51/README.md`;
el encargo, `ENCARGO_SONIDO.md`. Por partes (plan del 20-11-2026):

Hechas las partes 1 a 4: la base, la música en la partida, los sonidos de la interfaz y los de la ciudad (resumen
abajo, en «Hecho»). Falta:

- [ ] Repasar una vez en `main` los sonidos de la ciudad que puso la ciudad viva (los barcos del puerto, el clic al
  elegir uno, el fuego y el derrumbe) y cambiar el nombre del tercer parámetro de `TownSounds::ship_in_out` (`leaving`
  → `arriving`: el tipo 1 del juego es atracar; no cambia lo que suena). En el encargo, punto 0 y 1.
- [ ] **La atmósfera con el tiempo**: el director calla la atmósfera con los tintes de la vista ([+0xC178] |
  [+0xC1A8], `MusicScene::tinted`); el visor ya dibuja el tinte del tiempo (la ciudad viva, parte 6), pero aún no se lo
  pasa al director. Y la pantalla 0x6CBA64 (de 0x278 bytes, sin identificar), que también elige la música del mar.
- [ ] **Parte 5, el mar y las batallas**: el mapa del mar (sus listas cargadas por él, 0x44AA42, y tocadas con el
  `rand()` del mundo para su variante: elegir un barco 1000, la alarma 2000, la tormenta 3000, los icebergs 3200, los
  barcos 4000, las aves 5000), las batallas en el mar (60000..60100, en la vista por 0x595B00 y los 4 huecos especiales
  0x594AA0) y los asedios (61000..61100: los gritos 0xEE48, los golpes 0xEE8E, los disparos 0xEE70, 0xEE52 y 0xEE84,
  el cañón de las torres 0xEE5C, los impactos 0xEE98 y 0xEA92, que la parte 9 de la ciudad viva no ofrece aún).
- [ ] Al poner el sonido, el visor tiene que decir al motor si está encendido (`Sailing::set_battle_sound`, con las
  variantes de cada lista): con la batalla en pantalla, cada sonido de la batalla gasta un `rand()` del mundo, como el
  juego. Y el sonido del abordaje (0x61F1BF) tendrá que guardar en [+0x14A] lo que devuelve la vista de la ciudad del
  PC con la vista abierta (1, suponiendo un hueco de sonido libre); hoy guarda 0, como el emulador.
- [ ] **Parte 6, los vídeos**: los de los sucesos (`Videos/*.sks`: imágenes JPEG en pase con su música de
  `Musik/Rendering`, [Videos]), el final de Gobernador (`Eldermann.sks`) y la introducción (Bink, `Intro_2.bik`: con
  FFmpeg u otro descodificador). Con ellos, la casilla «Notificación de sucesos» de los ajustes de las cartas ([+0x3B]
  del jugador), que no usa ningún efecto.
- [ ] La ventana de los vídeos de sucesos (*0x6CC7E8: 0x469380, 0x469AF0 por códigos 0..0x25; 0x25 lanza el programa
  del vídeo `eldermann`): hoy el visor hace lo del juego con la opción «VIDEOS» apagada (una línea de noticias), y la
  opción sale como «No». El intérprete de misiones ya la manda al visor (`SaveGame::event_windows`: 0x16, 0x1D y los
  0x19 y 0x1A de las campañas, que el juego enseña siempre en la ventana y el visor aún no); los demás eventos del motor
  que la abren (la elección, 0x4DBBD0 con 0x25 o 6; los incendios, los asedios, el monumento...) aún no la mandan.
- [ ] **La voz del tutorial**: el MP3 de cada paso (`Musik/Tutorial/*fr.mp3`, solo en la versión española, 45 MB;
  `campaigns.cpp` ya lee `windowmp3_N` en `speech`).

### 6. Probar en la Vita

- [ ] Todo lo hecho desde el 04-11-2026 (el usuario lo probará cuando esté más avanzado, 17-11-2026; mientras, cada
  parte se compila para la Vita): el buzón y las líneas de aviso, el barco, la oficina, el astillero y los convoyes,
  el prestamista, construir y el panel lateral, las estadísticas, las batallas en pantalla, la partida nueva, guardar y
  cargar, las rutas comerciales, las ventanas de los edificios como las del juego, las misiones, las campañas y el
  tutorial, la ciudad viva (con su coste: apartado 4, parte 10) y el sonido. En especial el teclado de la Vita (el IME
  de SDL: nombres de barco, de jugador, de partidas, de ficheros de ruta), que no se puede probar sin ella. Cada VPK
  nuevo: `bash scripts/build-vita.sh` → `dist/p3vita.vpk`; lo que va en `ux0:data/p3vita/`, en
  `Patrician III\ux0\data\p3vita\` (la caché de textos hay que rehacerla una vez con el exe en esa carpeta cuando cambia
  su versión; la música, en su carpeta `Musik`).

### 7. Corregir los fallos del juego

El ajuste existe: `fix_game_bugs()` (`src/p3data/game_fixes.hpp`); cada corrección va con `if (fix_game_bugs())` junto
al comportamiento original. Apagado en la librería (el oráculo, tick_probe y los tests comparan con el original),
encendido por omisión en el visor (Opciones; `p3vita-settings.txt`; `--fix-game-bugs on|off`), y apagado siempre en
una partida en red. `tick_probe --fix-game-bugs` enseña lo que cambian. Las correcciones que solo cambian un texto, lo
que se ve o lo que se oye se hacen sin preguntar; las que tocan la partida o el equilibrio, se preguntan antes, una a
una.

Pendiente:
- [ ] **Decidir y corregir los fallos reproducidos que aún no tienen corrección** (lista abajo, «Reproducidos, sin
  corregir aún»): los de los piratas y las batallas en el mar, los de la batalla en una ciudad, los de la batalla que
  dirige el jugador, construir en el borde de la rejilla, el reloj de las batallas, los de las cartas, los de lo que
  enseñan las ventanas y los pocos de la vista.
- [ ] **Más adelante, cada corrección por separado** (idea del usuario, 05-11-2026; el 17-11-2026, una pantalla de
  ajustes para encender o apagar ciertas cosas, cada corrección por separado o como complementos): un inventario de los
  fallos y rarezas y una pantalla en las Opciones para encender o apagar cada corrección, en vez del ajuste único. Por
  eso cada fallo de la lista de abajo tiene que quedar bien identificado: dónde está, qué hace mal y qué cambia su
  corrección.
- [ ] **La pantalla de ajustes, como la de las cartas** (idea del usuario, 19-11-2026): la ventana de ajustes de las
  notificaciones del juego (352 × 544, sus filas con «−» y «+» y sus casillas con la marca; `draw_mail_options`,
  `re/emu/sesion43/README.md`) puede servir de modelo para la configuración del juego: las correcciones de los fallos
  una a una y los ajustes del visor.
- [ ] La fiesta de la IA que nunca ocurre, como opción aparte (abajo, «No se corrigen»).

### 8. Multijugador con el juego original (al final de todo)

La razón de fondo para reescribir el motor idéntico: si p3vita se comporta byte a byte como el juego, una Vita puede
jugar en red con el juego original en un PC. Las acciones del jugador no cambian la partida directamente: son órdenes
que van a una cola y ejecuta la simulación, y en red solo viajan esas órdenes.

- [ ] La simulación entera idéntica, no solo lo que sale en las partidas de prueba (los estados raros del barco, las
  ramas del apartado 2...). Un solo byte distinto y las dos partidas se separan.
- [ ] Ojo: el juego tira los gestos al azar de los interiores (el ayuntamiento, 0x5A94FA, y los demás edificios) con el
  mismo `rand()` de la CRT que usa la simulación: en el PC, tener un edificio abierto cambia los números al azar de la
  partida. El visor usa un azar aparte para esos gestos. Para jugar en red habrá que ver cómo lo resuelve el juego.
  Lo mismo la ciudad viva (20-11-2026): la gente, los animales, los carros, los soldados del asedio... de la vista de la
  ciudad gastan ese `rand()` (más de cien llamadas, `re/emu/sesion50/README.md`); en el visor tienen su azar propio
  (TownLife::rand).
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
  los números al azar de la partida. El visor usa un azar aparte (como para los gestos de los edificios). Igual los
  sonidos de los edificios de la ciudad (0x594770: su variante y sus esperas) con el sonido encendido.
- **El azar de los sonidos**: con la batalla en pantalla y el sonido encendido, los sonidos gastan el `rand()` del
  mundo (apartado 5); el dibujo de una batalla también gasta `rand()` por fotogramas (0x62018B, 0x62F0E2), fuera del
  motor: con una batalla a la vista y sonido, no podrá ir exacto.
- **La vista del asedio**: con la ciudad sitiada a la vista, el juego cambia la partida en tres sitios (las tropas, la
  cuenta del rescate y la munición del ejército) que el visor no toca, y **el convoy asaltante que busca su batalla
  con otro barco** (los dos, en el apartado 2).
- **El barco nuevo de una guarida sin barcos** toma su fuerza de lo que haya en la memoria (abajo, en los fallos de la
  batalla en una ciudad).
- **El choque de dos barcos muy montados** en una batalla en el mar (0x608F70, 21-11-2026; la partida 4 llega a los
  ~50 días): el juego toma el lado del golpe del byte bajo de una dirección del montón que quedó en la pila (abajo,
  «Los piratas y las batallas en el mar»). Cambia de un PC a otro: ni dos PC con el juego original darían lo mismo;
  p3vita pone 0. Habrá que ver qué hace el juego en red cuando no cuadran sus sumas de comprobación (0xB8: 0xB9 por
  barco, 0xD4 con las posiciones de los barcos).
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
- [ ] **Cada función del juego, identificable en p3vita, y el inventario de funciones** (lo pidió el usuario el
  24-11-2026: sin eso no se puede garantizar que no falte ninguna función, y un inventario de las funciones y de lo que
  hace cada una vendrá bien). Hoy hay de todo: unas van con su nombre `fn_XXXXXX`, otras con un nombre descriptivo y la
  dirección en algún comentario, y otras sin rastro (lo metido dentro de otra función, lo hecho por estructuras); así
  `coverage_parts.py` no distingue lo hecho sin ese nombre de lo que falta (arriba, «La simulación por partes»). La
  marca, desde el 24-11-2026 (abajo, «Decisiones del usuario»): el nombre `fn_XXXXXX` o, si no, `@fn 0xDIRECCIÓN` en el
  comentario de la función de p3vita que la reproduce (una por cada función del juego que reproduce, también las que
  van metidas dentro de ella), con una frase de lo que hace, y `@skip 0xDIRECCIÓN` con su motivo para las saltadas a
  propósito (las ventanas, el lector de ini del juego). Puestas ya: `@fn 0x504230` (`parse_ships`) y `@fn 0x51B330` (`write_town`). Falta: que
  `coverage_parts.py` lea las marcas (y las `fn_XXXXXX`) y separe lo hecho, lo saltado y lo que falta de verdad; marcar
  lo ya hecho, repasando función a función lo «nombrado» y lo «sin nada» de cada parte (con un número de parte, el script
  ya lista lo que no encuentra; habría que listar también lo nombrado), empezando por las partes con más (cargar, los
  barcos, la cola de eventos); sacar de las marcas el inventario (cada función del juego, dónde está en p3vita y qué
  hace); y que el script siga también las tablas de métodos, como el grafo de las batallas (`re/battles/graph.py` de
  p3vita-datos).
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
partidas `.pat` (leídas desde la versión 0x82; escritas en la 0x85 con el compresor del juego, iguales byte a byte), las
estadísticas `.pst`, los guiones de las misiones `.p2m`, la música MP3 y los efectos WAV.

**El motor, idéntico al juego** (`src/p3data`):
- el comercio, los precios y sus umbrales; el tick de cada ciudad (0x51BA10) y de cada comerciante (0x4F7BB0); lo
  diario y semanal, las tareas programadas, los 15 objetos del mundo y las estadísticas diarias;
- la navegación (las rutas entre ciudades, el movimiento, la salida y la llegada, la orden de zarpar), los barcos y los
  convoyes de la IA (comercio, destino, tripulación, armas, astillero), la reparación de barcos y convoyes en el
  astillero, las obras y las murallas de las ciudades;
- la cola de eventos (54 de los 58 tipos) y las 37 operaciones que encola la simulación; el inventario de
  [inventario.md](inventario.md), partes A a D cerradas (D, salvo los estados del barco);
- los asedios, las tormentas, los piratas (encendidos por omisión), y **las batallas** enteras: automáticas y
  dirigidas por el jugador, en el mar y en una ciudad o una guarida, con sus balas, sus choques con rocas e islotes,
  sus órdenes, sus ventanas y su tiempo (un tick cada 3375 ms mientras el jugador dirige), guardadas en la partida
  (notas: [cloud/battles.md](cloud/battles.md), [cloud/battles-2.md](cloud/battles-2.md),
  `re/emu/sesion21/player_battle_notes.md`); la diferencia de la partida 4 a los ~50 días, un fallo del juego que lee
  la pila (21-11-2026, `re/emu/sesion53/README.md`: el oráculo pone a 0 los locales de 0x608F70, como p3vita);
- **el camino a Gobernador** ([formats/world_tick.md](formats/world_tick.md) §7 y §8): la candidatura, los encargos de
  la Hansa de los cinco tipos (fundar una ciudad, la carretera, cazar un pirata, la ciudad que crece, la guarida), las
  elecciones con el voto del jugador (0x3F), la reunión de la Hansa, la dieta con su caza de piratas y su carta final
  0x3D (21-11-2026) y la ventana del final;
- **el buzón** ([formats/mail.md](formats/mail.md)): mandar y guardar las cartas como el juego (149 de las 154
  llamadas), el texto de cada carta (el compositor 0x47F3B0) y la crónica (0x47DD40) en los dos idiomas, las líneas de
  aviso (0x548CA0), leer, borrar y los ajustes (0x6E, 0x6F, 0x70, 0x71), las cartas con imagen y los botones de las
  cartas (0x4D6C90: la boda, 0x72 con el evento 0x17; la petición del príncipe, 0x8B y 0x8C; el embargo, 0x77; las
  ofertas, 0x75; las cartas de misión, 0x74);
- **las acciones del jugador** (las 106 operaciones; los manejadores, en [formats/operations.md](formats/operations.md)):
  comprar y vender, la tripulación y el capitán, reparar, la oficina y su administrador (0x5A, 0x5B, 0x5E, 0x62, 0x63,
  0x66), el astillero (encargar, cancelar, mejorar, cambiar el nombre), los convoyes, los préstamos y las acciones de
  empresa, construir, derribar, las calles y subastar un edificio, hacerse pirata, los encargos de la Hansa, las rutas
  comerciales (0x68..0x6D) y sus ficheros `.rou`, la iglesia (0x30..0x32) y el baño (0x39, 0x42, 0x43), las subastas del
  gremio (0x33..0x36 con el evento 0x2D), la herrería (0x50, 0x56 y la prioridad del alcalde, 0x57), la taberna (sus
  personas y la 0x52; el comerciante, 0x4D; el traficante de armas, 0x50; el viajero, 0x53 con el evento 0x10; el
  ladrón, 0x4F con el evento 0x0F; el pirata, 0x4E; el soplón, 0x4C y 0x55), las ofertas a otro comerciante (0x5F, 0x75
  con el evento 0x13), la ruta por puntos (0x0B), dos barcos (0x0E, 0x0F, 0x10, 0x11), el transporte por tierra (0x3A
  con el evento 0x0C), el ayuntamiento (los pedidos, 0x3D con el evento 0x11 y la entrega 0x51; mudar la ciudad natal,
  0x3E; la milicia, 0x40 y 0x41; un barco al servicio de la ciudad, 0x44; la candidatura a alcalde, 0x45; las arcas,
  0x47; las propuestas y el voto en el consejo, 0x48 y 0x49; las herramientas de los desarrolladores de los encargos de
  la Hansa, 0x4A), la fiesta (0x61), borrar un barco perdido (0x0A), los asedios (0x88, 0x89, 0x8A, 0x8D, 0x90 y la
  0x8F, que nadie encola) y los grupos de barcos de otro jugador (0xBF); y las reglas de las ventanas que las encolan
  (`player_actions.cpp` y los `*_window.cpp`: `building_windows.cpp`, `guild_window.cpp`, `office_window.cpp`,
  `offer_window.cpp`, `smithy_window.cpp`, `ship_pair_window.cpp`, `tavern_window.cpp`, `land_trade_window.cpp`,
  `town_hall_window.cpp`, `town_gate_window.cpp`, `market_hall_window.cpp`, `ship_list_window.cpp`,
  `statistics_window.cpp`, `trade_route_window.cpp`, `letter_window.cpp`, `new_game.cpp`...);
- **la partida nueva** (`new_game.cpp`; notas en `re/emu/sesion24/new_game_notes.md`): el mapa, 0x544E50 y sus
  operaciones, los 70 días de antes de jugar, el jugador y sus ajustes: el `.pat` que escribe p3vita es igual byte a
  byte al del juego, también con otros años, otros jugadores y mapas propios;
- **las rutas comerciales** (`trade_routes.cpp`; notas en `re/emu/sesion25/trade_routes_notes.md`): las paradas, los
  barcos y convoyes recorriéndolas, y los ficheros;
- **las misiones** (19..21-11-2026; `missions.cpp`, `mission_script.cpp`; [formats/missions.md](formats/missions.md);
  `re/emu/sesion45/`, `sesion47/`, `sesion48/`, `sesion53/`): son programas pequeños (`missions_addon/*.p2m`, 94
  ficheros con los textos de sus cartas dentro, en inglés y en español) que `scripts/missions_eng.ini` reparte por
  modos de juego; cada guion es un evento 0x1B que ejecuta el intérprete 0x4ECF64, una instrucción por sección. Hecho:
  leer el ini y los `.p2m` (los españoles con el archivo español), la lista de misiones del juego, la operación 0xC0 que las arranca,
  cargar y guardar los guiones en curso con sus variables, el bucle del intérprete con sus esperas y sus 138
  instrucciones (19 no las usa ningún fichero), lo que encolan (0x28, 0x7C, 0x7D, 0x82, 0x8E) y crean (el evento 0x09,
  la cosecha), las cartas que componen y las cuatro operaciones del jugador sobre ellas (0x74, 0x54, 0x55, 0x76).
  Comprobado: el mundo entero 30 días con las misiones igual al juego en las 12 partidas, llamada a llamada, y la cola
  de eventos y las operaciones 60 días; las ramas a las que no llegan las partidas, con guiones propios en estados
  retocados (`mission_*`). Desde el 21-11-2026 el oráculo y `world.sh` llevan las misiones por omisión
  (`--sin-misiones` para quitarlas);
- **las campañas y el tutorial** (`campaigns.cpp`; `re/emu/sesion52/README.md`): lo que hace la carga de una partida
  de campaña o del tutorial (`missav/1..7.pat`, `tut0..4.pat`) y el «Comenzar» de la pantalla de campañas (0x43097C..:
  el buzón vaciado, los guiones quitados, la lista de la campaña, 0x54A480), comprobado en las 7 con la partida que
  escribe el juego tras el arranque, byte a byte, y el mundo 7 días desde cada arranque. Las partidas de la versión
  0x4D (`missav/0100–0204.pat`) **el juego no las carga** (21-11-2026, leído en el ejecutable): su único lector de
  ficheros, 0x5476E0, lo piden la pantalla de partidas guardadas, las campañas 1..7 (`MISSAV\<n>.pat`, de la versión
  0x82) y el tutorial (`tut0..tut4`); las 0x4D, con la carpeta `missions/` (`0100xx_k.p2m`...), son restos de un
  sistema de campañas por capítulos anterior (escritas por una versión de depuración, con relleno 0xCD) que el lector
  del juego ni siquiera entiende: no hay nada que leer.

**El visor** (`src/app`; en el PC con SDL2 de escritorio y sin ventana con `--steps` y capturas, `p3vita --help`),
con los gráficos, las fuentes y los textos del juego, como sus ventanas (su código, sus `.ini` y las fotos del
usuario):
- la pantalla de inicio (el libro del menú principal), la ventana «Configuración del juego», la pantalla de campañas
  y la de los capítulos del tutorial con la ventana de sus pasos (sus recuadros llevados a los elementos del visor), la
  pantalla de opciones del juego y su pantalla de partidas (guardar y cargar por nombre, como el juego)
  (`re/emu/sesion46/`, `sesion52/`);
- el panel lateral del juego entero (17..19-11-2026, `re/emu/sesion40/` .. `sesion44/`): la lista de barcos (también
  desde el faro), el menú del barco con sus estados, sus páginas, la ruta comercial y la ventana de formar convoy,
  construir (con la colocación táctil), las estadísticas, los mensajes con la carta y la crónica en la ventanita del
  juego y los ajustes de las cartas, el panel de las batallas y la barra de arriba;
- las ventanas de los 13 edificios con vista propia (la lonja, la taberna, el astillero con el muelle, la oficina, el
  prestamista, la iglesia, el baño, el gremio, la herrería, la caballeriza, la armería, el ayuntamiento y la puerta de la
  ciudad), con su marco, su imagen con sus animaciones y gestos, su menú lateral y todas sus páginas como las del juego
  (13..15-11-2026; `re/emu/sesion31/views_notes.md`, `sesion33/` .. `sesion38/`); las ventanas de papel sobre la ciudad
  (la oficina de otro comerciante con sus ofertas, la armería, la fiesta); la ventana de comerciar; la ventanita de
  mensaje del juego (0x41D9B0) con sus preguntas; las líneas de noticias y las ayudas emergentes del juego; los textos
  con sus iconos, imágenes y capitulares (`re/emu/sesion39/`, `sesion45/` .. `sesion47/`); las cartas de misión y la
  trastienda de la taberna;
- el mapa del mar con la ruta por puntos; las batallas que dirige el jugador (la pregunta, en el mar, en una ciudad y en
  una guarida, con su panel, sus ventanas y sus órdenes táctiles);
- la ciudad con su estado actual, sus sombras y su nieve por estaciones, y la ciudad viva, partes 1 a 8
  (20..22-11-2026, `re/emu/sesion50/README.md`): la gente por las calles y los carros del comercio por tierra; los
  animales, los pájaros y los peces; las animaciones de los edificios, los árboles con sus estaciones y el pescador; los
  barcos del puerto con sus banderines al viento, la marca de los elegidos, sus iconos, sus puntos en el minimapa, sus
  nombres y el «ir al barco»; el agua (las olas, los destellos, el oleaje de la costa, los témpanos); el tiempo (la
  lluvia y la nieve, el tinte y la niebla, las huellas en la nieve; las nubes, que son de las batallas en el mar); los
  banderines de las torres; la competencia sombreada con la casa de la barra de arriba; las obras y las calles
  empedradas; el fuego, el humo de las llamas y de los barcos dañados, la ruina y el derrumbe;
- el sonido, partes 1 a 4 (20..22-11-2026; `re/emu/sesion51/README.md`, [formats/sound.md](formats/sound.md)): la base
  (el objeto de sonido del juego, *0x6CBDB8, sobre el audio de SDL, `src/app/audio.hpp`: la música y la atmósfera con
  sus fundidos, los 20 canales de efectos y los 4 de la interfaz; los MP3 con dr_mp3, los WAV IMA ADPCM y el
  `sound.ini`; los volúmenes de las Opciones; la música del menú principal; la carpeta `Musik` va en
  `ux0:data/p3vita/Musik/`); la música en la partida (el director 0x42C080, `src/app/music_director.hpp`, con sus
  atmósferas, el tema de cada ciudad, la música de batalla y la de los 12 edificios con ventana); los sonidos de la
  interfaz (los botones, las cartas, el baño, la oración, el escupitajo, «Invertir estéreo»; el pitido del juego al no
  poder cerrar una ventana, 8001, no tiene caso en el visor, cuyo Círculo abre las Opciones); y los de la ciudad
  (`src/app/town_sounds.hpp`, el motor de la vista con sus 17 huecos, 0x594C00, 0x594770, 0x5948D0, 0x594B50: los
  edificios desde su paso de animación de la ciudad viva, solo si trabajan; las campanas por el reloj del PC; los
  efectos parados al cambiar de pantalla; los barcos del puerto al zarpar y atracar, el clic al elegir uno tocándolo,
  el fuego y el derrumbe). La música y los sonidos de la ciudad, con un azar aparte (apartado 8).
Las cargas en la Vita, más ligeras y a demanda con aviso.

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
  más avanzado: mientras, cada parte se sigue compilando para la Vita. (A 24-11-2026: hechos el 1 y el 2; la ciudad
  viva y el sonido van en paralelo desde el 20-11-2026, a petición del usuario, cada uno en su copia del repositorio.)

- **Sin flecos** (14-11-2026: «No quiero dejar cosas atrás, hay que acabar lo que falta primero … no dejar flecos
  por todos lados»): lo que falte de algo ya empezado se termina antes de empezar nada nuevo; lo que una sesión deje sin
  hacer pasa a lo primero de la lista, no al final.

- **El motor idéntico al original**, comprobado contra el juego, aunque vaya más lento (04-11-2026): para jugar en red
  con el PC al final. Lo visual se comprueba a ojo.
- **Los fallos del juego**: reproducidos tal cual, apuntados aquí y corregidos detrás de `fix_game_bugs()`, encendido
  por omisión en el visor. Las correcciones que cambian el equilibrio se preguntan una a una.
- **No todo lo que salen en las partidas**: el inventario de la simulación sale del ejecutable, con estados retocados
  para lo que las partidas no alcanzan.
- **Cada función del juego, identificable** (24-11-2026): toda función del juego que p3vita reproduce se puede
  encontrar por su dirección. Una función de p3vita llamada `fn_XXXXXX` ya lo es (con su comentario de lo que hace);
  si no, lleva la marca `@fn 0xDIRECCIÓN` con una frase de lo que hace, en el comentario de la función de p3vita que la
  reproduce, una por cada función del juego que reproduzca (también las metidas dentro de ella o hechas por
  estructuras); y la que se salta a propósito, `@skip 0xDIRECCIÓN` con su motivo. Para poder hacer el inventario de
  funciones y garantizar que no falta ninguna (apartado 9).
- **Las batallas avanzan con una operación 0x94 de 84 pasos tras cada tick con batallas**, en p3vita y en el oráculo
  (en el juego depende del reloj real). Los piratas y la batalla automática, encendidos por omisión también en el
  visor.
- **El visor se maneja como el juego**: a cada acción se llega tocando su edificio (o el barco desde el panel); el
  panel lateral del juego, fijo a la derecha, abierto por defecto y plegable, con botones más grandes para el dedo.
- **Las ventanas, como las del juego**: de su código y sus `.ini`, comparadas con las fotos del usuario antes de
  enseñarlas; la ventana entera con su marco, ajustada al alto de la Vita (×0,95). Sin foto, improvisar (13-11-2026; lo
  improvisado, arriba en «Mirar en el juego original»).
- **Táctil primero**: tocar y arrastrar sobre lo que se ve, con lo posible resaltado (construir: las zonas donde cabe
  en verde, el edificio en verde o rojo bajo el dedo); nunca un cursor fijo en el centro para nada nuevo (la cruz para
  entrar en los edificios se aceptó «de momento»; si se rehace, preguntar).
- **L y R son la velocidad del juego** (más lenta, más rápida, con su aviso): en las ventanas sobre la ciudad y, desde
  el 19-11-2026 (a petición del usuario), también en la ciudad y en el mapa del mar, donde cambiaban de ciudad (de poco
  servía: en el mapa del mar una ciudad se elige tocándola). En una batalla siguen pasando de barco. Con un edificio
  abierto, arriba y abajo de la cruceta eligen el botón de su menú lateral (14-11-2026).
- **El manejo, lo más usable** (18-11-2026): las decisiones de manejo las toma la sesión, con el mando imitando al
  dedo. Elegidas así o por el usuario: varios barcos a la vez con una pulsación larga (en la lista y en el puerto), como
  el Ctrl del juego; al colocar un edificio con el mando, el joystick derecho lo mueve celda a celda y el izquierdo
  mueve la vista (idea del usuario); los dos botones de la ruta por puntos del mapa del mar, solos y discretos abajo en
  el centro (el papel de debajo le pareció «demasiado cantoso», 20-11-2026).
- **Las ventanas emergentes**: cada botón táctil con su tecla (X la acción, ○ cerrar), sin notas que expliquen lo
  obvio, lo que no se puede hacer atenuado y sin poder pulsarse, y con la ventana abierta la barra de arriba no enseña
  sus acciones.
- **Lo que se queda distinto del juego, por decisión del usuario**: en la pregunta de una batalla, el segundo botón
  («Que luche el ordenador») y su barra del tiempo (08-11-2026); la tabla de comerciar con el mismo margen a los dos
  lados y, en el modo de las armas, sin la columna del precio medio (19-11-2026); el texto de la taberna centrado bajo
  el retrato cuando es más estrecho; las partidas por nombre, como las del juego (`savegames/ein/NOMBRE.pat`,
  19-11-2026); las líneas de noticias propias del visor que hacen falta (una orden del mapa del mar que no se puede, la
  carga del mapa del mar o de una batalla), y los avisos del visor que el juego no tiene, quitados (20-11-2026). Y como
  el juego aunque escriba la partida: la fecha del menú del astillero (14-11-2026).
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

- **Los bancos de niebla sin dibujar** (la ciudad viva, 0x590543..0x5905BA; 22-11-2026, parece, leído en el código):
  el juego hace 20 filas de 5 bancos, pero dibuja de cada fila solo los 3 primeros, y deja sin dibujar la última fila
  (la cuenta se corta en 19 en vez de 20). Solo es la vista; puede ser a propósito (menos niebla): sin corregir.

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
- 0x60ADBF deja sin poner el lado que 0x608F70 lee de la pila (seguro: visto en la partida 4 a los ~50 días, tick
  126953125, `re/emu/sesion53/README.md`): cuando el segmento desde el centro del otro barco hasta el cruce de los
  cascos no toca ningún lado, el juego toma como lado el byte bajo de un puntero que dejó allí 0x60BCDA (el bloque del
  barco + 0x27 + una dirección), y con él lee 0x67AB98 fuera de su fila. Cambia con las direcciones del montón: no se
  puede reproducir; p3vita y el oráculo ponen 0. Corrección posible: el lado del casco más cercano al cruce.
- La carta final de la caza de piratas (0x4F3CE0, `diet.cpp`; parece, y no se da): los barcos perdidos de la flota
  salen como «Tu barco...» en la carta de cada jugador, sean de quien sean; con alguno del propio jugador, la lista
  pone el encabezado y la frase del último con su «%s» sin rellenar, y a los apresados y hundidos por el temporal la
  frase de «hundido» en vez del sufijo. Solo con barcos del jugador en la flota, que son de la IA (0x50AC40).
  Reproducido.
- Parecen, sin comprobar: 0x61A9E0 mira el borde de arriba con la x; 0x605420 y 0x606CC0 leen el primer barco del
  convoy de un lado sin mirar si existe (p3vita no cuenta ninguno); 0x60BCDA se puede colgar con un radio de 0; el bucle de nombres de 0x5063C0 no acaba con
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
  las que tiene (0x53EC61: `jb` en vez de `jbe`; parece). Reproducida la comparación (`missions_detail::fn_53ec30`);
  esa variable de más cae en el sitio del búfer tras ellas, que p3vita no guarda.

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

**El viento de las nubes de una batalla** (0x580520; 22-11-2026, seguro, leído en el código): el paso de cada nube
mira si su velocidad sigue cerca de la del viento comparando la de lado dos veces (con 32 y con 22) y nunca la de
arriba abajo, que así no vuelve nunca a la del viento salvo cuando la de lado se aleja. Solo es la vista. Reproducido
(`cloudcmp.sh`). Corregido detrás de `fix_game_bugs()` (la segunda, la de arriba abajo; `battle_cloud_step`).

**Lo que suena** (20-11-2026, leídos en el código del juego, `re/emu/sesion51/`; solo cambian lo que se oye, como los
de los textos: corregidos sin preguntar): la atmósfera de la ciudad al mostrarla (0x42C770 con 0: 0x42CCD1) sale la de
verano con el hielo y la de invierno sin él (la duración que calcula antes es la de la buena); el director de música
(0x42C080) busca la región de la ciudad en la tabla de StadtDaten.ini por el índice de la ciudad en la partida, no por
su número (0x6DE4B8), y elige la música de otra región; el tema de Rügenwald es `Ruegenwald.mp3` en StadtDaten.ini y el
fichero se llama `Rügenwald.mp3`, así que no suena (`music_director.cpp`, `main.cpp`); la lista 7000 (el baño, la
oración, el escupitajo) solo se carga al abrir el baño (0x5B1D41), así que hasta entonces ni la oración de la iglesia
ni el marinero de la taberna suenan (`main.cpp`: con el ajuste se carga antes de tocarlos).

**Las cartas de misión de la trastienda al salir de la taberna** (operación 0x52 de tipo 10, 0x53C2C0; 20-11-2026,
seguro, comprobado con el oráculo, escenario `tavern_side_room`): al irse, el juego quiere soltar las cartas 0x71 de
esa ciudad que el comerciante tenía apartadas (la variable del destinatario a 0xFFFF), pero el bucle sigue solo
mientras el número de la carta en el buzón es menor que el número de comerciantes (0x53C6FB, 0x53C796), y las cartas
suelen estar mucho más allá: casi nunca suelta ninguna. En una partida de un jugador no cambia nada (la carta sigue
apartada para él y él la sigue viendo); en red, otro humano no la vería en su taberna. Corregido a petición del usuario (21-11-2026; `re/emu/sesion52/README.md`, «Part 5»).

**La carta de misión a los dueños de las oficinas de una ciudad** (instrucción 0xF7 del intérprete, 0x4ED4A0;
20-11-2026): con respuestas, el destinatario −3 debería mandar una copia a cada dueño de una oficina de la ciudad
de la carta, pero antes de mirarlo un `memset` pone a −1 las ocho variables desde la del destinatario, así que la
carta va a todos los humanos (la rama del −3, en 0x4EDD57, no se ejecuta nunca). Corregido a petición del usuario (21-11-2026; `re/emu/sesion52/README.md`, «Part 5»).

**La mercancía que más le falta a una ciudad** (0x51BEB0, la usa la instrucción 0x2F del intérprete; 20-11-2026,
parece): de cada mercancía mira los obreros de la instalación con el número de la mercancía ([+0x844 + 16 ·
mercancía]) en vez de la de la empresa que la produce (lee ese tipo de 0x672C88 y no lo usa; la rama del aceite de
ballena queda muerta), como sí hace la lonja (0x5DE3xx). Cambia qué mercancía pide la misión 6 (y su cantidad): Corregido a petición del usuario (21-11-2026; `re/emu/sesion52/README.md`, «Part 5»).

**La instrucción 0xE8 con un comerciante que no existe** (0x4ED086; 20-11-2026, seguro, leído en el código): salta a
la misma instrucción sin avanzar (0x4F2C97) y el juego se queda colgado. p3vita termina el guion. Ninguna misión del
juego le da un comerciante fuera de la cuenta. Corregido a petición del usuario (21-11-2026; `re/emu/sesion52/README.md`, «Part 5»).

**Las cargas que no son mercancía de un barco** (0x518640 y 0x5186D0, las usa la instrucción 0xF1; 20-11-2026,
parece): al subirlas, la bodega ocupada [+0x118] suma las cargas nuevas · 2000, no las añadidas; al bajarlas, resta
las que quedan, no las quitadas. Solo se nota si ya había cargas al subir o si quedan al bajar, y se arregla en el
siguiente recálculo de la bodega (0x5182B0). Comprobado con el oráculo (`mission_ships_money`). Corregido a petición del usuario (21-11-2026; `re/emu/sesion52/README.md`, «Part 5»).

**El barco más rápido de un comerciante en una ciudad** (instrucción 0x57 del intérprete, 0x4F01AA, solo en las
misiones de la partida en red; 20-11-2026, parece): compara la velocidad de cada barco (0x5170F0) con la marca, pero
guarda como marca su potencia de fuego [+0x120], así que el elegido no es el más rápido. Reproducido
(`mission_network`). Corregido a petición del usuario (21-11-2026; `re/emu/sesion52/README.md`, «Part 5»).

**El incendio de la instrucción 0xED** (0x4EE567, campañas; 20-11-2026, parece): busca la ciudad y su sitio con el
número del operando (el de la variable) y no con su valor, que sí pone en la operación 0x7D; 0xE5, su pareja, usa el
valor. Reproducido (`mission_campaign_b`). Corregido a petición del usuario (21-11-2026; `re/emu/sesion52/README.md`, «Part 5»).

**El camino del pescador** (la ciudad viva, 0x569DAD y 0x56A450; 21-11-2026, seguro, leído en el código): al salir, el
pescador marca su camino de la etiqueta 4 de `.nodes` como ocupado ([vista+0xC59C]) y nadie lo vuelve a liberar (ni su
destructor ni nada hasta que se carga otra vez la ciudad). Si su cabaña deja de trabajar y vuelve, el siguiente sale
por otro camino, y cuando se acaban los que empiezan a menos de 304 píxeles, la cabaña se queda sin pescador. Solo es
la vista (nada de la partida). Corregido a petición del usuario (21-11-2026): al quitar al pescador se libera su
camino (`TownLife::remove_creature`).

**Los barcos de las filas impares en el minimapa** (la ciudad viva, 0x59C798; 21-11-2026, parece, leído en el código):
el punto de un barco va en su celda · 2 de ancho, y en las filas impares el juego suma 1 a la fila en vez de medio
punto a la derecha, donde el minimapa pone esas filas: el punto queda una fila por debajo y medio punto a la izquierda.
Solo es la vista. Corregido detrás de `fix_game_bugs()` (medio punto a la derecha; `TownView::draw_port_dots`).

**Una ciudad por su nombre** (instrucción 0x65 del intérprete, 0x4F26C0 → 0x5136A0; 21-11-2026, seguro, leído en el
código y visto en el oráculo con la campaña 3 en marcha): busca el nombre k del fichero de ciudades en la tabla 0x6DDA00
recorriéndola por índice de ciudad (de la última a la primera), pero esa tabla va por número de ciudad: con k menor que
el número de ciudades devuelve el índice k, que es otra ciudad salvo que su número sea k, y con k mayor, −1. En «El
auge de la Hansa» (la campaña 3, la única que la usa) la misión 301 («habitantes de Rostock», número 15) vigila la
ciudad de índice 15, que es Stockholm; la 305 (Londres, número 4) la de índice 4; y las 302 y 303 (Stettin, 28, y
Stockholm, 25) reciben −1, con el que 0x34 lee los habitantes de la ciudad −1 sin comprobar nada: memoria de antes de
las ciudades, que en el juego de verdad decide su montón (en el emulador sale 0x16CF492: la misión cumplida al
momento, 59 000 monedas y su carta). Sin el ajuste p3vita lee 0 ahí (la misión espera), y el oráculo también
(town_oracle.py `TOWN_RESIDENTS_READ`). Corregido a petición del usuario (21-11-2026): la ciudad cuyo número es k
(`mission_script.cpp`, 0x65; `re/emu/sesion52/fix65.sh`: Rostock en el índice 10, Stettin en el 17, Stockholm en el 15
y Londres en el 2).

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
- Los avisos de la caza de piratas (los nodos del evento 0x34) pierden el nombre del barco en cada guardado y carga
  (0x4F39E0 escribe el signo de su byte bajo y ese byte, 0x4F3A40 lee el primero dos veces: el nombre vuelve 0 o
  0xFFFF y la carta final dice otro barco; parece). Corregido: el nombre en dos bytes (el alto y el bajo), y al leer
  el signo 0xFF del juego como 0 (21-11-2026; antes, los bytes como se leyeron).
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
  0x52F030 y del informe de la expedición, que no se dan con los datos del juego; el humo de un fuego y de un derrumbe
  (0x550A70, 0x599040) no fija su alfa [+0x30], que da su gris (0x579D6B): el juego lo pinta del que dejara la memoria;
  p3vita, como su dibujo (0xFF).
