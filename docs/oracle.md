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

## Convoyes

`town_oracle.py --barcos` ejecuta ahora también el recorrido de los convoyes
de 0x506720 (`--sin-convoyes` lo salta, como antes). Los convoyes de los
piratas se quedan quietos: en el salto por estado (0x50719D) pasan al
siguiente, porque sus ataques acaban en batallas (0x50BC40 → 0x605420) que
el emulador no puede ejecutar (se descubrió al dejarlos: la emulación se
caía en 7 de las 12 partidas). Tampoco se ejecutan los encuentros del final
(0x5053C0). Las grabaciones llevan una región `Q` con la ruta de cada barco
que tiene (en el juego, [+0x24] es un puntero), que tick_probe reconstruye y
compara: así las funciones que mueven barcos por su ruta se comprueban una a
una. Con `--cpr`, tick_probe crea la navegación (`Sailing`) también para las
llamadas sueltas, y las sondas la tienen en `probe_sailing()`.

Las funciones de los convoyes (0x502110 y lo que cuelga, 0x5017F0, 0x509F80,
0x50A580, 0x50A3D0, 0x501EB0, 0x503B50...) se tradujeron en tres partes, cada
una comprobada sola en las 12 partidas (de 8 a 150 días) y con convoyes,
barcos y ciudades cambiados al azar para las ramas que no salen; el
recorrido, al juntarlas. Resultado del mundo entero (ciudades, barcos, IA,
convoyes y órdenes de zarpar) partiendo de la partida que lee p3vita: **0
diferencias byte a byte en las 12 partidas durante 60 días**, comparando
también las rutas. Por el camino apareció que p3vita no leía los 15 objetos
del mundo de [+0xB10] (0x52BBE0), que usa 0x534F10 para las operaciones
que encola un convoy al zarpar: la comparación de la carga no los miraba
porque solo compara los globales que llena el lector.

El tick de los comerciantes (0x4F7BB0) también está en el oráculo del
mundo (`--sin-comerciantes` lo salta); para repetir con tick_probe
`--continuo` hay que grabar `4f7bb0` con el resto. Traducido
([formats/town_tick.md](formats/town_tick.md) §9), el mundo entero con
ciudades, comerciantes, barcos, IA y convoyes sale idéntico al juego, **0
diferencias en las 12 partidas durante 60 días**:

```sh
python3 tools/oracle/town_oracle.py <exe> /tmp/1.raw /tmp/1-w.probe 60 51ba10,4f7bb0,506720,537670 --barcos /tmp/datos
build/tools/oracle/tick_probe /tmp/1-w.probe --continuo --carga /tmp/1.raw --cpr p2arch0_eng.cpr
```

El oráculo de partidas (`compare_save`) compara ahora, después de cada
compra y venta, las estadísticas del comerciante (ventas [0x49C], compras
[0x4A0]) y su reputación en la ciudad: 0 diferencias en las 24 000
operaciones de las 12 partidas.

## El resto de advance_time

Con `--barcos`, el oráculo del mundo hace ahora por omisión todo el resto de
advance_time (`--sin-resto` lo quita; `--resto PIEZAS`, de
`5315c0,535450,52d7d0,6349f0,4d85c0,633a90`, solo las pedidas): lo diario,
las tareas programadas, los 15 objetos del mundo, los asedios y la cola de
eventos, en el orden del juego
(ver [formats/world_tick.md](formats/world_tick.md)); las operaciones
encoladas se ejecutan al final del tick. Con `--cpr` crea con el código del
juego lo que la cola de eventos necesita: el objeto de los nombres
(`scripts/Namen*.txt`), la tabla entera de `StadtDaten.ini` y
`Expeditionen.dat` (p3vita, con `load_game_scripts`). Las grabaciones llevan
los ejércitos sitiadores y las listas de los asedios (regiones `Ji`, `Ui`,
`Li`, `Hi`), y la carga de las 12 partidas compara ya los asedios: 0
diferencias. Resultado: el mundo entero con lo diario, las tareas, los
objetos del mundo y los asedios, idéntico 30 días en las 12 partidas.

La cola de eventos se tradujo en dos tandas (la segunda, en cuatro grupos
de tipos, cada uno con su agente y comprobado función a función en las 12
partidas y 60 días, más unos pocos estados retocados por rama). Para ella
la grabación lleva también el buzón de avisos (región `B`, *0x6DD730) y los
5 objetos de los piratas (`Z0..Z4`, *[0x6DD7AC + 4·i]) y la lista de encargos
de cada uno de los 15 objetos del mundo (`F0..F14`), y p3vita los lee de la
partida. Con todo junto salía igual en 11 de las 12; tut3 se separaba el día
18 en la llegada de una expedición (evento 0x12, 0x4E6B4A–0x4E79F1), que se
tradujo después. Resultado, el mundo entero con todas las piezas (las que el
oráculo hace por omisión):

```sh
python3 tools/oracle/town_oracle.py <exe> /tmp/1.raw /tmp/1-w.probe 60   51ba10,4f7bb0,506720,537670,5315c0,535450,52d7d0,6349f0,633a90,4d85c0 --barcos /tmp/datos --cpr p2arch0_eng.cpr
build/tools/oracle/tick_probe /tmp/1-w.probe --continuo --carga /tmp/1.raw --cpr p2arch0_eng.cpr
```

**0 diferencias en las 12 partidas durante 60 días** (unos 56 minutos con
las 12 en paralelo), y la carga, también en 0.

## Las operaciones encoladas y la candidatura a Gobernador

El oráculo del mundo ejecuta ahora al final de cada tick las operaciones
encoladas con el ejecutor del juego (0x535760, ver
[formats/world_tick.md](formats/world_tick.md) §7), en su orden, las de los
códigos de `--operaciones` (por omisión `0,12,1e,1f,22,23,24,27,3b,46,4b,78,79,7a,7b,85,86`:
zarpar, construir con sus materiales y murallas, los encargos de la Hansa y
el alcalde de una ciudad nueva); las demás se siguen descartando, como en
p3vita
(`run_operation`, `simulation.cpp`). El ejecutor se llama por un trampolín
que le deja en la pila los 0x14 bytes de la operación y la marca «P3OP», así
que al grabar `535760` tick_probe repite cada operación (sonda en
`probes_alderman.cpp`). El juego también llama a 0x535760 desde dentro (el
evento 0x03, los barcos), con la operación en su pila: esas muestras no
llevan la marca y tick_probe las salta («llamadas saltadas»); las repite la
sonda de la función que las contiene. Para el mundo entero con `--continuo`
se graba `535760` en vez de `537670` (las dos a la vez repetirían la orden de
zarpar; tick_probe lo rechaza):

```sh
python3 tools/oracle/town_oracle.py <exe> /tmp/1.raw /tmp/1-w.probe 60   51ba10,4f7bb0,506720,535760,5315c0,535450,52d7d0,6349f0,633a90,4d85c0 --barcos /tmp/datos --cpr p2arch0_eng.cpr
build/tools/oracle/tick_probe /tmp/1-w.probe --continuo --carga /tmp/1.raw --cpr p2arch0_eng.cpr
```

Resultado con el ejecutor: el mundo entero sin retocar, 0 diferencias en las
12 partidas durante 7 días (los 60 no caben en una ejecución en primer
plano; en las partidas no sale ninguna 0x85 ni 0x86, así que lo único nuevo
es que la orden de zarpar pasa por el ejecutor).

**Estados retocados.** `tools/oracle/retouch.py` graba como el oráculo del
mundo partiendo de la partida cambiada tras la carga por un escenario, y
tick_probe repite cada llamada desde el estado del juego (sin `--continuo`
ni `--carga`: p3vita no conoce el retoque). Los escenarios de la candidatura
a Gobernador (un alcalde con 120 de reputación, que las partidas no tienen
en 60 días; ver la cabecera del script) se comprobaron grabando
`4f7bb0,5326e0,535760,4d85c0` durante 3 días:

| Escenario | Partidas | Resultado |
|-|-|-|
| `candidato` (un alcalde de la IA) | las 12 | 0 diferencias: la candidatura, sus 5–6 operaciones, el tick de los comerciantes y la cola de eventos (con los encargos nuevos) |
| `jugador` (el jugador humano) | 1, 3 | 0 diferencias |
| `dos_candidatos` (la segunda candidatura no crea nada) | 1, 5 | 0 diferencias |
| `sin_carretera` (sin encargo de carretera) | 1 | 0 diferencias |
| `encargos_viejos` (la 0x86 anula uno guardado y salta el que tiene comerciante) | 2 | 0 diferencias, sin grabar 4d85c0: el encargo con comerciante pasa por la parte del evento 0x32 que aún falta |

Por el camino apareció que el plazo del encargo de la carretera lee la
tabla de los meses (0x672D78) hasta la entrada 129: p3vita lleva copiados
esos 130 valores del ejecutable.


## Los encargos de la Hansa

El evento 0x32 con comerciante y las operaciones 0x4B y 0x3B
([formats/world_tick.md](formats/world_tick.md) §8) se comprobaron con más
escenarios de `retouch.py`. Estos hacen candidato a un comerciante con el
código del juego al cargar (la candidatura y sus operaciones, sin grabar) y
colocan cada encargo en una rama; graban `535760` y `4d85c0` (con
`51ba10` el de las obras de la carretera). Cuando hace falta un barco quieto
o un barco navegando junto a la guarida, el escenario mueve la guarida (su
posición en la tabla de StadtDaten.ini, en los globales) y no el barco, y
puede hacer pasar el mundo sin grabar hasta el tick anterior al encargo.

| Escenarios | Partidas | Resultado |
|-|-|-|
| `encargo_nuevo`, `encargo_cumplido`, `encargo_vencido`, `barco_perdido`, `jugador_coge`, `jugador_vencido` (lo común y los tipos 2 y 4, la operación 0x4B) | 1, 3 | 0 diferencias |
| `carretera_ia`, `carretera_tramos`, `carretera_ultimo`, `carretera_obra`, `carretera_hecha`, `jugador_carretera_vencida`, `jugador_materiales` (el tipo 1 y la operación 0x3B) | 1, 3 (`carretera_ultimo` también 5 y tut2) | 0 diferencias, también en las ciudades (rejilla, registros, obras) |
| `guarida_lejos`, `guarida_cerca`, `guarida_rondando`, `guarida_hecha`, `guarida_fallida` (el tipo 3 sin el ataque) | 3, 5 | 0 diferencias, también en los barcos piratas nuevos |
| `fundar` (el tipo 0: la ciudad nueva) | 1, 3 | 0 diferencias en la fundación (0x4EBF54 con 0x531D50 entero) y en cada pieza suelta, y en los 6 días siguientes grabando todo el mundo (los ticks de la ciudad nueva, su asedio, su muralla con el evento 0x39) |
| `fundar_aceite` (la máscara de cuando falta aceite: el nodo de 0x5462F0) | 1, 3 | 0 diferencias grabando `4ebf54` (2 días) y `51ba10,4d85c0,535760` (4 días: los ticks de la ciudad nueva ya trabajan con el nodo) |

Para fundar una ciudad el oráculo lee del archivo como el juego
(`Archiver.dll`: `arc_CreateFile`, `arc_FileLength`, `arc_Read` y
`arc_ReleaseFile`, desde DATOS o `--cpr`), y tick_probe da a las sondas las
plantillas de mapa (`SaveGame::town_templates`). El registro que recibe
0x531D50 está en la pila del juego, fuera de lo que graba el oráculo: se
comprueba entero grabando 0x4EBF54.

No se puede comprobar el ataque a la guarida (un barco justo en ella:
empieza una batalla).

**El desarrollo de la ciudad nueva** (evento 0x2F, 0x4EA0D4, y la operación
0x46, 0x535FD6; [formats/world_tick.md](formats/world_tick.md) §8). El
evento llega a los 180 días de fundar, así que los escenarios lo adelantan
al tick siguiente: lo desenganchan de la cola, lo ponen en cabeza y lo
colocan con 0x4D8DD0 del juego (o, en una ciudad que ya existe, lo crean con
0x4D8CF0), con las fases y los bits de [0x76C] de cada rama. Los cuatro
primeros fundan antes la ciudad sin grabar (`fundar` y el mundo hasta que
0x4EBF54 la crea); los otros dos usan ciudades de la partida de más de 1000
habitantes, que ya tienen muralla. La muralla (0x5252F0) se dirige con los
puntos de muralla [0x7B0 + 2·i] (0x4FCFE0), que solo cuentan sin piezas de
muralla pendientes ([0x766]): en la partida 3 todas las ciudades tienen, así
que allí no sale 0xFF. Graban `4d85c0,535760`; la rama tomada se ve en el
tick al que vuelve el evento (`retouch.py` lo escribe al final):

| Escenario | Días | Ramas | Partidas | Resultado |
|-|-|-|-|-|
| `nueva_sin_marca` | 2 | sin el bit 1 de [0x76C]: 21 días | 1, 3 | 0 diferencias |
| `nueva_alcalde_ia` | 2 | el fundador, de la IA, es su alcalde: su oficina (0x5308A0) y la fase 1; después la bandera 0x80 (4 pobres más) y menos de 500 habitantes: 28 días | 1, 3 | 0 diferencias |
| `nueva_alcalde_humano` | 2 | el jugador de alcalde: sin oficina; después 699 habitantes: 0x88DB − 35 · 699 ticks | 1, 3 | 0 diferencias |
| `nueva_sin_alcalde` | 16 | la operación 0x46 (el comerciante 23, el de la IA con más rango que no es alcalde; en la partida 1 con rango 2, entra por la marca 0xBADEAFFE), que el ejecutor ejecuta; a los 14 días, su oficina | 1, 3 | 0 diferencias |
| `poblada` | 3 | muralla 2 (partida 1, retocada) o 1 (partida 3): el mercado, a los 5 ticks la fase 2 y al día el prestamista (0x51FAB0 con 0x25); 0xFF: 28 días; 0 con un tramo de la ciudad en obras (el último de 1 o de 3): 14 días; 0 sin él: el mercado y 28 días | 1, 3 | 0 diferencias |
| `rica` | 2 | la fase 2 con el prestamista (bit 0x800): fase 3 y 21 días; sin él y 3 · 400 + 300 = 1500, que no pasa: 28 días | 1, 3 | 0 diferencias |

El mundo sin retocar, con la 0x46 entre las operaciones del oráculo: 0
diferencias en las 12 partidas durante 7 días.

## Las obras: la operación 0x22

Construir (0x53CE30 y lo que cuelga: 0x521900, la rejilla de `grid.cpp`,
las torres 0x523830) se comprobó función a función con el oráculo
ejecutando la 0x22 y grabando la función (`re/emu/sesion5/ops22.sh DÍAS
FUNCIONES`, fuera del repositorio: `town_oracle.py --operaciones
0,3b,46,4b,85,86,22` en las 12 partidas y `tick_probe` llamada a llamada):

| Funciones | Días | Llamadas | Resultado |
|-|-|-|-|
| 0x4FB9C0 (poner un edificio en la rejilla, con sus calles) y 0x4FCBD0 (si el solar vale) | 30 | 260 y 137 | 0 diferencias |
| 0x521900 (el edificio entero: registro, materiales de la oficina y los barcos, pago, reputación) | 30 | 137 | 0 diferencias |
| 0x523830 (las torres) | 30 | 35 | 0 diferencias |

Después, con la 0x22 entre las operaciones por omisión (en p3vita y en el
oráculo), el mundo entero sin retocar: 0 diferencias en las 12 partidas
durante 7 días (`world.sh 7`). No salen en las partidas: 0x523D70 (tipos
0x34 y 0x35), el almacén de un comerciante (0x4FF2D0, por reescribir), los
edificios 0x22, 0x29, 0x2A, 0x2B y 0x2C pagados por un comerciante, ni
construir sin oficina (0x4F5450).

Con los materiales (0x27) y las murallas (0x23) también entre las de por
omisión: el mundo entero sin retocar, 0 diferencias en las 12 partidas
durante 7 días (la 0x27 no sale en ellas), y el escenario `fundar` 40 días
grabando `535760,51ba10,4d85c0` (la ciudad nueva pide materiales, los recibe
y levanta su primer edificio municipal): 0 diferencias en las partidas 1 y 3
(85 operaciones, 1998 ticks de ciudad).

Las murallas (la operación 0x24, 0x522C70 con 0x51F3A0 y la rejilla):
llamada a llamada con `ops22.sh 30 522c70` (`OPS` con la 0x24), 4154
llamadas iguales en 30 días de las 12 partidas, entre ellas el cierre de la
muralla de la ciudad 16 de la partida 5. Con la 0x24 entre las de por
omisión, el mundo entero sin retocar, 0 diferencias en las 12 partidas
durante 7 días; y `fundar` 420 días grabando `535760` (partida 1): las 756
operaciones de la ciudad nueva y de las demás, iguales al juego (la ciudad
nueva levanta sus edificios municipales, acaba su muralla y empieza a
crecer: 272 habitantes).

**Qué operaciones cambian el mundo.** `re/emu/sesion5/opsdiff.sh PARTIDA
DÍAS` hace pasar el mundo del oráculo (sin grabar) con las operaciones que
reproduce p3vita y con cada una de las demás añadida, y compara cada día una
huella de cada región: en las partidas 1, 3 y 5 y 20 días cambian el mundo
la 0x7B, 0x78, 0x79, 0x7A, 0x1E, 0x1F, 0x12 y 0x64 (la 0x8E para el
emulador). Con la 0x79, la 0x7A y la 0x7B reescritas y entre las de por
omisión: el mundo entero, 0 diferencias en las 12 partidas durante 7 días.
La 0x1E (expediciones) salía distinta en la partida 2 porque `set_sail` no
hacía la rama de convoy de 0x537670; con ella reescrita, la 0x1E y la 0x1F
entre las de por omisión: el mundo entero, 0 diferencias en las 12 partidas
durante 7 días.
Con la 0x12 (capitanes) y la 0x78 (un barco nuevo) también: el mundo
entero, 0 diferencias en las 12 partidas durante 7 días.
La 0x64 (los robos en las oficinas del jugador, con el evento 0x33) sale
poco sola (en 20 días, una vez en las partidas 4 y 5, con el evento unos 8
días después). El escenario `robo` de `retouch.py` la encola dos veces al
final del primer tick con eventos a 16 y 32 ticks, uno en la oficina del
jugador con más mercancía y otro en una ciudad donde no tiene oficina
(`ret2f.sh robo:1`, grabando `535760` y `4d85c0`): 0 diferencias en las
partidas 1 a 7. Y entre las de por omisión, el mundo entero, 0 diferencias
en las 12 partidas durante 7 días.
La 0x8E (asedios) paraba el emulador: 0x629C60 lee la ciudad que se ve
([*0x6E51AC + 0xC324]). El oráculo salta ahí y en la ventana al jugador
(`EVENT_JUMPS`: 0x629CEA → 0x629D3B y 0x62B239 → 0x62B261). En 20 días crea
5 ejércitos en las partidas 4 a 7 (`re/emu/sesion5/armies.sh`); los de los
días 1 y 5, con el mundo entero (partidas 7 y 6, 7 días), y los de los días
9, 19 y 20 llamada a llamada grabando solo esos días (`late.sh
PARTIDA:DESDE:DÍAS`, con `--desde`; 535760, 633a90 y 6349f0): iguales al
juego. Las ramas que no salen, con el escenario `asedio` de `retouch.py`
(sin muralla, y la petición de ayuda 0x8B con el jugador de rango 5): 0
diferencias en 6 partidas (en la 3 todas las murallas tienen piezas
pendientes). Con la 0x8E entre las de por omisión, el mundo entero, 0
diferencias en las 12 partidas durante 7 días.
Con más días y la ciudad fundada salen otras: `opsdiff.py` con `FOUND=1`
funda antes la ciudad (`retouch.found_town`), y `opsdiff_n.sh DÍAS
PARTIDA...` compara varias partidas a la vez. En 40 a 200 días de las
partidas 1 y 3 cambian el mundo la 0x29, la 0x81, la 0x37, la 0x61 y la
0x2C. Las tres primeras, reescritas: llamada a llamada en la ciudad fundada
(`DESDE=14 FUNCS=535760 ret2f.sh fundar:32` en la partida 1 y `DESDE=28
... fundar:88` en la 3; `ret2f.sh` acepta `DESDE`), 193 y 2591 llamadas
iguales, y el mundo entero 7 días en las 12 partidas, 0 diferencias.

**Los eventos que p3vita creaba y no atendía** ([inventario.md](inventario.md),
parte A): escenarios de `retouch.py` que crean el evento para los ticks
siguientes, grabando `4d85c0` (`FUNCS=4d85c0 ret2f.sh ESCENARIO:DÍAS`):
`capitanes` (0x27, uno por estado de barco), `impuesto` (0x31, con deudas y
la queja a la Hansa) y `monumento` (0x38, con alcaldes de la IA puestos a
mano). Iguales en las partidas 1 a 7; con el impuesto, en las partidas 5 y 6
la IA de un comerciante endeudado encola después una 0x28 que p3vita no: es
la variable sin inicializar de 0x4F5A20 (fallo del juego; ver el roadmap), no
el evento. El monumento llama a 0x590680 si la ciudad es la que se ve: el
oráculo salta ahí (`EVENT_JUMPS`, 0x4ECD3E → 0x4ECD71).
Los incendios del asedio (0x25 y 0x26): `incendio` (partidas 3 a 7: una
torre, otra pieza de muralla, una empresa, una casa y un edificio de la
ciudad) y, sin retocar, los asedios de las partidas: `events_seen.sh DÍAS
TIPOS PARTIDA...` (re/emu/sesion5) dice cuándo aparecen (el primero, el día
13 de la partida 7) y `FUNCS=4d85c0 late.sh` los compara llamada a llamada
(días 13 a 19 de la 7, 19 a 23 de la 5, 28 a 32 de la 4 y 39 a 43 de la 3):
iguales. Las 60 jornadas de antes no llegaban a ninguno.
Las misiones de la dieta (0x23, 0x24, 0x34): en las partidas la dieta nunca
aprueba nada, así que el escenario `dieta` crea los tres eventos como los
deja el 0x21 al aprobar (fase 0). Grabando `4d85c0` y `535760` los días 1 a
12 (`dieta:12`) y 36 a 52 (`DESDE=36 ... dieta:52`) de las partidas 1, 3, 5
y 7: iguales (las 11 fases: la flota, la caza en el mar, la vuelta, la
protección, los encargos 0x81 y el final). El oráculo salta la ventana de la
fase 1 del 0x24 (`EVENT_JUMPS`, 0x4E0DB9 → 0x4E0E13).

**Las operaciones de la simulación que p3vita descartaba**
([inventario.md](inventario.md), parte B): cada una entre las de por omisión
(`OPERATIONS` de `town_oracle.py` y `run_operation`), con escenarios de
`retouch.py` que la encolan o la provocan, grabando `4d85c0` y `535760`
(`ret2f.sh ESCENARIO:DÍAS` con `SAVES`), y el mundo entero 7 días en las 12
partidas (`world.sh 7`, 0 diferencias tras cada una):

| Operación | Escenarios | Partidas | Resultado |
|-|-|-|-|
| 0x61, la fiesta, con el evento 0x07 | `fiesta:1` (la IA con precio, sin dinero, con deudas, sin oficina, un comerciante que no existe; el jugador con reputación 100 y la oficina llena en una ciudad con el bit 0x1000), `fiesta_vacia:1` (el jugador con una sola mercancía), `fiesta_ciudad:32` (la fiesta de la ciudad pasa y el evento 0x03 se adelanta: 0x5316E0 encola la 0x61 y el evento llega a los 12 a 30 días) | 1, 3, 5, 7 | iguales; en `fiesta_ciudad` de la partida 3, el día 20, una diferencia en el buzón que no es de la fiesta: el evento 0x03 adelantado le toca a un comerciante con avisos caducados (0x4D6780, por traducir) |
| 0x2C, un barco encargado al astillero | `astillero:1` (en la ciudad A con astillero y 150 de cada material: un comerciante de la IA con paño negativo en su oficina y un barco con ruta automática y materiales, otro sin oficina allí, el jugador con dinero; y no se pueden: un tipo grande en una ciudad con [0x2C8] & 1, una ciudad sin astillero, uno sin dinero y sin forzar, una ciudad sin materiales), `astillero_ciudad:1` (el evento 0x2B adelantado, con la ciudad 0 muy poblada y materiales: encola la 0x2C), y `astillero:8` grabando también `51ba10` y `506720` (el barco en obras en el astillero) | 1, 3, 5, 7 | iguales; en `astillero:8` de la partida 5, el día 1, la IA del comerciante al que el escenario deja 100 monedas encola una 0x28 que p3vita no: el fallo de 0x4F5A20 (variable sin inicializar, en el roadmap), no la 0x2C |
| 0x38 (subasta de barco), 0x7C (la peste), 0x7F (el bit 0x4000000 y los eventos 0x35), 0x80 y 0x83 (partidas en red) | `eventos_op:2` (la 0x80 la primera, sobre el registro 0x80 libre con el tipo 5 o en uso; la 0x38 a 40 días y a menos; la peste mañana y con un día pasado; la 0x7F con una ciudad que no existe y con un tick pasado; un evento 0x00 con grano) | 1, 3, 5, 7 | iguales |
| 0x84 (el evento 0x30), 0x67 (un administrador), 0x48 (una propuesta) | `propuestas:4` (la 0x84 pedida antes de 0x100 ticks y a 3 días; la 0x67 válida, con otro administrador y con un comerciante que no existe; la 0x48 como la ampliación de 0x51E270 en una ciudad con consejo y, en la ciudad 5 sin los bits 0xE0, el impuesto extraordinario y el impuesto por cabeza con 7 y con 25) | 1 a 7 (la 0x67, en 4, 5 y 6) | iguales |
| 0x28, un edificio en venta (0x4DAB00) | `subasta:2` (en la ciudad A se añade a una subasta en fase 2; en la B abre una nueva y otro edificio abre otra; sin el bit de la ciudad en [+0x468]; una celda vacía) y `subasta_ajuste` (con el byte [+0xD] de los ajustes a 0: el segundo se añade a la de fase 1), 2 días y 30 días (las subastas acaban y se venden) | 3, 5, 7 (2 y 30 días: 3 y 5) | iguales (en las partidas 1 y 2 ningún comerciante de la IA tiene dos empresas en la ciudad de una oficina) |
| 0x7E, la temporada de piratas | `temporada:1` (los bits 0..2 a 0 y otros) | 1, 3, 5, 7 | iguales |
| 0x7D, un incendio en la ciudad, con el evento 0x1D | `incendio_ciudad:4` (tres incendios, cada uno en su ciudad: en un almacén de un comerciante, 0x500C30; en una casa de una oficina; en un edificio de la ciudad; se extienden a 3 y 4 edificios, uno se para sin más cerca, y se apagan) | 1, 3, 5, 7 (en la 1 solo el de la casa) | iguales |
| 0x13, a human player's captain retires (event 0x27) | `capitan_jugador:1` (the player's first ship with a captain; and a captain that does not exist) | 1, 3, 5, 7 (in 1 the player's ship has no captain) | equal |
| 0x60, a wedding (event 0x15, with 0x50E000 and the feast) | `boda:2` recording also `506720` (the AI merchant with the biggest fleet, its first ship with a bigger hold, in a town with [0x2C8] & 1; the player; a merchant whose [+0x28] has passed; a past tick) | 1, 3, 5, 7 | equal |
| 0x4D6780, old letters (part D) | `fiesta_ciudad:32` (saves 3, the case of day 20) and `cartas:38` (event 0x03 moved to the player's day, letters kept 5 days: many types, 0x71 with its heap record, region L) | 1, 3, 5, 7 | equal |
| 0x4F69B0, a child from event 0x03 (part D) | `hijo_natural:1` (every AI merchant married 300 days ago, the even ones with 9 children; event 0x03 moved: the operation 0x87 it queues) | 1, 3, 5, 7 | equal |
| 0x0A, a ship deleted (0x507FB0) | `desguace:1` recording also `506720` (an AI ship in port, one at sea, a convoy member, the player's first ship; the wrong owner: nothing) | 1, 3, 5, 7 | equal |
| 0xB8, network sync checks | `red:1` and `red_activa:1` (kinds 0 with flag 0 and 1, 1 to 5 and 9; [0x6DF31C] 0 and 1) | 1, 3, 5, 7 | equal, also the operations queued in each call |
| 0x87, a child of a married merchant | `hijo:1` (married 300 days ago: one child, not a second right after; unmarried; married 100 days ago) | 1, 3, 5, 7 | equal |
| 0x65, an AI merchant goes bankrupt | `quiebra:2` recording also `4f7bb0` (made mayor of his home town, with a debt owed to him and one he owes; and the player: nothing) | 1, 3, 5, 7 | equal |


## Speed, the storms and the battle steps (2026-10-31)

From the cloud sessions merged on 2026-10-31 (notes in
[cloud/oracle-speed.md](cloud/oracle-speed.md) and
[cloud/battles.md](cloud/battles.md)).

- **About 4 times faster** (`cloud/oracle-speed`):
  - `regions()` reads each block once and takes the pointers and counts from the blocks it has already copied;
  - `write_diff` compares 4 KB blocks first;
  - the `Recorder` skips the snapshot before a call when nothing can have changed (checked from the code bytes);
  - `tick_probe` skips equal stretches with `memcmp`;
  - the PC builds use `-O2 -fwrapv -fno-strict-aliasing` when there is no build type (before, `-O0`).

  The `.probe` files are byte-identical to the old ones. `world.sh 7` in the 12 saves: 98 s on the PC (it was 392 s).
- **`P3_ORACLE_CHECK=1`**: the `Recorder` takes every skipped snapshot anyway and compares it (it raises if it differs). Use it after changing the recording machinery or the trampolines. New regions go through the fast readers of `regions()` (`read`, `block`, `gu32`), not `self.uc.mem_read`/`self.u32`.
- **The storms run by default** (0x5053C0 every 8 ticks, `src/p3data/battle/storms.cpp`), in p3vita (`Sailing`), `tick_probe` and `town_oracle.py`, as in the game; `--no-storms` turns them off in both (`--storms` is still accepted). The whole world for 7 days in the 12 saves: 0 differences with and without them. Recordings made before 61cdbfa have no storms: repeat them with `tick_probe --no-storms`. Retouched states: `tools/oracle/retouch_storms.py`.
- **The battle steps (decided, not implemented yet)**: the game steps the battles with operation 0x94, queued by the real-time driver (0x546620 → 0x612DB0) with steps taken from the real clock, 84 per tick on average at any speed. p3vita and the oracle will queue **one operation 0x94 with 84 steps after each tick in which there are battles**. The result of an automatic battle doesn't depend on how the steps are cut, only the tick in which it ends.
- **Known tool bug**: without `--barcos` (as `scripts/tick-probe.sh` runs it when `P3_SHIPS` is unset), `town_oracle.py` stops at the first snapshot with `UC_ERR_READ_UNMAPPED` in the routes (region `Q`): the ships' route pointers `[+0x24]` are the save's, which point nowhere without navigation. It was so before the speedup too. Fix: record `Q` only with `--barcos`, or clear `[+0x24]` on load without navigation.

## The pirates moving (2026-10-31)

From `cloud/battles`, piece 2 ([cloud/battles.md](cloud/battles.md) §7).
- `town_oracle.py --pirates` and `tick_probe --pirates` (`Sailing::set_pirates` in p3vita) move the pirate convoys: their state 0x12 (0x507A65) with 0x505C90, 0x50D9E0, 0x515360, 0x612930, 0x50E340 and 0x5159C0. **Off by default** until the automatic sea battle (piece 3) exists.
- With `--pirates` the oracle runs, at the load, the per-town waypoints 0x448780 (otherwise skipped with the navigation), from which the pirates choose where to go: 2.8 s more per load.
- Provisional rule, the same in p3vita and in the oracle (`PIRATE_JUMPS`): a pirate that would attack (0x507BE9 → 0x50BC40) goes on as if it had no target; a convoy that ends its route in a town (0x507C0B) goes on as if it still sailed; a convoy at its nest is not disbanded (0x505FF1 → 0x506029, until 0x514B80 is rewritten).
- Checked: call by call, 0 differences in the 12 saves for 7 days; the whole world with the pirates on in both, 0 differences in the 12 saves; the default world, unchanged.

## What tick_probe skips: the pointer fields (2026-11-01)

From `cloud/unchecked-branches` ([cloud/unchecked-branches.md](cloud/unchecked-branches.md)). Until then `compare` skipped, in every region, any 4-byte word whose game value looked like a pointer (`looks_like_pointer`: the oracle's stack 0x01000000..0x010FFFFF, save file 0x04000000.. and heap 0x10000000..0x17FFFFFF). That hid grid words (0x10101010...), building records (type 0x10..0x17 in the high byte), town, office and merchant fields, the businesses' staff, event bytes (a started road task's `01 merchant 00 01`) and routes. Now `pointer_field(region, word, game bytes, value)` decides:
- the globals (`G`) and the armies (`J*`): the old rule, because not all their pointer fields are known;
- elsewhere, only the known pointer fields: towns `T` [+0x75C], [+0x80C], [+0x9B4] (records, grid, nodes); merchants `M` [+0xE4], [+0xE8] (the name's strings); ships `P` [+0x24] (the route, apart in p3vita); sieges `X` [+0x8], [+0x101C], [+0x1020]; nodes `N*` each node's address; pirates `Z0..Z4` [+0x4]; letters `B` [+0x8] of types 0x29, 0x47, 0x63, 0x83 and [+0x8], [+0xC] of 0x3C..0x40 and 0x71; events `E` [+0x8], [+0xC] of type 0x1B and [+0x8] of 0x34 (the buffers p3vita's loader leaves at 0). Everything else is compared.

A new region with pointers must add its fields here; otherwise they show up as differences. The whole world for 7 days in the 12 saves stayed at 0 differences with the new rule, and every retouched scenario gave the same equal calls.

## The pirates and the sea battles, on by default (2026-11-01)

From `cloud/battles-2` ([cloud/battles-2.md](cloud/battles-2.md)), merged on 2026-11-01.
- **On by default** (the user's decision): the pirates move and attack, and their automatic sea battles run, in p3vita (`Sailing::set_pirates`, also in the viewer), `town_oracle.py` and `tick_probe`; `--no-pirates` turns them off in both, and `--pirates` and `--battles` are still accepted. Recordings made before this have no pirates: repeat them with `tick_probe --no-pirates`. The oracle is about 10 % slower with them (`world.sh 7`: ~160 s for the 12 saves on the PC).
- **New regions**: `ZMi`, `ZDi`, `ZPi` (what pirate i points to: the map of its nest, its cells, the pool; recorded even when empty), `BT` (the battles object of 0x6E55D0 and every heap block that hangs from it, the pointers turned into block indices) and `BA` (their addresses, for the probes). The battles' bit tables can hold 0x10000000, which looks like a heap pointer: the oracle, p3vita and tick_probe treat them as numbers.
- **The load reads the battles** (0x611AE0, the third section from the end): saves 6, 7 and tut4 start with their saved sea battle (pirates against a merchant; tut4's has a human side and waits for the player's window, operation 0x96, each step). Apart from the new regions, the default recordings change only in those three saves, by heap addresses (an `ecx` of 0x535760, a pointer in the globals): checked region by region against main's for saves 1, 3, 5, 6, 7 and tut4 and the `robo` scenario.
- **The clock rule**: after each tick with battles, one operation 0x94 with 84 steps (`BATTLE_STEPS`; p3vita `Sailing::step_battles`).
- **`HeapReAlloc`** keeps only the old block's requested bytes and zeroes the rest, as `alloc` (it used to copy up to the rounded size, bringing stale bytes of the slack).
- **Jumps over the interface**: the news window at a sea battle's end (0x60EB55 → 0x60EBAC, in `PIRATE_JUMPS`), and `WINDOW_JUMPS` (always on): 0x469380 opens the news ticker's window at once only when no battle is open ([0x6E59CC] = 0xFF, [0x6E59CD] = 0), which is the case once the battles are read; the emulator has none (0x4B4E30 jumped to 0 with `retouch_unchecked.py road_expired`), so the message always waits (0x46938F → 0x4693A1), as before.
- Provisional rule left (`PIRATE_JUMPS`): a convoy that ends its route in a town (0x507C0B) goes on as if it still sailed, instead of attacking the town (town battles, piece 6). The battles' rocks are multiplied by half the town view's width (*0x6E51AC), which the emulator has not: 0 in the oracle and in p3vita (`kTownViewHalf`), so the obstacles agree with each other, not with the game.
- Checked on the merge: the whole world 7 days in the 12 saves, 0 different with the pirates (now the default) and without; the writer's checks as before, and save 7 after 4 days now 0 bytes different (the battles are written from memory); the 19 player scenarios and the 47 of `unchecked.sh` (saves 3 and 5) as before.

## A battle of the player's ship (2026-11-03)

A battle with a human side used to wait forever: each step queued operation 0x96 for the player's sides and nothing executed it. 0x96 (0x536C52 → 0x60FEC9 on the battle in that slot) hands a merchant's sides to the computer (with 1) or back to the player (with 0); p3vita has it (`Sailing::fn_60fec9`, `run_operation`) and the oracle executes it with the pirates (`PIRATE_OPERATIONS`).
- **No battle window** (the question «Do you want to direct the ships by hand?», 0x41D9B0 on *0x6CBB10, part 7 of the battles). `BATTLE_WINDOW_JUMPS`, with the pirates: at a battle's setup a human side of the player takes the game's path for a player busy with another battle window (0x60659B → 0x606BAE: operation 0x96 with 1 at once and the side's count 0xFFFF), which p3vita takes too; the step never opens the window of a battle that asks for it ([+0x12E9] 1, from a save: 0x606E7E → 0x606EA5, 0x606FEA → 0x607011), nor does a boarding with a human side of the player (0x60CA4E → 0x60CA6F), skipping the window's writes as p3vita does.
- **The boarding's message** (0x60C3D6..0x60CA6F, `Sailing::boarding_message`): when a ship of the player boards or is boarded, the game writes «The crew of the "%s" has captured / plundered %s "%s" %s.» or «Your ship "%s" lost the boarding fight against / was plundered by ...» into the battle's [+0x1310], whether the side is human or not. p3vita writes it with the texts of the executable (`GameTexts::raw`, the game's sprintf `GameTexts::sprintf_raw`, the exact names of 0x512A60 and 0x512AE0); tick_probe loads them from the `Patrician3.exe` next to the CPR (`--exe` for another), so the comparison is byte for byte.
- **A pirate convoy without a guide** (its guide taken by the player's ship; also when its pirate goes away): 0x507A65 sorts ship 0xFFFF in the list by x (0x50D0C0), past the ships array; the emulator's zeros there make the list a ring and the sort never ended (event_attack, save 1). The oracle returns at once from 0x50D0C0 for a ship that doesn't exist (`_sort_x_guard`, with the pirates), and p3vita does nothing with a convoy without a guide (`pirate_convoy`; it used to read past the vector).
- **Numbers that look like pointers in the battles**: a sea battle's scene's arrays of bytes and u16 ([+0x18], [+0x1C], [+0x24]) and the items' boxes ([+0x28], [+0x2C], [+0x30], [+0x38], [+0x3C], [+0x40]: x1, x2 and y1, y2 per item) are numbers, like the bit pages: two coordinates such as 0x0CEA, 0x1028 make 0x10280CEA, which fell inside a heap block and the oracle turned it into a pointer (event_attack, save 5). `SCENE_NUMBER_ARRAYS` in the oracle, `BattleGraph::bit_pages` in p3vita and tick_probe.
- Checked: the whole world 7 days in the 12 saves, 0 different (tut4's saved battle, with a ship of the player, now ends: the «Viking» captures a pirate cog); `retouch_pirates.py event_attack` for 3 days in saves 1, 3 and 5 (the attack, the battle, the boarding and plunder messages), 0 different; the other 12 scenarios of `retouch_pirates.py` (1 day, saves 1, 3, 5) and the 19 player scenarios, as before. `retire` with days still differs: its convoy without a guide goes on with ship 0xFFFF in the game (zeros in the emulator), which p3vita doesn't reproduce; it can only happen when the pirates are switched off in the game's options, which p3vita has not.
- **The battle's question** (the attack message, 0x60659B..0x606AF5) isn't written with the busy path; p3vita composes it for the viewer (`Sailing::attack_message`, in `take_player_messages`, nothing in memory). Checked against the game with the window's path in the oracle for that check only (scratchpad `window_path.py`: 0x60659B not jumped, only the window call 0x606B86 → 0x606BA7): the game writes the text into [+0x1310] and it equals p3vita's in saves 1, 3 and 5 of `event_attack` (a ship alone, one in a convoy, a name with «ö»; `tick_probe --messages` prints p3vita's). The oracle doesn't run the static initializer 0x46BFC0, which fills at start the English exe's table 0x6A1778..0x6A17A0 («Your snaikka»..., «Your convoy», «the convoy»): without it the game's sprintf writes «(null)»; the check filled it as the initializer does. The Spanish exe has that table in its .data.
- **Pointers to blocks of 0 bytes** (2026-11-03): an array without items (a sea battle's [+0x664] and [+0x668], the second the cannon balls', in save 7's saved battle) is a heap block of 0 bytes, and `battle_graph` didn't convert a pointer to it (`lo <= v < hi` with `lo == hi`): it stayed a raw address, which tick_probe skips, so the whole world matched (p3vita has its own pointers there), but a call-by-call comparison (`ret2f.sh`) rebuilt p3vita from the game's graph without those blocks (32 against 34; the feast scenarios of save 7). Now the pointer to a 0-byte block's start is converted too; world 7 days in the 12 saves, the feast scenarios in saves 1, 3, 5, 7, `event_attack` and the pirate attacks, and the writer after 4 days: as before or 0.
- The end of a battle writes its messages into [+0x1310] (0x60E03B..0x60E8CE) only for a human side of the player (a battle directed by hand, part 7); in an automatic battle the report goes to the player as a letter (0x4D6530; see «The mailbox: sending letters» below).

## The mailbox: sending letters (2026-11-03)

The game sends a letter or notice with 0x4D6530 on the mailbox 0x6DD730 from 154 calls ([formats/mail.md](formats/mail.md)). p3vita has it in `src/p3data/mail.cpp` (`send_letter`, with 0x4D6680, 0x4D6B40, 0x51AB20 and 0x51AAB0), and the callers are connected group by group.
- **The oracle runs 0x4D6530 only from the connected calls** (`MAIL_CALLS` in `town_oracle.py`, as call addresses; the hook compares the return address): from the others it is still silenced (`QUIET`), so the world stays comparable while the groups are connected. `--letters all` lets every call through (to explore), `--letters none` silences them all (the recordings before this). `retouch_mail.py` and `TownOracle(letters=...)` take the same choice.
- **The letters' buffers**: a new region `LB` (u16 letter, u32 size and the bytes of each letter with a buffer: types 0x29, 0x47, 0x63, 0x83 at [+0x8], [+0xC] bytes; 0x3C..0x40 and 0x71 at [+0xC], the record's [+0x0] bytes). p3vita keeps them in `SaveGame::mail_buffers` (live state now; before, `SaveOpaque::mail_extras`, only for writing) and tick_probe compares them. The pointers in `B` stay skipped (`pointer_field`); the records stay region `L`.
- **First group**: the merchant's tick (0x4F7BB0, `merchants.cpp`): the weekly accounts of a human merchant (letters 0x0F, 0x10 and 0x11) and the promotion in the home town (0x06, to the chronicle). Checked: the whole world 7 days in the 12 saves, 0 different (the player gets his weekly letters: saves 1 and tut0 checked with the scratchpad's `mailcheck.py`); `retouch_mail.py grow` (the mailbox without free entries: it grows by 200; 8 days) and `promotion` (the player rises to rank 2 on day 1; 2 days) in saves 1, 3 and 5, every call of 0x4F7BB0 equal; the writer after 8 days in saves 1, 3 and 7 (`write-after-days.sh`), 0 bytes different and the reload equal. `tests/mail_test.cpp` keeps the rules (the human filter and the settings, −1, −2, growing, buffers, records, releasing).
- **The sea battles** (2026-11-03): the report of 0x60D299 (letter 0x29 to each side of the computer; the player's sides are the computer's while the battle window is missing) with the game's texts (`GameText::BattleReport*`, `BattleShip*`, slots in both exes; `GameTexts::sprintf_raw` takes `%d`, `%i`, `%u` now), the pirate's win in the chronicle (0x2A) and the letters 0x72; 0x50BFD0 (0x54, 0x2A), the boarding 0x60C1C9 (0x2A, 0x54, the share 0x30), operation 0x91 (0x54, 0x30), the privateer going home 0x514D40 (0x2A) and a pirate's ship sunk 0x61F96F (0x2A). Checked: the whole world 7 days in the 12 saves, 0 different (the reports reach the player in tut4); `retouch_pirates.py` `event_attack`, `attack_one`, `leave` and `daily` in saves 1, 3, 5 and the new `retouch_mail.py privateer` (every pirate ship a privateer of the player, so the shares come to him) in 9 saves, every call equal; the writer after 8 days in saves 6, 7 and tut4, the same bytes (tut4 with the player's report). Not reached, marked «sin comprobar»: the 0x72 to the attacking side (a merchant attacks: the Hanse fleet, the player's battle), the 0x54 and 0x2A of 0x50C274 and the share of operation 0x91.
- **The merchant's tick and the tasks** (2026-11-03): 0x4F7510 (0x5E, 0x5F; `has_rank_letter`, 0x4D7AC0), 0x4F75F0 (0x6D, 0x82), event 0x05 (0x4E5B84: 0x19 or 0x0A, 0x4A, 0x4B) and event 0x16 (0x4E8234: 0x21). Checked: the world 7 days in the 12 saves, 0 different; `retouch_mail.py commission` (34 events 0x05 of the player: the 16 kinds starting, ending and two failed) and `reputation` (saves 1, 3, 5; the letters in 5), and `retouch.py jugador` (the candidacy), every call equal.
- **The small files** (2026-11-03): `facilities.cpp` (0x00, 0x03), `offices.cpp` (0x60), `core.cpp` (0x35), `events.cpp` (0x81), `construction.cpp` (0x0C, 0x2F), `convoys_members.cpp` (0x32, 0x31; one letter for both, as in the game). Checked: the world 7 days in the 12 saves, 0 different; `retouch_mail.py hulk`, `wall_bill` (save 3: the others have no pending wall piece in the player's home town) and `convoy_fees` (direct calls through `retouch_unchecked.direct`: a call from Python with `o.call` returns to the sentinel and the recorder never writes its sample), every call equal.
- **The rare events** (2026-11-03, `events_rare.cpp`, 21 calls): the pirate hunt of event 0x1E (0x41, 0x42, 0x43), the plague (0x58), the council's vote (0x6B), the Hanse leader's task (0x77, 0x78), the extra tax (0x6C), the theft (0x73), the monument (0x7B) and the expedition (0x34, 0x1C, 0x56 and the report 0x63, whose buffer is the text composed by 0x4E6A54 with the CStrings of 0x52BFE0 and 0x52C6C0: the game's texts, new `GameText::Expedition*` and `Contract*` with their slots in both exes; the Spanish build passes the new contract's arguments in another order, `GameTexts::spanish()`). Checked: the world 7 days in the 12 saves, 0 different (the plague in save 3, the Hanse meeting 0x78 in saves 4 and 6); new `retouch_mail.py` scenarios `pirate_hunt`, `council`, `hanse_task`, `hanse_task_ai` (saves 1, 3, 5) and `expedition`, `expedition_lost`, `expedition_four`, `expedition_villages`, `expedition_new_world`, `expedition_nothing`, `expedition_contract` (the tutorials: only they have an expedition under way), and `retouch.py` `robo`, `impuesto`, `monumento`, every call equal. Things learned on the way:
  - a letter only reaches the player if his settings let its class through: the tutorials' player doesn't receive trading information (class 3, his [+0x3A]), and the expedition scenarios set it; an expedition's convoy goes back to its guide's owner in 0x502B20, so they make the ships the player's too;
  - **ZEROED_FRAMES** (`town_oracle.py`): 0x4F5A20 reads a local it never sets (the best rent, a game bug; p3vita takes 0). The oracle's stack had 0 there until the letters ran the real 0x4D6530, deeper in the stack (the extra tax's 0x6C before an AI merchant's debts, save 5 of `impuesto`); now its locals are set to 0 at its entry;
  - tick_probe gives each call's state the town names by number too (`SaveOpaque::town_names`, the table 0x6DDA00), which the report uses;
  - `retouch_mail.py pirate_hunt` first hung the oracle: the state 4 of event 0x1E doesn't move the event's tick, so with the event still «in force» the queue runs it forever in one tick (a possible game bug, in the roadmap); the scenario gives that event past deadlines;
  - the game's letters often hold the function's working state (0x4F6570's impounding letter is its «paid», «sold» and «settled»; 0x500C30's keeps the three wares that burn; the dowry 0x25 is the wedding's 0x23 with the ships' cargo added in its [+0xC], [+0xE]): read every store to the letter's slots, also the indexed ones (`[esp+reg*2+X]`). And mind the slots' order: the letter at Ln has [+k] at L(n−k), so Lf is [+0xD] and Ld [+0xF];
  - the debugging scripts' `fr.py` took the CRT's `_allmul` and `_alldiv` as cdecl and lost 16 bytes of the frame after each (`mail_calls.py`'s `ret_n` knows them now); `match.py` (`re/emu/sesion5/mail/`) finds an English code range in the Spanish exe with its addresses masked and pairs the text slots of both.
- **The towns' events** (2026-11-03, `events_towns.cpp`, 14 calls): the debts' notices (0x4DB4E0: 0x12, 0x26, 0x79), the impounding (0x4F6570: 0x13 four ways), the feast (0x24), the wedding (0x23, 0x25), the town's fire (0x59) and a burnt store (0x75), a new office (0x0D) and a captain's progress (0x4D; operation 0x12 now runs the same `fn_538a80` as 0x4DCEA0). Checked: the world 7 days in the 12 saves, 0 different (the new office in save 4, the impounding with money in saves 6 and 7); new `retouch_mail.py` scenarios `debts_notices`, `debts_office`, `debts_ship`, `debts_partial`, `captain` and `retouch.py` `fiesta`, `incendio_ciudad`, `boda` in saves 1, 3, 5: every call equal.
- **The elections, the Hanse and the auctions** (2026-11-03, `events_mayor.cpp`, 9 calls, and the Alderman's notice 0x5299F0, rewritten whole: 2 more): the mayor's election (0x528E60: 0x47 with its text to the human merchants of the town, 0x55), the Alderman's (0x5299F0: 0x47 to each merchant, 0x57), the Hanse vote's result (0x4DC1F0: 0x0B, to each merchant, not to one as appendix A said), the auctions' end (0x4E2CD4: 0x2C/0x2D, 0x5D, 0x2E, and 0x76/0x7D, 0x7F, 0x7E; 0x76 was missing from the inventory). New GameText entries `MayorElection*` and `Conference*`. Checked: the world 7 days in the 12 saves, 0 different (the mayor's election in saves 7 and tut1, the Alderman's in tut3, its letter in the player's chain); new `retouch_mail.py` scenarios `mayor_election`, `alderman_merchant`, `alderman_citizen`, `hanse_vote`, `auction_end` (saves 1, 3, 5, 7, tut0), every call equal. On the way:
  - tick_probe gives every probe the event queue's Sailing (`tick::set_event_sailing`), so the letters' texts reach the probes that don't set it (0x528E60's);
  - **`tick_probe --names SAVE.raw`**: the players' own names (`SaveGame::custom_names`) are in the save, not in the recording; without `--continuo` each call gets them from that save (`rm.sh`, `rp.sh` and `ret2f.sh` pass it). A letter that names the player needs it;
  - `P3_SHOW_LB=1` makes tick_probe print both sides' letters' buffers as text when the region `LB` differs in size (`re/emu/sesion5/mail/showlb.sh`);
  - becoming Alderman ends the game (the window 0x5499F0): a scenario that makes the player win the election stops the oracle there.
- **The Hanse tasks** (2026-11-03, `alderman.cpp`, 6 calls): a task started (0x4EBF54: 0x80 the road, with the Alderman's names from 0x5333C0, rewritten; 0x6E and 0x84 the pirate nest), expired (0x4EB0EB: 0x70) and done (0x4EC354: 0x6F to the chronicle and to a human merchant). Checked: the world 7 days in the 12 saves, 0 different; `retouch.py` `encargo_nuevo`, `encargo_cumplido`, `encargo_vencido`, `jugador_coge`, `jugador_vencido` and the new `retouch_mail.py hanse_task_letters` (saves 3 and 5), every call equal.
- **The sieges** (2026-11-03, `sieges.cpp`, 4 calls): the news 0x38 to the chronicle when the town pays the ransom (0x6281F0: 0x628A52) or the army leaves (0x629A2B), 0x53 a new army (0x629C60: 0x629F63) and the prince's payment 0x85 (0x636230: 0x6364E9). Checked: the world 7 days in the 12 saves, 0 different (a new army's 0x53 in saves 6 and 7); the new `retouch_mail.py siege_letters` (saves 1, 3, 5: two new armies, the ransom's and the retreat's 0x38, four 0x85, one over 65 535), `retouch.py asedio` (saves 1 and 5) and `retouch_unchecked.py` `siege_ops`, `siege_branches`, `siege_gates` (saves 3 and 5), every call equal; `siege_fixes` as before (only its known difference in the army's overwritten fields, J1). Saves 3 and 5 already had many 0x38 and 0x53 in their chronicle, with the same layout. The 0x6A of 0x633DB0 (0x6345D3) stays quiet: it is in the convoy branch, reached only from the battle in a town (0x5FEAC9), which p3vita doesn't have.
- **The operations and the Hanse's missions** (2026-11-03, `operations.cpp` 4 calls, `diet.cpp` 3): a captain retiring (operation 0x13: 0x52), a wedding arranged (0x60: 0x33), a manager's progress (0x67: 0x7C), a child (0x87: 0x7A), and the missions of 0x4E09D4 (0x48 in phase 0, 0x4C in phase 2, 0x5C when the fleet disbands). Checked: the world 7 days in the 12 saves, 0 different; the new `retouch_mail.py` scenarios `operation_letters` (saves 1, 3, 5: the four operations for the player; a captain's ship out of the towns run at once with its [+0x39] 0xFF) and `diet_letters` (saves 1, 3, 5: the 0x24 ending protected, the 0x23 with a convoy of the player's ships, the 0x34 with the player twice in its list), `retouch.py` `capitan_jugador`, `boda`, `propuestas`, `hijo` and `dieta` (12 days; saves 1, 3, 5) and `retouch_unchecked.py diet_branches` (saves 3, 5), every call equal. On the way, `dieta` showed that 0x4E09D4 takes the first pirates' captain for the 0x48, not the last: the game ends both loops by setting the pirate index (edx) to 0xFFFF. Queuing the same captain's retirement twice hung the oracle later on (not looked into; the game never does it).
- **Saving and loading the new letters** (2026-11-03): `scripts/write-after-days.sh 8` in the 12 saves, 0 bytes different from the game's writer and the game's reload of p3vita's `.pat` and `.pst` equal (the elections' 0x47 with its buffer in tut1 and tut3, the battle reports 0x29 in tut4). For the letters only retouched states reach, the new `scripts/write-scenario.sh SCENARIO DAYS SAVES` (`retouch_mail.py --write OUT`: at the end the game saves the state reached): the recording call by call, then the game (`write_oracle.py`) and p3vita (`p3save write`) load that save and write it again, compared byte for byte, and the game reloads p3vita's file. Not the first save against p3vita's: a load recomputes fields such as the price thresholds (t2, t3 of ware 19 in four towns differed), so a save written from memory after a day isn't what a load and a save give. Checked: `expedition` (tut0..tut4) and `expedition_four` (tut0, tut1, tut3, tut4: the report 0x63 without its final 0), `siege_letters` (save 1), 0 bytes different and the reload equal.
- **Which calls run**: with `P3_MAIL_STATS=1` the oracle prints at the end, to stderr, every call to 0x4D6530 by its address, «(quiet)» when it was silenced (the scratchpad's `stats.sh` runs the 12 saves).

## The letters' texts: the composer, in English and Spanish (2026-11-03)

The letter window composes each letter's text with 0x47F3B0 (a header with
the town and the date, then a case per type of the table 0x48664C;
[formats/mail.md](formats/mail.md) §4). p3vita's is `compose_letter`
(`src/p3data/letters.cpp`), with the game's texts by their address
(`GameTexts::raw_at`, the table `game_text_addresses.inc`). **Every type
with a text is done** (`letter_type_rewritten`) but the offers 0x1E/0x1F
(they need the world's offers list [0x6DE584] and operation 0x5F, a
player's action). Some cases change the save when shown (`letter_viewed`:
the loan dates of 0x12 and 0x26, the women's bit of 0x4F6420).

- **The two letters with a picture** (2026-11-04), 0x46 and 0x6E: their
  text is only «\n\n\c\h%u_» with the number of the texture 0x51AC60 makes
  through the graphics library (what it draws: [formats/mail.md](formats/mail.md)
  §4). The emulator has no graphics library: before, the game's composer
  «failed» on them. `letter_oracle.py` now stubs 0x51AC60 (`PICTURE`, also
  in `SpanishExe`'s `CODE`): it returns p3vita's `kLetterMapPicture` (1) and
  checks that it got the letter's entry; the fake window's [+0xB8] (the last
  picture, which the next one frees through the library) is cleared before
  each letter. Checked: `all_letters` in save 3 (three of each, the same
  bytes in both languages), a real pirate nest's letter
  (`retouch_mail.py hanse_task_letters`, `re/emu/sesion5/mail/s6/nest.sh`),
  and the 12 saves again (24 comparisons equal). The picture itself
  (`letter_map_picture`) is p3vita's reading of 0x51AC60 and the library's
  blitters, without an oracle: a test with made-up images
  (`tests/mail_test.cpp`, values worked out by hand from the code) and the
  same pixels as a separate Python version from the game's files; the viewer
  draws it (`s6/shot.sh`, the scenario's save loaded from a slot).

- Found on the way, all in `extra_texts.txt` (`re/emu/sesion5/mail/s3`):
  the alignment missed tables whose first element isn't a text (0x0A's
  reasons, 0x19's kinds 9..15), took slots loaded by their address for
  strings (0x6B48EC, 0x6C3054), and paired some texts one slot off (0x1B)
  or with another text (0x07's convoy, 0x78's reasons); the Spanish code
  differs in 0x03, 0x0B, 0x12, 0x1A, 0x27, 0x78 (the order of its
  arguments, or code of its own); 0x63ADD0 is strcpy, not strcat.
- **Two static initializers** (0x46BD80, 0x46BFC0; Spanish 0x46BE60,
  0x46C0A0) fill tables of text pointers at start: the monument's and the
  ranks' adjectives (0x6A0C18, 0x6A0C54), the battles' «Your snaikka»...
  The oracle now runs them (`TEXT_INITIALIZERS`, also in `SpanishExe`) and
  `GameTexts` reads their stores (`static_slots`). The world 7 days stays
  0 different.
- **The game's sprintf with %$N$** gives a conversion without `$` the
  argument of its place among all the conversions, the positional ones
  counted (checked in the emulator, `s4/sprintf_test.py`):
  `GameTexts::sprintf_raw` did otherwise (its test said so; it had never
  been checked against the game).

- **`tools/oracle/letter_oracle.py`** runs the game's composer on a fake
  window whose list row points at each letter of the player's chain (the row
  as 0x47C520 makes it: entry, type, [+0x5], unread, the date «DD.MM.YYYY»),
  takes the text when it is complete (0x48654B: buffer in eax, length in
  esi) and jumps to the composer's exit (0x4865E5). `p3save CPR SAVE letters
  EXE` prints p3vita's; `compare_letters.py` compares them by type (equal,
  different, not rewritten, failed in the game: the game's composer reading
  a bad pointer). `scripts/letters-check.sh [SAVES | x.raw]` does it all.
- **The Spanish exe** (`tools/oracle/spanish_exe.py`, `letter_oracle.py
  --spanish`, `P3_EXE_SPA` in `letters-check.sh`): the Spanish composer is
  not the English one with other texts: some cases have code of their own
  (0x03 picks «Nuevo » by the building's gender, a table the English exe
  lacks; 0x0B passes its arguments in another order, for its `%$N$s`). The
  world is still loaded with the English exe; `SpanishExe` makes a second
  emulator with the Spanish exe mapped, copies the English one's heap as it
  is and the game's globals moved to the Spanish layout (`STATE_RUNS`:
  +0x21FA4 from 0x6DD5BC, +0x21FA8 from 0x6DD628, +0x21FA0 from 0x6DE3D4,
  and two words of the initialized data), and hooks the Spanish addresses of
  malloc, free, new, delete, rand and the queue (`CODE`). Both come from an
  alignment of the two exes' whole code that pairs the addresses each
  instruction uses (`re/emu/sesion5/mail/s4/data_map.py`, outside the
  repository); `CODE` was checked byte by byte (the CRT's functions are in
  another order: malloc is +0x410, the rest +0x530). `reads_outside` lists
  what the Spanish code read of the globals not copied before writing it:
  nothing (the CRT's sprintf reads only what it writes). The towns' names
  stay the English archive's in both.
- **`retouch_mail.py all_letters`** (with `--letters all --write OUT`): three
  letters of each type with a text and random small fields, and for the
  types p3vita composes more with the fields a real letter has
  (`real_fields`, from each sender: the reasons 5..8 of a charge, the six
  Hanse votes rejected and carried, buildings, ship types with the convoy,
  captains and ships in range). The small random fields often fall out of a
  table's range (a ship type 770, a captain 514): the game then composes
  whatever its memory holds past the table, which can't be matched, and the
  game's reads past each table differ between the English and the Spanish
  exe.
- Checked: the 12 saves, every letter equal in English and Spanish (none
  left not rewritten); `all_letters` in save 3 with seeds 1..5, every type
  equal in both languages but the out-of-range fields (each checked: a
  reason, town, building, month, ship type, captain or merchant past its
  table, where the game composes «(null)» or whatever follows the table),
  the English blockade rejected (a game bug: the game prints the address of
  the town's name where p3vita writes «?») and 0x42's null slot (a game
  bug: the emulator copies its page 0, Windows would crash).
- On the way: 0x0A's reason table (0x6B4A0C) has its texts at 5..7 (0..4
  fall on other data), which the alignment missed (it counts a table's
  texts from its first element); the ship names past both lists (0x512B20)
  give a type's word («cog», «nao»), not nothing; the Spanish table of
  0x02's types has no convoy (roadmap, «Corregir los fallos del juego»).
- **`lstrlenA` as Windows has it** (`TownOracle._lstrlen`): the game's
  sprintf (0x64D410, its own, through the static buffer 0x6D84E0) makes a
  first pass that measures each conversion's argument in the order the
  conversions come, not by their `%$N$`, so a Spanish text with «%$2$s ...
  %$1$i» measures the number as a string. Windows' lstrlenA returns 0 for a
  pointer it can't read (and nothing is mapped below 0x10000); the oracle
  read the memory and stopped (numbers from 4096 up; below, the emulator's
  page 0). Now it returns 0 as Windows: the Spanish loan letters (0x12) that
  «failed in the game» compose, and the world 7 days stays 0 different.
- **The viewer's texts' cache** keeps the texts by address too (version 2);
  `p3save texts-cache EXE CACHE` writes it and checks that it reads back the
  same (both exes: 805 texts by address).

## The mailbox window: reading, deleting and the chronicle (2026-11-03)

The viewer's mailbox is the game's letter window (0x47C520 fills its list,
0x47D570 draws it; [formats/mail.md](formats/mail.md) §4). What it changes
in the game goes through the game's own code, checked here:

- **Operations 0x6E and 0x6F** (0x536AC7 → 0x4D69D0, a letter read: clears
  the month's 0x80 if the entry is in the merchant's chain; 0x536AE1 →
  0x4D6A50, a letter deleted: out of the merchant's chain, or of the
  chronicle for a negative merchant, to the head of the free list, released):
  `mark_letter_read` and `delete_letter` in `mail.cpp`, in `run_operation`
  and in `OPERATIONS`. Scenario `retouch_mail.py read_delete` (16 operations:
  the player's head letter, one in the middle, one with a buffer; the
  chronicle with −2 and −1; another merchant's, past the mailbox, a negative
  merchant for 0x6E): every call of 0x535760 equal in saves 1, 3, 5, 7 and
  tut4. The world 7 days in the 12 saves stays 0 different (nothing in the
  simulation queues them).
- **The chronicle** (0x47DD40; Spanish 0x47DDE0, the same code 0xA0 further
  on): `tools/oracle/chronicle_oracle.py` runs it on the loaded world, takes
  the buffer when it is complete (0x47E57B) and jumps to its end (0x47E599),
  past the text window (0x41D9B0 on *0x6CBB10); `--spanish` through
  `SpanishExe` (its `CODE` has the three addresses). It also prints the
  merchants whose [+0x1C] the composition changed (0x4F6420). p3vita's is
  `compose_chronicle` and `chronicle_viewed` (`letters.cpp`), `p3save CPR SAVE
  chronicle EXE`; `compare_chronicle.py` compares news by news and
  `scripts/chronicle-check.sh` does it all (`re/emu/sesion5/mail/s5/cc.sh`
  with this machine's paths). The texts' Spanish slots came from an alignment
  of the two functions (`s5/chron_texts.py`, `letter_texts.py` for a switch
  through an index table), into `game_text_addresses.inc`.
- Checked: the 12 saves, the same bytes in English and in Spanish (1,140
  news; the merchants' [+0x1C] unchanged in both, a load already leaves them
  as 0x4F6420 does). The cases and branches the saves don't reach, with the
  new `retouch_mail.py all_news` (`--letters all --write`, then
  chronicle-check on the written save; `s5/news.sh SAVE SEEDS`): the 21 cases
  with a man and a woman, a merchant past the merchants (the cases that then
  append the local buffer as it was), the pirates' four results, the siege's
  three ends (a sum over 2³¹ printed as a negative %i), the attacks beaten
  off and the lairs, the Hanse tasks' five kinds and one past them, and small
  random fields of each case: saves 1 and 3, seeds 1..5, the same bytes in
  both languages. Random fields out of range (a town or a title past its
  table, the 0x57's sex above 1) make the game read garbage and stop: the
  scenario keeps them small.
- On the way: the Spanish code differs in three cases: the promotion 0x06
  passes the town before the title (its text has %$6$s, %$5$s), the siege's
  0x38 passes the days before the town, and the new country 0x56 takes the
  capitalized articles (0x6C1418) where the English takes the lowercase ones
  (0x6C0ED8). Two game bugs (the 0x6F's adjective and the pirate's family
  name), in the roadmap.

## The ticker lines (2026-11-04)

The game's other messages to the player, the ticker lines of 0x548CA0
([formats/mail.md](formats/mail.md) §6), which compose their text in
0x5483D0(kind, e). Two checks:

- **The text**: `tools/oracle/ticker_oracle.py` calls 0x5483D0 on the
  real-time object for every kind 0..0x2A with arguments from the loaded
  world (the player's ships and others in port and at sea, one past the
  ships, towns and one past them, small numbers, a land transport's two
  towns), takes the CString it hands to the interface (0x5487BA) and skips
  the interface (to 0x5487C5); `--spanish` through `SpanishExe` (0x5488A0,
  +0x4D0). p3vita's is `compose_ticker` (`letters.cpp`), `p3save ... ticker
  EXE PAIRS`; `compare_ticker.py` and `scripts/ticker-check.sh`
  (`re/emu/sesion5/mail/s5/tc.sh`). The 12 saves, every pair the same in
  English and in Spanish (348 to 396 pairs each, 9 to 12 failing in the game:
  a town past the names' table). The texts' Spanish slots from the
  alignment of the two functions (`s5/chron_texts.py` with CASES=19).
- **The calls**: the oracle still silences 0x548CA0 but records the calls
  for the local player whose kind has a line as the region `TK` (u8 kind and
  u16 argument each, since the start; the argument 0 for a fixed text), and
  tick_probe compares it with `SaveGame::player_ticker` in every call. The
  world 7 days in the 12 saves, 0 different (a line in tut4); the scenarios
  `retouch_mail.py` `commission` (32 lines: 0x1B, 0x1C), `hanse_task` (0x21),
  `debts_office`, `debts_partial` (0x15), the new `ticker_lines` (the player
  of rank 6 in every town, then a Hanse vote, the Alderman's election, a
  council and the diet's blockade at the next day's start: 0xD, 0x20, 0x2A),
  `ticker_ships` (his ships sail to the next town: 5, and 8 in tut4) and
  `ticker_shipyard` (direct calls: 6, 7, 0xA), `expedition` and
  `expedition_lost` in the tutorials (4, 0x1D), and `retouch_pirates.py
  event_attack` (0xB), every call equal (`s5/tk.sh`, `s5/tkshow.sh` with
  `s5/probe_tk.py`, which prints a recording's last `TK`). `PLAYER_RANK=N`
  gives the player that rank in every town before a `retouch_mail.py`
  scenario. Not reached by a scenario: the end of the game (0xC), the famine
  (0x19), a convoy of his (3, 0x22), the auction's announcement; their code
  is a line each, next to the letters that are checked.
- **A ship of the player sunk** (0x506CF2): 0x519D90, which p3vita skipped,
  sends a human owner the letter 0x1B (rewritten in `Sailing::sink`, its call
  0x519E87 in `MAIL_CALLS`), then the ticker line 0x16. New scenario
  `retouch_mail.py ship_sunk` (two of his ships sail, at sea their hull left
  at 4, the second without its captain): saves 1 and 3, every call equal
  (the letters 0x1B and the lines 0x16; one ship sinks with its captain, two
  without), the letters' texts equal in English and Spanish (the scenario's
  save, `--write`, through `letters-check.sh`; `s5/sunk.sh`,
  `s5/sunk_text.sh`), and `write-scenario.sh ship_sunk 1` in saves 1 and 3:
  0 bytes different, the reload equal. The world 7 days in the 12 saves, 0
  different.

## Part C of the inventory (2026-11-04)

The calls from rewritten code to functions p3vita didn't name
([inventario.md](inventario.md), part C), reviewed one by one. Two gaps,
each checked with a new scenario of `retouch_unchecked.py` (saves 3 and 5,
`tools/oracle/unchecked.sh`):

- **`siege_pitch`**: a siege made with the game's code (operation 0x8E)
  retouched so that its army has arrived ([+0x42] = 0), has ammunition and
  attacks a gate 0x34, against a town with defenders of types 1..3; one day,
  recording 0x535760, 0x528070 and 0x6281F0. With p3vita as it was, one call
  to 0x528070 differed in each save, in pitch's thresholds ([+0x5E0..+0x5EC]:
  the game had 7 · 640 more in t0 in save 3, 640 = 6 defenders · 40 + 400 of
  the gate); with 0x636920 the 24 calls are equal, and the 512 and 768 calls to
  0x6281F0 too. The targets `@5281b1`, `@63692e`, `@6326cc`, `@63276a` are
  hit. `siege_branches` had checked 0x6326E0 only with direct calls, without
  the town tick after them.
- **`office_ops`**: in the town of an AI merchant's office, operation 0x5D
  for another AI merchant without an office there and with a ship docked
  there carrying some bricks and timber (0x4F5450 takes them all), 0x5D again
  (he has one: nothing), 0x5D with the merchant and with the town out of the
  world; 0x21 with the type 0x2C for a third merchant, first with the town's
  market empty (0x51F3A0 refuses), then with it full; 0x21 with the types
  0x57 (→ 5, the fisherman, on a site cell), 0x29 (→ 0x33), 0x2F (→ 0x35)
  and 0x70 (not in 0x672FC0). The 10 operations equal; the 9 targets hit.

0x53CE30 took a `request` argument for 0x21; `build_ops`, `siege_ops`,
`siege_gates` and `siege_branches` were repeated (equal), and the world 7
days in the 12 saves: 0 different.

The third gap, a player's action, came right after (the operation 0x4E, a
player's ship turns pirate, with the branch of 0x509250 for a winner that
isn't a merchant and 0x4F5900):

- **`player_pirate`**: the player's first ship in port left without its
  captain, who goes to the head of the town's list of captains ([0x82E]; if
  the ship had none, one already in that list or the head of another town's).
  The operation 0x4E with a captain not in the list (0x526AA0 refuses), with
  him (the ship joins the nearest pirate), and again (the ship is no longer a
  merchant's: refused); then a day recording 0x535760 and 0x506720. Saves 1,
  3, 5 and 7: every call equal, the 7 targets hit. With two details broken on
  purpose (the days of [+0x4C], the waypoint) the call differed in [+0x48],
  [+0x4C] and the route (region Q), so the comparison sees them.
- **`player_pirate_new`**: the same with the five pirate pointers at 0: the
  game makes a pirate in slot 0 (0x513720, 0x513D80 with its nest and
  ships). Saves 1, 3 and 5: equal.

0x509250 now takes the `Sailing` (the auction passes the event queue's,
the probe `probe_sailing()`); `retouch_mail.py auction_end` (the ship
auctions) and `retouch.py desguace` (0x507FB0, now with 0x4F5900) in saves 1,
3, 5 and 7 equal; the world 7 days in the 12 saves, 0 different.

The merchants' ship lists are empty in every save, so for 0x4F5900 the oracle
records a new region `ML` (each list not empty: u16 merchant, u8 list, u16
length and its u16s, read through the merchant's [+0x624 + 4·k]), which
tick_probe loads into `SaveOpaque::merchant_lists` before each call and
compares after it. Scenario **`player_pirate_lists`**: as `player_pirate`,
with three of the player's lists filled (the ship; an AI ship, the ship and
another of his, with the bit 0x8000; the ship twice), 0x4E and then 0x0A on
his other ship (0x507FB0 → 0x4F5900). Saves 3, 5 and 7: equal, both paths of
0x4F5900 hit; with its condition inverted on purpose both calls differed in
M and in ML.

The save after these states, with `scripts/write-scenario.sh` (new:
`RETOUCH=unchecked` runs a scenario of `retouch_unchecked.py`, which got
`--write`): `player_pirate` (1 day; saves 1, 3, 5, 7), `player_pirate_new`
(1, 3, 5), `player_pirate_lists` (3, 5, 7), `office_ops` (3, 5) and
`siege_pitch` (1 day; 3, 5). In every one the game and p3data load the save
the game wrote and write it again with the same bytes, and the game's reload
of p3data's file has 0 differences. (After `player_pirate_lists` the lists
are left empty but for the bit 0x8000; lists with ships in a save were
already checked by `write_retouch.cpp`, merchant 1.)

Then the last operation of part B, **0x9F** (0x536CD5 → 0x611309: ships join
a sea battle and its music level [+0x1E94] goes down once per bit of their
number; queued only by the chase 0x50B250, not rewritten yet). Scenario
**`battle_join`** on the sea battle saves 6, 7 and tut4 start with: 1, 0x2D,
0x7FFF and 0 ships, a slot without a battle, slot 0xFF and the battle with
[+0xAD2] = 3. The 7 operations equal in the three saves (region BT), the 5
targets hit; with the factor 950 changed to 951 on purpose, two calls
differed in [+0x1E94]. The save after it (`write-scenario.sh`,
`RETOUCH=unchecked`): the same bytes, the reload equal; the world 7 days in
the 12 saves, 0 different. When the chase is rewritten, 0x9F goes into the
oracle's `OPERATIONS`.

## Part D of the inventory (2026-11-04)

The marks «Por reescribir» of the code, reviewed. One gap: 0x633AE0 (is a
siege going on?) said «no» whenever the town's siege object had an army; now
0x629C30 on the army, as the game. Scenario **`siege_sale`** (saves 3 and 5):
a siege made with the game's code and direct calls to 0x4DAF10 (a building's
sale price) with the army besieging, without besiegers of type 0, retreating
before and after [+0x6A], with the flag (60 %) and in a town without a
siege: the 6 calls equal (the price halved while it besieges; p3vita gave
the whole price before). `siege_branches`, `siege_ops`, `siege_gates`,
`siege_pitch`, `retouch.py subasta`, `subasta_ajuste` and `asedio` equal;
the world 7 days in the 12 saves, 0 different.

## Part D: the unchecked branches of operations.cpp (2026-11-04)

The last block of part D, the branches marked «Sin comprobar con el
oráculo», file by file (`ENCARGO_SIN_COMPROBAR.md`). `operations.cpp` had
30 (one more than the count, written «sin comprobar» in lowercase): all
checked with four new scenarios of `retouch_unchecked.py`, operations run
with the game's executor while recording 0x535760, each branch a target
(`@A>B` for the early returns). Only comments changed in the simulation.

| Scenario | What it runs | Saves | Result |
|-|-|-|-|
| `operation_guards` | 0x7B with a town out, the slot 6 and the slot 4 (the jump for 3..5); 0x7A with each town out and a town whose [0x83F] is a merchant; 0x37, 0x87, 0x61 and 0x81 with a merchant out; 0x61 with a price over the money; 0x2C with a town out; 0x38 less than 40 days ahead; 0x7C on a past day; 0x7F and 0x60 with a tick not to come; 0x80 with a queue of 0x80 records (save 5's own; the others lowered for the call); 0xB8 of kinds 5 and 0 (the pirates' ships); 0x78 with the 16 entries of [0x6DD7C0] in use; 0x7E with 18 towns without the bit 0x40 | 1, 3, 5, 7 | 21 calls equal in each, 20 of 20 targets |
| `operation_convoys` | 0x1E and 0x1F with a convoy out of the table, its leading ship out, and in port with a leading ship that can't sail ([+0x3F] = 0); 0x1E asking for more than the owner has (1000) and with the owner in debt (−500: nothing) | 1, 3, 5, 7 | 9 calls equal, 7 of 7 |
| `operation_loans` | 0x29 with a town out, an AI merchant lending without the amount, and lending (0x4F8FE0: his reputation in the town) with the tavern's slots 1 and 4 and the kinds 2 and 9 (→ 4) | 1, 3, 5, 7 | 4 calls equal, 6 of 6 |
| `operation_sale` | 0x28 with a town and a merchant out, a merchant without an office in the town, the bit 0x10 of [0x2C8] with the building's cell without the bit 0x80 and with it (the auction), and a cell with no building (0x4FF2D0) | 3, 5, 7 (in save 1 no AI office has a building in its lists) | 6 calls equal, 7 of 7 |

With four details broken on purpose (0x7E not writing past [+0x17], the
0x1E taking 1 from an owner in debt, the 0x29 kind 9 taken as 3, the 0x28
cell test the other way round) the comparison caught each one (the next
event record, the owner's money and the convoy's [+0x20], the lender's
reputation [+0x120 + 12·town], the two calls with the bit 0x10). The old
`retouch.py` scenarios of these operations (`eventos_op`, `temporada`,
`red`, `subasta`, `fiesta`, `boda`, `hijo`, `propuestas`) again in saves 3
and 5: equal.

Found on the way:
- **0x7E doesn't look how many towns fit** (0x53F91E..0x53F941; certain,
  seen in the oracle): the towns without the bit 0x40 of [0x2C8] go to
  [+0x8 + k] of the new event 0x1F with no limit, so from the 17th on they
  overwrite the next record of the queue (its tick), or go past the
  queue's heap block if the record is the last one (save 5: record 0x7F of
  0x80; p3vita can't write there). It doesn't happen in a game: 12 of the
  24 towns lack the bit and at most 2 more can be founded.
- **0x28 doesn't look whether the cell is in the grid** (0x53D213, only in
  a town with the bit 0x10 of [0x2C8]): the AI always sends its own
  building's cell, so only the interface or a network game could read past
  the grid. p3vita doesn't read there.
- The marks undercount: 15 more «sin comprobar con el oráculo» are written
  in lowercase inside other sentences (most where the game reads past a
  table), in the files still to do and in `merchants.cpp`.

## Part D: the unchecked branches of events.cpp and events_towns.cpp (2026-11-04)

`events.cpp` had 43 marks: 36 checked with 5 new scenarios, 7 unreachable
(explained in the code). `events_towns.cpp` had 45 (and its header, which
described the convention): 40 checked with 7 new scenarios, 5 unreachable.
Only comments changed in the simulation. New for the oracle:
`probes_unchecked.cpp` repeats 0x4DAFE0 (its price and tick in scratch
globals), 0x4DAB00 and 0x50E000 (the type in a scratch global);
`unchecked.sh` passes tick_probe the save's players' names (`--names`, as
`ret2f.sh` does), which the first names written by the players need. With
that, every older scenario of `retouch_unchecked.py` again in its saves:
the same as before (the two known exceptions: `housing`'s target never
reached, `siege_fixes`'s call different on purpose); the world 7 days in the
12 saves, 0 different.

| Scenario | What it runs | Saves | Result |
|-|-|-|-|
| `event_queue_branches` | the queue called directly with an event 0x37 (a merchant may trade again in a town; made by 0x4E62F6) and an event 0x04 of a merchant out of the world; the grain event 0x00 with [0x6DF31C] and in a network game (operation 0x83); the queue without a free record (0x4D8CF0 reserves 0x80 more: the new ones zeroed, as the oracle's heap) | 1, 3, 5, 7 | 4 calls equal, 5 of 5 targets |
| `ai_debt_branches` | 0x4F42B0 in a network game; 0x4F9580 with a town out and a reputation over 32758; 0x4F4A20, 0x4F4B40 and 0x4F4E50 with [0x6DE52A]; 0x4F4A20 for a merchant with [+0x8] & 4; 0x4F4E50 under −50000 with houses (0x4F5A20 looks at them), with nothing on sale (bankrupt, operation 0x65) and with a building in an auction (0x4DB220: the loan) | 3, 5, 7 | 9 calls equal, 11 of 11 |
| `sale_branches` | 0x4DAF10, 0x4DAFE0 and 0x4DAB00 with a town and a record out; operation 0x28 twice on a building (on sale already: the phase-1 wait), on a second one with the settings' [+0xD] at 0 (added to the auction), on a town with an auction in phase 2 (added), on a building whose owner lacks the town's bit; 0x4DAB00 on a record of no merchant | 3, 5, 7 | 20 calls equal, 12 of 12 |
| `ice_storm_branches` | 0x4E45C4 on day 0x3A, on day 0x30 with an ice of 1 (melts below 0) and on day 0x10 with the speeds 3 and 4 and a level going up; 0x4E43E4 with a ship of [0x6DD7C0] where its storm would go (found first with the same rand state, not recorded) | 1, 3, 5, 7 | 5 calls equal, 4 of 4 |
| `jobs_captains` | 0x4E8E24 with 70 people, with [0x6DF31C], with [0x6DE52A], with no more merchants than towns (the count lowered); 0x4E2634 with every tavern but one empty (a new captain, 0x526A50 with 1: 0x4FDF50 not young) and [+0xC] at 0x1A | 1, 3, 5, 7 | 5 calls equal, 7 of 7 |
| `debt_mail_branches` | a debt paid with a ship's cargo past 65000; a debt of a debtor out of the world; 0x4D6780 for a merchant out and with an old letter of type 0x90 in the player's chain (cut; the free list lost); operation 0x12 for a ship out and one without a captain | 1, 3, 5, 7 | 6 calls equal, 6 of 6 |
| `fire_branches` | four fires: the nearest building's owner a merchant without an office there; its owner 0xFF (not in the town's list); the player's store (0x500C30); a burning list with a tower 0x32 and a building with [+0x6] 0x40 | 3, 5, 7 (in 1 the player has no store) | the queue's call equal, 5 of 5 |
| `wedding_feast_branches` | weddings of a merchant out, of one whose [+0x28] passed, of one whose [+0x13] is over his fleet, and of the biggest fleet in a town with [0x2C8] & 1 in a network game (a crayer, [+0x138] = 0x15); the player's feasts in a town without his office and with his reputation −100 and 100 inhabitants; 0x50E000 with a strength (the interface's and the new game's); 0x531470 on a town out and, through 0x5316E0, with an office of no merchant | 1, 3, 5, 7 | 6 calls equal, 11 of 11 |
| `event03_branches` | 0x4DE790 with [0x6DF31C]; 0x4DCEA0 with the player's administrators at 0xF0 (+0x2B overflows; the rand state and the phase chosen); 0x4DDA40 with [0x6DF31C], no more merchants than towns and [0x6DE52A]; 0x4DCBF0 with every town on fire; an office under the number of merchants freed and opened again (operation 0x5D: no news 0x0D) | 1, 3, 5, 7 | 11 calls equal (0x4DCEA0 and 0x4DCBF0 also inside 0x4DDA40), 7 of 7 |
| `child_names` | 0x4F69B0 with 2 and 3 children and the first name the player's own, the players' index 85 and past them (110); with a sister whose name the first rand() gives (> 0x7F: a woman's) | 1, 3, 5, 7 | 7 calls equal, 3 of 3 |
| `yard_branches` | operation 0x2C with the yard's level below 0, a cog in a town with [0x2C8] & 1, the shipyard's bit off, no money without force, and the player's order | 1, 3, 5, 7 | 5 calls equal, 5 of 5 |
| `captain_retire_branches` | events 0x27 with the ship in the states 0, 4 leading a convoy, 5, 7 (its captain already free: 0x5098B0), 12, 8, 9, 10 and 0x15; a captain out; a ship whose captain isn't the event's (found in the player's first ship) | 1, 3, 5, 7 | the queue's call equal, 10 of 10 |

With details broken on purpose the comparison caught each one: the new
event records' links, the event 0x37's bit, the phase-1 wait, the ice's
speed table, the event 0x08's 3, the 65000 cap, the letters' chain, the
tower, the guests, the captain already free, the first names' lengths and
the player's ship's name. Two didn't change anything and couldn't: a fleet
of 0 or 1 gives the same dowry.

Unreachable (explained in the code): in `events.cpp`, an auction's search
past day 0x190 (nothing blocks a day past 0x180), the best building of
0x4F5A20 with the setting off (the uninitialized rent is 0, as the oracle's
zeroed frame), the person's kind over 5 in 0x4E8CA4, the room over
0x78 − people in 0x4E8E24, a new captain's [+0xA] over what is left, and an
order's price under 1 (63 at least); in `events_towns.cpp`, a letter setting
past 9, a 0x71 without its record, a feast's share over 0x63, a dowry ship
without room and the event 0x03 without merchants. Found on the way: the
interface's operation 0x3C (0x535ECA, putting a building on sale) calls
0x4DAB00 with no check of its own; p3vita doesn't run it yet (a player's
action, phase 5). And a limit of the targets: an edge `@A>B` can't be seen
when B is the instruction right after A (the jump table of 0x4DDC00 at
0x4DDCF5); then an `@B` target does.

## Part D: the unchecked branches of events_mayor.cpp (2026-11-04)

31 marks: 28 checked with 4 new scenarios, 3 not reproduced or unreachable
(explained in the code). Only comments changed in the simulation.

| Scenario | What it runs | Saves | Result |
|-|-|-|-|
| `alderman_hanse_votes` | the Alderman's election (0x4DBBD0, phase 0, the event at the head) with every Hanse town's mayor a merchant but one's and two merchants with [+0x37] = 4: that town of 3000 (the least half the most − 1); then three citizens' towns of 17, 20 and 21 (the most 0x40, the least 0x10); the Hanse's vote (0x4DC1F0) with the player's rank 6 (a weight of 2) and in phase 5 with [+0x15] = 2 (retouched: phase 0 leaves it at 0) | 1, 3, 5, 7 | 4 calls equal, 5 of 5 targets |
| `ship_making_branches` | 0x507D60 in the modes 0, 1, 3 and 4 and with no free slot (0x40 more, zeroed as the oracle's heap); 0x52A8D0 with a level below 0 and an extra size past the most; 0x4F4400 with [+0x470] at 0; 0x509250 selling a big ship to a merchant whose home has [0x2C8] & 1 | 1, 3, 5, 7 | 9 calls equal, 9 of 9 |
| `auction_phases` | the queue with auctions at the tick: phase 0 with a town out (retouched: the game makes them with a town); phase 2 with only [+0xC] (a ship, a building) and with nothing; phase 7 with a town out (retouched), a ship without owner sold to the player (the seller's name from the tick in his letter 0x5D), unsold, and a building's auction going on with its second building; phase 9 | 3, 5, 7 (in 1 no AI office has two buildings) | the queue's call equal, 9 of 9 |
| `building_sale_branches` | 0x4F4550 (an AI bid) for houses of classes 1 and 2 with one of the class in the works list, for a fisherman (category 2) with [0x2CC] at 2000, with the town's offices and without; 0x5256F0 for a building whose owner has no office there, the third of a list (the walk takes more than a step) and to a buyer without an office (0x5308A0 makes one) | 3, 5, 7 | 7 calls equal, 6 of 6 |

With details broken on purpose the comparison caught them: the clamp
0x10 of the least (with the towns of 17, 20, 21: their shares of the
votes), the rank 6's weight, the new ships' state after the table grows,
the mode 3, the ship's size cap, the bid's factor, the sold ship's base
and the seller's name. Not every detail shows in the state: with a single
town among the candidates its weight is the whole one whatever the least
is, and the shares are floors of about 38 votes, so the clamps of the most
and of the least only show in states like the one chosen.

Not reproduced: the eleventh of a merchant's ship lists (0x4F5900; it isn't
saved and nothing in the simulation fills one), and a ship sold with a trade
route (0x509250 → 0x4D4DD0 on the routes object, which no save has: phase
6). Unreachable: a building's buyer letter with an owner who isn't a
merchant (0x4DAB00 only puts merchants' buildings on sale).

## Part D: the unchecked branches of events_rare.cpp (2026-11-04)

66 marks (and one more written over two lines in 0x4E4A44), in two parts:
the events 0x0E, 0x1C, 0x1E, 0x1F, 0x25, 0x26, 0x30, 0x31, 0x38 and 0x4DB100
first, then the council 0x2E, the Hanse's tasks (0x532C50, 0x4EAFE4) and the
expeditions (0x4E6A54). 62 checked with 5 new scenarios, 3 unreachable and 1
where the game fails (explained in the code). One branch of p3vita was
wrong and is fixed (below); the rest, only comments. New for the oracle:
`probes_unchecked.cpp` repeats 0x4DB100 (a building out of the auctions),
and the scenarios' block hooks follow 0x4E9A94 (for an edge whose second
block is the next instruction).

| Scenario | What it runs | Saves | Result |
|-|-|-|-|
| `game_end_branches` | the end of the game (0x4E4A44, called on one event record rewritten each time, with the game's start [0x6DE4AC] chosen for the branch): the goal 1 (rank 4) with the limit passed, the player at rank 4, an AI merchant at rank 4 with the settings' [+0xD] at 0 (not counted: a month after the tick of the event record of index the merchants' number), clamped to the limit, with 20 merchants and without a limit; the goal 2 (a mayor) and 3 (the Hanse's head) the same way, with an AI mayor or head not counted, no event 2 in the queue and the first event 0x20 of the array; the goals 4 and 5 before and at the limit; the goal 6 under and over the people (town 0 gets 70000 more: the saves have under 70000); a goal 7 | 1, 3, 5, 7 | 25 calls equal, 32 of 32 targets |
| `siege_fire_branches` | the queue called directly with the siege's fires 0x25 (a wall piece in works, a cell with no piece, burnt out with an empty burning list and with one without it) and 0x26 (a town out, the types 0x28, 0x32 and 0x33, a merchant's building without his office in the town and with it but not in its lists, one in works, one of type 0x29 with a merchant's owner, one of no merchant out of [0x764], a town with [0x2C8] & 0x8000, burnt out and not in the burning list), the tax 0x31 of a town out and the monuments 0x38 (a town out, a merchant out, another 0x38 of the mayor later in the queue, the town's first 0x2D a merchant's); 0x4DB100 with a town out, a record out, an auction with another lot (kept) and alone (removed); the plague 0x4E9094 of a town out and with the tick & 0x7F the next office's (the administrator leaves, 0x5098B0; the first free captain given to the office if it had none) | 1, 3, 5, 7 | 12 calls equal (0x4DB100 also inside the fires), 27 of 27 |
| `council_task_branches` | 0x4E9A94 (event 0x2E at the head; the world's tick with its low byte 0 shares the votes, 0xFF with the event's counts them): a town out; the extra tax recent, with the share between 10 and 90, over 90 and under 10; the wall with [+0x15], with works pending, and without them and with [0x78C] = [0x78E] (0x4FAC70); [0x9A0] with [0x2C8] & 0x10 and the weight between, under 100 and over 0x398; the tax per head under and over the town's rate; a proposal 4; passed: the extra tax (an event 0x31), the wall's side 2 (the point 1 out of the grid: 0x522B20), [0x9A0] under 0x8000 and at it, the tax per head (0x52A150). 0x532C50 (on a new event) past the queue, of another kind, with the merchant the count; the kind 1 with a town out and with [0x76C] & 0x8000; the kind 2 with a ship out, not in state 0x12, without a captain, with the captain's names and another first or family name; the kind 3 with the object 5 and a missing one; the kind 4 with a town out; a kind 5. 0x4EAFE4 (the event 0x32 at the head, no merchant, [+0xE] 0x3F): two AI merchants with [+0x37] 2 (the lowest gets it), and none | 1, 3, 5, 7 | 37 calls equal (0x532C50 also inside 0x4EAFE4), 36 of 36 |
| `expedition_branches` | 0x4E6A54 (event 0x12) on a new event with a convoy out of the table, and the save's expedition arriving as the player's (the report 0x63 reaches him) without a guide (0x512B20 with 0xFFFF) | 7, tut0, tut1, tut3, tut4 (the saves with an expedition under way) | 2 calls equal, 2 of 2 |
| `pirate_event_branches` | 0x4DFD44 (event 0x1E): the state 0 over without and with [0x2C8] & 0x600 (the state 4), the state 1 going on and over with a mood of −20 (the town's [0x2C6] back), the state 2 over and a state 5; 0x4E0334 (event 0x1F, its first slot the town): before March, the kinds 4 (six low moods), 2 (a pirate object of 600) and 1 (a business over 950 with 200 workers) with the other kinds' conditions emptied, and the operation 0x84 with every Hanse town's mayor the player but the last one's, an AI merchant; 0x4EA594 (event 0x30 at the head) without a task tick, and of kind 5 with an AI head | 3, 5, 7 (save 1 has no world records: the kind 1 needs one) | 12 calls equal, 15 of 15 |

With details broken on purpose the comparison caught each one: the goal
1's clamp, the goal 2 without the settings' test, the life of a wall piece
in works, the empty auction not removed, the administrator kept, a merchant
without an office going on, the bit 0x8000 skipped, the later 0x38 not
seen (its town has a monument, so the event would have gone on), the
town's [0x6F0] and [0x2C6] of event 0x1E, the kind 2's pirate in the
operation 0x84 (the queued operations are compared), the kind 5 of
event 0x30, the recent extra tax, the wall's 9/10 with [+0x15], the double
people with the bit 0x10, the passed tax per head and [0x9A0] at 0x8000, the
family name in 0x532C50, the task's new tick in 0x4EAFE4, the name of an
expedition without a guide in the report (the letter's text is compared)
and a convoy out going on. The older scenarios through the file again
(`re/emu/sesion8/old_rare.sh`: `retouch.py` `impuesto`, `monumento`,
`incendio` and `retouch_mail.py` `pirate_hunt`, `council`, `hanse_task`,
`hanse_task_ai` in saves 3 and 5, the seven `expedition*` in tut0 and
tut1): equal.

Found on the way:
- **p3vita had the operation 0x84's mayor the other way round** (0x4E0834,
  fixed): the pirates' season looks, from a random town, for the first one
  with [0x2C8] & 0x40 whose mayor isn't human, a citizen or an AI merchant
  (its names go in the operation); p3vita took the first human mayor and
  skipped the AI ones. In the saves those mayors are citizens, so the world
  didn't show it. With every such town a human's the game would loop
  forever, which can't happen in a game (a merchant can only be the mayor
  of his home town, 0x528CF0). The scenario's first version did that and
  hung the oracle (a loop inside Unicorn: `re/emu/sesion8/one.sh` runs one
  save with `timeout -s KILL`).
- 0x4E4A44 without a limit (the index 14, 15 or past 18) reads an
  uninitialized local ([esp+0x10]); the scenario zeroes the function's
  frame at its entry (as `ZEROED_FRAMES` does for 0x4F5A20), which is
  p3vita's 0. With the goal 3 and no event 0x20 in the array the game would
  read the address 0: the Gobernador's election never leaves the queue.
- Unreachable: an office without a merchant in a town's list (event 0x31: a
  freed office leaves it; the game doesn't check and would write past the
  merchants), and a fire 0x26 on a record past the count (the siege's
  groups are the town's buildings; the game would read the address 0).
- The game's end window (0x469380, 0x469AF0 and the ranking 0x4220B0 on
  *0x6CBB30) runs in the oracle without trouble; it only writes the
  interface's objects.
- An expedition's convoy without a guide when it arrives: the report takes
  the guide's name from 0x512B20 with 0xFFFF, which gives the text of
  [0x6C0CB4] (p3vita's `GameText::ShipUnnamed`); in a game a convoy always
  has one. Unreachable: a sunk ship of an expedition convoy without a
  merchant (0x502B20 reads through a null pointer; the convoys 0x501680
  makes have no expedition route). The game fails: a sale of less than a
  barrel or a load divides by 0 (`idiv` at 0x52C5B6; in the roadmap's
  bugs), which p3vita skips.

## Part D: the unchecked branches of the mail, events_big.cpp, merchants.cpp and the ships' trade and AI (2026-11-04)

17 marks: 11 checked with 4 new scenarios, 5 unreachable and 1 where the
game fails (explained in the code). Only comments changed in the
simulation. New probes in `probes_unchecked.cpp`: 0x4D6530 (sending a
letter without a buffer; the letter in scratch globals, and the date the
game writes back into it) and 0x500F50 (where to start looking for a site).
To let a direct call of 0x4D6530 run, the scenario adds its trampoline's
return address to the oracle's `MAIL_SITES` (the connected calls).

| Scenario | What it runs | Saves | Result |
|-|-|-|-|
| `mail_branches` | 0x4D6530: a letter of type 0x86 to the chronicle (which keeps any type), to a merchant past the merchants, to −3 (the owners of the offices of a town where the player has one: he keeps it; no caller of the game sends to −3), and to the chronicle with the free list's head past the entries (0x4D6B40 brings it down to the count and grows the mailbox) | 1, 3, 5, 7 | 4 calls equal, 4 of 4 targets |
| `site_start_houses` | 0x500F50 with house types (only the interface's 0x41B7B2 asks for them): the office with the most houses of a type, n 0 (the second) and 5 (the last), and a type it hasn't | 3, 5, 7 (save 1's offices have no houses in their lists) | 3 calls equal, 2 of 2 |
| `ship_trade_branches` | 0x526AA0 on a town without captains and with one that doesn't have the captain; 0x519920 with the ware 0 of the ship's town at 2000 and thresholds below 0, so that 0x52F030 asks for more than the store has (cut to it) | 1, 3, 5, 7 | 3 calls equal, 3 of 3 |
| `ship_ai_branches` | 0x519BA0 on an AI ship that may sail, in the states 11 to 15; 0x501BE0 with the convoy's guide in the state 1 and another ship's [+0x138] over the convoy's [+0x16]; a tick of the world with the player's first ship in port in the state 4, recording the ships' update 0x506720 (its handler 0x506A19 counts the player's ships, [0x6DD8A0]) | 1, 3, 5, 7 | 7 calls equal (8 in save 7), 3 of 3 |

With details broken on purpose the comparison caught them: the 0x86 type
let through (a letter of type 0x86 to the player isn't kept anyway: its
class is 0, so the scenario sends it to the chronicle), the town's offices
not written to, the free head not brought down, the counter of the houses
(with three of a type, save 5), the cut to the store, the captain not found,
the state 12, the convoy's wait and the player's ships in port.

Unreachable: a letter's merchant out of range in 0x4D6680 (the offices of a
town's list always have one), a negative code other than −1..−3 (no caller),
the court letters of events_big.cpp with neither [+0xA] nor [+0x9] a merchant
([+0x9] is then a town: 37 merchants in the saves, 26 towns at most) and the
letter 0x6D of merchants.cpp with the Hanse's town [+0x8E] out of the towns.
The game fails: 0x52F030's `idiv` at 0x52F3B7 with a divisor of 0 or
INT_MIN / −1 (with the rising thresholds the game makes, the divisor is over
0).

## Part D: the unchecked branches of sailing.cpp (2026-11-04)

6 marks: 5 checked with 3 new scenarios, 1 not reproduced (a ship with a
trade route [+0x132]: 0x507FB0 releases it with 0x4D4DD0 on the routes'
object 0x6DD728, which no save has; phase 6). New: `Sailing::fn_50b030`
(a public door to the chase, `pursue`) and the probes of 0x50B030 and
0x507FB0 in `probes_unchecked.cpp`; `retouch_unchecked.py --first-arrival`
(hidden: another process runs a copy of the world to find which ship arrives
first, as two oracles don't fit in one process).

| Scenario | What it runs | Saves | Result |
|-|-|-|-|
| `ship_delete_branches` | 0x507FB0 on a ship out of the table, the first of the free list, and a pirate's ship (out of the pirates' lists) | 1, 3, 5, 7 | 3 calls equal, 3 of 3 targets |
| `pursue_branches` | 0x50B030 on an AI ship at sea: in the state 0x11; raiding a ship in port, one in the state 0x13 and one far away; not raiding, a merchant's ship; in the state 0x16 a pirate convoy's ship out of the list by x (far away, so that only that test drops it) and a pirate far away (kept); near a raiding ship (20, 10 away) a cog at the ticks & 7 1 and 0 (the new heading), a snaikka at 0 and a cog in the shallows. Not the battle when it catches it (0x50B250: the battles' piece 4) | 3, 5, 7 (in save 1 no pirate is in the list by x) | 11 calls equal, 11 of 11 |
| `raid_arrival` | the AI ship at sea that arrives first (found by `--first-arrival`), raiding ([+0x15C]), its port closed ([0x2C8] & 0x200): the ships' update 0x506720 tick by tick until it arrives (0x506E19: first of the sea's list again, without a town; 0x50BA80 finds no Hanse fleet) | 3, 5, 7 (in save 1 another ship enters that port first) | 6, 36 and 6 calls equal, 1 of 1 |

With details broken on purpose the comparison caught them: the pirate's
ship left in its pirate's list, a free ship deleted again, the route kept
when the raided ship is in port, the target dropped in the state 0x16, the
shallows' rectangle, the pirate out of the list by x (alone: with the state
0x16's broken too, the two give the same) and the raiding ship not put back
at the head of the sea's list.

## Part D: the unchecked branches of the convoys (2026-11-05)

14 marks in `convoys.cpp`, `convoys_sea.cpp`, `convoys_port.cpp` and
`convoys_members.cpp` (and the state 4's, written over two lines): 10 checked
with 2 new scenarios, 3 unreachable, 1 not reproduced. Only comments changed.

| Scenario | What it runs | Saves | Result |
|-|-|-|-|
| `convoy_states` | a tick of the world recording the ships' update 0x506720, with AI convoys in port (waiting, put in the state 0) with [+0x14] & 0x10 (disbanded), led by a ship given to the player with its trade route's bit (no routes: interrupted), and with another destination (it sails: 0x519B50 for each AI ship, a destination unlike their [+0x13B]); convoys in the main list put in the states 4 (with the town 0 and without a town), 9, 0xA ([+0x16] 5) and 5 (a state the switch doesn't take: disbanded) | 1, 3, 5, 7 | 1 call equal, 8 of 8 targets |
| `convoy_ship_branches` | 0x50A580 with a convoy out of the table; 0x502110 with a convoy at sea whose guide has no hull (0x5017F0); 0x501EB0 with a convoy in port whose second ship has no sailors (it leaves the convoy) | 1, 3, 5, 7 | 3 calls equal, 3 of 3 |

With details broken on purpose the comparison caught them: the disbanding
by the bit 0x10, the player's ship's notice, 0x519B50 (only once the
destination differs from the ships' [+0x13B]: before, it changed nothing),
the state 0xA's countdown, the state 5's disbanding, the new guide and the
ship without sailors. The first version put a convoy at sea in the state
0xC: 0x509F80 takes ships out only in the states 0..5, 0xF and 0x12, so the
«any other state» branch (0x507C7F) loops forever with any other state, in
the game as in p3vita; in a game only the state 5 gets there (the convoys in
6 and 0x14 are out of the list).

Unreachable: a convoy's route index past its points at the end of the route
(`convoys_sea.cpp`), a pirates' convoy at a closed port (they returned
before), and the check after the loop of 0x503B50 (the loop returns first).
Not reproduced: the node a sunk ship adds to the diet mission's list (event
0x34, [+0x8]), which p3vita doesn't keep.

## Part D: the unchecked branches of the battles (2026-11-05)

The last 8 marks of part D, in `battle/`: 2 checked with a new scenario,
6 unreachable. Part D is closed: no «sin comprobar con el oráculo» is left.

| Scenario | What it runs | Saves | Result |
|-|-|-|-|
| `battle_branches` | the operation 0x91 (0x540520) of a pirate convoy's ship, its guide a privateer of the player, taking an AI merchant's ship at sea (out of the sea's list first, as in a battle: the player gets its worth and the letter 0x30); 0x50BFD0 on a pirate convoy (out of the list of convoys at sea and its guide out of the list by x first, as at a battle's start) with a sunk ship with a captain, beaten by the player; 0x509250 selling the player's ship to no merchant (it turns pirate) with a town out | 3, 5, 7 (save 1 has no pirate convoy of two ships) | 3 calls equal, 4 of 4 targets |

With the letter 0x30's amount broken on purpose the comparison caught it
(the letter is compared). The scenario's first versions hung the oracle in
0x504970 (the towns' wares on the way): the taken ship still in a port's
list, and then the convoy still in the list of those at sea, which
0x50BFD0 puts it back into, made a ring of a list; a battle takes them out
first (0x50BC40).

Found: **the letters about a pirate captain sunk by a merchant's convoy are
never sent** (0x50C1C7..0x50C20B, a game bug): 0x50BFD0 deletes the sunk
ship (0x507FB0, which frees its captain: [+0x42] 0xFFFF) and only then looks
at its captain, so the letter 0x54 and the news 0x2A never go out and
0x4F8D90 always gets «no captain». Seen in the oracle (the ship deleted, the
jump at 0x50C20B taken). Fixed behind `fix_game_bugs()` (the user's
decision): the captain the ship had, looked at before 0x507FB0, as the
broken-up convoy's path does (0x50C7EC). With `tick_probe --fix-game-bugs`
(`re/emu/sesion8/uc_fix.sh battle_branches:3`) the call of 0x50BFD0 differs
there: the player's reputation in the towns (0x4F8D90 with a captain) and
the letters; without it, equal.

Unreachable: the side a pirate beats past the merchants (`battle_end.cpp`)
and the side that takes a pirate's ship (`capture.cpp`): pirates don't fight
each other and a side's owner is its ships' (`battle_setup.cpp`), a merchant
or 0xFF; a plundered ship's owner neither 0xFF nor a merchant
(`capture.cpp`); the privateer's random merchant past the merchants
(`pirate_objects.cpp`: a random AI's index plus the humans at or before
it); and the news of a sunk pirate with a side past the merchants but not
0xFF (`ship_step.cpp`).

## The player's actions: buying and selling (2026-11-05)

The first of the player's actions (`ENCARGO_ACCIONES_JUGADOR.md`; the table of
the codes, [formats/operations.md](formats/operations.md)): operations 0x02
(0x537450) and 0x01 (0x537340), rewritten literally in `trade.cpp`
(`fn_537450`, `fn_537340`, with 0x52F3E0 and the merchant's statistics) and
run by `run_operation`; the viewer queues them with the bytes the market window
builds (`player_actions.cpp`: the steps 1, 5, 50 and all, clamped by what the
window keeps of the town, 0x4AAB10, and of the ship, 0x4AA9D0). New for the
oracle: 0x01 and 0x02 in `OPERATIONS` (`town_oracle.py`), probes of the prices
0x52E430 and 0x52E1D0 (`probes_unchecked.cpp`), `player_ops buy` and `sell`
(the market's bytes from the values the window reads), and in
`retouch_unchecked.py` `player_op` and `run_bytes` (an operation as the viewer
builds it, run with the game's executor).

| Scenario | What it runs | Saves | Result |
|-|-|-|-|
| `trade_window` | the viewer's bytes: the player's first ship in port, rich, buys beer (barrels) and grain (loads) with each step and sells them back with each step; grain by 50 with room for 3 loads; beer with the hold past its capacity (the window sends −20000; nothing is bought) | 1, 3, 5, 7 | 16 calls equal, 3 of 3 targets |
| `trade_branches` | operations made by hand: buying in a town out, in another town than the ship's, banned ([+0x608]), more than the store (rounded to barrels and loads), with the hold past its capacity, with 100, 0 and 1 coins, 1100 of beer with a coin less than its price, 40 barrels of a store of 41 with a coin less than the price of 10 (0x52F3E0 climbs barrel by barrel), 0, −200; selling in a town out, banned, more than the cargo, in another town (0x537340 doesn't look) | 1, 3, 5, 7 | 20 calls of 0x535760 and every price call (0x52E430, 0x52E1D0) equal, 18 of 18 targets |

Also `compare_save` (`save_oracle.py`, random trades with the game's handlers
on every merchant's docked ships), now on the rewritten handlers: 2000 per save
in the 12 saves, 0 different (`re/emu/sesion9/trade_check.sh`). With details
broken on purpose the comparison caught them: the sale paid with the store after
the sale (`trade_window`, 6 calls) and 0x52F3E0 climbing two barrels at a time
(`trade_branches`, saves 1 and 5; in 3 and 7 both ways end on the same amount).
Taking a ship's flag 0x517770 (4) away from the sale wasn't seen: the purchase
before it (0x517FE0) sets the same bit. Careful: PowerShell's `Copy-Item` keeps
the copied file's date, so a file put back with it isn't rebuilt by make
(`touch` it).

The world 7 days in the 12 saves, 0 different; the old scenarios of
`retouch_player.py` (`take_road`, `give_road`, `vote_self`, `diet_yes`), equal.

## The player's actions: the crew and the captain (2026-11-05)

Operations 0x04 (0x537C20, with 0x4F6CA0 and 0x4F6BD0), 0x05 (0x537DD0), 0x06
(0x537EE0) and 0x07 (0x538000, with 0x526B00), in `town_tick/ship_operations.cpp`;
their windows' bytes in `player_actions.cpp` (`player_ops hire-sailors`,
`dismiss-sailors`, `hire-captain`, `dismiss-captain`). The letter 0x04 of 0x537EE0
(0x537FC6) joins the oracle's `MAIL_CALLS`.

| Scenario | What it runs | Saves | Result |
|-|-|-|-|
| `crew_ops` | the viewer's bytes on the player's first ship in port: hiring 5, 1000 (cut to the room), with a quota of 2, with 1 unemployed (0x4F6CA0 gives 0), with the owner 0xFF and 3 unemployed, in a convoy, without sailors with and without a captain, at sea, a ship out; dismissing 3, all, more than it has, with a low morale, none, at sea; the captain dismissed, again (none), hired back (the letter), one not in the list, a town out, in the state 4; the ship made a convoy's guide (the game's 0x509B90, after 25 sailors and 7 guns) dismissing its captain (0x50A580) | 1, 3, 5, 7 | 28–29 calls equal, 24 of 24 targets |

With the morale's drop divided by the sailors instead of the sailors + 1 and
the quota not lowered, the comparison caught both (8 calls). The world 7 days in
the 12 saves, 0 different.

## The player's actions: repairs (2026-11-05)

Operations 0x03 (0x5357AA → 0x52ACD0, the cost 0x52A9E0) and 0x1D (0x539990,
0x52A950), in `town_tick/ship_operations.cpp`; their bytes in
`player_actions.cpp` (`player_ops repair`, `repair-convoy`). The letter 0x02 of
0x52ACD0 (0x52AEDD) joins `MAIL_CALLS`.

| Scenario | What it runs | Saves | Result |
|-|-|-|-|
| `repair_ops` | the viewer's bytes on the player's first ship in port: without damage (the letter 0x02), half its hull rich and poor, damaged less than the hull / 100, the town's [+0x818] at 2, 5, 10, 15 and 25 (the five rates), the ship given to an AI merchant in debts and to no one, made a convoy's guide (rich, poor, without damage), a ship out, at sea, in the state 4; 0x1D on that convoy with its town out, in its town, in the state 4, and a convoy out | 1, 3, 5, 7 | 21–22 calls equal, 24 of 24 targets |

Broken on purpose and caught: the rate above 20 and the least damage (2 calls);
0x1D without the expenses [+0x4AC] (1 call). Seen in the code (roadmap, «Corregir
los fallos del juego»): the letter 0x01 of a paid repair is built and never sent,
and the rate above 20 goes up again; both fixed behind `fix_game_bugs()` (with
`uc_fix.sh repair_ops:3` only the 9 repairs the player pays differ: the mailbox
and, at the level 25, the price). The world 7 days in the 12 saves, 0
different.

## The player's actions: the ship's windows in the viewer (2026-11-05)

The viewer's tavern, shipyard and ship's window queue the operations of the
sections above; what decides their bytes is what each window lists and offers,
rewritten in `player_actions.cpp`: the ships of 0x504AC0 (`window_ships`, its
modes 0, the tavern's sailors; 1, the shipyard's repairs; 3, the ships that can
hire a captain), the tavern's captain (0x5269A0, already in `ship_trade.cpp`),
the sailors it says there are (0x4F6CA0, or above 50 a number from the roll
the window takes with `rand()` when it opens, 0x5CC479: `tavern_sailors`,
`tavern_roll`) and the repair's operation (0x03, or 0x1D for a ship in a
convoy, 0x5F9327). The game's list is on the heap, so the scenario calls
0x504AC0 through a copier in the oracle's sentinel page (`WINDOW_SHIPS`,
SENTINEL + 0xF80) that copies it into the scratch globals 0x6DFF00, and
tick_probe's probe at that address writes p3vita's there; 0x4F6CA0 has a
probe of its own. The oracle replaces `rand()` (`_rand`), so the roll isn't a
recorded call: the scenario runs the window's own code (0x5CC479..0x5CC4AC,
then 0x5D03A5..0x5D03F3) and compares the roll's float and the number shown
with `player_ops tavern-sailors`.

| Scenario | What it runs | Saves | Result |
|-|-|-|-|
| `ship_windows` | the three lists on the player's first ship in port: as loaded, marked [+0x136] & 1, in the states 1 and 2, damaged and whole, without and with a captain; made a convoy's guide (0x509B90) with another ship of his chained in without a captain, the convoy's flag 2 and its states, its members damaged or not, the guide marked; a merchant out of the world. The tavern's captain: all free, the first busy, one kept for the player behind a busy one, one kept for another merchant, none free, an empty list. 0x4F6CA0 with 1, 3 and 200 unemployed and quotas 2, 30 and 100, and the tavern's count under and over 50. Then the viewer's bytes with the window's choices: the offered captain hired for the listed ship, the shown number of sailors hired, all of them dismissed, the convoy repaired | 1, 3, 5, 7 | 76 calls equal (50 lists, 15 of 0x4F6CA0, 7 of 0x5269A0, 4 operations), 29 of 29 targets, the tavern's count equal |

Broken on purpose and caught: the convoy's state in the repairs' list (≤ 2
instead of ≤ 1): 1 call different in saves 1 and 5. The world 7 days in the
12 saves, 0 different; `trade_window`, `trade_branches`, `crew_ops` and
`repair_ops` as before. The viewer without a window (save 1, Lübeck, with
`--debug-state damaged,tavern-captain`): 15 sailors hired of the 17 shown (the
ship's room), 3 dismissed, a captain hired (the letter «Contratación») and
dismissed, the ship repaired for 2,476 coins (the state 4); touching the
tavern and the shipyard opens them.

## The game's static initializers (2026-11-03)

At start the CRT runs about 550 initializers (`_initterm`, 0x63E292, on the tables 0x692890..0x6928A4 and 0x692000..0x69288C); the oracle runs none of them but the 4 CString allocators (0x64F005...). Checked by running them all in the emulator before the load (scratchpad `init_oracle.py`, with a few more Windows functions; 16 of them, the interface's, stop half done) and comparing the whole world with p3vita (`world_init.sh`). The simulation reads only two values they set differently:
- the session's network state ([0x6DF2F0 + 0x2C], 0x6DF31C, SchnellTransfer.dll: 0xFFFFFFFF from the constructor 0x54A67E, 0 when a game runs; the simulation skips the towns' building, the AI's orders and more while it isn't 0), which the oracle and p3vita already have at 0;
- the town of the AI's next feast ([+0x88] of the world, 0xFF from the world's constructor 0x52F520; neither the save nor the reader nor the reset 0x530230 sets it, so the AI's feasts never happen in the game). The oracle (`SaveOracle.__init__`) and p3vita (`load_world_header`) now leave it as the constructor does; before, both had 0 and held a feast in town 0 soon after the load.

With both, the oracle with every initializer and the session at 0 gives the whole world 7 days in the 12 saves with 0 differences against p3vita. The other values the initializers change and the simulation reads are heap addresses (the allocations come before the load's). A table of battle texts filled at start (0x46BFC0) is only read by the battle window's path, which the oracle jumps over.

## The fixes to the game's bugs (2026-10-31)

`p3::data::fix_game_bugs()` (`src/p3data/game_fixes.hpp`) is **off in the library**, so the oracle's comparisons (tick_probe, the scenarios, the writer's checks, the tests) see the original behaviour, bugs included, as always. The viewer turns it on by default (its Options screen). `tick_probe --fix-game-bugs` runs p3vita with the fixes: the differences then show exactly where a fix acts (for example, in tut0 an AI merchant in debt puts a building on sale, operation 0x28, which the game doesn't because of an uninitialized variable in 0x4F5A20). The list of bugs and which are fixed: roadmap, «Corregir los fallos del juego».
