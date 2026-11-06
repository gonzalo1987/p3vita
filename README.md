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
X. The Market Hall is the market; your office (drawn where you have one)
opens the market with the office; the tavern hires sailors and captains; the
repair dock repairs your ships, and the shipyard has five pages (L and R, or
touching its title): repair, order a ship, cancel an order, upgrade a ship and
rename one (left and right go through the game's names; Square types another,
with the Vita's keyboard, and X ends it). The moneylender has six pages (L and
R): take out a loan, pay one off, grant one to a townsman (the interest with
left and right; only where you have an office), the loans you granted, and buy
and sell company shares. X with no building in the middle opens the market.
Touching a building of yours opens its window: demolish it or auction it, with
the game's questions. Square opens the Hanse's panel (the way to Alderman) and
Circle the messages.

**The side panel** on the right is the game's: the town's minimap (touch or
drag on it to move the view), its row of icons (the ship list, the game's
speed, the messages, building, leaving the town for the sea map and the menu),
its four ship buttons (your ships in port and at sea, your convoys in port and
at sea: each touch goes to the next one) and, under them, the room of its
menus. The tab on its border folds it and unfolds it. With the buttons, Select
goes onto it: left and right go along its icons and ship buttons, up and down
between both rows, X does it, Circle or Select leave it. While a finger is on a
button, or the pad points at it, its tooltip (the game's) shows. The top bar is
the game's too: the inhabitants, the town, your money, the date and the
hourglass, which opens the chronicle.

**The ship menu** (a ship button, or a ship of the ship list) has the game's six
pages, by its buttons or with L and R: the goods (the hold's barrel, each ware
with its average price; drag the list to move it), the crew (morale, the
weekly wage, the sailors with the least and the most it takes; dismissing
sailors and the captain, his skills), the deck (its pieces in their slots), the
convoys as the game's convoy window offers them (forming a private or a public
convoy, its destination with left and right, joining a convoy of the town,
showing it, and leaving or disbanding the one it is in) and the trade route
(not yet). Up and down choose a row, left and right the amount, X does it. Its header says where the ship
is or where it goes and in how many days, its speed, hull, crew and firepower.
The crew's «Pirate» button stays off until the battles in the towns.

**The messages** (the panel's scroll, or Circle): the public news, the official
and personal letters and the trade information, by the game's buttons or left
and right; touching a letter, or X, opens it on a parchment over the town (up
and down go through them, L and R through its text, Square deletes it). Select opens the letters'
settings, as the game's: how long each kind is kept, its notice and sound, and
the events' sound (up and down choose, left and right or X change, Circle closes
it, which sends them to the game).

**The statistics** (touching the money on the top bar, as in the game): the menu in the panel's room with the
game's four pages (for now the company overview and the competition: the towns' two are off) and the chosen entry
on a parchment over the town: the balance (income and expenditure of the last 4 weeks or 3, 6, 12 months), the
graphs of the company's wealth, its ships and its production (4 weeks, 6 months, 2 years, or the whole game after
its second year) and the goods' supply in a town of your offices; and the competition: the company value, loading
capacity and popularity of up to four merchants (6 months, 2 years or the whole game), with the averages of all of
them. Touch the tabs, the entries and the period's buttons; with the buttons, up and down choose the entry, L and R
the period (or the supply's town), Circle closes them. In the competition, a merchant's button under the graph
opens the list of the others, sorted by name or by the graph's value, to put another one in his place or to leave
it empty (with the buttons: Square goes to the next merchant's button, X opens its list).

**On the sea map** the panel stays: the sea's minimap (touch or drag to move the
map), the ship buttons (a ship at sea is chosen and centred; one in port takes
you to its town), the chosen ship's menu (Square takes the next ship) and the
messages. Building and leaving the town are off there, as in the game.

**Building** (the panel's hammer): the game's build menu with its five
categories (houses and warehouses, public buildings, military ones for the
mayor, and the two groups of businesses the town can produce); L and R, or
touching them, change the category, up and down and X (or a touch) choose a
building. Its page shows its costs, the days of works, the materials and why it
can't be built, as the game's. The streets, the well and the town wall have a
button of their own; any other building is placed in the town: the areas where
it fits are marked green (also on the minimap), touch where it goes and drag
it, green where it fits and red where it doesn't (with the pad, it moves cell
by cell), and its button or X builds it. Circle goes back to the menu, and
closes it.

In a building's window, up and down choose the row, left and right the amount
(L and R, ten at a time), X does it, Select takes the next ship and Circle
closes it; the rows and the buttons at its foot can be touched too. What each
window lists and sends is the game's (`src/p3data/player_actions.hpp`).

**The market**: the stock, the price of buying and selling a unit and what your
ship carries. Up and down choose the ware, left and right the amount (1, 5, 50
or all, as the game's window), X buys, Square sells, Select takes the next ship
in the port (or convoy of yours: it trades with your ships in it) and Circle
closes it. A row and the buttons at its foot can be touched too. The prices,
the limits (store, hold and money) and the average price of the cargo are the
game's. In a town where you have an office, L and R go through what is traded:
ship (or convoy) and town, office and town, ship (or convoy) and office, and the
ship's weapons and pieces with the office.

Triangle switches between the town and the **sea map**, with the towns and the
ships (yours has a mark; touching a town chooses it and Triangle goes into it).
On the sea map X sends your ship to the chosen town: it leaves the port, sails
the game's route (its course is drawn) and enters the port of destination,
where it can trade. Square chooses another of your ships and Select changes the
game's speed (slowed, very slow, slow, normal, fast, very fast and fast
forward). The bottom bar says what the ship does and when it will arrive.
Without a game loaded, Select goes through the town's template with all the
designer's buildings (`.editor`) and with the terrain only (`.town`).

On the PC: the arrows, Page Up / Page Down, Enter (go into the building, or set
sail), E (map), F (templates), M (market), C (buy), V (sell), H (Hanse), B
(mailbox), Tab, + and − (speed) and Esc.

## Thanks

The file formats and the game's mechanics are based on the reverse engineering
documentation of [P3Modding](https://github.com/P3Modding).
