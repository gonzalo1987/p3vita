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

## Decisions for the user

- None yet.

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

## For the roadmap

- (to fill)

## For the inventory

- (to fill)

## Game bugs

Certain:
- None yet.

Looks like one:
- None yet.

## Done and left

Done: part 1. Left: part 2 (0x514B80, 0x515DA0, the sea states, 0x517170,
the pirate branch of 0x5017F0, 0x5063C0, 0x514000 with operation 0xB5,
retouched states), part 3 (battle infrastructure) and part 4 (the battle).

Not built for the Vita yet.
