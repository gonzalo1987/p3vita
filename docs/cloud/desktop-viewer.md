# Cloud task: the viewer on the PC, screenshots and the living town

Branch `cloud/desktop-viewer`, from `main` at 9749d5b. Screenshots (game
graphics, so not in this repository): `screenshots/desktop-viewer/` in
p3vita-datos, branch `cloud/desktop-viewer`.

## Status

| Part | State |
|---|---|
| 1. The viewer builds and runs on the PC with desktop SDL2 | done, checked |
| 2. Headless screenshots (`--steps`, `--screenshot`) | done, checked |
| 3. The living town, first step | in progress |

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
`tap:X:Y` (the touch screen) and `shot:FILE`.

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

## 3. The living town

(in progress)

## Checks and times

- `build.sh` with SDL2 (builds the viewer too): ctest 7 of 7; without SDL2
  (separate folder): ctest 7 of 7.
- Vita: VitaSDK installed here with `scripts/setup-vitasdk.sh` (vitasdk.org is
  allowed now); `vita_check.sh` builds the VPK with no warnings (1,154,670
  bytes on `main`, 1,162,866 with parts 1 and 2). Not tried on a Vita.
- The simulation isn't changed by parts 1 and 2 (only `src/app` and the
  setup script), so no `world.sh`.
- Times: `prepare.sh` 24 s, VitaSDK 2–3 min, `vita_check.sh` 75 s the first
  time and 16 s after, a screenshot run 0.6–2.4 s.

## For the roadmap

- The viewer runs on the PC (with `libsdl2-dev`) and can be driven without a
  window to take screenshots: the UI no longer needs a VPK to be seen.

## For the inventory

Nothing (no simulation code).

## Game bugs

None found.

## Changes to shared files

None. Outside `src/app`: `scripts/setup-vitasdk.sh` (one line).
