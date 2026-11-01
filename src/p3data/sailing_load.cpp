#include "p3data/sailing.hpp"

namespace p3::data {

// 0x517C10 (on the ship, with its index), from 0x5048C0 at the end of the
// load: whether the ship is still on its way to the point [+0x34] of its
// route. It isn't if there is no such point; with a movement [+0x28],
// [+0x2C] along one axis, if the point isn't on that line ahead; along both,
// if no step from where it is (from the smaller of the two numbers of steps
// to the point, at least 0, to the larger plus 2) lands on the point. Then,
// if it is out of the map (2400 × 1800, 0x66D7CC), it goes to (100, 100)
// (out of the list by x and in again); and it gets a new route (0x5172A0) to
// its destination or, at sea, to the last point of its route.
void Sailing::fn_517c10(uint16_t index) {
	SavedShip& ship = save_.ships[index];
	ShipMemory& m = ship.memory;
	if (m.u32(0x24) == 0 || !ship.route) {
		return;
	}
	const SeaRoute& route = *ship.route;
	const uint32_t target = m.u16(0x34);
	bool fine = false;
	if (target < route.size()) {
		const MapPoint p = route[target];
		const int32_t dx = m.s32(0x28);
		const int32_t dy = m.s32(0x2C);
		const int16_t hx = m.s16(0x1E);
		const int16_t hy = m.s16(0x22);
		if (dx == 0 && dy == 0) {
			fine = p.x == hx && p.y == hy;
		} else if (dx == 0) {
			fine = p.x == hx && (dy > 0 ? p.y > hy : p.y < hy);
		} else if (dy == 0) {
			fine = p.y == hy && (dx > 0 ? p.x > hx : p.x < hx);
		} else {
			const int32_t x = m.s32(0x1C);
			const int32_t y = m.s32(0x20);
			int32_t first = int32_t(uint32_t(int32_t(p.x)) << 16) - x;
			first /= dx;
			int32_t last = int32_t(uint32_t(int32_t(p.y)) << 16) - y;
			last /= dy;
			if (first > last) {
				std::swap(first, last);
			}
			if (first < 0) {
				first = 0;
			}
			last += 2;
			for (int32_t step = first; step <= last && !fine; ++step) {
				const int32_t px = int32_t(uint32_t(dx) * uint32_t(step) + uint32_t(x));
				const int32_t py = int32_t(uint32_t(dy) * uint32_t(step) + uint32_t(y));
				fine = (px >> 16) == p.x && (py >> 16) == p.y;
			}
		}
	}
	if (fine) {
		return;
	}
	if (m.u16(0x1E) > 2400 || m.u16(0x22) > 1800) {
		unlink_x(index);
		m.u16(0x1E) = 100;
		m.u16(0x22) = 100;
		insert_x(index, 0xFFFF);
	}
	if (ship.destination() < save_.globals.u32(global::kWorldTownCount)) {
		set_destination(ship, ship.destination(), 0, 0);
	} else if (!route.empty()) {
		const MapPoint end = route.back();
		set_destination(ship, 0xFF, end.x, end.y);
	}
}

void Sailing::check_loaded_routes() {
	for (size_t i = save_.ships.size(); i-- > 0;) {
		fn_517c10(uint16_t(i));
	}
}

}  // namespace p3::data
