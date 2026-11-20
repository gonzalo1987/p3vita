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

The ini has 51 lists with 200 samples (`re/emu/sesion51/soundini.py` lists them). A list is loaded only when a screen
asks for it (0x4439B0: the main menu at 0x42ED3C, the sea map at 0x44AA42.., the town at 0x5884A6..); one without
`Files` or `IDs` (SoundList3100, the calm's, `Count=0`) isn't. Which list sounds when: part of the roadmap's section
5 still to read (the buttons 8000, the letters 6000, the bath and the church 7000, the sea's 1000..5000, the town's
buildings from `GebaeudeSoundListBase` in iso.ini, the church bells 12000, ships in and out 13000, a building falling
9000, fire 10000, the sea battles 60000.., the sieges 61000..).

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
| +0x10 | a third stream (closed when it ends, 0x442180; [+0x1DE]) |
| +0x18 | 20 sample handles |
| +0x68 | Miles' digital driver: 0 without sound, and then nothing plays and 0x443690 gives 0 |
| +0x6C | a filter for the samples ([+0x1C5] on) |
| +0x12C, +0x138 | the lists loaded (each its id and its variants' count) and their count |
| +0x1B8, +0x1BC | the music's and the sound's volumes (LSMUSIK, LSSOUND) |
| +0x1C4 | the level the music fading out stays at when it isn't closed |
| +0x1C6, +0x1C7 | a fade of the atmospheres, of the music |
| +0x1C8, +0x1CC, +0x1D0, +0x1D4 | the times those four streams' fades last moved |
| +0x1D8, +0x1D9, +0x1DA, +0x1DB | their loudness (0..100) |
| +0x1DC | the music fading out ends closed (else at [+0x1C4]) |
| +0x1DD | SWAPSPEAKER: a sample's pan is 127 − pan |

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
- **0x442D10**: every stream closed at once (a game loaded, 0x460135; quitting to the menu, 0x45FFD8; the score,
  0x421F79).
- **0x442180**: the music, the atmosphere and the third stream closed once they ended.
- **0x442C20**: what is left of the music in ms (−1 without the driver, 0 without music). **0x442C60** (path): a
  file's length in ms.
- **0x442A10**: an atmosphere (the music director's; to read).
- **0x443690** (list id): its variants' count, 0 without the driver or with the list not loaded. The battles take
  `rand() % count` of the world's rand() when it isn't 0 (`Sailing::battle_sound`).

## The music director (0x42C080)

Called each frame (0x401ABA, 0x423D26, 0x45F25E) on the game's screen (*0x6CBB40), whose [+0x287C..0x28C8] hold
what sound.ini's music sections say (loaded at 0x427400). Read so far (to be rewritten, roadmap section 5):

- With the option at [*0x6CC3E8 + 0x21] on, at [+0x2884] (a time) a new piece: on the sea map (or the battle's screen, or 0x4B2200 of *0x6CBDD8) one of
  [SeaMap]; in a town one of its region's ([Region], the town's region at 0x6DDBC0 + town × 0x34; without a town the
  sea battles' three). Chosen with **the world's rand() (0x639C03)**: `int(rand() / 32767.0 × count)`. The same as the
  last one: tried again 20 s later. Played without crossfade (it fades in and the atmosphere out); the next one at
  its end + 40 s less a random part; the atmosphere 5 s before its end ([+0x28C4]).
- At [+0x28C4] the atmosphere (0x442A10), at a random point of it (rand() again, with that option on): in a town the
  battle's with a battle on the screen, else the rain's when it rains (0x59C050), the winter's when [+0x504] of the
  town view is 2..5, none with [town+0xC1A8 | +0xC178] & 0xFF000000, else the summer's; at sea the winter's in the
  months 0x5311D0 says, else the summer's.
- 0x42C770 (an event: 0, 1, 2; from 0x44F6B9 and the town's 0x589035, 0x589E02, 0x58F975): 2 plays one of the sea
  battles' three at once.

p3vita uses an RNG of its own for these choices (as for the buildings' gestures): with the music on, the game's
choices change the world's random numbers (roadmap section 8).
