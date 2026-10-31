// Probes of src/p3data/battle/*.cpp (functions of Sailing: they need
// tick_probe --cpr, which builds the navigation of probe_sailing()).

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
	// 0x514B80: thiscall on a pirate object, the convoy.
	r[0x514B80] = [pirate](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		const int k = pirate(save, c.ecx);
		if (k < 0 || !probe_sailing() || (c.args[0] & 0xFFFF) >= save.convoys.size()) {
			return kBad;
		}
		probe_sailing()->fn_514b80(size_t(k), uint16_t(c.args[0]));
		return std::nullopt;
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
