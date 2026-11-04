#pragma once

// The battles as game memory (docs/cloud/battles-2.md §3): the object of
// 0x6E55D0 and every heap block that hangs from it, as the oracle records
// them (region BT, town_oracle.py TownOracle.battle_graph).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace p3::data {

struct BattleGraph {
	static constexpr size_t kObjectSize = 0xC48;  // 0x6E55D0 up to its clock [+0xC44]
	static constexpr uint32_t kPointer = 0xF0000000;  // | block << 16 | offset
	static constexpr size_t kSlots = 255;
	static constexpr uint16_t kStopped = 0xFFFF;  // [+0x15C] of a battle that doesn't run

	// Block 0 is the battles object: u32 [+0x4·k] the battle of slot k (a
	// pointer), u8 [+0x3FE] set by operation 0x94, u8 [+0x404] the slots in
	// use, [+0xC30] and [+0xC34] the timers, [+0xC44] the clock. Then the
	// heap blocks reachable from it, in the order of a walk by offsets; a
	// pointer to one of them is kPointer | block << 16 | offset.
	std::vector<std::vector<uint8_t>> blocks{std::vector<uint8_t>(kObjectSize)};

	template <class T>
	T get(size_t block, size_t offset) const {
		T v{};
		if (block < blocks.size() && offset + sizeof(T) <= blocks[block].size()) {
			std::memcpy(&v, blocks[block].data() + offset, sizeof(T));
		}
		return v;
	}
	template <class T>
	void set(size_t block, size_t offset, T v) {
		if (block < blocks.size() && offset + sizeof(T) <= blocks[block].size()) {
			std::memcpy(blocks[block].data() + offset, &v, sizeof(T));
		}
	}

	// The words of a block that are numbers even when they look like a
	// pointer: in a scene's ship (0x164 bytes, vtable 0x67B0D4) its place and
	// speeds in 16.16 ([+0x8], [+0xC], [+0x14], [+0x18], [+0x1C]: x · 65536
	// can be the address of a heap block), in a sea battle (0x1E98 bytes,
	// vtable 0x67AD44) its generator [+0xADC]. The oracle's walk leaves them
	// as they are (town_oracle.py, BATTLE_NUMBERS).
	static bool number_field(const std::vector<uint8_t>& block, size_t offset) {
		uint32_t vtable = 0;
		if (block.size() >= 4) {
			std::memcpy(&vtable, block.data(), 4);
		}
		if (block.size() == 0x164 && vtable == 0x67B0D4) {
			return offset == 0x8 || offset == 0xC || offset == 0x14 || offset == 0x18 || offset == 0x1C;
		}
		if (block.size() == 0x1E98 && vtable == 0x67AD44) {
			return offset == 0xADC;
		}
		return false;
	}

	// The blocks of numbers only of a sea battle (a block of 0x1E98 bytes with
	// vtable 0x67AD44): the pages of its bits (the page lists [+0x48],
	// [+0x4C], [+0x50], 0x61A1D0), every word a mask of bits (bit 28 is the
	// heap's first block, 0x10000000); and the scene's arrays of bytes and
	// u16 [+0x18], [+0x1C], [+0x24] and the items' boxes [+0x28], [+0x2C],
	// [+0x30], [+0x38], [+0x3C], [+0x40] (x1, x2 and y1, y2 per item: two
	// coordinates such as 0x0CEA, 0x1028 make a word that looks like a heap
	// pointer). Numbers even when they look like pointers; the oracle's walk
	// leaves them as they are (town_oracle.py, pages in battle_graph).
	std::vector<bool> bit_pages() const {
		std::vector<bool> pages(blocks.size(), false);
		for (size_t b = 0; b < blocks.size(); ++b) {
			const std::vector<uint8_t>& data = blocks[b];
			if (data.size() != 0x1E98 || get<uint32_t>(b, 0) != 0x67AD44) {
				continue;
			}
			for (size_t o : {0x18, 0x1C, 0x24, 0x28, 0x2C, 0x30, 0x38, 0x3C, 0x40}) {
				const int array = ptr(b, o);
				if (array >= 0) {
					pages[size_t(array)] = true;
				}
			}
			for (size_t o : {0x48, 0x4C, 0x50}) {
				const int list = ptr(b, o);
				if (list < 0) {
					continue;
				}
				for (size_t w = 0; w + 4 <= blocks[size_t(list)].size(); w += 4) {
					const int page = ptr(size_t(list), w);
					if (page >= 0) {
						pages[size_t(page)] = true;
					}
				}
			}
		}
		return pages;
	}

	// A pointer to `offset` of block `block`, as the graph keeps it.
	static uint32_t pointer(size_t block, size_t offset = 0) {
		return kPointer | uint32_t(block) << 16 | uint32_t(offset);
	}
	// The block a word points to the start of, or −1.
	int target(uint32_t word) const {
		if ((word & 0xF0000000) != kPointer || (word & 0xFFFF) != 0) {
			return -1;
		}
		const size_t b = (word >> 16) & 0xFFF;
		return b < blocks.size() ? int(b) : -1;
	}
	// A new block of `size` bytes, all zero (the oracle's heap gives zeros;
	// the game's operator new 0x64F7B9 doesn't clear it). Its index.
	size_t alloc(size_t size) {
		blocks.emplace_back(size, 0);
		return blocks.size() - 1;
	}
	// The block the pointer at `offset` of `block` points to the start of, or −1.
	int ptr(size_t block, size_t offset) const { return target(get<uint32_t>(block, offset)); }
	void set_ptr(size_t block, size_t offset, int to) {
		set<uint32_t>(block, offset, to < 0 ? 0 : pointer(size_t(to)));
	}
	// realloc (0x63A29F) of the block the pointer at `offset` of `block`
	// points to: a new block (the oracle's sizes: n from no block, else n
	// rounded up to 16, as the CRT's HeapReAlloc), the old bytes copied and
	// the rest zero; the pointer updated. The new block's index.
	size_t realloc_at(size_t block, size_t offset, size_t n) {
		const int old = ptr(block, offset);
		const size_t size = old < 0 ? n : (n + 15) & ~size_t(15);
		const size_t b = alloc(size);
		if (old >= 0) {
			const size_t keep = std::min(size, blocks[size_t(old)].size());
			std::memcpy(blocks[b].data(), blocks[size_t(old)].data(), keep);
		}
		set_ptr(block, offset, int(b));
		return b;
	}

	// The same graph with its blocks in the oracle's order (a walk from block
	// 0 by offsets, following the pointers to a block's start); blocks no
	// pointer reaches are left out (freed or lost).
	BattleGraph normalized() const;

	uint8_t count() const { return get<uint8_t>(0, 0x404); }
	// The block of the battle in slot k, or −1.
	int battle(size_t k) const { return target(get<uint32_t>(0, 4 * k)); }
};

// The battle's generator ([+0xADC] of a sea battle, as 0x46C380 without the modulo).
inline uint32_t battle_rand(BattleGraph& g, size_t b) {
	uint32_t x = 0xA205B063u - g.get<uint32_t>(b, 0xADC) * 0x44BF19B3u;
	x = (x & 1) ? (x >> 1) | 0x80000000u : x >> 1;
	g.set<uint32_t>(b, 0xADC, x);
	return x;
}

// 0x611950: the battles' section of the save from the graph
// (battle/battle_save.cpp).
std::vector<uint8_t> battle_bytes(const BattleGraph& g);

// 0x611309 (on the sea battle of block b): ships join it (operation 0x9F):
// its music level [+0x1E94] goes down (battle/battle_end.cpp).
void fn_611309(BattleGraph& g, size_t b, uint16_t ships);

}  // namespace p3::data
