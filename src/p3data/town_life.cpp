#include "p3data/town_life.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"

namespace p3::data {

namespace tick::events_towns_detail {
bool fn_4e2314(const SaveGame& save, uint32_t town, uint8_t& merchant, uint32_t& tick);  // town_tick/events_towns.cpp
}

namespace {


// The .let types the creatures read (docs/formats/town.md).
constexpr uint8_t kWater = 0x0E;
constexpr uint8_t kStreet = 0x10;  // srasse_vari_*
constexpr uint8_t kStreetEdge = 0x17;  // strasse_rand_*
constexpr uint8_t kPath = 0x18;  // feldweg_*
constexpr uint8_t kGrass = 0x28;  // GRASS_VARI_*
constexpr int kGate = 0x2F;  // the walls' gates
constexpr int kNode = 0x30;  // the editor's nodes and spawners
constexpr int kSpawner = 0x31;

bool street(uint8_t ground) { return ground == kStreet || ground == kStreetEdge || ground == kPath; }

bool read_u32(const uint8_t* data, size_t size, size_t& at, uint32_t& v) {
	if (at + 4 > size) {
		return false;
	}
	std::memcpy(&v, data + at, 4);
	at += 4;
	return true;
}

// The walkers' groups' classes (0x595CA0: the beggars, the poor, the middle class, the rich).
constexpr int kGroupClass[4] = {4, 0, 1, 2};

}  // namespace

bool parse_town_spawn(const uint8_t* data, size_t size, std::vector<std::vector<TownCell2>>& out) {
	out.clear();
	size_t at = 0;
	while (at < size) {
		uint32_t tag = 0;
		uint32_t count = 0;
		if (!read_u32(data, size, at, tag) || !read_u32(data, size, at, count) || tag > 16 || count > 4096) {
			return false;
		}
		if (out.size() <= tag) {
			out.resize(tag + 1);
		}
		for (uint32_t i = 0; i < count; ++i) {
			uint32_t x = 0;
			uint32_t y = 0;
			if (!read_u32(data, size, at, x) || !read_u32(data, size, at, y)) {
				return false;
			}
			out[tag].push_back({int(x), int(y)});
		}
	}
	return true;
}

bool parse_town_nodes(const uint8_t* data, size_t size, std::vector<std::vector<TownPath>>& out) {
	out.clear();
	size_t at = 0;
	while (at < size) {
		uint32_t tag = 0;
		uint32_t count = 0;
		if (!read_u32(data, size, at, tag) || !read_u32(data, size, at, count) || tag > 16 || count > 4096) {
			return false;
		}
		if (out.size() <= tag) {
			out.resize(tag + 1);
		}
		for (uint32_t i = 0; i < count; ++i) {
			uint32_t n = 0;
			if (!read_u32(data, size, at, n) || n > 4096) {
				return false;
			}
			TownPath path;
			for (uint32_t k = 0; k < n; ++k) {
				uint32_t x = 0;
				uint32_t y = 0;
				if (!read_u32(data, size, at, x) || !read_u32(data, size, at, y)) {
					return false;
				}
				path.push_back({int(int32_t(x)), int(int32_t(y))});
			}
			out[tag].push_back(std::move(path));
		}
	}
	return true;
}

TownCell2 life_cell_pixel(int x, int y) {
	return {x * kTileWidth + kTileWidth / 2 + (y & 1) * (kTileWidth / 2), y * kRowStep + 10};
}

TownCell2 life_pixel_cell(int x, int y) {
	TownCell2 best = {0, 0};
	double nearest = 1e9;
	const int row = y / kRowStep;
	for (int cy = row - 2; cy <= row + 2; ++cy) {
		if (cy < 0) {
			continue;
		}
		const int column = (x - (cy & 1) * (kTileWidth / 2)) / kTileWidth;
		for (int cx = column - 1; cx <= column + 1; ++cx) {
			if (cx < 0) {
				continue;
			}
			const TownCell2 c = life_cell_pixel(cx, cy);
			const double d = std::abs(x - c[0]) / 17.0 + std::abs(y - c[1] - 0.5) / 10.5;
			if (d < nearest) {
				nearest = d;
				best = {cx, cy};
			}
		}
	}
	return best;
}

bool life_neighbour(int& x, int& y, uint8_t direction, int width, int height) {
	switch (direction) {
	case 0x00: y -= 2; break;
	case 0x40: x += 1; break;
	case 0x80: y += 2; break;
	case 0xC0: x -= 1; break;
	case 0x20: x += y & 1; y -= 1; break;
	case 0x60: x += y & 1; y += 1; break;
	case 0xA0: y += 1; x -= y & 1; break;
	case 0xE0: y -= 1; x -= y & 1; break;
	default: break;
	}
	return x >= 0 && y >= 0 && x < width && y < height;
}

LifeMap life_map(const TownMap& town, const TileSet& tiles, const std::vector<uint8_t>& grid) {
	LifeMap map;
	map.width = town.width;
	map.height = town.height;
	const size_t cells = size_t(town.width) * size_t(town.height);
	map.ground.assign(cells, 0);
	map.object.assign(cells, -1);
	for (size_t i = 0; i < cells; ++i) {
		if (i < town.ground.size() && town.ground[i].tile) {
			const auto it = tiles.find(town.ground[i].tile);
			if (it != tiles.end()) {
				map.ground[i] = uint8_t(it->second.type);
			}
		}
		if (i < town.objects.size() && town.objects[i].tile) {
			const auto it = tiles.find(town.objects[i].tile);
			if (it != tiles.end() && !it->second.is_editor_marker()) {
				map.object[i] = int16_t(it->second.type);
			}
		}
	}
	map.grid = grid;
	return map;
}

LifeTown life_town(const SaveGame& save, size_t index) {
	LifeTown t;
	if (index >= save.towns.size()) {
		return t;
	}
	const SavedTown& town = save.towns[index];
	const TownMemory& m = town.memory;
	for (int g = 0; g < 4; ++g) {
		t.residents[size_t(g)] = m.u32(0x2E4 - 4 * uint32_t(g));
	}
	for (uint32_t k = 0; k < 8; ++k) {
		const uint8_t x = m.u8(0x7D0 + 2 * k);
		const uint8_t y = m.u8(0x7D1 + 2 * k);
		if (x != 0xFF && y != 0xFF) {
			t.stalls.push_back({int(x), int(y)});
		}
	}
	auto walk = [&](uint32_t head) {
		size_t walked = 0;
		for (uint32_t i = head; i < town.records.size() && walked < town.records.size(); i = town.records[i].next, ++walked) {
			const BuildingRecord& r = town.records[i];
			t.buildings.push_back({int(r.x), int(r.y), int(r.type)});
		}
	};
	walk(m.u16(0x764));
	const uint16_t offices = save.globals.u16(global::kWorldOfficeCount);
	size_t walked = 0;
	for (uint32_t i = m.u16(0x784); i < offices && i < save.offices.size() && walked < save.offices.size();
		 i = save.offices[i].memory.u16(0x2CA), ++walked) {
		walk(save.offices[i].memory.u16(0x2CE));
	}
	for (int g = 0; g < 4; ++g) {
		t.passed[size_t(g)] = m.u32(0x2F4 - 4 * uint32_t(g));
	}
	t.plague = (m.u32(0x2C8) & 8) != 0;
	int64_t mood = 0;
	for (uint32_t k = 0; k < 4; ++k) {
		mood += int64_t(int16_t(m.u16(0x300 + 2 * k))) * int64_t(m.u32(0x2D8 + 4 * k));
	}
	t.children = t.plague ? 0 : uint32_t((std::max<int64_t>(mood, 0) + 9999) / 10000);
	t.total = m.u32(0x2D4);
	t.fishing = m.u16(0x898);
	t.frozen = (m.u32(0x2C8) & 0x4000000) != 0;
	t.weather = m.u8(0x9C4);
	t.official = (m.u32(0x76C) & 2) != 0;
	// 0x677958: by the town's map (0x6DE4E0, the town's [0x2C2]).
	static constexpr uint8_t kFace[28] = {0x20, 0xE0, 0x60, 0x60, 0x60, 0x60, 0x60, 0xA0, 0x60, 0x60, 0xA0, 0xA0, 0x20,
		0x60, 0xE0, 0xA0, 0xA0, 0xA0, 0xA0, 0x60, 0xA0, 0x60, 0xE0, 0xA0, 0xA0, 0xA0, 0x20, 0xA0};
	const int map = town.template_map();
	t.official_face = map >= 0 && map < 28 ? kFace[map] : 0x20;
	t.index = int(index);
	const uint32_t towns = std::min<uint32_t>(save.globals.u32(global::kWorldTownCount), uint32_t(save.towns.size()));
	for (uint32_t i = 0; i < towns; ++i) {
		const uint32_t row = 0x6DDB90 + 0x34 * save.globals.u8(0x6DE4B8 + i);
		t.world.push_back({int(save.globals.s32(row)), int(save.globals.s32(row + 4))});
	}
	// The event queue (*0x6DD73C: its head [+0x8], its count [+0xC]), its convoys.
	const uint32_t count = save.globals.u16(0x6DD73C + 0xC);
	walked = 0;
	for (uint32_t i = save.globals.u16(0x6DD73C + 0x8); i < count && i < save.events.size() && walked < save.events.size();
		 i = save.events[i].u16(4), ++walked) {
		const auto& e = save.events[i];
		if (e.u16(6) != 0x0C || !(e.u8(0xB) & 1)) {
			continue;
		}
		LifeTown::Convoy c;
		c.event = uint16_t(i);
		c.to = e.u8(9);
		c.state = e.u8(0x15);
		c.counter = e.u8(0x16);
		c.here = (c.state == 1 && e.u8(8) == index) || ((c.state == 6 || c.state == 7) && e.u8(9) == index);
		if (c.here) {
			t.convoys.push_back(c);
		}
	}
	t.feeding_poor = (m.u32(0x2C8) & 0x800000) != 0;
	const uint32_t now = save.globals.u32(global::kWorldTick);
	uint8_t merchant = 0;
	uint32_t date = 0xFFFFFFFFu;
	if (!tick::events_towns_detail::fn_4e2314(save, uint32_t(index), merchant, date)) {
		date = 0xFFFFFFFFu;
	}
	t.feast = now - m.u32(0x2D0) < 0x300 || date - now < 0x300;
	return t;
}

size_t Creature::direction_index() const {
	const size_t n = look ? look->directions.size() : 1;
	return n ? ((size_t(dir) * n + 0x80) >> 8) & (n - 1) : 0;
}

const LcrFrame* Creature::current() const {
	if (!look || look->directions.empty()) {
		return nullptr;
	}
	const std::vector<LcrFrame>& frames = look->directions[direction_index()];
	return frame < frames.size() ? &frames[frame] : (frames.empty() ? nullptr : &frames[0]);
}

int TownLife::rand() {
	rand_state = rand_state * 214013u + 2531011u;
	return int((rand_state >> 16) & 0x7FFF);
}

// 0x568A70: a creature's number (never 0).
uint16_t TownLife::new_id() {
	const uint16_t id = next_id_++;
	if (next_id_ == 0) {
		next_id_ = 1;
	}
	return id;
}

namespace {

uint32_t frame_ms(const LcrAnim& a) {
	const uint32_t ms = a.fps ? 1000 / a.fps : 0;  // 0x558770: 1000 / fps, at least 1
	return ms ? ms : 1;
}

int sequence_index(const LcrAnim& a, int id) {
	for (size_t i = 0; i < a.sequences.size(); ++i) {
		if (a.sequences[i].id == (id & 0x1F)) {
			return int(i);
		}
	}
	return -1;
}

}  // namespace

// 0x557730: the sequence `request` (its id | its mode) started; its first frame (its last, played backwards), or 0xFF
// if the look has no such sequence (nothing changed).
uint8_t TownLife::anim_start(Creature& c, uint8_t request) {
	const int si = sequence_index(*c.look, request);
	if (si < 0) {
		return 0xFF;
	}
	c.seq = request;
	const uint32_t due = now_ + frame_ms(*c.look);
	if (c.frame_due > due) {
		c.frame_due = due;
	}
	const LcrSequence& q = c.look->sequences[size_t(si)];
	switch (request & 0xE0) {
	case 0x00:
	case 0xC0:
		c.next_seq = request;
		return uint8_t(q.first);
	case 0x40:
	case 0x80:
		c.next_seq = uint8_t(request + 0x20);
		return uint8_t(q.first);
	case 0x20:
	case 0xA0:
		c.next_seq = request;
		return uint8_t(q.last);
	default:
		c.next_seq = uint8_t(request - 0x20);
		return uint8_t(q.last);
	}
}

// Each class's animation's +0x8, at a sequence's end. The citizens' (0x559FE0): after a round of walking, one time
// in 32 a gesture once (3 or 4) and one in 32 the sequence 5; after anything else, walking again.
void TownLife::sequence_end(Creature& c) {
	if (c.cls == 6 || c.cls == 0x1A || c.cls == 0x1B) {
		return;  // The juggler, a rat (0x57B240), a fish (0x569A70): the same again.
	}
	if (c.cls == 0x0F) {
		// A bird (0x564410): gliding back (1 backwards once), flapping again.
		if (c.seq == 0xE1) {
			c.next_seq = 0;
		}
		return;
	}
	if (c.cls == 0x10 || c.cls == 0x11) {
		// An animal (0x57CE90 a dog's or a cat's, 0x57F520 a wild one's): the sequence after by what it is to do
		// ([+0x3A]): sitting down (5 once) to rest (3), getting up (7 once), starting to run (9 once), running (0xB),
		// stopping (0xA once), walking (2); each only if its look has it (0x5576C0). A wild one ends its walk so only
		// on grass (or off the map), and walking there now and then grazes (0xE once, then back) if it may rest
		// there; resting without getting up it sits on (3 there and back, 0x43 and 0x63, then 5 shown once).
		const bool wild = c.cls == 0x10;
		auto has = [&](uint8_t id) { return sequence_index(*c.look, id) >= 0; };
		auto to_run = [&] { c.next_seq = has(0x89) ? 0x89 : 0x0B; };
		auto to_rest = [&] { c.next_seq = has(0x85) ? 0x85 : 3; };
		switch (c.seq) {
		case 0x02:
			if (wild) {
				const TownCell2 cell = life_pixel_cell(c.px(), c.py());
				if (uint32_t(cell[0]) < uint32_t(map_.width) && uint32_t(cell[1]) < uint32_t(map_.height) &&
					map_.ground_at(cell[0], cell[1]) != kGrass) {
					break;
				}
			}
			if (c.act == 3) {
				to_rest();
			} else if (c.act == 0x0B) {
				to_run();
			} else if (wild && animal_has(c, 3) && (rand() & 7) == 0) {
				c.next_seq = 0x8E;
			}
			break;
		case 0x03:
			if (wild && has(0x87)) {
				if (c.act != 3) {
					c.next_seq = 0x87;
				}
			} else if (c.act == 2) {
				c.next_seq = has(0x87) ? 0x87 : 2;
			} else if (c.act == 0x0B) {
				if (has(0x87)) {
					c.next_seq = 0x87;
				} else {
					to_run();
				}
			}
			break;
		case 0x85: c.next_seq = !wild || has(0x87) ? 3 : 0x43; break;
		case 0x63:
			if (wild && c.act != 3) {
				c.next_seq = 0xC5;
			}
			break;
		case 0xC5:
		case 0x87:
			if (c.seq == 0xC5 && !wild) {
				break;
			}
			if (c.act == 2) {
				c.next_seq = 2;
			} else if (c.act == 3) {
				c.next_seq = 0x85;
			} else {
				to_run();
			}
			break;
		case 0x0B:
			if (c.act == 2) {
				c.next_seq = has(0x8A) ? 0x8A : 2;
			} else if (c.act == 3) {
				if (has(0x8A)) {
					c.next_seq = 0x8A;
				} else {
					to_rest();
				}
			}
			break;
		case 0x89: c.next_seq = 0x0B; break;
		case 0x8A:
		case 0xEE:
			if (c.seq == 0xEE && !wild) {
				break;
			}
			if (c.act == 2) {
				c.next_seq = 2;
			} else if (c.act == 3) {
				to_rest();
			}
			break;
		case 0x8E:
			if (wild) {
				c.next_seq = 0xEE;
			}
			break;
		default: break;
		}
		return;
	}
	if (c.cls == 0x18) {
		// The fisherman (0x56A0C0): after a round of walking forward past his path's second point, one time in 16 he
		// casts his net (12 once); then he fishes (13) 8 to 23 rounds, pulls it in (15), picks it up (18 once), turns
		// one way or the other (9 or 10 once) and goes back.
		switch (c.seq) {
		case 0x02:
			if (!c.back && c.point > 1 && (rand() & 0xF) == 0) {
				c.next_seq = 0x8C;
			}
			break;
		case 0x0D:
			if (--c.counter == 0) {
				c.next_seq = 0x0F;
			}
			break;
		case 0x0F: c.next_seq = 0x92; break;
		case 0x8C:
			c.counter = uint8_t(rand() % 16 + 8);
			c.next_seq = 0x0D;
			break;
		case 0x92:
			c.next_seq = rand() & 1 ? 0x8A : 0x89;
			c.back = true;
			--c.point;
			fisher_target(c);
			break;
		default: break;
		}
		return;
	}
	if (c.cls == 7) {
		// The official (0x563010): his scroll unrolled (3 once), he reads (4) 4 to 11 rounds, rolls it up (3
		// backwards) and walks back.
		if (c.seq == 4) {
			if (--c.reads == 0) {
				c.next_seq = 0xE3;
			}
		} else if (c.seq == 0x83) {
			c.reads = uint8_t(rand() % 8 + 4);
			c.next_seq = 4;
		} else if (c.seq == 0xE3) {
			c.next_seq = 2;
			c.back = true;
			--c.point;
			official_target(c);
		}
		return;
	}
	if (c.cls == 5) {
		// A market seller (0x5724B0): crying (5) or the gesture 3, a third each, or bending to his wares (14 once,
		// then 4, then 14 backwards).
		switch (c.seq) {
		case 0x05:
		case 0x83:
			switch (rand() % 3) {
			case 0: c.next_seq = 5; break;
			case 1: c.next_seq = 0x83; break;
			default: c.next_seq = 0x8E; break;
			}
			break;
		case 0xEE: c.next_seq = rand() & 1 ? 5 : 0x83; break;
		case 0x84: c.next_seq = rand() % 3 == 0 ? 0x84 : 0xEE; break;
		case 0x8E: c.next_seq = 0x84; break;
		default: break;
		}
		return;
	}
	if (c.mode == 0 && c.cls == 3) {
		// A child (0x56ECA0): after walking or running, a gesture (3 or 4), running, walking or the sequence 5, each
		// one time in 32; after anything else, walking or running.
		if (c.seq == 2 || c.seq == 6) {
			switch (rand() & 0x1F) {
			case 0: c.next_seq = 0x83; break;
			case 1: c.next_seq = 0x84; break;
			case 2: c.next_seq = 6; break;
			case 3: c.next_seq = 2; break;
			case 4: c.next_seq = 5; break;
			default: break;
			}
		} else {
			c.next_seq = rand() & 1 ? 2 : 6;
		}
		return;
	}
	if (c.mode == 0 && c.cls == 4) {
		// A beggar (0x5635C0): after walking, one time in 32 he stands (3 once) and one in 32 sits down (14 once); sitting he
		// begs (7, the gesture 8), and one time in 16 gets up (14 backwards); then walks on or stands.
		switch (c.seq) {
		case 0x02:
			switch (rand() & 0x1F) {
			case 0: c.next_seq = 0x83; break;
			case 1: c.next_seq = 0x8E; break;
			default: break;
			}
			break;
		case 0x07:
			switch (rand() & 0x0F) {
			case 0: c.next_seq = 0x88; break;
			case 1: c.next_seq = 0xEE; break;
			default: break;
			}
			break;
		case 0x88: c.next_seq = 7; break;
		case 0x83: c.next_seq = 2; break;
		case 0x8E: c.next_seq = rand() & 1 ? 7 : 0x88; break;
		case 0xEE: c.next_seq = rand() & 1 ? 2 : 0x83; break;
		default: break;
		}
		return;
	}
	if (c.mode != 0) {
		// On a road (0x55A03C): walking on, but rounding a corner; a gesture once if the way ahead is taken and it isn't
		// at the road's point yet (not hurrying). The stack's cells of this branch are read as best they could be.
		if (c.corner == 2 || c.seq != 2) {
			c.next_seq = 2;
			return;
		}
		const TownPath* p = road_path(c.mode, c.road);
		const TownCell2 here = life_pixel_cell(c.px(), c.py());
		const TownCell2 at = p && c.point < p->size() ? life_pixel_cell((*p)[c.point][0], (*p)[c.point][1]) : here;
		if (!can_go(c, c.dir, true, true) && here != at && !c.flee) {
			c.next_seq = 0x83;
		}
		return;
	}
	if (c.seq != 2) {
		c.next_seq = 2;
		return;
	}
	switch (rand() & 0x1F) {
	case 0: c.next_seq = 0x83; break;
	case 1: c.next_seq = 0x84; break;
	case 2: c.next_seq = 5; break;
	default: break;
	}
}

// 0x557340: the animation stepped (a frame each 1000 / fps ms, half that running); the frame now.
uint8_t TownLife::anim_step(Creature& c, uint8_t* counter) {
	const LcrAnim& a = *c.look;
	uint8_t f = c.frame;
	if (now_ < c.frame_due) {
		return f;
	}
	if (counter) {
		--*counter;
	}
	const size_t count = a.directions.empty() ? 0 : a.directions[c.direction_index()].size();
	const int si = sequence_index(a, c.seq);
	auto restart = [&] {
		sequence_end(c);
		f = anim_start(c, c.next_seq);
		if (f == 0xFF) {
			f = anim_start(c, c.seq);
		}
	};
	switch (c.seq & 0xE0) {
	case 0x00:
	case 0x40:
	case 0x80:
		++f;
		if (!(f < count && si >= 0 && f < a.sequences[size_t(si)].last + 1)) {
			restart();
		}
		break;
	case 0xA0:
	case 0xC0:
		restart();
		break;
	default:
		--f;
		if (!(f < count && si >= 0 && int(f) > a.sequences[size_t(si)].first - 1)) {
			restart();
		}
		break;
	}
	if (f >= count) {
		f = 0;
	}
	c.frame_due = now_ + (c.seq == 6 ? frame_ms(a) / 2 : frame_ms(a));
	return f;
}

// 0x593580: no object but the editor's marks on the cell, no other creature, and a street, a path or (unless
// `no_grass`) grass.
bool TownLife::cell_free(int x, int y, bool no_grass, uint16_t id) const {
	const int object = map_.object_at(x, y);
	if (object >= 0 && object != kNode && object != kSpawner) {
		return false;
	}
	for (const Creature& o : creatures_) {
		if (o.id == id) {
			continue;
		}
		for (const TownCell2& cell : o.cells) {
			if (cell[0] == x && cell[1] == y) {
				return false;
			}
		}
	}
	const uint8_t ground = map_.ground_at(x, y);
	if (no_grass && ground == kGrass) {
		return false;
	}
	return street(ground) || ground == kGrass;
}

// 0x55AD90: whether the creature may stand on the cell. Off the map, yes (the game's).
bool TownLife::cell_walkable(const Creature& c, int x, int y, bool land, bool creatures) const {
	if (!map_.in(x, y)) {
		return true;
	}
	const uint8_t ground = map_.ground_at(x, y);
	if (!land) {
		return ground == kWater;
	}
	const int object = map_.object_at(x, y);
	if (object >= 0) {
		if (object == kGate) {
			if (!c.cart_pass || !c.street_first || ground != kPath) {
				return false;
			}
		} else if (object != kNode && object != kSpawner) {
			return false;
		}
	}
	if (creatures) {
		for (const Creature& o : creatures_) {
			if (o.id == c.id) {
				continue;
			}
			for (const TownCell2& cell : o.cells) {
				if (cell[0] == x && cell[1] == y && (!c.cart_pass || o.cls != 0x1F)) {
					return false;
				}
			}
		}
	}
	const bool in_town = map_.inside(x, y);
	if (street(ground)) {
		return c.cart_pass || (in_town ? !c.outside : c.outside);
	}
	if (ground == kGrass) {
		return (in_town ? !c.outside : c.outside) && !c.on_street;
	}
	return false;
}

// 0x55A2C0 (a siege's army in the way isn't read yet): whether it may take a step in the direction: the diagonals
// the next cell, the others the two cells they cross (the nearer first). A creature with a radius (the animals: 5 or
// 10) is tested twice, from the points that far to its left and to its right (the game's isometric thirds and
// fifths, each rounded towards zero).
bool TownLife::can_go(Creature& c, uint8_t dir, bool land, bool creatures) const {
	int px = c.px();
	int py = c.py();
	const int r = c.radius;
	const int sector = ((dir + 0x10) & 0xFF) >> 5;  // N, NE, E, SE, S, SW, W, NW
	int passes = 1;
	if (r > 0) {
		passes = 2;
		switch (sector) {
		case 0: px -= r; break;
		case 1: px += -17 * r / 20; py += -11 * r / 20; break;
		case 2: py += -11 * r / 17; break;
		case 3: px += 17 * r / 20; py += -11 * r / 20; break;
		case 4: px += r; break;
		case 5: px += 17 * r / 20; py += 11 * r / 20; break;
		case 6: py += 11 * r / 17; break;
		default: px += -17 * r / 20; py += 11 * r / 20; break;
		}
	}
	for (;;) {
		const TownCell2 here = life_pixel_cell(px, py);
		if (c.street_first) {
			c.on_street = true;
			c.on_street = cell_walkable(c, here[0], here[1], land, false);
		} else {
			c.on_street = false;
		}
		const TownCell2 middle = life_cell_pixel(here[0], here[1]);
		int x = here[0];
		int y = here[1];
		auto test = [&](uint8_t d) {
			life_neighbour(x, y, d, map_.width, map_.height);
			return cell_walkable(c, x, y, land, creatures);
		};
		bool ok;
		switch (sector) {
		case 0: ok = px < middle[0] ? test(0xE0) && test(0x20) : test(0x20) && test(0xE0); break;
		case 1: ok = test(0x20); break;
		case 2: ok = py < middle[1] ? test(0x20) && test(0x60) : test(0x60) && test(0x20); break;
		case 3: ok = test(0x60); break;
		case 4: ok = px > middle[0] ? test(0x60) && test(0xA0) : test(0xA0) && test(0x60); break;
		case 5: ok = test(0xA0); break;
		case 6: ok = py > middle[1] ? test(0xA0) && test(0xE0) : test(0xE0) && test(0xA0); break;
		default: ok = test(0xE0); break;
		}
		if (!ok) {
			return false;
		}
		if (--passes == 0) {
			return true;
		}
		// Over to its right side.
		switch (sector) {
		case 0: px += 2 * r; break;
		case 1: px += 17 * r / 10; py += 11 * r / 10; break;
		case 2: py += 22 * r / 17; break;
		case 3: px += -17 * r / 10; py += 11 * r / 10; break;
		case 4: px -= 2 * r; break;
		case 5: px += -17 * r / 10; py += -11 * r / 10; break;
		case 6: py += -22 * r / 17; break;
		default: px += 17 * r / 10; py += -11 * r / 10; break;
		}
	}
}

// 0x55D700: the walker on a cell ahead that this one meets (none if the cell isn't one to walk, or two are there).
const Creature* TownLife::walker_at(const Creature& c, int x, int y) const {
	const uint8_t ground = map_.ground_at(x, y);
	if (ground == kGrass) {
		if (c.on_street) {
			return nullptr;
		}
	} else if (street(ground)) {
		if (!c.cart_pass && !map_.inside(x, y)) {
			return nullptr;
		}
	} else {
		return nullptr;
	}
	const int object = map_.object_at(x, y);
	if (object >= 0) {
		if (object == kGate) {
			if (!c.cart_pass || !c.street_first || ground != kPath) {
				return nullptr;
			}
		} else if (object != kNode && object != kSpawner) {
			return nullptr;
		}
	}
	const Creature* found = nullptr;
	for (const Creature& o : creatures_) {
		if (o.id == c.id) {
			continue;
		}
		bool there = false;
		for (const TownCell2& cell : o.cells) {
			there = there || (cell[0] == x && cell[1] == y);
		}
		if (!there) {
			continue;
		}
		if (o.cls > 8 || o.cls == 5 || o.cls == 6) {
			return nullptr;
		}
		if (found) {
			return nullptr;
		}
		found = &o;
	}
	return found;
}

// 0x5593C0: whether it is seen (its picture on the view).
bool TownLife::visible(const Creature& c) const {
	const LcrFrame* f = c.current();
	if (!f) {
		return false;
	}
	const int left = c.px() - (f->anchor_x - f->body.x);
	const int top = c.py() - (f->anchor_y - f->body.y);
	return left < view_[0] + view_[2] && left + f->body.w > view_[0] && top < view_[1] + view_[3] &&
		top + f->body.h > view_[1];
}

// 0x559790 / 0x5596C0: the cells its strips stand on (the middle of each strip's bottom).
void TownLife::place(Creature& c) {
	c.cells.clear();
	const LcrFrame* f = c.current();
	if (!f) {
		return;
	}
	for (const LcrRect& s : f->strips) {
		const TownCell2 cell =
			life_pixel_cell(c.px() + s.x - f->anchor_x + s.w / 2, c.py() + s.y - f->anchor_y + s.h);
		c.cells.push_back(cell);
	}
}

// 0x55D910: the diagonal towards its goal ([+0x3D]) and, returned, the other diagonal on the goal's main axis.
uint8_t TownLife::wish_dir(Creature& c) const {
	const int x = c.px();
	const int y = c.py();
	const int gx = c.goal_x;
	const int gy = c.goal_y;
	if (gx > x) {
		const int a = (gx - x) * 11;
		if (gy > y) {
			c.wish = 0x60;
			return a <= (gy - y) * 17 ? 0xA0 : 0x20;
		}
		c.wish = 0x20;
		return a <= (y - gy) * 17 ? 0xE0 : 0x60;
	}
	const int a = (x - gx) * 11;
	if (gy > y) {
		c.wish = 0xA0;
		return a <= (gy - y) * 17 ? 0x60 : 0xE0;
	}
	c.wish = 0xE0;
	return a <= (y - gy) * 17 ? 0x20 : 0xA0;
}

// 0x55D9D0: a new goal: a beggar fed by the church goes to it; otherwise one of the market's stalls or the town's
// buildings, on the other side of the old goal: the farthest of those near (within 850 · 550 pixels), or one of those
// farther at random; at a feast, the stalls only.
void TownLife::choose_goal(Creature& c) {
	const int x = c.px();
	const int y = c.py();
	const bool goal_left = c.goal_x < x;
	const bool goal_up = c.goal_y < y;
	uint16_t far = 0;
	uint16_t best = 0;
	if (c.cls == 4 && town_.feeding_poor) {
		for (const std::array<int, 3>& b : town_.buildings) {
			if (b[2] == 0x1F) {
				const TownCell2 p = life_cell_pixel(b[0], b[1] - 4);
				c.goal_x = uint16_t(p[0]);
				c.goal_y = uint16_t(p[1]);
				c.countdown = 3;
				c.left_turns = 0;
				c.right_turns = 0;
				return;
			}
		}
	}
	auto consider = [&](const TownCell2& p, bool filter) {
		if (filter && (c.goal_x != x || c.goal_y != y)) {
			if ((p[0] < x) == goal_left || (p[1] < y) == goal_up) {
				return;
			}
		}
		const int dx = std::abs(p[0] - x);
		const int dy = std::abs(p[1] - y);
		if (dx > 0x352 || dy > 0x226) {
			++far;
			if (rand() % far == 0) {
				c.goal_x = uint16_t(p[0]);
				c.goal_y = uint16_t(p[1]);
			}
			return;
		}
		if (far != 0) {
			return;
		}
		int score = (dy * 17) / 11;
		if ((score & 0xFFFF) < dx) {
			score = dx;
		}
		if (uint16_t(score) > best) {
			best = uint16_t(score);
			c.goal_x = uint16_t(p[0]);
			c.goal_y = uint16_t(p[1]);
		}
	};
	for (const TownCell2& s : town_.stalls) {
		consider(life_cell_pixel(s[0], s[1]), !town_.feast);
	}
	if (town_.feast) {
		return;
	}
	for (const std::array<int, 3>& b : town_.buildings) {
		if (b[2] >= 0x2F || !map_.inside(b[0], b[1] - 2)) {
			continue;
		}
		consider(life_cell_pixel(b[0], b[1] - 2), true);
	}
	if (far == 0 && best == 0) {
		c.countdown = 3;
		c.left_turns = 0;
		c.right_turns = 0;
	}
}

// 0x55CF60: a step to the cell's middle (a little aside when another walker is there), its length by the look's
// speed: dist · 10000 / speed milliseconds.
void TownLife::begin_step(Creature& c, int cx, int cy) {
	TownCell2 t = life_cell_pixel(cx, cy);
	if (walker_at(c, cx, cy)) {
		t[1] += t[0] >= c.px() ? 3 : -3;
		t[0] += t[1] >= c.py() ? -4 : 4;
		c.sidestep = true;
	} else {
		c.sidestep = false;
	}
	const int dx = t[0] - c.px();
	const int dy = t[1] - c.py();
	const double dist = std::sqrt(double(dx * dx + dy * dy));
	const double speed = double(c.look && c.look->speed ? c.look->speed : 140);
	const int n = int(dist * 10000.0 / speed + 0.5);
	c.done = 0;
	c.total = uint16_t(n);
	c.step_x = dx;
	c.step_y = dy;
	if (n) {
		c.step_x = int32_t((int64_t(dx) << 16) / n);
		c.step_y = int32_t((int64_t(dy) << 16) / n);
	}
}

// 0x55C820: a free walker's frame. False when it is to go (out of the town, or stuck and unseen).
bool TownLife::walk(Creature& c) {
	const uint8_t f = anim_step(c);
	if (f != c.frame) {
		walker_footprints(c);  // 0x55C8D9: the frame shown before it changes.
	}
	if (c.dir != c.target) {
		if (f != c.frame) {
			const int8_t d = int8_t(uint8_t(c.target - c.dir));
			if (d < 0) {
				c.dir = uint8_t(c.dir - 0x20);
				c.right_turns = 0;
				++c.left_turns;
			} else {
				c.dir = uint8_t(c.dir + 0x20);
				c.left_turns = 0;
				++c.right_turns;
			}
			if (c.next_seq != 2 && c.next_seq != 6) {
				c.next_seq = 2;
			}
			uint8_t nf = 0;
			const int walk = sequence_index(*c.look, 2);
			const int run = sequence_index(*c.look, 6);
			if (c.seq == 2 && walk >= 0 && int(f) <= c.look->sequences[size_t(walk)].last) {
				nf = f;
			} else if (c.seq == 6 && run >= 0 && int(f) <= c.look->sequences[size_t(run)].last) {
				nf = f;
			} else {
				nf = anim_start(c, 2);
			}
			c.frame = nf;
			place(c);
		}
		c.countdown = 3;
		if (c.left_turns > 3 || c.right_turns > 3) {
			return visible(c);
		}
		return true;
	}
	const TownCell2 cell = life_pixel_cell(c.px(), c.py());
	if (!map_.inside(cell[0], cell[1])) {
		return false;
	}
	if (c.done == c.total) {
		if (c.left_turns > 2 || c.right_turns > 2) {
			choose_goal(c);
		}
		const uint8_t sec = wish_dir(c);
		const bool straight = c.wish == c.dir;
		int ax = cell[0];
		int ay = cell[1];
		life_neighbour(ax, ay, c.dir, map_.width, map_.height);
		const Creature* other = walker_at(c, ax, ay);
		if (c.countdown) {
			--c.countdown;
		}
		auto go = [&](uint8_t to) {
			int gx = cell[0];
			int gy = cell[1];
			c.target = to;
			life_neighbour(gx, gy, to, map_.width, map_.height);
			begin_step(c, gx, gy);
			c.next_seq = 2;
			return true;
		};
		if (straight) {
			if ((other && other->dir == uint8_t(c.dir + 0x80)) || can_go(c, c.dir, true, true)) {
				c.target = c.dir;
				begin_step(c, ax, ay);
				return true;
			}
			if (other && (other->seq == 2 || other->seq == 6)) {
				uint8_t nf = f;
				if (c.seq != 0x83) {
					nf = anim_start(c, 0x83);
					if (nf == 0xFF) {
						nf = c.frame;
					}
				}
				c.target = c.dir;
				begin_step(c, ax, ay);
				c.frame = nf;
				place(c);
				return true;
			}
		}
		const uint8_t old = c.dir;
		const uint8_t opposite_sec = uint8_t(sec - 0x80);
		for (int round = 0; round < 2; ++round) {
			if (round == 1) {
				c.street_first = false;
			}
			if (uint8_t(c.wish - 0x80) != c.dir && can_go(c, c.wish, true, true)) {
				const uint8_t to = c.wish;
				c.wish = old;
				c.street_first = true;
				return go(to);
			}
			if (opposite_sec != c.dir && can_go(c, sec, true, true)) {
				c.wish = old;
				c.street_first = true;
				return go(sec);
			}
			if (sec != c.dir) {
				const uint8_t back = uint8_t(sec + 0x80);
				if (can_go(c, back, true, true)) {
					c.wish = old;
					c.street_first = true;
					return go(back);
				}
			}
		}
		c.street_first = true;
		choose_goal(c);
		wish_dir(c);
		uint8_t to = c.wish;
		bool found = can_go(c, to, true, true);
		if (!found) {
			to = uint8_t(c.wish + ((rand() & 1) << 7) - 0x40);
			found = can_go(c, to, true, true);
		}
		if (!found) {
			to = uint8_t(to + 0x80);
			found = can_go(c, to, true, true);
		}
		if (!found) {
			if (c.stuck != -1) {
				c.stuck = int32_t(now_);
			}
			to = uint8_t(c.wish + 0x80);
			found = can_go(c, to, true, true);
		}
		if (!found) {
			c.target = c.dir;
			c.stuck = -1;
			return visible(c);
		}
		c.done = 0;
		c.total = 0;
		return go(to);
	}
	// Walking its step (0x55CE2A): the milliseconds of this frame (twice running), while walking or running.
	int32_t nx = c.x;
	int32_t ny = c.y;
	uint32_t d = elapsed_;
	if (c.seq == 6) {
		d *= 2;
	}
	if (c.seq == 2 || c.seq == 6) {
		const uint32_t rest = uint32_t(c.total - c.done);
		if (d > rest) {
			d = rest;
		}
		c.done = uint16_t(c.done + d);
		nx += c.step_x * int32_t(d);
		ny += c.step_y * int32_t(d);
	}
	bool moved = false;
	if ((nx >> 16) != c.px() || (ny >> 16) != c.py()) {
		moved = true;
		const TownCell2 next = life_pixel_cell(int(nx >> 16), int(ny >> 16));
		if (uint32_t(c.done) * 3 > uint32_t(c.total) * 2 && !c.sidestep &&
			!cell_free(next[0], next[1], c.cls == 0xFF, c.id)) {
			begin_step(c, next[0], next[1]);
			if (c.sidestep) {
				return true;
			}
		}
	}
	c.x = nx;
	c.y = ny;
	if (f != c.frame || moved) {
		c.frame = f;
		place(c);
	}
	return true;
}

// 0x59A4F0: a cell to start on: 200 at random over the map, then the town's points of its kind (the spawn's tag 1
// inside the town, 0 outside) from one at random; on its side of the walls, free (cell_free) and, with
// `visible_check`, out of the view (50 pixels around it).
bool TownLife::spawn_cell(bool inside, bool visible_check, bool no_grass, TownCell2& out) {
	const size_t tag = inside ? 1 : 0;
	static const std::vector<TownCell2> kNone;
	const std::vector<TownCell2>& points = tag < town_.spawn.size() ? town_.spawn[tag] : kNone;
	const int n = int(points.size());
	if (n == 0 || map_.width <= 0 || map_.height <= 0) {
		return false;
	}
	const int start = rand() % n;
	for (int k = -200; k < n; ++k) {
		TownCell2 cell;
		if (k < 0) {
			cell[0] = rand() % map_.width;
			cell[1] = rand() % map_.height;
		} else {
			cell = points[size_t((k + start) % n)];
		}
		if (map_.inside(cell[0], cell[1]) != inside) {
			continue;
		}
		if (!cell_free(cell[0], cell[1], no_grass, 0)) {
			continue;
		}
		if (visible_check) {
			const TownCell2 p = life_cell_pixel(cell[0], cell[1]);
			if (p[0] + 50 >= view_[0] && p[0] <= view_[0] + view_[2] + 50 && p[1] + 50 >= view_[1] &&
				p[1] <= view_[1] + view_[3] + 50) {
				continue;
			}
		}
		out = cell;
		return true;
	}
	return false;
}

namespace {

// 0x55C170: a vector's direction of the eight (the table 0x677928).
uint8_t vec_dir(int dx, int dy) {
	const uint32_t ax = uint32_t(std::abs(dx));
	const uint32_t ay = uint32_t(std::abs(dy));
	if (ax * 4 > ay * 15) {
		return dx < 0 ? 0xC0 : 0x40;
	}
	if (ax * 11 < ay * 7) {
		return dy < 0 ? 0x00 : 0x80;
	}
	if (dx < 0) {
		return dy < 0 ? 0xE0 : 0xA0;
	}
	return dy < 0 ? 0x20 : 0x60;
}

int32_t hi_signed(int32_t a, int32_t b) { return int32_t((int64_t(a) * int64_t(b)) >> 32); }
uint32_t hi_unsigned(uint32_t a, uint32_t b) { return uint32_t((uint64_t(a) * uint64_t(b)) >> 32); }
int32_t toward_zero(int32_t v) { return v + int32_t(uint32_t(v) >> 31); }
constexpr uint32_t kY = 0xC56106EBu;  // 0.771 (·2^32) as the game multiplies a y step
constexpr uint32_t kXPos = 0xD1B71759u;  // 0.8192
constexpr int32_t kXNeg = 0x68DB8BAD;  // 0.4096
int32_t y_neg(int32_t e) { return toward_zero((hi_signed(e, int32_t(kY)) + e) >> 13); }
int32_t y_pos(uint32_t e) { return int32_t(hi_unsigned(e, kY) >> 13); }
int32_t x_pos(uint32_t e) { return int32_t(hi_unsigned(e, kXPos) >> 9); }
int32_t x_neg(int32_t e) { return toward_zero(hi_signed(e, kXNeg) >> 8); }

// 0x55A1C0: a turn by one of the look's directions towards `target`, the shorter way.
void turn(uint8_t& dir, uint8_t target, size_t directions) {
	const uint8_t c = dir;
	const uint8_t a = target;
	const uint8_t cw = uint8_t(a - c);
	const uint8_t ccw = uint8_t(c - a);
	const uint8_t step = uint8_t(256 / (directions ? directions : 8));
	if (ccw > cw) {
		dir = c == 0xE0 ? 0x00 : uint8_t(c + step);
	} else {
		dir = c == 0x00 ? 0xE0 : uint8_t(c - step);
	}
}

// The town's size in pixels as the view keeps it ([+0xC31C] the cells' width · the columns, [+0xC320] (the rows' step
// + 1) · half the rows).
uint32_t map_pixels_x(const LifeMap& m) { return uint32_t(m.width * kTileWidth); }
uint32_t map_pixels_y(const LifeMap& m) { return uint32_t(2 * kRowStep * (m.height / 2)); }

// A rat's or an animal's run straight on (0x573900, 0x57C240, 0x57E840): `v` the 16.16 pixels across, up and down
// 11/17 of it, the diagonals 181/256 of it each way; none off the eight directions.
void straight(uint8_t dir, int32_t v, int32_t& nx, int32_t& ny) {
	constexpr int32_t kIso = 0x78787879;  // 0.47 (·2^32): 11/17 with the · 2 and >> 4
	auto iso = [](int32_t e) { return toward_zero(hi_signed(e, kIso) >> 4); };
	const int32_t a = (v * 181) >> 8;
	switch (dir) {
	case 0x00: ny += iso(-22 * v); break;
	case 0x80: ny += iso(22 * v); break;
	case 0x40: nx += v; break;
	case 0xC0: nx -= v; break;
	case 0x20: nx += a; ny += iso(-22 * a); break;
	case 0x60: nx += a; ny += iso(22 * a); break;
	case 0xA0: nx -= a; ny += iso(22 * a); break;
	case 0xE0: nx -= a; ny += iso(-22 * a); break;
	default: break;
	}
}

}  // namespace

// 0x55C200: the 16.16 pixels a millisecond of a walk in one of the eight directions at the look's speed (14 pixels a
// second across, 9 up and down, the diagonals 11 · 7 for the town's people).
void TownLife::set_steps(Creature& c, uint8_t dir) const {
	const int32_t s = int32_t(c.look ? c.look->speed : 140);
	switch (dir) {
	case 0x00: c.step_x = 0; c.step_y = y_neg(-(s * 11) << 12); break;
	case 0x20: c.step_y = y_neg(s * -0x7C73); c.step_x = x_pos(uint32_t(s * 181) << 4); break;
	case 0x40: c.step_y = 0; c.step_x = x_pos(uint32_t(s) << 12); break;
	case 0x60: c.step_y = y_pos(uint32_t(s * 0x7C73)); c.step_x = x_pos(uint32_t(s * 181) << 4); break;
	case 0x80: c.step_y = y_pos(uint32_t(s * 0xB000)); c.step_x = 0; break;
	case 0xA0: c.step_y = y_pos(uint32_t(s * 0x7C73)); c.step_x = x_neg(-(s * 181) << 4); break;
	case 0xC0: c.step_y = 0; c.step_x = x_neg(-s << 12); break;
	case 0xE0: c.step_y = y_neg(s * -0x7C73); c.step_x = x_neg(-(s * 181) << 4); break;
	default: break;
	}
}

bool TownLife::roads() const {
	return town_.nodes.size() > 2 && !town_.nodes[1].empty() && !town_.nodes[2].empty();
}

const TownPath* TownLife::road_path(int tag, int index) const {
	if (size_t(tag) >= town_.nodes.size() || size_t(index) >= town_.nodes[size_t(tag)].size()) {
		return nullptr;
	}
	const TownPath& p = town_.nodes[size_t(tag)][size_t(index)];
	return p.size() >= 2 ? &p : nullptr;
}

int TownLife::count(int group, uint8_t mode) const {
	int n = 0;
	for (const Creature& c : creatures_) {
		n += c.group == group && c.mode == mode;
	}
	return n;
}

// 0x55B1D0: a walker set on a road. Coming in (`arriving`, the tag 1 road): at its first point, towards its second.
// Leaving (tag 2): made so (its x the mark 0x1388), at the road's first point out of the town unless it is seen,
// facing the point before; or a walker in the town near one of the road's points (170 · 110 pixels, the farthest out
// first) that lies on a diagonal from the middle of its cell, by at most 10 free cells, goes to it.
bool TownLife::start_road(Creature& c, int road, bool arriving) {
	if (!roads()) {
		return false;
	}
	c.entered = false;
	c.lane_x = 0;
	c.lane_y = 0;
	c.corner = 0xFF;
	if (arriving) {
		const TownPath* p = road_path(1, road);
		if (!p) {
			return false;
		}
		c.x = (*p)[0][0] << 16;
		c.y = (*p)[0][1] << 16;
		c.dir = vec_dir((*p)[1][0] - (*p)[0][0], (*p)[1][1] - (*p)[0][1]);
		set_steps(c, c.dir);
		c.road = uint8_t(road);
		c.point = 1;
		c.mode = 1;
		c.cart_pass = true;
		c.flee = false;
		return true;
	}
	const TownPath* p = road_path(2, road);
	if (!p) {
		return false;
	}
	const int n = int(p->size());
	if (c.x == 0x1388) {
		int k = 0;
		TownCell2 q = (*p)[1];
		for (; k < n - 1; ++k) {
			q = (*p)[size_t(k) + 1];
			const TownCell2 cell = life_pixel_cell(q[0], q[1]);
			if (!map_.inside(cell[0], cell[1])) {
				break;
			}
		}
		if (q[0] + 50 > view_[0] && q[0] < view_[0] + view_[2] + 50 && q[1] + 50 > view_[1] &&
			q[1] < view_[1] + view_[3] + 50) {
			return false;
		}
		const TownCell2 back = (*p)[size_t(std::min(k, n - 1))];
		c.x = q[0] << 16;
		c.y = q[1] << 16;
		c.dir = vec_dir(back[0] - q[0], back[1] - q[1]);
		set_steps(c, c.dir);
		c.point = uint8_t(k);
		c.road = uint8_t(road);
		c.mode = 2;
		c.cart_pass = true;
		c.flee = false;
		return true;
	}
	for (int k = n - 2; k >= 0; --k) {
		const TownCell2 at = (*p)[size_t(k) + 1];
		if (at[0] + 0xAA < c.px() || at[0] - 0xAA > c.px() || at[1] + 0x6E < c.py() || at[1] - 0x6E > c.py()) {
			continue;
		}
		const TownCell2 cell_p = life_pixel_cell(at[0], at[1]);
		const TownCell2 cell_w = life_pixel_cell(c.px(), c.py());
		if (cell_w == cell_p) {
			if (k + 2 >= n) {
				continue;  // the game reads past the road's end here
			}
			const TownCell2 q = (*p)[size_t(k) + 2];
			c.target = vec_dir(q[0] - at[0], q[1] - at[1]);
			set_steps(c, c.target);
			c.point = uint8_t(k + 2);
			c.road = uint8_t(road);
			c.mode = 2;
			c.entered = false;
			c.x = at[0] << 16;
			c.y = at[1] << 16;
			place(c);
			c.cart_pass = true;
			c.flee = false;
			c.corner = 0;
			return true;
		}
		const TownCell2 middle = life_cell_pixel(cell_w[0], cell_w[1]);
		const int dx = at[0] - middle[0];
		const int dy = at[1] - middle[1];
		if (std::abs(dx) * 11 != std::abs(dy) * 17) {
			continue;
		}
		const uint8_t dir = vec_dir(dx, dy);
		TownCell2 cur = cell_w;
		bool found = false;
		for (int i = 0; i <= 10; ++i) {
			if (cur == cell_p) {
				found = true;
				break;
			}
			life_neighbour(cur[0], cur[1], dir, map_.width, map_.height);
			if (!cell_walkable(c, cur[0], cur[1], true, false)) {
				i = 10;
			}
		}
		if (!found) {
			continue;
		}
		c.target = dir;
		set_steps(c, dir);
		c.point = uint8_t(k + 1);
		c.road = uint8_t(road);
		c.mode = 2;
		c.entered = false;
		c.x = middle[0] << 16;
		c.y = middle[1] << 16;
		place(c);
		c.cart_pass = true;
		c.flee = false;
		return true;
	}
	return false;
}

// 0x55B6A0 (a walker on a road): it turns a direction a frame to its road's way, then walks it straight at its look's
// speed (set_steps) to the road's point (round a right-angled corner by its two cells' middles), there takes the
// next; coming in, at the first point after it reached the town it walks free (the mode 0); leaving, at the road's end
// it goes. The carts on its road (0x570BA0, the list *0x6E6280) would put it aside: none yet.
bool TownLife::road_walk(Creature& c) {
	if (c.mode == 2 && c.entered) {
		return false;
	}
	if (!roads()) {
		return false;
	}
	const TownPath* p = road_path(c.mode, c.road);
	if (!p) {
		return false;
	}
	const int n = int(p->size());
	if (c.corner == 0xFF) {
		c.corner = 0;
		if (c.point + 1 < n) {
			const TownCell2 a = (*p)[c.point];
			const TownCell2 b = (*p)[size_t(c.point) + 1];
			if (uint8_t(vec_dir(b[0] - a[0], b[1] - a[1]) + 0x40) == c.target) {
				TownCell2 cell = life_pixel_cell(a[0], a[1]);
				life_neighbour(cell[0], cell[1], uint8_t(c.target + 0x80), map_.width, map_.height);
				c.corner_a = life_cell_pixel(cell[0], cell[1]);
				cell = life_pixel_cell(a[0], a[1]);
				life_neighbour(cell[0], cell[1], uint8_t(c.target - 0x40), map_.width, map_.height);
				c.corner_b = life_cell_pixel(cell[0], cell[1]);
				c.corner = 1;
			}
		}
	}
	const uint32_t dt = elapsed_;
	const uint8_t f = anim_step(c);
	if (c.dir != c.target) {
		if (f != c.frame) {
			turn(c.dir, c.target, c.look ? c.look->directions.size() : 8);
			c.next_seq = 2;
			const uint8_t nf = c.seq == 2 ? f : anim_start(c, 2);
			c.x -= c.lane_x << 16;
			c.y -= c.lane_y << 16;
			c.lane_x = 0;
			c.lane_y = 0;
			c.frame = nf;
			place(c);
		}
		return true;
	}
	int32_t nx = c.x - (c.lane_x << 16);
	int32_t ny = c.y - (c.lane_y << 16);
	if (c.seq == 2) {
		const int32_t m = c.flee ? 3 : 1;
		nx += c.step_x * int32_t(dt) * m;
		ny += c.step_y * int32_t(dt) * m;
	}
	c.lane_x = 0;
	c.lane_y = 0;
	// 0x570BA0: within 68 pixels of a cart on its road it walks aside, to its right.
	const uint16_t near = cart_distance(int(nx >> 16), int(ny >> 16), c.road);
	if (near < 0x44) {
		const int a = near > 0x11 ? (0x44 - near) / 6 : 8;
		const int side = toward_zero(hi_signed(11 * a, int32_t(0x78787879)) >> 3);
		switch (c.target) {
		case 0x00: c.lane_x = a; break;
		case 0x20: c.lane_x = a; c.lane_y = side; break;
		case 0x40: c.lane_y = side; break;
		case 0x60: c.lane_x = -a; c.lane_y = side; break;
		case 0x80: c.lane_x = -a; break;
		case 0xA0: c.lane_x = -a; c.lane_y = -side; break;
		case 0xC0: c.lane_y = -side; break;
		case 0xE0: c.lane_x = a; c.lane_y = -side; break;
		default: break;
		}
		nx += c.lane_x << 16;
		ny += c.lane_y << 16;
	}
	bool moved = false;
	if ((nx >> 16) != c.px() || (ny >> 16) != c.py()) {
		moved = true;
		const TownCell2 old_cell = life_pixel_cell(c.px(), c.py());
		const TownCell2 new_cell = life_pixel_cell(int(nx >> 16), int(ny >> 16));
		const TownCell2 at = (*p)[c.point];
		TownCell2 aim = at;
		if (c.corner == 1) {
			aim = c.corner_a;
		} else if (c.corner == 2) {
			aim = c.corner_b;
		}
		if (new_cell != old_cell && c.mode == 1 && map_.inside(new_cell[0], new_cell[1])) {
			c.entered = true;
		}
		const int x = int(nx >> 16);
		const int y = int(ny >> 16);
		const bool passed = (c.step_x > 0 && x >= aim[0]) || (c.step_x < 0 && x <= aim[0]) ||
			(c.step_y > 0 && y >= aim[1]) || (c.step_y < 0 && y <= aim[1]);
		if (passed) {
			if (c.point + 1 < n && !c.entered) {
				uint8_t dir = 0;
				if (c.corner == 1) {
					dir = vec_dir(c.corner_b[0] - aim[0], c.corner_b[1] - aim[1]);
					c.corner = 2;
				} else {
					++c.point;
					const TownCell2 q = (*p)[c.point];
					dir = c.corner == 2 ? uint8_t(c.target - 0x20) : vec_dir(q[0] - aim[0], q[1] - aim[1]);
					c.corner = 0xFF;
				}
				c.target = dir;
				set_steps(c, dir);
				nx = (c.lane_x + aim[0]) << 16;
				ny = (c.lane_y + aim[1]) << 16;
			} else {
				if (c.mode != 1) {
					return false;
				}
				nx = at[0] << 16;
				ny = at[1] << 16;
				c.mode = 0;
				c.cart_pass = false;
			}
		}
	}
	if (f != c.frame || moved) {
		c.x = nx;
		c.y = ny;
		c.frame = f;
		place(c);
	} else {
		c.x = nx;
		c.y = ny;
	}
	return true;
}

// The class's factory and +0x40 (0x557D10; 0x567C80 → 0x559D60 → 0x55C7A0): a walker of the group, with a look of
// its class at random; by `mode_word` (its mode << 16 | a road) free at (x, y) (16.16) facing `dir`, coming in by the
// road, or leaving (the next two roads tried if not). False if it can't be (no look; leaving by no road: the game's
// is made and goes at once).
Creature TownLife::make_walker(int group, int32_t x, int32_t y, uint8_t dir, uint32_t mode_word) {
	Creature c;
	c.cls = kGroupClass[group];
	c.group = group;
	const std::vector<const LcrAnim*>& looks = people[size_t(c.cls)];
	if (looks.empty()) {
		c.look = nullptr;
		return c;
	}
	c.look = looks[size_t(rand()) % looks.size()];
	c.id = next_id_++;
	if (next_id_ == 0) {
		next_id_ = 1;
	}
	c.street_first = true;  // 0x55A290: 0, 1, 0, 0
	anim_start(c, 2);
	const uint32_t mode = mode_word >> 16;
	const int road = int(mode_word & 0xFF);
	if (mode == 2) {
		c.x = 0x1388;
		if (!start_road(c, road, false) && !start_road(c, (road + 1) % 3, false) &&
			!start_road(c, (road + 2) % 3, false)) {
			c.look = nullptr;
			return c;
		}
	} else if (mode == 1) {
		if (!start_road(c, road, true)) {
			c.look = nullptr;
			return c;
		}
	} else {
		c.x = x;
		c.y = y;
		c.dir = dir;
	}
	// 0x55C7A0: its direction, its goal from where it is, its strips.
	c.target = c.wish = c.dir;
	c.goal_x = uint16_t(c.px());
	c.goal_y = uint16_t(c.py());
	choose_goal(c);
	place(c);
	return c;
}

void TownLife::start(const LifeMap& map, const LifeTown& town, uint32_t now) {
	map_ = map;
	town_ = town;
	creatures_.clear();
	now_ = now;
	floes_made_ = false;  // (0x58B5D7: the town left, [view+0xC63C] cleared.)
	precip_count_ = 0;
	last_view_set_ = false;
	// 0x59B410, then 0x58F4E0 at once (0x58A20D, 0x58A68B): the weather's tint as it is, its fog banks made.
	fog_banks_.fill(0);
	reset_weather();
	update_weather(true);
	// 0x58EF26: every path of the tag 4 free for a fisherman.
	fisher_free_.assign(town.nodes.size() > 4 ? town.nodes[4].size() : 0, 1);
	// 0x58A8A0 (the view's animations on): 0x58A920, 20 birds anywhere over the map, facing one of the eight
	// directions (kept by their height: drawn so); 0x58B1B0, the town's dogs and cats, then the wild animals.
	for (int i = 0; i < 20; ++i) {
		const uint8_t dir = uint8_t((rand() % 8) * 32);
		const uint32_t h = map_pixels_y(map_);
		const uint32_t w = map_pixels_x(map_);
		const int32_t y = int32_t(h ? uint32_t(rand()) % h : 0);
		const int32_t x = int32_t(w ? uint32_t(rand()) % w : 0);
		spawn_bird(x << 16, y << 16, dir, nullptr, 0, 0);
	}
	if (town.spawn.size() >= 3 && !town.spawn[0].empty() && !town.spawn[1].empty() && !town.spawn[2].empty()) {
		for (int i = 0; i < 10; ++i) {
			spawn_animal(0x11, false, nullptr, 0);
		}
		for (int i = 0; i < 20; ++i) {
			spawn_animal(0x10, false, nullptr, 0);
		}
	}
	place_performers();
	// 0x595CA0: for each group one walker for every 50 of its residents, each on a town's point, facing a
	// diagonal, with a look of its class at random (the game's option of the animations on).
	for (int g = 0; g < 4; ++g) {
		const uint32_t count = (town.residents[size_t(g)] + 49) / 50;
		for (uint32_t i = 0; i < count; ++i) {
			TownCell2 cell;
			if (!spawn_cell(true, false, true, cell)) {
				continue;
			}
			const TownCell2 p = life_cell_pixel(cell[0], cell[1]);
			const uint8_t dir = uint8_t((rand() % 4) * 64 + 0x20);
			Creature c = make_walker(g, p[0] << 16, p[1] << 16, dir, 0);
			if (c.look) {
				creatures_.push_back(std::move(c));
			}
		}
	}
	// 0x595F0F: the children.
	for (uint32_t i = 0; i < town.children; ++i) {
		spawn_child(false);
	}
	// 0x596293: with the plague, a rat for every 50 residents.
	if (town.plague) {
		for (uint32_t i = 0; i < town.total / 50; ++i) {
			spawn_rat(false, true);
		}
	}
	prefill_roads();
	tick_due_ = now + 0x616;
	sync_carts(true);
}

// A child on a town's point, facing a diagonal (0x595F0F, 0x59797E: `visible_check`, out of sight).
bool TownLife::spawn_child(bool visible_check) {
	TownCell2 cell;
	if (!spawn_cell(true, visible_check, true, cell) || people[3].empty()) {
		return false;
	}
	const TownCell2 p = life_cell_pixel(cell[0], cell[1]);
	const uint8_t dir = uint8_t((rand() % 4) * 64 + 0x20);
	Creature c;
	c.cls = 3;
	c.look = people[3][size_t(rand()) % people[3].size()];
	c.id = next_id_++;
	if (next_id_ == 0) {
		next_id_ = 1;
	}
	c.street_first = true;
	anim_start(c, 2);
	c.x = p[0] << 16;
	c.y = p[1] << 16;
	c.dir = c.target = c.wish = dir;
	c.goal_x = uint16_t(p[0]);
	c.goal_y = uint16_t(p[1]);
	choose_goal(c);
	place(c);
	creatures_.push_back(std::move(c));
	return true;
}

// 0x598280, when a town is shown: the juggler on one of the town's points (the spawn's tag 2) at random, facing SE;
// a market seller on each of the tag 3 points beside a stall (the first diagonal whose next cell has a stall, .let
// type 6), 8 pixels towards it and 5 up, facing it. Both play their sequence 5 (crying, juggling).
void TownLife::place_performers() {
	auto make = [&](int cls, int x, int y, uint8_t dir) {
		if (people[size_t(cls)].empty()) {
			return;
		}
		Creature c;
		c.cls = cls;
		c.mode = 3;
		c.look = people[size_t(cls)][size_t(rand()) % people[size_t(cls)].size()];
		c.id = next_id_++;
		if (next_id_ == 0) {
			next_id_ = 1;
		}
		c.frame = anim_start(c, 5);
		c.x = x << 16;
		c.y = y << 16;
		c.dir = c.target = dir;
		place(c);
		creatures_.push_back(std::move(c));
	};
	if (town_.spawn.size() > 2 && !town_.spawn[2].empty()) {
		const TownCell2 cell = town_.spawn[2][size_t(rand()) % town_.spawn[2].size()];
		const TownCell2 p = life_cell_pixel(cell[0], cell[1]);
		make(6, p[0], p[1], 0x60);
	}
	if (town_.spawn.size() > 3) {
		for (const TownCell2& cell : town_.spawn[3]) {
			int d = 0x20;
			for (; d <= 0xE0; d += 0x40) {
				int x = cell[0];
				int y = cell[1];
				life_neighbour(x, y, uint8_t(d), map_.width, map_.height);
				if (map_.object_at(x, y) == 6) {
					break;
				}
			}
			if (d >= 0x100) {
				continue;
			}
			const TownCell2 p = life_cell_pixel(cell[0], cell[1]);
			make(5, p[0] + (d == 0x60 || d == 0xE0 ? 8 : -8), p[1] - 5, uint8_t(d));
		}
	}
	// 0x5984AA: the official's first coming out in 30 to 60 s.
	official_due_ = now_ + (uint32_t(rand()) % 60000 + 60000) / 2;
	if (official_due_ == 0) {
		official_due_ = 1;
	}
}

// 0x563090: the official's next point of his path (`point`), the diagonal towards it and its steps (as the prefilled
// road walkers', 0x5963F3).
void TownLife::official_target(Creature& c) const {
	if (town_.nodes.size() < 4 || town_.nodes[3].empty() || c.point >= town_.nodes[3][0].size()) {
		return;
	}
	const TownCell2 at = town_.nodes[3][0][c.point];
	c.goal_x = uint16_t(at[0]);
	c.goal_y = uint16_t(at[1]);
	const uint32_t s = c.look->speed;
	const int32_t sx = x_pos(s * 2896);
	const int32_t sy = y_pos(s * 31856);
	if (int(c.goal_x) < c.px()) {
		c.target = int(c.goal_y) < c.py() ? 0xE0 : 0xA0;
		c.step_x = -sx;
	} else {
		c.target = int(c.goal_y) < c.py() ? 0x20 : 0x60;
		c.step_x = sx;
	}
	c.step_y = c.target == 0xE0 || c.target == 0x20 ? -sy : sy;
}

// 0x562D60: the official walks to his path's points (passing one, it snaps to it); at the last he turns to where he
// reads and unrolls his scroll; back at the first he goes, and the next comes out in 60 s (*0x677954). He turns by
// 0x20 a frame while walking.
bool TownLife::official_walk(Creature& c) {
	const uint32_t dt = std::min<uint32_t>(elapsed_, 1000);
	const uint8_t before = c.counter;
	uint8_t f = anim_step(c, &c.counter);
	int32_t nx = c.x;
	int32_t ny = c.y;
	if (c.seq == 2) {
		const int x = c.px();
		const int y = c.py();
		const int tx = c.goal_x;
		const int ty = c.goal_y;
		const bool passed = (x < tx && c.step_x < 0) || (x > tx && c.step_x > 0) || (y < ty && c.step_y < 0) ||
			(y > ty && c.step_y > 0);
		if (passed) {
			nx = tx << 16;
			ny = ty << 16;
			const size_t n = town_.nodes.size() > 3 && !town_.nodes[3].empty() ? town_.nodes[3][0].size() : 0;
			if (c.back) {
				if (c.point == 0) {
					official_due_ = now_ + 60000;
					return false;
				}
				--c.point;
				official_target(c);
			} else if (c.point + 1u == n) {
				if (c.face == 0) {
					c.face = c.dir;
				}
				if (c.dir == c.face) {
					f = anim_start(c, 0x83);
				} else {
					c.target = c.face;
				}
			} else {
				++c.point;
				official_target(c);
			}
		} else {
			nx += c.step_x * int32_t(dt);
			ny += c.step_y * int32_t(dt);
		}
	}
	uint8_t dir = c.dir;
	if (c.seq == 2) {
		if (c.counter != before && c.target != dir) {
			dir = uint8_t(uint8_t(c.target - dir) < 0x80 ? dir + 0x20 : dir - 0x20);
		}
	} else {
		dir = c.target;
	}
	const bool moved = (nx >> 16) != c.px() || (ny >> 16) != c.py() || f != c.frame || dir != c.dir;
	c.x = nx;
	c.y = ny;
	if (moved) {
		c.frame = f;
		c.dir = dir;
		place(c);
	}
	return true;
}

uint16_t TownLife::spawn_fisherman(int x, int y) {
	if (people[0x18].empty() || town_.nodes.size() < 5) {
		return 0;
	}
	const std::vector<TownPath>& paths = town_.nodes[4];
	auto free_at = [&](size_t k) { return k < fisher_free_.size() && fisher_free_[k] != 0 && !paths[k].empty(); };
	// 0x59A420: the first free path starting within 304 pixels (its distance squared at most 0x1685A).
	bool near = false;
	for (size_t k = 0; k < paths.size() && k < 0x100 && !near; ++k) {
		if (free_at(k)) {
			const int dx = x - paths[k][0][0];
			const int dy = y - paths[k][0][1];
			near = dy * dy + dx * dx <= 0x1685A;
		}
	}
	if (!near) {
		return 0;
	}
	// 0x569C20: a look at random, the free path starting nearest (the first of the nearest), his first point, walking
	// to its second; the path taken.
	Creature c;
	c.cls = 0x18;
	c.mode = 10;
	c.look = people[0x18][size_t(rand()) % people[0x18].size()];
	c.id = new_id();
	int best = 0x7FFFFFFF;
	size_t road = 0;
	for (size_t k = 0; k < paths.size() && k < 0x100; ++k) {
		if (free_at(k)) {
			const int dx = x - paths[k][0][0];
			const int dy = y - paths[k][0][1];
			const int d = dy * dy + dx * dx;
			if (d < best) {
				best = d;
				road = k;
			}
		}
	}
	c.road = uint8_t(road);
	c.frame = anim_start(c, 2);
	c.x = paths[road][0][0] << 16;
	c.y = paths[road][0][1] << 16;
	c.back = false;
	c.point = 1;
	fisher_target(c);
	c.dir = c.target;
	place(c);
	fisher_free_[road] = 0;
	creatures_.push_back(std::move(c));
	return creatures_.back().id;
}

void TownLife::remove_creature(uint16_t id) {
	for (size_t i = 0; i < creatures_.size(); ++i) {
		if (creatures_[i].id == id) {
			// A fisherman's path: the game never gives it back (his destructor 0x56A450 doesn't), so his hut, stopped
			// and at work again, makes the next on another, and none once those near are all taken, until the town
			// is loaded again; fixed behind fix_game_bugs() at the user's word (2026-11-21): given back.
			const Creature& c = creatures_[i];
			if (fix_game_bugs() && c.mode == 10 && c.road < fisher_free_.size()) {
				fisher_free_[c.road] = 1;
			}
			creatures_.erase(creatures_.begin() + std::ptrdiff_t(i));
			// A ship figure's pennant goes with it (0x56CD50).
			for (size_t k = 0; k < creatures_.size(); ++k) {
				if (creatures_[k].mode == 12 && creatures_[k].owner == id) {
					creatures_.erase(creatures_.begin() + std::ptrdiff_t(k));
					break;
				}
			}
			return;
		}
	}
}

uint16_t TownLife::add_figure(int cls, const LcrAnim* look, int32_t x, int32_t y, uint8_t dir) {
	Creature c;
	c.cls = cls;
	c.mode = 11;
	c.look = look;
	c.id = new_id();
	c.x = x;
	c.y = y;
	c.dir = dir;
	c.frame = 0;
	place(c);
	creatures_.push_back(std::move(c));
	return creatures_.back().id;
}

void TownLife::move_figure(uint16_t id, const LcrAnim* look, int32_t x, int32_t y, uint8_t dir) {
	for (Creature& c : creatures_) {
		if (c.id != id) {
			continue;
		}
		if (c.look != look || c.x != x || c.y != y || c.dir != dir) {
			c.look = look;
			c.x = x;
			c.y = y;
			c.dir = dir;
			c.frame = 0;
			place(c);
		}
		return;
	}
}

uint16_t TownLife::add_pennant(const LcrAnim* look, uint16_t owner, int32_t x, int32_t y) {
	Creature c;
	c.cls = 0x1E;
	c.mode = 12;
	c.look = look;
	c.owner = owner;
	c.id = new_id();
	c.x = x;
	c.y = y;
	// 0x568DD0: a frame at random of the first direction's (*0x6E1808), the view's wind, the first frame due within
	// a frame's time (*0x6E17F8: 1000 / fps).
	const size_t frames = look->directions[0].size();
	c.frame = frames ? uint8_t(uint32_t(rand()) % frames) : 0;
	c.dir = wind_dir_;
	c.frame_due = uint32_t(rand()) % frame_ms(*look) + now_;
	creatures_.push_back(std::move(c));
	return creatures_.back().id;
}

// 0x568E40: put elsewhere, facing the view's wind.
void TownLife::move_pennant(uint16_t id, int32_t x, int32_t y) {
	for (Creature& c : creatures_) {
		if (c.id == id) {
			c.x = x;
			c.y = y;
			c.dir = wind_dir_;
			return;
		}
	}
}

// 0x568EA0 (the pennant's step): when due, the next frame of the direction it faced (from the first after the last),
// turned to the view's wind, and the next due a frame's time less a twentieth of it for each step of the wind's
// strength (from the step, not from when it was due).
bool TownLife::pennant_wave(Creature& c) {
	if (c.frame_due > now_) {
		return true;
	}
	const size_t dirs = c.look->directions.size();
	const size_t d = ((size_t(c.dir) * dirs + 0x80) >> 8) & (dirs - 1);
	++c.frame;
	c.dir = wind_dir_;
	if (c.frame >= c.look->directions[d].size()) {
		c.frame = 0;
	}
	const uint32_t period = frame_ms(*c.look);
	c.frame_due = period - period * wind_strength_ / 20 + now_;
	return true;
}

// 0x58C345..0x58C6B5 (the view's frame, with the animations on): the waves. While the next is due: the two counters on
// (5 columns' worth of 13, a row of parts 3 on at each round), a cell at random in their part of the map (x from 9
// columns' worth, y from 16 rows' worth, by the game's magic divisions) and a size 1..3; when every ground cell within
// that many columns and twice that many rows is water, a wave of that size there (the effect 0x50 | size << 8, at
// the cell's middle, an alpha 0x40..0x7F). Then the next due by the port's wind: 4950 / (4 · strength² + 95) ms on,
// from now when more than 100 ms behind, 50 ms from now without wind; more in this frame while still behind, never
// more than 50 ms ahead. (In a sea battle's view, [view+0xC324] 0xFF, two by two in its quarters: not here.)
void TownLife::spawn_waves() {
	if (wave_due_ >= now_) {
		return;
	}
	for (;;) {
		wave_col_ = uint8_t(wave_col_ + 5);
		if (wave_col_ >= 13) {
			wave_col_ = uint8_t(wave_col_ - 13);
			wave_row_ = uint8_t((wave_row_ + 3) % 13);
		}
		const int32_t a = int32_t((uint32_t(rand()) % 9 + 9u * wave_col_) << 7);
		int32_t qx = int32_t((int64_t(a) * int32_t(0x84210843u)) >> 32) + a;
		qx >>= 6;
		qx += int32_t(uint32_t(qx) >> 31);
		const uint16_t x = uint16_t(qx + 4);
		const int32_t b = ((rand() & 15) + (int32_t(wave_row_) << 4)) * 226;
		int32_t qy = int32_t((int64_t(b) * int32_t(0x95A02569u)) >> 32) + b;
		qy >>= 7;
		qy += int32_t(uint32_t(qy) >> 31);
		const uint16_t y = uint16_t(qy + 6);
		const int size = rand() % 3 + 1;
		// Its box all water (the cells by their index in 16 bits, as the game's: one off the map is not water here).
		bool water = true;
		const uint32_t cells = uint32_t(map_.width) * uint32_t(map_.height);
		for (int dx = -size; dx <= size && water; ++dx) {
			for (int dy = -2 * size; dy <= 2 * size; ++dy) {
				const uint16_t index = uint16_t(uint16_t(map_.width * (int(y) + dy)) + dx + int(x));
				if (index >= cells || map_.ground[index] != kWater) {
					water = false;
					break;
				}
			}
		}
		if (water && wave_looks[size_t(size)]) {
			Creature c;
			c.cls = 0x12;
			c.mode = 13;
			c.look = wave_looks[size_t(size)];
			c.id = new_id();
			const TownCell2 at = life_cell_pixel(x, y);
			c.x = at[0] << 16;
			c.y = at[1] << 16;
			c.dir = 0x20;
			c.effect = 0x50;
			c.frame = anim_start(c, 2);  // 0x57A264
			c.frame_due = now_ + frame_ms(*c.look);
			(void)rand();  // 0x57A1F2: its look among those of its number and size (one: rand() % 1).
			c.alpha = uint8_t((rand() & 63) + 0x40);  // 0x58C5EB
			creatures_.push_back(std::move(c));
		}
		const uint32_t s = wind_strength_;
		if (now_ - wave_due_ > 100) {
			wave_due_ = now_;
		} else if (s != 0) {
			wave_due_ += uint32_t(0x1356 / int32_t(4 * s * s + 0x5F));
		} else {
			wave_due_ = now_ + 50;
		}
		if (wave_due_ >= now_) {
			break;
		}
	}
	if (wave_due_ - 50 > now_) {
		wave_due_ = now_ + 50;
	}
}

// 0x58C865..0x58CB51 (the view's frame, after the fish): with the town's port frozen, once ([view+0xC63C], cleared
// when it thaws) the ice floes: in each part of a 15 · 15 grid a cell at random (x from 7 columns' worth, y from 13
// rows' worth, by the game's magic divisions) and a size 1..3 (1 with the animations off); when every cell within that
// many columns and twice that many rows is water with no object, an ice floe of that size there (the effect 0xC7 |
// size << 8, at the cell's middle) fading in (0x57AF40(0, 0x80): its alpha from 1, 128 a second), its first frame due
// (its pixel's x + y) % 3000 ms on.
void TownLife::spawn_floes() {
	if (!town_.frozen) {
		floes_made_ = false;
		return;
	}
	if (floes_made_) {
		return;
	}
	floes_made_ = true;
	const uint32_t cells = uint32_t(map_.width) * uint32_t(map_.height);
	for (int gy = 0; gy < 15; ++gy) {
		for (int gx = 0; gx < 15; ++gx) {
			const int32_t a = (rand() % 7 + gx * 10) * 121;
			int32_t qx = int32_t((int64_t(a) * int32_t(0xE070381Du)) >> 32) + a;
			qx >>= 7;
			qx += int32_t(uint32_t(qx) >> 31);
			const uint16_t x = uint16_t(qx + 3);
			const int32_t b = (rand() % 13 + gy * 20) * 213;
			int32_t qy = int32_t((int64_t(b) * int32_t(0xE070381Du)) >> 32) + b;
			qy >>= 8;
			qy += int32_t(uint32_t(qy) >> 31);
			const uint16_t y = uint16_t(qy + 6);
			const int size = rand() % 3 + 1;  // (The animations on: always in the viewer.)
			bool clear = true;
			for (int dx = -size; dx <= size && clear; ++dx) {
				for (int dy = -2 * size; dy <= 2 * size; ++dy) {
					const uint16_t index = uint16_t(uint16_t(map_.width * (int(y) + dy)) + dx + int(x));
					if (index >= cells || map_.ground[index] != kWater || map_.object[index] >= 0) {
						clear = false;
						break;
					}
				}
			}
			if (!clear || !floe_looks[size_t(size)]) {
				continue;
			}
			Creature c;
			c.cls = 0x12;
			c.mode = 13;
			c.look = floe_looks[size_t(size)];
			c.id = new_id();
			const TownCell2 at = life_cell_pixel(x, y);
			c.x = at[0] << 16;
			c.y = at[1] << 16;
			c.dir = 0x20;
			c.effect = 0xC7;
			c.frame = anim_start(c, 2);
			(void)rand();  // 0x57A1F2: its look among those of its number and size (one: rand() % 1).
			c.fade_delay = 0;
			c.fade_speed = 0x80;
			c.alpha = 1;
			c.frame_due = uint32_t(at[1] + at[0]) % 3000 + now_;
			creatures_.push_back(std::move(c));
		}
	}
}

// 0x57A8C0 (an effect's step) for an effect on the water: gone after a frame of more than 2 s when its number is below
// 0x80 (0x57A906: the waves); its fade (after its delay, its alpha by its speed in steps of 16, the rest kept: gone at
// 0 or below, stopped at 255), or an ice floe with the port thawed starting to fade out (0x57A9A1: −0x80); its frames on
// (0x557340), a wave's each frame's time · 9 / (the wind's strength + 8) (0x57A9E6: the stronger, the faster); gone when
// its sequence (2) starts again with its number below 0x80 (0x57AA9B: a wave played once; the floes go round).
bool TownLife::water_effect_step(Creature& c) {
	if (raw_elapsed_ > 2000 && c.effect < 0x80) {
		return false;
	}
	if (c.fade_speed != 0) {
		if (c.fade_delay > raw_elapsed_) {
			c.fade_delay -= raw_elapsed_;
		} else {
			const int32_t v = int32_t(raw_elapsed_ - c.fade_delay) * c.fade_speed + c.fade_rest;
			c.fade_delay = 0;
			const int32_t q = v / 16000;
			c.fade_rest = int16_t(v - q * 16000);
			if (q != 0) {
				const int32_t alpha = int32_t(c.alpha) + q * 16;
				if (alpha <= 0) {
					return false;
				}
				if (alpha >= 0xFF) {
					c.alpha = 0xFF;
					c.fade_speed = 0;
				} else {
					c.alpha = uint8_t(alpha);
				}
			}
		}
	} else if (c.effect == 0xC7 && !town_.frozen) {
		c.fade_delay = 0;
		c.fade_speed = -0x80;
	}
	const uint8_t old = c.frame;
	const uint8_t f = anim_step(c);
	if (f != old && c.effect == 0x50) {
		c.frame_due = (c.frame_due - now_) * 9 / (uint32_t(wind_strength_) + 8) + now_;
	}
	if (f < old && c.effect < 0x80 && c.seq == 2) {
		return false;
	}
	c.frame = f;
	return true;
}

// 0x55B030 (its flag 0; with it, a soldier's on the town's walls: a siege's): with the snow shown, a footprint (an effect
// of class 0x12 of the water list) at (x, y) facing `dir`, but not on a street or its edges (the cell's ground class
// 0x10, 0x17 or 0x18); at alpha 180, starting to fade after a second at 18 a second (0x57AF40(1000, −18)).
void TownLife::footprint(uint8_t effect, int x, int y, uint8_t dir) {
	if (snow_mode_ <= 1 || snow_mode_ >= 6 || effect < 0xC3 || effect > 0xC6 || !footprint_looks[effect - 0xC3]) {
		return;
	}
	const TownCell2 cell = life_pixel_cell(x, y);
	const uint8_t ground = map_.ground_at(cell[0], cell[1]);
	if (ground == 0x10 || ground == 0x17 || ground == 0x18) {
		return;
	}
	Creature c;
	c.cls = 0x12;
	c.mode = 13;
	c.look = footprint_looks[effect - 0xC3];
	c.id = new_id();
	c.x = int32_t(uint32_t(x) << 16);
	c.y = int32_t(uint32_t(y) << 16);
	c.dir = dir;
	c.effect = effect;
	c.frame = anim_start(c, 2);
	(void)rand();  // 0x57A1F2: its look among those of its number (one: rand() % 1).
	c.fade_delay = 1000;
	c.fade_speed = -18;
	c.alpha = 0xB4;
	made_.push_back(std::move(c));  // (Made during the creatures' step: added after it.)
}

void TownLife::smoke(int32_t x16, int32_t y16, uint16_t rise, uint8_t dir, uint8_t alpha, int32_t move_x,
	int32_t move_y) {
	if (!smoke_look || smoke_look->directions.empty()) {
		return;
	}
	Creature c;
	c.cls = 0x12;
	c.mode = 17;
	c.look = smoke_look;
	c.id = new_id();
	c.x = x16;
	c.y = int32_t(uint32_t(y16) + 0x300000u);  // (0x57A2C4.)
	c.dir = dir;
	c.effect = 0x24;
	c.rise = rise;
	c.alpha = alpha;
	c.move_x = move_x;
	c.move_y = move_y;
	c.frame = anim_start(c, 2);
	(void)rand();  // 0x57A1F2: its look among those of its number (one: rand() % 1).
	place(c);
	made_.push_back(std::move(c));
}

// The effects among the strips (0x57A8C0, as the water's: the smoke has no fade), moved by their motion · the frame's
// ms (0x57AA36), their cells again.
bool TownLife::strip_effect_step(Creature& c) {
	if (!water_effect_step(c)) {
		return false;
	}
	c.x = int32_t(uint32_t(c.x) + uint32_t(c.move_x) * raw_elapsed_);
	c.y = int32_t(uint32_t(c.y) + uint32_t(c.move_y) * raw_elapsed_);
	place(c);
	return true;
}

// Within 200 pixels of the view (0x55D0E3.., 0x57C451..).
bool TownLife::near_view(const Creature& c) const {
	return c.px() + 200 >= view_[0] && c.py() + 200 >= view_[1] && c.px() <= view_[0] + view_[2] + 200 &&
		c.py() <= view_[1] + view_[3] + 200;
}

// 0x55D0B0 (from the free walk, 0x55C8D9, as its frame is about to change; its look's class not the prince's): by the
// sequence playing ([+0x34] − 2, the table 0x55D60C): walking (2), running (6), 0xB and 0x13 a left foot at its frame 4
// of the sequence and a right one at its 9; 5, 0xD, 0xF, 0x14, 0x83, 0x84, 0x8C, 0x92, 0xEC and 0xEE both at its first
// frame; at its place, small ones for a child (class 3).
void TownLife::walker_footprints(const Creature& c) {
	if (snow_mode_ <= 1 || snow_mode_ >= 6 || !c.look || c.look->kind == 0x23 || !near_view(c)) {
		return;
	}
	const bool child = c.look->kind == 3;
	const uint8_t left = child ? 0xC5 : 0xC3;
	const uint8_t right = child ? 0xC6 : 0xC4;
	const int si = sequence_index(*c.look, c.seq);
	if (si < 0) {
		return;
	}
	const int first = c.look->sequences[size_t(si)].first;
	switch (c.seq) {
	case 0x02: case 0x06: case 0x0B: case 0x13:
		if (c.frame - first == 4) {
			footprint(left, c.px(), c.py(), c.dir);
		} else if (c.frame - first == 9) {
			footprint(right, c.px(), c.py(), c.dir);
		}
		break;
	case 0x05: case 0x0D: case 0x0F: case 0x14: case 0x83: case 0x84: case 0x8C: case 0x92: case 0xEC: case 0xEE:
		if (c.frame == first) {
			footprint(left, c.px(), c.py(), c.dir);
			footprint(right, c.px(), c.py(), c.dir);
		}
		break;
	default:
		break;
	}
}

// The animals' feet (0x57C61E.. the dogs and cats, 0x57EC12.. the wild ones; as the frame is about to change): their
// look's parameter 2 the kind (0 small prints 0xC5, 0xC6; else 0xC3, 0xC4), 3 the feet's distance d, 4..7 the frames a
// left front foot touches the ground, 8..11 a left back one, 12..15 a right front one, 16..19 a right back one (255
// none). The front feet at the place moved by (dx, dy) along the way it faces, the back ones by the opposite, d lower
// (2d the wild ones); (dx, dy) by its direction: d across, 11d/17 up and down, the diagonals 5d/7 and (the dogs and
// cats) 5d/7 or (the wild ones) 55d/119, toward zero. A wild one of kind 1 (the bear) its left feet a quarter of the
// way's normal to one side and its right feet to the other.
void TownLife::animal_footprints(const Creature& c) {
	const std::vector<uint32_t>& p = c.look->params;
	if (snow_mode_ <= 1 || snow_mode_ >= 6 || p.size() < 20 || !near_view(c)) {
		return;
	}
	const bool wild = c.cls == 0x10;
	const int d = int(p[3]);
	int dx = 0;
	int dy = 0;
	switch (c.dir) {
	case 0x00: dy = -(11 * d / 17); break;
	case 0x20: dx = 5 * d / 7; dy = -(wild ? 55 * d / 119 : 5 * d / 7); break;
	case 0x40: dx = d; break;
	case 0x60: dx = 5 * d / 7; dy = wild ? 55 * d / 119 : 5 * d / 7; break;
	case 0x80: dy = 11 * d / 17; break;
	case 0xA0: dx = -(5 * d / 7); dy = wild ? 55 * d / 119 : 5 * d / 7; break;
	case 0xC0: dx = -d; break;
	case 0xE0: dx = -(5 * d / 7); dy = -(wild ? 55 * d / 119 : 5 * d / 7); break;
	default: return;  // (Off the eight directions the game reads a stale value: never, they turn by 0x20.)
	}
	const bool big = p[2] != 0;
	const int below = wild ? 2 * d : d;
	// The bear's sideways quarter (its left feet: (dy >> 2, −dx >> 2); its right ones the other way).
	const int sx = wild && big ? dy >> 2 : 0;
	const int sy = wild && big ? -(dx >> 2) : 0;
	const int x = c.px();
	const int y = c.py() + below;
	for (int k = 0; k < 4; ++k) {
		if (c.frame == p[size_t(4 + k)]) {
			footprint(big ? 0xC3 : 0xC5, x + dx + sx, y + dy + sy, c.dir);
		}
		if (c.frame == p[size_t(8 + k)]) {
			footprint(big ? 0xC3 : 0xC5, x - dx + sx, y - dy + sy, c.dir);
		}
		if (c.frame == p[size_t(12 + k)]) {
			footprint(big ? 0xC4 : 0xC6, x + dx - sx, y + dy - sy, c.dir);
		}
		if (c.frame == p[size_t(16 + k)]) {
			footprint(big ? 0xC4 : 0xC6, x - dx - sx, y - dy - sy, c.dir);
		}
	}
}

namespace {

// The rain's and the snow's step in 16.16 pixels for a speed `v` over `elapsed` ms (0x575B5E.., 0x5763B2..): `scaled`
// the speed already scaled by its look (the snow's |v| · speed >> 6; the rain's |v| · 12000 / 64), then · elapsed · 32
// / 312 (the game's magic 0xD20D20D3), · 32.
int32_t precip_step(int32_t scaled, uint32_t elapsed) {
	const int32_t a = int32_t(uint32_t(scaled) * elapsed) << 5;
	int32_t q = int32_t((int64_t(a) * int32_t(0xD20D20D3u)) >> 32) + a;
	q >>= 8;
	q += int32_t(uint32_t(q) >> 31);
	return q << 5;
}

}  // namespace

// 0x58F4E0 (each frame, early: 0x58C103) as far as the rain and snow go: the weather's kind (the town's [0x9C4] & 6
// rain or snow 2, else its [0x2C8] & 8 the plague 1, else [0x9C4] & 1 fog 3, else 0); when it changes (or, raining or
// snowing, the rain's bit does), 0x58B090: the most 50 with rain or snow, else 0, and a new one every 9000 (snow) or
// 2500 (rain) ms over that many. (Its tint and fog: part 6, to come. P2.cfg's WEATHER, 3 in the game's files: its
// second argument 0.)
int tint_steps(uint32_t current, uint32_t target, uint8_t speed, uint32_t& step) {
	step = 0;
	int count = 0;
	for (int i = 0; i < 4; ++i) {
		const int d = int((target >> (8 * i)) & 0xFF) - int((current >> (8 * i)) & 0xFF);
		if (d == 0) {
			continue;
		}
		step |= uint32_t(uint8_t(d > 0 ? speed : -speed)) << (8 * i);
		count = std::max(count, (std::abs(d) + speed - 1) / speed);
	}
	return count;
}

void tint_split(uint32_t current, uint32_t target, int count, uint32_t& step) {
	step = 0;
	const int n = count & 0xFF;
	if (n == 0) {
		return;
	}
	for (int i = 0; i < 4; ++i) {
		const int d = int((target >> (8 * i)) & 0xFF) - int((current >> (8 * i)) & 0xFF);
		int s = 0;
		if (d > 0) {
			s = d + n - 1 > n ? (d + n - 1) / n : 1;
		} else if (d < 0) {
			s = d + 1 - n < -n ? (d + 1 - n) / n : -1;
		} else {
			continue;
		}
		step |= uint32_t(uint8_t(s)) << (8 * i);
	}
}

uint32_t tint_move(uint32_t current, uint32_t target, uint32_t step) {
	uint32_t out = 0;
	for (int i = 0; i < 4; ++i) {
		const int s = int8_t(uint8_t(step >> (8 * i)));
		const int t = int((target >> (8 * i)) & 0xFF);
		int v = int((current >> (8 * i)) & 0xFF) + s;
		if ((s > 0 && v > t) || (s < 0 && v < t)) {
			v = t;
		}
		out |= uint32_t(uint8_t(v)) << (8 * i);
	}
	return out;
}

// 0x59B410: the fog banks gone (with a fog texture chosen), the colours back to none and the sweep done (the objects'
// target [+0xC17C] kept).
void TownLife::reset_weather() {
	if (tint_.texture >= 0) {
		remove_fog_banks();
	}
	tint_.swept_at = 0;
	tint_.step = 0;
	tint_.over_step = 0;
	tint_.target = tint_.colour = tint_.previous = tint_.over = 0x00FFFFFF;
	tint_.texture = -1;
	weather_kind_ = 0;
	tint_.banks = tint_.banks_target = tint_.banks_step = 0;
	tint_.rect = {0, 0, 0, 0};
}

// The fog banks (0x58F76F, fog with WEATHER 3): 20 rows of 5, a row at every twentieth of the map's height, each
// column's at a fifth of its width and within a tenth of it either way (rand() % (W / 5) − W / 10), facing 0x20.
void TownLife::make_fog_banks() {
	if (fog_looks.empty()) {
		return;
	}
	const uint32_t w5 = map_pixels_x(map_) / 5;
	const uint32_t h20 = map_pixels_y(map_) / 20;
	size_t k = 0;
	for (uint32_t row = 0; row < 20; ++row) {
		for (uint32_t col = 0; col < 5; ++col, ++k) {
			const int32_t x = int32_t((w5 ? uint32_t(rand()) % w5 : 0) - (w5 >> 1) + col * w5);
			// 0x5732B0: its look one of the class's at random, a random fraction of a pixel to its x.
			Creature c;
			c.cls = 0x1C;
			c.mode = 16;
			c.id = new_id();
			c.look = fog_looks[size_t(uint32_t(rand()) % fog_looks.size())];
			c.x = int32_t((uint32_t(x) << 16) + uint32_t(rand()));
			c.y = int32_t(row * h20) << 16;
			c.dir = 0x20;
			fog_banks_[k] = c.id;
			creatures_.push_back(std::move(c));
		}
	}
}

void TownLife::remove_fog_banks() {
	for (uint16_t& id : fog_banks_) {
		if (id != 0) {
			remove_creature(id);
			id = 0;
		}
	}
}

// 0x573350 (a fog bank's step, the frame's unclamped time): to the right by its look's speed (the LCR head's 0x10,
// 60: 6 pixels a second; the product of the 16.16 speed and the ms kept to 32 bits), from past the map's right edge
// back to its picture's width left of its left; never gone.
bool TownLife::fog_drift(Creature& c) {
	const uint32_t v = (uint32_t(c.look->speed) << 16) * raw_elapsed_;
	c.x = int32_t(uint32_t(c.x) + uint32_t((uint64_t(v) * 0xD1B71759u) >> 45));
	if (int16_t(uint32_t(c.x) >> 16) > int(map_pixels_x(map_))) {
		const LcrFrame* f = c.current();
		c.x = int32_t(uint32_t(-(f ? f->body.w : 0)) << 16);
	}
	return true;
}

// 0x58F4E0: the weather's kind (the town's [0x9C4] & 6 rain or snow, else its plague, else its [0x9C4] & 1 fog) and,
// when it changes (or the rain's bit against the tint's alpha), its rain or snow (0x58B090: 50 at most, a new one every
// (snow 9000, rain 2500) / 50 ms), a fog texture if none (rand() % FogTexCnt), and the tint's targets: the plague
// alpha 155 − rand() % 40 and a base 70; rain the same alpha and a base 150 − rand() % 40 (snow none); fog alpha 185 −
// rand() % 40, base 255 − rand() % 20, and its banks; each of red, green and blue the base − rand() % 10. The buildings'
// colour over it white for the plague and fog, none else; the banks' white for fog. Then the steps to them (2 a byte
// for the colour, the rest in as many). Each frame the colours go on: at once, or (WEATHER 3) a step when the last
// one's sweep down the view is over and 200 ms have passed since it began, the sweep moving with the view's scroll; a
// tint come to its target with its banks' alpha 0 takes the banks away.
void TownLife::update_weather(bool at_once) {
	const uint8_t w = town_.weather;
	const int kind = (w & 6) ? 2 : town_.plague ? 1 : (w & 1) ? 3 : 0;
	const bool rain = (w & 2) != 0;
	WeatherTint& t = tint_;
	if (kind != weather_kind_ || (kind == 2 && rain != ((t.target >> 24) != 0))) {
		precip_max_ = kind == 2 ? 50 : 0;
		precip_period_ = precip_max_ ? uint32_t(((w & 4) ? 9000 : 2500) / precip_max_) : 0;
		if (kind == 0) {
			t.banks_target = 0;
			t.target = 0x00FFFFFF;
		} else {
			if (t.texture < 0) {
				t.texture = fog_tex_base + (fog_tex_count > 0 ? int(uint32_t(rand()) % uint32_t(fog_tex_count)) : 0);
			}
			uint8_t a = 0;
			uint8_t base = 0;
			if (kind == 1) {
				t.banks_target = 0;
				t.over_target = 0xFFFFFFFF;
				a = uint8_t(155 - rand() % 40);
				base = 0x46;
			} else if (kind == 2) {
				t.over_target = 0x00FFFFFF;
				t.banks_target = 0;
				if ((t.over >> 24) == 0) {
					t.over = 0x00FFFFFF;
				}
				if (rain) {
					a = uint8_t(155 - rand() % 40);
					base = uint8_t(150 - rand() % 40);
				}
			} else {
				if (t.colour == 0) {
					t.colour = 0x00FFFFFF;
				}
				t.over_target = 0xFFFFFFFF;
				a = uint8_t(185 - rand() % 40);
				base = uint8_t(255 - rand() % 20);
				t.banks_target = 0xFFFFFFFF;
				if (fog_banks_[0] == 0) {
					make_fog_banks();
				}
			}
			uint8_t r = 0xFF;
			uint8_t g = 0xFF;
			uint8_t b = 0xFF;
			if (a != 0) {
				r = uint8_t(base - rand() % 10);
				g = uint8_t(base - rand() % 10);
				b = uint8_t(base - rand() % 10);
			}
			t.target = uint32_t(a) << 24 | uint32_t(r) << 16 | uint32_t(g) << 8 | b;
		}
		const int n = tint_steps(t.colour, t.target, 2, t.step);
		tint_split(t.over, t.over_target, n, t.over_step);
		tint_split(t.banks, t.banks_target, n, t.banks_step);
		weather_kind_ = kind;
	}
	// 0x58F97A: the colours on.
	const int dx = last_view_set_ ? view_[0] - last_view_[0] : 0;
	const int dy = last_view_set_ ? view_[1] - last_view_[1] : 0;
	auto arrived = [&] {
		if (t.colour == t.target && t.rect_empty()) {
			t.previous = t.colour;
			if ((t.banks >> 24) == 0 && fog_banks_[0] != 0) {
				remove_fog_banks();
			}
		}
	};
	if (t.colour == t.target && t.rect_empty()) {
		return;
	}
	if (at_once) {
		t.colour = t.target;
		t.banks = t.banks_target;
		t.over = t.over_target;
		t.rect = {0, 0, 0, 0};
		arrived();
		return;
	}
	if ((dx | dy) != 0 && !t.rect_empty()) {
		// OffsetRect by the scroll, IntersectRect with the view.
		t.rect = {std::max(t.rect[0] - dx, 0), std::max(t.rect[1] - dy, 0), std::min(t.rect[2] - dx, view_[2]),
			std::min(t.rect[3] - dy, view_[3])};
		if (t.rect_empty()) {
			t.rect = {0, 0, 0, 0};
		}
	}
	if (!t.rect_empty()) {
		// 0x58FB17: the sweep down, the part of the rest the time since its last move is of the time left to the step's
		// end (all of it past that), whole tenths of the view's height while more than one is left, at most one.
		const int left = t.rect[3] - t.rect[1];
		int rows = left;
		if (int32_t(t.due - now_) > 0) {
			rows = int(uint32_t(left) * (now_ - t.swept_at) / uint32_t(t.due - now_));
		}
		const int tenth = view_[3] / 10;
		if (left > tenth && tenth > 0) {
			rows -= rows % tenth;
			if (rows == 0) {
				return;
			}
		}
		rows = std::min(std::min(rows, tenth), left);
		t.rect[1] += rows;
		arrived();
		t.swept_at = now_;
		return;
	}
	// 0x58FC49: the next step when due, the sweep begun over the whole view.
	if (now_ < t.due) {
		return;
	}
	if (t.over_step != 0) {
		t.over = tint_move(t.over, t.over_target, t.over_step);
	}
	if (t.banks_step != 0) {
		t.banks = tint_move(t.banks, t.banks_target, t.banks_step);
	}
	t.previous = t.colour;
	t.colour = tint_move(t.colour, t.target, t.step);
	t.rect = {0, 0, view_[2], view_[3]};
	t.due = now_ + 200;
}

// 0x58AD30: `count` falling ones in `rect` (x0, y0, x1, y1 in the town's pixels), while fewer than the most: snow (the
// town's [0x9C4] & 4: class 0x1D) or rain (0x21); each at a point of one rand() (its x by the width, its y by the
// height), drifting −30..29 (rain −15..−6) and falling 41..60 by it; moved so that it is inside the rect where the rect
// is the view's edge (a whole width strip: its bottom at the rect's bottom from the top, its top at the strip's from
// below; else its right or its left).
void TownLife::precip_in(const std::array<int, 4>& rect, int count) {
	const bool snow = (town_.weather & 4) != 0;
	const std::array<int, 4>& v = view_;
	for (; count > 0 && precip_count_ < precip_max_; --count) {
		if (snow && flake_looks.empty()) {
			return;
		}
		const int r = rand();
		int x = r % std::max(1, rect[2] - rect[0]) + rect[0];
		int y = r % std::max(1, rect[3] - rect[1]) + rect[1];
		Creature c;
		c.id = new_id();
		int xo = 0;
		int yo = 0;
		int w = 0;
		int h = 0;
		if (snow) {
			c.cls = 0x1D;
			c.mode = 14;
			c.drift = int16_t(int8_t(uint8_t(r % 60 - 30)));
			c.fall = int16_t(uint8_t(60 - r % 20));
			// 0x576250: a look at random and a frame of it.
			c.look = flake_looks[size_t(uint32_t(rand()) % flake_looks.size())];
			const size_t frames = c.look->directions[0].size();
			c.frame = frames ? uint8_t(uint32_t(rand()) % frames) : 0;
			if (const LcrFrame* f = c.current()) {
				xo = f->body.x - f->anchor_x;
				yo = f->body.y - f->anchor_y;
				w = f->body.w;
				h = f->body.h;
			}
		} else {
			c.cls = 0x21;
			c.mode = 15;
			c.drift = int16_t(int8_t(uint8_t(r % 10 - 15)) << 8);
			c.fall = int16_t(uint8_t(60 - r % 20) << 8);
			const int dx = c.drift >> 8;
			const int dy = c.fall >> 8;
			xo = std::min(0, dx);
			yo = std::min(0, dy);
			w = std::abs(dx) + 1;
			h = std::abs(dy) + 1;
		}
		if (rect[0] == v[0] && rect[2] == v[0] + v[2]) {
			if (rect[1] == v[1]) {
				if (yo + h + y > rect[3]) {
					y = rect[3] - h - yo;
				}
			} else if (yo + y < v[3] - rect[3] + v[1] + rect[1]) {
				y = v[3] - yo - rect[3] + v[1] + rect[1];
			}
		} else if (rect[0] == v[0]) {
			if (w + xo + x > rect[2]) {
				x = rect[2] - w - xo;
			}
		} else if (xo + x < v[2] - rect[2] + v[0] + rect[0]) {
			x = v[2] - xo - rect[2] + v[0] + rect[0];
		}
		c.x = x << 16;
		c.y = y << 16;
		creatures_.push_back(std::move(c));
		++precip_count_;
	}
	precip_last_ = std::max(precip_last_, precip_count_);
}

// 0x58AA30 (each frame, after the creatures' step): while fewer than the most, with the view moved, as many as the
// count when it last stood lacks (or a fifth of the count, never past the most) in the part newly in view, by the
// strips' areas; when due (its time ± a quarter on), one on the view's top line for snow, for rain one there, one
// anywhere in the view and one in its lower right quarter. With the view standing, the count kept as that count.
void TownLife::spawn_precipitation() {
	const std::array<int, 4>& v = view_;
	const int dx = last_view_set_ ? v[0] - last_view_[0] : 0;
	const int dy = last_view_set_ ? v[1] - last_view_[1] : 0;
	last_view_ = v;
	last_view_set_ = true;
	if (precip_count_ >= precip_max_) {
		return;
	}
	if ((dx | dy) != 0 && precip_max_ != precip_count_) {
		int n = precip_last_ - precip_count_;
		if (n < precip_count_ / 5) {
			n = precip_count_ / 5;
			if (precip_count_ + n > precip_max_) {
				n = precip_max_ - precip_count_;
			}
		}
		const std::array<int, 4> now_rect = {v[0], v[1], v[0] + v[2], v[1] + v[3]};
		const std::array<int, 4> old_rect = {now_rect[0] - dx, now_rect[1] - dy, now_rect[2] - dx, now_rect[3] - dy};
		const std::array<int, 4> in = {std::max(now_rect[0], old_rect[0]), std::max(now_rect[1], old_rect[1]),
			std::min(now_rect[2], old_rect[2]), std::min(now_rect[3], old_rect[3])};
		if (in[0] >= in[2] || in[1] >= in[3]) {
			precip_in(now_rect, n);
		} else {
			// SubtractRect of the whole width band and of the whole height band of what stayed in view.
			const std::array<int, 4> across = {now_rect[0], in[3] == now_rect[3] ? now_rect[1] : in[3], now_rect[2],
				in[3] == now_rect[3] ? in[1] : now_rect[3]};
			const std::array<int, 4> down = {in[2] == now_rect[2] ? now_rect[0] : in[2], now_rect[1],
				in[2] == now_rect[2] ? in[0] : now_rect[2], now_rect[3]};
			const int a = (across[3] - across[1]) * (across[2] - across[0]);
			const int b = (down[3] - down[1]) * (down[2] - down[0]);
			if (a + b > 0) {
				precip_in(across, n * a / (a + b));
				precip_in(down, n * b / (a + b));
			}
		}
	}
	if (now_ >= precip_due_) {
		const uint32_t p = precip_period_;
		precip_due_ = (p ? uint32_t(rand()) % p : 0) / 2 - p / 4 + p + now_;
		const std::array<int, 4> top = {v[0], v[1], v[0] + v[2], v[1] + 1};
		precip_in(top, 1);
		if (!(town_.weather & 4)) {
			precip_in({v[0], v[1], v[0] + v[2], v[1] + v[3]}, 1);
			precip_in({v[0] + v[2] / 2, v[1] + v[3] / 2, v[0] + v[2], v[1] + v[3]}, 1);
		}
	}
	if ((dx | dy) == 0) {
		precip_last_ = precip_count_;
	}
}

// A snowflake's rectangle (its frame's, x0, y0, x1, y1 in the town's pixels).
std::array<int, 4> TownLife::flake_rect(const Creature& c) const {
	const LcrFrame* f = c.current();
	if (!f) {
		return {c.px(), c.py(), c.px(), c.py()};
	}
	const int x = c.px() - f->anchor_x + f->body.x;
	const int y = c.py() - f->anchor_y + f->body.y;
	return {x, y, x + f->body.w, y + f->body.h};
}

// 0x59B5B0's answer: whether a rectangle (x0, y0, x1, y1) meets the view.
bool TownLife::seen(const std::array<int, 4>& r) const {
	return r[2] > view_[0] && r[3] > view_[1] && r[0] < view_[0] + view_[2] && r[1] < view_[1] + view_[3];
}

// 0x576300 (a snowflake's step, the frame's unclamped time): melting, when due its alpha down by its look's second
// parameter, gone once it is no more than that or out of the view (+0x18 → 0x5593C0); else moved by its drift and fall
// scaled by its look's speed, its frame on when due and then, with its look's first parameter, one chance in (the
// view's height · it · 0x3AB95901 >> 39) to start melting (its alpha 255); moved or on, gone when its rectangle before
// and after is out of the view (0x559130 → 0x59B5B0). (A look without that parameter never melts: it falls out.)
bool TownLife::flake_fall(Creature& c) {
	const LcrAnim& look = *c.look;
	const uint32_t p0 = look.params.size() > 0 ? look.params[0] : 0;
	const uint32_t p1 = look.params.size() > 1 ? look.params[1] : 0;
	if (c.melting) {
		if (now_ <= c.frame_due) {
			return true;
		}
		if (uint32_t(c.fall) > p1) {
			c.fall = int16_t(c.fall - int16_t(p1));
			c.frame_due = frame_ms(look) + now_;
			return seen(flake_rect(c));
		}
		return false;
	}
	const std::array<int, 4> before = flake_rect(c);
	const uint8_t frame = c.frame;
	int32_t dx = precip_step(int32_t((uint32_t(std::abs(c.drift)) * look.speed) >> 6), raw_elapsed_);
	int32_t dy = precip_step(int32_t((uint32_t(std::abs(c.fall)) * look.speed) >> 6), raw_elapsed_);
	if (c.drift < 0) {
		dx = -dx;
	}
	if (c.fall < 0) {
		dy = -dy;
	}
	c.x += dx;
	c.y += dy;
	if (now_ > c.frame_due) {
		++c.frame;
		if (c.frame >= look.directions[0].size()) {
			c.frame = 0;
		}
		c.frame_due = frame_ms(look) + now_;
		if (p0 != 0) {
			const uint32_t k = uint32_t((uint64_t(uint32_t(view_[3]) * p0) * 0x3AB95901u) >> 39);
			if (k != 0 && uint32_t(rand()) % k == 0) {
				c.melting = true;
				c.fall = 0xFF;
			}
		}
	}
	const std::array<int, 4> after = flake_rect(c);
	if (before == after && frame == c.frame) {
		return true;
	}
	return seen({std::min(before[0], after[0]), std::min(before[1], after[1]), std::max(before[2], after[2]),
		std::max(before[3], after[3])});
}

// 0x575B00 (a raindrop's step): moved by its drift and fall (· 12000 / 64), then, if its place changed, its line
// shortened (its 8.8 drift and fall each less 6 · their high bytes); gone once its fall is 3 or less or it is out of
// the view.
bool TownLife::drop_fall(Creature& c) {
	const int px = c.px();
	const int py = c.py();
	const int8_t drift = int8_t(c.drift >> 8);
	const int8_t fall = int8_t(c.fall >> 8);
	int32_t dx = precip_step((std::abs(int(drift)) * 12000) / 64, raw_elapsed_);
	int32_t dy = precip_step((std::abs(int(fall)) * 12000) / 64, raw_elapsed_);
	if (drift < 0) {
		dx = -dx;
	}
	if (fall < 0) {
		dy = -dy;
	}
	c.x += dx;
	c.y += dy;
	if (c.px() == px && c.py() == py) {
		return true;
	}
	c.drift = int16_t(c.drift - 6 * int16_t(drift));
	c.fall = int16_t(c.fall - 6 * int16_t(fall));
	const int ndx = int8_t(c.drift >> 8);
	const int ndy = int8_t(c.fall >> 8);
	const std::array<int, 4> before = {std::min(px, px + drift), std::min(py, py + fall), std::max(px, px + drift) + 1,
		std::max(py, py + fall) + 1};
	const std::array<int, 4> after = {std::min(c.px(), c.px() + ndx), std::min(c.py(), c.py() + ndy),
		std::max(c.px(), c.px() + ndx) + 1, std::max(c.py(), c.py() + ndy) + 1};
	return seen({std::min(before[0], after[0]), std::min(before[1], after[1]), std::max(before[2], after[2]),
				   std::max(before[3], after[3])}) &&
		ndy > 3;
}

// 0x56A240: the fisherman's next point of his path (`point`), the diagonal towards it and its steps (the official's).
void TownLife::fisher_target(Creature& c) const {
	if (town_.nodes.size() < 5 || c.road >= town_.nodes[4].size() || c.point >= town_.nodes[4][c.road].size()) {
		return;
	}
	const TownCell2 at = town_.nodes[4][c.road][c.point];
	c.goal_x = uint16_t(at[0]);
	c.goal_y = uint16_t(at[1]);
	const uint32_t s = c.look->speed;
	const int32_t sx = x_pos(s * 2896);
	const int32_t sy = y_pos(s * 31856);
	if (int(c.goal_x) < c.px()) {
		c.target = int(c.goal_y) < c.py() ? 0xE0 : 0xA0;
		c.step_x = -sx;
	} else {
		c.target = int(c.goal_y) < c.py() ? 0x20 : 0x60;
		c.step_x = sx;
	}
	c.step_y = c.target == 0xE0 || c.target == 0x20 ? -sy : sy;
}

// 0x569E10: the fisherman. A turn done (9 or 10 played once, held: 0xA9, 0xAA) he faces 0x40 to the right or left and
// walks; walking not facing his way he turns (9 if it is to his right, else 10); facing it, passing his point he
// snaps to it and goes to the next (back at the first, forward again from the second; at the last forward, he casts
// his net: 12 once), else he walks on.
bool TownLife::fisher_walk(Creature& c) {
	const uint32_t dt = std::min<uint32_t>(elapsed_, 1000);
	uint8_t f = anim_step(c);
	uint8_t dir = c.dir;
	if (c.seq == 0xA9 || c.seq == 0xAA) {
		dir = uint8_t(dir + (c.seq == 0xA9 ? 0x40 : 0xC0));
		f = anim_start(c, 2);
	}
	int32_t nx = c.x;
	int32_t ny = c.y;
	if (c.seq == 2) {
		if (c.target != dir) {
			f = anim_start(c, uint8_t(c.target - dir) < 0x80 ? 0x89 : 0x8A);
		} else {
			const int x = c.px();
			const int y = c.py();
			const int tx = c.goal_x;
			const int ty = c.goal_y;
			const bool passed = (x < tx && c.step_x < 0) || (x > tx && c.step_x > 0) || (y < ty && c.step_y < 0) ||
				(y > ty && c.step_y > 0);
			if (passed) {
				const size_t n = town_.nodes.size() > 4 && c.road < town_.nodes[4].size() ? town_.nodes[4][c.road].size() : 0;
				if (c.back) {
					if (c.point == 0) {
						c.back = false;
						c.point = 1;
					} else {
						--c.point;
					}
				} else if (c.point + 1u == n) {
					f = anim_start(c, 0x8C);
				} else {
					++c.point;
				}
				nx = tx << 16;
				ny = ty << 16;
				if (c.seq == 2) {
					fisher_target(c);
				}
			} else {
				nx += c.step_x * int32_t(dt);
				ny += c.step_y * int32_t(dt);
			}
		}
	}
	const bool moved = (nx >> 16) != c.px() || (ny >> 16) != c.py() || f != c.frame || dir != c.dir;
	c.x = nx;
	c.y = ny;
	if (moved) {
		c.frame = f;
		c.dir = dir;
		place(c);
	}
	return true;
}

// A rat on a town's point (0x5962D9 when a town is shown with the plague, 0x597E91): facing a diagonal; it may go on
// grass (0x55A290: 1, 0, 0, 0).
bool TownLife::spawn_rat(bool visible_check, bool no_grass) {
	TownCell2 cell;
	if (people[0x1A].empty() || !spawn_cell(true, visible_check, no_grass, cell)) {
		return false;
	}
	const TownCell2 p = life_cell_pixel(cell[0], cell[1]);
	const uint8_t dir = uint8_t((rand() % 4) * 64 + 0x20);
	Creature c;
	c.cls = 0x1A;
	c.mode = 5;
	c.look = people[0x1A][size_t(rand()) % people[0x1A].size()];
	c.id = next_id_++;
	if (next_id_ == 0) {
		next_id_ = 1;
	}
	c.street_first = false;
	c.frame = anim_start(c, 2);
	c.x = p[0] << 16;
	c.y = p[1] << 16;
	c.dir = c.target = dir;
	place(c);
	creatures_.push_back(std::move(c));
	return true;
}

// 0x573900: a rat runs straight on at its look's speed (across 14 pixels a second for 140, up and down 11/17 of
// that); at each frame, if the way ahead is free, each time its count of frames passes its look's first parameter
// (70) it turns a little at random; if not, it turns 0x20 towards the nearest free side. It goes when it leaves the
// view without the plague, leaves the town or the map, or finds no way.
bool TownLife::rat_walk(Creature& c) {
	if (!town_.plague && (c.px() < view_[0] || c.px() > view_[0] + view_[2] || c.py() < view_[1] ||
							 c.py() > view_[1] + view_[3])) {
		return false;
	}
	const TownCell2 cell = life_pixel_cell(c.px(), c.py());
	if (!map_.inside(cell[0], cell[1])) {
		return false;
	}
	const uint32_t dt = std::min<uint32_t>(elapsed_, 1000);
	int32_t nx = c.x;
	int32_t ny = c.y;
	straight(c.dir, int32_t(hi_unsigned((c.look->speed * dt) << 12, kXPos) >> 9), nx, ny);
	const uint8_t before = c.counter;
	uint8_t f = anim_step(c, &c.counter);
	uint8_t dir = c.dir;
	bool stuck = false;
	const uint32_t p0 = c.look->params.size() > 0 ? c.look->params[0] : 70;
	const uint32_t p1 = std::max<uint32_t>(c.look->params.size() > 1 ? c.look->params[1] : 50, 1);
	if (c.counter != before) {
		if (!can_go(c, c.dir, true, true)) {
			if (c.seq != 2 && c.seq != 0x0B) {
				f = anim_start(c, 2);
			}
			c.counter = uint8_t(p0 - uint32_t(rand()) % p1);
			uint8_t turn_by = 0x20;
			for (; turn_by <= 0x80; turn_by = uint8_t(turn_by + 0x20)) {
				if (can_go(c, uint8_t(c.dir + turn_by), true, true) || can_go(c, uint8_t(c.dir - turn_by), true, true)) {
					break;
				}
			}
			if (turn_by > 0x80) {
				stuck = true;
			}
			if (!can_go(c, uint8_t(c.dir + turn_by), true, true)) {
				dir = uint8_t(dir - 0x20);
			} else if (!can_go(c, uint8_t(c.dir - turn_by), true, true)) {
				dir = uint8_t(dir + 0x20);
			} else {
				dir = uint8_t(dir + ((rand() & 1) << 6) - 0x20);
			}
		} else if (c.counter > p0) {
			c.counter = uint8_t(p0 - uint32_t(rand()) % p1);
			if ((c.counter & 1) && can_go(c, uint8_t(c.dir - 0x20), true, true)) {
				dir = uint8_t(dir - 0x20);
			} else if (can_go(c, uint8_t(c.dir + 0x20), true, true)) {
				dir = uint8_t(dir + 0x20);
			}
		}
	}
	if ((nx >> 16) == c.px() && (ny >> 16) == c.py() && f == c.frame && dir == c.dir) {
		c.x = nx;
		c.y = ny;
		return true;
	}
	if ((nx >> 16) > map_.width * kTileWidth || (ny >> 16) > map_.height * kRowStep || stuck) {
		return false;
	}
	c.x = nx;
	c.y = ny;
	c.frame = f;
	c.dir = dir;
	place(c);
	return true;
}

// An animal on a town's point (0x58B1B0, when a town is shown with the view's animations on: 10 dogs and cats, class
// 0x11, at the spawn's tag 1 points in the town; 20 wild animals, class 0x10, at its tag 0 points outside the
// walls), facing one of the eight directions at random, with a look of its class at random. One that goes is made
// again (0x593FF2, 0x59414D: `out_of_sight`): on a cell out of sight if there is one, a wild one with the same look
// and number (`look`, `id`). Its start (0x57C090, 0x57E690): where it may walk 0x55A290 (10, 0, 0, 0) for the
// town's, (20, 0, 1, 0) for the wild ones: the radius 5 or 10, off the streets' rule, inside or outside the walls;
// its walk (2) started, its first frame due at once.
bool TownLife::spawn_animal(int cls, bool out_of_sight, const LcrAnim* look, uint16_t id) {
	TownCell2 cell;
	const bool inside = cls == 0x11;
	if (people[size_t(cls)].empty() || !((out_of_sight && spawn_cell(inside, true, false, cell)) ||
											spawn_cell(inside, false, false, cell))) {
		return false;
	}
	const TownCell2 p = life_cell_pixel(cell[0], cell[1]);
	const uint8_t dir = uint8_t((rand() % 8) * 32);
	Creature c;
	c.cls = cls;
	c.mode = 7;
	c.look = look ? look : people[size_t(cls)][size_t(rand()) % people[size_t(cls)].size()];
	c.id = id ? id : new_id();
	c.street_first = false;
	c.outside = cls == 0x10;
	c.radius = cls == 0x10 ? 10 : 5;
	c.act = 2;
	c.counter = 0;
	c.frame = anim_start(c, 2);
	c.frame_due = 0;
	c.x = p[0] << 16;
	c.y = p[1] << 16;
	c.dir = c.target = dir;
	place(c);
	creatures_.push_back(std::move(c));
	return true;
}

// 0x5576C0 and the look's frames in its direction (the look's [+0x20] by the direction >> 5): whether the animal's
// look has the sequence and it can be shown facing where it faces (the even directions have the walk only, in some
// looks).
bool TownLife::animal_has(const Creature& c, uint8_t id) const {
	const int si = sequence_index(*c.look, id);
	const size_t d = size_t(c.dir >> 5);
	return si >= 0 && d < c.look->directions.size() &&
		uint32_t(c.look->sequences[size_t(si)].first) < uint32_t(c.look->directions[d].size());
}

// 0x57C240 (a dog or a cat) and 0x57E840 (a wild animal): walking (2) or running (0xB, and 0x89, 0x8A its start and
// end), with the way ahead free, it goes straight on at its look's speed (the dogs and cats running three times as
// fast, the wild animals twice); each frame of its animation, if the way ahead is taken it walks again and turns by
// 0x20 to the nearest free side (none: it goes); if not, each time its count of frames passes its look's first
// parameter it is set again to it less a random of the second and it chooses at random what to do next ([+0x3A],
// done at its sequence's end: sequence_end): walking, a turn, resting (3) or running, by what its look has. It goes
// off the map or stuck. (As its frame changes, its feet leave footprints in the snow: animal_footprints.)
bool TownLife::animal_walk(Creature& c) {
	const bool wild = c.cls == 0x10;
	int32_t nx = c.x;
	int32_t ny = c.y;
	if ((c.seq == 2 || c.seq == 0x0B || c.seq == 0x89 || c.seq == 0x8A) && can_go(c, c.dir, true, true)) {
		const uint32_t dt = std::min<uint32_t>(elapsed_, 1000);
		int32_t v = int32_t(hi_unsigned((c.look->speed * dt) << 12, kXPos) >> 9);
		if (c.seq == 0x0B) {
			v *= wild ? 2 : 3;
		}
		straight(c.dir, v, nx, ny);
	}
	const uint8_t before = c.counter;
	uint8_t f = anim_step(c, &c.counter);
	if (f != c.frame) {
		animal_footprints(c);  // 0x57C449, 0x57EA41: the frame shown before it changes.
	}
	uint8_t dir = c.dir;
	bool stuck = false;
	const uint32_t p0 = c.look->params.size() > 0 ? c.look->params[0] : 70;
	const uint32_t p1 = std::max<uint32_t>(c.look->params.size() > 1 ? c.look->params[1] : 50, 1);
	if (c.counter != before) {
		if (!can_go(c, c.dir, true, true)) {
			if (wild) {
				if (c.seq != 2) {
					f = anim_start(c, 2);
				}
				c.next_seq = 2;
				c.act = 2;
			} else {
				if (c.seq != 2 && c.seq != 0x0B) {
					f = anim_start(c, 2);
				}
				if (c.next_seq != 0x0B) {
					c.next_seq = 2;
				}
				if (c.act != 0x0B) {
					c.act = 2;
				}
			}
			c.counter = uint8_t(p0 - uint32_t(rand()) % p1);
			uint8_t turn_by = 0x20;
			for (; turn_by <= 0x80; turn_by = uint8_t(turn_by + 0x20)) {
				if (can_go(c, uint8_t(c.dir + turn_by), true, true) || can_go(c, uint8_t(c.dir - turn_by), true, true)) {
					break;
				}
			}
			if (turn_by > 0x80) {
				stuck = true;
			}
			if (!can_go(c, uint8_t(c.dir + turn_by), true, true)) {
				dir = uint8_t(dir - 0x20);
			} else if (!can_go(c, uint8_t(c.dir - turn_by), true, true)) {
				dir = uint8_t(dir + 0x20);
			} else {
				dir = uint8_t(dir + ((rand() & 1) << 6) - 0x20);
			}
		} else if (c.counter > p0) {
			const uint8_t s = c.seq;
			c.counter = uint8_t(p0 - uint32_t(rand()) % p1);
			auto turn_a_little = [&] {
				if ((c.counter & 1) && can_go(c, uint8_t(c.dir - 0x20), true, true)) {
					dir = uint8_t(dir - 0x20);
				} else if (can_go(c, uint8_t(c.dir + 0x20), true, true)) {
					dir = uint8_t(dir + 0x20);
				}
			};
			const bool rest_here = animal_has(c, 3);
			const bool run_here = animal_has(c, 0x0B);
			if (s == 2) {
				// Walking: a turn, resting or running (the wild ones resting 8 times as often).
				const int tries = (run_here ? 2 : 1) + (rest_here ? (wild ? 8 : 2) : 0);
				const int r = rand() % tries;
				if (r == 0) {
					turn_a_little();
				} else if (!rest_here || r == (wild ? 9 : 3)) {
					c.act = 0x0B;
				} else {
					c.act = 3;
				}
			} else if (s == 0x0B || s == 0x89 || s == 0x8A) {
				// Running: walking or resting.
				const int tries = rest_here ? (wild ? 9 : 3) : 1;
				c.act = rand() % tries != 0 ? 3 : 2;
			} else if (wild ? s == 3 || s == 0x43 || s == 0x63 || s == 0x85 || s == 0x8E || s == 0xC5
							: s == 3 || s == 0x85 || s == 0x87) {
				// Resting: the wild ones go on four times in five; then walking or running.
				if (wild && rand() % 5 != 0) {
					c.act = 3;
				} else {
					c.act = rand() % (run_here ? 2 : 1) != 0 ? 0x0B : 2;
				}
			}
		}
	}
	if ((nx >> 16) == c.px() && (ny >> 16) == c.py() && f == c.frame && dir == c.dir) {
		c.x = nx;
		c.y = ny;
		return true;
	}
	if (uint32_t(nx) >> 16 > uint32_t(map_.width * kTileWidth) || uint32_t(ny) >> 16 > uint32_t(map_.height * kRowStep) ||
		stuck) {
		return false;
	}
	c.x = nx;
	c.y = ny;
	c.frame = f;
	c.dir = dir;
	place(c);
	return true;
}

// 0x563D50: a bird at (x, y) (16.16, the ground under it) facing `dir`, flapping (0) from its first frame at once;
// a look of its class at random and its height its look's parameter 0 plus a random of the parameter 1 (as a byte),
// or those given (`look`, `alt`: a bird made again, 0x593E89, with its number `id`).
bool TownLife::spawn_bird(int32_t x, int32_t y, uint8_t dir, const LcrAnim* look, uint8_t alt, uint16_t id) {
	const std::vector<const LcrAnim*>& looks = people[0x0F];
	if (looks.empty()) {
		return false;
	}
	if (!look) {
		look = looks[size_t(rand()) % looks.size()];
		const uint32_t p0 = look->params.size() > 0 ? look->params[0] : 80;
		const uint32_t p1 = std::max<uint32_t>(look->params.size() > 1 ? look->params[1] : 60, 1);
		alt = uint8_t(uint32_t(rand()) % p1 + p0);
	}
	Creature c;
	c.cls = 0x0F;
	c.mode = 8;
	c.look = look;
	c.alt = alt;
	c.id = id ? id : new_id();
	c.counter = 0;
	c.frame = anim_start(c, 0);
	c.frame_due = 0;
	c.x = x;
	c.y = y;
	c.dir = c.target = dir;
	creatures_.push_back(std::move(c));
	return true;
}

// 0x563EF0: a bird flies straight on at its look's speed, across the view's (rows' step + 1) / cells' width (22/34,
// *0x6E51B0) of it up and down, the same each way on the diagonals (the game adds (1 − that) / 2 to the product
// there); each time its count of frames ([+0x2D]) passes its look's parameter 3 it is set to it less a random of
// the parameter 4: flapping, one time in two it glides (1 once, then held, 0x81 and 0xA1) if its look has those
// frames in its direction (the parameter 2 below its count), else turns 0x20 (left on an odd count); gliding, it
// flaps again (1 backwards once, 0xE1). It goes past the map's right or bottom edge (and its height, a fifth of it
// right).
bool TownLife::bird_fly(Creature& c) {
	const uint32_t dt = std::min<uint32_t>(elapsed_, 1000);
	const int32_t v = int32_t(hi_unsigned((c.look->speed * dt) << 12, kXPos) >> 9);
	constexpr float kRatio = 22.0f / 34.0f;
	const double along = double(float(v)) * double(kRatio);
	int32_t nx = c.x;
	int32_t ny = c.y;
	switch (c.dir) {
	case 0x00: ny -= int32_t(along); break;
	case 0x80: ny += int32_t(along); break;
	case 0x40: nx += v; break;
	case 0xC0: nx -= v; break;
	default: {
		const int32_t e = int32_t(along + (1.0 - double(kRatio)) / 2.0);
		switch (c.dir) {
		case 0x20: nx += e; ny -= e; break;
		case 0x60: nx += e; ny += e; break;
		case 0xA0: nx -= e; ny += e; break;
		case 0xE0: nx -= e; ny -= e; break;
		default: break;
		}
		break;
	}
	}
	const uint8_t before = c.counter;
	const uint8_t f = anim_step(c, &c.counter);
	uint8_t dir = c.dir;
	const std::vector<uint32_t>& p = c.look->params;
	const uint32_t p3 = p.size() > 3 ? p[3] : 40;
	if (c.counter != before && c.counter > p3) {
		const uint8_t s = c.seq;
		c.counter = uint8_t(p3 - uint32_t(rand()) % std::max<uint32_t>(p.size() > 4 ? p[4] : 10, 1));
		if (s == 0xA1) {
			c.next_seq = 0xE1;
		} else if (s == 0) {
			const size_t d = c.direction_index();
			const bool glides = d < c.look->directions.size() && (p.size() > 2 ? p[2] : 0) < c.look->directions[d].size() &&
				rand() % 2 != 0;
			if (glides) {
				c.next_seq = 0x81;
			} else {
				dir = uint8_t(dir + ((c.counter & 1) ? 0xE0 : 0x20));
			}
		}
	}
	if ((nx >> 16) == c.px() && (ny >> 16) == c.py() && f == c.frame && dir == c.dir) {
		c.x = nx;
		c.y = ny;
		return true;
	}
	c.x = nx;
	c.y = ny;
	c.frame = f;
	c.dir = dir;
	return uint32_t(nx) >> 16 <= map_pixels_x(map_) + c.alt / 5u && uint32_t(ny) >> 16 <= map_pixels_y(map_) + c.alt;
}

// 0x594430: a point of the map at random; one in sight (the view `margin_x`, `margin_y` pixels larger: compared
// unsigned, so near the map's top or left edge nothing is in sight) goes to the middle of the map's quarter away from
// the view.
TownCell2 TownLife::out_of_sight_point(int margin_x, int margin_y) {
	const uint32_t w = map_pixels_x(map_);
	const uint32_t h = map_pixels_y(map_);
	uint32_t x = w ? uint32_t(rand()) % w : 0;
	uint32_t y = h ? uint32_t(rand()) % h : 0;
	const uint32_t vx = uint32_t(view_[0]);
	const uint32_t vy = uint32_t(view_[1]);
	if (x >= vx - uint32_t(margin_x) && x <= vx + uint32_t(view_[2]) + uint32_t(margin_x) &&
		y >= vy - uint32_t(margin_y) && y <= vy + uint32_t(view_[3]) + uint32_t(margin_y)) {
		x = w / 4 + (vx > w / 2 ? 0 : w / 2);
		y = h / 4 + (vy > h / 2 ? 0 : h / 2);
	}
	return {int(x), int(y)};
}

// 0x58C6CB..0x58C85F, each frame (the view's animations on): with fewer fish than the town's allow (`fishing`), one
// try on a cell at random (x 4..120, y 6..213): on water, a fish (0x569350) there facing a diagonal at random, a look
// of its class at random, 3 to 5 choices to make ([+0x3A]); where it may swim 0x55A290 (20, 0, 0, 0): the radius 10;
// coming up from unseen ([+0x3B] 0, [+0x3C] 1), its swimming (2) from its first frame at once.
void TownLife::spawn_fish() {
	const std::vector<const LcrAnim*>& looks = people[0x1B];
	const uint32_t most = town_.index < 0 ? 40 : town_.fishing > 1000 ? 15 : town_.fishing > 500 ? 10 : 5;
	uint32_t fish = 0;
	for (const Creature& c : creatures_) {
		fish += c.cls == 0x1B;
	}
	if (looks.empty() || fish >= most) {
		return;
	}
	const int x = rand() % 0x75 + 4;
	const int y = rand() % 0xD0 + 6;
	if (map_.ground_at(x, y) != kWater) {
		return;
	}
	const TownCell2 p = life_cell_pixel(x, y);
	Creature c;
	c.cls = 0x1B;
	c.mode = 9;
	c.dir = c.target = uint8_t((rand() & 0xC0) + 0x20);
	c.look = looks[size_t(rand()) % looks.size()];
	c.turns_left = uint8_t(rand() % 3 + 3);
	c.radius = 10;
	c.street_first = false;
	c.outside = false;
	c.id = new_id();
	c.frame = anim_start(c, 2);
	c.frame_due = 0;
	c.x = p[0] << 16;
	c.y = p[1] << 16;
	c.alpha = 0;
	c.appearing = true;
	creatures_.push_back(std::move(c));
}

// 0x569560: a fish comes up (its [+0x3B] up by the milliseconds / 8 a frame, the remainder at random, to 255) or
// fades away (down by as much; at 0 it goes); it swims straight on at its look's speed (the rat's steps) whatever is
// ahead; at each frame of its animation, with no water ahead it turns 0x20 to the nearest side with water (none: it
// fades away); with water, each time its count of frames passes its look's parameter 0 (set again to it less a random
// of the parameter 1), one of its choices: a little turn to water (left on an odd count; none: it fades away), the
// last one fading away. It goes off the map's right or bottom edge.
bool TownLife::fish_swim(Creature& c) {
	const uint32_t dt = std::min<uint32_t>(elapsed_, 1000);
	uint8_t step = uint8_t(dt >> 3);
	if (uint32_t(rand() & 7) < (dt & 7)) {
		++step;
	}
	bool changed = false;
	if (step) {
		if (c.appearing) {
			if (c.alpha != 0xFF) {
				changed = true;
				c.alpha = 0xFF - c.alpha > step ? uint8_t(c.alpha + step) : 0xFF;
			}
		} else {
			if (c.alpha < step) {
				return false;
			}
			c.alpha = uint8_t(c.alpha - step);
			changed = true;
		}
	}
	int32_t nx = c.x;
	int32_t ny = c.y;
	straight(c.dir, int32_t(hi_unsigned((c.look->speed * dt) << 12, kXPos) >> 9), nx, ny);
	const uint8_t before = c.counter;
	const uint8_t f = anim_step(c, &c.counter);
	uint8_t dir = c.dir;
	const uint32_t p0 = c.look->params.size() > 0 ? c.look->params[0] : 100;
	const uint32_t p1 = std::max<uint32_t>(c.look->params.size() > 1 ? c.look->params[1] : 80, 1);
	if (c.counter != before) {
		if (!can_go(c, c.dir, false, true)) {
			c.counter = uint8_t(p0 - uint32_t(rand()) % p1);
			uint8_t turn_by = 0x20;
			for (; turn_by <= 0x80; turn_by = uint8_t(turn_by + 0x20)) {
				if (can_go(c, uint8_t(c.dir + turn_by), false, true) || can_go(c, uint8_t(c.dir - turn_by), false, true)) {
					break;
				}
			}
			if (turn_by > 0x80) {
				c.appearing = false;
			}
			if (c.appearing) {
				if (!can_go(c, uint8_t(c.dir + turn_by), false, true)) {
					dir = uint8_t(dir - 0x20);
				} else if (!can_go(c, uint8_t(c.dir - turn_by), false, true)) {
					dir = uint8_t(dir + 0x20);
				} else {
					dir = uint8_t(dir + ((rand() & 1) << 6) - 0x20);
				}
			}
		} else if (c.counter > p0) {
			c.counter = uint8_t(p0 - uint32_t(rand()) % p1);
			if (--c.turns_left == 0) {
				c.appearing = false;
			} else if ((c.counter & 1) && can_go(c, uint8_t(c.dir - 0x20), false, true)) {
				dir = uint8_t(dir - 0x20);
			} else if (can_go(c, uint8_t(c.dir + 0x20), false, true)) {
				dir = uint8_t(dir + 0x20);
			} else {
				c.appearing = false;
			}
		}
	}
	if ((nx >> 16) == c.px() && (ny >> 16) == c.py() && f == c.frame && dir == c.dir && !changed) {
		c.x = nx;
		c.y = ny;
		return true;
	}
	if (uint32_t(nx) >> 16 > map_pixels_x(map_) || uint32_t(ny) >> 16 > map_pixels_y(map_)) {
		return false;
	}
	c.x = nx;
	c.y = ny;
	c.frame = f;
	c.dir = dir;
	return true;
}

// 0x593D50, a creature gone (the view's animations on): a bird made again at a point out of sight (0x594430, 50
// pixels round the view) facing one of the eight directions, with its look, height and number; a wild animal on a
// cell out of the walls out of sight (else anywhere out of them) with its look and number; a dog or a cat on one in
// the town out of sight (else anywhere in it), a new one. (The fish are counted again each frame.)
void TownLife::replace(const Creature& gone) {
	if (gone.cls == 0x0F) {
		const TownCell2 p = out_of_sight_point(50, 50);
		const uint8_t dir = uint8_t((rand() % 8) * 32);
		spawn_bird(p[0] << 16, p[1] << 16, dir, gone.look, gone.alt, gone.id);
	} else if (gone.cls == 0x10 && town_.index >= 0) {
		spawn_animal(0x10, true, gone.look, gone.id);
	} else if (gone.cls == 0x11 && town_.index >= 0) {
		spawn_animal(0x11, true, nullptr, 0);
	}
}

// A seller's or the juggler's frame (0x572370, 0x56E770): his animation; the juggler, each time his count down of
// frames passes his look's first parameter (100), takes it again less a random of the second (50) and, juggling,
// turns a little (by 0x20, west on odd counts, east on even ones, not past west and east).
bool TownLife::perform(Creature& c) {
	const uint8_t before = c.counter;
	const uint8_t f = anim_step(c, c.cls == 6 ? &c.counter : nullptr);
	uint8_t dir = c.dir;
	if (c.cls == 6 && c.counter != before && c.look->params.size() >= 2 && c.counter > c.look->params[0]) {
		c.counter = uint8_t(c.look->params[0] - uint32_t(rand()) % std::max<uint32_t>(c.look->params[1], 1));
		if (c.seq == 5) {
			if ((c.counter & 1) && dir != 0x40) {
				dir = uint8_t(dir - 0x20);
			} else if (dir != 0xC0) {
				dir = uint8_t(dir + 0x20);
			}
		}
	}
	if (f != c.frame || dir != c.dir) {
		c.frame = f;
		c.dir = dir;
		place(c);
	}
	return true;
}

// 0x5963F3..0x596E75 (no siege): each group's clocks from 0; the three roads in walked a cell a round (1558 ms of a
// clock of its own) from their first points along their diagonals, and at each round a walker of the first group
// whose clock is due put on one of them at random where its walk has got to (coming in, facing the road's way; leaving,
// on the cell to its left, facing back), until each road reached its end or a gate. So the roads aren't empty.
void TownLife::prefill_roads() {
	arrive_due_ = {};
	leave_due_ = {};
	if (!roads() || town_.nodes[1].size() < 3 || town_.nodes[2].size() < 3) {
		return;
	}
	std::array<uint32_t, 4> in{};
	std::array<uint32_t, 4> out{};
	for (size_t g = 0; g < 4; ++g) {
		const int64_t r = int64_t(town_.residents[g]);
		const int64_t x = int64_t(town_.passed[g]);
		in[g] = uint32_t(std::max<int64_t>(10, r - x + 10));
		out[g] = uint32_t(std::max<int64_t>(10, x - r + 10));
	}
	std::array<TownCell2, 3> cursor{};
	std::array<TownCell2, 3> next_cell{};
	std::array<int, 3> next{};
	std::array<uint8_t, 3> dir{};
	std::array<bool, 3> done{};
	auto diagonal = [](const TownCell2& a, const TownCell2& b) -> uint8_t {
		if (a[0] < b[0]) {
			return a[1] >= b[1] ? 0x20 : 0x60;
		}
		return a[1] >= b[1] ? 0xE0 : 0xA0;
	};
	for (int r = 0; r < 3; ++r) {
		const TownPath* p = road_path(1, r);
		if (!p || !road_path(2, r)) {
			return;
		}
		cursor[size_t(r)] = life_pixel_cell((*p)[0][0], (*p)[0][1]);
		next_cell[size_t(r)] = life_pixel_cell((*p)[1][0], (*p)[1][1]);
		next[size_t(r)] = 1;
		dir[size_t(r)] = diagonal((*p)[0], (*p)[1]);
	}
	// The x and y steps of the walkers put on a road (their own sums: 31856 for the y's 31859 of set_steps).
	auto steps = [](Creature& c, uint8_t way) {
		const int32_t s = int32_t(c.look->speed);
		c.step_x = way == 0x60 || way == 0x20 ? x_pos(uint32_t(s * 181) << 4) : x_neg(-(s * 181) << 4);
		c.step_y = way == 0x60 || way == 0xA0 ? y_pos(uint32_t(s * 31856)) : y_neg(-(s * 1991) << 4);
	};
	uint32_t t = 0;
	for (int round = 0; round < 1000 && !(done[0] && done[1] && done[2]); ++round, t += 0x616) {
		for (int g = 0; g < 4; ++g) {
			if (arrive_due_[size_t(g)] <= t) {
				const int r = rand() % 3;
				if (!done[size_t(r)]) {
					Creature c = make_walker(g, 0x100, 0x100, 0x20, 0x10000u | uint32_t(r));
					if (c.look) {
						const TownCell2 m = life_cell_pixel(cursor[size_t(r)][0], cursor[size_t(r)][1]);
						c.dir = c.target = dir[size_t(r)];
						c.x = (m[0] << 16) | (c.x & 0xFFFF);
						c.y = (m[1] << 16) | (c.y & 0xFFFF);
						steps(c, dir[size_t(r)]);
						c.point = uint8_t(next[size_t(r)]);
						place(c);
						creatures_.push_back(std::move(c));
					}
				}
				arrive_due_[size_t(g)] += 2000000 / in[size_t(g)];
				break;
			}
			if (leave_due_[size_t(g)] <= t) {
				const int r = rand() % 3;
				if (!done[size_t(r)]) {
					Creature c = make_walker(g, 0x1388, 0x1388, 0x20, 0x20000u | uint32_t(r));
					if (c.look) {
						const uint8_t back = uint8_t(dir[size_t(r)] + 0x80);
						c.dir = c.target = back;
						TownCell2 lane = cursor[size_t(r)];
						life_neighbour(lane[0], lane[1], uint8_t(dir[size_t(r)] - 0x40), map_.width, map_.height);
						const TownCell2 m = life_cell_pixel(lane[0], lane[1]);
						c.x = (m[0] << 16) | (c.x & 0xFFFF);
						c.y = (m[1] << 16) | (c.y & 0xFFFF);
						steps(c, back);
						c.road = uint8_t(r);
						c.point = uint8_t(int(town_.nodes[2][size_t(r)].size()) - next[size_t(r)]);
						place(c);
						creatures_.push_back(std::move(c));
					}
				}
				leave_due_[size_t(g)] += 2000000 / out[size_t(g)];
				break;
			}
		}
		for (int r = 0; r < 3; ++r) {
			if (done[size_t(r)]) {
				continue;
			}
			TownCell2& c = cursor[size_t(r)];
			life_neighbour(c[0], c[1], dir[size_t(r)], map_.width, map_.height);
			const TownPath& p = town_.nodes[1][size_t(r)];
			if (c == next_cell[size_t(r)]) {
				const TownCell2 a = p[size_t(next[size_t(r)])];
				++next[size_t(r)];
				const int k = next[size_t(r)];
				if (k >= int(p.size()) || k >= int(town_.nodes[2][size_t(r)].size())) {
					done[size_t(r)] = true;
					continue;
				}
				const TownCell2 b = p[size_t(k)];
				next_cell[size_t(r)] = life_pixel_cell(b[0], b[1]);
				dir[size_t(r)] = diagonal(a, b);
			}
			int px = c[0];
			int py = c[1];
			life_neighbour(px, py, dir[size_t(r)], map_.width, map_.height);
			if (map_.object_at(px, py) == kGate || !map_.in(px, py)) {
				done[size_t(r)] = true;
			}
		}
	}
}

// The carts' convoys read again (a new tick; when the town is shown, `shown`): a cart made for each convoy leaving the
// town or unloading in it that has none (0x4D8F00, 0x4E3A88, 0x4E3CA9); each cart its convoy's progress (0x5708F0:
// leaving its counter, arriving 255 less it; none, 255); a leaving cart whose convoy set out goes, an arriving one
// whose convoy is done keeps its place (0x570930).
void TownLife::sync_carts(bool shown) {
	if (!roads() || town_.nodes[1].size() < 3 || town_.nodes[2].size() < 3) {
		return;
	}
	if (shown) {
		carted_.clear();
	}
	// Each convoy in the state 1 (leaving) or 6 (unloading) gets one cart, once.
	std::vector<std::pair<uint16_t, uint8_t>> made;
	for (const LifeTown::Convoy& convoy : town_.convoys) {
		if (convoy.state != 1 && convoy.state != 6) {
			continue;
		}
		const std::pair<uint16_t, uint8_t> key{convoy.event, convoy.state};
		if (std::find(carted_.begin(), carted_.end(), key) == carted_.end()) {
			make_cart(convoy);
		}
		made.push_back(key);
	}
	carted_ = std::move(made);
	for (Creature& c : creatures_) {
		if (c.mode != 6) {
			continue;
		}
		const LifeTown::Convoy* convoy = nullptr;
		for (const LifeTown::Convoy& k : town_.convoys) {
			if (int(k.event) == c.event) {
				convoy = &k;
			}
		}
		if (c.event >= 0 && (!convoy || (!c.arriving && convoy->state != 1))) {
			c.event = -1;
			if (!c.arriving) {
				c.gone_away = true;
			}
			convoy = nullptr;
		}
		c.progress = convoy ? (c.arriving ? uint8_t(0xFF - convoy->counter) : convoy->counter) : 0xFF;
		c.progress_at = now_;
	}
}

// The middle of a road's two lanes (its way in, tag 1, its point k; its way out, tag 2, the same point from its end).
TownCell2 TownLife::lane_point(const Creature& c, int k) const {
	const TownPath& in = town_.nodes[1][c.road];
	const TownPath& out = town_.nodes[2][c.road];
	const int n = int(in.size());
	if (k < 0 || k >= n || n - 1 - k >= int(out.size())) {
		return in.empty() ? TownCell2{0, 0} : in[0];
	}
	return {(out[size_t(n - 1 - k)][0] + in[size_t(k)][0]) >> 1, (out[size_t(n - 1 - k)][1] + in[size_t(k)][1]) >> 1};
}

// 0x570800: the cart's way to its next point (from where it is): one of the diagonals, a pixel across a step (0.647 up
// or down).
void TownLife::cart_way(Creature& c) const {
	const TownCell2 m = lane_point(c, c.point);
	if (m[0] > c.px()) {
		c.step_x = 0x10000;
		if (m[1] > c.py()) {
			c.way = 0x60;
			c.step_y = 0xA5A6;
		} else {
			c.way = 0x20;
			c.step_y = -0xA5A6;
		}
	} else {
		c.step_x = -0x10000;
		if (m[1] > c.py()) {
			c.way = 0xA0;
			c.step_y = 0xA5A6;
		} else {
			c.way = 0xE0;
			c.step_y = -0xA5A6;
		}
	}
}

// 0x5709C0 (`ahead`: the way from the point `from` to the one after it along the cart's road) and 0x570AB0 (not
// ahead: the way into `from` from the one before it); at the road's end, its way.
uint8_t TownLife::lane_way(const Creature& c, int from, int ahead) const {
	const int n = c.points;
	const bool up = (ahead != 0) == c.arriving;
	if ((up && from == n - 1) || (!up && from == 0)) {
		return c.way;
	}
	const TownCell2 a = lane_point(c, from);
	const TownCell2 b = lane_point(c, up ? from + 1 : from - 1);
	if (ahead) {
		if (b[0] > a[0]) {
			return a[1] < b[1] ? 0x60 : 0x20;
		}
		return a[1] < b[1] ? 0xA0 : 0xE0;
	}
	if (a[0] > b[0]) {
		return b[1] < a[1] ? 0x60 : 0x20;
	}
	return b[1] < a[1] ? 0xA0 : 0xE0;
}

// 0x56FB20: a cart for a convoy: arriving, on the middle road of the three (the one opposite the farthest apart two
// starts); leaving, on the road whose start, from the map's middle, points most towards the convoy's town on the world
// map; at the road's first point (arriving) or last, its length the sum of its lanes' middles' x steps.
void TownLife::make_cart(const LifeTown::Convoy& convoy) {
	if (people[0x1F].empty()) {
		return;
	}
	Creature c;
	c.cls = 0x1F;
	c.mode = 6;
	c.look = people[0x1F][size_t(rand()) % people[0x1F].size()];
	const TownCell2 s0 = town_.nodes[1][0][0];
	const TownCell2 s1 = town_.nodes[1][1][0];
	const TownCell2 s2 = town_.nodes[1][2][0];
	auto d2 = [](const TownCell2& a, const TownCell2& b) {
		return int64_t(a[0] - b[0]) * (a[0] - b[0]) + int64_t(a[1] - b[1]) * (a[1] - b[1]);
	};
	int middle = 2;
	int64_t far = d2(s0, s1);
	if (d2(s0, s2) > far) {
		far = d2(s0, s2);
		middle = 1;
	}
	if (d2(s1, s2) > far) {
		middle = 0;
	}
	c.event = convoy.event;
	c.arriving = int(convoy.to) == town_.index;
	if (c.arriving) {
		c.road = uint8_t(middle);
	} else {
		TownCell2 here{0, 0};
		TownCell2 there{0, 0};
		if (town_.index >= 0 && size_t(town_.index) < town_.world.size()) {
			here = town_.world[size_t(town_.index)];
		}
		if (convoy.to < town_.world.size()) {
			there = town_.world[convoy.to];
		}
		const int dx = there[0] - here[0];
		const int dy = there[1] - here[1];
		const int len = std::max(1, int(std::sqrt(double(dx) * dx + double(dy) * dy)));
		const int ux = (dx << 8) / len;
		const int uy = (dy << 8) / len;
		int64_t best = 0;
		for (int r = 0; r < 3; ++r) {
			const TownCell2 st = town_.nodes[1][size_t(r)][0];
			const int vx = st[0] - map_.width * kTileWidth / 2;
			const int vy = st[1] - map_.height * kRowStep / 2;
			const int l = std::max(1, int(std::sqrt(double(vx) * vx + double(vy) * vy)));
			const int64_t a = (vx << 8) / l + ux;
			const int64_t b = (vy << 8) / l + uy;
			if (a * a + b * b > best) {
				best = a * a + b * b;
				c.road = uint8_t(r);
			}
		}
	}
	const int n = int(town_.nodes[1][c.road].size());
	if (n < 2 || int(town_.nodes[2][c.road].size()) < n) {
		return;
	}
	c.points = uint8_t(n);
	c.last_point = uint8_t(c.arriving ? 0 : n - 1);
	c.length = 0;
	c.gone = 0;
	for (int k = 1; k < n; ++k) {
		c.length += uint32_t(std::abs(lane_point(c, k)[0] - lane_point(c, k - 1)[0]));
	}
	c.progress = 0;
	c.progress_at = now_;
	const TownPath& in = town_.nodes[1][c.road];
	const TownPath& out = town_.nodes[2][c.road];
	c.x = (out[size_t(n - 1 - c.last_point)][0] + in[c.last_point][0]) << 15;
	c.y = (out[size_t(n - 1 - c.last_point)][1] + in[c.last_point][1]) << 15;
	c.point = uint8_t(c.arriving ? c.last_point + 1 : c.last_point - 1);
	cart_way(c);
	c.dir = c.way;
	while (lane_way(c, c.point, 1) == c.way && c.point != 0 && c.point != n - 1) {
		c.point = uint8_t(c.arriving ? c.point + 1 : c.point - 1);
	}
	c.id = next_id_++;
	if (next_id_ == 0) {
		next_id_ = 1;
	}
	place(c);
	creatures_.push_back(std::move(c));
}

// 0x570BA0: the nearest cart on the road `road` to (x, y): its x distance or its y's · 17 / 11, the larger; 0xFFFF
// none.
uint16_t TownLife::cart_distance(int x, int y, uint8_t road) const {
	uint16_t nearest = 0xFFFF;
	for (const Creature& c : creatures_) {
		if (c.mode != 6 || c.road != road) {
			continue;
		}
		const uint16_t dx = uint16_t(std::abs(x - c.px()));
		const uint16_t dy = uint16_t(std::abs(y - c.py()) * 17 / 11);
		nearest = std::min(nearest, std::max(dx, dy));
	}
	return nearest;
}

// 0x570120: a cart's frame. How far it has gone is its convoy's progress (the road's length · progress / 255), and
// between two ticks it goes on by the time since the last (a 255th of the road in a tick, 3375 ms in a battle's pace);
// it moves along its lane's diagonals by that many pixels across, takes the next point's way at each, and the frames
// of its look's sequence 2 roll with the pixels gone (a round each 34); within 17 pixels of a turn, the halves of its
// turning sequences (9 and 10). At the road's end it stays while seen (out of the view by 100 pixels, it goes); a
// leaving one at the road's first point goes.
bool TownLife::cart_walk(Creature& c) {
	if (c.gone_away || !roads()) {
		return false;
	}
	const uint32_t elapsed = now_ - c.progress_at;
	const uint32_t total = c.length;
	auto per255 = [](uint32_t v) { return uint32_t(hi_unsigned(v, 0x80808081u) >> 7); };
	uint32_t extra = 0;
	if (battle_time_) {
		extra = elapsed > 3375 ? per255(total) : uint32_t(hi_unsigned(total * elapsed, 0x26FD0AD9u) >> 17);
	} else {
		extra = elapsed > ms_per_tick_ ? per255(total) : uint32_t(uint64_t(total) * elapsed / (uint64_t(ms_per_tick_) * 255));
	}
	uint32_t dist = per255(uint32_t(c.progress) * total) + extra;
	if (dist >= total) {
		if (c.px() + 100 < view_[0] || c.py() + 100 < view_[1] || c.px() > view_[0] + view_[2] + 100 ||
			c.py() > view_[1] + view_[3] + 100) {
			return false;
		}
		dist = total;
	}
	if (dist <= c.gone) {
		return true;
	}
	uint32_t delta = dist - c.gone;
	c.gone = dist;
	int32_t fx = c.x;
	int32_t fy = c.y;
	const int n = c.points;
	while (delta) {
		fx += c.step_x * int32_t(delta);
		fy += c.step_y * int32_t(delta);
		const TownCell2 m = lane_point(c, c.point);
		const bool passed = (c.step_x > 0 && (fx >> 16) >= m[0]) || (c.step_x < 0 && (fx >> 16) <= m[0]);
		if (!passed) {
			break;
		}
		if (c.point == 0) {
			return false;
		}
		if (c.point == n - 1) {
			fx = m[0] << 16;
			fy = m[1] << 16;
			break;
		}
		delta = uint32_t(std::abs((fx >> 16) - m[0]));
		fx = m[0] << 16;
		fy = m[1] << 16;
		c.last_point = c.point;
		c.point = uint8_t(c.arriving ? c.point + 1 : c.point - 1);
		cart_way(c);
		while (lane_way(c, c.point, 1) == c.way && c.point != 0 && c.point != n - 1) {
			c.point = uint8_t(c.arriving ? c.point + 1 : c.point - 1);
		}
		if (c.arriving && c.point == n - 1) {
			for (Creature& o : creatures_) {
				if (&o != &c && o.mode == 6 && o.arriving && o.event < 0) {
					o.gone_away = true;
				}
			}
		}
	}
	const LcrAnim& a = *c.look;
	auto seq = [&](int id) -> const LcrSequence* {
		const int i = sequence_index(a, id);
		return i >= 0 ? &a.sequences[size_t(i)] : nullptr;
	};
	const int x = int(fx >> 16);
	int frame = 0;
	int turn = 0;
	const int to_next = std::abs(lane_point(c, c.point)[0] - x);
	const int8_t ahead = int8_t(uint8_t(lane_way(c, c.point, 1) - c.way));
	const int from_last = std::abs(lane_point(c, c.last_point)[0] - x);
	const int8_t behind = int8_t(uint8_t(lane_way(c, c.last_point, 0) - c.way));
	if (to_next <= 0x11 && ahead != 0) {
		if (const LcrSequence* q = seq(ahead < 0 ? 10 : 9)) {
			const int half = ((q->last - q->first) >> 1) + 1;
			frame = q->first + half * (0x11 - to_next) / 18;
		}
	} else if (from_last <= 0x11 && behind != 0) {
		if (const LcrSequence* q = seq(behind < 0 ? 9 : 10)) {
			const int mid = q->first + ((q->last - q->first) >> 1) + 1;
			frame = mid + (q->last - mid + 1) * from_last / 18;
		}
		turn = behind;
	} else if (const LcrSequence* q = seq(2)) {
		frame = q->first + ((from_last + 0x11) % 0x22) * (q->last - q->first + 1) / 34;
	}
	c.x = fx;
	c.y = fy;
	c.frame = uint8_t(frame);
	c.dir = uint8_t(c.way + turn);
	place(c);
	return true;
}

// 0x596EE0: each 1558 ms, with the town's three roads in and three out: one walker comes in by a road for the first
// group whose clock is due (2 000 000 / (its residents − its passed + 10) ms, at least 10); each group whose leaving
// clock is due sends one of its free walkers out if one is near a road (2 000 000 / (passed − residents + 10) ms), and
// for the first long overdue one comes out of the town by a road; then each group keeps a walker for every 50
// residents (100 with the plague): one made out of sight, or one out of sight taken away.
void TownLife::life_tick() {
	const bool ready = roads() && town_.nodes[1].size() == 3 && town_.nodes[2].size() == 3 && town_.spawn.size() > 1 &&
		!town_.spawn[1].empty();
	if (!ready || tick_due_ >= now_) {
		return;
	}
	tick_due_ = now_ + 0x616;
	std::array<uint32_t, 4> out{};
	for (size_t g = 0; g < 4; ++g) {
		out[g] = uint32_t(std::max<int64_t>(10, int64_t(town_.passed[g]) - int64_t(town_.residents[g]) + 10));
	}
	for (int g = 0; g < 4; ++g) {
		if (arrive_due_[size_t(g)] < now_) {
			const int r = rand() % 3;
			Creature c = make_walker(g, 0x100, 0x100, 0x20, 0x10000u | uint32_t(r));
			if (c.look) {
				creatures_.push_back(std::move(c));
			}
			const int64_t rate =
				std::max<int64_t>(10, int64_t(town_.residents[size_t(g)]) - int64_t(town_.passed[size_t(g)]) + 10);
			arrive_due_[size_t(g)] = now_ + uint32_t(2000000 / rate);
			break;
		}
	}
	for (int g = 0; g < 4; ++g) {
		if (leave_due_[size_t(g)] >= now_) {
			continue;
		}
		for (Creature& c : creatures_) {
			if (c.group != g || c.mode != 0) {
				continue;
			}
			if (start_road(c, 0, false) || start_road(c, 1, false) || start_road(c, 2, false)) {
				leave_due_[size_t(g)] = now_ + 2000000 / out[size_t(g)];
				break;
			}
		}
	}
	for (int g = 0; g < 4; ++g) {
		if (leave_due_[size_t(g)] + 8000000 / (3 * out[size_t(g)]) < now_) {
			const int r = rand() % 3;
			Creature c = make_walker(g, 0x1388, 0x1388, 0, 0x20000u | uint32_t(r));
			if (c.look) {
				creatures_.push_back(std::move(c));
			}
			leave_due_[size_t(g)] = now_ + 2000000 / (4 * out[size_t(g)]);
			break;
		}
	}
	const uint32_t per = town_.plague ? 100 : 50;
	for (int g = 0; g < 4; ++g) {
		const uint32_t active = uint32_t(count(g, 0));
		const uint32_t residents = town_.residents[size_t(g)];
		if (per * active < residents) {
			TownCell2 cell;
			if (spawn_cell(true, true, true, cell)) {
				const TownCell2 p = life_cell_pixel(cell[0], cell[1]);
				const uint8_t dir = uint8_t((rand() % 4) * 64 + 0x20);
				Creature c = make_walker(g, p[0] << 16, p[1] << 16, dir, 0);
				if (c.look) {
					creatures_.push_back(std::move(c));
				}
			}
		} else if (active > 0 && per * (active - 1) > residents) {
			size_t pick = creatures_.size();
			uint32_t best = 0;
			int ties = 0;
			for (size_t i = 0; i < creatures_.size(); ++i) {
				const Creature& c = creatures_[i];
				if (c.group != g || c.mode != 0) {
					continue;
				}
				const bool seen = c.px() + 50 >= view_[0] && c.px() <= view_[0] + view_[2] + 50 &&
					c.py() + 50 >= view_[1] && c.py() <= view_[1] + view_[3] + 50;
				if (seen) {
					continue;
				}
				const uint32_t stuck = uint32_t(c.stuck);
				if (stuck > best) {
					best = stuck;
					ties = 1;
					pick = i;
				} else if (stuck == best && rand() % ++ties == 0) {
					pick = i;
				}
			}
			if (pick < creatures_.size()) {
				creatures_.erase(creatures_.begin() + std::ptrdiff_t(pick));
			}
		}
	}
	// 0x5978EC: the children: one made out of sight, or the first out of sight taken away.
	uint32_t children = 0;
	for (const Creature& c : creatures_) {
		children += c.cls == 3;
	}
	if (children < town_.children) {
		spawn_child(true);
	} else if (children > town_.children) {
		for (size_t i = 0; i < creatures_.size(); ++i) {
			const Creature& c = creatures_[i];
			if (c.cls != 3) {
				continue;
			}
			const bool seen = c.px() + 50 >= view_[0] && c.px() <= view_[0] + view_[2] + 50 &&
				c.py() + 50 >= view_[1] && c.py() <= view_[1] + view_[3] + 50;
			if (!seen) {
				creatures_.erase(creatures_.begin() + std::ptrdiff_t(i));
				break;
			}
		}
	}
	// 0x597E45: with the plague, a rat more out of sight while there are fewer than one for every 50 residents.
	if (town_.plague) {
		uint32_t rats = 0;
		for (const Creature& c : creatures_) {
			rats += c.cls == 0x1A;
		}
		if (rats < town_.total / 50) {
			spawn_rat(true, false);
		}
	}
}

void TownLife::step(uint32_t now, uint32_t elapsed, const std::array<int, 4>& view) {
	now_ = now;
	raw_elapsed_ = elapsed;
	elapsed_ = std::min<uint32_t>(elapsed, 1000);
	view_ = view;
	update_weather(false);  // 0x58F4E0 (0x58C103; P2.cfg's WEATHER 3: not at once).
	// 0x5984E0: the official comes out (the town's [0x76C] & 2, his path, no siege), facing where he will read.
	if (town_.official && official_due_ != 0 && official_due_ <= now_ && town_.nodes.size() > 3 &&
		!town_.nodes[3].empty() && town_.nodes[3][0].size() >= 2 && !people[7].empty()) {
		official_due_ = 0;
		Creature c;
		c.cls = 7;
		c.mode = 4;
		c.look = people[7][size_t(rand()) % people[7].size()];
		c.face = town_.official_face;
		c.id = next_id_++;
		if (next_id_ == 0) {
			next_id_ = 1;
		}
		c.frame = anim_start(c, 2);
		const TownCell2 start = town_.nodes[3][0][0];
		c.x = start[0] << 16;
		c.y = start[1] << 16;
		c.point = 1;
		c.back = false;
		official_target(c);
		c.dir = c.target;
		place(c);
		creatures_.push_back(std::move(c));
	}
	life_tick();
	spawn_waves();
	spawn_fish();
	spawn_floes();
	for (size_t i = 0; i < creatures_.size();) {
		Creature& c = creatures_[i];
		const bool stays = c.mode == 3 ? perform(c)
			: c.mode == 4             ? official_walk(c)
			: c.mode == 5             ? rat_walk(c)
			: c.mode == 6             ? cart_walk(c)
			: c.mode == 7             ? animal_walk(c)
			: c.mode == 8             ? bird_fly(c)
			: c.mode == 9             ? fish_swim(c)
			: c.mode == 10            ? fisher_walk(c)
			: c.mode == 11            ? true
			: c.mode == 12            ? pennant_wave(c)
			: c.mode == 13            ? water_effect_step(c)
			: c.mode == 14            ? flake_fall(c)
			: c.mode == 15            ? drop_fall(c)
			: c.mode == 16            ? fog_drift(c)
			: c.mode == 17            ? strip_effect_step(c)
			: c.mode != 0             ? road_walk(c)
									  : walk(c);
		if (stays) {
			++i;
		} else {
			const Creature gone = std::move(creatures_[i]);
			creatures_.erase(creatures_.begin() + std::ptrdiff_t(i));
			if (gone.mode == 14 || gone.mode == 15) {
				--precip_count_;  // (0x593F69: the pool's last in its place, the count down.)
			}
			replace(gone);
		}
	}
	for (Creature& c : made_) {
		creatures_.push_back(std::move(c));
	}
	made_.clear();
	spawn_precipitation();  // 0x58AA30 (0x58CBB0, after the creatures' step).
}

}  // namespace p3::data
