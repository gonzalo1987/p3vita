# Inventario de la simulación

Qué le falta a p3vita para hacer todo lo que hace la simulación del juego,
sacado del ejecutable y no de lo que sale en las partidas. Hasta ahora se
reescribía lo que las partidas y los estados retocados recorren (cada
función entera, con las ramas que no salen marcadas «Sin comprobar con el
oráculo»); pero había tipos de evento, operaciones y funciones enteras a las
que no se llegaba, y p3vita los saltaba en silencio.

## Cómo se ha hecho

`re/emu/sesion5/callgraph.py` (fuera del repositorio, Python de Windows) lee
el desensamblado entero (`re/save_re/p3.asm`) y las funciones de p3vita
(`fn_XXXXXX` definidas en `src/`, «reescritas»; direcciones en comentarios,
«nombradas»), y escribe en `re/emu/sesion5/inventario/`:

| Fichero | Qué |
|-|-|
| `callgraph.tsv` | las funciones alcanzables desde advance_time (0x530E80) y el ejecutor de operaciones (0x535760): tamaño, quién las llama, si p3vita las tiene |
| `callgraph_ops.tsv` | los 193 códigos de operación: su manejador, a qué llama y cuánto hay debajo sin reescribir |
| `callgraph_encolar.tsv` | las 363 llamadas a 0x54AA70 (encolar): la función, si se llega a ella desde advance_time y por dónde, y el código (adivinado; los dudosos, leídos a mano o de los recuentos del oráculo) |
| `callgraph_eventos.tsv` | los 58 tipos de evento: su manejador y quién los crea (0x4D8CF0 y el tipo en [+6]) |
| `callgraph_olvidadas.tsv` | las llamadas desde lo que p3vita ya tiene a funciones que p3vita ni nombra |
| `callgraph_puertas.tsv`, `_pendiente.tsv` | las ramas sin reescribir por tamaño (con interfaz que se cuela: orientativo) |

El grafo es estático y heurístico: no sigue las llamadas indirectas (tablas
de métodos) y la poda de la interfaz (llamadas con `ecx` de un objeto de la
interfaz: *0x6E51AC, *0x6CC7E8, *0x6CBB40, *0x6CBDD8, *0x6E55D0, y una
lista) deja pasar cosas. Lo de abajo está revisado a mano.

## A. Eventos que p3vita crea y no atiende

Huecos reales: el código de p3vita los crea como el juego, pero al vencer
el despachador los quita sin hacer lo suyo. No ha salido en las
comparaciones porque tardan en vencer o porque su efecto tarda.

- [ ] 0x23, 0x24 y 0x34 (0x4E09D4, unos 7,6 KB): las decisiones de la dieta de la Hansa (los crea el evento 0x21); los consultan los convoyes y los barcos. Encola además la 0x81 a los jugadores humanos con un barco en la ciudad
- [x] 0x25 (0x4E1B84) y 0x26 (0x4E1E54): los incendios del asedio (los crea 0x6281F0 al caer un objetivo y al llegar un grupo): el edificio arde (100 de vida, 51 o 1 cada 5 ticks) y al final una torre queda en ruinas (0x36) y lo demás se borra (0x521240, con 0x4FACD0 ya para todos los tipos; 0x4DB100 lo quita de las subastas). Los gráficos solo con la vista de la ciudad. Escenario `incendio` (partidas 3 a 7) y los asedios de verdad de las partidas 3, 4, 5 y 7 (el primer incendio, el día 13 de la 7), iguales. Falta la guarida pirata (ciudad 0xFE − k, 0x515C00), que llega desde las batallas
- [x] 0x27 (0x4DDC00): un capitán de la IA que se retira (lo crea el evento 0x03): deja el barco (0x5098B0) según el estado de este. Escenario `capitanes` de `retouch.py`: iguales en las partidas 1 a 7
- [x] 0x31 (0x4EAB94): el impuesto extraordinario que aprueba el consejo de una ciudad (lo crea el evento 0x2E): lo pagan los comerciantes con oficina, con deudas si no pueden, y puede haber queja a la Hansa (evento 0x30). Escenario `impuesto`: el estado igual en las 7; en 2 de ellas la IA de un comerciante endeudado elige después distinto por un fallo del juego (una variable sin inicializar en 0x4F5A20; ver el roadmap)
- [x] 0x38 (0x4ECB14): el monumento al alcalde reelegido (lo crea 0x528E60): el edificio 0x2D de la ciudad pasa a ser suyo cuando la muralla está. Escenario `monumento`: iguales en las 7

## B. Operaciones que encola la simulación y p3vita descarta

Las que encolan funciones a las que se llega desde advance_time pasando
solo por lo que ya tiene p3vita. Casi todas son de unos cientos de bytes a
3 KB.

- [ ] 0x0A (0x4768B0, barcos; 0x507FB0)
- [ ] 0x13 (evento 0x03, 0x4DCEA0; 0x538C40): crea el evento 0x28 (0x4DDF40)
- [ ] 0x28 (la IA, 0x4F5A20; 0x53D190)
- [ ] 0x2C (evento 0x2B, 0x4DE790; 0x52A360 y 0x52B2C0)
- [ ] 0x38 (0x4DD400; crea un evento 0x0A, que p3vita atiende)
- [ ] 0x48 (avisos, 0x51E070 y 0x51E270; 0x53B010)
- [ ] 0x60 (evento 0x16; 0x53DA90): crea el evento 0x15 (0x4E7C74)
- [ ] 0x61 (0x5316E0; 0x53DBF0): crea el evento 0x07 (0x4E23A4)
- [ ] 0x65 (la IA, 0x4F4E50; 0x4F5D80)
- [ ] 0x67 (0x4DCEA0; 0x53DDF0)
- [ ] 0x7C (0x4DCA90; crea un evento 0x1C, la peste)
- [ ] 0x7D (0x4DCBF0): crea el evento 0x1D (0x4E9564)
- [ ] 0x7E (evento 0x1F; 0x53F8B0)
- [ ] 0x7F (evento 0x0D, 0x4E3F44): marca ciudades con 0x4000000 y crea eventos 0x35
- [ ] 0x80 (evento 0x05; 0x4D8DB0)
- [ ] 0x83 (evento 0x00; crea un evento 0x00)
- [ ] 0x84 (eventos 0x1F y 0x03; 0x53FCA0)
- [ ] 0x87 (un hijo, 0x4F69B0; 0x53FEA0)
- [ ] 0x9F (0x50B250, barcos)
- [ ] 0xB5 (0x514000, barcos; 0x514D40)
- [ ] 0xB8 (eventos 0x03, solo en partidas en red; 0x542F10)

Ya se reproducen: 0x00, 0x12, 0x1E, 0x1F, 0x22, 0x23, 0x24, 0x27, 0x29,
0x37, 0x3B, 0x46, 0x4B, 0x64, 0x78, 0x79, 0x7A, 0x7B, 0x81, 0x85, 0x86 y
0x8E.

## C. Llamadas desde lo reescrito a funciones que p3vita no nombra

De `callgraph_olvidadas.tsv` (70 llamadas), quitando los accesos de 16 a 32
bytes que p3vita hace en línea (0x5303B0, 0x46C330, 0x4F3630..0x4F3680,
0x621750...) y los manejadores de eventos (A y D). Revisar una a una si son
interfaz o un hueco:

- [ ] el tick del asedio (0x6281F0) → 0x62BE90, 0x62DD70, 0x62E970 y 0x58CFB0, y 0x632780 → 0x62DD70: leen la vista de la ciudad; casi seguro gráficos
- [ ] la candidatura (0x531D50) → 0x46F560 (109 KB debajo) y 0x545CB0
- [ ] el final de la partida (0x4E4A44) → 0x4220B0 (la ventana, interfaz)
- [ ] 0x4EC354 → 0x5E94B0; 0x4EAFE4 → 0x4B9650; 0x4EBF54 → 0x5333C0
- [ ] 0x5462F0 → 0x546450
- [ ] la operación 0x22 (0x53CE30) → 0x51B890 (también la operación 0x5D)
- [ ] 0x4FA1D0 → 0x503DB0 y 0x503D80; la muralla (0x522B20) → 0x503D50 y 0x503D40
- [ ] 0x528070 → 0x636920; 0x4E6A54 y 0x509A50 → 0x512B20; 0x4D6780 → 0x51AAB0; 0x509250 → 0x513720; 0x52C6C0 → 0x52DE10 y 0x52DE40; 0x508AA0 → 0x51A3A0

## D. Lo que marca el propio código

- [ ] «Por reescribir» y «Por traducir» (14; hecho ya 0x4FACD0 para los tipos de menos de 0x32): el principio del encargo de fundar una ciudad y el ataque a la guarida pirata (`alderman.cpp`), 0x4FF2D0 (el almacén del comerciante, `construction.cpp`), 0x629C30 sobre [+0x8] y el intérprete de misiones (`events.cpp`), el ataque al barco 0x5063C0 (`events_rare.cpp`), los mensajes caducados y una rama sin longitud (`events_towns.cpp`), 0x4FACD0 para los tipos de menos de 0x32 (`grid.cpp`), 0x4F99B0 sin plantilla, 0x545CE0 y 0x545F50, y la elección del alcalde en la fundación (`new_town.cpp`)
- [ ] «no se reproduce» y «no se traduce» (115): casi todo avisos, cartas, ventanas y gráficos; repasar que ninguno toque el estado
- [ ] «Sin comprobar con el oráculo» (347): ramas escritas que ninguna partida recorre; probarlas con estados retocados (`tools/oracle/retouch.py`), empezando por las de A y B

## E. Subsistemas aparte

- [ ] **Batallas y encuentros** (0x5FBFE0, 0x6130B0, 0x611D80, 0x60234C, 0x50B250, 0x5053C0, 0x50BA80; con la interfaz, cerca de 1 MB): las persecuciones, los ataques de los piratas a barcos y ciudades, la guarida pirata. Encolan las operaciones 0x92–0xA0 y 0xB4. Ya en el roadmap («Piratas, persecuciones, encuentros y batallas»)
- [ ] **Guiones de los textos y misiones**: 0x511270 interpreta los textos del juego y, con el comando 0xFD, programa eventos 0x1B con un guion que ejecuta 0x4ECF64 (el intérprete). Se arranca desde la interfaz (la operación 0xC0 y otros dos caminos), así que va con la interfaz; en las partidas los 0x1B llegan sin guion. El oráculo salta 0x511270 al cargar
- [ ] **La vista de la ciudad y los gráficos** (0x599040 y los métodos de *0x6E51AC, 0x62C730 en el asedio...): ver el roadmap, fase 2, «La ciudad viva en pantalla»
- [ ] **Las acciones del jugador**: el resto de los 193 códigos de operación los encola la interfaz (comprar, vender, construir, contratar...). Van con la interfaz (fase 5)

## Orden propuesto

1. A: los eventos que p3vita ya crea (empezando por la dieta, 0x4E09D4).
2. B: las operaciones de la simulación, con los eventos que crean.
3. C y los «Por reescribir» de D.
4. Las ramas «Sin comprobar» de lo anterior, con estados retocados.
5. E, cada subsistema cuando toque su parte del juego.
