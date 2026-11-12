// The town's weaponsmith (p3data/smithy_window.hpp), without game data: the prices (0x5285B0 and the hand weapons'),
// the counts (0x52E030), the two pages, why it stopped, and the bytes of its buttons (0x5ECFA0).

#include <cstdio>
#include <cstring>

#include "p3data/smithy_window.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

uint32_t u32_at(const OperationBytes& op, size_t at) {
	uint32_t v;
	std::memcpy(&v, op.data() + at, 4);
	return v;
}

// Two towns (the player's office in town 1, the merchant 0's), the player the mayor of town 1.
SaveGame world() {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 2;
	g.u16(global::kWorldMerchantCount) = 2;
	g.u16(global::kWorldOfficeCount) = 1;
	g.u32(global::kPlayer) = 0;
	g.u16(global::kWorldDayOfYear) = 7;
	g.u32(global::kWorldTick) = 0x1234 << 8;
	save.towns.resize(2);
	save.merchants.resize(2);
	save.offices.resize(1);
	for (uint8_t i = 0; i < 2; ++i) {
		save.towns[i].memory.u8(0x2C0) = i;
		save.towns[i].memory.u16(0x784) = 0xFFFF;
	}
	save.towns[1].memory.u16(0x784) = 0;
	OfficeMemory& o = save.offices[0].memory;
	o.u16(0x2C4) = 0;
	o.u16(0x2CA) = 0xFFFF;
	TownMemory& t = save.towns[1].memory;
	t.u8(0x6F1) = 0;
	t.u32(0x6D0) = 0x16;  // the crossbow first
	// The town: 3 small catapults (1000 each), 2 large ballistas (2000 each) and a part, 4 cutlasses; 95 bows' units.
	t.s32(0x124 + 4 * 0) = 3000;
	t.s32(0x124 + 4 * 3) = 5999;
	t.u32(0x2BC) = 4;
	t.s32(0x54 + 4 * 1) = 95;
	// The office: a cannon (1000), 25 swords' units.
	o.s32(0x124 + 4 * 5) = 1000;
	o.s32(0x54 + 4 * 0) = 25;
	return save;
}

}  // namespace

int main() {
	{
		const SaveGame save = world();
		// (1 + 7) % 5 = 3: 9 + 163 · {2, 3, 5, 8, 11, 14}; the cutlass 100.
		check(artillery_price(save, 1, 0) == 335 && artillery_price(save, 1, 1) == 498 &&
				  artillery_price(save, 1, 5) == 2291 && artillery_price(save, 1, 6) == 100,
			"0x5285B0: the pieces by the town and the day, the cutlass fixed");
		check(artillery_price(save, 0, 0) == 2 * 3 + 326, "town 0: (0 + 7) % 5 = 2");
		check(town_artillery(save, 1, 0) == 3 && town_artillery(save, 1, 3) == 2 && town_artillery(save, 1, 6) == 4,
			"0x52E030: the weight's whole pieces, the cutlasses one each");
		check(office_artillery(save, 0, 5) == 1, "0x52E030 on the office");
		// ((0x1234 + 1) · 3) % 5: 0x1235 = 4661, · 3 = 13983, % 5 = 3.
		check(hand_weapon_price(save, 1, 0) == 95 && hand_weapon_price(save, 1, 1) == 77 &&
				  hand_weapon_price(save, 1, 2) == 114 && hand_weapon_price(save, 1, 3) == 132,
			"the hand weapons: 92, 74, 111, 129 (−9.25 truncated) and the rest");
	}
	{
		const SaveGame save = world();
		const SmithyPage a = smithy_artillery(save, 0, 1);
		check(a.office && a.rows.size() == 7, "the artillery: an office, seven rows");
		check(a.rows.size() == 7 && a.rows[0].town == 3 && a.rows[5].office == 1 && a.rows[6].town == 4 &&
				  a.rows[0].price == 335 && a.rows[0].sell == 167,
			"the counts and the prices, selling at half");
		const SmithyPage h = smithy_hand_weapons(save, 0, 1);
		check(h.office && h.rows.size() == 4 && h.rows[1].town == 9 && h.rows[0].office == 2, "the hand weapons / 10");
		check(h.may_choose && h.priority == 2, "the mayor; the crossbow checked");
		const SmithyPage none = smithy_artillery(save, 0, 0);
		check(!none.office && none.rows.size() == 7, "no office in town 0: the buttons off");
		check(!smithy_hand_weapons(save, 1, 1).may_choose, "another merchant isn't the mayor");
	}
	{
		SaveGame save = world();
		save.towns[1].memory.u32(0x6D0) = 0;
		check(smithy_hand_weapons(save, 0, 1).priority == -1, "nothing first: no row checked");
		check(smithy_stopped(save, 1) == -1, "not stopped");
		save.towns[1].memory.u8(0x775) = 0x0C;
		check(smithy_stopped(save, 1) == 2, "the first reason: no hemp");
		save.towns[1].memory.u8(0x775) = 0x30;
		check(smithy_stopped(save, 1) == 4, "only the 5 low bits: the warehouse");
	}
	{
		const SaveGame save = world();
		const OperationBytes buy = smithy_artillery_operation(save, 0, 1, 2, false);
		check(u32_at(buy, 0) == 0x56 && u32_at(buy, 4) == 0 && u32_at(buy, 8) == 2 && u32_at(buy, 0xC) == 9 + 815 &&
				  u32_at(buy, 0x10) == 1,
			"0x56 buy: player, kind, price, town");
		const OperationBytes sell = smithy_artillery_operation(save, 0, 1, 2, true);
		check(u32_at(sell, 0xC) == (9 + 815) / 2 && u32_at(sell, 0x10) == 0x101, "0x56 sell: half, town | 0x100");
		const OperationBytes weapon = smithy_hand_weapon_operation(save, 0, 1, 3);
		check(u32_at(weapon, 0) == 0x50 && u32_at(weapon, 4) == 132 && u32_at(weapon, 8) == 1 && weapon[0xC] == 1 &&
				  weapon[0xD] == 0 && weapon[0xE] == 3 && weapon[0xF] == 1 && weapon[0x10] == 0xFF &&
				  weapon[0x11] == 0xFF && weapon[0x12] == 0,
			"0x50: one into the office");
		check(u32_at(smithy_priority_operation(1, 2), 0) == 0x57 && u32_at(smithy_priority_operation(1, 2), 4) == 1 &&
				  u32_at(smithy_priority_operation(1, 2), 8) == 0x16 && u32_at(smithy_priority_operation(1, -1), 8) == 0x113,
			"0x57: the row's ware, or 0x113 to put it off");
	}
	if (failures == 0) {
		std::printf("smithy_window: OK\n");
	}
	return failures == 0 ? 0 : 1;
}
