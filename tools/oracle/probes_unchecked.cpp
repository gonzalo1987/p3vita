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
