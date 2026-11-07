// The pirates at sea (piece 2 of docs/cloud/battles.md §5 and §7): the
// convoy state 0x12 of 0x506720 (0x507A65) and what it calls, and the
// per-town waypoints of the navigation object they choose from.
//
// The pirate convoys move only with Sailing::set_pirates(true), as with
// town_oracle.py --pirates; until the battles are there, a pirate that would
// attack goes on as if it had no target and a convoy that ends its route at a
// town gets a new course (the rule of piece 2, «Decisions for the user»).

#include <algorithm>
#include <cstring>

#include "p3data/battle/pirate_nests.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick::alderman_detail {
// 0x51A470 (town_tick/alderman.cpp): the pirate of a ship.
uint8_t fn_51a470(const SaveGame& save, int ship);
// 0x51A750 (town_tick/alderman.cpp): the ship goes up n levels.
void fn_51a750(SaveGame& save, int ship, uint8_t n);
}  // namespace p3::data::tick::alderman_detail

namespace p3::data {

namespace {

constexpr uint32_t kMaxTargetDistance = global::kShips + 0xFC;  // u16: distance² to keep a target
constexpr uint32_t kSearchDistance = global::kShips + 0xFA;  // u16: distance² to take a new one
constexpr uint32_t kSearchWidth = global::kShips + 0xFE;  // u16: how far in x the search walks
constexpr uint16_t kStatusStorm = 0x15;
constexpr uint16_t kStatusSailing = 0x0F;
constexpr uint16_t kStatusPursuing = 0x10;

// 0x673564 (x0, y0, x1, y1): the shallows. 0x50E340 and 0x50D9E0 take the
// bounds as inside; 0x448780 has its own copy (0x66D648) with strict bounds.
constexpr int16_t kShallows[3][4] = {{442, 1521, 658, 1691}, {2265, 147, 2332, 453}, {1497, 1263, 1578, 1444}};

// 0x67ADC0: the battle speed of each type of ship.
constexpr uint16_t kBattleSpeed[4] = {0x2B5, 0x2B5, 0x242, 0x242};

int16_t whole_x(const SavedShip& s) {
	return int16_t(s.memory.u16(0x1E));
}
int16_t whole_y(const SavedShip& s) {
	return int16_t(s.memory.u16(0x22));
}
int32_t distance2(const SavedShip& a, const SavedShip& b) {
	const int32_t dx = int32_t(whole_x(a)) - whole_x(b);
	const int32_t dy = int32_t(whole_y(a)) - whole_y(b);
	return dy * dy + dx * dx;
}

}  // namespace

// 0x50E340 (thiscall on the ships object, x and y): the point is in the shallows.
bool fn_50e340(int16_t x, int16_t y) {
	for (const auto& r : kShallows) {
		if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]) {
			return true;
		}
	}
	return false;
}

// 0x448780 (thiscall on the navigation object, the town): the waypoints of
// the pirates round a town ([+0x20] their number, [+0x24] the cells, packed as
// x | y << 16). Every 16 cells of the grid, the nearest water (0x448450,
// radius 7) whose pixel is in a ring round the town's approach point (from 0
// to 200 px, then 25 px more each time), outside the shallows (strict bounds)
// and not on a town, and whose route to the town (0x445010) is shorter than a
// length (6000, then 750 more each time; per leg 29 · the longer side + 12 ·
// the shorter one). The ones that were too long wait for the next ring. Until
// there are 10.
//
// The game keeps the length of each cell in a second array and, when it moves
// a cell into the kept ones, copies the length the wrong way (the kept cell
// leaves its length behind, see «Game bugs»): reproduced as it is.
void Sailing::fn_448780(int town) {
	const int width = nav_.width();
	const int height = nav_.height();
	const size_t capacity = size_t(width * height / 256);
	std::vector<uint32_t> cells(capacity), lengths(capacity);
	const SeaMapTown& t = map_towns_.at(size_t(save_.towns[size_t(town)].town_id()));
	const int16_t tx = int16_t(t.approach_x);
	const int16_t ty = int16_t(t.approach_y);
	uint32_t outer = 200, inner = 0, limit = 6000;
	size_t found = 0, kept = 0;
	auto pack = [](MapPoint c) { return uint32_t(uint16_t(c.x)) | uint32_t(uint16_t(c.y)) << 16; };
	do {
		for (size_t j = kept; j < found; ++j) {
			if (lengths[j] < limit) {
				if (j != kept) {
					std::swap(cells[kept], cells[j]);
					lengths[j] = lengths[kept];  // the game's copy, one way only
				}
				++kept;
			}
		}
		for (int x = 0; x < width; x += 16) {
			for (int y = 0; y < height; y += 16) {
				MapPoint cell{int16_t(x), int16_t(y)};
				if (!nav_.find_water(cell, 7)) {
					continue;
				}
				const MapPoint p{NavData::to_pixel(cell.x), NavData::to_pixel(cell.y)};
				const int32_t dx = int32_t(p.x) - tx;
				const int32_t dy = int32_t(p.y) - ty;
				const uint32_t d = uint32_t(dy * dy + dx * dx);
				if (d > outer * outer || d <= inner * inner) {
					continue;
				}
				bool shallow = false;
				for (const auto& r : kShallows) {
					if (p.x > r[0] && p.y > r[1] && p.x < r[2] && p.y < r[3]) {
						shallow = true;
						break;
					}
				}
				if (shallow) {
					continue;
				}
				bool on_town = false;
				for (const SeaMapTown& m : map_towns_) {
					if ((m.symbol_x < p.x && m.symbol_y < p.y && m.symbol_x + 22 > p.x && m.symbol_y + 22 > p.y) ||
							(m.approach_x == p.x && m.approach_y == p.y)) {
						on_town = true;
						break;
					}
				}
				if (on_town) {
					continue;
				}
				const std::shared_ptr<SeaRoute> route = route_for(p, MapPoint{tx, ty}).route;
				if (!route) {
					continue;
				}
				uint32_t length = 0;
				for (size_t k = 1; k < route->size(); ++k) {
					const int32_t ax = std::abs(int32_t((*route)[k - 1].x) - (*route)[k].x);
					const int32_t ay = std::abs(int32_t((*route)[k - 1].y) - (*route)[k].y);
					length += ay > ax ? uint32_t(29 * ay + 12 * ax) : uint32_t(29 * ax + 12 * ay);
				}
				if (length < limit) {
					++found;
					cells[found - 1] = cells[kept];
					lengths[found - 1] = lengths[kept];
					cells[kept] = pack(cell);
					++kept;
				} else {
					cells[found] = pack(cell);
					lengths[found] = length;
					++found;
				}
			}
		}
		inner = outer;
		outer += 25;
		limit += 750;
	} while (kept < 10 && inner < 0x10000);  // the second test can't fail on the game's map
	waypoints_.resize(std::max(waypoints_.size(), size_t(town) + 1));
	waypoints_[size_t(town)].assign(cells.begin(), cells.begin() + std::ptrdiff_t(kept));
	has_waypoints_.resize(waypoints_.size());
	has_waypoints_[size_t(town)] = true;
}

const std::vector<uint32_t>& Sailing::waypoints(int town) {
	if (size_t(town) >= has_waypoints_.size() || !has_waypoints_[size_t(town)]) {
		fn_448780(town);
	}
	return waypoints_[size_t(town)];
}

// 0x612930 (thiscall on the battles 0x6E55D0, the ship): its battle speed.
// The load as in 0x5170F0 (614 · cargo / capacity) against the speed of its
// type (0x67ADC0), the hull (179 · hull / maximum + 0x71, from 0xA6 to 0x100)
// and the captain's skill ([+0x9]: · (skill + 2550) / 2550). 1 without
// capacity or maximum hull.
uint32_t Sailing::fn_612930(uint16_t index) const {
	const SavedShip& s = save_.ships[index];
	const int32_t capacity = s.capacity();
	const int32_t max_hull = s.max_health();
	if (capacity == 0 || max_hull == 0) {
		return 1;
	}
	const int32_t load = int32_t(uint32_t(s.used()) * 614u) / capacity;
	const uint32_t base = (uint32_t(0x1000 - load) * kBattleSpeed[s.type() & 3]) >> 12;
	uint32_t hull = uint32_t(s.health()) * 179u / uint32_t(max_hull) + 0x71;
	hull = std::clamp(hull, 0xA6u, 0x100u);
	const uint32_t speed = (hull * base) >> 10;
	if (s.captain() >= save_.globals.u16(global::kCaptainCount)) {
		return speed;
	}
	return (uint32_t(save_.captains[s.captain()].u8(0x9)) + 2550u) * speed / 2550u;
}

// 0x50D9E0 (thiscall on the ships object, the guide of a pirate convoy): its
// target [+0x50]. Kept if it still sails (0x0F, 0x10), nearer than [+0xFC]
// and out of the shallows; otherwise the nearest ship by the list by x within
// [+0xFE] in x, nearer than [+0xFA], out of the shallows and not a storm.
// True if it has one.
bool Sailing::fn_50d9e0(uint16_t index) {
	SavedShip& p = save_.ships[index];
	const uint32_t count = save_.ships.size();
	const GlobalMemory& g = save_.globals;
	const uint16_t target = p.memory.u16(0x50);
	if (target < count) {
		const SavedShip& t = save_.ships[target];
		if (t.status() == kStatusSailing || t.status() == kStatusPursuing) {
			if (int32_t(g.u16(kMaxTargetDistance)) > distance2(t, p) && !fn_50e340(whole_x(t), whole_y(t))) {
				return true;
			}
		}
	}
	p.memory.u16(0x50) = 0xFFFF;
	int32_t best = g.u16(kSearchDistance);
	const int32_t width = g.u16(kSearchWidth);
	auto look = [&](uint32_t i) {
		const SavedShip& s = save_.ships[i];
		if (s.status() == kStatusStorm) {
			return;
		}
		const int32_t d = distance2(s, p);
		if (d < best && !fn_50e340(whole_x(s), whole_y(s))) {
			best = d;
			p.memory.u16(0x50) = uint16_t(i);
		}
	};
	for (uint32_t i = p.memory.u16(0xA); i < count; i = save_.ships[i].memory.u16(0xA)) {
		if (whole_x(save_.ships[i]) < whole_x(p) - width) {
			break;
		}
		look(i);
	}
	for (uint32_t i = p.memory.u16(0xC); i < count; i = save_.ships[i].memory.u16(0xC)) {
		if (whole_x(save_.ships[i]) > whole_x(p) + width) {
			break;
		}
		look(i);
	}
	return p.memory.u16(0x50) < count;
}

// 0x515360 (cdecl, the guide of a pirate convoy): whether to attack its
// target [+0x50] now (true: it is within 20 px and stops). Not its own
// ([+0x15D]), a merchant's ship that sails; an AI merchant without the flag
// 4 of [+0x8] only when the pirate doesn't wait ([+0x138] = 0), a human one
// only if [0x39C + home town] of the merchant + the difficulty ≥ 2 and the
// wait ≤ 0x900. A privateer ([+0x15D] a merchant) always. Otherwise the
// pirates must not be much slower (19 · the target's worst battle speed ≤
// 20 · their best), nor weaker (7 · crews and arms of the target ≤ 10 · theirs),
// the target must carry cargo and a pirate have 2000 free, and the target be
// out of the shallows. Then it heads for it (route cleared, [+0x52] = the
// target) unless its route already ends next to it. [+0x50] is cleared when
// the target is no good.
bool Sailing::fn_515360(uint16_t index) {
	SavedShip& p = save_.ships[index];
	const uint32_t count = save_.ships.size();
	const uint16_t convoys = save_.convoy_count();
	const uint16_t target = p.memory.u16(0x50);
	p.memory.u16(0x52) = 0xFFFF;
	if (target >= count) {
		return false;
	}
	const SavedShip& t = save_.ships[target];
	auto drop = [&]() {
		p.memory.u16(0x50) = 0xFFFF;
		return false;
	};
	if (t.owner() == p.memory.u8(0x15D) || t.owner() >= save_.merchant_count) {
		return drop();
	}
	if (t.status() != kStatusSailing && t.status() != kStatusPursuing) {
		return drop();
	}
	const MerchantMemory& m = save_.merchants[t.owner()].memory;
	const uint16_t kind = m.u16(0x8);
	if (kind != 0) {
		if (kind & 4) {
			return drop();
		}
		if (p.timer() > 0) {
			return false;
		}
	} else {
		if (uint32_t(m.u8(0x39C + m.u8(0x19))) + save_.settings.u8(0x13) < 2) {
			return drop();
		}
		if (p.timer() > 0x900) {
			return false;
		}
	}
	if (p.memory.u8(0x15D) < save_.merchant_count) {
		return true;
	}
	if (p.convoy() >= convoys) {
		return false;
	}
	// The pirates: their best battle speed, crews, crews or arms, free space.
	uint32_t fastest = 0;
	uint32_t crews = 0, strength = 0;
	bool space = false;
	for (uint32_t i = save_.convoys[p.convoy()].u16(0xA); i < count; i = save_.ships[i].next_in_list()) {
		const SavedShip& s = save_.ships[i];
		fastest = std::max(fastest, fn_612930(uint16_t(i)));
		const int32_t crew = s.sailors();
		crews += uint32_t(crew);
		strength += uint32_t(crew > s.memory.s32(0x120) ? crew : s.memory.s32(0x120));
		if (s.capacity() - s.equipment() - s.used() >= 0x7D0) {
			space = true;
		}
	}
	// The target: its worst battle speed, arms and crews.
	uint32_t slowest, arms, crew;
	bool cargo;
	if (t.convoy() >= convoys) {
		slowest = fn_612930(target);
		arms = uint32_t(t.memory.s32(0x120));
		crew = t.sailors();
		cargo = t.used() > 0;
	} else {
		slowest = 0x7FFFFFFF;
		arms = 0;
		crew = 0;
		cargo = false;
		for (uint32_t i = save_.convoys[t.convoy()].u16(0xA); i < count; i = save_.ships[i].next_in_list()) {
			const SavedShip& s = save_.ships[i];
			slowest = std::min(slowest, fn_612930(uint16_t(i)));
			const int32_t c = s.sailors();
			crew += uint32_t(c);
			arms += uint32_t(c > s.memory.s32(0x120) ? c : s.memory.s32(0x120));
			if (s.used() > 0) {
				cargo = true;
			}
		}
	}
	if (slowest == 0x7FFFFFFF || slowest * 19 > fastest * 20 || (arms + crew) * 7 > (strength + crews) * 10 || !cargo ||
			!space) {
		return drop();
	}
	if (fn_50e340(whole_x(t), whole_y(t))) {
		return drop();
	}
	if (distance2(t, p) <= 0x190) {
		clear_ship_route(p);
		return true;
	}
	if (p.route && !p.route->empty()) {
		const MapPoint last = p.route->back();
		const int32_t dx = int32_t(last.x) - whole_x(t);
		const int32_t dy = int32_t(last.y) - whole_y(t);
		if (dy * dy + dx * dx < 10) {
			return false;
		}
	}
	clear_ship_route(p);
	p.memory.u16(0x52) = p.memory.u16(0x50);
	return false;
}

namespace {

constexpr uint32_t kTick = 0x6DE4B4;  // u32
constexpr uint32_t kTownCount = 0x6DE4B0;  // u32 ([+0x10] of the world)
constexpr uint32_t kWorld4 = 0x6DE4A4;  // u32 [+0x4] of the world
constexpr uint32_t kTownOrder = 0x6DE4B8;  // u8 per town: its number in StadtDaten.ini
constexpr uint32_t kTownTable = 0x6DDB90;  // 0x34 bytes per number (load_game_scripts)
constexpr uint32_t kMaxConvoy = global::kShips + 0xF8;  // u16
constexpr uint8_t kMinSailors[4] = {5, 8, 10, 12};  // 0x673660
constexpr uint32_t kArtilleryBase = 9;  // the low byte of 0x672CC8

// The generator of the game (0x46C320 creates, 0x46C340 seeds, 0x46C380 the
// next one modulo n), as in town_tick/events.cpp.
struct GameRandom {
	uint32_t seed = 0x499602D2;
	uint32_t next(uint32_t n) {
		uint32_t x = 0xA205B063u - seed * 0x44BF19B3u;
		x = (x & 1) ? (x >> 1) | 0x80000000u : x >> 1;
		seed = x;
		return x % n;
	}
};

}  // namespace

// 0x5159C0 (cdecl, n): a town number of StadtDaten.ini that is not a town of
// the game, a pirate nest or marked in [+0x2F] of the table: the n-th one
// (modulo how many there are), or 0.
uint8_t fn_5159c0(const SaveGame& save, uint32_t n) {
	const GlobalMemory& g = save.globals;
	uint8_t used[40];
	for (uint32_t i = 0; i < 40; ++i) {
		used[i] = g.u8(kTownTable + 0x34 * i + 0x2F);
	}
	for (uint32_t k = g.u32(kTownCount); k > 0; --k) {
		used[g.u8(kTownOrder + k - 1)] = 1;
	}
	for (const auto& p : save.pirates) {
		if (p && p->u8(0xE) < 40) {
			used[p->u8(0xE)] = 1;
		}
	}
	uint8_t free[40];
	uint32_t count = 0;
	for (uint32_t i = 0; i < 40; ++i) {
		if (!used[i]) {
			free[count++] = uint8_t(i);
		}
	}
	return count ? free[n % count] : 0;
}


// 0x505C90 (thiscall on the ships object, the address of the convoy's link in
// the list): a new course for a pirate convoy without a route. Returns true if
// the convoy left the list (disbanded at its nest; the walk goes on from the
// same link).
// - Each ship: the crew's rest byte [+0x3F] goes up to the difficulty + itself
//   + 1 when under 2; the convoy goes home if a ship is weak (fewer sailors
//   than its type needs, 0x673660, hull under 8/10 of [+0x14] by tenths of
//   eighths, fewer arms than 18) and isn't a privateer, or carries cargo, or
//   the free space is under half the capacity (at most 30000).
// - A ship with [+0x136] & 0x80: not home; and with [+0x136] & 0x40 on the
//   guide and less than 2000 free, every ship's hull becomes 1.
// - More ships than [+0xF8] (and more than 3): to one of the first 16 towns
//   with the flag 0x40 in [0x2C8] (counted from the guide's captain, the world
//   and the guide), or, if it is already there, to a free town number
//   (0x5159C0). Home: to its pirate's nest (0x51A470, [+0xE] of the pirate); at
//   the nest it is disbanded (0x514B80, below), else the
//   wait [+0x138] grows 0x700. Otherwise: to the ship it saw ([+0x52]) if that
//   sails, is not at a town and not in the shallows; or to a waypoint of a
//   town (0x448780: the guide's [+0x159] if it is a town, else one at random)
//   from a random one on, not where it is.
// - The default course is (0x3D2, 0x317), or (0x220, 0x368) when it is at x =
//   0x3D2; then 0x5172A0 (set_destination) and, if the guide's route is not its
//   own and the guide is not the first ship, the first ship's hull becomes 1.
//
// At the nest the convoy leaves the list and is disbanded (0x514B80); with
// set_pirate_disbanding(false), as the oracle with --no-disband (the jump
// 0x505FF1 → 0x506029 of piece 2), it goes on as if it were elsewhere.
bool Sailing::fn_505c90(uint16_t index, uint16_t& link) {
	GlobalMemory& g = save_.globals;
	const uint32_t count = save_.ships.size();
	GameMemory<0x3C>& c = save_.convoys[index];
	uint32_t ships = 0;
	int32_t capacity = 0, free_space = 0;
	bool home = false, flagged = false;
	for (uint32_t i = c.u16(0xA); i < count;) {
		SavedShip& s = save_.ships[i];
		const uint32_t next = s.next_in_list();
		const int8_t rest = int8_t(s.memory.u8(0x3F));
		if (rest < 2) {
			s.memory.u8(0x3F) = uint8_t(save_.settings.u8(0x13) + uint8_t(rest) + 1);
		}
		++ships;
		const bool weak = s.sailors() < kMinSailors[s.type() & 3] || (s.health() >> 3) * 10 < s.max_health() ||
			int32_t(2 * kArtilleryBase) > s.memory.s32(0x120);
		if (weak && s.memory.u8(0x15D) >= save_.merchant_count) {
			home = true;
		} else if (s.used() > 0) {
			home = true;
		} else {
			const int32_t room = s.capacity() - s.equipment();
			free_space += room - s.used();
			capacity += room;
		}
		if (s.memory.u8(0x136) & 0x80) {
			flagged = true;
		}
		i = next;
	}
	const int32_t half = capacity < 60000 ? capacity >> 1 : 30000;
	if (half > free_space) {
		home = true;
	}
	if (flagged) {
		home = false;
		const SavedShip& guide = save_.ships[c.u16(0x10)];
		if ((guide.memory.u8(0x136) & 0x40) && free_space < 0x7D0) {
			for (uint32_t i = c.u16(0xA); i < count; i = save_.ships[i].next_in_list()) {
				save_.ships[i].health() = 1;
			}
		}
	}
	const uint16_t guide_index = c.u16(0x10);
	SavedShip& guide = save_.ships[guide_index];
	int32_t x, y;
	if (guide.memory.u16(0x1E) == 0x3D2) {
		x = 0x220;
		y = 0x368;
	} else {
		x = 0x3D2;
		y = 0x317;
	}
	auto approach_of = [&](uint32_t number) {
		x = g.s32(kTownTable + 0x34 * number + 0x20);
		y = g.s32(kTownTable + 0x34 * number + 0x24);
	};
	if (ships > g.u16(kMaxConvoy) && ships > 3) {
		uint32_t pick = ((guide.memory.u8(0x42) & 7) + (g.u32(kWorld4) >> 2) + (guide_index & 1)) & 0xF;
		for (uint32_t t = 0; t < 16; ++t) {
			if ((save_.towns[t].memory.u32(0x2C8) & 0x40) == 0x40) {
				if (pick == 0) {
					approach_of(g.u8(kTownOrder + t));
					break;
				}
				--pick;
			}
		}
		if (x == whole_x(guide) && y == whole_y(guide)) {
			approach_of(fn_5159c0(save_, g.u32(kTick) & 0x3F));
		}
	} else if (home) {
		const uint8_t k = tick::alderman_detail::fn_51a470(save_, guide_index);
		if (save_.pirates[k]) {
			const uint32_t nest = save_.pirates[k]->u8(0xE);
			const int32_t nx = g.u16(kTownTable + 0x34 * nest + 0x20);
			const int32_t ny = g.u16(kTownTable + 0x34 * nest + 0x24);
			if (nx == whole_x(guide) && ny == whole_y(guide) && disband_) {
				// At the nest: out of the list, the guide out of the list by x, and
				// disbanded (0x514B80).
				link = c.u16(0x8);
				c.u16(0x8) = 0xFFFF;
				unlink_x(c.u16(0x10));
				fn_514b80(k, index);
				return true;
			}
			x = nx;
			y = ny;
		}
		guide.timer() = uint16_t(guide.timer() + 0x700);
	} else {
		const uint16_t seen = guide.memory.u16(0x52);
		bool chosen = false;
		if (seen < count && save_.ships[seen].status() == kStatusSailing) {
			const SavedShip& t = save_.ships[seen];
			if (town_at(MapPoint{whole_x(t), whole_y(t)}) < 0 && !fn_50e340(whole_x(t), whole_y(t))) {
				x = whole_x(t);
				y = whole_y(t);
				chosen = true;
			}
		}
		if (!chosen) {
			GameRandom r;
			r.seed = g.u32(kTick);
			const uint8_t own = guide.memory.u8(0x159);
			const uint32_t towns = g.u32(kTownCount);
			const uint32_t town = towns > own ? own : r.next(towns);
			const std::vector<uint32_t>& points = waypoints(int(town));
			const uint32_t n = uint32_t(points.size());
			if (n > 0) {
				uint32_t k = r.next(n);
				do {
					const uint32_t p = points[k];
					x = NavData::to_pixel(int16_t(p & 0xFFFF));
					y = NavData::to_pixel(int16_t(p >> 16));
					if (++k >= n) {
						k = 0;
					}
				} while (x == whole_x(guide) && y == whole_y(guide));
			}
		}
	}
	set_destination(guide, -1, x, y);
	if (guide.own_route() == 0 && guide_index != c.u16(0xA)) {
		save_.ships[c.u16(0xA)].health() = 1;
	}
	return false;
}

// 0x514B80 (on pirate k's object, the convoy): a pirate convoy disbanded at
// its nest. Each ship leaves the convoy and goes to the head of the pirate's
// list of ships at the nest ([+0x12], linked by [+0x6]), off the map ([+0x1C],
// [+0x20] = 0x7FFFFFFF); its cargo is sold (each ware by 0x673A18, a float,
// truncated, added up over all the ships) and emptied; with less than half
// its hull it loses its owner ([+0x15D] = 0xFF); under level 2 it goes up one
// (0x51A750); and if it has a captain (and not in a network game), operation
// 0x12 is queued, for the convoy's guide (if the guide has a captain), not
// for the ship (see «Game bugs» in docs/cloud/battles-2.md). Then the convoy
// is deleted (0x50A3D0), the pirate's [+0x16] grows by the sum / 1000 and
// 0x515DA0 plans a building of the nest.
void Sailing::fn_514b80(size_t k, uint16_t convoy) {
	// 0x673A18 (float): what each ware is worth to the pirates.
	static constexpr float kWorth[24] = {0.055f, 0.47855f, 0.22005f, 0.174f, 0.1425f, 0.55f, 1.4f, 1.1f, 1.034f,
		3.3825f, 0.4125f, 0.0275f, 1.278f, 1.12f, 0.44f, 0.278f, 0.44f, 0.22f, 0.855f, 0.0399f, 10.0f, 8.0f, 12.0f,
		14.0f};
	GlobalMemory& g = save_.globals;
	GameMemory<0x18>& p = *save_.pirates[k];
	const uint32_t count = save_.ships.size();
	int32_t sum = 0;
	for (uint16_t i = save_.convoys[convoy].u16(0xA); i < count; i = save_.convoys[convoy].u16(0xA)) {
		SavedShip& s = save_.ships[i];
		s.memory.u16(0x8) = 0xFFFF;
		save_.convoys[convoy].u16(0xA) = s.memory.u16(0x6);
		s.memory.u16(0x6) = p.u16(0x12);
		p.u16(0x12) = i;
		s.memory.s32(0x1C) = 0x7FFFFFFF;
		s.memory.s32(0x20) = 0x7FFFFFFF;
		for (int w = 0; w < 24; ++w) {
			const float amount = float(s.memory.s32(0x54 + 4 * size_t(w)));
			sum += int32_t(tick::ftol(double(amount) * double(kWorth[w])));
			s.memory.s32(0x54 + 4 * size_t(w)) = 0;
		}
		if ((s.max_health() >> 1) > s.health()) {
			s.memory.u8(0x15D) = 0xFF;
		}
		if (s.memory.u8(0xF) < 2) {
			tick::alderman_detail::fn_51a750(save_, i, 1);
		}
		if (s.captain() < g.u16(global::kCaptainCount) && g.u32(0x6DF31C) == 0) {
			const uint16_t guide = save_.convoys[convoy].u16(0x10);
			if (guide < count && save_.ships[guide].captain() < g.u16(global::kCaptainCount)) {
				uint8_t op[0x14] = {};
				auto put32 = [&op](size_t o, uint32_t v) { std::memcpy(op + o, &v, 4); };
				put32(0x0, 0x12);
				put32(0x4, guide);
				put32(0x8, 0x32);
				put32(0xC, 0x32);
				put32(0x10, 0);
				tick::enqueue_operation(save_, op, sizeof(op));  // 0x54AA70
			}
		}
	}
	fn_50a3d0(convoy);
	p.u16(0x16) = uint16_t(p.u16(0x16) + sum / 1000);
	fn_515da0(save_, k);
}

// 0x5062F0: takes a free convoy (0x509AE0 makes 0x20 more when there is
// none), puts it at the head of `head` and makes `ship` its only ship and
// guide: owner, the ship's [+0xE], the towns [+0x39], [+0x37], [+0x38], the
// capacity [+0x10], [+0x18] |= 0x400, then 0x501680 with the mode 2; the wait
// [+0x16] = 0x100 and the state that of the ship.
uint16_t Sailing::fn_5062f0(uint16_t& head, uint16_t ship) {
	GlobalMemory& g = save_.globals;
	constexpr uint32_t kFreeConvoy = global::kShips + 0xE4;
	if (g.u16(kFreeConvoy) >= save_.convoy_count()) {
		tick::ship_ai_detail::fn_509ae0(save_);
	}
	const uint16_t index = g.u16(kFreeConvoy);
	GameMemory<0x3C>& c = save_.convoys[index];
	g.u16(kFreeConvoy) = c.u16(0x8);
	c.u16(0x8) = head;
	head = index;
	c.u16(0xA) = ship;
	ShipMemory& s = save_.ships[ship].memory;
	c.u8(0x0) = s.u8(0x0);
	c.u8(0x1) = s.u8(0xE);
	c.u16(0x1C) = s.u8(0x39);
	c.u8(0x1E) = s.u8(0x37);
	c.u8(0x1F) = s.u8(0x38);
	c.u32(0x4) = s.u32(0x10);
	c.u32(0x18) |= 0x400;
	s.u16(0x8) = index;
	tick::ship_ai_detail::fn_501680(save_, index, ship, 2);
	save_.convoys[index].u16(0x16) = 0x100;
	save_.convoys[index].u16(0x12) = save_.ships[ship].memory.u16(0x134);
	return index;
}

// 0x517170 (on the ship): puts it at the approach point of one of its towns
// ([+0x39], else [+0x37], else [+0x38], else town 0; the first one that is a
// town of the game), [+0x3D] |= 0x20.
void fn_517170(SaveGame& save, uint16_t ship) {
	ShipMemory& s = save.ships[ship].memory;
	const GlobalMemory& g = save.globals;
	const uint32_t towns = g.u32(kTownCount);
	uint32_t town = s.u8(0x39);
	if (town >= towns) {
		town = s.u8(0x37);
		if (town >= towns) {
			town = s.u8(0x38) < towns ? s.u8(0x38) : 0;
		}
	}
	s.s32(0x1C) = 0;
	s.s32(0x20) = 0;
	const uint32_t number = g.u8(kTownOrder + town);
	s.u16(0x1E) = g.u16(kTownTable + 0x34 * number + 0x20);
	s.u8(0x3D) |= 0x20;
	s.u16(0x22) = g.u16(kTownTable + 0x34 * number + 0x24);
}

// State 0x11 in port (0x506AF2): out of the list and of its convoy; a human
// player's ship stays (0x517170 puts it at one of its towns, with no target,
// hull or sailors); any other is deleted (0x507FB0).
void Sailing::lost_in_port(uint16_t index, uint16_t& link) {
	SavedShip& ship = save_.ships[index];
	ShipMemory& s = ship.memory;
	link = s.u16(0x6);
	s.u16(0x6) = 0xFFFF;
	s.u16(0x8) = 0xFFFF;
	if (human(ship)) {
		// 0x44F940: the sea map is told (UI).
		fn_517170(save_, index);
		s.u16(0x50) = 0xFFFF;
		s.u16(0x52) = 0xFFFF;
		s.s32(0x18) = 0;
		s.u16(0x40) = 0;
		return;
	}
	fn_507fb0(index);
}

// State 0x11 at sea (0x507022): out of the list and of the list by x, with no
// target; deleted (0x507FB0) unless it is a human player's.
void Sailing::lost_at_sea(uint16_t index, uint16_t& link) {
	SavedShip& ship = save_.ships[index];
	ShipMemory& s = ship.memory;
	link = s.u16(0x6);
	s.u16(0x6) = 0xFFFF;
	unlink_x(index);
	s.u16(0x50) = 0xFFFF;
	s.u16(0x52) = 0xFFFF;
	if (human(ship)) {
		return;
	}
	// 0x494990 and 0x4778A0 with 0x17: the UI is told.
	fn_507fb0(index);
}

// State 0x12 at sea, a pirate alone (0x507099): out of the list; with hull, a
// convoy of its own (0x5062F0), which the walk of the convoys moves; without,
// out of the list by x and deleted.
void Sailing::pirate_at_sea(uint16_t index, uint16_t& link) {
	ShipMemory& s = save_.ships[index].memory;
	link = s.u16(0x6);
	s.u16(0x6) = 0xFFFF;
	if (s.s32(0x18) > 0) {
		fn_5062f0(save_.globals.u16(global::kShips + 0xEC), index);
		return;
	}
	unlink_x(index);
	fn_507fb0(index);
}

// 0x507A65, the state 0x12 of a pirate convoy in 0x506720 (§7 of
// docs/cloud/battles.md): the guide back in the list by x if it is in none;
// the wait [+0x16] counts down into the guide's [+0x138]; a target (set by
// someone else with the flag 0x40 of [+0x14], or every 4 ticks 0x50D9E0 and
// 0x515360); then, without a route, a new course (0x505C90), and the convoy
// sails (0x502110 with 1) and is sorted by x, or at the end of its route
// attacks its town (a town battle, piece 6). Without the battles, the attack
// (0x50BC40) and the end of the route follow the rule of piece 2: as if there
// were no target, and as if it still sailed.
bool Sailing::pirate_convoy(uint16_t index, uint16_t& link) {
	const uint32_t count = save_.ships.size();
	GameMemory<0x3C>& c = save_.convoys[index];
	const uint16_t leader = c.u16(0x10);
	if (leader >= count) {
		// A convoy without a guide (its guide taken in a sea battle, or its
		// pirate gone): the game goes on with ship 0xFFFF, past the ships
		// array (whatever memory is there; zeros in the emulator), down to the
		// sort of 0x50D0C0 (see «Game bugs» in docs/cloud/battles-2.md). Not
		// reproduced: nothing is done with it.
		return false;
	}
	SavedShip& lead = save_.ships[leader];
	if (lead.memory.u16(0xA) == 0xFFFF && lead.memory.u16(0xC) == 0xFFFF) {
		insert_x(leader, 0xFFFF);
	}
	if (c.u16(0x16) > 0) {
		--c.u16(0x16);
		save_.ships[c.u16(0x10)].timer() = c.u16(0x16);
	}
	const uint16_t flags = c.u16(0x14);
	bool attack = false;
	if (flags & 0x40) {
		c.u16(0x14) = uint16_t(flags & 0xFFBF);
		// The target set by someone else (the attack of event 0x1E): a
		// merchant's ship that sails.
		const uint16_t t = save_.ships[c.u16(0x10)].memory.u16(0x50);
		if (t < count && save_.ships[t].owner() < save_.merchant_count) {
			const uint16_t st = save_.ships[t].status();
			attack = st == kStatusSailing || st == kStatusPursuing;
		}
	} else if ((save_.tick() & 3) == 0 && fn_50d9e0(leader)) {
		attack = fn_515360(leader);
	}
	if (attack && battles_) {
		// 0x507BAE: the target still sails (0x0F): the convoy leaves the list
		// and attacks (0x50BC40, a sea battle); the walk goes on from the link.
		const uint16_t t = save_.ships[save_.convoys[index].u16(0x10)].memory.u16(0x50);
		if (t < count && save_.ships[t].status() == kStatusSailing) {
			link = save_.convoys[index].u16(0x8);
			save_.convoys[index].u16(0x8) = 0xFFFF;
			fn_50bc40(index);
			return true;
		}
	}
	// Without battles (the rule of piece 2), as if there were no target.
	if (!save_.ships[c.u16(0x10)].route) {
		if (fn_505c90(index, link)) {
			return true;
		}
	}
	// 0x502110 false is the end of the route (0x507C0B): the convoy leaves the
	// list of those at sea; without a guide it is deleted (0x50A3D0); else its
	// guide leaves the list by x and the convoy attacks its town [+0x1C]
	// ([+0x1E] = [+0x1C]): a new town battle (0x507C4C: 0x611D80, 0x5FBFE0;
	// the battles' piece 6). The walk goes on from the link. Without the
	// battles, the rule of piece 2: the same as true.
	if (!fn_502110(index, 1) && battles_) {
		GameMemory<0x3C>& at = save_.convoys[index];
		link = at.u16(0x8);
		at.u16(0x8) = 0xFFFF;
		const uint16_t guide = at.u16(0x10);
		if (guide >= save_.ships.size()) {
			fn_50a3d0(index);
			return true;
		}
		unlink_x(guide);
		at.u8(0x1E) = at.u8(0x1C);
		town_attack(guide, at.u8(0x1C), false);
		return true;
	}
	if (save_.convoys[index].u16(0x10) >= save_.ships.size()) {
		// A convoy whose ships have no hull (its pirate went away, 0x514000)
		// has no guide: the game sorts ship 0xFFFF (0x50D0C0), outside the
		// ships (see «Game bugs» in docs/cloud/battles-2.md). Not reproduced.
		return false;
	}
	sort_x(save_.convoys[index].u16(0x10));
	return false;
}

}  // namespace p3::data
