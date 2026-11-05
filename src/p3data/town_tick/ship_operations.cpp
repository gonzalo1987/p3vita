// The player's operations on a ship's crew and captain, which only the
// interface queues (docs/formats/operations.md): 0x04 hire sailors
// (0x537C20), 0x05 dismiss sailors (0x537DD0), 0x06 hire a captain (0x537EE0)
// and 0x07 dismiss the captain (0x538000). Rewritten literally, by offset;
// run_operation (simulation.cpp) calls them. The bytes the viewer queues are
// built in player_actions.cpp.

#include <algorithm>
#include <cstring>
#include <string>

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
namespace alderman_detail {
void fn_51a750(SaveGame& save, int ship, uint8_t n);  // alderman.cpp
}  // namespace alderman_detail

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

// 0x5386C0 (operation 0x0D, 0x5358D5): {0x0D, ship, flag}, the ship menu's «Pirate» / «Civil» (0x494244: the
// flag 1 while the ship's [+0x15C] is 0). A ship out of a convoy, only sailing (the state 0xF): with the flag,
// [+0x3D] |= 0x18, [+0x15C] = 1 and [+0x136] = 0; without it, [+0x3D] |= 0x10 and [+0x15C] = 0. In a convoy the
// convoy's [+0x18] |= 0x1000 and then, without the flag, each of its ships (from the convoy's [+0xA], along
// their [+0x6]) [+0x3C] |= 0x1000 and [+0x15C] = 0; with it, only for a convoy of the ship's owner, sailing
// ([+0x12] 0xF) and with its flag 2, each ship [+0x3C] |= 0x1000 and [+0x15C] = 1.
void fn_5386c0(SaveGame& save, const uint8_t* operation) {
	uint32_t f[3];
	std::memcpy(f, operation, sizeof(f));
	GlobalMemory& g = save.globals;
	const uint16_t ships = g.u16(global::kShipCount);
	if (f[1] >= ships || f[1] >= save.ships.size()) {
		return;
	}
	ShipMemory& s = save.ships[f[1]].memory;
	const uint16_t convoy = s.u16(0x8);
	if (convoy >= g.u16(global::kShips + 0xF6) || convoy >= save.convoys.size()) {
		if (s.u16(0x134) != 0xF) {
			return;
		}
		if (f[2] != 0) {
			s.u8(0x3D) |= 0x18;
			s.u8(0x15C) = 1;
			s.u8(0x136) = 0;
		} else {
			s.u8(0x3D) |= 0x10;
			s.u8(0x15C) = 0;
		}
		return;
	}
	GameMemory<0x3C>& c = save.convoys[convoy];
	c.u32(0x18) |= 0x1000;
	if (f[2] != 0 && (c.u8(0x0) != s.u8(0x0) || c.u16(0x12) != 0xF || !(c.u8(0x14) & 2))) {
		return;
	}
	// The game walks the chain without a limit; past the ships it stops.
	for (uint32_t k = c.u16(0xA), steps = 0; k < ships && k < save.ships.size() && steps <= save.ships.size(); ++steps) {
		ShipMemory& m = save.ships[k].memory;
		m.u16(0x3C) |= 0x1000;
		m.u8(0x15C) = f[2] != 0 ? 1 : 0;
		k = m.u16(0x6);
	}
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

// --- The shipyard (docs/formats/operations.md: 0x0C, 0x2D, 0x2E, 0x2F) ---

namespace {

// 0x672C14: 1 for the wares traded in barrels (200), 0 for loads (2000).
constexpr uint8_t kInBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};

// 0x66E030 + 0x18 · type: per level, the loads of hold it takes, and the
// timber, pitch and tools it needs (the table's last two numbers aren't read
// here).
constexpr int32_t kUpgrade[4][4] = {{3, 2, 1, 1}, {5, 3, 2, 2}, {8, 4, 3, 3}, {10, 5, 4, 4}};
constexpr uint8_t kUpgradeWares[3] = {0xB, 0xF, 0xC};  // timber, pitch, tools
// 0x672CF0: two bytes for each 3 · type + level (the window's slots).
constexpr uint8_t kUpgradeSlots[13][2] = {{0, 0}, {4, 0}, {4, 2}, {0, 0}, {4, 0}, {2, 4}, {0, 4}, {0, 6}, {2, 8},
	{0, 6}, {2, 8}, {0, 0xC}, {0, 0}};

constexpr uint32_t kNames = 0x6DDAA0;  // The names object.

// strnlen: the length of a text of at most `most` bytes.
size_t text_length(const char* text, size_t most) {
	size_t n = 0;
	while (n < most && text[n] != 0) {
		++n;
	}
	return n;
}

uint32_t ship_name_base(const SaveGame& save) {
	return save.globals.u16(kNames + 0xD8);
}

// 0x512D40 (on the names object: the name, &index): the index of a ship name:
// one of the game's ([+0xD8] of them), one of those the players wrote (after
// them, up to [+0xDA]) or, if it is neither, a new one of the players'.
// Adding it grows the offsets' table ([+0xDC], by 0x20) and the text's
// buffer ([+0xE0], by 0x100) when they are near full, and sets [+0xE4] (0 if
// found).
void fn_512d40(SaveGame& save, const std::string& name, uint16_t& index) {
	GlobalMemory& g = save.globals;
	const uint32_t base = ship_name_base(save);
	for (uint32_t k = 0; k < base; ++k) {
		if (k < save.ship_names.size() && save.ship_names[k] == name) {
			index = uint16_t(k);
			g.u32(kNames + 0xE4) = 0;
			return;
		}
	}
	const uint32_t players = uint32_t(g.u16(kNames + 0xDA) - base) & 0xFFFF;
	std::vector<std::string>& added = save.player_ship_names;
	for (uint32_t k = 0; k < players; ++k) {
		if (k < added.size() && added[k] == name) {
			index = uint16_t(base + k);
			g.u32(kNames + 0xE4) = 0;
			return;
		}
	}
	if (players + 1 >= g.u16(kNames + 0xDC)) {
		g.u16(kNames + 0xDC) = uint16_t(g.u16(kNames + 0xDC) + 0x20);
	}
	uint32_t offset = 0;  // Where the new name starts in the buffer.
	for (uint32_t k = 0; k < players && k < added.size(); ++k) {
		offset += uint32_t(added[k].size() + 1);
	}
	offset &= 0xFFFF;
	if (int32_t(offset + 0x18) >= g.s32(kNames + 0xE0)) {
		g.s32(kNames + 0xE0) += 0x100;
	}
	g.u16(kNames + 0xDA) = uint16_t(g.u16(kNames + 0xDA) + 1);
	index = uint16_t(base + players);
	added.resize(players);
	added.push_back(name);
	g.u32(kNames + 0xE4) = 1;
}

// 0x512FC0 (on the names object: text, index): if the index is the players'
// last name and it was just added ([+0xE4]), the text goes at its end.
bool fn_512fc0(SaveGame& save, const std::string& text, uint16_t index) {
	GlobalMemory& g = save.globals;
	if (index != uint16_t(g.u16(kNames + 0xDA) - 1) || g.u32(kNames + 0xE4) == 0) {
		return false;
	}
	const uint32_t last = uint32_t(g.u16(kNames + 0xDA) - ship_name_base(save)) & 0xFFFF;
	if (last == 0 || last > save.player_ship_names.size()) {
		return true;  // Unreachable: [+0xE4] is only set with a name added.
	}
	save.player_ship_names[last - 1] += text;
	return true;
}

// 0x512B20 (on the names object): a ship name by index: the game's, the
// players', and past them the word of a type by (~index) & 3 (0x6C0CB4:
// «snaikka»...).
std::string fn_512b20(const SaveGame& save, uint16_t index) {
	static constexpr const char* kTypeWords[4] = {"snaikka", "crayer", "cog", "hulk"};
	const uint32_t base = ship_name_base(save);
	if (index < base) {
		return index < save.ship_names.size() ? save.ship_names[index] : std::string();
	}
	if (index < save.globals.u16(kNames + 0xDA)) {
		const size_t k = index - base;
		return k < save.player_ship_names.size() ? save.player_ship_names[k] : std::string();
	}
	return kTypeWords[~uint32_t(index) & 3];
}

}  // namespace

// 0x53CCE0 (operations 0x2D and 0x2E): {code, 12 characters, u32 ship}: the
// ship's name, sent by the window in pieces of 12. 0x2D: those 12 (as a
// name: 0x512D40 into [+0x15E]) are the name [+0x160]. 0x2E: they go at its
// end, in the names object too: at the end of its last name if the ship's
// index is it (0x512FC0), or as a new name made of the index's name and them
// (0x512B20, 0x512D40; that copy takes the operation's text up to its 0,
// past the 12 characters into the ship's number). The name [+0x160] up to
// 30 characters; the ship's notice 1 (0x517770).
void fn_53cce0(SaveGame& save, const uint8_t* operation) {
	uint32_t ship;
	std::memcpy(&ship, operation + 0x10, 4);
	if (ship >= save.globals.u16(global::kShipCount)) {
		return;
	}
	ShipMemory& s = save.ships[ship].memory;
	const char* text = reinterpret_cast<const char*>(operation + 4);
	const std::string piece(text, text_length(text, 12));  // strncpy 12 and a 0
	uint32_t code;
	std::memcpy(&code, operation, 4);
	uint16_t& index = s.u16(0x15E);
	size_t at = 0;
	if (code == 0x2D) {
		fn_512d40(save, piece, index);
		s.u8(0x160) = 0;
	} else {
		if (!fn_512fc0(save, piece, index)) {
			// The operation's text up to its first 0: with 12 characters, the
			// ship's number's bytes after them too (up to its first 0 byte).
			fn_512d40(save, fn_512b20(save, index) + std::string(text, text_length(text, 16)), index);
		}
		while (at < 0x20 && s.u8(0x160 + at) != 0) {
			++at;
		}
		if (at >= 0x20) {
			at = 0x1F;  // A name of 32 characters without its 0: the game writes the 0 past the ship.
		}
	}
	for (size_t k = 0; k < 12 && at < 0x1E && operation[4 + k] != 0; ++k) {
		s.u8(0x160 + at++) = operation[4 + k];
	}
	s.u8(0x160 + at) = 0;
	ship_ai_detail::fn_517770(save, int(ship), 1);
}

// 0x535CB9 (operation 0x2F): {0x2F, merchant, town, ship}: in a town (0x52B110
// on it), the merchant cancels a ship he ordered (in its list [+0x81C], his
// own): the letter 0x14 («Annulment»: [+0xD] its completion's day [+0x4C] << 2
// | its type, [+0xE] the year · 12 + the month − 1) with, if the work hasn't
// started ([+0x18] ≤ 0), nine tenths of its price [+0x48] paid back (out of
// his expenses [+0x4C4]) and the ship deleted (0x507FB0); otherwise −1 and
// nothing.
void fn_535cb9(SaveGame& save, Sailing* sailing, const uint8_t* operation) {
	uint32_t merchant, town, ship;
	std::memcpy(&merchant, operation + 4, 4);
	std::memcpy(&town, operation + 8, 4);
	std::memcpy(&ship, operation + 0xC, 4);
	if (town >= save.globals.u32(global::kWorldTownCount)) {
		return;
	}
	const uint16_t ships = save.globals.u16(global::kShipCount);
	if (ship >= ships) {
		return;
	}
	TownMemory& t = save.towns[town].memory;
	uint16_t i = t.u16(0x81C);
	while (i != ship) {
		if (i >= ships) {
			return;
		}
		i = save.ships[i].memory.u16(0x6);
	}
	ShipMemory& s = save.ships[ship].memory;
	if (merchant >= save.globals.u16(global::kWorldMerchantCount) || s.u8(0) != merchant) {
		return;
	}
	MerchantMemory& m = save.merchants[merchant].memory;
	Letter letter(0x14);
	letter.entry.u8(5) = t.u8(0x2C0);
	uint8_t day = 0, month = 0;
	uint16_t year = 0;
	events_mayor_detail::fn_5311d0(s.u32(0x4C), day, month, year);
	letter.entry.u16(0xE) = uint16_t(year * 12 + uint16_t(uint8_t(month) - 1));
	letter.entry.u8(0xC) = uint8_t(merchant);
	letter.entry.u8(0xD) = uint8_t(uint8_t(day << 2) | (s.u8(0xE) & 3));
	if (s.s32(0x18) > 0) {
		letter.entry.u32(8) = 0xFFFFFFFF;
	} else {
		const int32_t refund = int32_t(uint32_t(s.s32(0x48)) * 9u) / 10;
		letter.entry.s32(8) = refund;
		m.u32(0) += uint32_t(refund);
		m.u32(0x4C4) -= uint32_t(refund);
		if (sailing) {
			sailing->fn_507fb0(uint16_t(ship));
		}
	}
	send_letter(save, int32_t(merchant), letter);
}

// 0x52AAD0 (on the town: ship, flag): the days repairing the ship takes in
// the town's shipyard: its damage ([+0x14] − [+0x18]) by 100 a worker
// ([+0x854]; 600 below 6), rounded up; in a convoy, the most of its ships';
// with the flag 0, a day less before the town's hour of the day (the tick's
// low byte under its index [+0x2C0] · 8 + 7).
int32_t fn_52aad0(const SaveGame& save, int town, int ship, uint32_t flag) {
	const TownMemory& t = save.towns[size_t(town)].memory;
	const int32_t workers = t.u16(0x854);
	const int32_t rate = workers < 6 ? 600 : workers * 100;
	auto days = [&](const ShipMemory& m) {
		return int32_t(uint32_t(m.s32(0x14) - m.s32(0x18)) + uint32_t(rate) - 1u) / rate;
	};
	const ShipMemory& s = save.ships[size_t(ship)].memory;
	int32_t n = 0;
	const uint16_t convoy = s.u16(0x8);
	if (convoy >= save.globals.u16(kConvoyCount)) {
		n = days(s);
	} else {
		const uint16_t ships = save.globals.u16(global::kShipCount);
		for (uint32_t i = save.convoys[convoy].u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
			n = std::max(n, days(save.ships[i].memory));
		}
	}
	if (flag == 0 && int32_t(save.globals.u32(global::kWorldTick) & 0xFF) < int32_t(t.u8(0x2C0)) * 8 + 7) {
		--n;
	}
	return n;
}

// 0x52B670 (on the town: &out, ship, levels): upgrading a merchant's ship
// `levels` levels (up to its level 2; none if it is there already: out all
// 0 and false) in the town's shipyard. Its bytes [+0x42], [+0x43] aren't
// written. For timber, pitch and tools (0x66E030
// per level by type), in whole units: what the merchant has of each in his
// office there and his ships in it (state < 4, not marked [+0x136] & 1)
// [+0x0..], what of it is used [+0xC..], what is bought [+0x18..] (at most
// the town's store: false if short) and its price [+0x24..]. The loads it
// takes [+0x3C], the hold left [+0x38], the days [+0x34] (28 a load, by the
// workers [+0x854], 6 at least, and the repair's, 0x52AAD0 with 1), the
// slots [+0x40], [+0x41] (0x672CF0),
// and the cost [+0x30]: 405 a load, what is bought and the repair (0x52A950).
// False also without levels or if the merchant can't pay it. A game bug,
// maybe: a rate by the shipyard's level [+0x818] (495, 470, 450, 430) is
// written into [+0x30] and overwritten at once.
bool fn_52b670(const SaveGame& save, int town, GameMemory<0x44>& out, uint32_t ship, uint32_t levels) {
	if (uint16_t(ship) >= save.globals.u16(global::kShipCount)) {
		return false;
	}
	const ShipMemory& s = save.ships[uint16_t(ship)].memory;
	if (s.u8(0) >= save.globals.u16(global::kWorldMerchantCount)) {
		return false;
	}
	const TownMemory& t = save.towns[size_t(town)].memory;
	bool ok = true;
	uint32_t n = levels;
	if (int32_t(s.u8(0xF) + (n & 0xFFFF)) > 2) {
		if (s.u8(0xF) >= 2) {
			out = GameMemory<0x44>();  // memset 0x44
			return false;
		}
		n = 2u - s.u8(0xF);
	}
	n &= 0xFFFF;
	if (n == 0) {
		ok = false;
	}
	const int32_t* table = kUpgrade[s.u8(0xE) & 3];
	const int office = facilities_detail::fn_5308a0(save, s.u8(0), t.u8(0x2C0));
	for (size_t k = 0; k < 3; ++k) {
		out.s32(0xC + 4 * k) = office >= 0 ? save.offices[size_t(office)].memory.s32(4 + 4 * size_t(kUpgradeWares[k])) : 0;
	}
	const MerchantMemory& m = save.merchants[s.u8(0)].memory;
	const uint16_t ships = save.globals.u16(global::kShipCount);
	for (uint32_t i = m.u16(0xE); i < ships; i = save.ships[i].memory.u16(0x4)) {
		const ShipMemory& o = save.ships[i].memory;
		if (o.u8(0x39) == t.u8(0x2C0) && o.u16(0x134) < 4 && !(o.u8(0x136) & 1)) {
			for (size_t k = 0; k < 3; ++k) {
				out.s32(0xC + 4 * k) = int32_t(uint32_t(out.s32(0xC + 4 * k)) + uint32_t(o.s32(0x54 + 4 * size_t(kUpgradeWares[k]))));
			}
		}
	}
	for (size_t k = 0; k < 3; ++k) {
		const uint8_t ware = kUpgradeWares[k];
		const int32_t unit = kInBarrels[ware] ? 200 : 2000;
		const int32_t have = out.s32(0xC + 4 * k) / unit;
		out.s32(0xC + 4 * k) = have;
		out.s32(0x0 + 4 * k) = have;
		const int32_t need = int32_t(uint32_t(table[1 + k]) * n);
		if (have >= need) {
			out.s32(0xC + 4 * k) = need;
			out.s32(0x24 + 4 * k) = 0;
			out.s32(0x18 + 4 * k) = 0;
			continue;
		}
		const int32_t buy = need - have;
		out.s32(0x18 + 4 * k) = buy;
		out.s32(0x24 + 4 * k) = game_buy_price(save, ware, t.u8(0x2C0), int32_t(uint32_t(buy) * uint32_t(unit)));
		const int32_t store = t.s32(4 + 4 * size_t(ware)) / unit;
		if (buy > store) {
			ok = false;
			out.s32(0x18 + 4 * k) = store;
		}
	}
	const int32_t loads = int32_t(uint32_t(table[0]) * n);
	out.s32(0x3C) = loads;
	out.s32(0x38) = s.s32(0x10) / 2000 - loads;
	int32_t workers = t.u16(0x854);
	if (workers < 6) {
		workers = 6;
	}
	out.s32(0x34) = int32_t(uint32_t(loads) * 28u + uint32_t(workers) - 1u) / workers;
	out.s32(0x30) = int32_t(uint32_t(loads) * 405u);
	for (size_t k = 0; k < 3; ++k) {
		out.s32(0x30) = int32_t(uint32_t(out.s32(0x30)) + uint32_t(out.s32(0x24 + 4 * k)));
	}
	const size_t slot = size_t(3u * s.u8(0xE) + n + s.u8(0xF));
	if (slot < 13) {
		out.u8(0x40) = kUpgradeSlots[slot][1];
		out.u8(0x41) = kUpgradeSlots[slot][0];
	}
	out.s32(0x30) = int32_t(uint32_t(out.s32(0x30)) + uint32_t(fn_52a950(save, town, int(uint16_t(ship)))));
	out.s32(0x34) = int32_t(uint32_t(out.s32(0x34)) + uint32_t(fn_52aad0(save, town, int(uint16_t(ship)), 1)));
	return ok && !(m.s32(0) < out.s32(0x30));
}

// 0x508660 (ships object: ship, levels; operation 0x0C): a docked ship
// (state ≤ 1) of a merchant in a town upgrades, if 0x52B670 says so: it goes
// from the docked list to the head of the town's repair list [+0x81E], in the
// state 7 ([+0x3C] |= 0x20); the merchant pays (his expenses [+0x4C4]); the
// wares bought leave the town's store (not below 0) and the merchant's own
// leave his office in the town and then his ships there (state < 4 or 7, not
// marked [+0x136] & 1; their cargo isn't recomputed); the ship goes up the
// levels (0x51A750) and its owner gets the reputation of its damage
// (0x4F9120).
void fn_508660(SaveGame& save, uint32_t ship, uint32_t levels) {
	const uint16_t ships = save.globals.u16(global::kShipCount);
	if (ship >= ships || levels == 0) {
		return;
	}
	ShipMemory& s = save.ships[ship].memory;
	const uint32_t town = s.u8(0x39);
	if (town >= save.globals.u32(global::kWorldTownCount) || s.u16(0x134) > 1) {
		return;
	}
	GameMemory<0x44> cost;
	if (!fn_52b670(save, int(town), cost, ship, levels)) {
		return;
	}
	TownMemory& t = save.towns[town].memory;
	uint16_t* link = &save.globals.u16(global::kShips + 0xE8);
	while (*link != ship) {
		if (*link >= ships) {
			return;
		}
		link = &save.ships[*link].memory.u16(0x6);
	}
	*link = s.u16(0x6);
	s.u16(0x6) = t.u16(0x81E);
	t.u16(0x81E) = uint16_t(ship);
	s.u8(0x3C) |= 0x20;
	s.u16(0x134) = 7;
	MerchantMemory& m = save.merchants[s.u8(0)].memory;
	m.u32(0) -= uint32_t(cost.s32(0x30));
	m.u32(0x4C4) += uint32_t(cost.s32(0x30));
	for (size_t k = 0; k < 3; ++k) {
		const uint8_t ware = kUpgradeWares[k];
		int32_t& store = t.s32(4 + 4 * size_t(ware));
		store = int32_t(uint32_t(store) - uint32_t(cost.s32(0x18 + 4 * k)) * uint32_t(kInBarrels[ware] ? 200 : 2000));
		if (store < 0) {
			store = 0;
		}
	}
	// What is used of his own, in whole units of each ware.
	auto take = [&](int32_t& stock, size_t k) {
		const int32_t unit = kInBarrels[kUpgradeWares[k]] ? 200 : 2000;
		int32_t& used = cost.s32(0xC + 4 * k);
		const int32_t units = stock / unit;
		if (units >= used) {
			stock -= used * unit;
			used = 0;
		} else {
			stock -= units * unit;
			used -= units;
		}
	};
	if (cost.s32(0xC) != 0 || cost.s32(0x10) != 0 || cost.s32(0x14) != 0) {
		const int office = facilities_detail::fn_5308a0(save, s.u8(0), s.u8(0x39));
		if (office >= 0) {
			OfficeMemory& o = save.offices[size_t(office)].memory;
			for (size_t k = 0; k < 3; ++k) {
				take(o.s32(4 + 4 * size_t(kUpgradeWares[k])), k);
			}
		}
	}
	for (uint32_t i = m.u16(0xE); i < ships; i = save.ships[i].memory.u16(0x4)) {
		ShipMemory& o = save.ships[i].memory;
		if (o.u8(0x39) != s.u8(0x39) || (o.u16(0x134) >= 4 && o.u16(0x134) != 7) || (o.u8(0x136) & 1)) {
			continue;
		}
		for (size_t k = 0; k < 3; ++k) {
			take(o.s32(0x54 + 4 * size_t(kUpgradeWares[k])), k);
		}
	}
	alderman_detail::fn_51a750(save, int(ship), uint8_t(levels));
	diet_detail::fn_4f9120(save, s.u8(0), s.u8(0x39), uint32_t(s.s32(0x14) - s.s32(0x18)));
}

}  // namespace p3::data::tick
