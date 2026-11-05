// The player's operations on a ship's crew and captain, which only the
// interface queues (docs/formats/operations.md): 0x04 hire sailors
// (0x537C20), 0x05 dismiss sailors (0x537DD0), 0x06 hire a captain (0x537EE0)
// and 0x07 dismiss the captain (0x538000). Rewritten literally, by offset;
// run_operation (simulation.cpp) calls them. The bytes the viewer queues are
// built in player_actions.cpp.

#include <cstring>

#include "p3data/game_fixes.hpp"
#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {

namespace ship_trade_detail {
bool fn_526aa0(SaveGame& save, int town, uint32_t captain);
}  // namespace ship_trade_detail
namespace ship_ai_detail {
int32_t fn_5184f0(SaveGame& save, int ship);
}  // namespace ship_ai_detail

namespace {

constexpr uint32_t kConvoyCount = global::kShips + 0xF6;  // u16 (0x6DD896)

uint32_t word(const uint8_t* operation, size_t offset) {
	uint32_t v;
	std::memcpy(&v, operation + offset, 4);
	return v;
}

// 0x4F6BD0 (on the merchant, town, amount): the quota goes down by the
// sailors hired (to 0 at least).
void fn_4f6bd0(const SaveGame& save, MerchantMemory& m, uint32_t town, uint32_t amount) {
	if (town >= save.globals.u32(global::kWorldTownCount)) {
		return;  // Unreachable: 0x537C20 checks the ship's town first.
	}
	uint8_t& quota = m.u8(0xF0 + town);
	quota = amount < quota ? uint8_t(quota - amount) : 0;
}

// 0x526B00 (on the town, &captain): the captain to the head of the town's
// list of captains ([+0x82E], next [+0x0]); the field gets the captain's old
// link (0xFFFF for a captain on a ship).
void fn_526b00(SaveGame& save, int town, uint16_t& field) {
	const uint16_t c = field;
	if (c >= save.globals.u16(global::kCaptainCount)) {
		return;  // Unreachable: 0x538000 checks the captain first.
	}
	CaptainMemory& record = save.captains[c];
	field = record.u16(0);
	uint16_t& head = save.towns[size_t(town)].memory.u16(0x82E);
	record.u16(0) = head;
	head = c;
}

}  // namespace

// 0x4F6CA0 (on the merchant, town): the sailors the merchant may hire in the
// town: his quota there ([+0xF0 + town]) but at most the town's unemployed
// sailors ([+0x2E4]) less one; none if fewer than 2. The tavern's window
// shows it too (player_actions.cpp).
uint8_t fn_4f6ca0(const SaveGame& save, const MerchantMemory& m, uint32_t town) {
	const int32_t n = save.towns[town].memory.s32(0x2E4) - 1;
	if (n < 1) {
		return 0;
	}
	const uint8_t quota = m.u8(0xF0 + town);
	return int32_t(quota) >= n ? uint8_t(n) : quota;
}

namespace diet_detail {
void fn_4f9120(SaveGame& save, uint32_t merchant, uint32_t town, uint32_t hull);
}  // namespace diet_detail

// 0x52A9E0 (on the town, the ship): what repairing the ship costs in the
// town's shipyard: the damage (its hull [+0x14] less what is left [+0x18], at
// least the hull / 100) · its capacity [+0x10] · a rate by the town's
// [+0x818] (below 3: 330; below 8: 315; below 13: 300; up to 20: 270; above:
// 285), · 16 / (hull · 2000), / 16, + 1, in 64 bits.
int32_t fn_52a9e0(const SaveGame& save, int town, int ship) {
	const ShipMemory& s = save.ships[size_t(ship)].memory;
	const int32_t hull = s.s32(0x14);
	int64_t damage = int32_t(uint32_t(hull) - uint32_t(s.s32(0x18)));
	const int64_t least = hull / 100;
	if (damage <= least) {
		damage = least;
	}
	const uint64_t product = uint64_t(int64_t(s.s32(0x10))) * uint64_t(damage);
	const float level = save.towns[size_t(town)].memory.f32(0x818);
	uint32_t rate;
	if (!(level < 8.0f)) {
		if (!(level < 13.0f)) {
			// Above 20 dearer again: a game bug (0x52AA87 tests the other way
			// round: the rates go down with the level, 330 to 270). Fixed
			// behind fix_game_bugs(): 285 up to 20, 270 above.
			rate = !(level > 20.0f) ? 270 : 285;
			if (fix_game_bugs()) {
				rate = level > 20.0f ? 270 : 285;
			}
		} else {
			rate = 300;
		}
	} else {
		rate = !(level < 3.0f) ? 315 : 330;
	}
	const int64_t numerator = int64_t((product * rate) << 4);
	const int64_t denominator = int32_t(uint32_t(hull) * 2000u);
	if (denominator == 0) {
		return 1;  // The game divides by 0 (a ship without hull: unreachable).
	}
	return int32_t(uint32_t((numerator / denominator) >> 4) + 1);
}

// 0x52ACD0 (on the town, the ship): the ship, or its whole convoy, into the
// town's shipyard. The cost is 0x52A9E0's (the sum over the convoy's ships).
// A ship of no merchant goes in free; an AI merchant ([+0x8] ≠ 0) always pays
// (also into debts); a human gets the letter 0x02 («No repair»: no damage,
// or not enough money: [+0xC] the cost) and the ship stays docked (state 0,
// false); otherwise he pays. Paying: the money, his expenses [+0x4AC] and
// the reputation for the damage (0x4F9120); the ship in the state 4 with
// [+0x138] 0 (true).
bool fn_52acd0(SaveGame& save, int town, int ship) {
	ShipMemory& s = save.ships[size_t(ship)].memory;
	const uint16_t ships = save.globals.u16(global::kShipCount);
	const uint16_t convoy = s.u16(0x8);
	const bool in_convoy = convoy < save.globals.u16(kConvoyCount);
	int32_t cost = 0;
	if (!in_convoy) {
		cost = fn_52a9e0(save, town, ship);
	} else {
		for (uint16_t i = save.convoys[convoy].u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
			cost = int32_t(uint32_t(cost) + uint32_t(fn_52a9e0(save, town, i)));
		}
	}
	const uint8_t owner = s.u8(0);
	if (owner < save.globals.u16(global::kWorldMerchantCount)) {
		int32_t damaged = 0;  // What is left of the hulls.
		int32_t whole = 0;  // The hulls.
		if (in_convoy) {
			for (uint16_t i = save.convoys[convoy].u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
				damaged += save.ships[i].memory.s32(0x18);
				whole += save.ships[i].memory.s32(0x14);
			}
		} else {
			damaged = s.s32(0x18);
			whole = s.s32(0x14);
		}
		MerchantMemory& m = save.merchants[owner].memory;
		if (m.u16(0x8) == 0) {
			if (damaged >= whole || m.s32(0) < cost) {
				Letter letter(0x02);
				letter.entry.u8(5) = save.towns[size_t(town)].memory.u8(0x2C0);
				letter.entry.u16(8) = s.u16(0x15E);
				letter.entry.u16(0xA) = in_convoy ? 4 : s.u8(0xE);
				letter.entry.u32(0xC) = damaged >= whole ? 0 : uint32_t(cost);
				send_letter(save, owner, letter);
				s.u16(0x134) = 0;
				return false;
			}
			// The game builds here a letter 0x01 (0x52AF01..0x52AF5B: the
			// type, or 4 for a convoy; the hull left in %; the cost) and never
			// sends it: a game bug, fixed behind fix_game_bugs().
			if (fix_game_bugs() && whole / 100 != 0) {
				Letter letter(0x01);
				letter.entry.u8(5) = save.towns[size_t(town)].memory.u8(0x2C0);
				letter.entry.u16(8) = in_convoy ? 4 : s.u8(0xE);
				letter.entry.u16(0xA) = uint16_t(damaged / (whole / 100));
				letter.entry.u32(0xC) = uint32_t(cost);
				send_letter(save, owner, letter);
			}
		}
		m.u32(0) -= uint32_t(cost);
		m.u32(0x4AC) += uint32_t(cost);
		diet_detail::fn_4f9120(save, owner, s.u8(0x39), uint32_t(whole - damaged));
	}
	s.u16(0x134) = 4;
	s.u16(0x138) = 0;
	return true;
}

// 0x52A950 (on the town, the ship): the repair's cost of the ship (0x52A9E0)
// or, in a convoy, of all its ships.
int32_t fn_52a950(const SaveGame& save, int town, int ship) {
	const uint16_t convoy = save.ships[size_t(ship)].memory.u16(0x8);
	if (convoy >= save.globals.u16(kConvoyCount)) {
		return fn_52a9e0(save, town, ship);
	}
	int32_t cost = 0;
	for (uint16_t i = save.convoys[convoy].u16(0xA); i < save.globals.u16(global::kShipCount);
		 i = save.ships[i].memory.u16(0x6)) {
		cost = int32_t(uint32_t(cost) + uint32_t(fn_52a9e0(save, town, i)));
	}
	return cost;
}

// 0x539990 (operation 0x1D): {0x1D, convoy}: a convoy in its town ([+0x1C])
// into the shipyard: its state [+0x12] (under 4) to 4, and the owner of its
// first ship pays the cost (0x52A950 on the first ship; no check of his
// money nor of his kind), his expenses [+0x4AC] and the reputation for the
// damage of all its ships (0x4F9120). The ships' states don't change. The
// game builds a letter 0x01 (the hulls' average %, the cost) and never sends
// it.
void fn_539990(SaveGame& save, const uint8_t* operation) {
	const uint32_t convoy = word(operation, 4);
	if (convoy >= save.globals.u16(kConvoyCount)) {
		return;
	}
	auto& c = save.convoys[convoy];
	const uint16_t town = c.u16(0x1C);
	if (town >= save.globals.u32(global::kWorldTownCount) || c.u16(0x12) >= 4) {
		return;
	}
	c.u16(0x12) = 4;
	const uint16_t ships = save.globals.u16(global::kShipCount);
	const uint16_t first = c.u16(0xA);
	if (first >= ships || save.ships[first].memory.u8(0) >= save.merchants.size()) {
		return;  // The game doesn't look (an empty convoy, a convoy of no one: never queued).
	}
	const uint8_t owner = save.ships[first].memory.u8(0);
	const int32_t cost = fn_52a950(save, town, first);
	uint32_t damage = 0;
	uint16_t percent = 0;  // The letter's: each ship's hull left in %.
	uint32_t count = 0;
	for (uint16_t i = first; i < ships; i = save.ships[i].memory.u16(0x6)) {
		const ShipMemory& s = save.ships[i].memory;
		if (s.s32(0x14) != 0) {
			percent = uint16_t(percent + s.s32(0x18) * 100 / s.s32(0x14));
		}
		damage += uint32_t(s.s32(0x14) - s.s32(0x18));
		++count;
	}
	MerchantMemory& m = save.merchants[owner].memory;
	m.u32(0) -= uint32_t(cost);
	m.u32(0x4AC) += uint32_t(cost);
	diet_detail::fn_4f9120(save, owner, town, damage);
	// The letter 0x01 (0x539A3F..0x539AE7: 4, the average % of the hulls, the
	// cost) is built and never sent: a game bug, fixed behind fix_game_bugs().
	if (fix_game_bugs()) {
		Letter letter(0x01);
		letter.entry.u8(5) = uint8_t(town);
		letter.entry.u16(8) = 4;
		letter.entry.u16(0xA) = uint16_t(percent / count);
		letter.entry.u32(0xC) = uint32_t(cost);
		send_letter(save, owner, letter);
	}
}

// 0x5357AA (operation 0x03): {0x03, ship}: a docked ship (state < 4) into the
// shipyard of its town (0x52ACD0).
void fn_5357aa(SaveGame& save, const uint8_t* operation) {
	const uint32_t index = word(operation, 4);
	if (index >= save.globals.u16(global::kShipCount)) {
		return;
	}
	const ShipMemory& s = save.ships[index].memory;
	if (s.u8(0x39) >= save.globals.u32(global::kWorldTownCount) || s.u16(0x134) >= 4) {
		return;
	}
	fn_52acd0(save, s.u8(0x39), int(index));
}

// 0x537C20 (operation 0x04): {0x04, ship, amount}: hire sailors for the ship
// in its town, up to its room (0x5184F0) and what the town lets its owner
// hire (0x4F6CA0); the quota goes down (0x4F6BD0), the sailors leave the
// town's unemployed ([+0x2E4], and [+0x2D4]).
void fn_537c20(SaveGame& save, const uint8_t* operation) {
	const uint32_t index = word(operation, 4);
	uint32_t amount = word(operation, 8);
	if (index >= save.globals.u16(global::kShipCount)) {
		return;
	}
	ShipMemory& s = save.ships[index].memory;
	const uint32_t town = s.u8(0x39);
	if (town >= save.globals.u32(global::kWorldTownCount)) {
		return;  // At sea.
	}
	const uint32_t room = uint32_t(ship_ai_detail::fn_5184f0(save, int(index)));
	if (room < amount) {
		amount = room;
	}
	const uint8_t owner = s.u8(0);
	if (owner < save.globals.u16(global::kWorldMerchantCount)) {
		MerchantMemory& m = save.merchants[owner].memory;
		const uint8_t allowed = fn_4f6ca0(save, m, town);
		if (amount > allowed) {
			amount = allowed;
		}
		fn_4f6bd0(save, m, town, amount);
	}
	if (s.u16(0x40) == 0) {
		// The first sailors: the morale's high byte by whether it has a captain.
		s.u8(0x3F) = s.u16(0x42) < save.globals.u16(global::kCaptainCount) ? 4 : 3;
		s.u8(0x3D) |= 2;
		s.u16(0x40) = uint16_t(amount);
	} else {
		s.u16(0x40) = uint16_t(s.u16(0x40) + amount);
	}
	ship_ai_detail::fn_517770(save, int(index), 2);
	const uint16_t convoy = s.u16(0x8);
	if (convoy < save.globals.u16(kConvoyCount)) {
		save.convoys[convoy].u32(0x18) |= 2;
	}
	TownMemory& t = save.towns[town].memory;
	if (t.u32(0x2E4) > amount) {
		t.u32(0x2D4) -= amount;
		t.u32(0x2E4) -= amount;
	} else {
		t.u32(0x2D4) -= t.u32(0x2E4);
		t.u32(0x2E4) = 0;
	}
	recalculate_cargo(save.ships[index]);  // 0x5182B0
}

// 0x537DD0 (operation 0x05): {0x05, ship, amount}: dismiss sailors of a ship
// in port; they join the town's unemployed. All of them if asked for more
// (the morale to 0); otherwise the morale [+0x3E] goes down by amount · 2560
// / (sailors + 1), not below 0.
void fn_537dd0(SaveGame& save, const uint8_t* operation) {
	const uint32_t index = word(operation, 4);
	const uint32_t amount = word(operation, 8);
	if (index >= save.globals.u16(global::kShipCount)) {
		return;
	}
	ShipMemory& s = save.ships[index].memory;
	const uint16_t sailors = s.u16(0x40);
	if (sailors == 0 || s.u16(0x134) >= 4) {
		return;
	}
	const uint32_t town = s.u8(0x39);
	if (town >= save.globals.u32(global::kWorldTownCount)) {
		return;
	}
	TownMemory& t = save.towns[town].memory;
	if (sailors < amount) {
		t.u32(0x2E4) += sailors;
		t.u32(0x2D4) += sailors;
		s.u16(0x40) = 0;
		s.u16(0x3E) = 0;
	} else {
		t.u32(0x2E4) += amount;
		t.u32(0x2D4) += amount;
		const uint32_t drop = amount * 2560u / (uint32_t(sailors) + 1);
		s.u16(0x40) = uint16_t(sailors - amount);
		s.u16(0x3E) = uint16_t(s.u16(0x3E) - drop);
		if (int16_t(s.u16(0x3E)) < 0) {
			s.u16(0x3E) = 0;
		}
	}
	recalculate_cargo(save.ships[index]);  // 0x5182B0
	ship_ai_detail::fn_517770(save, int(index), 0x202);
}

// 0x537EE0 (operation 0x06): {0x06, captain, ship, town}: the ship hires the
// captain from the town's list (0x526AA0): the captain's link gets the ship's
// captain before him and the ship gets him, and its owner the letter 0x04
// («New captain»). It doesn't look whether the ship is in port or has a
// captain. The ship's notice 2 (0x517770) always.
void fn_537ee0(SaveGame& save, const uint8_t* operation) {
	const uint32_t captain = word(operation, 4);
	const uint32_t index = word(operation, 8);
	const uint32_t town = word(operation, 0xC);
	if (index >= save.globals.u16(global::kShipCount) || captain >= save.globals.u16(global::kCaptainCount) ||
		town >= save.globals.u32(global::kWorldTownCount)) {
		return;
	}
	ShipMemory& s = save.ships[index].memory;
	if (ship_trade_detail::fn_526aa0(save, int(town), captain)) {
		save.captains[captain].u16(0) = s.u16(0x42);
		s.u16(0x42) = uint16_t(captain);
		Letter letter(0x04);
		letter.entry.u8(5) = uint8_t(town);
		letter.entry.u16(8) = uint16_t(captain);
		letter.entry.u16(0xA) = uint16_t(index);
		letter.entry.u16(0xC) = 0xFFFF;
		send_letter(save, s.u8(0), letter);
	}
	ship_ai_detail::fn_517770(save, int(index), 2);
}

// 0x538000 (operation 0x07): {0x07, ship}: a ship in port dismisses its
// captain, who goes to the town's list (0x526B00); its owner's [+0x1F] to 0;
// the morale changes by −2560 / (sailors + 1) (to 0 if it goes negative);
// [+0x3C] |= 0x202; a convoy it guides breaks up (0x50A580).
void fn_538000(SaveGame& save, Sailing* sailing, const uint8_t* operation) {
	const uint32_t index = word(operation, 4);
	if (index >= save.globals.u16(global::kShipCount)) {
		return;
	}
	ShipMemory& s = save.ships[index].memory;
	if (s.u16(0x42) >= save.globals.u16(global::kCaptainCount) || s.u16(0x134) >= 4) {
		return;
	}
	const uint32_t town = s.u8(0x39);
	if (town >= save.globals.u32(global::kWorldTownCount)) {
		return;
	}
	const uint8_t owner = s.u8(0);
	if (owner < save.globals.u16(global::kWorldMerchantCount)) {
		save.merchants[owner].memory.u8(0x1F) = 0;
	}
	fn_526b00(save, int(town), s.u16(0x42));
	s.u16(0x3E) = uint16_t(s.u16(0x3E) + int16_t(-2560 / (int32_t(s.u16(0x40)) + 1)));
	if (int8_t(s.u8(0x3F)) < 0) {
		s.u16(0x3E) = 0;
	}
	s.u16(0x3C) |= 0x202;
	const uint16_t convoy = s.u16(0x8);
	if (convoy < save.globals.u16(kConvoyCount) && save.convoys[convoy].u16(0x10) == index && sailing) {
		sailing->fn_50a580(convoy);
	}
}

}  // namespace p3::data::tick
