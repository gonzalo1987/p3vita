#include "p3data/lz_compress.hpp"

#include <cstring>

namespace p3::data {

std::vector<uint8_t> lz_compress(const uint8_t* data, size_t size) {
	// Stored block (n < 0) until the game's compressor is rewritten.
	std::vector<uint8_t> out(4 + size);
	const int32_t n = -int32_t(size);
	std::memcpy(out.data(), &n, 4);
	std::memcpy(out.data() + 4, data, size);
	return out;
}

}  // namespace p3::data
