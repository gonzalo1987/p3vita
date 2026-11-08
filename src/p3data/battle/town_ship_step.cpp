// The step of a town battle's scene ship (the battles' piece 6, part 3): a
// block of 0x1168 bytes, vtable 0x67AAD0 (= the town battle's 0x67AAA0 +
// 0x30): the sea's 0x164 bytes, then its map of distances to its goal
// [+0x164] (64 × 64, x · 64 + y, as the battle's map of the water) and that
// goal's cell [+0x1164], [+0x1166]. Its methods: +0x0 the sub-step
// (0x621770), +0x4 the target (0x6247B0), +0x8 the guns (0x624950); and the
// way round the land (0x625BF0, with the map 0x6258A0 and 0x625B00). Each a
// copy of the sea's (battle/ship_step.cpp) with the towers, the land and a
// few changes, rewritten literally.

#include <algorithm>
#include <cstring>
#include <vector>

#include "p3data/battle/battle_math.hpp"
#include "p3data/sailing.hpp"

namespace p3::data {

using namespace battle_math;

void fn_5fff40(uint32_t x, uint32_t y, uint16_t& cx, uint16_t& cy);  // battle/town_step.cpp

namespace {

// 0x67B0E4 (s16 by type, as the sea's 0x67AF7C): half the size of a ship's box in the scene.
constexpr int32_t kTownBoxHalf[4] = {55, 66, 70, 84};

int32_t add32(int32_t a, int32_t b) {
	return int32_t(uint32_t(a) + uint32_t(b));
}

}  // namespace

// The map of distances of a town battle's scene ship, at a cell.
uint8_t Sailing::town_ship_map(size_t e, uint32_t x, uint32_t y) const {
	return save_.battles.get<uint8_t>(e, 0x164 + size_t(x) * 0x40 + size_t(y));
}

// 0x625B00 (on a town battle's scene ship; &x, &y): the nearest cell of its
// map that isn't land (≠ 0), by a diamond spiral from x, y: (0, 0), (1, 0),
// (0, 1), (−1, 0), (0, −1), (1, −1), (2, 0)... The game's loop never ends
// without water: p3vita stops after the whole map.
void Sailing::fn_625b00(size_t e, uint16_t& x, uint16_t& y) const {
	int32_t dx = 0, dy = 0;
	for (int guard = 0; guard < 0x10000; ++guard) {
		const int32_t cx = int32_t(x) + dx, cy = int32_t(y) + dy;
		if (cx >= 0 && cx < 0x40 && cy >= 0 && cy < 0x40 && town_ship_map(e, uint32_t(cx), uint32_t(cy)) != 0) {
			break;
		}
		if (dx == 0) {
			if (dy > 0) {
				--dx;
				--dy;
			} else {
				dx = 1;
			}
		} else if (dx > 0) {
			if (dy < 0) {
				++dx;
				++dy;
			} else {
				--dx;
				++dy;
			}
		} else if (dy > 0) {
			--dx;
			--dy;
		} else {
			++dx;
			--dy;
		}
	}
	x = uint16_t(x + dx);
	y = uint16_t(y + dy);
}

// 0x6258A0 (on a town battle's scene ship; x, y): its map of distances to the
// point: the battle's map of the water (0xFF water, 0 land), the point's cell
// (0x5FFF40) moved to the nearest water (0x625B00) at 1, and from it, by
// buckets of one unit as the game keeps them (each a list pushed at its head
// and walked from it, a cell kept once more when it gets nearer), each
// neighbour 2 further across a side and 3 across a corner (a byte, as the
// game adds it), where that is nearer.
void Sailing::fn_6258a0(size_t e, uint16_t x, uint16_t y) {
	BattleGraph& g = save_.battles;
	const size_t b = scene_battle(e);
	for (size_t i = 0; i < 0x1000; ++i) {
		g.set<uint8_t>(e, 0x164 + i, g.get<uint8_t>(b, 0x1EE0 + i) != 0 ? 0xFF : 0);
	}
	struct Cell {
		uint16_t x, y;
	};
	Cell start{};
	fn_5fff40(x, y, start.x, start.y);
	fn_625b00(e, start.x, start.y);
	g.set<uint8_t>(e, 0x164 + size_t(start.x) * 0x40 + start.y, 1);
	static constexpr int8_t kStep[8][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
	// The lists, their head at the back.
	std::vector<Cell> current{start}, b0, b1, b2;
	for (;;) {
		while (!current.empty()) {
			const Cell c = current.back();
			current.pop_back();
			for (int pass = 0; pass < 2; ++pass) {
				for (int d = 0; d < 4; ++d) {
					const int32_t cx = int32_t(c.x) + kStep[4 * pass + d][0];
					const int32_t cy = int32_t(c.y) + kStep[4 * pass + d][1];
					if (cx < 0 || cx >= 0x40 || cy < 0 || cy >= 0x40) {
						continue;
					}
					const uint8_t cost = uint8_t(town_ship_map(e, c.x, c.y) + pass + 2);
					const size_t at = 0x164 + size_t(cx) * 0x40 + size_t(cy);
					if (g.get<uint8_t>(e, at) > cost) {
						g.set<uint8_t>(e, at, cost);
						(pass == 0 ? b1 : b2).push_back({uint16_t(cx), uint16_t(cy)});
					}
				}
			}
		}
		bool more = false;
		for (int r = 0; r < 3 && !more; ++r) {
			current = std::move(b0);
			b0 = std::move(b1);
			b1 = std::move(b2);
			b2.clear();
			more = !current.empty();
		}
		if (!more) {
			break;
		}
	}
}

// 0x625BF0 (on a town battle's scene ship): its directions marked round the
// land. Its map of distances is made again (0x6258A0) to its goal [+0x20],
// [+0x22] when that goal's cell (0x5FFF40) changed, unless it flees or has no
// goal and has a map already. Then, from its own cell (its distance there, or
// 0xFF): each of the 8 neighbours that is land marks the 64 directions round
// it 0xC0; else, with a goal and not fleeing, a neighbour not nearer the goal
// marks the 33 round it at least 0x40 (one exactly 1 nearer, at least 0x10);
// and each of the 16 cells two away that is land marks the 31 round it at
// least 0x40 and the 48 further on each side at least 0x10.
void Sailing::fn_625bf0(size_t e) {
	BattleGraph& g = save_.battles;
	auto avoid = [&](uint32_t d) { return 0x27 + size_t(d & 0xFF); };
	auto raise = [&](uint32_t d, uint8_t v) {
		if (g.get<uint8_t>(e, avoid(d)) < v) {
			g.set<uint8_t>(e, avoid(d), v);
		}
	};
	const bool fleeing = (g.get<uint8_t>(e, 0x12A) & 0x1) != 0;
	if (!((fleeing || g.get<uint16_t>(e, 0x20) == 0xFFFF) && g.get<uint16_t>(e, 0x1164) != 0xFFFF)) {
		uint16_t gx = 0, gy = 0;
		fn_5fff40(uint32_t(int32_t(g.get<int16_t>(e, 0x20))), uint32_t(int32_t(g.get<int16_t>(e, 0x22))), gx, gy);
		if (gx != g.get<uint16_t>(e, 0x1164) || gy != g.get<uint16_t>(e, 0x1166)) {
			g.set<uint16_t>(e, 0x1166, gy);
			g.set<uint16_t>(e, 0x1164, gx);
			fn_6258a0(e, g.get<uint16_t>(e, 0x20), g.get<uint16_t>(e, 0x22));
		}
	}
	uint16_t sx = 0, sy = 0;
	fn_5fff40(uint32_t(int32_t(g.get<int16_t>(e, 0xA))), uint32_t(int32_t(g.get<int16_t>(e, 0xE))), sx, sy);
	uint8_t own = sx < 0x40 && sy < 0x40 ? town_ship_map(e, sx, sy) : 0;
	if (own == 0) {
		own = 0xFF;
	}
	auto cell = [&](int32_t dx, int32_t dy) -> uint8_t {
		const uint16_t x = uint16_t(sx + dx), y = uint16_t(sy + dy);
		return x < 0x40 && y < 0x40 ? town_ship_map(e, x, y) : 0xFF;
	};
	static constexpr int8_t kNear[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
	for (uint32_t i = 0; i < 8; ++i) {
		const uint32_t d = 0x20 * i;
		const uint8_t v = cell(kNear[i][0], kNear[i][1]);
		if (v == 0) {
			for (uint32_t j = 0; j < 0x20; ++j) {
				g.set<uint8_t>(e, avoid(d + j), 0xC0);
				g.set<uint8_t>(e, avoid(d - j), 0xC0);
			}
			continue;
		}
		if ((g.get<uint8_t>(e, 0x12A) & 0x1) || g.get<uint16_t>(e, 0x20) == 0xFFFF) {
			continue;
		}
		if (v >= own) {
			for (uint32_t j = 0; j < 0x11; ++j) {
				raise(d + j, 0x40);
				raise(d - j, 0x40);
			}
		}
		if (int32_t(v) == int32_t(own) - 1) {
			for (uint32_t j = 0; j < 0x11; ++j) {
				raise(d + j, 0x10);
				raise(d - j, 0x10);
			}
		}
	}
	static constexpr int8_t kFar[16][2] = {{0, -2}, {1, -2}, {2, -2}, {2, -1}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2},
		{-1, 2}, {-2, 2}, {-2, 1}, {-2, 0}, {-2, -1}, {-2, -2}, {-1, -2}};
	for (uint32_t i = 0; i < 16; ++i) {
		const uint32_t d = 0x10 * i;
		if (cell(kFar[i][0], kFar[i][1]) != 0) {
			continue;
		}
		for (uint32_t j = 0; j < 0x10; ++j) {
			raise(d + j, 0x40);
			raise(d - j, 0x40);
		}
		for (uint32_t j = 0; j < 0x30; ++j) {
			raise(d + 0x10 + j, 0x10);
			raise(d - 0x10 - j, 0x10);
		}
	}
}

// 0x6247B0 (method +0x4 of a town battle's scene ship): a target, unless
// [+0x161]: as the sea's 0x61DBBF (an enemy not out chosen by the battle's
// generator, each as likely as its arms per hull, 0x61EFDF), but the
// attackers (team 0) count the towers too (0xC80000 / each one's strength):
// when the pick falls on them, the first tower standing (0xFFFE − its
// number; 0xFFFA without any). [+0x128] the target.
void Sailing::fn_6247b0(size_t e) {
	BattleGraph& g = save_.battles;
	if (g.get<uint8_t>(e, 0x161) > 0) {
		return;
	}
	const size_t b = scene_battle(e);
	const uint8_t team = g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(e, 0x13D)));
	const uint16_t head = g.get<uint16_t>(b, team == 0 ? 0x65C : 0x10);
	const size_t next = size_t(g.ptr(b, 0xC));
	auto out = [&](uint16_t k) { return (g.get<uint8_t>(battle_entry(b, k), 0x12A) & 0x6) != 0; };
	int32_t total = 0;
	for (uint16_t k = head; k != 0xFFFF; k = g.get<uint16_t>(next, 2 * size_t(k))) {
		if (!out(k)) {
			total += fn_61efdf(battle_entry(b, k));
		}
	}
	if (team == 0) {
		for (size_t t = 0; t < 4; ++t) {
			const int32_t s = g.get<int32_t>(b, 0x1EC4 + 4 * t);
			if (s > 0) {
				total += 0xC80000 / s;
			}
		}
	}
	if (total > 0) {
		total = int32_t(battle_rand(g, b) % uint32_t(total) + 1);
	}
	uint16_t k = head;
	while (k != 0xFFFF) {
		if (!out(k)) {
			total -= fn_61efdf(battle_entry(b, k));
			if (total <= 0) {
				break;
			}
		}
		k = g.get<uint16_t>(next, 2 * size_t(k));
	}
	if (k == 0xFFFF && team == 0) {
		uint16_t t = 0;
		while (t < 4 && g.get<int32_t>(b, 0x1EC4 + 4 * size_t(t)) <= 0) {
			++t;
		}
		g.set<uint16_t>(e, 0x128, uint16_t(0xFFFE - t));
		return;
	}
	g.set<uint16_t>(e, 0x128, k);
}

// 0x624950 (method +0x8 of a town battle's scene ship; a flag): it fires. As
// the sea's 0x61DD4F (where its friends and enemies will be, the closest
// enemy within 480 on each side), with these changes: without the flag an AI
// ship leaves no enemy for fleeing or grappling; the attackers (team 0) aim
// at the towers standing too (by their place, at the broadsides' plain
// directions 0x37 to 0x49 and 0xB7 to 0xC9, a human side only its target
// tower); and an automatic battle's broadside raises the team's mode first
// (0xFF if the target ship fled), hits a ship (0x61F96F) or a tower
// (0x60130B) with the damage (0x620F0B), raises the music by 0xE
// (0x611649) when the team is [+0xAD2] − 1, and loads (0x96). With a human
// side the guns fire cannon balls (0x61E8EF; below, as the sea's).
void Sailing::fn_624950(size_t e, bool flag) {
	BattleGraph& g = save_.battles;
	const ShipMemory& s = scene_ship(e);
	if (s.u32(0x120) == 0 || (g.get<uint8_t>(e, 0x12A) & 0x2E) ||
		(g.get<uint8_t>(e, 0x130) != 0 && g.get<uint8_t>(e, 0x131) != 0)) {
		return;
	}
	const size_t b = scene_battle(e);
	const bool automatic = g.get<uint8_t>(b, 0xAC8) != 0;
	const uint8_t team = g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(e, 0x13D)));
	const size_t next = size_t(g.ptr(b, 0xC));
	const int32_t ex = g.get<int16_t>(e, 0xA), ey = g.get<int16_t>(e, 0xE);
	const uint8_t heading = g.get<uint8_t>(e, 0x11);
	// Where ship f will be: 4 steps of its last move by how far it is.
	auto ahead = [&](size_t f, int32_t& px, int32_t& py, int32_t& last) {
		const int32_t dx = g.get<int16_t>(f, 0xA) - ex, dy = g.get<int16_t>(f, 0xE) - ey;
		px = dx;
		py = dy;
		for (int i = 0; i < 4; ++i) {
			last = add32(mul(py, py), mul(px, px));
			const int32_t q = int32_t(uint32_t(root(last)) << 8) / 1328;
			int32_t vx = g.get<int32_t>(f, 0x14), vy = g.get<int32_t>(f, 0x18);
			if (automatic) {
				vx >>= 4;
				vy >>= 4;
			}
			py = add32(mul(vy, q) >> 16, dy);
			px = add32(mul(vx, q) >> 16, dx);
		}
	};
	int32_t starboard = 0x7FFFFFFF, port = 0x7FFFFFFF;
	uint16_t starboard_target = 0xFFFF, port_target = 0xFFFF;
	int32_t starboard_x = 0, starboard_y = 0, port_x = 0, port_y = 0;  // Where each target is or will be.
	for (uint16_t k = g.get<uint16_t>(b, team == 0 ? 0x10 : 0x65C); k != 0xFFFF;
		 k = g.get<uint16_t>(next, 2 * size_t(k))) {
		if (k == g.get<uint16_t>(e, 0x138)) {
			continue;
		}
		const size_t f = battle_entry(b, k);
		if (g.get<uint8_t>(f, 0x12A) & 0x6) {
			continue;
		}
		int32_t px = 0, py = 0, last = 0;
		ahead(f, px, py, last);
		const int32_t rel = uint8_t(fn_6029b0(int16_t(px), int16_t(py)) - heading);
		const int32_t d2 = add32(mul(py, py), mul(px, px));
		const int32_t spread = d2 < 0x559 ? 0x37 : 2000 / root(d2);
		if (rel + spread > 0x37 && rel - spread < 0x49) {
			if (d2 < starboard) {
				starboard = d2 - 0x50;
			}
		} else if (rel + spread > 0xB7 && rel - spread < 0xC9 && d2 < port) {
			port = d2 - 0x50;
		}
	}
	const bool ai = g.get<uint8_t>(b, 0x5D + size_t(g.get<uint8_t>(e, 0x13D))) != 0;
	for (uint16_t k = g.get<uint16_t>(b, team == 0 ? 0x65C : 0x10); k != 0xFFFF;
		 k = g.get<uint16_t>(next, 2 * size_t(k))) {
		const size_t f = battle_entry(b, k);
		if (g.get<uint8_t>(f, 0x12A) & 0x6) {
			continue;
		}
		if (!flag) {
			if (k != g.get<uint16_t>(e, 0x128) && !ai) {
				continue;
			}
			if (fn_61f02f(e, f, true)) {
				continue;
			}
		}
		int32_t px = 0, py = 0, last = 0;
		ahead(f, px, py, last);
		const int32_t rel = uint8_t(fn_6029b0(int16_t(px), int16_t(py)) - heading);
		const int32_t spread = last < 0x64 ? 0x37 : 500 / root(last);
		const int32_t d2 = add32(mul(py, py), mul(px, px));
		if (rel + spread > 0x37 && rel - spread < 0x49) {
			if (d2 < starboard && d2 <= 0x38400) {
				starboard = d2;
				starboard_target = k;
				starboard_x = px;
				starboard_y = py;
			}
		} else if (rel + spread > 0xB7 && rel - spread < 0xC9 && d2 < port && d2 <= 0x38400) {
			port = d2;
			port_target = k;
			port_x = px;
			port_y = py;
		}
	}
	if (team == 0) {
		for (uint32_t k = 0; k < 4; ++k) {
			if (g.get<int32_t>(b, 0x1EC4 + 4 * k) <= 0) {
				continue;
			}
			if (int32_t(k) != 0xFFFE - int32_t(g.get<uint16_t>(e, 0x128)) && !flag && !ai) {
				continue;
			}
			const int32_t dx = int32_t(g.get<uint16_t>(b, 0x1E9E + 2 * k)) - ex;
			const int32_t dy = int32_t(g.get<uint16_t>(b, 0x1EA6 + 2 * k)) - ey;
			const uint8_t rel = uint8_t(fn_6029b0(int16_t(dx), int16_t(dy)) - heading);
			const int32_t d2 = add32(mul(dy, dy), mul(dx, dx));
			if (rel > 0x37 && rel < 0x49) {
				if (d2 < starboard && d2 <= 0x38400) {
					starboard = d2;
					starboard_target = uint16_t(0xFFFE - k);
					starboard_x = dx;
					starboard_y = dy;
				}
			} else if (rel > 0xB7 && rel < 0xC9) {
				if (d2 < port && d2 <= 0x38400) {
					port = d2;
					port_target = uint16_t(0xFFFE - k);
					port_x = dx;
					port_y = dy;
				}
			}
		}
	}
	if (!automatic) {
		// 0x62542E..0x625899, as the sea's 0x61E628: each side with a target and
		// loaded fires a broadside of cannon balls (0x61E8EF, battle/balls.cpp)
		// at where it is or will be; with the order to fire (`flag`) and no
		// target on either side, at the closest enemy (not out) on each side by
		// its heading and, for the attackers, the closest tower standing (its
		// index k, not 0xFFFE − k, kept as the target: only its place is used),
		// only the closer side when both are loaded.
		auto broadside = [&](uint8_t side, int32_t x, int32_t y) {
			fn_61e8ef(e, side, uint16_t(g.get<uint16_t>(e, 0xA) + uint16_t(x)), uint16_t(g.get<uint16_t>(e, 0xE) + uint16_t(y)));
		};
		if (port_target != 0xFFFF && g.get<uint8_t>(e, 0x130) == 0) {
			broadside(0, port_x, port_y);
		}
		if (starboard_target != 0xFFFF && g.get<uint8_t>(e, 0x131) == 0) {
			broadside(1, starboard_x, starboard_y);
		}
		if (!flag || port_target != 0xFFFF || starboard_target != 0xFFFF) {
			return;
		}
		auto closest = [&](uint16_t k, int32_t dx, int32_t dy) {
			const int32_t d2 = add32(mul(dy, dy), mul(dx, dx));
			const uint8_t rel = uint8_t(fn_6029b0(int16_t(dx), int16_t(dy)) - heading);
			if (rel < 0x80) {
				if (d2 < starboard) {
					starboard = d2;
					starboard_target = k;
					starboard_x = dx;
					starboard_y = dy;
				}
			} else if (d2 < port) {
				port = d2;
				port_target = k;
				port_x = dx;
				port_y = dy;
			}
		};
		for (uint16_t k = g.get<uint16_t>(b, team == 0 ? 0x65C : 0x10); k != 0xFFFF;
			 k = g.get<uint16_t>(next, 2 * size_t(k))) {
			const size_t f = battle_entry(b, k);
			if (g.get<uint8_t>(f, 0x12A) & 0x6) {
				continue;
			}
			closest(k, g.get<int16_t>(f, 0xA) - ex, g.get<int16_t>(f, 0xE) - ey);
		}
		if (team == 0) {
			for (uint16_t k = 0; k < 4; ++k) {
				if (g.get<int32_t>(b, 0x1EC4 + 4 * size_t(k)) <= 0) {
					continue;
				}
				closest(k, int32_t(g.get<uint16_t>(b, 0x1E9E + 2 * size_t(k))) - ex,
					int32_t(g.get<uint16_t>(b, 0x1EA6 + 2 * size_t(k))) - ey);
			}
		}
		if (g.get<uint8_t>(e, 0x130) != 0) {
			port_target = 0xFFFF;
		}
		if (g.get<uint8_t>(e, 0x131) != 0) {
			starboard_target = 0xFFFF;
		}
		if (port_target != 0xFFFF) {
			if (starboard_target == 0xFFFF || port < starboard) {
				broadside(0, port_x, port_y);
				return;
			}
		}
		if (starboard_target != 0xFFFF) {
			broadside(1, starboard_x, starboard_y);
		}
		return;
	}
	auto fire = [&](uint16_t target, int32_t d2, uint8_t side, size_t loading) {
		if (target == 0xFFFF || g.get<uint8_t>(e, loading) != 0) {
			return;
		}
		const size_t mode = 0xACA + size_t(team);
		if (g.get<uint8_t>(b, mode) < 0x7F) {
			g.set<uint8_t>(b, mode, uint8_t(g.get<uint8_t>(b, mode) + 1));
		}
		if (target < g.get<uint16_t>(b, 0x14)) {
			const size_t t = battle_entry(b, target);
			if (g.get<uint8_t>(t, 0x12A) & 0x20) {
				g.set<uint8_t>(b, mode, 0xFF);
			}
			const int32_t damage = fn_620f0b(e, side, uint16_t(root(d2)));
			fn_61f96f(t, damage, -1, 3);
		} else {
			const int32_t damage = fn_620f0b(e, side, uint16_t(root(d2)));
			fn_60130b(b, uint8_t(0xFE - uint8_t(target)), damage);
		}
		if (uint32_t(team) + 1 == g.get<uint8_t>(b, 0xAD2)) {
			fn_611649(b, 0xE);
		}
		g.set<uint8_t>(e, loading, 0x96);
	};
	fire(port_target, port, 0, 0x130);
	fire(starboard_target, starboard, 1, 0x131);
}

// 0x621770 (method +0x0 of a town battle's scene ship; the wind's direction
// [+0x670]): its sub-step, as the sea's 0x61A9E0 (battle/ship_step.cpp) with
// the town's area (10 to 0x10F6, 0xEF8; its edges 0x190 in, 0xF70, 0xD72 and
// all four tested right, but a fleeing attacker still heads for the sea
// area's edges) and these changes:
// - a sinking ship slows down only with [+0x15C] (0 here: it drifts on);
// - no ship heads off (0x20), and a fled one or one without sailors goes on;
// - the decision to flee (fn_621770_flee) and, for the defenders (team 1),
//   fleeing is going back to their side's centre while the first tower
//   stands (the game tests tower 0 each time);
// - fleeing, without a target (the AI's target 0x6247B0) or with one, it
//   marks its way round the land first (0x625BF0);
// - a target may be a tower (0xFFFE − its number): followed to its place,
//   dropped when it falls; a target ship is dropped when out, of its team or
//   grappling a much weaker one (not for fleeing harmless);
// - close is 0x31A88 (0x15F90 with [+0x13C] and a ship), and the broadside
//   is turned to whether it would board without the flag;
// - its sails go down when the way ahead and on both sides is marked above
//   0x80 (and change only when it isn't).
// Then its boxes, its directions cleared, and it fires (method +0x8,
// 0x624950) with a target or as an AI's.
void Sailing::fn_621770(size_t e, uint8_t wind) {
	BattleGraph& g = save_.battles;
	const size_t b = scene_battle(e);
	const bool automatic = g.get<uint8_t>(b, 0xAC8) != 0;
	auto u8 = [&](size_t o) { return g.get<uint8_t>(e, o); };
	auto set8 = [&](size_t o, uint32_t v) { g.set<uint8_t>(e, o, uint8_t(v)); };
	auto flags = [&]() { return g.get<uint8_t>(e, 0x12A); };
	const uint8_t side = u8(0x13D);
	const uint8_t team = g.get<uint8_t>(b, 0x559 + size_t(side));
	const bool ai = g.get<uint8_t>(b, 0x5D + size_t(side)) != 0;
	const uint16_t place = g.get<uint16_t>(e, 0x138);
	const uint16_t count = g.get<uint16_t>(b, 0x14);
	auto strength = [&]() { return g.get<uint8_t>(b, 0x671); };
	auto item = [&](size_t at) { return g.get<uint16_t>(size_t(g.ptr(b, at)), 2 * size_t(place)); };
	auto avoid = [&](uint32_t d) { return g.get<uint8_t>(e, 0x27 + size_t(d & 0xFF)); };
	auto boxes = [&](bool check) {
		if (check && (flags() & 0x6)) {
			const uint16_t far = uint16_t(-100 * int32_t(place));
			fn_6197a0(b, item(0xA74), far, 0xFF9C, far, 0xFF9C);
			fn_6197a0(b, item(0xA78), far, 0xFF9C, far, 0xFF9C);
			return;
		}
		const int32_t h = kTownBoxHalf[scene_ship(e).u8(0xE) & 3];
		const int32_t x = g.get<int16_t>(e, 0xA), y = g.get<int16_t>(e, 0xE);
		fn_6197a0(b, item(0xA74), uint16_t(x - h), uint16_t(y - h), uint16_t(x + h), uint16_t(y + h));
		fn_6197a0(b, item(0xA78), uint16_t(x - h - 0x168), uint16_t(y - h - 0x168), uint16_t(x + h + 0x168),
			uint16_t(y + h + 0x168));
	};
	auto clear = [&]() {
		for (size_t k = 0; k < 0x100; ++k) {
			g.set<uint8_t>(e, 0x27 + k, 0);
		}
	};
	auto lower = [&]() {
		if (u8(0x26) > 0 && u8(0x149) == 0) {
			set8(0x26, u8(0x26) - 1);
			set8(0x149, 0x4B);
		}
	};
	auto keep = [&](uint8_t before) {
		if (before != u8(0x26)) {
			if (u8(0x149) != 0) {
				set8(0x26, before);
			} else {
				set8(0x149, 0x4B);
			}
		}
	};
	auto adrift = [&]() {
		g.set<uint16_t>(e, 0x20, 0xFFFF);
		fn_6264c0(e, wind, strength(), false);
		boxes(true);
		clear();
	};
	{
		const uint8_t h5 = uint8_t((u8(0x11) + 0x10) & 0x1F);
		if ((flags() & 0x4) && (h5 & 0x18)) {
			set8(0x11, h5 < 0x10 ? u8(0x11) - 1 : u8(0x11) + 1);
		}
	}
	if ((flags() & 0x4) && g.get<int32_t>(e, 0x1C) != 0) {
		if (g.get<uint32_t>(e, 0x15C) != 0) {
			const int32_t slow = automatic ? 0x1790 : 0x179;
			if (g.get<int32_t>(e, 0x1C) <= slow) {
				g.set<int32_t>(e, 0x1C, 0);
				return;
			}
			g.set<int32_t>(e, 0x1C, g.get<int32_t>(e, 0x1C) - slow);
		}
		g.set<uint16_t>(e, 0x20, 0xFFFF);
		const uint8_t sails = u8(0x26);
		set8(0x26, 0);
		fn_6264c0(e, wind, strength(), false);
		set8(0x26, sails);
	}
	if (flags() & 0x6) {
		return;
	}
	if (u8(0x149) != 0) {
		set8(0x149, u8(0x149) - 1);
	}
	{
		const int32_t x = g.get<int16_t>(e, 0xA), y = g.get<int16_t>(e, 0xE);
		if (x < 10 || y < 10 || x > 0x10F6 || y > 0xEF8) {
			if (side == 0xFF && g.get<uint8_t>(b, 0x1E98) >= save_.towns.size()) {
				return;  // (Never: a side is 0 or 1.)
			}
			set8(0x12A, flags() | 0x2);
			g.set<uint8_t>(b, 0xAC4, 0);
			g.set<uint16_t>(b, 0xAC6, place);
			g.set<uint32_t>(e, 0x134, 0);  // A UI object (released by the game; none in the emulator).
			fn_619560(b, item(0xA74));
			fn_619560(b, item(0xA78));
			return;
		}
	}
	for (const size_t o : {0x130, 0x131, 0x161}) {
		if (u8(o) != 0) {
			set8(o, u8(o) - 1);
		}
	}
	if (automatic) {
		for (const size_t o : {0x130, 0x131, 0x161}) {
			set8(o, u8(o) < 0x10 ? 0 : u8(o) - 0xF);
		}
	}
	if (flags() & 0x8) {
		if (g.get<uint8_t>(battle_entry(b, g.get<uint16_t>(e, 0x128)), 0x12A) & 0x6) {
			g.set<uint16_t>(e, 0x128, 0xFFFF);
			set8(0x12A, flags() & 0xF7);
		} else {
			if (place < g.get<uint16_t>(e, 0x128)) {
				fn_61f1bf(e);
			}
			if (g.get<uint16_t>(e, 0x128) != 0xFFFF) {
				lower();
				const size_t t = battle_entry(b, g.get<uint16_t>(e, 0x128));
				const int16_t dx = int16_t(g.get<uint16_t>(e, 0xA) - g.get<uint16_t>(t, 0xA));
				const int16_t dy = int16_t(g.get<uint16_t>(e, 0xE) - g.get<uint16_t>(t, 0xE));
				const uint8_t rel = uint8_t(u8(0x11) - fn_6029b0(dx, dy));
				if ((rel > 0 && rel < 0x48) || (rel > 0x80 && rel < 0xB8)) {
					set8(0x11, u8(0x11) + 2);
				} else if ((rel > 0x48 && rel < 0x80) || rel > 0xB8) {
					set8(0x11, u8(0x11) - 2);
				}
				g.set<int32_t>(e, 0x1C, g.get<int32_t>(e, 0x1C) - (g.get<int32_t>(e, 0x1C) >> 4));
			}
			adrift();
			return;
		}
	}
	if (!(flags() & 0x1) && g.get<uint8_t>(b, 0x5C) == 0 && !ai && g.get<uint16_t>(e, 0x128) == 0xFFFF) {
		// A human side without a target: its course and sails (as the sea's).
		const int16_t course = g.get<int16_t>(e, 0x24);
		if (course >= 0) {
			if (u8(0x11) == uint8_t(course) && course < 0x100) {
				g.set<uint16_t>(e, 0x20, 0xFFFF);
			} else {
				g.set<uint16_t>(e, 0x20, uint16_t(g.get<int16_t>(e, 0xA) + (sine(uint32_t(course)) >> 10)));
				g.set<uint16_t>(e, 0x22, uint16_t(g.get<int16_t>(e, 0xE) - (cosine(uint32_t(course)) >> 10)));
			}
		}
		if (u8(0x149) == 0) {
			if (u8(0x148) < u8(0x26)) {
				set8(0x26, u8(0x26) - 1);
				set8(0x149, 0x4B);
			} else if (u8(0x148) > u8(0x26)) {
				set8(0x26, u8(0x26) + 1);
				set8(0x149, 0x4B);
			}
		}
		fn_6264c0(e, wind, strength(), false);
		boxes(true);
		clear();
		return;
	}
	if (u8(0x161) == 0 && ai) {
		fn_621770_flee(e);
	}
	if (team == 1 && (flags() & 0x1)) {
		bool tower = false;
		for (int k = 0; k < 4; ++k) {
			if (g.get<uint32_t>(b, 0x1EC4) != 0) {  // The game tests tower 0 each time.
				tower = true;
			}
		}
		if (!tower) {
			set8(0x12A, flags() & 0xFE);
		}
	}
	if (flags() & 0x1) {
		// Fleeing: the attackers away from the enemies' centre (or the town
		// area's, near the sea area's edge), the defenders back to their own
		// centre; to the sea area's edge that way.
		fn_625bf0(e);
		const int32_t x = g.get<int16_t>(e, 0xA), y = g.get<int16_t>(e, 0xE);
		int32_t dx, dy;
		if (team == 0) {
			if (x < 0x190 || x > 0x2070 || y < 0x190 || y > 0x1C74) {
				dx = x - 0x880;
				dy = y - 0x781;
			} else {
				dx = x - g.get<int32_t>(b, 0xAB4 + 4 * size_t(1 - team));
				dy = y - g.get<int32_t>(b, 0xABC + 4 * size_t(1 - team));
			}
		} else {
			dx = g.get<int32_t>(b, 0xAB4 + 4 * size_t(team)) - x;
			dy = g.get<int32_t>(b, 0xABC + 4 * size_t(team)) - y;
		}
		if (dx < 0) {
			dy = mul(-x, dy) / dx;
			dx = -x;
		} else if (dx > 0) {
			dy = mul(0x2200 - x, dy) / dx;
			dx = 0x2200 - x;
		} else {
			dy = dy < 0 ? -y : 0x1E04 - y;
		}
		if (dy < -y) {
			dx = mul(dx, -y) / dy;
			dy = -y;
		} else if (dy > 0x1E04 - y) {
			dx = mul(0x1E04 - y, dx) / dy;
			dy = 0x1E04 - y;
		}
		g.set<uint16_t>(e, 0x20, uint16_t(x + dx));
		g.set<uint16_t>(e, 0x22, uint16_t(y + dy));
		const uint8_t before = u8(0x26);
		fn_6264c0(e, wind, strength(), true);
		keep(before);
	} else if (g.get<uint16_t>(e, 0x128) == 0xFFFF) {
		g.set<uint16_t>(e, 0x20, 0xFFFF);
		if (ai) {
			fn_6247b0(e);
			fn_625bf0(e);
		}
		const uint8_t before = u8(0x26);
		fn_6264c0(e, wind, strength(), ai);
		keep(before);
		boxes(false);
		clear();
		return;
	} else {
		fn_625bf0(e);
		const uint16_t target = g.get<uint16_t>(e, 0x128);
		const bool ship = target < count;
		const size_t t = ship ? battle_entry(b, target) : 0;
		bool drop = false;
		if (ship) {
			const uint8_t tflags = g.get<uint8_t>(t, 0x12A);
			if (tflags & 0x6) {
				drop = true;
			} else if (ai) {
				const uint8_t tteam = g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(t, 0x13D)));
				if (tteam == team) {
					drop = true;
				} else if (g.get<uint16_t>(t, 0x128) < count && (tflags & 0x8)) {
					const uint32_t theirs = scene_ship(battle_entry(b, g.get<uint16_t>(t, 0x128))).u16(0x40);
					if (int32_t(scene_ship(t).u16(0x40)) > int32_t(2 * theirs)) {
						drop = true;
					}
				}
			}
		} else if (g.get<uint32_t>(b, 0x1EC4 + 4 * size_t(uint16_t(0xFFFE - target))) == 0) {
			drop = true;
		}
		if (drop) {
			g.set<uint16_t>(e, 0x20, 0xFFFF);
			if (ai) {
				fn_6247b0(e);
			} else {
				g.set<uint16_t>(e, 0x128, 0xFFFF);
				set8(0x148, u8(0x26));
				g.set<uint16_t>(e, 0x24, u8(0x11));
			}
			const uint8_t before = u8(0x26);
			fn_6264c0(e, wind, strength(), ai || g.get<uint8_t>(b, 0x5C) != 0);
			keep(before);
			boxes(false);
			clear();
			return;
		}
		if (ship) {
			g.set<uint16_t>(e, 0x20, g.get<uint16_t>(t, 0xA));
			g.set<uint16_t>(e, 0x22, g.get<uint16_t>(t, 0xE));
		} else {
			const size_t k = size_t(uint16_t(0xFFFE - target));
			g.set<uint16_t>(e, 0x20, g.get<uint16_t>(b, 0x1E9E + 2 * k));
			g.set<uint16_t>(e, 0x22, g.get<uint16_t>(b, 0x1EA6 + 2 * k));
		}
		const int32_t x = g.get<int16_t>(e, 0xA), y = g.get<int16_t>(e, 0xE);
		const int32_t dx0 = g.get<int16_t>(e, 0x20) - x, dy0 = g.get<int16_t>(e, 0x22) - y;
		int32_t dx = dx0, dy = dy0;
		if (ship) {
			for (int i = 0; i < 4; ++i) {
				const int32_t q = int32_t(uint32_t(root(add32(mul(dx, dx), mul(dy, dy)))) << 8) / 0x530;
				int32_t vx = g.get<int32_t>(t, 0x14), vy = g.get<int32_t>(t, 0x18);
				if (automatic) {
					vx >>= 4;
					vy >>= 4;
				}
				dx = add32(dx0, mul(vx, q) >> 16);
				dy = add32(dy0, mul(vy, q) >> 16);
			}
		}
		const uint32_t d2 = uint32_t(add32(mul(dx, dx), mul(dy, dy)));
		const uint8_t tteam = ship ? g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(t, 0x13D))) : 1;
		if (tteam != team) {
			// The edges of the town area are to be avoided, unless boarding the
			// target ship that way.
			auto boarding = [&]() { return ship && fn_61f02f(e, t, false); };
			if (x < 0x190 && !(boarding() && dx <= 0)) {
				const uint8_t v = uint8_t(std::min(0x190 - x, 0xFF));
				for (size_t k = 0; k < 0x80; ++k) {
					g.set<uint8_t>(e, 0x27 + 0x80 + k, v);
				}
			}
			if (y < 0x190 && !(boarding() && dy <= 0)) {
				for (size_t k = 0; k < 0x80; ++k) {
					g.set<uint8_t>(e, 0x27 + ((k + 0xC0) & 0xFF), 0xFF);
				}
			}
			if (x > 0xF70 && !(boarding() && dx >= 0)) {
				for (size_t k = 0; k < 0x80; ++k) {
					g.set<uint8_t>(e, 0x27 + k, 0xFF);
				}
			}
			if (y > 0xD72 && !(boarding() && dy >= 0)) {
				for (size_t k = 0; k < 0x80; ++k) {
					g.set<uint8_t>(e, 0x27 + 0x40 + k, 0xFF);
				}
			}
		}
		const bool wide = u8(0x13C) != 0 && ship;
		const uint32_t near = wide ? 0x15F90 : 0x31A88, far = wide ? 0x1FA40 : 0x38400;
		if (d2 < near) {
			if (tteam == team) {
				if (u8(0x149) == 0 && u8(0x26) != 0) {
					set8(0x26, u8(0x26) - 1);
					set8(0x149, 0x4B);
				}
				g.set<uint16_t>(e, 0x20, 0xFFFF);
			} else {
				set8(0x12A, flags() | 0x10);
			}
		}
		const bool boards = ship && fn_61f02f(e, t, false);
		if (d2 < far && (flags() & 0x10) && !boards) {
			// Broadside: across the line to the target, the freest side.
			g.set<uint16_t>(e, 0x20, 0xFFFF);
			uint8_t dir = fn_6029b0(int16_t(dy), -int32_t(int16_t(dx)));
			const uint8_t h = u8(0x11);
			if (uint8_t(h - dir + 0x40) > 0x80) {
				dir = uint8_t(dir + 0x80);
			}
			if (avoid(dir) > 0x80 && avoid(uint8_t(dir + 0x80)) < avoid(dir)) {
				dir = uint8_t(dir + 0x80);
			}
			if (avoid(dir) != 0) {
				uint8_t k = uint8_t(dir - h);
				k = (k & 0x80) ? uint8_t((k >> 1) + 0x80) : uint8_t(k >> 1);
				uint8_t best = uint8_t(h + k);
				uint8_t i = 1, least = 0xFF;
				for (;;) {
					if (avoid(uint8_t(dir + i)) == 0 || avoid(uint8_t(dir - i)) == 0) {
						break;
					}
					++i;
					if (avoid(uint8_t(dir + i)) < least) {
						least = avoid(uint8_t(dir + i));
						best = uint8_t(dir + i);
					}
					if (avoid(uint8_t(dir - i)) < least) {
						least = avoid(uint8_t(dir - i));
						best = uint8_t(dir - i);
					}
					if (i == 0x80) {
						break;
					}
				}
				dir = best;
			}
			if (int32_t(dir) - int32_t(h) > 1 || int32_t(h) - int32_t(dir) > 1) {
				fn_6271d7(e, dir);
			}
			if (u8(0x149) == 0) {
				const uint8_t hh = u8(0x11);
				if (avoid(hh) > 0x80 && avoid(uint8_t(hh + 1)) > 0x80 && avoid(uint8_t(hh - 1)) > 0x80) {
					if (u8(0x26) > 0) {
						set8(0x26, u8(0x26) - 1);
						set8(0x149, 0x4B);
					}
				} else if (u8(0x26) < 4) {
					set8(0x26, u8(0x26) + 1);
					set8(0x149, 0x4B);
				}
			}
			fn_6264c0(e, wind, strength(), false);
		} else {
			set8(0x12A, flags() & 0xEF);
			if (u8(0x149) == 0 && avoid(u8(0x11)) > 0x80 && u8(0x26) > 0) {
				set8(0x26, u8(0x26) - 1);
				set8(0x149, 0x4B);
			}
			const uint8_t before = u8(0x26);
			fn_6264c0(e, wind, strength(), true);
			if (before != u8(0x26)) {
				if (u8(0x149) != 0 || avoid(u8(0x11)) > 0x80) {
					set8(0x26, before);
				} else {
					set8(0x149, 0x4B);
				}
			}
		}
	}
	boxes(true);
	clear();
	if (g.get<uint16_t>(e, 0x128) != 0xFFFF || ai) {
		fn_624950(e, false);
	}
}

// The decision to flee of a town battle's AI ship (part of 0x621770, at
// 0x622706). The attackers (team 0), as the sea's, by the enemy's best arms,
// its hull and the teams' sums of the sub-step, but: the hull's limit always
// as for a team of one; with its hull as at the start and nobody fled yet
// ([+0xAC9]) it stops fleeing; the first rule compares the strengths
// without doubling; and otherwise it always stops. The defenders (team 1)
// flee (to their centre) when weaker (0x62125B) than both teams' strengths
// and further than 200 from their side's centre on an axis.
void Sailing::fn_621770_flee(size_t e) {
	BattleGraph& g = save_.battles;
	const size_t b = scene_battle(e);
	const uint8_t team = g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(e, 0x13D)));
	const ShipMemory& s = scene_ship(e);
	auto flag = [&](uint8_t bit) { g.set<uint8_t>(e, 0x12A, uint8_t(g.get<uint8_t>(e, 0x12A) | bit)); };
	auto unflag = [&]() { g.set<uint8_t>(e, 0x12A, uint8_t(g.get<uint8_t>(e, 0x12A) & 0xFE)); };
	const uint32_t a90 = g.get<uint32_t>(b, 0xA90), a94 = g.get<uint32_t>(b, 0xA94);
	if (team == 0) {
		uint32_t limit = uint32_t(mul(g.get<int32_t>(b, 0xAA0 + 4 * size_t(1 - team)), 0x8C));
		if (uint32_t(mul(int32_t(limit), 3)) > uint32_t(mul(s.s32(0x14), 2))) {
			limit = uint32_t(mul(s.s32(0x14), 2) / 3);
		}
		if (uint32_t(mul(int32_t(limit), 4)) < s.u32(0x14)) {
			limit = uint32_t(s.s32(0x14) >> 2);
		}
		if (g.get<uint32_t>(e, 0x144) == s.u32(0x18) && g.get<uint8_t>(b, 0xAC9) == 0) {
			unflag();
			return;
		}
		const uint32_t n0 = g.get<uint8_t>(b, 0xAB0);
		const int32_t a9a = g.get<uint16_t>(b, 0xA9A), a9c = g.get<uint16_t>(b, 0xA9C);
		auto flee = [&]() {
			flag(0x1);
			g.set<uint8_t>(b, 0xAC9, 1);
		};
		if (a90 * n0 < a94 && a9c * int32_t(n0) < a9a * 2) {
			flee();
		} else if (a90 * n0 * 4 < a94) {
			flee();
		} else if (a9c * int32_t(n0) * 3 < a9a) {
			flee();
		} else if (s.u32(0x18) < limit) {
			flee();
		} else if (s.u32(0x120) == 0 && int32_t(s.u16(0x40)) < a9a * 2) {
			flee();
		} else {
			unflag();
		}
		return;
	}
	const uint32_t mine = uint32_t(fn_62125b(e));
	if (mine < a94 && mine < a90) {
		const int32_t dx = add32(g.get<int32_t>(b, 0xAB4 + 4 * size_t(team)), -int32_t(g.get<int16_t>(e, 0xA)));
		const int32_t dy = add32(g.get<int32_t>(b, 0xABC + 4 * size_t(team)), -int32_t(g.get<int16_t>(e, 0xE)));
		if ((dx < 0 ? -dx : dx) > 0xC8 || (dy < 0 ? -dy : dy) > 0xC8) {
			flag(0x1);
			return;
		}
	}
	unflag();
}

}  // namespace p3::data
