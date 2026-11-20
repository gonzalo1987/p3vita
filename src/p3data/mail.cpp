// The mailbox of letters and notices (the object 0x6DD730): sending a letter
// (0x4D6530), storing it for one merchant (0x4D6680), growing the mailbox
// (0x4D6B40), and copying and releasing an entry with what hangs from it
// (0x51AB20, 0x51AAB0). See docs/formats/mail.md.

#include "p3data/mail.hpp"

#include <algorithm>
#include <cstring>

namespace p3::data {

namespace tick::events_towns_detail {
void fn_4d6780(SaveGame& save, int32_t merchant);  // town_tick/events_towns.cpp
}

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
		g.u16(kMailFree) = count;  // Checked with the oracle: scenario mail_branches (saves 1, 3, 5, 7).
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
// %s»... by the merchant's [+0x26]. p3vita is not a network game: the entry
// of a letter stored for the local player goes to SaveGame::new_player_letters,
// and the viewer gives the notice (letters.hpp, letter_notice).

// 0x4D6680 (on the mailbox, with a merchant and the letter): a human merchant
// ([+0x8] == 0) keeps it if its class lets it through: class 1 with the
// setting [+0x38], 2 with [+0x39] (0x46 always), 3 with [+0x3A] (0x11
// always), 4 always, 0 never (a setting lets it through if its keeping time
// is not 0). Then the interface's notice (0x4D7B10) and the copy at the head
// of the merchant's chain ([+0xA]).
void store_for(SaveGame& save, uint32_t merchant, const Letter& letter) {
	if (merchant >= save.merchants.size()) {
		// Unreachable: store_for gets a merchant send_letter has checked, each
		// merchant, or the owner of an office of a town's list, which always has
		// one (a freed office leaves it). The game would read past the merchants.
		return;
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
	if (int32_t(merchant) == save.player) {
		save.new_player_letters.push_back(index);  // The interface's notice (0x4D7B10, above).
	}
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
		return;  // Checked with the oracle: scenario mail_branches (saves 1, 3, 5, 7).
	}
	const int32_t merchants = g.u16(global::kWorldMerchantCount);
	if (merchant >= merchants) {
		return;  // Checked with the oracle: scenario mail_branches (saves 1, 3, 5, 7).
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
		// No caller of the game sends to −3. Checked with the oracle: scenario mail_branches (saves
		// 1, 3, 5, 7), called directly. The game doesn't check the town (it would read past the
		// towns).
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
	// the merchants. Unreachable: no caller does it.
}

bool has_rank_letter(const SaveGame& save, uint32_t index, uint32_t rank) {
	const uint32_t count = save.globals.u16(kMailCount);
	// The game follows the chain without a limit of steps.
	for (uint32_t left = count; index < count && index < save.mail.size() && left > 0; --left) {
		const GameMemory<0x10>& e = save.mail[index];
		if (e.u8(4) == 0x5F && e.u16(0xA) == rank) {
			return true;
		}
		index = e.u16(6);
	}
	return false;
}

void mark_letter_read(SaveGame& save, int32_t merchant, uint32_t entry) {
	const GlobalMemory& g = save.globals;
	const uint32_t count = g.u16(kMailCount);
	if (entry >= count || merchant < 0 || merchant >= int32_t(g.u16(global::kWorldMerchantCount)) ||
		size_t(merchant) >= save.merchants.size()) {
		return;
	}
	// The game follows the chain for at most `count` steps.
	uint32_t i = save.merchants[size_t(merchant)].memory.u16(0xA);
	for (uint32_t left = count; i < count && i < save.mail.size() && left > 0; --left) {
		if (i == entry) {
			save.mail[i].u8(1) &= 0x7F;
			return;
		}
		i = save.mail[i].u16(6);
	}
}

void delete_letter(SaveGame& save, int32_t merchant, uint32_t entry) {
	GlobalMemory& g = save.globals;
	const uint32_t count = g.u16(kMailCount);
	if (entry >= count || merchant >= int32_t(g.u16(global::kWorldMerchantCount)) ||
		(merchant >= 0 && size_t(merchant) >= save.merchants.size())) {
		return;
	}
	// The link to follow: the chronicle's head for a negative merchant, else
	// the merchant's [+0xA]. The game follows the chain without a limit.
	uint16_t* link = merchant < 0 ? &g.u16(kMailChronicle) : &save.merchants[size_t(merchant)].memory.u16(0xA);
	for (size_t steps = 0; *link < count && *link < save.mail.size() && steps <= save.mail.size(); ++steps) {
		if (*link == entry) {
			GameMemory<0x10>& e = save.mail[entry];
			*link = e.u16(6);
			e.u16(6) = g.u16(kMailFree);
			g.u16(kMailFree) = uint16_t(entry);
			release_letter(save, entry);
			return;
		}
		link = &save.mail[*link].u16(6);
	}
}

void treasure_found(SaveGame& save, uint32_t merchant, uint32_t spot) {
	// 0x671FD0: the 100 treasure spots (u32 x, u32 y).
	static constexpr uint16_t kSpots[100][2] = {{84, 330}, {114, 1500}, {153, 462}, {159, 321}, {162, 1521}, {162, 672},
		{189, 762}, {198, 417}, {201, 330}, {201, 930}, {204, 1695}, {219, 1617}, {219, 621}, {225, 1506}, {243, 492},
		{252, 570}, {255, 1125}, {258, 351}, {288, 270}, {306, 1599}, {315, 1344}, {321, 1242}, {516, 1356}, {543, 1401},
		{543, 1479}, {662, 234}, {669, 426}, {699, 474}, {726, 897}, {726, 984}, {747, 813}, {750, 528}, {753, 1110},
		{771, 1332}, {777, 1182}, {822, 1233}, {837, 768}, {860, 1050}, {867, 516}, {867, 720}, {906, 1008}, {912, 1188},
		{912, 699}, {924, 864}, {930, 1230}, {936, 1131}, {951, 921}, {970, 1248}, {991, 981}, {996, 1191}, {1026, 516},
		{1032, 1146}, {1030, 570}, {1065, 618}, {1080, 930}, {1083, 1038}, {1098, 786}, {1111, 825}, {1119, 1128},
		{1122, 1239}, {1136, 957}, {1149, 867}, {1168, 1288}, {1206, 1338}, {1266, 1029}, {1296, 1098}, {1302, 1281},
		{1305, 936}, {1320, 1050}, {1389, 915}, {1416, 765}, {1428, 729}, {1431, 126}, {1434, 641}, {1443, 237},
		{1443, 556}, {1476, 534}, {1488, 264}, {1557, 615}, {1587, 138}, {1593, 1239}, {1629, 273}, {1642, 942},
		{1656, 858}, {1668, 321}, {1674, 552}, {1683, 591}, {1701, 1137}, {1701, 333}, {1716, 675}, {1740, 519},
		{1794, 315}, {1956, 387}, {1962, 201}, {2010, 369}, {2034, 186}, {2052, 266}, {2085, 249}, {2178, 105},
		{2292, 108}};
	GlobalMemory& g = save.globals;
	if (merchant >= g.u16(global::kWorldMerchantCount) || merchant >= save.merchants.size() || spot >= 100) {
		return;
	}
	const uint16_t x = kSpots[spot][0];
	const uint16_t y = kSpots[spot][1];
	const uint32_t count = g.u16(kMailCount);
	uint16_t* link = &save.merchants[merchant].memory.u16(0xA);
	for (uint32_t left = count; *link < count && *link < save.mail.size() && left > 0; --left) {
		const uint16_t entry = *link;
		GameMemory<0x10>& e = save.mail[entry];
		if (e.u8(4) == 0x45 && e.u16(0xC) == x && e.u16(0xE) == y) {
			// Out of the chain to the free list, its type 0xFF (nothing released: the game doesn't, 0x4D70CD).
			*link = e.u16(6);
			e.u8(4) = 0xFF;
			e.u16(6) = g.u16(kMailFree);
			g.u16(kMailFree) = entry;
			continue;
		}
		if (e.u8(4) == 0x46 && e.u16(0xC) == x && e.u16(0xE) == y) {
			e.u8(0xB) |= 0xFE;
		}
		link = &e.u16(6);
	}
}

void expire_letters(SaveGame& save, int32_t merchant) {
	tick::events_towns_detail::fn_4d6780(save, merchant);
}

void set_letter_settings(SaveGame& save, uint32_t merchant, const uint8_t* bytes, uint8_t notices) {
	// The merchant compared unsigned with the u16 count [0x6DE4AA] (0x4D6AFD).
	if (merchant >= save.globals.u16(global::kWorldMerchantCount) || merchant >= save.merchants.size()) {
		return;
	}
	MerchantMemory& m = save.merchants[merchant].memory;
	for (int k = 0; k < 8; ++k) {
		m.u8(0x38 + size_t(k)) = bytes[k];
	}
	m.u8(0x26) = notices;
}

void repair_letter_chains(SaveGame& save) {
	GlobalMemory& g = save.globals;
	const uint16_t count = g.u16(kMailCount);
	std::vector<uint8_t> seen(count, 0);  // operator new and memset of [+0x6] bytes.
	if (count < g.u16(kMailFree)) {
		g.u16(kMailFree) = count;
	}
	auto next = [&](uint16_t entry) -> uint16_t* {
		return entry < save.mail.size() ? &save.mail[entry].u16(6) : nullptr;
	};
	// Each merchant's chain from his [+0xA], then the chronicle's from [+0x8]: a letter reached a second time
	// cuts the link to it (0xFFFF).
	auto walk = [&](uint16_t* link) {
		while (link && *link < count) {
			if (seen[*link]) {
				*link = 0xFFFF;
				return;
			}
			seen[*link] = 1;
			link = next(*link);
		}
	};
	const uint16_t merchants = g.u16(global::kWorldMerchantCount);
	for (uint32_t m = 0; m < merchants && m < save.merchants.size(); ++m) {
		walk(&save.merchants[m].memory.u16(0xA));
	}
	walk(&g.u16(kMailChronicle));
	// The free list: a letter already in a chain ends it there (its link becomes the count, 0x4D7A7E).
	uint16_t* link = &g.u16(kMailFree);
	while (link && *link < count) {
		if (seen[*link]) {
			*link = count;
			return;
		}
		seen[*link] = 1;
		link = next(*link);
	}
}

void send_ticker(SaveGame& save, uint32_t merchant, uint8_t kind, uint16_t e) {
	// The kinds 0x5483D0 shows nothing for (its case 0x5487C5).
	static constexpr uint8_t kSilent[] = {0x02, 0x0F, 0x11, 0x13, 0x14, 0x17, 0x18, 0x1A, 0x1E, 0x1F, 0x26, 0x27};
	if (save.player < 0 || (merchant & 0xFFFF) != uint32_t(save.player) ||
		std::find(std::begin(kSilent), std::end(kSilent), kind) != std::end(kSilent)) {
		return;
	}
	// The kinds whose line is a fixed text (its case 0x548798): the argument
	// isn't kept (0), as the oracle records them.
	static constexpr uint8_t kFixed[] = {0x09, 0x0C, 0x10, 0x12, 0x19};
	const bool fixed = std::find(std::begin(kFixed), std::end(kFixed), kind) != std::end(kFixed);
	save.player_ticker.push_back({kind, fixed ? uint16_t(0) : e});
}

void send_ticker_text(SaveGame& save, uint32_t merchant, uint32_t english, uint16_t town) {
	if (save.player >= 0 && merchant == uint32_t(save.player)) {
		save.player_ticker.push_back({0, town, english});
	}
}

void send_ticker_interface(SaveGame& save, uint32_t merchant, uint8_t kind, uint16_t e) {
	const size_t before = save.player_ticker.size();
	send_ticker(save, merchant, kind, e);
	if (save.player_ticker.size() > before) {
		save.player_ticker.back().interface = true;
	}
}

std::vector<SaveGame::TickerLine> take_ticker(SaveGame& save) {
	std::vector<SaveGame::TickerLine> lines;
	lines.swap(save.player_ticker);
	return lines;
}

std::vector<uint16_t> take_new_letters(SaveGame& save) {
	std::vector<uint16_t> letters;
	letters.swap(save.new_player_letters);
	return letters;
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
