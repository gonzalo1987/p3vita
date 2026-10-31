# Cloud task: the viewer on the PC, screenshots and the living town

Branch `cloud/desktop-viewer`, from `main` at 9749d5b. Screenshots (game
graphics, so not in this repository): `screenshots/desktop-viewer/` in
p3vita-datos, branch `cloud/desktop-viewer`.

## Status

| Part | State |
|---|---|
| 1. The viewer builds and runs on the PC with desktop SDL2 | done, checked |
| 2. Headless screenshots (`--steps`, `--screenshot`) | done, checked |
| 3. The living town, first step | done, checked |

## 1. The viewer on the PC

What was there already worked: with `libsdl2-dev` installed (Ubuntu 24.04,
SDL 2.30.0), `find_package(SDL2 CONFIG)` finds it and `src/app` builds with
no errors or warnings; `environment.md` in p3vita-datos expected it to break
the PC build, but it doesn't. What changed:

- The window falls back to any renderer when there is no accelerated one
  (`SDL_VIDEODRIVER=dummy` or `offscreen` used to quit at start).
- On the PC the mouse acts as the touch screen (`SDL_HINT_MOUSE_TOUCH_EVENTS`):
  drag to scroll, click a town on the sea map, click market rows and buttons.
- Errors also go to the console on the PC, and there are no message boxes
  without a window.
- `--cpr FILE` gives the English archive itself (the Spanish one is mounted if
  it is next to it). The viewer already started with `p2arch0_eng.cpr` alone:
  `p2arch0_spa.cpr` is optional, and the town names come out in English
  («Luebeck»).
- `scripts/setup-vitasdk.sh` answers vdpm's `[Y/n]` question itself: without
  a terminal (cloud sessions) `vdpm install sdl2` stopped there.

PC build: with SDL2 the viewer is built too (`build.sh` builds it in
`~/build/p3vita-pc-check`); without SDL2 CMake says «SDL2 no encontrado» and
builds the rest as before. Both give ctest 7 of 7 (without SDL2 checked with
`-DCMAKE_DISABLE_FIND_PACKAGE_SDL2=TRUE` in a separate folder).

## 2. Screenshots without a window

```sh
p3vita --cpr p2arch0_eng.cpr --steps "X,shot:market.png,Circle,Triangle,R,R,X,days:6" --screenshot arrived.png
```

With `--steps` or `--screenshot` the viewer runs without a window (SDL's
`dummy` video driver and the software renderer drawing into a surface), plays
the steps one frame at a time and quits. `p3vita --help` lists the steps:
the Vita's buttons (`X`, `Circle`, `Square`, `Triangle`, `L`, `R`, `Select`,
`Start`, `Up`, `Down`, `Left`, `Right`), `BUTTON*N` to hold one N frames (the
pad moves the camera), `frames:N`, `days:N` (fast forward, one day a frame),
`tap:X:Y` (the touch screen), `cell:X:Y` (center the camera on a town cell:
not a Vita input, to frame screenshots) and `shot:FILE`.

The buttons go in as SDL controller events and the taps as finger events, so
they go through the same code as on the Vita. The time is the script's: a
frame is 1/60 s of game time, whatever the machine takes, so every run comes
out the same (`app_ticks()` replaces `SDL_GetTicks()` in the viewer). Code:
`src/app/script.{hpp,cpp}` (steps and PNG); `main.cpp` calls it at the start
and end of each frame.

A run of a few hundred frames takes 1–2 s here.

### What the screenshots show (the HANDOFF's «sin confirmar»)

All in save `missav/1.pat` (13 May 1300), English CPR:

- **The save's buildings**: Lübeck and Bergen with their houses, market
  stalls, cranes, piers, warehouses and statue; nothing floating or cut off.
  Not compared with the original game's screen (it doesn't run here).
- **Text bar**: date, town and inhabitants on the left, the controls on the
  right; it fits, accents included («Triángulo»).
- **Market panel**: fits on the screen (bottom at about y = 525); stock, buy
  and sell prices, the ship's hold. Buying 5 loads of fish in Lübeck: 2,984
  coins (30,000 → 27,016); selling them in Bergen: 2,095. Touch: tapping a
  row, the amount button (1 → 5) and «comprar» buys 5 barrels of salt for 148.
- **Sea map**: towns with their names, the player's town in blue, the ships
  at sea with their heading, the player's ship marker.
- **Navigation**: X sends Ermanrich from Lübeck to Bergen (route drawn,
  bottom bar «saliendo de Luebeck hacia Bergen», then «llegada prevista el 18
  de mayo»); it arrives (notice «Ermanrich ha llegado a Bergen.», Bergen stays
  selected), Triangle enters Bergen and X opens its market. Select goes
  through the speeds; tapping Oslo selects it.
- **Template views** (Select in a town): the designer's buildings and the
  terrain.

Looks like a problem (not certain):
- At the top and bottom edges of the sea map a striped band is seen (above
  Bergen, below Torun). It seems to be part of `Vollansichtskarte.bmp` that
  the game hides under its interface; the viewer could stop the camera before
  it.

## 3. The living town (first step)

The town on screen is drawn from its current state and drawn again when it
changes:

- `place_saved_buildings` (`src/p3data/town_buildings.cpp`) reads what the
  game draws (0x521580) from the town's memory, not from the copy taken at
  load time: the heads of the record lists [0x762] (works in progress),
  [0x764] (buildings), [0x768] (fortifications) and [0x76A] (what burns), the
  fixed points [0x7B0..0x80B] (lighthouses, cranes, market stalls,
  shipyards), the flags [0x76C] and the church [0x7A3]. Right after loading
  it is the same as before: the object layer of the 285 towns of the 12
  saves comes out byte for byte the same as on `main` (`p3town ... .town`).
- `drawn_buildings_key` hashes everything that function reads (5 µs a town
  here); `load_saved_town_template` and `place_saved_town` split the loading
  of the template (.town, .editor, region) from placing the buildings, so
  that they can be placed again without reading the archive (2 ms here).
- The viewer (`LiveTown` in `src/app/main.cpp`): after each frame in which
  ticks have passed, if the key of the town on screen has changed, it places
  the buildings again and rebuilds the sprites, with the camera where it was.
  Entering the town again also draws it as it is now.
- `tests/buildings_test.cpp`: the heads and fixed points now come from the
  memory; new checks: a building put at the head of [0x764] (as the
  simulation does) is drawn, and the key changes with it but not with a free
  record.

Screenshots (`screenshots/desktop-viewer/part3-living-town/`): Edinburgh
(save 1, town 0), steps `L` ×9 from Lübeck, then `days:N`:
- day 2: a well (type 0x28) appears at (76, 92), started that day (works
  list [0x762]); a house at (83, 88) has changed variant (type 0x18 → 0x19,
  day 1).
- day 3: the wall section 0x36 at (104, 69) becomes a tower 0x32 under
  construction.
- day 8, after going to the sea map and back: the same, drawn from the
  current state (pixel for pixel the same as the live redraw).

What changes in the towns of save 1 in 40 days (with a scratch program on the
p3data simulation): wells started and finished (about 5 days of work) in 12
towns at once, ruined towers rebuilt, new houses started, and houses
changing variant within their class (0x18 → 0x19, 0x1C → 0x1D...) almost
every day somewhere. Lübeck changes little (a house variant on day 15, a new
rich house started on day 16).

Pending (not in this first step):
- **Scaffolding**: what is in [0x762] (and has `pending` ≠ 0) is drawn as the
  finished building (houses with variant 0, as before). The game shows the
  works; to check with the original game what it draws and how it advances.
- **Fires** ([0x76A]) are drawn as the normal building; no smoke or flames.
- **Animations, people, carts, ships in the port, the siege on screen.**
- **Cost on the Vita**: a redraw is the whole town (2 ms of placing here plus
  rebuilding the sprite list); at fast forward the town on screen can change
  every few frames. If it stutters on the Vita, redraw only the cells that
  change.
- `SavedTown::drawn_lists` and `SavedTown::fixed` are no longer used for
  drawing (the loader still fills them). Left in `savegame.hpp`, which the
  local and savegames sessions also touch: they can go when merging.

## Checks and times

- `build.sh` with SDL2 (builds the viewer too): ctest 7 of 7; without SDL2
  (separate folder): ctest 7 of 7.
- Vita: VitaSDK installed here with `scripts/setup-vitasdk.sh` (vitasdk.org is
  allowed now); `vita_check.sh` builds the VPK with no warnings (1,154,670
  bytes on `main`, 1,162,866 with parts 1 and 2). Not tried on a Vita.
- No code the simulation runs has changed (`src/app`, the setup script and
  `town_buildings.cpp`, which only the viewer, `p3town` and the tests call),
  so no `world.sh`.
- Part 3: the object layer of all 285 saved towns of the 12 saves, the same
  as on `main` (byte for byte); ctest 7 of 7 with and without SDL2;
  `vita_check.sh`: VPK of 1,165,565 bytes, no warnings.
- Times: `prepare.sh` 24 s, VitaSDK 2–3 min, `vita_check.sh` 75 s the first
  time and 16 s after, a screenshot run 0.6–2.4 s.

## For the roadmap

- The viewer runs on the PC (with `libsdl2-dev`) and can be driven without a
  window to take screenshots: the UI no longer needs a VPK to be seen.
- Phase 2, «La ciudad viva en pantalla»: first step done (the town drawn from
  its memory and drawn again when its buildings change, also on entering it
  again). Left: scaffolding, fires, animations, people, the siege on screen,
  and redrawing only what changes if the Vita needs it (see above).

## For the inventory

Nothing (no simulation code).

## Game bugs

None found.

## Changes to shared files

None. Outside `src/app`: `scripts/setup-vitasdk.sh` (one line),
`src/p3data/town_buildings.{hpp,cpp}` and `tests/buildings_test.cpp`.
