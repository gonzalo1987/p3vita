#pragma once

#include <array>
#include <cstdint>

#include "p3data/player_actions.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// What the windows of some of the town's buildings show and the operations they queue (the rules; the viewer draws
// them its way): the church (.let 0x04, its interior *0x6E54F0) and the bath house (.let 0x09, *0x6E54C8). The
// operations themselves are the engine's (core.cpp, operations.cpp). Notes: re/emu/sesion26/letters_actions_notes.md.

// --- The church ---

// 0x4FE360 (on the church, the town's [+0x794]): how well the town's poor are fed, 0..5: ([+0x0] + unit / 2) / unit
// with unit = (the church factor, the world's [+0x8C], + 1) · 2000, at most 5.
int church_feeding_level(const SaveGame& save, uint32_t town);
// 0x4FE390 (on the church): its extension: 2 at the last level (3), 0 with its money gathered ([+0x4] ≥ the cost),
// −1 open to donations (100 days after the last extension, [+0x8] > 100), 1 not yet.
int church_extension_state(const SaveGame& save, uint32_t town);
// What the extension at the church's level costs (as 0x4FE420: the factor · 10000, 15000 or 20000, + 0x6734B4).
int32_t church_extension_cost(const SaveGame& save, uint32_t town);

// The feeding window's five rows (0x5CAAB0): grain, meat, fish, beer and wine, in barrels or loads.
inline constexpr std::array<uint8_t, 5> kFeedingWares = {0, 1, 2, 3, 7};
// Its kind (0x5CAF71..0x5CB116), the operation's [+0x6]: the worth of the rows (what the town would pay for them,
// 0x52E1D0; the fifth row priced as the ware 4, salt, though the operation gives wine: a game bug, fixed behind
// fix_game_bugs(): priced as wine) divided by
// ftol(sqrt(s16 [+0x304] · [+0x2D4] / 18)) + 8, at most 255.
uint8_t church_feeding_kind(const SaveGame& save, uint32_t town, const std::array<uint16_t, 5>& amounts);
// The operation 0x30 the window queues (0x5CB102): {0x30, u8 player, u8 town, u8 kind, -, u16 the rows grain, beer,
// fish, meat, wine}.
OperationBytes feeding_operation(const SaveGame& save, uint32_t player, uint32_t town,
	const std::array<uint16_t, 5>& amounts);
// The donations: 0x32 to the church (0x5CB419) and 0x31 for its extension (0x5CB811): {code, player, town, amount}.
OperationBytes church_donation_operation(uint32_t code, uint32_t player, uint32_t town, int32_t amount);

// --- The bath house ---

// The merchant's last bath (his [+0x4], the tick) as the window says it (0x5B1FC5): 0 within 6 days (0x600 ticks,
// the tick's low byte dropped), 1 within 21 (0x1500), 2 longer ago.
int bath_state(const SaveGame& save, uint32_t merchant);
// The operation 0x39 {0x39, player, the tick}: the window queues it when the player who took a bath goes on
// (0x5B19F1, 0x5B1E93: the tick then).
OperationBytes bath_operation(uint32_t player, uint32_t tick);
// 0x529E20 (on the town: merchant, slot): what the councillor of the slot (0..3) asks for a bribe, rolling the game's
// rand(): (rand() % 11 + 4 · f + 16) · 500, f by the merchant's rank in his home town (0, 1, 2, 3, 5, 7, 10, 15 for
// 0..7; past 7 the byte it leaves is the merchant's own number, its argument's slot), twice that if the slot is
// already some merchant's; 0x80000000 for a merchant past the count or a slot past 3 (no roll).
uint32_t councillor_price(SaveGame& save, uint32_t town, uint32_t merchant, uint32_t slot);
// The bribe window's button (0x5B2589): the councillor's price (0x529E20, rolling); an offer at least that much
// queues 0x42 {0x42, slot, player, offer, town}, a smaller one 0x43 {0x43, slot, player, 0, town}.
OperationBytes bribe_operation(SaveGame& save, uint32_t town, uint32_t player, uint32_t slot, uint32_t offer);

}  // namespace p3::data
