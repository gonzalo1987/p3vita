// The side panel's ship menu (player_actions.hpp): the firepower's cannonballs (0x4901C0) and the prices it
// shows of the cargo (0x5187F0), as read in the game's code (re/emu/sesion14/ship_menu_notes.md). Without
// the game's data: a guard against changing them by accident.

#include <cstdio>

#include "p3data/player_actions.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
	if (!ok) {
		std::printf("fails: %s\n", what);
		++failures;
	}
}

bool balls(int32_t firepower, int big, int small, bool half) {
	const FirepowerBalls b = firepower_balls(firepower);
	return b.big == big && b.small == small && b.half == half;
}

}  // namespace

int main() {
	expect(balls(0, 0, 0, false), "no firepower, no balls");
	expect(balls(5, 0, 0, false), "under 9: the game's sqrt of a negative draws none");
	expect(balls(9, 0, 1, false), "9: f = 1, n = 2, one small ball");
	expect(balls(11, 0, 2, true), "11: n = 5, two small balls and a half");
	expect(balls(13, 0, 3, false), "13: n = 6, three small balls");
	expect(balls(30, 1, 1, false), "30: n = 12, a big ball and a small one");
	expect(balls(100, 2, 2, false), "100: n = 24, two big balls and two small ones");
	expect(balls(1000, 5, 0, false), "1000: at most five big balls");

	SaveGame save;
	save.ships.resize(1);
	SavedShip& ship = save.ships[0];
	ship.average_price(3) = 0.5f;  // Beer, in barrels: 0.5 · 200 + 0.5 = 100.5.
	ship.average_price(0) = 0.058f;  // Grain, in loads: 0.058 · 2000 + 0.5.
	ship.average_price(20) = 12.34f;  // Swords, in pieces: 12.34 · 10 + 0.5.
	const std::array<int32_t, 24> prices = ship_cargo_prices(save, 0);
	expect(prices[3] == 100, "a barrel's price, truncated");
	expect(prices[0] == 116, "a load's price");
	expect(prices[20] == 123, "a weapon's price, by ten");
	expect(prices[5] == 0, "nothing paid, 0");

	{
		// The shipyard's side menu's dates (0x52AFC0): the town's list the ship 2, then the ship 0.
		SaveGame yard;
		yard.globals.u16(global::kShipCount) = 3;
		yard.globals.u32(global::kWorldTick) = (100u << 8) + 0x10;
		yard.towns.resize(1);
		yard.ships.resize(3);
		TownMemory& t = yard.towns[0].memory;
		t.u16(0x81C) = 2;
		t.u16(0x854) = 2;  // 200 a day.
		t.u8(0x2C0) = 1;  // The day's tick 0x10 isn't under 1 · 8 + 7: no day less.
		yard.ships[2].memory.s32(0x14) = 1000;
		yard.ships[2].memory.s32(0x18) = 10;  // 990 to do, 18 under 28.
		yard.ships[2].memory.u16(0x6) = 0;
		yard.ships[0].memory.s32(0x14) = 500;
		yard.ships[0].memory.s32(0x18) = -5;  // Counted as 0: 500 to do, 28 under 28.
		expect(shipyard_done_tick(yard, 0, -1) == ((100u + 8 + 18 + 28) << 8), "the shipyard free: 1490 in 8 days, 46 more");
		expect(yard.ships[0].memory.s32(0x18) == 0, "the [+0x18] below 0 written back as 0, as the game (0x52B03A)");
		expect(shipyard_done_tick(yard, 0, 2) == ((100u + 5 + 18) << 8), "the first ship done: 990 in 5 days, 18 more");
		t.u8(0x2C0) = 2;  // 0x10 under 2 · 8 + 7: a day less.
		expect(shipyard_done_tick(yard, 0, 2) == ((100u + 4 + 18) << 8), "early in the day, a day less");
		t.u16(0x81C) = 3;
		expect(shipyard_done_tick(yard, 0, -1) == 0, "no ship in the list");
	}

	{
		// The shipyard window's helpers (re/emu/sesion35/notes_shipyard.md): the guns' slots (0x672CF0), an upgrade's
		// needs (0x66E030), «Stop repairs» (0x00), a building contract's figures (0x52B2C0) and the list of repairs.
		expect(ship_slots(2, 0)[0] == 0 && ship_slots(2, 0)[1] == 4, "a cog at level 0: no small guns, 4 big ones");
		expect(ship_slots(0, 1)[0] == 4 && ship_slots(0, 1)[1] == 0, "a snaikka at level 1: 4 small guns");
		expect(ship_slots(3, 2)[0] == 0 && ship_slots(3, 2)[1] == 12, "a hulk at level 2: 12 big guns");
		const std::array<int32_t, 3> need = upgrade_needs(2, 2);
		expect(need[0] == 8 && need[1] == 6 && need[2] == 6, "a cog two levels up: timber 8, pitch 6, tools 6");
		const OperationBytes stop = stop_repairs_operation(7, 3);
		expect(stop[0] == 0x00 && stop[4] == 7 && stop[8] == 3, "stop repairs: {0x00, ship, town}");

		SaveGame yard;
		yard.globals.u32(global::kWorldTownCount) = 1;
		yard.globals.u16(global::kWorldMerchantCount) = 1;
		yard.globals.u16(global::kWorldOfficeCount) = 1;
		yard.globals.u16(global::kShipCount) = 3;
		yard.globals.u32(global::kWorldTick) = 100u << 8;
		yard.towns.resize(1);
		yard.merchants.resize(1);
		yard.offices.resize(1);
		yard.ships.resize(3);
		TownMemory& t = yard.towns[0].memory;
		t.u32(0x76C) = 0x20000;  // a shipyard
		t.s8(0x824) = 2;  // snaikkas at its level 2
		t.s8(0x825) = -1;
		t.s8(0x826) = -1;
		t.s8(0x827) = -1;
		t.u16(0x854) = 2;
		MerchantMemory& m = yard.merchants[0].memory;
		m.s32(0) = 100000;
		m.u16(0xC) = 0;  // his office
		m.u16(0xE) = 0;  // his ships from 0
		OfficeMemory& o = yard.offices[0].memory;
		o.u8(0x2C6) = 0;
		o.u16(0x2C8) = 1;
		const int wares[5] = {0xB, 8, 0xC, 0x11, 0xF};
		const int32_t units[5] = {2000, 200, 200, 2000, 200};  // timber and hemp in loads, the others in barrels
		for (int k = 0; k < 5; ++k) {
			o.s32(4 + 4 * wares[k]) = 30 * units[k];
		}
		const ShipOrderInfo info = ship_order_info(yard, 0, 0, 0);
		expect(info.ok, "a snaikka can be ordered: the materials in his office, the money");
		expect(info.need[0] == 11 && info.need[1] == 3 && info.need[4] == 20, "the needs at the level 2: 11, 3, 3, 3, 20");
		expect(info.have[0] == 30 && info.buy[0] == 0 && info.price[0] == 0, "timber: 30 there, none to buy");
		expect(info.hold == 23 && info.days == 23 * 28 / 2 + 28, "the hold 23 (· 10 shown), the days");
		expect(info.cost == 8800 + 2 * 0x1A4, "the cost: the type's and the labour by the shipyard's value");
		expect(info.crew == 5 && info.knots == 6 && info.big_slots == 0 && info.small_slots == 0, "the snaikka's figures");
		expect(!shipyard_builds(yard, 0, 1) && shipyard_builds(yard, 0, 0), "the shipyard builds snaikkas only");

		// The list of repairs: the merchant's ships in the state 6 at the town, by their days left.
		for (uint16_t i = 0; i < 3; ++i) {
			ShipMemory& s = yard.ships[i].memory;
			s.u8(0) = 0;
			s.u16(0x4) = uint16_t(i + 1);
			s.u8(0x39) = 0;
			s.u16(0x134) = 6;
			s.s32(0x14) = 1000;
			s.s32(0x18) = i == 1 ? 900 : i == 0 ? 100 : 500;
		}
		const std::vector<uint16_t> repairs = shipyard_repairs(yard, 0, 0);
		bool sorted = repairs.size() == 3;
		for (size_t k = 1; sorted && k < repairs.size(); ++k) {
			sorted = shipyard_ship_days(yard, repairs[k - 1], false) <= shipyard_ship_days(yard, repairs[k], false);
		}
		expect(sorted, "the list of repairs by the days left");
		yard.ships[2].memory.u16(0x134) = 0;
		expect(shipyard_repairs(yard, 0, 0).size() == 2, "a ship not repairing left out");
	}

	if (failures == 0) {
		std::printf("ship menu: all right\n");
	}
	return failures == 0 ? 0 : 1;
}
