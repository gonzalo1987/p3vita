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
