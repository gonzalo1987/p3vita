// The ship menu's convoy rules (p3data/ship_menu_window.hpp), without game data: which ship shows its convoy
// (0x495190), the convoy's list (0x48FE80) and its head's figures (0x4906E0, 0x492150), blurred for another's; «form
// convoy» with several ships (0x48FC00), the convoy window's layout (0x40C5F0) and the selection list's (0x46F9B0).

#include <cstdio>

#include "p3data/game_memory.hpp"
#include "p3data/ship_menu_window.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

// The player is merchant 0. Convoy 0's chain: 2, 0, 3 (free), 1; its guide 0; ships 0 and 1 merchant 1's, 2 the
// player's; ship 4 out of any convoy.
SaveGame world() {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u16(global::kShipCount) = 5;
	g.u16(global::kShipCount + 2) = 1;
	g.u32(global::kPlayer) = 0;
	save.merchants.resize(2);
	save.ships.resize(5);
	save.convoys.resize(1);
	save.convoys[0].u16(0xA) = 2;
	save.convoys[0].u16(0x10) = 0;
	const uint16_t next[5] = {3, 0xFFFF, 0, 1, 0xFFFF};
	const uint8_t owner[5] = {1, 1, 0, 1, 0};
	const uint16_t state[5] = {0, 0, 0, 0x17, 0};
	const int32_t health[5] = {80, 50, 100, 100, 100};
	for (uint16_t i = 0; i < 5; ++i) {
		ShipMemory& s = save.ships[i].memory;
		s.u16(0x6) = next[i];
		s.u8(0x0) = owner[i];
		s.u16(0x134) = state[i];
		s.u16(0x8) = i == 4 ? 0xFFFF : 0;
		s.s32(0x14) = 100;
		s.s32(0x18) = health[i];
	}
	ShipMemory& a = save.ships[0].memory;
	a.u16(0x40) = 10;
	a.s32(0x120) = 5;
	a.s32(0x54) = 400;
	a.s32(0x58) = 600;
	a.u8(0x156) = 1;
	ShipMemory& b = save.ships[1].memory;
	b.u16(0x40) = 20;
	b.s32(0x120) = 15;
	b.s32(0x54) = 1000;
	save.ships[2].memory.s32(0x10) = 50000;
	save.ships[2].memory.u16(0x40) = 99;
	return save;
}

}  // namespace

int main() {
	SaveGame save = world();
	check(ship_menu_shows_convoy(save, 0, -1), "a convoy's ship shows its convoy");
	check(!ship_menu_shows_convoy(save, 0, 0), "touched again while its convoy is shown: the ship alone");
	check(!ship_menu_shows_convoy(save, 4, -1), "a ship out of any convoy: itself");
	const ConvoyMenuList list = convoy_menu_list(save, 0);
	check(list.ships == std::vector<uint16_t>({2, 0, 1}) && list.pointed == 1, "the convoy's chain, its guide pointed");
	// Merchant 1's (the guide's owner): sailors 30, firepower 20, cargo 400 + 600 + 2000 + 1000 = 4000, the hull the
	// average of 80, 50 and 100 = 76, the capacity the player's 50000; blurred by the guide's number 0 (k 20: −10 %, k
	// 40: −20 %) and the convoy's 0 (k 70: −35 %, the 32-bit product wrapped): hull 84, firepower 22, sailors 36,
	// cargo 4090.
	const ConvoyMenuHead h = convoy_menu_head(save, list);
	check(h.hull == 84, "the hull's average, blurred");
	check(h.firepower == 22 && h.sailors == 36, "the firepower and the sailors, blurred");
	check(h.cargo == 4090, "the cargo, blurred by the convoy's number");
	check(h.capacity == 50000, "the capacity of the player's ships only");
	// The player's own convoy: exact (the guide his); the equipment of his ships each rounded up to a load (150 → 200,
	// 400 stays, merchant 1's 30 not counted).
	save.ships[0].memory.u8(0x0) = 0;
	save.ships[0].memory.s32(0x11C) = 150;
	save.ships[2].memory.s32(0x11C) = 400;
	save.ships[1].memory.s32(0x11C) = 30;
	const ConvoyMenuHead mine = convoy_menu_head(save, list);
	check(mine.hull == 76 && mine.sailors == 99 + 10 && mine.firepower == 5 && mine.cargo == 3000,
		"the player's convoy: his ships' figures as they are");
	check(mine.equipment == 600, "the player's convoy: the equipment in whole loads");

	// The pirate's name (0x612EE0): ship 4 alone without a captain: unknown; with captain 1 (names 7, 9): known; in convoy
	// 0 its guide's captain (ship 0's); in a battle the battle's ([+0x1E90], [+0x1E91]; both 0xFF unknown).
	save.globals.u16(global::kCaptainCount) = 2;
	save.captains.resize(2);
	save.captains[1].u8(0x2) = 7;
	save.captains[1].u8(0x3) = 9;
	for (SavedShip& s : save.ships) {
		s.memory.u16(0x42) = 0xFFFF;
	}
	check(!pirate_name(save, 4).known, "no captain, no battle: an unknown pirate");
	save.ships[4].memory.u16(0x42) = 1;
	PirateName p = pirate_name(save, 4);
	check(p.known && p.first == 7 && p.family == 9, "its captain's names");
	save.ships[0].memory.u16(0x42) = 1;
	p = pirate_name(save, 1);
	check(p.known && p.first == 7 && p.family == 9, "in a convoy its guide's captain");
	// A battle in slot 1 with ships 3 and 4 in its list.
	BattleGraph& g = save.battles;
	g.set<uint8_t>(0, 0x404, 2);
	const size_t battle = g.alloc(0x1E98);
	g.set_ptr(0, 4, int(battle));
	g.set<uint16_t>(battle, 0x14, 2);
	g.set<uint8_t>(battle, 0x1E90, 0xFF);
	g.set<uint8_t>(battle, 0x1E91, 0xFF);
	const size_t ships = g.alloc(8);
	g.set_ptr(battle, 0x4, int(ships));
	for (uint32_t k = 0; k < 2; ++k) {
		const size_t entry = g.alloc(0x164);
		g.set<uint32_t>(entry, 0x4, 3 + k);
		g.set_ptr(ships, 4 * k, int(entry));
	}
	check(!pirate_name(save, 4).known, "in a battle without the pirate's names: unknown, whatever its captain");
	g.set<uint8_t>(battle, 0x1E90, 3);
	g.set<uint8_t>(battle, 0x1E91, 5);
	p = pirate_name(save, 4);
	check(p.known && p.first == 3 && p.family == 5, "in a battle the battle's pirate");

	// «Form convoy» with several ships chosen (0x48FC00): ships 2 and 4 are the player's; 2 is in convoy 0.
	save = world();
	save.ships[2].memory.u8(0x39) = 7;
	save.ships[4].memory.u8(0x39) = 7;
	check(!ships_can_form_convoy(save, {4, 2}), "form convoy: off with a ship in a convoy");
	save.ships[2].memory.u16(0x8) = 0xFFFF;
	check(ships_can_form_convoy(save, {4, 2}), "form convoy: on, both docked in one town and in no convoy");
	save.ships[2].memory.u16(0x134) = 1;
	check(ships_can_form_convoy(save, {4, 2}), "form convoy: the state 1 too");
	save.ships[2].memory.u16(0x134) = 2;
	check(!ships_can_form_convoy(save, {4, 2}), "form convoy: off with a ship leaving");
	save.ships[2].memory.u16(0x134) = 0;
	save.ships[2].memory.u8(0x39) = 8;
	check(!ships_can_form_convoy(save, {4, 2}), "form convoy: off with ships in two towns");
	save.ships[2].memory.u8(0x39) = 7;
	save.ships[0].memory.u16(0x8) = 0xFFFF;
	save.ships[0].memory.u8(0x39) = 7;
	check(!ships_can_form_convoy(save, {4, 0}), "form convoy: off with another merchant's ship");

	// The convoy window's layout (0x40C5F0), as the game's traced in the emulator (re/emu/sesion42/convoytrace.sh 3 0,
	// two choices, font 1's height 15, a screen of 1024 × 768; its scroll bar the init's, its thumb 27 wide): 384 × 160
	// at (320, 304).
	ConvoyWindowLayout c = convoy_window_layout(0, 0, 1024, 768, 2, 15, 27);
	check(c.x == 320 && c.y == 304 && c.w == 384 && c.h == 160, "convoy window: the traced place and size");
	check(c.rows == 4, "convoy window: four rows shown in 160 (60 over them, 40 under)");
	c = convoy_window_layout(0, 0, 1024, 768, 2, 15, 0);
	check(c.w == 352, "convoy window: without a scroll bar 11 tiles (the list's 14 + 336 rounded up)");
	c = convoy_window_layout(0, 24, 690, 520, 40, 15, 27);
	check(c.h == 512 && c.y == 28 && c.rows == 27, "convoy window: as high as the view's whole tiles at most");
	check(c.x == 154, "convoy window: centred (153), x pushed on by its remainder of 4 (1)");

	// The selection list's layout (0x46F9B0): the offer list's (mode 3, the viewer's offer window) 7 tiles wide; towns.
	SelectionListLayout s = selection_list_layout(0, 0, 1024, 768, 3, 100, 98);
	check(s.w == 224 && s.rows_width == 160, "selection list: the button's 133 rounded up a tile, and a tile each side");
	check(s.h == ((3 * 16 + 62) / 32 + 3) * 32, "selection list: (rows · 16 + 62) / 32 + 3 tiles high");
	check(s.x == 1024 - 168 && s.y == (768 - s.h) / 2, "selection list: three quarters of it in the view, centred");
	s = selection_list_layout(0, 24, 690, 520, 40, 150, 98);
	check(s.rows_width == 160 && s.w == 224, "selection list: a row of 150 within the button's 160");
	check(s.h == 512 && s.rows == (512 - 64 - 62) / 16, "selection list: the view's whole tiles and the rows in them");
	s = selection_list_layout(0, 0, 1024, 768, 3, 170, 98);
	check(s.rows_width == 192, "selection list: a wider row, a tile more");
	if (failures == 0) {
		std::printf("ship_menu_window: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
