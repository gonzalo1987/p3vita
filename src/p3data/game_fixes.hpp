#pragma once

namespace p3::data {

// Fixes to the bugs of the original game (docs/roadmap.md, «Corregir los
// fallos del juego»). Each fix is written next to the original behaviour:
//
//     if (fix_game_bugs()) {
//         ... what the game should have done ...
//     } else {
//         ... what the game does (bug included) ...
//     }
//
// Off by default in the library, so every tool that compares p3vita with the
// game (the oracle, tick_probe, the tests) gets the original behaviour. The
// viewer turns the fixes on from its settings screen (on by default there);
// tick_probe --fix-game-bugs shows what the fixes change.
bool fix_game_bugs();
void set_fix_game_bugs(bool enabled);

}  // namespace p3::data
