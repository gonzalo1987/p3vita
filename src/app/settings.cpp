#include "settings.hpp"

#include <cstdio>

namespace p3::app {

namespace {

constexpr const char* kFileName = "p3vita-settings.txt";
constexpr const char* kFixGameBugs = "fix_game_bugs";
constexpr const char* kLanguage = "language";

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
		} else if (key == kLanguage && (value == "es" || value == "en")) {
			settings.spanish = value == "es";
		}
	}
	std::fclose(file);
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
	return std::fclose(file) == 0;
}

}  // namespace p3::app
