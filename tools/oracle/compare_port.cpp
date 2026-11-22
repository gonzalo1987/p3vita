// compare_port TOWNS_DIR [STEPS]: the port scene's ships on the towns' tag 0 paths (port_scene.hpp), printed as
// re/emu/sesion50/portpath.py prints the game's (0x613B20 in Unicorn): for each town's .nodes, each path, docked,
// then coming in (state 1), going out (8) and going out held (0x18) at STEPS progresses from 0 to 0x800.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "p3data/battle_clouds.hpp"
#include "p3data/port_scene.hpp"
#include "p3data/town_life.hpp"

using namespace p3::data;

namespace {

// `compare_port --wind [STEPS]`: the port's wind (port_wind) as re/emu/sesion50/portwind.py prints the game's
// (0x618D10 in Unicorn): each town index 0..39 and sea side 0..7 from a zeroed state, along the same ticks.
int compare_wind(int steps) {
	for (int town = 0; town < 40; ++town) {
		for (int sea = 0; sea < 8; ++sea) {
			PortWind wind;
			uint32_t x = uint32_t(town * 8 + sea) * 2654435761u;
			uint32_t t = 0x1000u + uint32_t(town) * 997u + uint32_t(sea) * 131u;
			for (int i = 0; i < steps; ++i) {
				x = x * 1103515245u + 12345u;
				const uint32_t r = (x >> 16) & 0x7FFF;
				t += r % 7 ? r % 23 : r % 400;
				port_wind(wind, t, town, uint8_t(sea));
				std::printf("wind town %d sea %d tick %u dir %u strength %u\n", town, sea, t, unsigned(wind.dir),
					unsigned(wind.strength));
			}
		}
	}
	return 0;
}

// `compare_port --tint [COUNT]`: the weather tint's byte steps (tint_steps, tint_split) as re/emu/sesion50/porttint.py
// prints the game's (0x58F3C0, 0x58F440 in Unicorn), on the same pseudo-random colours.
int compare_tint(int count) {
	uint32_t x = 12345;
	auto next = [&x] {
		x = x * 1103515245u + 12345u;
		return x;
	};
	for (int i = 0; i < count; ++i) {
		uint32_t cur = (next() >> 16) | (next() & 0xFFFF0000u);
		uint32_t tgt = (next() >> 16) | (next() & 0xFFFF0000u);
		const uint32_t mode = next() >> 16;
		for (int b = 0; b < 4; ++b) {
			const uint32_t m = (mode >> (2 * b)) & 3;
			const int sh = 8 * b;
			if (m == 0) {
				tgt = (tgt & ~(0xFFu << sh)) | (cur & (0xFFu << sh));
			} else if (m == 1) {
				const uint32_t v = ((cur >> sh) & 0xFF) + (next() >> 16) % 7 - 3;
				tgt = (tgt & ~(0xFFu << sh)) | ((v & 0xFF) << sh);
			}
		}
		uint32_t r = next() >> 16;
		const uint32_t speed = r % 3 ? 2 : 1 + r % 255;
		uint32_t step = 0;
		const int n = tint_steps(cur, tgt, uint8_t(speed), step);
		std::printf("steps %08x %08x speed %u -> %d %08x\n", cur, tgt, speed, n, step);
		r = next() >> 16;
		const int k = r % 4 == 0 ? n : int(r % 300);
		tint_split(cur, tgt, k, step);
		std::printf("split %08x %08x count %d -> %08x\n", cur, tgt, k, step);
	}
	return 0;
}

// `compare_port --clouds [COUNT]`: a sea battle's clouds (battle_clouds.hpp) as re/emu/sesion50/portcloud.py prints
// the game's (0x6107B9, 0x5803B0, 0x580520 in Unicorn), with the same LCGs as rand() and as the cases.
int compare_clouds(int count) {
	struct Lcg {
		uint32_t x;
		int next() {
			x = x * 1103515245u + 12345u;
			return int((x >> 16) & 0x7FFF);
		}
	};
	Lcg rand{1};
	const CloudRand r = [&rand] { return rand.next(); };
	const std::vector<uint32_t> variants = {2, 2, 3, 1, 1, 1};
	Lcg lcg{7};
	for (int i = 0; i < count; ++i) {
		const bool at_town = i % 3 == 0;
		const int d = lcg.next() & 0xFF;
		const int s = lcg.next() % (i % 5 ? 30 : 256);
		rand.x = uint32_t(i) * 2654435761u;
		const int n = at_town ? 0x38 : 0xE0;
		std::printf("place town %d dir %d strength %d clouds %d\n", int(at_town), d, s, n);
		const int32_t packed = battle_cloud_wind(uint8_t(d), uint8_t(s));
		for (int k = 0; k < n; ++k) {
			int32_t x = 0;
			int32_t y = 0;
			battle_cloud_place(k, at_town, r, x, y);
			if (k < 20 || k % 37 == 0) {
				std::printf("  %d cls 20 x %d y %d packed %d\n", k, x, y, packed);
			}
		}
	}
	for (int i = 0; i < count; ++i) {
		const int details = i % 4;
		int32_t packed = 0;
		if (i % 3) {
			const uint32_t a = uint32_t(lcg.next()) << 17;
			const uint32_t b = uint32_t(lcg.next()) << 2;
			packed = int32_t(a ^ b ^ uint32_t(lcg.next()));
		} else {
			const int32_t a = lcg.next() % 600 - 300;
			const int32_t b = lcg.next() % 400 - 200;
			packed = int32_t(uint32_t(a) - (uint32_t(b) << 16));
		}
		const int32_t x = lcg.next() % 9000;
		const int32_t y = lcg.next() % 8000;
		rand.x = uint32_t(i) * 40503u + 1;
		BattleCloud c;
		battle_cloud_start(c, x, y, packed, variants, details, r);
		std::printf("start details %d packed %d x %d y %d -> look %d v %d %d t %d %d at %d %d\n", details, packed, x, y,
			int(c.look), c.vx, c.vy, c.tx, c.ty, c.x, c.y);
	}
	for (int i = 0; i < count; ++i) {
		BattleCloud c;
		const uint32_t xa = uint32_t(lcg.next() % 9000) << 16;
		c.x = int32_t(xa | uint32_t(lcg.next()));
		const uint32_t ya = uint32_t(lcg.next() % 8000) << 16;
		c.y = int32_t(ya | uint32_t(lcg.next()));
		c.tx = lcg.next() % 1200 - 600;
		c.ty = lcg.next() % 800 - 400;
		c.vx = c.tx + (i % 4 ? lcg.next() % 80 - 40 : lcg.next() % 2000 - 1000);
		c.vy = c.ty + lcg.next() % 60 - 30;
		c.width = i % 2 ? 0x22C8 : 0x11C8;
		c.height = i % 2 ? 0x1ECC : 0xFCA;
		static const uint32_t kMs[8] = {16, 33, 1, 250, 9999, 10000, 12345, 70000};
		const uint32_t e = i % 3 ? kMs[i % 8] : 1 + uint32_t(lcg.next() % 400);
		const BattleCloud before = c;
		rand.x = uint32_t(i) * 69069u + 5;
		battle_cloud_step(c, e, r);
		std::printf("step %d %d v %d %d t %d %d area %u %u ms %u -> %d %d v %d %d\n", before.x, before.y, before.vx,
			before.vy, before.tx, before.ty, unsigned(c.width), unsigned(c.height), e, c.x, c.y, c.vx, c.vy);
	}
	return 0;
}

}  // namespace

int main(int argc, char** argv) {
	if (argc >= 2 && std::string(argv[1]) == "--clouds") {
		return compare_clouds(argc > 2 ? std::atoi(argv[2]) : 200);
	}
	if (argc >= 2 && std::string(argv[1]) == "--wind") {
		return compare_wind(argc > 2 ? std::atoi(argv[2]) : 200);
	}
	if (argc >= 2 && std::string(argv[1]) == "--tint") {
		return compare_tint(argc > 2 ? std::atoi(argv[2]) : 1000);
	}
	if (argc < 2) {
		std::fprintf(stderr, "usage: compare_port TOWNS_DIR [STEPS] | --wind [STEPS] | --tint [COUNT] | --clouds [COUNT]\n");
		return 2;
	}
	const int steps = argc > 2 ? std::atoi(argv[2]) : 33;
	std::vector<int> towns;
	for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
		const std::string name = entry.path().filename().string();
		if (name.size() > 6 && name.compare(name.size() - 6, 6, ".nodes") == 0) {
			towns.push_back(std::atoi(name.c_str()));
		}
	}
	std::sort(towns.begin(), towns.end());
	for (int t : towns) {
		std::ifstream in(std::string(argv[1]) + "/" + std::to_string(t) + ".nodes", std::ios::binary);
		const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		std::vector<std::vector<TownPath>> nodes;
		if (!parse_town_nodes(data.data(), data.size(), nodes) || nodes.empty()) {
			continue;
		}
		const std::vector<TownPath>& paths = nodes[0];
		for (size_t slot = 0; slot < paths.size(); ++slot) {
			if (paths[slot].size() < 2) {
				continue;
			}
			struct Case {
				uint8_t state;
				int progress;
				bool coming_in;
			};
			std::vector<Case> cases = {{0, 0, false}};
			for (int k = 0; k < steps; ++k) {
				const int progress = k * 0x800 / (steps - 1);
				cases.push_back({1, progress, true});
				cases.push_back({8, progress, false});
				cases.push_back({0x18, progress, false});
			}
			for (const Case& c : cases) {
				PortPath path = port_path(paths[slot], c.coming_in);
				path.progress = int16_t(c.progress);
				PortPlace o;
				port_place(c.state, paths[slot], path, o);
				std::printf("town %d slot %zu state %02x prog %03x x %d.%04x y %d.%04x dir %02x sails %d\n", t, slot,
					c.state, c.progress, o.x, o.x_frac, o.y, o.y_frac, o.dir, o.sails);
			}
		}
	}
	return 0;
}
