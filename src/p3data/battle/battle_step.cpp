// The step of a sea battle (operation 0x94 → method +0x14, 0x606CC0) and
// what it calls (docs/cloud/battles-2.md §4, docs/cloud/battles-2-605420.md).
// A scene's ship is a block of 0x164 bytes of BattleGraph: [+0x4] its ship,
// [+0x12C] its battle, [+0x138] its place, [+0x13D] its side, [+0x12A]
// flags (0x2 out, 0x4, 0x8, 0x20 fled, 0x40 taken, 0x80).

#include <array>
#include <cmath>

#include "p3data/battle/battle_math.hpp"
#include "p3data/sailing.hpp"

namespace p3::data {

using namespace battle_math;

namespace {

// Sailors and the reserve ([+0x40], [+0x154]): twice the sailors if they are
// fewer than the reserve, else both added (u16).
uint32_t fighters(const ShipMemory& s) {
	const uint16_t a = s.u16(0x40), r = s.u16(0x154);
	return uint16_t(a < r ? a + a : a + r);
}





// 0x67AB30: the hull of each type of ship, five corners (x, y) around its centre.
constexpr int16_t kHull[4][5][2] = {
	{{-19, -31}, {0, -55}, {19, -31}, {19, 47}, {-19, 47}},
	{{-21, -36}, {0, -66}, {21, -36}, {19, 55}, {-19, 55}},
	{{-25, -14}, {0, -54}, {25, -14}, {19, 67}, {-19, 67}},
	{{-22, -25}, {0, -67}, {22, -25}, {19, 81}, {-19, 81}},
};
// 0x67AB88 and 0x67AB90 (s16, by type): the length and the width that turn
// a blow into a spin; 0x67AB98 (by type and side of the hull): the side's direction.
constexpr int16_t kTurnLength[4] = {102, 121, 121, 148};
constexpr int16_t kTurnWidth[4] = {38, 42, 50, 44};
constexpr uint8_t kSideDirection[4][5] = {
	{103, 153, 192, 0, 64}, {105, 151, 193, 0, 63}, {107, 149, 195, 0, 61}, {110, 146, 193, 0, 63}};

struct Point {
	int32_t x, y;
};

// A corner of the hull turned to the heading (sin, cos): ((cos·x − sin·y) >> 16,
// (cos·y + sin·x) >> 16).
Point corner(uint8_t type, size_t k, int32_t sin, int32_t cos) {
	const int32_t x = kHull[type & 3][k][0], y = kHull[type & 3][k][1];
	return {(mul(cos, x) - mul(sin, y)) >> 16, (mul(cos, y) + mul(sin, x)) >> 16};
}

}  // namespace

// 0x6029B0 (stdcall: dx, dy): the direction of (dx, dy) in 256ths of a turn.
uint8_t fn_6029b0(int32_t dx, int32_t dy) {
	while (dx > 0x7FFF || dx < -0x7FFF) {
		dx >>= 1;
		dy >>= 1;
	}
	if (dy == 0) {
		return dx > 0 ? 0x40 : 0xC0;
	}
	const int32_t t = int32_t(uint32_t(dx) << 16) / dy;
	uint8_t c;
	if (t < 0) {
		if (-t > 0x10000) {
			const int32_t q = 0x40000000 / t;
			c = uint8_t(kAtan[(0x80 - q) >> 8] - 0x40);
		} else {
			c = uint8_t(-kAtan[(0x200 - t) >> 10]);
		}
	} else if (t > 0x10000) {
		const int32_t q = 0x40000000 / t;
		c = uint8_t(0x40 - kAtan[(q + 0x80) >> 8]);
	} else {
		c = kAtan[(t + 0x200) >> 10];
	}
	return dy > 0 ? uint8_t(0x80 - c) : uint8_t(-c);
}

// 0x619560 (on the scene, an item): the item is freed: back to the free list,
// out of the two lists of pairs, its row and column cleared in the two
// collision matrices ([+0x48], [+0x4C]: pages of 32 items, a bit each), and
// [+0x54] one less if it was the last.
void Sailing::fn_619560(size_t b, uint16_t item) {
	BattleGraph& g = save_.battles;
	auto arr = [&](size_t o) { return size_t(g.ptr(b, o)); };
	auto w = [&](size_t a, size_t i) { return g.get<uint16_t>(a, 2 * i); };
	auto setw = [&](size_t a, size_t i, uint16_t v) { g.set<uint16_t>(a, 2 * i, v); };
	const size_t i = item;
	setw(arr(0x24), i, g.get<uint16_t>(b, 0x20));
	g.set<uint16_t>(b, 0x20, item);
	for (const auto& [first, second, tail] : {std::array<size_t, 3>{0x2C, 0x30, 0x34}, std::array<size_t, 3>{0x3C, 0x40, 0x44}}) {
		for (const size_t node : {2 * i + 1, 2 * i}) {
			const size_t half = node & 1;  // the node's word in the item's pair
			const size_t f = arr(first), sc = arr(second);
			if (g.get<uint16_t>(b, tail) == node) {
				g.set<uint16_t>(b, tail, w(f, g.get<uint16_t>(b, tail)));
			} else {
				setw(f, w(sc, 2 * i + half), w(f, 2 * i + half));
			}
			const uint16_t next = w(f, 2 * i + half);
			if (next != 0xFFFF) {
				setw(sc, next, w(sc, 2 * i + half));
			}
		}
	}
	const size_t pages = g.get<uint16_t>(b, 0x5A);
	const size_t cap = g.get<uint16_t>(b, 0x56);
	const uint32_t mask = ~(1u << (i & 31));
	for (const size_t o : {0x48, 0x4C}) {
		for (size_t j = 0; j < pages; ++j) {
			g.set<uint32_t>(size_t(g.ptr(arr(o), 4 * j)), 4 * i, 0);
		}
		const size_t page = size_t(g.ptr(arr(o), 4 * ((i >> 5) & 0xFF)));
		for (size_t k = 0; k < cap; ++k) {
			g.set<uint32_t>(page, 4 * k, g.get<uint32_t>(page, 4 * k) & mask);
		}
	}
	if (i + 1 == g.get<uint16_t>(b, 0x54)) {
		g.set<uint16_t>(b, 0x54, uint16_t(i));
	}
}

const ShipMemory& Sailing::scene_ship(size_t entry) const {
	return save_.ships[save_.battles.get<uint32_t>(entry, 0x4)].memory;
}

size_t Sailing::scene_battle(size_t entry) const {
	return size_t(save_.battles.ptr(entry, 0x12C));
}

// 0x6212FB (on a scene's ship, another, a flag): whether to treat the other
// as an enemy to attack. With the flag, one of a side that isn't team 0
// ([+0x559 + side]) whose owner is the pirates' (or always, while the battle
// is not running: method +0x28 is 0 for a sea battle). A human side: the
// flag 0x80 of [+0x12A]. Otherwise only if its fighters are at least twice
// the other's sailors, then: the other's team 0 → yes; a pirate other → yes;
// one taken (0x40), or itself not a pirate → no; else by the team's mode
// [+0xACA + team]: 0, 1 no; 2 three times in 11, 3 six in 11 (by their
// places and ships); above, yes.
bool Sailing::fn_6212fb(size_t e, size_t f, bool flag) const {
	const BattleGraph& g = save_.battles;
	const size_t b = scene_battle(e);
	const uint8_t eside = g.get<uint8_t>(e, 0x13D);
	const uint8_t fside = g.get<uint8_t>(f, 0x13D);
	const uint8_t fteam = g.get<uint8_t>(b, 0x559 + size_t(fside));
	const bool running = false;  // Method +0x28 of a sea battle (0x611730).
	if (flag) {
		if (fteam == 0 || running || g.get<uint16_t>(b, 0x35A + 2 * size_t(fside)) == 0xFF) {
			return true;
		}
	}
	if (g.get<uint8_t>(b, 0x5D + size_t(eside)) == 0) {
		return (g.get<uint8_t>(e, 0x12A) >> 7) != 0;
	}
	if (int32_t(fighters(scene_ship(e))) < int32_t(2u * scene_ship(f).u16(0x40))) {
		return false;
	}
	if (fteam == 0 || running) {
		return true;
	}
	if (g.get<uint16_t>(b, 0x35A + 2 * size_t(fside)) == 0xFF) {
		return true;
	}
	if ((g.get<uint8_t>(f, 0x12A) & 0x40) || g.get<uint16_t>(b, 0x35A + 2 * size_t(eside)) != 0xFF) {
		return false;
	}
	const uint8_t mode = g.get<uint8_t>(b, 0xACA + size_t(fteam));
	const uint32_t mix = uint32_t(g.get<uint16_t>(f, 0x138)) + g.get<uint16_t>(e, 0x138) + g.get<uint32_t>(f, 0x4) +
		g.get<uint32_t>(e, 0x4);
	switch (mode) {
	case 0:
	case 1:
		return false;
	case 2:
		return mix % 11 < 3;
	case 3:
		return mix % 11 < 6;
	default:
		return true;
	}
}

// 0x61EFDF (on a scene's ship): its arms per hull (<< 16, signed division by
// the hull when it is at least 1); 1 without arms; 0 if taken (0x40) and
// empty ([+0x118] = 0).
int32_t Sailing::fn_61efdf(size_t e) const {
	const ShipMemory& s = scene_ship(e);
	if ((save_.battles.get<uint8_t>(e, 0x12A) & 0x40) && s.s32(0x118) == 0) {
		return 0;
	}
	const int32_t arms = s.s32(0x120);
	if (arms == 0) {
		return 1;
	}
	const int32_t r = int32_t(uint32_t(arms) << 16);
	return s.s32(0x18) >= 1 ? r / s.s32(0x18) : r;
}

// 0x61F02F (on a scene's ship, another, a flag): whether to board the other.
// Not when fled (0x20). A human side: if [+0x128] is the other's place (or
// none), [+0x13E]. Otherwise, if the other isn't an enemy to attack
// (0x6212FB without the flag): not if the other's ship has [+0x136] & 0x40 or
// it has less than 2000 free; yes if the other fled. Then, with the flag and
// the other not fled: yes if its fighters are more than twice the other's
// sailors, 19 · its battle speed > 20 · the other's and the other is not 0x8;
// without: whether its fighters are more than twice the other's sailors.
bool Sailing::fn_61f02f(size_t e, size_t f, bool flag) const {
	const BattleGraph& g = save_.battles;
	const uint8_t eflags = g.get<uint8_t>(e, 0x12A);
	const uint8_t fflags = g.get<uint8_t>(f, 0x12A);
	if (eflags & 0x20) {
		return false;
	}
	const size_t b = scene_battle(e);
	if (g.get<uint8_t>(b, 0x5D + size_t(g.get<uint8_t>(e, 0x13D))) == 0) {
		const uint16_t chosen = g.get<uint16_t>(e, 0x128);
		if (g.get<uint16_t>(f, 0x138) == chosen || chosen == 0xFFFF) {
			return g.get<uint8_t>(e, 0x13E) != 0;
		}
		return false;
	}
	const ShipMemory& se = scene_ship(e);
	const ShipMemory& sf = scene_ship(f);
	if (!fn_6212fb(e, f, false)) {
		if (sf.u8(0x136) & 0x40) {
			return false;
		}
		if (se.s32(0x10) - se.s32(0x11C) - se.s32(0x118) < 2000) {
			return false;
		}
		if (fflags & 0x20) {
			return true;
		}
	}
	const int32_t mine = int32_t(fighters(se));
	const int32_t theirs = 2 * int32_t(sf.u16(0x40));
	if (flag && !(fflags & 0x20)) {
		if (mine > theirs) {
			const uint32_t other = fn_612930(uint16_t(g.get<uint32_t>(f, 0x4))) * 20u;
			const uint32_t own = fn_612930(uint16_t(g.get<uint32_t>(e, 0x4))) * 19u;
			if (own > other && !(fflags & 0x8)) {
				return true;
			}
		}
		return false;
	}
	return mine > theirs;
}

}  // namespace p3::data

namespace p3::data {

using namespace battle_math;

// The scene's ship in place `i` of the battle ([+0x4], an array of pointers).
size_t Sailing::battle_entry(size_t b, uint16_t i) const {
	const BattleGraph& g = save_.battles;
	return size_t(g.ptr(size_t(g.ptr(b, 0x4)), 4 * size_t(i)));
}

// 0x609D40 (on the battle; i, j, &x, &y): whether the hulls of the ships in
// places i and j cross, and the mean of the points where their sides cross
// (each side of i against each side of j), or 0, 0.
bool Sailing::fn_609d40(size_t b, uint16_t i, uint16_t j, int32_t& out_x, int32_t& out_y) const {
	const BattleGraph& g = save_.battles;
	const size_t ei = battle_entry(b, i), ej = battle_entry(b, j);
	out_x = 0;
	out_y = 0;
	bool hit = false;
	uint8_t count = 0;
	const uint8_t hi = g.get<uint8_t>(ei, 0x11), hj = g.get<uint8_t>(ej, 0x11);
	const int32_t sin_i = sine(hi), cos_i = cosine(hi), sin_j = sine(hj), cos_j = cosine(hj);
	const uint8_t ti = scene_ship(ei).u8(0xE), tj = scene_ship(ej).u8(0xE);
	const int32_t xi = g.get<int16_t>(ei, 0xA), yi = g.get<int16_t>(ei, 0xE);
	const int32_t xj = g.get<int16_t>(ej, 0xA), yj = g.get<int16_t>(ej, 0xE);
	int32_t sx = 0, sy = 0;
	Point d = corner(ti, 4, sin_i, cos_i);
	for (size_t k = 0; k < 5; ++k) {
		sx += d.x;
		sy += d.y;
		const Point p = corner(ti, k, sin_i, cos_i);
		d = {p.x - sx, p.y - sy};
		const Point q4 = corner(tj, 4, sin_j, cos_j);
		Point q = {q4.x + (xj - xi) - sx, q4.y + (yj - yi) - sy};
		for (size_t m = 0; m < 5; ++m) {
			const Point q0 = q;
			const Point c = corner(tj, m, sin_j, cos_j);
			q = {c.x + (xj - xi) - sx, c.y + (yj - yi) - sy};
			int32_t num = mul(d.x, q0.y) - mul(d.y, q0.x);
			int32_t den = mul(d.x, q0.y - q.y) - mul(d.y, q0.x - q.x);
			if (den < 0) {
				num = -num;
				den = -den;
			}
			if (num > den || num < 0) {
				continue;
			}
			num = mul(q0.x, q.y - q0.y) - mul(q0.y, q.x - q0.x);
			den = mul(d.x, q.y - q0.y) - mul(d.y, q.x - q0.x);
			if (den < 0) {
				num = -num;
				den = -den;
			}
			if (den <= 0 || num > den || num < 0) {
				continue;
			}
			hit = true;
			out_x += xi + sx + mul(d.x, num) / den;
			out_y += yi + sy + mul(d.y, num) / den;
			++count;
		}
	}
	if (count > 0) {
		out_x /= count;
		out_y /= count;
	}
	return hit;
}

// 0x60ADBF (on the battle; i, x0, y0, x1, y1, &x, &y, &side): where the
// segment from (x0, y0) to (x1, y1) first meets the hull of the ship in
// place i, and the side it meets (the corner it starts from); (x1, y1) and
// the side untouched if it doesn't.
void Sailing::fn_60adbf(size_t b, uint16_t i, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t& out_x,
	int32_t& out_y, uint8_t& side) const {
	const BattleGraph& g = save_.battles;
	const size_t e = battle_entry(b, i);
	const uint8_t h = g.get<uint8_t>(e, 0x11);
	const int32_t sin = sine(h), cos = cosine(h);
	const uint8_t t = scene_ship(e).u8(0xE);
	const int32_t x = g.get<int16_t>(e, 0xA), y = g.get<int16_t>(e, 0xE);
	const int32_t dx = x1 - x0, dy = y1 - y0;
	int32_t best = 0x7FFFFFFF;
	const Point c4 = corner(t, 4, sin, cos);
	Point p = {c4.x + x - x0, c4.y + y - y0};
	for (size_t k = 0; k < 5; ++k) {
		const Point p0 = p;
		const Point c = corner(t, k, sin, cos);
		p = {c.x + x - x0, c.y + y - y0};
		int32_t num = mul(dx, p0.y) - mul(dy, p0.x);
		int32_t den = mul(dx, p0.y - p.y) - mul(dy, p0.x - p.x);
		if (den < 0) {
			num = -num;
			den = -den;
		}
		if (num > den || num < 0) {
			continue;
		}
		num = mul(p0.x, p.y - p0.y) - mul(p0.y, p.x - p0.x);
		den = mul(dx, p.y - p0.y) - mul(dy, p.x - p0.x);
		if (den < 0) {
			num = -num;
			den = -den;
		}
		if (num < 0 || den <= 0) {
			continue;
		}
		const int32_t at = int32_t(uint32_t(num) << 8) / den;
		if (at >= best) {
			continue;
		}
		best = at;
		side = uint8_t((k + 4) % 5);
	}
	if (best < 0x7FFFFFFF) {
		out_x = x0 + (mul(dx, best) >> 8);
		out_y = y0 + (mul(dy, best) >> 8);
	} else {
		out_x = x1;
		out_y = y1;
	}
}

// 0x608F70 (on the battle; i, j with i < j): the ships in places i and j
// collide. Closer than 10, they are pushed apart (to 11) and that is all.
// Else, if their hulls cross (0x609D40), each gets a blow where the line
// from the other's centre to the crossing meets its hull (0x60ADBF): a push
// from the side it hits ([+0x8], [+0xC]), a turn ([+0x10]) and, when the
// blow is behind the centre, its speed [+0x1C] changed and the last move
// ([+0x14], [+0x18]) undone. In an automatic battle ([+0xAC8], no human
// side; its steps are fewer and longer) the pushes and turns are larger. The crash's sound (0x443690 gives no sound in the
// emulator) is left out. Whether they collided.
//
// When the line meets no side of the hull, 0x60ADBF leaves the side as it
// was: the game reads a byte of its stack never written (see «Game bugs» in
// docs/cloud/battles-2.md); here it is 0.
bool Sailing::fn_608f70(size_t b, uint16_t i, uint16_t j) {
	BattleGraph& g = save_.battles;
	if (!(i < j)) {
		return false;
	}
	const size_t ei = battle_entry(b, i), ej = battle_entry(b, j);
	{
		const int32_t dx = g.get<int16_t>(ei, 0xA) - g.get<int16_t>(ej, 0xA);
		const int32_t dy = g.get<int16_t>(ei, 0xE) - g.get<int16_t>(ej, 0xE);
		const int32_t d2 = mul(dy, dy) + mul(dx, dx);
		if (d2 < 100) {
			const int32_t r = d2 < 0 ? 0 : int32_t(std::sqrt(double(d2)));
			if (r != 0) {
				const int32_t sx = mul(11 - r, dx) / r, sy = mul(11 - r, dy) / r;
				g.set<uint16_t>(ei, 0xA, uint16_t(g.get<uint16_t>(ei, 0xA) - sx));
				g.set<uint16_t>(ei, 0xE, uint16_t(g.get<uint16_t>(ei, 0xE) - sy));
				g.set<uint16_t>(ej, 0xA, uint16_t(g.get<uint16_t>(ej, 0xA) + sx));
				g.set<uint16_t>(ej, 0xE, uint16_t(g.get<uint16_t>(ej, 0xE) + sy));
				return true;
			}
		}
	}
	int32_t cx = 0, cy = 0;
	if (!fn_609d40(b, i, j, cx, cy)) {
		return false;
	}
	int32_t xi = 0, yi = 0, xj = 0, yj = 0;
	uint8_t side_i = 0, side_j = 0;
	fn_60adbf(b, i, g.get<int16_t>(ej, 0xA), g.get<int16_t>(ej, 0xE), cx, cy, xi, yi, side_i);
	fn_60adbf(b, j, g.get<int16_t>(ei, 0xA), g.get<int16_t>(ei, 0xE), cx, cy, xj, yj, side_j);
	const bool close = g.get<uint8_t>(b, 0xAC8) != 0;
	// One ship's blow; `shift` the push's.
	auto blow = [&](size_t e, int32_t px, int32_t py, uint8_t side, bool halve_sine, int shift) -> uint16_t {
		const uint8_t type = scene_ship(e).u8(0xE);
		const uint8_t h = g.get<uint8_t>(e, 0x11);
		const int32_t dx = px - g.get<int16_t>(e, 0xA), dy = py - g.get<int16_t>(e, 0xE);
		const int32_t s = sine(h), c = cosine(h);
		const int32_t ly = (mul(c, dy) - mul(s, dx)) >> 16;
		const int32_t lx = (mul(s, dy) + mul(c, dx)) >> 16;
		const uint8_t at = fn_6029b0(-lx, -ly);
		const uint8_t dir = kSideDirection[type & 3][side];
		const uint8_t rel = uint8_t(at - dir);
		const uint8_t world = uint8_t(dir + h);
		int32_t push = cosine(rel);
		if (push > 0) {
			push >>= 1;
			const int32_t ws = halve_sine ? sine(world) >> 1 : sine(world);
			const int32_t wc = halve_sine ? cosine(world) >> 1 : cosine(world);
			g.set<int32_t>(e, 0x8, g.get<int32_t>(e, 0x8) + (mul(ws, push) >> shift));
			g.set<int32_t>(e, 0xC, g.get<int32_t>(e, 0xC) - (mul(wc, push) >> shift));
		}
		int32_t turn = sine(rel);
		if ((side == 0 && turn < 0) || (side == 1 && turn > 0)) {
			turn = 0;
		}
		const uint16_t heading = uint16_t((close ? turn >> 4 : turn >> 6) + g.get<uint16_t>(e, 0x10));
		if (ly < 0) {
			const int32_t speed = g.get<int32_t>(e, 0x1C);
			const int32_t den = mul(mul(kTurnLength[type & 3], kTurnWidth[type & 3]), close ? 2 : 8);
			g.set<int32_t>(e, 0x1C, mul(mul(kTurnWidth[type & 3] - lx, speed), ly) / den + speed);
			g.set<int32_t>(e, 0x8, g.get<int32_t>(e, 0x8) - g.get<int32_t>(e, 0x14));
			g.set<int32_t>(e, 0xC, g.get<int32_t>(e, 0xC) - g.get<int32_t>(e, 0x18));
		}
		return heading;
	};
	const uint16_t hi = blow(ei, xi, yi, side_i, true, close ? 11 : 13);
	const uint16_t hj = blow(ej, xj, yj, side_j, false, close ? 12 : 14);
	g.set<uint16_t>(ei, 0x10, hi);
	g.set<uint16_t>(ej, 0x10, hj);
	return true;
}

// 0x609BA0 (on the battle; i, j): the ships in places i and j collide
// (0x608F70) and may grapple: if they did collide (or, in an automatic
// battle [+0xAC8], one has fled, 0x20) and the battle isn't over ([+0x5C]), each one that would
// board the other (0x61F02F) and has it as an enemy (0x6212FB), or the other
// isn't taken (0x40) or its ship has [+0x118], makes them grapple: 0x8 for
// both, [+0x128] the other's place, [+0x14A] 0 for i and 1 for j.
void Sailing::fn_609ba0(size_t b, uint16_t i, uint16_t j) {
	BattleGraph& g = save_.battles;
	if (!fn_608f70(b, i, j)) {
		const size_t ei = battle_entry(b, i), ej = battle_entry(b, j);
		if (!((g.get<uint8_t>(ei, 0x12A) | g.get<uint8_t>(ej, 0x12A)) & 0x20) || g.get<uint8_t>(b, 0xAC8) == 0) {
			return;
		}
	}
	if (g.get<uint8_t>(b, 0x5C) != 0) {
		return;
	}
	const size_t ei = battle_entry(b, i), ej = battle_entry(b, j);
	auto wants = [&](size_t e, size_t f) {
		return fn_6212fb(e, f, false) || scene_ship(f).s32(0x118) != 0 || !(g.get<uint8_t>(f, 0x12A) & 0x40);
	};
	const bool first = fn_61f02f(ei, ej, false) && wants(ei, ej);
	const bool second = fn_61f02f(ej, ei, false) && wants(ej, ei);
	if (!first && !second) {
		return;
	}
	g.set<uint8_t>(ei, 0x12A, g.get<uint8_t>(ei, 0x12A) | 0x8);
	g.set<uint8_t>(ej, 0x12A, g.get<uint8_t>(ej, 0x12A) | 0x8);
	g.set<uint16_t>(ei, 0x128, j);
	g.set<uint16_t>(ej, 0x128, i);
	g.set<uint8_t>(ei, 0x14A, 0);
	g.set<uint8_t>(ej, 0x14A, 1);
}

}  // namespace p3::data

namespace p3::data {

using namespace battle_math;

// 0x60BC5A (on the battle; an item, a place): whether the ship in the place is
// inside the item's circle (the centre of its box, radius half its width).
bool Sailing::fn_60bc5a(size_t b, uint16_t item, uint16_t place) const {
	const BattleGraph& g = save_.battles;
	const size_t xs = size_t(g.ptr(b, 0x28)), ys = size_t(g.ptr(b, 0x38));
	const int32_t x1 = g.get<int16_t>(xs, 4 * size_t(item)), x2 = g.get<int16_t>(xs, 4 * size_t(item) + 2);
	const int32_t y1 = g.get<int16_t>(ys, 4 * size_t(item)), y2 = g.get<int16_t>(ys, 4 * size_t(item) + 2);
	const size_t e = battle_entry(b, place);
	const int32_t dx = int16_t(uint16_t(((x2 + x1) >> 1) - g.get<uint16_t>(e, 0xA)));
	const int32_t dy = int16_t(uint16_t(((y2 + y1) >> 1) - g.get<uint16_t>(e, 0xE)));
	const int32_t r = int16_t(uint16_t((x2 - x1) >> 1));
	return mul(dx, dx) + mul(dy, dy) < mul(r, r);
}

// 0x60BCDA (on the battle; an item, a place, a, b, a level): the ship in the
// place marks the item in its 256 directions ([+0x27], the highest level
// kept): around the item's direction, as wide as the item looks from the
// ship with a radius of half its width + a − b (or by how far inside it the
// ship is), the level falling towards the edges. Without a level, by the
// item's kind: 0x40 (4), 0x20 (5), 0x80 (6). With one, a ship's circle
// (kind 6) further than twice the radius is left out.
//
// The game's loops never end when the width is 0 (a radius of 0) and the
// level isn't, or when it is 0x100 (see «Game bugs» in
// docs/cloud/battles-2.md): p3vita stops them.
void Sailing::fn_60bcda(size_t b, uint16_t item, uint16_t place, int32_t a, int32_t minus, uint8_t level) {
	BattleGraph& g = save_.battles;
	const size_t xs = size_t(g.ptr(b, 0x28)), ys = size_t(g.ptr(b, 0x38));
	const int32_t x1 = g.get<int16_t>(xs, 4 * size_t(item)), x2 = g.get<int16_t>(xs, 4 * size_t(item) + 2);
	const int32_t y1 = g.get<int16_t>(ys, 4 * size_t(item)), y2 = g.get<int16_t>(ys, 4 * size_t(item) + 2);
	const size_t e = battle_entry(b, place);
	const uint8_t kind = g.get<uint8_t>(size_t(g.ptr(b, 0x18)), item);
	const int32_t r = ((x2 - x1) >> 1) + a - minus;
	const int32_t dx = ((x2 + x1) >> 1) - g.get<int16_t>(e, 0xA);
	const int32_t dy = ((y2 + y1) >> 1) - g.get<int16_t>(e, 0xE);
	const uint8_t dir = fn_6029b0(dx, dy);
	const int32_t d2 = mul(dx, dx) + mul(dy, dy);
	if (d2 == 0) {
		return;
	}
	const int32_t d = root(d2);
	if (level != 0 && 2 * r < d && kind == 6) {
		return;
	}
	int16_t half;
	if (r < d) {
		const int32_t rest = root(d2 - mul(r, r));
		if (rest == 0) {
			return;  // The game divides by 0 here.
		}
		const int32_t q = int32_t(uint32_t(r) << 16) / rest;
		const int32_t t = q < 0 ? -q : q;
		const int32_t w = t > 0x10000 ? 0x40 - kAtan[((0x40000000 / t) + 0x80) >> 8] : kAtan[(t + 0x200) >> 10];
		half = int16_t(q < 0 ? 0x100 - w : w);
	} else {
		half = int16_t(r + 0x40 - d);
		if (half > 0x80) {
			half = 0x80;
		}
	}
	uint8_t step = level;
	if (level == 0) {
		step = kind == 4 ? 0x40 : kind == 5 ? 0x20 : kind == 6 ? 0x80 : 0;
	}
	uint16_t acc = 0;
	uint8_t mark = step;
	for (int k = 0; k <= half && k < 0x100; ++k) {
		acc = uint16_t(acc + step);
		while (int32_t(acc) > half) {
			if (half <= 0) {
				return;  // The game never leaves this loop.
			}
			--mark;
			acc = uint16_t(acc - half);
		}
		for (const uint8_t at : {uint8_t(dir + k), uint8_t(dir - k)}) {
			if (g.get<uint8_t>(e, 0x27 + size_t(at)) < mark) {
				g.set<uint8_t>(e, 0x27 + size_t(at), mark);
			}
		}
	}
}

// 0x608850 (method +0x0 of the battle; two items): item a, of a ship (kind
// 0) not out (0x2), meets item b. Another ship (0): of the same team they
// only collide (0x608F70); of another, once for the pair (lower place
// first), they collide and may grapple (0x609BA0), or only collide if one
// is grappling (0x8). The circles of kinds 4, 5 and 6 (another ship's; not
// for an AI ship without a target, unless [+0x5C] or 0x1) are marked in the
// ship's directions (0x60BCDA) when it is inside (0x60BC5A); an enemy's only
// if it wouldn't board it (0x61F02F).
//
// Not rewritten yet (not run in the saves): kind 1 (a cannon ball, 0x60A73C
// and 0x603470), kinds 2 and 3 (a rock and an islet, 0x60B357, 0x61F96F; in
// the emulator their boxes have no size, see kTownViewHalf).
void Sailing::fn_608850(size_t b, uint16_t a, uint16_t other) {
	BattleGraph& g = save_.battles;
	const size_t kinds = size_t(g.ptr(b, 0x18)), places = size_t(g.ptr(b, 0x1C));
	if (g.get<uint8_t>(kinds, a) != 0) {
		return;
	}
	const uint16_t pa = g.get<uint16_t>(places, 2 * size_t(a));
	const size_t ei = battle_entry(b, pa);
	const uint8_t flags = g.get<uint8_t>(ei, 0x12A);
	if (flags & 0x2) {
		return;
	}
	auto team = [&](size_t e) { return g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(e, 0x13D))); };
	// An AI ship without a target ([+0x128]) only looks round when [+0x5C] or 0x1.
	auto looks = [&]() {
		return g.get<uint16_t>(ei, 0x128) != 0xFFFF || g.get<uint8_t>(b, 0x5D + size_t(g.get<uint8_t>(ei, 0x13D))) != 0 ||
			g.get<uint8_t>(b, 0x5C) != 0 || (g.get<uint8_t>(ei, 0x12A) & 0x1);
	};
	switch (g.get<uint8_t>(kinds, other)) {
	case 0: {
		const uint16_t pb = g.get<uint16_t>(places, 2 * size_t(other));
		const size_t ej = battle_entry(b, pb);
		if (team(ei) == team(ej)) {
			fn_608f70(b, pa, pb);
		} else if (pa < pb) {
			if ((g.get<uint8_t>(ej, 0x12A) | flags) & 0x8) {
				fn_608f70(b, pa, pb);
			} else {
				fn_609ba0(b, pa, pb);
			}
		}
		return;
	}
	case 4:
	case 5:
		if (looks() && fn_60bc5a(b, other, pa)) {
			fn_60bcda(b, other, pa, 0x28, 0xC8, 0);
		}
		return;
	case 6: {
		const uint16_t pb = g.get<uint16_t>(places, 2 * size_t(other));
		if (pa == pb || !looks()) {
			return;
		}
		const size_t ej = battle_entry(b, pb);
		const bool same = team(ei) == team(ej);
		if (!same && fn_61f02f(ei, ej, false)) {
			return;
		}
		if (fn_60bc5a(b, other, pa)) {
			if (same) {
				fn_60bcda(b, other, pa, 0x28, 0x168, 0x20);
			} else {
				fn_60bcda(b, other, pa, 0xB4, 0x168, 0);
			}
		}
		return;
	}
	default:
		return;
	}
}

}  // namespace p3::data

namespace p3::data {

using namespace battle_math;

// 0x6113C9 (on the battle): the wind of a sub-step, by the battle's
// generator x. Its direction [+0x670] turns 8 one way or the other when x &
// 0xF (automatic, [+0xAC8]) or x & 0xFF (with a human side) is 0 or 1; its
// strength [+0x671] moves one towards ((x >> 16 & 0xFF) % 10 + strength + 2)
// / 2 when x >> 8 & 0xF (automatic) or x >> 8 & 0xFF is 0.
//
// With a human side, after a change, the battle on screen ([+0x662] =
// [0x6E59CC]) moves the 0xE0 objects of [+0x1B10] with the wind (UI): left
// out, not run in the saves (all their battles are automatic).
void Sailing::fn_6113c9(size_t b) {
	BattleGraph& g = save_.battles;
	const uint32_t x = battle_rand(g, b);
	const uint8_t r = uint8_t(x >> 16);
	const uint8_t c = uint8_t(x >> 8);
	auto strength = [&]() {
		const uint8_t s = g.get<uint8_t>(b, 0x671);
		const uint8_t to = uint8_t((r % 10 + s + 2) >> 1);
		if (to < s) {
			g.set<uint8_t>(b, 0x671, uint8_t(s - 1));
		} else if (to > s) {
			g.set<uint8_t>(b, 0x671, uint8_t(s + 1));
		}
	};
	auto turn = [&](uint32_t v) {
		if (v == 0) {
			g.set<uint8_t>(b, 0x670, uint8_t(g.get<uint8_t>(b, 0x670) + 8));
		} else if (v == 1) {
			g.set<uint8_t>(b, 0x670, uint8_t(g.get<uint8_t>(b, 0x670) - 8));
		}
	};
	if (g.get<uint8_t>(b, 0xAC8) != 0) {
		turn(x & 0xF);
		if ((c & 0xF) == 0) {
			strength();
		}
		return;
	}
	turn(x & 0xFF);
	if (c == 0) {
		strength();
	}
}

}  // namespace p3::data

namespace p3::data {

using namespace battle_math;

// 0x6197F0 (on the scene): the items that meet. Each axis keeps the ends of
// every item's box (node 2·item its start, 2·item + 1 its end; the keys in
// [+0x28] for x, [+0x38] for y) in a list ([+0x2C]/[+0x3C] next,
// [+0x30]/[+0x40] previous, [+0x34]/[+0x44] the head), sorted by bubbling
// until no pair is swapped; an end passing a start (or back) sets (or
// clears) the pair's bits in the axis' matrix ([+0x48], [+0x4C]: pages of 32
// items, a bit each). Then for each item i and each item k crossing it on
// both axes ([+0x50] = [+0x48] & [+0x4C]), method +0x0 of the battle
// (0x608850) with i, k. (The game tests the bits of [+0x50] in groups; they
// don't change during the calls.)
void Sailing::fn_6197f0(size_t b) {
	BattleGraph& g = save_.battles;
	auto bit = [&](size_t matrix, uint32_t x, uint32_t y, bool on) {
		const size_t page = size_t(g.ptr(matrix, 4 * size_t(x >> 5)));
		const uint32_t w = g.get<uint32_t>(page, 4 * size_t(y));
		g.set<uint32_t>(page, 4 * size_t(y), on ? w | 1u << (x & 31) : w & ~(1u << (x & 31)));
	};
	for (const auto& [keys_at, next_at, prev_at, head_at, matrix_at] :
		{std::array<size_t, 5>{0x28, 0x2C, 0x30, 0x34, 0x48}, std::array<size_t, 5>{0x38, 0x3C, 0x40, 0x44, 0x4C}}) {
		bool swapped = true;
		while (swapped && g.get<uint16_t>(b, head_at) != 0xFFFF) {
			swapped = false;
			const size_t keys = size_t(g.ptr(b, keys_at)), next = size_t(g.ptr(b, next_at)), prev = size_t(g.ptr(b, prev_at));
			const size_t matrix = size_t(g.ptr(b, matrix_at));
			auto nx = [&](uint16_t v) { return g.get<uint16_t>(next, 2 * size_t(v)); };
			uint16_t p = 0xFFFF;
			uint16_t d = g.get<uint16_t>(b, head_at);
			uint16_t a = nx(d);
			if (a == 0xFFFF) {
				break;
			}
			do {
				const uint16_t n = nx(a);
				if (g.get<int16_t>(keys, 2 * size_t(d)) > g.get<int16_t>(keys, 2 * size_t(a))) {
					swapped = true;
					if (p != 0xFFFF) {
						g.set<uint16_t>(next, 2 * size_t(p), a);
					} else {
						g.set<uint16_t>(b, head_at, a);
					}
					g.set<uint16_t>(next, 2 * size_t(d), n);
					g.set<uint16_t>(prev, 2 * size_t(d), a);
					g.set<uint16_t>(next, 2 * size_t(a), d);
					g.set<uint16_t>(prev, 2 * size_t(a), p);
					if (n != 0xFFFF) {
						g.set<uint16_t>(prev, 2 * size_t(n), d);
					}
					if ((a & 1) && !(d & 1)) {
						bit(matrix, a >> 1, d >> 1, false);
						bit(matrix, d >> 1, a >> 1, false);
					} else if (!(a & 1) && (d & 1)) {
						bit(matrix, a >> 1, d >> 1, true);
						bit(matrix, d >> 1, a >> 1, true);
					}
					p = a;
				} else {
					p = d;
					d = a;
				}
				a = n;
			} while (a != 0xFFFF);
		}
	}
	const uint16_t used = g.get<uint16_t>(b, 0x54);
	const uint16_t pages = uint16_t((int32_t(used) + 0x1F) >> 5);
	for (uint16_t i = 0; i < g.get<uint16_t>(b, 0x54); ++i) {
		for (uint16_t p = 0; p < pages; ++p) {
			const size_t px = size_t(g.ptr(size_t(g.ptr(b, 0x48)), 4 * size_t(p)));
			const size_t py = size_t(g.ptr(size_t(g.ptr(b, 0x4C)), 4 * size_t(p)));
			const size_t both = size_t(g.ptr(size_t(g.ptr(b, 0x50)), 4 * size_t(p)));
			g.set<uint32_t>(both, 4 * size_t(i), g.get<uint32_t>(px, 4 * size_t(i)) & g.get<uint32_t>(py, 4 * size_t(i)));
			for (uint32_t k = 0; k < 32; ++k) {
				const size_t now = size_t(g.ptr(size_t(g.ptr(b, 0x50)), 4 * size_t(p)));
				if (g.get<uint32_t>(now, 4 * size_t(i)) >> k & 1) {
					fn_608850(b, i, uint16_t(32 * p + k));
				}
			}
		}
	}
}

}  // namespace p3::data

namespace p3::data {

// 0x606CC0 (method +0x14 of a sea battle; steps): the battle's step.
// Automatic (both sides the AI's): the steps (0xFFF0 in a network game) and
// the carry [+0x130F] make (steps + carry) >> 4 sub-steps; with a human
// side at most 5 ([+0xAC8] says which). A battle over ([+0x5C]) counts down
// [+0x15C] by the steps and, when it reaches it, ends (method +0x4,
// 0x611740). Each sub-step: the teams' sums ([+0xA90] strength, [+0xA98]
// sailors, [+0xA9C] fighters, [+0xAA0] arms, [+0xAA8] battle speed: the
// largest; [+0xAB4], [+0xABC] the centre, or the area's) of the ships not
// out, fled nor taken (0x26); the counter [+0xA8C] (0 at 25); the end when
// over and [+0x15C] is 0 or above 0xF000 (method +0x1C, 0x60D299, with
// whether side 1 has a ship with sailors); the wind (0x6113C9); each ship's
// sub-step (method +0x0, 0x61A9E0); whether a side has lost (all its ships
// out or taken, unless the other plunders a taken one with cargo and has
// room) — the battle is over ([+0x5C]; [+0x15C] 0xFFFE the first time); a
// side all fled leaves the area when the other can't follow; then the
// scene's items that meet (0x6197F0).
//
// Not rewritten: what concerns a human side (the sea map and the battle
// window [+0x1E92], [+0x12E9], [+0x12EA], operation 0x96; part 7), the cannon balls (0x603130 for each of
// [+0x668], only from a human side's guns) and 0x608550 (with [+0x12E0] and
// [+0x66C], not run in the saves).
void Sailing::fn_606cc0(size_t b, uint16_t steps) {
	BattleGraph& g = save_.battles;
	auto u8 = [&](size_t o) { return g.get<uint8_t>(b, o); };
	if (steps == 0) {
		return;
	}
	if (u8(0x5C) != 0) {
		const uint16_t left = g.get<uint16_t>(b, 0x15C);
		if (left <= steps) {
			g.set<uint16_t>(b, 0x15C, 0);
			fn_611740(b, true);
			return;
		}
		g.set<uint16_t>(b, 0x15C, uint16_t(left - steps));
	} else {
		// A side still counting down ([+0x15C] by side): only the player's
		// count (the window, UI, left out), at most 5 steps; nothing else.
		bool counting = false;
		for (size_t i = 0; i < u8(0x558); ++i) {
			if (g.get<uint16_t>(b, 0x15C + 2 * i) > 0) {
				counting = true;
			}
		}
		if (u8(0x12E9) == 0 && counting) {
			const uint16_t most = steps > 5 ? 5 : steps;
			const uint32_t player = save_.globals.u32(0x6DFC14);
			for (size_t i = 0; i < u8(0x558); ++i) {
				const uint16_t left = g.get<uint16_t>(b, 0x15C + 2 * i);
				if (g.get<uint16_t>(b, 0x35A + 2 * i) == player && u8(0x5D + i) == 0 && left > 0 && left != 0xFFFF) {
					g.set<uint16_t>(b, 0x15C + 2 * i, left <= most ? 0xFFFE : uint16_t(left - most));
				}
			}
			return;
		}
		bool automatic = true;
		for (size_t i = 0; i < u8(0x558); ++i) {
			if (u8(0x5D + i) == 0) {
				automatic = false;  // A human side: part 7 (operation 0x96 and its countdown).
			}
		}
		if (automatic) {
			uint32_t t = steps;
			if (save_.globals.u32(0x6DFC18) != 0) {
				t = 0xFFF0;
			}
			g.set<uint8_t>(b, 0xAC8, 1);
			t = (t & 0xFFFF) + u8(0x130F);
			g.set<uint8_t>(b, 0x130F, uint8_t(t & 0xF));
			steps = uint16_t((t & 0xFFFF) >> 4);
		} else {
			if (steps > 5) {
				steps = 5;
			}
			g.set<uint8_t>(b, 0xAC8, 0);
		}
	}
	const size_t next = size_t(g.ptr(b, 0xC));
	auto nx = [&](uint16_t k) { return g.get<uint16_t>(next, 2 * size_t(k)); };
	for (uint16_t step = 0; step < steps; ++step) {
		for (const size_t o : {0xA90, 0xA94, 0xAA0, 0xAA4, 0xAB4, 0xAB8, 0xABC, 0xAC0, 0xAA8, 0xAAC}) {
			g.set<uint32_t>(b, o, 0);
		}
		for (const size_t o : {0xA98, 0xA9A, 0xA9C, 0xA9E}) {
			g.set<uint16_t>(b, o, 0);
		}
		for (size_t sd = 0; sd < 2; ++sd) {
			uint16_t n = 0;
			for (uint16_t k = g.get<uint16_t>(b, sd == 0 ? 0x10 : 0x65C); k != 0xFFFF; k = nx(k)) {
				const size_t e = battle_entry(b, k);
				if (g.get<uint8_t>(e, 0x12A) & 0x26) {
					continue;
				}
				g.set<uint32_t>(b, 0xA90 + 4 * sd, g.get<uint32_t>(b, 0xA90 + 4 * sd) + uint32_t(fn_62125b(e)));
				const ShipMemory& s = scene_ship(e);
				const uint16_t sailors = s.u16(0x40);
				if (sailors > g.get<uint16_t>(b, 0xA98 + 2 * sd)) {
					g.set<uint16_t>(b, 0xA98 + 2 * sd, sailors);
				}
				const uint16_t reserve = s.u16(0x154);
				const uint16_t fighting = uint16_t(sailors < reserve ? sailors + sailors : sailors + reserve);
				if (fighting > g.get<uint16_t>(b, 0xA9C + 2 * sd)) {
					g.set<uint16_t>(b, 0xA9C + 2 * sd, fighting);
				}
				if (s.u32(0x120) > g.get<uint32_t>(b, 0xAA0 + 4 * sd)) {
					g.set<uint32_t>(b, 0xAA0 + 4 * sd, s.u32(0x120));
				}
				const uint32_t speed = fn_612930(uint16_t(g.get<uint32_t>(e, 0x4)));
				if (g.get<uint32_t>(b, 0xAA8 + 4 * sd) < speed) {
					g.set<uint32_t>(b, 0xAA8 + 4 * sd, speed);
				}
				++n;
				g.set<int32_t>(b, 0xAB4 + 4 * sd, g.get<int32_t>(b, 0xAB4 + 4 * sd) + g.get<int16_t>(e, 0xA));
				g.set<int32_t>(b, 0xABC + 4 * sd, g.get<int32_t>(b, 0xABC + 4 * sd) + g.get<int16_t>(e, 0xE));
			}
			if (n != 0) {
				g.set<uint32_t>(b, 0xAB4 + 4 * sd, g.get<uint32_t>(b, 0xAB4 + 4 * sd) / n);
				g.set<uint32_t>(b, 0xABC + 4 * sd, g.get<uint32_t>(b, 0xABC + 4 * sd) / n);
			} else {
				g.set<uint32_t>(b, 0xAB4 + 4 * sd, 0x1100);
				g.set<uint32_t>(b, 0xABC + 4 * sd, 0xF02);
			}
		}
		const uint8_t count = uint8_t(u8(0xA8C) + 1);
		g.set<uint8_t>(b, 0xA8C, count * 40 >= 1000 ? 0 : count);
		if (u8(0x5C) == 1) {
			const uint16_t left = g.get<uint16_t>(b, 0x15C);
			if (left == 0 || left > 0xF000) {
				bool sailors = false;
				if (g.get<uint16_t>(b, 0x65A) > 0) {
					for (uint16_t k = g.get<uint16_t>(b, 0x65C); k != 0xFFFF; k = nx(k)) {
						const size_t e = battle_entry(b, k);
						if (!(g.get<uint8_t>(e, 0x12A) & 0x26) && scene_ship(e).u16(0x40) > 0) {
							sailors = true;
						}
					}
				}
				fn_60d299(b, sailors);
				return;
			}
		}
		fn_6113c9(b);
		// Per side: lost (all out or taken), plunders (a taken ship with
		// cargo), has room (2000), all fled.
		bool lost[2] = {true, true}, plunder[2] = {false, false}, room[2] = {false, false}, fled[2] = {true, true};
		for (size_t sd = 0; sd < 2; ++sd) {
			for (uint16_t k = g.get<uint16_t>(b, sd == 0 ? 0x10 : 0x65C); k != 0xFFFF; k = nx(k)) {
				const size_t e = battle_entry(b, k);
				const uint8_t fl = g.get<uint8_t>(e, 0x12A);
				if (!(fl & 0x6)) {
					if (!(fl & 0x20)) {
						fled[sd] = false;
					}
					const ShipMemory& s = scene_ship(e);
					if (fl & 0x40) {
						if (s.s32(0x118) > 0) {
							for (size_t w = 0; w < 24; ++w) {
								if (s.u32(0x54 + 4 * w) != 0) {
									plunder[sd] = true;
									break;
								}
							}
						}
					} else {
						lost[sd] = false;
						if (!(fl & 0x20) && s.s32(0x10) - s.s32(0x11C) - s.s32(0x118) >= 2000) {
							room[sd] = true;
						}
					}
				}
				fn_61a9e0(e, u8(0x670));
			}
		}
		if (plunder[0] && room[1]) {
			lost[0] = false;
		}
		if (plunder[1] && room[0]) {
			lost[1] = false;
		}
		bool over = lost[0] || lost[1];
		if (!lost[0] && !lost[1]) {
			// A side all fled leaves when the other can't follow (no room).
			auto leave = [&](size_t owner_at) {
				const uint8_t owner = u8(owner_at);
				const uint16_t head =
					g.get<uint16_t>(b, 0x35A) == owner ? g.get<uint16_t>(b, 0x65C) : g.get<uint16_t>(b, 0x10);
				for (uint16_t k = head; k < g.get<uint16_t>(b, 0x14); k = nx(k)) {
					const size_t e = battle_entry(b, k);
					if (!(g.get<uint8_t>(e, 0x12A) & 0xE)) {
						g.set<uint8_t>(e, 0x12A, uint8_t(g.get<uint8_t>(e, 0x12A) | 0x2));
						g.set<uint8_t>(b, 0xAC4, 0);
						g.set<uint16_t>(b, 0xAC6, k);
					}
				}
			};
			if (!room[1] && fled[0] && u8(0x5E) != 0) {
				if (u8(0x5C) != 0) {
					over = false;
				} else {
					leave(0x35C);
				}
			} else if (!room[0] && fled[1] && u8(0x5D) != 0) {
				if (u8(0x5C) != 0) {
					over = false;
				} else {
					leave(0x35A);
				}
			}
		}
		if (u8(0x5C) == 0 && over) {
			g.set<uint16_t>(b, 0x15C, 0xFFFE);
		}
		g.set<uint8_t>(b, 0x5C, over ? 1 : 0);
		fn_6197f0(b);
	}
}

}  // namespace p3::data
