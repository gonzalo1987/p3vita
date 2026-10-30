#include "p3data/simulation.hpp"

#include <cstring>

#include "p3data/town_tick/internal.hpp"

namespace p3::data {

int town_for_tick(const SaveGame& save, uint32_t tick) {
	const uint32_t t = tick & 0xFF;
	int town = -1;
	if ((t & 7) == 7) {
		town = int(t >> 3);
	} else if ((t & 7) == 3) {
		town = int((t + 0xFF) >> 3);
	}
	return town >= 0 && size_t(town) < save.towns.size() ? town : -1;
}

void town_tick(SaveGame& save, int town) {
	tick::fn_51ba10(save, town, save.globals.u16(global::kWorldDayOfYear));
}

void advance_game_tick(SaveGame& save, Sailing& sailing) {
	sailing.begin_tick();
	const int town = town_for_tick(save, save.tick());
	if (town >= 0) {
		town_tick(save, town);
	}
	sailing.update_ships();
	run_queued_operations(save, sailing);
}

void run_queued_operations(SaveGame& save, Sailing& sailing) {
	std::vector<std::vector<uint8_t>> operations;
	operations.swap(save.queued_operations);
	for (const std::vector<uint8_t>& op : operations) {
		int32_t fields[5];
		std::memcpy(fields, op.data(), sizeof(fields));
		if (fields[0] == 0) {  // Zarpar (0x537670): {0, barco, ciudad, x, y}.
			sailing.set_sail(fields[1], fields[2], fields[3], fields[4]);
		}
	}
}

}  // namespace p3::data
