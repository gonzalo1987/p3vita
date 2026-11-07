// A pirate convoy attacks a ship (0x50BC40, from the convoy state 0x12,
// 0x507BAE): the two sides become battle convoys and a sea battle is set up
// (0x611CD0, 0x605420). With Sailing::set_battles(true), as the oracle with
// --battles (docs/cloud/battles-2.md §4). And the battles' piece 4: a chase that
// catches its target (0x50B250, from 0x50B030) and pirates at a town the Hanse
// fleet guards (0x50BA80).

#include <cstring>
#include <vector>

#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kPlayerMerchant = 0x6DFC14;

}  // namespace

// 0x50CFB0 (ships object, a ship): the ships of the player stop targeting it
// ([+0x50], else [+0x52]).
void Sailing::fn_50cfb0(uint16_t ship) {
	const uint32_t player = save_.globals.u32(kPlayerMerchant);
	if (player >= uint32_t(save_.merchant_count)) {
		return;
	}
	const uint32_t count = save_.ships.size();
	for (uint32_t i = save_.merchants[player].memory.u16(0xE); i < count; i = save_.ships[i].memory.u16(0x4)) {
		ShipMemory& s = save_.ships[i].memory;
		if (s.u16(0x50) == ship) {
			s.u16(0x50) = 0xFFFF;
		} else if (s.u16(0x52) == ship) {
			s.u16(0x52) = 0xFFFF;
		}
	}
}

// 0x5176C0 (on a ship, the guide of its convoy): a ship without a route takes
// the guide's: position, course, speed, route point and mark; its own copy
// when the guide's route is its own, else the shared one.
void Sailing::fn_5176c0(uint16_t ship, uint16_t guide) {
	SavedShip& s = save_.ships[ship];
	const SavedShip& g = save_.ships[guide];
	if (s.route || !g.route) {
		return;
	}
	for (size_t o : {0x1C, 0x20, 0x28, 0x2C, 0x30}) {
		s.memory.u32(o) = g.memory.u32(o);
	}
	s.memory.u16(0x34) = g.memory.u16(0x34);
	s.memory.u16(0x2) = g.memory.u16(0x2);
	if (g.memory.u8(0x36)) {
		s.memory.u8(0x36) = 1;
		s.set_route(std::make_shared<SeaRoute>(*g.route));
	} else {
		s.memory.u8(0x36) = 0;
		s.set_route(g.route);
	}
}

// 0x505B60 (ships object, a ship): out of the list by x and of the sea list
// (if it isn't there, the last link of the list takes its [+0x6]); no ship of
// the sea list targets it any more. (0x44F940: the sea map is told, UI.)
void Sailing::fn_505b60(uint16_t ship) {
	const uint16_t count = uint16_t(save_.ships.size());
	unlink_x(ship);
	uint16_t* link = &save_.sailing_head();
	while (*link != ship && *link < count) {
		link = &save_.ships[*link].memory.u16(0x6);
	}
	*link = save_.ships[ship].memory.u16(0x6);
	save_.ships[ship].memory.u16(0x6) = 0xFFFF;
	for (uint32_t i = save_.sailing_head(); int32_t(i) < int32_t(count); i = save_.ships[i].memory.u16(0x6)) {
		ShipMemory& s = save_.ships[i].memory;
		if (s.u16(0x50) == ship) {
			s.u16(0x50) = 0xFFFF;
		}
		if (s.u16(0x52) == ship) {
			s.u16(0x52) = 0xFFFF;
		}
	}
}

// 0x50A4E0 (ships object, a convoy): out of the list of the convoys at sea
// [+0xEC] (false if it isn't there) and its guide out of the list by x
// (0x50D040). (0x44F940: the sea map is told, UI.)
bool Sailing::fn_50a4e0(uint16_t convoy) {
	const uint16_t convoys = save_.convoy_count();
	uint16_t* link = &save_.globals.u16(global::kShips + 0xEC);
	while (*link != convoy) {
		if (*link >= convoys) {
			return false;
		}
		link = &save_.convoys[*link].u16(0x8);
	}
	GameMemory<0x3C>& c = save_.convoys[convoy];
	*link = c.u16(0x8);
	c.u16(0x8) = 0xFFFF;
	if (c.u16(0x10) < save_.globals.u16(global::kShipCount)) {
		unlink_x(c.u16(0x10));
	}
	return true;
}

// 0x50BC40 (ships object, the attacking convoy, out of the list already):
// the attacker's guide leaves the list by x, the player's ships forget it
// (0x50CFB0), the convoy goes to state 0x14 and its ships take the guide's
// [+0x1]; the guide forgets its target T. If T is in a convoy, its ships take
// T's [+0x1], state 0x14 (a merchant's) or 0x12, [+0x3C] = 0xFFFF and the
// guide's route (0x5176C0); that convoy leaves the list (its guide the list by
// x) and goes to state 0x14 (if it was 0x12, the player's ships forget its
// guide). Otherwise T leaves the sea (0x505B60) in state 0x14 (or 0x12,
// forgotten by the player's ships) with [+0x3C] = 0xFFFF and becomes a
// convoy (0x5062F0 on a local head) in state 0x14, mode 0x12. Then a new sea
// battle (0x611CD0), both guides out of the list by x, and its setup
// (0x605420) between the two guides.
void Sailing::fn_50bc40(uint16_t convoy) {
	const uint16_t count = uint16_t(save_.ships.size());
	const uint16_t merchants = uint16_t(save_.merchant_count);
	const uint16_t leader = save_.convoys[convoy].u16(0x10);
	unlink_x(leader);
	fn_50cfb0(leader);
	save_.convoys[convoy].u16(0x12) = 0x14;
	const uint8_t mark = save_.ships[leader].memory.u8(0x1);
	for (uint32_t i = save_.convoys[convoy].u16(0xA); i < count; i = save_.ships[i].memory.u16(0x6)) {
		save_.ships[i].memory.u8(0x1) = mark;
	}
	const uint16_t target = save_.ships[leader].memory.u16(0x50);
	save_.ships[leader].memory.u16(0x50) = 0xFFFF;
	save_.ships[leader].memory.u16(0x52) = 0xFFFF;
	const uint16_t other = save_.ships[target].memory.u16(0x8);
	const uint8_t target_mark = save_.ships[target].memory.u8(0x1);
	uint16_t defender;
	if (other < save_.convoy_count()) {
		const uint16_t guide = save_.convoys[other].u16(0x10);
		for (uint32_t i = save_.convoys[other].u16(0xA); i < count;) {
			ShipMemory& s = save_.ships[i].memory;
			const uint32_t next = s.u16(0x6);
			s.u8(0x1) = target_mark;
			s.u16(0x134) = s.u8(0x0) < merchants ? 0x14 : 0x12;
			s.u16(0x3C) = 0xFFFF;
			if (guide < count) {
				fn_5176c0(uint16_t(i), guide);
			}
			i = next;
		}
		uint16_t* link = &save_.globals.u16(global::kShips + 0xEC);
		while (*link != other && *link < save_.convoy_count()) {
			link = &save_.convoys[*link].u16(0x8);
		}
		if (*link == other) {
			*link = save_.convoys[other].u16(0x8);
			save_.convoys[other].u16(0x8) = 0xFFFF;
			if (save_.convoys[other].u16(0x10) < count) {
				unlink_x(save_.convoys[other].u16(0x10));
				// 0x44F940: the sea map is told (UI).
			}
		}
		if (save_.convoys[other].u16(0x12) == 0x12) {
			fn_50cfb0(save_.convoys[other].u16(0x10));
		} else {
			save_.convoys[other].u16(0x12) = 0x14;
		}
		defender = other;
	} else {
		fn_505b60(target);
		// 0x44F940 when the attacker is the player's (UI).
		ShipMemory& s = save_.ships[target].memory;
		if (s.u8(0x0) < merchants) {
			s.u16(0x134) = 0x14;
		} else {
			s.u16(0x134) = 0x12;
			fn_50cfb0(target);
		}
		s.u16(0x3C) = 0xFFFF;
		uint16_t head = 0xFFFF;
		defender = fn_5062f0(head, target);
		GameMemory<0x3C>& c = save_.convoys[defender];
		const ShipMemory& t = save_.ships[target].memory;
		c.u8(0x0) = t.u8(0x0);
		c.u32(0x18) |= 0x400;
		c.u8(0x1) = t.u8(0xE);
		c.u32(0x4) = 0;
		c.u16(0x12) = 0x14;
		c.u16(0x14) = 0x12;
		c.u16(0x16) = 0;
		c.u32(0x18) = 0xFFFFFFFF;
		c.u16(0x1C) = t.u8(0x37);
		c.u8(0x1E) = t.u8(0x39);
		c.u8(0x1F) = t.u8(0x38);
	}
	const uint8_t slot = fn_611cd0();
	unlink_x(save_.convoys[convoy].u16(0x10));
	unlink_x(save_.convoys[defender].u16(0x10));
	const int battle = save_.battles.battle(slot);
	if (battle >= 0) {
		fn_605420(size_t(battle), save_.convoys[convoy].u16(0x10), save_.convoys[defender].u16(0x10), 0, 0);
	}
}

// 0x50B250's count for the operation 0x9F: the ships near the battle on the player's side. With the player the
// chaser's owner, each [+0x50] and [+0x52] of his ships that is a ship within the chase's reach of the chaser
// ([0x6DD7A0 + 0xFC] squared) counts its convoy's ships (or 1); else each of his ships that targets the chaser
// ([+0x50] or [+0x52]) counts its own convoy's ships (or 1).
uint32_t Sailing::chase_witnesses(uint16_t chaser) {
	const uint32_t count = uint32_t(save_.ships.size());
	const uint32_t convoys = save_.globals.u16(global::kShips + 0xF6);
	const uint32_t player = save_.globals.u32(kPlayerMerchant);
	const ShipMemory& c = save_.ships[chaser].memory;
	// The ships of the convoy of ship s, or 1 without one.
	auto ships_of = [&](uint32_t s) -> uint32_t {
		const uint32_t cv = save_.ships[s].memory.u16(0x8);
		if (cv >= convoys || cv >= save_.convoy_count()) {
			return 1;
		}
		uint32_t n = 0;
		for (uint32_t i = save_.convoys[cv].u16(0xA); i < count; i = save_.ships[i].memory.u16(0x6)) {
			++n;
		}
		return uint16_t(n);
	};
	uint32_t total = 0;
	const uint32_t first = save_.merchants[player].memory.u16(0xE);
	if (player == c.u8(0x0)) {
		const uint32_t reach = save_.globals.u16(global::kShips + 0xFC);
		for (uint32_t i = first; i < count; i = save_.ships[i].memory.u16(0x4)) {
			for (size_t field : {0x50, 0x52}) {
				const uint32_t t = save_.ships[i].memory.u16(field);
				if (t >= count) {
					continue;
				}
				const ShipMemory& s = save_.ships[t].memory;
				const int32_t dx = int32_t(int16_t(c.u16(0x1E))) - int16_t(s.u16(0x1E));
				const int32_t dy = int32_t(int16_t(c.u16(0x22))) - int16_t(s.u16(0x22));
				if (dy * dy + dx * dx < int32_t(reach)) {
					total += ships_of(t);
				}
			}
		}
	} else {
		for (uint32_t i = first; i < count; i = save_.ships[i].memory.u16(0x4)) {
			const ShipMemory& s = save_.ships[i].memory;
			if (s.u16(0x50) == chaser || s.u16(0x52) == chaser) {
				total += ships_of(i);
			}
		}
	}
	return total;
}

// 0x50B250's side of a ship: out of the sea (0x505B60) and made a convoy (0x5062F0) in state 0x12 (a pirate's) or
// 0x14 with [+0x14] |= 0x12 (and 0x20 when it raids: [+0x15C]); or its convoy out of the list of those at sea (its
// guide out of the list by x), all its ships out of the list by x and, for a merchant's, the convoy [+0x18] |= 0x20
// in state 0x14, its ships with [+0x3C] |= 0x20, in state 0x14, without targets, taking the guide's route
// (0x5176C0), and [+0x14] |= 0x20 when the guide raids.
void Sailing::chase_side(uint16_t ship) {
	const uint16_t count = uint16_t(save_.ships.size());
	const uint16_t merchants = uint16_t(save_.merchant_count);
	const uint16_t convoys = save_.globals.u16(global::kShips + 0xF6);
	const uint16_t own = save_.ships[ship].memory.u16(0x8);
	if (own >= convoys) {
		uint16_t head = 0xFFFF;
		fn_505b60(ship);
		const uint16_t cv = fn_5062f0(head, ship);
		GameMemory<0x3C>& c = save_.convoys[head];
		(void)cv;
		ShipMemory& s = save_.ships[ship].memory;
		if (s.u8(0x0) >= merchants) {
			c.u16(0x12) = 0x12;
			return;
		}
		c.u8(0x14) |= 0x12;
		c.u16(0x12) = 0x14;
		s.u16(0x134) = 0x14;
		if (s.u8(0x15C)) {
			c.u8(0x14) |= 0x20;
		}
		return;
	}
	uint16_t* link = &save_.globals.u16(global::kShips + 0xEC);
	while (*link != own && *link < convoys) {
		link = &save_.convoys[*link].u16(0x8);
	}
	if (*link == own) {
		*link = save_.convoys[own].u16(0x8);
		save_.convoys[own].u16(0x8) = 0xFFFF;
		if (save_.convoys[own].u16(0x10) < count) {
			unlink_x(save_.convoys[own].u16(0x10));
			// 0x44F940: the sea map is told (UI).
		}
	}
	for (uint32_t i = save_.convoys[own].u16(0xA); i < count; i = save_.ships[i].memory.u16(0x6)) {
		unlink_x(uint16_t(i));
	}
	if (save_.ships[ship].memory.u8(0x0) >= merchants) {
		return;
	}
	GameMemory<0x3C>& c = save_.convoys[own];
	c.u32(0x18) |= 0x20;
	c.u16(0x12) = 0x14;
	const uint16_t guide = c.u16(0x10);
	for (uint32_t i = c.u16(0xA); i < count;) {
		ShipMemory& s = save_.ships[i].memory;
		s.u8(0x3C) |= 0x20;
		s.u16(0x134) = 0x14;
		s.u16(0x50) = 0xFFFF;
		s.u16(0x52) = 0xFFFF;
		if (i != guide && guide < count) {
			fn_5176c0(uint16_t(i), guide);
		}
		i = s.u16(0x6);
	}
	if (guide < count && save_.ships[guide].memory.u8(0x15C)) {
		c.u8(0x14) |= 0x20;
	}
}

// 0x50B250 (ships object; the chaser and its target, from the chase 0x50B030 when it is under 0x190 away): a new
// sea battle (0x611CD0; none free, nothing). When a merchant's ship raids ([+0x15C], not in the state 0x16) and
// catches a merchant's ship that doesn't, with the player in the game, the operation 0x9F {the battle, the ships
// near on the player's side (chase_witnesses)} when there are any. Then both out of the list by x, each side made
// a battle side (chase_side), both out of the list by x again, and the battle's setup (0x605420) between the two
// ships.
void Sailing::fn_50b250(uint16_t chaser, uint16_t target) {
	const uint8_t slot = fn_611cd0();
	if (slot == 0xFF) {
		return;
	}
	const uint32_t merchants = save_.globals.u16(global::kWorldMerchantCount);
	const ShipMemory& c = save_.ships[chaser].memory;
	const ShipMemory& t = save_.ships[target].memory;
	if (c.u8(0x0) < merchants && c.u8(0x15C) && c.u16(0x134) != 0x16 && t.u8(0x0) < merchants && !t.u8(0x15C) &&
		save_.globals.u32(kPlayerMerchant) < merchants) {
		const uint32_t witnesses = chase_witnesses(chaser);
		if (witnesses > 0) {
			std::vector<uint8_t> op(0x14);
			const uint32_t fields[3] = {0x9F, slot, witnesses};
			std::memcpy(op.data(), fields, sizeof(fields));
			save_.queued_operations.push_back(std::move(op));
		}
	}
	unlink_x(chaser);
	unlink_x(target);
	chase_side(chaser);
	chase_side(target);
	unlink_x(chaser);
	unlink_x(target);
	const int battle = save_.battles.battle(slot);
	if (battle >= 0) {
		fn_605420(size_t(battle), chaser, target, 0, 0);
	}
}

// 0x50BA80 (ships object, a raiding ship at a town [+0x39]): the first event 0x24 of the queue for that town (the
// Hanse fleet guards it) and, along the list of the convoys at sea, the first in the state 9 at that town ([+0x1C]),
// which leaves the list (only that event is looked at: the walk stops after its convoys). Without one, false. A new
// sea battle (0x611CD0; none free: the convoy back at the head of the list, false); the ship without a convoy gets
// a free one (0x509AE0 when there is none) of its own, unlisted, in the state 0x14 (0x501680 with 0x10); the
// battle's setup (0x605420) between the ship and the convoy's guide; true.
bool Sailing::fn_50ba80(uint16_t ship) {
	GlobalMemory& g = save_.globals;
	constexpr uint32_t kEventHead = 0x6DD744;
	constexpr uint32_t kEventCount = 0x6DD748;
	const uint16_t convoys = g.u16(global::kShips + 0xF6);
	uint32_t fleet = 0xFFFF;
	for (uint32_t e = g.u16(kEventHead); e < g.u16(kEventCount) && e < save_.events.size();) {
		const auto& event = save_.events[e];
		if (event.u16(0x6) != 0x24 || event.u8(0x14) != save_.ships[ship].memory.u8(0x39)) {
			e = event.u16(0x4);
			continue;
		}
		uint16_t* link = &g.u16(global::kShips + 0xEC);
		while (*link < convoys) {
			GameMemory<0x3C>& c = save_.convoys[*link];
			if (c.u16(0x12) == 9 && c.u16(0x1C) == event.u8(0x14)) {
				fleet = *link;
				*link = c.u16(0x8);
				c.u16(0x8) = 0xFFFF;
			}
			link = &c.u16(0x8);
		}
		break;
	}
	if (fleet >= convoys) {
		return false;
	}
	const uint8_t slot = fn_611cd0();
	if (slot == 0xFF) {
		save_.convoys[fleet].u16(0x8) = g.u16(global::kShips + 0xEC);
		g.u16(global::kShips + 0xEC) = uint16_t(fleet);
		return false;
	}
	ShipMemory& s = save_.ships[ship].memory;
	if (s.u16(0x8) >= convoys) {
		tick::ship_ai_detail::fn_509ae0(save_);
		const uint16_t cv = g.u16(global::kShips + 0xE4);
		s.u16(0x8) = cv;
		g.u16(global::kShips + 0xE4) = save_.convoys[cv].u16(0x8);
		save_.convoys[cv].u16(0x8) = 0xFFFF;
		s.u16(0x134) = 0x14;
		tick::ship_ai_detail::fn_501680(save_, cv, ship, 0x10);
	}
	const int battle = save_.battles.battle(slot);
	if (battle >= 0) {
		fn_605420(size_t(battle), ship, save_.convoys[fleet].u16(0x10), 0, 0);
	}
	return true;
}

}  // namespace p3::data
