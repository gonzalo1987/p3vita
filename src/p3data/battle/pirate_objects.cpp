// The pirate objects' daily update (0x514000, from 0x506720 when the tick ends
// in 0x6F) and the operation it queues, 0xB5 (0x514D40: a convoy leaves the
// nest). See docs/cloud/battles-2.md §2.3. They run with
// Sailing::set_pirates(true), as with town_oracle.py --pirates.

#include <cstring>

#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {
namespace alderman_detail {
// town_tick/alderman.cpp
void fn_516170(SaveGame& save, uint32_t pirate, uint32_t type, bool captain, uint32_t strength);
void fn_51a750(SaveGame& save, int ship, uint8_t n);
void fn_51a4e0(SaveGame& save, int ship, uint32_t strength, uint8_t level);
}  // namespace alderman_detail
namespace events_detail {
// town_tick/events.cpp
uint16_t fn_5097c0(SaveGame& save, bool keep);
bool fn_4fe150(const CaptainMemory& c);
}  // namespace events_detail
namespace events_rare_detail {
void fn_5098b0(SaveGame& save, uint16_t& captain);  // town_tick/events_rare.cpp
}  // namespace events_rare_detail
namespace events_mayor_detail {
void fn_5181d0(SaveGame& save, int ship);  // town_tick/events_mayor.cpp
}  // namespace events_mayor_detail
namespace facilities_detail {
uint32_t fn_52bb00(const SaveGame& save, int town);  // town_tick/facilities.cpp
}  // namespace facilities_detail
}  // namespace p3::data::tick

namespace p3::data::tick::events_mayor_detail {
// town_tick/events_mayor.cpp
uint16_t fn_507d60(SaveGame& save, uint16_t& out, uint32_t town, uint32_t mode);
void fn_52a8d0(SaveGame& save, int town, int ship, uint32_t type);
void fn_509a50(SaveGame& save, int ship);
}  // namespace p3::data::tick::events_mayor_detail

namespace p3::data {

namespace tick::merchants_detail {
uint8_t fn_4f9430(const SaveGame& save, int merchant);  // town_tick/merchants.cpp: a merchant's title
}  // namespace tick::merchants_detail

// battle/pirates.cpp: a free town number for a nest.
uint8_t fn_5159c0(const SaveGame& save, uint32_t n);

namespace {

constexpr uint32_t kPirates = global::kShips + 0xC;  // 5 pointers to the pirate objects
constexpr uint32_t kConvoyHead = global::kShips + 0xEC;
constexpr uint32_t kTick = 0x6DE4B4;
constexpr uint32_t kWorld4 = 0x6DE4A4;  // u8
constexpr uint32_t kHanseTown = 0x6DE52E;  // u8: the town of the Hanse's leader
constexpr uint32_t kTownTable = 0x6DDB90;  // 0x34 bytes per number of StadtDaten.ini
constexpr uint32_t kNetwork = 0x6DF31C;
constexpr uint32_t kNoOperations = 0x6DE52A;  // u8
// 0x673658 (u8, indexed by the pirate's [+0x14]): how many of its ships may
// be away from the nest, less 3. Past the 4 entries, the bytes that follow.
constexpr uint8_t kShipsAway[8] = {4, 8, 16, 18, 0, 0, 0, 0x4B};
// 0x66E030 + 0x18 · type: the loads per level of each type.
constexpr int32_t kLoadsPerLevel[4] = {3, 5, 8, 10};

// The generator of the game (0x46C320 creates, 0x46C340 seeds, 0x46C380 the
// next one modulo n), as in battle/pirates.cpp.
struct GameRandom {
	uint32_t seed = 0x499602D2;
	uint32_t next(uint32_t n) {
		uint32_t x = 0xA205B063u - seed * 0x44BF19B3u;
		x = (x & 1) ? (x >> 1) | 0x80000000u : x >> 1;
		seed = x;
		return x % n;
	}
};

uint16_t ship_count(const SaveGame& save) {
	return save.globals.u16(global::kShipCount);
}

}  // namespace

// 0x5185C0 (on the ship): the sailors it can take, level [+0xF] · the loads
// of its type + capacity / 2000, at least 20 (also the side panel's ship
// menu, player_actions.cpp).
uint32_t fn_5185c0(const ShipMemory& s) {
	const int32_t n = int32_t(s.u8(0xF)) * kLoadsPerLevel[s.u8(0xE) & 3] + s.s32(0x10) / 2000;
	return uint32_t(n) < 20 ? 20 : uint32_t(n);
}

namespace {

// 0x517770 (on the ship, flags): [+0x3C] |= flags and, in a convoy, its [+0x18] too.
void fn_517770(SaveGame& save, ShipMemory& s, uint16_t flags) {
	s.u16(0x3C) |= flags;
	if (s.u16(0x8) < save.convoy_count()) {
		save.convoys[s.u16(0x8)].u32(0x18) |= flags;
	}
}

// 0x515750 (cdecl: the ship, &strength): arms a ship of a convoy leaving the
// nest. With *strength = −1, the mean of the arms [+0x120] of the AI... of
// every merchant's ships that have some (or 0). Then the cargo and [+0xB4..]
// emptied, [+0x154], [+0x156] = 0, the flag 4 (0x517770, twice) and one
// less of [+0x157], no artillery (0x5181D0, 0x5182B0), *strength · (setting
// [0x12] + 4) / 4 in artillery of the level of the Hanse leader's town
// (0x51A4E0), the sailors (0x5185C0 · 70, 85 or 100 % with a captain, 50, 70
// or 90 % without, by the setting [0x12], rounded up) and [+0x154] the
// sailors, or a tenth of the free space if that is less.
void fn_515750(SaveGame& save, uint16_t ship, int32_t& strength) {
	if (strength == -1) {
		int32_t sum = 0, n = 0;
		for (int m = int(save.merchant_count) - 1; m >= 0; --m) {
			for (uint32_t i = save.merchants[size_t(m)].memory.u16(0xE); i < ship_count(save);
					i = save.ships[i].memory.u16(0x4)) {
				const int32_t arms = save.ships[i].memory.s32(0x120);
				if (arms > 0) {
					sum += arms;
					++n;
				}
			}
		}
		strength = n > 0 ? sum / n : 0;
	}
	if (ship >= ship_count(save)) {
		return;
	}
	ShipMemory& s = save.ships[ship].memory;
	s.u16(0x154) = 0;
	std::memset(s.bytes.data() + 0x54, 0, 0x60);
	std::memset(s.bytes.data() + 0xB4, 0, 0x60);
	s.u8(0x156) = 0;
	fn_517770(save, s, 4);
	s.u8(0x157) = s.u8(0x157) > 1 ? uint8_t(s.u8(0x157) - 1) : 0;
	fn_517770(save, s, 4);
	std::memset(s.bytes.data() + 0x13C, 0xFF, 0x18);
	tick::events_mayor_detail::fn_5181d0(save, ship);
	const int32_t free_space = recalculate_cargo(save.ships[ship]);
	const uint8_t setting = save.settings.u8(0x12);
	strength = int32_t((uint32_t(setting + 4) * uint32_t(strength)) >> 2);
	const uint32_t level = tick::facilities_detail::fn_52bb00(save, save.globals.u8(kHanseTown));
	tick::alderman_detail::fn_51a4e0(save, ship, uint32_t(strength), uint8_t(level));
	ShipMemory& m = save.ships[ship].memory;
	const uint32_t most = fn_5185c0(m);
	static constexpr uint32_t kWithCaptain[3] = {70, 85, 100};
	static constexpr uint32_t kWithout[3] = {50, 70, 90};
	const bool captain = m.u16(0x42) < save.captain_count();
	const uint32_t percent = (captain ? kWithCaptain : kWithout)[setting < 2 ? setting : 2];
	m.u16(0x40) = uint16_t(uint16_t(most * percent) + 0x63) / 100;
	const int32_t sailors = m.u16(0x40);
	m.u16(0x154) = uint16_t(free_space > 10 * sailors ? sailors : free_space / 10);
	recalculate_cargo(save.ships[ship]);
}

}  // namespace

// 0x513EC0 (on pirate k's object): the pirate goes away. Its map and pool
// are freed ([+0x8], [+0xA], [+0x16] = 0), every ship of its list [+0xC] gets
// [+0x3C] |= 0x100 and no hull, its convoy [+0x10] is deleted (0x50A3D0), the
// ships at its nest are deleted (0x507FB0), the merchants whose [+0xE1] is
// its nest get 0xFF in [+0xE1] and [+0xE0], and its [+0xE], [+0xF], [+0x14]
// become 0xFF.
void Sailing::fn_513ec0(size_t k) {
	GameMemory<0x18>& p = *save_.pirates[k];
	PirateNest& nest = save_.pirate_nests[k];
	nest = PirateNest{};
	p.u32(0x0) = 0;
	p.u32(0x4) = 0;
	p.u16(0x8) = 0;
	p.u16(0xA) = 0;
	p.u16(0x16) = 0;
	for (uint32_t i = p.u16(0xC); i < ship_count(save_); i = save_.ships[i].memory.u16(0x4)) {
		save_.ships[i].memory.u16(0x3C) |= 0x100;
		save_.ships[i].memory.s32(0x18) = 0;
	}
	if (p.u16(0x10) < save_.convoy_count()) {
		fn_50a3d0(p.u16(0x10));
		p.u16(0x10) = 0xFFFF;
	}
	while (p.u16(0x12) < ship_count(save_)) {
		const uint16_t i = p.u16(0x12);
		p.u16(0x12) = save_.ships[i].memory.u16(0x6);
		save_.ships[i].memory.u16(0x6) = 0xFFFF;
		fn_507fb0(i);
	}
	for (uint32_t m = 0; m < uint32_t(save_.merchant_count); ++m) {
		MerchantMemory& merchant = save_.merchants[m].memory;
		if (merchant.u8(0xE1) == p.u8(0xE)) {
			merchant.u8(0xE1) = 0xFF;
			merchant.u8(0xE0) = 0xFF;
		}
	}
	p.u8(0xE) = 0xFF;
	p.u8(0xF) = 0xFF;
	p.u8(0x14) = 0xFF;
}

// 0x514000 (on pirate k's object, k), once a day (the tick ends in 0x6F).
// - A pirate that goes away ([+0x15] = 1): the ships at its nest are deleted;
//   of its ships, those without a convoy or whose convoy is in the list lose
//   their hull; when it has no ships left, 0x513EC0 and the object is
//   deleted (the pointer of the globals = 0).
// - Otherwise its ships get [+0x158] = k (0x51A4C0) and are counted (those of
//   the pirates, [+0x15D] not a merchant; those with a captain). With a
//   convoy out ([+0x10]) nothing more. The countdowns of the pool go down 4.
//   Without ships at the nest, every 4 days a new one (0x516170 of type 2,
//   strength 4000, full hull, two levels, its sailors). With them, each one
//   is repaired 1000 (fully when less is missing: then a privateer's ship, or
//   else one with a captain, is chosen) and those of the pirates are not
//   counted as away. With more away than 0x673658[[+0x14]] + 3, a ship at the
//   nest without captain is deleted. Without captains, the first ship at the
//   nest gets one (0x5097C0). With a chosen ship and none away (or a
//   privateer's), outside network games, operation 0xB5 {k, [+0x14] · 100 +
//   0x78, 1 for a privateer or [+0x14] + 1} (0x514D40: a convoy leaves).
void Sailing::fn_514000(size_t k) {
	GlobalMemory& g = save_.globals;
	GameMemory<0x18>& p = *save_.pirates[k];
	const uint16_t count = ship_count(save_);
	const uint16_t convoys = save_.convoy_count();
	const uint16_t captains = save_.captain_count();
	const uint16_t merchants = uint16_t(save_.merchant_count);
	if (p.u8(0x15) == 1) {
		while (p.u16(0x12) < count) {
			const uint16_t i = p.u16(0x12);
			p.u16(0x12) = save_.ships[i].memory.u16(0x6);
			fn_507fb0(i);
		}
		for (uint32_t i = p.u16(0xC); i < count; i = save_.ships[i].memory.u16(0x4)) {
			ShipMemory& s = save_.ships[i].memory;
			const uint16_t convoy = s.u16(0x8);
			bool sink = convoy >= convoys;
			for (uint32_t c = g.u16(kConvoyHead); !sink && c < convoys; c = save_.convoys[c].u16(0x8)) {
				sink = c == convoy;
			}
			if (sink) {
				s.s32(0x18) = 0;
			}
		}
		if (p.u16(0xC) < count) {
			return;
		}
		fn_513ec0(k);
		save_.pirates[k].reset();
		save_.pirate_nests[k] = PirateNest{};
		g.u32(kPirates + 4 * uint32_t(k)) = 0;
		return;
	}
	int32_t away = 0;
	int32_t with_captain = 0;
	for (uint32_t i = p.u16(0xC); i < count; i = save_.ships[i].memory.u16(0x4)) {
		ShipMemory& s = save_.ships[i].memory;
		s.u8(0x158) = uint8_t(k);  // 0x51A4C0
		if (s.u8(0x15D) >= merchants) {
			++away;
		}
		if (s.u16(0x42) < captains) {
			++with_captain;
		}
	}
	if (p.u16(0x10) < convoys) {
		return;
	}
	std::vector<GameMemory<8>>& pool = save_.pirate_nests[k].pool;
	for (uint32_t e = 0; e < p.u16(0x8); e = pool[e].u16(0x4)) {
		pool[e].u8(0x6) = pool[e].u8(0x6) >= 4 ? uint8_t(pool[e].u8(0x6) - 4) : 0;
	}
	bool privateer = false;
	uint32_t chosen = 0xFFFFFFFF;
	if (p.u16(0x12) >= count) {
		if ((g.u8(kWorld4) & 3) == 0) {
			tick::alderman_detail::fn_516170(save_, uint32_t(k), 2, false, 0xFA0);
			const uint16_t i = p.u16(0x12);
			ShipMemory& s = save_.ships[i].memory;
			s.s32(0x18) = s.s32(0x14);
			tick::alderman_detail::fn_51a750(save_, i, 2);
			ShipMemory& m = save_.ships[i].memory;
			m.u16(0x40) = uint16_t(fn_5185c0(m));
		}
	} else {
		for (uint32_t i = p.u16(0x12); i < count; i = save_.ships[i].memory.u16(0x6)) {
			ShipMemory& s = save_.ships[i].memory;
			if (s.s32(0x14) - s.s32(0x18) < 1000) {
				s.s32(0x18) = s.s32(0x14);
				if (s.u8(0x15D) < merchants) {
					privateer = true;
					chosen = i;
				} else if (s.u16(0x42) < captains && !privateer) {
					chosen = i;
				}
			} else {
				s.s32(0x18) += 1000;
			}
			if (s.u8(0x15D) >= merchants) {
				--away;
			}
		}
	}
	if (int32_t(kShipsAway[p.u8(0x14) & 7]) + 3 < away) {
		// The first ship at the nest without a captain and of the pirates is
		// deleted; then the walk ends (the game goes on from a local 0xFFFF).
		for (uint16_t* link = &p.u16(0x12); *link < count; link = &save_.ships[*link].memory.u16(0x6)) {
			ShipMemory& s = save_.ships[*link].memory;
			if (s.u16(0x42) >= captains && s.u8(0x15D) >= merchants) {
				const uint16_t i = *link;
				*link = s.u16(0x6);
				s.u16(0x6) = 0xFFFF;
				fn_507fb0(i);
				away = -1;
				break;
			}
		}
	}
	if (with_captain < 1 && p.u16(0x12) < count) {
		ShipMemory& s = save_.ships[p.u16(0x12)].memory;
		if (s.u16(0x42) >= captains) {
			const uint16_t c = tick::events_detail::fn_5097c0(save_, true);
			save_.ships[p.u16(0x12)].memory.u16(0x42) = c;
		}
	}
	if (chosen >= count || (away > 0 && !privateer) || g.u32(kNetwork) != 0 || g.u8(kNoOperations) != 0) {
		return;
	}
	uint8_t op[0x14] = {};
	auto put32 = [&op](size_t o, uint32_t v) { std::memcpy(op + o, &v, 4); };
	put32(0x0, 0xB5);
	put32(0x4, uint32_t(k));
	put32(0x8, uint32_t(p.u8(0x14)) * 100 + 0x78);
	put32(0xC, privateer ? 1 : uint32_t(p.u8(0x14)) + 1);
	tick::enqueue_operation(save_, op, sizeof(op));  // 0x54AA70
}

// 0x514D40 (on pirate k's object; most ships, strength): operation 0xB5, a
// convoy leaves the nest. Walking the ships at the nest, one with a captain
// that 0x4FE150 accepts (else the captain goes, 0x5098B0) and a full hull is
// taken out of the list and the walk ends; a privateer's ship ([+0x15D] a
// merchant) whose time [+0x4C] has passed goes home instead (below). The
// ship taken is a convoy of its own (0x5062F0, on a local head); a pirates'
// one takes with it ships at the nest with a full hull and no captain, up to
// `most`. Every ship of the convoy goes to the nest's approach point in state
// 0x12, armed (0x515750 with `strength`), the pirates' (owner 0xFF), without
// trade route, out of the list by x, with no course, route (0x516840),
// target or wait; the convoy is the pirates' in state 0x12 (mode 2) and goes
// into the list (0x501BE0, 0x50AAE0).
//
// The privateer that goes home: the news for everybody (a letter 0x2A,
// 0x4D6530) about an AI merchant chosen at random (the generator seeded with
// the tick);
// one time in 10 an event 5 in 0x300 ticks (0x4D8CF0: [+0x8] the privateer's
// merchant, [+0x9] and [+0xB] its home town, [+0xA] the AI merchant chosen,
// [+0x10] 4, [+0x16] the ship's [+0x15E]); then the ship stops (0x516840),
// leaves the list by x and is deleted (state 0x11, no hull, 0x507FB0).
void Sailing::fn_514d40(size_t k, uint16_t most, uint16_t strength_arg) {
	GlobalMemory& g = save_.globals;
	GameMemory<0x18>& p = *save_.pirates[k];
	const uint16_t count = ship_count(save_);
	int32_t strength = strength_arg;
	uint16_t taken = 0xFFFF;
	bool privateer = false;
	for (uint16_t* link = &p.u16(0x12); *link < count;) {
		const uint16_t i = *link;
		ShipMemory& s = save_.ships[i].memory;
		uint16_t& captain = s.u16(0x42);
		if (captain >= save_.captain_count()) {
			link = &s.u16(0x6);
			continue;
		}
		if (!tick::events_detail::fn_4fe150(save_.captains[captain])) {
			tick::events_rare_detail::fn_5098b0(save_, captain);
			link = &s.u16(0x6);
			continue;
		}
		if (s.s32(0x18) < s.s32(0x14)) {
			link = &s.u16(0x6);
			continue;
		}
		*link = s.u16(0x6);
		s.u16(0x6) = 0xFFFF;
		taken = i;
		privateer = s.u8(0x15D) < save_.merchant_count;
		if (privateer && s.u32(0x4C) < g.u32(kTick)) {
			GameRandom r;
			r.seed = g.u32(kTick);
			uint8_t ai = 0;
			for (uint32_t m = 0; m < uint32_t(save_.merchant_count); ++m) {
				if (save_.merchants[m].memory.u16(0x8) != 0) {
					++ai;
				}
			}
			uint8_t pick = uint8_t(r.next(ai));
			for (uint8_t m = 0; m <= pick; ++m) {
				if (save_.merchants[m].memory.u16(0x8) == 0) {
					++pick;
				}
			}
			// Unreachable otherwise (the game would read past the merchants):
			// pick is a random AI's index plus the humans at or before it.
			if (pick < save_.merchants.size()) {
				// 0x514EF0..0x514F79: the news 0x2A for everybody about that
				// merchant (home town, [+0x1A], title, rank at home, [+0x1C],
				// [+0x1D]) and the ship's captain's two names, then 0.
				const MerchantMemory& pm = save_.merchants[pick].memory;
				const CaptainMemory& c = save_.captains[captain];
				Letter letter(0x2A);
				letter.entry.u8(5) = pm.u8(0x19);
				letter.entry.u8(8) = pm.u8(0x1A);
				letter.entry.u8(9) = tick::merchants_detail::fn_4f9430(save_, pick);
				letter.entry.u8(0xA) = pm.u8(0x39C + pm.u8(0x19));
				letter.entry.u8(0xB) = pm.u8(0x1C);
				letter.entry.u8(0xC) = pm.u8(0x1D);
				letter.entry.u8(0xD) = c.u8(2);
				letter.entry.u8(0xE) = c.u8(3);
				letter.entry.u8(0xF) = 0;
				send_letter(save_, kLetterToChronicle, letter);
			}
			if (r.next(10) == 0) {
				GameMemory<0x18>& e = save_.events[tick::events_detail::fn_4d8cf0(save_, g.u32(kTick) + 0x300)];
				const uint8_t merchant = s.u8(0x15D);
				const uint8_t home = save_.merchants[merchant].memory.u8(0x19);
				e.u16(0x6) = 5;
				e.u8(0x8) = merchant;
				e.u8(0x9) = home;
				e.u8(0xA) = pick;
				e.u8(0xB) = home;
				e.u8(0x10) = 4;
				e.u32(0xC) = g.u32(kTick);
				e.u8(0x11) = 0;
				e.u16(0x16) = s.u16(0x15E);
			}
			SavedShip& ship = save_.ships[i];
			clear_ship_route(ship);  // 0x516840
			unlink_x(i);
			// 0x453DF0: the sea map (UI).
			ship.memory.u8(0x3C) |= 0x20;
			ship.memory.u16(0x134) = 0x11;
			ship.memory.s32(0x18) = 0;
			ship.memory.u16(0x50) = 0xFFFF;
			ship.memory.u16(0x52) = 0xFFFF;
			fn_507fb0(i);
			return;
		}
		break;
	}
	if (taken >= count) {
		return;
	}
	uint16_t head = 0xFFFF;
	const uint16_t convoy = fn_5062f0(head, taken);
	if (!privateer) {
		uint32_t ships = 1;
		for (uint16_t* link = &p.u16(0x12); *link < count && ships < most;) {
			const uint16_t i = *link;
			ShipMemory& s = save_.ships[i].memory;
			if (s.s32(0x18) >= s.s32(0x14) && s.u16(0x42) >= save_.captain_count()) {
				*link = s.u16(0x6);
				s.u16(0x6) = save_.convoys[convoy].u16(0xA);
				save_.convoys[convoy].u16(0xA) = i;
				++ships;
			} else {
				link = &s.u16(0x6);
			}
		}
	}
	const uint32_t nest = p.u8(0xE);
	for (uint16_t i = save_.convoys[convoy].u16(0xA); i < count; i = save_.ships[i].memory.u16(0x6)) {
		ShipMemory& s = save_.ships[i].memory;
		s.u16(0x1E) = g.u16(kTownTable + 0x34 * nest + 0x20);
		s.u16(0x1C) = 0;
		s.u16(0x22) = g.u16(kTownTable + 0x34 * nest + 0x24);
		s.u16(0x20) = 0;
		s.u16(0x3E) = 0x700;
		s.u16(0x134) = 0x12;
		s.u8(0x15C) = 0;
		fn_515750(save_, i, strength);
		ShipMemory& m = save_.ships[i].memory;
		m.u8(0x0) = 0xFF;
		m.u8(0x1) = 0;
		m.u16(0x8) = convoy;
		m.u32(0x124) = 0;
		m.u32(0x128) = 0;
		m.u32(0x12C) = 0;
		m.u16(0x130) = 0;
		m.u8(0x136) = 0;
		m.u8(0x13B) = 0;
		m.u8(0x13A) = 0;
		if (m.u16(0x132) <= g.u16(0x6DD72A)) {
			// 0x4D4DD0(route, 1) on the trade routes 0x6DD728, which SaveGame
			// doesn't keep (only the player's ships have them). Not checked
			// with the oracle.
			m.u16(0x132) = 0xFFFF;
		}
		if (m.u16(0xA) < count || m.u16(0xC) < count) {
			unlink_x(i);
		}
		m.s32(0x28) = 0;
		m.s32(0x2C) = 0;
		m.s32(0x30) = 0;
		m.u8(0x37) = 0;
		m.u8(0x38) = 0xFF;
		m.u8(0x39) = 0xFF;
		m.u16(0x3A) = 0xFFFF;
		clear_ship_route(save_.ships[i]);  // 0x516840
		m.u16(0x138) = 0;
		m.u32(0x44) = g.u32(kTick);
		m.s32(0x48) = 0x7FFFFFFF;
		m.u16(0x50) = 0xFFFF;
		m.u16(0x52) = 0xFFFF;
		recalculate_cargo(save_.ships[i]);
	}
	GameMemory<0x3C>& c = save_.convoys[convoy];
	c.u8(0x0) = 0xFF;
	c.u16(0x12) = 0x12;
	c.u16(0x14) = 2;
	c.u16(0x16) = 0;
	c.u32(0x18) = 0xFFFF;
	c.u16(0x1C) = 0xFF;
	c.u8(0x1E) = 0;
	c.u8(0x1F) = 0xFF;
	tick::ship_ai_detail::fn_501be0(save_, convoy);
	// 0x50AAE0: into the list of convoys; a guide (0x5017F0) if it has none,
	// and the guide into the list by x.
	save_.convoys[convoy].u16(0x8) = g.u16(kConvoyHead);
	g.u16(kConvoyHead) = convoy;
	if (save_.convoys[convoy].u16(0x10) >= count) {
		fn_5017f0(convoy);
	}
	insert_x(save_.convoys[convoy].u16(0x10), 0xFFFF);
}

// 0x515EA0 (on pirate k's object, the ship): the ship at the end of the
// pirate's list [+0xC] (linked by [+0x4]).
void fn_515ea0(SaveGame& save, size_t k, uint16_t ship) {
	const uint16_t count = ship_count(save);
	if (ship >= count) {
		return;
	}
	uint16_t* link = &save.pirates[k]->u16(0xC);
	while (*link < count) {
		link = &save.ships[*link].memory.u16(0x4);
	}
	*link = ship;
	save.ships[ship].memory.u16(0x4) = 0xFFFF;
}

// 0x513D80 (on pirate k's object: a town number, the difficulty, a number):
// [+0x14] = difficulty & 3, the nest a free town number (0x5159C0), [+0xF] 1
// for a town of kind 3..5 ([+0x31] of the table) or else number % 3 (0, 2 or
// 3); a new pool: entry 0 (0x24), difficulty entries 0x30 and three of 0x1B
// (orientations 0xE0, 0x20, 0xE0); [+0x16] = 1000; difficulty + 1 ships at
// the nest (0x516170 with strength 40 · difficulty + 50: the first of type 1
// with a captain, the others of type 1 + two bits of the number, consumed
// two at a time, without), each with 1 of hull; no convoy and [+0x15] = 0.
void fn_513d80(SaveGame& save, size_t k, uint32_t town, uint32_t difficulty, uint32_t number) {
	GameMemory<0x18>& p = *save.pirates[k];
	const uint32_t n = difficulty & 3;
	p.u8(0x14) = uint8_t(n);
	const uint8_t nest = fn_5159c0(save, town);
	p.u8(0xE) = nest;
	const uint8_t kind = save.globals.u8(kTownTable + 0x34 * nest + 0x31);
	if (kind >= 3 && kind <= 5) {
		p.u8(0xF) = 1;
	} else {
		const uint8_t r = uint8_t(number % 3);
		p.u8(0xF) = r ? uint8_t(r + 1) : 0;
	}
	p.u16(0xA) = 0;
	p.u16(0x8) = 0;
	p.u32(0x4) = 0;
	save.pirate_nests[k].pool.clear();
	fn_513bf0(save, k);
	for (uint32_t i = n; i > 0; --i) {
		save.pirate_nests[k].pool[fn_513bf0(save, k)].u8(0x3) = 0x30;
	}
	for (uint32_t i = 0; i < 3; ++i) {
		GameMemory<8>& e = save.pirate_nests[k].pool[fn_513bf0(save, k)];
		e.u8(0x3) = 0x1B;
		e.u8(0x7) = (i & 1) ? 0x20 : 0xE0;
	}
	p.u16(0x16) = 1000;
	for (uint32_t i = 0; i <= n; ++i) {
		const uint32_t strength = 40 * uint32_t(p.u8(0x14)) + 0x32;
		if (i == 0) {
			tick::alderman_detail::fn_516170(save, uint32_t(k), 1, true, strength);
		} else {
			const uint32_t type = ((number >> 1) & 1) + (number & 1) + 1;
			tick::alderman_detail::fn_516170(save, uint32_t(k), type, false, strength);
			number >>= 2;
		}
		save.ships[p.u16(0x12)].memory.s32(0x18) = 1;
	}
	p.u16(0x10) = 0xFFFF;
	p.u8(0x15) = 0;
}

// A new pirate object in slot k (operator new and its constructor 0x513720):
// [+0xC], [+0x10], [+0x12] 0xFFFF, [+0xE], [+0xF], [+0x14] 0xFF, the rest 0,
// no map nor pool; its pointer in the ships object (here 1).
GameMemory<0x18>& fn_513720(SaveGame& save, size_t k) {
	GameMemory<0x18>& p = save.pirates[k].emplace();
	p.bytes.fill(0);
	p.u16(0xC) = 0xFFFF;
	p.u8(0xE) = 0xFF;
	p.u8(0xF) = 0xFF;
	p.u16(0x10) = 0xFFFF;
	p.u16(0x12) = 0xFFFF;
	p.u8(0x14) = 0xFF;
	save.pirate_nests[k] = PirateNest{};
	save.globals.u32(kPirates + 4 * uint32_t(k)) = 1;
	return p;
}

// 0x5095EB, the end of 0x509250 for a winner that isn't a merchant (the
// operation 0x4E: a player's ship turns pirate; also the mission
// interpreter): with the town in the world, the ship joins the pirate whose
// nest ([+0xE], a number of StadtDaten.ini) is nearest to the town (squared
// distance of the table's [+0x20], [+0x24], the first on a tie) or, with no
// pirate, a new one in slot 0 (0x513720; 0x513D80 with the ship % 40, the
// difficulty and the tick + the ship + the town), at the end of its list
// (0x515EA0; unlike 0x5063C0 the ship's [+0x158] stays). Its course: the
// first waypoint round the town (0x448780, [+0x24] of the navigation) in
// pixels (0x5172A0 without a town). Then [+0x3D] |= 4, [+0x138] = 0, no
// owner and state 2.
void fn_509250_pirates(SaveGame& save, Sailing* sailing, uint16_t ship, uint32_t town) {
	GlobalMemory& g = save.globals;
	if (town >= g.u32(global::kWorldTownCount)) {
		// Checked with the oracle: scenario battle_branches (saves 3, 5, 7), 0x509250 called
		// directly (the operation 0x4E gets its town from the interface).
		return;
	}
	constexpr uint32_t kTownIds = 0x6DE4B8;  // u8 per town: its number in StadtDaten.ini
	const uint32_t row = kTownTable + 0x34 * uint32_t(g.u8(kTownIds + town));
	const uint32_t tx = g.u32(row + 0x20);
	const uint32_t ty = g.u32(row + 0x24);
	uint32_t chosen = 0xFF;
	int32_t best = 0x7FFFFFFF;
	for (uint32_t k = 0; k < 5; ++k) {
		if (g.u32(kPirates + 4 * k) == 0 || !save.pirates[k]) {
			continue;
		}
		const uint32_t nest = kTownTable + 0x34 * uint32_t(save.pirates[k]->u8(0xE));
		const uint32_t dx = uint32_t(g.u16(nest + 0x20)) - tx;
		const uint32_t dy = uint32_t(g.u16(nest + 0x24)) - ty;
		const int32_t d = int32_t(dy * dy + dx * dx);
		if (d < best) {
			chosen = k;
			best = d;
		}
	}
	if (chosen >= 5) {
		fn_513720(save, 0);
		fn_513d80(save, 0, ship % 0x28, save.settings.u8(0x13), g.u32(kTick) + ship + town);
		chosen = 0;
	}
	fn_515ea0(save, chosen, ship);
	SavedShip& s = save.ships[ship];
	if (sailing) {
		const std::vector<uint32_t>& points = sailing->waypoints(int(town));
		// The game reads the first one even if there were none (never in the saves).
		const uint32_t p = points.empty() ? 0 : points[0];
		const int16_t x = NavData::to_pixel(int16_t(p & 0xFFFF));
		const int16_t y = NavData::to_pixel(int16_t(p >> 16));
		sailing->set_destination(s, 0xFFFF, x, y);
	}
	s.memory.u8(0x3D) |= 4;
	s.memory.u16(0x138) = 0;
	s.memory.u8(0x0) = 0xFF;
	s.memory.u16(0x134) = 2;
}

// 0x5063C0 (on the ships object: the target ship, kind): the pirate of event
// 0x1E attacks a ship. A new pirate ship (0x507D60, out of the lists) of
// town 0's size (0x52A8D0) of type 0 for kind % 3 = 0, 1 for 1, and for 2 type
// 3, or 1 if the target is in the shallows (0x673564); full hull, one level
// (two for kind 2), a name (as 0x509A50), its sailors (0x5185C0), a new captain
// whose [+0xB] is (kind % 3 + 2) · 0x33, armed with the mean of the merchants'
// arms (0x515750 with −1). It joins the first pirate (0x515EA0, 0x51A4C0); if
// there is none, pirate 4 is made (0x513720, 0x513D80 with the tick % 40,
// the difficulty and town 0's [+0x4] + the tick). It appears at the target's place (or (0x3D2, 0x317) without
// target) in state 0x12 with the target in [+0x50] and [+0x52], as a convoy of
// its own (0x5062F0) with no wait and the flag 0x40 (attack at once), in the
// list by x after the head of the sea list; [+0x136] |= 0xC0. Returns the
// ship.
uint16_t Sailing::fn_5063c0(uint16_t target, uint32_t kind) {
	GlobalMemory& g = save_.globals;
	const uint16_t count = ship_count(save_);
	int16_t x = 0x3D2, y = 0x317;
	if (target < count) {
		x = save_.ships[target].memory.s16(0x1E);
		y = save_.ships[target].memory.s16(0x22);
	}
	const uint32_t mode = (kind & 0xFFFF) % 3;
	uint32_t type = 0;
	if (mode == 1) {
		type = 1;
	} else if (mode == 2) {
		type = 3;
		if (target < count && fn_50e340(x, y)) {
			type = 1;
		}
	}
	uint16_t index = 0;
	tick::events_mayor_detail::fn_507d60(save_, index, 0, 3);
	tick::events_mayor_detail::fn_52a8d0(save_, 0, index, type);
	{
		ShipMemory& s = save_.ships[index].memory;
		s.s32(0x18) = s.s32(0x14);
	}
	tick::alderman_detail::fn_51a750(save_, index, mode < 2 ? 1 : 2);
	// The name: as 0x509A50 (the game repeats the same sum when the name is
	// past the list, see «Game bugs»).
	tick::events_mayor_detail::fn_509a50(save_, index);
	{
		ShipMemory& s = save_.ships[index].memory;
		s.u8(0x3C) |= 1;
		s.u16(0x40) = uint16_t(fn_5185c0(s));
	}
	const uint16_t c = tick::events_detail::fn_5097c0(save_, true);
	save_.ships[index].memory.u16(0x42) = c;
	save_.captains[c].u8(0xB) = uint8_t((mode + 2) * 0x33);
	int32_t strength = -1;
	fn_515750(save_, index, strength);
	recalculate_cargo(save_.ships[index]);
	for (size_t k = 0; k < 5; ++k) {
		if (g.u32(kPirates + 4 * uint32_t(k)) != 0 && save_.pirates[k]) {
			fn_515ea0(save_, k, index);
			save_.ships[index].memory.u8(0x158) = uint8_t(k);  // 0x51A4C0
			break;
		}
		if (k == 4) {
			fn_513720(save_, 4);
			const uint32_t sum = save_.towns[0].memory.u32(0x4) + g.u32(kTick);
			fn_513d80(save_, 4, g.u32(kTick) % 0x28, save_.settings.u8(0x13), sum);
			fn_515ea0(save_, 4, index);
			save_.ships[index].memory.u8(0x158) = 4;
		}
	}
	ShipMemory& s = save_.ships[index].memory;
	s.u8(0x3D) |= 0x20;
	s.s16(0x1E) = x;
	s.u16(0x134) = 0x12;
	s.u16(0x1C) = 0x7FFF;
	s.u16(0x20) = 0x7FFF;
	s.s16(0x22) = y;
	s.u16(0x50) = target;
	s.u16(0x52) = target;
	const uint16_t convoy = fn_5062f0(g.u16(kConvoyHead), index);
	save_.convoys[convoy].u16(0x16) = 0;
	save_.convoys[convoy].u8(0x14) |= 0x40;
	save_.ships[index].memory.u16(0x138) = 0;
	insert_x(index, save_.sailing_head());
	save_.ships[index].memory.u8(0x136) |= 0xC0;
	return index;
}

}  // namespace p3::data
