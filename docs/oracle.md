# El oráculo: comprobar contra el código del juego

Para saber si una parte de la simulación se comporta exactamente como el
original no hace falta jugar: se puede ejecutar **solo la función del juego**
que interesa dentro de un emulador de CPU ([Unicorn](https://www.unicorn-engine.org/))
y comparar sus resultados con los de p3vita.

- Solo se emula la CPU x86 con las secciones de `Patrician3.exe` en memoria; no
  se carga Windows ni ninguna DLL, no se abre ninguna ventana y no se toca el
  sistema. Si el código intenta salir de la función (por ejemplo, llamando a
  una importación), la emulación se aborta.
- Se prepara en memoria emulada lo que la función lee (una ciudad, una tabla
  global...), se la llama con su convención y se lee el resultado.
- La FPU se pone a 53 bits de precisión (palabra de control 0x27F), como la deja
  el runtime de MSVC al arrancar el juego (`_controlfp(_PC_53, _MCW_PC)` en
  0x63EF71); el juego no la vuelve a cambiar. Con 64 bits, algunos precios en la
  frontera entre dos enteros salen distintos.

## Precios

`tools/oracle/price_oracle.py` llama a `get_buy_price` (0x52E430, `stdcall`:
mercancía, índice de ciudad, cantidad) y `get_sell_price` (0x52E1D0, `thiscall`
con el objeto 0x6DE3D8, cuyo float en +0x64 es el factor de dificultad) con una
ciudad de prueba: existencias en +0x4, umbrales en +0x4F0, mercado en la bandera
0x100 de +0x76C y la mercancía con recargo en el byte +0x6F0.

```sh
python3 -m pip install --user unicorn
python3 tools/oracle/price_oracle.py <ruta>/Patrician3.exe 20000 /tmp/casos.csv
build/tools/oracle/compare_prices /tmp/casos.csv
```

Resultado: los 20 000 casos que genera este script (umbrales aleatorios)
salen idénticos al entero, y también otros 20 000 hechos con los umbrales reales
de las 24 ciudades de `missav/1.pat`; cubren todas las mercancías, las tres
dificultades, ciudades sin mercado, mercancía con recargo, compras mayores que
las existencias y almacenes con menos de una unidad. `tests/prices_test.cpp` guarda una selección de esos casos para
no depender del ejecutable.

### Lo que el código del juego añade a P3Modding

- El precio es un entero: el resultado se **trunca** (`__ftol`), no se redondea.
- Las cuentas se hacen en float de 32 bits: el factor de cada tramo es una recta
  sobre la posición absoluta (`pendiente · (inferior + superior) / 2 + ordenada`)
  y se acumula `cantidad · factor` tramo a tramo; el precio base se multiplica
  al final.
- Comprar: hace falta al menos una unidad en el almacén (200, o 2000 para las
  mercancías por cargas); lo que se pida por encima de las existencias se cobra
  al factor 4,0; si la mercancía es la del byte +0x6F0 de la ciudad, el precio
  se multiplica por 1,15 (0x673A7C). Las partidas de `missav/` tienen 0xFF
  (ninguna) en todas las ciudades.
- Vender: se ajustan a 0 las existencias negativas; con cantidad < 1, 0.
- Sin la bandera de mercado (0x100), ambas devuelven 0.

## Partidas y comercio

`tools/oracle/save_oracle.py` hace lo mismo con una partida entera: carga en
memoria emulada el mundo (0x52FB60) y los barcos (0x504230) de una partida ya
descomprimida con el propio código del juego y vuelca, tal como quedan en
memoria, las ciudades (existencias y umbrales de precio), los comerciantes, las
oficinas y los barcos. Después ejecuta compras y ventas aleatorias con los
manejadores de las operaciones 0x02 y 0x01 (0x537450 y 0x537340) sobre los
barcos atracados de todos los comerciantes, cambiando a veces el dinero del
comerciante y la dificultad, y anota el resultado de cada una.
`tools/oracle/compare_save.cpp` lee la misma partida con `src/p3data`, repite
las operaciones con `src/p3data/trade.cpp` y compara campo a campo; el precio
medio de la carga, bit a bit.

Para que el código de carga corra fuera del juego, el script sustituye
`operator new`/`delete` por un montón propio, pone un puntero falso en
0x6DF2E0 (si no, el constructor del comerciante abre
`scripts/Expeditionen.dat`), crea los nodos centinela de las 15 `std::list`
del mundo (que no se ha construido) y salta dos funciones que no leen nada del
fichero: la matriz de distancias entre ciudades (0x4482C0) y el cálculo de la
ruta de un barco por el mar (0x445010).

```sh
build/tools/p3save/p3save p2arch0_eng.cpr missav/1.pat raw /tmp/1.raw
python3 tools/oracle/save_oracle.py <ruta>/Patrician3.exe /tmp/1.raw /tmp/1.csv 2000
build/tools/oracle/compare_save /tmp/1.raw /tmp/1.csv
```

Resultado: en las 12 partidas v0x82 de `missav/` (`1`–`7.pat` y `tut0`–`tut4.pat`)
coinciden todos los registros (dificultad del comercio, ciudades con sus 96
umbrales, comerciantes, oficinas y barcos) y 24 000 compras y ventas: cantidad, precio, dinero,
existencias, carga, precio medio, bodega ocupada y equipo.

Esta comparación encontró algo que el oráculo de precios no podía ver: el
juego recalcula los umbrales de precio al cargar cada ciudad y no usa los
guardados (ver [formats/savegame.md](formats/savegame.md), §4.1). Con los
guardados, las compras de ladrillos de cuatro partidas salían más baratas que
en el juego.
