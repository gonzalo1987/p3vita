// The fixes to the original game's bugs (p3data/game_fixes.hpp), without game
// data: off, p3vita does what the game does (bug included); on, the fix.

#include <cstdio>
#include <cstdlib>

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/savegame.hpp"

using namespace p3::data;

namespace p3::data::tick::events_towns_detail {
// 0x4D6780 (town_tick/events_towns.cpp): expires the old letters of a merchant.
void fn_4d6780(SaveGame& save, int32_t merchant);
}  // namespace p3::data::tick::events_towns_detail

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

constexpr uint32_t kMailFree = 0x6DD734;   // u16: the mailbox's first free letter
constexpr uint32_t kMailCount = 0x6DD736;  // u16

// One merchant with the letters 0 → 1 → 2: 0 and 2 of type 0x46 (kept for
// ever), 1 of type 0x86 (kept 0 days: past the table of 0x6C0198). With
// `old`, letter 1 is from day 1 of year 0 and the time is later, so it goes.
SaveGame mailbox(bool old) {
	SaveGame save;
	save.globals.u16(global::kWorldMerchantCount) = 1;
	save.globals.u32(global::kWorldTick) = old ? 0x10000 : 0;
	save.globals.u16(kMailCount) = 3;
	save.globals.u16(kMailFree) = 0xFFFF;
	save.merchants.resize(1);
	save.merchants[0].memory.u16(0xA) = 0;  // the first letter
	save.mail.assign(3, GameMemory<0x10>());
	save.mail_records.assign(3, std::nullopt);
	const uint8_t types[3] = {0x46, 0x86, 0x46};
	for (uint16_t i = 0; i < 3; ++i) {
		GameMemory<0x10>& letter = save.mail[i];
		letter.u8(0) = 1;  // day 1, month 0, year 0
		letter.u8(4) = types[i];
		letter.u16(6) = i < 2 ? uint16_t(i + 1) : 0xFFFF;
	}
	return save;
}

void old_letters() {
	using tick::events_towns_detail::fn_4d6780;
	// Letter 1 is removed. The game cuts the chain at it, so the free list
	// gets the cut link (0xFFFF) instead of letter 1: it is lost.
	set_fix_game_bugs(false);
	SaveGame save = mailbox(true);
	fn_4d6780(save, 0);
	check(save.mail[0].u16(6) == 2, "off, removed: the chain goes 0 → 2");
	check(save.globals.u16(kMailFree) == 0xFFFF, "off, removed: the free list is lost (the game's bug)");
	set_fix_game_bugs(true);
	save = mailbox(true);
	fn_4d6780(save, 0);
	check(save.mail[0].u16(6) == 2, "on, removed: the chain goes 0 → 2");
	check(save.globals.u16(kMailFree) == 1, "on, removed: letter 1 goes to the free list");
	check(save.mail[1].u16(6) == 0xFFFF, "on, removed: the free list ends after it");

	// Letter 1 is kept (its age is 0). The game still cuts the chain before
	// it, so letters 1 and 2 drop out of the merchant's list.
	set_fix_game_bugs(false);
	save = mailbox(false);
	fn_4d6780(save, 0);
	check(save.mail[0].u16(6) == 0xFFFF, "off, kept: the chain is cut after letter 0 (the game's bug)");
	set_fix_game_bugs(true);
	save = mailbox(false);
	fn_4d6780(save, 0);
	check(save.mail[0].u16(6) == 1 && save.mail[1].u16(6) == 2, "on, kept: the chain stays 0 → 1 → 2");
	check(save.globals.u16(kMailFree) == 0xFFFF, "on, kept: nothing freed");
	set_fix_game_bugs(false);
}

}  // namespace

int main() {
	check(!fix_game_bugs(), "the library starts with the fixes off");
	old_letters();
	if (failures == 0) {
		std::printf("game_fixes_test: all correct\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
