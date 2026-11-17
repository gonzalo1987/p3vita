// The ship list window (p3data/ship_list_window.hpp), without game data: its two lists (0x474B70), its sorts
// (0x4779D0), the others' figures off by their number (0x495340, 0x4954C0, 0x4953C0, 0x495450), the coins (0x493C20),
// the status column (0x473CD2), the last column (0x490F80) and the window's layout (0x4751D0).

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "p3data/game_memory.hpp"
#include "p3data/player_actions.hpp"
#include "p3data/ship_list_window.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

void name(SaveGame& save, uint16_t ship, const char* n) {
	std::memset(save.ships[ship].memory.bytes.data() + 0x160, 0, 32);
	std::memcpy(save.ships[ship].memory.bytes.data() + 0x160, n, std::strlen(n));
}

// Three merchants (the player 0, merchant 1 by the computer, merchant 2 a human) and two towns; the player's ships 0..4
// along his list 4, 0, 2, 1, 3 (ship 3 in the state 0x17), merchant 1's 5 and 6, merchant 2's 7; one convoy (0) of
// the ships 0 and 2 guided by 2.
SaveGame world() {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 2;
	g.u16(global::kWorldMerchantCount) = 3;
	g.u16(global::kShipCount) = 8;
	g.u16(global::kShipCount + 2) = 1;  // The convoys.
	g.u32(global::kPlayer) = 0;
	save.towns.resize(2);
	save.merchants.resize(3);
	save.ships.resize(8);
	save.convoys.resize(1);
	save.merchants[0].memory.u16(0xE) = 4;
	save.merchants[1].memory.u16(0xE) = 5;
	save.merchants[2].memory.u16(0xE) = 7;
	save.merchants[1].memory.u16(0x8) = 1;
	const uint16_t next[8] = {2, 3, 1, 0xFFFF, 0, 6, 0xFFFF, 0xFFFF};
	const uint8_t owner[8] = {0, 0, 0, 0, 0, 1, 1, 2};
	const uint8_t town[8] = {0, 1, 0, 0, 0, 0, 1, 0};
	const uint16_t state[8] = {0, 0, 0x0F, 0x17, 2, 1, 0, 3};
	const char* names[8] = {"Mowe", "Adler", "Kogge", "Free", "Bremen", "Falke", "Ente", "Wal"};
	for (uint16_t i = 0; i < 8; ++i) {
		ShipMemory& s = save.ships[i].memory;
		s.u16(0x4) = next[i];
		s.u8(0x0) = owner[i];
		s.u8(0x39) = town[i];
		s.u16(0x134) = state[i];
		s.u16(0x8) = 0xFFFF;
		s.u8(0x38) = 0xFF;
		s.s32(0x14) = 100;
		s.s32(0x18) = 100;
		name(save, i, names[i]);
	}
	save.ships[0].memory.u16(0x8) = 0;
	save.ships[2].memory.u16(0x8) = 0;
	save.convoys[0].u16(0x10) = 2;
	save.convoys[0].u16(0x2) = 0xFFFF;
	return save;
}

void lists() {
	const SaveGame save = world();
	const ShipList own = merchant_ship_list(save, 0);
	check(own.ships == std::vector<uint16_t>({4, 0, 2, 1}), "the player's ships along his list, not the state 0x17");
	check(own.own == 4 && own.others == 0, "the player's list: one table");
	// The port of town 0: his ships there in the states 0..3 (4 leaving, 0 docked; 2 sails, 3 is free), then merchant
	// 2's (7, arriving) before merchant 1's (5 waiting; 6 is in town 1).
	const ShipList port = port_ship_list(save, 0, 0);
	check(port.ships == std::vector<uint16_t>({4, 0, 7, 5}), "the port's ships: his, then the others' from the last merchant");
	check(port.own == 2 && port.others == 2, "the port's two tables");
	// The sea map's town: his ships alone.
	const ShipList mine = port_ship_list(save, 0, 0, false);
	check(mine.ships == std::vector<uint16_t>({4, 0}) && mine.others == 0, "the port's ships without the others'");
	// A list given: as given, his counted as the first table's.
	const ShipList given = given_ship_list(save, 0, {2, 0, 1});
	check(given.ships == std::vector<uint16_t>({2, 0, 1}) && given.own == 3 && given.others == 0, "a list given");
	// The sea map's click: his ships sailing (2) or in the Hanse's fleet (0x16) that the test hits.
	SaveGame sea = world();
	sea.ships[0].memory.u16(0x134) = 0x16;
	sea.ships[1].memory.u16(0x134) = 0x0F;
	const std::vector<uint16_t> hit = sea_map_ships_at(sea, 0, [](uint16_t s) { return s != 1; });
	check(hit == std::vector<uint16_t>({0, 2}), "the sea map's click: his ships at sea under it");
}

void sorts() {
	SaveGame save = world();
	ShipList list = merchant_ship_list(save, 0);
	check(ship_list_sort_kind(kShipListDefaultSort) == 0, "the window starts sorted by name");
	sort_ship_list(save, list, 1);
	check(list.ships == std::vector<uint16_t>({1, 4, 2, 0}), "by name: Adler, Bremen, Kogge, Mowe");
	// By convoy: the ships of the convoy first (by their guide's name, then their own), then the others by name.
	sort_ship_list(save, list, 0);
	check(list.ships == std::vector<uint16_t>({2, 0, 1, 4}), "by convoy: Kogge, Mowe, then Adler, Bremen");
	check(ship_list_convoy_line(save, list, 1, 0), "a line under the convoy's last ship");
	check(!ship_list_convoy_line(save, list, 0, 0) && !ship_list_convoy_line(save, list, 1, 1), "no line within a convoy or by name");
	check(!ship_list_convoy_line(save, list, 3, 0), "no line under the table's last row");
	// By type: the cogs (2) after the craiers (1), each type by name.
	save.ships[0].memory.u8(0xE) = 1;
	save.ships[4].memory.u8(0xE) = 1;
	save.ships[1].memory.u8(0xE) = 2;
	save.ships[2].memory.u8(0xE) = 2;
	sort_ship_list(save, list, 2);
	check(list.ships == std::vector<uint16_t>({4, 0, 1, 2}), "by type, then by name");
	// By condition: the worst hull first, equal hulls by name.
	save.ships[0].memory.s32(0x18) = 50;
	save.ships[2].memory.s32(0x18) = 50;
	save.ships[1].memory.s32(0x18) = 90;
	sort_ship_list(save, list, 4);
	check(list.ships == std::vector<uint16_t>({2, 0, 1, 4}), "by condition, then by name");
	// By weapons: the weakest first.
	save.ships[4].memory.s32(0x120) = 10;
	save.ships[0].memory.s32(0x120) = 30;
	save.ships[2].memory.s32(0x120) = 20;
	save.ships[1].memory.s32(0x120) = 20;
	sort_ship_list(save, list, 3);
	check(list.ships == std::vector<uint16_t>({4, 1, 2, 0}), "by weapons, then by name");
	// Each table alone: the port's others stay after his ships.
	ShipList port = port_ship_list(save, 0, 0);
	sort_ship_list(save, port, 1);
	check(port.ships == std::vector<uint16_t>({4, 0, 5, 7}), "the port's tables sorted apart");
}

void figures() {
	SaveGame save = world();
	// The player's own: exact.
	save.ships[0].memory.s32(0x18) = 37;
	check(shown_hull(save, 0) == 37 && ship_list_hull_red(save, 0), "his hull exact, red under 40");
	// Others' off by their number: ship 3 (n % 20 − 10 = −7) at 80 % shows 80 − ⌊⌊−7 · 80 · 65536 / 100⌋ / 65536⌋ = 86.
	ShipMemory& s = save.ships[3].memory;
	s.u8(0x0) = 1;
	s.s32(0x18) = 80;
	check(shown_hull(save, 3) == 86, "another's hull, off by −7 %");
	s.s32(0x18) = 98;
	check(shown_hull(save, 3) == 100, "another's hull at most 100");
	// Ship 7 (n % 20 − 10 = −3) firepower 50: −1.5 shifted down to −2, so 52.
	save.ships[7].memory.s32(0x120) = 50;
	check(shown_firepower(save, 7) == 52, "another's firepower");
	// Ship 7's cargo (7 − 35 = −28) 50000: its 250 two hundreds off by 70 → 64000.
	save.ships[7].memory.s32(0x118) = 50000;
	check(shown_cargo(save, 7) == 64000, "another's cargo, in two hundreds");
	// Ship 5's sailors (5 − 20 = −15) 30: −4.5 shifted down to −5, so 35, at most what it takes (a hulk without levels: 20).
	save.ships[5].memory.u16(0x40) = 30;
	save.ships[5].memory.u8(0xE) = 3;
	save.ships[5].memory.u8(0xF) = 0;
	const uint32_t most = ship_most_sailors(save, 5);
	check(shown_sailors(save, 5) == std::min<uint32_t>(35, most), "another's sailors");
	save.ships[5].memory.u16(0x40) = 0;
	check(shown_sailors(save, 5) == 1, "another's sailors at least 1");
}

void columns() {
	SaveGame save = world();
	check(ship_list_coins(0) == 0 && ship_list_coins(-5) == 1 && ship_list_coins(3249) == 1, "one coin under 3250");
	check(ship_list_coins(3250) == 2 && ship_list_coins(129249) == 7 && ship_list_coins(129250) == 8, "up to 8 coins");
	// The status: docked at anchor in its town; sailing without a destination nor route at anchor «at sea», with its
	// convoy's guide's route sailing; with a destination to it.
	ShipListStatus st = ship_list_status(save, 0);
	check(st.tex == 16042 && st.words == ShipListWords::Town && !st.grey, "docked: its town at anchor");
	save.ships[0].memory.u16(0x134) = 0x0F;
	st = ship_list_status(save, 0);
	check(st.tex == 16042 && st.words == ShipListWords::AtSea, "at sea without a route");
	save.ships[2].memory.u32(0x24) = 1;
	check(ship_list_status(save, 0).tex == 16043, "at sea, its convoy's guide with a route");
	save.ships[0].memory.u8(0x38) = 1;
	check(ship_list_status(save, 0).words == ShipListWords::Destination, "sailing to a town");
	save.ships[0].memory.u16(0x134) = 0x0C;
	st = ship_list_status(save, 0);
	check(st.tex == 16025 && st.words == ShipListWords::Auction && st.grey, "at an auction, grey");
	save.ships[0].memory.u16(0x134) = 0x11;
	check(ship_list_status(save, 0).tex == 16050 && ship_list_action(save, 0) == ShipListAction::Delete, "a lost ship");
	save.ships[0].memory.u16(0x134) = 0x13;
	check(ship_list_action(save, 0) == ShipListAction::None && !ship_list_can_choose(save, 0), "an expedition");
	save.ships[0].memory.u16(0x134) = 0x0E;
	check(ship_list_action(save, 0) == ShipListAction::GoToOnly, "being built: gone to, not chosen");
	// The last column: the convoy's guide and member in the player's blue; the guide on a trade route marks both.
	ShipListMarks m = ship_list_marks(save, 2);
	check(m.tex == 16002 && m.colour == 0xFF0000FF && !m.trade_route, "the guide of his convoy");
	save.ships[2].memory.u8(0x136) = 1;
	m = ship_list_marks(save, 0);
	check(m.tex == 16004 && m.trade_route, "a member, the guide on a trade route");
	save.ships[5].memory.u16(0x8) = 0;
	check(ship_list_marks(save, 5).colour == 0xFFFFFFFF, "another merchant's by the computer: white");
	save.ships[7].memory.u16(0x8) = 0;
	check(ship_list_marks(save, 7).colour == 0xFFFF0000, "another human's: red");
	save.ships[7].memory.u8(0x15C) = 1;
	check(ship_list_marks(save, 7).colour == 0xFF282828, "a pirate in a convoy: dark");
	save.ships[1].memory.u8(0x15C) = 1;
	m = ship_list_marks(save, 1);
	check(m.tex == 16003 && !m.trade_route, "a pirate alone");
	save.convoys[0].u8(0x14) = 0x10;
	check(ship_list_marks(save, 2).tex == 0, "a convoy with 0x10: no picture");
}

void layout() {
	check(ship_list_column_x(1) == 111 && ship_list_column_x(3) == 277 && ship_list_column_x(6) == 409, "the columns");
	// The Vita's town (676 × 510 under the bar): 544 wide (32 + 32 + 465 + 27 in tiles).
	ShipListLayout l = ship_list_layout(0, 34, 676, 510, 5, 0, 27);
	check(l.w == 544 && l.h == 256 && l.x == 68 && l.y == 50 && l.rows1 == 5, "five ships");
	l = ship_list_layout(0, 34, 676, 510, 30, 0, 27);
	check(l.h == 480 && l.rows1 == 17 && l.y == 49, "thirty ships: the view's tiles, 17 rows");
	l = ship_list_layout(0, 34, 676, 510, 3, 4, 27);
	check(l.h == 352 && l.rows1 == 3 && l.rows2 == 4 && l.chart2_y == 163, "two tables");
	l = ship_list_layout(0, 34, 676, 510, 10, 20, 27);
	check(l.h == 480 && l.rows1 == 7 && l.rows2 == 8, "two tables sharing the rows");
	l = ship_list_layout(0, 34, 676, 510, 2, 20, 27);
	check(l.rows1 == 2 && l.rows2 == 13, "the few own rows whole");
	l = ship_list_layout(0, 34, 676, 510, 0, 30, 27);
	check(l.rows1 == 0 && l.rows2 == 17, "the others' alone");
}

}  // namespace

int main() {
	lists();
	sorts();
	figures();
	columns();
	layout();
	if (failures == 0) {
		std::printf("ship_list_window: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
