#pragma once

// The game's sound object (*0x6CBDB8, made on Miles Sound System) on SDL's
// audio, for the PC and the Vita: its music and atmospheres, MP3 files beside
// the game played as streams with the game's fades (docs/formats/sound.md).
//
// The game keeps two streams of each kind: the atmosphere ([+0x0]) and the one
// fading out ([+0x4]) at the sound's volume; the music ([+0x8]) and the one
// fading out ([+0xC]) at the music's volume; and a building's music ([+0x10])
// at the sound's volume. A stream's volume is 0..127: the option's volume
// (0..127) × its loudness (0..100) / 100, at most 127; heard as that / 127 of
// its samples.

#include <SDL.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace p3::data {
class Mp3Stream;
}

namespace p3::app {

// One of the game's loose files (its `path` as the game names it, as «musik\MenueMusic\hauptmenue.mp3») in the first
// of `roots` (folders ending in '/') that has it, the case of its letters not minded (the game's names don't always
// match the files'); "" if none has it.
std::string find_game_file(const std::vector<std::string>& roots, const std::string& path);

class GameSound {
public:
	GameSound();
	~GameSound();
	GameSound(const GameSound&) = delete;
	GameSound& operator=(const GameSound&) = delete;

	// The audio device (44100 Hz, 16 bits, stereo) and the folders with the
	// game's Musik folder. Without a device the sound stays off, as the game
	// without Miles' driver ([+0x68]).
	bool open(std::vector<std::string> roots);
	// The device closed (before SDL_Quit) and everything stopped.
	void close();
	bool on() const { return device_ != 0; }

	// The options' volumes, 0..127 (P2.CFG LSMUSIK and LSSOUND: [+0x1B8] and
	// [+0x1BC]); the streams playing take theirs at once.
	void set_volumes(int music, int sound);
	// sound.ini's [Gebaeude] titles ([+0xCC]) and m_BMusicVolumeInBuildings, the level the music goes down to in a
	// building ([+0x1C4]; 2 when 0), as 0x441CD0 reads them.
	void set_building_music(std::vector<std::string> titles, int low);

	// 0x4426C0: a piece of music, the game's path, `loudness` 0..100. Not
	// played (0) with the music's volume at 0 unless `force`, nor in a
	// building. With `crossfade` the sound effects stop (part 3), the music
	// playing fades out and the new one starts at its volume, and the
	// atmospheres stop at once; without it the new one fades in from 0 and the
	// atmosphere fades out (the music it replaces, if it hasn't ended, plays
	// on to its end as the game's, which forgets it). `loop`: again and again.
	// Its length in ms, or 0.
	uint32_t play_music(const std::string& path, int loudness, bool crossfade, bool loop, bool force);
	// 0x442A10: an atmosphere from `start_ms` on, again and again: the one
	// playing fades out, the music too (closed), and it fades in from 0. Not
	// played with the sound's volume at 0 or in a building. What is left of it
	// from there (its length − `start_ms`), or 0.
	uint32_t play_atmo(const std::string& path, uint32_t start_ms, int loudness);
	// 0x442D10: every music and atmosphere stopped at once.
	void stop_music();
	// 0x442230: whether a piece of music plays (the ended streams closed first).
	bool music_playing();
	// 0x442C20: what is left of the music playing, in ms (0 without it; 0xFFFFFFFF without sound).
	uint32_t music_left_ms() const;
	// 0x442C60: the length of one of the game's files in ms (0 if it can't be opened or without sound).
	uint32_t file_length_ms(const std::string& path) const;

	// 0x4436E0: into a building (its [Gebaeude] title, 0..11): the atmospheres
	// low-passed at 400 Hz (Miles' «LowPass Filter», [+0x1C5]), its music
	// again and again at the sound's volume, the music playing let down to the
	// building's level, and no new music or atmosphere until out of it.
	void enter_building(int index);
	// Out of it: no filter, the music let down back up to its volume, the building's music stopped.
	void leave_building();

	// The music director's each frame (0x42C080 → 0x444030): the fades moved on.
	void fade();
	// Each frame, with the game's clock in ms (*0x6DCCF0): the streams that ended closed.
	void update(uint32_t now_ms);

private:
	struct Stream;
	enum Slot { kAtmo, kAtmoFading, kMusic, kMusicFading, kBuilding, kSlots };

	static void callback(void* self, Uint8* out, int bytes);
	void mix(int16_t* out, int frames);
	// A stream of the game's file from `start_ms`, or nullptr.
	std::unique_ptr<Stream> open_stream(const std::string& path, uint32_t start_ms = 0) const;
	// Under the device's lock: a stream's volume, its filter.
	void set_volume(Stream& s, int volume);
	void set_filter(Slot slot, bool on);
	// The volume of a loudness at the music's or the sound's volume (0..127).
	static int volume_of(int master, int loudness);
	// The stream of `slot` closed.
	void close_slot(Slot slot);
	// The stream of `from` moved to `to` (closing what `to` had).
	void move_slot(Slot from, Slot to);
	// 0x442180: the music, the atmosphere and a building's music that ended closed (and the forgotten music).
	void reap();
	// With P3VITA_SOUND_LOG in the environment (on the PC, to check the music director): what plays, on stderr.
	void log(const std::string& what, const std::string& path, int loudness, uint32_t ms) const;

	SDL_AudioDeviceID device_ = 0;
	std::vector<std::string> roots_;
	int music_volume_ = 0;  // [+0x1B8]
	int sound_volume_ = 0;  // [+0x1BC]
	std::vector<std::string> building_titles_;  // [+0xCC]
	mutable std::map<std::string, uint32_t> lengths_;  // file_length_ms's, by the game's path.
	std::unique_ptr<Stream> streams_[kSlots];
	// Pieces of music replaced without a fade before their end: on to their end.
	std::vector<std::unique_ptr<Stream>> forgotten_;
	// Their loudness ([+0x1D8], [+0x1D9], [+0x1DA], [+0x1DB]) and the times their fades last moved ([+0x1C8],
	// [+0x1CC], [+0x1D0], [+0x1D4]).
	int loudness_[kSlots] = {};
	uint32_t fade_time_[kSlots] = {};
	bool atmo_fade_ = false;  // [+0x1C6]
	bool music_fade_ = false;  // [+0x1C7]
	bool music_out_closes_ = true;  // [+0x1DC]: the music fading out ends closed, else at `music_low_` ([+0x1C4]).
	int music_low_ = 2;
	bool filtered_ = false;  // [+0x1C5]: in a building, the atmospheres low-passed.
	bool in_building_ = false;  // [+0x1E0]: a building's music plays: no new music or atmosphere.
	uint32_t now_ = 0;
	bool log_ = false;
	std::vector<int16_t> scratch_;  // The audio thread's decoded samples.
	std::vector<int32_t> sum_;
};

}  // namespace p3::app
