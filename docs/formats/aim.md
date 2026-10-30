# Formato de imagen AIM (Ascaron, Patrician III): especificación

Obtenida por ingeniería inversa estática de `AIM.dll` (GOG 1.1, MD5
`74c32bab62943da3e9397c57e4afce92`, PE32, image base `0x10000000`) y
validada contra los 3409 `.aim` de `gamedata/extracted/eng`.
No había documentación pública del formato (P3Modding usa la DLL original
por FFI); esta especificación es trabajo propio de p3vita.  Implementación:
`src/p3data/aim.cpp`.  Durante la ingeniería inversa se escribió además un
decodificador de referencia en Python: el de C++ coincide con él byte a byte
en 3404 de los 3409 ficheros; los otros 5 son JPEG, donde cada decodificador
redondea a su manera.

Convenciones:
- Todos los enteros son **little-endian**. `u8/u32` sin signo, `s32` con signo.
- Los offsets de cabecera son **relativos al primer byte del chunk** (el byte
  de tipo), igual que en la DLL.
- `0x1000xxxx` son direcciones virtuales de la DLL (ver índice al final).
- "Codificación interna" = el código de formato de píxel que la DLL guarda en
  su estructura de imagen (campo `+0x14`), ver §5.

---

## 1. Resumen

| Capa (tipo) | Byte enc. | Ficheros | Contenido | Codif. interna |
|---|---|---|---|---|
| 34 (0x22) | (no hay) | 2988 | 8 bits indexado + paleta 256×BGRA, ambos LZ | 1 PALETTE8 |
| 22 (0x16) | 2 | 305 | BGR 24 bits sin comprimir | 6 BGR24 |
| 33 (0x21) | 6 | 59 | BGRA 32 bits LZ, entrelazado (fuentes) | 4 BGRA32 |
| 33 (0x21) | 2 | 20 | BGRA 32 bits LZ, planar (nubes, fuego, sombras) | 4 BGRA32 |
| 26 (0x1A) | 1 | 32 | DXT3 comprimido con LZ (barcos hundiéndose) | 15 DXT3 |
| 30 (0x1E) | 1 | 5 | JPEG baseline (Intel JPEG Library) | 4 BGRA32 |

Todos los ficheros tienen la estructura `"AIM\0"` + chunk raíz tipo 3
(nombre `"lala"`) + **una** capa de imagen.  El único algoritmo de
compresión propio es un **LZ77 con flujo de bits LSB-primero** (§3).

---

## 2. Contenedor

### 2.1 Firma
```
+0  char[4]  "AIM\0"
```
`0x10004840` lee 4 bytes y solo compara `'A','I','M'`.

### 2.2 Chunk genérico
```
+0  u8   tipo
+1  u32  tamaño  (incluye estos 5 bytes)
+5  ...  cabecera específica del tipo + datos
```
La cabecera específica tiene un tamaño fijo por tipo (función `0x10005770`,
tabla de saltos `0x1001c480`); el valor incluye los 5 bytes comunes:

| tipo | 1 | 3 | 4 | 5 | 20–23 | 24–28 | 29–31 | 33 | 34 |
|---|---|---|---|---|---|---|---|---|---|
| cabecera | 0x35 | 0x25 | 0x2E | 0x26 | 0x12 | 0x0E | 0x13 | 0x15 (*) | 0x15 |

(*) La tabla dice 0x15 para el tipo 33, pero su lector real (`0x10007c20`)
consume **0x16** bytes (ver §4.2).  La tabla solo la usan
`AIM_READ_LAYER`/`AIM_WRITE_LAYER`; los lectores de imagen usan tamaños
fijos propios.

### 2.3 Recorrido de nivel superior (`0x10004660`)
```
pos = 4
bucle:
    si pos >= fin_fichero: tipo = 0xFF -> fin sin imagen (error)
    leer tipo (sin avanzar; 0x10005660)
    tipo 3 -> raíz de imagen (0x10004c80)        [único caso en los datos]
    tipo 4 -> "squiz image" (0x10004f20)         [sin muestras]
    tipo 5 -> contenedor de mipmaps (0x10005400) [sin muestras]
    otro   -> saltar 'tamaño' bytes (0x100056f0) y repetir
```
Tras procesar un chunk 3/4/5 la DLL termina: solo se lee **una** imagen.

### 2.4 Chunk raíz tipo 3
```
+0x00 u8      3
+0x01 u32     tamaño (en todos los ficheros = longitud_fichero − 4)
+0x05 char[32] nombre, terminado en 0 (siempre "lala")
+0x25 ...     capa de imagen (empieza aquí, offset 41 del fichero)
```
La DLL lee la cabecera de 0x25 bytes y a continuación decodifica la capa que
empieza inmediatamente después (`0x10005490`), sin comprobar que quede
dentro de la raíz.  En los 3409 ficheros la capa acaba exactamente donde
acaba la raíz.

Tipo 5 (mipmaps, sin muestras): cabecera de 0x26 bytes (`nombre[32]` + 1
byte); se decodifica solo la primera capa que sigue (la mayor).
Tipo 4 (squiz, sin muestras): cabecera de 0x2E bytes: `nombre[32]`,
`u32 @+0x25`, `u32 @+0x29`, `u8 @+0x2D`; le siguen datos de tamaño
`f(u32,u32,u8)` (`0x1000f1d0`) y después una capa normal que se expande con
`0x1000f5d0`.  No analizado en detalle.

### 2.5 Despacho de capas (`0x10005490`, tabla `0x1001c430`)

| tipo | lector | contenido | en datos |
|---|---|---|---|
| 20 | `0x100076f0` | paleta + índices crudos | no |
| 21 | `0x100074a0` | 16 bits crudos | no |
| 22 | `0x10007280` | 24 bits crudos | **sí** |
| 23 | `0x10007080` | 32 bits crudos | no |
| 24–28 | `0x10008920` (vía `0x10008e40/dc0/d40/cc0/c40`) | DXT1..DXT5 | **26** |
| 29 | `0x10005ce0` | JPEG (variante con 4 umbrales) | no |
| 30 | `0x100062b0` | JPEG | **sí** |
| 31 | `0x10006a50` | JPEG + máscara de alfa de 1 bit | no |
| 32 | — | no soportado (error) | no |
| 33 | `0x10007c20` | 32 bits LZ | **sí** |
| 34 | `0x100083e0` | 8 bits + paleta LZ | **sí** |

---

## 3. Compresión LZ

### 3.1 Envoltura en el fichero
Cada bloque comprimido aparece como:
```
u32  N          // tamaño del blob en bytes
u8   blob[N]
```
La DLL lee `N` y obtiene un puntero a los `N` bytes (`0x10008c20` dos
veces), y pasa el puntero al descompresor (`0x10010840` → `0x10011070`).

### 3.2 Cabecera del blob
```
+0  s32 n
    n == 0 : bloque vacío (no produce nada)
    n <  0 : ALMACENADO: siguen exactamente −n bytes que se copian tal cual
    n >  0 : COMPRIMIDO: n = número de bytes de salida; siguen palabras de bits
```
El compresor (`0x10010a70`) almacena (n<0) si la entrada mide < 16 bytes o
si la salida comprimida no sería menor que la entrada (`0x10010b04`).
En los datos: 8627 bloques comprimidos y 126 almacenados.

### 3.3 Flujo de bits
- Tras `n` viene una secuencia de **palabras u32 little-endian**.
- Los bits se consumen **del bit 0 (LSB) hacia el bit 31** de cada palabra;
  al agotar una palabra se pasa a la siguiente.
- Un campo de `k` bits se lee LSB-primero: su bit 0 es el siguiente bit del
  flujo.  Si cruza la frontera de palabra, los bits bajos vienen del final de
  la palabra actual y los altos del principio de la siguiente.
- Esto equivale exactamente a leer los bytes del blob (desde el offset 4) en
  orden, cada uno del bit 0 al bit 7.

Detalle de la DLL (relevante solo para saber cuántos bytes lee): la palabra
actual está en EDX y los bits restantes en EBX; en cuanto EBX llega a 0 se
carga la siguiente palabra (**carga ansiosa**, aunque no se vaya a usar).
Por tanto la DLL toca `4 + 4·(1 + T/32)` bytes del blob (división entera),
siendo `T` el total de bits consumidos tras `n`.  El codificador
(`0x10010fb8`) siempre vuelca la última palabra (parcial o vacía), así que
**en los 8627 bloques comprimidos `N == 4 + 4·(1 + T/32)` exactamente**: una
implementación con carga ansiosa nunca lee fuera del blob.

### 3.4 Tokens
Cada token empieza con 1 bit de bandera:

**Literal** (bandera 0) — 9 bits en total:
```
bit  0      : 0
bits 1..8   : byte literal (LSB primero)
```

**Coincidencia** (bandera 1):
```
1 bit   : 1
3 bits  : c  (clase de distancia, 0..7)
c+1 bits: e  (extra)
L bits  : longitud (código de escape, ver abajo)
d    = e + BASE[c]            BASE = {0, 2, 6, 14, 30, 62, 126, 254}  (= 2^(c+1) − 2)
dist = d + 1                  // 1..510
```
| c | bits extra | dist |
|---|---|---|
| 0 | 1 | 1–2 |
| 1 | 2 | 3–6 |
| 2 | 3 | 7–14 |
| 3 | 4 | 15–30 |
| 4 | 5 | 31–62 |
| 5 | 6 | 63–126 |
| 6 | 7 | 127–254 |
| 7 | 8 | 255–510 |

(Tablas de la DLL: `0x1001a508` pares `{bits, base}`, `0x1001a548` máscaras
`2^(c+1)−1`.  El compresor escribe `1 | c<<1 | e<<4` en `5+c` bits,
`0x10010e3a..0x10010f33`; la ventana es un anillo de 0x1FE = 510 posiciones.)

**Longitud**: `len = 2 + v₂ + v₃ + v₄ + …`, donde `v_w` es un campo de `w`
bits; se empieza con `w = 2` y se lee el siguiente campo (con `w+1` bits)
**solo si el anterior valía todo unos** (`2^w − 1`).  Máscaras en
`0x1001a56c` (índice = w, valor `2^w − 1`).

| bits de longitud (LSB primero, campo a campo) | len |
|---|---|
| `v₂`=0,1,2 | 2,3,4 |
| `v₂`=3, `v₃`=0..6 | 5..11 |
| `v₂`=3, `v₃`=7, `v₄`=0..14 | 12..26 |
| … | … |

La copia es **byte a byte hacia delante** desde `salida − dist`, por lo que
`dist < len` es válido y repite el patrón (p. ej. `dist=1` = RLE).
Longitud mínima 2; no hay máximo explícito.

### 3.5 Pseudocódigo exacto
```
func lz_decompress(blob, out) -> int:         // devuelve n (bytes declarados)
    n = s32(blob[0..4])
    if n == 0: return 0
    if n < 0:
        copy(out, blob[4 .. 4-n]); return -n
    br = BitReader(blob, 4)                   // LSB-primero (§3.3)
    o = 0; restante = n
    loop:
        if br.get(1) == 0:                    // literal
            out[o++] = br.get(8)
            restante -= 1
            if restante == 0: break           // la DLL usa 'jne'
        else:                                 // coincidencia
            c   = br.get(3)
            d   = br.get(c + 1) + (2^(c+1) - 2)
            len = 2; w = 2
            loop:
                v = br.get(w); len += v
                if v != 2^w - 1: break
                w += 1
            s = o - (d + 1)
            repeat len: out[o++] = out[s++]   // byte a byte
            restante -= len
            if restante <= 0: break           // la DLL usa 'jg'
    return n
```
Notas para la implementación C++:
- Con `remaining <= 0` la DLL acaba; si una coincidencia final se pasara de
  `n` escribiría fuera (desbordamiento real en la DLL).  En los datos
  **nunca** ocurre (salida producida == `n` en los 8753 bloques).  Recomiendo
  tratarlo como error.
- Nunca hay referencias anteriores al inicio del bloque (0 casos): cada
  bloque se puede descomprimir de forma independiente.
- Un acumulador de 64 bits con recarga de 32 evita los casos `shift 32`
  indefinidos en C/C++.  El campo de longitud puede encadenar muchos campos;
  hay que recargar dentro del bucle de longitud.
- Validar siempre `d+1 <= o` y `N` contra el final del chunk (la DLL no
  comprueba nada).

### 3.6 Troceado en bloques de 64 KiB
Las imágenes grandes se comprimen en bloques independientes de, como máximo,
**65536 bytes de salida** (`0x10000`).  El bloque `i` produce
`min(65536, total − 65536·i)` bytes; la DLL escribe el bloque `i` en
`salida + 65536·i` (tipos 33-entrelazado y 34) y avanza siempre 65536 bytes
**sin mirar** la `n` del blob (en los datos `n` coincide siempre).  En el tipo
26 la DLL avanza con la `n` devuelta (ver §4.3).

---

## 4. Capas de imagen

### 4.1 Tipo 34 — 8 bits indexado + paleta (`0x100083e0`)
```
+0x00 u8     0x22
+0x01 u32    tamaño del chunk
+0x05 u8[4]  bits por canal (informativo; R,G,B,A por analogía con el tipo 33)
             valores vistos: (8,8,8,8) ×2885, (8,8,8,0) ×103.  El lector lo
             IGNORA; el escritor lo usa para enmascarar canales antes de
             cuantizar (0x10004270).
+0x09 u32    ancho
+0x0D u32    alto
+0x11 u32    stride (bytes por fila de índices, >= ancho)
+0x15 u32    Npal ; blob paleta (Npal bytes) -> exactamente 1024 bytes
      después ceil(stride·alto / 65536) × { u32 N_i ; blob_i }
```
- Paleta: 256 entradas de 4 bytes **B, G, R, A** (u32 `0xAARRGGBB`).
- Píxeles: `stride·alto` bytes; fila `y` = `buf[y·stride .. y·stride+ancho)`.
  El relleno (si `stride > ancho`) se ignora.
- `stride` NO es derivable: en 435 ficheros `stride == ancho` con ancho no
  múltiplo de 4; en el resto `stride == (ancho+3) & ~3`.  Usar el campo.
- Resultado DLL: codificación 1 (PALETTE8), bpp 1, paleta en `+0x04`.
- Verificado: el chunk se consume exactamente en los 2988 ficheros.

### 4.2 Tipo 33 — 32 bits directo comprimido (`0x10007c20`)
```
+0x00 u8     0x21
+0x01 u32    tamaño del chunk
+0x05 u8     flags
               bit 0: 1 -> codificación 3 (RGBA en memoria); 0 -> 4 (BGRA)
               bit 2: 1 -> ENTRELAZADO; 0 -> PLANAR
               bit 1: no lo usa el lector (siempre 1 en los datos: 2 y 6)
+0x06 u8     bits[0]  -> byte 2 del píxel (R en BGRA)
+0x07 u8     bits[1]  -> byte 1 (G)
+0x08 u8     bits[2]  -> byte 0 (B)
+0x09 u8     bits[3]  -> byte 3 (A)
+0x0A u32    ancho
+0x0E u32    alto
+0x12 u32    stride (bytes por fila; = ancho·4 en los datos)
+0x16 ...    bloques
```
Total `T = stride·alto` bytes.
- **Entrelazado** (`flags & 4`): `ceil(T/65536)` bloques
  `{u32 N; blob}` que se concatenan.
- **Planar** (`!(flags & 4)`): 4 planos, en orden byte 0, 1, 2, 3 del píxel.
  Cada plano mide `P = T/4` bytes y se divide en `ceil(P/65536)` bloques
  (el troceado se reinicia en cada plano).  Cada bloque se descomprime a un
  buffer temporal y se esparce: `buf[plano + 4·k] = tmp[k]` (continuando `k`
  entre bloques del mismo plano).
- **Expansión de profundidad** (`0x10007e83`), solo si algún `bits != 8`:
  para cada byte `j` del píxel con `b = bits[...]` según la tabla de arriba,
  `s = 8 − b`:
  ```
  v = (x << s) & 0xFF
  v |= (v & 0x80) ? (0xFF >> b) : 0          // rellena los bits bajos con 1s
  caso especial alfa con b == 0: resultado = 0xFF
  ```
  Se aplica a todo el buffer (`T/4` píxeles, incluido el relleno).  En los
  datos siempre `bits = (8,8,8,8)`, así que no se aplica.
- Alfa: real de 8 bits (0..255) en fuentes, nubes y fuego.

**Cola no leída** (79 de 79 ficheros tipo 33): dentro del mismo chunk, tras
los datos anteriores, hay una segunda imagen con el *cuerpo* de un tipo 34:
```
+0x00 u8   0x21   +0x01 u32 0   +0x05 u8 flags(33)   +0x06 u8[3] bits[0..2]
+0x09 u32 ancho   +0x0D u32 alto   +0x11 u32 stride8   +0x15 Npal, blobs...
```
Es una versión cuantizada a 8 bits con paleta de la misma imagen (el
escritor reutilizó la estructura de la capa 33: tipo 0x21, tamaño 0).  La DLL
no la lee nunca; el tamaño del chunk 33 la incluye.  Decodifica
exactamente como un tipo 34 (79/79 consumen la cola completa).  **Se puede ignorar.**

### 4.3 Tipos 24–28 — DXT (`0x10008920`); presente: tipo 26 = DXT3
```
+0x00 u8   0x18..0x1C   (24=DXT1, 25=DXT2, 26=DXT3, 27=DXT4, 28=DXT5)
+0x01 u32  tamaño
+0x05 u8   flags: bit 0 = comprimido con LZ
+0x06 u32  ancho
+0x0A u32  alto
+0x0E ...
  si flags & 1:  u32 total ; luego bloques {u32 N; blob} mientras restante > 0,
                 con restante -= n_declarada_del_blob (si n == 0 la DLL se
                 queda en bucle infinito)
  si no:         datos DXT crudos = tamaño − 0x0E bytes
```
- Datos DXT estándar: bloques de 4×4 en orden de filas de bloques.  DXT3 =
  16 bytes/bloque: 8 bytes de alfa (4 bits/píxel, fila a fila, nibble bajo
  primero), `u16 c0`, `u16 c1` (RGB565), `u32` índices (2 bits/píxel, LSB
  primero, fila a fila).  `total = ancho·alto` para DXT3/5 y
  `ancho·alto/2` para DXT1.
- En los datos: 32 ficheros, todos con `total == ancho·alto`, ancho y alto
  múltiplos de 4, bloques de 65536.
- Codificación interna 15, bpp 0, "stride" (`+0x18`) = tamaño total.
- Conversión a BGRA de la DLL: ver §6.

### 4.4 Tipo 22 — 24 bits sin comprimir (`0x10007280`)
```
+0x00 u8   0x16
+0x01 u32  tamaño
+0x05 u8   enc: 1 -> codificación 5 (R,G,B en memoria); otro -> 6 (B,G,R)
+0x06 u32  ancho
+0x0A u32  alto
+0x0E u32  stride
+0x12 u8[stride·alto]  píxeles
```
En los datos `enc = 2` (B,G,R), 272×155, `stride = 816`; las 20 ciudades
0–19 son idénticas byte a byte a sus `.tga`.

Variantes sin muestras, misma cabecera de 0x12 bytes:
- **21** (16 bits, bpp 2): `enc` 1→cod. 7, 2→8, 3→9, otro→10.  Máscaras en
  `0x1001a340`: {565}, {555}, {4444}, {1555}; la asignación exacta
  codificación↔máscara no está verificada.
- **23** (32 bits, bpp 4): `enc` 1→cod. 3 (RGBA), otro→4 (BGRA).
- **20** (paleta cruda): `+0x05 u8` = nº de entradas de paleta (0 = 256);
  siguen `n·4` bytes de paleta (las no presentes valen `0xFFFFFFFF`) y
  `stride·alto` índices.

### 4.5 Tipo 30 — JPEG (`0x100062b0`)
```
+0x00 u8   0x1E
+0x01 u32  tamaño
+0x05 u8   flags  bit 0: canales R/B cruzados en el JPEG (ver abajo)
                  bit 1: submuestreo pedido a IJL (IJL_422 si 1, IJL_411 si 0)
+0x06 u8   calidad usada al escribir (informativo; 0x5A = 90 en los datos)
+0x07 u8   t0  \
+0x08 u8   t1   } umbrales; se aplican si el u32 en +0x07 != 0
+0x09 u8   t2  /
+0x0A u8   (parte del u32 anterior; sin otro uso)
+0x0B u32  ancho
+0x0F u32  alto
+0x13 ...  JPEG/JFIF completo hasta el final del chunk (tamaño − 0x13 bytes)
```
La DLL llama a `ijlRead` (ijl11.dll) con `JPGBytes = +0x13`,
`JPGSizeBytes = tamaño − 0x13`, `JPGColor = YCbCr`, `JPGChannels = 3`,
`DIBColor = IJL_RGBA_FPX (5)`, `DIBChannels = 4`, buffer `ancho·4·alto`.
IJL deja en memoria `J_R, J_G, J_B, A`.  Después:
1. Si `flags & 1 == 0`: intercambia bytes 0 y 2 → `J_B, J_G, J_R, A` (BGRA).
   Si `flags & 1 == 1`: no intercambia → `J_R, J_G, J_B, A` interpretado como
   BGRA, es decir: **R real = J_B, B real = J_R** (el escritor pasó un búfer
   RGB a IJL declarándolo BGR).
2. Si el u32 en `+0x07` ≠ 0: en el BGRA final, byte0 < t0 → 0, byte1 < t1 →
   0, byte2 < t2 → 0 (el alfa no se toca).

Resultado: codificación 4 (BGRA32), bpp 4, stride `ancho·4`.
En los datos: 5 ficheros (`iso/towns/27*.aim`), 272×155, `flags = 1`,
umbrales 0, JPEG baseline SOF0, Y 2×2 + Cb/Cr 1×1 (4:2:0), sin DRI, el EOI
coincide con el final del chunk.  Con la regla del bit 0 el resultado tiene
los mismos tonos que las ciudades tipo 22.

El valor del **alfa** lo pone IJL con `IJL_RGBA_FPX` desde un JPEG de 3
canales; no está verificado (p3vita pone 255).

Variantes sin muestras: **29** (`0x10005ce0`, usa 4 umbrales `+0x07..+0x0A`)
y **31** (`0x10006a50`; su escritor `0x100065b0` escribe cabecera de 0x13
bytes, `u32` longitud del JPEG, el JPEG y una máscara de alfa de 1 bit por
píxel (bit 7 primero; bit = alfa ≥ 128), `(ancho·alto+7)/8` bytes troceada
en bloques LZ de 64 KiB).  El escritor del tipo 30 es `0x10006000`.

---

## 5. Imagen en memoria y codificaciones de píxel

### 5.1 Estructura `AIM_IMAGE` (API pública)
`AIM_INIT` (`0x100023d0`) pone a cero 0x5C bytes y reserva 0x84 bytes
internos en `+0x58`.  `0x100024d0`/`0x10002520` copian dos descriptores de
0x2C bytes entre la estructura pública y la interna.
```
+0x00 void* píxeles
+0x04 u32*  paleta (256 entradas B,G,R,A) o NULL
+0x08 u32   ancho
+0x0C u32   alto
+0x10 u32   bytes por píxel (0 en DXT)       <- P3Modding lo llama "pixel_encoding"
+0x14 u32   codificación interna (ver 5.2)   <- P3Modding lo llama "bytes_per_pixel2"
+0x18 u32   stride en bytes (en DXT: tamaño total)
+0x1C..+0x28  sin uso por los lectores
+0x2C..+0x57  segundo descriptor (destino) con la misma disposición;
              si su codificación (+0x40) != 0, AIM_CONVERT_* convierte (0x10003970)
+0x58 void* estado interno
```
Los valores "0=Compact, 1=Palette, 3=NoAlpha RGB, 4=BGRA 32bpp" que usa
P3Modding para `+0x10` son en realidad los **bytes por píxel** (0 = DXT).

### 5.2 Codificaciones internas (`+0x14`)
bpp de la tabla `0x1001c160`; stride por defecto de `0x100033d0` (solo se
usa al crear imágenes; los ficheros traen su propio stride).

| cód | nombre | bytes en memoria | bpp | stride por defecto |
|---|---|---|---|---|
| 1 | PALETTE8 | índice | 1 | (ancho+3)&~3 |
| 2 | PACKED4 | 2 píxeles/byte | 1 | ((ancho/2)+1)&~1 |
| 3 | RGBA32 | R,G,B,A | 4 | ancho·4 |
| 4 | BGRA32 | B,G,R,A | 4 | ancho·4 |
| 5 | RGB24 | R,G,B | 3 | (ancho·3+3)&~3 |
| 6 | BGR24 | B,G,R | 3 | (ancho·3+3)&~3 |
| 7–12 | 16 bits | ver §4.4 | 2 | (ancho·2+3)&~3 |
| 13 | DXT1 | | 0 | ancho·alto/2 |
| 14 | DXT2 | | 0 | 0 |
| 15 | DXT3 | | 0 | ancho·alto |
| 16 | DXT4 | | 0 | 0 |
| 17 | DXT5 | | 0 | ancho·alto |

Orden de canales (anclajes independientes que coinciden):
1. El decodificador DXT construye `0xFF000000 | R5<<19 | G6<<10 | B5<<3`, es
   decir R en el byte 2 → codificación 4 = B,G,R,A.
2. Paleta→cod. 4 es copia directa (`0x1000e240`) y paleta→cod. 6 también
   (`0x1000e370`), mientras que paleta→3 y →5 cruzan R/B.
3. `pferd.aim` (tipo 34) == `pferd.bmp` (8 bits) en los 638 880 píxeles
   leyendo la paleta como B,G,R,A; ciudades 0–19 (tipo 22, enc 2) ==
   `.tga` en todos los píxeles leyendo B,G,R.

### 5.3 Conversión a BGRA32 (lo que entrega `AIM_CONVERT_*` con destino 4)
- PALETTE8 → BGRA: `dst[x] = paleta[idx]` (u32 completo, **alfa incluido**).
- BGR24/RGB24 → BGRA: canales colocados según 5.2 y **A = 0xFF**
  (`0x10002090`, `0x10002000`).
- RGBA32 ↔ BGRA32: intercambio de bytes 0 y 2.
- DXT3 → BGRA: §6.

### 5.4 Alfa y color clave (observaciones; la DLL no los interpreta)
Alfa de las entradas de paleta **usadas** en los 2988 tipo 34:
- solo 255: 2900 ficheros (el magenta `FF00FF` es opaco, p. ej. `text.aim`);
- solo 0: 16 (p. ej. `Gebaeude_innen/Bad/Badhaus_*_TP.aim`, vapor/agua:
  todos los colores con A=0);
- {0, 255}: 9 (magenta con A=0, p. ej. `Blide-kaputt*.aim`);
- {0, 100, 255}: 63 (soldados `_X_*_tp*_8.aim`: magenta A=0, negro de sombra
  A=100).

El magenta `FF00FF` aparece entre los colores usados en 2747 de 2988
ficheros.  La semántica de transparencia (clave magenta, mezcla de los `_TP`,
sombras) es del ejecutable del juego, no de `AIM.dll`: el portador debe
decidirla aparte.

---

## 6. DXT3 → BGRA en la DLL (`0x1000b580` → `0x1000a5c0`)
Para cada bloque de 16 bytes (`a[0..7]` alfa, `c0`, `c1`, `idx`):
```
col(c) = { B: (c & 0x1F) << 3,  G: ((c >> 5) & 0x3F) << 2,  R: (c >> 11) << 3,  A: 0xFF }
         // sin replicar bits altos (un GPU daría 255 para 0x1F; la DLL da 248)
si c0 > c1 (comparación u16):
    col2 = LUT[2·col0 + col1]   col3 = LUT[col0 + 2·col1]   (por canal, A=0xFF)
    LUT[i] = i / 3 (truncado)   // tabla 0x1001b400, rellenada por 0x10009b40
si no (c0 <= c1):               // modo "3 colores" incluso en DXT3 (no estándar)
    col2 = (col0 + col1) >> 1  por canal, A=0xFF
    col3 = u32 0x00FFFF00      // B=0, G=255, R=255, A=0
píxel(x,y) = col[(idx_fila_y >> 2x) & 3]; luego A = nibble_x(a_fila_y) · 0x11
```
Particularidades:
- **La LUT `0x1001b400` es cero en la imagen de la DLL** y solo la rellena
  `0x10009b40`, a la que únicamente llega la inicialización de DirectDraw
  (`0x10009c50`) usada por la *compresión* DXT (`0x10009df0`).  En una sesión
  que solo lee, la conversión por software de la DLL daría negro en los
  colores 2 y 3 de los bloques con `c0 > c1`.  Probablemente el juego sube
  el DXT3 tal cual a la GPU; p3vita usa la LUT
  rellenada (`i/3`), que es lo que el código pretende.
- En los datos: 659 456 bloques; `c0 > c1`: 231 908; `c0 == c1`: 427 548
  (ninguno usa los índices 2 ni 3); `c0 < c1`: 0.  Por tanto el modo de 3
  colores **no cambia ningún píxel** y un decodificador DXT3/BC2 estándar
  (o la GPU) da el mismo resultado salvo la expansión 565→888 (bit
  replication) y el redondeo de la interpolación.  En PS Vita lo natural es
  subir los datos como textura BC2 sin descomprimir.

---

## 7. Validación
- 3409/3409 ficheros decodificados sin errores ni excepciones:
  t34 2988, t22 305, t33/enc 6 59, t33/enc 2 20, t26 32, t30 5.
- Contenedor: en todos, chunk raíz en offset 4 con tamaño = fichero − 4, nombre
  "lala", capa en offset 41 que acaba justo al final de la raíz.
- Consumo: cada lector consume exactamente el tamaño del chunk (en los 79
  tipo 33, contando la cola tipo-34 de §4.2, que también cuadra exacta).
- LZ: 8753 bloques (8627 comprimidos, 126 almacenados);
  39 772 522 literales y 16 761 321 coincidencias; en **todos**:
  salida producida == `n` declarada, `n` == lo que espera el bucle de la
  DLL (1024 para paletas, 65536 o resto), `N == bytes que lee la DLL`
  (holgura 0), 0 referencias anteriores al inicio del bloque.
- Gemelos: `pferd.aim` == `pferd.bmp` (0 píxeles distintos); ciudades
  0–19 == `.tga` (0 distintos en cada una, 42 160 píxeles).  Las ciudades
  128–147 y 251–254 difieren de su `.tga` en contenido (otra revisión del
  arte; 251.tga es solo mar), no en orden de canales: comprobado
  visualmente.
- Revisión visual de PNG: tiles de ciudad con ancho impar (stride 153) sin
  cizalla, barcos sobre magenta, fuentes, nubes y fuego (planar) con alfa,
  barco hundiéndose DXT3, ciudad 24 bits y ciudad JPEG con tonos coherentes.

---

## 8. Dudas y partes no verificadas
1. **JPEG (tipo 30)**: la envoltura AIM es exacta, pero p3vita lo
   decodifica con stb_image y los píxeles pueden diferir en ±1–2 de IJL 1.1.  El
   alfa que deja IJL con `IJL_RGBA_FPX` no está verificado (se asume 255).
2. **Alfa/transparencia**: la DLL copia el alfa de la paleta tal cual; cómo
   lo usa el juego (clave magenta, alfa 0 en `_TP`, alfa 100 de sombras) no
   se deduce de `AIM.dll`.
3. **Tipos sin muestras** (20, 21, 23, 24, 25, 27, 28, 29, 31, raíces 4 y 5):
   documentados a partir del código, sin validar.  Squiz (raíz 4) y la
   asignación de máscaras de 16 bits no están analizados a fondo.
4. **Campo de bits del tipo 34** y **bit 1 de flags del tipo 33**: sin
   efecto en la lectura; su significado exacto para el escritor es una
   deducción.
5. **Desbordes de la DLL**: si un blob declarara más salida de la que cabe o
   una coincidencia final se pasara, la DLL escribiría fuera de búfer; en
   los datos no ocurre.  El decodificador C++ debe comprobar límites.

---

## 9. Índice de direcciones (AIM.dll)
| Dirección | Función |
|---|---|
| `0x10002740` | `AIM_CONVERT_FILE` (lee fichero → `0x10003fd0`) |
| `0x10002930` | `AIM_CONVERT_MEMFILE` (exige tamaño ≥ 10 → `0x10003fd0`) |
| `0x10003fd0` | despacho por extensión (`_strnicmp` `0x10017ba0` de los 3 últimos caracteres: aim/tga/bmp/rgb/jpg); "aim" → `0x10004660` |
| `0x10003e70` | ídem para `AIM_GETFILEFORMAT_*` ("aim" → `0x10004460`) |
| `0x10004660` | lector AIM: abrir, recorrer chunks de nivel superior |
| `0x10004840` | abrir desde memoria; comprueba "AIM" |
| `0x10005660` / `0x100056f0` | tipo de capa (sin avanzar) / saltar capa |
| `0x10004a70` / `0x10005770` | `AIM_READ_LAYER` / tabla de tamaños de cabecera |
| `0x10004c80` / `0x10004f20` / `0x10005400` | raíces tipo 3 / 4 / 5 |
| `0x10005490` | despacho de capas (tabla `0x1001c430`) |
| `0x10005810` | despacho de escritura (tabla `0x1001c4b8`) |
| `0x100083e0` / `0x10008010` | tipo 34 lector / escritor |
| `0x10007c20` / `0x10007820` | tipo 33 lector / escritor |
| `0x10008920` / `0x100085b0` | tipos 24–28 lector / escritor |
| `0x10007280` / `0x10007150` | tipo 22 lector / escritor |
| `0x100062b0` / `0x10006000` | tipo 30 lector / escritor |
| `0x10006a50` / `0x100065b0` | tipo 31 lector / escritor |
| `0x10008c20` | "tomar n bytes" del stream en memoria (sin límites) |
| `0x10010840` → `0x10011070` | descompresor LZ (globales `0x1001a4c0` src, `0x1001a4c4` dst, `0x1001a4cc` resultado) |
| `0x100107d0` → `0x10010a70` | compresor LZ (`0x10010d64` búsqueda, `0x10010fb8` escritor de bits, `0x10010b04` almacenado) |
| `0x1001a508` / `0x1001a548` / `0x1001a56c` | tablas LZ: {bits, base}, máscaras de clase, máscaras `2^w−1` |
| `0x10003550` / `0x100033d0` / `0x1001c160` | crear descriptor / stride por defecto / bpp por codificación |
| `0x10003970` | conversión entre codificaciones (tabla `0x1001c21c`) |
| `0x1000e240` | PALETTE8 → BGRA32 |
| `0x1000b580` → `0x1000a5c0` | DXT3 → BGRA32 (LUT `0x1001b400`, init `0x10009b40`) |
| `0x10007e83` | expansión de profundidad de canal del tipo 33 |
| `0x100023d0` / `0x100024d0` / `0x10002520` | `AIM_INIT` / copia público→interno / interno→público |
