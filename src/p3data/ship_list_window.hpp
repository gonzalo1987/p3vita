#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "p3data/savegame.hpp"

namespace p3::data {

// The ship list («Ship overview»): a parchment window over the town (vtable 0x66ECF8; the object *0x6CC950, made once:
// its constructor 0x4726B0, its init 0x472C60 with parchment.ini [ShipList0] and the chart [CHART2]; opened by 0x475E40,
// laid out by 0x4751D0, drawn by 0x473220, updated by 0x476050, its clicks 0x476790 and 0x477150). It keeps its sort
// and its columns between openings. The rules here; the viewer draws it (notes: re/emu/sesion40/README.md).

// Its lists (0x474B70 by the window's [+0x8DC], its opening's first byte): the side panel's icon opens it with 0
// (0x42470B: the player's ships), the town's lighthouse (.let 0x15, «leuchtturm», 0x5950C8) with 2 (the ships in that
// port, the other merchants' too: [+0x8D8] = 1); the sea map with 2 without the others' ships (a click on a town with
// more than one of the player's ships there, 0x44FAE4) and with 1 (a list it gives: the player's ships under a click,
// 0x450F9B, or in a dragged box, 0x44DC5F).
struct ShipList {
	std::vector<uint16_t> ships;  // [+0x8F4]: the merchant's first, then the others'.
	uint32_t own = 0;  // [+0x8EC]: the first table's rows.
	uint32_t others = 0;  // [+0x8F0]: the second table's (the others' ships).
};
// 0: the merchant's ships along his list ([+0xE], then each ship's [+0x4]) but those in the state 0x17.
ShipList merchant_ship_list(const SaveGame& save, uint32_t merchant);
// 2: the merchant's ships in the town ([+0x39]) in the states 0..3, then (with `others`, [+0x8D8]) the other merchants'
// there in those states, from the last merchant down (0x474E69..0x4751B1).
ShipList port_ship_list(const SaveGame& save, uint32_t merchant, uint32_t town, bool others = true);
// 1: the ships given, as given; the merchant's counted as the first table's, the rest as the second's (0x474BEB..; the
// sea map gives the player's ships only).
ShipList given_ship_list(const SaveGame& save, uint32_t merchant, const std::vector<uint16_t>& ships);
// The sea map's click on the player's ships (0x44FB40 with −2, 0x450712..0x450F3A): his ships along his list sailing
// (0xF) or in the Hanse's fleet (0x16), each tested on its own picture or its convoy's; the viewer tests each such ship
// with `hit`. One is chosen in the ship menu, more open the list (1, its first after the sort chosen, 0x475FED).
template <class Hit>
std::vector<uint16_t> sea_map_ships_at(const SaveGame& save, uint32_t merchant, Hit hit);

// The sort (the window's [+0xCCC], 0..4; its button after «Sort by:» steps it, 0x4760E1): its name (the table 0x6CC91C
// that 0x46C2E0 fills: «Convoy» *0x699908, «Name» *0x6998D8, «Type» *0x6998F0, «Weapons» *0x699934, «Condition»
// 0x6B2D80) and its kind (0x66E930: 2, 0, 3, 4, 1); the constructor starts it at the first of the kind 0, «Name»
// (0x472A94).
constexpr int kShipListSorts = 5;
constexpr int kShipListDefaultSort = 1;
int ship_list_sort_kind(int sort);
// 0x4779D0: each table sorted alone, by exchange sorts (each later entry swapped in when it goes before; the names
// compared byte by byte, strcmp): by name (the kind 0); by the shown hull, the worst first, then the runs of one exact
// hull by name (1, 0x477DA0); the ships in convoys first, by their guides' names, then the runs of one convoy by name
// (2, 0x478120); by type, then each type's run by name (3, 0x478340); by the shown firepower, the weakest first, then
// the runs of one firepower ([+0x120]) by name (4, 0x477F70).
void sort_ship_list(const SaveGame& save, ShipList& list, int sort);
// The kind 2 draws a line under a row whose next ship of its table is in another convoy (0x473B99, 0x474471).
bool ship_list_convoy_line(const SaveGame& save, const ShipList& list, uint32_t row, int sort);

// What the rows show of a ship: its own figures for the player's ([0x6DFC14]); others' off by their number n (0x51A3A0)
// as a part of the figure: the hull 0x495340 (100 · [+0x18] / [+0x14], off by (n % 20 − 10) %, 0..100), the cargo
// 0x4954C0 ([+0x118] off by (n % 70 − 35) % of its 200s, in 200s, from 0), the sailors 0x4953C0 ([+0x40] off by
// (n % 40 − 20) %, 1..the most it takes) and the firepower 0x495450 ([+0x120] off by (n % 20 − 10) %, from 0).
int32_t shown_hull(const SaveGame& save, uint16_t ship);
int32_t shown_cargo(const SaveGame& save, uint16_t ship);
uint32_t shown_sailors(const SaveGame& save, uint16_t ship);
int32_t shown_firepower(const SaveGame& save, uint16_t ship);
// The row's ink (0x473C50..0x473CBB): red under 40 % of hull (0x495340), the touched row's blue (*0x66E104) over it, a
// selected ship's (*0x66E108) over that; the status's grey (0xFF404040) over all.
constexpr uint32_t kShipListInk = 0xFF000000;
constexpr uint32_t kShipListDamaged = 0xFFFF0000;
constexpr uint32_t kShipListPointed = 0xFF0020CC;
constexpr uint32_t kShipListSelected = 0xFF003C8C;
constexpr uint32_t kShipListGrey = 0xFF404040;
// The hull's figure in red (0x47406A) under 40 %, but in the states 0xC..0xE and 0x13.
bool ship_list_hull_red(const SaveGame& save, uint16_t ship);

// The status column (0x473CD2 by the state [+0x134]): its picture (TEX of textures.ini; 0 none) and its words after it.
enum class ShipListWords : uint8_t {
	None,
	Town,  // The ship's town ([+0x39]).
	Destination,  // Sailing to a town ([+0x38]).
	AtSea,  // *0x699914 «at sea».
	Sunk,  // *0x699940.
	Auction,  // *0x699A14.
	Offered,  // *0x699A08.
	Expedition,  // *0x6999A0.
	SeaBattle,  // *0x6999FC.
	HanseFleet,  // *0x6B0D88 «Hanseatic League Fleet».
};
struct ShipListStatus {
	uint32_t tex = 0;
	ShipListWords words = ShipListWords::None;
	bool grey = false;
};
// The states 0, 1, 4, 5 its town at anchor (TEX16042); 2, 3, 0xF, 0x10, 0x15 sailing (TEX16043) to its destination, or
// «at sea» without one, at anchor unless it has a route ([+0x24]) or its convoy's guide has one; 6 its town with the
// repairs' (16015), 7 the upgrade's (16017), 8..0xB laid up (16062), 0xC «Auction» (16025, grey), 0xD «Offered» (16024,
// grey), 0xE its town being built (16016, grey), 0x11 «Sunk» (16050), 0x13 «Expedition» (16043, grey), 0x14 «Sea
// battle» (16043), 0x16 «Hanseatic League Fleet» (16062); 0x12 and past 0x16 nothing.
ShipListStatus ship_list_status(const SaveGame& save, uint16_t ship);

// The cargo's value ([+0x114]) as coins (TEX16014, 0x493C20): none for 0, else 1..8 by 3250, 9250, 21250, 39250,
// 63250, 93259 and 129250 (signed: under the first, 1); drawn from the column's right end leftwards, each a third of a
// coin left of the one before.
int ship_list_coins(int32_t value);

// The last column (0x490F80): TEX16034 for a ship on a trade route ([+0x136] & 1, or its convoy's guide's), then the
// convoy's picture, unless its convoy's [+0x14] has 0x10: 16002 for its guide ([+0x10]), else 16004, tinted yellow for
// the Hanse's fleet (0x503AC0), blue for the player's, white for another merchant run by the computer ([+0x8] ≠ 0), red
// for another human's, or 0xFF282828 for a pirate (the state 0x12 or [+0x15C]); without a convoy, a pirate's 16003.
struct ShipListMarks {
	bool trade_route = false;
	uint32_t tex = 0;
	uint32_t colour = 0xFFFFFFFF;
};
ShipListMarks ship_list_marks(const SaveGame& save, uint16_t ship);

// The button left of each row of the first table (0x475C10): «Delete» (parchment.ini [Button12], TEX16044 frames 6..8)
// for a lost ship (0x11), else «Go to» ([Button13], frames 9..11), off in the state 0x13. Pressed, or the row touched
// twice in 500 ms (0x4771C5), 0x4768B0: nothing in the state 0x13; a lost ship removed (0x0A, the window stays);
// otherwise the ship is gone to (0x476A30: its town, or the sea map) and the window closes, the ship chosen first but
// in the states 0xC..0xE.
enum class ShipListAction : uint8_t { None, Delete, GoTo, GoToOnly };
ShipListAction ship_list_action(const SaveGame& save, uint16_t ship);
// A row touched once (0x477150): the ship chosen in the side panel's ship menu, but in the states 0xC..0xE and 0x13.
bool ship_list_can_choose(const SaveGame& save, uint16_t ship);

// The window's layout (0x4751D0) in a view of w × h at (x, y) (the game's town view, *0x6E51AC's slots 0x60 and 0x40),
// with tiles of 32 (*0x6CBB00): its chart at (40, 60) from its top left (Chart2DPos), 465 wide (CHART2: 7 columns 108
// 54 106 18 40 62 56, spacing 3 3 3 6 3 3), its rows 18 high and 2 apart under a head 27 high; the width the tile, the
// buttons' room (Button13's 32), the chart and the scroll bar, in whole tiles; the height 60 + 40 (the close button's
// −40) + 20 a row + 27 (and with others' ships 20 + 20 a row + 27 + 27 more), plus a tile, in whole tiles, or the view's
// whole tiles when it doesn't fit (the rows then shared, 0x47538F..0x4754B5); centred across (x pushed by its remainder
// of 4), and down at most half a tile from the view's top. The second chart 20 under the first one's last row. The parchment without rolls.
constexpr int kShipListTile = 32;
constexpr int kShipListChartX = 40;
constexpr int kShipListChartY = 60;
constexpr int kShipListColumns = 7;
constexpr int kShipListColumnWidth[kShipListColumns] = {108, 54, 106, 18, 40, 62, 56};
constexpr int kShipListColumnSpacing[kShipListColumns] = {3, 3, 3, 6, 3, 3, 0};
constexpr int kShipListChartWidth = 465;
constexpr int kShipListRowHeight = 18;
constexpr int kShipListRowSpacing = 2;
constexpr int kShipListHeadHeight = 27;
constexpr int kShipListTitleY = 20;  // TitleYPos.
constexpr int kShipListButtonsOffset = 4;  // ButtonsXOffset.
constexpr int kShipListRowButton = 32;  // [Button12] and [Button13]: 32 × 18.
// The columns' left ends in the chart (0, 111, 168, 277, 301, 344, 409).
int ship_list_column_x(int column);
struct ShipListLayout {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
	int rows1 = 0;  // The first chart's rows shown ([+0x838]'s 0x40A900).
	int rows2 = 0;  // The second's ([+0x874]).
	int chart2_y = 0;  // From the window's top.
	// From the window's bottom right (CloseBtn2DPos, ChangeBtn2DPos): the close button (32 × 18) and the columns' button
	// (72 × 18); «Sort by:» at the chart's x on the close button's line, its button (buttons.ini [Button4], 128 × 18) 10
	// after the words.
	int close_x() const { return w - 60; }
	int change_x() const { return w - 140; }
	int buttons_y() const { return h - 40; }
};
ShipListLayout ship_list_layout(int view_x, int view_y, int view_w, int view_h, uint32_t own, uint32_t others,
	int scroll_bar_width);

template <class Hit>
std::vector<uint16_t> sea_map_ships_at(const SaveGame& save, uint32_t merchant, Hit hit) {
	std::vector<uint16_t> out;
	if (merchant >= save.merchants.size()) {
		return out;
	}
	const uint32_t ships = std::min<uint32_t>(save.globals.u16(0x6DD894), uint32_t(save.ships.size()));
	size_t walked = 0;
	for (uint32_t s = save.merchants[merchant].first_ship(); s < ships && walked < save.ships.size();
		 s = save.ships[s].memory.u16(0x4), ++walked) {
		const uint16_t state = save.ships[s].memory.u16(0x134);
		if ((state == 0x0F || state == 0x16) && hit(uint16_t(s))) {
			out.push_back(uint16_t(s));
		}
	}
	return out;
}

}  // namespace p3::data
