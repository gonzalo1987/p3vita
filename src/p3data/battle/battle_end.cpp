// The end of a sea battle (docs/cloud/battles-2.md §4): method +0x1C
// (0x60D299), the battle's destructor (method +0x4, 0x611740 → 0x603BC0) and
// the battles object's table of ships that met (0x612BD0, 0x612A20).

#include <array>

#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kTick = 0x6DE4B4;  // u32
constexpr uint32_t kPlayer = 0x6DFC14;  // u32
constexpr uint32_t kNetwork = 0x6DF31C;  // u32

// The music level of a battle ([+0x1E94]: level << 10 | a fraction of
// 1000) after a ship's end worth `worth` (0x60D299).
void music(BattleGraph& g, size_t b, int32_t worth) {
	const uint16_t v = g.get<uint16_t>(b, 0x1E94);
	const int32_t keep = ((1000 - int32_t(v & 0x3FF)) * (1000 - worth) + 500) / 1000;
	g.set<uint16_t>(b, 0x1E94, uint16_t(((v >> 10) << 10) + (1000 - keep)));
}

}  // namespace

// 0x612A20 (on the battles object; two ships, a value): ship j and the value
// in ship i's list of the table [+0xC30] (records of 12 bytes: the list, its
// room and its count; [+0xC34] records, grown by hundreds; each list grown
// by 10 entries of 8 bytes: u32 the ship and u8 the value).
void Sailing::fn_612a20(uint32_t i, uint32_t j, uint8_t value) {
	BattleGraph& g = save_.battles;
	const uint32_t records = g.get<uint32_t>(0, 0xC34);
	if (i >= records) {
		uint32_t more = i - records + 0x64;
		more -= more % 100;
		g.set<uint32_t>(0, 0xC34, records + more);
		const size_t table = g.realloc_at(0, 0xC30, size_t(records + more) * 12);
		for (uint32_t k = records; k < records + more; ++k) {
			g.set<uint32_t>(table, 12 * size_t(k) + 0x8, 0);
			g.set<uint32_t>(table, 12 * size_t(k) + 0x4, 0);
			g.set<uint32_t>(table, 12 * size_t(k), 0);
		}
	} else {
		const size_t table = size_t(g.ptr(0, 0xC30));
		const uint32_t count = g.get<uint32_t>(table, 12 * size_t(i) + 0x8);
		const int list = g.ptr(table, 12 * size_t(i));
		for (uint32_t k = 0; k < count && list >= 0; ++k) {
			if (g.get<uint32_t>(size_t(list), 8 * size_t(k)) == j) {
				g.set<uint8_t>(size_t(list), 8 * size_t(k) + 4, value);
				return;
			}
		}
	}
	const size_t table = size_t(g.ptr(0, 0xC30));
	const size_t at = 12 * size_t(i);
	if (g.get<uint32_t>(table, at + 0x8) == g.get<uint32_t>(table, at + 0x4)) {
		g.set<uint32_t>(table, at + 0x4, g.get<uint32_t>(table, at + 0x4) + 10);
		g.realloc_at(table, at, size_t(g.get<uint32_t>(table, at + 0x4)) * 8);
	}
	const size_t list = size_t(g.ptr(table, at));
	const uint32_t count = g.get<uint32_t>(table, at + 0x8);
	g.set<uint32_t>(list, 8 * size_t(count), j);
	g.set<uint8_t>(list, 8 * size_t(count) + 4, value);
	g.set<uint32_t>(table, at + 0x8, count + 1);
}

// 0x612BD0 (on the battles object; two convoys): each ship of one met each
// of the other (0x612A20 both ways, 0xFF).
void Sailing::fn_612bd0(uint16_t a, uint16_t b) {
	const uint16_t ships = save_.globals.u16(global::kShipCount);
	for (uint32_t i = save_.convoys[a].u16(0xA); i < ships; i = save_.ships[i].memory.u16(0x6)) {
		for (uint32_t j = save_.convoys[b].u16(0xA); j < ships; j = save_.ships[j].memory.u16(0x6)) {
			fn_612a20(i, j, 0xFF);
			fn_612a20(j, i, 0xFF);
		}
	}
}

// 0x60D299 (method +0x1C of a sea battle; whether side 1 has a ship with
// sailors): the end of the battle, once [+0x15C] is 0 or above 0xF000. A
// third party of the defender's convoy (owners of neither side) may stay
// for another battle: one of them, by the battle's generator weighted by
// hold ([+0x10]), into [+0x130C] (unless side 1 has sailors or [+0x12E8]).
// Without [+0x12E8], the owners of the two convoys' ships that are of
// neither side are added as sides (only in the arrays: [+0x558] stays; the
// game counts them in its report). Per ship (side 0's list, then side 1's;
// those with a hull): the report's counts by the side it started on
// ([+0x142]) and the music ([+0x1E94], if its team is [+0xAD2] − 1) by its
// end (sunk 100, taken over 200, else 50). A pirate side that won with a
// guide with a captain makes the news (the battles object's [+0x408] the
// tick, unless the loser is the player). Then [+0x130E] the flag, the battle
// over ([+0x5C]) and [+0x15C] 1 (automatic) or 250.
//
// The report and the letters (0x4D6530), the news ticker and the player's
// window are not rewritten (messages, UI; part 7).
void Sailing::fn_60d299(size_t b, bool sailors) {
	BattleGraph& g = save_.battles;
	auto u8 = [&](size_t o) { return g.get<uint8_t>(b, o); };
	auto u16 = [&](size_t o) { return g.get<uint16_t>(b, o); };
	{
		const uint16_t left = u16(0x15C);
		if (left != 0 && left < 0xF000) {
			return;
		}
	}
	const uint16_t ships = save_.globals.u16(global::kShipCount);
	const uint16_t convoys = save_.convoy_count();
	auto owner = [&](uint32_t s) { return uint16_t(save_.ships[s].memory.u8(0x0)); };
	uint16_t stay = 0xFFFF;
	if (!sailors && u8(0x12E8) == 0 && u16(0x660) < convoys) {
		const uint16_t first = save_.convoys[u16(0x660)].u16(0xA);
		if (first < ships) {
			int32_t total = 0;
			for (uint32_t s = first; s < ships; s = save_.ships[s].memory.u16(0x6)) {
				if (owner(s) != u16(0x35C) && owner(s) != u16(0x35A)) {
					total += save_.ships[s].memory.s32(0x10);
				}
			}
			if (total > 0) {
				int32_t r = int32_t(battle_rand(g, b) % uint32_t(total)) + 1;
				uint32_t s = first;
				for (;;) {
					stay = uint16_t(s);
					if (owner(s) != u16(0x35C) && owner(s) != u16(0x35A)) {
						r -= save_.ships[s].memory.s32(0x10);
					}
					if (r <= 0) {
						break;
					}
					s = save_.ships[s].memory.u16(0x6);
				}
			}
		}
	}
	if (u8(0x12E8) == 0) {
		uint32_t sides = u8(0x558);
		for (const auto& [convoy_at, side_at, team] :
			{std::array<size_t, 3>{0x65E, 0x35A, 0}, std::array<size_t, 3>{0x660, 0x35C, 1}}) {
			if (u16(convoy_at) >= convoys || u16(side_at) == 0xFE) {
				continue;
			}
			for (uint32_t s = save_.convoys[u16(convoy_at)].u16(0xA); s < ships; s = save_.ships[s].memory.u16(0x6)) {
				bool known = false;
				for (uint32_t k = 0; k < sides; ++k) {
					if (owner(s) == u16(0x35A + 2 * size_t(k))) {
						known = true;
					}
				}
				if (!known) {
					g.set<uint16_t>(b, 0x35A + 2 * size_t(sides), owner(s));
					g.set<uint8_t>(b, 0x5D + size_t(sides), 1);
					g.set<uint8_t>(b, 0x559 + size_t(sides), uint8_t(team));
					++sides;
				}
			}
		}
	}
	// The report's counts (by the side each ship started on).
	uint8_t on[2] = {0, 0}, kept[2] = {0, 0}, kept_taken[2] = {0, 0};
	const size_t next = size_t(g.ptr(b, 0xC));
	for (const size_t head : {size_t(0x10), size_t(0x65C)}) {
		for (uint16_t k = u16(head); k != 0xFFFF; k = g.get<uint16_t>(next, 2 * size_t(k))) {
			const size_t e = battle_entry(b, k);
			if (scene_ship(e).u32(0x14) == 0) {
				continue;
			}
			const uint8_t fl = g.get<uint8_t>(e, 0x12A);
			const size_t first = g.get<uint8_t>(e, 0x142) == 0 ? 0 : 1;
			int32_t worth = 0x32;
			if (fl & 0x4) {
				worth = 0x64;
			} else if (g.get<uint8_t>(e, 0x13D) != g.get<uint8_t>(e, 0x142)) {
				worth = 0xC8;
			} else {
				++kept[first];
				if (g.get<uint8_t>(e, 0x12A) & 0x60) {
					++kept_taken[first];
				}
			}
			++on[first];
			if (u8(0xAD2) == (first == 0 ? 2 : 1)) {
				music(g, b, worth);
			}
		}
	}
	const size_t loser = sailors ? 0 : 1;  // The side whose ships are counted as lost.
	uint16_t lost = uint16_t(on[loser] - kept[loser]);
	bool news = false;
	const uint32_t tick = save_.globals.u32(kTick);
	if (g.get<uint32_t>(0, 0x408) + 0x1E00 < tick) {
		news = true;
	} else {
		const uint16_t m = u16(0x35C - 2 * size_t(sailors ? 1 : 0));
		if (m < save_.globals.u16(global::kWorldMerchantCount) && save_.merchants[m].memory.u16(0x8) == 0) {
			news = true;
		}
	}
	if (lost == 0 && news && kept_taken[loser] != 0) {
		lost = 0xFF;
	}
	const size_t winner = sailors ? 1 : 0;
	if (u16(0x35A + 2 * winner) == 0xFF && lost > 0) {
		uint16_t guide = 0xFFFF;
		if (winner == 1) {
			guide = u16(0x660) < save_.convoys.size() ? save_.convoys[u16(0x660)].u16(0x10) : 0xFFFF;
			if (guide >= convoys) {
				guide = 0xFFFF;
			}
		} else {
			guide = u16(0x65E) < save_.convoys.size() ? save_.convoys[u16(0x65E)].u16(0x10) : 0xFFFF;
		}
		if (guide < save_.ships.size() && save_.ships[guide].memory.u32(0x18) != 0 &&
			save_.ships[guide].memory.u16(0x42) < save_.globals.u16(global::kCaptainCount)) {
			// A letter to everyone (0x4D6530) and the news ticker: not rewritten.
			if (save_.globals.u32(kPlayer) != u16(0x35C - 2 * winner)) {
				g.set<uint32_t>(0, 0x408, tick);
			}
		}
	}
	g.set<uint8_t>(b, 0x130E, sailors ? 1 : 0);
	g.set<uint16_t>(b, 0x130C, stay);
	g.set<uint8_t>(b, 0x5C, 1);
	g.set<uint16_t>(b, 0x15C, u8(0xAC8) != 0 ? 1 : 0xFA);
}

// 0x611740 (method +0x4 of a sea battle; free): its destructor 0x603BC0.
// Its slot in the battles object cleared (and the count [+0x404] down to
// the last used); the [+0x3FD] of a battle on screen; back to the normal
// time with a human side (0x612D20). When it ended ([+0x5C], [+0x15C] 0): a
// third party that stays ([+0x130C]) starts a new battle (0x611CD0,
// 0x605420) against side 0's first ship, with the music; else the two
// convoys' ships met (0x612BD0), maybe a convoy's trophy by the music
// (0x50CB80), a Hanse convoy in state 9 sent home (0x50C9E0), and both
// convoys back to their ways (0x50BFD0). Its blocks freed (they are no
// longer reachable from the battles object).
void Sailing::fn_611740(size_t b, bool free) {
	(void)free;
	BattleGraph& g = save_.battles;
	auto u8 = [&](size_t o) { return g.get<uint8_t>(b, o); };
	auto u16 = [&](size_t o) { return g.get<uint16_t>(b, o); };
	const uint16_t slot = u16(0x662);
	if (g.get<uint32_t>(0, 4 * size_t(slot)) == 0) {
		return;
	}
	g.set<uint32_t>(0, 4 * size_t(slot), 0);
	uint8_t count = g.get<uint8_t>(0, 0x404);
	while (count != 0 && g.get<uint32_t>(0, 4 * size_t(count - 1)) == 0) {
		--count;
	}
	g.set<uint8_t>(0, 0x404, count);
	if (u8(0x12EA) != 0) {
		g.set<uint8_t>(0, 0x3FD, 0);
	}
	if (save_.globals.u32(kNetwork) == 0) {
		bool human = false;
		for (size_t i = 0; i < u8(0x558); ++i) {
			if (u8(0x5D + i) == 0) {
				human = true;
			}
		}
		if (human) {
			fn_612d20(false);
		}
	}
	if (u8(0x5C) != 0 && u16(0x15C) == 0) {
		const uint16_t ships = save_.globals.u16(global::kShipCount);
		if (u16(0x130C) < ships) {
			const uint8_t s = fn_611cd0();
			const int nb = g.battle(s);
			const uint32_t first = g.get<uint32_t>(battle_entry(b, u16(0x10)), 0x4);
			const bool flag = save_.ships[u16(0x130C)].memory.u8(0x0) == u16(0x35C);
			if (nb >= 0) {
				fn_605420(size_t(nb), uint16_t(first), u16(0x130C), 1, flag ? 1 : 0);
				g.set<uint16_t>(size_t(nb), 0x1E94, u16(0x1E94));
			}
		} else {
			if (u16(0x65E) != 0xFFFF && u16(0x660) != 0xFFFF) {
				fn_612bd0(u16(0x65E), u16(0x660));
				const uint8_t side = u8(0xAD2);
				if (side == 1 || side == 2) {
					if (battle_rand(g, b) % 1000u < uint32_t(u16(0x1E94) & 0x3FF)) {
						const uint32_t r = battle_rand(g, b);
						if (side == 1) {
							fn_50cb80(u16(0x65E), u16(0x35C), r, uint8_t(u16(0x1E94) >> 10));
						} else {
							fn_50cb80(u16(0x660), u16(0x35A), r, uint8_t(u16(0x1E94) >> 10));
						}
					}
				}
			}
			if (u16(0x660) != 0xFFFF && save_.convoys[u16(0x660)].u16(0x12) == 9) {
				fn_50c9e0(u16(0x660), u8(0x130E) == 0);
				g.set<uint16_t>(b, 0x660, 0xFFFF);
			}
			const uint16_t a = u16(0x65E), d = u16(0x660);
			if (a == 0xFFFF) {
				if (d != 0xFFFF) {
					fn_50bfd0(d, 0, 0xFF);
				}
			} else if (d == 0xFFFF) {
				fn_50bfd0(a, 0, 0xFF);
			} else {
				fn_50bfd0(a, 0, uint8_t(u16(0x35C)));
				fn_50bfd0(d, 0, uint8_t(u16(0x35A)));
			}
		}
	}
	const size_t next = size_t(g.ptr(b, 0xC));
	for (const auto& [count_at, head_at] : {std::array<size_t, 2>{0x658, 0x10}, std::array<size_t, 2>{0x65A, 0x65C}}) {
		if (u16(count_at) == 0) {
			continue;
		}
		for (uint16_t k = u16(head_at); k != 0xFFFF; k = g.get<uint16_t>(next, 2 * size_t(k))) {
			const int e = g.ptr(size_t(g.ptr(b, 0x4)), 4 * size_t(k));
			if (e >= 0 && g.get<uint8_t>(size_t(e), 0x160) != 0) {
				g.set<uint8_t>(0, 0x3FD, 0);  // 0x61A6D0: the ship's window was open.
			}
		}
	}
}

}  // namespace p3::data

namespace p3::data {

// 0x50CB80 (on the ships object; a convoy, the other side's owner, a value
// of the battle's generator, the music level): an event 5 (0x4D8CF0, at the
// tick + the convoy + 0x500) about the convoy's merchant: u8 [+0x8] it,
// [+0x9] and [+0xA] (its [+0x19] and the other owner; above level 12 the
// other owner and 0xFF), [+0xB] its [+0x19], [+0xC] the tick, [+0x10] the
// level, [+0x12] the value, [+0x16] [+0x15E] of the guide (or the first
// ship). Nothing if the convoy's owner isn't a merchant.
void Sailing::fn_50cb80(uint16_t convoy, uint16_t other, uint32_t value, uint8_t level) {
	const GameMemory<0x3C>& c = save_.convoys[convoy];
	const uint8_t owner = c.u8(0x0);
	if (owner >= save_.globals.u16(global::kWorldMerchantCount)) {
		return;
	}
	const uint32_t tick = save_.globals.u32(kTick);
	GameMemory<0x18>& e = save_.events[tick::events_detail::fn_4d8cf0(save_, tick + convoy + 0x500)];
	e.u16(0x6) = 5;
	e.u8(0x8) = owner;
	if (level < 0xD) {
		e.u8(0x9) = save_.merchants[owner].memory.u8(0x19);
		e.u8(0xA) = uint8_t(other);
	} else {
		e.u8(0xA) = 0xFF;
		e.u8(0x9) = uint8_t(other);
	}
	e.u8(0xB) = save_.merchants[e.u8(0x8)].memory.u8(0x19);
	e.u8(0x10) = level;
	e.u32(0xC) = tick;
	e.u8(0x11) = 0;
	e.u16(0x12) = uint16_t(value);
	e.u16(0x14) = 0;
	const uint16_t ships = save_.globals.u16(global::kShipCount);
	if (c.u16(0x10) < ships) {
		e.u16(0x16) = save_.ships[c.u16(0x10)].memory.u16(0x15E);
	} else if (c.u16(0xA) < ships) {
		e.u16(0x16) = save_.ships[c.u16(0xA)].memory.u16(0x15E);
	} else {
		e.u16(0x16) = 0;
	}
}

// 0x50C9E0 (on the ships object; a convoy, a flag): a Hanse convoy (state 9)
// after its battle. Its event (if 0x24; with the flag it ends: [+0x15] 5,
// [+0x16] 0xFFFF and its town's [+0x2C8] without 0x600) or none; its sunk
// ships (hull ≤ 0) leave it for the list [+0xEA] (state 0x11), the others
// for [+0xE8] (state 3) without an event. The convoy, empty, is freed (0xFF,
// to [+0xE4]), else waits in state 9 ([+0xEC]).
//
// Not rewritten (not run in the saves): the ships of a Hanse convoy whose
// event stays (0x6132C0 on 0x6E6220, then state 9 and [+0x138] 0x20).
void Sailing::fn_50c9e0(uint16_t convoy, bool flag) {
	GlobalMemory& gl = save_.globals;
	GameMemory<0x3C>& c = save_.convoys[convoy];
	bool hanse = false;
	if (c.u16(0x2) < gl.u16(0x6DD748)) {
		GameMemory<0x18>& e = save_.events[c.u16(0x2)];
		if (e.u16(0x6) == 0x24) {
			hanse = !flag;
			if (flag) {
				e.u8(0x15) = 5;
				e.u16(0x16) = 0xFFFF;
				if (e.u8(0x14) < gl.u32(0x6DE4B0)) {
					TownMemory& t = save_.towns[e.u8(0x14)].memory;
					t.u32(0x2C8) = t.u32(0x2C8) & ~0x600u;
				}
			}
		}
	}
	const uint16_t ships = gl.u16(global::kShipCount);
	uint16_t* link = &c.u16(0xA);
	while (*link < ships) {
		ShipMemory& s = save_.ships[*link].memory;
		const uint16_t index = *link;
		if (s.s32(0x18) <= 0) {
			s.u16(0x8) = 0xFFFF;
			*link = s.u16(0x6);
			s.u16(0x134) = 0x11;
			s.u16(0x6) = gl.u16(global::kShips + 0xEA);
			gl.u16(global::kShips + 0xEA) = index;
		} else if (!hanse) {
			s.u16(0x8) = 0xFFFF;
			*link = s.u16(0x6);
			s.u16(0x134) = 3;
			s.u16(0x6) = gl.u16(global::kShips + 0xE8);
			gl.u16(global::kShips + 0xE8) = index;
			s.u16(0x138) = 0;
		} else {
			// Not rewritten: 0x6132C0.
			s.u16(0x134) = 9;
			s.u16(0x138) = 0x20;
			link = &s.u16(0x6);
		}
	}
	if (c.u16(0xA) >= ships) {
		c.bytes.fill(0xFF);
		c.u16(0x8) = gl.u16(global::kShips + 0xE4);
		gl.u16(global::kShips + 0xE4) = convoy;
	} else {
		c.u16(0x12) = 9;
		c.u16(0x8) = gl.u16(global::kShips + 0xEC);
		gl.u16(global::kShips + 0xEC) = convoy;
	}
}

}  // namespace p3::data

namespace p3::data {

// 0x50BFD0 (on the ships object; a convoy, a flag, the other side's owner):
// the convoy after its battle. Being rewritten (next part).
void Sailing::fn_50bfd0(uint16_t convoy, uint8_t flag, uint8_t other) {
	(void)convoy;
	(void)flag;
	(void)other;
}

}  // namespace p3::data
