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

## Fase 3: esqueleto del motor

- [ ] Plataforma común con SDL2 (ventana, entrada, audio) para PC y Vita
- [x] Render isométrico con cámara (visor de ciudades en `src/app`)
- [x] Entrada táctil y con sticks en la Vita
- [x] Probado en una Vita real: el desplazamiento va fluido

**Hito:** mover la cámara por una ciudad en la Vita. ✔

**Hito:** mover la cámara por Lübeck en la Vita.

## Fase 4: simulación

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
- [ ] Ejecutar las operaciones que encolan las ciudades (obras, materiales, avisos), con el ejecutor del juego (0x535760, [formats/world_tick.md](formats/world_tick.md) §7)
- [x] Tick de cada comerciante (0x4F7BB0): contabilidad, valor de la compañía, reputación y rangos, idéntico al juego
- [x] Lo diario y semanal, tareas programadas, los 15 objetos del mundo y los asedios, idénticos al juego ([formats/world_tick.md](formats/world_tick.md))
- [x] Cola de eventos (0x4D85C0): todos los tipos que salen en las partidas (elecciones, subastas, deudas, encargos, expediciones, piratas, peste, la Hansa...), idénticos al juego ([formats/world_tick.md](formats/world_tick.md) §6)
- [ ] Guardar las cartas y avisos nuevos en el buzón de cada comerciante como el juego (0x4D6530; el buzón ya se lee de la partida, `SaveGame::mail`). Ahora no se guardan, y la simulación lo consulta: el evento 0x16 no repite un aviso que siga vigente (42 días), así que sin esto llega antes de tiempo. Hacerlo junto con las cartas de la interfaz
- [x] El mundo entero (advance_time sin la interfaz) idéntico al juego byte a byte 60 días en las 12 partidas ([oracle.md](oracle.md))
- [ ] **Llegar a Gobernador** (el cargo más alto de la Liga Hanseática, «Alderman» en la versión inglesa; es la meta del juego). El camino entero está en [formats/world_tick.md](formats/world_tick.md) §7 y §8: un alcalde con 120 de reputación pasa a candidato, la Hansa ofrece encargos, con tres cumplidos se presenta a la elección.
  - [x] el ejecutor de las operaciones encoladas (0x535760, tabla 0x537030), en p3vita (`run_operation`) y en el oráculo, con las operaciones 0x00, 0x3B, 0x4B, 0x85 y 0x86; las demás aún se descartan
  - [x] la candidatura: el tick del comerciante (0x4F75F0) pone [+0x37] = 1 y 0x5326E0 (con 0x532E30 y 0x515A60) encola una operación 0x86 y hasta cinco 0x85, una por tipo de encargo; idéntica al juego con estados retocados en las 12 partidas (`tools/oracle/retouch.py`)
  - [x] las operaciones 0x85 (0x53FD10: crea el encargo, un evento 0x32 sin comerciante, si no hay ya uno igual) y 0x86 (quita los encargos sin comerciante que ya no valen), idénticas al juego
  - [x] los encargos sin comerciante (evento 0x32): maduran unos 64 días y se dan a un comerciante de la IA candidato
  - [ ] los encargos con comerciante (evento 0x32, 0x4EB0EB..0x4EBF0F, `alderman.cpp`): el principio (0x4EBF54), la comprobación hasta el plazo y, al cumplirse, [+0x37] + 1 (0x4EC354); con 4 se puede presentar
    - [x] lo común (el principio, el plazo vencido con su castigo de reputación 0x4F9170, el encargo fallido) y los tipos 2 (cazar un barco pirata: se cumple cuando la batalla pone [+0x16] = 1) y 4 (una ciudad pequeña que llegue a N habitantes), idénticos al juego
    - [x] el tipo 1 (carretera entre dos ciudades), con los edificios 0x27 (0x521900 y 0x4FCBD0, solo ese camino); la IA nunca aporta materiales, así que su carretera siempre vence
    - [x] el tipo 3 (guarida pirata), con el refuerzo de los piratas (barcos nuevos, 0x5144A0 y 0x516170), salvo el ataque: [ ] la batalla al llegar un barco a la guarida (0x6130B0, 0x611D80, 0x5FBFE0), que el emulador no ejecuta
    - [ ] el tipo 0 (fundar una ciudad): al empezar crea una ciudad entera en plena partida (0x531D50: carga la plantilla `iso/towns/stadtplatz%.2u.tow`, la inicializa, recalcula las rutas y crea un evento 0x39); se cumple cuando la ciudad tiene mercado; es la misma pieza que «partida nueva desde cero»
  - [x] que el jugador coja un encargo (operación 0x4B, 0x53B440) y aporte materiales a la carretera (operación 0x3B, 0x53A3E0); falta la interfaz que las encola
  - [ ] (opcional) **corregir el fallo del juego en la carretera vencida del jugador**. Ahora se reproduce tal cual, para que p3vita siga siendo idéntico al juego y se pueda comparar con el oráculo; una corrección tendría que ir detrás de un ajuste que el oráculo pueda apagar. El fallo, en 0x4EAFE4 (0x4EB25F): cuando al jugador humano se le vence el encargo de la carretera, el juego crea un encargo igual, ya empezado, y pone como su comerciante [+0xD] la ciudad [+0x10], que es un **índice de ciudad, no de comerciante**, y en sus materiales 0xFF y 0xFFFFFFFF (1023 de cada uno). Consecuencias, si ese índice es menor que el número de comerciantes (lo normal: 24 ciudades y unos 37 comerciantes):
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
