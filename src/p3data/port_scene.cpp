#include "p3data/port_scene.hpp"

#include <algorithm>
#include <cmath>

#include "p3data/battle/battle_math.hpp"
#include "p3data/sailing.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

using battle_math::cosine;
using battle_math::sine;

namespace {

int32_t mul(int32_t a, int32_t b) { return int32_t(uint32_t(a) * uint32_t(b)); }
int32_t high(int32_t a, int32_t b) { return int32_t((int64_t(a) * int64_t(b)) >> 32); }

// A division toward zero by 2^k as the game's cdq, and, add, sar.
int32_t div_pow2(int32_t v, int k) { return (v + int32_t((v >> 31) & ((1 << k) - 1))) >> k; }

// The progress's unit (0x84210843's product): ((length + 100) << 16) / 1984, so that 0x800 − 0x40 of it is the length
// + 100.
int32_t unit(int32_t length) {
	const int32_t v = int32_t(uint32_t(length + 100) << 16);
	const int32_t t = (high(v, int32_t(0x84210843u)) + v) >> 10;
	return t + int32_t(uint32_t(t) >> 31);
}

// The distance of a progress below 0x80 on a starting or ending stretch (0x6148D0, 0x615100): the unit · r² / 128,
// >> 17 (it gathers speed).
int32_t gathering(int32_t q, int32_t r) { return div_pow2(mul(mul(q, r), r), 7) >> 17; }

uint8_t direction(int32_t dx, int32_t dy) { return fn_6029b0(dx, dy); }

// The handlers' answer once the ship is placed.
constexpr int16_t kPlaced = -1;

// 0x6148D0 (going out, the last stretch of a path of three points or fewer): from p1 along p1 → p2, `r` from the
// start of the stretch.
int16_t out_last_short(const PortPath& path, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t r, PortPlace& o) {
	const uint8_t dir = direction(int32_t(x2) - x1, int32_t(y2) - y1);
	const int32_t q = unit(path.length);
	int32_t d;
	if (r < 0x80) {
		d = gathering(q, r);
		o.sails = uint8_t(div_pow2(r * 4 + 0x7F, 7));
	} else {
		d = mul(q, r - 0x40) >> 16;
		o.sails = 4;
	}
	o.x_frac = 0;
	o.x = int16_t(x1 + (mul(sine(dir), d) >> 16));
	o.y_frac = 0;
	o.y = int16_t(y1 - (mul(cosine(dir), d) >> 16));
	o.dir = dir;
	return kPlaced;
}

// 0x614B50 (going out, the last stretch of a path of four points or more): round the corner at p1 from p0 → p1 to p1
// → p2 while `r` is below 0x80 (the two headings blended, the place p1 + d1 along the new one − d2 along the old),
// then on along p1 → p2.
int16_t out_last_long(const PortPath& path, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x0, int16_t y0,
	int16_t r, PortPlace& o) {
	const uint8_t dir1 = direction(int32_t(x2) - x1, int32_t(y2) - y1);
	const int32_t q = unit(path.length);
	if (r < 0x80) {
		const int32_t a = r + 0x80;
		const int32_t b = 0x80 - r;
		const int32_t d1 = div_pow2(mul(mul(a, a) >> 2, q), 7) >> 16;
		const int32_t d2 = div_pow2(mul(mul(b, b) >> 2, q), 7) >> 16;
		const uint8_t dir0 = direction(int32_t(x1) - x0, int32_t(y1) - y0);
		uint8_t shown;
		const int32_t turn = (dir0 - dir1) & 0xFF;
		if (turn < 0x80) {
			shown = uint8_t(dir1 + (div_pow2(mul(turn, b), 8) & 0xFF));
		} else {
			shown = uint8_t(dir1 - (div_pow2(mul((dir1 - dir0) & 0xFF, b), 8) & 0xFF));
		}
		o.x_frac = 0;
		o.x = int16_t((mul(sine(dir1), d1) >> 16) - (mul(sine(dir0), d2) >> 16) + x1);
		o.y_frac = 0;
		o.y = int16_t((mul(cosine(dir0), d2) >> 16) - (mul(cosine(dir1), d1) >> 16) + y1);
		o.dir = shown;
		o.sails = 4;
		return kPlaced;
	}
	const int32_t d = mul(q, r) >> 16;
	o.x_frac = 0;
	o.x = int16_t((mul(sine(dir1), d) >> 16) + x1);
	o.y_frac = 0;
	o.y = int16_t(y1 - (mul(cosine(dir1), d) >> 16));
	o.dir = dir1;
	o.sails = 4;
	return kPlaced;
}

// 0x615100 (coming in, the last stretch of a path of three points or fewer): `r` before b on a → b, heading to b
// (below 0x80 slowing down, the sails taken in; from 4 to 3 the game plays a sound, 0x59A090: left to the sound).
int16_t in_last_short(const PortPath& path, int16_t xa, int16_t ya, int16_t xb, int16_t yb, int16_t r, PortPlace& o) {
	const uint8_t dir = direction(int32_t(xb) - xa, int32_t(yb) - ya);
	const int32_t q = unit(path.length);
	int32_t d;
	if (r < 0x80) {
		d = gathering(q, r);
		o.sails = uint8_t(div_pow2(r * 4, 7));
	} else {
		d = mul(q, r - 0x40) >> 16;
		o.sails = 4;
	}
	o.x_frac = 0;
	o.x = int16_t(xb - (mul(sine(dir), d) >> 16));
	o.y_frac = 0;
	o.y = int16_t((mul(cosine(dir), d) >> 16) + yb);
	o.dir = dir;
	return kPlaced;
}

// 0x615400 (coming in, the last stretch of a path of four points or more): `r` before p2 on p1 → p2, then round the
// corner at p2 to p2 → p3 while `r` is below 0x80 (the headings blended, the place p2 + d2 along the new one − d1
// along the old).
int16_t in_last_long(const PortPath& path, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3,
	int16_t r, PortPlace& o) {
	const uint8_t dir_a = direction(int32_t(x2) - x1, int32_t(y2) - y1);
	const int32_t q = unit(path.length);
	if (r < 0x80) {
		const int32_t a = r + 0x80;
		const int32_t b = 0x80 - r;
		const int32_t d1 = div_pow2(mul(mul(a, a) >> 2, q), 7) >> 16;
		const int32_t d2 = div_pow2(mul(mul(b, b) >> 2, q), 7) >> 16;
		const uint8_t dir_b = direction(int32_t(x3) - x2, int32_t(y3) - y2);
		uint8_t shown;
		const int32_t turn = (dir_b - dir_a) & 0xFF;
		if (turn < 0x80) {
			shown = uint8_t(dir_a + (div_pow2(mul(turn, b), 8) & 0xFF));
		} else {
			shown = uint8_t(dir_a - (div_pow2(mul((dir_a - dir_b) & 0xFF, b), 8) & 0xFF));
		}
		o.x_frac = 0;
		o.x = int16_t((mul(sine(dir_b), d2) >> 16) - (mul(sine(dir_a), d1) >> 16) + x2);
		o.y_frac = 0;
		o.y = int16_t((mul(cosine(dir_a), d1) >> 16) - (mul(cosine(dir_b), d2) >> 16) + y2);
		o.dir = shown;
		o.sails = 4;
		return kPlaced;
	}
	const int32_t d = mul(q, r) >> 16;
	o.x_frac = 0;
	o.x = int16_t(x2 - (mul(sine(dir_a), d) >> 16));
	o.y_frac = 0;
	o.y = int16_t((mul(cosine(dir_a), d) >> 16) + y2);
	o.dir = dir_a;
	o.sails = 4;
	return kPlaced;
}

// A stretch's length in the progress's units (0x615D22..0x615D69 and the like): its length (the square root
// truncated) << 16 / the unit, + 0x40, as a u16.
uint16_t stretch(int32_t q, int32_t dx, int32_t dy) {
	const int32_t len = int32_t(std::sqrt(double(mul(dx, dx) + mul(dy, dy))));
	return uint16_t((int32_t(uint32_t(len) << 16) / q) + 0x40);
}

// The corner's blend (0x615F93..0x615FFA and the like): from `from` turned towards `to` by (0x80 − t) / 256 of the turn
// the short way.
uint8_t blend(uint8_t from, uint8_t to, int32_t b) {
	const int32_t turn = (to - from) & 0xFF;
	if (turn < 0x80) {
		return uint8_t(from + (div_pow2(mul(b, turn), 8) & 0xFF));
	}
	return uint8_t(from - (div_pow2(mul((from - to) & 0xFF, b), 8) & 0xFF));
}

// 0x615CD0 (going out, the first stretch, p1 → p2 from the harbour's end): beyond it, what is left of `r`; else on it
// gathering speed from p1 (below 0x80, the sails set by fourths), and in its last 0x80 round the corner at p2 to p2 →
// p3.
int16_t out_first(const PortPath& path, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3,
	int16_t r, PortPlace& o) {
	const int32_t q = unit(path.length);
	const uint16_t seg = stretch(q, int32_t(x2) - x1, int32_t(y2) - y1);
	if (int32_t(r) > int32_t(seg)) {
		return int16_t(r - seg);
	}
	const uint8_t dir12 = direction(int32_t(x2) - x1, int32_t(y2) - y1);
	const int32_t t = int32_t(seg) - int32_t(uint16_t(r));
	if (t < 0x80) {
		const int32_t a = t + 0x80;
		const int32_t b = 0x80 - t;
		const int32_t d1 = div_pow2(mul(mul(a, a) >> 2, q), 7) >> 16;
		const int32_t d2 = div_pow2(mul(mul(b, b) >> 2, q), 7) >> 16;
		const uint8_t dir23 = direction(int32_t(x3) - x2, int32_t(y3) - y2);
		o.x_frac = 0;
		o.x = int16_t((mul(sine(dir23), d2) >> 16) - (mul(sine(dir12), d1) >> 16) + x2);
		o.y_frac = 0;
		o.y = int16_t((mul(cosine(dir12), d1) >> 16) - (mul(cosine(dir23), d2) >> 16) + y2);
		o.dir = blend(dir12, dir23, b);
		o.sails = 4;
		return kPlaced;
	}
	int32_t d;
	if (uint16_t(r) < 0x80) {
		const int32_t s = uint16_t(r);
		d = gathering(q, s);
		o.sails = uint8_t(div_pow2(s * 4 + 0x7F, 7));
	} else {
		d = mul(int32_t(uint16_t(r)) - 0x40, q) >> 16;
		o.sails = 4;
	}
	o.x_frac = 0;
	o.x = int16_t((mul(sine(dir12), d) >> 16) + x1);
	o.y_frac = 0;
	o.y = int16_t(y1 - (mul(cosine(dir12), d) >> 16));
	o.dir = dir12;
	return kPlaced;
}

// 0x616330 (coming in, the first stretch, a = p2 → b = p1 the harbour's end): beyond it, what is left of `r`; else in
// its first 0x80 round the corner at a from c → a (c = p3), then on to b, slowing down in its last 0x80 (the sails
// taken in by fourths; from 4 to 3 the game's sound).
int16_t in_first(const PortPath& path, int16_t xa, int16_t ya, int16_t xb, int16_t yb, int16_t xc, int16_t yc,
	int16_t r, PortPlace& o) {
	const int32_t q = unit(path.length);
	const int32_t len = int32_t(std::sqrt(double(mul(int32_t(yb) - ya, int32_t(yb) - ya) +
		mul(int32_t(xb) - xa, int32_t(xb) - xa))));
	const int32_t full = (int32_t(uint32_t(len) << 16) / q) + 0x40;
	const uint16_t seg = uint16_t(full);
	if (int32_t(r) > int32_t(seg)) {
		return int16_t(r - seg);
	}
	const uint8_t dir_ab = direction(int32_t(xb) - xa, int32_t(yb) - ya);
	const int32_t t = uint16_t(full - r);
	if (t < 0x80) {
		const int32_t a = t + 0x80;
		const int32_t b = 0x80 - t;
		const int32_t d1 = div_pow2(mul(mul(a, a) >> 2, q), 7) >> 16;
		const int32_t d2 = div_pow2(mul(mul(b, b) >> 2, q), 7) >> 16;
		const uint8_t dir_ca = direction(int32_t(xa) - xc, int32_t(ya) - yc);
		o.x_frac = 0;
		o.x = int16_t((mul(sine(dir_ab), d1) >> 16) - (mul(sine(dir_ca), d2) >> 16) + xa);
		o.y_frac = 0;
		o.y = int16_t((mul(cosine(dir_ca), d2) >> 16) - (mul(cosine(dir_ab), d1) >> 16) + ya);
		o.dir = blend(dir_ab, dir_ca, b);
		o.sails = 4;
		return kPlaced;
	}
	const int32_t left = int32_t(seg) - t;
	int32_t d;
	if (left < 0x80) {
		d = (mul(int32_t(seg) - 0x40, q) >> 16) - gathering(q, left);
		o.sails = uint8_t(div_pow2(left * 4, 7));
	} else {
		d = mul(q, t) >> 16;
		o.sails = 4;
	}
	o.x_frac = 0;
	o.x = int16_t((mul(sine(dir_ab), d) >> 16) + xa);
	o.y_frac = 0;
	o.y = int16_t(ya - (mul(cosine(dir_ab), d) >> 16));
	o.dir = dir_ab;
	return kPlaced;
}

// 0x6169E0 (a stretch a → b in the middle of the path, either way; c before a, d after b): beyond it, what is left of
// `r`; else, at `pos` from a (`r` going out, the stretch less `r` coming in): in its first 0x80 the corner at a from
// c → a, in its last 0x80 the corner at b to b → d, between them on a → b. The sails set.
int16_t middle(const PortPath& path, int16_t xa, int16_t ya, int16_t xb, int16_t yb, int16_t xc, int16_t yc, int16_t xd,
	int16_t yd, int16_t r, bool out, PortPlace& o) {
	const int32_t q = unit(path.length);
	const int32_t dx = int32_t(xb) - xa;
	const int32_t dy = int32_t(yb) - ya;
	const int32_t len = int32_t(std::sqrt(double(mul(dy, dy) + mul(dx, dx))));
	const int32_t full = int32_t(uint32_t(len) << 16) / q;
	const int32_t seg = uint16_t(full);
	if (int32_t(r) > seg) {
		return int16_t(r - seg);
	}
	const uint8_t dir_ab = direction(dx, dy);
	const int32_t pos = uint16_t(out ? int32_t(r) : full - r);
	o.sails = 4;
	if (pos < 0x80) {
		const int32_t a = pos + 0x80;
		const int32_t b = 0x80 - pos;
		const int32_t d1 = div_pow2(mul(mul(a, a) >> 2, q), 7) >> 16;
		const int32_t d2 = div_pow2(mul(mul(b, b) >> 2, q), 7) >> 16;
		const uint8_t dir_ca = direction(int32_t(xa) - xc, int32_t(ya) - yc);
		o.x_frac = 0;
		o.x = int16_t((mul(sine(dir_ab), d1) >> 16) - (mul(sine(dir_ca), d2) >> 16) + xa);
		o.y_frac = 0;
		o.y = int16_t((mul(cosine(dir_ca), d2) >> 16) - (mul(cosine(dir_ab), d1) >> 16) + ya);
		o.dir = blend(dir_ab, dir_ca, b);
		return kPlaced;
	}
	const int32_t rem = seg - pos;
	if (rem < 0x80) {
		const int32_t a = rem + 0x80;
		const int32_t b = 0x80 - rem;
		const int32_t d1 = div_pow2(mul(mul(a, a) >> 2, q), 7) >> 16;
		const int32_t d2 = div_pow2(mul(mul(b, b) >> 2, q), 7) >> 16;
		const uint8_t dir_bd = direction(int32_t(xd) - xb, int32_t(yd) - yb);
		o.x_frac = 0;
		o.x = int16_t((mul(sine(dir_bd), d2) >> 16) - (mul(sine(dir_ab), d1) >> 16) + xb);
		o.y_frac = 0;
		o.y = int16_t((mul(cosine(dir_ab), d1) >> 16) - (mul(cosine(dir_bd), d2) >> 16) + yb);
		o.dir = blend(dir_ab, dir_bd, b);
		return kPlaced;
	}
	const int32_t d = mul(q, pos) >> 16;
	o.x_frac = 0;
	o.x = int16_t((mul(sine(dir_ab), d) >> 16) + xa);
	o.y_frac = 0;
	o.y = int16_t(ya - (mul(cosine(dir_ab), d) >> 16));
	o.dir = dir_ab;
	return kPlaced;
}

}  // namespace

int32_t port_scale_y(int32_t y) {
	const int32_t v = y * 34;
	const int32_t t = high(v, 0x2E8BA2E9) >> 2;
	return t + int32_t(uint32_t(t) >> 31);
}

PortPath port_path(const std::vector<std::array<int, 2>>& points, bool coming_in) {
	PortPath p;
	const size_t n = points.size();
	if (n < 2 || n > 7) {
		return p;
	}
	for (size_t k = 0; k < n; ++k) {
		p.x[n - k] = int16_t(points[k][0]);
		p.y[n - k] = int16_t(points[k][1]);
	}
	p.flags[n] = 1;
	p.flags[1] = 2;
	if (coming_in) {
		p.x[0] = int16_t(2 * p.x[1] - p.x[2]);
		p.y[0] = int16_t(2 * p.y[1] - p.y[2]);
	} else {
		p.x[0] = p.x[2];
		p.y[0] = p.y[2];
	}
	p.count = uint8_t(n + 1);
	for (size_t k = 0; k <= n; ++k) {
		p.y[k] = int16_t(port_scale_y(p.y[k]));
	}
	for (size_t k = 0; k + 1 < n; ++k) {
		const int32_t dx = points[k + 1][0] - points[k][0];
		const int32_t dy = port_scale_y(points[k + 1][1] - points[k][1]);
		p.length += int32_t(std::sqrt(double(mul(dx, dx) + mul(dy, dy))));
	}
	return p;
}

void port_place_on_path(const PortPath& path, int16_t r, bool out, PortPlace& o) {
	const auto& x = path.x;
	const auto& y = path.y;
	const auto& f = path.flags;
	const int n = path.count;  // the points + 1: they are 1..n − 1, the extra one 0
	if (n < 3 || n > 8) {
		return;
	}
	// The stretches from the harbour's end out (the extra point stands before the first; the stretches whose flags
	// are both 2, or the second 2, are none of a port's: their branches, 0x6173EA and 0x618038, aren't read).
	for (int i = 1; i < n - 2; ++i) {
		if (f[size_t(i)] == 2 && f[size_t(i + 1)] == 2) {
			return;
		}
		if (f[size_t(i)] != 2 && f[size_t(i + 1)] == 2) {
			return;
		}
		const size_t k = size_t(i);
		if (f[k] == 2) {
			r = out ? out_first(path, x[k], y[k], x[k + 1], y[k + 1], x[k + 2], y[k + 2], r, o)
					: in_first(path, x[k + 1], y[k + 1], x[k], y[k], x[k + 2], y[k + 2], r, o);
		} else {
			r = out ? middle(path, x[k], y[k], x[k + 1], y[k + 1], x[k - 1], y[k - 1], x[k + 2], y[k + 2], r, true, o)
					: middle(path, x[k + 1], y[k + 1], x[k], y[k], x[k + 2], y[k + 2], x[k - 1], y[k - 1], r, false, o);
		}
		if (r < 0) {
			return;
		}
	}
	// The last stretch, to the sea's end and beyond it.
	if (f[size_t(n - 1)] != 1) {
		return;
	}
	const size_t a = size_t(n - 2);
	const size_t b = size_t(n - 1);
	if (out) {
		if (n < 4) {
			out_last_short(path, x[a], y[a], x[b], y[b], r, o);
		} else {
			out_last_long(path, x[a], y[a], x[b], y[b], x[a - 1], y[a - 1], r, o);
		}
	} else if (n < 4) {
		in_last_short(path, x[b], y[b], x[a], y[a], r, o);
	} else {
		in_last_long(path, x[b], y[b], x[a], y[a], x[a - 1], y[a - 1], r, o);
	}
}

void port_place_docked(const std::vector<std::array<int, 2>>& points, PortPlace& o) {
	// 0x613B78: at the path's last point, heading along its last stretch, the sails furled.
	const size_t n = points.size();
	if (n < 2) {
		return;
	}
	const auto& last = points[n - 1];
	const auto& before = points[n - 2];
	o.x_frac = 0;
	o.x = int16_t(last[0]);
	o.y_frac = 0;
	o.y = int16_t(port_scale_y(last[1]));
	o.dir = direction(last[0] - before[0], port_scale_y(last[1] - before[1]));
	o.sails = 0;
}

void port_place(uint8_t state, const std::vector<std::array<int, 2>>& points, const PortPath& path, PortPlace& o) {
	switch (state) {
	case 0x00: port_place_docked(points, o); return;
	case 0x01:
	case 0x09: port_place_on_path(path, int16_t(0x800 - path.progress), false, o); return;
	case 0x08:
	case 0x10: port_place_on_path(path, path.progress, true, o); return;
	case 0x18: port_place_on_path(path, int16_t(std::min<int>(path.progress, 0x400)), true, o); return;
	default: return;
	}
}

namespace {

// What the scene reads of the world (the ships' object 0x6DD7A0 and the globals).
constexpr uint32_t kShipCount = 0x6DD894;
constexpr uint32_t kConvoyCount = 0x6DD896;
constexpr uint32_t kDockedHead = 0x6DD888;
constexpr uint32_t kConvoyHead = 0x6DD88C;
constexpr uint32_t kPlayer = 0x6DFC14;
constexpr uint32_t kMsPerTick = 0x6DFBC4;
constexpr uint32_t kBattlePace = 0x6DFC1C;
constexpr uint32_t kNetwork = 0x6DFC18;
constexpr uint32_t kWorldTick = 0x6DE4B4;  // u32, 256 a day
constexpr uint32_t kTownNumbers = 0x6DE4B8;  // u8 per town: its number in StadtDaten.ini
constexpr uint32_t kTownTable = 0x6DDB90;  // StadtDaten.ini, 0x34 bytes per number ([+0x31] AusrichtungDesMeeres)
// P2.cfg's [OPTIONS] ORLOGONLY ([*0x6CBDF0 + 0x18C], 1 in the game's): of a convoy only its leader is shown.
constexpr bool kLeaderOnly = true;

struct World {
	const SaveGame& save;
	uint16_t ships() const { return save.globals.u16(kShipCount); }
	uint16_t convoys() const { return save.globals.u16(kConvoyCount); }
	bool ship_ok(uint32_t i) const { return i < ships() && i < save.ships.size(); }
	bool convoy_ok(uint32_t c) const { return c < convoys() && c < save.convoys.size(); }
	const ShipMemory& ship(uint32_t i) const { return save.ships[i].memory; }
	uint16_t convoy_of(uint32_t i) const { return ship(i).u16(0x8); }
	// The state that counts (its convoy's [+0x12] in one, else its [+0x134]).
	uint16_t state(uint32_t i) const {
		const uint16_t c = convoy_of(i);
		return convoy_ok(c) ? save.convoys[c].u16(0x12) : ship(i).u16(0x134);
	}
	// Its counter of coming in and going out (its convoy's [+0x16], else its [+0x138]).
	uint16_t counter(uint32_t i) const {
		const uint16_t c = convoy_of(i);
		return convoy_ok(c) ? save.convoys[c].u16(0x16) : ship(i).u16(0x138);
	}
	// The convoy's first ship (0x6132C0, 0x6135C0, 0x613920 start from it).
	uint16_t first(uint32_t i) const {
		const uint16_t c = convoy_of(i);
		return convoy_ok(c) ? save.convoys[c].u16(0xA) : uint16_t(i);
	}
};

// The scene's own clock divisions (0x614080, 0x613DA0): ms · 32 / the tick's ms, or / 3375 with the battles' pace.
uint32_t progress_since(const SaveGame& save, uint32_t elapsed, bool capped, uint32_t ms_per_tick) {
	if (save.globals.u32(kBattlePace) != 0) {
		if (capped && elapsed > 0xD2F) {
			elapsed = 0xD2F;
		}
		return (elapsed << 5) / 3375;
	}
	const uint32_t ms = std::max<uint32_t>(ms_per_tick ? ms_per_tick : save.globals.u32(kMsPerTick), 1);
	if (capped && elapsed > ms) {
		elapsed = ms;
	}
	return (elapsed << 5) / ms;
}

bool docked_state(uint16_t s) { return s <= 1 || (s >= 4 && s <= 7) || s == 0xA || s == 0xC || s == 0xD; }

}  // namespace

std::array<uint16_t, 255>& PortScene::table() {
	auto it = table_.find(town_);
	if (it == table_.end()) {
		std::array<uint16_t, 255> empty;
		empty.fill(0xFFFF);
		it = table_.emplace(town_, empty).first;
	}
	return it->second;
}

int PortScene::find(uint16_t ship) const {
	for (uint16_t slot : list_) {
		if (slots_[slot]->ship == ship) {
			return slot;
		}
	}
	return -1;
}

// 0x619330: the slot's scene ship freed and out of the list.
void PortScene::remove(uint16_t slot) {
	if (slot >= slots_.size() || !slots_[slot]) {
		return;
	}
	if (slots_[slot]->figure != 0) {
		gone.push_back(slots_[slot]->figure);
	}
	slots_[slot].reset();
	list_.erase(std::remove(list_.begin(), list_.end(), slot), list_.end());
}

// 0x618A30: the ship's slot, or a free one for it (the player's the first from 0, the others' the last, and these
// only while fewer than 3/4 are taken; of a convoy only its leader), a scene ship made on it; 0xFFFF none.
uint16_t PortScene::take_slot(const SaveGame& save, uint16_t ship) {
	const World w{save};
	const int found = find(ship);
	if (found >= 0) {
		return uint16_t(found);
	}
	const size_t paths = nodes_.size();
	if (list_.size() == paths) {
		return 0xFFFF;
	}
	const uint32_t owner = w.ship(ship).u8(0x0);
	const uint32_t player = save.globals.u32(kPlayer);
	if (owner != player && list_.size() * 4 >= paths * 3) {
		return 0xFFFF;
	}
	const uint16_t c = w.convoy_of(ship);
	if (w.convoy_ok(c) && kLeaderOnly && (save.convoys[c].u8(0x14) & 2) && save.convoys[c].u16(0x10) != ship) {
		return 0xFFFF;
	}
	// (In a network game another human's ships take the first too, 0x618B3A: none here.)
	size_t slot = 0;
	if (owner == player && save.globals.u32(kNetwork) == 0) {
		while (slot < paths && slots_[slot]) {
			++slot;
		}
		if (slot == paths) {
			return 0xFFFF;
		}
	} else {
		size_t k = paths;
		while (k > 0 && slots_[k - 1]) {
			--k;
		}
		if (k == 0) {
			return 0xFFFF;
		}
		slot = k - 1;
	}
	slots_[slot] = std::make_unique<PortShip>();
	slots_[slot]->ship = ship;
	list_.insert(list_.begin(), uint16_t(slot));
	return uint16_t(slot);
}

// 0x6132C0: a ship (and its convoy's others) starting to come in: a slot, its path copied coming in, state 1, its
// progress its counter · 32.
void PortScene::coming_in(const SaveGame& save, uint16_t ship) {
	const World w{save};
	if (!w.ship_ok(ship)) {
		return;
	}
	uint16_t s = w.first(ship);
	while (w.ship_ok(s)) {
		if (w.ship(s).u8(0x39) != town_) {
			return;
		}
		int slot = find(s);
		if (slot < 0 && list_.size() < nodes_.size()) {
			const uint16_t t = take_slot(save, s);
			if (t != 0xFFFF) {
				table()[t] = s;
				slot = t;
			}
		}
		if (slot >= 0) {
			paths_[size_t(slot)] = port_path(nodes_[size_t(slot)], true);
			slots_[size_t(slot)]->state = 1;
			paths_[size_t(slot)].progress = int16_t(w.counter(s) << 5);
		} else if (list_.size() >= nodes_.size()) {
			return;
		}
		if (!w.convoy_ok(w.convoy_of(s))) {
			return;
		}
		s = w.ship(s).u16(0x6);
	}
}

// 0x6135C0: a ship (and its convoy's others) starting to go out: its slot (a new one only if `create`), its path
// copied going out, state 8, its progress its counter · 32; leaving the quay (not already going out or coming in) in
// the town shown, the sound.
void PortScene::going_out(const SaveGame& save, uint16_t ship, bool create) {
	const World w{save};
	if (!w.ship_ok(ship)) {
		return;
	}
	uint16_t s = w.first(ship);
	while (w.ship_ok(s)) {
		if (w.ship(s).u8(0x39) != town_) {
			return;
		}
		int slot = find(s);
		if (slot >= 0) {
			const PortShip& p = *slots_[size_t(slot)];
			if (p.state != 8 && p.state != 0x18 && p.state != 1) {
				sounds.push_back({p.place.x, int(p.place.y) * 22 / 34, 0});
			}
		} else if (create && list_.size() < nodes_.size()) {
			const uint16_t t = take_slot(save, s);
			if (t != 0xFFFF) {
				table()[t] = s;
				slot = t;
			}
		} else if (create) {
			return;
		}
		if (slot >= 0) {
			paths_[size_t(slot)] = port_path(nodes_[size_t(slot)], false);
			slots_[size_t(slot)]->state = 8;
			paths_[size_t(slot)].progress = int16_t(w.counter(s) << 5);
		}
		if (!w.convoy_ok(w.convoy_of(s))) {
			return;
		}
		s = w.ship(s).u16(0x6);
	}
}

// 0x613920: a ship (and its convoy's others) docked: a slot, state 0, its progress its counter · 32.
void PortScene::docked(const SaveGame& save, uint16_t ship) {
	const World w{save};
	if (!w.ship_ok(ship)) {
		return;
	}
	uint16_t s = w.first(ship);
	while (w.ship_ok(s)) {
		if (w.ship(s).u8(0x39) != town_) {
			return;
		}
		int slot = find(s);
		if (slot < 0) {
			const uint16_t t = take_slot(save, s);
			if (t == 0xFFFF) {
				return;
			}
			table()[t] = s;
			slot = t;
		}
		slots_[size_t(slot)]->state = 0;
		paths_[size_t(slot)].progress = int16_t(w.counter(s) << 5);
		if (!w.convoy_ok(w.convoy_of(s))) {
			return;
		}
		s = w.ship(s).u16(0x6);
	}
}

void PortScene::show(const SaveGame& save, int town, const std::vector<std::vector<std::array<int, 2>>>& paths,
	uint32_t now) {
	const World w{save};
	for (uint16_t slot : std::vector<uint16_t>(list_)) {
		remove(slot);
	}
	nodes_ = paths;
	slots_.clear();
	slots_.resize(nodes_.size());
	paths_.assign(nodes_.size(), PortPath());
	list_.clear();
	last_state_.clear();
	town_ = town;
	for (size_t k = 0; k < nodes_.size(); ++k) {
		paths_[k] = port_path(nodes_[k], true);
	}
	if (town_ >= 0x80 || size_t(town_) >= save.towns.size()) {
		town_ = 0x80;
		return;
	}
	// The ships the town's table keeps on their slots, while still in it (and of a convoy its leader only).
	std::array<uint16_t, 255>& t = table();
	for (size_t slot = 0; slot < 255; ++slot) {
		const uint16_t s = t[slot];
		if (!w.ship_ok(s) || w.ship(s).u8(0x39) != town_) {
			t[slot] = 0xFFFF;
			continue;
		}
		const uint16_t c = w.convoy_of(s);
		if (w.convoy_ok(c) && kLeaderOnly && (save.convoys[c].u8(0x14) & 2) && save.convoys[c].u16(0x10) != s) {
			t[slot] = 0xFFFF;
			continue;
		}
		if (slot >= nodes_.size() || find(s) >= 0) {
			t[slot] = 0xFFFF;
			continue;
		}
		slots_[slot] = std::make_unique<PortShip>();
		slots_[slot]->ship = s;
		list_.insert(list_.begin(), uint16_t(slot));
	}
	// Every ship of the port by its state (0x61461F..0x61485D): the docked list (by [+0x6]; none, nothing more is
	// walked, a quirk kept), then the town's shipyard list [0x81E], then the convoys (the list *0x6DD88C, then the
	// town's [0x820]), each convoy's ships from its [+0xA]; while slots are left.
	uint16_t s = save.globals.u16(kDockedHead);
	uint16_t convoy = save.globals.u16(kConvoyHead);
	bool yard = false;
	bool town_convoys = false;
	const TownMemory& tm = save.towns[size_t(town_)].memory;
	while (w.ship_ok(s) && list_.size() < nodes_.size()) {
		if (w.ship(s).u8(0x39) == town_) {
			const uint16_t st = w.state(s);
			if (docked_state(st) && st != 0xA) {
				const uint16_t slot = take_slot(save, s);
				if (slot != 0xFFFF) {
					table()[slot] = s;
					slots_[slot]->state = 0;
				}
			} else if (st == 2) {
				going_out(save, s, true);
			} else if (st == 3) {
				coming_in(save, s);
			} else if (st == 8 || st == 9) {
				going_out(save, s, true);
				const int slot = find(s);
				if (slot >= 0) {
					slots_[size_t(slot)]->state = 0x18;
				}
			}
		}
		s = w.ship(s).u16(0x6);
		if (w.ship_ok(s)) {
			continue;
		}
		if (!yard) {
			yard = true;
			s = tm.u16(0x81E);
		}
		if (w.ship_ok(s)) {
			continue;
		}
		if (w.convoy_ok(convoy)) {
			s = save.convoys[convoy].u16(0xA);
			convoy = save.convoys[convoy].u16(0x8);
			if (!w.convoy_ok(convoy) && !town_convoys) {
				town_convoys = true;
				convoy = tm.u16(0x820);
			}
		}
	}
	scene_tick(save, now);
	for (uint32_t i = 0; i < w.ships() && i < save.ships.size(); ++i) {
		if (w.ship(i).u8(0x39) == town_) {
			last_state_[uint16_t(i)] = w.state(i);
		}
	}
	frame(save, now);
}

void PortScene::hide() {
	for (uint16_t slot : std::vector<uint16_t>(list_)) {
		remove(slot);
	}
	town_ = 0x80;
	last_state_.clear();
}

void PortScene::wind(const SaveGame& save, uint8_t& dir, uint8_t& strength) {
	if (town_ < 0x80) {
		const uint8_t number = save.globals.u8(kTownNumbers + uint32_t(town_));
		port_wind(wind_, save.globals.u32(kWorldTick), town_, save.globals.u8(kTownTable + 0x34 * uint32_t(number) + 0x31));
	}
	dir = wind_.dir;
	strength = wind_.strength;
}

void port_wind(PortWind& wind, uint32_t tick, int town, uint8_t sea) {
	const uint32_t e = (tick - wind.tick) >> 2;
	if (e == 0) {
		return;
	}
	// The day's byte plus the town's, · 0x33, from 0xE3, in a half turn; then away from the sea's side.
	uint8_t d = uint8_t((0xE3 - uint8_t(uint8_t(uint8_t(tick >> 8) + uint8_t(town)) * 0x33)) & 0x7F);
	if (int(d) + 0x20 > int(sea) * 32) {
		d = uint8_t(d + 0x40);
		if (int(d) > (int(sea) + 3) * 32) {
			d = uint8_t(d + 0x40);
		}
	}
	uint8_t s = uint8_t(((d - 1) & 3) + 4);
	if (e < 0x10) {
		int16_t dd = int16_t(int16_t(d) - int16_t(wind.dir));
		if (dd > 0x80) {
			dd = int16_t(dd - 0x100);
		} else if (dd < -0x80) {
			dd = int16_t(dd + 0x100);
		}
		const int16_t turn = int16_t(e * 8);
		if (dd > turn) {
			d = uint8_t(wind.dir + e * 8);
		} else if (-dd > turn) {
			d = uint8_t(wind.dir - e * 8);
		}
		const int16_t ds = int16_t(int16_t(s) - int16_t(wind.strength));
		if (ds > int16_t(e)) {
			s = uint8_t(wind.strength + e);
		} else if (-ds > int16_t(e)) {
			s = uint8_t(wind.strength - e);
		}
	}
	wind.dir = d;
	wind.strength = s;
	wind.tick = tick & ~3u;
}

void PortScene::tick(const SaveGame& save, uint32_t now) {
	if (town_ >= 0x80) {
		return;
	}
	const World w{save};
	// What the engine tells the scene during the tick (see the class's note): each of the town's ships that started
	// coming in (0x6132C0), docked (0x613920) or started going out (0x6135C0, «create» 0).
	std::map<uint16_t, uint16_t> now_states;
	for (uint32_t i = 0; i < w.ships() && i < save.ships.size(); ++i) {
		if (w.ship(i).u8(0x39) != town_ || w.ship(i).u16(0x134) >= 0x17) {
			continue;
		}
		const uint16_t st = w.state(i);
		now_states[uint16_t(i)] = st;
		const auto before = last_state_.find(uint16_t(i));
		const bool was = before != last_state_.end();
		if (st == 3 && (!was || before->second != 3)) {
			coming_in(save, uint16_t(i));
		} else if (st == 2 && (!was || before->second != 2)) {
			going_out(save, uint16_t(i), false);
		} else if (docked_state(st) && (!was || before->second == 3)) {
			docked(save, uint16_t(i));
		}
	}
	last_state_ = std::move(now_states);
	scene_tick(save, now);
}

// 0x613DA0: each ship's base from its progress and counter, and its scene state by its ship's.
void PortScene::scene_tick(const SaveGame& save, uint32_t now) {
	const World w{save};
	const uint32_t elapsed = now - tick_ms_;
	tick_ms_ = now;
	if (town_ >= 0x80) {
		return;
	}
	bool held = false;
	for (uint16_t slot : std::vector<uint16_t>(list_)) {
		if (!slots_[slot]) {
			continue;
		}
		PortShip& p = *slots_[slot];
		PortPath& path = paths_[slot];
		path.progress = int16_t(progress_since(save, elapsed, false, ms_per_tick_) + uint16_t(p.base));
		const uint16_t s = p.ship;
		const uint16_t st = w.ship_ok(s) ? w.state(s) : 0xFFFF;
		const uint16_t c = w.ship_ok(s) ? w.convoy_of(s) : 0xFFFF;
		const uint32_t counter = uint32_t(w.convoy_ok(c) && st != 9 ? save.convoys[c].u16(0x16) : w.ship_ok(s) ? w.ship(s).u16(0x138) : 0) << 5;
		const int32_t diff = int32_t(path.progress) - int32_t(counter);
		if (std::abs(diff) > 0x20) {
			p.base = int16_t(counter);
		} else {
			p.base = int16_t(uint32_t(int32_t(path.progress) * 7 + int32_t(counter)) >> 3);
		}
		if (st <= 0xD && docked_state(st)) {
			p.state = 0;
		} else if (st == 2 || st == 0xB) {
			going_out(save, s, false);
			if (slots_[slot]) {
				slots_[slot]->state = 8;
			}
		} else if (st == 3) {
			coming_in(save, s);
		} else if (st == 8 || st == 9) {
			going_out(save, s, false);
			held = true;
		} else {
			table()[slot] = 0xFFFF;
			remove(slot);
		}
	}
	if (held) {
		for (uint16_t slot : list_) {
			const uint16_t st = w.ship_ok(slots_[slot]->ship) ? w.state(slots_[slot]->ship) : 0xFFFF;
			if (st == 8 || st == 9) {
				slots_[slot]->state = 0x18;
			}
		}
	}
}

void PortScene::frame(const SaveGame& save, uint32_t now) {
	if (town_ >= 0x80) {
		return;
	}
	const uint32_t elapsed = save.globals.u32(kNetwork) != 0 ? 0 : now - tick_ms_;
	for (uint16_t slot : std::vector<uint16_t>(list_)) {
		if (slots_[slot] && slots_[slot]->state == 0x11) {
			table()[slot] = 0xFFFF;
			remove(slot);
		}
	}
	for (uint16_t slot : list_) {
		PortShip& p = *slots_[slot];
		PortPath& path = paths_[slot];
		int16_t progress = int16_t(progress_since(save, elapsed, true, ms_per_tick_) + uint16_t(p.base));
		progress = std::max<int16_t>(0, std::min<int16_t>(progress, 0x800));
		path.progress = progress;
		const PortPlace before = p.place;
		port_place(p.state, nodes_[slot], path, p.place);
		// Coming in, its sails taken in from 4 to 3 (0x615295, 0x61689C): the sound, at its place before.
		if ((p.state == 1 || p.state == 9) && before.sails == 4 && p.place.sails == 3) {
			sounds.push_back({before.x, int(before.y) * 22 / 34, 1});
		}
	}
}

}  // namespace p3::data
