#include "settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "p3data/cpr.hpp"

namespace p3::app {

namespace {

constexpr const char* kFileName = "p3vita-settings.txt";
constexpr const char* kFixGameBugs = "fix_game_bugs";
constexpr const char* kLanguage = "language";
constexpr const char* kTooltips = "tooltips";
constexpr const char* kMusicVolume = "music_volume";
constexpr const char* kSoundVolume = "sound_volume";
constexpr const char* kSwapStereo = "swap_stereo";
// The new game window's choices, as P2.CFG's [SPIELEINST] keys (the names in UTF-8).
constexpr const char* kFirstName = "new_game_first_name";
constexpr const char* kFamilyName = "new_game_family_name";
constexpr const char* kFemale = "new_game_female";
constexpr const char* kHome = "new_game_home_town";
constexpr const char* kYear = "new_game_year";
constexpr const char* kLevel = "new_game_level";
constexpr const char* kGoal = "new_game_goal";
constexpr const char* kGoalTime = "new_game_goal_time";
constexpr const char* kSpeed = "new_game_speed";
constexpr const char* kProduction = "new_game_production";
constexpr const char* kDetails = "new_game_details";

std::string trim(const std::string& s) {
	const size_t first = s.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return "";
	}
	const size_t last = s.find_last_not_of(" \t\r\n");
	return s.substr(first, last - first + 1);
}

// 1/0, yes/no, true/false, on/off; anything else leaves `value` unchanged.
void parse_bool(const std::string& text, bool& value) {
	if (text == "1" || text == "yes" || text == "true" || text == "on") {
		value = true;
	} else if (text == "0" || text == "no" || text == "false" || text == "off") {
		value = false;
	}
}

}  // namespace

std::string data_folder(const char* data_dir) {
#ifdef __vita__
	(void)data_dir;
	return "ux0:data/p3vita/";
#else
	if (data_dir && *data_dir) {
		std::string dir = data_dir;
		if (dir.back() != '/' && dir.back() != '\\') {
			dir += '/';
		}
		return dir;
	}
	return "";
#endif
}

std::string settings_path(const char* data_dir) {
	return data_folder(data_dir) + kFileName;
}

// C stdio rather than <fstream>: iostreams add about 300 KB to the Vita's VPK.
AppSettings load_settings(const std::string& path) {
	AppSettings settings;
	std::FILE* file = std::fopen(path.c_str(), "r");
	if (!file) {
		return settings;
	}
	char buffer[256];
	while (std::fgets(buffer, sizeof buffer, file)) {
		const std::string line = buffer;
		const size_t equals = line.find('=');
		if (line.empty() || line[0] == '#' || equals == std::string::npos) {
			continue;
		}
		const std::string key = trim(line.substr(0, equals));
		const std::string value = trim(line.substr(equals + 1));
		if (key == kFixGameBugs) {
			parse_bool(value, settings.fix_game_bugs);
		} else if (key == kTooltips) {
			parse_bool(value, settings.tooltips);
		} else if (key == kMusicVolume || key == kSoundVolume) {
			// 0..127; anything else leaves the default.
			char* end = nullptr;
			const long v = std::strtol(value.c_str(), &end, 10);
			if (end != value.c_str() && v >= 0 && v <= 127) {
				(key == kMusicVolume ? settings.music_volume : settings.sound_volume) = int(v);
			}
		} else if (key == kSwapStereo) {
			parse_bool(value, settings.swap_stereo);
		} else if (key == kLanguage && (value == "es" || value == "en")) {
			settings.spanish = value == "es";
		} else if (key == kFirstName) {
			settings.new_game.first_name = p3::data::utf8_to_latin1(value);
		} else if (key == kFamilyName) {
			settings.new_game.family_name = p3::data::utf8_to_latin1(value);
		} else if (key == kFemale) {
			parse_bool(value, settings.new_game.female);
		} else if (key == kHome) {
			settings.new_game.home = uint32_t(std::strtol(value.c_str(), nullptr, 10));
		} else if (key == kYear) {
			settings.new_game.year = uint32_t(std::strtol(value.c_str(), nullptr, 10));
		} else if (key == kLevel) {
			settings.new_game.level = uint32_t(std::strtol(value.c_str(), nullptr, 10));
		} else if (key == kGoal) {
			settings.new_game.goal = uint32_t(std::strtol(value.c_str(), nullptr, 10));
		} else if (key == kGoalTime) {
			settings.new_game.goal_time = uint32_t(std::strtol(value.c_str(), nullptr, 10));
		} else if (key == kSpeed) {
			settings.new_game.speed = uint32_t(std::strtol(value.c_str(), nullptr, 10));
		} else if (key == kProduction) {
			settings.new_game.production = std::strtol(value.c_str(), nullptr, 10) != 0 ? 1 : 0;
		} else if (key == kDetails) {
			// «1|1|1|1|1|1|1|2», as P2.CFG's DETAILS.
			const char* p = value.c_str();
			for (uint8_t& d : settings.new_game.details) {
				d = uint8_t(std::strtol(p, nullptr, 10));
				const char* bar = std::strchr(p, '|');
				if (!bar) {
					break;
				}
				p = bar + 1;
			}
		}
	}
	std::fclose(file);
	p3::data::clamp_new_game_choices(settings.new_game, 0);
	return settings;
}

bool save_settings(const std::string& path, const AppSettings& settings) {
	std::FILE* file = std::fopen(path.c_str(), "w");
	if (!file) {
		return false;
	}
	std::fprintf(file, "# p3vita settings\n");
	std::fprintf(file, "# fix_game_bugs: 1 fixes the original game's bugs, 0 behaves like the original\n");
	std::fprintf(file, "%s = %d\n", kFixGameBugs, settings.fix_game_bugs ? 1 : 0);
	std::fprintf(file, "# language: es (Spanish) or en (English)\n");
	std::fprintf(file, "%s = %s\n", kLanguage, settings.spanish ? "es" : "en");
	std::fprintf(file, "# tooltips: 1 shows the buttons' tooltips under a finger held on them (the options' «Tooltips»)\n");
	std::fprintf(file, "%s = %d\n", kTooltips, settings.tooltips ? 1 : 0);
	const p3::data::NewGameChoices& g = settings.new_game;
	std::fprintf(file, "# the new game window's last choices\n");
	std::fprintf(file, "%s = %s\n", kFirstName, p3::data::latin1_to_utf8(g.first_name).c_str());
	std::fprintf(file, "%s = %s\n", kFamilyName, p3::data::latin1_to_utf8(g.family_name).c_str());
	std::fprintf(file, "%s = %d\n", kFemale, g.female ? 1 : 0);
	std::fprintf(file, "%s = %u\n", kHome, unsigned(g.home));
	std::fprintf(file, "%s = %u\n", kYear, unsigned(g.year));
	std::fprintf(file, "%s = %u\n", kLevel, unsigned(g.level));
	std::fprintf(file, "%s = %u\n", kGoal, unsigned(g.goal));
	std::fprintf(file, "%s = %u\n", kGoalTime, unsigned(g.goal_time));
	std::fprintf(file, "%s = %u\n", kSpeed, unsigned(g.speed));
	std::fprintf(file, "%s = %u\n", kProduction, unsigned(g.production));
	std::fprintf(file, "%s = %u|%u|%u|%u|%u|%u|%u|%u\n", kDetails, g.details[0], g.details[1], g.details[2],
		g.details[3], g.details[4], g.details[5], g.details[6], g.details[7]);
	return std::fclose(file) == 0;
}

}  // namespace p3::app
