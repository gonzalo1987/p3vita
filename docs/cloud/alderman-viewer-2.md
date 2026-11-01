# Cloud task: the path to Alderman in the viewer, part 2

Branch `cloud/alderman-viewer-2`, from `main` at c3d6740. Screenshots (game
graphics): `screenshots/alderman-viewer-2/` in p3vita-datos, branch
`cloud/alderman-viewer-2`.

## Summary

- **The election with the player as a candidate** (part 1): p3vita is right.
  With the player at level 4 and the election moved to the next day, the
  player **wins** in saves 1 and 3, in the game and in p3vita:
  `retouch_player.py election_level4`, recording 4d85c0 and 535760, gives 0
  differences. In part 1's screenshot the player lost because that election
  came 117 days later, with the state changed by then. New debug state
  `election-tomorrow`.
- **The player's vote** (part 2) is **operation 0x3F** (0x53AA10). The Hanse
  window queues it (0x5E8280) as `{0x3F, player, row}`: the candidate 0..3
  in the Alderman election, or yes, no, abstain in a vote of the diet.
  - **Rewritten** in a new file, `src/p3data/town_tick/votes.cpp`, with
    0x4D9C70 (today's election or vote) and 0x4D9D40 (whether the player can
    vote).
  - **Checked**: 9 scenarios (own candidate, another, twice, early, late,
    rank 3, wrong row, diet yes, no, abstain) × saves 1 and 3: **0
    differences**.
  - **Not active yet in p3vita**: `run_operation` is the local session's,
    so the case that calls it is only described below (decision 1); for the
    checks it was added locally and taken out again.
- **The candidates** (part 3): the election page of the panel lists them:
  - before the election, as the game's window does: 0x529700, with the
    game's «Los siguientes alcaldes han presentado su candidatura…»;
  - on the day, from the election's event, with their votes;
  - the player votes with X.
- **Accepting the candidacy** (part 3) has no question for the Alderman:
  0x4F75F0 makes the mayor a candidate by itself. The game's «Accept
  candidature?» belongs to the town hall of the player's home town (the
  mayor's election): see decision 2.
- **The end** (part 4): when the player is elected, a window with the game's
  texts («Oficina del Gobernador», «Georg Stromberg de Luebeck ha sido
  elegido Gobernador de la Liga Hanseática por 15 votos.»). The `eldermann`
  video comes later, with the Bink videos.
- **The game's interface texts** (part 5): `src/p3data/game_texts` reads them
  at run time from the `Patrician3.exe` of the installation. p3vita keeps
  only where each one is in the two known builds (English GOG 1.1, Spanish),
  never the texts. They are used in the Hanse panel:
  - the tasks on offer and each one's description;
  - the Hanse's answer when a task is taken;
  - the election page and the end window.

  Without the exe the panel uses the viewer's own texts.
- **Checks**:
  - ctest 10 of 10 (new: `game_texts_test`);
  - `vita_check.sh`: VPK of 1,239,047 bytes, no warnings;
  - screenshots checked by looking at them.

  No `world.sh`: nothing the simulation already runs has changed (`votes.cpp`
  isn't called by it).

## Decisions for the user

1. **Add operation 0x3F to `run_operation`** (simulation.cpp, the local
   session's). Without it, the vote the viewer queues does nothing in
   p3vita. The exact change, checked with the oracle (0 differences in the 9
   scenarios):

   ```diff
   --- a/src/p3data/simulation.cpp
   +++ b/src/p3data/simulation.cpp
   @@ -3,6 +3,7 @@
    #include <cstring>
    
    #include "p3data/town_tick/internal.hpp"
   +#include "p3data/town_tick/votes.hpp"
    
    namespace p3::data {
    
   @@ -127,6 +128,9 @@ void run_operation(SaveGame& save, Sailing* sailing, const uint8_t* operation) {
    	case 0x38:  // Una subasta de barco: el evento 0x0A (0x535E3B).
    		tick::fn_535e3b(save, operation);
    		break;
   +	case 0x3F:  // The vote of a human player in an election or the diet (0x53AA10).
   +		tick::fn_53aa10(save, operation);
   +		break;
    	case 0x3B:  // Materiales para la carretera de un encargo de la Hansa (0x53A3E0).
    		tick::fn_53a3e0(save, operation);
    		break;
   ```

   and 0x3F in the list of `run_operation`'s comment in simulation.hpp and in
   `OPERATIONS` of `town_oracle.py`. `retouch_player.py` adds it at run time
   until then. **Recommendation**: add it when merging.
2. **«Accept candidature?»** isn't part of the path to Alderman. The game
   shows it in the town hall page of the Hanse window, only for the
   player's home town ([+0x18E8] = home town, 0x5E387E), with two buttons
   ([+0x604], [+0x6EC]). It is next to the town council's texts (taxes),
   which suggests the candidacy for mayor. I didn't find what those buttons
   queue: they aren't checked like the others (0x4C78B0) in the window's
   update functions. **Recommendation**: a short task on the mayor's election
   for the player (event 0x02, its candidates [0x6D4..0x6E7] and
   supporters [0x6DC]), with this question.
3. **The texts in the rest of the viewer.** Only the Hanse panel uses the
   game's texts for now; the rest of the viewer (market, bar, sea map) keeps
   its Spanish strings. With the English exe the panel mixes both languages
   (screenshot 04). **Recommendation**: move the rest to game texts in a
   later task, the same way (find the text, map its address in both builds
   with the matcher described below).
4. **The Vita needs the executable**: copy `Patrician3.exe` from the game's
   installation folder (the Spanish one for Spanish texts) to
   `ux0:data/p3vita/`, next to `p2arch0_eng.cpr` and `p2arch0_spa.cpr` (with
   VitaShell, as the CPRs). On the PC: next to the CPR, or `--exe FILE`.
   **Recommendation**: add this to the README's Vita section when merging.

## 1. The election with the player at level 4

`retouch_player.py election_level4`:
- puts the player as `--debug-state level4` leaves it: mayor of its home
  town, the mayor's rank 6 at home, the third component of its reputation
  at 20 in every town, [+0x37] = 4, the home town with [0x2C8] & 0x40;
- moves the election (event 0x20) to the first phase of the next day.

| Save | 4d85c0 | 535760 | Result |
|---|---|---|---|
| 1 | 512 equal, 0 differences | 36 equal | the player elected, 15 votes to 6, 7, 6 |
| 3 | 512 equal, 0 differences | 24 equal | the player elected, 18 votes to 2, 8, 6 |

The oracle stopped at the end of the election when a human merchant wins:
0x4DBBD0 opens windows there (*0x6CC7E8, 0x469380 and 0x469AF0, codes 0x25
for the player and 6 for another human; and 0x5499F0 when it is the player),
which reach `GetFullPathNameA`. `retouch_player.py` jumps over them
(0x4DC041 → 0x4DC0F2) in `to.EVENT_JUMPS` at run time, as p3vita, which
doesn't reproduce them (events_mayor.cpp). `town_oracle.py` isn't changed.

`level4` (scenario and debug state) now also sets the mayor's rank at home,
[+0x39C + home] = 6, as 0x4F75F0 leaves a mayor. Without it, in save 1 the
player's rank at home was below 4 on the election day, and the player had no
vote.

## 2. The vote: operation 0x3F

**The UI.** The Hanse window's update 0x5E8280: when its button [+0x17C]
or [+0x1484] is pressed, by its page [+0x1908]:
- 1 (the Alderman election): `{0x3F, [0x6DFC14], row [+0x1907]}`;
- 2 (a vote of the diet): the same with a row < 3 (0 yes, 1 no, 2 abstain);
- 4: `{0x49, player, town [+0x18E8], row}` (a vote of a town's council; not
  studied).

The page shows «Choose a candidate and cast your vote.» (table 0x69EE38,
entry 76) while 0x4D9D40 says the player can vote.

**0x4D9D40.** The player can vote when:
- it has rank ≥ 4 in its home town;
- an event 0x20 or 0x21 of today is reached walking the queue (it stops at
  the first event of another day);
- its bit, its number among the human players (the merchants with [+0x8]
  = 0 before it), is still set: [+0x14] of the election, [+0x16] of a vote
  of the diet.

**0x4D9C70.** The first event 0x20 or 0x21 of today; the walk stops at the
first event later than today.

**0x53AA10** (`fn_53aa10`):
- The merchant must exist and have rank ≥ 4 at home.
- Its votes: 1, 2 or 3 by rank (< 6, 6, 7).
- Its supporters: the towns' [0x6DC + j] that are it.
- In the election, its bit [+0x14] is cleared, then:
  - [+0x16] goes to at least 1;
  - the share = [+0x15] · supporters / [+0x16];
  - the candidate k (≤ 3) gets the votes in [+0xC + k] and the share in
    [+0x10 + k], with the share capped so the two don't pass 0xFF.
- In a vote of the diet, its bit [+0x16] is cleared:
  - yes or no: the votes go to [+0x12 + k] and the supporters to
    [+0x10 + k];
  - abstain: the votes go to [+0x14], and [+0x15] grows by the supporters
    up to 0xFA.

| Scenario | What | Saves 1 and 3 |
|---|---|---|
| vote_self | for its own candidacy: +2 votes (rank 6), bit cleared | 0 differences (535760: 37 and 25 equal) |
| vote_other | for another candidate: +2 to it | 0 differences |
| vote_twice | twice in a tick: the second refused | 0 differences (38, 26) |
| vote_early | the day before: refused (no election today) | 0 differences |
| vote_late | the day after: refused | 0 differences |
| vote_rank3 | its rank at home lowered to 3: refused | 0 differences |
| vote_bad_choice | row 4: bit cleared, nothing counted | 0 differences |
| diet_yes, diet_no, diet_abstain | a vote of the diet (event 0x21, proposal 5) made for the next day | 0 differences (37, 25) |

`town_oracle.OPERATIONS` (part B's) doesn't have 0x3F: `retouch_player.py`
adds it to `o.operations` at run time. The vote scenarios queue it at a
world tick relative to the election's first phase (+0x20: that day after
it).

The share of the supporters stays 0 in the saves: the player has no
supporters ([0x6DC]) there.

## 3. The election page and the candidacy

`HanseView` page 1 (Select in the panel; Tab on the keyboard):
- **The candidates**:
  - before the election day, `next_election_candidates`, which calls
    0x529700 (`events_mayor_detail::fn_529700`, declared in `votes.hpp`
    and used as it is) as the game's window does (0x5E3A75, 0x5E4353);
  - on the day, the election's event ([+0x8 + k], votes [+0xC + k]).
- **The text**: the game's «nominated… on %d. %s %d in %s» (the date and the
  Alderman's town: a guess for «in %s»), «candidates standing» or «choose a
  candidate».
- **The vote**: X votes for the selected row.

## 4. The end

When the Alderman changes to the player after a tick, besides the notice:
- **The window**: a centered window with the game's «Alderman's office» and
  «%s %s %s %s from %s was elected Alderman of the Hanseatic League by %d
  votes.»; X closes it.
- **The name**: the game's window (0x5299F0) gives title and rank as the
  first two %s; here they are empty and the spaces collapse.
- **The votes**: the player's row of the election event, which keeps its
  votes until the next year's first phase.

Pending: the `eldermann` video (with the Bink videos) and whatever the game
does after (0x5499F0, called with the end window when the player wins: not
studied).

## 5. The game's texts

**How the game keeps them.** In its `.data`, as pointers to the strings,
grouped in tables by window or topic: 0x69EE38 has the election and the
diet, 0x6A94E0 the town hall and the Hanse tasks, 0x6C0DA0 the months. The
code reads the pointer at a fixed address (`mov edx,[0x69EF68]`) or indexes
a table (`[eax*4+0x69EE38]`). Each language's build has its own addresses:
the Spanish exe is a different build (PE time stamp 0x3F8814FA, the English
GOG 1.1 0x3F544D3C).

**Control codes** in the texts: `\f0_`..`\f5_` (font), `\c` `\l` `\r`
(alignment), `\C` (the coin symbol), `\tNNNNN_`, and others of one letter
(`\d`, `\B`, `\L`, `\S`...); and real line breaks. Some formats are
positional: `%$5$d` (the town that grows).

**The mapping.** For each English text used, the address of its pointer in
the Spanish build was found by matching the sequence of «signatures»
(control codes and printf specifiers, the same in every language) of the
pointers around it in the other exe's `.data`. Each match was checked by
reading the Spanish text. Plain texts with no codes (the months, «Tasks»)
were found by content («enero») or by the offset of their table (+0x5D8 in
0x6A94E0). The two columns are in `kBuilds` (`game_texts.cpp`). A text for
another build (a third language, the Steam version) needs its column, found
the same way.

**At run time** (`GameTexts::load`):
- reads the exe and recognizes the build by its PE time stamp;
- follows each pointer;
- converts Windows-1252 to UTF-8 and removes the codes;
- `format` fills `%s`/`%d`, positional ones too.

The exe is read once at start (3 MB here; the strings kept are a few KB).

**Used for**:
- the tasks on offer: «Puedes elegir una de las siguientes misiones:», each
  one's description with its region;
- the Hanse's answer when a task is taken, with names and deadline;
- «no tasks»;
- the election: title, candidates, vote prompt, «nominated…»;
- the end window and the months.

**The region of a task** (the %s of the descriptions) isn't the `Region=`
of StadtDaten.ini. The window picks it by ranges of the town's number
(0x5E6C19):

| Town number | Region |
|---|---|
| < 6 | West |
| < 0x0D | North Sea |
| < 0x10 | Baltic |
| < 0x1C | North |
| < 0x21 | Baltic |
| else | East |

The answer when a task is taken is built from the task once it has
started: its deadline changes then, as the road's does.

## Checks and times

- `retouch_player.py`, saves 1 and 3, 2 days, 535760 and 4d85c0: the 9 vote
  scenarios and `election_level4`, all 0 differences. 7 scenarios × 2 saves
  took 40 s; the 3 of the diet, 14 s.
- The part 1 scenarios (take_*, give_*) weren't repeated: their code didn't
  change.
- `build.sh`: ctest 10 of 10. `vita_check.sh`: 1,239,047 bytes, no
  warnings. Reading the exe with iostream had added 300 KB to the VPK: it
  uses stdio. Not tried on a Vita.
- No `world.sh`: no code the simulation or the loader runs has changed.

## For the roadmap

- The path to Alderman in the viewer: the vote (0x3F, pending its case in
  `run_operation`), the candidates, the end window. Left: the `eldermann`
  video, 0x5499F0 after the win, the mayor's candidacy («Accept
  candidature?»).
- The interface texts come from the game's executable (English GOG 1.1 and
  Spanish builds): used in the Hanse panel; the rest of the viewer, later.

## For the inventory

- Simulation (executor): 0x53AA10 (operation 0x3F), with 0x4D9C70 and
  0x4D9D40, rewritten in `town_tick/votes.cpp` and checked; to call from
  `run_operation` (decision 1).
- UI studied: 0x5E8280 (the votes: 0x3F; 0x49 for a council, not studied),
  the election's end windows in 0x4DBBD0 (0x469380, 0x469AF0 code 0x25 or 6,
  0x5499F0), 0x5E6C19 (the region of a task), 0x5E387E («Accept
  candidature?»).
- Operation 0x4A (seen in part 1 in the task window) is the player's
  proposal to the diet from a complaint (event 0x30 → event 0x21, 0x53B1F0),
  not the developers' tools as part 1 guessed; 0xC8 and 0xCA (0x469AF0)
  look like game control (pause, speed).

## Game bugs

None found.

## Changes to shared files

None committed. `run_operation`: the change is in decision 1, tried only
locally. `fn_529700` (events_mayor.cpp) is declared in `votes.hpp` and
called, not changed. `retouch_player.py` changes `to.EVENT_JUMPS` and
`o.operations` at run time, in its own process.
