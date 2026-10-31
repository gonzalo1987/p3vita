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
- **La operación 0x8E** (en 0x536A67, `fn_536a67`): 0x633AF0 sobre el asedio
  de la ciudad. Si no tiene ejército, tiene el bit 0x100 de [0x76C] y tres
  puertas o torres de muralla, lo crea (0x627880) y lo prepara (0x629C60):
  tropas de cada tipo según ((dificultad + 2) · 765 + 2r) · habitantes / 2550
  (tablas 0x67B5C8 y 0x67B604), los defensores de [0x998 + k]; sin muralla
  (0x5252F0 da 0xFF) no ataca y cobra el rescate; con ella, de las tres
  primeras puertas del lado, la de menos torres cerca (sus objetivos,
  0x62B2B0, y su posición, 0x632780), los grupos que llegan (los edificios de
  las oficinas fuera de la muralla más cerca de esa puerta, 0x525220, menos
  algunos según los ricos y ordenados por dirección) y, de la longitud del
  camino desde la esquina del mapa más cercana (raíz + 0,5), la llegada
  ([+0x42], [+0x44], [+0x254 + 8·k]), la retirada [+0x6A] y el rescate
  [+0x68]. La ciudad pasa a la situación 0x10. Si el jugador tiene rango 5 en
  su ciudad natal, de la misma región, le piden ayuda (operación 0x8B). No se
  reproducen la ventana al jugador, la carta 0x53 ni los gráficos.
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
| 0x2F, 0x39 | 0x4EA0D4, 0x4ECDF4 | una ciudad fundada en plena partida: su desarrollo (alcalde, oficina, mercado, prestamista) y su muralla (§8) |
| 0x30, 0x32 | 0x4EA594, 0x4EAFE4 | encargos del jefe de la Hansa y de la Hansa a los candidatos a Gobernador (§8) |
| 0x25, 0x26 | 0x4E1B84, 0x4E1E54 | los incendios del asedio (los crea 0x6281F0: cae un objetivo, con su celda en [+0xC], [+0xE]; llega un grupo, con su edificio en [+0x8]; [+0x10] la ciudad, [+0x11] 1 la primera vez). La primera vez el edificio pasa de su lista (murallas [0x768]; de las oficinas [+0x2CE]/[+0x2D0] o de la ciudad [0x764], y fuera de las subastas, 0x4DB100) a la de lo que arde [0x76A] con 100 de vida ([+0xA], menos si estaba en obras: −50 · obras / días de obra); cada 5 ticks pierde 51 (1 desde 50) y se ve en sus obras; al acabar, una torre (0x32, 0x33) queda en ruinas (0x36, otra vez en [0x768]) y lo demás se borra (0x521240: a la lista de libres [0x760], fuera de la rejilla con 0x4FACD0). Con la ciudad 0xFE − k, la guarida pirata k (0x515C00, desde las batallas) |
| 0x27 | 0x4DDC00 | un capitán de la IA que se retira (lo crea 0x4DCEA0, evento 0x03): según el estado del barco ([+0x134]) deja el barco (0x5098B0: vuelve a la lista de libres [0x6DD880]; si guía un convoy en puerto, lo deshace antes, 0x50A580; en el astillero, el barco pasa al estado 0 y se libera el astillero de la ciudad, [0x990] y [0x994]) o espera (0x20, 0x40 o 0x41 − [+0x138] ticks). Si el capitán ya no está en el barco, el juego lo busca en el primer barco de cada comerciante y, si no es ese, se cuelga (bucle sin fin en 0x4DDCB0) |
| 0x31 | 0x4EAB94 | el impuesto extraordinario de la ciudad [+0xC], de [+0x8] (lo crea el 0x2E al aprobarse la propuesta 0): la ciudad lo cobra (0x529F90, ingreso 2) y lo pagan los comerciantes con oficina en ella, la mitad a partes iguales y la otra según la oficina ([+0x2D2] + [+0x2D4] + 1); lo que no pueden, deudas con la ciudad (eventos 0x01 a 7 días, de 250000 como mucho). Si paga alguien que no es de la ciudad ni su alcalde y la ciudad tiene el bit 0x100 de [0x76C], una queja a la Hansa (evento 0x30). Se quita siempre |
| 0x38 | 0x4ECB14 | el monumento al alcalde reelegido [+0x9] de la ciudad [+0x8] (lo crea 0x528E60): el primer edificio 0x2D de la lista [0x764], si es de la ciudad, pasa a ser suyo con la variante (tick + ciudad + comerciante) % 3 (3 más con rango > 7), cuando la muralla está; antes se aplaza 0x3E00 ticks y un poco más |
| 0x33 | 0x4EC934 | un robo en una oficina (lo crea la operación 0x64, §7): [+0x8 + k] mercancías (k = 0..5, >= 0x18 ninguna), [+0xE] comerciante, [+0xF] ciudad, [+0x10 + k] cantidades en cargas o barriles; de las que tiene la oficina se lleva hasta tres, como mucho lo que hay (y entonces [+0x10 + k] pasa a eso), al mercado de la ciudad ([+0x4 + 4w]); sin oficina, nada. Se quita siempre. La carta al comerciante (0x4D6530, tipo 0x73) no se reproduce |
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

## 7. Las operaciones encoladas **[código]**

Una operación son 0x14 bytes: u32 código y 16 bytes de datos. Las encola
0x54AA70 sobre el objeto 0x6DF2F0 (copia los 0x14 bytes con 0x543F10 en su
cola de [+0x48]; en partida en red, [+0x0] & 0xC, espera a que haya sitio) y
el juego las ejecuta entre dos llamadas a `advance_time`, en el orden en que
se encolaron, con **0x535760** (thiscall sobre la operación; lo llaman 0x544EDC
y 0x544FA6, y la interfaz desde 0x41B57B...): un salto por el código (0 a 0xC0,
tabla 0x537030) a unos 180 manejadores; los códigos 0x14, 0x73, 0x9C–0x9E,
0xA1, 0xB9–0xBC y 0xBE no hacen nada. Algunos:

| Código | Manejador | Qué |
|-|-|-|
| 0x00 | 0x537670 | zarpar ([navigation.md](navigation.md)) |
| 0x01, 0x02 | 0x537340, 0x537450 | vender, comprar |
| 0x22 | 0x53CE30 | construir: {0x22, u8 dueño, u8 ciudad, x, y, tipo, parámetro}; hasta 0x2F, 0x521900 (con la rejilla, `town_tick/grid.cpp`); 0x30..0x33, las torres (0x523830); 0x34 y 0x35, 0x523D70; 0x36..0x39, la muralla (0x522B20). La 0x21 (interfaz) es lo mismo con otra búsqueda del tipo |
| 0x23 | 0x535B12 | {0x23, u32 lado, u32 ciudad}: empieza la muralla de ese lado (0x522B20) |
| 0x24 | 0x522C70 | {0x24, u32 dueño, u32 ciudad, u32 clase (0 tramos, 1 las demás)}: la siguiente pieza de muralla pendiente ([0x766]) que se pueda pagar (0x51F3A0; el dueño, un comerciante con oficina en la ciudad) pasa a las obras; con la última, el cierre de la muralla (`construction.cpp`, `grid.cpp`) |
| 0x27 | 0x535BBC | {0x27, u32 ciudad (con el bit 31, también cáñamo), ladrillos, madera, herramientas}: la ciudad los recibe; la piden sus obras cuando le faltan |
| 0x3B | 0x53A3E0 | la carretera de un encargo de la Hansa (tipo 1, §8) desde la interfaz |
| 0x1E, 0x1F | 0x539B50, 0x539C60 | un convoy sale de expedición (su guía zarpa hacia (0x76, 0x640), lleva el dinero pedido del dueño) y un punto de su ruta (`operations.cpp`; la orden de zarpar con convoy, en `sailing.cpp`) |
| 0x46 | dentro de 0x535760, en 0x535FD6 | alcalde para una ciudad sin él: {0x46, comerciante, ciudad, -, marca}; si tiene rango ≥ 5 en su ciudad natal o con la marca 0xBADEAFFE en [+0x10] (la encola el evento 0x2F, §8; también la interfaz, desde 0x41AD92 y 0x41B554) |
| 0x4B | 0x53B440 | un candidato coge un encargo de la Hansa (§8) |
| 0x64 | dentro de 0x535760, en 0x5363EE | un robo: un evento 0x33 (§6) a tick + [+0x12] con los 16 bytes de la operación (`operations.cpp`). La encola 0x4DD790 desde el evento 0x03: en cada oficina de un jugador humano con menos guardias ([+0x2C7]) que su nivel y más de 50 cargas, a veces, con hasta 6 mercancías y un plazo de 100 a 2147 ticks |
| 0x12 | 0x538A80 | {0x12, u32 barco, u8 a, …, u8 b}: el capitán del barco suma a a [+9] y b a [+0xA] y [+0xB], con tope por los bits de su índice (0x673B34: 250, 200, 250, 150), y recalcula su salario (0x4FE190) |
| 0x78 | 0x5051E0 (sobre los barcos) | un barco nuevo (0x507D60 con 4) en el primer hueco de las 16 entradas de 12 bytes de [0x6DD7C0], en (x, y) y en la lista por x |
| 0x79 | en 0x536535 | la dieta de la Hansa: ocho parejas (ciudad, valor) → [0x82C] de cada ciudad |
| 0x7A | en 0x536570 | la dieta de la Hansa: {ciudad A, ciudad B, u32, u16} → [0x834..0x83F] de la ciudad A, si no tiene comerciante en [0x83F] |
| 0x7B | en 0x5365E3 | {0x7B, 6 bytes, …, [+0x10] ciudad | k << 8}: el capitán o administrador k (0..5) que busca trabajo en la ciudad, [0x9C6 + 6·k] (evento 0x19) |
| 0x29 | 0x539DC0 | un préstamo: {0x29, u32, u8, 3 bytes, u16 cantidad, u8 semanas, u8, u8 el que presta (>= comerciantes: el prestamista de la ciudad), u8 el que pide, u8 ciudad, u8 tipo y hueco}: el evento 0x01 dentro de las semanas, el dinero al que pide y, si presta un comerciante, se le resta y gana reputación (0x4F8FE0); un hueco < 6 vacía el de la taberna [0x9CA + 6·k] (`operations.cpp`) |
| 0x37 | dentro de 0x535760, en 0x535E04 | {0x37, comerciante, ciudad, cantidad}: 0x4F8560, el comerciante paga por la ciudad (la primera vez) |
| 0x61 | 0x53DBF0 | {0x61, comerciante, tick, u32, precio}: un evento 0x07 en ese tick, si el comerciante tiene el precio, que se le cobra (sin reescribir: p3vita no atiende el evento 0x07) |
| 0x81 | 0x53FA80 | {0x81, comerciante, tipo, plazo, u8}: un encargo de la dieta de la Hansa, el evento 0x05 a tick + plazo + 256 con el byte bajo a 0x80 |
| 0x85 | 0x53FD10 | crea un encargo de la Hansa (§8) |
| 0x8E | dentro de 0x535760, en 0x536A67 | un asedio: {0x8E, u32 ciudad, u32 tipo de ejército, u8 r, …, u32 flag} → 0x633AF0 (§5, `sieges.cpp`) |
| 0x86 | dentro de 0x535760, en 0x536801 (copia en 0x53FE20) | quita los encargos de la Hansa sin comerciante que ya no valen (§8) |

El u32 [0x6DF31C] ([+0x2C] del objeto 0x6DF2F0) vale 0 en las partidas; si
no, no hay candidatura (§8) y muchos eventos se saltan partes (`kAldermanBusy`
en el código).

p3vita guarda las operaciones en `SaveGame::queued_operations` y las ejecuta
al final de cada tick, en su orden (`run_queued_operations` y el ejecutor
`run_operation`, `simulation.cpp`): de momento la 0x00, la 0x12, la 0x1E, la 0x1F, la 0x22, la 0x23, la 0x24, la 0x27, la 0x29, la 0x37, la 0x3B, la 0x46, la 0x4B, la 0x64, la 0x78, la 0x79, la 0x7A, la 0x7B, la 0x81, la 0x85, la 0x86 y la 0x8E; las
demás se descartan. El oráculo del mundo hace lo mismo con el ejecutor del
juego ([../oracle.md](../oracle.md), «Las operaciones encoladas»). Lo que encolan las ciudades y la cola de eventos en 200
días de la partida 1 (con un candidato retocado): 0x22 ×1924, 0x7B ×1704,
0x1F ×360, 0x1E ×90, 0x78 ×38, 0x79 ×20, 0x7A ×20, 0x61 ×5, 0x81 ×4, 0x37 ×3.

## 8. Gobernador: la candidatura y los encargos de la Hansa **[código + oráculo]**

Gobernador («Alderman» en la versión inglesa) es el cargo más alto de la
Hansa y la meta del juego. El camino: un alcalde con 120 de reputación pasa a
candidato; la Hansa ofrece encargos (eventos 0x32); cada encargo cumplido
sube el [+0x37] del comerciante y con 4 se presenta a la elección (evento
0x20, §6, 0x529700). Lo medido aquí se hizo con el código del juego en el
emulador (`re/emu/cand_cov.py`, fuera del repositorio: estados retocados
sobre el oráculo del mundo).

> En p3vita (`src/p3data/town_tick/alderman.cpp`): la candidatura, las
> operaciones 0x85, 0x86, 0x4B y 0x3B y el evento 0x32 con comerciante
> (todos los tipos; fundar una ciudad en `new_town.cpp`), idénticos al juego
> con estados retocados ([../oracle.md](../oracle.md), «Las operaciones
> encoladas» y «Los encargos de la Hansa»), y el desarrollo de la ciudad
> nueva (evento 0x2F y operación 0x46, también en `new_town.cpp`). Falta el
> ataque a la guarida del tipo 3 (una batalla) y, para que se cumpla el de
> fundar, que la ciudad crezca con sus obras (abajo). Por reescribir dentro
> de la fundación, sin poder salir al fundar en plena partida:
> 0x545CE0/0x545F50, 0x4F99B0 y la elección del alcalde (abajo).

[+0x37] del comerciante: 0 nada; 1 a 3 candidato con 0 a 2 encargos
cumplidos; 4 puede presentarse (si su ciudad natal tiene [0x2C8] & 0x40).

**La candidatura.** 0x4F75F0 (rango en una ciudad, [town_tick.md](town_tick.md)
§9), con el alcalde de la ciudad, [+0x37] = 0 y la reputación para optar
≥ 120 (0x6737B8), pone [+0x37] = 1 y, si [0x6DF31C] = 0, llama a 0x5326E0
sobre el mundo, sin argumentos: no sabe quién es el candidato y los encargos
son para cualquiera. 0x5326E0 encola una operación 0x86 (datos a 0) y, para
cada tipo k de 0 a 4, si encuentra objetivo, una 0x85 con 16 bytes: u32
plazo (tick), u8 k, u8 0xFF (sin comerciante), dos 0 y 8 bytes de datos. El
registro no se limpia entre un tipo y el siguiente: los bytes que un tipo no
escribe se quedan con los del anterior (también lo que deja a medias un tipo
sin objetivo). Los plazos se cuentan desde el principio de un mes (tabla
0x672D78) del año en curso:

| Tipo | Encargo | Objetivo y datos | Plazo |
|-|-|-|-|
| 0 | fundar una ciudad | solo con menos de 26 ciudades. 0x532E30 (abajo): u32 máscara de mercancías y u8 número de ciudad de StadtDaten.ini; además u8 de la tabla 0x672D50 por ese número y 0xFF | mes + 25 |
| 1 | carretera entre dos ciudades | de los 7 pares de números de ciudad de 0x672BF8 (4 bytes: a, b, días, ?), el que tiene las dos ciudades con mercado y sin el bit 0x8000 de [0x76C] y más diferencia de habitantes: u8 a, u8 b (índices), u8 días | mes + días: el juego lo cuenta como meses y solo resta 12 una vez, así que lee 0x672D78 fuera de la tabla |
| 2 | cazar un barco pirata | de los 5 piratas, el de mayor suma del [+0x120] de sus barcos (más de 1000; la suma empieza en su [+0x16]); su último barco con capitán y [+0x15D] ≥ nº de comerciantes: u16 barco, u16 y u16 nombre y apellido del capitán ([+2], [+3]). Además, si la dificultad [0x13] de los ajustes es < 2, el [+0x14] del pirata del barco (0x51A470) pasa a ser ella + 2 | mes + 3 |
| 3 | acabar con una guarida pirata | el pirata con más barcos (más de 2): u8 pirata, u8 su guarida ([+0xE], un número de ciudad) | mes + 7 |
| 4 | que crezca una ciudad | la ciudad con mercado y menos habitantes (menos de 2001): u16 habitantes que debe tener (los suyos + 2000, redondeado hacia abajo a 250) y u8 ciudad | el mismo mes del año siguiente |

0x4EC5F4 (lista de los encargos sin dar) también se llama, pero lo que
devuelve no se usa.

0x532E30 (sitio de la ciudad nueva): suma, de las 20 primeras mercancías,
las existencias ([0x4 + 4w] y [0x670 + 4w] de cada ciudad, [+0x4 + 4w] de sus
oficinas) y el consumo ([0x64 + 4w] y [0x310 + 4w], [+0x64 + 4w] de las
oficinas); de las que se consumen al menos 1024, las tres con menos
existencias por consumo dan la máscara (un bit por mercancía, tabla
0x672C88; si dos coinciden, entra la cuarta; la 0xA añade 0x40020) y la que
más falta, la región buscada (tabla 0x5331F0). Entre los números de ciudad
libres (0x515A60: ni ciudad ni guarida pirata), el de esa región (byte
[+0x30] de StadtDaten.ini) más alejado de las demás ciudades: la mayor suma
de los cuadrados de la distancia a las 5 más cercanas (x, y de
StadtDaten.ini); si no hay ninguno de esa región, el más alejado de todos.

**Las operaciones.** 0x86 recorre la cola de eventos: cada 0x32 sin
comerciante y de tipo < 5 que ya no vale (0x532C50: la ciudad ya existe, una
de las dos ciudades tiene ya el bit 0x8000, el barco ya no es pirata o tiene
otro capitán, la guarida es otra, la ciudad ya tiene la mitad de los
habitantes...) pasa al tipo 5 y su manejador lo quita. 0x85 (0x53FD10), con
tipo < 5: si no hay ya un 0x32 del mismo tipo y objetivo (tipo 0: el mismo
número de ciudad o la misma máscara; 1: una de las dos ciudades; 2: el mismo
barco; 3: el mismo pirata o la misma guarida; 4: la misma ciudad) crea uno
(0x4D8CF0) para el día siguiente con los 16 bytes en [+0x8..+0x17].

**El evento 0x32** (0x4EAFE4): [+0x8] u32 plazo, [+0xC] u8 tipo (5:
anulado o fallido), [+0xD] u8 comerciante (≥ nº de comerciantes: sin dar),
[+0xE] u8 madurez, [+0xF] u8 empezado, [+0x10..+0x17] datos.

- Sin comerciante (en p3vita, `fn_4eafe4`): mientras valga, [+0xE] sube 2
  cada 2 días; en 0x3F se da al comerciante de la IA de índice más bajo con
  [+0x37] de 1 a 3 y reputación positiva (todos los encargos al mismo) y
  vuelve en [día del año] + 3 días. El jugador humano lo coge desde la
  interfaz: operación 0x4B (0x53B440: el encargo sin dar y válido, el
  comerciante candidato y sin otro encargo, 0x4EC694), que lo empieza.
- Con comerciante, sin empezar: 0x4EBF54 lo empieza ([+0xF] = 1). Tipo 0:
  crea la ciudad (0x531D50, que carga su mapa del archivo), [+0x16] es la
  ciudad nueva, su alcalde el comerciante, y un evento 0x39 al día siguiente.
  Tipo 1: el bit 0x8000 en [0x76C] de las dos ciudades y el plazo pasa a
  (1,5 · días + 20) días desde hoy.
- Empezado, antes del plazo (tabla 0x4EBF38), según el tipo:
  - 0: se cumple cuando la ciudad nueva tiene mercado ([0x76C] & 0x100; si
    no, mira cada 2 días).
  - 1, la carretera: mientras queden tramos ([+0x12], los días del par),
    cada día y medio, si el encargo tiene los materiales de uno (0x672BF0:
    3 de madera, 4 de ladrillos, 1 de herramientas, 2 de cáñamo, en
    contadores de 10 bits: [+0x14 + k] y dos bits en [+0x13]), los gasta y
    el tramo se hace; con el último, el edificio 0x27 en las dos ciudades
    (0x4F9C80 busca la única celda de la clase 0x18 de la ciudad, 0x521900
    lo pone en obras: 100 días, y cobra ladrillos, madera y herramientas a la
    ciudad). Cerca del plazo, si tiene los materiales de lo que falta, lo
    alarga 2 días. Sin tramos, se cumple cuando los dos edificios están
    terminados (lista [0x764]); el plazo se alarga lo que haga falta. **Los
    materiales solo los aporta el jugador** (operación 0x3B, que solo encola
    la interfaz): el de la IA siempre vence.
  - 2: cada 4 días mira que el barco siga siendo pirata (estado 0x12) con el
    mismo capitán (si no, [+0x16] = 2: fallido, se quita); se cumple con
    [+0x16] = 1, que pone 0x4EC864 desde las batallas.
  - 3, la guarida, por fases ([+0x12]): 0 espera a que un barco del
    comerciante se acerque (distancia² < 200; mira de 2 a 64 ticks después
    según lo cerca que esté el más cercano), pasa a la 1, apunta la guarida
    en su [+0xE1] y cada pirata con esa guarida se refuerza (0x5144A0: los
    barcos de la dificultad, 0x673658 = 4, 8, 16, 18, menos los que tiene,
    nuevos con 0x516170: nivel 1, artillería hasta [+0x14] · 40 + 100 y
    capitán); 1 espera a que uno llegue justo a la guarida (de 1 a 31
    ticks) y entonces el barco o su convoy ataca y empieza una batalla
    (0x6130B0, 0x611D80, 0x5FBFE0); 2 (lo pone la batalla) cumplido; 3 o
    más, fallido.
  - 4: se cumple cuando la ciudad llega a los habitantes (si no, mira cada
    semana y el último día antes del plazo).
  - 5: se quita.
- Plazo vencido: 0x4F9170 resta 1 (0x66BC80) del primer componente de su
  reputación en las ciudades con [0x2C8] & 0xC0, avisos al humano (tabla
  0x4EBF24) y el encargo pasa al tipo 5; se quita a los 28 días. Al jugador
  humano, además: con el de la guarida, su [+0xE1] = 0xFF; con el de la
  carretera, un encargo igual empezado cuyo «comerciante» [+0xD] es el
  índice de la ciudad [+0x10], con 1023 de cada material: la carretera la
  acaba haciendo la ciudad, y al cumplirse sube el [+0x37] del comerciante
  que tenga ese índice (un fallo del juego, reproducido).
- Cumplido: 0x4EC354 suma 1 a [+0x37] (2 si ha fundado una ciudad; con 4,
  el aviso lleva 0x80).
- La operación 0x3B (0x53A3E0, de la interfaz): u16 [+0x4] el encargo,
  u16 [+0x6] cuánto (cargas o barriles, 0x672C14), u8 [+0x8] ciudad, u8
  [+0x9] comerciante, u8 [+0xA] el material (0..3) o su mercancía. Lo saca
  de su oficina en la ciudad y después de sus barcos atracados en ella, y lo
  suma al contador del encargo. Con el comerciante ≥ nº de comerciantes, el
  juego toma por lo que falta un puntero: no se reproduce.

**Medido** (un alcalde de la IA retocado con 120 en cada una de las 12
partidas): sale siempre una 0x86 y 4 o 5 encargos (el 3 falta donde ningún
pirata tiene más de 2 barcos); en la partida 2 ya había 4 encargos guardados
y las 0x85 no crean nada. Instrucciones ejecutadas: 0x5326E0 336–385 de 387,
0x532E30 244–260 de 270, 0x53FD10 40–47 de 74. Siguiendo 200 días con los
encargos dados a la fuerza a ese comerciante (sin el de fundar): el de cazar
el pirata vence el día 38; los otros vencen hacia el día 200 y el 370; la IA
no cumple ninguno en ese tiempo. El de fundar una ciudad para el emulador el
día 5: 0x531D50 lee del archivo (`Archiver.dll!arc_CreateFile`) la plantilla
`iso/towns/stadtplatz%.2u.tow`.

**Fundar una ciudad** (al empezar el encargo de tipo 0, 0x4EBF54). Arma un
registro de 16 bytes (u32 situación 0, u32 mercancías [+0x10] del encargo,
u32 0, u8 número de ciudad [+0x14], u8 plantilla [+0x15], u8 índice nuevo =
nº de ciudades, que también va a [+0x16], u8 0) y llama a 0x531D50, que
amplía las ciudades (constructor 0x51B100, copiando las de antes) y sus
asedios (0x6334B0, [+0x4] el índice), pone el índice, el número (mundo
[+0x18 + i]) y la plantilla (mundo [+0x40 + i], [0x2C2]), elige los
ciudadanos candidatos a alcalde (0x5289B0), le da dos capitanes (0x526A50),
el astillero según el año (0x545BE0, nada con 0) y el mapa de la plantilla
`iso/towns/stadtplatz%.2u.tow` (0x51E8D0 → 0x4F9B40 → 0x4F9770: el mismo
formato que el town_class1 de la partida), lo demás (0x5458D0: productividad
de las empresas de las mercancías de la máscara, 38 habitantes, las calles)
y un evento 0x2F a los 180 días; rehace las rutas (0x448040: la matriz de
rutas pasa a n × n, las de la nueva con 0x445D50) y la matriz de tiempos de
viaje (0x531BD0). Después 0x4EBF54 pone de alcalde al comerciante y crea un
evento 0x39 para mañana: la muralla de la ciudad (0x4ECDF4, con 0x5252F0 y
0x522B20).

0x5462F0 (en 0x5458D0, una vez por cada bit 0x40000, 0x80000 y 0x100000 de
las mercancías, con 4, 0x10 y 0xD; en p3vita `fn_5462f0`): un nodo nuevo
(0x50FCA0) al principio de la lista [0x9B4] de la ciudad, de esa
instalación y de la ciudad, con [+0xC] = [+0xE] (0x50FE00, como mucho 1024)
= 1024 / (3 · nº de ciudades >> 4) por 200 (4), 700 (0xD), 500 (0x10) o 100,
>> 10. Al fundar en plena partida solo sale el 0x40000, que 0x532E30 pone
cuando el aceite (0xA) es de las mercancías que más faltan.

Por reescribir dentro de la fundación (en `new_town.cpp`, marcadas así; a
ninguna se llega al fundar en plena partida):
- 0x545CE0 y 0x545F50 (en 0x5458D0 con [+0xF] ≠ 0: preparar una ciudad que
  ya existe; 0x545F50 construye sus edificios y le da el mercado). Al fundar,
  [+0xF] = 0.
- 0x4F99B0 (en 0x51E8D0, sin plantilla de mapa): las 28 plantillas existen.
- La elección del alcalde (0x532063..0x5321C5, con la situación & 0x40): al
  fundar la situación es 0.

**El evento 0x2F** (0x4EA0D4, 391 instrucciones; en p3vita `fn_4ea0d4`,
`new_town.cpp`): [+0xC] u32 la ciudad, [+0x10] u32 fases, [+0x14] u32 tick
en que empezó la última. Lo crea 0x531D50 a los 180 días. Mira los bits de
[0x76C] de la ciudad que había al empezar; sin la ciudad, se quita; si no,
siempre vuelve:
- Fase 0 (sin la bandera 1). Sin el bit 1 de [0x76C] (el primer edificio
  municipal, [town_tick.md](town_tick.md)), a los 21 días. Con alcalde
  comerciante ([0x6F1]; al fundar es el fundador), si es de la IA, su
  oficina en la ciudad (0x5308A0 con create = 1); al humano, nada; bandera
  1, [+0x14] = tick y vuelve a 1/16 de día. Sin alcalde y con [0x6DF31C] =
  0, busca, del último comerciante al primero, el de la IA ([+0x8] ≠ 0) que
  no es alcalde de ninguna ciudad con más rango en su ciudad natal
  ([+0x39C + [+0x19]], estrictamente mayor, desde 0: con rango 0 no sale) y
  encola la operación {0x46, comerciante, ciudad, 0xCDCDCDCD, 0xBADEAFFE}
  (con ella sale alcalde aunque tenga menos de 5, §7); vuelve a los 14 días.
- Fase 1. Con la bandera 0x80 y sin trabajadores en la instalación 1
  ([0x854]), le pone uno y 4 pobres más ([0x2E0] y el total [0x2D4]). Con el
  bit 0x100 de [0x76C] (el mercado), fase 2 y 1/16 de día. Si no, según los
  habitantes [0x2D4] (con signo): menos de 500, 28 días; de 500 a 999,
  0x88DB − 35 por habitante ticks (68 días con 500, menos de uno con 999);
  desde 1000, según la muralla
  (0x5252F0): 1 o 2, **le da el mercado** (bit 0x100) y 5 ticks; 0, si en la
  lista de obras [0x762] hay un tramo de muralla de la ciudad (tipo ≥ 0x36,
  dueño 0xFF), 14 días, y si no, el mercado y 28 días; 0xFF (completa), 28
  días sin mercado.
- Fase 2 (0x100). Con el bit 0x800 de [0x76C] (lo pone 0x521900 al construir
  el prestamista, 0x25), fase 3, [+0x14] = tick y 1/16 de día. Si no, con
  más de 1500 entre tres veces los pobres y los acomodados ([0x2E0] · 3 +
  [0x2DC], sin signo), la ciudad encarga un prestamista (0x51FAB0 con 0x25;
  sin [0x6DF31C]) y vuelve a la semana; si no, a los 28 días.
- Fase 3 (0x800): cada 21 días, nada más.

El encargo de fundar se cumple cuando la ciudad tiene mercado, y en una
ciudad fundada en plena partida solo se lo da este evento (el otro que pone
el bit es 0x545F50, al que no se llega): hace falta que construya su primer
edificio municipal y que pase de 1000 habitantes, las dos cosas con sus
obras, y las operaciones de obras (0x22...) aún no se ejecutan, así que de
momento ni en p3vita ni en el oráculo llega a cumplirse.

Construir el edificio de la carretera solo ejecuta 140 de las 1071
instrucciones de 0x521900 (más 28 de 0x4FCBD0); el juego empieza a buscar el
solar desde lo que haya en su pila, pero como cada ciudad tiene una sola
celda 0x18 (ninguna, las de índice 8 y 9) y la espiral de 0x4F9C80 recorre
toda la rejilla, no importa: p3vita empieza en la estatua. El refuerzo
pirata ejecuta unas 930 instrucciones, casi todas ya reescritas (0x507D60,
0x509A50, 0x5097C0...).
