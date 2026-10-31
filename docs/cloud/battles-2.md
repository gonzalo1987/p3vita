# Battles, part 2 (cloud/battles-2)

Task `tasks/battles-2.md` of p3vita-datos: the pirates' pool, the rest of
piece 2 of `docs/cloud/battles.md` (the pirates moving) and piece 3 (the
automatic sea battle).

## Summary

- **Part 1, the pirates' pool (done):** the heap blocks of the 5 pirate
  objects, the pool of 8-byte entries ([+0x4], [+0x8], [+0xA]) and the map of
  the nest ([+0x0], a town_class1 object with its cells), are now game memory
  in `SaveGame::pirate_nests` (`src/p3data/battle/pirate_nests.*`), loaded
  as 0x5138B0/0x4F9770 do, written back as 0x5137E0/0x4F9680 do, and recorded
  by the oracle (regions `ZMi`, `ZDi`, `ZPi`) and compared by tick_probe.
  Checked: `write-check.sh` the 12 saves as before (0 different in 11, the
  12 known ones of the battles section in save 7); the world 12 saves × 7
  days, 0 «distintas», by default and with `--pirates` (the load comparison
  includes the new regions; 1 to 3 pirates with a pool in every save, no
  nest map in any).

- **Part 2 (in progress): the disbanding at the nest (done):** 0x514B80,
  0x515DA0 and 0x513BF0 rewritten (`battle/pirates.cpp`,
  `battle/pirate_nests.cpp`); the third jump of the provisional rule
  (0x505FF1) is gone with `--pirates` (`--no-disband` keeps it). Checked call
  by call: the 12 saves' pirate convoys, 7 days (8 calls of 0x514B80 in 6
  saves, 1097 of 0x505C90), and the retouched states of
  `tools/oracle/retouch_pirates.py` (`disband`, `disband_map`, `plan`: every
  block of the three functions), 0 differences; the world with `--pirates`
  in saves 3, 4, 5, 6 × 7 days, 0 «distintas».

- **Part 2: the states 0x11 and 0x12 and the pirate branch of 0x5017F0
  (done):** the lost ship (state 0x11 in port, 0x506AF2 with 0x517170, and
  at sea, 0x507022), the pirate alone at sea (0x12, 0x507099, with 0x5062F0)
  behind `--pirates`, and the pirate branch of 0x5017F0 (always). The saves
  don't reach them; retouched states `lost`, `alone`, `guide`: 0 differences
  (0x506720 256 calls per save and day, 0x5062F0, 0x517170, 0x5017F0 up to
  15 calls per save).

- **Part 2: the pirate objects' day (done):** 0x514000 (daily, behind
  `--pirates`), 0x513EC0, and operation 0xB5 (0x514D40, with 0x515750 and
  0x5185C0: a convoy leaves the nest) in `battle/pirate_objects.cpp`; the
  oracle runs 0x514000 (no longer quiet) and executes 0xB5 with `--pirates`.
  Call by call in the 12 saves × 7 days: 189 calls of 0x514000, 6 of
  0x514D40, 0 differences; the world with `--pirates`, saves 3, 5, 6, 7 × 7
  days, 0 «distintas»; retouched `daily`, `leave`, `retire`: every reachable
  branch, 0 differences.

## Decisions for the user

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
- `src/p3data/CMakeLists.txt`: the new file.
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
- `src/p3data/convoys_members.cpp`: the pirate branch of `fn_5017f0` and the
  declaration of `fn_5097c0` (`town_tick/events.cpp`).
- Declared in my files (not changed): `fn_4f9c80`
  (`town_tick/construction.cpp`), `fn_51a750` (`town_tick/alderman.cpp`).

## For the roadmap

- (to fill)

## For the inventory

- (to fill)

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
- **A convoy of a pirate that goes away** (the pirates switched off in the
  options while a pirate convoy is at sea): its ships lose their hull
  (0x514000) but the convoy stays, without a guide, and 0x507A65 sorts ship
  0xFFFF in the list by x (0x50D0C0), reading outside the ships array (the
  emulator stops there). Seen only in a retouched state. Not reproduced:
  p3vita skips the sort.
- **0x514B80 queues operation 0x12 (the captain improves) for the convoy's
  guide once per ship that has a captain**, instead of for each ship: the
  guide's captain improves up to n times and the others' never. Probably the
  ship was meant. Reproduced.

## Done and left

Done: part 1; of part 2, the disbanding (§2.1), the states 0x11 and 0x12
and 0x5017F0 (§2.2), the pirate objects' day (§2.3). Left: part 2 (0x5063C0,
retouched states), part 3 (battle infrastructure) and part 4 (the battle).

Not built for the Vita yet.
