#include "p3data/seamap.hpp"

#include <cstdlib>

namespace p3::data {

std::vector<SeaMapTown> load_sea_map_towns(const CprArchive& archive) {
	const CprEntry* entry = archive.find("scripts/StadtDaten.ini");
	std::vector<uint8_t> data;
	if (!entry || !archive.read(*entry, data)) {
		return {};
	}
	std::vector<SeaMapTown> towns(40);
	const std::string text(data.begin(), data.end());
	SeaMapTown* town = nullptr;
	for (size_t pos = 0; pos < text.size();) {
		size_t end = text.find('\n', pos);
		if (end == std::string::npos) {
			end = text.size();
		}
		std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
			line.pop_back();
		}
		if (line.compare(0, 6, "[Stadt") == 0) {
			const int id = std::atoi(line.c_str() + 6);
			town = id >= 0 && id < int(towns.size()) ? &towns[size_t(id)] : nullptr;
			continue;
		}
		const size_t equals = line.find('=');
		if (!town || equals == std::string::npos) {
			continue;
		}
		const std::string key = line.substr(0, equals);
		const std::string value = line.substr(equals + 1);
		const int number = std::atoi(value.c_str());
		if (key == "StadtSymbolXPos") {
			town->symbol_x = number;
		} else if (key == "StadtSymbolYPos") {
			town->symbol_y = number;
		} else if (key == "NamenXPos") {
			town->name_x = number;
		} else if (key == "NamenYPos") {
			town->name_y = number;
		} else if (key == "NameLinksOderRechtsBuendig") {
			town->name_right_aligned = !value.empty() && (value[0] == 'r' || value[0] == 'R');
		} else if (key == "EreignisSymbolXPos") {
			town->event_x = number;
		} else if (key == "EreignisSymbolYPos") {
			town->event_y = number;
		} else if (key == "AnfahrtXPos") {
			town->approach_x = number;
		} else if (key == "AnfahrtYPos") {
			town->approach_y = number;
		}
	}
	return towns;
}

}  // namespace p3::data
