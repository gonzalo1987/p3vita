#include "p3data/trade.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

// Marineros que lleva cada tipo de barco sin ocupar bodega (tabla 0x673664).
constexpr std::array<int32_t, 4> kMinSailors = {10, 16, 30, 24};
constexpr int32_t kSailorWeight = 400;
// Peso de cada tipo de pieza de artillería (tabla 0x672CB4).
constexpr std::array<int32_t, 6> kArtilleryWeight = {1000, 1000, 2000, 2000, 2000, 1000};
// 0x672CC8: lo que suma cada tipo de pieza a [+0x120] del barco.
constexpr std::array<int32_t, 6> kArtilleryFirepower = {9, 10, 22, 24, 30, 18};
constexpr int32_t kHandWeaponWeight = 10;
constexpr int32_t kLoad = 2000;
// 0x672C14: 1 for the wares traded in barrels (200), 0 for loads (2000).
constexpr uint8_t kInBarrels[kWareCount] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};

int32_t round_to_unit(int ware, int32_t amount) {
	return kInBarrels[ware] ? amount / 200 * 200 : amount / 2000 * 2000;
}

uint16_t op_u16(const uint8_t* op, size_t at) {
	uint16_t v;
	std::memcpy(&v, op + at, 2);
	return v;
}

int32_t op_s32(const uint8_t* op, size_t at) {
	int32_t v;
	std::memcpy(&v, op + at, 4);
	return v;
}

void set_op_s32(uint8_t* op, size_t at, int32_t v) {
	std::memcpy(op + at, &v, 4);
}

// The merchant's bit of the town in [+0x608] (trade banned there), as the
// handlers test it: `shl` by the town's low byte, which x86 masks to 5 bits.
bool banned(const SaveGame& save, uint8_t owner, uint16_t town) {
	return owner < save.globals.u16(global::kWorldMerchantCount) &&
		(save.merchants[owner].memory.u32(0x608) & (1u << (town & 31))) != 0;
}

// 0x52F3E0 (on the prices' object 0x6DE3D8: ware, town, money, &price,
// &amount): the purchase cut down to what the merchant can pay. Without money
// (≤ 0) nothing. Otherwise the amount scaled by money / the first price (in
// 64 bits, the quotient's low 32 bits), rounded down to the ware's unit,
// again while it costs more than the money (always scaling by the first
// price); then up unit by unit while it costs less than the money and isn't
// more than asked.
void fn_52f3e0(const SaveGame& save, int ware, int town, int32_t money, int32_t& price, int32_t& amount) {
	if (money <= 0) {
		price = 0;
		amount = 0;
		return;
	}
	const int64_t first_price = price;
	int32_t candidate = amount;
	int32_t candidate_price = 0;
	do {
		candidate = int32_t(int64_t(candidate) * int64_t(money) / first_price);
		candidate = round_to_unit(ware, candidate);
		if (candidate == 0) {
			price = 0;
			amount = 0;
			return;
		}
		candidate_price = tick::game_buy_price(save, ware, town, candidate);
	} while (candidate_price > money);
	int32_t best;
	for (;;) {
		best = candidate;
		price = candidate_price;
		candidate += kInBarrels[ware] ? 200 : 2000;
		if (candidate > amount) {
			break;
		}
		candidate_price = tick::game_buy_price(save, ware, town, candidate);
		if (candidate_price >= money) {
			break;
		}
	}
	amount = best;
}

}  // namespace

int32_t recalculate_cargo(SavedShip& ship) {
	int32_t used = 0;
	uint32_t value = 0;  // [+0x114] Lo que costó la carga: carga · precio medio, truncado.
	for (int w = kWareCount - 1; w >= 0; --w) {
		int32_t& cargo = ship.cargo(w);
		cargo -= cargo % ware_scale(kWares[size_t(w)].unit);
		used += cargo;
		value += uint32_t(int64_t(double(float(cargo)) * double(ship.average_price(w))));
	}
	ship.memory.u32(0x114) = value;
	used += int32_t(ship.extra_loads()) * kLoad;

	const int32_t min_sailors = kMinSailors[ship.type() & 3];
	int32_t equipment = ship.sailors() > min_sailors ? (ship.sailors() - min_sailors) * kSailorWeight : 0;
	int32_t firepower = 0;  // [+0x120] Suma de la tabla 0x672CC8 por pieza de artillería.
	for (int slot = 0; slot < 24; ++slot) {
		const uint8_t piece = ship.artillery(slot);
		if (piece < kArtilleryWeight.size()) {
			equipment += kArtilleryWeight[piece];
			firepower += kArtilleryFirepower[piece];
		}
	}
	ship.memory.s32(0x120) = firepower;
	equipment += int32_t(ship.hand_weapons()) * kHandWeaponWeight;

	ship.equipment() = equipment;
	ship.used() = (used + 199) / 200 * 200;
	return ship.capacity() - equipment - used;
}

// 0x537450 (operation 0x02, thiscall on the operation): buy for a ship from
// the market of the town it is docked in.
TradeResult fn_537450(SaveGame& save, uint8_t* op) {
	const uint16_t ware = op_u16(op, 8);
	const uint16_t ship_index = op_u16(op, 0xA);
	const uint16_t town = op_u16(op, 0xE);
	if (ship_index >= save.ships.size() || ware >= kWareCount) {
		// The game doesn't look: it would read past the ships or the hold.
		// The market window sends the player's ship and a ware < 20.
		return {};
	}
	SavedShip& ship = save.ships[ship_index];
	ShipMemory& s = ship.memory;
	if (town >= save.globals.u32(global::kWorldTownCount) || s.u8(0x39) != town) {
		return {};  // A town out, or the ship isn't in it.
	}
	const uint8_t owner = s.u8(0);
	if (banned(save, owner, town)) {
		return {};
	}
	int32_t& stock = save.towns[s.u8(0x39)].memory.s32(4 + 4 * size_t(ware));
	// The room left (0x5182B0, which rounds the cargo first) or the store.
	int32_t limit = recalculate_cargo(ship);
	if (stock < limit) {
		limit = stock;
	}
	int32_t amount = op_s32(op, 4);
	if (limit < amount) {
		if (limit < 0) {
			return {};
		}
		amount = round_to_unit(ware, limit);
		set_op_s32(op, 4, amount);
	}
	if (owner >= save.merchants.size()) {
		return {};  // 0x5303C0 doesn't look: never for the player's ship.
	}
	MerchantMemory& m = save.merchants[owner].memory;
	int32_t price = tick::game_buy_price(save, ware, town, amount);  // 0x52E430
	if (price > m.s32(0)) {
		fn_52f3e0(save, ware, s.u8(0x39), m.s32(0), price, amount);
		set_op_s32(op, 4, amount);
	}
	if (amount <= 0) {
		return {};
	}
	m.u32(0) -= uint32_t(price);
	tick::events_rare_detail::fn_517fe0(save, ship_index, ware, amount, price);
	recalculate_cargo(ship);
	stock -= amount;
	// The merchant's purchases and reputation in the town (0x4F8840), with the
	// store already lowered.
	tick::offices_detail::fn_4f8840(save, owner, town, ware, price, amount, false);
	return {amount, price};
}

// 0x537340 (operation 0x01, thiscall on the operation): sell from a ship to
// a town's market. It doesn't check that the ship is in the town.
TradeResult fn_537340(SaveGame& save, uint8_t* op) {
	const uint16_t ware = op_u16(op, 8);
	const uint16_t ship_index = op_u16(op, 0xA);
	const uint16_t town = op_u16(op, 0xE);
	if (ship_index >= save.ships.size() || ware >= kWareCount) {
		return {};  // The game doesn't look (as in 0x537450).
	}
	SavedShip& ship = save.ships[ship_index];
	ShipMemory& s = ship.memory;
	if (town >= save.globals.u32(global::kWorldTownCount)) {
		return {};
	}
	const uint8_t owner = s.u8(0);
	if (banned(save, owner, town)) {
		return {};
	}
	int32_t amount = op_s32(op, 4);
	int32_t& cargo = s.s32(0x54 + 4 * size_t(ware));
	if (cargo < amount) {
		amount = cargo;
		set_op_s32(op, 4, amount);
		cargo = 0;
	} else {
		cargo -= amount;
	}
	recalculate_cargo(ship);
	tick::ship_ai_detail::fn_517770(save, ship_index, 4);
	if (owner >= save.merchants.size()) {
		return {};  // 0x5303C0 doesn't look: never for the player's ship.
	}
	// Paid with the store before the sale (0x52E1D0).
	const int32_t price = tick::game_sell_price(save, ware, town, amount);
	save.merchants[owner].memory.u32(0) += uint32_t(price);
	// The merchant's sales and reputation in the town (0x4F8690).
	tick::offices_detail::fn_4f8690(save, owner, town, ware, price, amount, false);
	save.towns[town].memory.u32(4 + 4 * size_t(ware)) += uint32_t(amount);
	return {amount, price};
}

namespace {

std::array<uint8_t, 0x14> trade_operation(uint32_t code, int ship, int town, int ware, int32_t amount) {
	std::array<uint8_t, 0x14> op{};
	const uint16_t fields[4] = {uint16_t(ware), uint16_t(ship), 0, uint16_t(town)};
	std::memcpy(op.data(), &code, 4);
	std::memcpy(op.data() + 4, &amount, 4);
	std::memcpy(op.data() + 8, fields, sizeof(fields));
	return op;
}

}  // namespace

TradeResult buy_to_ship(SaveGame& save, int ship, int town, int ware, int32_t amount) {
	std::array<uint8_t, 0x14> op = trade_operation(0x02, ship, town, ware, amount);
	return fn_537450(save, op.data());
}

TradeResult sell_from_ship(SaveGame& save, int ship, int town, int ware, int32_t amount) {
	std::array<uint8_t, 0x14> op = trade_operation(0x01, ship, town, ware, amount);
	return fn_537340(save, op.data());
}

TradeResult quote_buy(const SaveGame& save, int ship_index, int town_index, int ware, int32_t amount) {
	if (ship_index < 0 || size_t(ship_index) >= save.ships.size() || ware < 0 || ware >= kWareCount ||
		town_index < 0 || uint32_t(town_index) >= save.globals.u32(global::kWorldTownCount)) {
		return {};
	}
	SavedShip ship = save.ships[size_t(ship_index)];
	if (ship.town() != town_index || banned(save, ship.owner(), uint16_t(town_index)) ||
		ship.owner() >= save.merchants.size()) {
		return {};
	}
	const int32_t stock = save.towns[size_t(town_index)].memory.s32(4 + 4 * size_t(ware));
	int32_t limit = recalculate_cargo(ship);
	if (stock < limit) {
		limit = stock;
	}
	if (limit < amount) {
		if (limit < 0) {
			return {};
		}
		amount = round_to_unit(ware, limit);
	}
	const int32_t money = save.merchants[ship.owner()].money();
	int32_t price = tick::game_buy_price(save, ware, town_index, amount);
	if (price > money) {
		fn_52f3e0(save, ware, town_index, money, price, amount);
	}
	return amount > 0 ? TradeResult{amount, price} : TradeResult{};
}

TradeResult quote_sell(const SaveGame& save, int ship_index, int town_index, int ware, int32_t amount,
	TradeDifficulty difficulty) {
	if (ship_index < 0 || size_t(ship_index) >= save.ships.size() || ware < 0 || ware >= kWareCount ||
		town_index < 0 || uint32_t(town_index) >= save.globals.u32(global::kWorldTownCount)) {
		return {};
	}
	const SavedShip& ship = save.ships[size_t(ship_index)];
	if (banned(save, ship.owner(), uint16_t(town_index))) {
		return {};
	}
	amount = std::min(amount, ship.cargo(size_t(ware)));
	const SavedTown& town = save.towns[size_t(town_index)];
	return {amount, sell_price(ware, town.market(ware), amount, difficulty)};
}

}  // namespace p3::data
