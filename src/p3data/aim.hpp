#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "p3data/image.hpp"

namespace p3::data {

// Decodifica una imagen AIM, el formato propio del motor (docs/formats/aim.md),
// a RGBA. El alfa se entrega tal cual viene en el fichero: la transparencia por
// color clave (magenta FF00FF) y las sombras son cosa del motor, no del formato.
bool decode_aim(const uint8_t* data, size_t size, Image& out, std::string* error = nullptr);

// An indexed AIM (type 34) as AIM.dll gives it for the encoding 1 (PALETTE8,
// docs/formats/aim.md): its 8-bit palette indices, rows of `pitch` =
// (width + 3) & ~3 bytes. The game locks the town view's hit mask so
// (0x5894F0, the battles' town view: SaveGame::TownView).
bool decode_aim_indices(const uint8_t* data, size_t size, uint32_t& width, uint32_t& height, uint32_t& pitch,
	std::vector<uint8_t>& indices, std::string* error = nullptr);

}  // namespace p3::data
