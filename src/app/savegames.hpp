#pragma once

// The player's saved games, by name as the game keeps them (its save/ein/NAME.pat and NAME.pst, the single player's)
// in a folder (ux0:data/p3vita/savegames/ on the Vita), each a .pat and its .pst as the game writes them
// (p3data/savegame_write.hpp); the viewer's older slots (slotN.pat, slotN.pst) are listed too.

#include <cstdint>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/savegame.hpp"

namespace p3::app {

// ux0:data/p3vita/savegames/ on the Vita; on the PC, savegames/ in the data
// folder given on the command line, or else in the current folder.
std::string savegames_dir(const char* data_dir);

// What the saved games' screen shows of a save, from the start of its .pat: the world's date and the player's home
// town (the world's header), and its game's settings as the screen's details box reads them (0x547BE0: the 0x4C bytes
// after the file's first u32; the objectives' time at most 14 unless the objectives are the population's, 6).
struct SaveInfo {
	bool damaged = false;  // It can't be read.
	int day = 0;
	int month = 0;  // 0 = January.
	int year = 0;
	int town_number = -1;  // The home town's number (0..39, scripts/StadtDaten.ini), or -1.
	// The settings: [+0x00] the starting year, [+0x04] the speed (0..5), [+0x08] the objectives' time (0..14, 16..18
	// the population's), [+0x09] the starting capital, [+0x0E] the difficulty, [+0x0F] the objectives, [+0x17] the
	// citizens' needs, [+0x19] the towns' production, [+0x1A] the game's date (day, month, u16 year), [+0x27] the home
	// town's name (Latin-1, up to 20), [+0x3B] the rank (bit 7: a man's titles).
	uint32_t start_year = 0;
	uint32_t speed = 0;
	uint8_t goal_time = 0;
	uint8_t capital = 0;
	uint8_t level = 0;
	uint8_t goal = 0;
	uint8_t needs = 0;
	uint8_t production = 0;
	uint8_t date_day = 0;
	uint8_t date_month = 0;
	uint16_t date_year = 0;
	std::string home;
	uint8_t rank = 0;
};

// A saved game in the list: its file's title, its path without «.pat», when it was written.
struct SaveEntry {
	std::string name;
	std::string path;
	int64_t time = 0;  // Seconds since 1970 (its file's modification time).
};

// The single player's saved games by name (0x4343E0 lists ./save/ein/*.pat): ein/ in the saved games' folder `dir`,
// sorted as a Windows folder lists them (by name, letters without case); then the viewer's older slots (slotN.pat in
// `dir`).
std::vector<SaveEntry> list_saves(const std::string& dir);
SaveInfo read_save_info(const std::string& path);
// A save's path by its name (without «.pat»), and whether a name can be a file's (not empty, at most 24 characters,
// none of a path's or Windows' forbidden ones, not ending in a dot or a space).
std::string save_path(const std::string& dir, const std::string& name);
bool valid_save_name(const std::string& name);

// Saves `save` as the game does (it expires the old letters and updates the settings first, as 0x5472F0 does) at the
// path (its .pat and .pst). The folders are created if they aren't there; the old files are replaced only once the new
// ones are written.
bool save_game_file(const std::string& path, p3::data::SaveGame& save, std::string& error);
// Loads the save at the path into `out` (with the game's scripts and its .pst).
bool load_game_file(const p3::data::CprArchive& archive, const std::string& path, p3::data::SaveGame& out,
	std::string& error);
bool delete_game_file(const std::string& path);

// The route files (the route page's «File»; the game's save\AutoRoute\NAME.rou):
// AutoRoute/ in the saved games' folder `dir`. Their names without «.rou»,
// sorted as a Windows folder lists them (letters without case); a file's bytes
// read, written (the folder made if it isn't there) or deleted.
std::vector<std::string> list_route_files(const std::string& dir);
bool read_route_file(const std::string& dir, const std::string& name, std::vector<uint8_t>& out);
bool write_route_file(const std::string& dir, const std::string& name, const std::vector<uint8_t>& data);
bool delete_route_file(const std::string& dir, const std::string& name);

}  // namespace p3::app
