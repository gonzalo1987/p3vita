#pragma once

// A new game (roadmap, phase 2, «Partida nueva desde cero»): what the game does
// when the player starts one from its new game window. The window writes the
// settings (*0x6CC3E8) and reads the map (Maps/<name>.gmf) into a setup
// object; then 0x544E50 runs the new game's operations on it. The notes:
// re/emu/sesion24/new_game_notes.md; checked with tools/oracle/newgame_oracle.py.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

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

// The new game window («Game settings», 0x496C90; *0x6D1C7C): what the player
// chooses in it, its fields. The window starts with the last choices, kept in
// P2.CFG [SPIELEINST] (and the speed in [OPTIONS]), read at 0x497B5D..0x497E60
// with these defaults (P2.CFG's of the game's installation; the family name's
// default in the exe is «Strohmberg», P2.CFG's «Stromberg»).
struct NewGameChoices {
	std::string first_name = "Georg";  // VORNAME, [+0x2E8] (Latin-1).
	std::string family_name = "Stromberg";  // NAME, [+0x168].
	bool female = false;  // GESCHLECHT, [+0x46C].
	uint32_t home = 9;  // STADT, [+0x468]: the home town's index in the map (a Hanse town).
	uint32_t year = 1345;  // STARTJAHR, [+0x1BA0]: 1300..1400, a multiple of 5.
	uint32_t level = 0;  // SCHWIERIGKEIT, [+0x1BA4]: 0..4 (Shopkeeper .. Patrician).
	uint32_t goal = 0;  // ZIEL, [+0x1BA8]: the game's kind, 0 (none) or 2..6.
	uint32_t goal_time = 0;  // LAUFZEIT, [+0x1BB0]: the goal's time (by the goal: 0..14, 0..13, 0..2).
	uint32_t speed = 0;  // ZEIT ([OPTIONS]), [+0x1BAC]: 0..5 («very slow» .. «very fast», «slowdown»).
	uint8_t production = 1;  // PRODUKTION, [+0x474]: the map's productions (1) or not (0).
	// DETAILS, [+0x1B80 + 4k]: the 8 fine settings, 1..3 (the last 1..2): trade,
	// reputation, the citizens' needs, sea battles, pirates, the prince, the
	// starting capital, the start (1: two ships, 2: one). The level sets them
	// (kNewGameLevelDetails); the «Details» window changes them one by one.
	std::array<uint8_t, 8> details = {1, 1, 1, 1, 1, 1, 1, 2};
};

// The details each level sets (0x66F608: 5 × 8 u32).
extern const std::array<std::array<uint8_t, 8>, 5> kNewGameLevelDetails;

// The choices as the window reads them from P2.CFG (0x497C31..0x497E5A): the
// year out of 1300..1400 becomes 1350, then a multiple of 5; a level out of
// 0..4 becomes 2; a goal out of 0..7, or 1 or 7, becomes 0; the goal's time
// within the goal's limits (table 0x497EB8); the speed out of 0..5 becomes
// `speed_before` (*0x6CC3E8 [+0x4]); the details 1..3, the last at most 2.
void clamp_new_game_choices(NewGameChoices& choices, uint32_t speed_before);

// The level whose presets' sum is nearest to the details' (0x498EFD, after the
// «Details» window: the sums 9, 12, 16, 19, 23 of table 0x66F6B0; the first of
// two as near).
uint32_t new_game_level_of(const std::array<uint8_t, 8>& details);

// The window's balls (scripts/options.ini: ← → for the sex and the goal, − +
// for the others), in its order (the balls 0, 1, 4, 5, 8..15 of [+0xAEC]; the
// home town's and the production's, 2, 3, 16 and 17, are hidden in this version).
enum class NewGameBall {
	GenderLeft, GenderRight, YearMinus, YearPlus, LevelMinus, LevelPlus, GoalLeft, GoalRight, TimeMinus, TimePlus,
	SpeedMinus, SpeedPlus, Count
};

// The window's choices and what it keeps besides: the goal's time put aside
// while the goal is the Hanse's population (*0x6D1C74; the window's handler
// sets it from the choices the first time it runs, 0x49879A).
struct NewGameWindowState {
	NewGameChoices choices;
	uint32_t saved_time = 0;
	bool saved = false;
};

// A ball pressed (the window's handler 0x498760, by the jump table 0x499960,
// and 0x499AF0 after the goal's): the sex, the year by 5 (1300..1400), the
// level with its details, the goal (without «Councillor», the time to 0 or put
// aside), the goal's time within the goal's limit, the speed («slowdown» below
// «very slow»).
void press_new_game_ball(NewGameWindowState& state, NewGameBall ball);
// Whether a ball can be pressed (0x49A2D0, 0x499AF0 and each handler's), and
// whether it shows (the goal's time only with a goal, 0x499AF0).
bool new_game_ball_enabled(const NewGameChoices& choices, NewGameBall ball);
bool new_game_ball_shown(const NewGameChoices& choices, NewGameBall ball);
// The goal's time limit by the goal (0x499AF0's table 0x499BBC): 0 without a
// goal, 14 for the goals 1..3, 13 for 4 and 5, 2 for 6 and 4 for 7.
uint32_t new_game_goal_time_limit(uint32_t goal);

// The map's next and previous Hanse town (flags 0x40), going round (0x544E00
// and 0x544E30 of the setup object; the map must have one).
uint32_t next_hanse_town(const GameMap& map, uint32_t index);
uint32_t previous_hanse_town(const GameMap& map, uint32_t index);
// The name of the map's town `index` (0x544DE0: the slot of its number; past
// the last town, the first's).
const std::string& game_map_town_name(const GameMap& map, uint32_t index);
// The wares the home town's window lists for the map's town `index` (0x49C410,
// as indices of the wares' names, 0x6C0BB0): of its efficient ([+0x4]) or
// inefficient ([+0xA4]) productions, the bits 0..16 by table 0x672C30, then the
// bit 6's leather and the bit 17's whale oil and fish. The window sorts them
// by name (strcmp).
std::vector<uint8_t> game_map_town_wares(const GameMap& map, uint32_t index, bool efficient);

// The settings at the game's start, before any game (0x463ADD: the year 1345,
// [0xB] 12, [0xA] and [0xC] 1, [0x17] 1, the rest 0; the PC's options in
// [0x1F..0x26] (0x46406D), here only TOOLTIPS and VIDEOS, 1 as the game's
// P2.CFG; the rest of operator new's memory, here 0).
SettingsMemory initial_game_settings();

// The settings the window writes when its «Start» is pressed (0x499291..0x499505)
// over `before`, what *0x6CC3E8 held (initial_game_settings, or the last
// game's): the year, the speed, the game's kind and goal, the details, the map's
// productions, 12 AI merchants, a game of one player ([0xD] 3), the options'
// TOOLTIPS in [0x25], the home town and its name.
SettingsMemory new_game_settings(const NewGameChoices& choices, const GameMap& map, const SettingsMemory& before,
	bool tooltips = true);
// What the window sends besides (0x49958D, 0x499600..0x499842): the ships of
// the start chosen (table 0x66F6B0 by the last detail), the sex and the names.
NewGamePlayer new_game_player(const NewGameChoices& choices);

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
