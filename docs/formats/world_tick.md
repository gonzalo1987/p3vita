# El paso del tiempo (advance_time)

Ingeniería inversa de `Patrician3.exe` (GOG 1.1). Marcas como en
[savegame.md](savegame.md): **[código]** leído en el desensamblado,
**[oráculo]** comparado con el propio código del juego en el emulador.

> Implementación en p3vita: `advance_game_tick` (`src/p3data/simulation.cpp`)
> con el tick de ciudad ([town_tick.md](town_tick.md)), el de los
> comerciantes ([town_tick.md](town_tick.md) §9), los barcos y los convoyes
> ([navigation.md](navigation.md)) y lo de este documento en
> `src/p3data/town_tick/daily.cpp`, `sieges.cpp` y `events*.cpp`. Todo junto
> es idéntico al juego byte a byte partiendo de la partida que lee p3vita, 60
> días en las 12 partidas (ver [../oracle.md](../oracle.md)).

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
los sitios fuera del mapa a los que van las expediciones (evento 0x12, §6).
u8 [+0x0], [+0x1] las mercancías que venden (0xFF ninguna), u8 [+0x2],
[+0x3] las que compran, u8 [+0x4] su sitio (tabla 0x673858), u8 [+0x5] tipo
0..2, u8 [+0x6] el comerciante que lo encontró (0xFF nadie), u32 [+0x8] tick
desde el que hay que llegar a su sitio exacto (0xFFFFFFFF: basta con
acercarse), [+0xC] y [+0x10] lo que les queda de lo que venden, [+0x14] y
[+0x18] lo que tienen de lo que compran, y su lista de encargos ([+0x24], con
su número en el u16 [+0x1C] y en el u32 [+0x28]; `SaveGame::world_object_lists`,
región `Fi` del oráculo): registros de 16 bytes (u16 comerciante, u8 cuál de
sus dos mercancías, u8 0 si el objeto vende o 1 si compra, u32 cantidad en
barriles, u32 precio, u32 tick en que caduca) que crea la llegada de una
expedición (0x52C6C0); vacía en las partidas. Cada uno, una vez al día: con
la suma de los habitantes de todas las ciudades, `ftol(√suma / {30, 40,
50}[tipo]) · 200`, suben [+0xC] y [+0x10] (tope 100 000, 200 000 o 0 según el
tipo, tabla 0x673990) y bajan [+0x14] y [+0x18] (sin pasar de 0). Además
quita de su lista los encargos caducados o sin cantidad.

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

## 6. La cola de eventos (0x4D85C0) **[código + oráculo]**

Objeto 0x6DD73C (`SaveGame::events`; la disposición, en la cabecera de
`src/p3data/town_tick/events.cpp`): registros de 0x18 bytes enlazados en
orden de tick. Tras los barcos, 0x4D85C0 atiende los que han vencido; cada
tipo tiene su manejador (tabla 0x4D8A28) y, después, el despachador lo
quita (vuelve a la lista de libres), le cambia el tick y lo recoloca
(0x4D8DD0) o decide por lo que devuelve el manejador. 0x4D8CF0 toma un
registro libre para un evento nuevo (si no queda, reserva 0x80 más).

| Tipo | Manejador | Qué |
|-|-|-|
| 0x00 | 0x4DB330 | grano |
| 0x01 | 0x4DB4E0 | deuda de un comerciante con otro o con la ciudad: cada 10 días cobra lo que puede (0x4F6570: dinero, mercancía de su oficina o de sus barcos) |
| 0x02 | 0x4DBB00 | la elección del alcalde de una ciudad (0x528E60), una vez al año |
| 0x03 | 0x4DDA40 | cada 10 días: capitanes que se retiran, mercancía en camino y operaciones para el juego (especialidades, rumores, plagas, incendios, fiestas, torneos, robos...) |
| 0x04 | 0x4DFC94 | la IA de cada comerciante |
| 0x05 | 0x4E5B84 | encargos a un comerciante; se convierte en un 0x01 (la paga) o un 0x37 (castigo) |
| 0x06, 0x08 | 0x4E2144, 0x4E2634 | puertos; capitanes en las tabernas |
| 0x0A, 0x36 | 0x4E2CD4 | subastas de barcos y de edificios, con las pujas de la IA |
| 0x0D | 0x4E4984 | estación, hielo y tormentas |
| 0x0E | 0x4E4A44 | el final de la partida (objetivo y plazo) |
| 0x12 | 0x4E6A54 | un convoy a puntos fuera del mapa (expediciones): la salida y la llegada (ver abajo) |
| 0x16 | 0x4E8234 | un aviso a cada jugador humano, salvo que tenga uno vigente en el buzón (0x6DD730) |
| 0x19, 0x1A | 0x4E8E24, 0x4F6C10 | capitanes y administradores que buscan trabajo; reputación entre los marineros |
| 0x1C | 0x4E9094 | la peste |
| 0x1E, 0x1F | 0x4DFD44, 0x4E0334 | la temporada de piratas (marzo a octubre) y los piratas ante cada ciudad; sin el ataque a un barco (0x5063C0) |
| 0x20, 0x21 | 0x4DBBD0, 0x4DC1F0 | la elección del alderman y las propuestas de la dieta de la Hansa, un voto por tick |
| 0x22 | 0x4DB7C0 | pedidos de mercancía |
| 0x2A, 0x2B | 0x4DE4F0, 0x4DE790 | un comerciante de la IA abre una oficina o encarga un barco |
| 0x2C | 0x4DEA20, 0x4DEBA0 | la IA elige qué empresa construir (operación 0x22) |
| 0x2E | 0x4E9A94 | la votación del consejo de una ciudad (con la muralla, 0x522B20) |
| 0x30, 0x32 | 0x4EA594, 0x4EAFE4 | encargos del jefe de la Hansa; sin el reparto a un comerciante que ya lo tiene |
| 0x35, 0x37 | | quitan un bit de la situación de la ciudad; dejan volver a comerciar |

**Las expediciones (0x12).** El convoy que sale del mapa por el oeste
(0x5029F0) deja un evento 0x12; el manejador calcula primero cuándo vuelve
(la distancia por zonas de su ruta, 0x52D950 y 0x52DB50, · 128 / su
velocidad, y la mitad más con el [+0x38] del convoy a 0, el de la IA). Al
llegar, en cada punto de la ruta ([+0x24], hasta 4): el dueño marca el punto
más cercano de `Expeditionen.dat` (0x4F93C0, en su [+0x4D8] y, si su
[+0x468] no es 0, en 0x6DF278; la primera vez que se llega a uno de los 100
primeros, 0x5319D0: +2560 de reputación y [+0x89] del mundo a 1) y el primero
de los 15 objetos que esté ahí (0x52BE90: cerca, a menos de 7 casillas, 5 sin
[+0x38] a 0, o en el sitio exacto si ya se ha llegado) se marca (0x4F9400), se
encuentra si nadie lo había hecho (0x52D0B0: +384 de reputación y la de cada
ciudad) y se comercia (0x52BFE0): los barcos le venden sus dos mercancías
(precio de 0x673A18 entre 1,5 y 1,2 veces según lo lleno, 0x52D4F0), atienden
los encargos (0x52C6C0) y compran las suyas con lo que les cabe según los bits
del punto ([+0x34 + k]) y el dinero del convoy ([+0x20], 0x52D6B0). Después el
casco baja 640 por día de viaje y, con el generador del juego sembrado con el
tick (0x46C320..0x46C380), 1 de cada 11 veces piratas (del 30 al 60 % del
casco menos lo que defiende la artillería media, y a veces se llevan dinero)
y 1 de cada 11 una tormenta. Al final 0x502B20: los barcos con casco vuelven a
(0x76, 0x640) navegando hacia la ciudad de origen, los demás se hunden, el
dueño cobra el [+0x20] y gana 51 de reputación.

La cola lee también lo que la partida guarda y p3vita no leía: el buzón de
avisos (0x4D6350, justo tras los barcos: `SaveGame::mail`), los 5 objetos de
los piratas (0x5138B0, al final de los barcos: `SaveGame::pirates`) y los
nombres de barco de `scripts/NamenSchiffe*.txt` (0x5128D0,
`SaveGame::ship_names`). Los avisos nuevos (0x4D6530) no se apuntan en el
buzón: en el juego, un aviso 0x21 recién enviado aplazaría el siguiente 0x16
42 días.
