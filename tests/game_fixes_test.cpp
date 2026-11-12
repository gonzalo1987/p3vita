// The fixes to the original game's bugs (p3data/game_fixes.hpp), without game
// data: off, p3vita does what the game does (bug included); on, the fix.

#include <cstdio>
#include <cstdlib>

#include "p3data/building_windows.hpp"
#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/letter_window.hpp"
#include "p3data/sailing.hpp"
#include "p3data/savegame.hpp"
#include "p3data/trade_routes.hpp"

using namespace p3::data;

namespace p3::data::tick::events_towns_detail {
// 0x4D6780 (town_tick/events_towns.cpp): expires the old letters of a merchant.
void fn_4d6780(SaveGame& save, int32_t merchant);
}  // namespace p3::data::tick::events_towns_detail
namespace p3::data::tick::facilities_detail {
// 0x508AA0 (town_tick/facilities.cpp): the shipyard's work on the ships and convoys it repairs.
void fn_508aa0(SaveGame& save, int town, int32_t work);
}  // namespace p3::data::tick::facilities_detail
namespace p3::data::tick {
// 0x4D98F0 (town_tick/operations.cpp): the operation 0x2B, company shares.
void fn_4d98f0(SaveGame& save, const uint8_t* operation);
}  // namespace p3::data::tick

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

constexpr uint32_t kMailFree = 0x6DD734;   // u16: the mailbox's first free letter
constexpr uint32_t kMailCount = 0x6DD736;  // u16

// One merchant with the letters 0 → 1 → 2: 0 and 2 of type 0x46 (kept for
// ever), 1 of type 0x86 (kept 0 days: past the table of 0x6C0198). With
// `old`, letter 1 is from day 1 of year 0 and the time is later, so it goes.
SaveGame mailbox(bool old) {
	SaveGame save;
	save.globals.u16(global::kWorldMerchantCount) = 1;
	save.globals.u32(global::kWorldTick) = old ? 0x10000 : 0;
	save.globals.u16(kMailCount) = 3;
	save.globals.u16(kMailFree) = 0xFFFF;
	save.merchants.resize(1);
	save.merchants[0].memory.u16(0xA) = 0;  // the first letter
	save.mail.assign(3, GameMemory<0x10>());
	save.mail_records.assign(3, std::nullopt);
	const uint8_t types[3] = {0x46, 0x86, 0x46};
	for (uint16_t i = 0; i < 3; ++i) {
		GameMemory<0x10>& letter = save.mail[i];
		letter.u8(0) = 1;  // day 1, month 0, year 0
		letter.u8(4) = types[i];
		letter.u16(6) = i < 2 ? uint16_t(i + 1) : 0xFFFF;
	}
	return save;
}

void old_letters() {
	using tick::events_towns_detail::fn_4d6780;
	// Letter 1 is removed. The game cuts the chain at it, so the free list
	// gets the cut link (0xFFFF) instead of letter 1: it is lost.
	set_fix_game_bugs(false);
	SaveGame save = mailbox(true);
	fn_4d6780(save, 0);
	check(save.mail[0].u16(6) == 2, "off, removed: the chain goes 0 → 2");
	check(save.globals.u16(kMailFree) == 0xFFFF, "off, removed: the free list is lost (the game's bug)");
	set_fix_game_bugs(true);
	save = mailbox(true);
	fn_4d6780(save, 0);
	check(save.mail[0].u16(6) == 2, "on, removed: the chain goes 0 → 2");
	check(save.globals.u16(kMailFree) == 1, "on, removed: letter 1 goes to the free list");
	check(save.mail[1].u16(6) == 0xFFFF, "on, removed: the free list ends after it");

	// Letter 1 is kept (its age is 0). The game still cuts the chain before
	// it, so letters 1 and 2 drop out of the merchant's list.
	set_fix_game_bugs(false);
	save = mailbox(false);
	fn_4d6780(save, 0);
	check(save.mail[0].u16(6) == 0xFFFF, "off, kept: the chain is cut after letter 0 (the game's bug)");
	set_fix_game_bugs(true);
	save = mailbox(false);
	fn_4d6780(save, 0);
	check(save.mail[0].u16(6) == 1 && save.mail[1].u16(6) == 2, "on, kept: the chain stays 0 → 1 → 2");
	check(save.globals.u16(kMailFree) == 0xFFFF, "on, kept: nothing freed");
	set_fix_game_bugs(false);
}

constexpr uint32_t kEventHead = 0x6DD744;  // u16
constexpr uint32_t kEventFree = 0x6DD746;  // u16
constexpr uint32_t kEventCount = 0x6DD748;  // u16

// Two merchants, the buyer 0 with `money`; the event queue with record 0
// merchant 1's pool of company shares (an event 0x18) holding 4 %, and
// records 1..3 free.
SaveGame shares_pool(int32_t money) {
	SaveGame save;
	save.globals.u16(global::kWorldMerchantCount) = 2;
	save.merchants.resize(2);
	save.merchants[0].memory.s32(0) = money;
	save.events.assign(4, GameMemory<0x18>());
	save.globals.u16(kEventCount) = 4;
	save.globals.u16(kEventHead) = 0;
	save.globals.u16(kEventFree) = 1;
	GameMemory<0x18>& pool = save.events[0];
	pool.u16(4) = 0xFFFF;
	pool.u16(6) = 0x18;
	pool.u8(0x10) = 1;
	pool.u8(0x11) = 0xFF;
	pool.u8(0x12) = 4;
	pool.u8(0x13) = 5;
	for (uint16_t i = 1; i < 4; ++i) {
		save.events[i].u16(4) = uint16_t(i + 1);
	}
	return save;
}

// Merchant 0 buys `n` of merchant 1's shares for 1000 (operation 0x2B).
void buy(SaveGame& save, uint8_t n) {
	const uint8_t op[0x14] = {0x2B, 0, 0, 0, 1, n, 0xFF, 0, 0xE8, 0x03};
	tick::fn_4d98f0(save, op);
}

// The holding (an event 0x18 of merchant 1's company held by 0): its shares, or -1.
int held(const SaveGame& save) {
	for (const GameMemory<0x18>& e : save.events) {
		if (e.u16(6) == 0x18 && e.u8(0x10) == 1 && e.u8(0x11) == 0) {
			return e.u8(0x12);
		}
	}
	return -1;
}

void company_shares() {
	// Five bought from a pool with four: the game gives the five (one out
	// of nothing) for the whole price; fixed, the four for four fifths.
	set_fix_game_bugs(false);
	SaveGame save = shares_pool(5000);
	buy(save, 5);
	check(held(save) == 5 && save.events[0].u8(0x12) == 0, "off: five shares from a pool of four (the game's bug)");
	check(save.merchants[0].memory.s32(0) == 4000, "off: the whole price");
	set_fix_game_bugs(true);
	save = shares_pool(5000);
	buy(save, 5);
	check(held(save) == 4 && save.events[0].u8(0x12) == 0, "on: the pool's four");
	check(save.merchants[0].memory.s32(0) == 4200, "on: four fifths of the price");
	// A buyer in debt: the game compares his money unsigned and he buys.
	set_fix_game_bugs(false);
	save = shares_pool(-100);
	buy(save, 2);
	check(held(save) == 2, "off: a buyer in debt buys (the game's bug)");
	set_fix_game_bugs(true);
	save = shares_pool(-100);
	buy(save, 2);
	check(held(save) == -1 && save.merchants[0].memory.s32(0) == -100, "on: a buyer in debt doesn't");
	set_fix_game_bugs(false);
}

}  // namespace

// Operation 0x6C (0x53E890, trade_routes.cpp): a route of 152 stops for a ship on an empty pool. The pool grows
// at the first stop and again at the 151st, when the game writes the link to it into the freed array: its route
// ends after 150 stops. Fixed, the 152.
void trade_route_growth() {
	for (bool fix : {false, true}) {
		set_fix_game_bugs(fix);
		SaveGame save;
		save.globals.u16(global::kShipCount) = 1;
		save.globals.u32(global::kWorldTownCount) = 2;
		save.ships.resize(1);
		save.ships[0].memory.u16(0x132) = 0xFFFF;
		uint8_t op[0x14] = {0x6C, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 152, 0, 0, 0, 0, 0, 0, 0};
		fn_53e890(save, op);
		check(save.globals.u16(0x6DD72A) == 300 && save.trade_routes.size() == 300, "0x6C: the pool grown twice");
		const uint32_t head = save.ships[0].memory.u16(0x132);
		int stops = 0;
		uint32_t s = head;
		do {
			++stops;
			s = save.trade_routes[s].u16(0x0);
		} while (s != head && stops < 1000);
		check(stops == (fix ? 152 : 150), fix ? "0x6C fixed: the whole route" : "0x6C: the route cut at 150");
		check(save.trade_routes[head].u8(0x2) == 1 && (save.trade_routes[head].u8(0x3) & 4), "0x6C: the first stop");
	}
	set_fix_game_bugs(false);
}

// 0x4D5900 (trade_routes.cpp): a ship on its route before its closed port (town 1), nothing to do at its only
// stop, its home (town 0) closed too: the nearest town with a shipyard is its own closed port (the game then
// hangs, the ship arriving again and again in the same tick). Fixed, the open town 2, far away.
void closed_port_route() {
	for (bool fix : {false, true}) {
		set_fix_game_bugs(fix);
		SaveGame save;
		GlobalMemory& g = save.globals;
		g.u32(global::kWorldTownCount) = 3;
		g.u16(global::kWorldMerchantCount) = 1;
		g.u16(global::kShipCount) = 1;
		save.towns.resize(3);
		save.merchants.resize(1);
		save.ships.resize(1);
		const uint32_t flags[3] = {0x240, 0x240, 0x40};
		const int32_t x[3] = {100, 500, 1500};
		for (uint32_t t = 0; t < 3; ++t) {
			save.towns[t].memory.u32(0x2C8) = flags[t];
			g.u8(0x6DE4B8 + t) = uint8_t(t);  // the towns' numbers
			g.s32(0x6DDB90 + 0x34 * t + 0x20) = x[t];
			g.s32(0x6DDB90 + 0x34 * t + 0x24) = 300;
		}
		save.merchants[0].memory.u8(0x19) = 0;  // home
		ShipMemory& s = save.ships[0].memory;
		s.u8(0x0) = 0;
		s.u16(0x8) = 0xFFFF;
		s.u16(0x1E) = 500;  // at town 1
		s.u16(0x22) = 300;
		uint8_t op[0x14] = {0x6C, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
		fn_53e890(save, op);  // one stop at town 1, nothing to do there
		check(next_route_town(save, 0) == (fix ? 2u : 1u),
			fix ? "0x4D5900 fixed: the open town" : "0x4D5900: its own closed port");
	}
	set_fix_game_bugs(false);
}

// 0x51A2C0 (sailing.cpp): a ship before its closed port (town 1) looks for the open town nearest to it by the
// travel times: town 2 is a day away and has no deep port (bit 1), town 3 is three days away, town 0 ten. A big
// ship: the game's test is the wrong way round, so it may go to town 2 and a small one may not; fixed, the other
// way. Passed 0xFF (what the game passes for a ship alone, its town already cleared), every time is 0xFF and the
// first open town wins (town 0).
void closed_port_ai() {
	for (bool fix : {false, true}) {
		set_fix_game_bugs(fix);
		SaveGame save;
		save.globals.u32(global::kWorldTownCount) = 4;
		save.globals.u16(global::kShipCount) = 1;
		save.towns.resize(4);
		save.ships.resize(1);
		const uint32_t flags[4] = {0, 0x200, 1, 0};
		for (uint32_t t = 0; t < 4; ++t) {
			save.towns[t].memory.u32(0x2C8) = flags[t];
		}
		save.town_travel.assign(16, 0xFF);
		const uint8_t from_1[4] = {160, 0, 16, 48};
		for (uint32_t t = 0; t < 4; ++t) {
			save.town_travel[4 + t] = from_1[t];
		}
		ShipMemory& s = save.ships[0].memory;
		s.u16(0x8) = 0xFFFF;
		for (uint8_t type : {0, 3}) {
			s.u8(0xE) = type;
			const bool big = type >= 2;
			// Unfixed, a small ship avoids town 2 and a big one doesn't; fixed, the other way round.
			const uint32_t expected = (big != fix) ? 2 : 3;
			check(closed_port_destination(save, 0, 1) == expected,
				fix ? "0x51A2C0 fixed: a big ship not up a river, a small one may" : "0x51A2C0: the test the wrong way");
		}
		check(closed_port_destination(save, 0, 0xFF) == 0, "0x51A2C0 from 0xFF: the first open town");
	}
	set_fix_game_bugs(false);
}

// The cancelled convoy repair (0x508C86): a convoy of one ship at half its hull, its guide's orders 1, in town 0's
// repair list; the work 100. Off, it leaves with the ship whole (the game); on, with the hull it got.
void cancelled_convoy_repair() {
	for (const bool fix : {false, true}) {
		set_fix_game_bugs(fix);
		SaveGame save;
		save.globals.u16(global::kShipCount) = 1;
		save.globals.u16(global::kShips + 0xF6) = 1;  // the convoys
		save.globals.u16(global::kShips + 0xEC) = 0xFFFF;  // the convoys' list
		save.towns.resize(1);
		save.towns[0].memory.u16(0x81E) = 0xFFFF;
		save.towns[0].memory.u16(0x820) = 0;
		save.ships.resize(1);
		SavedShip& ship = save.ships[0];
		ship.max_health() = 1000;
		ship.health() = 500;
		ship.orders() = 1;
		ship.next_in_list() = 0xFFFF;
		save.convoys.resize(1);
		GameMemory<0x3C>& c = save.convoys[0];
		c.u8(0x0) = 0xFF;
		c.u16(0x8) = 0xFFFF;
		c.u16(0xA) = 0;
		c.u16(0x10) = 0;
		tick::facilities_detail::fn_508aa0(save, 0, 100);
		check(save.towns[0].memory.u16(0x820) == 0xFFFF && c.u16(0x12) == 0, "the cancelled convoy leaves the shipyard");
		check(ship.health() == (fix ? 600 : 1000), fix ? "fixed: the ship keeps its hull" : "the game: the ship whole");
	}
	set_fix_game_bugs(false);
}

// The proposal's button (the letter 0x22, letter_window.hpp): accepted ([+0xF] 0, as 0x60 leaves it) the day it came,
// the game still shows it till the day's end; fixed, not. With 14 days: the 14th day is the last one, and the game
// gives one more.
void proposal_button() {
	const uint32_t day = 1300 * 365 + 100;  // the 11th of April
	SaveGame save;
	save.globals.u32(global::kWorldTick) = (day << 8) + 0x40;
	save.globals.u16(kMailCount) = 1;
	save.mail.resize(1);
	save.mail_records.resize(1);
	save.mail_buffers.resize(1);
	GameMemory<0x10>& e = save.mail[0];
	e.u8(0) = 11;
	e.u8(1) = 3;
	e.u16(2) = 1300;
	e.u8(4) = 0x22;
	e.u8(0xF) = 0;
	check(letter_buttons(save, nullptr, 0).size() == 1, "the game: the accepted proposal's button till the day's end");
	set_fix_game_bugs(true);
	check(letter_buttons(save, nullptr, 0).empty(), "fixed: no button once accepted");
	e.u8(0xF) = 14;
	save.globals.u32(global::kWorldTick) = ((day + 13) << 8) + 0xFF;
	check(letter_buttons(save, nullptr, 0).size() == 1, "fixed: the 14th day still");
	save.globals.u32(global::kWorldTick) = (day + 14) << 8;
	check(letter_buttons(save, nullptr, 0).empty(), "fixed: past the 14 days");
	set_fix_game_bugs(false);
	check(letter_buttons(save, nullptr, 0).size() == 1, "the game: one day more");
}

// The feeding window's kind (building_windows.hpp): five barrels of wine priced as salt (the game) or as wine (fixed).
void feeding_wine() {
	SaveGame save;
	save.globals.u32(global::kWorldTownCount) = 1;
	save.towns.resize(1);
	TownMemory& t = save.towns[0].memory;
	t.u32(0x76C) = 0x100;
	for (int w : {4, 7}) {
		for (int i = 0; i < 4; ++i) {
			t.u32(0x4F0 + 16 * uint32_t(w) + 4 * uint32_t(i)) = uint32_t(2000 * (i + 1));
		}
	}
	const std::array<uint16_t, 5> wine = {0, 0, 0, 0, 5};
	const uint8_t game = church_feeding_kind(save, 0, wine);
	set_fix_game_bugs(true);
	const uint8_t fixed = church_feeding_kind(save, 0, wine);
	set_fix_game_bugs(false);
	check(fixed > game, "fixed: wine is worth more than salt");
}

int main() {
	check(!fix_game_bugs(), "the library starts with the fixes off");
	cancelled_convoy_repair();
	proposal_button();
	feeding_wine();
	old_letters();
	company_shares();
	trade_route_growth();
	closed_port_route();
	closed_port_ai();
	if (failures == 0) {
		std::printf("game_fixes_test: all correct\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
