#include "p3data/player_actions.hpp"

#include <cstring>
#include <initializer_list>

#include "p3data/game_memory.hpp"

namespace p3::data {

namespace {

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

}  // namespace p3::data
