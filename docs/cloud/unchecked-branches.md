# Unchecked branches (cloud/unchecked-branches)

Task: `p3vita-datos/tasks/unchecked-branches.md`. The branches marked «Sin
comprobar con el oráculo» in the files of `src/p3data/town_tick/` that this
task owns, checked against the game with retouched states.

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

## Decisions for the user

- **The bankruptcy's endless surname search** (`merchants.cpp`, 0x4F5D80):
  reproduced as it is, so p3vita would hang where the game hangs (rare: the
  last merchant's surname index small and every surname up to it taken).
  Recommendation: keep it for now and fix it with the fix-bugs setting (stop
  after one turn); if a hang on the Vita is not acceptable even as an
  option, fix it always.

- **Operation 0x3B with a merchant out of the world** (`alderman.cpp`,
  0x53A3E0): the game uses the event's address as the amount missing and
  writes garbage into the road task's counters. p3vita now does the same
  with the address in `*0x6DD73C` (what the oracle has; in p3vita's own
  runs it is whatever the loader leaves there). Only the interface or a
  network game could send it. Recommendation: keep it as a game bug for the
  fix-bugs setting (with the fix: do nothing, as p3vita did before).

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

## Game bugs

- **Operation 0x3B with a merchant out of the world** (certain; 0x53A4CA
  `jae 0x53A619`, where edi is still the event pointer): nothing is taken
  from offices or ships, and the road task's counter of the material gets
  `counter + amount − address of the event` (its low 8 bits, and 2 more in
  [+0x13]). Reproduced (`alderman.cpp`, `fn_53a3e0`). Fix: return without
  touching the task.

- **The bankruptcy divides by zero** (certain; 0x4F5F57, operation 0x65):
  the free surname is searched modulo the last merchant's surname index; if
  that is 0 the game fails (confirmed with the oracle: an exception). p3vita
  returns instead (it can't reproduce a failure). Fix: modulo the number
  of surnames.
- **The bankruptcy's surname search can never end** (certain from the code,
  seen in the oracle with a retouched state; 0x4F5F68..0x4F5F86): the
  bankrupt merchant's own surname is already 0 (0x4F68D0 runs before), so
  if every surname from 0 to that divisor is taken the loop goes round for
  ever. In the game it needs the last merchant's surname index to be small
  and every surname up to it taken. p3vita reproduces it (it would hang
  too). Fix: stop after one turn.
- (Also in the code, already noted there: past the number of surnames the
  search reads beyond its table.)

- **0x62B2B0 doesn't check how many targets fit** (certain from the code,
  0x62B2F8..0x62B3B7): it reallocs [+0x250] for [+0x242] entries, then
  writes one for every tower 0x32/0x33 near the gate it finds, without
  looking at that size; with more it writes past its heap block. Only the
  loader calls it, and in the saves [+0x242] is always 0. p3vita grows the
  vector (comment in `sieges.cpp`). Fix: the same.

## For the roadmap

- The list of game bugs: the five above (two of them are decisions).

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

Not built for the Vita: nothing in `src/app` or in the p3data API the viewer
uses changed.
