// The living town (town_life.hpp): the town people's LCR head, the towns' .spawn and .nodes, the grid's neighbours
// and a walker on a synthetic street. No game data.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "p3data/lcr.hpp"
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
	std::vector<TownCell2> start;
	for (const Creature& c : life.creatures()) {
		start.push_back({c.px(), c.py()});
	}
	for (uint32_t t = 0; t < 120000; t += 20) {
		life.step(t, 20, {0, 0, 2000, 2000});
		for (size_t i = 0; i < life.creatures().size(); ++i) {
			const Creature& c = life.creatures()[i];
			const TownCell2 cell = life_pixel_cell(c.px(), c.py());
			if (map.in(cell[0], cell[1])) {
				check(map.inside(cell[0], cell[1]) == (c.cls == 0x11), "an animal keeps to its side of the walls");
			}
			ran = ran || c.seq == 0x0B;
			rested = rested || c.seq == 3;
			moved = moved || (i < start.size() && (c.px() != start[i][0] || c.py() != start[i][1]));
		}
	}
	check(moved && ran && rested, "they walk, run and rest");
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
	if (failures) {
		std::fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	std::puts("town_life: ok");
	return 0;
}
