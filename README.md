# p3vita

Reimplementación libre del motor de **Patrician III** (Ascaron, 2003) pensada
para la PS Vita, con versión de PC para desarrollar.

El repositorio no contiene ningún archivo del juego. Para jugar hace falta una
copia propia; la versión de referencia es la de GOG (1.1), que es la que
documenta el proyecto [P3Modding](https://p3modding.github.io/).

El plan de trabajo está en [docs/roadmap.md](docs/roadmap.md).

## Compilar (PC, desde WSL o Linux)

```sh
source scripts/env.sh
cmake -S . -B build
cmake --build build -j 8
(cd build && ctest --output-on-failure)
```

## Herramientas

`cprtool` lista y extrae el contenido de los archivos CPR del juego:

```sh
build/tools/cprtool/cprtool info    p2arch0_eng.cpr
build/tools/cprtool/cprtool list    p2arch0_eng.cpr [filtro]
build/tools/cprtool/cprtool extract p2arch0_eng.cpr salida/ [filtro]
```

`p3img` comprueba que todas las imágenes (AIM, BMP, TGA y JPG) se decodifican
y exporta cualquiera a PNG. El formato AIM, propio del motor de Ascaron, está
documentado en [docs/formats/aim.md](docs/formats/aim.md).

```sh
build/tools/p3img/p3img check p2arch0_spa.cpr
build/tools/p3img/p3img png   p2arch0_spa.cpr images/Options/Hauptmenu.jpg menu.png
build/tools/p3img/p3img png   p2arch0_spa.cpr images/Schiffe/page_schiffe.aim barcos.png
```

`p3town` dibuja el mapa de una ciudad (formato en
[docs/formats/town.md](docs/formats/town.md)); con un número final, reducido:

```sh
build/tools/p3town/p3town p2arch0_spa.cpr 0 ciudad0.png
build/tools/p3town/p3town p2arch0_spa.cpr 0.editor ciudad0_editor.png 4
build/tools/p3town/p3town p2arch0_spa.cpr 'missav/1.pat#3' ciudad3_1300.png 4
```

La última línea dibuja la ciudad 3 de una partida guardada tal como está en ella
(formato en [docs/formats/savegame.md](docs/formats/savegame.md)).

## Compilar para la Vita

Primero hay que instalar VitaSDK (una sola vez, sin sudo, en `~/vitasdk`):

```sh
bash scripts/setup-vitasdk.sh
```

Después, cada compilación deja el VPK en `dist/p3vita.vpk`:

```sh
bash scripts/build-vita.sh
```

Para probarlo en la consola (con HENkaku o Enso):

1. Instala `p3vita.vpk` con VitaShell.
2. Copia `p2arch0_eng.cpr` de tu instalación del juego a `ux0:data/p3vita/`; es
   la base de todas las versiones. Para la versión española copia también
   `p2arch0_spa.cpr`, que se monta encima (textos, nombres y fuentes en
   español). Con solo el español también arranca, pero le faltan datos.

De momento es un visor de ciudades en el que ya se puede comerciar. Arranca en
Lübeck tal como empieza la partida de 1300 (`missav/1.pat`), con el comerciante
del jugador (Georg Stromberg, 30 000 monedas) y su Snaikka en el puerto. La
cruceta o el stick izquierdo mueven la cámara, arrastrar con el dedo desplaza el
mapa, L y R cambian de ciudad, Triángulo alterna entre la partida, la plantilla
con todos los edificios del diseñador (`.editor`) y solo el terreno (`.town`), y
Start sale.

X abre el mercado de la ciudad: existencias, precio de compra y de venta de una
unidad y lo que lleva tu barco. Con el mercado abierto, arriba y abajo eligen la
mercancía, izquierda y derecha la cantidad (1, 5, 10 o todo), X compra, Cuadrado
vende, Select pasa al siguiente barco del puerto y Círculo lo cierra. También se
puede tocar una fila y los botones del pie. Los precios, los límites (almacén,
bodega y dinero) y el precio medio de la carga son los del juego. En el PC: las
flechas, Re Pág / Av Pág, E, M, C (comprar), V (vender), Tab y Esc.

`p3save` lista las ciudades de una partida y muestra el almacén de cada una, sus
comerciantes y sus barcos:

```sh
build/tools/p3save/p3save p2arch0_spa.cpr missav/1.pat
build/tools/p3save/p3save p2arch0_spa.cpr missav/1.pat 9
build/tools/p3save/p3save p2arch0_spa.cpr missav/1.pat comerciantes
build/tools/p3save/p3save p2arch0_spa.cpr missav/1.pat barcos
```

## Agradecimientos

Los formatos de archivo y las mecánicas del juego se basan en la documentación
de ingeniería inversa de [P3Modding](https://github.com/P3Modding).

Las imágenes estándar y las fuentes TrueType se decodifican con
[stb](https://github.com/nothings/stb) (dominio público), incluido en
`third_party/stb`.
