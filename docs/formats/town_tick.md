# El tick de ciudad

Ingeniería inversa de `Patrician3.exe` (GOG 1.1). Marcas como en
[savegame.md](savegame.md): **[código]** leído en el desensamblado,
**[oráculo]** comparado con el propio código del juego en el emulador.

> Implementación en p3vita: `src/p3data/town_tick/` (una traducción literal de
> las funciones del juego, `fn_<dirección>`, sobre la memoria de los objetos
> con la misma disposición que en el exe, ver `src/p3data/game_memory.hpp`) y
> `src/p3data/simulation.cpp` (cuándo se llama). Comparado con el juego con
> `tools/oracle/town_oracle.py` y `tick_probe` ([../oracle.md](../oracle.md)):
> en las 12 partidas v0x82 de `missav/`, cada función por separado y el tick
> entero, también partiendo de la partida que lee p3vita y dejando que
> evolucione sola durante semanas, 0 diferencias byte a byte en ciudades,
> oficinas, comerciantes, capitanes, registros, rejillas, nodos, cola de
> eventos, globales, `rand` y operaciones encoladas.

## 1. Cuándo

`advance_time` (0x530E80) llama al tick de ciudad (0x51BA10, thiscall sobre la
ciudad, con el día del año como argumento) una vez al día por ciudad: la
ciudad i (0..31) en el tick `8·i + 7` del día y las 32..39 en el
`8·(i − 32) + 3` **[código]**. En los demás ticks del día hace otras cosas que
p3vita aún no reproduce: el tick de cada comerciante (0x4F7BB0, ticks `8·i + 1`
y `8·(i − 32) + 6`), 15 objetos del mundo (0x52D7D0), los asedios (0x6349F0
tras cada ciudad y 0x633A90 cada tick) y, al empezar el día, la fecha
(0x5310D0, sí reproducida), 0x5315C0, las estadísticas (0x54DE90) y las tareas
programadas (0x535450).

## 2. El tick (0x51BA10) **[código + oráculo]**

En orden:

1. Invierno: el bit 0x2 de [0x2C8] se pone de diciembre a febrero (mes 11, 0
   o 1) y se quita el resto del año.
2. El auslieger de la ciudad ([0x994] barco, [0x990] coste): cada 7 días
   (`(tick >> 8) % 7 == 0`), si la ciudad no tiene dinero el barco pasa al
   estado 3 y [0x994] a 0xFFFF; si no, el coste se suma al dinero de su dueño y
   a su [+0x4C8] y se apunta como gasto 7 de la ciudad (0x529F90).
3. Pone a 0 consumo de las empresas [0x64], producción [0xC4] y consumo de los
   habitantes [0x310]; las existencias negativas, a 0.
4. 0x51CD80 (pobres mínimos según los puestos de trabajo), copia los
   habitantes por clase en [0x2E8], 0x527CD0 y el consumo de los habitantes
   (0x527D40, con la hambruna).
5. Vacía [0x490] (producción con todos los puestos ocupados) y la lista de
   puestos ofrecidos (0x6DD628), y hace el tick de las 21 instalaciones
   (0x5101D0, §4); copia el almacén en [0x3D0] (0x527D20).
6. Oficinas de la ciudad (lista [0x784], siguiente en [+0x2CA] de cada
   oficina): suma a [0x924] lo que devuelve 0x4FFD40 y, si es positivo,
   0x524920 (muelles, §6).
7. Cada 16 días (`día del año & 0xF == índice & 0xF`) recuenta calles
   (0x4FA6F0, [0x790] y [0x792]).
8. Satisfacción (0x51C830), población (0x51CBE0), tick de cada oficina
   (0x4FFF20, §5) y la última oficina de la lista pasa a ser la primera.
9. Sin asedio ([0x2C8] & 0x10): los nodos de [0x9B4] (0x50FE40, la mercancía
   que llega de los alrededores). Después 0x51C650, 0x520BD0 (casas y su
   ocupación) y, sin asedio, 0x51C0E0 (mendigos que llegan).
10. Umbrales de precio (0x528070, [savegame.md](savegame.md) §4.1) con el bit
    de mercado de [0x76C].
11. Bits 17–22 de [0x2C8]: la mercancía más escasa (0x51BDB0); la iglesia
    (0x4FE877) y 0x525F30.
12. Sin asedio y con [0x76C] > 0x200: consumo del excedente (0x528630).
13. Si el alcalde [0x6F1] es un comerciante humano, termina. Si no, y ni
    [0x6DE52A] ni [0x6DF31C] valen algo, las obras de la ciudad: 0x51E070 y
    0x51D870 (§6).

## 3. Campos de la ciudad

Además de lo de [savegame.md](savegame.md) §4:

| Campo | Contenido |
|-|-|
| [0x0] (almacén) | u16: 1 cuando a una empresa le falta material |
| [0x13C + 16·w + 2·d] | u16: producción de la mercancía w el día `d = (tick >> 8) & 7` |
| [0x124] 6 × s32, [0x2BC] | artillería y armas de mano del almacén |
| [0x2C3] | contador de hambruna: con grano baja 8; sin él sube 1 (+1 si la carne está bajo su t1, +1 si el pescado bajo su t0); desde 80 hay hambruna y acaba bajo 10 con grano |
| [0x2C8] | 0x2 invierno, 0x8 peste, 0x10 asedio, 0x200 bloqueo, 0x400 boicot, 0x800 piratas, 0x1000 hambruna, 0x2000/0x4000 mala/buena cosecha (las pone un suceso, 0x4E2898), 0xE0 límite de soldados, 0x800000 asedio rechazado, 0x1000000 no queda sitio para muelles; bits 17–22 la mercancía más escasa (`0x20000 \| w << 18`, 0x7C0000 si ninguna) |
| [0x2CC] | aceite de ballena por trabajador del pescador |
| [0x2D0] | tick de la última fiesta |
| [0x2D8] 4 × s32 | ricos, acomodados, pobres y mendigos ([0x2D4] el total); [0x2E8] los del día anterior |
| [0x2F8] 3 × u16 | plazas por clase |
| [0x300] 4 × s16 | satisfacción (−40..80); [0x308] 3 × s16 la objetivo |
| [0x3D0] 24 × s32 | almacén de ayer, que se ajusta alrededor de cada empresa |
| [0x430] 24 × s32 | media en 14 días de lo que baja el almacén |
| [0x490] 24 × u32 | producción con todos los puestos ocupados (también la suman las empresas de las oficinas) |
| [0x6D0] | arma que fabrica el armero |
| [0x6F1] | alcalde; [0x700] dinero; [0x704]/[0x730] 11 movimientos de dinero |
| [0x760]..[0x768] | listas de registros: libres, obras, ciudad, obras pendientes, murallas |
| [0x76C] | 0x80 astillero, 0x100 mercado, 0x200 sin límite de habitantes, 0x400 casa de la moneda; 0x01..0x20 edificios municipales construidos |
| [0x770]..[0x773] | capillas y hospitales; [0x786] defensa, [0x788] torres, [0x789] pozos |
| [0x778]/[0x77E] 3 × u16 | ocupación y plazas de las casas de la ciudad |
| [0x794] | iglesia: [+0] baja 100 al día, [+4] dinero, [+8] días, [+0xC..0xE] materiales entregados, [+0xF] nivel |
| [0x810], [0x814] | experiencia y trabajo del astillero; [0x81C], [0x81E], [0x820] colas de construcción, reparación y convoyes |
| [0x835] | días sin cerveza (milicia); [0x83C], [0x83E], [0x83F]/[0x838] estados de 0x525F30 |
| [0x840 + 16·k] | instalación k (siempre del tipo k): [+0] eficiencia (1024 = 100 %), [+4] trabajadores, [+6] tipo, [+7] ciudad, [+8] productividad, [+0xA] reserva, [+0xC] float, [+0xE] |
| [0x924] | trabajadores de base que se pasan a 0x524920 |
| [0x990], [0x994] | coste semanal e índice del auslieger |
| [0x998]/[0x99C] | soldados y reclutas por tipo; [0x9A0] límite |
| [0x9A4] 8 × u16 | cambio diario de mendigos |
| [0x9B4] | nodos: [+0x10] tipo de instalación, [+0x11] ciudad, [+8] consumo diario de esa mercancía en todas las ciudades, [+0xE] factor en /1024, [+4] lo que entra hoy |

## 4. Instalaciones (0x5101D0) **[código + oráculo]**

Según el tipo [+6] (tabla 0x510880): 0 milicia (0x510C50, 0x50E500 cerveza),
1 astillero (0x50E570: brea, construcción 0x5083B0 y reparación 0x508AA0 de
barcos), 2 obras de la ciudad (0x50E610 → 0x51FF30), 3 armero (0x510AA0 y
0x50F6E0) y 4..20 las empresas, una función por mercancía (caza 0x50ED70,
pescador 0x50E690, ... cáñamo 0x50EBF0). Alrededor de cada empresa se ajusta
[0x3D0] de la mercancía que produce; si sus existencias ya pasaban de t3, la
producción se pierde (los materiales sí se gastan). Después contrata
(0x5108E0, primero de la reserva [+0xA]) o despide (0x5109B0, 0x50E380: cada
trabajador son 4 habitantes que vuelven a la ciudad como mendigos), y los
puestos que faltan van a la lista 0x6DD628 (u32 n; u16 instalación en +4,
u16 trabajadores en +0x82), que lee la población (0x51CBE0).

Las empresas de los comerciantes producen con las mismas funciones (0x4D3780,
§5) sobre el almacén de la oficina y un registro del mundo de 0x14 bytes como
instalación.

## 5. Oficinas (0x4FFD40, 0x4FFF20) **[código + oráculo]**

0x4FFD40 recuenta puestos de trabajo (0x4D49D0) y devuelve los trabajadores
que necesitan casa. 0x4FFF20 hace el tick de cada empresa (0x4D3780: salarios
0x4D3560, trabajadores 0x4D3670, coste medio 0x4D4400), el alquiler y lo que
hace el administrador (capitán [+0x2F2]): mantener existencias (IA, 0x4FFA30),
comprar el consumo de las empresas (0x4FFC20) o comprar y vender a precio fijo
(0x4FF780, con las inversas de los precios 0x52EA80 y 0x52EC50), con las
estadísticas del comerciante (0x4F8690 y 0x4F8840).

| Campo de la oficina | Contenido |
|-|-|
| [0x2CC] | primera empresa (registros del mundo, siguiente en [+8]) |
| [0x2CE], [0x2D0] | primer edificio y primera casa en la ciudad; [0x2D2] edificios terminados, [0x2D4] casas |
| [0x2D6] | órdenes del administrador: 0x10 mantener existencias, 2 comprar el consumo, 1 precio fijo |
| [0x2D8], [0x2DE] 3 × u16, [0x2E4] 3 × u8 | plazas, ocupación y nivel de alquiler de sus casas |
| [0x2EC], [0x2EE], [0x2F0] | capacidad en cargas, capacidad en obras y días de obra |
| [0x2F4 + 4w], [0x354 + 4w], [0x3B8 + 4w] | precio del administrador (> 0 vende, < 0 compra), existencias objetivo y coste medio (float) |
| [0x418]..[0x430], [0x448], [0x44A] | gastos, empleados y puestos |

## 6. Obras de la ciudad (0x524920, 0x51E070, 0x51D870) **[código + oráculo]**

0x524920 busca una celda de muelle (0x0F) y lo construye (las celdas pasan a
0x0B); cuando no queda sitio pone el bit 0x1000000 de [0x2C8] y no lo vuelve a
intentar. 0x51E070 encola avisos (operación 0x48) según la cola de eventos; 0x51D870
decide las obras municipales (bits de [0x76C]), casas, pozos, murallas y
torres, con `rand`, y las encola como operaciones del juego (0x54AA70, 0x14
bytes): 0x22 construir (propietario, ciudad, x, y, tipo, parámetro), 0x23,
0x24, 0x27 materiales (ciudad, ladrillos, madera, herramientas) y 0x48 aviso.
p3vita las deja en `SaveGame::queued_operations` y las ejecuta al final del
tick como el juego: la 0x22 (construir, 0x53CE30: `construction.cpp` y la
rejilla en `grid.cpp`), la 0x27 (materiales), la 0x23 (empezar una
muralla) y la 0x24 (las piezas de muralla pendientes)
([world_tick.md](world_tick.md) §7).

## 7. Globales y otros objetos

- 0x6DD628/0x6DD62C: lista de puestos ofrecidos en el tick (§4).
- 0x6DE4A6: número de registros de 0x14 del mundo; 0x6DE51C el primero libre.
- 0x6DE52A, 0x6DF31C: si alguno no es 0, no hay obras.
- Cola de eventos *0x6DD73C: registros de 0x18 bytes (u32 tick, u16
  siguiente, u16 tipo) en orden de tick desde la cabeza u16 0x6DD744; u16
  0x6DD748 es su número ([savegame.md](savegame.md) §14).
- Capitán: [0xA] habilidad, [0xC] mantenimiento diario.
- Comerciante: [0x40 + 4·ciudad] tipos de empresa que producen hoy, [0x124 +
  12·ciudad] reputación, [0x478] barcos construidos, [0x480..0x4D0] gastos y
  estadísticas.
- Barco: [+0x3C] avisos a la interfaz, [+0x4C] tick en que se terminó; estado
  5, sale del astillero.

## 8. Lo que no se reproduce

- La interfaz: mensajes y cartas (0x548CA0, 0x4D6530), ventanas del jugador y
  el dibujo de la vista de la ciudad (0x590680, 0x5891C0).
- De las operaciones que encolan las ciudades se ejecutan la 0x22
  (construir), la 0x27 (materiales), la 0x23 y la 0x24 (murallas); los
  avisos (0x48) también, como las demás de la simulación
  ([world_tick.md](world_tick.md) §7). Lo que hace el almacén nuevo de un
  comerciante (0x4FF2D0, recontar su oficina) ya está (`construction.cpp`).
- Convoyes en reparación en el astillero (lista [0x820], vacía en las 12
  partidas) y los barcos nuevos de jugadores humanos (carta y ventana).
- Ramas que no se dan en las partidas ni en los estados retocados con los que
  se comprobaron: están marcadas `// Sin comprobar con el oráculo` en el código.
- Fallos del juego, reproducidos tal cual con el ajuste apagado y corregidos con
  `fix_game_bugs()` (roadmap, «Corregir los fallos del juego»): el administrador
  de 0x51C360 solo cuenta la primera oficina; 0x5109B0 deja negativo [+0xC]; la
  granja de grano compara al revés al contratar; 0x524920 escribe fuera de la
  rejilla con un muelle en la última columna (ya sin `assert`).

## 9. El tick del comerciante (0x4F7BB0) **[código + oráculo]**

`advance_time` hace el tick de cada comerciante en los ticks con `t & 7 == 1`
(comerciante `t >> 3`) y `t & 7 == 6` (comerciante `(t + 0x100) >> 3`), antes
de los barcos: un tick al día por comerciante. Traducido en
`src/p3data/town_tick/merchants.cpp` y conectado en `advance_game_tick`;
idéntico al juego en las 12 partidas (60 días, y con estados retocados), y
dentro del mundo entero.

- Cada 7 días (día del juego múltiplo de 7), la contabilidad: el dinero
  [+0x0] suma ingresos [+0x488] y resta gastos [+0x480], salarios [+0x490] y
  otros gastos [+0x4A8], [+0x4D0]; avisos al jugador; se ponen a 0; [+0x15]
  se limita a 200, [+0x25] a 0 y [+0x1E] baja 1.
- Cada día: [+0x16] se acerca a 0 de 11 en 11; [+0x1F] (reputación con los
  marineros) sube 1 hasta 20; [+0x22] se acerca 1 al objetivo 20 × rango en
  la ciudad natal (el 7 cuenta 8) menos [+0x17] si es positivo; los préstamos
  de la cola de eventos (tipo 1: [+0x8] importe, [+0x10] prestamista,
  [+0x11] deudor; 0x4D9080) y los salarios de las tripulaciones (0x517F70,
  [+0xC] del capitán) se suman a las deudas [+0x498] y a los salarios.
- El valor de la compañía [+0x46C]: dinero, oficinas con sus existencias y
  edificios (0x500330, 0x51F220, con los materiales de cada tipo de edificio
  de 0x672A4C… y el valor de cada mercancía de 0x673A18) y barcos (0x52A7D0,
  en 64 bits; con tipo > 3 el juego lee el propio barco como tabla). También
  [+0x470] capacidad, [+0x474] marineros, [+0x478] barcos no hundidos,
  [+0x47A] estado medio (‰) y [+0x47C] barcos.
- Reputación por ciudad: tres componentes en [+0x11C + 12·i] (los dos
  últimos ×0,99 al día), la de hoy [+0x2FC] y la de ayer [+0x3C4]; las
  oficinas dan reputación por sus inquilinos ([0x2DE], [0x2E4]) y sus
  empresas (0,01 por trabajador), el barco de la ciudad ([0x994]) uno más a
  su dueño y el cónyuge ([+0x31], [+0x32]) en la ciudad natal.
- Rangos [+0x39C + i] (0x4F75F0): el día 1 en la ciudad natal y cada día en
  otras dos; para subir hacen falta reputación 7, 12, 20, 40 o 60 y un valor
  de la compañía de 100 000, 200 000, 300 000, 500 000 o 900 000; 6 es
  alcalde ([0x6F1] de la ciudad) y 7 alderman ([0x6DE52D] / [0x6DE52E]); el
  bit de la ciudad en [+0x468] hace falta para los rangos 3 a 5. Optar a
  alderman exige 120 de reputación.

La candidatura a Gobernador (0x5326E0, con 0x532E30, que encola las
operaciones 0x86 y 0x85) solo se llama si [0x6DF31C] es 0 y un alcalde llega
a 120 de reputación, lo que no pasa en las partidas en 60 días: ver
[world_tick.md](world_tick.md) §8. No se reproducen los avisos (0x4D6530,
0x548CA0, y la búsqueda en el buzón 0x4D7AC0).
