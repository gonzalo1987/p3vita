// The ship menu's convoy rules (ship_menu_window.hpp; re/emu/sesion40/README.md, «Part 2»).

#include "p3data/ship_menu_window.hpp"

#include <algorithm>

#include "p3data/game_memory.hpp"
#include "p3data/ship_list_window.hpp"

namespace p3::data {

namespace {

uint32_t convoy_count(const SaveGame& save) {
	return std::min<uint32_t>(save.globals.u16(global::kShipCount + 2), uint32_t(save.convoys.size()));
}

uint32_t ship_count(const SaveGame& save) {
	return std::min<uint32_t>(save.globals.u16(global::kShipCount), uint32_t(save.ships.size()));
}

}  // namespace

bool ship_menu_shows_convoy(const SaveGame& save, uint16_t ship, int shown_convoy) {
	const uint16_t convoy = save.ships[ship].memory.u16(0x8);
	return convoy < convoy_count(save) && int(convoy) != shown_convoy;
}

ConvoyMenuList convoy_menu_list(const SaveGame& save, uint16_t convoy) {
	ConvoyMenuList list;
	if (convoy >= convoy_count(save)) {
		return list;
	}
	const uint32_t ships = ship_count(save);
	size_t walked = 0;
	for (uint32_t s = save.convoys[convoy].u16(0xA); s < ships && walked < save.ships.size();
		 s = save.ships[s].memory.u16(0x6), ++walked) {
		if (save.ships[s].memory.u16(0x134) < SavedShip::kFreeShipStatus) {
			list.ships.push_back(uint16_t(s));
		}
	}
	const uint16_t guide = save.convoys[convoy].u16(0x10);
	const auto at = std::find(list.ships.begin(), list.ships.end(), guide);
	list.pointed = at != list.ships.end() ? int(at - list.ships.begin()) : -1;
	return list;
}

PirateName pirate_name(const SaveGame& save, uint16_t ship) {
	PirateName name;
	const BattleGraph& g = save.battles;
	for (size_t k = 0; k < g.count(); ++k) {
		const int b = g.battle(k);
		if (b < 0) {
			continue;
		}
		const uint16_t entries = g.get<uint16_t>(size_t(b), 0x14);
		const int list = g.ptr(size_t(b), 0x4);
		for (uint16_t i = 0; i < entries && list >= 0; ++i) {
			const int entry = g.ptr(size_t(list), 4 * size_t(i));
			if (entry >= 0 && g.get<uint32_t>(size_t(entry), 0x4) == ship) {
				name.first = g.get<uint8_t>(size_t(b), 0x1E90);
				name.family = g.get<uint8_t>(size_t(b), 0x1E91);
				name.known = name.first != 0xFF || name.family != 0xFF;
				return name;
			}
		}
	}
	uint32_t s = ship;
	const uint16_t convoy = save.ships[ship].memory.u16(0x8);
	if (convoy < convoy_count(save)) {
		s = save.convoys[convoy].u16(0x10);
	}
	if (s >= save.ships.size()) {
		return name;
	}
	const uint16_t captain = save.ships[s].memory.u16(0x42);
	if (captain < save.globals.u16(global::kCaptainCount) && captain < save.captains.size()) {
		name.known = true;
		name.first = save.captains[captain].u8(0x2);
		name.family = save.captains[captain].u8(0x3);
	}
	return name;
}

bool hanse_fleet_convoy(const SaveGame& save, uint16_t convoy) {
	if (convoy >= save.convoys.size()) {
		return false;
	}
	const uint16_t e = save.convoys[convoy].u16(0x2);
	return e < save.globals.u16(0x6DD748) && e < save.events.size() && save.events[e].u16(0x6) == 0x34 &&
		   save.events[e].u16(0x16) == convoy;
}

ConvoyMenuHead convoy_menu_head(const SaveGame& save, const ConvoyMenuList& list) {
	ConvoyMenuHead h;
	if (list.ships.empty()) {
		return h;
	}
	const uint16_t pointed = list.ships[size_t(std::max(0, list.pointed))];
	const uint8_t owner = save.ships[pointed].memory.u8(0x0);
	const uint32_t player = save.globals.u32(global::kPlayer);
	int32_t hull_sum = 0;
	for (const uint16_t id : list.ships) {
		const ShipMemory& s = save.ships[id].memory;
		if (s.u8(0x0) == owner) {
			const int32_t equipment = s.s32(0x11C);
			h.equipment += equipment;
			if (equipment % 200) {
				h.equipment += 200 - equipment % 200;
			}
			h.sailors += s.u16(0x40);
			h.firepower += s.s32(0x120);
			for (int k = 0; k < 24; ++k) {
				h.cargo += s.s32(0x54 + 4 * size_t(k));
			}
			h.cargo += s.u8(0x156) * 2000;
		}
		if (s.u8(0x0) == player) {
			h.capacity += s.s32(0x10);
		}
		hull_sum += s.s32(0x14) != 0 ? s.s32(0x18) * 100 / s.s32(0x14) : 0;
	}
	h.hull = std::max(0, hull_sum / int32_t(list.ships.size()));
	if (owner != player) {
		h.hull -= figure_offset(h.hull, pointed, 20);
		h.hull = std::clamp(h.hull, 0, 100);
		h.firepower = std::max(0, h.firepower - figure_offset(h.firepower, pointed, 20));
		h.sailors = std::max(1, h.sailors - figure_offset(h.sailors, pointed, 40));
		const uint16_t convoy = save.ships[list.ships[0]].memory.u16(0x8);
		h.cargo = std::max(0, h.cargo - figure_offset(h.cargo, convoy, 70));
	}
	return h;
}

}  // namespace p3::data
