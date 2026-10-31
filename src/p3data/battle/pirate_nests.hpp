#pragma once

// The heap blocks of the 5 pirate objects (*[0x6DD7AC + 4·i], SaveGame::pirates):
// the map of the nest and the pool of 8-byte entries, kept as game memory (see
// docs/cloud/battles-2.md §1).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "p3data/game_memory.hpp"

namespace p3::data {

struct SaveGame;

struct PirateNest {
	// [+0x0] of the pirate: the map of its nest, a town_class1 object (0x4F95C0,
	// the same class as [0x7A4] of a town; 0x6C bytes): u32 [+0x0] 1, [+0x4]
	// width, [+0x8] height, the 0x5C bytes of [+0xC] (the anchors of
	// [0x7B0..0x80C) of a town) and [+0x68] the pointer to the cells, here 1
	// if there are cells and 0 if not. Empty if the pirate has no map.
	std::optional<GameMemory<0x6C>> map;
	// The cells (width × height; cell (x, y) is x · height + y), as a town's grid.
	std::vector<uint8_t> cells;
	// [+0x4] of the pirate: the pool, [+0x8] entries of 8 bytes, the free ones
	// linked from [+0xA] (0x513BF0 takes one, growing the pool by 0x20 when
	// none is free). Each entry: u8 [+0x0] x, u8 [+0x1] y, u8 [+0x2], u8
	// [+0x3] kind (0x24 entry 0, the nest; 0x1B the buildings planned by
	// 0x515DA0), u16 [+0x4] the next one (in the free list, or in the list
	// that starts at entry 0), u8 [+0x6] a countdown (0x514000 takes 4 a day)
	// and u8 [+0x7] the orientation. The pirate's [+0x4] is 1 if the pool
	// has entries and 0 if not.
	std::vector<GameMemory<8>> pool;
};

// Moves what parse_savegame keeps of each pirate (SaveOpaque::pirate_maps and
// pirate_records, the bytes of the file) into SaveGame::pirate_nests, as
// 0x5138B0 and 0x4F9770 load them.
void load_pirate_nests(SaveGame& save);

// What the file has after the 0x18-byte fields of pirate i (0x5137E0): a byte
// 0x7F and the map (0x4F9680), or 0, and the [+0x8] entries of the pool.
std::vector<uint8_t> pirate_nest_bytes(const SaveGame& save, size_t i);

// 0x513BF0 (on the pirate object): takes an entry of pirate k's pool (the
// head of the free ones, growing the pool by 0x20 when there is none) and
// returns its index. Entry 0 gets the kind 0x24; any other is appended to the
// list that starts at entry 0, with the kind 0x1B. x, y and [+0x2] become
// 0xFF, [+0x6] and [+0x7] 0.
size_t fn_513bf0(SaveGame& save, size_t k);

// 0x515DA0 (on the pirate object): plans a building of pirate k's nest, while
// it has fewer than [+0x16] / 200 − 2 (at most 0x20) and no convoy in
// [+0x10]: a new entry of kind 0x1B, its orientation 0x20 or 0xE0 by the
// tick, and, with a map, a place near entry 0 (0x4F9C80) where it is put
// (0x4FB9C0).
void fn_515da0(SaveGame& save, size_t k);

}  // namespace p3::data
