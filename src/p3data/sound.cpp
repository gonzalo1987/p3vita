#include "p3data/sound.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "dr_mp3.h"

namespace p3::data {

namespace {

uint16_t u16(const uint8_t* p) {
	return uint16_t(p[0] | (p[1] << 8));
}

uint32_t u32(const uint8_t* p) {
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// IMA ADPCM's steps and how each code moves the step index.
constexpr int16_t kImaSteps[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50,
	55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
	544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
	20350, 22385, 24623, 27086, 29794, 32767};
constexpr int8_t kImaIndexMoves[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

struct ImaChannel {
	int predictor = 0;
	int index = 0;

	int16_t decode(int code) {
		const int step = kImaSteps[index];
		int diff = step >> 3;
		if (code & 1) {
			diff += step >> 2;
		}
		if (code & 2) {
			diff += step >> 1;
		}
		if (code & 4) {
			diff += step;
		}
		predictor += (code & 8) ? -diff : diff;
		predictor = std::clamp(predictor, -32768, 32767);
		index = std::clamp(index + kImaIndexMoves[code & 15], 0, 88);
		return int16_t(predictor);
	}
};

// One block of IMA ADPCM into `out` (at most `frames` frames).
void decode_ima_block(const uint8_t* block, size_t size, int channels, size_t frames, std::vector<int16_t>& out) {
	if (size < size_t(4 * channels) || frames == 0) {
		return;
	}
	ImaChannel state[2];
	for (int c = 0; c < channels; ++c) {
		state[c].predictor = int16_t(u16(block + 4 * c));
		state[c].index = std::min<int>(block[4 * c + 2], 88);
		out.push_back(int16_t(state[c].predictor));
	}
	size_t done = 1;
	const uint8_t* p = block + 4 * channels;
	const uint8_t* end = block + size;
	// Each channel's 4 bytes (8 samples) in turn.
	while (done < frames && p + 4 * channels <= end) {
		int16_t decoded[2][8];
		for (int c = 0; c < channels; ++c) {
			for (int k = 0; k < 4; ++k) {
				decoded[c][2 * k] = state[c].decode(p[k] & 15);
				decoded[c][2 * k + 1] = state[c].decode(p[k] >> 4);
			}
			p += 4;
		}
		for (int k = 0; k < 8 && done < frames; ++k, ++done) {
			for (int c = 0; c < channels; ++c) {
				out.push_back(decoded[c][k]);
			}
		}
	}
}

bool same_text(std::string_view a, std::string_view b) {
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

std::string_view trim(std::string_view s) {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
		s.remove_prefix(1);
	}
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
		s.remove_suffix(1);
	}
	return s;
}

}  // namespace

bool decode_wav(const std::vector<uint8_t>& file, SoundPcm& out) {
	out = SoundPcm{};
	if (file.size() < 12 || std::memcmp(file.data(), "RIFF", 4) != 0 || std::memcmp(file.data() + 8, "WAVE", 4) != 0) {
		return false;
	}
	const uint8_t* fmt = nullptr;
	size_t fmt_size = 0;
	const uint8_t* data = nullptr;
	size_t data_size = 0;
	uint32_t fact = 0;
	bool has_fact = false;
	size_t at = 12;
	while (at + 8 <= file.size()) {
		const uint8_t* chunk = file.data() + at;
		const size_t size = std::min<size_t>(u32(chunk + 4), file.size() - at - 8);
		if (std::memcmp(chunk, "fmt ", 4) == 0) {
			fmt = chunk + 8;
			fmt_size = size;
		} else if (std::memcmp(chunk, "data", 4) == 0) {
			data = chunk + 8;
			data_size = size;
		} else if (std::memcmp(chunk, "fact", 4) == 0 && size >= 4) {
			fact = u32(chunk + 8);
			has_fact = true;
		}
		at += 8 + size + (size & 1);
	}
	if (!fmt || fmt_size < 16 || !data) {
		return false;
	}
	const int format = u16(fmt);
	const int channels = u16(fmt + 2);
	const int rate = int(u32(fmt + 4));
	const size_t block_align = u16(fmt + 12);
	const int bits = u16(fmt + 14);
	if (channels < 1 || channels > 2 || rate <= 0) {
		return false;
	}
	out.channels = channels;
	out.rate = rate;
	if (format == 1 && bits == 16) {
		out.samples.resize(data_size / 2);
		for (size_t i = 0; i < out.samples.size(); ++i) {
			out.samples[i] = int16_t(u16(data + 2 * i));
		}
	} else if (format == 0x11 && bits == 4 && block_align > size_t(4 * channels)) {
		// The frames of a whole block: its head's sample and 2 per byte of each channel.
		const size_t per_block = (block_align - 4 * size_t(channels)) * 2 / size_t(channels) + 1;
		size_t left = has_fact ? fact : (data_size / block_align + 1) * per_block;
		out.samples.reserve(std::min(left, (data_size / block_align + 1) * per_block) * size_t(channels));
		for (size_t b = 0; b < data_size && left > 0; b += block_align) {
			const size_t before = out.frames();
			decode_ima_block(data + b, std::min(block_align, data_size - b), channels, std::min(left, per_block),
				out.samples);
			left -= out.frames() - before;
		}
	} else {
		out = SoundPcm{};
		return false;
	}
	return true;
}

std::string ini_text(std::string_view ini, std::string_view section, std::string_view key, std::string_view fallback) {
	bool inside = false;
	size_t at = 0;
	while (at < ini.size()) {
		size_t end = ini.find('\n', at);
		if (end == std::string_view::npos) {
			end = ini.size();
		}
		std::string_view line = trim(ini.substr(at, end - at));
		at = end + 1;
		if (!line.empty() && line.front() == '[') {
			const size_t close = line.find(']');
			inside = close != std::string_view::npos && same_text(line.substr(1, close - 1), section);
			continue;
		}
		if (!inside || line.empty() || line.front() == ';') {
			continue;
		}
		const size_t equals = line.find('=');
		if (equals == std::string_view::npos || !same_text(trim(line.substr(0, equals)), key)) {
			continue;
		}
		std::string_view value = line.substr(equals + 1);
		const size_t comment = value.find(';');
		if (comment != std::string_view::npos) {
			value = value.substr(0, comment);
		}
		return std::string(trim(value));
	}
	return std::string(fallback);
}

int ini_int(std::string_view ini, std::string_view section, std::string_view key, int fallback) {
	const std::string value = ini_text(ini, section, key);
	return value.empty() ? fallback : std::atoi(value.c_str());
}

SoundIni parse_sound_ini(std::string_view text) {
	SoundIni ini;
	auto title = [&](std::string_view section, int k) {
		const std::string n = std::to_string(k);
		return MusicTitle{ini_text(text, section, "Titel" + n), ini_int(text, section, "Loudness" + n, 100)};
	};
	const int sea = std::max(0, ini_int(text, "SeaMap", "Titel", 0));
	for (int k = 0; k < sea; ++k) {
		ini.sea_map.push_back(title("SeaMap", k));
	}
	for (int k = 0; k < 2; ++k) {
		ini.sea_map_atmo[size_t(k)] = title("SeaMapAtmo", k);
	}
	for (int k = 0; k < 3; ++k) {
		ini.town_map_atmo[size_t(k)] = title("TownMapAtmo", k);
	}
	ini.sea_battle_atmo = title("SeeschlachtAtmo", 0);
	for (int k = 0; k < 7; ++k) {
		ini.region_titles[size_t(k)] = uint8_t(ini_int(text, "Region", "Region" + std::to_string(k), 0));
	}
	const int regions = std::max(0, ini_int(text, "Region", "Titel", 0));
	for (int k = 0; k < regions; ++k) {
		ini.regions.push_back(title("Region", k));
	}
	const int buildings = std::max(0, ini_int(text, "Gebaeude", "Titel", 0));
	for (int k = 0; k < buildings; ++k) {
		ini.buildings.push_back(ini_text(text, "Gebaeude", "Titel" + std::to_string(k)));
	}
	ini.building_music_volume = ini_int(text, "Gebaeude", "m_BMusicVolumeInBuildings", 0);
	for (int k = 0;; ++k) {
		std::string video = ini_text(text, "Videos", "Video" + std::to_string(k));
		if (video.empty()) {
			break;
		}
		ini.videos.push_back(std::move(video));
	}
	ini.video_volume = ini_int(text, "Videos", "Volume", 0);
	ini.main_menu = ini_text(text, "Hauptmenu", "Titel0");
	ini.score = ini_text(text, "Hauptmenu", "Titel1");

	// Every [SoundListN] (the game loads each when a screen asks for it, 0x4439B0).
	size_t at = 0;
	while ((at = text.find("[SoundList", at)) != std::string_view::npos) {
		at += 10;
		const int id = std::atoi(std::string(text.substr(at, 12)).c_str());
		const std::string section = "SoundList" + std::to_string(id);
		const int count = ini_int(text, section, "Count", -1);
		const std::string ids = ini_text(text, section, "IDs");
		if (count < 0 || ini_text(text, section, "Files").empty() || ids.empty()) {
			continue;
		}
		std::vector<SoundSample>& list = ini.lists[id];
		const char* p = ids.c_str();
		for (int k = 0; k < count; ++k) {
			char* next = nullptr;
			const long sample = std::strtol(p, &next, 10);
			if (next == p) {
				break;
			}
			p = next;
			const std::string s = "SAMPLE" + std::to_string(sample);
			list.push_back({ini_text(text, s, "SampleFile"), ini_int(text, s, "Loudness", 100)});
		}
	}
	return ini;
}

struct Mp3Stream::Impl {
	drmp3 mp3{};
	bool ready = false;

	~Impl() {
		if (ready) {
			drmp3_uninit(&mp3);
		}
	}
};

std::unique_ptr<Mp3Stream> Mp3Stream::open(const std::string& path) {
	// The length from the file's size and its first frame's bit rate (MPEG-1 layer III).
	uint32_t length_ms = 0;
	if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
		uint8_t head[4096];
		const size_t got = std::fread(head, 1, sizeof head, f);
		std::fseek(f, 0, SEEK_END);
		const long size = std::ftell(f);
		std::fclose(f);
		size_t at = 0;
		if (got >= 10 && std::memcmp(head, "ID3", 3) == 0) {
			at = 10 + ((size_t(head[6] & 0x7F) << 21) | (size_t(head[7] & 0x7F) << 14) |
						  (size_t(head[8] & 0x7F) << 7) | size_t(head[9] & 0x7F));
		}
		static constexpr int kKbps[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
		for (size_t i = at; i + 4 <= got; ++i) {
			if (head[i] == 0xFF && (head[i + 1] & 0xFE) == 0xFA) {
				const int kbps = kKbps[head[i + 2] >> 4];
				if (kbps > 0 && size > long(i)) {
					length_ms = uint32_t(uint64_t(size - long(i)) * 8 / uint64_t(kbps));
				}
				break;
			}
		}
	}
	std::unique_ptr<Mp3Stream> stream(new Mp3Stream);
	stream->impl_ = std::make_unique<Impl>();
	if (!drmp3_init_file(&stream->impl_->mp3, path.c_str(), nullptr)) {
		return nullptr;
	}
	stream->impl_->ready = true;
	stream->length_ms_ = length_ms;
	return stream;
}

Mp3Stream::~Mp3Stream() = default;

int Mp3Stream::channels() const {
	return int(impl_->mp3.channels);
}

int Mp3Stream::rate() const {
	return int(impl_->mp3.sampleRate);
}

size_t Mp3Stream::read(int16_t* out, size_t frames) {
	return size_t(drmp3_read_pcm_frames_s16(&impl_->mp3, frames, out));
}

bool Mp3Stream::rewind() {
	return drmp3_seek_to_pcm_frame(&impl_->mp3, 0) != 0;
}

bool Mp3Stream::seek_ms(uint32_t ms) {
	return drmp3_seek_to_pcm_frame(&impl_->mp3, uint64_t(ms) * impl_->mp3.sampleRate / 1000) != 0;
}

}  // namespace p3::data
