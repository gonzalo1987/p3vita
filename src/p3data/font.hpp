#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/image.hpp"

namespace p3::data {

// Fuente TrueType del juego (fonts/*.ttf, ver scripts/ttffonts.ini) rasterizada
// a un tamaño en un atlas blanco con alfa. Cubre Latin-1, que es lo que usan
// los textos del juego en español (ñ, ¿, ¡, acentos).
class FontAtlas {
public:
	// Trozo del atlas que hay que dibujar para un carácter.
	struct Quad {
		int src_x;
		int src_y;
		int width;
		int height;
		int dst_x;
		int dst_y;
	};

	// `pixel_height` es la altura de línea, como el segundo número de Params en
	// ttffonts.ini.
	static std::unique_ptr<FontAtlas> create(const std::vector<uint8_t>& ttf, float pixel_height,
		std::string* error = nullptr);

	static std::unique_ptr<FontAtlas> load(const CprArchive& archive, const std::string& path, float pixel_height,
		std::string* error = nullptr);

	const Image& image() const { return image_; }
	int ascent() const { return ascent_; }
	int line_height() const { return line_height_; }

	// Coloca un texto en UTF-8 con la esquina superior izquierda de la línea en
	// (x, y). Los caracteres fuera de Latin-1 salen como '?'.
	std::vector<Quad> layout(std::string_view utf8, int x, int y) const;
	int measure(std::string_view utf8) const;

private:
	struct Glyph {
		int x0 = 0;
		int y0 = 0;
		int x1 = 0;
		int y1 = 0;
		float x_offset = 0;
		float y_offset = 0;
		float advance = 0;
	};

	FontAtlas() = default;

	Image image_;
	std::array<Glyph, 256> glyphs_{};
	int ascent_ = 0;
	int line_height_ = 0;
};

// Nombres de las 40 ciudades en el idioma de los datos (scripts/NamenStaedte_*.txt),
// en UTF-8 y en el orden de sus ids. Vacío si no se encuentran.
std::vector<std::string> load_town_names(const CprArchive& archive);

}  // namespace p3::data
