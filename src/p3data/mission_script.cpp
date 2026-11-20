// The missions' interpreter: 0x4ECF64 (docs/formats/missions.md). Each section of a program is one instruction: its
// first byte the opcode (dispatched by the byte table 0x4F3054 into the jump table 0x4F2E34: 138 with a handler,
// 0x01..0x74 but 0x30 and 0x3D, and 0xE4..0xFB; the rest end the script), then its operands, mostly variables'
// numbers (a byte each) and constants.
//
// Rewritten so far: the loop and its waits (0x00, 0xFD, 0xFE, 0xFF); the variables' and numbers' 0x04..0x07, 0x09,
// 0x0A, 0x21; the arithmetic 0x0B..0x0E; the comparisons 0x17..0x1C, the jump 0x1D; a merchant's rank 0x15; a ship's
// town 0x1E; a ship name 0x20; the search of a ship 0x26 / 0x27; an office
// 0x38; the next human 0x3F; a first name 0x23, a family name 0x24; a way between towns 0x53; a wanderer's next town 0x29 (0x533210); a ship by its hull 0x40;
// the mission letter 0xF7. The handlers come in parts (roadmap, section 3; the order by what the saves run:
// re/emu/sesion47/opcount.sh); until then an instruction not rewritten ends the script as an unknown one does, and is
// counted (unwritten_mission_ops).

#include "p3data/mission_script.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "p3data/game_memory.hpp"
#include "p3data/letters.hpp"
#include "p3data/mail.hpp"
#include "p3data/missions.hpp"
#include "p3data/sailing.hpp"
#include "p3data/savegame.hpp"
#include "p3data/town_tick/internal.hpp"

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

// 0x533210 (on the world): the town nearest to `town` by the world's matrix (town_travel: [k · n + town], k's way to
// it), not `town` itself nor one of `avoid`, nor, for a ship (below the ships' count) of a type ([+0xE]) above 1, a
// town with the bit 1 of its [+0x2C8]; 0 if none. With `wander`, then the first town within the best way + 2 (the
// same ones left out) whose turn comes: the world's tick, counted up for each such town, a multiple of 3.
uint8_t fn_533210(const SaveGame& save, uint32_t town, const uint32_t* avoid, int32_t count, bool wander, uint32_t ship) {
	const GlobalMemory& g = save.globals;
	const uint32_t n = g.u32(global::kWorldTownCount);
	const bool has_ship = uint16_t(ship) != 0xFFFF && uint16_t(ship) < g.u16(global::kShipCount) &&
		uint16_t(ship) < save.ships.size();
	const uint8_t type = has_ship ? save.ships[uint16_t(ship)].memory.u8(0xE) : 0;
	auto left_out = [&](uint32_t k) {
		if (has_ship && k < save.towns.size() && (save.towns[k].memory.u8(0x2C8) & 1) && type > 1) {
			return true;
		}
		for (int32_t i = 0; i < count; ++i) {
			if (avoid[i] == k) {
				return true;
			}
		}
		return false;
	};
	auto way = [&](uint32_t k) -> uint32_t {
		const size_t at = size_t(k) * n + town;
		return at < save.town_travel.size() ? save.town_travel[at] : 0;
	};
	uint32_t best = 0x0FFFFFFF;
	uint32_t found = 0;
	for (uint32_t k = 0; k < n; ++k) {
		if (k == town || way(k) >= best || left_out(k)) {
			continue;
		}
		found = k;
		best = way(k);
	}
	if (wander) {
		uint32_t turn = g.u32(global::kWorldTick);
		for (uint32_t k = 0; k < n; ++k) {
			if (k == town || way(k) >= best + 2) {
				continue;
			}
			if (turn++ % 3 != 0 || left_out(k)) {
				continue;
			}
			return uint8_t(k);
		}
	}
	return uint8_t(found);
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
	case 0xF7: {
		// 0x4ED4A0: a mission letter. Its type b1 + 0x3C (0x71 past 0x40), its [+0x5] the variable b2's low byte;
		// its record [+0x4] the variable b4, [+0x8] the event being run (the event list's [+0x8], 0x6DD744), [+0xA]
		// b3 (the first variable for the answer), [+0xB] b6 (how many answers), [+0xC] b5, [+0xD] 0xFF, [+0xE] and
		// [+0xF] 0, [+0x0] the buffer's bytes; its text from the program at the u32 of the byte 8
		// (mission_letter_text). With answers, b1 not 5 and an addressee (the variable b5) below 0: the variables
		// b3..b3+7 get b6, b5..b5+7 −1 (a memset of 0x20 bytes), then, as b5 is −1 now, each human merchant k his own
		// copy, the variable b5 + k his index and the record's [+0xA] one more after each (the game's branch for −3,
		// the owners of the offices of the town [+0x5], comes after the memset: it can't be taken). Otherwise the
		// variable b3 gets b6 and the letter goes to the variable b5 (0x4D6530). With b7, the ticker line 0x12 for
		// b5 with b2 (0x548CA0; the player's messages window told, 0x47D1F0: the interface's).
		SaveGame& s = r.save;
		const uint8_t b1 = r.b(1);
		const uint8_t b3 = r.b(3);
		const uint8_t b5 = r.b(5);
		const uint8_t answers = r.b(6);
		uint8_t type = uint8_t(b1 + 0x3C);
		if (type > 0x40) {
			type = 0x71;
		}
		Letter letter(type);
		letter.entry.u8(5) = uint8_t(r.var(2));
		GameMemory<0x10> record{};
		record.u32(4) = r.var(4);
		record.u16(8) = s.globals.u16(0x6DD744);
		record.u8(0xA) = b3;
		record.u8(0xB) = answers;
		record.u8(0xC) = b5;
		record.u8(0xD) = 0xFF;
		const Sailing* sailing = tick::event_sailing();
		letter.buffer = mission_letter_text(s, sailing ? sailing->texts() : nullptr, r.p, r.d(8), r.vars, answers, record);
		record.u32(0) = uint32_t(letter.buffer.size());
		letter.record = record;
		auto at = [&](uint32_t index) -> uint32_t* { return index < r.vars.size() ? &r.vars[index] : nullptr; };
		if (answers != 0 && b1 != 5 && int32_t(r.var(5)) < 0) {
			for (uint32_t k = 0; k < 8; ++k) {
				if (uint32_t* v = at(b3 + k)) {
					*v = answers;
				}
			}
			for (uint32_t k = 0; k < 8; ++k) {
				if (uint32_t* v = at(b5 + k)) {
					*v = 0xFFFFFFFFu;
				}
			}
			uint32_t k = 0;
			for (uint16_t m = 0; m < s.globals.u16(global::kWorldMerchantCount); ++m) {
				if (m < s.merchants.size() && s.merchants[m].kind() == 0) {
					if (uint32_t* v = at(b5 + k)) {
						*v = m;
					}
					++k;
					send_letter(s, m, letter);
					letter.record->u8(0xA) = uint8_t(letter.record->u8(0xA) + 1);
				}
			}
		} else {
			if (uint32_t* v = at(b3)) {
				*v = answers;
			}
			send_letter(s, int32_t(r.var(5)), letter);
		}
		if (r.b(7) != 0) {
			send_ticker(s, uint16_t(r.var(5)), 0x12, uint16_t(r.var(2)));
		}
		return Step::Next;
	}
	case 0x1D:  // 0x4EF3DF: on at the section of the u16 at the byte 1.
		r.jump(1);
		return Step::Jumped;
	case 0x1E: {
		// 0x4EF3F0: b ← −1, then, for a ship a below the ships' count (signed) in a state below 0xF but 2 and 3, its
		// town ([+0x39]).
		const SaveGame& s = r.save;
		const int32_t ship = int32_t(r.var(1));
		r.var(2) = 0xFFFFFFFFu;
		if (ship < int32_t(s.globals.u16(global::kShipCount)) && ship >= 0 && size_t(ship) < s.ships.size()) {
			const uint16_t state = s.ships[size_t(ship)].memory.u16(0x134);
			if (state < 0xF && state != 2 && state != 3) {
				r.var(2) = s.ships[size_t(ship)].memory.u8(0x39);
			}
		}
		return Step::Next;
	}
	case 0x15: {  // 0x4EF260: b ← the merchant a's rank in his home town (0x4F3760: [+0x39C + [+0x19]]).
		const uint32_t m = r.var(1);
		r.var(2) = m < r.save.merchants.size()
			? r.save.merchants[m].memory.u8(0x39C + r.save.merchants[m].memory.u8(0x19))
			: 0;
		return Step::Next;
	}
	case 0x20: {
		// 0x4EF485: v ← the next ship name's number (0x50DF00 on the ships' object, as 0x509A50 names a ship: [+0xEE]
		// + [+0xF0] modulo 307 into [+0xEE] until below the names' count, 0x6DDB78).
		GlobalMemory& g = r.save.globals;
		const uint16_t names = g.u16(0x6DDB78);
		uint16_t name = g.u16(global::kShips + 0xEE);
		if (names != 0) {  // (None: the game loops.)
			const uint16_t step = g.u16(global::kShips + 0xF0);
			do {
				name = uint16_t(int32_t(uint16_t(g.u16(global::kShips + 0xEE) + step)) % 0x133);
				g.u16(global::kShips + 0xEE) = name;
			} while (name >= names);
		}
		r.var(1) = name;
		return Step::Next;
	}
	case 0x23: {
		// 0x4EF588: b ← a first name's number of the game's own: (now + the event list's [+0x8]) · the town 7's
		// [+0x348] (the towns' array + 0x4910), its high byte cleared; then by the variable a, 1 a man's (modulo the
		// men's names' count, the names object's [+0xD0]), 2 a woman's (modulo the women's, [+0xD2], with the bit
		// 0x80: the women's list), another by its bit 0x80 (set: a woman's).
		const SaveGame& s = r.save;
		uint32_t n = s.globals.u32(global::kWorldTick) + s.globals.u16(0x6DD744);
		n *= s.towns.size() > 7 ? s.towns[7].memory.u32(0x348) : 0;
		n &= 0x00FFFFFF;
		const uint32_t mode = r.var(1);
		const bool man = mode == 1 || (mode != 2 && !(mode & 0x80));
		const uint16_t names = s.globals.u16(man ? 0x6DDB70 : 0x6DDB72);
		n = names ? n % names : 0;  // (None: the game divides by 0.)
		r.var(2) = man ? n : n | 0x80;
		return Step::Next;
	}
	case 0x53:  // 0x4EFB40: c ← the way from the town a to the town b (0x531B70: the world's matrix; 0xFF outside it).
		r.var(3) = tick::ship_trade_detail::fn_531b70(r.save, r.var(1), r.var(2));
		return Step::Next;
	case 0x24: {
		// 0x4EF65F: v ← a family name's number of the game's own: (the event list's [+0x8] (0x6DD744) + the u32 at
		// the towns' array + 0x3EEC (the town 6's [+0x31C]) + the one at + 0x4910 (the town 7's [+0x348])) · now's
		// low 16 bits, its low 28 bits, modulo the families' count (the names object's [+0xD4], 0x4F38F0).
		const SaveGame& s = r.save;
		uint32_t n = s.globals.u16(0x6DD744);
		n += s.towns.size() > 6 ? s.towns[6].memory.u32(0x31C) : 0;
		n += s.towns.size() > 7 ? s.towns[7].memory.u32(0x348) : 0;
		n *= s.globals.u32(global::kWorldTick) & 0xFFFF;
		n &= 0x0FFFFFFF;
		const uint16_t families = s.globals.u16(0x6DDB74);
		r.var(1) = families ? n % families : 0;  // (None: the game divides by 0.)
		return Step::Next;
	}
	case 0x29: {
		// 0x4EFBAA: the town after d for a wanderer that was in a, b and c (0x533210 on d, those three left out,
		// wandering by the variable e, no ship); then a ← b, b ← c, c ← d and d ← it.
		const uint32_t seen[3] = {r.var(1), r.var(2), r.var(3)};
		const uint8_t next = fn_533210(r.save, r.var(4), seen, 3, r.var(5) != 0, 0xFFFF);
		r.var(1) = r.var(2);
		r.var(2) = r.var(3);
		r.var(3) = r.var(4);
		r.var(4) = next;
		return Step::Next;
	}
	case 0x40: {
		// 0x4F12BB: f ← −1, then the first ship of the merchant c (from his [+0xE] by [+0x4], below the ships' count)
		// whose hull ([+0x18] · 100 / [+0x14], signed) is at least b with the variable a set, below b without it, and
		// whose state ([+0x134]) the mode d takes: 0 the state 0xF, 1 below 4, 2 and over either; another mode none.
		SaveGame& s = r.save;
		const bool above = r.var(1) != 0;
		const int32_t hull = int32_t(r.var(2));
		const uint32_t who = r.var(3);
		const int32_t mode = int32_t(r.var(4));
		r.var(5) = 0xFFFFFFFFu;
		const uint16_t ships = s.globals.u16(global::kShipCount);
		uint16_t id = who < s.merchants.size() ? s.merchants[who].first_ship() : 0xFFFF;
		for (; id < ships && id < s.ships.size(); id = s.ships[id].memory.u16(0x4)) {
			const ShipMemory& m = s.ships[id].memory;
			const int32_t max = m.s32(0x14);
			const int32_t percent = max ? m.s32(0x18) * 100 / max : 0;  // (A max of 0: the game divides by 0.)
			if (above ? percent < hull : percent >= hull) {
				continue;
			}
			const uint16_t state = m.u16(0x134);
			const bool taken = mode >= 2 ? (state == 0xF || state < 4) : mode == 0 ? state == 0xF : mode == 1 && state < 4;
			if (taken) {
				r.var(5) = id;
				break;
			}
		}
		return Step::Next;
	}
	case 0x3F: {
		// 0x4F1253: b ← the next human merchant (kind 0) after a, round from the last to the merchant 0 (signed,
		// below the merchants' count); a itself when there is no other. (A −1 with no human: the game loops.)
		SaveGame& s = r.save;
		const int32_t from = int32_t(r.var(1));
		r.var(2) = uint32_t(from);
		const int32_t merchants = s.globals.u16(global::kWorldMerchantCount);
		int32_t m = from + 1;
		for (uint32_t guard = 0; m != from && guard < 0x10000; ++guard) {
			if (m >= merchants) {
				m = 0;
			}
			if (size_t(m) < s.merchants.size() && s.merchants[size_t(m)].kind() == 0) {
				r.var(2) = uint32_t(m);
				break;
			}
			++m;
		}
		return Step::Next;
	}
	case 0x38:
		// 0x4F0EFA: c ← whether the merchant a has an office in the town b (0x5308A0(merchant, town) with nothing
		// made, the two as u16): 1 or 0.
		r.var(3) = tick::facilities_detail::fn_5308a0(r.save, uint16_t(r.var(1)), uint16_t(r.var(2))) >= 0 ? 1 : 0;
		return Step::Next;
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
	// (A tool's trace, P3_MISSION_TRACE set: each instruction as re/emu/sesion47/optrace.py prints the game's.)
	static const bool trace = std::getenv("P3_MISSION_TRACE") != nullptr;
	while (op != 0 && op < 0xFD) {
		if (trace) {
			std::fprintf(stderr, "tick %u event %u mission %u section %u op %#04x", unsigned(now), unsigned(event),
				unsigned(e.u16(0x10)), unsigned(e.u16(0x14)), unsigned(op));
			for (uint32_t k = 0; k < 12; ++k) {
				std::fprintf(stderr, "%s%02x", k ? "" : " ", r.b(k));
			}
			std::fprintf(stderr, " vars");
			for (size_t k = 0; k < r.vars.size() && k < 24; ++k) {
				std::fprintf(stderr, " %x", r.vars[k]);
			}
			std::fprintf(stderr, "\n");
		}
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
