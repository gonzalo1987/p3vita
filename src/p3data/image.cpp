#include "p3data/image.hpp"

#include <climits>

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

void apply_magenta_key(Image& image) {
	for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
		if (image.rgba[i] == 0xFF && image.rgba[i + 1] == 0x00 && image.rgba[i + 2] == 0xFF) {
			image.rgba[i + 3] = 0;
		}
	}
}

}  // namespace p3::data
