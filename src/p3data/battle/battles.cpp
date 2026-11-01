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

// The clock rule (docs/oracle.md): after each tick with battles, operation
// 0x94 with kBattleSteps steps, after the tick's other operations.
void Sailing::step_battles() {
	if (battles_ && save_.battles.count() != 0) {
		fn_536bbf(kBattleSteps);
	}
}

// Operation 0x94 (0x536BBF): each battle that runs ([+0x15C] ≠ 0xFFFF) takes
// `steps` steps (method +0x14: 0x606CC0 for a sea battle, 0x5FD510 for a town
// battle; not rewritten yet, part 4); then the time mode (0x612D20 in game
// mode 2, else 0x612C90) and [+0x3FE] = 1.
void Sailing::fn_536bbf(uint32_t steps) {
	BattleGraph& b = save_.battles;
	for (size_t k = 0; k < b.count(); ++k) {
		const int battle = b.battle(k);
		if (battle >= 0 && b.get<uint16_t>(size_t(battle), kRunning) != BattleGraph::kStopped) {
			(void)steps;  // Method +0x14: part 4.
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

}  // namespace p3::data
