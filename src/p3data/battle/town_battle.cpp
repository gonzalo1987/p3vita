// The town battles (the battles' piece 6, docs/cloud/battles.md §1 and §5):
// pirates or a raider against a town or a pirate nest. A sea battle's object
// grown to 0x2F24 bytes (vtable 0x67AAA0): the town on side D with up to 4
// towers, its own 64 × 64 map of the water [+0x1EE0], and its scene's ships
// of 0x1168 bytes (vtable 0x67AAD0). Here its start: 0x611D80 (a new one),
// 0x6130B0 (the running one of a town and an attacker) and the setup
// 0x5FBFE0 with its map 0x5FFC10, the formation 0x600060 (method +0x2C) and
// method +0x18 (0x600660). It reads the town view (SaveGame::TownView): the
// attacked town's map file and the view's cell ↔ point geometry.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "p3data/battle/battle_math.hpp"
#include "p3data/game_fixes.hpp"
#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kTownBattleVtable = 0x67AAA0;
constexpr size_t kTownBattleSize = 0x2F24;
constexpr uint32_t kTownShipVtable = 0x67AAD0;
constexpr size_t kTownShipSize = 0x1168;
constexpr uint32_t kTownTable = 0x6DDB90;  // StadtDaten.ini, 0x34 bytes per town (0x51E440)
constexpr uint32_t kTownIds = 0x6DE4B8;  // u8 per town: its StadtDaten number
constexpr uint32_t kTownMaps = 0x6DE4E0;  // u8 per town: its map (iso/towns/<n>.town)
constexpr uint32_t kTick = 0x6DE4B4;  // the world's tick ([+0x14] of 0x6DE4A0)

// __ftol (0x639C28) of a double: truncated; NaN and out of range give the
// x87's integer indefinite, whose low 32 bits are 0.
int32_t game_ftol(double v) {
	if (!(v >= -9.2e18 && v <= 9.2e18)) {
		return 0;
	}
	return int32_t(int64_t(v));
}

// ftol(sqrt(n)) of an int, as the game: sqrt (0x63AAE4) of a negative one is NaN.
int32_t root(int32_t n) {
	return game_ftol(std::sqrt(double(n)));
}

uint32_t mulhi(uint32_t a, uint32_t b) {
	return uint32_t((uint64_t(a) * b) >> 32);
}

}  // namespace

// 0x611D80 (on the battles): a new town battle in the first free slot: a
// sea battle's constructor (0x603A70) on 0x2F24 bytes, then the vtable
// 0x67AAA0 and [+0x2F1C], [+0x2F20] 0; the count [+0x404] grows to cover it.
// The slot, or 0xFF if none is free.
uint8_t Sailing::fn_611d80() {
	BattleGraph& b = save_.battles;
	for (size_t k = 0; k < BattleGraph::kSlots; ++k) {
		if (b.get<uint32_t>(0, 4 * k) == 0) {
			const size_t block = b.alloc(kTownBattleSize);
			fn_603a70(block, uint8_t(k));
			b.set<uint32_t>(block, 0, kTownBattleVtable);
			b.set<uint32_t>(block, 0x2F1C, 0);
			b.set<uint16_t>(block, 0x2F20, 0);
			b.set<uint32_t>(0, 4 * k, BattleGraph::pointer(block));
			if (k + 1 > b.count()) {
				b.set<uint8_t>(0, 0x404, uint8_t(k + 1));
			}
			return uint8_t(k);
		}
	}
	return 0xFF;
}

// 0x6130B0 (on the battles; town, attacker): the slot of a battle that
// answers its method +0x28 (0x611730 of a sea battle 0, 0x602980 of a town
// battle 1: only the town battles) with [+0x1E98] the town and [+0x35A] the
// attacker, or 0xFF.
uint8_t Sailing::fn_6130b0(uint8_t town, uint8_t owner) const {
	const BattleGraph& g = save_.battles;
	for (uint32_t k = 0; k < g.count(); ++k) {
		const int b = g.battle(k);
		if (b < 0 || g.get<uint32_t>(size_t(b), 0) != kTownBattleVtable) {
			continue;
		}
		if (g.get<uint8_t>(size_t(b), 0x1E98) == town && g.get<uint16_t>(size_t(b), 0x35A) == owner) {
			return uint8_t(k);
		}
	}
	return 0xFF;
}

// 0x515D30 (on pirate k; an entry of its pool): a tower's strength, (200 −
// its damage [+0x6]) · 0x2FDA0 (kind 0x30) or 0x77A10 (0x31) / 200; 0 for
// another kind, a damage of 200 or more, or no such entry.
uint32_t Sailing::fn_515d30(size_t k, uint32_t entry) const {
	const PirateNest& nest = save_.pirate_nests[k];
	const uint32_t count = save_.pirates[k] ? save_.pirates[k]->u16(0x8) : 0;
	if (entry >= count || entry >= nest.pool.size()) {
		return 0;
	}
	const GameMemory<8>& e = nest.pool[entry];
	const uint8_t damage = e.u8(0x6);
	if (damage >= 0xC8) {
		return 0;
	}
	const uint32_t base = e.u8(0x3) == 0x30 ? 0x2FDA0 : e.u8(0x3) == 0x31 ? 0x77A10 : 0;
	return mulhi((0xC8u - damage) * base, 0x51EB851Fu) >> 6;
}

// 0x515AE0 (on pirate k; &a, &b): the names of the first captain of its
// ships (its list [+0xC], next [+0x4]) whose [+0x8] is above 0x20: true;
// else 0x2D and the families' count's low byte (0x6DDB74), false.
bool Sailing::fn_515ae0(size_t k, uint8_t& a, uint8_t& b) const {
	const uint16_t ships = uint16_t(save_.ships.size());
	for (uint32_t s = save_.pirates[k] ? save_.pirates[k]->u16(0xC) : 0xFFFF; s < ships;
		 s = save_.ships[s].memory.u16(0x4)) {
		const uint16_t c = save_.ships[s].memory.u16(0x42);
		if (c < save_.captain_count() && c < save_.captains.size() && save_.captains[c].u8(0x8) > 0x20) {
			a = save_.captains[c].u8(0x2);
			b = save_.captains[c].u8(0x3);
			return true;
		}
	}
	a = 0x2D;
	b = save_.globals.u8(0x6DDB74);
	return false;
}

// 0x50CC90 (on the ships; a town): the town's guard ship [+0x994] (none:
// 0xFFFF) leaves its port for the town's battle: [+0x994] 0xFFFF, out of the
// list in port, without route ([+0x1] 0), its targets, cargo and wares
// cleared, the towns [+0x37], [+0x38] the town and [+0x39] none, the state
// 0x14 ([+0x13B] the town), not raiding; its hold (0x5182B0); a convoy of
// its own (0x5062F0) with [+0x14] |= 0x10. The convoy.
uint16_t Sailing::fn_50cc90(uint8_t town) {
	const uint16_t ships = uint16_t(save_.ships.size());
	if (town >= save_.towns.size()) {
		return 0xFFFF;
	}
	TownMemory& t = save_.towns[town].memory;
	const uint16_t g = t.u16(0x994);
	if (g >= ships) {
		return 0xFFFF;
	}
	t.u16(0x994) = 0xFFFF;
	SavedShip& ship = save_.ships[g];
	ShipMemory& s = ship.memory;
	for (uint16_t* link = &save_.docked_head(); *link < ships; link = &save_.ships[*link].memory.u16(0x6)) {
		if (*link == g) {
			*link = s.u16(0x6);
			s.u16(0x6) = 0xFFFF;
			break;
		}
	}
	s.u8(0x1) = 0;
	if (ship.route) {
		s.u8(0x36) = 0;
		ship.set_route(nullptr);
	}
	s.u16(0x3A) = 0xFFFF;
	s.u16(0x3C) = 0xFFFF;
	s.u16(0x50) = 0xFFFF;
	s.u16(0x52) = 0xFFFF;
	for (size_t o = 0; o < 0x60; ++o) {
		s.u8(0x54 + o) = 0;
		s.u8(0xB4 + o) = 0;
	}
	s.u32(0x28) = 0;
	s.u32(0x2C) = 0;
	s.u32(0x30) = 0;
	s.u16(0x34) = 0;
	s.u8(0x37) = town;
	s.u8(0x38) = town;
	s.u8(0x39) = 0xFF;
	s.u32(0x48) = 0;
	s.u32(0x114) = 0;
	s.u32(0x118) = 0;
	s.u16(0x134) = 0x14;
	s.u16(0x138) = 0;
	s.u8(0x13B) = town;
	s.u8(0x15C) = 0;
	s.u8(0x15D) = 0xFF;
	recalculate_cargo(ship);
	uint16_t convoy = 0xFFFF;
	fn_5062f0(convoy, g);
	if (convoy >= save_.convoy_count()) {
		return 0xFFFF;
	}
	save_.convoys[convoy].u8(0x14) = uint8_t(save_.convoys[convoy].u8(0x14) | 0x10);
	return convoy;
}

// The map of a town battle's town: its town_class1 ([0x7A4] of a town, the
// map [+0x0] of a pirate's nest): width, height and the cells (x · height + y).
Sailing::TownBattleGrid Sailing::town_battle_grid(size_t b) const {
	const uint8_t town = save_.battles.get<uint8_t>(b, 0x1E98);
	TownBattleGrid grid;
	if (town < save_.towns.size()) {
		const SavedTown& t = save_.towns[town];
		grid.width = t.memory.u32(0x7A8);
		grid.height = t.memory.u32(0x7AC);
		grid.cells = &t.grid;
	} else {
		const PirateNest& nest = save_.pirate_nests[0xFE - town];
		if (nest.map) {
			grid.width = nest.map->u32(0x4);
			grid.height = nest.map->u32(0x8);
		}
		grid.cells = &nest.cells;
	}
	return grid;
}

// 0x5FFE20 (on a town battle; x, y): true if the cell is out of the town's
// map (for a town less the map file's offset [+0x1ED8], [+0x1EDC]; the size
// [+0x2EE0] × [+0x2EE4]) or of a kind 6, 12, 14 or 30 (tables 0x5FFEF4,
// 0x5FFF18: the same for a town and a nest).
bool Sailing::fn_5ffe20(size_t b, int32_t x, int32_t y) const {
	const BattleGraph& g = save_.battles;
	const uint8_t town = g.get<uint8_t>(b, 0x1E98);
	uint32_t cx = uint32_t(x), cy = uint32_t(y);
	if (town < save_.towns.size()) {
		cx = uint32_t(x - g.get<int32_t>(b, 0x1ED8));
		cy = uint32_t(y - g.get<int32_t>(b, 0x1EDC));
	}
	if (cx >= g.get<uint32_t>(b, 0x2EE0) || cy >= g.get<uint32_t>(b, 0x2EE4)) {
		return true;
	}
	const TownBattleGrid grid = town_battle_grid(b);
	const size_t i = size_t(grid.height) * cx + cy;
	const uint8_t cell = grid.cells && i < grid.cells->size() ? (*grid.cells)[i] : 0;
	return cell == 6 || cell == 12 || cell == 14 || cell == 30;
}

// 0x5FFC10 (on a town battle): its map of the water [+0x1EE0] (64 × 64, x ·
// 64 + y): all 1, then 0 under every cell of the town's map that 0x5FFE20
// doesn't take (each cell's point by the town view, 0x58CFB0: x · 64 / 4352
// and the point's y · 17 / 11 · 64 / 2486, at most 63), keeping the least and
// most x and y it cleared; [+0x2EE0], [+0x2EE4] the map's size; the item 7
// (0, 0, 0x1100, 0xF02) in [+0x1ED4]; for a nest (no map file) [+0x1ED8] and
// [+0x1EDC] from the cleared extent: least + most x − 63 and 2 (least + most
// y) − 126.
void Sailing::fn_5ffc10(size_t b) {
	BattleGraph& g = save_.battles;
	for (size_t i = 0; i < 0x40 * 0x40; ++i) {
		g.set<uint8_t>(b, 0x1EE0 + i, 1);
	}
	const TownBattleGrid grid = town_battle_grid(b);
	g.set<uint32_t>(b, 0x2EE0, grid.width);
	g.set<uint32_t>(b, 0x2EE4, grid.height);
	int32_t min_x = 0x3F, max_x = 0, min_y = 0x3F, max_y = 0;
	for (uint32_t y = 0; y < grid.height; ++y) {
		for (uint32_t x = 0; x < grid.width; ++x) {
			if (fn_5ffe20(b, int32_t(x), int32_t(y))) {
				continue;
			}
			const auto [px, py] = view_cell_point(int32_t(x), int32_t(y));
			const uint32_t ty = mulhi(uint32_t(py) * 17, 0xBA2E8BA3u) >> 3;
			uint32_t bx = mulhi(uint32_t(px) << 6, 0xF0F0F0F1u) >> 12;
			const uint32_t n = ty << 6;
			const uint32_t t = mulhi(n, 0x10ECAD3Fu);
			uint32_t by = (((n - t) >> 1) + t) >> 11;
			if (uint16_t(bx) >= 0x40) {
				bx = 0x3F;
			}
			if (uint16_t(by) >= 0x40) {
				by = 0x3F;
			}
			bx &= 0xFFFF;
			by &= 0xFFFF;
			const size_t at = 0x1EE0 + (size_t(bx) << 6) + by;
			if (g.get<uint8_t>(b, at) == 0) {
				continue;
			}
			g.set<uint8_t>(b, at, 0);
			min_x = std::min(min_x, int32_t(bx));
			max_x = std::max(max_x, int32_t(bx));
			min_y = std::min(min_y, int32_t(by));
			max_y = std::max(max_y, int32_t(by));
		}
	}
	g.set<uint16_t>(b, 0x1ED4, fn_619450(b, 7, 0, 0, 0, 0x1100, 0xF02));
	if (g.get<uint8_t>(b, 0x1E98) >= save_.towns.size()) {
		g.set<int32_t>(b, 0x1ED8, max_x + min_x - 0x3F);
		g.set<int32_t>(b, 0x1EDC, 2 * (max_y + min_y) - 0x7E);
	}
}

// The town view's cell → point (0x58CFB0 on *0x6E51AC; SaveGame::TownView):
// an odd row (x + 1) · w, (y / 2) · (h + 1) + h; an even one x · w + down x,
// (y / 2) · (h + 1) − down y + h (w, h the tile, down the DownVector).
std::pair<int32_t, int32_t> Sailing::view_cell_point(int32_t x, int32_t y) const {
	const SaveGame::TownView& v = save_.town_view;
	const int32_t w = v.tile[0], h = v.tile[1];
	const int32_t half = int32_t(uint32_t(y) >> 1);
	if (y & 1) {
		return {(x + 1) * w, (h + 1) * half + h};
	}
	return {w * x + v.down[0], (h + 1) * half - v.down[1] + h};
}

// The town view's point → cell (0x58D030, with 0x58B740): the point clamped
// to the map (false then), its cell box (x / w, 2 · (y / (h + 1))) and the
// hit mask's byte at (y in the box + 1, x in the box + 1): above 0xE0 inside,
// else the neighbour its code says (0x00 two rows up, 0x40 right, 0x80 two
// down, 0xC0 left, 0x20 / 0x60 / 0xA0 / 0xE0 up-right, down-right, down-left,
// up-left); a neighbour off the map clamped (false). Without the mask (no
// data) every point is inside its box.
bool Sailing::view_point_cell(int32_t px, int32_t py, int32_t& cx, int32_t& cy) const {
	const SaveGame::TownView& v = save_.town_view;
	const int32_t w = v.tile[0], h = v.tile[1];
	const int32_t cols = int32_t(v.cells[0]), rows = int32_t(v.cells[1]);
	const uint32_t pw = uint32_t(cols * w), ph = uint32_t((rows >> 1) * (h + 1));
	bool ok = true;
	if (px < 0) {
		px = 0;
		ok = false;
	} else if (uint32_t(px) >= pw) {
		px = int32_t(pw - 1);
		ok = false;
	}
	if (py < 0) {
		py = 0;
		ok = false;
	} else if (uint32_t(py) >= ph) {
		py = int32_t(ph - 1);
		ok = false;
	}
	const int32_t r = py / (h + 1);
	cx = px / w;
	cy = 2 * r;
	const size_t at = size_t(py - (h + 1) * r + 1) * v.detect_pitch + size_t(px - w * cx + 1);
	const uint8_t m = at < v.detect.size() ? v.detect[at] : 0xFF;
	if (m > 0xE0) {
		return ok;
	}
	switch (m) {
	case 0x00:
		cy -= 2;
		break;
	case 0x40:
		cx += 1;
		break;
	case 0x80:
		cy += 2;
		break;
	case 0xC0:
		cx -= 1;
		break;
	case 0x20:
		cx += cy & 1;
		cy -= 1;
		break;
	case 0x60:
		cy += 1;
		cx += ~cy & 1;
		break;
	case 0xA0:
		cy += 1;
		cx -= cy & 1;
		break;
	case 0xE0:
		cy -= 1;
		cx -= cy & 1;
		break;
	default:
		break;
	}
	if (uint32_t(cx) < uint32_t(cols) && uint32_t(cy) < uint32_t(rows)) {
		return ok;
	}
	// Off the map: the cell clamped to it (0x58D030 after 0x58B740 false).
	cx = std::max(0, std::min(cx, cols - 1));
	cy = std::max(0, std::min(cy, rows - 1));
	return false;
}

// 0x600060 (method +0x2C of a town battle): the formation. The way to the sea
// (the map file's offset negated, x doubled) gives the middle (0x880, 0x781;
// on the larger axis moved by 11/2 of it from the near side and halved) and a
// unit vector (· 256; 0xB5 each way when 0). Each side's ships stand in rows
// of ftol(sqrt(n − 1)) + 1 (n its ships): side A 300/256 of the vector out
// from the middle, side D 300/256 in, each row 200/256 further, each ship
// 200/256 across the vector from the last, a row centred; each ship's place
// x, y off by (generator & 0x7F) − 0x40 times the vector over 256 (logical
// shifts), its heading its ship's [+0x1] − 5 times 16; a ship whose cell
// (0x58D030 of x, y · 11 / 17) is not water (0x5FFE20 false) is placed again
// (side D's checked at side A's places: a game bug, below).
void Sailing::fn_600060(size_t b) {
	BattleGraph& g = save_.battles;
	const int32_t oy = -g.get<int32_t>(b, 0x1EDC);
	const int32_t ox = -g.get<int32_t>(b, 0x1ED8) * 2;
	int32_t mx = 0x880, my = 0x781;
	if (std::abs(ox) > std::abs(oy)) {
		if (ox < 0) {
			mx = (-(ox * 11)) / 2 + 0x880;
		} else {
			mx = 0x1100 - (ox * 11) / 2 + 0x880;
		}
		mx /= 2;
	} else {
		if (oy < 0) {
			my = (-(oy * 11)) / 2 + 0x781;
		} else {
			my = 0xF02 - (oy * 11) / 2 + 0x781;
		}
		my /= 2;
	}
	const int32_t len = root(int32_t(uint32_t(oy) * uint32_t(oy) + uint32_t(ox) * uint32_t(ox)));
	int32_t ux = 0xB5, uy = 0xB5;
	if (len != 0) {
		ux = (ox << 8) / len;
		uy = (oy << 8) / len;
	}
	const int32_t a0 = int32_t(uint32_t(ux) * 300), b0 = int32_t(uint32_t(uy) * 300);
	auto place = [&](size_t first, uint32_t count, int32_t sign) {
		const int32_t q = root(int32_t(count) - 1) + 1;
		int32_t ra = a0, rb = b0;
		auto row = [&](int32_t& x, int32_t& y) {
			x = mx + sign * (ra >> 8);
			y = my + sign * (rb >> 8);
			if (uint16_t(q) > 1) {
				x -= int32_t(uint32_t(q - 1) * uint32_t(uy) * 200) >> 9;
				y += int32_t(uint32_t(q - 1) * uint32_t(ux) * 200) >> 9;
			}
		};
		int32_t x, y;
		row(x, y);
		const int32_t step_x = int32_t(uint32_t(uy) * 200) >> 8, step_y = int32_t(uint32_t(ux) * 200) >> 8;
		uint32_t n = 1;
		for (uint32_t i = 0; i < count; ++i, ++n) {
			const size_t e = battle_entry(b, uint16_t(first + i));
			const uint32_t r1 = (battle_rand(g, b) & 0x7F) - 0x40;
			g.set<uint16_t>(e, 0xA, uint16_t((uint32_t(int32_t(r1) * uy) >> 8) + uint32_t(x)));
			const uint32_t r2 = (battle_rand(g, b) & 0x7F) - 0x40;
			g.set<uint16_t>(e, 0xE, uint16_t(uint32_t(y) - (uint32_t(int32_t(r2) * ux) >> 8)));
			const uint16_t ship = uint16_t(g.get<uint32_t>(e, 0x4));
			g.set<uint8_t>(e, 0x11, uint8_t((save_.ships[ship].memory.u8(0x1) - 5) << 4));
			x += step_x;
			y -= step_y;
			if (n % uint32_t(q) == 0) {
				ra = int32_t(uint32_t(ra) + uint32_t(ux) * 200);
				rb = int32_t(uint32_t(rb) + uint32_t(uy) * 200);
				row(x, y);
			}
			// The game checks the cell of the scene's ship i, not first + i: side
			// D's ships are checked at side A's places (0x6005D2, a game bug;
			// for i at or above side A's count, side D's own from its start, which
			// don't move: on land the loop never ends). With fix_game_bugs(), its own.
			const size_t checked = battle_entry(b, uint16_t(fix_game_bugs() ? first + i : i));
			const int32_t ex = g.get<int16_t>(checked, 0xA), ey = g.get<int16_t>(checked, 0xE);
			int32_t cx = 0, cy = 0;
			view_point_cell(ex, (ey * 11) / 17, cx, cy);
			if (!fn_5ffe20(b, cx, cy)) {
				--i;
				--n;
			}
		}
	};
	place(0, g.get<uint16_t>(b, 0x658), 1);
	place(g.get<uint16_t>(b, 0x658), g.get<uint16_t>(b, 0x65A), -1);
}

// 0x600660 (method +0x18 of a town battle; arg): a battle over ([+0x5C]) ends
// (method +0x1C, 0x5FE7E0, not rewritten yet); else [+0x2F18] 0 and, with
// arg, the formation (method +0x2C, 0x600060). (The local player's human
// side then shows the town (the view and the sea map, 0x6107B9 and
// [0x6E59CC]): the interface, part 7.)
void Sailing::fn_600660(size_t b, bool arg) {
	BattleGraph& g = save_.battles;
	if (g.get<uint8_t>(b, 0x5C) != 0) {
		return;  // 0x5FE7E0: the battle's end (piece 6, part 4).
	}
	g.set<uint16_t>(b, 0x2F18, 0);
	if (arg) {
		fn_600060(b);
	}
}

// 0x5FBFE0 (on a town battle; the attacking ship, the town): the setup.
// The town (or nest, 0xFE − k): [+0x1E98], its StadtDaten approach point
// [+0xAD6], [+0xADA], for a town the map file's two numbers (0x58E760:
// SaveGame::TownView::map_ends) and [+0xAD2] 1, [+0x1E94] 0x34C8 (a nest:
// 2 and 0). Up to 4 towers (kind 0x30 or 0x31, built; a nest's from its pool
// with a strength, 0x515D30): their cells, places (2x + (y & 1) + 1) · 17
// and 17y − 34 (a town's cells moved by the map file's numbers), strength
// (0x2FDA0, 0x77A10) and an item 8 of ±0x22. The defender: the town's guard
// ship (0x50CC90) or the nest's convoy [+0x10] (and its captain's names,
// 0x515AE0). The battle's map (0x5FFC10), the owners and the generator's
// seed (ship + owners + town + the tick), which sides are human (0x607990),
// the ships of each side in the scene as a sea battle's (0x605420) but of
// 0x1168 bytes (vtable 0x67AAD0), the town's [+0x2C8] |= 0x800, the wind
// (with the storms near), the steps, and when both sides are the AI's
// method +0x18 (0x600660). Without ships (or towers and defenders) the
// battle is over at once (method +0x1C: not rewritten yet). Left out: a
// human side's texts and battle window (part 7).
void Sailing::fn_5fbfe0(size_t b, uint16_t ship, uint8_t town) {
	BattleGraph& g = save_.battles;
	GlobalMemory& gl = save_.globals;
	const uint16_t ships = uint16_t(save_.ships.size());
	const uint16_t convoys = save_.convoy_count();
	auto u8 = [&](size_t o) { return g.get<uint8_t>(b, o); };
	auto u16 = [&](size_t o) { return g.get<uint16_t>(b, o); };
	auto set8 = [&](size_t o, uint8_t v) { g.set<uint8_t>(b, o, v); };
	auto set16 = [&](size_t o, uint16_t v) { g.set<uint16_t>(b, o, v); };
	auto set32 = [&](size_t o, uint32_t v) { g.set<uint32_t>(b, o, v); };
	set8(0x1E98, town);
	set8(0x2F1A, 0);
	set8(0x12E8, 1);
	auto approach = [&](uint8_t id) {
		set16(0xAD6, gl.u16(kTownTable + 0x20 + 0x34 * uint32_t(id)));
		set16(0xADA, gl.u16(kTownTable + 0x24 + 0x34 * uint32_t(id)));
	};
	// A tower in slot k: its cell, place, strength and item 8.
	auto tower = [&](uint32_t k, uint8_t x, uint8_t y, uint32_t strength, int32_t dx, int32_t dy) {
		set8(0x1E99 + k, 0xA);
		set8(0x1EAE + k, x);
		set8(0x1EB2 + k, y);
		set32(0x2EF8 + 4 * k, 0);
		const int32_t cx = int32_t(x) + dx, cy = int32_t(y) + dy;
		const uint16_t px = uint16_t(((cy & 1) + 2 * cx + 1) * 17);
		const uint16_t py = uint16_t(cy * 17 - 0x22);
		set16(0x1E9E + 2 * k, px);
		set16(0x1EA6 + 2 * k, py);
		set32(0x1EC4 + 4 * k, strength);
		set8(0x1EBE + k, 0);
		set16(0x1EB6 + 2 * k, fn_619450(b, 8, uint8_t(k), uint16_t(px - 0x22), uint16_t(py - 0x22),
			uint16_t(px + 0x22), uint16_t(py + 0x22)));
	};
	auto no_tower = [&](uint32_t k) {
		set8(0x1E99 + k, 0);
		set16(0x1E9E + 2 * k, 0xFFFF);
		set16(0x1EA6 + 2 * k, 0xFFFF);
		set32(0x1EC4 + 4 * k, 0);
	};
	uint16_t defender = 0;  // ebx: the defending ship (the guard ship, or the nest convoy's first)
	if (town < save_.towns.size()) {
		approach(gl.u8(kTownIds + town));
		// 0x59B4B0: the town's map number (0x6DE4E0[town]); 0x58E760 reads the
		// file of that number | 0x80.
		const auto& ends = save_.town_view.map_ends[gl.u8(kTownMaps + town) & 0x7F];
		set32(0x1ED8, uint32_t(ends[0]));
		set32(0x1EDC, uint32_t(ends[1]));
		set8(0xAD2, 1);
		set16(0x1E94, 0x34C8);
		const SavedTown& t = save_.towns[town];
		const uint16_t count = t.memory.u16(0x776);
		auto building = [&](uint32_t i) -> const BuildingRecord* {
			return i < count && i < t.records.size() ? &t.records[i] : nullptr;
		};
		const BuildingRecord* r = building(t.memory.u16(0x768));
		for (uint32_t k = 0; k < 4; ++k) {
			while (r && !((r->type == 0x30 || r->type == 0x31) && r->pending == 0)) {
				r = building(r->next);
			}
			if (!r) {
				no_tower(k);
			} else {
				tower(k, r->x, r->y, r->type == 0x30 ? 0x2FDA0 : 0x77A10, g.get<int32_t>(b, 0x1ED8),
					g.get<int32_t>(b, 0x1EDC));
				r = building(r->next);
			}
			set32(0x2F08 + 4 * k, 0);
		}
		defender = t.memory.u16(0x994);
		if (defender < ships) {
			fn_50cc90(town);
			set16(0x660, save_.ships[defender].memory.u16(0x8));
		} else {
			set16(0x660, 0xFFFF);
		}
	} else {
		const size_t k = size_t(0xFE - town);
		const GameMemory<0x18>& pirate = *save_.pirates[k];
		approach(pirate.u8(0xE));
		set8(0xAD2, 2);
		set16(0x1E94, 0);
		const PirateNest& nest = save_.pirate_nests[k];
		const uint32_t count = pirate.u16(0x8);
		uint32_t idx = 0;
		bool have = count > 0;
		for (uint32_t slot = 0; slot < 4; ++slot) {
			while (have) {
				const GameMemory<8>& e = nest.pool[idx];
				if ((e.u8(0x3) == 0x30 || e.u8(0x3) == 0x31) && fn_515d30(k, idx) != 0) {
					break;
				}
				const uint16_t next = e.u16(0x4);
				have = next < count;
				idx = next;
			}
			if (!have) {
				no_tower(slot);
			} else {
				const GameMemory<8>& e = nest.pool[idx];
				tower(slot, e.u8(0x0), e.u8(0x1), fn_515d30(k, idx), 0, 0);
				const uint16_t next = e.u16(0x4);
				have = next < count;
				idx = next;
			}
			set32(0x2F08 + 4 * slot, 0);
		}
		set16(0x660, pirate.u16(0x10));
		defender = u16(0x660) < convoys ? save_.convoys[u16(0x660)].u16(0xA) : 0xFFFF;
		uint8_t first = 0, family = 0;
		fn_515ae0(k, first, family);
		set8(0x1E90, first);
		set8(0x1E91, family);
	}
	fn_5ffc10(b);
	set8(0x558, 2);
	set16(0x35A, save_.ships[ship].memory.u8(0x0));
	set16(0x35C, defender < ships ? save_.ships[defender].memory.u8(0x0) : 0xFF);
	set32(0xADC, uint32_t(ship) + u16(0x35A) + u16(0x35C) + town + gl.u32(kTick));
	set8(0x559, 0);
	set8(0x55A, 1);
	set8(0xAB0, 1);
	set8(0xAB1, 1);
	const uint16_t merchants = uint16_t(save_.merchant_count);
	auto human = [&](uint32_t side) {  // 0x607990
		const uint16_t owner = u16(0x35A + 2 * side);
		return owner < merchants && save_.merchants[owner].memory.u16(0x8) == 0;
	};
	set8(0x5D, human(0) ? 0 : 1);
	set8(0x5E, human(1) ? 0 : 1);
	if (gl.u32(0x6DFC18) != 0) {
		if (!u8(0x5D) && !u8(0x5E) && u16(0x35A) != u16(0x35C)) {
			set8(0x5E, 1);
			set8(0x5D, 1);
		} else if (!u8(0x5D) || !u8(0x5E)) {
			set8(0xAC8, 0);
		}
	}
	if (!u8(0x5D) || !u8(0x5E)) {
		// The ticker lines to the human sides: «The X is attacking.» (0xE)
		// and «The X is being attacked.» (0xB).
		if (!u8(0x5D)) {
			send_ticker(save_, u16(0x35A), 0xE, ship);
		}
		if (!u8(0x5E) && u16(0x35A) != u16(0x35C)) {
			send_ticker(save_, u16(0x35C), 0xB, defender);
		}
		set8(0xAC8, 0);
		fn_612c90(false);
	}
	// The sides' ships. Side A: the attacker's convoy (all its ships: the
	// game counts them all, then takes only the attacker's, walking past
	// the convoy's end if another merchant's are in it) or the ship alone.
	const ShipMemory& sa = save_.ships[ship].memory;
	set16(0x65E, sa.u16(0x8));
	uint32_t na = 0;
	if (u16(0x65E) >= convoys) {
		set32(0xA84, ship);
		na = 1;
	} else {
		uint32_t i = save_.convoys[u16(0x65E)].u16(0xA);
		set32(0xA84, i);
		for (; i < ships; i = save_.ships[i].memory.u16(0x6)) {
			++na;
		}
	}
	set16(0x658, uint16_t(na));
	// The battle over at once: [+0x5C], automatic (0x612D20 sees [+0xAC8]: the
	// game sets it after the call without an attacker, before without a
	// defender), [+0xAC4] 3, then method +0x1C (0x5FE7E0, with 1 without an
	// attacker, 0 without a defender: the battle's end, piece 6 part 4).
	auto over = [&](bool no_attacker) {
		set8(0x5C, 1);
		if (!u8(0xAC8)) {
			if (no_attacker) {
				fn_612d20(false);
				set8(0xAC8, 1);
			} else {
				set8(0xAC8, 1);
				fn_612d20(false);
			}
		}
		set8(0xAC4, 3);
	};
	if (na == 0) {
		over(true);
		return;
	}
	uint32_t nd = 0;
	if (u16(0x660) >= convoys) {
		set32(0xA88, defender);
		nd = defender < ships ? 1 : 0;
	} else {
		uint32_t i = save_.convoys[u16(0x660)].u16(0xA);
		set32(0xA88, i);
		for (; i < ships; i = save_.ships[i].memory.u16(0x6)) {
			++nd;
		}
	}
	set16(0x65A, uint16_t(nd));
	if (nd == 0 && u16(0x1E9E) == 0xFFFF) {
		over(false);
		return;
	}
	set16(0x14, uint16_t(na + nd));
	const size_t n = u16(0x14);
	g.set_ptr(b, 0x4, int(g.alloc(4 * n)));
	g.set_ptr(b, 0xA74, int(g.alloc(2 * n)));
	g.set_ptr(b, 0xA78, int(g.alloc(2 * n)));
	g.set_ptr(b, 0xC, int(g.alloc(2 * n)));
	g.set_ptr(b, 0x8, int(g.alloc(2 * n)));
	if (town < save_.towns.size()) {
		TownMemory& t = save_.towns[town].memory;
		t.u32(0x2C8) = t.u32(0x2C8) | 0x800;
	}
	set16(0x10, 0);
	auto arr = [&](size_t o) { return size_t(g.ptr(b, o)); };
	auto new_entry = [&](uint16_t s, uint16_t index, uint8_t side) {
		const size_t e = g.alloc(kTownShipSize);
		fn_61a4e0(e, s, b, index, side);
		g.set<uint32_t>(e, 0, kTownShipVtable);
		g.set<uint16_t>(e, 0x1164, 0xFFFF);
		g.set<uint16_t>(e, 0x1166, 0xFFFF);
		g.set<uint8_t>(e, 0x127, 0);
		return e;
	};
	auto add_items = [&](size_t i) {
		g.set<uint16_t>(arr(0xA74), 2 * i, fn_619450(b, 0, uint8_t(i), 0, 0, 0, 0));
		g.set<uint16_t>(arr(0xA78), 2 * i, fn_619450(b, 6, uint8_t(i), 0, 0, 0, 0));
	};
	// Insertion sort of [+0x4][from .. from + m), strongest first (0x61A750);
	// then each scene ship's [+0x138] its place.
	auto sort = [&](size_t from, size_t m) {
		std::vector<int> tmp(m);
		for (size_t i = 0; i < m; ++i) {
			const int e = g.ptr(arr(0x4), 4 * (from + i));
			size_t j = i;
			while (j > 0 && fn_61a750(size_t(e), size_t(tmp[j - 1]))) {
				tmp[j] = tmp[j - 1];
				--j;
			}
			tmp[j] = e;
		}
		for (size_t i = 0; i < m; ++i) {
			g.set_ptr(arr(0x4), 4 * (from + i), tmp[i]);
			g.set<uint16_t>(size_t(tmp[i]), 0x138, uint16_t(from + i));
		}
	};
	if (u16(0x65E) >= convoys) {
		add_items(0);
		g.set_ptr(arr(0x4), 0, int(new_entry(ship, 0, 0)));
		g.set<uint16_t>(arr(0x8), 0, 0xFFFF);
		g.set<uint16_t>(arr(0xC), 0, 0xFFFF);
	} else {
		uint32_t s = save_.convoys[u16(0x65E)].u16(0xA);
		const size_t m = u16(0x658);
		for (size_t i = 0; i < m; ++i) {
			while (s < ships && save_.ships[s].memory.u8(0) != u16(0x35A)) {
				s = save_.ships[s].memory.u16(0x6);
			}
			add_items(i);
			g.set_ptr(arr(0x4), 4 * i, int(new_entry(uint16_t(s), uint16_t(i), 0)));
			g.set<uint16_t>(arr(0x8), 2 * i, uint16_t(i - 1));
			g.set<uint16_t>(arr(0xC), 2 * i, i + 1 < m ? uint16_t(i + 1) : 0xFFFF);
			s = s < ships ? save_.ships[s].memory.u16(0x6) : 0xFFFF;
		}
		sort(0, m);
	}
	set16(0x10, 0);
	const uint16_t m658 = u16(0x658);
	if (u16(0x65A) != 0) {
		set16(0x12, m658);
		g.set<uint16_t>(arr(0x8), 2 * size_t(m658), 0xFFFF);
	}
	set16(0x65C, m658);
	if (u16(0x660) >= convoys) {
		if (u16(0x65A) == 1) {
			add_items(m658);
			g.set_ptr(arr(0x4), 4 * size_t(m658), int(new_entry(defender, m658, 1)));
			g.set<uint16_t>(arr(0x8), 2 * size_t(m658), 0xFFFF);
			g.set<uint16_t>(arr(0xC), 2 * size_t(m658), 0xFFFF);
		}
	} else {
		uint32_t s = save_.convoys[u16(0x660)].u16(0xA);
		const size_t total = u16(0x14);
		for (size_t i = m658; i < total; ++i) {
			add_items(i);
			g.set_ptr(arr(0x4), 4 * i, int(new_entry(uint16_t(s), uint16_t(i), 1)));
			g.set<uint16_t>(arr(0x8), 2 * i, i > m658 ? uint16_t(i - 1) : 0xFFFF);
			g.set<uint16_t>(arr(0xC), 2 * i, i + 1 < total ? uint16_t(i + 1) : 0xFFFF);
			s = save_.ships[s].memory.u16(0x6);
		}
		sort(m658, u16(0x65A));
	}
	set16(0x12, 0xFFFF);
	set16(0x65C, u16(0x65A) ? m658 : 0xFFFF);
	// The wind, as a sea battle's (0x605420).
	int32_t wind = int32_t(battle_rand(g, b) & 3) + 5;
	for (uint32_t k = 0; k < 16; ++k) {
		const uint32_t slot = global::kShips + 0x20 + 0xC * k;
		if (gl.u16(slot) >= ships) {
			continue;
		}
		const int32_t dx = int32_t(gl.s16(slot + 4)) - int16_t(u16(0xAD6));
		const int32_t dy = int32_t(gl.s16(slot + 6)) - int16_t(u16(0xADA));
		if (dy * dy + dx * dx > 900) {
			continue;
		}
		if (gl.u16(slot + 8) == 0) {
			wind += 3;
		}
		if (gl.u16(slot + 8) == 1) {
			wind -= 3;
		}
	}
	if (int16_t(wind) < 2) {
		wind = 2;
	} else if (int16_t(wind) > 10) {
		wind = 10;
	}
	set8(0x670, uint8_t(battle_rand(g, b)));
	set8(0x671, uint8_t(wind));
	set8(0x5C, 0);
	set16(0x2F18, 0);
	set16(0x15C, u8(0x5D) ? 0 : 0xFA);
	set16(0x15E, u8(0x5E) ? 0 : 0xFA);
	if (u8(0x5D) && u8(0x5E)) {
		fn_600660(b, true);
		return;
	}
	// A human side of the local player: the battle's texts and window
	// (0x41D9B0, part 7), or with another battle on screen or its window open
	// the operation 0x96 {0x96, [+0x662], the player, 1} and the player's
	// sides' steps 0xFFFF. As the sea battles until part 7 (and the oracle,
	// TOWN_BATTLE_WINDOW_JUMPS), the second: the computer fights it.
	const uint32_t player = gl.u32(0x6DFC14);
	if (player != u16(0x35A) && player != u16(0x35C)) {
		return;
	}
	uint8_t op[0x14] = {};
	const uint32_t fields[5] = {0x96, u16(0x662), player, 1, 0};
	std::memcpy(op, fields, sizeof(op));
	tick::enqueue_operation(save_, op, sizeof(op));
	for (uint32_t k = 0; k < u8(0x558); ++k) {
		if (u16(0x35A + 2 * k) == player) {
			set16(0x15C + 2 * k, 0xFFFF);
		}
	}
}

}  // namespace p3::data
