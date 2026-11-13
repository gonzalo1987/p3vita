// The game's interface pictures from the archive (ui_images.hpp).

#include "ui_images.hpp"

#include <cstdio>
#include <string>

#include "p3data/aim.hpp"
#include "p3data/cpr.hpp"

namespace p3::app {

bool read_cpr_file(const p3::data::CprArchive& archive, const char* path, std::vector<uint8_t>& out) {
	const p3::data::CprEntry* entry = archive.find(path);
	return entry && archive.read(*entry, out);
}

bool read_cpr_image(const p3::data::CprArchive& archive, const char* path, p3::data::Image& out) {
	std::vector<uint8_t> data;
	if (!read_cpr_file(archive, path, data)) {
		return false;
	}
	// The engine's own AIM pictures (the parchment of the buildings' windows, some interiors) or a standard one.
	return p3::data::decode_standard_image(data.data(), data.size(), out) || p3::data::decode_aim(data.data(), data.size(), out);
}

std::string ini_value(const std::string& ini, const std::string& section, const char* key) {
	const std::string head = "[" + section + "]";
	const size_t at = ini.find(head);
	if (at == std::string::npos) {
		return "";
	}
	const size_t end = ini.find("\n[", at + head.size());
	const std::string wanted = std::string("\n") + key + "=";
	const size_t k = ini.find(wanted, at);
	if (k == std::string::npos || (end != std::string::npos && k > end)) {
		return "";
	}
	const size_t from = k + wanted.size();
	const size_t to = ini.find_first_of("\r\n;", from);
	std::string value = ini.substr(from, to == std::string::npos ? std::string::npos : to - from);
	while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
		value.pop_back();
	}
	const size_t first = value.find_first_not_of(" \t");
	return first == std::string::npos ? "" : value.substr(first);
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
