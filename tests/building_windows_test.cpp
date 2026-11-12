// The church's and the bath house's windows (p3data/building_windows.hpp), without game data: the operations' bytes
// (the feeding rows in the operation's order, the donations, the bath, the bribe by its price with the game's rand()),
// the bath's state and the church's (the scenario building_windows compares 0x4FE360, 0x4FE390 and 0x529E20 with the
// game's own).

#include <cstdio>
#include <cstring>

#include "p3data/building_windows.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

uint32_t field(const OperationBytes& op, int k) {
	uint32_t v;
	std::memcpy(&v, op.data() + 4 * k, 4);
	return v;
}

// Two towns, two merchants (the player 1, at rank 3 in his home town 0).
SaveGame world() {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 2;
	g.u16(global::kWorldMerchantCount) = 2;
	g.u32(global::kPlayer) = 1;
	g.u32(global::kWorldTick) = 1000 << 8;
	save.towns.resize(2);
	save.merchants.resize(2);
	save.merchants[1].memory.u8(0x19) = 0;
	save.merchants[1].memory.u8(0x39C) = 3;
	for (auto& t : save.towns) {
		for (int k = 0; k < 4; ++k) {
			t.memory.u8(0x6DC + k) = 0xFF;
		}
	}
	return save;
}

}  // namespace

int main() {
	{
		SaveGame save = world();
		const OperationBytes op = feeding_operation(save, 1, 1, {10, 11, 12, 13, 14});
		uint16_t rows[5];
		std::memcpy(rows, op.data() + 8, sizeof(rows));
		check(op[0] == 0x30 && op[4] == 1 && op[5] == 1 && op[6] == 0, "0x30: player, town, no worth");
		check(rows[0] == 10 && rows[1] == 13 && rows[2] == 12 && rows[3] == 11 && rows[4] == 14,
			"0x30: the rows as grain, beer, fish, meat, wine");
		const OperationBytes gift = church_donation_operation(0x32, 1, 0, 1500);
		check(field(gift, 0) == 0x32 && field(gift, 1) == 1 && field(gift, 2) == 0 && field(gift, 3) == 1500,
			"0x32: player, town, amount");
	}
	{
		SaveGame save = world();
		save.globals.u8(0x6DE52C) = 2;
		TownMemory& c = save.towns[0].memory;
		c.s32(0x794) = 3000 * 5;  // the unit is 6000: 2.5 steps
		check(church_feeding_level(save, 0) == 3, "fed: half a step rounds up");
		c.s32(0x794) = 6000 * 9;
		check(church_feeding_level(save, 0) == 5, "fed: at most 5");
		c.u8(0x794 + 0xF) = 1;
		check(church_extension_cost(save, 0) == 2 * 15000 + 40000, "the extension's cost at level 1");
		c.u32(0x794 + 8) = 101;
		check(church_extension_state(save, 0) == -1, "the extension open");
		c.u32(0x794 + 8) = 100;
		check(church_extension_state(save, 0) == 1, "the extension too soon");
		c.s32(0x794 + 4) = 70000;
		check(church_extension_state(save, 0) == 0, "the extension's money gathered");
		c.u8(0x794 + 0xF) = 3;
		check(church_extension_state(save, 0) == 2, "the last level");
	}
	{
		SaveGame save = world();
		MerchantMemory& m = save.merchants[1].memory;
		m.u32(0x4) = (1000 << 8) - 0x5FF;
		check(bath_state(save, 1) == 0, "bathed within 6 days");
		m.u32(0x4) = (1000 << 8) - 0x600;
		check(bath_state(save, 1) == 1, "6 days ago");
		m.u32(0x4) = (1000 << 8) - 0x15FF;
		check(bath_state(save, 1) == 1, "21 days and a part ago");
		m.u32(0x4) = (1000 << 8) - 0x1600;
		check(bath_state(save, 1) == 2, "22 days ago");
		const OperationBytes bath = bath_operation(1, 1000 << 8);
		check(field(bath, 0) == 0x39 && field(bath, 1) == 1 && field(bath, 2) == (1000u << 8), "0x39: player, tick");
	}
	{
		SaveGame save = world();
		// Rank 3: f 3; rand() from 1: 41 % 11 = 8 → (8 + 12 + 16) · 500.
		save.rand_state = 1;
		check(councillor_price(save, 0, 1, 0) == 36 * 500, "the price at rank 3, a free slot");
		save.towns[0].memory.u8(0x6DD) = 0;
		save.rand_state = 1;
		check(councillor_price(save, 0, 1, 1) == 2 * 36 * 500, "twice for a merchant's slot");
		check(councillor_price(save, 0, 2, 0) == 0x80000000u && councillor_price(save, 0, 1, 4) == 0x80000000u,
			"a merchant past, a slot past");
		save.rand_state = 1;
		const OperationBytes yes = bribe_operation(save, 0, 1, 0, 18000);
		check(field(yes, 0) == 0x42 && field(yes, 1) == 0 && field(yes, 2) == 1 && field(yes, 3) == 18000 &&
				  field(yes, 4) == 0,
			"0x42: the offer reaches the price");
		save.rand_state = 1;
		const OperationBytes no = bribe_operation(save, 0, 1, 0, 17999);
		check(field(no, 0) == 0x43 && field(no, 3) == 0 && field(no, 4) == 0, "0x43: short of the price");
		check(save.rand_state != 1, "the price rolls rand()");
	}
	if (failures == 0) {
		std::printf("building_windows: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
