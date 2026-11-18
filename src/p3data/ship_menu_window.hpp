#pragma once

#include <cstdint>
#include <vector>

#include "p3data/savegame.hpp"

namespace p3::data {

// The side panel's ship menu (CSideMenu_Ship, vtable 0x66F358, *0x6CE6D0) as the game shows a convoy: the rules; the
// viewer draws them (notes: re/emu/sesion40/README.md, «Part 2»).

// A ship chosen in the ship list or on the sea map (0x495190, the selection's flags): one in a convoy ([+0x8] under
// the convoys' count) shows its convoy (the flag 8), unless the menu already shows that convoy so (then the ship
// alone); in a battle the deck (2, not here). The menu then takes the convoy's state (0x488080: 2 the player's, 4
// another's) when the convoy's [+0x14] hasn't 0x10, else the ship's.
bool ship_menu_shows_convoy(const SaveGame& save, uint16_t ship, int shown_convoy);

// The convoy's ships the menu lists (0x48FE80): along the convoy's chain ([+0xA], then each ship's [+0x6]) but those
// in the state 0x17; the one pointed at its guide ([+0x10], 0x490037; -1 if it isn't in the list).
struct ConvoyMenuList {
	std::vector<uint16_t> ships;
	int pointed = -1;
};
ConvoyMenuList convoy_menu_list(const SaveGame& save, uint16_t convoy);

// What the convoy's head shows (0x4904B0 with the update's sums: 0x4906E0 and 0x492150 for the owner of the ship
// pointed at): of the owner's ships in the list, the sailors ([+0x40]), the firepower ([+0x120]) and the cargo (the 24
// wares' units [+0x54 + 4·k] and 2000 a packet [+0x156]); the hull the average of every ship's 100 · [+0x18] / [+0x14];
// the capacity ([+0x10]) of the player's ships only (0x48FF99); the equipment ([+0x524C], 0x4921CD..0x4921F7: each of
// the owner's ships' [+0x11C] rounded up to a load, 200). For another merchant's convoy blurred as the ship list's
// figures: the hull (k 20, 0..100), the firepower (20, from 0) and the sailors (40, at least 1) by the pointed ship's
// number, the cargo (70, from 0) by the convoy's (its equipment isn't shown).
struct ConvoyMenuHead {
	int32_t hull = 0;
	int32_t sailors = 0;
	int32_t firepower = 0;
	int32_t cargo = 0;
	int32_t capacity = 0;
	int32_t equipment = 0;
};
ConvoyMenuHead convoy_menu_head(const SaveGame& save, const ConvoyMenuList& list);

// The name a pirate ship goes by (0x612EE0 on the battles, 0x6E55D0; another merchant's ship's head says it for a ship
// past the merchants or raiding, 0x491413): in a battle (0x6087F0: one of the slots [+0x404], its ships' list [+0x4] of
// [+0x14] entries, each its ship at [+0x4]) the battle's pirate (0x610B29: [+0x1E90] the first name, [+0x1E91] the
// family name; both 0xFF unknown); otherwise the captain ([+0x42] under the count) of the ship, or of its convoy's guide
// in a convoy, with his names (the captains' +2, +3); without one unknown. Known: «%s %s %s» with «Pirate» (0x6C77B8,
// 0x6C7708: *0x69C98C and the two names, 0x512A60 and 0x512AE0); unknown: «Unknown pirate» (*0x6A1780).
struct PirateName {
	bool known = false;
	uint8_t first = 0;
	uint8_t family = 0;
};
PirateName pirate_name(const SaveGame& save, uint16_t ship);

// 0x503AC0 on a convoy: the Hanse's fleet of its event 0x34 ([+0x2] the event, whose [+0x16] is the convoy). Its head
// says «Convoy Hanseatic League Fleet» (0x4904F2), its flag is yellow (0x490F80).
bool hanse_fleet_convoy(const SaveGame& save, uint16_t convoy);

}  // namespace p3::data
