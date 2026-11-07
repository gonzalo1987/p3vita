// The step of a town battle (the battles' piece 6, part 3): operation 0x94
// → method +0x14 of a town battle (0x5FD510), its towers' step (0x5FE080),
// its method +0x0 for each pair of items that meet (0x5FF8E0: a ship on the
// town's land, 0x600770 with 0x601C7B; a cannon ball on a tower) and a tower
// hit (0x60130B). The scene's ships of a town battle (0x1168 bytes, vtable
// 0x67AAD0) take their sub-step in battle/town_ship_step.cpp.
//
// Left out, as in the sea battles until piece 7: what concerns a human side
// (the battle window [+0x12E9], [+0x12EA], 0x41D9B0; the countdown
// [+0x2F18] with 0x6100D9; the town's side all out with a human defender)
// and the cannon balls (only fired with a human side: 0x603130, the towers'
// 0x602A90, 0x60117B). p3vita hands every human side to the computer
// (operation 0x96) before the battle's first step, as the oracle does.

#include <cstring>

#include "p3data/battle/battle_math.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

using namespace battle_math;

namespace {

constexpr uint32_t kTownMaps = 0x6DE4E0;  // u8 per town: its map (iso/towns/<n>.town)
constexpr uint32_t kTick = 0x6DE4B4;  // the world's tick
constexpr uint32_t kPlayer = 0x6DFC14;  // u32: the local player
constexpr uint32_t kNetworkGame = 0x6DFC18;  // u32
constexpr uint8_t kTowerGun = 4;  // the towers' kind of gun in kGunA, kGunB (0x672CD4, 0x672CC4)
constexpr uint8_t kGunC[6] = {9, 10, 22, 24, 30, 18};  // 0x672CC8: per kind of gun

// 0x67AA70 (5 × 9): the weight of a land cell round the ship's cell, by its
// offset (x + 2, y + 4), in the push off the land.
constexpr uint8_t kLandWeight[5][9] = {
	{0x00, 0x00, 0x00, 0x12, 0x14, 0x12, 0x00, 0x00, 0x00},
	{0x00, 0x1C, 0x2B, 0x37, 0x3C, 0x37, 0x2B, 0x1C, 0x00},
	{0x14, 0x28, 0x3C, 0x50, 0x00, 0x50, 0x3C, 0x28, 0x14},
	{0x00, 0x1C, 0x2B, 0x37, 0x3C, 0x37, 0x2B, 0x1C, 0x00},
	{0x00, 0x00, 0x00, 0x12, 0x14, 0x12, 0x00, 0x00, 0x00},
};

// 0x67A8D0 (the same as the sea battles' 0x67AB30): the hull of each type of
// ship, five corners (x, y) round its centre.
constexpr int16_t kTownHull[4][5][2] = {
	{{-19, -31}, {0, -55}, {19, -31}, {19, 47}, {-19, 47}},
	{{-21, -36}, {0, -66}, {21, -36}, {19, 55}, {-19, 55}},
	{{-25, -14}, {0, -54}, {25, -14}, {19, 67}, {-19, 67}},
	{{-22, -25}, {0, -67}, {22, -25}, {19, 81}, {-19, 81}},
};

uint32_t mulhi(uint32_t a, uint32_t b) {
	return uint32_t((uint64_t(a) * b) >> 32);
}

int32_t add32(int32_t a, int32_t b) {
	return int32_t(uint32_t(a) + uint32_t(b));
}

int32_t sub32(int32_t a, int32_t b) {
	return int32_t(uint32_t(a) - uint32_t(b));
}

}  // namespace

// 0x5FFF40 (stdcall; x, y, &cx, &cy): a point's cell of a town battle's map
// of the water: x · 64 / 0x1100 and y · 64 / 3841 (unsigned, by the game's
// magic numbers), at most 63 each.
void fn_5fff40(uint32_t x, uint32_t y, uint16_t& cx, uint16_t& cy) {
	cx = uint16_t(mulhi(x << 6, 0xF0F0F0F1u) >> 12);
	const uint32_t n = y << 6;
	const uint32_t t = mulhi(n, 0x10ECAD3Fu);
	cy = uint16_t((((n - t) >> 1) + t) >> 11);
	if (cx >= 0x40) {
		cx = 0x3F;
	}
	if (cy >= 0x40) {
		cy = 0x3F;
	}
}

// 0x60114B (on a town battle; x, y): its map of the water there ([+0x1EE0],
// x · 64 + y: 1 water, 0 land); 1 off the map.
uint8_t Sailing::fn_60114b(size_t b, int32_t x, int32_t y) const {
	if (x < 0 || x >= 0x40 || y < 0 || y >= 0x40) {
		return 1;
	}
	return save_.battles.get<uint8_t>(b, 0x1EE0 + size_t(x) * 0x40 + size_t(y));
}

// 0x601C7B (on a town battle; a place, a cell of the town's map): whether the
// hull of the ship in the place touches the cell's diamond (±17 round the
// cell's point by the town view, 0x58CFB0, its y · 17 / 11). A cell past the
// map's width or height (x > [+0x2EE0], y > [+0x2EE4]: one past the last
// passes) is not. The ship's centre inside the diamond (by unsigned
// comparisons, as the game) touches it; else each side of the hull against
// each side of the diamond, as the rocks' 0x60B357.
bool Sailing::fn_601c7b(size_t b, uint16_t place, uint16_t x, uint16_t y) const {
	const BattleGraph& g = save_.battles;
	if (int32_t(x) > g.get<int32_t>(b, 0x2EE0) || int32_t(y) > g.get<int32_t>(b, 0x2EE4)) {
		return false;
	}
	const auto [px, py0] = view_cell_point(int32_t(x), int32_t(y));
	const int32_t py = int32_t(uint32_t(py0 * 17) / 11);
	const size_t e = battle_entry(b, place);
	const int32_t sx = g.get<int16_t>(e, 0xA), sy = g.get<int16_t>(e, 0xE);
	if (uint32_t(sx - sy) >= uint32_t(px - 0x11 - py) && uint32_t(sx + sy) >= uint32_t(px + py - 0x11) &&
		uint32_t(sx + sy) <= uint32_t(px + py + 0x11) && uint32_t(sx - sy) <= uint32_t(px + 0x11 - py)) {
		return true;
	}
	const uint8_t heading = g.get<uint8_t>(e, 0x11);
	const int32_t sin = sine(heading), cos = cosine(heading);
	const uint8_t type = scene_ship(e).u8(0xE) & 3;
	auto corner_x = [&](size_t k) {
		return (mul(cos, kTownHull[type][k][0]) - mul(sin, kTownHull[type][k][1])) >> 16;
	};
	auto corner_y = [&](size_t k) {
		return (mul(cos, kTownHull[type][k][1]) + mul(sin, kTownHull[type][k][0])) >> 16;
	};
	int32_t dx = corner_x(4) + sx, dy = corner_y(4) + sy;
	int32_t ax = 0, ay = 0;
	for (size_t k = 0; k < 5; ++k) {
		ax += dx;
		ay += dy;
		dx = corner_x(k) + sx - ax;
		dy = corner_y(k) + sy - ay;
		int32_t qx = px - 0x11 - ax, qy = py - ay;
		for (uint32_t m = 0; m < 4; ++m) {
			const int32_t q0x = qx, q0y = qy;
			switch (m) {
			case 0:
				qx = px - ax;
				qy = py - 0x11 - ay;
				break;
			case 1:
				qx = px + 0x11 - ax;
				qy = py - ay;
				break;
			case 2:
				qx = px - ax;
				qy = py + 0x11 - ay;
				break;
			default:
				qx = px - 0x11 - ax;
				qy = py - ay;
				break;
			}
			int32_t num = mul(dx, q0y) - mul(dy, q0x);
			int32_t den = mul(dx, q0y - qy) - mul(dy, q0x - qx);
			if (den < 0) {
				num = -num;
				den = -den;
			}
			if (num > den || num < 0) {
				continue;
			}
			num = mul(q0x, qy - q0y) - mul(q0y, qx - q0x);
			den = mul(dx, qy - q0y) - mul(dy, qx - q0x);
			if (den < 0) {
				num = -num;
				den = -den;
			}
			if (num > den || num < 0) {
				continue;
			}
			return true;
		}
	}
	return false;
}

// 0x600770 (on a town battle; a place): the ship in the place on the town's
// land. When a cell of the map of the water round its own (0x5FFF40, the 3 ×
// 3 cells, 0x60114B) is land, the land cells of the town's map round its
// cell by the town view (0x58D030 of x, y · 11 / 17: 5 across, 9 down; land
// where 0x5FFE20 is false) that its hull touches (0x601C7B) push it off:
// each such cell (touched or not, if one is) weighs by its offset (0x67AA70)
// on the side it lies (unsigned comparisons, as the game); the sum, scaled
// to the cell's size, gives the push's direction (0x6029B0): its place moved
// by (speed >> 4 + 0x2000) · sin/cos >> 10 (12 with a human side), turned by
// 2 (half a unit) away from it within a quarter turn, and its speed down by
// 1/16 (1/64). (On screen, a sound that draws a rand(): left out.)
void Sailing::fn_600770(size_t b, uint16_t place) {
	BattleGraph& g = save_.battles;
	const size_t e = battle_entry(b, place);
	uint16_t bx = 0, by = 0;
	fn_5fff40(uint32_t(int32_t(g.get<int16_t>(e, 0xA))), uint32_t(int32_t(g.get<int16_t>(e, 0xE))), bx, by);
	bool clear = true;
	for (int32_t y = int32_t(by) - 1; y <= int32_t(by) + 1; ++y) {
		for (int32_t x = int32_t(bx) - 1; x <= int32_t(bx) + 1; ++x) {
			if (!fn_60114b(b, x, y)) {
				clear = false;
			}
		}
	}
	if (clear) {
		return;
	}
	int32_t sx = 0, sy = 0;
	int32_t cx = 0, cy = 0;
	view_point_cell(g.get<int16_t>(e, 0xA), (int32_t(g.get<int16_t>(e, 0xE)) * 11) / 17, cx, cy);
	clear = true;
	for (int32_t j = cy - 4; j <= cy + 4; ++j) {
		for (int32_t i = cx - 2; i <= cx + 2; ++i) {
			if (fn_5ffe20(b, i, j)) {
				continue;
			}
			if (fn_601c7b(b, place, uint16_t(i), uint16_t(j))) {
				clear = false;
			}
			const uint8_t w = kLandWeight[uint8_t(i + 2 - cx)][uint8_t(j + 4 - cy)];
			if (uint32_t(i) < uint32_t(cx)) {
				sx -= w;
			} else if (uint32_t(i) > uint32_t(cx)) {
				sx += w;
			}
			if (uint32_t(j) < uint32_t(cy)) {
				sy -= w;
			} else if (uint32_t(j) > uint32_t(cy)) {
				sy += w;
			}
		}
	}
	if (clear || (sx == 0 && sy == 0)) {
		return;
	}
	sx = add32(mul(sx, 256), 0x70) / 0xE0;
	sy = add32(mul(sy, 256), 0xAC) / 0x158;
	const uint8_t dir = fn_6029b0(sx, sy);
	const bool automatic = g.get<uint8_t>(b, 0xAC8) != 0;
	const int shift = automatic ? 10 : 12;
	const int32_t push = add32(g.get<int32_t>(e, 0x1C) >> 4, 0x2000);
	g.set<int32_t>(e, 0x8, sub32(g.get<int32_t>(e, 0x8), mul(push, sine(dir)) >> shift));
	g.set<int32_t>(e, 0xC, add32(g.get<int32_t>(e, 0xC), mul(push, cosine(dir)) >> shift));
	const uint8_t rel = uint8_t(g.get<uint8_t>(e, 0x11) - dir);
	if (rel > 0 && rel < 0x40) {
		if (automatic) {
			g.set<uint8_t>(e, 0x11, uint8_t(g.get<uint8_t>(e, 0x11) + 2));
		} else {
			g.set<uint16_t>(e, 0x10, uint16_t(g.get<uint16_t>(e, 0x10) + 0x80));
		}
	} else if (rel > 0xC0) {
		if (automatic) {
			g.set<uint8_t>(e, 0x11, uint8_t(g.get<uint8_t>(e, 0x11) - 2));
		} else {
			g.set<uint16_t>(e, 0x10, uint16_t(g.get<uint16_t>(e, 0x10) - 0x80));
		}
	}
	const int32_t speed = g.get<int32_t>(e, 0x1C);
	g.set<int32_t>(e, 0x1C, speed - (speed >> (automatic ? 4 : 6)));
}

// 0x60130B (on a town battle; tower k, a damage): the tower is hit. Above
// the damage, its strength [+0x1EC4 + 4k] goes down (a nest's pool entry of
// the tower's cell then gets 0x515D90, which does nothing). Else it falls:
// the town's buildings (a nest's pool) are walked for a tower at its cell
// (kind 0x30 or 0x31, up to 4 others counted) and, found, an event 0x25 for
// it now (0x4D8CF0: [+0x8] 0xFFFF, [+0xA] 0, its cell, the town, [+0x11] 1;
// in a network game the queue runs, 0x4D85C0); its place [+0x1E9E] 0xFFFF,
// its strength 0, its item freed and [+0xAC4] 1. (On screen, an animation
// and a sound that draws a rand(): left out.)
void Sailing::fn_60130b(size_t b, uint8_t k, int32_t damage) {
	BattleGraph& g = save_.battles;
	GlobalMemory& gl = save_.globals;
	const int32_t strength = g.get<int32_t>(b, 0x1EC4 + 4 * size_t(k));
	const uint8_t town = g.get<uint8_t>(b, 0x1E98);
	if (strength > damage) {
		g.set<int32_t>(b, 0x1EC4 + 4 * size_t(k), strength - damage);
		return;
	}
	const uint8_t cell_x = g.get<uint8_t>(b, 0x1EAE + k), cell_y = g.get<uint8_t>(b, 0x1EB2 + k);
	struct Rec {
		uint8_t x, y, type;
		uint16_t next;
	};
	// The building or pool entry `i`, if any.
	auto at = [&](uint32_t i, Rec& r) -> bool {
		if (town < save_.towns.size()) {
			const SavedTown& t = save_.towns[town];
			if (i >= t.memory.u16(0x776) || i >= t.records.size()) {
				return false;
			}
			const BuildingRecord& br = t.records[i];
			r = {br.x, br.y, br.type, br.next};
			return true;
		}
		const size_t p = size_t(0xFE - town);
		const uint32_t count = save_.pirates[p] ? save_.pirates[p]->u16(0x8) : 0;
		if (i >= count || i >= save_.pirate_nests[p].pool.size()) {
			return false;
		}
		const GameMemory<8>& pe = save_.pirate_nests[p].pool[i];
		r = {pe.u8(0x0), pe.u8(0x1), pe.u8(0x3), pe.u16(0x4)};
		return true;
	};
	Rec r{};
	bool have = town < save_.towns.size() ? at(save_.towns[town].memory.u16(0x768), r) : at(0, r);
	for (uint32_t others = 0; have && others < 4;) {
		if (r.type != 0x30 && r.type != 0x31) {
			have = at(r.next, r);
			continue;
		}
		if (r.x == cell_x && r.y == cell_y) {
			GameMemory<0x18>& ev = save_.events[tick::events_detail::fn_4d8cf0(save_, gl.u32(kTick))];
			ev.u16(0x6) = 0x25;
			ev.u16(0x8) = 0xFFFF;
			ev.u16(0xA) = 0;
			ev.u16(0xC) = r.x;
			ev.u16(0xE) = r.y;
			ev.u8(0x10) = town;
			ev.u8(0x11) = 1;
			if (gl.u32(kNetworkGame) != 0) {
				tick::fn_4d85c0(save_, gl.u32(kTick));
			}
			break;
		}
		have = at(r.next, r);
		++others;
	}
	const uint16_t item = g.get<uint16_t>(b, 0x1EB6 + 2 * size_t(k));
	g.set<uint16_t>(b, 0x1E9E + 2 * size_t(k), 0xFFFF);
	g.set<int32_t>(b, 0x1EC4 + 4 * size_t(k), 0);
	fn_619560(b, item);
	g.set<uint8_t>(b, 0xAC4, 1);
}

// 0x5FE080 (on a town battle; tower k): the tower's step. Loading
// ([+0x1EBE + k], down by 1, and by 0xF more in an automatic battle), it does
// nothing else. Loaded, it aims at the attackers' ship (side A, [+0x10]) not
// out that will be nearest (4 steps ahead by its last move, as a scene's
// ship's guns; within 720 and nearer than the last chosen), unless one of the
// defenders' ships (side D, [+0x65C]) stands in the way: one in the
// direction of the aim (within 4000 / its distance, 128 when nearer than
// 32) not further than the target's distance − 80. The game takes that
// ship's direction as 0x6029B0(dx, dx), not (dx, dy): reproduced. With a
// target (a place whose x isn't 0xFFFF), it fires: damage (its state
// [+0x1E99 + k] · 90 less that · distance / 720; the difficulty, settings
// [+0x12], for a human attacker 0 → · 11/10, 2 → · 19/20, and for a human
// defender the other way) · 28 (at least 1); loading 0x96; in an automatic
// battle the target is hit at once (0x61F96F, guns of either side). With a
// human side the tower fires a cannon ball (0x6111B9, 0x602A90; and a sound
// on screen): part 7, not rewritten.
void Sailing::fn_5fe080(size_t b, uint8_t k) {
	BattleGraph& g = save_.battles;
	const bool automatic = g.get<uint8_t>(b, 0xAC8) != 0;
	const size_t load = 0x1EBE + size_t(k);
	const uint8_t loading = g.get<uint8_t>(b, load);
	if (loading > 0) {
		const uint8_t c = uint8_t(loading - 1);
		g.set<uint8_t>(b, load, c);
		if (automatic) {
			g.set<uint8_t>(b, load, c < 0x10 ? 0 : uint8_t(c - 0xF));
		}
		return;
	}
	const uint16_t tx = g.get<uint16_t>(b, 0x1E9E + 2 * size_t(k));
	const uint16_t ty = g.get<uint16_t>(b, 0x1EA6 + 2 * size_t(k));
	const size_t next = size_t(g.ptr(b, 0xC));
	int32_t best = 0x7E900;
	uint16_t aim_x = 0xFFFF, aim_y = 0;
	uint16_t target = 0;
	for (uint16_t i = g.get<uint16_t>(b, 0x10); i != 0xFFFF; i = g.get<uint16_t>(next, 2 * size_t(i))) {
		const size_t e = battle_entry(b, i);
		if (g.get<uint8_t>(e, 0x12A) & 0x6) {
			continue;
		}
		const int32_t dx0 = int16_t(uint16_t(g.get<uint16_t>(e, 0xA) - tx));
		const int32_t dy0 = int16_t(uint16_t(g.get<uint16_t>(e, 0xE) - ty));
		int16_t px = int16_t(dx0), py = int16_t(dy0);
		for (int it = 0; it < 4; ++it) {
			const int32_t d2 = add32(mul(py, py), mul(px, px));
			const int32_t q = (root(d2) << 8) / 1328;
			int32_t vx = g.get<int32_t>(e, 0x14), vy = g.get<int32_t>(e, 0x18);
			if (automatic) {
				vx >>= 4;
				vy >>= 4;
			}
			py = int16_t(add32(mul(vy, q) >> 16, dy0));
			px = int16_t(add32(mul(vx, q) >> 16, dx0));
		}
		const int32_t d2 = add32(mul(px, px), mul(py, py));
		if (d2 > best) {
			continue;
		}
		bool chosen = true;
		for (uint16_t j = g.get<uint16_t>(b, 0x65C); j != 0xFFFF; j = g.get<uint16_t>(next, 2 * size_t(j))) {
			const size_t f = battle_entry(b, j);
			const int32_t fdx = int16_t(uint16_t(g.get<uint16_t>(f, 0xA) - tx));
			const int32_t fdy = int16_t(uint16_t(g.get<uint16_t>(f, 0xE) - ty));
			uint8_t diff = uint8_t(fn_6029b0(fdx, fdx) - fn_6029b0(px, py));
			const int32_t fq = root(add32(mul(fdx, fdx), mul(fdy, fdy)));
			const int32_t spread = fq < 0x20 ? 0x80 : 4000 / fq;
			if (diff > 0x80) {
				diff = uint8_t(-diff);
			}
			if (int32_t(diff) <= spread && fq >= root(d2) - 0x50) {
				chosen = false;
			}
		}
		if (!chosen) {
			continue;
		}
		best = d2;
		target = i;
		aim_x = uint16_t(tx + uint16_t(px));
		aim_y = uint16_t(ty + uint16_t(py));
	}
	(void)aim_y;  // Where a cannon ball would go (part 7).
	if (aim_x == 0xFFFF) {
		return;
	}
	const int32_t q = root(best);
	const int32_t base = int32_t(g.get<uint8_t>(b, 0x1E99 + k)) * kGunA[kTowerGun];
	int32_t damage = base + mul(base, q) / -720;
	auto human = [&](uint16_t owner) {
		return owner != 0xFF && owner < save_.merchant_count && save_.merchants[owner].memory.u16(0x8) == 0;
	};
	const uint8_t difficulty = save_.settings.u8(0x12);
	if (human(g.get<uint16_t>(b, 0x35A))) {
		if (difficulty == 0) {
			damage = mul(damage, 11) / 10;
		} else if (difficulty == 2) {
			damage = mul(damage, 19) / 20;
		}
	}
	if (human(g.get<uint16_t>(b, 0x35C))) {
		if (difficulty == 0) {
			damage = mul(damage, 19) / 20;
		} else if (difficulty == 2) {
			damage = mul(damage, 11) / 10;
		}
	}
	damage = mul(damage, 2800) / 100;
	if (damage == 0) {
		damage = 1;
	}
	g.set<uint8_t>(b, load, 0x96);
	if (automatic) {
		fn_61f96f(battle_entry(b, target), damage, -1, 3);
	}
	// With a human side: a cannon ball from the tower's side towards the aim (part 7).
}

// 0x5FF8E0 (method +0x0 of a town battle; two items): item a, of a ship
// (kind 0) not out (0x2), meets item b: the town's land (kind 7, the item of
// the whole area) pushes it off (0x600770); a tower (8) nothing; else as in
// the sea (0x608850). A tower (8) meeting a cannon ball (1) is hit by it:
// cannon balls fly only with a human side (part 7), not rewritten.
void Sailing::fn_5ff8e0(size_t b, uint16_t a, uint16_t other) {
	BattleGraph& g = save_.battles;
	const size_t kinds = size_t(g.ptr(b, 0x18)), places = size_t(g.ptr(b, 0x1C));
	if (g.get<uint8_t>(kinds, a) != 0) {
		return;  // A tower and a cannon ball (0x5FF95C): part 7.
	}
	const uint16_t pa = g.get<uint16_t>(places, 2 * size_t(a));
	if (g.get<uint8_t>(battle_entry(b, pa), 0x12A) & 0x2) {
		return;
	}
	switch (g.get<uint8_t>(kinds, other)) {
	case 7:
		fn_600770(b, pa);
		return;
	case 8:
		return;
	default:
		fn_608850(b, a, other);
		return;
	}
}

// 0x5FD510 (method +0x14 of a town battle; steps): the battle's step.
//
// A battle read from the save ([+0x2F1A]) first gets again its towers'
// places and items (every slot: the game tests the address of the strength,
// not its value, so the slots without a tower get them too, from their
// cells) and its map of the water, a town's map file read again (0x58E760).
//
// Then as a sea battle's (0x606CC0): over ([+0x5C]), it counts [+0x15C] down
// by the steps and at 0 ends (method +0x4, 0x602990: piece 6, part 4); while
// a side counts down ([+0x15C] by side) only the player's count goes on (at
// most 5 steps), and nothing else; a human side of the player not on screen
// gets operation 0x96 (to the computer); automatic (both sides the AI's,
// [+0xAC8] 1), (steps + the carry [+0x130F]) >> 4 sub-steps (in a network
// game too, unlike the sea), else all the steps. Each sub-step: the teams'
// sums of the ships not out (0x6) ([+0xA90] strength, [+0xA98] sailors,
// [+0xA9C] fighters, [+0xAA0] arms; [+0xA9C] is not cleared between
// sub-steps, so it keeps its highest; [+0xAB4], [+0xABC] the centre, or the
// area's 0x880, 0x781), side D with its towers (strength · its state · 22 ·
// 64 / 96 >> 10 to [+0xA94], the gun to [+0xAA4], its place to the centre);
// the counter [+0xA8C]; over and [+0x15C] 0 or above 0xF000, the end (method
// +0x1C, 0x5FE7E0, with whether side A has no ship left: part 4); the wind
// (0x6113C9); each ship's sub-step (method +0x0, 0x621770), side A then
// side D; each tower's step (0x5FE080); the battle is over when all side A's
// ships are out, or all side D's and no tower stands ([+0x15C] 0xFFFE the
// first time); the items that meet (0x6197F0).
//
// Left out (part 7): the human side's window, its countdown [+0x2F18]
// (0x6100D9), the town's human side all out, the cannon balls (0x603130) and
// 0x608550 ([+0x12E0]).
void Sailing::fn_5fd510(size_t b, uint16_t steps) {
	BattleGraph& g = save_.battles;
	GlobalMemory& gl = save_.globals;
	auto u8 = [&](size_t o) { return g.get<uint8_t>(b, o); };
	auto u16 = [&](size_t o) { return g.get<uint16_t>(b, o); };
	auto set8 = [&](size_t o, uint8_t v) { g.set<uint8_t>(b, o, v); };
	auto set16 = [&](size_t o, uint16_t v) { g.set<uint16_t>(b, o, v); };
	if (u8(0x2F1A) != 0) {
		const uint8_t town = u8(0x1E98);
		int32_t ox = 0, oy = 0;
		if (town < save_.towns.size()) {
			const auto& ends = save_.town_view.map_ends[gl.u8(kTownMaps + town) & 0x7F];
			g.set<int32_t>(b, 0x1ED8, ends[0]);
			g.set<int32_t>(b, 0x1EDC, ends[1]);
			ox = ends[0];
			oy = ends[1];
		}
		for (uint32_t k = 0; k < 4; ++k) {
			const int32_t cx = int32_t(u8(0x1EAE + k)) + ox, cy = int32_t(u8(0x1EB2 + k)) + oy;
			const uint16_t px = uint16_t(((cy & 1) + 2 * cx + 1) * 17);
			const uint16_t py = uint16_t(cy * 17 - 0x22);
			set16(0x1E9E + 2 * k, px);
			set16(0x1EA6 + 2 * k, py);
			set16(0x1EB6 + 2 * k, fn_619450(b, 8, uint8_t(k), uint16_t(px - 0x22), uint16_t(py - 0x22),
				uint16_t(px + 0x22), uint16_t(py + 0x22)));
		}
		fn_5ffc10(b);
		set8(0x2F1A, 0);
	}
	const uint32_t player = gl.u32(kPlayer);
	if (u8(0x5C) == 0 && u8(0x12E9) == 0) {
		// The local player's side (the last one): while its count runs the
		// window decides (part 7); its countdown [+0x2F18] (0x6100D9 at 0,
		// part 7) is set only with a human side.
		uint32_t side = 0xFF;
		for (uint32_t i = 0; i < u8(0x558); ++i) {
			if (u16(0x35A + 2 * i) == player) {
				side = i;
			}
		}
		if (side != 0xFF && u16(0x15C + 2 * side) == 0 && u16(0x2F18) != 0) {
			set16(0x2F18, u16(0x2F18) > steps ? uint16_t(u16(0x2F18) - steps) : 0);
		}
	}
	if (steps == 0) {
		return;
	}
	if (u8(0x5C) != 0) {
		const uint16_t left = u16(0x15C);
		if (left <= steps) {
			set16(0x15C, 0);
			return;  // Method +0x4 (0x602990) with 1: the battle's end and destructor (piece 6, part 4).
		}
		set16(0x15C, uint16_t(left - steps));
	} else {
		bool counting = false;
		for (uint32_t i = 0; i < u8(0x558); ++i) {
			if (u16(0x15C + 2 * i) > 0) {
				counting = true;
			}
		}
		if (u8(0x12E9) == 0 && counting) {
			const uint16_t most = steps > 5 ? 5 : steps;
			for (uint32_t i = 0; i < u8(0x558); ++i) {
				const uint16_t left = u16(0x15C + 2 * i);
				if (u16(0x35A + 2 * i) == player && u8(0x5D + i) == 0 && left > 0) {
					set16(0x15C + 2 * i, left <= most ? 0xFFFE : uint16_t(left - most));
				}
			}
			return;
		}
		bool automatic = true;
		for (uint32_t i = 0; i < u8(0x558); ++i) {
			if (u8(0x5D + i) != 0) {
				continue;
			}
			automatic = false;
			const uint16_t owner = u16(0x35A + 2 * i);
			if (owner == player && g.get<uint8_t>(0, 0x3FC) != u16(0x662)) {
				uint8_t op[0x14];
				const uint32_t fields[5] = {0x96, u16(0x662), owner, 1, 0};
				std::memcpy(op, fields, sizeof(op));
				tick::enqueue_operation(save_, op, sizeof(op));  // 0x54AA70
			}
		}
		if (automatic) {
			const uint32_t t = (uint32_t(steps) + u8(0x130F)) & 0xFFFF;
			set8(0xAC8, 1);
			set8(0x130F, uint8_t(t & 0xF));
			steps = uint16_t(t >> 4);
		} else {
			set8(0xAC8, 0);
		}
	}
	const size_t next = size_t(g.ptr(b, 0xC));
	auto nx = [&](uint16_t k) { return g.get<uint16_t>(next, 2 * size_t(k)); };
	for (uint16_t step = 0; step < steps; ++step) {
		for (const size_t o : {0xA90, 0xA94, 0xAA0, 0xAA4, 0xAB4, 0xAB8, 0xABC, 0xAC0}) {
			g.set<uint32_t>(b, o, 0);
		}
		set16(0xA98, 0);
		set16(0xA9A, 0);
		for (size_t sd = 0; sd < 2; ++sd) {
			uint8_t n = 0;
			for (uint16_t k = u16(sd == 0 ? 0x10 : 0x65C); k != 0xFFFF; k = nx(k)) {
				const size_t e = battle_entry(b, k);
				if (g.get<uint8_t>(e, 0x12A) & 0x6) {
					continue;
				}
				g.set<uint32_t>(b, 0xA90 + 4 * sd, g.get<uint32_t>(b, 0xA90 + 4 * sd) + uint32_t(fn_62125b(e)));
				const ShipMemory& s = scene_ship(e);
				const uint16_t sailors = s.u16(0x40);
				if (sailors > u16(0xA98 + 2 * sd)) {
					set16(0xA98 + 2 * sd, sailors);
				}
				const uint16_t reserve = s.u16(0x154);
				const uint16_t fighting = uint16_t(sailors < reserve ? sailors + sailors : sailors + reserve);
				if (fighting > u16(0xA9C + 2 * sd)) {
					set16(0xA9C + 2 * sd, fighting);
				}
				if (s.u32(0x120) > g.get<uint32_t>(b, 0xAA0 + 4 * sd)) {
					g.set<uint32_t>(b, 0xAA0 + 4 * sd, s.u32(0x120));
				}
				++n;
				g.set<int32_t>(b, 0xAB4 + 4 * sd, add32(g.get<int32_t>(b, 0xAB4 + 4 * sd), g.get<int16_t>(e, 0xA)));
				g.set<int32_t>(b, 0xABC + 4 * sd, add32(g.get<int32_t>(b, 0xABC + 4 * sd), g.get<int16_t>(e, 0xE)));
			}
			if (sd == 1) {
				for (uint32_t k = 0; k < 4; ++k) {
					const uint32_t strength = g.get<uint32_t>(b, 0x1EC4 + 4 * k);
					if (strength == 0) {
						continue;
					}
					const uint32_t gun = uint32_t((int32_t(u8(0x1E99 + k)) * kGunC[kTowerGun] << 6) / kGunB[kTowerGun]);
					g.set<uint32_t>(b, 0xA94, g.get<uint32_t>(b, 0xA94) + ((strength * gun) >> 10));
					if (gun > g.get<uint32_t>(b, 0xAA4)) {
						g.set<uint32_t>(b, 0xAA4, gun);
					}
					++n;
					g.set<uint32_t>(b, 0xAB8, g.get<uint32_t>(b, 0xAB8) + u16(0x1E9E + 2 * k));
					g.set<uint32_t>(b, 0xAC0, g.get<uint32_t>(b, 0xAC0) + u16(0x1EA6 + 2 * k));
				}
			}
			if (n != 0) {
				g.set<uint32_t>(b, 0xAB4 + 4 * sd, g.get<uint32_t>(b, 0xAB4 + 4 * sd) / n);
				g.set<uint32_t>(b, 0xABC + 4 * sd, g.get<uint32_t>(b, 0xABC + 4 * sd) / n);
			} else {
				g.set<uint32_t>(b, 0xAB4 + 4 * sd, 0x880);
				g.set<uint32_t>(b, 0xABC + 4 * sd, 0x781);
			}
		}
		const uint8_t count = uint8_t(u8(0xA8C) + 1);
		set8(0xA8C, uint32_t(count) * 40 >= 1000 ? 0 : count);
		if (u8(0x5C) == 1) {
			const uint16_t left = u16(0x15C);
			if (left == 0 || left > 0xF000) {
				// Method +0x1C (0x5FE7E0) with whether side A has no ship left:
				// the battle's end (piece 6, part 4).
				return;
			}
		}
		fn_6113c9(b);
		bool a_out = true;
		for (uint16_t k = u16(0x10); k != 0xFFFF; k = nx(k)) {
			const size_t e = battle_entry(b, k);
			if (!(g.get<uint8_t>(e, 0x12A) & 0x6)) {
				a_out = false;
			}
			fn_621770(e, u8(0x670));
		}
		bool d_out = true;
		for (uint16_t k = u16(0x65C); k != 0xFFFF; k = nx(k)) {
			const size_t e = battle_entry(b, k);
			if (!(g.get<uint8_t>(e, 0x12A) & 0x6)) {
				d_out = false;
			}
			fn_621770(e, u8(0x670));
		}
		for (uint8_t k = 0; k < 4; ++k) {
			if (g.get<uint32_t>(b, 0x1EC4 + 4 * size_t(k)) == 0) {
				continue;
			}
			// With side D's ships all out and side D the local player's human
			// side, its countdown, text and window (part 7).
			d_out = false;
			fn_5fe080(b, k);
		}
		// The cannon balls [+0x668] (0x603130): only with a human side (part 7).
		const bool over = a_out || d_out;
		if (u8(0x5C) == 0 && over) {
			set16(0x15C, 0xFFFE);
		}
		set8(0x5C, over ? 1 : 0);
		fn_6197f0(b);
		// 0x608550 with [+0x12E0] and [+0xAD0] (not run in the saves; as the sea battles).
	}
}

}  // namespace p3::data
