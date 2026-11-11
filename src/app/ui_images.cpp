// The game's interface pictures from the archive (ui_images.hpp).

#include "ui_images.hpp"

#include <cstdio>
#include <string>

#include "p3data/cpr.hpp"

namespace p3::app {

bool read_cpr_file(const p3::data::CprArchive& archive, const char* path, std::vector<uint8_t>& out) {
	const p3::data::CprEntry* entry = archive.find(path);
	return entry && archive.read(*entry, out);
}

bool read_cpr_image(const p3::data::CprArchive& archive, const char* path, p3::data::Image& out) {
	std::vector<uint8_t> data;
	return read_cpr_file(archive, path, data) && p3::data::decode_standard_image(data.data(), data.size(), out);
}

SDL_Texture* make_texture(SDL_Renderer* renderer, const p3::data::Image& image) {
	SDL_Texture* texture =
		SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
	if (texture) {
		SDL_UpdateTexture(texture, nullptr, image.rgba.data(), image.width * 4);
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	}
	return texture;
}

void read_texture_frames(const std::vector<uint8_t>& ini, const char* section, SDL_Rect* frames, int count) {
	const std::string text(ini.begin(), ini.end());
	size_t pos = text.find(section);
	if (pos == std::string::npos) {
		return;
	}
	pos = text.find('\n', pos);
	while (pos != std::string::npos && pos + 1 < text.size()) {
		const size_t start = pos + 1;
		pos = text.find('\n', start);
		std::string line = text.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
		size_t first = line.find_first_not_of(" \t");
		if (first == std::string::npos) {
			continue;
		}
		line = line.substr(first);
		if (line[0] == '[') {
			break;
		}
		int k = 0;
		int x = 0;
		int y = 0;
		int w = 0;
		int h = 0;
		if (line[0] != ';' && std::sscanf(line.c_str(), "OffsetNSize%d=%d %d %d %d", &k, &x, &y, &w, &h) == 5 &&
			k >= 0 && k < count) {
			frames[k] = {x, y, w, h};
		}
	}
}

}  // namespace p3::app
