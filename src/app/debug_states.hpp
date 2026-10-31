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

}  // namespace p3::app
