# Cloud task: the path to Alderman in the viewer

Branch `cloud/alderman-viewer`, from `main` at 61cdbfa. Screenshots (game
graphics): `screenshots/alderman-viewer/` in p3vita-datos, branch
`cloud/alderman-viewer`.

## Summary

(in progress: part 1, the study, is written below; parts 2 and 3 to come)

## Decisions for the user

(to come)

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
