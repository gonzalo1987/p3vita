#include "music_director.hpp"

#include <SDL.h>

#include "audio.hpp"
#include "p3data/game_fixes.hpp"

namespace p3::app {

namespace {

constexpr int kRegions = 5;  // Region0..4: the towns'; Region5 the sea battles' (the town view without a town).
constexpr int kMostStartTries = 1000;  // (The game's tries for ever: a hang with an atmosphere shorter than the wait.)

// The game's _ftol (0x639C28): toward 0.
int ftol(double x) {
	return int(x);
}

}  // namespace

MusicDirector::MusicDirector(const p3::data::SoundIni& ini, TownThemes themes)
	: ini_(ini), themes_(std::move(themes)), seed_(SDL_GetTicks() | 1) {}

void MusicDirector::reset() {
	next_music_ = 0;
	atmo_at_ = 0;
}

float MusicDirector::chance() {
	seed_ = seed_ * 214013u + 2531011u;
	return float((seed_ >> 16) & 0x7FFF) / 32768.0f;
}

uint32_t MusicDirector::atmo_start(uint32_t length, uint32_t now) {
	uint32_t from = length;
	for (int k = 0; k < kMostStartTries; ++k) {
		from = uint32_t(ftol(double(float(length)) * double(chance())));
		if (length - from >= next_music_ - now) {
			break;
		}
	}
	return from;
}

void MusicDirector::frame(GameSound& sound, const MusicScene& s, uint32_t now) {
	// ([+0x1DF]: an event video's music, part 6.) The fades.
	sound.fade();
	const std::array<uint8_t, 7>& counts = ini_.region_titles;
	if (s.music_on && next_music_ < now) {
		// A new piece: the sea map's, or one of the town's region (the sea battles' without a town).
		int index = 0;
		if (s.sea_list) {
			const float f = chance();
			index = ftol(double(float(ini_.sea_map.size())) * double(f));
		} else {
			const float f = chance();
			if (s.town) {
				if (s.region >= 0 && s.region < kRegions) {
					for (int r = 0; r < s.region; ++r) {
						index += counts[size_t(r)];
					}
					index += ftol(double(float(counts[size_t(s.region)])) * double(f));
				}
			} else {
				for (int r = 0; r < kRegions; ++r) {
					index += counts[size_t(r)];
				}
				index += ftol(double(float(counts[kRegions])) * double(f));
			}
		}
		if (index == last_) {
			// The same as the last one: again in 20 s, the atmosphere at once.
			next_music_ = now + 20000;
			atmo_at_ = now - 1;
		} else {
			const std::vector<p3::data::MusicTitle>& titles = s.sea_list ? ini_.sea_map : ini_.regions;
			const p3::data::MusicTitle title = size_t(index) < titles.size() ? titles[size_t(index)] : p3::data::MusicTitle{};
			const uint32_t length = sound.play_music(title.file, title.loudness, false, false, false);
			const float f = chance();
			atmo_at_ = now + length - 5000;
			last_ = uint8_t(index);
			next_music_ = now - uint32_t(ftol(double(f) * -15000.0)) + length + 40000;
		}
	}
	if (atmo_at_ >= now) {
		return;
	}
	// The atmosphere 5 s before the music ends (put off while more is left).
	const int left = int(sound.music_left_ms());
	if (left > 5000) {
		atmo_at_ += uint32_t(left - 5000);
		return;
	}
	const p3::data::MusicTitle* atmo = nullptr;
	bool winter = false;
	if (s.town_view) {
		winter = s.winter;
		if (s.battle) {
			atmo = &ini_.sea_battle_atmo;
		} else if (s.rain) {
			atmo = &ini_.town_map_atmo[2];
		} else if (winter) {
			atmo = &ini_.town_map_atmo[1];
		} else if (!s.tinted) {
			atmo = &ini_.town_map_atmo[0];
		}
	} else {
		winter = s.month > 10 || s.month < 1;
		atmo = &ini_.sea_map_atmo[winter ? 1 : 0];
	}
	atmo_length_ = atmo ? sound.file_length_ms(atmo->file) : 0;
	atmo_from_ = atmo_length_;
	if (atmo_length_ == 0) {
		return;
	}
	atmo_from_ = s.music_on ? uint32_t(ftol(double(float(atmo_length_)) * double(chance()))) : 0;
	// (A battle's atmosphere at the loudness 100, not its Loudness0.)
	const int loudness = s.town_view && s.battle ? 100 : atmo->loudness;
	const uint32_t rest = sound.play_atmo(atmo->file, atmo_from_, loudness);
	atmo_at_ = now + rest - 5000;
}

void MusicDirector::event(GameSound& sound, const MusicScene& s, int kind, bool stop_effects, bool force,
	uint32_t now) {
	if (stop_effects) {
		sound.stop_samples();
	}
	auto music_plays = [&]() {
		// A piece plays: the atmosphere at its end, the next piece 10..20 s after.
		const float f = chance();
		const uint32_t left = sound.music_left_ms();
		atmo_at_ = now + left - 5000;
		next_music_ = now - uint32_t(ftol(double(f) * -10000.0)) + left + 10000;
	};
	switch (kind) {
	case 2: {
		// A battle's piece at once, crossfaded.
		const std::array<uint8_t, 7>& counts = ini_.region_titles;
		const float f = chance();
		int index = 0;
		for (int r = 0; r < kRegions; ++r) {
			index += counts[size_t(r)];
		}
		index += ftol(double(float(counts[kRegions])) * double(f));
		const p3::data::MusicTitle title =
			size_t(index) < ini_.regions.size() ? ini_.regions[size_t(index)] : p3::data::MusicTitle{};
		const uint32_t length = sound.play_music(title.file, title.loudness, true, false, false);
		atmo_at_ = now + length - 5000;
		last_ = uint8_t(index);
		next_music_ = now - uint32_t(ftol(double(f) * -5000.0)) + length + 15000;
		return;
	}
	case 1: {
		// The sea's atmosphere (unless a piece plays, with a town).
		const bool winter = s.month > 10 || s.month < 1;
		const p3::data::MusicTitle& atmo = ini_.sea_map_atmo[winter ? 1 : 0];
		atmo_length_ = sound.file_length_ms(atmo.file);
		if (sound.music_playing() && !s.town_past_count && !force) {
			music_plays();
			return;
		}
		const float f = chance();
		next_music_ = now - uint32_t(ftol(double(f) * -5000.0)) + 15000;
		atmo_from_ = atmo_length_;
		if (atmo_length_ == 0) {
			return;
		}
		if (next_music_ != now) {
			atmo_from_ = atmo_start(atmo_length_, now);
		}
		atmo_at_ = now + sound.play_atmo(atmo.file, atmo_from_, atmo.loudness);
		return;
	}
	case 0: {
		// The town's atmosphere (unless a piece plays).
		if (sound.music_playing() && !force) {
			music_plays();
			return;
		}
		const float f = chance();
		next_music_ = now - uint32_t(ftol(double(f) * -5000.0)) + 15000;
		const std::array<p3::data::MusicTitle, 3>& town = ini_.town_map_atmo;
		atmo_length_ = s.rain ? sound.file_length_ms(town[2].file)
			: s.winter       ? sound.file_length_ms(town[1].file)
			: !s.tinted      ? sound.file_length_ms(town[0].file)
							 : 0;
		atmo_from_ = atmo_length_;
		if (atmo_length_ == 0) {
			sound.play_atmo("", 0, 100);  // None: the one playing fades out.
			return;
		}
		if (!s.music_on) {
			atmo_from_ = 0;
		} else if (next_music_ != now) {
			atmo_from_ = atmo_start(atmo_length_, now);
		}
		// The game's bug: here the ice gives the summer's atmosphere and its absence the winter's (0x42CCD1; the
		// length above is the right one's). Fixed with the fixes on.
		const bool winter = p3::data::fix_game_bugs() ? s.winter : !s.winter;
		const p3::data::MusicTitle& atmo = s.rain ? town[2] : town[winter ? 1 : 0];
		atmo_at_ = now + sound.play_atmo(atmo.file, atmo_from_, atmo.loudness) - 5000;
		return;
	}
	default:
		return;
	}
}

void MusicDirector::town_theme(GameSound& sound, int number, uint32_t now) {
	if (number < 0 || size_t(number) >= themes_.size()) {
		return;
	}
	const p3::data::MusicTitle& title = themes_[size_t(number)];
	std::string file = title.file;
	// The game's bug: Rügenwald's piece is «Ruegenwald.mp3» in StadtDaten.ini but the file is Rügenwald.mp3, so that
	// town has none. Fixed with the fixes on.
	if (const size_t at = file.find("Ruegenwald"); at != std::string::npos && p3::data::fix_game_bugs()) {
		file.replace(at, 10, "R\xFCgenwald");
	}
	const uint32_t length = sound.play_music(file, title.loudness, false, false, false);
	const float f = chance();
	atmo_at_ = now + length - 5000;
	next_music_ = now - uint32_t(ftol(double(f) * -5000.0)) + length + 15000;
}

}  // namespace p3::app
