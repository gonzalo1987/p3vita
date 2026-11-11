// A new game: 0x544E50 and the operations it runs (new_game.hpp).

#include "p3data/new_game.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "p3data/cpr.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/lz.hpp"
#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/simulation.hpp"
#include "p3data/statistics.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {
namespace merchants_detail {
void fn_4f72c0(MerchantMemory& m);
void fn_4f68d0(SaveGame& save, MerchantMemory& m, uint32_t tick, uint32_t a1, uint8_t a3, uint8_t a4);
}  // namespace merchants_detail
namespace events_towns_detail {
int fn_5308a0_create(SaveGame& save, uint32_t merchant, uint32_t town);
}  // namespace events_towns_detail
namespace events_mayor_detail {
uint16_t fn_507d60(SaveGame& save, uint16_t& out, uint32_t town, uint32_t mode);
void fn_52a8d0(SaveGame& save, int town, int ship, uint32_t type);
void fn_5181d0(SaveGame& save, int ship);
void fn_509a50(SaveGame& save, int ship);
}  // namespace events_mayor_detail
namespace construction_detail {
bool fn_4f9c80(const SaveGame& save, int town, uint8_t& x, uint8_t& y, uint32_t type, uint8_t param, bool flag);
bool fn_521900(SaveGame& save, int town, uint8_t owner, uint8_t x, uint8_t y, uint8_t type, uint8_t param);
}  // namespace construction_detail
void fn_531bd0(SaveGame& save);  // town_tick/new_town.cpp
}  // namespace p3::data::tick

namespace p3::data {

namespace {

using namespace global;

constexpr uint32_t kTradeRoutes = 0x6DD728;  // the player's trade routes: u16 [+0x0], u16 [+0x2] count, [+0x4]
constexpr uint32_t kEvents = 0x6DD73C;  // the event queue: [+0x0] the array, u16 [+0x8] head, [+0xA] free, [+0xC] count
constexpr uint32_t kSieges = 0x6DF270;  // pointer to the towns' sieges
constexpr uint32_t kTownFlags = 0x6DE588;  // 40 slots of 0x40 bytes (0x5352B0)
constexpr uint32_t kSellContext = 0x6DE3D8;  // [+0x64] the trade difficulty's factor (0x52E1A0)

uint8_t op_u8(const uint8_t* op, size_t at) { return op[at]; }
uint16_t op_u16(const uint8_t* op, size_t at) { return uint16_t(op[at] | op[at + 1] << 8); }
uint32_t op_u32(const uint8_t* op, size_t at) {
	return uint32_t(op[at]) | uint32_t(op[at + 1]) << 8 | uint32_t(op[at + 2]) << 16 | uint32_t(op[at + 3]) << 24;
}

// 0x4D64B0 (on the mailbox 0x6DD730): emptied; every merchant's chain too.
void fn_4d64b0(SaveGame& save) {
	GlobalMemory& g = save.globals;
	save.mail.clear();
	save.mail_records.clear();
	save.mail_buffers.clear();
	g.u32(0x6DD730) = 0;
	g.u16(kMailCount) = 0;
	g.u16(kMailFree) = 0;
	g.u16(0x6DD73A) = 0;
	g.u16(kMailChronicle) = 0xFFFF;
	for (uint32_t i = 0; i < g.u16(kWorldMerchantCount) && i < save.merchants.size(); ++i) {
		save.merchants[i].memory.u16(0xA) = 0xFFFF;
	}
}

// 0x4D4C60 (on the trade routes 0x6DD728): emptied.
void fn_4d4c60(SaveGame& save) {
	GlobalMemory& g = save.globals;
	g.u16(kTradeRoutes) = 0;
	g.u16(kTradeRoutes + 2) = 0;
	save.opaque.trade_routes.clear();
	g.u32(kTradeRoutes + 4) = 0;
}

// 0x504750 (on the ships object 0x6DD7A0, two of the operation's random
// words): the pirates, the captains, the ships and the convoys emptied; its
// lists and the 16 entries of [0x6DD7C0] cleared; [+0xEE] = first, [+0xF0]
// from second.
void fn_504750(SaveGame& save, uint16_t first, uint16_t second) {
	GlobalMemory& g = save.globals;
	for (size_t k = 0; k < 5; ++k) {
		// 0x513760 and delete.
		save.pirates[k].reset();
		save.pirate_nests[k] = {};
		g.u32(kShips + 0xC + 4 * uint32_t(k)) = 0;
	}
	g.u16(kShips + 0xF8) = 0;
	save.captains.clear();
	g.u32(kShips) = 0;
	g.u16(kShips + 0xE0) = 0;
	g.u16(kShips + 0xF2) = 0;
	// 0x516840 for each ship: its route.
	save.ships.clear();
	g.u32(kShips + 0x4) = 0;
	g.u16(kShips + 0xF4) = 0;
	g.u16(kShips + 0xE2) = 0;
	save.convoys.clear();
	g.u16(kShips + 0xEE) = first;
	uint16_t w = uint16_t(second & 0x1FF);
	if (w > 0x11D) {
		w = uint16_t(w + 0xFEE3);
	}
	if (w < 0xF) {
		w = uint16_t(w + 0x96);
	}
	g.u32(kShips + 0x8) = 0;
	g.u16(kShips + 0xF6) = 0;
	g.u16(kShips + 0xE4) = 0;
	g.u16(kShips + 0xE6) = 0xFFFF;
	g.u16(kShips + 0xE8) = 0xFFFF;
	g.u16(kShips + 0xEA) = 0xFFFF;
	g.u16(kShips + 0xEC) = 0xFFFF;
	g.u16(kShips + 0xF0) = w;
	for (uint32_t k = 0; k < 16; ++k) {
		const uint32_t e = kShips + 0x20 + 12 * k;
		g.u16(e) = 0xFFFF;
		g.u16(e + 0x2) = 0;
		g.u16(e + 0x8) = 0xFF;
		g.u16(e + 0xA) = 0;
	}
	g.u16(kShips + 0x100) = 0;
	g.u16(kShips + 0x102) = 0;
	g.u16(kShips + 0x104) = 0;
	g.u16(kShips + 0x106) = 0;
}

// 0x530230 (on the world 0x6DE4A0, the new tick): the world records, the
// offices, the merchants, [+0xE4], the towns, the travel times and the sieges
// emptied; the date from the tick; the towns' numbers and maps cleared.
void fn_530230(SaveGame& save, uint32_t tick) {
	GlobalMemory& g = save.globals;
	save.world_records.clear();
	g.u32(kWorld + 0x70) = 0;
	g.u16(kWorld + 0x6) = 0;
	g.u16(kWorld + 0x7C) = 0;
	save.offices.clear();
	g.u32(kWorld + 0x74) = 0;
	g.u16(kWorld + 0x8) = 0;
	g.u16(kWorld + 0x7E) = 0;
	// The merchants' destructor (0x4F3FA0) for each.
	save.merchants.clear();
	save.opaque.merchant_lists.clear();
	save.merchant_count = 0;
	g.u32(kWorld + 0x78) = 0;
	g.u16(kWorld + 0xA) = 0;
	g.u16(kWorld + 0x80) = 0;
	g.u32(kWorld + 0xE4) = 0;  // 0x533E80: a list freed.
	// The towns' destructor (0x51B290) for each.
	save.towns.clear();
	g.u32(kWorld + 0x68) = 0;
	g.u32(kWorld + 0x10) = 0;
	save.town_travel.clear();
	g.u32(kWorld + 0x6C) = 0;
	// The sieges' destructor (0x633520) for each.
	save.sieges.clear();
	save.siege_extras.clear();
	g.u32(kSieges) = 0;
	g.u32(kWorld + 0x14) = tick;
	update_world_date(g);
	g.u32(kWorld + 0xC) = tick;
	g.u8(kWorld + 0x8A) = tick > 0 ? 1 : 0;
	std::memset(&g.u8(kWorld + 0x18), 0, 0x28);
	std::memset(&g.u8(kWorld + 0x40), 0, 0x28);
	g.u8(kWorld + 0x8D) = 0xFF;
	g.u8(kWorld + 0x8E) = 0;
	for (uint32_t k = 0; k < 7; ++k) {
		const uint32_t e = kWorld + 0x90 + 12 * k;
		g.u8(e) = 0xFF;
		g.u8(e + 0x1) = 0xFF;
		g.u32(e + 0x8) = 0;
		g.u8(e + 0x3) = 0;
		g.u32(e + 0x4) = 0;
	}
	g.u8(kWorld + 0x89) = 0;
	g.u8(kWorld + 0x8C) = 0;
	g.u8(kWorld + 0x8B) = 0x5A;
	g.u8(kWorld + 0x8F) = 0xFF;
}

// 0x5352B0 (on 0x6DE588): each of the 40 slots, its 16 u32 the town's [0x2C8]
// | 0x7C0000 (0 past the towns).
void fn_5352b0(SaveGame& save) {
	GlobalMemory& g = save.globals;
	for (uint32_t i = 0; i < 40; ++i) {
		const uint32_t value =
			i < g.u32(kWorldTownCount) ? save.towns[i].memory.u32(0x2C8) | 0x7C0000 : 0;
		for (uint32_t k = 0; k < 16; ++k) {
			g.u32(kTownFlags + 0x40 * i + 4 * k) = value;
		}
	}
}

// 0x4D8B10 (on the event queue 0x6DD73C, the tick and a random word): the
// queue emptied, then its first events.
void fn_4d8b10(SaveGame& save, uint32_t tick, uint32_t random) {
	using tick::events_detail::fn_4d8cf0;
	GlobalMemory& g = save.globals;
	save.events.clear();
	save.opaque.event_extras.clear();
	g.u32(kEvents) = 0;
	g.u16(kEvents + 0x8) = 0xFFFF;
	g.u16(kEvents + 0xA) = 0;
	g.u16(kEvents + 0xC) = 0;
	auto add = [&](uint32_t at) -> GameMemory<0x18>& { return save.events[fn_4d8cf0(save, at)]; };
	const uint32_t r = random & 0xFFFF;
	add((r + tick + 0x5A00) & ~0xFFu).u16(0x6) = 0x20;
	{
		GameMemory<0x18>& e = add(tick + 3);
		e.u16(0x6) = 0;
		e.u32(0xC) = 0;
		e.u32(0x8) = 0;
		e.u8(0x10) = 0xFF;
	}
	{
		GameMemory<0x18>& e = add(tick + 5);
		e.u16(0x6) = 3;
		e.u32(0xC) = 0;
		e.u32(0x8) = 0;
		e.u32(0x10) = 0;
	}
	add(tick + 0x121).u16(0x6) = 6;
	{
		GameMemory<0x18>& e = add(tick + 0xA);
		e.u16(0x6) = 0x19;
		e.u8(0x8) = 0;
	}
	{
		GameMemory<0x18>& e = add(tick + 0xC);
		e.u16(0x6) = 8;
		e.u32(0x8) = random & 0xF;
		e.u32(0xC) = random >> 1;
	}
	{
		GameMemory<0x18>& e = add(tick + 0x14);
		e.u16(0x6) = 0xD;
		e.u32(0x8) = 0;
	}
	{
		// fild of the u64 random >> 2, kept as a float, divided by 1987.0f
		// (0x672828) at the FPU's 53 bits.
		GameMemory<0x18>& e = add(tick + 0x80);
		e.u16(0x6) = 0x16;
		const float kept = float(random >> 2);
		e.f32(0x8) = float(double(kept) / 1987.0);
		e.u32(0xC) = ((tick + random) * 1987u) & 0xFFFF;
	}
	add((tick | 0xFF) + 0x100).u16(0x6) = 0x1A;
	{
		GameMemory<0x18>& e = add(tick + 0x5A00);
		e.u16(0x6) = 0x22;
		e.u32(0x8) = (tick & 0x3FF) % 0x28;
		e.u32(0xC) = random % 0x34 + 1;
	}
	add(tick + 0x10E00).u16(0x6) = 0x2A;
	// The game keeps tick + 0x5A00 in its argument's place (0x4D8C62) and reads
	// it back as the tick for the last two.
	add(tick + 0x5A00).u16(0x6) = 0x2B;
	{
		GameMemory<0x18>& e = add(tick + 0x5A00);
		e.u32(0x8) = 0;
		e.u32(0x10) = 0;
		e.u16(0x6) = 0x2C;
		e.u32(0xC) = 0x5D00;
	}
}

// 0x52E1A0 (on 0x6DE3D8): the trade's difficulty (0 low, 1 normal, 2 or
// more high): the factor of [+0x64] (table 0x6739FC).
void fn_52e1a0(SaveGame& save, uint8_t difficulty) {
	static constexpr float kFactors[3] = {2.2f, 2.0f, 1.8f};
	save.globals.f32(kSellContext + 0x64) = kFactors[difficulty < 2 ? difficulty : 2];
	save.trade_difficulty = TradeDifficulty(difficulty < 2 ? difficulty : 2);
}

// 0x4F3EB0: a merchant's constructor (0x4F5FD0, 0x534750 on [+0x4D8]): no
// home town, offices nor ships, his names' strings, 0x32 in each town's
// [+0xF0], no expeditions, the lists cleared and the reference prices of
// each ware (table 0x673A18 by a barrel's 200 or a load's 2000, 0x672C14).
void fn_4f3eb0(MerchantMemory& m) {
	static constexpr uint32_t kReference[24] = {0x3D6147AF, 0x3EF50482, 0x3E6154CA, 0x3E322D0E, 0x3E11EB85,
		0x3F0CCCCD, 0x3FB33333, 0x3F8CCCCD, 0x3F845A1D, 0x40587AE1, 0x3ED33333, 0x3CE147AF, 0x3FA39581, 0x3F8F5C29,
		0x3EE147AF, 0x3E8E5604, 0x3EE147AF, 0x3E6147AF, 0x3F5AE147, 0x3D236E30, 0x41200000, 0x41000000, 0x41400000,
		0x41600000};  // 0x673A18, floats
	static constexpr uint8_t kBarrel[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
	m.u8(0x19) = 0xFF;
	m.u16(0xA) = 0xFFFF;
	m.u16(0xC) = 0xFFFF;
	m.u16(0xE) = 0xFFFF;
	m.u32(0xE4) = 0;  // 0x512AE0(0): the family name's string (a pointer)
	m.u32(0xE8) = 0;  // 0x512A60(0): the first name's
	std::memset(m.bytes.data() + 0x38, 0, 8);
	m.u32(0x0) = 0;
	std::memset(m.bytes.data() + 0xF0, 0x32, 0x28);
	m.u8(0x24) = 0;
	m.u32(0xEC) = 0;
	m.u8(0x26) = 0x3F;
	m.u32(0x538) = 0;
	std::memset(m.bytes.data() + 0x4D8, 0, 0x30);
	std::memset(m.bytes.data() + 0x508, 0, 0x30);
	std::memset(m.bytes.data() + 0x60C, 0, 0x16);
	std::memset(m.bytes.data() + 0x624, 0, 0x2C);
	for (size_t w = 0; w < 24; ++w) {
		float reference = 0.0f;
		std::memcpy(&reference, &kReference[w], 4);
		const uint16_t v = uint16_t(tick::ftol(double(reference) * (kBarrel[w] ? 200.0 : 2000.0)));
		m.u16(0x548 + 4 * w) = v;
		m.u16(0x54A + 4 * w) = v;
		m.u16(0x5A8 + 4 * w) = v;
		m.u16(0x5AA + 4 * w) = v;
	}
	m.u32(0x608) = 0;
}

// 0x530470 (on the world: home town, kind, two bytes): a new merchant at the
// end (the array grows by 5 slots, 45 the first time): his home town, his
// statistics (0x4F72C0), 1000000 coins, the kind ([+0x8]; a human one, 0,
// sends the chronicle's letter 0xE: not rewritten yet), [+0x546] and [+0x547].
// Returns his index.
uint16_t fn_530470(SaveGame& save, uint8_t home, uint16_t kind, uint8_t a2, uint8_t a3) {
	GlobalMemory& g = save.globals;
	const uint16_t capacity = g.u16(kWorld + 0x80);
	if (g.u16(kWorld + 0xA) >= capacity) {
		g.u16(kWorld + 0xA) = capacity;
		g.u16(kWorld + 0x80) = uint16_t(capacity < 0x28 ? 0x2D : capacity + 5);
	}
	const uint16_t index = g.u16(kWorld + 0xA);
	g.u16(kWorld + 0xA) = uint16_t(index + 1);
	save.merchants.resize(size_t(index) + 1);
	save.opaque.merchant_lists.resize(save.merchants.size());
	save.merchant_count = int(save.merchants.size());
	MerchantMemory& m = save.merchants[index].memory;
	m = MerchantMemory{};
	fn_4f3eb0(m);
	m.u8(0x19) = home;
	tick::merchants_detail::fn_4f72c0(m);
	if (kind == 0) {
		// A human: the chronicle's «new merchant» (the letter 0xE, his rank in
		// his home town and his title, 0x4F9430).
		Letter letter(0xE);
		letter.entry.u8(0x5) = home;
		letter.entry.u16(0x8) = index;
		letter.entry.u16(0xA) = 0xFFFF;
		const uint32_t rank = m.u8(0x39C + home) & 7;
		const uint32_t title = tick::merchants_detail::fn_4f9430(save, index);
		letter.entry.u16(0xE) = 1;
		letter.entry.u16(0xC) = uint16_t(rank + title * 8);
		send_letter(save, kLetterToChronicle, letter);
	} else {
		m.u32(0x468) = 1u << home;
	}
	m.u32(0x53C) = 0;
	m.u32(0x540) = 0;
	m.u16(0x544) = 0;
	m.u16(0x8) = kind;
	m.u32(0x0) = 1000000;
	m.u16(0xE) = 0xFFFF;
	m.u8(0x546) = a2;
	m.u8(0x547) = a3;
	return index;
}

// 0x542560 (on the operation 0xB1: the town, two running seeds and the
// family names already used): the town's merchant (kind 7, a Hanse town's
// 5) with an event 0x04, a new life (0x4F68D0), a first name and an unused
// family name, his office there, the operation's money and four ships from
// towns drawn with the seeds (cogs while fewer than the operation's [+0x6]
// and his home town isn't on a river, snaikkas after), then his day
// (0x4F7BB0).
void fn_542560(SaveGame& save, const uint8_t* op, uint32_t town, uint32_t& pa, uint32_t& pb,
		std::vector<uint8_t>& used) {
	static constexpr uint8_t kCrew[4] = {5, 8, 10, 12};  // 0x673660
	GlobalMemory& g = save.globals;
	const uint32_t families = g.u16(0x6DDB74);
	const uint32_t wc = op_u16(op, 0xC);
	const uint32_t we = op_u16(op, 0xE);
	const uint32_t kind = (~save.towns[town].memory.u8(0x2C8) & 0x40) ? 7 : 5;
	const uint16_t index = fn_530470(save, uint8_t(town), uint16_t(kind), uint8_t(pa), uint8_t(pb));
	if (index >= g.u16(kWorldMerchantCount)) {
		return;
	}
	pa += op_u16(op, 0xC + 2 * (pb & 3));
	pb += op_u16(op, 0xC + 2 * (pa & 3));
	{
		GameMemory<0x18>& e = save.events[tick::events_detail::fn_4d8cf0(save, g.u32(kWorldTick) + index + 0xA00)];
		e.u16(0x6) = 4;
		e.u16(0x8) = uint16_t(index & 0xFF);
		e.u16(0xA) = 0x8300;
		e.u16(0xC) = 0x8300;
		const uint8_t c = e.u8(0x8);
		e.u8(0xE) = c & 7;
		e.u8(0x10) = c & 3;
		e.u8(0x11) = c & 0xF;
		e.u8(0xF) = uint8_t(c + 7) & 0xF;
	}
	MerchantMemory& m = save.merchants[index].memory;
	tick::merchants_detail::fn_4f68d0(save, m, op_u16(op, 0x12), op_u16(op, 0x10), 0, 1);
	const uint32_t v = op_u16(op, 0xC + 2 * (index & 3));
	m.u8(0x1C) = uint8_t((v + index + wc) % g.u16(0x6DDB70));
	m.u8(0x1C) = m.u8(0x1A) ? uint8_t(m.u8(0x1C) & 0x7F) : uint8_t(m.u8(0x1C) | 0x80);  // 0x4F6420
	m.u8(0x1D) = uint8_t((v + index + we) % families);
	while (used[m.u8(0x1D)] != 0) {
		const uint8_t f = m.u8(0x1D) ? m.u8(0x1D) : uint8_t(families);
		m.u8(0x1D) = uint8_t(f - 1);
	}
	// 0x4F6450: the family name's string.
	used[m.u8(0x1D)] = 1;
	m.u8(0x546) = 0;
	tick::events_towns_detail::fn_5308a0_create(save, index, town);
	save.merchants[index].memory.u32(0x0) = op_u32(op, 0x8);
	uint32_t where = (pa % g.u32(kWorldTownCount)) & 0xFFFF;
	uint32_t cogs = 0;
	for (int k = 0; k < 4; ++k) {
		uint16_t ship = 0;
		tick::events_mayor_detail::fn_507d60(save, ship, where, 1);
		where = (uint32_t(uint16_t(pb)) + where) % g.u32(kWorldTownCount);
		if (cogs < op_u16(op, 0x6) && !(save.towns[town].memory.u8(0x2C8) & 1)) {
			tick::events_mayor_detail::fn_52a8d0(save, int(town), ship, 2);
			++cogs;
		} else {
			tick::events_mayor_detail::fn_52a8d0(save, int(town), ship, 0);
		}
		tick::events_mayor_detail::fn_5181d0(save, ship);
		recalculate_cargo(save.ships[ship]);
		ShipMemory& s = save.ships[ship].memory;
		MerchantMemory& owner = save.merchants[index].memory;
		s.u8(0x0) = uint8_t(index);
		s.u16(0x4) = owner.u16(0xE);
		owner.u16(0xE) = ship;
		s.u32(0x18) = s.u32(0x14);
		s.u8(0x13A) = uint8_t(town);
		s.u8(0x13B) = uint8_t(town);
		tick::events_mayor_detail::fn_509a50(save, ship);
		if (s.u8(0xE) == 2) {
			s.u8(0x136) |= 0x10;
		}
		s.u16(0x40) = kCrew[s.u8(0xE) & 3];
	}
	tick::fn_4f7bb0(save, index);
}

// 0x531BA0 (on the world): the travel times [+0x6C] again for every town
// (0x531BD0 without the old matrix).
void fn_531ba0(SaveGame& save) {
	save.town_travel.clear();
	save.globals.u32(kWorld + 0x6C) = 0;
	tick::fn_531bd0(save);
}

// 0x533510 (on the world): the road between Hamburg (number 12) and Lübeck
// (14): its building (0x27) in each, on a site of 0x4F9C80, through 0x521900;
// [0x76C] |= 0x8000 and the first work's time [+0x6] = 1. The game passes the
// other town's index as the first site's y (it writes it).
void fn_533510(SaveGame& save) {
	GlobalMemory& g = save.globals;
	const uint32_t n = g.u32(kWorldTownCount);
	auto find = [&](uint8_t number) -> int {
		for (uint32_t i = 0; i < n; ++i) {
			if (g.u8(kWorld + 0x18 + i) == number) {
				return int(i);
			}
		}
		return -1;
	};
	const int a = find(0xC);
	if (a < 0) {
		return;
	}
	const int b = find(0xE);
	if (b < 0) {
		return;
	}
	uint8_t xa = 0xFF, ya = uint8_t(b), xb = 0xFF, yb = uint8_t(a);
	tick::construction_detail::fn_4f9c80(save, a, xa, ya, 0x27, 0, true);
	tick::construction_detail::fn_4f9c80(save, b, xb, yb, 0x27, 0, true);
	if (xa == 0xFF || xb == 0xFF) {
		return;
	}
	tick::construction_detail::fn_521900(save, a, 0xFF, xa, ya, 0x27, 0);
	tick::construction_detail::fn_521900(save, b, 0xFF, xb, yb, 0x27, 0);
	for (const int t : {a, b}) {
		save.towns[size_t(t)].memory.u32(0x76C) |= 0x8000;
	}
	for (const int t : {a, b}) {
		SavedTown& st = save.towns[size_t(t)];
		const uint16_t head = st.memory.u16(0x762);
		if (head < st.memory.u16(0x776)) {
			st.records[head].pending = 1;  // 0x51F1F0: the record.
		}
	}
}

}  // namespace

void fn_5434e0(SaveGame& save, const uint8_t* operation) {
	fn_4d64b0(save);
	fn_4d4c60(save);
	clear_statistics(save);
	fn_504750(save, op_u16(operation, 0x8), op_u16(operation, 0xA));
	uint32_t tick = op_u32(operation, 0x4);
	if (tick < 0x474A00) {
		tick = (tick & 0xFF) + 0x474A00;  // Before the year 64.
	}
	fn_530230(save, tick);
	fn_5352b0(save);
	fn_4d8b10(save, tick, op_u16(operation, 0xC));
	fn_52e1a0(save, op_u8(operation, 0x10));
}

void fn_542860(SaveGame& save, const uint8_t* operation) {
	GlobalMemory& g = save.globals;
	if (g.u16(kWorldMerchantCount) != 0) {
		return;
	}
	std::vector<uint8_t> used(g.u16(0x6DDB74), 0);
	uint32_t pa = uint32_t(op_u16(operation, 0x10)) * op_u16(operation, 0xC);
	uint32_t pb = uint32_t(op_u16(operation, 0x12)) * op_u16(operation, 0xE);
	for (uint32_t i = 0; i < g.u32(kWorldTownCount); ++i) {
		fn_542560(save, operation, i, pa, pb, used);
	}
	// The navigation object (0x444BA0, 0x444D30) and the routes between the
	// towns (0x4482C0) are Sailing's, not the game's memory.
	fn_531ba0(save);
	fn_533510(save);
}

void fn_542420(SaveGame& save, const uint8_t* operation) {
	GlobalMemory& g = save.globals;
	switch (op_u8(operation, 0x8)) {
	case 1:
		g.u16(0x6DD89A) = 0xE10;
		g.u16(0x6DD89C) = 0x1324;
		g.u16(0x6DD89E) = 0x3C;
		break;
	case 3:
		g.u16(0x6DD89A) = 0x4C90;
		g.u16(0x6DD89C) = 0x57E4;
		g.u16(0x6DD89E) = 0x8C;
		break;
	default:
		g.u16(0x6DD89A) = 0x2710;
		g.u16(0x6DD89C) = 0x2F44;
		g.u16(0x6DD89E) = 0x64;
		break;
	}
	fn_531ba0(save);
	fn_4d64b0(save);
	GameMemory<0x18>& e = save.events[tick::events_detail::fn_4d8cf0(save, g.u32(kWorldTick) + 0x100)];
	e.u16(0x6) = 0xE;
	e.u16(0x8) = op_u16(operation, 0x4);
	e.u16(0xA) = op_u16(operation, 0x6);
}

void fn_52bd80(SaveGame& save, const uint8_t* operation) {
	// 0x673944: by the record's [+0x0] % 50 (below 30), the pair of towns (an
	// index of 0x673920) and the object's [+0x5].
	static constexpr uint8_t kChoice[60] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 2, 1, 2, 0, 2,
		0, 2, 1, 2, 0, 3, 0, 3, 1, 3, 0, 3, 1, 3, 0, 4, 0, 4, 1, 4, 0, 4, 0, 4, 1, 5, 0, 5, 1, 5, 0, 5, 1, 5, 0};
	static constexpr uint8_t kTowns[12] = {7, 18, 7, 8, 8, 18, 7, 18, 6, 18, 6, 8};  // 0x673920
	static constexpr uint8_t kWares[24] = {3, 15, 9, 3, 15, 9, 5, 15, 9, 12, 5, 15, 12, 5, 15, 12, 5, 15, 12, 7, 7, 0, 0,
		0};  // 0x67392C
	const uint32_t index = op_u32(operation, 0x10);
	if (index >= 15) {
		return;
	}
	GlobalMemory& g = save.globals;
	const uint32_t o = kWorld + 0xB10 + 0x2C * index;
	const uint8_t* rec = operation + 4;
	const uint8_t v = uint8_t(rec[0] % 50);
	g.u8(o + 0x4) = v;
	uint32_t pair = 6;
	if (v < 30) {
		pair = kChoice[2 * v];
		g.u8(o + 0x5) = kChoice[2 * v + 1];
		g.u8(o + 0x0) = kTowns[2 * pair];
		g.u8(o + 0x1) = kTowns[2 * pair + 1];
	} else {
		g.u8(o + 0x5) = 2;
		g.u8(o + 0x0) = 0xFF;
		g.u8(o + 0x1) = 0xFF;
	}
	g.u8(o + 0x6) = 0xFF;
	g.u8(o + 0x7) = 0;
	const uint32_t c = pair * 3;
	g.u8(o + 0x2) = kWares[c + rec[2] % 3];
	g.u8(o + 0x3) = kWares[c + (rec[3] & 3)];
	if (g.u8(o + 0x3) == g.u8(o + 0x2)) {
		for (uint32_t i = 0; i < 3; ++i) {
			const uint8_t w = kWares[c + i];
			if (w < 0x18 && w != g.u8(o + 0x2)) {
				g.u8(o + 0x3) = w;
				break;
			}
		}
	}
	g.u32(o + 0x8) = 0xFFFFFFFF;
	g.u32(o + 0xC) = 0;
	g.u32(o + 0x10) = 0;
	g.u32(o + 0x14) = 0;
	g.u32(o + 0x18) = 0;
	g.u16(o + 0x1C) = 0;
	save.world_object_lists[index].clear();
	g.u32(o + 0x28) = 0;
}

void fn_542a30(SaveGame& save, uint8_t* operation) {
	static constexpr uint8_t kCrew[4] = {5, 8, 10, 12};  // 0x673660
	GlobalMemory& g = save.globals;
	uint32_t le = op_u16(operation, 0xE);
	uint32_t lc = op_u16(operation, 0xC);
	uint32_t l10 = op_u16(operation, 0x10);
	uint32_t l12 = op_u16(operation, 0x12);
	std::vector<uint8_t> used(g.u16(0x6DDB74), 0);
	for (uint32_t i = 0; i < g.u16(kWorldMerchantCount); ++i) {
		used[save.merchants[i].memory.u8(0x1D)] = 1;
	}
	if (op_u16(operation, 0x4) > 30) {
		operation[4] = 30;
		operation[5] = 0;
	}
	uint32_t cursor = 0;
	for (uint32_t k = 0; k < op_u16(operation, 0x4); ++k) {
		// The next Hanse town from the cursor on.
		uint32_t home = 0;
		do {
			home = cursor;
			cursor = (cursor + 1) & 0xFFFF;
			if (cursor >= g.u32(kWorldTownCount)) {
				cursor = 0;
			}
		} while (!(save.towns[home & 0xFFFF].memory.u8(0x2C8) & 0x40));
		home &= 0xFFFF;
		// Two floats of the seeds (the FPU at 53 bits, each kept as a float),
		// of which the merchant keeps the low byte ([+0x546], [+0x547]).
		const float fa = float(double(float(int32_t(op_u16(operation, 0xC)))) + 1.0);
		const float q1 = float(double(fa) / (double(float(int32_t(op_u16(operation, 0x10)))) + 1.0));
		const float q2 = float(double(fa) / (double(float(int32_t(op_u16(operation, 0x10)) + 1)) * 7.0));
		uint32_t b1 = 0, b2 = 0;
		std::memcpy(&b1, &q1, 4);
		std::memcpy(&b2, &q2, 4);
		const uint16_t index = fn_530470(save, uint8_t(home), 1, uint8_t(b1), uint8_t(b2));
		if (index >= g.u16(kWorldMerchantCount)) {
			continue;
		}
		{
			GameMemory<0x18>& e = save.events[tick::events_detail::fn_4d8cf0(save, g.u32(kWorldTick) + k + 0xA05)];
			const uint8_t c = uint8_t(index);
			e.u16(0x6) = 4;
			e.u16(0xA) = 0x8300;
			e.u16(0x8) = c;
			e.u16(0xC) = 0xF448;
			e.u8(0xE) = uint8_t(c + 3) & 7;
			e.u8(0x10) = c & 3;
			e.u8(0x11) = c & 0xF;
			e.u8(0xF) = uint8_t(c + 5) & 0xF;
		}
		MerchantMemory& m = save.merchants[index].memory;
		tick::merchants_detail::fn_4f68d0(save, m, l10, l12, 0, 1);
		le = (op_u16(operation, 0xE) + k + le) & 0x3F;
		lc = (op_u16(operation, 0xC) + k + lc) & 0x3F;
		while (used[lc] != 0) {
			lc = (lc + 1) & 0x3F;
		}
		m.u8(0x1D) = uint8_t(lc % g.u16(0x6DDB74));  // 0x4F6450: its string.
		used[lc] = 1;
		m.u8(0x1C) = uint8_t(le % g.u16(0x6DDB70));
		m.u8(0x1C) = m.u8(0x1A) ? uint8_t(m.u8(0x1C) & 0x7F) : uint8_t(m.u8(0x1C) | 0x80);  // 0x4F6420
		const uint32_t a = (op_u16(operation, 0xC + 2 * (l12 & 3)) + l10) & 0xFFFF;
		const uint32_t b = (op_u16(operation, 0xC + 2 * (a & 3)) + l12) & 0xFFFF;
		l10 = a;
		l12 = b;
		tick::events_towns_detail::fn_5308a0_create(save, index, home);
		save.merchants[index].memory.u32(0x53C) = 48 * 10000;  // [0x673841] · 10000
		save.merchants[index].memory.u32(0x0) = op_u32(operation, 0x8);
		uint32_t type = 0;
		for (uint32_t j = 0; j < op_u16(operation, 0x6); ++j) {
			uint16_t ship = 0;
			tick::events_mayor_detail::fn_507d60(save, ship, home, 1);
			tick::events_mayor_detail::fn_52a8d0(save, int(home), ship, type);
			tick::events_mayor_detail::fn_5181d0(save, ship);
			recalculate_cargo(save.ships[ship]);
			ShipMemory& s = save.ships[ship].memory;
			MerchantMemory& owner = save.merchants[index].memory;
			s.u8(0x0) = uint8_t(index);
			s.u16(0x4) = owner.u16(0xE);
			owner.u16(0xE) = ship;
			s.u32(0x18) = s.u32(0x14);
			s.u8(0x13A) = uint8_t(home);
			tick::events_mayor_detail::fn_509a50(save, ship);
			s.u16(0x40) = kCrew[s.u8(0xE) & 3];
			if (!(save.towns[home].memory.u8(0x2C8) & 1)) {
				type = 2;
			}
		}
		tick::fn_4f7bb0(save, index);
		add_merchant_statistics(save, index);
	}
}

bool parse_game_map(const uint8_t* data, size_t size, GameMap& out) {
	out = GameMap{};
	size_t p = 0;
	auto u32 = [&](uint32_t& v) {
		if (p + 4 > size) {
			return false;
		}
		v = uint32_t(data[p]) | uint32_t(data[p + 1]) << 8 | uint32_t(data[p + 2]) << 16 | uint32_t(data[p + 3]) << 24;
		p += 4;
		return true;
	};
	if (!u32(out.count) || out.count > 40) {
		return false;
	}
	if (p + 4 <= size && (data[p] | data[p + 1] << 8) == 0xAFFE) {
		p += 4 + size_t(data[p + 2] | data[p + 3] << 8);
	}
	for (uint32_t i = 0; i < out.count; ++i) {
		if (!u32(out.efficient[i])) {
			return false;
		}
	}
	for (uint32_t i = 0; i < out.count; ++i) {
		if (!u32(out.inefficient[i])) {
			return false;
		}
	}
	for (std::array<uint8_t, 40>* a : {&out.flags, &out.numbers, &out.templates}) {
		if (p + out.count > size) {
			return false;
		}
		std::memcpy(a->data(), data + p, out.count);
		p += out.count;
	}
	for (size_t i = 0; i < 40; ++i) {
		const size_t start = p;
		while (p < size && data[p] != 0) {
			++p;
		}
		if (p >= size) {
			return false;
		}
		out.names[i].assign(reinterpret_cast<const char*>(data + start), p - start);
		++p;
	}
	return true;
}

bool load_game_map(const CprArchive& archive, const std::string& path, GameMap& out, std::string* error) {
	const CprEntry* entry = archive.find(path);
	std::vector<uint8_t> blob;
	if (!entry || !archive.read(*entry, blob) || blob.size() < 4) {
		if (error) {
			*error = path + " is not in the archive";
		}
		return false;
	}
	std::vector<uint8_t> raw;
	if (!lz_decompress_file(blob.data(), blob.size(), raw) || !parse_game_map(raw.data(), raw.size(), out)) {
		if (error) {
			*error = path + " can't be read";
		}
		return false;
	}
	return true;
}

void fn_543570(SaveGame& save, const uint8_t* operation) {
	const uint8_t slot = operation[0x11];
	if (slot >= 0x28 || operation[0x12] > 0x2F || operation[0x13] > 0x27) {
		return;
	}
	std::vector<std::string>& names = save.opaque.town_names;
	if (names.size() < 40) {
		names.resize(40);
	}
	char buffer[0x30] = {};
	std::memcpy(buffer, names[slot].data(), std::min<size_t>(names[slot].size(), 0x30));
	uint8_t at = operation[0x13];
	for (size_t k = 0; k < 0xD; ++k) {
		buffer[at++] = char(operation[4 + k]);
		if (operation[4 + k] == 0 || at >= 0x2F) {
			break;
		}
	}
	buffer[0x2F] = 0;
	names[slot] = buffer;
	// The table 0x6DDA00 at the index of each town of that number.
	std::vector<std::string>& table = save.opaque.town_name_table;
	if (table.size() < 0x28) {
		table.resize(0x28);
	}
	for (uint32_t i = 0; i < 0x28; ++i) {
		if (save.globals.u8(kWorld + 0x18 + i) == slot) {
			table[i] = names[slot];
		}
	}
}

void fn_544e50(SaveGame& save, Sailing* sailing, const GameMap& map) {
	using tick::game_rand;
	const SettingsMemory& settings = save.settings;
	// One operation in the frame for all of them (0x535740: code −1, the rest
	// 0): each writes only its fields, the rest stays from the ones before (the
	// queued 0xC4 carries the end of the 0xB3).
	uint8_t op[0x14] = {0xFF, 0xFF, 0xFF, 0xFF};
	auto run = [&]() { run_operation(save, sailing, op); };
	auto put16 = [&](size_t at, uint32_t v) {
		op[at] = uint8_t(v);
		op[at + 1] = uint8_t(v >> 8);
	};
	auto put32 = [&](size_t at, uint32_t v) {
		put16(at, v & 0xFFFF);
		put16(at + 2, v >> 16);
	};
	// 0xB7: the game starts on 1 March (0x672D7C) of the year (1250 at least).
	const uint32_t year = std::max<uint32_t>(settings.u32(0x0), 0x4E2);
	const uint32_t tick = (year * 365 + 0x3B) << 8;
	put32(0x0, 0xB7);
	put32(0x4, tick);
	for (size_t k = 0; k < 4; ++k) {
		put16(0x8 + 2 * k, game_rand(save));
	}
	op[0x10] = settings.u8(0x10);
	run();
	// 0xAF: the towns, from the last one.
	for (uint32_t i = map.count; i-- > 0;) {
			put32(0x0, 0xAF);
		uint32_t flags = uint32_t(map.flags[i]) | 0x20;
		if (flags & 0x40) {
			flags |= 0x80;
		}
		put32(0x4, flags);
		put32(0x8, map.efficient[i]);
		put32(0xC, map.inefficient[i] | game_rand(save) << 22);
		op[0x10] = map.numbers[i];
		op[0x11] = map.templates[i];
		op[0x12] = uint8_t(i);
		op[0x13] = 1;
		run();
	}
	// 0xB0: the names that aren't the game's, 13 characters at a time.
	for (size_t slot = 0; slot < 40; ++slot) {
		const std::string& name = map.names[slot];
		const std::string game = slot < save.opaque.town_names.size() ? save.opaque.town_names[slot] : std::string();
		const size_t length = name.size();
		if (length == 0 || (game.size() == length && game == name)) {
			continue;
		}
		for (size_t pos = 0; pos < length;) {
					put32(0x0, 0xB0);
			op[0x11] = uint8_t(slot);
			op[0x12] = uint8_t(length);
			op[0x13] = uint8_t(pos);
			for (size_t k = 0; k < 0xD; ++k) {
				op[4 + k] = pos < length ? uint8_t(name[pos++]) : 0;
			}
			run();
		}
	}
	// 0xB1: the town merchants.
	put32(0x0, 0xB1);
	put16(0x4, 0);
	put16(0x6, settings.u8(0xA));
	put32(0x8, 750000);  // 30000 (0x672D20) · 25
	for (size_t k = 0; k < 4; ++k) {
		put16(0xC + 2 * k, game_rand(save));
	}
	run();
	// 0xAC: the game's kind and goal.
	put32(0x0, 0xAC);
	put16(0x4, settings.u8(0xF));
	put16(0x6, settings.u8(0x8));
	op[0x8] = 2;
	run();
	// 0x7E: the next pirate season.
	put32(0x0, 0x7E);
	for (size_t k = 0; k < 4; ++k) {
		put32(0x4 + 4 * k, game_rand(save));
	}
	run();
	// 0xAD: the world objects, two for each pair of towns of table 0x673944 and
	// three more with distinct numbers 30..49.
	static constexpr uint8_t kChoice[62] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 2, 1, 2, 0, 2,
		0, 2, 1, 2, 0, 3, 0, 3, 1, 3, 0, 3, 1, 3, 0, 4, 0, 4, 1, 4, 0, 4, 0, 4, 1, 5, 0, 5, 1, 5, 0, 5, 1, 5, 0, 6, 2};
	uint8_t taken[0x32] = {};
	put32(0x0, 0xAD);
	auto draws = [&]() {
		op[0x5] = uint8_t(game_rand(save) & 1);
		op[0x6] = uint8_t(int32_t(game_rand(save)) % 3);
		op[0x7] = uint8_t(int32_t(game_rand(save)) % 3);
		if (op[0x6] == op[0x7]) {
			op[0x7] = op[0x7] ? uint8_t(op[0x7] - 1) : 2;
		}
	};
	for (uint32_t k = 0; k < 6; ++k) {
		put32(0x10, 2 * k);
		uint8_t first = 0;
		while (kChoice[2 * first] < k) {
			++first;
		}
		uint32_t count = 0;
		while (kChoice[2 * (first + count)] == k) {
			++count;
		}
		op[0x4] = uint8_t(first + int32_t(game_rand(save)) % int32_t(count));
		draws();
		run();
		const uint8_t before = op[0x4];
		do {
			op[0x4] = uint8_t(first + int32_t(game_rand(save)) % int32_t(count));
		} while (op[0x4] == before);
		put32(0x10, 2 * k + 1);
		draws();
		run();
	}
	for (uint32_t k = 12; k < 15; ++k) {
		put32(0x10, k);
		op[0x5] = 2;
		op[0x4] = uint8_t(int32_t(game_rand(save)) % 20 + 0x1E);
		while (taken[op[0x4]]) {
			op[0x4] = uint8_t(op[0x4] + 1);
			if (op[0x4] >= 0x32) {
				op[0x4] = 0x1E;
			}
		}
		op[0x6] = 0;
		op[0x7] = 1;
		taken[op[0x4]] = 1;
		run();
	}
	// 0xB4: the pirates.
	int32_t pirates = int32_t(settings.u8(0x13)) * 2 + 1;
	if (pirates > 5) {
		pirates = 5;
	}
	for (int32_t i = pirates; i-- > 0;) {
			put32(0x0, 0xB4);
		put32(0x4, uint32_t(i));
		put32(0xC, game_rand(save) & 0xFFFF);
		put32(0x8, settings.u8(0x13));
		put32(0x10, game_rand(save));
		run();
	}
	// 0xB3: the AI merchants.
	put32(0x0, 0xB3);
	put16(0x4, settings.u8(0xB));
	put16(0x6, 3u - settings.u8(0xA));
	put32(0x8, 10000);  // 0x672D22
	for (size_t k = 0; k < 4; ++k) {
		put16(0xC + 2 * k, game_rand(save));
	}
	run();
	// 0xC4, queued: the world to 70 days later, a week more for each 5 years
	// after 1300.
	const int32_t from = int32_t(settings.u32(0x0));
	const uint32_t weeks = from > 0x514 ? uint32_t((from - 0x514) / 5) : 0;
	put32(0x0, 0xC4);
	put32(0x4, tick);
	put32(0x8, weeks * 7 * 256 + tick + 0x4600);
	put32(0xC, 0);
	save.queued_operations.emplace_back(op, op + sizeof(op));
}

void fn_541d70(SaveGame& save, Sailing* sailing, uint8_t* op) {
	(void)sailing;
	static constexpr uint8_t kCrew[4] = {5, 8, 10, 12};  // 0x673660
	static constexpr uint16_t kMoney[3] = {30000, 10000, 1000};  // 0x672D20
	static constexpr uint32_t kCareful[3] = {0x3FA66666, 0x3F800000, 0x3F333333};  // 0x672E04: 1.3, 1.0, 0.7
	GlobalMemory& g = save.globals;
	if (op[0x9] > g.u32(kWorldTownCount)) {
		op[0x9] = uint8_t(op[0x9] % g.u32(kWorldTownCount));  // (one past the last stays)
	}
	const uint16_t index = fn_530470(save, op[0x9], 0, uint8_t(op_u16(op, 0xE)),
		uint8_t(op[0xD] + op_u16(op, 0xE)));
	if (index == 0xFFFF) {
		return;
	}
	{
		MerchantMemory& m = save.merchants[index].memory;
		m.u16(0x8) = 0;
		m.u32(0x0) = op[0xA] < 3 ? kMoney[op[0xA]] : 0;
		m.u32(0x4) = 0;
		tick::merchants_detail::fn_4f68d0(save, m, op[0xD], 0x15, 1, op[0xC] != 0 ? 1 : 0);
		// 0x4F9550: a human's [+0x464] by the difficulty.
		if (m.u16(0x8) == 0) {
			std::memcpy(&m.f32(0x464), &kCareful[op[0x10] % 3], 4);
		}
	}
	{
		GameMemory<0x18>& e = save.events[tick::events_detail::fn_4d8cf0(save, g.u32(kWorldTick) + 0x5A00)];
		e.u16(0x6) = 4;
		e.u16(0xA) = 0;
		e.u16(0x8) = uint16_t(index & 0xFF);
		e.u16(0xC) = 0;
		e.u8(0xE) = 0;
		e.u8(0x10) = 0;
		e.u8(0x11) = 0;
		e.u8(0xF) = 0;
	}
	{
		// 0x512BA0: the player's names are a slot of the players' names (after
		// each list of the game's), emptied.
		MerchantMemory& m = save.merchants[index].memory;
		const uint8_t slot = uint8_t(uint8_t(index) - uint8_t(g.u32(kWorldTownCount)) - 0xC);
		if (op[0xC] != 0) {
			m.u8(0x1C) = uint8_t(g.u16(0x6DDB70) + slot);
			if (save.custom_names[0].size() > slot) {
				save.custom_names[0][slot].clear();
			}
		} else {
			m.u8(0x1C) = uint8_t(uint8_t(g.u16(0x6DDB72) + slot) | 0x80);
			if (save.custom_names[1].size() > slot) {
				save.custom_names[1][slot].clear();
			}
		}
		m.u8(0x1D) = uint8_t(g.u16(0x6DDB76) + slot);
		if (save.custom_names[2].size() > slot) {
			save.custom_names[2][slot].clear();
		}
		m.u8(0x1C) = m.u8(0x1A) ? uint8_t(m.u8(0x1C) & 0x7F) : uint8_t(m.u8(0x1C) | 0x80);  // 0x4F6420
	}
	if (op_u32(op, 0x4) == g.u32(0x6DF31C)) {
		g.u32(kPlayer) = index;
		save.player = int(index);
	}
	tick::events_towns_detail::fn_5308a0_create(save, index, op[0x9]);
	while (int8_t(op[0xB]) > 0) {
		op[0xB] = uint8_t(op[0xB] - 1);
		uint16_t ship = 0;
		tick::events_mayor_detail::fn_507d60(save, ship, op[0x9], 1);
		if (op[0x8] == 0 && op[0xB] == 0) {
			// The last one a craier of level 2: its hold less two levels' 5 loads
			// (0x66E048), the first four unusable gun places (0xFF) free.
			tick::events_mayor_detail::fn_52a8d0(save, op[0x9], ship, 1);
			ShipMemory& s = save.ships[ship].memory;
			s.u8(0xF) = 2;
			s.s32(0x10) += -20000;
			tick::events_mayor_detail::fn_5181d0(save, ship);
			uint32_t freed = 0;
			for (size_t k = 0; k < 12; ++k) {
				if (s.u8(0x13C + 2 * k) == 0xFF) {
					s.u8(0x13C + 2 * k) = 0;
					if (++freed >= 4) {
						break;
					}
				}
			}
		} else {
			tick::events_mayor_detail::fn_52a8d0(save, op[0x9], ship, 0);
			tick::events_mayor_detail::fn_5181d0(save, ship);
		}
		recalculate_cargo(save.ships[ship]);
		ShipMemory& s = save.ships[ship].memory;
		MerchantMemory& m = save.merchants[index].memory;
		s.u8(0x0) = uint8_t(index);
		s.u16(0x4) = m.u16(0xE);
		m.u16(0xE) = ship;
		s.u32(0x18) = s.u32(0x14);
		s.u8(0x13A) = op[0x9];
		tick::events_mayor_detail::fn_509a50(save, ship);
		s.u16(0x40) = kCrew[s.u8(0xE) & 3];
		s.u8(0x3F) = 5;
	}
	op[0xB] = uint8_t(op[0xB] - 1);
	tick::fn_4f7bb0(save, index);
	add_merchant_statistics(save, index);
	if (g.u32(0x6DF31C) == 0) {
		// A game of one player (the game object's [+0] & 1): the real-time
		// loop's 0xC0, 0xC6 and 0xC8 (the game speed of the settings [+0x4],
		// table 0x66DE7C, in ms · 1000 / 256).
		static constexpr int32_t kSpeeds[6] = {120, 90, 60, 30, 20, 900};
		uint8_t q[0x14] = {0xFF, 0xFF, 0xFF, 0xFF};
		auto put32 = [&](size_t at, uint32_t v) {
			for (size_t b = 0; b < 4; ++b) {
				q[at + b] = uint8_t(v >> (8 * b));
			}
		};
		put32(0x0, 0xC0);
		tick::enqueue_operation(save, q, sizeof(q));
		put32(0x0, 0xC6);
		tick::enqueue_operation(save, q, sizeof(q));
		const uint32_t speed = save.settings.u32(0x4);
		const int32_t ms = speed < 6 ? kSpeeds[speed] * 1000 : 0;
		put32(0x0, 0xC8);
		put32(0xC, 0);
		put32(0x4, uint32_t(ms / 256));
		put32(0x8, 0xFFFFFFFF);
		put32(0x10, 0xFFFFFFFF);
		tick::enqueue_operation(save, q, sizeof(q));
	}
	// In a network game the player's names follow (0xAA, 0xAB from the
	// interface's [+0x1908] and [+0x1788]); in a game of one player the new
	// game window sends them.
}

void fn_536d90(SaveGame& save, const uint8_t* operation) {
	const uint16_t merchant = op_u16(operation, 0x4);
	if (merchant >= save.globals.u16(kWorldMerchantCount)) {
		return;
	}
	const MerchantMemory& m = save.merchants[merchant].memory;
	size_t list = 2;
	uint32_t index = m.u8(0x1D);
	uint32_t count = save.globals.u16(0x6DDB76);
	if (op_u32(operation, 0x0) == 0xAA) {
		list = (m.u8(0x1A) & 1) ? 0 : 1;
		index = m.u8(0x1C);
		if (list == 1) {
			index &= 0x7F;
		}
		count = save.globals.u16(list ? 0x6DDB72 : 0x6DDB70);
	}
	// 0x512C50: the name's string of the list (a player's slot past the game's
	// names; one of the game's would be written over: no caller does it).
	if (index >= count + 20 || index < count) {
		return;
	}
	std::vector<std::string>& names = save.custom_names[list];
	if (names.size() <= index - count) {
		names.resize(index - count + 1);
	}
	std::string& name = names[index - count];
	if (name.size() >= 0x17) {
		return;
	}
	size_t length = name.size();
	for (size_t i = 0;; ++i) {
		++length;
		if (i >= 0xC || operation[8 + i] == 0) {
			break;
		}
		name.push_back(char(operation[8 + i]));
		if (length >= 0x17) {
			break;
		}
	}
}

const std::array<std::array<uint8_t, 8>, 5> kNewGameLevelDetails = {{
	{1, 1, 1, 1, 1, 1, 1, 2},
	{1, 1, 1, 1, 2, 2, 2, 2},
	{2, 2, 2, 2, 2, 2, 2, 2},
	{2, 2, 2, 2, 3, 3, 3, 2},
	{3, 3, 3, 3, 3, 3, 3, 2},
}};

void clamp_new_game_choices(NewGameChoices& c, uint32_t speed_before) {
	if (int32_t(c.year) < 1300 || int32_t(c.year) > 1400) {
		c.year = 1350;
	}
	c.year = uint32_t(int32_t(c.year) / 5 * 5);
	if (int32_t(c.level) < 0 || int32_t(c.level) > 4) {
		c.level = 2;
	}
	if (int32_t(c.goal) < 0 || int32_t(c.goal) > 7 || c.goal == 1 || c.goal == 7) {
		c.goal = 0;
	}
	// Table 0x497EB8, by the goal.
	const int32_t time = int32_t(c.goal_time);
	switch (c.goal) {
	case 0:
		if (time > 0) {
			c.goal_time = 0;
		}
		break;
	case 1:
	case 2:
	case 3:
		if (time > 14) {
			c.goal_time = 14;
		}
		break;
	case 4:
	case 5:
		if (time >= 13) {
			c.goal_time = 13;
		}
		break;
	case 6:
		if (time >= 2) {
			c.goal_time = 2;
		}
		break;
	default:
		if (time >= 4) {
			c.goal_time = 4;
		}
		break;
	}
	if (int32_t(c.goal_time) < 0) {
		c.goal_time = 0;
	}
	if (int32_t(c.speed) < 0 || int32_t(c.speed) > 5) {
		c.speed = speed_before;
	}
	for (size_t k = 0; k < c.details.size(); ++k) {
		uint8_t& v = c.details[k];
		if (v < 1) {
			v = 1;
		}
		if (v > 2) {
			v = k == 7 ? 2 : 3;
		}
	}
}

uint32_t new_game_level_of(const std::array<uint8_t, 8>& details) {
	static constexpr int32_t kSums[5] = {9, 12, 16, 19, 23};  // 0x66F6B0
	int32_t sum = 0;
	for (uint8_t v : details) {
		sum += v;
	}
	int32_t nearest = 0x24;
	uint32_t level = 0;
	for (uint32_t k = 0; k < 5; ++k) {
		const int32_t d = std::abs(sum - kSums[k]);
		if (d < nearest) {
			nearest = d;
			level = k;
		}
	}
	return level;
}

uint32_t new_game_goal_time_limit(uint32_t goal) {
	switch (goal) {
	case 0:
		return 0;
	case 1:
	case 2:
	case 3:
		return 14;
	case 4:
	case 5:
		return 13;
	case 6:
		return 2;
	default:
		return 4;
	}
}

void press_new_game_ball(NewGameWindowState& state, NewGameBall ball) {
	NewGameChoices& c = state.choices;
	if (!state.saved) {
		state.saved = true;
		state.saved_time = c.goal_time;
	}
	switch (ball) {
	case NewGameBall::GenderLeft:
		c.female = false;
		break;
	case NewGameBall::GenderRight:
		c.female = true;
		break;
	case NewGameBall::YearMinus:
		c.year -= 5;
		if (int32_t(c.year) <= 1300) {
			c.year = 1300;
		}
		break;
	case NewGameBall::YearPlus:
		c.year += 5;
		if (int32_t(c.year) >= 1400) {
			c.year = 1400;
		}
		break;
	case NewGameBall::LevelMinus:
		c.level -= 1;
		if (int32_t(c.level) <= 0) {
			c.level = 0;
		}
		c.details = kNewGameLevelDetails[c.level];
		break;
	case NewGameBall::LevelPlus:
		c.level += 1;
		if (int32_t(c.level) >= 4) {
			c.level = 4;
		}
		c.details = kNewGameLevelDetails[c.level];
		break;
	case NewGameBall::GoalLeft:
	case NewGameBall::GoalRight: {
		const uint32_t goal = ball == NewGameBall::GoalLeft ? c.goal - 1 : c.goal + 1;
		c.goal = goal;
		if (ball == NewGameBall::GoalLeft) {
			if (goal == 0 || goal == 6) {
				c.goal_time = 0;
			} else if (goal == 5) {
				c.goal_time = state.saved_time;  // Back from the Hanse's population.
			}
			if (goal == 1) {
				c.goal = 0;  // No «Councillor».
			}
			if (int32_t(c.goal) <= 0) {
				c.goal = 0;
			}
		} else {
			if (goal == 1) {
				c.goal = 2;
			} else if (goal == 4) {
				if (int32_t(c.goal_time) > 13) {
					c.goal_time = 13;
				}
			} else if (goal == 6 || goal == 7) {
				state.saved_time = c.goal_time;
				c.goal_time = 0;
			}
			if (int32_t(c.goal) >= 6) {
				c.goal = 6;
			}
		}
		// 0x499AF0: the time within the new goal's limit (none without a goal).
		const uint32_t limit = new_game_goal_time_limit(c.goal);
		if (c.goal == 0 || int32_t(c.goal_time) > int32_t(limit)) {
			c.goal_time = limit;
		}
		break;
	}
	case NewGameBall::TimeMinus:
		c.goal_time -= 1;
		if (int32_t(c.goal_time) <= 0) {
			c.goal_time = 0;
		}
		break;
	case NewGameBall::TimePlus: {
		c.goal_time += 1;
		const uint32_t limit = new_game_goal_time_limit(c.goal);
		if (c.goal <= 7 && int32_t(c.goal_time) >= int32_t(limit)) {
			c.goal_time = limit;
		}
		break;
	}
	case NewGameBall::SpeedMinus:
		c.speed = c.speed == 0 ? 5 : c.speed - 1;
		break;
	case NewGameBall::SpeedPlus:
		if (c.speed == 5) {
			c.speed = 0;
		} else if (int32_t(++c.speed) >= 4) {
			c.speed = 4;
		}
		break;
	case NewGameBall::Count:
		break;
	}
}

bool new_game_ball_enabled(const NewGameChoices& c, NewGameBall ball) {
	switch (ball) {
	case NewGameBall::GenderLeft:
		return c.female;
	case NewGameBall::GenderRight:
		return !c.female;
	case NewGameBall::YearMinus:
		return int32_t(c.year) > 1300;
	case NewGameBall::YearPlus:
		return int32_t(c.year) < 1400;
	case NewGameBall::LevelMinus:
		return int32_t(c.level) > 0;
	case NewGameBall::LevelPlus:
		return int32_t(c.level) < 4;
	case NewGameBall::GoalLeft:
		return int32_t(c.goal) > 0;
	case NewGameBall::GoalRight:
		return int32_t(c.goal) < 6;
	case NewGameBall::TimeMinus:
		return c.goal_time != 0;
	case NewGameBall::TimePlus:
		return c.goal != 0 && int32_t(c.goal_time) < int32_t(new_game_goal_time_limit(c.goal));
	case NewGameBall::SpeedMinus:
		return c.speed != 5;
	case NewGameBall::SpeedPlus:
		return c.speed != 4;
	case NewGameBall::Count:
		break;
	}
	return false;
}

bool new_game_ball_shown(const NewGameChoices& c, NewGameBall ball) {
	if (ball == NewGameBall::TimeMinus || ball == NewGameBall::TimePlus) {
		return c.goal != 0;
	}
	return ball != NewGameBall::Count;
}

uint32_t next_hanse_town(const GameMap& map, uint32_t index) {
	uint32_t i = index;
	for (uint32_t n = 0; n <= map.count; ++n) {
		if (++i >= map.count) {
			i = 0;
		}
		if (map.flags[i] & 0x40) {
			return i;
		}
	}
	return index;  // The game loops for ever (no map without a Hanse town).
}

uint32_t previous_hanse_town(const GameMap& map, uint32_t index) {
	uint32_t i = index;
	for (uint32_t n = 0; n <= map.count; ++n) {
		if (i == 0) {
			i = map.count;
		}
		if (i == 0 || i > map.flags.size()) {
			break;
		}
		const uint8_t flags = map.flags[i - 1];
		--i;
		if (flags & 0x40) {
			return i;
		}
	}
	return index;
}

const std::string& game_map_town_name(const GameMap& map, uint32_t index) {
	if (index >= map.count) {
		index = 0;
	}
	const uint8_t number = map.numbers[index % map.numbers.size()];
	return map.names[number % map.names.size()];
}

std::vector<uint8_t> game_map_town_wares(const GameMap& map, uint32_t index, bool efficient) {
	static constexpr uint8_t kOrder[17] = {9, 2, 3, 12, 5, 0, 1, 11, 8, 4, 16, 14, 7, 18, 19, 15, 17};  // 0x672C30
	std::vector<uint8_t> wares;
	if (index >= map.count) {
		return wares;
	}
	const uint32_t bits = efficient ? map.efficient[index] : map.inefficient[index];
	for (uint32_t k = 0; k < 17; ++k) {
		if (bits & (1u << k)) {
			wares.push_back(kOrder[k]);
		}
	}
	if (bits & 0x40) {
		wares.push_back(13);  // leather (0x6C0BE4)
	}
	if (bits & 0x20000) {
		wares.push_back(10);  // whale oil (0x6C0BD8)
		wares.push_back(2);  // fish (0x6C0BB8)
	}
	return wares;
}

SettingsMemory initial_game_settings() {
	SettingsMemory s;
	s.u32(0x0) = 0x541;
	s.u8(0xB) = 12;  // *0x66E920
	s.u8(0xA) = 1;
	s.u8(0xC) = 1;
	s.u8(0x17) = 1;
	s.u8(0x25) = 1;  // TOOLTIPS
	s.u8(0x26) = 1;  // VIDEOS
	return s;
}

SettingsMemory new_game_settings(const NewGameChoices& c, const GameMap& map, const SettingsMemory& before,
		bool tooltips) {
	// The last detail's table 0x66F6A4 (u32, read by the detail's value).
	static constexpr uint8_t kStart[4] = {2, 1, 2, 9};
	SettingsMemory s = before;
	s.u32(0x0) = c.year;
	s.u8(0xF) = uint8_t(c.goal);
	s.u8(0xB) = 12;
	s.u8(0xA) = kStart[std::min<size_t>(c.details[7], 3)];
	s.u8(0x19) = c.production != 0 ? 1 : 0;
	s.u8(0xE) = uint8_t(c.level);
	s.u8(0xC) = 1;
	s.u32(0x4) = c.speed;
	s.u8(0x9) = uint8_t(c.details[6] - 1);
	s.u8(0xD) = 3;  // [+0x1BB4]: the window opened for a game of one player (0x42F6E7).
	s.u8(0x11) = uint8_t(c.details[1] - 1);
	s.u8(0x16) = uint8_t(c.details[5] - 1);
	s.u8(0x13) = uint8_t(c.details[4] - 1);
	s.u8(0x10) = uint8_t(c.details[0] - 1);
	s.u8(0x12) = uint8_t(c.details[3] - 1);
	s.u8(0x17) = uint8_t(c.details[2] - 1);
	s.u8(0x25) = tooltips ? 1 : 0;  // *0x6CBDF0 [+0x184]
	s.u8(0x1E) = uint8_t(c.home);
	// The home town's name (strcpy; the game's fit).
	const std::string& name = game_map_town_name(map, c.home);
	const size_t length = std::min<size_t>(name.size(), SettingsMemory::kSize - 0x28);
	std::memcpy(s.bytes.data() + 0x27, name.data(), length);
	s.u8(0x27 + length) = 0;
	// The goal (jump tables 0x4999E4 by the kind and 0x499A00 by the time); a
	// time the window doesn't give leaves it as it was.
	if (c.goal == 0) {
		s.u8(0x8) = 0xE;
	} else if (c.goal <= 5) {
		if (c.goal_time <= 14) {
			s.u8(0x8) = uint8_t(c.goal_time);
		}
	} else if (c.goal == 6 && c.goal_time <= 2) {
		s.u8(0x8) = uint8_t(0x10 + c.goal_time);
	}
	return s;
}

NewGamePlayer new_game_player(const NewGameChoices& c) {
	// Table 0x66F6B0 − 4 · the last detail (u32: 9, 2, 1, 2).
	static constexpr uint8_t kShips[4] = {9, 2, 1, 2};
	NewGamePlayer player;
	player.ships = kShips[std::min<size_t>(c.details[7], 3)];
	player.male = !c.female;
	player.first_name = c.first_name;
	player.family_name = c.family_name;
	return player;
}

void start_new_game(SaveGame& save, Sailing& sailing, const GameMap& map, const NewGamePlayer& player, uint32_t seed,
		NewGameStop stop) {
	using tick::game_rand;
	use_sailing(sailing);
	save.rand_state = seed;  // 0x547F10: srand(time)
	fn_544e50(save, &sailing, map);
	if (stop == NewGameStop::Operations) {
		return;
	}
	// The window: the player (0xA7) and the game speed (0xC8), queued.
	const SettingsMemory& s = save.settings;
	uint8_t a7[0x14] = {};
	a7[0x0] = 0xA7;
	std::memcpy(a7 + 4, &save.globals.u32(0x6DF31C), 4);
	a7[0x8] = s.u8(0xE);
	a7[0x9] = s.u8(0x1E);
	a7[0xA] = s.u8(0x9);
	a7[0xB] = player.ships;
	a7[0xC] = player.male ? 1 : 0;
	a7[0xD] = uint8_t(int32_t(game_rand(save)) % 0x16D);
	const uint16_t r = uint16_t(game_rand(save));
	a7[0xE] = uint8_t(r);
	a7[0xF] = uint8_t(r >> 8);
	a7[0x10] = s.u8(0x11);
	save.queued_operations.emplace_back(a7, a7 + sizeof(a7));
	{
		static constexpr int32_t kSpeeds[6] = {120, 90, 60, 30, 20, 900};  // 0x66DE7C
		const uint32_t speed = s.u32(0x4);
		const int32_t ms = speed < 6 ? kSpeeds[speed] * 1000 : 0;
		uint8_t c8[0x14] = {0xC8};
		const uint32_t fields[4] = {uint32_t(ms / 256), 2, 0, 0xFFFFFFFF};
		std::memcpy(c8 + 4, fields, sizeof(fields));
		save.queued_operations.emplace_back(c8, c8 + sizeof(c8));
	}
	// The queue (0x546870 copies it and runs the copy): the 0xC4 takes the world
	// to its date in one advance_time, whose operations wait in the queue
	// behind the window's.
	std::vector<std::vector<uint8_t>> batch;
	batch.swap(save.queued_operations);
	for (std::vector<uint8_t>& op : batch) {
		uint32_t code = 0;
		std::memcpy(&code, op.data(), 4);
		if (code == 0xC4) {
			uint32_t target = 0;
			std::memcpy(&target, op.data() + 8, 4);
			while (save.tick() < target) {
				advance_game_tick(save, sailing, false);
			}
			cap_merchant_money(save);
			if (stop == NewGameStop::Advance) {
				return;
			}
		} else if (code == 0xA7) {
			fn_541d70(save, &sailing, op.data());
		} else {
			run_operation(save, &sailing, op.data());
		}
	}
	// The window sends the player's names, 12 characters, then 11 at a time.
	for (const auto& [code, name] : {std::pair<uint32_t, const std::string*>{0xAA, &player.first_name},
			 std::pair<uint32_t, const std::string*>{0xAB, &player.family_name}}) {
		uint8_t q[0x14] = {};
		std::memcpy(q, &code, 4);
		const uint16_t who = uint16_t(save.globals.u32(kPlayer));
		q[4] = uint8_t(who);
		q[5] = uint8_t(who >> 8);
		size_t offset = 0;
		size_t i = 0;
		for (; i < name->size(); ++i) {
			q[8 + i - offset] = uint8_t((*name)[i]);
			if (i % 11 == 0 && i != 0) {
				save.queued_operations.emplace_back(q, q + sizeof(q));
				offset = i + 1;
				q[6] = uint8_t(offset);
				q[7] = uint8_t(offset >> 8);
			}
		}
		q[8 + i - offset] = 0;
		save.queued_operations.emplace_back(q, q + sizeof(q));
	}
	// The next frame's queue: what the days queued, the 0xA7's and the names.
	run_queued_operations(save, sailing);
}

}  // namespace p3::data
