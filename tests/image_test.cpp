// The 8-bit BMP decoder (p3data/image.hpp: parse_bmp8, decode_bmp8) against
// stb_image (decode_standard_image) on small made-up files: bottom-up and
// top-down, a width that needs the rows' padding, a palette shorter than 256,
// and a part of the rows and columns.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "p3data/image.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
	for (int k = 0; k < 4; ++k) {
		v[at + size_t(k)] = uint8_t(x >> (8 * k));
	}
}

// An 8-bit BMP of w × h (top-down with a negative height), `colours` palette
// entries (0: 256), pixel (x, y) = (x * 7 + y * 13) % colours.
std::vector<uint8_t> make_bmp(int w, int h, bool top_down, uint32_t colours) {
	const uint32_t count = colours ? colours : 256;
	const uint32_t stride = (uint32_t(w) + 3) & ~3u;
	const uint32_t pixels = 14 + 40 + 4 * count;
	std::vector<uint8_t> v(pixels + stride * uint32_t(h), 0xEE);
	v[0] = 'B';
	v[1] = 'M';
	put32(v, 2, uint32_t(v.size()));
	put32(v, 6, 0);
	put32(v, 10, pixels);
	put32(v, 14, 40);
	put32(v, 18, uint32_t(w));
	put32(v, 22, uint32_t(top_down ? -h : h));
	v[26] = 1;
	v[27] = 0;
	v[28] = 8;
	v[29] = 0;
	put32(v, 30, 0);
	put32(v, 34, stride * uint32_t(h));
	put32(v, 38, 2835);
	put32(v, 42, 2835);
	put32(v, 46, colours);
	put32(v, 50, 0);
	for (uint32_t i = 0; i < count; ++i) {
		v[54 + 4 * i] = uint8_t(i * 3);  // B
		v[54 + 4 * i + 1] = uint8_t(255 - i);  // G
		v[54 + 4 * i + 2] = uint8_t(i * 5 + 1);  // R
		v[54 + 4 * i + 3] = 0;
	}
	for (int y = 0; y < h; ++y) {
		const int file_row = top_down ? y : h - 1 - y;
		for (int x = 0; x < w; ++x) {
			v[pixels + uint32_t(file_row) * stride + uint32_t(x)] = uint8_t((x * 7 + y * 13) % int(count));
		}
	}
	return v;
}

void same_as_stb(int w, int h, bool top_down, uint32_t colours, const char* what) {
	const std::vector<uint8_t> file = make_bmp(w, h, top_down, colours);
	Image reference;
	check(decode_standard_image(file.data(), file.size(), reference) && reference.width == w && reference.height == h,
		what);
	Bmp8 bmp;
	check(parse_bmp8(file.data(), file.size(), bmp) && bmp.width == w && bmp.height == h &&
			bmp.bottom_up == !top_down,
		what);
	std::vector<uint8_t> whole(size_t(w) * size_t(h) * 4);
	decode_bmp8(bmp, file.data(), 0, 0, 0, w, h, whole.data(), size_t(w) * 4);
	check(whole == reference.rgba, what);
	// Rows 1..h-2 and columns 2..w-2 from only their bytes of the file, into a
	// buffer with a wider pitch.
	const int x0 = 2;
	const int y0 = 1;
	const int pw = w - 3;
	const int ph = h - 2;
	const uint32_t first = std::min(bmp.row(y0), bmp.row(y0 + ph - 1));
	const uint32_t end = std::max(bmp.row(y0), bmp.row(y0 + ph - 1)) + bmp.stride;
	const std::vector<uint8_t> rows(file.begin() + first, file.begin() + end);
	const size_t pitch = size_t(pw) * 4 + 12;
	std::vector<uint8_t> part(pitch * size_t(ph), 0x55);
	decode_bmp8(bmp, rows.data(), first, x0, y0, pw, ph, part.data(), pitch);
	bool equal = true;
	for (int y = 0; y < ph; ++y) {
		equal &= std::memcmp(&part[size_t(y) * pitch], &reference.rgba[(size_t(y0 + y) * size_t(w) + size_t(x0)) * 4],
					 size_t(pw) * 4) == 0;
		equal &= part[size_t(y) * pitch + size_t(pw) * 4] == 0x55;  // The pitch's rest untouched.
	}
	check(equal, what);
}

}  // namespace

int main() {
	same_as_stb(9, 6, false, 0, "bottom-up, 256 colours, padded rows");
	same_as_stb(12, 5, true, 0, "top-down");
	same_as_stb(7, 7, false, 40, "a palette of 40");
	Bmp8 bmp;
	std::vector<uint8_t> file = make_bmp(8, 4, false, 0);
	file[28] = 24;
	check(!parse_bmp8(file.data(), file.size(), bmp), "24 bits: not an 8-bit BMP");
	file = make_bmp(8, 4, false, 0);
	file[30] = 1;
	check(!parse_bmp8(file.data(), file.size(), bmp), "RLE: not read");
	file = make_bmp(8, 4, false, 0);
	check(!parse_bmp8(file.data(), 300, bmp), "the palette cut");
	if (failures) {
		std::fprintf(stderr, "%d failures\n", failures);
		return EXIT_FAILURE;
	}
	std::puts("image: ok");
	return EXIT_SUCCESS;
}
