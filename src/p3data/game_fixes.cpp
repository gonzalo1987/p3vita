#include "p3data/game_fixes.hpp"

namespace p3::data {

namespace {
bool g_fix_game_bugs = false;
}  // namespace

bool fix_game_bugs() {
	return g_fix_game_bugs;
}

void set_fix_game_bugs(bool enabled) {
	g_fix_game_bugs = enabled;
}

}  // namespace p3::data
