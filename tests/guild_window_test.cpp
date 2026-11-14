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
	{
		// The members' page (0x5C67A0, 0x5C0E2D..): the town's guild's members who aren't a town's own merchant; their
		// offices (the town's list [0x784], the office's next [+0x2CA]), the oldest one, the best respect.
		SaveGame save = world();
		save.merchants.resize(4);
		save.globals.u16(global::kWorldMerchantCount) = 4;
		save.globals.u16(global::kWorldOfficeCount) = 3;
		save.merchants[0].memory.u32(0x468) = 1;  // an AI's trader: a member
		save.merchants[1].memory.u32(0x468) = 2;  // the other town's only
		save.merchants[2].memory.u32(0x468) = 3;  // the player: a member
		save.merchants[3].memory.u8(0x8) = 5;  // a town's own merchant: never listed
		save.merchants[3].memory.u32(0x468) = 1;
		save.offices.resize(3);
		save.towns[0].memory.u16(0x784) = 0;
		save.towns[1].memory.u16(0x784) = 2;
		save.offices[0].memory.u16(0x2C4) = 2;
		save.offices[0].memory.u32(0x2C0) = 900 << 8;
		save.offices[0].memory.u16(0x2CA) = 1;
		save.offices[1].memory.u16(0x2C4) = 0;
		save.offices[1].memory.u32(0x2C0) = 500 << 8;
		save.offices[1].memory.u16(0x2CA) = 0xFFFF;
		save.offices[2].memory.u16(0x2C4) = 2;
		save.offices[2].memory.u32(0x2C0) = 400 << 8;
		save.offices[2].memory.u16(0x2CA) = 0xFFFF;
		save.merchants[2].memory.f32(0x2FC) = 3.5f;
		save.merchants[2].memory.f32(0x2FC + 4) = 3.9f;  // 3 too: not above the best
		save.merchants[0].memory.f32(0x2FC) = -2.0f;
		const std::vector<GuildMember> members = guild_members(save, 0);
		check(members.size() == 2 && members[0].merchant == 0 && members[1].merchant == 2, "the members");
		check(members[0].offices == 1 && members[0].first_tick == (500u << 8) && members[0].first_town == 0 &&
				members[0].respected_town == 0,
			"an AI's trader: one office, no respect above 0");
		check(members[1].offices == 2 && members[1].first_tick == (400u << 8) && members[1].first_town == 1 &&
				members[1].respected_town == 0,
			"the player: two offices, the oldest in the town 1, the best respect the first town's");
		save.merchants[2].memory.f32(0x2FC + 4) = 4.0f;
		check(guild_members(save, 0)[1].respected_town == 1, "a better respect in the town 1");
		MerchantMemory& m = save.merchants[2].memory;
		m.u8(0x1A) = 1;
		m.u8(0x19) = 1;
		m.u8(0x39C + 1) = 3;
		m.u32(0x10) = (1000u << 8) - 93440u * 39 - 5;
		m.u8(0x31) = 2;
		m.u8(0x36) = 4;
		m.u16(0x478) = 7;
		GuildMemberPage page = guild_member_page(save, 2);
		check(page.sex == 1 && page.home == 1 && page.rank == 3 && page.age == 39 && !page.married && page.children == 4 &&
				page.ships == 7,
			"the member's page");
		m.u8(0x31) = 1;
		m.u8(0x30) = 12;
		page = guild_member_page(save, 2);
		check(page.married && page.spouse == 12, "married");
	}
	{
		// The convoys' page (0x50A340, 0x5C6A30, 0x5C5370): five convoys linked 0 → 1 → 2 → 3 → 4; 1 private (flag 2),
		// 3 in the state 5; 0 sailing (2) leaving at 500, 2 and 4 waiting (0, 1) at 300 and 100: before 0, the last
		// first: 4, 2, 0.
		SaveGame save = world();
		GlobalMemory& g = save.globals;
		g.u16(global::kShips + 0xF6) = 5;
		g.u16(global::kShips + 0xEC) = 0;
		save.convoys.resize(5);
		const uint16_t states[5] = {2, 0, 0, 5, 1};
		const uint16_t leave[5] = {500, 0, 300, 0, 100};
		for (uint16_t c = 0; c < 5; ++c) {
			GameMemory<0x3C>& m = save.convoys[c];
			m.u16(0x8) = uint16_t(c + 1);
			m.u8(0x0) = 1;
			m.u16(0x12) = states[c];
			m.u16(0x16) = leave[c];
		}
		save.convoys[1].u8(0x14) = 2;
		std::vector<uint16_t> list = guild_convoys(save);
		check(list.size() == 3 && list[0] == 4 && list[1] == 2 && list[2] == 0, "the convoys: the waiting ones first, the last first");
		save.convoys[2].u16(0x12) = 0xF;
		list = guild_convoys(save);
		check(list.size() == 3 && list[0] == 4 && list[1] == 0 && list[2] == 2, "the state 0xF: sailing");
		// A convoy's line: the ships from [+0xA] (next [+0x6]) of its guide's owner.
		GameMemory<0x3C>& c = save.convoys[4];
		c.u16(0xA) = 1;
		c.u16(0x10) = 1;
		c.u8(0x1E) = 0;
		c.u8(0x1F) = 1;
		const uint32_t health[4] = {0, 100, 50, 80};
		for (uint16_t s = 1; s < 4; ++s) {
			ShipMemory& m = save.ships[s].memory;
			m.u8(0x0) = s == 2 ? 0 : 2;
			m.u16(0x6) = uint16_t(s + 1);
			m.s32(0x14) = 100;
			m.s32(0x18) = int32_t(health[s]);
			m.u16(0x40) = uint16_t(10 * s);
			m.s32(0x120) = int32_t(20 * s);
		}
		const GuildConvoyLine l = guild_convoy_line(save, 4);
		check(l.guide == 1 && l.from == 0 && l.to == 1 && l.ships == 2 && l.hull == 90 && l.crew == 40 && l.firepower == 80 &&
				l.waiting && l.departure == (1000u << 8) + 100,
			"a convoy's line: its guide's owner's ships");
	}
	{
		// The trading centres' map (0x534A40 through 0x4F7490): two states made the same way in the emulator by
		// re/emu/sesion34/guild/fogtrace.py (its cases `a` and `b`, the player not a member), the game's fog's FNV-1a
		// and counts: the Mediterranean (points off its left, top, right and bottom once scaled, overlapping ones, wide
		// ones), and the whole map (a point among the first 100; points at its edges and corners).
		struct FogCase {
			std::vector<std::pair<uint32_t, std::array<uint16_t, 2>>> points;
			std::vector<uint32_t> bits;
			std::vector<uint32_t> wide;
			uint32_t centres;
			bool mediterranean;
			uint64_t fnv;
			size_t zeros;
			size_t untouched;
		};
		const FogCase cases[2] = {
			{{{100, {150, 10}}, {101, {398, 50}}, {102, {250, 95}}, {103, {260, 50}}, {104, {266, 54}}, {105, {380, 18}},
				 {106, {186, 16}}, {107, {300, 80}}},
				{100, 101, 102, 103, 104, 105, 106, 107}, {104, 105, 107}, 0x4A51, true, 0xd3a37fbf21b3688aull, 572, 57974},
			{{{3, {2, 2}}, {7, {398, 148}}, {12, {200, 75}}, {13, {204, 77}}, {40, {0, 140}}, {99, {399, 3}},
				 {100, {120, 149}}, {110, {60, 60}}, {129, {330, 100}}},
				{3, 7, 12, 13, 40, 99, 100, 110, 129}, {7, 13, 100, 129}, 0x7FFF, false, 0x3309082d33eb9501ull, 214, 59070},
		};
		for (const FogCase& fc : cases) {
			SaveGame save;
			save.globals.u16(global::kWorldMerchantCount) = 1;
			save.merchants.resize(1);
			save.expeditions.assign(130, {0, 0});
			for (const auto& [k, p] : fc.points) {
				save.expeditions[k] = p;
			}
			MerchantMemory& m = save.merchants[0].memory;
			for (uint32_t k : fc.bits) {
				m.u32(0x4D8 + 4 * (k >> 5)) |= 1u << (k & 31);
			}
			for (uint32_t k : fc.wide) {
				m.u32(0x508 + 4 * (k >> 5)) |= 1u << (k & 31);
			}
			m.u32(0x538) = fc.centres;
			save.globals.u32(0x6DF278) = 0xFFFFFFFF;  // The members': not his (not a member).
			const TradingMap map = trading_map(save, guild_map_finds(save, 0));
			uint64_t h = 0xCBF29CE484222325ull;
			size_t zeros = 0;
			size_t untouched = 0;
			for (uint8_t b : map.fog) {
				h = (h ^ b) * 0x100000001B3ull;
				zeros += b == 0;
				untouched += b == 200;
			}
			check(map.mediterranean == fc.mediterranean && map.centres == fc.centres, "the trading map: its picture and centres");
			check(map.fog.size() == 400u * 150u && h == fc.fnv && zeros == fc.zeros && untouched == fc.untouched,
				"the trading map's fog as the game's");
		}
		// A member: the members' finds too (0x6DF278 and every member's own), a merchant past the count only those.
		SaveGame save;
		save.globals.u16(global::kWorldMerchantCount) = 3;
		save.merchants.resize(3);
		save.merchants[0].memory.u32(0x468) = 1;
		save.merchants[0].memory.u32(0x4D8) = 1;
		save.merchants[1].memory.u32(0x4D8) = 2;
		save.merchants[1].memory.u32(0x538) = 4;
		save.merchants[2].memory.u32(0x468) = 4;
		save.merchants[2].memory.u32(0x50C) = 8;
		save.globals.u32(0x6DF278 + 0x60) = 0x10;
		ExpeditionFinds f = guild_map_finds(save, 1);
		check(f.points[0] == 2 && f.wide[1] == 0 && f.centres == 4, "not a member: his own finds");
		f = guild_map_finds(save, 2);
		check(f.points[0] == 1 && f.wide[1] == 8 && f.centres == 0x10, "a member: the members' finds");
		f = guild_map_finds(save, 5);
		check(f.points[0] == 1 && f.wide[1] == 8 && f.centres == 0x10, "past the merchants: the members' finds");
		// The centres shown (0x5C63A0): with their bit, found by then by a merchant, inside the map; on the
		// Mediterranean scaled in 16 bits.
		save.globals.u32(global::kWorldTick) = 5000;
		const uint32_t objects = 0x6DEFB0;
		auto centre = [&](uint32_t k, uint8_t place, uint8_t finder, uint32_t tick) {
			save.globals.u8(objects + 0x2C * k + 4) = place;
			save.globals.u8(objects + 0x2C * k + 5) = 1;
			save.globals.u8(objects + 0x2C * k + 6) = finder;
			save.globals.u32(objects + 0x2C * k + 8) = tick;
			save.globals.u8(objects + 0x2C * k + 0) = uint8_t(k);
			save.globals.u8(objects + 0x2C * k + 3) = 7;
		};
		centre(0, 0, 1, 100);  // (248, 22): (116, 11) on the Mediterranean.
		centre(1, 30, 0, 100);  // (93, 8): off the Mediterranean, (93, 8) on the whole map.
		centre(2, 25, 2, 5000);  // (394, 74): (392, 109).
		centre(3, 3, 3, 100);  // Its finder past the merchants.
		centre(4, 4, 0, 5001);  // Not found yet.
		centre(5, 5, 0, 100);  // Its bit not set.
		TradingMap map;
		map.centres = 0x1F;
		map.mediterranean = true;
		std::vector<TradingCentre> shown = trading_centres(save, map);
		check(shown.size() == 2 && shown[0].object == 0 && shown[0].x == 116 && shown[0].y == 11 && shown[1].object == 2 &&
				shown[1].x == 392 && shown[1].y == 109 && shown[1].offer[0] == 2 && shown[1].demand[1] == 7 &&
				shown[1].kind == 1 && shown[1].finder == 2,
			"the centres on the Mediterranean");
		map.mediterranean = false;
		shown = trading_centres(save, map);
		check(shown.size() == 3 && shown[1].object == 1 && shown[1].x == 93 && shown[1].y == 8, "the centres on the whole map");
		// The place under the mouse (0x5BFC40): the nearest within a distance² below 225, the first of the nearest.
		check(trading_centre_at(shown, 93 + 12, 8 + 9) == -1 && trading_centre_at(shown, 93 + 11, 8 + 9) == 1 &&
				trading_centre_at(shown, 248, 22) == 0 && trading_centre_at(shown, 200, 100) == -1,
			"the centre under the mouse");
	}
	if (failures == 0) {
		std::printf("guild_window: ok\n");
	}
	return failures == 0 ? 0 : 1;
}
