# Mapa del mar y navegación

Ingeniería inversa de `Patrician3.exe` (GOG 1.1). Marcas como en
[savegame.md](savegame.md): **[código]** leído en el desensamblado,
**[datos]** comprobado con los ficheros del juego, **[oráculo]** comparado con
el propio código del juego en el emulador.

> Implementación en p3vita: `src/p3data/seamap.cpp` (posiciones del mapa),
> `src/p3data/navigation.cpp` (rutas, §5), `src/p3data/sailing.cpp`
> (movimiento, puerto y orden de zarpar, §4 y §6),
> `src/p3data/town_tick/ship_ai.cpp` y `ship_trade.cpp` (los barcos de la IA
> en puerto, §8), `src/p3data/convoys*.cpp` (convoyes, §9) y la vista del mapa del mar del visor (`src/app/main.cpp`);
> `tools/p3map` dibuja el mapa de una partida en un PNG. Las rutas entre
> ciudades coinciden con las del juego en las 12 partidas v0x82 de `missav/`
> (6633 rutas). La navegación, con los barcos de la IA decidiendo y
> comerciando y los ticks de las ciudades, es idéntica byte a byte a la del
> juego partiendo de la partida que lee p3vita, también con los convoyes de
> los comerciantes (ver [../oracle.md](../oracle.md)).

## 1. Coordenadas

Todo va en píxeles de `images/Vollansichtskarte.bmp` (2400 × 1800, TEX16036):

- La posición de un barco ([+0x1C], [+0x20] del `struct ship`) es 16.16: la
  parte entera ([+0x1E], [+0x22]) es el píxel. Los barcos atracados están
  exactamente en el punto de llegada de su ciudad **[datos]** (la Lupus en
  Edimburgo: (115,5; 715,5), y `AnfahrtXPos/YPos` de Edimburgo es (115, 715)).
- La rejilla de navegación (`navdata/nav_matrix.dat`, 640 × 472) es más basta
  (unos 3,75 píxeles por celda).

## 2. Ciudades en el mapa (`scripts/StadtDaten.ini`)

El juego carga cada `[StadtN]` en 0x6DDB90 + id · 0x34 (0x51e440) **[código]**:

| Offset | Clave | Uso |
|-|-|-|
| +0x00, +0x04 | `StadtSymbolXPos/YPos` | esquina superior izquierda del símbolo (26 × 26) |
| +0x08, +0x0C | `NamenXPos/YPos` | nombre; `NameLinksOderRechtsBuendig=r` lo alinea a la derecha |
| +0x10, +0x14 | `EreignisSymbolXPos/YPos` | símbolo de evento |
| +0x18, +0x1C | `SchiffAufLandXPos/YPos` | |
| +0x20, +0x24 | `AnfahrtXPos/YPos` | **punto de llegada**: ahí atracan los barcos y empiezan y acaban las rutas |
| +0x30 | `Region` | estilo de los edificios (savegame.md §8.3) |

Símbolos: `images/p_global_stadt01.tga` (TEX30021), 3 × 3 de 26 × 26: tres
colores (rojo, azul, oscuro) por filas y tres estados por columnas. p3vita usa
el azul en las ciudades con oficina del jugador y la columna 1 para la ciudad
elegida **[supuesto]**. Comprobado a ojo con `p3map` que símbolos, nombres y
barcos caen en su sitio en las 24 ciudades de `1.pat` **[datos]**.

## 3. Barcos en el mapa

- Sprites: `images/Schiffe/page_m{schnigge,kraier,kogge,holk}01.tga`
  (TEX30007–30004), 16 fotogramas de 52 × 56 en una rejilla de 4 × 4.
- El fotograma es el **rumbo** del barco, el byte [+0x1] (offset 7 del barco
  en la partida): 1 oeste, 5 norte, 9 este, 13 sur, en sentido horario; en la
  hoja, cada fila de 4 va de un rumbo cardinal menos uno al siguiente
  **[código, 0x517410 + datos]**.
- El mapa del juego solo dibuja los barcos con estado 0x0F (navegando) y 0x12
  (piratas) (P3Modding, `patches/scrollmap-render-all-ships.md`).
- Marca del barco del jugador: `images/Schiffe/schiffmarker01.tga` (16 × 22).

## 4. Movimiento (0x516cc0, un tick) **[código + oráculo]**

`advance_time` (0x530E80) avanza tick a tick (256 por día). En cada tick, si
`tick & 0xFF == 0` recalcula la fecha (0x5310D0: año = tick / 93440, día del
año = (tick ≫ 8) mod 365 y el mes con la tabla 0x672D78, sin bisiestos) y
siempre llama a la actualización de barcos 0x506720 (thiscall sobre el objeto
de barcos 0x6DD7A0), que recorre con una máquina de estados dos listas
enlazadas por [+0x6]: los barcos en puerto (cabeza [+0xE8], tabla 0x507CB0) y
los que navegan ([+0xEA], tabla 0x507CF0), y después los convoyes ([+0xEC]).
Los que navegan (estado 0x0F) llaman a 0x516cc0(índice):

1. Si el dueño es un comerciante, cada 4 ticks (`tick & 3 == 0`) el casco
   [+0x18] baja 1; con casco ≤ 0 devuelve 0 y el llamador lo hunde.
2. Sin ruta ([+0x24] = 0) devuelve 1; si es de la IA, pone [+0x15C] a 0 y cada
   64 ticks (`(índice ^ tick) & 0x3F == 0`) encola una operación 0x00 hacia su
   destino o su ciudad natal (0x54AA70).
3. Posición += desplazamiento ([+0x28], [+0x2C], 16.16).
4. [+0x2], otro índice de la ruta, vuelve a 0 si su punto está a más de 0x48
   píxeles en x o en y.
5. Si la parte entera de la posición coincide con el punto actual de la ruta
   ([+0x34]), pasa al siguiente: centra la fracción (0x7FFF), fija un nuevo
   desplazamiento con 0x517410(velocidad, punto) y guarda en [+0x30] la
   velocidad que se ve, `velocidad · 64 / 115 + 4`.
6. Pasado el último punto: desplazamiento y [+0x30] a 0, y busca la ciudad de
   ese punto (0x446d50). Si hay ciudad, coloca el barco en su punto de llegada
   (fracción 0), anota la ciudad en [+0x39] y devuelve 0. Si no, se queda en
   el mar sin ruta ([+0x24] y [+0x48] a 0, pero [+0x34] no vuelve a 0) y
   devuelve 1. Los barcos con [+0x15C] (ataque pirata) pasan al estado 0x14.

0x446d50(punto): la primera ciudad de la partida (en el orden del mundo) cuyo
símbolo lo contiene (`x0 < x < x0 + 22`, igual en y; StadtSymbolX/YPos) o
cuyo punto de llegada es exactamente ese; si no, 0x28.

Cada 32 ticks (`tick & 0x1F == 0`) el llamador baja en 1 el descanso de la
tripulación [+0x3E] (u16; si el byte bajo pasa a 0xFF y el alto es < −1, el
alto se queda en 0xFF). Con lo que devuelve 0x516cc0:

- 1: si [+0x3A] (barco al que persigue) no es 0xFFFF, 0x50B030 (persecución;
  p3vita solo reproduce que lo olvida si el índice no existe) y la lista
  ordenada por x de 0x50D0C0 ([+0xA], [+0xC], para los encuentros), que no
  afecta al movimiento.
- 0 con casco ≤ 0: se hunde (0x506CD6): sin ruta, fuera de la lista, estado
  0x11 y casco 0.
- 0: llega a la ciudad [+0x39] (0x506D8E). Si la ciudad tiene en su
  situación [+0x2C8] el bit 0x200 o 0x4000000 (bloqueo o asedio), o el 0x400
  y el barco sigue una ruta automática ([+0x136] & 1), se queda fuera: sin
  ruta, [+0x37] = [+0x39], [+0x39] = 0xFF, estado 0x0F, y **el bucle vuelve a
  pasar por él en el mismo tick** (no avanza el enlace). Si no, quita la ruta
  (0x516840), lo saca de la lista del mar y lo pone el primero de la de
  puerto en el estado 3 con [+0x138] = 0.

Velocidad (paso 5; también 0x5170F0), en enteros sin signo salvo la carga:

```
carga = 614 · bodega ocupada [+0x118] / capacidad [+0x10]          (división entera)
base  = (4096 − carga) · tabla_0x67366C[tipo] >> 12                 (635, 693, 520, 578)
casco = clamp(130 · casco [+0x18] / casco máximo [+0x14] + 165, 204, 256)
velocidad = casco · base >> 10
```

Una Snaikka vacía y sana: 256 · 635 >> 10 = 158; llena, 134.

0x517410(velocidad, x, y): con `dx, dy` desde la parte entera de la
posición, `pasos = 1 + trunc(128 · √(dx² + dy²) / velocidad)` (en double con la
constante −128.0 de 0x673678) y desplazamiento = `(d << 16 ± 0x8000) / pasos`
(+0x8000 si d > 0, −0x7FFF si d < 0; 0 si d = 0; división que trunca). El
rumbo [+0x1] sale del desplazamiento ya calculado (vx, vy): el cuadrante da la
base (1, 5, 9, 13) y la razón entre las componentes suma 0–4 (umbrales
0x3244/65536 ≈ tan 11° y 0x96CC/65536 ≈ tan 30°): con `c` y `e` según el
cuadrante (vx > 0 y vy > 0: base 9, c = vx, e = vy; vx > 0 y vy ≤ 0: base 5,
c = |vy|, e = vx; vx ≤ 0 y vy > 0: base 13, c = vy, e = |vx|; si no, base 1,
c = |vx|, e = |vy|), si c > e suma 2 si c · 0x96CC < e ≪ 16, 1 si
c · 0x3244 < e ≪ 16 y 0 si no; si c ≤ e, 2, 3 o 4 con los papeles cambiados
(productos de 64 bits). El rumbo se queda en 4 bits.

Llegada prevista [+0x48] (0x5169C0): tick + `8 · longitud / velocidad`, con
la longitud de la ruta sumando por tramo `16 · mayor + 7 · menor` de |dx| y |dy|.

## 5. Rutas **[código + oráculo]**

Objeto global `*0x6CBDC8` (0x2C bytes, constructor 0x444ba0). 0x444d30 lee
los ficheros de `scripts/scrollmaps.ini` (`[Navmap0]`):

| Campo | Contenido | Fichero |
|-|-|-|
| +0x18, +0x1A, +0x14 | ancho, alto y rejilla tierra/agua (u8, 0 = agua) | `nav_matrix.dat` |
| +0x1C, +0x0C | nº de puntos (350) y puntos (s16 x, s16 y, en celdas) | `nav_vec.dat` |
| +0x04 | nº × nº celdas: u32 distancia (**16.16**) y s16 siguiente punto, en huecos de 8 bytes | `matrix_int.dat` |
| +0x10, +0x1E | rutas entre las ciudades de la partida (n × n), las crea 0x4482c0 al cargar | |
| +0x20, +0x24 | un precálculo por ciudad sobre toda la rejilla (0x448780), que las rutas no usan | |

Los formatos de los tres ficheros los documenta P3Modding
(`file-formats/navigation-matrix.md`, `navigation-vector.md`,
`navpoint-matrix.md`); la distancia de `matrix_int.dat` va en 16.16.

Al cargar una partida, 0x4482c0 calcula con 0x445d50 la ruta entre los puntos
de llegada de cada par de ciudades y las guarda en la matriz. Un barco que
navega entre dos ciudades no tiene ruta propia: al cargarlo (0x516520, con
los puntos de llegada de [+0x37] y [+0x38]), 0x445010 ve que los dos
extremos son ciudades (0x446d50) y le da el puntero de la matriz, con el
primer punto cambiado por el de salida; el campo [+0x36] indica si la ruta es
suya (solo entonces se guarda y se libera) **[código + oráculo]**.

0x445010(puntos, &ruta) es también la que da la ruta al zarpar. Con dos
puntos que no son los dos ciudades calcula una ruta propia con el mismo
algoritmo que 0x445d50 (repetido dentro de la función; p3vita usa
`NavData::route` y coincide en las 415 órdenes hacia puntos del mar del
oráculo). Si uno de los extremos no tiene agua en 50 celdas, no escribe la
ruta, pero sí cambia el primer punto del elemento 6 · n de la matriz (la ruta
de la ciudad 6 a la 0) por el de salida.

With three points or more (0x44561E; only the sea map's route by points,
operation 0x0B, uses it: `NavData::route_through`, checked with the scenario
`route_points_ops`), stretch by stretch: both ends to cells and water within 50
(if not: the matrix's element 6 · n gets the stretch's first point, no route,
0 returned); in sight, the end is added; if not, 0x444800's pair and the
matrix's next points to it, as pixels, then the end, without cutting corners.
Then the whole route: its first point, the corners by 0x446f20 with the last
argument **1** (A not added again), from the first point different from the
first one and the third always the raw route's third, the last point read and
the repeats out as in step 6 below. It returns 1 (the route is the ship's).

0x445d50(inicio, fin), con puntos en píxeles:

1. Pasa los dos puntos a celdas: `celda = ((píxel − 0x44) · 1000 + 1768) / 3536`
   (al revés, `píxel = (celda · 3536 + 500) / 1000 + 0x44`, divisiones que
   truncan). Si una celda es tierra, 0x448450 busca la de agua más cercana en
   anillos cuadrados de radio 1 a 50 (de cada anillo, las columnas de los
   extremos enteras y del resto solo la fila de arriba y la de abajo; gana la
   de menor dx² + dy² y, a igualdad, la primera; para en cuanto ese mínimo no
   supera el cuadrado del radio siguiente).
2. Si desde una celda se ve la otra (0x4445a0), la ruta son los dos puntos.
3. Si no, 0x444800 hace dos listas de los puntos de navegación visibles desde
   cada celda, con su distancia `trunc(√((dx² + dy²) · 2³²) + 0,5)` (16.16, como
   la matriz), insertando cada uno delante del primero que tenga menor
   distancia. Elige el par que minimiza `distancia al inicio + matriz +
   distancia al fin`; a igualdad, el de menor tramo de matriz y, si no, el
   primero.
4. La ruta: el punto de inicio, después recorre la matriz («siguiente punto»)
   desde el punto elegido para el inicio hasta el del fin y, en cada punto de
   navegación B (con A el anterior y C el siguiente, contando inicio y fin),
   llama a 0x446f20; al final, el punto de fin.
5. 0x446f20 recorta la esquina en B: con los puntos medios M1 de A–B y M2 de
   B–C (en float), el giro es `180 − acos(v1x/|v1|) − acos(v2x/|v2|)` en grados
   (v1 = B − M1, v2 = M2 − B). Si |giro| < 0,5 añade A y B (A solo si el
   parámetro es 0; la ruta siempre lo pasa a 0). Si no, añade dos puntos a
   distancia `r = mín(10, |giro|/180 · mín(|v1|, |v2|))` de B por cada tramo
   (el segundo solo si es distinto del primero), con la pendiente de cada
   tramo y redondeo `trunc(x + 0,5)`.
6. Quita los puntos repetidos seguidos; tras quitar uno no compara el
   siguiente.

Visión directa (0x4445a0), entre celdas, recorriendo desde el segundo punto
hacia el primero: primero 15 barreras verticales y 13 horizontales fijas
(tablas 0x66D6E4/0x66D704/0x66D724 y 0x66D6C8/0x66D690/0x66D6AC) que cierran
estrechos; un segmento las corta si `t = (X − x1)(y2 − y1) + (dx ≫ 1)` cae entre
`(Y0 − y1) · dx` y `(Y1 − y1) · dx`. Después, Bresenham por las celdas
intermedias buscando tierra; en una esquina exacta, si la celda intermedia es
agua, la diagonal no se mira.

Los cálculos de 0x446f20 mezclan floats y doubles con la FPU a 53 bits y
`acos` (0x63B2C4, con `fpatan`); p3vita los hace en el mismo orden con
`std::acos`. Con las comparaciones x87, un NaN sigue la rama de la esquina.

## 6. Puerto: zarpar, salir y atracar **[código + oráculo]**

Estados [+0x134] que intervienen (lista de puerto salvo 0x0F):

| Estado | Manejador | Qué hace en cada tick |
|-|-|-|
| 0 atracado | 0x5067D0 | Pasado un día desde que atracó ([+0x44] + 0x100 < tick), el descanso [+0x3E] sube 1 hasta 0x700. Si es de la IA, decide su próximo viaje (0x518B80); si es de un jugador sin ruta automática, [+0x37] = [+0x39]. |
| 1 esperando | 0x5068CE | El mismo descanso; [+0x138] cuenta atrás y a 0 pasa al estado 0. Lo usan los convoyes y los puertos bloqueados. |
| 2 saliendo | 0x506945 | [+0x138] sube hasta 0x40. En el primer tick, si la ciudad está bloqueada (0x4000200) pasa al estado 1 con [+0x138] = 0x100 y sin ruta; si no, [+0x30] = velocidad / 2. Con 0x40, 0x5090E0 y a la lista del mar. |
| 3 entrando | 0x506A86 | [+0x138] sube hasta 0x40; en el primer tick, [+0x30] = velocidad / 2 y [+0x15C] = 0. Con 0x40, atraca (0x519C90). |
| 0x0F navegando | 0x506C53 | §4. |

Lo demás de la escena del puerto (0x6E6220: 0x6132C0, 0x6135C0, 0x613920) es
dibujo.

**Salir al mar (0x5090E0):** [+0x44] = tick, recalcula la bodega (0x5182B0)
y 0x516BC0: desplazamiento a 0, [+0x37] = [+0x39], posición = punto de
llegada de la ciudad (fracción 0); si no tiene ruta, se la da 0x445010 hacia
[+0x38] con [+0x48] = tick + llegada prevista; [+0x34] y [+0x2] a 0; el rumbo
pasa a `(rumbo − 4) & 7` si era < 8 y a 2 si no. Después estado 0x0F,
[+0x138] = 0, [+0x39] = 0xFF y, si el destino es una ciudad, suma su carga
positiva a la mercancía en camino de esa ciudad ([+0x670] y [+0x370] del
struct town). Si el dueño es un comerciante apunta la estadística de bodega
llena (carga / capacidad en float: en el barco [+0x12C] y [+0x130]++, en el
comerciante [+0x540] y [+0x544]++); si no, estado 0x12 (pirata).

**Atracar (0x519C90):** estado 0, [+0x30] = 0, [+0x44] = tick, la ciudad
[+0x39] cuenta un barco más ([+0x996]) y la ciudad de destino [+0x38] resta la
carga de su mercancía en camino ([+0x670] se queda en ≥ 0; el juego no
comprueba que [+0x38] sea una ciudad), mensaje al jugador y [+0x138] = 0.

**Orden de zarpar, operación 0x00 (0x537670):** `{u32 código, u32 barco,
u32 ciudad, s32 x, s32 y}`. [+0x3A] = 0xFFFF. Los estados 6 (el barco está en
una lista de su ciudad, [+0x81E] del struct town) y 8 tienen su propio
camino; p3vita no los reproduce. Si no: [+0x136] &= 0xFC, el aviso 0x800
(0x517770, también en el [+0x18] de su convoy); solo siguen los estados < 4,
0x0F y 0x10. Un barco de un convoy ([+0x8] < [+0xF6]): si lo guía, el
[+0x16] del convoy a 0; si no, sale del convoy (0x509F80, 0x501BE0) y, sin
capitán, [+0x3C] |= 0x1000 y [+0x15C] = 0. En puerto (< 4) tiene que pasar
0x519BA0:

- marineros [+0x40] ≥ mínimo del tipo (0x673660: 5, 8, 10, 12);
- byte alto del descanso [+0x3F] > 0;
- bodega ocupada ≤ capacidad;
- sin capitán ([+0x42] ≥ [+0xF2]) o casco máximo ≤ 5 · casco (con capitán, el
  barco no zarpa con el casco por debajo del 20 %);
- estado distinto de 7, 9, 12–14 y < 17;
- la ciudad, si está en una, sin los bits 0x4000200.

Después, según el estado: 2 (saliendo) hacia la ciudad de la que sale (o su
punto de llegada) da la vuelta: estado 3, [+0x138] = 0x40 − [+0x138],
[+0x38] = [+0x37], [+0x48] = tick − [+0x138] + 0x40 y sin ruta; el guía de un
convoy, con todos sus barcos (y el convoy: [+0x12] = 3, [+0x16], [+0x1F]); y
no sigue. 3 (entrando): si es hacia la misma ciudad no hace nada; si no, da
la vuelta: estado 2, [+0x138] = 0x40 − [+0x138], [+0x37] = [+0x39] (y el
convoy: [+0x18] |= 0x28, [+0x1C], [+0x12] = 2, [+0x16]). 0 y 1: si ya está en
esa ciudad no hace nada; si no, [+0x37] = [+0x39], [+0x138] = 0 y estado 2.
Los que navegan cambian de rumbo sin más. Si el barco iba en un convoy con
una expedición sin empezar ([+0x38] < 3), el dinero del convoy vuelve al
dueño. Al final, 0x5172A0 fija el destino ([+0x38] = ciudad, o 0xFF si es un
punto del mar), pone la fracción de la posición a 0x7FFF, quita la ruta y le
da una nueva con 0x445010 desde la parte entera de la posición, con su
llegada prevista; [+0x30] = 1; y el convoy del guía, [+0x38] = 0xFFFF,
[+0x1F] = la ciudad, el barco parado pasa al estado 1 y 0x501BE0.

Un barco con destino 0xFF que se queda sin ruta lee al salir del puerto
(0x516BC0) el id de ciudad 255 de la tabla de 40 del mundo, fuera de ella, y
navega hacia el punto de llegada de esa ciudad; p3vita lo deja sin ruta.

### 6.1 Lo que no mueve el barco pero sí cambia la memoria **[código + oráculo]**

- Avisos a la interfaz [+0x3C] (u16, siempre con `or`): 0x2000 cuando cambia
  la parte entera de la posición, 0x80 al cambiar de tramo si cambia la
  velocidad que se ve (`([+0x30] ^ velocidad) & ~0xF`), al acabar la ruta y
  en el primer tick del estado 2; 0x28 al llegar a una ciudad, al zarpar y al
  dar la vuelta entrando en el puerto; 0x08 al quedarse en el mar sin ruta;
  0x20 en los cambios de estado; 0x1000 en el primer tick del estado 3; 0x200
  cuando cambia el byte alto del descanso; 0x800 con una orden nueva (0x517770).
- Lista de los barcos en el mar ordenada por x, para los encuentros: [+0xA]
  el anterior y [+0xC] el siguiente, sin cabeza. 0x50CE50 mete el barco al
  zarpar (tras la cabeza de la lista del mar), 0x50D0C0 lo recoloca tras cada
  tick en el mar y 0x50D040 lo saca al entrar en puerto o al hundirse.
- Contadores del objeto de los barcos, a 0 al empezar 0x506720: [+0x100]
  barcos del jugador en puerto (estados 0 a 3), [+0x102] en el mar, [+0x104]
  y [+0x106] sus convoyes.
- Al atracar, la ciudad cuenta un barco más en [+0x996]; al zarpar, el
  comerciante suma la bodega llena (float) en [+0x540] y un viaje en [+0x544].
- La orden de zarpar deja en [+0x13B] la ciudad de destino.

## 7. El tiempo real **[código]**

El bucle de mensajes (0x4B8EE0) mide el tiempo con `timeGetTime` y duerme
para no pasar de unos 50 fotogramas por segundo. 0x546620 convierte el tiempo
transcurrido en ticks con los milisegundos por tick de la velocidad elegida
(`segundos por día · 1000 / 256`, tabla 0x66DE7C) y encola una operación
0xC4 hasta el tick nuevo, que llama a `advance_time`. Lo que pase de 8 ticks
por actualización se pierde (256 en el avance rápido).

| Velocidad | s/día | ms/tick |
|-|-|-|
| muy lenta (la de `ZEIT=0` en `P2.cfg`) | 120 | 468 |
| lenta | 90 | 351 |
| normal | 60 | 234 |
| rápida | 30 | 117 |
| muy rápida | 20 | 78 |
| ralentizada (tecla Pausa) | 900 | 3515 |
| avance rápido (F12) | — | 2 |

Hay un tercer modo de 3375 ms por tick, con un tick como mucho por
actualización, que por el código que lo activa parece el de las batallas
navales **[supuesto]**. Las teclas + y − cambian la velocidad. Las partidas
guardan los ms por tick después de los ajustes ([+0x8D4]; las de `missav/`
están en 3515).

## 8. Los barcos de la IA en puerto **[código + oráculo]**

En la lista de barcos en puerto de 0x506720, el estado 0 (0x5067D0) de un
barco de un comerciante de la IA llama a 0x518B80, que decide qué hace:

- Órdenes [+0x136] & 0x20: quedarse quieto (estado 1, 0x80 ticks, sin
  marineros); & 0x10: va armado (0x51A070 monta la artillería del almacén de
  armas de la ciudad, [0x124]; 0x519FC0 y 0x5184F0 contratan marineros de más).
- Cada barco tiene una ciudad base [+0x13A] (donde están las oficinas de su
  dueño) y una ciudad a la que va a comerciar [+0x13B]. Al llegar a esta,
  0x519280 deja en la oficina de la base lo que le falta y vende el resto; en
  las ciudades de paso, 0x5193F0 (volviendo a la base) y 0x5196B0 (yendo a
  comerciar) reservan lo que falta a las oficinas y a la base y llenan la
  oficina de la ciudad en la que están.
- Al volver a la base elige otra ciudad para comerciar (0x518FF0): la de más
  margen esperado (0x52F030, con los tiempos de viaje de la matriz [+0x6C] del
  mundo, 0x531B70 y 0x516AB0) entre las que tienen mercado y no están
  sitiadas ni bloqueadas (0x518D60; las cocas y hulks tampoco van donde está
  el bit 1 de [0x2C8]); 0x519170 añade una parada intermedia [+0x38] si
  alarga el viaje menos de un 20 %. Antes de zarpar compra lo que más ganancia
  promete (0x519920). La IA cobra 6·precio/5 al vender y paga 5·precio/6 al
  comprar, y apunta el saldo en [+0x124].
- Si puede zarpar (0x519BA0) pasa al estado 2 hacia su próxima parada; si la
  bodega no llega a la cuarta parte, carga primero. 0x519EB0 la une a un convoy
  o lo crea (0x509B90, 0x509AE0, 0x501680...). Si no puede (0x518E00), espera
  (estado 1, [0x547] + 0x40 ticks del comerciante, o 3·[0x547] con la
  tripulación cansada) o, con el casco por debajo de (0x200 − [0x547])/0x200
  del máximo, va al astillero (estado 4).
- Estado 4 (0x506A19): sale de la lista y entra en la de reparación de su
  ciudad ([0x81E], 0x52AC80) con el estado 6; el astillero (0x508AA0, tick de
  ciudad) lo repara y lo devuelve con el estado 5, que (0x506B6A) vuelve al 0.

Los barcos de la IA que se quedan en el mar sin ruta encolan cada 64 ticks
una orden de zarpar (§4). Los convoyes se crean y se llenan en puerto (§9).

## 9. Convoyes **[código + oráculo]**

Un convoy es un registro de 0x3C bytes (array *0x6DD7A8, [0x6DD896]
reservados; `SaveGame::convoys`) con sus barcos enlazados por el [+0x6] de
cada barco, que mientras van en él no están en las listas de puerto ni del
mar. Los convoyes que existen forman una lista que empieza en el [+0xEC] del
objeto de los barcos y sigue por el [+0x8] de cada uno; los libres, otra en
[+0xE4] (0x509AE0 reserva 0x20 más cuando no queda ninguno). Campos:

| Offset | Qué |
|-|-|
| +0x0 u8 | dueño (el del barco que lo guía) |
| +0x1 u8 | tipos de sus barcos (OR del [+0xE] de cada uno) |
| +0x2 u16 | evento de la cola asociado (0xFFFF: ninguno); con uno de tipo 0x34 apunta los barcos que pierde |
| +0x4 u32 | capacidad de los barcos del dueño |
| +0x8 u16 | siguiente en su lista |
| +0xA u16 | primer barco |
| +0xC u32 | velocidad del más lento: rumbo de todos y velocidad que se ve, `(v << 6) / 115 + 4` |
| +0x10 u16 | barco que lo guía |
| +0x12 u16 | estado (el de los barcos): 0, 1, 2, 3 como en puerto; 4 al astillero; 6 en la lista de su ciudad; 0x0F navegando; 0x12 piratas al acecho; 0x13 fuera del mapa; 0x14 ataque a una ciudad; 0xFF libre |
| +0x14 u16 | bit 1: todos del mismo dueño (al deshacerlo no se cobra ni cambia la reputación); 0x10: ningún barco del dueño puede guiarlo |
| +0x16 u16 | ticks de la maniobra o de espera ([+0x138] de los barcos) |
| +0x18 u32 | avisos a la interfaz |
| +0x1C u16 | ciudad (0xFF en el mar) |
| +0x1E u8, +0x1F u8 | origen y destino |
| +0x20 s32 | dinero que se lleva un convoy de la IA que sale del mapa (lo cobra al volver) |
| +0x24 u16 | con < 0x7FFF, al acabar la ruta en x < 0x82 y 0x578 < y < 0x708, sale del mapa |

La IA los crea en puerto (0x519EB0 → 0x509B90, §8). El tercer recorrido de
0x506720 los mueve según su estado (tabla 0x507D14). Antes, el convoy sin
barcos sale de la lista y se borra (0x50A3D0), y el que se ha quedado sin
guía se deshace (0x50A580). En cada estado:

- **0 en puerto** (0x5071A4): si el que lo guía es de la IA decide como un
  barco atracado (0x518B80) y espera al menos 0x200 ticks. Si su ciudad no
  es su destino, todos pasan a esperar (1): los barcos de la IA que han
  llegado a su destino dejan el convoy (0x509F80) y los demás reservan y
  compran para la parada siguiente (0x519B50); después se recalcula
  (0x501BE0). Si ya está en su destino, descansan (como en puerto).
- **1 esperando** (0x5073C0): cada barco descuenta su espera y descansa un
  punto por tick; cuando no queda ninguno esperando, sale (2) si va a otra
  ciudad.
- **2 saliendo** (0x5074DB): en el primer tick, 0x501EB0 hace salir a cada
  barco como 0x5090E0 (el que no puede zarpar deja el convoy; la carga en
  camino se suma al [0x670] del destino y al [0x370] de la ciudad de la que
  sale) y 0x503CF0 les da la mitad de la velocidad del convoy. A los 64
  ticks, al mar (0x0F): el guía entra en la lista por x, se recalcula la
  bodega y, si todos son del mismo dueño de la IA, 0x503B50 encola las
  operaciones 0x1E y 0x1F (cuatro), con `rand` y puntos de 0x534F10 (los
  15 objetos del mundo en [+0xB10] o `scripts/Expeditionen.dat`).
- **0x0F navegando** (0x50787F → 0x502110): el guía avanza por su ruta y los
  demás copian su posición. El casco de los barcos de la IA y de los piratas
  baja 1 en los ticks 0x28, 0x50 y 0x78 de cada 128 y el de los humanos uno
  cada 4; el barco que se queda sin casco sale del convoy a la lista del mar.
  En cada punto de la ruta, rumbo y llegada prevista para todos (0x50A240);
  al final, una ciudad (entra en puerto: estado 3; ante un puerto cerrado se
  queda fuera), el mar o la salida del oeste (0x5029F0: evento 0x12 en la
  cola, posición (30000, 30000), estado 0x13). Si el guía ha cambiado de
  convoy, 0x5017F0 elige otro y el recorrido vuelve a pasar por el mismo.
- **3 entrando** (0x5076A0): a los 64 ticks atracan todos (0x519C90), el
  dueño cobra [+0x20] y el convoy sigue si 0x5017F0 encuentra guía y es de
  un solo dueño; si no, se deshace (0x50A580). Al deshacerse, cada
  comerciante ajeno paga al dueño, si el convoy llega a su destino desde
  otra ciudad, `10 · ⌊(⌊√Σ max([+0x114], 100)⌋ · ⌊√(20 · tiempo de viaje)⌋
  + 99) / 100⌋`, y cambia la reputación (0x4F9050).
- **4** pasa a la lista de convoyes de su ciudad ([0x820]) con el estado 6;
  **9** (0x503930) y **0xA** cuentan una espera; **0xB** y **0x13**, nada;
  cualquier otro estado deshace el convoy barco a barco y lo borra.

En p3vita (`src/p3data/convoys*.cpp`, miembros de `Sailing`) los convoyes de
los **piratas** (dueño que no es comerciante) se quedan quietos: sus
ataques acaban en batallas (0x50BC40 → 0x605420), que no se reproducen, y
tampoco los encuentros (0x5053C0) ni la persecución entera (0x50B030). Sin
traducir, porque solo los alcanzan los piratas o los jugadores, que no
tienen convoyes en las partidas: la rama de piratas de 0x5017F0 (crea un
capitán), las rutas automáticas del jugador en convoy (0x503230, 0x4D4DD0),
borrar los barcos que queden en un convoy al borrarlo (0x507FB0) y el
ataque a una ciudad (0x50BA80).
