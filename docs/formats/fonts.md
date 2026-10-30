# Fuentes, textos y archivos por idioma

## Archivos superpuestos

Todas las instalaciones traen `p2arch0_eng.cpr` (y `p2arch1_eng.cpr`, una misión
del add-on). La española añade `p2arch0_spa.cpr`, que el ejecutable abre como
`p2arch%d_spa.cpr` encima del inglés: lo que está en los dos se toma del español.
El archivo español no es completo por sí solo; por ejemplo, no trae las fuentes
AIM (por eso tiene 6 AIM menos que el inglés). `CprArchive::mount` reproduce
este montaje.

## Fuentes de mapa de bits (versión inglesa)

`scripts/fonts.ini` define seis fuentes AIM (`fonts/tiepolo_*.aim`,
`fonts/elgreco*.aim`, capa tipo 33 de 32 bits con alfa). Cada una es una tira
horizontal de **celdas de ancho fijo**, el primer número de `Params`: en
`tiepolo_bold24.aim` (`Params=25 24 0`) son 118 celdas de 25 × 24, con el
carácter pegado al borde izquierdo de su celda. La celda 0 es el espacio, de
la 1 a la 94 van `!` … `~` y las 23 últimas son
`ä ö ü Ä Ö Ü ß á é í ó ú à è ì ò ù â ê î ô û`. No hay `ñ`, `¿` ni `¡`.

## Fuentes TrueType (versión española)

El ejecutable español dibuja el texto con TrueType (`sgl_DrawTrueTypeText…`).
Su `scripts/ttffonts.ini` usa la página de códigos 1252 y define:

| Fuente | Params (ancho alto espaciado nº) | Fichero |
|-|-|-|
| Font0 | 14 15 3 0 | `fonts/Tiepolo_black.ttf` |
| Font1 | 13 15 0 | `fonts/Tiepolo_bold.ttf` |
| Font2 | 16 17 0 | `fonts/Tiepolo_black.ttf` |
| Font3 | 22 20 3 0 | `fonts/Elgreco_.ttf` |
| Font4 | 22 23 3 0 | `fonts/Tiepolo_bold.ttf` |
| Font5 | 68 63 3 0 | `fonts/Elgreco_.ttf` |

p3vita las rasteriza con stb_truetype (`src/p3data/font.cpp`) para Latin-1,
usando el alto como altura de línea. Tiepolo no tiene el punto medio `·`.

## Nombres

`scripts/NamenStaedte_<idioma>.txt` tiene los nombres de las 40 ciudades, uno
por línea en el orden de sus ids (Latin-1; algunos con espacios al final), y
después los de los gremios. En español: Edimburgo, Londres, Brujas, Colonia,
Lubeck, Estocolmo, Kaliningrado, Tallin…
