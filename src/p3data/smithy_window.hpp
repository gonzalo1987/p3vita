#pragma once

#include <cstdint>
#include <vector>

#include "p3data/player_actions.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// What the town's weaponsmith shows and what it queues (the rules; the viewer draws them its way): the building .let
// 0x08 («8bitkomplettstadtschmiede», the building type 0x03, «Weaponsmith»), its window *0x6E55A8 (made by 0x5EB730,
// BuildingParchment.ini; opened by 0x5EBAE0; its two pages set by 0x5EBD70 from the side menu *0x6E550C, 0x5AA7F0; its
// update 0x5ECFA0; the pages drawn by 0x5EC590 and 0x5ECAB0). The operations are the engine's (trade.cpp: 0x50, 0x56,
// 0x57). (The armoury, .let 0x0B, is the militia's: 0x40, 0x41.)
//
// Page 0, the ship's artillery: seven rows (the six pieces, then the cutlasses of [+0x2BC]), each with the town's
// count, a button to buy one at its price and one to sell one at half of it, and the office's count. Page 1, the hand
// weapons (the wares 0x14..0x17: sword, bow, crossbow, carbine): the town's count, a button to buy one into the office
// at its price, a «Priority» check (for the town's mayor only: the weapon the weaponsmith makes first) and the
// office's count. Without an office of the player's in the town the buttons are off and the update does nothing. Under
// both, the first reason the weaponsmith stopped (the town's [+0x775]).

constexpr int kArtilleryRows = 7;   // 0..5 the pieces (0x672CB4 their weights), 6 the cutlasses ([+0x2BC], one each)
constexpr int kHandWeaponRows = 4;  // the wares 0x14..0x17

// 0x5285B0 (thiscall on the town, kind): the price of a piece of artillery: below 6, ((the town's [+0x2C0] + the
// world's day of the year [0x6DE4A4]) % 5) · 3 + 163 · {2, 3, 5, 8, 11, 14}[kind]; else the byte 0x673A78 (100). The
// window sells one at half of it (shown halved signed, queued shifted right).
int32_t artillery_price(const SaveGame& save, uint32_t town, uint32_t kind);
// 0x52E030 (thiscall on a town or an office, kind): how many of a kind it holds: below 6, its [+0x124 + 4·kind] / the
// weight (signed); else [+0x2BC].
int32_t town_artillery(const SaveGame& save, uint32_t town, uint32_t kind);
int32_t office_artillery(const SaveGame& save, uint32_t office, uint32_t kind);
// The price of one hand weapon (ten units; inline in 0x5EBD70 and 0x5ECFA0): ((the tick [0x6DE4B4] >> 8) + town) · 3
// % 5 (unsigned) minus the float 0x673A68[kind] (10, 8, 12, 14) times the double 0x67A3F8 (−9.25), truncated: 92, 74,
// 111, 129 and that.
int32_t hand_weapon_price(const SaveGame& save, uint32_t town, uint32_t kind);

struct SmithyRow {
	int32_t town = 0;    // What the town has: page 0 its count of the kind (0x52E030); page 1 its units / 10 (signed).
	uint16_t office = 0;  // The same of the player's office there (kept as a u16 by the window), 0 without one.
	int32_t price = 0;   // Buying one.
	int32_t sell = 0;    // Page 0: selling one, as shown (price / 2, signed).
};
struct SmithyPage {
	bool office = false;  // The player has an office in the town: the buttons are on.
	std::vector<SmithyRow> rows;
	// Page 1: the «Priority» checks are on for the town's mayor ([+0x6F1] the player); the row checked, from the town's
	// [+0x6D0] − 0x14 as a byte (none above 3).
	bool may_choose = false;
	int priority = -1;
};
SmithyPage smithy_artillery(const SaveGame& save, uint32_t player, int town);
SmithyPage smithy_hand_weapons(const SaveGame& save, uint32_t player, int town);
// Why the weaponsmith stopped (the window's [+0x1396] = the town's [+0x775]): the first of its 5 low bits that is set
// (0 no wood, 1 no leather, 2 no hemp, 3 no iron goods, 4 the warehouse too full: the texts 0x6B2284 + 4·k), or −1.
int smithy_stopped(const SaveGame& save, int town);

// The buttons (0x5ECFA0): page 0, {0x56, player, kind, the price (0x5285B0; selling, shifted right by one), town (|
// 0x100 selling)}; page 1, {0x50, the price, 1, u8 town, u8 player, u8 kind, u8 1 (buy), u16 0xFFFF (the office), u8
// 0}; a «Priority» check put on another row {0x57, town, 0x14 + row}, the checked one put off {0x57, town, 0x113} (the
// window's row 0xFF + 0x14: none).
OperationBytes smithy_artillery_operation(const SaveGame& save, uint32_t player, uint32_t town, uint32_t kind, bool sell);
OperationBytes smithy_hand_weapon_operation(const SaveGame& save, uint32_t player, uint32_t town, uint32_t kind);
OperationBytes smithy_priority_operation(uint32_t town, int row);  // row 0..3, or −1 to put the check off

}  // namespace p3::data
