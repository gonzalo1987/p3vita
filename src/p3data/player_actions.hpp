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

class Sailing;

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
	// The guild's ships to auction (0x504F02, its page 1): none without the merchant's guild bit of the town
	// ([+0x468]); in the town, out of a convoy, docked (state 0 or 1), not marked [+0x136] & 1.
	Auction = 6,
	// 0x504F30 alone: in the town, out of a convoy, docked (state 0 or 1), not marked [+0x136] & 1 (the ships another
	// merchant's office lets the player offer).
	Docked = 7,
	// The tavern's traveller to a town of [+0x2C8] & 1 (0x5D6AE0, 0x504E0B): in the town, docked (state ≤ 1), of the
	// types 0 and 1 ([+0xE] & 2 clear).
	SmallShips = 4,
	// The tavern's pirate (0x5D5430, 0x504E77): in the town, without a captain, out of a convoy, more than a quarter of
	// its hull ([+0x18] > [+0x14] >> 2), docked (state ≤ 1).
	Pirate = 5,
	// The tavern's weapons dealer (0x5D5C60, 0x504FAC): in the town, docked (state ≤ 1), not marked [+0x136] & 1, with
	// a hand weapon above 10 units or room above 10 (0x5182B0); in a convoy, its guide (the merchant's list's ship) for
	// each of the merchant's ships of it, which go on the list, with the guide's weapons and room (the game's).
	WeaponsDealer = 8,
	// The town hall's outrigger (0x5E1A10, 0x505126): in the town, out of a convoy, docked (state ≤ 1), able to lead a
	// convoy (0x519C40), its firepower [+0x120] at least `least` (0x504AC0's fourth argument; signed).
	Guard = 9,
};
std::vector<uint16_t> window_ships(const SaveGame& save, int merchant, int town, ShipListMode mode, int32_t least = 0);

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

// What the side panel's ship menu (CSideMenu_Ship; re/emu/sesion14/ship_menu_notes.md) shows of a ship of
// the player: its crew's daily wage (0x517F70; the crew page writes 7 times it, a week's), the most sailors
// it can take (0x5185C0), the days it still spends at the shipyard in the states 6 and 7 (0x52AAD0 with 0),
// the days to its destination at sea (0x492C2B: in the states 2, 3, 0xF and 0x10, ([+0x48] − the tick) >> 8)
// and the price it shows of each of its 24 cargo slots (0x5187F0: the average price · 200 or 2000, · 10 for
// the weapons, + 0.5, truncated).
uint32_t ship_daily_wage(const SaveGame& save, int ship);
uint32_t ship_most_sailors(const SaveGame& save, int ship);
int32_t ship_shipyard_days(const SaveGame& save, int ship);
int32_t ship_days_to_go(const SaveGame& save, int ship);
std::array<int32_t, 24> ship_cargo_prices(const SaveGame& save, int ship);
// Its firepower as the menu's cannonballs (0x4901C0): with f = sqrt((firepower − 9) · 1.4) + 1 (as a float)
// and n = trunc(2 f) / 2, ftol(n / 5) big balls, at most 5; under 5, also trunc(n − 5 · big) small ones and
// half a small one if what is left is at least 0.5. Nothing with 0 or under 9 (the game's sqrt of a negative).
struct FirepowerBalls {
	int big = 0;
	int small = 0;
	bool half = false;
};
FirepowerBalls firepower_balls(int32_t firepower);
// Its crew page's «Pirate» / «Civil» (0x492A70, 0x494244): shown with a captain; «Pirate» while the ship's
// [+0x15C] is 0 and its state isn't 0x12, «Civil» otherwise; pressed, 0x0D {ship, [+0x15C] == 0}. It can be
// pressed (0x518750) for a ship whose [+0x8] is past the ships' count (the game compares the convoy with
// it), not pursued ([+0x50] and [+0x52] past the ships) and sailing (0xF); in a convoy, if its guide isn't
// pursued and the convoy is the ship's owner's, sailing ([+0x12] 0xF) and with its flag 2; and then only away
// from the towns (0x517260 at the ship's point: no town).
bool can_turn_pirate(const SaveGame& save, const Sailing& sailing, int ship);
OperationBytes pirate_operation(uint32_t ship, bool pirate);

// The sea map's route by points (0x0B, town_tick/ship_operations.cpp fn_538de0): {0x0B, ship, step, x, y}: step 0
// starts the points with (x, y), step k adds the point k, step 0xFFFFFFFF (kSailRoute) sails along them.
constexpr uint32_t kSailRoute = 0xFFFFFFFF;
OperationBytes route_point_operation(uint32_t ship, uint32_t step, int x, int y);

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
// 0x52AFC0 (on the town; the shipyard's side menu, 0x5AE0EC, 0x5AE18A): the tick its shipyard is done with the ships
// of its list up to `ship` (all of them with -1): for each, its work left ([+0x14] − [+0x18]) summed, and 0x100 more
// per point under 28 of [+0x18]; the days the sum takes at the town's [+0x854] · 100 a day, rounded up, one less while
// the day's tick (its low byte) is under the town's number [+0x2C0] · 8 + 7; then from today (the low byte dropped).
// 0 with no ship in the list. A [+0x18] below 0 is set to 0 there, as the game does (0x52B03A): showing the date
// changes the save (the user's word, 2026-11-14: as the original). The game calls it where it shows a shipyard's dates
// (its side menu 0x5ADE2B, 0x5ADEC9, 0x5AE0EC, 0x5AE18A; the shipyard window 0x5F6CAF, 0x5F9ACA).
uint32_t shipyard_done_tick(SaveGame& save, int town, int ship);
bool can_cancel_order(const SaveGame& save, int ship);
// What the page «Building contract» shows for a type (0x5F9570 copies 0x52B2C0's fill into the window; 0x5F5520,
// 0x5F5A60 draw it): whether it can be ordered (its «Execute»), each material's need at the shipyard's level
// (0x66DEB0: timber, cloth, tools, hemp, pitch), what the player has there, what he must buy and its price, the whole
// cost (the page shows it less the materials' prices beside the coin pile, and whole in its total), the days, the
// hold (the page shows it · 10), the big and small guns' slots ([+0x5C], [+0x5D]), the crew and the knots of the type
// (0x673660, 0x673668).
struct ShipOrderInfo {
	bool ok = false;
	int32_t need[5] = {};
	int32_t have[5] = {};
	int32_t buy[5] = {};
	int32_t price[5] = {};
	int32_t cost = 0;
	int32_t days = 0;
	int32_t hold = 0;
	uint8_t big_slots = 0;
	uint8_t small_slots = 0;
	uint8_t crew = 0;
	uint8_t knots = 0;
};
ShipOrderInfo ship_order_info(const SaveGame& save, int town, uint32_t type, int merchant);
// 0x52B0D0: whether the town's shipyard builds ships of the type (the page's «-» and «+» skip the others).
bool shipyard_builds(const SaveGame& save, int town, uint32_t type);
// The page «List of repairs» (0x5FA000): the player's ships in the state 6 (repairing) at the town, walking his list,
// sorted by 0x5FB870 (an exchange sort: each pair swapped when the later one has fewer days left, 0x52AAD0 with 0).
std::vector<uint16_t> shipyard_repairs(const SaveGame& save, int town, int player);
// 0x52AAD0 (on the ship's town): the days a ship spends at the shipyard; `whole` its repair as the page «Repair ship»
// shows it (the flag 1), else what is left.
int32_t shipyard_ship_days(const SaveGame& save, int ship, bool whole);
// «Stop repairs» (0x5FA040): the operation 0x00 {0x00, ship, town} (the sailing order to the town itself).
OperationBytes stop_repairs_operation(uint32_t ship, uint32_t town);
// What an upgrade of `levels` needs of timber, pitch and tools (0x66E030 + 0x18 · type: its [1..3] · levels).
std::array<int32_t, 3> upgrade_needs(int type, int levels);
// 0x672CF0 (two bytes for each 3 · type + level): a ship's small and big guns' slots at a level ([0] small, [1] big;
// the pages «Building contract» and «Improve ship» show them).
std::array<uint8_t, 2> ship_slots(int type, int level);
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

// --- The moneylender (its window, opened at 0x5B2B00 in the town the player
// looks at: take out a loan, pay one off, grant one, the loans granted, buy
// and sell company shares) ---

// A loan's terms: an operation 0x29's 16 bytes after its code (0x539DC0): u32
// [+0x0] the repayment, u8 [+0x4] the weekly interest (in tenths of %), [+0x5]
// the borrower's rank, [+0x6], [+0x7] his names, u16 [+0x8] the amount, u8
// [+0xA] the weeks, [+0xB] the low byte of the loan's tick (whether a
// townsman pays it back), [+0xC] the lender (0xFF: the moneylender), [+0xD]
// the borrower (0xFF: a townsman), [+0xE] the town, [+0xF] the town's slot << 4
// | the interest's kind.
using LoanTerms = std::array<uint8_t, 0x10>;
// 0x51CE80 (on the town, with the window's buffer [+0xF5C], written in place;
// the window calls it when it opens and every frame on its page; `changed`,
// whether a shown field changed): the loans the moneylender offers the
// player, from the town's
// slots 0..2 ([0x9C6 + 6·k]: u8 [+0x3] the weeks, u16 [+0x4] the amount): the
// amount plus his value [+0x46C] / 500 (at most 2000), in fifties, at most half
// his value less his debts [+0x498]; the interest 3000 / √(weeks · amount) + 15
// − his reputation in the town (0x4F9580, 0..30) / 3, and the repayment, the
// amount compounded week by week (at most 65000). They stop at the first that
// doesn't fit. Returns how many, −1 with a bad payment history ([+0x1E]:
// «"Unfortunately, I cannot offer you a loan because of your bad payment
// history."») or −2 when half his value less his debts is under 2000 («with
// your mountain of debts, it is too great a risk»), 0 without a player.
int32_t loan_offers(const SaveGame& save, int town, std::array<LoanTerms, 3>& buffer, bool* changed = nullptr);
// Taking one ({0x29, the offer}, 0x5B5A00): the window lets him while he has
// at most 2 debts (the loans he owes when it opens, plus the ones taken there).
OperationBytes loan_operation(const LoanTerms& terms);
// The loans (events 0x01) in the queue's order, as the window's lists copy
// them: those the merchant owes (0x4D9100, the debtor [+0x11]; the page to pay
// off) and those he granted (0x4D9210, the lender [+0x10]). Their indices.
std::vector<uint16_t> loans_owed(const SaveGame& save, int merchant);
std::vector<uint16_t> loans_granted(const SaveGame& save, int merchant);
// Paying one off (0x5B5B90): {0x2A, the player, its index}. The window lets
// him pay where he has an office or a ship in port (state < 4, not marked
// [+0x136] & 1) or in its town, and with the money ([+0x8], unsigned: «"You do
// not enough cash to pay off the loan."»).
bool can_pay_off_here(const SaveGame& save, int town, uint16_t loan);
OperationBytes pay_off_operation(uint32_t player, uint32_t loan);
// Granting one (only where the player has an office: «You have to be resident
// in this town in order to be able to grant a loan»). 0x51D160 (on the town,
// with the window's buffer, in place; every frame on its page): the townsmen
// who want one, the town's slots 3..5 (u8 [+0x0] the rank, [+0x1], [+0x2] the
// names, [+0x3] the weeks, u16 [+0x4] the amount), the player the lender, the
// interest's kind 2, with their terms (0x51D270, called before the rank, the
// names and the borrower 0xFF are written: on the first frame the buffer still
// holds the offers to take out, so it doesn't roll). Returns how many (0:
// «Nobody in this town wants to take a loan.»). `roll`: the game's rand() for
// 0x51D270 (null: no roll, what the viewer shows).
int32_t loan_requests(const SaveGame& save, int town, std::array<LoanTerms, 3>& buffer, SaveGame* roll,
	bool* changed = nullptr);
// 0x51D270 (cdecl, on the terms): the interest ((kind + 8) · (√(1 / (weeks ·
// amount)) · 300 + 1.2)) and the repayment (compounded; 65000 above that or
// below the amount); [+0xB] 0 or, for a townsman ([+0xD] 0xFF) to repay more
// than (rank + 1) · 5000, with rand() & 0x3FF below 75 · ((the excess + 1250)
// / 1250), (rand() & 0xFF) % 78 + 51: he won't pay it back. The game's
// rand(): with `save` null it doesn't roll (what the viewer shows); the
// window rolls every frame on its page, so the viewer rolls once, when the
// player grants it.
void fn_51d270(LoanTerms& terms, SaveGame* save);
// Granting it (0x5B6000, after «How high should the interest be?»: 0 very low
// .. 4 very high, 2 at first): with the money for the amount (signed:
// «Perhaps you should take out a loan yourself before offering an amount of
// this size.»), {0x29, the terms with the kind in the low nibble of [+0xF]},
// rolled again (0x51D270 with `save`).
OperationBytes grant_loan_operation(SaveGame& save, LoanTerms request, uint32_t kind);

// Company shares: an entry of the window's lists (12 bytes), the operation
// 0x2B's after its code (0x4D98F0): u8 the company (its owner), the shares (%),
// the seller, the buyer (0xFF: the company's pool), u32 the price, u32 what the
// player owns of that company (%, shown; the handler doesn't read it).
using ShareEntry = std::array<uint8_t, 0xC>;
// 0x4D9320 (on the event queue, the player): the pools that offer some
// ([+0x13] > 0) for him to buy, at 6 · the offered · (the company's value /
// 500 + 1) (6 · the offered for a value up to 500). It also drops a second pool
// of the player's company (its [+0x10] 0xFF, [+0x12] 0, which 0x4D98F0 never
// makes): the window calls it when it opens and every frame on its page.
std::vector<ShareEntry> shares_on_sale(SaveGame& save);
// 0x4D9620: what he may sell: first new shares of his own company (with more
// than 55 % his and his pool unchanged for 7 days: 0 to 25 by the tick, at
// most down to 55 %, each at his value / 100), then each of his holdings, what
// the moneylender would buy of it ([+0x13]) at the company's value / 100 + 1
// each.
std::vector<ShareEntry> shares_to_sell(const SaveGame& save);
// {0x2B, the entry}: buying one (0x5B6400, with the money for its price,
// unsigned) or selling one (0x5B66C0, with some shares in it).
OperationBytes shares_operation(const ShareEntry& entry);
// Whether the window lets the player buy it (0x5B6400): his money against its
// price, compared unsigned as the handler does (a game bug: one in debt
// buys); with fix_game_bugs(), signed, as the handler then.
bool can_buy_shares(const SaveGame& save, const ShareEntry& entry);
// When the next dividend is paid (0x5B6584, the message after buying): the
// first tick of the next quarter after now.
uint32_t next_dividend_tick(const SaveGame& save);

// --- Building: the build window (the side menu SIDEMENU_BUILD, *0x6CC95C;
// 0x4797C0 and its neighbours) and the town view's placing (0x590840) ---
//
// The building types are the town's records' ones (0x00..0x38; their names,
// 0x6A57C8 + 4 · type, but the office's, «trading office»); the town view
// works with each type's picture, 0x672FC0[type] (build_catalog).

// The window's five categories ([+0x104C], its tabs [+0xAC + 0xE8 · k]).
enum BuildCategory : int {
	kBuildHousing = 0,  // The warehouse and the houses, or the office.
	kBuildPublic = 1,  // Streets, the well and the public buildings.
	kBuildFortifications = 2,  // The mayor's: towers, gate weapons, the wall.
	kBuildRaw = 3,  // The businesses of the first group (0x66EF58 = 0: farms...).
	kBuildCrafts = 4,  // Those of the second group.
};
// 0x47AB20 (on the window, in the town the view shows, for the player
// [0x6DFC14]): the menu's buttons for a category, their building types
// ([+0xC74], up to 8):
// - housing: in a town without the bits 0xE0 of [0x2C8] only for the mayor
//   with something built ([0x76C] ≠ 0); with an office there (0x5308A0) the
//   warehouse 0x1E and the houses 0x1B, 0x18, 0x15; without one, the office
//   (type 0) if he has a rank there (his [+0x39C + town]) or is the mayor,
//   otherwise nothing and the text «You are not yet able to build a trading
//   office...» ([+0x1060]);
// - public: with the town's bit 0x10 of [0x76C], the streets (2) and the well
//   (0x28); for the mayor the first missing of 0x23, 0x20, 3, 0x21 or, with all
//   of them and the bit 0x10, 0x24, 1 (each if missing) and 0x1F (with
//   [0x76C] & 0x27F = 0x7F); with the bit 0x200, 0x2A ([0x76C] & 0x600 =
//   0x200), 0x2B (if missing), 0x29 and 0x2C;
// - fortifications, the mayor's (otherwise the category becomes 3): with the
//   bit 0x10, by the fortification list [0x768] the port bombard 0x30 (fewer
//   than 3 of 0x30/0x31), the port cannon 0x31 (with a 0x30), the gate bombard
//   0x32 (fewer than 6 of 0x32/0x33), the gate cannon 0x33 (with a 0x32), the
//   pitch shoot 0x34 (with a gate 0x37/0x38); and the wall 0x36 with pieces
//   pending ([0x766] < [0x776]);
// - businesses: the types 4..0x14 of the group (0x66EF58) whose productivity
//   in the town ([0x888 + 0x10 · (type − 4)], u16) is over 400, up to 8; a
//   category over 3 becomes 4.
struct BuildMenu {
	int category = 0;  // The one the window shows after it ([+0x104C]).
	std::array<int32_t, 8> types{};  // −1: empty.
	bool reputation_text = false;
};
BuildMenu build_menu(const SaveGame& save, int town, uint32_t player, int category);
// Which tabs the window enables (read in its frame, 0x479F80..0x47A3E0; the
// game keeps their state from frame to frame, this is the state they settle
// in): housing, public, fortifications, the two groups of businesses.
std::array<bool, 5> build_tabs(const SaveGame& save, int town, uint32_t player);
// 0x672FC0: the town view's picture of each building type (what the 0x21
// carries).
uint8_t build_catalog(int type);

// Choosing a building of the menu (0x47A833..0x47AB13): its costs (0x51F3A0's
// 0x44 bytes, [+0x1118]: [+0x00..] the bricks, timber, tools and hemp it
// needs, [+0x10..] what the player has there, [+0x20..] what is bought, [+0x30]
// their price, [+0x34] the whole cost, [+0x38] the days / 5, [+0x40] the
// money, [+0x41] the materials, [+0x42] whether he may, [+0x43] why not) and
// what the window does with it: the streets (2) and the well (0x28) get a
// button of their own («build street», [+0xC94]: with the cost and planned
// streets left, [0x792] > [0x790]; the well's [+0xD7C]: with the cost and
// [0x789] · 500 < the inhabitants [0x2D4]); the wall (0x36 or more) builds
// the next piece pending (its type [0x766]'s, «build piece of wall» [+0xE64]:
// with the cost and the town not besieged, the bit 0x10 of [0x2C8]); any
// other is placed in the town if 0x51F3A0 allows it.
enum class BuildAction { None, Place, Street, Well, Wall };
struct BuildChoice {
	int type = -1;  // [+0x1070]: for the wall, the next piece's type.
	GameMemory<0x44> cost;
	bool can = false;  // 0x51F3A0.
	BuildAction action = BuildAction::None;
	bool button = false;  // The street's, the well's or the wall's button enabled.
};
BuildChoice build_choice(SaveGame& save, int town, uint32_t player, int type);

// 0x47B8C0 (on the window: town, x, y): whether the building of the window's
// type ([+0x1070], `type`) fits the cell (x, y): the office, the repair dock
// and the shipyard at their fixed points; a business (with 0x4FD020, not out
// of the walls while the town is besieged); the houses and the warehouse in
// either orientation (0x4FD020: 1, 2 or both, then the town's [0x774] > 0
// picks 0xE0); most public buildings by the cell's class (& 0x3F); the towers
// by the fortification list, and the pitch shoot on a gate (0x37 or 0x38, not
// in works), which turns the window's type into 0x34 or 0x35 by the gate. The
// town view's picture it then asks for (0x5928A0 with the picture and a key,
// 0x58CC90) is `ghost` (unchanged when it asks for none): the key's
// orientation is the 0x21's parameter.
struct BuildGhost {
	uint8_t catalog = 0;
	uint32_t key = 0;
};
bool fn_47b8c0(const SaveGame& save, uint32_t town, uint8_t x, uint8_t y, int& type, BuildGhost& ghost);
// The picture the window asks for when the building is chosen (0x47A969..
// 0x47A9FF): the office's with the player's rank in his home town (0x4F74F0),
// the church's (0), the warehouse's (0x20), the towers' (0), the pitch
// shoot's and the others' (0x20 << 16).
BuildGhost build_ghost(const SaveGame& save, uint32_t player, int type);
// The 0x21's parameter from the picture (0x590840): the houses' orientation
// (0x54E5A0: the key >> 16), the warehouse's (its key), a gate's (the key >>
// 16); 0 for the others.
uint8_t build_param(const BuildGhost& ghost);

// The town view's 0x21 (0x590840, when the picture's tint says it fits): {0x21,
// u8 player, town, x, y, the picture, the parameter} and zeros. The handler
// (0x53CE30) takes the picture back to its type from the end of 0x672FC0.
OperationBytes build_operation(uint32_t player, uint32_t town, uint8_t x, uint8_t y, uint8_t catalog, uint8_t param);
// The whole placing of the building `type` at (x, y) with the picture it got
// when chosen: whether it fits (fn_47b8c0) and the 0x21; `type` may change
// (the pitch shoot), and so may `ghost`.
std::optional<OperationBytes> place_operation(const SaveGame& save, uint32_t player, uint32_t town, uint8_t x,
	uint8_t y, int& type, BuildGhost& ghost);

// The window's own buttons (0x4797C0): «build street» {0x25, u32 town, u32
// player}; the well {0x22, u8 player, town, x, y from 0x5250D0, 0x28, 0};
// «build piece of wall» {0x24, u32 player, u32 town, u32 the next piece is a
// gate (its type ≠ 0x36)}; the rest zeros.
OperationBytes street_operation(uint32_t town, uint32_t player);
OperationBytes well_operation(const SaveGame& save, uint32_t town, uint32_t player);
OperationBytes well_bytes(uint32_t player, uint32_t town, uint8_t x, uint8_t y);
OperationBytes wall_operation(uint32_t player, uint32_t town, int next_type);

// 0x47B830 (on the window, the town): where the window's type fits, as the
// town view marks it (0x59D5E0): a map of the grid's cells, one byte per cell
// at y · [0x7A8] + x (the grid itself is x · [0x7AC] + y), 1 on each cell of
// the footprint of a building that fits there: the office at its fixed point
// (0x4FD5E0), the houses and the warehouse in each orientation that fits
// (0x4FD4A0), the others by their cells' class (0x4FD0D0, with the cells
// inside the walls while the town is besieged, but for the fisherman), the
// towers and the pitch shoot by the fortification list (0x5241E0). Empty for
// the streets and the well. Like the game, it clears the town's [0x7A4] (the
// grid changed since).
std::vector<uint8_t> build_marks(SaveGame& save, uint32_t town, int type);

// --- The player's building (the window 0x5AF5C0: 0x5AFEE0 finds it at the
// cell, in his offices' lists [+0x2D0] or the town's works [0x762]) ---
// «Demolish» ([+0xAC]): not on sale (0x4DAFE0) and, in a besieged town (the bit
// 0x10 of [0x2C8]), inside the walls (the cell's bit 0x80); its question «Do
// you really want to demolish this building?» (0x5AF731), then {0x26, u32
// town, u32 merchant, u32 x, u32 y} (0x5AFD80).
bool can_demolish(const SaveGame& save, uint32_t town, uint32_t record);
// The merchant's building whose footprint covers the cell (x, y) (its class's,
// as the marks: the houses and the warehouse by their orientation), among his
// office's lists in the town ([+0x2D0] the houses and the others, [+0x2CE]
// the businesses: the game opens a window for each, 0x5AF5C0 and 0x5A2xxx) and
// his works in the town's [0x762]; its record, or -1.
int own_building_at(const SaveGame& save, uint32_t town, int x, int y, uint32_t merchant);
OperationBytes demolish_operation(uint32_t town, uint32_t merchant, uint32_t x, uint32_t y);
// «Auction» ([+0x194]): as demolish, finished (0x51D6F0 gives 0: here, not in
// works), the player with the guild's licence in the town ([+0x468] bit
// town) and the town's guild hall ([0x76C] & 0x2000); its question with the
// reserve price (0x5AFC50: 0x4DAF10 with the cell's bit 0x80), then {0x28,
// u32 town, u32 merchant, u32 x, u32 y}.
bool can_auction(const SaveGame& save, uint32_t town, uint32_t merchant, uint32_t record);
int32_t auction_reserve_price(const SaveGame& save, uint32_t town, uint32_t record);
OperationBytes auction_operation(uint32_t town, uint32_t merchant, uint32_t x, uint32_t y);

// --- The letters: the side panel's messages (SIDEMENU_INFOS, the letter window *0x6CC960) and its letters'
// settings window (0x408F50, «Configuration of letters») ---
//
// The settings window shows the player's [+0x38..+0x3A] (how long each class of letters is kept: 0 never
// deleted, 1..4 deleted after 40, 20, 10, 5 days, 5 not received; 0x6C1058), [+0x26] (each class's
// notices: 1, 2, 4 play a sound, 8, 0x10, 0x20 show a line) and [+0x3B] (the event sound); on closing it
// queues 0x70 {u16 player, the three days, the event sound, zeros, [+0xE] the notices} (0x4096DB) and then
// 0x71 {player, 1} (0x409735). The letter window queues 0x71 {player, 0} once a tick while it is open
// (0x47CD83: when the tick's low byte isn't the one it last saw).
OperationBytes letter_settings_operation(uint32_t player, const std::array<uint8_t, 3>& days, bool event_sound,
	uint8_t notices);
OperationBytes letters_check_operation(uint32_t player, bool repair);

// --- The top bar's buttons (screenGame.ini BUTTON13 «Population» and BUTTON14 «Own buildings»; the game screen's
// update polls them at 0x423D4A) ---
//
// The man opens the citizens' window (*0x6CBA90, the class 0x66ABA0, «CitizenStats»; its update 0x40B570) of the
// town on screen (the town view's [+0xC324]): each class's residents ([+0x2D8 + 4k]); for the three first, their
// mood ([+0x300 + 2k] as a float against −10.5, −0.5, 9.5, 19.5 and 29.5: 0 annoyed .. 5 very happy, the texts
// 0x699C34 + 4 · mood) and an arrow (that value against twice [+0x308 + 2k]: 0 below, 1 above, 2 equal: none), and
// «Shortage of dwellings!» when a class's residents pass 95 % of its dwellings ([+0x2F8 + 2k]: residents · 100 >
// dwellings · 95, as ints). Its draw (0x40B860): «Citizens in %s», a row per class («%s :» the class's name
// 0x6C1AF8 + 4k, «%u (%s) » its residents and mood, the arrow), «There are %u beggars in the town.» and the
// shortage in red.
struct CitizenStats {
	std::array<uint32_t, 4> residents{};  // [+0xAC + 4k]
	std::array<int16_t, 3> mood{};  // [+0xA4 + 2k]
	std::array<uint8_t, 3> arrow{};  // [+0xBC + k]
	bool shortage = false;  // [+0x1AC]
};
CitizenStats citizen_stats(const SaveGame& save, uint32_t town);
// The house's number (0x42B15E): the player's buildings in the town on screen, his office's [+0x2D2] and [+0x2D4]
// and the office itself (0 without one). Touching the house toggles the town view's [+0x564] (0x59A1E0), the
// shading of the competition's buildings (the manual, figure 4-4: «Edificios»).
uint32_t own_building_count(const SaveGame& save, uint32_t merchant, uint32_t town);

// A town's event symbols (images/Ereignisse/page_glkarte01.bmp, TEX30009: 15 frames of 20 · 20; their tooltips
// GameText::EventFamine..EventBoycott). The town view shows one for each bit of the town's [+0x2C8] in a row at
// its top right (0x58BF3A, 0x5892D0: these bits in this order, from the right; the last two share a place) and
// the sea map one by the town (0x44BFD0) from what the player knows of it (town_situation_known), the first of
// kSeaMapEvents that is set, 20 pixels right of the shortage symbol.
struct TownEventSymbol {
	uint32_t bit;  // In the town's [+0x2C8].
	int frame;  // Of TEX30009.
};
inline constexpr TownEventSymbol kTownViewEvents[8] = {{0x800, 9}, {0x8000, 11}, {0x8, 10}, {0x10, 8}, {0x1000, 6},
	{0x4000000, 14}, {0x400, 13}, {0x200, 12}};
inline constexpr TownEventSymbol kSeaMapEvents[7] = {{0x200, 12}, {0x4000000, 14}, {0x400, 13}, {0x10, 8},
	{0x8000, 11}, {0x8, 10}, {0x800, 9}};

// 0x535300 (on the towns' history 0x6DE588; a town): the town's situation as the local player knows it, which the
// sea map draws (0x44BFD0: its shortage symbol, the famine 0x1000 (frame 6), or a ware short in bits 18..22 (0x1F
// none: the ware symbols, images/Ereignisse/Ware_fehlt.bmp, TEX30040), or the weapons 0x10000 (frame 5); and the
// event symbol, kSeaMapEvents) and its tooltips (0x452973): the town's [+0x2C8] | 0x7E0000 and its entry of the
// history ([+0x40 · town + 4 · day], 0x535450) of the world's day [0x6DE4B4] >> 8 & 0xF, plus 16, less the
// player's lag for the town (the byte of his slot's table, SaveGame::player_towns: his slot is the one of
// [+0xA00..+0xA07] that is the local player), modulo 16; with no slot or table, of the day after.
uint32_t town_situation_known(const SaveGame& save, uint32_t town);

}  // namespace p3::data
