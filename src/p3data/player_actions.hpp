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
#include <string>
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

// --- The market window with the player's office ---
// The window keeps two sides, each a ware's units, what it holds and its
// room: in mode 0 the ship (0x4AA9D0, which writes the first side in the
// modes 0 and 5) and the office (0x4AA7C0); in mode 2 the town (0x4AAB10) and
// the office, its room its [+0x2EC] · 2000 (0x4A97B6). The office's units are
// its stock / 200 or / 2000 (the weapons / 10); what it holds, all its stock
// and its [+0x2E8] rounded up to 200, in steps of 200.
int32_t market_office_units(const SaveGame& save, int office, int ware);
int32_t market_office_used(const SaveGame& save, int office);
int32_t market_office_capacity(const SaveGame& save, int office);
// Mode 0 (a ship and the office, 0x4A921F..0x4A93F0): 0x08 {0x08, amount,
// u16 ship, u16 ware, u16 player, u8 direction}. From the office into the
// ship (direction 1) as buying (the office's units, the ship's room); from
// the ship (0) as selling. std::nullopt when the window sends nothing (or
// the player has no office in the ship's town).
std::optional<OperationBytes> office_to_ship_operation(const SaveGame& save, int ship, int ware, uint32_t step);
std::optional<OperationBytes> ship_to_office_operation(const SaveGame& save, int ship, int ware, uint32_t step);
// Mode 2 (the office and the town): 0x59 buying for the office (the town's
// units, the office's room) and 0x58 selling from it (the office's units),
// {code, amount, u16 ware, u16 merchant ([+0xF4]), u16 player, u16 town}.
std::optional<OperationBytes> office_buy_operation(const SaveGame& save, int town, int ware, uint32_t step);
std::optional<OperationBytes> office_sell_operation(const SaveGame& save, int town, int ware, uint32_t step);
// The player's office in a town (0x5308A0), or -1.
int player_office(const SaveGame& save, int town);

// Mode 5 (a ship and the office: the weapons and the artillery,
// 0x4A8F0C..0x4A91B6), 11 rows. Rows 0..3 are the weapons, the wares
// 0x14..0x17 (0x69854C), sent as 0x08 in pieces of 10: from the office its
// units (stock / 10) cut to the ship's room (its capacity less what it holds,
// in steps of 200, / 10); from the ship its units (cargo / 10). Rows 4..10
// send 0x09 with the type row − 5 (row 4, 0xFFFFFFFF, the hand weapons
// [+0x154]) and the count of the side it leaves: the ship's hand weapons and
// its slots of each type (0x4A8B3C), the office's [+0x2BC] and each type's
// weight / its piece's (0x4A8B7E).
int32_t weapons_to_ship_pieces(uint32_t step, int32_t office_units, int32_t capacity, int32_t used);
int32_t ship_artillery_count(const SaveGame& save, int ship, int type);  // type −1: the hand weapons.
int32_t office_artillery_count(const SaveGame& save, int office, int type);
// {0x09, type, town | (from the ship: 0x80000000), ship, count}.
OperationBytes artillery_bytes(int type, int town, int ship, uint32_t count, bool from_ship);
// The weapons rows (ware 0x14..0x17). A game bug: with nothing to move, the
// window jumps into the artillery rows with the weapon's row (0x4A8F99,
// 0x4A903A) and, from the office, sends 0x09 with the type row − 5 (the hand
// weapons) and the count of the ship's pieces of the type row + 2 (its table
// read 0x1C on); from the ship it would read fields of the window nothing
// writes (assumed 0: nothing). Fixed behind fix_game_bugs(): nothing.
std::optional<OperationBytes> weapons_to_ship_operation(const SaveGame& save, int ship, int ware, uint32_t step);
std::optional<OperationBytes> weapons_to_office_operation(const SaveGame& save, int ship, int ware, uint32_t step);
// The artillery rows (type −1..5).
std::optional<OperationBytes> artillery_operation(const SaveGame& save, int ship, int type, uint32_t step, bool from_ship);
// The same from the window's values (player_ops): PIECES, the ship's pieces of
// the type row + 2 (the bug's count).
std::optional<OperationBytes> weapons_to_ship_bytes(uint32_t step, int32_t office_units, int ware, int32_t capacity,
	int32_t used, int ship, uint16_t player, int town, int32_t pieces);
std::optional<OperationBytes> weapons_to_office_bytes(uint32_t step, int32_t ship_units, int ware, int ship, uint16_t player);
std::optional<OperationBytes> artillery_row_bytes(uint32_t step, int32_t units, int type, int town, int ship, bool from_ship);

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
	// The shipyard's upgrades (0x5FA320): in the town, out of a convoy,
	// docked, below the level 2 ([+0xF]), not marked [+0x136] & 1.
	Upgrades = 2,
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

// --- The shipyard's other pages (0x5F8F50) ---
// Order a ship (0x5F9570): {0x2C, type ([+0xC80]), the player, town
// ([+0xCB8]), 0}; whether the merchant can order it there and what it costs
// (0x52B2C0, which the page shows).
OperationBytes order_ship_operation(uint32_t type, uint32_t player, uint32_t town);
bool ship_order_cost(const SaveGame& save, int town, uint32_t type, int merchant, int32_t& cost);
// The ships ordered at the town's shipyard (its list [+0x81C], next [+0x6]);
// the page lists them all. Cancel one (0x5F9B00): {0x2F, the player, town,
// ship}, only if its work hasn't started ([+0x18] ≤ 0; otherwise the page
// says «Work is already being done on this ship.» and sends nothing).
std::vector<uint16_t> shipyard_orders(const SaveGame& save, int town);
bool can_cancel_order(const SaveGame& save, int ship);
OperationBytes cancel_order_operation(uint32_t player, uint32_t town, uint32_t ship);
// Upgrade a ship (0x5FA320): the ships of 0x504AC0's mode 2 (in the town,
// out of a convoy, docked, below the level 2, not marked [+0x136] & 1); the
// level to reach goes from its level [+0xF] + 1 to 2 ([+0xC88]), and that
// level is what it sends: {0x0C, ship ([+0xC84]), level} (0x508660 adds
// that many levels, cut down to the level 2: the difference). What it costs
// (0x52B670 with the difference), in the game's 0x44 bytes: [+0x00..] the
// timber, pitch and tools the player has there, [+0x0C..] used, [+0x18..]
// bought, [+0x24..] their prices, [+0x30] the total, [+0x34] the days
// (without the repair's), [+0x38] the hold left in loads, [+0x3C] the loads
// it takes, [+0x40], [+0x41] the slots; and whether it can be done.
using ShipUpgrade = GameMemory<0x44>;
bool upgrade_cost(const SaveGame& save, int town, int ship, int level, ShipUpgrade& out);
OperationBytes upgrade_operation(uint32_t ship, uint32_t level);
// Rename a ship (0x5FABE0, the ships of 0x504AC0's mode 0): the name in
// pieces of 12 characters, the first with 0x2D and the others with 0x2E:
// {code, the piece (its 0 after it, if shorter), ship ([+0xC84])}.
std::vector<OperationBytes> rename_operations(uint32_t ship, const std::string& name);

// --- Convoys (the ship's panel: the convoy window 0x40C170, 0x494B80,
// 0x494AF0, 0x494DC0) ---
// What the window offers for the ships the player selected (in the town of
// the first): kPrivateConvoy if one of them can guide a convoy (0x519C40),
// then kPublicConvoy if the player has the guild's licence ([+0x468]: in a
// town with the bit 0x80 of [0x2C8], that town's bit; otherwise any), then
// each convoy of the town (state 0, 1, 9 or 0xA) the ships may join: one
// open to others, if one of the ships has its sailors (0x673660) and more
// than a fifth of its hull; one of an event (0x5039F0 for the first ship);
// or a convoy of one owner, the player's. Not a convoy whose destination
// takes no big ships (the bit 1 of [0x2C8]) if one of the ships is big (type
// ≥ 2). Empty: «Convoy cannot be formed».
constexpr int32_t kPrivateConvoy = -1;
constexpr int32_t kPublicConvoy = -2;
std::vector<int32_t> convoy_choices(const SaveGame& save, const std::vector<uint16_t>& selected);
// A new convoy (0x494B80): {0x15, its guide, 1 (private) or 0 (public), 5,
// the destination (0 if private)}. With one ship selected ([+0xB4] =
// [+0xBC]) the guide is that ship; with several, the first of the most
// firepower [+0x120] that can guide, and then each of the others joins it
// ({0x18, guide, ship}).
std::vector<OperationBytes> create_convoy_operations(const SaveGame& save, const std::vector<uint16_t>& selected,
	bool single, bool is_public, uint32_t destination);
OperationBytes create_convoy_bytes(uint32_t guide, bool is_public, uint32_t destination);
// The destinations a public convoy may take (0x40CA8E): the towns but its
// own and, with a big ship, those with the bit 1 of [0x2C8].
std::vector<uint8_t> public_convoy_towns(const SaveGame& save, const std::vector<uint16_t>& selected);
// Joining a convoy (0x494AF0): {0x18, the convoy's guide [+0x10], ship} for
// each selected ship.
std::vector<OperationBytes> join_convoy_operations(const SaveGame& save, uint32_t convoy, const std::vector<uint16_t>& selected);
// Leaving (0x494DC0, the answer 0x4950D0): with the convoy selected, or a
// ship that guides it, {0x16, its convoy}: «Should the convoy %s be
// disbanded?»; with another ship of it, {0x17, ship}: «Should the ship %s
// leave the convoy?».
OperationBytes leave_convoy_operation(const SaveGame& save, uint16_t ship, bool convoy_selected);
OperationBytes leave_convoy_bytes(uint32_t ship, uint32_t convoy, bool guide, bool convoy_selected);

// --- The market window with a convoy (its modes 4 and 3) ---
// What the window keeps of the convoy (0x4AA4A0): of the player's ships in
// it, each ware's units (the cargo summed, / 200 or / 2000), what they hold
// in steps of 200 (each ship's [+0x118] + [+0x11C] rounded up to 200) and
// their capacity [+0x10].
int32_t market_convoy_units(const SaveGame& save, int convoy, int ware);
int32_t market_convoy_used(const SaveGame& save, int convoy);
int32_t market_convoy_capacity(const SaveGame& save, int convoy);
// Mode 4 (a convoy and the town, as mode 1): 0x1A buying and 0x19 selling,
// {code, amount, u16 ware, u16 convoy, u16 player, u16 town}.
std::optional<OperationBytes> convoy_buy_operation(const SaveGame& save, int convoy, int ware, uint32_t step);
std::optional<OperationBytes> convoy_sell_operation(const SaveGame& save, int convoy, int ware, uint32_t step);
// Mode 3 (a convoy and the office, as mode 0): 0x1B {0x1B, amount, u16
// convoy, u16 ware, u16 player, u8 direction: 1 from the office}.
std::optional<OperationBytes> office_to_convoy_operation(const SaveGame& save, int convoy, int ware, uint32_t step);
std::optional<OperationBytes> convoy_to_office_operation(const SaveGame& save, int convoy, int ware, uint32_t step);

}  // namespace p3::data
