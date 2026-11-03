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
- **Which calls run**: with `P3_MAIL_STATS=1` the oracle prints at the end, to stderr, every call to 0x4D6530 by its address, «(quiet)» when it was silenced (the scratchpad's `stats.sh` runs the 12 saves).

## The game's static initializers (2026-11-03)

At start the CRT runs about 550 initializers (`_initterm`, 0x63E292, on the tables 0x692890..0x6928A4 and 0x692000..0x69288C); the oracle runs none of them but the 4 CString allocators (0x64F005...). Checked by running them all in the emulator before the load (scratchpad `init_oracle.py`, with a few more Windows functions; 16 of them, the interface's, stop half done) and comparing the whole world with p3vita (`world_init.sh`). The simulation reads only two values they set differently:
- the session's network state ([0x6DF2F0 + 0x2C], 0x6DF31C, SchnellTransfer.dll: 0xFFFFFFFF from the constructor 0x54A67E, 0 when a game runs; the simulation skips the towns' building, the AI's orders and more while it isn't 0), which the oracle and p3vita already have at 0;
- the town of the AI's next feast ([+0x88] of the world, 0xFF from the world's constructor 0x52F520; neither the save nor the reader nor the reset 0x530230 sets it, so the AI's feasts never happen in the game). The oracle (`SaveOracle.__init__`) and p3vita (`load_world_header`) now leave it as the constructor does; before, both had 0 and held a feast in town 0 soon after the load.

With both, the oracle with every initializer and the session at 0 gives the whole world 7 days in the 12 saves with 0 differences against p3vita. The other values the initializers change and the simulation reads are heap addresses (the allocations come before the load's). A table of battle texts filled at start (0x46BFC0) is only read by the battle window's path, which the oracle jumps over.

## The fixes to the game's bugs (2026-10-31)

`p3::data::fix_game_bugs()` (`src/p3data/game_fixes.hpp`) is **off in the library**, so the oracle's comparisons (tick_probe, the scenarios, the writer's checks, the tests) see the original behaviour, bugs included, as always. The viewer turns it on by default (its Options screen). `tick_probe --fix-game-bugs` runs p3vita with the fixes: the differences then show exactly where a fix acts (for example, in tut0 an AI merchant in debt puts a building on sale, operation 0x28, which the game doesn't because of an uninitialized variable in 0x4F5A20). The list of bugs and which are fixed: roadmap, «Corregir los fallos del juego».
