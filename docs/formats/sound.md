# The game's sound and music

What Patrician III plays and how it decides it, read from `Patrician3.exe` (GOG 1.1, English; addresses as in
`re/save_re/p3.asm`) and its files. The game plays everything with the Miles Sound System (`mss32.dll`); p3vita plays
it with SDL's audio (`src/app/audio.hpp`) and reads the files with `src/p3data/sound.hpp`. The roadmap's section 5 says
what is done.

## The files

- **Music and atmospheres**: MP3 files beside the game, in `Musik\` (76 MB; MPEG-1 layer III, 44,100 Hz, stereo,
  almost all at 128 kbit/s; the same in the English and the Spanish versions, whose `Musik\Tutorial\` alone has the
  tutorial's voices, 45 MB). Folders: `Atmos` (the atmospheres), `Gebaeude` (the buildings' music), `MenueMusic`
  (the main menu's and the score's), `RegionenMusic`, `Rendering` (the event videos' music), `SeekarteMusic` (the
  sea map's and the sea battles'), `StadtkarteMusic` (each town's own piece), `SeeschlachtWaves` (one WAV, unused:
  the archive has its own). On the Vita they go in `ux0:data/p3vita/Musik/`.
- **Sound effects**: 178 RIFF WAVE files in the archive (`musik\Add`, `BelagerungWaves`, `Gebaeude`, `MenueSounds`,
  `SeekarteWaves`, `SeeschlachtWaves`, `StadtkarteWaves`), Microsoft IMA ADPCM (format 0x11, 4 bits, 44,100 Hz; 172
  mono, 6 stereo), about 10 minutes in all (54 MB as 16-bit samples). An IMA ADPCM block starts with a 4-byte head
  per channel (the first sample, int16, and the step index, a byte, then a byte 0) and then 4-bit codes, the low
  nibble first; with two channels, 4 bytes (8 samples) of each in turn. The `fact` chunk gives the samples' count.
- **The event videos** (`Videos\*.sks`, «SK10»): JPEG pictures shown as a slide show with their music from
  `Musik\Rendering` ([Videos] below). The introduction alone is a Bink video (`Intro_2.bik`; `Intro_1.bik` and
  `Intro_3.bik` are empty, 5 frames; `sound.cp0` is also a Bink file).
- `P2.CFG` [OPTIONS]: `LSMUSIK`, `LSSOUND`, `LSVIDEO` (0..127; read at 0x45CE70, kept if 0..255), `SWAPSPEAKER`,
  `VIDEOS`. Without them: LSMUSIK 80 % of the options screen's `[Screen0] Musik=60` (options.ini), so 48; LSSOUND 80;
  LSVIDEO 80.

## scripts/sound.ini

Read with the game's own ini reader (0x4BE0B0 for a number, 0x4BE060 for a text; sections and keys in any case).

| Section | Keys | Read at | What |
|-|-|-|-|
| [SeaMap] | `Titel` = n; `TitelN`, `LoudnessN` (0..100, 100 without it) | 0x4276BD | the sea map's music (18) |
| [SeaMapAtmo] | `Titel0..1`, `Loudness0..1` | 0x427801 | the sea's atmosphere in summer and in winter |
| [TownMapAtmo] | `Titel0..2`, `Loudness0..2` (three, whatever `Titel` says) | 0x427630 | the town's atmosphere in summer, in winter and in the rain |
| [SeeschlachtAtmo] | `Titel0`, `Loudness0` | 0x42788E | a sea battle's atmosphere |
| [Region] | `Titel` = n; `Region0..6` (how many of the titles are each region's, in turn); `TitelN`, `LoudnessN` | 0x4274D8 | the regions' music (23: regions 0..4, then 5, the sea battles' three) |
| [Gebaeude] | `Titel` = n; `TitelN`; `m_BMusicVolumeInBuildings` | 0x441D02 | the buildings' music (12) |
| [Videos] | `VideoN`; `Volume` | 0x467321 | the event videos' music (37) |
| [Hauptmenu] | `Titel0`, `Titel1` | 0x42EE1E, 0x421866 | the main menu's music and the score's |
| [SoundListN] | `Count`, `Files`, `IDs` | 0x4439B0 | a list of sound effects: its variants |
| [SAMPLEn] | `SampleFile`, `Loudness` (100 without it) | 0x4439B0 | a sound effect |

The ini has 51 lists with 200 samples (`re/emu/sesion51/soundini.py` lists them); a list's samples have numbers of
their own (the list 7000: 7000, 7100, 7200). A list is loaded only when a screen asks for it («The sound effects»
below); one without `Files` or `IDs` (SoundList3100, the calm's, `Count=0`) isn't. The town's: «The town's
sounds» below. Still to read: the sea's 1000..5000 (0x4598xx..0x45A3xx: their variant by the world's rand() too), the
sea battles 60000.. and the sieges 61000.. (part 5).

iso.ini's «Sound Stuff»: `GebaeudeSoundListBase=10000`, `SoundsProGebaeude=10`, `GebaeudeTypLockTime=6000`,
`GebaeudeKanalLockTime=4000`, `GebaeudeMinDist=300`, `PanDist=500`, `VolDist=750`, `ChurchSoundList=12000`,
`ShipInOutSoundList=13000`, `BuildingCollapseList=9000`, `ShipSelSoundID=1000` (read at 0x588352). StadtDaten.ini:
each town's `NameDerStadtMusik` and `LautstaerkeDerMusik` (45 for all; read at 0x51E6BB). Rügenwald's says
`Ruegenwald.mp3`, but the file is `Rügenwald.mp3`: in the game that town has no piece of its own.

## The sound object (*0x6CBDB8)

| Offset | What |
|-|-|
| +0x0, +0x4 | the atmosphere's stream and the one fading out (at the sound's volume) |
| +0x8, +0xC | the music's stream and the one fading out (at the music's volume) |
| +0x10 | a building's music (at the sound's volume; closed when it ends, 0x442180) |
| +0x18 | 20 sample handles |
| +0x68 | Miles' digital driver: 0 without sound, and then nothing plays and 0x443690 gives 0 |
| +0x6C | Miles' «LowPass Filter» (cutoff 400 Hz, the float 0x66D640), on the atmospheres and the samples in a building |
| +0xCC | [Gebaeude]'s titles (0x441CD0) |
| +0x12C, +0x138 | the lists loaded (each its id and its variants' count) and their count |
| +0x1B8, +0x1BC | the music's and the sound's volumes (LSMUSIK, LSSOUND) |
| +0x1C4 | the level the music fading out stays at when it isn't closed (m_BMusicVolumeInBuildings, 3; 2 when 0) |
| +0x1C5 | in a building: the filter on |
| +0x1C6, +0x1C7 | a fade of the atmospheres, of the music |
| +0x1C8, +0x1CC, +0x1D0, +0x1D4 | the times those four streams' fades last moved |
| +0x1D8, +0x1D9, +0x1DA, +0x1DB | their loudness (0..100) |
| +0x1DC | the music fading out ends closed (else at [+0x1C4]) |
| +0x1DD | SWAPSPEAKER: a sample's pan is 127 − pan |
| +0x1DE, +0x1DF | (an event video's music: to read) |
| +0x1E0 | a building's music plays: no music or atmosphere starts |

A stream's volume is `min(volume × loudness / 100, 127)` (the option's volume 0..127, its loudness 0..100); a
sample's, `min(loudness × (sound volume × its own volume / 127) / 100, 127)` (0x443542).

- **0x4426C0** (path, loudness, crossfade, loop, force): a piece of music. Nothing without the driver, nor (unless
  `force`) with `volume × loudness / 100` = 0. The streams that ended are closed first (0x442180). With `crossfade`:
  the 20 samples end, the music playing becomes the one fading out (from its volume, [+0x1DC] = 1), the new one starts
  at its volume, and the atmospheres are closed at once; if it can't be opened, on as without it. Without it: the
  atmosphere becomes the one fading out, the new music starts at 0 and fades in; the music it replaces is forgotten
  (Miles plays it on to its end). `loop`: the loop count 0 (for ever). It gives the piece's length in ms. The main
  menu (0x42F1F0) plays [Hauptmenu] Titel0 with (100, crossfade, loop, force); the score (0x421F9F) Titel1 with (100,
  crossfade, loop).
- **0x444030** (each frame with a fade): a fade moves a stream's volume by one for each `5000 / its full volume` ms
  since it last moved (5 s from 0 to its volume or back); the one coming in stops at its volume, the one going out is
  closed at 0 (or, the music without [+0x1DC], left at [+0x1C4]). The flag is cleared when the stream going out is
  done, even if the one coming in hasn't reached its volume yet.
- **0x442D10**: every stream closed at once, but a building's (the options' «OK» with the music's volume 0, 0x460135;
  quitting to the menu, 0x45FFD8; the score, 0x421F79).
- **0x442180**: the music, the atmosphere and a building's music closed once they ended. **0x442230**: whether a
  piece of music plays, after it.
- **0x442C20**: what is left of the music in ms (−1 without the driver, 0 without music). **0x442C60** (path): a
  file's length in ms.
- **0x442A10** (path, from, loudness): an atmosphere. Nothing without the driver, in a building, or with the sound's
  volume 0. The atmosphere playing becomes the one fading out; the new one starts at `from` ms, again and again
  (the loop count 0), at 0 fading in; the music playing fades out (closed). It gives its length less `from`. (An
  empty path: only the fading out.)
- **0x4436E0** (in, building): into a building's window (its [Gebaeude] title: 0 the tavern, 1 the office, 2 the
  shipyard, 3 the church, 4 the lender, 5 the market hall, 6 the guild, 7 the town hall, 8 the bath house, 9 the
  weaponsmith, 10 the town gate, 11 the barn; each window's two calls, 0x5A650A..0x5EBAD7): the filter on the
  atmospheres, the samples stopped, and 0x442280: unless a building's music plays, its title again and again at the
  sound's volume, the music playing let down to [+0x1C4] ([+0x1DC] = 0), [+0x1E0] set. Out of it: no filter, the
  music let down back as the one playing fading in, the building's music closed, [+0x1E0] cleared.
- **0x443690** (list id): its variants' count, 0 without the driver or with the list not loaded. The battles take
  `rand() % count` of the world's rand() when it isn't 0 (`Sailing::battle_sound`).

## The sound effects

The 20 channels of the sound object ([+0x18]; p3vita `GameSound::play_sample`), each with its sample's id ([+0x7C]), a
reservation ([+0x18C]) and a «world» flag ([+0x1A0]):

- **0x4439B0** (list, file): a list loaded: its samples' ids ([+0x124]), files ([+0x74]) and loudness ([+0x78]);
  a sample plays only once its list is loaded, and 0x443690 gives 0 for a list not loaded. Loaded when each screen is
  made: the main menu 8000 and 6000 (0x42ED3C); the sea map 1000..5000 (0x44AA42); the town view its buildings' lists
  (from iso.ini) and 60000..61100, the battles' and the sieges' (0x5884A6..); and the bath house's window 7000
  (0x5B1D41), alone: until it has been opened, the prayer (7100) and the spit (7200), samples of that list, don't
  sound (a bug; p3vita loads it before them with the fixes on).
- **0x443150** (sample, world, reserve, volume, pan, no start, force): nothing without the driver; without `force`,
  nothing in a building ([+0x1E0]) or with an event video ([+0x1DF]); the letters' 6000 not while one plays; nothing
  with the 20 channels playing. The sample's file read into a cache of 20 (0x4432D4..); `volume` −1 is 127, 0 nothing.
  A channel neither reserved nor playing: its pan (127 − pan with SWAPSPEAKER), unless `no start` its volume
  `min(loudness × (sound volume × volume / 127) / 100, 127)` and started (the streams serviced); `world`: [+0x1A0] set
  and, in a building, Miles' low-pass; `reserve`: [+0x18C] set. Its channel.
- **0x4430D0** (channel): its reservation and flag cleared. **0x442130**: how many channels play. **0x443110**:
  whether the prayer (7100) plays. **0x443940**: every channel stopped and freed (the town view's own first,
  0x59C010). 0x4426C0 with crossfade ends the 20 (their reservations stay); 0x4436E0 into a building stops them.

Who plays what (the interface's; the town's and the sea's are parts 4 and 5):

| Where | Sample | How |
|-|-|-|
| The main menu, an entry chosen (0x42FA82) | 8000 okbottun_menu.wav | not forced |
| A new letter (0x4D7B10 → 0x4D7C06) | 6000 Nachrichten.wav | forced, for the classes (0x6C0198) the player's [+0x26] bits 0..2 want |
| A game loaded (0x548C6E → 0x4D7D50) | 6000, for each unread letter ([+1] & 0x40) | as above (one sounds) |
| The bath taken (0x5B1EC1) | 7000 Badnehmen.wav | forced |
| The church's «Pray» (0x5A5B76) | 7100 Beten.wav | forced and reserved, if no channel plays; the four buttons off while it plays (0x5A5AF9); stopped when the window closes (0x5A5957) |
| The tavern's sailor spits (0x5CD6D9) | 7200 Rotzmitwuerg2.wav | forced, if no channel plays |
| A window can't close (0x462230, 0x4622F9, 0x462420) | 8001 alleButtons.WAV | (the viewer's Circle has no such case) |

**The interface's sounds** (*0x6DCCDC, made at 0x441FF1: 0x4C4130; 4 channels): the buttons' `HSoundID` (hover,
[+0x7C]) and `PSoundID` (pressed, [+0x78]) of the screens' ini (0x4B1EC0; −1 without, 0x670F74), indexes in the table
0x6BF118 (0 okbottun_menu.WAV, 1 alleButtons.WAV, 2..6 hawk.wav, 7..19 sound/sound02..14.wav, not in the data);
0x4C45A0 plays one on a free channel, at the sound's volume (0x4C4540 with `volume × 100 / 127` as a percentage of 127).
Only `PSoundID=1` is used: options.ini's buttons (BUTTON3, BUTTON4 the 149 × 36 ones, BUTTON6..10 the «−», «+», «←»
and «→», BUTTON14) — the options screen and the new game window, its details and its maps' window, which use them —,
mplayer.ini's and Stadtauswahl.ini's BUTTON4.

## The town's sounds

The town view (*0x6E51AC; p3vita `src/app/town_sounds.hpp`) reads iso.ini's [LIGHTNINGVIEW0] «Sound Stuff» at 0x588352
([+0xC7DC] GebaeudeSoundListBase 10000, [+0xC7E0] SoundsProGebaeude 10, [+0xC7D0] GebaeudeMinDist 300, [+0xC7D4]
PanDist 500, [+0xC7D8] VolDist 750, [+0xC7E4] GebaeudeTypLockTime 6000, [+0xC7E8] GebaeudeKanalLockTime 4000,
[+0xC994] BuildingCollapseList 9000, [+0xCBA4] ShipSelSoundID 1000, [+0xCBA0] ShipInOutSoundList 13000, [+0xCB98]
ChurchSoundList 12000) and loads the lists 13000, 12000, `10000 + 10 · type` for the 106 types, 9000 and
60000..61100. Its 17 slots ([+0xC998], 0x18 bytes: x, y, the offer's squared distance, the channel, its end, its type,
taken) are the buildings' (0..3) and the ships' (14..16); 0x59C010 forgets them and the types' waits ([+0xC7EC]).

- **0x594C00** (x, y, type: the view's pixels): a building cell's sound offered, from its class's step (0x592000 →
  +0x38): the default 0x552C10 after its «active» (+0x80; the workshops 0x54F710), and the sawmill's 0x556860, the
  foundry's 0x555190 and the fisherman's hut's 0x555B00 with their own; the type is the class's .let type
  ([[+0x74] + 0x2D], +0x50 0x54E930). The classes with a step and a list: the shipyard (01, 10010), the church (04),
  the lighthouse (15, 10210), the poorhouses (50, 10800), the sheep (55), cattle (56) and bees (59) farms, the smithy
  (5B), the mine (5F), the foundry (60), the pitch maker (61), the quarry (62, Ziegelei) and the sawmill (64)
  (`re/emu/sesion51/soundclasses.py`); the market stalls', the well's and the crane's classes (06/0A/37, 16: their
  +0x38 0x550370 does nothing) never sound, nor fire's list (type 0) but from a burning building's step (0x550A70 →
  0x5506D0 → 0x550CC9). Nothing without the list, with the type waiting ([+0xC7EC]), with the 4 taken or farther than
  GebaeudeMinDist from the view's middle; else (type ≠ 0) a slot offered and not started: the nearer of the two;
  else a free slot; else a not started farther one. ([+0x56C] with a type ≠ 0 leaves one slot: the fire's. [+0x56C] is
  set by the view's periodic 0x58B82E: the town shown's burning list [0x76A] not empty; `TownSounds::set_burning`,
  the living town's part 8.)
- **0x594770** (each frame): each offered slot not started: a variant (`rand() % count` of the world's rand() with
  more than one), at **0x594B50**'s volume (`127 − (dx² + dy²) · 127 / VolDist²`, 0 past it) and side (`dx · 64 /
  PanDist + 64`, 0..127) from the view's middle: 0x443150 (world, reserved). Its end: its length, and for a type ≠ 0
  `r % KanalLock + KanalLock / 2` more; the type's wait from its end `r % TypLock + TypLock / 2` (one rand() for
  both). Not played: the slot cleared.
- **0x5948D0** (each frame): the slots past their end with their sound done, released and cleared; with the view
  moved ([+0xC640] ≠ [+0xC648]...), the others' volume (0x443000) and side (0x4438D0) again.
- **0x59A090** (x, y, 0 in / 1 out; from the port's ships, 0x6135C0, 0x615100, 0x616805): with the town shown and a
  volume, ShipInOutSoundList + kind in the last free of the slots 14..16, unless the same kind's started this very ms.
- **The bells** (0x58B945..0x58BA5D, each frame with a town, by GetLocalTime): at minute 30 once an hour ([+0xCB9D])
  12001; at minute 0 once an hour ([+0xCB9C]) the strikes ([+0xCB94]) its hour (13..23: − 12; midnight none), then
  12000 (reserved, [+0xCB90]) each time the last one is done, one strike less.
- The ship chosen in the port (0x5958CD, 0x598AB9: after 0x495190): ShipSelSoundID (1000) at 127; a building fallen
  (0x599040 from the simulation: 0x4E1DBF.., the fires and the sales) its list 9000 by its place (0x5993AA: with the
  town view on the screen, a variant `rand() % count`, 0x594B50's volume and side at the cell's point, 0x443150 not
  kept; `TownSounds::collapse`, the living town's part 8; before it, 0x5990A2: the slots of a fire's sound on that
  building's cells stopped and cleared, `TownSounds::stop_fire`); the 4 special slots
  [+0xCB30] (0x594AA0, by an id; 0x594A20 stops it) only for the battles' (0x595B31).

## The music director (0x42C080)

Called each frame (0x401ABA: the screen *0x6CBA64, of 0x278 bytes, not identified; 0x423D26: the main screen
*0x6CBB40; 0x45F25E: the options screen *0x6CBDF0; not the saved games' screen), on the main screen, whose
[+0x287C..0x28C8] hold what sound.ini's music sections say (loaded at 0x427400): [+0x2884] the next piece's time,
[+0x2888] the last piece's index, [+0x28B4] an atmosphere's length, [+0x28B8] its start, [+0x28C4] its time. Its
choices take **the world's rand() (0x639C03)**: `f = rand() / 32768.0` (0x66CB34) as a float, then `_ftol(count × f)`
or `_ftol(f × −15000.0)` (0x66CB30; −10000 0x66CB38, −5000 0x66CB3C). p3vita: `src/app/music_director.hpp`, with a
generator of its own (as for the buildings' gestures; roadmap section 8).

1. The fades (0x444030), if one goes on.
2. With the music's volume not 0 ([*0x6CC3E8 + 0x21], LSMUSIK) and [+0x2884] past: a piece. With the options screen,
   the screen *0x6CBA64 or the sea map (0x4B2200 on *0x6CBDD8) shown, one of [SeaMap]; else one of the region's of
   the town view's town ([*0x6E51AC + 0xC324]): the region from [0x6DDBC0 + 0x34 · town], StadtDaten.ini's table by
   the town's **index** (a bug: the table goes by the town's number, 0x6DE4B8); regions 0..4 their [Region] titles,
   above 4 the first title; without a town (0xFF, a battle) the sea battles' (Region5). The same index as [+0x2888]:
   again 20 s later, the atmosphere at once. Else played (0x4426C0 without crossfade, loop or force), the atmosphere
   at its end − 5 s, the next piece at its end + 40 s + `f · 15 s`.
3. At [+0x28C4]: with more than 5 s of music left, later by that. Else an atmosphere: with the town view shown
   (0x4B2200 on *0x6E51AC), a battle's ([SeeschlachtAtmo], at the loudness 100) with a battle on the screen
   ([0x6E59CC] ≠ 0xFF), else the rain's with a storm by the town (0x59C050: the town's [+0x9C4] & 2, the view's
   [+0xC7CC] & 2), else the winter's with the town's ice ([*0x6E51AC + 0x504] 2..5), else none with its tints
   (([+0xC1A8] | [+0xC178]) & 0xFF000000), else the summer's; at sea the winter's in November and December (the month
   of 0x5311D0 above 10), else the summer's. From `_ftol(length · f)` (0 with the music's volume 0); the next time at
   its end − 5 s.

**0x42C770** (kind, stop the samples, force), the events:
- 0, the town view shown (0x589010, its Show: 0x589035 with the samples stopped; 0x58F975, forced, when the view's
  tints change): with a piece playing (and not forced) the atmosphere at its end − 5 s and the next piece at its end +
  10 s + `f · 10 s`; else the next piece in 15 s + `f · 5 s` and the town's atmosphere as in 3 (its start a random
  point leaving at least the time to the next piece, 0 with the music's volume 0) — but played **the summer's with the
  ice and the winter's without it** (0x42CCD1, a bug; its length is the right one's); none: 0x442A10 with an empty
  path.
- 1, the sea map shown (0x44F680, its Show; 0x44F6B9): the same with the sea's atmosphere, also when the town view's
  town is past the towns' count (a battle's), and its next time at its end, not − 5 s.
- 2, a battle on the screen (0x589E02, the town view's town 0xFF): one of the sea battles' at once, with crossfade; the
  next piece at its end + 15 s + `f · 5 s` (the same `f`).

**0x42CD90** (town): the town's own piece (StadtDaten.ini's NameDerStadtMusik and LautstaerkeDerMusik, by its number
0x6DE4B8; Rügenwald's, `Ruegenwald.mp3`, isn't the file's name, `Rügenwald.mp3`: a bug), played as in 2; the next piece
at its end + 15 s + `f · 5 s`. From 0x58A4F0 (the town view's town) when its third argument is 1: only from the sea
map into a town where the player has a ship or an office (0x44A1B7).

The options' «OK» (0x46010A): with the music's slider at 0 and a game, every music stopped (0x442D10) and, with the sea
map made, [+0x2884] now and [+0x28C4] now − 1 s; from 0 to more, both now. The game's main screen made (0x423385):
both 0.
