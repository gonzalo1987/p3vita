#include "debug_states.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "p3data/game_memory.hpp"
#include "p3data/hanse_tasks.hpp"
#include "p3data/land_trade_window.hpp"
#include "p3data/mail.hpp"
#include "p3data/player_actions.hpp"
#include "p3data/sailing.hpp"
#include "p3data/ship_list_window.hpp"
#include "p3data/simulation.hpp"
#include "p3data/town_hall_window.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"
#include "p3data/wares.hpp"

namespace p3::data::tick::events_towns_detail {
int fn_5308a0_create(SaveGame& save, uint32_t merchant, uint32_t town);  // events_towns.cpp: the office, made if none
bool fn_52a360(SaveGame& save, int town, uint32_t merchant_arg, uint32_t type_arg, uint8_t force);  // events_towns.cpp
}  // namespace p3::data::tick::events_towns_detail

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

// As pirate_attack, the player's first ship first made the guide of a private convoy (0x15, then 0x18) with another
// of his ships docked in its port and out of a convoy: a captain given to it (as convoy_leader) when it has none.
bool convoy_attack(SaveGame& save) {
	const uint16_t ship = save.merchants[size_t(save.player)].memory.u16(0xE);
	if (ship >= save.ships.size() || !save.ships[ship].docked() || save.ships[ship].town() >= save.towns.size()) {
		return false;
	}
	const uint16_t convoys = save.globals.u16(global::kShips + 0xF6);
	int other = -1;
	for (size_t i = 0; i < save.ships.size() && other < 0; ++i) {
		const SavedShip& s = save.ships[i];
		if (i != ship && s.docked() && s.owner() == save.player && s.town() == save.ships[ship].town() &&
			s.convoy() >= convoys) {
			other = int(i);
		}
	}
	if (other < 0) {
		return false;
	}
	ShipMemory& g = save.ships[ship].memory;
	const uint16_t count = save.globals.u16(global::kCaptainCount);
	for (size_t u = 0; u < save.towns.size() && g.u16(0x42) >= count; ++u) {
		uint16_t& head = save.towns[(save.ships[ship].town() + u) % save.towns.size()].memory.u16(0x82E);
		if (head < count) {
			const uint16_t c = head;
			head = save.captains[c].u16(0);
			save.captains[c].u16(0) = 0xFFFF;
			g.u16(0x42) = c;
		}
	}
	for (const OperationBytes& op : create_convoy_operations(save, {ship, uint16_t(other)}, false, false, 0)) {
		save.queued_operations.push_back(std::vector<uint8_t>(op.begin(), op.end()));
	}
	return pirate_attack(save);
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
			// The pirate (0x5261D0): the next captain of the town's list busy ([+0x8] above 0x20) and nobody's; one of
			// another town's list moved there if there is none.
			const uint16_t count = save.globals.u16(global::kCaptainCount);
			uint16_t& next = save.captains[t.u16(0x82E)].u16(0);
			for (size_t u = 0; next >= count && u < save.towns.size(); ++u) {
				uint16_t& other = save.towns[u].memory.u16(0x82E);
				if (u != ship.town() && other < count) {
					const uint16_t c = other;
					other = save.captains[c].u16(0);
					save.captains[c].u16(0) = 0xFFFF;
					next = c;
				}
			}
			if (next < count) {
				save.captains[next].u8(0x8) = 0x40;
				save.captains[next].u8(0xF) = 0xFF;
			}
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

// The office's pages of information (office_window.hpp): the player's office in his home town (made if there is none)
// with some stock, a day's making and using of a few wares (the beer short for the week), hand weapons, pieces and
// cutlasses.
bool office_goods(SaveGame& save) {
	const uint32_t home = save.merchants[size_t(save.player)].hometown();
	const int office = tick::events_towns_detail::fn_5308a0_create(save, uint32_t(save.player), home);
	if (office < 0) {
		return false;
	}
	OfficeMemory& o = save.offices[size_t(office)].memory;
	struct Ware {
		uint32_t ware;
		int32_t stock, made, used;
	};
	const Ware wares[] = {{0, 60 * 2000, 4000, 0}, {3, 15 * 200, 0, 600}, {11, 22 * 2000, 0, 900}, {12, 45 * 200, 2000, 300},
		{18, 0, 1500, 0}};
	for (const Ware& w : wares) {
		o.s32(0x4 + 4 * w.ware) = w.stock;
		o.s32(0xC4 + 4 * w.ware) = w.made;
		o.s32(0x64 + 4 * w.ware) = w.used;
	}
	o.s32(0x4 + 4 * 20) = 120;  // 12 swords
	o.s32(0x4 + 4 * 22) = 35;  // 3 crossbows
	o.s32(0x124) = 3000;  // 3 small catapults
	o.s32(0x124 + 4 * 5) = 2000;  // 2 cannons
	o.s32(0x2BC) = 4;
	return true;
}

// The player married (the office's «Personal»; no save has it): his wife from his home town ([+0x31]), her first name
// [+0x30], portrait [+0x27], 25 years old ([+0x28]), two children, respected (2) by the citizens and councillors ([+0x32]).
void married(SaveGame& save) {
	MerchantMemory& m = save.merchants[size_t(save.player)].memory;
	const uint32_t now = save.globals.u32(global::kWorldTick);
	m.u8(0x31) = m.u8(0x19);
	m.u8(0x30) = 5;
	m.u8(0x27) = 3;
	m.u32(0x28) = now > 25u * 93440u ? now - 25u * 93440u : 0;
	m.u8(0x36) = 2;
	m.u8(0x32) = 2;
}

// The arsenal (town_hall_window.hpp): the player the mayor of his home town (make_mayor), his office there (made if
// there is none) with 60 of each hand weapon but the muskets (40), 300 beggars in the town, three archers on duty
// and one in training.
bool arsenal(SaveGame& save) {
	make_mayor(save);
	const uint32_t home = save.merchants[size_t(save.player)].hometown();
	const int office = tick::events_towns_detail::fn_5308a0_create(save, uint32_t(save.player), home);
	if (office < 0) {
		return false;
	}
	OfficeMemory& o = save.offices[size_t(office)].memory;
	for (uint32_t k = 0; k < 4; ++k) {
		o.s32(0x54 + 4 * k) = k == 3 ? 40 : 60;
	}
	TownMemory& t = save.towns[home].memory;
	t.s32(0x2E4) = 300;
	t.u8(0x998 + 1) = 3;
	t.u8(0x99C + 1) = 1;
	return true;
}

// The town hall's notices (town_hall_window.hpp): three of the world's seven orders (0x6DE530; the event 0x22 makes them
// only for wares a town lacks without stock nor production): 40 loads of grain for the town after the player's home
// town, 25 barrels of beer with the bonus for the next one, 12 barrels of wine for the one after it, due in 20, 30 and
// 45 days; the player's standing at least 20, so that he may take them.
void town_orders(SaveGame& save) {
	GlobalMemory& g = save.globals;
	constexpr uint32_t kOrders = 0x6DE530;
	struct Order {
		uint8_t ware, bonus, count;
		uint32_t days, price;
	};
	constexpr Order kOrdersMade[3] = {{0, 0, 40, 20, 120}, {3, 1, 25, 30, 70}, {7, 0, 12, 45, 380}};
	for (uint32_t k = 0; k < 7; ++k) {
		g.u8(kOrders + 12 * k) = 0xFF;
		g.u8(kOrders + 12 * k + 1) = 0xFF;
	}
	const uint32_t towns = g.u32(global::kWorldTownCount);
	const uint32_t home = save.merchants[size_t(save.player)].hometown();
	for (uint32_t k = 0; k < 3; ++k) {
		const uint32_t at = kOrders + 12 * k;
		g.u8(at) = uint8_t((home + 1 + k) % towns);
		g.u8(at + 1) = kOrdersMade[k].ware;
		g.u8(at + 2) = kOrdersMade[k].bonus;
		g.u8(at + 3) = kOrdersMade[k].count;
		g.u32(at + 4) = g.u32(global::kWorldTick) + kOrdersMade[k].days * 256;
		g.u32(at + 8) = kOrdersMade[k].price;
	}
	MerchantMemory& m = save.merchants[size_t(save.player)].memory;
	if (int16_t(m.u16(0x22)) < 20) {
		m.u16(0x22) = 20;
	}
}

// The land trade (land_trade_window.hpp): the first town from the player's home town on whose side menu lets its
// window open (0x5336B0: the barn built, no siege) and its partner, the player's office in both (made where there is
// none), in the first one 10 loads of grain, 30 barrels of beer, 12 barrels of wine and 8 loads of timber; the town
// and how many towns from the home town (R in the viewer) on stdout.
bool land_trade(SaveGame& save) {
	const uint32_t player = uint32_t(save.player);
	const uint32_t home = save.merchants[size_t(save.player)].hometown();
	for (uint32_t k = 0; k < save.towns.size(); ++k) {
		const uint32_t town = uint32_t((home + k) % save.towns.size());
		uint8_t partner = 0xFF;
		if (fn_5336b0(save, town, partner) != 0) {
			continue;
		}
		const int from = tick::events_towns_detail::fn_5308a0_create(save, player, town);
		const int to = tick::events_towns_detail::fn_5308a0_create(save, player, partner);
		if (from < 0 || to < 0) {
			return false;
		}
		OfficeMemory& o = save.offices[size_t(from)].memory;
		o.s32(4 + 4 * 0) += 20000;
		o.s32(4 + 4 * 3) += 6000;
		o.s32(4 + 4 * 7) += 2400;
		o.s32(4 + 4 * 11) += 16000;
		std::printf("land-trade: town %u (R x%u from the home town %u), its partner %u\n", town, k, home, partner);
		return true;
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
// bid on a day after. `count` 1: only the first (the guild's window keeps one auction's results: the last's).
bool auctions(SaveGame& save, uint32_t count = 2) {
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
	for (uint32_t k = 0; k < count; ++k) {
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

// The guild's buildings' auction: the player a member of his first ship's town's guild and an auction of three of the
// town's buildings (an event 0x36 as 0x4DAB00 makes one, in phase 1 at the next tick, waiting a day): the last three
// records of a type below 0x39 owned by AI merchants (or, short of them, the player's own, or the town's made an AI
// trader's), each owner given the town's guild bit (as 0x4DAB00 asks).
bool building_auction(SaveGame& save) {
	GlobalMemory& g = save.globals;
	MerchantMemory& m = save.merchants[size_t(save.player)].memory;
	const uint16_t ship = m.u16(0xE);
	if (ship >= save.ships.size() || save.ships[ship].town() >= g.u32(global::kWorldTownCount)) {
		return false;
	}
	const uint8_t town = save.ships[ship].town();
	m.u32(0x468) |= 1u << (town & 31);
	const SavedTown& st = save.towns[town];
	const uint32_t merchants = g.u16(global::kWorldMerchantCount);
	std::vector<uint16_t> lots;
	// The AI merchants' first, then the player's, then (a save's early years) the town's own houses made the first AI
	// trader's.
	const int ai = [&]() {
		for (uint32_t k = 0; k < merchants && k < save.merchants.size(); ++k) {
			if (k != uint32_t(save.player) && !(save.merchants[k].memory.u8(0x8) & 6)) {
				return int(k);
			}
		}
		return -1;
	}();
	SavedTown& owned = save.towns[town];
	for (int pass = 0; pass < 3; ++pass) {
		for (uint32_t r = std::min<uint32_t>(st.memory.u16(0x776), uint32_t(st.records.size())); r-- > 0 && lots.size() < 3;) {
			BuildingRecord& b = owned.records[r];
			const bool merchant = b.owner < merchants;
			if (b.type >= 0x39 || (pass < 2 && (!merchant || (b.owner == uint32_t(save.player)) != (pass == 1))) ||
				(pass == 2 && (merchant || ai < 0 || b.type == 0))) {
				continue;
			}
			if (pass == 2) {
				b.owner = uint8_t(ai);
			}
			const size_t cell = size_t(st.memory.u32(0x7AC)) * b.x + b.y;
			const bool fine = cell < st.grid.size() && (st.grid[cell] & 0x80);
			save.merchants[b.owner].memory.u32(0x468) |= 1u << (town & 31);
			lots.push_back(uint16_t(r | (fine ? 0x8000 : 0)));
		}
	}
	if (lots.empty()) {
		return false;
	}
	lots.resize(3, 0xFFFF);
	GameMemory<0x18>& e = save.events[tick::events_detail::fn_4d8cf0(save, g.u32(global::kWorldTick) + 1)];
	e.u16(0x6) = 0x36;
	e.u16(0x8) = lots[0];
	e.u16(0xA) = lots[1];
	e.u16(0xC) = lots[2];
	e.u16(0xE) = 0x100;
	e.u16(0x10) = 0;
	e.u16(0x12) = 0;
	e.u8(0x14) = 1;
	e.u8(0x15) = town;
	e.u8(0x16) = 0;
	e.u8(0x17) = 0xFF;
	return true;
}

// The guild's trading centres: the player's own finds (his [+0x4D8]) of America's points (the first 100 of
// Expeditionen.dat) north of y 70 and of the others west of x 300, every third one with the wide brush, and every
// trading centre (his [+0x538] 0x7FFF), those nobody found yet found by him ten days ago.
void trading_centres(SaveGame& save) {
	GlobalMemory& g = save.globals;
	MerchantMemory& m = save.merchants[size_t(save.player)].memory;
	for (uint32_t j = 0; j < 384 && j < save.expeditions.size(); ++j) {
		const bool america = j < 100;
		if ((america && save.expeditions[j][1] < 70) || (!america && save.expeditions[j][0] < 300)) {
			m.u32(0x4D8 + 4 * (j >> 5)) |= 1u << (j & 31);
			if (j % 3 == 0) {
				m.u32(0x508 + 4 * (j >> 5)) |= 1u << (j & 31);
			}
		}
	}
	m.u32(0x538) = 0x7FFF;
	const uint32_t now = g.u32(global::kWorldTick);
	for (uint32_t k = 0; k < 15; ++k) {
		const uint32_t o = 0x6DEFB0 + 0x2C * k;
		if (g.u8(o + 6) >= g.u16(global::kWorldMerchantCount) || g.u32(o + 8) > now) {
			g.u8(o + 6) = uint8_t(save.player);
			g.u32(o + 8) = now - 10 * 256;
		}
	}
}

// The guild's trade contracts: two of the player's in the world objects' lists (SaveGame::world_object_lists, as
// 0x52C6C0 adds them with the object's counts [+0x28] and [+0x1C]): the first trading post buys 1200 of its first
// ware it wants at 95 each, by 120 days from now; the second sells 800 of its second ware it offers at 40, by 60 days.
void trade_contracts(SaveGame& save) {
	GlobalMemory& g = save.globals;
	const uint32_t now = g.u32(global::kWorldTick);
	const struct {
		uint32_t object;
		uint8_t side;
		uint8_t buys;
		uint32_t amount;
		uint32_t price;
		uint32_t days;
	} kContracts[2] = {{0, 0, 1, 1200, 95, 120}, {1, 1, 0, 800, 40, 60}};
	for (const auto& c : kContracts) {
		GameMemory<0x10> order;
		order.u16(0x0) = uint16_t(save.player);
		order.u8(0x2) = c.side;
		order.u8(0x3) = c.buys;
		order.u32(0x4) = c.amount;
		order.u32(0x8) = c.price;
		order.u32(0xC) = now + c.days * 256;
		save.world_object_lists[c.object].push_back(order);
		const uint32_t o = 0x6DEFB0 + 0x2C * c.object;
		++g.u32(o + 0x28);
		++g.u16(o + 0x1C);
	}
}

}  // namespace

const char kDebugStatesHelp[] =
	"Debugging states (--debug-state, comma-separated, for the path to Alderman):\n"
	"  mayor       the player is mayor of its home town with reputation to stand\n"
	"  candidate   mayor and candidate: the Hanse offers its tasks after a tick\n"
	"  materials   road materials in the player's first ship (12 loads)\n"
	"  level4      three tasks done: the player can stand for election\n"
	"  alderman    the player is the Alderman\n"
	"  failed-task  a Hanse task of the player's failed today (kind 5, gone in 28 days):\n"
	"              the town hall's «Alderman's office» tells him to take no more (with\n"
	"              candidate: 0x4EC694 needs the merchant's [+0x37])\n"
	"  siege       a siege of the player's home town now (mercenaries; the town gate's pages)\n"
	"  siege-prince  the same by the prince's army\n"
	"  prince-loan  goods the prince of his home asked him for, accepted, in his office\n"
	"              there (the town gate's «Loan»)\n"
	"  order-client  a town hall order the player took in his first docked ship's town (10\n"
	"              of 30 left, a ware the ship carries): its client in the tavern there\n"
	"  order-premium  the same, paid at the end (a premium)\n"
	"  road-task   a Hanse road of the player's, 5 sections left, some materials delivered\n"
	"              (the town gate's «Overland route»; with materials: some to hand over)\n"
	"  town-manager  mayor of his home town without a council (the town hall's page 2:\n"
	"              the town manager's taxes)\n"
	"  council-vote  a town council's vote today in his home town, his rank there 5\n"
	"              (the town hall's «Council Chamber»)\n"
	"  diet-vote   a Hanse diet's vote today, his home the Alderman's town (the same)\n"
	"  guard-ship  his docked ship of least firepower enough made the guard ship of its\n"
	"              town (the town hall's «Outrigger»)\n"
	"  prince-decree  the prince's decree in his home town (the town hall's «Notices»\n"
	"              can be opened)\n"
	"  election-tomorrow  the election of the Alderman at the start of the next day\n"
	"  pirate-attack  the player's first ship sails and pirates attack it two days\n"
	"              later (event 0x1E): a sea battle against the player\n"
	"  convoy-attack  as pirate-attack, the player's first ship guiding a private\n"
	"              convoy with another of his ships in its port (the battle's ship\n"
	"              menu, «show convoy»)\n"
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
	"  season-N    every town's season [0x9BC] N (0 summer, 1 autumn, 2..5 the snow, 6\n"
	"              spring): the town view's snow (only to look at it, until it changes)\n"
	"  tavern-captain  a free captain in the tavern of the player's first ship's town\n"
	"  tavern-people   all the tavern's people there (a captain, the trader, the informer, the traveller, ...)\n"
	"  convoy-leader  the player's first ship in port fit to guide a convoy\n"
	"  shipyard-materials  100 units of each shipyard material in that ship's town\n"
	"  arsenal     the player the mayor of his home town, weapons in his office there,\n"
	"              beggars and archers in the town (the arsenal)\n"
	"  town-orders  three orders of towns in the town hall's notices (grain, beer\n"
	"              with the bonus, wine), the player's standing enough to take them\n"
	"  land-trade  the player's offices at both ends of the first open road, wares in\n"
	"              the first (its town and the R presses to it on stdout)\n"
	"  married     the player married, two children (the office's «Personal»)\n"
	"  home-elsewhere  the player's home town the next one (the office's «Move» offered)\n"
	"  shipyard-orders  20 snaikkas ordered by the player at his first ship's town's\n"
	"              shipyard (its list of contracts with its scroll bar)\n"
	"  raiding-ship  in each town another merchant's ship docked there raiding (the\n"
	"              lighthouse's list; the ship menu's head with the pirate's name)\n"
	"  lost-ship   the player's last ship out of a convoy lost (the state 0x11: the ship\n"
	"              list removes it)\n"
	"  office-goods  the player's office at home with stock, a day's making and using of\n"
	"              a few wares, weapons (the office's pages of information)\n"
	"  lender      loans on offer and asked for in that ship's town, an AI merchant's\n"
	"              shares on sale (the moneylender)\n"
	"  letters     letters with buttons: a suitor, a proposal, the prince's request\n"
	"              and a townsman who can't pay back the player's loan\n"
	"  auctions    the player in the guild of his first ship's town and two auctions\n"
	"              of ships there and in the next town with a guild (bidding in 15 days)\n"
	"  auction     the same with the first auction only\n"
	"  church-fed-N  every town's poor fed by its church to the level N (1..5): the\n"
	"              church's statues and altar in its picture\n"
	"  building-auction  the player in that guild and an auction of three of the town's\n"
	"              buildings there (bidding in about 2 days)\n"
	"  network     the settings' [+0xD] at 0: the network game's auction hall (and its\n"
	"              automatic bids)\n"
	"  centres     the player's own expeditions' finds (America's north, the Mediterranean's\n"
	"              west) and every trading centre (the guild's map)\n"
	"  contracts   two trade contracts of the player's with the first two trading posts\n";

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
		} else if (state == "convoy-attack") {
			if (!convoy_attack(save)) {
				error = "the player's first ship isn't in a town with another of his ships, or the save has no events";
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
		} else if (state == "town-manager") {
			// The player's home town without a council (its [+0x2C8] & 0xE0 cleared: as a town a merchant founded) and
			// he its mayor, the «Town Manager» of the town hall's page 2.
			make_mayor(save);
			save.towns[m.u8(0x19)].memory.u32(0x2C8) &= ~0xE0u;
		} else if (state == "council-vote" || state == "diet-vote") {
			// A vote of today for the town hall's «Council Chamber» in the player's home town (his rank there 5): the town
			// council's (an event 0x2E: the head tax to 12, two votes for, one against, eight abstaining, his bit to vote
			// in its [+0x13]), or the diet's (an event 0x21, the town his home made the Alderman's: a boycott of the next
			// town for an export tax on wine, his bit in its [+0x16]: 0x4D9D40).
			const uint32_t home = m.u8(0x19);
			const uint32_t towns = save.globals.u32(global::kWorldTownCount);
			m.u8(0x39C + home) = 5;
			uint32_t bit = 1;
			for (int p = 0; p < save.player; ++p) {
				if (save.merchants[size_t(p)].memory.u16(0x8) == 0) {
					bit += bit;
				}
			}
			const uint32_t now = save.globals.u32(global::kWorldTick);
			GameMemory<0x18>& e = save.events[tick::events_detail::fn_4d8cf0(save, now + 1)];
			if (state == "council-vote") {
				save.towns[home].memory.u32(0x2C8) |= 0x20;
				e.u16(0x6) = 0x2E;
				e.u32(0x8) = 0;
				e.u8(0xC) = uint8_t(home);
				e.u8(0xD) = 3;
				e.u8(0x10) = 2;
				e.u8(0x11) = 1;
				e.u8(0x12) = 8;
				e.u8(0x13) = uint8_t(bit);
				e.u8(0x14) = 12;
			} else {
				save.globals.u8(kAldermanTown) = uint8_t(home);
				save.towns[home].memory.u32(0x2C8) |= 0x20;
				e.u16(0x6) = 0x21;
				e.u8(0x8) = uint8_t(home);
				e.u8(0x9) = uint8_t((home + 1) % std::max<uint32_t>(1, towns));
				e.u8(0xA) = 0;
				e.u8(0xC) = 5;
				e.u8(0xE) = 2;
				e.u8(0x12) = 3;
				e.u8(0x13) = 6;
				e.u8(0x14) = 1;
				e.u16(0x16) = uint16_t(bit);
			}
		} else if (state == "alderman") {
			save.globals.u8(kAlderman) = uint8_t(save.player);
			save.globals.u8(kAldermanTown) = m.u8(0x19);
		} else if (state == "failed-task") {
			// A Hanse task of the player's that failed today, as fn_4eb0eb leaves it (an event 0x32 of kind 5, gone 28
			// days later): the town hall's page 7 tells him to take no more.
			const uint32_t now = save.globals.u32(global::kWorldTick);
			GameMemory<0x18>& e = save.events[tick::events_detail::fn_4d8cf0(save, now + 0x1C00)];
			e.u16(0x6) = 0x32;
			e.u32(0x8) = now + 0x1C01;
			e.u8(0xC) = 5;
			e.u8(0xD) = uint8_t(save.player);
			e.u8(0xF) = 1;
		} else if (state == "road-task") {
			// A Hanse road of the player's (an event 0x32 of kind 1, started) between a pair of 0x672BF8 whose two towns the
			// save has (one his home town if it can: the materials given there count, 0x53A3E0), 5 sections left, some
			// materials delivered (wood 2, bricks 4, tools 0, hemp 1: short of a section's), its deadline in 46 days: the
			// town gate's «Overland route».
			int a = -1;
			int b = -1;
			const uint32_t towns = std::min<uint32_t>(uint32_t(save.towns.size()), save.globals.u32(global::kWorldTownCount));
			for (int pass = 0; pass < 2 && b < 0; ++pass) {
				for (size_t k = 0; k < kRoadPairs && b < 0; ++k) {
					const std::array<uint8_t, 3> pair = road_pair(k);
					a = b = -1;
					for (uint32_t t = 0; t < towns; ++t) {
						if (save.towns[t].town_id() == pair[0]) {
							a = int(t);
						} else if (save.towns[t].town_id() == pair[1]) {
							b = int(t);
						}
					}
					if (a < 0 || (pass == 0 && a != int(m.u8(0x19)) && b != int(m.u8(0x19)))) {
						b = -1;
					}
				}
			}
			if (b < 0) {
				error = "the save has no pair of towns a road can join";
				return false;
			}
			const uint32_t now = save.globals.u32(global::kWorldTick);
			GameMemory<0x18>& e = save.events[tick::events_detail::fn_4d8cf0(save, now + 0x100)];
			e.u16(0x6) = 0x32;
			e.u32(0x8) = now + 0xBA00;
			e.u8(0xC) = 1;
			e.u8(0xD) = uint8_t(save.player);
			e.u8(0xF) = 1;
			e.u8(0x10) = uint8_t(a);
			e.u8(0x11) = uint8_t(b);
			e.u8(0x12) = 5;
			e.u8(0x13) = 0;
			e.u8(0x14) = 2;
			e.u8(0x15) = 4;
			e.u8(0x16) = 0;
			e.u8(0x17) = 1;
			if (m.u8(0x37) == 0) {
				m.u8(0x37) = 1;  // (0x4EC694 needs it.)
			}
		} else if (state == "siege" || state == "siege-prince") {
			// A siege of the player's home town now (the operation 0x8E: 0x633AF0 with the kind 0, r 0x40, no request
			// letters): its army made and set up; siege-prince the prince's army (its [+0x61C]). The town gate's pages.
			const uint32_t home = m.u8(0x19);
			OperationBytes op{};
			const uint32_t words[5] = {0x8E, home, 0, 0x40, 0};
			std::memcpy(op.data(), words, sizeof words);
			run_operation(save, nullptr, op.data());
			if (home >= save.siege_extras.size() || !save.siege_extras[home].army) {
				error = "the player's home town can't be besieged (its walls: three gates or towers)";
				return false;
			}
			if (state == "siege-prince") {
				save.siege_extras[home].army->u8(0x61C) = 1;
			}
		} else if (state == "prince-loan") {
			// The prince of the player's home town asked him for goods (timber 2, grain 3, tools 4, pitch 1, beer 5) and he
			// accepted (a node of its siege list [+0x1020] with [+0x7] set, the deadline in 10 days; the besieged town the
			// next one), the goods put in his office there if he has one: the town gate's «Loan».
			const uint32_t home = m.u8(0x19);
			if (home >= save.siege_extras.size()) {
				error = "no siege object for the player's home town";
				return false;
			}
			GameMemory<0x10> node;
			const uint8_t goods[5] = {2, 3, 4, 1, 5};
			for (uint32_t k = 0; k < 5; ++k) {
				node.u8(k) = goods[k];
			}
			node.u8(5) = uint8_t(save.player);
			node.u8(6) = uint8_t((home + 1) % std::max<uint32_t>(1, save.globals.u32(global::kWorldTownCount)));
			node.u8(7) = 1;
			node.u32(0xC) = save.globals.u32(global::kWorldTick) + 10 * 0x100;
			save.siege_extras[home].list_1020.push_back(node);
			const int office = tick::facilities_detail::fn_5308a0(save, uint32_t(save.player), home);
			if (office >= 0) {
				OfficeMemory& o = save.offices[size_t(office)].memory;
				o.s32(0x4 + 4 * 11) += 2 * 2000;  // timber
				o.s32(0x4 + 4 * 0) += 3 * 2000;   // grain
				o.s32(0x4 + 4 * 12) += 4 * 200;   // tools
				o.s32(0x4 + 4 * 15) += 1 * 200;   // pitch
				o.s32(0x4 + 4 * 3) += 5 * 200;    // beer
			}
		} else if (state == "order-client" || state == "order-premium") {
			// A town hall order the player took (an event 0x11 in the town's place [+0x830], as 0x3D leaves it) in the town
			// of his first docked ship, of the first ware that ship carries 2 loads or barrels of (else grain, 10 loads of
			// it put in the ship): 10 of 30
			// left at 100 each, its deadline in 20 days, paid by the unit or (order-premium) at the end: its client in
			// that town's tavern.
			int town = -1;
			int ware = -1;
			for (int s = m.u16(0xE); s >= 0 && size_t(s) < save.ships.size() && s < save.globals.u16(global::kShipCount);
				 s = save.ships[size_t(s)].memory.u16(0x4)) {
				ShipMemory& ship = save.ships[size_t(s)].memory;
				if (ship.u16(0x134) <= 1) {
					town = ship.u8(0x39);
					for (int w = 0; w < 20 && ware < 0; ++w) {
						if (ship.s32(0x54 + 4 * w) >= (kWares[w].unit == WareUnit::Barrel ? 400 : 4000)) {
							ware = w;
						}
					}
					if (ware < 0) {
						ware = 0;
						ship.s32(0x54) += 20000;  // (10 loads of grain to hand over.)
						recalculate_cargo(save.ships[size_t(s)]);
					}
					break;
				}
			}
			if (town < 0 || size_t(town) >= save.towns.size()) {
				error = "the player has no ship in port";
				return false;
			}
			const uint32_t now = save.globals.u32(global::kWorldTick);
			const uint32_t deadline = now + 20 * 0x100;
			const uint16_t index = tick::events_detail::fn_4d8cf0(save, deadline);
			GameMemory<0x18>& e = save.events[index];
			TownMemory& t = save.towns[size_t(town)].memory;
			e.u16(0x6) = 0x11;
			e.u8(0x8) = uint8_t(town);
			e.u8(0x9) = uint8_t(ware);
			e.u8(0xA) = t.u8(0x6E8);
			e.u8(0xB) = t.u8(0x6EC);
			e.u32(0xC) = deadline;
			e.u16(0x10) = 10;
			e.u16(0x12) = 30;
			e.u16(0x14) = 100;
			e.u8(0x16) = uint8_t(save.player);
			e.u8(0x17) = state == "order-premium" ? 1 : 0;
			t.u16(t.u16(0x830) < save.globals.u16(0x6DD73C + 0xC) ? 0x832 : 0x830) = index;
		} else if (state == "nest-attack") {
			if (!nest_attack(save)) {
				error = "the player's first ship isn't in a town, or no pirate has a nest";
				return false;
			}
		} else if (state == "weak-towers") {
			// The viewer weakens them when the battle's screen opens (weaken_towers).
		} else if (state == "damaged") {
			damage_ships(save);
		} else if (state.size() == 8 && state.compare(0, 7, "season-") == 0 && state[7] >= '0' && state[7] <= '6') {
			for (auto& town : save.towns) {
				town.memory.u8(0x9BC) = uint8_t(state[7] - '0');
			}
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
		} else if (state == "auctions" || state == "auction") {
			if (!auctions(save, state == "auction" ? 1 : 2)) {
				error = "the player's first ship isn't in a town";
				return false;
			}
		} else if (state == "building-auction") {
			if (!building_auction(save)) {
				error = "the player's first ship isn't in a town, or no AI merchant has a building there";
				return false;
			}
		} else if (state == "network") {
			save.settings.u8(0xD) = 0;
		} else if (state == "centres") {
			trading_centres(save);
		} else if (state == "contracts") {
			trade_contracts(save);
		} else if (state == "arsenal") {
			if (!arsenal(save)) {
				error = "no office for the player in his home town";
				return false;
			}
		} else if (state == "town-orders") {
			town_orders(save);
		} else if (state == "land-trade") {
			if (!land_trade(save)) {
				error = "no town lets the land trade's window open";
				return false;
			}
		} else if (state == "married") {
			married(save);
		} else if (state == "home-elsewhere") {
			// The player's home town the next one ([+0x19]): his office there offers the move (the office's «Move»).
			MerchantMemory& m = save.merchants[size_t(save.player)].memory;
			m.u8(0x19) = uint8_t((m.u8(0x19) + 1) % std::max<uint32_t>(1, save.globals.u32(global::kWorldTownCount)));
		} else if (state == "shipyard-orders") {
			// 20 snaikkas ordered by the player at his first ship's town's shipyard (0x52A360 with force), his office
			// there given the materials and him the money first: the shipyard's list of contracts with its scroll bar.
			const int ship = save.player >= 0 ? save.merchants[size_t(save.player)].memory.u16(0xE) : -1;
			if (ship < 0 || size_t(ship) >= save.ships.size()) {
				error = "the player has no ship";
				return false;
			}
			const int town = save.ships[size_t(ship)].memory.u8(0x39);
			const int office = tick::events_towns_detail::fn_5308a0_create(save, uint32_t(save.player), uint32_t(town));
			if (office >= 0) {
				for (int ware : {0xB, 8, 0xC, 0x11, 0xF}) {
					save.offices[size_t(office)].memory.s32(4 + 4 * ware) += 500 * 2000;
				}
			}
			save.merchants[size_t(save.player)].memory.s32(0) += 10000000;
			int ordered = 0;
			for (int k = 0; k < 20; ++k) {
				ordered += tick::events_towns_detail::fn_52a360(save, town, uint32_t(save.player), 0, 1) ? 1 : 0;
			}
			if (ordered == 0) {
				error = "the shipyard there takes no orders";
				return false;
			}
		} else if (state == "guard-ship") {
			// The town hall's outrigger kept by the player: in the town of his first docked ship, his docked ship of the
			// least firepower the page asks for or more made the town's guard ship (the operation 0x44 with its pay).
			const int first = save.player >= 0 ? save.merchants[size_t(save.player)].memory.u16(0xE) : -1;
			int town = -1;
			for (int s = first; s >= 0 && size_t(s) < save.ships.size() && s < save.globals.u16(global::kShipCount);
				 s = save.ships[size_t(s)].memory.u16(0x4)) {
				if (save.ships[size_t(s)].memory.u16(0x134) <= 1 && save.ships[size_t(s)].convoy() == 0xFFFF) {
					town = save.ships[size_t(s)].memory.u8(0x39);
					break;
				}
			}
			if (town < 0 || size_t(town) >= save.towns.size()) {
				error = "the player has no ship in port";
				return false;
			}
			const GuardShipTerms terms = guard_ship_terms(save, uint32_t(town));
			const std::vector<uint16_t> able = window_ships(save, save.player, town, ShipListMode::Guard, int32_t(terms.firepower));
			if (able.empty()) {
				error = "the player has no ship able to guard his ship's town";
				return false;
			}
			const uint16_t guard = *std::min_element(able.begin(), able.end(), [&](uint16_t a, uint16_t b) {
				return save.ships[a].memory.s32(0x120) < save.ships[b].memory.s32(0x120);
			});
			run_operation(save, nullptr, guard_ship_operation(uint32_t(town), guard, terms.pay).data());
		} else if (state == "prince-decree") {
			// The prince's decree in the player's home town (its siege object's wares the world lacks, [+0x1010..],
			// three months listed): its town hall's «Notices» can be opened (town_hall_menu).
			const uint32_t home = save.merchants[size_t(save.player)].hometown();
			if (home >= save.sieges.size()) {
				error = "no siege object for the player's home town";
				return false;
			}
			const uint8_t wares[4] = {5, 7, 0xFF, 0xFF};
			for (uint32_t k = 0; k < 4; ++k) {
				save.sieges[home].u8(0x1010 + k) = wares[k];
				save.sieges[home].u8(0x1014 + k) = k < 2 ? 3 : 0;
			}
		} else if (state == "raiding-ship") {
			// In each town, the first of the lighthouse's second table (port_ship_list: another merchant's ship docked
			// there, the states 0..3) raiding ([+0x15C] 1): the list says «Pirate» for its owner, the ship menu's head the
			// pirate's name (0x612EE0: its captain's or its convoy guide's, else «Unknown pirate»).
			int raiders = 0;
			for (size_t town = 0; town < save.towns.size() && save.player >= 0; ++town) {
				const ShipList list = port_ship_list(save, uint32_t(save.player), uint32_t(town));
				if (list.others > 0) {
					const uint16_t raider = list.ships[list.own];
					save.ships[raider].memory.u8(0x15C) = 1;
					std::printf("raiding-ship: town %zu, ship %u of merchant %u\n", town, unsigned(raider),
						unsigned(save.ships[raider].owner()));
					++raiders;
				}
			}
			if (raiders == 0) {
				error = "no other merchant's ship docked in any town";
				return false;
			}
		} else if (state == "lost-ship") {
			// The player's last ship out of a convoy in the state 0x11 (as a ship sunk in a battle: Sailing::sink).
			int last = -1;
			for (size_t i = 0; i < save.ships.size(); ++i) {
				if (save.ships[i].in_use() && save.ships[i].owner() == save.player && save.ships[i].convoy() == 0xFFFF) {
					last = int(i);
				}
			}
			if (last < 0) {
				error = "the player has no ship out of a convoy";
				return false;
			}
			save.ships[size_t(last)].status() = kShipSunk;
		} else if (state == "office-goods") {
			if (!office_goods(save)) {
				error = "no office for the player in his home town";
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
		} else if (state.size() == 12 && state.compare(0, 11, "church-fed-") == 0 && state[11] >= '1' &&
			state[11] <= '5') {
			// The church's [+0x0] (the town's [+0x794], less 100 a day) at N units of 0x4FE360 (the church factor, the
			// world's [+0x8C] at 0x6DE52C, + 1, · 2000): its level N (church_feeding_level).
			const int32_t unit = (int32_t(save.globals.u8(0x6DE52C)) + 1) * 2000;
			for (SavedTown& town : save.towns) {
				town.memory.s32(0x794) = (state[11] - '0') * unit;
			}
		} else {
			error = "unknown debugging state \"" + state + "\"";
			return false;
		}
	}
	return true;
}

}  // namespace p3::app
