# Cloud task: the daily statistics, the real .pst and the loader's last steps

Branch `cloud/statistics`, from main f9d86eb. Task:
`p3vita-datos/tasks/statistics.md`. Written as the work goes.

## Summary

(Filled at the end.)

## Decisions for the user

(Filled at the end.)

## Status

| Part | State |
|-|-|
| 1. The daily statistics (0x54DE90) | done: rewritten, checked in the 12 saves, on by default |
| 2. The real `.pst` | done: same bytes as the game's, read back the same |
| 3. The loader's last three steps | done: load 0 differences in the 12 saves and in retouched ones |

## 1. The daily statistics (0x54DE90)

`advance_time` (0x530E80) calls 0x54DE90 on the object 0x6DFC90 at the start
of each day, after the date (0x5310D0) and the daily work (0x5315C0) and
before the scheduled tasks (0x535450). Rewritten in `src/p3data/statistics.cpp`
(`daily_statistics`), with 0x54E170 (`add_merchant_statistics`) and 0x54DDF0
(`clear_statistics`).

### What it records and where

The object (its header in the globals, `kStatistics`):

| Field | |
|-|-|
| u16 [+0x0] | days recorded, & 0xFFF |
| u16 [+0x2], [+0x4] | weeks and months recorded, up to 24 |
| u16 [+0x8] | years recorded |
| u16 [+0xA] | number of merchant objects (only the `.pst` size function sets it) |
| [+0xC], [+0x10], [+0x14], [+0x18] | lists of records of the towns: days (32 kept), weeks (25), months (25), years (all), newest first |
| [+0x1C] | list of objects of 0x2310 bytes, one per merchant |
| u8 [+0x20..+0x23] | 1, and "there was a new week / month / year" (OR) |

A record of the towns (0x54D1D0) is a block of 0x204 bytes for each town:
what is consumed (citizens' [0x310] + businesses' [0x64]) and produced
([0xC4]) plus the offices of the town, the stock [0x4], [0x370], [0x430],
the situation [0x2C8], the citizens by class [0x2D8], 6 u16 of
[0x300..0x30D] and [0x996]. Weeks, months and years are averages
(0x54D390: sums, then integer division by the number of records; the
situation is an OR masked with 0xFBFFFFF9; [0x996] is only summed).

A merchant's object (0x54CA20) has three rings of records of 0x70 bytes,
linked backwards by a pointer in each one: 32 days, 24 weeks and 24 months,
and a list of years without end. The record of the day (0x54C520): floats
of [0x46C], the money, [0x498], [0x49C], [0x4A0], 15 counters of
[0x484..0x4CC], his offices (their number, their production and its worth
by the table 0x673A18, staff, administrators, the world records of each),
[0x470], [0x474], [0x478] and his reputation (0x4F9580 in his hometown,
0x4F73D0). Weeks (when the day number is 0 modulo 7), months and years are
averages of 7, the days of the month before, and 12 months (0x54C940).

### What is state and not only drawing

The graphs only read these lists (0x49xxxx–0x4Axxxx), but recording them
changes the game: **0x54D1D0 takes and clears every day [0x996] and the 24
s32 of [0x370] of each town**, and **0x54C520 takes and clears 15 counters
of each merchant** ([0x484], [0x48C], [0x494], [0x49C], [0x4A0], [0x4A4],
[0x4AC], [0x4B0], [0x4B4], [0x4B8], [0x4BC], [0x4C0], [0x4C4], [0x4C8],
[0x4CC]; [0x49C] and [0x4A0] are the sales and purchases 0x4F8690 and
0x4F8840 count). Without 0x54DE90 these sums grow for ever in p3vita and in
the oracle; in the game they are the day's. Everything recorded is state
too, since it goes to the `.pst` (part 2).

### In the oracle and in p3vita

- `town_oracle.py --statistics` calls 0x54DE90 after 0x5315C0 (a trampoline)
  and then always records it; regions S0..S3 (the four lists of the towns:
  u32 number and the blocks of each record) and S4 (the merchants' objects
  with the links of their rings as offsets in the object, then their years),
  through the fast readers of `regions()`; `P3_ORACLE_CHECK=1` passes.
- `tick_probe`: the probes of 0x54DE90 and 0x54E170
  (`tools/oracle/probes_statistics.cpp`), the regions S0..S4 in
  `build_save`/`dump_save`, the header of 0x6DFC90 among the globals it
  compares, and the countdown [+0x8B] of the world no longer decreased before
  0x54DE90 (it goes between 0x5315C0 and 0x535450).
- `advance_game_tick` calls `daily_statistics` after 0x5315C0 if
  `SaveGame::record_statistics`, now true by default; `town_oracle.py`
  runs them by default too (`--no-statistics` to leave them out; the class
  `TownOracle` keeps `statistics=False`, so `retouch.py` and
  `retouch_storms.py` don't change), and `world.sh` as it is records them
  (11 functions).
- `tools/oracle/retouch_date.py`: moves the date of a save (here to 30
  December 1301, so that within 3 days come a new week of the towns, of the
  merchants, a month and a year).

### Checks so far

| Check | Result |
|-|-|
| `scripts/tick-probe.sh 54de90 3 1 3` (`P3_ORACLE_OPTS=--statistics`) | 3 + 3 calls equal |
| the same, 4 days, saves 1 and 3 moved to 30 December 1301 | 4 + 4 calls equal |
| `scripts/statistics-world.sh 4 1 3 s1dec s3dec` (the whole world with the statistics, continuous) | 0 different in the 4 (120 s) |
| `scripts/statistics-world.sh 7`, the 12 saves in three batches | 0 different in the 12 (182 + 173 + 154 s; without the statistics 370 s) |
| `world.sh 7` as it is, `SAVES='1 3 7 tut4'`, with the statistics on by default | 0 «distintas», 11 functions (183 s) |

## 2. The real `.pst`

`write_statistics` (0x54D7F0 and 0x54D870) and `read_statistics` (0x54DA60)
in `statistics.cpp`; `statistics_file(SaveGame&)` compresses it,
`p3save write` puts it next to the `.pat`, and `load_savegame` (and `p3save`
from the disk) reads the `.pst` next to a `.pat` as 0x5476E0 does (none, or
of another version like the `Tut*.pst` of `missav/`: empty statistics).

The file (version 0xB): u32 0xAFFE000B; the 6 u16 of the header ([+0xA] set
by the size function to the number of merchant objects); for each of the
four lists of the towns, a byte 1 before each record (u32 number and its
blocks of 0x204) and a byte 0 at the end, then u32 0xAFFEAFFE; each
merchant's object (u16 merchant, u16, u8, u8, u16 years — the size function
recounts them —, its 32 + 24 + 24 records of the rings and its years, 0x6C
bytes each: the link isn't saved); u32 0xAFFEAFFE.

Two things of the game reproduced:
- **0x54D7F0 counts one byte too many when there are months** (0x54D822,
  `lea esi, [esi + eax + 1]` and then `inc esi`), and 0x5472F0 compresses
  that size: the `.pst` ends in one byte more, whatever the buffer had (0 in
  the emulator; p3vita writes 0). The reader leaves it.
- **0x54DA60 builds each merchant's object with 0x54CA20 for the merchant
  0** before reading it over: the record of the day of the merchant 0 clears
  his 15 counters at each object read.

Checks (`scripts/write-after-days.sh 4`, which now compares the `.pst` of the
game and of p3vita byte for byte, and then makes the game's readers load
p3vita's `.pat` and `.pst` and compares with p3vita's load: `town_oracle.py
--pst` and `tick_probe --carga --pst`):

| Save | `.pst` | Reload of `.pat` + `.pst` |
|-|-|-|
| 1 | same bytes (174 409) | 0 differences |
| 3 | same bytes (174 409) | 0 differences |
| 7 | same bytes (174 409) | 0 differences |
| 1 moved to 30 Dec 1301 (weeks, months, a year) | same bytes (200 592) | 0 differences |
| 3 moved to 30 Dec 1301 | same bytes (200 592) | 0 differences |

The `.pat` of the same runs: 0 different bytes (save 7: the 12 bytes of
padding of the battles, `docs/cloud/savegames.md`).

## 3. The loader's last three steps

What 0x549F40 does after reading the sections (`docs/cloud/savegames.md`
§1.3), now in p3vita and in the oracle:

1. **The ship names** (0x5131B0 on the names object, for each ship of each
   merchant from the last one, and of each pirate): `finish_load` in
   `src/p3data/savegame_load.cpp`, called at the end of `load_game_scripts`
   (it needs the game's ship names). With a version (every save p3vita
   reads), a ship whose index [+0x15E] is one of the game's 300 names takes
   that name, in the game's language (in 1.pat ship 0 "Lupus", index 147,
   becomes "Dawn Greeting"); the tutorial ships of the original game
   (indexes 0x12C..0x12F with "Adler der See", "Altes Fass", "Willeharm",
   "Morgengruß", "Sturmwind", "Windgurke", "Nepumuk", "Klabauter",
   "Seepott" or "Susanna") the index of that name; any other 0x7FFF − type
   and "snaikka", "crayer", "cog" or "hulk". The search that follows (and
   0x512D40, which adds a ship name of the save) is only reached by a version
   0; rewritten anyway, with the save's ship names kept by `parse_names`
   (`SaveOpaque::save_ship_names`). In the 12 saves it renames 112 to 152
   ships of each and changes some [+0x15E] in 3 and tut4.
2. **The routes of the ships** (0x5048C0: 0x517C10 for each ship, from the
   last one): `Sailing::check_loaded_routes` (`src/p3data/sailing_load.cpp`),
   at the end of `Sailing::assign_loaded_routes`, since it needs the routes.
   A ship whose next point [+0x34] doesn't exist or is behind it (by its
   movement per tick) gets a new route (0x5172A0) to its destination or to
   the last point of its route; if it is out of the map (2400 × 1800) it
   first goes to (100, 100). Nothing changes in the 12 saves.
3. **The trade routes** (0x4D60B0): those neither in the free list nor
   reached from a ship's [+0x132] go to the free list. In `finish_load`.
   The saves have none.

`town_oracle.py` does the three in its load (`loader_last_steps`, between
reading the save's names and the second 0x5121C0, as the game; the route
check only with the navigation), always: `TownOracle(last_steps=True)`.

| Check | Result |
|-|-|
| `tick_probe --carga` (0 days), the 12 saves | 0 differences |
| `write-retouched.sh` 1, 3, tut4 (2 trade routes) | load 0 differences, 0 different bytes |
| `write_retouch --routes` on 1 and 3 (a ship with a point [+0x34] it hasn't, one with its own route out of the map), 1 day of the whole world, continuous | 0 different; in the oracle's load ship 1 gets a new route and ship 16 goes to (100, 100) (1), ship 88 a new route (3) |

## Changes to shared files

- `src/p3data/simulation.cpp` (`advance_game_tick`): after `fn_5315c0`,
  `if (save.record_statistics) daily_statistics(save);` and the include.
- `tools/oracle/town_oracle.py`: the statistics on by default, `--no-statistics`
  (constants, the trampoline, the call in `advance_tick`, the regions S0..S4
  at the end of `regions()`, 0x54DE90 added to the recorded functions);
  `OPERATIONS` not touched.
- `tools/oracle/tick_probe.cpp`: the regions S0..S4, the header among the
  compared globals, `register_probes_statistics`, the countdown rule,
  `--write` also writes OUT.pst, `--pst FILE` with `--carga`.
- `tools/oracle/write_oracle.py`: `game_statistics`, `read_game_statistics`;
  `write_files` also writes OUT.pst. `town_oracle.py`: `--pst FILE`.
- `src/p3data/savegame.cpp` (`load_savegame`): reads the `.pst` next to the
  `.pat`. `savegame_write.{hpp,cpp}`: `statistics_file(SaveGame&)`,
  `load_statistics_file`. `tools/p3save/main.cpp`: writes and reads the real
  `.pst`. `scripts/write-after-days.sh`: the `.pst` checks.
- `tools/oracle/probe_registry.hpp`, `tools/oracle/CMakeLists.txt`,
  `src/p3data/CMakeLists.txt`: the new files.
- `src/p3data/savegame.hpp`: `Statistics`, `SaveGame::statistics`,
  `SaveGame::record_statistics`, `SaveOpaque::save_ship_names`.
- `src/p3data/savegame.cpp`: `parse_names` keeps the save's ship names;
  `load_game_scripts` calls `finish_load`.
- `src/p3data/sailing.hpp`, `sailing.cpp`: `check_loaded_routes` and
  `fn_517c10` (in the new `sailing_load.cpp`), called at the end of
  `assign_loaded_routes`.
- `tools/oracle/town_oracle.py`: `loader_last_steps` in the load
  (`last_steps=True`).
- `tools/oracle/write_retouch.cpp`: `--routes`.
