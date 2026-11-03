#pragma once

// The player's saved games: a few slots in a folder (ux0:data/p3vita/savegames/
// on the Vita), each a .pat and its .pst as the game writes them
// (p3data/savegame_write.hpp), named slotN.pat and slotN.pst.

#include <string>

#include "p3data/cpr.hpp"
#include "p3data/savegame.hpp"

namespace p3::app {

constexpr int kSaveSlots = 8;

// ux0:data/p3vita/savegames/ on the Vita; on the PC, savegames/ in the data
// folder given on the command line, or else in the current folder.
std::string savegames_dir(const char* data_dir);

// What the list shows of a slot, from the start of its .pat (the settings
// and the world's header): the date and the player's home town.
struct SlotInfo {
	bool used = false;  // There is a file.
	bool damaged = false;  // It can't be read.
	int day = 0;
	int month = 0;  // 0 = January.
	int year = 0;
	int town_number = -1;  // The home town's number (0..39, scripts/StadtDaten.ini), or -1.
};

SlotInfo read_slot_info(const std::string& dir, int slot);

// Saves `save` in the slot as the game does (it expires the old letters and
// updates the settings first, as 0x5472F0 does). The folder is created if it
// isn't there; the old files are replaced only once the new ones are written.
bool save_slot(const std::string& dir, int slot, p3::data::SaveGame& save, std::string& error);

// Loads the slot into `out` (with the game's scripts and its .pst).
bool load_slot(const p3::data::CprArchive& archive, const std::string& dir, int slot, p3::data::SaveGame& out,
	std::string& error);

bool delete_slot(const std::string& dir, int slot);

}  // namespace p3::app
