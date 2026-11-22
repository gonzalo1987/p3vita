// Comprueba la colocación de edificios guardados en la capa de objetos, sin datos del juego.

#include <cstdio>
#include <cstdlib>

#include "p3data/building_site.hpp"
#include "p3data/town_buildings.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FALLO: %s\n", what);
		failures++;
	}
}

TileDef tile(const char* name, uint16_t id, uint32_t type, int cols, int rows) {
	TileDef t;
	t.name = name;
	t.id = id;
	t.type = type;
	t.cols = cols;
	t.rows = rows;
	t.params = {0};
	t.slices.resize(size_t(cols + rows - 1));
	return t;
}

const TownCell& cell(const TownMap& town, int x, int y) {
	return town.objects[size_t(y) * size_t(town.width) + size_t(x)];
}

void put(TownMap& town, int x, int y, TownCell value) {
	town.objects[size_t(y) * size_t(town.width) + size_t(x)] = value;
}

// No list has records but the works [0x762] (`works`) and the burning list [0x76A] (`burning`), no fixed points.
SavedTown town_with_lists(uint16_t works, uint16_t burning) {
	SavedTown saved;
	for (size_t head = 0x760; head <= 0x76A; head += 2) {
		saved.memory.u16(head) = 0xFFFF;
	}
	saved.memory.u16(0x762) = works;
	saved.memory.u16(0x76A) = burning;
	for (size_t o = 0x7B0; o < 0x80C; ++o) {
		saved.memory.u8(o) = 0xFF;
	}
	return saved;
}

// The building sites: their change (0x550550), the rows shown (0x550770), the fade; where each cell's comes from.
void test_sites(const TileSet& tiles, const TownMap& base) {
	BuildingSite site;
	check(!site.active(), "a new site (0x550430) is no site");
	change_site(site, 45, 200);
	check(site.active() && site.works() && site.cur == 25 && site.total == 200,
		"45 days of 200: shown a tenth less, 25");
	change_site(site, 45, 200);
	check(site.cur == 25, "the same days again: the same");
	change_site(site, 15, 200);
	check(site.cur == 0, "fewer days than a tenth: 0");
	change_site(site, 200, 200);
	check(!site.active() && site.cur == 200, "its days done: the building whole");
	change_site(site, 100, 200);
	// 100 days of 200 shown as 80: of a picture 150 rows high, its 60 bottom rows; a slice 50 rows high at its bottom
	// all of them (60, cut at 50 by the drawing), one ending 100 rows above its bottom none (−40: its fade's last rows).
	check(site.cur == 80 && site_rows(site, 150, 100, 50) == 60 && site_rows(site, 150, 0, 50) == -40,
		"the rows shown: the works' share of the picture from its bottom");
	const std::vector<uint8_t> fade = site_fade(255);
	check(fade.size() == 21 && fade[0] == 243 && fade[20] == 11, "the fade: 21 rows from 243, a 22nd of 255 less each");
	check(site_fog_fade_alpha(255) == 204 && site_fade(204).size() == 21, "the fog pass's from 204 (255 / 5 · 4)");
	check(site_fade(0).empty() && site_fade(20).size() == 20, "no fade from 0; from 20, 20 rows (down to 1)");

	// A tavern (type 0x24, 200 days) with 20 days left, then on fire; the road (0x28) has no site.
	SaveGame save;
	save.towns.push_back(town_with_lists(0, 0xFFFF));
	SavedTown& saved = save.towns[0];
	saved.records = {{10, 20, 3, 0x24, 0xFFFF, 20, 0}, {30, 30, 0xFF, 0x28, 0xFFFF, 5, 0}};
	SiteProgress p;
	check(site_progress(save, 0, 0, p) && p.cur == 180 && p.total == 200 && !p.fire,
		"a building's works: its type's days less those left, of its days (0x521310)");
	check(!site_progress(save, 0, 1, p), "the road has no site");
	check(!site_progress(save, 0, kKontorSite, p), "no office of the player's: no kontor's works");
	saved.memory.u16(0x762) = 0xFFFF;
	saved.memory.u16(0x76A) = 0;
	check(site_progress(save, 0, 0, p) && p.cur == 20 && p.total == 50 && p.fire && p.at_load,
		"in the burning list: its fire");
	saved.records[0].pending = 0;
	check(site_progress(save, 0, 0, p) && p.cur == 0 && p.fire && !p.at_load,
		"burnt out: its fire with 0 left, but no call when the town is shown (0x521310)");
	saved.memory.u16(0x76A) = 0xFFFF;
	saved.memory.u16(0x762) = 0;
	check(!site_progress(save, 0, 0, p), "nothing left of its works: no site");

	// The fire (0x550550 with fire, 0x550A70's flames): 32 frames, the loop from 10, 100 ms.
	const FlameTables flames = {FlameTable{32, 10, 100}, FlameTable{32, 10, 100}};
	uint32_t rand_state = 1;
	BuildingSite burning;
	change_site_fire(burning, 49, 50, flames, rand_state);
	bool in_loop = true;
	for (const int8_t f : burning.flames) {
		in_loop = in_loop && f >= 10 && f < 32;
	}
	check(burning.active() && !burning.works() && burning.cur == 49 && in_loop,
		"a fire with 49 left: its flames at frames of their loop, no tenth taken");
	BuildingSite fresh;
	change_site_fire(fresh, 100, 50, flames, rand_state);
	check(fresh.flames[0] == 0 && fresh.flames[3] == 0 && fresh.cur == 50, "100 or more: from the first frame, 50 less");
	std::vector<FlameSmoke> smoke;
	const std::array<bool, 4> points = {true, false, true, true};
	check(step_site_flames(fresh, 1000, flames, points, rand_state, smoke) && fresh.flames[0] == 1 &&
			fresh.flames[1] == 0 && fresh.next == 1100,
		"a step: each flame with a point a frame on, the next 100 ms on");
	check(step_site_flames(fresh, 1050, flames, points, rand_state, smoke) && fresh.flames[0] == 1,
		"before its time: nothing");
	fresh.flames[0] = 8;
	fresh.flames[2] = 31;
	step_site_flames(fresh, 1200, flames, points, rand_state, smoke);
	check(fresh.flames[0] >= 10 && fresh.flames[0] < 32 && fresh.flames[2] == 10,
		"past the intro into the loop at random; past the last back to the loop's first");
	change_site_fire(fresh, 0, 50, flames, rand_state);
	check(fresh.flames[2] == -1 && fresh.active() && !step_site_flames(fresh, 2000, flames, points, rand_state, smoke),
		"0 left: burnt out (−1), no more steps");

	// Each cell of a building drawn has its record's index.
	saved.records[0].pending = 20;
	saved.memory.u16(0x762) = 0;
	saved.memory.u16(0x76A) = 0xFFFF;
	TownMap town = base;
	CellBuildings cells;
	place_saved_buildings(saved, 0, 2, tiles, nullptr, town, nullptr, &cells);
	const size_t front = size_t(20) * size_t(town.width) + 10;
	const size_t far = size_t(50) * size_t(town.width) + 50;
	check(cells.sites.size() == town.objects.size() && cells.sites[front] == 0 && cells.sites[far] == kNoSite &&
			cells.owners[front] == 3 && cells.owners[far] == kNoBuildingOwner,
		"each cell's building: its owner and its site's record");
}

// The streets the town paves (cells 0x81): a road's edge of the first kind in the ground, the tree on it gone.
void test_streets() {
	TileSet tiles;
	auto edge = [&](uint16_t id, uint32_t kind) {
		TileDef t = tile("strasse_rand", id, 0x17, 1, 1);
		t.params = {0, kind};
		tiles[id] = t;
	};
	edge(10, 3);
	edge(11, 0);
	edge(12, 1);
	edge(13, 0);
	edge(14, 0);
	tiles[20] = tile("wiese", 20, 0x0C, 1, 1);
	tiles[200] = tile("baum1_1x2", 200, 0x14, 1, 2);
	TownMap town;
	town.width = 16;
	town.height = 16;
	town.ground.assign(16 * 16, TownCell{20, 0});
	town.objects.resize(16 * 16);
	put(town, 5, 6, {200, 0x0000});  // A tree of 1 · 2 on the cell (5, 6) and the one behind it.
	put(town, 5, 5, {200, 0x0001});  // (An even row: the cell behind is above it.)
	SavedTown saved = town_with_lists(0xFFFF, 0xFFFF);
	saved.memory.s32(0x7A8) = 16;
	saved.memory.s32(0x7AC) = 16;
	saved.grid.assign(16 * 16, 0);
	saved.grid[5 * 16 + 6] = 0x81;  // (x · height + y)
	saved.grid[2 * 16 + 3] = 0x81;
	const uint64_t key = drawn_buildings_key(saved);
	place_saved_buildings(saved, 0, 2, tiles, nullptr, town);
	// The edges by number, sorted by their kind with 0x5898A9's swaps: 11, 13, 14 (the first kind's), 12, 10.
	// (5 + 1) · 6 + 5 = 41 and (2 + 1) · 3 + 2 = 11, both 2 modulo 3: the third, 14.
	const TownCell& g1 = town.ground[size_t(6) * 16 + 5];
	const TownCell& g2 = town.ground[size_t(3) * 16 + 2];
	check(g1.tile == 14 && g2.tile == 14, "a paved street: the first kind's edge by (x + 1) · y + x");
	check(town.objects[size_t(6) * 16 + 5].tile == 0 && town.objects[size_t(5) * 16 + 5].tile == 0,
		"the tree on it taken away whole");
	check(town.ground[0].tile == 20, "the other cells' ground as it was");
	saved.grid[1 * 16 + 1] = 0x81;
	saved.memory.u16(0x790) = 3;
	check(drawn_buildings_key(saved) != key, "more built streets ([0x790]) change the key");
	TownMap again;
	again.width = 16;
	again.height = 16;
	again.ground.assign(16 * 16, TownCell{20, 0});
	again.objects.resize(16 * 16);
	place_saved_buildings(saved, 0, 2, tiles, nullptr, again);
	check(again.ground[size_t(1) * 16 + 1].tile == 11, "(1 + 1) · 1 + 1 = 3, 0 modulo 3: the first, 11");
}

// A pirate nest's tower burning in a battle's view (0x515C00): drawn only while its object was there; its site's fire.
void test_nest() {
	TileSet tiles;
	TileDef tower = tile("geschuetzturm", 300, 0x29, 1, 1);
	tower.params = {0, 2, 0};  // (Its region 2 at [1], a cannon tower at [2].)
	tiles[300] = tower;
	SaveGame save;
	save.pirates[0].emplace();
	save.pirates[0]->u16(0x8) = 3;
	std::vector<GameMemory<8>>& pool = save.pirate_nests[0].pool;
	pool.resize(3);
	for (uint16_t i = 0; i < 3; ++i) {
		pool[i].u8(0x0) = uint8_t(4 + i);
		pool[i].u8(0x1) = 6;
		pool[i].u8(0x3) = 0x30;
		pool[i].u16(0x4) = uint16_t(i + 1);
	}
	auto fresh = [] {
		TownMap town;
		town.width = 16;
		town.height = 16;
		town.ground.assign(16 * 16, TownCell{});
		town.objects.resize(16 * 16);
		return town;
	};
	TownMap town = fresh();
	CellBuildings cells;
	std::vector<bool> drawn;
	place_nest_buildings(save.pirate_nests[0], 3, 2, tiles, town, &cells, 0, 0, nullptr, &drawn);
	const size_t second = size_t(6) * 16 + 5;
	check(drawn.size() == 3 && drawn[1] && cells.sites[second] == (kNestSite | 1), "a nest's towers, their sources");
	pool[1].u8(0x6) = 0xC9;  // Fallen: burning (0x515C00).
	SiteProgress p;
	check(site_progress(save, 0xFE, kNestSite | 1, p) && p.fire && p.cur == 0x31 && p.total == 50 &&
			!site_progress(save, 0xFE, kNestSite | 0, p),
		"its fire's life 0xFA − [+0x6], of 50");
	town = fresh();
	const std::vector<bool> kept = drawn;
	place_nest_buildings(save.pirate_nests[0], 3, 2, tiles, town, &cells, 0, 0, &kept, &drawn);
	check(town.objects[second].tile == 300 && drawn[1], "drawn before: kept while it burns");
	town = fresh();
	place_nest_buildings(save.pirate_nests[0], 3, 2, tiles, town, &cells, 0, 0, nullptr, &drawn);
	check(town.objects[second].tile == 0 && !drawn[1], "a battle shown anew: not placed (0x515B80)");
	pool[1].u8(0x6) = 0xFA;
	check(!site_progress(save, 0xFE, kNestSite | 1, p), "at 0xFA no more fire (taken away)");

	// The view's 0x599040 told by the world (SaveGame::building_fallen): taken once, at most 64 kept.
	for (uint32_t k = 0; k < 70; ++k) {
		save.building_fallen(9, k, 2);
	}
	const std::vector<SaveGame::FallenBuilding> fallen = save.take_fallen_buildings();
	check(fallen.size() == 64 && fallen.front().x == 6 && fallen.back().x == 69 && fallen.back().place == 9 &&
			save.take_fallen_buildings().empty(),
		"the fallen buildings: the last 64, taken once");
}

}  // namespace

int main() {
	TileSet tiles;
	tiles[100] = tile("kneipe", 100, 0x02, 2, 3);  // Taberna (tipo de edificio 0x24).
	tiles[200] = tile("baum1_1x2", 200, 20, 1, 2);  // Árbol de 1x2.

	TownMap town;
	town.width = 128;
	town.height = 226;
	town.ground.resize(128 * 226);
	town.objects.resize(128 * 226);
	// Un árbol justo donde irá la taberna y otro lejos.
	put(town, 10, 19, {200, 0x0000});
	put(town, 11, 18, {200, 0x0001});
	put(town, 50, 50, {200, 0x0000});
	put(town, 51, 49, {200, 0x0001});

	const TownMap base = town;

	SavedTown saved;
	saved.records = {{10, 20, 0xFF, 0x24, 1, 0, 0}};
	// The list heads in the town's memory: only the buildings list [0x764] has record 0.
	saved.memory.u16(0x760) = 1;
	saved.memory.u16(0x762) = 1;
	saved.memory.u16(0x764) = 0;
	saved.memory.u16(0x766) = 1;
	saved.memory.u16(0x768) = 1;
	saved.memory.u16(0x76A) = 1;
	for (size_t o = 0x7B0; o < 0x80C; ++o) {
		saved.memory.u8(o) = 0xFF;  // No fixed points.
	}

	const size_t missing = place_saved_buildings(saved, 0, 2, tiles, nullptr, town);
	check(missing == 0, "todos los edificios tienen tile");

	// Huella de 2x3 desde la celda delantera (10,20): sube 2 a la izquierda y 3 a la derecha.
	check(cell(town, 10, 20).tile == 100 && cell(town, 10, 20).aux == 1, "celda delantera con la franja 1");
	check(cell(town, 9, 19).tile == 100 && cell(town, 9, 19).aux == 0, "borde izquierdo con la franja 0");
	check(cell(town, 10, 19).tile == 100 && cell(town, 10, 19).aux == 2, "borde derecho con la franja 2");
	check(cell(town, 11, 18).tile == 100 && cell(town, 11, 18).aux == 3, "borde derecho con la franja 3");
	check(cell(town, 10, 18).tile == 100 && cell(town, 10, 18).aux == 0xFF, "celda interior sin franja");
	check(cell(town, 10, 17).tile == 100 && cell(town, 10, 17).aux == 0xFF, "celda trasera sin franja");

	size_t building_cells = 0;
	size_t tree_cells = 0;
	for (const TownCell& c : town.objects) {
		building_cells += c.tile == 100;
		tree_cells += c.tile == 200;
	}
	check(building_cells == 6, "la taberna ocupa 6 celdas");
	check(tree_cells == 2 && cell(town, 50, 50).tile == 200, "solo se quita el árbol que pisa la taberna");

	// The simulation puts a new building at the head of [0x764]: it is drawn,
	// and the key of what is drawn changes.
	const uint64_t key = drawn_buildings_key(saved);
	saved.records[0].next = 0xFFFF;  // Still the last one.
	saved.records.push_back({70, 80, 0xFF, 0x24, 0, 0, 0});
	saved.memory.u16(0x764) = 1;
	for (size_t head = 0x760; head <= 0x76A; head += 2) {
		if (head != 0x764) {
			saved.memory.u16(head) = 3;  // Past the last record: empty.
		}
	}
	const uint64_t built = drawn_buildings_key(saved);
	check(built != key, "a new building changes the key");
	TownMap again = base;
	place_saved_buildings(saved, 0, 2, tiles, nullptr, again);
	check(cell(again, 70, 80).tile == 100 && cell(again, 10, 20).tile == 100,
		"the new building and the old one are drawn");

	// A record that isn't drawn (in the free list [0x760]) doesn't change it.
	saved.records.push_back({0xFF, 0xFF, 0xFF, 0xFF, 3, 0xFF, 0xFF});
	saved.memory.u16(0x760) = 2;
	saved.records[2].param = 7;
	check(drawn_buildings_key(saved) == built, "a free record doesn't change the key");

	test_sites(tiles, base);
	test_streets();
	test_nest();

	if (failures == 0) {
		std::printf("buildings_test: todo correcto\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
