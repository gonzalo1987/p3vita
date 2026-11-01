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

}  // namespace p3::data
