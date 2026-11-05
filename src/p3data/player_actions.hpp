#pragma once

// The operations the player's windows queue, byte for byte as the game's
// interface writes them (0x14 bytes: 0x535740 and then the fields), so that
// the viewer queues the very same operations: the table of the codes, with
// the window that queues each one, is docs/formats/operations.md. The
// simulation runs them (run_operation, simulation.cpp) as the game's executor
// 0x535760 does. Everything here only reads the save, except queue_operation.

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "p3data/savegame.hpp"

namespace p3::data {

using OperationBytes = std::array<uint8_t, 0x14>;

// Adds the operation to the queue (SaveGame::queued_operations). The game
// runs its queue between two calls to advance_time: advance_game_tick runs
// it after the next tick; the viewer may also run it at once
// (run_queued_operations) while the time doesn't move.
void queue_operation(SaveGame& save, const OperationBytes& operation);

// --- The market window (0x4A8CF0) ---
// Its mode ([+0xF0], set by 0x4A9690) says what it trades: 1 a ship and the
// town (0x02 buy, 0x01 sell), 4 a convoy and the town (0x1A, 0x19), 2 the
// office and the town (0x59, 0x58), 0 a ship and the office (0x08), 3 a
// convoy and the office (0x1B). The amounts it offers ([+0x520]): 1, 5, 50
// and all; here 0 is «all».
constexpr std::array<uint32_t, 4> kMarketSteps = {1, 5, 50, 0};

// What the window keeps of the ship and the town, as its summaries compute
// them (0x4AA9D0 for a ship, 0x4AAB10 for the town): a ware in its units
// (cargo or store / 200 or / 2000, truncated), and the hold the ship uses in
// steps of 200 (its [+0x118] plus the equipment [+0x11C], rounded up to 200
// by the equipment's remainder).
int32_t market_ship_units(const SaveGame& save, int ship, int ware);
int32_t market_town_units(const SaveGame& save, int town, int ware);
int32_t market_ship_used(const SaveGame& save, int ship);

// The amount the window sends when buying `step` of a ware (mode 1,
// 0x4A94A0..0x4A953C): n = the step or all the town's units, at most what
// the town has; in loads ×10; at most the hold's room (the ship's capacity
// [+0x10] / 200 minus what it uses); the amount n · 200, rounded down to
// loads. 0: the window sends nothing.
int32_t market_buy_amount(uint32_t step, int32_t town_units, int ware, int32_t capacity, int32_t used);
// Selling (0x4A95B3..0x4A9615): n = the step or all, at most the ship's
// units; the amount n · 200 or n · 2000 (0: nothing).
int32_t market_sell_amount(uint32_t step, int32_t ship_units, int ware);
// {code, s32 amount, u16 ware, u16 ship, u16 player, u16 town}: 0x02 or 0x01
// (0x4A9542..0x4A95A6, 0x4A961B..0x4A967C).
OperationBytes market_operation(uint32_t code, int32_t amount, int ware, int ship, uint16_t player, int town);

// The same from the save, for the ship docked in `town`; std::nullopt when
// the window sends nothing.
std::optional<OperationBytes> market_buy_operation(const SaveGame& save, int ship, int town, int ware, uint32_t step);
std::optional<OperationBytes> market_sell_operation(const SaveGame& save, int ship, int town, int ware, uint32_t step);

// --- A ship's crew and captain ---
// 0x04 (0x5D4C10): {0x04, ship ([+0x1C30]), amount (the number typed in the
// window, atoi)}: hire sailors.
OperationBytes hire_sailors_operation(uint32_t ship, uint32_t amount);
// 0x05 (the ship's window 0x493ED0): {0x05, ship, amount ([+0x2060])}:
// dismiss sailors.
OperationBytes dismiss_sailors_operation(uint32_t ship, uint32_t amount);
// 0x06 (0x5D4870): {0x06, captain ([+0x1C34]), ship ([+0x1C30]), town
// ([+0x1BFC])}: hire a captain of the town's list.
OperationBytes hire_captain_operation(uint32_t captain, uint32_t ship, uint32_t town);
// 0x07 (0x493ED0): {0x07, ship}: dismiss the ship's captain.
OperationBytes dismiss_captain_operation(uint32_t ship);
// 0x03 (the shipyard's window 0x5F8F50): {0x03, ship ([+0xC84])}: repair the
// ship; with the window's «whole convoy» ([+0xCDF]) it sends 0x1D with the
// ship's convoy instead.
OperationBytes repair_operation(uint32_t ship);
// 0x1D (0x5F8F50, «whole convoy»): {0x1D, the ship's convoy ([+0x8])}.
OperationBytes repair_convoy_operation(uint32_t convoy);

// --- What the windows list and show (they only read the save, but the
// tavern's roll) ---

// 0x504AC0 (thiscall on the ships' object: merchant, town, mode, 0): the
// merchant's ships a window lists in a town, walking his list ([+0xE], next
// [+0x4]); empty for a merchant out of the world (the game returns no list).
enum class ShipListMode : uint32_t {
	// The tavern's sailors (0x5D4C10): in the town, docked (state ≤ 1), not
	// marked [+0x136] & 1.
	Sailors = 0,
	// The shipyard's repairs (0x5F8F50): in the town, not marked [+0x136] & 1;
	// out of a convoy, docked and damaged ([+0x18] < [+0x14]); in a convoy,
	// its guide when the convoy ([+0x14] & 2) is docked ([+0x12] ≤ 1) and one
	// of its ships is damaged.
	Repairs = 1,
	// The tavern's captains (0x5D4870): in the town, docked; out of a convoy,
	// without a captain; the guide of a convoy (not marked [+0x136] & 1)
	// stands for the convoy's ships of the merchant without a captain.
	Captains = 3,
};
std::vector<uint16_t> window_ships(const SaveGame& save, int merchant, int town, ShipListMode mode);

// The tavern's window rolls rand() each time it opens (0x5CC479, its method
// 0x5CC120): rand() / 32768 as a float, kept in its [+0x1CA0]. The game's
// rand is the simulation's too (SaveGame::rand_state), so the viewer rolls it
// when the tavern opens.
float tavern_roll(SaveGame& save);
float tavern_roll_of(uint32_t rand_value);
// «There are N sailors in the tavern.» (0x5D03A5..0x5D03ED, and the window's
// limit 0x5D4CB1..0x5D4CF4): what the town lets the merchant hire (0x4F6CA0)
// or, above 50, the roll · 40 + 10.5, truncated (10..50). The amount the
// window sends goes from 0 to N.
uint32_t tavern_sailors(const SaveGame& save, int merchant, int town, float roll);
uint32_t tavern_sailors_shown(uint8_t allowed, float roll);
// 0x5269A0 (on the town, merchant): the captain the tavern offers: the first
// of the town's list ([+0x82E], next [+0x0]) that is free ([+0x8] ≤ 0x20,
// 0x4FE150) and kept for the merchant ([+0xF]); if none, the first free one
// kept for no one ([+0xF] 0xFF); 0xFFFF if none.
uint16_t tavern_captain(const SaveGame& save, int town, uint16_t merchant);

// What the tavern says of a captain (0x5CF880..0x5CFCC0).
struct CaptainInfo {
	uint8_t first_name = 0;  // [+0x2], [+0x3]: the names' indices (game_first_name...).
	uint8_t family_name = 0;
	uint32_t age = 0;  // (the tick − [+0x4]) / 93440: years of 365 days of 256 ticks.
	// His three skills, each / 50 (0..5), in the window's order: [+0xA]
	// (trading: an administrator's), [+0x9] (sailing: the pirates' speed),
	// [+0xB] (fighting: the sea battles).
	uint8_t trading = 0;
	uint8_t sailing = 0;
	uint8_t fighting = 0;
	uint16_t wage = 0;  // [+0xC]: his daily wage.
};
CaptainInfo captain_info(const SaveGame& save, uint16_t captain);

// The ship's window (0x493ED0) acts on a ship docked in the state 0: the
// amount of sailors to dismiss goes from 0 to its sailors ([+0x40]); the
// captain can be dismissed if it has one, isn't in the state 0x12 and its
// [+0x15C] is 0 (0x4940DB..0x494102).
bool ship_window_open(const SaveGame& save, int ship);
bool can_dismiss_captain(const SaveGame& save, int ship);

// The shipyard's repair (0x5F8F50): what it costs (0x52A950: the ship, or
// all the ships of its convoy) and what it sends: 0x1D with its convoy for a
// ship in a convoy (0x5F9327: the window's «whole convoy» [+0xCDF]), 0x03
// with the ship otherwise.
int32_t repair_cost(const SaveGame& save, int town, int ship);
OperationBytes repair_window_operation(const SaveGame& save, int ship);

}  // namespace p3::data
