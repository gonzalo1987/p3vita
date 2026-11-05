#include "p3data/player_actions.hpp"

#include <cstring>
#include <initializer_list>

#include "p3data/game_fixes.hpp"
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

int player_office(const SaveGame& save, int town) {
	if (save.player < 0 || town < 0 || uint32_t(town) >= save.globals.u32(global::kWorldTownCount)) {
		return -1;
	}
	return tick::facilities_detail::fn_5308a0(save, uint32_t(save.player), uint32_t(town));
}

int32_t market_office_units(const SaveGame& save, int office, int ware) {
	const int32_t stock = save.offices[size_t(office)].memory.s32(4 + 4 * size_t(ware));
	return ware < 20 ? stock / unit_of(ware) : stock / 10;
}

int32_t market_office_used(const SaveGame& save, int office) {
	const OfficeMemory& o = save.offices[size_t(office)].memory;
	int32_t used = 0;
	for (size_t w = 0; w < 24; ++w) {
		used += o.s32(4 + 4 * w);
	}
	used += o.s32(0x2E8);
	if (used % 200 != 0) {
		used = used - used % 200 + 200;
	}
	return used / 200;
}

int32_t market_office_capacity(const SaveGame& save, int office) {
	return int32_t(save.offices[size_t(office)].memory.u16(0x2EC)) * 2000;
}

std::optional<OperationBytes> office_to_ship_operation(const SaveGame& save, int ship, int ware, uint32_t step) {
	if (ship < 0 || size_t(ship) >= save.ships.size() || ware < 0 || ware >= kTradeWareCount) {
		return std::nullopt;
	}
	const int office = player_office(save, save.ships[size_t(ship)].memory.u8(0x39));
	if (office < 0) {
		return std::nullopt;
	}
	const int32_t amount = market_buy_amount(step, market_office_units(save, office, ware), ware,
		save.ships[size_t(ship)].memory.s32(0x10), market_ship_used(save, ship));
	if (amount == 0) {
		return std::nullopt;
	}
	OperationBytes op = market_operation(0x08, amount, ship, ware, uint16_t(save.player), 0);
	op[0xE] = 1;
	return op;
}

std::optional<OperationBytes> ship_to_office_operation(const SaveGame& save, int ship, int ware, uint32_t step) {
	if (ship < 0 || size_t(ship) >= save.ships.size() || ware < 0 || ware >= kTradeWareCount ||
		player_office(save, save.ships[size_t(ship)].memory.u8(0x39)) < 0) {
		return std::nullopt;
	}
	const int32_t amount = market_sell_amount(step, market_ship_units(save, ship, ware), ware);
	if (amount == 0) {
		return std::nullopt;
	}
	return market_operation(0x08, amount, ship, ware, uint16_t(save.player), 0);
}

std::optional<OperationBytes> office_buy_operation(const SaveGame& save, int town, int ware, uint32_t step) {
	const int office = player_office(save, town);
	if (office < 0 || ware < 0 || ware >= kTradeWareCount) {
		return std::nullopt;
	}
	const int32_t amount = market_buy_amount(step, market_town_units(save, town, ware), ware,
		market_office_capacity(save, office), market_office_used(save, office));
	if (amount == 0) {
		return std::nullopt;
	}
	return market_operation(0x59, amount, ware, save.player, uint16_t(save.player), town);
}

std::optional<OperationBytes> office_sell_operation(const SaveGame& save, int town, int ware, uint32_t step) {
	const int office = player_office(save, town);
	if (office < 0 || ware < 0 || ware >= kTradeWareCount) {
		return std::nullopt;
	}
	const int32_t amount = market_sell_amount(step, market_office_units(save, office, ware), ware);
	if (amount == 0) {
		return std::nullopt;
	}
	return market_operation(0x58, amount, ware, save.player, uint16_t(save.player), town);
}

static OperationBytes words_operation(std::initializer_list<uint32_t> words);

namespace {

constexpr int32_t kPieceWeight[6] = {1000, 1000, 2000, 2000, 2000, 1000};  // 0x672CB4

// A row's count: the step (1, 5, 50) or all, at most all (0x4A90E1..0x4A9118).
int32_t row_count(uint32_t step, int32_t all) {
	int32_t n = step != 0 ? int32_t(step) : all;
	return n > all ? all : n;
}

}  // namespace

int32_t weapons_to_ship_pieces(uint32_t step, int32_t office_units, int32_t capacity, int32_t used) {
	int32_t n = row_count(step, office_units);
	const int32_t room = capacity - used * 200;
	if (n * 10 > room) {
		n = room / 10;  // It can be negative: the window sends it (0x538100 drops the sign).
	}
	return n;
}

int32_t ship_artillery_count(const SaveGame& save, int ship, int type) {
	const ShipMemory& s = save.ships[size_t(ship)].memory;
	if (type < 0) {
		return s.u16(0x154);
	}
	int32_t n = 0;
	for (size_t i = 0; i < 24; ++i) {
		n += s.u8(0x13C + i) == type;
	}
	return n;
}

int32_t office_artillery_count(const SaveGame& save, int office, int type) {
	const OfficeMemory& o = save.offices[size_t(office)].memory;
	return type < 0 ? o.s32(0x2BC) : o.s32(0x124 + 4 * size_t(type)) / kPieceWeight[type];
}

OperationBytes artillery_bytes(int type, int town, int ship, uint32_t count, bool from_ship) {
	return words_operation({0x09, uint32_t(type), uint32_t(town) | (from_ship ? 0x80000000u : 0u), uint32_t(ship), count});
}

std::optional<OperationBytes> weapons_to_ship_bytes(uint32_t step, int32_t office_units, int ware, int32_t capacity,
	int32_t used, int ship, uint16_t player, int town, int32_t pieces) {
	const int32_t n = weapons_to_ship_pieces(step, office_units, capacity, used);
	if (n == 0) {
		if (fix_game_bugs()) {
			return std::nullopt;
		}
		const int32_t c = row_count(step, pieces);
		if (c == 0) {
			return std::nullopt;
		}
		return artillery_bytes(ware - 0x14 - 5, town, ship, uint32_t(c), false);
	}
	OperationBytes op = market_operation(0x08, n * 10, ship, ware, player, 0);
	op[0xE] = 1;
	return op;
}

std::optional<OperationBytes> weapons_to_office_bytes(uint32_t step, int32_t ship_units, int ware, int ship, uint16_t player) {
	const int32_t n = row_count(step, ship_units);
	if (n == 0) {
		return std::nullopt;  // The game's bug reads fields nothing writes (assumed 0): nothing either way.
	}
	return market_operation(0x08, n * 10, ship, ware, player, 0);
}

std::optional<OperationBytes> artillery_row_bytes(uint32_t step, int32_t units, int type, int town, int ship, bool from_ship) {
	const int32_t n = row_count(step, units);
	if (n == 0) {
		return std::nullopt;
	}
	return artillery_bytes(type, town, ship, uint32_t(n), from_ship);
}

std::optional<OperationBytes> weapons_to_ship_operation(const SaveGame& save, int ship, int ware, uint32_t step) {
	if (ship < 0 || size_t(ship) >= save.ships.size() || ware < 0x14 || ware > 0x17) {
		return std::nullopt;
	}
	const int town = save.ships[size_t(ship)].memory.u8(0x39);
	const int office = player_office(save, town);
	if (office < 0) {
		return std::nullopt;
	}
	return weapons_to_ship_bytes(step, market_office_units(save, office, ware), ware, save.ships[size_t(ship)].memory.s32(0x10),
		market_ship_used(save, ship), ship, uint16_t(save.player), town, ship_artillery_count(save, ship, ware - 0x14 + 2));
}

std::optional<OperationBytes> weapons_to_office_operation(const SaveGame& save, int ship, int ware, uint32_t step) {
	if (ship < 0 || size_t(ship) >= save.ships.size() || ware < 0x14 || ware > 0x17 ||
		player_office(save, save.ships[size_t(ship)].memory.u8(0x39)) < 0) {
		return std::nullopt;
	}
	return weapons_to_office_bytes(step, save.ships[size_t(ship)].memory.s32(0x54 + 4 * size_t(ware)) / 10, ware, ship,
		uint16_t(save.player));
}

std::optional<OperationBytes> artillery_operation(const SaveGame& save, int ship, int type, uint32_t step, bool from_ship) {
	if (ship < 0 || size_t(ship) >= save.ships.size() || type < -1 || type > 5) {
		return std::nullopt;
	}
	const int town = save.ships[size_t(ship)].memory.u8(0x39);
	const int office = player_office(save, town);
	if (office < 0) {
		return std::nullopt;
	}
	return artillery_row_bytes(step,
		from_ship ? ship_artillery_count(save, ship, type) : office_artillery_count(save, office, type), type, town, ship,
		from_ship);
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
