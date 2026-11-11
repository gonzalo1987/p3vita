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
//        player_ops repair SHIP | repair-convoy CONVOY
//        player_ops tavern-sailors ALLOWED RAND
//        player_ops office-to-ship STEP OFFICE_UNITS WARE CAPACITY USED SHIP PLAYER
//        player_ops ship-to-office STEP SHIP_UNITS WARE SHIP PLAYER
//        player_ops office-buy STEP TOWN_UNITS WARE CAPACITY USED MERCHANT PLAYER TOWN
//        player_ops office-sell STEP OFFICE_UNITS WARE MERCHANT PLAYER TOWN
//        player_ops artillery STEP UNITS TYPE TOWN SHIP FROM_SHIP
//        player_ops weapons-to-ship STEP OFFICE_UNITS WARE CAPACITY USED SHIP PLAYER TOWN PIECES
//        player_ops weapons-to-office STEP SHIP_UNITS WARE SHIP PLAYER
//        player_ops create-convoy SHIP PUBLIC DESTINATION | join-convoy GUIDE SHIP
//        player_ops leave-convoy SHIP CONVOY GUIDE CONVOY_SELECTED
//        player_ops convoy-buy STEP TOWN_UNITS WARE CAPACITY USED CONVOY PLAYER TOWN
//        player_ops convoy-sell STEP CONVOY_UNITS WARE CONVOY PLAYER TOWN
//        player_ops office-to-convoy STEP OFFICE_UNITS WARE CAPACITY USED CONVOY PLAYER
//        player_ops convoy-to-office STEP CONVOY_UNITS WARE CONVOY PLAYER
//        player_ops order-ship TYPE PLAYER TOWN | cancel-order PLAYER TOWN SHIP
//        player_ops upgrade SHIP LEVEL | rename SHIP NAME (one line per operation)
//        player_ops loan TERMS (32 hex digits: the window's terms, take out or grant)
//        player_ops pay-off PLAYER LOAN | shares ENTRY (24 hex digits: an entry of the shares' lists)
//        player_ops build PLAYER TOWN X Y CATALOG KEY (the town view's 0x21, from the window's picture)
//        player_ops street TOWN PLAYER | well PLAYER TOWN X Y | wall PLAYER TOWN NEXT_TYPE
//        player_ops demolish TOWN MERCHANT X Y | auction TOWN MERCHANT X Y
//        player_ops letters PLAYER DAYS1 DAYS2 DAYS3 EVENT_SOUND NOTICES | letters-check PLAYER REPAIR
//        player_ops pirate SHIP FLAG (the ship menu's «Pirate» 1, «Civil» 0)
//        player_ops letter-answer LETTER ENTRY ANSWER PLAYER TOWNS TICK RAND [RECORD] (a letter's button: the
//        operation, then the rand() state after)
// (STEP 1, 5, 50, or 0 for all; «none» when the window sends nothing;
// tavern-sailors prints the tavern's roll, its float's bits, and the
// sailors it shows, from what 0x4F6CA0 allows and the game's rand() value)

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "p3data/hanse_tasks.hpp"
#include "p3data/letter_window.hpp"
#include "p3data/mail.hpp"
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

// Hex digits into `out` (exactly 2 · N of them).
template <size_t N>
bool hex_bytes(const char* s, std::array<uint8_t, N>& out) {
	if (std::strlen(s) != 2 * N) {
		return false;
	}
	for (size_t k = 0; k < N; ++k) {
		const char pair[3] = {s[2 * k], s[2 * k + 1], 0};
		out[k] = uint8_t(std::strtoul(pair, nullptr, 16));
	}
	return true;
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
	} else if (what == "repair" && argc == 3) {
		op = repair_operation(uint32_t(number(argv[2])));
	} else if (what == "repair-convoy" && argc == 3) {
		op = repair_convoy_operation(uint32_t(number(argv[2])));
	} else if ((what == "office-to-ship" && argc == 9) || (what == "office-buy" && argc == 10)) {
		// As buying: the source's units (the office's or the town's), the
		// room of the destination (the ship's capacity or the office's).
		const int ware = int(number(argv[4]));
		const int32_t amount = market_buy_amount(uint32_t(number(argv[2])), int32_t(std::strtol(argv[3], nullptr, 0)),
			ware, int32_t(std::strtol(argv[5], nullptr, 0)), int32_t(std::strtol(argv[6], nullptr, 0)));
		if (amount == 0) {
			std::printf("none\n");
			return 0;
		}
		if (what == "office-to-ship") {  // SHIP PLAYER
			op = market_operation(0x08, amount, int(number(argv[7])), ware, uint16_t(number(argv[8])), 0);
			op[0xE] = 1;
		} else {  // MERCHANT PLAYER TOWN
			op = market_operation(0x59, amount, ware, int(number(argv[7])), uint16_t(number(argv[8])), int(number(argv[9])));
		}
	} else if ((what == "ship-to-office" && argc == 7) || (what == "office-sell" && argc == 8)) {
		// As selling: the source's units (the ship's or the office's).
		const int ware = int(number(argv[4]));
		const int32_t amount = market_sell_amount(uint32_t(number(argv[2])), int32_t(std::strtol(argv[3], nullptr, 0)), ware);
		if (amount == 0) {
			std::printf("none\n");
			return 0;
		}
		op = what == "ship-to-office"  // SHIP PLAYER
			? market_operation(0x08, amount, int(number(argv[5])), ware, uint16_t(number(argv[6])), 0)
			: market_operation(0x58, amount, ware, int(number(argv[5])), uint16_t(number(argv[6])), int(number(argv[7])));
	} else if (what == "artillery" || what == "weapons-to-ship" || what == "weapons-to-office") {
		// The window's mode 5: a row of the artillery (STEP UNITS TYPE TOWN SHIP
		// FROM_SHIP) or of the weapons (STEP OFFICE_UNITS WARE CAPACITY USED SHIP
		// PLAYER TOWN PIECES; STEP SHIP_UNITS WARE SHIP PLAYER).
		auto n = [&](int k) { return int32_t(std::strtol(argv[k], nullptr, 0)); };
		std::optional<OperationBytes> made;
		if (what == "artillery" && argc == 8) {
			made = artillery_row_bytes(uint32_t(n(2)), n(3), n(4), n(5), n(6), n(7) != 0);
		} else if (what == "weapons-to-ship" && argc == 11) {
			made = weapons_to_ship_bytes(uint32_t(n(2)), n(3), n(4), n(5), n(6), n(7), uint16_t(n(8)), n(9), n(10));
		} else if (what == "weapons-to-office" && argc == 7) {
			made = weapons_to_office_bytes(uint32_t(n(2)), n(3), n(4), n(5), uint16_t(n(6)));
		} else {
			return usage();
		}
		if (!made) {
			std::printf("none\n");
			return 0;
		}
		op = *made;
	} else if (what == "create-convoy" && argc == 5) {
		// One ship selected (the window's [+0xB4] = [+0xBC]): it guides.
		op = create_convoy_bytes(uint32_t(number(argv[2])), number(argv[3]) != 0, uint32_t(number(argv[4])));
	} else if (what == "join-convoy" && argc == 4) {
		const uint32_t w[3] = {0x18, uint32_t(number(argv[2])), uint32_t(number(argv[3]))};
		std::memcpy(op.data(), w, sizeof(w));
	} else if (what == "leave-convoy" && argc == 6) {
		op = leave_convoy_bytes(uint32_t(number(argv[2])), uint32_t(number(argv[3])), number(argv[4]) != 0,
			number(argv[5]) != 0);
	} else if ((what == "convoy-buy" || what == "office-to-convoy") && (argc == 10 || argc == 9)) {
		// As buying: the source's units (the town's or the office's), the
		// convoy's capacity and what it holds (the window's 0x4AA4A0).
		const int ware = int(number(argv[4]));
		const int32_t amount = market_buy_amount(uint32_t(number(argv[2])), int32_t(std::strtol(argv[3], nullptr, 0)),
			ware, int32_t(std::strtol(argv[5], nullptr, 0)), int32_t(std::strtol(argv[6], nullptr, 0)));
		if (amount == 0) {
			std::printf("none\n");
			return 0;
		}
		if (what == "convoy-buy" && argc == 10) {  // CONVOY PLAYER TOWN
			op = market_operation(0x1A, amount, ware, int(number(argv[7])), uint16_t(number(argv[8])), int(number(argv[9])));
		} else if (what == "office-to-convoy" && argc == 9) {  // CONVOY PLAYER
			op = market_operation(0x1B, amount, int(number(argv[7])), ware, uint16_t(number(argv[8])), 0);
			op[0xE] = 1;
		} else {
			return usage();
		}
	} else if ((what == "convoy-sell" && argc == 8) || (what == "convoy-to-office" && argc == 7)) {
		const int ware = int(number(argv[4]));
		const int32_t amount = market_sell_amount(uint32_t(number(argv[2])), int32_t(std::strtol(argv[3], nullptr, 0)), ware);
		if (amount == 0) {
			std::printf("none\n");
			return 0;
		}
		op = what == "convoy-sell"  // CONVOY PLAYER TOWN
			? market_operation(0x19, amount, ware, int(number(argv[5])), uint16_t(number(argv[6])), int(number(argv[7])))
			: market_operation(0x1B, amount, int(number(argv[5])), ware, uint16_t(number(argv[6])), 0);  // CONVOY PLAYER
	} else if (what == "order-ship" && argc == 5) {
		op = order_ship_operation(uint32_t(number(argv[2])), uint32_t(number(argv[3])), uint32_t(number(argv[4])));
	} else if (what == "cancel-order" && argc == 5) {
		op = cancel_order_operation(uint32_t(number(argv[2])), uint32_t(number(argv[3])), uint32_t(number(argv[4])));
	} else if (what == "upgrade" && argc == 4) {
		op = upgrade_operation(uint32_t(number(argv[2])), uint32_t(number(argv[3])));
	} else if (what == "loan" && argc == 3) {
		LoanTerms terms{};
		if (!hex_bytes(argv[2], terms)) {
			return usage();
		}
		op = loan_operation(terms);
	} else if (what == "pay-off" && argc == 4) {
		op = pay_off_operation(uint32_t(number(argv[2])), uint32_t(number(argv[3])));
	} else if (what == "shares" && argc == 3) {
		ShareEntry entry{};
		if (!hex_bytes(argv[2], entry)) {
			return usage();
		}
		op = shares_operation(entry);
	} else if (what == "build" && argc == 8) {
		// The town view's 0x21 from the picture the window asked for (its key).
		const BuildGhost ghost{uint8_t(number(argv[6])), uint32_t(number(argv[7]))};
		op = build_operation(uint32_t(number(argv[2])), uint32_t(number(argv[3])), uint8_t(number(argv[4])),
			uint8_t(number(argv[5])), ghost.catalog, build_param(ghost));
	} else if (what == "street" && argc == 4) {
		op = street_operation(uint32_t(number(argv[2])), uint32_t(number(argv[3])));
	} else if (what == "well" && argc == 6) {
		op = well_bytes(uint32_t(number(argv[2])), uint32_t(number(argv[3])), uint8_t(number(argv[4])),
			uint8_t(number(argv[5])));
	} else if (what == "wall" && argc == 5) {
		op = wall_operation(uint32_t(number(argv[2])), uint32_t(number(argv[3])), int(number(argv[4])));
	} else if ((what == "demolish" || what == "auction") && argc == 6) {
		const uint32_t w[4] = {uint32_t(number(argv[2])), uint32_t(number(argv[3])), uint32_t(number(argv[4])),
			uint32_t(number(argv[5]))};
		op = what == "demolish" ? demolish_operation(w[0], w[1], w[2], w[3]) : auction_operation(w[0], w[1], w[2], w[3]);
	} else if (what == "letters" && argc == 8) {
		// The letters' settings window's 0x70 on closing (0x4096DB).
		const std::array<uint8_t, 3> days = {uint8_t(number(argv[3])), uint8_t(number(argv[4])), uint8_t(number(argv[5]))};
		op = letter_settings_operation(uint32_t(number(argv[2])), days, number(argv[6]) != 0, uint8_t(number(argv[7])));
	} else if (what == "pirate" && argc == 4) {
		// The ship menu's «Pirate» / «Civil» (0x494244).
		op = pirate_operation(uint32_t(number(argv[2])), number(argv[3]) != 0);
	} else if (what == "letter-answer" && (argc == 9 || argc == 10)) {
		// LETTER (32 hex digits: the entry's 16 bytes) ENTRY ANSWER PLAYER TOWNS TICK RAND [RECORD (32 hex digits)]:
		// the button of the letter window (0x4D6C90), with the game's rand() state before it; the rand() state after
		// on a second line.
		std::array<uint8_t, 16> letter{};
		std::array<uint8_t, 16> record{};
		if (!hex_bytes(argv[2], letter) || (argc == 10 && !hex_bytes(argv[9], record))) {
			return usage();
		}
		const uint16_t entry = uint16_t(number(argv[3]));
		SaveGame save;
		save.globals.u16(kMailCount) = uint16_t(entry + 1);
		save.mail.resize(size_t(entry) + 1);
		std::memcpy(save.mail[entry].bytes.data(), letter.data(), 16);
		save.mail_records.resize(size_t(entry) + 1);
		save.mail_buffers.resize(size_t(entry) + 1);
		if (argc == 10) {
			save.mail_records[entry].emplace();
			std::memcpy(save.mail_records[entry]->bytes.data(), record.data(), 16);
		}
		save.globals.u32(global::kPlayer) = uint32_t(number(argv[5]));
		save.globals.u32(global::kWorldTownCount) = uint32_t(number(argv[6]));
		save.globals.u32(global::kWorldTick) = uint32_t(number(argv[7]));
		save.rand_state = uint32_t(number(argv[8]));
		const std::optional<OperationBytes> answer = letter_answer(save, entry, uint32_t(number(argv[4])));
		if (!answer) {
			std::printf("none\n");
			return 0;
		}
		for (const uint8_t b : *answer) {
			std::printf("%02x", b);
		}
		std::printf("\n%u\n", save.rand_state);
		return 0;
	} else if (what == "letters-check" && argc == 4) {
		op = letters_check_operation(uint32_t(number(argv[2])), number(argv[3]) != 0);
	} else if (what == "rename" && argc == 4) {
		for (const OperationBytes& o : rename_operations(uint32_t(number(argv[2])), argv[3])) {
			for (const uint8_t b : o) {
				std::printf("%02x", b);
			}
			std::printf("\n");
		}
		return 0;
	} else if (what == "tavern-sailors" && argc == 4) {
		// The roll of the tavern's window from the game's rand() value (its
		// float's bits) and the sailors it says there are.
		const float roll = tavern_roll_of(uint32_t(number(argv[3])));
		uint32_t bits;
		std::memcpy(&bits, &roll, 4);
		std::printf("%08x %u\n", bits, tavern_sailors_shown(uint8_t(number(argv[2])), roll));
		return 0;
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
