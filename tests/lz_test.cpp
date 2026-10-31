// The LZ compressor (lz_compress.hpp) against the game's: the expected bytes
// are what 0x54C410 of Patrician3.exe writes for the same synthetic inputs
// (computed in the emulator, as tools/oracle/write_oracle.py runs it), and
// every output must decompress to its input.

#include <cstdio>
#include <string>
#include <vector>

#include "p3data/lz.hpp"
#include "p3data/lz_compress.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
	if (!ok) {
		std::printf("FAIL: %s\n", what.c_str());
		++failures;
	}
}

std::vector<uint8_t> from_hex(const char* hex) {
	std::vector<uint8_t> out;
	for (const char* p = hex; p[0] && p[1]; p += 2) {
		out.push_back(uint8_t(std::stoi(std::string(p, 2), nullptr, 16)));
	}
	return out;
}

// The inputs of the expected vectors: literals, runs and repeats; random
// bytes with blocks copied from 200 to 509 bytes back; zeros, text and all
// the bytes; 16 zeros (the smallest compressed size); 15 bytes (stored).
std::vector<std::vector<uint8_t>> inputs() {
	std::vector<std::vector<uint8_t>> out;
	std::vector<uint8_t> a;
	for (int i = 0; i < 10; ++i) {
		a.push_back(0x41);
		a.push_back(0x42);
	}
	a.resize(a.size() + 12, 0);
	for (char c : std::string("hello hello hello!")) {
		a.push_back(uint8_t(c));
	}
	out.push_back(a);
	std::vector<uint8_t> b;
	uint32_t x = 12345;
	size_t left = 0;
	size_t distance = 0;
	while (b.size() < 2000) {
		x = (x * 1103515245u + 12345u) & 0x7FFFFFFF;
		if (left > 0) {
			b.push_back(b[b.size() - distance]);
			--left;
		} else if (b.size() >= 600 && (x >> 16) % 3 == 0) {
			distance = 200 + (x >> 8) % 310;
			left = 3 + (x >> 4) % 40;
		} else {
			b.push_back(uint8_t(x >> 16));
		}
	}
	out.push_back(b);
	std::vector<uint8_t> c(600, 0);
	for (int i = 0; i < 30; ++i) {
		for (char ch : std::string("Patrician ")) {
			c.push_back(uint8_t(ch));
		}
	}
	for (int i = 0; i < 256; ++i) {
		c.push_back(uint8_t(i));
	}
	out.push_back(c);
	out.push_back(std::vector<uint8_t>(16, 0));
	std::vector<uint8_t> e;
	for (int i = 0; i < 15; ++i) {
		e.push_back(uint8_t(i));
	}
	out.push_back(e);
	return out;
}

const char* const kExpected[] = {
		"320000008208c56f00c286a60c1b366f40e62d04",
		"d0070000b81128a3ea43ab0e5ab49563b5c1c3970970f2b0e907e15f8657603a106045ab83801546f0ccd121efdb1757"
		"b7106489e62e078465482ad562962845847f066c8971f18d47be1afd965042a12c011f22b30478b1f46596ba36aca8b5"
		"08e426cd2b70e60e31e144e09c341b5d588049a68e5e1e7b8640ad9b64829c1809d51a2c9b75a7821a353ad0e872b48d"
		"0e02239ee6399ba5001fb46e6876e1fba28f843c69fc301cdac7e5902e5a386494dab4a107043ef7f058fb23ae0dbe08"
		"9152f8995469998462579c698947cd02866b197811d8e227539978a228c521a682959a543888d539d64a060e011f0aec"
		"60914903bf7515bc3def08707c7859f3706983a6059d7c5d1a334d12bf57df4431ba34a2889875dd0e9481f30f4b2235"
		"cfee2973f783433a720ad094f336a9110e4e7d7a5ddb9280da39713388fc50c288582848d670e1e216e64b3447287c65"
		"71a00aca06288ad4d848610eceb72a77747123d266c8ae54082e5803666f0f823070b2b5482641ce16127e8489d3164b"
		"16014f2000d1b9a1e90d2406dd5c7891532f01125b58a25d4a378c512c37d3ba4920224606030aa75efdd0c7c09bb509"
		"ce26185a7605d411725b7c452b42cb463544fea4010822ef50ac0783bed029a30b85110b52be21123521db2f21ef6e45"
		"a8e5edc09f6479748d438545492a742d503581d2c91ca635e252697a16828919158caa418a7003d39944bb5acd8bd268"
		"42ab7a1c4205a0c40a162136f514d553a12752bd79ab5e1892a12b5011103d869da180484f1d48c8c61c3b00ce833038"
		"0334455b64e107097bc3bcad78d7454b34737626e45126c4422e08aa0a641b04e398826785e09959c6ee1dab444cf4dd"
		"fa8041d7301adf6c8151220b908c663942b9f94689088570f34cb10b462fc71003c03689e9344ec7b430ef864d5903ed"
		"89a50fee9af49a57a0c525ff0ffdc9d3172021eddc006df4f4b2ff9b859de85bff4f2cb5fdf67f338fd7fbef28eacbb3"
		"5ef2f71abf6119799afbf2cfe777fc3f68a9b2d6cbe6b5169d37bfe01d2a7a96ff63a1afadfc2f5ef8fff0f41dff1362"
		"fa9ff27f8092f40d3f3ffa7f10c9c8a8ac07fd0014297627adf84f546163abbad993c4db7d9203f0793e69d26a880c5f"
		"f32c283973f6059fdef8fb63ae97fb4f807ef08ff30df785ff1bc9e2c47fd76f1a6ff15f516ee9ff80ed1fb7620d1ffa"
		"dfd85cf89f01912e658924f610ff83d6067d4402f29dfec1d4a1e57bff9bf33f94203df0ffd4ec4fee549f3c2ecd1a04"
		"1fe5d1c2ff90ffcd27fc7f80894813f8a89f9c7fda375afa10fba76412a26a73cca15b52dd425ee03f6247e127fe277f"
		"42d59dfde9f83509",
		"84040000000084ffffffff7f01250c1d3969c6a409e306a4f6ffff7f150002081840a080810308122858c0a081830710"
		"22489840a182850b183268d8c0a183870f2042881841a28489132852a858c1a2858b173062c89841a3868d1b3872e8d8"
		"c1a3878f1f4082081942a488912348922859c2a489932750a2489942a58a952b58b268d9c2a58b972f60c2881943a68c"
		"993368d2a859c3a68d9b3770e2c89943a78e9d3b78f2e8d9c3a78f9f3f8002091a44a890a1438812295ac4a891a34790"
		"22499a44a992a54b983269dac4a993a74fa042891a45aa94a953a852a95ac5aa95ab57b062c99a45ab96ad5bb872e9da"
		"c5ab97af5fc082091b46ac98b163c892295bc6ac99b367d0a2499b46ad9ab56bd8b269dbc6ad9bb76fe0c2891b47ae9c"
		"b973e8d2a95bc7ae9dbb77f0e2c99b47af9ebd7bf8f2e9dbc7af9fbf7f000000",
		"100000000000842f00000000",
		"f1ffffff000102030405060708090a0b0c0d0e"
};

void round_trip(const std::vector<uint8_t>& data, const std::string& what) {
	const std::vector<uint8_t> packed = lz_compress(data.data(), data.size());
	std::vector<uint8_t> back;
	std::string error;
	check(lz_decompress_file(packed.data(), packed.size(), back, &error) && back == data, what + ": " + error);
}

}  // namespace

int main() {
	const std::vector<std::vector<uint8_t>> in = inputs();
	for (size_t i = 0; i < in.size(); ++i) {
		const std::vector<uint8_t> packed = lz_compress(in[i].data(), in[i].size());
		check(packed == from_hex(kExpected[i]), "vector " + std::to_string(i) + " is not what the game writes");
		round_trip(in[i], "vector " + std::to_string(i));
	}
	round_trip({}, "empty");
	// Long runs (length fields up to 20 bits) and a long incompressible block.
	std::vector<uint8_t> zeros(1 << 20, 0);
	round_trip(zeros, "1 MB of zeros");
	std::vector<uint8_t> noise(100000);
	uint32_t x = 1;
	for (uint8_t& v : noise) {
		x = x * 214013u + 2531011u;
		v = uint8_t(x >> 16);
	}
	round_trip(noise, "noise");
	const std::vector<uint8_t> stored = lz_compress(noise.data(), noise.size());
	check(stored.size() == noise.size() + 4, "noise is not stored");
	if (failures == 0) {
		std::printf("lz_compress: ok\n");
	}
	return failures ? 1 : 0;
}
