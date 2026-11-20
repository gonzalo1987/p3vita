#pragma once

// The missions' interpreter (docs/formats/missions.md): 0x4ECF64, the event 0x1B of the event queue, which runs its
// script's program (missions.hpp) from its section [+0x14] until one waits or ends it.

#include <cstdint>

namespace p3::data {

struct SaveGame;
class Sailing;

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
// The operation 0x82 (0x5367A6 → 0x53FB00; queued by the instructions 0xF9, 0xE9 and 0xE7): a pirate for the mission.
// Unless the ship [+0x4] (a u16) is below the ships' count in a state other than 0xF and 0x10, a pirate attacks it
// (0x5063C0 with the kind [+0xB]); its captain ([+0x42], below the captains' count) gets [+0x6] and [+0x7] in his [+0x2]
// and [+0x3], and, for a town [+0xC] below the towns' count, the ship that town (0x51A820: its [+0x159], for a ship of
// no merchant) and its [+0x136] 0x80. Then the script of the event [+0x8] (below the events' count, an event 0x1B)
// gets in its variable [+0xA] (below its count) the captain's key ([+0x6] << 24 | [+0x7] << 16 | the captain), or −2
// when no ship came.
void fn_53fb00(SaveGame& save, Sailing* sailing, const uint8_t* operation);
// The player's operations on the mission letters (the letters' window and the tavern's; the scripts' variables get
// what he chose):
// 0x74 (0x536B56 → 0x53EC30; an answer to a mission letter 0x3C..0x40 of 0xF7): for the event [+0x4] below the
// events' count (else nothing): if a 0x1B, its variable [+0x8] (up to its count [+0x12]) gets [+0xC]; then the entry
// [+0x10] (up to the mailbox's count), if a letter 0x3C..0x40, its record's [+0x4] (its deadline) 0.
void fn_53ec30(SaveGame& save, const uint8_t* operation);
// 0x54 (0x53611A → 0x53CA30): for the town [+0xC], the ship [+0x4] (a u16) and the town [+0xD] below their counts, the
// letter 0x44 of the ship's owner for the town and the event [+0xE] (a u16) removed (0x4D7340, mail.cpp); its [+0xC]
// the variable of that event's script that gets the ship. Nobody queues it (and only dead code makes the letters).
void fn_53ca30(SaveGame& save, const uint8_t* operation);
// 0x55 (0x536129, inline; a treasure map's piece bought): the merchant [+0x4] pays [+0xC] for his letter 0x45/0x46
// [+0x8], which gets the bits [+0x10] >> 5 (0x4D74C0, mail.cpp); the town [+0x10] & 0x1F (below the count) its
// [+0x82C] 0xFF.
void fn_536129(SaveGame& save, const uint8_t* operation);
// 0x76 (0x536B74 → 0x53EDF0; the tavern's mission letter 0x71): the merchant [+0x10]'s letter 0x71 for the event [+0x4]
// and the key [+0x8] & 0xFF removed (0x4D7870, mail.cpp); if one went and the event (below the events' count) is a
// 0x1B: its variable [+0x8] >> 8 (below its count), if it holds a merchant (below the merchants' count; else nothing
// more), gets this one, and its variable [+0x8] & 0xFF (below the count) gets [+0xC].
void fn_53edf0(SaveGame& save, const uint8_t* operation);
}  // namespace missions_detail

}  // namespace p3::data
