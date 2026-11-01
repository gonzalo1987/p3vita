#pragma once

#include <string>

namespace p3::app {

// The viewer's settings, kept in a small text file with one «key = value» per
// line. A missing file or an unknown value leaves the default.
struct AppSettings {
	// p3::data::set_fix_game_bugs (p3data/game_fixes.hpp): the fixes to the
	// original game's bugs. On by default; off, the game behaves like the
	// original, bugs included.
	bool fix_game_bugs = true;
};

// ux0:data/p3vita/p3vita-settings.txt on the Vita; on the PC, in the data
// folder given on the command line, or else in the current folder.
std::string settings_path(const char* data_dir);
AppSettings load_settings(const std::string& path);
bool save_settings(const std::string& path, const AppSettings& settings);

}  // namespace p3::app
