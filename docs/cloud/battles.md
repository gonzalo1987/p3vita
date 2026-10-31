# Battles and encounters: the study (cloud/battles)

Task `tasks/battles.md` of p3vita-datos. Phase 1 (this study) is about the
battle subsystem of Patrician3.exe (inventory part E, roadmap «Piratas,
persecuciones, encuentros y batallas»): what is simulation and what is UI,
where the simulation reaches it from and how often, what the oracle needs to
run it, and a plan in pieces.

Status: **phase 1 in progress** (this file is written as the work goes; see
«Done and left» at the end).

Tools written for the study (in p3vita-datos, branch `cloud/battles`,
`re/battles/`; nothing in p3vita uses them):
- `graph.py`: the static call graph of the battles, like
  `re/emu/sesion5/callgraph.py` (direct calls and jumps; calls made with
  `ecx` = a UI object are not followed) plus the methods of the two battle
  classes (vtables) and the handlers of the battle operations.
  `graph.py components [COVERAGE]` gives the size of each part.
- `explore.py`: the game in the emulator with the pirates moving. It
  subclasses `TownOracle` of `tools/oracle/town_oracle.py` without changing
  it: pirate convoys and every sea state run, the storms (0x5053C0) run,
  and after each tick, when there are battles, operation 0x94 is executed
  with N steps (`--steps`). It logs each entry into the battle code, what a
  battle changes in its ships and owners (`--trace`), the coverage of the
  battle code (`--cov`), the hull the storms take, and where the emulation
  stops. `explore.py town SAVE DAYS --town T` is a retouched state: the first
  pirate convoy attacks town T the way 0x506720 does.
- `bx.py` (jump tables, reading the exe) and `cfgx.sh` (`cfg.py` with the
  exe path of the cloud machine).

A run of the world without recording is cheap: about 1.2 s per day and save
(30 days in the 12 saves, four at a time: 2.5 min). What is slow in the
oracle is the recording (the regions of each sample).

## 1. How the battles work

### The battle objects

- Up to 255 battles: pointers in `0x6E55D0[0..0xFE]`, the number of slots
  used in the byte `[0x6E55D0 + 0x404]` (= 0x6E59D4). The array is saved in
  the savegame (0x611950 writes, 0x611AE0 reads, 0x6118F0 the size; the
  section of 5 bytes after the event queue in `docs/formats/savegame.md`):
  **no battle in any of the 12 saves**.
- Two C++ classes (three levels):
  - 0x67AF70, the base «scene» (constructor 0x618F10, 0x5C bytes of lists of
    the ships in the battle).
  - **Sea battle**, vtable 0x67AD44, 0x1E98 bytes: `0x611CD0` takes a free
    slot and builds it (0x603A70); `0x605420(ship_a, ship_b, x, y)` sets it
    up between the convoys of two ships. Destructor 0x603BC0.
  - **Town battle**, vtable 0x67AAA0 (derives from the sea battle),
    0x2F24 bytes: `0x611D80` takes a slot; `0x5FBFE0(ship, town)` sets it up
    (the town in `[+0x1E98]`); `0x6130B0(town, owner)` finds a running one
    of that town and attacker; `0x60234C(ship)` adds another attacker. The
    pirate nest is a town too: number `0xFE − k` for pirate k
    (0x4EAFE4, Hanse task type 3). Destructor 0x5FB9F0.
- Fields seen so far (sea battle): `[+0x35A]`, `[+0x35C]` the owners of the
  two sides (0xFE pirates); `[+0x5D]`, `[+0x5E]` side A / B is not a human
  player; `[+0xAC8]` 1 = both sides are AI: **automatic battle**;
  `[+0x65E]`, `[+0x660]` the convoys of the two sides; `[+0xAD4]`,
  `[+0xAD8]` the place (midpoint of the two ships); `[+0x14]` number of
  ships, `[+0x4]` their entries; `[+0x15C]` ≠ 0xFFFF while it runs;
  `[+0x1E94]` a value with 10-bit fields changed by operation 0x9F;
  `[+0x12E8]` the 4th argument of 0x605420.

Methods (vtable offset → function; «ran» = executed in the automatic
battles of §3):

| Offset | Sea battle (0x67AD44) | Town battle (0x67AAA0) | Notes |
|-|-|-|-|
| +0x00 | 0x608850 (ran, 8.3 M calls) | 0x5FF8E0 | per ship and step |
| +0x04 | 0x611740 (ran) | 0x602990 | end of the battle: destroys it (0x603BC0) |
| +0x08 | 0x604140 | 0x5FBE90 → 0x604140 | called by operation 0x92 |
| +0x0C | 0x604220 | 0x5FBEA0 → 0x604220 | |
| +0x10 | 0x604600 | 0x5FBF30 → 0x604600 | leads to 0x61A880 (16 KB) and the UI widgets 0x4B…–0x4C… |
| +0x14 | **0x606CC0 (ran)** | **0x5FD510** | **one step of the battle**: operation 0x94 |
| +0x18 | 0x608230 (ran) | 0x600660 | |
| +0x1C | 0x60D299 (ran) | 0x5FE7E0 | |
| +0x20 | 0x60ED39 | 0x6018DB | |
| +0x24 | 0x6102A9 | 0x5FFFA0 | |
| +0x28 | 0x611730 (ran) | 0x602980 | is it running (returns al) |
| +0x2C | 0x6079D0 (ran) | 0x600060 | 0x600060 uses the town view (below) |
| +0x30..+0x38 | — | 0x621770 (12 KB), 0x6247B0, 0x624950 | town only |

### A battle runs on operation 0x94, which comes from the real-time loop

`advance_time` only **starts** battles (§3). It never steps them: the call
it makes on the battles, 0x6127C0, only counts down a list of timers
(`[+0xC30]`, `[+0xC34]`) and handles the UI flags `[+0x42C]`, `[+0x42D]`
(on *0x6CBB10).

The steps come from the game's real-time driver (0x546620, on 0x6DF2F0, the
same that queues operation 0xC4 to advance the time and 0xC2): each frame it
calls 0x612DB0, which, when the previous one has been executed (the flag
`[0x6E55D0 + 0x3FE]`, set back to 1 by the operation), queues **operation
0x94 with a number of steps in `[+4]`** taken from the real clock
`[0x6DCCF8]` (ms):

```
elapsed = clamp(now − [+0xC44], 0, 100)        (100 000 in network mode 1)
steps   = (elapsed / 40) · 3375 / [0x6DFBC4]   ([0x6DFBC8] in mode 1), at most 0x5460
[+0xC44] = now − elapsed + 40 · steps
```

`[0x6DFBC4]` is the length of a tick in ms at the chosen speed (0x42D230:
table 0x66DE7C · 1000 / 256; normal speed 60 → 234 ms). So on average a
battle gets 3375 / 40 = **84.4 steps per game tick, whatever the speed**,
but cut into pieces that depend on the frames. Operation 0x94 (handler
0x536BBF) calls method +0x14 of every battle with `[+0x15C]` ≠ 0xFFFF and
then queues operation 0xC8 (0x612C90 if a battle has a human side: the
battle time mode; 0x612D20 if all are automatic: back to normal).

**Consequence for p3vita and the oracle:** to be deterministic they need a
fixed rule, for example «after each tick, if there are battles, operation
0x94 with 84 steps» (what `explore.py --steps 84` does). The game itself is
not deterministic here (it depends on the frame times), so any rule is a
choice; it should be written down in `docs/oracle.md` when it is taken.

### Storms: 0x5053C0 (the «encuentros» of the docs)

0x5053C0, called by 0x506720 every 8 ticks and skipped today by the oracle
(`ENCOUNTERS`) and by p3vita, is not a battle: it is the **storms**. The
ships object holds 16 slots of 12 bytes at `[0x6DD7A0 + 0x20 + 0xC·k]` (u16
ship, u16, u16 x, u16 y, u16 kind 0/1/2; ship 0xFFFF and kind 0xFF when
empty); each storm is a pseudo-ship of owner 0xFF in state **0x15** (11 of
them in save 1). For every ship within 0x24 px in x (list by x, `[+0xA]`,
`[+0xC]`) and 0x24 in y:
- if it has a route that passes near the storm (`[+0x42]` < `[+0xF2]`),
  0x505290 reroutes it around (0x447580 on the navigation object
  *0x6CBDC8, 2752 bytes, not in p3vita) and the sea map is told (0x44F940);
- if it is closer than 28 px (distance² < 0x310): kind 2 takes
  `[+0x14] >> 5` of hull (`[+0x18]`), kind 0 `[+0x14] >> 6`, kind 1
  nothing; to the ship alone or, in a convoy, to each ship of the convoy
  (at least 1 left), `[+0x3C] |= 0x100`; and the convoy follows the storm's
  push (`[+0x28]`, `[+0x2C]`).

Measured (10 days, `--steps 84`): hull lowered by storms in 8 of the 12
saves: save 1, 97 times in 15 ships (83 968 hull); 3, 21 in 3; 4, 7 in 2;
6, 94 in 20; tut1, 6 in 1; tut2, 4 in 1; tut3, 231 in 49 (190 896 hull);
tut4, 12 in 3; none in 2, 5, 7 and tut0. So this is simulation that
changes the world in most saves, and today neither the oracle nor p3vita
does it (they agree with each other, not with the game).

## 2. The call graph: simulation and UI

Static, from the entry points and the methods, stopping at what p3vita
already has, at the known UI (callgraph.py's list) and at calls made with
`ecx` = a UI object (*0x6E51AC town view, *0x6CC7E8 windows, *0x6CBB40,
*0x6CBDD8 sea map, *0x6CBB10 the application, *0x6E6220 port scene...),
plus the window of the battle (0x41D9B0) and the sea map 0x457C90.
«Touch UI» = the function reads a UI object or calls an import (it may be a
small branch of a simulation function). «Ran» = executed in the 30-day runs
of §3 (17 automatic sea battles).

| Part | Functions | Bytes | Touch UI (functions, bytes) | Ran (functions, bytes) |
|-|-|-|-|-|
| Sea battle (0x611CD0, 0x605420, methods of 0x67AD44) | 249 | 148 441 | 50, 72 287 | 48, 55 103 |
| Town battle (0x611D80, 0x5FBFE0, 0x60234C, 0x6130B0, methods of 0x67AAA0) | 41 | 49 600 | 17, 38 700 | 0 |
| Entry points in the ships code (0x50BC40, 0x50B250, 0x50BA80, 0x50B030) | 6 | 4 320 | 2, 3 008 | 2, 1 056 |
| Storms (0x5053C0, 0x505290, 0x447580) | 3 | 4 672 | 2, 1 920 | 3, 4 672 |
| Battle operations (handlers of 0x92–0xA0, 0xB4) | 18 | 6 439 | 1, 1 568 | 0 |
| Save and load (0x6118F0, 0x611950, 0x611AE0) | 3 | 992 | 0 | 0 |
| Timing (0x6127C0, 0x612DB0) | 2 | 672 | 1, 368 | 0 |

Without cutting the UI the closure is 1.06 MB (the «close to 1 MB» of the
inventory): almost all of it hangs from the window of the battle 0x41D9B0
and the widgets under method +0x10 (0x61A880 → 0x4B…–0x4C…).

What ran in the automatic sea battles (48 functions of the sea battle part,
55 KB, plus the entry points): 0x50BC40 (attack), 0x611CD0, 0x603A70,
0x618F10/0x618F90 (construction), **0x605420** (setup, 6.3 KB), 0x619450,
0x619560, 0x61A1D0, 0x61A4E0 (the ships into the scene), **0x606CC0** (the
step), 0x608850, 0x608F70, 0x609D40, 0x609BA0, 0x60ADBF, 0x60BC5A,
0x60BCDA, 0x6113C9, 0x6197F0 (32 indirect calls), 0x6197A0, 0x61F02F,
0x61F1BF, 0x61EFDF, 0x6212FB, 0x62125B, 0x621750, 0x621760, 0x612930
(also called by the pirates' choice of a target, 0x515360), 0x612A20,
0x612BD0, 0x612C90; and at the end 0x611730, 0x611740, 0x6079D0, 0x608230,
0x60D299, 0x60F479, 0x603BC0, **0x50BFD0** (2.6 KB: the result back on the
ships and convoys), 0x60C1C9, 0x611649.

The big parts that did not run are the player's battle (the window
0x41D9B0, method +0x10 → 0x61A880 and the widgets, +0x20 0x60ED39 →
0x62018B), and the town battle.

### The battle operations

None is queued by the automatic sea battles (measured). The town battle
queues several itself, so the oracle would have to execute them:

| Code | Handler | Queued by | What |
|-|-|-|-|
| 0x92 | 0x540F50 | 0x60D259 (town battle start), 0x4942C0 (UI) | per side: method +0x8 |
| 0x93 | 0x541050 | 0x6016CB, 0x608360 (from the town view clicks) | method +0x14 and 0x612D20 |
| 0x94 | 0x536BBF | 0x612DB0 (real-time driver) | **the steps** |
| 0x95 | 0x541200 | 0x608550 | 0x6270C7 |
| 0x96 | 0x60FEC9 | 0x5FFF40, 0x6100D9 (town battle) | a side gives up or joins: `[+0xAC8]`, 0x612C90 |
| 0x97 | 0x541380 | 0x6103E9 (town battle) | |
| 0x98 | 0x5414E0 | 0x610579 (town battle), 0x48D420 (UI) | |
| 0x99 | 0x50B250 | (network) | a chase that catches its target: sea battle |
| 0x9A | 0x5415E0 | 0x610C09 (town battle) | |
| 0x9B | 0x541700 | 0x610FD9 (town battle) | |
| 0x9C–0x9E | — | — | unused (default case 0x537025) |
| 0x9F | 0x611309 | 0x50B250 | `[+0x1E94]` |
| 0xA0 | 0x6116A9 | 0x60D259 (town battle), 0x42D230 (UI) | the ships of one side flee (`[+0x12A]` \|= 2) |
| 0xB4 | 0x542E40 | 0x514920 (town battle destructor), 0x544E50 | pirates (0x513720, 0x513EC0, 0x513D80) |

## 3. Where the simulation starts battles, and how often

From `advance_time` (all in 0x506720, the ships update, except the last):

1. **A pirate convoy reaches the ship it hunts** (convoy state 0x12,
   handler 0x507A65: flag 0x40 of `[+0x14]` and the target in `[+0x50]` of
   its guide, sailing): **0x50BC40** takes both out of the lists, makes a
   convoy of the target if it had none (0x5062F0), puts both convoys in
   state 0x14 and starts a **sea battle** (0x611CD0, 0x605420).
2. **A chase catches its target** (`[+0x3A]`, 0x50B030 → 0x50B250, also
   operation 0x99): sea battle. Chases come from the Hanse pirate hunt
   (event 0x34, phase 9, `diet.cpp`), the player's orders (operation 0x91,
   0x540520) and 0x4DDF40. p3vita already has 0x50B030 except the battle.
3. **Pirates arrive at a town that the Hanse fleet protects** (event 0x24
   and a Hanse convoy in state 9 waiting there): 0x50BA80, sea battle
   against the fleet.
4. **Pirates attack a town**: a pirate convoy that arrives (states 0xF/0x16
   with 0x14, 0x507969: after 3; state 0x12 at the end of its route,
   0x507C4C) or a single pirate ship with `[+0x15C]` (state 0x14, 0x506F33):
   0x535400, then 0x6130B0 (join a running town battle, 0x60234C) or a new
   **town battle** (0x611D80, 0x5FBFE0).
5. **The attack on the pirate nest** (Hanse task type 3, 0x4EAFE4, a ship
   at the nest): town battle at town 0xFE − k (find, join or new).
6. 0x4ECF64 (the mission interpreter) asks the battles 0x4F3940; 0x515360
   (a pirate chooses a target) asks 0x612930 (is the ship in a battle).

Measured with the pirates moving (`explore.py run SAVE 30 --steps 84
--trace`, the 12 saves, 30 days each):

| Save | Sea battles (case 1) | Ops 0x94 | Other cases |
|-|-|-|-|
| 1, 2, 7, tut2, tut4 | 0 | 0 | — |
| 3, 4, 5, 6 | 1 each (days 8, 0, 0, 1) | 28, 29, 41, 45 | — |
| tut0 | 7 | 235 | — |
| tut1 | 5 | 173 | — |
| tut3 | 3 | 86 | — |

17 sea battles in 360 save-days, all of case 1 (a pirate convoy of 1–2
ships against one ship of an AI merchant); each lasted 20–45 ticks (22–45
operations 0x94 of 84 steps); none queued operations; **the emulation never
stopped**. No town attack, chase, Hanse fleet battle or nest attack in 30
days. What the battles did (the ships, convoys and owners before 0x605420
and at the destructor): the pirates took cargo from the victim (save 3:
1000 units of one ware, `[+0x74]` and `[+0x118]` from ship 124 to ship 96,
and the victim lost one sailor, `[+0x40]` 10 → 9); in most of the others
only the shown speed `[+0x30]` and the flags `[+0x3C]` (0x80 on the pirate
guide) changed. No ship was sunk or taken.

Pirates in the saves (`explore.py census`): owner 0xFF; 2–12 pirate ships in
state 0x12 per save; 1–3 pirate convoys in state 0x12; in 6, 7 and tut4 a
pirate convoy left in state 0x14 (outside the list).

### The town battle needs the town view

Retouched state `explore.py town 3 2 --town 5` (the first pirate convoy of
save 3 attacks town 5 as in 0x507C4C): **the emulation stops in 0x5FBFE0**,
at 0x58D030 reading `[*0x6E51AC + 0xC31C]` (the town view is 0 in the
emulator). 0x58D030 is a method of the town view: it turns a point into a
tile of the town map using the view's size (`[+0xC31C]`, `[+0xC320]`,
`[+0x510]`, `[+0x514]`) and its hit mask (`[+0xC6D8]`, stride `[+0xC6E8]`,
0x58B740). The town battle calls it from method +0x2C (0x600060) and from
0x600770 and 0x603130. So the town battle uses the geometry of the drawn
town (it is the town of the view, *0x6E51AC, and not necessarily the one
attacked: to check).

## For the roadmap

(To be completed with the plan, §5.)

## For the inventory

(To be completed.)

## Game bugs

(None yet.)

## Done and left

Done: the call graph by parts, the battle classes and methods, the battle
operations, how a battle is stepped (operation 0x94 from the real-time
driver), the storms, the entry points, the frequency in 30 days of the 12
saves with the pirates moving, the town battle stopping on the town view.

Left in phase 1: what the oracle needs (stubs, jumps), the plan in pieces.
