// Loading and writing the heap blocks of the pirate objects (pirate_nests.hpp).

#include "p3data/battle/pirate_nests.hpp"

#include <algorithm>
#include <cstring>

#include "p3data/savegame.hpp"

namespace p3::data {

namespace {

// The map object's fields that the file holds: [+0x4] to [+0x68) (0x4F9770
// with version 0x7D or more; the pirates are only read from 0x7F on).
constexpr size_t kMapFieldsBegin = 0x4;
constexpr size_t kMapFieldsEnd = 0x68;

}  // namespace

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
