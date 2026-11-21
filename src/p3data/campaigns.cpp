// The campaigns' start (campaigns.hpp).

#include "p3data/campaigns.hpp"

#include <cstring>

#include "p3data/battle/pirate_nests.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/missions.hpp"
#include "p3data/new_game.hpp"
#include "p3data/savegame.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

using namespace global;

constexpr uint32_t kLocalPlayer = 0x6DF31C;  // The network session's local player (0 alone).

uint16_t merchant_count(const SaveGame& save) {
	const uint16_t n = save.globals.u16(kWorldMerchantCount);
	return n < save.merchants.size() ? n : uint16_t(save.merchants.size());
}

}  // namespace

void fn_5421e0(SaveGame& save, const uint8_t* operation) {
	uint32_t local = 0;
	uint32_t merchant = 0;
	std::memcpy(&local, operation + 4, 4);
	std::memcpy(&merchant, operation + 8, 4);
	if (merchant >= merchant_count(save)) {
		return;
	}
	save.merchants[merchant].memory.u16(0x8) = 0;
	// (0x542208: in a network game ([0x6DF2F0] & 0xE) the lobby's seats, 0x43F660..0x43E760: not in p3vita.)
	if (local == save.globals.u32(kLocalPlayer)) {
		save.globals.u32(kPlayer) = merchant;
		save.player = int(merchant);
		// (0x5422E2: the town view's marks of his offices: the interface.)
	}
}

void after_game_load(SaveGame& save, uint8_t mode, bool from_screen, const LoadOptions& options) {
	const uint16_t merchants = merchant_count(save);
	// 0x547985 (0x5476E0, after 0x549F40): the humans from the 20th merchant on wait for their players.
	for (uint16_t i = 0x14; i < merchants; ++i) {
		if (save.merchants[i].memory.u16(0x8) == 0) {
			save.merchants[i].memory.u16(0x8) = 0x8001;
		}
	}
	// 0x433747: the mode and the options (0x433752..0x43378B).
	save.settings.u8(0xD) = mode;
	save.settings.u8(0x1F) = options.resolution;
	save.settings.u8(0x24) = options.details;
	save.settings.u8(0x20) = options.colour_depth;
	// (0x43378E..0x4337CF: the humans still 0 from the 20th on counted for 0x54B230, the session's copy of the
	// settings, and its flag 0x10: none of the world.)
	if (!from_screen) {
		return;
	}
	// 0x4337E5: the first merchant from the 20th on with [+0x8] ≠ 0 and its bit 15.
	uint16_t taken = 0xFFFF;
	for (uint16_t i = 0x14; i < merchants; ++i) {
		const uint16_t kind = save.merchants[i].memory.u16(0x8);
		if (kind != 0 && (kind & 0x8000)) {
			taken = i;
			break;
		}
	}
	// 0x433820: the operation 0xA8 run at once (0x535760 → 0x5421E0) with the local player and him. Without one, the
	// game passes the screen's own address, past the merchants: nothing.
	uint8_t a8[0x14] = {0xA8};
	const uint32_t local = save.globals.u32(kLocalPlayer);
	const uint32_t who = taken < merchants ? taken : 0xFFFFFFFF;
	std::memcpy(a8 + 4, &local, 4);
	std::memcpy(a8 + 8, &who, 4);
	fn_5421e0(save, a8);
	// 0x433842: the operation 0xC8 queued (0x54AA70): the time's mode 0, the speed unchanged (−1).
	uint8_t c8[0x14];
	std::memset(c8, 0xFF, sizeof(c8));
	c8[0] = 0xC8;
	c8[1] = c8[2] = c8[3] = 0;
	std::memset(c8 + 0xC, 0, 4);
	tick::enqueue_operation(save, c8, sizeof(c8));
}

void start_campaign(SaveGame& save, uint8_t campaign, uint32_t speed) {
	// 0x43097C..0x4309D2.
	fn_4d64b0(save);
	tick::events_detail::fn_4d8e70(save, 0x1B);
	save.settings.u8(0xD) = 5;
	save.settings.u8(0x14) = campaign;
	save.settings.u32(0x4) = speed;
	missions_detail::fn_511270(save, true);
	campaigns_detail::fn_54a480(save);
}

namespace campaigns_detail {

void fn_54a480(SaveGame& save) {
	GlobalMemory& g = save.globals;
	// (0x54A4A3: [0x6DF2F0] & 0xC, a network game: nothing. p3vita has none.)
	fn_52e1a0(save, save.settings.u8(0x10));
	const uint32_t player = g.u32(kPlayer);
	if (player < merchant_count(save)) {
		// 0x4F9550 (on the merchant, the settings' [+0x11]): a human's factor by table 0x672E04.
		static constexpr uint32_t kCareful[3] = {0x3FA66666, 0x3F800000, 0x3F333333};  // 1.3, 1.0, 0.7
		MerchantMemory& m = save.merchants[player].memory;
		if (m.u16(0x8) == 0) {
			m.u32(0x464) = kCareful[save.settings.u8(0x11) % 3];
		}
	}
	const uint8_t level = save.settings.u8(0x13);
	const uint32_t made = 2u * level + 1u;
	for (uint32_t k = 0; k < 5; ++k) {
		const bool exists = save.pirates[k].has_value();
		if (k < made) {
			if (!exists) {
				fn_513720(save, k);
				const uint32_t number = tick::game_rand(save);
				const uint32_t town = tick::game_rand(save);
				fn_513d80(save, k, town, level, number);
			} else {
				save.pirates[k]->u8(0x14) = uint8_t(level & 3);
			}
		} else if (exists) {
			save.pirates[k]->u8(0x15) = 1;
		}
	}
}

}  // namespace campaigns_detail

}  // namespace p3::data
