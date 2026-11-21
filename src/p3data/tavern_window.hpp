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
	MissionLetter,    // the side room: a tavern's mission letter 0x71 (side_room_letter); its page 0x5D8100 queues 0x76
};
constexpr int kTavernPeople = 10;

// 0x5A6E60: who is there in the town's tavern, as the player sees it, but the side room (it depends on the letter the
// menu keeps: side_room_letter). (The menu first sets the one talked to, unless the sailors or the informer, but
// 0x5A6E60 clears all ten before it looks: it has no effect.)
std::array<bool, kTavernPeople> tavern_people(const SaveGame& save, uint32_t town);

// Who is there, as the town's functions say (thiscall on the town in the game; [0x6DFC14] the player).
// 0x526880: one of the town's two order places [+0x830], [+0x832] holds an event (below the events) whose [+0x16],
// the merchant who took the order, is the player.
bool tavern_order_client(const SaveGame& save, uint32_t town);
// The order itself (0x5268D0, when his page opens, 0x5CF77B: the event's [+0x8..+0x17] copied to the window's
// [+0x2174], the event to [+0x1BF0]); event 0xFFFF: none.
struct OrderClient {
	uint16_t event = 0xFFFF;
	uint8_t town = 0;
	uint8_t ware = 0;
	uint8_t first = 0;   // [+0xA]: the client's first name (0x512A60)
	uint8_t family = 0;  // [+0xB]: his family name (0x512AE0), the side menu's button (0x5A6F2A)
	uint32_t deadline = 0;  // [+0xC]
	uint16_t left = 0;      // [+0x10]
	uint16_t total = 0;     // [+0x12]
	uint16_t price = 0;     // [+0x14], of one barrel or load
	bool at_end = false;    // [+0x17]: paid at the end (a premium of price · total), not for each delivery
};
OrderClient tavern_order(const SaveGame& save, uint32_t town);
// What his page (update 0x5D7760: 0x5D77D4..0x5D78E0) says the player has of the ware in the town: his office's
// (0x5308A0) and his ships' docked there ([+0x39] the town, [+0x136] bit 0 clear) summed, then in barrels or loads.
// The page's amount goes from 0 to the smaller of it and what is left (its box's top).
int32_t order_client_stock(const SaveGame& save, uint32_t town, uint8_t ware);
// «Hand over supplies» (0x5D7A90, with an amount): {0x51, player, 0, the event, the amount} (the engine's fn_53c050);
// then the page 16 says it was all when the amount reaches what is left.
OperationBytes order_delivery_operation(uint32_t player, uint16_t event, uint32_t amount);
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

// The side room (the menu's tenth person, «Side room», 0x5A6E60's [+0xB15]): a tavern's mission letter 0x71 of the
// player's for the town, sent by the mission interpreter's 0xF7 (b1 5). The menu keeps the letter shown ([+0xB1C]).
// Whether the player may take the letter `entry` (0x5A7175..0x5A719A): its record's event [+0x8] a 0x1B whose variable
// the record's [+0xC] (the addressee) holds the player or no merchant (a value not below the merchants' count). (Past
// the script's variables the game reads the room after them, which p3vita doesn't keep: no.)
bool side_room_open(const SaveGame& save, uint16_t entry);
// 0x5A6E60's end: the letter the side room has: `kept` (0xFFFF none) if 0x4D7900 from it still finds it for the town
// (a 0x71 of the town, before its deadline) and it is open; else the first open one found from the player's chain;
// 0xFFFF for none (no tenth person).
uint16_t side_room_letter(const SaveGame& save, uint32_t town, uint16_t kept);
// 0x5A74B0 (the page's second button enabled with one) and 0x5A77A0 (going to it): the next open letter of the town
// after `kept` along the player's chain, else the first from its head; 0xFFFF when that is `kept` itself or none.
uint16_t side_room_next(const SaveGame& save, uint32_t town, uint16_t kept);
// The page 9's words (0x5CF665, 0x5D7FD0): the letter's buffer (raw, Windows-1252): its first string the title (the
// banner's), the second its text up to a '|' (the page's, in the font 1) and after it (the page 0x11's after
// accepting: none, the page closes), then the record's [+0xB] answers: the first the accept button's label (none
// without answers: the button stays off).
struct SideRoomWords {
	std::string title;
	std::string text;
	std::string after;
	std::string accept;
	bool has_accept = false;
};
SideRoomWords side_room_words(const SaveGame& save, uint16_t entry);
// The talk with the side room's person (0x5A7BA0 taken, 0x5A7A60 left, the kind 9): 0x52 {the record's event | its
// [+0xC] << 16, `merchant` (the player, or the merchants' count to leave), town, 9}.
OperationBytes side_room_talk_operation(const SaveGame& save, uint16_t entry, uint32_t merchant, uint32_t town);
// The accept button (0x5D81BC..0x5D8202): 0x76 {the record's event, its [+0xC] << 8 | its [+0xA], 0, the player}.
OperationBytes side_room_accept_operation(const SaveGame& save, uint16_t entry);

// A merchant's first name and family name as the game keeps them in his [+0xE8] and [+0xE4] (0x4F6420: the first
// name [+0x1C] from the women's list, the bit 0x80, unless [+0x1A]; 0x4F6450: the family name [+0x1D]).
std::pair<std::string, std::string> merchant_names(const SaveGame& save, uint32_t merchant);

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

// --- The burglar (the page 7: update 0x5D68E0, drawing 0x5D1E70, the page after it 0x5D21A0) ---
// 0x531470 (on the world: town): the owners of the town's offices (its list [+0x784], next [+0x2CA]; the owner
// [+0x2C4] below the merchants) without the flag [+0x8] & 4, not the player, each with the office's [+0x2D4] +
// [+0x2D2] (u16).
std::vector<std::pair<uint16_t, uint16_t>> fn_531470(const SaveGame& save, uint32_t town);
// The victims as the window makes them when it opens (0x5CC95C): 0x531470's merchants sorted by their family name,
// then their first name (upper-cased and compared by _stricmp: ASCII without case, the rest by its byte; equal ones in
// their order), each with the price ((his [+0x46C] / 10000) + 1) · 500, at most 65000; 500 when [+0x46C] is negative.
struct BurglarVictim {
	uint16_t merchant = 0;
	uint16_t price = 0;
};
std::vector<BurglarVictim> burglar_victims(const SaveGame& save, uint32_t town);
struct BurglarWindow {
	std::vector<BurglarVictim> victims;
	bool hired = false;   // [+0x1CA7]
	int refusal = 0;      // [+0x1C8C]: which «get some more gold» (0..4)
	bool done = false;    // the page after it (0xD)
};
// «Hire» on a victim (0x5D69A6): with the money (signed, the price at most it) {0x4F, u32 price, u16 rand(), u16 town,
// u16 player, u16 the victim, u8 0}; else rand() / 32768.0 · 5 truncated, the refusal. The page after it.
std::optional<OperationBytes> burglar_press(SaveGame& save, uint32_t town, BurglarWindow& w, size_t victim);

// --- The pirate (the page 8: update 0x5D5430, drawing 0x5D09A0, the page after it 0x5D0F00) ---
// His captain is the town's 0x5261D0 (the page closes without one); his henchmen the window's [+0x1C3C], made when it
// opens from the tavern's roll (0x5CC482: roll · 40 + 10.5, truncated: 10..50); his share of the loot ((the captain's
// [+0x8] >> 5) + 6) · 5 %; the ships 0x504AC0's kind 5 (ShipListMode::Pirate), as the traveller's (the arrows on with
// more than one, the first when the list was empty).
uint32_t tavern_pirate_captain(const SaveGame& save, uint32_t town);  // 0x5261D0
uint32_t pirate_henchmen(float roll);
uint32_t pirate_share(const SaveGame& save, uint32_t captain);
struct PirateWindow {
	uint16_t ship = 0xFFFF;  // [+0x1C30]
	bool wrecked = false;    // the ship with 20 % of its hull or less: refused
	bool done = false;       // the page after it (0xB)
};
// «Hand over ship» (0x5D573A: the pirates sail all the Hanse) or its second button «before <town>» (0x5D584C): with
// more than 20 % of the ship's hull ([+0x18] · 100 / [+0x14], signed), {0x4E, captain, ship, henchmen, 0xFF or the
// town}; else refused. The page after it.
std::optional<OperationBytes> pirate_press(const SaveGame& save, uint32_t town, PirateWindow& w, uint32_t henchmen,
	bool before_town);
// The traveller's and the pirate's arrows: the ships in turn (nothing with fewer than two).
void ship_turn(uint16_t& ship, const std::vector<uint16_t>& ships, int step);

// --- The informer (the page 4: update 0x5D5970, drawing 0x5D10C0, the page after it 0x5D0F00) ---
// He is there while the town's [+0x82C] is below 0x14. What he sells is made by 0x526B40 when the tavern's window
// opens and again after each sale: an operation the window queues as it is when the player pays its price ([+0xC],
// signed against his money), and a text (the game's, Latin-1) shown then.
struct InformerNews {
	OperationBytes op{};   // [+0x0]: 0x4C {player, his town or 0xFFFF, price, 0xFFFF} or 0x55 (a map's piece)
	// [+0x14], [+0x16]: his first and family names (0x673732 and 0x673733 of his [+0x82C]; 0 without), which the pages
	// 4 and 12 put in their title through 0x512A60 and 0x512AE0 (the window's copy at [+0x2168], [+0x216A]).
	uint16_t first_name = 0;
	uint16_t family_name = 0;
	std::string text;      // [+0x18]
	int32_t price() const;
};
class NavData;
class GameTexts;
// 0x526B40 (on the town, &news). Every third day ((tick >> 8) + the town) % 3 == 0, with a treasure map's letter of the
// player's (0x4D75D0: his mail's first 0x45, or 0x46 with [+0xA] below 0x1FF; a piece of its nine bits [+0xA] not
// found yet, the only one or one by rand()): 0x55 {player, the letter, 10 · (rand() % 11 + 30 · (his rank + 1)), the
// piece << 5 + the town} with the text 0x6C29F8. Else 0x4C at (his rank in his home town + 1) · 100, and the news of
// the region of the informer ([+0x82C]'s 0x673731), each 10 more, joined by lines:
//   the towns of the region whose state the player knows (0x535300) four days late or more (0x5353C0 above 3), the
//   first change of each: the siege 0x10 (0x6C25C8, lifted 0x6C25CC), the epidemic 0x8 and the famine 0x1000 (the
//   game says the siege's words for them too: fixed behind fix_game_bugs, 0x6C2624/8 and 0x6C2670/4), the fire
//   0x8000 (0x6C26C0, 0x6C26C4), the ware short (bits 18..22; 0x6C276C or 0x6C2770 by 0x66E0D4); else a bad (0x2000,
//   0x6C272C) or a good (0x4000, 0x6C2730) harvest; none told, the informer's own town becomes [+0x8] (he leaves);
//   the scarcest ware of the region (stock below its [+0x4F0 + 0x10·w]: (stock · 4) / (that >> 7), or −that without
//   stock; the least below 256; 0x6C27E0); the most plentiful of the mask 0x4472E in a region town with [+0x76C] &
//   0x100 (stock above [+0x4F4 + 0x10·w] + 20000; 0x6C28A8); the pirate with the largest [+0x16] (his captain's names
//   0x515AE0; or the player's Hanse task 0x4EC694 hunting one: [+0xC] 2, [+0x16] 0) seen near the nearest town of
//   each of his ships with him as captain (0x448610; 0x6B1460, 0x6B1464, 0x6B1468 by (captain + town) % 3).
// Nothing at all: a town of the region (the (rand() & 3) + 1-th, round the towns once; none: [+0x82C] % towns) and,
// by the informer's town's [+0x54] & 3, its people and beggars (0x6C29B8), its mayor (0x6C29BC), its beer [+0x31C]
// (0x6C29C0) or its trade tax [+0x2C6] (0x6C29C4); the price (([+0x54] + that town + the region) & 3 + 7) · 10 and
// [+0x8] his town. Any text ends in quotes. The towns' names are 0x6DDA00 by the town's index (the letters' game bug:
// the right ones with fix_game_bugs). The pirates' part needs the navigation: without it, nothing of it. With the
// Spanish exe's texts, its code's orders (0x526FF0: the scarce and the plentiful ware's and the beer's town first, the
// plentiful ware in the plural by 0x66E0D4: 0x6C2F80).
// It writes over the window's news (the map's piece leaves [+0x14] and [+0x16] as they were).
void fn_526b40(SaveGame& save, const NavData* nav, const GameTexts& texts, uint32_t town, InformerNews& news);
// 0x448610 (a ship): the town (its index) whose sea route from the ship (0x445010) is the shortest by 29 · the sum of
// its legs' vertical lengths (the game's measure), the first of equals; 0 with no route.
uint32_t fn_448610(const SaveGame& save, const NavData& nav, uint32_t ship);

}  // namespace p3::data
