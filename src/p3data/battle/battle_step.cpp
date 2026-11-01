// The step of a sea battle (operation 0x94 → method +0x14, 0x606CC0) and
// what it calls (docs/cloud/battles-2.md §4, docs/cloud/battles-2-605420.md).
// A scene's ship is a block of 0x164 bytes of BattleGraph: [+0x4] its ship,
// [+0x12C] its battle, [+0x138] its place, [+0x13D] its side, [+0x12A]
// flags (0x2 out, 0x4, 0x8, 0x20 fled, 0x40 taken, 0x80).

#include <array>
#include <cmath>

#include "p3data/sailing.hpp"

namespace p3::data {

namespace {

// Sailors and the reserve ([+0x40], [+0x154]): twice the sailors if they are
// fewer than the reserve, else both added (u16).
uint32_t fighters(const ShipMemory& s) {
	const uint16_t a = s.u16(0x40), r = s.u16(0x154);
	return uint16_t(a < r ? a + a : a + r);
}

// 0x67AA28: the arctangent for tan from 0 to 1 in 64 steps, in 256ths of a turn.
constexpr uint8_t kAtan[66] = {0, 1, 1, 2, 3, 3, 4, 4, 5, 6, 6, 7, 8, 8, 9, 9, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15,
	16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24, 25, 25, 25, 26, 26, 27, 27, 27, 28, 28,
	29, 29, 29, 30, 30, 30, 31, 31, 31, 32, 32, 0};

// 0x67ABB0: the sine of a quarter turn in 64 steps (65536 = 1).
constexpr int32_t kSin[65] = {0, 1608, 3216, 4821, 6424, 8022, 9616, 11204, 12785, 14359, 15924, 17479, 19024, 20557,
	22078, 23586, 25080, 26558, 28020, 29466, 30893, 32303, 33692, 35062, 36410, 37736, 39040, 40320, 41576, 42806,
	44011, 45190, 46341, 47464, 48559, 49624, 50660, 51665, 52639, 53581, 54491, 55368, 56212, 57022, 57798, 58538,
	59244, 59914, 60547, 61145, 61705, 62228, 62714, 63162, 63572, 63944, 64277, 64571, 64827, 65043, 65220, 65358,
	65457, 65516, 65536};

// The sine of a direction in 256ths of a turn, as the battle code reads kSin.
int32_t sine(uint32_t a) {
	a &= 0xFF;
	if (a < 0x80) {
		return a <= 0x40 ? kSin[a] : kSin[0x80 - a];
	}
	const uint32_t b = a - 0x80;
	return -(b <= 0x40 ? kSin[b] : kSin[0x100 - a]);
}
int32_t cosine(uint32_t a) {
	return sine(a + 0x40);
}

// 32-bit products as the game's imul.
int32_t mul(int32_t a, int32_t b) {
	return int32_t(uint32_t(a) * uint32_t(b));
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
