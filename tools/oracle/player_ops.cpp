// The operations the viewer queues for the player (p3data/hanse_tasks.hpp),
// printed in hex so that tools/oracle/retouch_player.py queues the very same
// bytes in the oracle.
//
// usage: player_ops take PLAYER EVENT
//        player_ops road EVENT AMOUNT TOWN PLAYER WARE

#include <cstdio>
#include <cstdlib>
#include <string>

#include "p3data/hanse_tasks.hpp"

using namespace p3::data;

namespace {

int usage() {
	std::fprintf(stderr,
		"usage: player_ops take PLAYER EVENT\n"
		"       player_ops road EVENT AMOUNT TOWN PLAYER WARE\n"
		"Prints the operation the viewer queues (0x4B or 0x3B, 0x14 bytes) in hex.\n");
	return 2;
}

unsigned long number(const char* s) {
	return std::strtoul(s, nullptr, 0);
}

}  // namespace

int main(int argc, char** argv) {
	OperationBytes op{};
	const std::string what = argc > 1 ? argv[1] : "";
	if (what == "take" && argc == 4) {
		op = take_task_operation(uint32_t(number(argv[2])), uint16_t(number(argv[3])));
	} else if (what == "road" && argc == 7) {
		op = road_materials_operation(uint16_t(number(argv[2])), uint16_t(number(argv[3])), uint8_t(number(argv[4])),
			uint8_t(number(argv[5])), uint8_t(number(argv[6])));
	} else {
		return usage();
	}
	for (const uint8_t b : op) {
		std::printf("%02x", b);
	}
	std::printf("\n");
	return 0;
}
