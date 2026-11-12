#include "p3data/guild_window.hpp"

#include <cstring>

#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace tick::events_detail {
int32_t fn_51b7e0(const SaveGame& save, int town, int merchant);  // events.cpp
int32_t fn_4daf10(const SaveGame& save, uint32_t town, uint32_t record, bool flag);  // events.cpp
}  // namespace tick::events_detail
namespace tick::events_mayor_detail {
int32_t fn_4da860(const SaveGame& save, uint32_t ship);  // events_mayor.cpp
}  // namespace tick::events_mayor_detail

namespace {

constexpr uint32_t kEventHead = 0x6DD744;  // u16 ([+0x8] of the queue 0x6DD73C)
constexpr uint32_t kEventCount = 0x6DD748;  // u16 ([+0xC])

using Event = GameMemory<0x18>;

OperationBytes words(std::initializer_list<uint32_t> fields) {
	OperationBytes op{};
	size_t at = 0;
	for (uint32_t f : fields) {
		std::memcpy(op.data() + at, &f, 4);
		at += 4;
	}
	return op;
}

bool is_auction(const Event& e) {
	return e.u16(6) == 0xA || e.u16(6) == 0x36;
}

const Event* auction_at(const SaveGame& save, uint32_t event) {
	if (event >= save.globals.u16(kEventCount) || event >= save.events.size() || !is_auction(save.events[event])) {
		return nullptr;
	}
	return &save.events[event];
}

uint32_t merchant_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldMerchantCount);
}

}  // namespace

bool guild_member(const SaveGame& save, uint32_t town, uint32_t merchant) {
	if (merchant >= merchant_count(save) || merchant >= save.merchants.size()) {
		return false;
	}
	return (save.merchants[merchant].memory.u32(0x468) >> (town & 31)) & 1;
}

int32_t guild_join_price(const SaveGame& save, uint32_t town, uint32_t merchant) {
	if (merchant >= merchant_count(save) || town >= save.towns.size()) {
		return 10000;
	}
	return tick::events_detail::fn_51b7e0(save, int(town), int(merchant));
}

bool can_join_guild(const SaveGame& save, uint32_t town, uint32_t merchant) {
	return merchant < save.merchants.size() &&
		save.merchants[merchant].memory.s32(0x0) >= guild_join_price(save, town, merchant);
}

OperationBytes join_guild_operation(uint32_t merchant, uint32_t town, int32_t price) {
	return words({0x37, merchant, town, uint32_t(price)});
}

std::vector<AuctionEntry> upcoming_auctions(const SaveGame& save) {
	std::vector<AuctionEntry> list;
	const GlobalMemory& g = save.globals;
	const uint32_t count = g.u16(kEventCount);
	const uint32_t towns = g.u32(global::kWorldTownCount);
	const uint16_t ships = g.u16(global::kShipCount);
	for (uint32_t i = g.u16(kEventHead); i < count && i < save.events.size() && list.size() < 3;
		i = save.events[i].u16(4)) {
		const Event& e = save.events[i];
		if (!is_auction(e)) {
			continue;
		}
		const uint8_t town = e.u8(0x15);
		if (town >= towns || town >= save.towns.size() || !(save.towns[town].memory.u8(0x2C8) & 0xC0)) {
			continue;
		}
		const uint8_t phase = e.u8(0x14);
		if (phase != 3 && phase != 2 && !(phase == 1 && save.settings.u8(0xD) == 0)) {
			continue;
		}
		AuctionEntry a;
		a.tick = e.u32(0);
		if (phase == 1) {
			a.tick += e.u16(0xE);
		}
		if (phase <= 2) {
			a.tick += 0x100;
		}
		a.town = town;
		a.event = i;
		const uint16_t records = save.towns[town].memory.u16(0x776);
		for (size_t k = 0; k < 3; ++k) {
			const uint16_t slot = e.u16(8 + 2 * k);
			a.lots[k] = 0xFFFF;
			if (e.u16(6) == 0xA) {
				if (slot < ships) {
					++a.types[save.ships[slot].memory.u8(0xE) & 3];
					a.lots[k] = slot;
				}
			} else if ((slot & 0x7FFF) < records) {
				a.types[0] = 0xFF;
				a.lots[k] = slot;
			}
		}
		list.push_back(a);
	}
	return list;
}

bool told_of_auction(const SaveGame& save, uint32_t event) {
	const Event* e = auction_at(save, event);
	if (!e) {
		return false;
	}
	const uint32_t player = save.globals.u32(global::kPlayer);
	uint32_t bit = 1;
	for (uint32_t m = 0; m < merchant_count(save) && m < save.merchants.size(); ++m) {
		if (m == player) {
			return (bit & e->u8(0x16)) == bit;
		}
		if (save.merchants[m].memory.u16(0x8) == 0) {
			bit += bit;
		}
	}
	return false;
}

OperationBytes tell_operation(const SaveGame& save, uint32_t player, uint32_t event) {
	return words({0x34, player, event, told_of_auction(save, event) ? 0u : 1u});
}

OperationBytes auction_ship_operation(uint32_t ship) {
	return words({0x36, ship});
}

int town_auction(const SaveGame& save, uint32_t town) {
	if (town >= save.globals.u32(global::kWorldTownCount) || town >= save.towns.size()) {
		return -1;
	}
	const uint16_t event = save.towns[town].memory.u16(0x9A2);
	const Event* e = auction_at(save, event);
	return e && e->u8(0x14) <= 7 ? int(event) : -1;
}

std::vector<uint16_t> auction_lots(const SaveGame& save, uint32_t event) {
	std::vector<uint16_t> lots;
	const Event* e = auction_at(save, event);
	if (!e) {
		return lots;
	}
	auto exists = [&](uint16_t slot) {
		if (e->u16(6) == 0xA) {
			return slot < save.globals.u16(global::kShipCount);
		}
		const uint8_t town = e->u8(0x15);
		return town < save.towns.size() && (slot & 0x7FFF) < save.towns[town].memory.u16(0x776);
	};
	for (size_t k = 0; k < 3 && exists(e->u16(8 + 2 * k)); ++k) {
		lots.push_back(e->u16(8 + 2 * k));
	}
	return lots;
}

int32_t lot_minimum_bid(const SaveGame& save, uint32_t event, uint16_t lot) {
	const Event* e = auction_at(save, event);
	if (!e) {
		return 0;
	}
	if (e->u16(6) == 0xA) {
		return lot < save.globals.u16(global::kShipCount) ? tick::events_mayor_detail::fn_4da860(save, lot) : 0;
	}
	const uint8_t town = e->u8(0x15);
	if (town >= save.towns.size() || (lot & 0x7FFF) >= save.towns[town].memory.u16(0x776)) {
		return 0;
	}
	return tick::events_detail::fn_4daf10(save, town, lot & 0x7FFF, (lot >> 15) != 0);
}

int ship_condition(const SaveGame& save, uint16_t ship) {
	if (ship >= save.ships.size()) {
		return 0;
	}
	const ShipMemory& s = save.ships[ship].memory;
	const int32_t most = s.s32(0x14);
	const int32_t percent = most != 0 ? s.s32(0x18) * 100 / most : 100;
	return percent < 40 ? 4 : percent < 60 ? 3 : percent < 80 ? 2 : percent < 90 ? 1 : 0;
}

bool bid_shown(const SaveGame& save, uint32_t event, uint32_t player) {
	const Event* e = auction_at(save, event);
	if (!e || e->u8(0x14) < 4 || e->u8(0x14) == 8) {
		return false;
	}
	return e->u16(6) == 0xA || tick::facilities_detail::fn_5308a0(save, player, e->u8(0x15)) >= 0;
}

bool can_bid(const SaveGame& save, uint32_t event, uint32_t player) {
	const Event* e = auction_at(save, event);
	if (!e || player >= save.merchants.size()) {
		return false;
	}
	const int32_t bid = int32_t(e->u16(0xE)) + int32_t(e->u16(0x10));
	return bid <= save.merchants[player].memory.s32(0x0) && e->u8(0x17) != player;
}

OperationBytes bid_operation(const SaveGame& save, uint32_t event, uint32_t player) {
	const Event* e = auction_at(save, event);
	const uint32_t bid = e ? uint32_t(e->u16(0xE)) + e->u16(0x10) : 0;
	return words({0x33, player, event, bid});
}

OperationBytes automatic_bid_operation(uint32_t top, uint32_t event, uint32_t lot, uint32_t player) {
	return words({0x35, top, event, lot, player});
}

uint32_t automatic_bid(const SaveGame& save, uint32_t event, uint32_t lot, uint32_t player) {
	const Event* e = auction_at(save, event);
	uint32_t top = 0;
	if (e) {
		tick::events_detail::fn_4d9000(save, player, e->u8(0x15), lot, top);
	}
	return top;
}

}  // namespace p3::data
