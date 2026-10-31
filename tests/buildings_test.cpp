// Comprueba la colocación de edificios guardados en la capa de objetos, sin datos del juego.

#include <cstdio>
#include <cstdlib>

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

	if (failures == 0) {
		std::printf("buildings_test: todo correcto\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
