#include "debug_states.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include "p3data/game_memory.hpp"
#include "p3data/hanse_tasks.hpp"
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
	"  damaged     the player's ships in port with half their hull (the shipyard)\n"
	"  tavern-captain  a free captain in the tavern of the player's first ship's town\n"
	"  convoy-leader  the player's first ship in port fit to guide a convoy\n"
	"  shipyard-materials  100 units of each shipyard material in that ship's town\n";

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
		} else if (state == "alderman") {
			save.globals.u8(kAlderman) = uint8_t(save.player);
			save.globals.u8(kAldermanTown) = m.u8(0x19);
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
