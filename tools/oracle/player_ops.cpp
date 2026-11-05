// The operations the viewer queues for the player (p3data/hanse_tasks.hpp,
// p3data/player_actions.hpp), printed in hex so that the oracle's scenarios
// (tools/oracle/retouch_player.py, retouch_unchecked.py) queue the very same
// bytes in the game. For the market, what the window reads of the ship and
// the town is given (the scenario reads it from the game's memory).
//
// usage: player_ops take PLAYER EVENT
//        player_ops road EVENT AMOUNT TOWN PLAYER WARE
//        player_ops vote PLAYER CHOICE
//        player_ops buy STEP TOWN_UNITS WARE CAPACITY USED SHIP PLAYER TOWN
//        player_ops sell STEP SHIP_UNITS WARE SHIP PLAYER TOWN
//        player_ops hire-sailors SHIP AMOUNT | dismiss-sailors SHIP AMOUNT
//        player_ops hire-captain CAPTAIN SHIP TOWN | dismiss-captain SHIP
// (STEP 1, 5, 50, or 0 for all; «none» when the window sends nothing)

#include <cstdio>
#include <cstdlib>
#include <string>

#include "p3data/hanse_tasks.hpp"
#include "p3data/player_actions.hpp"

using namespace p3::data;

namespace {

int usage() {
	std::fprintf(stderr,
		"usage: player_ops take PLAYER EVENT\n"
		"       player_ops road EVENT AMOUNT TOWN PLAYER WARE\n"
		"       player_ops vote PLAYER CHOICE\n"
		"       player_ops buy STEP TOWN_UNITS WARE CAPACITY USED SHIP PLAYER TOWN\n"
		"       player_ops sell STEP SHIP_UNITS WARE SHIP PLAYER TOWN\n"
		"Prints the operation the viewer queues (0x14 bytes) in hex, or none.\n");
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
	} else if (what == "vote" && argc == 4) {
		op = vote_operation(uint32_t(number(argv[2])), uint32_t(number(argv[3])));
	} else if (what == "hire-sailors" && argc == 4) {
		op = hire_sailors_operation(uint32_t(number(argv[2])), uint32_t(number(argv[3])));
	} else if (what == "dismiss-sailors" && argc == 4) {
		op = dismiss_sailors_operation(uint32_t(number(argv[2])), uint32_t(number(argv[3])));
	} else if (what == "hire-captain" && argc == 5) {
		op = hire_captain_operation(uint32_t(number(argv[2])), uint32_t(number(argv[3])), uint32_t(number(argv[4])));
	} else if (what == "dismiss-captain" && argc == 3) {
		op = dismiss_captain_operation(uint32_t(number(argv[2])));
	} else if ((what == "buy" && argc == 10) || (what == "sell" && argc == 8)) {
		const bool buy = what == "buy";
		const int ware = int(number(argv[4]));
		const int32_t units = int32_t(std::strtol(argv[3], nullptr, 0));
		const int32_t amount = buy ? market_buy_amount(uint32_t(number(argv[2])), units, ware,
										 int32_t(std::strtol(argv[5], nullptr, 0)), int32_t(std::strtol(argv[6], nullptr, 0)))
								   : market_sell_amount(uint32_t(number(argv[2])), units, ware);
		const int k = buy ? 7 : 5;
		if (amount == 0) {
			std::printf("none\n");
			return 0;
		}
		op = market_operation(buy ? 0x02 : 0x01, amount, ware, int(number(argv[k])), uint16_t(number(argv[k + 1])),
			int(number(argv[k + 2])));
	} else {
		return usage();
	}
	for (const uint8_t b : op) {
		std::printf("%02x", b);
	}
	std::printf("\n");
	return 0;
}
