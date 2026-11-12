// The office's windows (p3data/office_window.hpp), without game data: the warehouse page's numbers (0x5DCA70), the
// administrator's operations (0x5DC2A0), the orders' and the own buildings' operations.

#include <cstdio>
#include <cstring>

#include "p3data/office_window.hpp"

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

// One town, the player 0 with the office 0 in it, 10 captains.
SaveGame world() {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 1;
	g.u16(global::kWorldMerchantCount) = 1;
	g.u16(global::kWorldOfficeCount) = 1;
	g.u16(global::kCaptainCount) = 10;
	save.towns.resize(1);
	save.merchants.resize(1);
	save.offices.resize(1);
	OfficeMemory& o = save.offices[0].memory;
	o.u16(0x2C4) = 0;
	o.u8(0x2C6) = 0;
	o.u16(0x2F2) = 0xFFFF;
	return save;
}

}  // namespace

int main() {
	{
		SaveGame save = world();
		OfficeMemory& o = save.offices[0].memory;
		o.s32(4 + 4 * 0) = 2000 * 60 + 1500;  // grain: 60 whole loads
		o.s32(4 + 4 * 3) = 200 * 30 + 150;  // beer: 30 whole barrels (6000)
		o.s32(0x2E8) = 450;  // 400 in whole 200s
		o.u16(0x2EC) = 450;
		o.u8(0x2C7) = 1;
		const OfficeStorage s = office_storage(save, 0);
		// (120000 + 6000 + 400 + 1800) / 2000 = 64
		check(s.used == 64 && s.capacity == 450 && s.over == 0, "the storage used, in loads");
		check(s.max_guards == 3, "(450 − 50) / 200 + 1 guards at most");
		// (3000 / 2 − 1000) · 14 / 200 = 35: 1
		check(s.risk == 1, "the thieves' chance");
		o.u16(0x2EC) = 40;
		const OfficeStorage t = office_storage(save, 0);
		check(t.over == 24 && t.max_guards == 1, "over its capacity");
	}
	{
		SaveGame save = world();
		std::vector<OperationBytes> ops = administrator_operations(save, 0, 0, 0);
		check(ops.size() == 1 && field(ops[0], 0) == 0x5E && field(ops[0], 3) == 0, "hire");
		save.offices[0].memory.u16(0x2F2) = 3;
		ops = administrator_operations(save, 0, 0, 0);
		check(ops.size() == 21 && field(ops[0], 0) == 0x5B && field(ops[19], 4) == 19 && field(ops[20], 0) == 0x5E &&
				field(ops[20], 3) == 1,
			"dismiss: the 20 orders cleared first");
		const OperationBytes order = administrator_order_operation(4000, -35, 0, 7);
		check(field(order, 1) == 4000 && int32_t(field(order, 2)) == -35 && field(order, 4) == 7, "0x5B");
		save.merchants[0].memory.u16(0x5A8 + 4 * 7) = 33;
		save.merchants[0].memory.u16(0x5AA + 4 * 7) = 44;
		check(last_order_price(save, 0, 7, true) == 33 && last_order_price(save, 0, 7, false) == 44, "the last prices");
		const OperationBytes keep = reserve_operation(7, 0, 0, true);
		check(field(keep, 0) == 0x66 && field(keep, 1) == 7 && field(keep, 4) == 1, "0x66");
	}
	{
		check(house_class(0x15) == 0 && house_class(0x19) == 1 && house_class(0x1D) == 2, "the house classes");
		SaveGame save = world();
		save.offices[0].memory.u8(0x2E4 + 2) = 3;
		check(house_rent(save, 0, 0, 0x1B) == 3, "a poor house's rent level");
		const OperationBytes rent = rent_operation(0, 0, 4, 2);
		check(field(rent, 0) == 0x62 && field(rent, 3) == 4 && field(rent, 4) == 2, "0x62");
		const OperationBytes share = business_share_operation(0, 0, 9, 75);
		check(field(share, 0) == 0x5A && field(share, 3) == 9 && field(share, 4) == 75, "0x5A");
		check(field(guards_operation(0, 0, 2), 0) == 0x63 && field(guards_operation(0, 0, 2), 3) == 2, "0x63");
	}
	if (failures == 0) {
		std::printf("office_window: ok\n");
	}
	return failures == 0 ? 0 : 1;
}
