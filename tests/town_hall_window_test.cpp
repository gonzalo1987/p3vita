// The town hall's windows (p3data/town_hall_window.hpp), without game data: the arsenal's rules as the game's update
// 0x5B0930 runs them (the mayor, the weekly costs, «Form unit» and its messages, «Dismiss unit») and the operations
// they queue (0x41, 0x40, checked in the engine by the oracle: the scenario militia_ops); the town hall's notices
// (0x5E7730): the world's orders, «Accept order» and its 0x3D (the scenario town_orders), the prince's decree.

#include <cstdio>
#include <cstring>
#include <vector>

#include "p3data/town_hall_window.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

uint32_t u32_at(const OperationBytes& op, size_t at) {
	uint32_t v;
	std::memcpy(&v, op.data() + at, 4);
	return v;
}

// One town, the player (merchant 0) its mayor with an office there (`office`).
SaveGame world(bool office) {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 1;
	g.u16(global::kWorldMerchantCount) = 1;
	g.u16(global::kWorldOfficeCount) = 1;
	g.u32(global::kPlayer) = 0;
	save.towns.resize(1);
	save.merchants.resize(1);
	save.offices.resize(1);
	TownMemory& t = save.towns[0].memory;
	t.u8(0x6F1) = 0;
	t.u16(0x784) = office ? 0 : 0xFFFF;
	t.s32(0x2E4) = 100;
	save.offices[0].memory.u16(0x2C4) = 0;
	save.offices[0].memory.u16(0x2CA) = 0xFFFF;
	for (uint32_t k = 0; k < kArsenalTypes; ++k) {
		save.offices[0].memory.s32(0x54 + 4 * k) = 60;
		t.s32(0x54 + 4 * k) = 10;
	}
	return save;
}

}  // namespace

int main() {
	{
		SaveGame save = world(true);
		check(arsenal_mayor(save, 0), "the player the mayor");
		save.towns[0].memory.u8(0x6F1) = 5;
		check(!arsenal_mayor(save, 0), "another mayor");
		TownMemory& t = save.towns[0].memory;
		t.u8(0x998 + 1) = 3;
		t.u8(0x99C + 1) = 2;
		check(arsenal_cost(save, 0, 1) == 5 * 26 * 7, "the archers' weekly cost: 5 · 26 · 7");
		check(arsenal_cost(save, 0, 3) == 0, "no musketeers");
		check(arsenal_open_message(save, 0) == ArsenalMessage::None, "opened: no message");
		t.u32(0x2C8) = 0x20;
		check(arsenal_open_message(save, 0) == ArsenalMessage::Authorised, "opened with the council's limit");
	}
	{
		// «Form unit»: the office's weapons, the beggars, the council's total; queued.
		SaveGame save = world(true);
		TownMemory& t = save.towns[0].memory;
		ArsenalMessage message;
		OperationBytes op{};
		check(arsenal_form(save, 0, 2, message, op) && message == ArsenalMessage::None, "formed");
		check(u32_at(op, 0) == 0x41 && u32_at(op, 4) == 2 && u32_at(op, 8) == 0, "0x41 {type 2, town 0}");
		save.offices[0].memory.s32(0x54 + 4 * 2) = 49;
		check(!arsenal_form(save, 0, 2, message, op) && message == ArsenalMessage::NoWeapons, "49 weapons in the office");
		t.s32(0x2E4) = 19;
		check(!arsenal_form(save, 0, 2, message, op) && message == ArsenalMessage::NoBeggars, "19 beggars: over it");
		t.s32(0x2E4) = 20;
		save.offices[0].memory.s32(0x54 + 4 * 2) = 50;
		t.u32(0x2C8) = 0x40;
		t.u16(0x9A0) = 6;
		t.u8(0x998) = 4;
		t.u8(0x99C + 3) = 2;
		check(!arsenal_form(save, 0, 2, message, op) && message == ArsenalMessage::NoCouncil, "6 units of 6 authorised");
		t.u16(0x9A0) = 7;
		check(arsenal_form(save, 0, 2, message, op) && message == ArsenalMessage::Authorised, "6 of 7: formed, the total");
	}
	{
		// Without an office there, the town's weapons.
		SaveGame save = world(false);
		ArsenalMessage message;
		OperationBytes op{};
		check(!arsenal_form(save, 0, 0, message, op) && message == ArsenalMessage::NoWeapons, "the town's 10 weapons");
		save.towns[0].memory.s32(0x54) = 50;
		check(arsenal_form(save, 0, 0, message, op), "the town's 50");
	}
	{
		SaveGame save = world(true);
		ArsenalMessage message;
		const OperationBytes op = arsenal_dismiss(save, 0, 3, message);
		check(u32_at(op, 0) == 0x40 && u32_at(op, 4) == 3 && u32_at(op, 8) == 0 && message == ArsenalMessage::None,
			"0x40 {type 3, town 0}");
		save.towns[0].memory.u32(0x2C8) = 0x80;
		arsenal_dismiss(save, 0, 3, message);
		check(message == ArsenalMessage::Authorised, "dismissed with the council's limit");
	}
	{
		// The world's orders: one of town 2 (beer, 25 barrels, the bonus), the rest empty.
		SaveGame save = world(true);
		GlobalMemory& g = save.globals;
		g.u32(global::kWorldTownCount) = 3;
		save.towns.resize(3);
		for (uint32_t k = 0; k < 7; ++k) {
			g.u8(0x6DE530 + 12 * k) = 0xFF;
		}
		g.u8(0x6DE530 + 12 * 4) = 2;
		g.u8(0x6DE530 + 12 * 4 + 1) = 3;
		g.u8(0x6DE530 + 12 * 4 + 2) = 1;
		g.u8(0x6DE530 + 12 * 4 + 3) = 25;
		g.u32(0x6DE530 + 12 * 4 + 4) = 0x12345;
		g.u32(0x6DE530 + 12 * 4 + 8) = 70;
		check(!town_order(save, 0) && !town_order(save, 7), "no order in an empty entry nor past the seventh");
		const std::optional<TownOrder> o = town_order(save, 4);
		check(o && o->town == 2 && o->ware == 3 && o->bonus && o->count == 25 && o->deadline == 0x12345 && o->price == 70,
			"the order 4");
		// The client: c = (the window's town 1 + ware 3 + count 25) & 3 = 1.
		save.towns[2].memory.u8(0x6E8 + 1) = 0x11;
		save.towns[2].memory.u8(0x6EC + 1) = 0x22;
		OperationBytes op{};
		save.merchants[0].memory.u16(0x22) = 19;
		check(!accept_order(save, 1, 4, op), "refused below the standing 20");
		save.merchants[0].memory.u16(0x22) = 0xFFF0;
		check(!accept_order(save, 1, 4, op), "refused with a negative standing");
		check(!accept_order(save, 1, 3, op), "no order to accept");
		save.merchants[0].memory.u16(0x22) = 20;
		check(accept_order(save, 1, 4, op), "accepted at the standing 20");
		uint16_t count, left, price;
		std::memcpy(&count, op.data() + 0xC, 2);
		std::memcpy(&left, op.data() + 0xE, 2);
		std::memcpy(&price, op.data() + 0x10, 2);
		check(op[0] == 0x3D && op[4] == 2 && op[5] == 3 && op[6] == 0x11 && op[7] == 0x22 && u32_at(op, 8) == 0x123FF &&
				  count == 25 && left == 25 && price == 70 && op[0x12] == 0 && op[0x13] == 1,
			"0x3D {town 2, beer, the client, the deadline | 0xFF, 25, 25, 70, the player, the bonus}");
	}
	{
		// The prince's decree: wares 5 and 7 lacking, both three months in the list ([+0x1014 + k] 3).
		SaveGame save = world(true);
		save.sieges.resize(1);
		save.siege_extras.resize(1);
		SiegeMemory& s = save.sieges[0];
		s.u8(0x1010) = 0xFF;
		check(!prince_decree(save, 0), "no decree with [+0x1010] 0xFF");
		const uint8_t wares[4] = {5, 7, 0xFF, 0xFF};
		for (uint32_t k = 0; k < 4; ++k) {
			s.u8(0x1010 + k) = wares[k];
			s.u8(0x1014 + k) = k < 2 ? 3 : 0;
		}
		s.u8(0x4) = 2;
		save.globals.u8(global::kWorldMonth) = 1;
		// (2 + 1) % 1 == 0 picks the ware 5; (2 + 1) % 2 == 1 leaves it.
		std::optional<PrinceDecree> d = prince_decree(save, 0);
		check(d && d->wares[0] == 5 && d->wares[1] == 7 && d->wares[2] == 0xFF && d->contract == 5, "the contract of 5");
		save.globals.u8(global::kWorldMonth) = 2;
		// (2 + 2) % 1 == 0 picks 5, (2 + 2) % 2 == 0 then 7.
		d = prince_decree(save, 0);
		check(d && d->contract == 7, "the contract of 7");
		GameMemory<8> node{};
		node.u8(0x3) = 0;
		save.siege_extras[0].list_101c.push_back(node);
		d = prince_decree(save, 0);
		check(d && d->wares[0] == 5 && d->contract == 0xFF, "no contract with the player's deliveries promised");
	}
	{
		// The page 2, «Town info» (0x5E29C0, 0x5E7C80): the mayor, the next election, the council's vote, the proposals.
		SaveGame save = world(true);
		GlobalMemory& g = save.globals;
		TownMemory& t = save.towns[0].memory;
		check(!council_town(save, 0), "no council without [+0x2C8] & 0xE0");
		t.u32(0x2C8) = 0x20;
		check(council_town(save, 0), "a council with 0x20");
		// A citizen mayor (0xFE: j = 1) and a merchant one.
		t.u8(0x6F1) = 0xFE;
		t.u8(0x6D5) = 11;
		t.u8(0x6D9) = 22;
		t.u8(0x6E1) = 3;
		t.u8(0x6E5) = 5;
		TownPerson p = town_mayor(save, 0);
		check(!p.merchant && p.first == 11 && p.family == 22 && p.title == 3 && p.rank == 5, "a citizen mayor: j = 1");
		t.u8(0x6F1) = 0;
		save.merchants[0].memory.u8(0x1A) = 0;
		save.merchants[0].memory.u8(0x1C) = 0x85;
		save.merchants[0].memory.u8(0x1D) = 9;
		save.merchants[0].memory.u8(0x19) = 0;
		save.merchants[0].memory.u8(0x39C) = 6;
		p = town_mayor(save, 0);
		check(p.merchant && p.female && p.first == 0x85 && p.family == 9 && p.rank == 6, "the player the mayoress");
		// The next election: the town's event 2 (its [+0xE] the town's [+0x2C0]).
		g.u16(global::kWorldYear) = 1300;
		g.u16(global::kWorldDayOfYear) = 100;
		g.u32(global::kWorldTick) = 0x10000;
		g.u16(0x6DD73C + 0x8) = 0;
		g.u16(0x6DD73C + 0xC) = 2;
		save.events.resize(2);
		TownDate d = next_mayor_election(save, 0);
		check(d.day == 31 && d.month == 12 && d.year == 1300, "no election event: 31 December");
		save.events[0].u32(0) = 0x10100;
		save.events[0].u16(4) = 1;
		save.events[0].u16(6) = 2;
		save.events[0].u8(0xE) = 0;
		save.events[0].u16(8) = 90;
		save.events[0].u8(0xF) = 14;
		save.events[0].u8(0x10) = 3;
		save.events[1].u32(0) = 0x10000 + 0x1000;
		save.events[1].u16(4) = 0xFFFF;
		save.events[1].u16(6) = 0x2E;
		save.events[1].u8(0xC) = 0;
		save.events[1].u8(0xD) = 3;
		save.events[1].u8(0x14) = 12;
		d = next_mayor_election(save, 0);
		check(d.day == 14 && d.month == 3 && d.year == 1301, "the election on its day 90, past it: next year");
		std::optional<CouncilVote> vote = council_vote(save, 0);
		check(vote && vote->event == 1 && vote->kind == 3 && vote->tax == 12, "the council's vote: the head tax at 12");
		// The proposals: the head tax's vote ahead (the bit 8 of 0x4D9F60), the military's total reached.
		t.u16(0x9A0) = 3;
		t.u8(0x998) = 2;
		t.u8(0x99C) = 1;
		std::array<bool, 4> open = proposals_open(save, 0);
		check(!open[0] && open[1] && !open[2] && open[3], "the walls (no room), the military, not the head tax");
		t.u16(0x9A0) = 4;
		check(!proposals_open(save, 0)[1], "the military under the council's total");
		t.s32(0x7A8) = 10;  // The town's grid 10 × 10: the wall's point 2 ([+0x7B4], [+0x7B5] at 0) inside it.
		t.s32(0x7AC) = 10;
		check(proposals_open(save, 0)[0], "the walls with room and the building list done");
		t.u16(0x776) = 1;
		check(!proposals_open(save, 0)[0], "the walls while buildings are listed");
		t.u32(0x2C8) = 0;
		open = proposals_open(save, 0);
		check(!open[0] && !open[1] && open[2] && open[3], "without a council only the taxes");
		t.u32(0x2C8) = 0x20;
		t.u8(0x6F8) = 7;
		const ProposalAmount head = proposal_amount(save, 0, 2);
		check(head.start == 7 && head.high == 20 && head.step == 1, "the head tax from the town's 7, up to 20");
		check(proposal_step(head, 20, 1) == 20 && proposal_step(head, 0, -1) == 0, "held at its ends");
		check(!proposal_ok(save, 0, 2, 7) && proposal_ok(save, 0, 2, 8), "OK for another head tax only");
		const ProposalAmount extra = proposal_amount(save, 0, 3);
		check(proposal_step(extra, 995000, 1) == 999999 && proposal_step(extra, 999999, -1) == 994999, "the extra tax's top");
		check(!proposal_ok(save, 0, 3, 0) && proposal_ok(save, 0, 3, 5000), "OK for an extra tax above 0");
		// «OK»: the tick now + 0xE00 moved past the head tax's vote (within 0x100 of it: 0x200 after it).
		save.events[1].u32(0) = 0x10000 + 0xE80;
		OperationBytes op = council_proposal(save, 0, 3, 15000);
		check(u32_at(op, 0) == 0x48 && u32_at(op, 4) == 0x10000 + 0xE00 + 0x200 && u32_at(op, 8) == 15000 && op[0xC] == 0 &&
				  op[0xD] == 0 && op[0xE] == 0 && op[0x10] == 0,
			"0x48 {the tick moved, 15000, town 0, the extra tax 0, the player}");
		op = council_proposal(save, 0, 2, 9);
		check(op[0xD] == 3 && op[0xE] == 9 && u32_at(op, 8) == 0, "0x48 the head tax 9");
		op = candidacy_operation(0, true);
		check(u32_at(op, 0) == 0x45 && u32_at(op, 4) == 0 && u32_at(op, 8) == 1, "0x45 {the player, standing}");
	}
	{
		// The town coffers (0x5E8380): this month's and the last one's balance; the 0x47.
		SaveGame save = world(true);
		TownMemory& t = save.towns[0].memory;
		const int32_t now[10] = {100, 20, 7, 50, 0, 11, 12, 13, 14, 30};  // [+0x704] .. [+0x728]
		for (uint32_t k = 0; k < 10; ++k) {
			t.s32(0x704 + 4 * k) = now[k];
		}
		t.s32(0x72C) = 9;
		t.s32(0x730) = 1000;
		t.s32(0x73C) = 5;
		t.s32(0x754) = 8;
		std::array<int32_t, 8> b = coffers_balance(save, 0, false);
		check(b == std::array<int32_t, 8>{120, 7, 20, -11, -12, -13, -14, -9}, "this month: 100 + 20, 7, 50 − 30, the costs");
		b = coffers_balance(save, 0, true);
		check(b[0] == 1000 && b[2] == -3, "the last month: 1000, 5 − 8");
		check(!coffers_operation(save, 0, true, 0), "nothing for 0");
		const std::optional<OperationBytes> op = coffers_operation(save, 0, true, 20000);
		check(op && u32_at(*op, 0) == 0x47 && u32_at(*op, 4) == 0 && u32_at(*op, 8) == 0 && u32_at(*op, 12) == 1 &&
				  u32_at(*op, 16) == 20000,
			"0x47 {the player, town 0, donating, 20000}");
	}
	{
		// The cellar's dish (0x5E60D2): (day % 31 · (town + 1) + town + town / 2) % 31.
		SaveGame save = world(true);
		save.globals.u32(global::kWorldTick) = 40u << 8;  // The day 40: 9.
		check(cellar_dish(save, 0) == 9, "the town 0 on the day 40: 9");
		check(cellar_dish(save, 5) == (9 * 6 + 5 + 2) % 31, "the town 5: (54 + 7) % 31 = 30");
	}
	{
		// The side menu's buttons (0x5A9780).
		SaveGame save = world(true);
		GlobalMemory& g = save.globals;
		save.sieges.resize(1);
		save.sieges[0].u8(0x1010) = 0xFF;
		g.u8(0x6DE52E) = 5;  // The Alderman's town elsewhere.
		g.u8(0x6DE52D) = 0xFF;
		save.towns[0].memory.u8(0x6F1) = 7;
		TownHallMenu menu = town_hall_menu(save, 0);
		check(!menu.on[0] && menu.on[1] && menu.on[2] && menu.on[3] && !menu.on[4] && menu.on[5] && !menu.on[6] &&
				  !menu.office && !menu.on[7],
			"the menu: no decree, no council, not the mayor, not the Alderman's town");
		g.u8(0x6DE530) = 0;  // An order of the town 0 in the world: still off (the game's slip).
		check(!town_hall_menu(save, 0).on[0], "an order alone leaves the notices off");
		save.sieges[0].u8(0x1010) = 3;
		check(town_hall_menu(save, 0).on[0], "the prince's decree: the notices on");
		save.towns[0].memory.u8(0x6F1) = 0;
		check(town_hall_menu(save, 0).on[6], "the mayor: the coffers on");
		g.u8(0x6DE52E) = 0;
		menu = town_hall_menu(save, 0);
		check(menu.office && !menu.on[7], "the Alderman's town: his office shown, off");
		save.merchants[0].memory.u8(0x37) = 1;
		check(town_hall_menu(save, 0).on[7], "[+0x37]: his office on");
		save.merchants[0].memory.u8(0x37) = 0;
		g.u8(0x6DE52D) = 0;
		check(town_hall_menu(save, 0).on[7], "the Alderman: his office on");
		// The council's vote of today (an event 0x2E of the town due today), the player of the rank 4 there.
		g.u8(0x6DE52E) = 5;
		g.u32(0x6DE4B4) = 0x1234;
		g.u16(0x6DD73C + 0x8) = 0;
		g.u16(0x6DD73C + 0xC) = 1;
		save.events.resize(1);
		save.events[0].u32(0) = 0x1280;
		save.events[0].u16(4) = 0xFFFF;
		save.events[0].u16(6) = 0x2E;
		save.events[0].u8(0xC) = 0;
		save.merchants[0].memory.u8(0x39C) = 4;
		check(!town_hall_menu(save, 0).on[4], "the council needs the town's [+0x2C8] & 0xE0");
		save.towns[0].memory.u32(0x2C8) = 0x40;
		check(town_hall_menu(save, 0).on[4], "the council's vote today, the rank 4: on");
		save.merchants[0].memory.u8(0x39C) = 3;
		check(!town_hall_menu(save, 0).on[4], "the rank 3: off");
		save.merchants[0].memory.u8(0x39C) = 4;
		save.events[0].u32(0) = 0x1300;
		check(!town_hall_menu(save, 0).on[4], "the vote tomorrow: off");
	}
	{
		// The outrigger's page (0x5E1A10): the terms from the average firepower, the strength's pictures, the cases.
		SaveGame save = world(true);
		GlobalMemory& g = save.globals;
		g.u16(global::kWorldMerchantCount) = 2;
		save.merchants.resize(2);
		g.u16(global::kShipCount) = 4;
		g.u16(0x6DD896) = 0;  // No convoys.
		g.u16(0x6DD892) = 1;  // One captain.
		save.ships.resize(4);
		// The player's ships 0 → 2 → 3 (two of them able to guard), the merchant 1's ship 1.
		save.merchants[0].memory.u16(0xE) = 0;
		save.merchants[1].memory.u16(0xE) = 1;
		const uint16_t next[4] = {2, 0xFFFF, 3, 0xFFFF};
		const int32_t firepower[4] = {80, 70, 0, 120};
		for (uint32_t k = 0; k < 4; ++k) {
			ShipMemory& s = save.ships[k].memory;
			s.u16(0x4) = next[k];
			s.u8(0x0) = k == 1 ? 1 : 0;
			s.s32(0x120) = firepower[k];
			s.u16(0x40) = 30;
			s.u16(0x42) = 0;
			s.u16(0x8) = 0xFFFF;
			s.u8(0x39) = 0;
			s.u16(0x134) = 0;
		}
		save.towns[0].memory.s32(0x2D4) = 1450;
		save.towns[0].memory.u16(0x994) = 0xFFFF;
		check(average_firepower(save) == 90, "the average of 80, 70 and 120 (the 0 left out)");
		// 90 − 9 = 81: sqrt(81 · 5.6) = 21.3 → 23; (21² = 441) / 5.6 = 78.75 → 78 + 10; 14 + (115 + 50) · 2.
		GuardShipTerms terms = guard_ship_terms(save, 0);
		check(terms.strength == 23 && terms.firepower == 88 && terms.pay == 344, "the terms: 23, 88, 344 a week");
		save.ships[3].memory.s32(0x120) = 75;  // (80 + 70 + 75) / 3 = 75: 66 · 5.6 = 369.6 → 19 + 2.
		terms = guard_ship_terms(save, 0);
		check(terms.strength == 21 && terms.firepower == 74 && terms.pay == 324, "the terms: 21, 74, 324 (save 1's)");
		save.ships[3].memory.s32(0x120) = 120;
		const std::vector<uint32_t> p74 = guard_ship_strength_pictures(74);
		check(p74 == std::vector<uint32_t>{31112, 31111}, "74: a big ball with a half, a big one");
		const std::vector<uint32_t> p81 = guard_ship_strength_pictures(81);
		check(p81 == std::vector<uint32_t>{31113, 31111}, "81: a big ball with a small one, a big one");
		check(guard_ship_strength_pictures(20) == std::vector<uint32_t>{31110, 31110, 31110, 31110, 31109},
			"20: four small balls and a half");
		for (uint32_t k = 0; k < 4; ++k) {
			save.ships[k].memory.s32(0x120) = firepower[k];
		}
		GuardShipPage page = guard_ship_page(save, 0);
		check(page.kind == GuardShipCase::Wanted && page.list && page.least == 88 && page.ships == std::vector<uint16_t>{3},
			"wanted: the player's ship 3 (120), not 0 (80)");
		save.ships[3].memory.u16(0x134) = 2;
		check(guard_ship_page(save, 0).ships.empty(), "not docked");
		save.ships[3].memory.u16(0x134) = 1;
		save.ships[3].memory.u16(0x40) = 19;
		check(guard_ship_page(save, 0).ships.empty(), "19 sailors: can't lead a convoy");
		save.ships[3].memory.u16(0x40) = 30;
		save.towns[0].memory.u16(0x994) = 1;
		page = guard_ship_page(save, 0);
		check(page.kind == GuardShipCase::Unsuitable && page.list && page.ships.size() == 1, "the merchant 1's 70 unsuitable");
		save.ships[1].memory.s32(0x120) = 110;  // The average 103: the least 96.
		page = guard_ship_page(save, 0);
		check(page.kind == GuardShipCase::Kept && !page.own && !page.list && page.ships.empty(), "kept by another merchant");
		save.towns[0].memory.u16(0x994) = 0;
		save.ships[0].memory.s32(0x120) = 105;  // The average 111: the least 104.
		page = guard_ship_page(save, 0);
		check(page.kind == GuardShipCase::Kept && page.own && page.least == 106 && page.ships == std::vector<uint16_t>{3},
			"kept by the player: better ships only (106)");
		const OperationBytes op = guard_ship_operation(0, 3, 344);
		check(u32_at(op, 0) == 0x44 && u32_at(op, 4) == 0 && u32_at(op, 8) == 3 && u32_at(op, 12) == 344,
			"0x44 {town 0, ship 3, pay 344}");
	}
	if (failures) {
		std::fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	std::printf("town_hall_window: ok\n");
	return 0;
}
