#pragma once

// The game's music director (0x42C080, each frame of the game's main screen
// and of its options screen, on *0x6CBB40's [+0x287C..+0x28C8]): which piece
// of music plays and when, and the atmospheres between them; with its events
// (0x42C770: a town shown, the sea map shown, a battle on the screen) and a
// town's own piece (0x42CD90). docs/formats/sound.md.
//
// A piece of music fades in over the atmosphere; 5 s before its end the
// atmosphere comes back (at a random point of it) and the music fades out;
// the next piece, 40..55 s after its end (15..20 s after a town's own piece or
// a battle's). The game chooses with the world's rand() (0x639C03): the
// viewer with a generator of its own (the same formulas), so that the music
// doesn't change the game's random numbers (roadmap, section 8).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "p3data/sound.hpp"

namespace p3::app {

class GameSound;

// What the director looks at, each frame.
struct MusicScene {
	bool music_on = false;  // The music's volume isn't 0 ([*0x6CC3E8 + 0x21]).
	bool sea_list = false;  // The sea map's music: the sea map shown or the options screen.
	bool town_view = false;  // The town view shown (a battle's too: 0x4B2200 on *0x6E51AC).
	bool town = false;  // The town view has a town (its [+0xC324] < the towns' count; not in a battle).
	int region = 0;  // That town's region for the music (0x6DDBC0 + 0x34 · its index, as the game reads it).
	bool town_past_count = false;  // Its [+0xC324] > the towns' count (unsigned: 0xFF in a battle), for 0x42C770's 1.
	bool battle = false;  // A battle on the screen ([0x6E59CC] ≠ 0xFF).
	bool rain = false;  // A storm by the town (0x59C050: the town's [+0x9C4] & 2, with the weather's effects on).
	bool winter = false;  // The town's ice shown (the view's [+0x504] 2..5).
	bool tinted = false;  // The view's tints ([+0xC178] | [+0xC1A8]) & 0xFF000000: no atmosphere (none in the viewer).
	int month = 1;  // 1..12 (0x5311D0): November and December, the sea's winter.
};

// A town's own piece ([StadtN] NameDerStadtMusik, LautstaerkeDerMusik of StadtDaten.ini), by its number there.
using TownThemes = std::vector<p3::data::MusicTitle>;

class MusicDirector {
public:
	MusicDirector(const p3::data::SoundIni& ini, TownThemes themes);

	// The game's main screen made (0x423385): a piece of music at once.
	void reset();
	// The next piece's time ([+0x2884]) and the atmosphere's ([+0x28C4]): the options' «OK» (0x460143, 0x460195).
	void set_times(uint32_t next_music, uint32_t atmo_at) {
		next_music_ = next_music;
		atmo_at_ = atmo_at;
	}
	// 0x42C080: each frame of the main screen or the options screen, with the game's clock in ms.
	void frame(GameSound& sound, const MusicScene& scene, uint32_t now);
	// 0x42C770: 0 a town shown (its atmosphere), 1 the sea map shown (the sea's), 2 a battle on the screen (a
	// battle's music); `force` (0 and 1): the atmosphere even with a piece of music playing. (`stop_effects`: the
	// sound effects stopped, part 3.)
	void event(GameSound& sound, const MusicScene& scene, int kind, bool force, uint32_t now);
	// 0x42CD90: a town's own piece (its index in the game, `number` its StadtDaten.ini number), on going into it
	// from the sea map (0x44A1B7).
	void town_theme(GameSound& sound, int number, uint32_t now);

private:
	// rand() / 32768.0 (0x66CB34) as a float, from the director's own generator (MSVC's rand()).
	float chance();
	// An atmosphere's start: a random point of its length leaving at least what is left before the next piece.
	uint32_t atmo_start(uint32_t length, uint32_t now);

	p3::data::SoundIni ini_;
	TownThemes themes_;
	uint32_t seed_ = 1;
	uint32_t next_music_ = 0;  // [+0x2884]
	uint8_t last_ = 0;  // [+0x2888]: the last piece's index in [Region] or [SeaMap].
	uint32_t atmo_length_ = 0;  // [+0x28B4]
	uint32_t atmo_from_ = 0;  // [+0x28B8]
	uint32_t atmo_at_ = 0;  // [+0x28C4]
};

}  // namespace p3::app
