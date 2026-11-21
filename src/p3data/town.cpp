#include "p3data/town.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace p3::data {

namespace {

constexpr size_t kLetHeaderSize = 0x28;
constexpr size_t kTownHeaderSize = 10;

bool fail(std::string* error, const std::string& message) {
	if (error) {
		*error = message;
	}
	return false;
}

uint16_t read_u16_le(const uint8_t* p) {
	return uint16_t(p[0] | p[1] << 8);
}

uint32_t read_u32_le(const uint8_t* p) {
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

void add_cell_sprites(const TileDef& tile, uint16_t aux, int x, int y, std::vector<TownSprite>& out) {
	const size_t slice_index = tile.slices.size() == 1 ? 0 : (aux & 0xFF);
	if (slice_index >= tile.slices.size()) {
		return;  // Celda interior de la huella (0xFF): la tapan las franjas de delante.
	}
	const TileSlice& slice = tile.slices[slice_index];

	// La franja se apoya en el borde inferior de su celda. La delantera y las de
	// su izquierda se alinean con el lado izquierdo de la celda; las de la
	// derecha, con el lado derecho.
	const int cell_x = cell_screen_x(x, y);
	const int cell_y = cell_screen_y(y);
	TownSprite sprite;
	sprite.tile = &tile;
	sprite.src_x = tile.src_x + slice.x;
	sprite.src_y = tile.src_y + slice.y;
	sprite.width = slice.width;
	sprite.height = slice.height;
	sprite.dst_x = slice_index <= tile.front_slice ? cell_x : cell_x + kTileWidth - slice.width;
	sprite.dst_y = cell_y + kTileHeight - slice.height;
	sprite.cell_x = x;
	sprite.cell_y = y;
	sprite.slice = int(slice_index);
	out.push_back(sprite);
}

void add_layer(const TownMap& town, const std::vector<TownCell>& layer, const TileSet& tiles,
	std::vector<TownSprite>& out, bool objects) {
	for (int y = 0; y < town.height; ++y) {
		for (int x = 0; x < town.width; ++x) {
			const TownCell& cell = layer[size_t(y) * size_t(town.width) + size_t(x)];
			if (cell.tile == 0) {
				continue;
			}
			auto it = tiles.find(cell.tile);
			if (it != tiles.end() && !it->second.is_editor_marker()) {
				const size_t before = out.size();
				add_cell_sprites(it->second, cell.aux, x, y, out);
				for (size_t k = before; k < out.size(); ++k) {
					out[k].object = objects;
				}
			}
		}
	}
}

}  // namespace

bool parse_let(const uint8_t* data, size_t size, TileDef& out, std::string* error) {
	if (size < kLetHeaderSize || data[1] != 'R') {
		return fail(error, "no es un tile .let");
	}
	out = TileDef();
	out.id = read_u16_le(data + 0x02);
	out.type = read_u32_le(data + 0x04);
	out.src_x = read_u16_le(data + 0x0C);
	out.src_y = read_u16_le(data + 0x10);
	out.width = read_u16_le(data + 0x14);
	out.height = read_u16_le(data + 0x18);
	out.cols = data[0x1C];
	out.rows = data[0x20];
	if (out.cols == 0 || out.rows == 0 || out.width == 0 || out.height == 0) {
		return fail(error, "tile .let vacío");
	}

	// Tras la cabecera: los parámetros, las franjas y la ruta del atlas.
	const uint32_t param_count = read_u32_le(data + 0x24);
	const size_t slice_count = size_t(out.cols + out.rows - 1);
	size_t pos = kLetHeaderSize + size_t(param_count) * 4;
	if (param_count > size || pos + slice_count * 16 + 4 > size) {
		return fail(error, "tile .let truncado");
	}
	for (uint32_t i = 0; i < param_count; ++i) {
		out.params.push_back(read_u32_le(data + kLetHeaderSize + size_t(i) * 4));
	}
	int front_bottom = -1;
	for (size_t i = 0; i < slice_count; ++i, pos += 16) {
		// Las franjas vienen en coordenadas del atlas; las pasamos a relativas.
		TileSlice slice;
		slice.x = int(int32_t(read_u32_le(data + pos))) - out.src_x;
		slice.y = int(int32_t(read_u32_le(data + pos + 4))) - out.src_y;
		slice.width = int(int32_t(read_u32_le(data + pos + 8)));
		slice.height = int(int32_t(read_u32_le(data + pos + 12)));
		if (slice.width <= 0 || slice.height <= 0) {
			return fail(error, "franja de tile inválida");
		}
		if (slice.y + slice.height > front_bottom) {
			front_bottom = slice.y + slice.height;
			out.front_slice = i;
		}
		out.slices.push_back(slice);
	}

	const uint32_t path_size = read_u32_le(data + pos);
	pos += 4;
	if (path_size == 0 || path_size > size - pos) {
		return fail(error, "ruta de atlas inválida");
	}
	const char* path = reinterpret_cast<const char*>(data + pos);
	const void* end = std::memchr(path, 0, path_size);
	out.atlas.assign(path, end ? static_cast<const char*>(end) - path : path_size);
	pos += path_size;  // (The atlas's colour key is the header's, 0x08.)

	// Its variants (0x5513F0: their count; each, 0x5852A0 and its slot 8: its kind, its parameters' count and them,
	// its x and y, its frames' count and each frame's x, y, width and height, its atlas's path's length and, with one,
	// the path and its colour key). A tail that can't be read leaves the tile without them.
	auto u32 = [&](uint32_t& v) {
		if (pos + 4 > size) {
			return false;
		}
		v = read_u32_le(data + pos);
		pos += 4;
		return true;
	};
	uint32_t count = 0;
	if (!u32(count) || count > 64) {
		return true;
	}
	std::vector<TileVariant> variants;
	for (uint32_t i = 0; i < count; ++i) {
		TileVariant v;
		uint32_t params = 0;
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t frames = 0;
		uint32_t length = 0;
		if (!u32(v.kind) || !u32(params) || params > 256) {
			return true;
		}
		for (uint32_t k = 0; k < params; ++k) {
			uint32_t p = 0;
			if (!u32(p)) {
				return true;
			}
			v.params.push_back(p);
		}
		if (!u32(x) || !u32(y) || !u32(frames) || frames > 256) {
			return true;
		}
		// (The game keeps them as 16 bits, 0x5852B7..0x5852DA, signed where it draws them, 0x585550: a smoke's y above
		// the atlas's top is negative.)
		v.x = int(int16_t(uint16_t(x))) - out.src_x;
		v.y = int(int16_t(uint16_t(y))) - out.src_y;
		for (uint32_t k = 0; k < frames; ++k) {
			uint32_t f[4] = {};
			for (uint32_t& e : f) {
				if (!u32(e)) {
					return true;
				}
			}
			v.frames.push_back({int(uint16_t(f[0])), int(uint16_t(f[1])), int(uint16_t(f[2])), int(uint16_t(f[3]))});
		}
		if (!u32(length) || length > size - pos) {
			return true;
		}
		if (length) {
			const char* at = reinterpret_cast<const char*>(data + pos);
			const void* stop = std::memchr(at, 0, length);
			v.atlas.assign(at, stop ? static_cast<const char*>(stop) - at : length);
			pos += length + 4;
		}
		variants.push_back(std::move(v));
	}
	out.variants = std::move(variants);
	return true;
}

bool parse_town(const uint8_t* data, size_t size, TownMap& out, std::string* error) {
	if (size < kTownHeaderSize || read_u16_le(data) != 256) {
		return fail(error, "no es un mapa .town");
	}
	const int width = read_u16_le(data + 2);
	const int height = read_u16_le(data + 6);
	const size_t cells = size_t(width) * size_t(height);
	if (width == 0 || height == 0 || size < kTownHeaderSize + 2 * cells * 4) {
		return fail(error, "mapa .town truncado");
	}

	out.width = width;
	out.height = height;
	std::vector<TownCell>* layers[2] = {&out.ground, &out.objects};
	const uint8_t* p = data + kTownHeaderSize;
	for (std::vector<TownCell>* layer : layers) {
		layer->resize(cells);
		for (TownCell& cell : *layer) {
			cell.tile = read_u16_le(p);
			cell.aux = read_u16_le(p + 2);
			p += 4;
		}
	}
	return true;
}

TileSet load_tileset(const CprArchive& archive) {
	TileSet tiles;
	std::vector<uint8_t> data;
	const std::string prefix = "iso/let/";
	const std::string suffix = ".let";
	for (const CprEntry& entry : archive.entries()) {
		const std::string key = normalize_path(entry.path);
		if (key.size() < prefix.size() + suffix.size() || key.compare(0, prefix.size(), prefix) != 0 ||
			key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0) {
			continue;
		}
		TileDef tile;
		if (archive.read(entry, data) && parse_let(data.data(), data.size(), tile)) {
			const size_t slash = entry.path.find_last_of("\\/");
			tile.name = entry.path.substr(slash + 1, entry.path.size() - slash - 1 - suffix.size());
			tiles.insert_or_assign(tile.id, std::move(tile));
		}
	}
	return tiles;
}

std::vector<int> list_town_ids(const CprArchive& archive) {
	std::vector<int> ids;
	const std::string prefix = "iso/towns/";
	const std::string suffix = ".town";
	for (const CprEntry& entry : archive.entries()) {
		const std::string key = normalize_path(entry.path);
		if (key.size() <= prefix.size() + suffix.size() || key.compare(0, prefix.size(), prefix) != 0 ||
			key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0) {
			continue;
		}
		const std::string name = key.substr(prefix.size(), key.size() - prefix.size() - suffix.size());
		if (name.find_first_not_of("0123456789") == std::string::npos) {
			ids.push_back(std::atoi(name.c_str()));
		}
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}

bool load_town(const CprArchive& archive, int id, TownMap& out, std::string* error, bool with_buildings) {
	const std::string path = "iso/towns/" + std::to_string(id) + (with_buildings ? ".editor" : ".town");
	const CprEntry* entry = archive.find(path);
	std::vector<uint8_t> data;
	if (!entry || !archive.read(*entry, data)) {
		return fail(error, "no se puede leer " + path);
	}
	return parse_town(data.data(), data.size(), out, error);
}

std::vector<TownSprite> build_town_sprites(const TownMap& town, const TileSet& tiles) {
	std::vector<TownSprite> sprites;
	add_layer(town, town.ground, tiles, sprites, false);
	add_layer(town, town.objects, tiles, sprites, true);
	return sprites;
}

}  // namespace p3::data
