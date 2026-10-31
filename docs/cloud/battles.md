# Battles and encounters (cloud/battles)

Task `tasks/battles.md` of p3vita-datos: the study of the battle subsystem
of Patrician3.exe (inventory part E, roadmap «Piratas, persecuciones,
encuentros y batallas») and its pieces, in order.

## Summary

- **Phase 1, the study (done):** the two battle classes (sea battle 0x1E98
  bytes, town battle 0x2F24, up to 255 in 0x6E55D0, saved in the savegame),
  what is simulation and what is UI (without the UI: 249 functions / 148 KB
  for the sea battle, 41 / 50 KB for the town battle, not 1 MB), where
  `advance_time` starts battles and how often (17 automatic sea battles in 30
  days of the 12 saves with the pirates moving, all of a pirate convoy against
  an AI ship; the emulator runs them to the end), how they are stepped
  (operation 0x94 from the real-time driver, not `advance_time`), that
  0x5053C0 is the storms, what the oracle needs and a plan in 7 pieces (§1–§5).
- **Piece 1, the storms (done, merged into `main` at 1c4fbfd and on by
  default since 61cdbfa):** 0x5053C0, 0x505290 and 0x447580 in
  `src/p3data/battle/storms.cpp`; call by call 0 differences in the 12 saves
  (10 days) and in retouched states covering every reachable block; the
  world with the storms on, 0 differences (§6).
- **Piece 2, the pirates moving (in progress):** measured what runs (§7):
  only the convoy state 0x12, 8 functions not in p3vita (about 1 400
  instructions) and the per-town precompute 0x448780 that the oracle skipped
  and the pirates need (2.8 s in the emulator for the 24 towns).
- **Piece 3, the automatic sea battle:** not started.

## Decisions for the user

- **The battle clock (decided):** one operation 0x94 with 84 steps after
  each tick in which there are battles (`[0x6E59D4]` ≠ 0). For
  `docs/oracle.md`: «Las batallas avanzan con la operación 0x94, que en el
  juego encola el bucle de tiempo real (0x546620 → 0x612DB0) con los pasos
  del reloj; p3vita y el oráculo la ejecutan, con 84 pasos, al final de cada
  tick en el que haya batallas, tras las demás operaciones encoladas.» (The
  result of a battle doesn't depend on how the steps are cut, §3.)
- **Piece 2 changes the oracle's load** (recommendation: accept): with the
  pirates on, the per-town precompute 0x448780 runs at the load (today it is
  skipped, `SKIPPED_WITH_NAVIGATION`), because the pirates choose their
  waypoints from it ([+0x20], [+0x24] of the navigation object). It adds
  2.8 s per load and p3vita must compute the same lists.
- **The rule for piece 2 alone** (recommendation: only as a step, until piece
  3): while battles don't exist, a pirate that would attack (0x507BE9 →
  0x50BC40) goes on as if it had no target, and a pirate convoy that arrives
  at a town (0x507C0B → town battle) gets a new course. Both p3vita and the
  oracle (`--pirates`) do it; it disappears with pieces 3 and 6.
- The two possible game bugs of «Game bugs» (the battle clock, the town
  battle reading the town view): to confirm before fixing.

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

The town battle also has its own map of 64 × 64 bytes at `[+0x1EE0]`
(0x60114B: 1 = blocked outside it), and queues the battle operations of §2.

### The steps don't change the result, only when it ends

Save 5, its battle of day 3, with 84 steps per tick in one operation, in 6
of 14, and with 42 steps per tick in 3 of 14: the same ships, cargo, crew
and owners at the end in the three; it ends at tick +0x28, +0x27 and +0x4F.
So for the automatic battles the cut into operations doesn't matter (the
part of 0x606CC0 before the loop of steps is the sea map and the window,
only for human sides), only the number of steps per tick.

### Other things seen

- The Hanse pirate hunt (scenario `dieta` of `retouch.py`, saves 1, 3, 5 and
  7, 24 days, pirates moving): the fleet (state 0x16) chases a pirate
  (`[+0x3A]`: 524 calls of 0x50B030 in save 3, 137 in save 5) but doesn't
  catch it in 24 days; no battle of case 2 or 3.
- The pirate attack of event 0x1E (0x5063C0, «por reescribir» in
  `events_rare.cpp`) does not start a battle: it makes pirates (0x513D80,
  0x5062F0) that hunt a ship of a human player near the town, so it ends in
  case 1 with a human side. 251 instructions; most of what it calls is in
  p3vita (0x507D60, 0x5097C0, 0x51A750, 0x52A8D0, and named 0x5062F0,
  0x50CE50, 0x513D80, 0x5182B0, 0x515EA0).
- A battle with a human side (`[+0xAC8]` = 0) is the player's: messages
  (0x548CA0, quiet in the oracle), operation 0xC8 (the battle time mode,
  0x612C90) and, in the next step, the window of the battle (0x41D9B0 on
  *0x6CBB10, which is 0 in the emulator: it would stop there). In network
  games (`[0x6DFC18]` ≠ 0) a battle between two human players is made
  automatic (0x605673).

## 4. What the oracle needs

For each part, what `explore.py` had to do (or would have to do) on top of
`town_oracle.py`:

| Part | To run it | To record and compare it |
|-|-|-|
| Storms | stop jumping over 0x5053C0 (`ENCOUNTERS`); nothing else: 0x44F940 is already quiet, 0x447580 runs (navigation object) | ships, convoys and routes: regions `P`, `V`, `Q` (there already) |
| Pirates moving | let pirate convoys through `CONVOY_DISPATCH` and every state through `SAILING_DISPATCH` (0x11, 0x12, 0x0A, 0x0B, 0x16); docked states of the pirates (to look at: 0x0A, 0x0C, 0x11 are skipped today) | the same; `Z0..Z4` (pirates) |
| Automatic sea battle | the above, and **after each tick, operation 0x94 with N steps** (a rule shared with p3vita, §1) when `[0x6E59D4]` ≠ 0; nothing stops (no stub needed in 360 save-days) | new regions: the battle slots (0x6E55D0, 0x408 bytes, already inside `G`), each battle object (0x1E98 or 0x2F24 bytes, `*[0x6E55D0 + 4·k]`) and its arrays: `[+0x4]` (u32 per ship → an entry of the scene), `[+0x8]`, `[+0xC]`, `[+0xA74]`, `[+0xA78]` (u16 per ship) and the scene's lists (0x618F10). Pointers inside must be turned into indices like `Q` does for routes. **That is Recorder/regions: oracle-speed's part**; here only the list of what is needed |
| Battles with a human side | a rule: either the player's side fights as AI (setting `[+0x5D]`/`[+0x5E]` and `[+0xAC8]` to 1 after 0x605420, as the network case does) or jump over the window (0x606FEA..0x607011 and the same in 0x5FD510) | — |
| Town battle | the town view: a stub of 0x58D030 (and 0x58B740) with the geometry of the town (`[+0x510]`, `[+0x514]`, `[+0xC314..+0xC320]`, the hit mask `[+0xC6D8]`), or a town view built by the game's code (0x5871xx–0x589Exx set those fields when a town is loaded); execute the battle operations 0x92, 0x95–0x98, 0x9A, 0x9B, 0xA0, 0xB4 (`--operaciones`) | the town battle object (0x2F24) and its map `[+0x1EE0]` |

`explore.py` does all of this from a subclass, so `town_oracle.py` was not
changed. When it goes into the oracle it should be an option (`--battles`,
with `--battle-steps N`), off by default, so `world.sh` gives the same
results until p3vita does the same.

## 5. The plan, in pieces

Each piece behind a setting of p3vita (off by default, like the oracle's
option) until both are switched on together on the PC. Sizes: functions and
instructions not yet in p3vita (static, from `graph.py`, and what ran).

1. **Storms (0x5053C0)**: 0x5053C0 (493 instructions), 0x505290 (101) and
   the reroute of the navigation 0x447580 (906). In every save (8 of 12
   lose hull in 10 days). Check: probe `5053c0` call by call
   (`scripts/tick-probe.sh 5053c0 3 1 6 tut3` with the oracle's new option),
   then a retouched storm next to a convoy (kind 0 and 2) and a ship with a
   route through it; at the end, the world in 4 saves with the option.
2. **Pirates moving, without battles**: the convoy state 0x12 (0x507A65:
   0x50D9E0 202 instructions, 0x515360 313 + 1.3 KB below, 0x505C90 443 +
   2.8 KB below, 0x50CFB0 57), the sea states 0x11 and 0x12 (0x507022,
   0x507099), 0x517170, the pirate branch of 0x5017F0 (a captain) and the
   attack of event 0x1E (0x5063C0, 251). About 2 500 instructions with what
   hangs below them. While the
   battles are not there, the rule for both: when a battle would start
   (0x50BC40, 0x50B250, 0x50BA80, 0x611D80) the pirates stop (the oracle
   jumps over it). Check: probes of the new functions in the 7 saves with
   pirate attacks (3, 4, 5, 6, tut0, tut1, tut3), a few days; the world in
   4 saves.
3. **The automatic sea battle**, case 1 (what happens in the saves):
   - 3a start and end: 0x50BC40 (255), 0x611CD0, 0x603A70, 0x618F10,
     0x618F90 (334), **0x605420 (1 650)**, 0x619450, 0x619560, 0x61A1D0,
     0x61A4E0; the end 0x611740, 0x603BC0 (395), **0x50BFD0 (697)**,
     0x6079D0 (632), 0x608230, 0x60D299 (1 826, partly window), 0x60F479
     (769), 0x60C1C9, 0x611649. About 7 500 instructions. p3vita needs a
     `SaveGame::battles` (the slots and the objects as game memory, like the
     ships) and tick_probe must compare them.
   - 3b the step (operation 0x94, method +0x14): 0x606CC0 (848), 0x608850
     (581), 0x608F70 (983), 0x609D40 (752), 0x609BA0, 0x60ADBF (428),
     0x60BC5A, 0x60BCDA (398), 0x6113C9, 0x6197F0 (771), 0x6197A0, 0x61F02F,
     0x61F1BF (580), 0x61EFDF, 0x6212FB, 0x62125B, 0x621750, 0x621760,
     0x612930, 0x612A20, 0x612BD0. About 7 000 instructions; 32 indirect
     calls in 0x6197F0 to resolve (methods of the scene's entries).
   - Check: probes of 0x50BC40 and of the operation 0x94 (per call, with
     the battle regions) in the 7 saves with battles; retouched: a pirate
     convoy next to an AI ship with flag 0x40 and the target in `[+0x50]`;
     a victim in a convoy; two pirate ships against one; a victim with no
     cargo. The world in 4 saves at the end.
4. **The other sea battles**: the chase that catches (0x50B250, 592
   instructions; also operation 0x99) and the Hanse fleet at a protected
   town (0x50BA80, 137). Retouched: the `dieta` mission 0x34 in phase 9 with
   the pirate within 0x190 of the fleet; event 0x24 with the fleet in state
   9 and pirates arriving.
5. **Battles in the savegame**: 0x611AE0 (read), 0x611950 (write), the
   methods +0x10 (0x604600, 1 131, load), +0x18 (0x608230) and +0xC
   (0x604220, 355, save). Check: save a game with a battle running from the
   emulator and load it in p3vita.
6. **The town battle** (pirates against a town and the pirate nest): 41
   functions, 50 KB (0x5FBFE0 1 468, 0x621770 2 896, 0x5FE7E0 1 196,
   0x624950 1 173, 0x5FD510 765, 0x600770 718, 0x5FE080, 0x601C7B,
   0x600060...), the battle operations it queues (0x92, 0x95–0x98, 0x9A,
   0x9B, 0xA0, 0xB4: 18 functions, 6.4 KB), the town view geometry (0x58D030,
   0x58B740) and what hangs from it (the nest: 0x515C00, the burnt buildings
   of events 0x25/0x26, the Hanse task type 3 attack in `alderman.cpp`).
   First the oracle stub of the town view (§4); check with `explore.py town`
   and retouched nests.
7. **The player's battle**: the window (0x41D9B0), method +0x10 and
   0x61A880 with the widgets (0x4B…–0x4C…), the orders (operations 0x92,
   0x93, 0x98, 0xA0 from the UI). With the UI phase. Until then, the rule of
   §4 for human sides.

## 6. Phase 2: the storms (piece 1)

Rewritten in `src/p3data/battle/storms.cpp` (members of `Sailing`):
0x5053C0 (`fn_5053c0`), 0x505290 (`fn_505290`) and the reroute of the
navigation 0x447580 (`fn_447580`, a free function on `NavData`). Off by
default: `Sailing::set_storms(true)` makes `update_ships` call it every 8
ticks after the convoys (only with the convoys on, as in the oracle).

The oracle: `town_oracle.py --storms` (with `--barcos`) stops jumping over
0x5053C0 and jumps over its two writes to the sea map (`STORM_UI_JUMPS`,
0x50596F → 0x505983 and 0x5059CA → 0x5059E0: `*0x6CBDD8 + 0x2B22`, which is 0
in the emulator). tick_probe: `--storms` (for `--continuo`) and the probes of
`tools/oracle/probes_battle.cpp` (0x5053C0, 0x505290, 0x447580).

Checked, call by call (`town_oracle.py ... 10 5053c0,505290,447580 --barcos
/tmp/datos --cpr ... --storms`, then `tick_probe --cpr`): **0 differences in
the 12 saves, 10 days each**: 3840 calls of 0x5053C0, 350 of 0x505290, 48 of
0x447580 (1 min 9 s, four saves at a time).

Blocks that the saves don't reach (`re/battles/blocks.py`, 10 days in the
12): in 0x5053C0 a convoy hit by a storm of kind 2 (both walks), the minimum
of 1 for a convoy's ships, the push back with only a vertical course, the
countdown of the sea map `[+0xA]` (always 0 in the saves) and the UI at the
end; in 0x505290 the player's ship (UI, not reproduced); in 0x447580 the
plain insertion of the ship's position (the two pairs of corners touch, or
both ways round have land), the case of the way down being empty, and the
paths after land on the second way.

Retouched states for them: `tools/oracle/retouch_storms.py` (new; it moves
the existing storms next to chosen ships and sets kind, strength and
countdown, then records 5053c0,505290,447580 one day; `--blocks` prints the
blocks that ran). Scenarios `convoys` (kinds 2 and 0 on convoys at sea, a ship
with 2 of hull), `countdown` (the countdown at 6, 3, and two storms that end
with it at 2 and 10), `vertical` (a course with `[+0x28]` = 0), `reroute`
(a storm at 8 places round ships with a captain and a route) and `coast` (the
same near a town). Run in saves 1, 3 and 5 (all), and `coast` also in 2, 4,
6, 7, tut0–tut4, `reroute` in 2, 4 and tut1: **0 differences in all** (each
run 1 day, 30 s for a batch of 10). With them every block of the three
functions has run except the player's ship in 0x505290 (0x50539A: a call
to the UI, not reproduced) and blocks that can't be reached: the sign fix-up
of `(k + 8) & 7` (k + 8 is never negative) and the empty ways round
(0x447888 and the `b > d`, `a < c` exits: after `c −= 8` and `d += 8` they
can't happen).

The world with the storms on in both (`town_oracle.py ... --storms` with the
functions of `world.sh`, then `tick_probe --continuo --carga ... --storms`;
the script is `world.sh` with the two options added): **0 «distintas» in
saves 1, 3, 5 and 7, 7 days** (570 s); «saltadas» 1, 0, 5 and 4, the same as
without the storms (calls to 0x535760 from inside the game). The default
(storms off) is the code of `main` by construction: `storms_` is false and the
oracle installs the same jump as before; `world.sh` itself was not rerun.

Since 61cdbfa (`main`) the storms are on by default in p3vita, tick_probe
and the oracle (`--no-storms` turns them off).

## 7. Piece 2: the pirates moving (in progress)

What runs when the pirates move and no battle starts (`re/battles/pirates.py`
SAVE DAYS, without recording, against `--off`, the oracle of today; the 12
saves, 10 days, 3 min): the pirate convoys are only ever in **state 0x12**
(20 619 passes); no pirate ship alone at sea or in port. New functions entered
(none in p3vita): 0x505C90, 0x50D9E0, 0x50E340, 0x514B80, 0x515360, 0x515DA0,
0x612930, 0x621750 (an empty `ret`), and 0x50BC40, the start of a battle.

The handler of state 0x12 (0x507A65, in 0x506720; `c` the convoy, `G` its
guide):
1. `G` back in the list by x (0x50CE50) if it is in none; the wait `c[+0x16]`
   counts down into `G[+0x138]`.
2. If `c[+0x14] & 0x40` (the target was set by 0x50BA80 / the attack of event
   0x1E): clear it; if `G[+0x50]` is a merchant's ship sailing (0x0F/0x10) →
   step 4. Otherwise, every 4 ticks, 0x50D9E0 (choose a target) and 0x515360
   (worth attacking?) → step 4.
3. No target: without a route, 0x505C90 (a new course; it may disband the
   convoy at its nest: the walk goes on from the same link); then the convoy
   sails (0x502110 with 1) and is sorted by x (0x50D0C0); when its route ends
   (0x502110 false) it leaves the list and attacks the town `c[+0x1C]` (town
   battle: 0x611D80, 0x5FBFE0, piece 6).
4. Attack: if `G[+0x50]` still sails (0x0F), the convoy leaves the list and
   0x50BC40 starts the sea battle (piece 3).

The functions:
- **0x50D9E0** (thiscall on the ships object, the guide; 202 instructions):
  keeps `[+0x50]` if it is sailing (0x0F/0x10), closer than `[0x6DD7A0 +
  0xFC]` (distance²) and outside the shallows (table 0x673564, the three
  boxes of `sailing.cpp`); otherwise looks for the nearest ship by the list
  by x within `[+0xFE]` in x (both ways), not a storm (0x15), closer than
  `[+0xFA]`, outside the shallows. No owner check here.
- **0x515360** (cdecl, the guide; 313): whether to attack `T = [+0x50]`
  (clears `[+0x50]` when not): not its own ([+0x15D]), a merchant's, sailing;
  an AI merchant without flag 4 in `[+0x8]` only if the pirate doesn't wait
  (`[+0x138]` = 0); a human only if `[0x39C + home town]` of the merchant +
  difficulty ≥ 2 and the wait ≤ 0x900. A privateer (`[+0x15D]` a merchant)
  always. Otherwise compares the convoys: the pirates' best battle speed
  (0x612930) and the target's worst, `19 · target ≤ 20 · pirates`; the crews
  and arms (`[+0x40]`, `[+0x120]`) `7 · target ≤ 10 · pirates`; the target
  carries cargo and a pirate has 2000 free; and the target is not in the
  shallows (0x50E340). Within 20 px (0x190) it stops (0x516840) and returns
  1; else it heads for it (route cleared, `[+0x52]` = the target) unless its
  route already ends next to it.
- **0x612930** (on 0x6E55D0, a ship; 83): the battle speed of a ship:
  `(0x1000 − 614 · cargo / capacity) · table 0x67ADC0[type] >> 12` (table
  0x2B5, 0x2B5, 0x242, 0x242) times the hull factor `clamp(179 · hull / max +
  0x71, 0xA6, 0x100) >> 10`, times `(captain's [+0x9] + 2550) / 2550` with a
  captain; 1 without capacity or hull.
- **0x50E340** (2 arguments): whether a point is in the shallows.
- **0x505C90** (on the ships object, &link; 443, plus 0x514B80 129,
  0x515DA0 102, 0x5159C0 72, 0x513BF0 144 and the generator 0x46C320..380):
  a new course for a convoy without a route: if a ship is weak (crew under
  the table 0x673660, hull under 80 %, arms under 2 · `[0x672CC8]`) or loaded,
  it goes home (the nest of its pirate, 0x51A470 → the town table 0x6DDBB0);
  at the nest it is disbanded (0x514B80, which queues an operation). Else it
  follows `G[+0x52]` (a ship it saw) or goes to a random waypoint of the
  per-town lists `[nav+0x20]` (counts) and `[nav+0x24]` (points) with the
  generator seeded with the tick.
- **0x448780** (on the navigation object, a town; about 460 instructions,
  0x448450 for each cell, 0x445010 the route): **the per-town list of
  waypoints** that 0x4482C0 builds at the load (and 0x448040 for a new town):
  the water cells (every 16 of the grid) whose route from the town is between
  two lengths, widening the ring until there are 10. The oracle skips it
  (`SKIPPED_WITH_NAVIGATION`, «las rutas no lo usan»): with it the load
  takes 2.8 s more for the 24 towns (save 3: 10 to 29 points per town). p3vita
  doesn't have it either.

Still to do for piece 2: rewrite these (≈ 1 900 instructions with 0x448780),
`--pirates` in the oracle (pirate convoys through `CONVOY_DISPATCH`,
0x448780 at the load, the two jumps of the rule of «Decisions»), the same in
p3vita (`Sailing::set_pirates`), probes and retouched states.

## Changes to shared files

None of the files reserved for the local session (operations.cpp,
`run_operation`, events*.cpp, internal.hpp, `OPERATIONS` and retouch.py) and
none of the shared docs. Nothing of Recorder, the regions or `write_diff`.

- `tools/oracle/town_oracle.py` (16 lines): the option `--storms` and the
  constructor argument `storms=False`; with it, instead of the jump over
  0x5053C0 (`ENCOUNTERS`), the two jumps of `STORM_UI_JUMPS`. The diff:

  ```diff
  -       [--sin-comerciantes] [--resto PIEZAS | --sin-resto] [--operaciones CÓDIGOS]]
  +       [--sin-comerciantes] [--resto PIEZAS | --sin-resto] [--operaciones CÓDIGOS] [--storms]]
  +# With --storms, 0x5053C0 (the storms) runs; its two writes to the sea map
  +# (*0x6CBDD8 + 0x2B22, UI) are jumped over.
  +STORM_UI_JUMPS = {0x50596F: 0x505983, 0x5059CA: 0x5059E0}
  -                 operations=OPERATIONS):
  +                 operations=OPERATIONS, storms=False):
  -                self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=ENCOUNTERS, end=ENCOUNTERS, user_data=EPILOGUE)
  +                if storms:
  +                    for address, target in STORM_UI_JUMPS.items():
  +                        self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=address, end=address, user_data=target)
  +                else:
  +                    self.uc.hook_add(UC_HOOK_CODE, self._jump, begin=ENCOUNTERS, end=ENCOUNTERS, user_data=EPILOGUE)
  +    p.add_argument('--storms', action='store_true', help='with --barcos, the storms too (0x5053C0, every 8 ticks)')
  -                   not a.sin_comerciantes, rest, a.cpr, [int(x, 16) for x in a.operaciones.split(',') if x])
  +                   not a.sin_comerciantes, rest, a.cpr, [int(x, 16) for x in a.operaciones.split(',') if x], a.storms)
  ```
- `src/p3data/sailing.hpp`: `set_storms`, `storms_` (false), the
  declarations of `fn_5053c0`, `fn_505290` and `fn_447580`.
- `src/p3data/sailing.cpp` (`update_ships`): after `update_convoys()`,
  `if (storms_ && (save_.tick() & 7) == 0) fn_5053c0();` and a comment.
- `tools/oracle/tick_probe.cpp`: the option `--storms` (→ `set_storms`) and
  `register_probes_battle`; `tools/oracle/probe_registry.hpp`: its
  declaration; the two `CMakeLists.txt`: the new files.

## For the roadmap

- «Piratas, persecuciones, encuentros y batallas» → split into the 7 pieces
  of §5 (storms, pirates moving, automatic sea battle, chases and the Hanse
  fleet, battles in the savegame, town battle, the player's battle).
- New: «Las tormentas (0x5053C0)»: rewritten in this branch behind a
  setting (`Sailing::set_storms`, `town_oracle.py --storms`, `tick_probe
  --storms`), off by default; checked call by call in the 12 saves and with
  retouched states (§6). To switch on together in p3vita and in the oracle
  when merging (then `world.sh` with the storms). They lower the hull of
  ships in 8 of the 12 saves in 10 days.
- The rule for stepping the battles (operation 0x94 with N steps after each
  tick) is a decision to take and write in `docs/oracle.md`.

## For the inventory

Part E, «Batallas y encuentros»: replace the line with
- 0x5053C0 is the storms, not encounters (§1); simulation, every 8 ticks.
  Rewritten (§6) with 0x505290 and 0x447580, behind a setting.
- The battle closure without the UI is 249 + 41 functions (148 KB sea, 50 KB
  town), not 1 MB; 48 functions (55 KB) run in the automatic battles of the
  saves (§2, `re/battles/graph.py components`).
- The battles are stepped by operation 0x94, queued by the real-time driver
  (0x546620 → 0x612DB0), not by advance_time; 0x6127C0 (called from
  advance_time) is only timers and UI (callgraph.py prunes it: right).
- Operations 0x92–0xA0 and 0xB4: §2 (0x9C–0x9E are unused).
- Part D: «el ataque al barco 0x5063C0» goes to piece 2 (pirates moving);
  «el ataque a la guarida pirata» (alderman.cpp) and 0x515C00
  (`events_rare.cpp`) to piece 6.

## Game bugs

Certain:
- None yet.

Looks like one (to confirm):
- **The battle clock (0x612DB0)** moves its time `[+0xC44]` by 40 ms per
  *step* (`now − elapsed + 40 · steps`, with steps already multiplied by
  3375 / `[0x6DFBC4]` ≈ 14 at normal speed) instead of 40 ms per 40 ms
  consumed. After each operation 0x94 the next frame finds the time in the
  future, clamps it to 0 and resets `[+0xC44]` to now. So the battles run at
  about half the intended 84 steps per tick (about 50 at 60 frames per
  second, 40 at 25) and their speed depends on the frame rate. It doesn't
  change a battle's result (see «The steps don't change the result»), only
  how long it lasts.
- **The town battle reads the town view** (*0x6E51AC, 0x58D030) for its
  geometry, which is the town the player is looking at, not necessarily the
  one attacked. To check what the game does when they differ.

## Done and left

Done (phase 1): the call graph by parts, the battle classes and methods,
the battle operations, how a battle is stepped (operation 0x94 from the
real-time driver), the storms, the entry points, the frequency in 30 days
of the 12 saves with the pirates moving, the town battle stopping on the
town view, the dieta scenario with pirates, what the oracle needs and the
plan.

Checks run (none changed p3vita's code, so no world.sh): `build.sh` with
ctest 7 of 7 before each push; `explore.py` runs without recording: 10 days
in the 12 saves (47 s, four at a time), 30 days in the 12 (2.5 min), the
dieta scenario 24 days in 4 saves (36 s), the step comparison (3 runs of 4
days, 20 s), the town battle (2 s, stops).

Not built for the Vita (phase 1 changed no code).

Phase 2 so far: §6.

Phase 2 is done for piece 1 (the storms), behind its setting. I chose the
storms as the first piece instead of the automatic sea battle: they are
small, need nothing else and act in 8 of the 12 saves, while the sea battle
needs the pirates moving (piece 2) and new regions in the oracle's recording
for the battle objects (Recorder/regions: oracle-speed's part).

Left: pieces 2 to 7 of §5. On the PC, when merging: `world.sh 7` in the 12
saves as usual (storms off), and the same with `--storms` in the oracle and
in tick_probe before switching them on by default.

Time of the checks of phase 2: the 12 saves × 10 days of probes 1 min 9 s;
the retouched states about 30 s per batch of 10 (5 batches); the world with
the storms 570 s; `build.sh` 20 s each time. Not built for the Vita (no
VitaSDK here, see the README); the change is in p3data (`Sailing`), which
the viewer links, but no API it uses changed.
