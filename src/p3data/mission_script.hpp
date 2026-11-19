#pragma once

// The missions' interpreter (docs/formats/missions.md): 0x4ECF64, the event 0x1B of the event queue, which runs its
// script's program (missions.hpp) from its section [+0x14] until one waits or ends it.

#include <cstdint>

namespace p3::data {

struct SaveGame;

namespace missions_detail {
// 0x4ECF64 (on the event queue, the event 0x1B `event`; named fn_4ecf64 once its handlers are all rewritten): without
// a program, its variables go and it ends (0). Else
// its sections run, one instruction each, from [+0x14]: each goes on to the next unless it jumps; 0xFD waits so many
// ticks (its variable) and goes on at its section; 0x00 waits for a tick (ends if it has passed); 0xFE waits so many
// days and starts again at the section 0; 0xFF and an unknown instruction end it. 1 while it waits (the event's tick
// [+0x0] changed), 0 when it ends (its program let go, 0x4F37E0, and its variables gone).
uint32_t run_mission_script(SaveGame& save, uint16_t event);
// The instructions p3vita doesn't run yet (while the interpreter is being rewritten: they end the script, as an
// unknown one): how many times each one has been met since the program started.
const uint32_t* unwritten_mission_ops();
}  // namespace missions_detail

}  // namespace p3::data
