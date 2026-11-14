#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "p3data/player_actions.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// What the guild's window shows and the operations it queues (the rules; the viewer draws them its way): the guild
// (.let 0x07, building type 0x22; its window *0x6E5558, its update 0x5BFD60 with a page per button of its side menu
// 0x5A3C00). The operations themselves are the engine's (town_tick/operations.cpp, events.cpp, events_mayor.cpp).
// Notes: re/emu/sesion27/guild_notes.md.
//
// The side menu's buttons (0x5A37C0): 0 joining, only for a merchant who isn't a member of the town's guild; 1 the
// auctions to come, always; 2 the town's auction, while it has one (the town's [+0x9A2], an event 0x0A or 0x36 up to
// its phase 7); 3 and 4 only for members; 5 and 6 (the page 8) always. Each opens the window's page of its number
// (0x5A3DCE), the last the page 8; the page 6 is the auction's end and 7 the welcome after joining.

// --- Joining (the page 0, 0x5BFE60) ---

// Whether the merchant is a member of the town's guild: the town's bit in his [+0x468].
bool guild_member(const SaveGame& save, uint32_t town, uint32_t merchant);
// What joining costs (0x5C1243: 0x51B7E0 on the town; 10000 for a merchant past the count).
int32_t guild_join_price(const SaveGame& save, uint32_t town, uint32_t merchant);
// The button (0x5BFE9A..0x5BFEF8): with the money at least the price (signed), {0x37, player, town, price}.
bool can_join_guild(const SaveGame& save, uint32_t town, uint32_t merchant);
OperationBytes join_guild_operation(uint32_t merchant, uint32_t town, int32_t price);

// --- The auctions to come (the page 1, 0x5BFF0B) ---

// An entry of 0x4DA1A0's list (0x14 bytes on the heap).
struct AuctionEntry {
	uint32_t tick = 0;  // [+0x0] when it begins: its tick, + its wait [+0xE] in phase 1, + 0x100 up to phase 2.
	std::array<uint16_t, 3> lots{};  // [+0x4] what is on sale (a ship, or a building's record | 0x8000), 0xFFFF none.
	uint16_t town = 0;  // [+0xA]
	uint32_t event = 0;  // [+0xC] the auction (an event 0x0A or 0x36).
	// [+0x10] a ships' auction: how many of each type (the ship's [+0xE] & 3); a buildings': 0xFF, 0, 0, 0.
	std::array<uint8_t, 4> types{};
};
// 0x4DA1A0 (on the queue): the auctions to come, at most 3 in the queue's order: the events 0x0A and 0x36 of a town
// with a guild (its [+0x2C8] & 0xC0) in the phases 2 and 3 (and 1 with the settings' byte [+0xD] at 0).
std::vector<AuctionEntry> upcoming_auctions(const SaveGame& save);
// 0x4DA480 (on the queue): whether the player is told of the auction (his bit in its [+0x16], the human merchants in
// order from 1); false for an event past the queue or not an auction.
bool told_of_auction(const SaveGame& save, uint32_t event);
// Its boxes (0x5C0109..0x5C0161): one per entry, enabled with the guild's bit of the entry's town; pressed,
// {0x34, player, the auction, told ? 0 : 1}.
OperationBytes tell_operation(const SaveGame& save, uint32_t player, uint32_t event);
// «Register own ship for auction» (0x5C0251..0x5C04E6): the player's ships of 0x504AC0's mode 6
// (ShipListMode::Auction); the button with the guild's bit of the town and a ship, once each time the page opens
// ([+0x12F5]); {0x36, ship} (run at once with the settings' byte [+0xD] at 0, queued otherwise).
OperationBytes auction_ship_operation(uint32_t ship);

// --- The town's auction (the page 2, 0x5C055E) ---

// The town's auction (its [+0x9A2]) while it is an event 0x0A or 0x36 of the queue up to its phase 7, or −1.
int town_auction(const SaveGame& save, uint32_t town);
// What is on sale (0x5C07E4..0x5C089A): its first lot while it exists (a ship of the count, a record of the town's)
// and the ones after it that exist too, at most 3; their slots' values ([+0x8 + 2·k]).
std::vector<uint16_t> auction_lots(const SaveGame& save, uint32_t event);
// A lot's minimum bid (what its auction starts at, the phase 3): a ship's 0x4DA860, a building's 0x4DAF10 (with the
// slot's bit 15); 0 for a lot that doesn't exist.
int32_t lot_minimum_bid(const SaveGame& save, uint32_t event, uint16_t lot);
// A ship's condition in the lots (0x5C42B4): its hull in % (health · 100 / the most) below 40 → 4, 60 → 3, 80 → 2,
// 90 → 1, else 0; the words are the slots 0x69C81C − 4·k («undamaged» .. «catastrophic»).
int ship_condition(const SaveGame& save, uint16_t ship);
// The bid in a game of one player (the settings' byte [+0xD] not 0; 0x5C06CC..0x5C0C59): the button shown in the
// phases 4 to 7 (not before them nor at the end, the phase 8) and, at a buildings' auction, only with an office of the
// player's in the town (0x5308A0); it can be pressed with the money at least the price plus the step ([+0xE] + [+0x10],
// signed) and while the player isn't the highest bidder ([+0x17]); {0x33, player, the auction, price + step}.
bool bid_shown(const SaveGame& save, uint32_t event, uint32_t player);
bool can_bid(const SaveGame& save, uint32_t event, uint32_t player);
OperationBytes bid_operation(const SaveGame& save, uint32_t event, uint32_t player);
// The automatic bid (the settings' byte [+0xD] at 0, 0x5C0AFA..0x5C0B55, run at once): {0x35, the top, the auction,
// the lot, player}; the player's current one for a lot (0x4D9000: its top, 0 without one).
OperationBytes automatic_bid_operation(uint32_t top, uint32_t event, uint32_t lot, uint32_t player);
uint32_t automatic_bid(const SaveGame& save, uint32_t event, uint32_t lot, uint32_t player);
// Its button («offer», the window's [+0x14D0] in that game: 0x5C0AAC..0x5C0AE5) in every phase but hidden at a
// buildings' auction without an office of the player's in the town (0x5308A0).
bool automatic_bid_shown(const SaveGame& save, uint32_t event, uint32_t player);

// 0x4DAA50 (on the queue: ship, &minimum, &tick): the ship's minimum bid (0x4DA860) and when the first auction of
// ships (0x0A) in the queue's order with the ship among its lots takes place (its tick, + 0x100 up to its phase 2, +
// its wait [+0xE] in phase 1), or −1 for none. The auctions' page tells the player of the ship he registered with it
// (0x5C2B61).
struct ShipAuction {
	int32_t minimum = 0;
	int32_t tick = -1;
};
ShipAuction ship_auction(const SaveGame& save, uint16_t ship);

// --- The convoys (the page 3: its list made as the page opens, 0x5C14D0; its drawing 0x5C5370) ---

// 0x50A340 (on the ships' object) sorted by 0x5C6A30: the convoys from the head [0x6DD88C] (their next [+0x8]) whose
// guide's owner byte [+0x0] is a merchant, without the flags [+0x14] & 0xA, in a state [+0x12] below 4 or 0xF; then, in
// that order, one in a state of 2 or more goes after those placed, one below 2 before the first placed one that is below
// 2 too or that leaves later than it (their [+0x16], unsigned): the waiting ones first, the last of them first.
std::vector<uint16_t> guild_convoys(const SaveGame& save);
// A convoy's lines on the page (0x5C5370): its guide ship ([+0x10]), whence and whither ([+0x1E], [+0x1F]: 0x7F or
// more, an expedition), and over the ships from its [+0xA] (their next [+0x6]) of its guide's owner: how many, their
// hulls' average in % (each health · 100 / the most, the sum divided by the count), their crews and firepowers
// ([+0x120]) summed; while waiting (the state below 2) when it sails: the tick + its [+0x16].
struct GuildConvoyLine {
	uint16_t guide = 0;
	uint8_t from = 0;
	uint8_t to = 0;
	int ships = 0;
	int hull = 0;
	int crew = 0;
	int32_t firepower = 0;
	bool waiting = false;
	uint32_t departure = 0;
};
GuildConvoyLine guild_convoy_line(const SaveGame& save, uint16_t convoy);

// --- The members (the page 4: its list 0x5C67A0, made as the page opens; its update 0x5C0D68; its drawing 0x5C59D0) ---

// A member as the page's list keeps him (the window's [+0xFC], [+0x100], [+0x104], [+0x108], [+0x10C]).
struct GuildMember {
	uint16_t merchant = 0;
	uint16_t offices = 0;  // In how many towns he has an office (0x5308A0 on each).
	uint16_t respected_town = 0;  // Where his respect (0x4F9580) is highest, if above 0 (the first such town); else 0.
	uint16_t first_town = 0;  // The town of his oldest office (its [+0x2C0] the least, the first such town).
	uint32_t first_tick = 0x7F7F7F7F;  // That office's [+0x2C0]: when it opened.
};
// 0x5C67A0: the merchants in their order who aren't a town's own (their [+0x8] & 6 is 0: the humans and the AI's
// traders) with the town's bit in their [+0x468]. (A member without an office would keep the tick 0x7F7F7F7F and,
// in the game, whatever the buffer held for the town: 0 here.)
std::vector<GuildMember> guild_members(const SaveGame& save, uint32_t town);

// What the page shows of a member besides the list's (0x5C0E2D..0x5C0F1D, read from his memory as the page updates).
struct GuildMemberPage {
	uint32_t sex = 0;  // [+0x1A] & 1 (1 a man): the article 0x6C0EF8, «his» 0x69C870, «He» 0x69C884 and 0x69C8D0, 0x69C8C8.
	uint8_t title = 0;  // 0x4F9430: the word 0x6A0C18 + 4·title.
	uint8_t rank = 0;  // [+0x39C + his home town]: the word 0x6A0608 + 4·(rank + 8·sex).
	uint8_t home = 0;  // [+0x19]
	uint32_t age = 0;  // (the tick − [+0x10]) / 93440, unsigned.
	bool married = false;  // [+0x31] below the towns' count.
	uint8_t spouse = 0;  // [+0x30]: the spouse's first name (0x512A60).
	uint8_t children = 0;  // [+0x36]
	uint16_t ships = 0;  // [+0x478]
};
GuildMemberPage guild_member_page(const SaveGame& save, uint16_t merchant);

// --- The trading centres (the page 5: its map 0x534A40 made as the page opens, 0x5C1607; its places 0x5C63A0; its
// drawing 0x5C5F00; the place under the mouse 0x5BFC40) ---

// What an expeditions' finds object keeps (0x534750 makes one; the merchant's [+0x4D8], the guild members' 0x6DF278):
// a bit per point of scripts/Expeditionen.dat (SaveGame::expeditions) reached, [+0x0..0x30), the same bits for those
// drawn on the map with the wider brush, [+0x30..0x60), and a bit per trading centre found (the 15 world objects),
// [+0x60].
struct ExpeditionFinds {
	std::array<uint32_t, 12> points{};
	std::array<uint32_t, 12> wide{};
	uint32_t centres = 0;
};
// 0x4F7490 (on the merchant, as the page opens for the player): his own finds ([+0x4D8]) with, if he is a member of
// any guild ([+0x468] not 0), the guild members' 0x6DF278. The game keeps that one as the save loads it (0x5348B0: the
// finds of every merchant with a guild's bit), with what members find (0x4F93C0, 0x4F9400) and a new member's own
// (0x4F8560) added; p3vita's loader doesn't make the first part, so here it is the members' finds now with what the
// engine added to 0x6DF278 (the same but for a merchant who left every guild since the load).
ExpeditionFinds guild_map_finds(const SaveGame& save, uint32_t merchant);

constexpr int kTradingMapWidth = 400;  // 0x673AD8
constexpr int kTradingMapHeight = 150;  // 0x673ADA
// The map (0x534A40): the picture (textures.ini TEX21000 «Amerika.bmp», the whole sea from America to the Levant, or
// TEX21001 «Mittelmeer.bmp», the Mediterranean: the window's [+0x540 + 4·mediterranean]) under a fog the game makes,
// a texture 400 × 150 of one byte a pixel (0x4BB2A0 with the format 0x23) whose palette's entry k is the grey
// (0x50, 0x50, 0x50) with the alpha k, every byte 200 at first; each point found clears it round its place with a brush
// (a byte less the brush's, at 0 the least): the brush's 2r × 2r bytes 255 − (dx² + dy²)·f (0 past 255) for dx, dy from
// −r, r and f 5 and 8 (the wide brush 7 and 5) on the whole map. Without a point among the first 100 (America's) the
// map is the Mediterranean, the points scaled to it ((x − 186)·100 / 53, (y − 16)·100 / 53, truncated) and the brushes
// 9 and 3 (the wide 13 and 2). A brush past the map's left edge starts at 0, past its right at 400 − 2r; its middle row
// at r at least and 150 − r at most.
struct TradingMap {
	bool mediterranean = false;  // The window's [+0xC2].
	uint32_t centres = 0;  // [+0xC8]: the finds' trading centres.
	std::vector<uint8_t> fog;  // kTradingMapWidth · kTradingMapHeight bytes, row by row.
};
TradingMap trading_map(const SaveGame& save, const ExpeditionFinds& finds);

// A trading centre on the map (0x5C63A0, the window's [+0x54C]: 15 records of 0x128 bytes): a world object
// (0x6DEFB0 + 0x2C·k: the wares it offers [+0x0], [+0x1], those it wants [+0x2], [+0x3], its place [+0x4] and kind
// [+0x5], who found it first [+0x6], when [+0x8]) shown with the finds' bit k, found by then (its tick not past the
// world's) by a merchant, its place (0x673858 + 4·place, scaled on the Mediterranean as the points but in 16 bits:
// (x − 186)·100 as a short, then divided by 53) inside the map.
struct TradingCentre {
	uint32_t object = 0;
	int16_t x = 0;  // On the map ([+0x4], [+0x6] of the window's record).
	int16_t y = 0;
	uint8_t place = 0;  // Its name 0x6B1F20 + 4·place below 30 (with the kind 0x6C0840 + 4·kind), else «indian tribe».
	uint8_t kind = 0;
	uint8_t finder = 0;
	uint32_t tick = 0;
	std::array<uint8_t, 2> offer{};  // A first ware past the wares (0x18 or more): «-/-».
	std::array<uint8_t, 2> demand{};
};
std::vector<TradingCentre> trading_centres(const SaveGame& save, const TradingMap& map);
// 0x5BFC40 (the mouse on the page): the centre nearest to (x, y) of the map whose distance² is below `limit` (the
// game's 225), the first of the nearest; −1 for none (the window's [+0xC4] 0xFFFF).
int trading_centre_at(const std::vector<TradingCentre>& centres, int x, int y, int limit = 225);

}  // namespace p3::data
