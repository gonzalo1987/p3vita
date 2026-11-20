#pragma once

// The mailbox of letters and notices (the object 0x6DD730): sending a letter
// (0x4D6530) and what it calls. See docs/formats/mail.md.

#include <cstdint>
#include <optional>
#include <vector>

#include "p3data/game_memory.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// The mailbox's globals ([+0x4], [+0x6] and [+0x8] of 0x6DD730).
constexpr uint32_t kMailFree = 0x6DD734;  // u16 head of the free list
constexpr uint32_t kMailCount = 0x6DD736;  // u16 number of entries
constexpr uint32_t kMailChronicle = 0x6DD738;  // u16 head of the chronicle (the letters to −2)

// Merchant codes of 0x4D6530 besides a merchant's index.
constexpr int32_t kLetterToEach = -1;  // every merchant (only the human ones keep it)
constexpr int32_t kLetterToChronicle = -2;  // the chronicle, the news for everybody
constexpr int32_t kLetterToTown = -3;  // the owner of each office of the town in the letter's [+0x5]

// Whether a letter type carries a heap buffer ([+0x8] pointer, [+0xC] size)
// or a 16-byte heap record ([+0x8]) with a buffer ([+0xC], record [+0x0]
// bytes) (0x51A890, 0x51AB20, 0x51AAB0).
bool letter_has_buffer(uint8_t type);
bool letter_has_record(uint8_t type);

// A letter as its sender builds it on the stack: the 16 bytes (as the
// constructor 0x51A850 leaves them: day 0, month 0x80, year 0xFFFF, type and
// [+0x5] 0xFF, [+0x6] 0xFFFF, the payload 0) and what hangs from it. The game
// keeps pointers in [+0x8] (and [+0xC] for a record); p3vita keeps 0 there
// and the buffer and record apart, as SaveGame::mail_buffers and
// SaveGame::mail_records. For a type with a buffer the sender sets [+0xC] to
// the buffer's size.
struct Letter {
	GameMemory<0x10> entry;
	std::vector<uint8_t> buffer;
	std::optional<GameMemory<0x10>> record;

	Letter();
	explicit Letter(uint8_t type);
	uint8_t type() const { return entry.u8(4); }
};

// 0x4D6530 (on the mailbox): sends `letter` to `merchant` (an index or one of
// the codes above). It gets today's date and unread mark; the human
// merchants whose settings let it through, and the chronicle, keep a copy at
// the head of their chain.
void send_letter(SaveGame& save, int32_t merchant, const Letter& letter);

// 0x4D7AC0 (on the mailbox): whether the chain from `index` (a merchant's
// [+0xA]) has a letter 0x5F with u16 [+0xA] equal to `rank`.
bool has_rank_letter(const SaveGame& save, uint32_t index, uint32_t rank);

// 0x4D69D0 (on the mailbox; operation 0x6E, which the letter window queues
// when the player picks a letter): clears the «unread» bit 0x80 of entry
// `entry`'s month if it is in the chain of `merchant` (a merchant's index;
// nothing for a negative one, an entry past the mailbox or one not in it).
void mark_letter_read(SaveGame& save, int32_t merchant, uint32_t entry);

// 0x4D6A50 (on the mailbox; operation 0x6F from the letter window, event
// 0x13, operation 0x75): takes entry `entry` out of the chain of `merchant`
// (a negative one: the chronicle), puts it at the head of the free list and
// releases it (0x51AAB0). Nothing if it isn't there.
void delete_letter(SaveGame& save, int32_t merchant, uint32_t entry);

// 0x4D7030 (on the mailbox; the mission interpreter's 0xF0): the treasure spot `spot` (< 100: the table 0x671FD0) found
// by `merchant` (below the count): in his chain (from his [+0xA], at most the mailbox's count of steps) a letter 0x45
// for that spot ([+0xC], [+0xE]) goes to the free list with its type 0xFF, a letter 0x46 for it gets [+0xB] |= 0xFE.
void treasure_found(SaveGame& save, uint32_t merchant, uint32_t spot);
// 0x4D7340 (on the mailbox; the operation 0x54): in the chain of the merchant `merchant` (below the count; at most
// the mailbox's count of steps) the first letter 0x44 with [+0x5] `town` and u16 [+0x8] `event` goes to the free list,
// released (0x51AAB0). Its u16 [+0xC], or 0xFFFF when there is none. (Only dead code makes letters 0x44: 0x4D7140.)
uint16_t remove_letter_44(SaveGame& save, uint8_t merchant, uint8_t town, uint16_t event);
// 0x4D74C0 (on the mailbox; the operation 0x55, a treasure map's piece paid): the merchant `merchant` (below the
// count) pays `amount` ([+0x0], whatever comes next); if the entry `entry` is in his chain (at most the mailbox's count
// of steps) as a letter 0x45 or 0x46, it becomes a 0x46 with `bits` in its u16 [+0xA], today's date and the «unread»
// bit, and goes to the head of his chain.
void pay_map_letter(SaveGame& save, uint32_t merchant, uint32_t entry, uint32_t amount, uint16_t bits);
// 0x4D7870 (on the mailbox; the operation 0x76): from the link `head` (a merchant's [+0xA]) the first letter 0x71 whose
// record's u16 [+0x8] is `event` and u8 [+0xA] `key` goes to the free list, released (0x51AAB0). Whether one went.
// The game follows the chain without a limit.
bool remove_letter_71(SaveGame& save, uint16_t& head, uint32_t event, uint8_t key);
// 0x4D7900 (on the mailbox; the tavern's side room, the operation 0x52, the mission interpreter's 0x4A): from the entry
// `start` (below the mailbox's count) along its chain, the first tavern's mission letter 0x71 for the town `town` (its
// [+0x5]) whose record's [+0x4] is after now; 0xFFFF for none. The game follows the chain without a limit.
uint16_t find_tavern_letter(const SaveGame& save, uint16_t start, uint16_t town);
// The treasure spot `spot`'s place (the game's 100 spots, u32 x and y: 0x671FD0, the same at 0x672340); false past
// them.
bool treasure_spot(uint32_t spot, uint16_t& x, uint16_t& y);

// 0x4D6780 (on the mailbox): removes a merchant's letters kept longer than
// their class's setting allows (town_tick/events_towns.cpp, fn_4d6780). The
// letter window calls it for the local player each time it fills its list
// (0x47C520), the game's save for every merchant.
void expire_letters(SaveGame& save, int32_t merchant);

// 0x4D6AF0 (on the mailbox; operation 0x70, 0x536AFB, which the letters' settings window 0x408F50 queues
// on closing): the 8 `bytes` to the merchant's [+0x38..+0x3F] (the days each class of letters is kept,
// the indices of 0x6C0220, then the window's «event sound» and zeros) and `notices` to his [+0x26] (the
// sound and the line of each class), for a merchant under the count.
void set_letter_settings(SaveGame& save, uint32_t merchant, const uint8_t* bytes, uint8_t notices);

// 0x4D7960 (on the mailbox; operation 0x71 with its [+0x8] set, after the settings' 0x70): repairs the
// chains. Each merchant's chain and the chronicle's are walked marking their letters; one reached a second
// time cuts the link to it (0xFFFF); the free list ends (its link set to the count) at a letter already
// marked. The free list's head over the count first becomes the count. The operation 0x71 then removes
// the merchant's old letters (expire_letters); the letter window queues it with 0 once a tick while it is
// open (0x47CD83).
void repair_letter_chains(SaveGame& save);

// 0x548CA0 (on the real-time object 0x6DF2F0): a ticker line for `merchant`,
// shown only to the local player (save.player): its kind and argument go to
// SaveGame::player_ticker, unless the kind has no line (0x5483D0 shows nothing
// for 12 kinds); for the 5 kinds of a fixed text the argument is kept as 0
// (the oracle's region TK the same). The rest of 0x548CA0 is the
// interface's (the view moved to
// the ship or the town: its arguments a, b, c, not kept).
void send_ticker(SaveGame& save, uint32_t merchant, uint8_t kind, uint16_t e);

// A line the game writes to its interface itself (0x42B4C0, not through
// 0x548CA0): the text at `english` (GameTexts::raw_at) with the town `town`,
// for the local player.
void send_ticker_text(SaveGame& save, uint32_t merchant, uint32_t english, uint16_t town);

// A ticker line the game sends only while its interface exists (the window
// object *0x6CC7E8: the promotion's): as send_ticker, marked so that the
// comparisons with the oracle, which has no interface, leave it out.
void send_ticker_interface(SaveGame& save, uint32_t merchant, uint8_t kind, uint16_t e);

// The ticker lines sent to the local player, oldest first (compose_ticker,
// letters.hpp, makes their text). Emptied.
std::vector<SaveGame::TickerLine> take_ticker(SaveGame& save);

// The event videos' windows (SaveGame::event_windows) and the message windows (SaveGame::player_messages) the world
// opened for the local player, oldest first. Emptied. (Without a viewer to take them, each keeps its last 32.)
void open_event_window(SaveGame& save, uint32_t code, uint16_t a, uint16_t b, const std::string& first,
	const std::string& second);
std::vector<SaveGame::EventWindow> take_event_windows(SaveGame& save);
void open_player_message(SaveGame& save, const std::string& text);
std::vector<std::string> take_player_messages(SaveGame& save);

// The local player's (save.player) new letters, as 0x4D6680 stores them:
// their entries, oldest first, for the viewer's notice (the game's 0x4D7B10,
// «Official writing: %s»...: see letters.hpp, letter_notice). Emptied.
std::vector<uint16_t> take_new_letters(SaveGame& save);

// 0x51AAB0 on entry `index`: frees its buffer and record and empties it (day
// and month 0, year 0xFFFF, type 0xFF, [+0x5] 0, payload 0; the link stays).
void release_letter(SaveGame& save, size_t index);

// The class of a letter type (0x6C0198): 1 official writing, 2 personal
// letter, 3 trading information, 4 notice, 0 never kept. Types from 0x88 on
// read past the table (0 here).
uint8_t letter_class(uint8_t type);

// How long a merchant's setting keeps a letter of its class (0x6C0220: never
// removed, 40, 20, 10, 5 days, not received), in ticks.
uint32_t letter_keep(uint8_t setting);

}  // namespace p3::data
