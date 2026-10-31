# Hoja de ruta

Patrician III no tiene el código fuente publicado, así que esto es una
reimplementación del motor que lee los datos originales del juego (versión GOG
1.1, la misma que documenta [P3Modding](https://p3modding.github.io/)).

Cada fase termina en un hito que se puede comprobar a simple vista.

## Fase 1: entorno y datos

- [x] Repositorio y estructura CMake
- [x] Datos del juego extraídos con `innoextract` (fuera del repositorio)
- [x] VitaSDK instalado en WSL (`scripts/setup-vitasdk.sh`)
- [x] VPK de prueba que lee el CPR desde `ux0:data/p3vita/` y muestra BMP del juego (`scripts/build-vita.sh`)
- [x] Probado en una Vita real (botones, táctil y colores correctos)
- [ ] Icono y LiveArea propios (sin usar arte del juego)
- [x] Lector de archivos CPR (`src/p3data/cpr.*`), herramienta `cprtool` y test
- [x] Formato CPR documentado en [formats/cpr.md](formats/cpr.md)

## Fase 2: herramientas de datos

- [x] BMP, TGA y JPG con stb_image (1100 de 1100 ficheros, inglés y español) y herramienta `p3img`
- [x] Formato AIM documentado ([formats/aim.md](formats/aim.md)) por ingeniería inversa de `AIM.dll`
- [x] Decodificador AIM en C++: 3409 de 3409 ficheros, idéntico a la referencia salvo los 5 JPEG
- [ ] Decidir la semántica de transparencia del motor (magenta, alfa 0 de los `_TP`, sombras con alfa 100)
- [ ] Subir los DXT3 a la Vita como texturas BC2 sin descomprimir
- [x] Mapas de ciudad y tiles ([formats/town.md](formats/town.md)), con tres correcciones a P3Modding
- [x] `p3town`: dibuja cualquier ciudad en un PNG

**Hito:** ver una ciudad dibujada en el PC. ✔

- [x] Ciudades con edificios desde los `.editor` (superconjunto: todas las murallas a la vez)
- [x] Partidas guardadas ([formats/savegame.md](formats/savegame.md)): edificios, objetos fijos y plantilla de cada ciudad
- [x] Ciudades tal como empiezan (`missav/1.pat`, año 1300), con el estilo de su región
- [ ] Partida nueva desde cero: `stadtplatz*.tow` y la generación de las casas (función sin localizar)
- [x] Comerciantes (dinero, nombre, cuál es el jugador), oficinas y barcos (posición, carga, bodega) de las partidas, comprobados con el oráculo
- [ ] Dibujar los kontors (las oficinas ya se leen), andamios de obras y bordes de calle de las celdas 0x81
- [ ] Partidas de la versión 0x4D (`missav/0100–0204.pat`)
- [ ] Variantes de los tiles: sombras, nieve y andamios
- [ ] Agua animada (`WaterSparkleTexID`)
- [ ] **La ciudad viva en pantalla.** La simulación ya cambia la ciudad como el juego (edificios nuevos, obras, murallas, asedios), pero la vista es una imagen fija. Es un trabajo grande, para más adelante:
  - [ ] antes, contrastar con el juego original qué se ve y qué cambia: si la ciudad se redibuja en cuanto se construye algo, qué se anima en tiempo real (gente, carros, agua, humo...), cómo se ve un asedio y si los edificios atacados cambian. Del código solo se sabe que el ejército sitiador tiene objetos gráficos propios y que el juego prepara sus gráficos (0x62C730) si la ciudad sitiada es la que se está viendo
  - [ ] redibujarla mientras pasa el tiempo: ahora se hace al entrar en la ciudad (o al cambiar de vista) y no cambia hasta salir y volver
  - [ ] dibujar desde las listas de edificios actuales de la ciudad ([0x762..0x768] de su memoria) y no desde la copia que se hace al cargar la partida (`SavedTown::drawn_lists`): la simulación mete edificios nuevos en cabeza de lista y esos no salen ni al volver a entrar
  - [ ] el asedio en la vista: el campamento, las tropas y el ataque a la puerta (lo que la simulación salta: 0x62C730 y los objetos del ejército [+0x1F8], [+0x574], [+0x5DC..]) y el aviso al jugador
  - [ ] los habitantes y los carros por las calles y las animaciones de los edificios

## Fase 3: esqueleto del motor

- [ ] Plataforma común con SDL2 (ventana, entrada, audio) para PC y Vita
- [x] Render isométrico con cámara (visor de ciudades en `src/app`)
- [x] Entrada táctil y con sticks en la Vita
- [x] Probado en una Vita real: el desplazamiento va fluido

**Hito:** mover la cámara por una ciudad en la Vita. ✔

**Hito:** mover la cámara por Lübeck en la Vita.

## Fase 4: simulación

- [ ] **Inventario completo de la simulación, sacado del ejecutable** (no de lo que sale en las partidas): hasta ahora se reescribe lo que las partidas y los estados retocados recorren (con todas las ramas de cada función, las que no salen marcadas «Sin comprobar con el oráculo»), pero hay funciones enteras a las que no se llega. Lo que se sabe:
  - [ ] la cola de eventos: la tabla 0x4D8A28 tiene 58 tipos y p3vita atiende 34; los otros 24 tienen manejador en el juego y p3vita los quita sin hacer nada. Quién crea cada uno (`callgraph_eventos.tsv`: 0x4D8CF0 seguido del tipo en [+6], y a mano):
    - **los crea código que p3vita ya tiene** (huecos reales: cuando vencen, p3vita los quita sin hacer lo suyo; no ha salido en las comparaciones porque tardan en vencer): 0x23, 0x24 y 0x34 (las decisiones de la dieta de la Hansa, evento 0x21; los consultan los convoyes y los barcos; 0x4E09D4, unos 7,6 KB, que además encola la 0x81 a los jugadores humanos), 0x25 y 0x26 (el asedio, 0x6281F0: cae un objetivo, llega un grupo; 0x4E1B84 y 0x4E1E54, que llaman a los gráficos de la ciudad 0x599040: separar), 0x27 (los capitanes de la IA que se retiran, evento 0x03; 0x4DDC00), 0x31 (lo aprobado por el consejo, evento 0x2E; 0x4EAB94) y 0x38 (la elección de alcalde, 0x528E60; 0x4ECB14)
    - **los crean operaciones de la simulación aún sin reescribir**: 0x07 (la 0x61; 0x4E23A4), 0x15 (la 0x60; 0x4E7C74), 0x1D (la 0x7D; 0x4E9564) y 0x28 (la 0x13; 0x4DDF40). Las 0x38, 0x7C, 0x7F y 0x83 crean eventos que p3vita ya atiende (0x0A, 0x1C, 0x35 y 0x00)
    - **solo desde el intérprete de misiones, la interfaz o acciones del jugador**: 0x09 (0x4ECF64), 0x0C (la 0x3A), 0x0F (la 0x4F), 0x10 (la 0x53), 0x11 (la 0x3D), 0x13 (la 0x5F), 0x14 (la 0xA4), 0x17 (la 0x72), 0x18 (la 0x2B) y 0x2D (la 0x35)
    - **sin creador encontrado** (el tipo no se escribe como constante tras 0x4D8CF0): 0x0B (0x4E37F4) y 0x29 (0x4DDE70)
  - [ ] las operaciones: la tabla 0x537030 tiene 193 códigos y p3vita ejecuta 22. **Las que encola la simulación** (de las 363 llamadas a 0x54AA70, las de funciones a las que se llega desde advance_time pasando solo por lo que ya tiene p3vita; `re/emu/sesion5/callgraph.py`, con el código leído a mano donde el script no lo saca) y p3vita aún descarta, con quién la encola y su manejador:
    - 0x0A (0x4768B0, barcos; 0x507FB0), 0x13 (evento 0x03, 0x4DCEA0; 0x538C40), 0x28 (la IA, 0x4F5A20; 0x53D190), 0x2C (evento 0x2B, 0x4DE790; 0x52A360 y 0x52B2C0), 0x38 (0x4DD400; un evento), 0x48 (avisos, 0x51E070/0x51E270; 0x53B010), 0x60 (evento 0x16; 0x53DA90), 0x61 (0x5316E0; 0x53DBF0 y el evento 0x07), 0x65 (la IA, 0x4F4E50; 0x4F5D80), 0x67 (0x4DCEA0; 0x53DDF0), 0x7C (0x4DCA90), 0x7D (0x4DCBF0; un evento), 0x7E (evento 0x1F; 0x53F8B0), 0x7F (evento 0x0D, 0x4E3F44; un evento), 0x80 (evento 0x05; 0x4D8DB0), 0x83 (evento 0x00; un evento), 0x84 (eventos 0x1F y 0x03; 0x53FCA0), 0x87 (un hijo, 0x4F69B0; 0x53FEA0), 0x9F (0x50B250, barcos), 0xB5 (0x514000, barcos; 0x514D40), 0xB8 (eventos 0x03, solo en red; 0x542F10)
    - casi todas de unos cientos de bytes a 3 KB; varias crean eventos de los que p3vita no atiende
    - las demás que salen desde advance_time pasan por partes aún sin reescribir: las batallas (0x5FBFE0: 0x92–0xA0, 0xB4), el intérprete de misiones (0x4ECF64: 0x28, 0x7C, 0x7D, 0x82, 0x8E, y por la interfaz 0x60–0x77, 0x8C, 0xA2–0xA8, 0xBF), los gráficos de la ciudad (0x599040, desde el evento 0x25 y la muralla: 0x08, 0x09, 0x0A, 0x2F, 0x39, 0x52, 0x56–0x59, 0x61, 0x93) y 0x5C7810 (de los objetos del mundo: 0x33, 0x34, 0x36, 0x37, 0x98). Hay que mirar si alguna de esas ramas es simulación
  - [ ] el grafo de llamadas estático desde las piezas de advance_time (0x51BA10, 0x4F7BB0, 0x506720, 0x535760, 0x5315C0, 0x535450, 0x52D7D0, 0x6349F0, 0x633A90, 0x4D85C0), contra las funciones que ya tiene p3vita (`fn_XXXXXX`), separando las de interfaz y gráficos
  - [ ] lo que el código ya marca: «Por reescribir», «no se reproduce» y los manejadores que no hacen nada
- [x] Mercancías, almacenes y precios
- [x] Umbrales de precio de cada ciudad (`update_town_price_thresholds`), como al cargar la partida
- [x] Comprar y vender entre un barco y la ciudad, idéntico al juego en 24 000 operaciones ([oracle.md](oracle.md))
- [x] Estadísticas de compra y venta del comerciante y su reputación en la ciudad, idénticas al juego
- [ ] Comercio con el kontor (operación 0x1B)
- [x] Ciudades: población, satisfacción, producción de las empresas, consumo, milicia, astillero, iglesia y oficinas de los comerciantes ([formats/town_tick.md](formats/town_tick.md))
- [x] Mapa del mar con las ciudades y los barcos de la partida, con su rumbo ([formats/navigation.md](formats/navigation.md))
- [x] Rutas entre ciudades como las calcula el juego (6633 rutas idénticas en las 12 partidas)
- [x] Movimiento de los barcos tick a tick, salida y llegada a puerto y orden de zarpar, idénticos al juego en 32 días de las 12 partidas ([oracle.md](oracle.md))
- [x] Paso del tiempo al ritmo del juego (234 ms por tick a velocidad normal) y fecha
- [x] Tick de ciudad completo (0x51BA10), idéntico al juego byte a byte: 23 040 ticks partiendo de las 12 partidas, hasta 240 días ([oracle.md](oracle.md))
- [x] Partidas cargadas en memoria igual que el juego (ciudades, oficinas, comerciantes, cola de eventos...)
- [ ] Ejecutar las operaciones que encolan las ciudades (obras, materiales, avisos), con el ejecutor del juego (0x535760, [formats/world_tick.md](formats/world_tick.md) §7). Hace falta para llegar a Gobernador: la ciudad fundada crece con sus obras hasta tener mercado, que es lo que cumple el encargo de fundar. Medido en 20 días de las 12 partidas (código: veces, quién la encola):
  - [x] **0x22 construir** (169: las casas y edificios de la ciudad 0x51D870/0x51FAB0, muelles y murallas 0x524D5A/0x524E0D/0x5250BA, la IA de los comerciantes 0x4DFA41/0x4DFC63/0x4F4E46). Manejador 0x53CE30; al ejecutarla recorre unas 2400 instrucciones. Por partes:
    - [x] colocar el edificio en la rejilla de la ciudad (`town_tick/grid.cpp`): 0x4FB9C0 entero con 0x4FB8F0, 0x4FBBA0, 0x4FBEC0, 0x4FBFF0, 0x4FC130, 0x4FC1D0, 0x4FC4A0, 0x4FC630, 0x4FC8C0 y 0x4FC990 (huella, bits de tamaños, calles hasta las que hay), y 0x4FCBD0 entero (si el solar vale); idénticos al juego en 30 días de las 12 partidas con el oráculo ejecutando la 0x22 (260 y 137 llamadas)
    - [x] el registro del edificio: 0x521900 entero con 0x5259D0, 0x500A70, 0x4F5450, 0x517770 y la reputación (0x4F89E0, 0x4F8A50, 0x4F8C70, 0x4F8560 sin la carta); idéntico al juego en 30 días de las 12 partidas (137 llamadas)
      - [ ] por reescribir: 0x4FF2D0, lo que hace el almacén nuevo (tipo 0x1E) de un comerciante con oficina (308 instrucciones; no ha salido en 30 días de las 12 partidas)
    - [x] las torres: 0x523830 (tipos 0x30..0x33), idéntica al juego en 30 días de las 12 partidas (35 llamadas), y 0x523D70 (0x34, 0x35, sin salir en las partidas: sin comprobar)
    - [x] el manejador 0x53CE30 y ejecutarla en `run_operation` y en el oráculo (por omisión): el mundo entero con las obras, 0 diferencias en las 12 partidas durante 7 días
  - [x] 0x27 (los materiales que pide la ciudad para sus obras: la ciudad los recibe, 0x535BBC) y 0x23 (empezar la muralla de un lado, 0x535B12 → 0x522B20); con el escenario `fundar` 40 días, idénticas al juego
  - [x] **0x24** (2397 en 20 días; la siguiente pieza de muralla pendiente, [0x766]): 0x522C70 con lo que cuesta (0x51F3A0), la rejilla (0x4FBD60, 0x4FD830 con el relleno de dentro de la muralla 0x4FDA10) y el cierre de la muralla (0x4FACD0 para las piezas, 0x4FDCE0, 0x4FD680, 0x524B70, las deudas de las oficinas como eventos 0x01); idéntica al juego llamada a llamada en 30 días de las 12 partidas (4154 llamadas, con un cierre en la partida 5). **La ciudad fundada no crecía sin ella**: empieza su muralla hacia el día 100 y, mientras tiene piezas pendientes, no hace casas; con la 0x24 la acaba y empieza a crecer (272 habitantes el día 420; las 756 operaciones de esos 420 días, iguales al juego)
    - [ ] por reescribir: 0x4FACD0 para los tipos de menos de 0x32 (quitar edificios de la rejilla), que solo usan llamadores aún no reescritos
  - **Las que faltan cambian el mundo**: con el código del juego, la ciudad fundada sale distinta desde el día 40 ejecutando todas las operaciones que si solo se ejecutan las que reproduce p3vita. Medido cuáles (`re/emu/sesion5/opsdiff.sh PARTIDA DÍAS`: el mundo con las de p3vita y con cada una más, huellas de cada región por día; partidas 1, 3 y 5, 20 días): cambian el mundo la 0x7B (las ciudades desde el día 1-2, en todas), la 0x78 (globales y barcos), la 0x79 y la 0x7A (ciudades), la 0x1E y la 0x1F (convoyes), la 0x12 (capitanes) y la 0x64 (eventos); la 0x8E se para en el emulador (toca la vista de la ciudad). Las demás (0x29, 0x2C, 0x37, 0x38, 0x48, 0x61, 0x67, 0x7D, 0x7E, 0x81, 0x84) no cambian nada en 20 días
  - [x] 0x7B (0x5365E3: los capitanes y administradores que buscan trabajo, 6 bytes en [0x9C6 + 6·k] de la ciudad), 0x79 (0x536535: [0x82C] de hasta 8 ciudades) y 0x7A (0x536570: [0x834..0x83F] de una ciudad), las dos de la dieta de la Hansa (`town_tick/operations.cpp`): el mundo entero 7 días en las 12 partidas, 0 diferencias
  - [x] 0x1E (0x539B50: un convoy sale de expedición hacia el borde oeste) y 0x1F (0x539C60: un punto de su ruta), con la **rama de convoy de la orden de zarpar** (0x537670 en `Sailing::set_sail`: el guía mueve el convoy, los demás salen de él, la vuelta al puerto de todo el convoy, el dinero de una expedición sin empezar): el mundo entero 7 días en las 12 partidas, 0 diferencias (en la partida 2 sale una expedición). Antes, **las expediciones de la IA no salían** (tampoco en el oráculo, que las descartaba)
  - [x] 0x78 (0x5051E0: un barco nuevo en las 16 entradas de [0x6DD7C0], con 0x507D60 y 0x50CE50) y 0x12 (0x538A80: el capitán de un barco mejora sus habilidades, con tope por bits de su índice, y su salario 0x4FE190; sin la carta): el mundo entero 7 días en las 12 partidas, 0 diferencias (la 0x78 sale en 10 de ellas)
  - [x] 0x64 (en 0x5363EE) y el evento 0x33 (0x4EC934): **los robos en las oficinas de los jugadores humanos** con pocos guardias (los encola 0x4DD790 en el evento 0x03): hasta tres mercancías pasan de la oficina al mercado de la ciudad, como mucho lo que hay (la carta 0x73 al jugador no se reproduce). Idénticos al juego con el escenario `robo` de `retouch.py` en las partidas 1 a 7 (lo que hay, más de lo que hay, lo que no hay, el tope de tres, una ciudad sin oficina); el mundo entero 7 días en las 12 partidas, 0 diferencias
  - [x] **0x8E, los asedios** (en 0x536A67): 0x633AF0 crea el ejército sitiador y 0x629C60 lo prepara (tropas según los habitantes y la dificultad, la puerta con menos torres cerca, los grupos que llegan desde los edificios de fuera de la muralla, los plazos de llegada, retirada y rescate; sin muralla, rescate sin ataque) y, si el jugador tiene rango 5 en una ciudad natal de la misma región, le piden ayuda (operación 0x8B). Antes **no había asedios nuevos** en p3vita: solo los de la partida. El oráculo se salta la ventana al jugador y los gráficos de la ciudad que se ve (donde se paraba). Idéntica al juego: los 5 ejércitos de 20 días de las partidas 4 a 7 (con 0, 1 y 2 grupos y objetivos) y sus 18 432 ticks de asedio siguientes, y el escenario `asedio` de `retouch.py` (sin muralla y con la 0x8B) en 6 partidas; el mundo entero 7 días en las 12 partidas, 0 diferencias. Con eso se reproducían todas las que cambiaban el mundo en 20 días de las partidas
  - [x] con la ciudad fundada y más días (`opsdiff_n.sh` con `FOUND=1`, 40 a 200 días) salen otras: 0x29 (0x539DC0: un préstamo, el evento 0x01; los del prestamista de la ciudad a los comerciantes, desde 0x4F4F6B), 0x81 (0x53FA80: un encargo de la dieta de la Hansa, el evento 0x05) y 0x37 (en 0x535E04: el comerciante paga por una ciudad, 0x4F8560). Iguales al juego llamada a llamada en la ciudad fundada (partidas 1 y 3, días 14 a 32 y 28 a 88: 193 y 2591 llamadas) y el mundo entero 7 días en las 12 partidas, 0 diferencias
  - [ ] y faltan: 0x61 (0x53DBF0: crea un evento 0x07, que p3vita no atiende: 0x4E23A4) y 0x2C (0x52A360 sobre la ciudad, con 0x52B2C0). Entran en el inventario completo (abajo)
  - [ ] el resto: 0x7B (3588, el evento 0x19: capitanes y administradores que buscan trabajo), 0x1E y 0x1F (17 y 68, los convoyes al zarpar, 0x503B50), 0x48 avisos (58, 0x51E070), 0x78 (44), 0x79, 0x7A y 0x81 (24, 24 y 4, la dieta de la Hansa), 0x37 (16 en 20 días; 1071 en 520 días de la partida 1, desde 0x4F4B30), 0x64, 0x8E (asedios), 0x12, 0x29, 0x2C, 0x38, 0x61, 0x67, 0x7D, 0x7E, 0x84 (pocas). El emulador las ejecuta todas sin fallar (con `OPS=` en el oráculo)
- [x] Tick de cada comerciante (0x4F7BB0): contabilidad, valor de la compañía, reputación y rangos, idéntico al juego
- [x] Lo diario y semanal, tareas programadas, los 15 objetos del mundo y los asedios, idénticos al juego ([formats/world_tick.md](formats/world_tick.md))
- [x] Cola de eventos (0x4D85C0): todos los tipos que salen en las partidas (elecciones, subastas, deudas, encargos, expediciones, piratas, peste, la Hansa...), idénticos al juego ([formats/world_tick.md](formats/world_tick.md) §6)
- [ ] Guardar las cartas y avisos nuevos en el buzón de cada comerciante como el juego (0x4D6530; el buzón ya se lee de la partida, `SaveGame::mail`). Ahora no se guardan, y la simulación lo consulta: el evento 0x16 no repite un aviso que siga vigente (42 días), así que sin esto llega antes de tiempo. Hacerlo junto con las cartas de la interfaz
- [x] El mundo entero (advance_time sin la interfaz) idéntico al juego byte a byte 60 días en las 12 partidas ([oracle.md](oracle.md))
- [ ] **Llegar a Gobernador** (el cargo más alto de la Liga Hanseática, «Alderman» en la versión inglesa; es la meta del juego). El camino entero está en [formats/world_tick.md](formats/world_tick.md) §7 y §8: un alcalde con 120 de reputación pasa a candidato, la Hansa ofrece encargos, con tres cumplidos se presenta a la elección.
  - [x] el ejecutor de las operaciones encoladas (0x535760, tabla 0x537030), en p3vita (`run_operation`) y en el oráculo, con las operaciones 0x00, 0x12, 0x1E, 0x1F, 0x22, 0x23, 0x24, 0x27, 0x29, 0x37, 0x3B, 0x46, 0x4B, 0x64, 0x78, 0x79, 0x7A, 0x7B, 0x81, 0x85, 0x86 y 0x8E; las demás aún se descartan (la 0x8B, la petición de ayuda en un asedio, es de la interfaz)
  - [x] la candidatura: el tick del comerciante (0x4F75F0) pone [+0x37] = 1 y 0x5326E0 (con 0x532E30 y 0x515A60) encola una operación 0x86 y hasta cinco 0x85, una por tipo de encargo; idéntica al juego con estados retocados en las 12 partidas (`tools/oracle/retouch.py`)
  - [x] las operaciones 0x85 (0x53FD10: crea el encargo, un evento 0x32 sin comerciante, si no hay ya uno igual) y 0x86 (quita los encargos sin comerciante que ya no valen), idénticas al juego
  - [x] los encargos sin comerciante (evento 0x32): maduran unos 64 días y se dan a un comerciante de la IA candidato
  - [ ] los encargos con comerciante (evento 0x32, 0x4EB0EB..0x4EBF0F, `alderman.cpp`): el principio (0x4EBF54), la comprobación hasta el plazo y, al cumplirse, [+0x37] + 1 (0x4EC354); con 4 se puede presentar
    - [x] lo común (el principio, el plazo vencido con su castigo de reputación 0x4F9170, el encargo fallido) y los tipos 2 (cazar un barco pirata: se cumple cuando la batalla pone [+0x16] = 1) y 4 (una ciudad pequeña que llegue a N habitantes), idénticos al juego
    - [x] el tipo 1 (carretera entre dos ciudades), con los edificios 0x27 (0x521900 y 0x4FCBD0, solo ese camino); la IA nunca aporta materiales, así que su carretera siempre vence
    - [x] el tipo 3 (guarida pirata), con el refuerzo de los piratas (barcos nuevos, 0x5144A0 y 0x516170), salvo el ataque: [ ] la batalla al llegar un barco a la guarida (0x6130B0, 0x611D80, 0x5FBFE0), que el emulador no ejecuta
    - [x] el tipo 0 (fundar una ciudad, `new_town.cpp`): al empezar crea una ciudad entera en plena partida (0x531D50: la plantilla `iso/towns/stadtplatz%.2u.tow`, el constructor, los candidatos, los capitanes, el astillero, las rutas y la matriz de tiempos de viaje) y su muralla (evento 0x39), idénticos al juego; la ciudad nueva sigue igual que en el juego los días siguientes
    - [x] el desarrollo de la ciudad nueva (evento 0x2F, 0x4EA0D4, a los 180 días de fundarla): con su primer edificio municipal, la oficina de su alcalde si es de la IA o, sin alcalde, la operación 0x46 (0x535FD6) que hace alcalde al comerciante de la IA con más rango; después, con 1000 habitantes y según su muralla, le da el mercado, y con muchos pobres encarga un prestamista; idéntico al juego con estados retocados dirigidos a cada rama ([oracle.md](oracle.md), «Los encargos de la Hansa»)
    - [ ] que se cumpla el de fundar: se cumple cuando la ciudad tiene mercado, y eso solo lo da el evento 0x2F cuando la ciudad tiene su primer edificio municipal y pasa de 1000 habitantes. p3vita lo hace todo como el juego hasta el día 420 (edificios municipales hacia el día 40, muralla, y empieza a crecer: 272 habitantes). Medido con el código del juego en el emulador (`grow.sh`, partida 1): la ciudad se queda en 272 habitantes hasta el día 520 y sin edificios nuevos, también ejecutando todas las operaciones (empieza una segunda muralla que se atasca sin herramientas); el plazo del encargo es de unos dos años, así que en el emulador no parece llegar a cumplirse. Pendiente: medir más allá de 550 días (no cabe en 10 minutos en primer plano) y ver si en el juego de verdad crece porque alguien le lleva mercancías
    - [ ] la ciudad nueva en el visor de la Vita (sin probar)
    - [ ] por reescribir dentro de la fundación (`new_town.cpp`, marcadas «Por reescribir»):
      - [x] 0x5462F0: el nodo de una instalación en la lista [0x9B4] de la ciudad (0x50FCA0, 0x50FE00). Era la única a la que se llega al fundar: cuando el aceite (mercancía 0xA) es de las que más faltan al elegir el sitio (0x532E30 añade el bit 0x40000); idéntica al juego con el escenario `fundar_aceite` (la fundación y los 4 días siguientes)
      - [ ] 0x545CE0 y 0x545F50: preparar una ciudad que ya existe (sus edificios y su mercado); al fundar no se llega ([+0xF] = 0 en el registro)
      - [ ] 0x4F99B0: la ciudad sin plantilla de mapa; las 28 plantillas existen
      - [ ] la elección del alcalde dentro de 0x531D50 (0x532063..0x5321C5, con el bit 0x40 de la situación); al fundar la situación es 0
  - [x] que el jugador coja un encargo (operación 0x4B, 0x53B440) y aporte materiales a la carretera (operación 0x3B, 0x53A3E0)
  - [ ] la interfaz que encola esas dos operaciones en el visor: ver los encargos de la Hansa sin dar, coger uno (0x4B) y aportar materiales a la carretera desde la oficina o los barcos (0x3B)
  - [ ] (opcional) **corregir el fallo del juego en la carretera vencida del jugador**. Ahora se reproduce tal cual, para que p3vita siga siendo idéntico al juego y se pueda comparar con el oráculo; la corrección iría detrás del ajuste de «Corregir los fallos del juego» (abajo). El fallo, en 0x4EAFE4 (0x4EB25F): cuando al jugador humano se le vence el encargo de la carretera, el juego crea un encargo igual, ya empezado, y pone como su comerciante [+0xD] la ciudad [+0x10], que es un **índice de ciudad, no de comerciante**, y en sus materiales 0xFF y 0xFFFFFFFF (1023 de cada uno). Consecuencias, si ese índice es menor que el número de comerciantes (lo normal: 24 ciudades y unos 37 comerciantes):
    - ese encargo no vence nunca: los tramos se hacen solos cada día y medio, y cerca del plazo siempre tiene materiales para lo que falta, así que el plazo se alarga;
    - la ciudad acaba con la carretera construida (los edificios 0x27 en las dos ciudades) sin que nadie haya aportado nada;
    - al cumplirse, 0x4EC354 sube el [+0x37] del comerciante que tenga ese índice: un comerciante cualquiera avanza en la candidatura a Gobernador sin haber hecho el encargo (y, si es el jugador, le cuenta un encargo que no hizo); si ese comerciante no es candidato, pasa de 0 a 1 sin haber sido alcalde;
    - si el índice no es menor que el número de comerciantes, el encargo cuenta como sin dar y se quita enseguida (las dos ciudades ya tienen el bit 0x8000).
    Corrección posible: no crear la copia, o crearla sin comerciante (0xFF). Otras rarezas del mismo encargo, también reproducidas: el plazo inicial (0x5326E0) cuenta los días como meses y lee la tabla de los meses (0x672D78) fuera de sus límites, hasta la entrada 129; y la operación 0x3B con un comerciante que no existe toma por cantidad que falta un puntero (esa no se reproduce: p3vita no hace nada).
  - [x] la elección en la reunión de la Hansa (evento 0x20), idéntica al juego
  - [ ] el final: la ventana y el vídeo (`eldermann`) cuando el jugador sale elegido; el objetivo de la partida (evento 0x0E) ya se comprueba
  - Las partidas de prueba no llegan a esto en 60 días (ningún alcalde comerciante llega a 120): se comprueba con estados retocados, unos pocos dirigidos a cada rama
- [x] Barcos de la IA en puerto: comercio con las ciudades y sus oficinas, elección de destino, tripulación, armas, reparación en el astillero y creación de convoyes, idénticos al juego ([formats/navigation.md](formats/navigation.md) §8)
- [x] Barcos como memoria del juego; navegación idéntica byte a byte (avisos, lista por x, contadores)
- [x] Convoyes de los comerciantes: en puerto, saliendo, en el mar, entrando, deshaciéndose, idénticos al juego 60 días en las 12 partidas ([formats/navigation.md](formats/navigation.md) §9)
- [ ] Piratas, persecuciones, encuentros y batallas
- [ ] (opcional) **Corregir los fallos del juego** detrás de un ajuste (feature flag) **activado por omisión**: con él, p3vita corrige los fallos que vayamos encontrando; apagado, se comporta como el original, para jugar a la versión original y para las comprobaciones con el oráculo (que comparan con el juego tal cual). Ahora todos se reproducen tal cual. Cada fallo que aparezca se apunta aquí:
  - [ ] la carretera vencida del jugador (0x4EAFE4; detalle y consecuencias en «Llegar a Gobernador», arriba)
  - [ ] las mejoras de las piezas de muralla (la operación 0x22 de los tipos 0x34 y 0x35, 0x523D70, `construction.cpp`). Comparada con la de las torres (0x523830), que hace bien las cuentas: (1) las herramientas se sacan de la oficina del comerciante con la cantidad de **madera** que falta (baja el contador de la madera, y la ciudad pierde de su almacén todas las herramientas y las paga enteras); (2) lo que se le paga por ellas es 1,278 · 0 (multiplica por la constante 0x66BC6C, que vale 0, en lugar de por lo sacado); (3) al comerciante no se le paga nada de lo que sale de su oficina, aunque la ciudad lo apunta como gasto (0x529F90). Los dos primeros son fallos casi seguro; el tercero lo parece. Sin comprobar con el oráculo: no sale en 30 días de las 12 partidas
  - [ ] los del tick de ciudad ([formats/town_tick.md](formats/town_tick.md) §8): el administrador de 0x51C360 solo cuenta la primera oficina; 0x5109B0 deja negativo [+0xC]; la granja de grano compara al revés al contratar; 0x524920 escribe fuera de la rejilla con un muelle en la última columna
  - [ ] el ejército sitiador nuevo (0x629C60): no mira cuántos grupos caben (más de 0x7F se sale del ejército), si no encuentra las tres puertas lee el registro de la dirección 0 (en el juego, un cuelgue), y al sumar la fuerza de los objetivos cuenta las torres cerca de la puerta en obras aunque 0x62B2B0 no las haya puesto de objetivo (lee entradas sin llenar). No pasa en las partidas; p3vita lee un registro a 0 y no escribe fuera
- [ ] Tests que comparen con el comportamiento documentado del original

## Fase 5: interfaz

- [x] Archivos por idioma montados como el juego: el español encima del inglés ([formats/fonts.md](formats/fonts.md))
- [x] Texto con las fuentes TrueType del juego (Latin-1, con ñ) y nombres de ciudad en el idioma de los datos
- [x] Barra superior en el visor con ciudad, año y habitantes
- [x] Mercado: existencias, producción y consumo diarios de cada ciudad (de la partida)
- [x] Precios de compra y venta con los umbrales guardados en la partida
- [x] Precios idénticos al juego, comprobados con el oráculo (su propio código en un emulador de CPU, [oracle.md](oracle.md))
- [x] Mercado interactivo en la Vita: elegir mercancía y cantidad, comprar y vender con el barco del jugador
- [x] Dificultad de comercio de la partida
- [x] Mapa del mar: mandar el barco del jugador a una ciudad, verlo navegar con su ruta y comerciar al llegar
- [ ] Rehacer la interfaz para 960×544 (16:9) con soporte táctil

## Fase 6: resto del juego

- [ ] Batallas navales
- [ ] Comerciantes de la IA y rutas automáticas
- [ ] Misiones, cartas y eventos (las cartas, con el buzón de la fase 4)
- [ ] Partidas guardadas
- [ ] Vídeos Bink (FFmpeg) y audio MP3
