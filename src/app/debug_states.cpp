#include "debug_states.hpp"

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

}  // namespace

const char kDebugStatesHelp[] =
	"Debugging states (--debug-state, comma-separated, for the path to Alderman):\n"
	"  mayor       the player is mayor of its home town with reputation to stand\n"
	"  candidate   mayor and candidate: the Hanse offers its tasks after a tick\n"
	"  materials   road materials in the player's first ship (12 loads)\n"
	"  level4      three tasks done: the player can stand for election\n"
	"  alderman    the player is the Alderman\n";

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
		} else if (state == "alderman") {
			save.globals.u8(kAlderman) = uint8_t(save.player);
			save.globals.u8(kAldermanTown) = m.u8(0x19);
		} else {
			error = "unknown debugging state \"" + state + "\"";
			return false;
		}
	}
	return true;
}

}  // namespace p3::app
