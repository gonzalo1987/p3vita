# Mapas de ciudad (.town) y tiles (.let)

Base: [P3Modding, Town Map](https://p3modding.github.io/file-formats/town.html)
y [Isometric Tiles](https://p3modding.github.io/file-formats/let.html). Aquí se
recoge lo comprobado al dibujar las ciudades con `tools/p3town` y la
implementación de `src/p3data/town.cpp`, incluidos **tres puntos en los que la
documentación de P3Modding no es correcta** (marcados con ⚠).

## .town

`iso/towns/<id>.town`, 231 450 bytes: a 10-byte header (`u16 256`,
`u16 width = 128`, `u16 0`, `u16 height = 226`, `u16 0`; the game reads it as
`u8, u8, u32 width, u32 height`), two layers of `128 × 226` cells of 4 bytes
(ground and objects) and 16 final bytes: the first 8 unidentified; the last 8,
two `s32` that a town battle reads (0x58E760, at `width · height · 8 + 0x12`,
from the map `n | 0x80` of the attacked town: 128–155, and the pirate nests
251–254; 0 in the templates 0–27). They look like the way to the sea:
(0, 140), (−82, 0)... Every map is 128 × 226 (the town view's size,
`SaveGame::TownView`). Each cell is `u16 tile` (a `.let` id, 0 = empty) and
`u16 aux`.

Los mapas `0..27` son plantillas (`scripts/StadtBild.ini`: "la numeración de
los mapas corresponde a la de los ficheros town"); varias ciudades comparten
plantilla.

`iso/towns/<id>.editor` (todos los mapas menos el 30) tiene el mismo formato y
el mismo suelo, pero su capa de objetos incluye **todo lo que colocó el
diseñador**: los edificios de la ciudad, los tres anillos de muralla de las
ampliaciones a la vez y marcadores que solo ve el editor: tiles de tipo 48
(nodos de rutas: `*_node`, `nodeschiff`...) y 49 (puntos de aparición:
`*_spawner`, `spawnwaldtier`...), que no se dibujan. Es un superconjunto del
estado de una partida, no el comienzo de una.

### ⚠ Proyección: isométrica escalonada

P3Modding da una proyección en rombo (`(x − y)·17, (x + y)·11`). Dibujada así,
la ciudad no se parece a su miniatura. La correcta es **escalonada**: las filas
impares van desplazadas media celda.

```
pantalla_x = x · 34 + (y & 1) · 17
pantalla_y = y · 11
```

Cada celda es un rombo de 34 × 21 (filas 0 a 20, la 10 es la más ancha), y
`DownVector=17 11` de `scripts/iso.ini` es justo el paso de una fila a la
siguiente. La ciudad entera mide 4369 × 2496 píxeles más lo que sobresalgan los
sprites; la miniatura `iso/towns/<id>.aim` (272 × 155) es esa imagen a 1/16, y
los nodos (`.nodes`) usan las mismas coordenadas de píxel (X hasta 4352 =
128 · 34).

Vecinos de `(x, y)` en esta rejilla: arriba-izquierda `(x − 1 + (y & 1), y − 1)`,
arriba-derecha `(x + (y & 1), y − 1)`, abajo-izquierda `(x − 1 + (y & 1), y + 1)`,
abajo-derecha `(x + (y & 1), y + 1)`.

### ⚠ El byte bajo de `aux` es el índice de franja

Un tile de `cols × rows` celdas tiene `cols + rows − 1` **franjas** verticales
(la tabla de fotogramas del `.let`). La celda delantera lleva la franja más
baja, de 34 de ancho; las del borde izquierdo y derecho de la huella llevan las
de 17. El byte bajo de `aux` dice qué franja va en cada celda; `0xFF` marca las
celdas interiores o traseras, que no dibujan nada.

Ejemplo, un barril de 2 × 2 (`fass.let`): arriba `0x01FF`, izquierda `0x0000`,
derecha `0x0102` y delante `0x0101`.

Vale igual para la capa de suelo: lo que P3Modding llama "pendiente 1–6" son las
franjas de los acantilados de 4 × 4 (7 franjas), y sus `0xFF` son las 9 celdas
interiores de cada uno (1776 en la ciudad 0 ≈ 9 × 197). Los tiles de una sola
franja ignoran `aux`. El byte alto (¿elevación?) no afecta al dibujo.

Colocación de la franja `k` en la celda `(x, y)`, con `f` la franja delantera
(la de borde inferior más bajo) y `(sx, sy, w, h)` la franja relativa al
rectángulo del tile en el atlas:

```
destino_x = k <= f ? celda_x : celda_x + 34 − w     // izquierda / derecha
destino_y = celda_y + 21 − h                           // apoyada en el borde inferior
origen    = (src_x + sx, src_y + sy, w, h)
```

Así cada franja se dibuja con la profundidad de su propia celda, y basta pintar
cada capa fila a fila de arriba abajo: primero todo el suelo y luego todos los
objetos. `tests/town_test.cpp` comprueba que las tres franjas del barril forman
un único sprite.

### ⚠ Ids repetidos: gana el último

Hay 11 ids compartidos por dos `.let`. Solo se usan en mapas los 273–276
(`strasse_rand_*` / `wege_stadt02_*`), el 549 (`baum4_1x2` / `strasse_rand_neu2`)
y el 570 (`baum14_1x2` / `strasse_rand_neu3`), siempre en la capa de suelo. Con
el primero del archivo salen árboles (y rombos negros) en los bordes de las
plazas; con **el último del archivo** los bordes encajan.

## .let

Cabecera de 0x28 bytes como la describe P3Modding (`'R'` en 0x01, id en 0x02,
tipo en 0x04, color clave en 0x08, rectángulo del atlas en 0x0C/0x10/0x14/0x18,
`cols` en 0x1C y `rows` en 0x20, número de entradas extra en 0x24), luego
`u32[extra]`, las `cols + rows − 1` franjas (`s32 x, y, ancho, alto` en
coordenadas del atlas) y la ruta del atlas AIM (`u32 longitud` + texto con
separadores mezclados `\` y `/`; su color clave es el de la cabecera).

Tras la ruta, las variantes (0x5513F0, cada una leída por 0x5852A0): `u32`
cuántas y, cada una, `u32 clase` (0 la sombra, el `[data+0x14]` del juego; 1 la
nieve, `[+0x18]`; 2 los fotogramas de los andamios, `[+0x1C]`; 3 a 8 las de la
clase del objeto), `u32 n` y `u32[n]` parámetros, `u32 x, y` (su sitio en las
coordenadas del atlas del tile; el juego los guarda en u16), `u32 m` y `m`
rectángulos `u32 x, y, ancho, alto` en su propio atlas, `u32 longitud` y, si no es
0, la ruta de su AIM y su `u32` color clave (sin ruta, el atlas del tile). Leído
así, cada uno de los 934 `.let` acaba justo en su último byte (15-11-2026,
`re/emu/sesion38/letvariants.py`): 358 sombras (un rectángulo cada una), 175
nieves, 3083 de la clase 2, 393 de la 3 y unas pocas de la 4 a la 8. `TileDef::variants`.

La sombra se dibuja (0x551F70) en negro con alfa 100 (el color 0x64000000 de
quien la llama), su primer rectángulo con su `x, y` puestos desde la franja
`cols − 1` del tile: x de la celda + x − x de la franja, y de abajo + y − y de la
franja − alto de la franja.

El color clave es siempre `0xFFFF00FF`: los píxeles magenta `FF00FF` del atlas
son transparentes.
