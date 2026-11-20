// The missions' interpreter: 0x4ECF64 (docs/formats/missions.md). Each section of a program is one instruction: its
// first byte the opcode (dispatched by the byte table 0x4F3054 into the jump table 0x4F2E34: 138 with a handler,
// 0x01..0x74 but 0x30 and 0x3D, and 0xE4..0xFB; the rest end the script), then its operands, mostly variables'
// numbers (a byte each) and constants.
//
// Rewritten so far: the loop and its waits (0x00, 0xFD, 0xFE, 0xFF); the variables' and numbers' 0x04..0x07, 0x09,
// 0x0A, 0x21; the arithmetic 0x0B..0x0E; the comparisons 0x17..0x1C; the search of a ship 0x26 / 0x27. The handlers come in parts (roadmap, section 3; the order by what the saves run:
// re/emu/sesion47/opcount.sh); until then an instruction not rewritten ends the script as an unknown one does, and is
// counted (unwritten_mission_ops).

#include "p3data/mission_script.hpp"

#include <cstring>

#include "p3data/game_memory.hpp"
#include "p3data/missions.hpp"
#include "p3data/savegame.hpp"

namespace p3::data::missions_detail {

namespace {

uint32_t g_unwritten[256];

// The instructions with a handler of their own in the game (0x4F3054 → 0x4F2E34 not its default 0x4F2D44).
bool has_handler(uint8_t op) {
	return (op >= 0x01 && op <= 0x74 && op != 0x30 && op != 0x3D) || (op >= 0xE4 && op <= 0xFB);
}

// A running script: its event, program and variables (the game's esi, [esi+0x8], [esi+0xC]).
struct Run {
	SaveGame& save;
	uint16_t event;
	GameMemory<0x18>& e;
	const std::vector<uint8_t>& p;
	std::vector<uint32_t>& vars;

	// The current section's offset ([+0x14]) and its bytes.
	uint32_t at() const {
		const size_t o = 4 + 4 * size_t(e.u16(0x14));
		uint32_t v = 0;
		if (o + 4 <= p.size()) {
			std::memcpy(&v, p.data() + o, 4);
		}
		return v;
	}
	uint8_t b(uint32_t k) const {
		const size_t o = size_t(at()) + k;
		return o < p.size() ? p[o] : 0;
	}
	uint16_t w(uint32_t k) const { return uint16_t(b(k) | b(k + 1) << 8); }
	// A jump: [+0x14] the section of the u16 at the byte `k` (the loop doesn't move it on: 0x4F2C97).
	void jump(uint32_t k) { e.u16(0x14) = w(k); }
	uint32_t d(uint32_t k) const { return uint32_t(w(k)) | uint32_t(w(k + 2)) << 16; }
	// A variable by the operand byte `k` (the game indexes the buffer without a check).
	uint32_t& var(uint32_t k) {
		static uint32_t outside = 0;
		const uint8_t v = b(k);
		if (v < vars.size()) {
			return vars[v];
		}
		outside = 0;
		return outside;
	}
};

// The script ends: its program let go (0x4F37E0) and its variables freed ([+0xC] to 0).
void end_script(SaveGame& save, uint16_t event, MissionData::Script& script) {
	fn_4f37e0(save, script.program);
	script.variables.clear();
	save.events[event].u32(0xC) = 0;
}

// What an instruction leaves: the script ends there (an unknown instruction, 0x4F2D44: the program let go and the
// variables freed by the caller), it goes on at the next section (0x4F2C93), or it jumped (0x4F2C97).
enum class Step { End, Next, Jumped };

// One instruction (the jump table's handlers).
Step step(Run& r, uint8_t op) {
	switch (op) {
	case 0x04: {
		// 0x4EEB62: v ← a human merchant (kind 0) of the game's choosing: the town (v modulo the towns' count)'s
		// [+0x2D4] modulo how many humans there are (signed, kept as a byte) is the rank of the one taken, counting
		// the humans from the merchant 0; the merchants' count if no rank matches (a negative remainder). (No human:
		// the game divides by 0.)
		SaveGame& s = r.save;
		const uint16_t merchants = s.globals.u16(global::kWorldMerchantCount);
		auto human = [&](uint32_t m) { return m < s.merchants.size() && s.merchants[m].kind() == 0; };
		int8_t humans = 0;
		for (uint32_t m = 0; m < merchants; ++m) {
			humans = int8_t(humans + (human(m) ? 1 : 0));
		}
		const uint32_t towns = s.globals.u32(global::kWorldTownCount);
		const uint32_t town = towns ? r.var(1) % towns : 0;
		const int32_t field = town < s.towns.size() ? s.towns[town].memory.s32(0x2D4) : 0;
		const uint8_t rank = humans ? uint8_t(field % humans) : 0;
		int8_t seen = -1;
		uint32_t m = 0;
		for (; m < merchants; ++m) {
			seen = int8_t(seen + (human(m) ? 1 : 0));
			if (int32_t(seen) == int32_t(rank)) {
				break;
			}
		}
		r.var(1) = m;
		return Step::Next;
	}
	case 0x05:  // 0x4EEC39: v ← now (0x4F3860 on the world: [0x6DE4B4]).
		r.var(1) = r.save.globals.u32(global::kWorldTick);
		return Step::Next;
	case 0x06:  // 0x4EECE2: b ← now + a days (a · 256 ticks).
		r.var(2) = r.save.globals.u32(global::kWorldTick) + (r.var(1) << 8);
		return Step::Next;
	case 0x07: {  // 0x4EED2D: b ← the merchant a's home town ([+0x19]).
		const uint32_t m = r.var(1);
		r.var(2) = m < r.save.merchants.size() ? r.save.merchants[m].memory.u8(0x19) : 0;
		return Step::Next;
	}
	case 0x09: {
		// 0x4EEDFD: b ← a number below a of the game's own: now, plus the ship of the ships' object's [+0xEA]
		// (0x4F38A0) [+0x1C] and [+0x20] when it is below the ships' count, plus the event list's [+0x8]; its
		// remainder by a (signed). (a 0: the game divides by 0.)
		SaveGame& s = r.save;
		uint32_t n = s.globals.u32(global::kWorldTick);
		const uint16_t ship = s.globals.u16(global::kShips + 0xEA);
		if (ship < s.globals.u16(global::kShipCount) && ship < s.ships.size()) {
			n += s.ships[ship].memory.u32(0x1C);
			n += s.ships[ship].memory.u32(0x20);
		}
		n += s.globals.u16(0x6DD744);
		const int32_t by = int32_t(r.var(1));
		r.var(2) = by != 0 && !(by == -1 && int32_t(n) == INT32_MIN) ? uint32_t(int32_t(n) % by) : 0;
		return Step::Next;
	}
	// The arithmetic of two variables a and b into c (32 bits; the quotient signed, its remainder dropped).
	case 0x0B:  // 0x4EEED1
		r.var(3) = r.var(1) + r.var(2);
		return Step::Next;
	case 0x0C:  // 0x4EEEF3
		r.var(3) = r.var(1) - r.var(2);
		return Step::Next;
	case 0x0D:  // 0x4EEF15
		r.var(3) = r.var(1) * r.var(2);
		return Step::Next;
	case 0x0E: {  // 0x4EEF36 (b 0: the game divides by 0)
		const int32_t a = int32_t(r.var(1));
		const int32_t b = int32_t(r.var(2));
		r.var(3) = b != 0 && !(b == -1 && a == INT32_MIN) ? uint32_t(a / b) : 0;
		return Step::Next;
	}
	case 0x0A:  // 0x4EEEAA: v ← the u32 at the byte 2.
		r.var(1) = r.d(2);
		return Step::Next;
	// The comparisons of two variables (signed): true → the section of the u16 at the byte 3, else at 5.
	case 0x17:  // 0x4EF2DF: a < b.
		r.jump(int32_t(r.var(1)) < int32_t(r.var(2)) ? 3 : 5);
		return Step::Jumped;
	case 0x18:  // 0x4EF30B: a <= b.
		r.jump(int32_t(r.var(1)) <= int32_t(r.var(2)) ? 3 : 5);
		return Step::Jumped;
	case 0x19:  // 0x4EF337: a == b.
		r.jump(r.var(1) == r.var(2) ? 3 : 5);
		return Step::Jumped;
	case 0x1A:  // 0x4EF35F: a != b.
		r.jump(r.var(1) != r.var(2) ? 3 : 5);
		return Step::Jumped;
	case 0x1B:  // 0x4EF387: a >= b.
		r.jump(int32_t(r.var(1)) >= int32_t(r.var(2)) ? 3 : 5);
		return Step::Jumped;
	case 0x1C:  // 0x4EF3B3: a > b.
		r.jump(int32_t(r.var(1)) > int32_t(r.var(2)) ? 3 : 5);
		return Step::Jumped;
	case 0x21: {
		// 0x4EF4B6: v ← a number of the game's own: (now + the event list's [+0x8] (u16 0x6DD744) + the u32 at the
		// towns' array + 0xCCC (the town 1's [+0x2D4]) ) · the section, modulo the towns' count (0x4F3680:
		// [0x6DE4B0]), unsigned.
		const SaveGame& s = r.save;
		const uint32_t mixed = (s.globals.u32(global::kWorldTick) + s.globals.u16(0x6DD744) +
								   (s.towns.size() > 1 ? s.towns[1].memory.u32(0x2D4) : 0)) *
			uint32_t(r.e.u16(0x14));
		const uint32_t towns = s.globals.u32(global::kWorldTownCount);
		r.var(1) = towns ? mixed % towns : 0;  // (The game divides by 0 without towns: never.)
		return Step::Next;
	}
	case 0x26:
	case 0x27: {
		// 0x4EF875 (both): among the ships of the human merchants (kind 0), the one in the variable of the byte 1
		// or, with −1, all of them (0 to the merchants' count − 1), those in the state 0 (docked; 0x27: 0xF) with
		// some of the ware of the variable of the byte 2, each merchant's first: one of them, by the sum of their
		// [+0x1E] (u16) modulo how
		// many (a byte), into the variable of the byte 3; −1 if none. Each merchant's ships from his [+0xE] by their
		// [+0x4], while below the ships' count.
		SaveGame& s = r.save;
		const uint16_t state = op == 0x27 ? 0xF : 0;
		const uint32_t who = r.var(1);
		const uint32_t ware = r.var(2);
		const int32_t last = who == 0xFFFFFFFFu ? int32_t(s.globals.u16(global::kWorldMerchantCount)) - 1 : int32_t(who);
		uint16_t found[256];
		uint8_t count = 0;
		uint16_t sum = 0;
		const uint16_t ships = s.globals.u16(global::kShipCount);
		for (int32_t m = who == 0xFFFFFFFFu ? 0 : int32_t(who); m <= last; ++m) {
			if (size_t(m) >= s.merchants.size() || s.merchants[size_t(m)].kind() != 0) {
				continue;
			}
			for (uint16_t id = s.merchants[size_t(m)].first_ship(); id < ships && id < s.ships.size();
				 id = s.ships[id].memory.u16(0x4)) {
				const SavedShip& ship = s.ships[id];
				if (ship.status() == state && ware < 0x40 && ship.memory.u32(0x54 + 4 * ware) != 0) {
					found[count++] = id;
					sum = uint16_t(sum + ship.memory.u16(0x1E));
					break;  // (Each merchant's first such ship: 0x4EF972 goes on to the next merchant.)
				}
			}
		}
		r.var(3) = count > 0 ? found[sum % count] : 0xFFFFFFFFu;
		return Step::Next;
	}
	default:
		if (has_handler(op)) {
			++g_unwritten[op];
		}
		return Step::End;
	}
}

}  // namespace

const uint32_t* unwritten_mission_ops() {
	return g_unwritten;
}

uint32_t run_mission_script(SaveGame& save, uint16_t event) {
	GameMemory<0x18>& e = save.events[event];
	MissionData::Script& script = save.missions.scripts[event];
	if (e.u32(8) == 0 || !script.program) {
		// No program (a mission the list doesn't have, or one that couldn't be loaded): the variables go.
		script.variables.clear();
		e.u32(0xC) = 0;
		return 0;
	}
	const MissionProgram program = script.program;  // (kept while it runs, even if the list lets it go)
	Run r{save, event, e, *program, script.variables};
	const uint32_t now = save.globals.u32(global::kWorldTick);
	uint8_t op = r.b(0);
	while (op != 0 && op < 0xFD) {
		const Step next = step(r, op);
		if (next == Step::End) {
			end_script(save, event, script);
			return 0;
		}
		if (next == Step::Next) {
			e.u16(0x14) = uint16_t(e.u16(0x14) + 1);
		}
		op = r.b(0);
	}
	switch (op) {
	case 0xFD:
		// Wait so many ticks (the variable of its byte 1), then go on at its section (u16 at its byte 2).
		e.u32(0) = r.var(1) + now;
		e.u16(0x14) = r.w(2);
		return 1;
	case 0x00: {
		// Wait for the tick of its variable; one that has passed (with a day's grace) ends the script.
		const uint32_t tick = r.var(1);
		if (tick - 0x100 < now) {
			end_script(save, event, script);
			return 0;
		}
		e.u32(0) = tick;
		e.u16(0x14) = uint16_t(e.u16(0x14) + 1);
		return 1;
	}
	case 0xFE:
		// Wait so many days, then start again at the section 0.
		e.u32(0) = (r.var(1) << 8) + now;
		e.u16(0x14) = 0;
		return 1;
	default:  // 0xFF: the end.
		end_script(save, event, script);
		return 0;
	}
}

}  // namespace p3::data::missions_detail
