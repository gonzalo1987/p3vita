#pragma once

// The game's sounds and music (docs/formats/sound.md): its sound effects, RIFF
// WAVE files in the archive (musik\...), almost all of them Microsoft IMA ADPCM;
// its music, MP3 files beside the game (Musik\...); and scripts/sound.ini, which
// names them: the music of each place and the lists of sound effects, each
// list a few variants with their loudness.

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace p3::data {

// 16-bit samples, the channels interleaved.
struct SoundPcm {
	int channels = 0;
	int rate = 0;
	std::vector<int16_t> samples;

	size_t frames() const { return channels > 0 ? samples.size() / size_t(channels) : 0; }
};

// A RIFF WAVE file of 16-bit PCM or IMA ADPCM (format 0x11: blocks of a 4-byte
// head per channel, its first sample and step index, then 4-bit codes, the low
// nibble first; with 2 channels 4 bytes of each in turn), 1 or 2 channels;
// false for anything else. With a «fact» chunk the samples end at its length.
bool decode_wav(const std::vector<uint8_t>& file, SoundPcm& out);

// A piece of music or an atmosphere: «TitelN=» and «LoudnessN=» (0..100).
struct MusicTitle {
	std::string file;  // As the ini names it: Latin-1, '\' separators.
	int loudness = 100;
};

// A sound effect, «[SAMPLEn]»: its file in the archive and its loudness (0..100).
struct SoundSample {
	std::string file;
	int loudness = 100;
};

// What scripts/sound.ini says, read as the game reads it (0x427400, 0x42EE1E,
// 0x441D02, 0x4439B0, 0x467321).
struct SoundIni {
	// [SeaMap] «Titel» titles: the sea map's music.
	std::vector<MusicTitle> sea_map;
	// [SeaMapAtmo] Titel0..1: the sea's atmosphere in summer and in winter.
	std::array<MusicTitle, 2> sea_map_atmo;
	// [TownMapAtmo] Titel0..2: the town's atmosphere in summer, in winter and in the rain.
	std::array<MusicTitle, 3> town_map_atmo;
	// [SeeschlachtAtmo] Titel0: a sea battle's atmosphere.
	MusicTitle sea_battle_atmo;
	// [Region] Region0..6: how many of «Titel» belong to each region, in turn; the titles.
	std::array<uint8_t, 7> region_titles{};
	std::vector<MusicTitle> regions;
	// [Gebaeude] «Titel» titles: the buildings' music; m_BMusicVolumeInBuildings.
	std::vector<std::string> buildings;
	int building_music_volume = 0;
	// [Videos] Video0..: the event videos' music; «Volume».
	std::vector<std::string> videos;
	int video_volume = 0;
	// [Hauptmenu] Titel0, Titel1: the main menu's music and the score's.
	std::string main_menu;
	std::string score;
	// [SoundListN] (Count, IDs) with their [SAMPLEn], by N; a list without its
	// «Files» and «IDs» (as SoundList3100) left out, as the game does.
	std::map<int, std::vector<SoundSample>> lists;
};

SoundIni parse_sound_ini(std::string_view text);

// The value of «key=» in «[section]» of an ini's text as the game's reader
// takes it (trimmed, without a «;» comment), or `fallback` if it isn't there.
std::string ini_text(std::string_view ini, std::string_view section, std::string_view key,
	std::string_view fallback = {});
int ini_int(std::string_view ini, std::string_view section, std::string_view key, int fallback);

// An MP3 file read in pieces as it plays (dr_mp3): 16-bit samples.
class Mp3Stream {
public:
	// nullptr if the file can't be opened or isn't an MP3.
	static std::unique_ptr<Mp3Stream> open(const std::string& path);
	~Mp3Stream();
	Mp3Stream(const Mp3Stream&) = delete;
	Mp3Stream& operator=(const Mp3Stream&) = delete;

	int channels() const;
	int rate() const;
	// Its length in ms from the size of the file and the first frame's bit
	// rate (the game's music is of a constant rate), without reading it all.
	uint32_t length_ms() const { return length_ms_; }
	// Up to `frames` frames into `out`; fewer at its end (0 past it).
	size_t read(int16_t* out, size_t frames);
	// Back to its start (to play it again).
	bool rewind();
	// To a point in ms (an atmosphere starts at a random point).
	bool seek_ms(uint32_t ms);

private:
	Mp3Stream() = default;
	struct Impl;
	std::unique_ptr<Impl> impl_;
	uint32_t length_ms_ = 0;
};

}  // namespace p3::data
