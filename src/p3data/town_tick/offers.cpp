// The offers between merchants (the window of another merchant's office, .let 0x2D → *0x6E5550): a list of nodes on
// the heap, *0x6DE584, that the game doesn't keep in the save (SaveGame::offers, the head first). A node (0x14 bytes):
//   [+0x0] the next node (the game's pointer; p3vita keeps 0)   [+0x4] u32 the offer's number   [+0x8] s32 the price
//   asked of B   [+0xC] u8 A, who offers   [+0xD] u8 B, who is offered   [+0xE] u8 the town   [+0xF] u8 what is offered:
//   0 wares (u16 [+0x10] the ware, u16 [+0x12] how many barrels or loads), 1 a ship (u16 [+0x10]), 2 money ([+0x10];
//   also what a finished offer is left as, with 0)
// The operations 0x5F (a new offer: 0x53DA30 → 0x5338A0) and 0x75 (the answer to it, from the buttons of the letters
// 0x1E and 0x1F: 0x53ECB0), and the event 0x13 (0x4E7A14) that ends an offer whose letters are gone. An AI merchant
// keeps no letters: an offer to him ends at its first event, 5 ticks later, with what was held back returned.

#include <algorithm>
#include <cstring>

#include "p3data/mail.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {

namespace ship_ai_detail {
void fn_517770(SaveGame& save, int ship, uint32_t flags);
}  // namespace ship_ai_detail

namespace {

using Offer = GameMemory<0x14>;

constexpr uint32_t kDockedHead = global::kShips + 0xE8;  // u16 0x6DD888: the docked ships' list (next [+0x6])
constexpr uint8_t kInBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};  // 0x672C14

uint32_t word(const uint8_t* operation, size_t offset) {
	uint32_t v;
	std::memcpy(&v, operation + offset, 4);
	return v;
}

uint16_t merchant_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldMerchantCount);
}

uint16_t ship_count(const SaveGame& save) {
	return save.globals.u16(global::kShipCount);
}

int32_t units_of(uint16_t ware, uint16_t amount) {
	return int32_t(amount) * (ware < 24 && kInBarrels[ware] ? 200 : 2000);
}

int32_t whole_units(uint16_t ware, int32_t units) {
	return ware < 24 && kInBarrels[ware] ? units / 200 * 200 : units / 2000 * 2000;
}

// 0x508FC0 (on the ships' object): the ship out of the docked list ([0x6DD888], next [+0x6]), its [+0x6] 0xFFFF; nothing
// if it isn't there.
void fn_508fc0(SaveGame& save, uint16_t ship) {
	GlobalMemory& g = save.globals;
	uint16_t* link = &g.u16(kDockedHead);
	while (*link != ship) {
		if (*link >= ship_count(save)) {
			return;
		}
		link = &save.ships[*link].memory.u16(0x6);
	}
	*link = save.ships[ship].memory.u16(0x6);
	save.ships[ship].memory.u16(0x6) = 0xFFFF;
}

// A ship back into the docked list at its head, docked (state 0) and its [+0x3C] |= 0x20, if it isn't in it ([+0x6]
// 0xFFFF): the end of an offer of a ship (0x5340D0, 0x534330).
void back_to_port(SaveGame& save, uint16_t ship) {
	ShipMemory& s = save.ships[ship].memory;
	if (s.u16(0x6) != 0xFFFF) {
		return;
	}
	s.u16(0x134) = 0;
	s.u8(0x3C) |= 0x20;
	s.u16(0x6) = save.globals.u16(kDockedHead);
	save.globals.u16(kDockedHead) = ship;
}

bool matches(const Offer& o, uint32_t id, uint32_t a, uint32_t b) {
	return o.u32(4) == id && o.u8(0xC) == a && o.u8(0xD) == b;
}

// The merchant's letter of `type` about the offer (its [+0x8] the number, u16 [+0xC] A and [+0xE] B) in his chain
// ([+0xA], next [+0x6]), or −1.
int32_t offer_letter(const SaveGame& save, uint32_t merchant, uint8_t type, uint32_t id, uint32_t a, uint32_t b) {
	if (merchant >= merchant_count(save) || merchant >= save.merchants.size()) {
		return -1;
	}
	const uint32_t count = save.mail.size();
	for (uint32_t i = save.merchants[merchant].memory.u16(0xA); i < count && i < save.globals.u16(0x6DD736);
		i = save.mail[i].u16(6)) {
		const GameMemory<0x10>& e = save.mail[i];
		if (e.u8(4) == type && e.u16(0xC) == a && e.u16(0xE) == b && e.u32(8) == id) {
			return int32_t(i);
		}
	}
	return -1;
}

}  // namespace

namespace offers_detail {

// 0x534090: whether the list has the offer (its number, A and B).
bool fn_534090(const SaveGame& save, uint32_t id, uint32_t a, uint32_t b) {
	return std::any_of(save.offers.begin(), save.offers.end(), [&](const Offer& o) { return matches(o, id, a, b); });
}

// 0x5340D0 (on the list's head: number, A, B): the offer out of the list, what was held back returned to A: wares into
// his office in the town (or, without one, into the first of his ships in the town, docked (state < 4), with room for
// all: [+0x54], its notice 4 and its hold again; with none, lost), a ship back into port (if it isn't there), money
// (to the node's A). A ware past 0x17 or a ship past the count: nothing returned. Nothing for an offer not in it.
void fn_5340d0(SaveGame& save, uint32_t id, uint32_t a, uint32_t b) {
	const auto it = std::find_if(save.offers.begin(), save.offers.end(), [&](const Offer& o) { return matches(o, id, a, b); });
	if (it == save.offers.end()) {
		return;
	}
	const Offer o = *it;
	save.offers.erase(it);
	switch (o.u8(0xF)) {
	case 0: {
		const uint16_t ware = o.u16(0x10);
		if (ware >= 0x18) {
			return;
		}
		const int32_t units = units_of(ware, o.u16(0x12));
		const int office = facilities_detail::fn_5308a0(save, a & 0xFFFF, o.u8(0xE));
		if (office >= 0) {
			save.offices[size_t(office)].memory.s32(4 + 4 * size_t(ware)) += units;
			return;
		}
		if (a >= merchant_count(save)) {
			return;
		}
		for (uint32_t i = save.merchants[a].memory.u16(0xE); i < ship_count(save); i = save.ships[i].memory.u16(0x4)) {
			SavedShip& ship = save.ships[i];
			ShipMemory& s = ship.memory;
			if (s.u8(0x39) != o.u8(0xE) || s.u16(0x134) >= 4 || recalculate_cargo(ship) < units) {
				continue;
			}
			s.s32(0x54 + 4 * size_t(ware)) += units;
			ship_ai_detail::fn_517770(save, int(i), 4);
			recalculate_cargo(ship);
			return;
		}
		return;
	}
	case 1: {
		const uint16_t ship = o.u16(0x10);
		if (ship < ship_count(save)) {
			back_to_port(save, ship);
		}
		return;
	}
	case 2:
		if (o.u8(0xC) < merchant_count(save)) {
			MerchantMemory& m = save.merchants[o.u8(0xC)].memory;
			m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) + o.u32(0x10));
		}
		return;
	default:
		return;
	}
}

// 0x534330 (on the node; 0x534600 finds it by its number, A and B): B accepts. With a price, B must have that much
// money (signed) or nothing happens; B pays it to A. Then what is offered goes to B: money (with the statistics
// [+0x4B8], [+0x4BC] of both for the money and the price), a ship (back into port if it isn't there; out of A's list
// [+0xE] into B's head; its old owner's ship lists 0x4F5900; [+0x3D] |= 4, B's owner; the ships' counts [+0x478] u16,
// [+0x47C] of both; if the ship isn't in A's list, nothing more), wares (into B's office in the town for the price in
// all, 0x5007A0, and the statistics [+0x4A0], [+0x49C]; without an office, they are lost). The node is left with price
// and amount 0 and the kind 2 (its removal returns nothing).
void fn_534330(SaveGame& save, Offer& o) {
	const uint16_t merchants = merchant_count(save);
	if (o.u8(0xD) >= merchants || o.u8(0xC) >= merchants) {
		return;
	}
	MerchantMemory& b = save.merchants[o.u8(0xD)].memory;
	MerchantMemory& a = save.merchants[o.u8(0xC)].memory;
	const int32_t price = o.s32(8);
	if (price > 0 && b.s32(0x0) < price) {
		return;
	}
	b.s32(0x0) = int32_t(uint32_t(b.s32(0x0)) - uint32_t(price));
	a.s32(0x0) = int32_t(uint32_t(a.s32(0x0)) + uint32_t(price));
	auto add = [](MerchantMemory& m, size_t field, uint32_t v) { m.u32(field) += v; };
	switch (o.u8(0xF)) {
	case 2:
		add(b, 0x0, o.u32(0x10));
		add(b, 0x4B8, o.u32(0x10));
		add(a, 0x4BC, o.u32(0x10));
		add(b, 0x4BC, o.u32(8));
		add(a, 0x4B8, o.u32(8));
		break;
	case 1: {
		const uint16_t ship = o.u16(0x10);
		if (ship >= ship_count(save)) {
			break;
		}
		back_to_port(save, ship);
		uint16_t* link = &a.u16(0xE);
		while (*link != ship) {
			if (*link >= ship_count(save)) {
				o.u8(0xF) = 2;
				o.u32(8) = 0;
				o.u32(0x10) = 0;
				return;
			}
			link = &save.ships[*link].memory.u16(0x4);
		}
		ShipMemory& s = save.ships[ship].memory;
		*link = s.u16(0x4);
		s.u16(0x4) = b.u16(0xE);
		b.u16(0xE) = ship;
		events_mayor_detail::fn_4f5900(save, s.u8(0));
		s.u8(0x3D) |= 4;
		s.u8(0) = o.u8(0xD);
		add(b, 0x47C, 1);
		b.u16(0x478) = uint16_t(b.u16(0x478) + 1);
		add(a, 0x47C, 0xFFFFFFFFu);
		a.u16(0x478) = uint16_t(a.u16(0x478) - 1);
		add(b, 0x4BC, o.u32(8));
		add(a, 0x4B8, o.u32(8));
		break;
	}
	case 0: {
		const int office = facilities_detail::fn_5308a0(save, o.u8(0xD), o.u8(0xE));
		if (office < 0) {
			break;
		}
		const uint16_t ware = o.u16(0x10);
		fn_5007a0(save.offices[size_t(office)].memory, ware, units_of(ware, o.u16(0x12)), o.s32(8));
		add(b, 0x4A0, o.u32(8));
		add(a, 0x49C, o.u32(8));
		break;
	}
	default:
		break;
	}
	o.u8(0xF) = 2;
	o.u32(8) = 0;
	o.u32(0x10) = 0;
}

void fn_534600(SaveGame& save, uint32_t id, uint32_t a, uint32_t b) {
	for (Offer& o : save.offers) {
		if (matches(o, id, a, b)) {
			fn_534330(save, o);
			return;
		}
	}
}

}  // namespace offers_detail

using namespace offers_detail;

// The operation 0x5F (0x53DA30 → 0x5338A0): {0x5F, u32 the offer's number, s32 the price asked, u8 A, u8 B, u8 town,
// u8 kind, u32 what (money; a ship; u16 ware and u16 amount)}: A's offer to B, at the head of the list. Its number is
// made unique among the offers of the same A and B (one less while another has it; at 0, nothing: the game would put an
// uninitialized node at the head, as with A or B past the merchants: p3vita adds none). The event 0x13 at tick + 5
// ([+0x8] the number, u32 [+0x10] A, [+0x14] B). What is offered is held back: money (all A has if less, 0 if none),
// a ship (one of the docked list, compared with its convoy [+0x8] ≥ its own number (seems meant: out of a convoy);
// else its number becomes 0xFFFF and nothing more: no letters) out of it into the state 0xD, [+0x3C] |= 0x20, wares
// from A's office in the town and then from his docked ships there (in whole barrels or loads; the node keeps the
// amount asked even if A had less: seems a game bug). Then the letters 0x1E to A and 0x1F to B ([+0x5] the town, [+0x8]
// the number, u16 [+0xC] A, [+0xE] B).
void fn_53da30(SaveGame& save, const uint8_t* operation) {
	const uint16_t merchants = merchant_count(save);
	const uint8_t a = operation[0xC];
	const uint8_t b = operation[0xD];
	if (a >= merchants || b >= merchants) {
		return;  // The game puts an uninitialized node at the list's head here.
	}
	uint32_t id = word(operation, 4);
	for (;;) {
		const bool taken = fn_534090(save, id, a, b);
		if (!taken) {
			break;
		}
		if (--id < 1) {
			return;  // The same as above.
		}
	}
	GlobalMemory& g = save.globals;
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, g.u32(global::kWorldTick) + 5)];
	e.u16(6) = 0x13;
	e.u32(8) = id;
	e.u32(0x10) = a;
	e.u32(0x14) = b;
	Offer node{};
	node.u32(4) = id;
	node.u32(8) = word(operation, 8);
	node.u8(0xC) = a;
	node.u8(0xD) = b;
	node.u8(0xE) = operation[0xE];
	node.u8(0xF) = operation[0xF];
	node.u32(0x10) = word(operation, 0x10);
	save.offers.insert(save.offers.begin(), node);
	Offer& o = save.offers.front();
	const uint8_t town = o.u8(0xE);
	switch (o.u8(0xF)) {
	case 2: {
		MerchantMemory& m = save.merchants[a].memory;
		const int32_t money = m.s32(0x0);
		if (money >= o.s32(0x10)) {
			m.s32(0x0) = money - o.s32(0x10);
		} else if (money > 0) {
			o.s32(0x10) = money;
			m.s32(0x0) = 0;
		} else {
			o.s32(0x10) = 0;
		}
		break;
	}
	case 1: {
		const uint16_t ship = o.u16(0x10);
		if (ship >= ship_count(save)) {
			return;
		}
		ShipMemory& s = save.ships[ship].memory;
		if (s.u16(0x8) < ship) {
			o.u16(0x10) = 0xFFFF;
			return;
		}
		bool docked = false;
		for (uint32_t i = g.u16(kDockedHead); i < ship_count(save); i = save.ships[i].memory.u16(0x6)) {
			if (i == ship) {
				docked = true;
				break;
			}
		}
		if (!docked) {
			o.u16(0x10) = 0xFFFF;
			return;
		}
		fn_508fc0(save, ship);
		s.u8(0x3C) |= 0x20;
		s.u16(0x134) = 0xD;
		s.u16(0x6) = 0xFFFF;
		break;
	}
	case 0: {
		const uint16_t ware = o.u16(0x10);
		int32_t left = units_of(ware, o.u16(0x12));
		const int office = facilities_detail::fn_5308a0(save, a, town);
		if (office >= 0) {
			int32_t& stock = save.offices[size_t(office)].memory.s32(4 + 4 * size_t(ware));
			if (stock >= left) {
				stock -= left;
				left = 0;
			} else {
				const int32_t taken = whole_units(ware, stock);
				left -= taken;
				stock -= taken;
			}
		}
		for (uint32_t i = save.merchants[a].memory.u16(0xE); left > 0 && i < ship_count(save);) {
			SavedShip& ship = save.ships[i];
			ShipMemory& s = ship.memory;
			const uint32_t next = s.u16(0x4);
			if (s.u8(0x39) == town && s.u16(0x134) < 4) {
				int32_t& cargo = s.s32(0x54 + 4 * size_t(ware));
				if (left >= cargo) {
					left -= cargo;
					cargo = 0;
				} else {
					cargo -= left;
					left = 0;
					cargo = whole_units(ware, cargo);
				}
				recalculate_cargo(ship);
				ship_ai_detail::fn_517770(save, int(i), 4);
			}
			i = next;
		}
		break;
	}
	default:
		break;
	}
	Letter letter;
	letter.entry.u8(5) = town;
	letter.entry.u32(8) = id;
	letter.entry.u16(0xC) = a;
	letter.entry.u16(0xE) = b;
	letter.entry.u8(4) = 0x1E;
	send_letter(save, a, letter);
	letter.entry.u8(4) = 0x1F;
	send_letter(save, b, letter);
}

// The operation 0x75 (0x53ECB0): {0x75, u32 the offer's number, u32 A, u32 B, u32 the answer}: the buttons of the
// letters 0x1E and 0x1F (letter_window.hpp). Answer 0: B accepts (0x534600). Then the offer is removed (what is left of
// it back to A, 0x5340D0) and A's letter 0x1E and B's 0x1F about it deleted (0x4D6A50).
void fn_53ecb0(SaveGame& save, const uint8_t* operation) {
	const uint32_t id = word(operation, 4);
	const uint32_t a = word(operation, 8);
	const uint32_t b = word(operation, 0xC);
	if (word(operation, 0x10) == 0) {
		fn_534600(save, id, a, b);
	}
	fn_5340d0(save, id, a, b);
	if (const int32_t letter = offer_letter(save, a, 0x1E, id, a, b); letter >= 0) {
		delete_letter(save, int32_t(a), uint32_t(letter));
	}
	if (const int32_t letter = offer_letter(save, b, 0x1F, id, a, b); letter >= 0) {
		delete_letter(save, int32_t(b), uint32_t(letter));
	}
}

// 0x4E7A14 (the event 0x13): while A still has his letter 0x1E and B his 0x1F about the offer, and the offer is in the
// list, again in 0x40 ticks (returns 1); else both letters deleted, the offer removed (0x5340D0) and the event too.
uint32_t fn_4e7a14(SaveGame& save, uint16_t event) {
	const GameMemory<0x18>& e = save.events[event];
	const uint32_t id = e.u32(8);
	const uint32_t a = e.u32(0x10);
	const uint32_t b = e.u32(0x14);
	const int32_t memo = offer_letter(save, a, 0x1E, id, a, b);
	const int32_t offer = offer_letter(save, b, 0x1F, id, a, b);
	const bool listed = fn_534090(save, id, a, b);
	if (memo >= 0 && offer >= 0 && listed) {
		save.events[event].u32(0) += 0x40;
		return 1;
	}
	if (memo >= 0) {
		delete_letter(save, int32_t(a), uint32_t(memo));
	}
	if (offer >= 0) {
		delete_letter(save, int32_t(b), uint32_t(offer));
	}
	if (listed) {
		fn_5340d0(save, id, a, b);
	}
	return 0;
}

}  // namespace p3::data::tick
