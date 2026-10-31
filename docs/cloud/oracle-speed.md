# Oracle speed (cloud session `cloud/oracle-speed`)

Task: `p3vita-datos/tasks/oracle-speed.md`. Make the checks faster without
changing what is recorded: the `.probe` files must come out byte-identical
and `tick_probe` must print the same.

Status: **part 1 (measurement) done**; part 2 (speedups) written but **not checked yet** (commit "WIP"): regions() reads each block once, write_diff skips equal 4 KB blocks, the Recorder skips the "before" snapshot when nothing can have changed, and the PC build defaults to -O2.

## Part 1: where the time goes (before any change)

Machine: cloud, 4 vCPU, 15 GB, Python 3.13.18, unicorn 2.1.4, GCC 13.3.
p3vita `main` at 9749d5b. Default `FUNCS` of `re/emu/sesion5/world.sh`
(`51ba10,4f7bb0,506720,535760,5315c0,535450,52d7d0,6349f0,633a90,4d85c0`),
`--barcos /tmp/datos --cpr p2arch0_eng.cpr`, saves 1 and 3, **1 day**
(256 ticks, 6770 recorded calls, a 4.5–4.8 MB `.probe`).

Timers wrapped around each piece (no profiler; the script is a monkeypatch
of `town_oracle.py`, so it doesn't change what runs), 2 saves in parallel:

| Piece | Save 1 | Save 3 | Share |
|---|---|---|---|
| **`town_oracle.py` total** | **36.1 s** | **37.0 s** | 100% |
| `regions()` (13541 snapshots, 2 per recorded call) | 28.2 s | 28.8 s | 78% |
| `write_diff` (13540 calls) | 2.6 s | 2.8 s | 7% |
| `load()` (world, ships, mail, events, names) | 1.25 s | 1.2 s | 3% |
| Python hooks of `TownOracle` (`_sailing`, `_convoy`, `_stub`, `_ini_int`...) | 0.4 s | 0.4 s | 1% |
| the rest: emulation, the `Recorder` hooks (`hook_add`, `ctl_remove_cache`, `hook_del` per call), writing the `.probe`, reading the CPR | 3.6 s | 3.8 s | 10% |
| **`tick_probe --continuo --carga --cpr --max 8`** (as built by `build.sh`) | **29.6 s** | **30.7 s** | |
| `tick_probe`, same sources built with `-O2` (same output, byte for byte) | 5.9 s | 6.4 s | |

With 4 oracles in parallel (as `world.sh` runs them) the same day takes
about 50 s per save instead of 36 s: the 4 vCPU are not 4 free cores.

What the numbers say:
- **`regions()` dominates the oracle.** cProfile (which inflates small
  calls: 83 s instead of 36 s) shows why: each snapshot does about 1130
  `uc.mem_read` calls (15.4 million in the day), most of them 4-byte `u32`
  reads of pointers and counts that are already inside blocks the same
  snapshot copies whole (`G` holds all the globals, `T` the towns, `P` the
  ships, `X` the sieges). `ship(i)` alone is called 2.6 million times, and
  each call re-reads `[0x6DD7A4]`. Every `mem_read` allocates a ctypes buffer
  and copies it twice (`bytearray`, then `bytes`).
- Pure emulation is small: under cProfile `emu_start`'s own time is 5 s of
  83 s.
- **`tick_probe` is built without optimization.** `CMAKE_BUILD_TYPE` is empty
  in `~/build/p3vita-pc-check` (and `build.sh` doesn't set it), so
  everything is `-O0`. That makes `tick_probe` 45% of the time of a
  `world.sh` save; at `-O2` it is 5 times faster with identical output.
  (The Vita build already uses `Release`.)
- Of the 6770 recorded calls in a day, 6144 are `0x633a90` (24 towns × 256
  ticks), so whatever is paid per recorded call is paid mostly there.

cProfile of save 1 (sorted by own time): `mem_read` 25.7 s (15.4 M calls),
`create_string_buffer` 15.1 s, `regions` 7.5 s, `u32` 7.0 s, `emu_start`
5.1 s, `write_diff` 4.3 s, `isinstance` 3.7 s (inside the binding),
`add` 2.4 s.

## For the roadmap

(filled in at the end)

## For the inventory

Nothing: this task changes the checking tools only.

## Game bugs

None found (this task doesn't touch the simulation).
