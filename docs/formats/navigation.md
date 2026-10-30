# Mapa del mar y navegación

Ingeniería inversa de `Patrician3.exe` (GOG 1.1). Marcas como en
[savegame.md](savegame.md): **[código]** leído en el desensamblado,
**[datos]** comprobado con los ficheros del juego, **[oráculo]** comparado con
el propio código del juego en el emulador.

> Implementación en p3vita: `src/p3data/seamap.cpp` (posiciones del mapa),
> `src/p3data/navigation.cpp` (rutas, §5) y la vista del mapa del mar del
> visor (`src/app/main.cpp`); `tools/p3map` dibuja el mapa de una partida en
> un PNG. Las rutas entre ciudades coinciden con las del juego en las 12
> partidas v0x82 de `missav/` (6633 rutas, ver [../oracle.md](../oracle.md)).
> El movimiento de los barcos (§4) aún no está implementado.

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

## 4. Movimiento (0x516cc0, un tick) **[código]**

`advance_time` (0x530E80) avanza tick a tick (256 por día) y en cada uno llama
a la actualización de barcos 0x506720, que recorre dos listas de barcos con una
máquina de estados. Los que navegan (estado 0x0F, segunda lista, tabla
0x507CF0) llaman a 0x516cc0:

1. Si el dueño es un comerciante, cada 4 ticks (`tick & 3 == 0`) la salud
   [+0x18] baja 1; con salud ≤ 0 devuelve «no ha llegado» y el llamador lo hunde.
2. Sin ruta ([+0x24] = 0) pide una a la cola de operaciones (comerciantes de la IA).
3. Posición += desplazamiento ([+0x28], [+0x2C], 16.16).
4. Si la parte entera de la posición coincide con el punto actual de la ruta
   ([+0x34]), pasa al siguiente: centra la fracción (0x7FFF) y fija un nuevo
   desplazamiento con 0x517410(velocidad, punto).
5. Pasado el último punto: para el barco y busca la ciudad de ese punto
   (0x446d50 del objeto de navegación). Si hay ciudad, coloca el barco en su
   punto de llegada y anota la ciudad en [+0x39]; la llegada se detecta en el
   tick siguiente (0x50d0c0 lo atraca).

Velocidad (en el paso 4):

```
carga = 614 · bodega ocupada [+0x118] / capacidad [+0x10]          (división entera)
base  = (4096 − carga) · tabla_0x67366C[tipo] >> 12                 (635, 693, 520, 578)
salud = clamp(130 · salud [+0x18] / salud máxima [+0x14] + 165, 204, 256)
velocidad = salud · base >> 10
```

Una Snaikka vacía y sana: 256 · 635 >> 10 = 158.

0x517410(velocidad, x, y): con `dx, dy` desde la parte entera de la
posición, `pasos = 1 + trunc(128 · √(dx² + dy²) / velocidad)` (en double con la
constante −128.0 de 0x673678) y desplazamiento = `(d << 16 ± 0x8000) / pasos`
(+0x8000 si d > 0, −0x7FFF si d < 0; 0 si d = 0). El rumbo [+0x1] sale del
cuadrante (bases 1, 5, 9, 13) más 0–4 según la razón entre |dx| y |dy|
(umbrales 0x3244/65536 ≈ tan 11° y 0x96CC/65536 ≈ tan 30°): con `c` y `e` las
componentes según el cuadrante (dx > 0 y dy > 0: base 9, c = dx, e = dy;
dx > 0 y dy ≤ 0: base 5, c = |dy|, e = dx; dx ≤ 0 y dy > 0: base 13, c = dy,
e = |dx|; si no, base 1, c = |dx|, e = |dy|), si c > e suma 2 si c · 0x96CC < e ≪ 16,
1 si c · 0x3244 < e ≪ 16 y 0 si no; si c ≤ e, 2, 3 o 4 con los papeles cambiados.

Devuelve 0 cuando el barco llega (o se hunde) y 1 mientras navega; en el
primer caso el llamador lo atraca (0x506d8e) o lo hunde (estado 0x11).

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
navega entre dos ciudades no tiene ruta propia: al cargarlo, 0x445010 ve que
los dos extremos son ciudades (0x446d50) y le da el puntero de la matriz, con
el primer punto cambiado por el de salida; el campo [+0x36] indica si la ruta
es suya (solo entonces se guarda y se libera).

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
