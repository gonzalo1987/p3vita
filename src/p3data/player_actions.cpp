#include "p3data/player_actions.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick::ship_trade_detail {
uint16_t fn_5269a0(const SaveGame& save, int town, uint16_t owner);  // ship_trade.cpp
}  // namespace p3::data::tick::ship_trade_detail
namespace p3::data::tick::events_towns_detail {
bool ship_order_cost(const SaveGame& save, int town, uint32_t type, uint32_t merchant, int32_t& cost);  // events_towns.cpp
bool ship_order_details(const SaveGame& save, int town, uint32_t type, uint32_t merchant, int32_t out[26],
	int32_t materials[5]);  // events_towns.cpp
bool fn_52b0d0(const SaveGame& save, int town, uint8_t type);  // events_towns.cpp
}  // namespace p3::data::tick::events_towns_detail
namespace p3::data::tick::events_detail {
int16_t fn_4f9580(const SaveGame& save, int merchant, uint32_t town);  // events.cpp
int32_t fn_4daf10(const SaveGame& save, uint32_t town, uint32_t record, bool flag);  // events.cpp
void fn_4dafe0(const SaveGame& save, uint32_t town, uint32_t record, uint32_t& tick, int32_t& price);  // events.cpp
}  // namespace p3::data::tick::events_detail
namespace p3::data::tick::construction_detail {
bool fn_51f3a0(SaveGame& save, int town, GameMemory<0x44>& s);  // construction.cpp
void fn_5250d0(const SaveGame& save, int town, uint8_t& x, uint8_t& y);  // construction.cpp
}  // namespace p3::data::tick::construction_detail
namespace p3::data::tick::merchants_detail {
uint32_t fn_517f70(const SaveGame& save, int ship);  // town_tick/merchants.cpp: the crew's daily wage
}  // namespace p3::data::tick::merchants_detail
namespace p3::data {
uint32_t fn_5185c0(const ShipMemory& s);  // battle/pirate_objects.cpp: the most sailors a ship takes
}  // namespace p3::data
namespace p3::data::convoys_sea_detail {
int fn_517260(const Sailing& sailing, uint16_t x, uint16_t y);  // convoys_sea.cpp: the town at a point, or -1
}  // namespace p3::data::convoys_sea_detail

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
	if (mode == ShipListMode::Auction && !(save.merchants[size_t(merchant)].memory.u32(0x468) & (1u << (uint32_t(town) & 31)))) {
		return list;
	}
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
		case ShipListMode::Upgrades:  // 0x504C88
			if (convoy >= convoys && s.u16(0x134) <= 1 && s.u8(0xF) < 2 && !(s.u8(0x136) & 1)) {
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
		case ShipListMode::Auction:  // 0x504F02, then 0x504F30 (the mode 7)
		case ShipListMode::Docked:  // 0x504F30
			if (convoy >= convoys && !(s.u8(0x136) & 1) && s.u16(0x134) <= 1) {
				list.push_back(uint16_t(i));
			}
			break;
		case ShipListMode::SmallShips:  // 0x504E0B
			if (s.u16(0x134) <= 1 && !(s.u8(0xE) & 2)) {
				list.push_back(uint16_t(i));
			}
			break;
		case ShipListMode::Pirate:  // 0x504E77
			if (s.u16(0x42) >= captains && convoy >= convoys && s.s32(0x18) > (s.s32(0x14) >> 2) && s.u16(0x134) <= 1) {
				list.push_back(uint16_t(i));
			}
			break;
		case ShipListMode::WeaponsDealer: {  // 0x504FAC
			if (s.u16(0x134) > 1 || (s.u8(0x136) & 1)) {
				break;
			}
			// Some hand weapon above 10 units, or room for more than 10 (0x5182B0, asked of a copy); in a convoy those of
			// the guide (the merchant's list's ship), for each of the merchant's ships of the convoy (the game's).
			bool fits = false;
			for (uint32_t k = 0; k < 4; ++k) {
				fits = fits || s.s32(0xA4 + 4 * k) > 10;
			}
			if (!fits) {
				SavedShip copy = save.ships[i];
				fits = recalculate_cargo(copy) > 10;
			}
			if (convoy >= convoys) {
				if (fits) {
					list.push_back(uint16_t(i));
				}
				break;
			}
			const auto& c = save.convoys[convoy];
			if (c.u16(0x10) != i) {
				break;
			}
			for (uint32_t k = c.u16(0xA); k < ships; k = ship(k).u16(0x6)) {
				if (int(ship(k).u8(0)) == merchant && fits) {
					list.push_back(uint16_t(k));
				}
			}
			break;
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

uint32_t ship_daily_wage(const SaveGame& save, int ship) {
	return tick::merchants_detail::fn_517f70(save, ship);
}

uint32_t ship_most_sailors(const SaveGame& save, int ship) {
	return fn_5185c0(save.ships[size_t(ship)].memory);
}

int32_t ship_shipyard_days(const SaveGame& save, int ship) {
	// 0x493E3E: only in the states 6 and 7, with the town it is in (0x52AAD0 with 0).
	const SavedShip& s = save.ships[size_t(ship)];
	if ((s.status() != 6 && s.status() != 7) || s.town() >= save.towns.size()) {
		return 0;
	}
	return tick::fn_52aad0(save, s.town(), ship, 0);
}

int32_t ship_days_to_go(const SaveGame& save, int ship) {
	const SavedShip& s = save.ships[size_t(ship)];
	const uint16_t state = s.status();
	if (state != 2 && state != 3 && state != 0xF && state != 0x10) {
		return 0;
	}
	const int32_t now = int32_t(save.tick());
	const int32_t arrival = int32_t(s.arrival_tick());
	return arrival > now ? (arrival - now) >> 8 : 0;
}

std::array<int32_t, 24> ship_cargo_prices(const SaveGame& save, int ship) {
	std::array<int32_t, 24> out{};
	const SavedShip& s = save.ships[size_t(ship)];
	for (int w = 0; w < 24; ++w) {
		// fld [+0xB4 + 4w]; fmul 200.0f, 2000.0f or 10.0f; fadd 0.5f; __ftol. The product of the float and the
		// whole factor and the half are exact in a double, as in the x87.
		const double factor = w >= 20 ? 10.0 : kInBarrels[w] ? 200.0 : 2000.0;
		out[size_t(w)] = int32_t(double(s.average_price(w)) * factor + 0.5);
	}
	return out;
}

FirepowerBalls firepower_balls(int32_t firepower) {
	FirepowerBalls out;
	const int32_t v = firepower - 9;  // [0x672CC8], a byte
	if (firepower == 0 || v < 0) {
		return out;  // 0: none; under 9 the game's sqrt gives a NaN, whose counts are negative: none drawn.
	}
	// fild; fmul 1.4f; fstp qword; sqrt; fadd 1.0; fstp float; fld; fadd st, st; ftol.
	const double root = std::sqrt(double(float(v)) * double(1.4f));
	const float f = float(root + 1.0);
	const int32_t n = int32_t(double(f) + double(f));
	const float half = float(n) / 2.0f;
	out.big = int32_t(half / 5.0f);
	if (out.big >= 5) {
		out.big = 5;
		return out;
	}
	const float rest = half - float(5 * out.big);
	out.small = int32_t(rest);
	out.half = !(double(rest - float(out.small)) < 0.5);
	return out;
}

bool can_turn_pirate(const SaveGame& save, const Sailing& sailing, int ship) {
	const GlobalMemory& g = save.globals;
	const uint16_t ships = g.u16(global::kShipCount);
	const ShipMemory& s = save.ships[size_t(ship)].memory;
	const uint16_t convoy = s.u16(0x8);
	if (convoy >= ships) {
		if (s.u16(0x50) < ships || s.u16(0x52) < ships || s.u16(0x134) != 0xF) {
			return false;
		}
	} else {
		if (convoy >= save.convoys.size()) {
			return false;  // The game reads past its convoys there.
		}
		const GameMemory<0x3C>& c = save.convoys[convoy];
		const uint16_t guide = c.u16(0x10);
		if (guide >= ships || guide >= save.ships.size()) {
			return false;
		}
		const ShipMemory& gs = save.ships[guide].memory;
		if (gs.u16(0x50) < ships || gs.u16(0x52) < ships || c.u8(0x0) != s.u8(0x0) || c.u16(0x12) != 0xF ||
			!(c.u8(0x14) & 2)) {
			return false;
		}
	}
	// 0x5187CE: the ship's own point (the high words of [+0x1C] and [+0x20]).
	return convoys_sea_detail::fn_517260(sailing, s.u16(0x1E), s.u16(0x22)) < 0;
}

OperationBytes pirate_operation(uint32_t ship, bool pirate) {
	return words_operation({0x0D, ship, pirate ? 1u : 0u});
}

OperationBytes route_point_operation(uint32_t ship, uint32_t step, int x, int y) {
	return words_operation({0x0B, ship, step, uint32_t(x), uint32_t(y)});
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

// --- The shipyard's other pages ---

OperationBytes order_ship_operation(uint32_t type, uint32_t player, uint32_t town) {
	return words_operation({0x2C, type, player, town, 0});
}

bool ship_order_cost(const SaveGame& save, int town, uint32_t type, int merchant, int32_t& cost) {
	cost = 0;
	if (town < 0 || size_t(town) >= save.towns.size() || merchant < 0 || size_t(merchant) >= save.merchants.size() ||
		type > 3) {
		return false;
	}
	return tick::events_towns_detail::ship_order_cost(save, town, type, uint32_t(merchant), cost);
}

std::vector<uint16_t> shipyard_orders(const SaveGame& save, int town) {
	std::vector<uint16_t> list;
	if (town < 0 || size_t(town) >= save.towns.size()) {
		return list;
	}
	const uint16_t ships = save.globals.u16(global::kShipCount);
	for (uint32_t i = save.towns[size_t(town)].memory.u16(0x81C); i < ships && list.size() < ships;
		 i = save.ships[i].memory.u16(0x6)) {
		list.push_back(uint16_t(i));
	}
	return list;
}

uint32_t shipyard_done_tick(SaveGame& save, int town, int ship) {
	if (town < 0 || size_t(town) >= save.towns.size()) {
		return 0;
	}
	const TownMemory& t = save.towns[size_t(town)].memory;
	const uint32_t ships = save.globals.u16(global::kShipCount);
	const uint32_t now = save.globals.u32(global::kWorldTick);
	uint32_t i = t.u16(0x81C);
	if (i >= ships) {
		return 0;
	}
	const int32_t per_day = int32_t(t.u16(0x854)) * 100;
	int32_t work = 0;
	uint32_t extra = 0;
	for (size_t guard = 0; i < ships && i < save.ships.size() && guard < ships; ++guard) {
		ShipMemory& s = save.ships[i].memory;
		int32_t done = s.s32(0x18);
		if (done < 0x1C) {
			if (done < 0) {
				s.s32(0x18) = 0;  // 0x52B03A: the game writes it back, a date shown changing the save (as the game).
				done = 0;
			}
			extra += uint32_t(0x1C - done) << 8;
		}
		work += s.s32(0x14) - done;
		if (int(i) == ship) {
			break;
		}
		i = s.u16(0x6);
	}
	int32_t days = per_day != 0 ? (per_day + work - 1) / per_day : 0;  // The game divides by 0 there.
	if (int32_t(now & 0xFF) < int32_t(t.u8(0x2C0)) * 8 + 7) {
		--days;
	}
	return (uint32_t(days) << 8) + extra + (now & 0xFFFFFF00u);
}

bool can_cancel_order(const SaveGame& save, int ship) {
	return ship >= 0 && size_t(ship) < save.ships.size() && save.ships[size_t(ship)].memory.s32(0x18) <= 0 &&
		int(save.ships[size_t(ship)].memory.u8(0)) == save.player;
}

OperationBytes cancel_order_operation(uint32_t player, uint32_t town, uint32_t ship) {
	return words_operation({0x2F, player, town, ship});
}

ShipOrderInfo ship_order_info(const SaveGame& save, int town, uint32_t type, int merchant) {
	ShipOrderInfo info;
	if (town < 0 || size_t(town) >= save.towns.size() || merchant < 0 || size_t(merchant) >= save.merchants.size() ||
		type > 3) {
		return info;
	}
	int32_t f[26] = {};
	info.ok = tick::events_towns_detail::ship_order_details(save, town, type, uint32_t(merchant), f, info.need);
	for (size_t k = 0; k < 5; ++k) {
		info.have[k] = f[k];
		info.buy[k] = f[10 + k];
		info.price[k] = f[15 + k];
	}
	info.cost = f[20];
	info.days = f[21];
	info.hold = f[22];
	info.big_slots = uint8_t(f[23]);
	info.small_slots = uint8_t(f[24]);
	static constexpr uint8_t kCrew[4] = {5, 8, 10, 12};  // 0x673660
	static constexpr uint8_t kKnots[4] = {6, 6, 5, 5};  // 0x673668
	info.crew = kCrew[type];
	info.knots = kKnots[type];
	return info;
}

bool shipyard_builds(const SaveGame& save, int town, uint32_t type) {
	return town >= 0 && size_t(town) < save.towns.size() && type < 4 &&
		tick::events_towns_detail::fn_52b0d0(save, town, uint8_t(type));
}

int32_t shipyard_ship_days(const SaveGame& save, int ship, bool whole) {
	if (ship < 0 || size_t(ship) >= save.ships.size()) {
		return 0;
	}
	const uint32_t town = save.ships[size_t(ship)].memory.u8(0x39);
	if (town >= save.towns.size()) {
		return 0;
	}
	return tick::fn_52aad0(save, int(town), ship, whole ? 1 : 0);
}

std::vector<uint16_t> shipyard_repairs(const SaveGame& save, int town, int player) {
	std::vector<uint16_t> list;
	if (player < 0 || size_t(player) >= save.merchants.size()) {
		return list;
	}
	const uint16_t ships = save.globals.u16(global::kShipCount);
	for (uint32_t i = save.merchants[size_t(player)].memory.u16(0xE); i < ships && i < save.ships.size();
		 i = save.ships[i].memory.u16(0x4)) {
		const ShipMemory& s = save.ships[i].memory;
		if (s.u16(0x134) == 6 && int(s.u8(0x39)) == town) {
			list.push_back(uint16_t(i));
		}
	}
	// 0x5FB870(0, count, list, 0): for each i, each later j swapped in when it has fewer days left (each ship's days
	// by its own town's 0x52AAD0 with 0).
	for (size_t i = 0; i < list.size(); ++i) {
		for (size_t j = i + 1; j < list.size(); ++j) {
			if (shipyard_ship_days(save, list[j], false) < shipyard_ship_days(save, list[i], false)) {
				std::swap(list[i], list[j]);
			}
		}
	}
	return list;
}

OperationBytes stop_repairs_operation(uint32_t ship, uint32_t town) {
	return words_operation({0x00, ship, town});
}

std::array<uint8_t, 2> ship_slots(int type, int level) {
	static constexpr uint8_t kSlots[24] = {0x00, 0x00, 0x04, 0x00, 0x04, 0x02, 0x00, 0x00, 0x04, 0x00, 0x02, 0x04, 0x00,
		0x04, 0x00, 0x06, 0x02, 0x08, 0x00, 0x06, 0x02, 0x08, 0x00, 0x0C};  // 0x672CF0
	const size_t at = size_t(6 * (type & 3) + 2 * std::clamp(level, 0, 2));
	return {kSlots[at], kSlots[at + 1]};
}

std::array<int32_t, 3> upgrade_needs(int type, int levels) {
	static constexpr int32_t kTable[4][3] = {{2, 1, 1}, {3, 2, 2}, {4, 3, 3}, {5, 4, 4}};  // 0x66E030's [1..3]
	std::array<int32_t, 3> need{};
	for (size_t k = 0; k < 3; ++k) {
		need[k] = kTable[type & 3][k] * levels;
	}
	return need;
}

bool upgrade_cost(const SaveGame& save, int town, int ship, int level, ShipUpgrade& out) {
	out = ShipUpgrade();
	if (town < 0 || size_t(town) >= save.towns.size() || ship < 0 || size_t(ship) >= save.ships.size()) {
		return false;
	}
	const int levels = level - int(save.ships[size_t(ship)].memory.u8(0xF));
	return tick::fn_52b670(save, town, out, uint32_t(ship), uint32_t(levels));
}

OperationBytes upgrade_operation(uint32_t ship, uint32_t level) {
	return words_operation({0x0C, ship, level});
}

std::vector<OperationBytes> rename_operations(uint32_t ship, const std::string& name) {
	std::vector<OperationBytes> ops;
	size_t at = 0;
	do {
		OperationBytes op{};
		const uint32_t code = at == 0 ? 0x2D : 0x2E;
		std::memcpy(op.data(), &code, 4);
		const size_t n = std::min<size_t>(12, name.size() - at);
		std::memcpy(op.data() + 4, name.data() + at, n);
		std::memcpy(op.data() + 0x10, &ship, 4);
		ops.push_back(op);
		at += n;
	} while (at < name.size());
	return ops;
}

// --- Convoys ---

namespace {

constexpr uint32_t kConvoyHeadGlobal = global::kShips + 0xEC;  // u16 the first convoy (0x6DD88C)
constexpr uint8_t kJoinSailors[4] = {5, 8, 10, 12};  // 0x673660

bool can_guide(const SaveGame& save, uint16_t ship) {
	return ship < save.ships.size() && tick::ship_ai_detail::fn_519c40(save, ship);
}

// 0x5039F0 (on the convoy: ship), as convoys_members.cpp has it.
bool event_convoy_takes(const SaveGame& save, const GameMemory<0x3C>& c, uint16_t ship) {
	if (c.u16(0x1C) >= save.globals.u32(global::kWorldTownCount) || (c.u16(0x12) != 9 && c.u16(0x12) != 0xA) ||
		c.u16(0x2) >= save.globals.u16(0x6DD748) || c.u16(0x2) >= save.events.size() ||
		ship >= save.globals.u16(global::kShipCount)) {
		return false;
	}
	const GameMemory<0x18>& e = save.events[c.u16(0x2)];
	const uint16_t type = e.u16(0x6);
	if ((type != 0x34 && type != 0x23 && type != 0x24) || e.u8(0x15) != 2) {
		return false;
	}
	const ShipMemory& s = save.ships[ship].memory;
	if (s.u8(0x39) != e.u8(0x14) || s.u16(0x134) > 1) {
		return false;
	}
	return type != 0x34 || s.s32(0x120) >= 72;
}

bool any_big(const SaveGame& save, const std::vector<uint16_t>& selected) {
	for (const uint16_t s : selected) {
		if (save.ships[s].memory.u8(0xE) >= 2) {
			return true;
		}
	}
	return false;
}

bool valid_selection(const SaveGame& save, const std::vector<uint16_t>& selected) {
	for (const uint16_t s : selected) {
		if (s >= save.ships.size()) {
			return false;
		}
	}
	return !selected.empty();
}

}  // namespace

std::vector<int32_t> convoy_choices(const SaveGame& save, const std::vector<uint16_t>& selected) {
	std::vector<int32_t> choices;
	if (!valid_selection(save, selected) || save.player < 0 || size_t(save.player) >= save.merchants.size()) {
		return choices;
	}
	const uint8_t town = save.ships[selected[0]].memory.u8(0x39);
	const uint32_t towns = save.globals.u32(global::kWorldTownCount);
	bool guide = false;
	for (const uint16_t s : selected) {
		guide = guide || can_guide(save, s);
	}
	if (guide) {
		choices.push_back(kPrivateConvoy);
		const MerchantMemory& m = save.merchants[size_t(save.player)].memory;
		const bool licence = town < save.towns.size() && (save.towns[town].memory.u8(0x2C8) & 0x80)
			? (m.u32(0x468) & (1u << (town & 31))) != 0
			: m.u32(0x468) != 0;
		if (licence) {
			choices.push_back(kPublicConvoy);
		}
	}
	bool fit = false;  // For a convoy open to others (0x40C2C3..0x40C2FF).
	for (const uint16_t s : selected) {
		const ShipMemory& m = save.ships[s].memory;
		if (!(m.u16(0x40) < kJoinSailors[m.u8(0xE) & 3]) && int32_t(uint32_t(m.s32(0x18)) * 5u) > m.s32(0x14)) {
			fit = true;
			break;
		}
	}
	const bool big = any_big(save, selected);
	const uint16_t convoys = save.globals.u16(kConvoyCountGlobal);
	for (uint32_t c = save.globals.u16(kConvoyHeadGlobal); c < convoys && c < save.convoys.size(); c = save.convoys[c].u16(0x8)) {
		const GameMemory<0x3C>& r = save.convoys[c];
		const uint16_t state = r.u16(0x12);
		if (r.u16(0x1C) != town || (state != 0 && state != 1 && state != 9 && state != 0xA)) {
			continue;
		}
		const bool open = !(r.u8(0x14) & 2);
		if (!(open && fit) && !event_convoy_takes(save, r, selected[0]) && (open || int(r.u8(0)) != save.player)) {
			continue;
		}
		if (big && r.u8(0x1F) < towns && (save.towns[r.u8(0x1F)].memory.u8(0x2C8) & 1)) {
			continue;
		}
		choices.push_back(int32_t(c));
	}
	return choices;
}

OperationBytes create_convoy_bytes(uint32_t guide, bool is_public, uint32_t destination) {
	return words_operation({0x15, guide, is_public ? 0u : 1u, 5, destination & 0xFF});
}

std::vector<OperationBytes> create_convoy_operations(const SaveGame& save, const std::vector<uint16_t>& selected,
	bool single, bool is_public, uint32_t destination) {
	std::vector<OperationBytes> ops;
	if (!valid_selection(save, selected)) {
		return ops;
	}
	uint32_t guide = selected[0];
	if (!single) {
		guide = 0xFFFFFFFF;
		int32_t best = -1;
		for (const uint16_t s : selected) {
			if (can_guide(save, s) && best < save.ships[s].memory.s32(0x120)) {
				best = save.ships[s].memory.s32(0x120);
				guide = s;
			}
		}
	}
	ops.push_back(create_convoy_bytes(guide, is_public, destination));
	if (!single) {
		for (const uint16_t s : selected) {
			if (s != guide) {
				ops.push_back(words_operation({0x18, guide, s}));
			}
		}
	}
	return ops;
}

std::vector<uint8_t> public_convoy_towns(const SaveGame& save, const std::vector<uint16_t>& selected) {
	std::vector<uint8_t> towns;
	if (!valid_selection(save, selected)) {
		return towns;
	}
	const uint8_t here = save.ships[selected[0]].memory.u8(0x39);
	const bool big = any_big(save, selected);
	for (uint32_t t = 0; t < save.globals.u32(global::kWorldTownCount) && t < save.towns.size(); ++t) {
		if (t != here && !(big && (save.towns[t].memory.u8(0x2C8) & 1))) {
			towns.push_back(uint8_t(t));
		}
	}
	return towns;
}

std::vector<OperationBytes> join_convoy_operations(const SaveGame& save, uint32_t convoy, const std::vector<uint16_t>& selected) {
	std::vector<OperationBytes> ops;
	if (convoy >= save.convoys.size()) {
		return ops;
	}
	const uint32_t guide = save.convoys[convoy].u16(0x10);
	for (const uint16_t s : selected) {
		ops.push_back(words_operation({0x18, guide, s}));
	}
	return ops;
}

OperationBytes leave_convoy_bytes(uint32_t ship, uint32_t convoy, bool guide, bool convoy_selected) {
	if (convoy_selected || guide) {
		return words_operation({0x16, convoy & 0xFFFF});
	}
	return words_operation({0x17, ship & 0xFFFF});
}

OperationBytes leave_convoy_operation(const SaveGame& save, uint16_t ship, bool convoy_selected) {
	const uint16_t convoy = save.ships[ship].memory.u16(0x8);
	const bool guide = convoy < save.convoys.size() && save.convoys[convoy].u16(0x10) == ship;
	return leave_convoy_bytes(ship, convoy, guide, convoy_selected);
}

int32_t market_convoy_units(const SaveGame& save, int convoy, int ware) {
	const uint16_t ships = save.globals.u16(global::kShipCount);
	int32_t sum = 0;
	for (uint32_t i = save.convoys[size_t(convoy)].u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
		const ShipMemory& s = save.ships[i].memory;
		if (int(s.u8(0)) == save.player) {
			sum += s.s32(0x54 + 4 * size_t(ware));
		}
	}
	return ware < 20 ? sum / unit_of(ware) : sum;
}

int32_t market_convoy_used(const SaveGame& save, int convoy) {
	const uint16_t ships = save.globals.u16(global::kShipCount);
	int32_t used = 0;
	for (uint32_t i = save.convoys[size_t(convoy)].u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
		const ShipMemory& s = save.ships[i].memory;
		if (int(s.u8(0)) != save.player) {
			continue;
		}
		used += s.s32(0x118) + s.s32(0x11C);
		const int32_t rest = s.s32(0x11C) % 200;
		if (rest != 0) {
			used = used - rest + 200;
		}
	}
	return used / 200;
}

int32_t market_convoy_capacity(const SaveGame& save, int convoy) {
	const uint16_t ships = save.globals.u16(global::kShipCount);
	int32_t capacity = 0;
	for (uint32_t i = save.convoys[size_t(convoy)].u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
		const ShipMemory& s = save.ships[i].memory;
		if (int(s.u8(0)) == save.player) {
			capacity += s.s32(0x10);
		}
	}
	return capacity;
}

namespace {

bool valid_convoy(const SaveGame& save, int convoy, int ware) {
	return convoy >= 0 && uint32_t(convoy) < save.globals.u16(kConvoyCountGlobal) && size_t(convoy) < save.convoys.size() &&
		ware >= 0 && ware < kTradeWareCount && save.convoys[size_t(convoy)].u16(0x1C) < save.towns.size();
}

}  // namespace

std::optional<OperationBytes> convoy_buy_operation(const SaveGame& save, int convoy, int ware, uint32_t step) {
	if (!valid_convoy(save, convoy, ware)) {
		return std::nullopt;
	}
	const int town = save.convoys[size_t(convoy)].u16(0x1C);
	const int32_t amount = market_buy_amount(step, market_town_units(save, town, ware), ware,
		market_convoy_capacity(save, convoy), market_convoy_used(save, convoy));
	if (amount == 0) {
		return std::nullopt;
	}
	return market_operation(0x1A, amount, ware, convoy, uint16_t(save.player), town);
}

std::optional<OperationBytes> convoy_sell_operation(const SaveGame& save, int convoy, int ware, uint32_t step) {
	if (!valid_convoy(save, convoy, ware)) {
		return std::nullopt;
	}
	const int32_t amount = market_sell_amount(step, market_convoy_units(save, convoy, ware), ware);
	if (amount == 0) {
		return std::nullopt;
	}
	return market_operation(0x19, amount, ware, convoy, uint16_t(save.player), save.convoys[size_t(convoy)].u16(0x1C));
}

std::optional<OperationBytes> office_to_convoy_operation(const SaveGame& save, int convoy, int ware, uint32_t step) {
	if (!valid_convoy(save, convoy, ware)) {
		return std::nullopt;
	}
	const int office = player_office(save, save.convoys[size_t(convoy)].u16(0x1C));
	if (office < 0) {
		return std::nullopt;
	}
	const int32_t amount = market_buy_amount(step, market_office_units(save, office, ware), ware,
		market_convoy_capacity(save, convoy), market_convoy_used(save, convoy));
	if (amount == 0) {
		return std::nullopt;
	}
	OperationBytes op = market_operation(0x1B, amount, convoy, ware, uint16_t(save.player), 0);
	op[0xE] = 1;
	return op;
}

std::optional<OperationBytes> convoy_to_office_operation(const SaveGame& save, int convoy, int ware, uint32_t step) {
	if (!valid_convoy(save, convoy, ware) || player_office(save, save.convoys[size_t(convoy)].u16(0x1C)) < 0) {
		return std::nullopt;
	}
	const int32_t amount = market_sell_amount(step, market_convoy_units(save, convoy, ware), ware);
	if (amount == 0) {
		return std::nullopt;
	}
	return market_operation(0x1B, amount, convoy, ware, uint16_t(save.player), 0);
}

// --- The moneylender ---

namespace {

constexpr uint32_t kEventQueueHead = 0x6DD744;  // u16 ([+0x8] of the event queue 0x6DD73C)
constexpr uint32_t kEventQueueCount = 0x6DD748;  // u16 ([+0xC])

uint16_t get16(const uint8_t* p) {
	uint16_t v;
	std::memcpy(&v, p, 2);
	return v;
}

uint32_t get32(const uint8_t* p) {
	uint32_t v;
	std::memcpy(&v, p, 4);
	return v;
}

void put16(uint8_t* p, uint16_t v) {
	std::memcpy(p, &v, 2);
}

void put32(uint8_t* p, uint32_t v) {
	std::memcpy(p, &v, 4);
}

// The records of the event queue from its head (next [+0x4]) while the
// index is a record, at most as many as there are (the game doesn't count).
template <typename F>
void each_event(const SaveGame& save, F f) {
	const uint32_t count = save.globals.u16(kEventQueueCount);
	uint32_t i = save.globals.u16(kEventQueueHead);
	for (uint32_t walked = 0; i < count && i < save.events.size() && walked < count; ++walked) {
		const uint32_t next = save.events[i].u16(4);
		f(uint16_t(i));
		i = next;
	}
}

uint32_t merchant_total(const SaveGame& save) {
	return save.globals.u16(global::kWorldMerchantCount);
}

bool player_ok(const SaveGame& save) {
	return save.player >= 0 && uint32_t(save.player) < merchant_total(save) && size_t(save.player) < save.merchants.size();
}

// The loan events (0x01) whose byte `at` ([+0x10] the lender, [+0x11] the
// debtor) is the merchant (0x4D9210, 0x4D9100).
std::vector<uint16_t> loans_of(const SaveGame& save, int merchant, size_t at) {
	std::vector<uint16_t> list;
	each_event(save, [&](uint16_t i) {
		if (save.events[i].u16(6) == 1 && int(save.events[i].u8(at)) == merchant) {
			list.push_back(i);
		}
	});
	return list;
}

}  // namespace

int32_t loan_offers(const SaveGame& save, int town, std::array<LoanTerms, 3>& buffer, bool* changed) {
	bool dummy = false;
	bool& moved = changed ? *changed : dummy;
	moved = false;
	if (!player_ok(save) || town < 0 || size_t(town) >= save.towns.size()) {
		return 0;
	}
	const MerchantMemory& m = save.merchants[size_t(save.player)].memory;
	if (m.u8(0x1E) != 0) {
		return -1;
	}
	// cdq; sub; sar 1: half his value, rounded towards 0, less his debts.
	const uint32_t limit = uint32_t(m.s32(0x46C) / 2) - m.u32(0x498);
	if (limit < 2000) {
		return -2;
	}
	uint32_t bonus = uint32_t(m.s32(0x46C) / 500);
	if (bonus > 2000) {
		bonus = 2000;
	}
	const TownMemory& t = save.towns[size_t(town)].memory;
	int32_t reputation = tick::events_detail::fn_4f9580(save, save.player, t.u8(0x2C0));
	reputation = reputation < 0 ? 0 : reputation > 30 ? 30 : reputation;
	int32_t count = 0;
	for (uint32_t k = 0; k < 3; ++k) {
		const uint8_t weeks = t.u8(0x9C9 + 6 * k);
		const uint16_t amount = t.u16(0x9CA + 6 * k);
		if (amount == 0 || weeks == 0) {
			continue;
		}
		uint8_t* o = buffer[size_t(count)].data();
		o[0xD] = uint8_t(save.player);
		o[0xC] = 0xFF;
		o[0xE] = t.u8(0x2C0);
		o[0xB] = 0;
		o[0xF] = uint8_t((k << 4) + 2);
		o[0x5] = m.u8(0x39C + m.u8(0x19));
		o[0x6] = m.u8(0x1C);
		o[0x7] = m.u8(0x1D);
		const uint32_t lent = (uint32_t(amount) + bonus) / 50 * 50;
		if (uint32_t(get16(o + 8)) != lent) {  // The whole amount against the u16 kept.
			moved = true;
			put16(o + 8, uint16_t(lent));
		}
		if (lent > limit) {
			break;
		}
		if (o[0xA] != weeks) {
			moved = true;
			o[0xA] = weeks;
		}
		// fild; fstp (floats); fmul; sqrt of the float as a double; 3000 / it +
		// 15, less the reputation / 3, to a float; __ftol.
		const float a = float(int32_t(get16(o + 8)));
		const float w = float(int32_t(weeks));
		const float root = float(std::sqrt(double(float(double(w) * double(a)))));
		const float interest = float(double(3000.0f) / double(root) + double(15.0f) - double(float(reputation)) / double(3.0f));
		const uint8_t i = uint8_t(tick::ftol(double(interest)));
		if (o[0x4] != i) {
			moved = true;
			o[0x4] = i;
		}
		const float rate = float(double(float(int32_t(o[0x4]))) * double(0.001f) + double(1.0f));
		float repay = float(int32_t(get16(o + 8)));
		for (uint32_t n = o[0xA]; n > 0; --n) {
			repay = float(double(repay) * double(rate));
		}
		if (repay > 65000.0f) {
			break;
		}
		const uint32_t sum = uint32_t(tick::ftol(double(repay))) & 0xFFFF;
		if (get32(o) != sum) {
			moved = true;
			put32(o, sum);
		}
		++count;
	}
	return count;
}

OperationBytes loan_operation(const LoanTerms& terms) {
	OperationBytes op{};
	const uint32_t code = 0x29;
	std::memcpy(op.data(), &code, 4);
	std::memcpy(op.data() + 4, terms.data(), terms.size());
	return op;
}

std::vector<uint16_t> loans_owed(const SaveGame& save, int merchant) {
	return loans_of(save, merchant, 0x11);
}

std::vector<uint16_t> loans_granted(const SaveGame& save, int merchant) {
	return loans_of(save, merchant, 0x10);
}

bool can_pay_off_here(const SaveGame& save, int town, uint16_t loan) {
	if (!player_ok(save) || loan >= save.events.size()) {
		return false;
	}
	const uint8_t where = save.events[loan].u8(0x14);
	if (where < save.globals.u32(global::kWorldTownCount) &&
		tick::facilities_detail::fn_5308a0(save, uint32_t(save.player), where) >= 0) {
		return true;
	}
	const uint32_t ships = save.globals.u16(global::kShipCount);
	bool docked = false;  // The game doesn't stop at the first.
	for (uint32_t s = save.merchants[size_t(save.player)].memory.u16(0xE); s < ships && s < save.ships.size();
		 s = save.ships[s].memory.u16(0x4)) {
		const ShipMemory& m = save.ships[s].memory;
		docked = docked || (!(m.u8(0x136) & 1) && m.u8(0x39) == where && m.u16(0x134) < 4);
	}
	return docked || int(where) == town;
}

OperationBytes pay_off_operation(uint32_t player, uint32_t loan) {
	return words_operation({0x2A, player, loan & 0xFFFF});
}

void fn_51d270(LoanTerms& terms, SaveGame* save) {
	uint8_t* o = terms.data();
	const float a = float(int32_t(get16(o + 8)));
	const float w = float(int32_t(o[0xA]));
	const float product = float(double(w) * double(a));
	const float root = float(std::sqrt(double(1.0f) / double(product)));
	const float factor = float(double(root) * double(300.0f) + double(1.2f));
	const float kind = float(int32_t((o[0xF] & 0xF) + 8));
	o[0x4] = uint8_t(tick::ftol(double(float(double(kind) * double(factor)))));
	const float rate = float(double(float(int32_t(o[0x4]))) * double(0.001f) + double(1.0f));
	float repay = a;
	for (uint32_t n = o[0xA]; n > 0; --n) {
		repay = float(double(repay) * double(rate));
	}
	// fcomp 65000: above it, 65000; fcomp the amount: below it (C0), 65000 too.
	if (repay > 65000.0f || repay < a) {
		put32(o, 0xFDE8);
	} else {
		put32(o, uint32_t(tick::ftol(double(repay))) & 0xFFFF);
	}
	o[0xB] = 0;
	if (o[0xD] != 0xFF) {
		return;
	}
	const uint32_t floor = (uint32_t(o[0x5]) + 1) * 5000;
	const uint32_t sum = get32(o);
	if (floor >= sum || !save) {
		return;
	}
	const uint32_t steps = (sum - floor + 1250) / 1250;
	if ((tick::game_rand(*save) & 0x3FF) < steps * 75) {
		o[0xB] = uint8_t(int32_t(tick::game_rand(*save) & 0xFF) % 78 + 51);
	}
}

int32_t loan_requests(const SaveGame& save, int town, std::array<LoanTerms, 3>& buffer, SaveGame* roll, bool* changed) {
	bool dummy = false;
	bool& moved = changed ? *changed : dummy;
	moved = false;
	if (town < 0 || size_t(town) >= save.towns.size()) {
		return 0;
	}
	const TownMemory& t = save.towns[size_t(town)].memory;
	int32_t count = 0;
	for (uint32_t k = 0; k < 3; ++k) {
		const uint32_t slot = 0x9C6 + 6 * (k + 3);
		if (t.u16(slot + 4) == 0 || t.u8(slot + 3) == 0) {
			continue;
		}
		uint8_t* o = buffer[size_t(count)].data();
		o[0xB] = 0;
		o[0xC] = uint8_t(save.player);
		o[0xE] = t.u8(0x2C0);
		o[0xF] = uint8_t((k << 4) + 0x32);
		if (get16(o + 8) != t.u16(slot + 4)) {
			put16(o + 8, t.u16(slot + 4));
			moved = true;
		}
		if (o[0xA] != t.u8(slot + 3)) {
			o[0xA] = t.u8(slot + 3);
			moved = true;
		}
		const uint32_t before = get32(o);
		fn_51d270(buffer[size_t(count)], roll);
		if (get32(o) != before) {
			moved = true;
		}
		for (uint32_t b = 0; b < 3; ++b) {  // The rank and the names.
			if (o[0x5 + b] != t.u8(slot + b)) {
				o[0x5 + b] = t.u8(slot + b);
				moved = true;
			}
		}
		o[0xD] = 0xFF;
		++count;
	}
	return count;
}

OperationBytes grant_loan_operation(SaveGame& save, LoanTerms request, uint32_t kind) {
	request[0xF] = uint8_t((request[0xF] & 0xF0) | (kind & 0xF));
	fn_51d270(request, &save);
	return loan_operation(request);
}

std::vector<ShareEntry> shares_on_sale(SaveGame& save) {
	std::vector<ShareEntry> list;
	if (!player_ok(save)) {
		return list;
	}
	const uint32_t merchants = merchant_total(save);
	const uint32_t player = uint32_t(save.player);
	std::vector<int32_t> owns(merchants, 0);
	owns[player] = 100;
	bool own_pool = false;
	uint32_t count = 0;
	each_event(save, [&](uint16_t i) {
		GameMemory<0x18>& e = save.events[i];
		if (e.u16(6) != 0x18) {
			return;
		}
		if (e.u8(0x13) > 0 && e.u8(0x11) == 0xFF && e.u8(0x10) < merchants) {
			++count;
			if (e.u8(0x10) == player) {
				if (!own_pool) {
					own_pool = true;
				} else {
					// A second pool of his company: dropped (counted all the same,
					// so the game's list ends in an entry it never fills).
					e.u8(0x10) = 0xFF;
					e.u8(0x12) = 0;
				}
			}
		}
		const uint32_t company = e.u8(0x10);
		if (company >= merchants) {
			return;
		}
		if (company == player) {
			owns[player] -= e.u8(0x12);
		} else if (e.u8(0x11) == player) {
			owns[company] += e.u8(0x12);
		}
	});
	if (count < 1) {
		return list;
	}
	each_event(save, [&](uint16_t i) {
		const GameMemory<0x18>& e = save.events[i];
		if (e.u16(6) != 0x18 || e.u8(0x13) == 0 || e.u8(0x11) != 0xFF || e.u8(0x10) >= merchants ||
			(!own_pool && list.size() >= count)) {
			return;
		}
		ShareEntry entry{};
		entry[0] = e.u8(0x10);
		entry[1] = e.u8(0x13);
		entry[2] = 0xFF;
		entry[3] = uint8_t(player);
		const int32_t value = save.merchants[e.u8(0x10)].memory.s32(0x46C);
		const uint32_t price = value <= 500 ? uint32_t(e.u8(0x13)) * 6
											: uint32_t(e.u8(0x13)) * (uint32_t(value) / 500 + 1) * 6;
		put32(entry.data() + 4, price);
		const int32_t owned = owns[e.u8(0x10)];
		put32(entry.data() + 8, uint32_t(owned) <= 100 ? uint32_t(owned) : 0);
		list.push_back(entry);
	});
	// A dropped pool's entry is never filled: the heap's zeros in the oracle.
	list.resize(std::max<size_t>(list.size(), count));
	return list;
}

std::vector<ShareEntry> shares_to_sell(const SaveGame& save) {
	std::vector<ShareEntry> list;
	if (!player_ok(save)) {
		return list;
	}
	const uint32_t merchants = merchant_total(save);
	const uint32_t player = uint32_t(save.player);
	const uint32_t now = save.globals.u32(global::kWorldTick);
	int32_t own = 100;
	uint32_t pool_free = 0;  // When his pool may sell again: its last change + 7 days.
	each_event(save, [&](uint16_t i) {
		const GameMemory<0x18>& e = save.events[i];
		if (e.u16(6) != 0x18) {
			return;
		}
		if (e.u8(0x11) == player) {
			return;  // A holding of his (counted below).
		}
		if (e.u8(0x10) == player) {
			own -= e.u8(0x12);
			if (e.u8(0x11) == 0xFF) {
				pool_free = e.u32(8) + 0x700;
			}
		}
	});
	ShareEntry first{};
	first[0] = uint8_t(player);
	first[2] = uint8_t(player);
	first[3] = 0xFF;
	put32(first.data() + 8, uint32_t(own));
	if (uint32_t(own) > 0x37 && pool_free < now) {
		const MerchantMemory& m = save.merchants[player].memory;
		// The tick's float / 1987, its low 4 bits / 3 · 5: 0..25.
		const float f = float(double(float(int64_t(now >> 10))) / double(1987.0f));
		uint32_t bits;
		std::memcpy(&bits, &f, 4);
		const uint32_t n = (bits & 0xF) / 3 * 5;
		first[1] = uint8_t(n > 0x19 ? 0x19 : n);
		if (uint32_t(own) - 0x37 < first[1]) {
			first[1] = uint8_t(own - 0x37);
		}
		const int32_t value = m.s32(0x46C);
		put32(first.data() + 4, value > 100 ? uint32_t(first[1]) * uint32_t(value / 100) : uint32_t(first[1]));
	}
	list.push_back(first);
	each_event(save, [&](uint16_t i) {
		const GameMemory<0x18>& e = save.events[i];
		if (e.u16(6) != 0x18 || e.u8(0x11) != player) {
			return;
		}
		ShareEntry entry{};
		entry[0] = e.u8(0x10);
		entry[1] = e.u8(0x13);
		entry[2] = uint8_t(player);
		entry[3] = 0xFF;
		put32(entry.data() + 8, e.u8(0x12));
		uint32_t each = 1000;
		if (e.u8(0x10) < merchants) {
			const uint32_t value = save.merchants[e.u8(0x10)].memory.u32(0x46C);
			each = (value > 100 ? value / 100 : 1) + 1;
			put32(entry.data() + 4, uint32_t(e.u8(0x13)) * each);
		} else {
			put32(entry.data() + 4, uint32_t(entry[1]) * each);
		}
		list.push_back(entry);
	});
	return list;
}

OperationBytes shares_operation(const ShareEntry& entry) {
	OperationBytes op{};
	const uint32_t code = 0x2B;
	std::memcpy(op.data(), &code, 4);
	std::memcpy(op.data() + 4, entry.data(), entry.size());
	return op;
}

bool can_buy_shares(const SaveGame& save, const ShareEntry& entry) {
	if (!player_ok(save)) {
		return false;
	}
	const int32_t money = save.merchants[size_t(save.player)].memory.s32(0);
	const uint32_t price = get32(entry.data() + 4);
	return fix_game_bugs() ? int64_t(money) >= int64_t(price) : uint32_t(money) >= price;
}

uint32_t next_dividend_tick(const SaveGame& save) {
	const uint32_t now = save.globals.u32(global::kWorldTick);
	uint32_t year = uint32_t(save.globals.u16(global::kWorldYear)) * 365;
	for (uint32_t k = 3;; k += 3) {
		if (k > 9) {
			k = 0;
			year += 0x16D;
		}
		const uint32_t tick = (uint32_t(tick::kMonthStartTable[k]) + year) << 8;
		if (tick > now) {
			return tick;
		}
	}
}

// --- Building ---

namespace {

// 0x672FC0: the town view's picture of each building type.
constexpr uint8_t kBuildCatalog[0x39] = {0x2C, 0x1B, 0x0B, 0x08, 0x5D, 0x57, 0x58, 0x5B, 0x59, 0x53, 0x56, 0x64, 0x5A,
	0x5E, 0x60, 0x55, 0x5C, 0x63, 0x62, 0x61, 0x54, 0x51, 0x51, 0x51, 0x50, 0x50, 0x50, 0x2A, 0x2A, 0x2A, 0x66, 0x04,
	0x03, 0x0B, 0x07, 0x00, 0x02, 0x05, 0x09, 0x33, 0x52, 0x65, 0x67, 0x68, 0x69, 0x37, 0x01, 0x5F, 0x29, 0x29, 0x29,
	0x29, 0x2F, 0x2F, 0x2E, 0x2F, 0x2F};
// 0x672F84 (s8): the class of each building type; 0x672F74 and 0x672F64: the
// footprint of each class, its cells one way (the inner walk of the marks)
// and the other (the outer one).
constexpr int8_t kBuildClass[0x39] = {3, 7, 0, 0, 4, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 10, 10, 3, 2, 3, 0, 0, 3, 0, 5, 3, 2, 2, 3, 6, 7, 6, 6, 6, 6, 6, 6, 8, 9, 8, 9};
constexpr int8_t kClassWidth[16] = {3, 2, 4, 3, 5, 1, 2, 6, 6, 2, 4, 5, 3, 1, 1, 1};
constexpr int8_t kClassDepth[16] = {2, 3, 3, 4, 4, 1, 2, 4, 2, 6, 6, 5, 3, 1, 1, 1};
// 0x66EF58: the group of each business (types 4..0x14): 0 the window's
// category 3, otherwise its category 4.
constexpr uint8_t kBusinessGroup[0x15] = {1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0};

// The town's record r (0x51F1F0: none from [0x776] on).
const BuildingRecord* town_record(const SavedTown& st, uint32_t r) {
	return r < st.memory.u16(0x776) && r < st.records.size() ? &st.records[r] : nullptr;
}

// The grid's cell (x, y): [0x80C][x · [0x7AC] + y] (0 out of it; the game
// doesn't look).
uint8_t grid_cell(const SavedTown& st, int64_t x, int64_t y) {
	const int64_t i = int64_t(st.memory.s32(0x7AC)) * x + y;
	return i >= 0 && size_t(i) < st.grid.size() ? st.grid[size_t(i)] : 0;
}

// 0x4FD020 (on town_class1: type, x, y, flag): for the houses and the
// warehouse, which orientations fit the cell (1: its bits 0xA1, 2: 0xA2);
// for the others, whether the cell has the class's bit and 0x20 (and 0x80,
// inside the walls, with the flag); a class from 6 on never fits.
uint32_t fn_4fd020(const SavedTown& st, uint32_t type, uint8_t x, uint8_t y, bool flag) {
	const uint8_t c = grid_cell(st, x, y);
	if (type >= 0x15 && type <= 0x1E) {
		return ((c & 0xA1) == 0xA1 ? 1u : 0u) + ((c & 0xA2) == 0xA2 ? 2u : 0u);
	}
	const int8_t cls = type < 0x39 ? kBuildClass[type] : 0;
	if (cls >= 6) {
		return 0;
	}
	uint8_t mask = uint8_t((1u << cls) | 0x20u);
	if (flag) {
		mask |= 0x80;
	}
	return (c & mask) == mask ? 1u : 0u;
}

// The marks' footprint (0x4FD1A8, 0x4FD3B4...): from the cell (x, y) of the
// map (y · w + x), `outer` times `inner` cells: each inner step goes up a row
// to (x + parity, y − 1), each outer one to (x + parity − 1, y − 1). Out of the
// map (the game would write past it) nothing.
void mark_footprint(std::vector<uint8_t>& map, int64_t w, int64_t x, int64_t y, int inner, int outer) {
	int64_t row = y * w + x;
	int64_t parity = y & 1;
	for (int o = 0; o < outer; ++o) {
		int64_t at = row;
		int64_t p = parity;
		for (int i = 0; i < inner; ++i) {
			if (at >= 0 && size_t(at) < map.size()) {
				map[size_t(at)] = 1;
			}
			at += p - w;
			p = (p - 1) & 1;
		}
		row += parity - w - 1;
		parity = (parity - 1) & 1;
	}
}

void mark(std::vector<uint8_t>& map, int64_t i) {
	if (i >= 0 && size_t(i) < map.size()) {
		map[size_t(i)] = 1;
	}
}

// A tower's four cells (0x524317..0x52433E): (x, y), (x, y − 1), the one at its
// side and (x, y − 2).
void mark_tower(std::vector<uint8_t>& map, int64_t w, const BuildingRecord& r) {
	int64_t i = int64_t(r.y) * w + r.x;
	mark(map, i);
	i -= w;
	mark(map, i);
	mark(map, (r.y & 1) ? i + 1 : i - 1);
	mark(map, i - w);
}

// The town's fortification list ([0x768], next [+0x4]).
template <typename F>
void each_fortification(const SavedTown& st, F f) {
	const uint32_t count = st.memory.u16(0x776);
	for (uint32_t i = st.memory.u16(0x768); i < count && i < st.records.size();) {
		const BuildingRecord& r = st.records[i];
		i = r.next;
		if (f(r)) {
			return;
		}
	}
}

// 0x5241E0 (on the town, a type from 0x31): the towers' and the pitch shoot's
// marks. Only the gate bombard's clears [0x7A4].
std::vector<uint8_t> fn_5241e0(SaveGame& save, uint32_t town, int type) {
	SavedTown& st = save.towns[town];
	TownMemory& t = st.memory;
	const int64_t w = t.s32(0x7A8);
	std::vector<uint8_t> map(size_t(uint32_t(t.s32(0x7AC)) * uint32_t(w)), 0);
	if (type == 0x32) {
		// Beside each gate, two sites: (x ∓ 1, y + 2) and (x ± 3, y − 6), the
		// sign by its side (0x37 or 0x34, 0x38 or 0x35). The game keeps them on
		// its stack, room for 24.
		std::vector<std::pair<int64_t, int64_t>> sites;
		each_fortification(st, [&](const BuildingRecord& r) {
			if (r.type == 0x38 || r.type == 0x35) {
				sites.push_back({int64_t(r.x) + 1, int64_t(r.y) + 2});
				sites.push_back({int64_t(r.x) - 3, int64_t(r.y) - 6});
			} else if (r.type == 0x37 || r.type == 0x34) {
				sites.push_back({int64_t(r.x) - 1, int64_t(r.y) + 2});
				sites.push_back({int64_t(r.x) + 3, int64_t(r.y) - 6});
			}
			return false;
		});
		// A wall piece on a site gets the marks, and the site goes (the last in
		// its place).
		each_fortification(st, [&](const BuildingRecord& r) {
			if (r.type != 0x36) {
				return false;
			}
			for (size_t k = 0; k < sites.size(); ++k) {
				if (sites[k].first == r.x && sites[k].second == r.y) {
					mark_tower(map, w, r);
					sites[k] = sites.back();
					sites.pop_back();
					break;
				}
			}
			return false;
		});
		t.u32(0x7A4) = 0;
	} else if (type <= 0x33) {
		// The port cannon on a port bombard, the gate cannon on a gate bombard.
		each_fortification(st, [&](const BuildingRecord& r) {
			if (r.type == type - 1) {
				mark_tower(map, w, r);
			}
			return false;
		});
	} else if (type == 0x34 || type == 0x35) {
		// The gates not in works: a strip of 6 · 2 cells along each.
		each_fortification(st, [&](const BuildingRecord& r) {
			if (r.pending != 0) {
				return false;
			}
			if (r.type == 0x37) {
				int64_t e = int64_t(r.y) * w + r.x - 1;
				for (int64_t k = 0; k < 6; ++k) {
					++e;
					mark(map, e);
					e += ((int64_t(r.y) + k) & 1) - w - 1;
					mark(map, e);
				}
			} else if (r.type == 0x38) {
				int64_t e = int64_t(r.y) * w + r.x + 1;
				for (int64_t k = 0; k < 6; ++k) {
					--e;
					mark(map, e);
					e += ((int64_t(r.y) + k) & 1) - w;
					mark(map, e);
				}
			}
			return false;
		});
	}
	return map;
}

// 0x4FD0D0 (on town_class1: type, flag): the cells whose bits fit the type
// (by the case of 0x4FD460), each with its class's footprint; the shipyard and
// the repair dock at their fixed points. A class from 0x10 on: nothing, and
// [0x7A4] untouched; the types of the case 0 (the streets, the well...):
// nothing.
std::vector<uint8_t> fn_4fd0d0(SaveGame& save, uint32_t town, int type, bool flag) {
	SavedTown& st = save.towns[town];
	TownMemory& t = st.memory;
	const int8_t cls = type >= 0 && type < 0x39 ? kBuildClass[type] : 0;
	if (cls >= 0x10) {
		return {};
	}
	const int64_t w = t.s32(0x7A8);
	const int64_t h = t.s32(0x7AC);
	std::vector<uint8_t> map(size_t(uint32_t(w) * uint32_t(h)), 0);
	t.u32(0x7A4) = 0;
	const int inner = kClassWidth[cls];
	const int outer = kClassDepth[cls];
	uint32_t mask = 0;
	uint32_t value = 0;
	auto cell_class = [&](uint32_t v) {
		mask = 0x3F;
		value = v;
	};
	switch (type) {
	case 0x01:
	case 0x2E:
		// 0x4FD17E: the repair dock's or the shipyard's fixed point.
		if (type == 0x2E) {
			mark_footprint(map, w, t.u8(0x808), t.u8(0x809), inner, outer);
		} else {
			mark_footprint(map, w, t.u8(0x80A), t.u8(0x80B), inner, outer);
		}
		return map;
	case 0x03: cell_class(0x1D); break;
	case 0x05: cell_class(0x0E); break;
	case 0x1F: cell_class(0x19); break;
	case 0x20: cell_class(0x1B); break;
	case 0x21: cell_class(0x14); break;
	case 0x22: cell_class(0x1A); break;
	case 0x23: cell_class(0x16); break;
	case 0x24: cell_class(0x17); break;
	case 0x25: cell_class(0x15); break;
	case 0x27: cell_class(0x18); break;
	case 0x2F: cell_class(0x0F); break;
	case 0x30: cell_class(0x1C); break;
	case 0x26:
	case 0x29:
	case 0x2A:
	case 0x2B:
	case 0x2C:
		mask = value = (1u << cls) | 0xA0u;
		break;
	case 0x2D:
	case 0x34:
	case 0x35:
		// 0x4FD366: the arguments as they are, the type the mask and the flag
		// the value.
		mask = uint32_t(type);
		value = flag ? 1 : 0;
		break;
	default:
		if (type == 0x04 || (type >= 0x06 && type <= 0x14)) {
			mask = value = (1u << cls) | 0x20u | (flag ? 0x80u : 0u);
			break;
		}
		return {};  // 0x4FD169: the case 0.
	}
	for (size_t i = st.grid.size(); i-- > 0;) {
		if ((st.grid[i] & mask) == value) {
			mark_footprint(map, w, int64_t(i) / h, int64_t(i) % h, inner, outer);
		}
	}
	return map;
}

}  // namespace

uint8_t build_catalog(int type) {
	return type >= 0 && type < 0x39 ? kBuildCatalog[type] : 0;
}

BuildMenu build_menu(const SaveGame& save, int town_arg, uint32_t player, int category) {
	BuildMenu menu;
	menu.types.fill(-1);
	menu.category = category;
	const uint32_t towns = save.globals.u32(global::kWorldTownCount);
	const uint32_t town = uint32_t(town_arg) < towns ? uint32_t(town_arg) : 0;  // The view's town [+0xC324].
	const SavedTown& st = save.towns[town];
	const TownMemory& t = st.memory;
	const bool mayor = t.u8(0x6F1) == player;
	const uint32_t bits = t.u32(0x76C);
	size_t n = 0;
	auto add = [&](int32_t type) {
		if (n < menu.types.size()) {
			menu.types[n++] = type;
		}
	};
	// The businesses of a group (0x47B123, 0x47B1AD): their productivity there
	// over 400, up to 8 buttons.
	auto businesses = [&](bool second) {
		for (int type = 4; type < 0x15 && n < 8; ++type) {
			if ((kBusinessGroup[type] != 0) == second && t.u16(0x888 + 0x10 * uint32_t(type - 4)) > 400) {
				add(type);
			}
		}
	};
	switch (uint32_t(category)) {
	case kBuildHousing: {
		if ((t.u32(0x2C8) & 0xE0) == 0 && (!mayor || bits == 0)) {
			break;
		}
		if (tick::facilities_detail::fn_5308a0(save, player, town) >= 0) {
			add(0x1E);
			add(0x1B);
			add(0x18);
			add(0x15);
			break;
		}
		const uint32_t merchants = save.globals.u16(global::kWorldMerchantCount);
		const MerchantMemory& m = save.merchants[player < merchants ? player : 0].memory;
		if (m.u8(0x39C + town) != 0 || mayor) {
			add(0);  // «trading office».
		} else {
			menu.reputation_text = true;
		}
		break;
	}
	case kBuildPublic: {
		const bool walls = (bits & 0x10) != 0;
		if (walls) {
			add(2);
			add(0x28);
		}
		if (mayor) {
			if (!(bits & 0x1)) {
				add(0x23);
			} else if (!(bits & 0x2)) {
				add(0x20);
			} else if (!(bits & 0x4)) {
				add(3);
			} else if (!(bits & 0x8)) {
				add(0x21);
			} else if (walls) {
				if (!(bits & 0x20)) {
					add(0x24);
				}
				if (!(bits & 0x40)) {
					add(0x01);
				}
				if ((bits & 0x27F) == 0x7F) {
					add(0x1F);
				}
			}
		}
		if (bits & 0x200) {
			if ((bits & 0x600) == 0x200) {
				add(0x2A);
			}
			if (!(bits & 0x1000)) {
				add(0x2B);
			}
			add(0x29);
			add(0x2C);
		}
		break;
	}
	case kBuildFortifications:
		if (!mayor) {
			menu.category = kBuildRaw;
			businesses(false);
			break;
		}
		if (bits & 0x10) {
			uint8_t port = 0;  // 0x30 and 0x31.
			uint8_t gate = 0;  // 0x32 and 0x33.
			uint32_t have = 0;
			each_fortification(st, [&](const BuildingRecord& r) {
				switch (r.type) {
				case 0x30: have |= 1; ++port; break;
				case 0x31: ++port; break;
				case 0x32: have |= 2; ++gate; break;
				case 0x33: ++gate; break;
				case 0x37:
				case 0x38: have |= 4; break;
				default: break;
				}
				return false;
			});
			if (port < 3) {
				add(0x30);
			}
			if (have & 1) {
				add(0x31);
			}
			if (gate < 6) {
				add(0x32);
			}
			if (have & 2) {
				add(0x33);
			}
			if (have & 4) {
				add(0x34);
			}
		}
		if (t.u16(0x766) < t.u16(0x776)) {
			add(0x36);  // «Town wall»: its pieces pending.
		}
		break;
	case kBuildRaw:
		businesses(false);
		break;
	default:
		menu.category = kBuildCrafts;
		businesses(true);
		break;
	}
	return menu;
}

std::array<bool, 5> build_tabs(const SaveGame& save, int town_arg, uint32_t player) {
	std::array<bool, 5> tabs{};
	const uint32_t towns = save.globals.u32(global::kWorldTownCount);
	const uint32_t town = uint32_t(town_arg) < towns ? uint32_t(town_arg) : 0;
	const TownMemory& t = save.towns[town].memory;
	const bool mayor = t.u8(0x6F1) == player;
	const bool office = tick::facilities_detail::fn_5308a0(save, player, town) >= 0;
	const uint32_t bits = t.u32(0x76C);
	if (t.u32(0x2C8) & 0xE0) {
		tabs = {true, office || mayor, mayor, office, office};
	} else if (mayor) {
		tabs = {office || bits != 0, true, (bits & 0x8) != 0, office, office};
	} else if (bits & 0x100) {
		tabs = {true, office, false, office, office};
	}
	return tabs;
}

BuildChoice build_choice(SaveGame& save, int town, uint32_t player, int type) {
	BuildChoice c;
	c.type = type;
	c.cost.u16(0x3C) = uint16_t(type);
	c.cost.u16(0x3E) = uint16_t(player);
	const SavedTown& st = save.towns[size_t(town)];
	const TownMemory& t = st.memory;
	if (type < 0x36) {
		c.can = tick::construction_detail::fn_51f3a0(save, town, c.cost);
		if (type == 2) {
			c.action = BuildAction::Street;
			c.button = c.can && t.u16(0x792) > t.u16(0x790);
		} else if (type == 0x28) {
			c.action = BuildAction::Well;
			c.button = c.can && int32_t(t.u8(0x789)) * 500 < t.s32(0x2D4);
		} else {
			c.action = c.can ? BuildAction::Place : BuildAction::None;
		}
		return c;
	}
	// The wall: the next piece pending ([0x766]); without one, the window
	// shows a whole side's cost (0x5226C0) and its button sends nothing.
	const BuildingRecord* r = town_record(st, t.u16(0x766));
	if (!r) {
		return c;
	}
	c.type = r->type;
	c.cost.u16(0x3C) = r->type;
	c.can = tick::construction_detail::fn_51f3a0(save, town, c.cost);
	c.action = BuildAction::Wall;
	c.button = c.can && !(t.u32(0x2C8) & 0x10);
	return c;
}

bool fn_47b8c0(const SaveGame& save, uint32_t town, uint8_t x, uint8_t y, int& type, BuildGhost& ghost) {
	if (town >= save.globals.u32(global::kWorldTownCount)) {
		return false;
	}
	const SavedTown& st = save.towns[town];
	const TownMemory& t = st.memory;
	const uint8_t cell = grid_cell(st, x, y);
	// Outside the walls while the town is besieged.
	const bool outside = (t.u32(0x2C8) & 0x10) && cell < 0x80;
	if (uint32_t(type) > 0x35) {
		return false;
	}
	auto cell_is = [&](uint8_t v) { return (cell & 0x3F) == v; };
	// The houses' and the warehouse's orientation by what fits: both, the
	// town's [0x774] > 0 picks 0xE0 ([0x67792F]), otherwise 0x20 ([0x677929]).
	auto orientation = [&](uint32_t r) -> uint32_t {
		return (r == 3 && t.s8(0x774) > 0) || r == 2 ? 0xE0 : 0x20;
	};
	// A fortification of `kind` at (x, y).
	auto tower_at = [&](uint8_t kind) {
		bool found = false;
		each_fortification(st, [&](const BuildingRecord& r) {
			found = r.type == kind && r.x == x && r.y == y;
			return found;
		});
		return found;
	};
	switch (type) {
	case 0x00: return x == t.u8(0x802) && y == t.u8(0x803);
	case 0x01: return x == t.u8(0x80A) && y == t.u8(0x80B);
	case 0x03: return cell_is(0x1D);
	case 0x05: return cell_is(0x0E);
	case 0x1F: return cell_is(0x19);
	case 0x20: return cell_is(0x1B);
	case 0x21: return cell_is(0x14);
	case 0x22: return cell_is(0x1A);
	case 0x23: return cell_is(0x16);
	case 0x24: return cell_is(0x17);
	case 0x25: return cell_is(0x15);
	case 0x27: return cell_is(0x18);
	case 0x2F: return cell_is(0x0F);
	case 0x30: return cell_is(0x1C);
	case 0x26:
	case 0x29:
	case 0x2A:
	case 0x2B:
	case 0x2C:
		return fn_4fd020(st, uint32_t(type), x, y, true) == 1;
	case 0x2E: return x == t.u8(0x808) && y == t.u8(0x809);
	case 0x1E: {
		const uint32_t r = fn_4fd020(st, 0x1E, x, y, true);
		ghost = {kBuildCatalog[0x1E], orientation(r)};
		return r != 0;
	}
	case 0x31: return tower_at(0x30);
	case 0x33: return tower_at(0x32);
	case 0x32: {
		// Not on a gate bombard; on one of the two sites beside a gate.
		bool fits = false;
		bool taken = false;
		each_fortification(st, [&](const BuildingRecord& r) {
			const int rx = r.x;
			const int ry = r.y;
			if (r.type == 0x32 && r.x == x && r.y == y) {
				taken = true;
				return true;
			}
			if (r.type == 0x37 || r.type == 0x34) {
				if (rx - 1 == x) {
					fits = fits || ry + 2 == y;
				} else if (rx + 3 == x && ry - 6 == y) {
					fits = true;
				}
			} else if (r.type == 0x38 || r.type == 0x35) {
				if (rx + 1 == x) {
					fits = fits || ry + 2 == y;
				} else if (rx - 3 == x && ry - 6 == y) {
					fits = true;
				}
			}
			return false;
		});
		return fits && !taken;
	}
	case 0x34:
	case 0x35: {
		// A gate not in works: the window's type follows the gate (0x37: 0x34,
		// 0x38: 0x35), with its picture by the gate's orientation.
		bool found = false;
		each_fortification(st, [&](const BuildingRecord& r) {
			if (r.pending != 0 || (r.type != 0x37 && r.type != 0x38) || r.x != x || r.y != y) {
				return false;
			}
			found = true;
			const int wanted = r.type == 0x38 ? 0x35 : 0x34;
			if (wanted != type) {
				type = wanted;
				ghost = {kBuildCatalog[wanted], uint32_t(r.param) << 16};
			}
			return true;
		});
		return found;
	}
	default:
		break;
	}
	if (type == 0x04 || (type >= 0x06 && type <= 0x14)) {
		return !outside && fn_4fd020(st, uint32_t(type), x, y, false) != 0;
	}
	if (type >= 0x15 && type <= 0x1D) {
		const uint32_t r = fn_4fd020(st, uint32_t(type), x, y, true);
		ghost = {kBuildCatalog[type], orientation(r) << 16};
		return r != 0;
	}
	return false;  // The streets, the well and the monument (0x2D) aren't placed.
}

BuildGhost build_ghost(const SaveGame& save, uint32_t player, int type) {
	BuildGhost g;
	g.catalog = build_catalog(type);
	if (g.catalog == 0x2C) {
		const uint32_t merchants = save.globals.u16(global::kWorldMerchantCount);
		const MerchantMemory& m = save.merchants[player < merchants ? player : 0].memory;
		const uint8_t rank = m.u8(0x39C + m.u8(0x19));  // 0x4F74F0.
		g.key = rank < 4 ? rank : (rank > 4 ? 4 : 3);
	} else if (type == 0x1F || g.catalog == 0x29) {
		g.key = 0;
	} else if (type == 0x1E) {
		g.key = 0x20;
	} else {
		g.key = 0x20u << 16;  // [0x677929] << 16, also the pitch shoot's.
	}
	return g;
}

uint8_t build_param(const BuildGhost& ghost) {
	switch (ghost.catalog) {
	case 0x2A:
	case 0x50:
	case 0x51:
	case 0x2F:
		return uint8_t(ghost.key >> 16);
	case 0x66:
		return uint8_t(ghost.key);
	default:
		return 0;
	}
}

OperationBytes build_operation(uint32_t player, uint32_t town, uint8_t x, uint8_t y, uint8_t catalog, uint8_t param) {
	OperationBytes op{};
	op[0] = 0x21;
	op[4] = uint8_t(player);
	op[5] = uint8_t(town);
	op[6] = x;
	op[7] = y;
	op[8] = catalog;
	op[9] = param;
	return op;
}

std::optional<OperationBytes> place_operation(const SaveGame& save, uint32_t player, uint32_t town, uint8_t x,
	uint8_t y, int& type, BuildGhost& ghost) {
	if (!fn_47b8c0(save, town, x, y, type, ghost)) {
		return std::nullopt;
	}
	return build_operation(player, town, x, y, ghost.catalog, build_param(ghost));
}

OperationBytes street_operation(uint32_t town, uint32_t player) {
	return words_operation({0x25, town, player});
}

OperationBytes well_bytes(uint32_t player, uint32_t town, uint8_t x, uint8_t y) {
	OperationBytes op{};
	op[0] = 0x22;
	op[4] = uint8_t(player);
	op[5] = uint8_t(town);
	op[6] = x;
	op[7] = y;
	op[8] = 0x28;
	return op;
}

OperationBytes well_operation(const SaveGame& save, uint32_t town, uint32_t player) {
	uint8_t x = 0;
	uint8_t y = 0;
	tick::construction_detail::fn_5250d0(save, int(town), x, y);
	return well_bytes(player, town, x, y);
}

OperationBytes wall_operation(uint32_t player, uint32_t town, int next_type) {
	return words_operation({0x24, player, town, next_type != 0x36 ? 1u : 0u});
}

std::vector<uint8_t> build_marks(SaveGame& save, uint32_t town, int type) {
	SavedTown& st = save.towns[town];
	TownMemory& t = st.memory;
	if (type == 0) {
		// 0x4FD5E0: the office at its fixed point.
		const int64_t w = t.s32(0x7A8);
		std::vector<uint8_t> map(size_t(uint32_t(w) * uint32_t(t.s32(0x7AC))), 0);
		t.u32(0x7A4) = 0;
		mark_footprint(map, w, t.u8(0x802), t.u8(0x803), 3, 4);
		return map;
	}
	if (type >= 0x15 && type <= 0x1E) {
		// 0x4FD4A0: each orientation that fits.
		const int64_t w = t.s32(0x7A8);
		const int64_t h = t.s32(0x7AC);
		std::vector<uint8_t> map(size_t(uint32_t(w) * uint32_t(h)), 0);
		t.u32(0x7A4) = 0;
		for (size_t i = st.grid.size(); i-- > 0;) {
			const uint8_t c = st.grid[i];
			if ((c & 0xA1) == 0xA1) {
				mark_footprint(map, w, int64_t(i) / h, int64_t(i) % h, 3, 2);
			}
			if ((c & 0xA2) == 0xA2) {
				mark_footprint(map, w, int64_t(i) / h, int64_t(i) % h, 2, 3);
			}
		}
		return map;
	}
	if (type < 0x31) {
		return fn_4fd0d0(save, town, type, (t.u32(0x2C8) & 0x10) && type != 5);
	}
	return fn_5241e0(save, town, type);
}

bool can_demolish(const SaveGame& save, uint32_t town, uint32_t record) {
	if (town >= save.towns.size()) {
		return false;
	}
	const SavedTown& st = save.towns[town];
	const BuildingRecord* r = town_record(st, record);
	if (!r) {
		return false;
	}
	if ((st.memory.u32(0x2C8) & 0x10) && grid_cell(st, r->x, r->y) < 0x80) {
		return false;
	}
	uint32_t tick = 0;
	int32_t price = 0;
	tick::events_detail::fn_4dafe0(save, town, record, tick, price);
	return tick == 0xFFFFFFFF;
}

int own_building_at(const SaveGame& save, uint32_t town, int x, int y, uint32_t merchant) {
	if (town >= save.towns.size()) {
		return -1;
	}
	const SavedTown& st = save.towns[town];
	// Whether the building of record r covers the cell: its footprint walked as
	// mark_footprint walks it, in cells.
	auto covers = [&](const BuildingRecord& r) {
		int inner = 1;
		int outer = 1;
		if (r.type >= 0x15 && r.type <= 0x1E) {
			inner = r.param == 0xE0 ? 2 : 3;  // 0x4FD4A0: 0xA1 (0x20) 3 · 2, 0xA2 (0xE0) 2 · 3.
			outer = r.param == 0xE0 ? 3 : 2;
		} else if (r.type < 0x39 && kBuildClass[r.type] < 16) {
			inner = kClassWidth[kBuildClass[r.type]];
			outer = kClassDepth[kBuildClass[r.type]];
		}
		int row_x = r.x;
		int row_y = r.y;
		for (int o = 0; o < outer; ++o) {
			int cx = row_x;
			int cy = row_y;
			for (int i = 0; i < inner; ++i) {
				if (cx == x && cy == y) {
					return true;
				}
				cx += cy & 1;
				--cy;
			}
			row_x += (row_y & 1) - 1;
			--row_y;
		}
		return false;
	};
	auto search = [&](uint32_t head, bool owned) {
		const uint32_t count = st.memory.u16(0x776);
		for (uint32_t i = head; i < count && i < st.records.size();) {
			const BuildingRecord& r = st.records[i];
			if ((!owned || r.owner == merchant) && covers(r)) {
				return int(i);
			}
			i = r.next;
		}
		return -1;
	};
	const int office = tick::facilities_detail::fn_5308a0(save, merchant, town);
	if (office >= 0) {
		const OfficeMemory& o = save.offices[size_t(office)].memory;
		for (const uint32_t head : {uint32_t(o.u16(0x2D0)), uint32_t(o.u16(0x2CE))}) {
			if (const int r = search(head, false); r >= 0) {
				return r;
			}
		}
	}
	return search(st.memory.u16(0x762), true);
}

OperationBytes demolish_operation(uint32_t town, uint32_t merchant, uint32_t x, uint32_t y) {
	return words_operation({0x26, town, merchant, x, y});
}

bool can_auction(const SaveGame& save, uint32_t town, uint32_t merchant, uint32_t record) {
	if (!can_demolish(save, town, record) || merchant >= save.merchants.size()) {
		return false;
	}
	const SavedTown& st = save.towns[town];
	return st.records[record].pending == 0 && (save.merchants[merchant].memory.u32(0x468) & (1u << (town & 31))) &&
		(st.memory.u32(0x76C) & 0x2000);
}

int32_t auction_reserve_price(const SaveGame& save, uint32_t town, uint32_t record) {
	const SavedTown& st = save.towns[town];
	const BuildingRecord* r = town_record(st, record);
	return r ? tick::events_detail::fn_4daf10(save, town, record, (grid_cell(st, r->x, r->y) >> 7) != 0) : 0;
}

OperationBytes auction_operation(uint32_t town, uint32_t merchant, uint32_t x, uint32_t y) {
	return words_operation({0x28, town, merchant, x, y});
}

OperationBytes letter_settings_operation(uint32_t player, const std::array<uint8_t, 3>& days, bool event_sound,
	uint8_t notices) {
	// 0x535740 clears the buffer; 0x4096DB writes the u16 player, the three days ([+0xCD..+0xCF] of the window),
	// the event sound ([+0xC8] != 0) and the notices ([+0xCC]).
	OperationBytes bytes{};
	const uint32_t code = 0x70;
	std::memcpy(bytes.data(), &code, 4);
	const uint16_t who = uint16_t(player);
	std::memcpy(bytes.data() + 4, &who, 2);
	bytes[6] = days[0];
	bytes[7] = days[1];
	bytes[8] = days[2];
	bytes[9] = event_sound ? 1 : 0;
	bytes[0xE] = notices;
	return bytes;
}

OperationBytes letters_check_operation(uint32_t player, bool repair) {
	return words_operation({0x71, player, repair ? 1u : 0u});
}

CitizenStats citizen_stats(const SaveGame& save, uint32_t town) {
	CitizenStats out;
	if (town >= save.towns.size()) {
		return out;
	}
	const TownMemory& t = save.towns[town].memory;
	for (size_t k = 0; k < 4; ++k) {
		out.residents[k] = t.u32(0x2D8 + 4 * k);
		if (k == 3) {
			break;  // The beggars: only their number.
		}
		// residents · 100 > dwellings · 95, as signed ints (lea; shl: the product wraps).
		if (int32_t(out.residents[k] * 100u) > int32_t(uint32_t(t.u16(0x2F8 + 2 * k)) * 95u)) {
			out.shortage = true;
		}
		const int32_t value = t.s16(0x300 + 2 * k);
		const float v = float(value);  // fild; fstp a float; fcomp the float thresholds
		out.mood[k] = v <= -10.5f ? 0 : v <= -0.5f ? 1 : v <= 9.5f ? 2 : v <= 19.5f ? 3 : v <= 29.5f ? 4 : 5;
		const int32_t twice = 2 * int32_t(t.s16(0x308 + 2 * k));
		out.arrow[k] = value < twice ? 0 : value == twice ? 2 : 1;
	}
	return out;
}

uint32_t own_building_count(const SaveGame& save, uint32_t merchant, uint32_t town) {
	const int office = tick::facilities_detail::fn_5308a0(save, merchant, town);
	if (office < 0 || size_t(office) >= save.offices.size()) {
		return 0;
	}
	const OfficeMemory& o = save.offices[size_t(office)].memory;
	return uint32_t(o.u16(0x2D4)) + o.u16(0x2D2) + 1;
}

uint32_t town_situation_known(const SaveGame& save, uint32_t town) {
	constexpr uint32_t kHistory = 0x6DE588;
	const GlobalMemory& g = save.globals;
	if (town >= save.towns.size()) {
		return 0;
	}
	const uint32_t now = save.towns[town].memory.u32(0x2C8) | 0x7E0000;
	const uint32_t day = ((g.u32(0x6DE4B4) >> 8) & 0xF) + 0x10;
	const uint32_t player = g.u32(0x6DFC14);
	uint32_t slot = 0;
	while (slot < 8 && g.u8(kHistory + 0xA00 + slot) != player) {
		++slot;
	}
	uint32_t index = (day + 1) & 0xF;
	if (slot < 8 && !save.player_towns[slot].empty()) {
		const std::vector<uint8_t>& lag = save.player_towns[slot];
		index = (day - (town < lag.size() ? lag[town] : 0)) & 0xF;
	}
	return g.u32(kHistory + 4 * (16 * town + index)) & now;
}

}  // namespace p3::data
