// Dibuja el mapa de una ciudad en un PNG.

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "p3data/aim.hpp"
#include "p3data/cpr.hpp"
#include "p3data/image.hpp"
#include "p3data/savegame.hpp"
#include "p3data/town.hpp"
#include "p3data/town_buildings.hpp"
#include "stb_image_write.h"

using namespace p3::data;

namespace {

int usage() {
	std::fprintf(stderr,
		"uso: p3town <archivo.cpr> <ciudad> <salida.png|salida.town> [reducción] [x y ancho alto]\n"
		"\n"
		"<ciudad> puede ser un id (iso/towns/<id>.town), <id>.editor con todos los\n"
		"edificios del diseñador, <partida>#<n> con la ciudad n de una partida del\n"
		"archivo (p. ej. missav/1.pat#3) o un .town del disco.\n"
		"Con una salida .town se escribe el mapa en vez de dibujarlo.\n"
		"Con una reducción N (por ejemplo 4) el PNG sale\n"
		"N veces más pequeño, promediando bloques de NxN píxeles. Con un rectángulo,\n"
		"solo se guarda ese recorte (en píxeles de la imagen sin reducir).\n");
	return 2;
}

Image crop(const Image& image, int x, int y, int width, int height) {
	x = std::clamp(x, 0, image.width);
	y = std::clamp(y, 0, image.height);
	width = std::clamp(width, 0, image.width - x);
	height = std::clamp(height, 0, image.height - y);
	Image out;
	out.width = width;
	out.height = height;
	out.rgba.resize(size_t(width) * height * 4);
	for (int row = 0; row < height; ++row) {
		const auto src = image.rgba.begin() + long((size_t(y + row) * image.width + x) * 4);
		std::copy(src, src + long(width) * 4, out.rgba.begin() + long(size_t(row) * width * 4));
	}
	return out;
}

// Atlas decodificados, con el magenta FF00FF como transparente.
class AtlasCache {
public:
	explicit AtlasCache(const CprArchive& archive) : archive_(archive) {}

	const Image* get(const std::string& path) {
		auto it = cache_.find(path);
		if (it != cache_.end()) {
			return it->second.width > 0 ? &it->second : nullptr;
		}
		Image& image = cache_[path];
		std::vector<uint8_t> data;
		const CprEntry* entry = archive_.find(path);
		std::string error;
		if (!entry || !archive_.read(*entry, data) || !decode_aim(data.data(), data.size(), image, &error)) {
			std::fprintf(stderr, "atlas no disponible: %s %s\n", path.c_str(), error.c_str());
			image = Image();
			return nullptr;
		}
		apply_magenta_key(image);
		bytes_ += image.rgba.size();
		return &image;
	}

	size_t count() const { return cache_.size(); }
	size_t bytes() const { return bytes_; }

private:
	const CprArchive& archive_;
	std::map<std::string, Image> cache_;
	size_t bytes_ = 0;
};

void blit(const Image& atlas, const TownSprite& sprite, Image& canvas, int offset_x, int offset_y) {
	for (int y = 0; y < sprite.height; ++y) {
		const int sy = sprite.src_y + y;
		const int dy = sprite.dst_y + offset_y + y;
		if (sy < 0 || sy >= atlas.height || dy < 0 || dy >= canvas.height) {
			continue;
		}
		for (int x = 0; x < sprite.width; ++x) {
			const int sx = sprite.src_x + x;
			const int dx = sprite.dst_x + offset_x + x;
			if (sx < 0 || sx >= atlas.width || dx < 0 || dx >= canvas.width) {
				continue;
			}
			const uint8_t* src = &atlas.rgba[(size_t(sy) * atlas.width + sx) * 4];
			uint8_t* dst = &canvas.rgba[(size_t(dy) * canvas.width + dx) * 4];
			const int alpha = src[3];
			for (int c = 0; c < 3; ++c) {
				dst[c] = uint8_t((src[c] * alpha + dst[c] * (255 - alpha)) / 255);
			}
			dst[3] = uint8_t(std::max<int>(dst[3], alpha));
		}
	}
}

Image downscale(const Image& image, int factor) {
	Image out;
	out.width = image.width / factor;
	out.height = image.height / factor;
	out.rgba.assign(size_t(out.width) * out.height * 4, 0);
	for (int y = 0; y < out.height; ++y) {
		for (int x = 0; x < out.width; ++x) {
			for (int c = 0; c < 4; ++c) {
				int sum = 0;
				for (int dy = 0; dy < factor; ++dy) {
					for (int dx = 0; dx < factor; ++dx) {
						sum += image.rgba[((size_t(y) * factor + dy) * image.width + size_t(x) * factor + dx) * 4 + c];
					}
				}
				out.rgba[(size_t(y) * out.width + x) * 4 + c] = uint8_t(sum / (factor * factor));
			}
		}
	}
	return out;
}

// Mismo formato que los .town del juego, con los 16 bytes finales a cero.
bool write_town(const TownMap& town, const std::string& path) {
	std::vector<uint8_t> out;
	auto put16 = [&](uint32_t v) {
		out.push_back(uint8_t(v));
		out.push_back(uint8_t(v >> 8));
	};
	for (const uint32_t v : {256u, uint32_t(town.width), 0u, uint32_t(town.height), 0u}) {
		put16(v);
	}
	for (const std::vector<TownCell>* layer : {&town.ground, &town.objects}) {
		for (const TownCell& cell : *layer) {
			put16(cell.tile);
			put16(cell.aux);
		}
	}
	out.resize(out.size() + 16, 0);
	std::ofstream file(path, std::ios::binary);
	file.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
	if (!file) {
		std::fprintf(stderr, "error al escribir %s\n", path.c_str());
		return false;
	}
	return true;
}

void write_to_stream(void* context, void* data, int size) {
	static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
}

}  // namespace

int main(int argc, char** argv) {
	if (argc != 4 && argc != 5 && argc != 9) {
		return usage();
	}
	const int factor = argc >= 5 ? std::atoi(argv[4]) : 1;
	if (factor < 1) {
		return usage();
	}

	std::string error;
	auto archive = CprArchive::open(argv[1], &error);
	if (!archive) {
		std::fprintf(stderr, "%s: %s\n", argv[1], error.c_str());
		return 1;
	}
	const TileSet tiles = load_tileset(*archive);
	TownMap town;
	const std::string source = argv[2];
	const size_t hash = source.find('#');
	std::ifstream town_file(argv[2], std::ios::binary);
	if (hash != std::string::npos) {
		SaveGame save;
		const size_t index = size_t(std::atoi(source.c_str() + hash + 1));
		if (!load_savegame(*archive, source.substr(0, hash), save, &error) ||
			!load_saved_town(*archive, save, index, tiles, load_town_regions(*archive), town, &error)) {
			std::fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
			return 1;
		}
		const SavedTown& saved = save.towns[index];
		std::printf("partida v%#x, año %d: ciudad %zu de %zu = id %d, plantilla %d, %u habitantes\n", save.version,
			save.year, index, save.towns.size(), saved.town_id, saved.template_map, saved.citizens);
	} else if (town_file) {
		// Un .town del disco, por ejemplo uno reconstruido desde una partida guardada.
		const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(town_file)), std::istreambuf_iterator<char>());
		if (!parse_town(bytes.data(), bytes.size(), town, &error)) {
			std::fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
			return 1;
		}
	} else {
		const bool with_buildings = std::string(argv[2]).find(".editor") != std::string::npos;
		if (!load_town(*archive, std::atoi(argv[2]), town, &error, with_buildings)) {
			std::fprintf(stderr, "%s\n", error.c_str());
			return 1;
		}
	}

	const std::string out_path = argv[3];
	if (out_path.size() > 5 && out_path.compare(out_path.size() - 5, 5, ".town") == 0) {
		return write_town(town, out_path) ? 0 : 1;
	}

	const std::vector<TownSprite> sprites = build_town_sprites(town, tiles);
	int min_x = INT_MAX;
	int min_y = INT_MAX;
	int max_x = INT_MIN;
	int max_y = INT_MIN;
	for (const TownSprite& sprite : sprites) {
		min_x = std::min(min_x, sprite.dst_x);
		min_y = std::min(min_y, sprite.dst_y);
		max_x = std::max(max_x, sprite.dst_x + sprite.width);
		max_y = std::max(max_y, sprite.dst_y + sprite.height);
	}

	Image canvas;
	canvas.width = max_x - min_x;
	canvas.height = max_y - min_y;
	canvas.rgba.assign(size_t(canvas.width) * canvas.height * 4, 0);
	AtlasCache atlases(*archive);
	for (const TownSprite& sprite : sprites) {
		if (const Image* atlas = atlases.get(sprite.tile->atlas)) {
			blit(*atlas, sprite, canvas, -min_x, -min_y);
		}
	}
	std::printf("ciudad %s: %dx%d celdas, %zu trozos de sprite, %zu atlas (%.1f MiB en RGBA)\n", argv[2],
		town.width, town.height, sprites.size(), atlases.count(), atlases.bytes() / 1048576.0);
	std::printf("imagen: %dx%d, origen de la celda (0,0) en (%d,%d)\n", canvas.width, canvas.height, -min_x, -min_y);

	if (argc == 9) {
		canvas = crop(canvas, std::atoi(argv[5]), std::atoi(argv[6]), std::atoi(argv[7]), std::atoi(argv[8]));
	}
	if (factor > 1) {
		canvas = downscale(canvas, factor);
	}
	std::ofstream file(argv[3], std::ios::binary);
	if (!stbi_write_png_to_func(write_to_stream, &file, canvas.width, canvas.height, 4, canvas.rgba.data(),
			canvas.width * 4) ||
		!file) {
		std::fprintf(stderr, "error al escribir %s\n", argv[3]);
		return 1;
	}
	return 0;
}
