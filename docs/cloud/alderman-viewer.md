# Cloud task: the path to Alderman in the viewer

Branch `cloud/alderman-viewer`, from `main` at 61cdbfa. Screenshots (game
graphics): `screenshots/alderman-viewer/` in p3vita-datos, branch
`cloud/alderman-viewer`.

## Summary

The viewer now has the player's side of the path to Alderman. Square in a
town opens the **Hanse panel**:
- the Alderman and the date of the next election;
- whether the player is a candidate, or what is missing (being a mayor,
  the «reputation to stand» out of 120);
- the Hanse tasks on offer with their deadline, and taking one (operation
  **0x4B**);
- the task taken, its deadline and progress;
- for a road, in either of its two towns, the materials still needed and
  available, given with operation **0x3B**, with the game's amounts 1, 5, 50
  and all;
- a notice with the result of each election («¡Has sido elegido Gobernador
  de la Hansa!» if the player wins).

The panel works with buttons, keyboard and touch, like the market. Both
operations are built byte for byte as the game's windows build them (study
below: 0x5E87D0 and 0x5F2A20). `--debug-state` puts the player in the states
the saves never reach (mayor, candidate, materials, level4, alderman).

Checks:
- **Oracle**: `tools/oracle/retouch_player.py`, 8 scenarios × saves 1 and 3,
  2 days, recording 535760 and 4d85c0: **0 differences** in every one
  (535760: 25 to 39 equal calls per run, 4d85c0: 512). They queue the bytes
  the viewer builds (`player_ops`) and choose them with the game's own code,
  as its windows do:
  - taking a task of each type (found, road, pirate ship, grow);
  - a second task in the same tick (refused);
  - a player who isn't a candidate (refused);
  - giving each road material with each amount, clamped to what is
    available and to what is still needed.
- **Tests**: ctest 8 of 8 (new `hanse_tasks_test`: the operations' bytes and
  the window's amount).
- **Vita**: the VPK builds with no warnings (1,194,088 bytes).
- **Screenshots**: 15, checked by looking at them (p3vita-datos,
  `screenshots/alderman-viewer/`).

The game's UI texts are not in the CPR (they are in `Patrician3.exe`), so
the panel's texts are the viewer's own, in Spanish like the rest of the
viewer. The names (towns, people, ships) come from the CPR and the save.

## Decisions for the user

1. **The game's texts** (part 3 of the task). They are strings in
   `Patrician3.exe`, in the language of each installation, reached through
   pointer tables (see «Where the game's texts are»); the CPR only has the
   names. Options:
   - read them from the executable: the Vita would also need
     `Patrician3.exe` in `ux0:data/p3vita/`, plus the addresses of the
     tables in the Spanish executable, which isn't in p3vita-datos;
   - keep the viewer's own strings, as now.

   **Recommendation**: add the Spanish GOG `Patrician3.exe` to p3vita-datos
   (`game/` or a `game-spa/`). A later task can then find its text tables and
   write a loader that falls back to the viewer's strings when the
   executable isn't there.
2. **The election**. With `--debug-state level4` the player (mayor, three
   tasks done, «reputation to stand» about 470, home town with the 0x40
   flag) loses the election of 7 September 1300 to the citizen of Rostock.
   It may be right (the votes of the towns), but nothing here checks it.
   **Recommendation**: an oracle scenario with the player at level 4 over
   the election day (event 0x20), in `retouch_player.py` or for the session
   that owns the election code.
3. **What comes next in the viewer** (not done):
   - the player's vote in the election («Choose a candidate and cast your
     vote.»);
   - the candidature question («Accept candidature?», text 95 of 0x69EE38):
     how the game asks and what it queues hasn't been studied;
   - the list of candidates;
   - the `eldermann` video and the end window.

   **Recommendation**: study the vote first. It is the last step the player
   takes on the path to Alderman.
4. Minor: the town bar has no room for «Triángulo: mapa» once «Cuadrado:
   Hansa» is there; it shows «X: mercado   Cuadrado: Hansa». **Recommendation**:
   leave it; the sea map's own bar says how to go back.

## 1. Study: how the game's UI does it

All addresses are of the English GOG 1.1 `Patrician3.exe`. The player is the
global [0x6DFC14]; the operation queue is 0x54AA70 on ecx = 0x6DF2F0; an
operation is 0x14 bytes, which 0x535740 fills with u32 0xFFFFFFFF and then
zeros before the UI writes it.

### The Hanse tasks window (0x5E87D0, the window's update)

- The list: 0x4EC5F4(5, out) walks the event queue from its head and returns
  up to 5 indices of events 0x32 **without merchant** ([+0xD] ≥ number of
  merchants) and **type < 5**, in queue order (the rest 0xFFFF). The window
  keeps them in [+0x1946] and their number in [+0x1966]; the selected row is
  [+0x1907] (0xFF = none).
- Taking a task (the button at [+0x17C], with a row selected): **operation
  0x4B** = `u32 0x4B, u32 player ([0x6DFC14]), u32 event index (the u16 of
  the list), u32 0, u32 0` (0x5E8A3D..0x5E8A5A → 0x5E8E6C). The executor
  (0x53B440, `fn_53b440` in p3vita) checks the rest (candidate, without a
  task, the task still free and valid) and starts it (0x4EBF54).
- The same window queues operations **0x4A** with sub-codes (6, 2, 1...) on
  the selected task from other buttons ([+0xC5C], [+0xD44]...); they look
  like the developers' tools (they change the task's state) and aren't part
  of this task.
- The window also reads the task of a merchant with 0x4EC694 (the first
  event 0x32 with that merchant), as the road window does.

### The road window (0x5F2A20, its update; built by 0x5EED40, town 0x5EF0AD)

Shown in a town ([+0x15E8] = the town the game screen is in) when the
player's task (0x4EC694) is a road (event type 1, [+0xC] = 1); otherwise it
closes (0x5EFD30).

- 0x4EC704(event, out[12 u16]) — only for a road whose merchant is the
  player: finds the pair of 0x672BF8 with the two towns and its number of
  sections n (byte +2), and writes, per material k (wood, bricks, tools,
  hemp; wares 0x672BF4 = 0x0B, 0x13, 0x0C, 0x11): **total** = per-section
  amount (0x672BF0: 3, 4, 1, 2) × n; **done** = per-section amount × (n −
  sections left [+0x12]); **delivered** = the event's 10-bit counter
  ([+0x14 + k] and two bits of [+0x13]).
- The window shows per material **still needed** = total − done −
  delivered ([+0x15EA + 2k]) and **available** ([+0x15FC + 4k]) = the
  player's office in the town (0x5308A0) plus the player's ships docked in
  it ([+0x39] = town, without bit 1 of [+0x136]), in loads or barrels
  (0x672C14: barrels of 200, loads of 2000). The progress is done·100/total
  of the wood ([+0x160C], %).
- The amount: 4 option buttons with **1, 5, 50 and all** ([+0x1620..0x162C]
  = 1, 5, 0x32, 0xFFFFFFFF, set by 0x5EED40); the chosen one in [+0x161C].
- A material's button is enabled when something is still needed and
  something is available. Pressing it: **operation 0x3B** = `u32 0x3B, u16
  event index, u16 amount, u8 town, u8 player, u8 the material's ware
  (0x672BF4[k], not k), then zeros` (0x5F2D9A..0x5F2E0A), with amount =
  min(chosen, available), but the still-needed value if that is smaller
  (all compared as unsigned). The executor (0x53A3E0, `fn_53a3e0`) takes it
  from the office first and then from the docked ships.

### The candidacy and the election

- The candidacy has no button: 0x4F75F0 makes the mayor of a town a
  candidate ([+0x37] = 1) when the «reputation to stand» reaches 120
  (0x6737B8): twice that of town 0 minus its three components, plus the
  last component of every town and the other two of the towns with
  [0x2C8] & 0xC0 (`merchants.cpp`). The game then asks «Accept
  candidature?» (text table 0x69EE38, entry 95).
- [+0x37]: 0 not a candidate; 1–3 candidate with 0–2 tasks done; 4 can
  stand in the election (event 0x20, 0x529700) if the home town has
  [0x2C8] & 0x40. The Alderman is the global `kAlderman`.

### Where the game's texts are

Not in the CPR: the UI texts are strings inside `Patrician3.exe` (`.data`),
in the language of each installation, reached through tables of pointers
(for example 0x69EE38: 96 texts of the election and the Hanse day: 39
«Election of Alderman», 81 «%s %s %s %s from %s was elected Alderman...»,
95 «Accept candidature?»). The task window's texts are also there («You may
choose one of the following tasks:», one description per type: «An overland
trade route is to be established between two towns in the %s. Your task is
to bring in the necessary raw materials.», «The Hanseatic League is being
targeted by a notorious pirate...», «...a pirate's hideout...», «A town in
the %s is having big supply problems...», «Many thanks for taking on this
important task.»). The CPR has the names (towns, people, ships:
`scripts/Namen*.txt`), which the viewer already reads.

## 2. The viewer

- `src/p3data/hanse_tasks.{hpp,cpp}` (new): what the game's UI reads.
  - `offered_hanse_tasks` (0x4EC5F4), `merchant_hanse_task` (0x4EC694),
    `road_materials` (0x4EC704);
  - `available_road_materials` (the office from 0x5308A0, the ships docked);
  - `candidacy_reputation` (the sum of 0x4F75F0, the same as 0x4F73D0's),
    `mayor_towns`, `alderman_election_tick`;
  - the operations: `take_task_operation`, `road_materials_operation`,
    `road_window_amount` and `queue_operation`. The executor runs them after
    the next tick, as the game runs its queue between two calls to
    advance_time.

  It only reads the save, except `queue_operation`; nothing in the
  simulation calls it, so no `world.sh`.
- `src/app/main.cpp`: the panel (`HanseUi`, `HanseView`, `HanseLayout`,
  `hanse_act`, `draw_hanse`). It opens with Square in a town (H on the
  keyboard):
  - up/down picks the task or the material;
  - left/right picks the amount (1, 5, 50, all);
  - X takes the task or gives the material, Circle or Square closes;
  - touch works on the rows and the buttons.

  A notice gives the result of each election.
- `src/app/debug_states.{hpp,cpp}` (new), `--debug-state` (see `p3vita
  --help`). The candidate does what 0x4F75F0 does past 120: [+0x37] = 1 and
  the candidacy (0x5326E0, `fn_5326e0`, which queues the 0x86 and 0x85
  operations). Its tasks appear after the next tick.

What the screenshots showed and is worth knowing:
- In save 1, the candidacy offers 4 tasks: found Windau, road Stettin–Torun
  (120 sections: 360 loads of wood, 480 of bricks, 120 barrels of tools, 240
  loads of hemp), the pirate ship Hildegard, Visby to 3,250 inhabitants.
  There is no pirate nest (no pirate has more than 2 ships).
- When a task starts (0x4EBF54) the game sets its target and deadline again:
  Visby offered at 3,250 until 1 May 1301, taken at 3,300 until 13 May
  1301. p3vita does the same (`take_grow`: 0 differences).

Pending in the panel (besides the decisions above):
- The game's longer descriptions of each task (in the executable).
- Progress of the pirate tasks: they need the battles, which aren't in
  p3vita yet.
- The map of the pirate's hideout.

## 3. Checks and times

- `tools/oracle/retouch_player.py`: the scenarios, the bytes from
  `tools/oracle/player_ops`. A runner like `ret2f.sh`, with the scripts'
  paths as `/home/user/...` here:
  `python3 retouch_player.py EXE /tmp/$s.raw OUT.probe 2 4d85c0,535760
  SCENARIO --barcos /tmp/datos --cpr CPR --player-ops
  ~/build/p3vita-pc-check/tools/oracle/player_ops`, then `tick_probe
  OUT.probe --cpr CPR`. 2 scenarios × 2 saves took 10 s; 6 × 2, 32 s.

  | Scenario | Save 1 (535760 / 4d85c0) | Save 3 |
  |---|---|---|
  | take_found | 36 / 512 equal | 25 / 512 equal |
  | take_road | 36 / 512 | 25 / 512 |
  | take_ship | 36 / 512 | 25 / 512 |
  | take_grow | 36 / 512 | 25 / 512 |
  | take_two | 37 / 512 | 26 / 512 |
  | take_not_candidate | 37 / 512 | 25 / 512 |
  | give_road | 39 / 512 | 27 / 512 |
  | give_needed | 39 / 512 | 27 / 512 |

  All with 0 differences.
- In save 3 the player's office town has no road pair, so the scenario
  gives from the ship only, in the road's town. Save 1 covers the office
  (Lübeck is in the pair 12–14).
- `build.sh`: ctest 8 of 8. `vita_check.sh`: VPK of 1,194,088 bytes, no
  warnings. Not tried on a Vita.
- No `world.sh`: no code the simulation or the loader runs has changed.

## For the roadmap

- The path to Alderman, the player's side in the viewer: candidacy, tasks,
  taking one (0x4B), the road materials (0x3B), progress, the election's
  result. Left: the vote, the candidature question, the end (video and
  window), the game's texts (decision 1).

## For the inventory

- UI functions studied (not simulation): 0x5E87D0 (task window: 0x4B and
  the developers' 0x4A), 0x5F2A20 and 0x5EED40 (road window: 0x3B), with
  the helpers 0x4EC5F4 and 0x4EC704. Operation 0x4A (queued by the task
  window's other buttons) is not in the executor's list of what p3vita runs.

## Game bugs

None found.

## Changes to shared files

None. Not touched: `alderman.cpp`, `run_operation`, `retouch.py` (only
imported). Additions to `src/p3data/CMakeLists.txt`,
`tools/oracle/CMakeLists.txt` and `tests/CMakeLists.txt`.
