// The missions' interpreter: 0x4ECF64 (docs/formats/missions.md). Each section of a program is one instruction: its
// first byte the opcode (dispatched by the byte table 0x4F3054 into the jump table 0x4F2E34: 138 with a handler,
// 0x01..0x74 but 0x30 and 0x3D, and 0xE4..0xFB; the rest end the script), then its operands, mostly variables'
// numbers (a byte each) and constants.
//
// Rewritten so far: the loop and its waits (0x00, 0xFD, 0xFE, 0xFF); the variables' and numbers' 0x04..0x07, 0x09,
// 0x0A, 0x21; the arithmetic 0x0B..0x0E; the comparisons 0x17..0x1C, the jump 0x1D; a merchant's rank 0x15; a ship's
// town 0x1E; a ship name 0x20; the humans 0x39; the towns 0x43; an office's town 0x44; a treasure found 0xF0; a
// merchant's names 0x11, 0x12; a town's offices 0x4F; the search of a ship 0x26 / 0x27; an office
// 0x38; the next human 0x3F; a first name 0x23, a family name 0x24; a way between towns 0x53; a wanderer's next town 0x29 (0x533210); a ship by its hull 0x40;
// the mission letter 0xF7; the ware a town lacks 0x2F (0x51BEB0); the sieges' difficulty 0x35; a history event's window
// 0x37 (the interface's); a town besieged 0x45; a sell price 0x4B; the walls' level 0x6B; a siege 0xFA (the operation
// 0x8E); a merchant's money 0xF8; a pirate attack 0xF9, 0xE9, 0xE7 (the operation 0x82); a merchant's ticker line 0xE8;
// a ship's extra loads 0xF1, its [+0x157] 0xF2, its [+0x3F] 0xF3, its cargo 0xF4; the treasure map's letter 0xF6; the
// missions' hold on a ship 0x3A (0x50DF40, 0x50DF70); a ship's sailors 0x3B, owner 0x28, place 0x2A, hull 0x42; a
// mission's pirate sunk 0x51; a merchant's ship with most room in a town 0x6E; a number below 100 0x22; a ship near a
// treasure spot 0x1F; a captain's state 0x2C (0x4D7770); a ship's hull set 0x41; a commission's event 0x46; an
// office's stock 0x47 and its [+0x2E8] 0x48; what wares are worth to the pirates 0x49; a town's or an office's stock
// moved 0x4C, 0xEE; a merchant's float 0x4D; a ship's hold 0x4E; a merchant's letters 0x72 summed 0x50 (0x531260); a
// ship to lead a convoy 0x54; a merchant's [+0x16] 0x61; an event 0xF 0x69; a merchant with an office 0x6A; a ship's
// bit 0x40 0x6C; a merchant's best armed ship 0x6D; a ship's next town 0x6F. The network game's: the leading human
// 0x08, a reputation 0x2E, a merchant's ship by chance 0x36, a ship's cargo 0x3E, the Hanse's town 0x56, a merchant's
// ship by speed 0x57 or by a ware 0x5A, the variables by number 0x58 and 0x59, a ship's sailors 0xEB. The handlers come
// in parts (roadmap, section 3; the order by what the saves run:
// re/emu/sesion47/opcount.sh); until then an instruction not rewritten ends the script as an unknown one does, and is
// counted (unwritten_mission_ops).

#include "p3data/mission_script.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "p3data/game_memory.hpp"
#include "p3data/letters.hpp"
#include "p3data/mail.hpp"
#include "p3data/missions.hpp"
#include "p3data/sailing.hpp"
#include "p3data/savegame.hpp"
#include "p3data/trade.hpp"
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
	// The event, by its index each time: an instruction that makes an event (0x46) may move the array (0x4D8CF0 grows
	// it), as the game reads the event again after it.
	GameMemory<0x18>& e() const { return save.events[event]; }
	const std::vector<uint8_t>& p;
	std::vector<uint32_t>& vars;

	// The current section's offset ([+0x14]) and its bytes.
	uint32_t at() const {
		const size_t o = 4 + 4 * size_t(e().u16(0x14));
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
	void jump(uint32_t k) { e().u16(0x14) = w(k); }
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

// 0x51BEB0 (on the town): the ware it lacks the most: of the wares 0..19 but 6 (spices), those whose u16 [+0x844 + 16 ·
// the ware] isn't 0 and whose stock ([+0x4 + 4 · ware], signed) is below its [+0x4F4 + 16 · ware], the one with the
// lowest stock / [+0x4F0 + 16 · ware] (floats) below 400; 0x1F if none. The u16 is the workers of the town's facility
// [+0x840 + 16 · k] of the business type k numbered as the ware: the game reads the ware's business type (0x672C88)
// and leaves it unused, its branch for the whale oil (0xFF, with [+0x2CC] above 1000) under a check of the ware's
// number against 0x15 that is always true; the market hall's 0x5DE3xx reads the facility of the ware's type: it looks
// like a game bug (reproduced; the roadmap's list).
uint8_t fn_51beb0(const SavedTown& st) {
	const TownMemory& t = st.memory;
	float best = 400.0f;
	uint8_t found = 0x1F;
	for (uint32_t ware = 0; ware < 20; ++ware) {
		if (ware == 6 || t.u16(0x844 + 16 * ware) == 0) {
			continue;
		}
		const int32_t stock = t.s32(4 + 4 * ware);
		if (stock >= t.s32(0x4F4 + 16 * ware)) {
			continue;
		}
		const int32_t by = t.s32(0x4F0 + 16 * ware);
		float ratio;
		if (by != 0) {
			ratio = float(stock) / float(by);
		} else if (stock == 0) {
			continue;  // (0 / 0: the x87's NaN, never below.)
		} else {
			ratio = stock > 0 ? HUGE_VALF : -HUGE_VALF;  // (The x87's infinities.)
		}
		if (ratio < best) {
			best = ratio;
			found = uint8_t(ware);
		}
	}
	return found;
}

// 0x50DF40 and 0x50DF70 (on the ships' object, a ship below the count): the missions' hold on a ship ([+0x137], a
// sunk or deleted ship with it waits in the list [+0xE6] instead of the free list [+0xE2]: Sailing's free) one up (up
// to 0xFA) or one down (from 0); going down, the waiting ships whose count is 0 go from [+0xE6] (next [+0x6]) to the
// head of [+0xE2].
void fn_50df40(SaveGame& s, uint16_t ship) {
	if (ship < s.globals.u16(global::kShipCount) && ship < s.ships.size() && s.ships[ship].memory.u8(0x137) < 0xFA) {
		++s.ships[ship].memory.u8(0x137);
	}
}

void fn_50df70(SaveGame& s, uint16_t ship) {
	GlobalMemory& g = s.globals;
	const uint16_t count = g.u16(global::kShipCount);
	if (ship >= count || ship >= s.ships.size()) {
		return;
	}
	if (s.ships[ship].memory.u8(0x137) != 0) {
		--s.ships[ship].memory.u8(0x137);
	}
	uint16_t* link = &g.u16(global::kShips + 0xE6);
	for (uint32_t guard = 0; *link < count && *link < s.ships.size() && guard < 0x10000; ++guard) {
		const uint16_t i = *link;
		ShipMemory& m = s.ships[i].memory;
		if (m.u8(0x137) == 0) {
			*link = m.u16(0x6);
			m.u16(0x6) = g.u16(global::kShips + 0xE2);
			g.u16(global::kShips + 0xE2) = i;
		} else {
			link = &m.u16(0x6);
		}
	}
}

// 0x531260 (on the world): a date's tick: (year · 365 + the days before the month (0x672D78) + the day − 1, the day
// cut to the month's length (0x672D94); a month past 11: the next year's start) · 256.
uint32_t fn_531260(uint8_t day, uint8_t month, uint16_t year) {
	static constexpr uint16_t kBefore[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
	static constexpr uint16_t kLength[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	uint32_t days = uint32_t(year) * 365;
	if (month > 11) {
		return (days + 365) << 8;
	}
	days += kBefore[month];
	if (day > 1) {
		days += (day > kLength[month] ? kLength[month] : day) - 1u;
	}
	return days << 8;
}

// 0x4C and 0xEE: `units` loads (· 2000; · 200 for a ware in barrels, 0x672C14) of the ware into a stock (a negative
// count takes them out, at most what there is); returns what moved in loads or barrels (signed).
int32_t move_stock(int32_t& stock, uint32_t ware, uint32_t units) {
	static constexpr uint8_t kBarrels[kWareCount] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
	const int32_t unit = kBarrels[ware] ? 200 : 2000;
	const int32_t amount = int32_t(units * uint32_t(unit));
	int32_t moved = amount;
	if (amount < 0) {
		const int32_t out = int32_t(0u - uint32_t(amount));
		if (stock < out) {
			moved = stock;
			stock = 0;
		} else {
			moved = out;
			stock = int32_t(uint32_t(stock) + uint32_t(amount));
		}
	} else {
		stock = int32_t(uint32_t(stock) + uint32_t(amount));
	}
	return moved / unit;
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
			uint32_t(r.e().u16(0x14));
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
		if (std::getenv("P3_MISSION_TRACE")) {  // (The tools' trace: the letter's text, its NULs as «|».)
			std::string shown;
			for (const uint8_t c : letter.buffer) {
				shown += c == 0 ? '|' : char(c);
			}
			std::fprintf(stderr, "letter %s\n", shown.c_str());
		}
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
	case 0x11:  // 0x4EF112: b ← the merchant a's family name ([+0x1D]).
	case 0x12: {  // 0x4EF161: b ← his first name ([+0x1C]).
		const uint32_t m = r.var(1);
		r.var(2) = m < r.save.merchants.size() ? r.save.merchants[m].memory.u8(op == 0x11 ? 0x1D : 0x1C) : 0;
		return Step::Next;
	}
	case 0x4F: {
		// 0x4F1B74: c ← how many offices the town b has (from its [+0x784] by [+0x2CA], below the offices' count) by
		// the mode a: 0 a human's (its owner [+0x2C4] of kind 0), 1 the others', 2 all; another none.
		const SaveGame& s = r.save;
		const uint32_t mode = r.var(1);
		const uint32_t town = r.var(2);
		r.var(3) = 0;
		const uint16_t offices = s.globals.u16(global::kWorldOfficeCount);
		uint16_t i = town < s.towns.size() ? s.towns[town].memory.u16(0x784) : 0xFFFF;
		for (; i < offices && i < s.offices.size(); i = s.offices[i].memory.u16(0x2CA)) {
			const uint16_t owner = s.offices[i].memory.u16(0x2C4);
			const bool human = owner < s.merchants.size() && s.merchants[owner].kind() == 0;
			if ((mode == 0 && human) || (mode == 1 && !human) || mode == 2) {
				++r.var(3);
			}
		}
		return Step::Next;
	}
	case 0x39: {  // 0x4F0F6A: v ← how many human merchants (kind 0) there are.
		const SaveGame& s = r.save;
		uint32_t humans = 0;
		for (uint32_t m = 0; m < s.globals.u16(global::kWorldMerchantCount); ++m) {
			humans += m < s.merchants.size() && s.merchants[m].kind() == 0 ? 1 : 0;
		}
		r.var(1) = humans;
		return Step::Next;
	}
	case 0x43:  // 0x4F1592: v ← the towns' count (0x4F3680: [0x6DE4B0]).
		r.var(1) = r.save.globals.u32(global::kWorldTownCount);
		return Step::Next;
	case 0x44: {
		// 0x4F15AC: b ← the town ([+0x2C6]) of one of the merchant a's offices (from his [+0xC] by [+0x2C8], below the
		// offices' count): the one now modulo how many he has (unsigned) steps from the first. (None: the game
		// divides by 0.)
		const SaveGame& s = r.save;
		const uint32_t who = r.var(1);
		const uint16_t offices = s.globals.u16(global::kWorldOfficeCount);
		auto next = [&](uint16_t i) { return i < s.offices.size() ? s.offices[i].memory.u16(0x2C8) : uint16_t(0xFFFF); };
		const uint16_t first = who < s.merchants.size() ? s.merchants[who].memory.u16(0xC) : 0xFFFF;
		uint32_t count = 0;
		for (uint16_t i = first; i < offices && count < 0x10000; i = next(i)) {
			++count;
		}
		if (count == 0) {
			return Step::Next;
		}
		uint16_t at = first;
		for (uint32_t k = s.globals.u32(global::kWorldTick) % count; k > 0; --k) {
			at = next(at);
		}
		r.var(2) = at < s.offices.size() ? s.offices[at].memory.u8(0x2C6) : 0;
		return Step::Next;
	}
	case 0xF0:  // 0x4EE238: the treasure spot b found by the merchant a (0x4D7030 on the mailbox: his map letters).
		treasure_found(r.save, r.var(1), r.var(2));
		return Step::Next;
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
	case 0x2F: {
		// 0x4F0780: b ← the ware the town a lacks the most (0x51BEB0, a byte: 0x1F if none), c ← an amount of it
		// (0x672660[ware], 0 from the ware 20 on). (A town past the towns: the game reads past them.)
		static constexpr uint8_t kAmount[20] = {25, 10, 10, 75, 100, 30, 0, 20, 20, 10, 40, 25, 30, 20, 10, 100, 10, 10, 20, 40};
		const uint32_t town = r.var(1);
		const uint8_t ware = town < r.save.towns.size() ? fn_51beb0(r.save.towns[town]) : 0x1F;
		r.var(2) = ware;
		r.var(3) = ware < 20 ? kAmount[ware] : 0;
		return Step::Next;
	}
	case 0x35:  // 0x4F0A54: v ← the game's detail 6 (the settings' [+0x16], *0x6CC3E8; the sieges' difficulty).
		r.var(1) = r.save.settings.u8(0x16);
		return Step::Next;
	case 0x37:
		// 0x4F0E94: a history event's window to the player (*0x6CC7E8: 0x469380, then 0x469AF0 with 0x16, the event's
		// [+0x8] and [+0xA], and the program's two strings at the u32s of the bytes 1 and 5, its title and its text):
		// the interface's (the viewer's, roadmap section 3), nothing in the world.
		return Step::Next;
	case 0x45: {  // 0x4F167C: b ← whether the town a is besieged ([+0x2C8] & 0x10). (Past the towns: the game reads on.)
		const uint32_t town = r.var(1);
		r.var(2) = town < r.save.towns.size() && (r.save.towns[town].memory.u32(0x2C8) & 0x10) ? 1 : 0;
		return Step::Next;
	}
	case 0x4B: {
		// 0x4F1A0F: d ← what the town c pays for b loads of the ware a (0x52E1D0 on 0x6DE3D8, the sell price of b ·
		// 2000 units, or b · 200 for a ware in barrels, 0x672C14). (A ware or town past theirs: the game reads on.)
		static constexpr uint8_t kBarrels[kWareCount] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
		const uint32_t ware = r.var(1);
		const uint32_t town = r.var(3);
		if (ware >= uint32_t(kWareCount) || town >= r.save.towns.size()) {
			r.var(4) = 0;
			return Step::Next;
		}
		const int32_t units = int32_t(r.var(2) * (kBarrels[ware] ? 200u : 2000u));
		r.var(4) = uint32_t(tick::game_sell_price(r.save, int(ware), int(town), units));
		return Step::Next;
	}
	case 0x6B: {
		// 0x4ED332: b ← the level of the walls of the town a (0x5252F0; −1 for none). (Past the towns: the game reads
		// on.)
		const uint32_t town = r.var(1);
		const uint8_t level = town < r.save.towns.size() ? tick::facilities_detail::fn_5252f0(r.save, int(town)) : 0;
		r.var(2) = level == 0xFF ? 0xFFFFFFFFu : level;
		return Step::Next;
	}
	case 0xFA: {
		// 0x4ED3A6: a siege of the town v (below the towns' count) when it has walls (0x5252F0 not 0xFF): the operation
		// 0x8E {the town, the army by the year (before 1350 0, before 1400 now & 1, then now % 3), the town 0's [+0x2D4]
		// signed modulo 256, 0} queued (0x54AA70), as 0x635170 does with random numbers.
		SaveGame& s = r.save;
		const uint32_t town = r.var(1);
		if (town >= s.globals.u32(global::kWorldTownCount) || town >= s.towns.size() ||
			tick::facilities_detail::fn_5252f0(s, int(town)) == 0xFF) {
			return Step::Next;
		}
		const uint32_t now = s.globals.u32(global::kWorldTick);
		const uint16_t year = s.globals.u16(global::kWorldYear);
		uint32_t op[5] = {0x8E, town, year < 0x546 ? 0 : year < 0x578 ? now & 1 : now % 3,
			uint32_t(s.towns.empty() ? 0 : s.towns[0].memory.s32(0x2D4) % 256), 0};
		uint8_t bytes[0x14];
		std::memcpy(bytes, op, sizeof(bytes));
		tick::enqueue_operation(s, bytes, sizeof(bytes));
		return Step::Next;
	}
	case 0xF8: {
		// 0x4ED20D: the merchant a gets b (his money [+0x0], signed) and his accounts: b below 0 into his [+0x4BC] (as
		// −b), else his [+0x4B8]. (A merchant past them: the game writes past the array.)
		const uint32_t m = r.var(1);
		if (m >= r.save.merchants.size()) {
			return Step::Next;
		}
		MerchantMemory& mm = r.save.merchants[m].memory;
		const int32_t amount = int32_t(r.var(2));
		mm.s32(0x0) = int32_t(uint32_t(mm.s32(0x0)) + uint32_t(amount));
		if (amount < 0) {
			mm.s32(0x4BC) = int32_t(uint32_t(mm.s32(0x4BC)) - uint32_t(amount));
		} else {
			mm.s32(0x4B8) = int32_t(uint32_t(mm.s32(0x4B8)) + uint32_t(amount));
		}
		return Step::Next;
	}
	case 0xE7:
	case 0xE9:
	case 0xF9: {
		// 0x4ED125 (the three): unless the session's state ([0x6DF31C], 0x4F3690) is set, the operation 0x82 (a pirate
		// attack for the mission, queued: 0x54AA70) {[+0x4] u16 the variable b (0xE9: 0xFFFF), [+0x6] the variable a's
		// second byte, [+0x7] its low byte, [+0x8] u16 the event being run (the event list's [+0x8]), [+0xA] the
		// variable d's number (the operation writes the result there), [+0xB] the variable c, [+0xC] the variable b
		// for 0xE9 (else 0xFF), [+0xD] 1 for 0xE7}; then, either way, d ← −1.
		SaveGame& s = r.save;
		if (s.globals.u32(0x6DF31C) == 0) {
			uint8_t op82[0x14] = {0x82, 0, 0, 0};  // (0x535740 leaves the rest 0.)
			const uint16_t b = op == 0xE9 ? 0xFFFF : uint16_t(r.var(2));
			std::memcpy(op82 + 4, &b, 2);
			op82[6] = uint8_t(r.var(1) >> 8);
			op82[7] = uint8_t(r.var(1));
			const uint16_t event = s.globals.u16(0x6DD744);
			std::memcpy(op82 + 8, &event, 2);
			op82[0xA] = r.b(4);
			op82[0xB] = uint8_t(r.var(3));
			op82[0xC] = op == 0xE9 ? uint8_t(r.var(2)) : 0xFF;
			op82[0xD] = op == 0xE7 ? 1 : 0;
			tick::enqueue_operation(s, op82, sizeof(op82));
		}
		r.var(4) = 0xFFFFFFFFu;
		return Step::Next;
	}
	case 0xE8: {
		// 0x4ED086: the merchant v (as a u16) told: the ticker line 0x26 with his home town (0x548CA0) and his window
		// (*0x6CC7E8: 0x469380, 0x469AF0 with 0x1D, the interface's). A merchant past the count: the game jumps back
		// to the same instruction (0x4F2C97 without a step) and hangs there, a game bug (the roadmap's list); p3vita
		// ends the script.
		SaveGame& s = r.save;
		const uint16_t m = uint16_t(r.var(1));
		if (m >= s.globals.u16(global::kWorldMerchantCount) || m >= s.merchants.size()) {
			return Step::End;
		}
		send_ticker(s, m, 0x26, s.merchants[m].memory.u8(0x19));
		return Step::Next;
	}
	case 0xF1: {
		// 0x4EE1CD: the ship a's extra loads ([+0x156], loads that aren't wares) by b: with b above 0, c ← 0x518640 (1
		// if the room is there: the capacity [+0x10] − its loads · 2000 − [+0x11C] − [+0x118] at least b · 2000; then
		// [+0x156] += b and [+0x118] += its new loads · 2000 — the new count, not b: it looks like a game bug, the hold
		// recalculated later puts it right — and 0x517770 with 4; else 0); otherwise 0x5186D0 with −b's low byte (the
		// loads down by it, to 0 if fewer, and [+0x118] − the loads left · 2000, or recalculated (0x5182B0) when it
		// isn't above the loads taken · 2000; then 0x517770 with 4). (A ship past them: the game reads past the array.)
		SaveGame& s = r.save;
		const uint32_t ship = r.var(1);
		if (ship >= s.ships.size()) {
			return Step::Next;
		}
		ShipMemory& m = s.ships[ship].memory;
		const int32_t by = int32_t(r.var(2));
		if (by > 0) {
			const uint8_t n = uint8_t(by);
			const int32_t room = int32_t(uint32_t(m.s32(0x10)) - uint32_t(m.u8(0x156)) * 2000u - uint32_t(m.s32(0x11C)) -
				uint32_t(m.s32(0x118)));
			uint8_t done = 0;
			if (room >= int32_t(uint32_t(n) * 2000u)) {
				m.u8(0x156) = uint8_t(m.u8(0x156) + n);
				m.s32(0x118) = int32_t(uint32_t(m.u8(0x156)) * 2000u + uint32_t(m.s32(0x118)));
				tick::ship_ai_detail::fn_517770(s, int(ship), 4);
				done = 1;
			}
			r.var(3) = done;
		} else {
			const uint8_t n = uint8_t(-int8_t(uint8_t(by)));
			const uint8_t loads = m.u8(0x156);
			if (loads < n) {
				m.u8(0x156) = 0;
				recalculate_cargo(s.ships[ship]);
			} else {
				m.u8(0x156) = uint8_t(loads - n);
				if (m.s32(0x118) <= int32_t(uint32_t(n) * 2000u)) {
					recalculate_cargo(s.ships[ship]);
				} else {
					m.s32(0x118) = int32_t(uint32_t(m.s32(0x118)) - uint32_t(m.u8(0x156)) * 2000u);
				}
			}
			tick::ship_ai_detail::fn_517770(s, int(ship), 4);
		}
		return Step::Next;
	}
	case 0xF2: {
		// 0x4EE1A2: the ship a's [+0x157] += b (a byte; 0 from 0x7F on), then 0x517770 with 4 (0x4F3790). (A ship past
		// them: the game reads past the array.)
		SaveGame& s = r.save;
		const uint32_t ship = r.var(1);
		if (ship >= s.ships.size()) {
			return Step::Next;
		}
		ShipMemory& m = s.ships[ship].memory;
		m.u8(0x157) = uint8_t(m.u8(0x157) + uint8_t(r.var(2)));
		if (m.u8(0x157) >= 0x7F) {
			m.u8(0x157) = 0;
		}
		tick::ship_ai_detail::fn_517770(s, int(ship), 4);
		return Step::Next;
	}
	case 0xF3: {
		// 0x4EE155: the ship a, unless lost (the state 0x11): [+0x3F] ← b's low byte and [+0x3D] |= 2. (A ship past them:
		// the game reads past the array.)
		const uint32_t ship = r.var(1);
		if (ship < r.save.ships.size() && r.save.ships[ship].memory.u16(0x134) != 0x11) {
			ShipMemory& m = r.save.ships[ship].memory;
			m.u8(0x3F) = uint8_t(r.var(2));
			m.u8(0x3D) |= 2;
		}
		return Step::Next;
	}
	case 0xF4: {
		// 0x4EE08A: c loads of the ware b (×2000; ×200 in barrels, 0x672C14) into the ship a's hold ([+0x54 + 4 ·
		// ware]): a negative c takes them out (the hold to 0 if it has fewer); a positive one at most the room
		// (0x5182B0), rounded down to whole loads or barrels (a signed remainder; a negative room takes out). Then
		// 0x5182B0 and 0x517770 with 4. (A ship or ware past theirs: the game reads past them.)
		static constexpr uint8_t kBarrels[kWareCount] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
		SaveGame& s = r.save;
		const uint32_t ship = r.var(1);
		const uint32_t ware = r.var(2);
		if (ship >= s.ships.size() || ware >= uint32_t(kWareCount)) {
			return Step::Next;
		}
		SavedShip& sh = s.ships[ship];
		const int32_t unit = kBarrels[ware] ? 200 : 2000;
		int32_t amount = int32_t(r.var(3) * uint32_t(unit));
		int32_t& cargo = sh.memory.s32(0x54 + 4 * ware);
		if (amount < 0) {
			cargo = cargo < int32_t(0u - uint32_t(amount)) ? 0 : int32_t(uint32_t(amount) + uint32_t(cargo));
		} else {
			const int32_t room = recalculate_cargo(sh);
			if (room < amount) {
				amount = recalculate_cargo(sh);
			}
			amount -= amount % unit;
			cargo = int32_t(uint32_t(cargo) + uint32_t(amount));
		}
		recalculate_cargo(sh);
		tick::ship_ai_detail::fn_517770(s, int(ship), 4);
		return Step::Next;
	}
	case 0xF6: {
		// 0x4EDF3F: a letter of the treasure map to the merchant b: its type 0x45 + (e & 1) (a piece or the map), [+0x5]
		// the variable d's low byte, [+0x8] 0, [+0xA] the variable c, [+0xC] and [+0xE] the treasure spot a's x and y
		// (0x672340: u16 of the u32s; past the 100 the game reads the tables after it: here 0). Then the next section
		// (the handler steps [+0x14] itself and takes the jump's way, 0x4F2C97).
		Letter letter(uint8_t(0x45 + (r.var(5) & 1)));
		letter.entry.u8(5) = uint8_t(r.var(4));
		letter.entry.u16(8) = 0;
		letter.entry.u16(0xA) = uint16_t(r.var(3));
		uint16_t x = 0;
		uint16_t y = 0;
		treasure_spot(r.var(1), x, y);
		letter.entry.u16(0xC) = x;
		letter.entry.u16(0xE) = y;
		send_letter(r.save, int32_t(r.var(2)), letter);
		return Step::Next;
	}
	case 0x3A:  // 0x4F0FCF: the missions' hold on the ship a (a u16): with b set one more (0x50DF40), else one less (0x50DF70).
		if (r.var(2) != 0) {
			fn_50df40(r.save, uint16_t(r.var(1)));
		} else {
			fn_50df70(r.save, uint16_t(r.var(1)));
		}
		return Step::Next;
	case 0x3B: {  // 0x4F1001: b ← the ship a's sailors ([+0x40]). (A ship past them: the game reads past the array.)
		const uint32_t ship = r.var(1);
		r.var(2) = ship < r.save.ships.size() ? r.save.ships[ship].memory.u16(0x40) : 0;
		return Step::Next;
	}
	case 0x28: {
		// 0x4EF9DF: b ← the merchants' count, then, for a ship a (a u16) below the ships' count with some hull ([+0x18]
		// above 0, signed), its owner ([+0x0]).
		SaveGame& s = r.save;
		r.var(2) = s.globals.u16(global::kWorldMerchantCount);
		const uint16_t ship = uint16_t(r.var(1));
		if (ship < s.globals.u16(global::kShipCount) && ship < s.ships.size() && s.ships[ship].memory.s32(0x18) > 0) {
			r.var(2) = s.ships[ship].memory.u8(0x0);
		}
		return Step::Next;
	}
	case 0x2A: {
		// 0x4EFDDF: c ← whether the ship b (a u16, below the ships' count) is in the town a (its [+0x39], a byte) in a
		// state below 2, 4 to 8 or 0x10; 0 otherwise.
		SaveGame& s = r.save;
		const uint16_t ship = uint16_t(r.var(2));
		uint32_t in = 0;
		if (ship < s.globals.u16(global::kShipCount) && ship < s.ships.size() &&
			s.ships[ship].memory.u8(0x39) == uint8_t(r.var(1))) {
			const uint16_t state = s.ships[ship].memory.u16(0x134);
			in = state < 2 || (state >= 4 && state <= 8) || state == 0x10 ? 1 : 0;
		}
		r.var(3) = in;
		return Step::Next;
	}
	case 0x42: {
		// 0x4F1514: b ← the ship a's hull in hundredths ([+0x18] · 100 / [+0x14], signed), 1 for a ship with some hull
		// that rounds to 0. (A ship past them: the game reads past the array; a [+0x14] of 0: it divides by 0.)
		const uint32_t ship = r.var(1);
		if (ship >= r.save.ships.size()) {
			r.var(2) = 0;
			return Step::Next;
		}
		const ShipMemory& m = r.save.ships[ship].memory;
		const int32_t max = m.s32(0x14);
		const int32_t hull = int32_t(uint32_t(m.s32(0x18)) * 100u);
		int32_t percent = max != 0 && !(max == -1 && hull == INT32_MIN) ? hull / max : 0;
		if (percent == 0 && m.s32(0x18) > 0) {
			percent = 1;
		}
		r.var(2) = uint32_t(percent);
		return Step::Next;
	}
	case 0x51: {
		// 0x4F1DCA: the first ship whose [+0x42] is v (a u16) in the state 0x12 (a pirate alone at sea) sinks: its hull
		// [+0x18] to 0 and 0x517770 with 0x100.
		SaveGame& s = r.save;
		const uint16_t which = uint16_t(r.var(1));
		const uint16_t ships = s.globals.u16(global::kShipCount);
		for (uint16_t i = 0; i < ships && i < s.ships.size(); ++i) {
			ShipMemory& m = s.ships[i].memory;
			if (m.u16(0x42) == which && m.u16(0x134) == 0x12) {
				m.s32(0x18) = 0;
				tick::ship_ai_detail::fn_517770(s, int(i), 0x100);
				break;
			}
		}
		return Step::Next;
	}
	case 0x6E: {
		// 0x4F03AC: of the merchant a's ships (from his [+0xE] by [+0x4], below the ships' count) in the town b (its
		// [+0x39], a byte) in a state below 4 without the bit 1 of [+0x136] — and, of a type ([+0xE]) above 1, unless
		// the town e (a byte) has the bit 1 of its [+0x2C8] — the one with the most room (0x5182B0, compared unsigned:
		// an overloaded ship's negative room wins): c ← it, d ← its room / 2000 (unsigned); c −1 and d 0 if none. (A
		// merchant or town past theirs: the game reads past them.)
		SaveGame& s = r.save;
		const uint32_t who = r.var(1);
		const uint8_t town = uint8_t(r.var(2));
		const uint8_t gate = uint8_t(r.var(5));
		r.var(3) = 0xFFFFFFFFu;
		r.var(4) = 0;
		uint32_t best = 0;
		const uint16_t ships = s.globals.u16(global::kShipCount);
		uint16_t id = who < s.merchants.size() ? s.merchants[who].first_ship() : 0xFFFF;
		for (; id < ships && id < s.ships.size(); id = s.ships[id].memory.u16(0x4)) {
			SavedShip& sh = s.ships[id];
			const ShipMemory& m = sh.memory;
			if (m.u8(0x39) != town || m.u16(0x134) >= 4 || (m.u8(0x136) & 1)) {
				continue;
			}
			if (m.u8(0xE) >= 2 && gate < s.towns.size() && (s.towns[gate].memory.u32(0x2C8) & 1)) {
				continue;
			}
			const uint32_t room = uint32_t(recalculate_cargo(sh));
			if (room > best) {
				best = uint32_t(recalculate_cargo(sh));
				r.var(3) = id;
				r.var(4) = best / 2000;
			}
		}
		return Step::Next;
	}
	case 0x22: {
		// 0x4EF518: v ← (now + the event list's [+0x8]) · (the town 3's [+0x2D4] + 3) (32 bits, signed), modulo 100
		// (unsigned).
		const SaveGame& s = r.save;
		uint32_t n = s.globals.u32(global::kWorldTick) + s.globals.u16(0x6DD744);
		n *= uint32_t((s.towns.size() > 3 ? s.towns[3].memory.s32(0x2D4) : 0) + 3);
		r.var(1) = n % 100;
		return Step::Next;
	}
	case 0x1F: {
		// 0x4EF788: c ← −1, then the first ship of the merchant a (a u16 below the count; from his [+0xE] by [+0x4],
		// below the ships' count) near the treasure spot b (a u16, 0x672340): the squares of its [+0x1E] and [+0x22]
		// (s16) less the spot's x and y below 27 (80 in the mission 308). (A spot past the 100: the game reads the
		// tables after it; here none is near.)
		SaveGame& s = r.save;
		r.var(3) = 0xFFFFFFFFu;
		const uint16_t who = uint16_t(r.var(1));
		uint16_t x = 0;
		uint16_t y = 0;
		if (who >= s.globals.u16(global::kWorldMerchantCount) || who >= s.merchants.size() ||
			!treasure_spot(uint16_t(r.var(2)), x, y)) {
			return Step::Next;
		}
		const int32_t near = r.e().u16(0x10) == 0x134 ? 0x50 : 0x1B;
		const uint16_t ships = s.globals.u16(global::kShipCount);
		for (uint16_t id = s.merchants[who].first_ship(); id < ships && id < s.ships.size();
			 id = s.ships[id].memory.u16(0x4)) {
			const ShipMemory& m = s.ships[id].memory;
			const int32_t dx = int32_t(int16_t(m.u16(0x1E))) - int32_t(x);
			const int32_t dy = int32_t(int16_t(m.u16(0x22))) - int32_t(y);
			if (int32_t(uint32_t(dy) * uint32_t(dy) + uint32_t(dx) * uint32_t(dx)) < near) {
				r.var(3) = id;
				break;
			}
		}
		return Step::Next;
	}
	case 0x2C: {
		// 0x4F0692: c ← the state of the captain b (0x4D7770 on the mailbox, for the merchant a): 0 when b is −1; for
		// a captain (b's low u16 below the count, his [+0x3] its third byte and his [+0x2] its fourth), 2 if the
		// merchant (below the count) has in his chain (at most the mailbox's count of steps) a letter 0x54 for him
		// ([+0xA] b's low u16, [+0x8] its high one), else 0 if a ship of his ([+0x42]) sails as a pirate (the state
		// 0x12), else 1; 1 for anything else.
		SaveGame& s = r.save;
		const GlobalMemory& g = s.globals;
		const uint32_t key = r.var(2);
		const uint32_t who = r.var(1);
		const uint16_t captain = uint16_t(key);
		uint32_t state = 1;
		if (key == 0xFFFFFFFFu) {
			state = 0;
		} else if (captain < g.u16(global::kCaptainCount) && captain < s.captains.size() &&
				   s.captains[captain].u8(0x3) == uint8_t(key >> 16) && s.captains[captain].u8(0x2) == uint8_t(key >> 24)) {
			bool letter = false;
			if (who < g.u16(global::kWorldMerchantCount) && who < s.merchants.size()) {
				const uint16_t count = g.u16(0x6DD736);
				uint32_t at = s.merchants[who].memory.u16(0xA);
				for (int32_t left = count; at < count && at < s.mail.size() && left > 0; --left) {
					const GameMemory<0x10>& m = s.mail[at];
					if (m.u8(4) == 0x54 && m.u16(0xA) == captain && m.u16(0x8) == uint16_t(key >> 16)) {
						letter = true;
						break;
					}
					at = m.u16(6);
				}
			}
			if (letter) {
				state = 2;
			} else {
				const uint16_t ships = g.u16(global::kShipCount);
				for (uint16_t i = 0; i < ships && i < s.ships.size(); ++i) {
					if (s.ships[i].memory.u16(0x42) == captain && s.ships[i].memory.u16(0x134) == 0x12) {
						state = 0;
						break;
					}
				}
			}
		}
		r.var(3) = state;
		return Step::Next;
	}
	case 0x41: {
		// 0x4F1409: the ship b's hull set to a % of its full hull ([+0x18] = a · [+0x14] / 100, signed) and 0x517770 with
		// 0x100, c ← 1, when its state (in a convoy, its guide's: the convoy's [+0x10]) is below 4 or 8, 9, 0xF, 0x12 or
		// 0x14; else c ← 0. (A ship past them: the game reads past the array.)
		SaveGame& s = r.save;
		const uint32_t ship = r.var(2);
		if (ship >= s.ships.size()) {
			r.var(3) = 0;
			return Step::Next;
		}
		ShipMemory& m = s.ships[ship].memory;
		uint16_t state = m.u16(0x134);
		const uint16_t convoy = m.u16(0x8);
		if (convoy < s.convoy_count() && convoy < s.convoys.size()) {
			const uint16_t guide = s.convoys[convoy].u16(0x10);
			state = guide < s.ships.size() ? s.ships[guide].memory.u16(0x134) : 0;
		}
		if (state < 4 || state == 8 || state == 9 || state == 0xF || state == 0x12 || state == 0x14) {
			m.s32(0x18) = int32_t(r.var(1) * uint32_t(m.s32(0x14))) / 100;
			tick::ship_ai_detail::fn_517770(s, int(ship), 0x100);
			r.var(3) = 1;
		} else {
			r.var(3) = 0;
		}
		return Step::Next;
	}
	case 0x46: {
		// 0x4F16B3: a commission for the town a: a new event (0x4D8CF0 at now + 1, the queue's [+0x4] 0 meanwhile) of
		// the kind 5 with [+0x8] a, [+0x9] b, [+0xA] 0xFF, [+0xB] b, [+0xC] now, [+0x10] 8, [+0x11] 0, [+0x12] and
		// [+0x14] the low halves of the [+0x1C] and [+0x20] of the ships' object's ship [+0xEA] (else [+0xE8], else the
		// ship 0), [+0x16] 0xFF.
		SaveGame& s = r.save;
		GlobalMemory& g = s.globals;
		const uint32_t now = g.u32(global::kWorldTick);
		g.u32(0x6DD740) = 0;
		const uint16_t made = tick::events_detail::fn_4d8cf0(s, now + 1);
		g.u32(0x6DD740) = 1;
		const uint16_t ships = g.u16(global::kShipCount);
		uint16_t ship = g.u16(global::kShips + 0xEA);
		if (ship >= ships) {
			ship = g.u16(global::kShips + 0xE8) < ships ? g.u16(global::kShips + 0xE8) : 0;
		}
		GameMemory<0x18>& e = s.events[made];
		e.u16(0x6) = 5;
		e.u8(0x8) = uint8_t(r.var(1));
		e.u8(0x9) = uint8_t(r.var(2));
		e.u8(0xA) = 0xFF;
		e.u8(0xB) = uint8_t(r.var(2));
		e.u32(0xC) = now;
		e.u8(0x10) = 8;
		e.u8(0x11) = 0;
		e.u16(0x12) = ship < s.ships.size() ? s.ships[ship].memory.u16(0x1C) : 0;
		e.u16(0x14) = ship < s.ships.size() ? s.ships[ship].memory.u16(0x20) : 0;
		e.u16(0x16) = 0xFF;
		return Step::Next;
	}
	case 0x47:
	case 0x48: {
		// 0x4F17C0 and 0x4F188D: the office of the merchant a in the town b (0x5308A0 with nothing made, the two as
		// u16): 0x47 d ← its stock of the ware c ([+0x4 + 4 · c]) in loads (/ 2000) or barrels (/ 200, 0x672C14),
		// signed; 0x48 its [+0x2E8] += d loads (· 2000) or barrels (· 200) of c. (No office: the game reads through a
		// null pointer; a ware past them: past the tables.)
		static constexpr uint8_t kBarrels[kWareCount] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
		SaveGame& s = r.save;
		const int office = tick::facilities_detail::fn_5308a0(s, uint16_t(r.var(1)), uint16_t(r.var(2)));
		const uint32_t ware = r.var(3);
		if (office < 0 || size_t(office) >= s.offices.size() || ware >= uint32_t(kWareCount)) {
			if (op == 0x47) {
				r.var(4) = 0;
			}
			return Step::Next;
		}
		OfficeMemory& o = s.offices[size_t(office)].memory;
		if (op == 0x47) {
			r.var(4) = uint32_t(o.s32(4 + 4 * ware) / (kBarrels[ware] ? 200 : 2000));
		} else {
			o.s32(0x2E8) = int32_t(uint32_t(o.s32(0x2E8)) + r.var(4) * (kBarrels[ware] ? 200u : 2000u));
		}
		return Step::Next;
	}
	case 0x49: {
		// 0x4F192D: c ← what b loads (or barrels) of the ware a are worth to the pirates: b (a float) · 2000 (200 in
		// barrels) · 0x673A18[a], truncated (__ftol, at the double precision the game runs the x87). (A ware past them:
		// past the tables.)
		static constexpr uint8_t kBarrels[kWareCount] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
		static constexpr float kWorth[kWareCount] = {0.055000003f, 0.47855002f, 0.22005001f, 0.174f, 0.1425f, 0.55f,
			1.4f, 1.1f, 1.034f, 3.3825f, 0.41249999f, 0.027500002f, 1.278f, 1.12f, 0.44000003f, 0.278f, 0.44000003f,
			0.22000001f, 0.85499996f, 0.039900005f, 10.0f, 8.0f, 12.0f, 14.0f};
		const uint32_t ware = r.var(1);
		if (ware >= uint32_t(kWareCount)) {
			r.var(3) = 0;
			return Step::Next;
		}
		const double worth = double(float(int32_t(r.var(2)))) * double(float(kBarrels[ware] ? 200 : 2000)) *
			double(kWorth[ware]);
		r.var(3) = uint32_t(int32_t(worth));
		return Step::Next;
	}
	case 0x4C: {
		// 0x4EE30B: c loads (or barrels) of the ware b (a byte) into the town a's stock (a byte; [+0x4 + 4 · ware]), out
		// with c negative (at most what it has); d ← what moved, in loads or barrels. (A town or ware past theirs: the
		// game reads past them.)
		SaveGame& s = r.save;
		const uint8_t town = uint8_t(r.var(1));
		const uint8_t ware = uint8_t(r.var(2));
		if (town >= s.towns.size() || ware >= kWareCount) {
			r.var(4) = 0;
			return Step::Next;
		}
		r.var(4) = uint32_t(move_stock(s.towns[town].memory.s32(4 + 4 * ware), ware, r.var(3)));
		return Step::Next;
	}
	case 0xEE: {
		// 0x4EE44A: the same into the office of the merchant a in the town b (bytes; 0x5308A0 with nothing made): d
		// loads (or barrels) of the ware c, e ← what moved; 0 without an office.
		SaveGame& s = r.save;
		const int office = tick::facilities_detail::fn_5308a0(s, uint8_t(r.var(1)), uint8_t(r.var(2)));
		const uint8_t ware = uint8_t(r.var(3));
		if (office < 0 || size_t(office) >= s.offices.size() || ware >= kWareCount) {
			r.var(5) = 0;
			return Step::Next;
		}
		r.var(5) = uint32_t(move_stock(s.offices[size_t(office)].memory.s32(4 + 4 * ware), ware, r.var(4)));
		return Step::Next;
	}
	case 0x4D: {
		// 0x4F1A96: the merchant a's float [+0x120 + 12 · c] += b / 1000 (floats; 0x4F3770). (Past his memory: the game
		// writes on.)
		SaveGame& s = r.save;
		const uint32_t m = r.var(1);
		const size_t at = 0x120 + 12 * size_t(r.var(3));
		if (m < s.merchants.size() && at + 4 <= MerchantMemory::kSize) {
			float& f = s.merchants[m].memory.f32(at);
			f = f + float(int32_t(r.var(2))) / 1000.0f;
		}
		return Step::Next;
	}
	case 0x4E: {  // 0x4F1B13: b ← the ship a's used hold in barrels ([+0x118] / 200, signed). (Past them: read on.)
		const uint32_t ship = r.var(1);
		r.var(2) = ship < r.save.ships.size() ? uint32_t(r.save.ships[ship].memory.s32(0x118) / 200) : 0;
		return Step::Next;
	}
	case 0x50: {
		// 0x4F1C74: e ← the sum over the merchant a's letters 0x72 for b (a u16 in their [+0x8]) dated (0x531260 on
		// their day, month & 0x7F and year) at c's tick with its low byte cleared or later (unsigned) of, by the bits
		// of d, 1 their [+0xE], 2 their [+0xC], 4 their [+0xA]. (A merchant past them: the game reads past the array.)
		SaveGame& s = r.save;
		r.var(5) = 0;
		const uint32_t who = r.var(1);
		if (who >= s.merchants.size()) {
			return Step::Next;
		}
		const uint16_t which = uint16_t(r.var(2));
		const uint32_t from = r.var(3) & 0xFFFFFF00u;
		const uint8_t bits = uint8_t(r.var(4));
		const uint16_t count = s.globals.u16(kMailCount);
		uint32_t sum = 0;
		uint32_t at = s.merchants[who].memory.u16(0xA);
		for (uint32_t guard = 0; at < count && at < s.mail.size() && guard < 0x10000; ++guard) {
			const GameMemory<0x10>& m = s.mail[at];
			if (m.u8(4) == 0x72 && m.u16(8) == which && fn_531260(m.u8(0), uint8_t(m.u8(1) & 0x7F), m.u16(2)) >= from) {
				sum += (bits & 1) ? m.u16(0xE) : 0;
				sum += (bits & 2) ? m.u16(0xC) : 0;
				sum += (bits & 4) ? m.u16(0xA) : 0;
			}
			at = m.u16(6);
		}
		r.var(5) = sum;
		return Step::Next;
	}
	case 0x54: {
		// 0x4EFA54: c ← −1, then the first ship of the merchant a (from his [+0xE] by [+0x4], below the ships' count) in
		// the town b (its [+0x39]) that can lead a convoy (0x519C40), with its [+0x136] 0 and a state below 4. (A
		// merchant past them: the game reads past the array.)
		SaveGame& s = r.save;
		r.var(3) = 0xFFFFFFFFu;
		const uint32_t who = r.var(1);
		const uint32_t town = r.var(2);
		const uint16_t ships = s.globals.u16(global::kShipCount);
		uint16_t id = who < s.merchants.size() ? s.merchants[who].first_ship() : 0xFFFF;
		for (; id < ships && id < s.ships.size(); id = s.ships[id].memory.u16(0x4)) {
			const ShipMemory& m = s.ships[id].memory;
			if (m.u8(0x39) == town && tick::ship_ai_detail::fn_519c40(s, id) && m.u8(0x136) == 0 && m.u16(0x134) < 4) {
				r.var(3) = id;
				break;
			}
		}
		return Step::Next;
	}
	case 0x61: {
		// 0x4F2512: the merchant a's [+0x16] (s16) + b (a s16), held at 0x7FFF and −0x8000 (0x4F36E0). (A merchant
		// past them: the game writes past the array.)
		const uint32_t who = r.var(1);
		if (who < r.save.merchants.size()) {
			uint16_t& v = r.save.merchants[who].memory.u16(0x16);
			const int32_t now = int16_t(v);
			const int32_t by = int16_t(uint16_t(r.var(2)));
			if (by > 0) {
				v = now < 0x7FFF - by ? uint16_t(now + by) : 0x7FFF;
			} else {
				v = now + 0x8000 > -by ? uint16_t(now + by) : 0x8000;
			}
		}
		return Step::Next;
	}
	case 0x69: {
		// 0x4F2A89: a new event (0x4D8CF0 at now + 1, the queue's [+0x4] 0 meanwhile) of the kind 0xF with [+0x8] now's
		// low half, [+0xA] the variable a's low byte, [+0xC] b's, [+0xB] c's.
		SaveGame& s = r.save;
		GlobalMemory& g = s.globals;
		const uint32_t now = g.u32(global::kWorldTick);
		g.u32(0x6DD740) = 0;
		const uint16_t made = tick::events_detail::fn_4d8cf0(s, now + 1);
		g.u32(0x6DD740) = 1;
		GameMemory<0x18>& e = s.events[made];
		e.u16(0x8) = uint16_t(now);
		e.u8(0xA) = uint8_t(r.var(1));
		e.u8(0xC) = uint8_t(r.var(2));
		e.u8(0xB) = uint8_t(r.var(3));
		e.u16(0x6) = 0xF;
		return Step::Next;
	}
	case 0x6A: {
		// 0x4F2B3A: c ← 0, then, for b not negative, the (b + 1)th merchant (by number) with an office in the town a (a
		// u16; 0x5308A0 with nothing made), or the last there is.
		SaveGame& s = r.save;
		r.var(3) = 0;
		int32_t left = int32_t(r.var(2));
		const uint16_t town = uint16_t(r.var(1));
		for (uint16_t m = 0; left >= 0 && m < s.globals.u16(global::kWorldMerchantCount); ++m) {
			if (tick::facilities_detail::fn_5308a0(s, m, town) >= 0) {
				r.var(3) = m;
				--left;
			}
		}
		return Step::Next;
	}
	case 0x6C: {
		// 0x4F2BDA: the ship a (a byte, below the ships' count): the bit 0x40 of its [+0x136] set with b's low byte, else
		// cleared.
		SaveGame& s = r.save;
		const uint8_t ship = uint8_t(r.var(1));
		if (ship < s.globals.u16(global::kShipCount) && ship < s.ships.size()) {
			uint8_t& f = s.ships[ship].memory.u8(0x136);
			f = uint8_t(r.var(2)) != 0 ? uint8_t(f | 0x40) : uint8_t(f & 0xBF);
		}
		return Step::Next;
	}
	case 0x6D: {
		// 0x4EFF63: c ← −1, then, for the merchant a below the count (signed), his ship (from his [+0xE] by [+0x4]) in the
		// town b (a byte, its [+0x39]) in a state below 4 without the bit 1 of [+0x136] — of a type above 1, unless the
		// town d (a byte) has the bit 1 of [+0x2C8] — with the most firepower ([+0x120], signed, above −1).
		SaveGame& s = r.save;
		r.var(3) = 0xFFFFFFFFu;
		const int32_t who = int32_t(r.var(1));
		if (who >= int32_t(s.globals.u16(global::kWorldMerchantCount)) || who < 0 || size_t(who) >= s.merchants.size()) {
			return Step::Next;  // (Negative: the game reads before the array.)
		}
		const uint8_t town = uint8_t(r.var(2));
		const uint8_t gate = uint8_t(r.var(4));
		int32_t best = -1;
		const uint16_t ships = s.globals.u16(global::kShipCount);
		for (uint16_t id = s.merchants[size_t(who)].first_ship(); id < ships && id < s.ships.size();
			 id = s.ships[id].memory.u16(0x4)) {
			const ShipMemory& m = s.ships[id].memory;
			if (m.u8(0x39) != town || m.u16(0x134) >= 4 || (m.u8(0x136) & 1)) {
				continue;
			}
			if (m.u8(0xE) >= 2 && gate < s.towns.size() && (s.towns[gate].memory.u32(0x2C8) & 1)) {
				continue;
			}
			if (m.s32(0x120) > best) {
				best = m.s32(0x120);
				r.var(3) = id;
			}
		}
		return Step::Next;
	}
	case 0x6F: {
		// 0x4EFCB9: as 0x29, for the ship f (a u16): the town after d (0x533210 on d with a, b and c left out, wandering
		// by e, the ship's type and towns' bit 1 left out too); then a ← b, b ← c, c ← d and d ← it.
		const uint32_t seen[3] = {r.var(1), r.var(2), r.var(3)};
		const uint8_t next = fn_533210(r.save, r.var(4), seen, 3, r.var(5) != 0, uint16_t(r.var(6)));
		r.var(1) = r.var(2);
		r.var(2) = r.var(3);
		r.var(3) = r.var(4);
		r.var(4) = next;
		return Step::Next;
	}
	case 0x08: {
		// 0x4EED7B: v ← in a game of more than one human (the settings' [+0xC] above 1) the human merchant (kind 0) with
		// the most [+0x46C] (signed, above −2³¹; none: the game keeps an uninitialized byte of its frame, here 0),
		// else the player (*0x6DFC14), as a byte.
		SaveGame& s = r.save;
		uint8_t who = 0;
		if (s.settings.u8(0xC) > 1) {
			int32_t best = INT32_MIN;
			for (uint16_t m = 0; m < s.globals.u16(global::kWorldMerchantCount) && m < s.merchants.size(); ++m) {
				const MerchantMemory& mm = s.merchants[m].memory;
				if (mm.u16(0x8) == 0 && mm.s32(0x46C) > best) {
					best = mm.s32(0x46C);
					who = uint8_t(m);
				}
			}
		} else {
			who = s.globals.u8(0x6DFC14);
		}
		r.var(1) = who;
		return Step::Next;
	}
	case 0x2E: {
		// 0x4F0711: c ← the merchant a's reputation in the town b as 16.16 (0x4F3740: his float [+0x2FC + 4 · b] ·
		// 65536, truncated). (Past them: the game reads on.)
		const uint32_t m = r.var(1);
		const size_t at = 0x2FC + 4 * size_t(r.var(2));
		r.var(3) = m < r.save.merchants.size() && at + 4 <= MerchantMemory::kSize
			? uint32_t(int32_t(double(r.save.merchants[m].memory.f32(at)) * 65536.0))
			: 0;
		return Step::Next;
	}
	case 0x56:  // 0x4F1F14: v ← the Hanse's town (the world's [+0x8E]).
		r.var(1) = r.save.globals.u8(global::kWorld + 0x8E);
		return Step::Next;
	case 0x58: {  // 0x4F1F2C: b ← the variable whose number a holds. (Past them: the game reads past the buffer.)
		const uint32_t k = r.var(1);
		r.var(2) = k < r.vars.size() ? r.vars[k] : 0;
		return Step::Next;
	}
	case 0x59: {  // 0x4F1F47: the variable whose number a holds ← b. (Past them: the game writes past the buffer.)
		const uint32_t k = r.var(1);
		const uint32_t v = r.var(2);
		if (k < r.vars.size()) {
			r.vars[k] = v;
		}
		return Step::Next;
	}
	case 0xEB: {
		// 0x4EE984: the ship a (a u16 below the ships' count): its sailors ([+0x40]) ← b (a u16), its hold again
		// (0x5182B0).
		SaveGame& s = r.save;
		const uint16_t ship = uint16_t(r.var(1));
		if (ship < s.globals.u16(global::kShipCount) && ship < s.ships.size()) {
			s.ships[ship].memory.u16(0x40) = uint16_t(r.var(2));
			recalculate_cargo(s.ships[ship]);
		}
		return Step::Next;
	}
	case 0x3E: {
		// 0x4F11A5: c ← the ship a's cargo of the ware b ([+0x54 + 4 · b]) in loads (/ 2000) or barrels (/ 200,
		// 0x672C14), signed. (A ship or ware past theirs: the game reads past them.)
		static constexpr uint8_t kBarrels[kWareCount] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
		const uint32_t ship = r.var(1);
		const uint32_t ware = r.var(2);
		r.var(3) = ship < r.save.ships.size() && ware < uint32_t(kWareCount)
			? uint32_t(r.save.ships[ship].memory.s32(0x54 + 4 * ware) / (kBarrels[ware] ? 200 : 2000))
			: 0;
		return Step::Next;
	}
	case 0x5A:
	case 0x57: {
		// 0x4F00AC and 0x4F01AA: of the merchant a's ships (from his [+0xE] by [+0x4], below the ships' count) in the
		// town b (a byte, their [+0x39]) in a state below 4 without the bit 1 of [+0x136]: 0x5A d ← the one with the
		// most of the ware c (a byte; [+0x54 + 4 · c], unsigned, above 0); 0x57 c ← the one whose speed (0x5170F0) is
		// above the record (unsigned, from 0), the record then its firepower [+0x120] — not its speed: it looks like a
		// game bug (the roadmap's list). −1 if none. (A merchant past them: the game reads past the array.)
		SaveGame& s = r.save;
		const uint32_t out = op == 0x5A ? 4 : 3;
		r.var(out) = 0xFFFFFFFFu;
		const uint32_t who = r.var(1);
		const uint8_t town = uint8_t(r.var(2));
		const uint8_t ware = uint8_t(r.var(3));
		uint32_t best = 0;
		const uint16_t ships = s.globals.u16(global::kShipCount);
		uint16_t id = who < s.merchants.size() ? s.merchants[who].first_ship() : 0xFFFF;
		for (; id < ships && id < s.ships.size(); id = s.ships[id].memory.u16(0x4)) {
			const SavedShip& sh = s.ships[id];
			const ShipMemory& m = sh.memory;
			if (m.u8(0x39) != town || m.u16(0x134) >= 4 || (m.u8(0x136) & 1)) {
				continue;
			}
			if (op == 0x5A) {
				const uint32_t cargo = ware < 0x40 ? m.u32(0x54 + 4 * ware) : 0;
				if (cargo > best) {
					best = cargo;
					r.var(out) = id;
				}
			} else if (Sailing::speed(sh) > best) {
				best = m.u32(0x120);
				r.var(out) = id;
			}
		}
		return Step::Next;
	}
	case 0x36: {
		// 0x4F0A6F: b ← one of the merchant a's ships (from his [+0xE] by [+0x4], below the ships' count) out of a convoy
		// with hull ([+0x18] above 0) and sailors ([+0x40]) in the states of the mode c (0 the state 0xF, 1 up to 3,
		// above 1 either; another none): (now + the event list's [+0x8] + the town 0's [+0x2D4]) modulo how many (a
		// u16, unsigned) of them, counted again in a second pass whose convoy check is `jbe` where the first's is `jb`
		// (a ship of the convoy number equal to the count passes the first, not the second), and when that pass ends
		// before the pick, the list's end ([+0x4], a u16). −1 if none.
		SaveGame& s = r.save;
		const GlobalMemory& g = s.globals;
		const uint32_t who = r.var(1);
		const int32_t mode = int32_t(r.var(3));
		const uint16_t ships = g.u16(global::kShipCount);
		const uint16_t convoys = s.convoy_count();
		auto fits = [&](const ShipMemory& m) {
			const uint16_t state = m.u16(0x134);
			const bool in_mode = mode == 0 ? state == 0xF : mode == 1 ? state <= 3 : mode > 1 && (state == 0xF || state <= 3);
			return m.s32(0x18) > 0 && in_mode && m.u16(0x40) > 0;
		};
		const uint16_t first = who < s.merchants.size() ? s.merchants[who].first_ship() : 0xFFFF;
		uint32_t out = 0xFFFFFFFFu;
		uint16_t count = 0;
		for (uint16_t id = first; id < ships && id < s.ships.size(); id = s.ships[id].memory.u16(0x4)) {
			const ShipMemory& m = s.ships[id].memory;
			if (m.u16(0x8) >= convoys && fits(m)) {
				++count;
			}
		}
		if (count != 0) {
			const uint32_t mixed = g.u32(global::kWorldTick) + g.u16(0x6DD744) +
				(s.towns.empty() ? 0u : uint32_t(s.towns[0].memory.s32(0x2D4)));
			uint16_t pick = uint16_t(mixed % count);
			uint32_t id = first;
			while (id < ships && id < s.ships.size()) {
				const ShipMemory& m = s.ships[id].memory;
				if (m.u16(0x8) > convoys && fits(m)) {
					if (pick == 0) {
						break;
					}
					--pick;
				}
				id = m.u16(0x4);
			}
			out = id;
		}
		r.var(2) = out;
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
	Run r{save, event, *program, script.variables};
	const uint32_t now = save.globals.u32(global::kWorldTick);
	uint8_t op = r.b(0);
	// (A tool's trace, P3_MISSION_TRACE set: each instruction as re/emu/sesion47/optrace.py prints the game's.)
	static const bool trace = std::getenv("P3_MISSION_TRACE") != nullptr;
	while (op != 0 && op < 0xFD) {
		if (trace) {
			std::fprintf(stderr, "tick %u event %u mission %u section %u op %#04x", unsigned(now), unsigned(event),
				unsigned(r.e().u16(0x10)), unsigned(r.e().u16(0x14)), unsigned(op));
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
			r.e().u16(0x14) = uint16_t(r.e().u16(0x14) + 1);
		}
		op = r.b(0);
	}
	switch (op) {
	case 0xFD:
		// Wait so many ticks (the variable of its byte 1), then go on at its section (u16 at its byte 2).
		r.e().u32(0) = r.var(1) + now;
		r.e().u16(0x14) = r.w(2);
		return 1;
	case 0x00: {
		// Wait for the tick of its variable; one that has passed (with a day's grace) ends the script.
		const uint32_t tick = r.var(1);
		if (tick - 0x100 < now) {
			end_script(save, event, script);
			return 0;
		}
		r.e().u32(0) = tick;
		r.e().u16(0x14) = uint16_t(r.e().u16(0x14) + 1);
		return 1;
	}
	case 0xFE:
		// Wait so many days, then start again at the section 0.
		r.e().u32(0) = (r.var(1) << 8) + now;
		r.e().u16(0x14) = 0;
		return 1;
	default:  // 0xFF: the end.
		end_script(save, event, script);
		return 0;
	}
}

}  // namespace p3::data::missions_detail
