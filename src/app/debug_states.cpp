#include "debug_states.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "p3data/game_memory.hpp"
#include "p3data/hanse_tasks.hpp"
#include "p3data/mail.hpp"
#include "p3data/player_actions.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::app {

using namespace p3::data;

namespace {

constexpr uint32_t kAlderman = 0x6DE52D;  // u8 the Alderman ([+0x8D] of the world).
constexpr uint32_t kAldermanTown = 0x6DE52E;  // u8 its town.
constexpr uint32_t kAldermanBusy = 0x6DF31C;  // u32: no candidacy while it isn't 0.

// The player, mayor of its home town ([0x6F1], as 0x4F75F0 sees it) and with
// the third component of its reputation at 20 in every town (as
// make_candidate of retouch.py): «reputation to stand» over 120.
void make_mayor(SaveGame& save) {
	MerchantMemory& m = save.merchants[size_t(save.player)].memory;
	save.towns[m.u8(0x19)].memory.u8(0x6F1) = uint8_t(merchant_as_owner(save, save.player));
	m.u8(0x39C + m.u8(0x19)) = 6;  // The mayor's rank at home, as 0x4F75F0 leaves it.
	for (size_t i = 0; i < save.towns.size(); ++i) {
		m.f32(0x124 + 12 * i) = 20.0f;
	}
}

// What 0x4F75F0 does when the mayor reaches 120: [+0x37] = 1 and, if the
// Hanse isn't busy, the candidacy (0x5326E0), which queues the operations
// that create the tasks (they run after the next tick).
void make_candidate(SaveGame& save) {
	make_mayor(save);
	save.merchants[size_t(save.player)].memory.u8(0x37) = 1;
	if (save.globals.u32(kAldermanBusy) == 0) {
		tick::fn_5326e0(save);
	}
}

// The road materials in the player's first ship: 5 loads of wood, 4 of
// bricks, 10 barrels of tools and 2 loads of hemp (12 loads).
void give_materials(SaveGame& save) {
	const uint16_t first = save.merchants[size_t(save.player)].memory.u16(0xE);
	if (first >= save.ships.size()) {
		return;
	}
	SavedShip& ship = save.ships[first];
	const int32_t amounts[4] = {5 * 2000, 4 * 2000, 10 * 200, 2 * 2000};
	for (size_t k = 0; k < 4; ++k) {
		ship.memory.s32(0x54 + 4 * kRoadWares[k]) += amounts[k];
	}
	recalculate_cargo(ship);
}

// The election of the Alderman (event 0x20) moved to the first phase of the
// next day, as election_tomorrow of tools/oracle/retouch_player.py: unlinked,
// put at the head with its new tick and placed again (0x4D8DD0).
bool election_tomorrow(SaveGame& save) {
	GlobalMemory& g = save.globals;
	constexpr uint32_t kEventHead = 0x6DD73C + 0x8;
	constexpr uint32_t kEventCount = 0x6DD73C + 0xC;
	const uint32_t count = std::min<uint32_t>(g.u16(kEventCount), uint32_t(save.events.size()));
	uint16_t* link = &g.u16(kEventHead);
	for (size_t walked = 0; *link < count && walked < count; ++walked) {
		const uint16_t i = *link;
		if (save.events[i].u16(6) == 0x20) {
			*link = save.events[i].u16(4);
			save.events[i].u16(4) = g.u16(kEventHead);
			g.u16(kEventHead) = i;
			save.events[i].u32(0) = (g.u32(global::kWorldTick) & ~0xFFu) + 0x100;
			tick::events_detail::fn_4d8dd0(save);
			return true;
		}
		link = &save.events[i].u16(4);
	}
	return false;
}

// A pirate attack on the player's first ship, as event_attack of
// tools/oracle/retouch_pirates.py: the ship, in port, is ordered to sail to
// the last town (or the first; operation 0x00, run after the first tick); the
// last event of the queue becomes an event 0x1E (0x4DFD44) in state 2 at the
// ship's town, due in two days, when the ship is at sea; and event 0x1E's
// radius [0x6DD89C] is the largest. Then the pirates attack the ship
// (0x5063C0) and a sea battle against the player starts.
bool pirate_attack(SaveGame& save) {
	GlobalMemory& g = save.globals;
	constexpr uint32_t kEventHead = 0x6DD73C + 0x8;
	constexpr uint32_t kEventCount = 0x6DD73C + 0xC;
	constexpr uint32_t kPirateRange = 0x6DD89C;
	const uint16_t ship = save.merchants[size_t(save.player)].memory.u16(0xE);
	if (ship >= save.ships.size() || save.ships[ship].town() >= save.towns.size()) {
		return false;
	}
	const uint8_t town = save.ships[ship].town();
	const uint32_t target = town + 1u == save.towns.size() ? 0u : uint32_t(save.towns.size() - 1);
	const uint32_t fields[5] = {0x00, ship, target, 0, 0};
	std::vector<uint8_t> op(sizeof(fields));
	std::memcpy(op.data(), fields, sizeof(fields));
	save.queued_operations.push_back(op);
	// The last event of the queue, out of it and at its head with its new
	// tick; 0x4D8DD0 puts it in its place.
	const uint32_t count = std::min<uint32_t>(g.u16(kEventCount), uint32_t(save.events.size()));
	uint16_t* link = &g.u16(kEventHead);
	uint16_t* last = nullptr;
	for (size_t walked = 0; *link < count && walked < count; ++walked) {
		last = link;
		link = &save.events[*link].u16(4);
	}
	if (!last) {
		return false;
	}
	const uint16_t i = *last;
	GameMemory<0x18>& e = save.events[i];
	*last = e.u16(4);
	e.u16(4) = g.u16(kEventHead);
	g.u16(kEventHead) = i;
	const uint32_t now = g.u32(global::kWorldTick);
	e.u32(0) = now + 0x200;
	e.u16(6) = 0x1E;
	e.u32(8) = 0;
	e.u32(0xC) = now + 0x100000;  // Still going.
	e.u8(0x10) = 2;
	e.u8(0x11) = town;
	e.u16(0x12) = 0;
	e.u8(0x14) = 1;
	e.u8(0x15) = 3;  // Attacks left.
	e.u8(0x16) = 0;
	g.u16(kPirateRange) = 0xFFFF;
	tick::events_detail::fn_4d8dd0(save);
	return true;
}

// A town battle in Lübeck, the town the viewer opens (the battles' piece 6), as town_end_branches of
// tools/oracle/retouch_unchecked.py makes a raider arrive: the player's first ship is the town's guard ship
// ([+0x994]); the first AI merchant's ship sailing alone turns raider (operation 0x0D with 1, 0x5386C0) and gets a
// route of its own of one point, the town's approach point (StadtDaten.ini AnfahrtXPos/YPos), where it is put: at
// the next tick it arrives and attacks the town (0x506F33), the player's ship defending it.
bool town_attack(SaveGame& save) {
	const uint16_t guard = save.merchants[size_t(save.player)].memory.u16(0xE);
	uint8_t town = 0xFF;
	for (size_t i = 0; i < save.towns.size(); ++i) {
		if (save.towns[i].town_id() == 14) {  // Lübeck (main.cpp kLuebeck)
			town = uint8_t(i);
		}
	}
	if (town >= save.towns.size() || guard >= save.ships.size()) {
		return false;
	}
	const uint16_t convoys = save.globals.u16(global::kShips + 0xF6);
	for (size_t i = 0; i < save.ships.size(); ++i) {
		SavedShip& s = save.ships[i];
		if (!s.in_use() || s.owner() >= save.merchants.size() || int(s.owner()) == save.player || s.status() != 0xF ||
			s.convoy() < convoys) {
			continue;
		}
		const uint32_t fields[3] = {0x0D, uint32_t(i), 1};
		uint8_t op[0x14] = {};
		std::memcpy(op, fields, sizeof(fields));
		tick::fn_5386c0(save, op);
		if (s.memory.u8(0x15C) != 1) {
			continue;
		}
		const uint32_t entry = 0x6DDB90 + 0x34 * uint32_t(save.globals.u8(0x6DE4B8 + town));
		const int16_t ax = int16_t(save.globals.s32(entry + 0x20));
		const int16_t ay = int16_t(save.globals.s32(entry + 0x24));
		// Two points (where it was and the approach point): at the end of a route of one point a raider stays at
		// sea instead of attacking (Sailing, the route's end).
		s.set_route(std::make_shared<SeaRoute>(SeaRoute{MapPoint{s.memory.s16(0x1E), s.memory.s16(0x22)}, MapPoint{ax, ay}}));
		s.town_route = false;
		s.own_route() = 1;
		s.route_index() = 1;
		s.memory.u8(0x38) = town;
		s.memory.u32(0x1C) = uint32_t(uint16_t(ax)) << 16 | 0x7FFF;
		s.memory.u32(0x20) = uint32_t(uint16_t(ay)) << 16 | 0x7FFF;
		s.memory.u32(0x28) = 0;
		s.memory.u32(0x2C) = 0;
		save.towns[town].memory.u16(0x994) = guard;
		return true;
	}
	return false;
}

// The towns' event symbols, to look at them (the battles' piece 6, part 5): Lübeck with the eight bits of the town
// view's symbols in its [+0x2C8] (player_actions.hpp kTownViewEvents); and for the sea map, from the first other
// town on, one event of kSeaMapEvents each and as shortage the famine, the ware 3 short (bits 18..22) and the
// weapons, also in the 16 days of the towns' history (0x6DE588), so that the player knows them whatever his lag.
// Only for the eyes: the towns aren't really so.
bool town_events(SaveGame& save) {
	constexpr uint32_t kHistory = 0x6DE588;
	constexpr uint32_t kNoWare = 0x7C0000;
	uint32_t all = 0x1000 | 0x10000;
	for (const TownEventSymbol& e : kTownViewEvents) {
		all |= e.bit;
	}
	size_t k = 0;
	for (size_t i = 0; i < save.towns.size(); ++i) {
		uint32_t& flags = save.towns[i].memory.u32(0x2C8);
		uint32_t history = 0;
		if (save.towns[i].town_id() == 14) {  // Lübeck (main.cpp kLuebeck)
			flags = (flags & ~all) | (all & ~0x10000u);
			history = flags | kNoWare;
		} else if (k < 7) {
			flags = (flags & ~all) | kSeaMapEvents[k].bit | (k == 0 ? 0x1000 : k == 2 ? 0x10000 : 0);
			history = flags | (k == 1 ? 3u << 18 : kNoWare);
			++k;
		} else {
			continue;
		}
		for (uint32_t d = 0; d < 16; ++d) {
			save.globals.u32(kHistory + 0x40 * uint32_t(i) + 4 * d) = history;
		}
	}
	return k > 0;
}

// The player's ships in port, with half their hull (for the shipyard's repairs).
void damage_ships(SaveGame& save) {
	for (SavedShip& ship : save.ships) {
		if (ship.docked() && ship.owner() == save.player) {
			ship.health() = ship.max_health() / 2;
		}
	}
}

// A captain in the tavern of the town of the player's first ship in port:
// the head of its list ([0x82E], next [+0x0]) or, if empty, another town's
// head moved there; free ([+0x8] 0) and kept for no one ([+0xF] 0xFF).
bool tavern_captain(SaveGame& save) {
	const uint16_t count = save.globals.u16(global::kCaptainCount);
	int town = -1;
	for (const SavedShip& ship : save.ships) {
		if (ship.docked() && ship.owner() == save.player && ship.town() < save.towns.size()) {
			town = ship.town();
			break;
		}
	}
	if (town < 0) {
		return false;
	}
	uint16_t& head = save.towns[size_t(town)].memory.u16(0x82E);
	for (size_t u = 0; head >= count && u < save.towns.size(); ++u) {
		uint16_t& other = save.towns[u].memory.u16(0x82E);
		if (int(u) != town && other < count) {
			const uint16_t c = other;
			other = save.captains[c].u16(0);
			save.captains[c].u16(0) = head;
			head = c;
		}
	}
	if (head >= count) {
		return false;
	}
	save.captains[head].u8(0x8) = 0;
	save.captains[head].u8(0xF) = 0xFF;
	return true;
}

// All the tavern's people (tavern_window.hpp) in the town of the player's first ship in port: a free captain
// (tavern-captain), the trader (the stock of the ware 3 100000 above its [+0x4F4 + 0x30]), the informer ([+0x82C] 0),
// the traveller, the weapons dealer (25 swords), the trader and the burglar free ([+0x83C..0x83F] 0xFF).
bool tavern_people(SaveGame& save) {
	if (!tavern_captain(save)) {
		return false;
	}
	for (const SavedShip& ship : save.ships) {
		if (ship.docked() && ship.owner() == save.player && ship.town() < save.towns.size()) {
			TownMemory& t = save.towns[ship.town()].memory;
			t.s32(4 + 4 * 3) = t.s32(0x4F4 + 0x10 * 3) + 100000;
			t.u8(0x82C) = 0;
			t.u8(0x83C) = 0xFF;
			t.u8(0x83D) = 0xFF;
			t.u8(0x83E) = 0xFF;
			t.u8(0x83F) = 0xFF;
			t.s32(0x54) = 25;
			// The traveller: to the next town for 1500 within ten days.
			t.u8(0x834) = uint8_t((ship.town() + 1) % save.towns.size());
			t.u16(0x836) = 1500;
			t.u32(0x838) = save.globals.u32(global::kWorldTick) + 10 * 256;
			return true;
		}
	}
	return false;
}

// The player's first ship docked out of a convoy, fit to guide one
// (0x519C40): a captain of its town's list (or another town's), 25 sailors,
// 7 small guns (firepower 70) and its whole hull.
bool convoy_leader(SaveGame& save) {
	const uint16_t count = save.globals.u16(global::kCaptainCount);
	const uint16_t convoys = save.globals.u16(global::kShips + 0xF6);
	for (size_t i = 0; i < save.ships.size(); ++i) {
		SavedShip& ship = save.ships[i];
		if (!ship.docked() || ship.owner() != save.player || ship.town() >= save.towns.size() || ship.convoy() < convoys) {
			continue;
		}
		ShipMemory& s = ship.memory;
		for (size_t u = 0; u < save.towns.size() && s.u16(0x42) >= count; ++u) {
			uint16_t& head = save.towns[(ship.town() + u) % save.towns.size()].memory.u16(0x82E);
			if (head < count) {
				const uint16_t c = head;
				head = save.captains[c].u16(0);
				save.captains[c].u16(0) = 0xFFFF;
				s.u16(0x42) = c;
			}
		}
		s.u16(0x40) = 25;
		for (size_t k = 0; k < 24; ++k) {
			s.u8(0x13C + k) = k < 7 ? 1 : 0xFF;
		}
		s.s32(0x18) = s.s32(0x14);
		s.u8(0x15C) = 0;
		recalculate_cargo(ship);
		return s.u16(0x42) < count;
	}
	return false;
}

// The town of the player's first ship in port with 100 units of each of the
// shipyard's materials: timber, cloth, tools, hemp and pitch.
bool shipyard_materials(SaveGame& save) {
	for (const SavedShip& ship : save.ships) {
		if (ship.docked() && ship.owner() == save.player && ship.town() < save.towns.size()) {
			static constexpr int kWares[5][2] = {{0xB, 2000}, {8, 200}, {0xC, 200}, {0x11, 2000}, {0xF, 200}};
			for (const auto& w : kWares) {
				save.towns[ship.town()].memory.s32(4 + 4 * size_t(w[0])) = 100 * w[1];
			}
			return true;
		}
	}
	return false;
}

// The moneylender of the town of the player's first ship in port, busy: three
// loans on offer (the town's slots 0..2, [0x9C6 + 6·k]: rank, names, weeks,
// amount) and three townsmen asking for one (3..5); the player worth 300000
// ([+0x46C]) without debts or a bad payment history; and the first AI
// merchant's company selling 30 % of shares, 20 of them offered (operation
// 0x2B, then the pool's [+0x13]).
bool lender(SaveGame& save) {
	int town = -1;
	for (const SavedShip& ship : save.ships) {
		if (ship.docked() && ship.owner() == save.player && ship.town() < save.towns.size()) {
			town = ship.town();
			break;
		}
	}
	if (town < 0) {
		return false;
	}
	static constexpr uint8_t kSlots[6][6] = {{0, 1, 2, 12, 0xB8, 0x0B}, {0, 3, 4, 26, 0x28, 0x23}, {0, 5, 6, 8, 0x20, 0x4E},
		{2, 7, 8, 10, 0x98, 0x3A}, {5, 9, 10, 30, 0x30, 0x75}, {1, 11, 12, 20, 0x70, 0x17}};
	TownMemory& t = save.towns[size_t(town)].memory;
	for (size_t k = 0; k < 6; ++k) {
		std::memcpy(t.bytes.data() + 0x9C6 + 6 * k, kSlots[k], 6);
	}
	MerchantMemory& m = save.merchants[size_t(save.player)].memory;
	m.s32(0x46C) = 300000;
	m.s32(0x498) = 0;
	m.u8(0x1E) = 0;
	for (size_t ai = 0; ai < save.merchants.size(); ++ai) {
		if (int(ai) == save.player || save.merchants[ai].memory.u16(8) == 0) {
			continue;
		}
		save.merchants[ai].memory.s32(0x46C) = 50000;
		uint8_t op[0x14] = {0x2B, 0, 0, 0, uint8_t(ai), 30, uint8_t(ai), 0xFF, 0x28, 0x23};  // price 9000
		tick::fn_4d98f0(save, op);
		for (GameMemory<0x18>& e : save.events) {
			if (e.u16(6) == 0x18 && e.u8(0x10) == ai && e.u8(0x11) == 0xFF) {
				e.u8(0x13) = 20;
			}
		}
		break;
	}
	return true;
}

// Letters with buttons in the player's mailbox (letter_window.hpp): a suitor 0x21 and a proposal 0x22 in time, the
// prince's request 0x64 (the operation 0x8B on the siege object of his home town) and a townsman who can't pay back
// the player's loan, 0x79 (an event 0x01 to decide, due in 10 days).
void letters_with_buttons(SaveGame& save) {
	GlobalMemory& g = save.globals;
	const uint32_t tick = g.u32(global::kWorldTick);
	const MerchantMemory& m = save.merchants[size_t(save.player)].memory;
	const uint8_t home = m.u8(0x19);
	const uint8_t other = uint8_t((home + 1) % g.u32(global::kWorldTownCount));
	Letter suitor(0x21);
	suitor.entry.u8(5) = home;
	suitor.entry.u16(8) = 5;
	suitor.entry.u16(0xA) = 7;
	suitor.entry.u16(0xC) = 4000;
	suitor.entry.u16(0xE) = uint16_t((tick >> 8) % 365 + 14);
	send_letter(save, save.player, suitor);
	Letter proposal(0x22);
	const uint8_t bytes[8] = {5, 7, 2, other, 9, 11, 3, 14};
	std::memcpy(proposal.entry.bytes.data() + 8, bytes, 8);
	proposal.entry.u8(5) = home;
	send_letter(save, save.player, proposal);
	if (home < save.sieges.size()) {
		uint8_t op[0x14] = {0x8B, 0, 0, 0, 10, 20, 30, 40, 50, home, uint8_t(save.player), other};
		const uint32_t when = tick + 0x1500;
		std::memcpy(op + 0xC, &when, 4);
		tick::fn_536974(save, op);
	}
	const uint16_t index = tick::events_detail::fn_4d8cf0(save, tick + 0xA00);
	GameMemory<0x18>& e = save.events[index];
	e.u16(6) = 1;
	e.u32(8) = 10000;
	e.u8(0xC) = 0x40;
	e.u8(0x10) = uint8_t(save.player);
	e.u8(0x11) = 0xFF;
	e.u8(0x12) = 0;
	e.u8(0x13) = 0;
	e.u8(0x14) = home;
	e.u8(0x15) = 2;
	e.u8(0x16) = 5;
	e.u8(0x17) = 9;
	Letter debtor(0x79);
	debtor.entry.u8(5) = home;
	debtor.entry.u16(8) = 10000;
	debtor.entry.u16(0xA) = index;
	debtor.entry.u8(0xC) = 2;
	debtor.entry.u8(0xD) = 9;
	debtor.entry.u8(0xE) = m.u8(0x1C);
	debtor.entry.u8(0xF) = m.u8(0x1D);
	send_letter(save, save.player, debtor);
}

// The guild's auctions (guild_window.hpp): the player a member of the guild of his first ship's town (its bit in his
// [+0x468]) and two auctions of ships (events 0x0A as 0x4DA510 makes them, in phase 0, which makes their ship) at the
// next ticks, in that town and in the next one with a guild ([+0x2C8] & 0xC0): they are announced 14 days later and
// bid on a day after.
bool auctions(SaveGame& save) {
	GlobalMemory& g = save.globals;
	MerchantMemory& m = save.merchants[size_t(save.player)].memory;
	const uint16_t ship = m.u16(0xE);
	const uint32_t towns = g.u32(global::kWorldTownCount);
	if (ship >= save.ships.size() || save.ships[ship].town() >= towns) {
		return false;
	}
	const uint8_t town = save.ships[ship].town();
	m.u32(0x468) |= 1u << (town & 31);
	uint8_t other = town;
	for (uint32_t k = 1; k < towns; ++k) {
		const uint32_t t = (town + k) % towns;
		if (save.towns[t].memory.u8(0x2C8) & 0xC0) {
			other = uint8_t(t);
			break;
		}
	}
	const uint32_t now = g.u32(global::kWorldTick);
	const uint8_t places[2] = {town, other};
	for (uint32_t k = 0; k < 2; ++k) {
		GameMemory<0x18>& e = save.events[tick::events_detail::fn_4d8cf0(save, now + 1 + k)];
		e.u16(0x6) = 0xA;
		e.u16(0x8) = 0xFFFF;
		e.u16(0xA) = 0xFFFF;
		e.u16(0xC) = 0xFFFF;
		e.u16(0xE) = 0;
		e.u16(0x10) = 0;
		e.u16(0x12) = 0;
		e.u8(0x14) = 0;
		e.u8(0x15) = places[k];
		e.u8(0x16) = 0;
		e.u8(0x17) = 0xFF;
	}
	return true;
}

}  // namespace

const char kDebugStatesHelp[] =
	"Debugging states (--debug-state, comma-separated, for the path to Alderman):\n"
	"  mayor       the player is mayor of its home town with reputation to stand\n"
	"  candidate   mayor and candidate: the Hanse offers its tasks after a tick\n"
	"  materials   road materials in the player's first ship (12 loads)\n"
	"  level4      three tasks done: the player can stand for election\n"
	"  alderman    the player is the Alderman\n"
	"  election-tomorrow  the election of the Alderman at the start of the next day\n"
	"  pirate-attack  the player's first ship sails and pirates attack it two days\n"
	"              later (event 0x1E): a sea battle against the player\n"
	"  town-attack  an AI raider attacks Lübeck at the next tick, the player's first\n"
	"              ship its guard ship: a town battle against the player\n"
	"  town-events  Lübeck with the eight event symbols of the town view, and towns\n"
	"              with each of the sea map's (only to look at them)\n"
	"  nest-attack  the player's first ship sails and attacks the first pirate's nest\n"
	"              (a Hanse task of kind 3; the nest's place follows the ship): a\n"
	"              battle at the nest against the player\n"
	"  weak-towers  the towers of a town battle weakened when its screen opens: smoke\n"
	"              and fire (only to look at them)\n"
	"  damaged     the player's ships in port with half their hull (the shipyard)\n"
	"  tavern-captain  a free captain in the tavern of the player's first ship's town\n"
	"  tavern-people   all the tavern's people there (a captain, the trader, the informer, the traveller, ...)\n"
	"  convoy-leader  the player's first ship in port fit to guide a convoy\n"
	"  shipyard-materials  100 units of each shipyard material in that ship's town\n"
	"  lender      loans on offer and asked for in that ship's town, an AI merchant's\n"
	"              shares on sale (the moneylender)\n"
	"  letters     letters with buttons: a suitor, a proposal, the prince's request\n"
	"              and a townsman who can't pay back the player's loan\n"
	"  auctions    the player in the guild of his first ship's town and two auctions\n"
	"              of ships there and in the next town with a guild (bidding in 15 days)\n";

namespace {

constexpr uint32_t kTownTableEntries = 0x6DDB90;  // StadtDaten.ini, 0x34 bytes per number.
constexpr uint32_t kPirateObjects = global::kShips + 0xC;  // The 5 pirate objects.

// The first pirate with an object (its k), or -1.
int first_pirate(const SaveGame& save) {
	for (size_t k = 0; k < 5 && k < save.pirates.size(); ++k) {
		if (save.globals.u32(kPirateObjects + 4 * uint32_t(k)) != 0 && save.pirates[k] && k < save.pirate_nests.size()) {
			return int(k);
		}
	}
	return -1;
}

}  // namespace

// The player's attack on a pirate nest (the battles' piece 7, part 3), as nest_attack of
// tools/oracle/retouch_unchecked.py does it for an AI merchant: the player's first ship, in port, is ordered to sail
// to the last town (or the first; operation 0x00); a new event 0x32 (a Hanse task) of kind 3 for the player, started,
// in its phase 1 ([+0x12] 1: the nest found), against the first pirate's nest ([+0x10] the pirate, [+0x11] its
// nest's number), due at the next tick, the merchant's [+0xE1] the nest; and while that ship sails the viewer moves
// the nest's place to it (follow_nest_attack), so that the task finds it right at the nest (0x4EBA71: distance² 0)
// and it attacks (Sailing::nest_attack): a battle at the nest (0xFE − k) against the player.
bool nest_attack(SaveGame& save) {
	GlobalMemory& g = save.globals;
	const int k = first_pirate(save);
	const uint16_t ship = save.merchants[size_t(save.player)].memory.u16(0xE);
	if (k < 0 || ship >= save.ships.size() || save.ships[ship].town() >= save.towns.size()) {
		return false;
	}
	const uint8_t town = save.ships[ship].town();
	const uint32_t target = town + 1u == save.towns.size() ? 0u : uint32_t(save.towns.size() - 1);
	const uint32_t fields[5] = {0x00, ship, target, 0, 0};
	std::vector<uint8_t> op(sizeof(fields));
	std::memcpy(op.data(), fields, sizeof(fields));
	save.queued_operations.push_back(op);
	const uint8_t nest = save.pirates[size_t(k)]->u8(0xE);
	const uint32_t now = g.u32(global::kWorldTick);
	GameMemory<0x18>& e = save.events[tick::events_detail::fn_4d8cf0(save, now + 1)];
	e.u16(0x6) = 0x32;
	e.u32(0x8) = now + 0x3000;  // Its deadline.
	e.u8(0xC) = 3;
	e.u8(0xD) = uint8_t(save.player);
	e.u8(0xE) = 0;
	e.u8(0xF) = 1;
	e.u8(0x10) = uint8_t(k);
	e.u8(0x11) = nest;
	e.u8(0x12) = 1;
	e.u8(0x13) = 0xFF;
	e.u16(0x14) = 0xFFFF;
	e.u16(0x16) = 0;
	save.merchants[size_t(save.player)].memory.u8(0xE1) = nest;
	return true;
}

bool follow_nest_attack(SaveGame& save) {
	const int k = first_pirate(save);
	const uint16_t ship = save.player >= 0 ? save.merchants[size_t(save.player)].memory.u16(0xE) : 0xFFFF;
	if (k < 0 || ship >= save.ships.size()) {
		return false;
	}
	for (size_t i = 0; i < save.battles.count(); ++i) {  // Its battle has started: done.
		const int b = save.battles.battle(i);
		if (b >= 0 && save.battles.get<uint32_t>(size_t(b), 0) == Sailing::kTownBattleVtable &&
			save.battles.get<uint8_t>(size_t(b), 0x1E98) == 0xFE - k) {
			return false;
		}
	}
	const ShipMemory& m = save.ships[ship].memory;
	// The ships move in a tick before its events run (0x506720 before 0x4D85C0): the place where the ship will be
	// after the next tick, by how far it went in the last one.
	static uint32_t seen_tick = 0;
	static int32_t now_x = 0, now_y = 0, last_x = 0, last_y = 0;
	const uint32_t t = save.globals.u32(global::kWorldTick);
	if (t != seen_tick) {
		seen_tick = t;
		last_x = now_x;
		last_y = now_y;
		now_x = m.s16(0x1E);
		now_y = m.s16(0x22);
	}
	if (m.u16(0x134) == 0xF) {
		const uint32_t entry = kTownTableEntries + 0x34 * uint32_t(save.pirates[size_t(k)]->u8(0xE));
		save.globals.s32(entry + 0x20) = 2 * now_x - last_x;
		save.globals.s32(entry + 0x24) = 2 * now_y - last_y;
	}
	return true;
}

void weaken_towers(SaveGame& save, size_t battle) {
	static constexpr int32_t kStrength[4] = {120000, 50000, 25000, 9000};
	for (size_t k = 0; k < 4; ++k) {
		if (save.battles.get<int32_t>(battle, 0x1EC4 + 4 * k) > kStrength[k]) {
			save.battles.set<int32_t>(battle, 0x1EC4 + 4 * k, kStrength[k]);
		}
	}
}

bool apply_debug_states(SaveGame& save, const std::string& states, std::string& error) {
	if (save.player < 0 || size_t(save.player) >= save.merchants.size()) {
		error = "the save has no player";
		return false;
	}
	MerchantMemory& m = save.merchants[size_t(save.player)].memory;
	size_t pos = 0;
	while (pos <= states.size()) {
		size_t end = states.find(',', pos);
		if (end == std::string::npos) {
			end = states.size();
		}
		const std::string state = states.substr(pos, end - pos);
		pos = end + 1;
		if (state.empty()) {
			continue;
		}
		if (state == "mayor") {
			make_mayor(save);
		} else if (state == "candidate") {
			make_candidate(save);
		} else if (state == "materials") {
			give_materials(save);
		} else if (state == "level4") {
			make_mayor(save);
			m.u8(0x37) = 4;
			// The election (0x529700) looks at the merchants of the towns with [0x2C8] & 0x40.
			save.towns[m.u8(0x19)].memory.u32(0x2C8) |= 0x40;
		} else if (state == "election-tomorrow") {
			if (!election_tomorrow(save)) {
				error = "the save has no election (event 0x20)";
				return false;
			}
		} else if (state == "pirate-attack") {
			if (!pirate_attack(save)) {
				error = "the player's first ship isn't in a town, or the save has no events";
				return false;
			}
		} else if (state == "town-attack") {
			if (!town_attack(save)) {
				error = "the player has no ship, or no AI ship sails alone";
				return false;
			}
		} else if (state == "town-events") {
			if (!town_events(save)) {
				error = "the save has no other town than Lübeck";
				return false;
			}
		} else if (state == "alderman") {
			save.globals.u8(kAlderman) = uint8_t(save.player);
			save.globals.u8(kAldermanTown) = m.u8(0x19);
		} else if (state == "nest-attack") {
			if (!nest_attack(save)) {
				error = "the player's first ship isn't in a town, or no pirate has a nest";
				return false;
			}
		} else if (state == "weak-towers") {
			// The viewer weakens them when the battle's screen opens (weaken_towers).
		} else if (state == "damaged") {
			damage_ships(save);
		} else if (state == "shipyard-materials") {
			if (!shipyard_materials(save)) {
				error = "the player has no ship in port";
				return false;
			}
		} else if (state == "convoy-leader") {
			if (!convoy_leader(save)) {
				error = "the player has no ship in port out of a convoy, or no town has a captain";
				return false;
			}
		} else if (state == "letters") {
			letters_with_buttons(save);
		} else if (state == "auctions") {
			if (!auctions(save)) {
				error = "the player's first ship isn't in a town";
				return false;
			}
		} else if (state == "lender") {
			if (!lender(save)) {
				error = "the player has no ship in port";
				return false;
			}
		} else if (state == "tavern-people") {
			if (!tavern_people(save)) {
				error = "the player has no ship in port, or no town has a captain";
				return false;
			}
		} else if (state == "tavern-captain") {
			if (!tavern_captain(save)) {
				error = "the player has no ship in port, or no town has a captain";
				return false;
			}
		} else {
			error = "unknown debugging state \"" + state + "\"";
			return false;
		}
	}
	return true;
}

}  // namespace p3::app
