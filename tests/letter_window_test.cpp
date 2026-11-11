// The letter window's buttons (p3data/letter_window.hpp), without game data: which letters have buttons and until
// when (the composer 0x47F3B0), what showing a letter 0x79 queues by itself, and the bytes of each button
// (0x4D6C90; the scenario letter_answers compares those with the game's own in the emulator).

#include <cstdio>
#include <cstring>

#include "p3data/letter_window.hpp"
#include "p3data/mail.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

uint32_t field(const OperationBytes& op, int k) {
	uint32_t v;
	std::memcpy(&v, op.data() + 4 * k, 4);
	return v;
}

constexpr uint32_t kDay = 1300 * 365 + 100;  // the 11th of April, 1300 (the day 100 of the year)

// A world on that day (tick at its start) with 4 towns, 2 merchants (the player 1) and an event queue of 4 records,
// and one letter of `type` dated that day at entry 3.
SaveGame world(uint8_t type) {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 4;
	g.u16(global::kWorldMerchantCount) = 2;
	g.u32(global::kPlayer) = 1;
	g.u32(global::kWorldTick) = kDay << 8;
	g.u16(kMailCount) = 4;
	save.mail.resize(4);
	save.mail_records.resize(4);
	save.mail_buffers.resize(4);
	GameMemory<0x10>& e = save.mail[3];
	e.u8(0) = 10;  // the 11th of April: 90 + 10
	e.u8(1) = 0x83;  // April (index 3), unread
	e.u16(2) = 1300;
	e.u8(4) = type;
	e.u8(5) = 2;
	g.u16(0x6DD748) = 4;
	save.events.resize(4);
	return save;
}

void at_day(SaveGame& save, uint32_t day, uint32_t part = 0) { save.globals.u32(global::kWorldTick) = (day << 8) + part; }

}  // namespace

int main() {
	// 0x1E: one button always; only its first answers (0x75 with −1). 0x1F: two, each its answer.
	{
		SaveGame save = world(0x1E);
		save.mail[3].u32(8) = 0x12345;
		save.mail[3].u16(0xC) = 1;
		save.mail[3].u16(0xE) = 0;
		check(letter_buttons(save, nullptr, 3).size() == 1, "0x1E has one button");
		const auto op = letter_answer(save, 3, 0);
		check(op && field(*op, 0) == 0x75 && field(*op, 1) == 0x12345 && field(*op, 2) == 1 && field(*op, 3) == 0 &&
				  field(*op, 4) == 0xFFFFFFFF,
			"0x1E: 0x75 {id, from, to, -1}");
		check(!letter_answer(save, 3, 1), "0x1E: a second answer queues nothing");
		save.mail[3].u8(4) = 0x1F;
		check(letter_buttons(save, nullptr, 3).size() == 2, "0x1F has two buttons");
		const auto reject = letter_answer(save, 3, 1);
		check(reject && field(*reject, 0) == 0x75 && field(*reject, 4) == 1, "0x1F: 0x75 with the answer");
	}
	// 0x21: a button while the tick is before the day [+0xE] of the letter's year; 0x72 with three rand() and a town.
	{
		SaveGame save = world(0x21);
		save.mail[3].u16(0xC) = 4000;
		save.mail[3].u16(8) = 5;
		save.mail[3].u16(0xA) = 7;
		save.mail[3].u16(0xE) = 114;  // 14 days after the letter
		at_day(save, 1300 * 365 + 113, 0xFF);
		check(letter_buttons(save, nullptr, 3).size() == 1, "0x21: in time");
		at_day(save, 1300 * 365 + 114);
		check(letter_buttons(save, nullptr, 3).empty(), "0x21: the day itself is too late");
		at_day(save, kDay);
		save.rand_state = 1;
		const auto op = letter_answer(save, 3, 0);
		check(op && field(*op, 0) == 0x72, "0x21: 0x72");
		if (op) {
			uint16_t sum, entry;
			std::memcpy(&sum, op->data() + 4, 2);
			std::memcpy(&entry, op->data() + 6, 2);
			check(sum == 4000 && entry == 3, "0x72: the sum and the entry");
			check((*op)[0xF] == 1 && (*op)[0x10] == 2 && (*op)[0x11] == 5 && (*op)[0x12] == 7 && (*op)[0x13] == 1,
				"0x72: the player, the letter's town and names, in time");
			check((*op)[0xE] < 4, "0x72: a town");
		}
		// MSVC's rand() four times from 1.
		uint32_t state = 1;
		for (int k = 0; k < 4; ++k) {
			state = state * 0x343FD + 0x269EC3;
		}
		check(save.rand_state == state, "0x72: four rand()");
		at_day(save, 1300 * 365 + 115);
		const auto late = letter_answer(save, 3, 0);
		check(late && (*late)[0x13] == 0, "0x72: too late");
	}
	// 0x22: a button while the tick is before the letter's day + [+0xF] days; 0x60 with five rand().
	{
		SaveGame save = world(0x22);
		save.mail[3].u8(0xF) = 14;
		at_day(save, kDay + 13, 0xFF);
		check(letter_buttons(save, nullptr, 3).size() == 1, "0x22: in time");
		at_day(save, kDay + 14);
		check(letter_buttons(save, nullptr, 3).empty(), "0x22: too late");
		save.rand_state = 7;
		save.mail[3].u8(0xB) = 6;
		const auto op = letter_answer(save, 3, 0);
		check(op && field(*op, 0) == 0x60 && (*op)[0xE] == 2 && (*op)[0x11] == 1 && (*op)[0x10] < 4,
			"0x22: 0x60 with the town % towns and the player");
		uint32_t state = 7;
		for (int k = 0; k < 4; ++k) {
			state = state * 0x343FD + 0x269EC3;
		}
		check(save.rand_state == state, "0x60: four rand()");
	}
	// 0x64: a button unless [+0xF] is 0xFE, while the tick's day isn't past the letter's day + 13; 0x8C.
	{
		SaveGame save = world(0x64);
		save.mail[3].u8(0xF) = 3;
		at_day(save, kDay + 13, 0xFF);
		check(letter_buttons(save, nullptr, 3).size() == 1, "0x64: the 13th day still");
		at_day(save, kDay + 14);
		check(letter_buttons(save, nullptr, 3).empty(), "0x64: too late");
		at_day(save, kDay);
		save.mail[3].u8(0xF) = 0xFE;
		check(letter_buttons(save, nullptr, 3).empty(), "0x64: answered");
		save.mail[3].u8(0xF) = 3;
		const auto op = letter_answer(save, 3, 0);
		check(op && field(*op, 0) == 0x8C && field(*op, 1) == 2 && field(*op, 2) == 1 && field(*op, 3) == 3 &&
				  field(*op, 4) == 3,
			"0x64: 0x8C {town, player, besieged, entry}");
	}
	// 0x79: two buttons while its event is the loan to decide; else showing it queues 0x77 {0xFFFF, entry, 1},
	// unless the event is past the queue.
	{
		SaveGame save = world(0x79);
		save.mail[3].u16(0xA) = 2;
		GameMemory<0x18>& loan = save.events[2];
		loan.u16(6) = 1;
		loan.u8(0x10) = 1;
		loan.u8(0x11) = 0xFF;
		check(letter_buttons(save, nullptr, 3).size() == 2, "0x79: the loan to decide");
		check(!letter_shown_operation(save, 3), "0x79: nothing queued by showing it");
		const auto op = letter_answer(save, 3, 1);
		check(op && field(*op, 0) == 0x77 && field(*op, 1) == 2 && field(*op, 2) == 3 && field(*op, 3) == 1,
			"0x79: 0x77 {event, entry, answer}");
		loan.u8(0x12) = 1;
		check(letter_buttons(save, nullptr, 3).empty(), "0x79: [+0x12] set");
		const auto shown = letter_shown_operation(save, 3);
		check(shown && field(*shown, 0) == 0x77 && field(*shown, 1) == 0xFFFF && field(*shown, 2) == 3 &&
				  field(*shown, 3) == 1,
			"0x79: showing it queues 0x77 {0xFFFF, entry, 1}");
		loan.u8(0x12) = 0;
		loan.u8(0x11) = 0;  // a merchant debtor
		check(letter_buttons(save, nullptr, 3).empty() && letter_shown_operation(save, 3), "0x79: a merchant debtor");
		save.mail[3].u16(0xA) = 0xFFFF;
		check(letter_buttons(save, nullptr, 3).empty() && !letter_shown_operation(save, 3), "0x79: no event");
	}
	// 0x3D: the record's labels (after the title and the text in the buffer) until its deadline; 0x74.
	{
		SaveGame save = world(0x3D);
		GameMemory<0x10>& record = save.mail_records[3].emplace();
		record.u32(4) = (kDay + 5) << 8;
		record.u8(0xB) = 2;
		record.u16(8) = 3;
		record.u8(0xA) = 1;
		const char text[] = "Title\0Text\0Yes\0No\0";
		save.mail_buffers[3].assign(text, text + sizeof(text) - 1);
		const std::vector<std::string> labels = letter_buttons(save, nullptr, 3);
		check(labels.size() == 2 && labels[0] == "Yes" && labels[1] == "No", "0x3D: the buffer's labels");
		const auto op = letter_answer(save, 3, 1);
		check(op && field(*op, 0) == 0x74 && field(*op, 1) == 3 && field(*op, 2) == 1 && field(*op, 3) == 1 &&
				  field(*op, 4) == 3,
			"0x3D: 0x74 {event, slot, answer, entry}");
		at_day(save, kDay + 5);
		check(letter_buttons(save, nullptr, 3).empty(), "0x3D: past the record's deadline");
	}
	// Others: none.
	{
		SaveGame save = world(0x06);
		check(letter_buttons(save, nullptr, 3).empty() && !letter_answer(save, 3, 0), "0x06: no buttons");
		check(letter_buttons(save, nullptr, 4).empty() && !letter_answer(save, 4, 0), "past the mailbox");
	}
	if (failures == 0) {
		std::printf("letter_window: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
