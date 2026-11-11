// The viewer's start screen: the game's main menu (start_screen.hpp).

#include "start_screen.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "p3data/cpr.hpp"
#include "p3data/image.hpp"

namespace p3::app {

namespace {

using p3::data::CprArchive;
using p3::data::CprEntry;
using p3::data::Image;

// Where the game draws each lit entry on the book (800 · 600): the tables of
// its exes (English GOG 1.1 0x6970D8, Spanish 0x696978: u32 x, y).
constexpr SDL_Point kEnglishPlaces[kStartEntries] = {
	{235, 185}, {208, 211}, {180, 239}, {148, 268}, {115, 300}, {80, 334}, {41, 372}, {0, 412}};
constexpr SDL_Point kSpanishPlaces[kStartEntries] = {
	{223, 183}, {197, 207}, {167, 235}, {135, 265}, {103, 297}, {64, 332}, {24, 368}, {0, 409}};

bool read_file(const CprArchive& archive, const char* path, std::vector<uint8_t>& out) {
	const CprEntry* entry = archive.find(path);
	return entry && archive.read(*entry, out);
}

bool read_image(const CprArchive& archive, const char* path, Image& out) {
	std::vector<uint8_t> data;
	return read_file(archive, path, data) && p3::data::decode_standard_image(data.data(), data.size(), out);
}

SDL_Texture* make_texture(SDL_Renderer* renderer, const Image& image) {
	SDL_Texture* texture =
		SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
	if (texture) {
		SDL_UpdateTexture(texture, nullptr, image.rgba.data(), image.width * 4);
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	}
	return texture;
}

// The frames «OffsetNSizeK=x y w h» of a section «[TEXn]» of textures.ini (the
// lines with a «;» before are left out).
void read_frames(const std::vector<uint8_t>& ini, const char* section, SDL_Rect* frames, int count) {
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

}  // namespace

StartScreen::StartScreen(SDL_Renderer* renderer, const CprArchive& archive, bool spanish, int screen_width,
		int screen_height)
	: screen_width_(screen_width), screen_height_(screen_height) {
	Image book;
	if (!read_image(archive, "images/options/Hauptmenu.jpg", book) || book.width <= 0 || book.height <= 0) {
		return;
	}
	book_ = make_texture(renderer, book);
	Image glow;
	if (read_image(archive, "images/options/p_hmenu01.bmp", glow)) {
		p3::data::apply_magenta_key(glow);
		glow_ = make_texture(renderer, glow);
	}
	Image map;
	if (read_image(archive, "images/options/hmenu_colormap.bmp", map)) {
		map_width_ = map.width;
		map_height_ = map.height;
		map_.assign(size_t(map.width) * size_t(map.height), 0);
		for (size_t i = 0; i < map_.size(); ++i) {
			const uint8_t r = map.rgba[4 * i];
			const uint8_t g = map.rgba[4 * i + 1];
			const uint8_t b = map.rgba[4 * i + 2];
			if (r == g && g == b && r >= 1 && r <= kStartEntries) {
				map_[i] = r;
			}
		}
	}
	std::vector<uint8_t> ini;
	if (read_file(archive, "scripts/textures.ini", ini)) {
		read_frames(ini, "[TEX14]", frames_, kStartEntries);
	}
	for (int k = 0; k < kStartEntries; ++k) {
		places_[k] = spanish ? kSpanishPlaces[k] : kEnglishPlaces[k];
	}
	// The book over the screen's height, in the middle (the Vita's is wider).
	scale_ = float(screen_height) / float(book.height);
	const int width = int(float(book.width) * scale_ + 0.5f);
	dest_ = {(screen_width - width) / 2, 0, width, screen_height};
}

StartScreen::~StartScreen() {
	if (book_) {
		SDL_DestroyTexture(book_);
	}
	if (glow_) {
		SDL_DestroyTexture(glow_);
	}
}

void StartScreen::draw(SDL_Renderer* renderer, int lit) const {
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	if (!book_) {
		return;
	}
	SDL_RenderCopy(renderer, book_, nullptr, &dest_);
	if (glow_ && lit >= 0 && lit < kStartEntries && frames_[lit].w > 0) {
		const SDL_Rect& f = frames_[lit];
		const SDL_Rect to = {dest_.x + int(float(places_[lit].x) * scale_ + 0.5f),
			dest_.y + int(float(places_[lit].y) * scale_ + 0.5f), int(float(f.w) * scale_ + 0.5f),
			int(float(f.h) * scale_ + 0.5f)};
		SDL_RenderCopy(renderer, glow_, &f, &to);
	}
}

int StartScreen::entry_at(int x, int y) const {
	if (map_.empty() || scale_ <= 0) {
		return -1;
	}
	const int mx = int(float(x - dest_.x) / scale_);
	const int my = int(float(y - dest_.y) / scale_);
	if (mx < 0 || my < 0 || mx >= map_width_ || my >= map_height_) {
		return -1;
	}
	return int(map_[size_t(my) * size_t(map_width_) + size_t(mx)]) - 1;
}

}  // namespace p3::app
