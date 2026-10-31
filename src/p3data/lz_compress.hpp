#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace p3::data {

// The game's LZ compressor (0x54C410 → 0x6381E0), the inverse of
// lz_decompress (lz.hpp): a blob that starts with the s32 size and the bit
// stream. Same bytes as the game's for the same input.
std::vector<uint8_t> lz_compress(const uint8_t* data, size_t size);

}  // namespace p3::data
