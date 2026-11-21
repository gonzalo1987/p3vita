#include "audio.hpp"

#include <dirent.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <map>

#include "p3data/cpr.hpp"
#include "p3data/sound.hpp"

namespace p3::app {

namespace {

constexpr int kRate = 44100;
constexpr int kFadeMs = 5000;  // A fade from 0 to a stream's volume, or back (0x444030).
constexpr size_t kMaxFrames = 16384;  // The most the audio thread mixes at once.
constexpr double kLowPassHz = 400.0;  // The building's filter's cutoff (the float 0x66D640).

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

// A second-order low-pass (Butterworth) at kLowPassHz: Miles' «LowPass Filter» stands for it.
struct LowPass {
	float b0, b1, b2, a1, a2;

	LowPass() {
		const double w = 2.0 * 3.14159265358979323846 * kLowPassHz / kRate;
		const double alpha = std::sin(w) / (2.0 * std::sqrt(0.5));
		const double c = std::cos(w);
		const double a0 = 1.0 + alpha;
		b0 = float((1.0 - c) / 2.0 / a0);
		b1 = float((1.0 - c) / a0);
		b2 = b0;
		a1 = float(-2.0 * c / a0);
		a2 = float((1.0 - alpha) / a0);
	}
};

const LowPass& low_pass() {
	static const LowPass filter;
	return filter;
}

// The interface's files (0x6BF118, by the buttons' HSoundID / PSoundID): the ones past 6 aren't in the game's data.
const char* const kInterfaceFiles[] = {"musik\\MenueSounds\\okbottun_menu.WAV", "musik\\MenueSounds\\alleButtons.WAV",
	"musik\\SeekarteWaves\\hawk.wav", "musik\\SeekarteWaves\\hawk.wav", "musik\\SeekarteWaves\\hawk.wav",
	"musik\\SeekarteWaves\\hawk.wav", "musik\\SeekarteWaves\\hawk.wav", "sound/sound02.wav", "sound/sound03.wav",
	"sound/sound04.wav", "sound/sound05.wav", "sound/sound06.wav", "sound/sound07.wav", "sound/sound08.wav",
	"sound/sound09.wav", "sound/sound10.wav", "sound/sound11.wav", "sound/sound12.wav", "sound/sound13.wav",
	"sound/sound14.wav"};
constexpr size_t kPcmCacheBytes = 24u << 20;  // The decoded WAVs kept (all of them would be 54 MB).
constexpr int kLetterSample = 6000;  // Nachrichten.wav: one at a time.
constexpr int kPrayerSample = 7100;  // Beten.wav


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
		if (parts.empty()) {
			break;
		}
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
	bool low_pass = false;
	float x1[2] = {}, x2[2] = {}, y1[2] = {}, y2[2] = {};  // The filter's past samples, by channel.
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
	log_ = SDL_getenv("P3VITA_SOUND_LOG") != nullptr;
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
		const LowPass& f = low_pass();
		for (int i = 0; i < got; ++i) {
			for (int c = 0; c < 2; ++c) {
				float v = scratch_[size_t(i) * size_t(s.channels) + size_t(s.channels > 1 ? c : 0)];
				if (s.low_pass) {
					const float y = f.b0 * v + f.b1 * s.x1[c] + f.b2 * s.x2[c] - f.a1 * s.y1[c] - f.a2 * s.y2[c];
					s.x2[c] = s.x1[c];
					s.x1[c] = v;
					s.y2[c] = s.y1[c];
					s.y1[c] = y;
					v = y;
				}
				sum_[size_t(2 * i + c)] += int32_t(v) * s.volume / 127;
			}
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
	for (Voice& v : voices_) {
		add_voice(v, frames);
	}
	for (Voice& v : interface_) {
		add_voice(v, frames);
	}
	for (int i = 0; i < 2 * frames; ++i) {
		out[i] = int16_t(std::clamp(sum_[size_t(i)], -32768, 32767));
	}
}

std::unique_ptr<GameSound::Stream> GameSound::open_stream(const std::string& path, uint32_t start_ms) const {
	const std::string file = find_game_file(roots_, path);
	auto mp3 = file.empty() ? nullptr : p3::data::Mp3Stream::open(file, start_ms);
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

void GameSound::set_filter(Slot slot, bool on) {
	SDL_LockAudioDevice(device_);
	if (Stream* s = streams_[slot].get(); s && s->low_pass != on) {
		s->low_pass = on;
		for (int c = 0; c < 2; ++c) {
			s->x1[c] = s->x2[c] = s->y1[c] = s->y2[c] = 0;
		}
	}
	SDL_UnlockAudioDevice(device_);
}

void GameSound::close_slot(Slot slot) {
	std::unique_ptr<Stream> gone;
	SDL_LockAudioDevice(device_);
	gone = std::move(streams_[slot]);
	SDL_UnlockAudioDevice(device_);
}

void GameSound::move_slot(Slot from, Slot to) {
	std::unique_ptr<Stream> gone;
	SDL_LockAudioDevice(device_);
	gone = std::move(streams_[to]);
	streams_[to] = std::move(streams_[from]);
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
	if (streams_[kMusic] && !music_fade_) {
		streams_[kMusic]->volume = volume_of(music_volume_, loudness_[kMusic]);
	}
	if (streams_[kAtmo] && !atmo_fade_) {
		streams_[kAtmo]->volume = volume_of(sound_volume_, loudness_[kAtmo]);
	}
	if (streams_[kBuilding]) {
		streams_[kBuilding]->volume = sound_volume_;
	}
	SDL_UnlockAudioDevice(device_);
}

void GameSound::set_building_music(std::vector<std::string> titles, int low) {
	building_titles_ = std::move(titles);
	music_low_ = low != 0 ? low : 2;
}

uint32_t GameSound::play_music(const std::string& path, int loudness, bool crossfade, bool loop, bool force) {
	if (!on() || (!force && music_volume_ * loudness / 100 == 0) || in_building_) {
		return 0;
	}
	reap();
	if (crossfade) {
		// The 20 channels end (their reservations stay); the music playing fades out from where it is.
		SDL_LockAudioDevice(device_);
		for (Voice& v : voices_) {
			v.playing = false;
		}
		SDL_UnlockAudioDevice(device_);
		if (streams_[kMusic]) {
			move_slot(kMusic, kMusicFading);
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
			log("music (crossfade)", path, loudness, length);
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
		move_slot(kAtmo, kAtmoFading);
		loudness_[kAtmoFading] = loudness_[kAtmo];
		atmo_fade_ = true;
		fade_time_[kAtmoFading] = now_;
	}
	auto s = open_stream(path);
	if (!s) {
		log("music not found", path, loudness, 0);
		return 0;
	}
	s->volume = 0;
	s->loop = loop;
	loudness_[kMusic] = loudness;
	music_fade_ = true;
	fade_time_[kMusic] = now_;
	const uint32_t length = s->mp3->length_ms();
	log("music", path, loudness, length);
	SDL_LockAudioDevice(device_);
	if (streams_[kMusic]) {
		forgotten_.push_back(std::move(streams_[kMusic]));
	}
	streams_[kMusic] = std::move(s);
	SDL_UnlockAudioDevice(device_);
	return length;
}

uint32_t GameSound::play_atmo(const std::string& path, uint32_t start_ms, int loudness) {
	if (!on() || in_building_) {
		return 0;
	}
	reap();
	if (sound_volume_ == 0) {
		return 0;
	}
	if (streams_[kAtmo]) {
		move_slot(kAtmo, kAtmoFading);
		atmo_fade_ = true;
		fade_time_[kAtmoFading] = now_;
		loudness_[kAtmoFading] = loudness_[kAtmo];
	}
	auto s = open_stream(path, start_ms);
	if (!s) {
		return 0;  // (The game's gives what its stack holds; an empty name: the atmosphere fades out alone.)
	}
	const uint32_t left = s->mp3->length_ms() - start_ms;
	log("atmosphere from " + std::to_string(start_ms), path, loudness, left);
	if (streams_[kMusic]) {
		move_slot(kMusic, kMusicFading);
		music_fade_ = true;
		fade_time_[kMusicFading] = now_;
		loudness_[kMusicFading] = loudness_[kMusic];
		music_out_closes_ = true;
	}
	s->volume = 0;
	s->loop = true;
	s->low_pass = filtered_;
	loudness_[kAtmo] = loudness;
	atmo_fade_ = true;
	fade_time_[kAtmo] = now_;
	SDL_LockAudioDevice(device_);
	streams_[kAtmo] = std::move(s);
	SDL_UnlockAudioDevice(device_);
	return left;
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

bool GameSound::music_playing() {
	if (!on()) {
		return false;
	}
	reap();
	return streams_[kMusic] != nullptr;
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

uint32_t GameSound::file_length_ms(const std::string& path) const {
	if (!on()) {
		return 0;
	}
	if (const auto it = lengths_.find(path); it != lengths_.end()) {
		return it->second;
	}
	const std::string file = find_game_file(roots_, path);
	const auto mp3 = file.empty() ? nullptr : p3::data::Mp3Stream::open(file);
	return lengths_[path] = mp3 ? mp3->length_ms() : 0;
}

void GameSound::enter_building(int index) {
	filtered_ = true;
	if (!on()) {
		return;
	}
	set_filter(kAtmo, true);
	set_filter(kAtmoFading, true);
	stop_samples();
	// 0x442280: its music, unless one plays already.
	reap();
	if (streams_[kBuilding] || sound_volume_ == 0 || index < 0 || index >= int(building_titles_.size())) {
		return;
	}
	auto s = open_stream(building_titles_[size_t(index)]);
	if (!s) {
		return;
	}
	s->volume = sound_volume_;
	s->loop = true;
	log("building", building_titles_[size_t(index)], 100, s->mp3->length_ms());
	if (streams_[kMusic]) {
		move_slot(kMusic, kMusicFading);
		music_fade_ = true;
		fade_time_[kMusicFading] = now_;
		loudness_[kMusicFading] = loudness_[kMusic];
		music_out_closes_ = false;  // Down to music_low_, and left there.
	}
	in_building_ = true;
	SDL_LockAudioDevice(device_);
	streams_[kBuilding] = std::move(s);
	SDL_UnlockAudioDevice(device_);
}

void GameSound::leave_building() {
	filtered_ = false;
	if (!on()) {
		return;
	}
	set_filter(kAtmo, false);
	set_filter(kAtmoFading, false);
	// The music let down comes back as the one playing, and fades in from where it is.
	if (!streams_[kMusic] && streams_[kMusicFading]) {
		move_slot(kMusicFading, kMusic);
		loudness_[kMusic] = loudness_[kMusicFading];
	}
	if (streams_[kMusic]) {
		music_fade_ = true;
		fade_time_[kMusic] = now_;
	}
	close_slot(kBuilding);
	in_building_ = false;
	log("out of the building", "", 0, 0);
}

void GameSound::log(const std::string& what, const std::string& path, int loudness, uint32_t ms) const {
	if (log_) {
		std::fprintf(stderr, "[sound %u] %s %s (loudness %d): %u ms\n", unsigned(now_), what.c_str(), path.c_str(),
			loudness, unsigned(ms));
	}
}

void GameSound::reap() {
	// 0x442180: the music, the atmosphere and a building's music that ended, closed; the forgotten ones too.
	std::vector<std::unique_ptr<Stream>> gone;
	SDL_LockAudioDevice(device_);
	for (Slot slot : {kMusic, kAtmo, kBuilding}) {
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
	if (on()) {
		reap();
	}
}

void GameSound::fade() {
	if (!on()) {
		return;
	}
	// 0x444030: each fade moves its volume by one for every 5000 / its volume ms gone since it last moved; the one
	// coming in up to its volume, the one going out down to 0 (closed) or, for the music, to music_low_. The flag is
	// cleared when the one going out is done, even if the one coming in hasn't reached its volume.
	auto steps = [&](Slot slot, int master) {
		const int full = std::min(master * loudness_[slot] / 100, 127);
		const uint32_t per = uint32_t(full != 0 ? kFadeMs / full : kFadeMs);
		return std::pair<int, uint32_t>(full, (now_ - fade_time_[slot]) / per);
	};
	auto fade_pair = [&](bool& active, Slot in, Slot out, int master, bool out_closes) {
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
			const uint32_t n = steps(out, master).second;
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
		fade_pair(atmo_fade_, kAtmo, kAtmoFading, sound_volume_, true);
	}
	if (music_fade_) {
		fade_pair(music_fade_, kMusic, kMusicFading, music_volume_, music_out_closes_);
	}
}

void GameSound::set_effects(const p3::data::SoundIni& ini, FileReader read) {
	read_file_ = std::move(read);
	ini_lists_.clear();
	for (const auto& [id, list] : ini.lists) {
		std::vector<std::pair<int, Sample>>& samples = ini_lists_[id];
		for (const p3::data::SoundSample& s : list) {
			samples.push_back({s.id, Sample{s.file, s.loudness}});
		}
	}
}

void GameSound::load_list(int id) {
	const auto it = ini_lists_.find(id);
	if (!on() || it == ini_lists_.end() || loaded_lists_.count(id) != 0) {
		return;
	}
	loaded_lists_[id] = int(it->second.size());
	for (const auto& [sample, s] : it->second) {
		samples_[sample] = s;
	}
}

int GameSound::list_variants(int id) const {
	const auto it = loaded_lists_.find(id);
	return on() && it != loaded_lists_.end() ? it->second : 0;
}

std::shared_ptr<const p3::data::SoundPcm> GameSound::pcm_of(const std::string& file) {
	const std::string key = p3::data::normalize_path(file);
	for (auto it = pcm_cache_.begin(); it != pcm_cache_.end(); ++it) {
		if (it->first == key) {
			pcm_cache_.splice(pcm_cache_.begin(), pcm_cache_, it);
			return it->second;
		}
	}
	std::vector<uint8_t> bytes;
	auto pcm = std::make_shared<p3::data::SoundPcm>();
	if (!read_file_ || !read_file_(file, bytes) || !p3::data::decode_wav(bytes, *pcm)) {
		return nullptr;
	}
	pcm_cache_.emplace_front(key, pcm);
	pcm_cache_bytes_ += pcm->samples.size() * 2;
	while (pcm_cache_bytes_ > kPcmCacheBytes && pcm_cache_.size() > 1) {
		pcm_cache_bytes_ -= pcm_cache_.back().second->samples.size() * 2;
		pcm_cache_.pop_back();  // (A channel playing it keeps it until it ends.)
	}
	return pcm;
}

void GameSound::add_voice(Voice& v, int frames) {
	if (!v.playing || !v.pcm) {
		return;
	}
	const p3::data::SoundPcm& p = *v.pcm;
	const size_t n = p.frames();
	const size_t ch = size_t(p.channels);
	// Miles' pan: 64 the middle, both sides whole; towards a side the other one fades.
	const float left = v.pan <= 64 ? 1.0f : float(127 - v.pan) / 63.0f;
	const float right = v.pan >= 64 ? 1.0f : float(v.pan) / 64.0f;
	const float gain = float(v.volume) / 127.0f;
	const LowPass& f = low_pass();
	for (int i = 0; i < frames && v.at < n; ++i, ++v.at) {
		float s[2] = {float(p.samples[v.at * ch]), float(p.samples[v.at * ch + (ch > 1 ? 1 : 0)])};
		for (int c = 0; c < 2; ++c) {
			if (v.low_pass) {
				const float y = f.b0 * s[c] + f.b1 * v.x1[c] + f.b2 * v.x2[c] - f.a1 * v.y1[c] - f.a2 * v.y2[c];
				v.x2[c] = v.x1[c];
				v.x1[c] = s[c];
				v.y2[c] = v.y1[c];
				v.y1[c] = y;
				s[c] = y;
			}
		}
		sum_[size_t(2 * i)] += int32_t(s[0] * gain * left);
		sum_[size_t(2 * i + 1)] += int32_t(s[1] * gain * right);
	}
	if (v.at >= n) {
		v.playing = false;
	}
}

int GameSound::play_sample(int sample, bool world, bool reserve, int volume, int pan, bool no_start, bool force) {
	// ([+0x1DF]: an event video, part 6.)
	if (!on() || (!force && in_building_)) {
		return -1;
	}
	if (volume == -1) {
		volume = 127;
	}
	SDL_LockAudioDevice(device_);
	bool letter = false;
	int playing = 0;
	for (int k = 0; k < kChannels; ++k) {
		letter |= voice_sample_[k] == kLetterSample && voices_[k].playing;
		playing += voices_[k].playing ? 1 : 0;
	}
	SDL_UnlockAudioDevice(device_);
	if ((sample == kLetterSample && letter) || playing == kChannels) {
		return -1;
	}
	const auto it = samples_.find(sample);
	if (it == samples_.end()) {
		return -1;  // Its list not loaded.
	}
	const std::shared_ptr<const p3::data::SoundPcm> pcm = pcm_of(it->second.file);
	if (!pcm || volume == 0) {
		return -1;
	}
	Voice v;
	v.pcm = pcm;
	v.pan = swap_stereo_ ? 127 - pan : pan;
	if (!no_start) {
		v.volume = std::clamp(it->second.loudness * (sound_volume_ * volume / 127) / 100, 0, 127);
		v.playing = true;
	}
	v.low_pass = world && filtered_;
	int channel = -1;
	SDL_LockAudioDevice(device_);
	for (int k = 0; k < kChannels && channel < 0; ++k) {
		if (!reserved_[k] && !voices_[k].playing) {
			channel = k;
			voices_[k] = v;
		}
	}
	SDL_UnlockAudioDevice(device_);
	if (channel < 0) {
		return -1;
	}
	voice_sample_[channel] = sample;
	world_[channel] = world;
	reserved_[channel] = reserve;
	log("sample " + std::to_string(sample) + " on " + std::to_string(channel), it->second.file, v.volume,
		uint32_t(pcm->frames() * 1000 / kRate));
	return channel;
}

void GameSound::release_sample(int channel) {
	if (channel >= 0 && channel < kChannels) {
		reserved_[channel] = false;
		world_[channel] = false;
	}
}

void GameSound::stop_sample(int channel) {
	if (on() && channel >= 0 && channel < kChannels) {
		SDL_LockAudioDevice(device_);
		voices_[channel].playing = false;
		SDL_UnlockAudioDevice(device_);
	}
}

int GameSound::samples_playing() const {
	if (!on()) {
		return 0;
	}
	int n = 0;
	SDL_LockAudioDevice(device_);
	for (const Voice& v : voices_) {
		n += v.playing ? 1 : 0;
	}
	SDL_UnlockAudioDevice(device_);
	return n;
}

bool GameSound::prayer_playing() const {
	if (!on()) {
		return false;
	}
	for (int k = 0; k < kChannels; ++k) {
		if (voice_sample_[k] == kPrayerSample) {
			SDL_LockAudioDevice(device_);
			const bool playing = voices_[k].playing;
			SDL_UnlockAudioDevice(device_);
			return playing;
		}
	}
	return false;
}

void GameSound::stop_samples() {
	if (!on()) {
		return;
	}
	SDL_LockAudioDevice(device_);
	for (Voice& v : voices_) {
		v.playing = false;
	}
	SDL_UnlockAudioDevice(device_);
	for (int k = 0; k < kChannels; ++k) {
		reserved_[k] = false;
		world_[k] = false;
	}
	if (on_stop_samples_) {
		on_stop_samples_();
	}
}

bool GameSound::sample_done(int channel) const {
	if (!on() || channel < 0 || channel >= kChannels) {
		return true;
	}
	SDL_LockAudioDevice(device_);
	const bool done = !voices_[channel].playing;
	SDL_UnlockAudioDevice(device_);
	return done;
}

uint32_t GameSound::sample_ms(int channel) const {
	if (!on() || channel < 0 || channel >= kChannels || !voices_[channel].pcm) {
		return 0;
	}
	return uint32_t(voices_[channel].pcm->frames() * 1000 / kRate);
}

void GameSound::set_sample_volume(int channel, int volume) {
	if (!on() || channel < 0 || channel >= kChannels) {
		return;
	}
	const auto it = samples_.find(voice_sample_[channel]);
	const int loudness = it != samples_.end() ? it->second.loudness : 100;
	const int v = std::clamp(loudness * (sound_volume_ * volume / 127) / 100, 0, 127);
	SDL_LockAudioDevice(device_);
	voices_[channel].volume = v;
	SDL_UnlockAudioDevice(device_);
}

void GameSound::set_sample_pan(int channel, int pan) {
	if (!on() || channel < 0 || channel >= kChannels) {
		return;
	}
	SDL_LockAudioDevice(device_);
	voices_[channel].pan = swap_stereo_ ? 127 - pan : pan;
	SDL_UnlockAudioDevice(device_);
}

void GameSound::play_interface(int index) {
	if (!on() || index < 0 || index >= int(std::size(kInterfaceFiles))) {
		return;
	}
	const std::shared_ptr<const p3::data::SoundPcm> pcm = pcm_of(kInterfaceFiles[index]);
	if (!pcm) {
		return;
	}
	Voice v;
	v.pcm = pcm;
	v.volume = sound_volume_ * 100 / 127 * 127 / 100;  // (0x442FC8 → 0x4C4540: a percentage of 127.)
	v.playing = true;
	SDL_LockAudioDevice(device_);
	for (Voice& c : interface_) {
		if (!c.playing) {
			c = v;
			break;
		}
	}
	SDL_UnlockAudioDevice(device_);
	log("interface " + std::to_string(index), kInterfaceFiles[index], v.volume, uint32_t(pcm->frames() * 1000 / kRate));
}

}  // namespace p3::app
