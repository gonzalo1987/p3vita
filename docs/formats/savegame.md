# Partidas de Patrician III: ciudades y edificios

Ingeniería inversa estática de `Patrician3.exe` (GOG 1.1 inglés, MD5
`baf7e0f931ce3d35c1ba3f5d81eb990a`) y de las partidas de `missav/`. Todas las
direcciones son VA del exe; los offsets de fichero se refieren al volcado ya
descomprimido. Marcas usadas:

- **[código]**: leído en el desensamblado (función citada).
- **[datos]**: comprobado sobre las partidas/ficheros del juego (con el script citado).
- **[supuesto]**: inferencia razonable que no he podido cerrar.

> Implementación en p3vita: `src/p3data/lz.cpp` (descompresión),
> `src/p3data/savegame.cpp` (§2–§6 y §11–§13, solo la versión 0x82),
> `src/p3data/thresholds.cpp` (§4.1), `src/p3data/trade.cpp` (§12.3) y
> `src/p3data/town_buildings.cpp` (§8). Comerciantes, oficinas, barcos,
> umbrales de precio y comercio están comprobados contra el propio juego con
> `tools/oracle/save_oracle.py` en las 12 partidas v0x82 de `missav/` (ver
> [../oracle.md](../oracle.md)). Sus tablas se han comprobado byte a byte
> contra el ejecutable, y las 72 ciudades de `1.pat`, `3.pat` y `tut0.pat` salen
> idénticas, celda a celda, a las de la referencia en Python escrita durante la
> ingeniería inversa. Esa referencia y los scripts de análisis que se citan
> (`p3save.py`, `ana_*.py`, `check_editor.py`...) no están en el repositorio.

## 1. Contenedor, nombres y versiones

- `.pat`, `.pst` y `.gmf` son un único blob LZ (el del formato AIM): `s32 n` + flujo de bits.
- Al cargar una partida `<nombre>` el exe (0x5476e0) lee **dos** ficheros **[código]**:
  `<carpeta><nombre>.pat` → descomprime → `0x549f40` (estado del juego), y
  `<carpeta><nombre>.pst` → `0x54da60` (objeto global 0x6dfc90). Carpetas: `Save\Ein\`,
  `Save\Kam\`, `Save\Mehr\`, `MISSAV\`.
- La primera palabra del `.pat` es `u32 = 0xAFFE | versión << 16` **[código]** (`0x549f40`:
  `shr edi,0x10`). En `missav/`: `1..7.pat` y `tut0..4.pat` son **v0x82**; `0100..0204.pat`
  son **v0x4D** (no soportadas, ver §14). Todo lo que sigue es para v0x82; el código tiene ramas para versiones anteriores
  que `p3save.py` replica en parte.
- Los `.pst` son otra cosa (§10): no contienen edificios.

## 2. Estructura global de un `.pat` v0x82 (0x549f40)

Secuencia de lectura **[código]**; los separadores son `u32 0x0000AFFE` que el exe lee y
descarta. Rangos de `1.pat` **[datos]** (los 8 separadores están en 0x5c, 882292, 935806,
946570, 946578, 955072, 955081 y 955480):

| Offset (1.pat) | Contenido | Función |
|-|-|-|
| 0x00 | `u32` magia+versión (`fe af 82 00`) | 0x549f40 |
| 0x04 | ajustes de partida, 0x4C bytes (v≥0x7A; 0x3C si v≥0x76, 0x28 antes); a 0x2B va el nombre de la ciudad natal (`Lübeck`); el byte +0x10 es la **dificultad del comercio** (abajo) | → `*0x6cc3e8` |
| 0x50 | `u32` [+0x8d4], `u32` [+0x8d8], `u32` [+0x8dc] (este último si v≥0x5F) | |
| 0x5C | separador | |
| 0x60 | **mundo** (objeto 0x6de4a0): cabecera (§3), 24 ciudades (§4) hasta 0xBE66E y el resto del mundo hasta 882292 | 0x52fb60 |
| 882296 | barcos (§12) | 0x504230 (0x6dd7a0) |
| 935810 | encargos/ofertas (textos "Schmuggler", "Kurier"...) | 0x4d6350 (0x6dd730) |
| 946574 | 4 bytes | 0x4d4bc0 (0x6dd728) |
| 946582 | 8490 bytes sin identificar | 0x4d81e0 (0x6dd73c) |
| 955076 | 5 bytes | 0x611ae0 (0x6e55d0) |
| 955085 | tabla de nombres (§13) | 0x511e30 (0x6ddaa0) |
| 955480 | separador final | |

Dificultad del comercio **[código + oráculo]**: al cargar, 0x549f40 llama a 0x52e1a0 con el byte
+0x10 de los ajustes (offset 0x14 del fichero), que fija el factor de venta con poco almacén
(0x6DE3D8+0x64, tabla 0x6739FC): 0 → 2,2 (baja), 1 → 2,0 (normal), 2 o más → 1,8 (alta). En
`missav/` valen 0 `1.pat`, `2.pat` y los tutoriales, y 1 de `3.pat` a `7.pat`.

Dentro del mundo, tras las ciudades **[código, 0x52fb60]**: `n·n` bytes (n = nº de
ciudades; ¿distancias?), `u32` (v≥0x5A), n objetos de 0x1024 bytes en memoria
(0x633730, guardados con formato propio), `[+6]` registros de 0x14 (0x4d32f0), `[+8]`
oficinas/kontors (0x4feff0, tamaño variable: 6 bytes si están libres), `[+0xa]`
comerciantes (0x4f4100), 0x5351d0, 15 × 0x52bbe0, un objeto opcional (0x533da0) y un byte.
Formato en §11.

## 3. Cabecera del mundo (0x52fb60), desde 0x60 en 1.pat

**[código]** salvo donde se indica; valores de 1.pat.

| Off | Tipo | Campo (offset en 0x6de4a0) | 1.pat |
|-|-|-|-|
| 0x00 | u8, u8 | [+0] **día** (1–31), [+1] **mes** (0 = enero; lo usan los umbrales de precio, §4.1) | 13, 4 |
| 0x02 | u16 | [+2] **año** | 1300 |
| 0x04 | u16 | [+4] día del año | 132 |
| 0x06 | u32 | [+0x10] **nº de ciudades** | 24 |
| 0x0A | u16 ×3 | [+6] nº registros 0x14, [+8] **nº de oficinas**, [+0xa] **nº de comerciantes** | 0, 64, 37 |
| (v<0x5E: +4 bytes) | | | |
| 0x10 | u8 ×4 | [+0x89] bool, [+0x8c], [+0x8a] (=1 si el byte es 0x7F), [+0x8b] | |
| 0x14 | u32 ×2 | [+0xc], [+0x14] **tick**: 256 por día desde el año 0, de 365 días; la fecha sale de él (navigation.md §4) | , 121506024 |
| 0x1C | u16 ×2 | [+0x7c], [+0x7e] | 0, 37 |
| 0x20 | u8[40] | [+0x18] **id de ciudad (0–39) de cada índice** | 0 2 4 5 8 9 … 39 |
| 0x48 | u8[40] | [+0x40] **plantilla `iso/towns/<id>` de cada índice** | 19 20 15 14 21 … 9 |
| 0x70 | u8, u8 | [+0x8d], [+0x8e] | 0xFF, 10 |
| 0x72 | 7 × 12 bytes | [+0x90] | |

Las dos tablas de 40 bytes son las mismas que lleva `maps/Standard24.gmf` (cuenta 24,
ids y permutación). Que [+0x40] es la plantilla está probado **[datos]**: en las 24 ciudades
de 1.pat el `stadtplatz<plantilla>.tow` coincide con la rejilla guardada (§9) y los
edificios únicos caen en anclas del `.editor` de esa plantilla (§8.6); `2.pat` (21 ciudades)
usa otra tabla (`2 1 18 18 18 18 2 0 …`), así que la asignación va en cada partida.

## 4. Bloque de ciudad (0x51b520)

Las ciudades van seguidas, sin cabecera ni tamaño. Cada una son cinco partes separadas
por un byte que el exe salta sin mirar (`inc eax`; en v0x82 siempre vale `0x7F`, que el
script usa como comprobación) **[código]**. En 1.pat empiezan en 0x126, 0x8003, 0xFEE0…
y miden 32 477 + 8·(N−128) + 16·M bytes (N registros de edificio, M nodos) **[datos]**:
1.pat tiene bloques de 32 477/32 493/32 509 y 3.pat de 34 525/34 541/35 565 (N = 384 o 512).

Offsets relativos al inicio del bloque (v0x82). Entre corchetes, el offset en el
`struct town` de 0x9F8 bytes de P3Modding, que coincide con todo lo identificado allí:

| Off | Tamaño | Contenido |
|-|-|-|
| +0x000 | 1 | separador |
| +0x001 | 0x2C0 | almacén (0x52df20, `struct storage` de P3Modding): `u32`, 24 `u32` **existencias**, 24 `u32` **consumo diario de las empresas**, 24 `u32` **producción diaria**, 0x18 armas, 0x180 series, `u32` [0x000–0x2bf]. Unidades internas: barril = 200, carga = 2000, pieza de armamento = 10 (`src/p3data/wares.hpp`) |
| +0x2C1 | 0x414 | parte 1 (0x527ac0): `u8` índice [0x2c0], `u8` **id de ciudad** [0x2c1], [0x2c2], [0x2c3]; `u16` [0x2c4], `u16` impuesto [0x2c6], `u32` flags [0x2c8], [0x2cc], [0x2d0], `u32` **habitantes** [0x2d4], `4×u32` por clase [0x2d8], … , 24 `u32` **consumo diario de los habitantes** [0x310], 24 `s32` **mercancía en camino** [0x370] (navigation.md §6), … , 24 `u32` [0x490] (el "array sin identificar" de los umbrales de P3Modding; ≈ producción), 24 × 4 `u32` **umbrales de precio t0–t3** [0x4F0] (ver abajo), 24 `s32` **mercancía en camino** que no baja de 0 [0x670], `u32` [0x6d0] |
| +0x6D5 | 1 | separador |
| +0x6D6 | 20 | parte 2 (0x525e30): [0x82c–0x83f] |
| +0x6EA | 1 | separador |
| +0x6EB | 52 | parte 3, cabecera (0x51ec10): ver §5 |
| +0x71F | 16 | iglesia (0x4fe240) [0x794–0x7a3]; el **último byte [0x7a3] es el nivel de la iglesia** (§8.3) |
| +0x72F | 8·N | **registros de edificio** (§5) |
| +0x72F+8N | 100 | `town_class1` [0x7a4] (0x4f9770): `u32 ancho=128`, `u32 alto=226` y 92 bytes de posiciones (§6) |
| … | 128·226 | **rejilla** (§7) |
| | 1 + 26 | separador + parte 4 (0x52a2b0) [0x810–0x82b] |
| | 1 + 136 | separador + parte 5 (0x528870) [0x6d4–0x75b]; sus primeros 20 bytes [0x6d4] contienen `ff ff ff ff 03 02 02 02 06 05 04 04`, la firma que se repetía en muchos bloques |
| | 1 + 21·16 | separador + 21 instalaciones (0x510070, `struct facility` de P3Modding) [0x840–0x98f] |
| | 85 + 16·M | [0x9a4] 16, [0x990] 4, [0x994] 2, [0x996] 2, 4×(1+1) [0x998/0x99c], [0x9a0] 2, [0x9a2] 2, [0x9b8] 4, [0x9bc] 1, [0x9bd] 1, [0x9c0] 4, [0x9c4] 1, [0x9c6] 18, [0x9d8] 18, `u16 M` y M nodos de 16 bytes (0x50fd40: `u32`, `u32`, `u16` BE, `u16` BE, `u8`, `u8`, `s16` BE) |

### 4.1 Umbrales de precio **[código + oráculo]**

El bloque que P3Modding llama `consumption_data` ([0x4F0], 24 × 16 bytes) son
los cuatro umbrales de precio de cada mercancía (t0 < t1 < t2 < t3: las
existencias a partir de las que el precio baja de tramo), tal como los dejó
`update_town_price_thresholds` (0x528070) en el último tick de la ciudad.

**Pero el juego no usa los guardados**: el lector de la parte 1 (0x527ac0)
llama a 0x528070 al terminar (0x527cc1), con parámetro `t0 de grano > 1`
(con 0 solo pone {1, 2, 3, 4} en las 19 primeras mercancías). En ese momento
el resto de la ciudad aún no está leído y conserva el valor del constructor;
en particular, el u16 de la instalación 18 (campo [0x968]) vale 0, así que los
ladrillos siguen siempre la rama «sin fábrica» al cargar. En 1.pat el recálculo
da lo mismo que lo guardado, pero no siempre: en la ciudad 7 de 2.pat los
ladrillos pasan de (80000, 160000, 163618, 167236) a (80000, 160000, 196180,
436180) y su precio de compra cambia de 47 a 74 por carga hasta el siguiente tick.

La función, leída en el ejecutable (`src/p3data/thresholds.cpp`), completa el
pseudocódigo de P3Modding. Con `c = consumo de empresas + consumo de habitantes + 1`,
`e = 2` si la ciudad está en crisis y 1 si no, y `s` = array sin identificar
de [0x490] (P3Modding: «weird prods»):

- Situación ([0x2C8]): crisis (`& 0x4000A10`: asedio, bloqueo, piratas o
  hielo) → t1 = 28 días en vez de 14, `e = 2` y más demanda en las mercancías
  de la máscara 0x8FF (grano–vino y madera); si no, incendio (`& 0x8000`, 0x8FF),
  hambruna (`& 0x1000`, 0xFF) o peste (`& 0x8`, 0x7F). Las mercancías con más
  demanda tienen t0 = 14 días en vez de 7.
- t0 y t1 de las 19 primeras: días × c; ladrillos y armas empiezan en 0; el
  grano suma otros t1 días.
- Cosecha (mes [+1] del mundo): de septiembre a diciembre, t1 de grano y vino
  sube 3/20, 3/10, 1/5 y 1/10.
- Materiales: ladrillos t0 += 80000e, t1 += 160000e; madera += 42000/84000;
  brea += 1800/3600; cáñamo += 5000e/10000e; herramientas += 2000e/4000e.
- Armas (20–23): `a = habitantes · 10 / 200` (doble si `[0x2C8] & 0x810`),
  `b = max(7 · consumo de habitantes, 10)`; t0 = b + a, t1 = max(3b + 2a, 50),
  t2 = t1 + 10 · (producción + 1), t3 = t2 + b + a.
- Las 20 mercancías de comercio: t2 = t1 + 10 · s, t3 = t2 + t0 (también los
  ladrillos); +3600 a t3 de la brea y +160000 a t3 de los ladrillos si se
  producen; telas +400/800/800/1600.
- Ladrillos: si [0x968] > 300, t2 = t1 + s y t3 = t2 + s; si no, si t2 > 2·t1,
  t2 = 2·t1 y t3 = 3·t1.
- Mínimos (20 mercancías): t0 ≥ m y t_i ≥ (i + 1)·m, cada umbral por encima del
  anterior (si no, anterior + m), con m = 1000 por barriles y 10000 por cargas.
- No reproducido: durante un asedio, t0/t1 de la brea suben 7/14 veces el
  consumo de brea del asedio (0x636920, solo si existe el objeto del asedio en
  0x6df270, que al cargar aún no existe).

Cobertura de las partidas de `missav/`: hay crisis, hambruna y los meses 8–10;
no hay incendio, peste sola ni diciembre.

Los precios (`src/p3data/prices.cpp`) reproducen `get_buy_price` y
`get_sell_price` del ejecutable y coinciden al entero con ellas, comprobado
ejecutándolas en un emulador de CPU (ver [../oracle.md](../oracle.md)). La
mercancía con recargo de la ciudad es el byte [0x6F0] (parte 5 del bloque);
0xFF = ninguna.

## 5. Edificios: cabecera de la parte 3 y registros de 8 bytes

Cabecera (52 bytes, en orden de lectura) **[código, 0x51ec10]**:

| Off | Tipo | Campo |
|-|-|-|
| 0 | u16 | [0x776] **N = nº de registros** (capacidad; 128, 384, 512…) |
| 2 | u16 ×6 | cabezas de lista: [0x760] **libres**, [0x762], [0x764] **edificios**, [0x76a], [0x768] **fortificación**, [0x766] |
| 14 | u8 ×3 | [0x774], [0x771], [0x770] |
| 17 | u32 | [0x76c] **flags de la ciudad** (bit 0x80 astillero de reparación, 0x20000 astillero, 0x100 puestos de mercado, ≠0 grúas; ver §8.5) |
| 21 | u16 | [0x784] primera oficina de la ciudad |
| 23 | u8 ×5 | [0x773], [0x772], [0x789] pozos, [0x788], [0x78a] |
| 28 | u16 ×5 | [0x78c], [0x78e], [0x786], [0x790] calles construidas, [0x792] calles totales |
| 38 | u8 ×2 | [0x775], [0x78b] |
| 40 | u16 ×3 ×2 | [0x778], [0x77e] |

Registro (array [0x75c], 8 bytes) **[código]** para el formato, **[datos]** para el significado:

| Off | Tipo | Campo |
|-|-|-|
| 0 | u8 | x (columna 0–127) de la **celda ancla** = celda delantera (inferior) del edificio |
| 1 | u8 | y (fila 0–225) |
| 2 | u8 | **propietario**: ≥ nº de comerciantes (0xFF) = la ciudad; si no, índice del comerciante (en murallas aparecen también 0xFE/0xFD, sin aclarar) |
| 3 | u8 | **tipo** (0x00–0x38, tabla §8.1) |
| 4 | u16 | siguiente de la lista (≥ N termina) |
| 6 | u8 | obra pendiente: ≠0 = en construcción; el exe dibuja andamios con progreso `tabla_0x672bb4[tipo] − b6` (0x521310) |
| 7 | u8 | parámetro: dirección/orientación (bits 7–5: 0x00 N, 0x20 NE, 0x40 E, 0x60 SE, 0x80 S, 0xA0 SO, 0xC0 O, 0xE0 NO, según 0x58b740), bit 0 = final de muralla |

Libres: x = y = b2 = tipo = 0xFF, encadenados desde [0x760]. Al cargar, si no hay ningún
tipo 0x2D y `[0x76c] & 1`, el exe añade uno en la posición de la estatua (§6) **[código]**.
Para versiones < 0x69/0x67/0x59 el exe corrige los tipos (+1/+2): la numeración cambió.

Listas **[código, 0x521580]**: el exe dibuja las listas [0x762], [0x764], [0x768] y
[0x76a]; **no** dibuja [0x760] (libres) ni [0x766]. **[datos]**: [0x764] tiene las casas,
edificios municipales, empresas, pozos y almacenes; [0x768] murallas, puertas y torres;
[0x766] (vacía en 1.pat y tut0.pat) tiene en 3.pat murallas y puertas antiguas.

## 6. `town_class1`: posiciones fijas (92 bytes)

Pares `(x, y)` de celdas ancla, 0xFF = no hay **[código, 0x4f9770 y 0x521580]**. En las 24
ciudades de 1.pat cada punto válido es exactamente el ancla de un objeto del `.editor` de la
plantilla del tipo indicado **[datos, ana_points.py]**:

| Campo (offset en town) | Contenido | Comprobación |
|-|-|-|
| `a` 4 pares [0x7b0] | puntos de muralla (cambian al construir) | anclas de `mauer*`/`mauerende*` en 48/48 |
| `b` 4 pares [0x7b8] | **faros** (.let tipo 0x15 `leuchtturm`) | 53/53 |
| `c` 8 pares [0x7c0] | **grúas** (.let tipo 0x16, 6 modelos) | 90/90 |
| `pts16` 16 pares [0x7d0] | **puestos de mercado** (.let tipo 6) | 250/250 |
| `d` 16 bytes [0x7f0] | modelo de cada puesto = `ext[1]` del .let tipo 6 (0 pescado … 7 salchichas) | 250/250 |
| `e` [0x800] | estatua (x,y), kontor del jugador (x,y), kontor rival (x,y), solar del baño (x,y) | 24/24 cada uno |
| `f` [0x808] (v>0x68) | **astillero** (x,y), **astillero de reparación** (x,y) | 24/24 |

## 7. La rejilla por celda

`w·h` bytes con **índice `x·226 + y`** (por columnas): el exe accede como
`grid[[+0x7ac]·x + y]` (0x47ba9e) **[código]**. 6 bits bajos = clase de celda; bits altos =
marcas. Es un mapa de **zonas y solares**, no de edificios: no basta para dibujar la ciudad.

Clases comprobadas en el código (0x4fcbd0 "¿es (x,y) solar para el tipo T?", 0x47b9xx) y
en los datos (valor del `.tow` en el ancla de cada edificio de 1.pat y tut0.pat, `ana_tow2.py`):

| Clase (byte) | Uso |
|-|-|
| 0x13 (0x93) | estatua (tipo 0x2D) |
| 0x14 (0x94) | arsenal 0x21 · 0x15 (0x95) prestamista 0x25 · 0x16 (0x96) oficina municipal 0x23 · 0x17 (0x97) taberna 0x24 · 0x18 (0x98) comercio rural 0x27 · 0x19 (0x99) iglesia 0x1F · 0x1A (0x9a) gremio 0x22 · 0x1B (0x9b) ayuntamiento 0x20 · 0x1C (0x9c) torre 0x30/0x31 · 0x1D (0x9d) herrería 0x03 · 0x28 (0xa8) baño 0x26 |
| 0x0E | cabaña de pescador (0x05) |
| 0x04 (0x84) | pozo (0x28) |
| 0x0A / 0x09 | muralla del primer anillo / anillos de ampliación (en el `.tow`); 0x05 = muralla ya construida |
| 0x07 / 0x08 | puertas 0x37 / 0x38 |
| 0x0F | muelle `maulwurf` (0x2F) |
| 0xA0–0xA2 | zonas de casas (en el `.tow`); al construir pasan a 0x8B |
| 0x8B | celda ocupada por un edificio (en la partida) |
| 0x81 | el exe pone un borde de calle aleatorio (.let tipo 0x17) **[código]** (ninguna en 1.pat) |

Observadas sin código **[datos]**: 0x0C agua, 0x0D acantilado/muelle/pasarelas, 0x03 y
0x80/0x83 calles y plaza, 0x0B astillero y faros, 0x1E zona de pescadores, 0x9F celdas
bloqueadas (= `.locked` 1), 0x20/0x3B y 0x21/0x22 zonas de granjas, 0x04/0x06/0x10/0x11/0x20
tipos de prado.

## 8. Cómo dibuja el juego una ciudad y cómo pasarlo a la capa de objetos

0x521580 monta la vista isométrica de la ciudad: los `.town` dan suelo y objetos naturales;
encima añade los objetos de §8.1–8.5 con 0x590680(x, y, tipo_let, parámetro, propietario).

### 8.1 Tipo de edificio → tipo de `.let`

El campo **tipo** del `.let` (offset 4) es la "NewBuildingId" de P3Modding. La tabla
**0x672fc0** (57 bytes) traduce el tipo del registro **[código + datos]**:

| Tipo | .let tipo | .let | Tipo | .let tipo | .let |
|-|-|-|-|-|-|
| 0x00 | 0x2C | spielerkontor* | 0x1F | 0x04 | kircheR*_* |
| 0x01 | 0x1B | repwerft | 0x20 | 0x03 | rathaus* |
| 0x03 | 0x08 | 8bitkomplettstadtschmiede | 0x21 | 0x0B | arsenal |
| 0x04–0x14 | 0x5D, 0x57, 0x58, 0x5B, 0x59, 0x53, 0x56, 0x64, 0x5A, 0x5E, 0x60, 0x55, 0x5C, 0x63, 0x62, 0x61, 0x54 | empresas (jagdhuette, fischerhütte, brauerei, schmiede02, imkerei, getreidehof, viehof, saegewerk, weberei, siederei, gießerei, schafstall, weingut, tongrube, steinbruch, pechsiederei, hanfhof) | 0x22 | 0x07 | gilde |
| 0x15–0x17 | 0x51 | casas ricas (reich*) | 0x23 | 0x00 | stadtkontor |
| 0x18–0x1A | 0x50 | casas acomodadas (mittelstand*) | 0x24 | 0x02 | kneipe |
| 0x1B–0x1D | 0x2A | casas pobres (armenhaus*) | 0x25 | 0x05 | darlehensgeber |
| 0x1E | 0x66 | lagerhaus (almacén) | 0x26 | 0x09 | badhaus |
| 0x28 | 0x52 | kurbelbrunnen (pozo) | 0x27 | 0x33 | landhandel |
| 0x29–0x2C | 0x65, 0x67, 0x68, 0x69 | hospital, muenze, elementarschule, kapelle_* | 0x2D | 0x37 | estatua / denkmal (§8.3) |
| 0x2E | 0x01 | werft | 0x2F | 0x5F | maulwurf* |
| 0x30–0x33 | 0x29 | torres (geschuetzturm, gturm_*) | 0x36 | 0x2E | muralla (mauer*, eckturm*) |
| 0x34, 0x35, 0x37, 0x38 | 0x2F | puertas (stadttor_*, mauertor_*) | 0x02 | 0x0B | ? (no visto) |

### 8.2 Parámetro (0x521310, tabla de casos 0x5214f4) **[código]**

`b7`: tipos 0x1E, 0x2F, 0x36 (y `(s8)b7` en granjas 0x04/0x08/0x09/0x0A/0x0F/0x10/0x14);
casas: `variante = (tipo − 0x15) % 3` (0 si b6≠0) y `b7 << 16`; iglesia: nivel = byte
[0x7a3]; ayuntamiento: 0–3 según habitantes (<2500, <5000, <10000, resto); estatua de la
ciudad: `tabla_0x672d28[id de ciudad]`; torres 0x31/0x33: 1, 0x30/0x32: 0; puertas
0x37/0x38: `b7 << 16 | 0xFF` (0x34/0x35: `| 0x1FF`); el resto, 0.

### 8.3 Elección del `.let` dentro del tipo

Los parámetros extra del `.let` (`u32[extra]` en 0x28) codifican variante, orientación y
**estilo regional**. La región es `Region=` de `scripts/StadtDaten.ini` de la ciudad: el exe
la carga en 0x6ddb90 + id·0x34 + 0x30 (0x5121c0/0x51e440) y la copia a `[iso+0x570]`
(0x58a3fd), de donde la leen el buscador 0x5928a0 y las fábricas de muralla (0x582130) e
iglesia (`p = región·4 + nivel`) **[código]**. Si no hay juego para la región se usa la 2
(tablas 0x6e4080, 0x6e3430… en 0x5928a0) **[código]**.

| .let tipo | ext | Regla | Estado |
|-|-|-|-|
| 0x2A/0x50/0x51 casas | [0, variante, orient 32/224, región] | variante y `b7`; región (0 engl*, 1 schweden*, 2 alemán, 4 russland*; 3 → 2) | **[código]** (0x592be5: `p/9 == región`, función de variante/orientación) |
| 0x04 iglesia | [0, región, nivel] | kircheR{región+1}_{nivel+1} | **[código]** |
| 0x03 ayuntamiento | [0, región] | por región (el parámetro 0–3 no selecciona `.let`) | **[datos]** 21/24 plantillas; **[supuesto]** que el juego siga la región en las otras 3 |
| 0x2E muralla | [0, b7, región] | mauer1/2, eckturm1–4, mauerende*; región 0 `_r3`, 1 `_r4`, 4 `_r5` | **[código]** |
| 0x2F puerta | [0, pieza, orient, región] | 3 piezas (0 derecha, 1 centro, 2 izquierda); la pieza k va 2·k pasos en la dirección `b7` desde el ancla (0x582650 + 0x58b740) | **[código]** + **[datos]** 9/9 piezas en Lübeck |
| 0x29 torre | [0, región, clase] | clase 0 cañones / 1 bombardas | **[datos]** + nombres |
| 0x37 estatua | t10 [0, índice] | `statue_*` con `ext[1] = tabla_0x672d28[id]` | **[supuesto]**: el registro pide .let tipo 0x37 (`denkmal_*`, ext 0–5) y no he cerrado cómo resuelve el exe índices ≥6; coincide con el `.editor` en 20/24 plantillas (las 20–23 usan `brunnenstatue`) |
| 0x5F muelle, 0x66 almacén | [0, b7] | por `b7` | **[código]** (0x592c68) / **[datos]** |
| 0x69 capilla | [0, x] | x por región (polen 0, schweden 1, england 2, deutschland 3, russland 4) | **[supuesto]** por los nombres |
| 0x16 grúa | [0] ×6 | el `.let` que tenga el `.editor` en ese ancla | **[supuesto]** (90/90 grúas tienen objeto en el `.editor`) |
| resto | [0] o [0,…,0] | el único `.let` del tipo (o el de `ext[2] = 0`) | **[datos]** |

### 8.4 Huella, ancla y franjas **[datos]**

La celda guardada es la **delantera**. Para un `.let` de `cols × rows` la huella sube `cols`
celdas hacia arriba-izquierda y `rows` hacia arriba-derecha; la franja (byte bajo de `aux`)
es `cols−1−i` en el borde izquierdo (i pasos arriba-izquierda), `cols−1+j` en el derecho
(j pasos arriba-derecha) y `0xFF` en el interior (comprobado con ayuntamiento 6×4,
gremio 3×4, casas 3×2/2×3). Pasos en la rejilla escalonada: arriba-izquierda
`(x−1+(y&1), y−1)`, arriba-derecha `(x+(y&1), y−1)`.

### 8.5 Objetos fijos (0x521580) **[código]**

Faros en `b` (siempre); grúas en `c` si `[0x76c] ≠ 0`; puestos en `pts16` con modelo `d`
si `[0x76c] & 0x100`; astillero de reparación (`repwerft`) en `f[2..3]` si `[0x76c] & 0x80`
y, además, astillero (`werft`) en `f[0..1]` si `[0x76c] & 0x20000`; kontor del jugador
(.let tipo 0x2C, parámetro devuelto por 0x4f74f0) en `e[2..3]` si alguna oficina de la
ciudad es del jugador (comerciante `*0x6dfc14`), y kontor rival (`gegnerkontor`, tipo 0x2D) en
`e[4..5]` si hay oficinas de otros comerciantes (recorre las oficinas desde [0x784] por su
campo +0x2ca; propietario en +0x2c4); bordes de calle en celdas 0x81.

### 8.6 Conversión a `.town` (save_towns.py) y comprobación

Cabecera, suelo y 16 bytes finales del `.town` de la plantilla; capa 1 = objetos del `.town`
+ objetos de §8.1–8.5 escritos con §8.4. Los objetos naturales que pisen una huella se quitan
enteros. El byte alto de `aux` se copia del `.editor` cuando tiene el mismo `.let` en esa
celda y si no se deja a 0 (no afecta al dibujo). Resultado **[datos, check_editor.py]**:

- 0 solapes entre objetos en las 72 ciudades de 1.pat, tut0.pat y 3.pat, ninguno fuera del mapa.
- 1.pat: de 2713 objetos, 1396 coinciden con el `.editor` (mismo `.let` y ancla): todos los
  edificios únicos, faros, grúas, puestos y astilleros; 1035 tienen otro `.let` del mismo tipo
  (847 murallas, 126 puertas y 14 torres con el juego de su región; 6 iglesias, 3
  ayuntamientos y 4 estatuas por la regla de región/tabla; 35 casas) y 282 (casas) no están en
  el `.editor`: el juego pone las casas en otros solares de la zona 0xA0–0xA2.
- Muralla: en 1.pat y tut0.pat solo existe el primer anillo. En 3.pat aparece un segundo
  anillo en obras (b6 ≠ 0), que el juego también dibuja (con andamios).

## 9. Partida nueva: `stadtplatz<plantilla>.tow`

0x4f9b40 abre `iso/towns/stadtplatz%.2u.tow` y lo pasa al mismo deserializador de §6–7
(0x4f9770) con versión 0x85 **[código]**: el fichero es `u32 128`, `u32 226`, **los 92 bytes
de posiciones** y después la rejilla (29 028 bytes; el orden es ese, no rejilla + 92). En las
24 ciudades de 1.pat el `.tow` de su plantilla coincide con lo guardado **[datos, ana_tow.py]**:
`b c pts16 d e f` idénticos; `a` cambia; en la rejilla cambian 379–634 celdas por ciudad,
casi todas 0x09/0x0A → 0x05 (muralla construida) y 0xA0/0x9E → 0x8B/0x82 (edificios).

Así que el estado inicial sale del `.tow`, no del `.editor`: cada edificio único tiene en el
`.tow` una celda marcada con su clase (§7) que es exactamente su ancla en las partidas
(en 1.pat y tut0.pat, todos salvo una herrería y un arsenal cuya celda ya vale 0x8B; dos
oficinas municipales y seis torres llevan la clase sin el bit 7), la muralla inicial sigue las celdas 0x0A, las
puertas 0x07/0x08, las torres 0x9C, el muelle 0x0F y los pozos 0x84 **[datos]**. Las casas
se reparten por las zonas 0xA0–0xA2 y no coinciden con las del `.editor` (**[supuesto]**: el
juego las genera según la población). No he localizado la función que crea los registros
iniciales, así que la orientación `b7` de cada tipo y la generación de casas quedan por
confirmar; los valores vistos son constantes por tipo (0x20 casi todos; 0xE0 arsenal,
taberna y puerta 0x38; 0x00 comercio rural, estatua, torre).

## 10. Los `.pst`

`<nombre>.pst` lo carga 0x54da60 en el objeto global 0x6dfc90 **[código]**: `u32`
`0xAFFE0000 | versión`, 6 `u16` (contadores de días/meses/años) y cadenas de bloques de
`k · 0x204` bytes (k series de 129 `u32`): son series de estadísticas para las gráficas, sin
edificios. Además, los `Tut*.pst` de `missav/` son versión 8 y el exe exige la 0xB: si no
coincide llama a 0x54ddf0 (vaciar) y no lee nada **[código + datos]**. Para el tutorial, la
ciudad sale de `tut0.pat`.

## 11. Resto del mundo: oficinas y comerciantes (0x52fb60)

Tras las ciudades, en v0x82 y en este orden **[código + oráculo]** (n = nº de ciudades):

| Contenido | Tamaño | Función |
|-|-|-|
| matriz n·n | n² bytes | |
| u32 | 4 | |
| n objetos de ciudad (0x1024 bytes en memoria) | 17 + n1·8 + n2·16 + 3 (+ opcional) | 0x633730 |
| registros de 0x14 ([+6] de la cabecera) | 20 cada uno | 0x4d32f0 |
| **oficinas** ([+8]) | 6 o 1100 | 0x4feff0 |
| **comerciantes** ([+0xa]) | 1552 + variable | 0x4f4100 |
| objeto del mundo + 0xe8 | 0xA00 + 8 + 40·k | 0x5351d0 |
| 15 objetos de 0x2C | 30 + 16·c | 0x52bbe0 |
| u8 ≠ 0 → objeto opcional | (no visto en ninguna partida) | 0x533da0 |
| u8 [+0x8f] | 1 | |

- 0x633730: `u32, u8, u16, u8, u8, 4 × (u8, u8)`, `u8 n1` y n1 × 8 bytes,
  `u8 n2` y n2 × 16 bytes, `u8` y, si no es 0, un objeto de 0x650 bytes en
  memoria (0x627ad0, de los asedios): `5 × u32, 7 × u16, 5 × u8, 5 × (u32, u32),
  u8 total, u8 primero` y, si primero ≠ 0xFF, (total − primero) × 8 bytes.
- 0x5351d0: 0xA00 bytes, 8 bytes y 40 bytes por cada uno de esos 8 que no sea 0xFF.
- 0x52bbe0 (v > 0x71): 8 × u8, u32, 8 bytes, 8 bytes, `u16 c` y c × 16 bytes.

### 11.1 Oficina (kontor)

`u16` dueño [+0x2c4]: **0xFFFF = libre**, y entonces solo siguen `u16`
[+0x2c8] (siguiente libre) y `u16` [+0x2ca]. Si no, 1100 bytes en total:

| Off | Tipo | Campo |
|-|-|-|
| 0 | u16 | [+0x2c4] **dueño** (índice de comerciante) |
| 2 | 704 | almacén, como el de las ciudades (§4): **existencias** en +4 |
| 706 | u32 | [+0x2c0] |
| 710 | u16 | [+0x2c8] |
| 712 | u8 | [+0x2c6] **ciudad** (índice en la partida) |
| 713 | u8 | [+0x2c7] |
| 714 | u16 | [+0x2ca] **siguiente oficina de la ciudad** (la primera es [0x784] de la ciudad, §5) |
| 716 | 384 | resto (0x4ff044: varios u16, 3 × 0x60, 6 × 8 bytes…) |

En 1.pat las 37 primeras están ocupadas, una por comerciante en su ciudad
natal, y las 27 restantes libres; todas las existencias valen 0.

### 11.2 Comerciante (0x4f4100 → 0x4f6fa0 → 0x4f6240)

1552 bytes fijos en v0x82 y, al final, 10 `u16` cuyos 15 bits bajos son la
longitud de otras tantas listas de u16 que van a continuación. Campos usados
(offset en el fichero y en el `struct merchant` de P3Modding):

| Off | Tipo | Campo |
|-|-|-|
| 0 | s32 | [+0x0] **dinero** |
| 8 | u16 | [+0x8] **tipo**: 0 = jugador humano; la IA usa 1 (pequeños), 5, 7 y 15 |
| 12 | u16 | [+0xC] primera oficina |
| 14 | u16 | [+0xE] **primer barco** (los demás siguen el campo +4 de cada barco) |
| 25 | u8 | [+0x19] **ciudad natal** (índice) |
| 26 | u8 | [+0x1A] hombre (≠ 0) |
| 28 | u8 | [+0x1C] índice del nombre de pila (bit 7 = mujer, lo pone el juego al cargar según +0x1A) |
| 29 | u8 | [+0x1D] índice del apellido |
| 1336 | u32 | [+0x608] ciudades en las que no puede comerciar (un bit por índice) |
| 1532 | 10 × u16 | [+0x60C] longitudes de las listas finales |

El resto del orden: 0x4f6240 lee 268 bytes ([+0x0]…[+0xE2]), 0x4f6fa0 956
([+0x118], reputación [+0x11C] 0x1E0, [+0x2FC] 0xA0, [+0x3C4] 0xA0, [+0x464],
rangos [+0x39C] 0x28, [+0x468]…[+0x4D4]) y 0x534830 100; después 0x4f4100 lee
[+0x53C], [+0x540], [+0x544], [+0x546], [+0x547], [+0x608], [+0x548] 0x60,
[+0x5A8] 0x60 y [+0x60C] 0x14.

**El jugador** es el último comerciante con tipo 0: así lo elige el juego al
cargar una partida de un jugador (0x546978–0x5469a1). En 1.pat es el 36,
Georg Stromberg, natural de Lübeck, con 30 000 monedas y un barco.

## 12. Barcos (0x504230)

Tras el separador: `u16` versión (= la de la partida), 16 `u16` de cabecera
([+0xe0], [+0xe2], [+0xe4], [+0xe8], [+0xea], [+0xec], [+0xee], [+0xf0],
**[+0xf2] capitanes**, **[+0xf4] barcos**, [+0xf6] convoyes, [+0xe6], [+0xfa],
[+0xfc], [+0xfe], [+0xf8]), los capitanes (16 bytes cada uno en v ≥ 0x78), los
barcos (0x517990) y después convoyes, 0xC0 bytes y 5 objetos opcionales, que no
se leen. [+0xE8] y [+0xEA] son las cabezas de las listas de barcos en puerto y
en el mar, que recorre la actualización de cada tick (navigation.md §4).

### 12.1 Barco

`u16` estado [+0x134]. Si es ≥ 0x17 es un hueco libre y solo sigue un `u16`
[+0x6] (y un `u8` más si la versión es ≥ 0x83). Si no, 372 bytes más la ruta
(offsets en el fichero, contando el estado; entre corchetes, el `struct ship`
de 0x180 bytes de P3Modding):

| Off | Tipo | Campo |
|-|-|-|
| 0 | u16 | [+0x134] **estado**: 0 atracado, 1 esperando, 2 saliendo, 3 entrando, 6 en una lista de su ciudad, 0x0F navegando, 0x11 hundido, 0x12 pirata, 0x15 barcos sin dueño de cada ciudad (navigation.md §6) |
| 2 | u16 | [+0x4] siguiente barco del mismo dueño |
| 4 | u8 | [+0x0] **dueño** (comerciante; 0xFF ninguno) |
| 5 | u8 | [+0xE] **tipo**: 0 Snaikka, 1 Craier, 2 Kogge, 3 Holk |
| 6 | u8 | [+0xF] nivel |
| 7 | u8 | [+0x1] **rumbo** (navigation.md §3) |
| 8 | 2 × s8 | [+0x2] otro índice de la ruta: byte alto primero, `(b8 ≪ 8) + b9` con signo |
| 10 | s32 | [+0x10] **capacidad** (unidades internas: Snaikka 30000 = 15 cargas, Kogge 90000) |
| 14 | s32 | [+0x14] casco máximo |
| 18 | u16 | [+0x6] **siguiente de su lista** (en puerto o en el mar, §12) |
| 20 | u16 | [+0x8] convoy (< [+0xF6]: va en uno) |
| 22, 24 | u16 | [+0xA], [+0xC] lista ordenada por x para los encuentros (0x50D0C0) |
| 26 | s32 | [+0x18] casco |
| 30, 34 | s32 | [+0x1C], [+0x20] posición x, y en el mapa del mar (16.16) |
| 38 | u32 | [+0x24] puntero a la ruta (del proceso que guardó; solo importa si es 0) |
| 42, 46 | s32 | [+0x28], [+0x2C] **desplazamiento por tick** (16.16) |
| 50 | s32 | [+0x30] velocidad que se ve |
| 54 | u16 | [+0x34] **punto de la ruta** hacia el que va |
| 56 | u8 | [+0x36] la ruta es suya (no la de la matriz) |
| 57, 58 | u8 | [+0x37] ciudad de origen, [+0x38] **destino** (0xFF: un punto del mar) |
| 59 | u8 | [+0x39] **ciudad en la que está** (0xFF en el mar) |
| 60 | 2 × u8 | [+0x3A] barco al que persigue, byte alto primero (0xFFFF ninguno) |
| 62 | u16 | [+0x3C] avisos a la interfaz |
| 64 | u16 | [+0x3E] **descanso** de la tripulación: baja navegando, sube en puerto; sin byte alto positivo no zarpa |
| 66 | u16 | [+0x40] marineros |
| 68 | u16 | [+0x42] capitán (< [+0xF2]: tiene) |
| 70 | u32 | [+0x44] tick en que zarpó o atracó |
| 74 | u32 | [+0x48] **llegada prevista** (tick) |
| 78, 82 | u32 | [+0x4C], [+0x50] |
| 86 | | si [+0x24] ≠ 0 y [+0x36] ≠ 0: `u16 n` y n × (s16, s16) de ruta; si [+0x24] ≠ 0 y [+0x36] = 0, el juego le da la ruta de la matriz entre [+0x37] y [+0x38] (0x445010, navigation.md §5) |
| R | 24 × s32 | [+0x54] **carga** por mercancía |
| R + 96 | 24 × f32 | [+0xB4] **precio medio** pagado por unidad interna |
| R + 192 | u32 ×3 | [+0x114], [+0x124], [+0x128] |
| R + 204 | f32 | [+0x12C] suma de la bodega llena al zarpar (tanto por uno) |
| R + 208 | u16 ×2 | [+0x130] viajes, [+0x132] |
| R + 212 | u8 ×4 | [+0x136] órdenes (bit 0: ruta automática), [+0x137], [+0x13A], [+0x13B] |
| R + 216 | u16 | [+0x138] ticks de la maniobra de puerto |
| R + 218 | 24 × u8 | [+0x13C] artillería: tipo de pieza en cada hueco, ≥ 6 vacío |
| R + 242 | u16 | [+0x154] armas de mano |
| R + 244 | u8 | [+0x156] cargas ocupadas que no son mercancía |
| R + 245 | u8 | [+0x157]; si v ≥ 0x79, [+0x158], [+0x159] y `u16` [+0x15A] |
| R + 250 | u8 | [+0x15C] ataque pirata a una ciudad (offsets con v ≥ 0x79) |
| R + 251 | u8, 2 × u8 | [+0x15D], [+0x15E] (byte alto primero) |
| R + 254 | 32 | [+0x160] **nombre** (Latin-1) |

(R = 86 + longitud de la ruta.) Al cargar cada barco, el juego llama a
0x5182b0, que también ejecuta antes y después de cada compra o venta:

### 12.2 Bodega (0x5182b0) **[código + oráculo]**

- Redondea hacia abajo la carga de cada mercancía a su unidad (2000 las
  cargas, 200 los barriles, 10 las armas).
- Ocupado = suma de la carga + 2000 · [+0x156]; se guarda en [+0x118]
  redondeado hacia arriba a 200.
- Equipo [+0x11C] = 400 por cada marinero por encima del mínimo del tipo
  (tabla 0x673664: 10, 16, 30, 24) + el peso de cada pieza de artillería
  (0x672cb4: 1000, 1000, 2000, 2000, 2000, 1000) + 10 por arma de mano.
- [+0x114]: lo que costó la carga, la suma de `ftol(carga · precio medio)`
  (la carga pasada a float); [+0x120]: la suma por pieza de artillería de la
  tabla 0x672CC8 (9, 10, 22, 24, 30, 18).
- Devuelve el espacio libre: capacidad − equipo − ocupado (sin redondear).

### 12.3 Comprar y vender (operaciones 0x02 y 0x01) **[código + oráculo]**

Operación de 20 bytes: código, `s32` cantidad, `u16` mercancía, `u16` barco,
`u16`, `u16` ciudad. Manejadores en la tabla 0x537030 de 0x535760.

Comprar (0x537450): el barco tiene que estar en esa ciudad ([+0x39]) y su
dueño no tenerla vetada ([+0x608]). Límite = mín(existencias, espacio libre);
si la cantidad lo supera, se toma el límite redondeado a cargas o barriles. Si
el precio (`get_buy_price`) supera el dinero, 0x52f3e0 rebaja la cantidad:
`cantidad · dinero / precio` redondeada, repetido mientras siga costando más
que el dinero, y luego sube de unidad en unidad mientras cueste **menos** que
el dinero y no pase de lo pedido. Después cobra, añade la carga (0x517fe0:
precio medio = (medio · carga anterior + precio) / carga nueva, en double y
guardado como float, con la carga redondeada a su unidad), recalcula la
bodega, baja las existencias y apunta estadísticas del comerciante (0x4f8840).

Vender (0x537340): no comprueba que el barco esté en la ciudad. Vende
mín(cantidad, carga), recalcula la bodega, cobra `get_sell_price` con las
existencias de antes de la venta, apunta estadísticas (0x4f8690) y suma la
mercancía al almacén de la ciudad.

Las estadísticas no cambian dinero, existencias ni carga, y p3vita aún no las
reproduce.

## 13. Tabla de nombres (0x511e30)

Es la última sección, entre los dos últimos separadores: los 40 nombres de
ciudad en alemán (terminados en 0), tres listas de nombres escritos por los
jugadores (hombre, mujer, apellido), cada una con hasta 20 cadenas terminadas
en 0 y cerrada con `#`, y un u32. Los índices de nombre del comerciante
(§11.2) apuntan a `scripts/NamenMaenner.txt` / `NamenFrauen.txt` (80
nombres) o `NamenFamilien.txt` (86 apellidos: los 80 de `Familiennamen` y los
6 de piratas); los que pasan del final son estos huecos de la partida. En 1.pat
el jugador usa el hueco 16 de las dos listas: «Georg» y «Stromberg» **[datos]**.
Que las listas del juego vayan en el orden del fichero es **[supuesto]**, pero
da nombres plausibles a los 36 comerciantes de la IA (Ferdinand Fugger, Hans Horn…).

## 14. Los objetos en memoria tras la carga **[código + oráculo]**

La simulación de las ciudades (`src/p3data/town_tick`) trabaja sobre los
objetos con la misma disposición que en memoria (`game_memory.hpp`), así que
`savegame.cpp` los llena como el cargador del juego. Comparado con el juego
recién cargada la partida (`tick_probe --carga`, ver [../oracle.md](../oracle.md)):
0 diferencias en las 12 partidas v0x82 en ciudades, oficinas, comerciantes,
capitanes, registros de 0x14 del mundo, registros de edificio, rejillas,
nodos, los campos con nombre de los barcos y los globales de abajo. Lo que no
sale directamente de §4–§12 (los barcos se comparan enteros salvo el puntero
de la ruta [+0x24]):

- Ciudad: la parte 1 (0x414 bytes) y la 2 van seguidas a [0x2C0] y [0x82C];
  la parte 4 deja sin leer [0x822]; en la 5, los movimientos de dinero son
  11 × 4 bytes en [0x704] y en [0x730] (versión ≥ 0x6A). El lector de la parte
  1 apunta en el mundo el id ([+0x18 + índice]) y la plantilla ([+0x40 +
  índice]) de la ciudad. Los nodos de [0x9B4] (0x14 bytes, el siguiente en
  +0) quedan en orden inverso al del fichero, con `[+0xC]` y `[+0xE]` en big
  endian sin signo y `[+0x12]` con signo.
- Registros de edificio (0x51EC10): los libres (lista [0x760]) quedan con
  x = y = 0xFF y, si la ciudad tiene estatua ([0x76C] & 1) y no hay ningún
  registro del tipo 0x2D en [0x764], se añade uno al principio de esa lista en
  [0x800] tomando un registro libre (0x51F100: si no queda ninguno, la tabla
  crece en 0x80). Pasa en 21 ciudades de una de las partidas.
- Oficina: su constructor (0x4FEBD0) deja 0xFFFF en [0x2C4], [0x2C8], [0x2CA],
  [0x2CC], [0x2CE], [0x2D0] y [0x2F2], 0xFF en [0x2C6], 0x02020202 en [0x2E4]
  y 0x32 en [0x2EC]; las libres solo leen [0x2C4], [0x2C8] y [0x2CA].
- Comerciante: el reparto de los 1552 bytes está en `load_merchant_memory`
  (0x4F6240, 0x4F6FA0, 0x534830 sobre [0x4D8] y 0x4F4100); [0x22] va en big
  endian, [0x25] es 0 o 1, y las diez listas del final se guardan aparte
  (punteros en [0x624]). [0xE4] y [0xE8] apuntan al nombre. El lector de
  barcos cuenta en [0x47C] los barcos de cada comerciante (0x504652).
- Capitán (0x4FDEB0, 16 bytes en el fichero): [0x0] 2, [0x2], [0x3], [0x4] 4,
  [0xC] 2, [0x8..0xB], [0xE] (0 o 1) y [0xF].
- Barco (0x516520 y 0x517990, en el orden de §12.1): [+0x2] y [+0x3A] van en
  big endian, [+0x15C] es 0 o 1 y [+0x15E] es big endian sin signo; tras
  [+0x50] y la ruta, [+0x114], [+0x124], [+0x128] y [+0x12C]..[+0x132] van
  seguidos, y después [+0x136], [+0x137], [+0x13A], [+0x13B] y [+0x138]. Los
  huecos libres quedan con dueño 0xFF, [+0x4], [+0xA] y [+0xC] a 0xFFFF y lo
  demás a 0, salvo [+0x6] (siguiente hueco libre) y [+0x137] (desde la
  versión 0x83), que se leen. Después la bodega (§12.2) recalcula [+0x114],
  [+0x118], [+0x11C] y [+0x120].
- Globales: los ajustes en `*0x6CC3E8` (0x4C bytes, con [0x3C..0x4B] a 0 y el
  byte 8 limitado a 14 si el 0xF no es 6), la cabecera del mundo (§3; [+0x89]
  es 0 o 1, [+0x8A] es 1 si el byte vale 0x7F), [+0x80] del mundo con el
  número de comerciantes y [+0x8F] con el último byte del mundo, la cabecera
  del objeto de los barcos ([+0xE0..+0xFE]), el jugador en 0x6DFC14 y el
  factor de la dificultad del comercio en 0x6DE43C.

También se leen los objetos de asedio de cada ciudad (0x633730, en
`*0x6DF270`, `SaveGame::sieges`) y su ejército sitiador (0x627AD0, con lo
que hace el lector después: puertas, posición y objetivos;
`tick::load_siege_army`), con lo que cuelga de punteros en
`SaveGame::siege_extras`, y los 15 objetos del mundo de [+0xB10] (0x52BBE0;
su lista de registros de 16 bytes se descarta, vacía en las partidas). Ver
[world_tick.md](world_tick.md).

## 15. Pendiente

- v0x4D (`0100–0204.pat`): siguiendo 0x549f40 para esa versión (ajustes de 0x28 bytes, sin
  [+0x8dc]) la cabecera del mundo sale incoherente (año 1, 0x849C0000 ciudades) y en 0x34 no
  hay `0xAFFE`; falta alguna rama que no he localizado.
- Kontors en el mapa: las oficinas ya se leen (§11.1); falta dibujar `spielerkontor` y
  `gegnerkontor` (§8.5).
- Estatua de la ciudad (§8.3), grúas (qué modelo de los 6 elige el exe), capillas,
  ayuntamiento de las plantillas 21–23 y significado de `b2` = 0xFE/0xFD en murallas.
- Andamios de edificios en obras (0x5891c0) y bordes de calle de las celdas 0x81
  (elección aleatoria 0x592cfd): no se dibujan.
- Tras los barcos: encargos (0x4d6350) y las secciones de 0x4d4bc0, 0x4d81e0 y 0x611ae0 solo
  están delimitadas.
