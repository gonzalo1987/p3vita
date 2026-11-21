# The missions

What the game calls missions are small programs that run during a game: the historical events (the plague of 1347, the
fire of Lübeck...), the random tasks that come and go (a drunken crew, a wedding, a smuggler, a treasure map...), the
network game's contests and the campaigns' scripts. Read in the executable and in the game's data on 2026-11-19
(`re/emu/sesion45/`); p3vita: `src/p3data/missions.{hpp,cpp}` (the list and the start) and
`src/p3data/mission_script.{hpp,cpp}` (the interpreter).

## The data

- `scripts/missions_eng.ini` (`missions_spa.ini` in the Spanish archive: from `[Missions]` on the same as the English
  one; the Spanish executable reads its own) has the campaigns' titles and texts for the start menu
  (`[Campaign_0..7]`), the missions' files by number (`[Missions]`: `0=MannschaftBesoffen.p2m`...), the groups
  (`[Group<name>]`, `Include=` numbers apart by spaces; a number twice starts two scripts of it) and the groups of
  each game mode (`[EinzelSpiel]`, `[Multiplay]`, `[Hotseat]`, `[Kampagne0..7]`: `IncludeGroup=`).
  - A game of one player (`EinzelSpiel`): `History` (48 dated events, 1305..1669) and `Standard` (18 missions, some
    two or three times: 0 1 2 3 4 5 5 6 7 8 8 9 9 9 10 11 12 13 13 15 15 16 16 22 23).
  - Network and hot seat: also `Standard2` (14, the ship's kobold) and `Multiplay` (17..21, the contests).
  - The campaigns: 0 the tutorial (no group: its own system, `scripts/tutorial.ini` and `missav/Tut*.pst`), 1 (100..103),
    2 (200, 201), 3 (300..309), 4..7 the scenarios (400, 500, 600, 700).
- `missions_addon/<file>.p2m` (94 files; in the Spanish archive the same programs with the Spanish texts):
  - u16 the sections' count, u16 the variables' count (u32 each, a script's own), u32 the offset of each section;
  - each section is one instruction: its opcode byte, then its operands (mostly variables' numbers, a byte each, and
    constants);
  - the texts after the last section (the letters' subjects and bodies, Windows-1252, each with its 0): the
    instructions point to them by their offset in the file.
  - `re/emu/sesion45/p2m.py FILE` lists a file's sections; `--hist` the opcodes over every mission.
- The game's data have two slips: `[Missions] 308=Kampagne3_Entfuehrung.p2m` but the file is
  `Kampagne3_Entführung.p2m` (the campaign 3's abduction never loads: its scripts get no program, 0x511270 sets its
  count to 0; fixed behind fix_game_bugs: `MissionData::respelt`); `p2arch1_eng.cpr` (an update, not mounted by the viewer) has a newer `WettbewerbRennen.p2m` (18, a
  network contest).

## The list (*0x6DD8B4)

An object of 12 bytes (0x511000; vtable 0x673608): u32 [+0x4] its entries, u16 [+0x8] their count. An entry: u32
[+0x0] the program (the whole file, 0 until loaded), u16 [+0x4] the mission's number, u16 [+0x6] the scripts that use
it (at a start, how many to start).

- 0x5110E0 (name, start): the group's `Include=` (0x4BE060 reads the ini; TrimLeft), its numbers by
  `Find(' ')` and `atoi` from each position (two spaces make an empty token whose `atoi` reads the next number); a
  number not in the list goes in with the count `start ? 1 : 0`; one in it gets one more with `start`.
- 0x511270 (start): empties the list; the mode's section by the settings' [+0xD] (0 Hotseat, 1 and 2 Multiplay, 3
  Einzelspiel, 4 and 5 Kampagne<[+0x14]>, others none), its `IncludeGroup=` split the same way, 0x5110E0 for each.
  With `start`: each entry's program (0x5116F0), then for each entry `count` scripts: the variables (zeroed), the first
  sections that are a 0x0A (u8 variable, u32 value) set them; at the first other one: 0xFD waits its variable's ticks
  from now, 0x00 its variable's tick (one that has passed, with a day's grace, drops the script: 0x4F37E0 on the
  program, the variables freed), 0xFE and 0xFF nothing (the variables leak). A waiting one is an event 0x1B
  (0x4D8CF0 at its tick): [+0x8] the program, [+0xC] the variables, u16 [+0x10] the mission, [+0x12] the variables'
  count, [+0x14] the section after the wait. The drops count the entry down while the loop counts up to it: of three
  scripts of a passed tick only two go (none of the game's data has one; fixed behind fix_game_bugs).
- 0x5116F0 (number, index): `[Missions] <number>=` → `missions_addon/` + the file, read whole (0x402760 opens it with
  0x8020 through Archiver.dll) into the entry; fails without the name, the file or with an empty one.
- 0x511690 (number): a script takes its mission's program: the entry by its number (none: 0), loaded if it has none,
  one more on its count.
- 0x4F37E0 (program): one less on the count of its entry (found by the program); at 0 the program is freed.

## The start and the load

- The operation 0xC0 (0x543810; queued by 0xA7 in a game of one player, and by the network game's start): 0x532420 on
  the world with the settings' [+0xE] + 1 competitors; 0x635170 on each town's siege object (from the last); the old
  list freed (0x511080: its programs too) and a new one with 0x511270(1). Scripts already in the queue would point into
  the freed programs; a new game has none (`retouch_unchecked.py mission_start` puts the saves' out of the way).
  - 0x532420: every merchant's reputation factor [+0x464] to 1 (the bit 0x8000 of [+0x8], if set, becomes 0x1);
    for each human merchant (from the last) with a merchant of the kind 1 at home in his town, a competitor: **game
    bug**, the factor 2 and the bit 8 go to the merchant 0 (0x532500 writes through the array's base); then the
    merchants of the kind 1 with the factor 1, in order, take the rest (bit 8, factor 2). The 12 saves show it (their
    merchant 0 has the bit 8; `re/emu/sesion45/factors.py`; fixed behind fix_game_bugs: the merchant found). The
    humans' factors too, which 0xA7 has just set by the difficulty (**game bug**: lost, nothing sets them again; fixed
    behind fix_game_bugs: kept). Then a walk of the events to the first of kind 3 that keeps
    nothing; the Alderman's town [0x6DE52E]: the Hanse town (flags 0x40) without a human at home whose number is
    nearest to 14 (0 if none); [0x6DE52A] 0; [0x6DE4AC] the tick.
  - 0x635170: the town's countdown to its next siege: a human's home town 0x3FFF with [+0x101B] 0, the others
    (residents · now) % 0xA001 + 0x4800 with 0xFF; then the month's lists (0x635220).
- A load (0x4D81E0): a new list with 0x511270(0) (no counts, no programs), then each event 0x1B by its index: its
  program by 0x511690 ([+0x10]) and its variables from the file (u32 [+0x12] of them; the buffer has room for 16 bytes
  each). The writer (0x4D80F0) writes the event's pointers as they are and the variables after the records.
- p3vita: `MissionData` in `SaveGame::missions` (the ini, the files, the list, each event's script: its program shared
  with the list's entry, its variables); `load_mission_files` and `attach_mission_programs` in `load_game_scripts`;
  `missions_detail::fn_511270` and the others; the event's [+0x8] and [+0xC] are 1 or 0.

## The interpreter (0x4ECF64)

The event 0x1B's handler (on the event queue, the event's pointer as its argument). Without a program it frees the
variables and ends (0). Else, from the section [+0x14]: each section's opcode 0x01..0xFB through the byte table
0x4F3054 into the jump table 0x4F2E34 (138 with a handler of their own, below; the others, 0x4F2D44, end the script);
after a handler [+0x14] goes to the next section (0x4F2C93) unless it jumped (0x4F2C97); the loop stops at 0x00 or
0xFD..0xFF:

- 0xFD v, u16 s: the event at now + v (its variable's value), [+0x14] = s; 1.
- 0x00 v: v − 0x100 below now (a tick that has passed): the end; else the event at v and the next section; 1.
- 0xFE v: the event at now + v · 256 (days), [+0x14] = 0; 1.
- 0xFF: the end (0x4F37E0 on the program, the variables freed; 0).

The handlers' table: the operand bytes as the game's files have them (each section's length) and how many missions
use each one (the free game's History and Standard, the network game's Standard2 and Multiplay, the campaigns). The
free game uses 80 (and the four waits), the network game 10 more, the campaigns 28 more; 20 no file uses (0x27 among
them, 0x26's handler).

| op | handler | bytes | free game | network | campaigns |
|-|-|-|-|-|-|
| 0x01 | 0x4EEA22 | 3 |  |  | 4 |
| 0x02 | 0x4EEA6E | - |  |  |  |
| 0x03 | 0x4EEAAB | 4 |  |  | 1 |
| 0x04 | 0x4EEB62 | 2 | 4 | 3 | 12 |
| 0x05 | 0x4EEC39 | 2 | 15 | 6 | 16 |
| 0x06 | 0x4EECE2 | 3 | 15 | 6 | 9 |
| 0x07 | 0x4EED2D | 3 | 1 | 2 | 10 |
| 0x08 | 0x4EED7B | 2 |  | 1 |  |
| 0x09 | 0x4EEDFD | 3 | 18 | 6 | 7 |
| 0x0A | 0x4EEEAA | 6 | 66 | 6 | 19 |
| 0x0B | 0x4EEED1 | 4 | 18 | 6 | 15 |
| 0x0C | 0x4EEEF3 | 4 | 17 | 6 | 10 |
| 0x0D | 0x4EEF15 | 4 | 14 | 5 | 8 |
| 0x0E | 0x4EEF36 | 4 | 12 | 6 | 6 |
| 0x0F | 0x4EEF59 | - |  |  |  |
| 0x10 | 0x4EEF8A | - |  |  |  |
| 0x11 | 0x4EF112 | 3 | 7 |  | 1 |
| 0x12 | 0x4EF161 | 3 | 3 |  |  |
| 0x13 | 0x4EF1AF | - |  |  |  |
| 0x14 | 0x4EF1FD | - |  |  |  |
| 0x15 | 0x4EF260 | 3 | 8 | 4 | 3 |
| 0x16 | 0x4EF2B6 | - |  |  |  |
| 0x17 | 0x4EF2DF | 7 | 17 | 6 | 8 |
| 0x18 | 0x4EF30B | 7 | 4 | 2 | 3 |
| 0x19 | 0x4EF337 | 7 | 9 | 5 | 5 |
| 0x1A | 0x4EF35F | 7 | 9 | 5 | 3 |
| 0x1B | 0x4EF387 | 7 | 10 | 2 | 15 |
| 0x1C | 0x4EF3B3 | 7 | 14 | 6 | 3 |
| 0x1D | 0x4EF3DF | 3, 1818 | 13 | 4 | 7 |
| 0x1E | 0x4EF3F0 | 3 | 4 | 2 | 1 |
| 0x1F | 0x4EF788 | 4 | 2 | 1 | 1 |
| 0x20 | 0x4EF485 | 2 | 1 |  |  |
| 0x21 | 0x4EF4B6 | 2 | 10 | 4 |  |
| 0x22 | 0x4EF518 | 2 | 2 | 1 |  |
| 0x23 | 0x4EF588 | 3 | 5 | 1 |  |
| 0x24 | 0x4EF65F | 2 | 8 | 1 | 1 |
| 0x25 | 0x4EF70C | - |  |  |  |
| 0x26 | 0x4EF875 | 4 | 1 |  |  |
| 0x27 | 0x4EF875 | - |  |  |  |
| 0x28 | 0x4EF9DF | 3 | 5 | 3 | 2 |
| 0x29 | 0x4EFBAA | 6 | 1 |  |  |
| 0x2A | 0x4EFDDF | 4 | 3 |  | 1 |
| 0x2B | 0x4F028D | 5 |  |  | 1 |
| 0x2C | 0x4F0692 | 4 | 2 |  | 1 |
| 0x2D | 0x4F06C1 | 3 |  |  | 1 |
| 0x2E | 0x4F0711 | 4 |  | 1 | 2 |
| 0x2F | 0x4F0780 | 4 | 1 |  |  |
| 0x31 | 0x4F0816 | 3 |  |  | 1 |
| 0x32 | 0x4F086A | - |  |  |  |
| 0x33 | 0x4F098E | 3 |  |  | 1 |
| 0x34 | 0x4F0A27 | 3 |  |  | 10 |
| 0x35 | 0x4F0A54 | 2 | 1 |  |  |
| 0x36 | 0x4F0A6F | 4 |  | 1 | 1 |
| 0x37 | 0x4F0E94 | 9 | 48 |  |  |
| 0x38 | 0x4F0EFA | 4 | 2 | 1 | 3 |
| 0x39 | 0x4F0F6A | 2 | 8 | 6 |  |
| 0x3A | 0x4F0FCF | 3 | 6 | 2 | 2 |
| 0x3B | 0x4F1001 | 3 | 2 | 1 | 1 |
| 0x3C | 0x4F1050 | - |  |  |  |
| 0x3E | 0x4F11A5 | 4 |  | 1 |  |
| 0x3F | 0x4F1253 | 3 | 9 | 3 | 4 |
| 0x40 | 0x4F12BB | 6 | 1 |  |  |
| 0x41 | 0x4F1409 | 4 | 1 |  |  |
| 0x42 | 0x4F1514 | 3 | 3 |  | 1 |
| 0x43 | 0x4F1592 | 2 | 1 | 2 | 4 |
| 0x44 | 0x4F15AC | 3 | 6 |  |  |
| 0x45 | 0x4F167C | 3 | 1 |  |  |
| 0x46 | 0x4F16B3 | 3 | 2 | 1 |  |
| 0x47 | 0x4F17C0 | 5 | 2 |  | 2 |
| 0x48 | 0x4F188D | 5 | 2 |  |  |
| 0x49 | 0x4F192D | 4 | 1 |  |  |
| 0x4A | 0x4F1987 | - |  |  |  |
| 0x4B | 0x4F1A0F | 5 | 1 |  |  |
| 0x4C | 0x4EE30B | 5 | 1 |  | 2 |
| 0x4D | 0x4F1A96 | 4 | 2 |  |  |
| 0x4E | 0x4F1B13 | 3 | 1 |  |  |
| 0x4F | 0x4F1B74 | 4 | 2 |  | 1 |
| 0x50 | 0x4F1C74 | 6 | 1 |  |  |
| 0x51 | 0x4F1DCA | 2 | 3 |  | 1 |
| 0x52 | 0x4F1E38 | - |  |  |  |
| 0x53 | 0x4EFB40 | 4 | 2 |  |  |
| 0x54 | 0x4EFA54 | 4 | 1 |  |  |
| 0x55 | 0x4EFE91 | - |  |  |  |
| 0x56 | 0x4F1F14 | 2 |  | 2 | 6 |
| 0x57 | 0x4F01AA | 4 |  | 1 |  |
| 0x58 | 0x4F1F2C | 3 |  | 3 |  |
| 0x59 | 0x4F1F47 | 3 |  | 1 |  |
| 0x5A | 0x4F00AC | 5 |  | 1 |  |
| 0x5B | 0x4F1F62 | 3 |  |  | 1 |
| 0x5C | 0x4F1FB3 | 5 |  |  | 1 |
| 0x5D | 0x4F20E6 | 4 |  |  | 1 |
| 0x5E | 0x4F212B | 5 |  |  | 2 |
| 0x5F | 0x4F2216 | 7 |  |  | 1 |
| 0x60 | 0x4F24E3 | 3 |  |  | 1 |
| 0x61 | 0x4F2512 | 3 | 2 |  |  |
| 0x62 | 0x4F2565 | 4 |  |  | 2 |
| 0x63 | 0x4F2363 | 5 |  |  | 1 |
| 0x64 | 0x4F25E8 | 2 |  |  | 1 |
| 0x65 | 0x4F26C0 | 3 |  |  | 11 |
| 0x66 | 0x4F272E | 3 |  |  | 1 |
| 0x67 | 0x4F27BE | 6 |  |  | 1 |
| 0x68 | 0x4F286E | - |  |  |  |
| 0x69 | 0x4F2A89 | 4 | 1 |  |  |
| 0x6A | 0x4F2B3A | 4 | 2 |  |  |
| 0x6B | 0x4ED332 | 3 | 1 |  |  |
| 0x6C | 0x4F2BDA | 3 | 1 |  |  |
| 0x6D | 0x4EFF63 | 5 | 1 |  |  |
| 0x6E | 0x4F03AC | 6 | 3 |  |  |
| 0x6F | 0x4EFCB9 | 7 | 2 |  |  |
| 0x70 | 0x4F0C74 | 4 |  |  | 1 |
| 0x71 | 0x4ED018 | - |  |  |  |
| 0x72 | 0x4EF062 | 3 |  |  | 1 |
| 0x73 | 0x4EEC65 | - |  |  |  |
| 0x74 | 0x4F051A | - |  |  |  |
| 0xE4 | 0x4ED00A | - |  |  |  |
| 0xE5 | 0x4EE698 | 3 |  |  | 1 |
| 0xE6 | 0x4F2C3E | 3 |  |  | 2 |
| 0xE7 | 0x4ED125 | 5 |  |  | 1 |
| 0xE8 | 0x4ED086 | 2 | 1 |  |  |
| 0xE9 | 0x4ED125 | 5 | 1 |  | 1 |
| 0xEA | 0x4EE9DB | - |  |  |  |
| 0xEB | 0x4EE984 | 3 |  | 1 | 1 |
| 0xEC | 0x4EE7D0 | 2 |  |  | 4 |
| 0xED | 0x4EE567 | 2 |  |  | 2 |
| 0xEE | 0x4EE44A | 6 | 2 |  | 4 |
| 0xEF | 0x4EE25C | 2 |  |  | 7 |
| 0xF0 | 0x4EE238 | 3 | 1 | 1 | 1 |
| 0xF1 | 0x4EE1CD | 4 | 3 |  |  |
| 0xF2 | 0x4EE1A2 | 3 | 2 |  | 1 |
| 0xF3 | 0x4EE155 | 3 | 1 | 1 | 1 |
| 0xF4 | 0x4EE08A | 4 | 1 | 1 |  |
| 0xF5 | 0x4EE012 | - |  |  |  |
| 0xF6 | 0x4EDF3F | 6 | 2 | 1 | 1 |
| 0xF7 | 0x4ED4A0 | 12 | 16 | 6 | 14 |
| 0xF8 | 0x4ED20D | 3 | 13 | 4 | 5 |
| 0xF9 | 0x4ED125 | 5 | 3 |  |  |
| 0xFA | 0x4ED3A6 | 2 | 1 |  |  |
| 0xFB | 0x4ED2A7 | 3 |  |  | 1 |

(`re/emu/sesion45/optable.py` makes it.) The 20 without a file: 0x02 0x0F 0x10 0x13 0x14 0x16 0x25 0x27 (0x26's
handler) 0x32 0x3C 0x4A 0x52 0x55 0x68 0x71 0x73 0x74 0xE4 0xEA 0xF5. What each one does: the comments of
`mission_script.cpp` (`step()`), every one rewritten (2026-11-20) and checked with the oracle (`docs/oracle.md`).

What hangs from them in the world: the operations they queue (0x28, 0x7C, 0x7D, 0x82, 0x8E: `run_operation`), the
letters they compose (0xF7, 0xF6, 0x68) and the event 0x09 that 0xFB makes (0x4E2824, `events_towns.cpp`: a good or bad
harvest in a town, the town's [+0x2C8] 0x4000 or 0x2000 for [+0xC] ticks or so, the letters 0x08 to the town's
producers of grain, honey, wine and hemp and 0x09 to the chronicle at its start and its end; with [+0x12] it comes
back the next year, turned over when negative).

## Checks

- The oracle runs the missions by default since 2026-11-21 (`town_oracle.py` with `--cpr`, and so `world.sh`;
  `--sin-misiones` without them; the class `TownOracle` keeps `missions=False` unless asked: the scenario tools choose,
  `retouch_unchecked.py`'s by each scenario's `missions`). `world.sh` 7 days with them: 0 differences in the 12 saves.
- The loads: the oracle with the missions (TownOracle `missions`) runs 0x511270 and 0x511690 as the game; the regions
  MS (the list), MV (each event 0x1B's script) and MF compared by `tick_probe --carga`: 0 differences in the 12 saves
  (each has 49 to 75 scripts: about 15 of the Standard missions and the History's to come). Without it (as all the
  older checks), the oracle skips 0x511270 and tick_probe gives p3vita's load the same (`detach_mission_programs`).
- The start: `retouch_unchecked.py mission_start` (0xC0 in each mode, with competitors and the bit 0x8000): equal in
  saves 1, 3, 5 and 7, 18 targets; the merchant 0 of 0x532420 broken on purpose: caught. The new game to its end
  (`re/emu/sesion24/ngc.sh player`, the oracle with 0xC0): 0 differences.
- The saves written: `write_oracle.py` loads with the missions (the programs' pointers in the file, p3vita's 1);
  `compare_written.py` leaves out the battles' table's unset bytes (the game's 0x611950).
