#include "p3data/town_buildings.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>

namespace p3::data {

namespace {

constexpr int kFallbackRegion = 2;
constexpr size_t kMaxListWalk = 5000;

// Tablas copiadas de Patrician3.exe (GOG 1.1); ver docs/formats/savegame.md.
// 0x672FC0: tipo de edificio de la partida -> tipo del .let.
constexpr uint8_t kBuildingTileType[0x39] = {
	0x2c, 0x1b, 0x0b, 0x08, 0x5d, 0x57, 0x58, 0x5b, 0x59, 0x53, 0x56, 0x64, 0x5a, 0x5e, 0x60, 0x55,
	0x5c, 0x63, 0x62, 0x61, 0x54, 0x51, 0x51, 0x51, 0x50, 0x50, 0x50, 0x2a, 0x2a, 0x2a, 0x66, 0x04,
	0x03, 0x0b, 0x07, 0x00, 0x02, 0x05, 0x09, 0x33, 0x52, 0x65, 0x67, 0x68, 0x69, 0x37, 0x01, 0x5f,
	0x29, 0x29, 0x29, 0x29, 0x2f, 0x2f, 0x2e, 0x2f, 0x2f};
// 0x5214F4: caso del cálculo del parámetro; aquí solo importa el 7 (torres de bombardas).
constexpr uint8_t kParamCase[0x39] = {
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x04,
	0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x0a, 0x03,
	0x00, 0x07, 0x00, 0x07, 0x08, 0x08, 0x03, 0x09, 0x09};
// 0x672D28: estatua de cada ciudad (por id).
constexpr uint8_t kStatueByTown[40] = {
	0x13, 0x13, 0x08, 0x0f, 0x0f, 0x0e, 0x0e, 0x10, 0x07, 0x10, 0x07, 0x03, 0x03, 0x0a, 0x00, 0x06,
	0x0c, 0x0c, 0x0d, 0x0d, 0x0a, 0x0b, 0x04, 0x0b, 0x06, 0x08, 0x02, 0x05, 0x01, 0x00, 0x05, 0x11,
	0x11, 0x01, 0x02, 0x04, 0x09, 0x12, 0x12, 0x09};

// Tipos de .let.
constexpr uint32_t kTilePoorHouse = 0x2A;
constexpr uint32_t kTileMiddleHouse = 0x50;
constexpr uint32_t kTileRichHouse = 0x51;
constexpr uint32_t kTileChurch = 0x04;
constexpr uint32_t kTileTownHall = 0x03;
constexpr uint32_t kTileWall = 0x2E;
constexpr uint32_t kTileGate = 0x2F;
constexpr uint32_t kTileTower = 0x29;
constexpr uint32_t kTileMonument = 0x37;
constexpr uint32_t kTileStatue = 0x0A;
constexpr uint32_t kTilePier = 0x5F;
constexpr uint32_t kTileWarehouse = 0x66;
constexpr uint32_t kTileChapel = 0x69;
constexpr uint32_t kTileLighthouse = 0x15;
constexpr uint32_t kTileCrane = 0x16;
constexpr uint32_t kTileMarketStall = 0x06;

// Banderas de la ciudad que activan objetos fijos.
constexpr uint32_t kFlagMarketStalls = 0x100;
constexpr uint32_t kFlagRepairShipyard = 0x80;
constexpr uint32_t kFlagShipyard = 0x20000;

struct Placed {
	int x;
	int y;
	const TileDef* tile;
};

// Tiles por tipo, en el orden por bytes de sus nombres: cuando varios encajan,
// se elige el primero, como en la referencia validada.
class TileIndex {
public:
	explicit TileIndex(const TileSet& tiles) {
		for (const auto& [id, tile] : tiles) {
			by_type_[tile.type].push_back(&tile);
		}
		for (auto& [type, list] : by_type_) {
			std::sort(list.begin(), list.end(), [](const TileDef* a, const TileDef* b) { return a->name < b->name; });
		}
	}

	template <typename Pred>
	const TileDef* find(uint32_t type, Pred pred) const {
		auto it = by_type_.find(type);
		if (it != by_type_.end()) {
			for (const TileDef* tile : it->second) {
				if (pred(tile->params)) {
					return tile;
				}
			}
		}
		return nullptr;
	}

	// Como find, con el parámetro `region_param` igual a la región; si ningún
	// tile la tiene, se usa la región 2 (lo hace el juego).
	template <typename Pred>
	const TileDef* find_regional(uint32_t type, Pred pred, int region, size_t region_param) const {
		for (const int wanted : {region, kFallbackRegion}) {
			const TileDef* tile = find(type, [&](const std::vector<uint32_t>& p) {
				return p.size() > region_param && p[region_param] == uint32_t(wanted) && pred(p);
			});
			if (tile) {
				return tile;
			}
		}
		return nullptr;
	}

	const TileDef* by_name(const std::string& name) const {
		const std::string key = normalize_path(name);
		for (const auto& [type, list] : by_type_) {
			for (const TileDef* tile : list) {
				if (normalize_path(tile->name) == key) {
					return tile;
				}
			}
		}
		return nullptr;
	}

private:
	std::map<uint32_t, std::vector<const TileDef*>> by_type_;
};

bool has(const std::vector<uint32_t>& p, size_t index, uint32_t value) {
	return p.size() > index && p[index] == value;
}

bool in_map(const TownMap& town, int x, int y) {
	return x >= 0 && y >= 0 && x < town.width && y < town.height;
}

// Un paso en la dirección de los bits 7..5 (N, NE, E, SE, S, SO, O, NO).
void step(int& x, int& y, uint8_t direction) {
	switch (direction & 0xE0) {
	case 0x00: y -= 2; break;
	case 0x20: x += y & 1; y -= 1; break;
	case 0x40: x += 1; break;
	case 0x60: x += y & 1; y += 1; break;
	case 0x80: y += 2; break;
	case 0xA0: x += (y & 1) - 1; y += 1; break;
	case 0xC0: x -= 1; break;
	default: x += (y & 1) - 1; y -= 1; break;
	}
}

// Celdas de un tile de cols x rows con ancla delantera en (x, y), con su franja:
// la huella sube cols celdas a la izquierda y rows a la derecha; el interior, 0xFF.
struct FootprintCell {
	int x;
	int y;
	uint8_t slice;
};

std::vector<FootprintCell> footprint(int x, int y, int cols, int rows) {
	std::vector<FootprintCell> cells;
	for (int i = 0; i < cols; ++i) {
		for (int j = 0; j < rows; ++j) {
			int cx = x;
			int cy = y;
			for (int k = 0; k < i; ++k) {
				cx += (cy & 1) - 1;
				cy -= 1;
			}
			for (int k = 0; k < j; ++k) {
				cx += cy & 1;
				cy -= 1;
			}
			const int slice = j == 0 ? cols - 1 - i : i == 0 ? cols - 1 + j : 0xFF;
			cells.push_back({cx, cy, uint8_t(slice)});
		}
	}
	return cells;
}

void building_objects(const BuildingRecord& record, const SavedTown& town, int region, int merchant_count,
	const TileIndex& index, std::vector<Placed>& out, size_t& missing) {
	if (record.type >= sizeof(kBuildingTileType)) {
		missing++;
		return;
	}
	const uint32_t tile_type = kBuildingTileType[record.type];
	const uint32_t param = record.param;
	const TileDef* tile = nullptr;

	switch (tile_type) {
	case kTilePoorHouse:
	case kTileMiddleHouse:
	case kTileRichHouse: {
		// Variante (las tres de cada clase) y orientación; las casas en obras usan la 0.
		const uint32_t variant = record.pending ? 0 : uint32_t(record.type - 0x15) % 3;
		tile = index.find_regional(tile_type, [&](const auto& p) {
			return p.size() >= 4 && p[1] == variant && p[2] == param;
		}, region, 3);
		break;
	}
	case kTileChurch:
		tile = index.find_regional(tile_type, [&](const auto& p) { return has(p, 2, town.church_level); },
			region, 1);
		break;
	case kTileTownHall:
		tile = index.find_regional(tile_type, [](const auto&) { return true; }, region, 1);
		break;
	case kTileWall:
		tile = index.find_regional(tile_type, [&](const auto& p) { return has(p, 1, param); }, region, 2);
		break;
	case kTileGate: {
		// Tres piezas separadas dos pasos en la dirección de la puerta.
		int x = record.x;
		int y = record.y;
		for (uint32_t piece = 0; piece < 3; ++piece) {
			const TileDef* part = index.find_regional(tile_type, [&](const auto& p) {
				return p.size() >= 4 && p[1] == piece && p[2] == param;
			}, region, 3);
			if (part) {
				out.push_back({x, y, part});
			} else {
				missing++;
			}
			step(x, y, record.param);
			step(x, y, record.param);
		}
		return;
	}
	case kTileTower: {
		const uint32_t kind = kParamCase[record.type] == 7 ? 1 : 0;  // Cañones o bombardas.
		tile = index.find_regional(tile_type, [&](const auto& p) { return has(p, 2, kind); }, region, 1);
		break;
	}
	case kTileMonument:
		if (record.owner >= merchant_count) {
			// Supuesto: la estatua de la ciudad es el statue_* de su tabla.
			const uint32_t statue = town.town_id < 40 ? kStatueByTown[town.town_id] : 0;
			tile = index.find(kTileStatue, [&](const auto& p) { return has(p, 1, statue); });
		} else {
			tile = index.find(tile_type, [&](const auto& p) { return has(p, 1, param); });
		}
		break;
	case kTilePier:
	case kTileWarehouse:
		tile = index.find(tile_type, [&](const auto& p) { return has(p, 1, param); });
		break;
	case kTileChapel: {
		// Supuesto: el estilo sale de los nombres kapelle_*.
		static const uint32_t kChapelByRegion[5] = {2, 1, 3, 0, 4};
		const uint32_t wanted = region >= 0 && region < 5 ? kChapelByRegion[region] : 3;
		tile = index.find(tile_type, [&](const auto& p) { return has(p, 1, wanted); });
		break;
	}
	default:
		// Edificios únicos y empresas: el tile principal de su tipo.
		tile = index.find(tile_type, [](const auto& p) { return p.size() < 3 || p[2] == 0; });
		if (!tile) {
			tile = index.find(tile_type, [](const auto&) { return true; });
		}
		break;
	}

	if (tile) {
		out.push_back({record.x, record.y, tile});
	} else {
		missing++;
	}
}

void fixed_objects(const SavedTown& town, const TownMap& map, const TileIndex& index, const TownMap* editor,
	const TileSet& tiles, std::vector<Placed>& out) {
	const TownFixedPoints& f = town.fixed;
	// Los puntos sin uso valen 0xFF y caen fuera del mapa.
	auto valid = [&](uint8_t x, uint8_t y) { return in_map(map, x, y); };

	if (const TileDef* lighthouse = index.find(kTileLighthouse, [](const auto&) { return true; })) {
		for (size_t k = 0; k < f.lighthouses.size(); k += 2) {
			if (valid(f.lighthouses[k], f.lighthouses[k + 1])) {
				out.push_back({f.lighthouses[k], f.lighthouses[k + 1], lighthouse});
			}
		}
	}

	if (town.flags != 0) {
		// Supuesto: el modelo de cada grúa es el que tenga el .editor en esa celda.
		const TileDef* default_crane = index.by_name("8bitkomplett_kranlinks");
		for (size_t k = 0; k < f.cranes.size(); k += 2) {
			const int x = f.cranes[k];
			const int y = f.cranes[k + 1];
			if (!valid(f.cranes[k], f.cranes[k + 1])) {
				continue;
			}
			const TileDef* crane = default_crane;
			if (editor && in_map(*editor, x, y)) {
				auto it = tiles.find(editor->objects[size_t(y) * size_t(editor->width) + size_t(x)].tile);
				if (it != tiles.end() && it->second.type == kTileCrane) {
					crane = &it->second;
				}
			}
			if (crane) {
				out.push_back({x, y, crane});
			}
		}
	}

	if (town.flags & kFlagMarketStalls) {
		for (size_t k = 0; k < f.stall_models.size(); ++k) {
			const uint8_t x = f.market_stalls[2 * k];
			const uint8_t y = f.market_stalls[2 * k + 1];
			const uint32_t model = f.stall_models[k];
			const TileDef* stall = index.find(kTileMarketStall, [&](const auto& p) { return has(p, 1, model); });
			if (valid(x, y) && stall) {
				out.push_back({x, y, stall});
			}
		}
	}

	if (town.flags & kFlagRepairShipyard) {
		if (valid(f.shipyards[2], f.shipyards[3])) {
			if (const TileDef* repair = index.by_name("repwerft")) {
				out.push_back({f.shipyards[2], f.shipyards[3], repair});
			}
		}
		if ((town.flags & kFlagShipyard) && valid(f.shipyards[0], f.shipyards[1])) {
			if (const TileDef* shipyard = index.by_name("werft")) {
				out.push_back({f.shipyards[0], f.shipyards[1], shipyard});
			}
		}
	}
}

}  // namespace

std::vector<int> load_town_regions(const CprArchive& archive) {
	std::vector<int> regions(40, kFallbackRegion);
	const CprEntry* entry = archive.find("scripts/StadtDaten.ini");
	std::vector<uint8_t> data;
	if (!entry || !archive.read(*entry, data)) {
		return regions;
	}
	const std::string text(data.begin(), data.end());
	int town = -1;
	size_t pos = 0;
	while (pos < text.size()) {
		size_t end = text.find('\n', pos);
		if (end == std::string::npos) {
			end = text.size();
		}
		const std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		if (line.compare(0, 6, "[Stadt") == 0) {
			town = std::atoi(line.c_str() + 6);
		} else if (line.compare(0, 7, "Region=") == 0 && town >= 0 && town < int(regions.size())) {
			regions[size_t(town)] = std::atoi(line.c_str() + 7);
		}
	}
	return regions;
}

size_t place_saved_buildings(const SavedTown& saved, int merchant_count, int region, const TileSet& tiles,
	const TownMap* editor, TownMap& town) {
	const TileIndex index(tiles);
	std::vector<Placed> placed;
	size_t missing = 0;

	// Las listas que dibuja el juego, siguiendo el campo `next` de cada registro.
	for (const uint16_t head : saved.drawn_lists) {
		size_t walked = 0;
		for (size_t i = head; i < saved.records.size() && walked < kMaxListWalk; i = saved.records[i].next, ++walked) {
			building_objects(saved.records[i], saved, region, merchant_count, index, placed, missing);
		}
	}
	fixed_objects(saved, town, index, editor, tiles, placed);

	// Objetos naturales del mapa (árboles, rocas...), agrupados por huella desde
	// su celda delantera, para poder quitarlos enteros si un edificio los pisa.
	const size_t cells = size_t(town.width) * size_t(town.height);
	auto at = [&](int x, int y) { return size_t(y) * size_t(town.width) + size_t(x); };
	std::vector<int> natural_owner(cells, -1);
	std::vector<std::vector<size_t>> natural_cells;
	for (int y = town.height - 1; y >= 0; --y) {
		for (int x = 0; x < town.width; ++x) {
			const TownCell& cell = town.objects[at(x, y)];
			if (cell.tile == 0 || natural_owner[at(x, y)] >= 0) {
				continue;
			}
			std::vector<size_t> group = {at(x, y)};
			auto it = tiles.find(cell.tile);
			if (it != tiles.end() && it->second.cols * it->second.rows > 1 &&
				(cell.aux & 0xFF) == it->second.cols - 1) {
				group.clear();
				for (const FootprintCell& c : footprint(x, y, it->second.cols, it->second.rows)) {
					if (in_map(town, c.x, c.y) && town.objects[at(c.x, c.y)].tile == cell.tile &&
						natural_owner[at(c.x, c.y)] < 0) {
						group.push_back(at(c.x, c.y));
					}
				}
			}
			for (const size_t c : group) {
				natural_owner[c] = int(natural_cells.size());
			}
			natural_cells.push_back(std::move(group));
		}
	}

	// Cada celda se queda con el último objeto colocado que la pisa.
	std::vector<int> layer_owner(cells, -1);
	std::vector<bool> removed(natural_cells.size(), false);
	for (size_t i = 0; i < placed.size(); ++i) {
		for (const FootprintCell& c : footprint(placed[i].x, placed[i].y, placed[i].tile->cols, placed[i].tile->rows)) {
			if (!in_map(town, c.x, c.y)) {
				continue;
			}
			const int natural = natural_owner[at(c.x, c.y)];
			if (natural >= 0 && !removed[size_t(natural)]) {
				removed[size_t(natural)] = true;
				for (const size_t n : natural_cells[size_t(natural)]) {
					town.objects[n] = TownCell();
				}
			}
			layer_owner[at(c.x, c.y)] = int(i);
		}
	}
	for (size_t i = 0; i < placed.size(); ++i) {
		for (const FootprintCell& c : footprint(placed[i].x, placed[i].y, placed[i].tile->cols, placed[i].tile->rows)) {
			if (in_map(town, c.x, c.y) && layer_owner[at(c.x, c.y)] == int(i)) {
				town.objects[at(c.x, c.y)] = {placed[i].tile->id, c.slice};
			}
		}
	}
	return missing;
}

bool load_saved_town(const CprArchive& archive, const SaveGame& save, size_t index, const TileSet& tiles,
	const std::vector<int>& regions, TownMap& out, std::string* error) {
	if (index >= save.towns.size()) {
		if (error) {
			*error = "la partida no tiene la ciudad " + std::to_string(index);
		}
		return false;
	}
	const SavedTown& saved = save.towns[index];
	if (!load_town(archive, saved.template_map, out, error)) {
		return false;
	}
	TownMap editor;
	const bool has_editor = load_town(archive, saved.template_map, editor, nullptr, true);
	const int region = saved.town_id < int(regions.size()) ? regions[size_t(saved.town_id)] : kFallbackRegion;
	place_saved_buildings(saved, save.merchant_count, region, tiles, has_editor ? &editor : nullptr, out);
	return true;
}

}  // namespace p3::data
