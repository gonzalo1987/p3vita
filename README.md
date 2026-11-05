# p3vita

A free reimplementation of the engine of **Patrician III** (Ascaron, 2003) for
the PS Vita, with a PC build for development.

The repository holds none of the game's files. Playing needs your own copy;
the reference version is GOG's (1.1), the one the
[P3Modding](https://p3modding.github.io/) project documents.

The plan of work is in [docs/roadmap.md](docs/roadmap.md) (in Spanish).

## Building (PC, from WSL or Linux)

```sh
source scripts/env.sh
cmake -S . -B build
cmake --build build -j 8
(cd build && ctest --output-on-failure)
```

## Tools

`cprtool` lists and extracts the contents of the game's CPR archives:

```sh
build/tools/cprtool/cprtool info    p2arch0_eng.cpr
build/tools/cprtool/cprtool list    p2arch0_eng.cpr [filter]
build/tools/cprtool/cprtool extract p2arch0_eng.cpr out/ [filter]
```

`p3img` checks that every image (AIM, BMP, TGA and JPG) decodes and exports any
of them as a PNG. The AIM format, Ascaron's own, is documented in
[docs/formats/aim.md](docs/formats/aim.md).

```sh
build/tools/p3img/p3img check p2arch0_spa.cpr
build/tools/p3img/p3img png   p2arch0_spa.cpr images/Options/Hauptmenu.jpg menu.png
build/tools/p3img/p3img png   p2arch0_spa.cpr images/Schiffe/page_schiffe.aim ships.png
```

`p3town` draws a town's map (format in [docs/formats/town.md](docs/formats/town.md));
with a last number, scaled down:

```sh
build/tools/p3town/p3town p2arch0_spa.cpr 0 town0.png
build/tools/p3town/p3town p2arch0_spa.cpr 0.editor town0_editor.png 4
build/tools/p3town/p3town p2arch0_spa.cpr 'missav/1.pat#3' town3_1300.png 4
```

The last line draws the town 3 of a saved game as it is in it (format in
[docs/formats/savegame.md](docs/formats/savegame.md)).

`p3save` lists the towns of a save and shows each one's store, its merchants
and its ships; `p3save texts EXE` prints the game's texts p3vita reads from an
executable:

```sh
build/tools/p3save/p3save p2arch0_spa.cpr missav/1.pat
build/tools/p3save/p3save p2arch0_spa.cpr missav/1.pat 9
build/tools/p3save/p3save p2arch0_spa.cpr missav/1.pat comerciantes
build/tools/p3save/p3save p2arch0_spa.cpr missav/1.pat barcos
```

`p3map` draws a save's sea map as a PNG:

```sh
build/tools/p3map/p3map p2arch0_eng.cpr missav/1.pat map.png p2arch0_spa.cpr
```

## Building for the Vita

First install VitaSDK (once, without sudo, in `~/vitasdk`):

```sh
bash scripts/setup-vitasdk.sh
```

Then each build leaves the VPK in `dist/p3vita.vpk`:

```sh
bash scripts/build-vita.sh
```

To try it on the console (with HENkaku or Enso):

1. Install `p3vita.vpk` with VitaShell.
2. Copy `p2arch0_eng.cpr` from your installation of the game to
   `ux0:data/p3vita/`; it is the base of every version. For the Spanish version
   copy `p2arch0_spa.cpr` too, which is mounted on top (Spanish texts, names and
   fonts). With only the Spanish one it starts, but data are missing.
3. Copy `Patrician3.exe` of the same installation to `ux0:data/p3vita/` too: the
   interface's texts come from it (the Spanish version's, to have them in
   Spanish; GOG 1.1's English one works as well). Without it the viewer uses its
   own texts. To be able to choose English in the options, also copy the English
   version's `Patrician3.exe` named `Patrician3_eng.exe` (the Spanish one may be
   `Patrician3.exe` or `Patrician3_spa.exe`). The first time it starts with each
   language, the viewer takes the texts it uses out of the exe and keeps them in
   a small file (`p3vita-texts-es.bin`, `p3vita-texts-en.bin`); afterwards it
   reads only that file.

## The viewer

For now it is a viewer of the towns where you can already trade, sail and
look after your ship. It starts in Lübeck as the game of 1300 starts
(`missav/1.pat`), with the player's merchant (Georg Stromberg, 30,000 coins) and
a ship in the port. Time passes as in the game at normal speed (a day a
minute) and the bar shows the date. The D-pad or the left stick move the camera,
dragging a finger scrolls the map, L and R change the town and Start opens the
menu: continue, the mailbox, save and load the game (8 slots, in
`ux0:data/p3vita/savegames/`), the options (fixing the game's bugs, and the
language, Spanish or English) and quit; while it is open the game stops. The
hints on the screen show the buttons with their symbols (✕, ○, □, △); the rear
touch pad isn't used.

**In a town**, as in the game, you go into its buildings: touch one, or bring it
to the middle of the screen (it glows and shows its name, the game's) and press
X. The Market Hall is the market; the tavern hires sailors and captains; the
shipyard and the repair dock repair your ships. X with no building in the
middle opens the market. Your ship in the port has a card at the bottom left
(touch it, or press Select) with its window: its captain, sailors and hull, and
dismissing sailors or the captain. Square opens the Hanse's panel (the way to
Alderman) and Circle the mailbox.

In a building's window, up and down choose the row, left and right the amount
(L and R, ten at a time), X does it, Select takes the next ship and Circle
closes it; the rows and the buttons at its foot can be touched too. What each
window lists and sends is the game's (`src/p3data/player_actions.hpp`).

**The market**: the stock, the price of buying and selling a unit and what your
ship carries. Up and down choose the ware, left and right the amount (1, 5, 50
or all, as the game's window), X buys, Square sells, Select takes the next ship
in the port and Circle closes it. A row and the buttons at its foot can be
touched too. The prices, the limits (store, hold and money) and the average
price of the cargo are the game's.

Triangle switches between the town and the **sea map**, with the towns and the
ships (yours has a mark; touching a town chooses it and Triangle goes into it).
On the sea map X sends your ship to the chosen town: it leaves the port, sails
the game's route (its course is drawn) and enters the port of destination,
where it can trade. Square chooses another of your ships and Select changes the
game's speed (slowed, very slow, slow, normal, fast, very fast and fast
forward). The bottom bar says what the ship does and when it will arrive. In a
town where you have no ship, Select goes through the town's template with all
the designer's buildings (`.editor`) and with the terrain only (`.town`).

On the PC: the arrows, Page Up / Page Down, Enter (go into the building, or set
sail), E (map), F (templates), M (market), C (buy), V (sell), H (Hanse), B
(mailbox), Tab, + and − (speed) and Esc.

## Thanks

The file formats and the game's mechanics are based on the reverse engineering
documentation of [P3Modding](https://github.com/P3Modding).
