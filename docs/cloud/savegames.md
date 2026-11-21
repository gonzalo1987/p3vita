# Cloud task: writing savegames

Branch `cloud/savegames`. Task: `p3vita-datos/tasks/savegames.md`. These notes
are written as the work goes; the sections "For the roadmap", "For the
inventory" and "Game bugs" are meant to be moved into the shared docs when
merging on the PC.

## Summary

p3vita now writes the save (`.pat`) the way Patrician III does: the game's
save function (0x5472F0) and every writer under it are rewritten in
`src/p3data/savegame_write.cpp` (`prepare_save`, `write_savegame`,
`savegame_file`), the game's LZ compressor in `src/p3data/lz_compress.cpp`,
and the `.pst` is written as the game writes it with empty statistics. The
game writes version 0x85; p3vita now reads 0x82 to 0x85. Checked against
the game's own code in the emulator (`write_oracle.py`), on top of main at
61cdbfa (fast oracle, storms on):

1. Byte for byte right after loading (`scripts/write-check.sh`): 0
   different bytes in 11 of the 12 saves of `missav/`; in save 7, 12 bytes
   of padding that the game itself writes without initializing (battles).
2. After 4 days of the whole world (`scripts/write-after-days.sh 4 1 3 7`):
   0 different calls, and the same 0 / 0 / 12 different bytes.
3. Round trip (`scripts/write-reload.sh`): the game's readers load what
   p3vita writes and p3vita loads it the same, 0 differences in the 12
   saves.
4. Compression: the game's compressor gives the same bytes as p3vita's on
   the 12 saves (and on 5 synthetic inputs in ctest).
5. Retouched saves for every branch the 12 saves don't reach, including the
   mailboxes of 0x4D6780 (`scripts/write-retouched.sh` on 1, 3, tut4): load
   0 differences, 0 different bytes.
6. The whole world, `world.sh 7` in the 12 saves in three batches: 0
   «distintas» (370 s). Built for the Vita (VPK, 1 min 17 s).

Game bugs found and reproduced: the wrong hometown name in the settings of
every save ("Cologne" for Lübeck), the uninitialized padding of the
battles, the nodes of the events 0x34 that change at each load and save,
and the notices of type ≥ 0x86 that cut a merchant's list and can lose the
free list. Two bugs of p3vita's reader fixed (events 0x1B, names).

## Decisions for the user

1. **The 12 bytes of save 7.** p3vita keeps the battles as in the file, so
   it writes the original 0xCD padding where the game (in the emulator)
   writes 0. Recommendation: leave it, and let the `battles` task zero bytes
   5..7 of each entry of the battles' table when it parses them (§2.1).
2. **ctest has 8 tests now** (`lz_test`). Recommendation: change the "7 of
   7" of the README of p3vita-datos and of `build.sh` when merging.
3. **The loader steps p3vita and the oracle don't do** (§1.3: the ship
   names in the names object, 0x5048C0/0x517C10 checking the ship routes,
   0x4D60B0 checking the trade routes). Recommendation: a small task for
   the loader; it isn't needed for saving, and `tick_probe --carga` can't
   see it because the oracle skips it too.
4. **The `.pst`.** p3vita writes it with empty statistics (what the game
   loads when there is none). Recommendation: keep it so until the daily
   statistics (0x54DE90) are rewritten (§7).
5. **The optional world object of 0x533DA0** (none of the saves has it):
   p3vita refuses a save that has it. Recommendation: rewrite it only if a
   save with it appears.
6. **Two loaders of the town names**: `load_town_names` (font.cpp, UTF-8,
   Spanish first, for showing them) and `load_save_scripts` (Latin-1, the
   English file first, as the English exe, for the writer).
   Recommendation: keep both; the writer needs the bytes the exe uses.

## Status

| Part | State |
|-|-|
| 1. Reader ↔ writer table of the game | done (§1) |
| 2. Oracle: the game's writer in the emulator | done (`tools/oracle/write_oracle.py`) |
| 3. Version 0x85 branches in the readers | done: only the free ship slots branch above 0x82 (§4) |
| 4. C++ writer (`savegame_write.cpp`), byte for byte | done for the 12 saves (§2) |
| 5. LZ compressor rewritten in C++ | done: same bytes as the game's in the 12 saves (§3) |
| 6. After playing, round trip, `.pst` | done: round trip (§4), after playing (§5), retouched states (§6), empty `.pst` (§7) |
| Merge of main 61cdbfa, all checks again | done (§8) |

What is left (none of it is needed to save a game the game loads): the real
statistics of the `.pst` (§7), the battles (kept as in the file; the
`battles` task), the optional object of [+0xE4] of the world, ship names
added by the player during a game (§6), and the loader steps of §1.3.

## 1. How the game saves

The save function is **0x5472F0** (thiscall on the game object 0x6DF2F0, the
same `ecx` the loader 0x5476E0 gives to 0x549F40; one argument). In order:

1. For each merchant `i < [0x6DE4AA]`: **0x4D6780(i)** on the mailbox
   0x6DD730 (expires old notices: `fn_4d6780` in
   `town_tick/events_towns.cpp`, the local session's, §2.3).
2. Builds the path (`Save\Ein\`, `Save\Kam\` or `Save\Mehr\` from byte
   [+0xD] of the settings, then the name, then `.pat`) and opens it with
   `CFile` (0x653C1D, mode 0x9001).
3. **0x549C60** → `n`, the size of the uncompressed `.pat`; two buffers of
   `n` and `n + 4` bytes; **0x549CC0(buffer)** writes the `.pat`;
   **0x54C410(src, dst, n)** compresses (on a local object built by
   0x54C3F0) and returns the compressed size; `CFile::Write` (0x653D74).
4. The same for the `.pst` on 0x6DFC90: **0x54D7F0** (size; it also sets
   the u16 [+0xA] to the number of objects of its list), **0x54D870**
   (writer), 0x54C410, write.

Every writer has the reader's shape: `thiscall(object, u8* out)` returns the
end. Every size function only reads.

**The game writes version 0x85** (first u32 `0x0085AFFE`), not the 0x82 of
the saves in `missav/`. So p3vita writes 0x85 and, for the round trip, must
read 0x85 too (part 3).

### 1.1 Reader ↔ writer ↔ size

Readers as in `docs/formats/savegame.md`. "Object" is `ecx`.

| Part | Object | Reader | Writer | Size |
|-|-|-|-|-|
| whole `.pat` | 0x6DF2F0 | 0x549F40 | 0x549CC0 | 0x549C60 |
| compression | local | 0x54C480 (decompress) | 0x54C410 → 0x6381E0 | |
| world | 0x6DE4A0 | 0x52FB60 | 0x52F870 | 0x52F760 |
| · town block | town | 0x51B520 | 0x51B330 | 0x51B2D0 |
| · · storage (towns and offices) | | 0x52DF20 | 0x52DE90 | |
| · · part 1 | | 0x527AC0 | 0x527910 | |
| · · part 2 | | 0x525E30 | 0x525D80 | |
| · · part 3 (records, `town_class1`) | | 0x51EC10 | 0x51EA30 | |
| · · · church | | 0x4FE240 | 0x4FE1E0 | |
| · · · `town_class1` (also the pirates' map) | | 0x4F9770 | 0x4F9680 | |
| · · part 4 | | 0x52A2B0 | 0x52A200 | |
| · · part 5 | | 0x528870 | 0x5287A0 | |
| · · facilities | | 0x510070 | 0x50FFF0 | |
| · · nodes [0x9B4] | | 0x50FD40 | 0x50FCE0 | |
| · siege of each town | *0x6DF270 | 0x633730 | 0x6335A0 | 0x633530 |
| · · besieging army | | 0x627AD0 | 0x627920 | |
| · records of 0x14 | *0x6DE510 | 0x4D32F0 | 0x4D3250 | 0x4D3240 |
| · office | | 0x4FEFF0 | 0x4FED50 | 0x4FED30 |
| · merchant | | 0x4F4100 | 0x4F4010 | 0x4F3FE0 |
| · · its body | | 0x4F6FA0 | 0x4F6CF0 | |
| · · · first 268 bytes | | 0x4F6240 | 0x4F6090 | |
| · · · 100 bytes on [0x4D8] | | 0x534830 | 0x5347F0 | |
| · object 0x6DE588 | 0x6DE588 | 0x5351D0 | 0x535160 | 0x5350F0 |
| · 15 world objects [+0xB10] | | 0x52BBE0 | 0x52BB30 | (inline) |
| · optional object (list) | | 0x533DA0 | 0x533D10 | 0x533CF0 |
| ships | 0x6DD7A0 | 0x504230 | 0x504010 | 0x503F70 |
| · captain | | 0x4FDEB0 | 0x4FDE30 | 0x4FDE20 |
| · ship | | 0x517990 | 0x5177D0 | 0x5177B0 |
| · · its first part | | 0x516520 | 0x5162D0 | |
| · convoy | | 0x5014E0 | 0x5013D0 | 0x5013C0 |
| · pirate | | 0x5138B0 | 0x5137E0 | 0x5137B0 |
| mailbox | 0x6DD730 | 0x4D6350 | 0x4D62B0 | 0x4D6270 |
| · notice | | 0x51A9B0 | 0x51A8D0 | 0x51A890 |
| automatic trade routes (0xDC each) | 0x6DD728 | 0x4D4BC0 | 0x4D4B60 | 0x4D4B40 |
| event queue | 0x6DD73C | 0x4D81E0 | 0x4D80F0 | 0x4D8080 |
| · node lists of event 0x34 | | 0x4F3A40 | 0x4F39E0 | 0x4F39C0 |
| battles | 0x6E55D0 | 0x611AE0 | 0x611950 | 0x6118F0 |
| names | 0x6DDAA0 | 0x511E30 | 0x511C60 | 0x511B90 |
| whole `.pst` | 0x6DFC90 | 0x54DA60 | 0x54D870 | 0x54D7F0 |
| · statistics series (4 optional) | | | 0x54D080 | 0x54D050 |
| · objects of 0x2310 (list [+0x1C]) | | 0x54CBD0? | 0x54CBD0? | 0x54CB90 |

(The last row: the writer calls 0x54CBD0 for each object; the matching
reader is still to be confirmed.)

### 1.2 What the writers do besides copying memory

- 0x549CC0 changes the settings (`*0x6CC3E8`) **in memory** before writing
  their 0x4C bytes: [+0x1A] = day, [+0x1B] = month + 1, u16 [+0x1C] = year
  of the world; [+0x1E] = hometown of the player (0x6DFC14) unless
  `[0x6DF2F0] & 0xC` (then 0); [+0x27..+0x3A] = 0 and a town name copied in
  (`strcpy` from the table 0x6DDA00, see "Game bugs"); [+0x3B] = (male of the
  player) << 7 | his rank in the hometown (merchant [0x39C + hometown]);
  [+0x3C..+0x4B] = 0. Then it writes `u32` [0x6DFBC4], [0x6DFBC8],
  [0x6DFBCC] ([+0x8D4..+0x8DC] of 0x6DF2F0: ms per tick, a speed value the
  loader clamps to 2 when it is above 4, and one more) and the eight
  sections, each followed by `u32 0xAFFE`.
- 0x52F870 (world): after the town matrix it writes the constant bytes
  `F1 F2 F3 F4` (the u32 the reader skips); [+0x8A] goes as 0x7F or 0; and
  before the offices it walks the free list (from [+0x7E] of the world,
  through [+0x2C8]) setting each owner [+0x2C4] to 0xFFFF **in memory**.
- Nodes of a town ([0x9B4]): written in the order of the list; the reader
  puts each one first, so the order turns round at each save and load.
- 0x52BB30 (the 15 world objects) writes as the number of its records the
  u16 [+0x1C], not the length of its list.
- 0x504010 (ships) starts with u16 0x85; free ship slots carry the byte
  [+0x137] from version 0x83 (that's why the ships section grows).
- 0x4D62B0 (mailbox) writes version 5 (`0xAFFE0005`).
- 0x511C60 (names) writes the 40 town names as `"A"`; before copying each
  name written by the players it puts a 0 in the last byte of its slot of 24
  bytes (**in memory**: 23 characters at most); and the ship names of the
  save are written as u16 0, u16 0 (the loader undoes them: 0x5121C0 at the
  end of the load).
- Pointers into the file: the route of a ship ([+0x24]), the buffer of the
  notices 0x29/0x47/0x63/0x83 ([+0x8]), and [+0x8]/[+0xC] of the events 0x1B
  and 0x34. Only their being null matters to the readers (0x4D835E reads the
  list of an event 0x34 only if its [+0x8] isn't 0). p3vita writes 1.

### 1.3 What the loader does that the oracle and p3vita don't

0x549F40 also clears [0x6DFBF0..+0x14], clamps [+0x8D8], and after the
sections: registers the name of each ship of each merchant and pirate in
the names object (0x5131B0), checks the route of each ship (0x5048C0 →
0x517C10 per ship: may move it or recompute its route) and checks the
trade routes (0x4D60B0). `town_oracle.py` calls the readers one by one and
does none of this, so `tick_probe --carga` doesn't see it either. See "For
the roadmap".

The `.pst` is optional for loading: if it isn't found, 0x5476E0 calls
0x54DDF0 (empty statistics) and goes on.

## 2. The writer in p3vita

- `src/p3data/savegame_write.{hpp,cpp}`: `prepare_save` (0x4D6780 for each
  merchant), `write_savegame` (0x549CC0, the uncompressed `.pat`),
  `savegame_file` (the three, compressed), `empty_statistics` and
  `statistics_file` (the `.pst`, §7), and `load_save_scripts` (the town
  names, called from `load_game_scripts`).
- `src/p3data/lz_compress.{hpp,cpp}`: `lz_compress`, the game's compressor (§3).
- `SaveOpaque` in `savegame.hpp` (`SaveGame::opaque`): what p3vita read and
  threw away before and the writer needs: the ten lists of each merchant,
  the pirates' maps and records, the extras of the notices and of the
  events, the trade routes, the battles (as in the file) and the town names.
  The 12 bytes [0x6DFBC4..0x6DFBCC] and the u16 [0x6DD728], [0x6DD72A] go to
  the globals.
- `p3save <cpr> <save> write <out.pat> [<out.raw>]` (the save is read from
  the archive or, if it isn't there, from the disk; `raw` too) and `p3save
  compress <in> <out>`.
- Checks: `tools/oracle/write_oracle.py` (the game's code: load like
  `town_oracle.py`, the 12 bytes and the battles like 0x549F40, then
  0x4D6780, 0x549C60, 0x549CC0 and 0x54C410), `tools/oracle/compare_written.py`
  (byte for byte by sections; pointers counted apart) and
  `scripts/write-check.sh [SAVES]` (both on each save, the compressor alone on
  the game's uncompressed file, and p3vita's `.pat` decompressed again; 19 s
  for the 12 here). `tests/lz_test.cpp` (ctest, now 8 of 8): 5 synthetic
  inputs against the bytes the game's compressor writes for them (computed
  in the emulator) and round trips.

### 2.1 Check 1: byte for byte against the game, right after loading

`scripts/write-check.sh`, the 12 v0x82 saves of `missav/`, uncompressed:

| Save | Bytes | Different | Pointers (ships, mailbox, events) |
|-|-|-|-|
| 1 | 955 308 | 0 | 69, 0, 73 |
| 2 | 845 446 | 0 | 78, 0, 75 |
| 3 | 1 076 815 | 0 | 80, 10, 49 |
| 4 | 1 042 754 | 0 | 60, 6, 51 |
| 5 | 1 019 569 | 0 | 76, 5, 51 |
| 6 | 1 025 883 | 0 | 80, 0, 51 |
| 7 | 1 009 572 | **12** (battles) | 85, 0, 54 |
| tut0 | 967 514 | 0 | 76, 0, 59 |
| tut1 | 967 974 | 0 | 76, 1, 59 |
| tut2 | 947 295 | 0 | 78, 0, 59 |
| tut3 | 969 629 | 0 | 75, 0, 59 |
| tut4 | 956 157 | 0 | 64, 2, 59 |

The 12 bytes of save 7 are padding the game writes without initializing (see
"Game bugs"): 3 bytes after each of the 4 entries of the battles' table; in
the file they are 0xCD, in the emulator 0. p3vita keeps the battles as they
are in the file (they are the `battles` task's), so it writes 0xCD.

The load (`tick_probe --carga`, 0 days) still has 0 differences in the 12
saves after the changes to `savegame.cpp`.

### 2.2 Fixes to the reader (savegame.cpp)

- Events 0x1B: the file has 4 bytes an entry ([+0x12] of them, 0x4D831B
  copies `n·4` into a buffer of `n·16`); the reader skipped `n·16`. It went
  unnoticed because these extras go at the end of the section and only the
  lists of the events 0x34 (none in the saves) come after them.
- Names: after the three lists the save has its own ship names (u16
  number, u16 bytes and the text), not always a u32 0; when they weren't
  empty (saves 3–7 and the five tutorials) `parse_names` left
  `custom_names` empty.

### 2.3 0x4D6780 and the notices with a record

`prepare_save` calls `fn_4d6780` (town_tick/events_towns.cpp), the local
session's rewrite of 0x4D6780, checked call by call there (scenarios
`fiesta_ciudad` and `cartas`); my own copy (`expire_merchant_mail`) was
removed when merging main. The notices of types 0x3C..0x40 and 0x71 carry a
16-byte record that the simulation keeps as live state
(`SaveGame::mail_records`, region L of the oracle); the writer writes that
record and, after it, its [+0x0] bytes, which only the file has
(`SaveOpaque::mail_extras`). The retouched saves (§6) give merchant 0 a list
of 9 notices of every class (one 0x71 kept by the tick of its record) and
merchant 2 one that loses the free list: the game's 0x4D6780 and
`fn_4d6780` leave the same bytes.

## 3. The compressor

The game's compressor is in the exe (statically linked, not in
`archiver.dll`): 0x54C410 allocates a hash table of 0x40000 bytes and a ring
of 0x17E8 and calls 0x6381E0, which keeps its state in globals
(0x6C7B20..0x6C7CB8). Rewritten in `lz_compress.cpp` with the same choices,
which decide the bytes:

- Below 16 bytes, or when the output reaches the size of the input (checked
  after each u32 written, 0x6387C7 changes the return address to
  0x63826C), a stored block (`n < 0`, 0x638274).
- The u32 size, the first byte as a literal; then at each position 0x6384D4:
  if its two bytes are equal, a literal and the run from the next byte as a
  match at distance 1 (if it is at least 2 bytes); if not, the longest match
  of the hash chain of the two bytes (u16 key, 0x10000 heads), newest first,
  the newest on a tie, and the first that reaches the end of the input wins
  at once; with no chain, a literal. Each position covered is inserted at the
  head of its chain (0x638434); the chains keep the last 510 positions (ring
  of 0x1FE nodes; the oldest leaves its chain when its node is reused). The
  last byte, if no match covered it, is a literal, and the last u32 is
  written even if it is empty.
- Distance − 1 in 8 classes (`c`: 3 bits, then `c + 1` bits from 0, 2, 6,
  14, 30, 62, 126, 254), length − 2 in fields of 2, 3, 4... bits (table
  0x6C7BCC: 2^w − 1).

Result: the game's uncompressed file of each of the 12 saves, compressed by
p3vita, gives the same bytes as the game's (189 700 for save 1; 161 892 to
278 028). The files p3vita writes differ from the game's only through the
pointers (§2.1), so their compressed sizes differ a little.

## 4. Reading version 0x85 and the round trip

Every version comparison of the readers (all the functions of §1.1, with
`cfg.py`): only one is above 0x82, the byte [+0x137] of the free ship slots
from 0x83 (0x5179FF), which `parse_ship` already had; the others (0x55 to
0x82: `cmp ebx,0x82; jb` in 0x4FF1E3, the office) take the same branch for
0x82 and 0x85. The mailbox (version 5) and the `.pst` (0xB) carry their own.
So `parse_savegame` now reads the versions 0x82 to 0x85.

Check 3, `scripts/write-reload.sh` (25 s for the 12 saves): `p3save write`,
the game's readers load the written file in the emulator (`town_oracle.py`,
0 days) and `tick_probe --carga` compares that with what p3vita reads from
the same file: **0 differences in the 12 saves**.

Writing again a file p3vita wrote (v0x85), saves 1, 3, 7 and tut4: the game's
writer and p3vita's give the same bytes (0 different; in save 7 the 12 of the
battles). p3vita's second save differs from its first only in the node
lists of the towns that have more than one node (14 bytes in save 1), and
the third is equal to the first: the order of [0x9B4] turns round at each
save and load, as in the game (§1.2).

## 5. Check 2: after playing

`scripts/write-after-days.sh DAYS [SAVES]`: the whole world with the pieces
of `world.sh` in the oracle (`town_oracle.py ... --write OUT`, which at the
end does what the oracle's load leaves out and runs the game's save code)
and in p3vita (`tick_probe --continuo --carga ... --write OUT`), then
`compare_written.py`. 4 days, saves 1, 3 and 7 (6 min 11 s here):

| Save | Calls different | Bytes | Different | Pointers (ships, mailbox, events) |
|-|-|-|-|-|
| 1 | 0 | 951 142 | 0 | 74, 0, 58 |
| 3 | 0 | 1 073 302 | 0 | 91, 10, 30 |
| 7 | 0 | 1 005 238 | 12 (battles, §2.1) | 85, 0, 40 |

## 6. Retouched states: the branches the saves don't reach

What the 12 saves reach (counted with p3data): ships with their own route
(1 to 78 a save), free ship slots, besieging armies (saves 3, 4, 5, 7),
pirates with records, the slots of 0x6DE588, free offices, towns with more
than one node, notices with extras of the types 0x29, 0x3D, 0x3E, 0x47 and
0x71, events 0x1B. What they don't: the two lists of the siege objects, the
pirates' map objects, the records of the 15 world objects, the lists of the
events 0x34, the ten lists of the merchants (empty in all), the trade
routes, the notices 0x63 and 0x83, and any notice in a merchant's list
(0x4D6780 does nothing in them).

`tools/oracle/write_retouch.cpp` loads a save, adds all of that (see its
header; merchant 0 gets a list of 9 notices of every class of 0x4D6780,
expired and not, a 0x71 kept by its block and a 0x90 dated tomorrow that
cuts the list; merchant 2 a 0x95 that cuts its list and expires) and writes
it without expiring. `scripts/write-retouched.sh [SAVE]`: the game's readers
load it and `tick_probe --carga` compares with p3data's load; then the game
(with 0x4D6780 for each merchant) and p3data save it again:

| Base save | Load | Bytes | Different |
|-|-|-|-|
| 1 | 0 differences | 955 977 | 0 |
| 3 | 0 differences | 1 077 484 | 0 |
| tut4 | 0 differences | 956 826 | 0 |

Checked that it isn't empty: the saved file differs from the retouched one in
the mailbox (the expired notices emptied, the free list lost, see "Game
bugs") and in the nodes of the event 0x34, which come out as the game
leaves them.

Not checked: `[0x6DF2F0] & 0xC` in 0x549CC0 (the hometown written as 0; the
byte isn't in the file), the optional object of [+0xE4] of the world
(0x533D10; p3vita doesn't read it), and ship names added during the game
(0x511C60 writes them if [+0xDA] > [+0xD8] of the names object; p3vita has
no way to add them yet and writes u16 0, u16 0).

## 7. The `.pst`

The game doesn't need it to load a save: if 0x5476E0 doesn't find it, it
empties the statistics (0x54DDF0) and goes on. p3data doesn't keep the
statistics, so it writes the `.pst` the game writes when they are empty
(`empty_statistics`, `statistics_file`; `p3save write` puts it next to the
`.pat`): u32 0xAFFE000B, 6 u16 at 0, the four series absent (a byte 0 and
the u32 0xAFFEAFFE each) and the final 0xAFFEAFFE, 40 bytes, 20 compressed.
Same bytes as the game's writer (0x54D870 after 0x54DDF0) and its compressor
in the emulator, and 0x54DA60 reads it back (counters 0, no series);
`tests/lz_test.cpp` checks both. Writing it instead of nothing keeps an old
`.pst` of the same name from being loaded with a save that isn't its own.

What is missing to keep the real statistics (for the graphs of the game):
the reader 0x54DA60 and the writer 0x54D870 with their parts: the 6 u16
counters [+0x0..+0xA] (days, months, years of data), the four optional
series [+0xC..+0x18] (0x54D080 and its size 0x54D050: blocks of k · 0x204
bytes, k series of 129 u32, docs/formats/savegame.md §10), the list of
objects of 0x2310 bytes from [+0x1C] (linked by [+0x230C]; 0x54CBD0 writes
each, 0x54CB90 is its size, 0x54CA20 builds an empty one), and above all the
daily update that fills them (0x54DE90, which neither the oracle nor p3vita
runs). The `Tut*.pst` of `missav/` are version 8 and the game rejects them.

## 8. After merging main (61cdbfa)

Main brought the fast oracle, the storms on by default and the local
session's work. Conflicts in `savegame.cpp`/`.hpp`: the notices with a
record (§2.3), the first names of `load_game_scripts` (kept as they are;
`load_save_scripts` only reads the town names), and the usage lines of
`tick_probe.cpp` and `town_oracle.py` (`--no-storms` and `--write`). All the
checks again:

| Check | Result | Time |
|-|-|-|
| `write-check.sh`, 12 saves | 0 different in 11, 12 (battles) in save 7; compressor equal in 12 | 30 s |
| `write-reload.sh`, 12 saves | load: 0 differences in 12 | 23 s |
| `write-retouched.sh` 1, 3, tut4 | load 0 differences; 0 different bytes (955 977, 1 077 484, 956 826) | 4 s each |
| `write-after-days.sh 4 1 3 7` | 0 different calls; 0, 0, 12 different bytes (951 322, 1 072 935, 1 005 238) | 59 s |
| `world.sh 7`, `1 2 3 4` | 0 «distintas»; «saltadas» 1, 0, 0, 0 | 136 s |
| `world.sh 7`, `5 6 7 tut0` | 0 «distintas»; «saltadas» 5, 0, 4, 0 | 124 s |
| `world.sh 7`, `tut1 tut2 tut3 tut4` | 0 «distintas»; «saltadas» 0, 2, 5, 7 | 110 s |
| `vita_check.sh` (VitaSDK installed with `scripts/setup-vitasdk.sh`) | VPK built, no errors or warnings | 1 min 17 s |

The «saltadas» are the same as in the README of p3vita-datos. Before the
merge, `SAVES='1 3 5 7' world.sh 7` had also given 0 «distintas» (718 s
with the slow oracle).

## For the roadmap

- Loader: 0x549F40 does more than the readers (§1.3): 0x5131B0 for the
  ship names, 0x5048C0/0x517C10 for the ship routes, 0x4D60B0 for the trade
  routes. p3vita and the oracle don't do it.
- The playable milestone can save with `savegame_file` and `statistics_file`
  (the `.pst` with empty statistics, §7). Keeping the real statistics needs
  0x54DE90 and the `.pst` reader and writer (§7).

## For the inventory

- New: the save path (0x5472F0 and everything under 0x549CC0, 0x549C60,
  0x54C410, 0x54D7F0 and 0x54D870 for an empty `.pst`); 0x4D6780 is the
  local session's `fn_4d6780`.

## Game bugs

Certain:
- **0x549D7D: wrong town name in the settings of the save.** The writer
  copies to [+0x27] of the settings the name from the table 0x6DDA00, which
  goes by town number (0..39), using the index in the save of the player's
  hometown. In 1.pat it writes "Cologne" (number 9) for Lübeck (index 9,
  number 14). Reproduced in `update_settings`. (Presumably the name shown in
  the load menu; not checked.)
- **0x611950: uninitialized padding in the battles.** Each entry of the
  table at the end of the battles is 8 bytes in the file; the reader keeps
  only 5 (0x612A20 gets two u32 and keeps the low byte of the second) and the
  writer writes 8, so 3 bytes come from whatever was in memory (0xCD in
  save 7, 0 in the emulator).

- **0x4D6800 and 0x4D68E0: a notice of type ≥ 0x86 in a merchant's list.**
  0x4D6780 cuts the list at the link that points to it (0xFFFF), and the
  notices after it stay out of every list; if the notice also expires, the
  new head of the free notices is that cut link (0xFFFF), so all the free
  notices are lost. Reproduced in `fn_4d6780` (the local session's), same as
  the game in the retouched states (§6).
- **0x4F3A40/0x4F39E0: the nodes of the event 0x34 change at each load and
  save.** The reader makes the u16 [+0x6] of the third byte of the node
  twice and skips the fourth; the writer writes the sign of the low byte of
  [+0x6] and that byte. A node `b0 b1 b2 b3` is saved as `b0 b1 sign(b2)
  b2`, and the list always ends in 0. Reproduced in `write_event_nodes`,
  same as the game (§6). (Since 2026-11-21 the nodes are kept in memory,
  `SaveGame::hunt_notices`, read and written as the game; the fix writes
  the name big-endian.)

Looks like one:
- The node list of a town ([0x9B4]) turns round at each save and load (the
  writer writes it in order, the reader puts each node first). Harmless if
  the order doesn't matter.

## Changes to shared files

- `src/p3data/savegame.hpp`: new `struct SaveOpaque` before `SaveGame` and
  the member `SaveGame::opaque` at its end.
- `src/p3data/savegame.cpp`: `Reader::since`; `parse_merchant` keeps the
  lists (new argument); `parse_ships` keeps the pirates' maps and records;
  `load_mail` keeps what follows each notice in the file (the buffers, and
  what follows the records of `mail_records`); `load_events` keeps the
  extras and reads 4 bytes per entry of the type 0x1B (§2.2); `parse_names`
  accepts the ship names (§2.2); `parse_savegame` reads the 12 bytes after
  the settings into [0x6DFBC4..0x6DFBCC] (with the clamp of the loader), the
  trade routes (5th section from the end) and the battles (3rd);
  `load_game_scripts` calls `load_save_scripts` (after the first names); versions 0x82 to 0x85
  (`kOldestVersion`, `kNewestVersion` instead of `kSupportedVersion`).
  Nothing else changes in what it reads (load: 0 differences in the 12
  saves).
- `src/p3data/CMakeLists.txt`: `savegame_write.cpp`, `lz_compress.cpp`.
- `tests/CMakeLists.txt`: `lz_test` (ctest goes from 7 to 8 tests).
- `tools/p3save/main.cpp`: `write`, `compress`, and reading the save from the disk.
- `tools/oracle/tick_probe.cpp`: `--write OUT` (at the end of `main`; region
  L and the rest as on main).
- `tools/oracle/town_oracle.py`: `--write OUT` (in `main` and the
  docstring; `OPERATIONS` not touched).
- `tools/oracle/CMakeLists.txt`: `write_retouch`.

## Time of the checks

- `write_oracle.py`: 1.5 s a save; `scripts/write-check.sh` for the 12: 30 s.
- `tick_probe --carga` with a recording of 0 days: 2 s a save;
  `scripts/write-reload.sh` for the 12: 23 s; `scripts/write-retouched.sh`:
  4 s a save; `scripts/write-after-days.sh 4 1 3 7`: 59 s with the fast
  oracle (6 min 11 s before).
- `world.sh 7`, the 12 saves in three batches: 370 s.
- Built for the Vita: `scripts/setup-vitasdk.sh` (11 s) and
  `re/emu/sesion5/vita_check.sh` (1 min 17 s), no errors or warnings.
- ctest has 8 tests now (`lz_test`): `build.sh` and the README count 7.
