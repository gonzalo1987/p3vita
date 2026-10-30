#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "p3data/image.hpp"

namespace p3::data {

// Decodifica una imagen AIM, el formato propio del motor (docs/formats/aim.md),
// a RGBA. El alfa se entrega tal cual viene en el fichero: la transparencia por
// color clave (magenta FF00FF) y las sombras son cosa del motor, no del formato.
bool decode_aim(const uint8_t* data, size_t size, Image& out, std::string* error = nullptr);

}  // namespace p3::data
