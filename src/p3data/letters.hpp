#pragma once

// The text of a letter, as the game's letter window composes it (0x47F3B0;
// docs/formats/mail.md, §4): a header with the town and the date, then a body
// by type, from the game's texts (GameTexts::raw_at, by their address in the
// English exe) and the letter's fields.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

// The letter window's list (0x47C520, which first removes the player's old
// letters, 0x4D6780): the local player's letters of one class (its tabs 1..3:
// official writings, personal letters, trading information; letter_class),
// in the order of the chain, newest first.
std::vector<uint16_t> letter_rows(const SaveGame& save, uint8_t tab);

// A row of the list as 0x47D570 draws it, raw (Windows-1252): the town (the
// table 0x6DDA00 by the letter's [+0x5], an index taken as a number: the bug
// of compose_letter), the date «DD.MM.YYYY» and the title (0x6A52D0 by type;
// for 0x3C..0x40 the first string of the letter's buffer).
struct LetterRow {
	std::string town;
	std::string date;
	std::string title;
	bool unread = false;  // The month's bit 0x80.
};
LetterRow letter_row(const SaveGame& save, const GameTexts& texts, uint16_t entry);

// The window's tab names (0x6B2DD8: 0 «Public news», 1..3 the classes),
// raw. The Spanish exe repeats the first class's name for tab 0.
const std::string& letter_tab_name(const GameTexts& texts, uint8_t tab);

// The new letter's line (0x4D7B10: «Official writing: %s» with the title,
// by its class 1..3), raw; empty for the other classes. The game shows it
// only when the local player's [+0x26] has the class's bit 8, 0x10 or 0x20
// (letter_notice_wanted).
std::string letter_notice(const SaveGame& save, const GameTexts& texts, uint16_t entry);
bool letter_notice_wanted(const SaveGame& save, uint16_t entry);

// The chronicle, as 0x47DD40 composes it for its text window, raw: a title
// with today's date («\c\f2_chronicle of 1. Apr 1362»), then the news of the
// list −2 (the letters sent to kLetterToChronicle, newest first), a header
// with each new date and a line per news by its type (21 types have one; the
// others make an empty line), each followed by the game's ornament
// («\c\t16063_», a picture). The Spanish exe's code differs in two cases (the
// siege's 0x38 passes the days first; the expedition's 0x56 takes the
// capitalized articles).
std::string compose_chronicle(const SaveGame& save, const GameTexts& texts);

// What composing the chronicle writes into the save: 0x4F6420 sets the bit
// 0x80 of [+0x1C] of each woman it names (and clears it for a man). The viewer
// calls it when it shows the chronicle, as the game.
void chronicle_viewed(SaveGame& save, const GameTexts& texts);

// The game's ticker line (0x5483D0 on the real-time object 0x6DF2F0, which
// 0x548CA0 calls for the local player): the text of `kind` (0..0x2A) with its
// argument `e` (a ship, a town or a number, by kind), raw; an empty text
// when the game shows an empty line (a ship past the ships...), nullopt when
// it shows nothing (12 kinds have no line in this build; 0x2A, «The
// Hanseatic League is blockading %s», only while the game isn't running: a
// game bug, shown with fix_game_bugs()).
std::optional<std::string> compose_ticker(const SaveGame& save, const GameTexts& texts, uint8_t kind, uint16_t e);
// The same for a line of the queue (take_ticker), also the game's own lines.
std::optional<std::string> compose_ticker(const SaveGame& save, const GameTexts& texts, const SaveGame::TickerLine& line);

// A letter's text as the game's text box reads it (0x462540): paragraphs
// (at each '\n'; a 0 byte ends the text) with the alignment set by the codes
// \l (left), \r (right) and \c (centre), which stays for the next ones, in
// UTF-8 without the other codes: \fN_ (a font: kept in `font`), \d (the next
// letter as a drop cap: the letter stays), \tN_ and \hN_ (pictures: out, their
// number in `picture`), \C,
// \B and \L (the coin's, a barrel's and a load's signs: the bytes below), and
// '_' (where a word may be split with a hyphen: kLetterSoftHyphen). The
// carriage returns of the mission letters are dropped.
constexpr char kLetterCoin = '\x05';
constexpr char kLetterBarrel = '\x06';
constexpr char kLetterLoad = '\x07';
constexpr char kLetterSoftHyphen = '\x1F';
struct LetterParagraph {
	enum Align : uint8_t { Left = 1, Right = 2, Center = 3 };
	Align align = Left;
	uint8_t font = 0;
	int picture = -1;  // The last picture's number (the chronicle's ornament: 16063).
	std::string text;
};
std::vector<LetterParagraph> letter_paragraphs(const std::string& raw);

}  // namespace p3::data
