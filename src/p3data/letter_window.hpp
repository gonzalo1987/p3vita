#pragma once

#include <optional>
#include <string>
#include <vector>

#include "p3data/game_texts.hpp"
#include "p3data/player_actions.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// The buttons of a letter in the game's letter window, for the viewer: the composer (0x47F3B0) gives some letters
// buttons as it writes their text (in the struct it hands the text window: a count and the labels), and pressing one
// calls 0x4D6C50, which queues an operation by the letter's type (0x4D6C90). See docs/formats/mail.md §4.

// The labels of the buttons the letter `entry` has now, raw (the game's Windows-1252 texts; for the missions'
// letters, the strings of the letter's buffer), in order; empty for none:
// - 0x1E «Memo»: «Take back» (0x6B503C); 0x1F «Offer»: «Accept offer», «Reject offer» (0x6B5028, 0x6B502C);
// - 0x21 «Suitor»: «Accept offer» (0x6B52A0) while the tick is before the day [+0xE] of the letter's year;
// - 0x22 «Proposal»: «Accept offer» while the tick is before the letter's day + [+0xF] days (the game counts the
//   letter's day a day late: with fix_game_bugs(), from 0, so an accepted proposal loses its button at once);
// - 0x3D..0x3F (the missions'; 0x3C and 0x40 have no text): the record's [+0xB] labels, the strings after the
//   title and the text in the buffer, while the tick is before the record's [+0x4];
// - 0x64 «Prince»: «Accept» (0x6A9A04) unless its [+0xF] is 0xFE, while the tick's day isn't past the letter's day
//   + 13;
// - 0x79 (a citizen can't pay back a loan): «impound», «release» (0x6AEBFC, 0x6AEC00) while its event [+0xA] is the
//   loan still to decide (an event 0x01 without [+0x12], the debtor a citizen, the creditor a merchant).
// Without texts, the labels are empty strings (the count still right).
std::vector<std::string> letter_buttons(const SaveGame& save, const GameTexts* texts, uint16_t entry);

// The operation the button `answer` (0, 1...) of the letter `entry` queues (0x4D6C90); nullopt for none (a letter
// without buttons; 0x1E's answers but the first). Two roll the game's rand(), as the game does when the button is
// pressed: 0x72 (0x21: three rand() and a town) and 0x60 (0x22: four rand()).
// - 0x1E, 0x1F: 0x75 {u32 [+0x8], u32 u16 [+0xC], u32 u16 [+0xE], u32 the answer (0x1E: −1)};
// - 0x21: 0x72 {u16 the sum [+0xC], u16 the entry, 3 × u16 rand(), u8 rand() % towns, u8 the player, u8 [+0x5],
//   u8 [+0x8], u8 [+0xA], u8 whether ((year · 365 + [+0xE]) << 8 | 0xFF) ≥ tick};
// - 0x22: 0x60 {u32 tick + ((rand() & 7) + 14) << 8, u16 (rand() & 0x1FFF) + 0x13F6, u8 rand(), u8 [+0xE], u8 [+0xC],
//   u8 [+0xD], u8 [+0xB] % towns, u8 [+0xA], u8 rand() & 3, u8 the player};
// - 0x3C..0x3F: 0x74 {u32 u16 the record's [+0x8], u32 its u8 [+0xA], u32 the answer, u32 the entry};
// - 0x64: 0x8C {u32 [+0x5], u32 the player, u32 [+0xF], u32 the entry};
// - 0x79: 0x77 {u32 u16 [+0xA], u32 the entry, u32 the answer}.
std::optional<OperationBytes> letter_answer(SaveGame& save, uint16_t entry, uint32_t answer);

// What showing the letter `entry` queues by itself (the composer, 0x485D7F): a letter 0x79 whose event [+0xA] is a
// queue record but not the loan to decide queues 0x77 {0xFFFF, the entry, 1} (the letter loses its event); nullopt
// for nothing.
std::optional<OperationBytes> letter_shown_operation(const SaveGame& save, uint16_t entry);

}  // namespace p3::data
