// Loading and writing the heap blocks of the pirate objects (pirate_nests.hpp).

#include "p3data/battle/pirate_nests.hpp"

#include <algorithm>
#include <cstring>

#include "p3data/savegame.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick::construction_detail {
// 0x4F9C80 (town_tick/construction.cpp): a place for a building on a town's grid.
bool fn_4f9c80(const SaveGame& save, int town, uint8_t& x, uint8_t& y, uint32_t type, uint8_t param, bool flag);
}  // namespace p3::data::tick::construction_detail

namespace p3::data {

namespace {

// The map object's fields that the file holds: [+0x4] to [+0x68) (0x4F9770
// with version 0x7D or more; the pirates are only read from 0x7F on).
constexpr size_t kMapFieldsBegin = 0x4;
constexpr size_t kMapFieldsEnd = 0x68;

constexpr uint32_t kTick = 0x6DE4B4;  // u32
constexpr uint32_t kWorld4 = 0x6DE4A4;  // u8, the low byte of [+0x4] of the world
constexpr uint16_t kBuildingWorth = 200;  // 0x672A2E (u16)
constexpr uint8_t kKindNest = 0x24;
constexpr uint8_t kKindBuilding = 0x1B;
constexpr uint8_t kBuildingType = 0x2A;
// 0x677929 and 0x67792F (u8): the orientation of a building, by the tick.
constexpr uint8_t kOrientation[2] = {0xE0, 0x20};

// The grid functions of the towns (town_tick/grid.cpp, construction.cpp)
// work on [0x7A4..0x810) of a town and its grid, which is the same class and
// layout as the nest's map. They are run on the nest's map by putting it,
// for the call, in the place of town 0's and back again.
template <class F>
auto on_nest_map(SaveGame& save, PirateNest& nest, F f) {
	constexpr size_t kAt = 0x7A4;
	SavedTown& town = save.towns[0];
	std::array<uint8_t, 0x6C> kept;
	std::memcpy(kept.data(), town.memory.bytes.data() + kAt, kept.size());
	std::memcpy(town.memory.bytes.data() + kAt, nest.map->bytes.data(), kept.size());
	std::swap(town.grid, nest.cells);
	const auto result = f(0);
	std::swap(town.grid, nest.cells);
	std::memcpy(nest.map->bytes.data(), town.memory.bytes.data() + kAt, kept.size());
	std::memcpy(town.memory.bytes.data() + kAt, kept.data(), kept.size());
	return result;
}

}  // namespace

size_t fn_513bf0(SaveGame& save, size_t k) {
	GameMemory<0x18>& p = *save.pirates[k];
	std::vector<GameMemory<8>>& pool = save.pirate_nests[k].pool;
	uint16_t taken = p.u16(0xA);
	if (taken == p.u16(0x8)) {
		p.u16(0x8) = uint16_t(p.u16(0x8) + 0x20);
		pool.resize(p.u16(0x8));
		for (uint32_t i = taken; i < p.u16(0x8); ++i) {
			pool[i].bytes.fill(0xFF);
			pool[i].u16(0x4) = uint16_t(i + 1);
		}
		p.u32(0x4) = 1;
	}
	GameMemory<8>& e = pool[taken];
	p.u16(0xA) = e.u16(0x4);
	e.u16(0x4) = 0xFFFF;
	if (taken > 0) {
		const uint16_t size = p.u16(0x8);
		uint16_t last = 0;
		for (uint16_t i = pool[0].u16(0x4); i < size; i = pool[i].u16(0x4)) {
			last = i;
		}
		pool[last].u16(0x4) = taken;
		e.u8(0x3) = kKindBuilding;
	} else {
		e.u8(0x3) = kKindNest;
	}
	e.u8(0x7) = 0;
	e.u8(0x0) = 0xFF;
	e.u8(0x1) = 0xFF;
	e.u8(0x2) = 0xFF;
	e.u8(0x6) = 0;
	return taken;
}

// The game calls 0x4F9C80 with the orientation as the type and the building
// type 0x2A as the orientation, the other way round from 0x4FB9C0 (see «Game
// bugs» in docs/cloud/battles-2.md): reproduced as it is.
void fn_515da0(SaveGame& save, size_t k) {
	GameMemory<0x18>& p = *save.pirates[k];
	const GlobalMemory& g = save.globals;
	int32_t left = int32_t(p.u16(0x16)) / int32_t(kBuildingWorth) - 2;
	if (left > 0x20) {
		left = 0x20;
	}
	if (p.u16(0x10) < save.convoy_count()) {
		return;
	}
	PirateNest& nest = save.pirate_nests[k];
	const uint16_t size = p.u16(0x8);
	for (uint32_t i = 0; i < size; i = nest.pool[i].u16(0x4)) {
		if (nest.pool[i].u8(0x3) == kKindBuilding && --left <= 0) {
			return;
		}
	}
	const size_t index = fn_513bf0(save, k);
	GameMemory<8>& e = nest.pool[index];
	e.u8(0x3) = kKindBuilding;
	e.u8(0x7) = kOrientation[g.u32(kTick) & 1];
	if (!nest.map) {
		e.u8(0x0) = 0xFF;
		e.u8(0x1) = 0xFF;
		return;
	}
	e.u8(0x0) = uint8_t(nest.pool[0].u8(0x0) - (g.u32(kTick) & 3) + 0xF);
	e.u8(0x1) = uint8_t(nest.pool[0].u8(0x1) - (g.u8(kWorld4) & 3) + 0xF);
	on_nest_map(save, nest, [&](int town) {
		uint8_t x = e.u8(0x0), y = e.u8(0x1);
		const bool found = tick::construction_detail::fn_4f9c80(save, town, x, y, e.u8(0x7), kBuildingType, false);
		e.u8(0x0) = x;
		e.u8(0x1) = y;
		if (found) {
			tick::fn_4fb9c0(save, town, x, y, kBuildingType, e.u8(0x7));
		}
		return found;
	});
}

void load_pirate_nests(SaveGame& save) {
	for (size_t i = 0; i < save.pirate_nests.size(); ++i) {
		PirateNest& nest = save.pirate_nests[i];
		nest = PirateNest{};
		std::vector<uint8_t>& map = save.opaque.pirate_maps[i];
		if (!map.empty()) {
			GameMemory<0x6C>& m = nest.map.emplace();
			m.u32(0x0) = 1;
			const size_t fields = kMapFieldsEnd - kMapFieldsBegin;
			std::memcpy(m.bytes.data() + kMapFieldsBegin, map.data(), std::min(fields, map.size()));
			if (map.size() > fields) {
				nest.cells.assign(map.begin() + std::ptrdiff_t(fields), map.end());
			}
			m.u32(0x68) = nest.cells.empty() ? 0 : 1;
		}
		const std::vector<uint8_t>& records = save.opaque.pirate_records[i];
		nest.pool.resize(records.size() / 8);
		for (size_t k = 0; k < nest.pool.size(); ++k) {
			std::memcpy(nest.pool[k].bytes.data(), records.data() + 8 * k, 8);
		}
		map.clear();
		save.opaque.pirate_records[i].clear();
	}
}

std::vector<uint8_t> pirate_nest_bytes(const SaveGame& save, size_t i) {
	std::vector<uint8_t> out;
	const PirateNest& nest = save.pirate_nests[i];
	if (nest.map) {
		out.push_back(0x7F);
		out.insert(out.end(), nest.map->bytes.begin() + kMapFieldsBegin, nest.map->bytes.begin() + kMapFieldsEnd);
		out.insert(out.end(), nest.cells.begin(), nest.cells.end());
	} else {
		out.push_back(0);
	}
	const size_t count = save.pirates[i] ? save.pirates[i]->u16(0x8) : 0;
	for (size_t k = 0; k < count; ++k) {
		if (k < nest.pool.size()) {
			out.insert(out.end(), nest.pool[k].bytes.begin(), nest.pool[k].bytes.end());
		} else {
			out.insert(out.end(), 8, 0);
		}
	}
	return out;
}

void fn_514530_map(SaveGame& save, size_t k) {
	GameMemory<0x18>& p = *save.pirates[k];
	if (p.u32(0x0) != 0) {
		return;
	}
	PirateNest& nest = save.pirate_nests[k];
	// 0x4F95C0 (the ctor: [+0x4], [+0x8], [+0x68] 0, the anchors 0xFF) and
	// 0x4F9B40 (the template iso/towns/stadtplatz%.2u.tow by the number; the
	// four of the nests, 251..254, are in the game's data: without one the
	// game makes an empty map, 0x4F99B0).
	GameMemory<0x6C>& m = nest.map.emplace();
	m.bytes.fill(0);
	nest.cells.clear();
	const uint32_t number = 0xFEu - (p.u8(0xF) & 3u);
	const std::vector<uint8_t>* file = number < save.town_templates.size() ? &save.town_templates[number] : nullptr;
	if (file && file->size() >= 0x6C) {
		m.u32(0x0) = 1;
		std::memcpy(m.bytes.data() + kMapFieldsBegin, file->data(), kMapFieldsEnd - kMapFieldsBegin);
		const int64_t cells = int64_t(int32_t(m.u32(0x4))) * int32_t(m.u32(0x8));
		const size_t at = kMapFieldsEnd - kMapFieldsBegin;
		if (cells > 0 && size_t(cells) <= file->size() - at) {
			nest.cells.assign(file->begin() + std::ptrdiff_t(at), file->begin() + std::ptrdiff_t(at + size_t(cells)));
		}
		m.u32(0x68) = nest.cells.empty() ? 0 : 1;
	}
	p.u32(0x0) = 1;
	const uint32_t tick = save.globals.u32(kTick);
	std::vector<GameMemory<8>>& pool = nest.pool;
	for (uint32_t i = 0; i < p.u16(0x8) && i < pool.size();) {
		GameMemory<8>& e = pool[i];
		e.u8(0x6) = 0;
		const uint8_t kind = e.u8(0x3);
		// Each one is looked for a place round its cell (0x4F9C80, which leaves
		// the cell it ends at) and put there.
		auto place = [&](uint32_t type, uint8_t param) {
			return on_nest_map(save, nest, [&](int town) {
				uint8_t x = e.u8(0x0), y = e.u8(0x1);
				const bool found = tick::construction_detail::fn_4f9c80(save, town, x, y, type, param, false);
				e.u8(0x0) = x;
				e.u8(0x1) = y;
				if (found && type == 0x30) {
					tick::fn_4fbdb0(save, town, x, y);
				} else if (found) {
					tick::fn_4fb9c0(save, town, x, y, uint8_t(type), param);
				}
				return found;
			});
		};
		if (kind == kKindNest) {
			e.u8(0x7) = 0;
			place(kKindNest, 0);
			i = e.u16(0x4);
		} else if (kind == 0x30 || kind == 0x31) {
			if (place(0x30, 0)) {
				i = e.u16(0x4);
			} else {
				e.u8(0x3) = kKindBuilding;  // A tower with no room becomes a planned building (the same entry again).
			}
		} else if (kind == kKindBuilding) {
			e.u8(0x7) = kOrientation[(i & 1) ? 0 : 1];
			e.u8(0x0) = uint8_t(m.u8(0x5C) - ((tick + i) & 7) + 4);
			e.u8(0x1) = uint8_t(m.u8(0x5D) - ((uint32_t(int32_t(tick) >> 4) + i) & 7) + 4);
			const uint16_t next = e.u16(0x4);
			if (!place(kKindBuilding, e.u8(0x7))) {
				// No room: out of the list from entry 0, to the free one [+0xA].
				for (uint16_t* link = &pool[0].u16(0x4); *link < p.u16(0x8) && *link < pool.size();
					 link = &pool[*link].u16(0x4)) {
					if (*link == i) {
						*link = e.u16(0x4);
						e.u16(0x4) = p.u16(0xA);
						p.u16(0xA) = uint16_t(i);
						break;
					}
				}
			}
			i = next;
		} else {
			break;  // The game loops for ever on another kind. Unreachable: the lists have only these.
		}
	}
}

bool fn_515c00(SaveGame& save, size_t k, uint32_t x, uint32_t y) {
	// The game reads the pool without checking it (a nest's battle has one).
	std::vector<GameMemory<8>>& pool = save.pirate_nests[k].pool;
	const uint16_t count = save.pirates[k] ? save.pirates[k]->u16(0x8) : 0;
	if (pool.empty()) {
		return false;
	}
	for (uint16_t* link = &pool[0].u16(0x4); *link < count && *link < pool.size();) {
		GameMemory<8>& e = pool[*link];
		const uint8_t kind = e.u8(0x3);
		if (e.u8(0x0) == x && e.u8(0x1) == y && (kind == 0x30 || kind == 0x31)) {
			e.u8(0x6) = e.u8(0x6) > 0xC8 ? uint8_t(e.u8(0x6) + 1) : 0xC9;
			if (e.u8(0x6) < 0xFA) {
				return true;  // (0x5891C0, the fire's graphics when the view shows the nest.)
			}
			*link = e.u16(0x4);
			save.building_fallen(0xFE - k, x, y);  // (0x515CE9: the view's 0x599040.)
			return false;
		}
		link = &e.u16(0x4);
	}
	return false;
}

}  // namespace p3::data
