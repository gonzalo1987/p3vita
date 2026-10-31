// Probes for functions that the task cloud/unchecked-branches calls directly
// in its retouched states (tools/oracle/retouch_unchecked.py) or records on
// their own, so that a difference points at them and not at their caller.

#include <optional>

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

// What alderman.cpp exposes for them.
namespace p3::data::tick::alderman_detail {

void fn_51a750(SaveGame& save, int ship, uint8_t n);
void fn_51a4e0(SaveGame& save, int ship, uint32_t strength, uint8_t level);
void fn_516170(SaveGame& save, uint32_t pirate, uint32_t type, bool captain, uint32_t strength);
void fn_5144a0(SaveGame& save, uint32_t pirate, uint32_t nest);

}  // namespace p3::data::tick::alderman_detail

// What construction.cpp exposes for them.
namespace p3::data::tick::construction_detail {

bool fn_4f9c80(const SaveGame& save, int town, uint8_t& x, uint8_t& y, uint32_t type, uint8_t param, bool flag);
void fn_4fa7f0(const SaveGame& save, int town, uint8_t& x, uint8_t& y);
void fn_5250d0(const SaveGame& save, int town, uint8_t& x, uint8_t& y);
bool fn_51f3a0(SaveGame& save, int town, GameMemory<0x44>& s);
void fn_4f8a50(SaveGame& save, int merchant, uint32_t town, bool count);
void fn_4f8c70(SaveGame& save, int merchant, uint32_t town);
void fn_4f8560(SaveGame& save, int merchant, uint32_t town, int32_t amount);

}  // namespace p3::data::tick::construction_detail

// What merchants.cpp exposes for them.
namespace p3::data::tick::merchants_detail {

void fn_4f68d0(SaveGame& save, MerchantMemory& m, uint32_t tick, uint32_t a1, uint8_t a3, uint8_t a4);

}  // namespace p3::data::tick::merchants_detail

// What new_town.cpp exposes for them.
namespace p3::data::tick::new_town_detail {

void fn_5462f0(SaveGame& save, int town, uint32_t k);

}  // namespace p3::data::tick::new_town_detail

namespace p3::probe {

using namespace p3::data;

namespace {

// The pirate object at `address` (*[0x6DD7AC + 4·k]): k, or -1.
int pirate_at(const SaveGame& s, uint32_t address) {
	for (uint32_t k = 0; k < 5; ++k) {
		if (address != 0 && s.globals.u32(0x6DD7AC + 4 * k) == address && s.pirates[k]) {
			return int(k);
		}
	}
	return -1;
}

// The town whose town_class1 ([0x7A4]) is at `address`, or -1.
int town_class1(const ProbeCall& c, uint32_t address) {
	const Location l = c.locate(address);
	return l.kind == Location::Town && l.offset == 0x7A4 ? l.index : -1;
}

// Pointer arguments to the caller's variables can't be repeated; the
// scenarios of retouch_unchecked.py point them at scratch globals
// (0x6DFF00..0x6DFFFF, which the game doesn't use), which the state holds.
bool in_globals(const ProbeCall& c, uint32_t address) {
	return c.locate(address).kind == Location::Global;
}

}  // namespace

void register_probes_unchecked(ProbeRegistry& r) {
	namespace ad = tick::alderman_detail;
	r[0x51A4E0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the ship (strength, level).
		ad::fn_51a4e0(s, c.ship(c.ecx), c.args[0], uint8_t(c.args[1]));
		return std::nullopt;
	};
	r[0x51A750] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the ship (levels).
		ad::fn_51a750(s, c.ship(c.ecx), uint8_t(c.args[0]));
		return std::nullopt;
	};
	r[0x516170] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the pirate object (type, captain, strength); returns the
		// new ship's address, which isn't compared.
		const int k = pirate_at(s, c.ecx);
		if (k < 0) {
			c.skip = true;
			return std::nullopt;
		}
		ad::fn_516170(s, uint32_t(k), c.args[0], (c.args[1] & 0xFF) != 0, c.args[2] & 0xFFFF);
		return std::nullopt;
	};
	namespace cd = tick::construction_detail;
	r[0x4F9C80] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on town_class1 (&x, &y, type, param, flag); returns al.
		const int town = town_class1(c, c.ecx);
		if (town < 0 || !in_globals(c, c.args[0]) || !in_globals(c, c.args[1])) {
			c.skip = true;
			return std::nullopt;
		}
		c.eax_mask = 0xFF;
		uint8_t x = s.globals.u8(c.args[0]);
		uint8_t y = s.globals.u8(c.args[1]);
		const bool found = cd::fn_4f9c80(s, town, x, y, c.args[2], uint8_t(c.args[3]), (c.args[4] & 0xFF) != 0);
		s.globals.u8(c.args[0]) = x;
		s.globals.u8(c.args[1]) = y;
		return found ? 1 : 0;
	};
	r[0x4FA7F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on town_class1 (&x, &y).
		const int town = town_class1(c, c.ecx);
		if (town < 0 || !in_globals(c, c.args[0]) || !in_globals(c, c.args[1])) {
			c.skip = true;
			return std::nullopt;
		}
		uint8_t x = s.globals.u8(c.args[0]);
		uint8_t y = s.globals.u8(c.args[1]);
		cd::fn_4fa7f0(s, town, x, y);
		s.globals.u8(c.args[0]) = x;
		s.globals.u8(c.args[1]) = y;
		return std::nullopt;
	};
	r[0x5250D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the town (&x, &y).
		const int town = c.town(c.ecx);
		if (town < 0 || !in_globals(c, c.args[0]) || !in_globals(c, c.args[1])) {
			c.skip = true;
			return std::nullopt;
		}
		uint8_t x = s.globals.u8(c.args[0]);
		uint8_t y = s.globals.u8(c.args[1]);
		cd::fn_5250d0(s, town, x, y);
		s.globals.u8(c.args[0]) = x;
		s.globals.u8(c.args[1]) = y;
		return std::nullopt;
	};
	r[0x51F3A0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the town (pointer to the game's 0x44-byte structure); returns al.
		const int town = c.town(c.ecx);
		if (town < 0 || !in_globals(c, c.args[0]) || !in_globals(c, c.args[0] + 0x43)) {
			c.skip = true;
			return std::nullopt;
		}
		c.eax_mask = 0xFF;
		GameMemory<0x44> st;
		for (uint32_t k = 0; k < 0x44; ++k) {
			st.u8(k) = s.globals.u8(c.args[0] + k);
		}
		const bool can = cd::fn_51f3a0(s, town, st);
		for (uint32_t k = 0; k < 0x44; ++k) {
			s.globals.u8(c.args[0] + k) = st.u8(k);
		}
		return can ? 1 : 0;
	};
	r[0x4F8A50] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the merchant (town, count).
		cd::fn_4f8a50(s, c.merchant(c.ecx), c.args[0] & 0xFF, (c.args[1] & 0xFF) != 0);
		return std::nullopt;
	};
	r[0x4F8C70] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the merchant (town).
		cd::fn_4f8c70(s, c.merchant(c.ecx), c.args[0] & 0xFF);
		return std::nullopt;
	};
	r[0x4F8560] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the merchant (its index, for the letter; town, amount).
		cd::fn_4f8560(s, c.merchant(c.ecx), c.args[1], int32_t(c.args[2]));
		return std::nullopt;
	};
	r[0x4F68D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the merchant (tick, a1, the merchant, a3, a4): a new life.
		const int m = c.merchant(c.ecx);
		tick::merchants_detail::fn_4f68d0(s, s.merchants[size_t(m)].memory, c.args[0], c.args[1], uint8_t(c.args[3]),
			uint8_t(c.args[4]));
		return std::nullopt;
	};
	r[0x5462F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the town (the facility k): a node at the front of [0x9B4].
		tick::new_town_detail::fn_5462f0(s, c.town(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x5144A0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the pirate object (its lair, u16).
		const int k = pirate_at(s, c.ecx);
		if (k < 0) {
			c.skip = true;
			return std::nullopt;
		}
		ad::fn_5144a0(s, uint32_t(k), c.args[0] & 0xFFFF);
		return std::nullopt;
	};
}

}  // namespace p3::probe
