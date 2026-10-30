#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"
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
	int town_id = 0;  // 0..39, el orden de scripts/StadtDaten.ini.
	int template_map = 0;  // Mapa iso/towns/<n> en el que se basa.
	uint32_t citizens = 0;
	uint32_t situation = 0;  // Asedio, incendio, hambruna, peste... (campo 0x2C8).
	// Almacén de la ciudad, en unidades internas (ver ware_scale). Producción y
	// consumos son diarios.
	std::array<uint32_t, kWareCount> wares{};
	std::array<uint32_t, kWareCount> production{};
	std::array<uint32_t, kWareCount> business_consumption{};
	std::array<uint32_t, kWareCount> citizen_consumption{};
	// Mercancía que viene de camino en barcos que han zarpado hacia la ciudad:
	// cada barco suma su carga al salir del puerto (0x5090E0) y la resta al
	// atracar (0x519C90). [0x370] baja de cero; [0x670] se queda en cero y el
	// juego lo recalcula de vez en cuando con los barcos que navegan (0x504970).
	std::array<int32_t, kWareCount> incoming{};
	std::array<int32_t, kWareCount> in_transit{};
	// Array sin identificar de 0x490 que entra en los umbrales de precio.
	std::array<uint32_t, kWareCount> supply{};
	// Umbrales de precio. El juego los recalcula en cada tick y al cargar la
	// partida (ver thresholds.hpp); estos son ya los recalculados al cargar.
	std::array<PriceThresholds, kWareCount> price_thresholds{};
	uint8_t taxed_ware = 0xFF;  // Mercancía con recargo del 15 % al comprar; 0xFF = ninguna.

	bool has_market() const { return flags & 0x100; }

	MarketState market(int ware) const {
		MarketState state;
		state.stock = int32_t(wares[size_t(ware)]);
		state.thresholds = price_thresholds[size_t(ware)];
		state.open = has_market();
		state.taxed = taxed_ware == ware;
		return state;
	}
	uint32_t flags = 0;  // Qué objetos fijos existen: grúas, puestos, astilleros...
	uint8_t church_level = 0;
	std::vector<BuildingRecord> records;
	// Cabezas de las listas que dibuja el juego: edificios, murallas, torres y otra más.
	std::array<uint16_t, 4> drawn_lists{};
	TownFixedPoints fixed;
};

// Índice vacío en las listas enlazadas de la partida (oficinas, barcos...).
constexpr uint16_t kNoIndex = 0xFFFF;

// Oficina de un comerciante en una ciudad (el kontor), con su propio almacén.
struct SavedOffice {
	uint16_t owner = kNoIndex;  // Índice de comerciante; kNoIndex = oficina libre.
	uint8_t town = 0xFF;  // Índice en SaveGame::towns.
	uint16_t next_in_town = kNoIndex;  // Siguiente oficina de la misma ciudad.
	std::array<uint32_t, kWareCount> wares{};

	bool used() const { return owner != kNoIndex; }
};

struct SavedMerchant {
	int32_t money = 0;
	uint16_t kind = 0;  // 0 = jugador humano; la IA usa 1, 5, 7 o 15.
	uint16_t first_office = kNoIndex;
	uint16_t first_ship = kNoIndex;  // Los demás siguen SavedShip::next_of_owner.
	uint8_t hometown = 0;  // Índice en SaveGame::towns.
	bool male = true;
	// Índices en las listas de nombres (ver merchant_name); los que pasan del
	// final de la lista son nombres escritos por el jugador.
	uint8_t first_name = 0;
	uint8_t family_name = 0;
	uint32_t banned_towns = 0;  // Un bit por índice de ciudad: ahí no puede comerciar.
};

enum class ShipType : uint8_t { Snaikka, Craier, Cog, Hulk };

// Estados de un barco ([+0x134]) que reproduce p3vita (ver sailing.hpp).
enum ShipStatus : uint16_t {
	kShipDocked = 0x00,  // Atracado.
	kShipWaiting = 0x01,  // Esperando en puerto: ciudad bloqueada o barco de un convoy (cuenta [+0x138] atrás).
	kShipLeaving = 0x02,  // Saliendo del puerto (64 ticks).
	kShipArriving = 0x03,  // Entrando en el puerto (64 ticks).
	kShipSailing = 0x0F,
	kShipSunk = 0x11,
	kShipPirate = 0x12,
};

// Ruta de un barco por el mar. Las rutas entre dos ciudades son las de la
// matriz del objeto de navegación y las comparten todos los barcos que las siguen.
using SeaRoute = std::vector<MapPoint>;

struct SavedShip {
	static constexpr uint16_t kFreeShipStatus = 0x17;

	uint16_t status = kFreeShipStatus;  // ShipStatus; ver in_use().
	uint8_t owner = 0xFF;  // Índice de comerciante.
	uint8_t type = 0;  // ShipType.
	uint8_t level = 0;
	uint8_t heading = 0;  // Rumbo en 16 pasos: 1 oeste, 5 norte, 9 este, 13 sur (0x517410).
	uint16_t next_of_owner = kNoIndex;
	uint16_t next_in_list = kNoIndex;  // [+0x6] Siguiente en la lista de barcos en puerto o en el mar.
	uint16_t convoy = kNoIndex;  // [+0x8]
	int32_t capacity = 0;  // En unidades internas, como las mercancías.
	int32_t health = 0;
	int32_t max_health = 0;
	int32_t x = 0;  // Posición en el mapa del mar, en 16.16.
	int32_t y = 0;
	uint8_t origin = 0xFF;  // Ciudad de la que salió.
	uint8_t destination = 0xFF;  // 0xFF si va a un punto del mar.
	uint8_t town = 0xFF;  // Ciudad en la que está atracado; 0xFF en el mar.
	uint16_t sailors = 0;
	uint16_t captain = kNoIndex;  // [+0x42]

	// Movimiento (docs/formats/navigation.md §4).
	int32_t dx = 0;  // [+0x28], [+0x2C] Desplazamiento por tick, en 16.16.
	int32_t dy = 0;
	int32_t speed = 0;  // [+0x30] Velocidad que se muestra (0x516CC0).
	uint16_t route_index = 0;  // [+0x34] Punto de la ruta hacia el que va.
	uint16_t route_mark = 0;  // [+0x2] Otro índice de la ruta; vuelve a 0 si el barco se aleja de su punto.
	std::shared_ptr<SeaRoute> route;  // [+0x24] Vacío: sin ruta.
	bool own_route = false;  // [+0x36] La ruta es suya y no de la matriz.
	// Al leer la partida: navega entre dos ciudades con la ruta de la matriz,
	// que asigna Sailing al cargar (0x516520 → 0x445010).
	bool town_route = false;
	uint16_t target = kNoIndex;  // [+0x3A] Barco al que persigue.
	uint16_t readiness = 0;  // [+0x3E] Baja navegando y se recupera en puerto; sin byte alto positivo no zarpa.
	uint32_t moved_tick = 0;  // [+0x44] Tick en que zarpó o atracó.
	uint32_t arrival_tick = 0;  // [+0x48] Llegada prevista.
	uint16_t timer = 0;  // [+0x138] Ticks de la maniobra de puerto.
	uint8_t orders = 0;  // [+0x136] Bit 0: ruta automática.
	bool raid = false;  // [+0x15C] Ataque pirata a una ciudad.
	float load_factor_sum = 0;  // [+0x12C] Suma de la bodega ocupada al zarpar, en tanto por uno.
	uint16_t voyages = 0;  // [+0x130]

	std::array<int32_t, kWareCount> cargo{};
	std::array<float, kWareCount> average_price{};  // Lo pagado por unidad interna.
	std::array<uint8_t, 24> artillery{};  // Tipo de pieza en cada hueco; >= 6 = vacío.
	uint16_t hand_weapons = 0;
	uint8_t extra_loads = 0;  // Cargas ocupadas que no son mercancía.
	std::string name;
	// Lo que el juego recalcula al cargar y después de cada compra o venta
	// (ver recalculate_cargo en trade.hpp).
	int32_t used = 0;  // Bodega ocupada, redondeada a barriles.
	int32_t equipment = 0;  // Peso de artillería, armas y marineros de más.

	bool in_use() const { return status < kFreeShipStatus; }
	bool docked() const { return in_use() && town != 0xFF && status != 0x0F && status != 0x12; }
};

struct SaveGame {
	int version = 0;
	// Tiempo de juego en ticks: 256 por día desde el día 0 del año 0 (365 días
	// por año). La fecha sale de él (date_from_tick en sailing.hpp) y el juego
	// la recalcula al empezar cada día.
	uint32_t tick = 0;
	int day = 1;  // 1..31
	int month = 0;  // 0 = enero.
	int year = 0;
	// Cambia lo que paga una ciudad cuando tiene poco almacén (ver sell_price).
	TradeDifficulty trade_difficulty = TradeDifficulty::Normal;
	int merchant_count = 0;
	std::vector<SavedTown> towns;
	std::vector<SavedOffice> offices;
	std::vector<SavedMerchant> merchants;
	std::vector<SavedShip> ships;
	// Listas de barcos que recorre la actualización de cada tick (0x506720),
	// enlazadas por SavedShip::next_in_list: en puerto y en el mar.
	uint16_t docked_head = kNoIndex;  // [+0xE8] del objeto de barcos
	uint16_t sailing_head = kNoIndex;  // [+0xEA]
	uint16_t captain_count = 0;
	uint16_t convoy_count = 0;
	int player = -1;  // Comerciante del jugador.
	// Nombres escritos por los jugadores (20 huecos tras cada lista del juego):
	// de hombre, de mujer y apellidos.
	std::array<std::vector<std::string>, 3> custom_names;
};

// Lee una partida ya descomprimida.
bool parse_savegame(const uint8_t* data, size_t size, SaveGame& out, std::string* error = nullptr);

// Lee una partida comprimida (un único blob LZ) del archivo, p. ej. "missav/1.pat".
bool load_savegame(const CprArchive& archive, const std::string& path, SaveGame& out, std::string* error = nullptr);

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

}  // namespace p3::data
