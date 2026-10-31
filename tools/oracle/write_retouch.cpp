// A retouched save for the branches of the savegame readers and writers (and
// of 0x4D6780) that the 12 saves of missav/ don't reach: it loads a save, adds
// what is listed below and writes it as the game would (version 0x85), without
// expiring the notices first. scripts/write-retouched.sh then makes the game
// (in the emulator) and p3data load it and save it again, and compares.
//
// usage: write_retouch <p2arch0_eng.cpr> <save> <out.pat>
//
// What it adds (each one with values the saves don't have):
// - town 0: two lists in its siege object (3 nodes of 8 bytes, 2 of 16);
// - the first pirate: a map object (4 × 3 cells);
// - world object 0: 2 records in its list ([+0x1C] and [+0x28] = 2);
// - an event 0x34 with [+0x15] = 7 and a list of 2 nodes;
// - merchant 1: two of its ten lists (one with the bit 15 of its length);
// - 2 automatic trade routes;
// - two free notices of the types 0x63 and 0x83 with their buffer;
// - merchant 0: its limits [+0x38..+0x3A] = 40, 10 and 5 days and a list of
//   notices of each class of 0x4D6780: expired and not, a 0x71 that doesn't
//   expire until its block says, and one of type 0x90 dated tomorrow that
//   cuts the list (the notices after it stay out of every list);
// - merchant 2: a notice of type 0x95 that cuts its list and expires (the
//   free notices are lost).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "p3data/lz_compress.hpp"
#include "p3data/savegame.hpp"
#include "p3data/savegame_write.hpp"

using namespace p3::data;

namespace {

constexpr uint32_t kMailFree = 0x6DD734;
constexpr uint32_t kMailCount = 0x6DD736;
constexpr uint16_t kMonthStart[13] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};

// Day, month and year of a notice so that 0x4D6780 counts the day `d` (from
// year 0): days = year · 365 + month start[month] + day − 1.
void set_notice_day(GameMemory<0x10>& n, int32_t d) {
	const int32_t year = d / 365;
	const int32_t rest = d % 365;
	int month = 0;
	while (month < 11 && kMonthStart[month + 1] <= rest) {
		++month;
	}
	n.u8(0) = uint8_t(rest - kMonthStart[month] + 1);
	n.u8(1) = uint8_t(month);
	n.u16(2) = uint16_t(year);
}

// Takes the first free notice out of the free list.
size_t take_free_notice(SaveGame& save) {
	const uint16_t index = save.globals.u16(kMailFree);
	if (index >= save.globals.u16(kMailCount) || index >= save.mail.size()) {
		std::fprintf(stderr, "no free notice left\n");
		std::exit(1);
	}
	save.globals.u16(kMailFree) = save.mail[index].u16(6);
	save.mail[index] = GameMemory<0x10>();
	save.opaque.mail_extras[index].clear();
	return index;
}

std::vector<uint8_t> pattern(size_t n, uint8_t seed) {
	std::vector<uint8_t> v(n);
	for (size_t i = 0; i < n; ++i) {
		v[i] = uint8_t(seed + 37 * i);
	}
	return v;
}

}  // namespace

int main(int argc, char** argv) {
	if (argc != 4) {
		std::fprintf(stderr, "usage: write_retouch <p2arch0_eng.cpr> <save> <out.pat>\n");
		return 2;
	}
	std::string error;
	auto archive = CprArchive::open(argv[1], &error);
	SaveGame save;
	if (!archive || !load_savegame(*archive, argv[2], save, &error)) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	GlobalMemory& g = save.globals;

	// Town 0: the two lists of its siege object.
	SiegeExtras& extras = save.siege_extras.at(0);
	for (uint8_t k = 0; k < 3; ++k) {
		GameMemory<8> node;
		std::memcpy(node.bytes.data(), pattern(8, uint8_t(0x10 + k)).data(), 8);
		extras.list_101c.push_back(node);
	}
	for (uint8_t k = 0; k < 2; ++k) {
		GameMemory<0x10> node;
		std::memcpy(node.bytes.data(), pattern(0x10, uint8_t(0x40 + k)).data(), 0x10);
		extras.list_1020.push_back(node);
	}
	save.sieges.at(0).u32(0x101C) = 1;
	save.sieges.at(0).u32(0x1020) = 1;

	// The first pirate: a map of 4 × 3 cells.
	for (size_t i = 0; i < 5; ++i) {
		if (!save.pirates[i]) {
			continue;
		}
		std::vector<uint8_t> map = {4, 0, 0, 0, 3, 0, 0, 0};
		const std::vector<uint8_t> rest = pattern(0x5C + 12, 0x77);
		map.insert(map.end(), rest.begin(), rest.end());
		save.opaque.pirate_maps[i] = map;
		save.pirates[i]->u32(0x0) = 1;
		break;
	}

	// World object 0: two records in its list.
	for (uint8_t k = 0; k < 2; ++k) {
		GameMemory<0x10> record;
		std::memcpy(record.bytes.data(), pattern(0x10, uint8_t(0x90 + k)).data(), 0x10);
		save.world_object_lists[0].push_back(record);
	}
	g.u16(global::kWorld + 0xB10 + 0x1C) = 2;
	g.u32(global::kWorld + 0xB10 + 0x28) = 2;

	// An event 0x34 with a list (the last event, whatever it was).
	if (!save.events.empty()) {
		GameMemory<0x18>& e = save.events.back();
		e.u16(6) = 0x34;
		e.u8(0x15) = 7;
		save.opaque.event_extras.back() = {1, 2, 0x85, 9, 0x7F, 3, 4, 0x05, 0xAA, 0x00};
	}

	// Merchant 1: two of its ten lists.
	if (save.merchants.size() > 1) {
		save.opaque.merchant_lists[1][0] = {1, 2, 3};
		save.opaque.merchant_lists[1][9] = {0x1234};
		save.merchants[1].memory.u16(0x60C) = 3;
		save.merchants[1].memory.u16(0x60C + 18) = 0x8001;
	}

	// Two automatic trade routes.
	for (uint8_t k = 0; k < 2; ++k) {
		GameMemory<0xDC> route;
		std::memcpy(route.bytes.data(), pattern(0xDC, uint8_t(0xC0 + k)).data(), 0xDC);
		save.opaque.trade_routes.push_back(route);
	}
	g.u16(0x6DD72A) = 2;

	// Merchant 0: the limits and a list of notices.
	const int32_t today = int32_t(g.u32(global::kWorldTick) >> 8);
	SavedMerchant& m = save.merchants.at(0);
	m.memory.u8(0x38) = 1;  // 40 days
	m.memory.u8(0x39) = 3;  // 10 days
	m.memory.u8(0x3A) = 4;  // 5 days
	struct Notice {
		uint8_t type;
		int32_t days_ago;
	};
	// Classes: 0x0A 1, 0x00 2, 0x46 2 (never), 0x11 3 (7 days), 0x0F 4 (7
	// days), 0x71 4 (its block), 0x90 beyond the table.
	const Notice notices[] = {{0x0A, 50}, {0x0B, 30}, {0x00, 12}, {0x46, 100}, {0x71, 200}, {0x11, 8}, {0x90, -1},
		{0x0F, 6}, {0x0A, 60}};
	uint16_t* link = &m.memory.u16(0xA);
	for (const Notice& notice : notices) {
		const size_t index = take_free_notice(save);
		GameMemory<0x10>& n = save.mail[index];
		set_notice_day(n, today - notice.days_ago);
		n.u8(4) = notice.type;
		n.u8(5) = 0x21;
		if (notice.type == 0x71) {
			// The 16-byte block (its u32 [+0x0] the bytes that follow, [+0x4]
			// the tick until which it doesn't expire) and 3 bytes.
			std::vector<uint8_t> block(16 + 3, 0x5A);
			const uint32_t size = 3;
			const uint32_t until = g.u32(global::kWorldTick) + 1000;
			std::memcpy(block.data(), &size, 4);
			std::memcpy(block.data() + 4, &until, 4);
			save.opaque.mail_extras[index] = block;
		} else {
			std::memcpy(&n.u8(8), pattern(8, notice.type).data(), 8);
		}
		*link = uint16_t(index);
		link = &n.u16(6);
	}
	*link = 0xFFFF;

	// Merchant 2: one notice of type 0x95, 30 days old. It cuts the list and
	// then expires, and 0x4D6780 takes as the new head of the free notices the
	// link it has just cut (0xFFFF): the free list is lost.
	if (save.merchants.size() > 2) {
		const size_t index = take_free_notice(save);
		GameMemory<0x10>& n = save.mail[index];
		set_notice_day(n, today - 30);
		n.u8(4) = 0x95;
		n.u16(6) = 0xFFFF;
		save.merchants[2].memory.u16(0xA) = uint16_t(index);
	}

	// Two free notices with a buffer (0x63 and 0x83).
	for (uint8_t type : {0x63, 0x83}) {
		const size_t index = take_free_notice(save);
		GameMemory<0x10>& n = save.mail[index];
		set_notice_day(n, today - 3);
		n.u8(4) = type;
		save.opaque.mail_extras[index] = pattern(type == 0x63 ? 5 : 0, type);
		n.u32(0xC) = uint32_t(save.opaque.mail_extras[index].size());
		n.u16(6) = g.u16(kMailFree);
		g.u16(kMailFree) = uint16_t(index);
	}

	const std::vector<uint8_t> raw = write_savegame(save);
	const std::vector<uint8_t> packed = lz_compress(raw.data(), raw.size());
	FILE* out = std::fopen(argv[3], "wb");
	if (!out || std::fwrite(packed.data(), 1, packed.size(), out) != packed.size()) {
		std::fprintf(stderr, "can't write %s\n", argv[3]);
		return 1;
	}
	std::fclose(out);
	std::printf("%zu bytes in %s\n", packed.size(), argv[3]);
	return 0;
}
