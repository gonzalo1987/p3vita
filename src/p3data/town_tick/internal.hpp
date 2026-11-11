#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "p3data/game_memory.hpp"
#include "p3data/game_texts.hpp"
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
// battle/pirate_objects.cpp: the end of 0x509250 for a winner that isn't a
// merchant (0x5095EB): the ship goes to the pirate object nearest to the
// town, or to a new one (its route with `sailing`).
void fn_509250_pirates(SaveGame& save, Sailing* sailing, uint16_t ship, uint32_t town);
}  // namespace p3::data

namespace p3::data::tick {
namespace events_mayor_detail {
// events_mayor.cpp: 0x4F5900 (on the merchant), out of his ship lists go
// the ships that are no longer his; 0x509250, a ship sold (or, with a winner
// that isn't a merchant, turned pirate).
void fn_4f5900(SaveGame& save, int merchant);
void fn_509250(SaveGame& save, Sailing* sailing, uint32_t ship, uint32_t winner, int32_t price90, int32_t price,
	uint32_t town);
}  // namespace events_mayor_detail
// operations.cpp: the operation 0x4E (0x53B850), a player's ship turns pirate.
void fn_53b850(SaveGame& save, Sailing* sailing, const uint8_t* operation);
// ship_operations.cpp: the player's operations on a ship's crew and captain.
void fn_5357aa(SaveGame& save, const uint8_t* operation);  // 0x03 repair a ship.
void fn_539990(SaveGame& save, const uint8_t* operation);  // 0x1D repair a convoy.
void fn_537c20(SaveGame& save, const uint8_t* operation);  // 0x04 hire sailors.
void fn_537dd0(SaveGame& save, const uint8_t* operation);  // 0x05 dismiss sailors.
void fn_537ee0(SaveGame& save, const uint8_t* operation);  // 0x06 hire a captain.
void fn_538000(SaveGame& save, Sailing* sailing, const uint8_t* operation);  // 0x07 dismiss the captain.
void fn_5386c0(SaveGame& save, const uint8_t* operation);  // 0x0D a ship's «Pirate» / «Civil».
// The shipyard's: 0x0C upgrade a ship (0x508660), 0x2D and 0x2E its name
// (0x53CCE0), 0x2F cancel a ship ordered (0x535CB9 → 0x52B110).
void fn_508660(SaveGame& save, uint32_t ship, uint32_t levels);
void fn_53cce0(SaveGame& save, const uint8_t* operation);
void fn_535cb9(SaveGame& save, Sailing* sailing, const uint8_t* operation);
// What the windows read too (player_actions.cpp): the sailors the merchant
// may hire in a town (0x4F6CA0) and what repairing a ship or its convoy
// costs (0x52A950).
uint8_t fn_4f6ca0(const SaveGame& save, const MerchantMemory& m, uint32_t town);
int32_t fn_52a950(const SaveGame& save, int town, int ship);
// 0x52B670 (on the town: &out, ship, levels): what upgrading the ship
// `levels` levels costs in the town's shipyard, in the game's 0x44 bytes
// (player_actions.hpp, ShipUpgrade); whether it can be done.
bool fn_52b670(const SaveGame& save, int town, GameMemory<0x44>& out, uint32_t ship, uint32_t levels);
// 0x52AAD0 (on the town: ship, flag): the days repairing the ship (or its
// convoy) takes there; the shipyard's page shows them.
int32_t fn_52aad0(const SaveGame& save, int town, int ship, uint32_t flag);

// --- Utilidades comunes (shared.cpp) ---

// rand() de MSVC (0x639C03): 0..0x7FFF.
uint32_t game_rand(SaveGame& save);

// Encola una operación para el juego (0x54AA70): se
// guarda en SaveGame::queued_operations (sus 0x14 bytes, los que copia 0x543F10).
void enqueue_operation(SaveGame& save, const uint8_t* operation, size_t size);

// __ftol (0x639C28): de la FPU a entero truncando hacia cero.
inline int64_t ftol(double v) { return int64_t(v); }

// 0x672D78: the day of the year each month starts (u16; the entry 12 is the
// year's end) and what follows it in the exe, which some loops read past
// the table (the road's deadline, 0x5326E0, counting its days as months).
inline constexpr uint16_t kMonthStartTable[130] = {0x0000, 0x001F, 0x003B, 0x005A, 0x0078, 0x0097, 0x00B5, 0x00D4,
	0x00F3, 0x0111, 0x0130, 0x014E, 0x016D, 0x0000, 0x001F, 0x001C, 0x001F, 0x001E, 0x001F, 0x001E, 0x001F, 0x001F,
	0x001E, 0x001F, 0x001E, 0x001F, 0xFFFF, 0x0000, 0x1A21, 0x2E28, 0x0000, 0x4B00, 0x3333, 0x3F33, 0x999A, 0x3F99,
	0xCCCD, 0x3D4C, 0x0000, 0x0000, 0x12DA, 0x51C1, 0x5555, 0x3FD5, 0x0033, 0x0A00, 0x0180, 0x0012, 0x0100, 0x0100,
	0x0019, 0xFE00, 0x0041, 0x0041, 0x0041, 0x000B, 0x0000, 0x4B00, 0x70A4, 0x3F7D, 0x0000, 0x3F80, 0xB852, 0x3F1E,
	0x0A3D, 0x3ED7, 0x851F, 0x3E6B, 0x0000, 0x0000, 0x6666, 0x3FA6, 0x0000, 0x3F80, 0x3333, 0x3F33, 0xAAAB, 0x3FAA,
	0xAAAB, 0x402A, 0xAAAB, 0x40AA, 0x0000, 0x4080, 0xAAAB, 0x402A, 0xAAAB, 0x402A, 0xAAAB, 0x3FAA, 0x0000, 0x4080,
	0x0000, 0x4080, 0xAAAB, 0x3FAA, 0x0000, 0x4080, 0x0000, 0x4080, 0xAAAB, 0x3FAA, 0xAAAB, 0x3FAA, 0xAAAB, 0x3FAA,
	0x0000, 0x4000, 0xAAAB, 0x3FAA, 0xAAAB, 0x3F2A, 0x0000, 0x4000, 0xAAAB, 0x3FAA, 0xAAAB, 0x3F2A, 0x0000, 0x4080,
	0xAAAB, 0x3FAA, 0x0000, 0x0000, 0x0000, 0x4080, 0xAAAB, 0x402A, 0xAAAB, 0x3FAA};

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
// 0x4FB580 (grid.cpp): up to 16 streets planned become built ones, ring by
// ring from the town's middle; how many.
uint32_t fn_4fb580(SaveGame& save, int town);
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
// 0x64 (un robo en una oficina: el evento 0x33, en 0x5363EE).
void fn_5363ee(SaveGame& save, const uint8_t* operation);
// 0x29 (un préstamo: el evento 0x01, 0x539DC0) y 0x81 (un encargo de la
// dieta de la Hansa: el evento 0x05, 0x53FA80).
void fn_539dc0(SaveGame& save, const uint8_t* operation);
void fn_53fa80(SaveGame& save, const uint8_t* operation);
// The moneylender's: 0x2A, a loan paid off before it falls due (0x539F30),
// and 0x2B, company shares bought or sold (0x535C5A → 0x4D98F0 on the event
// queue, with the operation's [+4]: events 0x18).
void fn_539f30(SaveGame& save, const uint8_t* operation);
void fn_4d98f0(SaveGame& save, const uint8_t* operation);
// 0x37 (el comerciante paga por una ciudad, 0x4F8560; en 0x535E04).
void fn_535e04(SaveGame& save, const uint8_t* operation);
// 0x61 (la fiesta de un comerciante en una ciudad: el evento 0x07, 0x53DBF0).
void fn_53dbf0(SaveGame& save, const uint8_t* operation);
// 0x2C (un barco encargado al astillero de una ciudad, 0x52A360; en 0x535C70).
void fn_535c70(SaveGame& save, const uint8_t* operation);
// Las que solo crean o tocan eventos: 0x38 (una subasta de barco, el evento
// 0x0A; en 0x535E3B), 0x7C (la peste, el 0x1C; en 0x536642), 0x7F (ciudades
// con 0x4000000 y eventos 0x35; en 0x5366E8), 0x80 (el registro 0x80 de la
// cola; en 0x536756) y 0x83 (un evento 0x00; en 0x5367B5).
void fn_535e3b(SaveGame& save, const uint8_t* operation);
void fn_536642(SaveGame& save, const uint8_t* operation);
void fn_5366e8(SaveGame& save, const uint8_t* operation);
void fn_536756(SaveGame& save, const uint8_t* operation);
void fn_5367b5(SaveGame& save, const uint8_t* operation);
// 0x84 (el encargo del jefe de la Hansa: el evento 0x30, 0x53FCA0), 0x67 (un
// administrador mejora, 0x53DDF0) y 0x48 (una propuesta para una ciudad: la
// votación 0x2E o, sin consejo, el impuesto, 0x53B010).
void fn_53fca0(SaveGame& save, const uint8_t* operation);
void fn_53ddf0(SaveGame& save, const uint8_t* operation);
void fn_53b010(SaveGame& save, const uint8_t* operation);
// 0x28 (un comerciante pone en venta un edificio: la subasta 0x4DAB00, 0x53D190).
void fn_53d190(SaveGame& save, const uint8_t* operation);
// 0x7E (la temporada de piratas que viene: el evento 0x1F, 0x53F8B0).
void fn_53f8b0(SaveGame& save, const uint8_t* operation);
// 0x7D (un incendio en una ciudad: el evento 0x1D; en 0x5366A0).
void fn_5366a0(SaveGame& save, const uint8_t* operation);
// 0x13 (el capitán del barco de un jugador humano se retira: el evento 0x27, 0x538C40).
void fn_538c40(SaveGame& save, const uint8_t* operation);
// 0x60 (a merchant's wedding is arranged: event 0x15, 0x53DA90).
void fn_53da90(SaveGame& save, const uint8_t* operation);
// 0x72 (the player answers a suitor's letter 0x21: the event 0x17 or the letter 0x2B; 0x53EAC0).
void fn_53eac0(SaveGame& save, const uint8_t* operation);
// 0x77 (the creditor answers a letter 0x79: lenient, or the impounding with the letter 0x83; 0x53EEC0).
void fn_53eec0(SaveGame& save, const uint8_t* operation);
// 0x65 (an AI merchant goes bankrupt, 0x4F5D80; 0x53DD70).
void fn_53dd70(SaveGame& save, const uint8_t* operation);
// 0x87 (a child is born to a married merchant, 0x53FEA0).
void fn_53fea0(SaveGame& save, const uint8_t* operation);
// 0x0A (a ship is deleted, Sailing::fn_507fb0; in 0x535866).
void fn_535866(SaveGame& save, Sailing* sailing, const uint8_t* operation);
// 0xB8 (network games' sync checks, 0x542F10).
void fn_542f10(SaveGame& save, const uint8_t* operation);
// construction.cpp: la operación 0x22, construir (0x53CE30): edificios
// (0x521900), torres (0x523830, 0x523D70) y murallas (0x522B20).
// Y los materiales: 0x27 (0x535BBC: la ciudad los recibe) y 0x23 (0x535B12:
// la muralla de un lado, 0x522B20).
namespace merchants_detail {
// merchants.cpp: 0x4F9430, a merchant's title (by his money and fame).
uint8_t fn_4f9430(const SaveGame& save, int merchant);
}  // namespace merchants_detail
namespace construction_detail {
// The operations 0x22 and, with `request`, 0x21 (0x53CE30); 0x5D (0x5362AE):
// a merchant opens an office (0x51B890).
void fn_53ce30(SaveGame& save, const uint8_t* operation, bool request);
void fn_5362ae(SaveGame& save, const uint8_t* operation);
// The operation 0x26 (0x535BAD → 0x53CF50): a merchant demolishes a building of
// his; 0x51F050 (on the town): a record back to the free list.
void fn_53cf50(SaveGame& save, const uint8_t* operation);
void fn_51f050(SaveGame& save, int town, uint32_t record);
// The operation 0x25 (0x535B7B → 0x5244F0 on the town, with the merchant): streets.
void fn_5244f0(SaveGame& save, int town, uint32_t merchant);
void fn_535bbc(SaveGame& save, const uint8_t* operation);
void fn_535b12(SaveGame& save, const uint8_t* operation);
void fn_535b44(SaveGame& save, const uint8_t* operation);
// 0x4F8560 (sobre el comerciante): paga por el edificio 0x22 de la ciudad
// la primera vez; también la operación 0x37.
void fn_4f8560(SaveGame& save, int merchant, uint32_t town, int32_t amount);
// 0x521240 (sobre la ciudad): borra un edificio (y los que le siguen en su
// lista) y lo quita de la rejilla.
void fn_521240(SaveGame& save, int town, uint32_t record);
// Días de obra de un tipo de edificio (0x672BB4).
uint8_t build_days(uint8_t type);
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
// sieges.cpp: 0x636920 (on a town's siege object): the pitch its besieging
// army uses each tick (0x632690 + 0x6326E0), 0 without an army. The price
// thresholds (0x528070) add it during the siege.
uint32_t fn_636920(const SaveGame& save, int siege);
// sieges.cpp: la operación 0x8E, un asedio a una ciudad (en 0x536A67:
// 0x633AF0 crea el ejército sitiador y 0x629C60 lo prepara).
void fn_536a67(SaveGame& save, const uint8_t* operation);
// sieges.cpp: the operations 0x8B (the prince asks the player for goods: a node of the siege's list [+0x1020]
// and the letter 0x64; 0x635B80) and 0x8C (the player accepts: the letter 0x65; 0x635CD0).
void fn_536974(SaveGame& save, const uint8_t* operation);
void fn_5369c7(SaveGame& save, const uint8_t* operation);
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
int events_towns(SaveGame& save, uint16_t event, uint16_t type);  // 0x3, 0x1, 0x2a, 0x2b, 0x27
int events_mayor(SaveGame& save, uint16_t event, uint16_t type);  // 0x2, 0xa, 0x36, 0x20, 0x21
// events_mayor.cpp: 0x5311D0 (on the world), a tick's date: the day of the
// month (from 1), the month (from 1) and the year ((tick >> 8) / 365), as
// the letters carry it.
namespace events_mayor_detail {
void fn_5311d0(uint32_t tick, uint8_t& day, uint8_t& month);
void fn_5311d0(uint32_t tick, uint8_t& day, uint8_t& month, uint16_t& year);
}  // namespace events_mayor_detail
// diet.cpp: las misiones que aprueba la dieta de la Hansa (eventos 0x23,
// 0x24 y 0x34, 0x4E09D4) con su flota (0x50AC40). Devuelve 1 si el evento
// sigue (con el tick ya cambiado) o 0 si se quita.
namespace diet_detail {
int fn_4e09d4(SaveGame& save, uint16_t event, Sailing* sailing);
// 0x4F8CA0 (on a merchant, a town): his reputation there down 3 (with no such
// town, in every Hanse town), [+0x22] down 60 and [+0x1E] down 1.
void fn_4f8ca0(SaveGame& save, uint32_t merchant, uint32_t town);
}  // namespace diet_detail
namespace sieges_detail {
// 0x633DB0 (on the town's siege object): the town pays a ransom or is
// plundered (with `convoy`, by a town battle's attackers: the wares go into
// its ships). Its money in `amount`, the offices looked at in `offices`;
// whether it paid anything.
bool fn_633db0(SaveGame& save, int siege, uint32_t& amount, uint16_t& offices, uint16_t convoy = 0xFFFF);
}  // namespace sieges_detail
int events_rare(SaveGame& save, uint16_t event, uint16_t type);  // 0xe, 0x12, 0x1c, 0x1e, 0x1f, 0x25, 0x26, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x35, 0x38, 0x39
// La navegación que usa la cola de eventos (agua en un punto del mapa,
// 0x4474D0, para las tormentas). La pone quien la atiende; sin ella, no hay agua.
void set_event_navigation(const NavData* nav);
const NavData* event_navigation();
// events_rare.cpp: los barcos (Sailing) para la vuelta de las expediciones
// (evento 0x12, 0x502B20). Lo pone quien atiende la cola; sin ellos, el
// convoy no vuelve al mapa.
void set_event_sailing(Sailing* sailing);
Sailing* event_sailing();
// events_rare.cpp: the game's texts for the letters the simulation composes
// (the expedition's report, the elections...): the installation's, through
// the event queue's Sailing; empty without them. letter_text gives one raw,
// as the executable has it.
const GameTexts* letter_texts();
std::string letter_text(GameText id);
// The name of the town `town` (an index) as the letters take it from the
// table 0x6DDA00 (SaveOpaque::town_names), which goes by town number: a game
// bug, the index is used; with fix_game_bugs(), the town's number.
std::string letter_town_name(const SaveGame& save, uint32_t town);

// ship_ai.cpp: los manejadores de 0x506720 para un barco de la lista de
// barcos en puerto en los estados 0 (0x5067D0), 4 (0x506A19) y 5 (0x506B6A).
// `link` apunta al u16 que enlaza el barco (la cabeza [+0xE8] del objeto de
// los barcos o el [+0x6] del anterior). Devuelven true si el recorrido sigue
// por el [+0x6] del barco y false si el barco ha salido de la lista (el
// recorrido sigue por `link`).
bool fn_5067d0(SaveGame& save, int ship, uint16_t* link, Sailing* sailing);
bool fn_506a19(SaveGame& save, int ship, uint16_t* link);
bool fn_506b6a(SaveGame& save, int ship, uint16_t* link);
void fn_518b80(SaveGame& save, int ship, uint32_t arg);  // Un barco de la IA decide qué hacer en puerto.
// events_rare.cpp: the cargo bought for a ship (also trade.cpp, operation 0x02).
namespace events_rare_detail {
void fn_517fe0(SaveGame& save, int ship, uint32_t ware, int32_t amount, int32_t price);
// Una pieza de muralla de 0x4FA1D0: celda (x, y), tipo (0x36 tramo, 0x37 y
// 0x38 esquinas o torres) y orientación.
struct WallPiece {
	uint8_t x = 0;
	uint8_t y = 0;
	uint8_t type = 0;
	uint8_t param = 0;
};
// 0x4FA1D0 (on town_class1): the planned wall's pieces from the wall point
// `side` (also 0x545F50, a town of a new game).
void fn_4fa1d0(SaveGame& save, int town, uint32_t side, std::vector<WallPiece>& out, uint32_t& corners);
}  // namespace events_rare_detail
// ship_ai.cpp: lo que usan también los convoyes (src/p3data/convoys*.cpp).
namespace ship_ai_detail {
void fn_517770(SaveGame& save, int ship, uint32_t flags);  // Aviso a la interfaz del barco y de su convoy.
bool fn_519ba0(const SaveGame& save, int ship);  // ¿Puede zarpar?
bool fn_519c40(const SaveGame& save, int ship);  // ¿Puede guiar un convoy?
void fn_509ae0(SaveGame& save);  // Más convoyes libres si no quedan.
void fn_501be0(SaveGame& save, int convoy);  // Recalcula el convoy a partir de sus barcos.
void fn_501680(SaveGame& save, int convoy, uint16_t ship, uint16_t mode);  // Convoy nuevo guiado por `ship`.
// 0x509B90 (ships object): the docked ship founds a convoy (operation 0x15 too).
void fn_509b90(SaveGame& save, uint32_t ship, uint32_t immediate, uint32_t wait_days, uint32_t destination);
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
