// The ship menu's convoy rules (p3data/ship_menu_window.hpp), without game data: which ship shows its convoy
// (0x495190), the convoy's list (0x48FE80) and its head's figures (0x4906E0, 0x492150), blurred for another's.

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
	// The player's own convoy: exact (the guide his).
	save.ships[0].memory.u8(0x0) = 0;
	const ConvoyMenuHead mine = convoy_menu_head(save, list);
	check(mine.hull == 76 && mine.sailors == 99 + 10 && mine.firepower == 5 && mine.cargo == 3000,
		"the player's convoy: his ships' figures as they are");
	if (failures == 0) {
		std::printf("ship_menu_window: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
