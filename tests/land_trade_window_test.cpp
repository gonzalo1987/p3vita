// The land trade (p3data/land_trade_window.hpp), without game data: the page 0's buttons and update as the game's
// 0x5EAAE0 runs them (the step, the office's stock, the carts' room: a barrel 1, a load 10 of 100), the page 1's danger,
// cost and days (0x5EB010), the send's operations without an ambush, and the side menu's check with its fix. The
// road's ends and days, the check and the send with its ambushes are checked with the oracle: the scenario
// land_trade_window.

#include <cstdio>
#include <cstring>

#include "p3data/game_fixes.hpp"
#include "p3data/land_trade_window.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

uint16_t u16_at(const OperationBytes& op, size_t at) {
	uint16_t v;
	std::memcpy(&v, op.data() + at, 2);
	return v;
}

constexpr uint32_t kTownIds = 0x6DE4B8;

// Two towns (the numbers 3 and 4: the road {3, 4, 80, 32}), town 0 on it ([+0x76C] & 0x8000); the player (merchant 0)
// with an office in town 0 and, if `both`, in town 1.
SaveGame world(bool both) {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 2;
	g.u16(global::kWorldMerchantCount) = 1;
	g.u16(global::kWorldOfficeCount) = 2;
	g.u32(global::kPlayer) = 0;
	g.u8(kTownIds) = 3;
	g.u8(kTownIds + 1) = 4;
	save.towns.resize(2);
	save.merchants.resize(1);
	save.offices.resize(2);
	for (uint8_t i = 0; i < 2; ++i) {
		save.towns[i].memory.u8(0x2C0) = i;
		save.towns[i].memory.u16(0x784) = 0xFFFF;
		save.offices[i].memory.u16(0x2C4) = 0;
		save.offices[i].memory.u16(0x2CA) = 0xFFFF;
	}
	save.towns[0].memory.u32(0x76C) = 0x8000;
	save.towns[0].memory.u16(0x784) = 0;
	if (both) {
		save.towns[1].memory.u16(0x784) = 1;
	}
	return save;
}

constexpr uint32_t kGrain = 0;  // in loads (2000 units)
constexpr uint32_t kBeer = 3;   // in barrels (200 units)

}  // namespace

int main() {
	{
		// The road: town 0's other end is town 1; without the flag none; its days (16 · 32 + 0x287) >> 8; without a
		// road (16 + 0x287) >> 8.
		SaveGame save = world(true);
		check(fn_525630(save, 0) == 1, "town 0's road leads to town 1");
		check(fn_525630(save, 1) == 0xFF, "town 1 without [+0x76C] & 0x8000 has none");
		save.towns[1].memory.u32(0x76C) = 0x8000;
		check(fn_525630(save, 1) == 0, "town 1 with it leads back to town 0");
		check(fn_533490(save, 0, 1) == 4 && fn_533490(save, 1, 0) == 4, "the road's days: 4 both ways");
		save.globals.u8(kTownIds + 1) = 5;
		check(fn_533490(save, 0, 1) == 2, "without a road: 2 days");
		check(fn_525630(save, 0) == 0xFF, "the other end (number 4) no town's: 0xFF");
	}
	{
		// The side menu's check (0x5336B0): town 0's barn built (a record 0x27 in its list [+0x764]), town 1's not: the
		// game says yes (it looks at town 0's list twice); fixed, 2 (the partner's barn). Without town 0's, 1; town 0
		// besieged, 3; town 1 besieged, 4.
		SaveGame save = world(true);
		for (SavedTown& t : save.towns) {
			t.records.resize(2);
			t.memory.u16(0x776) = 2;
			t.memory.u16(0x764) = 0xFFFF;
		}
		save.towns[0].records[1] = {1, 1, 0xFF, 0x27, 0xFFFF, 0, 0};
		save.towns[0].memory.u16(0x764) = 1;
		uint8_t partner = 0xFF;
		check(fn_5336b0(save, 0, partner) == 0 && partner == 1, "the game's: no look at the partner's barn");
		set_fix_game_bugs(true);
		check(fn_5336b0(save, 0, partner) == 2, "fixed: the partner's barn not built");
		save.towns[1].records[0] = {2, 2, 0xFF, 0x27, 0xFFFF, 0, 0};
		save.towns[1].memory.u16(0x764) = 0;
		check(fn_5336b0(save, 0, partner) == 0, "fixed: both barns built");
		set_fix_game_bugs(false);
		save.towns[0].memory.u32(0x2C8) = 0x10;
		check(fn_5336b0(save, 0, partner) == 3, "town 0 besieged");
		save.towns[0].memory.u32(0x2C8) = 0;
		save.towns[1].memory.u32(0x2C8) = 0x10;
		check(fn_5336b0(save, 0, partner) == 4, "its partner besieged");
		save.towns[0].memory.u16(0x764) = 0xFFFF;
		check(fn_5336b0(save, 0, partner) == 1, "town 0 without its barn");
		partner = 0xFF;
		save.globals.u8(kTownIds) = 7;
		check(fn_5336b0(save, 0, partner) == 5 && partner == 0xFF, "no road from number 7");
	}
	{
		// Opened: the offices in both, or not.
		SaveGame save = world(false);
		LandTradeWindow w;
		land_trade_open(save, 0, w);
		check(w.to == 1 && !w.offices && w.page == 0 && w.escorts == 0, "opened without an office in town 1");
		save.towns[1].memory.u16(0x784) = 1;
		land_trade_update(save, w);
		check(w.offices, "the update sees the office built");
		LandTradeWindow w2;
		land_trade_open(save, 0, w2);
		check(w2.offices, "opened with both");
	}
	{
		// The office's stock in units; the step's loads cut by it and by the room.
		SaveGame save = world(true);
		save.offices[0].memory.s32(4 + 4 * kGrain) = 5000;   // 2 loads
		save.offices[0].memory.s32(4 + 4 * kBeer) = 2000;    // 10 barrels
		save.offices[0].memory.s32(4 + 4 * 5) = 30000;       // 150 barrels of ware 5
		LandTradeWindow w;
		land_trade_open(save, 0, w);
		land_trade_update(save, w);
		check(w.left[kGrain] == 2 && w.left[kBeer] == 10 && w.left[5] == 150, "what the office has");
		w.step = 2;  // 50
		land_trade_load(w, kGrain);
		check(w.loaded[kGrain] == 2 && w.left[kGrain] == 0 && w.used == 20, "two loads: 20 of the room");
		w.step = 3;  // all
		land_trade_load(w, kBeer);
		check(w.loaded[kBeer] == 10 && w.left[kBeer] == 0 && w.used == 30, "ten barrels: 10 more");
		land_trade_load(w, 5);
		check(w.loaded[5] == 70 && w.left[5] == 80 && w.used == 100, "the barrels cut by the room");
		save.offices[0].memory.s32(4 + 4 * kGrain) = 9000;
		land_trade_update(save, w);
		check(w.left[kGrain] == 2, "the update reads the stock again");
		land_trade_load(w, kGrain);
		check(w.loaded[kGrain] == 2 && w.used == 100, "a load without room: none");
		w.step = 0;
		land_trade_unload(w, 5);
		check(w.loaded[5] == 69 && w.left[5] == 81 && w.used == 99, "a barrel back");
		land_trade_load(w, kGrain);
		check(w.loaded[kGrain] == 2 && w.used == 99, "a load needs 10");
		w.step = 3;
		land_trade_unload(w, kGrain);
		check(w.loaded[kGrain] == 0 && w.left[kGrain] == 4 && w.used == 79, "all the loads back: 20 of the room");
		land_trade_load(w, kGrain);
		check(w.loaded[kGrain] == 2 && w.left[kGrain] == 2 && w.used == 99, "two of the four: (100 - 79) / 10");
		// The stock fallen below the loaded: the loaded cut to it.
		save.offices[0].memory.s32(4 + 4 * kBeer) = 1000;
		land_trade_update(save, w);
		check(w.loaded[kBeer] == 5 && w.left[kBeer] == 0, "the beer cut to the 5 barrels left");
		check(w.used == 99, "the room isn't updated by it (the game's)");
		// The slots: 5 + 69 barrels, 2 loads (20); the value Σ ftol(units · value + 0.5).
		check(w.slots == 94, "the slots");
		const uint32_t value = uint32_t(4000 * double(0.055000003427267075f) + 0.5) +
			uint32_t(1000 * double(0.17399999499320984f) + 0.5) + uint32_t(13800 * double(0.550000011920929f) + 0.5);
		check(w.value == value, "the value");
	}
	{
		// The danger (the value against 100 and 400 · slots), the cost ((slots + 9) / 10 · 50, 200, 800), the days.
		SaveGame save = world(true);
		LandTradeWindow w;
		land_trade_open(save, 0, w);
		w.slots = 25;
		w.value = 2500;
		land_trade_costs(save, w);
		check(w.danger == 0 && w.cost == 150 && w.days == 4, "value 2500: no danger; 3 · 50");
		w.value = 2501;
		w.escorts = 1;
		land_trade_costs(save, w);
		check(w.danger == 1 && w.cost == 600, "value 2501: danger 1; 3 · 200");
		w.value = 10001;
		w.escorts = 2;
		land_trade_costs(save, w);
		check(w.danger == 2 && w.cost == 2400, "value 10001: danger 2; 3 · 800");
	}
	{
		// Despatched with the escort at the danger: a 0x3A for each ware loaded, the first told and with the cost.
		SaveGame save = world(true);
		LandTradeWindow w;
		land_trade_open(save, 0, w);
		w.loaded[kGrain] = 2;
		w.loaded[kBeer] = 7;
		w.used = 27;
		w.slots = 27;
		w.escorts = 1;
		w.danger = 1;
		w.cost = 600;
		const std::vector<OperationBytes> ops = land_trade_send(save, w);
		check(ops.size() == 2, "two operations");
		if (ops.size() == 2) {
			check(ops[0][0] == 0x3A && ops[0][4] == 0 && ops[0][5] == 1 && ops[0][6] == 0 && ops[0][7] == 1,
				"the first: from, to, the player, told");
			check(u16_at(ops[0], 8) == 2 && u16_at(ops[0], 0xA) == 600 && u16_at(ops[0], 0xC) == 0 &&
					ops[0][0xE] == kGrain && ops[0][0xF] == 1,
				"the first: 2 loads of grain, the cost, no loss, the escort");
			check(ops[1][7] == 0 && u16_at(ops[1], 8) == 7 && u16_at(ops[1], 0xA) == 0 && ops[1][0xE] == kBeer,
				"the second: not told, 7 barrels of beer, no cost");
		}
		check(w.loaded[kGrain] == 0 && w.loaded[kBeer] == 0 && w.used == 0 && w.slots == 27,
			"emptied (the slots kept: the page 2's carts)");
	}
	if (failures) {
		std::fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	std::printf("land_trade_window: ok\n");
	return 0;
}
