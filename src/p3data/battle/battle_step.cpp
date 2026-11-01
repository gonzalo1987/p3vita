// The step of a sea battle (operation 0x94 → method +0x14, 0x606CC0) and
// what it calls (docs/cloud/battles-2.md §4, docs/cloud/battles-2-605420.md).
// A scene's ship is a block of 0x164 bytes of BattleGraph: [+0x4] its ship,
// [+0x12C] its battle, [+0x138] its place, [+0x13D] its side, [+0x12A]
// flags (0x2 out, 0x4, 0x8, 0x20 fled, 0x40 taken, 0x80).

#include <array>

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
