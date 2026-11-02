// The battles' clock and operation 0x94 (piece 3 of docs/cloud/battles.md;
// docs/cloud/battles-2.md §3). With Sailing::set_battles(true), as the oracle
// with --battles.

#include <cstring>

#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kNetwork = 0x6DF31C;
constexpr uint32_t kGameMode = 0x6DFC1C;
constexpr size_t kAutomatic = 0xAC8;  // u8 of a battle: both sides are the AI's
constexpr size_t kRunning = 0x15C;  // u16 of a battle: 0xFFFF when it doesn't run

void queue_time_mode(SaveGame& save, uint32_t mode) {
	uint8_t op[0x14];
	const uint32_t fields[5] = {0xC8, 0xFFFFFFFF, 0xFFFFFFFF, mode, 0xFFFFFFFF};
	std::memcpy(op, fields, sizeof(op));
	tick::enqueue_operation(save, op, sizeof(op));  // 0x54AA70
}

}  // namespace

BattleGraph BattleGraph::normalized() const {
	const std::vector<bool> pages = bit_pages();
	std::vector<int> order{0};
	std::vector<int> index(blocks.size(), -1);
	index[0] = 0;
	for (size_t i = 0; i < order.size(); ++i) {
		const std::vector<uint8_t>& data = blocks[size_t(order[i])];
		if (pages[size_t(order[i])]) {
			continue;
		}
		for (size_t o = 0; o + 4 <= data.size(); o += 4) {
			uint32_t w;
			std::memcpy(&w, data.data() + o, 4);
			const int b = number_field(data, o) ? -1 : target(w);
			if (b >= 0 && index[size_t(b)] < 0) {
				index[size_t(b)] = int(order.size());
				order.push_back(b);
			}
		}
	}
	BattleGraph out;
	out.blocks.clear();
	for (const int b : order) {
		std::vector<uint8_t> data = blocks[size_t(b)];
		for (size_t o = 0; o + 4 <= data.size() && !pages[size_t(b)]; o += 4) {
			uint32_t w;
			std::memcpy(&w, data.data() + o, 4);
			if ((w & 0xF0000000) == kPointer && !number_field(data, o)) {
				const size_t from = (w >> 16) & 0xFFF;
				if (from < blocks.size() && index[from] >= 0) {
					w = pointer(size_t(index[from]), w & 0xFFFF);
					std::memcpy(data.data() + o, &w, 4);
				}
			}
		}
		out.blocks.push_back(std::move(data));
	}
	return out;
}

// 0x611CD0 (on the battles): a new sea battle in the first free slot (0x1E98
// bytes, 0x603A70); the count [+0x404] grows to cover it. The slot, or 0xFF
// if none is free.
uint8_t Sailing::fn_611cd0() {
	BattleGraph& b = save_.battles;
	for (size_t k = 0; k < BattleGraph::kSlots; ++k) {
		if (b.get<uint32_t>(0, 4 * k) == 0) {
			const size_t block = b.alloc(kSeaBattleSize);
			fn_603a70(block, uint8_t(k));
			b.set<uint32_t>(0, 4 * k, BattleGraph::pointer(block));
			if (k + 1 > b.count()) {
				b.set<uint8_t>(0, 0x404, uint8_t(k + 1));
			}
			return uint8_t(k);
		}
	}
	return 0xFF;
}

// 0x603A70 (the sea battle's constructor, its slot): the scene's (0x618F10),
// the vtable 0x67AD44, the slot's pointer, and its first fields.
void Sailing::fn_603a70(size_t block, uint8_t slot) {
	BattleGraph& b = save_.battles;
	fn_618f10(block);
	auto u8 = [&](size_t o, uint8_t v) { b.set<uint8_t>(block, o, v); };
	auto u16 = [&](size_t o, uint16_t v) { b.set<uint16_t>(block, o, v); };
	auto u32 = [&](size_t o, uint32_t v) { b.set<uint32_t>(block, o, v); };
	u32(0x0, kSeaBattleVtable);
	b.set<uint32_t>(0, 4 * size_t(slot), BattleGraph::pointer(block));
	u16(0x15C, 0xFFFF);
	u16(0x662, slot);
	u16(0x65C, 0xFFFF);
	u16(0xAC6, 0xFFFF);
	u16(0x130C, 0xFFFF);
	u16(0x65A, 0);
	u16(0x65E, 0);
	u16(0x660, 0);
	u32(0xA7C, 0x2200);
	u32(0xA80, 0x136C);
	u32(0xAE0, 0xFFFFFFFF);
	u8(0x558, 0);
	u8(0x12E0, 0);
	u8(0xA8C, 0);
	u32(0x12E4, 0);
	u8(0x12E8, 0);
	u8(0x130F, 0);
	u8(0xAC9, 0);
	u8(0xAC4, 0xFF);
	u8(0xACB, 0);
	u8(0xACA, 0);
	u16(0x1E94, 0x800);
	u8(0x12EA, 0);
	u8(0x12E9, 0);
	for (size_t o = 0x1B10; o < 0x1B10 + 0x380; o += 4) {
		u32(o, 0);
	}
	for (size_t i = 0; i < 0x40; ++i) {
		u16(0x672 + 8 * i, 0xFFFF);
		u16(0x872 + 8 * i, 0xFFFF);
	}
	u8(0x1E92, 0);
	u32(0x664, 0);
	u32(0x668, 0);
	u32(0x66C, 0);
	u32(0xA74, 0);
	u32(0xA78, 0);
	u16(0xACC, 0);
	u16(0xACE, 0);
	u16(0xAD0, 0);
}

// 0x618F10 (the scene's constructor): the vtable 0x67AF70 and its lists empty.
void Sailing::fn_618f10(size_t block) {
	BattleGraph& b = save_.battles;
	b.set<uint32_t>(block, 0x0, kSceneVtable);
	for (size_t o : {0x4, 0x8, 0xC, 0x18, 0x1C, 0x24, 0x28, 0x2C, 0x30, 0x38, 0x3C, 0x40, 0x48, 0x4C, 0x50}) {
		b.set<uint32_t>(block, o, 0);
	}
	for (size_t o : {0x10, 0x12, 0x20, 0x34, 0x44}) {
		b.set<uint16_t>(block, o, 0xFFFF);
	}
	for (size_t o : {0x14, 0x16, 0x54, 0x56, 0x58, 0x5A}) {
		b.set<uint16_t>(block, o, 0);
	}
}

// The clock rule (docs/oracle.md): after each tick with battles, operation
// 0x94 with kBattleSteps steps, after the tick's other operations.
void Sailing::step_battles() {
	if (battles_ && save_.battles.count() != 0) {
		fn_536bbf(kBattleSteps);
	}
}

// Operation 0x94 (0x536BBF): each battle that runs ([+0x15C] ≠ 0xFFFF) takes
// `steps` steps (method +0x14: 0x606CC0 for a sea battle, battle_step.cpp;
// 0x5FD510 for a town battle, not rewritten: none in the saves); then the
// time mode (0x612D20 in game mode 2, else 0x612C90) and [+0x3FE] = 1.
void Sailing::fn_536bbf(uint32_t steps) {
	BattleGraph& b = save_.battles;
	for (size_t k = 0; k < b.count(); ++k) {
		const int battle = b.battle(k);
		if (battle >= 0 && b.get<uint16_t>(size_t(battle), kRunning) != BattleGraph::kStopped &&
			b.get<uint32_t>(size_t(battle), 0) == kSeaBattleVtable) {
			fn_606cc0(size_t(battle), uint16_t(steps));
		}
	}
	if (save_.globals.u32(kGameMode) == 2) {
		fn_612d20(false);
	} else {
		fn_612c90(false);
	}
	b.set<uint8_t>(0, 0x3FE, 1);
}

// 0x612C90 (on the battles, network): unless in a network game (or with
// `network`), if a battle has a human side ([+0xAC8] = 0), operation 0xC8
// with 2 (the battle time mode).
void Sailing::fn_612c90(bool network) {
	const BattleGraph& b = save_.battles;
	if (!network && save_.globals.u32(kNetwork) != 0) {
		return;
	}
	bool human = false;
	for (size_t k = 0; k < b.count(); ++k) {
		const int battle = b.battle(k);
		if (battle >= 0 && b.get<uint8_t>(size_t(battle), kAutomatic) == 0) {
			human = true;
		}
	}
	if (human) {
		queue_time_mode(save_, 2);
	}
}

// 0x612D20 (on the battles, network): unless in a network game (or with
// `network`), if no battle has a human side, operation 0xC8 with 0 (back to
// the normal time).
void Sailing::fn_612d20(bool network) {
	const BattleGraph& b = save_.battles;
	if (!network && save_.globals.u32(kNetwork) != 0) {
		return;
	}
	for (size_t k = 0; k < b.count(); ++k) {
		const int battle = b.battle(k);
		if (battle >= 0 && b.get<uint8_t>(size_t(battle), kAutomatic) == 0) {
			return;
		}
	}
	queue_time_mode(save_, 0);
}

// Operation 0x96 (0x536C52, and 0x541350 for a direct call): {0x96, slot,
// merchant, automatic}, thiscall 0x60FEC9(merchant, automatic ≠ 0) on the
// battle in that slot (nothing if the slot is empty). The merchant's sides
// are handed to the computer (automatic: [+0x5D + side] = 1) or taken back
// to be directed by hand (0: for the local player, the battles object's
// [+0x3FC], the battle in the window, becomes this one). Each step of a
// battle where the local player has a human side and isn't the window's
// queues it with 1 (0x607011, fn_606cc0), so a battle the player doesn't
// direct is fought by the computer, as when the player answers «no» to the
// battle's question («Do you want to direct the ships by hand?»; p3vita has
// no battle window yet: part 7). With every human side the merchant's, the
// battle becomes automatic ([+0xAC8] 1, 0x612D20) or not ([+0xAC8] 0,
// 0x612C90).
void Sailing::fn_60fec9(size_t b, uint16_t merchant, bool automatic) {
	BattleGraph& g = save_.battles;
	auto u8 = [&](size_t o) { return g.get<uint8_t>(b, o); };
	auto u16 = [&](size_t o) { return g.get<uint16_t>(b, o); };
	const size_t sides = u8(0x558);
	if (save_.globals.u32(0x6DFC14) == merchant) {
		g.set<uint8_t>(b, 0x12E0, 0);
		if (!automatic) {
			g.set<uint8_t>(0, 0x3FC, u8(0x662));
		} else if (g.get<uint8_t>(0, 0x3FC) == u16(0x662)) {
			g.set<uint8_t>(0, 0x3FC, 0xFF);
		}
	}
	// Whether a side was counting down ([+0x15C] by side) and none is after.
	bool counted = false;
	for (size_t i = 0; i < sides; ++i) {
		if (u16(0x15C + 2 * i) > 0) {
			counted = true;
		}
	}
	for (size_t i = 0; i < sides; ++i) {
		if (u16(0x35A + 2 * i) == merchant) {
			g.set<uint8_t>(b, 0x5D + i, automatic ? 1 : 0);
			g.set<uint16_t>(b, 0x15C + 2 * i, 0);
		}
	}
	bool only_merchant = true;
	for (size_t i = 0; i < sides; ++i) {
		if (u8(0x5D + i) == 0 && u16(0x35A + 2 * i) != merchant) {
			only_merchant = false;
		}
	}
	if (only_merchant) {
		g.set<uint8_t>(b, kAutomatic, automatic ? 1 : 0);
		if (automatic) {
			fn_612d20(false);
		} else {
			fn_612c90(false);
		}
	}
	for (size_t i = 0; i < sides; ++i) {
		if (u16(0x15C + 2 * i) > 0) {
			counted = false;
		}
	}
	if (counted && g.get<uint32_t>(b, 0) == kSeaBattleVtable) {
		fn_608230(b, true);  // Method +0x18 (a town battle's, 0x600660, isn't rewritten: none in p3vita).
	}
	if (automatic) {
		return;
	}
	// By hand: the merchant's ships of both sides (the lists [+0x10] and
	// [+0x65C], next in [+0xC]) get [+0x24] = [+0x11] and no order ([+0x128]
	// 0xFFFF).
	const size_t next = size_t(g.ptr(b, 0xC));
	for (const size_t head : {size_t(0x10), size_t(0x65C)}) {
		for (uint16_t k = u16(head); k != 0xFFFF; k = g.get<uint16_t>(next, 2 * size_t(k))) {
			const size_t e = battle_entry(b, k);
			if (u16(0x35A + 2 * size_t(g.get<uint8_t>(e, 0x13D))) == merchant) {
				g.set<uint16_t>(e, 0x24, g.get<uint8_t>(e, 0x11));
				g.set<uint16_t>(e, 0x128, 0xFFFF);
			}
		}
	}
}

}  // namespace p3::data
