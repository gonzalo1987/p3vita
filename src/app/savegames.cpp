#include "savegames.hpp"

#include <sys/stat.h>

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

std::string slot_path(const std::string& dir, int slot, const char* extension) {
	return dir + "slot" + std::to_string(slot + 1) + extension;
}

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

}  // namespace

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

SlotInfo read_slot_info(const std::string& dir, int slot) {
	SlotInfo info;
	std::vector<uint8_t> packed;
	if (!read_file(slot_path(dir, slot, ".pat"), packed)) {
		return info;
	}
	info.used = true;
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
	const uint8_t hometown = header[kSettings + 0x1E];
	if (hometown < 40) {
		info.town_number = header[kWorld + 0x20 + hometown];
	}
	return info;
}

bool save_slot(const std::string& dir, int slot, SaveGame& save, std::string& error) {
	if (!make_dir(dir)) {
		error = "can't create " + dir;
		return false;
	}
	const std::vector<uint8_t> pat = savegame_file(save);
	const std::vector<uint8_t> pst = statistics_file(save);
	const std::string pat_path = slot_path(dir, slot, ".pat");
	const std::string pst_path = slot_path(dir, slot, ".pst");
	if (!write_file(pat_path + ".tmp", pat) || !write_file(pst_path + ".tmp", pst) ||
		!replace_file(pat_path + ".tmp", pat_path) || !replace_file(pst_path + ".tmp", pst_path)) {
		error = "can't write " + pat_path;
		return false;
	}
	return true;
}

bool load_slot(const CprArchive& archive, const std::string& dir, int slot, SaveGame& out, std::string& error) {
	std::vector<uint8_t> packed;
	std::vector<uint8_t> data;
	if (!read_file(slot_path(dir, slot, ".pat"), packed)) {
		error = "can't read " + slot_path(dir, slot, ".pat");
		return false;
	}
	if (!lz_decompress_file(packed.data(), packed.size(), data, &error) ||
		!parse_savegame(data.data(), data.size(), out, &error)) {
		return false;
	}
	load_game_scripts(archive, out);
	// The .pst next to it, as the game reads it after the .pat (0x5476E0).
	std::vector<uint8_t> pst;
	load_statistics_file(read_file(slot_path(dir, slot, ".pst"), pst) ? &pst : nullptr, out);
	return true;
}

bool delete_slot(const std::string& dir, int slot) {
	const bool pat = std::remove(slot_path(dir, slot, ".pat").c_str()) == 0;
	std::remove(slot_path(dir, slot, ".pst").c_str());
	return pat;
}

}  // namespace p3::app
