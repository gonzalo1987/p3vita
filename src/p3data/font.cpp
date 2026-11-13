#include "p3data/font.hpp"

#include <algorithm>
#include <cmath>

#include "stb_truetype.h"

namespace p3::data {

namespace {

constexpr int kFirstChar = 32;
constexpr int kCharCount = 256 - kFirstChar;
constexpr int kMaxAtlasSize = 2048;

bool fail(std::string* error, const std::string& message) {
	if (error) {
		*error = message;
	}
	return false;
}

// Siguiente carácter de un texto UTF-8; los que no son Latin-1 pasan a '?'.
uint8_t next_latin1(std::string_view text, size_t& i) {
	const uint8_t c = uint8_t(text[i++]);
	if (c < 0x80) {
		return c;
	}
	int extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
	if (extra == 0) {
		return '?';  // Byte de continuación suelto o inválido.
	}
	uint32_t code = c & (0x3F >> extra);
	for (; extra > 0 && i < text.size() && (uint8_t(text[i]) & 0xC0) == 0x80; --extra) {
		code = code << 6 | (uint8_t(text[i++]) & 0x3F);
	}
	return code <= 0xFF && extra == 0 ? uint8_t(code) : uint8_t('?');
}

std::string trim(const std::string& text) {
	const size_t first = text.find_first_not_of(" \t\r");
	const size_t last = text.find_last_not_of(" \t\r");
	return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}

}  // namespace

std::unique_ptr<FontAtlas> FontAtlas::create(const std::vector<uint8_t>& ttf, float pixel_height, std::string* error) {
	stbtt_fontinfo info;
	if (ttf.empty() || !stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
		fail(error, "fuente TrueType inválida");
		return nullptr;
	}

	// Empaqueta los caracteres en un atlas que crece hasta que caben.
	std::vector<stbtt_packedchar> packed(kCharCount);
	std::vector<uint8_t> coverage;
	int size = 256;
	for (;; size *= 2) {
		if (size > kMaxAtlasSize) {
			fail(error, "la fuente no cabe en el atlas");
			return nullptr;
		}
		coverage.assign(size_t(size) * size_t(size), 0);
		stbtt_pack_context context;
		if (!stbtt_PackBegin(&context, coverage.data(), size, size, 0, 1, nullptr)) {
			fail(error, "no se puede rasterizar la fuente");
			return nullptr;
		}
		const int ok = stbtt_PackFontRange(&context, ttf.data(), 0, pixel_height, kFirstChar, kCharCount,
			packed.data());
		stbtt_PackEnd(&context);
		if (ok) {
			break;
		}
	}

	std::unique_ptr<FontAtlas> font(new FontAtlas());
	font->image_.width = size;
	font->image_.height = size;
	font->image_.rgba.resize(coverage.size() * 4);
	for (size_t i = 0; i < coverage.size(); ++i) {
		uint8_t* p = &font->image_.rgba[i * 4];
		p[0] = p[1] = p[2] = 0xFF;
		p[3] = coverage[i];
	}
	for (int c = 0; c < kCharCount; ++c) {
		const stbtt_packedchar& pc = packed[size_t(c)];
		font->glyphs_[size_t(kFirstChar + c)] = {pc.x0, pc.y0, pc.x1, pc.y1, pc.xoff, pc.yoff, pc.xadvance};
	}

	int ascent = 0;
	int descent = 0;
	int line_gap = 0;
	stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
	const float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);
	font->ascent_ = int(std::lround(ascent * scale));
	font->line_height_ = int(std::lround((ascent - descent + line_gap) * scale));
	return font;
}

std::unique_ptr<FontAtlas> FontAtlas::load(const CprArchive& archive, const std::string& path, float pixel_height,
	std::string* error) {
	const CprEntry* entry = archive.find(path);
	std::vector<uint8_t> ttf;
	if (!entry || !archive.read(*entry, ttf)) {
		fail(error, "no se puede leer " + path);
		return nullptr;
	}
	return create(ttf, pixel_height, error);
}

std::unique_ptr<FontAtlas> FontAtlas::create_em(const std::vector<uint8_t>& ttf, int em_width, int em_height,
	std::string* error) {
	stbtt_fontinfo info;
	if (ttf.empty() || em_width <= 0 || em_height <= 0 ||
		!stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
		fail(error, "invalid TrueType font");
		return nullptr;
	}
	const float scale_x = stbtt_ScaleForMappingEmToPixels(&info, float(em_width));
	const float scale_y = stbtt_ScaleForMappingEmToPixels(&info, float(em_height));
	// The glyphs in rows of an atlas that grows until they fit (a pixel between them).
	struct Box {
		int glyph = 0;
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		int x = 0, y = 0;
	};
	std::vector<Box> boxes(kCharCount);
	for (int c = 0; c < kCharCount; ++c) {
		Box& b = boxes[size_t(c)];
		b.glyph = stbtt_FindGlyphIndex(&info, kFirstChar + c);
		stbtt_GetGlyphBitmapBox(&info, b.glyph, scale_x, scale_y, &b.x0, &b.y0, &b.x1, &b.y1);
	}
	int size = 128;
	for (;; size *= 2) {
		if (size > kMaxAtlasSize) {
			fail(error, "the font doesn't fit in the atlas");
			return nullptr;
		}
		int x = 1;
		int y = 1;
		int row = 0;
		bool fits = true;
		for (Box& b : boxes) {
			const int w = b.x1 - b.x0;
			const int h = b.y1 - b.y0;
			if (x + w + 1 > size) {
				x = 1;
				y += row + 1;
				row = 0;
			}
			if (y + h + 1 > size) {
				fits = false;
				break;
			}
			b.x = x;
			b.y = y;
			x += w + 1;
			row = std::max(row, h);
		}
		if (fits) {
			break;
		}
	}
	std::vector<uint8_t> coverage(size_t(size) * size_t(size), 0);
	std::unique_ptr<FontAtlas> font(new FontAtlas());
	for (int c = 0; c < kCharCount; ++c) {
		const Box& b = boxes[size_t(c)];
		const int w = b.x1 - b.x0;
		const int h = b.y1 - b.y0;
		if (w > 0 && h > 0) {
			stbtt_MakeGlyphBitmap(&info, &coverage[size_t(b.y) * size_t(size) + size_t(b.x)], w, h, size, scale_x, scale_y,
				b.glyph);
		}
		int advance = 0;
		int bearing = 0;
		stbtt_GetGlyphHMetrics(&info, b.glyph, &advance, &bearing);
		font->glyphs_[size_t(kFirstChar + c)] = {b.x, b.y, b.x + w, b.y + h, float(b.x0), float(b.y0), advance * scale_x};
	}
	font->image_.width = size;
	font->image_.height = size;
	font->image_.rgba.resize(coverage.size() * 4);
	for (size_t i = 0; i < coverage.size(); ++i) {
		uint8_t* p = &font->image_.rgba[i * 4];
		p[0] = p[1] = p[2] = 0xFF;
		p[3] = coverage[i];
	}
	int ascent = 0;
	int descent = 0;
	int line_gap = 0;
	stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
	font->ascent_ = int(std::lround(ascent * scale_y));
	font->line_height_ = font->ascent_ + int(std::lround(-descent * scale_y));
	return font;
}

std::unique_ptr<FontAtlas> FontAtlas::load_em(const CprArchive& archive, const std::string& path, int em_width,
	int em_height, std::string* error) {
	const CprEntry* entry = archive.find(path);
	std::vector<uint8_t> ttf;
	if (!entry || !archive.read(*entry, ttf)) {
		fail(error, "can't read " + path);
		return nullptr;
	}
	return create_em(ttf, em_width, em_height, error);
}

std::vector<FontAtlas::Quad> FontAtlas::layout(std::string_view utf8, int x, int y) const {
	std::vector<Quad> quads;
	float pen = float(x);
	const float baseline = float(y + ascent_);
	for (size_t i = 0; i < utf8.size();) {
		const uint8_t c = next_latin1(utf8, i);
		if (c < kFirstChar) {
			continue;
		}
		const Glyph& g = glyphs_[c];
		if (g.x1 > g.x0 && g.y1 > g.y0) {
			quads.push_back({g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0, int(std::lround(pen + g.x_offset)),
				int(std::lround(baseline + g.y_offset))});
		}
		pen += g.advance;
	}
	return quads;
}

int FontAtlas::measure(std::string_view utf8) const {
	float width = 0;
	for (size_t i = 0; i < utf8.size();) {
		const uint8_t c = next_latin1(utf8, i);
		if (c >= kFirstChar) {
			width += glyphs_[c].advance;
		}
	}
	return int(std::lround(width));
}

std::vector<std::string> load_town_names(const CprArchive& archive) {
	for (const char* path : {"scripts/NamenStaedte_spa.txt", "scripts/NamenStaedte_eng.txt",
			 "scripts/NamenStaedte.txt"}) {
		const CprEntry* entry = archive.find(path);
		std::vector<uint8_t> data;
		if (!entry || !archive.read(*entry, data)) {
			continue;
		}
		std::vector<std::string> names;
		const std::string text(data.begin(), data.end());
		for (size_t pos = 0; pos < text.size() && names.size() < 40;) {
			size_t end = text.find('\n', pos);
			if (end == std::string::npos) {
				end = text.size();
			}
			names.push_back(latin1_to_utf8(trim(text.substr(pos, end - pos))));
			pos = end + 1;
		}
		return names;
	}
	return {};
}

}  // namespace p3::data
