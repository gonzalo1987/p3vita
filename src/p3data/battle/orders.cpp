// The player's orders in a battle he directs (the battles' piece 7): the
// operations 0x92, 0x93, 0x95, 0x97, 0x98, 0x9A, 0x9B (0x540F50..0x541700)
// and 0xA0 (0x6116A9), and the goal of a scene's ship (0x6270C7).
//
// Each order but 0xA0 is for the scene's ships marked in a group of 64
// places ([+0x138] of a scene's ship): the record's byte `group`, its low
// mask (places group·64 .. group·64 + 31, bit place & 31) and its high mask
// (group·64 + 32 .. group·64 + 63). The interface builds them from the
// battle's selection (its ships of the player), one record per group with a
// bit set (0x6103E9, 0x610579, 0x610C09, 0x610FD9, 0x608360, 0x608550; the
// keys and the panel's buttons, 0x60ED39 and 0x42D230; the deck page,
// 0x4942C0 and 0x48D420). The game doesn't check a place against the scene's
// ships ([+0x4] read past its end): p3vita does nothing for one past it.

#include <array>
#include <cstring>
#include <vector>

#include "p3data/battle/battle_math.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

using namespace battle_math;

namespace {

constexpr uint32_t kSeaShipVtable = 0x67B0D4;  // a sea battle's scene ship (0x164 bytes)

}  // namespace

// The marked places of a battle's order: the low mask's, then the high's.
template <class F>
void Sailing::for_marked(size_t b, uint8_t group, uint32_t low, uint32_t high, F f) {
	const BattleGraph& g = save_.battles;
	const int ships = g.ptr(b, 0x4);
	for (size_t half = 0; half < 2; ++half) {
		const uint32_t mask = half == 0 ? low : high;
		if (mask == 0) {
			continue;
		}
		const uint32_t first = (uint32_t(group) << 6) + 32 * uint32_t(half);
		for (uint32_t place = first; place < first + 32; ++place) {
			if (!(mask & (1u << (place & 0x1F)))) {
				continue;
			}
			const int e = ships >= 0 ? g.ptr(size_t(ships), 4 * size_t(place)) : -1;
			if (e >= 0) {
				f(size_t(e));
			}
		}
	}
}

// 0x6270C7 (thiscall on a scene ship's [+0x4], x, y): its goal ([+0x20] x,
// [+0x22] y of the scene ship) and its heading to it from its place ([+0xA],
// [+0xE]) in [+0x24], in 256ths of a turn: dx and dy halved while dx is out
// of ±0x7FFF; dy 0: 0x40 (dx > 0) or 0xC0; else by t = (dx << 16) / dy and
// the arctangent (0x67B348, the same as 0x67AA28), towards 0x80 when dy > 0.
void Sailing::fn_6270c7(size_t e, uint16_t x, uint16_t y) {
	BattleGraph& g = save_.battles;
	g.set<uint16_t>(e, 0x20, x);
	g.set<uint16_t>(e, 0x22, y);
	int32_t dy = int32_t(int16_t(y)) - g.get<int16_t>(e, 0xE);
	int32_t dx = int32_t(int16_t(x)) - g.get<int16_t>(e, 0xA);
	while (dx > 0x7FFF || dx < -0x7FFF) {
		dx >>= 1;
		dy >>= 1;
	}
	if (dy == 0) {
		g.set<uint16_t>(e, 0x24, dx <= 0 ? 0xC0 : 0x40);
		return;
	}
	const int32_t t = int32_t(uint32_t(dx) << 16) / dy;
	uint8_t a;
	if (t < 0) {
		if (-t > 0x10000) {
			a = uint8_t(kAtan[(0x80 - 0x40000000 / t) >> 8] - 0x40);
		} else {
			a = uint8_t(-kAtan[(0x200 - t) >> 10]);
		}
	} else if (t > 0x10000) {
		a = uint8_t(0x40 - kAtan[(0x40000000 / t + 0x80) >> 8]);
	} else {
		a = kAtan[(t + 0x200) >> 10];
	}
	g.set<uint16_t>(e, 0x24, dy > 0 ? uint8_t(0x80 - a) : uint8_t(-a));
}

// Operation 0x92 (0x540F50): {u8 slot, u8 argument, u8 group, u32 high, u32
// low}: each marked ship's method +0x8 (it fires: 0x61DD4F, a town battle's
// 0x624950) with the argument (1 from the interface).
void Sailing::fn_540f50(const uint8_t* op) {
	const int b = save_.battles.battle(op[4]);
	if (b < 0) {
		return;
	}
	uint32_t high, low;
	std::memcpy(&high, op + 8, 4);
	std::memcpy(&low, op + 0xC, 4);
	for_marked(size_t(b), op[6], low, high, [&](size_t e) {
		if (save_.battles.get<uint32_t>(e, 0) == kSeaShipVtable) {
			fn_61dd4f(e, op[5] != 0);
		} else {
			fn_624950(e, op[5] != 0);
		}
	});
}

// Operation 0x93 (0x541050): {u8 slot, u8 group, u16 target, u32 high, u32
// low}: each marked ship not grappling (0x8) gets the target ([+0x128]: a
// place, or 0xFFFE − k the tower k of a town battle); flag 0x1 cleared.
// (For a mark with no ship the game clears the flag at [0x12A].)
void Sailing::fn_541050(const uint8_t* op) {
	const int b = save_.battles.battle(op[4]);
	if (b < 0) {
		return;
	}
	BattleGraph& g = save_.battles;
	uint16_t target;
	uint32_t high, low;
	std::memcpy(&target, op + 6, 2);
	std::memcpy(&high, op + 8, 4);
	std::memcpy(&low, op + 0xC, 4);
	for_marked(size_t(b), op[5], low, high, [&](size_t e) {
		if (!(g.get<uint8_t>(e, 0x12A) & 0x8)) {
			g.set<uint16_t>(e, 0x128, target);
		}
		g.set<uint8_t>(e, 0x12A, uint8_t(g.get<uint8_t>(e, 0x12A) & 0xFE));
	});
}

// Operation 0x95 (0x541200): {u8 slot, u8 group, u16 x, u16 y, u32 high, u32
// low}: each marked ship heads for the point (0x6270C7); not grappling, it
// drops its target ([+0x128] 0xFFFF); flag 0x1 cleared.
void Sailing::fn_541200(const uint8_t* op) {
	const int b = save_.battles.battle(op[4]);
	if (b < 0) {
		return;
	}
	BattleGraph& g = save_.battles;
	uint16_t x, y;
	uint32_t high, low;
	std::memcpy(&x, op + 6, 2);
	std::memcpy(&y, op + 8, 2);
	std::memcpy(&high, op + 0xC, 4);
	std::memcpy(&low, op + 0x10, 4);
	for_marked(size_t(b), op[5], low, high, [&](size_t e) {
		fn_6270c7(e, x, y);
		uint8_t flags = g.get<uint8_t>(e, 0x12A);
		if (!(flags & 0x8)) {
			g.set<uint16_t>(e, 0x128, 0xFFFF);
		}
		g.set<uint8_t>(e, 0x12A, uint8_t(flags & 0xFE));
	});
}

// Operation 0x97 (0x541380): {u8 slot, u8 board, u8 take over, u8 group, u32
// high, u32 low}: each marked ship boards ([+0x13E]; boarding clears flag
// 0x1) and takes the ship over (flag 0x80: capture) or plunders it (no 0x80).
void Sailing::fn_541380(const uint8_t* op) {
	const int b = save_.battles.battle(op[4]);
	if (b < 0) {
		return;
	}
	BattleGraph& g = save_.battles;
	uint32_t high, low;
	std::memcpy(&high, op + 8, 4);
	std::memcpy(&low, op + 0xC, 4);
	for_marked(size_t(b), op[7], low, high, [&](size_t e) {
		if (op[5] != 0) {
			g.set<uint8_t>(e, 0x12A, uint8_t(g.get<uint8_t>(e, 0x12A) & 0xFE));
		}
		g.set<uint8_t>(e, 0x13E, op[5]);
		const uint8_t flags = g.get<uint8_t>(e, 0x12A);
		g.set<uint8_t>(e, 0x12A, op[6] != 0 ? uint8_t(flags | 0x80) : uint8_t(flags & 0x7F));
	});
}

// Operation 0x98 (0x5414E0): {u8 slot, u8 sails, u8 group, u32 high, u32
// low}: each marked ship's sails ([+0x148], 0..4).
void Sailing::fn_5414e0(const uint8_t* op) {
	const int b = save_.battles.battle(op[4]);
	if (b < 0) {
		return;
	}
	uint32_t high, low;
	std::memcpy(&high, op + 8, 4);
	std::memcpy(&low, op + 0xC, 4);
	for_marked(size_t(b), op[6], low, high, [&](size_t e) { save_.battles.set<uint8_t>(e, 0x148, op[5]); });
}

// Operation 0x9A (0x5415E0): {u8 slot, u8 group, u32 high, u32 low}: each
// marked ship flees: no boarding ([+0x13E] 0), flags 0x1 and 0x80 cleared
// and 0x20 set.
void Sailing::fn_5415e0(const uint8_t* op) {
	const int b = save_.battles.battle(op[4]);
	if (b < 0) {
		return;
	}
	BattleGraph& g = save_.battles;
	uint32_t high, low;
	std::memcpy(&high, op + 8, 4);
	std::memcpy(&low, op + 0xC, 4);
	for_marked(size_t(b), op[5], low, high, [&](size_t e) {
		const uint8_t flags = g.get<uint8_t>(e, 0x12A);
		g.set<uint8_t>(e, 0x13E, 0);
		g.set<uint8_t>(e, 0x12A, uint8_t((flags & 0x5E) | 0x20));
	});
}

// Operation 0x9B (0x541700): {u8 slot, u8 on, u8 group, u32 high, u32 low}:
// each marked ship raises the white flag (flag 0x1; no boarding, 0x80
// cleared) or lowers it.
void Sailing::fn_541700(const uint8_t* op) {
	const int b = save_.battles.battle(op[4]);
	if (b < 0) {
		return;
	}
	BattleGraph& g = save_.battles;
	uint32_t high, low;
	std::memcpy(&high, op + 8, 4);
	std::memcpy(&low, op + 0xC, 4);
	for_marked(size_t(b), op[6], low, high, [&](size_t e) {
		const uint8_t flags = g.get<uint8_t>(e, 0x12A);
		if (op[5] != 0) {
			g.set<uint8_t>(e, 0x12A, uint8_t((flags & 0x7E) | 1));
			g.set<uint8_t>(e, 0x13E, 0);
		} else {
			g.set<uint8_t>(e, 0x12A, uint8_t(flags & 0xFE));
		}
	});
}

// Operation 0xA0 (0x536D04 → 0x6116A9 on the battle in slot [+0x4], u32,
// unchecked; the merchant [+0x8]): in a sea battle not over, the other
// side's ships (side A's merchant: side B's list [+0x65C], else side A's
// [+0x10]; next [+0xC], while below [+0x14]) not out nor fled (0xE) leave
// it (flag 0x2; [+0xAC4] 0, [+0xAC6] the place): «Abandonar», when the enemy
// has fled.
void Sailing::fn_6116a9(size_t b, uint8_t merchant) {
	BattleGraph& g = save_.battles;
	if (g.get<uint32_t>(b, 0) != kSeaBattleVtable || g.get<uint8_t>(b, 0x5C) != 0) {
		return;  // Method +0x28: 1 for a town battle (0x602980).
	}
	uint16_t k = g.get<uint16_t>(b, 0x35A) == merchant ? g.get<uint16_t>(b, 0x65C) : g.get<uint16_t>(b, 0x10);
	const int next = g.ptr(b, 0xC);
	while (k < g.get<uint16_t>(b, 0x14)) {
		const size_t e = battle_entry(b, k);
		const uint8_t flags = g.get<uint8_t>(e, 0x12A);
		if (!(flags & 0xE)) {
			g.set<uint8_t>(e, 0x12A, uint8_t(flags | 0x2));
			g.set<uint8_t>(b, 0xAC4, 0);
			g.set<uint16_t>(b, 0xAC6, k);
		}
		k = next >= 0 ? g.get<uint16_t>(size_t(next), 2 * size_t(k)) : 0xFFFF;
	}
}

// The interface's builders of the orders: the battle's selection ([+0x66C],
// a block of [+0xAD0] pointers to scene ships; the viewer sets it,
// set_battle_selection) gives the marks of the player's ships, one record
// per group of 64 places with a bit set, queued only while the queue has
// room (0x34 − [0x6DF346] > 0x1A). `skip` leaves out ships with those flags.
std::vector<std::array<uint32_t, 2>> Sailing::selection_marks(size_t b, uint8_t skip) const {
	const BattleGraph& g = save_.battles;
	const size_t groups = size_t(uint8_t((g.get<uint16_t>(b, 0x14) + 0x3F) >> 6));
	std::vector<std::array<uint32_t, 2>> marks(groups, {0, 0});
	const int selection = g.ptr(b, 0x66C);
	const uint32_t player = save_.globals.u32(0x6DFC14);
	for (uint16_t i = 0; selection >= 0 && i < g.get<uint16_t>(b, 0xAD0); ++i) {
		const int e = g.ptr(size_t(selection), 4 * size_t(i));
		if (e < 0) {
			continue;
		}
		const uint8_t side = g.get<uint8_t>(size_t(e), 0x13D);
		if (g.get<uint16_t>(b, 0x35A + 2 * size_t(side)) != player || (g.get<uint8_t>(size_t(e), 0x12A) & skip)) {
			continue;
		}
		const uint16_t place = g.get<uint16_t>(size_t(e), 0x138);
		if (size_t(place >> 6) < groups) {
			marks[place >> 6][(place >> 5) & 1] |= 1u << (place & 0x1F);
		}
	}
	return marks;
}

bool Sailing::order_room() const {
	return 0x34 - int32_t(save_.globals.u16(0x6DF346)) > 0x1A;
}

// One order per group with a mark: `bytes` fills the record (the group, the
// low mask, the high mask).
template <class F>
void Sailing::queue_marked(size_t b, uint8_t skip, F bytes) {
	const std::vector<std::array<uint32_t, 2>> marks = selection_marks(b, skip);
	for (size_t k = 0; k < marks.size(); ++k) {
		if ((marks[k][0] == 0 && marks[k][1] == 0) || !order_room()) {
			continue;
		}
		uint8_t op[0x14] = {};
		op[4] = uint8_t(save_.battles.get<uint16_t>(b, 0x662));
		bytes(op, uint8_t(k), marks[k][0], marks[k][1]);
		tick::enqueue_operation(save_, op, sizeof(op));  // 0x54AA70
	}
}

// 0x6103E9 (on a battle; a, b): operation 0x97 {0x97, slot, board (a or b),
// take over (b), group, high, low}: the panel's Plündern (a: plunder) and
// Kapern (b: capture) buttons, the keys.
void Sailing::fn_6103e9(size_t b, bool plunder, bool capture) {
	queue_marked(b, 0, [&](uint8_t* op, uint8_t group, uint32_t low, uint32_t high) {
		op[0] = 0x97;
		op[5] = (plunder || capture) ? 1 : 0;
		op[6] = capture ? 1 : 0;
		op[7] = group;
		std::memcpy(op + 8, &high, 4);
		std::memcpy(op + 0xC, &low, 4);
	});
}

// 0x610579 (on a battle; the sails): operation 0x98 {0x98, slot, sails,
// group, high, low}: the keys (the selection's highest ± 1), the deck page.
// Then the ship's window (*0x6CE6D0) shows them: the viewer's.
void Sailing::fn_610579(size_t b, uint8_t sails) {
	queue_marked(b, 0, [&](uint8_t* op, uint8_t group, uint32_t low, uint32_t high) {
		op[0] = 0x98;
		op[5] = sails;
		op[6] = group;
		std::memcpy(op + 8, &high, 4);
		std::memcpy(op + 0xC, &low, 4);
	});
}

// 0x610C09 (on a battle): operation 0x9A {0x9A, slot, group, high, low}: the
// panel's Fliehen button (flee).
void Sailing::fn_610c09(size_t b) {
	queue_marked(b, 0, [&](uint8_t* op, uint8_t group, uint32_t low, uint32_t high) {
		op[0] = 0x9A;
		op[5] = group;
		std::memcpy(op + 8, &high, 4);
		std::memcpy(op + 0xC, &low, 4);
	});
}

// 0x610FD9 (on a battle; on): operation 0x9B {0x9B, slot, on, group, high,
// low}: the panel's Aufgeben button (the white flag).
void Sailing::fn_610fd9(size_t b, bool on) {
	queue_marked(b, 0, [&](uint8_t* op, uint8_t group, uint32_t low, uint32_t high) {
		op[0] = 0x9B;
		op[5] = on ? 1 : 0;
		op[6] = group;
		std::memcpy(op + 8, &high, 4);
		std::memcpy(op + 0xC, &low, 4);
	});
}

// 0x608360 (on a battle; the target's sprite id): operation 0x93 {0x93,
// slot, group, u16 the target's place, high, low} for the selected ships of
// the player not grappling (0x8): a touch on an enemy ship. The game finds
// the target by its sprite ([+0x134], its id [+0x16]; with none it reads
// place [0x138]); p3vita's viewer gives the target's place.
void Sailing::fn_608360(size_t b, uint16_t target) {
	queue_marked(b, 0x8, [&](uint8_t* op, uint8_t group, uint32_t low, uint32_t high) {
		op[0] = 0x93;
		op[5] = group;
		std::memcpy(op + 6, &target, 2);
		std::memcpy(op + 8, &high, 4);
		std::memcpy(op + 0xC, &low, 4);
	});
}

// 0x6016CB (on a town battle; a cell of the town view): a touch on a tower.
// The game first moves the touched cell to the base cell of what stands there
// on the town shown (0x590960 on the town view: the viewer's; p3vita takes
// that cell), less the town map's ends ([+0x1ED8], [+0x1EDC]) at a town (not
// a nest); the tower k at that cell ([+0x1EAE + k], [+0x1EB2 + k]), or
// nothing; with a selection ([+0xAD0]), operation 0x93 {0x93, slot, group,
// u16 0xFFFE − k, high, low} per group of 64 places with a mark, while the
// queue has room. Unlike the sea's builders every selected ship is marked,
// of whatever owner and state. (The game leaks its masks' block when no
// tower is there, and writes past it for a place beyond the battle's
// groups: here such a place is left out.)
void Sailing::fn_6016cb(size_t b, int32_t cell_x, int32_t cell_y) {
	const BattleGraph& g = save_.battles;
	const size_t groups = size_t(uint8_t((g.get<uint16_t>(b, 0x14) + 0x3F) >> 6));
	if (g.get<uint16_t>(b, 0xAD0) == 0) {
		return;
	}
	if (g.get<uint8_t>(b, 0x1E98) < save_.globals.u32(0x6DE4B0)) {
		cell_x -= g.get<int32_t>(b, 0x1ED8);
		cell_y -= g.get<int32_t>(b, 0x1EDC);
	}
	uint8_t k = 0;
	while (k < 4 && !(uint32_t(g.get<uint8_t>(b, 0x1EAE + k)) == uint32_t(cell_x) &&
					   uint32_t(g.get<uint8_t>(b, 0x1EB2 + k)) == uint32_t(cell_y))) {
		++k;
	}
	if (k == 4) {
		return;
	}
	std::vector<uint32_t> masks(2 * groups, 0);
	const int selection = g.ptr(b, 0x66C);
	for (uint16_t i = 0; selection >= 0 && i < g.get<uint16_t>(b, 0xAD0); ++i) {
		const int e = g.ptr(size_t(selection), 4 * size_t(i));
		if (e < 0) {
			continue;
		}
		const uint16_t place = g.get<uint16_t>(size_t(e), 0x138);
		if (size_t(uint8_t(place >> 5)) < masks.size()) {
			masks[uint8_t(place >> 5)] |= 1u << (place & 0x1F);
		}
	}
	const uint16_t target = uint16_t(0xFFFE - k);
	for (size_t group = 0; group < groups; ++group) {
		const uint32_t low = masks[2 * group], high = masks[2 * group + 1];
		if ((low == 0 && high == 0) || !order_room()) {
			continue;
		}
		uint8_t op[0x14] = {};
		op[0] = 0x93;
		op[4] = uint8_t(g.get<uint16_t>(b, 0x662));
		op[5] = uint8_t(group);
		std::memcpy(op + 6, &target, 2);
		std::memcpy(op + 8, &high, 4);
		std::memcpy(op + 0xC, &low, 4);
		tick::enqueue_operation(save_, op, sizeof(op));  // 0x54AA70
	}
}

// 0x608550 (on a battle; x, y, send): for each selected ship of the player
// (index i of the selection) its line to draw ([+0xAE0 + 8·i] its x,
// [+0xAE4 + 8·i] its y · 22 / 34), the point ([+0x12D8] x, [+0x12DC] y · 22
// / 34) and [+0x12E0] = send; with send, operation 0x95 {0x95, slot, group,
// u16 x, u16 y, high, low}: a touch on the water (its point), which the step
// sends again while [+0x12E0] is set.
void Sailing::fn_608550(size_t b, uint32_t x, uint32_t y, bool send) {
	BattleGraph& g = save_.battles;
	const int selection = g.ptr(b, 0x66C);
	const uint32_t player = save_.globals.u32(0x6DFC14);
	for (uint16_t i = 0; selection >= 0 && i < g.get<uint16_t>(b, 0xAD0); ++i) {
		const int e = g.ptr(size_t(selection), 4 * size_t(i));
		if (e < 0 || g.get<uint16_t>(b, 0x35A + 2 * size_t(g.get<uint8_t>(size_t(e), 0x13D))) != player) {
			continue;
		}
		g.set<int32_t>(b, 0xAE0 + 8 * size_t(i), g.get<int16_t>(size_t(e), 0xA));
		g.set<int32_t>(b, 0xAE4 + 8 * size_t(i), int32_t(g.get<int16_t>(size_t(e), 0xE)) * 22 / 34);
		g.set<uint32_t>(b, 0x12D8, x);
		g.set<int32_t>(b, 0x12DC, int32_t(y * 22) / 34);
		g.set<uint8_t>(b, 0x12E0, send ? 1 : 0);
	}
	if (!send) {
		return;
	}
	queue_marked(b, 0, [&](uint8_t* op, uint8_t group, uint32_t low, uint32_t high) {
		op[0] = 0x95;
		op[5] = group;
		const uint16_t px = uint16_t(x), py = uint16_t(y);
		std::memcpy(op + 6, &px, 2);
		std::memcpy(op + 8, &py, 2);
		std::memcpy(op + 0xC, &high, 4);
		std::memcpy(op + 0x10, &low, 4);
	});
}

// Fire (the key, 0x60F0D2..0x60F110 in method +0x20; the deck page's button,
// 0x4942C0, for its one ship): operation 0x92 {0x92, slot, 1, group, high,
// low}.
void Sailing::queue_fire(size_t b) {
	queue_marked(b, 0, [&](uint8_t* op, uint8_t group, uint32_t low, uint32_t high) {
		op[0] = 0x92;
		op[5] = 1;
		op[6] = group;
		std::memcpy(op + 8, &high, 4);
		std::memcpy(op + 0xC, &low, 4);
	});
}

// The deck page's orders for its one ship (its place): the fire button (0x4942C0: operation 0x92 {0x92, slot,
// 1, group, high, low}) and the sails' rope (0x48D420: 0x98 {0x98, slot, sails, group, high, low}); the place's
// bit in the high mask with 0x20, else in the low one.
void Sailing::queue_one(size_t b, uint16_t place, uint8_t kind, uint8_t value) {
	uint8_t op[0x14] = {};
	op[0] = kind;
	op[4] = uint8_t(save_.battles.get<uint16_t>(b, 0x662));
	op[5] = value;
	op[6] = uint8_t(place >> 6);
	const uint32_t bit = 1u << (place & 0x1F);
	const uint32_t high = (place & 0x20) ? bit : 0, low = (place & 0x20) ? 0 : bit;
	std::memcpy(op + 8, &high, 4);
	std::memcpy(op + 0xC, &low, 4);
	tick::enqueue_operation(save_, op, sizeof(op));  // 0x54AA70
}

void Sailing::queue_fire_one(size_t b, uint16_t place) {
	queue_one(b, place, 0x92, 1);
}

void Sailing::queue_sails_one(size_t b, uint16_t place, uint8_t sails) {
	queue_one(b, place, 0x98, sails);
}

// «Abandonar» (the panel's Beenden button, 0x42D45D; the key, 0x60F2D0):
// operation 0xA0 {0xA0, the slot, the player}.
void Sailing::queue_abandon(size_t b) {
	uint8_t op[0x14];
	const uint32_t fields[5] = {0xA0, save_.battles.get<uint16_t>(b, 0x662), save_.globals.u32(0x6DFC14), 0, 0};
	std::memcpy(op, fields, sizeof(op));
	tick::enqueue_operation(save_, op, sizeof(op));  // 0x54AA70
}

// The battle's selection (the game's method +0x20 builds it from its ship
// selection, *0x6CE6D0): a new block of the scene ships' pointers in [+0x66C]
// and their number in [+0xAD0]; the viewer's.
void Sailing::set_battle_selection(size_t b, const std::vector<uint16_t>& places) {
	BattleGraph& g = save_.battles;
	g.blocks.emplace_back(4 * places.size());
	const size_t sel = g.blocks.size() - 1;
	for (size_t i = 0; i < places.size(); ++i) {
		g.set_ptr(sel, 4 * i, int(battle_entry(b, places[i])));
	}
	g.set_ptr(b, 0x66C, int(sel));
	g.set<uint16_t>(b, 0xAD0, uint16_t(places.size()));
}

}  // namespace p3::data
