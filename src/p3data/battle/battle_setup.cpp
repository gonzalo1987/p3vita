// The setup of a sea battle (part of 0x605420 and what it calls; piece 3 of
// docs/cloud/battles.md, docs/cloud/battles-2.md §4): the scene's items
// (0x619450, 0x61A1D0), the scene's ships (0x61A4E0), their strength
// (0x62125B, 0x61A750) and the Hanse fleet's convoys (0x503AC0).

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include "p3data/battle/battle_math.hpp"
#include "p3data/game_texts.hpp"
#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

using battle_math::kGunA;
using battle_math::kGunB;
using battle_math::kGunTime;

namespace {

constexpr uint32_t kSceneShipVtable = 0x67B0D4;
// 0x67AB80: half the size of a ship of each type, in the battle's units.
constexpr uint16_t kShipHalf[8] = {55, 66, 70, 84, 102, 121, 121, 148};
// 0x67ACFC and 0x67AD20: the shapes (width, height) of the rocks and islets.
constexpr uint16_t kRock[5][2] = {{2, 2}, {3, 2}, {4, 4}, {6, 4}, {6, 5}};
constexpr uint16_t kIslet[3][2] = {{4, 4}, {4, 4}, {6, 4}};
// __ftol (0x639C28) of a double: truncated; NaN and out of range give the
// x87's integer indefinite (0x8000000000000000), whose low 32 bits are 0.
int32_t game_ftol(double v) {
	if (!(v >= -9.2e18 && v <= 9.2e18)) {
		return 0;
	}
	return int32_t(int64_t(v));
}

int32_t s16(uint32_t v) {
	return int16_t(uint16_t(v));
}

}  // namespace

// 0x61A1D0 (on the scene): a free item. When none is free (and there are
// fewer than 0xFFBF pairs) every array grows by 0x20 items (realloc, in the
// game's order), a new page of each of the three page arrays [+0x48],
// [+0x4C], [+0x50], and the new items are linked in the free list [+0x24].
uint16_t Sailing::fn_61a1d0(size_t scene) {
	BattleGraph& g = save_.battles;
	auto u16 = [&](size_t o) { return g.get<uint16_t>(scene, o); };
	if (u16(0x20) != 0xFFFF || u16(0x58) >= 0xFFBF) {
		return u16(0x20);
	}
	g.set<uint16_t>(scene, 0x20, u16(0x56));
	g.set<uint16_t>(scene, 0x56, uint16_t(u16(0x56) + 0x20));
	g.set<uint16_t>(scene, 0x5A, uint16_t(u16(0x5A) + 1));
	g.set<uint16_t>(scene, 0x58, uint16_t(u16(0x58) + 0x40));
	const size_t cap = u16(0x56);
	const size_t pairs = u16(0x58);
	const size_t pages = u16(0x5A);
	g.realloc_at(scene, 0x18, cap);
	g.realloc_at(scene, 0x1C, 2 * cap);
	g.realloc_at(scene, 0x24, 2 * cap);
	for (size_t o : {0x28, 0x2C, 0x30, 0x38, 0x3C, 0x40}) {
		g.realloc_at(scene, o, 2 * pairs);
	}
	for (size_t j = 0; j + 1 < pages; ++j) {
		for (size_t o : {0x48, 0x4C, 0x50}) {
			const int list = g.ptr(scene, o);
			const size_t page = g.realloc_at(size_t(list), 4 * j, 4 * cap);
			for (size_t e = u16(0x20); e < cap; ++e) {
				g.set<uint32_t>(page, 4 * e, 0);
			}
		}
	}
	for (size_t o : {0x48, 0x4C, 0x50}) {
		g.realloc_at(scene, o, 4 * pages);
	}
	for (size_t o : {0x48, 0x4C, 0x50}) {
		const size_t list = size_t(g.ptr(scene, o));
		const size_t page = g.alloc(4 * cap);  // realloc(NULL, ...)
		g.set_ptr(list, 4 * (pages - 1), int(page));
	}
	const size_t free_list = size_t(g.ptr(scene, 0x24));
	for (size_t e = cap - 0x20; e + 1 < cap; ++e) {
		g.set<uint16_t>(free_list, 2 * e, uint16_t(e + 1));
	}
	g.set<uint16_t>(free_list, 2 * (cap - 1), 0xFFFF);
	return u16(0x20);
}

// 0x619450 (on the scene; a kind, a byte, four u16): a new item (0x61A1D0),
// taken out of the free list, with its kind [+0x18], its byte [+0x1C], the
// pairs [+0x28] (a2, a4) and [+0x38] (a3, a5), at the end of the two lists
// of pairs ([+0x2C]/[+0x30] with the tail [+0x34], [+0x3C]/[+0x40] with
// [+0x44]); [+0x54] the items used. Returns the item.
uint16_t Sailing::fn_619450(size_t scene, uint8_t a0, uint8_t a1, uint16_t a2, uint16_t a3, uint16_t a4, uint16_t a5) {
	BattleGraph& g = save_.battles;
	const uint16_t i = fn_61a1d0(scene);
	auto arr = [&](size_t o) { return size_t(g.ptr(scene, o)); };
	g.set<uint16_t>(scene, 0x20, g.get<uint16_t>(arr(0x24), 2 * size_t(g.get<uint16_t>(scene, 0x20))));
	g.set<uint8_t>(arr(0x18), i, a0);
	g.set<uint16_t>(arr(0x1C), 2 * size_t(i), a1);
	g.set<uint16_t>(arr(0x28), 4 * size_t(i), a2);
	g.set<uint16_t>(arr(0x28), 4 * size_t(i) + 2, a4);
	g.set<uint16_t>(arr(0x38), 4 * size_t(i), a3);
	g.set<uint16_t>(arr(0x38), 4 * size_t(i) + 2, a5);
	const uint16_t two = uint16_t(2 * i);
	for (const auto& [first, second, tail] : {std::array<size_t, 3>{0x2C, 0x30, 0x34}, std::array<size_t, 3>{0x3C, 0x40, 0x44}}) {
		const uint16_t last = g.get<uint16_t>(scene, tail);
		g.set<uint16_t>(arr(first), 4 * size_t(i) + 2, last);
		g.set<uint16_t>(arr(second), 4 * size_t(i) + 2, two);
		if (last != 0xFFFF) {
			g.set<uint16_t>(arr(second), 2 * size_t(last), uint16_t(two + 1));
		}
		g.set<uint16_t>(arr(first), 4 * size_t(i), uint16_t(two + 1));
		g.set<uint16_t>(arr(second), 4 * size_t(i), 0xFFFF);
		g.set<uint16_t>(scene, tail, two);
	}
	if (i >= g.get<uint16_t>(scene, 0x54)) {
		g.set<uint16_t>(scene, 0x54, uint16_t(i + 1));
	}
	return i;
}

// 0x61A4E0 (a scene's ship, 0x164 bytes: the ship, its battle, its place in
// it, its side): the vtable 0x67B0D4, the ship's index [+0x4], the battle
// [+0x12C], the place [+0x138], the side [+0x13D], [+0x142], its sailors
// [+0x140] and hull [+0x144], and [+0x13C] 1 when its guns (the 0x18 bytes of
// [+0x13C] of the ship, the kinds under 6) are mostly of the slow kinds.
void Sailing::fn_61a4e0(size_t entry, uint16_t ship, size_t battle, uint16_t index, uint8_t side) {
	BattleGraph& g = save_.battles;
	auto u8 = [&](size_t o, uint8_t v) { g.set<uint8_t>(entry, o, v); };
	auto u16 = [&](size_t o, uint16_t v) { g.set<uint16_t>(entry, o, v); };
	auto u32 = [&](size_t o, uint32_t v) { g.set<uint32_t>(entry, o, v); };
	const ShipMemory& s = save_.ships[ship].memory;
	u8(0x10, 0x80);
	u8(0x127, 0);
	g.set_ptr(entry, 0x12C, int(battle));
	u16(0x138, index);
	u8(0x26, 4);
	u8(0x148, 4);
	u32(0x4, ship);
	u32(0x0, kSceneShipVtable);
	u16(0x128, 0xFFFF);
	u8(0x12A, 0);
	u8(0x130, 0);
	u8(0x131, 0);
	u8(0x132, 0);
	u32(0x134, 0);
	u32(0x1C, 0);
	u8(0x149, 0);
	u16(0x13A, 0);
	u8(0x13D, side);
	u8(0x142, side);
	u8(0x13E, 0);
	u16(0x140, s.u16(0x40));
	u32(0x144, s.u32(0x18));
	u8(0x14A, 1);
	u16(0x8, 0);
	u16(0xC, 0);
	u16(0xA, 0);
	u16(0xE, 0);
	u8(0x11, 0);
	u8(0x10, 0x80);
	u8(0x127, 1);
	u8(0x162, 0);
	for (size_t o = 0x27; o < 0x127; ++o) {
		u8(o, 0);
	}
	int32_t all = 0, quick = 0;
	for (size_t k = 0; k < 0x18; ++k) {
		const uint8_t t = s.u8(0x13C + k);
		if (t < 6) {
			const int32_t a = int32_t(kGunA[t]) * kGunB[t];
			all += a;
			quick += a - (480 * a) / (6 * int32_t(kGunTime[t]));
		}
	}
	u8(0x13C, 301 * all > 605 * quick ? 1 : 0);
	u32(0x14C, 0xFFFFFFFF);
	u32(0x150, 0);
	u8(0x160, 0);
	u32(0x15C, 0);
	u32(0x158, 0);
	u8(0x154, 0xFF);
	u8(0x161, 0xC);
}

// 0x62125B (on a scene's ship): its strength, arms · hull / 1024, more with
// a captain ([+0xB] / 850), 5 % less when the crew is tired ([+0x3F] < 1),
// up to 10 % more when rested (2 < [+0x3F], at most 4).
int32_t Sailing::fn_62125b(size_t entry) const {
	const BattleGraph& g = save_.battles;
	const ShipMemory& s = save_.ships[g.get<uint32_t>(entry, 0x4)].memory;
	int32_t v = int32_t(uint32_t(s.s32(0x120)) * uint32_t(s.s32(0x18))) >> 10;
	if (s.u16(0x42) < save_.captain_count()) {
		v += int32_t(uint32_t(save_.captains[s.u16(0x42)].u8(0xB)) * uint32_t(v)) / 850;
	}
	const int8_t rest = s.s8(0x3F);
	if (rest < 1) {
		v += v / -20;
	} else if (rest > 2) {
		v += int32_t(uint32_t(std::min<int8_t>(rest, 4) - 2) * uint32_t(v)) / 20;
	}
	return v;
}

// 0x61A750 (on a scene's ship, another): whether it is stronger.
bool Sailing::fn_61a750(size_t entry, size_t other) const {
	return fn_62125b(entry) > fn_62125b(other);
}

// 0x503AC0 (on a convoy, its index): whether the convoy is the Hanse fleet of
// its event 0x34 ([+0x2] the event, whose [+0x16] is the convoy).
bool Sailing::fn_503ac0(uint16_t convoy) const {
	const uint16_t e = save_.convoys[convoy].u16(0x2);
	if (e >= save_.globals.u16(0x6DD748) || e >= save_.events.size()) {
		return false;
	}
	return save_.events[e].u16(0x6) == 0x34 && save_.events[e].u16(0x16) == convoy;
}

// 0x6197A0 (on the scene; an item, x1, y1, x2, y2): the item's box.
void Sailing::fn_6197a0(size_t scene, uint16_t item, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
	BattleGraph& g = save_.battles;
	const size_t xs = size_t(g.ptr(scene, 0x28));
	const size_t ys = size_t(g.ptr(scene, 0x38));
	g.set<uint16_t>(xs, 4 * size_t(item), x1);
	g.set<uint16_t>(xs, 4 * size_t(item) + 2, x2);
	g.set<uint16_t>(ys, 4 * size_t(item), y1);
	g.set<uint16_t>(ys, 4 * size_t(item) + 2, y2);
}

// 0x6079D0 (method +0x2C of a sea battle): the two sides in formation. The
// direction is from the battle's middle to the first ship of side A (/8192,
// unit 256), the columns √(ships − 1) + 1; each ship at its place with some
// noise (the battle's generator, ±0x40 · the direction / 256, with a logical
// shift as the game), its [+0x11] from its owner's [+0x1] and its box in the
// scene (0x6197A0, ± the half size of its type + 0x168). Side A from (0x1100,
// 0xF02) + 250/256 of the direction, side D from − 300/256; a strong ship
// followed by one with no strength starts a new block further back (side D
// marks the ships from there with [+0x12A] |= 1).
void Sailing::fn_6079d0(size_t b) {
	BattleGraph& g = save_.battles;
	const size_t ships = size_t(g.ptr(b, 0x4));
	const size_t boxes = size_t(g.ptr(b, 0xA78));
	auto entry = [&](size_t i) { return size_t(g.ptr(ships, 4 * i)); };
	auto ship = [&](size_t e) -> const ShipMemory& { return save_.ships[g.get<uint32_t>(e, 0x4)].memory; };
	const ShipMemory& first = ship(entry(0));
	const int32_t dx = (first.s32(0x1C) - g.get<int32_t>(b, 0xAD4)) >> 13;
	const int32_t dy = (first.s32(0x20) - g.get<int32_t>(b, 0xAD8)) >> 13;
	const int32_t r = game_ftol(std::sqrt(double(int32_t(uint32_t(dy) * uint32_t(dy) + uint32_t(dx) * uint32_t(dx)))));
	int32_t ux = 0xB5, uy = 0xB5;
	if (r != 0) {
		ux = int32_t(uint32_t(dx) << 8) / r;
		uy = int32_t(uint32_t(dy) << 8) / r;
	}
	auto mul = [](int32_t a, int32_t k) { return int32_t(uint32_t(a) * uint32_t(k)); };
	auto noise = [&](int32_t u) { return (uint32_t(mul(int32_t(battle_rand(g, b) & 0x7F) - 0x40, u))) >> 8; };
	auto place = [&](size_t i, int32_t x, int32_t y, bool mark) {
		const size_t e = entry(i);
		g.set<uint16_t>(e, 0xA, uint16_t(noise(uy) + uint32_t(x)));
		g.set<uint16_t>(e, 0xE, uint16_t(uint32_t(y) - noise(ux)));
		const uint8_t owner = ship(e).u8(0x1);
		g.set<uint8_t>(e, 0x11, uint8_t((owner - 5) << 4));
		g.set<uint16_t>(e, 0x24, g.get<uint8_t>(e, 0x11));
		if (mark) {
			g.set<uint8_t>(e, 0x12A, uint8_t(g.get<uint8_t>(e, 0x12A) | 1));
		}
		const uint32_t half = kShipHalf[ship(e).u8(0xE) & 7];
		const uint32_t ex = g.get<uint16_t>(e, 0xA);
		const uint32_t ey = g.get<uint16_t>(e, 0xE);
		fn_6197a0(b, g.get<uint16_t>(boxes, 2 * i), uint16_t(ex - half - 0x168), uint16_t(ey - half - 0x168),
			uint16_t(ex + half + 0x168), uint16_t(ey + half + 0x168));
	};
	auto shift = [&](uint32_t cols, int32_t& x, int32_t& y) {
		if (uint16_t(cols) > 1) {
			const int32_t k = int32_t(cols & 0xFFFF) - 1;
			x -= mul(mul(k, uy), 200) >> 9;
			y += mul(mul(k, ux), 200) >> 9;
		}
	};
	// Side A.
	const uint32_t na = g.get<uint16_t>(b, 0x658);
	const uint32_t cols_a = uint32_t(game_ftol(std::sqrt(double(int32_t(na) - 1))) + 1);
	int32_t x = (mul(ux, 250) >> 8) + 0x1100;
	int32_t y = (mul(uy, 250) >> 8) + 0xF02;
	shift(cols_a, x, y);
	int32_t ax = mul(ux, 300), ay = mul(uy, 300);
	for (uint32_t i = 0; i < na; ++i) {
		place(i, x, y, false);
		if (i + 1 >= na) {
			continue;
		}
		if (fn_62125b(entry(i)) > 0 && fn_62125b(entry(i + 1)) == 0) {
			ax += mul(ux, 400);
			ay += mul(uy, 400);
			x = (ax >> 8) + 0x1100;
			y = (ay >> 8) + 0xF02;
			shift(cols_a, x, y);
		} else if ((i + 1) % (cols_a & 0xFFFF) == 0) {
			ax += mul(ux, 200);
			ay += mul(uy, 200);
			x = (ax >> 8) + 0x1100;
			y = (ay >> 8) + 0xF02;
			shift(cols_a, x, y);
		} else {
			x += mul(uy, 200) >> 8;
			y -= mul(ux, 200) >> 8;
		}
	}
	// Side D.
	const uint32_t nd = g.get<uint16_t>(b, 0x65A);
	const uint32_t cols_d = uint32_t(game_ftol(std::sqrt(double(int32_t(nd) - 1))) + 1);
	ax = mul(ux, 300);
	ay = mul(uy, 300);
	x = 0x1100 - (ax >> 8);
	y = 0xF02 - (ay >> 8);
	shift(cols_d, x, y);
	bool mark = false;
	for (uint32_t j = 0; j < nd; ++j) {
		place(na + j, x, y, mark);
		if (j + 1 >= nd) {
			continue;
		}
		if (fn_62125b(entry(na + j)) > 0 && fn_62125b(entry(na + j + 1)) == 0) {
			ax += mul(ux, 400);
			ay += mul(uy, 400);
			x = 0x1100 - (ax >> 8);
			y = 0xF02 - (ay >> 8);
			mark = true;
			shift(cols_d, x, y);
		} else if ((j + 1) % (cols_d & 0xFFFF) == 0) {
			ax += mul(ux, 200);
			ay += mul(uy, 200);
			x = 0x1100 - (ax >> 8);
			y = 0xF02 - (ay >> 8);
			shift(cols_d, x, y);
		} else {
			x += mul(uy, 200) >> 8;
			y -= mul(ux, 200) >> 8;
		}
	}
}

// 0x60F479 (on a sea battle): the rocks ([+0x872 + 8k]: x, y, width, height)
// and the islets ([+0x672 + 8k]) of the battle, up to 0x40 of each: how many
// from three draws of the battle's generator; each of a shape drawn at
// random (0x67ACFC, 0x67AD20) at a random place, not on another (a rock: the
// rocks before; an islet: every rock and the islets before) nor on a ship's
// box (/34 in x, /17 in y); after 256 failures one fewer. Each placed one
// is two items of the scene (kinds 2 and 4 for a rock, 3 and 5 for an islet,
// with its number; the second 200 larger each way), with coordinates times
// half the town view's tile width (u16 [*0x6E51AC + 0x510] >> 1, sar: 17;
// SaveGame::TownView. Before 2026-11-07 the emulator had no town view and
// both took 0). The game keeps it in eax's low word over the view's address,
// but the coordinates are words: only the low word counts.
// The unused entries get x = 0xFFFF. (The UI calls 0x5944E0/0x594640 when
// the battle is the one shown are left out.)
void Sailing::fn_60f479(size_t b) {
	BattleGraph& g = save_.battles;
	const uint32_t half = uint32_t(int16_t(uint16_t(save_.town_view.tile[0])) >> 1);
	auto draw3 = [&](uint32_t m) {
		const uint32_t r1 = battle_rand(g, b);
		const uint32_t r2 = battle_rand(g, b);
		const uint32_t r3 = battle_rand(g, b);
		return r2 % m + r1 % m + r3 % m;
	};
	auto x_of = [&](size_t base, int k) { return s16(g.get<uint16_t>(b, base + 8 * size_t(k))); };
	auto y_of = [&](size_t base, int k) { return s16(g.get<uint16_t>(b, base + 8 * size_t(k) + 2)); };
	auto free_of = [&](size_t base, int from, int to, int32_t x, int32_t y, bool skip_empty) {
		bool ok = true;
		for (int j = from; j < to; ++j) {
			const int32_t xj = x_of(base, j);
			if (skip_empty && xj <= 0) {
				continue;
			}
			const int32_t yj = y_of(base, j);
			bool apart = true;
			if (x > xj + 8 || y > yj + 16 || x + 8 < xj) {
				apart = false;
			}
			if (y + 16 >= yj && apart) {
				ok = false;
			}
		}
		return ok;
	};
	auto off_ships = [&](int32_t x, int32_t y, bool ok) {
		const size_t xs = size_t(g.ptr(b, 0x28));
		const size_t ys = size_t(g.ptr(b, 0x38));
		const size_t boxes = size_t(g.ptr(b, 0xA78));
		const int32_t n = g.get<uint16_t>(b, 0x14);
		for (int32_t i = 0; i < n; ++i) {
			const uint32_t two = uint32_t(g.get<uint16_t>(boxes, 2 * size_t(i))) * 2;
			const int32_t x1 = s16(g.get<uint16_t>(xs, 2 * two)), y1 = s16(g.get<uint16_t>(ys, 2 * two));
			const int32_t x2 = s16(g.get<uint16_t>(xs, 2 * two + 2)), y2 = s16(g.get<uint16_t>(ys, 2 * two + 2));
			bool apart = true;
			if (x > x2 / 34 || y > y2 / 17 || x < x1 / 34) {
				apart = false;
			}
			if (y >= y1 / 17 && apart) {
				ok = false;
			}
		}
		return ok;
	};
	auto items = [&](size_t base, int k, uint8_t kind) {
		const uint32_t x = g.get<uint16_t>(b, base + 8 * size_t(k));
		const uint32_t y = g.get<uint16_t>(b, base + 8 * size_t(k) + 2);
		const uint32_t w = g.get<uint16_t>(b, base + 8 * size_t(k) + 4);
		const uint32_t h = g.get<uint16_t>(b, base + 8 * size_t(k) + 6);
		const uint32_t c = 2 * x + (y & 1);
		const uint16_t item = fn_619450(b, kind, uint8_t(k), uint16_t((c - w + 1) * half),
			uint16_t((y - w - h + 1) * half), uint16_t((c + h + 1) * half), uint16_t((y + 1) * half));
		const size_t xs = size_t(g.ptr(b, 0x28));
		const size_t ys = size_t(g.ptr(b, 0x38));
		const uint16_t x1 = g.get<uint16_t>(xs, 4 * size_t(item)), x2 = g.get<uint16_t>(xs, 4 * size_t(item) + 2);
		const uint16_t y1 = g.get<uint16_t>(ys, 4 * size_t(item)), y2 = g.get<uint16_t>(ys, 4 * size_t(item) + 2);
		fn_619450(b, uint8_t(kind + 2), uint8_t(k), uint16_t(x1 - 200), uint16_t(y1 - 200), uint16_t(x2 + 200),
			uint16_t(y2 + 200));
	};
	auto place = [&](size_t base, const uint16_t (*shapes)[2], uint32_t kinds, int k) {
		const uint32_t shape = battle_rand(g, b) % kinds;
		const uint16_t w = uint16_t(shapes[shape][0] - 1);
		const uint16_t h = uint16_t(shapes[shape][1] - 1);
		const uint32_t r = battle_rand(g, b);
		const uint32_t wx = (uint32_t(w) + 1) >> 1;
		const uint32_t hx = (uint32_t(h) + 1) >> 1;
		g.set<uint16_t>(b, base + 8 * size_t(k), uint16_t(r % (0x100 - hx - wx) + wx));
		const uint32_t r2 = battle_rand(g, b);
		g.set<uint16_t>(b, base + 8 * size_t(k) + 2, uint16_t(r2 % (0x1C4u - w - h) + h + w));
		g.set<uint16_t>(b, base + 8 * size_t(k) + 4, uint16_t(w + 1));
		g.set<uint16_t>(b, base + 8 * size_t(k) + 6, uint16_t(h + 1));
	};
	// The rocks.
	int32_t count = s16(draw3(65));
	count = count < 0x60 ? 0x40 - count : count - 0x80;
	if (int16_t(count) < 0) {
		count = 0;
	}
	uint8_t retries = 0;
	for (int k = 0; k < 0x40; ++k) {
		if (k >= int16_t(count)) {
			g.set<uint16_t>(b, 0x872 + 8 * size_t(k), 0xFFFF);
			continue;
		}
		place(0x872, kRock, 5, k);
		const int32_t x = x_of(0x872, k), y = y_of(0x872, k);
		bool ok = free_of(0x872, 0, k, x, y, false);
		ok = off_ships(x, y, ok);
		if (ok) {
			items(0x872, k, 2);
			retries = 0;
		} else if (++retries != 0) {
			--k;
		} else {
			--count;
			--k;
		}
	}
	// The islets.
	count = s16(draw3(25));
	count = count < 0x24 ? 0x30 - count : count - 0x18;
	retries = 0;
	for (int k = 0; k < 0x40; ++k) {
		if (k >= int16_t(count)) {
			g.set<uint16_t>(b, 0x672 + 8 * size_t(k), 0xFFFF);
			continue;
		}
		place(0x672, kIslet, 3, k);
		const int32_t x = x_of(0x672, k), y = y_of(0x672, k);
		bool ok = free_of(0x872, 0, 0x40, x, y, true);
		ok = ok && free_of(0x672, 0, k, x, y, false) ? ok : false;
		ok = off_ships(x, y, ok);
		if (ok) {
			items(0x672, k, 3);
		} else if (++retries != 0) {
			--k;
		} else {
			--count;
			--k;
		}
	}
}

// 0x608230 (method +0x18 of a sea battle, arg): if [+0x5C] the end
// (method +0x1C, 0x60D299, with 1); else, with arg, the
// formation (method +0x2C, 0x6079D0), the human player's sides (the town
// view and the battle shown: UI, part 7), and the rocks and islets (0x60F479).
void Sailing::fn_608230(size_t b, bool arg) {
	BattleGraph& g = save_.battles;
	if (g.get<uint8_t>(b, 0x5C) != 0) {
		fn_60d299(b, true);
		return;
	}
	if (!arg) {
		return;
	}
	fn_6079d0(b);
	fn_60f479(b);
}

// 0x60659B..0x606AF5 (in 0x605420): the battle's message to the local
// player, who has the human side `side` (team [+0x559 + side]) of a battle
// of ships a (side 0) and d (side 1). With team 0 the player attacks: «%s
// "%s" is attacking %s "%s" %s.» with «Your convoy» (the player's convoy
// [+0x65E] of two ships or more: its first ship [+0xA] has a next [+0x6]) or
// «Your <a's type>», a's name, «the convoy» (the same test on [+0x660]) or d's
// type as the one taken (0x6A171C), d's name, and its owner: when team 1 is
// the pirates' (bit 2 of [+0xAD2]), «of the pirate %s %s» ([+0x1E90],
// [+0x1E91]) if [+0x35C] is a pirate with a captain (0xFF) and the guide of
// convoy [+0x660] has a captain, else «of an unknown pirate»; otherwise «of
// %s %s», the names of d's owner ([+0x0]). With team 1 the player is
// attacked: «%s "%s" is being attacked by %s "%s" %s.» with «Your convoy»
// ([+0x660]) or «Your <type>» of the side's ship, its name, «the convoy»
// ([+0x65E]) or a's type as the winner (0x6A175C), a's name, and its owner
// (bit 1, [+0x35A] and convoy [+0x65E] for the pirate; the merchant
// [+0x35A] himself). The game doesn't check the convoys' first ships or the
// pirate's guide against the number of ships: here none then.
std::string Sailing::attack_message(size_t b, uint16_t a, uint16_t d, size_t side) const {
	const BattleGraph& g = save_.battles;
	const uint32_t ships = save_.globals.u16(global::kShipCount);
	auto raw = [&](GameText t, uint32_t plus = 0) { return texts_->raw(GameText(int(t) + int(plus))); };
	auto name = [&](uint16_t s) {
		std::string out;
		for (size_t i = 0x160; i < 0x180 && save_.ships[s].memory.u8(i) != 0; ++i) {
			out += char(save_.ships[s].memory.u8(i));
		}
		return out;
	};
	auto type = [&](uint16_t s) { return uint32_t(save_.ships[s].memory.u8(0xE) & 3); };
	auto convoy = [&](size_t o) {
		const uint16_t c = g.get<uint16_t>(b, o);
		if (c >= save_.convoy_count()) {
			return false;
		}
		const uint16_t first = save_.convoys[c].u16(0xA);
		return first < ships && save_.ships[first].memory.u16(0x6) < ships;
	};
	auto pirate = [&](size_t owner, size_t convoy_field) {
		if (g.get<uint16_t>(b, owner) == 0xFF) {
			const uint16_t c = g.get<uint16_t>(b, convoy_field);
			const uint16_t guide = c < save_.convoy_count() ? save_.convoys[c].u16(0x10) : 0xFFFF;
			if (guide < ships && save_.ships[guide].memory.u16(0x42) < save_.captain_count()) {
				return GameTexts::sprintf_raw(raw(GameText::BattleOfPirate),
					{game_first_name(save_, g.get<uint8_t>(b, 0x1E90)), game_family_name(save_, g.get<uint8_t>(b, 0x1E91))});
			}
		}
		return raw(GameText::BattleOfAnUnknownPirate);
	};
	auto merchant = [&](uint32_t owner) {
		if (owner >= save_.merchants.size()) {
			return GameTexts::sprintf_raw(raw(GameText::BattleOfMerchant), {});
		}
		const MerchantMemory& m = save_.merchants[owner].memory;
		return GameTexts::sprintf_raw(raw(GameText::BattleOfMerchant),
			{game_first_name(save_, m.u8(0x1C)), game_family_name(save_, m.u8(0x1D))});
	};
	const uint8_t pirates = g.get<uint8_t>(b, 0xAD2);
	if (g.get<uint8_t>(b, 0x559 + side) == 0) {
		const std::string owner = (pirates & 2) ? pirate(0x35C, 0x660) : merchant(save_.ships[d].memory.u8(0x0));
		return GameTexts::sprintf_raw(raw(GameText::BattleAttacking),
			{convoy(0x65E) ? raw(GameText::BattleYourConvoy) : raw(GameText::BattleYourSnaikka, type(a)), name(a),
				convoy(0x660) ? raw(GameText::BattleTheConvoyAttacked) : raw(GameText::BattleTakenSnaikka, type(d)), name(d),
				owner});
	}
	const std::string owner = (pirates & 1) ? pirate(0x35A, 0x65E) : merchant(g.get<uint16_t>(b, 0x35A));
	const uint16_t mine = side == 0 ? a : d;
	return GameTexts::sprintf_raw(raw(GameText::BattleAttacked),
		{convoy(0x660) ? raw(GameText::BattleYourConvoy) : raw(GameText::BattleYourSnaikka, type(mine)), name(mine),
			convoy(0x65E) ? raw(GameText::BattleTheConvoyAttacking) : raw(GameText::BattleWinnerSnaikka, type(a)), name(a),
			owner});
}

// 0x605420 (on a sea battle: ship A, ship D, a byte, a flag; its automatic
// path, docs/cloud/battles-2-605420.md): the owners and sides, the middle,
// the two convoys, which side is human; the ships of each side counted and
// put in the scene (0x619450 twice each, 0x61A4E0), strongest first
// (0x61A750); the wind from the battle's generator and the storms near;
// the pirates' captain; the steps of each side; and, when automatic, method
// +0x18 (0x608230). Not rewritten: the human sides' messages and window
// (part 7), the case of a storm (method +0x4, the end) and the error log.
void Sailing::fn_605420(size_t b, uint16_t a, uint16_t d, uint8_t c, uint32_t flag) {
	BattleGraph& g = save_.battles;
	GlobalMemory& gl = save_.globals;
	const uint16_t count = uint16_t(save_.ships.size());
	const uint16_t convoys = save_.convoy_count();
	const uint16_t merchants = uint16_t(save_.merchant_count);
	auto u8 = [&](size_t o) { return g.get<uint8_t>(b, o); };
	auto u16 = [&](size_t o) { return g.get<uint16_t>(b, o); };
	auto set8 = [&](size_t o, uint8_t v) { g.set<uint8_t>(b, o, v); };
	auto set16 = [&](size_t o, uint16_t v) { g.set<uint16_t>(b, o, v); };
	auto set32 = [&](size_t o, uint32_t v) { g.set<uint32_t>(b, o, v); };
	auto human_merchant = [&](uint16_t owner) { return owner < merchants && save_.merchants[owner].memory.u16(0x8) == 0; };
	const ShipMemory& sa = save_.ships[a].memory;
	const ShipMemory& sd = save_.ships[d].memory;
	set8(0x12E8, c);
	set8(0x558, 2);
	set16(0x35A, sa.u8(0x0));
	set16(0x35C, sd.u8(0x0));
	set8(0x559, 0);
	set8(0x55A, 1);
	if (sa.u16(0x134) == 0x15 || sd.u16(0x134) == 0x15 || (sa.u8(0) == 0xFF && sd.u8(0) == 0xFF)) {
		return;  // 0x606C28: a storm or two pirates: method +0x4 (the end), not rewritten yet.
	}
	set8(0xAD2, (u16(0x35A) != 0xFF && sa.u8(0x15C) == 0) ? 0 : 1);
	if (!(u16(0x35C) != 0xFF && sd.u8(0x15C) == 0)) {
		set8(0xAD2, uint8_t(u8(0xAD2) | 2));
	}
	set32(0xAD4, uint32_t((sd.s32(0x1C) + sa.s32(0x1C)) >> 1));
	set32(0xAD8, uint32_t((sd.s32(0x20) + sa.s32(0x20)) >> 1));
	set16(0x65E, sa.u16(0x8));
	set16(0x660, sd.u16(0x8));
	set32(0xADC, (uint32_t(u16(0xAD8)) << 16) + u16(0xAD4));
	if (u16(0x65E) < convoys && fn_503ac0(u16(0x65E))) {
		set8(0x5D, 1);
		set16(0x35A, 0xFE);
	} else {
		set8(0x5D, human_merchant(u16(0x35A)) ? 0 : 1);
	}
	const uint16_t dstate = u16(0x660) < convoys ? save_.convoys[u16(0x660)].u16(0x12) : 0;
	if (dstate == 9 || dstate == 0x16) {
		set8(0x5E, 1);
		set16(0x35C, 0xFE);
	} else {
		set8(0x5E, human_merchant(u16(0x35C)) ? 0 : 1);
	}
	if (gl.u32(0x6DFC18) != 0 && !u8(0x5D) && !u8(0x5E) && u16(0x35A) != u16(0x35C)) {
		set8(0x5E, 1);
		set8(0x5D, 1);
		set8(0xAC8, 1);
	}
	if (u8(0x5D) && u8(0x5E)) {
		set8(0xAC8, 1);
	} else {
		set8(0xAC8, 0);
		// The ticker lines to the human sides (0x6056F7, 0x60572E): «The X is
		// attacking.» (kind 0xE, the attacker's guide) to side 0's merchant,
		// «The X is being attacked.» (0xB, the defender's) to side 1's.
		if (!u8(0x5D)) {
			send_ticker(save_, u16(0x35A), 0xE, a);
		}
		if (!u8(0x5E) && u16(0x35A) != u16(0x35C)) {
			send_ticker(save_, u16(0x35C), 0xB, d);
		}
		if (gl.u32(0x6DFC18) == 0) {
			fn_612c90(false);
		}
	}
	set8(0xAB0, 1);
	set8(0xAB1, (u16(0x35C) != 0xFF || u16(0x35A) != 0xFE) ? 1 : 0xFF);
	// 0x1E92: a ship of the player (or of a human in a network game) is in
	// the battle (human_ship_in_battle): the sea map is told (0x457C90) and
	// it goes back to 0 only when its window *0x6CBDD8 exists: never in the
	// simulation (the app does it), as in the oracle.
	set8(0x1E92, human_ship_in_battle(b) ? 1 : 0);
	const uint16_t ships_count = count;
	// The sides: the ships of convoy b65E (or ship A alone) for A.
	auto weaken = [&]() {
		const uint32_t t = u16(0x1E94);
		const int32_t q = (int32_t(1000 - (t & 0x3FF)) * int32_t(1000 - ((1000 - 0x3B6) & 0xFFFF)) + 500) / 1000;
		set16(0x1E94, uint16_t((((t >> 10) & 0xFF) << 10) + uint32_t(1000 - q)));
	};
	uint32_t na = 0, nd = 0;
	if (u16(0x65E) >= convoys) {
		set32(0xA84, a);
		set16(0x658, sa.s32(0x120) > 0 ? 1 : 0);
		na = 1;
	} else {
		set16(0x658, 0);
		uint32_t i = save_.convoys[u16(0x65E)].u16(0xA);
		set32(0xA84, i);
		for (; i < ships_count; i = save_.ships[i].memory.u16(0x6)) {
			const ShipMemory& s = save_.ships[i].memory;
			if (u16(0x35A) == 0xFE) {
				set16(0x658, uint16_t(u16(0x658) + 1));
				++na;
			} else if (u16(0x35A) == s.u8(0)) {
				if (s.s32(0x120) > 0) {
					set16(0x658, uint16_t(u16(0x658) + 1));
				}
				++na;
			} else if (u8(0xAD2) == 2) {
				weaken();
			}
		}
	}
	if (u16(0x660) >= convoys) {
		set32(0xA88, d);
		set16(0x65A, sd.s32(0x120) > 0 ? 1 : 0);
		nd = 1;
	} else {
		set16(0x65A, 0);
		uint32_t i = save_.convoys[u16(0x660)].u16(0xA);
		set32(0xA88, i);
		for (; i < ships_count; i = save_.ships[i].memory.u16(0x6)) {
			const ShipMemory& s = save_.ships[i].memory;
			if (u16(0x35C) == 0xFE) {
				set16(0x65A, uint16_t(u16(0x65A) + 1));
				++nd;
			} else if (u16(0x35C) == s.u8(0)) {
				if (s.s32(0x120) > 0) {
					set16(0x65A, uint16_t(u16(0x65A) + 1));
				}
				++nd;
			} else if (u8(0xAD2) == 1) {
				weaken();
			}
		}
	}
	if (uint8_t(flag) != 0) {
		nd = uint32_t(nd - u16(0x65A));
		set16(0x65A, 0);
	}
	set16(0x658, uint16_t(na));
	set16(0x65A, uint16_t(nd));
	set16(0x14, uint16_t(na + nd));
	const size_t n = u16(0x14);
	g.set_ptr(b, 0x4, int(g.alloc(4 * n)));
	g.set_ptr(b, 0xA74, int(g.alloc(2 * n)));
	g.set_ptr(b, 0xA78, int(g.alloc(2 * n)));
	g.set_ptr(b, 0xC, int(g.alloc(2 * n)));
	g.set_ptr(b, 0x8, int(g.alloc(2 * n)));
	set16(0x10, 0);
	auto arr = [&](size_t o) { return size_t(g.ptr(b, o)); };
	auto new_entry = [&](uint16_t ship, uint16_t index, uint8_t side) {
		const size_t e = g.alloc(0x164);
		fn_61a4e0(e, ship, b, index, side);
		return e;
	};
	auto add_items = [&](size_t i) {
		g.set<uint16_t>(arr(0xA74), 2 * i, fn_619450(b, 0, uint8_t(i), 0, 0, 0, 0));
		g.set<uint16_t>(arr(0xA78), 2 * i, fn_619450(b, 6, uint8_t(i), 0, 0, 0, 0));
	};
	// Insertion sort of [+0x4][from .. from + n), strongest first; then
	// each scene ship's [+0x138] its place.
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
		g.set_ptr(arr(0x4), 0, int(new_entry(a, 0, 0)));
		g.set<uint16_t>(arr(0x8), 0, 0xFFFF);
		g.set<uint16_t>(arr(0xC), 0, 0xFFFF);
	} else {
		uint32_t s = save_.convoys[u16(0x65E)].u16(0xA);
		const size_t m = u16(0x658);
		for (size_t i = 0; i < m; ++i) {
			while (u16(0x35A) != 0xFE && save_.ships[s].memory.u8(0) != u16(0x35A)) {
				s = save_.ships[s].memory.u16(0x6);
			}
			add_items(i);
			const size_t e = new_entry(uint16_t(s), uint16_t(i), 0);
			g.set_ptr(arr(0x4), 4 * i, int(e));
			if (u16(0x35A) == 0xFE && save_.ships[s].memory.u8(0) == u16(0x35C)) {
				g.set<uint8_t>(e, 0x12A, uint8_t(g.get<uint8_t>(e, 0x12A) | 1));
			}
			g.set<uint16_t>(arr(0x8), 2 * i, uint16_t(i - 1));
			g.set<uint16_t>(arr(0xC), 2 * i, i + 1 < m ? uint16_t(i + 1) : 0xFFFF);
			s = save_.ships[s].memory.u16(0x6);
		}
		sort(0, m);
	}
	const uint16_t m658 = u16(0x658);
	set16(0x12, m658);
	set16(0x10, 0);
	g.set<uint16_t>(arr(0x8), 2 * size_t(m658), 0xFFFF);
	set8(0x12E8, 1);
	set16(0x65C, m658);
	if (u16(0x660) >= convoys) {
		add_items(m658);
		g.set_ptr(arr(0x4), 4 * size_t(m658), int(new_entry(d, m658, 1)));
		g.set<uint16_t>(arr(0x8), 2 * size_t(m658), 0xFFFF);
		g.set<uint16_t>(arr(0xC), 2 * size_t(m658), 0xFFFF);
	} else {
		uint32_t s = save_.convoys[u16(0x660)].u16(0xA);
		const size_t total = u16(0x14);
		for (size_t i = m658; i < total; ++i) {
			for (;;) {
				const ShipMemory& sm = save_.ships[s].memory;
				if (u16(0x35C) == 0xFE) {
					break;
				}
				const bool skip_armed = sm.s32(0x120) != 0 && uint8_t(flag) != 0;
				if (!skip_armed && sm.u8(0) == u16(0x35C)) {
					break;
				}
				set8(0x12E8, c);
				s = sm.u16(0x6);
			}
			add_items(i);
			const size_t e = new_entry(uint16_t(s), uint16_t(i), 1);
			g.set_ptr(arr(0x4), 4 * i, int(e));
			if (u16(0x35C) == 0xFE && save_.ships[s].memory.u8(0) == u16(0x35A)) {
				g.set<uint8_t>(e, 0x12A, uint8_t(g.get<uint8_t>(e, 0x12A) | 1));
			}
			g.set<uint16_t>(arr(0x8), 2 * i, i > m658 ? uint16_t(i - 1) : 0xFFFF);
			g.set<uint16_t>(arr(0xC), 2 * i, i + 1 < total ? uint16_t(i + 1) : 0xFFFF);
			s = save_.ships[s].memory.u16(0x6);
		}
		sort(m658, u16(0x65A));
	}
	// The wind.
	battle_rand(g, b);
	set16(0x12, 0xFFFF);
	set16(0x65C, u16(0x658));
	int32_t wind = int32_t(g.get<uint32_t>(b, 0xADC) & 3) + 5;
	for (uint32_t k = 0; k < 16; ++k) {
		const uint32_t slot = global::kShips + 0x20 + 0xC * k;
		if (gl.u16(slot) >= count) {
			continue;
		}
		const int32_t dx = int32_t(gl.s16(slot + 4)) - s16(u16(0xAD6));
		const int32_t dy = int32_t(gl.s16(slot + 6)) - s16(u16(0xADA));
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
	const uint32_t r = battle_rand(g, b);
	set8(0x670, uint8_t(r));
	set8(0x671, uint8_t(wind));
	set8(0x5C, 0);
	uint16_t captain = 0xFFFF;
	if (u16(0x35A) == 0xFF) {
		captain = save_.ships[save_.convoys[u16(0x65E)].u16(0x10)].memory.u16(0x42);
	} else if (u16(0x35C) == 0xFF) {
		captain = save_.ships[save_.convoys[u16(0x660)].u16(0x10)].memory.u16(0x42);
	}
	if (captain < save_.captain_count()) {
		set8(0x1E90, save_.captains[captain].u8(0x2));
		set8(0x1E91, save_.captains[captain].u8(0x3));
	} else {
		set8(0x1E90, 0xFF);
		set8(0x1E91, 0xFF);
	}
	for (size_t i = 0; i < u8(0x558); ++i) {
		set16(0x15C + 2 * i, u8(0x5D + i) ? 0 : 0xFA);
	}
	if (u8(0xAC8) != 0) {
		fn_608230(b, true);
		return;
	}
	// The local player's human sides (0x60651F..0x606C12; [+0x5D + i] 0,
	// owner [0x6DFC14]). In the game, when the player directs no battle
	// ([0x6E59CC] 0xFF) and no battle window is open ([0x6E59CD] 0), the
	// battle's question opens: the message «%s "%s" is being attacked by
	// ...» (or «is attacking») written into [+0x1310], the window's
	// description at [+0x12EC] (its buttons 0x6B2BC0, «Steer manually»; in
	// the Spanish exe «¿Quieres dirigir manualmente a los barcos en la
	// batalla naval?») and the window (0x41D9B0 on *0x6CBB10) with [+0x12E9]
	// 0, [0x6E59CD] 1 and [+0x12EA] 1: part 7. Otherwise (0x606BAE) the
	// player is busy with another battle, and this one goes to the computer
	// at once: operation 0x96 {0x96, [+0x662], player, 1, 0} (fn_60fec9,
	// battles.cpp) and the side's count [+0x15C] 0xFFFF (the step waits until
	// the operation runs). p3vita has no battle window yet, so it always
	// takes this path; the oracle too (BATTLE_WINDOW_JUMPS, 0x60659B →
	// 0x606BAE).
	// (The game tests [+0x5D] at the side's team, [+0x559 + i], which is the
	// side itself in a sea battle.) For the viewer, the message the window
	// would have shown goes to the player's messages (attack_message).
	const uint32_t player = gl.u32(0x6DFC14);
	for (size_t i = 0; i < u8(0x558); ++i) {
		if (u16(0x35A + 2 * i) != player || u8(0x5D + size_t(u8(0x559 + i))) != 0) {
			continue;
		}
		if (texts_) {
			player_messages_.push_back(attack_message(b, a, d, i));
		}
		uint8_t op[0x14];
		const uint32_t fields[5] = {0x96, u16(0x662), player, 1, 0};
		std::memcpy(op, fields, sizeof(op));
		tick::enqueue_operation(save_, op, sizeof(op));  // 0x54AA70
		set16(0x15C + 2 * i, 0xFFFF);
	}
}

}  // namespace p3::data
