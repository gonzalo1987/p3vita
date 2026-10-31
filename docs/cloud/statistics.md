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
| 1. The daily statistics (0x54DE90) | rewritten and checked; on by default pending the 12 saves |
| 2. The real `.pst` | pending |
| 3. The loader's last three steps | pending |

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
  `SaveGame::record_statistics` (false until the 12 saves are checked).
- `tools/oracle/retouch_date.py`: moves the date of a save (here to 30
  December 1301, so that within 3 days come a new week of the towns, of the
  merchants, a month and a year).

### Checks so far

| Check | Result |
|-|-|
| `scripts/tick-probe.sh 54de90 3 1 3` (`P3_ORACLE_OPTS=--statistics`) | 3 + 3 calls equal |
| the same, 4 days, saves 1 and 3 moved to 30 December 1301 | 4 + 4 calls equal |
| `scripts/statistics-world.sh 4 1 3 s1dec s3dec` (the whole world with the statistics, continuous) | 0 different in the 4 (120 s) |

## Changes to shared files

- `src/p3data/simulation.cpp` (`advance_game_tick`): after `fn_5315c0`,
  `if (save.record_statistics) daily_statistics(save);` and the include.
- `tools/oracle/town_oracle.py`: `--statistics` (constants, the trampoline,
  the call in `advance_tick`, the regions S0..S4 at the end of `regions()`,
  0x54DE90 added to the recorded functions); `OPERATIONS` not touched.
- `tools/oracle/tick_probe.cpp`: the regions S0..S4, the header among the
  compared globals, `register_probes_statistics`, the countdown rule.
- `tools/oracle/probe_registry.hpp`, `tools/oracle/CMakeLists.txt`,
  `src/p3data/CMakeLists.txt`: the new files.
- `src/p3data/savegame.hpp`: `Statistics`, `SaveGame::statistics`,
  `SaveGame::record_statistics`.
