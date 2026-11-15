#pragma once

// The viewer's text: a font of the game (FontAtlas) put in a texture, with the
// Vita's buttons, the game's signs (the coin, a barrel, a load) and its
// pictures by number drawn as small pictures among the words.

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "p3data/font.hpp"
#include "p3data/game_texts.hpp"
#include "p3data/image.hpp"
#include "ui_images.hpp"

// The Vita's buttons in the viewer's texts: a byte that TextRenderer draws as
// the button's symbol, in its colour (the game's font has none). Macros, to
// be joined to the literals ("\x01" followed by a letter would be one escape).
#define BTN_CROSS "\x01"
#define BTN_CIRCLE "\x02"
#define BTN_SQUARE "\x03"
#define BTN_TRIANGLE "\x04"

namespace p3::app {

using p3::data::FontAtlas;
using p3::data::Image;

// The bytes TextRenderer draws as a symbol: the buttons above and the signs
// of the game's texts (game_texts.hpp: kTextCoin, kTextBarrel, kTextLoad).
inline bool symbol_byte(char c) {
	return c >= 1 && c <= 7;
}

// The interface's text in a font of the game, uploaded as a texture.
class TextRenderer {
public:
	TextRenderer(SDL_Renderer* renderer, std::unique_ptr<FontAtlas> font) : renderer_(renderer), font_(std::move(font)) {
		const Image& image = font_->image();
		texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width,
			image.height);
		if (texture_) {
			SDL_UpdateTexture(texture_, nullptr, image.rgba.data(), image.width * 4);
			SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
		}
	}

	~TextRenderer() {
		SDL_DestroyTexture(texture_);
		for (SDL_Texture* t : buttons_) {
			if (t) {
				SDL_DestroyTexture(t);
			}
		}
		for (const auto& [id, picture] : pictures_) {
			if (picture.texture) {
				SDL_DestroyTexture(picture.texture);
			}
		}
	}

	// The game's pictures by their TEX number of textures.ini, read from `archive` (the caller keeps it while this
	// renderer lives; null: none) the first time each is drawn: the signs (5, 6, 7: TEX16014, TEX16030 and TEX16031,
	// the ones the game's text box draws for \C, \B and \L: 0x42ED9E, 0x462540) and the texts' pictures by number
	// (\tN_: kTextPicture N kTextPictureEnd). Without them, the signs are drawn by hand and the pictures left out.
	void set_archive(const p3::data::CprArchive* archive) {
		archive_ = archive;
		ini_.clear();
		ini_read_ = false;
	}

	// The width of the text, the symbols and pictures included.
	int measure(const std::string& text) const {
		int width = 0;
		walk(text, [&](const std::string& part) { width += font_->measure(part); },
			[&](char c) { width += symbol_width(c); }, [&](uint32_t id) { width += picture_width(id); });
		return width;
	}
	int line_height() const { return font_->line_height(); }

	// The colour's alpha fades the letters, the buttons' symbols and the pictures alike (a button that can't be
	// pressed).
	void draw(const std::string& text, int x, int y, SDL_Color color) {
		SDL_SetTextureColorMod(texture_, color.r, color.g, color.b);
		SDL_SetTextureAlphaMod(texture_, color.a);
		alpha_ = color.a;
		walk(
			text,
			[&](const std::string& part) {
				for (const FontAtlas::Quad& q : font_->layout(part, x, y)) {
					const SDL_Rect src = {q.src_x, q.src_y, q.width, q.height};
					const SDL_Rect dst = {q.dst_x, q.dst_y, q.width, q.height};
					SDL_RenderCopy(renderer_, texture_, &src, &dst);
				}
				x += font_->measure(part);
			},
			[&](char c) {
				draw_symbol(c, x, y);
				x += symbol_width(c);
			},
			[&](uint32_t id) {
				draw_picture(id, x + 1, y);
				x += picture_width(id);
			});
	}

private:
	// The text's runs of letters, its symbols and its pictures by number, in order.
	template <typename Part, typename Symbol, typename Picture>
	static void walk(const std::string& text, Part part, Symbol symbol, Picture picture) {
		size_t start = 0;
		for (size_t i = 0; i <= text.size(); ++i) {
			const bool end = i == text.size();
			if (!end && !symbol_byte(text[i]) && text[i] != p3::data::kTextPicture) {
				continue;
			}
			if (i > start) {
				part(text.substr(start, i - start));
			}
			if (end) {
				break;
			}
			if (text[i] == p3::data::kTextPicture) {
				size_t close = text.find(p3::data::kTextPictureEnd, i + 1);
				if (close == std::string::npos) {
					close = text.size();
				}
				picture(uint32_t(std::strtoul(text.substr(i + 1, close - i - 1).c_str(), nullptr, 10)));
				i = close;
			} else {
				symbol(text[i]);
			}
			start = i + 1;
		}
	}

	struct Picture {
		SDL_Texture* texture = nullptr;
		SDL_Rect frame{};  // The texture's frame 0 (OffsetNSize0).
	};

	// The picture TEX`id` of textures.ini (its TextureFile, its frame 0), loaded the first time; null if it can't be.
	const Picture* picture(uint32_t id) const {
		const auto known = pictures_.find(id);
		if (known != pictures_.end()) {
			return known->second.texture ? &known->second : nullptr;
		}
		Picture& p = pictures_[id];
		if (!archive_) {
			return nullptr;
		}
		if (!ini_read_) {
			std::vector<uint8_t> bytes;
			if (read_cpr_file(*archive_, "scripts/textures.ini", bytes)) {
				ini_.assign(bytes.begin(), bytes.end());
			}
			ini_read_ = true;
		}
		const std::string section = "[TEX" + std::to_string(id) + "]";
		const size_t at = ini_.find(section);
		const size_t next = at == std::string::npos ? at : ini_.find('[', at + section.size());
		const size_t key = at == std::string::npos ? at : ini_.find("TextureFile=", at);
		if (key == std::string::npos || key > next) {
			return nullptr;
		}
		const size_t from = key + 12;
		const size_t to = ini_.find_first_of("\r\n", from);
		const std::string file = ini_.substr(from, to == std::string::npos ? std::string::npos : to - from);
		p3::data::Image image;
		if (!read_cpr_image(*archive_, file.c_str(), image)) {
			return nullptr;
		}
		const size_t use_alpha = ini_.find("UseAlpha=", at);
		if (use_alpha < next && (ini_.compare(use_alpha + 9, 4, "true") == 0 || ini_.compare(use_alpha + 9, 4, "True") == 0)) {
			apply_bmp_palette_alpha(*archive_, file.c_str(), image);  // (The votes' tally marks.)
		}
		p.frame = {0, 0, image.width, image.height};
		const std::vector<uint8_t> ini(ini_.begin(), ini_.end());
		read_texture_frames(ini, section.c_str(), &p.frame, 1);
		p.texture = make_texture(renderer_, image);
		return p.texture && p.frame.w > 0 && p.frame.h > 0 ? &p : nullptr;
	}

	// A picture's size in a line: its own, no higher than the line, its width kept in proportion.
	SDL_Rect picture_size(const Picture& p) const {
		const int h = std::min(p.frame.h, font_->line_height());
		return {0, 0, std::max(1, p.frame.w * h / p.frame.h), h};
	}
	int picture_width(uint32_t id) const {
		const Picture* p = picture(id);
		return p ? picture_size(*p).w + 2 : 0;
	}
	void draw_picture(uint32_t id, int x, int y) {
		const Picture* p = picture(id);
		if (!p) {
			return;
		}
		const SDL_Rect r = picture_size(*p);
		const SDL_Rect dst = {x, y + (font_->line_height() - r.h) / 2, r.w, r.h};
		SDL_SetTextureAlphaMod(p->texture, alpha_);
		SDL_RenderCopy(renderer_, p->texture, &p->frame, &dst);
	}

	// The signs' pictures: the coin's, a barrel's and a load's.
	static uint32_t sign_picture(char c) { return c == 5 ? 16014u : c == 6 ? 16030u : 16031u; }

	// A button's symbol is about as wide as the line is high; a sign, its picture's width (or narrower, by hand).
	int symbol_width(char c) const {
		if (c <= 4) {
			return font_->line_height() + 2;
		}
		const int w = picture_width(sign_picture(c));
		return w > 0 ? w : font_->line_height() * 2 / 3 + 2;
	}

	// The symbol in a square about the text's height: the buttons in the
	// PlayStation's colours, smoothed (button_texture), three quarters of the
	// line high; the signs, the game's pictures or, without them, a gold
	// disc, a barrel and a sack drawn small and filled.
	void draw_symbol(char c, int x, int y) {
		const int h = font_->line_height();
		if (c <= 4) {
			const int size = std::max(8, h * 3 / 4);
			if (SDL_Texture* t = button_texture(c, size)) {
				const SDL_Rect dst = {x + (symbol_width(c) - size) / 2, y + (h - size) / 2, size, size};
				SDL_SetTextureAlphaMod(t, alpha_);
				SDL_RenderCopy(renderer_, t, nullptr, &dst);
			}
			return;
		}
		if (picture(sign_picture(c))) {
			draw_picture(sign_picture(c), x + 1, y);
			return;
		}
		const int size = h * 3 / 5;
		const int left = x + (symbol_width(c) - size) / 2;
		const int top = y + (h - size) / 2;
		SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
		draw_sign(c, left, top, size);
	}

	// The game draws \C, \B and \L of its texts as small pictures.
	void draw_sign(char c, int left, int top, int size) {
		if (c == 5) {  // The coin: a gold disc with a darker rim.
			const float r = size / 2.0f;
			const float cx = left + r - 0.5f;
			for (int k = 0; k < size; ++k) {
				const float dy = float(k) - r + 0.5f;
				const int half = int(std::lround(std::sqrt(std::max(0.0f, r * r - dy * dy))));
				SDL_SetRenderDrawColor(renderer_, 236, 190, 60, 255);
				SDL_RenderDrawLine(renderer_, int(cx) - half + 1, top + k, int(cx) + half - 1, top + k);
				SDL_SetRenderDrawColor(renderer_, 150, 105, 25, 255);
				SDL_RenderDrawPoint(renderer_, int(cx) - half, top + k);
				SDL_RenderDrawPoint(renderer_, int(cx) + half, top + k);
			}
			return;
		}
		const int w = size * 3 / 4;
		const SDL_Rect body = {left + (size - w) / 2, top, w, size};
		if (c == 6) {  // A barrel: brown staves and two dark hoops.
			SDL_SetRenderDrawColor(renderer_, 160, 105, 55, 255);
			SDL_RenderFillRect(renderer_, &body);
			SDL_SetRenderDrawColor(renderer_, 70, 45, 25, 255);
			SDL_RenderDrawRect(renderer_, &body);
			SDL_RenderDrawLine(renderer_, body.x, top + size / 3, body.x + w - 1, top + size / 3);
			SDL_RenderDrawLine(renderer_, body.x, top + size * 2 / 3, body.x + w - 1, top + size * 2 / 3);
		} else {  // A load: a pale sack tied at the top.
			const SDL_Rect sack = {body.x, top + size / 4, w, size - size / 4};
			SDL_SetRenderDrawColor(renderer_, 205, 185, 140, 255);
			SDL_RenderFillRect(renderer_, &sack);
			SDL_SetRenderDrawColor(renderer_, 110, 90, 60, 255);
			SDL_RenderDrawRect(renderer_, &sack);
			SDL_RenderDrawLine(renderer_, body.x + w / 2, top, body.x + w / 2, top + size / 4);
			SDL_RenderDrawLine(renderer_, body.x + w / 4, top + size / 4, body.x + w * 3 / 4, top + size / 4);
		}
	}

	// A button's symbol (1 the cross, 2 the circle, 3 the square, 4 the triangle), in the PlayStation's colours,
	// drawn once into a texture of `size` pixels and kept: smoothed (each pixel's coverage from 4 × 4 samples),
	// its lines about a sixth of its size thick.
	SDL_Texture* button_texture(char c, int size) {
		SDL_Texture*& kept = buttons_[size_t(c - 1)];
		if (kept) {
			return kept;
		}
		static constexpr SDL_Color kColors[4] = {{124, 178, 232, 255}, {255, 102, 102, 255}, {255, 105, 248, 255},
			{64, 226, 160, 255}};
		const SDL_Color color = kColors[c - 1];
		const float s = float(size);
		const float w = std::max(1.6f, s / 6.0f);  // The line's width.
		const float m = w / 2 + 0.5f;  // The margin, so that the lines' ends stay inside.
		// The distance from (px, py) to the segment (ax, ay)-(bx, by).
		auto segment = [](float px, float py, float ax, float ay, float bx, float by) {
			const float dx = bx - ax, dy = by - ay;
			const float t = std::clamp(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
			const float qx = ax + t * dx - px, qy = ay + t * dy - py;
			return std::sqrt(qx * qx + qy * qy);
		};
		auto inside = [&](float px, float py) {
			switch (c) {
			case 1:
				return segment(px, py, m, m, s - m, s - m) <= w / 2 || segment(px, py, s - m, m, m, s - m) <= w / 2;
			case 2: {
				const float d = std::sqrt((px - s / 2) * (px - s / 2) + (py - s / 2) * (py - s / 2));
				return d <= s / 2 - 0.5f && d >= s / 2 - 0.5f - w;
			}
			case 3: {
				const float lo = s * 0.12f, hi = s - s * 0.12f;
				const bool outer = px >= lo && px <= hi && py >= lo && py <= hi;
				const bool inner = px > lo + w && px < hi - w && py > lo + w && py < hi - w;
				return outer && !inner;
			}
			default: {
				const float top = m, bottom = s - m - s * 0.06f;
				return segment(px, py, s / 2, top, m, bottom) <= w / 2 || segment(px, py, s / 2, top, s - m, bottom) <= w / 2 ||
					segment(px, py, m, bottom, s - m, bottom) <= w / 2;
			}
			}
		};
		std::vector<uint8_t> rgba(size_t(size * size) * 4);
		for (int y = 0; y < size; ++y) {
			for (int x = 0; x < size; ++x) {
				int hits = 0;
				for (int sy = 0; sy < 4; ++sy) {
					for (int sx = 0; sx < 4; ++sx) {
						hits += inside(float(x) + (float(sx) + 0.5f) / 4, float(y) + (float(sy) + 0.5f) / 4) ? 1 : 0;
					}
				}
				uint8_t* p = &rgba[size_t(y * size + x) * 4];
				p[0] = color.r;
				p[1] = color.g;
				p[2] = color.b;
				p[3] = uint8_t(hits * 255 / 16);
			}
		}
		kept = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, size, size);
		if (kept) {
			SDL_UpdateTexture(kept, nullptr, rgba.data(), size * 4);
			SDL_SetTextureBlendMode(kept, SDL_BLENDMODE_BLEND);
		}
		return kept;
	}

	SDL_Renderer* renderer_;
	std::unique_ptr<FontAtlas> font_;
	SDL_Texture* texture_ = nullptr;
	std::array<SDL_Texture*, 4> buttons_{};  // The buttons' symbols (button_texture).
	const p3::data::CprArchive* archive_ = nullptr;  // set_archive
	mutable std::string ini_;  // textures.ini, read the first time a picture is drawn.
	mutable bool ini_read_ = false;
	mutable std::unordered_map<uint32_t, Picture> pictures_;  // By TEX number; a null texture: it can't be loaded.
	Uint8 alpha_ = 255;  // The alpha of the text being drawn.
};

}  // namespace p3::app
