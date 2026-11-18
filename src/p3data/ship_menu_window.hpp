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

// The menu's «form convoy» button with several ships chosen (the state 0, 0x48A60E: 0x48FC00): on when every one is
// the player's ([+0x0] = *0x6DFC14), in no convoy ([+0x8] past the convoys' count), docked (the state [+0x134] 0 or 1)
// in the first one's town ([+0x39]).
bool ships_can_form_convoy(const SaveGame& save, const std::vector<uint16_t>& ships);

// The game's convoy window («form convoy»: *0x6CBA98, vtable 0x66ACE0, made by 0x424EF4 with its constructor 0x40BD20;
// its init 0x40CB90 with parchment.ini [CreateKonvoi0], CloseBtn2DPos −60 −40, ListOffset2DPos 14 60, ListXSize 336;
// opened by the menu's button, its slot 0x120 0x40C470, with the ships chosen when 0x40C170 finds something to offer
// (player_actions.hpp, convoy_choices), else the message «Convoy cannot be formed»). Its layout 0x40C5F0 (traced in
// the emulator, re/emu/sesion42/README.md): wide enough for the list's offset, its width and the scroll bar, high
// enough for the list's rows under 60 and the buttons' 40, both in whole tiles of 32 (the view's whole tiles at most);
// centred in the view (0x6E51AC), x pushed on by its remainder of 4. The list's rows (the font 1's height apart) from
// (14, 60), as many shown as fit above the buttons; its scroll bar (0x460EF0 on the list's room 336 + 2 and the bar
// wide: the bar at its right end) when some don't. The title «Chooses an action» (*0x699B68) in the font 3 centred at
// half a tile down, «OK» (*0x699B50, buttons.ini [Button3], 96 × 18) centred on the close button's line (h − 40),
// the close button at (w − 60, h − 40). The parchment without rolls.
constexpr int kConvoyWindowTile = 32;
constexpr int kConvoyListX = 14;
constexpr int kConvoyListY = 60;
constexpr int kConvoyListWidth = 336;
constexpr int kConvoyOkWidth = 96;
struct ConvoyWindowLayout {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
	int rows = 0;  // Shown ([+0x14F0]).
	int title_y() const { return kConvoyWindowTile / 2; }
	int buttons_y() const { return h - 40; }
	int close_x() const { return w - 60; }
	int ok_x() const { return (w - kConvoyOkWidth) / 2; }
	// The scroll bar's left end (its room's right end less the bar), from the window's left.
	int bar_x() const { return kConvoyListX + kConvoyListWidth + 2; }
};
ConvoyWindowLayout convoy_window_layout(int view_x, int view_y, int view_w, int view_h, int choices, int line,
	int scroll_bar_width);

// A convoy the window offers to join, as its row says it (0x40CFDE..0x40D0E9): «Join convoy %s» with its guide's
// ([+0x10]) name or, the Hanse's fleet (0x503AC0), «Hanseatic League Fleet»; an open one ([+0x14] without 2) adds
// «; %.2u.%.2u.%.4u %s»: when it leaves (0x5311D0 on [+0x16] + the tick *0x6DE4B4: the day, the month 1..12 and the
// year) and its destination ([+0x1F]).
struct ConvoyJoinRow {
	bool hanse = false;
	uint16_t guide = 0xFFFF;
	bool open = false;
	uint32_t leaves = 0;  // The tick.
	uint8_t destination = 0xFF;
};
ConvoyJoinRow convoy_join_row(const SaveGame& save, uint16_t convoy);

// The game's selection list (0x46F760, vtable 0x66EBC8; its init 0x470700, its open 0x46F9B0, its drawing 0x471420):
// a parchment with rolls over the view, its title in the font 3 («choose goods», «choose town», «choose trader»,
// «choose offer»: *0x6C10C8 + 4 · mode), its rows in the font 0 (sidemenu.ini has no [Parchment1]: Font 0) a row its
// height and 1 apart (16) from (32, 62): 62 = the font 3's height (20) and a half, and a tile; the row under the
// pointer in 0x0020CC. Its width: the widest of the title, its button [+0x58C] (128) and 5, and the rows, rounded up
// a tile, and a tile each side; its height: (rows · 16 + 62) / 32 + 3 tiles, or the view's whole tiles when it doesn't
// fit (then (h − 64 − 62) / 16 rows shown, its scroll bar for the rest); placed at the view's right less three
// quarters of its width (0x46FED0..0x46FF02: its last quarter over the side panel), centred in the view's height. The
// window ([+0x2C], slot 0x54) is 12 wider than its tiles, the rolls' ends (the tiles 6 in); from its width the close
// button at (W − 32 − 32, h − 47) and the scroll bar ending at W − 32 + half the bar (0x46FF8B..0x470000).
constexpr int kSelectionListTile = 32;
constexpr int kSelectionListRowX = 32;
constexpr int kSelectionListRowY = 62;
constexpr int kSelectionListRowHeight = 16;
constexpr int kSelectionListButton = 128;
struct SelectionListLayout {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
	int rows = 0;  // Shown ([+0xDA]).
	int rows_width = 0;  // [+0xE4]: the rows' room (and the title's), a tile in.
	int window_w() const { return w + 12; }  // With the rolls' ends ([+0x2C]).
	int title_x() const { return w / 2 + 1; }  // Centred there ([+0xDC] + 1).
	int title_y() const { return kSelectionListTile + 1; }  // [+0xDE] + 1.
	int close_x() const { return window_w() - kSelectionListTile - 32; }
	int close_y() const { return h - 47; }
	int bar_x(int bar_width) const { return window_w() - kSelectionListTile - bar_width / 2; }
};
SelectionListLayout selection_list_layout(int view_x, int view_y, int view_w, int view_h, int entries, int widest_row,
	int title_width);

}  // namespace p3::data
