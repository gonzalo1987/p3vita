#pragma once

// A new game (roadmap, phase 2, «Partida nueva desde cero»): what the game does
// when the player starts one from its new game window. The window writes the
// settings (*0x6CC3E8) and reads the map (Maps/<name>.gmf) into a setup
// object; then 0x544E50 runs the new game's operations on it. The notes:
// re/emu/sesion24/new_game_notes.md; checked with tools/oracle/newgame_oracle.py.

#include <array>
#include <cstdint>
#include <string>

#include "p3data/savegame.hpp"

namespace p3::data {

class CprArchive;
class Sailing;

// A map (Maps/<name>.gmf, an LZ blob; the game's Standard24 is in the archive
// as maps/Standard24.gmf), as 0x5448A0 reads it into the setup object: the
// number of towns, for each its productions' bits (efficient [+0x4],
// inefficient [+0xA4]), its flags (0x40 a Hanse town) [+0x144], its number in
// StadtDaten.ini [+0x16C] and its map template [+0x194]; and the 40 slots'
// names (by town number, Latin-1) [+0x1BC].
struct GameMap {
	uint32_t count = 0;
	std::array<uint32_t, 40> efficient{};
	std::array<uint32_t, 40> inefficient{};
	std::array<uint8_t, 40> flags{};
	std::array<uint8_t, 40> numbers{};
	std::array<uint8_t, 40> templates{};
	std::array<std::string, 40> names;
};

// A decompressed .gmf (0x5448A0).
bool parse_game_map(const uint8_t* data, size_t size, GameMap& out);
// A .gmf of the archive (e.g. "maps/Standard24.gmf").
bool load_game_map(const CprArchive& archive, const std::string& path, GameMap& out, std::string* error = nullptr);

// 0x544E50 (on the setup object): the new game's operations, with the
// settings (*0x6CC3E8) already in save.settings and rand() seeded (0x547F10:
// srand(time)). Each is run at once (run_operation with `sailing`), as in a
// game of one player; the last, 0xC4 (the world 70 days on), is queued in
// save.queued_operations.
void fn_544e50(SaveGame& save, Sailing* sailing, const GameMap& map);

// The new game's operations (run_operation, simulation.cpp), each the
// operation's 0x14 bytes.
// 0xB7 (0x536F0E → 0x5434E0): the game starts: the mailbox, the trade routes,
// the statistics, the ships and the world emptied, the event queue with its
// first events, the trade's difficulty.
void fn_5434e0(SaveGame& save, const uint8_t* operation);
// 0xB1 (0x536E50 → 0x542860; only with no merchants yet): each town's merchant
// (0x542560) with his office and four ships, the travel times (0x531BA0) and
// the road between Hamburg and Lübeck (0x533510).
void fn_542860(SaveGame& save, const uint8_t* operation);
// 0xAC (0x536DF2 → 0x542420): [0x6DD89A..0x6DD89F] by the operation's [+0x8],
// the travel times again (0x531BA0), the mailbox emptied (0x4D64B0) and the
// game's goal, an event 0x0E a day later with the game's kind and goal.
void fn_542420(SaveGame& save, const uint8_t* operation);
// 0xAD (0x536E01 → 0x52BD80 on the world object [+0x10] < 15, the record at
// [+0x4]): its towns and wares drawn from tables 0x673944, 0x673920 and
// 0x67392C, its counts cleared and its list emptied.
void fn_52bd80(SaveGame& save, const uint8_t* operation);
// 0xB3 (0x536E6E → 0x542A30): the AI merchants ([+0x4] of them, at most 30), each
// in the next Hanse town with an event 0x04, a new life, names, his office,
// 480000 in [+0x53C], the operation's money and [+0x6] ships (the first a
// snaikka, then cogs unless his town is on a river), his day (0x4F7BB0) and
// his statistics (0x54E170). It writes the count back into the operation when
// it is above 30.
void fn_542a30(SaveGame& save, uint8_t* operation);
// 0xB0 (0x536F1D → 0x543570): 13 characters ([+0x4]) of the name of the slot
// [+0x11] (a town number) from [+0x13] on (0x512830: SaveOpaque::town_names).
void fn_543570(SaveGame& save, const uint8_t* operation);
// 0xA7 (0x541D70): a human merchant (0x530470 with the chronicle's letter) in
// the town [+0x9] with the money of [+0xA] (table 0x672D20), a new life, a
// players' names slot (0x512BA0), his office and [+0xB] ships (the last a
// craier of level 2 with [+0x8] = 0); the local player's (*0x6DF31C) becomes
// the player; in a game of one player the real-time loop's 0xC0, 0xC6 and
// 0xC8 are queued. It writes into the operation (the town, the ships' count).
void fn_541d70(SaveGame& save, Sailing* sailing, uint8_t* operation);
// 0xAA and 0xAB (0x536D90 → 0x512C50): up to 12 characters ([+0x8]) more of
// the first name (0xAA) or family name (0xAB) of the merchant [+0x4], in his
// players' names slot (SaveGame::custom_names), at most 23 in all.
void fn_536d90(SaveGame& save, const uint8_t* operation);

// What the new game window sends besides the settings: the player's ships
// (the 0xA7's [+0xB]: by the window's start, 2 in the test saves' settings),
// sex and names (Latin-1).
struct NewGamePlayer {
	uint8_t ships = 2;
	bool male = true;
	std::string first_name;
	std::string family_name;
};

// Where start_new_game stops (for the checks): after 0x544E50, after the
// world's 70 days, or at the end.
enum class NewGameStop { Operations, Advance, Player };

// A new game as the window starts it (0x499252..0x49985C), on a save with the
// game's data (load_game_scripts) and the settings in save.settings: srand
// with `seed` (the game: the time), 0x544E50 on the map, then the queue: the
// 0xC4 (advance_time to 70 days later, its operations held), the player
// (0xA7) and 0xC8, then the player's names (0xAA, 0xAB) and the operations
// the days queued.
void start_new_game(SaveGame& save, Sailing& sailing, const GameMap& map, const NewGamePlayer& player, uint32_t seed,
	NewGameStop stop = NewGameStop::Player);

}  // namespace p3::data
