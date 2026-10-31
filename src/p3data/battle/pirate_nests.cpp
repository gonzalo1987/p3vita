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

}  // namespace p3::data
