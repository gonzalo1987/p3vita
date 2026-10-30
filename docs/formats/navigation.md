# Mapa del mar y navegación

Ingeniería inversa de `Patrician3.exe` (GOG 1.1). Marcas como en
[savegame.md](savegame.md): **[código]** leído en el desensamblado,
**[datos]** comprobado con los ficheros del juego, **[oráculo]** comparado con
el propio código del juego en el emulador.

> Implementación en p3vita: `src/p3data/seamap.cpp` (posiciones del mapa) y la
> vista del mapa del mar del visor (`src/app/main.cpp`); `tools/p3map` dibuja
> el mapa de una partida en un PNG. El movimiento de los barcos y las rutas
> (§4–§5) aún no están implementados.

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
  en la partida): 0 oeste, 4 norte, 8 este, 12 sur, en sentido horario
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
(umbrales 0x3244/65536 ≈ tan 11° y 0x96CC/65536 ≈ tan 30°).

## 5. Rutas y objeto de navegación (pendiente)

Objeto global `*0x6CBDC8` (0x2C bytes, constructor 0x444ba0). 0x444d30 lee
los ficheros de `scripts/scrollmaps.ini` (`[Navmap0]`):

| Campo | Contenido | Fichero |
|-|-|-|
| +0x18, +0x1A, +0x14 | ancho, alto y rejilla tierra/agua (u8) | `nav_matrix.dat` |
| +0x1C, +0x0C | nº de puntos (350) y puntos (u16 x, u16 y) | `nav_vec.dat` |
| +0x04 | nº × nº celdas (u32 distancia, u16 siguiente punto, en huecos de 8 bytes) | `matrix_int.dat` |
| +0x10, +0x1E | matriz de rutas entre ciudades (n × n), la crea 0x4482c0 al cargar | |

Los formatos de los tres ficheros los documenta P3Modding
(`file-formats/navigation-matrix.md`, `navigation-vector.md`,
`navpoint-matrix.md`).

La ruta de un barco ([+0x24] → {puntos, número}) la construye 0x445010
(unas 1000 instrucciones; usa 0x446d50, 0x448450, 0x4445a0, 0x444800 y
0x446f20, que parecen buscar el punto de navegación más cercano, recorrer la
matriz y comprobar líneas sobre la rejilla). **Al cargar una partida, el juego
recalcula la ruta de cada barco que navega** salvo que esté marcada como
guardada ([+0x36]), así que hace falta reproducirla para mover los barcos de
la partida. Plan: en el oráculo, rellenar el objeto de navegación con los
ficheros, dejar de saltar 0x4482c0 y 0x445010, volcar las rutas resultantes y
compararlas con la versión en C++; después, ejecutar 0x516cc0 tick a tick.
