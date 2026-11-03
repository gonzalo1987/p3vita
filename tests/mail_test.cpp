// Sending a letter (p3data/mail.hpp, 0x4D6530), without game data: the
// human filter and the settings, the chain, the chronicle, growing the
// mailbox, the buffers, and releasing an entry. Checked against the game with
// the oracle (tools/oracle/retouch_mail.py); this keeps the rules in place.

#include <cstdio>
#include <cstdlib>

#include "p3data/game_memory.hpp"
#include "p3data/mail.hpp"
#include "p3data/savegame.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

// Three merchants: 0 and 2 human ([+0x8] = 0), 1 of the computer; no letters
// and no entries yet; the date 5.3.1310.
SaveGame world() {
	SaveGame save;
	save.globals.u16(global::kWorldMerchantCount) = 3;
	save.globals.u8(global::kWorldDay) = 5;
	save.globals.u8(global::kWorldMonth) = 3;
	save.globals.u16(global::kWorldYear) = 1310;
	save.merchants.resize(3);
	for (SavedMerchant& m : save.merchants) {
		m.memory.u16(0xA) = 0xFFFF;
	}
	save.merchants[1].memory.u16(8) = 2;
	save.globals.u16(kMailChronicle) = 0xFFFF;
	return save;
}

void sending() {
	SaveGame save = world();
	Letter tax(0x11);
	tax.entry.u8(5) = 9;
	tax.entry.u32(8) = 1234;
	send_letter(save, 0, tax);
	check(save.globals.u16(kMailCount) == 200, "the empty mailbox grows by 200");
	check(save.mail.size() == 200 && save.mail_buffers.size() == 200 && save.mail_records.size() == 200,
		"the entries and what hangs from them, the same size");
	check(save.merchants[0].memory.u16(0xA) == 0, "merchant 0's chain starts at entry 0");
	const GameMemory<0x10>& e = save.mail[0];
	check(e.u8(0) == 5 && e.u8(1) == (3 | 0x80) && e.u16(2) == 1310, "today's date, unread");
	check(e.u8(4) == 0x11 && e.u8(5) == 9 && e.u32(8) == 1234, "the type and the payload");
	check(e.u16(6) == 0xFFFF, "the end of the chain");
	check(save.globals.u16(kMailFree) == 1, "the free list goes on at entry 1");
	check(save.mail[1].u8(1) == 0x80 && save.mail[1].u8(4) == 0xFF && save.mail[1].u16(6) == 2,
		"a new free entry as 0x51A850 leaves it, linked to the next");
	check(save.mail[199].u16(6) == 200, "the last new entry links past the end");

	// The computer's merchants don't keep letters; −1 reaches the human ones.
	send_letter(save, 1, tax);
	check(save.merchants[1].memory.u16(0xA) == 0xFFFF, "merchant 1 (computer): nothing");
	send_letter(save, kLetterToEach, Letter(0x81));
	check(save.merchants[0].memory.u16(0xA) == 1 && save.mail[1].u16(6) == 0, "−1: merchant 0, at the head");
	check(save.merchants[2].memory.u16(0xA) == 2, "−1: merchant 2");

	// The settings: class 1 (0x0A) with [+0x38] = 5 (not received), class 2
	// (0x00) with [+0x39] = 5 but 0x46 always, class 3 with [+0x3A] = 5 but
	// 0x11 always, class 4 always.
	MerchantMemory& m = save.merchants[2].memory;
	m.u8(0x38) = 5;
	m.u8(0x39) = 5;
	m.u8(0x3A) = 5;
	const uint16_t before = m.u16(0xA);
	send_letter(save, 2, Letter(0x0A));
	send_letter(save, 2, Letter(0x00));
	send_letter(save, 2, Letter(0x07));
	check(m.u16(0xA) == before, "not received: classes 1, 2 and 3 with setting 5");
	send_letter(save, 2, Letter(0x46));
	send_letter(save, 2, Letter(0x11));
	send_letter(save, 2, Letter(0x0F));
	check(save.mail[m.u16(0xA)].u8(4) == 0x0F && save.mail[save.mail[m.u16(0xA)].u16(6)].u8(4) == 0x11,
		"0x46, 0x11 and the class 4 ones always");
	send_letter(save, 2, Letter(0x86));
	check(save.mail[m.u16(0xA)].u8(4) == 0x0F, "a type from 0x86 on is never stored");

	// The chronicle keeps everything, and the buffers go with their letter.
	Letter report(0x29);
	report.buffer = {1, 2, 3, 4};
	report.entry.u32(0xC) = 4;
	send_letter(save, kLetterToChronicle, report);
	const uint16_t chronicle = save.globals.u16(kMailChronicle);
	check(chronicle < save.mail.size() && save.mail[chronicle].u8(4) == 0x29, "the chronicle's head");
	check(save.mail_buffers[chronicle].size() == 4 && save.mail_buffers[chronicle][3] == 4, "the buffer");
	check(save.mail[chronicle].u32(8) == 0 && save.mail[chronicle].u32(0xC) == 4,
		"p3vita keeps 0 for the pointer and the size in [+0xC]");

	Letter mission(0x3D);
	GameMemory<0x10> record;
	record.u32(0) = 3;
	mission.record = record;
	mission.buffer = {7, 8, 9, 10};
	send_letter(save, 0, mission);
	const uint16_t head = save.merchants[0].memory.u16(0xA);
	check(save.mail_records[head] && save.mail_records[head]->u32(0) == 3, "the record");
	check(save.mail_buffers[head].size() == 3, "the record's buffer, of record [+0x0] bytes");

	release_letter(save, head);
	const GameMemory<0x10>& r = save.mail[head];
	check(r.u8(0) == 0 && r.u8(1) == 0 && r.u16(2) == 0xFFFF && r.u8(4) == 0xFF && r.u8(5) == 0,
		"released as 0x51AAB0 leaves it");
	check(!save.mail_records[head] && save.mail_buffers[head].empty(), "released: no record, no buffer");
}

}  // namespace

int main() {
	sending();
	if (failures) {
		std::fprintf(stderr, "%d failures\n", failures);
		return EXIT_FAILURE;
	}
	std::puts("mail: ok");
	return EXIT_SUCCESS;
}
