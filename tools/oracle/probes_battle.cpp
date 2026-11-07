// Probes of src/p3data/battle/*.cpp (functions of Sailing: they need
// tick_probe --cpr, which builds the navigation of probe_sailing()).

#include "p3data/battle/pirate_nests.hpp"
#include "probe_registry.hpp"

namespace p3::probe {

using namespace p3::data;

namespace {

constexpr uint32_t kBad = 0xDEAD;  // The sample can't be repeated (without --cpr or without the object).

}  // namespace

void register_probes_battle(ProbeRegistry& r) {
	// 0x5053C0, the storms: thiscall on the ships object, no arguments.
	r[0x5053C0] = [](SaveGame&, const ProbeCall&) -> std::optional<uint32_t> {
		if (!probe_sailing()) {
			return kBad;
		}
		probe_sailing()->fn_5053c0();
		return std::nullopt;
	};
	// 0x505290: thiscall on the ships object (the ship, its index, the storm's x and y).
	r[0x505290] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int ship = c.ship(c.args[0]);
		if (!probe_sailing() || ship < 0) {
			return kBad;
		}
		probe_sailing()->fn_505290(uint16_t(ship), int32_t(c.args[2]), int32_t(c.args[3]));
		return std::nullopt;
	};
	// 0x612930: thiscall on the battles (0x6E55D0), the ship: its battle speed.
	r[0x612930] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing()) {
			return kBad;
		}
		return probe_sailing()->fn_612930(uint16_t(c.args[0]));
	};
	// 0x50D9E0: thiscall on the ships object, the guide of a pirate convoy; returns al.
	r[0x50D9E0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing()) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_50d9e0(uint16_t(c.args[0])) ? 1 : 0;
	};
	// 0x515360: cdecl, the guide of a pirate convoy; returns al.
	r[0x515360] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing()) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_515360(uint16_t(c.args[0])) ? 1 : 0;
	};
	// 0x505C90: thiscall on the ships object, the address of a variable of the
	// caller (0x506720) that holds the address of the convoy's link (the head
	// of the list in the globals or [+0x8] of the convoy before); returns al.
	r[0x505C90] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const std::optional<uint32_t> address = c.stack(c.args[0]);
		if (!address) {
			return kBad;
		}
		const Location l = c.locate(*address);
		uint16_t* link = nullptr;
		if (l.kind == Location::Global) {
			link = &save.globals.u16(l.offset);
		} else if (l.kind == Location::Convoy && l.offset == 0x8) {
			link = &save.convoys[size_t(l.index)].u16(0x8);
		}
		if (!probe_sailing() || !link || *link >= save.convoys.size()) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_505c90(*link, *link) ? 1 : 0;
	};
	// The pirate objects: ecx is one of *[0x6DD7AC + 4·k] (the globals of the
	// state hold the game's pointers).
	auto pirate = [](const SaveGame& save, uint32_t address) -> int {
		for (int k = 0; k < 5; ++k) {
			if (address != 0 && save.globals.u32(global::kShips + 0xC + 4 * uint32_t(k)) == address && save.pirates[size_t(k)]) {
				return k;
			}
		}
		return -1;
	};
	// 0x513BF0: thiscall on a pirate object; returns the address of the entry.
	r[0x513BF0] = [pirate](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int k = pirate(save, c.ecx);
		if (k < 0) {
			return kBad;
		}
		fn_513bf0(save, size_t(k));
		c.eax_mask = 0;
		return 0;
	};
	// 0x515DA0: thiscall on a pirate object.
	r[0x515DA0] = [pirate](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int k = pirate(save, c.ecx);
		if (k < 0) {
			return kBad;
		}
		fn_515da0(save, size_t(k));
		return std::nullopt;
	};
	r[0x515F70] = [pirate](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int k = pirate(save, c.ecx);
		if (k < 0) {
			return kBad;
		}
		fn_515f70(save, size_t(k));
		return std::nullopt;
	};
	// 0x60C1C9: thiscall on a battle, the winner's place, the taken one's.
	r[0x60C1C9] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_60c1c9(size_t(b), uint16_t(c.args[0]), uint16_t(c.args[1]));
		return std::nullopt;
	};
	// 0x514B80: thiscall on a pirate object, the convoy.
	r[0x514B80] = [pirate](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int k = pirate(save, c.ecx);
		if (k < 0 || !probe_sailing() || (c.args[0] & 0xFFFF) >= save.convoys.size()) {
			return kBad;
		}
		probe_sailing()->fn_514b80(size_t(k), uint16_t(c.args[0]));
		return std::nullopt;
	};
	// 0x514000: thiscall on a pirate object, its number.
	r[0x514000] = [pirate](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int k = pirate(save, c.ecx);
		if (k < 0 || !probe_sailing()) {
			return kBad;
		}
		probe_sailing()->fn_514000(size_t(k));
		return std::nullopt;
	};
	// 0x514920 (a nest after its town battle): thiscall on a pirate object.
	r[0x514920] = [pirate](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int k = pirate(save, c.ecx);
		if (k < 0 || !probe_sailing()) {
			return kBad;
		}
		probe_sailing()->fn_514920(size_t(k));
		return std::nullopt;
	};
	// 0x514530 (a nest's defence against an attack): thiscall on a pirate object.
	r[0x514530] = [pirate](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int k = pirate(save, c.ecx);
		if (k < 0 || !probe_sailing()) {
			return kBad;
		}
		probe_sailing()->fn_514530(size_t(k));
		return std::nullopt;
	};
	// 0x515C00 (a nest's tower burns): thiscall on a pirate object (the nest's number, x, y); returns al.
	r[0x515C00] = [pirate](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int k = pirate(save, c.ecx);
		if (k < 0) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return fn_515c00(save, size_t(k), c.args[1], c.args[2]) ? 1 : 0;
	};
	// 0x513EC0: thiscall on a pirate object.
	r[0x513EC0] = [pirate](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int k = pirate(save, c.ecx);
		if (k < 0 || !probe_sailing()) {
			return kBad;
		}
		probe_sailing()->fn_513ec0(size_t(k));
		return std::nullopt;
	};
	// 0x514D40: thiscall on a pirate object, the most ships and the strength (u16 each).
	r[0x514D40] = [pirate](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int k = pirate(save, c.ecx);
		if (k < 0 || !probe_sailing()) {
			return kBad;
		}
		probe_sailing()->fn_514d40(size_t(k), uint16_t(c.args[0]), uint16_t(c.args[1]));
		return std::nullopt;
	};
	// 0x5063C0: thiscall on the ships object, the target ship and the kind; returns the new ship (ax).
	r[0x5063C0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing()) {
			return kBad;
		}
		c.eax_mask = 0xFFFF;
		return probe_sailing()->fn_5063c0(uint16_t(c.args[0]), c.args[1] & 0xFFFF);
	};
	// 0x619450: thiscall on a scene (a battle), 6 arguments; returns the item (ax).
	r[0x619450] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int scene = battle_block(c.ecx);
		if (!probe_sailing() || scene < 0) {
			return kBad;
		}
		c.eax_mask = 0xFFFF;
		return probe_sailing()->fn_619450(size_t(scene), uint8_t(c.args[0]), uint8_t(c.args[1]), uint16_t(c.args[2]),
			uint16_t(c.args[3]), uint16_t(c.args[4]), uint16_t(c.args[5]));
	};
	// 0x61A4E0: thiscall on a new scene's ship (ship, battle, place, side); returns it.
	// The new block hangs from the battle only after the call (0x605420 puts it
	// in [+0x4]), so its bytes are not in BT yet: skipped, checked with 0x605420.
	r[0x61A4E0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int entry = battle_block(c.ecx);
		const int battle = battle_block(c.args[1]);
		if (!probe_sailing() || entry < 0 || battle < 0) {
			c.skip = true;
			return std::nullopt;
		}
		probe_sailing()->fn_61a4e0(size_t(entry), uint16_t(c.args[0]), size_t(battle), uint16_t(c.args[2]),
			uint8_t(c.args[3]));
		c.eax_mask = 0;
		return 0;
	};
	// 0x62125B: thiscall on a scene's ship; returns its strength.
	r[0x62125B] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int entry = battle_block(c.ecx);
		if (!probe_sailing() || entry < 0) {
			return kBad;
		}
		return uint32_t(probe_sailing()->fn_62125b(size_t(entry)));
	};
	// 0x61A750: thiscall on a scene's ship, another; returns al.
	r[0x61A750] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int entry = battle_block(c.ecx);
		const int other = battle_block(c.args[0]);
		if (!probe_sailing() || entry < 0 || other < 0) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_61a750(size_t(entry), size_t(other)) ? 1 : 0;
	};
	// 0x503AC0: thiscall on a convoy, its index; returns al.
	r[0x503AC0] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int convoy = c.convoy(c.ecx);
		if (!probe_sailing() || convoy < 0 || convoy >= int(save.convoys.size())) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_503ac0(uint16_t(convoy)) ? 1 : 0;
	};
	// 0x50BA80: thiscall on the ships object, a ship at a town; returns al (a battle against the Hanse fleet).
	r[0x50BA80] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const uint16_t ship = uint16_t(c.args[0]);
		if (!probe_sailing() || ship >= save.ships.size()) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_50ba80(ship) ? 1 : 0;
	};
	// 0x50BC40: thiscall on the ships object, the attacking convoy.
	r[0x50BC40] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const uint16_t convoy = uint16_t(c.args[0]);
		if (!probe_sailing() || convoy >= save.convoys.size()) {
			return kBad;
		}
		probe_sailing()->fn_50bc40(convoy);
		return std::nullopt;
	};
	// Scene ships: thiscall on one, another (and a flag); return al or eax.
	r[0x6212FB] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx), f = battle_block(c.args[0]);
		if (!probe_sailing() || e < 0 || f < 0) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_6212fb(size_t(e), size_t(f), (c.args[1] & 0xFF) != 0) ? 1 : 0;
	};
	// 0x6029B0: stdcall (dx, dy); returns al.
	r[0x6029B0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;
		return fn_6029b0(int32_t(c.args[0]), int32_t(c.args[1]));
	};
	// 0x619560: thiscall on a scene (a battle), the item.
	r[0x619560] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_619560(size_t(b), uint16_t(c.args[0]));
		return std::nullopt;
	};
	r[0x61EFDF] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		return uint32_t(probe_sailing()->fn_61efdf(size_t(e)));
	};
	r[0x61F02F] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx), f = battle_block(c.args[0]);
		if (!probe_sailing() || e < 0 || f < 0) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_61f02f(size_t(e), size_t(f), (c.args[1] & 0xFF) != 0) ? 1 : 0;
	};
	// Collisions: thiscall on a battle, two places. 0x609D40 returns al (its
	// two results go to the caller's stack and are checked with 0x608F70, as
	// those of 0x60ADBF, which has no probe).
	r[0x609D40] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		int32_t x = 0, y = 0;
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_609d40(size_t(b), uint16_t(c.args[0]), uint16_t(c.args[1]), x, y) ? 1 : 0;
	};
	r[0x608F70] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_608f70(size_t(b), uint16_t(c.args[0]), uint16_t(c.args[1])) ? 1 : 0;
	};
	r[0x609BA0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_609ba0(size_t(b), uint16_t(c.args[0]), uint16_t(c.args[1]));
		return std::nullopt;
	};
	r[0x60BC5A] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_60bc5a(size_t(b), uint16_t(c.args[0]), uint16_t(c.args[1])) ? 1 : 0;
	};
	r[0x60BCDA] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_60bcda(size_t(b), uint16_t(c.args[0]), uint16_t(c.args[1]), int32_t(c.args[2]),
			int32_t(c.args[3]), uint8_t(c.args[4]));
		return std::nullopt;
	};
	r[0x608850] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_608850(size_t(b), uint16_t(c.args[0]), uint16_t(c.args[1]));
		return std::nullopt;
	};
	r[0x6113C9] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_6113c9(size_t(b));
		return std::nullopt;
	};
	r[0x6197F0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_6197f0(size_t(b));
		return std::nullopt;
	};
	// The movement: thiscall on [+0x4] of a scene's ship.
	r[0x6271D7] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx - 4);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		probe_sailing()->fn_6271d7(size_t(e), uint8_t(c.args[0]));
		return std::nullopt;
	};
	r[0x6264C0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx - 4);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		probe_sailing()->fn_6264c0(size_t(e), uint8_t(c.args[0]), uint8_t(c.args[1]), (c.args[2] & 0xFF) != 0);
		return std::nullopt;
	};
	r[0x61DBBF] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		probe_sailing()->fn_61dbbf(size_t(e));
		return std::nullopt;
	};
	r[0x61F1BF] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		probe_sailing()->fn_61f1bf(size_t(e));
		return std::nullopt;
	};
	r[0x611649] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_611649(size_t(b), uint8_t(c.args[0]));
		return std::nullopt;
	};
	// 0x620F0B: thiscall on a scene's ship (side, distance, two pointers the
	// step passes as 0; with them, skipped).
	r[0x620F0B] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		if (c.args[2] != 0 || c.args[3] != 0) {
			c.skip = true;
			return std::nullopt;
		}
		return uint32_t(probe_sailing()->fn_620f0b(size_t(e), uint8_t(c.args[0]), uint16_t(c.args[1])));
	};
	r[0x61F96F] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_61f96f(size_t(e), int32_t(c.args[0]), int16_t(c.args[1]), uint8_t(c.args[2]));
	};
	r[0x61DD4F] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		probe_sailing()->fn_61dd4f(size_t(e), (c.args[0] & 0xFF) != 0);
		return std::nullopt;
	};
	r[0x61A9E0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		probe_sailing()->fn_61a9e0(size_t(e), uint8_t(c.args[0]));
		return std::nullopt;
	};
	r[0x606CC0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_606cc0(size_t(b), uint16_t(c.args[0]));
		return std::nullopt;
	};
	r[0x60D299] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_60d299(size_t(b), (c.args[0] & 0xFF) != 0);
		return std::nullopt;
	};
	r[0x612BD0] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing() || c.args[0] >= save.convoys.size() || c.args[1] >= save.convoys.size()) {
			return kBad;
		}
		probe_sailing()->fn_612bd0(uint16_t(c.args[0]), uint16_t(c.args[1]));
		return std::nullopt;
	};
	// 0x50A4E0: thiscall on the ships object (a convoy); returns al.
	r[0x50A4E0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing()) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_50a4e0(uint16_t(c.args[0])) ? 1 : 0;
	};
	// 0x50BFD0: thiscall on the ships object (convoy, flag, the other owner).
	r[0x50BFD0] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing() || c.args[0] >= save.convoys.size()) {
			return kBad;
		}
		probe_sailing()->fn_50bfd0(uint16_t(c.args[0]), uint8_t(c.args[1]), uint8_t(c.args[2]));
		return std::nullopt;
	};
	r[0x611740] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_611740(size_t(b), (c.args[0] & 1) != 0);
		c.eax_mask = 0;
		return 0;
	};
	// 0x540520: thiscall on an operation 0x91 record (u32 [+0x4] the winner,
	// [+0x8] the ship taken), on the caller's stack.
	r[0x540520] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const std::optional<uint32_t> w = c.stack(c.ecx + 4), t = c.stack(c.ecx + 8);
		if (!probe_sailing() || !w || !t) {
			c.skip = true;
			return std::nullopt;
		}
		probe_sailing()->fn_540520(uint16_t(*w), uint16_t(*t));
		c.eax_mask = 0;
		return 0;
	};
	// 0x542E40 (operation 0xB4): thiscall on its record (u32 [+0x4] the pirate, [+0x8] the difficulty, [+0xC]
	// the town number, [+0x10] the number), on the executor's stack.
	r[0x542E40] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const std::optional<uint32_t> k = c.stack(c.ecx + 4), d = c.stack(c.ecx + 8), t = c.stack(c.ecx + 0xC),
									  n = c.stack(c.ecx + 0x10);
		if (!probe_sailing() || !k || !d || !t || !n) {
			c.skip = true;
			return std::nullopt;
		}
		probe_sailing()->fn_542e40(*k, *d, *t, *n);
		c.eax_mask = 0;
		return 0;
	};
	// 0x605420: thiscall on a sea battle (ship A, ship D, a byte, a flag).
	r[0x605420] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int battle = battle_block(c.ecx);
		const uint16_t a = uint16_t(c.args[0]), d = uint16_t(c.args[1]);
		if (!probe_sailing() || battle < 0 || a >= save.ships.size() || d >= save.ships.size()) {
			return kBad;
		}
		probe_sailing()->fn_605420(size_t(battle), a, d, uint8_t(c.args[2]), c.args[3]);
		return std::nullopt;
	};
	// 0x611CD0: thiscall on the battles; returns the slot (al).
	r[0x611CD0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing()) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_611cd0();
	};
	// The town battles (piece 6). 0x611D80: thiscall on the battles; returns the slot (al).
	r[0x611D80] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing()) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_611d80();
	};
	// 0x6130B0: thiscall on the battles (town, attacker); returns the slot (al).
	r[0x6130B0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing()) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_6130b0(uint8_t(c.args[0]), uint8_t(c.args[1]));
	};
	// 0x5FBFE0: thiscall on a town battle (the attacking ship, the town).
	r[0x5FBFE0] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int battle = battle_block(c.ecx);
		const uint16_t ship = uint16_t(c.args[0]);
		if (!probe_sailing() || battle < 0 || ship >= save.ships.size()) {
			return kBad;
		}
		probe_sailing()->fn_5fbfe0(size_t(battle), ship, uint8_t(c.args[1]));
		c.eax_mask = 0;
		return 0;
	};
	// 0x60234C: thiscall on a town battle (a ship).
	r[0x60234C] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int battle = battle_block(c.ecx);
		const uint16_t ship = uint16_t(c.args[0]);
		if (!probe_sailing() || battle < 0 || ship >= save.ships.size()) {
			return kBad;
		}
		probe_sailing()->fn_60234c(size_t(battle), ship);
		return std::nullopt;
	};
	// 0x50CC90: thiscall on the ships (a town); returns the guard ship's convoy (ax).
	r[0x50CC90] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing()) {
			return kBad;
		}
		c.eax_mask = 0xFFFF;
		return probe_sailing()->fn_50cc90(uint8_t(c.args[0]));
	};
	// The town battle's step (piece 6, part 3): thiscall on a town battle.
	r[0x5FD510] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_5fd510(size_t(b), uint16_t(c.args[0]));
		return std::nullopt;
	};
	r[0x5FE080] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_5fe080(size_t(b), uint8_t(c.args[0]));
		return std::nullopt;
	};
	r[0x5FF8E0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_5ff8e0(size_t(b), uint16_t(c.args[0]), uint16_t(c.args[1]));
		return std::nullopt;
	};
	r[0x600770] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_600770(size_t(b), uint16_t(c.args[0]));
		return std::nullopt;
	};
	// 0x601C7B: thiscall on a town battle (place, x, y); returns al.
	r[0x601C7B] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_601c7b(size_t(b), uint16_t(c.args[0]), uint16_t(c.args[1]), uint16_t(c.args[2]))
			? 1
			: 0;
	};
	// 0x60114B: thiscall on a town battle (x, y); returns al.
	r[0x60114B] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_60114b(size_t(b), int32_t(c.args[0]), int32_t(c.args[1]));
	};
	r[0x60130B] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_60130b(size_t(b), uint8_t(c.args[0]), int32_t(c.args[1]));
		return std::nullopt;
	};
	// The town battle's end (piece 6, part 4): thiscall on a town battle.
	r[0x5FE7E0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_5fe7e0(size_t(b), (c.args[0] & 0xFF) != 0);
		return std::nullopt;
	};
	// 0x602990 (method +0x4; free) and its destructor 0x5FB9F0.
	r[0x602990] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_5fb9f0(size_t(b));
		c.eax_mask = 0;
		return 0;
	};
	r[0x5FB9F0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int b = battle_block(c.ecx);
		if (!probe_sailing() || b < 0) {
			return kBad;
		}
		probe_sailing()->fn_5fb9f0(size_t(b));
		return std::nullopt;
	};
	// A town battle's scene ship (0x1168 bytes): thiscall on it.
	r[0x621770] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		probe_sailing()->fn_621770(size_t(e), uint8_t(c.args[0]));
		return std::nullopt;
	};
	r[0x6247B0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		probe_sailing()->fn_6247b0(size_t(e));
		return std::nullopt;
	};
	r[0x624950] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		probe_sailing()->fn_624950(size_t(e), (c.args[0] & 0xFF) != 0);
		return std::nullopt;
	};
	r[0x625BF0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		probe_sailing()->fn_625bf0(size_t(e));
		return std::nullopt;
	};
	// 0x6258A0: thiscall on a town battle's scene ship (x, y: words).
	r[0x6258A0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int e = battle_block(c.ecx);
		if (!probe_sailing() || e < 0) {
			return kBad;
		}
		probe_sailing()->fn_6258a0(size_t(e), uint16_t(c.args[0]), uint16_t(c.args[1]));
		return std::nullopt;
	};
	// 0x517170: thiscall on a ship.
	r[0x517170] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int ship = c.ship(c.ecx);
		if (ship < 0) {
			return kBad;
		}
		fn_517170(save, uint16_t(ship));
		c.eax_mask = 0;
		return 0;
	};
	// 0x5062F0: thiscall on the ships object, the address of the head of a
	// list of convoys (a global, or a local of 0x514D40 on the stack) and the ship.
	r[0x5062F0] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const Location l = c.locate(c.args[0]);
		const int ship = int(c.args[1] & 0xFFFF);
		if (!probe_sailing() || ship >= int(save.ships.size())) {
			return kBad;
		}
		c.eax_mask = 0;
		if (l.kind == Location::Global) {
			probe_sailing()->fn_5062f0(save.globals.u16(l.offset), uint16_t(ship));
			return 0;
		}
		// A local head on the stack (0x514D40): its value from the recording.
		const std::optional<uint32_t> head = c.stack(c.args[0]);
		if (!head) {
			return kBad;
		}
		uint16_t local = uint16_t(*head);
		probe_sailing()->fn_5062f0(local, uint16_t(ship));
		return 0;
	};
	// 0x447580: thiscall on the navigation object (the address of the ship's
	// route [+0x24], the storm and the ship as packed points, the first point
	// far from the storm, the point the ship goes to, whether the route is its
	// own); returns al.
	r[0x447580] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const Location l = c.locate(c.args[0]);
		if (!probe_sailing() || l.kind != Location::Ship || l.offset != 0x24) {
			return kBad;
		}
		SavedShip& ship = save.ships[size_t(l.index)];
		if (!ship.route) {
			return kBad;
		}
		auto point = [](uint32_t packed) { return MapPoint{int16_t(packed & 0xFFFF), int16_t(packed >> 16)}; };
		std::shared_ptr<SeaRoute> route = ship.route;
		const bool result = fn_447580(probe_sailing()->nav(), route, point(c.args[1]), point(c.args[2]), c.args[3],
				c.args[4], (c.args[5] & 0xFF) != 0);
		ship.set_route(std::move(route));
		c.eax_mask = 0xFF;
		return result ? 1 : 0;
	};
}

}  // namespace p3::probe
