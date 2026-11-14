#pragma once

#include <array>
#include <cstdint>
#include <string>
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
	int32_t other_goods = 0;  // [+0x2E8] / 200 (truncated): «other goods» in barrels.
	int32_t extension = 0;  // [+0x2EE]: the warehouse's extension being built (its loads more; «Extension to»).
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

// --- The office window's pages of information (*0x6E557C; each frame its page's update fills its arrays) ---

// «Stock of goods» (the page 1: update 0x5DBF70, drawing 0x5DA020), a row per ware 0..19: the stock in whole barrels
// or loads ([+0x4 + 4·w] / 200 or 2000, truncated), the week's production (0x500690: [+0xC4 + 4·w] as a float · 7,
// then · 0.005 or 0.0005, each product stored as a float) and consumption ([+0x64 + 4·w] as a float · 0.035 or
// 0.0035). The ware is short when its stock (as a float) is under the week's consumption (its name, stock and
// consumption red then); a business of the office's (its list [+0x2CC], next [+0x8]) with fewer workers ([+0x4]) than
// it wants ([+0xA]) marks the ware it makes (0x672C2C) (its name and production red, the production shown even at 0),
// a fishery (type 5) also the hemp (0x11: the whale oil it makes too is 0x0A; it seems a slip, display only, kept)
// and a cattle farm (type 0xA) the leather (0x0D).
struct OfficeGoodsRow {
	int32_t stock = 0;
	float production = 0.0f;
	float consumption = 0.0f;
	bool short_of = false;
	bool workers = false;
};
std::array<OfficeGoodsRow, 20> office_goods(const SaveGame& save, uint32_t office);

// «Stock of weapons» (the page 2: update 0x5DC190, drawing 0x5DA370): the hand weapons (the wares 20..23,
// [+0x4 + 4·w] / 10, truncated), the six pieces ([+0x124 + 4·k] / their weights 0x672CB4) and the cutlasses ([+0x2BC]).
std::array<int32_t, 11> office_weapons(const SaveGame& save, uint32_t office);

// «Total» (the page 0: update 0x5DC2A0, drawing 0x5DA5B0): the office's week. Each row is its account's sum so far
// this week and last week's (the office's pairs [+0x418] rents, [+0x428] the loans' costs, [+0x438] the land taxes,
// [+0x420] the office's own (negated), [+0x430] the manager, [+0x440] others; 0x5006F0 moves each sum to the second
// word when the week ends), the first turned into a forecast: with d of the town's week gone ((now − town · 8 − 7) /
// 256 mod 7), the sum / (d + 1) · 7, or, at d = 0, the sum · 7 (last week's when the sum is 0); the land taxes by the
// world's week instead ((now / 256) mod 7: the sum / d · 7); the manager's sum plus his wage for the 6 − d days left
// (with one). The totals: the rents and the office's own added, the loans, taxes and others taken away (the manager's
// row isn't in them: a game slip, display only, kept). Then the buildings: businesses [+0x2D2] and their workers
// [+0x448] (of [+0x44A]), dwellings [+0x2D4] and their tenants ([+0x2DE] + [+0x2E0] + [+0x2E2], of [+0x2D8] + [+0x2DA]
// + [+0x2DC]); the warning when [+0x2] (some business short of raw materials last week); the manager's stage (his
// captain's [+0xA] / 43) and wage per day (0x500F10: his [+0xC] + the businesses; without him the businesses + 10).
struct OfficeBalance {
	std::array<int32_t, 6> forecast{};
	std::array<int32_t, 6> last{};
	int32_t forecast_total = 0;
	int32_t last_total = 0;
	uint32_t from_tick = 0;  // Last week's first and last days (the banner's «%i. %s to %i. %s»).
	uint32_t to_tick = 0;
	int businesses = 0, workers = 0, most_workers = 0, dwellings = 0, tenants = 0, most_tenants = 0;
	bool short_of_materials = false;
	bool manager = false;
	int stage = 0;
	int wage = 0;
};
OfficeBalance office_balance(const SaveGame& save, uint32_t office);

// «Move» (the page 6: update 0x5DD520, drawing 0x5DBCD0): the fees, the merchant's [+0x46C] (as a double) · 0.1327
// (0x679DE0) truncated, at most 1000000 (compared unsigned); the button queues {0x3E, player, town, fees} when his
// money [+0x0] covers them (signed).
uint32_t home_move_fees(const SaveGame& save, uint32_t merchant);
OperationBytes home_move_operation(uint32_t player, uint32_t town, uint32_t fees);

// The game's «%10.1f» (MSVC's printf: the value to 17 significant digits, then its tenths rounded half up), without
// its padding.
std::string game_tenths(double value);

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
