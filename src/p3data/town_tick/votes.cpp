#include "p3data/town_tick/votes.hpp"

#include <algorithm>
#include <cstring>

#include "p3data/game_memory.hpp"

namespace p3::data::tick {

namespace {

constexpr uint32_t kEvents = 0x6DD73C;
constexpr uint32_t kEventHead = kEvents + 0x8;  // u16
constexpr uint32_t kEventCount = kEvents + 0xC;  // u16

uint32_t merchant_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldMerchantCount);
}

// [+0x39C + home town] of the merchant: its rank at home.
uint32_t home_rank(const SaveGame& save, uint32_t merchant) {
	const MerchantMemory& m = save.merchants[merchant].memory;
	return m.u8(0x39C + m.u8(0x19));
}

// The merchant's number among the human players ([+0x8] = 0) before it.
uint32_t human_number(const SaveGame& save, uint32_t merchant) {
	uint32_t n = 0;
	for (uint32_t i = 0; i < merchant; ++i) {
		if (save.merchants[i].memory.u16(0x8) == 0) {
			++n;
		}
	}
	return n;
}

}  // namespace

uint16_t fn_4d9c70(const SaveGame& save) {
	const GlobalMemory& g = save.globals;
	const uint32_t today = g.u32(global::kWorldTick) & ~0xFFu;
	const uint32_t count = std::min<uint32_t>(g.u16(kEventCount), uint32_t(save.events.size()));
	size_t walked = 0;
	for (uint32_t i = g.u16(kEventHead); i < count && walked < count; i = save.events[i].u16(4), ++walked) {
		const GameMemory<0x18>& e = save.events[i];
		if (e.u32(0) - today > 0xFF) {
			return 0xFFFF;
		}
		if (e.u16(6) == 0x20 || e.u16(6) == 0x21) {
			return uint16_t(i);
		}
	}
	return 0xFFFF;
}

bool fn_4d9d40(const SaveGame& save) {
	const GlobalMemory& g = save.globals;
	const uint32_t player = g.u32(global::kPlayer);
	if (player >= merchant_count(save) || home_rank(save, player) < 4) {
		return false;
	}
	const uint32_t today = g.u32(global::kWorldTick) & ~0xFFu;
	const uint32_t count = std::min<uint32_t>(g.u16(kEventCount), uint32_t(save.events.size()));
	size_t walked = 0;
	for (uint32_t i = g.u16(kEventHead); i < count && walked < count; i = save.events[i].u16(4), ++walked) {
		const GameMemory<0x18>& e = save.events[i];
		if ((e.u32(0) & ~0xFFu) != today) {
			return false;
		}
		if (e.u16(6) == 0x20 || e.u16(6) == 0x21) {
			const uint8_t bit = uint8_t(1u << human_number(save, player));
			return (e.u8(e.u16(6) == 0x21 ? 0x16 : 0x14) & bit) == bit;
		}
	}
	return false;
}

void fn_53aa10(SaveGame& save, const uint8_t* operation) {
	uint32_t merchant;
	uint32_t choice;
	std::memcpy(&merchant, operation + 4, 4);
	std::memcpy(&choice, operation + 8, 4);
	if (merchant >= merchant_count(save)) {
		return;
	}
	const uint32_t rank = home_rank(save, merchant);
	if (rank < 4) {
		return;
	}
	const uint32_t index = fn_4d9c70(save);
	if (index >= save.globals.u16(kEventCount)) {
		return;
	}
	const uint32_t human = human_number(save, merchant);
	// The merchant's supporters: the towns' [0x6DC + j] that are it.
	int32_t supporters = 0;
	for (uint32_t t = 0; t < save.globals.u32(global::kWorldTownCount); ++t) {
		for (uint32_t j = 0; j < 4; ++j) {
			supporters += save.towns[t].memory.u8(0x6DC + j) == merchant;
		}
	}
	const uint8_t votes = rank < 6 ? 1 : rank == 6 ? 2 : 3;
	GameMemory<0x18>& e = save.events[index];
	const uint8_t bit = uint8_t(1u << human);
	if (e.u16(6) == 0x20) {
		if (!(e.u8(0x14) & bit)) {
			return;
		}
		e.u8(0x14) = uint8_t(e.u8(0x14) & ~bit);
		if (e.u8(0x16) < 1) {
			e.u8(0x16) = 1;
		}
		int32_t share = int32_t(e.u8(0x15)) * supporters / int32_t(e.u8(0x16));
		if (choice > 3) {
			return;
		}
		const int32_t cast = e.u8(0xC + choice);
		const int32_t to_give = e.u8(0x10 + choice);
		if (cast + to_give + share > 0xFF) {
			share = 0xFF - cast - to_give;
			if (share < 0) {
				share = 0;
			}
		}
		e.u8(0xC + choice) = uint8_t(cast + votes);
		e.u8(0x10 + choice) = uint8_t(to_give + share);
		return;
	}
	// The diet (event 0x21).
	if (!(e.u8(0x16) & bit)) {
		return;
	}
	e.u8(0x16) = uint8_t(e.u8(0x16) & ~bit);
	if (choice <= 1) {
		e.u8(0x12 + choice) = uint8_t(e.u8(0x12 + choice) + votes);
		e.u8(0x10 + choice) = uint8_t(e.u8(0x10 + choice) + supporters);
		return;
	}
	e.u8(0x14) = uint8_t(e.u8(0x14) + votes);
	const int32_t room = 0xFA - int32_t(e.u8(0x15));
	e.u8(0x15) = room > supporters ? uint8_t(e.u8(0x15) + supporters) : uint8_t(0xFA);
}

}  // namespace p3::data::tick
