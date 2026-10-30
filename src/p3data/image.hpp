#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace p3::data {

// Imagen decodificada: RGBA de 8 bits por canal, filas seguidas sin relleno.
struct Image {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba;
};

// Decodifica BMP, TGA, JPEG o PNG desde memoria. Devuelve false y describe el
// fallo en `error` si el formato no se reconoce o los datos están dañados.
bool decode_standard_image(const uint8_t* data, size_t size, Image& out, std::string* error = nullptr);

// Vuelve transparentes los píxeles magenta FF00FF, el color clave de los tiles
// (campo TransparencyType de los .let).
void apply_magenta_key(Image& image);

}  // namespace p3::data
