#pragma once

// The campaigns (roadmap section 3, «Las campañas y el tutorial»; docs/formats/missions.md): what starting one does.
// The game's campaign screen (*0x6CBB6C, 0x42FBF0) lists the [Campaign_1..7] of scripts/missions_eng.ini (the
// tutorial, [Campaign_0], has its own start); its «Start» (0x4306FB..0x4309D7) loads the campaign n's save,
// MISSAV\<n>.pat (missav/1..7.pat), through the game's load 0x4335B0 in the mode 4, and then starts it.

#include <cstdint>

namespace p3::data {

struct SaveGame;

// What the options screen (*0x6CBDF0, P2.CFG [OPTIONS]) gives a game's load (0x4335B0): three display options it
// writes into the settings, and the game's speed.
struct LoadOptions {
	uint8_t resolution = 1;  // AUFLOESUNG, the options' [+0x168] → the settings' [+0x1F].
	uint8_t colour_depth = 1;  // FARBTIEFE, [+0x164] → [+0x20].
	uint8_t details = 3;  // DETAILS (0..3), [+0x16C] → [+0x24].
};

// 0x5421E0, the operation 0xA8 ({0xA8, u32 a player of the session, u32 a merchant}; the network game's lobby and a
// game's load): a merchant within the count becomes a human ([+0x8] = 0) and, the local player's (*0x6DF31C), the
// player (0x6DFC14). In a network game the lobby's seats too (0x43F660..: not in p3vita).
void fn_5421e0(SaveGame& save, const uint8_t* operation);

// What a game's load does once its file is read, but the interface (0x5476E0 from 0x547985, then 0x4335B0 from
// 0x433747): every merchant from the 20th on with [+0x8] 0 (a human) marked 0x8001; the settings' mode [+0xD] =
// `mode` (the folder's: 3 a single player game, 4 the campaigns' and the tutorial's MISSAV, 5 a campaign's saves, 0
// the network's) and the three display options; then, for a load of the saved games' screen, a campaign or the
// tutorial (its [+0x11BC] 3 or more: `from_screen`), the operation 0xA8 (0x5421E0) run on the first merchant from the
// 20th on whose [+0x8] has the bit 15 (none: nothing): his [+0x8] = 0 and, being the local player's, he is the player
// (0x6DFC14); and the operation 0xC8 queued (the time's mode 0). The battles' 0x611E50 with the player, after it, is the
// caller's (Sailing).
void after_game_load(SaveGame& save, uint8_t mode, bool from_screen, const LoadOptions& options);

// A campaign's start once its save is loaded (after_game_load in the mode 4): the mailbox emptied (0x4D64B0), the
// save's mission scripts taken out (0x4D8E70 with 0x1B), the settings' [+0xD] = 5, [+0x14] = `campaign` (1..7) and
// [+0x4] = `speed` (the game's speed before: the options' [+0x17C], kept at 0x430947), the list of Kampagne<n> with
// its missions started (0x511270 with 1) and 0x54A480.
void start_campaign(SaveGame& save, uint8_t campaign, uint32_t speed);

namespace campaigns_detail {
// 0x54A480 (on the game's control object 0x6DF2F0; only a campaign's start calls it; not in a network game): the
// trade's difficulty (0x52E1A0 with the settings' [+0x10]), the player's factor (0x4F9550 with [+0x11]: a human's
// [+0x464] 1.3, 1 or 0.7) and the pirates by [+0x13]: the first 2·[+0x13] + 1 of the five, made if missing (0x513720
// and 0x513D80 with two rand(): the town the second, the number the first) or else their [+0x14] = [+0x13] & 3; the
// others' [+0x15] = 1.
void fn_54a480(SaveGame& save);
}  // namespace campaigns_detail

}  // namespace p3::data
