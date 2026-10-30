// Dibuja en un PNG el mapa del mar de una partida: ciudades y barcos en sus
// posiciones, como base para comprobar coordenadas sin la Vita.

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/font.hpp"
#include "p3data/image.hpp"
#include "p3data/savegame.hpp"
#include "p3data/seamap.hpp"
#include "stb_image_write.h"

using namespace p3::data;

namespace {

int usage() {
	std::fprintf(stderr,
		"uso: p3map <archivo.cpr> <partida> <salida.png> [archivo de idioma.cpr]\n"
		"\n"
		"Dibuja el mapa del mar de la partida (p. ej. missav/1.pat) con las ciudades y\n"
		"los barcos que navegan. Los del jugador llevan su marca encima.\n");
	return 2;
}

bool load_image(const CprArchive& archive, const std::string& path, Image& out) {
	const CprEntry* entry = archive.find(path);
	std::vector<uint8_t> data;
	std::string error;
	if (!entry || !archive.read(*entry, data) || !decode_standard_image(data.data(), data.size(), out, &error)) {
		std::fprintf(stderr, "no se puede leer %s %s\n", path.c_str(), error.c_str());
		return false;
	}
	return true;
}

// Copia un trozo de `src` sobre `dst` con mezcla alfa.
void blit(Image& dst, const Image& src, int sx, int sy, int w, int h, int dx, int dy, const uint8_t* tint = nullptr) {
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const int tx = dx + x;
			const int ty = dy + y;
			if (tx < 0 || ty < 0 || tx >= dst.width || ty >= dst.height) {
				continue;
			}
			const uint8_t* s = &src.rgba[size_t((sy + y) * src.width + sx + x) * 4];
			uint8_t* d = &dst.rgba[size_t(ty * dst.width + tx) * 4];
			const int a = s[3];
			for (int c = 0; c < 3; ++c) {
				const int v = tint ? tint[c] : s[c];
				d[c] = uint8_t((v * a + d[c] * (255 - a)) / 255);
			}
		}
	}
}

void draw_text(Image& dst, const FontAtlas& font, const std::string& text, int x, int y, const uint8_t* color) {
	for (const FontAtlas::Quad& q : font.layout(text, x, y)) {
		blit(dst, font.image(), q.src_x, q.src_y, q.width, q.height, q.dst_x, q.dst_y, color);
	}
}

void write_to_stream(void* context, void* data, int size) {
	static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
}

}  // namespace

int main(int argc, char** argv) {
	if (argc != 4 && argc != 5) {
		return usage();
	}
	std::string error;
	auto archive = CprArchive::open(argv[1], &error);
	if (!archive) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	if (argc == 5) {
		archive->mount(argv[4]);
	}
	SaveGame save;
	if (!load_savegame(*archive, argv[2], save, &error)) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	const std::vector<SeaMapTown> towns = load_sea_map_towns(*archive);
	const std::vector<std::string> names = load_town_names(*archive);
	Image map, symbols, marker;
	if (towns.empty() || !load_image(*archive, kSeaMapImage, map) || !load_image(*archive, kTownSymbols, symbols) ||
		!load_image(*archive, kShipMarker, marker)) {
		return 1;
	}
	std::unique_ptr<FontAtlas> font = FontAtlas::load(*archive, "fonts/Tiepolo_bold.ttf", 18);

	// Ciudades: símbolo azul si el jugador tiene oficina, rojo si no.
	for (size_t i = 0; i < save.towns.size(); ++i) {
		const int id = save.towns[i].town_id();
		const SeaMapTown& t = towns[size_t(id)];
		bool kontor = false;
		for (const SavedOffice& office : save.offices) {
			kontor |= office.used() && office.owner() == save.player && office.town() == i;
		}
		const int frame = kontor ? 3 : 0;
		blit(map, symbols, (frame % 3) * kTownSymbolSize, (frame / 3) * kTownSymbolSize, kTownSymbolSize,
			kTownSymbolSize, t.symbol_x, t.symbol_y);
		if (font && id < int(names.size())) {
			static const uint8_t kInk[3] = {40, 25, 10};
			const int width = font->measure(names[size_t(id)]);
			draw_text(map, *font, names[size_t(id)], t.name_right_aligned ? t.name_x - width : t.name_x, t.name_y,
				kInk);
		}
	}

	// Barcos navegando y piratas, centrados en su posición.
	std::vector<Image> sprites(4);
	int drawn = 0;
	for (const SavedShip& ship : save.ships) {
		if (!ship.in_use() || (ship.status != 0x0F && ship.status != 0x12)) {
			continue;
		}
		Image& sheet = sprites[ship.type & 3];
		if (sheet.rgba.empty() && !load_image(*archive, kShipSprites[ship.type & 3], sheet)) {
			return 1;
		}
		const int frame = ship.heading & 15;
		const int x = ship.x >> 16;
		const int y = ship.y >> 16;
		blit(map, sheet, (frame % 4) * kShipSpriteWidth, (frame / 4) * kShipSpriteHeight, kShipSpriteWidth,
			kShipSpriteHeight, x - kShipSpriteWidth / 2, y - kShipSpriteHeight / 2);
		if (ship.owner == save.player) {
			blit(map, marker, 0, 0, marker.width, marker.height, x - marker.width / 2,
				y - kShipSpriteHeight / 2 - marker.height);
		}
		++drawn;
	}

	std::ofstream file(argv[3], std::ios::binary);
	if (!stbi_write_png_to_func(write_to_stream, &file, map.width, map.height, 4, map.rgba.data(), map.width * 4)) {
		std::fprintf(stderr, "no se puede escribir %s\n", argv[3]);
		return 1;
	}
	std::printf("%dx%d, %zu ciudades, %d barcos en el mar -> %s\n", map.width, map.height, save.towns.size(), drawn,
		argv[3]);
	return 0;
}
