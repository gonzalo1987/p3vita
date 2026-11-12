// Two ships of the player in the same place (p3data/ship_pair_window.hpp), without game data: when the window opens,
// which of its buttons are on (0x40F210) and their bytes.

#include <cstdio>
#include <cstring>

#include "p3data/ship_pair_window.hpp"

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

// The player's two snaikkas docked in town 3, each 100 loads (it takes 100 sailors at most) with 10 sailors; A with
// the captain 0 and 5 hand weapons, B without either; one convoy record, unused.
SaveGame world() {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u16(global::kShipCount) = 3;
	g.u16(global::kCaptainCount) = 1;
	g.u16(global::kShips + 0xF6) = 1;  // the convoys
	save.ships.resize(3);
	save.convoys.resize(1);
	save.convoys[0].u16(0x10) = 0xFFFF;
	for (int i = 0; i < 3; ++i) {
		ShipMemory& s = save.ships[size_t(i)].memory;
		s.u8(0x0) = 0;
		s.u16(0x8) = 0xFFFF;
		s.u8(0xE) = 0;
		s.u8(0xF) = 0;
		s.s32(0x10) = 100 * 2000;
		s.u8(0x39) = 3;
		s.u16(0x134) = i == 1 ? 1 : 0;
		s.u16(0x40) = 10;
		s.u16(0x42) = 0xFFFF;
		for (int slot = 0; slot < 24; ++slot) {
			s.u8(0x13C + slot) = 0xFF;
		}
	}
	save.ships[0].memory.u16(0x42) = 0;
	save.ships[0].memory.u16(0x154) = 5;
	save.ships[2].memory.u8(0x0) = 1;  // another merchant's
	return save;
}

}  // namespace

int main() {
	{
		SaveGame save = world();
		check(can_pair_ships(save, 0, 0, 1), "two docked ships of the player in one town");
		check(!can_pair_ships(save, 0, 0, 0), "not the same ship");
		check(!can_pair_ships(save, 0, 0, 2), "not another merchant's ship");
		check(!can_pair_ships(save, 1, 0, 1), "not another player's");
		save.ships[1].memory.u16(0x134) = 4;
		check(!can_pair_ships(save, 0, 0, 1), "not one in the shipyard");
		save.ships[1].memory.u16(0x134) = 0;
		save.ships[1].memory.u8(0x39) = 4;
		check(!can_pair_ships(save, 0, 0, 1), "not in two towns");
	}
	{
		SaveGame save = world();
		const ShipPairButtons on = ship_pair_buttons(save, 0, 1);
		check(!on.captain[0] && on.captain[1], "the captain: only A has one");
		check(on.sailors[0] && on.sailors[1], "the sailors: both ways");
		check(!on.weapons[0] && on.weapons[1], "the hand weapons: only A has some");
		save.ships[1].memory.u16(0x40) = 0;
		save.ships[0].memory.u16(0x40) = 100;
		const ShipPairButtons full = ship_pair_buttons(save, 0, 1);
		check(!full.sailors[0], "B has none to give (and A is full)");
		save.ships[1].memory.u16(0x40) = 10;
		check(!ship_pair_buttons(save, 0, 1).sailors[0], "A is full: it takes none");
		save.ships[0].memory.u16(0x40) = 10;
		save.ships[1].memory.s32(0x54) = 100 * 2000;  // B's hold full of grain
		check(!ship_pair_buttons(save, 0, 1).weapons[1], "B's hold without room for ten units");
	}
	{
		SaveGame save = world();
		save.ships[0].memory.u16(0x8) = 0;  // A guides the convoy 0
		save.convoys[0].u16(0x10) = 0;
		save.convoys[0].u8(0x14) = 0;
		const ShipPairButtons guide = ship_pair_buttons(save, 0, 1);
		check(!guide.captain[1] && !guide.sailors[1] && guide.weapons[1],
			"a convoy with other owners' ships: its guide keeps its captain and sailors");
		save.convoys[0].u8(0x14) = 2;
		const ShipPairButtons own = ship_pair_buttons(save, 0, 1);
		check(own.captain[1] && own.sailors[1], "all of one owner: it gives them");
	}
	{
		// The sea map's target: only the chosen ship's [+0x50] and [+0x52]; a convoy's ship drawn as its guide.
		SaveGame save = world();
		ShipMemory& a = save.ships[0].memory;
		a.u16(0x134) = 0xF;
		a.u16(0x50) = 1;
		a.u16(0x52) = 2;
		save.ships[1].memory.u16(0x134) = 0x12;  // fleeing
		save.ships[2].memory.u16(0x134) = 0xF;   // sailing, not a pirate: only for a pirate
		std::vector<SeaTarget> t = sea_targets(save, 0);
		check(t.size() == 1 && t[0].ship == 1 && t[0].drawn == 1, "the fleeing one; not a plain sailing one");
		a.u8(0x15C) = 1;  // the chosen ship a pirate
		t = sea_targets(save, 0);
		check(t.size() == 2 && t[1].ship == 2, "a pirate takes a sailing one too, [+0x50] first");
		save.ships[2].memory.u16(0x8) = 0;  // ship 2 in the convoy 0, guided by ship 1
		save.convoys[0].u16(0x10) = 1;
		t = sea_targets(save, 0);
		check(t.size() == 2 && t[1].ship == 2 && t[1].drawn == 1, "a convoy's ship: its guide is tapped, it is sent");
		a.u16(0x50) = 0xFFFF;
		a.u16(0x52) = 0xFFFF;
		check(sea_targets(save, 0).empty(), "nothing near: no target");
		const OperationBytes op = target_operation(0, 2);
		check(u32_at(op, 0) == 0x0E && u32_at(op, 4) == 0 && u32_at(op, 8) == 2, "0x0E: the chosen ship, its target");
	}
	{
		const OperationBytes swap = swap_captains_operation(4, 7);
		check(u32_at(swap, 0) == 0x0F && u32_at(swap, 4) == 4 && u32_at(swap, 8) == 7 && u32_at(swap, 0xC) == 0,
			"0x0F: A, B");
		const OperationBytes sailors = pass_sailors_operation(7, 4);
		check(u32_at(sailors, 0) == 0x10 && u32_at(sailors, 4) == 7 && u32_at(sailors, 8) == 4 && u32_at(sailors, 0xC) == 1,
			"0x10: from, to, 1");
		const OperationBytes weapons = pass_hand_weapons_operation(4, 7);
		check(u32_at(weapons, 0) == 0x11 && u32_at(weapons, 4) == 4 && u32_at(weapons, 8) == 7 && u32_at(weapons, 0xC) == 1,
			"0x11: from, to, 1");
	}
	if (failures == 0) {
		std::printf("ship_pair_window: OK\n");
	}
	return failures == 0 ? 0 : 1;
}
