#pragma once

#include <cstdint>
#include <vector>

#include "p3data/player_actions.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// What another merchant's office shows and what it queues (the rules; the viewer draws them its way): the building
// .let 0x2D, its window *0x6E5550 (made by 0x5BB750, opened by 0x5BBAA0, its update 0x5BDAFA). The offer itself is the
// engine's (town_tick/offers.cpp: 0x5F, 0x75, the event 0x13). Notes: re/emu/sesion27/guild_notes.md (3c).
//
// The window lists the merchants of the town's offices (0x531470); one chosen, the offer's page takes the tick as the
// offer's number ([+0x1EF0]) and him as B ([+0x1EF4]); there the player chooses what to offer (a list of money, the
// wares he has and his ships), how much (a slider) and the price he asks (a box with − and +), and «Offer» queues
// 0x5F and closes the window.

// 0x531470: the merchants of the town's offices in the order of its list [0x784], without the player ([0x6DFC14])
// and those with [+0x8] & 4.
std::vector<uint16_t> offer_merchants(const SaveGame& save, int town);

// What the player can offer (0x5BDB17..0x5BDD78), in the window's order: money if he has some (signed > 0); each of
// the wares 0..19 (the table 0x698538 is the identity) whose units are over 0: the office's stock in the town plus the
// cargo of his ships with [+0x39] the town and not marked [+0x136] & 1 (in any state), divided unsigned by 200 for the
// wares in barrels, 2000 for the loads; and his ships of 0x504AC0's mode 7 (ShipListMode::Docked). With
// fix_game_bugs() a ware's end is at most what 0x5F can hold back (offers_detail::holdable_units), and a ware with
// nothing to hold back isn't listed.
struct OfferChoice {
	uint8_t kind = 2;  // The operation's [+0xF]: 2 money, 0 a ware, 1 a ship.
	uint16_t what = 0;  // The ware or the ship.
	int32_t most = 0;  // The slider's end: the money he has; the ware's units, signed this time (0x5BE2ED..0x5BE3E7).
};
std::vector<OfferChoice> offer_choices(const SaveGame& save, uint32_t player, int town);

// «Offer» (0x5BE020..0x5BE16A): enabled with a ship, or with an amount over 0 (the box's price may be 0); {0x5F, the
// number, s32 the price, u8 player, u8 merchant, u8 town, u8 kind, then money: s32 amount; a ware: u16 ware, u16
// amount; a ship: u16 ship} (from 0x535740's zeros).
bool can_offer(const OfferChoice& choice, int32_t amount);
OperationBytes offer_operation(uint32_t number, int32_t price, uint32_t player, uint32_t merchant, int town,
	const OfferChoice& choice, int32_t amount);

// After an offer of wares without an office of the player's in the town, the window shows a message (0x5BE1A6, the
// text slot 0x6A0538): the wares come from his ships there.
bool offer_from_ships_only(const SaveGame& save, uint32_t player, int town, const OfferChoice& choice);

}  // namespace p3::data
