// A ship taken in a sea battle (0x60C1C9, called by the boarding 0x61F1BF)
// and the pirates' share of the loot (0x515F70); docs/cloud/battles-2.md §4.

#include <algorithm>

#include "p3data/battle/battle_math.hpp"
#include "p3data/battle/pirate_nests.hpp"
#include "p3data/sailing.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick::alderman_detail {
// town_tick/alderman.cpp
uint8_t fn_51a470(const SaveGame& save, int ship);
void fn_516170(SaveGame& save, uint32_t pirate, uint32_t type, bool captain, uint32_t strength);
}  // namespace p3::data::tick::alderman_detail

namespace p3::data {

using namespace battle_math;

namespace {

constexpr uint32_t kTick = 0x6DE4B4;  // u32
constexpr uint16_t kBuildingWorth = 200;  // 0x672A2E (u16)
constexpr int16_t kLootFame = 65;  // 0x672DE2 (s16)
// 0x672C14 (u8 by ware < 0x14): 1 for a ware counted in barrels (200), else
// in loads (2000); the others by 10.
constexpr uint8_t kBarrels[20] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0};
// 0x673A18 (float by ware): what a unit of each ware is worth to the pirates.
constexpr float kWorth[24] = {0.055000003f, 0.47855002f, 0.22005001f, 0.174f, 0.1425f, 0.55f, 1.4f, 1.1f, 1.034f,
	3.3825f, 0.41249999f, 0.027500002f, 1.278f, 1.12f, 0.44000003f, 0.278f, 0.44000003f, 0.22000001f, 0.85499996f,
	0.039900005f, 10.0f, 8.0f, 12.0f, 14.0f};
// 0x673658 (by [+0x14] of a pirate): its ships at the nest.
constexpr uint8_t kNestShips[4] = {4, 8, 16, 18};

// The game's generator 0x46C350 (seeded with the tick).
uint32_t next(uint32_t& x) {
	x = 0xA205B063u - x * 0x44BF19B3u;
	x = (x & 1) ? (x >> 1) | 0x80000000u : x >> 1;
	return x;
}

}  // namespace

// 0x515F70 (on pirate k): its share of a rich loot, when it has no convoy at
// sea: [+0x16] + 200 and a building planned (0x515DA0); by a generator
// seeded with the tick, sometimes (by its [+0x14]) a new ship at the nest
// (0x516170, a wreck: hull 1) if it has fewer than 1.5 · 0x673658 ships
// there, and sometimes a building of the nest (entries 1 to 3 of its pool,
// outside its lists, of kind 0x30 or 0x31) back in the list as 0x30 with a
// countdown of 100; or the first 0x30 of the list becomes 0x31.
void fn_515f70(SaveGame& save, size_t k) {
	GameMemory<0x18>& p = *save.pirates[k];
	if (p.u16(0x10) < save.convoy_count()) {
		return;
	}
	p.u16(0x16) = uint16_t(p.u16(0x16) + kBuildingWorth);
	fn_515da0(save, k);
	uint32_t x = save.globals.u32(kTick);
	const uint16_t ships = save.globals.u16(global::kShipCount);
	if (p.u8(0x14) >= (next(x) & 0xF)) {
		const uint8_t most = p.u8(0x14) < 4 ? kNestShips[p.u8(0x14)] : 0;
		uint8_t left = uint8_t(most + (most >> 1));
		for (uint32_t s = p.u16(0xC); s < ships && left != 0; s = save.ships[s].memory.u16(0x4)) {
			if (save.ships[s].memory.u8(0x15D) != 0) {
				--left;
			}
		}
		if (left != 0) {
			const uint32_t type = next(x) & 3;
			tick::alderman_detail::fn_516170(save, uint32_t(k), type, false, uint32_t(p.u8(0x14)) * 40 + 0x32);
			save.ships[save.pirates[k]->u16(0xC)].memory.u32(0x18) = 1;
		}
	}
	if (p.u8(0x14) < (next(x) & 0x1F)) {
		return;
	}
	std::vector<GameMemory<8>>& pool = save.pirate_nests[k].pool;
	const uint16_t size = std::min<uint16_t>(p.u16(0x8), uint16_t(pool.size()));
	bool used[4] = {false, false, false, false};
	if (size > 0 && pool[0].u16(0x4) < size) {
		uint16_t i = 0;
		for (;;) {
			if (i < 4) {
				used[i] = true;
			}
			if (pool[i].u8(0x3) == 0x30) {
				pool[i].u8(0x3) = 0x31;
				return;
			}
			const uint16_t n = pool[i].u16(0x4);
			if (n >= size || pool[n].u16(0x4) >= size) {
				break;
			}
			i = n;
		}
	}
	for (uint16_t f = p.u16(0xA); f < size; f = pool[f].u16(0x4)) {
		if (f < 4) {
			used[f] = true;
		}
	}
	for (uint16_t c = 1; c < 4 && c < size; ++c) {
		if (!used[c] && (pool[c].u8(0x3) == 0x31 || pool[c].u8(0x3) == 0x30)) {
			pool[c].u8(0x3) = 0x30;
			pool[c].u8(0x6) = 0x64;
			pool[c].u16(0x4) = pool[0].u16(0x4);
			pool[0].u16(0x4) = c;
			return;
		}
	}
}

// 0x60C1C9 (on the battle; the winner's place, the taken one's): the ship
// in the second place is taken by the first's. Both stop grappling and
// heading anywhere ([+0x8], [+0x128], [+0x24] its heading, [+0x20]); the
// battle's [+0xAC4] = 2, [+0xAC6] the taken place. Unless the winner attacks
// it (0x6212FB with the flag), the taken ship is plundered (0x40 | 0x20):
// the winner takes as many of its reserve ([+0x154]) as fit (10 each), then
// its wares, the most worth first (0x673A18), as fit, whole barrels or loads
// (the loser's rounded down too); both holds again (0x5182B0) and [+0x3C] |
// 0x14. The worth of the loot · 65 / 100 (at most 60 000) goes to the
// merchant of the winner's convoy's guide ([+0x15D], with a captain), with
// fame + 65; a pirate with 5 000 or more gets its share (0x515F70). The
// winner's team, when [+0xAD2] is it + 1, raises [+0x1E94] >> 10 to 10.
//
// Not rewritten (not run in the saves): the winner attacks the ship and
// takes it (operation 0x91 at once, 0x540520, and the sides' lists); a
// side of the player (its messages and window, part 7); the messages of a
// pirate ship with a captain taken (0x4D6530, not rewritten in p3vita).
void Sailing::fn_60c1c9(size_t b, uint16_t winner, uint16_t taken) {
	BattleGraph& g = save_.battles;
	const size_t w = battle_entry(b, winner), t = battle_entry(b, taken);
	const bool attack = fn_6212fb(w, t, true);
	g.set<uint8_t>(t, 0x148, 0);
	g.set<uint8_t>(w, 0x148, 0);
	g.set<uint16_t>(b, 0xAC6, taken);
	g.set<uint8_t>(b, 0xAC4, 2);
	for (const size_t x : {w, t}) {
		g.set<uint8_t>(x, 0x12A, uint8_t(g.get<uint8_t>(x, 0x12A) & 0xF7));
		g.set<uint16_t>(x, 0x128, 0xFFFF);
		g.set<uint16_t>(x, 0x24, g.get<uint8_t>(x, 0x11));
		g.set<uint16_t>(x, 0x20, 0xFFFF);
	}
	if (attack) {
		return;  // Not rewritten: see above.
	}
	SavedShip& sw = save_.ships[g.get<uint32_t>(w, 0x4)];
	SavedShip& st = save_.ships[g.get<uint32_t>(t, 0x4)];
	ShipMemory& mw = sw.memory;
	ShipMemory& mt = st.memory;
	g.set<uint8_t>(t, 0x12A, uint8_t(g.get<uint8_t>(t, 0x12A) | 0x60));
	bool changed = false;
	uint32_t room = mw.u32(0x10) - mw.u32(0x11C) - mw.u32(0x118);
	const uint16_t reserve = mt.u16(0x154);
	if (reserve > 0) {
		if (room < uint32_t(reserve) * 10) {
			const uint32_t n = room / 10;
			if (n != 0) {
				mw.u16(0x154) = uint16_t(mw.u16(0x154) + n);
				mt.u16(0x154) = uint16_t(mt.u16(0x154) - n);
				room -= 10 * n;
				changed = true;
			}
		} else {
			room -= uint32_t(reserve) * 10;
			mw.u16(0x154) = uint16_t(mw.u16(0x154) + reserve);
			mt.u16(0x154) = 0;
			changed = true;
		}
	}
	uint8_t order[24];
	uint8_t count = 0;
	for (uint8_t ware = 0; ware < 24; ++ware) {
		if (mt.u32(0x54 + 4 * size_t(ware)) == 0) {
			continue;
		}
		uint8_t i = count;
		while (i > 0 && kWorth[ware] > kWorth[order[i - 1]]) {
			order[i] = order[i - 1];
			--i;
		}
		order[i] = ware;
		++count;
	}
	uint32_t worth = 0;
	for (uint8_t i = 0; i < count; ++i) {
		const uint8_t ware = order[i];
		const size_t at = 0x54 + 4 * size_t(ware);
		const int32_t have = mt.s32(at);
		uint32_t take = std::min(uint32_t(have), room);
		const int32_t unit = ware < 0x14 ? (kBarrels[ware] ? 200 : 2000) : 10;
		take -= take % uint32_t(unit);
		mt.s32(at) = have - have % unit;
		if (take == 0) {
			continue;
		}
		changed = true;
		mw.u32(at) = mw.u32(at) + take;
		mt.u32(at) = mt.u32(at) - take;
		room -= take;
		// fild, fstp to a float, fmul by the float: the product of two floats
		// is exact in a double, as in the x87.
		const double v = double(static_cast<float>(int64_t(take))) * double(kWorth[ware]);
		worth += uint32_t(int64_t(v));
	}
	if (changed) {
		recalculate_cargo(sw);
		recalculate_cargo(st);
		mw.u16(0x3C) = uint16_t(mw.u16(0x3C) | 0x14);
		mt.u16(0x3C) = uint16_t(mt.u16(0x3C) | 0x14);
	}
	const uint16_t ships = save_.globals.u16(global::kShipCount);
	const uint16_t convoy = mw.u16(0x8);
	const uint16_t guide = convoy < save_.convoys.size() ? save_.convoys[convoy].u16(0x10) : 0xFFFF;
	if (guide < ships) {
		const ShipMemory& gs = save_.ships[guide].memory;
		const uint8_t owner = gs.u8(0x15D);
		if (owner < save_.globals.u16(global::kWorldMerchantCount) &&
			gs.u16(0x42) < save_.globals.u16(global::kCaptainCount)) {
			MerchantMemory& m = save_.merchants[owner].memory;
			worth = worth * 65u / 100u;
			if (worth > 60000) {
				worth = 60000;
			}
			m.u32(0x0) += worth;
			m.u32(0x4B8) += worth;
			int16_t fame = m.s16(0x16);
			if (kLootFame > 0) {
				fame = int32_t(fame) < 0x7FFF - kLootFame ? int16_t(fame + kLootFame) : int16_t(0x7FFF);
			} else {
				fame = int32_t(fame) + 0x8000 > -int32_t(kLootFame) ? int16_t(fame + kLootFame) : int16_t(-0x8000);
			}
			m.s16(0x16) = fame;
			// A letter to the merchant (0x4D6530, not rewritten in p3vita).
		}
	}
	if (worth >= 5000) {
		const uint8_t k = tick::alderman_detail::fn_51a470(save_, int(g.get<uint32_t>(w, 0x4)));
		if (k < 5 && save_.pirates[k]) {
			fn_515f70(save_, k);
		}
	}
	const uint8_t team = g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(w, 0x13D)));
	if (uint32_t(team) + 1 == g.get<uint8_t>(b, 0xAD2)) {
		const uint16_t v = g.get<uint16_t>(b, 0x1E94);
		if ((v >> 10) < 0xA) {
			g.set<uint16_t>(b, 0x1E94, uint16_t((v & 0x3FF) + 0x2800));
		}
	}
}

}  // namespace p3::data
