# Unchecked branches (cloud/unchecked-branches)

Task: `p3vita-datos/tasks/unchecked-branches.md`. The branches marked «Sin
comprobar con el oráculo» in the files of `src/p3data/town_tick/` that this
task owns, checked against the game with retouched states; then (second
part) the game's bugs of these files fixed behind `fix_game_bugs()`.

## Summary

**Done.** The 213 marks «Sin comprobar con el oráculo» of the 11 files this
task owns are gone: 188 branches checked with the oracle, 23 explained in
the code as unreachable, 1 not reached (a float rounding case in
`businesses.cpp`) and 1 where the game fails (a division by zero, checked:
the oracle gets the exception). 42 scenarios; every recording repeated with
`tick_probe` with 0 different calls. Two pieces of simulation code changed:
`alderman.cpp` (a game bug now reproduced) and `construction.cpp` (two
gaps rewritten in C++); after each, `world.sh 7` on the 12 saves: 0
different. The rest only changed comments.

| File | Marks | Checked | Unreachable | Other | Scenarios | Targets |
|---|---|---|---|---|---|---|
| alderman.cpp | 28 | 27 | 1 | — | 10 | 81/81 |
| construction.cpp | 42 | 42 | 0 | — | 7 | 130/130 |
| new_town.cpp | 21 | 20 | 1 | — | 5 | 50/50 |
| facilities.cpp | 35 | 31 | 4 | — | 7 | 68/68 |
| offices.cpp | 28 | 26 | 2 | — | 3 | 62/62 |
| businesses.cpp, population.cpp, merchants.cpp | 21 | 10 | 9 | 1 not reached, 1 game failure | 5 | 24/26 |
| diet.cpp | 15 | 12 | 3 | — | 1 | 13/13 |
| grid.cpp | 6 | 6 | 0 | — | 1 | 6/6 |
| sieges.cpp | 17 | 14 | 3 | — | 3 | 28/28 |

Details per file below. In order:

- **Tools** (new files): `tools/oracle/retouch_unchecked.py` (the scenarios:
  a retouched state, direct calls or operations while recording, and block
  hooks that say whether each target branch of the game ran),
  `tools/oracle/unchecked.sh` (runs scenarios in several saves, 4 at a time,
  and repeats each recording with `tick_probe`) and
  `tools/oracle/probes_unchecked.cpp` (probes for 0x51A4E0, 0x51A750,
  0x516170, 0x5144A0, 0x4F9C80, 0x4FA7F0, 0x5250D0, 0x51F3A0, 0x4F8A50,
  0x4F8C70, 0x4F8560, 0x5462F0, 0x4F68D0, 0x4F8CA0, 0x4FD680, 0x4FACD0,
  0x4FD830, 0x629C60 and 0x633DB0; the ones with pointers to the caller's variables
  take them in scratch globals, 0x6DFF00..0x6DFFFF, that the game never
  uses).
- **`alderman.cpp`** (the path to Alderman, the priority): its 28 marks are
  gone. 27 branches checked with 10 scenarios (19 runs in saves 1, 3 and 5:
  81 of 81 target branches run by the game, 2757 calls repeated, 0
  different); 1 branch is unreachable (explained in the code). One
  difference found and fixed: operation 0x3B with a merchant out of the
  world, a game bug now reproduced (see «Game bugs»). Whole world after it:
  `world.sh 7`, 12 saves in three batches, 0 different.
- **`construction.cpp`**: its 42 marks are gone, all checked with 7
  scenarios (14 runs in saves 3 and 5: 130 of 130 target branches run, 472
  calls repeated, 0 different). Two gaps rewritten on the way: 0x51FAB0
  building at once (`immediate`, an `assert(false)` before; only the
  interface and 0x545F50 use it) and the store (0x1E) a merchant builds,
  which recounts his office (0x4FF2D0). Whole world: 0 different.
- **`new_town.cpp`**: its 21 marks are gone: 20 checked with 5 scenarios
  (10 runs in saves 3 and 5: 50 of 50 targets, 1570 calls equal), 1
  unreachable. Only comments changed, so no world run.
- **`facilities.cpp`**: its 35 marks are gone: 31 checked with 7 scenarios
  (14 runs in saves 3 and 5: 68 of 68 targets, 64 calls equal), 4
  unreachable. Only comments changed.
- **`offices.cpp`**: its 28 marks are gone: 26 checked with 3 scenarios
  (6 runs in saves 3 and 5: 62 of 62 targets, 188 calls equal), 2
  unreachable. Only comments changed.
- **`businesses.cpp`, `population.cpp`, `merchants.cpp`** (7, 8 and 6
  marks): 10 checked with 5 scenarios (10 runs in saves 3 and 5: 24 of 26
  targets, 162 calls equal), 9 unreachable, 1 not reached (a float
  rounding case) and 1 game bug the oracle confirms (a division by zero,
  the game fails); one more game bug found (a search that never ends).
  Only comments changed.
- **`diet.cpp`**: its 15 marks are gone: 12 checked with 1 scenario (saves
  3 and 5: 13 of 13 targets, 6 calls equal), 3 unreachable. Only comments
  changed.
- **`grid.cpp`**: its 6 marks are gone, all checked with 1 scenario (saves
  3 and 5: 6 of 6 targets, 6 calls equal). Only comments changed.

- **`sieges.cpp`**: its 17 marks are gone: 14 checked with 3 scenarios
  (saves 3 and 5: 28 of 28 targets, every call equal), 3 unreachable; one
  more game bug noted (0x62B2B0 doesn't check its size). Only comments
  changed.

Marks left in files this task doesn't own (other sessions' or the local
one's): `src/p3data/convoys.cpp` 6, `convoys_members.cpp` 1,
`convoys_port.cpp` 3, `convoys_sea.cpp` 4, `sailing.cpp` 7; in
`src/p3data/town_tick/`: `events.cpp` 45, `events_big.cpp` 1,
`events_mayor.cpp` 30, `events_rare.cpp` 68, `events_towns.cpp` 45,
`operations.cpp` 32, `ship_ai.cpp` 3, `ship_trade.cpp` 4.

**Second part: the game's bugs fixed behind `fix_game_bugs()`** (main
21cade4 merged; the items of the roadmap's «Corregir los fallos del juego»
that are this task's, plus the two of the first part). Each fix is next to
the original behaviour, in the file of the bug:

| Bug | File | With the setting on |
|---|---|---|
| The player's expired road (0x4EB25F): the copy's merchant is the town [+0x10] | `alderman.cpp` | the copy has no merchant (0xFF): the game's own check removes it |
| Operation 0x3B with a merchant out of the world (the event's address as the amount) | `alderman.cpp` | nothing: the task stays as it is |
| The wall pieces' upgrade 0x523D70: the tools with the timber's amount, paid 1.278 · 0, the merchant not paid | `construction.cpp` | the costs as for the towers (0x523830) |
| 0x524920 with a pier in the last column (or y < 2): cells past the grid, or in the previous column | `construction.cpp` | only the cells inside the grid |
| 0x51C360 adds only the first office's businesses | `population.cpp` | every office ([+0x2CA]) |
| 0x5109B0 takes what leaves from the float [+0xC] twice (negative) | `businesses.cpp` | once (it keeps the fraction) |
| The grain farm compares the room the other way round when hiring | `businesses.cpp` | as the other facilities |
| The bankrupt merchant's surname (0x4F5D80): modulo the last merchant's surname index (division by zero, endless search, read past the table) | `merchants.cpp` | modulo the number of surnames, without himself, one turn at most |
| The new besieging army 0x629C60: no room check for the groups; the gate not found read from address 0; unfilled targets in the strength | `sieges.cpp` | at most 100 groups; the missing gates left out; only the targets 0x62B2B0 filled |

How each was checked (details in «Fixes behind fix_game_bugs»):
- with the setting off, the scenarios that reach each bug (new ones:
  `road_expired`, `gate_costs`, `pier_edge`, `town_tick_fixes`,
  `siege_fixes`; and `hanse_ops`, `tower_ops`, `bankruptcy`, `siege_gates`,
  `siege_branches`) give 0 different calls, except one call of
  `siege_fixes` where the game overwrites its own army (explained there);
  and `world.sh 7` on the 12 saves: 0 different;
- with `tick_probe --fix-game-bugs`, only what each fix changes differs;
- the whole world with `--fix-game-bugs` (7 days, 12 saves): three of the
  fixes act in normal play (0x51C360 in 6 saves, the grain farm in save 3,
  0x5109B0 in saves 1, 5, 6 and tut4); in tut0, another session's fix
  (operation 0x28); nothing else. Each first difference was traced to a
  fix.

## Decisions for the user

- **The two decisions of the first part are done as recommended**: the
  bankruptcy's endless search and operation 0x3B are reproduced with the
  setting off and fixed with it on.

- **The player's expired road**: the roadmap gave two fixes, no copy or a
  copy without merchant. I chose the copy without merchant (0xFF): the
  event queue stays as in the game (the copy is made and then removed by
  the game's own check, 0x532C50, because the two towns keep the road's
  bit 0x8000), so the fix changes one byte. The road between those two
  towns is never offered again either way (that bit stays, as when an AI
  merchant fails it). If you'd rather offer the road again, it needs the
  bit cleared too: say so.

- **The fixes change normal play**: 0x51C360 (people leaving poor:
  fewer workers dismissed from the facilities when the town has several
  offices with businesses), 0x5109B0 (the float of the reserve) and the
  grain farm act in 7 of the 12 saves within 7 days. That is what the
  setting is for (on by default in the viewer); just so you know it is not
  only rare cases.

- **0x629C60 with the setting off, in states the saves don't reach**:
  - more than 100 groups: the game overwrites its own army ([+0x574] on:
    a graphics object, the strengths, its town...) and then the heap;
    p3vita can't reproduce that, so it keeps the groups that fall outside
    in a local array (before, an `assert` failed there);
  - the gate not found: the game reads address 0 (on Windows it fails);
    the oracle has there, at that moment, an SEH frame's address on its
    stack (0x010FEF30), so p3vita reads (0x30, 0xEF) to match the oracle
    (before it read (0, 0), which matched only while no building was near
    either). Recommendation: keep it; the alternative is (0, 0) and
    accepting a difference with the oracle in that retouched state.

- **A blind spot of tick_probe** (shared file, not touched): a 4-byte word
  that looks like a pointer (0x01000000..0x010FFFFF, 0x04000000..,
  0x10000000..0x17FFFFFF) is never compared. In an event 0x32 the word
  [+0xC..+0xF] is `01 merchant 00 01` for a started road task, so a wrong
  merchant there was invisible (found with `road_expired`: the scenario now
  sets [+0xE] = 0x20 to avoid it). The earlier checks of `alderman.cpp`
  could have missed a difference in [+0xD] of a started task with
  [+0xE] < 0x10. Suggestion for whoever owns tick_probe: don't take as
  pointers the words of the regions that have no pointers (E, B, W...).

## How it is checked

For each marked branch: the state that reaches it (from the disassembly), a
scenario in `tools/oracle/retouch_unchecked.py` that creates it after loading
a save, the recording of the functions involved, `tick_probe` call by call
(without `--continuo`), and block hooks on the game's code that confirm the
branch ran. Each scenario lists its targets: a block (`51a48e`) or an edge
between consecutive blocks (`53275c>532c1f`, for a conditional jump into a
block shared by several branches); the script prints each one as `hit` or
`MISSING`.

```sh
export P3_EXE=.../Patrician3.exe P3_CPR=.../p2arch0_eng.cpr P3_SHIPS=/tmp/datos
export P3_BUILD=~/build/p3vita-pc-check      # the build with tick_probe
bash tools/oracle/unchecked.sh hanse_ops:3,5 task_branches:3,5
python3 tools/oracle/retouch_unchecked.py --list
```

Three kinds of scenario:
- **retouched state + days**: e.g. tasks moved to the next ticks of the
  queue (`retouch.move_event`) with the fields of the branch; records the
  event queue (0x4D85C0) and what it calls.
- **direct calls while recording**: the game's function through a
  trampoline (push the arguments, call, ret) so the recorder sees it; for
  branches whose callers p3vita doesn't run yet (e.g. ships that already
  have artillery) or to run 0x4F75F0 right after a retouch that the town
  tick would undo.
- **operations**: run with the game's executor through the oracle's
  trampoline (the mark «P3OP»), as the interface or a network game could
  send them.

## alderman.cpp

28 marks: 27 checked, 1 unreachable, 0 left. Scenarios (saves; days):

| Branch (p3vita) | Game | Scenario |
|---|---|---|
| 0x515A60: no free number after wrapping | 0x515AD4 | unreachable: at most 26 towns + 5 lairs = 31 of 40 numbers |
| 0x51A470: the ship's pirate doesn't exist | 0x51A48E | `candidacy_pirate_index` (1, 3; 1) |
| 0x5326E0: 26 towns, no task 0 | 0x53275C→0x532C1F | `candidacy_26_towns` (3; 1) |
| 0x5326E0: no pirate ship with a captain, no task 2 | 0x5329DC→0x532C1F | `candidacy_no_targets` (1, 3; 1) |
| 0x5326E0: no town under 2001, no task 4 | 0x532BA2→0x532C1F | `candidacy_no_targets` (1, 3; 1) |
| 0x53FD10 (op 0x85): kind ≥ 5 | 0x53FD10→0x53FE00 | `hanse_ops` (3, 5) |
| 0x4EBF54: already started | 0x4EBF89→0x4EC30C | `hanse_ops` (op 0x4B) |
| 0x4EBF54: road of no pair, default deadline | 0x4EC108 | `task_branches` (3, 5; 1) |
| 0x4EBF54: kind 5 | 0x4EBF9A→0x4EC30C | `task_branches` |
| 0x4EC354: kind 0, [+0x37] + 2 | 0x4EC481 | `new_town_market` (3, 5; 1) |
| 0x4EB0EB kind 0: the new town has a market | 0x4EB41E | `new_town_market` |
| road: a town out of the world | 0x4EB44A/0x4EB45D→0x4EBF0F | `task_branches` |
| kind 2 with [+0x16] = 1: done | 0x4EB7AA | `task_branches` |
| lair phase 0, pirate gone | 0x4EBD06 | `nest_branches` (3, 5; 1) |
| lair phase 1, another lair | 0x4EB972 | `nest_branches` |
| lair phase 1, attacker at the lair / away | 0x4EB99E, 0x4EB9D9 | `nest_branches` |
| 0x5144A0: another lair / pirate out | 0x5144A0→0x514526, 0x5144B0→0x514526 | `nest_reinforce`, `nest_reinforce_away` (3, 5; 1) |
| 0x51A4E0: level > 4 | 0x51A530 | `nest_reinforce` (Hanse town at level 5), `ship_artillery` |
| 0x51A4E0: strength reached, no room, level < 2 | 0x51A4E0/0x51A50A/0x51A51E→0x51A73C | `ship_artillery` (3, 5; direct calls) |
| 0x51A4E0: a big piece doesn't fit (break) | 0x51A566 | `ship_artillery` |
| 0x51A4E0: pieces replaced (remove, both halves) | 0x51A5F0, 0x51A637 | `ship_artillery` |
| 0x51A750: already at level 2 | 0x51A76D→0x51A806 | `ship_artillery` |
| 0x53A3E0 (op 0x3B): each check (7) | 0x53A3E0…0x53A45B→0x53A613 | `hanse_ops` |
| 0x53A3E0: merchant out of the world | 0x53A619 | `hanse_ops` (**was different: fixed**) |
| 0x53B440 (op 0x4B): each check (5) | 0x53B440…0x53B488→0x53B4CC | `hanse_ops` |

Calls repeated by `tick_probe` (all equal): 0x4D85C0 2560, 0x535760 112,
0x51A4E0 30, 0x51A750 18, 0x516170 14, 0x5144A0 12, 0x4EBF54 6,
0x5326E0 5. `candidacy_26_towns` founds two towns with the game's code
first; in save 1 the second founding task isn't created (the game finds it
equal to the first one, the same mask of wares), so it runs in save 3.

Which of these the game can reach on its own: the operations 0x3B and 0x4B
come only from the interface (the player's merchant and a task of the list)
or a network game; the 0x85 of kind 5 never (0x5326E0 queues kinds 0..4);
0x51A4E0's replacements and 0x51A750 at level 2 only from callers p3vita
doesn't rewrite yet (the pirate fleets, 0x514000, and the battles); the
rest are states a game reaches (a pirate whose ships were lost, 26 towns,
the lair's pirate out at sea...).

## construction.cpp

42 marks: all checked, 0 left. Scenarios (saves 3 and 5; 0 days: direct
calls and operations run while recording):

| Branch (p3vita) | Game | Scenario |
|---|---|---|
| 0x4F9C80: type ≥ 0x39; every type's site (both orientations and flags); out of the grid; the start is already a site | 0x4F9C80→0x4FA10D, 0x4F9CC9→0x4FA10D, 0x4F9CA6, 0x4F9CBB, the 17 case blocks, 0x4F9EAB→0x4F9EE2 | `site_search` |
| 0x51FAB0: type < 0x15 without a site; houses next to one of their type (both branches); bath; shipyard; tower at once | 0x51FAE0→0x51FE7F, 0x51FBBC, 0x51FBC0, 0x51FC05, 0x51FC17, 0x51FC4D | `site_search` |
| 0x51FAB0 at once (rewritten): a house (0x521900) and a tower (0x523830) | 0x51FDB3, 0x51FDD9 | `site_search` |
| 0x5250D0: x wraps at the left edge | 0x525177 | `site_search` |
| 0x5254C0: types ≥ 0x30 at the end | 0x52552B | `tower_ops`, `wall_ops` |
| 0x521900: type ≥ 0x30; the fisherman's hemp halved; a municipal building already built; a business in a siege; the arsenal's wall (0x522B20); bath, well, 0x2A, 0x2B refused | 0x521919, 0x521A08, 0x521DA3, 0x521AE2, 0x521E12, 0x52207F, 0x5220E5, 0x522169, 0x5221C1 | `build_ops` |
| 0x521900 by a merchant: no office (0x4F5450), the store (+ 0x4FF2D0, rewritten), 0x29/0x2C (0x4F8A50), 0x2A/0x2B (0x4F8C70), 0x22 (0x4F8560) | 0x522415, 0x522533, 0x52255A, 0x5225A2, 0x522592 | `build_ops` |
| 0x4F8A50: no inhabitants, without `count`, few | 0x4F8A50→0x4F8AE6, 0x4F8AC7, 0x4F8AA8 | `chapel_reputation` (direct), `build_ops` |
| 0x4F8560 (op 0x37): town out of the world, poor, already paid, 0x6DE52A, paid | 0x4F8560/0x4F858F→0x4F8672, 0x4F85AF/0x4F85C5→0x4F8661, 0x4F85D3 | `build_ops` |
| 0x523830: no piece there; a wall piece and a 0x32 in a siege; a new tower | 0x523927→0x523D66, 0x523990, 0x5239C0→0x523D66 | `tower_ops`, `tower_ops_big` |
| 0x523D70: a gate on a finished piece (with the reproduced tools bug) | 0x523E3D | `tower_ops` |
| 0x51F3A0: the fisherman's hemp halved | 0x51F44C | `cost_fisherman` (direct) |
| 0x522C70: siege; the wall closed | 0x522CA3→0x523803, 0x5230DC | `wall_ops` |
| ops 0x27 (out of the world, hemp bit), 0x23, 0x24, 0x22 (out of the world), 0x22 of 0x36..0x39 | 0x535BBC→0x537025, 0x535BE8, 0x535B12/0x535B44→0x537025, 0x53CE30→0x53CEF2, 0x53CF25 | `build_ops` |

Calls repeated (all equal): 0x4F9C80 230, 0x535760 81, 0x521900 34,
0x51FAB0 28, 0x5254C0 24, 0x523830 17, 0x4F8A50 12, 0x4F8560 12, 0x5250D0
6, 0x4FA7F0 6, 0x51F3A0 6, 0x4F8C70 6, 0x522C70 4. The sites are prepared
by writing the cell of the grid with the class each type asks for.

Reachability: types ≥ 0x39 (0x4F9C80) and ≥ 0x30 (0x521900) no caller
passes; 0x4F8A50 without `count` only from operation 0x25 (0x5244F0),
which the simulation doesn't queue (interface); building at once only from
the interface and 0x545F50; the operations' checks of a town out of the
world only matter for the interface and network games. Still not
rewritten here: 0x4F9C80 with the types 0x34 and 0x35 (the game searches
with an unset mask: no caller of the simulation uses them).

## new_town.cpp

21 marks: 20 checked, 1 unreachable, 0 left. Scenarios (saves 3 and 5):

| Branch (p3vita) | Game | Scenario |
|---|---|---|
| 0x5289B0: the names, families and [0x6EC] wrap round their list | 0x528A0D, 0x528A55, 0x528C1D | `mayor_names` (direct calls, series chosen by replaying the game's steps in Python) |
| 0x5289B0: the same pair as another town's (the name, then the family, moves on) | 0x528ADB, 0x528AE3, 0x528AFB | `mayor_names` |
| 0x5289B0: more than 100000 checks | 0x528B57 | unreachable (at most ~2500) |
| 0x5462F0: facility out of 4..20 | 0x5462F0/0x546313→0x546387 | `new_town_bits` (direct) |
| 0x5458D0: bits 0x20000 of `wares` and of `other`, 0x80000, 0x100000, 0x200000; [0x2CC] > 0 | 0x545958, 0x54597B, 0x5459BE, 0x5459D5, 0x5459EC, 0x545AAB | `new_town_bits` (direct; the founding never gives them) |
| 0x531D50: a town on a river (AmFluss) | 0x531FF7 | `river_town` (the founding; the river numbers 9, 31 and 39 are taken in these saves, so the task's number gets the flag in the table) |
| 0x4ECDF4 (event 0x39): town out of the world; [0x2C8] & 0xE0; pending pieces; wall 2; wall 0/1 | 0x4ECDF4/0x4ECE19→0x4ECF58, 0x4ECE48, 0x4ECE9C→0x4ECF58, 0x4ECEA4 | `new_town_events` |
| 0x4EA0D4 (event 0x2F): town out of the world | 0x4EA0F9 | `new_town_events` |
| op 0x46: merchant or town out of the world; a mayor already; rank < 5 without the mark | 0x535FD6/0x535FED/0x535FFE/0x536039→0x537025 | `mayor_ops` |

Calls repeated (all equal): 0x4D85C0 1536, 0x5289B0 10, 0x535760 10,
0x5462F0 8, 0x5458D0 4, 0x4EBF54 2.

## facilities.cpp

35 marks: 31 checked, 4 unreachable, 0 left. All direct calls (saves 3 and
5):

| Branch (p3vita) | Scenario |
|---|---|
| 0x510C50 (militia): employees not 5 per soldier; guards to spare (0x51D410 of type 0); soldiers of types 1, 2, 3 to spare for the walls; a human mayor | `militia` |
| 0x51D410: a type without soldiers nor recruits (only operation 0x40, interface); the third class under 20 | `militia` |
| 0x51D4C0: a human mayor (his office's weapons; without an office there, nothing) | `militia` |
| 0x51FF30: the 0x30 waits with [0x2C8] & 0x800; owned works ending as shipyard, town list, wall, tower, big tower, replaced gate, free; the town's as free, shipyard, list, flag, replaced gate | `works_completion` (a works list of new records, 0x51F100) |
| 0x4D36E0 with 6 and 9 businesses; 0x5306B0 growing by 0x40; 0x530780 on a free record and on a business with employees | `world_records` |
| 0x5252F0: 0 (first pending piece not the town's) and 2 (no point 2) | `wall_level` |
| 0x508AA0: a repaired ship with orders 2, one not finished with orders 1 | `shipyard_repair` |
| 0x510AA0 keeps its weapon; 0x50F6E0 with [+0x0] = 0 | `weaponsmith` |
| 0x4FF2D0: [0x2F0] > 0; a pending store; a business whose buildings are gone | `office_recount` |
| unreachable: 0x510AA0 with level ≤ 1 (0x52BB00 gives 2..5); the office versions of 0x50E500, 0x50E570 and 0x50F6E0 (offices never have businesses of types 0..3: those buildings go to the town) | — |

Calls repeated (all equal): 0x510C50 12, 0x51D410 12, 0x51D4C0 10,
0x4FF2D0 6, 0x51FF30 4, 0x4D36E0 4, 0x5252F0 4, 0x530780 4, and 2 each of
0x508AA0, 0x50F6E0, 0x510AA0 and 0x5306B0. A direct call of 0x4D3780 on a
business of type 0..3 also showed that p3vita asserts there (an access out
of a merchant's memory); it can't happen, so it is left as it is.

## offices.cpp

28 marks: 26 checked, 2 unreachable, 0 left. All direct calls (saves 3 and
5):

| Branch (p3vita) | Scenario |
|---|---|
| 0x4FFF20: the human player's office with the orders 0x10 (cleared); orders 2 (0x4FFC20, which no save has) | `office_orders` |
| 0x4FF780 (administrator): none; selling over its stock (0x52EA80, no save sells); buying more than the town has (negative thresholds), for 0x1000000 or more, without the money | `office_orders` |
| 0x52EC50 and 0x52EA80: every interval of the price curve (17 prices × 2 wares), quality, the taxed ware, no market | `reverse_prices` |
| 0x4D4400: a type out of 4..20 (type 3); fishing without fish, cattle without leather and without meat; 0x4D3670 dismissing; 0x4D3560 a human's fishing with [0x2CC]; 0x4F8690/0x4F8840 without `flag`, saturating at 0x7FFFFFFF, and a ware ≥ 0x14 | `office_business` |
| unreachable: 0x4D3780's types 0..3 (as in facilities.cpp); 0x4D4400 with a type ≥ 0x15 (the game divides by a 0 of 0x6729D0 first) | — |

Calls repeated (all equal): 0x52EA80 74, 0x52EC50 74, 0x4D4400 10,
0x4F8690 8, 0x4F8840 6, 0x4FF780 4, 0x4FFC20 4, 0x4FFF20 4, 0x4D3560 2,
0x4D3670 2. The script's targets can now also be a taken jump
(`@A>B`: B runs right after the jump at A): the emulator merges blocks
when it falls into a jump target, so some edges aren't seen as blocks.

## businesses.cpp, population.cpp, merchants.cpp

| Branch (p3vita) | Scenario / why not |
|---|---|
| 0x50E380 with more than its workers, with `all` and without; 0x5109B0 with more than its workers; 0x50FBC0's timber below 0 (only by an overflow: pig iron below 0 and the timber at the top) | `business_staff` (direct) |
| 0x50E380: a share passing the people left | unreachable (the shares are floors of weight · people / sum with weights ≥ 0) |
| 0x5101D0: the works wanting fewer than 5; a type > 20 | unreachable (0x50E610 gives 5 or more; facilities 0..20) |
| 0x51C360: n ≥ the staff (facilities) and n > the staff (offices' businesses) | unreachable (what is left of the sum is under the ratio, ≥ 2) |
| 0x520840: 2·half ≤ the occupancy | unreachable (exact; a full class is handled before) |
| 0x520840: a negative step | not reached: only float rounding could do it; `housing` tries every town of saves 3 and 5 with each class 1, 2 and 3 places short of full (144 calls, all equal) and it never happens |
| 0x51CBE0: a job offer of a world record out of the table and one of another town | `job_offers` (direct) |
| 0x520BD0: the truncated parts passing the surplus; a negative class | unreachable |
| 0x4F7BB0: the clamp at ±30000 | unreachable (\|target\| ≤ 20 × 127) |
| 0x4F5D80 (op 0x65): the human player; the search for a free surname going back to 0; the merchant was his home town's mayor; 0x4F68D0 with a3 = 1 | `bankruptcy` |
| 0x4F5D80: the divisor (the last merchant's surname) is 0 | game bug: the game divides by zero (`bankruptcy_zero`: an exception at 0x4F5F57); p3vita returns |

Calls repeated (all equal): 0x520840 144, 0x50E380 4, 0x4F68D0 4,
0x535760 4, 0x50FBC0 2, 0x5109B0 2, 0x51CBE0 2.

## diet.cpp

15 marks: 12 checked, 3 unreachable, 0 left. Scenario `diet_branches`
(saves 3 and 5): missions of the diet (events 0x34 and 0x24) put at the
current tick, and the event queue (0x4D85C0) called directly so that no
other part of the world runs; plus a direct call to 0x4F8CA0.

| Branch (p3vita) | Game |
|---|---|
| 0x4E09D4: a town out of the world; a phase past 10 | 0x4E0A10/0x4E0A2E→0x4E1B36 |
| disband (phase 5): a convoy out of the list; one out of the table | 0x4E1272→0x4E1830, 0x4E13A7→0x4E1B36 |
| phases 7, 8, 9, 10 without a convoy | 0x4E147D/0x4E1547/0x4E16AF/0x4E18FD→0x4E1B18 |
| phase 9: the convoy fighting (0x14); out of the list | 0x4E16E7, 0x4E170D→0x4E168F |
| phase 10: out of the list; the convoy not sailing back | 0x4E193D→0x4E16E9, 0x4E195D→0x4E1B18 |
| 0x4F8CA0 with a town (only the battles pass one) | 0x4F8CAF |
| unreachable: 0x50AC40 without a free convoy (0x509AE0 grows the table first); phases 8 and 9 with a convoy without its leading ship (the game doesn't look; the fleet's convoy always has it) | — |

A 0x34 from phase 2 on keeps in [+0x8] the game's list of notices for the
final letter: the scenario gives it none (0), as the game frees it on
finishing.

## grid.cpp

6 marks: all checked. Scenario `grid_branches` (saves 3 and 5, direct calls
on town 0's town_class1; probes for 0x4FD680, 0x4FACD0 and 0x4FD830):
0x4FACD0 of a fisherman (type 5, 0x4FB161) and a pier (0x2F, 0x4FB1F7);
0x4FD680 with the statue out of the grid (0x4FD693→0x4FD816); 0x4FD830
after a gate 0x37 outside the town, so that 0x4FDA10 looks for the walls:
no wall cell (0x91) and the statue out of the grid (0x4FDB0D→0x4FDC95), no
wall cell after the statue (0x4FDB21), a single wall cell after it whose
four seeds aren't 0x11 (0x4FDBB4→0x4FDC91). (The game's forward search
also reads the byte right after the grid; p3vita stops at the end.)

## sieges.cpp

17 marks: 14 checked, 3 unreachable, 0 left. Scenarios (saves 3 and 5; no
days, all within the same tick):

| Branch (p3vita) | Game | Scenario |
|---|---|---|
| operation 0x8E: a town out of the world; the human player with rank 5 in the region (asked for help: operation 0x8B queued) | 0x536A72→0x537025, 0x633CDF | `siege_ops` |
| 0x629C60 on an army that is not new ([+0x3A] set) | 0x629C9B→0x62B28F | `siege_branches` |
| 0x62E810 with the first target not finished (`++[+0x6C]`) | 0x62E8F2 | `siege_branches` |
| 0x62B2B0 (the loader's targets) with [+0x242] set | 0x62B3CE | `siege_branches` |
| 0x6281F0: a gate 0x34 attacked with ammunition, without pitch and with it | 0x6290E9, 0x62927E | `siege_branches` |
| 0x51D5F0 (a defender lost) with few people in the classes | 0x51D64C | `siege_branches` |
| 0x633DB0 (the ransom): nothing to pay | 0x634600 | `siege_branches` |
| 0x635220: a ware over 0x800000 in stock; no ware chosen (0xFF) | 0x6352B4, 0x6353AD | `siege_branches` |
| 0x635440: the supplies pay past the 0x4FFF cap | 0x6355C5 | `siege_branches` |
| 0x629C60: two gates on the side worth the same (the tie by inhabitants), and the third one, not found, read from address 0 | 0x62A3BA, 0x62A49F | `siege_gates` |
| unreachable: the NaN of the distances (a 256 × 256 grid doesn't overflow); the ransom's `take = 0` (amount + paid reaches the limit only if 200 · k units of a ware are worth an exact number, and none of wares 0..19 is); 0x633AF0 with a player that is not a merchant (the human player always is) | — | — |

All 28 targets ran, every call equal. `probes_unchecked.cpp` has its own
probe for 0x633DB0 (it overrides the one in `probes_sieges`): it writes the
amount and the offices back into the scratch globals, where the scenario
reads them.

## Fixes behind fix_game_bugs

Each fix is `if (fix_game_bugs())` next to the original code. Checked with
the scenarios below in saves 3 and 5: off, every call equal to the game;
on (`PROBE_ARGS=--fix-game-bugs bash tools/oracle/unchecked.sh ...`, or
`tick_probe ... --fix-game-bugs` on a recording kept with `KEEP=1`), only
what the fix changes:

| Fix | Scenario | With `--fix-game-bugs`, the differences |
|---|---|---|
| The player's expired road | `road_expired` (new: the player's road task expiring in the next tick, town 1 < merchants) | one call of 0x4D85C0: the copy's [+0xD], 0x01 → 0xFF |
| Operation 0x3B, merchant out of the world | `hanse_ops` | the 5 calls of 0x3B with that merchant: the task's counters [+0x13..+0x17] left as they were |
| The gate's upgrade 0x523D70 | `gate_costs` (new: the merchant's office with some of each material); `tower_ops` | 0x523D70's call: the office gives 5 barrels of tools (what the gate needs, not the timber's amount), the town doesn't lose its tools and takes timber, the merchant gets 1626 ([+0x0], [+0x49C]), the town's expense [+0x71C] and money [+0x700] |
| 0x524920's cells | `pier_edge` (new: the only pier cell at (last column, 1)) | the third cell, the last row of the previous column, not written |
| 0x51C360 | `town_tick_fixes` (new: a town whose second office has staffed businesses) | the workers dismissed from each facility and business |
| 0x5109B0 | `town_tick_fixes` (a business with a reserve and the float at 0.9) | [+0xC]: 0.14 instead of −0.86 |
| The grain farm | `town_tick_fixes` (the farm with 1 worker) | it hires what it needs (to 9) instead of filling the room (15), and takes less from the reserves and the beggars |
| The surname | `bankruptcy` | the new surname byte (1 → 0x30 in save 3, 1 → 0x0D in save 5) |
| 0x629C60's strength | `siege_fixes` (new: a tower in the works next to each gate) | [+0x580]: 1800 less (the unfilled entry read record 0, a finished piece) |
| 0x629C60's groups | `siege_fixes` (an office with every record outside the wall lists, at the chosen gate's place, no grid bit 0x80) | the army is sane (its town marked besieged, at most 100 groups) where the game overwrote itself |
| 0x629C60's missing gate | `siege_fixes` (two gates on the side, moved, and an office building nearer the place read at address 0) | the groups: the buildings the game gave to the missing gate go to the found ones |

With the setting off, `siege_fixes` has one call different from the game
on purpose: the second town, where the game overwrites its own army (see
«Game bugs»). Everything else in these scenarios is equal off.

**The world with the fixes** (the 12 saves, 7 days, `world.sh` with
`--fix-game-bugs` added to tick_probe, `--continuo`): saves 2, tut1, tut2
and tut3 give 0 different; tut0 gives 1 (operation 0x28: another session's
fix, as the roadmap says); saves 1, 3, 4, 5, 6, 7 and tut4 differ from
the first call where one of these fixes acts (then it carries on). With
traces (not committed) in saves 3 and 6: 0x51C360's fix acts in 4 and 7
towns, the grain farm's 26 times in save 3, 0x5109B0's once in save 6;
with 0x51C360's fix disabled, save 6's first difference moves to
0x5109B0's float. In saves 1, 5 and tut4 the first difference is
0x5109B0's float of a facility ([+0x96C] of the town); in 4 and 7, the
workers (0x51C360).

## Game bugs

- **Operation 0x3B with a merchant out of the world** (certain; 0x53A4CA
  `jae 0x53A619`, where edi is still the event pointer): nothing is taken
  from offices or ships, and the road task's counter of the material gets
  `counter + amount − address of the event` (its low 8 bits, and 2 more in
  [+0x13]). Reproduced (`alderman.cpp`, `fn_53a3e0`). Fixed with
  `fix_game_bugs()`: return without touching the task.

- **The bankruptcy divides by zero** (certain; 0x4F5F57, operation 0x65):
  the free surname is searched modulo the last merchant's surname index; if
  that is 0 the game fails (confirmed with the oracle: an exception). p3vita
  returns instead (it can't reproduce a failure). Fixed with
  `fix_game_bugs()`: modulo the number of surnames.
- **The bankruptcy's surname search can never end** (certain from the code,
  seen in the oracle with a retouched state; 0x4F5F68..0x4F5F86): the
  bankrupt merchant's own surname is already 0 (0x4F68D0 runs before), so
  if every surname from 0 to that divisor is taken the loop goes round for
  ever. In the game it needs the last merchant's surname index to be small
  and every surname up to it taken. p3vita reproduces it (it would hang
  too). Fixed with `fix_game_bugs()`: one turn at most, and the merchant
  himself doesn't count.
- (Also in the code, already noted there: past the number of surnames the
  search reads beyond its table.)

- **0x62B2B0 doesn't check how many targets fit** (certain from the code,
  0x62B2F8..0x62B3B7): it reallocs [+0x250] for [+0x242] entries, then
  writes one for every tower 0x32/0x33 near the gate it finds, without
  looking at that size; with more it writes past its heap block. Only the
  loader calls it, and in the saves [+0x242] is always 0. p3vita grows the
  vector (comment in `sieges.cpp`). Fix: the same.

- **0x629C60's groups run over the army before they leave it** (certain
  from the layout, seen in the oracle with `siege_fixes`): the roadmap says
  «more than 0x7F leave the army», but the groups are 8 bytes from
  [+0x254], so from the 101st on they overwrite the army's own fields
  ([+0x574] a graphics object, [+0x578], the strengths, the town [+0x5D3],
  the level...): in the oracle the game then doesn't even mark its town
  as besieged. Fixed with `fix_game_bugs()` (at most 100).
- **The gate not found** (certain): 0x51F1F0 gives 0 for an index out of
  the list, and 0x629C60 reads the gate's x and y at address 0: on
  Windows the game fails. In the oracle it reads an SEH frame's address
  (0x010FEF30: (0x30, 0xEF)).

## For the roadmap

- «Corregir los fallos del juego»: these items can be ticked, «Corregido
  con el ajuste» (details above, in the table of the summary): la carretera
  vencida del jugador (`alderman.cpp`; and the operation 0x3B sentence of
  that item: now reproduced with the setting off and fixed with it on);
  las mejoras de las piezas de muralla (`construction.cpp`; the item says
  «sin comprobar con el oráculo»: now checked, `gate_costs`, `tower_ops`);
  los del tick de ciudad (`population.cpp`, `businesses.cpp`,
  `construction.cpp`; checked with `town_tick_fixes` and `pier_edge`; the
  first three act in normal play: see the summary); el apellido de un
  comerciante en quiebra (`merchants.cpp`); el ejército sitiador nuevo
  (`sieges.cpp`; with the correction about the groups in «Game bugs»).
- `docs/formats/town_tick.md` §8, the sentence on the bugs: they are fixed
  with the setting, and 0x524920 no longer has an `assert`.
- The list of game bugs: the ones above.

## For the inventory

- `alderman.cpp`, `construction.cpp`, `new_town.cpp`, `facilities.cpp`,
  `offices.cpp`, `businesses.cpp`, `population.cpp`, `merchants.cpp`,
  `diet.cpp`, `grid.cpp` and `sieges.cpp`: every branch checked with the oracle or
  explained (scenarios of `tools/oracle/retouch_unchecked.py`).
- D: 0x4FF2D0 for the store a merchant builds is done (`construction.cpp`);
  0x51FAB0 building at once is rewritten.
- B/C: operation 0x25 (0x535B7B → 0x5244F0, which calls 0x51F3A0 and
  0x4F8A50 without `count`) is not handled by p3vita; nothing in the
  simulation queues it as far as these checks go (worth confirming in the
  operations inventory). The same for operations 0x40 and 0x41 (dismiss and
  recruit soldiers: 0x51D410 and 0x51D4C0, the mayor's interface).

## Changes to shared files

- `tools/oracle/CMakeLists.txt`: `probes_unchecked.cpp` added to tick_probe.
- `tools/oracle/probe_registry.hpp`: `register_probes_unchecked` declared.
- `tools/oracle/tick_probe.cpp`: `register_probes_unchecked(registry);`
  after `register_probes_battle`.

## Times

- `prepare.sh --test`: 45 s.
- The 19 runs of `alderman.cpp`'s scenarios (recording and tick_probe): 25 s.
- The 14 runs of `construction.cpp`'s scenarios: 9 s; the 10 of
  `new_town.cpp`: about 15 s.
- `world.sh 7`, the 12 saves in three batches at the end (branch head):
  132 s, 129 s and 116 s (0 different; skipped as in `README.md`).
- The scenarios of the other files: seconds each (sieges, 6 runs: about
  10 s).
- `world.sh 7`, the 12 saves in three batches after `construction.cpp`:
  130 s, 126 s and 116 s (0 different).
- `world.sh 7`, the 12 saves in three batches after `alderman.cpp`: 130 s,
  131 s and 121 s (0 different; skipped as in `README.md`: 1 in save 1, 5 in
  5, 4 in 7, 2 in tut2, 5 in tut3, 7 in tut4).

- The fixes: each scenario, off and on, seconds; `world.sh 7` off after
  them: 132 s, 129 s and 122 s (0 different; skipped as always); the same
  with `--fix-game-bugs` (a copy of `world.sh` in the scratchpad): about
  the same time.

Not built for the Vita: nothing in `src/app` or in the p3data API the viewer
uses changed.
