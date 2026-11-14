// The market hall's window (p3data/market_hall_window.hpp), without game data: its three pages' figures as the updates
// 0x5DE810, 0x5DE960 and 0x5DEB70 make them (truncated or rounded, in barrels or loads, the town's production not
// multiplied by 7 as the offices' is, a business's use at least 1) and which the drawing shows.

#include <cstdio>

#include "p3data/game_fixes.hpp"
#include "p3data/market_hall_window.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

constexpr int kGrain = 0;  // loads (2000), the business 0x09
constexpr int kBeer = 3;  // barrels (200), the business 0x06
constexpr int kSpices = 6;  // barrels, no business of the town's
constexpr int kWhaleOil = 10;  // barrels, the town's whalers

// One town with two offices (the merchants 0's and 1's) in its list.
SaveGame world() {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 1;
	g.u16(global::kWorldMerchantCount) = 2;
	g.u16(global::kWorldOfficeCount) = 2;
	save.towns.resize(1);
	save.merchants.resize(2);
	save.offices.resize(2);
	save.towns[0].memory.u16(0x784) = 0;
	save.offices[0].memory.u16(0x2CA) = 1;
	save.offices[1].memory.u16(0x2CA) = 0xFFFF;
	return save;
}

}  // namespace

int main() {
	{
		SaveGame save = world();
		TownMemory& t = save.towns[0].memory;
		t.s32(0x4 + 4 * kBeer) = 399;
		t.s32(0x4 + 4 * kGrain) = 3999;
		const auto rows = market_hall_rows(save, 0, 0);
		check(rows[kBeer].values[0] == 1 && rows[kBeer].shown[0], "stock: 399 / 200 truncated");
		check(rows[kGrain].values[0] == 1, "stock: 3999 / 2000 truncated");
		check(rows[kSpices].values[0] == 0 && !rows[kSpices].shown[0], "stock: none not shown");
		check(!rows[kBeer].shown[3], "stock: no fourth column");
	}
	{
		SaveGame save = world();
		TownMemory& t = save.towns[0].memory;
		t.s32(0x4 + 4 * kBeer) = 300;
		t.s32(0x4 + 4 * kGrain) = 2999;
		t.s32(0xC4 + 4 * kBeer) = 100;
		save.offices[0].memory.u32(0xC4 + 4 * kBeer) = 30;
		save.offices[1].memory.u32(0xC4 + 4 * kBeer) = 30;
		const auto rows = market_hall_rows(save, 0, 1);
		check(rows[kBeer].values[0] == 2, "production: the stock 1.5 rounded up");
		check(rows[kGrain].values[0] == 1, "production: the stock 1.4995 rounded down");
		check(rows[kBeer].values[1] == 1, "production: the town's day not multiplied by 7 (the game's slip)");
		set_fix_game_bugs(true);
		check(market_hall_rows(save, 0, 1)[kBeer].values[1] == 4, "production: behind fix_game_bugs, the town's week (3.5)");
		set_fix_game_bugs(false);
		check(rows[kBeer].values[2] == 2 && rows[kBeer].shown[2], "production: the offices' days · 7 summed");
		check(!rows[kGrain].shown[1], "production: the town's 0 not shown without its business");
		t.u16(0x844 + 16 * 0x09) = 3;  // workers in the grain's business
		t.u32(0x2CC) = 5;  // whalers
		const auto busy = market_hall_rows(save, 0, 1);
		check(busy[kGrain].shown[1] && busy[kGrain].values[1] == 0, "production: the town's 0 shown with its business");
		check(busy[kWhaleOil].shown[1], "production: the whale oil's 0 shown with whalers");
		check(!busy[kSpices].shown[1], "production: the spices never by business");
		check(!busy[kGrain].shown[2], "production: the offices' 0 not shown");
	}
	{
		SaveGame save = world();
		TownMemory& t = save.towns[0].memory;
		t.u32(0x310 + 4 * kBeer) = 15;  // 105 a week: 0.525
		t.u32(0x64 + 4 * kBeer) = 10;  // 70 a week: 0.35, shown as 1
		t.s32(0x4 + 4 * kBeer) = 100;  // 0.5 rounded up
		const auto rows = market_hall_rows(save, 0, 2);
		check(rows[kBeer].values[0] == 1, "consumption: the stock rounded");
		check(rows[kBeer].values[1] == 1 && rows[kBeer].shown[1], "consumption: the citizens' week rounded");
		check(rows[kBeer].values[2] == 1 && rows[kBeer].shown[2], "consumption: a business's use at least 1");
		check(rows[kBeer].values[3] == 2 && rows[kBeer].shown[3], "consumption: the total");
		check(!rows[kGrain].shown[0] && !rows[kGrain].shown[1] && !rows[kGrain].shown[2] && !rows[kGrain].shown[3],
			"consumption: nothing not shown");
	}
	if (failures == 0) {
		std::printf("market_hall_window: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
