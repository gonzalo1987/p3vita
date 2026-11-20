// The game's sound files and scripts/sound.ini (p3data/sound.hpp).
//
// The IMA ADPCM vectors were decoded independently with Python's audioop
// (adpcm2lin, its nibbles in the other order): see re/emu/sesion51/ima_vectors.py.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "p3data/sound.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		++failures;
	}
}

void put16(std::vector<uint8_t>& v, int x) {
	v.push_back(uint8_t(x));
	v.push_back(uint8_t(x >> 8));
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
	for (int k = 0; k < 4; ++k) {
		v.push_back(uint8_t(x >> (8 * k)));
	}
}

void put_tag(std::vector<uint8_t>& v, const char* tag) {
	v.insert(v.end(), tag, tag + 4);
}

// A RIFF WAVE file: its «fmt » (format, channels, 44100 Hz, block align, bits), «fact» (if `fact` >= 0) and «data».
std::vector<uint8_t> wav(int format, int channels, int block_align, int bits, long fact, const uint8_t* data,
	size_t size) {
	std::vector<uint8_t> body;
	put_tag(body, "WAVE");
	put_tag(body, "fmt ");
	put32(body, format == 0x11 ? 20 : 16);
	put16(body, format);
	put16(body, channels);
	put32(body, 44100);
	put32(body, 44100 * uint32_t(block_align));
	put16(body, block_align);
	put16(body, bits);
	if (format == 0x11) {
		put16(body, 2);
		put16(body, (block_align - 4 * channels) * 2 / channels + 1);
	}
	if (fact >= 0) {
		put_tag(body, "fact");
		put32(body, 4);
		put32(body, uint32_t(fact));
	}
	put_tag(body, "data");
	put32(body, uint32_t(size));
	body.insert(body.end(), data, data + size);
	std::vector<uint8_t> file;
	put_tag(file, "RIFF");
	put32(file, uint32_t(body.size()));
	file.insert(file.end(), body.begin(), body.end());
	return file;
}

const uint8_t kMonoBlocks[] = {
	5, 82, 56, 0, 59, 3, 46, 17, 42, 50, 181, 121, 8, 15, 8, 177, 247, 237, 76, 46, 93, 58, 7, 249, 127, 33, 238,
	35, 45, 23, 138, 32, 154, 246, 181, 136, 152, 202, 83, 0, 102, 232, 9, 36, 2, 170, 73, 242, 193, 85, 27, 39,
	254, 83, 38, 110, 73, 13, 177, 56, 72, 156, 232, 20, 213, 141, 20, 90, 139, 79, 153, 79};
const int16_t kMonoExpected[] = {
	20997, 19639, 20872, 21993, 22138, 20416, 21589, 22228, 22810, 21929, 22730, 23458, 24385, 25708, 24475, 23995,
	26180, 25868, 26152, 22279, 22832, 22329, 22786, 24032, 21386, 26539, 15489, -1883, -31945, -32768, 4094,
	-32768, -12290, -32768, 12285, -8193, 17876, 32767, 32767, 21595, -29190, -32768, 28668, 32767, 32767, -11247,
	-32768, -4099, 14522, -22720, -2242, 32767, 32767, 14146, 10761, 13838, 27828, 15110, 8173, 32767, -23096,
	21957, -6712, -10436, -13821, -13672, 19396, 32767, 28672, -19743, -32029, -28305, 2166, 22644, 32767, 32767,
	17379, 3389, -4241, 16571, 30561, -7594, 4692, -28826, 16227, 32767, 4098, 15270, 32767, 32767, -15648, -32768,
	-4099, 32767, 32767, 32767, -15648, 32767, 20481, 32767};
const uint8_t kStereoBlock[] = {
	71, 82, 59, 0, 232, 158, 50, 0, 21, 197, 178, 253, 109, 24, 146, 19, 174, 239, 243, 23, 112, 44, 162, 156, 241,
	87, 225, 224, 235, 33, 131, 37, 151, 140, 63, 95, 218, 103, 51, 203, 213, 223, 61, 52, 99, 235, 120, 184, 248,
	192, 130, 98, 105, 215, 89, 104, 176, 55, 80, 137, 154, 30, 180, 78, 79, 165, 228, 36, 255, 241, 170, 71};
const int16_t kStereoExpected[] = {
	21063, -24856, 23903, -26060, 25037, -23977, 28816, -24261, 24287, -23487, 27330, -22314, 23456, -22953, 17921,
	-21595, 6871, -21067, -13660, -20907, -27650, -18722, -32768, -21533, -32768, -19643, -4099, -17926, -32768,
	-19487, 28668, -22043, 32767, -23073, 32767, -25258, -18018, -28950, 32767, -27441, 32767, -25154, 32767,
	-22245, -15648, -22623, -11553, -18844, -32768, -16328, 28668, -18615, 16382, -23188, -17136, -14057, -21231,
	2911, -32768, 19098, -4099, 32767, -32768, 19390, 12285, 3754, 32767, 18469, -12286, 32767, -32768, 9068,
	-32768, -30943, -32768, -32768, -4099, 23095, 29419, 19000, 32767, -7069, 29043, -17225, -21742, 22786, -17647,
	32767, -32768, -12286, -12290, -24572, -16014, 16394, 914, 12299, 32767, 32767, 32767, 12289, 6698, 1117,
	32767, -32768, 32767, -20482, 32767, 13036, 32767, -15633, 20481, -32768, 16757, 4094, -32768, -32768, 4094,
	-32768, 32767, -20482, 12289, -32768, 32767, -32768, -20478, -32768, 16384, 18017, 32767, 32767};

void test_adpcm() {
	p3::data::SoundPcm pcm;
	// Mono, two blocks of 36 bytes, the second cut by «fact» at 100 samples.
	check(p3::data::decode_wav(wav(0x11, 1, 36, 4, 100, kMonoBlocks, sizeof kMonoBlocks), pcm), "mono ADPCM read");
	check(pcm.channels == 1 && pcm.rate == 44100, "mono ADPCM format");
	check(pcm.samples.size() == 100 && std::memcmp(pcm.samples.data(), kMonoExpected, sizeof kMonoExpected) == 0,
		"mono ADPCM samples");
	// Without «fact»: both whole blocks.
	check(p3::data::decode_wav(wav(0x11, 1, 36, 4, -1, kMonoBlocks, sizeof kMonoBlocks), pcm) &&
			pcm.samples.size() == 130,
		"mono ADPCM without fact");
	// A last block cut short: the samples of its bytes.
	check(p3::data::decode_wav(wav(0x11, 1, 36, 4, -1, kMonoBlocks, 36 + 4 + 8), pcm) &&
			pcm.samples.size() == 65 + 1 + 16 && std::memcmp(pcm.samples.data(), kMonoExpected, 82 * 2) == 0,
		"mono ADPCM short last block");
	// Stereo, one block of 72 bytes: 65 frames.
	check(p3::data::decode_wav(wav(0x11, 2, 72, 4, 65, kStereoBlock, sizeof kStereoBlock), pcm), "stereo ADPCM read");
	check(pcm.channels == 2 && pcm.frames() == 65 &&
			std::memcmp(pcm.samples.data(), kStereoExpected, sizeof kStereoExpected) == 0,
		"stereo ADPCM samples");
}

void test_pcm() {
	const uint8_t data[] = {1, 0, 0xFF, 0xFF, 0x00, 0x80, 0xFF, 0x7F};
	p3::data::SoundPcm pcm;
	check(p3::data::decode_wav(wav(1, 2, 4, 16, -1, data, sizeof data), pcm), "PCM read");
	check(pcm.channels == 2 && pcm.frames() == 2 && pcm.samples[0] == 1 && pcm.samples[1] == -1 &&
			pcm.samples[2] == -32768 && pcm.samples[3] == 32767,
		"PCM samples");
	check(!p3::data::decode_wav(wav(3, 1, 4, 32, -1, data, sizeof data), pcm), "float WAV refused");
	check(!p3::data::decode_wav(std::vector<uint8_t>{'R', 'I', 'F', 'F'}, pcm), "short file refused");
}

void test_ini() {
	const std::string text =
		"[SeaMap]\r\nTitel=2\r\nTitel0=musik\\SeekarteMusic\\seekarte01.mp3\r\nLoudness0=100\r\n"
		"Titel1=musik\\SeekarteMusic\\kurz01.mp3\r\nLoudness1=90\r\n\r\n"
		"[TownMapAtmo]\r\nTitel=2\r\nTitel0=a.mp3\r\nTitel1=b.mp3\r\nLoudness1=70\r\nTitel2=c.mp3\r\n"
		"[Region]\r\nTitel=3\r\nRegion0=2\r\nRegion1=1\r\nTitel0=r0.mp3\r\nLoudness0=60\r\nTitel1=r1.mp3\r\n\r\n"
		"Titel2=r2.mp3\r\nLoudness2=55\r\n"
		"[Gebaeude]\r\nTitel=2\r\nTitel0=kneipe.mp3\r\nTitel1=kontor.mp3\r\nm_BMusicVolumeInBuildings=3\r\n"
		"[Videos]\r\nVideo0=v0.mp3\r\nVideo1=v1.mp3\r\nVolume=60\r\n"
		"[Hauptmenu]\r\nTitel=2\r\nTitel0=menu.mp3\r\nTitel1=score.mp3\r\n"
		"[SoundList3000]\r\nCount=2\r\nFiles=scripts\\sound.ini\r\nIDs=3000 3001\r\n"
		";Flautensounds\r\n[SoundList3100]\r\nCount=0\r\n"
		"[SAMPLE3000]\r\nSampleFile=musik\\SeekarteWaves\\Sturm1.wav\r\nLoudness=50\r\n"
		"[SAMPLE3001]\r\nsamplefile = Sturm2.wav ; a comment\r\n";
	const p3::data::SoundIni ini = p3::data::parse_sound_ini(text);
	check(ini.sea_map.size() == 2 && ini.sea_map[1].file == "musik\\SeekarteMusic\\kurz01.mp3" &&
			ini.sea_map[1].loudness == 90 && ini.sea_map[0].loudness == 100,
		"sea map titles");
	check(ini.town_map_atmo[1].file == "b.mp3" && ini.town_map_atmo[1].loudness == 70 &&
			ini.town_map_atmo[2].file == "c.mp3" && ini.town_map_atmo[2].loudness == 100,
		"town atmospheres (three, whatever «Titel» says)");
	check(ini.sea_map_atmo[0].file.empty() && ini.sea_battle_atmo.file.empty(), "missing sections empty");
	check(ini.region_titles[0] == 2 && ini.region_titles[1] == 1 && ini.region_titles[2] == 0, "regions' counts");
	check(ini.regions.size() == 3 && ini.regions[2].file == "r2.mp3" && ini.regions[2].loudness == 55,
		"regions' titles after a blank line");
	check(ini.buildings.size() == 2 && ini.buildings[1] == "kontor.mp3" && ini.building_music_volume == 3,
		"buildings' music");
	check(ini.videos.size() == 2 && ini.video_volume == 60, "videos' music");
	check(ini.main_menu == "menu.mp3" && ini.score == "score.mp3", "main menu's music");
	check(ini.lists.size() == 1 && ini.lists.count(3000) == 1, "sound lists (3100 without IDs left out)");
	const std::vector<p3::data::SoundSample>& storm = ini.lists.at(3000);
	check(storm.size() == 2 && storm[0].file == "musik\\SeekarteWaves\\Sturm1.wav" && storm[0].loudness == 50 &&
			storm[1].file == "Sturm2.wav" && storm[1].loudness == 100 && storm[0].id == 3000 && storm[1].id == 3001,
		"a list's samples (case, comment, default loudness)");
	check(p3::data::ini_text(text, "seamap", "TITEL1") == "musik\\SeekarteMusic\\kurz01.mp3", "ini case");
	check(p3::data::ini_int(text, "Nothing", "x", -7) == -7, "ini fallback");
}

}  // namespace

int main() {
	test_adpcm();
	test_pcm();
	test_ini();
	if (failures == 0) {
		std::printf("sound: OK\n");
	}
	return failures == 0 ? 0 : 1;
}
