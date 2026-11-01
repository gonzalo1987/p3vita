// The setup of a sea battle (part of 0x605420 and what it calls; piece 3 of
// docs/cloud/battles.md, docs/cloud/battles-2.md §4): the scene's items
// (0x619450, 0x61A1D0), the scene's ships (0x61A4E0), their strength
// (0x62125B, 0x61A750) and the Hanse fleet's convoys (0x503AC0).

#include <array>

#include "p3data/sailing.hpp"

namespace p3::data {

namespace {

// 0x672CD0, 0x672CC0, 0x672CE4: per kind of gun (< 6), two factors and a time.
constexpr uint8_t kGunA[6] = {60, 80, 60, 80, 90, 90};
constexpr uint8_t kGunB[6] = {32, 32, 77, 77, 96, 58};
constexpr uint16_t kGunTime[6] = {480, 160, 480, 160, 120, 120};
constexpr uint32_t kSceneShipVtable = 0x67B0D4;

}  // namespace

// 0x61A1D0 (on the scene): a free item. When none is free (and there are
// fewer than 0xFFBF pairs) every array grows by 0x20 items (realloc, in the
// game's order), a new page of each of the three page arrays [+0x48],
// [+0x4C], [+0x50], and the new items are linked in the free list [+0x24].
uint16_t Sailing::fn_61a1d0(size_t scene) {
	BattleGraph& g = save_.battles;
	auto u16 = [&](size_t o) { return g.get<uint16_t>(scene, o); };
	if (u16(0x20) != 0xFFFF || u16(0x58) >= 0xFFBF) {
		return u16(0x20);
	}
	g.set<uint16_t>(scene, 0x20, u16(0x56));
	g.set<uint16_t>(scene, 0x56, uint16_t(u16(0x56) + 0x20));
	g.set<uint16_t>(scene, 0x5A, uint16_t(u16(0x5A) + 1));
	g.set<uint16_t>(scene, 0x58, uint16_t(u16(0x58) + 0x40));
	const size_t cap = u16(0x56);
	const size_t pairs = u16(0x58);
	const size_t pages = u16(0x5A);
	g.realloc_at(scene, 0x18, cap);
	g.realloc_at(scene, 0x1C, 2 * cap);
	g.realloc_at(scene, 0x24, 2 * cap);
	for (size_t o : {0x28, 0x2C, 0x30, 0x38, 0x3C, 0x40}) {
		g.realloc_at(scene, o, 2 * pairs);
	}
	for (size_t j = 0; j + 1 < pages; ++j) {
		for (size_t o : {0x48, 0x4C, 0x50}) {
			const int list = g.ptr(scene, o);
			const size_t page = g.realloc_at(size_t(list), 4 * j, 4 * cap);
			for (size_t e = u16(0x20); e < cap; ++e) {
				g.set<uint32_t>(page, 4 * e, 0);
			}
		}
	}
	for (size_t o : {0x48, 0x4C, 0x50}) {
		g.realloc_at(scene, o, 4 * pages);
	}
	for (size_t o : {0x48, 0x4C, 0x50}) {
		const size_t list = size_t(g.ptr(scene, o));
		const size_t page = g.alloc(4 * cap);  // realloc(NULL, ...)
		g.set_ptr(list, 4 * (pages - 1), int(page));
	}
	const size_t free_list = size_t(g.ptr(scene, 0x24));
	for (size_t e = cap - 0x20; e + 1 < cap; ++e) {
		g.set<uint16_t>(free_list, 2 * e, uint16_t(e + 1));
	}
	g.set<uint16_t>(free_list, 2 * (cap - 1), 0xFFFF);
	return u16(0x20);
}

// 0x619450 (on the scene; a kind, a byte, four u16): a new item (0x61A1D0),
// taken out of the free list, with its kind [+0x18], its byte [+0x1C], the
// pairs [+0x28] (a2, a4) and [+0x38] (a3, a5), at the end of the two lists
// of pairs ([+0x2C]/[+0x30] with the tail [+0x34], [+0x3C]/[+0x40] with
// [+0x44]); [+0x54] the items used. Returns the item.
uint16_t Sailing::fn_619450(size_t scene, uint8_t a0, uint8_t a1, uint16_t a2, uint16_t a3, uint16_t a4, uint16_t a5) {
	BattleGraph& g = save_.battles;
	const uint16_t i = fn_61a1d0(scene);
	auto arr = [&](size_t o) { return size_t(g.ptr(scene, o)); };
	g.set<uint16_t>(scene, 0x20, g.get<uint16_t>(arr(0x24), 2 * size_t(g.get<uint16_t>(scene, 0x20))));
	g.set<uint8_t>(arr(0x18), i, a0);
	g.set<uint16_t>(arr(0x1C), 2 * size_t(i), a1);
	g.set<uint16_t>(arr(0x28), 4 * size_t(i), a2);
	g.set<uint16_t>(arr(0x28), 4 * size_t(i) + 2, a4);
	g.set<uint16_t>(arr(0x38), 4 * size_t(i), a3);
	g.set<uint16_t>(arr(0x38), 4 * size_t(i) + 2, a5);
	const uint16_t two = uint16_t(2 * i);
	for (const auto& [first, second, tail] : {std::array<size_t, 3>{0x2C, 0x30, 0x34}, std::array<size_t, 3>{0x3C, 0x40, 0x44}}) {
		const uint16_t last = g.get<uint16_t>(scene, tail);
		g.set<uint16_t>(arr(first), 4 * size_t(i) + 2, last);
		g.set<uint16_t>(arr(second), 4 * size_t(i) + 2, two);
		if (last != 0xFFFF) {
			g.set<uint16_t>(arr(second), 2 * size_t(last), uint16_t(two + 1));
		}
		g.set<uint16_t>(arr(first), 4 * size_t(i), uint16_t(two + 1));
		g.set<uint16_t>(arr(second), 4 * size_t(i), 0xFFFF);
		g.set<uint16_t>(scene, tail, two);
	}
	if (i >= g.get<uint16_t>(scene, 0x54)) {
		g.set<uint16_t>(scene, 0x54, uint16_t(i + 1));
	}
	return i;
}

// 0x61A4E0 (a scene's ship, 0x164 bytes: the ship, its battle, its place in
// it, its side): the vtable 0x67B0D4, the ship's index [+0x4], the battle
// [+0x12C], the place [+0x138], the side [+0x13D], [+0x142], its sailors
// [+0x140] and hull [+0x144], and [+0x13C] 1 when its guns (the 0x18 bytes of
// [+0x13C] of the ship, the kinds under 6) are mostly of the slow kinds.
void Sailing::fn_61a4e0(size_t entry, uint16_t ship, size_t battle, uint16_t index, uint8_t side) {
	BattleGraph& g = save_.battles;
	auto u8 = [&](size_t o, uint8_t v) { g.set<uint8_t>(entry, o, v); };
	auto u16 = [&](size_t o, uint16_t v) { g.set<uint16_t>(entry, o, v); };
	auto u32 = [&](size_t o, uint32_t v) { g.set<uint32_t>(entry, o, v); };
	const ShipMemory& s = save_.ships[ship].memory;
	u8(0x10, 0x80);
	u8(0x127, 0);
	g.set_ptr(entry, 0x12C, int(battle));
	u16(0x138, index);
	u8(0x26, 4);
	u8(0x148, 4);
	u32(0x4, ship);
	u32(0x0, kSceneShipVtable);
	u16(0x128, 0xFFFF);
	u8(0x12A, 0);
	u8(0x130, 0);
	u8(0x131, 0);
	u8(0x132, 0);
	u32(0x134, 0);
	u32(0x1C, 0);
	u8(0x149, 0);
	u16(0x13A, 0);
	u8(0x13D, side);
	u8(0x142, side);
	u8(0x13E, 0);
	u16(0x140, s.u16(0x40));
	u32(0x144, s.u32(0x18));
	u8(0x14A, 1);
	u16(0x8, 0);
	u16(0xC, 0);
	u16(0xA, 0);
	u16(0xE, 0);
	u8(0x11, 0);
	u8(0x10, 0x80);
	u8(0x127, 1);
	u8(0x162, 0);
	for (size_t o = 0x27; o < 0x127; ++o) {
		u8(o, 0);
	}
	int32_t all = 0, quick = 0;
	for (size_t k = 0; k < 0x18; ++k) {
		const uint8_t t = s.u8(0x13C + k);
		if (t < 6) {
			const int32_t a = int32_t(kGunA[t]) * kGunB[t];
			all += a;
			quick += a - (480 * a) / (6 * int32_t(kGunTime[t]));
		}
	}
	u8(0x13C, 301 * all > 605 * quick ? 1 : 0);
	u32(0x14C, 0xFFFFFFFF);
	u32(0x150, 0);
	u8(0x160, 0);
	u32(0x15C, 0);
	u32(0x158, 0);
	u8(0x154, 0xFF);
	u8(0x161, 0xC);
}

// 0x62125B (on a scene's ship): its strength, arms · hull / 1024, more with
// a captain ([+0xB] / 850), 5 % less when the crew is tired ([+0x3F] < 1),
// up to 10 % more when rested (2 < [+0x3F], at most 4).
int32_t Sailing::fn_62125b(size_t entry) const {
	const BattleGraph& g = save_.battles;
	const ShipMemory& s = save_.ships[g.get<uint32_t>(entry, 0x4)].memory;
	int32_t v = int32_t(uint32_t(s.s32(0x120)) * uint32_t(s.s32(0x18))) >> 10;
	if (s.u16(0x42) < save_.captain_count()) {
		v += int32_t(uint32_t(save_.captains[s.u16(0x42)].u8(0xB)) * uint32_t(v)) / 850;
	}
	const int8_t rest = s.s8(0x3F);
	if (rest < 1) {
		v += v / -20;
	} else if (rest > 2) {
		v += int32_t(uint32_t(std::min<int8_t>(rest, 4) - 2) * uint32_t(v)) / 20;
	}
	return v;
}

// 0x61A750 (on a scene's ship, another): whether it is stronger.
bool Sailing::fn_61a750(size_t entry, size_t other) const {
	return fn_62125b(entry) > fn_62125b(other);
}

// 0x503AC0 (on a convoy, its index): whether the convoy is the Hanse fleet of
// its event 0x34 ([+0x2] the event, whose [+0x16] is the convoy).
bool Sailing::fn_503ac0(uint16_t convoy) const {
	const uint16_t e = save_.convoys[convoy].u16(0x2);
	if (e >= save_.globals.u16(0x6DD748) || e >= save_.events.size()) {
		return false;
	}
	return save_.events[e].u16(0x6) == 0x34 && save_.events[e].u16(0x16) == convoy;
}

}  // namespace p3::data
