#include "p3data/player_actions.hpp"

#include <cstring>
#include <initializer_list>

#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick::ship_trade_detail {
uint16_t fn_5269a0(const SaveGame& save, int town, uint16_t owner);  // ship_trade.cpp
}  // namespace p3::data::tick::ship_trade_detail

namespace p3::data {

namespace {

constexpr uint32_t kConvoyCountGlobal = global::kShips + 0xF6;  // u16 (0x6DD896)

// 0x672C14: 1 for the wares traded in barrels (200), 0 for loads (2000).
constexpr uint8_t kInBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};

int32_t unit_of(int ware) {
	return kInBarrels[ware] ? 200 : 2000;
}

bool valid(const SaveGame& save, int ship, int town, int ware) {
	return ship >= 0 && size_t(ship) < save.ships.size() && town >= 0 && size_t(town) < save.towns.size() &&
		ware >= 0 && ware < kTradeWareCount;
}

}  // namespace

void queue_operation(SaveGame& save, const OperationBytes& operation) {
	save.queued_operations.emplace_back(operation.begin(), operation.end());
}

int32_t market_ship_units(const SaveGame& save, int ship, int ware) {
	return save.ships[size_t(ship)].memory.s32(0x54 + 4 * size_t(ware)) / unit_of(ware);
}

int32_t market_town_units(const SaveGame& save, int town, int ware) {
	return save.towns[size_t(town)].memory.s32(4 + 4 * size_t(ware)) / unit_of(ware);
}

int32_t market_ship_used(const SaveGame& save, int ship) {
	const ShipMemory& s = save.ships[size_t(ship)].memory;
	int32_t used = s.s32(0x118) + s.s32(0x11C);
	const int32_t rest = s.s32(0x11C) % 200;
	if (rest != 0) {
		used = used - rest + 200;
	}
	return used / 200;
}

int32_t market_buy_amount(uint32_t step, int32_t town_units, int ware, int32_t capacity, int32_t used) {
	int32_t n = step != 0 ? int32_t(step) : town_units;
	if (n > town_units) {
		n = town_units;
	}
	if (!kInBarrels[ware]) {
		n *= 10;  // In barrels of 200, to compare with the hold.
	}
	const int32_t room = capacity / 200 - used;
	if (n > room) {
		n = room;  // It can be negative: the window sends it, and 0x537450 buys nothing.
	}
	int32_t amount = int32_t(uint32_t(n) * 200u);
	if (!kInBarrels[ware]) {
		amount = amount / 2000 * 2000;
	}
	return amount;
}

int32_t market_sell_amount(uint32_t step, int32_t ship_units, int ware) {
	int32_t n = step != 0 ? int32_t(step) : ship_units;
	if (n > ship_units) {
		n = ship_units;
	}
	return int32_t(uint32_t(n) * uint32_t(unit_of(ware)));
}

OperationBytes market_operation(uint32_t code, int32_t amount, int ware, int ship, uint16_t player, int town) {
	OperationBytes op{};
	const uint16_t fields[4] = {uint16_t(ware), uint16_t(ship), player, uint16_t(town)};
	std::memcpy(op.data(), &code, 4);
	std::memcpy(op.data() + 4, &amount, 4);
	std::memcpy(op.data() + 8, fields, sizeof(fields));
	return op;
}

std::optional<OperationBytes> market_buy_operation(const SaveGame& save, int ship, int town, int ware, uint32_t step) {
	if (!valid(save, ship, town, ware)) {
		return std::nullopt;
	}
	const int32_t amount = market_buy_amount(step, market_town_units(save, town, ware), ware,
		save.ships[size_t(ship)].memory.s32(0x10), market_ship_used(save, ship));
	if (amount == 0) {
		return std::nullopt;
	}
	return market_operation(0x02, amount, ware, ship, uint16_t(save.player), town);
}

std::optional<OperationBytes> market_sell_operation(const SaveGame& save, int ship, int town, int ware, uint32_t step) {
	if (!valid(save, ship, town, ware)) {
		return std::nullopt;
	}
	const int32_t amount = market_sell_amount(step, market_ship_units(save, ship, ware), ware);
	if (amount == 0) {
		return std::nullopt;
	}
	return market_operation(0x01, amount, ware, ship, uint16_t(save.player), town);
}

static OperationBytes words_operation(std::initializer_list<uint32_t> words) {
	OperationBytes op{};
	size_t at = 0;
	for (const uint32_t w : words) {
		std::memcpy(op.data() + at, &w, 4);
		at += 4;
	}
	return op;
}

OperationBytes hire_sailors_operation(uint32_t ship, uint32_t amount) {
	return words_operation({0x04, ship, amount});
}

OperationBytes dismiss_sailors_operation(uint32_t ship, uint32_t amount) {
	return words_operation({0x05, ship, amount});
}

OperationBytes hire_captain_operation(uint32_t captain, uint32_t ship, uint32_t town) {
	return words_operation({0x06, captain, ship, town});
}

OperationBytes dismiss_captain_operation(uint32_t ship) {
	return words_operation({0x07, ship});
}

OperationBytes repair_operation(uint32_t ship) {
	return words_operation({0x03, ship});
}

OperationBytes repair_convoy_operation(uint32_t convoy) {
	return words_operation({0x1D, convoy});
}

std::vector<uint16_t> window_ships(const SaveGame& save, int merchant, int town, ShipListMode mode) {
	std::vector<uint16_t> list;
	if (merchant < 0 || uint32_t(merchant) >= save.globals.u16(global::kWorldMerchantCount) ||
		size_t(merchant) >= save.merchants.size()) {
		return list;
	}
	const uint32_t ships = save.globals.u16(global::kShipCount);
	const uint32_t convoys = save.globals.u16(kConvoyCountGlobal);
	const uint32_t captains = save.globals.u16(global::kCaptainCount);
	auto ship = [&](uint32_t i) -> const ShipMemory& { return save.ships[i].memory; };
	for (uint32_t i = save.merchants[size_t(merchant)].memory.u16(0xE); i < ships; i = ship(i).u16(0x4)) {
		const ShipMemory& s = ship(i);
		if (int(s.u8(0x39)) != town) {
			continue;
		}
		const uint32_t convoy = s.u16(0x8);
		switch (mode) {
		case ShipListMode::Sailors:  // 0x504B32
			if (s.u16(0x134) <= 1 && !(s.u8(0x136) & 1)) {
				list.push_back(uint16_t(i));
			}
			break;
		case ShipListMode::Repairs:  // 0x504BA0
			if (s.u8(0x136) & 1) {
				break;
			}
			if (convoy < convoys) {
				const auto& c = save.convoys[convoy];
				if (c.u16(0x10) != i || (c.u8(0x14) & 2) != 2 || c.u16(0x12) > 1) {
					break;
				}
				for (uint32_t k = c.u16(0xA); k < ships; k = ship(k).u16(0x6)) {
					if (ship(k).s32(0x18) < ship(k).s32(0x14)) {
						list.push_back(uint16_t(i));
						break;
					}
				}
			} else if (s.u16(0x134) <= 1 && s.s32(0x18) < s.s32(0x14)) {
				list.push_back(uint16_t(i));
			}
			break;
		case ShipListMode::Captains:  // 0x504D09
			if (s.u16(0x134) > 1) {
				break;
			}
			if (convoy >= convoys) {
				if (s.u16(0x42) >= captains) {
					list.push_back(uint16_t(i));
				}
				break;
			}
			{
				const auto& c = save.convoys[convoy];
				if (c.u16(0x10) != i || (s.u8(0x136) & 1)) {
					break;
				}
				for (uint32_t k = c.u16(0xA); k < ships; k = ship(k).u16(0x6)) {
					if (int(ship(k).u8(0)) == merchant && ship(k).u16(0x42) >= captains) {
						list.push_back(uint16_t(k));
					}
				}
			}
			break;
		}
	}
	return list;
}

float tavern_roll_of(uint32_t rand_value) {
	// fild; fstp (a float); fdiv 32768.0f; fstp [esp+0x14] (a float).
	return float(rand_value) / 32768.0f;
}

float tavern_roll(SaveGame& save) {
	return tavern_roll_of(tick::game_rand(save));
}

uint32_t tavern_sailors_shown(uint8_t allowed, float roll) {
	if (allowed <= 50) {
		return allowed;
	}
	// fld [+0x1CA0]; fmul 40.0f (0x672F04); fadd 10.5 (0x679CA0); __ftol.
	return uint32_t(tick::ftol(double(roll) * 40.0 + 10.5));
}

uint32_t tavern_sailors(const SaveGame& save, int merchant, int town, float roll) {
	if (merchant < 0 || size_t(merchant) >= save.merchants.size() || town < 0 ||
		uint32_t(town) >= save.globals.u32(global::kWorldTownCount)) {
		return 0;
	}
	return tavern_sailors_shown(tick::fn_4f6ca0(save, save.merchants[size_t(merchant)].memory, uint32_t(town)), roll);
}

uint16_t tavern_captain(const SaveGame& save, int town, uint16_t merchant) {
	if (town < 0 || size_t(town) >= save.towns.size()) {
		return 0xFFFF;
	}
	return tick::ship_trade_detail::fn_5269a0(save, town, merchant);
}

CaptainInfo captain_info(const SaveGame& save, uint16_t captain) {
	CaptainInfo info;
	if (captain >= save.globals.u16(global::kCaptainCount) || captain >= save.captains.size()) {
		return info;
	}
	const CaptainMemory& r = save.captains[captain];
	info.first_name = r.u8(0x2);
	info.family_name = r.u8(0x3);
	info.age = (save.globals.u32(global::kWorldTick) - r.u32(0x4)) / 93440u;
	// imul by 0x51EB851F, sar 4: / 50.
	info.trading = uint8_t(r.u8(0xA) / 50);
	info.sailing = uint8_t(r.u8(0x9) / 50);
	info.fighting = uint8_t(r.u8(0xB) / 50);
	info.wage = r.u16(0xC);
	return info;
}

bool ship_window_open(const SaveGame& save, int ship) {
	return ship >= 0 && size_t(ship) < save.ships.size() && save.ships[size_t(ship)].memory.u16(0x134) == 0;
}

bool can_dismiss_captain(const SaveGame& save, int ship) {
	if (!ship_window_open(save, ship)) {
		return false;
	}
	const ShipMemory& s = save.ships[size_t(ship)].memory;
	return s.u16(0x42) < save.globals.u16(global::kCaptainCount) && s.u16(0x134) != 0x12 && s.u8(0x15C) == 0;
}

int32_t repair_cost(const SaveGame& save, int town, int ship) {
	if (town < 0 || size_t(town) >= save.towns.size() || ship < 0 || size_t(ship) >= save.ships.size()) {
		return 0;
	}
	return tick::fn_52a950(save, town, ship);
}

OperationBytes repair_window_operation(const SaveGame& save, int ship) {
	const uint16_t convoy = save.ships[size_t(ship)].memory.u16(0x8);
	if (convoy < save.globals.u16(kConvoyCountGlobal)) {
		return repair_convoy_operation(convoy);
	}
	return repair_operation(uint32_t(ship));
}

}  // namespace p3::data
