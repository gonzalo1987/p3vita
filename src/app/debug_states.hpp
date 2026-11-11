#pragma once

// Debugging states for the viewer (--debug-state): they put the player where
// the saves never are, to see and screenshot each step of the path to
// Alderman, as the scenarios `jugador` and `candidato` of
// tools/oracle/retouch.py do in the emulator.

#include <string>

#include "p3data/savegame.hpp"

namespace p3::app {

// Comma-separated states (see kDebugStatesHelp), applied to the loaded save
// in order. False and `error` for an unknown one.
bool apply_debug_states(p3::data::SaveGame& save, const std::string& states, std::string& error);

extern const char kDebugStatesHelp[];

// weak-towers: a town battle's towers with less strength each (from the first's smoke alone to the fourth's
// middling fire, 0x6018DB), when the viewer opens its screen. Only for the eyes.
void weaken_towers(p3::data::SaveGame& save, size_t battle);

// nest-attack: while the player's first ship sails and no battle at the first pirate's nest has started, the
// nest's place is moved to the ship (each frame); false once there is nothing more to do.
bool follow_nest_attack(p3::data::SaveGame& save);

}  // namespace p3::app
