// The people of a town's tavern that the player deals with (the tavern's side menu and windows, 0x5A5xxx..0x5A7xxx and
// 0x5D5xxx..0x5D7xxx): the burglar (the operation 0x4F and the event 0x0F, the letters 0x74, 0x16, 0x20 and 0x15) and
// the traveller (the operation 0x53 and the event 0x10, the letters 0x17 and 0x18). The people taken and left (0x52)
// are in operations.cpp.

#include <algorithm>
#include <cstring>

#include "p3data/mail.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

namespace {

constexpr uint32_t kEventHead = 0x6DD73C + 0x8;   // u16
constexpr uint32_t kEventCount = 0x6DD73C + 0xC;  // u16

uint32_t field(const uint8_t* operation, size_t at) {
	uint32_t v = 0;
	for (size_t k = 0; k < 4; ++k) {
		v |= uint32_t(operation[at + k]) << (8 * k);
	}
	return v;
}

uint16_t field16(const uint8_t* operation, size_t at) {
	return uint16_t(operation[at] | operation[at + 1] << 8);
}

// The end of a traveller's event (0x4E64BE, after 0x4E6476's look for another): unless another event 0x10 of the same
// ship follows in the queue, the ship's travellers [+0x157] one less (to 0 from 1 or 0) and its notice 4.
void traveller_leaves(SaveGame& save, uint16_t event) {
	const GlobalMemory& g = save.globals;
	const uint16_t ship = save.events[event].u16(8);
	for (uint32_t i = save.events[g.u16(kEventHead)].u16(4); i < g.u16(kEventCount); i = save.events[i].u16(4)) {
		if (save.events[i].u16(6) == 0x10 && save.events[i].u16(8) == save.events[g.u16(kEventHead)].u16(8)) {
			return;
		}
	}
	uint8_t& travellers = save.ships[ship].memory.u8(0x157);
	travellers = travellers <= 1 ? 0 : uint8_t(travellers - 1);
	ship_ai_detail::fn_517770(save, int(ship), 4);
}

constexpr int16_t kBurglaryFame = 256;  // 0x672DDA and 0x672DD8 (s16): the fame of hiring the burglar, twice
constexpr uint8_t kInBarrels[16] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1};  // 0x672C14

// A merchant's fame [+0x16] (s16) plus `k`, kept within the s16 (0x53BB24..0x53BB7B, 0x53BBC9..0x53BC18).
void add_fame(MerchantMemory& m, int16_t k) {
	const int32_t fame = m.s16(0x16);
	if (k > 0) {
		m.s16(0x16) = fame < 0x7FFF - k ? int16_t(fame + k) : int16_t(0x7FFF);
	} else {
		m.s16(0x16) = fame + 0x8000 > -int32_t(k) ? int16_t(fame + k) : int16_t(-0x8000);
	}
}

// The game's generator 0x46C350 (seeded by 0x46C340).
uint32_t next(uint32_t& x) {
	x = 0xA205B063u - x * 0x44BF19B3u;
	x = (x & 1) ? (x >> 1) | 0x80000000u : x >> 1;
	return x;
}

// A commission: the event 0x05 (as 0x52's, operations.cpp) at `tick` with the merchant, town, other merchant.
GameMemory<0x18>& commission(SaveGame& save, uint32_t tick, uint8_t merchant, uint8_t town, uint8_t other) {
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, tick)];
	e.u16(6) = 0x05;
	e.u8(8) = merchant;
	e.u8(9) = town;
	e.u8(0xA) = other;
	e.u8(0xB) = save.merchants[merchant].memory.u8(0x19);
	return e;
}

}  // namespace

// The operation 0x4F (0x53BA40): {0x4F, u32 the price, u16 the burglar's draw, u16 town, u16 A (who hires him), u16
// B (his victim), u8 a commission}: the tavern's burglar (the town's [+0x83C]; the window 0x5D68E0). With A or B past
// the merchants, or B with [+0x8] & 4, or B without an office in the town, the burglar free again ([+0x83C] = 0xFF)
// unless A has him; nothing while another has him. Else A pays the price ([+0x0] down, [+0x4BC] up), the burglar
// busy ([+0x83C] = 1), A's fame up twice by 256 (within the s16), the event 0x0F at tick + max(3, draw & 7) days |
// 0xFA ([+0x8] u16 the draw, [+0xA] A, [+0xB] town, [+0xC] B) and, with the commission, an event 0x05 at
// (tick + 0x200) | 0x80 ([+0x8] A, [+0x9] town, [+0xA] B, [+0xB] A's home town, [+0xC] the tick, [+0x10], [+0x11] 0).
// A town past the towns: nothing.
void fn_53ba40(SaveGame& save, const uint8_t* operation) {
	const GlobalMemory& g = save.globals;
	const uint16_t town = field16(operation, 0xA);
	if (town >= g.u32(global::kWorldTownCount)) {
		return;
	}
	TownMemory& t = save.towns[town].memory;
	const uint16_t a = field16(operation, 0xC);
	const uint16_t b = field16(operation, 0xE);
	const uint16_t merchants = g.u16(global::kWorldMerchantCount);
	auto release = [&] {
		if (t.u8(0x83C) != a) {
			t.u8(0x83C) = 0xFF;
		}
	};
	if (a >= merchants || b >= merchants || (save.merchants[b].memory.u8(0x8) & 4)) {
		release();
		return;
	}
	if (t.u8(0x83C) != a && t.u8(0x83C) != 0xFF) {
		return;
	}
	MerchantMemory& m = save.merchants[a].memory;
	if (facilities_detail::fn_5308a0(save, b, town) < 0) {
		t.u8(0x83C) = 0xFF;
		return;
	}
	const int32_t price = int32_t(field(operation, 4));
	m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) - uint32_t(price));
	m.s32(0x4BC) = int32_t(uint32_t(m.s32(0x4BC)) + uint32_t(price));
	t.u8(0x83C) = 1;
	add_fame(m, kBurglaryFame);
	const uint32_t days = std::max(3u, uint32_t(operation[8] & 7));
	const uint32_t tick = g.u32(global::kWorldTick);
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, ((days << 8) + tick) | 0xFA)];
	e.u16(6) = 0x0F;
	e.u8(0xA) = uint8_t(a);
	e.u8(0xB) = uint8_t(town);
	e.u8(0xC) = uint8_t(b);
	e.u16(8) = field16(operation, 8);
	add_fame(m, kBurglaryFame);
	if (operation[0x10] != 0) {
		GameMemory<0x18>& c = commission(save, (tick + 0x200) | 0x80, uint8_t(a), uint8_t(town), uint8_t(b));
		c.u32(0xC) = g.u32(global::kWorldTick);
		c.u8(0x10) = 0;
		c.u8(0x11) = 0;
	}
}

// 0x4E5664 (the event 0x0F, the burglary; then removed): with the town [+0xB], B [+0xC] (below the merchants) and A
// [+0xA] (B not A), the town's burglar back ([+0x83C] = 0) and the outcome n: without an office of B's in the town,
// 0x19A; else, with g his office's most guards ((capacity [+0x2EC] − 50) / 200 + 1, as an s16; 2 at least for an AI
// merchant), the draw's low 10 bits ([+0x8]) · (g − 1) / (his guards [+0x2C7] + g). A past the merchants becomes 0.
// Up to 0x66 (A a merchant): he was caught: a commission (the event 0x05 at the event's tick + (n + 0x20) · 16,
// [+0x10] 3, u16 [+0x12] (draw << 8) + its high byte, [+0x14] draw · tick, [+0x16] 0xFFFF) and the letter 0x74 «Broken
// in» to B (u16 [+0x8] B's names, [+0xA] his rank | 0x100 with guards, [+0xC] A's names, [+0xE] A's title << 8 | his
// rank). Up to 0x19A (or A not a merchant): the letter 0x16 to A (u16 [+0x8], [+0xA] B's names). Over it, the
// burglary: from B with more than 100, money · min(8, (n & 7) + 3) / 100 (at most 50000; [+0x4BC] up), half of it to
// A ([+0x4B8]); with the generator seeded with the tick, of each of B's office's 16 first wares with 200 units or more,
// ((r & 0x1F) + 15) · (stock / 100) units: in barrels / 200, in loads / 2000 · 10, at most 1000 of 200 units, taken
// (r >>= 2 after each); the letters 0x20 to B and 0x15 to A (u16 [+0x8] B's names, [+0xA] the wares taken as bits,
// [+0xC] how many, [+0xE] the money; to A, half). The letters' [+0x5] the town.
void fn_4e5664(SaveGame& save, uint16_t event) {
	const GlobalMemory& g = save.globals;
	const uint16_t merchants = g.u16(global::kWorldMerchantCount);
	{
		const GameMemory<0x18>& e = save.events[event];
		if (e.u8(0xB) >= g.u32(global::kWorldTownCount) || e.u8(0xC) >= merchants || e.u8(0xC) == e.u8(0xA)) {
			return;
		}
	}
	const uint8_t town = save.events[event].u8(0xB);
	const uint8_t b = save.events[event].u8(0xC);
	save.towns[town].memory.u8(0x83C) = 0;
	const int office = facilities_detail::fn_5308a0(save, b, town);
	uint32_t n = 0x19A;
	if (office >= 0) {
		const OfficeMemory& o = save.offices[size_t(office)].memory;
		uint32_t most = uint32_t(int32_t(int16_t((int32_t(o.u16(0x2EC)) - 50) / 200 + 1)));
		if (save.merchants[b].memory.u16(0x8) != 0 && most <= 1) {
			most = 2;
		}
		n = uint32_t(int32_t(save.events[event].u16(8) & 0x3FF) * int32_t(most - 1)) / (uint32_t(o.u8(0x2C7)) + most);
	}
	Letter letter;
	letter.entry.u8(5) = town;
	if (save.events[event].u8(0xA) >= merchants) {
		save.events[event].u8(0xA) = 0;
	} else if (n <= 0x66) {
		// Caught: a commission and the letter 0x74 to B.
		const uint8_t a = save.events[event].u8(0xA);
		const uint32_t at = save.events[event].u32(0) + ((n + 0x20) << 4);
		GameMemory<0x18>& c = commission(save, at, a, town, b);
		const GameMemory<0x18>& e = save.events[event];
		c.u32(0xC) = e.u32(0);
		c.u8(0x10) = 3;
		c.u8(0x11) = 0;
		c.u16(0x12) = uint16_t((e.u16(8) << 8) + e.u8(9));
		c.u16(0x14) = uint16_t(e.u16(8) * e.u16(0));
		c.u16(0x16) = 0xFFFF;
		const MerchantMemory& mb = save.merchants[b].memory;
		const MerchantMemory& ma = save.merchants[a].memory;
		letter.entry.u8(4) = 0x74;
		letter.entry.u16(8) = uint16_t((mb.u8(0x1C) << 8) + mb.u8(0x1D));
		letter.entry.u16(0xA) = mb.u8(0x39C + mb.u8(0x19));
		if (save.offices[size_t(office)].memory.u8(0x2C7) > 0) {
			letter.entry.u8(0xB) |= 1;
		}
		letter.entry.u16(0xC) = uint16_t((ma.u8(0x1C) << 8) + ma.u8(0x1D));
		letter.entry.u16(0xE) = uint16_t((merchants_detail::fn_4f9430(save, a) << 8) + ma.u8(0x39C + ma.u8(0x19)));
		send_letter(save, b, letter);
		return;
	}
	if (n <= 0x19A) {
		// Failed: the letter 0x16 to A.
		const MerchantMemory& mb = save.merchants[b].memory;
		letter.entry.u8(4) = 0x16;
		letter.entry.u16(8) = mb.u8(0x1C);
		letter.entry.u16(0xA) = mb.u8(0x1D);
		send_letter(save, save.events[event].u8(0xA), letter);
		return;
	}
	// The burglary.
	MerchantMemory& mb = save.merchants[b].memory;
	MerchantMemory& ma = save.merchants[save.events[event].u8(0xA)].memory;
	letter.entry.u16(8) = uint16_t((mb.u8(0x1C) << 8) + mb.u8(0x1D));
	letter.entry.u16(0xA) = 0;
	letter.entry.u16(0xC) = 0;
	const int32_t money = mb.s32(0x0);
	if (money > 100) {
		const int32_t part = std::min<int32_t>(8, int32_t(n & 7) + 3);
		int32_t stolen = int32_t(uint32_t(money) * uint32_t(part)) / 100;
		stolen = std::min(stolen, 50000);
		mb.s32(0x0) = money - stolen;
		mb.s32(0x4BC) = int32_t(uint32_t(mb.s32(0x4BC)) + uint32_t(stolen));
		letter.entry.u16(0xE) = uint16_t(stolen);
		ma.s32(0x0) = int32_t(uint32_t(ma.s32(0x0)) + uint32_t(stolen >> 1));
		ma.s32(0x4B8) = int32_t(uint32_t(ma.s32(0x4B8)) + uint32_t(stolen >> 1));
	} else {
		letter.entry.u16(0xE) = 0;
	}
	uint32_t r = g.u32(global::kWorldTick);
	r = next(r);
	OfficeMemory& o = save.offices[size_t(office)].memory;
	for (uint32_t w = 0; w < 16; ++w) {
		int32_t& stock = o.s32(4 + 4 * w);
		if (stock < 200) {
			continue;
		}
		const int32_t units = int32_t(r & 0x1F) * 1 + 15;
		const int32_t base = units * (stock / 100);
		int32_t amount = kInBarrels[w] ? base / 200 : base / 2000 * 10;
		if (amount > 1000) {
			amount = 1000;
		}
		if (amount > 0) {
			stock -= amount * 200;
			letter.entry.u16(0xA) = uint16_t(letter.entry.u16(0xA) | (1u << w));
			letter.entry.u16(0xC) = uint16_t(letter.entry.u16(0xC) + amount);
		}
		r >>= 2;
	}
	letter.entry.u8(4) = 0x20;
	send_letter(save, b, letter);
	letter.entry.u16(0xE) = uint16_t(letter.entry.u16(0xE) >> 1);
	letter.entry.u8(4) = 0x15;
	send_letter(save, save.events[event].u8(0xA), letter);
}

// The operation 0x53 (0x53C840): {0x53, u16 ship, u16 the payment, u32 the tick to arrive by, u8 the traveller's
// town, u8 his destination}: the tavern's traveller goes with the player's ship (the window 0x5D6AE0). With both towns
// and the ship in the world, and to a destination with [+0x2C8] & 1 only on a ship of the types 0 and 1 (else
// nothing): the event 0x10 at tick + 4 ([+0x8] ship, [+0xA] town, [+0xB] destination, u16 [+0xC] payment, [+0x10] the
// tick), the town's traveller taken ([+0x83F] = 0), the letter 0x17 to the ship's owner ([+0x5] town, u16 [+0x8] the
// destination, [+0xA] the day to arrive by, month << 5 + day (0x5311D0), [+0xC] the payment, [+0xE] the ship), the
// ship's travellers [+0x157] one more (0 from 0x7F) and its notice 4. With the ship or the destination past: the
// town's traveller free again ([+0x83F] = 0xFF).
void fn_53c840(SaveGame& save, const uint8_t* operation) {
	const GlobalMemory& g = save.globals;
	const uint32_t towns = g.u32(global::kWorldTownCount);
	const uint8_t town = operation[0xC];
	if (town >= towns) {
		return;
	}
	const uint16_t ship = field16(operation, 4);
	const uint8_t destination = operation[0xD];
	if (ship >= g.u16(global::kShipCount) || destination >= towns) {
		if (field(operation, 0) == 0x53) {
			save.towns[town].memory.u8(0x83F) = 0xFF;
		}
		return;
	}
	ShipMemory& s = save.ships[ship].memory;
	if ((save.towns[destination].memory.u32(0x2C8) & 1) && s.u8(0xE) >= 2) {
		return;
	}
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, g.u32(global::kWorldTick) + 4)];
	e.u16(6) = 0x10;
	e.u16(8) = ship;
	e.u8(0xA) = town;
	e.u8(0xB) = destination;
	e.u16(0xC) = field16(operation, 6);
	e.u32(0x10) = field(operation, 8);
	save.towns[town].memory.u8(0x83F) = 0;
	uint8_t day = 0;
	uint8_t month = 0;
	uint16_t year = 0;
	events_mayor_detail::fn_5311d0(e.u32(0x10), day, month, year);
	Letter letter(0x17);
	letter.entry.u8(5) = town;
	letter.entry.u16(8) = destination;
	letter.entry.u16(0xA) = uint16_t((month << 5) + day);
	letter.entry.u16(0xC) = field16(operation, 6);
	letter.entry.u16(0xE) = ship;
	send_letter(save, s.u8(0), letter);
	s.u8(0x157) = uint8_t(s.u8(0x157) + 1);
	if (s.u8(0x157) >= 0x7F) {
		s.u8(0x157) = 0;
	}
	ship_ai_detail::fn_517770(save, int(ship), 4);
}

// 0x4E63E4 (the event 0x10, a traveller aboard; returns 1 to keep it): with the ship [+0x8] past the ships, removed.
// At sea, while the ship isn't sunk or fleeing (0x11, 0x12, 0x17 and over), again in 0x20 ticks; else he is gone. In a
// town other than his destination [+0xB], again in 0x100 ticks until the day [+0x10]; past it, the letter 0x18 «2»
// ([+0x8] 2: he went on by land) and he is gone. At his destination, while the ship is leaving or entering (states 2,
// 3), again in 0x40 − [+0x138] ticks (1 at least); then the letter 0x18: by the day, [+0x8] 0 and the payment [+0xC];
// late by n days (rounded up), within 4 [+0x8] 1 and payment · (5 − n) / 5, later [+0x8] 2 and nothing; the owner (a
// merchant) paid ([+0x0], [+0x4B8]) and the letter sent (none to an owner past the merchants). Gone: traveller_leaves.
// The letters' [+0x5] the ship's town, u16 [+0xC] the traveller's town [+0xA].
uint32_t fn_4e63e4(SaveGame& save, uint16_t event) {
	const GlobalMemory& g = save.globals;
	GameMemory<0x18>& e = save.events[event];
	const uint16_t ship = e.u16(8);
	if (ship >= g.u16(global::kShipCount)) {
		return 0;
	}
	ShipMemory& s = save.ships[ship].memory;
	const uint32_t tick = g.u32(global::kWorldTick);
	const uint8_t town = s.u8(0x39);
	const uint16_t state = s.u16(0x134);
	if (town >= g.u32(global::kWorldTownCount)) {
		if (state != 0x11 && state != 0x12 && state < 0x17) {
			e.u32(0) += 0x20;
			return 1;
		}
		traveller_leaves(save, event);
		return 0;
	}
	Letter letter(0x18);
	letter.entry.u8(5) = town;
	letter.entry.u16(0xC) = e.u8(0xA);
	if (town != e.u8(0xB)) {
		if (e.u32(0x10) >= tick) {
			e.u32(0) += 0x100;
			return 1;
		}
		letter.entry.u16(8) = 2;
		letter.entry.u16(0xA) = 0;
		send_letter(save, s.u8(0), letter);
		traveller_leaves(save, event);
		return 0;
	}
	if (state == 3 || state == 2) {
		const int32_t wait = 0x40 - int32_t(s.u16(0x138));
		e.u32(0) += wait > 0 ? uint32_t(wait) : 1u;
		return 1;
	}
	if (e.u32(0x10) >= tick) {
		letter.entry.u16(8) = 0;
		letter.entry.u16(0xA) = e.u16(0xC);
	} else {
		const int32_t late = int32_t(((tick - e.u32(0x10)) >> 8) + 1);
		if (late > 4) {
			letter.entry.u16(8) = 2;
			letter.entry.u16(0xA) = 0;
		} else {
			letter.entry.u16(8) = 1;
			letter.entry.u16(0xA) = uint16_t(int32_t(e.u16(0xC)) * (5 - late) / 5);
		}
	}
	const uint8_t owner = s.u8(0);
	if (owner < g.u16(global::kWorldMerchantCount)) {
		MerchantMemory& m = save.merchants[owner].memory;
		const uint16_t paid = letter.entry.u16(0xA);
		m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) + paid);
		m.s32(0x4B8) = int32_t(uint32_t(m.s32(0x4B8)) + paid);
		send_letter(save, owner, letter);
	}
	traveller_leaves(save, event);
	return 0;
}

namespace ship_ai_detail {
void fn_535400(SaveGame& save, uint32_t town, uint32_t merchant);
}  // namespace ship_ai_detail

// The operation 0x4C (0x53B4D0): {0x4C, merchant, the informer's town, the price, the town told about}: the tavern's
// informer's news (his window 0x5D5970 queues what 0x526B40 prepared). The merchant (below the merchants) pays ([+0x0]
// down, [+0x4BC] up) and, with the town told about in the world, every town of its region (the byte [+0x30] of the
// StadtDaten entry 0x6DDB90 of each town's number [+0x2C1]) is known to him as if his ship were there (0x535400). The
// informer leaves: the informer's town's [+0x82C] = 0xFF (a town in the world).
void fn_53b4d0(SaveGame& save, const uint8_t* operation) {
	constexpr uint32_t kTownTable = 0x6DDB90;  // 0x34 bytes per town number (StadtDaten.ini)
	uint32_t f[5];
	std::memcpy(f, operation, sizeof(f));
	GlobalMemory& g = save.globals;
	const uint32_t towns = g.u32(global::kWorldTownCount);
	if (f[1] < g.u16(global::kWorldMerchantCount)) {
		MerchantMemory& m = save.merchants[f[1]].memory;
		m.s32(0) = int32_t(uint32_t(m.s32(0)) - f[3]);
		m.s32(0x4BC) = int32_t(uint32_t(m.s32(0x4BC)) + f[3]);
		if (f[4] < towns) {
			auto region = [&](uint32_t town) { return g.u8(kTownTable + 0x34 * save.towns[town].memory.u8(0x2C1) + 0x30); };
			const uint8_t wanted = region(f[4]);
			for (uint32_t t = 0; t < g.u32(global::kWorldTownCount); ++t) {  // (The game counts in a byte.)
				if (region(t) == wanted) {
					ship_ai_detail::fn_535400(save, t, f[1]);
				}
			}
		}
	}
	if (f[2] < g.u32(global::kWorldTownCount)) {
		save.towns[f[2]].memory.u8(0x82C) = 0xFF;
	}
}

}  // namespace p3::data::tick
