#include "p3data/image.hpp"

#include <climits>
#include <cstring>

#include "stb_image.h"

namespace p3::data {

bool decode_standard_image(const uint8_t* data, size_t size, Image& out, std::string* error) {
	if (size > size_t(INT_MAX)) {
		if (error) {
			*error = "imagen demasiado grande";
		}
		return false;
	}
	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_uc* pixels = stbi_load_from_memory(data, int(size), &width, &height, &channels, 4);
	if (!pixels) {
		if (error) {
			*error = stbi_failure_reason();
		}
		return false;
	}
	out.width = width;
	out.height = height;
	out.rgba.assign(pixels, pixels + size_t(width) * size_t(height) * 4);
	stbi_image_free(pixels);
	return true;
}

namespace {

uint32_t le32(const uint8_t* p) {
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

}  // namespace

bool parse_bmp8(const uint8_t* data, size_t size, Bmp8& out) {
	if (size < 54 || data[0] != 'B' || data[1] != 'M') {
		return false;
	}
	const uint32_t header = le32(data + 14);
	const int32_t width = int32_t(le32(data + 18));
	const int32_t height = int32_t(le32(data + 22));
	const uint16_t bits = uint16_t(data[28] | data[29] << 8);
	const uint32_t compression = le32(data + 30);
	uint32_t colours = le32(data + 46);
	if (header < 40 || bits != 8 || compression != 0 || width <= 0 || height == 0 || height == INT32_MIN ||
		width > 0x10000 || height > 0x10000 || height < -0x10000) {
		return false;
	}
	if (colours == 0 || colours > 256) {
		colours = 256;
	}
	const size_t palette = 14 + size_t(header);
	if (size < palette + 4 * size_t(colours)) {
		return false;
	}
	out.width = width;
	out.height = height < 0 ? -height : height;
	out.bottom_up = height > 0;
	out.pixels = le32(data + 10);
	out.stride = (uint32_t(width) + 3) & ~3u;
	for (uint32_t i = 0; i < 256; ++i) {
		// The file's B, G, R (its fourth byte isn't used) as R, G, B, opaque.
		const uint8_t* q = data + palette + 4 * i;
		const uint8_t rgba[4] = {i < colours ? q[2] : uint8_t(0), i < colours ? q[1] : uint8_t(0),
			i < colours ? q[0] : uint8_t(0), 0xFF};
		std::memcpy(&out.palette[i], rgba, 4);
	}
	return true;
}

void decode_bmp8(const Bmp8& bmp, const uint8_t* file, uint32_t file_offset, int x0, int y0, int width, int rows,
	uint8_t* out, size_t pitch) {
	for (int y = 0; y < rows; ++y) {
		const uint8_t* src = file + (bmp.row(y0 + y) - file_offset) + x0;
		uint8_t* dst = out + size_t(y) * pitch;
		for (int x = 0; x < width; ++x, dst += 4) {
			std::memcpy(dst, &bmp.palette[src[x]], 4);
		}
	}
}

void apply_magenta_key(Image& image) {
	for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
		if (image.rgba[i] == 0xFF && image.rgba[i + 1] == 0x00 && image.rgba[i + 2] == 0xFF) {
			image.rgba[i + 3] = 0;
		}
	}
}

}  // namespace p3::data
