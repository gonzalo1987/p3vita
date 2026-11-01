# Battles, part 2 (cloud/battles-2)

Task `tasks/battles-2.md` of p3vita-datos: the pirates' pool, the rest of
piece 2 of `docs/cloud/battles.md` (the pirates moving) and piece 3 (the
automatic sea battle).

## Summary

- **Part 1, the pirates' pool (done):** the heap blocks of the 5 pirate
  objects (the pool of 8-byte entries and the map of the nest) are game
  memory in `SaveGame::pirate_nests`, loaded and written as the game does,
  recorded by the oracle (`ZMi`, `ZDi`, `ZPi`) and compared by tick_probe.
  `write-check.sh` as before; the world 12 saves × 7 days, 0 «distintas».
- **Part 2, the rest of the pirates moving (done):** the disbanding at the
  nest (0x514B80, 0x515DA0, 0x513BF0: the third jump of the provisional
  rule is gone), the lost ship and the pirate alone (states 0x11, 0x12:
  0x506AF2, 0x517170, 0x507022, 0x507099, 0x5062F0), the pirate branch of
  0x5017F0, the pirate objects' day (0x514000, 0x513EC0) with operation 0xB5
  (0x514D40, 0x515750, 0x5185C0), and the attack of event 0x1E (0x5063C0,
  0x513D80, 0x515EA0; rewritten, its call in the local session's
  `events_rare.cpp` described, not made). Checked call by call in the 12
  saves × 7 days (0x514000 182–189 calls, 0x505C90 1775, 0x514D40 6...) and
  with the retouched states of the new `tools/oracle/retouch_pirates.py`
  (12 scenarios, every reachable branch): 0 differences; the world 12 saves
  × 7 days, 0 «distintas» by default and with `--pirates`.
- **Part 3, the infrastructure (done):** the oracle records the battles as
  one region `BT` (the object of 0x6E55D0 and every heap block that hangs
  from it, pointers turned into block indices; `BA` their addresses for the
  probes) and has `--battles` (the pirates attack; after each tick with
  battles, operation 0x94 with 84 steps); p3vita has `SaveGame::battles`
  (`BattleGraph`, normalized to the oracle's order), compared by tick_probe,
  the clock rule (`Sailing::step_battles`, `set_battles`) and operation 0x94
  (without the battles' own step yet).
- **Part 4, the battle (started):** the whole start of an automatic sea
  battle is rewritten and equal call by call: the attack 0x50BC40 and the
  setup 0x605420 with what they call (0x611CD0, 0x603A70, 0x618F10,
  0x619450, 0x61A1D0, 0x61A4E0, 0x62125B, 0x61A750, 0x503AC0, 0x608230,
  0x6079D0, 0x60F479, 0x6197A0, 0x50CFB0, 0x5176C0, 0x505B60). With
  `--pirates --battles`, in the 6 saves with battles × 7 days: 0x50BC40 11
  calls, 0x605420 11, 0x619450 670, 0x62125B 6297, and the ships' update
  0x506720 6 × 1792 calls with the attacks in it, 0 differences. Left: the
  step of a battle (0x606CC0 and its tree) and its end, about 15 000
  instructions, mapped in §4 and `docs/cloud/battles-2-605420.md`.
- Main (0320701) merged; everything checked again after it. The oracle's
  `HeapReAlloc` now keeps only the old block's requested bytes (the rest
  zero, as `alloc`): the default world after it, 0 «distintas».
- Built for the Vita (`vita_check.sh`: VPK, no errors or warnings).

- **Correction to docs/cloud/battles.md («no battle in any of the 12
  saves»):** three saves have a sea battle saved, all of pirates (owner
  0xFF) against one merchant: **6** (3 ships, 2 + 1, against merchant 0x1D,
  automatic), **7** (3 ships, 2 + 1, against 0x20, automatic) and **tut4**
  (4 ships, 3 + 1, against 0x24, not automatic: a human side). Read with the
  game's reader 0x611AE0 (`write_oracle.prepare_game_object`): count
  `[0x6E59D4]` = 1, slot 0, vtable 0x67AD44, `[+0x15C]` = 0. Their battles
  sections are 1240, 1296 and 1291 bytes (5 in the other saves). They are
  the «pirate convoys left in state 0x14, outside the list» that
  battles.md saw in 6, 7 and tut4. (cloud/statistics reported save 7, the
  one whose padding the writer got wrong; main bc353ec writes it as zeros
  with `fix_game_bugs()`.) The case for piece 5 (the battles in the save).

## Decisions for the user

- **`--pirates` by default** (recommendation: not yet). With `--battles`
  the pirates attack as in the game, but the battles don't move in p3vita
  until the step is rewritten (part 4); without `--battles` the first jump
  of the provisional rule stays. Switch both on together when the step and
  the end are done and the world with `--pirates --battles` gives 0
  «distintas».
- **The oracle's realloc** (recommendation: accept): `HeapReAlloc` copied
  the old block up to its rounded size, so a grown block got the stale bytes
  of the heap slack (a battle's page array got a pointer to another block
  that way). Now it keeps the old block's requested bytes and the rest is
  zero, the same convention as `alloc`. The default world: 0 «distintas».
- **The event 0x1E's attack** (recommendation: whoever owns
  `town_tick/events_rare.cpp` adds the two calls of §2.4, with the pirates
  on).
- **The battles saved in saves 6, 7 and tut4** (recommendation: piece 5
  after the step; until then the oracle and p3vita both leave them out, so
  the world agrees but those three saves run without their battle).
- **What to do next** (recommendation: a task `battles-3` for the step and
  the end of the automatic sea battle, with the map of §4 and
  `docs/cloud/battles-2-605420.md`; then the other sea battles, piece 4).

- **The battles' rocks and the town view** (recommendation: give the
  oracle a town view object with the game's `[+0x510]`, once its value is
  known, and use the same in p3vita): 0x60F479 multiplies the rocks' and
  islets' boxes by half the town view's width (`*0x6E51AC`, UI); the
  emulator has none, so they are 0 there and in p3vita (`kTownViewHalf`).
  Until then the oracle and p3vita agree with each other, not with the
  game, for the automatic battles' obstacles. (The same family as the town
  battle reading the town view, docs/cloud/battles.md.)
- **The grid functions on a nest's map** (recommendation: for
  cloud/unchecked-branches or whoever owns `town_tick/grid.cpp` and
  `construction.cpp`): `fn_4f9c80` and `fn_4fb9c0` take a town; the nest's
  map is the same object. This branch swaps it into town 0 for the call
  (`on_nest_map`, exact but a trick). A version of both on a
  `town_class1` view (the 0x6C bytes and the cells) would remove it.

## 1. The pirates' pool (part 1)

What each pirate object (0x18 bytes, `*[0x6DD7AC + 4·i]`, `SaveGame::pirates`)
points to:
- `[+0x0]`: the map of its nest, a **town_class1** object (constructor
  0x4F95C0, 0x6C bytes; the same class as `[0x7A4]` of a town): `[+0x4]`
  width, `[+0x8]` height, the 0x5C bytes of the anchors (`[0x7B0..0x80C)` of
  a town) and `[+0x68]` the cells. Read by 0x4F9770 (version ≥ 0x7D: the file
  holds `[+0x4..+0x68)` as they are and then the cells), written by 0x4F9680.
  **None of the 12 saves has one.**
- `[+0x4]`: the pool, `[+0x8]` entries of 8 bytes; `[+0xA]` the head of the
  free ones. 0x513BF0 takes one: the free head (growing the pool by 0x20
  entries, each pointing to the next, when none is free), appends it to the
  list that starts at entry 0 (kind 0x1B), or, for entry 0, kind 0x24; and
  sets x, y and `[+0x2]` to 0xFF, `[+0x6]` and `[+0x7]` to 0. Entry: u8 x,
  u8 y, u8 `[+0x2]`, u8 kind (0x24 the nest, 0x30 from the pirate's creation
  0x513D80, 0x1B a building planned by 0x515DA0), u16 next, u8 `[+0x6]` a
  countdown (0x514000 takes 4 a day), u8 `[+0x7]` the orientation. Every save
  has 1–3 pirates with a pool of 0x10 or 0x20 entries (6–10 used).

In p3vita: `PirateNest` (`map`, `cells`, `pool`) in
`SaveGame::pirate_nests`. `load_pirate_nests` (called once at the end of the
ships section in `savegame.cpp`) moves what the parser kept in
`SaveOpaque::pirate_maps/pirate_records` into it; `pirate_nest_bytes` (one
call in `savegame_write.cpp`) writes it back. The pointers keep the
convention of the rest: 1 if there is something, 0 if not.

The oracle: `regions()` reads, for each pirate, `ZMi` (the map object, 0x6C
bytes, through the pointer of the already copied `Zi`), `ZDi` (the cells:
`[+0x68]`, width × height) and `ZPi` (the pool: `[+0x4]`, 8 · `[+0x8]`), all
through `block`, from the bytes already read. tick_probe loads and dumps
them into `pirate_nests` (pointers are not compared, as everywhere).

Checked:
- `scripts/write-check.sh` (the game's writer against p3vita's), the 12
  saves: the same as before (0 different in 11; in save 7 the 12 known bytes
  of the battles section); 18 s.
- `P3_ORACLE_CHECK=1`, world 1 day in saves 1, 3, 6, tut3: 0 «distintas»
  (30 s).
- The world, 12 saves × 7 days, by default: 0 «distintas» (125 + 123 +
  111 s); with `--pirates` in the oracle and tick_probe: 0 «distintas» (129 +
  129 + 118 s). «saltadas» as always (1, 0, 0, 0, 5, 0, 4, 0, 0, 2, 5, 7).

## 2. The rest of piece 2

### 2.1 Disbanding a convoy at its nest: 0x514B80, 0x515DA0, 0x513BF0

In 0x505C90, a pirate convoy that goes home and whose guide is exactly at
the approach point of its pirate's nest (table 0x6DDB90 + 0x34 · `[+0xE]`)
leaves the list of convoys (the link takes its `[+0x8]`), its guide leaves
the list by x (0x50D040) and **0x514B80** (on the pirate object) disbands
it:
- each ship (from the convoy's `[+0xA]`): `[+0x8]` = 0xFFFF, to the head of
  the pirate's list of ships at the nest (`[+0x12]`, linked by the ship's
  `[+0x6]`), position `[+0x1C]`, `[+0x20]` = 0x7FFFFFFF; its cargo is sold:
  for each ware, `ftol(float(amount) · 0x673A18[ware])` (floats), added up
  over all the ships, and the cargo emptied; with less than half its hull
  (`[+0x14] >> 1 > [+0x18]`) its owner `[+0x15D]` becomes 0xFF; under level
  2 (`[+0xF]`) it goes up one (0x51A750); with a captain and outside a
  network game, operation 0x12 (the captain improves, 0x538A80) is queued
  **for the convoy's guide** (if it has a captain), once per ship with a
  captain;
- the convoy is deleted (0x50A3D0), the pirate's `[+0x16]` (u16) grows by
  the sum / 1000 and **0x515DA0** plans a building of the nest.

0x515DA0: `left = [+0x16] / 200 − 2` (at most 0x20); nothing if the pirate
has a convoy in `[+0x10]` (< the number of convoys); it walks the list from
entry 0 and stops when it has counted `left` entries of kind 0x1B; otherwise
**0x513BF0** takes an entry (kind 0x1B), its `[+0x7]` = 0x20 at an odd tick,
0xE0 at an even one; without a map x and y stay 0xFF; with one, x = entry
0's x − (tick & 3) + 0xF and y = entry 0's y − ([0x6DE4A4] & 3) + 0xF,
0x4F9C80 looks for a place from there and, if found, 0x4FB9C0 puts the
building. The game passes the arguments of 0x4F9C80 swapped (see «Game
bugs»): it looks for a site of type 0x20 or 0xE0 (the orientation) and
0x4FB9C0 then puts type 0x2A with that orientation, which never fits there.

The map of the nest is a town_class1 object like a town's, and p3vita's
grid functions (`fn_4f9c80`, `fn_4fb9c0`, town_tick files) work on a town:
`on_nest_map` (`pirate_nests.cpp`) puts the nest's map in the place of town
0's for the call and back. A cleaner way, for whoever owns those files: a
version of them on `town_class1` + grid (see «Decisions»).

What the saves reach (12 saves, 7 days, `--pirates`): 0x514B80 8 times (saves
3, 4, 5, 6, tut1 once, 7 twice), each followed by 0x515DA0, which returns
before taking an entry (the pirates have under 600 in `[+0x16]`); 0x513BF0
never; no pirate has a map. Probes `514b80,515da0,513bf0,505c90`: 0
differences (43 s for 8 saves).

Retouched states, `tools/oracle/retouch_pirates.py` (new):
- `disband`: every pirate convoy in state 0x12 at its nest without a route
  and with cargo (wares 0, 9, 20, 23), the guide with captain 0, levels 0, 1,
  2..., the last ship at 1/3 of its hull, the pirate with 0x3000 and a full
  pool (0x513BF0 grows it). Saves 3 and 5: 5 disbandings, 0 differences.
- `disband_map`: the same with a map for each pirate (a copy of town 0's).
  Saves 3 and 4: 0 differences.
- `plan`: direct calls of 0x515DA0 (through a trampoline: a call straight
  from Python returns to the sentinel and the Recorder never closes its
  sample) on each pirate at an odd and an even tick: with a convoy, no money,
  no map, a map with entry 0 at the anchor, at (0, 0) and at (0xF0, 0xF0)
  (outside the grid: 0x4F9C80 moves it to the anchor), with a town hall site
  where the search starts and 3 rings away, and with the pool full. Saves 1,
  3, tut4: 0x515DA0 18 + 54 + 18 calls, 0x513BF0 14 + 42 + 14, 0 differences.
With them every block of 0x514B80, 0x515DA0 and 0x513BF0 has run (checked
with `--blocks`) except the network-game test of 0x514B80.

The oracle: `--pirates` no longer jumps over 0x505FF1 (`NO_DISBAND_JUMP`,
with `--no-disband`); tick_probe `--no-disband`
(`Sailing::set_pirate_disbanding`). Probes 0x514B80, 0x515DA0, 0x513BF0 in
`probes_battle.cpp` (the pirate is found by its pointer in the globals).

### 2.2 The lost ship and the pirate alone: states 0x11 and 0x12

The two walks of 0x506720 dispatch by the ship's state (tables 0x507CB0 for
port, 0x507CF0 for sea). With `--pirates` (oracle `_docked`/`_sailing`, p3vita
`pirates_`) these now run too:
- **port, state 0x11 (0x506AF2), a lost ship:** out of the port list and of
  its convoy (`[+0x6]`, `[+0x8]` = 0xFFFF); a human player's ship stays out
  of every list at the approach point of one of its towns (0x517170: the
  first of `[+0x39]`, `[+0x37]`, `[+0x38]` that is a town, else town 0;
  `[+0x3D]` |= 0x20; 0x44F940 tells the sea map, UI), with no target
  (`[+0x50]`, `[+0x52]`), hull or sailors; any other is deleted (0x507FB0).
- **sea, state 0x11 (0x507022):** out of the sea list and the list by x, no
  target; deleted unless it is a human player's (two UI notices with 0x17
  before, quiet).
- **sea, state 0x12 (0x507099), a pirate alone:** out of the sea list; with
  hull, **0x5062F0** makes it a convoy of its own at the head of the convoys
  (a free convoy, 0x509AE0 if none; owner, `[+0xE]`, the towns, the capacity,
  `[+0x18]` |= 0x400, 0x501680 with mode 2, wait 0x100, the ship's state),
  which the walk of the convoys then moves; without hull, out of the list by
  x and deleted.
Who sets these states: 0x5070CF (sea states 0xA, 0xB, 0x16: state 0x11 and
no hull) and the end of the battles (piece 3).

**The pirate branch of 0x5017F0** (0x5018E4: the guide of a pirate convoy is
gone or has no hull or sailors): the first ship with hull and a captain
guides; else the first with hull, with a new captain (0x5097C0 with 1); the
bit 0x10 of `[+0x14]` is cleared and the new guide goes back to the list by x
if the old was in it (true). With no hull at all, `[+0x10]` ends past the
last ship and false. Rewritten without a setting (it is the game's code; it
was a `return false` that nothing reached).

The saves reach none of this. Retouched states (`retouch_pirates.py`,
recording `506720,517170,5062f0,5017f0`, 1 day; tick_probe `--pirates`):
- `lost` (saves 3, 5, 6, tut0): state 0x11 for a human and an AI ship in
  port and at sea (an AI ship becomes the player's when no human ship is
  alone at sea): 0 differences (256 calls of 0x506720 each).
- `alone` (saves 3, 4, 5, 6): a ship of each pirate convoy leaves it
  (0x509F80) and is put at the head of the sea list, one with no hull, and
  no free convoy for the first (0x509AE0): 0 differences (1–2 calls of
  0x5062F0 each).
- `guide` (saves 3, 5, 6, tut0): direct calls of 0x5017F0 on each pirate
  convoy with the guide without hull, no captains, no hull at all and the
  guide without sailors: 0 differences (9–15 calls each).
Probes 0x517170 and 0x5062F0 in `probes_battle.cpp`.

### 2.3 The pirate objects' day: 0x514000, 0x513EC0 and operation 0xB5

0x506720 calls **0x514000** (on each pirate object, with its number) when
the tick ends in 0x6F, before the walks. Today the oracle keeps it quiet
(`QUIET_SHIPS`); with `--pirates` it runs, in p3vita too
(`update_ships`).
- A pirate that goes away (`[+0x15]` = 1, set by 0x54A57A when the pirates
  are switched off in the game's options): the ships at its nest are deleted
  (0x507FB0); of the ships of its list `[+0xC]`, those with no convoy or
  whose convoy is in the list lose their hull; when the list is empty,
  **0x513EC0** (map and pool freed, `[+0x3C]` |= 0x100 and no hull for the
  ships of `[+0xC]`, its convoy `[+0x10]` deleted, the ships at the nest
  deleted, the merchants with `[+0xE1]` = its nest get 0xFF in `[+0xE0]`,
  `[+0xE1]`, its `[+0xE]`, `[+0xF]`, `[+0x14]` = 0xFF) and the object is
  deleted (pointer 0).
- Otherwise: each of its ships gets `[+0x158]` = k (0x51A4C0); they are
  counted (the pirates' own, `[+0x15D]` not a merchant; those with a
  captain). With a convoy out (`[+0x10]`) it stops there. The countdowns
  `[+0x6]` of the pool go down 4 (to 0). Without ships at the nest, when
  `[0x6DE4A4] & 3` = 0, a new one (0x516170 type 2, strength 4000, full hull,
  two levels, 0x5185C0 sailors). With them, each is repaired 1000 (fully if
  less is missing: then a privateer's ship, or else one with a captain, is
  chosen) and the pirates' own are not counted as away. More away than
  0x673658[`[+0x14]`] + 3 (past its 4 entries the bytes that follow, 0, 0,
  0, 0x4B): the first ship at the nest without captain and of the pirates is
  deleted. No captain in any ship: the first at the nest gets one
  (0x5097C0). A chosen ship, and none away or a privateer's, outside network
  games and with `[0x6DE52A]` = 0: **operation 0xB5** {k, `[+0x14]` · 100 +
  0x78, 1 for a privateer or `[+0x14]` + 1}.

Operation 0xB5 (0x536E8C → **0x514D40** on the pirate, most ships, strength):
walking the ships at the nest, those without captain are passed; a captain
that 0x4FE150 refuses (`[+0x8]` ≤ 0x20) leaves (0x5098B0); the first with a
good captain and a full hull is taken out and the walk ends. A privateer's
ship whose `[+0x4C]` has passed goes home instead: a message to every
merchant (UI) about an AI merchant chosen with the generator seeded by the
tick, one time in 10 an event 5 in 0x300 ticks (0x4D8CF0; its fields in
the code), and the ship is deleted. Otherwise the ship is a convoy of its own
(0x5062F0 on a local head); a pirates' one takes ships at the nest with full
hull and no captain, up to `most`. Each ship goes to the nest's approach
point in state 0x12, is armed (**0x515750**: cargo emptied, flag 4, no
artillery, strength · (setting `[0x12]` + 4) / 4 of artillery of the Hanse
leader's town's level, the sailors **0x5185C0** at 70/85/100 % with a
captain or 50/70/90 % without, `[+0x154]`), becomes the pirates' (owner
0xFF), leaves the list by x, loses course, route, target and wait; the
convoy is the pirates' in state 0x12 and goes into the list (0x501BE0,
0x50AAE0). The strength is updated by each 0x515750, so it grows from ship
to ship ((setting + 4) / 4 each time).

p3vita: `battle/pirate_objects.cpp` (`fn_514000`, `fn_513ec0`, `fn_514d40`,
and `fn_515750`, `fn_5185c0`, `fn_517770` inside); `run_operation` case
0xB5. Probes 0x514000, 0x513EC0, 0x514D40.

Checked:
- probes `514000,514d40,513ec0,514b80`, 12 saves × 7 days, `--pirates`: 0
  differences (0x514000 7 calls in the saves with one pirate, 21 with three;
  0x514D40 1–2 calls in saves 3, 5, 6, 7); 53 s.
- the world with `--pirates`, saves 3, 5, 6, 7 × 7 days: 0 «distintas».
- retouched (`retouch_pirates.py`, direct calls, 0 days): `daily` (saves 3,
  5, tut0, 1: a privateer at the nest, `[+0x14]` = 4, no captains, no ships
  at the nest at both day parities, the pool's countdowns, an empty pool,
  every pirate ship in one list so one at the nest is deleted, no ships at
  all), `leave` (captains accepted and refused, a ship in the list by x, up to
  3 ships, a privateer going home and one leaving, an empty nest), `retire`
  (saves 6, 7, tut4, 3: with maps, without map or pool, a direct 0x513EC0):
  0 differences. Every branch of 0x514000, 0x514D40 and 0x513EC0 has run
  except the unreachable ones (0x51413F, 0x514169 and 0x513EE8 test a map
  that 0x513EC0 has just freed; 0x51529E a convoy without ships).

**A pirate that goes away with a convoy at sea** (retouched `retire` with
days): 0x514000 takes the hull of its convoy's ships, the convoy stays in
state 0x12, and the next tick 0x5017F0 finds no ship with hull and leaves
`[+0x10]` past the last ship; 0x507A65 then sorts ship 0xFFFF in the list by
x (0x50D0C0), outside the ships: the emulator stops there. p3vita stops
before it (`pirate_convoy`); see «Game bugs».

### 2.4 The attack of event 0x1E: 0x5063C0, 0x513D80, 0x515EA0

Event 0x1E (state 2, and state 3 again on the merchant's ships) calls
**0x5063C0** (on the ships object: the target ship, a kind; it returns the
new ship): a new pirate ship (0x507D60 out of the lists, 0x52A8D0 of town
0's size) of type 0 for kind % 3 = 0, 1 for 1, for 2 type 3 or 1 if the
target is in the shallows (0x673564, bounds inclusive); full hull; one level
(two for kind 2, 0x51A750); a name as 0x509A50 does (`fn_509a50`); 0x5185C0
sailors; a new captain (0x5097C0 with 1) whose `[+0xB]` = (kind % 3 + 2) ·
0x33; armed by 0x515750 with −1 (the mean of the merchants' arms). It joins
the first pirate that exists (**0x515EA0**: at the end of its list `[+0xC]`;
0x51A4C0); with no pirate at all, pirate 4 is made (0x513720: the empty
object) and **0x513D80**(tick % 40, the difficulty, town 0's `[+0x4]` + the
tick): `[+0x14]` = difficulty & 3, the nest a free town number (0x5159C0),
`[+0xF]` 1 for a town of kind 3..5 or number % 3 (0, 2, 3), a new pool (entry
0, difficulty entries of kind 0x30, three of 0x1B), `[+0x16]` = 1000, and
difficulty + 1 ships at the nest (0x516170, strength 40 · difficulty + 50,
the first of type 1 with a captain, the others of type 1 + two bits of the
number, two at a time, without), each with 1 of hull. The new ship appears at
the target's place (or (0x3D2, 0x317)) in state 0x12, `[+0x1C]`, `[+0x20]`
fractions 0x7FFF, the target in `[+0x50]` and `[+0x52]`, as a convoy of its
own (0x5062F0) with no wait and the flag 0x40 (the attack at once), into the
list by x from the head of the sea list (0x50CE50), `[+0x136]` |= 0xC0.

p3vita: `Sailing::fn_5063c0`, and `fn_513d80`, `fn_515ea0` in
`battle/pirate_objects.cpp`; `fn_5159c0` (`pirates.cpp`) is no longer in an
anonymous namespace. Probe 0x5063C0 (it compares the returned ship).

**Not connected:** the calls are in `src/p3data/town_tick/events_rare.cpp`
(`fn_4dfd44`, the local session's file). The change for whoever owns it,
with a `Sailing*` reaching that function:
- state 2, where it says «Por traducir: el ataque al barco (0x5063C0 con 1…)»:
  `sailing->fn_5063c0(uint16_t(s), 1);` before `e.u32(0) += 0x200;`
- state 3, where it says «Por traducir: 0x5063C0 con 0 sobre el barco `s`»:
  `sailing->fn_5063c0(uint16_t(s), 0);` before the `break`.
Both only with the pirates on (`set_pirates`), as the oracle with `--pirates`
(without it, the oracle runs the game's 0x5063C0 too, but the saves never
reach it).

Checked with retouched direct calls (`retouch_pirates.py`): `attack` (a
human player's ship at sea, kinds 0, 1, 2, 5, the target moved into the
shallows, no target; 0 days), `attack_new` (the same, then every pirate
pointer cleared: pirate 4 is made, twice; 0 days), saves 1, 3, 5, tut2 (and 6,
tut0): 0 differences (6–8 calls each); `attack_one` (one call, then a day of
0x506720), saves 1 and 3: 0 differences (256 calls of 0x506720). Every
branch ran except the name loop's repetition (below) and a failed
allocation. Six new pirates at the same place, followed by days, tangle the
list by x so that 0x50D0C0 never ends in the game (saves 1 and 3): an
artefact of the retouch (one call is fine).

While checking it: tick_probe now empties `SaveGame::pirates[i]` when the
region `Zi` is empty (before, a pirate deleted in the game stayed in p3vita
from an earlier sample).

## 3. Piece 3, the infrastructure

### 3.1 What a live sea battle holds

`re/battles/layout.py`-like walk (save 5, the battle of day 3, right after
0x605420 and 3 ticks later): 28 heap blocks hang from the battle object,
none of them a world block:
- the object (0x1E98, vtable 0x67AD44); `[+0x4]` an array (4 · ships) of
  pointers to the **scene's ships**: objects of 0x164 bytes, vtable 0x67B0D4,
  with a pointer back to the battle at `+0x12C` and, once they move, at
  `+0x68` a pointer into the navigation's points (a world block: the only
  pointer that leaves the battle); `[+0x8]`, `[+0xC]`, `[+0xA74]`,
  `[+0xA78]` arrays of u16 per ship; `[+0x18]` 0x40, `[+0x1C]`, `[+0x24]`
  0x80, `[+0x28]..[+0x40]` six blocks of 0x100; `[+0x48]`, `[+0x4C]`,
  `[+0x50]` arrays of two pointers to blocks of 0x100.
- So the «0x180 copies of the ships» of the plan are these 0x164-byte scene
  objects.

### 3.2 The oracle: region BT and `--battles`

`TownOracle.battle_graph` (`town_oracle.py`, in `regions()`, through the
fast reader): block 0 is the battles object at 0x6E55D0 (0xC48 bytes, up to
its clock `[+0xC44]`: the 255 slots, the count `[+0x404]`, the flag
`[+0x3FE]`, the timers `[+0xC30]`, `[+0xC34]`); then every heap block
reachable through a pointer to its start (not the world's blocks, the
regions' addresses), in the order of a walk by offsets, with its requested
size. Every pointer into one of these blocks becomes `0xF0000000 | block <<
16 | offset`; other pointers stay. Region bytes: u32 number of blocks and,
for each, u32 size and the bytes. The order is the walk's, not the
allocation's, so p3vita gets the same bytes if it has the same structure.
After the load the object is all zero in the emulator: town_oracle.py's
load doesn't run the battles' reader 0x611AE0 (only write_oracle.py does,
for writing), so the battles saved in saves 6, 7 and tut4 don't exist there,
and p3vita doesn't load them either (`SaveOpaque`): one block of zeros on
both sides. Loading them (0x611AE0 in the oracle's load, and p3vita reading
the section into `BattleGraph`) is piece 5; with it those three saves would
start with a battle running (in tut4 one with a human side, part 7's
window).

`--battles` (with `--pirates`): the jump 0x507BE9 (the attack) goes, and
after each tick with battles (`[0x6E59D4]` ≠ 0), after the tick's other
operations, operation 0x94 with `BATTLE_STEPS` = 84 through the executor
trampoline (recorded as 0x535760, like the others). The third jump of the
rule (the end of a route, a town battle) stays.

Checked: `P3_ORACLE_CHECK=1`, the world 1 day in saves 3 and 5 (BT always
present, no battle): 0 «distintas»; `--pirates --battles` in save 5, 5 days
(437 samples, the battle of day 3 recorded: about 18 KB of BT): the
snapshot check passes.

### 3.3 p3vita: SaveGame::battles, the clock rule, operation 0x94

- `battle/battles.hpp`: `BattleGraph` (the blocks as the oracle records
  them; `count()`, `battle(k)`, `get`/`set`), `SaveGame::battles`.
- `battle/battles.cpp`: `Sailing::step_battles` (the rule: with
  `set_battles`, operation 0x94 with `kBattleSteps` = 84 when there are
  battles, called at the end of `advance_game_tick`), `fn_536bbf`
  (operation 0x94: method +0x14 of each battle that runs, **not rewritten
  yet**, then the time mode and `[+0x3FE]` = 1), `fn_612c90` and
  `fn_612d20` (operation 0xC8 with 2 if a battle has a human side, with 0 if
  none has: tick_probe compares the queued operations).
- tick_probe: loads and dumps `BT`; `--battles` (`set_battles`).

With `--battles` the comparison of the steps differs (the battles don't
move in p3vita yet): save 5, 5 days, 266 equal executor calls and 163
different (the ones with a battle), as expected until part 4.

## 4. Piece 3, the rewrite (in progress)

`BattleGraph::alloc` (a new block of zeros: the oracle's heap gives zeros,
the game's operator new doesn't clear) and `normalized()` (the blocks in the
oracle's walk order; tick_probe dumps `BT` normalized, so p3vita may create
blocks in any order).

Done, call by call with `--pirates --battles` (saves 4, 5, 6, tut0, tut1,
tut3, 7 days; 39 s):
- **0x611CD0** (a new sea battle in the first free slot, the count),
  **0x603A70** (its constructor), **0x618F10** (the scene's constructor):
  11 calls of 0x611CD0, 0 differences.
- `battle/battle_setup.cpp`: **0x619450** (a new item of the scene) with
  **0x61A1D0** (the scene's arrays grow by 0x20 items, realloc), **0x62125B**
  (a scene ship's strength) and **0x61A750**, **0x503AC0** (the Hanse
  fleet's convoy), and **0x61A4E0** (a scene ship, 0x164 bytes): 0x619450
  670 calls, 0x62125B 6297, 0x61A750 12, 0x503AC0 11, 0 differences.
  0x61A4E0 can't be compared alone (its block hangs from the battle only
  after it returns): its samples are skipped; it is checked with 0x605420.
- The probes find a battle block by the emulator's address it receives:
  the oracle records region `BA` (the address of each block of BT, not
  compared) and tick_probe's `battle_block`.
- **The oracle's realloc changed** (`HeapReAlloc` in
  `TownOracle.setup_imports`): it copied the old block up to its rounded
  size, bringing the stale bytes of the heap slack into the new block; the
  page arrays of a battle scene (realloc from 4 to 8 bytes) got a stale
  pointer to another block that way, which the walk of BT recorded. Now it
  keeps the old block's requested bytes and the rest is zero, as `alloc`
  gives zeros. The world 12 saves × 7 days by default after it: 0
  «distintas» (180 + 178 + 162 s). The sizes the oracle sees for a realloc:
  n for no block, n rounded up to 16 for a block (the CRT's HeapReAlloc);
  p3vita's `BattleGraph::realloc_at` does the same.

- **0x605420, the setup of a sea battle** (its automatic path; notes in
  `docs/cloud/battles-2-605420.md`), with **0x608230** (method +0x18),
  **0x6079D0** (method +0x2C: the two sides in formation, with a square
  root and the battle's generator), **0x60F479** (the rocks and islets) and
  **0x6197A0**: 11 calls of 0x605420 in saves 4, 5, 6, tut0, tut1, tut3, 0
  differences (the whole battle object and every block it hangs, the scene's
  ships included). Not rewritten in it: the human sides' messages and
  window (part 7), a storm or two pirates as sides (method +0x4, the end) and
  the end when `[+0x5C]` (method +0x1C, 0x60D299).
- **0x60F479 depends on the town view**: it multiplies the coordinates of
  its items (the rocks' and islets' boxes in the scene) by u16 `[*0x6E51AC
  + 0x510]` / 2, the half width of the town view (UI). The emulator has no
  town view: 0, and p3vita reproduces that (`kTownViewHalf`). In the game
  the rocks' boxes have their real size, so the battles there may differ
  (ships avoiding or hitting rocks). See «Decisions».

- **The attack, 0x50BC40** (`battle/attack.cpp`, with 0x50CFB0, 0x5176C0,
  0x505B60), wired into the convoy state 0x12 (`pirate_convoy`, as
  0x507BAE) with `set_battles`: the first jump of the provisional rule is
  gone with `--battles`. Call by call with `--pirates --battles` (saves 4,
  5, 6, tut0, tut1, tut3, 7 days): 0x50BC40 11 calls and 0x506720 6 × 1792
  calls (the ships' update, with the attacks in it), 0 differences.

- The step, started (`battle/battle_step.cpp`): **0x6212FB** (an enemy to
  attack), **0x61EFDF** (arms per hull), **0x61F02F** (whether to board),
  **0x6029B0** (the direction of a vector, table 0x67AA28), **0x619560**
  (a scene item freed). Call by call with `--pirates --battles` (saves 5, 6,
  5 days): 0x6212FB 7698 calls, 0x61F02F 7781, 0x61EFDF 161, 0x6029B0
  9996, 0x619560 2, 0 differences.
- The collisions (`battle_step.cpp`, tables 0x67ABB0 the sine, 0x67AB30 the
  hulls, 0x67AB88/0x67AB90/0x67AB98; 0x67ACB8 is a copy of 0x67AA28):
  **0x609BA0** (two ships collide and may grapple), **0x608F70** (the
  collision: pushed apart when closer than 10, else a blow to each where the
  line from the other's centre to the crossing meets its hull: a push, a
  turn, the speed), **0x609D40** (whether two hulls cross, and where),
  **0x60ADBF** (where a segment meets a hull; no probe: its results go to
  the caller's stack, checked with 0x608F70). Call by call (saves 5, 6, tut0,
  tut3, 7 days): 0x608F70 771 + 745 calls, 0x609BA0 35 + 59, 0x609D40 528 +
  468, 0 differences. `[+0xAC8]` is 1 in an automatic battle (no human side)
  and makes the pushes and turns larger; its 0 branches, and the blow on a
  side that turns it the other way, are not run in the saves: left for a
  retouched state with a human side once 0x606CC0 is done. The crash's
  sound (0x443690, UI; it gives none in the emulator) is left out.
  Not wired yet into p3vita's simulation (their caller is the step), so the
  world can't change with them; the world is checked when 0x606CC0 is.
- **0x608850** (method +0x0 of the battle: two items of the scene meet),
  **0x60BC5A** (a ship inside an item's circle), **0x60BCDA** (the ship marks
  the item in its 256 directions, [+0x27] of a scene's ship). 0x608850 is
  called thousands of times a day: probes with `--max` (saves 5, 6, tut0, 5
  days, 6000 samples; and from day 2, 20000): 0x608850 5971 + 5987 calls,
  0x60BC5A 2098 + 1914, 0x60BCDA 1749 + 1616, 0x608F70 356 + 348, 0
  differences. Not rewritten yet in 0x608850 (not run in the saves): kind 1
  (a cannon ball: 0x60A73C, 0x603470) and kinds 2 and 3 (a rock, an islet:
  0x60B357, 0x61F96F; their boxes have no size in the emulator).
- **0x6113C9** (the wind of a sub-step, by the battle's generator) and
  **0x6197F0** (the scene's items that meet: both axes sorted by bubbling
  lists of the boxes' ends, two bit matrices of the pairs that cross, then
  0x608850 for each pair crossing on both): saves 5, 6, tut0, 7 days,
  0x6113C9 972 + 547 + 166 calls, 0x6197F0 the same, 0 differences.
- `battle/ship_step.cpp` (the step of a scene's ship), first part:
  **0x6264C0** (it moves: its speed towards its battle speed by the wind,
  its sails and the wind's strength; its ship's [+0x30]; then it turns to
  where it heads, or the least avoided direction near it, with the sails set
  by the distance) and **0x6271D7** (it turns: a rate by type, speed,
  captain and hull; the way round with less to avoid). The shared arithmetic
  is in `battle/battle_math.hpp`. Saves 5, 6, tut0, tut3, 7 days: 0x6264C0
  3349 + 1807 + 330 + 214 calls, 0x6271D7 2477 + 1447 + 328 + 212, 0
  differences.
- **0x61DBBF** (method +0x4 of a scene's ship: a target among the enemy's
  ships, by the battle's generator weighted by arms per hull), **0x61F1BF**
  (a round of a boarding: losses by strength, the reserve, fleeing) and
  **0x60C1C9** (`battle/capture.cpp`: a ship taken: the plunder of its
  reserve and wares by worth, 0x673A18; the pay and fame of the merchant of
  the winner's guide; the pirates' share **0x515F70**: [+0x16], a planned
  building, maybe a new wreck at the nest, a nest building back). Saves 4,
  5, 6, tut0, tut1, tut3, 7 days: 0x61DBBF 15 + 12 + 2 + 2 calls, 0x61F1BF
  78 + 378 + 156, 0x60C1C9 1 + 4 + 2, 0x515F70 3 + 2, 0 differences. Not
  rewritten in 0x60C1C9 (not run in the saves): the winner taking the ship
  over (operation 0x91 at once, 0x540520, and the sides' lists), a side of
  the player (its messages and window, part 7), the letters (0x4D6530, not
  rewritten in p3vita).
- **0x61DD4F** (method +0x8 of a scene's ship: it fires; where the ships
  will be, friends in the way, the closest enemy on each side), **0x620F0B**
  (a broadside's damage: guns by distance, captain, fighting, generator),
  **0x61F96F** (a hit: hull, sunk, a gun lost, sailors) and **0x611649** (the
  music level [+0x1E94]). Saves 4, 5, 6, tut0, tut3, 7 days: 0x61DD4F 277 +
  2540 + 1483 + 328 + 212 calls, 0 differences; but automatic battles
  seldom fire (they board): one broadside in tut3 (7 days) and one in save
  6 (21 days), both equal. Not rewritten: with a human side the guns fire
  cannon balls (0x61E8EF, part 7).
- **0x61A9E0** (method +0x0 of a scene's ship: its whole sub-step:
  sinking, out of the area, counters, grappling, a human side's course, an
  AI's decision to flee, fleeing, its target, the chase 4 steps ahead, the
  edges, broadside, its boxes, firing) with **0x611149**. Saves 4, 5, 6,
  tut0, tut3, 7 days: 435 + 3355 + 1809 + 332 + 216 calls, 0 differences.
  The branches of a human side are written but not run in the saves.
- **0x606CC0** (method +0x14 of a sea battle, the step: the sub-steps, the
  teams' sums, the wind, each ship's sub-step, whether a side has lost or
  leaves, the items that meet). Saves 4, 5, 6, tut0, tut1, tut3, 7 days: 27
  + 184 + 103 + 31 + 20 + 20 calls equal; 2 + 8 + 6 + 2 + 2 + 2 differ: the
  last two ticks of each battle, which reach its end (method +0x1C,
  0x60D299, and the destructor, method +0x4, 0x611740 → 0x603BC0), next to
  be rewritten (`battle/battle_end.cpp`). Not wired into the simulation
  until then.
- The end (`battle/battle_end.cpp`): **0x60D299** (method +0x1C: a third
  party that stays for another battle, the counts and the music by how each
  ship ended, the pirates' news [+0x408] of the battles object, the battle
  over with [+0x15C] 1 or 250), **0x611740/0x603BC0** (the destructor: the
  slot and count, the time mode, a new battle with the third party, the
  table of ships that met **0x612BD0/0x612A20** ([+0xC30], [+0xC34] of the
  battles object), the event 5 **0x50CB80**, a Hanse convoy home
  **0x50C9E0** (but 0x6132C0, not run in the saves) and each convoy after the
  battle, 0x50BFD0, being rewritten). Saves 4, 5, 6, tut0, tut1, tut3, 7
  days: 0x60D299 11 calls, 0x612BD0 11, 0 differences; 0x606CC0 now differs
  only in the battle's last tick (1 + 4 + 3 + 1 + 1 + 1 of 396 calls), the
  one that reaches 0x50BFD0. The report and letters of the end (0x4D6530),
  the news ticker and the player's window are not rewritten (messages, UI).
- **0x50BFD0** (each convoy after its battle: going on or broken up, sunk
  ships deleted, money, fame, [+0x1B]) with **0x4EC864**, **0x4F8D90**,
  **0x503B00**, **0x504970** (the towns' wares on the way again). Saves 4, 5,
  6, tut0, tut1, tut3, 7 days: 0x50BFD0 22 calls, 0x611740 11, and **0x606CC0
  407 calls, all equal**: the whole step and end of an automatic sea battle
  run as in the game. Not reproduced in 0x50BFD0: the trade routes
  [+0x132] (0x4D4DD0 on 0x6DD728, not in SaveGame), the Hanse event's list of
  sunk ships (0x4F3980, 0x4F3AD0), letters and UI.
- **The recording of BT took numbers for pointers**: at the start of a
  battle the places of the scene's ships are whole, so their 16.16 x
  ([+0x8]) is x · 65536, 0x10xx0000 for x from 4096 on, and the heap gives
  blocks that start exactly there: `battle_graph` turned the place into a
  pointer to that block (block 0x20, x = −4064 in p3vita). Now the oracle
  knows the words of a battle block that are numbers (`BATTLE_NUMBERS`: a
  scene ship's [+0x8], [+0xC], [+0x14], [+0x18], [+0x1C], a sea battle's
  generator [+0xADC]; by size and vtable) and leaves them alone, as
  p3vita's `BattleGraph::normalized` (`number_field`), and tick_probe
  compares them even when they look like a pointer (before, they were never
  compared then). The probes of §4 again after it (saves 4, 5, 6, tut0,
  tut1, tut3, 7 days): 0 differences.

So with `--battles`, p3vita starts the automatic sea battles exactly as the
game. What is left is the battle itself: the step (operation 0x94, method
+0x14, 0x606CC0 and what it calls, about 7 000 instructions) and its end
(methods +0x4, +0x1C, 0x50BFD0...). Until then `--pirates` can't be
switched on by default with `--battles` (the battles would never move in
p3vita), and without `--battles` the first jump of the rule stays.


## Changes to shared files

- `src/p3data/savegame.cpp` (cloud/statistics): one line, at the end of the
  ships section, before `return r.ok() ...`: `load_pirate_nests(out);`.
- `src/p3data/savegame_write.cpp` (cloud/statistics): in the loop of the 5
  pirates, the four lines

  ```cpp
  const std::vector<uint8_t>& map = save.opaque.pirate_maps[i];
  w.u8(map.empty() ? 0 : 0x7F);
  w.bytes(map);  // 0x4F9680
  w.bytes(save.opaque.pirate_records[i]);
  ```

  become `w.bytes(pirate_nest_bytes(save, i));  // The map (0x4F9680) and the pool.`
- `src/p3data/savegame.hpp`: the include of `battle/pirate_nests.hpp`, the
  member `pirate_nests` after `pirates`, and a sentence in the comment of
  `SaveOpaque::pirate_maps/pirate_records` (now only between the parser and
  `load_pirate_nests`).
- `tools/oracle/town_oracle.py`: the regions `ZMi`, `ZDi`, `ZPi` in
  `regions()` and in the header's list.
- `tools/oracle/tick_probe.cpp`: load and dump of those regions.
- `src/p3data/CMakeLists.txt`: the new files (`battle/pirate_nests.cpp`,
  ..., `battle/ship_step.cpp`).
- `src/p3data/sailing.hpp`: `fn_514b80`, `set_pirate_disbanding`,
  `disband_`.
- `tools/oracle/town_oracle.py`: `NO_DISBAND_JUMP` out of `PIRATE_JUMPS`,
  the option `--no-disband` and the constructor argument `disband=True`.
- `tools/oracle/town_oracle.py`: with `--pirates`, `_docked` lets the port
  state 0x11 through and `_sailing` the sea states 0x11 and 0x12.
- `src/p3data/sailing.cpp`: in `update_docked`, `case kShipSunk` →
  `lost_in_port` with `pirates_`; in `update_sailing`, the states 0x11 and
  0x12 → `lost_at_sea`, `pirate_at_sea` with `pirates_`. `sailing.hpp`: their
  declarations, `fn_5062f0` and the free `fn_517170`.
- `src/p3data/simulation.cpp` (`run_operation`, local session's): before
  `case 0xB8`,

  ```cpp
  case 0xB5:  // A pirate convoy leaves the nest (0x536E8C → 0x514D40; battle/pirate_objects.cpp).
      if (sailing && uint32_t(fields[1]) < 5 && save.pirates[size_t(fields[1])] &&
          save.globals.u32(global::kShips + 0xC + 4 * uint32_t(fields[1])) != 0) {
          sailing->fn_514d40(size_t(fields[1]), uint16_t(fields[3]), uint16_t(fields[2]));
      }
      break;
  ```
- `src/p3data/sailing.cpp` (`update_ships`): with `pirates_`, when the tick
  ends in 0x6F, `fn_514000` for each pirate; `sailing.hpp`: `fn_514000`,
  `fn_513ec0`, `fn_514d40`.
- `tools/oracle/town_oracle.py`: `PIRATE_UPDATE`, `PIRATE_OPERATIONS`; with
  `--pirates`, 0x514000 is not quiet and 0xB5 is executed.
- `tools/oracle/tick_probe.cpp`: an empty region `Zi` resets
  `SaveGame::pirates[i]`.
- `src/p3data/convoys_members.cpp`: the pirate branch of `fn_5017f0` and the
  declaration of `fn_5097c0` (`town_tick/events.cpp`).
- `src/p3data/simulation.cpp` (local session's): at the end of
  `advance_game_tick`, after `run_queued_operations(save, sailing);`, the
  line `sailing.step_battles();  // The battles' clock (battle/battles.cpp).`;
  in `run_operation`, before `case 0xB5`:

  ```cpp
  case 0x94:  // The battles take their steps (0x536BBF; battle/battles.cpp).
      if (sailing) {
          sailing->fn_536bbf(uint32_t(fields[1]) & 0xFFFF);
      }
      break;
  ```
- `src/p3data/savegame.hpp`: the include of `battle/battles.hpp` and
  `SaveGame::battles`.
- `tools/oracle/town_oracle.py`: `HeapReAlloc` keeps only the old block's
  requested bytes (§4); the region `BA`;
  `BATTLES`, `BATTLES_SIZE`, `BATTLE_POINTER`,
  `BATTLE_STEPS`, `BATTLE_JUMP`, `battle_graph`, the region `BT`, `--battles`
  (constructor `battles=False`, after main's `statistics`, `last_steps`).
- `tools/oracle/tick_probe.cpp`: `BT`, `--battles`, and in `pointer_field`
  (from main) the cases `Z` `[+0x0]`, `ZM` `[+0x68]`, `BT`.
- `tools/oracle/town_oracle.py`: `BATTLE_NUMBERS`, and `battle_graph`
  leaves those words as they are (§4, the 16.16 places); `tick_probe.cpp`:
  in `pointer_field`, `BT` compares them even when they look like a pointer
  (`BattleGraph::number_field`).
- Declared in my files (not changed): `fn_4f9c80`
  (`town_tick/construction.cpp`), `fn_51a750` (`town_tick/alderman.cpp`).

## For the roadmap

- «Piratas, persecuciones, encuentros y batallas», piece 2 (the pirates
  moving): done in p3vita behind `--pirates` (`Sailing::set_pirates`): the
  convoys at sea, the disbanding at the nest, the nest's pool and map, the
  lost ship and the pirate alone, the pirate objects' day and operation
  0xB5, the attack of event 0x1E (to connect in `events_rare.cpp`). Only the
  first jump of the provisional rule (the attack, until the battles step)
  and the second (the end of a route: the town battle, piece 6) remain.
- Piece 3 (the automatic sea battle): the infrastructure is done
  (`--battles`, region `BT`, `SaveGame::battles`, the clock rule) and the
  start of a battle (the attack and the setup). Left: the step and the end
  (§4: about 15 000 instructions).
- New, for the oracle: `HeapReAlloc` keeps only the requested bytes (§4).
- New decision: the battles' rocks depend on the town view (§4,
  «Decisions»).
- Correction: saves 6, 7 and tut4 have a sea battle saved (pirates against a
  merchant); the oracle's load and p3vita leave it out today. Piece 5.

## For the inventory

Part E, «Batallas y encuentros»:
- The pirates' pool and nest map: game memory (`SaveGame::pirate_nests`).
- 0x514B80, 0x515DA0, 0x513BF0, 0x514000, 0x513EC0, 0x514D40 (operation
  0xB5), 0x515750, 0x5185C0, 0x5062F0, 0x517170, the states 0x11 and 0x12
  of 0x506720 (0x506AF2, 0x507022, 0x507099), the pirate branch of
  0x5017F0, 0x5063C0, 0x513D80, 0x515EA0: rewritten.
- The sea battle: 0x50BC40, 0x611CD0, 0x603A70, 0x618F10, 0x605420
  (automatic path), 0x608230, 0x6079D0, 0x60F479, 0x619450, 0x61A1D0,
  0x6197A0, 0x61A4E0, 0x62125B, 0x61A750, 0x503AC0, 0x50CFB0, 0x5176C0,
  0x505B60, operation 0x94 (0x536BBF, without the step), 0x612C90,
  0x612D20: rewritten. The step (0x606CC0: method +0x14) and what it calls
  (0x6113C9, 0x6197F0 and the battle's method +0x0 0x608850 for two scene
  items, the scene ship's method +0x0 0x61A9E0, 3041 instructions, with
  0x6264C0, 0x61F1BF, 0x6271D7...), the end (method +0x1C 0x60D299, +0x4
  0x611740 → 0x603BC0, 0x50BFD0, 0x60C1C9): to do. Part D: «el ataque al
  barco 0x5063C0» is done (not connected).

## Game bugs

Certain:
- **0x515DA0 calls 0x4F9C80 with the type and the orientation swapped**:
  `0x4F9C80(&x, &y, orientation, 0x2A, 0)` where its arguments are (x, y,
  type, orientation, flag), while the next call is `0x4FB9C0(x, y, 0, 0x2A,
  orientation)`. So the search is for a site of type 0xE0 (always fails:
  ≥ 0x39) or 0x20 (a town hall site, class 0x1B), and 0x4FB9C0 then wants
  the bits of type 0x2A (mask 0xA4) in a cell of class 0x1B, which never has
  bit 2: **the nest's buildings are never put on its map**; only the entry
  in the pool (with the place) remains. Reproduced. Fix: swap the two
  arguments in the first call.

Looks like one:
- **0x5063C0's name loop never ends** when ([0x6DD88E] + [0x6DD890]) % 307
  is not below the number of names [0x6DDB78]: unlike 0x509A50, it doesn't
  store the new index inside the loop, so it computes the same value again
  and again. With the game's list (307 names or more) it can't happen. p3vita
  calls `fn_509a50`, which goes on.
- **A convoy of a pirate that goes away** (the pirates switched off in the
  options while a pirate convoy is at sea): its ships lose their hull
  (0x514000) but the convoy stays, without a guide, and 0x507A65 sorts ship
  0xFFFF in the list by x (0x50D0C0), reading outside the ships array (the
  emulator stops there). Seen only in a retouched state. Not reproduced:
  p3vita skips the sort.
- **0x61A9E0 tests the top edge with x**: of the four edges of the area a
  ship chasing an enemy keeps off, the second (y < 0x190, directions 0xC0 to
  0x3F) is tested as `x < 0x190`, like the first. So near the left edge the
  ship also avoids heading up, and near the top edge it doesn't. Also, only
  the first edge marks its directions by how close it is (0x190 − x); the
  other three mark 0xFF (the value they compute is unused). Reproduced.
- **0x60ADBF leaves the side it found unset when the segment meets no
  side of the hull**, and 0x608F70 then reads that byte of its stack, never
  written, as an index into 0x67AB98. It doesn't happen in the saves (the
  segment goes from the other ship's centre to a point where the hulls
  cross). p3vita starts it at 0 (`fn_608f70`).
- **0x60BCDA can hang**: its loops never end when the width of the item
  seen from the ship is 0 (a radius of 0: half the box + a − b = 0) and the
  level isn't, or when it is 0x100. Not seen in the saves. p3vita stops the
  loops (`fn_60bcda`), and returns where the game would divide by 0.
- **0x514B80 queues operation 0x12 (the captain improves) for the convoy's
  guide once per ship that has a captain**, instead of for each ship: the
  guide's captain improves up to n times and the others' never. Probably the
  ship was meant. Reproduced.

## Done and left

Done: parts 1, 2 and 3; of part 4, the start of an automatic sea battle
(the attack and the setup, §4). Left: the step and the end of a battle
(§4), then the other battles (pieces 4–7 of docs/cloud/battles.md).

Times of the checks here: the world 12 saves × 7 days, three batches of 4,
about 2–3 min each (133–194 s); the probes of a part in 6–12 saves × 7
days, 37–64 s; a retouched scenario in 4 saves, 6–12 s; `write-check.sh`
18 s; `build.sh` about 20 s; the Vita build 1 min 42 s (VitaSDK installed
with `scripts/setup-vitasdk.sh` in 11 s).

Built for the Vita: `vita_check.sh`, VPK built, no errors or warnings
(after the setup of a battle, 4e33692...).

Not run (long): the world with `--pirates --battles` (it differs by
construction until the step is rewritten).
