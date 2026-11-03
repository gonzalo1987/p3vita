// Sending a letter (p3data/mail.hpp, 0x4D6530), without game data: the
// human filter and the settings, the chain, the chronicle, growing the
// mailbox, the buffers, and releasing an entry. Checked against the game with
// the oracle (tools/oracle/retouch_mail.py); this keeps the rules in place.

#include <cstdio>
#include <cstdlib>

#include <string>
#include <vector>

#include "p3data/game_memory.hpp"
#include "p3data/letters.hpp"
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

// Operations 0x6E and 0x6F (0x4D69D0, 0x4D6A50; checked with retouch_mail.py
// read_delete), and the new letters' queue for the viewer.
void reading_and_deleting() {
	SaveGame save = world();
	save.player = 0;
	for (uint8_t type : {0x11, 0x00, 0x29}) {
		Letter letter(type);
		if (type == 0x29) {
			letter.entry.u32(0xC) = 2;
			letter.buffer = {1, 2};
		}
		send_letter(save, 0, letter);  // Entries 0, 1, 2; the chain 2, 1, 0.
	}
	send_letter(save, 2, Letter(0x00));  // Entry 3, merchant 2's.
	send_letter(save, kLetterToChronicle, Letter(0x06));  // Entry 4.
	const std::vector<uint16_t> fresh = take_new_letters(save);
	check(fresh.size() == 3 && fresh[0] == 0 && fresh[2] == 2, "the player's new letters, oldest first");
	check(take_new_letters(save).empty(), "taken once");

	mark_letter_read(save, 0, 1);
	check((save.mail[1].u8(1) & 0x80) == 0, "0x6E: read");
	mark_letter_read(save, 0, 3);
	mark_letter_read(save, -1, 0);
	mark_letter_read(save, 0, 999);
	check((save.mail[3].u8(1) & 0x80) && (save.mail[0].u8(1) & 0x80), "0x6E: not in his chain, nothing");

	delete_letter(save, 0, 1);  // The middle one.
	check(save.mail[2].u16(6) == 0, "0x6F: the chain skips it");
	check(save.globals.u16(kMailFree) == 1 && save.mail[1].u16(6) == 5 && save.mail[1].u8(4) == 0xFF,
		"0x6F: at the head of the free list, released");
	delete_letter(save, 0, 2);  // The head, with its buffer.
	check(save.merchants[0].memory.u16(0xA) == 0 && save.mail_buffers[2].empty(), "0x6F: the head, its buffer freed");
	delete_letter(save, 0, 3);  // Merchant 2's.
	check(save.merchants[2].memory.u16(0xA) == 3 && save.mail[3].u8(4) == 0x00, "0x6F: another chain, nothing");
	delete_letter(save, -2, 4);
	check(save.globals.u16(kMailChronicle) == 0xFFFF && save.globals.u16(kMailFree) == 4, "0x6F: the chronicle");
}

// The ticker lines' queue (0x548CA0): the local player's only, not the kinds
// without a line, the argument of a fixed text as 0, the game's own lines.
void ticker_queue() {
	SaveGame save = world();
	save.player = 2;
	send_ticker(save, 2, 0x16, 7);  // Sunk: kept.
	send_ticker(save, 0, 0x16, 7);  // Another merchant.
	send_ticker(save, 2, 0x13, 4);  // The plague: no line.
	send_ticker(save, 0x10002, 0x0C, 9);  // The merchant's low 16 bits; a fixed text.
	send_ticker_text(save, 2, 0x6C34CC, 3);
	send_ticker_interface(save, 2, 0x10, 5);
	const std::vector<SaveGame::TickerLine> lines = take_ticker(save);
	check(lines.size() == 4, "four lines for the player");
	check(lines[0].kind == 0x16 && lines[0].e == 7 && !lines[0].text && !lines[0].interface, "a line");
	check(lines[1].kind == 0x0C && lines[1].e == 0, "a fixed text's argument as 0");
	check(lines[2].text == 0x6C34CC && lines[2].e == 3, "the game's own line");
	check(lines[3].interface && lines[3].e == 0, "an interface's line");
	check(take_ticker(save).empty(), "taken once");
}

// A letter's text as the game's text box reads it (0x462540).
void text_codes() {
	const std::string raw = std::string("\n\\r\\f3_L\xFC" "beck,  1. 4. 1362\n\n\\l\\cHanse_tag\n\\l\\dYou owe 12\\C.\r\nX") +
		'\0' + "after";
	const std::vector<LetterParagraph> p = letter_paragraphs(raw);
	check(p.size() == 6, "a paragraph per line");
	check(p[1].align == LetterParagraph::Right && p[1].font == 3 && p[1].text == "L\xC3\xBC" "beck,  1. 4. 1362",
		"\\r, \\f3_ and Windows-1252 to UTF-8");
	check(p[2].align == LetterParagraph::Right && p[2].text.empty(), "the alignment stays");
	check(p[3].align == LetterParagraph::Center && p[3].text == std::string("Hanse") + kLetterSoftHyphen + "tag",
		"\\l\\c: centred, the soft hyphen");
	check(p[4].align == LetterParagraph::Left && p[4].text == std::string("You owe 12") + kLetterCoin + ".",
		"\\d out, the coin, the carriage return out");
	check(p[5].text == "X", "a 0 ends the text");
	check(p[0].picture < 0 && p[2].picture < 0 && p[5].picture < 0, "no picture");
	const std::vector<LetterParagraph> news = letter_paragraphs("\\c\\f3_1. 4. 1362\n\n\\l\\f0_News\n\n\\c\\t16063_ ");
	check(news.size() == 5 && news[4].picture == 16063 && news[4].text == " " && news[4].align == LetterParagraph::Center &&
			news[3].picture < 0 && news[2].font == 0 && news[0].font == 3,
		"the chronicle's ornament, \\t16063_; the fonts");
}

}  // namespace

int main() {
	sending();
	reading_and_deleting();
	ticker_queue();
	text_codes();
	if (failures) {
		std::fprintf(stderr, "%d failures\n", failures);
		return EXIT_FAILURE;
	}
	std::puts("mail: ok");
	return EXIT_SUCCESS;
}
