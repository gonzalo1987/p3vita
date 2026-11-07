// The battles in the savegame (piece 5 of docs/cloud/battles.md;
// docs/cloud/battles-2.md §5): read as the game's 0x611AE0 (with method +0x10
// of a sea battle, 0x604600, and of a scene's ship, 0x61A880 → 0x6263D0) and
// written as 0x611950 (method +0xC, 0x604220; 0x61A790 → 0x626310).

#include <cstring>
#include <utility>

#include "p3data/battle/battle_math.hpp"
#include "p3data/sailing.hpp"

namespace p3::data {

// The bytes of the section, read in order (not ok once past the end).
struct BattleReader {
	const std::vector<uint8_t>& b;
	size_t pos = 0;
	bool ok = true;
	uint32_t get(size_t n) {
		if (pos + n > b.size()) {
			ok = false;
			pos = b.size();
			return 0;
		}
		uint32_t v = 0;
		std::memcpy(&v, b.data() + pos, n);
		pos += n;
		return v;
	}
	uint8_t u8() { return uint8_t(get(1)); }
	uint16_t u16() { return uint16_t(get(2)); }
	uint32_t u32() { return get(4); }
};

namespace {

using In = BattleReader;

struct Out {
	std::vector<uint8_t> b;
	void put(uint32_t v, size_t n) {
		const size_t at = b.size();
		b.resize(at + n);
		std::memcpy(b.data() + at, &v, n);
	}
	void u8(uint32_t v) { put(v, 1); }
	void u16(uint32_t v) { put(v, 2); }
	void u32(uint32_t v) { put(v, 4); }
};

constexpr uint32_t kSeaBattleSize = 0x1E98;
constexpr uint32_t kTownBattleSize = 0x2F24;
constexpr uint32_t kSceneShipSize = 0x164;
constexpr uint32_t kTownShipSize = 0x1168;
// The scale of the rocks' and islets' places when read from the save
// (0x604600 multiplies by 17; 0x60F479, placing new ones, by half the town
// view's tile width, also 17: see fn_60f479 in battle_setup.cpp).
constexpr uint32_t kSavedRockScale = 17;

// The fields of a scene's ship in the save, in their order (0x61A880 and
// 0x6263D0 on its [+0x4]): offset and size.
constexpr std::pair<size_t, size_t> kShipFields[] = {{0x128, 2}, {0x12A, 1}, {0x130, 1}, {0x131, 1}, {0x132, 1},
	{0x13A, 2}, {0x13D, 1}, {0x13E, 1}, {0x140, 2}, {0x142, 1}, {0x144, 4}, {0x162, 1}, {0x4, 4}, {0x8, 2}, {0xA, 2},
	{0xC, 2}, {0xE, 2}, {0x10, 1}, {0x11, 1}, {0x14, 4}, {0x18, 4}, {0x1C, 4}, {0x24, 2}, {0x26, 1}};
// The battle's own fields at the start of its record (0x604600, 0x604220).
constexpr std::pair<size_t, size_t> kBattleFields[] = {{0x558, 1}, {0x658, 2}, {0x65A, 2}, {0x10, 2}, {0x65C, 2},
	{0x65E, 2}, {0x660, 2}, {0xAD4, 2}, {0xAD6, 2}, {0xAD8, 2}, {0xADA, 2}, {0xADC, 4}, {0x12E8, 1}, {0x130C, 2},
	{0x130E, 1}, {0x5C, 1}, {0x670, 1}, {0x671, 1}, {0x1E90, 1}, {0x1E91, 1}, {0xACA, 1}, {0xACB, 1}, {0x1E94, 2},
	{0xAD2, 1}};

}  // namespace

// 0x611AE0 (on the battles object; the save's section, its version) with
// 0x604600 and 0x61A880: SaveGame::opaque::battles into SaveGame::battles,
// after the rest of the load (in 0x549F40 it comes after the events; nothing
// read in between uses it). Each battle (u8 how many; each one a u8 kind: 0
// a sea battle, else a town battle) gets the slot of its order: a new sea
// battle (0x603A70; a town battle's 0x2F24 bytes with the vtable 0x67AAA0 and
// [+0x2F1C], [+0x2F20] 0, and first its town's fields, 0x5FBF30), its fields,
// its arrays ([+0x4] the scene's ships, [+0xA74] and [+0xA78]
// their items, [+0xC] next, [+0x8] previous; [+0x664] and [+0x668] the cannon
// balls), the sides, its islets and rocks as items of the scene (their place
// · 17), its scene's ships (0x61A4E0 with ship 0, then the saved fields; two
// items each; [+0x13C] whether its guns are better close), [+0x1E92] 1,
// [+0xAB0] 1, [+0xAB1] 2 for pirates against the Hanse fleet; then method
// +0x18 without the formation (0x608230, a town battle's 0x600660: the end
// if [+0x5C], which [+0x15C] 1 keeps from doing anything). Then the
// battles object: the count, [+0x3FC] 0xFF, [+0x42C] and [+0x3FD] 0, the
// table of ships that met (0x612A20), [+0xC44] the graphics clock
// *0x6DCCF8 (0 in the emulator), [+0x408] 0, [+0x3FE] 1.
//
// Not rewritten (none in the 12 saves): saved cannon balls ([+0x664] bits;
// 0x602A90, 0x602EF0, only with a human side, piece 7); with them the section
// stays as it was read (opaque) and the battles are not loaded. Saves older
// than version 0x5C neither (p3vita reads from 0x82).
void Sailing::load_battles() {
	std::vector<uint8_t>& bytes = save_.opaque.battles;
	if (bytes.empty() || save_.version <= 0x5C) {
		return;
	}
	In in{bytes};
	BattleGraph g;
	const BattleGraph kept = save_.battles;
	save_.battles = g;
	BattleGraph& b = save_.battles;
	const uint8_t n = in.u8();
	bool ok = true;
	for (uint8_t i = 0; i < n && ok && in.ok; ++i) {
		const bool town = in.u8() != 0;
		const size_t s = b.alloc(town ? kTownBattleSize : kSeaBattleSize);
		fn_603a70(s, i);
		if (town) {
			b.set<uint32_t>(s, 0, kTownBattleVtable);
			b.set<uint32_t>(s, 0x2F1C, 0);
			b.set<uint16_t>(s, 0x2F20, 0);
		}
		b.set<uint32_t>(0, 4 * size_t(i), BattleGraph::pointer(s));
		ok = town ? fn_5fbf30(s, in) : fn_604600(s, in);
		if (ok && town) {
			fn_600660(s, false);
		} else if (ok) {
			fn_608230(s, false);
		}
	}
	if (!ok || !in.ok) {
		save_.battles = kept;
		return;
	}
	b.set<uint8_t>(0, 0x404, n);
	b.set<uint8_t>(0, 0x3FC, 0xFF);
	b.set<uint8_t>(0, 0x42C, 0);
	b.set<uint8_t>(0, 0x3FD, 0);
	const uint32_t records = in.u32();
	for (uint32_t r = 0; r < records && in.ok; ++r) {
		const uint32_t i = in.u32();
		const uint32_t count = in.u32();
		for (uint32_t k = 0; k < count && in.ok; ++k) {
			const uint32_t j = in.u32();
			const uint32_t v = in.u32();
			fn_612a20(i, j, uint8_t(v));
		}
	}
	if (!in.ok || in.pos != bytes.size()) {
		save_.battles = kept;
		return;
	}
	b.set<uint32_t>(0, 0xC44, 0);  // *0x6DCCF8, the graphics clock: 0 here.
	b.set<uint32_t>(0, 0x408, 0);
	b.set<uint8_t>(0, 0x3FE, 1);
	bytes.clear();
}

// 0x5FBF30 (method +0x10 of a town battle; the section from its record, the
// version): the town ([+0x1E98]) and, for each of the 4 towers, its strength
// [+0x1EC4] (u32), its cell [+0x1EAE], [+0x1EB2] and [+0x1EBE], its state
// [+0x1E99] 0xA (a slot without a tower too) and [+0x2EF8], [+0x2F08] 0;
// [+0x2F1A] 1 (its first step makes the towers' places and items and its map
// of the water again, 0x5FD510) and [+0x2F18] 0; then a sea battle's record
// (0x604600).
bool Sailing::fn_5fbf30(size_t s, In& in) {
	BattleGraph& g = save_.battles;
	g.set<uint8_t>(s, 0x1E98, in.u8());
	g.set<uint8_t>(s, 0x2F1A, 1);
	for (size_t k = 0; k < 4; ++k) {
		g.set<uint32_t>(s, 0x1EC4 + 4 * k, in.u32());
		g.set<uint8_t>(s, 0x1EAE + k, in.u8());
		g.set<uint8_t>(s, 0x1EB2 + k, in.u8());
		g.set<uint8_t>(s, 0x1EBE + k, in.u8());
		g.set<uint8_t>(s, 0x1E99 + k, 0xA);
		g.set<uint32_t>(s, 0x2EF8 + 4 * k, 0);
		g.set<uint32_t>(s, 0x2F08 + 4 * k, 0);
	}
	g.set<uint16_t>(s, 0x2F18, 0);
	return fn_604600(s, in);
}

// 0x604600 (method +0x10 of a sea battle; the section from its record, the
// version, ≥ 0x82 here): see load_battles. False for what isn't rewritten.
// For a town battle (its method +0x28) the arrays are realloc'ed (the same
// sizes) and its scene's ships are 0x1168 bytes (vtable 0x67AAD0, [+0x1164]
// and [+0x1166] 0xFFFF, [+0x127] 0; 0x61A4E0 gets the place's low byte).
bool Sailing::fn_604600(size_t s, In& in) {
	BattleGraph& g = save_.battles;
	g.set<uint8_t>(s, 0x5C, 0);
	for (const auto& [at, size] : kBattleFields) {
		const uint32_t v = in.get(size);
		if (size == 1) {
			g.set<uint8_t>(s, at, uint8_t(v));
		} else if (size == 2) {
			g.set<uint16_t>(s, at, uint16_t(v));
		} else {
			g.set<uint32_t>(s, at, v);
		}
		if (at == 0x660) {
			g.set<uint16_t>(s, 0x12, 0xFFFF);  // (the game sets it before [+0x65E])
		}
		if (at == 0x130E) {
			for (size_t k = 0; k < g.get<uint8_t>(s, 0x558); ++k) {
				g.set<uint16_t>(s, 0x15C + 2 * k, 0);
			}
		}
		if (at == 0x5C && v != 0) {
			g.set<uint16_t>(s, 0x15C, 1);
		}
	}
	const uint16_t places = uint16_t(g.get<uint16_t>(s, 0x65A) + g.get<uint16_t>(s, 0x658));
	g.set<uint16_t>(s, 0x14, places);
	for (const auto& [at, size] :
		{std::pair<size_t, size_t>{0x4, 4}, {0xA74, 2}, {0xA78, 2}, {0xC, 2}, {0x8, 2}}) {
		g.set_ptr(s, at, int(g.alloc(size_t(places) * size)));
	}
	g.set<uint8_t>(s, 0xAC8, 1);
	for (size_t i = 0; i < g.get<uint8_t>(s, 0x558); ++i) {
		g.set<uint16_t>(s, 0x35A + 2 * i, in.u16());
		g.set<uint8_t>(s, 0x5D + i, in.u8());
		g.set<uint8_t>(s, 0x559 + i, in.u8());
		if (i < 2) {
			g.set<uint32_t>(s, 0xA84 + 4 * i, in.u32());
		}
		if (g.get<uint8_t>(s, 0x5D + i) == 0) {
			g.set<uint8_t>(s, 0xAC8, 0);
		}
	}
	const uint16_t words = in.u16();
	g.set<uint16_t>(s, 0xACE, words);
	g.set<uint16_t>(s, 0xACC, uint16_t(words << 5));
	const size_t bits = g.realloc_at(s, 0x664, size_t(words) * 4);
	g.realloc_at(s, 0x668, size_t(uint16_t(words << 5)) * 4);
	for (size_t k = 0; k < words; ++k) {
		const uint32_t w = in.u32();
		g.set<uint32_t>(bits, 4 * k, w);
		if (w != 0) {
			return false;  // Saved cannon balls: not rewritten.
		}
	}
	const size_t next = size_t(g.ptr(s, 0xC)), prev = size_t(g.ptr(s, 0x8));
	for (size_t k = 0; k < places; ++k) {
		g.set<uint16_t>(next, 2 * k, in.u16());
	}
	for (size_t k = 0; k < places; ++k) {
		g.set<uint16_t>(prev, 2 * k, in.u16());
	}
	// The islets ([+0x672]: items 3 and 5) and the rocks ([+0x872]: 2 and 4).
	for (const auto& [base, kind] : {std::pair<size_t, uint8_t>{0x672, 3}, {0x872, 2}}) {
		for (int k = 0; k < 0x40; ++k) {
			for (size_t f = 0; f < 4; ++f) {
				g.set<uint16_t>(s, base + 8 * size_t(k) + 2 * f, in.u16());
			}
			if (g.get<uint16_t>(s, base + 8 * size_t(k)) == 0xFFFF) {
				continue;
			}
			const uint32_t x = g.get<uint16_t>(s, base + 8 * size_t(k));
			const uint32_t y = g.get<uint16_t>(s, base + 8 * size_t(k) + 2);
			const uint32_t w = g.get<uint16_t>(s, base + 8 * size_t(k) + 4);
			const uint32_t h = g.get<uint16_t>(s, base + 8 * size_t(k) + 6);
			const uint32_t c = 2 * x + (y & 1);
			const uint32_t f = kSavedRockScale;
			const uint16_t item = fn_619450(s, kind, uint8_t(k), uint16_t((c - w + 1) * f),
				uint16_t((y - w - h + 1) * f), uint16_t((c + h + 1) * f), uint16_t((y + 1) * f));
			const size_t xs = size_t(g.ptr(s, 0x28)), ys = size_t(g.ptr(s, 0x38));
			const uint16_t x1 = g.get<uint16_t>(xs, 4 * size_t(item)), x2 = g.get<uint16_t>(xs, 4 * size_t(item) + 2);
			const uint16_t y1 = g.get<uint16_t>(ys, 4 * size_t(item)), y2 = g.get<uint16_t>(ys, 4 * size_t(item) + 2);
			fn_619450(s, uint8_t(kind + 2), uint8_t(k), uint16_t(x1 - 200), uint16_t(y1 - 200), uint16_t(x2 + 200),
				uint16_t(y2 + 200));
		}
	}
	const bool town = g.get<uint32_t>(s, 0) == kTownBattleVtable;
	for (const size_t head : {size_t(0x10), size_t(0x65C)}) {
		for (uint16_t k = g.get<uint16_t>(s, head); k != 0xFFFF; k = g.get<uint16_t>(next, 2 * size_t(k))) {
			const size_t e = g.alloc(town ? kTownShipSize : kSceneShipSize);
			fn_61a4e0(e, 0, s, town ? uint16_t(k & 0xFF) : k, 0);
			if (town) {
				g.set<uint32_t>(e, 0, kTownShipVtable);
				g.set<uint16_t>(e, 0x1164, 0xFFFF);
				g.set<uint16_t>(e, 0x1166, 0xFFFF);
				g.set<uint8_t>(e, 0x127, 0);
			}
			g.set_ptr(size_t(g.ptr(s, 0x4)), 4 * size_t(k), int(e));
			// 0x61A880 and 0x6263D0.
			g.set<uint32_t>(e, 0x12C, BattleGraph::pointer(s));
			for (const auto& [at, size] : kShipFields) {
				const uint32_t v = in.get(size);
				if (size == 1) {
					g.set<uint8_t>(e, at, uint8_t(v));
				} else if (size == 2) {
					g.set<uint16_t>(e, at, uint16_t(v));
				} else {
					g.set<uint32_t>(e, at, v);
				}
			}
			g.set<uint8_t>(e, 0x161, 0);
			g.set<uint8_t>(e, 0x148, g.get<uint8_t>(e, 0x26));
			g.set<uint16_t>(size_t(g.ptr(s, 0xA74)), 2 * size_t(k), fn_619450(s, 0, uint8_t(k), 0, 0, 0, 0));
			g.set<uint16_t>(size_t(g.ptr(s, 0xA78)), 2 * size_t(k), fn_619450(s, 6, uint8_t(k), 0, 0, 0, 0));
			// [+0x13C]: guns that lose more with the distance than they give.
			const ShipMemory& sh = scene_ship(e);
			int32_t all = 0, far = 0;
			for (size_t slot = 0; slot < 0x18; ++slot) {
				const uint8_t gun = sh.u8(0x13C + slot);
				if (gun >= 6) {
					continue;
				}
				const int32_t a = int32_t(battle_math::kGunA[gun]) * battle_math::kGunB[gun];
				all += a;
				far += a + (-480 * a) / (6 * int32_t(battle_math::kGunTime[gun]));
			}
			g.set<uint8_t>(e, 0x13C, 301 * all > 605 * far ? 1 : 0);
		}
	}
	for (size_t k = 0; k < g.get<uint16_t>(s, 0xACC); ++k) {
		g.set<uint32_t>(size_t(g.ptr(s, 0x668)), 4 * k, 0);
	}
	g.set<uint8_t>(s, 0x1E92, 1);
	g.set<uint8_t>(s, 0xAB0, 1);
	g.set<uint8_t>(s, 0xAB1,
		g.get<uint16_t>(s, 0x35C) == 0xFF && g.get<uint16_t>(s, 0x35A) == 0xFE ? 2 : 1);
	return true;
}

// 0x611950 (on the battles object) with 0x604220 and 0x61A790: the section
// of the save from SaveGame::battles. Its sea battles in slot order (u8
// how many, u8 kind 0 and each record: the fields of load_battles, the
// sides, the cannon balls' bits, next and previous, the islets and rocks,
// the scene's ships of side 0's list then side 1's), then the records of
// the table of ships that met that have entries (u32 how many; u32 the
// ship, u32 count and the entries of 8 bytes as they are in memory).
std::vector<uint8_t> battle_bytes(const BattleGraph& g) {
	Out out;
	uint8_t n = 0;
	for (size_t k = 0; k < BattleGraph::kSlots; ++k) {
		if (g.get<uint32_t>(0, 4 * k) != 0) {
			++n;
		}
	}
	out.u8(n);
	for (size_t k = 0; k < BattleGraph::kSlots; ++k) {
		const int s = g.target(g.get<uint32_t>(0, 4 * k));
		if (g.get<uint32_t>(0, 4 * k) == 0) {
			continue;
		}
		// Method +0x28: 0 for a sea battle, 1 for a town battle, whose record
		// (method +0xC, 0x5FBEA0) starts with its town and, for each of the 4
		// towers, its strength [+0x1EC4] and [+0x1EAE], [+0x1EB2], [+0x1EBE].
		// (With its countdown [+0x2F18] running, a human side's, the game
		// first sets [+0x5E] 1: piece 7.)
		const bool town = s >= 0 && g.get<uint32_t>(size_t(s), 0) == Sailing::kTownBattleVtable;
		out.u8(town ? 1 : 0);
		if (s < 0) {
			continue;
		}
		const size_t b = size_t(s);
		if (town) {
			out.u8(g.get<uint8_t>(b, 0x1E98));
			for (size_t t = 0; t < 4; ++t) {
				out.u32(g.get<uint32_t>(b, 0x1EC4 + 4 * t));
				out.u8(g.get<uint8_t>(b, 0x1EAE + t));
				out.u8(g.get<uint8_t>(b, 0x1EB2 + t));
				out.u8(g.get<uint8_t>(b, 0x1EBE + t));
			}
		}
		for (const auto& [at, size] : kBattleFields) {
			out.put(size == 1 ? g.get<uint8_t>(b, at) : size == 2 ? g.get<uint16_t>(b, at) : g.get<uint32_t>(b, at),
				size);
		}
		for (size_t i = 0; i < g.get<uint8_t>(b, 0x558); ++i) {
			out.u16(g.get<uint16_t>(b, 0x35A + 2 * i));
			out.u8(g.get<uint8_t>(b, 0x5D + i));
			out.u8(g.get<uint8_t>(b, 0x559 + i));
			if (i < 2) {
				out.u32(g.get<uint32_t>(b, 0xA84 + 4 * i));
			}
		}
		const uint16_t words = g.get<uint16_t>(b, 0xACE);
		out.u16(words);
		const int bits = g.ptr(b, 0x664);
		for (size_t k2 = 0; k2 < words; ++k2) {
			out.u32(bits >= 0 ? g.get<uint32_t>(size_t(bits), 4 * k2) : 0);
		}
		const uint16_t places = g.get<uint16_t>(b, 0x14);
		for (const size_t arr : {size_t(0xC), size_t(0x8)}) {
			const int a = g.ptr(b, arr);
			for (size_t k2 = 0; k2 < places; ++k2) {
				out.u16(a >= 0 ? g.get<uint16_t>(size_t(a), 2 * k2) : 0);
			}
		}
		for (const size_t base : {size_t(0x672), size_t(0x872)}) {
			for (size_t k2 = 0; k2 < 0x40 * 4; ++k2) {
				out.u16(g.get<uint16_t>(b, base + 2 * k2));
			}
		}
		const int ships = g.ptr(b, 0x4), next = g.ptr(b, 0xC);
		for (const size_t head : {size_t(0x10), size_t(0x65C)}) {
			for (uint16_t p = g.get<uint16_t>(b, head); p != 0xFFFF && next >= 0;
				 p = g.get<uint16_t>(size_t(next), 2 * size_t(p))) {
				const int e = ships >= 0 ? g.ptr(size_t(ships), 4 * size_t(p)) : -1;
				for (const auto& [at, size] : kShipFields) {
					const uint32_t v = e < 0 ? 0
						: size == 1      ? g.get<uint8_t>(size_t(e), at)
						: size == 2      ? g.get<uint16_t>(size_t(e), at)
										 : g.get<uint32_t>(size_t(e), at);
					out.put(v, size);
				}
			}
		}
		// The cannon balls (0x602E20 for each bit of [+0x664]): none in p3vita's battles.
	}
	uint32_t records = 0;
	const int table = g.ptr(0, 0xC30);
	const uint32_t slots = g.get<uint32_t>(0, 0xC34);
	for (uint32_t i = 0; i < slots && table >= 0; ++i) {
		if (g.get<uint32_t>(size_t(table), 12 * size_t(i) + 8) != 0) {
			++records;
		}
	}
	out.u32(records);
	for (uint32_t i = 0; i < slots && table >= 0; ++i) {
		const uint32_t count = g.get<uint32_t>(size_t(table), 12 * size_t(i) + 8);
		if (count == 0) {
			continue;
		}
		out.u32(i);
		out.u32(count);
		const int list = g.ptr(size_t(table), 12 * size_t(i));
		for (size_t k = 0; k < count; ++k) {
			for (size_t q = 0; q < 8; ++q) {
				out.u8(list >= 0 ? g.get<uint8_t>(size_t(list), 8 * k + q) : 0);
			}
		}
	}
	return out.b;
}

}  // namespace p3::data
