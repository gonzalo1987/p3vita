# LCR: the town view's animations («creatures»)

`iso/lcr/*.lcr` (288 files in the GOG 1.1 CPR, listed in `iso/lcr/files.lst`): what the game's town view
animates, from the town's people and animals to a battle's ships, their smoke and the cannon balls. Each file
names its images (AIM or BMP in the CPR) and gives, for each direction and frame, the rectangle to draw from
them and the point of it that stands on the creature's place. Read by `src/p3data/lcr.cpp` (`parse_lcr`), worked
out from the files on 2026-11-08 (the battles' piece 7, part 3) and checked on the 80 ships' looks and the 108
effects; the town people's files with several sequences (`armbrust_pa`, `bogen_pa`, `musket_pa`) and the old
ship files (`holk_0`..`4`, `kogge_0`..`4`...) have another layout and aren't read.

The game loads them at start (0x5894F0: each name of `files.lst`, its u16 at 2 and its class at 4, an object of
that class, 0x54F7E0, registered by the id); its loader 0x551000 checks a second byte 0x52, which these files
don't have (they are version 6), so the layout below comes from the files themselves.

## Head

| Offset | Type | What |
|-|-|-|
| 0x00 | u16 | 0x0600 |
| 0x02 | u16 | the id (not unique: the 8 directions of a sinking ship share one) |
| 0x04 | u32 | the class: 0x0D a ship's look, 0x0E the cannon ball, 0x12 an effect, 0x13 splinters, 0x14 a cloud, 0x17 a projectile, 0x1C fog... |
| 0x08 | u32 | the key: 0xFFFF00FF, the images' magenta (FF00FF) is transparent; 0, they have their own alpha (`_tp` images) |
| 0x0C | u32 | ? |
| 0x10 | u32 | ? (a ball's 4000) |
| 0x14 | u32 | the directions (32 a ship's look, 16 the guns' smoke and fire, 8 the town people, 1 most effects) |
| 0x18 | u32 | frames per second (10 the guns' smoke, whose image is `_X_kanonenqualm_10fps_tp.aim`; 0 a ship's look) |
| 0x1C | u8 | 1 |
| 0x1D | 6 × u32 | for an effect `[1, S, 1, 0, its last frame, 2]`: **S is the number the game asks for it by**, with its class (the battle code's 0x590610 calls: 0x12/0x24 `brandrauch` the smoke of a burning or sinking ship, 0x12/4..7 the upper part of a sinking snaikka, craier, cog, hulk, 0x12/0x40..0x43 their lower part, 0x12/0x21 a gun's smoke, 0x12/0x25 a heavy gun's fire, 0x12/0 a splash, 0x12/0x20 a puff, 0x12/0x11 an explosion); the variant (the call's high word) picks among the files of one S (a sinking ship's direction, 1..8 from north clockwise: `n`, `no`, `o`, `so`, `s`, `sw`, `w`, `nw`). A ship's look has `[4, 2, 0, 110, 1, 0]`. Classes 0x14 and 0x17 have a longer head and another layout |
| 0x35 | u32 + bytes | the shadow image's name: its length with the NUL (0: no shadow), the bytes (`\` and `/` mixed) |

## Directions and frames

Then, for each direction: u32 its frames, and each frame:

| Words | What |
|-|-|
| 2 × s32 | the anchor x, y: the point on the creature's place, in the main image's coordinates |
| 4 × s32 | (with a shadow) the shadow's rectangle x, y, w, h in the shadow image |
| 2 × s32 | (with a shadow) where the shadow's corner goes, x, y, in the main image's coordinates |
| u32 + n × 4 s32 | the strips: n vertical strips (x, y, w, h; 17 pixels wide, half a town cell) by which the town view sorts it with the buildings |
| 4 × s32 | the body's x1, y1, x2, y2 (inclusive) in the main image |

A ship's look has 32 directions of one frame each (so each direction's record starts with its count, 1). After
the last direction comes the main image's name (u32 length, bytes). The looks of hull state 0 go on after it with
the main image again and the icons the town view puts over a ship (`markerpfeil01.aim` the marker arrow,
`weisseFlagge.tga` the white flag, `shield_autohandel.tga`, `Versteigerung und Verkauf01.tga`...).

To draw a frame with its anchor at (x, y): the body's rectangle at (x − (anchor x − x1), y − (anchor y − y1));
its shadow's rectangle at (x + shadow x − anchor x, y + shadow y − anchor y), drawn black and see-through.

## A ship's look

`<type>_schadenHH_segelSS.lcr`, type `schnigge`, `kraier`, `kogge`, `holk` (the ship types 0..3), hull state HH
0..3 and sails SS 0..4 (the scene ship's [+0x26], 0 furled): 80 files. The town view's ship sprite (vtable
0x6758F8) keeps the look as type · 20 + hull state · 5 + sails ([+0x2C], 0x56B140); the hull state comes from
0x56E000: the hull in percent, one less unless 0, at most 99, and 3 − that / 25 (with the damage looks, byte
0x6C4010, 1 by default; without them always 0). The frame of a direction d (256ths of a turn, 0 north, clockwise)
is (d · 32 + 128) >> 8 & 31 (0x56C99F). Shadows: the cog has its own for hull states 0 and 3
(`_X_Kogge_Schaden00_Segel00_Shadow.aim`...), the others one per sails or per state; the looks of states 1 and 2
reuse them.
