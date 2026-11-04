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

// An uncompressed 8-bit BMP (the game's big maps, such as the sea map's 2400
// × 1800): its header and palette, to decode it in one pass through the
// palette straight into where it goes (decode_bmp8), or only some of its rows
// read apart. The result is decode_standard_image's (the same bytes).
struct Bmp8 {
	int width = 0;
	int height = 0;
	bool bottom_up = true;  // The file's first row is the image's last (the usual BMP).
	uint32_t pixels = 0;  // Where the rows start in the file.
	uint32_t stride = 0;  // Bytes per row in the file (the width rounded up to 4).
	uint32_t palette[256] = {};  // Each entry as an Image's 4 bytes (R, G, B, A 0xFF).

	// Where the image's row y starts in the file.
	uint32_t row(int y) const { return pixels + uint32_t(bottom_up ? height - 1 - y : y) * stride; }
};

// The header and the palette from the file's first bytes; false if it isn't an
// uncompressed 8-bit BMP (or `size` doesn't reach the palette's end).
bool parse_bmp8(const uint8_t* data, size_t size, Bmp8& out);

// The image's rows [y0, y0 + rows), columns [x0, x0 + width), into `out`
// (RGBA, `pitch` bytes from a row to the next). `file` holds the file's bytes
// from `file_offset` on, at least up to the end of those rows; the caller
// checks the ranges.
void decode_bmp8(const Bmp8& bmp, const uint8_t* file, uint32_t file_offset, int x0, int y0, int width, int rows,
	uint8_t* out, size_t pitch);

}  // namespace p3::data
