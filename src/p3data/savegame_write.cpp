#include "p3data/savegame_write.hpp"

#include <algorithm>
#include <cstring>

#include "p3data/game_fixes.hpp"
#include "p3data/lz.hpp"
#include "p3data/lz_compress.hpp"
#include "p3data/statistics.hpp"

namespace p3::data {

namespace tick::events_towns_detail {
// 0x4D6780 (town_tick/events_towns.cpp): expires the old notices of a merchant.
void fn_4d6780(SaveGame& save, int32_t merchant);
}  // namespace tick::events_towns_detail

// The writers of Patrician3.exe, one function each, in the order 0x549CC0
// calls them (docs/cloud/savegames.md §1.1). Each one writes what its reader
// in savegame.cpp reads, from the memory p3data keeps like the game's.

namespace {

constexpr uint32_t kSeparator = 0xAFFE;
constexpr uint32_t kGameObject = 0x6DF2F0;  // The object of 0x549F40 and 0x5472F0.
constexpr uint32_t kMailbox = 0x6DD730;
constexpr uint32_t kMailCount = kMailbox + 0x6;  // u16
constexpr uint32_t kMailFree = kMailbox + 0x4;  // u16: the head of the free notices.
constexpr uint32_t kTownNumbers = 0x6DE4B8;  // u8 per town of the save: its number (0..39)

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
	// Fixed: the name of the hometown's number (0x6DE4B8), at most 19
	// characters and its 0 in the 0x14 bytes of [+0x27].
	const size_t number = fix_game_bugs() ? g.u8(kTownNumbers + hometown) : hometown;
	const std::vector<std::string>& table = fix_game_bugs() ? save.opaque.town_names : save.opaque.town_name_table;
	if (number < table.size()) {
		const std::string& name = table[number];
		const size_t n = fix_game_bugs() ? std::min<size_t>(name.size(), 0x13)
		                                 : std::min(name.size() + 1, SettingsMemory::kSize - 0x27);
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

// 0x5350F0 (the size of the object 0x6DE588, which the save's size 0x549C60
// takes before writing): a human player's slot whose merchant doesn't exist,
// or without its byte per town, is freed (0xFF). No loaded save has one; a new
// game's eight are all freed.
void free_unused_player_slots(SaveGame& save) {
	GlobalMemory& g = save.globals;
	for (uint32_t i = 0; i < 8; ++i) {
		uint8_t& slot = g.u8(0x6DEF88 + i);
		if (slot >= g.u16(global::kWorldMerchantCount)) {
			slot = 0xFF;
			save.player_towns[i].clear();
			g.u32(0x6DEF90 + 4 * i) = 0;
		} else if (save.player_towns[i].empty()) {
			slot = 0xFF;
		}
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
	free_unused_player_slots(save);
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
// [+0x137] (from version 0x83). Its own route's flag [+0x36] is written as 0
// without a route, 1 when its origin or destination is no town (0x5163F2:
// then the points are written even for a route of the matrix, and the load
// reads it as the ship's own), else as it is; the points follow when it is 1.
void write_ship(Writer& w, const SavedShip& ship, uint32_t towns) {
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
	const bool own = has_route && (m.u8(0x37) >= towns || m.u8(0x38) >= towns || ship.own_route());
	chunks(w, m, {{0x28, 4}, {0x2C, 4}, {0x30, 4}, {0x34, 2}});
	w.u8(own ? 1 : 0);
	chunks(w, m, {{0x37, 1}, {0x38, 1}, {0x39, 1}});
	w.u16_be(m.u16(0x3A));
	chunks(w, m, {{0x3C, 2}, {0x3E, 2}, {0x40, 2}, {0x42, 2}, {0x44, 4}, {0x48, 4}, {0x4C, 4}, {0x50, 4}});
	if (own) {
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
		chunks(w, c, {{0x0, 2}, {0x2, 1}, {0x3, 1}, {0x4, 4}, {0xC, 2}, {0x8, 1}, {0x9, 1}, {0xA, 1}, {0xB, 1}});
		w.u8(c.u8(0xE) != 0 ? 1 : 0);  // as a bool (a free captain's 0xFF, a new game's, is written 1)
		w.from(c, 0xF, 1);
	}
	for (const SavedShip& ship : save.ships) {
		write_ship(w, ship, g.u32(global::kWorldTownCount));
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
		w.bytes(pirate_nest_bytes(save, i));  // The map (0x4F9680) and the pool.
	}
}

// --- After the ships ---

bool notice_with_buffer(uint8_t type) {
	return type == 0x29 || type == 0x47 || type == 0x63 || type == 0x83;
}

bool notice_with_block(uint8_t type) {
	return (type >= 0x3C && type <= 0x40) || type == 0x71;
}

// 0x4D62B0 (version 5) and 0x51A8D0 for each notice: its 8 bytes and, for
// the types with a buffer, the pointer, [+0xC] and the [+0xC] bytes of the
// buffer; for the types with a record, the 16 bytes of the record
// (SaveGame::mail_records, which the simulation changes) and its [+0x0] bytes
// (SaveGame::mail_buffers); for the others, [+0x8..+0xF].
void write_mail(Writer& w, const SaveGame& save) {
	const GlobalMemory& g = save.globals;
	w.u32(0xAFFE0005);
	for (uint32_t a : {kMailCount, kMailFree, kMailbox + 0x8, kMailbox + 0xA}) {
		w.from(g, a, 2);
	}
	// `n` bytes of the letter's buffer (zeros past its end: only if the
	// buffer is shorter than its size says).
	auto kept = [&](size_t i, size_t n) {
		static const std::vector<uint8_t> kNone;
		const std::vector<uint8_t>& extras = i < save.mail_buffers.size() ? save.mail_buffers[i] : kNone;
		for (size_t k = 0; k < n; ++k) {
			w.u8(k < extras.size() ? extras[k] : 0);
		}
	};
	for (size_t i = 0; i < save.mail.size(); ++i) {
		const GameMemory<0x10>& m = save.mail[i];
		w.from(m, 0, 8);
		const uint8_t type = m.u8(4);
		if (notice_with_buffer(type)) {
			w.u32(pointer(true));
			w.from(m, 0xC, 4);
			kept(i, m.u32(0xC));
		} else if (notice_with_block(type)) {
			static const GameMemory<0x10> kEmpty{};
			const GameMemory<0x10>& record =
				i < save.mail_records.size() && save.mail_records[i] ? *save.mail_records[i] : kEmpty;
			w.from(record, 0, 0x10);
			kept(i, record.u32(0));
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
// b2, and the list always ends in 0. Both meant a big-endian u16 (`sar dx, 8`
// and the fourth byte as its low one); fixed, each node is written as it was
// read (b0 b1 b2 b3), and the list ends in 0 as the game's.
void write_event_nodes(Writer& w, const std::vector<uint8_t>& list) {
	for (size_t p = 0; p + 4 <= list.size(); p += 5) {
		w.u8(list[p]);
		w.u8(list[p + 1]);
		if (fix_game_bugs()) {
			w.u8(list[p + 2]);
			w.u8(list[p + 3]);
		} else {
			w.u8(list[p + 2] & 0x80 ? 0xFF : 0);
			w.u8(list[p + 2]);
		}
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

// GAME BUG: 0x611950 writes each entry of the table at the end of the
// battles as 8 bytes of memory, of which the reader (0x612A20: two u32, the
// low byte of the second) keeps 5: the other 3 are whatever the memory had
// (0xCD in 7.pat, 0 in the emulator). p3vita keeps the battles as they were
// read, so it writes what the file had. This finds where those 3 bytes are:
// u8 the battles; each one a u8 kind (not 0: a town battle, 0x5FBEA0, with
// 0x1D bytes before the rest) and what 0x604220 writes: 0x27 bytes (u8 n at
// +0x0, u16 a and b at +0x1 and +0x3, the heads of two lists of ships at
// +0x5 and +0x7), n entries of 4 bytes and 4 more for the first two, u16 m
// and m u32 of bits, 2 · (a + b) u16, 0x400 bytes, 0x2F bytes for each ship
// of the two lists (the u16 links are the first (a + b) u16) and 0x1F for
// each bit set; then u32 the slots of the table and, for each, u32 its
// index, u32 k and k entries of 8 bytes. Empty if the bytes don't end there.
std::vector<size_t> battle_padding(const std::vector<uint8_t>& b) {
	size_t p = 0;
	bool ok = true;
	auto need = [&](size_t n) {
		ok = ok && n <= b.size() - std::min(p, b.size());
		return ok;
	};
	auto u16 = [&](size_t at) { return uint16_t(b[at] | b[at + 1] << 8); };
	auto u32 = [&](size_t at) { return uint32_t(u16(at) | uint32_t(u16(at + 2)) << 16); };
	if (!need(1)) {
		return {};
	}
	const uint8_t battles = b[p++];
	for (uint8_t i = 0; i < battles; ++i) {
		if (!need(1)) {
			return {};
		}
		p += b[p] ? 1 + 0x1D : 1;
		if (!need(0x27)) {
			return {};
		}
		const size_t header = p;
		const uint8_t n = b[header];
		const size_t links = size_t(u16(header + 1)) + u16(header + 3);
		p += 0x27 + 4 * size_t(n) + 4 * std::min<size_t>(n, 2);
		if (!need(2)) {
			return {};
		}
		const size_t words = u16(p);
		p += 2;
		if (!need(4 * words)) {
			return {};
		}
		size_t bits = 0;
		for (size_t k = 0; k < 4 * words; ++k) {
			for (uint8_t v = b[p + k]; v; v &= uint8_t(v - 1)) {
				++bits;
			}
		}
		p += 4 * words;
		if (!need(4 * links)) {
			return {};
		}
		const size_t next = p;
		p += 4 * links + 0x400;
		for (size_t head : {header + 5, header + 7}) {
			size_t count = 0;
			for (uint16_t ship = u16(head); ship != 0xFFFF; ship = u16(next + 2 * size_t(ship))) {
				if (ship >= links || ++count > links) {
					return {};
				}
				p += 0x2F;
			}
		}
		p += 0x1F * bits;
	}
	std::vector<size_t> padding;
	if (!need(4)) {
		return {};
	}
	const uint32_t slots = u32(p);
	p += 4;
	for (uint32_t i = 0; i < slots; ++i) {
		if (!need(8)) {
			return {};
		}
		const uint32_t entries = u32(p + 4);
		p += 8;
		if (!need(8 * size_t(entries))) {
			return {};
		}
		for (uint32_t k = 0; k < entries; ++k, p += 8) {
			padding.push_back(p + 5);
		}
	}
	return p == b.size() ? padding : std::vector<size_t>();
}

// 0x511C60: the names. The 40 town names go as "A"; each list of names
// written by the players, its 20 slots (the game puts a 0 in the last byte
// of each slot of 24 bytes before copying it: 23 characters at most) and
// '#'; then the ship names the players wrote during the game (the names
// object's second list; none after a load, which the loader undoes, 0x5121C0
// at its end): u16 their number ([0x6DDB7A] − [0x6DDB78]) and, with a table
// of them (one was added), u16 their bytes and their texts each with its 0;
// without one, u16 0.
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
	const uint16_t count = uint16_t(save.globals.u16(0x6DDB7A) - save.globals.u16(0x6DDB78));
	w.u16(count);
	if (save.globals.u16(0x6DDB7C) == 0) {
		w.u16(0);  // No table yet ([+0xCC] null; its capacity [+0xDC] is 0 until the first name).
		return;
	}
	std::string text;
	for (size_t k = 0; k < count && k < save.player_ship_names.size(); ++k) {
		text += save.player_ship_names[k];
		text.push_back(0);
	}
	w.u16(uint16_t(text.size()));
	w.bytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

// 0x5125E0: the town names of the file, each up to a byte below 0x20.
std::vector<std::string> split_town_names(const std::vector<uint8_t>& data, size_t count = 40) {
	std::vector<std::string> names(count);
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

}  // namespace

void prepare_save(SaveGame& save) {
	for (uint32_t i = 0; i < save.globals.u16(global::kWorldMerchantCount); ++i) {
		tick::events_towns_detail::fn_4d6780(save, int32_t(i));
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
	// 0x611950: the battles, as they were read (fixed: the padding of the
	// table at their end at 0, battle_padding).
	if (save.opaque.battles.empty()) {
		battles_before_write(save.battles);  // The game changes a battle in memory as it writes it (0x5FBEA6).
		w.bytes(battle_bytes(save.battles));  // Loaded into SaveGame::battles (battle/battle_save.cpp).
	} else if (fix_game_bugs()) {
		std::vector<uint8_t> battles = save.opaque.battles;
		for (size_t p : battle_padding(battles)) {
			std::memset(battles.data() + p, 0, 3);
		}
		w.bytes(battles);
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

std::vector<uint8_t> statistics_file(SaveGame& save) {
	const std::vector<uint8_t> raw = write_statistics(save);
	return lz_compress(raw.data(), raw.size());
}

bool load_statistics_file(const std::vector<uint8_t>* pst, SaveGame& save) {
	if (!pst) {
		clear_statistics(save);
		return true;
	}
	std::vector<uint8_t> raw;
	if (!lz_decompress_file(pst->data(), pst->size(), raw)) {
		clear_statistics(save);
		return false;
	}
	return read_statistics(raw.data(), raw.size(), save);
}

// The game's town names table (0x6DDA00) in the language of the archive: the
// Spanish one when p2arch0_spa.cpr is mounted (as the Spanish game reads its
// own), else the English one.
void load_save_scripts(const CprArchive& archive, SaveGame& save) {
	for (const char* path : {"scripts/NamenStaedte_spa.txt", "scripts/NamenStaedte_eng.txt", "scripts/NamenStaedte.txt"}) {
		if (const CprEntry* entry = archive.find(path)) {
			std::vector<uint8_t> data;
			if (archive.read(*entry, data)) {
				// 40 names, 40 guilds, 40 taverns (0x512730).
				std::vector<std::string> lines = split_town_names(data, 120);
				save.opaque.town_names.assign(lines.begin(), lines.begin() + 40);
				save.opaque.town_name_table = save.opaque.town_names;
				save.opaque.town_taverns.assign(lines.begin() + 80, lines.end());
				return;
			}
		}
	}
}

}  // namespace p3::data
