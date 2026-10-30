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

Con `--navdata` (una carpeta con `navdata/` y `scripts/StadtDaten.ini`,
sacados con `cprtool extract`), el script crea además el objeto de navegación
del juego, deja que calcule las rutas entre ciudades al cargar y las vuelca;
`compare_save`, con el archivo CPR como tercer argumento, las compara con
`src/p3data/navigation.cpp`. Resultado: las 6633 rutas de las 12 partidas,
idénticas punto a punto. Así se encontraron, por ejemplo, que las distancias
van en 16.16 y que la visión directa recorre la línea desde el segundo punto.

```sh
build/tools/cprtool/cprtool extract p2arch0_eng.cpr /tmp/datos navdata
build/tools/cprtool/cprtool extract p2arch0_eng.cpr /tmp/datos stadtdaten
python3 tools/oracle/save_oracle.py <ruta>/Patrician3.exe /tmp/1.raw /tmp/1.csv 200 --navdata /tmp/datos
build/tools/oracle/compare_save /tmp/1.raw /tmp/1.csv p2arch0_eng.cpr
```

Esta comparación encontró algo que el oráculo de precios no podía ver: el
juego recalcula los umbrales de precio al cargar cada ciudad y no usa los
guardados (ver [formats/savegame.md](formats/savegame.md), §4.1). Con los
guardados, las compras de ladrillos de cuatro partidas salían más baratas que
en el juego.

## Navegación

`tools/oracle/sail_oracle.py` carga la partida con navegación (como
`save_oracle.py --navdata`) y hace pasar el tiempo con el código del juego:
en cada tick sube el tick del mundo, recalcula la fecha al empezar el día
(0x5310D0) y llama a la actualización de barcos (0x506720) entera. Para que
corra fuera del juego, unos ganchos en los dos saltos por estado de la
función dejan pasar solo los estados que reproduce `src/p3data/sailing.cpp`
(en puerto: 1, 2, 3 y el 0 de los barcos de jugadores humanos; en el mar,
0x0F) y saltan el bucle de los convoyes. Las llamadas a la interfaz, a la
escena del puerto y a la cola de mensajes se cambian por funciones vacías, y
también las decisiones de la IA (pedir una ruta, buscar otro puerto, la ruta
automática), que quedan anotadas. Cada 24 ticks de media da una orden de
zarpar (operación 0x00, 0x537670) a un barco al azar de un comerciante que no
va en convoy ni está en los estados 6 u 8, hacia una ciudad o, una de cada
diez, hacia un punto de agua del mar. Vuelca cada N ticks y tras cada orden el
estado de movimiento de todos los barcos, sus rutas cuando cambian, las
cabezas de las dos listas y la mercancía en camino de cada ciudad.
`tools/oracle/compare_sail.cpp` repite los mismos ticks y órdenes con
`src/p3data` y compara.

```sh
python3 tools/oracle/sail_oracle.py <ruta>/Patrician3.exe /tmp/1.raw /tmp/datos /tmp/1-nav.csv 8192 32
build/tools/oracle/compare_sail p2arch0_eng.cpr /tmp/1.raw /tmp/1-nav.csv
```

Resultado: en las 12 partidas v0x82, 32 días de juego (8192 ticks) en cada
una, con 4065 órdenes de zarpar (415 hacia el mar), 0 diferencias en 426 316
estados de barco, 4626 rutas y las listas y la mercancía en camino. Por el
camino pasan unas 1900 salidas de puerto, 2400 llegadas, 80 barcos que dan la
vuelta al entrar o al salir y 200 que se quedan en el mar al final de su
ruta. No ha habido hundimientos ni puertos bloqueados. Así se encontraron,
por ejemplo, que la orden a un barco de un convoy o del estado 6 sigue otro
camino y que, si el punto de destino no tiene agua cerca, el juego lee fuera
de su tabla de ciudades al salir del puerto (ver
[formats/navigation.md](formats/navigation.md), §6).

## Ciudades

El tick de ciudad (0x51BA10) son unas 9000 instrucciones repartidas en más de
cien funciones, y toca casi todo el struct town (muchos campos sin nombre), las
oficinas, los comerciantes y algunos barcos. Para compararlo entero, p3vita
guarda esos objetos con la misma disposición que en memoria
(`src/p3data/game_memory.hpp`) y la comparación es byte a byte.

`tools/oracle/town_oracle.py` carga la partida como el juego (además de lo de
`save_oracle.py`: los ajustes en `*0x6CC3E8`, 0x5348B0, el jugador en
0x6DFC14, la dificultad del comercio y las tres secciones que siguen a los
barcos, con la cola de eventos) y hace pasar los días con la fecha y los ticks
de ciudad del juego. `rand` se lleva fuera, con el generador de MSVC y semilla
1 (el estado es por hilo); se saltan la interfaz (0x548CA0, 0x4D6530,
0x613920, y al cargar la tabla de textos 0x511270), y las operaciones que se
encolan (0x54AA70) se anotan sin ejecutarlas. Cada vez que se entra en una de
las funciones pedidas guarda el estado de antes y el de después: copias de la
memoria por regiones (globales, ciudades, oficinas, comerciantes, barcos,
capitanes, registros del mundo, asedios, cola de eventos, registros de
edificio, rejillas y nodos de cada ciudad, `rand`) como diferencias, y `esp`
para resolver punteros a la pila del llamador. Se pueden grabar a la vez
funciones que se llaman entre sí.

`tools/oracle/tick_probe.cpp` repite cada llamada con su traducción
(`src/p3data/town_tick`, registradas en `tools/oracle/probes_*.cpp`) y
compara todo, también eax y las operaciones encoladas:

- por omisión, cada llamada parte del estado del juego justo antes de ella;
- `--continuo`: p3vita evoluciona solo desde el principio (con 51ba10);
- `--carga partida.raw`: compara el estado del juego recién cargada la partida
  con lo que lee `src/p3data/savegame.cpp`; con `--continuo`, la simulación
  parte de esa lectura.

```sh
scripts/tick-probe.sh 527d40,51c830 8            # 8 días en las 12 partidas
python3 tools/oracle/town_oracle.py <exe> /tmp/1.raw /tmp/1.probe 60 51ba10
build/tools/oracle/tick_probe /tmp/1.probe --continuo --carga /tmp/1.raw
```

Resultado: 0 diferencias en cada una de las funciones traducidas (las de
las seis partes, de 8 a 96 días en las 12 partidas, y con estados retocados
para recorrer ramas que las partidas no recorren), en la carga de las 12
partidas y en el tick de ciudad entero partiendo de la partida que lee
p3vita: 60 días en las 12 partidas y 240 días en `1.pat`, `3.pat` y
`tut2.pat` (23 040 ticks de ciudad). Por el camino se encontraron, por
ejemplo, los 11 movimientos de dinero de la ciudad (el fichero guarda 0x2C
bytes y no 0x28), que las operaciones encoladas son de 0x14 bytes y que la
cola de eventos de la partida decide si la ciudad avisa de sus obras. Ver
[formats/town_tick.md](formats/town_tick.md).

## El mundo entero: ciudades, barcos y la IA

Con `--barcos DATOS` (la carpeta de `--navdata`), `town_oracle.py` hace en
cada tick lo que reproduce p3vita de `advance_time`: la fecha, el tick de
ciudad, la actualización de barcos del juego (0x506720) con los estados en
puerto 0 a 5 (también las decisiones de los barcos de la IA, 0x518B80) y en el
mar el 0x0F, sin convoyes en el mar, piratas ni encuentros, y al final del
tick ejecuta las órdenes de zarpar encoladas (0x537670). `--sin-ia` deja los
barcos como en `sail_oracle.py`. Con los barcos ya como memoria del juego, la
comparación es del barco entero salvo el puntero de la ruta.

`tick_probe --continuo --carga partida.raw --cpr p2arch0_eng.cpr` repite una
grabación de `51ba10,506720,537670` partiendo de la partida que lee p3vita:
el mundo entero tick a tick (`Sailing` y `advance_game_tick`).

```sh
python3 tools/oracle/town_oracle.py <exe> /tmp/1.raw /tmp/1-w.probe 60 51ba10,506720,537670 --barcos /tmp/datos
build/tools/oracle/tick_probe /tmp/1-w.probe --continuo --carga /tmp/1.raw --cpr p2arch0_eng.cpr
```

Resultado: 0 diferencias byte a byte en las 12 partidas v0x82 durante 60
días (15 360 actualizaciones de barcos y unos 1440 ticks de ciudad por
partida), con los barcos de la IA comerciando, eligiendo destino, yendo al
astillero y formando convoyes. Sin la IA (`--sin-ia`, y `tick_probe
--sin-ia`), también 0 en 32 días; el oráculo de navegación de antes sigue en
0 con `Sailing::set_ai(false)`.

Las funciones de la IA se comprobaron también una a una (grabando con
`P3_SHIPS=/tmp/datos scripts/tick-probe.sh ...`) y con estados retocados al
azar para las ramas que las partidas no recorren. Así se encontraron, por
ejemplo, que el juego exige el casco por encima del 20 % solo a los barcos
con capitán (p3vita lo tenía al revés), que la IA crea convoyes en puerto, los
avisos a la interfaz que pone cada estado y la lista de barcos ordenada por x.
