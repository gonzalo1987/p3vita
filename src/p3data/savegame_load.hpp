#pragma once

#include "p3data/savegame.hpp"

namespace p3::data {

// The last steps of the game's loader (0x549F40, after reading the sections;
// docs/cloud/statistics.md §3) that need the scripts: the ship names
// (0x5131B0) and the trade routes (0x4D60B0). load_game_scripts calls it.
// The third one, the routes of the ships (0x5048C0), needs the navigation:
// Sailing::assign_loaded_routes does it.
void finish_load(SaveGame& save);

}  // namespace p3::data
