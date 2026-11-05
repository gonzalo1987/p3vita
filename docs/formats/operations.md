# The operations: the 193 codes of the executor

Inventoried on 2026-11-05 (the inventory's part E, [../inventario.md](../inventario.md)).
An operation is 0x14 bytes: a u32 code and 16 bytes of data. The interface and the
simulation fill one (the interface first with 0x535740: a u32 0xFFFFFFFF, then
zeros), and either queue it with **0x54AA70** on the object 0x6DF2F0, which the
game runs between two calls to advance_time ([world_tick.md](world_tick.md) §7), or
run it at once with the executor **0x535760** (ecx = the operation). The executor
jumps by the code, 0x00 to 0xC0, through the table **0x537030**: 180 case blocks
in 0x535760, most of them only a call to the handler. The codes from 0xC1 to 0xD4
never reach it: the real-time loop's own executor (0x546870) takes them (below).

In a network game the operations are what travels: every machine runs the same
simulation and only the operations are sent (P3Modding's book,
«Operation Synchronization»). So p3vita rewrites them byte for byte like the rest
of the engine, and the viewer queues the same bytes the game's windows write.

## How the table is made

With the scripts of `re/emu/sesion9/` (outside the repository; README there):
- `ops_table.py` reads the whole disassembly (`re/save_re/p3.asm`): each code's case
  block, what it calls and what is left below without anything in p3vita (as
  `callgraph.py` counts it: functions and bytes, not crossing what p3vita has; an
  orientation, since the static graph also walks into the interface), whether
  `run_operation` handles it, and every call to 0x54AA70 (388) and to 0x535760 with
  the code read back from the operation's buffer: the register pushed (or ecx),
  the `lea` that set it, and the `mov DWORD PTR [buffer],code` before it, following
  esp; a code in a register (`mov ebx,0xB` earlier) is followed too, and so is a
  second path that jumps into a shared call (`push eax; jmp`). A queuing function
  is tagged as the simulation's when advance_time reaches it without the interface
  (callgraph.py's pruning plus the interface's objects in 0x6CB000..0x6CFFFF and a
  few windows), as the executor's, the battles' or the mission interpreter's.
- `ops_sum.py`, `ops_writes.py` and `ops_peek.py` print each handler short (the
  operation's fields it reads, the globals, the calls with the p3vita files that
  name them, the letters; what it writes; its first instructions), to describe it.
- The descriptions are written by hand in `ops_notes.tsv` and `ops_doc.py` makes the
  table below. The «Source» column says how sure each one is: **read** (in the
  code, or in p3vita, or in a doc that read it: `mail.md`, `alderman-viewer.md`,
  `battles.md`), **book** (P3Modding's list of operations), **guess** (from the
  window that queues it, the functions it calls and the fields it touches: to be
  read before rewriting it).

Found by hand, where the code is computed:
- **The market window (0x4A8CF0)** picks the code by its mode ([+0xF0], set by
  0x4A9690): 1 a ship and the town (buying 0x02, selling 0x01), 4 a convoy and the
  town (0x1A, 0x19), 2 the office and the town (0x59, 0x58), 0 a ship and the
  office (0x08) and 3 a convoy and the office (0x1B), both with a direction byte in
  [+0xE]; 5 moves the ship's weapons with the office: rows 0..3 the weapons (0x08, in pieces of 10), rows 4..10
the hand weapons and the six pieces (0x09, the type row − 5). The window keeps two sides, each a ware's
units, what it holds and its room: the ship goes on the first side in the modes 0 and 5 (0x4AA9D0), on the
second otherwise; the office's side (0x4AA7C0) has its stock / 200 or / 2000 and, in mode 2, its room
[+0x2EC] · 2000 (0x4A97B6); in mode 2 the window's [+0xF4] is the merchant. The amount: the step (1, 5, 50 or all) clamped
  by what the window keeps of the town and the ship (`player_actions.hpp`).
- **The tavern** keeps an operation that the town prepares (0x526B40 on the town,
  into the window's [+0x2154]) and queues it as it is (0x5D5970) when the player
  accepts: any code can come that way.
- The Hanse tasks window queues 0x4A and 0x4B from the same call (0x5E8E71).
- **The convoy window** (the ship's panel, 0x6CE6D0; 2026-11-05) offers, for the ships
  selected in the town of the first (0x40C170): a private convoy if one of them can
  guide (0x519C40), a public one with the guild's licence ([+0x468]), and the convoys
  of the town they may join. A new one (0x494B80: 0x15 with 1 and 0 when private, from
  0x40CA79; with 0 and the destination when public, from the town picker 0x40C919) and,
  with several ships selected, 0x18 for the others; joining (0x494AF0) 0x18 for each
  ship; the questions «Disband convoy» and «Leave convoy» (0x494DC0) send 0x16 with the
  convoy or 0x17 with the ship (their answer, 0x4950D0). In the market window's modes 4
  and 3 the convoy's side is 0x4AA4A0 (the player's ships in it). 0x1C is queued by the
  trade routes (0x5492D0), not by a window; 0x20 has no site with its code.
- **The shipyard window** (0x5F8F50) has the pages repair (0x03, 0x1D), order a ship
  (0x5F9570: 0x2C {type, the player, town, 0}), cancel an order (0x5F9B00: 0x2F; only
  without work started, «Work is already being done on this ship» otherwise), upgrade
  (0x5FA320: 0x0C with the level to reach, the list 0x504AC0 mode 2) and rename
  (0x5FABE0: the name in pieces of 12, 0x2D and then 0x2E; the list 0x504AC0 mode 0).
- **The moneylender's window** (opened at 0x5B2B00 in the town the player looks at;
  2026-11-05) has the pages take out a loan (0x51CE80 on the town: the offers of its slots
  0..2 as 0x29's 16 bytes; 0x5B5A00 sends one while he has at most 2 debts), pay one off
  (the loans he owes, 0x4D9100; 0x5B5B90: 0x2A {player, event} where he has an office or a
  ship in port, or in its town, with the money), grant one (only with an office in the
  town: 0x51D160, the townsmen of its slots 3..5, then the interest's kind 0..4 and
  0x51D270, which rolls rand() for whether the townsman pays it back; 0x5B6000: 0x29),
  the loans granted (0x4D9210), buy company shares (0x4D9320: the pools offering some;
  0x5B6400: 0x2B with the entry's 12 bytes, with the money for its price) and sell them
  (0x4D9620: his company's new shares, then his holdings; 0x5B66C0: 0x2B). The lists of
  the debts, the loans granted and the shares are 0x18-byte or 12-byte entries on the
  heap; the window calls 0x51CE80, 0x51D160 and 0x4D9320 every frame on their pages.
- **The build window** (the side menu SIDEMENU_BUILD, *0x6CC95C; 2026-11-05) has five
  categories ([+0x104C]: housing, public, fortifications, two groups of businesses) whose
  buttons 0x47AB20 fills with building types; choosing one (0x47A833) asks 0x51F3A0 for its
  costs. The streets, the well and the wall have buttons of their own (0x4797C0): 0x25
  {town, player}, 0x22 {player, town, x, y from 0x5250D0, 0x28, 0} and 0x24 {player, town,
  the next piece is a gate}; the others are placed in the town view, which asks 0x47B8C0
  whether the building fits the cell under the cursor (the tint [+0xC6CC]) and queues 0x21
  (0x590840) with the picture the window asked for (0x5928A0: the houses' and the
  warehouse's orientation by what fits, the gate under the pitch shoot). 0x47B830 marks
  where it fits. The player's own building's window (0x5AF5C0) demolishes it (0x26, after
  «Do you really want to demolish this building?») or auctions it (0x28, after the
  question with the reserve price, 0x5AFC50), both through 0x5AFD80.
- **The side panel's ship menu** (SIDEMENU_SHIP, `CSideMenu_Ship`, vtable 0x66F358, its update
  0x48B3E0; 2026-11-06) queues the convoy window's operations from its convoy pages and, on its
  crew page, 0x05 and 0x07 (dismiss sailors, the captain) and «Pirate» / «Civil» (0x494244: 0x0D
  {ship, [+0x15C] == 0}, the button enabled by 0x518750 for a ship sailing away from the towns);
  on its deck page in a sea battle, 0x98 (0x48D420, the battle window).
- **The letters' settings window** (0x408F50, «Configuration of letters», from the side panel's
  messages; 2026-11-06) queues on closing 0x70 {u16 player, the days each class is kept, the event
  sound, zeros, [+0xE] the notices} (0x4096DB) and then 0x71 {player, 1} (0x409735); the letter
  window queues 0x71 {player, 0} once a tick while it is open (0x47CD83).

Not found: 0x3C, 0x4C, 0x54, 0x55, 0x5C, 0x5D, 0x8F, 0x99, 0xA6, 0xAE, 0xB2, 0xB6 and
0xBD have no queuing site with a constant code. Some are surely queued with a
computed code or through the tavern's stored operation (0x5D, opening an office, is
an action of the interface; 0x54 and 0x55 go with letters), others may be dead.

## The groups

| Group | Codes | Done in p3vita |
|-|-|-|
| simulation | 37: the ones the simulation queues (inventory, part B) | all 37 |
| player | 105: queued by the interface | 43: 0x00..0x0A, 0x0C, 0x0D, 0x15..0x1D, 0x20, 0x21, 0x25, 0x26, 0x29..0x2B, 0x2D..0x2F, 0x3B, 0x3F, 0x4B, 0x4E, 0x58, 0x59, 0x5D, 0x6E..0x71 |
| battles | 14: 0x91..0x9B, 0x9F, 0xA0 and 0xBD (the battle window) | 0x91, 0x94, 0x96, 0x9F |
| network | 15: the network game's lobby (0xA2..0xAB, guessed) and the sync checks that do nothing (0xB9..0xBC, 0xBE) | the five that do nothing |
| new game | 11: queued by the new game (0x544E50): 0xAC..0xB4, 0xB6, 0xB7 | — |
| missions | 1: 0xC0, the mission interpreter | — |
| unused | 10: nothing (0x14, 0x73, 0x9C..0x9E, 0xA1) or no queuer found and nothing to do with the player (0x3C, 0x4C, 0x5C, 0x8F) | — |

Some codes of the simulation are also queued by the interface (0x22, 0x24 and 0x23
by the construction window; 0x2C by the shipyard; 0x37, 0x61 by the guild and the
feast window): the handler is the same and is done.

## The table

| Code | Group | What it does | Queued by | Handler | Left below | p3vita | Source |
|-|-|-|-|-|-|-|-|
| 0x00 | player | set sail: {0, ship, town, x, y} (`Sailing::set_sail`) | sea map 0x455B50, 0x45A6B0; shipyard 0x5FA000; also 0x509250 (sim, runs it at once) | 0x53577D → 0x537670 |  | yes | read |
| 0x01 | player | sell from a ship to the town: {1, s32 amount, u16 ware, u16 ship, u16 player, u16 town}; it doesn't check that the ship is in the town; statistics 0x4F8690 (`trade.cpp` `fn_537340`; savegame.md §12.3) | market 0x4A8CF0 (mode 1, a ship and the town); 0x40EEB0 | 0x53578C → 0x537340 |  | yes | read |
| 0x02 | player | buy for a ship from the town: the same fields; the ship in the town, the limit by the store, the hold and the money (0x52F3E0); statistics 0x4F8840 (`trade.cpp` `fn_537450`) | market 0x4A8CF0 (mode 1) | 0x53579B → 0x537450 |  | yes | read |
| 0x03 | player | repair: {0x03, ship}: a docked ship into its town's shipyard (0x52ACD0): the cost 0x52A9E0 (the damage, at least hull / 100, · capacity · a rate by the town's [+0x818], / (hull · 2000)), for its whole convoy if in one; free for no merchant, the AI pays always, a human gets the letter 0x02 without damage or money, otherwise pays; the state 4 (`town_tick/ship_operations.cpp`) | shipyard window 0x5F8F50 | 0x5357AA → 0x52ACD0 |  | yes | read |
| 0x04 | player | hire sailors: {0x04, ship, amount}: a ship in a town, up to its room (0x5184F0) and what the town lets its owner hire (0x4F6CA0: his quota [+0xF0 + town], at most the unemployed less one), the quota down (0x4F6BD0); the first sailors set the morale's high byte (4 with a captain, 3 without); out of the town's unemployed [+0x2E4], [+0x2D4] (`town_tick/ship_operations.cpp`) | 0x5D4C10 (the amount typed, atoi) | 0x53580C → 0x537C20 |  | yes | read |
| 0x05 | player | dismiss sailors: {0x05, ship, amount}: a ship in port with sailors; to the town's unemployed; all if asked for more (morale 0), otherwise the morale [+0x3E] down by amount · 2560 / (sailors + 1), not below 0 | ship window 0x493ED0 | 0x53581B → 0x537DD0 |  | yes | read |
| 0x06 | player | hire a captain: {0x06, captain, ship, town}: out of the town's list (0x526AA0), the ship's captain before him into his link, the letter 0x04 «New captain»; it doesn't look whether the ship is in port or has one | 0x5D4870 | 0x53582A → 0x537EE0 |  | yes | read |
| 0x07 | player | dismiss the captain: {0x07, ship}: a ship in port; the captain to the town's list (0x526B00), the owner's [+0x1F] to 0, the morale −2560 / (sailors + 1), [+0x3C] \|= 0x202; a convoy it guides breaks up (0x50A580) | ship window 0x493ED0 | 0x535839 → 0x538000 |  | yes | read |
| 0x08 | player | wares between a ship and its owner's office in its town: {0x08, s32 amount (its sign dropped), u16 ship, u16 ware, u16 (the player), u8 direction}: a ship out of the shipyard (state < 4); direction 1 from the office into the ship, at most the hold's room (0x5182B0) and the stock, at the office's average price (0x5180C0: the cargo rounded to its unit); 0 from the ship, at most its cargo, at the ship's average (0x4FF6F0); the notice 4 (`trade.cpp` `fn_538100`) | market 0x4A8CF0 (mode 0, a ship and the office: the ship on the window's first side, 0x4AA9D0; the office 0x4AA7C0) | 0x535848 → 0x538100 |  | yes | read |
| 0x09 | player | artillery between a ship and its owner's office in its town: {0x09, u32 piece type, u32 town (low byte; the bit 31 for from the ship), u32 ship, u32 count}: the ship's 24 slots [+0x13C] (0..5 a type, 0xFF free, 6 the second half of a big piece: a weight of 2000 or more, 0x672CB4; 7 free for small pieces, filled first), the office's weight of each type [+0x124 + 4 · type]; a type of 6 or more moves the hand weapons ([+0x154] of the ship, [+0x2BC] of the office) by the room / 10; from a convoy's guide without its flag 2, nothing; counts compared unsigned (`trade.cpp` `fn_538210`) | market 0x4A8CF0 (mode 5: rows 4..10, the type row − 5; rows 0..3 are the weapons, 0x08) | 0x535857 → 0x538210 |  | yes | read |
| 0x0A | player | a ship is deleted (0x507FB0) | 0x4768B0 | 0x535866 → 0x507FB0 |  | yes | read |
| 0x0B | player | a ship's list of stops: allocates the merchant's [+0xEC] (0x40 entries) and sails (0x519BA0, 0x517260, 0x537670); most of what is left below is interface | sea map 0x44FB40, 0x451EB0 (code in ebx), 0x455B50, 0x45A6B0 | 0x5358AC → 0x538DE0 | 1, 1.1 KB |  | guess |
| 0x0C | player | upgrade a ship at the shipyard: {0x0C, ship, level}: a docked ship (state ≤ 1) in a town, if 0x52B670 allows it (per level the timber, pitch and tools of 0x66E030; the cost 405 a load, what is bought and the repair 0x52A950): to the head of the town's repair list [+0x81E] in the state 7, the merchant pays, what is bought leaves the town's store and his own wares his office and his ships there; 0x51A750 adds the levels (up to 2), 0x4F9120 (`town_tick/ship_operations.cpp` `fn_508660`) | shipyard window 0x5FA320 (the level to reach, [+0xC88]) | 0x5358BB → 0x508660 |  | yes | read |
| 0x0D | player | a ship's «Pirate» / «Civil»: {0x0D, ship, flag}: a ship out of a convoy and sailing (0xF): [+0x3D] \|= 0x18, [+0x15C] = 1, [+0x136] = 0, or without the flag [+0x3D] \|= 0x10, [+0x15C] = 0; in a convoy, its [+0x18] \|= 0x1000 and its ships' [+0x3C] \|= 0x1000 and [+0x15C] (with the flag only for a convoy of the ship's owner, sailing, with its flag 2) (0x5386C0, `town_tick/ship_operations.cpp`) | the side panel's ship menu, crew page 0x494244 ({ship, [+0x15C] == 0}; enabled by 0x518750) | 0x5358D5 → 0x5386C0 |  | yes | read |
| 0x0E | player | a ship's [+0x3A] | 0x44FB40 | 0x5358E4 → 0x538800 | 1, 304 B |  | guess |
| 0x0F | player | two ships swap their captains ([+0x42]) | 0x40EEB0 | 0x5358F3 → 0x517770 |  |  | read |
| 0x10 | player | a ship takes a ware or weapons ([+0xE] ware, [+0x10] amount; 0x5184F0) | 0x40EEB0 | 0x535953 → 0x538930 | 1, 144 B |  | guess |
| 0x11 | player | a ship gives a ware or weapons back ([+0xE], [+0x10]) | 0x40EEB0 | 0x535962 → 0x5389C0 | 1, 192 B |  | guess |
| 0x12 | simulation | a ship's captain improves (0x538A80) | 0x4DCEA0, 0x514B80 | 0x535971 → 0x538A80 |  | yes | read |
| 0x13 | simulation | the captain of a human player's ship retires (0x538C40) | 0x4DCEA0 | 0x535980 → 0x538C40 |  | yes | read |
| 0x14 | unused | nothing (the executor's end, 0x537025) |  | — |  |  | read |
| 0x15 | player | create a convoy: {0x15, guide, 1 private or 0 public, 5 days, destination} (0x509B90, `ship_ai.cpp`) | the ship's panel: convoy window 0x494B80 (private, 0x40CA79 with (0, 0); public, the town picker 0x40C919 with (1, town)), then 0x18 for the other ships selected | 0x53598F → 0x509B90 |  | yes | read |
| 0x16 | player | disband a convoy: {0x16, convoy} (0x50A580; then 0x6141D0 if the port scene shows its town: UI) | the question «Disband convoy» (0x494DC0, its answer 0x4950D0) | 0x5359B1 → 0x50A580, 0x6141D0 |  | yes | read |
| 0x17 | player | a ship leaves its convoy: {0x17, ship} (0x539270: 0x509F80; its guide disbands it, 0x50A580; `convoys_members.cpp`) | the question «Leave convoy» (0x494DC0, its answer 0x4950D0) | 0x535A16 → 0x539270 |  | yes | read |
| 0x18 | player | a ship joins a convoy: {0x18, a ship of it (the guide), ship} (0x509D10, `convoys_members.cpp`): one owner's convoy only his ships; one open to others a ship with its sailors (0x673660) and a fifth of its hull; in its town, docked; a big ship not with the bit 4 nor to a town of no big ships; the state 9 asks 0x5039F0; in the states 9 and 0xA (an event's convoy) 0x4F8D20 and the event's merchant byte | convoy window 0x494AF0 (join a convoy), 0x494B80 (the others of a new one) | 0x535A25 → 0x509D10 |  | yes | read |
| 0x19 | player | sell from a convoy to the town: {0x19, s32 amount, u16 ware, u16 convoy, u16 merchant, u16 town} (0x539300 with 1: the merchant's ships of the convoy one after another, paid at once with the store before; `trade.cpp`) | market 0x4A8CF0 (mode 4, a convoy and the town: what it keeps of the convoy, 0x4AA4A0); also 0x43D030 | 0x535A3F → 0x539300 |  | yes | read |
| 0x1A | player | buy for a convoy from the town (0x539300 with 0: at most the merchant's ships' room and the store; each ship what fits, cut down to the money in 32 bits; `trade.cpp`) | market 0x4A8CF0 (mode 4) | 0x535A50 → 0x539300 |  | yes | read |
| 0x1B | player | wares between a convoy and its owner's office: {0x1B, s32 amount (its sign dropped), u16 convoy, u16 ware, u16 merchant, u8 direction} (0x5396B0, `trade.cpp`) | market 0x4A8CF0 (mode 3, a convoy and the office); the mission interpreter | 0x535A61 → 0x5396B0 |  | yes | read |
| 0x1C | player | a convoy sets sail: {0x1C, u16 convoy, u16 wait, town, x, y}: its guide sails (0x537670 on the operation) and [+0x16] goes up to the wait (0x535A70) | the trade routes 0x5492D0 (at 0x5493BF) | 0x535A70 → 0x537670 |  | yes | read |
| 0x1D | player | repair a convoy: {0x1D, convoy}: its state [+0x12] to 4, the owner of its first ship pays the cost (0x52A950) without checks, the reputation for the damage (0x4F9120) | shipyard window 0x5F8F50 («whole convoy») | 0x535AC0 → 0x539990 |  | yes | read |
| 0x1E | simulation | a convoy leaves on an expedition (0x539B50) | 0x503B50; 0x5B9210 | 0x535ACF → 0x539B50 |  | yes | read |
| 0x1F | simulation | a point of a convoy's expedition route (0x539C60) | 0x503B50; 0x5B9210 | 0x535ADE → 0x539C60 |  | yes | read |
| 0x20 | player | a docked convoy (state 0) waits for ships to another destination: {0x20, convoy, destination, u16 days}: its [+0x1F], [+0x16] = days << 8, the state 1, and its ships wait as long (0x539CF0, `convoys_members.cpp`) | not found with its code (0x40EEB0, 0x4237D0 by the table) | 0x535AED → 0x539CF0 |  | yes | read |
| 0x21 | player | build, from the town view: {0x21, u8 player, town, x, y, the building's picture (0x672FC0), the parameter (the houses' and the warehouse's orientation, a gate's), zeros}; the handler takes the picture back to its type from the end of 0x672FC0, 0x2C opens an office (0x53CE30, `construction.cpp`; the bytes, `player_actions.cpp` `place_operation`) | town view 0x590840 (when the picture's tint says it fits: 0x47B8C0 of the build window) | 0x535AFC → 0x53CE30 |  | yes | read |
| 0x22 | simulation | build (0x53CE30) | the towns' works, the AI; the build window 0x4797C0 (the well: {0x22, u8 player, town, x, y from 0x5250D0, 0x28, 0}), 0x419EB0 | 0x535AFC → 0x53CE30 |  | yes | read |
| 0x23 | simulation | start a side of the town wall (0x522B20) | 0x51D870; 0x419EB0 | 0x535B12 → 0x522B20 |  | yes | read |
| 0x24 | simulation | the next wall piece (0x522C70; the book: «Build Town Wall») | 0x51D870; the build window 0x4797C0 («build piece of wall»: {0x24, player, town, the next piece is a gate}), 0x419EB0 | 0x535B44 → 0x522C70 |  | yes | read |
| 0x25 | player | streets: {0x25, town, merchant}: what a street (type 2) costs (0x51F3A0: streets still to make, [0x790] < [0x792]), with an office in the town; his reputation (0x4F8A50), he pays, his office and ships give the materials (0x500A70), the town's store what is missing; a street record into the works and up to 16 planned streets built (0x4FB580, ring by ring from the town's middle) into [0x790] (0x5244F0, `construction.cpp`) | build window 0x4797C0 («build street») | 0x535B7B → 0x5244F0 |  | yes | read |
| 0x26 | player | demolish: {0x26, town, merchant, x, y}: his building at (x, y) in his office's lists or the town's works, with an office there, not on sale (0x4DAFE0); out of the grid (0x4FACD0), the office recounted, a business costs 1 of reputation, the record freed (0x51F050) (0x53CF50, `construction.cpp`) | the player's building's window 0x5AF5C0 («demolish»: the question 0x5AF731, then 0x5AFD80 with its flag 0) | 0x535BAD → 0x53CF50 |  | yes | read |
| 0x27 | simulation | materials for a town's works (0x535BBC) | 0x51D870 | 0x535BBC |  | yes | read |
| 0x28 | simulation | a merchant puts a building on sale (0x53D190) | 0x4F5A20; the mission interpreter; the player's building's window (auction: 0x5AFC50's question with the reserve price 0x4DAF10, then 0x5AFD80: {0x28, town, merchant, x, y}) | 0x535C2D → 0x53D190 |  | yes | read |
| 0x29 | player | a loan: {0x29, the terms: u32 the repayment, u8 the weekly interest (tenths of %), the borrower's rank and names, u16 the amount, u8 the weeks, the low byte of the loan's tick, u8 lender, borrower (0xFF: the moneylender or a townsman), town, slot << 4 \| kind}: the event 0x01, the town's slot emptied, the lender pays it (0x539DC0, `operations.cpp`) | moneylender: taking one 0x5B5A00 (the terms of 0x51CE80), granting one 0x5B6000 (0x51D160, the kind chosen, 0x51D270); the merchants' tick 0x4F4E50 | 0x535C3C → 0x539DC0 |  | yes | read |
| 0x2A | player | pay off a loan before it falls due: {0x2A, merchant, event}: the record (0x4D8DB0) a loan whose debtor is the merchant: he pays its [+0x8], a creditor merchant gets it, both become 0xFF (0x539F30, `operations.cpp`) | moneylender 0x5B5B90 (the debts of 0x4D9100) | 0x535C4B → 0x539F30 |  | yes | read |
| 0x2B | player | company shares bought or sold: {0x2B, u8 company, shares, seller, buyer (0xFF: the pool), u32 price, u32 what the player owns}: the events 0x18, at most 45 % sold by the owner; their dividends each quarter (event 0x18, 0x4E8804) (0x4D98F0, `operations.cpp`) | moneylender 0x5B6400 (buy: the list 0x4D9320), 0x5B66C0 (sell: 0x4D9620) | 0x535C5A → 0x4D98F0 |  | yes | read |
| 0x2C | simulation | a ship ordered at the shipyard (0x52A360; the book: «Build Ship») | event 0x2B; shipyard window 0x5F9570 | 0x535C70 → 0x52A360 |  | yes | read |
| 0x2D | player | a ship's name, its first 12 characters: {0x2D, 12 characters, u32 ship} (0x53CCE0: the name in the names object, 0x512D40, its index [+0x15E]; the name [+0x160]; `town_tick/ship_operations.cpp`) | shipyard window 0x5FABE0 (rename), 0x46C420 (a new ship's name) | 0x535CAA → 0x53CCE0 |  | yes | read |
| 0x2E | player | the next 12 characters of a ship's name (0x53CCE0: at the end of the names object's last name, 0x512FC0, or a new name of its index's and them, 0x512B20 and 0x512D40) | shipyard window 0x5FABE0, 0x46C420 | 0x535CAA → 0x53CCE0 |  | yes | read |
| 0x2F | player | cancel a ship ordered: {0x2F, merchant, town, ship} (0x535CB9 → 0x52B110): his ship in the town's list [+0x81C]; the letter 0x14 «Annulment»; with its work not started, nine tenths of [+0x48] back and the ship deleted (`town_tick/ship_operations.cpp`) | shipyard window 0x5F9B00 | 0x535CB9 → 0x52B110 |  | yes | read |
| 0x30 | player | feed the poor (0x4FE557) | church 0x5CAAB0 | 0x535CF0 → 0x4FE557 | 2, 896 B |  | book |
| 0x31 | player | donate to the church's extension (0x4FE420) | church 0x5CB4E0 | 0x535D49 → 0x4FE420 | 2, 407 B |  | book |
| 0x32 | player | donate to the church (0x4FE2D0) | church 0x5CB280 | 0x535D85 → 0x4FE2D0 | 2, 240 B |  | book |
| 0x33 | player | an auction's bid? (an event record [+0x4], 0x6DD748) | guild 0x5BFB20 | 0x535DC1 → 0x53A010 | 1, 160 B |  | guess |
| 0x34 | player | an auction's bid? (an event record) | guild 0x5BFB20 | 0x535DD0 → 0x53A0B0 | 1, 192 B |  | guess |
| 0x35 | player | an auction: a new event (0x4D9000, 0x4D8CF0) | guild 0x5BFB20 (run at once) | 0x535DDF → 0x53A170 | 2, 464 B |  | guess |
| 0x36 | player | an auction of a ship: an event (0x4DA510) | guild 0x5BFB20 | 0x535DEE → 0x4DA510 | 1, 848 B |  | guess |
| 0x37 | simulation | a merchant pays for a town (0x4F8560; the book: «Join Guild») | 0x4F4A20; guild 0x5BFB20 | 0x535E04 → 0x5303C0, 0x4F8560 |  | yes | read |
| 0x38 | simulation | a ship auction: event 0x0A (0x535E3B) | 0x4DD400 | 0x535E3B → 0x4D8CF0 |  | yes | read |
| 0x39 | player | bathe (0x535E7F on the merchant; the book) | bath house 0x5B1410, 0x5B15F0, 0x5B1D80 | 0x535E7F → 0x5303C0 |  |  | book |
| 0x3A | player | an event record's fields (0x53A2C0: 0x4D8CF0, 0x4D8DB0, 0x4D85C0) | 0x419EB0, 0x5EB3C0 | 0x535EAC → 0x53A2C0 | 1, 288 B |  | guess |
| 0x3B | player | materials for a Hanse road (0x53A3E0, `fn_53a3e0`) | road window 0x5F2A20 | 0x535EBB → 0x53A3E0 |  | yes | read |
| 0x3C | unused | a building on sale (0x4DAB00) | not found (computed code or dead) | 0x535ECA → 0x4DAB00 |  |  | read |
| 0x3D | player | an order (letter 0x1A «Order»; 0x53A620) | 0x5E7730 | 0x535EE5 → 0x53A620 | 1, 496 B |  | read (mail.md) |
| 0x3E | player | move one's home town? (letter 0x0E «Move» to the chronicle; 0x4F9430, 0x4F75F0, 0x635040) | 0x5DD520 | 0x535EF4 → 0x53A810 | 2, 816 B |  | guess |
| 0x3F | player | the vote of a human player (0x53AA10) | 0x5E8280 | 0x535F03 → 0x53AA10 |  | yes | read |
| 0x40 | player | dismiss a militia squad (0x51D410) | town hall 0x5B0510 | 0x535F12 → 0x51D410 |  |  | book |
| 0x41 | player | form a militia squad (0x51D4C0; the book) | town hall 0x5B0510 | 0x535F45 → 0x51D4C0 |  |  | book |
| 0x42 | player | a bribe in the bath house that works (0x4F8E30; the book) | bath house 0x5B2480 | 0x535F77 → 0x53AC50 | 2, 288 B |  | book |
| 0x43 | player | a bribe in the bath house that fails (0x4F8E90; the book) | bath house 0x5B2480 | 0x535F86 → 0x53AD10 | 2, 160 B |  | book |
| 0x44 | player | a ship or convoy's load or speed (0x5170F0) | 0x5E7AB0 | 0x535F95 → 0x53AD90 | 2, 464 B |  | guess |
| 0x45 | player | something of a merchant (0x4F9390) | 0x5E7C80 | 0x535FA4 → 0x5303C0, 0x4F9390 | 1, 48 B |  | guess |
| 0x46 | simulation | a mayor for a new town (0x535FD6) | event 0x2F; 0x419EB0 | 0x535FD6 → 0x5303C0 |  | yes | read |
| 0x47 | player | a town's expense (0x529F90) | 0x5E8380 | 0x536057 → 0x53AF30 | 1, 224 B |  | guess |
| 0x48 | simulation | a proposal for a town (0x53B010; the book: «Make Town Hall Offer») | 0x51E070, 0x51E270 | 0x536066 → 0x53B010 |  | yes | read |
| 0x49 | player | an event's record (0x4DA060, 0x4D8DB0) | 0x5E8280 (through the jump at 0x5E82FE) | 0x536075 → 0x53B0D0 | 2, 512 B |  | guess |
| 0x4A | player | the Hanse tasks window's other buttons (developers' tools; letter 0x78 «Hanse Day») | Hanse tasks window 0x5E87D0 | 0x536084 → 0x53B1F0 | 1, 592 B |  | read (alderman-viewer.md) |
| 0x4B | player | take a Hanse task (0x53B440) | Hanse tasks window 0x5E87D0 (through the jump at 0x5E8A5A) | 0x536093 → 0x53B440 |  | yes | read |
| 0x4C | unused | something of a merchant in a town (0x535400) | not found | 0x5360A2 → 0x53B4D0 | 1, 272 B |  | guess |
| 0x4D | player | move wares between a convoy or ship and an office (0x4FF6F0, 0x5180C0) | 0x5D6FC0 | 0x5360B1 → 0x53B5E0 | 1, 624 B |  | guess |
| 0x4E | player | a player's ship turns pirate (0x53B850) | 0x5D5430 | 0x5360C0 → 0x53B850 |  | yes | read |
| 0x4F | player | an event in a town (0x4D8CF0, an office 0x5308A0) | 0x5D68E0 | 0x5360CF → 0x53BA40 | 1, 592 B |  | guess |
| 0x50 | player | something with a ship or convoy and an office (0x526300) | 0x5ECAB0 | 0x5360DE → 0x53BC90 | 2, 1.1 KB |  | guess |
| 0x51 | player | something with a ship, an office and an event record | 0x5D7760 | 0x5360ED → 0x53C050 | 1, 624 B |  | guess |
| 0x52 | player | a tavern interaction (0x53C2C0; the book) | 0x5A5EA0, 0x5A7A60, 0x5A7BA0; the tavern's stored operation (0x5D5970) | 0x5360FC → 0x53C2C0 | 2, 1.5 KB |  | book |
| 0x53 | player | a traveller (letter 0x17 «Traveller») | 0x5D6AE0 | 0x53610B → 0x53C840 | 1, 496 B |  | read (mail.md) |
| 0x54 | player | remove a letter 0x44 (0x4D7340) | not found (letter answers?) | 0x53611A → 0x53CA30 | 2, 576 B |  | read (mail.md) |
| 0x55 | player | pay for a letter 0x45/0x46 (0x4D74C0) | not found (letter answers?) | 0x536129 → 0x4D74C0 | 1, 272 B |  | read (mail.md) |
| 0x56 | player | something in an office (0x5308A0) | 0x5ECAB0 | 0x536177 → 0x53CAF0 | 1, 496 B |  | guess |
| 0x57 | player | something in a town (inline, 0x536186) | 0x5ECAB0 | 0x536186 |  |  | guess |
| 0x58 | player | sell from the office to the town: {0x58, s32 amount, u16 ware, u16 merchant, u16 (the player), u16 town}: the merchant's office there (0x5308A0), at most its stock, if its town [+0x2C6] is one; the price with the store before the sale (0x52E1D0), statistics 0x4F8690; no ban looked at (`trade.cpp` `fn_53d2b0`) | market 0x4A8CF0 (mode 2, the office and the town; [+0xF4] the merchant) | 0x5361E2 → 0x53D2B0 |  | yes | read |
| 0x59 | player | buy for the office from the town: the same fields; at most the store (rounded to the unit) and the money (0x52E430, 0x52F3E0), the office's average (0x5007A0), its town's store down, statistics 0x4F8840 (`trade.cpp` `fn_53d3c0`) | market 0x4A8CF0 (mode 2: the town's units, the office's room [+0x2EC] · 2000) | 0x5361F1 → 0x53D3C0 |  | yes | read |
| 0x5A | player | something in an office (0x5308A0, 0x5303B0) | 0x5A0C30 | 0x536200 → 0x5308A0, 0x5303B0 | 1, 16 B |  | guess |
| 0x5B | player | a captain or administrator (0x530400) | 0x41ED90, 0x5DC2A0, 0x5DCDB0 | 0x536290 → 0x53D5C0 | 1, 272 B |  | guess |
| 0x5C | unused | something in an office (0x5308A0) | not found | 0x53629F → 0x53D6D0 | 1, 704 B |  | guess |
| 0x5D | player | open an office (0x5362AE, 0x51B890) | none found: not a literal anywhere, nor in the tavern's stored operations (0x55, 0x4C); the player opens an office by building it (0x21 with the type 0x2C) | 0x5362AE → 0x51B890 |  | yes | read |
| 0x5E | player | an office's administrator (0x5098B0, 0x5097C0) | 0x5DC2A0 | 0x5362F5 → 0x53D990 | 1, 160 B |  | guess |
| 0x5F | player | the letters 0x1E «Memo» / 0x1F «Offer» (0x5338A0) | 0x5BB9D0 | 0x536304 → 0x53DA30 | 4, 1.3 KB |  | read (mail.md) |
| 0x60 | simulation | a wedding (0x53DA90) | event 0x16 (dead), letter answers 0x4D6C90 | 0x536313 → 0x53DA90 |  | yes | read |
| 0x61 | simulation | a merchant's feast (0x53DBF0) | 0x5316E0; the feast window 0x5BAE40 | 0x536322 → 0x53DBF0 |  | yes | read |
| 0x62 | player | something in an office (inline, 0x536331) | 0x5AE4D0 | 0x536331 → 0x5308A0 |  |  | guess |
| 0x63 | player | something in an office (inline, 0x53639B) | 0x5DCA70 | 0x53639B → 0x5308A0 |  |  | guess |
| 0x64 | simulation | a theft (0x5363EE) | 0x4DD790 | 0x5363EE → 0x4D8CF0 |  | yes | read |
| 0x65 | simulation | an AI merchant goes bankrupt (0x53DD70) | 0x4F4E50 | 0x53642E → 0x5303C0, 0x4F5D80 |  | yes | read |
| 0x66 | player | something in an office (inline, 0x53644B) | 0x5DCDB0 (at 0x5DD479) | 0x53644B → 0x5308A0 |  |  | guess |
| 0x67 | simulation | an administrator improves (0x53DDF0) | 0x4DCEA0 | 0x5364B6 → 0x53DDF0 |  | yes | read |
| 0x68 | player | trade routes: a ship's route (the routes' object 0x6DD728; 0x4D5AB0, 0x5172A0...) | 0x45A6B0, 0x48AC30, 0x5492D0 | 0x5364C5 → 0x53DF00 | 4, 2.0 KB |  | guess |
| 0x69 | player | trade routes (0x53E480) | 0x404B50, 0x406D50, 0x407CA0, 0x5492D0 | 0x5364D4 → 0x53E480 | 1, 400 B |  | guess |
| 0x6A | player | trade routes (0x4D4C90, 0x4D4E80) | 0x492CA0, 0x5492D0 | 0x5364E3 → 0x53E610 | 5, 1.2 KB |  | guess |
| 0x6B | player | trade routes (0x4D4F60) | 0x492EE0 | 0x5364F2 → 0x53E7C0 | 9, 1.8 KB |  | guess |
| 0x6C | player | trade routes (0x4D4DD0, 0x4D4C90) | 0x492CA0, 0x5492D0 | 0x536501 → 0x53E890 | 2, 624 B |  | guess |
| 0x6D | player | trade routes (0x53E9C0) | 0x48AC30 (code in ebp), 0x5492D0 | 0x536510 → 0x53E9C0 | 2, 496 B |  | guess |
| 0x6E | player | a letter read (0x4D69D0, `mark_letter_read`) | letters 0x47F3B0 | 0x536AC7 → 0x4D69D0 |  | yes | read |
| 0x6F | player | a letter deleted (0x4D6A50, `delete_letter`) | mailbox 0x47C9B0 | 0x536AE1 → 0x4D6A50 |  | yes | read |
| 0x70 | player | a merchant's letter settings: {0x70, u16 merchant at [+4], [+6..+0xD] to his [+0x38..+0x3F] (the days each class is kept, the event sound, zeros), [+0xE] to his [+0x26] (the notices)}; a merchant under the count (0x4D6AF0, `mail.cpp` `set_letter_settings`) | the letters' settings window 0x408F50 on closing (0x4096DB) | 0x536AFB → 0x4D6AF0 |  | yes | read |
| 0x71 | player | the letters checked: {0x71, merchant, repair}: with repair, 0x4D7960 repairs the chains (a letter reached twice cuts the link to it; the free list ends at a letter already in a chain); then 0x4D6780 removes the merchant's old letters (`mail.cpp` `repair_letter_chains`, `expire_letters`) | the letters' settings window 0x408F50 ({player, 1}, 0x409735); the letter window 0x47C9B0 ({player, 0} once a tick while open, 0x47CD83), 0x47D1F0 | 0x536B1C → 0x4D7960, 0x4D6780 |  | yes | read |
| 0x72 | player | answer a letter: «It's a pity» (letter 0x2B; 0x53EAC0) | letter answers 0x4D6C90 | 0x536B43 → 0x53EAC0 | 1, 368 B |  | read (mail.md) |
| 0x73 | unused | nothing (0x537025) |  | — |  |  | read |
| 0x74 | player | answer a letter (0x53EC30) | letter answers 0x4D6C90 | 0x536B56 → 0x53EC30 | 1, 128 B |  | read (mail.md) |
| 0x75 | player | answer a letter (0x53ECB0, deletes it 0x4D6A50) | letter answers 0x4D6C90 | 0x536B65 → 0x53ECB0 | 4, 2.0 KB |  | read (mail.md) |
| 0x76 | player | remove a letter 0x71 (0x4D7870) | 0x5D8100 | 0x536B74 → 0x53EDF0 | 2, 352 B |  | read (mail.md) |
| 0x77 | player | answer a letter: the impounding (letter 0x83; a ship 0x507D60) | letter answers 0x4D6C90, 0x47F3B0 | 0x536B83 → 0x53EEC0 | 2, 2.5 KB |  | read (mail.md) |
| 0x78 | simulation | a new ship in [0x6DD7C0] (0x5051E0) | 0x4E3F44, 0x4E43E4 | 0x53651F → 0x5051E0 |  | yes | read |
| 0x79 | simulation | the Hanse diet: a byte per town (0x536535) | 0x4DC800 | 0x536535 |  | yes | read |
| 0x7A | simulation | the Hanse diet: a town's [0x834..0x83F] (0x536570) | 0x4DC6B0 | 0x536570 |  | yes | read |
| 0x7B | simulation | captains and administrators looking for work (0x5365E3) | 0x4E8E24 | 0x5365E3 |  | yes | read |
| 0x7C | simulation | the plague (0x536642) | 0x4DCA90; 0x419EB0; the mission interpreter | 0x536642 → 0x4D8CF0 |  | yes | read |
| 0x7D | simulation | a fire in a town (0x5366A0) | 0x4DCBF0; 0x419EB0; the mission interpreter | 0x5366A0 → 0x4D8CF0 |  | yes | read |
| 0x7E | simulation | the next pirate season (0x53F8B0) | 0x4E0334; the new game 0x544E50 | 0x5366D9 → 0x53F8B0 |  | yes | read |
| 0x7F | simulation | towns with the bit 0x4000000 and events 0x35 (0x5366E8) | 0x4E3F44 | 0x5366E8 → 0x4D8CF0 |  | yes | read |
| 0x80 | simulation | two fields of the queue's record 0x80 (0x536756) | 0x4E5B84 | 0x536756 → 0x4D8DB0 |  | yes | read |
| 0x81 | simulation | a task of the Hanse diet (0x53FA80) | 0x4DCD90, 0x4E09D4 | 0x536797 → 0x53FA80 |  | yes | read |
| 0x82 | player | a pirate attack on a ship (0x5063C0, 0x51A820) | 0x419EB0; the mission interpreter | 0x5367A6 → 0x53FB00 | 1, 416 B |  | guess |
| 0x83 | simulation | an event 0x00 (0x5367B5) | 0x4DB330 | 0x5367B5 → 0x4D8CF0 |  | yes | read |
| 0x84 | simulation | the Hanse leader's task (0x53FCA0) | 0x4DD530, 0x4E0334 | 0x5367E3 → 0x53FCA0 |  | yes | read |
| 0x85 | simulation | create a Hanse task (0x53FD10) | 0x5326E0 | 0x5367F2 → 0x53FD10 |  | yes | read |
| 0x86 | simulation | remove the Hanse tasks no longer valid (0x536801) | 0x5326E0 | 0x536801 → 0x4D8DB0, 0x532C50 |  | yes | read |
| 0x87 | simulation | a child is born (0x53FEA0) | 0x4F69B0 | 0x53687E → 0x53FEA0 |  | yes | read |
| 0x88 | player | a siege: a town's expense (0x529F90) and 0x6348E0 on the siege objects 0x6DF270 | 0x5F1F00 | 0x53688D → 0x529F90, 0x6348E0 | 1, 272 B |  | guess |
| 0x89 | player | a siege (0x635990) | 0x5F2200 | 0x5368E9 → 0x635990 | 1, 496 B |  | guess |
| 0x8A | player | a siege (0x6356D0) | 0x5F2550 | 0x53692B → 0x6356D0 |  |  | guess |
| 0x8B | player | a siege: the prince (letter 0x64 «Prince», 0x635B80) | 0x633AF0 (op) | 0x536974 → 0x635B80 | 1, 336 B |  | read (mail.md) |
| 0x8C | player | a siege: the prince (letter 0x65, 0x635CD0) | letter answers 0x4D6C90 | 0x5369C7 → 0x635CD0 | 1, 272 B |  | read (mail.md) |
| 0x8D | player | a siege (0x635DE0) | 0x5F2E90 | 0x536A2B → 0x635DE0 | 1, 1.1 KB |  | guess |
| 0x8E | simulation | a siege: the besieging army (0x633AF0) | 0x6349F0; 0x419EB0; the mission interpreter | 0x536A67 → 0x633AF0 |  | yes | read |
| 0x8F | unused | a siege's letters (0x38 «Siege», 0x37 «Extra tax»; 0x540240) | not found | 0x536AA9 → 0x540240 | 1, 592 B |  | read (mail.md) |
| 0x90 | player | a siege (letter 0x38, 0x634660, 0x62B860, 0x62DD70) | 0x5F2F90, 0x5F30B0, 0x5F31C0 | 0x536AB8 → 0x540490 | 6, 4.4 KB |  | read (mail.md) |
| 0x91 | battles | a ship takes another (0x540520) | the sea battle 0x60C1C9 (run at once) | 0x536B92 → 0x540520 |  | yes | read |
| 0x92 | battles | a town battle's order (0x540F50) | 0x4942C0, 0x60D259 | 0x536BA1 → 0x540F50 | 1, 256 B |  | read (battles.md) |
| 0x93 | battles | a battle's order (0x541050) | 0x6016CB, 0x608360 | 0x536BB0 → 0x541050 | 1, 432 B |  | guess |
| 0x94 | battles | the battles take their steps (0x536BBF) | the real-time loop 0x612DB0 | 0x536BBF → 0x612D20, 0x612C90 |  | yes | read |
| 0x95 | battles | a battle (0x541200, 0x6270C7) | 0x608550 | 0x536C43 → 0x541200 | 2, 656 B |  | guess |
| 0x96 | battles | a merchant's sides to the computer or back (0x60FEC9) | 0x5FBFE0, 0x5FFF40, 0x605420, 0x6100D9 | 0x536C52 → 0x60FEC9 |  | yes | read |
| 0x97 | battles | a battle (0x541380) | 0x6103E9 | 0x536C7F → 0x541380 | 1, 352 B |  | guess |
| 0x98 | battles | a battle (0x5414E0) | 0x48D420, 0x610579 | 0x536C8E → 0x5414E0 | 1, 256 B |  | guess |
| 0x99 | battles | a battle at sea from a chase (0x50B250) | not found | 0x536C9D → 0x50B250 |  |  | read |
| 0x9A | battles | a battle (0x5415E0) | 0x610C09 | 0x536CB7 → 0x5415E0 | 1, 288 B |  | guess |
| 0x9B | battles | a battle (0x541700) | 0x610FD9 | 0x536CC6 → 0x541700 | 1, 320 B |  | guess |
| 0x9C | unused | nothing (0x537025) |  | — |  |  | read |
| 0x9D | unused | nothing (0x537025) |  | — |  |  | read |
| 0x9E | unused | nothing (0x537025) |  | — |  |  | read |
| 0x9F | battles | ships join a sea battle (0x611309) | the chase 0x50B250 | 0x536CD5 → 0x611309 |  | yes | read |
| 0xA0 | battles | a battle (0x6116A9) | 0x42D230, 0x60D259 | 0x536D04 → 0x6116A9 | 2, 583 B |  | guess |
| 0xA1 | unused | nothing (0x537025) |  | — |  |  | read |
| 0xA2 | network | a network game's lobby (0x43E3C0, 0x439DB0; *0x6CBB94) | 0x547FD0 | 0x536D27 → 0x541840 | 1, 384 B |  | guess |
| 0xA3 | network | a network game's lobby (0x43E240) | 0x548160 | 0x536D36 → 0x5419C0 | 1, 240 B |  | guess |
| 0xA4 | network | a network game's lobby (0x43E1F0, 0x43FF40, 0x54BDD0) | 0x43C5F0, 0x43FD20, 0x547FD0 | 0x536D45 → 0x541AB0 | 2, 752 B |  | guess |
| 0xA5 | network | a network game's lobby (0x43EF70, 0x43FBA0, 0x440310) | 0x4381E0, 0x43C090, 0x43FF40 | 0x536D54 → 0x541CC0 | 124, 35.3 KB |  | guess |
| 0xA6 | network | a network game (inline, 0x536FBA) | not found | 0x536FBA → 0x5303C0 |  |  | guess |
| 0xA7 | network | a merchant joins a network game: a new merchant with a ship (0x541D70: 0x4F68D0, 0x507D60...) | 0x43D030, 0x497EE0, 0x542380 | 0x536D63 → 0x541D70 |  |  | guess |
| 0xA8 | network | a network game's lobby (0x43F660...) | 0x4335B0, 0x4381E0, 0x43A2A0, 0x43C090, 0x43C5F0, 0x43D030, 0x43FD20, 0x547FD0 | 0x536D72 → 0x5421E0 | 1, 416 B |  | guess |
| 0xA9 | network | queues more operations (0x542380) | 0x547F10 | 0x536D81 → 0x542380 | 1, 160 B |  | guess |
| 0xAA | network | a merchant's name (0x512C50; same handler as 0xAB) | 0x43D030, 0x497EE0, 0x541D70 | 0x536D90 → 0x5303C0, 0x512C50 | 1, 240 B |  | guess |
| 0xAB | network | a merchant's name (0x512C50) | 0x43D030, 0x497EE0, 0x541D70 | 0x536D90 → 0x5303C0, 0x512C50 | 1, 240 B |  | guess |
| 0xAC | new game | the game starts: the mailbox reset (0x4D64B0), an event (0x4D8CF0) | the new game 0x544E50 | 0x536DF2 → 0x542420 | 3, 352 B |  | guess |
| 0xAD | new game | 0x52BD80 | 0x544E50 | 0x536E01 → 0x52BD80 | 1, 272 B |  | guess |
| 0xAE | new game | a town's nodes (0x50FCA0, 0x50FE00) | not found | 0x536E2B → 0x5424D0 | 1, 144 B |  | guess |
| 0xAF | new game | found a town (0x531D50; with [+0xF] = 1, the new game) | 0x544E50; 0x419EB0 | 0x536E3A → 0x531D50 |  |  | read |
| 0xB0 | new game | names (0x512830, 0x6DDAA0) | 0x544E50 | 0x536F1D → 0x543570 | 3, 656 B |  | guess |
| 0xB1 | new game | 0x542860 (0x444BA0, 0x4482C0, 0x533510) | 0x544E50 | 0x536E50 → 0x542860 | 49, 13.5 KB |  | guess |
| 0xB2 | new game | 0x542980 | not found | 0x536E5F → 0x542980 | 3, 1.5 KB |  | guess |
| 0xB3 | new game | a new merchant with ships (0x542A30: 0x4F68D0, 0x507D60, 0x4F7BB0, 0x54E170) | 0x544E50 | 0x536E6E → 0x542A30 |  |  | guess |
| 0xB4 | new game | a pirate (0x513720, 0x513EC0, 0x513D80) | 0x544E50; the pirates 0x514920 | 0x536E7D → 0x542E40 | 1, 208 B |  | guess |
| 0xB5 | simulation | a pirate fleet leaves its nest (0x514D40) | 0x514000 | 0x536E8C → 0x514D40 |  | yes | read |
| 0xB6 | new game | a merchant's name (0x512BA0) | not found | 0x536EBE → 0x5303C0, 0x512BA0 | 1, 176 B |  | guess |
| 0xB7 | new game | the game starts: resets (mail 0x4D64B0, routes 0x4D4C60, statistics 0x54DDF0...) | 0x544E50 | 0x536F0E → 0x5434E0 | 8, 1.3 KB |  | guess |
| 0xB8 | simulation | network sync checks (0x542F10) | 0x4DD400, 0x4DDA40; 0x544E50 | 0x536F2C → 0x542F10 |  | yes | read |
| 0xB9 | network | nothing: a ship's checksum | 0x542F10 (op) | — |  |  | read |
| 0xBA | network | nothing: a town's checksum | 0x542F10 (op) | — |  |  | read |
| 0xBB | network | nothing: money, citizens, counters | 0x542F10 (op) | — |  |  | read |
| 0xBC | network | nothing: counters | 0x542F10 (op) | — |  |  | read |
| 0xBD | battles | open the battle window (0x462280, 0x41D9B0: interface) | not found | 0x536F3B → 0x462280, 0x41D9B0 | 1, 144 B |  | read |
| 0xBE | network | nothing: a merchant's checksum | 0x542F10 (op) | — |  |  | read |
| 0xBF | player | a merchant's ship lists (0x4F5500, 0x4F56C0) | 0x4F5500, 0x4F56C0 (op), 0x4F5840 | 0x536FAB → 0x5436D0 | 6, 1.7 KB |  | guess |
| 0xC0 | missions | the mission interpreter starts (0x511270, 0x532420...) | 0x43D030, 0x541D70 | 0x53701E → 0x543810 | 49, 14.2 KB |  | read |

## Above 0xC0: the real-time loop's operations

0x546870 (the loop that runs the queue, called from the real-time loop 0x546620)
sends the codes under 0xC1 to 0x535760 and runs 0xC1..0xD4 itself (table 0x547290,
20 entries). They are about time and the game session, not the world: 0xC2 the
autosave and 0xC4 advance the time (P3Modding's book; queued by 0x546620), 0xC8 the
battles' time mode (0x612C90, 0x612D20; [battles.md](../cloud/battles.md)) and the
game speed of many windows, 0xD2 (0x546870 itself), 0xD4 the network sync checks
(0xB8), and 0xC0..0xD1 from the network lobby (0x43xxxx), the start of a game
(0x544300, 0x544E50) and the battle window. The viewer has its own time controls;
these go with the network game (roadmap, phase 7).
