# Oracle speed (cloud session `cloud/oracle-speed`)

Task: `p3vita-datos/tasks/oracle-speed.md`. Make the checks faster without
changing what is recorded: the `.probe` files must come out byte-identical
and `tick_probe` must print the same.

Status: **parts 1 and 2 done and checked** (two rounds). `world.sh 7` on
saves 1 3 5 7: 580 s before, 134 s after (4.3 times faster), 0 «distintas».

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

### Second round: direct reads and `tick_probe`'s comparison (checked)

- **`fast_reader()`** (`town_oracle.py`): `regions()` reads through
  `uc_mem_read` into one reused ctypes buffer and `ctypes.string_at` (one
  copy), instead of the binding's `mem_read` (a new zeroed buffer per call
  and two copies). It uses the binding's internals (`uclib`, `Uc._uch`) of
  unicorn 2.1.4, the version `prepare.sh` installs and the PC uses; with
  another binding it falls back to `uc.mem_read`, and on an error it repeats
  the read with `uc.mem_read` so the exception is the same. The `Recorder`
  reads the return address and the 8 arguments in one read. Small gain: a
  snapshot copies about 1.1 MB (700 KB of them are the 24 towns' `D` maps)
  in about 220 reads (70 blocks, 2 per ship with a route, the list heads),
  0.55 ms; the reads are 0.17 ms of it, the rest is Python.
- **`tick_probe`'s `compare()`** compared the whole state (about 1.1 MB)
  byte by byte after every call: 87% of `tick_probe`'s time at `-O2`.
  `next_difference()` now skips equal stretches 64 bytes at a time with
  `memcmp`; the differing bytes are handled exactly as before.

Check: the same 7 recordings as in the first round, compared with the
reference of `main` (the scripts of 9749d5b and the `-O0` `tick_probe`):
`.probe` 7 of 7 identical and `tick_probe` output 7 of 7 identical, both with
`P3_ORACLE_CHECK=1` and without it. ctest 7 of 7.

| Job (4 in parallel) | `main` | First round | Second round |
|---|---|---|---|
| saves 1/3/5/7, 3 days, `--barcos`: oracle | 144–150 s | 48–51 s | 46.5–47 s |
| same: `tick_probe` | 93–97 s | 19–20 s | 9.7–10.3 s |
| save 7, 2 days, `--desde 1`: oracle / `tick_probe` | 39.1 / 33.9 s | 15.6 / 7.8 s | 14.2 / 5.0 s |
| whole set | 318 s | 94 s | 76 s |
| **`SAVES='1 3 5 7' world.sh 7`** | **580 s** | **166 s** | **134 s** |

`world.sh 7`: 0 «distintas» and the same «saltadas» (1, 0, 5, 4) every time.

Where a save-day goes now (save 1, oracle alone, 2 in parallel): 11.8 s, of
which `regions()` 5.6 s (7040 snapshots), `write_diff` 1.5 s, `load()` 1.4 s,
the rest (emulation, hooks, writing) 2.8 s. A day with nothing recorded takes
3 s (load included): recording is about 9 of the 12 s. `tick_probe`: about
3.3 s per save-day.

### Found on the way (tool bug, not the game's)

- **The towns-only mode of the oracle fails on `main`**: without `--barcos`
  (as `scripts/tick-probe.sh` runs it when `P3_SHIPS` is unset),
  `town_oracle.py ... 2 527d40,51c830` stops at the first snapshot with
  `UC_ERR_READ_UNMAPPED` in the routes (`Q`): without navigation the ships'
  route pointers `[+0x24]` are the ones in the save, which point nowhere.
  Same error before and after this branch. Fix (not done, to keep the
  recordings identical): record `Q` only with `--barcos`, or clear `[+0x24]`
  on load without navigation.

### The other oracles and helpers

- `retouch.py` uses `town_oracle.Recorder` and `regions()`: it gets every
  speedup (checked with `robo:1`, identical).
- `re/emu/sesion5/opsdiff.py` uses `regions()` (md5 of the bytes, still
  `bytes`): faster without changes. The other helpers (`armies.py`,
  `cov_fn.py`, `events_seen.py`...) build a `TownOracle` and read a few
  fields: nothing to gain.
- `save_oracle.py` (0.6 s with its default run) and `sail_oracle.py` (4.2 s
  for 2048 ticks) write CSV with field-by-field reads and are not part of
  `world.sh`: not worth touching. (`sail_oracle.route()` reads the points one
  by one; one read would do, if it ever matters.)

## What else could be sped up (not done)

Estimates for `world.sh` on a save, where the oracle is now about 80% and
`tick_probe` about 15–20% of the time:

1. **Overlap `tick_probe` with the oracle**: `world.sh` runs them one after
   the other per save and then deletes the `.probe`. `tick_probe` reads the
   file sequentially, so a named pipe (`mkfifo`) would let it run on another
   core while the oracle records. On the PC (32 threads, 12 saves) that
   takes `tick_probe` out of the wall time: about 15% less for `world.sh`.
   Here, with 4 vCPU and 4 saves, little or nothing. It is a change to
   `re/emu/sesion5/world.sh` (the PC's), not to p3vita; it needs checking that
   `tick_probe` never seeks.
2. **`regions()` in C**: a snapshot is about 220 reads and 211 entries built
   in Python (0.55 ms, of which the reads are 0.17 ms). A small C helper (or
   a `ctypes` function compiled with the tools) could take it to about
   0.1 ms: 3–4 s of the 12 s of a save-day, about 25–30% of the oracle. Not
   done: it adds a compiled piece to the Python tools.
3. **Skip the "after" snapshot of calls that write nothing** (most calls to
   `0x633A90` probably change nothing): it would need to know that the
   emulated code wrote nothing outside the stack. Unicorn has no dirty-page
   API, and a `UC_HOOK_MEM_WRITE` hook sends every write through the slow
   path. Worth a measurement only if 2 is done and the snapshots still
   dominate: up to 40% of the remaining snapshots.
4. **`write_diff`**: 1.5 s per save-day (13540 calls); most of it is the
   equality test of 211 regions per call. Comparing only the regions that a
   cheaper test says may have changed would save maybe 1 s per save-day
   (8%).
5. **`tick_probe`**: what is left is the `memcmp` of the whole state after
   each call (55%) and `dump_save` (24%), about 3.3 s per save-day.
   Comparing only the regions the call changed (the `.probe` says which)
   could halve it: about 5% of `world.sh`.
6. `load()` is 1.4 s per run, once: nothing to gain in runs of days.

## For the roadmap

- Checks are about 4 times faster (`world.sh 7` on 4 saves: 580 s to 134 s
  here; on the PC, expect `world.sh 7` on the 12 saves well under the
  6.5 min of before). The «Cloud measurements» of the p3vita-datos README
  are from before this branch.
- Tool bug to fix (not the game's): the towns-only mode of `town_oracle.py`
  (no `--barcos`, as `scripts/tick-probe.sh` without `P3_SHIPS`) fails on the
  first snapshot (see «Found on the way»).
- The Vita build uses `Release` (`-O3`, with strict aliasing and no
  `-fwrapv`), while the PC builds that the oracle checks now use `-O2
  -fwrapv -fno-strict-aliasing` (before, `-O0`). If p3data has signed
  overflow or type punning somewhere, the Vita could compute something
  different from what is checked. Worth considering the same two flags for
  the Vita build (not checked: no VitaSDK here). What is certain is only that
  the PC results didn't change.
- Possible next speedups: «What else could be sped up», above.

For `docs/oracle.md` (not edited here; it is shared):
- `P3_ORACLE_CHECK=1`: the `Recorder` takes every skipped snapshot anyway and
  compares it (raises if it differs). Use it after changing the recording
  machinery or the trampolines.
- The PC build is optimized by default (`-O2 -fwrapv -fno-strict-aliasing`
  when there is no build type).

## Changes to shared files

- `CMakeLists.txt` (top level): the default `-O2 -fwrapv
  -fno-strict-aliasing` when there is no build type (10 lines). Applies to
  `build.sh`'s build and any build without `CMAKE_BUILD_TYPE`; not to the
  Vita (it sets `Release`) nor to MSVC.
- `tools/oracle/town_oracle.py`: only the recording machinery (`regions`,
  `write_diff` and the new `changed_runs`, `Recorder`, the new
  `fast_reader`, and `self.fast_read` in `TownOracle.__init__`). `OPERATIONS`,
  `EVENT_JUMPS` and the rest of the lists are untouched.
- `tools/oracle/save_oracle.py`: `STACK_SIZE` (used in its `mem_map`).
- `tools/oracle/tick_probe.cpp`: `next_difference()` and its use in
  `compare()`.
- None of the files of part B (`operations.cpp`, `simulation.cpp`,
  `events*.cpp`, `internal.hpp`, `retouch.py`) and no shared doc.

## Checks run and their time

- Measurement (part 1): 4 oracle runs of 1 day (plain and cProfile), 2 of
  `tick_probe` at `-O0` and 2 at `-O2`, the timer breakdown: about 5 min.
- Reference recordings with `main`'s scripts (7 jobs, 4 at a time): 318 s.
- Each byte-identity round (the 7 recordings, with `P3_ORACLE_CHECK=1` and
  without it): 223 s + 94 s in the first round, about 2 + 1.3 min in the
  second.
- `SAVES='1 3 5 7' world.sh 7`: 580 s (before), 166 s, 134 s.
- `build.sh` and ctest 7 of 7 before each push.
- **Not built for the Vita** (no VitaSDK in the cloud); the Vita build
  doesn't use anything changed here (the `CMakeLists.txt` default doesn't
  apply to it, and the tools aren't built for it).

## For the inventory

Nothing: this task changes the checking tools only.

## Game bugs

None found (this task doesn't touch the simulation).
