# Unchecked branches (cloud/unchecked-branches)

Task: `p3vita-datos/tasks/unchecked-branches.md`. The branches marked «Sin
comprobar con el oráculo» in the files of `src/p3data/town_tick/` that this
task owns, checked against the game with retouched states.

## Summary

**In progress.** Done so far:

- **Tools** (new files): `tools/oracle/retouch_unchecked.py` (the scenarios:
  a retouched state, direct calls or operations while recording, and block
  hooks that say whether each target branch of the game ran),
  `tools/oracle/unchecked.sh` (runs scenarios in several saves, 4 at a time,
  and repeats each recording with `tick_probe`) and
  `tools/oracle/probes_unchecked.cpp` (probes for 0x51A4E0, 0x51A750,
  0x516170, 0x5144A0, 0x4F9C80, 0x4FA7F0, 0x5250D0, 0x51F3A0, 0x4F8A50,
  0x4F8C70 and 0x4F8560; the ones with pointers to the caller's variables
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

Left: `facilities.cpp` (35),
`offices.cpp` (28), `businesses.cpp` (7), `population.cpp` (8),
`merchants.cpp` (6), `diet.cpp` (15), `sieges.cpp` (17), `grid.cpp` (6).

## Decisions for the user

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

## Game bugs

- **Operation 0x3B with a merchant out of the world** (certain; 0x53A4CA
  `jae 0x53A619`, where edi is still the event pointer): nothing is taken
  from offices or ships, and the road task's counter of the material gets
  `counter + amount − address of the event` (its low 8 bits, and 2 more in
  [+0x13]). Reproduced (`alderman.cpp`, `fn_53a3e0`). Fix: return without
  touching the task.

## For the roadmap

- The list of game bugs: the one above.

## For the inventory

- `alderman.cpp`, `construction.cpp` and `new_town.cpp`: every branch
  checked with the oracle (scenarios of `tools/oracle/retouch_unchecked.py`).
- D: 0x4FF2D0 for the store a merchant builds is done (`construction.cpp`);
  0x51FAB0 building at once is rewritten.
- B/C: operation 0x25 (0x535B7B → 0x5244F0, which calls 0x51F3A0 and
  0x4F8A50 without `count`) is not handled by p3vita; nothing in the
  simulation queues it as far as these checks go (worth confirming in the
  operations inventory).

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
- `world.sh 7`, the 12 saves in three batches after `construction.cpp`:
  130 s, 126 s and 116 s (0 different).
- `world.sh 7`, the 12 saves in three batches after `alderman.cpp`: 130 s,
  131 s and 121 s (0 different; skipped as in `README.md`: 1 in save 1, 5 in
  5, 4 in 7, 2 in tut2, 5 in tut3, 7 in tut4).

Not built for the Vita: nothing in `src/app` or in the p3data API the viewer
uses changed.
