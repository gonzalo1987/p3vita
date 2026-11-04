#include "p3data/letter_picture.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/seamap.hpp"

namespace p3::data {

namespace {

// Where the game's window over the sea map may go (0x51ACFB..0x51AD6D): its
// left edge from 0x44, its right edge up to 0x91C, its top from 0x42, its
// bottom up to 0x6C7 (the map without its frame).
constexpr int kMapLeft = 0x44;
constexpr int kMapRight = 0x91C;
constexpr int kMapTop = 0x42;
constexpr int kMapBottom = 0x6C7;

// TEX30033's frame 7 (scripts/textures.ini, OffsetNSize7): the whole cross.
constexpr int kCrossX = 126;
constexpr int kCrossY = 0;
constexpr int kCrossSize = 18;

// A channel of the sepia palette (0x51ADF1..0x51AE29): brighter above 0x80,
// darker below, kept in 0..255.
uint8_t tone(int x) {
	const int v = x >= 0x80 ? x + ((x >> 1) & 0x3F) : x - ((0x80 - x) >> 1);
	return uint8_t(std::clamp(v, 0, 255));
}

// The floor of v / 256 (the MMX code's psraw 8).
int floor_256(int v) {
	return v >= 0 ? v >> 8 : -((-v + 255) >> 8);
}

// The picture's window over the sea map, w × h, and the cross in it.
struct MapWindow {
	int x0 = 0;
	int y0 = 0;
	int cross_x = 0;
	int cross_y = 0;
};

// Centred on the place, moved inside the map; the cross keeps the place
// (0x51ACDE..0x51AD69). Empty (false) if the window can't fit.
bool map_window(const GameMemory<0x10>& letter, int w, int h, MapWindow& out) {
	if (w <= 0 || h <= 0 || kMapRight - kMapLeft < w || kMapBottom - kMapTop < h) {
		return false;
	}
	out.x0 = int(letter.u16(0xC)) - w / 2;
	out.y0 = int(letter.u16(0xE)) - h / 2;
	out.cross_x = w / 2;
	out.cross_y = h / 2;
	if (out.x0 < kMapLeft) {
		out.cross_x += out.x0 - kMapLeft;
		out.x0 = kMapLeft;
	} else if (out.x0 + w > kMapRight) {
		out.cross_x += out.x0 + w - kMapRight;
		out.x0 = kMapRight - w;
	}
	if (out.y0 < kMapTop) {
		out.cross_y += out.y0 - kMapTop;
		out.y0 = kMapTop;
	} else if (out.y0 + h > kMapBottom) {
		out.cross_y += out.y0 + h - kMapBottom;
		out.y0 = kMapBottom - h;
	}
	return true;
}

// The picture from the window's pixels of the sea map (`crop`, 3 × the
// parchment's size, RGBA as decoded).
Image compose(const GameMemory<0x10>& letter, const MapWindow& window, Image crop, const Image& piece,
	const Image& cross) {
	Image out = std::move(crop);
	const int pw = piece.width;
	const int ph = piece.height;
	const int w = out.width;
	const int h = out.height;
	// The map's 8-bit pixels through its palette in sepia (0x51AD6D..0x51AF08):
	// each entry's grey s, red tone(s / 2 + s), green tone(s), blue tone(s / 2);
	// the entry's fourth byte as it is. ASSUMPTION: 0xFF, as a decoded
	// palette here; the file's is 0, and what the graphics library (sgl in
	// ddraw_Dll.dll) leaves there when it loads the BMP wasn't read.
	for (size_t i = 0; i < out.rgba.size(); i += 4) {
		uint8_t* p = &out.rgba[i];
		const int s = (p[0] + p[1] + p[2]) / 3;
		p[0] = tone((s >> 1) + s);
		p[1] = tone(s);
		p[2] = tone(s >> 1);
	}
	// The parchment over the pieces of [+0xA] (0x51AF58..0x51AFB1): the
	// library's alpha blend of a 32-bit texture with the constant colour
	// 0xFFFFFFFF, in its MMX code (0x508DDC9; 0x508DEBA with SSE is the same):
	// each channel s' = s · 255 >> 8, then d + (s' − d) · a' >> 8 with a'
	// the source's s' alpha, the alpha too.
	const uint16_t pieces = letter.u16(0xA);
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			if (!(pieces & (1u << (3 * row + column)))) {
				continue;
			}
			for (int y = 0; y < ph; ++y) {
				const uint8_t* src = &piece.rgba[size_t(y) * size_t(pw) * 4];
				uint8_t* dst = &out.rgba[(size_t(row * ph + y) * size_t(w) + size_t(column * pw)) * 4];
				for (int x = 0; x < pw; ++x, src += 4, dst += 4) {
					const int alpha = src[3] * 255 >> 8;
					for (int k = 0; k < 4; ++k) {
						const int s = src[k] * 255 >> 8;
						dst[k] = uint8_t(dst[k] + floor_256((s - dst[k]) * alpha));
					}
				}
			}
		}
	}
	// The cross on the place (0x51AFB3..0x51B016), with its colour key
	// (ColorKey=0xff000000: black is not drawn, the rest opaque).
	if (cross.width >= kCrossX + kCrossSize && cross.height >= kCrossY + kCrossSize) {
		const int left = window.cross_x - kCrossSize / 2;
		const int top = window.cross_y - kCrossSize / 2;
		for (int y = 0; y < kCrossSize; ++y) {
			for (int x = 0; x < kCrossSize; ++x) {
				if (left + x < 0 || left + x >= w || top + y < 0 || top + y >= h) {
					continue;
				}
				const uint8_t* src = &cross.rgba[(size_t(kCrossY + y) * size_t(cross.width) + size_t(kCrossX + x)) * 4];
				if (src[0] == 0 && src[1] == 0 && src[2] == 0) {
					continue;
				}
				uint8_t* dst = &out.rgba[(size_t(top + y) * size_t(w) + size_t(left + x)) * 4];
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst[3] = 0xFF;
			}
		}
	}
	// The pieces not in [+0xA], cleared to 0 (0x51B033..0x51B0B8): not drawn.
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			if (pieces & (1u << (3 * row + column))) {
				continue;
			}
			for (int y = 0; y < ph; ++y) {
				uint8_t* dst = &out.rgba[(size_t(row * ph + y) * size_t(w) + size_t(column * pw)) * 4];
				std::fill(dst, dst + size_t(pw) * 4, uint8_t(0));
			}
		}
	}
	return out;
}

bool read_image(const CprArchive& archive, const char* path, Image& out) {
	const CprEntry* entry = archive.find(path);
	std::vector<uint8_t> data;
	return entry && archive.read(*entry, data) && decode_standard_image(data.data(), data.size(), out);
}

// The window's pixels of the sea map read from the archive: only its rows when
// the map is an uncompressed 8-bit BMP (the game's: 0.7 MB of its 4.3 MB, and
// no decoding of the rest), else the whole image decoded.
bool read_map_window(const CprArchive& archive, const MapWindow& window, int w, int h, Image& out) {
	const CprEntry* entry = archive.find(kSeaMapImage);
	if (!entry) {
		return false;
	}
	std::vector<uint8_t> head;
	Bmp8 bmp;
	if (archive.read_range(*entry, 0, std::min<uint32_t>(entry->size, 2048), head) &&
		parse_bmp8(head.data(), head.size(), bmp)) {
		if (bmp.width < kMapRight || bmp.height < kMapBottom) {
			return false;
		}
		const uint32_t first = std::min(bmp.row(window.y0), bmp.row(window.y0 + h - 1));
		const uint32_t end = std::max(bmp.row(window.y0), bmp.row(window.y0 + h - 1)) + bmp.stride;
		std::vector<uint8_t> rows;
		if (!archive.read_range(*entry, first, end - first, rows)) {
			return false;
		}
		out.width = w;
		out.height = h;
		out.rgba.resize(size_t(w) * size_t(h) * 4);
		decode_bmp8(bmp, rows.data(), first, window.x0, window.y0, w, h, out.rgba.data(), size_t(w) * 4);
		return true;
	}
	Image map;
	if (!read_image(archive, kSeaMapImage, map) || map.width < kMapRight || map.height < kMapBottom) {
		return false;
	}
	out.width = w;
	out.height = h;
	out.rgba.resize(size_t(w) * size_t(h) * 4);
	for (int y = 0; y < h; ++y) {
		const uint8_t* src = &map.rgba[(size_t(window.y0 + y) * size_t(map.width) + size_t(window.x0)) * 4];
		std::copy(src, src + size_t(w) * 4, &out.rgba[size_t(y) * size_t(w) * 4]);
	}
	return true;
}

}  // namespace

Image letter_map_picture(const GameMemory<0x10>& letter, const Image& sea_map, const Image& piece, const Image& cross) {
	const int w = 3 * piece.width;
	const int h = 3 * piece.height;
	MapWindow window;
	if (!map_window(letter, w, h, window) || sea_map.width < kMapRight || sea_map.height < kMapBottom) {
		return Image();
	}
	Image crop{w, h, std::vector<uint8_t>(size_t(w) * size_t(h) * 4)};
	for (int y = 0; y < h; ++y) {
		const uint8_t* src = &sea_map.rgba[(size_t(window.y0 + y) * size_t(sea_map.width) + size_t(window.x0)) * 4];
		std::copy(src, src + size_t(w) * 4, &crop.rgba[size_t(y) * size_t(w) * 4]);
	}
	return compose(letter, window, std::move(crop), piece, cross);
}

bool letter_map_picture(const CprArchive& archive, const GameMemory<0x10>& letter, Image& out) {
	Image piece;
	Image cross;
	if (!read_image(archive, kLetterMapPiece, piece) || !read_image(archive, kLetterMapCross, cross)) {
		return false;
	}
	const int w = 3 * piece.width;
	const int h = 3 * piece.height;
	MapWindow window;
	Image crop;
	if (!map_window(letter, w, h, window) || !read_map_window(archive, window, w, h, crop)) {
		return false;
	}
	out = compose(letter, window, std::move(crop), piece, cross);
	return true;
}

}  // namespace p3::data
