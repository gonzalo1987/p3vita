#pragma once

// The votes of the human players: operation 0x3F (0x53AA10), queued by the
// Hanse window (0x5E8280) in the election of the Alderman (event 0x20) and in
// the votes of the Hanse diet (event 0x21). See docs/cloud/alderman-viewer-2.md.
//
// Not called by run_operation yet (simulation.cpp belongs to the local
// session): the case to add is in the notes.

#include <cstdint>

#include "p3data/savegame.hpp"

namespace p3::data::tick {

// 0x4D9C70 (on the event queue): the first event 0x20 or 0x21 of today (the
// walk stops at the first event of a later day), or 0xFFFF.
uint16_t fn_4d9c70(const SaveGame& save);

// 0x4D9D40: whether the player can vote now: rank >= 4 in its home town and
// its bit (its number among the human players) still set in today's
// election ([+0x14]) or diet vote ([+0x16]).
bool fn_4d9d40(const SaveGame& save);

// 0x53AA10 (operation 0x3F: {0x3F, u32 merchant, u32 choice}): a human
// merchant with rank >= 4 at home casts its votes (1, 2 or 3 by rank). In the
// election, `choice` is the candidate 0..3: its votes [+0xC + k] and a share
// of the votes to give ([+0x15] · the merchant's supporters / [+0x16]) to
// [+0x10 + k]. In the diet, 0 yes and 1 no ([+0x12 + k] and the supporters to
// [+0x10 + k]), more abstains ([+0x14], [+0x15] up to 0xFA).
void fn_53aa10(SaveGame& save, const uint8_t* operation);

// In events_mayor.cpp (the local session's), used here as it is: 0x529700,
// the 4 candidates for the next election (town and merchant, or 0xFF).
namespace events_mayor_detail {
void fn_529700(const SaveGame& save, uint8_t town[4], uint8_t merchant[4]);
}  // namespace events_mayor_detail

}  // namespace p3::data::tick
