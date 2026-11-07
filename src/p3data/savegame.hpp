#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "p3data/battle/battles.hpp"
#include "p3data/battle/pirate_nests.hpp"
#include "p3data/cpr.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/navigation.hpp"
#include "p3data/prices.hpp"
#include "p3data/wares.hpp"

namespace p3::data {

// Partidas guardadas (.pat), ver docs/formats/savegame.md. Se lee lo necesario
// para dibujar las ciudades y para comerciar: edificios, almacenes y precios,
// oficinas, comerciantes y barcos.

// Registro de edificio de 8 bytes.
struct BuildingRecord {
	uint8_t x = 0;  // Celda delantera (ancla) del edificio.
	uint8_t y = 0;
	uint8_t owner = 0;  // Índice de comerciante; >= número de comerciantes: la ciudad.
	uint8_t type = 0;  // 0x00..0x38 (tabla en town_buildings.cpp).
	uint16_t next = 0;  // Siguiente de su lista; >= número de registros termina.
	uint8_t pending = 0;  // Distinto de 0: en construcción.
	uint8_t param = 0;  // Dirección u orientación (bits 7..5), según el tipo.
};
static_assert(sizeof(BuildingRecord) == 8, "mismo formato que en el juego ([0x75C] de la ciudad)");

// Nodo de la lista [0x9B4] de una ciudad (0x14 bytes; 0x50FD40 lo lee). Los
// 4 primeros bytes son el puntero al siguiente en el juego; aquí no se usan.
using TownNode = GameMemory<0x14>;

// Pares (x, y) de celdas ancla de los objetos fijos de la ciudad; 0xFF = no hay.
struct TownFixedPoints {
	std::array<uint8_t, 8> walls{};  // Cambian al construir; no se dibujan.
	std::array<uint8_t, 8> lighthouses{};
	std::array<uint8_t, 16> cranes{};
	std::array<uint8_t, 32> market_stalls{};
	std::array<uint8_t, 16> stall_models{};  // Mercancía de cada puesto.
	std::array<uint8_t, 8> sites{};  // Estatua, kontor del jugador, kontor rival, baño.
	std::array<uint8_t, 4> shipyards{};  // Astillero y astillero de reparación.
};

struct SavedTown {
	// El struct town del juego tal como queda en memoria al cargar la partida
	// (ver game_memory.hpp). Es lo que usa la simulación (town_tick.hpp) y de
	// donde salen los campos de abajo.
	TownMemory memory;
	std::vector<uint8_t> grid;  // Rejilla de la ciudad ([0x80C]: ancho [0x7A8] × alto [0x7AC]).
	std::vector<TownNode> nodes;  // Lista [0x9B4], en su orden.

	int town_id() const { return memory.u8(0x2C1); }  // 0..39, el orden de scripts/StadtDaten.ini.
	int template_map() const { return memory.u8(0x2C2); }  // Mapa iso/towns/<n> en el que se basa.
	uint32_t citizens() const { return memory.u32(0x2D4); }
	uint32_t situation() const { return memory.u32(0x2C8); }  // Asedio, incendio, hambruna, peste...
	// Almacén de la ciudad, en unidades internas (ver ware_scale). Producción y
	// consumos son diarios.
	uint32_t& stock(int ware) { return memory.u32(0x4 + 4 * size_t(ware)); }
	uint32_t stock(int ware) const { return memory.u32(0x4 + 4 * size_t(ware)); }
	uint32_t business_consumption(int ware) const { return memory.u32(0x64 + 4 * size_t(ware)); }
	uint32_t production(int ware) const { return memory.u32(0xC4 + 4 * size_t(ware)); }
	uint32_t citizen_consumption(int ware) const { return memory.u32(0x310 + 4 * size_t(ware)); }
	// Mercancía que viene de camino en barcos que han zarpado hacia la ciudad:
	// cada barco suma su carga al salir del puerto (0x5090E0) y la resta al
	// atracar (0x519C90). [0x370] baja de cero; [0x670] se queda en cero y el
	// juego lo recalcula de vez en cuando con los barcos que navegan (0x504970).
	int32_t& incoming(int ware) { return memory.s32(0x370 + 4 * size_t(ware)); }
	int32_t incoming(int ware) const { return memory.s32(0x370 + 4 * size_t(ware)); }
	int32_t& in_transit(int ware) { return memory.s32(0x670 + 4 * size_t(ware)); }
	int32_t in_transit(int ware) const { return memory.s32(0x670 + 4 * size_t(ware)); }
	// Umbrales de precio [0x4F0]. El juego los recalcula en cada tick de la
	// ciudad y al cargar la partida (ver thresholds.hpp).
	PriceThresholds price_thresholds(int ware) const {
		const size_t o = 0x4F0 + 16 * size_t(ware);
		return {memory.u32(o), memory.u32(o + 4), memory.u32(o + 8), memory.u32(o + 12)};
	}
	void set_price_thresholds(int ware, const PriceThresholds& t) {
		for (size_t i = 0; i < 4; ++i) {
			memory.u32(0x4F0 + 16 * size_t(ware) + 4 * i) = t[i];
		}
	}
	// Mercancía con recargo del 15 % al comprar; 0xFF = ninguna.
	uint8_t taxed_ware() const { return memory.u8(0x6F0); }
	// [0x76C]: qué objetos fijos existen (grúas, puestos, astilleros...) y si hay mercado.
	uint32_t flags() const { return memory.u32(0x76C); }
	uint8_t church_level() const { return memory.u8(0x7A3); }

	bool has_market() const { return flags() & 0x100; }

	MarketState market(int ware) const {
		MarketState state;
		state.stock = int32_t(stock(ware));
		state.thresholds = price_thresholds(ware);
		state.open = has_market();
		state.taxed = taxed_ware() == ware;
		return state;
	}
	std::vector<BuildingRecord> records;
	// Cabezas de las listas que dibuja el juego: edificios, murallas, torres y otra más.
	std::array<uint16_t, 4> drawn_lists{};
	TownFixedPoints fixed;
};

// Índice vacío en las listas enlazadas de la partida (oficinas, barcos...).
constexpr uint16_t kNoIndex = 0xFFFF;

// Oficina de un comerciante en una ciudad (el kontor), con su propio almacén.
struct SavedOffice {
	OfficeMemory memory;  // Como en el juego (0x44C bytes).

	uint16_t owner() const { return memory.u16(0x2C4); }  // Índice de comerciante; kNoIndex = oficina libre.
	uint8_t town() const { return memory.u8(0x2C6); }  // Índice en SaveGame::towns.
	uint16_t next_in_town() const { return memory.u16(0x2CA); }  // Siguiente oficina de la misma ciudad.
	uint32_t& stock(int ware) { return memory.u32(0x4 + 4 * size_t(ware)); }
	uint32_t stock(int ware) const { return memory.u32(0x4 + 4 * size_t(ware)); }

	bool used() const { return owner() != kNoIndex; }
};

struct SavedMerchant {
	MerchantMemory memory;  // Como en el juego (0x650 bytes).

	int32_t& money() { return memory.s32(0x0); }
	int32_t money() const { return memory.s32(0x0); }
	uint16_t kind() const { return memory.u16(0x8); }  // 0 = jugador humano; la IA usa 1, 5, 7 o 15.
	uint16_t first_office() const { return memory.u16(0xC); }
	uint16_t first_ship() const { return memory.u16(0xE); }  // Los demás siguen SavedShip::next_of_owner.
	uint8_t hometown() const { return memory.u8(0x19); }  // Índice en SaveGame::towns.
	bool male() const { return memory.u8(0x1A) != 0; }
	// Índices en las listas de nombres (ver merchant_name); los que pasan del
	// final de la lista son nombres escritos por el jugador.
	uint8_t first_name() const { return memory.u8(0x1C); }
	uint8_t family_name() const { return memory.u8(0x1D); }
	uint32_t banned_towns() const { return memory.u32(0x608); }  // Un bit por índice de ciudad: ahí no puede comerciar.
};

enum class ShipType : uint8_t { Snaikka, Craier, Cog, Hulk };

// Estados de un barco ([+0x134]) que reproduce p3vita (ver sailing.hpp).
enum ShipStatus : uint16_t {
	kShipDocked = 0x00,  // Atracado.
	kShipWaiting = 0x01,  // Esperando en puerto: ciudad bloqueada o barco de un convoy (cuenta [+0x138] atrás).
	kShipLeaving = 0x02,  // Saliendo del puerto (64 ticks).
	kShipArriving = 0x03,  // Entrando en el puerto (64 ticks).
	kShipToShipyard = 0x04,  // Va a la lista de reparación de su ciudad (pasa al 6).
	kShipFromShipyard = 0x05,  // Sale del astillero (vuelve al 0).
	kShipSailing = 0x0F,
	kShipSunk = 0x11,
	kShipPirate = 0x12,
};

// Ruta de un barco por el mar. Las rutas entre dos ciudades son las de la
// matriz del objeto de navegación y las comparten todos los barcos que las siguen.
using SeaRoute = std::vector<MapPoint>;

using ShipMemory = GameMemory<0x180>;  // struct ship

// Barco: el struct ship del juego tal como queda en memoria al cargar la
// partida (game_memory.hpp), con accesores para los campos conocidos. La ruta
// va aparte (`route`); en la memoria, [+0x24] solo dice si la tiene.
struct SavedShip {
	static constexpr uint16_t kFreeShipStatus = 0x17;

	ShipMemory memory;
	// [+0x24] Vacío: sin ruta. Se cambia con set_route, que deja en la memoria
	// [+0x24] a 1 o a 0 para el código que pregunta si el barco tiene ruta.
	std::shared_ptr<SeaRoute> route;
	// Al leer la partida: navega entre dos ciudades con la ruta de la matriz,
	// que asigna Sailing al cargar (0x516520 → 0x445010).
	bool town_route = false;

	// Un hueco libre, como lo deja el lector del juego (0x517A17).
	SavedShip() {
		status() = kFreeShipStatus;
		owner() = 0xFF;
		next_of_owner() = kNoIndex;
		next_in_list() = kNoIndex;
		memory.u16(0xA) = kNoIndex;
		memory.u16(0xC) = kNoIndex;
	}

	uint16_t& status() { return memory.u16(0x134); }  // ShipStatus; ver in_use().
	uint16_t status() const { return memory.u16(0x134); }
	uint8_t& owner() { return memory.u8(0x0); }  // Índice de comerciante.
	uint8_t owner() const { return memory.u8(0x0); }
	uint8_t& heading() { return memory.u8(0x1); }  // Rumbo en 16 pasos: 1 oeste, 5 norte, 9 este, 13 sur (0x517410).
	uint8_t heading() const { return memory.u8(0x1); }
	// [+0x2] Otro índice de la ruta; vuelve a 0 si el barco se aleja de su punto.
	uint16_t& route_mark() { return memory.u16(0x2); }
	uint16_t route_mark() const { return memory.u16(0x2); }
	uint16_t& next_of_owner() { return memory.u16(0x4); }
	uint16_t next_of_owner() const { return memory.u16(0x4); }
	// [+0x6] Siguiente en la lista de barcos en puerto o en el mar (o de huecos libres).
	uint16_t& next_in_list() { return memory.u16(0x6); }
	uint16_t next_in_list() const { return memory.u16(0x6); }
	uint16_t& convoy() { return memory.u16(0x8); }
	uint16_t convoy() const { return memory.u16(0x8); }
	uint8_t& type() { return memory.u8(0xE); }  // ShipType.
	uint8_t type() const { return memory.u8(0xE); }
	uint8_t& level() { return memory.u8(0xF); }
	uint8_t level() const { return memory.u8(0xF); }
	int32_t& capacity() { return memory.s32(0x10); }  // En unidades internas, como las mercancías.
	int32_t capacity() const { return memory.s32(0x10); }
	int32_t& max_health() { return memory.s32(0x14); }
	int32_t max_health() const { return memory.s32(0x14); }
	int32_t& health() { return memory.s32(0x18); }
	int32_t health() const { return memory.s32(0x18); }
	int32_t& x() { return memory.s32(0x1C); }  // Posición en el mapa del mar, en 16.16.
	int32_t x() const { return memory.s32(0x1C); }
	int32_t& y() { return memory.s32(0x20); }
	int32_t y() const { return memory.s32(0x20); }
	// Movimiento (docs/formats/navigation.md §4).
	int32_t& dx() { return memory.s32(0x28); }  // [+0x28], [+0x2C] Desplazamiento por tick, en 16.16.
	int32_t dx() const { return memory.s32(0x28); }
	int32_t& dy() { return memory.s32(0x2C); }
	int32_t dy() const { return memory.s32(0x2C); }
	int32_t& speed() { return memory.s32(0x30); }  // [+0x30] Velocidad que se muestra (0x516CC0).
	int32_t speed() const { return memory.s32(0x30); }
	uint16_t& route_index() { return memory.u16(0x34); }  // [+0x34] Punto de la ruta hacia el que va.
	uint16_t route_index() const { return memory.u16(0x34); }
	uint8_t& own_route() { return memory.u8(0x36); }  // [+0x36] La ruta es suya y no de la matriz.
	uint8_t own_route() const { return memory.u8(0x36); }
	uint8_t& origin() { return memory.u8(0x37); }  // Ciudad de la que salió.
	uint8_t origin() const { return memory.u8(0x37); }
	uint8_t& destination() { return memory.u8(0x38); }  // 0xFF si va a un punto del mar.
	uint8_t destination() const { return memory.u8(0x38); }
	uint8_t& town() { return memory.u8(0x39); }  // Ciudad en la que está atracado; 0xFF en el mar.
	uint8_t town() const { return memory.u8(0x39); }
	uint16_t& target() { return memory.u16(0x3A); }  // [+0x3A] Barco al que persigue.
	uint16_t target() const { return memory.u16(0x3A); }
	// [+0x3C] Avisos a la interfaz. El astillero pone 0x100 cada día que
	// trabaja en el barco, 0x200 si le sube el descanso y 0x20 al terminarlo.
	uint16_t& notices() { return memory.u16(0x3C); }
	uint16_t notices() const { return memory.u16(0x3C); }
	// [+0x3E] Baja navegando y se recupera en puerto; sin byte alto positivo no zarpa.
	uint16_t& readiness() { return memory.u16(0x3E); }
	uint16_t readiness() const { return memory.u16(0x3E); }
	uint16_t& sailors() { return memory.u16(0x40); }
	uint16_t sailors() const { return memory.u16(0x40); }
	uint16_t& captain() { return memory.u16(0x42); }
	uint16_t captain() const { return memory.u16(0x42); }
	uint32_t& moved_tick() { return memory.u32(0x44); }  // [+0x44] Tick en que zarpó o atracó.
	uint32_t moved_tick() const { return memory.u32(0x44); }
	uint32_t& arrival_tick() { return memory.u32(0x48); }  // [+0x48] Llegada prevista.
	uint32_t arrival_tick() const { return memory.u32(0x48); }
	uint32_t& built_tick() { return memory.u32(0x4C); }  // [+0x4C] Tick en que el astillero lo terminó (0x5083B0).
	uint32_t built_tick() const { return memory.u32(0x4C); }
	int32_t& cargo(int ware) { return memory.s32(0x54 + 4 * size_t(ware)); }
	int32_t cargo(int ware) const { return memory.s32(0x54 + 4 * size_t(ware)); }
	// Lo pagado por unidad interna.
	float& average_price(int ware) { return memory.f32(0xB4 + 4 * size_t(ware)); }
	float average_price(int ware) const { return memory.f32(0xB4 + 4 * size_t(ware)); }
	// Lo que el juego recalcula al cargar y después de cada compra o venta
	// (ver recalculate_cargo en trade.hpp).
	int32_t& used() { return memory.s32(0x118); }  // Bodega ocupada, redondeada a barriles.
	int32_t used() const { return memory.s32(0x118); }
	int32_t& equipment() { return memory.s32(0x11C); }  // Peso de artillería, armas y marineros de más.
	int32_t equipment() const { return memory.s32(0x11C); }
	// [+0x12C] Suma de la bodega ocupada al zarpar, en tanto por uno.
	float& load_factor_sum() { return memory.f32(0x12C); }
	float load_factor_sum() const { return memory.f32(0x12C); }
	uint16_t& voyages() { return memory.u16(0x130); }
	uint16_t voyages() const { return memory.u16(0x130); }
	uint8_t& orders() { return memory.u8(0x136); }  // [+0x136] Bit 0: ruta automática.
	uint8_t orders() const { return memory.u8(0x136); }
	uint16_t& timer() { return memory.u16(0x138); }  // [+0x138] Ticks de la maniobra de puerto.
	uint16_t timer() const { return memory.u16(0x138); }
	// Tipo de pieza en cada uno de los 24 huecos de artillería; >= 6 = vacío.
	uint8_t& artillery(int slot) { return memory.u8(0x13C + size_t(slot)); }
	uint8_t artillery(int slot) const { return memory.u8(0x13C + size_t(slot)); }
	uint16_t& hand_weapons() { return memory.u16(0x154); }
	uint16_t hand_weapons() const { return memory.u16(0x154); }
	uint8_t& extra_loads() { return memory.u8(0x156); }  // Cargas ocupadas que no son mercancía.
	uint8_t extra_loads() const { return memory.u8(0x156); }
	uint8_t& raid() { return memory.u8(0x15C); }  // [+0x15C] Ataque pirata a una ciudad.
	uint8_t raid() const { return memory.u8(0x15C); }
	// Nombre (Latin-1 en [+0x160], 32 bytes), en UTF-8.
	std::string name() const;

	void set_route(std::shared_ptr<SeaRoute> r) {
		route = std::move(r);
		memory.u32(0x24) = route ? 1 : 0;
	}

	bool in_use() const { return status() < kFreeShipStatus; }
	bool docked() const { return in_use() && town() != 0xFF && status() != 0x0F && status() != 0x12; }
};

// Lo que cuelga por punteros del objeto de asedio de una ciudad (0x633730).
// En SiegeMemory, los punteros [+0x8], [+0x101C] y [+0x1020] valen 1 si hay
// algo y 0 si no (en el juego, la dirección).
struct SiegeExtras {
	std::optional<SiegeArmyMemory> army;  // [+0x8]: el ejército que la sitia, si lo hay.
	// [+0x250] del ejército (0x62B2B0): entradas de 12 bytes (u8 [+0x0] su
	// número, u32 [+0x4] el reloj *0x6DCCF8, u32 [+0x8] registro de edificio),
	// todas las reservadas: quedan las [+0x242] primeras, pero el juego sigue
	// leyendo las demás. Vacío si no tiene.
	std::vector<GameMemory<0xC>> army_targets;
	// Listas [+0x101C] (nodos de 0xC bytes: 8 de datos y el siguiente en
	// [+0x8]; las crea 0x6356D0) y [+0x1020] (nodos de 0x14: 16 de datos y el
	// siguiente en [+0x10]): los datos de cada nodo, en el orden de la lista.
	std::vector<GameMemory<8>> list_101c;
	std::vector<GameMemory<0x10>> list_1020;
};

// What the game saves and p3vita reads without using it, kept so that
// write_savegame (savegame_write.hpp) writes it back as the game does (see
// docs/cloud/savegames.md). Filled by parse_savegame and load_game_scripts.
struct SaveOpaque {
	// The ten lists of u16 at the end of each merchant (the game keeps
	// pointers to them in [0x624]; their lengths are the 15 low bits of the u16
	// [0x60C + 2·k]).
	std::vector<std::array<std::vector<uint16_t>, 10>> merchant_lists;
	// Each pirate's map object ([+0x0], 0x4F9770: u32 width, u32 height, 0x5C
	// bytes and width × height cells) and its records of 8 bytes ([+0x4],
	// [+0x8] of them), as in the file. Empty if it has none. Only between
	// parse_savegame and load_pirate_nests, which moves them into
	// SaveGame::pirate_nests (live state).
	std::array<std::vector<uint8_t>, 5> pirate_maps;
	std::array<std::vector<uint8_t>, 5> pirate_records;
	// For each event of SaveGame::events, what follows the records: [+0x12]
	// entries of 4 bytes (type 0x1B) or the node list of type 0x34 (with
	// [+0x15] >= 7 and a list: 4 bytes a node, 0x7F before each next one and
	// 0 at the end).
	std::vector<std::vector<uint8_t>> event_extras;
	// The automatic trade routes of the player: the records of 0xDC bytes of
	// the object 0x6DD728 (0x4D4BC0; their number is the u16 [0x6DD72A] of
	// the globals, which the simulation reads).
	std::vector<GameMemory<0xDC>> trade_routes;
	// The battles (0x611AE0 on 0x6E55D0), as in the file.
	std::vector<uint8_t> battles;
	// The ship names written by the players that the save carries (the end of
	// its names section, 0x511E30), in Latin-1: they only last for the load
	// (savegame_load.hpp).
	std::vector<std::string> save_ship_names;
	// The 40 town names of scripts/NamenStaedte_eng.txt (0x5125E0), by town
	// number, in Latin-1 (the table 0x6DDA00). Read by load_game_scripts.
	std::vector<std::string> town_names;
	// The same file's 40 taverns' names after its 40 guilds' (the table
	// 0x6DD8C0, by town number after a load: 0x512730).
	std::vector<std::string> town_taverns;
};

// The statistics for the graphs of the game: the object 0x6DFC90, filled
// every day by 0x54DE90 and saved in the .pst (statistics.hpp). Its header
// (u16 [+0x0..+0xA] and the bytes [+0x20..+0x23]) is in the globals.
// A record of the towns: one block of 0x204 bytes for each town (0x54D1D0),
// as the game's {u32 number, data, next}.
using TownStatistics = std::vector<GameMemory<0x204>>;
// The object of 0x2310 bytes of a merchant (0x54CA20): u16 [+0x0] the
// merchant, u16 [+0x2], u8 [+0x4], u8 [+0x5] the current record of its rings
// of records of 0x70 bytes (32 days from [+0x8], 24 weeks from [+0xE08], 24
// months from [+0x1888]), u16 [+0x6] its years. In each record the game
// links the previous one by the pointer [+0x6C]; the rings never change, so
// here that u32 holds the offset of the previous record in the object, and
// [+0x2308] (the years) and [+0x230C] (the next object) are 0.
struct MerchantStatistics {
	GameMemory<0x2310> memory;
	std::vector<GameMemory<0x70>> years;  // The list [+0x2308], newest first (the link [+0x6C] at 0).
};
struct Statistics {
	// [+0xC] the days (32 kept), [+0x10] the weeks, [+0x14] the months (25
	// kept), [+0x18] the years: newest first.
	std::array<std::vector<TownStatistics>, 4> towns;
	std::vector<MerchantStatistics> merchants;  // [+0x1C], in the order of the list.
};

struct SaveGame {
	int version = 0;
	// Tiempo de juego en ticks: 256 por día desde el día 0 del año 0 (365 días
	// por año), en el objeto del mundo. La fecha sale de él (update_world_date
	// en sailing.hpp) y el juego la recalcula al empezar cada día.
	uint32_t& tick() { return globals.u32(global::kWorldTick); }
	uint32_t tick() const { return globals.u32(global::kWorldTick); }
	int day() const { return globals.u8(global::kWorldDay); }  // 1..31
	int month() const { return globals.u8(global::kWorldMonth); }  // 0 = enero.
	int year() const { return globals.u16(global::kWorldYear); }
	// Cambia lo que paga una ciudad cuando tiene poco almacén (ver sell_price).
	TradeDifficulty trade_difficulty = TradeDifficulty::Normal;
	int merchant_count = 0;
	std::vector<SavedTown> towns;
	std::vector<SavedOffice> offices;
	std::vector<SavedMerchant> merchants;
	std::vector<SavedShip> ships;
	// Listas de barcos que recorre la actualización de cada tick (0x506720),
	// enlazadas por SavedShip::next_in_list: en puerto y en el mar. Están en el
	// objeto de los barcos (0x6DD7A0), como los contadores.
	uint16_t& docked_head() { return globals.u16(global::kShips + 0xE8); }
	uint16_t docked_head() const { return globals.u16(global::kShips + 0xE8); }
	uint16_t& sailing_head() { return globals.u16(global::kShips + 0xEA); }
	uint16_t sailing_head() const { return globals.u16(global::kShips + 0xEA); }
	uint16_t captain_count() const { return globals.u16(global::kCaptainCount); }
	uint16_t convoy_count() const { return globals.u16(global::kShips + 0xF6); }
	int player = -1;  // Comerciante del jugador.
	// Nombres escritos por los jugadores (20 huecos tras cada lista del juego):
	// de hombre, de mujer y apellidos.
	std::array<std::vector<std::string>, 3> custom_names;

	// Memoria del juego que usa la simulación de las ciudades (town_tick.hpp),
	// con la misma disposición que en Patrician3.exe (game_memory.hpp).
	GlobalMemory globals;  // Mundo, objeto de los barcos, jugador...
	SettingsMemory settings;  // Ajustes de la partida (*0x6CC3E8).
	std::vector<CaptainMemory> captains;  // Capitanes y administradores (*0x6DD7A0).
	std::vector<GameMemory<0x14>> world_records;  // Registros de 0x14 bytes del mundo (*0x6DE510).
	std::vector<SiegeMemory> sieges;  // Uno por ciudad (*0x6DF270); vacío si no existen.
	std::vector<SiegeExtras> siege_extras;  // Lo que cuelga de cada uno de sieges (mismo índice).
	uint32_t rand_state = 1;  // Estado de rand() de MSVC (0x639C03).
	// Operaciones que la simulación ha encolado para el juego (0x54AA70): sus
	// 0x14 bytes (los que copia 0x543F10). Quien avanza el tiempo las recoge.
	std::vector<std::vector<uint8_t>> queued_operations;
	// Cola de eventos con fecha (*0x6DD73C): registros de 0x18 bytes con u32
	// tick [+0x0], u16 siguiente [+0x4] y u16 tipo [+0x6], enlazados en orden de
	// tick desde la cabeza u16 [0x6DD744]; u16 [0x6DD748] es su número.
	std::vector<GameMemory<0x18>> events;
	// Buzón de los avisos a los comerciantes (*0x6DD730, tantos como u16
	// [0x6DD736]): entradas de 16 bytes con u8 [+0x0] día, u8 [+0x1] mes, u16
	// [+0x2] año, u8 [+0x4] tipo y u16 [+0x6] siguiente; la lista de cada
	// comerciante empieza en su u16 [+0xA]. El evento 0x16 (0x4E8234) mira en
	// él los avisos 0x21 y 0x22 del jugador. New letters are added by
	// send_letter (0x4D6530, src/p3data/mail.cpp).
	std::vector<GameMemory<0x10>> mail;
	// The 16-byte heap record of each letter of types 0x3C..0x40 and 0x71
	// (the game keeps it apart, pointer in [+0x8]; for 0x71, [+0x4] is the
	// tick until which the letter is kept, 0x4D6780). Same size as `mail`;
	// empty for the other letters. In tick_probe, region L.
	std::vector<std::optional<GameMemory<0x10>>> mail_records;
	// The heap buffer of each letter (mail.hpp): the [+0xC] bytes at the
	// game's [+0x8] for the types 0x29, 0x47, 0x63 and 0x83 (the battle
	// report, the election, the expedition, the impounding); the record's
	// [+0x0] bytes at the game's [+0xC] for 0x3C..0x40 and 0x71. Same size as
	// `mail`; empty for the other letters. In tick_probe, region LB.
	std::vector<std::vector<uint8_t>> mail_buffers;
	// Not saved: the entries of the letters stored for the local player since
	// the viewer last looked (take_new_letters, mail.hpp), for its notice.
	std::vector<uint16_t> new_player_letters;
	// Not saved: the ticker lines sent to the local player since the viewer
	// last looked (0x548CA0: send_ticker, take_ticker in mail.hpp), the kind
	// and its argument; in tick_probe, region TK.
	struct TickerLine {
		uint8_t kind = 0;
		uint16_t e = 0;
		// Not 0: a line the game writes to its interface itself, without
		// 0x548CA0: the text (by its English address) with the town `e`.
		uint32_t text = 0;
		// Sent only while the game's interface exists (its window *0x6CC7E8):
		// the oracle, without it, never sends these.
		bool interface = false;
	};
	std::vector<TickerLine> player_ticker;
	// Not saved: the game's window for the battles (*0x6CBB10, 0x41D9B0: a battle's question and its end), which
	// the viewer is. Without it (`present` false: the checks, as the oracle without a window) a battle's question
	// takes the game's path of a player busy with another window (operation 0x96 with 1: the computer fights it).
	// In tick_probe, region WN (the oracle's stand-in window, TownOracle.use_battle_window).
	struct BattleWindow {
		bool present = false;
		bool open = false;  // its method +0xC8 ([+0x48])
		uint32_t result = 0;  // [+0x5A4]: 10 ([0x66BDC0]) closed with its button, 0 closed otherwise
		uint8_t battle = 0xFF;  // the slot of the battle whose description it shows
		uint16_t buttons = 0;  // the description's [+0x6]: 1 the question («Steer manually»), 0 the end
		std::string text;  // what it shows: the battle's [+0x1310] when it opened
		// The battle sounds' variants (0x443690): with any, each sound of the battle on the screen takes the
		// world's rand() (Sailing::battle_sound); 0 the sound off.
		uint32_t sound_variants = 0;
	};
	BattleWindow battle_window;
	// Matriz n × n del mundo (*[0x6DE50C], [+0x6C] de 0x6DE4A0; 0x531B70 la
	// lee): lo que se tarda de una ciudad a otra, en dieciseisavos de día a la
	// velocidad de una snaikka (0x516930; 0x516AB0 lo pasa a días).
	std::vector<uint8_t> town_travel;
	// The map templates (iso/towns/stadtplatz%.2u.tow, 256 by their number,
	// the missing ones empty; 0x4F9B40 reads them for a new town, 0..27, and a
	// pirate nest's map, 251..254): u32 width, u32 height, 0x5C bytes of
	// town_class1 and the grid. load_game_scripts loads them.
	std::vector<std::vector<uint8_t>> town_templates;
	// The town view (*0x6E51AC, the interface's), which the battles read: its
	// geometry as the game sets it at start (0x5871xx, from scripts/iso.ini
	// [LIGHTNINGVIEW0]; load_game_scripts, and without the file its values)
	// and for a town's map (0x589DE0: every iso/towns/*.town is 128 × 226),
	// its hit mask, and what it reads of a town's map file for a town battle.
	// The oracle gives the emulator the same (town_oracle.py TOWN_VIEW).
	struct TownView {
		int32_t tile[3] = {34, 21, 0};  // TileSize: [+0x510], [+0x514], [+0x518]
		int32_t down[2] = {17, 11};  // DownVector: [+0x4D0], [+0x4D4]
		uint32_t cells[2] = {128, 226};  // [+0xC314], [+0xC318]
		// The texture DetectBitmap (scripts/textures.ini) as 8-bit palette
		// indices, rows of `detect_pitch` bytes ([+0xC6D8], [+0xC6E8]; 0x5894F0):
		// which neighbour a point of a cell's box belongs to (0x58D030,
		// 0x58B740). Empty without the data.
		std::vector<uint8_t> detect;
		uint32_t detect_pitch = 0;
		// The two s32 at the end of iso/towns/<n>.town (after the 10 bytes of
		// header, the two layers and 8 bytes) for n = 0x80..0xFF, by n − 0x80:
		// what 0x58E760 reads for a town battle ([+0x1ED8], [+0x1EDC]) from the
		// map n = the town's (0x59B4B0) | 0x80. 0 if the file is missing.
		std::array<std::array<int32_t, 2>, 0x80> map_ends{};
	};
	TownView town_view;
	// Convoyes (*0x6DD7A8, tantos como [0x6DD896]): registros de 0x3C bytes
	// enlazados por el u16 [+0x8] en la lista de libres ([0x6DD884]) o en la
	// de los que existen ([0x6DD88C]). Ver ship_ai.cpp.
	std::vector<GameMemory<0x3C>> convoys;
	// Objeto 0x6DE588 (0x5351D0): 8 huecos, uno por jugador humano, con su
	// comerciante en el u8 [0x6DEF88 + i] (0xFF: libre) y un byte por ciudad
	// (40) en *[0x6DEF90 + 4·i]: 0 en las ciudades en las que tiene un barco
	// y, en las demás, 1 + el tiempo de viaje (en días) desde la más cercana
	// con 0, hasta 15 (0x535400). Vacío si el hueco no lo tiene.
	std::array<std::vector<uint8_t>, 8> player_towns;
	// Los 5 objetos de 0x18 bytes de los piratas en [+0xC] del objeto de los
	// barcos (*[0x6DD7AC + 4·i]; los lee 0x5138B0): u16 [+0xC] la cabeza de su
	// lista de barcos (por el [+0x4] de cada barco), u8 [+0xE] el número de
	// ciudad (StadtDaten.ini) de su guarida, u16 [+0x16] (el evento 0x1F mira
	// si pasa de 500). Los punteros [+0x0] (un objeto de 0x6C bytes) y [+0x4]
	// (u16 [+0x8] registros de 8 bytes) valen 1 si hay algo y 0 si no; lo que
	// apuntan no se guarda. Vacío si no existe; el puntero de los globales
	// queda a 1 si existe y a 0 si no. Los mira la cola de eventos (0x03,
	// 0x1F, 0x30, 0x32); en tick_probe, las regiones Z0..Z4.
	std::array<std::optional<GameMemory<0x18>>, 5> pirates;
	// What [+0x0] and [+0x4] of each pirate point to: the map of its nest and
	// its pool (battle/pirate_nests.hpp). Loaded by load_pirate_nests; in
	// tick_probe, the regions ZMi, ZDi and ZPi.
	std::array<PirateNest, 5> pirate_nests;
	// The battles (0x6E55D0 and its heap blocks; battle/battles.hpp). None in
	// the saves: the empty object. In tick_probe, region BT.
	BattleGraph battles;
	// Los encargos de los 15 objetos del mundo ([+0xB10] del mundo, 0x2C
	// bytes cada uno): la lista del montón de su [+0x24], con su número en el
	// u16 [+0x1C] y en el u32 [+0x28] del objeto. Registros de 16 bytes, en el
	// orden de la lista: u16 [+0x0] comerciante, u8 [+0x2] cuál de las dos
	// mercancías del objeto, u8 [+0x3] 0 si el objeto la vende y 1 si la
	// compra, u32 [+0x4] cantidad que falta (barriles), u32 [+0x8] precio,
	// u32 [+0xC] tick en que caduca. Los crea la llegada de una expedición
	// (0x52C6C0, evento 0x12) y los quita 0x52D7D0; en tick_probe, las
	// regiones F0..F14.
	std::array<std::vector<GameMemory<0x10>>, 15> world_object_lists;
	// Puntos (u16 x, u16 y) de scripts/Expeditionen.dat, que el juego lee al
	// crear el primer comerciante (*0x6DF2E0; su número, u32 [0x6DF2E4]). Los
	// usa 0x534F10 (convoys_port.cpp). Vacío si no se ha leído.
	std::vector<std::array<uint16_t, 2>> expeditions;
	// Nombres de barco (scripts/NamenSchiffe_eng.txt, [0x6DDB78] de ellos)
	// tal como los parte el juego (0x5128D0), en Latin-1: 0x509A50 copia uno
	// al [+0x160] de cada barco nuevo. Vacío si no se ha leído.
	std::vector<std::string> ship_names;
	// The ship names the players write during the game (the names object's
	// second list, [0x6DDB7A] − [0x6DDB78] of them, after the game's: 0x512D40
	// adds one, 0x512FC0 lengthens the last), in Latin-1. Empty after a load
	// (0x5121C0 drops the save's); the game writes them in the save (0x511C60).
	std::vector<std::string> player_ship_names;
	// Men's and women's first names (scripts/NamenMaenner.txt and
	// NamenFrauen.txt, [0x6DDB70] and [0x6DDB72] of them) as the game splits
	// them (0x5121C0 with 0x5128D0), in Latin-1: 0x512A60 gives one, and
	// 0x4F69B0 uses the length of a merchant's. Empty if not read.
	std::array<std::vector<std::string>, 2> first_names;
	// The family names (scripts/NamenFamilien.txt: [0x6DDB76] of them, the
	// families and then the pirates), split and kept in the same way: 0x512AE0
	// gives one. Empty if not read.
	std::vector<std::string> family_names;
	// What p3vita keeps only to write the save (savegame_write.hpp).
	SaveOpaque opaque;
	// The daily statistics (statistics.hpp) and whether advance_game_tick
	// records them (0x54DE90), as the game does: on since it is checked
	// (0 differences in the 12 saves, docs/cloud/statistics.md).
	Statistics statistics;
	bool record_statistics = true;
};

// Lee una partida ya descomprimida.
bool parse_savegame(const uint8_t* data, size_t size, SaveGame& out, std::string* error = nullptr);

// Lee una partida comprimida (un único blob LZ) del archivo, p. ej. "missav/1.pat".
// Llama también a load_game_scripts.
bool load_savegame(const CprArchive& archive, const std::string& path, SaveGame& out, std::string* error = nullptr);

// Lo que el juego lee de scripts/ al arrancar, deja en sus globales y usa la
// simulación (la cola de eventos): los números de nombres de cada lista del
// objeto de los nombres (0x6DDAA0; 0x5121C0 los lee de Namen_eng.ini): u16
// [0x6DDB70] hombres, [0x6DDB72] mujeres, [0x6DDB74] apellidos, [0x6DDB76]
// apellidos y piratas, [0x6DDB78] y [0x6DDB7A] barcos; y la tabla de las 40
// ciudades (0x6DDB90 + 0x34 · número de la ciudad; 0x51E440, de
// StadtDaten.ini), sin el CString de [+0x28]; y los puntos de
// scripts/Expeditionen.dat (SaveGame::expeditions y u32 [0x6DF2E4], 0x5346A0),
// y los nombres de barco (SaveGame::ship_names). Tras parse_savegame.
void load_game_scripts(const CprArchive& archive, SaveGame& save);

// Descomprime una partida del archivo sin interpretarla.
bool read_savegame_data(const CprArchive& archive, const std::string& path, std::vector<uint8_t>& out,
	std::string* error = nullptr);

// Listas de nombres del juego (scripts/Namen*.txt): de hombre, de mujer y apellidos.
using NameLists = std::array<std::vector<std::string>, 3>;
NameLists load_name_lists(const CprArchive& archive);

// "Nombre Apellido" del comerciante, como lo resuelve el juego (0x512A60 y
// 0x512AE0): índice en la lista de su sexo y, pasado su final, en los nombres
// escritos por el jugador que guarda la partida.
std::string merchant_name(const SaveGame& save, const NameLists& names, int merchant);

// 0x512A60 (on the names object 0x6DDAA0): a first name by index (bit 7, a
// woman's), in Latin-1 as the game holds it: from the game's list
// (SaveGame::first_names), then the 20 written by the players
// (SaveGame::custom_names), and past them the list's first name.
std::string game_first_name(const SaveGame& save, uint32_t index);
// 0x512AE0: a family name by index in the same way (SaveGame::family_names,
// then the players' third list).
std::string game_family_name(const SaveGame& save, uint32_t index);

}  // namespace p3::data
