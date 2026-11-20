#include "audio.hpp"

#include <dirent.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

#include "p3data/cpr.hpp"
#include "p3data/sound.hpp"

namespace p3::app {

namespace {

constexpr int kRate = 44100;
constexpr int kFadeMs = 5000;  // A fade from 0 to a stream's volume, or back (0x444030).
constexpr size_t kMaxFrames = 16384;  // The most the audio thread mixes at once.

bool same_name(const std::string& a, const std::string& b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i) {
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
			return false;
		}
	}
	return true;
}

bool exists(const std::string& path) {
	if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
		std::fclose(f);
		return true;
	}
	return false;
}

}  // namespace

std::string find_game_file(const std::vector<std::string>& roots, const std::string& path) {
	static std::map<std::string, std::string> found;  // Each path asked, once.
	const std::string key = p3::data::normalize_path(path);
	if (const auto it = found.find(key); it != found.end()) {
		return it->second;
	}
	std::vector<std::string> parts;
	std::string part;
	for (const char c : p3::data::latin1_to_utf8(path)) {
		if (c == '\\' || c == '/') {
			if (!part.empty()) {
				parts.push_back(part);
			}
			part.clear();
		} else {
			part += c;
		}
	}
	if (!part.empty()) {
		parts.push_back(part);
	}
	std::string result;
	for (const std::string& root : roots) {
		std::string direct = root;
		for (size_t i = 0; i < parts.size(); ++i) {
			direct += (i ? "/" : "") + parts[i];
		}
		if (exists(direct)) {
			result = direct;
			break;
		}
		// Each folder's entries for a name of other capitals.
		std::string at = root;
		bool ok = true;
		for (size_t i = 0; i < parts.size() && ok; ++i) {
			ok = false;
			if (DIR* d = opendir(at.empty() ? "." : at.c_str())) {
				while (dirent* e = readdir(d)) {
					if (same_name(e->d_name, parts[i])) {
						at += (i ? "/" : "") + std::string(e->d_name);
						ok = true;
						break;
					}
				}
				closedir(d);
			}
		}
		if (ok && exists(at)) {
			result = at;
			break;
		}
	}
	found[key] = result;
	return result;
}

struct GameSound::Stream {
	std::unique_ptr<p3::data::Mp3Stream> mp3;
	int channels = 2;
	bool loop = false;
	bool done = false;  // Ended (the audio thread's; Miles' status 2).
	int volume = 0;  // 0..127
	uint64_t played = 0;  // Frames since its start (or its last turn).
};

GameSound::GameSound() = default;

GameSound::~GameSound() {
	close();
}

void GameSound::close() {
	if (device_ != 0) {
		SDL_CloseAudioDevice(device_);  // (The audio thread stopped: the streams closed after it.)
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		device_ = 0;
	}
	for (auto& s : streams_) {
		s.reset();
	}
	forgotten_.clear();
}

bool GameSound::open(std::vector<std::string> roots) {
	roots_ = std::move(roots);
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		std::fprintf(stderr, "no sound: %s\n", SDL_GetError());
		return false;
	}
	SDL_AudioSpec want{};
	want.freq = kRate;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
#ifdef __vita__
	want.samples = 2048;  // A longer buffer: the MP3s are decoded as it plays.
#else
	want.samples = 1024;
#endif
	want.callback = &GameSound::callback;
	want.userdata = this;
	SDL_AudioSpec got{};
	scratch_.resize(kMaxFrames * 2);
	sum_.resize(kMaxFrames * 2);
	device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);  // SDL converts to the device's own format.
	if (device_ == 0) {
		std::fprintf(stderr, "no sound: %s\n", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}
	SDL_PauseAudioDevice(device_, 0);
	return true;
}

void GameSound::callback(void* self, Uint8* out, int bytes) {
	GameSound& s = *static_cast<GameSound*>(self);
	int16_t* samples = reinterpret_cast<int16_t*>(out);
	int frames = bytes / 4;
	while (frames > 0) {
		const int n = std::min(frames, int(kMaxFrames));
		s.mix(samples, n);
		samples += 2 * n;
		frames -= n;
	}
}

void GameSound::mix(int16_t* out, int frames) {
	std::fill(sum_.begin(), sum_.begin() + 2 * frames, 0);
	auto add = [&](Stream& s) {
		if (s.done) {
			return;
		}
		// Decoded even at volume 0: it goes on playing, unheard.
		int got = 0;
		int turns = 0;
		while (got < frames) {
			const size_t n = s.mp3->read(scratch_.data() + size_t(got) * size_t(s.channels), size_t(frames - got));
			if (n == 0) {
				if (s.loop && turns++ == 0 && s.mp3->rewind()) {
					s.played = 0;
					continue;
				}
				s.done = true;
				break;
			}
			turns = 0;
			got += int(n);
			s.played += n;
		}
		if (s.volume <= 0) {
			return;
		}
		for (int i = 0; i < got; ++i) {
			const int l = scratch_[size_t(i) * size_t(s.channels)];
			const int r = s.channels > 1 ? scratch_[size_t(i) * 2 + 1] : l;
			sum_[size_t(2 * i)] += l * s.volume / 127;
			sum_[size_t(2 * i + 1)] += r * s.volume / 127;
		}
	};
	for (auto& s : streams_) {
		if (s) {
			add(*s);
		}
	}
	for (auto& s : forgotten_) {
		add(*s);
	}
	for (int i = 0; i < 2 * frames; ++i) {
		out[i] = int16_t(std::clamp(sum_[size_t(i)], -32768, 32767));
	}
}

std::unique_ptr<GameSound::Stream> GameSound::open_stream(const std::string& path) const {
	const std::string file = find_game_file(roots_, path);
	auto mp3 = file.empty() ? nullptr : p3::data::Mp3Stream::open(file);
	if (!mp3 || mp3->rate() != kRate || mp3->channels() < 1 || mp3->channels() > 2) {
		return nullptr;
	}
	auto s = std::make_unique<Stream>();
	s->channels = mp3->channels();
	s->mp3 = std::move(mp3);
	return s;
}

int GameSound::volume_of(int master, int loudness) {
	return std::clamp(master * loudness / 100, 0, 127);
}

void GameSound::set_volume(Stream& s, int volume) {
	SDL_LockAudioDevice(device_);
	s.volume = volume;
	SDL_UnlockAudioDevice(device_);
}

void GameSound::close_slot(Slot slot) {
	std::unique_ptr<Stream> gone;
	SDL_LockAudioDevice(device_);
	gone = std::move(streams_[slot]);
	SDL_UnlockAudioDevice(device_);
}

void GameSound::set_volumes(int music, int sound) {
	music = std::clamp(music, 0, 127);
	sound = std::clamp(sound, 0, 127);
	if (music == music_volume_ && sound == sound_volume_) {
		return;
	}
	music_volume_ = music;
	sound_volume_ = sound;
	if (!on()) {
		return;
	}
	// (The game's options set the streams' volumes again; a fade goes on from there.)
	SDL_LockAudioDevice(device_);
	for (int k = 0; k < kSlots; ++k) {
		if (streams_[k] && !(k == kMusic && music_fade_) && !(k == kAtmo && atmo_fade_) && k != kMusicFading &&
			k != kAtmoFading) {
			streams_[k]->volume = volume_of(k == kMusic ? music_volume_ : sound_volume_, loudness_[k]);
		}
	}
	SDL_UnlockAudioDevice(device_);
}

uint32_t GameSound::play_music(const std::string& path, int loudness, bool crossfade, bool loop, bool force) {
	if (!on() || (!force && music_volume_ * loudness / 100 == 0)) {
		return 0;
	}
	reap();
	if (crossfade) {
		// (The sound effects stopped: part 3.) The music playing fades out from where it is.
		if (streams_[kMusic]) {
			std::unique_ptr<Stream> gone;
			SDL_LockAudioDevice(device_);
			gone = std::move(streams_[kMusicFading]);
			streams_[kMusicFading] = std::move(streams_[kMusic]);
			SDL_UnlockAudioDevice(device_);
			loudness_[kMusicFading] = loudness_[kMusic];
			music_fade_ = true;
			fade_time_[kMusicFading] = now_;
			music_out_closes_ = true;
		}
		if (auto s = open_stream(path)) {
			loudness_[kMusic] = loudness;
			s->volume = volume_of(music_volume_, loudness);
			s->loop = loop;
			const uint32_t length = s->mp3->length_ms();
			std::unique_ptr<Stream> atmo, atmo_fading;
			SDL_LockAudioDevice(device_);
			streams_[kMusic] = std::move(s);
			atmo = std::move(streams_[kAtmo]);
			atmo_fading = std::move(streams_[kAtmoFading]);
			SDL_UnlockAudioDevice(device_);
			return length;
		}
		// Not opened: on as without the crossfade (where it isn't opened either).
	}
	if (streams_[kAtmo]) {
		std::unique_ptr<Stream> gone;
		SDL_LockAudioDevice(device_);
		gone = std::move(streams_[kAtmoFading]);
		streams_[kAtmoFading] = std::move(streams_[kAtmo]);
		SDL_UnlockAudioDevice(device_);
		loudness_[kAtmoFading] = loudness_[kAtmo];
		atmo_fade_ = true;
		fade_time_[kAtmoFading] = now_;
	}
	auto s = open_stream(path);
	if (!s) {
		return 0;
	}
	s->volume = 0;
	s->loop = loop;
	loudness_[kMusic] = loudness;
	music_fade_ = true;
	fade_time_[kMusic] = now_;
	const uint32_t length = s->mp3->length_ms();
	SDL_LockAudioDevice(device_);
	if (streams_[kMusic]) {
		forgotten_.push_back(std::move(streams_[kMusic]));
	}
	streams_[kMusic] = std::move(s);
	SDL_UnlockAudioDevice(device_);
	return length;
}

void GameSound::stop_music() {
	if (!on()) {
		return;
	}
	for (Slot slot : {kMusic, kAtmo, kMusicFading, kAtmoFading}) {
		close_slot(slot);
	}
	music_fade_ = false;
}

uint32_t GameSound::music_left_ms() const {
	if (!on()) {
		return 0xFFFFFFFF;
	}
	const Stream* s = streams_[kMusic].get();
	if (!s) {
		return 0;
	}
	SDL_LockAudioDevice(device_);
	const uint64_t at = s->played * 1000 / kRate;
	SDL_UnlockAudioDevice(device_);
	return at >= s->mp3->length_ms() ? 0 : uint32_t(s->mp3->length_ms() - at);
}

void GameSound::reap() {
	// 0x442180: the music and the atmosphere that ended, closed; the forgotten ones too.
	std::vector<std::unique_ptr<Stream>> gone;
	SDL_LockAudioDevice(device_);
	for (Slot slot : {kMusic, kAtmo}) {
		if (streams_[slot] && streams_[slot]->done) {
			gone.push_back(std::move(streams_[slot]));
		}
	}
	for (auto& s : forgotten_) {
		if (s->done) {
			gone.push_back(std::move(s));
		}
	}
	forgotten_.erase(std::remove(forgotten_.begin(), forgotten_.end(), nullptr), forgotten_.end());
	SDL_UnlockAudioDevice(device_);
}

void GameSound::update(uint32_t now_ms) {
	now_ = now_ms;
	if (!on()) {
		return;
	}
	reap();
	// 0x444030: each fade moves its volume by one for every 5000 / its volume ms gone since it last moved; the one
	// coming in up to its volume, the one going out down to 0 (closed) or, for the music, to music_low_.
	auto steps = [&](Slot slot, int master) {
		const int full = std::min(master * loudness_[slot] / 100, 127);
		const uint32_t per = uint32_t(full != 0 ? kFadeMs / full : kFadeMs);
		return std::pair<int, uint32_t>(full, (now_ - fade_time_[slot]) / per);
	};
	auto fade = [&](bool& active, Slot in, Slot out, int master, bool out_closes) {
		active = false;
		if (Stream* s = streams_[in].get()) {
			const auto [full, n] = steps(in, master);
			active = true;
			if (n != 0) {
				int v = s->volume + int(n);
				if (v > full) {
					v = full;
					active = false;
				}
				set_volume(*s, v);
				fade_time_[in] = now_;
			}
		}
		if (Stream* s = streams_[out].get()) {
			const auto [full, n] = steps(out, master);
			(void)full;
			active = true;
			if (n == 0) {
				return;
			}
			const int v = s->volume - int(n);
			if (!out_closes && v <= music_low_) {
				// The music let down to its low level, and left there.
				active = false;
				if (s->volume > music_low_) {
					set_volume(*s, music_low_);
				}
			} else if (v <= 0) {
				close_slot(out);
				active = false;
			} else {
				set_volume(*s, v);
				fade_time_[out] = now_;
			}
		}
	};
	if (atmo_fade_) {
		fade(atmo_fade_, kAtmo, kAtmoFading, sound_volume_, true);
	}
	if (music_fade_) {
		fade(music_fade_, kMusic, kMusicFading, music_volume_, music_out_closes_);
	}
}

}  // namespace p3::app
