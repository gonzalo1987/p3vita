# Cloud task: writing savegames

Branch `cloud/savegames`. Task: `p3vita-datos/tasks/savegames.md`. These notes
are written as the work goes; the sections "For the roadmap", "For the
inventory" and "Game bugs" are meant to be moved into the shared docs when
merging on the PC.

## Status

| Part | State |
|-|-|
| 1. Reader ↔ writer table of the game | done (below) |
| 2. Oracle: the game's writer in the emulator | pending |
| 3. Version 0x85 branches in the readers | pending |
| 4. C++ writer (`savegame_write.cpp`), byte for byte | pending |
| 5. LZ compressor rewritten in C++ | pending |
| 6. After playing, round trip, `.pst` | pending |

## 1. How the game saves

The save function is **0x5472F0** (thiscall on the game object 0x6DF2F0, the
same `ecx` the loader 0x5476E0 gives to 0x549F40; one argument). In order:

1. For each merchant `i < [0x6DE4AA]`: **0x4D6780(i)** on the mailbox
   0x6DD730 (expires old notices; p3vita has it as a stub, `fn_4d6780` in
   `town_tick/events_towns.cpp`, because the mailboxes of the saves are empty).
2. Builds the path (`Save\Ein\`, `Save\Kam\` or `Save\Mehr\` from byte
   [+0xD] of the settings, then the name, then `.pat`) and opens it with
   `CFile` (0x653C1D, mode 0x9001).
3. **0x549C60** → `n`, the size of the uncompressed `.pat`; two buffers of
   `n` and `n + 4` bytes; **0x549CC0(buffer)** writes the `.pat`;
   **0x54C410(src, dst, n)** compresses (on a local object built by
   0x54C3F0) and returns the compressed size; `CFile::Write` (0x653D74).
4. The same for the `.pst` on 0x6DFC90: **0x54D7F0** (size; it also sets
   the u16 [+0xA] to the number of objects of its list), **0x54D870**
   (writer), 0x54C410, write.

Every writer has the reader's shape: `thiscall(object, u8* out)` returns the
end. Every size function only reads.

**The game writes version 0x85** (first u32 `0x0085AFFE`), not the 0x82 of
the saves in `missav/`. So p3vita must write 0x85 and, for the round trip,
read 0x85 too (part 3).

### 1.1 Reader ↔ writer ↔ size

Readers as in `docs/formats/savegame.md`. "Object" is `ecx`.

| Part | Object | Reader | Writer | Size |
|-|-|-|-|-|
| whole `.pat` | 0x6DF2F0 | 0x549F40 | 0x549CC0 | 0x549C60 |
| compression | local | 0x54C480 (decompress) | 0x54C410 → 0x6381E0 | |
| world | 0x6DE4A0 | 0x52FB60 | 0x52F870 | 0x52F760 |
| · town block | town | 0x51B520 | 0x51B330 | 0x51B2D0 |
| · · storage (towns and offices) | | 0x52DF20 | 0x52DE90 | |
| · · part 1 | | 0x527AC0 | 0x527910 | |
| · · part 2 | | 0x525E30 | 0x525D80 | |
| · · part 3 (records, `town_class1`) | | 0x51EC10 | 0x51EA30 | |
| · · · church | | 0x4FE240 | 0x4FE1E0 | |
| · · · `town_class1` (also the pirates' map) | | 0x4F9770 | 0x4F9680 | |
| · · part 4 | | 0x52A2B0 | 0x52A200 | |
| · · part 5 | | 0x528870 | 0x5287A0 | |
| · · facilities | | 0x510070 | 0x50FFF0 | |
| · · nodes [0x9B4] | | 0x50FD40 | 0x50FCE0 | |
| · siege of each town | *0x6DF270 | 0x633730 | 0x6335A0 | 0x633530 |
| · · besieging army | | 0x627AD0 | 0x627920 | |
| · records of 0x14 | *0x6DE510 | 0x4D32F0 | 0x4D3250 | 0x4D3240 |
| · office | | 0x4FEFF0 | 0x4FED50 | 0x4FED30 |
| · merchant | | 0x4F4100 | 0x4F4010 | 0x4F3FE0 |
| · · its body | | 0x4F6FA0 | 0x4F6CF0 | |
| · · · first 268 bytes | | 0x4F6240 | 0x4F6090 | |
| · · · 100 bytes on [0x4D8] | | 0x534830 | 0x5347F0 | |
| · object 0x6DE588 | 0x6DE588 | 0x5351D0 | 0x535160 | 0x5350F0 |
| · 15 world objects [+0xB10] | | 0x52BBE0 | 0x52BB30 | (inline) |
| · optional object (list) | | 0x533DA0 | 0x533D10 | 0x533CF0 |
| ships | 0x6DD7A0 | 0x504230 | 0x504010 | 0x503F70 |
| · captain | | 0x4FDEB0 | 0x4FDE30 | 0x4FDE20 |
| · ship | | 0x517990 | 0x5177D0 | 0x5177B0 |
| · · its first part | | 0x516520 | 0x5162D0 | |
| · convoy | | 0x5014E0 | 0x5013D0 | 0x5013C0 |
| · pirate | | 0x5138B0 | 0x5137E0 | 0x5137B0 |
| mailbox | 0x6DD730 | 0x4D6350 | 0x4D62B0 | 0x4D6270 |
| · notice | | 0x51A9B0 | 0x51A8D0 | 0x51A890 |
| records of 0xDC | 0x6DD728 | 0x4D4BC0 | 0x4D4B60 | 0x4D4B40 |
| event queue | 0x6DD73C | 0x4D81E0 | 0x4D80F0 | 0x4D8080 |
| · node lists of event 0x34 | | 0x4F3A40 | 0x4F39E0 | 0x4F39C0 |
| battles | 0x6E55D0 | 0x611AE0 | 0x611950 | 0x6118F0 |
| names | 0x6DDAA0 | 0x511E30 | 0x511C60 | 0x511B90 |
| whole `.pst` | 0x6DFC90 | 0x54DA60 | 0x54D870 | 0x54D7F0 |
| · statistics series (4 optional) | | | 0x54D080 | 0x54D050 |
| · objects of 0x2310 (list [+0x1C]) | | 0x54CBD0? | 0x54CBD0? | 0x54CB90 |

(The last row: the writer calls 0x54CBD0 for each object; the matching
reader is still to be confirmed.)

### 1.2 What 0x549CC0 does besides writing

Before writing the 0x4C bytes of the settings (`*0x6CC3E8`) it changes them
**in memory**: [+0x1A] = day, [+0x1B] = month + 1, u16 [+0x1C] = year of the
world; [+0x1E] = hometown of the player (0x6DFC14) unless `[0x6DF2F0] & 0xC`
(then 0); [+0x27..+0x3A] = 0 and the name of that town copied in
(`strcpy` from the table of pointers 0x6DDA00); [+0x3B] = (male of the
player) << 7 | his rank in the hometown (merchant [0x39C + hometown]);
[+0x3C..+0x4B] = 0. Then it writes `u32` [0x6DFBC4], [0x6DFBC8], [0x6DFBCC]
([+0x8D4..+0x8DC] of 0x6DF2F0: ms per tick, a speed value the loader clamps
to 2 when it is above 4, and one more) and the eight sections, each followed
by `u32 0xAFFE`.

### 1.3 What the loader does that the oracle and p3vita don't

0x549F40 also clears [0x6DFBF0..+0x14], clamps [+0x8D8], and after the
sections: registers the name of each ship of each merchant and pirate in
the names object (0x5131B0), checks the route of each ship (0x5048C0 →
0x517C10 per ship: may move it or recompute its route) and checks the
records of 0xDC (0x4D60B0). `town_oracle.py` calls the readers one by one
and does none of this, so `tick_probe --carga` doesn't see it either. See
"For the roadmap".

The `.pst` is optional for loading: if it isn't found, 0x5476E0 calls
0x54DDF0 (empty statistics) and goes on.

## For the roadmap

- Loader: 0x549F40 does more than the readers (§1.3): 0x5131B0 for the
  ship names, 0x5048C0/0x517C10 for the ship routes, 0x4D60B0, the clamp of
  [+0x8D8] and the 12 bytes [0x6DFBC4..0x6DFBCC], which p3vita skips today.

## For the inventory

- New: the save path (0x5472F0 and everything under 0x549CC0, 0x549C60,
  0x54C410, 0x54D7F0, 0x54D870).

## Game bugs

(none yet)

## Changes to shared files

(none yet)
