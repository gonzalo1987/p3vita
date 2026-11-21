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

#include "p3data/port_scene.hpp"
#include "p3data/town_life.hpp"

using namespace p3::data;

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: compare_port TOWNS_DIR [STEPS]\n");
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
