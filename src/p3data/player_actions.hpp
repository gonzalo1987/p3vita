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

}  // namespace p3::data
