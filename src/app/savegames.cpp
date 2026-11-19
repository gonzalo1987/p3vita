#include "savegames.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#include "p3data/lz.hpp"
#include "p3data/savegame_write.hpp"

using namespace p3::data;

namespace p3::app {

namespace {

// The start of a .pat (decompressed): u32 0xAFFE + version << 16, the settings
// (0x4C bytes), 3 u32, a separator and the world's header (0x60): u8 day,
// u8 month, u16 year, ... u8[40] the number of each town index at 0x20
// (docs/formats/savegame.md §2, §3). In the settings, [+0x1E] is the
// player's home town (an index), written by the game when it saves.
constexpr size_t kHeaderBytes = 0x60 + 0x20 + 40;
constexpr size_t kSettings = 4;
constexpr size_t kWorld = 0x60;

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
	std::FILE* file = std::fopen(path.c_str(), "rb");
	if (!file) {
		return false;
	}
	out.clear();
	uint8_t buffer[1 << 15];
	for (size_t n; (n = std::fread(buffer, 1, sizeof(buffer), file)) > 0;) {
		out.insert(out.end(), buffer, buffer + n);
	}
	const bool ok = !std::ferror(file);
	std::fclose(file);
	return ok;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& data) {
	std::FILE* file = std::fopen(path.c_str(), "wb");
	if (!file) {
		return false;
	}
	const bool written = std::fwrite(data.data(), 1, data.size(), file) == data.size();
	return std::fclose(file) == 0 && written;
}

// Replaces `path` with `temporary` (the Vita's rename doesn't replace).
bool replace_file(const std::string& temporary, const std::string& path) {
	std::remove(path.c_str());
	return std::rename(temporary.c_str(), path.c_str()) == 0;
}

bool make_dir(const std::string& dir) {
	std::string path = dir;
	while (!path.empty() && (path.back() == '/' || path.back() == '\\')) {
		path.pop_back();
	}
	if (path.empty() || mkdir(path.c_str(), 0777) == 0) {
		return true;
	}
	// Already there? (Not through errno: the Vita's may not say EEXIST.)
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string route_path(const std::string& dir, const std::string& name) {
	return dir + "AutoRoute/" + name + ".rou";
}

}  // namespace

std::vector<std::string> list_route_files(const std::string& dir) {
	std::vector<std::string> out;
	if (DIR* folder = opendir((dir + "AutoRoute").c_str())) {
		while (const dirent* entry = readdir(folder)) {
			const std::string name = entry->d_name;
			if (name.size() > 4) {
				std::string extension = name.substr(name.size() - 4);
				for (char& c : extension) {
					c = char(std::tolower(uint8_t(c)));
				}
				if (extension == ".rou") {
					out.push_back(name.substr(0, name.size() - 4));
				}
			}
		}
		closedir(folder);
	}
	std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
		for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
			const int x = std::tolower(uint8_t(a[i]));
			const int y = std::tolower(uint8_t(b[i]));
			if (x != y) {
				return x < y;
			}
		}
		return a.size() < b.size();
	});
	return out;
}

bool read_route_file(const std::string& dir, const std::string& name, std::vector<uint8_t>& out) {
	return read_file(route_path(dir, name), out);
}

bool write_route_file(const std::string& dir, const std::string& name, const std::vector<uint8_t>& data) {
	return make_dir(dir) && make_dir(dir + "AutoRoute") && write_file(route_path(dir, name), data);
}

bool delete_route_file(const std::string& dir, const std::string& name) {
	return std::remove(route_path(dir, name).c_str()) == 0;
}

std::string savegames_dir(const char* data_dir) {
#ifdef __vita__
	(void)data_dir;
	return "ux0:data/p3vita/savegames/";
#else
	if (data_dir && *data_dir) {
		std::string dir = data_dir;
		if (dir.back() != '/' && dir.back() != '\\') {
			dir += '/';
		}
		return dir + "savegames/";
	}
	return "savegames/";
#endif
}

namespace {

// Names as a Windows folder lists them: letters without case.
bool windows_order(const std::string& a, const std::string& b) {
	for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
		const int x = std::tolower(uint8_t(a[i]));
		const int y = std::tolower(uint8_t(b[i]));
		if (x != y) {
			return x < y;
		}
	}
	return a.size() < b.size();
}

// The .pat files of a folder (their titles), as the folder lists them.
std::vector<std::string> pat_titles(const std::string& dir) {
	std::vector<std::string> out;
	if (DIR* folder = opendir(dir.c_str())) {
		while (const dirent* entry = readdir(folder)) {
			const std::string name = entry->d_name;
			if (name.size() > 4) {
				std::string extension = name.substr(name.size() - 4);
				for (char& c : extension) {
					c = char(std::tolower(uint8_t(c)));
				}
				if (extension == ".pat") {
					out.push_back(name.substr(0, name.size() - 4));
				}
			}
		}
		closedir(folder);
	}
	std::sort(out.begin(), out.end(), windows_order);
	return out;
}

int64_t file_time(const std::string& path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0 ? int64_t(st.st_mtime) : 0;
}

}  // namespace

std::vector<SaveEntry> list_saves(const std::string& dir) {
	std::vector<SaveEntry> out;
	for (const std::string& title : pat_titles(dir + "ein")) {
		const std::string path = dir + "ein/" + title;
		out.push_back({title, path, file_time(path + ".pat")});
	}
	for (const std::string& title : pat_titles(dir.empty() ? std::string(".") : dir)) {
		const std::string path = dir + title;
		out.push_back({title, path, file_time(path + ".pat")});
	}
	return out;
}

std::string save_path(const std::string& dir, const std::string& name) {
	return dir + "ein/" + name;
}

bool valid_save_name(const std::string& name) {
	if (name.empty() || name.size() > 24 || name.back() == '.' || name.back() == ' ' || name.front() == ' ') {
		return false;
	}
	for (char c : name) {
		if (uint8_t(c) < 0x20 || std::strchr("/\\:*?\"<>|", c)) {
			return false;
		}
	}
	return true;
}

SaveInfo read_save_info(const std::string& path) {
	SaveInfo info;
	std::vector<uint8_t> packed;
	if (!read_file(path + ".pat", packed)) {
		info.damaged = true;
		return info;
	}
	uint8_t header[kHeaderBytes] = {};
	size_t produced = 0;
	lz_decompress_prefix(packed.data(), packed.size(), header, sizeof(header), produced);
	uint32_t tag = 0;
	std::memcpy(&tag, header, 4);
	if (produced < kHeaderBytes || (tag & 0xFFFF) != 0xAFFE) {
		info.damaged = true;
		return info;
	}
	info.day = header[kWorld];
	info.month = header[kWorld + 1];
	info.year = header[kWorld + 2] | header[kWorld + 3] << 8;
	const uint8_t* s = header + kSettings;
	const uint8_t hometown = s[0x1E];
	if (hometown < 40) {
		info.town_number = header[kWorld + 0x20 + hometown];
	}
	std::memcpy(&info.start_year, s, 4);
	std::memcpy(&info.speed, s + 4, 4);
	info.goal_time = s[0x08];
	info.capital = s[0x09];
	info.level = s[0x0E];
	info.goal = s[0x0F];
	info.needs = s[0x17];
	info.production = s[0x19];
	info.date_day = s[0x1A];
	info.date_month = s[0x1B];
	info.date_year = uint16_t(s[0x1C] | s[0x1D] << 8);
	for (size_t i = 0x27; i < 0x3B && s[i] != 0; ++i) {
		info.home += char(s[i]);
	}
	info.rank = s[0x3B];
	// (0x547E0A: the objectives' time at most 14 but for the population's.)
	if (info.goal != 6 && info.goal_time > 14) {
		info.goal_time = 14;
	}
	return info;
}

bool save_game_file(const std::string& path, SaveGame& save, std::string& error) {
	const size_t slash = path.find_last_of('/');
	const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
	const size_t parent = dir.size() > 1 ? dir.find_last_of('/', dir.size() - 2) : std::string::npos;
	if ((parent != std::string::npos && !make_dir(dir.substr(0, parent + 1))) || !make_dir(dir)) {
		error = "can't create " + dir;
		return false;
	}
	const std::vector<uint8_t> pat = savegame_file(save);
	const std::vector<uint8_t> pst = statistics_file(save);
	const std::string pat_path = path + ".pat";
	const std::string pst_path = path + ".pst";
	if (!write_file(pat_path + ".tmp", pat) || !write_file(pst_path + ".tmp", pst) ||
		!replace_file(pat_path + ".tmp", pat_path) || !replace_file(pst_path + ".tmp", pst_path)) {
		error = "can't write " + pat_path;
		return false;
	}
	return true;
}

bool load_game_file(const CprArchive& archive, const std::string& path, SaveGame& out, std::string& error) {
	std::vector<uint8_t> packed;
	std::vector<uint8_t> data;
	if (!read_file(path + ".pat", packed)) {
		error = "can't read " + path + ".pat";
		return false;
	}
	if (!lz_decompress_file(packed.data(), packed.size(), data, &error) ||
		!parse_savegame(data.data(), data.size(), out, &error)) {
		return false;
	}
	load_game_scripts(archive, out);
	// The .pst next to it, as the game reads it after the .pat (0x5476E0).
	std::vector<uint8_t> pst;
	load_statistics_file(read_file(path + ".pst", pst) ? &pst : nullptr, out);
	return true;
}

bool delete_game_file(const std::string& path) {
	const bool pat = std::remove((path + ".pat").c_str()) == 0;
	std::remove((path + ".pst").c_str());
	return pat;
}

}  // namespace p3::app
