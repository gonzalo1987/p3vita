// The land transport (the town gate's window 0x5EB3C0): the operation 0x3A sends a cart convoy with a ware from the
// merchant's office in a town to his office in another, and the event 0x0C carries it (0x4E38B4): it leaves, waits a
// day, travels the time of its road, may be ambushed, turns back at a besieged town and arrives. Its letters 0x66
// «Arrival», 0x67 «Return», 0x68 «Ambush» and 0x69 (the prince's troops), and the ticker line 0x29.
//
// The event 0x0C: [+0x8] u8 the town it is in / leaves, [+0x9] u8 where it goes, [+0xA] u8 the merchant, [+0xB] u8 the
// escort's share (escorts · 200 / the amount; its bit 0: the merchant is told), [+0xC] f32 the ware's average price,
// u16 [+0x10] the price paid, u16 [+0x12] the amount (barrels, loads), [+0x14] u8 the ware, [+0x15] u8 the state,
// [+0x16] u8 a counter, [+0x17] u8 the operation's [+0xF].

#include "p3data/game_fixes.hpp"
#include "p3data/mail.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

namespace {

constexpr uint32_t kEventHead = 0x6DD73C + 0x8;   // u16
constexpr uint32_t kEventCount = 0x6DD73C + 0xC;  // u16
constexpr uint32_t kTownIds = 0x6DE4B8;          // u8 per town: its number of StadtDaten.ini
// 0x672C14: 0 loads (2000 units), 1 barrels (200), else pieces (10), by ware (24 of them).
constexpr uint8_t kUnitKind[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
// 0x672BF8: the 7 roads by land, both towns' numbers, a price, and the time in 16 ticks.
constexpr uint8_t kRoads[7][4] = {{3, 4, 80, 32}, {9, 10, 130, 52}, {11, 13, 30, 12}, {12, 14, 40, 16}, {28, 31, 120, 48},
	{36, 37, 60, 24}, {38, 39, 90, 36}};

int32_t unit_of(uint8_t ware) {
	return kUnitKind[ware] == 0 ? 2000 : kUnitKind[ware] == 1 ? 200 : 10;
}

uint16_t field16(const uint8_t* operation, size_t at) {
	return uint16_t(operation[at] | operation[at + 1] << 8);
}

// The letter of the convoy to its merchant: [+0x5] the town, and (for 0x66, 0x67, 0x68) u16 [+0x8] where it was,
// [+0xA] where it went.
void tell(SaveGame& save, const GameMemory<0x18>& e, uint8_t type, uint8_t town, bool towns) {
	Letter letter(type);
	letter.entry.u8(5) = town;
	if (towns) {
		letter.entry.u16(8) = e.u8(8);
		letter.entry.u16(0xA) = e.u8(9);
	}
	send_letter(save, e.u8(0xA), letter);
}

}  // namespace

// The operation 0x3A (0x53A2C0): {0x3A, u8 from, u8 to, u8 merchant, u8 tell him, u16 the amount, u16 the price, u16
// the escorts, u8 the ware, u8 [+0xF]}: an event 0x0C at the tick (made even with no amount: then left as the free
// record was), its fields from the operation, the escorts' share escorts · 200 / amount with the bit 0 to tell him
// (cleared when there are no escorts and the queue's head is an event 0x0C of the same town in the state 1 with its
// counter below 9; the look goes on only while each event's tick − 16 is not below the tick), the state 0; then the
// queue runs at once (0x4D85C0 with the tick): the convoy leaves.
void fn_53a2c0(SaveGame& save, const uint8_t* operation) {
	GlobalMemory& g = save.globals;
	const uint32_t tick = g.u32(global::kWorldTick);
	const uint16_t index = events_detail::fn_4d8cf0(save, tick);
	const uint16_t amount = field16(operation, 8);
	if (amount == 0) {
		return;
	}
	GameMemory<0x18>& e = save.events[index];
	e.u8(8) = operation[4];
	e.u8(9) = operation[5];
	e.u8(0xA) = operation[6];
	uint8_t share = uint8_t((uint32_t(field16(operation, 0xC)) * 200) / amount);
	if (operation[7] != 0) {
		share |= 1;
		if (field16(operation, 0xC) == 0) {
			for (uint32_t i = g.u16(kEventHead); i <= g.u16(kEventCount) && i < save.events.size();) {
				const GameMemory<0x18>& other = save.events[i];
				if (other.u16(6) == 0x0C && other.u8(8) == operation[4] && other.u8(0x15) == 1 && other.u8(0x16) < 9) {
					share &= 0xFE;
				}
				const uint16_t next = other.u16(4);
				if (next >= g.u16(kEventCount) || other.u32(0) - 0x10 < g.u32(global::kWorldTick)) {
					break;
				}
				i = next;
			}
		}
	} else {
		share &= 0xFE;
	}
	e.u8(0xB) = share;
	e.u16(0x12) = amount;
	e.u16(0x10) = field16(operation, 0xA);
	e.u8(0x15) = 0;
	e.u8(0x16) = 0;
	e.u8(0x14) = operation[0xE];
	e.u32(0xC) = 0;
	e.u16(6) = 0x0C;
	e.u8(0x17) = operation[0xF];
	fn_4d85c0(save, g.u32(global::kWorldTick));
}

// 0x4E38B4 (the event 0x0C, a cart convoy; returns 1 to keep it). By its state [+0x15]:
//   0, leaving: with the merchant's office in the town [+0x8] (else removed) and the destination in the world (else
//     removed), a ware below 0x18 taken from the office: all the amount if it has it; else all its whole units (loads,
//     barrels, pieces of 10) and the amount becomes their count / 2000 (seems a game bug: the office loses them and
//     the convoy carries about nothing; fixed behind fix_game_bugs(): the convoy carries their count; docs/roadmap.md);
//     the price [+0xC] the office's average; the merchant pays
//     [+0x10] ([+0x0] down, [+0x4BC] up); then the state 1 in 1 tick.
//   1, waiting in the town: while the town isn't besieged ([+0x2C8] & 0x10), a day (the counter [+0x16] round, 1 tick
//     each), then the state 2; besieged, the letter 0x67 (with its bit 0), it goes back home ([+0x9] = [+0x8]) in the
//     state 6.
//   2 and 4, setting out: the time of the road between the two towns' numbers (0x672BF8, either way; 16 ticks without
//     one), the next state, the counter 0xFF.
//   3 and 5, arriving: at a town not besieged, the amount less escort's share / 2 % of it, with an ambush's letter
//     0x68 when the share is over 1 (its bit 0), and the state 6 (the counter 0xFF); at a besieged one, in the state
//     3 the letter 0x67 and back (the towns swapped, the state 4, 0x41 ticks), in the state 5 the letter 0x69: lost.
//   6, unloading: the counter down to 0, a tick each, then the state 7 in 4 ticks.
//   7, arrived: into the merchant's office in [+0x9] (if any; a ware below 0x18) with the average price ((stock ·
//     average + units · [+0xC]) / the new stock; 0 and the stock 0 if not over 0); arrived elsewhere than it left,
//     the letter 0x66; then (with the bit 0) the ticker line 0x29 ((to << 8) | from): removed.
// The letters (and the town view's carts: 0x570930 on its list of objects, the interface's, not here) only with the
// share's bit 0.
uint32_t fn_4e38b4(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	GameMemory<0x18>& e = save.events[event];
	const uint8_t state = e.u8(0x15);
	const bool told = e.u8(0xB) & 1;
	switch (state) {
	case 0: {
		const int office = facilities_detail::fn_5308a0(save, e.u8(0xA), e.u8(8));
		if (office < 0 || e.u8(9) >= g.u32(global::kWorldTownCount)) {
			return 0;
		}
		OfficeMemory& o = save.offices[size_t(office)].memory;
		const uint8_t ware = e.u8(0x14);
		if (ware < 0x18) {
			const int32_t unit = unit_of(ware);
			const int32_t units = int32_t(e.u16(0x12)) * unit;
			int32_t& stock = o.s32(4 + 4 * ware);
			if (stock >= units) {
				stock -= units;
			} else {
				const int32_t count = stock / unit;
				stock -= count * unit;
				// The game divides the count again (fixed: the units that left the office).
				e.u16(0x12) = uint16_t(fix_game_bugs() ? count : count / 2000);
			}
			e.u32(0xC) = o.u32(0x3B8 + 4 * ware);
		}
		MerchantMemory& m = save.merchants[e.u8(0xA)].memory;
		m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) - e.u16(0x10));
		m.s32(0x4BC) = int32_t(uint32_t(m.s32(0x4BC)) + e.u16(0x10));
		e.u32(0) += 1;
		e.u8(0x15) = 1;
		e.u8(0x16) = 0;
		return 1;
	}
	case 1:
		if (!(save.towns[e.u8(8)].memory.u32(0x2C8) & 0x10)) {
			const uint8_t was = e.u8(0x16);
			e.u8(0x16) = uint8_t(was + 1);
			if (was == 0xFF) {
				e.u8(0x15) = 2;
			}
			e.u32(0) += 1;
			return 1;
		}
		if (told) {
			tell(save, e, 0x67, e.u8(8), true);
		}
		e.u8(0x15) = 6;
		e.u8(9) = e.u8(8);
		e.u32(0) += 1;
		return 1;
	case 2:
	case 4: {
		e.u8(0x16) = 0xFF;
		const uint32_t a = g.u8(kTownIds + e.u8(8));
		const uint32_t b = g.u8(kTownIds + e.u8(9));
		uint32_t time = 0x10;
		for (const auto& road : kRoads) {
			if ((road[0] == a && road[1] == b) || (road[0] == b && road[1] == a)) {
				time *= road[3];
				break;
			}
		}
		e.u32(0) += time;
		e.u8(0x15) = uint8_t(state + 1);
		return 1;
	}
	case 3:
	case 5: {
		if (!(save.towns[e.u8(9)].memory.u32(0x2C8) & 0x10)) {
			const uint8_t share = e.u8(0xB);
			if (share > 1) {
				const uint16_t amount = e.u16(0x12);
				e.u16(0x12) = uint16_t(amount - int32_t(share >> 1) * amount / 100);
				if (share & 1) {
					tell(save, e, 0x68, e.u8(9), true);
				}
			}
			e.u8(0x15) = 6;
			e.u8(0x16) = 0xFF;
			e.u32(0) += 1;
			return 1;
		}
		if (state == 3) {
			if (told) {
				tell(save, e, 0x67, e.u8(9), true);
			}
			const uint8_t from = e.u8(8);
			e.u8(8) = e.u8(9);
			e.u8(9) = from;
			e.u32(0) += 0x41;
			e.u8(0x15) = 4;
			return 1;
		}
		if (told) {
			tell(save, e, 0x69, e.u8(9), false);
		}
		return 0;
	}
	case 6: {
		const uint8_t left = e.u8(0x16);
		e.u8(0x16) = uint8_t(left - 1);
		if (left != 0) {
			e.u32(0) += 1;
			return 1;
		}
		e.u32(0) += 4;
		e.u8(0x15) = 7;
		return 1;
	}
	case 7: {
		const int office = facilities_detail::fn_5308a0(save, e.u8(0xA), e.u8(9));
		if (office >= 0) {
			OfficeMemory& o = save.offices[size_t(office)].memory;
			const uint8_t ware = e.u8(0x14);
			if (ware < 0x18) {
				const int32_t units = int32_t(e.u16(0x12)) * unit_of(ware);
				int32_t& stock = o.s32(4 + 4 * ware);
				const int32_t old = stock;
				const double before = double(o.f32(0x3B8 + 4 * ware)) * double(old);
				stock = old + units;
				const double sum = double(float(units)) * double(e.f32(0xC)) + before;
				if (stock > 0) {
					o.f32(0x3B8 + 4 * ware) = float(sum / double(stock));
				} else {
					o.u32(0x3B8 + 4 * ware) = 0;
					stock = 0;
				}
			}
			if (e.u8(9) != e.u8(8) && told) {
				tell(save, e, 0x66, e.u8(9), true);
			}
		}
		if (told) {
			send_ticker(save, e.u8(0xA), 0x29, uint16_t((e.u8(9) << 8) + e.u8(8)));
		}
		return 0;
	}
	default:
		return 0;
	}
}

}  // namespace p3::data::tick
