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
