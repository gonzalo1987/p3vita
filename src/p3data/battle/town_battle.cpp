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
#include "p3data/battle/pirate_nests.hpp"
#include "p3data/game_fixes.hpp"
#include "p3data/game_texts.hpp"
#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {
namespace alderman_detail {
// town_tick/alderman.cpp
void fn_516170(SaveGame& save, uint32_t pirate, uint32_t type, bool captain, uint32_t strength);
void fn_51a4e0(SaveGame& save, int ship, uint32_t strength, uint8_t level);
}  // namespace alderman_detail
namespace facilities_detail {
uint32_t fn_52bb00(const SaveGame& save, int town);  // town_tick/facilities.cpp
}  // namespace facilities_detail
}  // namespace p3::data::tick

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
			// don't move: on land the loop never ends, the game hangs; seen in the
			// oracle, town_raids, save 7). With fix_game_bugs(), its own; without
			// it too where the game would hang (p3vita never hangs).
			auto water = [&](size_t checked) {
				const int32_t ex = g.get<int16_t>(checked, 0xA), ey = g.get<int16_t>(checked, 0xE);
				int32_t cx = 0, cy = 0;
				view_point_cell(ex, (ey * 11) / 17, cx, cy);
				return fn_5ffe20(b, cx, cy);
			};
			bool ok = water(battle_entry(b, uint16_t(fix_game_bugs() ? first + i : i)));
			if (!ok && !fix_game_bugs() && first != 0 && i >= first) {
				ok = water(battle_entry(b, uint16_t(first + i)));  // The game hangs here.
			}
			if (!ok) {
				--i;
				--n;
			}
		}
	};
	place(0, g.get<uint16_t>(b, 0x658), 1);
	place(g.get<uint16_t>(b, 0x658), g.get<uint16_t>(b, 0x65A), -1);
}

// 0x600660 (method +0x18 of a town battle; arg): a battle over ([+0x5C]) ends
// (method +0x1C, 0x5FE7E0, with 1); else [+0x2F18] 0 and, with
// arg, the formation (method +0x2C, 0x600060). (The local player's human
// side then shows the town (the view and the sea map, 0x6107B9 and
// [0x6E59CC]): the interface, part 7.)
void Sailing::fn_600660(size_t b, bool arg) {
	BattleGraph& g = save_.battles;
	if (g.get<uint8_t>(b, 0x5C) != 0) {
		fn_5fe7e0(b, true);
		return;
	}
	g.set<uint16_t>(b, 0x2F18, 0);
	if (arg) {
		fn_600060(b);
	}
}

// The town attack of a raider at a town (0x506F33), a raiding convoy's guide
// at a town (0x50797E) and a pirate convoy's guide at its route's end
// (0x507C4C, `join` false): unless it ends in the battle of that town and
// the ship's owner (0x6130B0: the ship, or its convoy, joins it, 0x60234C),
// a new town battle (0x611D80) set up (0x5FBFE0) with the ship against the
// town. The slot (0xFF without a free one: the game would use the slot's
// pointer past the array; nothing here).
uint8_t Sailing::town_attack(uint16_t ship, uint8_t town, bool join) {
	const uint8_t owner = save_.ships[ship].memory.u8(0x0);
	if (join) {
		const uint8_t running = fn_6130b0(save_.ships[ship].memory.u8(0x39), owner);
		if (running != 0xFF) {
			fn_60234c(size_t(save_.battles.battle(running)), ship);
			return running;
		}
	}
	const uint8_t slot = fn_611d80();
	if (slot != 0xFF) {
		fn_5fbfe0(size_t(save_.battles.battle(slot)), ship, town);
	}
	return slot;
}

// 0x50E280 (ships object; a convoy, a town's number): the convoy's ships
// nudged a pixel ([+0x1C], [+0x20] in 16.16) by the town's kind (StadtDaten.ini
// [+0x31]): x one less for a kind above 4 and one more for 1..3; y one less
// for 0, 1 and 7 and one more for 3..5.
void Sailing::fn_50e280(uint16_t convoy, uint8_t town) {
	const uint8_t kind = save_.globals.u8(kTownTable + 0x34 * uint32_t(town) + 0x31);
	uint32_t dx = 0, dy = 0;
	if (kind > 4) {
		dx = 0xFFFF0000;
	} else if (kind != 0 && kind < 4) {
		dx = 0x10000;
	}
	if (kind < 2 || kind == 7) {
		dy = 0xFFFF0000;
	} else if (kind > 2 && kind < 6) {
		dy = 0x10000;
	}
	if (convoy >= save_.convoy_count()) {
		return;
	}
	const int32_t ships = int32_t(save_.globals.u16(global::kShipCount));
	for (int32_t s = save_.convoys[convoy].u16(0xA); s < ships; s = save_.ships[size_t(s)].memory.u16(0x6)) {
		ShipMemory& m = save_.ships[size_t(s)].memory;
		m.u32(0x1C) += dx;
		m.u32(0x20) += dy;
	}
}

// 0x515D00 (pirate k's object): whether its nest is destroyed ([+0x15] 1) or
// its convoy is out ([+0x10]).
bool Sailing::fn_515d00(size_t k) const {
	const GameMemory<0x18>& p = *save_.pirates[k];
	return p.u8(0x15) == 1 || p.u16(0x10) < save_.convoy_count();
}

// 0x514530 (pirate k's object): its nest's defence against a Hanse convoy.
// Without a map, the map and the entries set on it (fn_514530_map, battle/
// pirate_nests.cpp). Then, with no convoy out ([+0x10]): the first ship at the
// nest ([+0x12]; with none, a new one, 0x516170 of type 0 with a captain and
// the strength [+0x14] · 40 + 100) leaves the nest's list for the nest's
// place and a convoy of its own (0x5062F0 into [+0x10]); a new one is put
// back in the nest's list with a quarter of its hull and the convoy left
// empty. With the convoy (and no new ship), every ship at the nest joins it
// at its head, with its upgrades (0x51A4E0: the strength ([+0x14] + 1) · its
// type · 20 + 50, the level of the Hanse town's 0x52BB00).
//
// The game computes the new ship's strength in eax after movzx ax: its high
// half is what eax held, the map's address when the nest had one already (a
// game bug: roadmap, «Corregir los fallos del juego»); p3vita takes it as 0,
// as when the map is made in the same call (then eax held the pool's count).
void Sailing::fn_514530(size_t k) {
	GameMemory<0x18>& p = *save_.pirates[k];
	fn_514530_map(save_, k);
	const uint16_t convoys = save_.convoy_count();
	const uint16_t ships = save_.globals.u16(global::kShipCount);
	bool fresh = false;
	if (p.u16(0x10) >= convoys) {
		if (p.u16(0x12) >= ships) {
			tick::alderman_detail::fn_516170(save_, uint32_t(k), 0, true, uint32_t(p.u8(0x14)) * 40 + 100);
			fresh = true;
		}
		const uint16_t first = p.u16(0x12);
		ShipMemory& s = save_.ships[first].memory;
		p.u16(0x12) = s.u16(0x6);
		s.u16(0x6) = 0xFFFF;
		const uint32_t nest = kTownTable + 0x34 * uint32_t(p.u8(0xE));
		s.u16(0x1E) = save_.globals.u16(nest + 0x20);
		s.u16(0x22) = save_.globals.u16(nest + 0x24);
		fn_5062f0(p.u16(0x10), first);
		if (fresh) {
			s.s32(0x18) = s.s32(0x14) >> 2;
			p.u16(0x12) = first;
			save_.convoys[p.u16(0x10)].u16(0xA) = 0xFFFF;
			s.u16(0x8) = 0xFFFF;
		}
	}
	if (p.u16(0x10) >= save_.convoy_count() || fresh) {
		return;
	}
	GameMemory<0x3C>& c = save_.convoys[p.u16(0x10)];
	while (p.u16(0x12) < ships) {
		const uint16_t i = p.u16(0x12);
		ShipMemory& s = save_.ships[i].memory;
		p.u16(0x12) = s.u16(0x6);
		s.u16(0x6) = c.u16(0xA);
		c.u16(0xA) = i;
		s.u16(0x8) = p.u16(0x10);
		const uint32_t level = tick::facilities_detail::fn_52bb00(save_, save_.globals.u8(0x6DE52E));
		tick::alderman_detail::fn_51a4e0(save_, i, (uint32_t(p.u8(0x14)) + 1) * s.u8(0xE) * 20 + 50, uint8_t(level));
	}
}

// The attack on a nest (0x4EBA71..0x4EBC5E; the Hanse task of type 3 in its
// phase 1, town_tick/alderman.cpp, when a ship of the merchant at sea stands
// on the nest): the ship's convoy leaves the list of the convoys at sea
// (0x50A4E0; not there: no attack, false) and goes to the state 0x14 ([+0x18]
// | 0x20, its ships [+0x3C] | 0x20 and the state 0x14; the guide out of the
// list by x, 0x50D040); a ship alone leaves the sea (0x505B60), gets [+0x3C]
// | 0x20, the state 0x14 and a convoy of its own (0x5062F0, [+0x14] 0x12). The
// convoy goes to the nest (0x50E280). If the nest is destroyed or its convoy
// out (0x515D00), the guide joins the battle of the nest (0xFE − k) and the
// convoy's owner (0x6130B0, 0x60234C); else (or without one) [+0x14] of the
// task is the ship, the nest sets up its defence (0x514530) and a new town
// battle starts against it (0x611D80, 0x5FBFE0 with the guide).
//
// A ship alone gets its convoy's list head from a local of 0x4EAFE4 (`head`)
// that only the convoy's path writes (with the convoy, before 0x50A4E0), so
// it keeps what the stack held (the oracle sets it to 0xFFFF at the
// function's entry, NEST_ATTACK_HEAD) or a convoy of an earlier ship of the
// same look at the task that wasn't at sea.
bool Sailing::nest_attack(uint16_t ship, GameMemory<0x18>& task, uint16_t& head) {
	const uint16_t ships = save_.globals.u16(global::kShipCount);
	ShipMemory& s = save_.ships[ship].memory;
	uint16_t convoy = s.u16(0x8);
	if (convoy < save_.convoy_count()) {
		head = convoy;
		if (!fn_50a4e0(convoy)) {
			return false;
		}
		GameMemory<0x3C>& c = save_.convoys[convoy];
		c.u32(0x18) |= 0x20;
		c.u16(0x12) = 0x14;
		for (int32_t i = c.u16(0xA); i < int32_t(ships); i = save_.ships[size_t(i)].memory.u16(0x6)) {
			ShipMemory& m = save_.ships[size_t(i)].memory;
			m.u16(0x3C) |= 0x20;
			m.u16(0x134) = 0x14;
		}
		if (c.u16(0x10) < ships) {
			unlink_x(c.u16(0x10));  // Again (0x50A4E0 did it): nothing.
		}
	} else {
		fn_505b60(ship);
		s.u8(0x3C) |= 0x20;
		s.u16(0x134) = 0x14;
		unlink_x(ship);  // Again (0x505B60 did it): nothing.
		fn_5062f0(head, ship);
		convoy = head;
		save_.convoys[convoy].u16(0x14) = 0x12;
	}
	const uint8_t k = task.u8(0x10);
	fn_50e280(convoy, task.u8(0x11));
	const uint8_t nest = uint8_t(0xFE - k);
	if (fn_515d00(k)) {
		const uint8_t running = fn_6130b0(nest, save_.convoys[convoy].u8(0x0));
		if (running != 0xFF) {
			fn_60234c(size_t(save_.battles.battle(running)), save_.convoys[convoy].u16(0x10));
			return true;
		}
	}
	task.u16(0x14) = ship;
	fn_514530(k);
	const uint8_t slot = fn_611d80();
	// Without a free slot the game uses the pointer past the array; nothing here.
	if (slot != 0xFF) {
		fn_5fbfe0(size_t(save_.battles.battle(slot)), save_.convoys[convoy].u16(0x10), nest);
	}
	return true;
}

// 0x6027CC (on a town battle; a place): the start of a ship that joins the
// battle: at the edge of the town area on the sea's side (by the map file's
// offset [+0x1ED8] · 2, [+0x1EDC], the larger: x 0x10EC heading 0xC0 or x 0x14
// heading 0x40 at y 0x781; else y 0xEEE heading 0 or 0x14 heading 0x80 at x
// 0x880), moved along that edge (by 50, then each time back past the last
// try: −50 − d or 50 − d) until no circle (item 6, [+0xA78]) of the ships
// before it covers the point. The game changes the y step by the x step's
// sign when the y step is 0 or less (it tests dx, not dy): reproduced.
void Sailing::fn_6027cc(size_t b, uint16_t place) {
	BattleGraph& g = save_.battles;
	const int32_t oy = g.get<int32_t>(b, 0x1EDC);
	const int32_t ox = int32_t(uint32_t(g.get<int32_t>(b, 0x1ED8)) * 2);
	int32_t x, y, dx, dy;
	uint8_t heading;
	if (std::abs(ox) > std::abs(oy)) {
		x = ox < 0 ? 0x10EC : 0x14;
		heading = ox < 0 ? 0xC0 : 0x40;
		y = 0x781;
		dx = 0;
		dy = 0x32;
	} else {
		x = 0x880;
		y = oy < 0 ? 0xEEE : 0x14;
		heading = oy < 0 ? 0x00 : 0x80;
		dx = 0x32;
		dy = 0;
	}
	const size_t circles = size_t(g.ptr(b, 0xA78));
	for (int guard = 0; guard < 0x10000; ++guard) {
		const size_t xs = size_t(g.ptr(b, 0x28)), ys = size_t(g.ptr(b, 0x38));
		bool free = true;
		for (uint16_t i = 0; i < place; ++i) {
			const size_t item = size_t(uint16_t(g.get<uint16_t>(circles, 2 * size_t(i)) * 2));
			if (g.get<int16_t>(xs, 2 * item) <= x && g.get<int16_t>(xs, 2 * item + 2) >= x &&
				g.get<int16_t>(ys, 2 * item) <= y && g.get<int16_t>(ys, 2 * item + 2) >= y) {
				free = false;
				break;
			}
		}
		if (free) {
			break;
		}
		x += dx;
		y += dy;
		if (dx > 0) {
			dx = -50 - dx;
		} else if (dx < 0) {
			dx = 50 - dx;
		}
		if (dy > 0) {
			dy = -50 - dy;
		} else if (dx < 0) {
			dy = 50 - dy;
		}
	}
	const size_t e = battle_entry(b, place);
	g.set<uint16_t>(e, 0xA, uint16_t(x));
	g.set<uint16_t>(e, 0xE, uint16_t(y));
	g.set<uint8_t>(e, 0x11, heading);
}

// 0x60234C (on a town battle; a ship): the ship, or all its convoy's ships
// of the same owner, joins the battle, on side A (its owner [+0x35A]; another
// owner's would count for side D's list, but its scene ships are made on side
// 0 all the same). Its convoy goes on the battle's list of joined convoys
// [+0x2F1C] ([+0x2F20] of them, realloc). The scene's arrays grow by them
// (realloc: [+0x4], [+0xA74], [+0xA78], [+0xC], [+0x8]); each new ship at the
// next place gets its two items (0 and 6), a scene ship of 0x1168 bytes
// (0x61A4E0 with the place's low byte, vtable 0x67AAD0, [+0x1164] and
// [+0x1166] 0xFFFF, [+0x127] 0) and its start (0x6027CC); then they go at
// the head of its team's list ([+0x10] for team 0, else [+0x65C]) and its
// count ([+0x658], [+0x65A]) grows. No sort.
void Sailing::fn_60234c(size_t b, uint16_t ship) {
	BattleGraph& g = save_.battles;
	const uint16_t ships = uint16_t(save_.ships.size());
	const uint16_t convoys = save_.convoy_count();
	const ShipMemory& s = save_.ships[ship].memory;
	const size_t side = s.u8(0x0) != g.get<uint16_t>(b, 0x35A) ? 1 : 0;
	const uint8_t team = g.get<uint8_t>(b, 0x559 + side);
	const uint16_t owner = g.get<uint16_t>(b, 0x35A + 2 * side);
	const uint16_t convoy = s.u16(0x8);
	uint32_t joining = 0;
	if (convoy < convoys) {
		const uint16_t n = uint16_t(g.get<uint16_t>(b, 0x2F20) + 1);
		g.set<uint16_t>(b, 0x2F20, n);
		const size_t list = g.realloc_at(b, 0x2F1C, 2 * size_t(n));
		g.set<uint16_t>(list, 2 * size_t(n) - 2, convoy);
		for (uint16_t i = save_.convoys[convoy].u16(0xA); i < ships; i = save_.ships[i].memory.u16(0x6)) {
			if (save_.ships[i].memory.u8(0x0) == owner) {
				++joining;
			}
		}
	} else {
		joining = 1;
	}
	const uint16_t total = uint16_t(g.get<uint16_t>(b, 0x658) + g.get<uint16_t>(b, 0x65A) + joining);
	g.set<uint16_t>(b, 0x14, total);
	g.realloc_at(b, 0x4, 4 * size_t(total));
	g.realloc_at(b, 0xA74, 2 * size_t(total));
	g.realloc_at(b, 0xA78, 2 * size_t(total));
	g.realloc_at(b, 0xC, 2 * size_t(total));
	g.realloc_at(b, 0x8, 2 * size_t(total));
	auto arr = [&](size_t o) { return size_t(g.ptr(b, o)); };
	uint16_t place = uint16_t(g.get<uint16_t>(b, 0x658) + g.get<uint16_t>(b, 0x65A));
	// A new scene ship of `x` at `place`, its items and start.
	auto add = [&](uint16_t x) {
		g.set<uint16_t>(arr(0xA74), 2 * size_t(place), fn_619450(b, 0, uint8_t(place), 0, 0, 0, 0));
		g.set<uint16_t>(arr(0xA78), 2 * size_t(place), fn_619450(b, 6, uint8_t(place), 0, 0, 0, 0));
		const size_t e = g.alloc(0x1168);
		fn_61a4e0(e, x, b, uint8_t(place), 0);
		g.set<uint32_t>(e, 0, kTownShipVtable);
		g.set<uint16_t>(e, 0x1164, 0xFFFF);
		g.set<uint16_t>(e, 0x1166, 0xFFFF);
		g.set<uint8_t>(e, 0x127, 0);
		g.set_ptr(arr(0x4), 4 * size_t(place), int(e));
	};
	if (convoy < convoys) {
		for (uint16_t x = save_.convoys[convoy].u16(0xA); x < ships; x = save_.ships[x].memory.u16(0x6)) {
			if (save_.ships[x].memory.u8(0x0) != owner) {
				continue;
			}
			add(x);
			g.set<uint16_t>(arr(0x8), 2 * size_t(place), uint16_t(place - 1));
			g.set<uint16_t>(arr(0xC), 2 * size_t(place), uint16_t(place + 1));
			fn_6027cc(b, place);
			++place;
		}
	} else {
		add(ship);
		fn_6027cc(b, place);
	}
	const size_t head = team == 0 ? 0x10 : 0x65C;
	const uint16_t old = g.get<uint16_t>(b, head);
	g.set<uint16_t>(arr(0xC), 2 * size_t(total) - 2, old);
	g.set<uint16_t>(arr(0x8), 2 * size_t(old), uint16_t(total - 1));
	const uint16_t first = uint16_t(g.get<uint16_t>(b, 0x658) + g.get<uint16_t>(b, 0x65A));
	g.set<uint16_t>(b, head, first);
	g.set<uint16_t>(arr(0x8), 2 * size_t(first), 0xFFFF);
	const size_t count = team == 0 ? 0x658 : 0x65A;
	g.set<uint16_t>(b, count, uint16_t(g.get<uint16_t>(b, count) + joining));
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
// battle is over at once (method +0x1C, battle/town_end.cpp). Left out: a
// human side's battle window (part 7); its text goes to the viewer
// (town_attack_message).
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
		uint32_t own = 0;
		for (; i < ships; i = save_.ships[i].memory.u16(0x6)) {
			++na;
			own += save_.ships[i].memory.u8(0x0) == u16(0x35A) ? 1 : 0;
		}
		// GAME BUG: with ships of another owner in the convoy, the game's walk
		// for the attacker's ships (0x5FCA7A) runs past the convoy's list, with
		// no end test: it hangs or reads past the ships. p3vita (always) counts
		// only the attacker's ships then.
		if (own != na) {
			na = own;
		}
	}
	set16(0x658, uint16_t(na));
	// The battle over at once: [+0x5C], automatic (0x612D20 sees [+0xAC8]: the
	// game sets it after the call without an attacker, before without a
	// defender), [+0xAC4] 3, then method +0x1C (0x5FE7E0, with 1 without an
	// attacker, 0 without a defender: the battle's end, battle/town_end.cpp).
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
		fn_5fe7e0(b, true);
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
		fn_5fe7e0(b, false);
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
	// TOWN_BATTLE_WINDOW_JUMPS), the second: the computer fights it. For the
	// viewer, the window's text goes to the player's messages
	// (town_attack_message, without the question).
	const uint32_t player = gl.u32(0x6DFC14);
	if (player != u16(0x35A) && player != u16(0x35C)) {
		return;
	}
	if (texts_) {
		player_messages_.push_back(town_attack_message(b, ship, town));
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

// 0x5FD102..0x5FD3DD (in 0x5FBFE0, when the battle window can open): the
// text of the battle's question to the local player, without the question.
// The player attacks ([+0x35A]; the attacker's ship [esp+0x14] is `ship`):
// «%s "%s" attacks the town of %s.» with «Your convoy» (side A's convoy
// [+0x65E] of two ships or more: its first ship [+0xA] has a next [+0x6]) or
// «Your <type>», the ship's name and the town's name (the table 0x6DDA00 by
// the town's index, the letters' bug: tick::letter_town_name); at a nest
// «%s "%s" attacks the hideout of the pirate %s %s.» with the nest's captain
// ([+0x1E90], [+0x1E91], set by the setup). The player is attacked: «The
// town of %s is under attack by %s "%s" %s.» with the town, «the convoy» (the
// same test) or the ship's type as the winner (0x6A175C), its name and «of
// the pirate %s %s», the captain [+0x42] of the guide [+0x10] of convoy
// [+0x65E] by the captains' table (0x6DD7A0: [+0x2], [+0x3]), or «of an
// unknown pirate» (even when the attacker is a merchant's raider). Left out,
// as the game's busy path writes neither: the game's writes of [+0x1E90] and
// [+0x1E91] here (0xFF for a town when the player attacks, the captain's when
// attacked) and of the text into [+0x1310]. The game checks neither the
// convoy [+0x65E] in the second case nor the first ship or the guide against
// the number of ships: here «Your <type>» or the type, and the unknown pirate.
std::string Sailing::town_attack_message(size_t b, uint16_t ship, uint8_t town) const {
	const BattleGraph& g = save_.battles;
	const GlobalMemory& gl = save_.globals;
	const uint32_t ships = gl.u16(global::kShipCount);
	auto raw = [&](GameText t, uint32_t plus = 0) { return texts_->raw(GameText(int(t) + int(plus))); };
	auto u8 = [&](size_t o) { return g.get<uint8_t>(b, o); };
	auto u16 = [&](size_t o) { return g.get<uint16_t>(b, o); };
	const ShipMemory& s = save_.ships[ship].memory;
	std::string name;
	for (size_t i = 0x160; i < 0x180 && s.u8(i) != 0; ++i) {
		name += char(s.u8(i));
	}
	const uint32_t type = s.u8(0xE) & 3;
	const uint16_t c = u16(0x65E);
	const bool in_convoy = c < save_.convoy_count();
	bool two = false;
	if (in_convoy) {
		const uint16_t first = save_.convoys[c].u16(0xA);
		two = first < ships && save_.ships[first].memory.u16(0x6) < ships;
	}
	if (gl.u32(0x6DFC14) == u16(0x35A)) {
		const std::string who = two ? raw(GameText::BattleYourConvoy) : raw(GameText::BattleYourSnaikka, type);
		const uint8_t at = u8(0x1E98);
		if (at < gl.u32(0x6DE4B0)) {
			return GameTexts::sprintf_raw(raw(GameText::TownAttacking), {who, name, tick::letter_town_name(save_, at)});
		}
		return GameTexts::sprintf_raw(raw(GameText::NestAttacking),
			{who, name, game_first_name(save_, u8(0x1E90)), game_family_name(save_, u8(0x1E91))});
	}
	std::string pirate = raw(GameText::BattleOfAnUnknownPirate);
	const uint16_t guide = in_convoy ? save_.convoys[c].u16(0x10) : 0xFFFF;
	if (guide < ships) {
		const uint16_t captain = save_.ships[guide].memory.u16(0x42);
		if (captain < save_.captain_count() && captain < save_.captains.size()) {
			const CaptainMemory& m = save_.captains[captain];
			pirate = GameTexts::sprintf_raw(raw(GameText::BattleOfPirate),
				{game_first_name(save_, m.u8(0x2)), game_family_name(save_, m.u8(0x3))});
		}
	}
	return GameTexts::sprintf_raw(raw(GameText::TownAttacked),
		{tick::letter_town_name(save_, town), two ? raw(GameText::BattleTheConvoyAttacking) : raw(GameText::BattleWinnerSnaikka, type),
			name, pirate});
}

}  // namespace p3::data
