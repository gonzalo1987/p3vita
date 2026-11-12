#include "p3data/ship_pair_window.hpp"

#include <cstring>

#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data {

uint32_t fn_5185c0(const ShipMemory& s);  // battle/pirate_objects.cpp: the sailors a ship can take

namespace {

OperationBytes words(std::initializer_list<uint32_t> fields) {
	OperationBytes op{};
	size_t at = 0;
	for (uint32_t f : fields) {
		std::memcpy(op.data() + at, &f, 4);
		at += 4;
	}
	return op;
}

bool docked(const ShipMemory& s) {
	return s.u16(0x134) == 0 || s.u16(0x134) == 1;
}

// The ship guides a convoy without the bit 1 of its [+0x14] (0x40F2A2..0x40F30F).
bool keeps_crew(const SaveGame& save, int ship) {
	const uint16_t convoy = save.ships[size_t(ship)].memory.u16(0x8);
	if (convoy >= save.convoy_count() || convoy >= save.convoys.size()) {
		return false;
	}
	const GameMemory<0x3C>& c = save.convoys[convoy];
	return c.u16(0x10) == uint16_t(ship) && !(c.u8(0x14) & 2);
}

bool has_captain(const SaveGame& save, int ship) {
	return save.ships[size_t(ship)].memory.u16(0x42) < save.globals.u16(global::kCaptainCount);
}

// Sailors from `giver` to `taker` (0x40F4C2..0x40F4F7).
bool may_pass_sailors(const SaveGame& save, int giver, int taker) {
	const ShipMemory& g = save.ships[size_t(giver)].memory;
	SavedShip t = save.ships[size_t(taker)];  // 0x5184F0 writes the hold (0x5182B0): asked of a copy.
	return !keeps_crew(save, giver) && g.u16(0x40) > 0 && t.memory.u16(0x40) < fn_5185c0(t.memory) &&
		tick::ship_ai_detail::fn_5184f0_on(t) != 0;  // unsigned above 0
}

// Hand weapons from `giver` to `taker` (0x40F54D..0x40F567).
bool may_pass_weapons(const SaveGame& save, int giver, int taker) {
	SavedShip t = save.ships[size_t(taker)];
	return save.ships[size_t(giver)].memory.u16(0x154) > 0 && recalculate_cargo(t) >= 10;
}

}  // namespace

bool can_pair_ships(const SaveGame& save, uint32_t player, int a, int b) {
	const uint16_t ships = save.globals.u16(global::kShipCount);
	if (a == b || a < 0 || b < 0 || a >= ships || b >= ships || size_t(a) >= save.ships.size() ||
		size_t(b) >= save.ships.size()) {
		return false;
	}
	const ShipMemory& sa = save.ships[size_t(a)].memory;
	const ShipMemory& sb = save.ships[size_t(b)].memory;
	return docked(sa) && docked(sb) && sa.u8(0x39) == sb.u8(0x39) && sa.u8(0) == player && sb.u8(0) == player;
}

ShipPairButtons ship_pair_buttons(const SaveGame& save, int a, int b) {
	ShipPairButtons on;
	on.captain[0] = !keeps_crew(save, b) && has_captain(save, b);
	on.captain[1] = !keeps_crew(save, a) && has_captain(save, a);
	on.sailors[0] = may_pass_sailors(save, b, a);
	on.sailors[1] = may_pass_sailors(save, a, b);
	on.weapons[0] = may_pass_weapons(save, b, a);
	on.weapons[1] = may_pass_weapons(save, a, b);
	return on;
}

OperationBytes swap_captains_operation(int a, int b) {
	return words({0x0F, uint32_t(a), uint32_t(b)});
}

OperationBytes pass_sailors_operation(int from, int to) {
	return words({0x10, uint32_t(from), uint32_t(to), 1});
}

OperationBytes pass_hand_weapons_operation(int from, int to) {
	return words({0x11, uint32_t(from), uint32_t(to), 1});
}

std::vector<SeaTarget> sea_targets(const SaveGame& save, int a) {
	std::vector<SeaTarget> list;
	const uint16_t ships = save.globals.u16(global::kShipCount);
	if (a < 0 || a >= ships || size_t(a) >= save.ships.size()) {
		return list;
	}
	const ShipMemory& sa = save.ships[size_t(a)].memory;
	for (const size_t field : {size_t(0x50), size_t(0x52)}) {
		const uint16_t ship = sa.u16(field);
		if (ship >= ships || ship >= save.ships.size()) {
			continue;
		}
		uint16_t drawn = ship;
		const uint16_t convoy = save.ships[ship].memory.u16(0x8);
		if (convoy < save.convoy_count() && convoy < save.convoys.size()) {
			drawn = save.convoys[convoy].u16(0x10);
			if (drawn >= save.ships.size()) {
				continue;
			}
		}
		const ShipMemory& sb = save.ships[drawn].memory;
		const uint16_t state = sb.u16(0x134);
		if ((state == 0xF && sa.u8(0x15C) != 0) || state == 0x12 || sb.u8(0x15C) != 0) {
			list.push_back({int(ship), int(drawn)});
		}
	}
	return list;
}

OperationBytes target_operation(int a, int b) {
	return words({0x0E, uint32_t(a), uint32_t(b)});
}

}  // namespace p3::data
