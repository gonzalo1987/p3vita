// Comprueba la lectura de .let y la colocación de franjas, sin datos del juego.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "p3data/town.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FALLO: %s\n", what);
		failures++;
	}
}

void put_u32(std::vector<uint8_t>& out, size_t offset, uint32_t value) {
	if (out.size() < offset + 4) {
		out.resize(offset + 4, 0);
	}
	for (int i = 0; i < 4; ++i) {
		out[offset + size_t(i)] = uint8_t(value >> (8 * i));
	}
}

// Un barril de 2x2 como 8bit_lager1.let: tres franjas, la delantera en medio.
std::vector<uint8_t> barrel_let() {
	std::vector<uint8_t> let(0x28, 0);
	let[1] = 'R';
	put_u32(let, 0x02, 513);
	put_u32(let, 0x0C, 100);  // src_x
	put_u32(let, 0x10, 200);  // src_y
	put_u32(let, 0x14, 68);
	put_u32(let, 0x18, 85);
	let[0x1C] = 2;
	let[0x20] = 2;
	put_u32(let, 0x24, 0);  // Sin lista extra.
	const int frames[3][4] = {{100, 207, 17, 67}, {117, 200, 34, 85}, {151, 221, 17, 53}};
	for (const auto& frame : frames) {
		const size_t pos = let.size();
		for (int i = 0; i < 4; ++i) {
			put_u32(let, pos + size_t(i) * 4, uint32_t(frame[i]));
		}
	}
	const char path[] = "images\\module_stadtkarte/8bit_lager1.aim";
	put_u32(let, let.size(), sizeof(path));
	let.insert(let.end(), path, path + sizeof(path));
	return let;
}

void test_let() {
	const std::vector<uint8_t> data = barrel_let();
	TileDef tile;
	std::string error;
	check(parse_let(data.data(), data.size(), tile, &error), "lee un .let");
	check(tile.id == 513 && tile.cols == 2 && tile.rows == 2, "id y huella");
	check(tile.slices.size() == 3 && tile.front_slice == 1, "tres franjas, la delantera en medio");
	check(tile.slices[0].x == 0 && tile.slices[0].y == 7, "franjas relativas al rectángulo del atlas");
	check(tile.atlas == "images\\module_stadtkarte/8bit_lager1.aim", "ruta del atlas");
	check(!parse_let(data.data(), 0x20, tile), "rechaza un .let truncado");
}

const TownSprite* find_slice(const std::vector<TownSprite>& sprites, int src_x) {
	for (const TownSprite& sprite : sprites) {
		if (sprite.src_x == src_x) {
			return &sprite;
		}
	}
	return nullptr;
}

void test_slices() {
	const std::vector<uint8_t> data = barrel_let();
	TileSet tiles;
	parse_let(data.data(), data.size(), tiles[513]);

	// Huella como en las ciudades: arriba (1,1) sin franja, izquierda (1,2),
	// derecha (2,2) y delante (1,3), con la franja en el byte bajo de aux.
	TownMap town;
	town.width = 4;
	town.height = 4;
	town.ground.resize(16);
	town.objects.resize(16);
	town.objects[1 * 4 + 1] = {513, 0x01FF};
	town.objects[2 * 4 + 1] = {513, 0x0000};
	town.objects[2 * 4 + 2] = {513, 0x0002};
	town.objects[3 * 4 + 1] = {513, 0x0101};
	const std::vector<TownSprite> sprites = build_town_sprites(town, tiles);
	check(sprites.size() == 3, "una franja por celda y ninguna en la celda trasera");

	// Las tres franjas deben formar un único sprite con la esquina en (34, -31).
	const TownSprite* left = find_slice(sprites, 100);
	const TownSprite* front = find_slice(sprites, 117);
	const TownSprite* right = find_slice(sprites, 151);
	check(front && front->dst_x == 51 && front->dst_y == -31, "franja delantera apoyada en su celda");
	check(left && left->dst_x == 34 && left->dst_y == -24, "franja izquierda alineada con el sprite");
	check(right && right->dst_x == 85 && right->dst_y == -10, "franja derecha alineada con el sprite");
}

}  // namespace

int main() {
	test_let();
	test_slices();
	if (failures == 0) {
		std::printf("town_test: todo correcto\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
