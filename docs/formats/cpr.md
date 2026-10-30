# CPR (archivos de datos)

Contenedor sin compresión que usan varios juegos de Ascaron. Patrician III
(GOG 1.1) trae `p2arch0_<idioma>.cpr` con casi todos los recursos y
`p2arch1_<idioma>.cpr` con una misión del add-on.

Base: [P3Modding, CPR](https://p3modding.github.io/file-formats/cpr.html). Lo
que sigue está comprobado con `p2arch0_eng.cpr`, `p2arch1_eng.cpr` y
`p2arch0_spa.cpr`.

Todos los enteros son `u32` little-endian.

## Cabecera (0x00 a 0x1F)

| Offset | Tamaño | Contenido |
|--------|--------|-----------|
| 0x00   | 16     | Firma `ASCARON_ARCHIVE ` (con el espacio final) |
| 0x10   | 16     | Versión como texto, rellena con ceros: `V0.9` |

## Chunks (desde 0x20 hasta el final)

| Offset | Campo | Notas |
|--------|-------|-------|
| +0x00  | `index_size` | Bytes del bloque de índice **incluida esta cabecera**. Siempre 0x1000 en los archivos de Patrician III. |
| +0x04  | `index_used` | Bytes realmente usados del bloque (cabecera más entradas); el resto son ceros. |
| +0x08  | `files` | Número de entradas del chunk. |
| +0x0C  | `data_size` | Bytes de datos que siguen al bloque de índice (con relleno). |
| +0x10  | entradas | `files` entradas seguidas, sin alineación. |

Los datos empiezan en `chunk + index_size` y el siguiente chunk está en
`chunk + index_size + data_size`.

## Entrada de índice

| Offset | Campo | Notas |
|--------|-------|-------|
| +0x00  | `offset` | Absoluto desde el inicio del archivo. |
| +0x04  | `size` | Tamaño del fichero. |
| +0x08  | `unknown` | Vale 1 en las 6485 entradas del archivo inglés. |
| +0x0C  | `path` | Latin-1 terminada en cero, con separadores `\`. |

## Contenido de p2arch0_eng.cpr

6485 ficheros (236,4 MiB). Por carpeta raíz: `images` (4148), `iso` (1882),
`musik` (178), `missions` (108), `missions_addon` (93), `scripts` (34),
`missav` (27), `fonts` (10), `navdata` (4) y `maps` (1).

Por tipo, los más importantes: `.aim` (3409, imágenes propias del motor),
`.bmp` (755), `.tga` (318), `.wav` (178), `.let` (934), `.lcr` (287) y
`.p2m` (200, misiones).
