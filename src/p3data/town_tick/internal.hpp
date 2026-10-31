#pragma once

#include <cstddef>
#include <cstdint>

#include "p3data/game_memory.hpp"
#include "p3data/prices.hpp"
#include "p3data/savegame.hpp"

// Simulación de las ciudades: traducción de las funciones de Patrician3.exe
// que ejecuta el tick de ciudad (0x51BA10). Cada función se llama fn_<dirección>
// y trabaja sobre la memoria del juego de SaveGame (game_memory.hpp) con los
// mismos offsets que el original. Los punteros del juego se cambian por
// índices: una ciudad es su índice en SaveGame::towns, una instalación su
// índice (0..20) dentro de la ciudad, etc.
//
// Reparto por ficheros (cada uno con lo que cuelga de sus funciones):
//   core.cpp          0x51BA10 y el resto del tick
//   population.cpp    satisfacción y población: 0x51C830, 0x51CBE0, 0x51C650, 0x520BD0, 0x51C0E0
//   businesses.cpp    instalaciones: 0x5101D0 y las empresas (tipos 4..20)
//   facilities.cpp    instalaciones de los tipos 0..3 (milicia, astillero...)
//   offices.cpp       oficinas de los comerciantes: 0x4FFD40, 0x4FFF20
//   construction.cpp  obras de la ciudad: 0x524920, 0x51E070, 0x51D870
//   ship_ai.cpp       barcos en puerto (estados 0, 4 y 5 de 0x506720) y las
//                     decisiones de los de la IA (0x518B80)
//   ship_trade.cpp    lo que compran y venden los barcos de la IA
//   merchants.cpp     el tick de cada comerciante: 0x4F7BB0
//   alderman.cpp      la candidatura a Gobernador (0x5326E0) y las
//                     operaciones 0x85 y 0x86 que encola
//   daily.cpp         lo diario (0x5315C0), las tareas programadas (0x535450)
//                     y los 15 objetos del mundo (0x52D7D0)
//   sieges.cpp        los asedios de cada ciudad: 0x6349F0, 0x633A90
//   events.cpp        la cola de eventos: 0x4D85C0
// Los convoyes (src/p3data/convoys*.cpp) son de Sailing.
// shared.cpp tiene las funciones pequeñas que usan varios.

namespace p3::data {
class Sailing;
}  // namespace p3::data

namespace p3::data::tick {

// --- Utilidades comunes (shared.cpp) ---

// rand() de MSVC (0x639C03): 0..0x7FFF.
uint32_t game_rand(SaveGame& save);

// Encola una operación para el juego (0x54AA70): se
// guarda en SaveGame::queued_operations (sus 0x14 bytes, los que copia 0x543F10).
void enqueue_operation(SaveGame& save, const uint8_t* operation, size_t size);

// __ftol (0x639C28): de la FPU a entero truncando hacia cero.
inline int64_t ftol(double v) { return int64_t(v); }

// 0x529F90: apunta un movimiento de dinero de la ciudad. `kind` < 11 suma
// `amount` a la estadística [0x704 + 4·kind]; < 5 son ingresos (suman a
// [0x700]) y el 2 (impuestos extra) lleva además la cuenta de [0x6F4]/[0x6F8];
// de 5 a 10, gastos (restan de [0x700]).
void fn_529f90(SaveGame& save, int town, int32_t amount, uint32_t kind);

// grid.cpp: pone un edificio de tipo `type` en (x, y) de la rejilla de la
// ciudad, con lo que hace alrededor (bits de tamaños, calles), si la celda
// vale (0x4FB9C0).
void fn_4fb9c0(SaveGame& save, int town, uint32_t x, uint32_t y, uint8_t type, uint8_t param);
// La huella de una torre en (x, y) (0x4FBDB0).
void fn_4fbdb0(SaveGame& save, int town, uint32_t x, uint32_t y);
// Las murallas (la operación 0x24): una pieza en (x, y) (0x4FBD60); de qué
// lado queda la ciudad y lo que recupera (0x4FD830); el dentro de la ciudad
// (0x4FD680); quitar una pieza de la rejilla al cerrar la muralla (0x4FACD0);
// y 0x4FBFF0 y 0x4FC130 sobre la ciudad.
void fn_4fbd60(SaveGame& save, int town, uint32_t x, uint32_t y);
void fn_4fd830(SaveGame& save, int town, uint32_t x, uint32_t y, uint32_t type);
void fn_4fd680(SaveGame& save, int town);
void fn_4facd0(SaveGame& save, int town, uint32_t x, uint32_t y, uint32_t type, uint32_t param);
void fn_4fbff0(SaveGame& save, int town, int32_t x, int32_t y, uint32_t cls);
void fn_4fc130(SaveGame& save, int town, int32_t x);

// 0x4FCFE0 sobre town_class1 ([0x7A4] de la ciudad): ¿el punto de muralla
// `i` (0..3) cae dentro de la rejilla?
bool fn_4fcfe0(const SaveGame& save, int town, uint32_t i);

// Precios del mercado de una ciudad sobre su memoria: get_buy_price
// (0x52E430) y get_sell_price (0x52E1D0, con la dificultad de la partida).
MarketState market_state(const SaveGame& save, int town, int ware);
int32_t game_buy_price(const SaveGame& save, int ware, int town, int32_t amount);
int32_t game_sell_price(const SaveGame& save, int ware, int town, int32_t amount);

// 0x528070: recalcula los umbrales de precio [0x4F0] (thresholds.cpp). Con
// `compute` = false solo pone {1, 2, 3, 4} en las 19 primeras mercancías.
void fn_528070(SaveGame& save, int town, bool compute);

// --- Funciones que se llaman entre ficheros ---

// core.cpp
void fn_51ba10(SaveGame& save, int town, uint32_t day_of_year);  // El tick de ciudad.

// population.cpp
void fn_51c830(SaveGame& save, int town);  // update_citizen_satisfaction
void fn_51cbe0(SaveGame& save, int town);
void fn_51c650(SaveGame& save, int town);
void fn_520bd0(SaveGame& save, int town);
void fn_51c0e0(SaveGame& save, int town);

// businesses.cpp
void fn_5101d0(SaveGame& save, int town, int facility);  // Tick de una instalación.

// Las empresas producen igual en las ciudades (0x5101D0) que en las oficinas
// (0x4D3780): sobre un almacén (struct storage, los 0x2C0 primeros bytes de
// una ciudad o de una oficina) y una instalación (struct facility, 16 bytes:
// [0x840 + 16·k] de una ciudad o el principio de un registro del mundo).
using StorageMemory = GameMemory<0x2C0>;
using FacilityMemory = GameMemory<0x10>;
inline StorageMemory& storage_of(TownMemory& m) { return reinterpret_cast<StorageMemory&>(m); }
inline StorageMemory& storage_of(OfficeMemory& m) { return reinterpret_cast<StorageMemory&>(m); }
inline FacilityMemory& facility_of(TownMemory& m, int k) {
	return *reinterpret_cast<FacilityMemory*>(m.bytes.data() + 0x840 + 16 * size_t(k));
}
inline FacilityMemory& facility_of(GameMemory<0x14>& world_record) {
	return reinterpret_cast<FacilityMemory&>(world_record);
}
// Producción de cada tipo de empresa (4..20): `max_workers` solo cuenta para
// la producción con todos los puestos ocupados ([0x490] de la ciudad [+7]);
// `reserve`, los trabajadores de reserva que cuentan en el consumo apuntado.
void fn_50ed70(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t max_workers);  // 4 caza
void fn_50e690(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint16_t max_workers, bool whale_oil_limit);  // 5
void fn_50e8b0(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint16_t reserve, uint16_t max_workers);  // 6
void fn_50fbc0(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t max_workers);  // 7 taller
void fn_50ea00(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t max_workers);  // 8 apicultor
void fn_50ead0(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t max_workers);  // 9 grano
void fn_50f460(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t max_workers, bool leather_below_t3);  // 10
void fn_50ecd0(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t max_workers);  // 11 aserradero
void fn_50eeb0(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t max_workers);  // 12 tejeduría
void fn_50ef50(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint16_t reserve, uint16_t max_workers);  // 13
void fn_50f650(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t max_workers);  // 14 fundición
void fn_50f060(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t max_workers);  // 15 ovejas
void fn_50f100(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t max_workers);  // 16 viñedo
void fn_50f220(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint16_t reserve, uint16_t max_workers);  // 17
void fn_50f2e0(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t max_workers);  // 18 ladrillar
void fn_50f390(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint16_t reserve, uint16_t max_workers);  // 19
void fn_50ebf0(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t max_workers);  // 20 cáñamo
// 0x50E380: quita trabajadores a una instalación y los devuelve a su ciudad [+7].
void fn_50e380(SaveGame& save, FacilityMemory& f, uint32_t count, bool allow_all);
// 0x4D4A40 sobre la lista de 0x6DD628: instalación de tipo `type` a la que le faltan `count` trabajadores.
void fn_4d4a40(SaveGame& save, uint16_t type, uint16_t count);

// facilities.cpp (las llama fn_5101d0 según el tipo de instalación [+6])
void fn_510c50(SaveGame& save, int town, int facility);
void fn_50e500(SaveGame& save, int town, int facility);
void fn_50e570(SaveGame& save, int town, int facility);
uint32_t fn_50e610(SaveGame& save, int town, int facility);
uint32_t fn_510aa0(SaveGame& save, int town, int facility);
void fn_50f6e0(SaveGame& save, int town, int facility, uint32_t arg);
// Las mismas sobre un almacén y una instalación cualesquiera: 0x4D3780 las
// llama con las empresas de las oficinas (registros del mundo; los tipos 0..3
// no salen en las 12 partidas) y 0x50F6E0 siempre con arg = 0x14.
void fn_50e500(SaveGame& save, StorageMemory& s, FacilityMemory& f);
void fn_50e570(SaveGame& save, StorageMemory& s, FacilityMemory& f);
uint32_t fn_50e610(SaveGame& save, StorageMemory& s, FacilityMemory& f);
void fn_50f6e0(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t arg);
// 0x4FF2D0: recuenta las empresas y casas de una oficina y crea o quita sus
// empresas (registros del mundo). La llaman 0x51FF30 y 0x51D870 (construction).
void fn_4ff2d0(SaveGame& save, int office);

// offices.cpp
uint32_t fn_4ffd40(SaveGame& save, int office);
void fn_4fff20(SaveGame& save, int office, uint32_t day_of_year);

// construction.cpp
void fn_524920(SaveGame& save, int town, int32_t workers);
void fn_51e070(SaveGame& save, int town);
void fn_51d870(SaveGame& save, int town);

// Ya traducidas y que también usan los barcos de la IA.
namespace facilities_detail {
// 0x5308A0 con create = 0: la oficina del comerciante en la ciudad, o -1.
int fn_5308a0(const SaveGame& save, uint32_t merchant, uint32_t town);
}  // namespace facilities_detail
namespace offices_detail {
// 0x4F8690 y 0x4F8840: estadísticas de venta y de compra del comerciante.
void fn_4f8690(SaveGame& save, int merchant, uint32_t town, uint32_t ware, int32_t money, int32_t amount, bool flag);
void fn_4f8840(SaveGame& save, int merchant, uint32_t town, uint32_t ware, int32_t money, int32_t amount, bool flag);
}  // namespace offices_detail

// merchants.cpp: el tick de un comerciante (0x4F7BB0), que advance_time hace
// en los ticks con t & 7 == 1 (comerciante t >> 3) y t & 7 == 6 ((t + 0x100) >> 3).
void fn_4f7bb0(SaveGame& save, int merchant);

// alderman.cpp: la candidatura a Gobernador (0x5326E0, la llama 0x4F75F0
// cuando un alcalde llega a 120 de reputación para optar), que encola una
// operación 0x86 y una 0x85 por encargo de la Hansa; y lo que hace el
// ejecutor (0x535760, simulation.cpp) con ellas: 0x85 crea el encargo
// (0x53FD10, sobre los 0x14 bytes de la operación) y 0x86 quita los que ya
// no valen (0x53FE20).
void fn_5326e0(SaveGame& save);
void fn_53fd10(SaveGame& save, const uint8_t* operation);
void fn_53fe20(SaveGame& save);
// El encargo de la Hansa ya dado a un comerciante: la parte de 0x4EAFE4
// (evento 0x32 de la cabeza) desde 0x4EB0EB, que devuelve lo que hace el
// despachador (0 quitarlo, 1 recolocarlo); y la operación 0x4B (0x53B440),
// con la que el jugador humano coge un encargo.
uint32_t fn_4eb0eb(SaveGame& save);
// new_town.cpp: fundar una ciudad (0x531D50, con el registro de 16 bytes que
// arma 0x4EBF54) y la matriz de tiempos de viaje con la ciudad nueva
// (0x531BD0).
void fn_531d50(SaveGame& save, const GameMemory<0x10>& rec);
void fn_531bd0(SaveGame& save);
// Los eventos 0x39 (0x4ECDF4, la muralla de la ciudad nueva) y 0x2F
// (0x4EA0D4, su desarrollo), como los de events_rare; y la operación 0x46
// (0x535FD6), que da alcalde a la ciudad nueva.
uint32_t fn_4ecdf4(SaveGame& save);
uint32_t fn_4ea0d4(SaveGame& save);
void fn_535fd6(SaveGame& save, const uint8_t* operation);
// operations.cpp: otros manejadores del ejecutor: 0x7B (capitanes y
// administradores que buscan trabajo en una ciudad, 0x5365E3), 0x79 y 0x7A
// (la dieta de la Hansa, 0x536535 y 0x536570).
void fn_5365e3(SaveGame& save, const uint8_t* operation);
void fn_536535(SaveGame& save, const uint8_t* operation);
void fn_536570(SaveGame& save, const uint8_t* operation);
// Y los convoyes de expedición: 0x1E (sale, 0x539B50) y 0x1F (un punto de
// su ruta, 0x539C60); la salida usa la rama de convoy de la orden de
// zarpar (Sailing::set_sail).
void fn_539b50(SaveGame& save, Sailing* sailing, const uint8_t* operation);
void fn_539c60(SaveGame& save, const uint8_t* operation);
// 0x12 (un capitán mejora, 0x538A80) y 0x78 (un barco nuevo en las 16
// entradas de [0x6DD7C0], 0x5051E0).
void fn_538a80(SaveGame& save, const uint8_t* operation);
void fn_5051e0(SaveGame& save, Sailing* sailing, const uint8_t* operation);
// construction.cpp: la operación 0x22, construir (0x53CE30): edificios
// (0x521900), torres (0x523830, 0x523D70) y murallas (0x522B20).
// Y los materiales: 0x27 (0x535BBC: la ciudad los recibe) y 0x23 (0x535B12:
// la muralla de un lado, 0x522B20).
namespace construction_detail {
void fn_53ce30(SaveGame& save, const uint8_t* operation);
void fn_535bbc(SaveGame& save, const uint8_t* operation);
void fn_535b12(SaveGame& save, const uint8_t* operation);
void fn_535b44(SaveGame& save, const uint8_t* operation);
}  // namespace construction_detail
void fn_53b440(SaveGame& save, const uint8_t* operation);
// La operación 0x3B (0x53A3E0): el comerciante aporta materiales a su
// encargo de la carretera, de su oficina y de sus barcos en la ciudad.
void fn_53a3e0(SaveGame& save, const uint8_t* operation);

// El resto de advance_time (0x530E80), en su orden dentro del tick:
// daily.cpp: al empezar el día, tras la fecha, lo diario (0x5315C0; `weekly`
// si el tick es múltiplo de 0x700) y las tareas programadas (0x535450, sobre
// el [+0xE8] del mundo); en los ticks con t & 7 == 5, el objeto t >> 3 (< 15)
// de los 15 del mundo ([+0xB10], 0x52D7D0).
void fn_5315c0(SaveGame& save, bool weekly);
void fn_535450(SaveGame& save, uint32_t tick);
void fn_52d7d0(SaveGame& save, int object);
// sieges.cpp: en los ticks con t & 7 == 7, tras el tick de la ciudad t >> 3,
// su asedio (0x6349F0); al final de cada tick, el de cada ciudad (0x633A90).
// Sobre SaveGame::sieges[ciudad].
void fn_6349f0(SaveGame& save, int town);
void fn_633a90(SaveGame& save, int town);
// sieges.cpp: lo que hace el lector del ejército sitiador (0x627AD0, la
// llama savegame.cpp) tras leer sus campos: las puertas de la muralla
// ([+0x244..+0x24C]), su posición (0x632780), sus objetivos (0x62B2B0,
// 0x62E810) y los indicadores [+0x78], [+0x79] y [+0x578].
void load_siege_army(SaveGame& save, int town);
// events.cpp: tras los barcos, la cola de eventos (0x4D85C0, sobre el objeto
// 0x6DD73C, con el tick).
void fn_4d85c0(SaveGame& save, uint32_t tick);
// events.cpp: la lista de la cola, que usan también los convoyes, los asedios
// y el lector de la partida.
namespace events_detail {
// 0x4D8DD0: lleva el evento de la cabeza a su sitio en la lista, detrás de
// los que tienen un tick menor o igual.
void fn_4d8dd0(SaveGame& save);
// 0x4D8CF0: toma un registro libre (si no queda ninguno, reserva 0x80 más),
// lo pone en la cabeza con el tick `tick` y lo lleva a su sitio. Devuelve su
// índice.
uint16_t fn_4d8cf0(SaveGame& save, uint32_t tick);
}  // namespace events_detail
// events_*.cpp: los manejadores de los demás tipos de la cola de eventos,
// por grupos. Cada uno devuelve qué hace el despachador (0x4D85C0) con el
// evento `event` (la cabeza de la cola) de tipo `type`: -1 si el tipo no es
// del grupo, 0 quitarlo (vuelve a la lista de libres) y 1 llevarlo a su sitio
// por su tick, ya cambiado (0x4D8DD0).
int events_big(SaveGame& save, uint16_t event, uint16_t type);  // 0x2c, 0x5, 0x16
int events_towns(SaveGame& save, uint16_t event, uint16_t type);  // 0x3, 0x1, 0x2a, 0x2b
int events_mayor(SaveGame& save, uint16_t event, uint16_t type);  // 0x2, 0xa, 0x36, 0x20, 0x21
int events_rare(SaveGame& save, uint16_t event, uint16_t type);  // 0xe, 0x12, 0x1c, 0x1e, 0x1f, 0x2e, 0x30, 0x32, 0x35
// La navegación que usa la cola de eventos (agua en un punto del mapa,
// 0x4474D0, para las tormentas). La pone quien la atiende; sin ella, no hay agua.
void set_event_navigation(const NavData* nav);
const NavData* event_navigation();
// events_rare.cpp: los barcos (Sailing) para la vuelta de las expediciones
// (evento 0x12, 0x502B20). Lo pone quien atiende la cola; sin ellos, el
// convoy no vuelve al mapa.
void set_event_sailing(Sailing* sailing);
Sailing* event_sailing();

// ship_ai.cpp: los manejadores de 0x506720 para un barco de la lista de
// barcos en puerto en los estados 0 (0x5067D0), 4 (0x506A19) y 5 (0x506B6A).
// `link` apunta al u16 que enlaza el barco (la cabeza [+0xE8] del objeto de
// los barcos o el [+0x6] del anterior). Devuelven true si el recorrido sigue
// por el [+0x6] del barco y false si el barco ha salido de la lista (el
// recorrido sigue por `link`).
bool fn_5067d0(SaveGame& save, int ship, uint16_t* link);
bool fn_506a19(SaveGame& save, int ship, uint16_t* link);
bool fn_506b6a(SaveGame& save, int ship, uint16_t* link);
void fn_518b80(SaveGame& save, int ship, uint32_t arg);  // Un barco de la IA decide qué hacer en puerto.
// ship_ai.cpp: lo que usan también los convoyes (src/p3data/convoys*.cpp).
namespace ship_ai_detail {
void fn_517770(SaveGame& save, int ship, uint32_t flags);  // Aviso a la interfaz del barco y de su convoy.
bool fn_519ba0(const SaveGame& save, int ship);  // ¿Puede zarpar?
bool fn_519c40(const SaveGame& save, int ship);  // ¿Puede guiar un convoy?
void fn_509ae0(SaveGame& save);  // Más convoyes libres si no quedan.
void fn_501be0(SaveGame& save, int convoy);  // Recalcula el convoy a partir de sus barcos.
void fn_501680(SaveGame& save, int convoy, uint16_t ship, uint16_t mode);  // Convoy nuevo guiado por `ship`.
void fn_535400(SaveGame& save, uint32_t town, uint32_t merchant);
}  // namespace ship_ai_detail

// ship_trade.cpp (las llama fn_518b80)
void fn_519280(SaveGame& save, int ship);
void fn_5193f0(SaveGame& save, int ship);
void fn_5196b0(SaveGame& save, int ship);
void fn_519920(SaveGame& save, int ship, uint32_t town);
void fn_51a070(SaveGame& save, int ship, int town);  // ecx = el barco, arg = [0x124] (armas) de la ciudad.
// ship_trade.cpp: lo que usa también 0x518FF0.
namespace ship_trade_detail {
uint8_t fn_531b70(const SaveGame& save, uint32_t a, uint32_t b);  // Tiempo de viaje entre dos ciudades.
int32_t fn_516ab0(const SaveGame& save, int ship, uint32_t town);  // Días de viaje del barco hasta la ciudad.
// Cantidad que conviene llevar de `from` a `to` y margen (`factor`).
int32_t fn_52f030(const SaveGame& save, uint32_t from, uint32_t to, uint32_t ware, int32_t days, float& factor);
}  // namespace ship_trade_detail

}  // namespace p3::data::tick
