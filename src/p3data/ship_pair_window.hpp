#pragma once

#include <cstdint>
#include <vector>

#include "p3data/player_actions.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// Two ships of the player in the same place (the rules; the viewer draws them its way): the window *0x6CBAAC, opened
// by 0x40EEB0 (A, B) when the player picks another of his ships in the town with one chosen (0x595982), its update
// 0x40F210. Three rows, each with a button towards each ship: the captain, the sailors and the hand weapons. The
// operations are the engine's (town_tick/ship_operations.cpp: 0x0F, 0x10, 0x11).

// 0x40EEB0's checks (and the update's, which closes the window otherwise): A and B different, both docked (state 0 or
// 1), in the same town ([+0x39]) and both the player's.
bool can_pair_ships(const SaveGame& save, uint32_t player, int a, int b);

// Which buttons are on (0x40F462..0x40F5AC). [0] moves from B to A (the window's left column), [1] from A to B. A ship
// that guides a convoy without the bit 1 of its [+0x14] (ships of other owners in it) gives neither its captain nor
// its sailors.
//   captain: the giver has one ([+0x42] below the captains [0x6DD892]); either button swaps them (0x0F);
//   sailors: the giver has some ([+0x40]), the taker has fewer than it can take (0x5185C0) and hires some (0x5184F0
//            above 0, unsigned);
//   hand weapons: the giver has some ([+0x154]) and the taker's hold has room for ten units (0x5182B0 ≥ 10).
struct ShipPairButtons {
	bool captain[2] = {false, false};
	bool sailors[2] = {false, false};
	bool weapons[2] = {false, false};
};
ShipPairButtons ship_pair_buttons(const SaveGame& save, int a, int b);

// The bytes (0x40F615..0x40F720): {0x0F, A, B}; {0x10, from, to, 1}; {0x11, from, to, 1}.
OperationBytes swap_captains_operation(int a, int b);
OperationBytes pass_sailors_operation(int from, int to);
OperationBytes pass_hand_weapons_operation(int from, int to);

// --- The sea map: a ship's target (0x44FB40, the right button with one ship of the player's chosen) ---

// What the sea map lets the player's chosen ship `a` take as its target: only its [+0x50] and [+0x52] (the ships the
// simulation keeps near it), in that order; each drawn (hit and checked) as the guide of its convoy, which must sail
// (0xF) with `a` a pirate ([+0x15C]), flee (0x12) or be a pirate (0x44FBE1..0x44FC6B, 0x44FEBB..0x44FF4F). The
// operation names the ship itself, not the guide. The engine checks the rest (a sailing, the ship within reach:
// town_tick/ship_operations.cpp fn_538800).
struct SeaTarget {
	int ship;   // the [+0x50] or [+0x52] sent
	int drawn;  // the ship tapped: it, or its convoy's guide
};
std::vector<SeaTarget> sea_targets(const SaveGame& save, int a);
OperationBytes target_operation(int a, int b);

}  // namespace p3::data
