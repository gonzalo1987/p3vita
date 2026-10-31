#include "p3data/savegame_write.hpp"

#include <algorithm>
#include <cstring>

#include "p3data/lz_compress.hpp"

namespace p3::data {

// The writers of Patrician3.exe, one function each, in the order 0x549CC0
// calls them (docs/cloud/savegames.md §1.1). Each one writes what its reader
// in savegame.cpp reads, from the memory p3data keeps like the game's.

namespace {

constexpr uint32_t kSeparator = 0xAFFE;
constexpr uint32_t kGameObject = 0x6DF2F0;  // The object of 0x549F40 and 0x5472F0.
constexpr uint32_t kMailbox = 0x6DD730;
constexpr uint32_t kMailCount = kMailbox + 0x6;  // u16
constexpr uint32_t kMailFree = kMailbox + 0x4;  // u16: the head of the free notices.

// Sequential writing, little endian like the game.
class Writer {
public:
	explicit Writer(std::vector<uint8_t>& out) : out_(out) {}

	void u8(uint8_t v) { out_.push_back(v); }
	void u16(uint16_t v) {
		u8(uint8_t(v));
		u8(uint8_t(v >> 8));
	}
	void u32(uint32_t v) {
		u16(uint16_t(v));
		u16(uint16_t(v >> 16));
	}
	void bytes(const uint8_t* p, size_t n) { out_.insert(out_.end(), p, p + n); }
	void bytes(const std::vector<uint8_t>& v) { bytes(v.data(), v.size()); }
	// n bytes of a game object from `offset`, as the game's memcpy.
	template <size_t N>
	void from(const GameMemory<N>& m, size_t offset, size_t n) {
		bytes(m.bytes.data() + offset, n);
	}
	void from(const GlobalMemory& g, uint32_t address, size_t n) {
		for (size_t i = 0; i < n; ++i) {
			u8(g.u8(address + uint32_t(i)));
		}
	}
	// A u16 high byte first (the game's readers rebuild some fields so).
	void u16_be(uint16_t v) {
		u8(uint8_t(v >> 8));
		u8(uint8_t(v));
	}
	// The u32 0xAFFE that ends each section.
	void separator() { u32(kSeparator); }

private:
	std::vector<uint8_t>& out_;
};

struct Chunk {
	size_t offset;
	size_t size;
};

template <size_t N>
void chunks(Writer& w, const GameMemory<N>& m, std::initializer_list<Chunk> list) {
	for (Chunk c : list) {
		w.from(m, c.offset, c.size);
	}
}

// A pointer of the game that goes into the file: 1 if it isn't null (in
// the game, a heap address of the process that saved).
uint32_t pointer(bool present) {
	return present ? 1 : 0;
}

// --- 0x549CC0: the settings ---

// What 0x549CC0 does to the settings (*0x6CC3E8) before writing them.
void update_settings(SaveGame& save) {
	SettingsMemory& s = save.settings;
	const GlobalMemory& g = save.globals;
	s.u8(0x1A) = g.u8(global::kWorldDay);
	s.u8(0x1B) = uint8_t(g.u8(global::kWorldMonth) + 1);
	s.u16(0x1C) = g.u16(global::kWorldYear);
	const uint32_t player = g.u32(global::kPlayer);
	const SavedMerchant* merchant = player < save.merchants.size() ? &save.merchants[player] : nullptr;
	const uint8_t hometown = merchant ? merchant->memory.u8(0x19) : 0;
	s.u8(0x1E) = (g.u8(kGameObject) & 0xC) ? 0 : hometown;
	std::memset(&s.u8(0x27), 0, 0x14);
	// GAME BUG: 0x549D7D takes the name from the table 0x6DDA00, which goes
	// by town number (0..39), with the index in the save of the player's
	// hometown: in 1.pat it writes "Cologne" (number 9) for Lübeck (index 9,
	// number 14). strcpy: the name may run over [+0x3B] and later fields.
	if (hometown < save.opaque.town_names.size()) {
		const std::string& name = save.opaque.town_names[hometown];
		const size_t n = std::min(name.size() + 1, SettingsMemory::kSize - 0x27);
		std::memcpy(&s.u8(0x27), name.c_str(), n);
	}
	s.u8(0x3B) = 0;
	if (merchant) {
		s.u8(0x3B) = uint8_t(s.u8(0x3B) | merchant->memory.u8(0x1A) << 7);
		s.u8(0x3B) = uint8_t(s.u8(0x3B) | merchant->memory.u8(0x39C + hometown));
	}
	std::memset(&s.u8(0x3C), 0, 0x10);
}

// --- The world (0x52F870) ---

// 0x52DE90 (storage) and 0x527910 (part 1): the town memory [0x0, 0x6D4).
// 0x525D80 (part 2): [0x82C, 0x840). 0x51EA30 (part 3) with 0x4FE1E0 (church)
// and 0x4F9680 (town_class1). 0x52A200 (part 4, without [0x822]), 0x5287A0
// (part 5: [0x6D4, 0x75C)), 0x50FFF0 (facilities) and 0x50FCE0 (nodes).
void write_town(Writer& w, const SavedTown& town) {
	const TownMemory& m = town.memory;
	w.u8(0x7F);
	w.from(m, 0x000, 0x6D4);
	w.u8(0x7F);
	w.from(m, 0x82C, 20);
	w.u8(0x7F);
	chunks(w, m,
		{{0x776, 2}, {0x760, 2}, {0x762, 2}, {0x764, 2}, {0x76A, 2}, {0x768, 2}, {0x766, 2}, {0x774, 1}, {0x771, 1},
			{0x770, 1}, {0x76C, 4}, {0x784, 2}, {0x773, 1}, {0x772, 1}, {0x789, 1}, {0x788, 1}, {0x78A, 1}, {0x78C, 2},
			{0x78E, 2}, {0x786, 2}, {0x790, 2}, {0x792, 2}, {0x775, 1}, {0x78B, 1}, {0x778, 6}, {0x77E, 6},
			{0x794, 16}});
	for (size_t i = 0; i < m.u16(0x776) && i < town.records.size(); ++i) {
		const BuildingRecord& r = town.records[i];
		w.u8(r.x);
		w.u8(r.y);
		w.u8(r.owner);
		w.u8(r.type);
		w.u16(r.next);
		w.u8(r.pending);
		w.u8(r.param);
	}
	w.from(m, 0x7A8, 4);
	w.from(m, 0x7AC, 4);
	w.from(m, 0x7B0, 0x5C);
	w.bytes(town.grid);
	w.u8(0x7F);
	chunks(w, m, {{0x810, 4}, {0x814, 4}, {0x818, 4}, {0x81C, 2}, {0x81E, 2}, {0x820, 2}, {0x824, 4}, {0x828, 4}});
	w.u8(0x7F);
	w.from(m, 0x6D4, 0x88);
	w.u8(0x7F);
	w.from(m, 0x840, 21 * 16);
	chunks(w, m, {{0x9A4, 16}, {0x990, 4}, {0x994, 2}, {0x996, 2}});
	for (size_t i = 0; i < 4; ++i) {
		w.from(m, 0x998 + i, 1);
		w.from(m, 0x99C + i, 1);
	}
	chunks(w, m,
		{{0x9A0, 2}, {0x9A2, 2}, {0x9B8, 4}, {0x9BC, 1}, {0x9BD, 1}, {0x9C0, 4}, {0x9C4, 1}, {0x9C6, 18}, {0x9D8, 18}});
	// The nodes in the order of the list [0x9B4]. The reader puts each one
	// first, so the order turns round at each save and load.
	w.u16(uint16_t(town.nodes.size()));
	for (const TownNode& node : town.nodes) {
		w.from(node, 4, 4);
		w.from(node, 8, 4);
		w.u16_be(node.u16(0xC));
		w.u16_be(node.u16(0xE));
		w.from(node, 0x10, 1);
		w.from(node, 0x11, 1);
		w.u16_be(node.u16(0x12));
	}
}

// 0x6335A0 (siege of a town) and 0x627920 (its army).
void write_siege(Writer& w, const SiegeMemory& m, const SiegeExtras& extras) {
	chunks(w, m, {{0x0, 4}, {0x4, 1}, {0x1018, 2}, {0x101A, 1}, {0x101B, 1}});
	for (size_t k = 0; k < 4; ++k) {
		w.from(m, 0x1010 + k, 1);
		w.from(m, 0x1014 + k, 1);
	}
	w.u8(uint8_t(extras.list_101c.size()));
	for (const GameMemory<8>& node : extras.list_101c) {
		w.from(node, 0, 8);
	}
	w.u8(uint8_t(extras.list_1020.size()));
	for (const GameMemory<0x10>& node : extras.list_1020) {
		w.from(node, 0, 0x10);
	}
	w.u8(extras.army.has_value());
	if (!extras.army) {
		return;
	}
	const SiegeArmyMemory& a = *extras.army;
	chunks(w, a,
		{{0x4, 4}, {0x0, 4}, {0x57C, 4}, {0x580, 4}, {0x584, 4}, {0x42, 2}, {0x66, 2}, {0x3C, 2}, {0x68, 2}, {0x3A, 2},
			{0x6A, 2}, {0x44, 2}, {0x78, 1}, {0x6D, 1}, {0x5D4, 1}, {0x61C, 1}, {0x62C, 1}});
	for (size_t k = 0; k < 5; ++k) {
		w.from(a, 0x21C + 4 * k, 4);
		w.from(a, 0x230 + 4 * k, 4);
	}
	w.from(a, 0x6E, 1);
	w.from(a, 0x6F, 1);
	if (a.u8(0x6F) != 0xFF) {
		for (uint32_t b = a.u8(0x6F); b < a.u8(0x6E); ++b) {
			w.from(a, 0x254 + 8 * b, 8);
		}
	}
}

// 0x4FED50: an office; the free ones only [0x2C4], [0x2C8] and [0x2CA].
void write_office(Writer& w, const SavedOffice& office) {
	const OfficeMemory& m = office.memory;
	w.from(m, 0x2C4, 2);
	if (m.u16(0x2C4) == 0xFFFF) {
		w.from(m, 0x2C8, 2);
		w.from(m, 0x2CA, 2);
		return;
	}
	w.from(m, 0x000, 0x2C0);
	chunks(w, m,
		{{0x2C0, 4}, {0x2C8, 2}, {0x2C6, 1}, {0x2C7, 1}, {0x2CA, 2}, {0x2CC, 2}, {0x2D2, 2}, {0x2D4, 2}, {0x2CE, 2},
			{0x2DE, 6}, {0x2D8, 6}, {0x2E4, 4}, {0x2D0, 2}, {0x2D6, 2}, {0x2EC, 2}, {0x2EE, 2}, {0x2F0, 2},
			{0x2F2, 2}, {0x2F4, 0x60}, {0x354, 0x60}, {0x3B8, 0x60}, {0x3B4, 4}, {0x418, 8}, {0x420, 8}, {0x428, 8},
			{0x430, 8}, {0x438, 8}, {0x440, 8}, {0x448, 2}, {0x44A, 2}, {0x2E8, 4}});
}

// 0x4F4010 → 0x4F6CF0 → 0x4F6090 and 0x5347F0: a merchant and its ten lists.
void write_merchant(Writer& w, const SavedMerchant& merchant, const std::array<std::vector<uint16_t>, 10>& lists) {
	const MerchantMemory& m = merchant.memory;
	chunks(w, m, {{0x0, 4}, {0x4, 4}, {0x8, 2}, {0xA, 2}, {0xC, 2}, {0xE, 2}, {0x10, 4}, {0x14, 2}, {0x16, 2}});
	w.from(m, 0x18, 10);
	w.u16_be(m.u16(0x22));
	chunks(w, m,
		{{0x24, 1}, {0x25, 1}, {0x26, 1}, {0x27, 1}, {0x28, 4}, {0x2C, 4}, {0x30, 1}, {0x31, 1}, {0x32, 1}, {0x37, 1},
			{0x33, 1}, {0x36, 1}, {0x34, 2}, {0x38, 8}, {0xF0, 0x28}, {0x40, 0xA0}, {0xE0, 1}, {0xE1, 1}, {0xE2, 2},
			{0x118, 4}, {0x11C, 0x1E0}, {0x2FC, 0xA0}, {0x3C4, 0xA0}, {0x464, 4}, {0x39C, 0x28}, {0x468, 4},
			{0x46C, 4}, {0x470, 4}, {0x474, 4}, {0x478, 2}, {0x47A, 2}});
	for (size_t o = 0x480; o <= 0x4D4; o += 4) {
		w.from(m, o, 4);
	}
	chunks(w, m,
		{{0x538, 4}, {0x4D8, 0x30}, {0x508, 0x30}, {0x53C, 4}, {0x540, 4}, {0x544, 2}, {0x546, 1}, {0x547, 1},
			{0x608, 4}, {0x548, 0x60}, {0x5A8, 0x60}, {0x60C, 0x14}});
	for (size_t k = 0; k < lists.size(); ++k) {
		const size_t n = m.u16(0x60C + 2 * k) & 0x7FFF;
		for (size_t i = 0; i < n; ++i) {
			w.u16(i < lists[k].size() ? lists[k][i] : 0);
		}
	}
}

// 0x52F870 marks as free (owner 0xFFFF) the offices of the free list
// ([+0x7E] of the world, then [+0x2C8] of each) before writing them.
void free_listed_offices(SaveGame& save) {
	const size_t count = save.offices.size();
	size_t steps = 0;
	for (size_t i = save.globals.u16(global::kWorld + 0x7E); i < count && steps <= count;
		 i = save.offices[i].memory.u16(0x2C8), ++steps) {
		save.offices[i].memory.u16(0x2C4) = 0xFFFF;
	}
}

void write_world(Writer& w, SaveGame& save) {
	const GlobalMemory& g = save.globals;
	constexpr uint32_t W = global::kWorld;
	for (Chunk c : {Chunk{0x0, 1}, {0x1, 1}, {0x2, 2}, {0x4, 2}, {0x10, 4}, {0x6, 2}, {0x8, 2}, {0xA, 2}}) {
		w.from(g, W + uint32_t(c.offset), c.size);
	}
	w.u8(g.u8(W + 0x89));
	w.u8(g.u8(W + 0x8C));
	w.u8(g.u8(W + 0x8A) ? 0x7F : 0);
	w.u8(g.u8(W + 0x8B));
	for (Chunk c : {Chunk{0xC, 4}, {0x14, 4}, {0x7C, 2}, {0x7E, 2}, {0x18, 40}, {0x40, 40}, {0x8D, 1}, {0x8E, 1},
			 {0x90, 7 * 12}}) {
		w.from(g, W + uint32_t(c.offset), c.size);
	}
	for (const SavedTown& town : save.towns) {
		write_town(w, town);
	}
	w.bytes(save.town_travel);
	for (uint8_t b : {0xF1, 0xF2, 0xF3, 0xF4}) {
		w.u8(b);
	}
	for (size_t i = 0; i < save.towns.size(); ++i) {
		static const SiegeMemory kEmpty{};
		static const SiegeExtras kNoExtras{};
		write_siege(w, i < save.sieges.size() ? save.sieges[i] : kEmpty,
			i < save.siege_extras.size() ? save.siege_extras[i] : kNoExtras);
	}
	for (const GameMemory<0x14>& record : save.world_records) {
		w.from(record, 0, 0x14);  // 0x4D3250
	}
	free_listed_offices(save);
	for (const SavedOffice& office : save.offices) {
		write_office(w, office);
	}
	for (size_t i = 0; i < save.merchants.size(); ++i) {
		static const std::array<std::vector<uint16_t>, 10> kNoLists{};
		write_merchant(w, save.merchants[i],
			i < save.opaque.merchant_lists.size() ? save.opaque.merchant_lists[i] : kNoLists);
	}
	// 0x535160 on 0x6DE588: 0xA08 bytes and 40 for each used slot.
	w.from(g, 0x6DE588, 0xA08);
	for (size_t i = 0; i < save.player_towns.size(); ++i) {
		if (g.u8(0x6DEF88 + uint32_t(i)) < 0xFF) {
			for (size_t k = 0; k < 40; ++k) {
				w.u8(k < save.player_towns[i].size() ? save.player_towns[i][k] : 0);
			}
		}
	}
	// 0x52BB30: the 15 world objects of [+0xB10]: their first 0x1C bytes,
	// the u16 [+0x1C] (not the length of the list) and the records of the list.
	for (uint32_t i = 0; i < 15; ++i) {
		const uint32_t object = W + 0xB10 + 0x2C * i;
		w.from(g, object, 0x1E);
		for (const GameMemory<0x10>& record : save.world_object_lists[i]) {
			w.from(record, 0, 0x10);
		}
	}
	// The optional object of [+0xE4] (0x533D10): p3data never has it.
	w.u8(0);
	w.u8(g.u8(W + 0x8F));
}

// --- The ships (0x504010) ---

// 0x5177D0 → 0x5162D0: a ship; the free slots, their state, [+0x6] and
// [+0x137] (from version 0x83).
void write_ship(Writer& w, const SavedShip& ship) {
	const ShipMemory& m = ship.memory;
	w.from(m, 0x134, 2);
	if (!ship.in_use()) {
		w.from(m, 0x6, 2);
		w.from(m, 0x137, 1);
		return;
	}
	chunks(w, m, {{0x4, 2}, {0x0, 1}, {0xE, 1}, {0xF, 1}, {0x1, 1}});
	w.u16_be(m.u16(0x2));
	chunks(w, m,
		{{0x10, 4}, {0x14, 4}, {0x6, 2}, {0x8, 2}, {0xA, 2}, {0xC, 2}, {0x18, 4}, {0x1C, 4}, {0x20, 4}});
	const bool has_route = m.u32(0x24) != 0;
	w.u32(pointer(has_route));
	chunks(w, m, {{0x28, 4}, {0x2C, 4}, {0x30, 4}, {0x34, 2}, {0x36, 1}, {0x37, 1}, {0x38, 1}, {0x39, 1}});
	w.u16_be(m.u16(0x3A));
	chunks(w, m, {{0x3C, 2}, {0x3E, 2}, {0x40, 2}, {0x42, 2}, {0x44, 4}, {0x48, 4}, {0x4C, 4}, {0x50, 4}});
	if (has_route && ship.own_route()) {
		const SeaRoute empty;
		const SeaRoute& route = ship.route ? *ship.route : empty;
		w.u16(uint16_t(route.size()));
		for (const MapPoint& p : route) {
			w.u16(uint16_t(p.x));
			w.u16(uint16_t(p.y));
		}
	}
	chunks(w, m,
		{{0x54, 0x60}, {0xB4, 0x60}, {0x114, 4}, {0x124, 4}, {0x128, 4}, {0x12C, 4}, {0x130, 2}, {0x132, 2},
			{0x136, 1}, {0x137, 1}, {0x13A, 1}, {0x13B, 1}, {0x138, 2}, {0x13C, 24}, {0x154, 2}, {0x156, 1},
			{0x157, 1}, {0x158, 1}, {0x159, 1}, {0x15A, 2}, {0x15C, 1}, {0x15D, 1}});
	w.u16_be(m.u16(0x15E));
	w.from(m, 0x160, 32);
}

void write_ships(Writer& w, const SaveGame& save) {
	const GlobalMemory& g = save.globals;
	w.u16(uint16_t(kWrittenSaveVersion));
	static constexpr uint32_t kHeaderFields[16] = {0xE0, 0xE2, 0xE4, 0xE8, 0xEA, 0xEC, 0xEE, 0xF0, 0xF2, 0xF4, 0xF6,
		0xE6, 0xFA, 0xFC, 0xFE, 0xF8};
	for (uint32_t field : kHeaderFields) {
		w.from(g, global::kShips + field, 2);
	}
	for (const CaptainMemory& c : save.captains) {  // 0x4FDE30
		chunks(w, c, {{0x0, 2}, {0x2, 1}, {0x3, 1}, {0x4, 4}, {0xC, 2}, {0x8, 1}, {0x9, 1}, {0xA, 1}, {0xB, 1},
						 {0xE, 1}, {0xF, 1}});
	}
	for (const SavedShip& ship : save.ships) {
		write_ship(w, ship);
	}
	for (const GameMemory<0x3C>& convoy : save.convoys) {  // 0x5013D0
		w.from(convoy, 0, 0x3A);
	}
	w.from(g, global::kShips + 0x20, 0xC0);
	// The 5 pirates of [+0xC] (0x5137E0), each after a byte 0 or 0x7F.
	for (size_t i = 0; i < 5; ++i) {
		if (!save.pirates[i]) {
			w.u8(0);
			continue;
		}
		w.u8(0x7F);
		const GameMemory<0x18>& p = *save.pirates[i];
		chunks(w, p,
			{{0x8, 2}, {0xA, 2}, {0xC, 2}, {0xE, 1}, {0xF, 1}, {0x10, 2}, {0x12, 2}, {0x14, 1}, {0x15, 1}, {0x16, 2}});
		const std::vector<uint8_t>& map = save.opaque.pirate_maps[i];
		w.u8(map.empty() ? 0 : 0x7F);
		w.bytes(map);  // 0x4F9680
		w.bytes(save.opaque.pirate_records[i]);
	}
}

// --- After the ships ---

bool notice_with_buffer(uint8_t type) {
	return type == 0x29 || type == 0x47 || type == 0x63 || type == 0x83;
}

bool notice_with_block(uint8_t type) {
	return (type >= 0x3C && type <= 0x40) || type == 0x71;
}

// 0x4D62B0 (version 5) and 0x51A8D0 for each notice.
void write_mail(Writer& w, const SaveGame& save) {
	const GlobalMemory& g = save.globals;
	w.u32(0xAFFE0005);
	for (uint32_t a : {kMailCount, kMailFree, kMailbox + 0x8, kMailbox + 0xA}) {
		w.from(g, a, 2);
	}
	for (size_t i = 0; i < save.mail.size(); ++i) {
		const GameMemory<0x10>& m = save.mail[i];
		static const std::vector<uint8_t> kNone;
		const std::vector<uint8_t>& extras = i < save.opaque.mail_extras.size() ? save.opaque.mail_extras[i] : kNone;
		w.from(m, 0, 8);
		const uint8_t type = m.u8(4);
		if (notice_with_buffer(type)) {
			w.u32(pointer(true));
			w.u32(uint32_t(extras.size()));
			w.bytes(extras);
		} else if (notice_with_block(type)) {
			w.bytes(extras);
		} else {
			w.from(m, 8, 8);
		}
	}
}

// 0x4F39E0: the node list of an event 0x34, from the bytes of the file it
// was read from (4 a node, 0x7F before each next one). GAME BUG (looks like
// one): the reader (0x4F3A40) keeps [+0x4] and [+0x5] and makes the u16
// [+0x6] of the third byte twice (b2 · 0x101), skipping the fourth; the
// writer writes [+0x4], [+0x5], the sign of the low byte of [+0x6] (`sar dl,
// 8` on a byte register) and that byte. So each node becomes b0 b1 sign(b2)
// b2, and the list always ends in 0.
void write_event_nodes(Writer& w, const std::vector<uint8_t>& list) {
	for (size_t p = 0; p + 4 <= list.size(); p += 5) {
		w.u8(list[p]);
		w.u8(list[p + 1]);
		w.u8(list[p + 2] & 0x80 ? 0xFF : 0);
		w.u8(list[p + 2]);
		if (p + 4 >= list.size() || list[p + 4] != 0x7F) {
			break;
		}
		w.u8(0x7F);
	}
	w.u8(0);
}

// 0x4D80F0: the event queue and, after the records, the extras of the types
// 0x1B (4 bytes an entry) and 0x34 (0x4F39E0).
void write_events(Writer& w, const SaveGame& save) {
	const GlobalMemory& g = save.globals;
	for (uint32_t a : {0x6DD744u, 0x6DD746u, 0x6DD748u}) {
		w.from(g, a, 2);
	}
	static const std::vector<uint8_t> kNone;
	auto extras_of = [&](size_t i) -> const std::vector<uint8_t>& {
		return i < save.opaque.event_extras.size() ? save.opaque.event_extras[i] : kNone;
	};
	for (size_t i = 0; i < save.events.size(); ++i) {
		GameMemory<0x18> e = save.events[i];
		if (e.u16(6) == 0x1B) {
			e.u32(0xC) = pointer(e.u16(0x12) != 0);
		} else if (e.u16(6) == 0x34 && e.u8(0x15) >= 7) {
			e.u32(8) = pointer(!extras_of(i).empty());
		}
		w.from(e, 0, 0x18);
	}
	for (size_t i = 0; i < save.events.size(); ++i) {
		const GameMemory<0x18>& e = save.events[i];
		if (e.u16(6) == 0x1B) {
			w.bytes(extras_of(i));
		} else if (e.u16(6) == 0x34 && e.u8(0x15) >= 7) {
			write_event_nodes(w, extras_of(i));
		}
	}
}

// UTF-8 to Latin-1, as the game keeps the names (latin1_to_utf8 undone).
std::string utf8_to_latin1(const std::string& s) {
	std::string out;
	for (size_t i = 0; i < s.size(); ++i) {
		const uint8_t c = uint8_t(s[i]);
		if (c >= 0xC0 && c < 0xE0 && i + 1 < s.size()) {
			out.push_back(char(((c & 0x1F) << 6) | (uint8_t(s[++i]) & 0x3F)));
		} else {
			out.push_back(char(c));
		}
	}
	return out;
}

// 0x511C60: the names. The 40 town names go as "A"; each list of names
// written by the players, its 20 slots (the game puts a 0 in the last byte
// of each slot of 24 bytes before copying it: 23 characters at most) and
// '#'; then the ship names of the save, which the loader undoes (0x5121C0
// at the end of the load): u16 0 and u16 0.
void write_names(Writer& w, const SaveGame& save) {
	for (int i = 0; i < 40; ++i) {
		w.u8('A');
		w.u8(0);
	}
	for (const std::vector<std::string>& list : save.custom_names) {
		for (size_t k = 0; k < 20; ++k) {
			const std::string latin1 = k < list.size() ? utf8_to_latin1(list[k]) : std::string();
			const size_t n = std::min<size_t>(latin1.size(), 23);
			w.bytes(reinterpret_cast<const uint8_t*>(latin1.data()), n);
			w.u8(0);
		}
		w.u8('#');
	}
	w.u16(0);
	w.u16(0);
}

// 0x5125E0: the town names of the file, each up to a byte below 0x20.
std::vector<std::string> split_town_names(const std::vector<uint8_t>& data) {
	std::vector<std::string> names(40);
	size_t p = 0;
	for (std::string& name : names) {
		while (p < data.size() && data[p] >= 0x20) {
			name.push_back(char(data[p++]));
		}
		while (p < data.size() && data[p] < 0x20) {
			++p;
		}
	}
	return names;
}

// The month starts of 0x672D78 (as in savegame.cpp).
constexpr uint16_t kMonthStart[14] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365, 0};

// 0x6C0198: how long each type of notice lasts (0x4D6780). From the type
// 0x86 on, the table runs into other data and the limit is always 0.
constexpr uint8_t kNoticeClass[0x86] = {
	2, 3, 3, 2, 2, 2, 4, 3, 2, 4, 1, 1, 4, 4, 4, 4,
	4, 3, 2, 1, 1, 2, 2, 2, 2, 1, 2, 2, 4, 2, 2, 2,
	2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 2, 1, 1, 1, 1,
	2, 1, 1, 2, 2, 4, 1, 1, 4, 4, 4, 1, 4, 1, 2, 3,
	4, 1, 1, 2, 4, 4, 2, 1, 1, 1, 1, 1, 1, 2, 2, 2,
	2, 2, 2, 4, 4, 4, 4, 4, 4, 4, 3, 2, 1, 2, 1, 4,
	2, 2, 2, 3, 1, 1, 3, 3, 3, 3, 2, 1, 1, 1, 1, 1,
	1, 4, 4, 2, 2, 2, 1, 1, 1, 2, 2, 1, 2, 1, 1, 2,
	1, 2, 1, 2, 1, 1,
};

// 0x6C0220: the limits a merchant chooses ([+0x38], [+0x39], [+0x3A]), in
// ticks: never, 40, 20, 10 and 5 days.
constexpr uint32_t kNoticeLimits[5] = {0xFFFFFFFF, 0x2800, 0x1400, 0xA00, 0x500};

uint32_t notice_limit(const SaveGame& save, const SavedMerchant& merchant, size_t index) {
	const GameMemory<0x10>& n = save.mail[index];
	const uint8_t type = n.u8(4);
	const uint8_t cls = type < sizeof(kNoticeClass) ? kNoticeClass[type] : 0;
	auto chosen = [&](size_t field) {
		const uint8_t k = merchant.memory.u8(field);
		return k < 5 ? kNoticeLimits[k] : 0;  // Beyond the table in the game; not checked.
	};
	switch (cls) {
	case 1:
		return chosen(0x38);
	case 2:
		return type == 0x46 ? 0xFFFFFFFF : chosen(0x39);
	case 3:
		return type == 0x11 ? 0x700 : chosen(0x3A);
	case 4:
		if (type == 0x0F || type == 0x10) {
			return 0x700;
		}
		if (type == 0x72) {
			return 0x1C00;
		}
		if (type == 0x71) {
			// [+0x8] points to the 16-byte block of the notice: until its u32
			// [+0x4], it doesn't expire.
			const std::vector<uint8_t>& block = save.opaque.mail_extras[index];
			uint32_t until = 0;
			if (block.size() >= 8) {
				std::memcpy(&until, block.data() + 4, 4);
			}
			return save.globals.u32(global::kWorldTick) < until ? 0xFFFFFFFF : 0;
		}
		return 0xFFFFFFFF;
	default:
		return 0;
	}
}

// 0x51AAB0: empties a notice (and frees its extras); [+0x6] stays.
void clear_notice(SaveGame& save, size_t index) {
	GameMemory<0x10>& n = save.mail[index];
	n.u8(0) = 0;
	n.u8(1) = 0;
	n.u16(2) = 0xFFFF;
	n.u8(4) = 0xFF;
	n.u8(5) = 0;
	std::memset(&n.u8(8), 0, 8);
	if (index < save.opaque.mail_extras.size()) {
		save.opaque.mail_extras[index].clear();
	}
}

}  // namespace

void expire_merchant_mail(SaveGame& save, int merchant) {
	GlobalMemory& g = save.globals;
	if (merchant < 0 || merchant >= int(save.merchants.size())) {
		return;
	}
	SavedMerchant& m = save.merchants[size_t(merchant)];
	const uint16_t count = g.u16(kMailCount);
	const uint32_t tick = g.u32(global::kWorldTick);
	uint16_t* link = &m.memory.u16(0xA);
	if (*link >= count) {
		return;
	}
	// The game counts down from the number of notices so as not to loop for ever.
	for (int remaining = count; remaining > 0 && *link < count && *link < save.mail.size(); --remaining) {
		const size_t index = *link;
		GameMemory<0x10>& n = save.mail[index];
		if (n.u8(4) >= 0x86) {
			// GAME BUG (looks like one): a notice of an unknown type cuts the
			// merchant's list at the link that points to it (0x4D6800), but
			// this one is still checked below. Not checked with the oracle.
			*link = 0xFFFF;
		}
		const uint32_t days = uint32_t(n.u16(2)) * 365 + kMonthStart[(n.u8(1) & 0x7F) % 14] + n.u8(0) - 1;
		const uint32_t stamp = days << 8;
		const uint32_t age = tick > stamp ? tick - stamp : 0;
		if (age > notice_limit(save, m, index)) {
			// Out of the merchant's list and into the free one.
			const uint16_t taken = *link;
			*link = n.u16(6);
			clear_notice(save, index);
			n.u16(6) = g.u16(kMailFree);
			g.u16(kMailFree) = taken;
		} else {
			link = &n.u16(6);
		}
	}
}

void prepare_save(SaveGame& save) {
	for (size_t i = 0; i < save.merchants.size() && i < save.globals.u16(global::kWorldMerchantCount); ++i) {
		expire_merchant_mail(save, int(i));
	}
}

std::vector<uint8_t> write_savegame(SaveGame& save) {
	std::vector<uint8_t> out;
	out.reserve(1 << 20);
	Writer w(out);
	w.u32(kSeparator | uint32_t(kWrittenSaveVersion) << 16);
	update_settings(save);
	w.from(save.settings, 0, SettingsMemory::kSize);
	for (uint32_t a : {0x6DFBC4u, 0x6DFBC8u, 0x6DFBCCu}) {
		w.from(save.globals, a, 4);
	}
	w.separator();
	write_world(w, save);
	w.separator();
	write_ships(w, save);
	w.separator();
	write_mail(w, save);
	w.separator();
	// 0x4D4B60 on 0x6DD728.
	w.from(save.globals, 0x6DD728, 2);
	w.u16(uint16_t(save.opaque.trade_routes.size()));
	for (const GameMemory<0xDC>& record : save.opaque.trade_routes) {
		w.from(record, 0, 0xDC);
	}
	w.separator();
	write_events(w, save);
	w.separator();
	// 0x611950: the battles, as they were read.
	if (save.opaque.battles.empty()) {
		w.u8(0);
		w.u32(0);
	} else {
		w.bytes(save.opaque.battles);
	}
	w.separator();
	write_names(w, save);
	w.separator();
	return out;
}

std::vector<uint8_t> savegame_file(SaveGame& save) {
	prepare_save(save);
	const std::vector<uint8_t> raw = write_savegame(save);
	return lz_compress(raw.data(), raw.size());
}

std::vector<uint8_t> empty_statistics() {
	std::vector<uint8_t> out;
	Writer w(out);
	w.u32(0xAFFE000B);
	for (int i = 0; i < 6; ++i) {
		w.u16(0);  // [+0x0..+0xA]: the counters, and [+0xA] the objects of the list.
	}
	for (int i = 0; i < 4; ++i) {
		w.u8(0);  // The series [+0xC..+0x18] (0x54D080) aren't there.
		w.u32(0xAFFEAFFE);
	}
	w.u32(0xAFFEAFFE);  // No objects in the list [+0x1C] (0x54CBD0).
	return out;
}

std::vector<uint8_t> statistics_file() {
	const std::vector<uint8_t> raw = empty_statistics();
	return lz_compress(raw.data(), raw.size());
}

void load_save_scripts(const CprArchive& archive, SaveGame& save) {
	for (const char* path : {"scripts/NamenStaedte_eng.txt", "scripts/NamenStaedte_spa.txt", "scripts/NamenStaedte.txt"}) {
		if (const CprEntry* entry = archive.find(path)) {
			std::vector<uint8_t> data;
			if (archive.read(*entry, data)) {
				save.opaque.town_names = split_town_names(data);
				return;
			}
		}
	}
}

}  // namespace p3::data
