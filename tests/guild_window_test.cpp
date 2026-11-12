// The guild's window (p3data/guild_window.hpp), without game data: the auctions to come (0x4DA1A0), whether the player
// is told of one (0x4DA480), the town's auction and its lots, the bid's rules and the operations' bytes (the scenario
// auction_ops compares 0x4DA1A0, 0x4DA480 and 0x504AC0's mode 6 with the game's own).

#include <cstdio>
#include <cstring>

#include "p3data/guild_window.hpp"

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

constexpr uint32_t kEventHead = 0x6DD744;
constexpr uint32_t kEventCount = 0x6DD748;

// Two towns (the first with a guild), three merchants (0 an AI, 1 and 2 humans; the player 2), four ships, and the
// events linked in their order: auctions of ships (0, phase 2: ships 1 and 2), of buildings (1, phase 3), of the
// town 1 without a guild (2), in phase 1 (3) and bidding (4, phase 5).
SaveGame world() {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 2;
	g.u16(global::kWorldMerchantCount) = 3;
	g.u16(global::kShipCount) = 4;
	g.u32(global::kPlayer) = 2;
	g.u32(global::kWorldTick) = 1000 << 8;
	save.settings.u8(0xD) = 3;
	save.towns.resize(2);
	save.towns[0].memory.u8(0x2C8) = 0x40;
	save.towns[0].memory.u16(0x776) = 5;
	save.towns[0].memory.u16(0x9A2) = 0xFFFF;
	save.towns[1].memory.u16(0x9A2) = 0xFFFF;
	save.merchants.resize(3);
	save.merchants[0].memory.u16(0x8) = 1;
	save.ships.resize(4);
	save.ships[1].memory.u8(0xE) = 2;
	save.ships[2].memory.u8(0xE) = 2;
	save.events.resize(6);
	g.u16(kEventHead) = 0;
	g.u16(kEventCount) = 6;
	auto auction = [&](uint16_t i, uint16_t type, uint8_t phase, uint8_t town, uint16_t a, uint16_t b, uint16_t c) {
		GameMemory<0x18>& e = save.events[i];
		e.u32(0) = (1000 << 8) + i;
		e.u16(4) = uint16_t(i + 1);
		e.u16(6) = type;
		e.u16(8) = a;
		e.u16(0xA) = b;
		e.u16(0xC) = c;
		e.u16(0xE) = 0x200;
		e.u16(0x10) = 100;
		e.u8(0x14) = phase;
		e.u8(0x15) = town;
		e.u8(0x17) = 0xFF;
	};
	auction(0, 0xA, 2, 0, 1, 0xFFFF, 2);
	auction(1, 0x36, 3, 0, 0x8001, 0xFFFF, 0xFFFF);
	auction(2, 0xA, 2, 1, 1, 0xFFFF, 0xFFFF);
	auction(3, 0xA, 1, 0, 3, 0xFFFF, 0xFFFF);
	auction(4, 0xA, 5, 0, 0, 3, 0xFFFF);
	save.events[5].u16(4) = 6;
	save.events[5].u16(6) = 0xFFFF;
	return save;
}

}  // namespace

int main() {
	{
		SaveGame save = world();
		std::vector<AuctionEntry> list = upcoming_auctions(save);
		check(list.size() == 2, "the auctions to come: the two of the town with a guild in the phases 2 and 3");
		check(list[0].event == 0 && list[0].tick == (1000u << 8) + 0x100 && list[0].town == 0,
			"the phase 2's: a day later");
		check(list[0].lots[0] == 1 && list[0].lots[1] == 0xFFFF && list[0].lots[2] == 2 && list[0].types[2] == 2,
			"the ships and their types");
		check(list[1].event == 1 && list[1].tick == (1000u << 8) + 1 && list[1].lots[0] == 0x8001 &&
				list[1].types[0] == 0xFF,
			"a buildings' auction");
		save.settings.u8(0xD) = 0;
		list = upcoming_auctions(save);
		check(list.size() == 3 && list[2].event == 3 && list[2].tick == (1000u << 8) + 3 + 0x200 + 0x100,
			"with the settings' byte at 0, the phase 1's too, with its wait");
	}
	{
		SaveGame save = world();
		check(!told_of_auction(save, 0), "not told");
		save.events[0].u8(0x16) = 2;
		check(told_of_auction(save, 0), "told: the second human's bit");
		const OperationBytes off = tell_operation(save, 2, 0);
		check(field(off, 0) == 0x34 && field(off, 1) == 2 && field(off, 2) == 0 && field(off, 3) == 0, "0x34 off");
		save.events[0].u8(0x16) = 1;
		check(field(tell_operation(save, 2, 0), 3) == 1, "0x34 on");
		check(!told_of_auction(save, 5), "not an auction");
	}
	{
		SaveGame save = world();
		check(town_auction(save, 0) == -1, "no auction in the town");
		save.towns[0].memory.u16(0x9A2) = 4;
		check(town_auction(save, 0) == 4, "the town's auction");
		const std::vector<uint16_t> lots = auction_lots(save, 4);
		check(lots.size() == 2 && lots[0] == 0 && lots[1] == 3, "its lots");
		save.merchants[2].memory.s32(0) = 0x2FF;
		check(bid_shown(save, 4, 2) && can_bid(save, 4, 2), "the bid: money enough");
		const OperationBytes bid = bid_operation(save, 4, 2);
		check(field(bid, 0) == 0x33 && field(bid, 1) == 2 && field(bid, 2) == 4 && field(bid, 3) == 0x264,
			"0x33: price plus step");
		save.merchants[2].memory.s32(0) = 0x263;
		check(!can_bid(save, 4, 2), "the bid: short of money");
		save.merchants[2].memory.s32(0) = 0x2FF;
		save.events[4].u8(0x17) = 2;
		check(!can_bid(save, 4, 2), "the bid: the highest bidder already");
		save.events[4].u8(0x14) = 8;
		check(!bid_shown(save, 4, 2) && town_auction(save, 0) == -1, "the phase 8");
		save.events[4].u8(0x14) = 3;
		check(!bid_shown(save, 4, 2), "before the bidding");
	}
	{
		SaveGame save = world();
		check(guild_join_price(save, 0, 3) == 10000, "a merchant past the count: 10000");
		check(!guild_member(save, 1, 2), "not a member");
		save.merchants[2].memory.u32(0x468) = 2;
		check(guild_member(save, 1, 2), "a member");
		const OperationBytes join = join_guild_operation(2, 1, 25000);
		check(field(join, 0) == 0x37 && field(join, 1) == 2 && field(join, 2) == 1 && field(join, 3) == 25000, "0x37");
		const OperationBytes ship = auction_ship_operation(3);
		check(field(ship, 0) == 0x36 && field(ship, 1) == 3 && field(ship, 2) == 0, "0x36");
		const OperationBytes top = automatic_bid_operation(5000, 4, 3, 2);
		check(field(top, 0) == 0x35 && field(top, 1) == 5000 && field(top, 2) == 4 && field(top, 3) == 3 &&
				field(top, 4) == 2,
			"0x35");
	}
	{
		// 0x504AC0's mode 6: nothing without the guild's bit of the town; the docked ships out of a convoy.
		SaveGame save = world();
		save.merchants[2].memory.u16(0xE) = 1;
		for (uint16_t s = 1; s < 4; ++s) {
			ShipMemory& m = save.ships[s].memory;
			m.u16(0x4) = uint16_t(s + 1);
			m.u8(0x39) = 0;
			m.u16(0x8) = 0xFFFF;
			m.u16(0x134) = 0;  // docked (a new SavedShip is a free slot, 0x17)
		}
		save.ships[2].memory.u16(0x134) = 2;
		check(window_ships(save, 2, 0, ShipListMode::Auction).empty(), "mode 6 without the guild's bit");
		save.merchants[2].memory.u32(0x468) = 1;
		const std::vector<uint16_t> ships = window_ships(save, 2, 0, ShipListMode::Auction);
		check(ships.size() == 2 && ships[0] == 1 && ships[1] == 3, "mode 6: the docked ships");
	}
	if (failures == 0) {
		std::printf("guild_window: ok\n");
	}
	return failures == 0 ? 0 : 1;
}
