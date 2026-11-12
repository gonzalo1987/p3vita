#pragma once

#include <cstdint>
#include <vector>

#include "p3data/player_actions.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// What the office's windows show and the operations they queue (the rules; the viewer draws them its way): the
// warehouse page (0x5DCA70: the storage, the guards), the administrator (0x5DC2A0: hire, dismiss) and his orders per
// ware (0x5DCDB0), and the windows of the player's own buildings in the town: a business's share of jobs (0x5A1F80)
// and a house's rent (0x5AF9xx). The operations are the engine's (town_tick/operations.cpp). Notes:
// re/emu/sesion27/guild_notes.md.

// --- The warehouse page (0x5DCA70) ---

struct OfficeStorage {
	int32_t used = 0;  // In loads: (each ware's stock in whole barrels or loads + [+0x2E8] in whole 200s + 1800) / 2000.
	int32_t capacity = 0;  // [+0x2EC], in loads.
	int32_t over = 0;  // used − capacity, or 0.
	int32_t guards = 0;  // [+0x2C7].
	// (capacity − 0x6734E4 (50)) / 0x6734E5 (200) + 1, as an s16.
	int32_t max_guards = 0;
	// The thieves' chance shown (0..3): above 50 loads used, ((max · 1000) / (guards + 1) − 1000) · (used − 50) / 200:
	// ≤ 0 → 0, ≤ 340 → 1, ≤ 670 → 2, more → 3; 0 up to 50 loads.
	int risk = 0;
};
OfficeStorage office_storage(const SaveGame& save, uint32_t office);
// The buttons (0x5DCC6D, 0x5DCCC7): one guard less (with some) or more (below the most): {0x63, player, town, the
// guards then}.
OperationBytes guards_operation(uint32_t player, uint32_t town, uint32_t guards);

// --- The administrator (0x5DC2A0) ---

bool has_administrator(const SaveGame& save, uint32_t office);
// Its button (0x5DC88C): without one, {0x5E, player, town, 0} (hire); with one, first his orders of the wares 0..19
// cleared ({0x5B, 0, 0, the office, ware}, 20 of them) and then {0x5E, player, town, 1} (dismiss).
std::vector<OperationBytes> administrator_operations(const SaveGame& save, uint32_t player, uint32_t town,
	uint32_t office);
// His order for one of the wares 0..19 (0x5DCDB0): {0x5B, the stock to keep, the price (< 0 buy at most at it, > 0 sell
// at least at it, 0 neither), the office, ware}; the merchant's last prices ([+0x5A8 + 4·w] buying, [+0x5AA + 4·w]
// selling: 0x5DD233, 0x5DD2CF) start the toggles. {0x66, ware, player, town, on}: the stock to keep is kept from the
// ships of the trade routes (the office's [+0x3B4], 0x500EC0).
OperationBytes administrator_order_operation(int32_t stock, int32_t price, uint32_t office, uint32_t ware);
OperationBytes reserve_operation(uint32_t ware, uint32_t player, uint32_t town, bool on);
int32_t last_order_price(const SaveGame& save, uint32_t merchant, uint32_t ware, bool buying);

// --- The player's own buildings (the windows of a business, 0x5A0C30, and of a house) ---

// A house's class by its type (0x5AFA44: 0x18..0x1A 1, 0x1B..0x1D 2, the rest 0), and its rent level 0..4 (the
// merchant's office in the town, [+0x2E4 + class]; −1 without an office). Its buttons: one less (above 0) or one more
// (below 4): {0x62, player, town, the level then, class}.
int house_class(uint8_t type);
int house_rent(const SaveGame& save, uint32_t town, uint32_t merchant, uint8_t type);
OperationBytes rent_operation(uint32_t player, uint32_t town, int level, int house_class);
// A business's share of its jobs in % (its world record's [+0x10] · 100, rounded; the window's slider, 0..100), and
// its operation {0x5A, merchant, town, type, percent} (0x5A2233: on each change of the slider). −1 for a building
// that isn't a business of the merchant's office in the town.
int business_share(const SaveGame& save, uint32_t town, uint32_t merchant, uint8_t type);
OperationBytes business_share_operation(uint32_t merchant, uint32_t town, uint8_t type, int percent);

}  // namespace p3::data
