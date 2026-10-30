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
- [ ] Estadísticas de comercio del comerciante y comercio con el kontor
- [x] Ciudades: población, satisfacción, producción de las empresas, consumo, milicia, astillero, iglesia y oficinas de los comerciantes ([formats/town_tick.md](formats/town_tick.md))
- [x] Mapa del mar con las ciudades y los barcos de la partida, con su rumbo ([formats/navigation.md](formats/navigation.md))
- [x] Rutas entre ciudades como las calcula el juego (6633 rutas idénticas en las 12 partidas)
- [x] Movimiento de los barcos tick a tick, salida y llegada a puerto y orden de zarpar, idénticos al juego en 32 días de las 12 partidas ([oracle.md](oracle.md))
- [x] Paso del tiempo al ritmo del juego (234 ms por tick a velocidad normal) y fecha
- [x] Tick de ciudad completo (0x51BA10), idéntico al juego byte a byte: 23 040 ticks partiendo de las 12 partidas, hasta 240 días ([oracle.md](oracle.md))
- [x] Partidas cargadas en memoria igual que el juego (ciudades, oficinas, comerciantes, cola de eventos...)
- [ ] Ejecutar las operaciones que encolan las ciudades (obras, materiales, avisos)
- [ ] Resto de advance_time: tick de los comerciantes, asedios, tareas programadas
- [ ] Barcos de la IA (sus decisiones de viaje), convoyes, astillero y piratas
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
- [ ] Misiones, cartas y eventos
- [ ] Partidas guardadas
- [ ] Vídeos Bink (FFmpeg) y audio MP3
