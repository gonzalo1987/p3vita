#pragma once

// The text of a letter, as the game's letter window composes it (0x47F3B0;
// docs/formats/mail.md, §4): a header with the town and the date, then a body
// by type, from the game's texts (GameTexts::raw_at, by their address in the
// English exe) and the letter's fields.

#include <cstdint>
#include <optional>
#include <string>

#include "p3data/game_texts.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// The raw text of mailbox entry `entry` (Windows-1252 with the game's control
// codes, 0 bytes inside included, as the window gets it) for the local player
// (save.player); nullopt for the types without a letter text (the composer's
// default case: news for the chronicle, the mission interpreter's records...)
// and for a type not rewritten yet (letter_type_rewritten).
std::optional<std::string> compose_letter(const SaveGame& save, const GameTexts& texts, uint16_t entry);

// What the game's composer writes into the save when it shows a letter: a
// loan's date (0x12, 0x26) past its month's end moved to the next month's 1st
// (0x47FDB3); the women's list's bit of a woman player's first name (0x21,
// 0x22: 0x4F6420). The viewer calls it when it shows a letter, as the game.
void letter_viewed(SaveGame& save, uint16_t entry);

// Whether compose_letter has the type's case (rewritten and checked so far).
bool letter_type_rewritten(uint8_t type);

// Whether the type has a letter text at all (0x47F3B0's table 0x48664C).
bool letter_has_text(uint8_t type);

}  // namespace p3::data
