# The mailbox: letters and notices (0x6DD730)

The game keeps every letter and notice it sends to the merchants in one
object, the mailbox at 0x6DD730. Each human merchant has a chain of letters
in it, and there is a second list, the **chronicle**, with the news sent to
everybody (merchant −2). The interface lists a merchant's letters, composes
the text of each one from its type and its 16 bytes, and shows the chronicle.
The simulation also reads the mailbox: event 0x16 doesn't repeat a notice
that is still there, the mission interpreter looks for its letters, and
several operations (from the interface) answer, mark or delete letters.

This document is the inventory of that subsystem (2026-11-03), taken from
the executable: the object and its methods, the 154 calls that send a
letter, the interface's texts, and the other channel of messages to the
player (0x548CA0). The tables at the end were generated with the scripts in
`re/emu/sesion5/mail/` (outside the repository; Windows Python with
capstone over `Patrician3.exe`, English GOG 1.1): `mail_calls.py 4d6530`
(each call's function, arguments and stores to the letter, with the stack
depth from a walk of the function's flow; `STARTS=41ade0,41ae60,5fe7e0`),
`gen_table.py`, `cases.py` (a switch's cases and the texts they use),
`gen_types.py`, `ticker_calls.py`, `who.py`, `cdis.py` and `e8scan.py`.

## 1. The object

| Address | Field | What |
|---|---|---|
| 0x6DD730 | [+0x0] u32 | the array of 16-byte entries (heap, with the MSVC count in front) |
| 0x6DD734 | [+0x4] u16 | head of the free list |
| 0x6DD736 | [+0x6] u16 | number of entries (the array's size) |
| 0x6DD738 | [+0x8] u16 | head of the chronicle (the letters sent to −2) |
| 0x6DD73A | [+0xA] u16 | read from the file from version 2 on, set to 0 by the reset; no other use found |

Each merchant's chain starts at the merchant's u16 [+0xA] and follows the
entries' [+0x6]; a link not below [+0x6] of the object ends it. New letters
go at the head (newest first). The free list follows [+0x6] too. When it is
empty, 0x4D6B40 grows the array by 200 entries: a new array of count + 200,
each old entry deep-copied (0x51AC00), the old array freed, and the 200 new
entries given type 0xFF and linked in order as the free list.

An entry:

| Offset | Size | What |
|---|---|---|
| +0x0 | u8 | day (from [0x6DE4A0], the date) |
| +0x1 | u8 | month ([0x6DE4A1]) \| 0x80 «unread» \| 0x40 (set by 0x4D7B10 only in network games) |
| +0x2 | u16 | year ([0x6DE4A2]) |
| +0x4 | u8 | type (0x00..0x85; 0xFF free; 0x86 and above are never stored) |
| +0x5 | u8 | by type: usually a town or a merchant |
| +0x6 | u16 | next entry |
| +0x8 | 8 bytes | the payload, by type |

The payload (0x51A890 size, 0x51AB20 copy, 0x51AAB0 release, 0x51A8D0 write,
0x51A9B0 read; the constructor 0x51A850 leaves day 0, month 0x80, year
0xFFFF, type and [+0x5] 0xFF and the payload 0):

- **Types 0x29, 0x47, 0x63 and 0x83**: [+0x8] a heap buffer, [+0xC] its
  size. The battle report (0x29), the election (0x47), the expedition (0x63)
  and the impounding (0x83) carry their text or table there.
- **Types 0x3C..0x40 and 0x71**: [+0x8] a 16-byte heap record whose u32
  [+0x0] is the size of the heap buffer at the letter's [+0xC]. For 0x71 the
  record's u32 [+0x4] is the tick until which the letter is kept, its u16
  [+0x8] and u8 [+0xA] identify it (0x4D7870), and the letter's [+0x5] is a
  town (0x4D7900). The mission interpreter writes these.
- **The others**: 8 bytes in the entry.

The copy (0x51AB20) copies the date, type and [+0x5] (not the link) and
allocates new buffers, so the sender keeps its own and frees them after
0x4D6530 (the battle report's six buffers, for example).

**In the savegame** (0x4D62B0 writes, 0x4D6350 reads; p3vita: `load_mail`
in `savegame.cpp`, `write_mail` in `savegame_write.cpp`): u32 0xAFFE0005
(the version in the low half), u16 count, u16 free head, u16 chronicle head,
u16 [+0xA] (version ≥ 2), then each entry: its first 8 bytes and the payload
(8 bytes and [+0xC] bytes of buffer; the 16-byte record and record[+0x0]
bytes of buffer; or 8 bytes). The reader allocates exactly `count` entries.

## 2. Sending a letter: 0x4D6530

`0x4D6530(merchant, letter)` (thiscall on 0x6DD730, ret 8):

1. A type of 0x86 or more: nothing. A merchant not below the number of
   merchants ([0x6DE4AA], signed compare, so the negative codes pass):
   nothing.
2. The letter gets today's date: day, month | 0x80, year.
3. By `merchant`:
   - **−2**: the chronicle. Takes an entry from the free list (growing it if
     needed), copies the letter (0x51AB20) and puts it at the head of
     [+0x8]. No filter: it is always stored.
   - **−1**: 0x4D6680 for each merchant.
   - **−3**: 0x4D6680 for the owner ([+0x2C4]) of each office of the town in
     the letter's [+0x5] (the town's [+0x784] and the offices' [+0x2CA]).
     None of the 154 calls uses it.
   - **a merchant**: 0x4D6680.

`0x4D6680(merchant, letter)` stores it only if the merchant is **human**
([+0x8] == 0: in single player, only the player) and the player's settings
let it through. Each type has a class in the byte table 0x6C0198:

| Class | Window title (0x6A4570..0x6A4578) | Stored if | Kept (0x4D6780) |
|---|---|---|---|
| 0 | — | never | — |
| 1 | «Official writing: %s» | days[[+0x38]] > 0 | days[[+0x38]] |
| 2 | «Personal letter: %s» | type 0x46, or days[[+0x39]] > 0 | 0x46 forever; days[[+0x39]] |
| 3 | «Trading information: %s» | type 0x11, or days[[+0x3A]] > 0 | 0x11 seven days; days[[+0x3A]] |
| 4 | — | always | 0x0F and 0x10 seven days, 0x72 28 days, 0x71 until its record's tick, the others forever |

`days` is the table 0x6C0220: forever (0xFFFFFFFF), 40, 20, 10 and 5 days
(0x2800, 0x1400, 0xA00, 0x500 ticks), and 0 («don't receive»). The merchant's
bytes [+0x38], [+0x39] and [+0x3A] are the indices, set with operation 0x70.
Then 0x4D7B10 tells the interface (below), and the letter is copied into an
entry from the free list at the head of the merchant's chain.

0x4D7B10 does nothing without the town view (*0x6E51AC, so nothing in the
oracle). In a network game, for a human merchant who isn't the local
player, it only sets the bit 0x40 of the letter's month (the copy keeps it).
For the local player, when no battle window is open: the sound 0x1770
(0x443150 on 0x6CBDB8) if the merchant's [+0x26] has the bit of the class
(1, 2, 4 for the classes 1, 2, 3), and a line «Official writing: Naval
battle» (class format and title, the title of 0x3C..0x40 being the letter's
own buffer) to the interface (*0x6CBB40) if it has 8, 0x10 or 0x20.

**Only the human merchants' chains and the chronicle change.** A letter to a
merchant of the computer is dropped in 0x4D6680, so in the saves (one human
player) the simulation's letters only reach the player and the chronicle.
None of this changes the simulation by itself, apart from the mailbox and
the RNG of the senders (rand() calls that build the letters).

## 3. The other methods

| Address | What | Callers | p3vita |
|---|---|---|---|
| 0x4D61D0, 0x4D61F0 | static initializer and destructor of 0x6DD730 | CRT | — |
| 0x4D6230, 0x4D6240 | constructor, destructor | none | — |
| 0x4D6270 | size for the savegame | 0x549C81 | — |
| 0x4D62B0 | write | 0x549E9E | `write_mail` |
| 0x4D6350 | read | 0x54A135 | `load_mail` |
| 0x4D64B0 | reset: frees everything, no entries, chronicle 0xFFFF, every merchant's [+0xA] = 0xFFFF | 0x430981 (UI), 0x542494, 0x5434E8, 0x54A39C (new game, load) | — |
| 0x4D6530 | send | 154 calls (§5) | skipped |
| 0x4D6680 | filter and store for one merchant | 0x4D6530 | — |
| 0x4D6780 | remove a merchant's old letters (kept time by class, above) | 0x47C54D (UI), 0x4DDA6E (event 0x03), 0x4F5EAE (operation 0x65), 0x536B36 (operation 0x71), 0x54737B (real-time loop) | `events_towns.cpp` (with a game bug fixed behind `fix_game_bugs`) |
| 0x4D6930 | copy an entry into a 0x14-byte view | none | — |
| 0x4D69D0 | mark a letter read (clear 0x80 of [+0x1]) if it is in the merchant's chain | operation 0x6E (0x536AD4) | — |
| 0x4D6A50 | delete a letter (merchant ≥ 0: from its chain; negative: from the chronicle) | event 0x13 (0x4E7A14: 0x4E7B6C, 0x4E7B80), operation 0x6F (0x536AEE), operation 0x75 (0x53ECB0: 0x53ED5E, 0x53EDDA) | — |
| 0x4D6AF0 | a merchant's letter settings: 8 bytes to [+0x38..+0x3F] and [+0x26] | operation 0x70 (0x536B0F) | — |
| 0x4D6B40 | grow by 200 | 0x4D6530, 0x4D6680, 0x4D7140, 0x4D73F0 | — |
| 0x4D6C50 → 0x4D6C90 | the player answers a letter (a button of the letter window, pushed at 0x486550): by type, queues operation 0x75, 0x72, 0x60, 0x74, 0x8C or 0x77 | interface | — |
| 0x4D6FD0 | clear the marks of a merchant's letters 0x21 ([+0xE]) and 0x22 ([+0xF]) | operation 0x60 (0x53DA90) | `fn_4d6fd0` |
| 0x4D7030 | remove the 0x45 letters and mark the 0x46 ones that match two u16 | mission interpreter (0x4EE252) | — |
| 0x4D7140, 0x4D7200, 0x4D73F0 | add a 0x44 letter, find one, add a 0x45/0x46 letter | none (dead code) | — |
| 0x4D7340 | remove the 0x44 letter matching [+0x5] and [+0x8] and return its [+0xC] | operation 0x54 (0x53CA30) | — |
| 0x4D74C0 | pay for a 0x45/0x46 letter: the merchant's money [+0x0] − amount, the letter becomes 0x46, gets the bits in [+0xA], today's date and unread, and moves to the head | operation 0x55 (0x536141) | — |
| 0x4D75D0 | pick a merchant's 0x45/0x46 letter (with rand) | 0x526B40, from the interface (0x5CC942, 0x5CF2EF, 0x5D5C3B) | — |
| 0x4D7720 | whether a chain has a 0x45/0x46 letter | mission interpreter (0x4EF769) | — |
| 0x4D7770 | whether a convoy is still the object of a 0x54 letter | mission interpreter (0x4F06AC) | — |
| 0x4D7870 | remove the 0x71 letter matching its record's [+0x8] and [+0xA] | operation 0x76 (0x53EDF0) | — |
| 0x4D7900 | the 0x71 letter for a town still valid (its record's [+0x4] after today) | mission interpreter (0x4F19E0), operation 0x52 (0x53C2C0), the interface (0x5A7xxx, 17 calls) | — |
| 0x4D7960 | repair the chains: a letter reached twice is cut off (0xFFFF), the chronicle and the free list too | operation 0x71 (0x536B28) | — |
| 0x4D7AC0 | whether a chain has a 0x5F letter with [+0xA] equal to a rank | 0x4F7559 (0x4F7510) | named in `merchants.cpp` |
| 0x4D7B10 | the new-letter notification (above) | 0x4D6680 | — (interface) |
| 0x4D7D50 | the player's unread letters with bit 0x40 (network) to the interface | 0x548C6E | — (interface) |

The simulation also reads the entries directly: event 0x16 (0x4E849D, the
0x21/0x22 notices of the player: `events_big.cpp`, without the mailbox it
fires early), event 0x13 (0x4E7A48, 0x4E7ABF), the mission interpreter
(0x4F19E5..0x4F20C1), operations 0x8C (0x5369FD), 0x52 (0x53C708), 0x72
(0x53EBD1..0x53EC7E), 0x75 and 0x76 (0x53ED0C..0x53EEF1), 0x8F (0x54017F)
and 0xB8 (0x543115: the free list's head goes into the network checksum,
operation 0xBC), and 0x526BB9. The interface: 0x4135C0,
0x47C57E..0x47FC24, 0x4869BB, 0x5A7110..0x5A7C99, 0x5CED8D, 0x5D8120.

## 4. The interface's texts

- **The list** (0x47D570): one row per letter, with the town name (table
  0x6DDA00) and the title (pointer table 0x6A52D0 by type; for 0x3C..0x40,
  the letter's own buffer [+0xC]); unread rows in another colour.
- **A letter** (0x47F3B0, called from 0x47CD46, 0x47DD29, 0x486AD0): a
  switch on the type at 0x47F5EA (table 0x48664C, types 0..0x85; 0x4865E5
  is the default, no text) composes the body from text pointers of the exe
  (templates with printf slots and the game's own codes: `\c` centre, `\l`
  rule, `\d` paragraph, `\f1_` font, `\C` the currency sign, `\B` a bale...)
  and the letter's fields. About 28 KB of code. Classes 0 and 3 start with
  another font byte. Its per-type case and longest text are in appendix B.
- **The chronicle** (0x47DD40, called from 0x424504): walks the −2 list from
  [+0x8], writes a header for each date (format 0x6C11D0 with the month
  names 0x6C0D04) and one line per letter: a switch at 0x47DFF1 (index
  0x47F320, table 0x47F2C8, types 6..0x7B; 0x47E4C0 default). The types
  that only go to −2 (class 4 news: promotions, guild entries, offices,
  moves, harvests, sieges, plague, fire, elections...) have no letter text
  and are only read here.
- **New letter**: 0x4D7B10, above.

For p3vita's viewer, these templates have to be read from the exe of the
installation (`GameTexts`), each with its slot in the English and Spanish
exes, as was done for the boarding messages (`docs/oracle.md`, «A battle of
the player's ship»).

## 5. The calls to 0x4D6530

154 calls in 84 functions (all of them `call` rel32 in `.text`; checked
against the raw bytes). 152 are reached from the simulation (advance_time,
the operations' executor, the battles' methods); 2 are in an interface
window (0x419EB0, from 0x410A02: letters 0x45 and 0x21 to the local player,
built with rand()).

- **97** are in functions p3vita has rewritten (`fn_XXXXXX`), which skip the
  letter today (comments «A letter (0x4D6530): not rewritten» or «no se
  traduce»).
- **10** are in functions p3vita names but skips whole: the mission
  interpreter 0x4ECF64 (5, three with a letter built by the script), the
  alderman's notification 0x5299F0 (2), the convoy's automatic route
  0x503230, the sinking message 0x519D90 and 0x4F3AF0.
- **47** are in events and operations p3vita doesn't have yet. Events 0x09,
  0x0C, 0x0F, 0x10, 0x11, 0x17, 0x18 and 0x28 are created only by player
  actions (operations from the interface) or by the mission interpreter;
  operations 0x03, 0x06, 0x2F, 0x3D, 0x3E, 0x4A, 0x53, 0x5F, 0x72, 0x77,
  0x8B, 0x8C, 0x8F and 0x90 are player actions; plus the town battle
  (0x5FE7E0, 3), a new game (0x530470) and a trade route that ends
  (0x4D5200).

Appendix A lists them. «To» is the first argument: one merchant (a register), −1
(each merchant: in practice the human ones) or −2 (the chronicle). The type
was taken from the store to the letter's [+0x4] on the stack; the ones the
script could not resolve were read by hand (Note).

## 6. Messages to the player: 0x548CA0

The other channel. `0x548CA0(merchant, a, b, c, kind, e)` (thiscall on the
real-time object 0x6DF2F0, ret 0x18) only acts for the local player
([+0x924]): while a window is open it queues up to 8 messages at [+0x948];
otherwise it composes a line of the **ticker** in 0x5483D0 (a switch on the
kind, 0..0x2A, index 0x548834, table 0x5487E8: «%s interrupts trade
journey.», «Convoy %s reaches its destination.», «Your ship %s has sunk.»,
«Your land transport from %s has reached %s.», «The Hanseatic League is
blockading %s»...) and can move the town view to the ship or the town
(0x5308A0 with 0 only looks the office up). It changes nothing of the
simulation: only the real-time object's fields and the interface. The
oracle silences it (`QUIET`) and p3vita skips it.

60 calls (the kind is the fifth argument): 47 in functions p3vita has
rewritten (the end of the game 0x4E4A44 alone has 9, the convoys 0x502110
and 0x506B6A, docking 0x519C90, arriving 0x516CC0, the battles' setup
0x605420 and 0x5FBFE0, the diet 0x4E09D4...), the rest in the mission
interpreter, the automatic routes (0x4D5200, 0x503230) and events p3vita
doesn't have.

## 7. In p3vita

- `src/p3data/mail.hpp`, `mail.cpp`: `send_letter` (0x4D6530 with 0x4D6680),
  the growth (0x4D6B40), the copy (0x51AB20) and `release_letter` (0x51AAB0),
  `letter_class` (0x6C0198) and `letter_keep` (0x6C0220). A sender builds a
  `Letter` (the 16 bytes as 0x51A850 leaves them, plus its buffer or
  record) and calls `send_letter`. p3vita keeps 0 where the game keeps
  pointers ([+0x8]; [+0xC] of a record) and the buffers and records apart:
  `SaveGame::mail_buffers` and `SaveGame::mail_records`, as long as
  `SaveGame::mail`. 0x4D6780 (`events_towns.cpp`) releases with
  `release_letter`. 0x4D7B10 (the interface's notice) is not reproduced.
- The oracle runs 0x4D6530 only from the connected calls (`MAIL_CALLS` in
  `tools/oracle/town_oracle.py`) and records the buffers as region `LB`
  ([oracle.md](../oracle.md), «The mailbox: sending letters»); scenarios in
  `tools/oracle/retouch_mail.py`; `tests/mail_test.cpp`.
- Connected so far: the merchant's tick 0x4F7BB0 (`merchants.cpp`): 0x0F,
  0x10, 0x11 (the week's accounts, to a human merchant: the home town in
  [+0x5], two u32 amounts in [+0x8] and [+0xC]) and 0x06 (a promotion in the
  home town, to the chronicle: [+0x5] the town, u16 [+0x8] the old rank,
  [+0xA] the new one, [+0xC] the merchant, [+0xE] his title, 0x4F9430).
- The merchant's rank (`merchants.cpp`): 0x5E and 0x5F (0x4F7510; not if
  his chain has a 0x5F of that rank, `has_rank_letter`), 0x6D (the
  candidacy) and 0x82 (rights in another town) of 0x4F75F0; a merchant's
  task, event 0x05 (`events_big.cpp`, 0x4E5B84: 0x19 or 0x0A at the start
  by the kind, 0x4A failed, 0x4B the result: the pay / 500, the ware and
  loads, or the punishment's town and days), and 0x21 of event 0x16.
- The towns and convoys: 0x00 (a ship launched: [+0x5] the town, u16
  [+0x8] the ship, [+0xA] its type), 0x03 (works done: the town's [+0x2C0],
  u16 the record's type and the record, to its owner; nobody when the
  record was freed or given to the town), 0x60 (an office ready), 0x35 (a
  famine, to the chronicle), 0x81 (the hulk, to each merchant), 0x0C (the
  guild, to the chronicle: the merchant, his title and rank), 0x2F (a
  town wall's bill: the payer's and the mayor's names, the sum), 0x32 and
  0x31 (a convoy's fees at its destination: the fee or the total, the
  guide's [+0x15E], the leader's rank and names).
- The sea battles (`battle/`):
  - 0x60D299 (`battle_end.cpp`): the report, a letter 0x29 to each side of
    the computer: [+0x5] the merchant's home town, the buffer the text of
    its team (the result, «Own ships:» and the team's lines, «Captured
    ships:» and the other team's ships it took, «Enemy ships:» and the
    other team's lines; each line «type name: hull%» with «, captured»,
    «, fled», «, plundered» or «sunk»), without the final 0. A pirate's win
    (its guide with a captain) makes the news 0x2A in the chronicle with the
    loser: [+0x5] home town, [+0x8] [+0x1A], [+0x9] title, [+0xA] rank at
    home, [+0xB] [+0x1C], [+0xC] [+0x1D], [+0xD] and [+0xE] the battle's
    [+0x1E90] and [+0x1E91], [+0xF] the ships lost (0xFF: none, but some
    boarded). Then 0x72 to each side's merchant (not a pirate) about the
    other side: u16 [+0x8] its merchant, [+0xA] sunk, [+0xC] taken, [+0xE]
    plundered.
  - 0x50BFD0 (`battle_end.cpp`, `Sailing::pirate_letters`), 0x60C1C9 and
    operation 0x91 (`capture.cpp`), 0x514D40 (`pirate_objects.cpp`) and
    0x61F96F (`ship_step.cpp`, `Sailing::sunk_pirate_news`): 0x54 to the
    merchant who sank or took a pirate's captain (u16 [+0x8] the captain's
    names, [+0xA] the captain, [+0xC] the ship), the news 0x2A of the same
    layout as above with the captain's names in [+0xD], [+0xE], and the
    share of a plunder 0x30 (the captain's names, the plundered ship's
    owner's [+0x1C], [+0x1D] or the two ships & 0x3F, u16 [+0xC] the share,
    [+0xE] the guide's [+0x15E] << 2 + the ship's type).

## Appendix A. The 154 calls

«p3vita file»: where p3vita has the function (`fn_XXXXXX`), or only names it
(«named»); «—» with what it is when p3vita doesn't have it. «Class» is the
type's class (§2).

| p3vita file | Function | Call | Type | Title (English exe) | Class | To | Note |
|---|---|---|---|---|---|---|---|
| p3data/battle/battle_end.cpp | 0x50BFD0 | 0x50C274 | 0x54 | Pirates | 4 | one merchant |  |
| p3data/battle/battle_end.cpp | 0x50BFD0 | 0x50C326 | 0x2A | Pirates | 2 | -2 (chronicle) |  |
| p3data/battle/battle_end.cpp | 0x50BFD0 | 0x50C853 | 0x54 | Pirates | 4 | one merchant |  |
| p3data/battle/battle_end.cpp | 0x60D299 | 0x60DD93 | 0x29 | Naval battle | 2 | one merchant |  |
| p3data/battle/battle_end.cpp | 0x60D299 | 0x60EB42 | 0x2A | Pirates | 2 | -2 (chronicle) |  |
| p3data/battle/battle_end.cpp | 0x60D299 | 0x60EC1B | 0x72 | — | 4 | side 0 ([+0x35A]) | after the report |
| p3data/battle/battle_end.cpp | 0x60D299 | 0x60EC83 | 0x72 | — | 4 | side 1 ([+0x35C]) | after the report |
| p3data/battle/capture.cpp | 0x540520 | 0x540A8C | 0x54 | Pirates | 4 | one merchant |  |
| p3data/battle/capture.cpp | 0x540520 | 0x540DDF | 0x30 | Captured | 2 | one merchant |  |
| p3data/battle/capture.cpp | 0x60C1C9 | 0x60C31B | 0x2A | Pirates | 2 | -2 (chronicle) |  |
| p3data/battle/capture.cpp | 0x60C1C9 | 0x60C383 | 0x54 | Pirates | 4 | one merchant |  |
| p3data/battle/capture.cpp | 0x60C1C9 | 0x60D189 | 0x30 | Captured | 2 | one merchant |  |
| p3data/battle/pirate_objects.cpp | 0x514D40 | 0x514F79 | 0x2A | Pirates | 2 | -2 (chronicle) |  |
| p3data/battle/ship_step.cpp | 0x61F96F | 0x61FC80 | 0x2A | Pirates | 2 | -2 (chronicle) | letter at [ebp-0x24] |
| p3data/convoys.cpp (named) | 0x503230 | 0x5035CD | 0x07 | Ship trade | 3 | one merchant |  |
| p3data/convoys_members.cpp | 0x50A580 | 0x50A966 | 0x32 | Convoy | 1 | one merchant |  |
| p3data/convoys_members.cpp | 0x50A580 | 0x50AA02 | 0x31 | Convoy | 1 | one merchant |  |
| p3data/sailing.cpp (named) | 0x519D90 | 0x519E87 | 0x1B | Sank | 2 | one merchant |  |
| p3data/town_tick/alderman.cpp | 0x4EB0EB | 0x4EB32B | 0x70 | Unsuccessful | 1 | one merchant |  |
| p3data/town_tick/alderman.cpp | 0x4EBF54 | 0x4EC18F | 0x80 | Overland trade | 1 | one merchant |  |
| p3data/town_tick/alderman.cpp | 0x4EBF54 | 0x4EC230 | 0x6E | Pirate hideaway | 1 | one merchant |  |
| p3data/town_tick/alderman.cpp | 0x4EBF54 | 0x4EC2B2 | 0x84 | Order | 1 | one merchant |  |
| p3data/town_tick/alderman.cpp | 0x4EC354 | 0x4EC4FC | 0x6F | Reputation | 1 | -2 (chronicle) |  |
| p3data/town_tick/alderman.cpp | 0x4EC354 | 0x4EC51C | 0x6F | Reputation | 1 | one merchant |  |
| p3data/town_tick/construction.cpp | 0x4F8560 | 0x4F8639 | 0x0C | Guild entry | 4 | -2 (chronicle) |  |
| p3data/town_tick/construction.cpp | 0x522C70 | 0x523702 | 0x2F | City wall | 1 | one merchant |  |
| p3data/town_tick/core.cpp | 0x527D40 | 0x527F53 | 0x35 | Famine | 4 | -2 (chronicle) |  |
| p3data/town_tick/diet.cpp | 0x4E09D4 | 0x4E0CEE | 0x48 | Boycott | 1 | one merchant |  |
| p3data/town_tick/diet.cpp | 0x4E09D4 | 0x4E0F4C | 0x4C | Boycott | 1 | one merchant |  |
| p3data/town_tick/diet.cpp | 0x4E09D4 | 0x4E1371 | 0x5C | Boycott ended | 1 | one merchant |  |
| p3data/town_tick/diet.cpp (named) | 0x4F3AF0 | 0x4F3C99 | 0x3D | Writing | 1 | one merchant |  |
| p3data/town_tick/events.cpp | 0x4E2144 | 0x4E22A8 | 0x81 | Hulk | 2 | -1 (each merchant) |  |
| p3data/town_tick/events.cpp (named) | 0x4ECF64 | 0x4EDDEB | script |  |  | one merchant | mission interpreter: letter built by the script (ebx) |
| p3data/town_tick/events.cpp (named) | 0x4ECF64 | 0x4EDE69 | script |  |  | one merchant | mission interpreter: letter built by the script (ebx) |
| p3data/town_tick/events.cpp (named) | 0x4ECF64 | 0x4EDEAA | script |  |  | one merchant | mission interpreter: letter built by the script (ebx) |
| p3data/town_tick/events.cpp (named) | 0x4ECF64 | 0x4EDFED | 0x45/0x46 |  |  | one merchant | mission interpreter: 0x45 + (flag & 1) |
| p3data/town_tick/events.cpp (named) | 0x4ECF64 | 0x4F2A67 | 0x22 | Proposal | 2 | one merchant |  |
| p3data/town_tick/events_big.cpp | 0x4E5B84 | 0x4E5D4D | 0x19 | Indictment | 1 | one merchant |  |
| p3data/town_tick/events_big.cpp | 0x4E5B84 | 0x4E5F2A | 0x4A | Acquitted | 1 | one merchant |  |
| p3data/town_tick/events_big.cpp | 0x4E5B84 | 0x4E6286 | 0x4B | Verdict | 1 | one merchant |  |
| p3data/town_tick/events_big.cpp | 0x4E8234 | 0x4E8613 | 0x21 | Suitor | 2 | one merchant |  |
| p3data/town_tick/events_mayor.cpp | 0x4DC1F0 | 0x4DC53D | 0x0B | Hanse Day | 1 | one merchant |  |
| p3data/town_tick/events_mayor.cpp | 0x4E2CD4 | 0x4E32E9 | 0x2C/0x2D |  |  | one merchant | auction: 0x2C + (buyer is the owner) |
| p3data/town_tick/events_mayor.cpp | 0x4E2CD4 | 0x4E3378 | 0x5D | Auctioned | 2 | one merchant |  |
| p3data/town_tick/events_mayor.cpp | 0x4E2CD4 | 0x4E33FC | 0x2E | Keep | 1 | one merchant |  |
| p3data/town_tick/events_mayor.cpp | 0x4E2CD4 | 0x4E3598 | 0x7D | Bought | 1 | one merchant |  |
| p3data/town_tick/events_mayor.cpp | 0x4E2CD4 | 0x4E363A | 0x7F | Bought | 2 | one merchant |  |
| p3data/town_tick/events_mayor.cpp | 0x4E2CD4 | 0x4E36B3 | 0x7E | Kept | 1 | one merchant |  |
| p3data/town_tick/events_mayor.cpp | 0x528E60 | 0x5294F7 | 0x47 | Elections | 1 | one merchant |  |
| p3data/town_tick/events_mayor.cpp | 0x528E60 | 0x5295BD | 0x55 | Lord Mayor | 4 | -2 (chronicle) |  |
| p3data/town_tick/events_mayor.cpp (named) | 0x5299F0 | 0x529CA0 | 0x47 | Elections | 1 | -1 (each merchant) |  |
| p3data/town_tick/events_mayor.cpp (named) | 0x5299F0 | 0x529D8A | 0x57 | Hanse Day | 4 | -2 (chronicle) |  |
| p3data/town_tick/events_rare.cpp | 0x4DFD44 | 0x4E0065 | 0x41 | Pirate Hunt | 1 | one merchant |  |
| p3data/town_tick/events_rare.cpp | 0x4DFD44 | 0x4E00CF | 0x42 | Hunt ended | 1 | one merchant |  |
| p3data/town_tick/events_rare.cpp | 0x4DFD44 | 0x4E02B6 | 0x43 | Plunder | 2 | one merchant |  |
| p3data/town_tick/events_rare.cpp | 0x4E6A54 | 0x4E79A3 | 0x63 | Expedition | 3 | one merchant |  |
| p3data/town_tick/events_rare.cpp | 0x4E9094 | 0x4E9389 | 0x58 | Plague | 4 | -2 (chronicle) |  |
| p3data/town_tick/events_rare.cpp | 0x4E9A94 | 0x4E9F8F | 0x6B | Council Meeting | 1 | one merchant |  |
| p3data/town_tick/events_rare.cpp | 0x4E9A94 | 0x4E9FBD | 0x6B | Council Meeting | 1 | one merchant |  |
| p3data/town_tick/events_rare.cpp | 0x4EA594 | 0x4EA72B | 0x77 | Hanse Day | 1 | one merchant |  |
| p3data/town_tick/events_rare.cpp | 0x4EA594 | 0x4EA77F | 0x77 | Hanse Day | 1 | -1 (each merchant) |  |
| p3data/town_tick/events_rare.cpp | 0x4EA594 | 0x4EA825 | 0x77 | Hanse Day | 1 | one merchant |  |
| p3data/town_tick/events_rare.cpp | 0x4EA594 | 0x4EA8B0 | 0x77 | Hanse Day | 1 | one merchant |  |
| p3data/town_tick/events_rare.cpp | 0x4EA594 | 0x4EA979 | 0x77 | Hanse Day | 1 | -1 (each merchant) |  |
| p3data/town_tick/events_rare.cpp | 0x4EA594 | 0x4EAB45 | 0x78 | Hanse Day | 1 | -1 (each merchant) |  |
| p3data/town_tick/events_rare.cpp | 0x4EAB94 | 0x4EAE01 | 0x6C | Extra Tax | 1 | one merchant |  |
| p3data/town_tick/events_rare.cpp | 0x4EC934 | 0x4ECAEC | 0x73 | Theft | 2 | one merchant |  |
| p3data/town_tick/events_rare.cpp | 0x4ECB14 | 0x4ECDAE | 0x7B | Monument | 1 | -2 (chronicle) |  |
| p3data/town_tick/events_rare.cpp | 0x4ECB14 | 0x4ECDBF | 0x7B | Monument | 1 | -1 (each merchant) |  |
| p3data/town_tick/events_rare.cpp | 0x502B20 | 0x502CDF | 0x34 | Loss | 2 | one merchant |  |
| p3data/town_tick/events_rare.cpp | 0x52D0B0 | 0x52D1E7 | 0x1C | Discovery | 4 | -1 (each merchant) |  |
| p3data/town_tick/events_rare.cpp | 0x52D0B0 | 0x52D1F8 | 0x1C | Discovery | 4 | -2 (chronicle) |  |
| p3data/town_tick/events_rare.cpp | 0x5319D0 | 0x531B46 | 0x56 | Discovery | 4 | -2 (chronicle) |  |
| p3data/town_tick/events_towns.cpp | 0x4DB4E0 | 0x4DB5BE | 0x26 | Repayment | 2 | one merchant |  |
| p3data/town_tick/events_towns.cpp | 0x4DB4E0 | 0x4DB652 | 0x79 | Repayment | 2 | one merchant |  |
| p3data/town_tick/events_towns.cpp | 0x4DB4E0 | 0x4DB6C7 | 0x12 | Maturity | 2 | one merchant |  |
| p3data/town_tick/events_towns.cpp | 0x4E23A4 | 0x4E2570 | 0x24 | Festival | 2 | one merchant |  |
| p3data/town_tick/events_towns.cpp | 0x4E7C74 | 0x4E7DA1 | 0x23 | Marriage | 4 | -2 (chronicle) |  |
| p3data/town_tick/events_towns.cpp | 0x4E7C74 | 0x4E80E5 | 0x25 | Dowry | 2 | one merchant |  |
| p3data/town_tick/events_towns.cpp | 0x4E9564 | 0x4E998A | 0x59 | Fire | 4 | -2 (chronicle) |  |
| p3data/town_tick/events_towns.cpp | 0x4F6570 | 0x4F6618 | 0x13 | Impound | 1 | one merchant |  |
| p3data/town_tick/events_towns.cpp | 0x4F6570 | 0x4F66FA | 0x13 | Impound | 1 | one merchant |  |
| p3data/town_tick/events_towns.cpp | 0x4F6570 | 0x4F6818 | 0x13 | Impound | 1 | one merchant |  |
| p3data/town_tick/events_towns.cpp | 0x4F6570 | 0x4F68A3 | 0x13 | Impound | 1 | one merchant |  |
| p3data/town_tick/events_towns.cpp | 0x500C30 | 0x500E8F | 0x75 | Fire | 2 | one merchant |  |
| p3data/town_tick/events_towns.cpp | 0x5308A0 | 0x530C53 | 0x0D | Opening | 4 | -2 (chronicle) |  |
| p3data/town_tick/events_towns.cpp | 0x538A80 | 0x538C16 | 0x4D | Captain | 2 | one merchant |  |
| p3data/town_tick/facilities.cpp | 0x5083B0 | 0x5084A5 | 0x00 | Launching | 2 | one merchant |  |
| p3data/town_tick/facilities.cpp | 0x51FF30 | 0x5202EF | 0x03 | Completion | 2 | one merchant |  |
| p3data/town_tick/merchants.cpp | 0x4F7510 | 0x4F75AD | 0x5E | Reputation | 1 | one merchant |  |
| p3data/town_tick/merchants.cpp | 0x4F7510 | 0x4F75C2 | 0x5F | ? | 4 | one merchant |  |
| p3data/town_tick/merchants.cpp | 0x4F75F0 | 0x4F7827 | 0x6D | Alderman | 1 | one merchant |  |
| p3data/town_tick/merchants.cpp | 0x4F75F0 | 0x4F793D | 0x82 | Permission | 1 | one merchant |  |
| p3data/town_tick/merchants.cpp | 0x4F7BB0 | 0x4F7CB0 | 0x0F | Dump | 4 | one merchant |  |
| p3data/town_tick/merchants.cpp | 0x4F7BB0 | 0x4F7CF6 | 0x10 | Dump | 4 | one merchant |  |
| p3data/town_tick/merchants.cpp | 0x4F7BB0 | 0x4F7D33 | 0x11 | Tax Bill | 3 | one merchant |  |
| p3data/town_tick/merchants.cpp | 0x4F7BB0 | 0x4F82D9 | 0x06 | Promotion | 4 | -2 (chronicle) |  |
| p3data/town_tick/offices.cpp | 0x4FFF20 | 0x500280 | 0x60 | Office ready | 2 | one merchant |  |
| p3data/town_tick/operations.cpp | 0x538C40 | 0x538D76 | 0x52 | Captain | 2 | one merchant |  |
| p3data/town_tick/operations.cpp | 0x53DA90 | 0x53DBC7 | 0x33 | Marriage | 2 | one merchant |  |
| p3data/town_tick/operations.cpp | 0x53DDF0 | 0x53DED7 | 0x7C | Manager | 2 | one merchant |  |
| p3data/town_tick/operations.cpp | 0x53FEA0 | 0x53FFF4 | 0x7A | Baby boom | 2 | one merchant |  |
| p3data/town_tick/sieges.cpp | 0x6281F0 | 0x628A52 | 0x38 | Siege | 4 | -2 (chronicle) |  |
| p3data/town_tick/sieges.cpp | 0x6281F0 | 0x629A2B | 0x38 | Siege | 4 | -2 (chronicle) |  |
| p3data/town_tick/sieges.cpp | 0x629C60 | 0x629F63 | 0x53 | Siege | 4 | -2 (chronicle) |  |
| p3data/town_tick/sieges.cpp | 0x633DB0 | 0x6345D3 | 0x6A | Plundered | 2 | one merchant |  |
| p3data/town_tick/sieges.cpp | 0x636230 | 0x6364E9 | 0x85 | Prince | 1 | one merchant |  |
| — a trade route ends (0x503230, 0x518860) | 0x4D5200 | 0x4D55F9 | 0x07 | Ship trade | 3 | one merchant |  |
| — battle in a town (method) | 0x5FE7E0 | 0x5FEA54 | 0x3B | Outrigger | 1 | one merchant | battle in a town |
| — battle in a town (method) | 0x5FE7E0 | 0x5FEEAA | 0x39/0x3A |  |  | -2 (chronicle) | battle in a town |
| — battle in a town (method) | 0x5FE7E0 | 0x5FF55F | 0x29 | Naval battle | 2 | one merchant | battle in a town: report buffer |
| — event 0x09 | 0x4E2824 | 0x4E2941 | 0x08 | Good harvest | 2 | one merchant |  |
| — event 0x09 | 0x4E2824 | 0x4E299D | 0x09 | Harvest failure | 4 | -2 (chronicle) |  |
| — event 0x09 | 0x4E2824 | 0x4E2A9E | 0x08 | Good harvest | 2 | one merchant |  |
| — event 0x09 | 0x4E2824 | 0x4E2B04 | 0x09 | Harvest failure | 4 | -2 (chronicle) |  |
| — event 0x0C | 0x4E38B4 | 0x4E3B4B | 0x67 | Return | 3 | one merchant |  |
| — event 0x0C | 0x4E38B4 | 0x4E3C6E | 0x68 | Ambush | 3 | one merchant |  |
| — event 0x0C | 0x4E38B4 | 0x4E3CFC | 0x67 | Return | 3 | one merchant |  |
| — event 0x0C | 0x4E38B4 | 0x4E3D43 | 0x69 | Fled | 3 | one merchant |  |
| — event 0x0C | 0x4E38B4 | 0x4E3E95 | 0x66 | Arrived | 3 | one merchant |  |
| — event 0x0F | 0x4E5664 | 0x4E5925 | 0x74 | Broken in | 2 | one merchant |  |
| — event 0x0F | 0x4E5664 | 0x4E594D | 0x74 | Broken in | 2 | one merchant |  |
| — event 0x0F | 0x4E5664 | 0x4E5AFF | 0x20 | Robbed | 2 | one merchant |  |
| — event 0x0F | 0x4E5664 | 0x4E5B1E | 0x15 | Burglary | 2 | one merchant |  |
| — event 0x10 | 0x4E63E4 | 0x4E65CC | 0x18 | Traveller | 2 | one merchant |  |
| — event 0x10 | 0x4E63E4 | 0x4E6691 | 0x18 | Traveller | 2 | one merchant |  |
| — event 0x11 | 0x4E6744 | 0x4E6878 | 0x5B | Dishonoured | 2 | one merchant |  |
| — event 0x11 | 0x4E6744 | 0x4E6987 | 0x5A | Fulfilled | 3 | one merchant |  |
| — event 0x17 | 0x4E8684 | 0x4E87CF | 0x22 | Proposal | 2 | one merchant |  |
| — event 0x18 | 0x4E8804 | 0x4E8975 | 0x62 | Dividend | 2 | one merchant |  |
| — event 0x18 | 0x4E8804 | 0x4E8A49 | 0x28 | Dividend | 2 | one merchant |  |
| — event 0x18 | 0x4E8804 | 0x4E8AD2 | 0x27 | Dividend | 3 | one merchant |  |
| — event 0x28 | 0x4DDF40 | 0x4DE32B | 0x51 | Captain | 2 | one merchant |  |
| — event 0x28 | 0x4DDF40 | 0x4DE394 | 0x50 | Captain | 2 | one merchant |  |
| — event 0x28 | 0x4DDF40 | 0x4DE485 | 0x4E | Captain | 2 | one merchant |  |
| — interface (a window, from 0x410A02) | 0x419EB0 | 0x41B16D | 0x45 | — | 4 | player (0x6DFC14) | interface window 0x419EB0 (rand) |
| — interface (a window, from 0x410A02) | 0x419EB0 | 0x41B4D3 | 0x21 | Suitor | 2 | player (0x6DFC14) | interface window 0x419EB0 (rand) |
| — new game (0x541DC4, 0x5425D1, 0x542B9D) | 0x530470 | 0x530625 | 0x0E | Move | 4 | -2 (chronicle) |  |
| — operation 0x03 | 0x52ACD0 | 0x52AEDD | 0x02 | No repair | 3 | one merchant |  |
| — operation 0x06 | 0x537EE0 | 0x537FC6 | 0x04 | New captain | 2 | one merchant |  |
| — operation 0x2F | 0x52B110 | 0x52B288 | 0x14 | Annulment | 1 | one merchant |  |
| — operation 0x3D | 0x53A620 | 0x53A794 | 0x1A | Order | 2 | one merchant |  |
| — operation 0x3E | 0x53A810 | 0x53A910 | 0x0E | Move | 4 | -2 (chronicle) |  |
| — operation 0x4A | 0x53B1F0 | 0x53B40C | 0x78 | Hanse Day | 1 | -1 (each merchant) |  |
| — operation 0x53 | 0x53C840 | 0x53C9B5 | 0x17 | Traveller | 2 | one merchant |  |
| — operation 0x5F (0x53DA30) | 0x5338A0 | 0x533C9B | 0x1E | Memo | 2 | one merchant |  |
| — operation 0x5F (0x53DA30) | 0x5338A0 | 0x533CB5 | 0x1F | Offer | 2 | one merchant |  |
| — operation 0x72 | 0x53EAC0 | 0x53EBC8 | 0x2B | It's a pity | 2 | one merchant |  |
| — operation 0x77 | 0x53EEC0 | 0x53F6D6 | 0x83 | Impounding | 2 | one merchant |  |
| — operation 0x8B | 0x635B80 | 0x635C9F | 0x64 | Prince | 1 | one merchant |  |
| — operation 0x8C | 0x635CD0 | 0x635DA9 | 0x65 | Prince | 1 | one merchant |  |
| — operation 0x8F | 0x540240 | 0x5402BF | 0x38 | Siege | 4 | -2 (chronicle) |  |
| — operation 0x8F | 0x540240 | 0x5403DB | 0x37 | Extra tax | 1 | one merchant |  |
| — operation 0x90 (0x540490) | 0x634660 | 0x63477B | 0x38 | Siege | 4 | -2 (chronicle) |  |

## Appendix B. The types

«Kept»: how long 0x4D6780 keeps it (the option is the merchant's byte, an
index into the days table). «Sent from»: the calls of appendix A (−1: to
each merchant, −2: to the chronicle, UI: from the interface). «Composer
case»: the case of 0x47F3B0 (none: the default, no letter text; the class 4
types sent to −2 are read in the chronicle, 0x47DD40). «Main text»: the
longest text the case's code range refers to, as an orientation (the ranges
are cut at the next case, so some neighbours share it).

| Type | Title | Class | Kept | Sent from | Composer case | Main text (English exe) |
|---|---|---|---|---|---|---|
| 0x00 | Launching | 2 | option [+0x39] | 0x5084A5 | 0x47F5F1 | The shipyard in %s has finished %s for you today. The ship is fully sea worthy at ancho… |
| 0x01 | Shipyard order | 3 | option [+0x3A] | — | 0x47F625 | The shipyard in %s is repairing %s for you. The actual condition amounts to %i%c. You w… |
| 0x02 | No repair | 3 | option [+0x3A] | 0x52AEDD | 0x47F666 | %s %s cannot be repaired. The shipyard management in %s has found out that you cannot  … |
| 0x03 | Completion | 2 | option [+0x39] | 0x5202EF | 0x47F70C | The construction work on a building in %s was completed on %s. %s %s is ready for use b… |
| 0x04 | New captain | 2 | option [+0x39] | 0x537FC6 | 0x47F76F | Captain %s %s has just completed formalities with the Harbour Master in %s and taken up… |
| 0x05 | Retirement | 2 | option [+0x39] | — | 0x47F7F2 | The captain %s %s has hired a crew today. He is leaving in %s the %s, on which he last … |
| 0x06 | Promotion | 4 | forever | 0x4F82D9 (−2) | none (default) |  |
| 0x07 | Ship trade | 3 | option [+0x3A] | 0x5035CD, 0x4D55F9 | 0x47F866 | Your convoy under the leadership of %s went to anchor today in the harbour of %s.  |
| 0x08 | Good harvest | 2 | option [+0x39] | 0x4E2941, 0x4E2A9E | none (default) |  |
| 0x09 | Harvest failure | 4 | forever | 0x4E299D (−2), 0x4E2B04 (−2) | none (default) |  |
| 0x0A | Charge | 1 | option [+0x38] | — | 0x47F980 |  We will thoroughly examine this charge and inform you of the outcome of investigations… |
| 0x0B | Hanse Day | 1 | option [+0x38] | 0x4DC53D | 0x47FA4C |  Hanseatic League conference day in %s. \l \dThe town of %s, which %s, was the subject … |
| 0x0C | Guild entry | 4 | forever | 0x4F8639 (−2) | none (default) |  |
| 0x0D | Opening | 4 | forever | 0x530C53 (−2) | none (default) |  |
| 0x0E | Move | 4 | forever | 0x530625 (−2), 0x53A910 (−2) | none (default) |  |
| 0x0F | Dump | 4 | 7 days | 0x4F7CB0 | none (default) |  |
| 0x10 | Dump | 4 | 7 days | 0x4F7CF6 | none (default) |  |
| 0x11 | Tax Bill | 3 | 7 days | 0x4F7D33 | 0x47FB96 | Taxes for your trading offices and workshops last week amounted to %u\C.  |
| 0x12 | Maturity | 2 | option [+0x39] | 0x4DB6C7 | 0x47FDB3 |  today. The loan has to be repaid within the next 10 days; otherwise, you run the risk … |
| 0x13 | Impound | 1 | option [+0x38] | 0x4F6618, 0x4F66FA, 0x4F6818, 0x4F68A3 | 0x47FE79 | Because you are in arrears with your payments, goods in %s to the value of %u\C will be… |
| 0x14 | Annulment | 1 | option [+0x38] | 0x52B288 | 0x47FF50 | Your order of %i. %s %i for %s in %s was cancelled. After subtraction of a small fee, %… |
| 0x15 | Burglary | 2 | option [+0x39] | 0x4E5B1E | 0x47FFF4 | The burglary at %s %s was extremely successful. Your share of the stolen gold amounts t… |
| 0x16 | Burglary | 2 | option [+0x39] | — | 0x480164 | Just a bit of information: The burglary at %s %s did not, unfortunately, go according t… |
| 0x17 | Traveller | 2 | option [+0x39] | 0x53C9B5 | 0x4801A1 | Traveller from %s  \dTake me with your ship %s at the latest by %i. %s to %s. I will pa… |
| 0x18 | Traveller | 2 | option [+0x39] | 0x4E65CC, 0x4E6691 | 0x480221 | Traveller from %s  \dMany thanks for the quick and safe transportation. I am most oblig… |
| 0x19 | Indictment | 1 | option [+0x38] | 0x4E5D4D | 0x480256 | \dYou stand accused of taking part in a punishable offence, undermining the good of the… |
| 0x1A | Order | 2 | option [+0x39] | 0x53A794 | 0x480581 | \dMany thanks for taking care of this small transport order for me. Meet at the latest … |
| 0x1B | Sank | 2 | option [+0x39] | 0x519E87 | 0x480694 | What a tragic lost: %s %s sank in the floods today, taking %i sailors and the captain %… |
| 0x1C | Discovery | 4 | forever | 0x52D1E7 (−1), 0x52D1F8 (−2) | none (default) |  |
| 0x1D | Expedition | 2 | option [+0x39] | — | 0x480849 |  |
| 0x1E | Memo | 2 | option [+0x39] | 0x533C9B | 0x480868 | Dear Miss %s %s,%c%cI would like to make you an offer: %cI offer  |
| 0x1F | Offer | 2 | option [+0x39] | 0x533CB5 | 0x480868 | Dear Miss %s %s,%c%cI would like to make you an offer: %cI offer  |
| 0x20 | Robbed | 2 | option [+0x39] | 0x4E5AFF | 0x4809C5 | The following goods were destroyed in the burglary %u\B. |
| 0x21 | Suitor | 2 | option [+0x39] | 0x4E8613, 0x41B4D3 (UI) | 0x480ADE | Dear Mr. %s,  are you interested in getting married? For the small sum of %i\C, I could… |
| 0x22 | Proposal | 2 | option [+0x39] | 0x4F2A67, 0x4E87CF | 0x480BF2 | Dear Miss %s %s,  the gentleman in question is the handsome %s %s from %s. In case of a… |
| 0x23 | Marriage | 4 | forever | 0x4E7DA1 (−2) | none (default) |  |
| 0x24 | Festival | 2 | option [+0x39] | 0x4E2570 | 0x480DF8 | \dThis was not really a great celebration; it was really quite pathetic! No more than %… |
| 0x25 | Dowry | 2 | option [+0x39] | 0x4E80E5 | 0x480E38 | The marriage was not only socially profitable but also financially, then as dowry you a… |
| 0x26 | Repayment | 2 | option [+0x39] | 0x4DB5BE | 0x480FA5 | A loan of %u\C that you gave on %u. %s %u in %s was repaid today. You will find a few w… |
| 0x27 | Dividend | 3 | option [+0x3A] | 0x4E8AD2 | 0x48102A |  You will receive a dividend of %u\C for your %u shares in this company. |
| 0x28 | Dividend | 2 | option [+0x39] | 0x4E8A49 | 0x48111C | Your company could not pay out a dividend of %u\C today. Repay the remaining debt of %u… |
| 0x29 | Naval battle | 2 | option [+0x39] | 0x60DD93, 0x5FF55F | 0x481167 |  |
| 0x2A | Pirates | 2 | option [+0x39] | 0x50C326 (−2), 0x60EB42 (−2), 0x60C31B (−2), 0x514F79 (−2), 0x61FC80 (−2) | 0x481780 | %s %s %s %s %s was today attacked by the pirate %s %s and lost %d ships to the buccaneer. |
| 0x2B | It's a pity | 2 | option [+0x39] | 0x53EBC8 | 0x4811CA | Unfortunately, I got your message too late, so I cannot do anything for you at the mome… |
| 0x2C | Auctioned | 1 | option [+0x38] | 0x4E32E9 | 0x48120D |  . The sum has already been paid to you. We have taken the liberty of retaining a small… |
| 0x2D | Auctioned | 1 | option [+0x38] | 0x4E32E9 | 0x481472 |   Congratulations! You have bought your own ship. We find it somewhat difficult to unde… |
| 0x2E | Keep | 1 | option [+0x38] | 0x4E33FC | 0x48162F |  today. Your ship has been returned to anchor in the harbour. |
| 0x2F | City wall | 1 | option [+0x38] | 0x523702 | 0x4819B2 | , made an effort to protect your building, we the town, take the liberty of billing you… |
| 0x30 | Captured | 2 | option [+0x39] | 0x540DDF, 0x60D189 | 0x481B02 |  and plundered it. Your share of the goods and gold amounts to  |
| 0x31 | Convoy | 1 | option [+0x38] | 0x50AA02 | 0x481CA2 |  in god shape. You have received a bonus of  |
| 0x32 | Convoy | 1 | option [+0x38] | 0x50A966 | 0x481D5D | %i\C was transferred to the convoy leader for it's participation in this convoy. |
| 0x33 | Marriage | 2 | option [+0x39] | 0x53DBC7 | 0x481E82 | . In order to appropriately cater for all guests of rank and name, please make sure tha… |
| 0x34 | Loss | 2 | option [+0x39] | 0x502CDF | 0x481F3B | %s %s sunk on the expedition to the South Seas on %u. %s %u. Unfortunately, all records… |
| 0x35 | Famine | 4 | forever | 0x527F53 (−2) | none (default) |  |
| 0x36 | Town wall | 1 | option [+0x38] | — | 0x481FBD | ,  the extension to our town wall was recently completed. Because you, as the Lord Mayo… |
| 0x37 | Extra tax | 1 | option [+0x38] | 0x5403DB | 0x48208F |   you will certainly have noticed that our town was recently plundered. In order to spe… |
| 0x38 | Siege | 4 | forever | 0x628A52 (−2), 0x629A2B (−2), 0x5402BF (−2), 0x63477B (−2) | none (default) |  |
| 0x39 | Pirates | 4 | forever | 0x5FEEAA (−2) | none (default) |  |
| 0x3A | Pirates | 4 | forever | 0x5FEEAA (−2) | none (default) |  |
| 0x3B | Outrigger | 1 | option [+0x38] | 0x5FEA54 | 0x4821E0 |  \l After you had placed one of your ships for the defence of the harbour at our dispos… |
| 0x3C | Message | 4 | forever | — | none (default) |  |
| 0x3D | Writing | 1 | option [+0x38] | 0x4F3C99 | 0x482296 |  |
| 0x3E | Letter | 2 | option [+0x39] | — | 0x482296 |  |
| 0x3F | Info | 3 | option [+0x3A] | — | 0x482296 |  |
| 0x40 | — | 4 | forever | — | none (default) |  |
| 0x41 | Pirate Hunt | 1 | option [+0x38] | 0x4E0065 | 0x48238B |  sent the pirate. It is therefore assumed that more pirates are on their way. The membe… |
| 0x42 | Hunt ended | 1 | option [+0x38] | 0x4E00CF | 0x482531 | Many thanks for your help. It seems as if the pirates have been stopped and the town  h… |
| 0x43 | Plunder | 2 | option [+0x39] | 0x4E02B6 | 0x48269B | \cPlundering in %s. \l Your trading office in %s was completely plundered by an angry m… |
| 0x44 | — | 4 | forever | — | none (default) |  |
| 0x45 | — | 4 | forever | 0x4EDFED, 0x41B16D (UI) | none (default) |  |
| 0x46 | Map | 2 | forever | 0x4EDFED | 0x4826C3 |  |
| 0x47 | Elections | 1 | option [+0x38] | 0x5294F7, 0x529CA0 (−1) | 0x48116F |  |
| 0x48 | Boycott | 1 | option [+0x38] | 0x4E0CEE | 0x482705 |  Ensure your ship joins the hunt convoy by %u. %u. %u at the latest. The convoy will sh… |
| 0x49 | Town Guard | 1 | option [+0x38] | — | 0x482C57 | , your share of the costs for town guards last month came to  |
| 0x4A | Acquitted | 1 | option [+0x38] | 0x4E5F2A | 0x482D8D |   \dWe have today agreed, that due to a lack of evidence, the facts of the case of pira… |
| 0x4B | Verdict | 1 | option [+0x38] | 0x4E6286 | 0x48308F |   \dThese monsterous allegations against you have strengthend and it has now been prove… |
| 0x4C | Boycott | 1 | option [+0x38] | 0x4E0F4C | 0x4833EF | You were requested to provide one of your ships to help with the blockade of the port o… |
| 0x4D | Captain | 2 | option [+0x39] | 0x538C16 | 0x483566 | %u\C. He has gained so much experience that he sees no problem in justifying the new wa… |
| 0x4E | Captain | 2 | option [+0x39] | 0x4DE485 | 0x483656 |   \dThank you for your generous offer  , however, I am very satisfied here at the momen… |
| 0x4F | Captain | 2 | option [+0x39] | — | 0x48378A |   you should get enough money together the next time, before you try to woo a captain o… |
| 0x50 | Captain | 2 | option [+0x39] | 0x4DE394 | 0x483854 |  and will be starting shortly afterwards at one of your competitors.  \rFare thee well!… |
| 0x51 | Captain | 2 | option [+0x39] | 0x4DE32B | 0x483946 |   I accept your generous offer with profound gratitude. I await you on the  |
| 0x52 | Captain | 2 | option [+0x39] | 0x538D76 | 0x483A8C |   the weight of age lies heavily upon my shoulders. It is with deep regrets that announ… |
| 0x53 | Siege | 4 | forever | 0x629F63 (−2) | none (default) |  |
| 0x54 | Pirates | 4 | forever | 0x50C274, 0x50C853, 0x540A8C, 0x60C383 | none (default) |  |
| 0x55 | Lord Mayor | 4 | forever | 0x5295BD (−2) | none (default) |  |
| 0x56 | Discovery | 4 | forever | 0x531B46 (−2) | none (default) |  |
| 0x57 | Hanse Day | 4 | forever | 0x529D8A (−2) | none (default) |  |
| 0x58 | Plague | 4 | forever | 0x4E9389 (−2) | none (default) |  |
| 0x59 | Fire | 4 | forever | 0x4E998A (−2) | none (default) |  |
| 0x5A | Fulfilled | 3 | option [+0x3A] | 0x4E6987 | 0x483BC0 |  at the agreed time. We would like to take this opportunity to thank you once again and… |
| 0x5B | Dishonoured | 2 | option [+0x39] | 0x4E6878 | 0x483DA9 | . Unfortunately, we were very disappointed and will have to think twice about giving yo… |
| 0x5C | Boycott ended | 1 | option [+0x38] | 0x4E1371 | 0x483EED |  Thank you for placing your ship for guarding the harbour at our disposal. We return it… |
| 0x5D | Auctioned | 2 | option [+0x39] | 0x4E3378 | 0x483FFE |  you have auctioned for %u\C %s %s from %s %s. |
| 0x5E | Reputation | 1 | option [+0x38] | 0x4F75AD | 0x4840AB |  is sufficient to arise in the next rank. However, unfortunately, you are not wealthy e… |
| 0x5F | ? | 4 | forever | 0x4F75C2 | none (default) |  |
| 0x60 | Office ready | 2 | option [+0x39] | 0x500280 | 0x484117 |  This is to inform you that your trading office in %s is now ready for use.  The Board … |
| 0x61 | Trading Office upgrade | 2 | option [+0x39] | — | 0x484144 |  This is to inform you that the capacity of your trading office in %s has been extended… |
| 0x62 | Dividend | 2 | option [+0x39] | 0x4E8975 | 0x484178 |  shares of your busi_ness, thus re_ceives  |
| 0x63 | Expedition | 3 | option [+0x3A] | 0x4E79A3 | 0x48115F |  |
| 0x64 | Prince | 1 | option [+0x38] | 0x635C9F | 0x48435A | \l%s %s %s  \l\dThere are great supply shortages in our country at this time. We would … |
| 0x65 | Prince | 1 | option [+0x38] | 0x635DA9 | 0x4844E9 | \l%s %s %s  \l\dMeet my emissary by %d. %s. at the town gate in order to hand the %s ov… |
| 0x66 | Arrived | 3 | option [+0x3A] | 0x4E3E95 | 0x484683 | Your cart convoy from %s has arrived in %s. |
| 0x67 | Return | 3 | option [+0x3A] | 0x4E3B4B, 0x4E3CFC | 0x4846C0 | Your cart convoy from %s was unable to continue it's journey, as the destination of %s … |
| 0x68 | Ambush | 3 | option [+0x3A] | 0x4E3C6E | 0x4846F5 | Your cart convoy from %s has arrived in %s. |
| 0x69 | Fled | 3 | option [+0x3A] | 0x4E3D43 | 0x48473E | Everywhere is crawling with the Prince's troops! As we didn't know what on earth to do,… |
| 0x6A | Plundered | 2 | option [+0x39] | 0x6345D3 | 0x484764 |  plundered. Your share of the goods and gold amounts to  |
| 0x6B | Council Meeting | 1 | option [+0x38] | 0x4E9F8F, 0x4E9FBD | 0x484899 | an extra tax of %u\C will be levied to fill the town's coffers. A demand will shortly b… |
| 0x6C | Extra Tax | 1 | option [+0x38] | 0x4EAE01 | 0x4849C0 | As you did not have enough money you are required to pay the shortfall in the next 7 days. |
| 0x6D | Alderman | 1 | option [+0x38] | 0x4F7827 | 0x484AE8 | ,  The Hanseatic League council has long been observing your meteoric rise in establish… |
| 0x6E | Pirate hideaway | 1 | option [+0x38] | 0x4EC230 | 0x484D7A |  |
| 0x6F | Reputation | 1 | option [+0x38] | 0x4EC4FC (−2), 0x4EC51C | 0x484DC9 | Your intervention has ensured completion of the overland trade route between %s and %s. |
| 0x70 | Unsuccessful | 1 | option [+0x38] | 0x4EB32B | 0x484DC9 | Your intervention has ensured completion of the overland trade route between %s and %s. |
| 0x71 | — | 4 | until record [+0x4] | — | none (default) |  |
| 0x72 | — | 4 | 28 days | 0x60EC1B, 0x60EC83 | none (default) |  |
| 0x73 | Theft | 2 | option [+0x39] | 0x4ECAEC | 0x485175 |  \lWe should employ more guards to prevent any more fiendish attacks. |
| 0x74 | Broken in | 2 | option [+0x39] | 0x4E5925, 0x4E594D | 0x485303 |  However the ever vigilant guards were able to capture the culprit. He was immediately … |
| 0x75 | Fire | 2 | option [+0x39] | 0x500E8F | 0x4854BA |  \lOne of our warehouses has been lost in the town fire. |
| 0x76 | Auctioned off | 1 | option [+0x38] | — | 0x485604 | We have managed to sell your building (%$2$s) at the %$1$s Guild's auction. It was sold… |
| 0x77 | Hanse Day | 1 | option [+0x38] | 0x4EA72B, 0x4EA77F (−1), 0x4EA825, 0x4EA8B0, 0x4EA979 (−1) | 0x4857D8 | \cHanse Day was not called \l It is your duty as the Hanseatic League's Alderman to cal… |
| 0x78 | Hanse Day | 1 | option [+0x38] | 0x4EAB45 (−1), 0x53B40C (−1) | 0x485AE3 | \f1_On %d. %s %d the town of %s will be calling an extraordinary Hanse Day. The matter … |
| 0x79 | Repayment | 2 | option [+0x39] | 0x4DB652 | 0x485D0A | You should be lenient and release him from this debt, it would do your reputation much … |
| 0x7A | Baby boom | 2 | option [+0x39] | 0x53FFF4 | 0x485F3B | Diary Entry  Rejoice! Sing and shout from the rooftops! Today our darling daughter %s w… |
| 0x7B | Monument | 1 | option [+0x38] | 0x4ECDAE (−2), 0x4ECDBF (−1) | 0x485F91 |  It should forever remind us of one of the best Mayors the town of %s has ever had. |
| 0x7C | Manager | 2 | option [+0x39] | 0x53DED7 | 0x48635B |  \cManager gains experience  \lYour trade office manager in %s has gained much trading … |
| 0x7D | Bought | 1 | option [+0x38] | 0x4E3598 | 0x485604 | We have managed to sell your building (%$2$s) at the %$1$s Guild's auction. It was sold… |
| 0x7E | Kept | 1 | option [+0x38] | 0x4E36B3 | 0x485604 | We have managed to sell your building (%$2$s) at the %$1$s Guild's auction. It was sold… |
| 0x7F | Bought | 2 | option [+0x39] | 0x4E363A | 0x485604 | We have managed to sell your building (%$2$s) at the %$1$s Guild's auction. It was sold… |
| 0x80 | Overland trade | 1 | option [+0x38] | 0x4EC18F | 0x48604E |  Bring building materials to the town's traders in front of the town gates. Part delive… |
| 0x81 | Hulk | 2 | option [+0x39] | 0x4E22A8 (−1) | 0x4861BF | Hulk can now be built  It is with great pride that the shipyard in %s can announce to a… |
| 0x82 | Permission | 1 | option [+0x38] | 0x4F793D | 0x486222 |  Because of this you have been granted the right to create businesses in our town. Wish… |
| 0x83 | Impounding | 2 | option [+0x39] | 0x53F6D6 | 0x48116F |  |
| 0x84 | Order | 1 | option [+0x38] | 0x4EC2B2 | 0x486387 | ,  I hope you are aware of what you have let yourself in for here. You will be given a … |
| 0x85 | Prince | 1 | option [+0x38] | 0x6364E9 | 0x4845F5 | \l%s %s %s  \l\dYour delivery has helped me and my troops greatly. As agreed, here are … |
