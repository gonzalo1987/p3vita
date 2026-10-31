# Oracle speed (cloud session `cloud/oracle-speed`)

Task: `p3vita-datos/tasks/oracle-speed.md`. Make the checks faster without
changing what is recorded: the `.probe` files must come out byte-identical
and `tick_probe` must print the same.

Status: **part 1 (measurement) and part 2, first round (regions, write_diff,
skipped snapshots, -O2) done and checked**; looking for more.

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

## Part 2: speedups (checked)

### What changed

- **`regions()`** (`tools/oracle/town_oracle.py`) reads each block from the
  emulator once and takes every pointer and count from the blocks it has
  already copied (`G` holds all the globals, `T` the towns, `P` the ships, `X`
  the sieges, `J` the armies), instead of one `u32` read per field; each list
  node (`L`, `H`, `N`, `F`) is read in one piece, data and next pointer
  together. Same keys, same order, same bytes. About 1130 `mem_read` per
  snapshot become a few hundred at most.
- **`write_diff`**: `changed_runs()` compares 4 KB blocks first and only goes
  64 bytes at a time inside the blocks that differ. Same chunks as before
  (checked against the old loop on 20000 random cases, and by the `.probe`
  files).
- **`Recorder` skips the "before" snapshot when nothing can have changed**
  since the last one, and reuses it. The conditions are exact and checked
  from the code bytes, not from a list of addresses: the last snapshot was
  taken when a function returned to code that only returns (`ret`, or
  `add esp` and `ret`) to `SENTINEL`, so that emulator run ended there; since
  then there is only one new emulator run, which up to the call is only
  `push dword [esp+n]` and the `call` to this very function, and this is the
  first recorded function entered in it; Python hasn't written outside the
  stack (the `Recorder` counts `uc.emu_start` and `uc.mem_write` by wrapping
  them on the `Uc` object) and `rand()` hasn't moved. That is the case of
  consecutive calls made from Python, above all `0x633A90` for each town:
  6501 of the 13541 snapshots of a day are skipped. With `P3_ORACLE_CHECK=1`
  the snapshot is taken anyway and compared (it raises if it differs).
- **The PC build is optimized by default**: `CMakeLists.txt` adds
  `-O2 -fwrapv -fno-strict-aliasing` when there is no build type (as with
  `build.sh`), keeping `assert()`. `-fwrapv` and `-fno-strict-aliasing` keep
  what `-O0` did with signed overflow and type punning, so the optimizer
  can't change a result. The Vita build sets `Release` itself and doesn't
  change.
- `save_oracle.py`: only a `STACK_SIZE` constant (used by the `Recorder`).

`retouch.py` uses `town_oracle.Recorder`, and `re/emu/sesion5/opsdiff.py` uses
`regions()` (it hashes the bytes, which are still `bytes`): both get the
speedup without changes.

### Byte-identity check

Reference: the scripts of `main` (9749d5b) copied aside, and `tick_probe` built
by `build.sh` before the change (`-O0`). New: this branch, first with the old
`tick_probe` and `P3_ORACLE_CHECK=1`, then without it and with the new
`tick_probe` (`-O2`). The `.probe` files compared with `cmp`, and
`tick_probe`'s output too:

| Recording | `.probe` | `tick_probe` output |
|---|---|---|
| saves 1, 3, 5, 7: 3 days, world.sh `FUNCS`, `--barcos --cpr`; `tick_probe --continuo --carga --cpr --max 8` | identical (4 of 4) | identical (4 of 4; 0 distintas) |
| saves 1, 3: `retouch.py ... 1 4d85c0,535760 robo` (as `ret2f.sh robo:1`); `tick_probe --cpr --max 8` | identical (2 of 2) | identical (2 of 2) |
| save 7: 2 days, world.sh `FUNCS`, `--barcos --cpr --desde 1 --sin-convoyes`; `tick_probe --cpr --max 8` | identical | identical |

The oracle's stdout (the sample count, and `retouch.py`'s summary) is also
the same. `P3_ORACLE_CHECK=1` found no skipped snapshot that differed from
memory in any of these runs. ctest 7 of 7.

### Times

Oracle and `tick_probe` per job, 4 jobs in parallel (the 4 vCPU):

| Job | Before: oracle | After | Before: `tick_probe` | After |
|---|---|---|---|---|
| saves 1/3/5/7, 3 days, `--barcos` | 144–150 s | 48–51 s | 93–97 s | 19–20 s |
| `robo`, 1 day | 4.9–5.4 s | 4.2–4.7 s | 1.4 s | 0.4–0.5 s |
| save 7, 2 days, `--desde 1` | 39.1 s | 15.6 s | 33.9 s | 7.8 s |
| whole set | 318 s | 94 s | | |

Timer breakdown of the oracle, save 1, 1 day, 2 in parallel (compare with
part 1):

| Piece | Before | After |
|---|---|---|
| total | 36.1 s | 12.7 s |
| `regions()` | 28.2 s (13541 snapshots) | 6.6 s (7040) |
| `write_diff` | 2.6 s | 1.6 s |
| `load()` | 1.25 s | 1.2 s |
| emulation and the rest | 3.6 s | 3.0 s |

**`SAVES='1 3 5 7' re/emu/sesion5/world.sh 7`: 580 s before, 166 s after**
(3.5 times faster), 0 «distintas» in the 4 saves both times, and the same
«saltadas» (1, 0, 5, 4). (580 s is less than the 17 min of the setup task
for other saves: this machine seems faster; both numbers are from this
session.)

### Found on the way (tool bug, not the game's)

- **The towns-only mode of the oracle fails on `main`**: without `--barcos`
  (as `scripts/tick-probe.sh` runs it when `P3_SHIPS` is unset),
  `town_oracle.py ... 2 527d40,51c830` stops at the first snapshot with
  `UC_ERR_READ_UNMAPPED` in the routes (`Q`): without navigation the ships'
  route pointers `[+0x24]` are the ones in the save, which point nowhere.
  Same error before and after this branch. Fix (not done, to keep the
  recordings identical): record `Q` only with `--barcos`, or clear `[+0x24]`
  on load without navigation.

## For the roadmap

(filled in at the end)

## For the inventory

Nothing: this task changes the checking tools only.

## Game bugs

None found (this task doesn't touch the simulation).
