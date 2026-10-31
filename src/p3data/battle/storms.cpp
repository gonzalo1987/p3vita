// The storms (0x5053C0) and what hangs from them: the ships near a storm are
// rerouted around it (0x505290 → 0x447580 on the navigation object), take
// damage to the hull and are pushed back. See docs/cloud/battles.md §1.
//
// 0x506720 calls 0x5053C0 at its end, every 8 ticks, after the convoys. It
// runs only with Sailing::set_storms(true), like town_oracle.py --storms:
// until the oracle and p3vita switch it on together, both skip it.
//
// The ships object (0x6DD7A0) holds 16 storm slots of 12 bytes at
// [+0x20 + 0xC·k]:
//   [+0x0] u16 the storm: a pseudo-ship of owner 0xFF in state 0x15 (0xFFFF: none)
//   [+0x2] u16 strength: −8 each time; under 8 the storm ends
//   [+0x4] u16, [+0x6] u16: x and y where it was created (not used here)
//   [+0x8] u16 kind: 0 takes 1/64 of the maximum hull, 2 takes 1/32, 1 no damage
//   [+0xA] u16 a countdown for the sea map: −4 each time (at 0 the map is redrawn)

#include <algorithm>

#include "p3data/sailing.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kStormSlots = global::kShips + 0x20;
constexpr int kStormSlotCount = 16;
constexpr uint32_t kStormSlotSize = 0xC;
constexpr uint32_t kFreeShipHead = global::kShips + 0xE2;  // u16 the first free ship
constexpr uint32_t kCaptainCountShips = global::kShips + 0xF2;  // u16
constexpr uint32_t kConvoyCountShips = global::kShips + 0xF6;  // u16
constexpr uint16_t kStatusStorm = 0x15;
constexpr uint16_t kStatusFree = 0xFF;
constexpr uint16_t kNoticeStorm = 0x100;  // [+0x3C] of the ship and [+0x18] of its convoy

// 0x66D660: the eight corners of the octagon around a storm (radius 40).
constexpr int16_t kOctagon[8][2] = {{0, -40}, {26, -26}, {40, 0}, {26, 26}, {0, 40}, {-26, 26}, {-40, 0}, {-26, -26}};
constexpr int kGridScale = 3536;  // 0x66E0D0: thousandths of a pixel per cell of the navigation grid
constexpr int16_t kGridBorder = 0x44;

// The corner k (taken modulo 8 the way the game does it, (k + 8) & 7 with the
// sign kept) of the octagon around `storm`.
MapPoint corner(MapPoint storm, int k) {
	int index = (k + 8) % 8;  // and eax,0x80000007 with the sign fix-up
	if (index < 0) {
		index += 8;
	}
	return MapPoint{int16_t(storm.x + kOctagon[index][0]), int16_t(storm.y + kOctagon[index][1])};
}

// The test of 0x447580 for a corner: in the grid and on water.
bool open_water(const NavData& nav, MapPoint storm, int k) {
	const MapPoint p = corner(storm, k);
	const int16_t px = int16_t(p.x - kGridBorder);
	const int16_t py = int16_t(p.y - kGridBorder);
	const int32_t cx = (int32_t(px) * 1000 + kGridScale / 2) / kGridScale;  // idiv: truncates
	const int32_t cy = (int32_t(py) * 1000 + kGridScale / 2) / kGridScale;
	if (cx >= nav.width() || cy >= nav.height() || cy < 0 || cx < 0 || py < 0 || px < 0) {
		return false;
	}
	return nav.is_water(cx, cy);
}

// The two corners nearest to `from` (the nearest first), as in the two loops
// of 0x447580: strict comparisons, so on a tie the lower corner wins.
void nearest_two(MapPoint storm, MapPoint from, int& best, int& second) {
	uint32_t best_d = 0xFFFFFFFF;
	uint32_t second_d = 0xFFFFFFFF;
	best = 0;
	second = 0;
	for (int k = 0; k < 8; ++k) {
		const int32_t dx = int32_t(from.x) - kOctagon[k][0] - storm.x;
		const int32_t dy = int32_t(from.y) - kOctagon[k][1] - storm.y;
		const uint32_t d = uint32_t(dy * dy) + uint32_t(dx * dx);
		if (d < best_d) {
			second_d = best_d;
			second = best;
			best_d = d;
			best = k;
		} else if (d < second_d) {
			second_d = d;
			second = k;
		}
	}
}

}  // namespace

// 0x447580 (thiscall on the navigation object; the game's arguments: the
// address of the ship's route [+0x24], the storm, the ship, the first point
// of the route that is far from the storm, the point the ship goes to
// [+0x34] and whether the route is the ship's own [+0x36]): a new route that
// goes round the storm. Up to the point the ship goes to, the old route; then
// the ship's own position; then the corners of the octagon from the one
// nearest to the ship to the one nearest to `found` (the shorter way round
// that is all water); then the old route from `found`. If neither way round is
// all water, only the ship's position goes in. When the old route was the
// ship's own the game frees it; here the shared_ptr does. Always returns 1
// (the new route is the ship's own).
bool fn_447580(const NavData& nav, std::shared_ptr<SeaRoute>& route, MapPoint storm, MapPoint ship, uint32_t found,
		uint32_t current, bool own) {
	(void)own;
	const SeaRoute& old = *route;
	const size_t count = old.size();
	current &= 0xFFFF;
	found &= 0xFFFF;

	// The corners nearest to the ship (a, b) and to the point `found` (c, d),
	// each pair put in order.
	int a = 0, b = 0;
	nearest_two(storm, ship, a, b);
	if ((a > b && b != 0 && a != 7) || (b == 6 && a == 7) || (b == 7 && a == 0) || (b == 0 && a == 1)) {
		std::swap(a, b);
	}
	int c = 0, d = 0;
	nearest_two(storm, old[found], c, d);
	if ((c < d && d != 7 && c != 0) || (d == 7 && c == 6) || (d == 0 && c == 7) || (d == 1 && c == 0)) {
		std::swap(c, d);
	}

	// Only the ship's position, at the point it goes to.
	auto insert_ship = [&]() {
		auto next = std::make_shared<SeaRoute>();
		next->reserve(count + 1);
		next->insert(next->end(), old.begin(), old.begin() + std::ptrdiff_t(current));
		next->push_back(ship);
		next->insert(next->end(), old.begin() + std::ptrdiff_t(current), old.end());
		route = std::move(next);
		return true;
	};
	if (a == d || b == c) {
		return insert_ship();
	}
	if (a < c || a == 0) {
		c -= 8;
	}
	if (b > d || d == 0) {
		d += 8;
	}
	// The two ways round: down from a to c, or up from b to d (0xFFFF when
	// that way is not all water or is empty). The numbers are compared as u16.
	uint32_t down = 0;  // [esp+0x30]
	uint32_t up = 0;  // [esp+0x34]
	if (c <= a) {
		down = uint32_t(a - c + 1);
	}
	bool use_down = false;
	if (b <= d) {
		up = uint32_t(d - b + 1);
	}
	if (b <= d && uint16_t(up) > uint16_t(down)) {
		// 0x447B16: the way down first.
		if (a < c) {
			use_down = false;  // 0x447DD5 with [esp+0x34] = up: the comparison below decides
		} else {
			int k = a;
			while (k >= c && open_water(nav, storm, k)) {
				--k;
			}
			if (k >= c) {
				// Land on the way down: the way up.
				k = b;
				while (k <= d && open_water(nav, storm, k)) {
					++k;
				}
				if (k < d) {
					return insert_ship();
				}
				down = 0xFFFF;
			}
		}
	} else if (b <= d) {
		// 0x4478AE: the way up first.
		int k = b;
		while (k <= d && open_water(nav, storm, k)) {
			++k;
		}
		if (k <= d) {
			// Land on the way up: the way down.
			k = a;
			if (a >= c) {
				while (k >= c && open_water(nav, storm, k)) {
					--k;
				}
			}
			if (k > c) {
				return insert_ship();
			}
			up = 0xFFFF;
		}
	}
	use_down = uint16_t(up) > uint16_t(down);
	auto next = std::make_shared<SeaRoute>();
	next->insert(next->end(), old.begin(), old.begin() + std::ptrdiff_t(current));
	next->push_back(ship);
	if (use_down) {
		for (int k = a; k >= c; --k) {
			next->push_back(corner(storm, k));
		}
	} else {
		for (int k = b; k <= d; ++k) {
			next->push_back(corner(storm, k));
		}
	}
	next->insert(next->end(), old.begin() + std::ptrdiff_t(found), old.end());
	route = std::move(next);
	return true;
}

// 0x505290 (thiscall on the ships object: the ship, its index, the storm's
// x and y): a ship with a captain near a storm. From the point of its route it
// goes to, the first point farther than 36 px from the storm (distance² >
// 0x510); if there is one and the point [+0x2] is not within 0x24 of the storm,
// the route goes round it (0x447580), the ship stops ([+0x28], [+0x2C] = 0,
// so 0x516CC0 sets a new course) and [+0x2] = [+0x34]. The sea map is told
// when it is the player's ship (0x44F940: UI, not reproduced).
void Sailing::fn_505290(uint16_t index, int32_t storm_x, int32_t storm_y) {
	SavedShip& ship = save_.ships[index];
	if (!ship.route) {
		return;
	}
	const SeaRoute& points = *ship.route;
	const int32_t count = int32_t(points.size());
	if (count < 1) {
		return;
	}
	int32_t k = ship.route_index();
	for (; k < count; ++k) {
		const int32_t dx = storm_x - points[size_t(k)].x;
		const int32_t dy = storm_y - points[size_t(k)].y;
		if (dy * dy + dx * dx > 0x510) {
			break;
		}
	}
	if (k >= count) {
		return;
	}
	const uint16_t mark = ship.route_mark();
	if (mark < count) {
		const int32_t dx = points[mark].x - storm_x;
		const int32_t dy = points[mark].y - storm_y;
		if (dx <= 0x24 && dx >= -0x24 && dy <= 0x24 && dy >= -0x24) {
			return;
		}
	}
	const MapPoint storm{int16_t(storm_x), int16_t(storm_y)};
	const MapPoint here{int16_t(ship.memory.u16(0x1E)), int16_t(ship.memory.u16(0x22))};
	std::shared_ptr<SeaRoute> route = ship.route;
	ship.own_route() = fn_447580(nav_, route, storm, here, uint32_t(k), ship.route_index(), ship.own_route() != 0) ? 1 : 0;
	ship.set_route(std::move(route));
	ship.dx() = 0;
	ship.route_mark() = ship.route_index();
	ship.dy() = 0;
}

// 0x5053C0 (thiscall on the ships object): each storm hits the ships within
// 0x24 px of it in x (walking the list by x both ways from the storm) and in y.
// A ship with a captain may be rerouted (0x505290); within 28 px (distance² <
// 0x310) it takes the damage of the kind (the ship alone without a minimum, or
// every ship of its convoy, keeping at least 1) with the notice 0x100, and,
// kinds 0 and 1, is pushed back to the start of the pixel it is in (and its
// convoy with it). Then the storm weakens and, under 8, ends: out of the list
// by x and into the free ships.
void Sailing::fn_5053c0() {
	GlobalMemory& g = save_.globals;
	const uint32_t ships = save_.ships.size();
	const uint16_t captains = g.u16(kCaptainCountShips);
	const uint16_t convoys = g.u16(kConvoyCountShips);

	// The damage of kinds 0 and 2: shift 6 or 5 of the maximum hull.
	auto damage = [&](SavedShip& victim, int shift) {
		if (victim.convoy() >= convoys) {
			victim.health() -= victim.max_health() >> shift;
			victim.notices() |= kNoticeStorm;
			return;
		}
		GameMemory<0x3C>& convoy = save_.convoys[victim.convoy()];
		convoy.u32(0x18) |= kNoticeStorm;
		for (uint32_t j = convoy.u16(0xA); j < ships; j = save_.ships[j].next_in_list()) {
			SavedShip& member = save_.ships[j];
			member.notices() |= kNoticeStorm;
			member.health() -= member.max_health() >> shift;
			if (member.health() < 1) {
				member.health() = 1;
			}
		}
	};
	// Pushed back: one step of its course back while the whole pixel stays
	// the same (0x50560F); the ships of its convoy take its position.
	auto push_back = [&](SavedShip& victim) {
		const int32_t dx = victim.dx();
		if (dx != 0 || victim.dy() != 0) {
			const int32_t dy = victim.dy();
			int32_t x = victim.x();
			int32_t y = victim.y();
			for (;;) {
				x = int32_t(uint32_t(x) - uint32_t(dx));
				y = int32_t(uint32_t(y) - uint32_t(dy));
				if (((uint32_t(victim.x()) ^ uint32_t(x)) & 0xFFFF0000) != 0 ||
						((uint32_t(victim.y()) ^ uint32_t(y)) & 0xFFFF0000) != 0) {
					break;
				}
				victim.x() = x;
				victim.y() = y;
			}
		}
		if (victim.convoy() < convoys) {
			const GameMemory<0x3C>& convoy = save_.convoys[victim.convoy()];
			for (uint32_t j = convoy.u16(0xA); j < ships; j = save_.ships[j].next_in_list()) {
				save_.ships[j].x() = victim.x();
				save_.ships[j].y() = victim.y();
			}
		}
	};
	// One ship near the storm: true if the walk goes on.
	auto hit = [&](uint32_t i, uint16_t kind, int32_t sx, int32_t sy) {
		SavedShip& victim = save_.ships[i];
		const int32_t vx = int16_t(victim.memory.u16(0x1E));
		const int32_t vy = int16_t(victim.memory.u16(0x22));
		const int32_t dx = sx - vx;
		const int32_t dy = sy - vy;
		if (victim.captain() < captains) {
			fn_505290(uint16_t(i), sx, sy);
		}
		if (dy * dy + dx * dx >= 0x310) {
			return;
		}
		switch (kind) {
		case 2:
			damage(victim, 5);
			break;
		case 0:
			damage(victim, 6);
			push_back(victim);
			break;
		case 1:
			push_back(victim);
			break;
		default:
			break;
		}
	};

	for (int k = 0; k < kStormSlotCount; ++k) {
		const uint32_t slot = kStormSlots + kStormSlotSize * uint32_t(k);
		const uint16_t storm_index = g.u16(slot);
		if (storm_index >= ships) {
			continue;
		}
		SavedShip& storm = save_.ships[storm_index];
		storm.status() = kStatusStorm;
		const int32_t sx = int16_t(storm.memory.u16(0x1E));
		const int32_t sy = int16_t(storm.memory.u16(0x22));
		const uint16_t kind = g.u16(slot + 0x8);
		// Towards smaller x ([+0xA]) while x ≥ sx − 0x24, then towards larger
		// x ([+0xC]) while x ≤ sx + 0x24; only the ships with |y − sy| ≤ 0x24.
		for (uint32_t i = storm.memory.u16(0xA); i < ships; i = save_.ships[i].memory.u16(0xA)) {
			const SavedShip& v = save_.ships[i];
			if (int16_t(v.memory.u16(0x1E)) < sx - 0x24) {
				break;
			}
			const int32_t vy = int16_t(v.memory.u16(0x22));
			if (vy >= sy - 0x24 && vy <= sy + 0x24) {
				hit(i, kind, sx, sy);
			}
		}
		for (uint32_t i = storm.memory.u16(0xC); i < ships; i = save_.ships[i].memory.u16(0xC)) {
			const SavedShip& v = save_.ships[i];
			if (int16_t(v.memory.u16(0x1E)) > sx + 0x24) {
				break;
			}
			const int32_t vy = int16_t(v.memory.u16(0x22));
			if (vy >= sy - 0x24 && vy <= sy + 0x24) {
				hit(i, kind, sx, sy);
			}
		}
		// The countdown of the sea map (at 0 the game redraws it: UI).
		const uint16_t countdown = g.u16(slot + 0xA);
		if (countdown > 0) {
			g.u16(slot + 0xA) = countdown <= 4 ? 0 : uint16_t(countdown - 4);
		}
		const uint16_t strength = g.u16(slot + 0x2);
		if (strength < 8) {
			unlink_x(storm_index);
			storm.status() = kStatusFree;
			storm.next_in_list() = g.u16(kFreeShipHead);
			g.u16(kFreeShipHead) = g.u16(slot);
			g.u16(slot) = 0xFFFF;
		} else {
			g.u16(slot + 0x2) = uint16_t(strength - 8);
		}
	}
}

}  // namespace p3::data
