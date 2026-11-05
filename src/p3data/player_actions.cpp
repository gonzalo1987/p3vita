#include "p3data/player_actions.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick::ship_trade_detail {
uint16_t fn_5269a0(const SaveGame& save, int town, uint16_t owner);  // ship_trade.cpp
}  // namespace p3::data::tick::ship_trade_detail
namespace p3::data::tick::events_towns_detail {
bool ship_order_cost(const SaveGame& save, int town, uint32_t type, uint32_t merchant, int32_t& cost);  // events_towns.cpp
}  // namespace p3::data::tick::events_towns_detail
namespace p3::data::tick::events_detail {
int16_t fn_4f9580(const SaveGame& save, int merchant, uint32_t town);  // events.cpp
}  // namespace p3::data::tick::events_detail

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

bool can_cancel_order(const SaveGame& save, int ship) {
	return ship >= 0 && size_t(ship) < save.ships.size() && save.ships[size_t(ship)].memory.s32(0x18) <= 0 &&
		int(save.ships[size_t(ship)].memory.u8(0)) == save.player;
}

OperationBytes cancel_order_operation(uint32_t player, uint32_t town, uint32_t ship) {
	return words_operation({0x2F, player, town, ship});
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

}  // namespace p3::data
