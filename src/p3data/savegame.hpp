#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/prices.hpp"
#include "p3data/wares.hpp"

namespace p3::data {

// Partidas guardadas (.pat), ver docs/formats/savegame.md. De momento solo se
// lee lo necesario para dibujar las ciudades: sus edificios y objetos fijos.

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
	// Almacén de la ciudad, en unidades internas (ver ware_scale). Producción y
	// consumos son diarios.
	std::array<uint32_t, kWareCount> wares{};
	std::array<uint32_t, kWareCount> production{};
	std::array<uint32_t, kWareCount> business_consumption{};
	std::array<uint32_t, kWareCount> citizen_consumption{};
	// Umbrales de precio que el juego recalcula cada tick y guarda ya hechos.
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

struct SaveGame {
	int version = 0;
	int start_year = 0;
	int merchant_count = 0;
	std::vector<SavedTown> towns;
};

// Lee una partida ya descomprimida.
bool parse_savegame(const uint8_t* data, size_t size, SaveGame& out, std::string* error = nullptr);

// Lee una partida comprimida (un único blob LZ) del archivo, p. ej. "missav/1.pat".
bool load_savegame(const CprArchive& archive, const std::string& path, SaveGame& out, std::string* error = nullptr);

}  // namespace p3::data
