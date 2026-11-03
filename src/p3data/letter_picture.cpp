#include "p3data/letter_picture.hpp"

#include <algorithm>
#include <cstdint>
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

}  // namespace

Image letter_map_picture(const GameMemory<0x10>& letter, const Image& sea_map, const Image& piece, const Image& cross) {
	Image out;
	const int pw = piece.width;
	const int ph = piece.height;
	const int w = 3 * pw;
	const int h = 3 * ph;
	if (pw <= 0 || ph <= 0 || sea_map.width < kMapRight || sea_map.height < kMapBottom || kMapRight - kMapLeft < w ||
		kMapBottom - kMapTop < h) {
		return out;
	}
	// The window: centred on the place, moved inside the map; the cross keeps
	// the place (0x51ACDE..0x51AD69).
	int x0 = int(letter.u16(0xC)) - w / 2;
	int y0 = int(letter.u16(0xE)) - h / 2;
	int cross_x = w / 2;
	int cross_y = h / 2;
	if (x0 < kMapLeft) {
		cross_x += x0 - kMapLeft;
		x0 = kMapLeft;
	} else if (x0 + w > kMapRight) {
		cross_x += x0 + w - kMapRight;
		x0 = kMapRight - w;
	}
	if (y0 < kMapTop) {
		cross_y += y0 - kMapTop;
		y0 = kMapTop;
	} else if (y0 + h > kMapBottom) {
		cross_y += y0 + h - kMapBottom;
		y0 = kMapBottom - h;
	}
	out.width = w;
	out.height = h;
	out.rgba.assign(size_t(w) * size_t(h) * 4, 0);
	// The map's 8-bit pixels through its palette in sepia (0x51AD6D..0x51AF08):
	// each entry's grey s, red tone(s / 2 + s), green tone(s), blue tone(s / 2);
	// the entry's fourth byte as it is. ASSUMPTION: 0xFF, as a decoded
	// palette here; the file's is 0, and what the graphics library (sgl in
	// ddraw_Dll.dll) leaves there when it loads the BMP wasn't read.
	for (int y = 0; y < h; ++y) {
		const uint8_t* src = &sea_map.rgba[(size_t(y0 + y) * size_t(sea_map.width) + size_t(x0)) * 4];
		uint8_t* dst = &out.rgba[size_t(y) * size_t(w) * 4];
		for (int x = 0; x < w; ++x, src += 4, dst += 4) {
			const int s = (src[0] + src[1] + src[2]) / 3;
			dst[0] = tone((s >> 1) + s);
			dst[1] = tone(s);
			dst[2] = tone(s >> 1);
			dst[3] = src[3];
		}
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
		const int left = cross_x - kCrossSize / 2;
		const int top = cross_y - kCrossSize / 2;
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

bool letter_map_picture(const CprArchive& archive, const GameMemory<0x10>& letter, Image& out) {
	Image images[3];
	const char* paths[3] = {kSeaMapImage, kLetterMapPiece, kLetterMapCross};
	for (int k = 0; k < 3; ++k) {
		const CprEntry* entry = archive.find(paths[k]);
		std::vector<uint8_t> data;
		if (!entry || !archive.read(*entry, data) || !decode_standard_image(data.data(), data.size(), images[k])) {
			return false;
		}
	}
	out = letter_map_picture(letter, images[0], images[1], images[2]);
	return out.width > 0;
}

}  // namespace p3::data
