#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "p3data/player_actions.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// What the tavern shows and queues (the rules; the viewer draws them its way). The tavern's side menu (*0x6E54F8, its
// update 0x5A66C0) shows the people there (0x5A6E60) as up to ten buttons, one under the other; choosing one opens his
// page in the tavern's window (*0x6E5574: 0x5CED00 sets the page; its update 0x5CD540 and its drawing 0x5CDCDC go by
// the page) and queues the operation 0x52 twice: the one talked to before left (0x5A7A60, also when there was none: its
// check of 0xFF never holds) and the new one taken (0x5A7BA0). Closing the menu queues 0x52 with the kind 10 (0x5A6590:
// all of the player's left). The engine's 0x52 is fn_53c2c0 (town_tick/operations.cpp).
enum class TavernPerson : int {
	OrderClient = 0,  // the client of a town hall order the player took (0x526880; his page 0x5D7760 queues 0x51)
	Trader,           // 0x5265E0 (not looked at with an order's client); his page 0x5D6FC0 queues 0x4D
	Sailors,          // 0x4F6CA0 above 0; 0x5D4C10
	Captain,          // 0x5269A0 below the captains; 0x5D4870
	Informer,         // the town's [+0x82C] below 0x14; 0x5D5970 queues the operation the town stored
	Traveller,        // 0x5261A0; 0x5D6AE0 queues 0x53
	WeaponsDealer,    // 0x526280; 0x5D5C60 queues 0x50
	Burglar,          // 0x526120; 0x5D68E0 queues 0x4F
	Pirate,           // 0x5261D0 below the captains; 0x5D5430 queues 0x4E
	MissionLetter,    // an event 0x1B of the mission scripts for the player (0x5D8100): the mission interpreter's
};
constexpr int kTavernPeople = 10;

// 0x5A6E60: who is there in the town's tavern, as the player sees it. The mission letter (the list 0x6DD730 of the
// mission scripts) is the mission interpreter's: never here. (The menu first sets the one talked to, unless the
// sailors or the informer, but 0x5A6E60 clears all ten before it looks: it has no effect.)
std::array<bool, kTavernPeople> tavern_people(const SaveGame& save, uint32_t town);

// Who is there, as the town's functions say (thiscall on the town in the game; [0x6DFC14] the player).
// 0x526880: one of the town's two order places [+0x830], [+0x832] holds an event (below the events) whose [+0x16],
// the merchant who took the order, is the player.
bool tavern_order_client(const SaveGame& save, uint32_t town);
// 0x5265E0: the trader: the town's [+0x83E] free (not below the merchants) or the player, and a ware of the mask
// 0x47FAF (the wares 0..19 but grain, meat, wine, ... : 0, 1, 2, 3, 5, 7..14, 18) whose stock [+0x4 + 4·ware] is above
// [+0x4F4 + 0x10·ware] + 40000 (signed).
bool tavern_trader(const SaveGame& save, uint32_t town);
// 0x5261A0: the traveller: the town's [+0x83F] free (0xFF) or the player.
bool tavern_traveller(const SaveGame& save, uint32_t town);
// 0x526280: the weapons dealer: the town's [+0x83D] free (0xFF) or the player; but when the town isn't besieged
// ([+0x2C8] & 0x10), the town [+0x2C0]'s [+0x874] is above 0 and no hand weapon (the wares 0x14..0x17) has 10 units
// in the town, only the player.
bool tavern_weapons_dealer(const SaveGame& save, uint32_t town);
// 0x526120: the burglar: the town's [+0x83C] the player; or free (0xFF) when the town's first office ([+0x784]) has a
// next one ([+0x2CA] below the offices). (The game reads [0x2CA] of nothing for a town without offices: here none.)
bool tavern_burglar(const SaveGame& save, uint32_t town);

// The operation 0x52 {0x52, the draw, merchant, town, kind}: `merchant` the player taking the person `kind`, or the
// merchants' count when he leaves him. The draw (the game's rand(), 0x639C03): the kind 3, (rand() << 16) + the
// captain to hire (0x5269A0 with the player); 8, (rand() << 16) + the pirate (0x5261D0); 6 and 7, rand(); else 0.
OperationBytes tavern_talk_operation(SaveGame& save, uint32_t merchant, uint32_t town, uint32_t kind);
// 0x5A6590, the tavern closed: {0x52, 0xFFFFFFFF, player, town, 10}.
OperationBytes tavern_leave_operation(uint32_t player, uint32_t town);

// A greeting's variant when the window opens (0x5CCE4B, 0x5CCEA1): rand() / 32768.0 · 3, truncated (0..2).
int tavern_greeting(SaveGame& save);

// --- The trader (the page 1: update 0x5D6FC0, drawing 0x5D2DC0, the page after it 0x5D36A0) ---

// The 16 bytes 0x526650 makes on the town when the tavern's window opens (0x5CCE96) and when the page opens without
// one (0x5CF5C9); the window queues them as the operation 0x4D (trade.cpp fn_53b5e0).
struct TraderOffer {
	uint32_t tick = 0;     // [+0x0] the world's tick
	int32_t price = 0;     // [+0x4] for all of it
	uint16_t amount = 0;   // [+0x8] in barrels or loads (at least 1)
	uint16_t ship = 0;     // [+0xA] 0xFFFF; the window puts the destination (past the ships: the office)
	uint8_t seller = 0;    // [+0xC]
	uint8_t buyer = 0;     // [+0xD] the player; the window again
	uint8_t ware = 0;      // [+0xE]
	uint8_t town = 0;      // [+0xF] not set (the heap's); the window puts its town
	int32_t units() const;  // the amount · 200 or 2000 (the window's [+0x1C98])
};
// 0x526650: the ware of the mask 0x47FAF with the largest stock − [+0x4F4 + 0x10·ware] − 40000 above 0 (none: no
// offer). The seller: the owner of the first ship of the docked list (0x6DD888, next [+0x6]) in the town whose owner
// is a merchant from another home town ([+0x19]) with the flag [+0x8] & 4, the most his [+0x10]; but if there is none,
// or that ship is the last of the list (the game looks at the next one's index), the town's own merchant (the town's
// [+0x2C0]) and 30000. The most cut to the stock − [+0x4F4 + 0x10·ware], in whole barrels or loads (at least one); the
// price the half (an arithmetic shift) of the town's sell price (0x52E1D0) plus its buy price (0x52E430) of them.
std::optional<TraderOffer> trader_offer(const SaveGame& save, uint32_t town);
// 0x5D7DE0 (the window, units): the player's ships (his list [+0xE], next [+0x4]) in the town at anchor ([+0x134]
// below 4) with room (0x5182B0) for the units; a convoy by its guide only, with the room of all its ships, all of them
// his.
std::vector<uint16_t> trader_ships(const SaveGame& save, uint32_t town, int32_t units);

// The trader's window ([+0x1C30] .. [+0x1CAB], reset when the page opens: 0x5CED00, 0x5CF59C).
struct TraderWindow {
	std::optional<TraderOffer> offer;  // [+0x2170]
	int greeting = 0;          // [+0x1CA5]
	uint16_t ship = 0xFFFF;    // [+0x1C30] the chosen one (the ships' count: the office)
	bool office = false;       // [+0x1CA6] the office chosen
	bool confirmed = false;    // [+0x1CA9] accepted: now where to
	bool afforded = false;     // [+0x1CA7] the money was enough
	bool placed = false;       // [+0x1CA8] bought at once
	bool no_place = false;     // [+0x1CAB] neither an office nor a ship
	bool convoy = false;       // [+0x1CAA] bought at once into a convoy
	bool done = false;         // the page after it (0xF)
};
// The page opened (0x5CF59C): the offer if none, the first ship, the flags off.
void trader_open(const SaveGame& save, uint32_t town, TraderWindow& w);
// Each update (0x5D701A..0x5D7074): the list again; a list that was empty gives its first ship.
void trader_refresh(const SaveGame& save, uint32_t town, TraderWindow& w, const std::vector<uint16_t>& before,
	const std::vector<uint16_t>& now);
// The two arrows (0x5D70A3): on with an office and one ship, or more than one ship.
bool trader_arrows(const SaveGame& save, uint32_t town, const std::vector<uint16_t>& ships);
// An arrow (-1 the left one, 0x5D70EA; +1 the right one, 0x5D722A): the ships in turn, the office (if any) between the
// last and the first.
void trader_turn(const SaveGame& save, uint32_t town, TraderWindow& w, const std::vector<uint16_t>& ships, int step);
// The act button (0x5D7384): «Accept offer», then «Load goods». The first time, with the money (signed, [+0x0] ≥ the
// price): the office if any; bought at once into the only place (the office alone, or one ship without an office),
// else where to; without money the page after it. Accepted: with the money the operation 0x4D to the chosen place;
// the page after it.
std::optional<OperationBytes> trader_press(const SaveGame& save, uint32_t town, TraderWindow& w,
	const std::vector<uint16_t>& ships);
// The operation 0x4D: {0x4D, the 16 bytes} with the player, the town and the place (ship, or the ships' count).
OperationBytes trader_operation(const TraderOffer& offer, uint32_t player, uint32_t town, uint16_t ship);
// The arrows of the trader's and the weapons dealer's windows (the same code): the ships in turn, the office (if any)
// between the last and the first.
void tavern_turn(const SaveGame& save, uint32_t town, bool& office, uint16_t& ship, const std::vector<uint16_t>& ships,
	int step);

// --- The weapons dealer (the page 6: update 0x5D5C60, drawing 0x5D14A0) ---
// Four rows, the hand weapons (the wares 0x14..0x17), each with the town's count, a button to buy at the price of one,
// the place's count and a button to sell at the price of one; the step (1, 5, 50, all: [+0x1CA4]) and the place (the
// ships of 0x504AC0's kind 8, ShipListMode::WeaponsDealer, and the office, as the trader's arrows; without ships the
// office, without either the buttons off). Each button queues 0x50 at once.

// 0x526470 (on the town: step, &town's, &sell, &buy, &have; each four): for each weapon k the town's count (its
// [+0x54 + 4k] / 10), the price of one bought for the units of the step (10, 50, 500; all: the town's whole tens) by
// 0x5263D0, and the price of one sold for the step's units (all: have · 10) by 0x526350's sums: (the town's units +
// half the units) · 0x673784[k] / 50 (a town of [+0x2D4] below 500) or · 10 / [+0x2D4], from 0x673780[k], at least
// 0x673788[k]; nothing sold, 0. 0x5263D0: the town without 10 units, 0; else (its units − half of those taken, at
// most its units) · 0x673794[k] the same way, from 0x67378C[k], at least 0x67379C[k].
struct DealerPrices {
	std::array<int32_t, 4> town{};
	std::array<int32_t, 4> sell{};
	std::array<int32_t, 4> buy{};
};
DealerPrices fn_526470(const SaveGame& save, uint32_t town, uint32_t step, const std::array<int32_t, 4>& have);
// The place's count of each weapon (the drawing's [+0x1C00]): the office's [+0x54 + 4k] or the ship's [+0xA4 + 4k],
// / 10.
std::array<int32_t, 4> dealer_have(const SaveGame& save, uint32_t town, bool office, uint16_t ship);
// A button pressed (0x5D65ED: buy; 0x5D66A7: sell): {0x50, the price of one, how many, town, player, kind, buy, the
// place (the ships' count: the office), 0}. Buying the step's 1, 5, 50 or the town's count; selling 1, or the place's
// count now (at most 5 or 50, compared unsigned). Selling with none, the price 0.
OperationBytes dealer_operation(const SaveGame& save, uint32_t town, uint32_t kind, bool buy, uint32_t step, bool office,
	uint16_t ship);

// --- The traveller (the page 5: update 0x5D6AE0, drawing 0x5D22E0, the page after it 0x5D2BC0) ---
// He wants to go to the town's [+0x834] by the tick [+0x838] and pays [+0x836] (the event 0x10 pays it, less when
// late). His page closes when he isn't there (0x5261A0) or the destination is past the towns.
struct TravellerJob {
	uint8_t destination = 0;
	uint16_t payment = 0;
	uint32_t deadline = 0;
	uint8_t day = 0;     // the deadline's date (0x5311D0)
	uint8_t month = 0;   // 1..12
	uint16_t year = 0;
};
TravellerJob traveller_job(const SaveGame& save, uint32_t town);
// The ships offered (0x5D6B49): 0x504AC0's kind 0 (ShipListMode::Sailors), or its kind 4 (ShipListMode::SmallShips)
// for a destination with [+0x2C8] & 1.
std::vector<uint16_t> traveller_ships(const SaveGame& save, uint32_t town);
struct TravellerWindow {
	uint16_t ship = 0xFFFF;  // [+0x1C30]
	bool late = false;       // [+0x1CAE]: accepted with 0x700 ticks or less left
	bool expired = false;    // [+0x1CAD]: the deadline passed with the page open
	bool done = false;       // the page after it (0xE)
};
// Each update: the list again; a list that was empty, or the ship chosen not on it, gives its first; the deadline past
// (the tick above it), the page after it.
void traveller_refresh(const SaveGame& save, uint32_t town, TravellerWindow& w, const std::vector<uint16_t>& before,
	const std::vector<uint16_t>& now);
// The arrows (on with more than one ship): the ships in turn.
void traveller_turn(TravellerWindow& w, const std::vector<uint16_t>& ships, int step);
// «Accept order» (0x5D6DEE, with a ship): with more than 0x700 ticks to the deadline (unsigned), {0x53, u16 ship, u16
// payment, u32 deadline, u8 town, u8 destination}; else too late. The page after it.
std::optional<OperationBytes> traveller_press(const SaveGame& save, uint32_t town, TravellerWindow& w);

}  // namespace p3::data
