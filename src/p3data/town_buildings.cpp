#include "p3data/town_buildings.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>

#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

constexpr int kFallbackRegion = 2;
constexpr size_t kMaxListWalk = 5000;

// The record lists the game draws (0x521580), in its order: works in progress,
// the town's buildings, fortifications, then each office's in the town (its
// businesses [+0x2CE] and its houses and the others [+0x2D0], along the town's
// offices [0x784]: the merchants' buildings, the player's too), and what burns.
// [0x760] (free) and [0x766] (walls still to build) aren't drawn.
constexpr size_t kDrawnListHeads[] = {0x762, 0x764, 0x768};
constexpr size_t kBurningListHead = 0x76A;
// The fixed points (TownFixedPoints) in the town's memory, as the save has them.
constexpr size_t kFixedPoints = 0x7B0;
static_assert(sizeof(TownFixedPoints) == 0x5C, "the fixed points are [0x7B0..0x80B]");

TownFixedPoints current_fixed_points(const TownMemory& m) {
	TownFixedPoints f;
	size_t o = kFixedPoints;
	auto copy = [&](auto& points) {
		for (uint8_t& b : points) {
			b = m.u8(o++);
		}
	};
	copy(f.walls);
	copy(f.lighthouses);
	copy(f.cranes);
	copy(f.market_stalls);
	copy(f.stall_models);
	copy(f.sites);
	copy(f.shipyards);
	return f;
}

// A pirate nest's pool in the order 0x515B80 places it: from entry 0 along [+0x4] while below its count.
template <class Visit>
void walk_nest_entries(const PirateNest& nest, uint32_t count, Visit visit) {
	size_t walked = 0;
	for (uint32_t i = 0; count > 0 && i < count && i < nest.pool.size() && walked < nest.pool.size(); ++walked) {
		visit(nest.pool[i]);
		i = nest.pool[i].u16(0x4);
	}
}

// Calls visit(record) for each record of the drawn lists, in the game's order.
template <typename Visit>
void walk_drawn_records(const SavedTown& saved, const std::vector<uint16_t>& office_heads, Visit visit) {
	auto walk = [&](size_t first) {
		size_t walked = 0;
		for (size_t i = first; i < saved.records.size() && walked < kMaxListWalk; i = saved.records[i].next, ++walked) {
			visit(saved.records[i]);
		}
	};
	for (const size_t head : kDrawnListHeads) {
		walk(saved.memory.u16(head));
	}
	for (const uint16_t head : office_heads) {
		walk(head);
	}
	walk(saved.memory.u16(kBurningListHead));
}

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
constexpr uint32_t kTileRoadEdge = 0x17;  // strasse_rand*: a paved street's edges (its second parameter their kind)
constexpr uint32_t kTileTree = 0x14;

// Banderas de la ciudad que activan objetos fijos.
constexpr uint32_t kFlagMarketStalls = 0x100;
constexpr uint32_t kFlagRepairShipyard = 0x80;
constexpr uint32_t kFlagShipyard = 0x20000;

struct Placed {
	int x;
	int y;
	const TileDef* tile;
	uint8_t owner = kNoBuildingOwner;  // The record's owner (kRivalOwner: the rival kontor), for the owners' map.
	uint16_t site = kNoSite;  // Where its building site's progress is read (CellBuildings::sites).
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

// (`town` null: a pirate nest's, without a church or a statue. `site`: the record's index, kNoSite for a nest's;
// `burning`: in the town's burning list [0x76A].)
void building_objects(const BuildingRecord& record, uint16_t site, const SavedTown* town, int region,
	int merchant_count, const TileIndex& index, std::vector<Placed>& out, size_t& missing, bool burning = false) {
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
		// Variante (las tres de cada clase) y orientación; las casas en obras usan la 0. Burning, its own while its
		// life [+0x6] is above 44, then the 0 (0x54E5E0: the view's call with 44 or less makes it the variant 0).
		const uint32_t own = uint32_t(record.type - 0x15) % 3;
		const uint32_t variant = burning ? (record.pending > 0x2C ? own : 0) : record.pending ? 0 : own;
		tile = index.find_regional(tile_type, [&](const auto& p) {
			return p.size() >= 4 && p[1] == variant && p[2] == param;
		}, region, 3);
		break;
	}
	case kTileChurch:
		tile = index.find_regional(tile_type, [&](const auto& p) { return has(p, 2, town ? town->church_level() : 0); },
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
				out.push_back({x, y, part, record.owner, site});
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
			const uint32_t statue = town && town->town_id() < 40 ? kStatueByTown[town->town_id()] : 0;
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
		out.push_back({record.x, record.y, tile, record.owner, site});
	} else {
		missing++;
	}
}

// (`dx`, `dy`: where the editor's cells are, on a battle map: see place_saved_buildings.)
void fixed_objects(const SavedTown& town, const TownFixedPoints& f, const TownMap& map, const TileIndex& index,
	const TownMap* editor, const TileSet& tiles, std::vector<Placed>& out, int dx, int dy) {
	// Los puntos sin uso valen 0xFF y caen fuera del mapa.
	auto valid = [&](uint8_t x, uint8_t y) { return in_map(map, x, y); };

	if (const TileDef* lighthouse = index.find(kTileLighthouse, [](const auto&) { return true; })) {
		for (size_t k = 0; k < f.lighthouses.size(); k += 2) {
			if (valid(f.lighthouses[k], f.lighthouses[k + 1])) {
				out.push_back({f.lighthouses[k], f.lighthouses[k + 1], lighthouse});
			}
		}
	}

	if (town.flags() != 0) {
		// Supuesto: el modelo de cada grúa es el que tenga el .editor en esa celda.
		const TileDef* default_crane = index.by_name("8bitkomplett_kranlinks");
		for (size_t k = 0; k < f.cranes.size(); k += 2) {
			const int x = f.cranes[k];
			const int y = f.cranes[k + 1];
			if (!valid(f.cranes[k], f.cranes[k + 1])) {
				continue;
			}
			const TileDef* crane = default_crane;
			if (editor && in_map(*editor, x + dx, y + dy)) {
				auto it = tiles.find(editor->objects[size_t(y + dy) * size_t(editor->width) + size_t(x + dx)].tile);
				if (it != tiles.end() && it->second.type == kTileCrane) {
					crane = &it->second;
				}
			}
			if (crane) {
				out.push_back({x, y, crane});
			}
		}
	}

	if (town.flags() & kFlagMarketStalls) {
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

	if (town.flags() & kFlagRepairShipyard) {
		if (valid(f.shipyards[2], f.shipyards[3])) {
			if (const TileDef* repair = index.by_name("repwerft")) {
				out.push_back({f.shipyards[2], f.shipyards[3], repair});
			}
		}
		if ((town.flags() & kFlagShipyard) && valid(f.shipyards[0], f.shipyards[1])) {
			if (const TileDef* shipyard = index.by_name("werft")) {
				out.push_back({f.shipyards[0], f.shipyards[1], shipyard});
			}
		}
	}
}

// The kontors (0x521580, savegame.md §8.5): the player's (.let type 0x2C, the
// variant 0x4F74F0 gives: its params[1]) at the fixed point e[2..3] when one of
// the town's offices is his, a rival's (0x2D) at e[4..5] when one is another
// merchant's.
void kontor_objects(const TownFixedPoints& f, const TownMap& map, const TileIndex& index, const KontorView& kontors,
	std::vector<Placed>& out) {
	if (kontors.player_variant >= 0 && in_map(map, f.sites[2], f.sites[3])) {
		const uint32_t variant = uint32_t(kontors.player_variant);
		if (const TileDef* tile = index.find(0x2C, [&](const auto& p) { return has(p, 1, variant); })) {
			out.push_back({f.sites[2], f.sites[3], tile, kNoBuildingOwner, kKontorSite});  // (Its works: his office's.)
		}
	}
	if (kontors.rival && in_map(map, f.sites[4], f.sites[5])) {
		if (const TileDef* tile = index.find(0x2D, [](const auto&) { return true; })) {
			out.push_back({f.sites[4], f.sites[5], tile, kRivalOwner});
		}
	}
}

void put_placed(const TileSet& tiles, const std::vector<Placed>& placed, TownMap& town, CellBuildings* cells);
void pave_built_streets(const SavedTown& saved, const TileSet& tiles, TownMap& town, int dx, int dy);

}  // namespace

KontorView kontor_view(const SaveGame& save, size_t town) {
	KontorView view;
	if (town >= save.towns.size()) {
		return view;
	}
	const uint16_t offices = save.globals.u16(global::kWorldOfficeCount);
	size_t walked = 0;
	for (uint32_t i = save.towns[town].memory.u16(0x784); i < offices && i < save.offices.size() && walked < kMaxListWalk;
		 i = save.offices[i].memory.u16(0x2CA), ++walked) {
		const uint16_t owner = save.offices[i].memory.u16(0x2C4);
		if (save.player >= 0 && owner == save.player && size_t(owner) < save.merchants.size()) {
			// 0x4F74F0: the player's rank in his home town, 0..3, or 3 (4) and 4 (more).
			const MerchantMemory& m = save.merchants[owner].memory;
			const uint8_t rank = m.u8(0x39C + m.u8(0x19));
			view.player_variant = rank < 4 ? rank : (rank > 4 ? 4 : 3);
		} else if (size_t(owner) < save.merchants.size() && !(save.merchants[owner].memory.u8(0x8) & 4)) {
			view.rival = true;  // 0x521761: a merchant's office without the bit 4 of his [+0x8].
		}
	}
	return view;
}

std::vector<uint16_t> office_list_heads(const SaveGame& save, size_t town) {
	std::vector<uint16_t> heads;
	if (town >= save.towns.size()) {
		return heads;
	}
	const uint16_t offices = save.globals.u16(global::kWorldOfficeCount);
	size_t walked = 0;
	for (uint32_t i = save.towns[town].memory.u16(0x784); i < offices && i < save.offices.size() && walked < kMaxListWalk;
		 i = save.offices[i].memory.u16(0x2CA), ++walked) {
		heads.push_back(save.offices[i].memory.u16(0x2CE));
		heads.push_back(save.offices[i].memory.u16(0x2D0));
	}
	return heads;
}

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
	const TownMap* editor, TownMap& town, const KontorView* kontors, CellBuildings* cells,
	const std::vector<uint16_t>& office_heads, int dx, int dy) {
	const TileIndex index(tiles);
	std::vector<Placed> placed;
	size_t missing = 0;

	// Las listas que dibuja el juego, siguiendo el campo `next` de cada registro.
	std::vector<bool> burning(saved.records.size(), false);
	size_t walked = 0;
	for (size_t i = saved.memory.u16(kBurningListHead); i < saved.records.size() && walked < kMaxListWalk;
		 i = saved.records[i].next, ++walked) {
		burning[i] = true;
	}
	walk_drawn_records(saved, office_heads, [&](const BuildingRecord& record) {
		const size_t k = size_t(&record - saved.records.data());
		building_objects(record, uint16_t(k), &saved, region, merchant_count, index, placed, missing, burning[k]);
	});
	fixed_objects(saved, current_fixed_points(saved.memory), town, index, editor, tiles, placed, dx, dy);
	if (kontors) {
		kontor_objects(current_fixed_points(saved.memory), town, index, *kontors, placed);
	}
	for (Placed& p : placed) {
		p.x += dx;
		p.y += dy;
	}
	put_placed(tiles, placed, town, cells);
	pave_built_streets(saved, tiles, town, dx, dy);
	return missing;
}

namespace {

// Objetos naturales del mapa (árboles, rocas...), agrupados por huella desde
// su celda delantera, para poder quitarlos enteros si un edificio los pisa:
// each cell's group (−1 none) and each group's cells.
struct NaturalGroups {
	std::vector<int> owner;
	std::vector<std::vector<size_t>> cells;
};
NaturalGroups natural_groups(const TileSet& tiles, const TownMap& town) {
	NaturalGroups g;
	auto at = [&](int x, int y) { return size_t(y) * size_t(town.width) + size_t(x); };
	g.owner.assign(size_t(town.width) * size_t(town.height), -1);
	for (int y = town.height - 1; y >= 0; --y) {
		for (int x = 0; x < town.width; ++x) {
			const TownCell& cell = town.objects[at(x, y)];
			if (cell.tile == 0 || g.owner[at(x, y)] >= 0) {
				continue;
			}
			std::vector<size_t> group = {at(x, y)};
			auto it = tiles.find(cell.tile);
			if (it != tiles.end() && it->second.cols * it->second.rows > 1 &&
				(cell.aux & 0xFF) == it->second.cols - 1) {
				group.clear();
				for (const FootprintCell& c : footprint(x, y, it->second.cols, it->second.rows)) {
					if (in_map(town, c.x, c.y) && town.objects[at(c.x, c.y)].tile == cell.tile &&
						g.owner[at(c.x, c.y)] < 0) {
						group.push_back(at(c.x, c.y));
					}
				}
			}
			for (const size_t c : group) {
				g.owner[c] = int(g.cells.size());
			}
			g.cells.push_back(std::move(group));
		}
	}
	return g;
}

// What place_saved_buildings and place_nest_buildings have to place, put on the map: the natural objects it
// covers taken away whole, each cell to the last one placed on it.
void put_placed(const TileSet& tiles, const std::vector<Placed>& placed, TownMap& town, CellBuildings* out) {
	const size_t cells = size_t(town.width) * size_t(town.height);
	auto at = [&](int x, int y) { return size_t(y) * size_t(town.width) + size_t(x); };
	const NaturalGroups naturals = natural_groups(tiles, town);
	const std::vector<int>& natural_owner = naturals.owner;
	const std::vector<std::vector<size_t>>& natural_cells = naturals.cells;

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
	if (out) {
		out->owners.assign(cells, kNoBuildingOwner);
		out->sites.assign(cells, kNoSite);
	}
	for (size_t i = 0; i < placed.size(); ++i) {
		for (const FootprintCell& c : footprint(placed[i].x, placed[i].y, placed[i].tile->cols, placed[i].tile->rows)) {
			if (in_map(town, c.x, c.y) && layer_owner[at(c.x, c.y)] == int(i)) {
				town.objects[at(c.x, c.y)] = {placed[i].tile->id, c.slice};
				if (out) {
					out->owners[at(c.x, c.y)] = placed[i].owner;
					out->sites[at(c.x, c.y)] = placed[i].site;
				}
			}
		}
	}
}

// The town's built streets (0x5218A3, the end of 0x521580), after its buildings: each cell 0x81 of its grid (a street
// the town has paved: 0x4FB580; its [x · height + y], from the last column and row back) gets a road's edge (.let
// type 0x17) through 0x590680: 0x5928A0 picks of the class's tiles (by number, 0x586EB0) sorted by their second
// parameter (their [+0x40], 0x582E90; the swaps of 0x5898A9) one of the first kind's (0), the cell's (x + 1) · y + x
// modulo their count; 0x58DB40 puts it in the ground (0x58DF80, 0x58E180) and, unless the ground there was already a
// road's edge or of class 0x10, takes away the tree standing on it (0x58DC81: a whole tree, 0x58DF80 on the objects).
// `dx`, `dy` as place_saved_buildings's.
void pave_built_streets(const SavedTown& saved, const TileSet& tiles, TownMap& town, int dx, int dy) {
	const int32_t width = saved.memory.s32(0x7A8);
	const int32_t height = saved.memory.s32(0x7AC);
	if (width <= 0 || height <= 0 || saved.grid.size() < size_t(width) * size_t(height)) {
		return;
	}
	std::vector<const TileDef*> edges;
	for (const auto& [id, tile] : tiles) {
		if (tile.type == kTileRoadEdge) {
			edges.push_back(&tile);
		}
	}
	std::sort(edges.begin(), edges.end(), [](const TileDef* a, const TileDef* b) { return a->id < b->id; });
	auto kind = [](const TileDef* t) { return t->params.size() >= 2 ? uint8_t(t->params[1]) : uint8_t(0); };
	for (size_t i = 0; i < edges.size(); ++i) {
		for (size_t j = i + 1; j < edges.size(); ++j) {
			if (kind(edges[i]) > kind(edges[j])) {
				std::swap(edges[i], edges[j]);
			}
		}
	}
	size_t first_kind = 0;
	while (first_kind < edges.size() && kind(edges[first_kind]) == 0) {
		++first_kind;
	}
	if (first_kind == 0) {
		return;
	}
	auto at = [&](int x, int y) { return size_t(y) * size_t(town.width) + size_t(x); };
	NaturalGroups naturals;
	bool grouped = false;
	for (int32_t x = width - 1; x >= 0; --x) {
		for (int32_t y = height - 1; y >= 0; --y) {
			if (saved.grid[size_t(x) * size_t(height) + size_t(y)] != 0x81) {
				continue;
			}
			const int vx = x + dx;
			const int vy = y + dy;
			if (!in_map(town, vx, vy)) {
				continue;
			}
			const uint32_t pick = (uint32_t(x + 1) * uint32_t(y) + uint32_t(x)) % uint32_t(first_kind);
			TownCell& ground = town.ground[at(vx, vy)];
			const auto old = tiles.find(ground.tile);
			const uint32_t old_type = old != tiles.end() ? old->second.type : 0;
			ground = {edges[pick]->id, 0};
			if (old_type == 0x10 || old_type == kTileRoadEdge) {
				continue;
			}
			const TownCell& object = town.objects[at(vx, vy)];
			const auto tree = tiles.find(object.tile);
			if (object.tile == 0 || tree == tiles.end() || tree->second.type != kTileTree) {
				continue;
			}
			if (!grouped) {
				naturals = natural_groups(tiles, town);
				grouped = true;
			}
			const int group = naturals.owner[at(vx, vy)];
			if (group >= 0) {
				for (const size_t c : naturals.cells[size_t(group)]) {
					town.objects[c] = TownCell();
					naturals.owner[c] = -1;
				}
			}
		}
	}
}

}  // namespace

size_t place_nest_buildings(const PirateNest& nest, uint32_t count, int region, const TileSet& tiles,
	TownMap& town, CellBuildings* cells, int dx, int dy, const std::vector<bool>* kept, std::vector<bool>* drawn) {
	const TileIndex index(tiles);
	std::vector<Placed> placed;
	size_t missing = 0;
	std::vector<bool> now(nest.pool.size(), false);
	walk_nest_entries(nest, count, [&](const GameMemory<8>& e) {
		const size_t k = size_t(&e - nest.pool.data());
		const bool burning = e.u8(0x6) >= 0xC8 && e.u8(0x6) < 0xFA && kept && k < kept->size() && (*kept)[k];
		if (e.u8(0x6) >= 0xC8 && !burning) {
			return;  // Fallen (0x515BB0): not drawn.
		}
		now[k] = true;
		BuildingRecord record;
		record.x = e.u8(0x0);
		record.y = e.u8(0x1);
		record.type = e.u8(0x3);
		record.param = e.u8(0x7);
		record.owner = kRivalOwner;  // Not a merchant's.
		building_objects(record, uint16_t(kNestSite | k), nullptr, region, 0, index, placed, missing);
	});
	if (drawn) {
		*drawn = std::move(now);
	}
	for (Placed& p : placed) {
		p.x += dx;
		p.y += dy;
	}
	put_placed(tiles, placed, town, cells);
	return missing;
}

uint64_t nest_buildings_key(const PirateNest& nest, uint32_t count) {
	uint64_t key = 1469598103934665603ull;
	walk_nest_entries(nest, count, [&](const GameMemory<8>& e) {
		const uint32_t v = uint32_t(e.u8(0x0)) | uint32_t(e.u8(0x1)) << 8 | uint32_t(e.u8(0x3)) << 16 |
			uint32_t(e.u8(0x6) >= 0xC8) << 24 | uint32_t(e.u8(0x7)) << 25;
		key = (key ^ v) * 1099511628211ull;
	});
	return key;
}

bool load_town_ends(const CprArchive& archive, int id, int32_t& x, int32_t& y) {
	const CprEntry* entry = archive.find("iso/towns/" + std::to_string(id) + ".town");
	std::vector<uint8_t> data;
	if (!entry || !archive.read(*entry, data) || data.size() < 8) {
		return false;
	}
	std::memcpy(&x, data.data() + data.size() - 8, 4);
	std::memcpy(&y, data.data() + data.size() - 4, 4);
	return true;
}

bool load_nest_town_template(const CprArchive& archive, int map, int region, SavedTownTemplate& out, std::string* error) {
	if (!load_town(archive, map, out.town, error)) {
		return false;
	}
	out.has_editor = load_town(archive, map, out.editor, nullptr, true);
	out.region = region;
	return true;
}

uint64_t drawn_buildings_key(const SavedTown& saved, const std::vector<uint16_t>& office_heads) {
	// FNV-1a over what place_saved_buildings reads.
	uint64_t key = 0xCBF29CE484222325ull;
	auto add = [&](uint32_t value, int bytes) {
		for (int i = 0; i < bytes; ++i) {
			key = (key ^ ((value >> (8 * i)) & 0xFF)) * 0x100000001B3ull;
		}
	};
	add(uint32_t(saved.town_id()), 1);
	add(saved.flags(), 4);
	add(saved.church_level(), 1);
	add(saved.memory.u16(0x790), 2);  // The built streets' count: more when the town paves some (0x5244F0).
	for (size_t o = kFixedPoints; o < kFixedPoints + sizeof(TownFixedPoints); ++o) {
		add(saved.memory.u8(o), 1);
	}
	// The records in drawing order (which list each one is in doesn't change the drawing).
	walk_drawn_records(saved, office_heads, [&](const BuildingRecord& r) {
		add(uint32_t(r.x) | uint32_t(r.y) << 8 | uint32_t(r.owner) << 16 | uint32_t(r.type) << 24, 4);
		// (Its works or fire, and a burning house's life above 44: its variant, building_objects.)
		add(uint32_t(r.pending != 0) | uint32_t(r.pending > 0x2C) << 1 | uint32_t(r.param) << 8, 2);
	});
	return key;
}

namespace {

bool load_town_template(const CprArchive& archive, const SavedTown& saved, int map, const std::vector<int>& regions,
	SavedTownTemplate& out, std::string* error) {
	if (!load_town(archive, map, out.town, error)) {
		return false;
	}
	out.has_editor = load_town(archive, map, out.editor, nullptr, true);
	out.region = saved.town_id() < int(regions.size()) ? regions[size_t(saved.town_id())] : kFallbackRegion;
	return true;
}

}  // namespace

bool load_saved_town_template(const CprArchive& archive, const SavedTown& saved, const std::vector<int>& regions,
	SavedTownTemplate& out, std::string* error) {
	return load_town_template(archive, saved, saved.template_map(), regions, out, error);
}

bool load_battle_town_template(const CprArchive& archive, const SavedTown& saved, const std::vector<int>& regions,
	SavedTownTemplate& out, std::string* error) {
	return load_town_template(archive, saved, saved.template_map() | 0x80, regions, out, error);
}

void place_saved_town(const SavedTownTemplate& base, const SaveGame& save, size_t index, const TileSet& tiles,
	TownMap& out, CellBuildings* cells, int dx, int dy) {
	out = base.town;
	const KontorView kontors = kontor_view(save, index);
	place_saved_buildings(save.towns[index], save.merchant_count, base.region, tiles,
		base.has_editor ? &base.editor : nullptr, out, &kontors, cells, office_list_heads(save, index), dx, dy);
}

uint64_t drawn_buildings_key(const SaveGame& save, size_t index) {
	const KontorView kontors = kontor_view(save, index);
	return drawn_buildings_key(save.towns[index], office_list_heads(save, index)) * 31 +
		uint64_t(kontors.player_variant + 1) * 2 + (kontors.rival ? 1 : 0);
}

bool load_saved_town(const CprArchive& archive, const SaveGame& save, size_t index, const TileSet& tiles,
	const std::vector<int>& regions, TownMap& out, std::string* error) {
	if (index >= save.towns.size()) {
		if (error) {
			*error = "la partida no tiene la ciudad " + std::to_string(index);
		}
		return false;
	}
	SavedTownTemplate base;
	if (!load_saved_town_template(archive, save.towns[index], regions, base, error)) {
		return false;
	}
	place_saved_town(base, save, index, tiles, out);
	return true;
}

bool site_progress(const SaveGame& save, size_t town, uint16_t source, SiteProgress& out) {
	if (town >= save.towns.size()) {
		// A pirate nest's (0xFE − k) tower burning (0x515C00 → 0x5891C0 at 0x515CB6): its [+0x6] 0xC9 up to 0xF9, the
		// fire's life 0xFA less it, of 50; at 0xFA it leaves the pool's list.
		const size_t k = size_t(0xFE - town);
		if (town > 0xFE || k >= save.pirate_nests.size() || k >= save.pirates.size() || !save.pirates[k] ||
			!(source & kNestSite) || source == kNoSite || source == kKontorSite) {
			return false;
		}
		const size_t entry = source & ~kNestSite;
		bool found = false;
		walk_nest_entries(save.pirate_nests[k], save.pirates[k]->u16(0x8), [&](const GameMemory<8>& e) {
			if (size_t(&e - save.pirate_nests[k].pool.data()) != entry) {
				return;
			}
			const uint8_t fallen = e.u8(0x6);
			if (fallen > 0xC8 && fallen < 0xFA) {
				out = {uint16_t(0xFA - fallen), 50, true, true};
				found = true;
			}
		});
		return found;
	}
	const SavedTown& saved = save.towns[town];
	if (source == kKontorSite) {
		// 0x5216B5..0x52175A: the town's office ([0x784] along [+0x2CA]) of the player, its works' days left.
		constexpr uint16_t kKontorDays = 0x28;  // 0x672A48
		const uint16_t offices = save.globals.u16(global::kWorldOfficeCount);
		size_t walked = 0;
		for (uint32_t i = saved.memory.u16(0x784); i < offices && i < save.offices.size() && walked < kMaxListWalk;
			 i = save.offices[i].memory.u16(0x2CA), ++walked) {
			const auto& o = save.offices[i].memory;
			if (save.player >= 0 && o.u16(0x2C4) == uint16_t(save.player)) {
				const uint16_t left = o.u16(0x2F0);
				if (left == 0) {
					return false;
				}
				out = {uint16_t(kKontorDays - left), kKontorDays, false, true};
				return true;
			}
		}
		return false;
	}
	if (source >= saved.records.size()) {
		return false;
	}
	const BuildingRecord& r = saved.records[source];
	if (r.type == 0x28) {
		return false;
	}
	// 0x521465: in the burning list (0x52596A's walk of [0x76A] along [+0x4]) its fire, else its works.
	size_t walked = 0;
	for (size_t i = saved.memory.u16(kBurningListHead); i < saved.records.size() && walked < kMaxListWalk;
		 i = saved.records[i].next, ++walked) {
		if (i == source) {
			out = {r.pending, 50, true, r.pending != 0};
			return true;
		}
	}
	if (r.pending == 0) {
		return false;
	}
	const uint8_t days = tick::construction_detail::build_days(r.type);
	out = {uint16_t(days - r.pending), days, false, true};
	return true;
}

}  // namespace p3::data
