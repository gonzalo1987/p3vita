// The mailbox of letters and notices (the object 0x6DD730): sending a letter
// (0x4D6530), storing it for one merchant (0x4D6680), growing the mailbox
// (0x4D6B40), and copying and releasing an entry with what hangs from it
// (0x51AB20, 0x51AAB0). See docs/formats/mail.md.

#include "p3data/mail.hpp"

#include <cstring>

namespace p3::data {

namespace {

// The class of each letter type (0x6C0198). Past 0x87 the table runs into
// other data, which gives no 1 to 4.
constexpr uint8_t kLetterClass[0x88] = {
	2, 3, 3, 2, 2, 2, 4, 3, 2, 4, 1, 1, 4, 4, 4, 4, 4, 3, 2, 1, 1, 2, 2, 2,
	2, 1, 2, 2, 4, 2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 2, 1, 1, 1, 1,
	2, 1, 1, 2, 2, 4, 1, 1, 4, 4, 4, 1, 4, 1, 2, 3, 4, 1, 1, 2, 4, 4, 2, 1,
	1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 4, 4, 4, 4, 4, 4, 4, 3, 2, 1, 2, 1, 4,
	2, 2, 2, 3, 1, 1, 3, 3, 3, 3, 2, 1, 1, 1, 1, 1, 1, 4, 4, 2, 2, 2, 1, 1,
	1, 2, 2, 1, 2, 1, 1, 2, 1, 2, 1, 2, 1, 1, 0, 0,
};

// The keeping times of the settings (0x6C0220: never removed, 40, 20, 10, 5
// days, not received; past 5 the table runs into text).
constexpr uint32_t kLetterKeep[10] = {0xFFFFFFFF, 0x2800, 0x1400, 0xA00, 0x500, 0, 1702323527, 1650811506,
	540702049, 1701604449};

constexpr uint32_t kTownFirstOffice = 0x784;  // u16 a town's first office (next in the office's [+0x2CA])

// The mailbox's per-entry state kept apart: the buffers and records, as
// long as the entries.
void fit(SaveGame& save) {
	save.mail_records.resize(save.mail.size());
	save.mail_buffers.resize(save.mail.size());
}

// 0x4D6B40 (on the mailbox): 200 more entries. The free list's head comes
// down to the old count if it was past it; the old entries are moved into
// the new array (0x51AC00 moves the pointers, so the buffers and records stay
// with their letters); the new ones are built by 0x51A850 (day 0, month
// 0x80, year 0xFFFF, type and [+0x5] 0xFF, payload 0) and linked in order as
// free entries, the last one to count + 200.
void grow(SaveGame& save) {
	GlobalMemory& g = save.globals;
	uint16_t count = g.u16(kMailCount);
	if (count < g.u16(kMailFree)) {
		g.u16(kMailFree) = count;  // Sin comprobar con el oráculo.
	}
	save.mail.resize(count);
	for (int k = 0; k < 200; ++k) {
		GameMemory<0x10> e;
		e.u8(1) = 0x80;
		e.u16(2) = 0xFFFF;
		e.u8(4) = 0xFF;
		e.u8(5) = 0xFF;
		e.u16(6) = uint16_t(count + 1);
		save.mail.push_back(e);
		++count;
	}
	g.u16(kMailCount) = count;
	fit(save);
}

// Takes the head of the free list (growing the mailbox when it is empty:
// the count not above the head) and copies the letter into it (0x51AB20: the
// date, type and [+0x5], the payload with new copies of what hangs from it;
// not the link). Returns the entry.
uint16_t store(SaveGame& save, const Letter& letter) {
	GlobalMemory& g = save.globals;
	if (g.u16(kMailCount) <= g.u16(kMailFree)) {
		grow(save);
	}
	fit(save);
	const uint16_t index = g.u16(kMailFree);
	GameMemory<0x10>& e = save.mail[index];
	g.u16(kMailFree) = e.u16(6);
	std::memcpy(e.bytes.data(), letter.entry.bytes.data(), 6);
	const uint8_t type = letter.type();
	if (letter_has_record(type)) {
		// The game's [+0x8] and [+0xC] are the new record and buffer.
		std::memset(e.bytes.data() + 8, 0, 8);
		GameMemory<0x10> record = letter.record.value_or(GameMemory<0x10>());
		save.mail_records[index] = record;
		std::vector<uint8_t> buffer = letter.buffer;
		buffer.resize(record.u32(0));
		save.mail_buffers[index] = std::move(buffer);
	} else {
		std::memcpy(e.bytes.data() + 8, letter.entry.bytes.data() + 8, 8);
		save.mail_records[index].reset();
		if (letter_has_buffer(type)) {
			e.u32(8) = 0;  // The game's: the new buffer.
			std::vector<uint8_t> buffer = letter.buffer;
			buffer.resize(e.u32(0xC));
			save.mail_buffers[index] = std::move(buffer);
		} else {
			save.mail_buffers[index].clear();
		}
	}
	return index;
}

// 0x4D7B10 (on the mailbox, with the merchant and the letter): the
// interface's notice of a new letter. Nothing without the town view
// (*0x6E51AC). In a network game it marks the letters of the other human
// players with 0x40 in the month (for the network); for the local player,
// when no battle window is open, the sound and the line «Official writing:
// %s»... by the merchant's [+0x26]. Nothing of it is reproduced: p3vita is
// not a network game, and the viewer finds the unread letters itself.

// 0x4D6680 (on the mailbox, with a merchant and the letter): a human merchant
// ([+0x8] == 0) keeps it if its class lets it through: class 1 with the
// setting [+0x38], 2 with [+0x39] (0x46 always), 3 with [+0x3A] (0x11
// always), 4 always, 0 never (a setting lets it through if its keeping time
// is not 0). Then the interface's notice (0x4D7B10) and the copy at the head
// of the merchant's chain ([+0xA]).
void store_for(SaveGame& save, uint32_t merchant, const Letter& letter) {
	if (merchant >= save.merchants.size()) {
		return;  // Sin comprobar con el oráculo (an office's owner out of range: the game reads past the merchants).
	}
	MerchantMemory& m = save.merchants[merchant].memory;
	if (m.u16(8) != 0) {
		return;
	}
	const uint8_t type = letter.type();
	bool keep = false;
	switch (letter_class(type)) {
	case 1:
		keep = letter_keep(m.u8(0x38)) > 0;
		break;
	case 2:
		keep = type == 0x46 || letter_keep(m.u8(0x39)) > 0;
		break;
	case 3:
		keep = type == 0x11 || letter_keep(m.u8(0x3A)) > 0;
		break;
	case 4:
		keep = true;
		break;
	default:
		break;
	}
	if (!keep) {
		return;
	}
	const uint16_t index = store(save, letter);
	save.mail[index].u16(6) = m.u16(0xA);
	m.u16(0xA) = index;
}

}  // namespace

bool letter_has_buffer(uint8_t type) {
	return type == 0x29 || type == 0x47 || type == 0x63 || type == 0x83;
}

bool letter_has_record(uint8_t type) {
	return (type >= 0x3C && type <= 0x40) || type == 0x71;
}

uint8_t letter_class(uint8_t type) {
	return type < 0x88 ? kLetterClass[type] : 0;
}

uint32_t letter_keep(uint8_t setting) {
	return setting < 10 ? kLetterKeep[setting] : 0;  // Past 9 the game reads further text.
}

Letter::Letter() {
	entry.u8(1) = 0x80;
	entry.u16(2) = 0xFFFF;
	entry.u8(4) = 0xFF;
	entry.u8(5) = 0xFF;
	entry.u16(6) = 0xFFFF;
}

Letter::Letter(uint8_t type) : Letter() {
	entry.u8(4) = type;
}

void send_letter(SaveGame& save, int32_t merchant, const Letter& sent) {
	GlobalMemory& g = save.globals;
	if (sent.type() >= 0x86) {
		return;  // Sin comprobar con el oráculo.
	}
	const int32_t merchants = g.u16(global::kWorldMerchantCount);
	if (merchant >= merchants) {
		return;  // Sin comprobar con el oráculo.
	}
	Letter letter = sent;
	letter.entry.u8(0) = g.u8(global::kWorldDay);
	letter.entry.u8(1) = uint8_t(g.u8(global::kWorldMonth) | 0x80);
	letter.entry.u16(2) = g.u16(global::kWorldYear);
	if (merchant == kLetterToChronicle) {
		const uint16_t index = store(save, letter);
		save.mail[index].u16(6) = g.u16(kMailChronicle);
		g.u16(kMailChronicle) = index;
	} else if (merchant == kLetterToEach) {
		for (int32_t i = 0; i < int32_t(g.u16(global::kWorldMerchantCount)); ++i) {
			store_for(save, uint32_t(i), letter);
		}
	} else if (merchant == kLetterToTown) {
		// Sin comprobar con el oráculo (no caller sends to −3).
		const uint8_t town = letter.entry.u8(5);
		if (town >= save.towns.size()) {
			return;
		}
		const uint32_t offices = g.u16(global::kWorldOfficeCount);
		for (uint32_t o = save.towns[town].memory.u16(kTownFirstOffice); o < offices && o < save.offices.size();
			 o = save.offices[o].memory.u16(0x2CA)) {
			store_for(save, save.offices[o].memory.u16(0x2C4), letter);
		}
	} else if (merchant >= 0) {
		store_for(save, uint32_t(merchant), letter);
	}
	// Another negative code: the game takes it as a merchant and reads before
	// the merchants (sin comprobar con el oráculo; no caller does it).
}

void release_letter(SaveGame& save, size_t index) {
	GameMemory<0x10>& e = save.mail[index];
	e.u8(0) = 0;
	e.u8(1) = 0;
	e.u16(2) = 0xFFFF;
	e.u8(4) = 0xFF;
	e.u8(5) = 0;
	std::memset(e.bytes.data() + 8, 0, 8);
	if (index < save.mail_records.size()) {
		save.mail_records[index].reset();
	}
	if (index < save.mail_buffers.size()) {
		save.mail_buffers[index].clear();
	}
}

}  // namespace p3::data
