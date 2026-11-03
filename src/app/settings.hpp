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
	// The language of the game's texts and data: Spanish (by default: the
	// Spanish archive p2arch0_spa.cpr mounted on the English one, and the
	// Spanish Patrician3.exe) or English (the English archive alone and the
	// English exe).
	bool spanish = true;
};

// ux0:data/p3vita/p3vita-settings.txt on the Vita; on the PC, in the data
// folder given on the command line, or else in the current folder.
std::string settings_path(const char* data_dir);
// The folder of the settings file (with its final '/', or empty for the
// current folder): the viewer keeps its own files there.
std::string data_folder(const char* data_dir);
AppSettings load_settings(const std::string& path);
bool save_settings(const std::string& path, const AppSettings& settings);

}  // namespace p3::app
