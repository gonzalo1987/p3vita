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

// --- The player's office (docs/formats/operations.md: 0x08, 0x58, 0x59) ---

// 0x5180C0 (thiscall on the ship: ware, amount, float price): `amount` of a
// ware into the hold at `price` a unit: the average price weighed as the game
// does (each product exact in double, the sum stored as a float, the quotient
// by the new cargo as a float, in double too), or the price itself into an
// empty hold; then that cargo rounded down to its unit (200 for barrels, 10
// for the weapons past the 20 trade wares, 2000 for loads). First the ship's
// notice 4 (0x517770). Nothing for an amount ≤ 0.
void fn_5180c0(SaveGame& save, int ship, uint32_t ware, int32_t amount, float price) {
	if (amount <= 0) {
		return;
	}
	tick::ship_ai_detail::fn_517770(save, ship, 4);
	ShipMemory& s = save.ships[size_t(ship)].memory;
	int32_t& cargo = s.s32(0x54 + 4 * ware);
	float& average = s.f32(0xB4 + 4 * ware);
	const int32_t old = cargo;
	if (old > 0) {
		cargo = old + amount;
		const float sum = float(double(float(old)) * double(average) + double(float(amount)) * double(price));
		average = float(double(sum) / double(float(cargo)));
	} else {
		cargo = amount;
		average = price;
	}
	if (kInBarrels[ware]) {
		cargo = ware < 0x14 ? cargo / 200 * 200 : cargo / 10 * 10;
	} else {
		cargo = cargo / 2000 * 2000;
	}
}

// 0x4FF6F0 (thiscall on the office: ware, amount, float price): `amount` of a
// ware into the office at `price` a unit, its average price weighed in double
// (stock · average + amount as a float · price, / the new stock) and stored
// as a float; the price itself into an empty store. Nothing for an amount ≤ 0.
void fn_4ff6f0(OfficeMemory& o, uint32_t ware, int32_t amount, float price) {
	if (amount <= 0) {
		return;
	}
	int32_t& stock = o.s32(4 + 4 * ware);
	float& average = o.f32(0x3B8 + 4 * ware);
	const int32_t old = stock;
	if (old > 0) {
		stock = old + amount;
		const double held = double(average) * double(old);
		const double sum = double(float(amount)) * double(price) + held;
		average = float(sum / double(stock));
	} else {
		stock = amount;
		average = price;
	}
}

// 0x5007A0 (thiscall on the office: ware, amount, price): `amount` bought for
// `price` in all into the office: its average price (stock · average + price)
// / the new stock, unless the new stock isn't positive.
void fn_5007a0(OfficeMemory& o, uint32_t ware, int32_t amount, int32_t price) {
	int32_t& stock = o.s32(4 + 4 * ware);
	float& average = o.f32(0x3B8 + 4 * ware);
	const double before = double(average);
	const int32_t old = stock;
	const int32_t now = int32_t(uint32_t(old) + uint32_t(amount));
	stock = now;
	const double held = double(old) * before;
	const double sum = double(price) + held;
	if (now > 0) {
		average = float(sum / double(now));
	}
}

// 0x538100 (operation 0x08): {0x08, s32 amount, u16 ship, u16 ware, u16 (the
// player), u8 direction}: wares between a ship out of the shipyard (state <
// 4) and its owner's office in its town (0x5308A0). The amount's sign is
// dropped. Direction ≠ 0: from the office into the ship, at most the hold's
// room (0x5182B0; none if it is full) and the office's stock, at the office's
// average price (0x5180C0); 0: from the ship into the office, at most its
// cargo, at the ship's average price (0x4FF6F0). Then the ship's cargo again
// (0x5182B0) and its notice 4 (0x517770).
void fn_538100(SaveGame& save, const uint8_t* op) {
	const uint16_t ship = op_u16(op, 8);
	const uint16_t ware = op_u16(op, 0xA);
	if (ship >= save.globals.u16(global::kShipCount) || ware >= 0x18) {
		return;
	}
	SavedShip& saved = save.ships[ship];
	ShipMemory& s = saved.memory;
	if (s.u16(0x134) >= 4) {
		return;
	}
	const int office = tick::facilities_detail::fn_5308a0(save, s.u8(0), s.u8(0x39));
	if (office < 0) {
		return;
	}
	OfficeMemory& o = save.offices[size_t(office)].memory;
	int32_t amount = op_s32(op, 4);
	if (amount < 0) {
		amount = int32_t(0u - uint32_t(amount));
	}
	if (op[0xE] != 0) {
		const int32_t room = recalculate_cargo(saved);
		if (amount > room) {
			amount = room > 0 ? room : 0;
		}
		if (amount >= o.s32(4 + 4 * ware)) {
			amount = o.s32(4 + 4 * ware);
		}
		fn_5180c0(save, ship, ware, amount, o.f32(0x3B8 + 4 * ware));
		o.s32(4 + 4 * ware) -= amount;
	} else {
		int32_t& cargo = s.s32(0x54 + 4 * ware);
		if (amount > cargo) {
			amount = cargo;
		}
		cargo -= amount;
		fn_4ff6f0(o, ware, amount, s.f32(0xB4 + 4 * ware));
	}
	recalculate_cargo(saved);
	tick::ship_ai_detail::fn_517770(save, ship, 4);
}

// 0x538210 (operation 0x09): {0x09, u32 piece type, u32 town (its low byte;
// the bit 31 for «from the ship»), u32 ship, u32 count}: artillery between a
// ship in that town out of the shipyard (state < 4) and its owner's office
// there (0x5308A0). The ship's 24 slots [+0x13C] hold a piece's type (0..5),
// 0xFF if free, 6 for the second half of a big piece (its weight, 0x672CB4,
// 2000 or more: an even slot and the next one) and 7 for a free slot for
// small pieces (filled first); the office keeps each type as its weight
// [+0x124 + 4 · type]. A type of 6 or more (the window sends 0xFFFFFFFF)
// moves the ship's hand weapons [+0x154] and the office's [+0x2BC] instead,
// by the hold's room / 10 (unsigned: without room, the count itself). From a
// ship that guides a convoy without its flag 2, nothing. The counts compare
// unsigned, as the game does. Then the ship's notice 0x10 (0x517770) and its
// cargo again (0x5182B0).
void fn_538210(SaveGame& save, const uint8_t* operation) {
	static constexpr uint16_t kWeight[6] = {1000, 1000, 2000, 2000, 2000, 1000};  // 0x672CB4
	uint32_t op[5];
	std::memcpy(op, operation, sizeof(op));
	uint32_t& count = op[4];  // The handler counts it down in the operation.
	const uint32_t town = op[2] & 0xFF;
	if (town >= save.globals.u32(global::kWorldTownCount) || op[3] >= save.globals.u16(global::kShipCount)) {
		return;
	}
	const uint32_t ship = op[3];
	SavedShip& saved = save.ships[ship];
	ShipMemory& s = saved.memory;
	if (s.u16(0x134) >= 4 || s.u8(0x39) != town) {
		return;
	}
	const int office = tick::facilities_detail::fn_5308a0(save, s.u8(0), s.u8(0x39));
	if (office < 0) {
		return;
	}
	OfficeMemory& o = save.offices[size_t(office)].memory;
	const uint32_t type = op[1];
	auto slot = [&](uint32_t i) -> uint8_t& { return s.u8(0x13C + i); };  // Slot 24 is [+0x154]'s low byte.
	auto finish = [&]() {
		tick::ship_ai_detail::fn_517770(save, int(ship), 0x10);
		recalculate_cargo(saved);
	};
	if (op[2] & 0x80000000) {
		// From the ship into the office.
		const uint16_t convoy = s.u16(0x8);
		if (convoy < save.globals.u16(global::kShips + 0xF6)) {
			const auto& c = save.convoys[convoy];
			if (c.u16(0x10) == ship && !(c.u8(0x14) & 2)) {
				return;
			}
		}
		if (type >= 6) {
			const uint16_t held = s.u16(0x154);
			if (count >= held) {
				o.u32(0x2BC) += held;
				s.u16(0x154) = 0;
			} else {
				s.u16(0x154) = uint16_t(held - count);
				o.u32(0x2BC) += count;
			}
			finish();
			return;
		}
		const uint32_t weight = kWeight[type];
		auto take = [&](uint32_t i) {
			slot(i) = 0xFF;
			o.u32(0x124 + 4 * type) += weight;
		};
		if (weight < 2000) {
			// Two walks from the last slot, as the game: the second only counts
			// the operation down once more when the first ran out.
			for (int i = 23; i >= 0; --i) {
				if (slot(uint32_t(i)) != type || (!(i & 1) && slot(uint32_t(i) | 1) == 6)) {
					continue;
				}
				if (count == 0) {
					break;
				}
				--count;
				take(uint32_t(i));
			}
			for (int i = 23; i >= 0; --i) {
				if (slot(uint32_t(i)) != type || (!(i & 1) && slot(uint32_t(i) | 1) == 6)) {
					continue;
				}
				if (count-- == 0) {
					break;
				}
				take(uint32_t(i));
			}
		} else {
			for (int i = 23; i >= 0; --i) {
				if (slot(uint32_t(i)) != type) {
					continue;
				}
				if (count-- == 0) {
					break;
				}
				take(uint32_t(i));
				slot(uint32_t(i) + 1) = 0xFF;
			}
		}
		finish();
		return;
	}
	// From the office into the ship.
	if (type >= 6) {
		const int32_t room = recalculate_cargo(saved);
		uint32_t n = uint32_t(room / 10);
		if (count < n) {
			n = count;
		}
		if (o.u32(0x2BC) < n) {
			n = o.u32(0x2BC);
		}
		s.u16(0x154) = uint16_t(s.u16(0x154) + n);
		o.u32(0x2BC) -= n & 0xFFFF;
		finish();
		return;
	}
	const uint32_t weight = kWeight[type];
	const uint32_t in_office = uint32_t(o.s32(0x124 + 4 * type) / int32_t(weight));
	if (count > in_office) {
		count = in_office;
	}
	const uint32_t by_room = uint32_t(recalculate_cargo(saved) / int32_t(weight));
	if (count > by_room) {
		count = by_room;
	}
	auto put = [&](uint32_t i) {
		o.u32(0x124 + 4 * type) -= weight;
		slot(i) = uint8_t(type);
	};
	if (weight < 2000) {
		for (uint32_t i = 0; i < 24; i += 2) {  // First the slots marked 7 for small pieces.
			if (slot(i) == 0xFF && slot(i + 1) == 7) {
				if (count == 0) {
					break;
				}
				--count;
				put(i);
			}
		}
		for (uint32_t i = 0; i < 24; ++i) {
			if (slot(i) == 0xFF) {
				if (count-- == 0) {
					break;
				}
				put(i);
			}
		}
	} else {
		for (uint32_t i = 0; i < 24; i += 2) {
			if (slot(i) == 0xFF && slot(i + 1) == 0xFF) {
				if (count-- == 0) {
					break;
				}
				put(i);
				slot(i + 1) = 6;
			}
		}
	}
	finish();
}

// 0x53D2B0 (operation 0x58): {0x58, s32 amount, u16 ware, u16 merchant, u16
// (the player), u16 town}: the merchant's office in the town (0x5308A0) sells
// to the town's market, at most its stock (written back into [+4]), if its
// town ([+0x2C6]) is one. Its owner ([+0x2C4]), if a merchant, gets the price
// with the store before the sale (0x52E1D0, in the operation's town) and his
// sales' statistics (0x4F8690). The office's stock goes down and its town's
// store up. No ban ([+0x608]) is looked at.
TradeResult fn_53d2b0(SaveGame& save, uint8_t* op) {
	const uint16_t ware = op_u16(op, 8);
	if (ware >= 0x18 || op_s32(op, 4) <= 0) {
		return {};
	}
	const uint16_t town = op_u16(op, 0xE);
	const int office = tick::facilities_detail::fn_5308a0(save, op_u16(op, 0xA), town);
	if (office < 0) {
		return {};
	}
	OfficeMemory& o = save.offices[size_t(office)].memory;
	if (o.s32(4 + 4 * ware) < op_s32(op, 4)) {
		set_op_s32(op, 4, o.s32(4 + 4 * ware));
	}
	const uint8_t office_town = o.u8(0x2C6);
	if (office_town >= save.globals.u32(global::kWorldTownCount)) {
		return {};
	}
	const int32_t amount = op_s32(op, 4);
	int32_t price = 0;
	const uint16_t owner = o.u16(0x2C4);
	if (owner < save.globals.u16(global::kWorldMerchantCount)) {
		price = tick::game_sell_price(save, ware, town, amount);
		save.merchants[owner].memory.u32(0) += uint32_t(price);
		tick::offices_detail::fn_4f8690(save, owner, town, ware, price, amount, false);
	}
	o.s32(4 + 4 * ware) -= amount;
	save.towns[office_town].memory.s32(4 + 4 * size_t(ware)) += amount;
	return {amount, price};
}

// 0x53D3C0 (operation 0x59): {0x59, s32 amount, u16 ware, u16 merchant, u16
// (the player), u16 town}: the merchant's office in the town (0x5308A0) buys
// from the operation's town: at most its store (rounded down to the ware's
// unit, 200 or 2000) and what its owner ([+0x2C4]) can pay (0x52E430,
// 0x52F3E0), both written back into [+4]; the office's average price
// (0x5007A0), its town's ([+0x2C6]) store down, the owner's money and his
// purchases' statistics in that town (0x4F8840).
TradeResult fn_53d3c0(SaveGame& save, uint8_t* op) {
	const uint16_t ware = op_u16(op, 8);
	if (ware >= 0x18 || op_s32(op, 4) <= 0) {
		return {};
	}
	const uint16_t town = op_u16(op, 0xE);
	const int office = tick::facilities_detail::fn_5308a0(save, op_u16(op, 0xA), town);
	if (office < 0) {
		return {};
	}
	OfficeMemory& o = save.offices[size_t(office)].memory;
	const uint16_t owner = o.u16(0x2C4);
	if (owner >= save.merchants.size()) {
		return {};  // 0x5303C0 doesn't look: an office has a merchant.
	}
	MerchantMemory& m = save.merchants[owner].memory;
	const int32_t store = save.towns[town].memory.s32(4 + 4 * size_t(ware));
	if (store < op_s32(op, 4)) {
		set_op_s32(op, 4, round_to_unit(ware, store));
	}
	int32_t amount = op_s32(op, 4);
	int32_t price = tick::game_buy_price(save, ware, town, amount);
	if (m.s32(0) < price) {
		fn_52f3e0(save, ware, town, m.s32(0), price, amount);
		set_op_s32(op, 4, amount);
		if (amount <= 0) {
			return {};
		}
	}
	fn_5007a0(o, ware, amount, price);
	const uint8_t office_town = o.u8(0x2C6);
	save.towns[office_town].memory.s32(4 + 4 * size_t(ware)) -= amount;
	m.u32(0) -= uint32_t(price);
	tick::offices_detail::fn_4f8840(save, owner, office_town, ware, price, amount, false);
	return {amount, price};
}

// --- A convoy's trade (docs/formats/operations.md: 0x19, 0x1A, 0x1B) ---

namespace {

constexpr uint32_t kConvoyCount = global::kShips + 0xF6;  // u16 (0x6DD896)

}  // namespace

// 0x539300 (operations 0x19 and 0x1A, thiscall on the operation: 1 to sell,
// 0 to buy): {code, s32 amount, u16 ware, u16 convoy, u16 merchant, u16
// town}: the merchant's ships of a convoy in the town ([+0x1C]) trade with its
// market, one after another from the convoy's first ([+0xA], next [+0x6]).
// Selling: each gives what it has up to what is left (0x5182B0, its [+0x3C]
// |= 4); then all of it is paid at once with the store before the sale
// (0x52E1D0) and the merchant's sales (0x4F8690). Buying, with money: at most
// the merchant's ships' room (0x5182B0, counting only the positive) and the
// store; then each ship buys what fits of what is left, cut down to the money
// (its room · the money, in 32 bits, / the price, rounded down to the ware's
// unit, until it can pay: no more ships after that), with the purchases'
// statistics (0x4F8840), the store down, the cargo and its average (0x517FE0).
// The convoy's notices [+0x18] |= 4. No ban ([+0x608]) is looked at.
TradeResult fn_539300(SaveGame& save, const uint8_t* op, bool sell) {
	const uint16_t convoy = op_u16(op, 0xA);
	const uint16_t ware = op_u16(op, 8);
	const uint16_t merchant = op_u16(op, 0xC);
	if (convoy >= save.globals.u16(kConvoyCount) || ware >= 0x18 || merchant >= save.globals.u16(global::kWorldMerchantCount)) {
		return {};
	}
	MerchantMemory& m = save.merchants[merchant].memory;  // 0x5303C0
	GameMemory<0x3C>& c = save.convoys[convoy];
	const uint16_t town = c.u16(0x1C);
	if (town != op_u16(op, 0xE)) {
		return {};
	}
	int32_t& store = save.towns[town].memory.s32(4 + 4 * size_t(ware));
	const uint16_t ships = save.globals.u16(global::kShipCount);
	int32_t amount = op_s32(op, 4);
	int32_t total = 0;
	if (sell) {
		for (uint32_t i = c.u16(0xA); i < ships && amount > 0; i = save.ships[i].memory.u16(0x6)) {
			ShipMemory& s = save.ships[i].memory;
			if (uint16_t(s.u8(0)) != merchant) {
				continue;
			}
			int32_t& cargo = s.s32(0x54 + 4 * size_t(ware));
			if (cargo > amount) {
				cargo -= amount;
				total += amount;
				amount = 0;
			} else {
				amount -= cargo;
				total += cargo;
				cargo = 0;
			}
			recalculate_cargo(save.ships[i]);
			s.u8(0x3C) |= 4;
		}
		const int32_t price = tick::game_sell_price(save, ware, town, total);  // 0x52E1D0
		tick::offices_detail::fn_4f8690(save, merchant, town, ware, price, total, false);
		m.u32(0) += uint32_t(price);
		store += total;
		c.u32(0x18) |= 4;
		return {total, price};
	}
	if (m.s32(0) <= 0) {
		return {};
	}
	int32_t room_sum = 0;
	for (uint32_t i = c.u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
		if (uint16_t(save.ships[i].memory.u8(0)) == merchant) {
			const int32_t room = recalculate_cargo(save.ships[i]);
			if (room > 0) {
				room_sum += room;
			}
		}
	}
	if (room_sum < amount) {
		amount = room_sum;
	}
	if (amount > store) {
		amount = store;
	}
	TradeResult result;
	bool more = true;
	for (uint32_t i = c.u16(0xA); i < ships && more; i = save.ships[i].memory.u16(0x6)) {
		SavedShip& ship = save.ships[i];
		if (uint16_t(ship.memory.u8(0)) != merchant) {
			continue;
		}
		int32_t room = recalculate_cargo(ship);
		if (amount < room) {
			room = amount;
			more = false;
		}
		if (room <= 0) {
			continue;
		}
		int32_t price = tick::game_buy_price(save, ware, town, room);  // 0x52E430
		while (price > m.s32(0)) {
			more = false;
			room = int32_t(uint32_t(m.s32(0)) * uint32_t(room)) / price;  // imul (32 bits), idiv
			room = round_to_unit(ware, room);
			price = tick::game_buy_price(save, ware, town, room);
		}
		room = round_to_unit(ware, room);
		if (room <= 0 || price < 0) {
			continue;
		}
		tick::offices_detail::fn_4f8840(save, merchant, town, ware, price, room, false);
		m.u32(0) -= uint32_t(price);
		store -= room;
		tick::events_rare_detail::fn_517fe0(save, int(i), ware, room, price);
		amount -= room;
		recalculate_cargo(ship);
		c.u32(0x18) |= 4;
		result.amount += room;
		result.price += price;
	}
	return result;
}

// 0x5396B0 (operation 0x1B): {0x1B, s32 amount (its sign dropped), u16
// convoy, u16 ware, u16 merchant, u8 direction}: wares between the merchant's
// ships of a convoy and his office in its town ([+0x1C], 0x5308A0). Direction
// 0: from the ships, one after another, each giving all it has until what is
// left fits (0x4FF6F0 at the ship's average price; 0x5182B0, [+0x3C] |= 4).
// Otherwise from the office (at most its stock; nothing if empty) into the
// ships with room for a unit at least (200 or 2000): the last one takes all
// that is left, the others what fits in whole units (0x5180C0 at the office's
// average price; 0x5182B0, [+0x3C] |= 4), the stock down by as much.
void fn_5396b0(SaveGame& save, const uint8_t* op) {
	const uint16_t convoy = op_u16(op, 8);
	const uint16_t ware = op_u16(op, 0xA);
	const uint16_t merchant = op_u16(op, 0xC);
	if (convoy >= save.globals.u16(kConvoyCount) || ware >= 0x18 || merchant >= save.globals.u16(global::kWorldMerchantCount)) {
		return;
	}
	const GameMemory<0x3C>& c = save.convoys[convoy];
	const int office = tick::facilities_detail::fn_5308a0(save, merchant, c.u16(0x1C));
	if (office < 0) {
		return;
	}
	OfficeMemory& o = save.offices[size_t(office)].memory;
	int32_t amount = op_s32(op, 4);
	if (amount < 0) {
		amount = int32_t(0u - uint32_t(amount));
	}
	const uint16_t ships = save.globals.u16(global::kShipCount);
	if (op[0xE] == 0) {
		for (uint32_t i = c.u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
			SavedShip& ship = save.ships[i];
			ShipMemory& s = ship.memory;
			if (uint16_t(s.u8(0)) != merchant) {
				continue;
			}
			int32_t& cargo = s.s32(0x54 + 4 * size_t(ware));
			if (cargo >= amount) {
				cargo -= amount;
				fn_4ff6f0(o, ware, amount, s.f32(0xB4 + 4 * size_t(ware)));
				recalculate_cargo(ship);
				s.u8(0x3C) |= 4;
				return;
			}
			amount -= cargo;
			fn_4ff6f0(o, ware, cargo, s.f32(0xB4 + 4 * size_t(ware)));
			cargo = 0;
			recalculate_cargo(ship);
			s.u8(0x3C) |= 4;
		}
		return;
	}
	int32_t& stock = o.s32(4 + 4 * size_t(ware));
	if (stock == 0) {
		return;
	}
	const int32_t unit = kInBarrels[ware] ? 200 : 2000;
	if (amount > stock) {
		amount = stock;
	}
	for (uint32_t i = c.u16(0xA); i < ships;) {
		SavedShip& ship = save.ships[i];
		const uint32_t next = ship.memory.u16(0x6);
		if (uint16_t(ship.memory.u8(0)) == merchant) {
			int32_t room = recalculate_cargo(ship);
			if (room < 0) {
				room = 0;
			}
			if (room >= unit) {
				const float price = o.f32(0x3B8 + 4 * size_t(ware));
				if (room >= amount) {
					fn_5180c0(save, int(i), ware, amount, price);
					recalculate_cargo(ship);
					ship.memory.u8(0x3C) |= 4;
					stock -= amount;
					return;
				}
				const int32_t n = room / unit * unit;
				fn_5180c0(save, int(i), ware, n, price);
				amount -= n;
				recalculate_cargo(ship);
				ship.memory.u8(0x3C) |= 4;
				stock -= n;
			}
		}
		i = next;
	}
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
