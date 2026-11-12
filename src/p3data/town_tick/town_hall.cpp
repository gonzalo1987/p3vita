// The town hall's orders (the world's seven, 0x6DE530, which the event 0x22 makes again; taken with the operation 0x3D,
// the event 0x11 counts its time): the operation 0x51, the wares delivered for an order taken; and the candidacy for
// mayor, 0x45.

#include <algorithm>

#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {

namespace ship_ai_detail {
void fn_517770(SaveGame& save, int ship, uint32_t flags);
}  // namespace ship_ai_detail

namespace {

constexpr uint32_t kEventCount = 0x6DD73C + 0xC;  // u16
constexpr uint8_t kInBarrels[20] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0};  // 0x672C14

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

}  // namespace p3::data::tick
