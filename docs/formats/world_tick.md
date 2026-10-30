# El paso del tiempo (advance_time)

Ingeniería inversa de `Patrician3.exe` (GOG 1.1). Marcas como en
[savegame.md](savegame.md): **[código]** leído en el desensamblado,
**[oráculo]** comparado con el propio código del juego en el emulador.

> Implementación en p3vita: `advance_game_tick` (`src/p3data/simulation.cpp`)
> con el tick de ciudad ([town_tick.md](town_tick.md)), el de los
> comerciantes ([town_tick.md](town_tick.md) §9), los barcos y los convoyes
> ([navigation.md](navigation.md)) y lo de este documento en
> `src/p3data/town_tick/daily.cpp` y `sieges.cpp`. Todo junto es idéntico al
> juego byte a byte partiendo de la partida que lee p3vita (ver
> [../oracle.md](../oracle.md)).

## 1. Un tick **[código]**

`advance_time` (0x530E80, sobre el mundo 0x6DE4A0) sube el tick del mundo
[+0x14] y, con `t = tick & 0xFF`, según `t & 7` (tabla 0x5310A4):

| `t & 7` | Qué |
|-|-|
| 0 | si `t == 0` (empieza el día): la fecha (0x5310D0), lo diario (0x5315C0, semanal si el tick es múltiplo de 0x700), las estadísticas (0x54DE90, sobre 0x6DFC90), las tareas programadas (0x535450, sobre [+0xE8]) y [+0x8B] baja 1 si no es 0 |
| 1 | el tick del comerciante `t >> 3` (0x4F7BB0) |
| 3 | el tick de la ciudad `(t + 0xFF) >> 3` (las 32..39) |
| 5 | el objeto `t >> 3` (< 15) de los 15 del mundo ([+0xB10], 0x52D7D0) |
| 6 | el tick del comerciante `(t + 0x100) >> 3` (los 32..63) |
| 7 | el tick de la ciudad `t >> 3` y su asedio (0x6349F0) |

Después, siempre: la actualización de barcos (0x506720), la cola de eventos
(0x4D85C0, sobre 0x6DD73C), la escena del puerto (0x613DA0) y las batallas
(0x6127C0, sobre 0x6E55D0), que son interfaz, y el asedio de cada ciudad
(0x633A90). Las operaciones encoladas se ejecutan entre dos llamadas a
`advance_time` (que hace hasta 8 ticks); p3vita y el oráculo, al final de
cada tick.

No se reproducen las estadísticas (0x54DE90: listas en el montón con los
valores de cada día, para las gráficas).

## 2. Lo diario (0x5315C0) **[código + oráculo]**

- El día 1 de cada mes, el cierre del mes de cada ciudad (0x52A020): si el
  alcalde [0x6F1] es un comerciante y los gastos de tipo 9 menos los ingresos
  de tipo 3 de este mes y el anterior son positivos, baja su reputación en la
  ciudad (0x4F9220: `importe · 1e-4`, o 10 desde 1 000 000); después los 11
  movimientos del mes [0x704 + 4k] pasan a [0x730 + 4k] y se ponen a 0.
- Cada día, el coste de cada oficina de cada ciudad (0x500820; lista desde
  [0x784] de la ciudad y [0x2CA] de la oficina): [0x2C6] de la ciudad por
  cada edificio terminado [0x2D2] y cada puesto [0x2D4]; se acumula en
  [0x438] de la oficina y en [0x4A4], [0x4A8] y [0x4D4] del comerciante.
- Cada semana, el cierre de cada oficina (0x5006F0: cada total pasa al campo
  siguiente y vuelve a 0; la mitad de [0x438] va a la ciudad como ingreso de
  tipo 0) y el impuesto por cabeza de la ciudad,
  `(3·[0x2D8] + 2·[0x2DC] + [0x2E0]) · u8 [0x6F8] · 10 / 100` (sin signo de
  32 bits), apuntado como movimiento de tipo 1 (0x529F90).

## 3. Las tareas programadas (0x535450) **[código + oráculo]**

Sobre el objeto 0x6DE588 (8 huecos, uno por jugador humano; u8 [+0xA00 + i]
su comerciante y 40 bytes por hueco con un byte por ciudad): guarda la
situación [0x2C8] de cada ciudad en una historia de 16 días
([+0x40·ciudad + 4·(día mod 16)], sin el bit 0x20000 cuando
`(situación & 0x7C0000) ≥ 0x600000`); en cada hueco suma 1 a cada ciudad,
pone a 0 las ciudades donde el comerciante tiene oficina y propaga los
tiempos de viaje (0x535630). Libera los huecos cuyo comerciante ya no es
humano y da uno nuevo (0x0F en todas las ciudades) a cada humano que no
tiene.

## 4. Los 15 objetos del mundo (0x52D7D0) **[código + oráculo]**

Registros de 0x2C bytes en [+0xB10] del mundo (0x6DEFB0; los lee 0x52BBE0):
u8 [+0x5] tipo 0..2. Cada uno, una vez al día: con la suma de los habitantes
de todas las ciudades, `ftol(√suma / {30, 40, 50}[tipo]) · 200`, suben
[+0xC] y [+0x10] (tope 100 000, 200 000 o 0 según el tipo, tabla 0x673990)
y bajan [+0x14] y [+0x18] (sin pasar de 0). Además limpia su lista ([+0x24],
registros de 16 bytes que caducan), vacía en las partidas y que p3vita no
guarda (la llena la cola de eventos, 0x52BFE0).

## 5. Los asedios **[código + oráculo]**

Cada ciudad tiene un objeto de asedio de 0x1024 bytes (*0x6DF270 + 0x1024·i;
`SaveGame::sieges`, con lo que cuelga de punteros en `siege_extras`), que la
partida guarda (0x633730) con un ejército sitiador opcional (0x627AD0, 0x650
bytes). Campos: ver la cabecera de `src/p3data/town_tick/sieges.cpp`.

- **0x6349F0** (diario, tras el tick de la ciudad): lleva la lista de las 4
  mercancías que más faltan en el mundo (0x635220) y los meses que llevan en
  ella, la venta a la ciudad de lo que falta (0x635740, cada [+0x101B]
  semanas), las entregas prometidas ([+0x101C], 0x635440) y los pagos con
  fecha ([+0x1020], 0x636230). La cuenta atrás [+0x1018] baja cada día; a 0,
  si [0x6DE52A] y [0x6DF31C] son 0, encola la operación 0x8E (asedio:
  `{0x8E, ciudad, tipo, rand & 0xFF, 1}`, con el tipo 0, `rand % 2` desde
  1350 o `rand % 3` desde 1400) y vuelve a empezar según la dificultad de los
  asedios (ajustes [0x16]) y la región de la ciudad (`Region` de
  StadtDaten.ini, byte [+0x30] de la tabla 0x6DDB90, indexada aquí con el
  índice de la ciudad en la partida).
- **0x633A90** (cada tick): si hay ejército, lucha contra la ciudad
  (0x6281F0): gasta provisiones y munición, llegan sus grupos (evento 0x26),
  ataca una puerta y los objetivos de [+0x250] (evento 0x25 al caer uno),
  copia las defensas de la ciudad ([0x998 + k]) y gasta su brea [0x40];
  cuando ya no ataca, se retira ([+0x3A]) o cobra un rescate a la ciudad y a
  los comerciantes con oficina en ella (0x633DB0); al irse, la ciudad sube
  [0x300 + 2k] y se borra el ejército.

En las partidas de `missav/` hay 8 ejércitos al cargar (partidas 3, 4, 5 y 7)
y en 60 días se van todos (3 cobran rescate). No se traducen los mensajes ni
los gráficos del asedio.
