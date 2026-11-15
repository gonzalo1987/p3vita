// The town hall's orders (the world's seven, 0x6DE530, which the event 0x22 makes again; taken with the operation 0x3D,
// the event 0x11 counts its time): the operation 0x51, the wares delivered for an order taken; and the candidacy for
// mayor, 0x45.

#include <algorithm>
#include <utility>

#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {

namespace ship_ai_detail {
void fn_517770(SaveGame& save, int ship, uint32_t flags);
}  // namespace ship_ai_detail

namespace {

constexpr uint32_t kEventHead = 0x6DD73C + 0x8;   // u16: the event being run
constexpr uint32_t kEventFree = 0x6DD73C + 0xA;   // u16: the next free record (the next new event)
constexpr uint32_t kEventCount = 0x6DD73C + 0xC;  // u16
constexpr uint32_t kOrders = 0x6DE530;            // the world's seven orders, 12 bytes each (events.cpp fn_4db7c0)
constexpr uint8_t kInBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};  // 0x672C14

uint32_t field(const uint8_t* operation, size_t at) {
	uint32_t v = 0;
	for (size_t k = 0; k < 4; ++k) {
		v |= uint32_t(operation[at + k]) << (8 * k);
	}
	return v;
}

}  // namespace

// The operation 0x45 (0x535FA4, inline in the executor): {0x45, u32 merchant, u32 yes}: whether the merchant stands
// for mayor (the town hall's question; 0x4F9390 on him): a human merchant's [+0x118] the answer; an AI merchant's
// always 1 (0x4F9360 reads it). A merchant past the merchants: nothing.
void fn_535fa4(SaveGame& save, const uint8_t* operation) {
	const uint32_t merchant = field(operation, 4);
	if (merchant >= save.globals.u16(global::kWorldMerchantCount)) {
		return;
	}
	MerchantMemory& m = save.merchants[merchant].memory;
	m.u32(0x118) = m.u16(0x8) == 0 ? field(operation, 8) : 1;
}

// The operation 0x51 (0x53C050): {0x51, u32 merchant, -, u32 the event, u32 how many}: wares delivered for an order the
// merchant took: the event (below the events) of the type 0x11 ([+0x8] town, [+0x9] the ware (below 0x14), u16
// [+0x10] how many are left, [+0x14] the price of one, [+0x16] the merchant, [+0x17] not 0: already paid), how many
// at most what is left (barrels or loads). Taken from his ships in the town (his list [+0xE], next [+0x4]; those with
// some of it), then from his office there (none: nothing more, not even the end): from each, all that is asked if it
// has it, else its whole barrels or loads; left and asked down by it, paid price · it ([+0x0], [+0x49C]) unless
// [+0x17]; a ship's hold again (0x5182B0) and its notice 4. After the office, no more. With nothing left the order is
// out of the town's places [+0x830] and [+0x832] that hold the event.
void fn_53c050(SaveGame& save, const uint8_t* operation) {
	const GlobalMemory& g = save.globals;
	const uint32_t merchant = field(operation, 4);
	const uint32_t index = field(operation, 0xC);
	if (merchant >= g.u16(global::kWorldMerchantCount) || index >= g.u16(kEventCount)) {
		return;
	}
	MerchantMemory& m = save.merchants[merchant].memory;
	GameMemory<0x18>& e = save.events[index];
	if (e.u16(6) != 0x11 || e.u8(8) >= g.u32(global::kWorldTownCount) || e.u8(9) >= 0x14 || e.u8(0x16) != merchant) {
		return;
	}
	const uint8_t town = e.u8(8);
	const uint8_t ware = e.u8(9);
	const int32_t scale = kInBarrels[ware] ? 200 : 2000;
	uint32_t asked = std::min<uint32_t>(field(operation, 0x10), e.u16(0x10));
	const uint32_t ships = g.u16(global::kShipCount);
	uint32_t next = m.u16(0xE);
	while (asked > 0) {
		int32_t* source = nullptr;
		int ship = -1;
		if (next < ships) {
			ship = int(next);
			ShipMemory& s = save.ships[next].memory;
			next = s.u16(0x4);
			if (s.u8(0x39) != town || s.s32(0x54 + 4 * ware) <= 0) {
				continue;
			}
			source = &s.s32(0x54 + 4 * ware);
		} else {
			const int office = facilities_detail::fn_5308a0(save, merchant & 0xFFFF, town);
			if (office < 0) {
				return;
			}
			source = &save.offices[size_t(office)].memory.s32(4 + 4 * ware);
		}
		const int32_t units = int32_t(asked) * scale;
		int32_t count = int32_t(asked);
		int32_t taken = units;
		if (*source < units) {
			count = *source / scale;
			taken = count * scale;
		}
		*source -= taken;
		e.u16(0x10) = uint16_t(e.u16(0x10) - count);
		asked -= uint32_t(count);
		if (e.u8(0x17) == 0) {
			const int32_t paid = int32_t(e.u16(0x14)) * count;
			m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) + uint32_t(paid));
			m.s32(0x49C) = int32_t(uint32_t(m.s32(0x49C)) + uint32_t(paid));
		}
		if (ship < 0) {
			break;  // After the office, no more.
		}
		recalculate_cargo(save.ships[size_t(ship)]);
		ship_ai_detail::fn_517770(save, ship, 4);
	}
	if (e.u16(0x10) != 0) {
		return;
	}
	TownMemory& t = save.towns[town].memory;
	for (const size_t place : {size_t(0x830), size_t(0x832)}) {
		if (t.u16(place) == index) {
			t.u16(place) = 0xFFFF;
		}
	}
}

// The operation 0x3D (0x53A620): {0x3D, u8 town, u8 ware, u8 [+0x6], u8 [+0x7], u32 the deadline, u16 how many are
// left, u16 how many in all, u16 the price of one, u8 the merchant, u8 paid at the end}: an order of the town hall
// taken. With the town in the world, the ware below 0x18, the deadline after the tick and a free place in the town
// ([+0x830], else [+0x832]; both taken: nothing), the place holds the next free event, made an event 0x11 at the
// deadline with the operation's 16 bytes from [+0x4] (its [+0x8..+0x17]); the letter 0x1A «Order» to the merchant
// ([+0x5] the town, u16 [+0x8] [+0x6] << 8 | [+0x7], u16 [+0xA] the ware, u16 [+0xC] how many in all, u16 [+0xE] the
// deadline: its day | (its months since the year 0x6DE4A2, the month from 1 less 1) << 5); and the world's orders of
// that town cleared ([+0x0] and [+0x1] 0xFF, [+0x3] 0).
void fn_53a620(SaveGame& save, const uint8_t* operation) {
	GlobalMemory& g = save.globals;
	const uint8_t* o = operation + 4;
	const uint32_t town = o[0];
	const uint32_t deadline = field(operation, 8);
	if (town >= g.u32(global::kWorldTownCount) || o[1] >= 0x18 || deadline <= g.u32(global::kWorldTick)) {
		return;
	}
	TownMemory& t = save.towns[town].memory;
	if (t.u16(0x830) < 0xFFFF && t.u16(0x832) < 0xFFFF) {
		return;
	}
	t.u16(t.u16(0x830) >= 0xFFFF ? 0x830 : 0x832) = g.u16(kEventFree);
	const uint16_t index = events_detail::fn_4d8cf0(save, deadline);
	GameMemory<0x18>& e = save.events[index];
	e.u16(6) = 0x11;
	for (uint32_t k = 0; k < 0x10; ++k) {
		e.u8(8 + k) = o[k];
	}
	Letter letter(0x1A);
	letter.entry.u8(5) = o[0];
	letter.entry.u16(8) = uint16_t((uint32_t(o[2]) << 8) + o[3]);
	letter.entry.u16(0xA) = o[1];
	letter.entry.u16(0xC) = uint16_t(o[0xA] | (o[0xB] << 8));
	uint8_t day = 0;
	uint8_t month = 0;
	uint16_t year = 0;
	events_mayor_detail::fn_5311d0(deadline, day, month, year);
	const uint32_t months = uint32_t(year) * 12 - uint16_t(g.u16(global::kWorldYear) * 12) + month - 1;
	letter.entry.u16(0xE) = uint16_t((months << 5) | (day & 0x1F));
	send_letter(save, o[0xE], letter);
	for (uint32_t k = 0; k < 7; ++k) {
		const uint32_t order = kOrders + 12 * k;
		if (g.u8(order) == town) {
			g.u8(order) = 0xFF;
			g.u8(order + 1) = 0xFF;
			g.u8(order + 3) = 0;
		}
	}
}

// The event 0x11 (0x4E6744), an order taken, at its deadline: with its town ([+0x8]) in the world and how many in all
// (u16 [+0x12]) not 0, out of the town's places [+0x830] and [+0x832] that hold it; then, once, its merchant ([+0x16],
// below the merchants) judged. Wares left (u16 [+0x10]): paid at the end ([+0x17]), the letter 0x5B «Dishonoured» to a
// human one ([+0x5] the town, u16 [+0x8] how many in all, [+0xA] the ware, [+0xC] [+0xA] << 8 | [+0xB], [+0xE] his
// [+0x1C] << 8 | [+0x1D]) and his standing (s16 [+0x22]) down 66 while above −30000; not paid at the end, his
// standing (below 30000) up by (all − left) · 61 / all; [+0x10] 0. Nothing left: his standing up 61 (below 30000) and,
// paid at the end, the price ([+0x14]) · all into his money ([+0x0], [+0x4C8]) with the letter 0x5A «Fulfilled» to a
// human one (u16 [+0x8] the price, [+0xA] the ware, [+0xC] how many in all, [+0xE] his names). [+0x16] 0xFF. Then a
// day at a time the town gets a barrel or a load of the ware ([+0x4 + 4w] up 200 or 2000) while how many in all,
// counted down, lasts (the event again a day later; 0 when it ends).
uint32_t fn_4e6744(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	GameMemory<0x18>& e = save.events[event];
	const uint32_t town = e.u8(8);
	if (town >= g.u32(global::kWorldTownCount) || e.u16(0x12) == 0) {
		return 0;
	}
	TownMemory& t = save.towns[town].memory;
	for (const size_t place : {size_t(0x830), size_t(0x832)}) {
		if (t.u16(place) == g.u16(kEventHead)) {
			t.u16(place) = 0xFFFF;
		}
	}
	const uint32_t merchant = e.u8(0x16);
	if (merchant < g.u16(global::kWorldMerchantCount)) {
		MerchantMemory& m = save.merchants[merchant].memory;
		const bool human = m.u16(0x8) == 0;
		const uint16_t names = uint16_t((uint32_t(m.u8(0x1C)) << 8) + m.u8(0x1D));
		const int16_t standing = int16_t(m.u16(0x22));
		if (e.u16(0x10) != 0) {
			if (e.u8(0x17) != 0) {
				if (human) {
					Letter letter(0x5B);
					letter.entry.u8(5) = e.u8(8);
					letter.entry.u16(8) = e.u16(0x12);
					letter.entry.u16(0xA) = e.u8(9);
					letter.entry.u16(0xC) = uint16_t((uint32_t(e.u8(0xA)) << 8) + e.u8(0xB));
					letter.entry.u16(0xE) = names;
					send_letter(save, int32_t(merchant), letter);
				}
				if (standing > -30000) {
					m.u16(0x22) = uint16_t(standing - 66);
				}
			} else if (standing < 30000) {
				const int32_t all = e.u16(0x12);
				const int32_t done = all - int32_t(e.u16(0x10));
				m.u16(0x22) = uint16_t(int32_t(standing) + done * 61 / all);
			}
			e.u16(0x10) = 0;
		} else {
			if (standing < 30000) {
				m.u16(0x22) = uint16_t(standing + 61);
			}
			if (e.u8(0x17) != 0) {
				const uint32_t paid = uint32_t(e.u16(0x14)) * e.u16(0x12);
				m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) + paid);
				m.s32(0x4C8) = int32_t(uint32_t(m.s32(0x4C8)) + paid);
				if (human) {
					Letter letter(0x5A);
					letter.entry.u8(5) = e.u8(8);
					letter.entry.u16(8) = e.u16(0x14);
					letter.entry.u16(0xA) = e.u8(9);
					letter.entry.u16(0xC) = e.u16(0x12);
					letter.entry.u16(0xE) = names;
					send_letter(save, int32_t(merchant), letter);
				}
			}
		}
		e.u8(0x16) = 0xFF;
	}
	if (e.u16(0x12) == 0) {
		return 0;
	}
	const uint32_t ware = e.u8(9);
	t.s32(4 + 4 * ware) = int32_t(uint32_t(t.s32(4 + 4 * ware)) + (ware < 24 && kInBarrels[ware] ? 200u : 2000u));
	e.u32(0) += 0x100;
	e.u16(0x12) = uint16_t(e.u16(0x12) - 1);
	return e.u16(0x12) != 0 ? 1 : 0;
}

// The operation 0x44 (0x53AD90): {0x44, u32 town, u32 ship, u32 the pay}: a ship in the town's service (its guard ship,
// which the town pays [+0x990] each week: core.cpp). With the town in the world: its guard ship ([+0x994], below the
// ships) if in the state 8 goes back to the state 3 ([+0x3C] |= 0xA0, [+0x30] half its speed 0x5170F0, [+0x138] 0x20),
// and [+0x994] 0xFFFF; then the ship given (below the ships) docked in that town ([+0x39]) in the state 0 or 1 and out
// of a convoy ([+0x8] not below the convoys) goes into the state 8 ([+0x3C] |= 0xA0, [+0x30] half its speed, [+0x138]
// 0), the town's [+0x990] the pay and [+0x994] the ship.
void fn_53ad90(SaveGame& save, const uint8_t* operation) {
	const GlobalMemory& g = save.globals;
	const uint32_t town = field(operation, 4);
	if (town >= g.u32(global::kWorldTownCount)) {
		return;
	}
	TownMemory& t = save.towns[town].memory;
	const uint16_t ships = g.u16(global::kShipCount);
	const uint16_t guard = t.u16(0x994);
	if (guard < ships) {
		SavedShip& ship = save.ships[guard];
		ShipMemory& s = ship.memory;
		if (s.u16(0x134) == 8) {
			s.u8(0x3C) = uint8_t(s.u8(0x3C) | 0xA0);
			s.u16(0x134) = 3;
			s.u32(0x30) = Sailing::speed(ship) >> 1;
			s.u16(0x138) = 0x20;
		}
		t.u16(0x994) = 0xFFFF;
	}
	const uint32_t given = field(operation, 8);
	if (given >= ships) {
		return;
	}
	SavedShip& ship = save.ships[given];
	ShipMemory& s = ship.memory;
	if (s.u8(0x39) != town || s.u16(0x134) > 1 || s.u16(0x8) < g.u16(global::kShips + 0xF6)) {
		return;
	}
	s.u8(0x3C) = uint8_t(s.u8(0x3C) | 0xA0);
	s.u16(0x134) = 8;
	s.u32(0x30) = Sailing::speed(ship) >> 1;
	s.u16(0x138) = 0;
	t.u32(0x990) = field(operation, 0xC);
	t.u16(0x994) = uint16_t(given);
}

// The operation 0x47 (0x53AF30): {0x47, u32 merchant, u32 town, u32 into the town, u32 amount}: money between the
// merchant (the mayor's window 0x5E8380) and the town's treasury [+0x700]. With both in the world: into the town, the
// town's account 0x529F90 of the kind 3 and the merchant pays ([+0x0] down, [+0x4BC] up); out of it, at most what the
// treasury has (0 when it has none), the account of the kind 9 and the merchant gets it ([+0x0], [+0x4B8] up).
void fn_53af30(SaveGame& save, const uint8_t* operation) {
	const GlobalMemory& g = save.globals;
	const uint32_t merchant = field(operation, 4);
	const uint32_t town = field(operation, 8);
	if (merchant >= g.u16(global::kWorldMerchantCount) || town >= g.u32(global::kWorldTownCount)) {
		return;
	}
	MerchantMemory& m = save.merchants[merchant].memory;
	uint32_t amount = field(operation, 0x10);
	if (field(operation, 0xC) != 0) {
		fn_529f90(save, int(town), int32_t(amount), 3);
		m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) - amount);
		m.s32(0x4BC) = int32_t(uint32_t(m.s32(0x4BC)) + amount);
		return;
	}
	const int32_t treasury = save.towns[town].memory.s32(0x700);
	if (uint32_t(treasury) < amount) {
		amount = treasury <= 0 ? 0 : uint32_t(treasury);
	}
	fn_529f90(save, int(town), int32_t(amount), 9);
	m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) + amount);
	m.s32(0x4B8) = int32_t(uint32_t(m.s32(0x4B8)) + amount);
}

namespace town_hall_detail {

// 0x4DA060 (on the queue: &the event, &may vote, town, merchant): the first event 0x2E (the town council's vote) of
// the town among those due by the end of the day (tick | 0xFF; past them, 0xFFFF), and whether the merchant, a human
// one, is still to vote in it: his bit (1, doubled for each human merchant before him) in its [+0x13].
std::pair<uint16_t, bool> fn_4da060(const SaveGame& save, uint16_t town, uint16_t merchant) {
	const GlobalMemory& g = save.globals;
	const uint32_t limit = g.u32(global::kWorldTick) | 0xFF;
	uint16_t index = g.u16(kEventHead);
	while (index < g.u16(kEventCount)) {
		const GameMemory<0x18>& e = save.events[index];
		if (e.u32(0) > limit) {
			return {0xFFFF, false};
		}
		if (e.u16(6) == 0x2E && e.u8(0xC) == town) {
			const uint16_t merchants = g.u16(global::kWorldMerchantCount);
			if (merchant >= merchants || save.merchants[merchant].memory.u16(0x8) != 0) {
				return {index, false};
			}
			uint32_t bit = 1;
			for (uint32_t p = 0; p < merchant; ++p) {
				if (save.merchants[p].memory.u16(0x8) == 0) {
					bit += bit;
				}
			}
			return {index, (uint8_t(bit) & e.u8(0x13)) != 0};
		}
		index = e.u16(4);
	}
	return {index, false};
}

}  // namespace town_hall_detail

using town_hall_detail::fn_4da060;

// The operation 0x49 (0x53B0D0): {0x49, u32 merchant, u32 town, u32 the vote}: a human merchant's vote in the town
// council (the town hall's window 0x5E8280): in the town's vote of the day (0x4DA060) he is still to vote in, the vote
// weighs by his rank in the town (1 below 5, 2 for 5 and 6, 3 above) into [+0x10] (0: for), [+0x11] (1: against) or
// [+0x12] (else); his bit out of [+0x13]; and each of the town's councillors [+0x6DC..+0x6DF] that is he adds one to
// [+0xE] (0), [+0xF] (1) or [+0x12] (else).
void fn_53b0d0(SaveGame& save, const uint8_t* operation) {
	const GlobalMemory& g = save.globals;
	const uint32_t merchant = field(operation, 4);
	const uint32_t town = field(operation, 8);
	const uint32_t vote = field(operation, 0xC);
	const auto [index, may] = fn_4da060(save, uint16_t(town), uint16_t(merchant));
	if (index >= g.u16(kEventCount) || !may) {
		return;
	}
	GameMemory<0x18>& e = save.events[index];
	const uint32_t rank = save.merchants[merchant].memory.u8(0x39C + (town & 0xFFFF));
	const uint8_t weight = rank > 6 ? 3 : rank < 5 ? 1 : 2;
	const size_t into = vote == 0 ? 0x10 : vote == 1 ? 0x11 : 0x12;
	e.u8(into) = uint8_t(e.u8(into) + weight);
	uint8_t bit = 1;
	for (uint32_t p = 0; p < merchant; ++p) {
		if (save.merchants[p].memory.u16(0x8) == 0) {
			bit = uint8_t(bit + bit);
		}
	}
	if (e.u8(0x13) & bit) {
		e.u8(0x13) = uint8_t(e.u8(0x13) - bit);
	}
	const TownMemory& t = save.towns[town & 0xFFFF].memory;  // (Its event's town: the low half matched.)
	for (uint32_t k = 0; k < 4; ++k) {
		if (t.u8(0x6DC + k) == merchant) {
			const size_t seat = vote == 0 ? 0xE : vote == 1 ? 0xF : 0x12;
			e.u8(seat) = uint8_t(e.u8(seat) + 1);
		}
	}
}

// The operation 0x3E (0x53A810): {0x3E, u32 merchant, u32 town, u32 the price}: the merchant moves his home town (the
// window 0x5DD520). With both in the world, the town not his home town, the merchant without [+0x8] & 4 and the town
// with [+0x2C8] & 0x40: the letter 0x0E «Move» to the chronicle ([+0x5] the new town, u16 [+0x8] the merchant, [+0xA]
// the old one, [+0xC] his title 0x4F9430 << 3 | his rank in the old one & 7, [+0xE] 0); his home town [+0x19] the new
// one; the mayor of the old one, if he is it and it has [+0x2C8] & 0xE0, no more ([+0x6F1] 0xFF, [+0x6E4] 6) and his
// rank there again (0x4F75F0); he pays ([+0x0] down, [+0x4BC] up); the two towns' siege objects told (0x635040: the
// old one left, the new one arrived at); the Alderman's town the new one if he is the Alderman of the old one.
void fn_53a810(SaveGame& save, const uint8_t* operation) {
	GlobalMemory& g = save.globals;
	const uint32_t merchant = field(operation, 4);
	const uint32_t town = field(operation, 8);
	if (merchant >= g.u16(global::kWorldMerchantCount) || town >= g.u32(global::kWorldTownCount)) {
		return;
	}
	MerchantMemory& m = save.merchants[merchant].memory;
	const uint8_t old = m.u8(0x19);
	if (old == town || (m.u8(0x8) & 4) || !(save.towns[town].memory.u32(0x2C8) & 0x40)) {
		return;
	}
	Letter letter(0x0E);
	letter.entry.u8(5) = uint8_t(town);
	letter.entry.u16(8) = uint16_t(merchant);
	letter.entry.u16(0xA) = old;
	const uint8_t rank = m.u8(0x39C + old) & 7;
	const uint8_t title = merchants_detail::fn_4f9430(save, int(merchant));
	letter.entry.u16(0xE) = 0;
	letter.entry.u16(0xC) = uint16_t(rank + uint32_t(title) * 8);
	send_letter(save, kLetterToChronicle, letter);
	m.u8(0x19) = uint8_t(town);
	TownMemory& was = save.towns[old].memory;
	if (was.u8(0x6F1) == merchant && (was.u32(0x2C8) & 0xE0)) {
		was.u8(0x6F1) = 0xFF;
		was.u8(0x6E4) = 6;
		merchants_detail::fn_4f75f0(save, int(merchant), old);
	}
	const uint32_t price = field(operation, 0xC);
	m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) - price);
	m.s32(0x4BC) = int32_t(uint32_t(m.s32(0x4BC)) + price);
	sieges_detail::fn_635040(save, old, false);
	sieges_detail::fn_635040(save, int(town), true);
	if (merchant == g.u8(0x6DE52D) && old == g.u8(0x6DE52E)) {
		g.u8(0x6DE52E) = uint8_t(town);  // The Alderman's town ([+0x8E] of the world).
	}
}

}  // namespace p3::data::tick
