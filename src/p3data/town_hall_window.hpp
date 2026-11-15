#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "p3data/player_actions.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// The arsenal (.let 0x0B, the window *0x6E552C: its update 0x5B0930, its drawing 0x5B0CC0): the town's militia, only
// for the town's mayor ([+0x6F1] the player; else the warning 0x69DD1C and nothing else). For each type k (the town
// watch, archers, crossbowmen, musketeers) the soldiers on duty [+0x998 + k], those in training [+0x99C + k] and
// their weekly cost; «Form unit» (0x41) with a type chosen, «Dismiss unit» (0x40) with soldiers of it on duty.
enum class ArsenalMessage : uint8_t {
	NoWeapons = 0,   // 0x6AA450: the office (or, without one, the town) has fewer than 50 of the type's weapons
	NoBeggars = 1,   // 0x69DD24: fewer than 20 beggars in the town ([+0x2E4])
	NoCouncil = 2,   // 0x6AA448: with [+0x2C8] & 0xE0, the units at the council's total [+0x9A0]
	Authorised = 3,  // 0x6AA44C: «the town council has authorised a total of %u units» ([+0x9A0])
	None = 0xFF,
};

constexpr uint32_t kArsenalTypes = 4;

// Whether the player is the town's mayor (the window's [+0x38A]).
bool arsenal_mayor(const SaveGame& save, uint32_t town);
// A type's weekly cost: (on duty + in training) · 0x672DB0[k] (33, 26, 40, 46) · 7.
int32_t arsenal_cost(const SaveGame& save, uint32_t town, uint32_t type);
// The message when the window opens: Authorised with [+0x2C8] & 0xE0, else None.
ArsenalMessage arsenal_open_message(const SaveGame& save, uint32_t town);
// «Form unit» (0x5B0AD1): the weapons (the player's office in the town, else the town, at least 50: NoWeapons), the
// beggars (at least 20: NoBeggars, over it), with [+0x2C8] & 0xE0 the units under the council's total (NoCouncil);
// with none of them the operation 0x41 {0x41, type, town} (`op`) and then Authorised with [+0x2C8] & 0xE0. Whether
// it queues.
bool arsenal_form(const SaveGame& save, uint32_t town, uint32_t type, ArsenalMessage& message, OperationBytes& op);
// «Dismiss unit» (0x5B0BF3): 0x40 {0x40, type, town}, the message Authorised with [+0x2C8] & 0xE0, else None.
OperationBytes arsenal_dismiss(const SaveGame& save, uint32_t town, uint32_t type, ArsenalMessage& message);

// The town hall (.let 0x03): its side menu *0x6E5500 (0x5A8AF0) opens with its 8 buttons the pages of the window
// *0x6E558C (0x5E0BA0; the update 0x5E0850 and the drawing 0x5E0960 by the page [+0x18E4]; the window's town
// [+0x18E8]). The page 0, the notices (0x5E7730, 0x5E1010): the world's orders (0x6DE530, 12 bytes each; the event
// 0x22 makes them) one at a time with two arrows, and an entry 7, the prince's decree, while the town's siege object
// has wares the world lacks.
struct TownOrder {
	uint8_t town = 0;       // [+0x0]: the town that orders
	uint8_t ware = 0;       // [+0x1]
	bool bonus = false;     // [+0x2]: the price paid when all is delivered (a «bonus»), else at each delivery
	uint8_t count = 0;      // [+0x3]: barrels or loads
	uint32_t deadline = 0;  // [+0x4]: a tick
	uint32_t price = 0;     // [+0x8]: of one
};
// The world's order k (0..6) when its town is in the world.
std::optional<TownOrder> town_order(const SaveGame& save, uint32_t k);
// The prince's decree (with the town's siege object's [+0x1010] not 0xFF): the four wares the world lacks most (0xFF
// none past the first) and the ware of a supply contract the prince offers (0x635660: of those three months in the
// list, by (the town + [0x6DE4A1]) % their count; 0xFF none), or 0xFF when the player has promised deliveries there
// already (0x636590: his node in [+0x101C]).
struct PrinceDecree {
	std::array<uint8_t, 4> wares{};
	uint8_t contract = 0xFF;
};
std::optional<PrinceDecree> prince_decree(const SaveGame& save, uint32_t town);
// «Accept order» (0x5E7730) on the order k: the player's standing (s16 [+0x22]) below 20, refused (false); else the
// operation 0x3D {0x3D, town, ware, the client's first name, family name (the order's town's [+0x6E8 + c] and
// [+0x6EC + c], c = (the window's town + ware + count) & 3), the deadline | 0xFF, u16 count, u16 count, u16 price,
// the player, the bonus}.
bool accept_order(const SaveGame& save, uint32_t town, uint32_t k, OperationBytes& op);

// The side menu's buttons (0x5A9780, on the menu each frame, for the town of its [+0x8F5]): 0 «Notices» on with the
// prince's decree (the town's siege object's [+0x1010] not 0xFF), else off — the game turns it on for an order in the
// world (0x6DE530: a town byte below the towns' count) and then off again in the same breath (a game bug: noted in the
// roadmap; with no decree the page can't be opened); 1 «Outrigger», 2 «Town info», 3 «Hanseatic info», 5 «Town hall
// cellar» always; 4 «Council Chamber» in a town with [+0x2C8] & 0xE0 when the player (his rank in the town [+0x39C +
// town] at least 4) has the town council's vote of today (0x4DA060 finds it), or, in the Alderman's town
// (0x6DE52E), the Alderman's election (the first event 0x20 of the queue's slots, 0x4D9D10) or the diet's vote (the
// first event 0x21 of the queue, 0x4D9CD0) is today and the player may vote (0x4D9D40); 6 «Town Coffers» for the
// town's mayor ([+0x6F1]); 7 «Alderman's office» only in the Alderman's town (`office`), on for the Alderman
// (0x6DE52D) or a merchant with [+0x37] set.
struct TownHallMenu {
	std::array<bool, 8> on{};
	bool office = false;  // The eighth button shown.
};
TownHallMenu town_hall_menu(const SaveGame& save, uint32_t town);

// The page 1, the outrigger (its drawing 0x5E1A10, its update 0x5E7AB0): the town's guard ship [+0x994] and the
// terms for one. The drawing works them out each frame from the average firepower of every merchant's ships
// (0x515750 with −1: the ships with a firepower [+0x120] above 0, summed and divided by their count, signed; 0
// without any): v = that average less 9 ([0x672CC8], 9 when below it, unsigned); the strength
// n = trunc(sqrt(v · 5.6)) + 2 as a u16, at least 20 ([+0x18EC]); the least firepower
// trunc(float((n − 2)²) / 5.6f) + 10 ([+0x18F0]); the weekly pay town[+0x2D4] / 100 + (5n + 50) · 2 as a u16
// ([+0x18F4]).
struct GuardShipTerms {
	uint32_t strength = 0;
	uint32_t firepower = 0;
	uint16_t pay = 0;
};
int32_t average_firepower(const SaveGame& save);
GuardShipTerms guard_ship_terms(const SaveGame& save, uint32_t town);
// The least firepower told as cannonballs in the page's text (0x5E1C95..0x5E1D20): firepower_balls of it, each big
// ball with a small one TEX31113, with the half one 31112, alone 31111; the small ones left 31110, the half 31109.
std::vector<uint32_t> guard_ship_strength_pictures(uint32_t firepower);
// What the page shows (0x5E1D25..0x5E2376): the town without a guard ship ([+0x994] past the ships), Wanted;
// a guard whose firepower (the u16 of [+0x120]) is below the least, Unsuitable; else Kept. The page then lists
// the player's ships that can take the place (0x504AC0's mode 9: in the town, out of a convoy, docked — state
// ≤ 1 —, able to lead a convoy, 0x519C40, and of the firepower `least` at least), unless the guard is kept by
// another merchant (`list` false: nothing more, its buttons hidden); kept by the player, the ships must be
// better than his guard (its firepower + 1) and the page tells his pay ([+0x990]).
enum class GuardShipCase : uint8_t { Wanted, Unsuitable, Kept };
struct GuardShipPage {
	GuardShipCase kind = GuardShipCase::Wanted;
	uint16_t guard = 0xFFFF;
	bool own = false;  // Kept by the player.
	bool list = true;
	GuardShipTerms terms;
	uint32_t least = 0;  // The firepower the ships listed need.
	std::vector<uint16_t> ships;
};
GuardShipPage guard_ship_page(const SaveGame& save, uint32_t town);
// «Post outrigger» (0x5E7AB0): {0x44, u32 town, u32 ship, u32 pay}.
OperationBytes guard_ship_operation(uint32_t town, uint32_t ship, uint32_t pay);

// The page 2, «Town info» (its set page 0x5E0E18, update 0x5E7C80, drawing 0x5E29C0): in a town with a council
// ([+0x2C8] & 0xE0) first the town's information ([+0x18F6] 0: its mayor, the next election and its candidates, the
// council's next vote, the player's candidacy and «Make offer»), then the proposals for the council (1); in a town
// without one (founded by a merchant: its «Town Manager») the manager and, for him, the taxes he may change (2).
bool council_town(const SaveGame& save, uint32_t town);
// A person of the page's texts: a merchant, or one of the town's four citizens (0xFC..0xFF, j = (0xFF − k) & 3: their
// names [+0x6D4 + j], [+0x6D8 + j], title [+0x6E0 + j] and rank [+0x6E4 + j]).
struct TownPerson {
	uint8_t who = 0xFF;     // A merchant's number, or 0xFC..0xFF.
	bool merchant = false;  // who below the merchants' count
	bool female = false;    // A merchant's [+0x1A] at 0.
	uint8_t title = 0;      // 0x4F9430 / [+0x6E0 + j]: the word 0x69EE48 + 4·title
	uint8_t rank = 0;       // [+0x39C + his home] / [+0x6E4 + j]
	uint8_t first = 0;      // [+0x1C] / [+0x6D4 + j] (game_first_name)
	uint8_t family = 0;     // [+0x1D] / [+0x6D8 + j] (game_family_name)
};
// The town's mayor ([+0x6F1]; his title as 0x5296B0 gives it).
TownPerson town_mayor(const SaveGame& save, uint32_t town);
// The four candidates for the next election (0x528CF0, with the titles, ranks and names it also gives the page).
std::array<TownPerson, 4> mayor_candidates(SaveGame& save, uint32_t town);
// The next election's date (0x5295F0): the town's event 2 (the first of the queue with [+0xE] the town's [+0x2C0],
// 0x4D9F10): its day [+0xF] and month [+0x10] (1..12), the year this one or, past its day of the year [+0x8], the
// next; none: 31 December of this year.
struct TownDate {
	uint8_t day = 0;
	uint8_t month = 0;
	uint16_t year = 0;
};
TownDate next_mayor_election(const SaveGame& save, uint32_t town);
// The council's next vote (0x4DA140: the first event 0x2E of the town ([+0xC]) before now + 0x1E00): its tick, its
// kind [+0xD] (0 an extra tax of [+0x8], 1 the walls, 2 the military, 3 the head tax [+0x14]).
struct CouncilVote {
	uint16_t event = 0xFFFF;
	uint32_t tick = 0;
	uint8_t kind = 0;
	int32_t amount = 0;
	uint8_t tax = 0;
};
std::optional<CouncilVote> council_vote(const SaveGame& save, uint32_t town);
// Whether the merchant stands for mayor (0x4F9360: a human one's [+0x118]; an AI merchant's set by his [+0x8] & 4),
// and «Yes» / «No» (0x5E7C92): {0x45, u32 merchant, u32 standing}.
bool standing_for_mayor(SaveGame& save, uint32_t merchant);
OperationBytes candidacy_operation(uint32_t merchant, bool standing);
// The proposals (the buttons [+0x10E4 + 0xE8·k]: 0 «Enlarge town walls», 1 «Expand military», 2 «Change tax per head»,
// 3 «Levy extra tax»). With a council (0x5E302B): each off while the town has a vote of its kind ahead (0x4D9F60's
// bits 2, 4, 8, 1); the walls only with the building list done ([+0x766] at [+0x776] or more) and room for them
// (0x4FCFE0(2)); the military while the units on duty and in training reach the council's total [+0x9A0]. Without a
// council only the two taxes, always on.
std::array<bool, 4> proposals_open(const SaveGame& save, uint32_t town);
// A tax proposal's amount (0x5E7E92, 0x5E7EEC): the head tax from the town's [+0x6F8], 0..20 by 1; the extra tax
// from 0, 0..999999 by 5000; the walls' and the military's none.
struct ProposalAmount {
	int32_t start = 0;
	int32_t low = 0;
	int32_t high = 0;
	int32_t step = 1;
};
ProposalAmount proposal_amount(const SaveGame& save, uint32_t town, uint32_t kind);
// The amount after «-» or «+» (the game's: one step, then held at the ends).
int32_t proposal_step(const ProposalAmount& a, int32_t value, int step);
// Whether «OK» can be pressed for a tax (0x5E7FBF..0x5E808B): the extra tax above 0, the head tax not the town's.
bool proposal_ok(const SaveGame& save, uint32_t town, uint32_t kind, int32_t value);
// «OK» / «Confirm» (0x5E80AD): {0x48, u32 tick (now + 0xE00 moved off the town's other votes, 0x4D9FC0), u32 the extra
// tax, u8 town, u8 the vote's kind (the walls 1, the military 2, the head tax 3, the extra tax 0), u8 the head tax, 0,
// u8 the player}.
OperationBytes council_proposal(const SaveGame& save, uint32_t town, uint32_t kind, int32_t value);

// The page 3, «Hanseatic info» (its drawing 0x5E39F0; no update): the Alderman (0x6DE52D: a merchant, or one of the
// citizens of his town 0x6DE52E, his title 0x529DC0), the next election's candidates (0x529700: the mayors of four
// towns, a merchant or a citizen, with the titles, ranks and names it also gives the page: a citizen of a town whose
// mayor is a merchant taken by the Alderman's number, the rank 6, or 7 in the Alderman's town), the election's day
// (the first event 0x20 among the queue's slots, 0x4D9D10) and the diet's extraordinary meeting (the first event 0x21
// of the queue, 0x4D9CD0: its town [+0x9] (the towns' count or past it: the pirates' fleet), its action [+0xE] (1 a
// blockade, 2 a boycott, 3 .. 5 a large, moderate, small fine; else none), its reason [+0xA] (0 an export tax on the
// ware [+0xC], 1 an extra tax, 2 and 5 the pirates, 4 and 6 the offices plundered, 3 none)).
TownPerson alderman_person(const SaveGame& save);
struct AldermanCandidate {
	uint8_t town = 0xFF;
	TownPerson person;
};
std::array<AldermanCandidate, 4> alderman_candidates(const SaveGame& save);
std::optional<uint32_t> election_slot_tick(const SaveGame& save);
struct HanseDay {
	uint32_t tick = 0;
	uint8_t town = 0;
	uint8_t action = 0;
	uint8_t reason = 0;
	uint8_t ware = 0;
};
std::optional<HanseDay> hanse_day(const SaveGame& save);

// The page 4, «Council Chamber» (its update 0x5E8280, its drawing 0x5E4160), in a town with a council: the vote of today
// the player can take part in, the first of: the Alderman's election (in his town, the first event 0x20 among the
// queue's slots on today, the player's rank at home above 3), the diet's vote (the same, the first event 0x21 of the
// queue), the town council's vote (0x4DA060: the town's event 0x2E due by the end of the day, the player's rank in
// the town 4 or more). «Cast vote» shown when he may vote (0x4D9D40; the council's, his bit still in the event's
// [+0x13]). The votes so far: the election's four candidates [+0xC + k]; the diet's for, against and abstaining
// [+0x12..+0x14]; the council's [+0x10..+0x12].
enum class ChamberVote : uint8_t { None = 0, Election = 1, Diet = 2, Council = 4 };
struct ChamberState {
	ChamberVote vote = ChamberVote::None;
	uint16_t event = 0xFFFF;
	bool may_vote = false;
	std::array<uint8_t, 4> votes{};
};
ChamberState chamber_state(const SaveGame& save, uint32_t town);
// The votes as tally marks (0x5E43C3.., 0x5E51F3..): groups of five (TEX32010) and the rest (TEX32006..32009: 1..4),
// above `cap` (15 for the election, 20 for the others) the cap's fives alone.
std::vector<uint32_t> vote_tally(uint32_t votes, uint32_t cap);
// «Cast vote» (0x5E8280) for the choice (a candidate 0..3; for, against, abstain 0..2): the election's and the diet's
// 0x3F {player, choice}, the council's 0x49 {player, town, choice} (the diet's and the council's only below 3).
std::optional<OperationBytes> chamber_vote_operation(const SaveGame& save, uint32_t town, ChamberVote vote, uint32_t choice);

// The page 5, «Town hall cellar» (its drawing 0x5E6040; no update): the dish of the day, one of 31 (0x69FD74 + 8·n its
// name, + 4 its words): n = ((the day, tick >> 8, % 31) · (town + 1) + town + town / 2) % 31.
uint32_t cellar_dish(const SaveGame& save, uint32_t town);

// The page 6, «Town Coffers» (its update 0x5E8380, its drawing 0x5E6190; for the town's mayor): the balance by kind,
// this month's (0x5E8563) or the last one's (0x5E84F7): the personal and land taxes ([+0x704] + [+0x708], [+0x730] +
// [+0x734]), the extra taxes ([+0x70C], [+0x738]), donations less withdrawals ([+0x710] − [+0x728], [+0x73C] −
// [+0x754]), and spent: the town guard (−[+0x718], −[+0x744]), the walls and towers (−[+0x71C], −[+0x748]), the
// outriggers (−[+0x720], −[+0x74C]), the sieges (−[+0x724], −[+0x750]), the rest (−[+0x72C], −[+0x758]).
std::array<int32_t, 8> coffers_balance(const SaveGame& save, uint32_t town, bool last_month);
// «Take gold» off while the town is besieged (0x633AE0 on its siege object).
bool coffers_can_take(const SaveGame& save, uint32_t town);
// «OK» after «Donate gold» or «Take gold» (0x5E8740; nothing for 0): {0x47, u32 player, u32 town, u32 into the town
// (1 donating), u32 amount}; the amount from 0 to the player's money or the town's [+0x700], by 10000.
std::optional<OperationBytes> coffers_operation(const SaveGame& save, uint32_t town, bool donate, int32_t amount);

}  // namespace p3::data
