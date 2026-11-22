// The living town (town_life.hpp): the town people's LCR head, the towns' .spawn and .nodes, the grid's neighbours
// and a walker on a synthetic street. No game data.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "p3data/game_fixes.hpp"
#include "p3data/lcr.hpp"
#include "p3data/port_scene.hpp"
#include "p3data/savegame.hpp"
#include "p3data/tile_anims.hpp"
#include "p3data/town_life.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

void put(std::vector<uint8_t>& out, uint32_t v) {
	for (int i = 0; i < 4; ++i) {
		out.push_back(uint8_t(v >> (8 * i)));
	}
}

void put_text(std::vector<uint8_t>& out, const char* s) {
	put(out, uint32_t(std::strlen(s) + 1));
	out.insert(out.end(), s, s + std::strlen(s) + 1);
}

// A poor woman's look as arme_frau_2_neu.lcr is laid out: 8 directions, the walk (0..9) in each, the odd ones also
// standing (10..29); one strip a frame.
std::vector<uint8_t> people_lcr() {
	std::vector<uint8_t> d = {0x00, 0x06, 0x04, 0x00};
	put(d, 0);  // the class: the poor
	put(d, 0xFFFF00FF);
	put(d, 1);
	put(d, 140);  // the speed
	put(d, 8);  // the directions
	put(d, 10);  // the frames a second
	d.push_back(1);
	put(d, 0);
	put(d, 2);  // two sequences
	for (uint32_t v : {0u, 9u, 2u, 10u, 29u, 3u}) {
		put(d, v);
	}
	put_text(d, "images\\creatures/_X_shd.aim");
	for (int dir = 0; dir < 8; ++dir) {
		const int frames = dir & 1 ? 30 : 10;
		put(d, uint32_t(frames));
		for (int f = 0; f < frames; ++f) {
			put(d, uint32_t(f * 16 + 8));  // the anchor
			put(d, 30);
			for (uint32_t v : {0u, 0u, 19u, 7u, 2u, 26u}) {  // the shadow
				put(d, v);
			}
			put(d, 1);  // one strip
			for (uint32_t v : {uint32_t(f * 16), 0u, 16u, 31u}) {
				put(d, v);
			}
			for (uint32_t v : {uint32_t(f * 16), 0u, uint32_t(f * 16 + 15), 30u}) {  // the body
				put(d, v);
			}
		}
	}
	put_text(d, "images\\creatures/_X_body.aim");
	put(d, 0);
	return d;
}

void test_people_lcr() {
	const std::vector<uint8_t> d = people_lcr();
	LcrAnim a;
	std::string error;
	check(parse_lcr(d.data(), d.size(), a, &error), "the people's layout is read");
	check(a.kind == 0 && a.speed == 140 && a.fps == 10, "head");
	check(a.sequences.size() == 2 && a.sequences[1].first == 10 && a.sequences[1].last == 29 && a.sequences[1].id == 3,
		"sequences");
	check(a.shadow_image == "images/creatures/_X_shd.aim" && a.image == "images/creatures/_X_body.aim", "images");
	check(a.directions.size() == 8 && a.directions[0].size() == 10 && a.directions[1].size() == 30, "frames");
	check(a.directions[0][2].strips.size() == 1 && a.directions[0][2].strips[0].x == 32 &&
			a.directions[0][2].strips[0].h == 31,
		"strips");
}

void test_files() {
	std::vector<uint8_t> spawn;
	for (uint32_t v : {0u, 1u, 5u, 6u, 1u, 2u, 7u, 8u, 9u, 10u}) {
		put(spawn, v);
	}
	std::vector<std::vector<TownCell2>> points;
	check(parse_town_spawn(spawn.data(), spawn.size(), points), "spawn read");
	check(points.size() == 2 && points[1].size() == 2 && points[1][1][0] == 9 && points[1][1][1] == 10, "spawn groups");
	std::vector<uint8_t> nodes;
	for (uint32_t v : {1u, 1u, 2u, 17u, 1924u, 272u, 1759u}) {
		put(nodes, v);
	}
	std::vector<std::vector<TownPath>> paths;
	check(parse_town_nodes(nodes.data(), nodes.size(), paths), "nodes read");
	check(paths.size() == 2 && paths[1].size() == 1 && paths[1][0].size() == 2 && paths[1][0][1][0] == 272, "nodes");
}

void test_neighbours() {
	int x = 5;
	int y = 10;
	life_neighbour(x, y, 0x20, 128, 226);
	check(x == 5 && y == 9, "NE from an even row");
	x = 5;
	y = 9;
	life_neighbour(x, y, 0x20, 128, 226);
	check(x == 6 && y == 8, "NE from an odd row");
	x = 5;
	y = 10;
	life_neighbour(x, y, 0xA0, 128, 226);
	check(x == 4 && y == 11, "SW from an even row");
	x = 5;
	y = 9;
	life_neighbour(x, y, 0x60, 128, 226);
	check(x == 6 && y == 10, "SE from an odd row");
	const TownCell2 p = life_cell_pixel(5, 9);
	check(p[0] == 5 * 34 + 17 + 17 && p[1] == 9 * 11 + 10, "a cell's middle");
	const TownCell2 c = life_pixel_cell(p[0], p[1]);
	check(c[0] == 5 && c[1] == 9, "a middle's cell");
}

void test_walker() {
	const std::vector<uint8_t> d = people_lcr();
	LcrAnim a;
	parse_lcr(d.data(), d.size(), a);
	// A town of 20 × 40 cells, all inside the walls; a street along the cells (x, y) with x == y / 2 + 2 (going SE
	// from (2, 0), the grid's diagonal), grass elsewhere.
	LifeMap map;
	map.width = 20;
	map.height = 40;
	map.ground.assign(20 * 40, 0x28);
	map.object.assign(20 * 40, -1);
	map.grid.assign(20 * 40, 0x80);
	std::vector<TownCell2> street;
	int x = 2;
	int y = 0;
	for (int k = 0; k < 30; ++k) {
		map.ground[size_t(y) * 20 + size_t(x)] = 0x10;
		street.push_back({x, y});
		life_neighbour(x, y, 0x60, 20, 40);
	}
	LifeTown town;
	town.residents = {0, 50, 0, 0};  // one poor walker
	town.spawn.resize(2);
	town.spawn[1] = {street[5]};
	town.stalls = {street[25]};
	TownLife life;
	life.people[0] = {&a};
	life.start(map, town, 1000);
	check(life.creatures().size() == 1, "one walker for 50 poor");
	if (life.creatures().empty()) {
		return;
	}
	const Creature& w = life.creatures()[0];
	const TownCell2 start = life_pixel_cell(w.px(), w.py());
	check(map.ground[size_t(start[1]) * 20 + size_t(start[0])] == 0x10, "it starts on the street");
	const TownCell2 goal = life_cell_pixel(street[25][0], street[25][1]);
	check(w.goal_x == goal[0] && w.goal_y == goal[1], "its goal the stall");
	// Until it reaches its goal it keeps to the street (grass only when no street way is left, 0x55C820's second
	// round); there it turns about until it takes another goal.
	bool on_street = true;
	bool reached = false;
	for (uint32_t t = 1000; t < 1000 + 60000 && !reached; t += 20) {
		life.step(t, 20, {0, 0, 640, 480});
		if (life.creatures().empty()) {
			break;
		}
		const Creature& c = life.creatures()[0];
		const TownCell2 cell = life_pixel_cell(c.px(), c.py());
		on_street = on_street && map.ground[size_t(cell[1]) * 20 + size_t(cell[0])] == 0x10;
		reached = cell == street[25];
		if (std::getenv("LIFE_TRACE") && t % 400 == 0) {
			std::fprintf(stderr, "t %u px %d %d cell %d %d dir %02x target %02x wish %02x goal %d %d done %u/%u seq %02x f %u\n",
				t, c.px(), c.py(), cell[0], cell[1], c.dir, c.target, c.wish, c.goal_x, c.goal_y, c.done, c.total, c.seq,
				c.frame);
		}
	}
	check(!life.creatures().empty(), "it stays");
	check(reached, "it reaches the stall");
	check(on_street, "it keeps to the street on its way");
}

// A town of 40 × 80 cells: inside the walls the rows 40 and below, streets everywhere. Three roads in from the top
// edge going SE along the cells' diagonal, three out (the same, the other way); one poor walker should come in and
// walk free once in the town.
void test_roads() {
	const std::vector<uint8_t> d = people_lcr();
	LcrAnim a;
	parse_lcr(d.data(), d.size(), a);
	LifeMap map;
	map.width = 40;
	map.height = 80;
	map.ground.assign(40 * 80, 0x10);
	map.object.assign(40 * 80, -1);
	map.grid.assign(40 * 80, 0);
	for (int x = 0; x < 40; ++x) {
		for (int y = 40; y < 80; ++y) {
			map.grid[size_t(x) * 80 + size_t(y)] = 0x80;
		}
	}
	LifeTown town;
	town.residents = {0, 50, 0, 0};
	town.passed = {0, 0, 0, 0};
	town.spawn.resize(2);
	town.spawn[1] = {{20, 60}};
	town.nodes.resize(3);
	for (int r = 0; r < 3; ++r) {
		// From the cell (4 + 6r, 2) SE 60 rows: 30 columns on.
		const TownCell2 a0 = life_cell_pixel(4 + 6 * r, 2);
		const TownCell2 a1 = life_cell_pixel(4 + 6 * r + 15, 32);
		const TownCell2 a2 = life_cell_pixel(4 + 6 * r + 30, 62);
		town.nodes[1].push_back({a0, a1, a2});
		town.nodes[2].push_back({a2, a1, a0});
	}
	TownLife life;
	life.people[0] = {&a};
	life.start(map, town, 0);
	bool came = false;
	bool walked_free = false;
	for (uint32_t t = 0; t < 400000 && !walked_free; t += 20) {
		life.step(t, 20, {2000, 2000, 640, 480});
		for (const Creature& c : life.creatures()) {
			came = came || c.mode == 1;
			walked_free = walked_free || (came && c.mode == 0 && c.group == 1 && c.py() > 40 * 11);
		}
	}
	check(came, "a walker comes in by a road");
	check(walked_free, "it walks free in the town");
}

// With the plague a rat for every 50 residents: they run about the town and stay in it.
void test_rats() {
	std::vector<uint8_t> d = people_lcr();
	d[4] = 0x1A;
	LcrAnim a;
	parse_lcr(d.data(), d.size(), a);
	LifeMap map;
	map.width = 30;
	map.height = 60;
	map.ground.assign(30 * 60, 0x10);
	map.object.assign(30 * 60, -1);
	map.grid.assign(30 * 60, 0x80);
	LifeTown town;
	town.plague = true;
	town.total = 100;
	town.spawn.resize(2);
	town.spawn[1] = {{10, 20}, {20, 40}};
	TownLife life;
	life.people[0x1A] = {&a};
	life.start(map, town, 0);
	int rats = 0;
	for (const Creature& c : life.creatures()) {
		rats += c.cls == 0x1A;
	}
	check(rats == 2, "two rats for 100 residents with the plague");
	const int x0 = life.creatures().empty() ? 0 : life.creatures()[0].px();
	const int y0 = life.creatures().empty() ? 0 : life.creatures()[0].py();
	bool moved = false;
	for (uint32_t t = 0; t < 10000; t += 20) {
		life.step(t, 20, {0, 0, 2000, 2000});
		for (const Creature& c : life.creatures()) {
			const TownCell2 cell = life_pixel_cell(c.px(), c.py());
			check(map.inside(cell[0], cell[1]), "a rat stays in the town");
		}
		if (!life.creatures().empty()) {
			moved = moved || life.creatures()[0].px() != x0 || life.creatures()[0].py() != y0;
		}
	}
	check(moved, "a rat runs");
}

// An animal's look laid out as hund2.lcr's: 20 parameters (its count of frames 70 less up to 40, the footprints'),
// walking (2) and running (0xB) in every direction (0..9), resting (3) in the odd ones only (10..29).
std::vector<uint8_t> animal_lcr(uint32_t cls) {
	std::vector<uint8_t> d = people_lcr();
	std::vector<uint8_t> head(d.begin(), d.begin() + 0x1D);
	head[4] = uint8_t(cls);
	put(head, 20);
	for (uint32_t v : {70u, 40u, 0u, 5u, 5u, 17u, 24u, 255u, 2u, 13u, 24u, 255u, 4u, 13u, 24u, 255u, 1u, 17u, 24u, 255u}) {
		put(head, v);
	}
	put(head, 3);
	for (uint32_t v : {0u, 9u, 2u, 0u, 9u, 11u, 10u, 29u, 3u}) {
		put(head, v);
	}
	// The rest as the people's: after its head (n = 0, two sequences: 4 + 4 + 24 bytes).
	head.insert(head.end(), d.begin() + 0x1D + 32, d.end());
	return head;
}

// The town's dogs and cats in the town, the wild animals outside the walls (0x58B1B0): they walk, run and rest, keep
// to their side of the walls, and one that goes is made again (0x593D50).
void test_animals() {
	const std::vector<uint8_t> dog = animal_lcr(0x11);
	const std::vector<uint8_t> fox = animal_lcr(0x10);
	LcrAnim a;
	LcrAnim b;
	check(parse_lcr(dog.data(), dog.size(), a) && parse_lcr(fox.data(), fox.size(), b), "the animals' looks read");
	check(a.params.size() == 20 && a.params[1] == 40 && a.sequences.size() == 3 && a.sequences[1].id == 11,
		"their parameters and sequences");
	LifeMap map;
	map.width = 30;
	map.height = 80;
	map.ground.assign(30 * 80, 0x28);
	map.object.assign(30 * 80, -1);
	map.grid.assign(30 * 80, 0);
	for (int x = 0; x < 30; ++x) {
		for (int y = 40; y < 80; ++y) {
			map.grid[size_t(x) * 80 + size_t(y)] = 0x80;
		}
	}
	// Trees round the town (off the map a creature may step, as in the game, and there it goes: the wild ones do).
	for (int x = 0; x < 30; ++x) {
		for (int y = 40; y < 80; ++y) {
			if (x < 1 || x > 28 || y > 77) {
				map.object[size_t(y) * 30 + size_t(x)] = 0x2A;
			}
		}
	}
	LifeTown town;
	town.index = 0;
	town.spawn.resize(3);
	town.spawn[0] = {{10, 20}};
	town.spawn[1] = {{10, 60}};
	town.spawn[2] = {{12, 60}};
	TownLife life;
	life.people[0x11] = {&a};
	life.people[0x10] = {&b};
	// The footprints in the snow (their parameters 2, 3: small prints, the feet 5 apart; the frames 5, 17, 24...).
	LcrAnim small_left;
	LcrAnim small_right;
	for (LcrAnim* f : {&small_left, &small_right}) {
		f->kind = 0x12;
		f->fps = 1;
		f->params = {f == &small_left ? 0xC5u : 0xC6u};
		f->sequences = {{0, 0, 2}};
		LcrFrame one;
		one.body = {0, 0, 8, 16};
		f->directions.assign(8, std::vector<LcrFrame>{one});
	}
	life.footprint_looks[2] = &small_left;
	life.footprint_looks[3] = &small_right;
	life.set_snow(3);
	life.start(map, town, 0);
	int dogs = 0;
	int wild = 0;
	for (const Creature& c : life.creatures()) {
		dogs += c.cls == 0x11;
		wild += c.cls == 0x10;
	}
	check(dogs == 10 && wild == 20, "10 town animals and 20 wild ones");
	bool ran = false;
	bool rested = false;
	bool moved = false;
	size_t prints = 0;
	bool faint = true;
	std::vector<TownCell2> start;
	for (const Creature& c : life.creatures()) {
		start.push_back({c.px(), c.py()});
	}
	for (uint32_t t = 0; t < 120000; t += 20) {
		life.step(t, 20, {0, 0, 2000, 2000});
		for (size_t i = 0; i < life.creatures().size(); ++i) {
			const Creature& c = life.creatures()[i];
			const TownCell2 cell = life_pixel_cell(c.px(), c.py());
			if (map.in(cell[0], cell[1]) && (c.cls == 0x10 || c.cls == 0x11)) {
				check(map.inside(cell[0], cell[1]) == (c.cls == 0x11), "an animal keeps to its side of the walls");
			}
			ran = ran || c.seq == 0x0B;
			rested = rested || c.seq == 3;
			if (c.mode == 13 && (c.effect == 0xC5 || c.effect == 0xC6)) {
				++prints;
				faint = faint && c.alpha <= 0xB4;
			}
			moved = moved || (i < start.size() && (c.px() != start[i][0] || c.py() != start[i][1]));
		}
	}
	check(moved && ran && rested, "they walk, run and rest");
	check(prints > 0 && faint, "their small footprints in the snow, at alpha 180 at most");
	// Without the snow none more, and those left fade away within 11 s.
	life.set_snow(0);
	for (uint32_t t = 120000; t < 132000; t += 20) {
		life.step(t, 20, {0, 0, 2000, 2000});
	}
	check(std::none_of(life.creatures().begin(), life.creatures().end(), [](const Creature& c) { return c.mode == 13; }),
		"no footprints without the snow, the old ones faded");
	dogs = 0;
	wild = 0;
	for (const Creature& c : life.creatures()) {
		dogs += c.cls == 0x11;
		wild += c.cls == 0x10;
	}
	check(dogs == 10 && wild == 20, "those that go are made again");
}

// A look of `cls` with the head's parameters and sequences given, 8 directions of `frames` frames each, a shadow,
// and the strips only with `strips` (the head's byte at 0x1C: the birds' files have none).
std::vector<uint8_t> look_lcr(uint32_t cls, bool strips, const std::vector<uint32_t>& params,
	const std::vector<uint32_t>& sequences, int frames) {
	std::vector<uint8_t> d = {0x00, 0x06, 0x04, 0x00};
	put(d, cls);
	put(d, 0xFFFF00FF);
	put(d, 1);
	put(d, 300);  // the speed
	put(d, 8);
	put(d, 10);
	d.push_back(strips ? 1 : 0);
	put(d, uint32_t(params.size()));
	for (uint32_t v : params) {
		put(d, v);
	}
	put(d, uint32_t(sequences.size() / 3));
	for (uint32_t v : sequences) {
		put(d, v);
	}
	put_text(d, "images\\creatures/_X_shd.aim");
	for (int dir = 0; dir < 8; ++dir) {
		put(d, uint32_t(frames));
		for (int f = 0; f < frames; ++f) {
			put(d, uint32_t(f * 16 + 8));
			put(d, 12);
			for (uint32_t v : {0u, 0u, 9u, 5u, 2u, 10u}) {
				put(d, v);
			}
			if (strips) {
				put(d, 1);
				for (uint32_t v : {uint32_t(f * 16), 0u, 16u, 13u}) {
					put(d, v);
				}
			}
			for (uint32_t v : {uint32_t(f * 16), 0u, uint32_t(f * 16 + 15), 12u}) {
				put(d, v);
			}
		}
	}
	put_text(d, "images\\creatures/_X_body.aim");
	put(d, 0);
	return d;
}

// The birds: 20 over the map (0x58A920), flying straight on; one that leaves the map is made again out of sight with
// its height (0x593E89), so there are always 20.
void test_birds() {
	const std::vector<uint8_t> d = look_lcr(0x0F, false, {80, 60, 6, 40, 10}, {0, 5, 0, 6, 11, 1}, 12);
	LcrAnim a;
	std::string error;
	check(parse_lcr(d.data(), d.size(), a, &error), "a bird's look (no strips) read");
	check(a.params.size() == 5 && a.params[3] == 40 && a.sequences.size() == 2 && a.directions.size() == 8 &&
			a.directions[3].size() == 12 && a.directions[3][2].strips.empty() && a.directions[3][2].shadowed &&
			a.directions[3][2].body.w == 16,
		"its parameters, sequences and frames");
	LifeMap map;
	map.width = 30;
	map.height = 40;
	map.ground.assign(30 * 40, 0x28);
	map.object.assign(30 * 40, -1);
	map.grid.assign(30 * 40, 0x80);
	LifeTown town;
	TownLife life;
	life.people[0x0F] = {&a};
	life.start(map, town, 0);
	int birds = 0;
	for (const Creature& c : life.creatures()) {
		birds += c.cls == 0x0F;
		check(c.alt >= 80 && c.alt < 140, "a bird's height its parameters'");
	}
	check(birds == 20, "20 birds");
	std::vector<uint8_t> heights;
	for (const Creature& c : life.creatures()) {
		heights.push_back(c.alt);
	}
	bool moved = false;
	const int x0 = life.creatures().empty() ? 0 : life.creatures()[0].px();
	for (uint32_t t = 0; t < 300000; t += 20) {
		life.step(t, 20, {0, 0, 300, 200});
		moved = moved || (!life.creatures().empty() && life.creatures()[0].px() != x0);
	}
	birds = 0;
	std::vector<uint8_t> after;
	for (const Creature& c : life.creatures()) {
		birds += c.cls == 0x0F;
		after.push_back(c.alt);
	}
	std::sort(heights.begin(), heights.end());
	std::sort(after.begin(), after.end());
	check(moved, "they fly");
	check(birds == 20 && heights == after, "those leaving the map are made again with their heights");
}

// The fish: with the town's fishermen above 1000, up to 15 on the water, one try a frame; they come up and fade away.
void test_fish() {
	const std::vector<uint8_t> d = look_lcr(0x1B, true, {100, 80}, {0, 9, 2}, 10);
	LcrAnim a;
	check(parse_lcr(d.data(), d.size(), a), "a fish's look read");
	LifeMap map;
	map.width = 128;
	map.height = 226;
	map.ground.assign(128 * 226, 0x0E);
	map.object.assign(128 * 226, -1);
	map.grid.assign(128 * 226, 0);
	LifeTown town;
	town.index = 0;
	town.fishing = 1024;
	TownLife life;
	life.people[0x1B] = {&a};
	life.start(map, town, 0);
	size_t most = 0;
	bool seen = false;
	bool faded = false;
	for (uint32_t t = 0; t < 120000; t += 20) {
		life.step(t, 20, {0, 0, 640, 480});
		size_t fish = 0;
		for (const Creature& c : life.creatures()) {
			fish += c.cls == 0x1B;
			seen = seen || c.alpha == 0xFF;
			faded = faded || (!c.appearing && c.alpha < 0xFF);
		}
		most = std::max(most, fish);
	}
	check(most == 15, "15 fish for the fishermen's 1024");
	check(seen && faded, "they come up and fade away");
}

// The weather's tint (0x58F4E0): its byte steps; a foggy town's tint and fog banks set at once when it is shown, the
// banks drifting right; the fog gone, the tint back to none a step every 220 ms (200 and the sweep's tenths of the view a
// frame), each step swept down the view, the banks taken away at the end.
void test_weather() {
	uint32_t step = 0;
	check(tint_steps(0x00FFFFFF, 0xA0E0E1E2, 2, step) == 80 && step == 0x02FEFEFE, "the colour's steps");
	tint_split(0xFFFFFFFF, 0x00FFFFFF, 80, step);
	check(step == 0xFC000000, "the others' steps, as many");
	check(tint_move(0x10FFFFFF, 0x00FFFFFF, 0xFC000000) == 0x0CFFFFFF &&
			tint_move(0x02FFFFFF, 0x00FFFFFF, 0xFC000000) == 0x00FFFFFF,
		"a step, not past its target");
	LcrAnim fog;
	fog.kind = 0x1C;
	fog.speed = 60;
	LcrFrame frame;
	frame.body = {0, 0, 128, 128};
	fog.directions = {{frame}};
	LifeMap map;
	map.width = 128;
	map.height = 226;
	map.ground.assign(128 * 226, 0x0E);
	map.object.assign(128 * 226, -1);
	map.grid.assign(128 * 226, 0);
	LifeTown town;
	town.index = 0;
	town.weather = 1;
	TownLife life;
	life.fog_looks = {&fog};
	life.start(map, town, 1000);
	const WeatherTint& t = life.tint();
	check(t.colour == t.target && (t.colour >> 24) >= 146 && (t.colour >> 24) <= 185 && t.over == 0xFFFFFFFF &&
			t.banks == 0xFFFFFFFF && t.rect_empty() && t.texture == 100,
		"a foggy town's tint at once");
	check(std::count_if(life.fog_banks().begin(), life.fog_banks().end(), [](uint16_t id) { return id != 0; }) == 100,
		"its 100 fog banks");
	const Creature* bank = life.find(life.fog_banks()[7]);  // the row 1's third
	const int x0 = bank ? bank->px() : 0;
	check(bank && bank->py() == 2486 / 20 && x0 >= 2 * 870 - 435 && x0 < 2 * 870 + 435, "a bank's place");
	uint32_t now = 1000;
	for (; now < 11000; now += 20) {
		life.step(now, 20, {0, 0, 640, 480});
	}
	bank = life.find(life.fog_banks()[7]);
	check(bank && bank->px() - x0 >= 59 && bank->px() - x0 <= 60, "it drifts 6 pixels a second");
	town.weather = 0;
	life.update_town(town);
	const int steps = int((t.colour >> 24) + 1) / 2;
	const uint32_t from = now;
	bool swept = false;
	bool lifted = false;
	for (; now < 60000 && !lifted; now += 20) {
		life.step(now, 20, {0, 0, 640, 480});
		swept = swept || (!t.rect_empty() && t.previous != t.colour && t.rect[1] > 0 && t.rect[3] == 480);
		lifted = t.colour == 0x00FFFFFF && t.rect_empty();
	}
	check(swept, "each step swept down the view");
	check(lifted && std::abs(int(now - from) - steps * 220) <= 440, "the tint gone, a step every 220 ms");
	check(life.fog_banks()[0] == 0 &&
			std::none_of(life.creatures().begin(), life.creatures().end(), [](const Creature& c) { return c.cls == 0x1C; }),
		"the banks gone with it");
}

// A tile's animations (tile_anims.hpp): a loop of 2 parameters runs while active and hides when not; one of 5 waits
// and starts its pages at random; one of 7 goes from place to place; one of 9 repeats its page.
void test_tile_anims() {
	auto variant = [](uint32_t kind, std::vector<uint32_t> params, int frames) {
		TileVariant v;
		v.kind = kind;
		v.params = std::move(params);
		for (int f = 0; f < frames; ++f) {
			v.frames.push_back({f * 10, 0, 10, 10});
		}
		return v;
	};
	TileDef tile;
	tile.variants.push_back(variant(0, {}, 1));  // its shadow: no group
	tile.variants.push_back(variant(3, {100, 0}, 4));  // smoke
	tile.variants.push_back(variant(4, {100, 0, 1000, 2, 500}, 3));  // a sheep
	tile.variants.push_back(variant(4, {100, 0, 1000, 2, 500}, 5));
	tile.variants.push_back(variant(5, {100, 0, 0, 1, 100, 0, 1}, 3));  // a gull: from the gate (0) to a post (1)
	tile.variants.push_back(variant(5, {100, 0, 0, 1, 100, 1, 0}, 3));  // and back
	tile.variants.push_back(variant(6, {100, 0, 0, 1, 100, 0, 0, 3, 0}, 2));  // a farmer, three rounds
	std::vector<TileAnimGroup> groups = tile_anim_groups(tile);
	check(groups.size() == 4 && groups[0].kind == 2 && groups[1].kind == 5 && groups[1].pages.size() == 2 &&
			groups[2].kind == 7 && groups[3].kind == 9,
		"the groups by kind and their states by their parameters");
	if (groups.size() != 4) {
		return;
	}
	uint32_t rs = 1;
	TileAnimGroup& smoke = groups[0];
	check(smoke.shown() == nullptr, "hidden before its first step");
	step_tile_anim(smoke, 1000, true, rs);
	check(smoke.shown() != nullptr && smoke.frame == 0, "started while active");
	{
		// Under the weather's tint: the lighthouse's animations left to the fog pass with the buildings' alpha at 0x80
		// or more, the shipyard's flying gulls with any.
		TileAnims lit;
		lit.type = 0x15;
		lit.groups = {smoke};
		check(tile_anim_frames(lit).size() == 1 && tile_anim_frames(lit, 0x7F).size() == 1 &&
				tile_anim_frames(lit, 0x80).empty() && tile_anim_fog_frames(lit).size() == 1,
			"the lighthouse's fire over the tint: the fog pass's");
		TileAnims yard;
		yard.type = 0x01;
		yard.groups = {smoke, smoke, smoke};
		check(tile_anim_frames(yard).size() == 3 && tile_anim_frames(yard, 0).size() == 3 &&
				tile_anim_frames(yard, 1).size() == 2 && tile_anim_fog_frames(yard).size() == 1,
			"the shipyard's flying gulls over the tint: the fog pass's");
	}
	step_tile_anim(smoke, 1001, true, rs);
	step_tile_anim(smoke, 1250, true, rs);
	check(smoke.frame == 2, "a frame each 100 ms");
	step_tile_anim(smoke, 1500, true, rs);
	check((smoke.flags & 0x80) != 0 && smoke.frame == 3, "ended at its last frame");
	step_tile_anim(smoke, 1501, true, rs);
	check(smoke.frame == 0 && (smoke.flags & 0x80) == 0, "and again");
	step_tile_anim(smoke, 1502, true, rs);
	step_tile_anim(smoke, 1700, false, rs);
	check(smoke.shown() == nullptr, "hidden when not active");
	// The gull: whatever page it starts leaves its place, and it ends where that page goes.
	TileAnimGroup& gull = groups[2];
	uint8_t places_seen = 0;
	uint32_t t = 0;
	for (int k = 0; k < 400; ++k, t += 50) {
		const uint8_t before = gull.place;
		const bool stopped = (gull.flags & 0x80) != 0;
		step_tile_anim(gull, t, true, rs);
		if (stopped && !(gull.flags & 0x80)) {
			check(gull.pages[gull.flags & 0x3F]->params[5] == before, "a page leaving its place");
		}
		places_seen |= uint8_t(1u << gull.place);
	}
	check(places_seen == 3, "it goes from place to place");
	// The farmer: three rounds of his page before he stops.
	TileAnimGroup& farmer = groups[3];
	int rounds = 0;
	t = 0;
	for (int k = 0; k < 200 && !(rounds > 0 && (farmer.flags & 0x80)); ++k, t += 50) {
		const uint8_t mode = farmer.mode;
		step_tile_anim(farmer, t, true, rs);
		rounds += farmer.mode == 3 && mode != 3;
	}
	check(rounds == 3, "his page three times");

	// A grain farm (0x553AB0): its wife sows (her last but one page), its field grows a stage a day, its farmer
	// reaps when it is ripe (his last page), and it is bare again.
	TileDef farm;
	farm.type = 0x53;
	farm.params = {0, 1, 0};
	farm.variants.push_back(variant(3, {}, 4));  // the field's four stages
	farm.variants.push_back(variant(4, {100, 0, 0, 1, 100, 0, 1, 1, 0}, 2));  // the wife out
	farm.variants.push_back(variant(4, {100, 0, 0, 1, 100, 1, 2, 1, 0}, 2));  // she sows
	farm.variants.push_back(variant(4, {100, 0, 0, 1, 100, 2, 0, 1, 0}, 2));  // she goes in
	farm.variants.push_back(variant(5, {100, 0, 0, 1, 100, 0, 1, 1, 0}, 2));  // the farmer out
	farm.variants.push_back(variant(5, {100, 0, 0, 1, 100, 1, 0, 1, 0}, 2));  // he reaps
	TileAnims fa = make_tile_anims(farm);
	TileAnimTown town;
	std::vector<int> stages;
	t = 0;
	for (int k = 0; k < 20000; ++k, t += 50) {
		town.tick = uint32_t(k) * 4;  // a day every 64 steps
		step_tile_anims(fa, t, town, rs);
		const int stage = fa.stage == 0xFF ? -1 : int(fa.stage);
		if (stages.empty() || stages.back() != stage) {
			stages.push_back(stage);
		}
	}
	// From where it started at random, every stage in its order.
	bool order = stages.size() > 6;
	for (size_t k = 1; k < stages.size(); ++k) {
		order = order && (stages[k] == stages[k - 1] + 1 || (stages[k - 1] == 3 && stages[k] == -1));
	}
	check(order, "a grain farm: sown, growing, reaped, bare, again");
	town.active = false;
	step_tile_anims(fa, t, town, rs);
	check(fa.stage == 0xFF && tile_anim_frames(fa).empty(), "not working, bare and empty");

	// The trees (0x14): the sway's page and the winter shadow, no groups; a type sways with an atlas of its frames,
	// not with one frame's; its frame steps every its page's milliseconds while its cells are seen, each tree that
	// frame plus four times its anchor cell; the cells stepped stop the rest at 17 for that frame.
	TileDef tree;
	tree.type = 0x14;
	tree.cols = 1;
	tree.variants.push_back(variant(3, {125, 0}, 12));
	tree.variants.push_back(variant(0, {}, 1));
	tree.variants.push_back(variant(4, {}, 0));
	std::vector<TileAnims> trees(3, make_tile_anims(tree));
	check(trees[0].groups.empty() && trees[0].sway && trees[0].winter_shadow, "a tree's pages");
	check(tree_sways(trees[0], 120, 10) && !tree_sways(trees[0], 10, 10), "it sways with its frames' atlas only");
	std::vector<TileAnims*> list;
	for (TileAnims& a : trees) {
		a.sways = true;
		list.push_back(&a);
	}
	uint16_t cursor = 0;
	auto see = [&](TileAnims& a, int cells) {
		for (int k = 0; k < cells; ++k) {
			step_tile_anims(a, 0, town, rs);
		}
	};
	see(trees[0], 10);
	see(trees[1], 10);
	see(trees[2], 2);
	step_tree_sway(list, 1000, cursor);
	check(trees[0].sway_frame == 1 && trees[1].sway_frame == 1 && trees[2].sway_frame == 0 && cursor == 2 &&
			trees[0].seen == 0,
		"two types stepped, the third left for 17 cells");
	see(trees[0], 10);
	see(trees[1], 10);
	see(trees[2], 2);
	step_tree_sway(list, 1001, cursor);
	check(trees[0].sway_frame == 1 && trees[2].sway_frame == 1 && cursor == 2, "the third's turn, the others not due");
	step_tree_sway(list, 2000, cursor);
	check(trees[0].sway_frame == 1, "not seen, not stepped");
	trees[0].sway_frame = 11;
	check(tree_sway_frame(trees[0], 0) == &trees[0].sway->frames[11] &&
			tree_sway_frame(trees[0], 1) == &trees[0].sway->frames[3],
		"each tree its frame moved by its cell");
	// The anchor: a tree's second slice (cols 1) one row down, left by one from an even row.
	check(tile_anchor_cell(10 * 128 + 5, 128, 1, 1) == 11 * 128 + 4 && tile_anchor_cell(11 * 128 + 5, 128, 1, 1) ==
			12 * 128 + 5 && tile_anchor_cell(7, 128, 0, 1) == 7,
		"a slice's anchor cell");
	check(tree_season_index(2, true, 1) == 3 && tree_season_index(2, true, 11) == 1 && tree_season_index(2, false, 1) == 0 &&
			tree_season_index(3, false, 1) == 2 && tree_season_index(6, true, 2) == 2 && tree_season_index(5, false, 0) == 3,
		"a tree's picture by the season");
}

// A convoy leaving the town (the event 0x0C in the state 1): its cart starts at its road's inner end, goes out as the
// convoy's counter grows, and goes when the convoy sets out (the state 2).
void test_cart() {
	std::vector<uint8_t> d = people_lcr();
	d[4] = 0x1F;
	LcrAnim a;
	parse_lcr(d.data(), d.size(), a);
	LifeMap map;
	map.width = 40;
	map.height = 80;
	map.ground.assign(40 * 80, 0x10);
	map.object.assign(40 * 80, -1);
	map.grid.assign(40 * 80, 0x80);
	LifeTown town;
	town.index = 0;
	town.world = {{0, 0}, {-100, 0}};
	town.nodes.resize(3);
	for (int r = 0; r < 3; ++r) {
		const TownCell2 a0 = life_cell_pixel(4 + 6 * r, 2);
		const TownCell2 a1 = life_cell_pixel(4 + 6 * r + 15, 32);
		const TownCell2 a2 = life_cell_pixel(4 + 6 * r + 30, 62);
		town.nodes[1].push_back({a0, a1, a2});
		town.nodes[2].push_back({a2, a1, a0});
	}
	LifeTown::Convoy convoy;
	convoy.event = 7;
	convoy.to = 1;
	convoy.state = 1;
	convoy.counter = 0;
	convoy.here = true;
	town.convoys = {convoy};
	TownLife life;
	life.people[0x1F] = {&a};
	life.set_world_time(0, 234, false);
	life.start(map, town, 0);
	const Creature* cart = nullptr;
	for (const Creature& c : life.creatures()) {
		cart = c.cls == 0x1F ? &c : cart;
	}
	check(cart != nullptr, "a cart for the convoy");
	if (!cart) {
		return;
	}
	const int y0 = cart->py();
	life.step(100, 100, {0, 0, 2000, 2000});
	town.convoys[0].counter = 128;
	life.update_town(town);
	life.step(200, 100, {0, 0, 2000, 2000});
	const Creature* moved = nullptr;
	for (const Creature& c : life.creatures()) {
		moved = c.cls == 0x1F ? &c : moved;
	}
	check(moved && moved->py() < y0 - 100, "it goes out with the convoy's counter");
	town.convoys[0].state = 2;
	life.update_town(town);
	life.step(300, 100, {0, 0, 2000, 2000});
	bool left = true;
	for (const Creature& c : life.creatures()) {
		left = left && c.cls != 0x1F;
	}
	check(left, "it goes when the convoy sets out");
}

}  // namespace

// The fisherman's look laid out as fischer.lcr's sequences: walking (2), turning right and left (9, 10), casting (12),
// fishing (13), pulling in (15), picking up (18), 10 frames each (13: 5) in every direction; one strip a frame.
std::vector<uint8_t> fisher_lcr() {
	std::vector<uint8_t> d = {0x00, 0x06, 0x04, 0x00};
	put(d, 0x18);
	put(d, 0xFFFF00FF);
	put(d, 1);
	put(d, 140);  // the speed
	put(d, 8);  // the directions
	put(d, 10);  // the frames a second
	d.push_back(1);
	put(d, 0);
	put(d, 7);
	for (uint32_t v : {0u, 9u, 2u, 10u, 19u, 9u, 20u, 29u, 10u, 30u, 39u, 12u, 40u, 44u, 13u, 45u, 54u, 15u, 55u, 64u, 18u}) {
		put(d, v);
	}
	put_text(d, "images\\creatures/_X_shd.aim");
	for (int dir = 0; dir < 8; ++dir) {
		put(d, 65);
		for (int f = 0; f < 65; ++f) {
			put(d, 8);
			put(d, 30);
			for (uint32_t v : {0u, 0u, 19u, 7u, 2u, 26u}) {
				put(d, v);
			}
			put(d, 1);
			for (uint32_t v : {0u, 0u, 16u, 31u}) {
				put(d, v);
			}
			for (uint32_t v : {0u, 0u, 15u, 30u}) {
				put(d, v);
			}
		}
	}
	put_text(d, "images\\creatures/_X_body.aim");
	put(d, 0);
	return d;
}

// The fisherman (0x59A420, 0x569C20, 0x569E10, 0x56A0C0): none without a free path of the tag 4 starting within 304
// pixels; else on the nearest free one, walking to its end, fishing, back to its start and out again; a path taken
// isn't given back when he is taken away. And his hut (0x555B00): it asks for him when it starts working, its net and
// smoke shown while it works, and it lets him go when it stops.
void test_fisherman() {
	const std::vector<uint8_t> d = fisher_lcr();
	LcrAnim look;
	check(parse_lcr(d.data(), d.size(), look) && look.sequences.size() == 7, "the fisherman's look");
	LifeMap map;
	map.width = 30;
	map.height = 60;
	map.ground.assign(30 * 60, 0x10);
	map.object.assign(30 * 60, -1);
	map.grid.assign(30 * 60, 0x80);
	LifeTown town;
	town.nodes.resize(5);
	town.nodes[4] = {{{900, 600}, {950, 620}}, {{100, 100}, {140, 124}, {180, 148}}, {{120, 100}, {160, 124}, {200, 148}}};
	TownLife life;
	life.people[0x18] = {&look};
	life.start(map, town, 0);
	check(life.spawn_fisherman(500, 500) == 0, "no free path near");
	const uint16_t id = life.spawn_fisherman(150, 120);
	const Creature* f = nullptr;
	for (const Creature& c : life.creatures()) {
		f = c.id == id ? &c : f;
	}
	check(id != 0 && f && f->px() == 120 && f->py() == 100 && f->dir == 0x60, "on the nearest free path's start");
	bool fished = false;
	bool at_end = false;
	bool back_home = false;
	for (uint32_t t = 20; t < 200000 && !back_home; t += 20) {
		life.step(t, 20, {0, 0, 2000, 2000});
		f = nullptr;
		for (const Creature& c : life.creatures()) {
			f = c.id == id ? &c : f;
		}
		if (!f) {
			break;
		}
		at_end = at_end || (f->px() == 200 && f->py() == 148);
		fished = fished || (at_end && f->seq == 0x0D);
		back_home = fished && f->px() == 120 && f->py() == 100;
	}
	check(at_end && fished && back_home, "to his path's end, fishing, back to its start");
	const uint16_t second = life.spawn_fisherman(150, 120);
	bool on_b = false;
	for (const Creature& c : life.creatures()) {
		on_b = on_b || (c.id == second && c.px() == 100 && c.py() == 100);
	}
	check(second != 0 && on_b, "a second hut's fisherman on the next free path");
	life.remove_creature(id);
	int fishermen = 0;
	for (const Creature& c : life.creatures()) {
		fishermen += c.cls == 0x18;
	}
	check(fishermen == 1, "taken away");
	check(life.spawn_fisherman(150, 120) == 0, "his path not given back");
	// Fixed behind fix_game_bugs(): given back when he is taken away.
	set_fix_game_bugs(true);
	life.remove_creature(second);
	const uint16_t third = life.spawn_fisherman(150, 120);
	bool on_b_again = false;
	for (const Creature& c : life.creatures()) {
		on_b_again = on_b_again || (c.id == third && c.px() == 100 && c.py() == 100);
	}
	set_fix_game_bugs(false);
	check(third != 0 && on_b_again, "fixed: his path given back");

	// The hut.
	auto variant = [](uint32_t kind, std::vector<uint32_t> params, int frames) {
		TileVariant v;
		v.kind = kind;
		v.params = std::move(params);
		for (int k = 0; k < frames; ++k) {
			v.frames.push_back({k * 10, 0, 10, 10});
		}
		return v;
	};
	TileDef hut;
	hut.type = 0x57;
	hut.variants.push_back(variant(3, {80, 0}, 11));
	hut.variants.push_back(variant(4, {100, 0}, 14));
	TileAnims a = make_tile_anims(hut);
	TileAnimTown at;
	uint32_t rs = 1;
	check(a.groups.empty() && tile_anim_frames(a).empty(), "a hut as made shows nothing");
	step_tile_anims(a, 1000, at, rs);
	check(a.want_fisherman && tile_anim_frames(a).size() == 2, "working: its fisherman asked for, its net and smoke");
	a.want_fisherman = false;
	a.fisherman = 5;
	for (uint32_t t = 1000; t < 5000; t += 20) {
		step_tile_anims(a, t, at, rs);
	}
	check(!a.want_fisherman && tile_anim_frames(a).size() == 2, "round and round, one fisherman");
	at.active = false;
	step_tile_anims(a, 5000, at, rs);
	check(a.drop_fisherman && tile_anim_frames(a).empty(), "stopped: nothing shown, its fisherman let go");
}

// The port's scene (port_scene.hpp): the player's ships on the first free slot, the others' on the last; a ship coming
// in on its path, docked at its end; one going out keeps its slot; one gone to sea leaves it; on a straight path a
// ship going out starts at the quay and one coming in comes from the sea.
void test_port_scene() {
	SaveGame save;
	save.globals.u16(0x6DD894) = 5;  // ships
	save.globals.u16(0x6DD896) = 0;  // convoys
	save.globals.u16(0x6DD888) = 0;  // the docked list
	save.globals.u16(0x6DD88C) = 0xFFFF;
	save.globals.u32(0x6DFC14) = 0;  // the player
	save.globals.u32(0x6DFBC4) = 300;  // ms a tick
	save.towns.resize(1);
	save.towns[0].memory.u16(0x81E) = 0xFFFF;
	save.towns[0].memory.u16(0x820) = 0xFFFF;
	save.ships.resize(5);
	auto ship = [&](int i, int owner, int town, int state, int counter, int next) {
		ShipMemory& m = save.ships[size_t(i)].memory;
		m.u8(0x0) = uint8_t(owner);
		m.u16(0x6) = uint16_t(next);
		m.u16(0x8) = 0xFFFF;
		m.u8(0xE) = 2;
		m.u8(0x39) = uint8_t(town);
		m.u16(0x134) = uint16_t(state);
		m.u16(0x138) = uint16_t(counter);
	};
	ship(0, 0, 0, 0, 0, 1);  // the player's, docked
	ship(1, 5, 0, 0, 0, 2);  // another's, docked
	ship(2, 5, 0, 3, 0x10, 0xFFFF);  // another's, coming in
	ship(3, 5, 0xFF, 0xF, 0, 0xFFFF);  // at sea
	ship(4, 0, 0xFF, 0xF, 0, 0xFFFF);
	std::vector<std::vector<std::array<int, 2>>> paths;
	for (int k = 0; k < 8; ++k) {
		paths.push_back({{100 + 60 * k, 10}, {100 + 60 * k, 300}, {100 + 60 * k, 500}});
	}
	PortScene scene;
	scene.show(save, 0, paths, 1000);
	auto slot_of = [&](int s) {
		for (uint16_t slot : scene.order()) {
			if (scene.at(slot).ship == s) {
				return int(slot);
			}
		}
		return -1;
	};
	check(slot_of(0) == 0 && slot_of(1) == 7 && slot_of(2) == 6 && slot_of(3) == -1, "the slots by owner");
	const PortShip& own = scene.at(0);
	check(own.state == 0 && own.place.x == 100 && own.place.y == port_scale_y(500) && own.place.dir == 0x80 &&
			own.place.sails == 0,
		"docked at its path's end, its sails furled");
	check(scene.at(6).state == 1 && scene.at(6).place.y < port_scale_y(500) && scene.at(6).place.sails == 4,
		"coming in on its path");
	// A tick: the ship coming in docked, the other docked one going out, one at sea arrives.
	save.ships[2].memory.u16(0x134) = 0;
	save.ships[2].memory.u16(0x138) = 0x40;
	save.ships[1].memory.u16(0x134) = 2;
	save.ships[3].memory.u8(0x39) = 0;
	save.ships[3].memory.u16(0x134) = 3;
	scene.tick(save, 1300);
	scene.frame(save, 1300);
	check(scene.at(6).state == 0 && scene.at(7).state == 8 && slot_of(3) == 5 && scene.at(5).state == 1,
		"docked, going out, another coming in");
	save.ships[1].memory.u8(0x39) = 0xFF;
	save.ships[1].memory.u16(0x134) = 0xF;
	scene.tick(save, 1600);
	check(slot_of(1) == -1, "gone to sea, its slot freed");
	// A straight path: going out from the quay, coming in from the sea.
	const std::vector<std::array<int, 2>> line = {{500, 0}, {500, 400}, {500, 800}};
	PortPlace o;
	port_place_on_path(port_path(line, false), 0, true, o);
	check(o.x == 500 && o.y == port_scale_y(800) && o.dir == 0 && o.sails == 0, "going out from the quay");
	port_place_on_path(port_path(line, true), 0x800, false, o);
	check(o.x == 500 && o.y < 0 && o.dir == 0x80 && o.sails == 4, "coming in from beyond the sea's edge");
}

int main() {
	test_people_lcr();
	test_files();
	test_neighbours();
	test_walker();
	test_roads();
	test_rats();
	test_cart();
	test_animals();
	test_birds();
	test_fish();
	test_weather();
	test_tile_anims();
	test_fisherman();
	test_port_scene();
	if (failures) {
		std::fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	std::puts("town_life: ok");
	return 0;
}
