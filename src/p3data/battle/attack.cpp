// A pirate convoy attacks a ship (0x50BC40, from the convoy state 0x12,
// 0x507BAE): the two sides become battle convoys and a sea battle is set up
// (0x611CD0, 0x605420). With Sailing::set_battles(true), as the oracle with
// --battles (docs/cloud/battles-2.md §4).

#include "p3data/sailing.hpp"

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

}  // namespace p3::data
