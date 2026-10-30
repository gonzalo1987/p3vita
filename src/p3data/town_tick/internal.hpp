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
// shared.cpp tiene las funciones pequeñas que usan varios.

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
