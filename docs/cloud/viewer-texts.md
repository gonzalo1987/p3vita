# Cloud task: the rest of the viewer with the game's texts

Branch `cloud/viewer-texts`, from `main` at 8336a57. Screenshots (game
graphics): `screenshots/viewer-texts/` in p3vita-datos, branch
`cloud/viewer-texts`.

## Summary

The market, the bars and the sea map use the game's interface texts read
from `Patrician3.exe` (`game_texts`, as the Hanse panel of
`cloud/alderman-viewer-2`).

| Where | The game's texts now used |
|---|---|
| Market | Its 20 ware names, the columns («Mercancías», «Ciudad», «Comprar», «Vender», «Barco»), the amounts («%.0f toneladas», «%.0f barriles», «%.0f piezas»; «%.1f toneladas» for the hold), the ship types and «¡No tienes dinero suficiente!» |
| Town bar | The game's date («%u de %s de %u», months included) and «%.0f habitantes» |
| Sea map | The ship types, «En el mar», «hundido», «El %s ha llegado a su destino.», the four reasons why a ship can't sail, «Velocidad del juego» and the game's six speed names |
| Hanse panel | Its road materials and dates, with the same helpers |

Without the executable the viewer uses its own Spanish texts, as before.

These texts were added to `GameText`, with where they are in the English
GOG 1.1 and Spanish builds. Each address was checked by reading the text in
both executables: the screenshots show them in Spanish and in English.

Checks:
- ctest 11 of 11;
- `vita_check.sh`: VPK of 1,274,496 bytes, no warnings;
- screenshots checked by looking at them.

No `world.sh`: only the viewer and the text loader changed, nothing the
simulation runs.

## Decisions for the user

1. **The viewer's own sentences have no game text.** They stay in Spanish
   even with the English exe, so in English the screen mixes both languages
   (screenshots 05–07: «Compra: 5 load de fish por 2.984 monedas»). They
   are:
   - the Vita's button hints («L/R: ciudad   Triángulo: mapa…»);
   - «Mapa del mar» and the template views;
   - the market's result messages («Compra:/Venta: …», «No llevas…», «La
     ciudad no tiene…», «No queda sitio en la bodega.», «Esta ciudad no
     tiene mercado.», «No tienes ningún barco en este puerto.»);
   - the ship's activity («en el puerto de», «saliendo de… hacia…»,
     «navegando hacia…, llegada prevista el…»);
   - «zarpa hacia», «ya está en», «se ha detenido en el mar», «El puerto
     está bloqueado.»;
   - «monedas» and «avance rápido».

   **Recommendation**: keep them Spanish for the Vita (the user's game is
   Spanish), or, if the English build matters, a small table of the viewer's
   own sentences in two languages chosen by the exe's build. That table
   would be p3vita's own text, not the game's.
2. **The units in Spanish**: the Spanish game calls a load «tonelada» in
   its amounts («%.0f toneladas») and «fardo» elsewhere; the viewer said
   «cargas». The ships are «Goleta», «Carabela», «Nao» and «Galeón», not
   Snaikka, Craier, Kogge and Holk as the viewer wrote them. Now the screen
   follows the game. **Recommendation**: none to decide, just be aware of it.
3. **To check — the ship's name.** Since the loader's change «ship names»
   (91b958b, `cloud/savegames`), the player's first ship in save 1 is called
   «Constance» instead of «Ermanrich», with or without the exe. It isn't
   this task's change. **Recommendation**: check it against the game (the
   oracle of saves) in the savegames branch's follow-up.

## What was done

- `src/p3data/game_texts.{hpp,cpp}` has 50 more texts: wares, ship types,
  the market's columns and amounts, «Not enough money!», inhabitants, the
  date, «at sea», «Sunk», the arrival, the four sail errors, the game speed
  and its six names. Also:
  - `%f` in the formats;
  - spaces trimmed around a text: the Spanish «  Comprar» has two leading
    ones.
- `src/app/main.cpp`:
  - helpers: `g_texts` (set when the exe is loaded) with `game_text`,
    `month_name`, `game_date`, `amount_label`, `ware_name`/`ware_label`,
    `ship_type_name`, `speed_name`;
  - the market, the bars, the sea map's notices and the road materials use
    them;
  - the market's columns are a little wider (the Spanish «toneladas» and
    «Comprar» are longer).

### Where the texts are

They were found the same way as part 2's:
- texts with codes or formats: by the sequence of signatures around them;
- plain texts (ware and ship names, the columns, the speeds, «at sea»): by
  content in the Spanish exe, and by the order of their table, then checked
  by reading them.

| Text | English | Spanish |
|---|---|---|
| Wares (20) | 0x6C0BB0 | 0x6C0D70 |
| Ship types | 0x6C0CC4 | 0x6C0E80 |
| Trade window columns | 0x699880, 0x69988C, 0x699894, 0x6998A0, 0x6998AC | 0x698D10, 0x698D1C, 0x698D2C, 0x698D3C, 0x698D48 |
| Amounts | 0x6C1ECC.. | 0x6C249C.. |
| «at sea», «Sunk» | 0x699914, 0x699940 | 0x698DCC, 0x698DFC |
| Speeds | 0x69A934.. | 0x69A04C.. |

The speeds are in the same order but translated freely: «very slow» →
«Lenta», «slow» → «Moderada», «slowdown» → «Muy lenta».

The game's date format («%u. %s %u"», 0x69DA34) ends in a quote: the viewer
removes it.

The arrival notice is the game's «The %s has reached its destination.» with
«: town» added, so that it says where.

## Checks and times

- `build.sh`: ctest 11 of 11. `vita_check.sh`: 1,274,496 bytes, no
  warnings. Not tried on a Vita.
- Screenshots, about 1 s each run.

## For the roadmap

- The viewer's interface uses the game's texts where the game has them
  (Hanse panel, market, bars, sea map). Left: the viewer's own sentences
  (decision 1).

## For the inventory

Nothing (no simulation code).

## Game bugs

None found.

## Changes to shared files

None.
