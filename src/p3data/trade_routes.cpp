#include "p3data/trade_routes.hpp"

#include <cstring>

#include "p3data/game_fixes.hpp"
#include "p3data/lz.hpp"
#include "p3data/lz_compress.hpp"
#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/simulation.hpp"
#include "p3data/trade.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {
bool fn_52acd0(SaveGame& save, int town, int ship);  // town_tick/ship_operations.cpp: into the shipyard
namespace offices_detail {
int32_t fn_52ea80(const SaveGame& save, uint32_t ware, int town, int32_t price);  // town_tick/offices.cpp
int32_t fn_52ec50(const SaveGame& save, uint32_t ware, int town, int32_t price, uint32_t quality);
}  // namespace offices_detail
}  // namespace p3::data::tick

namespace p3::data {

namespace {

using namespace global;

constexpr uint32_t kFreeStop = 0x6DD728;  // u16 [+0x0] of the pool: the free list's head
constexpr uint32_t kStopCount = 0x6DD72A;  // u16 [+0x2]: the records
constexpr uint32_t kConvoyHead = 0x6DD88C;  // u16 [+0xEC] of the ship object: the convoys that exist
constexpr uint16_t kGrowth = 150;  // 0x96: records added when none is free

// 0x673A18 (a float by ware): the base price of a unit (the bits, as in the exe).
constexpr uint32_t kBasePriceBits[24] = {0x3D6147AF, 0x3EF50482, 0x3E6154CA, 0x3E322D0E, 0x3E11EB85, 0x3F0CCCCD,
	0x3FB33333, 0x3F8CCCCD, 0x3F845A1D, 0x40587AE1, 0x3ED33333, 0x3CE147AF, 0x3FA39581, 0x3F8F5C29, 0x3EE147AF,
	0x3E8E5604, 0x3EE147AF, 0x3E6147AF, 0x3F5AE147, 0x3D236E30, 0x41200000, 0x41000000, 0x41400000, 0x41600000};
// 0x672C14 (a byte by ware): 1 for the wares in barrels, whose stop price is 200 units' (0x66A91C), else 2000's
// (0x66A918).
constexpr uint8_t kInBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
// 0x698538: the default order of the wares.
constexpr uint8_t kDefaultOrder[24] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23};

uint32_t op_u32(const uint8_t* op, size_t at) {
	uint32_t v;
	std::memcpy(&v, op + at, 4);
	return v;
}

uint16_t op_u16(const uint8_t* op, size_t at) {
	uint16_t v;
	std::memcpy(&v, op + at, 2);
	return v;
}

int32_t op_s32(const uint8_t* op, size_t at) {
	int32_t v;
	std::memcpy(&v, op + at, 4);
	return v;
}

void set_op_u32(uint8_t* op, size_t at, uint32_t v) {
	std::memcpy(op + at, &v, 4);
}

uint32_t stop_count(const SaveGame& save) {
	return save.globals.u16(kStopCount);
}

uint32_t ship_count(const SaveGame& save) {
	return save.globals.u16(kShipCount);
}

// A stop the game reads without looking: p3vita stops at the records it has (an index past them is never made by
// the game's own operations).
bool valid_stop(const SaveGame& save, uint32_t stop) {
	return stop < save.trade_routes.size();
}

}  // namespace

uint16_t fn_4d4c90(SaveGame& save) {
	GlobalMemory& g = save.globals;
	std::vector<TradeStop>& stops = save.trade_routes;
	if (g.u16(kFreeStop) >= g.u16(kStopCount)) {
		// A new array of count + 150 records, the old ones copied (operator new; the game gives up with 0xFFFF
		// when it fails).
		uint16_t count = g.u16(kStopCount);
		stops.resize(size_t(count) + kGrowth);
		for (int k = 0; k < kGrowth; ++k) {
			TradeStop& s = stops[count];
			++count;
			s.u8(0x2) = 0xFF;
			s.u8(0x3) = 0;
			s.u16(0x0) = count;
		}
		g.u16(kStopCount) = count;
	}
	const uint16_t index = g.u16(kFreeStop);
	if (index >= stops.size()) {
		return 0xFFFF;  // never with the pool as the game leaves it (the game would read past its array)
	}
	TradeStop& s = stops[index];
	g.u16(kFreeStop) = s.u16(0x0);
	s.u16(0x0) = index;
	s.u8(0x2) = 0;  // [+0x3] keeps what it had.
	for (int w = 23; w >= 0; --w) {
		float price;
		std::memcpy(&price, &kBasePriceBits[w], sizeof(price));
		s.s32(0x1C + 4 * w) = int32_t(double(price) * (kInBarrels[w] ? 200.0 : 2000.0));  // __ftol of the x87 product
		s.s32(0x7C + 4 * w) = 0;
		s.u8(0x4 + w) = kDefaultOrder[w];
	}
	return index;
}

void fn_4d4dd0(SaveGame& save, uint32_t stop, bool ships) {
	GlobalMemory& g = save.globals;
	std::vector<TradeStop>& stops = save.trade_routes;
	if (stop >= stop_count(save) || !valid_stop(save, stop)) {
		return;
	}
	// A chain that doesn't come back (never made by the game) stops after every record.
	const size_t limit = stops.size();
	if (ships) {
		uint32_t s = stop;
		size_t n = 0;
		do {
			fn_509030(save, uint16_t(s), 0xFFFF);
			s = stops[s].u16(0x0);
		} while (s != stop && valid_stop(save, s) && ++n < limit);
	}
	uint32_t last = stop;
	size_t n = 0;
	while (stops[last].u16(0x0) != stop && valid_stop(save, stops[last].u16(0x0)) && ++n < limit) {
		stops[last].u8(0x3) = 0;  // all but the last
		last = stops[last].u16(0x0);
	}
	stops[last].u16(0x0) = g.u16(kFreeStop);
	g.u16(kFreeStop) = uint16_t(stop);
}

void fn_4d4e80(SaveGame& save, uint32_t stop) {
	GlobalMemory& g = save.globals;
	std::vector<TradeStop>& stops = save.trade_routes;
	const uint32_t count = g.u16(kStopCount);
	uint32_t free = g.u16(kFreeStop);
	while (free < count) {
		if (free == stop) {
			fn_509030(save, uint16_t(stop), 0xFFFF);  // already free
			return;
		}
		free = valid_stop(save, free) ? stops[free].u16(0x0) : count;
	}
	if (!valid_stop(save, stop)) {
		return;
	}
	uint16_t to = 0xFFFF;
	if (stops[stop].u16(0x0) < count) {
		// Game bug: the game reads the next of the record at the free list's end (always past the pool) where it
		// meant the stop's own next. That is heap memory after the array; in the oracle (its blocks zeroed when
		// made) a 0. Harmless from operation 0x6A, which has moved the ships away before.
		to = fix_game_bugs() ? stops[stop].u16(0x0) : (free < stops.size() ? stops[free].u16(0x0) : 0);
	}
	fn_509030(save, uint16_t(stop), to);
	uint32_t prev = stop;
	size_t n = 0;
	while (stops[prev].u16(0x0) != stop && valid_stop(save, stops[prev].u16(0x0)) && ++n < stops.size()) {
		prev = stops[prev].u16(0x0);
	}
	stops[prev].u16(0x0) = stops[stop].u16(0x0);
	stops[stop].u16(0x0) = g.u16(kFreeStop);
	g.u16(kFreeStop) = uint16_t(stop);
}

void fn_4d4f60(SaveGame& save, uint16_t& head, uint32_t a, uint32_t b, bool first) {
	GlobalMemory& g = save.globals;
	std::vector<TradeStop>& stops = save.trade_routes;
	const uint32_t count = g.u16(kStopCount);
	const size_t limit = stops.size();
	auto next = [&](uint32_t s) -> uint32_t { return valid_stop(save, s) ? stops[s].u16(0x0) : 0xFFFF; };
	if (a == b) {
		// `a` becomes the first stop.
		if (!first || a >= count || !valid_stop(save, a)) {
			return;
		}
		uint32_t n = count;
		uint32_t s = b;
		for (;;) {
			stops[s].u8(0x3) &= 0xFB;
			s = stops[s].u16(0x0);
			if (s >= count || !valid_stop(save, s) || --n < 1 || s == a) {
				break;
			}
		}
		stops[a].u8(0x3) |= 4;
		return;
	}
	if (b < count && valid_stop(save, b) && stops[b].u16(0x0) == a) {
		// Already after `b`.
		if (!first || !valid_stop(save, a)) {
			return;
		}
		stops[a].u8(0x3) |= 4;
		uint32_t s = a;
		for (size_t n = 0; n < limit; ++n) {
			s = next(s);
			if (!valid_stop(save, s)) {
				return;
			}
			if (stops[s].u8(0x3) & 4) {
				stops[s].u8(0x3) -= 4;
			}
			if (count == 0 || b == s) {
				return;
			}
		}
		return;
	}
	// The stop before `a`, from the head, at most `count` steps.
	uint32_t n = count;
	const uint32_t h = head;
	if (!valid_stop(save, h) || !valid_stop(save, a)) {
		return;
	}
	uint32_t prev = h;
	uint32_t after = stops[h].u16(0x0);
	while (after != a) {
		prev = after;
		if (--n == 0 || prev == h || !valid_stop(save, prev)) {
			return;
		}
		after = stops[prev].u16(0x0);
	}
	if (prev == a) {
		// Alone in its route: only the head's is removed, and the route with it.
		if (a != h) {
			return;
		}
		stops[prev].u16(0x0) = g.u16(kFreeStop);
		g.u16(kFreeStop) = uint16_t(prev);
		head = 0xFFFF;
		return;
	}
	if ((stops[a].u8(0x3) & 4) == 4) {
		stops[a].u8(0x3) -= 4;
		const uint32_t s = stops[a].u16(0x0);
		if (valid_stop(save, s)) {
			stops[s].u8(0x3) |= 4;
		}
	}
	stops[prev].u16(0x0) = stops[a].u16(0x0);
	uint32_t s = stops[prev].u16(0x0);
	bool found = false;
	if (s != prev) {
		for (size_t k = 0; k < limit && valid_stop(save, s); ++k) {
			if (s == b) {
				found = true;
				break;
			}
			s = stops[s].u16(0x0);
			if (s == prev) {
				break;
			}
		}
	}
	if (!found) {
		// `b` isn't in the route: `a` is freed.
		stops[a].u16(0x0) = g.u16(kFreeStop);
		g.u16(kFreeStop) = uint16_t(a);
		if (head == a) {
			head = stops[prev].u16(0x0);
		}
		return;
	}
	stops[a].u16(0x0) = stops[b].u16(0x0);
	stops[b].u16(0x0) = uint16_t(a);
	if (!first) {
		return;
	}
	stops[a].u8(0x3) |= 4;
	n = count;
	s = stops[a].u16(0x0);
	while (s != a && valid_stop(save, s)) {
		if (n-- == 0) {
			return;
		}
		stops[s].u8(0x3) &= 0xFB;
		s = stops[s].u16(0x0);
	}
}

bool fn_4d5ab0(SaveGame& save, int ship) {
	SavedShip& saved = save.ships[size_t(ship)];
	const ShipMemory& s = saved.memory;
	if (!valid_stop(save, s.u16(0x132))) {
		return false;  // the game reads past its array
	}
	const TradeStop& stop = save.trade_routes[s.u16(0x132)];
	const uint8_t town = stop.u8(0x2);
	if (town >= save.towns.size() || s.u8(0x0) >= save.merchants.size()) {
		return false;  // the game doesn't look (its operations only put real towns in the stops)
	}
	const uint32_t flags = save.towns[town].memory.u32(0x2C8);
	// The merchant's bit of the town in [+0x608] (trade banned there): `shl` by the town, which x86 masks to 5 bits.
	const bool may_trade = (save.merchants[s.u8(0x0)].memory.u32(0x608) & (1u << (town & 31))) == 0;
	if (flags & 0x4000E00) {
		return false;
	}
	if (stop.u8(0x3) & 8) {
		return false;
	}
	if (s.u8(0xE) >= 2) {
		if (flags & 1) {
			return false;  // a big ship into a town without a deep port
		}
	} else if (s.u16(0x8) < save.convoy_count() && save.convoys[s.u16(0x8)].u8(0x1) >= 2 && (flags & 1)) {
		return false;  // a convoy with a big ship, likewise
	}
	const bool office = tick::facilities_detail::fn_5308a0(save, s.u8(0x0), town) >= 0;
	if (may_trade) {
		for (int w = 0; w < 24; ++w) {
			if (stop.s32(0x1C + 4 * w) > 0 && stop.s32(0x7C + 4 * w) > 0 && s.s32(0x54 + 4 * w) > 0) {
				return true;  // something to sell
			}
		}
	}
	bool take = false;
	if (office) {
		for (int w = 0; w < 24; ++w) {
			if (stop.s32(0x1C + 4 * w) != 0) {
				continue;
			}
			const int32_t amount = stop.s32(0x7C + 4 * w);
			if (amount > 0) {
				take = true;  // something to take from the office (with room, below)
			} else if (amount < 0 && s.s32(0x54 + 4 * w) > 0) {
				return true;  // something to put in it
			}
		}
	}
	if (recalculate_cargo(saved) <= 0) {
		return false;
	}
	if (take) {
		return true;
	}
	if (may_trade) {
		for (int w = 0; w < 24; ++w) {
			if (stop.s32(0x1C + 4 * w) < 0 && stop.s32(0x7C + 4 * w) > 0) {
				return true;  // something to buy
			}
		}
	}
	return false;
}

void fn_509030(SaveGame& save, uint16_t from, uint16_t to) {
	const uint32_t ships = ship_count(save);
	for (uint32_t i = 0; i < ships && i < save.ships.size(); ++i) {
		ShipMemory& s = save.ships[i].memory;
		if (s.u16(0x132) == from) {
			s.u16(0x132) = to;
		}
	}
}

void fn_51a3c0(SaveGame& save, int ship) {
	ShipMemory& s = save.ships[size_t(ship)].memory;
	if (s.u16(0x8) >= save.convoy_count()) {
		return;
	}
	const uint32_t ships = ship_count(save);
	for (uint32_t i = save.convoys[s.u16(0x8)].u16(0xA); i < ships;) {
		const ShipMemory& other = save.ships[i].memory;
		const uint32_t next = other.u16(0x6);
		for (int w = 0; w < 24; ++w) {
			if (other.s32(0x54 + 4 * w) > 0 && s.s32(0x54 + 4 * w) == 0) {
				s.s32(0x54 + 4 * w) = 1;
			}
		}
		i = next;
	}
}

void fn_51a450(SaveGame& save, int ship) {
	ShipMemory& s = save.ships[size_t(ship)].memory;
	for (int w = 0; w < 24; ++w) {
		if (s.s32(0x54 + 4 * w) == 1) {
			s.s32(0x54 + 4 * w) = 0;
		}
	}
}

void fn_5171e0(SavedShip& ship) {
	ShipMemory& s = ship.memory;
	if (s.u32(0x24) != 0) {
		ship.set_route(nullptr);
		s.u8(0x36) = 0;
		s.u32(0x48) = 0;
		// The owner the local player: a notice to the interface (0x44F940 on *0x6CBDD8).
	}
	s.u8(0x3C) |= 0xA8;
	s.u16(0x1C) = 0;
	s.u16(0x20) = 0;
	s.u32(0x28) = 0;
	s.u32(0x2C) = 0;
	s.u32(0x30) = 0;
	s.u16(0x34) = 0;
	s.u16(0x2) = 0;
	s.u8(0x38) = s.u8(0x39);
}

void fn_53df00(SaveGame& save, Sailing* sailing, uint8_t* op) {
	GlobalMemory& g = save.globals;
	const uint32_t ships = ship_count(save);
	if (op_u32(op, 4) >= ships) {
		return;
	}
	ShipMemory* s = &save.ships[op_u32(op, 4)].memory;
	if (op_u32(op, 8) == 0) {
		// Stop the route: the ship's, or that of every ship of the convoy's owner.
		if (s->u16(0x8) >= save.convoy_count()) {
			s->u8(0x3D) |= 8;
			s->u8(0x136) &= 0xFC;
			if (s->u16(0x134) > 1) {
				return;
			}
			s->u8(0x38) = s->u8(0x39);
			return;
		}
		GameMemory<0x3C>& c = save.convoys[s->u16(0x8)];
		if (c.u16(0x12) <= 1) {
			c.u8(0x1F) = c.u8(0x1C);
		}
		for (uint32_t i = c.u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
			ShipMemory& m = save.ships[i].memory;
			if (m.u8(0x0) == c.u8(0x0)) {
				m.u8(0x136) &= 0xFC;
				m.u16(0x3C) |= 0x800;
			}
			if (m.u16(0x134) <= 1) {
				m.u8(0x38) = m.u8(0x39);
			}
		}
		return;
	}
	if (s->u16(0x8) < save.convoy_count()) {
		// The convoy's guide (the operation gets it).
		set_op_u32(op, 4, save.convoys[s->u16(0x8)].u16(0x10));
		if (op_u32(op, 4) >= save.ships.size()) {
			return;  // the game doesn't look
		}
		s = &save.ships[op_u32(op, 4)].memory;
	} else if (s->u16(0x42) >= g.u16(kCaptainCount)) {
		return;  // no captain
	}
	const int index = int(op_u32(op, 4));
	if (s->u8(0x15C) != 0) {
		return;
	}
	if (s->u16(0x132) >= stop_count(save) || s->u16(0x42) >= g.u16(kCaptainCount)) {
		s->u8(0x136) &= 0xFC;
		s->u8(0x3D) |= 8;
		return;
	}
	// From its stop, the first with something to do (with the convoy's cargo).
	const uint16_t start = s->u16(0x132);
	fn_51a3c0(save, index);
	bool ready = fn_4d5ab0(save, index);
	while (!ready) {
		s->u16(0x132) = save.trade_routes[s->u16(0x132)].u16(0x0);
		if (s->u16(0x132) == start) {
			return;  // nothing anywhere (the convoy's 1s stay in the guide's cargo)
		}
		ready = fn_4d5ab0(save, index);
	}
	fn_51a450(save, index);
	const uint16_t status = s->u16(0x134);
	if (status == kShipSailing) {
		s->u8(0x3D) |= 8;
		s->u8(0x136) = 1;
		const uint32_t town = save.trade_routes[s->u16(0x132)].u8(0x2);
		if (town >= g.u32(kWorldTownCount)) {
			return;
		}
		if (sailing) {
			sailing->set_destination(save.ships[size_t(index)], int(town), 0, 0);  // 0x5172A0
		}
		return;
	}
	if (status == 8) {
		const uint32_t town = s->u8(0x39) % g.u32(kWorldTownCount);
		TownMemory& t = save.towns[town].memory;
		if (t.u16(0x994) == op_u32(op, 4)) {
			t.u32(0x990) = 0;
			t.u16(0x994) = 0xFFFF;
		}
		s->u16(0x3C) |= 0x820;
		s->u8(0x37) = uint8_t(town);
		s->u8(0x136) = 1;
		const uint8_t to = save.trade_routes[s->u16(0x132)].u8(0x2);
		s->u8(0x38) = to;
		if (to == s->u8(0x39)) {
			s->u16(0x134) = kShipArriving;
			s->u16(0x138) = uint16_t(0x40 - s->u16(0x138));
		} else {
			s->u16(0x134) = kShipLeaving;
		}
		return;
	}
	if (!tick::ship_ai_detail::fn_519ba0(save, index)) {
		return;
	}
	s->u8(0x3D) |= 8;
	s->u8(0x136) = 1;
	if (s->u16(0x134) <= 1) {
		if (s->u16(0x8) < save.convoy_count()) {
			GameMemory<0x3C>& c = save.convoys[s->u16(0x8)];
			c.u16(0x16) = 0;
			for (uint32_t i = c.u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
				save.ships[i].memory.u16(0x138) = 0;
			}
		} else {
			s->u16(0x138) = 0;
		}
		return;
	}
	if (s->u16(0x134) != 6) {
		return;
	}
	// In the shipyard (6): out of its town's list into the docked ones.
	if (s->u8(0x39) >= save.towns.size()) {
		return;  // the game doesn't look
	}
	TownMemory& t = save.towns[s->u8(0x39)].memory;
	const uint16_t convoy = s->u16(0x8);
	const uint16_t convoys = save.convoy_count();
	if (convoy < convoys) {
		uint16_t* link = &t.u16(0x820);
		while (*link != convoy) {
			if (*link >= convoys) {
				return;
			}
			link = &save.convoys[*link].u16(0x8);
		}
		GameMemory<0x3C>& c = save.convoys[convoy];
		*link = c.u16(0x8);
		c.u16(0x8) = g.u16(kConvoyHead);
		g.u16(kConvoyHead) = s->u16(0x8);
		c.u16(0x12) = 0;
		for (uint32_t i = c.u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
			ShipMemory& m = save.ships[i].memory;
			m.u16(0x3C) |= 0x100;
			m.u16(0x134) = kShipDocked;
		}
		// 0x613920 on the port's scene 0x6E6220 (the guide): the interface.
		return;
	}
	const uint32_t me = op_u32(op, 4);
	uint16_t* link = &t.u16(0x81E);
	while (*link != me) {
		if (*link >= ships) {
			return;
		}
		link = &save.ships[*link].memory.u16(0x6);
	}
	*link = s->u16(0x6);
	s->u16(0x6) = 0xFFFF;
	if (me < ships && save.ships[me].memory.u16(0x6) == 0xFFFF) {
		ShipMemory& m = save.ships[me].memory;
		m.u16(0x134) = kShipDocked;
		m.u8(0x3C) |= 0x20;
		m.u16(0x6) = save.docked_head();
		save.docked_head() = uint16_t(me);
	}
	s->u16(0x134) = kShipFromShipyard;
	s->u16(0x138) = 0;
	// 0x613920 on the port's scene 0x6E6220: the interface.
}

void fn_53e480(SaveGame& save, const uint8_t* op) {
	const uint16_t stop = op_u16(op, 4);
	// Game bug: the game checks the stop with `ja`, so the count itself passes and the record past the pool is
	// written. p3vita stops there (the game's window never sends it).
	if (stop > stop_count(save) || !valid_stop(save, stop)) {
		return;
	}
	const uint16_t ware = op_u16(op, 8);
	if (ware >= 0x18) {
		return;
	}
	TradeStop& r = save.trade_routes[stop];
	r.s32(0x7C + 4 * ware) = op_s32(op, 0xC);
	r.s32(0x1C + 4 * ware) = op_s32(op, 0x10);
	// The merchant's last price for the ware (0x5303C0, without looking): to buy [+0x548], to sell [+0x54A].
	const uint16_t merchant = op_u16(op, 6);
	const int32_t price = op_s32(op, 0x10);
	if (merchant < save.merchants.size() && price != 0) {
		MerchantMemory& m = save.merchants[merchant].memory;
		if (price < 0) {
			m.u16(0x548 + 4 * ware) = uint16_t(-price);
		} else {
			m.u16(0x54A + 4 * ware) = uint16_t(price);
		}
	}
	const uint16_t position = op_u16(op, 0xA);
	if (position >= 0x18) {
		return;
	}
	// The ware's place in the order. The game looks at 24 places after the first, so the byte after the order
	// (the low byte of ware 0's price) counts as a 25th.
	int k = 0;
	if (r.u8(0x4) != ware) {
		for (;;) {
			if (k >= 0x18) {
				for (int i = 0; i < 0x18; ++i) {
					r.u8(0x4 + i) = uint8_t(i);  // not there: the order is reset
				}
				return;
			}
			const uint8_t v = r.u8(0x5 + k);
			++k;
			if (v == ware) {
				break;
			}
		}
	}
	if (k > position) {
		for (int i = k - 1; i >= position; --i) {
			r.u8(0x5 + i) = r.u8(0x4 + i);
		}
	} else if (k < position) {
		for (int i = k + 1; i <= position; ++i) {
			r.u8(0x3 + i) = r.u8(0x4 + i);
		}
	}
	r.u8(0x4 + position) = uint8_t(ware);
	// The order made a permutation: a ware twice keeps its last place, the other places get the missing wares.
	uint8_t place[0x18];
	std::memset(place, 0xFF, sizeof(place));
	for (int c = 0; c < 0x18; ++c) {
		const uint8_t w = r.u8(0x4 + c);
		if (w < 0x18) {
			place[w] = uint8_t(c);
		}
	}
	for (int c = 0; c < 0x18; ++c) {
		const uint8_t w = r.u8(0x4 + c);
		if (w < 0x18 && place[w] == c) {
			continue;
		}
		for (int m = 0; m < 0x18; ++m) {
			if (place[m] == 0xFF) {
				r.u8(0x4 + c) = uint8_t(m);
				place[m] = uint8_t(c);
				break;
			}
		}
	}
}

void fn_53e610(SaveGame& save, const uint8_t* op) {
	GlobalMemory& g = save.globals;
	const uint32_t stop = op_u32(op, 4);
	if (stop >= stop_count(save) || !valid_stop(save, stop)) {
		return;
	}
	const uint32_t towns = g.u32(kWorldTownCount);
	const uint32_t town = op_u32(op, 0xC);
	if (op_u32(op, 8) != 0) {
		// A new stop after it, at the town.
		if (town >= towns) {
			return;
		}
		const uint16_t added = fn_4d4c90(save);
		TradeStop& r = save.trade_routes[stop];
		TradeStop& n = save.trade_routes[added];
		n.u16(0x0) = r.u16(0x0);
		r.u16(0x0) = added;
		n.u8(0x2) = uint8_t(town);
		n.u8(0x3) = 0;
		return;
	}
	TradeStop& r = save.trade_routes[stop];
	if (town >= towns) {
		// Removed: its ships go to the next stop (none if it was alone), which gets the first stop's bit.
		uint16_t to = r.u16(0x0);
		if (to == stop) {
			to = 0xFFFF;
		} else if ((r.u8(0x3) & 4) == 4 && valid_stop(save, to)) {
			save.trade_routes[to].u8(0x3) |= 4;
		}
		fn_509030(save, uint16_t(stop), to);
		fn_4d4e80(save, stop);
		return;
	}
	if (town == r.u8(0x2)) {
		return;
	}
	// Another town: the ship on it stops its route; without the owner's office there, no transfers with it.
	r.u8(0x2) = uint8_t(town);
	r.u8(0x3) &= 4;
	const uint32_t ship = op_u32(op, 0x10);
	if (ship < ship_count(save)) {
		SavedShip& saved = save.ships[ship];
		ShipMemory& s = saved.memory;
		if (s.u16(0x132) == stop) {
			s.u8(0x3C) |= 0x20;
			s.u8(0x136) &= 0xFC;
			if (s.u16(0x134) >= 0xC) {
				fn_5171e0(saved);
			}
		}
		if (s.u8(0x0) < g.u16(kWorldMerchantCount) && tick::facilities_detail::fn_5308a0(save, s.u8(0x0), r.u8(0x2)) >= 0) {
			return;
		}
	}
	for (int w = 0; w < 24; ++w) {
		if (r.s32(0x1C + 4 * w) == 0) {
			r.s32(0x7C + 4 * w) = 0;
		}
	}
}

void fn_53e7c0(SaveGame& save, const uint8_t* op) {
	const uint32_t ships = ship_count(save);
	const uint32_t ship = op_u32(op, 4);
	if (ship >= ships) {
		return;
	}
	ShipMemory* s = &save.ships[ship].memory;
	if (s->u16(0x8) < save.convoy_count()) {
		// Game bug: the guide is checked with `ja` against the ships' number (the number itself passes). p3vita
		// stops at the ships it has.
		const uint16_t guide = save.convoys[s->u16(0x8)].u16(0x10);
		if (guide > ships || guide >= save.ships.size()) {
			return;
		}
		s = &save.ships[guide].memory;
	}
	uint16_t& head = s->u16(0x132);
	if (head >= stop_count(save)) {
		return;
	}
	fn_4d4f60(save, head, op_u32(op, 8), op_u32(op, 0xC), op_u32(op, 0x10) == 1);
	if (head >= stop_count(save)) {
		s->u8(0x136) &= 0xFC;
		s->u8(0x3D) |= 8;
	}
	// 0x48FD90 on *0x6CE6D0: the window.
}

void fn_53e890(SaveGame& save, uint8_t* op) {
	const uint32_t ships = ship_count(save);
	uint32_t ship = op_u32(op, 4);
	if (ship >= ships || op_u32(op, 8) >= save.globals.u32(kWorldTownCount) || op_u32(op, 0xC) < 1) {
		return;
	}
	ShipMemory* s = &save.ships[ship].memory;
	// Game bug: the convoy is checked against the ships' number, not the convoys'. Harmless: a ship's convoy is a
	// real one or 0xFFFF.
	if (s->u16(0x8) < ships && s->u16(0x8) < save.convoys.size()) {
		const uint8_t owner = s->u8(0x0);
		ship = save.convoys[s->u16(0x8)].u16(0x10);
		set_op_u32(op, 4, ship);
		if (ship >= ships) {
			return;
		}
		s = &save.ships[ship].memory;
		if (owner != s->u8(0x0)) {
			return;
		}
	}
	uint16_t& head = s->u16(0x132);
	if (head < stop_count(save)) {
		fn_4d4dd0(save, head, true);
		head = 0xFFFF;
	}
	// `count` stops at the town, linked from the ship's [+0x132] (the first with the bit 0x4) and back to it.
	int link = -1;  // the stop whose [+0x0] gets the next one; -1: the ship's [+0x132]
	while (op_u32(op, 0xC) > 0) {
		set_op_u32(op, 0xC, op_u32(op, 0xC) - 1);
		const uint16_t before = save.globals.u16(kStopCount);
		const uint16_t added = fn_4d4c90(save);
		if (link < 0) {
			head = added;
		} else if (save.globals.u16(kStopCount) == before || fix_game_bugs()) {
			save.trade_routes[size_t(link)].u16(0x0) = added;
		}
		// Game bug: the game keeps a pointer to the previous stop's [+0x0] across 0x4D4C90, so when the pool grows
		// it writes into the freed array and the route ends there (the rest stay apart, linked to the head, until
		// the next load frees them). Fixed behind fix_game_bugs.
		TradeStop& r = save.trade_routes[added];
		if (added == head) {
			r.u8(0x3) = 4;
		}
		r.u8(0x2) = op[8];
		r.u16(0x0) = head;
		link = added;
	}
	set_op_u32(op, 0xC, op_u32(op, 0xC) - 1);
}

void fn_53e9c0(SaveGame& save, const uint8_t* op) {
	const uint32_t stop = op_u32(op, 4);
	if (stop >= stop_count(save) || !valid_stop(save, stop)) {
		return;
	}
	uint32_t action = op_u32(op, 8);
	if ((action & 1) == 1) {
		const uint32_t town = save.trade_routes[stop].u8(0x2);
		if (save.globals.u32(kWorldTownCount) > town) {
			if ((save.towns[town].memory.u32(0x76C) & 0x40) == 0) {
				action = (action - 1) | 8;
			}
		} else {
			action -= 1;
		}
	}
	save.trade_routes[stop].u8(0x3) = uint8_t(action);
}

int32_t fn_500ec0(const SaveGame& save, const OfficeMemory& o, uint32_t ware) {
	if (o.u16(0x2F2) < save.globals.u16(kCaptainCount) && (o.u32(0x3B4) & (1u << (ware & 31)))) {
		const int32_t keep = o.s32(0x354 + 4 * ware);
		if (keep > 0) {
			const int32_t left = int32_t(uint32_t(o.s32(0x4 + 4 * ware)) - uint32_t(keep));
			return left <= 0 ? 0 : left;
		}
	}
	return o.s32(0x4 + 4 * ware);
}

void fn_4d5200(SaveGame& save, int ship, int office) {
	SavedShip& saved = save.ships[size_t(ship)];
	ShipMemory& s = saved.memory;
	const GlobalMemory& g = save.globals;
	if (!valid_stop(save, s.u16(0x132))) {
		return;  // the game reads past its array
	}
	TradeStop& stop = save.trade_routes[s.u16(0x132)];
	Letter letter;  // 0x51A850
	const uint8_t owner = s.u8(0x0);
	if (owner >= g.u16(kWorldMerchantCount)) {
		return;
	}
	if (s.u8(0x39) != stop.u8(0x2)) {
		send_ticker(save, owner, 0, uint16_t(ship));  // «The X's captain interrupts his trade journey.»
		return;
	}
	const uint8_t town = stop.u8(0x2);
	if (town >= save.towns.size()) {
		return;  // the game doesn't look
	}
	MerchantMemory& m = save.merchants[owner].memory;
	TownMemory& t = save.towns[town].memory;
	// The merchant's bit of the town in [+0x608]: trade banned there (`shl` by the town, masked to 5 bits).
	const bool may_sell = (m.u32(0x608) & (1u << (town & 31))) == 0;
	// Buying: in a town with [+0x76C] & 0x100, as selling; elsewhere only in one whose [+0x6F1] is the owner.
	const bool may_buy = (t.u32(0x76C) & 0x100) ? may_sell : (may_sell && t.u8(0x6F1) == owner);
	if (s.u16(0x42) >= g.u16(kCaptainCount)) {
		s.u8(0x136) = 0;
		return;
	}
	const uint32_t skill = save.captains[s.u16(0x42)].u8(0xA);
	// The captain's discount on purchases: (50 − skill / 43) · 2 per cent, in a byte.
	const int32_t factor = uint8_t(uint8_t(0x32 - uint8_t(int32_t(skill) / 43)) * 2);
	// Selling, and the ship's cargo into the office, in the stop's order.
	for (int k = 0; k < 0x18; ++k) {
		const uint32_t w = stop.u8(0x4 + k);
		if (w >= 0x18) {
			continue;
		}
		const int32_t price = stop.s32(0x1C + 4 * w);
		const int32_t unit = kInBarrels[w] ? 200 : 2000;
		if (price > 0) {
			if (!may_sell) {
				continue;
			}
			// What the town takes before its price falls to the stop's, the stop's amount and the cargo.
			int32_t n = int32_t(uint32_t(tick::offices_detail::fn_52ea80(save, w, town, price)) -
				uint32_t(t.s32(0x4 + 4 * w)));
			if (n <= 0) {
				continue;
			}
			bool another = true;  // the town's limit, not the stop's or the hold's, stopped it
			if (n > stop.s32(0x7C + 4 * w)) {
				another = false;
				n = stop.s32(0x7C + 4 * w);
			}
			if (n > s.s32(0x54 + 4 * w)) {
				n = s.s32(0x54 + 4 * w);
				another = false;
			}
			const int32_t amount = n / unit * unit;
			if (amount > 0) {
				const int32_t money = tick::game_sell_price(save, int(w), town, amount);
				m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) + uint32_t(money));
				s.s32(0x124) = int32_t(uint32_t(s.s32(0x124)) + uint32_t(money));
				tick::offices_detail::fn_4f8690(save, owner, town, w, money, amount, true);
				t.s32(0x4 + 4 * w) += amount;
				s.s32(0x54 + 4 * w) -= amount;
				tick::ship_ai_detail::fn_517770(save, ship, 4);
			}
			if (another && s.s32(0x54 + 4 * w) >= unit) {
				// One unit more while it still fetches more than the stop's price.
				const int32_t money = tick::game_sell_price(save, int(w), town, unit);
				if (money > stop.s32(0x1C + 4 * w)) {
					m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) + uint32_t(money));
					s.s32(0x124) = int32_t(uint32_t(s.s32(0x124)) + uint32_t(money));
					tick::offices_detail::fn_4f8690(save, owner, town, w, money, unit, true);
					t.s32(0x4 + 4 * w) += unit;
					s.s32(0x54 + 4 * w) -= unit;
				}
			}
			continue;
		}
		if (office < 0 || price != 0) {
			continue;
		}
		int32_t amount = stop.s32(0x7C + 4 * w);
		if (amount >= 0) {
			continue;
		}
		const int32_t cargo = s.s32(0x54 + 4 * w);
		amount = -amount;
		if (amount > cargo) {
			amount = cargo;
		}
		amount = amount / unit * unit;
		s.s32(0x54 + 4 * w) = cargo - amount;
		tick::ship_ai_detail::fn_517770(save, ship, 4);
		fn_4ff6f0(save.offices[size_t(office)].memory, w, amount, s.f32(0xB4 + 4 * w));
	}
	if ((stop.u8(0x3) & 4) && s.u16(0x8) >= save.convoy_count()) {
		// At the route's first stop a ship alone reports its trading since the last time: the letter 0x07.
		const CaptainMemory& captain = save.captains[s.u16(0x42)];
		letter.entry.u8(0x4) = 7;
		letter.entry.u8(0x5) = s.u8(0x39);
		letter.entry.u16(0x8) = s.u16(0x15E);
		letter.entry.u16(0xA) = uint16_t((captain.u8(0x2) << 8) + captain.u8(0x3));
		letter.entry.s32(0xC) = s.s32(0x124);
		s.s32(0x124) = 0;
		send_letter(save, owner, letter);
	}
	int32_t room = recalculate_cargo(saved);
	if (room < 0) {
		room = 0;
	}
	// Buying, and the office's goods into the ship.
	for (int k = 0; k < 0x18; ++k) {
		const uint32_t w = stop.u8(0x4 + k);
		if (w >= 0x18) {
			continue;
		}
		const int32_t price = stop.s32(0x1C + 4 * w);
		const int32_t unit = kInBarrels[w] ? 200 : 2000;
		if (price >= 0) {
			if (office < 0 || price != 0) {
				continue;
			}
			int32_t amount = stop.s32(0x7C + 4 * w);
			if (amount <= 0) {
				continue;
			}
			OfficeMemory& o = save.offices[size_t(office)].memory;
			const int32_t available = fn_500ec0(save, o, w);
			if (amount > available) {
				amount = available;
			}
			if (amount > room) {
				amount = room;
			}
			amount = amount / unit * unit;
			o.s32(0x4 + 4 * w) -= amount;
			fn_5180c0(save, ship, w, amount, o.f32(0x3B8 + 4 * w));
			room -= amount;
			continue;
		}
		if (!may_buy) {
			continue;
		}
		// What the town has above the stock where its price passes the stop's, the stop's amount and the room.
		int32_t amount = int32_t(uint32_t(t.s32(0x4 + 4 * w)) -
			uint32_t(tick::offices_detail::fn_52ec50(save, w, town, -price, skill)));
		if (amount > stop.s32(0x7C + 4 * w)) {
			amount = stop.s32(0x7C + 4 * w);
		}
		if (amount > room) {
			amount = room;
		}
		if (amount <= 0) {
			continue;
		}
		amount = amount / unit * unit;
		// The price with the captain's discount (big sums divided first).
		auto discounted = [factor](int32_t cost) {
			return cost >= 0x1000000 ? cost / 100 * factor : int32_t(uint32_t(cost) * uint32_t(factor)) / 100;
		};
		int32_t cost = discounted(tick::game_buy_price(save, int(w), town, amount));
		if (m.s32(0x0) < cost) {
			if (m.s32(0x0) <= 0) {
				continue;
			}
			amount = int32_t(uint32_t(m.s32(0x0)) * uint32_t(amount)) / cost;  // a 32-bit product, as the game
			amount = amount / unit * unit;
			cost = discounted(tick::game_buy_price(save, int(w), town, amount));
		}
		if (amount <= 0) {
			continue;
		}
		t.s32(0x4 + 4 * w) -= amount;
		if (t.s32(0x4 + 4 * w) < 0) {
			t.s32(0x4 + 4 * w) = 0;
		}
		tick::events_rare_detail::fn_517fe0(save, ship, w, amount, cost);
		room -= amount;
		m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) - uint32_t(cost));
		s.s32(0x124) = int32_t(uint32_t(s.s32(0x124)) - uint32_t(cost));
		tick::offices_detail::fn_4f8840(save, owner, town, w, cost, amount, true);
	}
	if (stop.u8(0x3) & 2) {
		s.u8(0x136) &= 0xFC;  // the route ends here
	}
	if (s.u8(0x3C) & 4) {
		recalculate_cargo(saved);
	}
}

uint32_t next_route_town(SaveGame& save, int ship) {
	ShipMemory& s = save.ships[size_t(ship)].memory;
	const GlobalMemory& g = save.globals;
	uint32_t town = 0xFFFFF;
	// From the stop after its own, round to it, the first with something to do: its town.
	uint32_t end = s.u16(0x132);
	if (end < stop_count(save)) {
		do {
			if (!valid_stop(save, s.u16(0x132))) {
				break;  // the game reads past its array
			}
			s.u16(0x132) = save.trade_routes[s.u16(0x132)].u16(0x0);
			if (fn_4d5ab0(save, ship)) {
				end = s.u16(0x132);
				town = save.trade_routes[s.u16(0x132)].u8(0x2);
			}
		} while (end != s.u16(0x132));
	}
	const uint32_t towns = g.u32(kWorldTownCount);
	if (town >= towns) {
		// Else the home town, unless it is closed or takes no big ships.
		town = s.u8(0x0) < save.merchants.size() ? save.merchants[s.u8(0x0)].memory.u8(0x19) : 0xFF;
		const uint32_t flags = town < save.towns.size() ? save.towns[town].memory.u32(0x2C8) : 0;
		if (flags & 0x4000600) {
			town = 0xFF;
		} else if ((flags & 1) &&
			(s.u8(0xE) >= 2 || (s.u16(0x8) < save.convoy_count() && save.convoys[s.u16(0x8)].u8(0x1) >= 2))) {
			town = 0xFF;
		}
		if (town >= towns) {
			// Else the nearest town with [+0x2C8] & 0x40 and neither 1 nor 0x4000000, from where the ship is.
			int32_t best = 0x0FFFFFFF;
			for (uint32_t u = 0; u < towns && u < save.towns.size(); ++u) {
				const uint32_t f = save.towns[u].memory.u32(0x2C8);
				if ((f & 0x4000041) != 0x40) {
					continue;
				}
				// Game bug: a closed port (0x200, 0x400) may be chosen, so a ship before its closed port (with nothing
				// to do on its route and its home closed) is sent to that same town, arrives again in the same tick
				// and the game hangs. Fixed: skipped, as the home town's check does.
				if (fix_game_bugs() && (f & 0x4000600)) {
					continue;
				}
				const uint32_t row = 0x6DDB90 + 0x34 * g.u8(0x6DE4B8 + u);  // StadtDaten.ini's table by number
				const int32_t dx = int32_t(int16_t(s.u16(0x1E))) - g.s32(row + 0x20);
				const int32_t dy = int32_t(int16_t(s.u16(0x22))) - g.s32(row + 0x24);
				const int32_t d = int32_t(uint32_t(dy) * uint32_t(dy) + uint32_t(dx) * uint32_t(dx));
				if (town >= towns || best > d) {
					town = u;
					best = d;
				}
			}
		}
	}
	return town;
}

void fn_4d5900(SaveGame& save, Sailing* sailing, int ship) {
	SavedShip& saved = save.ships[size_t(ship)];
	const uint32_t town = next_route_town(save, ship);
	if (sailing) {
		// 0x5172A0 (town, x, y): with no town, its own place.
		const ShipMemory& s = saved.memory;
		sailing->set_destination(saved, town < save.globals.u32(kWorldTownCount) ? int(town) : -1,
			int16_t(s.u16(0x1E)), int16_t(s.u16(0x22)));
	}
}

void fn_518860(SaveGame& save, Sailing* sailing, int ship) {
	SavedShip& saved = save.ships[size_t(ship)];
	ShipMemory& s = saved.memory;
	const GlobalMemory& g = save.globals;
	const uint8_t owner = s.u8(0x0);
	tick::ship_ai_detail::fn_535400(save, s.u8(0x39), owner);
	// Stops at a town out of the world are dropped (their ships moved on).
	bool found = false;
	while (s.u16(0x132) < stop_count(save) && valid_stop(save, s.u16(0x132))) {
		const uint16_t here = s.u16(0x132);
		if (save.trade_routes[here].u8(0x2) < g.u32(kWorldTownCount)) {
			found = true;
			break;
		}
		const uint16_t next = save.trade_routes[here].u16(0x0);
		s.u16(0x132) = here == next ? uint16_t(0xFFFF) : next;
		fn_509030(save, here, s.u16(0x132));
		fn_4d4e80(save, here);
	}
	if (!found) {
		s.u8(0x136) &= 0xFC;
		s.u8(0x3D) |= 8;
		send_ticker(save, owner, 0, uint16_t(ship));
		return;
	}
	TradeStop& stop = save.trade_routes[s.u16(0x132)];
	const uint8_t town = s.u8(0x39);
	if (stop.u8(0x2) != town) {
		// The stop is elsewhere: it leaves for it.
		s.u8(0x38) = stop.u8(0x2);
		s.u16(0x134) = kShipLeaving;
		s.u8(0x37) = town;
		return;
	}
	if (stop.u8(0x3) & 1) {
		// Repairs at this stop: only where the town has [+0x76C] & 0x40 (else the bit goes), below nine tenths.
		if (town >= save.towns.size() || (save.towns[town].memory.u32(0x76C) & 0x40) == 0) {
			stop.u8(0x3) = uint8_t(stop.u8(0x3) - 1);
		} else if (int32_t(uint32_t(s.s32(0x14)) * 9) > int32_t(uint32_t(s.s32(0x18)) * 10) &&
			tick::fn_52acd0(save, town, ship)) {
			return;
		}
	}
	s.u8(0x37) = town;
	fn_4d5200(save, ship, tick::facilities_detail::fn_5308a0(save, owner, town));
	s.u16(0x134) = kShipWaiting;
	const int8_t rest = int8_t(s.u8(0x3F));
	if (rest < 2) {
		s.u16(0x138) = uint16_t(s.u16(0x138) + uint16_t(0x500 - s.u16(0x3E)));
	} else {
		s.u16(0x138) = 0x40;
	}
	if (rest < 1) {
		s.u8(0x3F) = 1;
	}
	if (!tick::ship_ai_detail::fn_519ba0(save, ship)) {
		s.u8(0x136) &= 0xFC;
		s.u8(0x3D) |= 8;
		if (s.s32(0x18) < s.s32(0x14) / 10) {
			send_ticker(save, owner, 0x23, uint16_t(ship));  // too damaged
			return;
		}
		// Too few sailors (0x673660 by the type), or else the line 0.
		constexpr uint8_t kMinSailors[4] = {5, 8, 10, 12};
		send_ticker(save, owner, s.u16(0x40) < kMinSailors[s.u8(0xE) & 3] ? 0x28 : 0, uint16_t(ship));
		return;
	}
	// The next stop with something to do (not this one again).
	const uint16_t start = s.u16(0x132);
	for (;;) {
		s.u16(0x132) = save.trade_routes[s.u16(0x132)].u16(0x0);
		if (s.u16(0x132) == start || !valid_stop(save, s.u16(0x132))) {
			s.u8(0x3D) |= 8;
			s.u8(0x136) &= 0xFC;
			send_ticker(save, owner, 0x24, uint16_t(ship));  // nothing to do anywhere
			return;
		}
		if (fn_4d5ab0(save, ship)) {
			break;
		}
	}
	const uint8_t to = save.trade_routes[s.u16(0x132)].u8(0x2);
	s.u8(0x38) = to;
	if (sailing) {
		sailing->set_destination(saved, to, 0, 0);  // 0x5172A0
	}
}

void fn_503230(SaveGame& save, Sailing* sailing, int convoy) {
	GameMemory<0x3C>& c = save.convoys[size_t(convoy)];
	const GlobalMemory& g = save.globals;
	const uint32_t ships = ship_count(save);
	Letter letter;  // 0x51A850
	const uint16_t guide = c.u16(0x10);
	ShipMemory* lead = &save.ships[guide].memory;
	uint16_t wait = 0x40;  // the longest rest a member still needs, at least 0x40
	int32_t money = 0;  // the members' trading since the last report
	if (!valid_stop(save, lead->u16(0x132))) {
		return;  // the game reads past its array
	}
	const uint16_t route_stop = lead->u16(0x132);
	bool first = (save.trade_routes[route_stop].u8(0x3) & 4) == 4;
	// Each member fit to look at the stop (one of another owner always is): the stop as his, 0x4D5AB0.
	auto serves = [&](uint32_t i) {
		ShipMemory& m = save.ships[i].memory;
		if (i != guide) {
			fn_4d4dd0(save, m.u16(0x132), false);
			m.u16(0x132) = 0xFFFF;
		}
		m.u16(0x132) = lead->u16(0x132);
		const bool ok = fn_4d5ab0(save, int(i));
		if (i != guide) {
			m.u16(0x132) = 0xFFFF;
		}
		return ok;
	};
	if (c.u16(0x1C) != save.trade_routes[route_stop].u8(0x2)) {
		// Not at the stop's town: it sails if a member has something to do there.
		first = false;
		if (!tick::ship_ai_detail::fn_519ba0(save, guide)) {
			lead->u8(0x136) &= 0xFC;
			lead->u8(0x3D) |= 8;
			send_ticker(save, lead->u8(0x0), 0x22, guide);  // the route interrupted
			return;
		}
		for (uint32_t i = c.u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
			if (save.ships[i].memory.u8(0x0) != c.u8(0x0) || serves(i)) {
				lead->u16(0x134) = kShipWaiting;
				lead->u16(0x138) = 1;
				const uint8_t to = save.trade_routes[lead->u16(0x132)].u8(0x2);
				lead->u8(0x38) = to;
				lead->u8(0x13B) = to;
				return;
			}
		}
	} else {
		TradeStop& stop = save.trade_routes[route_stop];
		if (stop.u8(0x3) & 1) {
			// Repairs: where the town has [+0x76C] & 0x40 (else the bit goes), when a member is below nine tenths.
			if (c.u16(0x1C) >= save.towns.size() || (save.towns[c.u16(0x1C)].memory.u32(0x76C) & 0x40) == 0) {
				stop.u8(0x3) = uint8_t(stop.u8(0x3) - 1);
			} else {
				for (uint32_t i = c.u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
					const ShipMemory& m = save.ships[i].memory;
					if (int32_t(uint32_t(m.s32(0x14)) * 9) > int32_t(uint32_t(m.s32(0x18)) * 10)) {
						c.u16(0x12) = 4;
					}
				}
			}
		}
		if (c.u16(0x12) == 4) {
			if (tick::fn_52acd0(save, c.u16(0x1C), guide)) {
				lead->u8(0x136) = 2;
				return;
			}
			c.u16(0x12) = 0;
		}
		// The members trade one after the other on a copy of the stop (alone, no action bits), which counts what
		// each one did, so the next does the rest.
		const uint16_t copy = fn_4d4c90(save);
		if (!valid_stop(save, copy)) {
			return;
		}
		save.trade_routes[copy] = save.trade_routes[route_stop];
		TradeStop& temp = save.trade_routes[copy];
		temp.u8(0x3) = 0;
		temp.u16(0x0) = copy;
		const int office = tick::facilities_detail::fn_5308a0(save, c.u8(0x0), temp.u8(0x2));
		for (uint32_t i = c.u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
			ShipMemory& m = save.ships[i].memory;
			if (m.u8(0x0) != c.u8(0x0)) {
				continue;
			}
			int32_t before[24];
			std::memcpy(before, &m.bytes[0x54], sizeof(before));
			const uint16_t own = m.u16(0x132);
			m.u16(0x132) = copy;
			if (m.u16(0x42) >= g.u16(kCaptainCount)) {
				m.u16(0x42) = lead->u16(0x42);  // the guide's captain for a while
			}
			fn_4d5200(save, int(i), office);
			if (i == guide) {
				m.u16(0x132) = route_stop;
			} else {
				m.u16(0x132) = own;
				if (m.u16(0x42) == lead->u16(0x42)) {
					m.u16(0x42) = 0xFFFF;
				}
			}
			TradeStop& left = save.trade_routes[copy];
			for (int w = 0; w < 24; ++w) {
				const int32_t now = m.s32(0x54 + 4 * w);
				if (before[w] == now) {
					continue;
				}
				if (left.s32(0x1C + 4 * w) > 0) {
					left.s32(0x7C + 4 * w) += now - before[w];
				} else {
					left.s32(0x7C + 4 * w) += before[w] - now;
				}
			}
			if (first) {
				money = int32_t(uint32_t(money) + uint32_t(m.s32(0x124)));
				m.s32(0x124) = 0;
			}
			if (int8_t(m.u8(0x3F)) < 2) {
				const uint16_t need = uint16_t(0x500 - m.u16(0x3E));
				if (wait < need) {
					wait = need;
				}
			}
		}
		fn_4d4dd0(save, copy, false);
		lead = &save.ships[guide].memory;
		if (first) {
			// The convoy's report: the letter 0x07 with the convoy's name (the bit 0x8000) and all the money.
			const CaptainMemory& captain = save.captains[lead->u16(0x42)];
			letter.entry.u8(0x4) = 7;
			letter.entry.u8(0x5) = lead->u8(0x39);
			letter.entry.u16(0x8) = uint16_t(lead->u16(0x15E) | 0x8000);
			letter.entry.u16(0xA) = uint16_t((captain.u8(0x2) << 8) + captain.u8(0x3));
			letter.entry.s32(0xC) = money;
			send_letter(save, c.u8(0x0), letter);
		}
	}
	// The next stop that serves a member.
	const uint16_t start = lead->u16(0x132);
	if (!tick::ship_ai_detail::fn_519ba0(save, guide)) {
		lead->u8(0x136) &= 0xFC;
		lead->u8(0x3D) |= 8;
		send_ticker(save, lead->u8(0x0), 0x22, guide);
		return;
	}
	for (;;) {
		lead->u16(0x132) = valid_stop(save, lead->u16(0x132)) ? save.trade_routes[lead->u16(0x132)].u16(0x0) : start;
		if (lead->u16(0x132) == start) {
			lead->u8(0x136) &= 0xFC;
			lead->u8(0x3D) |= 8;
			send_ticker(save, lead->u8(0x0), 0x25, guide);  // nothing to do on the route
			return;
		}
		for (uint32_t i = c.u16(0xA); i < ships; i = save.ships[i].memory.u16(0x6)) {
			if (save.ships[i].memory.u8(0x0) != c.u8(0x0) || serves(i)) {
				c.u16(0x12) = 1;
				lead->u16(0x134) = kShipWaiting;
				lead->u16(0x138) = wait;
				const uint8_t to = save.trade_routes[lead->u16(0x132)].u8(0x2);
				lead->u8(0x38) = to;
				lead->u8(0x13B) = to;
				if (sailing) {
					sailing->set_destination(save.ships[guide], to, 0, 0);  // 0x5172A0
				}
				return;
			}
		}
	}
}

// --- The route files ---

std::optional<std::vector<uint8_t>> fn_4d5c50(const SaveGame& save, uint16_t stop) {
	const uint32_t count = stop_count(save);
	// How many stops the route has from `stop` (back to it, or to a next out of the pool); as many as the pool's
	// count: nothing.
	if (stop >= count) {
		return std::nullopt;  // (the page never asks: the game would read past the pool)
	}
	uint32_t n = 0;
	{
		uint32_t at = stop;
		do {
			if (!valid_stop(save, at)) {
				break;  // (the game reads past the records)
			}
			at = save.trade_routes[at].u16(0x0);
			++n;
			if (n >= count) {
				return std::nullopt;
			}
		} while (at != stop && at < count);
	}
	// From the first (the bit 0x4; back at `stop` without one, `stop`).
	uint32_t first = stop;
	for (uint32_t at = stop; valid_stop(save, at);) {
		if (save.trade_routes[at].u8(0x3) & 4) {
			first = at;
			break;
		}
		at = save.trade_routes[at].u16(0x0);
		if (at == stop) {
			break;
		}
	}
	std::vector<uint8_t> records(size_t(n) * 0xDC);
	uint32_t at = first;
	for (uint32_t k = 0; k < n && valid_stop(save, at); ++k) {
		std::memcpy(records.data() + 0xDC * k, save.trade_routes[at].bytes.data(), 0xDC);
		records[0xDC * k] = 0;
		records[0xDC * k + 1] = 0;
		at = save.trade_routes[at].u16(0x0);
	}
	return lz_compress(records.data(), records.size());
}

std::optional<std::vector<TradeStop>> fn_4d5ee0(const std::vector<uint8_t>& file) {
	if (file.size() < 4) {
		return std::nullopt;
	}
	uint32_t header;
	std::memcpy(&header, file.data(), 4);
	const uint32_t size = header & 0x3FFFFFFF;
	if (size % 0xDC != 0 || size == 0 || size > 0x1000000) {
		return std::nullopt;  // (a size the game's allocation refuses: none)
	}
	const uint32_t n = size / 0xDC;
	std::vector<uint8_t> records(size);
	size_t produced = 0;
	if (!lz_decompress(file.data(), file.size(), records.data(), records.size(), produced) || produced != size) {
		return std::nullopt;
	}
	std::vector<TradeStop> out(n);
	for (uint32_t k = 0; k < n; ++k) {
		std::memcpy(out[k].bytes.data(), records.data() + 0xDC * k, 0xDC);
		out[k].u16(0x0) = uint16_t(k + 1 < n ? k + 1 : 0);
	}
	return out;
}

void load_route_file(SaveGame& save, int ship, const std::optional<std::vector<TradeStop>>& stops) {
	save.globals.u32(kLoadedRouteShip) = uint32_t(ship);
	save.loaded_route = stops ? *stops : std::vector<TradeStop>();
	save.globals.u32(kLoadedRoute) = save.loaded_route.empty() ? 0 : 1;  // (the game's buffer; here only non-zero)
}

void fn_5492d0(SaveGame& save, Sailing* sailing) {
	GlobalMemory& g = save.globals;
	std::vector<TradeStop>& loaded = save.loaded_route;
	// The operation it fills and queues (0x535740: −1 and zeros); what one doesn't write stays from the one before.
	uint32_t op[5] = {0xFFFFFFFF, 0, 0, 0, 0};
	auto release = [&](bool ship_too) {
		loaded.clear();
		g.u32(kLoadedRoute) = 0;
		if (ship_too) {
			g.u32(kLoadedRouteShip) = 0xFFFF;
		}
	};
	auto queue = [&]() { tick::enqueue_operation(save, reinterpret_cast<const uint8_t*>(op), sizeof(op)); };
	// The queue's count: in a network game 0x54AA70 adds and the network's thread takes (p3vita: as the oracle, where
	// nothing adds to it).
	auto full = [&]() { return g.u16(kQueueCount) == 0x34; };
	const bool network = (g.u32(kQueueObject) & 0xC) != 0;
	const uint32_t ships = ship_count(save);
	const uint32_t ship = g.u32(kLoadedRouteShip);
	if (ship >= ships || ship >= save.ships.size()) {
		release(false);
		return;
	}
	// The ship, or its convoy's guide; big: its type (or a convoy's biggest) ≥ 2. The game compares the convoy with the
	// ships' count, not the convoys'.
	uint32_t s_index = ship;
	bool big = false;
	const uint16_t convoy = save.ships[ship].memory.u16(0x8);
	if (convoy < ships && convoy < save.convoys.size()) {
		const GameMemory<0x3C>& c = save.convoys[convoy];
		s_index = c.u16(0x10);
		for (uint32_t i = c.u16(0xA); i < ships && i < save.ships.size(); i = save.ships[i].memory.u16(0x6)) {
			if (save.ships[i].memory.u8(0xE) >= 2) {
				big = true;
				break;
			}
		}
	} else {
		big = save.ships[ship].memory.u8(0xE) >= 2;
	}
	if (s_index >= save.ships.size()) {
		release(false);
		return;  // (the game reads past the ships)
	}
	ShipMemory& s = save.ships[s_index].memory;
	const uint32_t towns = g.u32(kWorldTownCount);
	// The stops, up to the first whose +0x0 is 0.
	uint32_t valid = 0;
	uint32_t n = 0;
	for (size_t k = 0; k < loaded.size(); ++k) {
		TradeStop& r = loaded[k];
		const uint8_t town = r.u8(0x2);
		if (town >= towns || town >= save.towns.size() || (big && (save.towns[town].memory.u32(0x2C8) & 1))) {
			r.u8(0x2) = 0xFF;
		} else {
			if (tick::facilities_detail::fn_5308a0(save, s.u8(0x0), town) < 0) {
				for (int w = 0; w < 24; ++w) {
					if (r.s32(0x1C + 4 * w) == 0 && r.s32(0x7C + 4 * w) != 0) {
						r.s32(0x7C + 4 * w) = 0;  // no office there: no transfers
					}
				}
			}
			++valid;
		}
		++n;
		if (r.u16(0x0) == 0) {
			break;
		}
	}
	if (valid == 0) {
		release(false);
		return;
	}
	if (!network) {
		// Alone: the route off, a new one of `valid` stops at the first stop's town still in the world (0x6C, run at
		// once), and the stops copied into it.
		s.u8(0x136) = 0;
		op[0] = 0x6C;
		op[1] = ship;
		uint32_t town = loaded[0].u8(0x2);
		for (uint32_t k = 1; k < n && town >= towns; ++k) {
			town = loaded[k].u8(0x2);
		}
		op[2] = town;
		op[3] = valid;
		run_operation(save, sailing, reinterpret_cast<const uint8_t*>(op));
		uint32_t at = s.u16(0x132);
		if (at < stop_count(save)) {
			for (uint32_t k = 0; k < n; ++k) {
				const TradeStop& r = loaded[k];
				if (r.u8(0x2) >= towns) {
					continue;
				}
				if (!valid_stop(save, at)) {
					break;  // (the game writes past the records)
				}
				TradeStop& t = save.trade_routes[at];
				t.u8(0x2) = r.u8(0x2);
				t.u8(0x3) = r.u8(0x3);
				std::memcpy(t.bytes.data() + 0x4, r.bytes.data() + 0x4, 0x18);
				std::memcpy(t.bytes.data() + 0x1C, r.bytes.data() + 0x1C, 0x60);
				std::memcpy(t.bytes.data() + 0x7C, r.bytes.data() + 0x7C, 0x60);
				at = t.u16(0x0);
			}
		}
		release(true);
		return;
	}
	// A network game: queued.
	if (s.u8(0x136) == 1) {
		op[0] = 0x68;
		op[1] = ship;
		op[2] = 0;
		queue();
	}
	// The route's length from the ship's stop (0: out of the pool, or longer than the stops loaded).
	const uint32_t count = stop_count(save);
	const uint16_t own = s.u16(0x132);
	uint32_t length = 0;
	if (own < count) {
		length = 1;
		uint32_t at = valid_stop(save, own) ? save.trade_routes[own].u16(0x0) : count;
		while (at != own) {
			++length;
			at = valid_stop(save, at) ? save.trade_routes[at].u16(0x0) : count;
			if (at >= count || length > valid) {
				length = 0;
				break;
			}
		}
	}
	if (valid != length) {
		op[0] = 0x6C;
		op[1] = ship;
		op[2] = loaded[0].u8(0x2);
		op[3] = valid;
		queue();
		return;  // the stops kept: the next call goes on
	}
	uint32_t at = own;
	for (uint32_t k = 0; k < n; ++k) {
		const TradeStop& r = loaded[k];
		const uint8_t town = r.u8(0x2);
		if (town >= towns) {
			continue;  // (its stop not passed)
		}
		if (!valid_stop(save, at)) {
			break;  // (the game reads past the records)
		}
		const TradeStop& t = save.trade_routes[at];
		if (t.u8(0x2) != town) {
			if (full()) {
				return;
			}
			op[0] = 0x6A;
			op[1] = at;
			op[2] = 0;
			op[3] = town;
			op[4] = ship;
			queue();
		}
		if (t.u8(0x3) != r.u8(0x3)) {
			if (full()) {
				return;
			}
			op[0] = 0x6D;
			op[1] = at;
			op[2] = r.u8(0x3);
			queue();
		}
		// A ware's amount and price (a ware past 23, in no file the game writes: the game reads past the stop; 0).
		auto amount = [](const TradeStop& x, uint32_t ware) { return ware < 24 ? x.s32(0x7C + 4 * ware) : 0; };
		auto price = [](const TradeStop& x, uint32_t ware) { return ware < 24 ? x.s32(0x1C + 4 * ware) : 0; };
		for (uint32_t w = 0; w < 24; ++w) {
			const uint8_t ware = r.u8(0x4 + w);
			if (t.u8(0x4 + w) == ware && amount(t, ware) == amount(r, ware) && price(t, ware) == price(r, ware)) {
				continue;
			}
			if (full()) {
				return;
			}
			op[0] = 0x69;
			op[1] = at | uint32_t(s.u8(0x0)) << 16;
			op[2] = ware | w << 16;
			op[3] = uint32_t(amount(r, ware));
			op[4] = uint32_t(price(r, ware));
			queue();
		}
		at = t.u16(0x0);
	}
	release(true);
}

}  // namespace p3::data
