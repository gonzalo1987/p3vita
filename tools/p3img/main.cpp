// Comprueba y exporta a PNG las imágenes guardadas en los archivos CPR.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "p3data/aim.hpp"
#include "p3data/cpr.hpp"
#include "p3data/font.hpp"
#include "p3data/image.hpp"
#include "stb_image_write.h"

using p3::data::CprArchive;
using p3::data::CprEntry;
using p3::data::Image;

namespace {

int usage() {
	std::fprintf(stderr,
		"uso: p3img check  <archivo.cpr> [filtro]\n"
		"     p3img digest <archivo.cpr> [filtro]\n"
		"     p3img png    <archivo.cpr> <ruta interna> <salida.png>\n"
		"     p3img text   <archivo.cpr> <fuente.ttf> <altura> <texto> <salida.png>\n"
		"\n"
		"check decodifica todas las imágenes (AIM, BMP, TGA, JPG y PNG) y resume los fallos.\n"
		"digest imprime el CRC32 de los píxeles RGBA de cada imagen, para comparar decodificadores.\n");
	return 2;
}

std::string extension(const CprEntry& entry) {
	const std::string key = p3::data::normalize_path(entry.path);
	const size_t dot = key.rfind('.');
	return dot == std::string::npos ? "" : key.substr(dot);
}

bool is_image(const std::string& ext) {
	return ext == ".aim" || ext == ".bmp" || ext == ".tga" || ext == ".jpg" || ext == ".png";
}

bool selected(const CprEntry& entry, const std::string& filter) {
	return is_image(extension(entry)) &&
		(filter.empty() || p3::data::normalize_path(entry.path).find(filter) != std::string::npos);
}

bool decode(const CprArchive& archive, const CprEntry& entry, Image& image, std::string& error) {
	std::vector<uint8_t> data;
	if (!archive.read(entry, data)) {
		error = "error de lectura";
		return false;
	}
	if (extension(entry) == ".aim") {
		return p3::data::decode_aim(data.data(), data.size(), image, &error);
	}
	return p3::data::decode_standard_image(data.data(), data.size(), image, &error);
}

// CRC-32 estándar (el de zlib), para cotejar con otras implementaciones.
uint32_t crc32(const std::vector<uint8_t>& data) {
	static uint32_t table[256];
	static bool ready = false;
	if (!ready) {
		for (uint32_t i = 0; i < 256; ++i) {
			uint32_t c = i;
			for (int k = 0; k < 8; ++k) {
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			}
			table[i] = c;
		}
		ready = true;
	}
	uint32_t crc = 0xFFFFFFFFu;
	for (uint8_t byte : data) {
		crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
	}
	return crc ^ 0xFFFFFFFFu;
}

int digest(const CprArchive& archive, const std::string& filter) {
	int status = 0;
	Image image;
	for (const CprEntry& entry : archive.entries()) {
		if (!selected(entry, filter)) {
			continue;
		}
		std::string error;
		const std::string path = p3::data::latin1_to_utf8(entry.path);
		if (decode(archive, entry, image, error)) {
			std::printf("%08x %d %d %s\n", crc32(image.rgba), image.width, image.height, path.c_str());
		} else {
			std::printf("ERROR 0 0 %s\n", path.c_str());
			status = 1;
		}
	}
	return status;
}

int check(const CprArchive& archive, const std::string& filter) {
	std::map<std::string, std::pair<size_t, size_t>> results;  // extensión -> (bien, mal)
	std::vector<std::string> failures;
	Image image;
	for (const CprEntry& entry : archive.entries()) {
		if (!selected(entry, filter)) {
			continue;
		}
		const std::string ext = extension(entry);
		std::string error;
		if (decode(archive, entry, image, error)) {
			results[ext].first++;
		} else {
			results[ext].second++;
			failures.push_back(p3::data::latin1_to_utf8(entry.path) + ": " + error);
		}
	}

	for (const auto& [ext, counts] : results) {
		std::printf("%-5s %6zu bien %6zu mal\n", ext.c_str(), counts.first, counts.second);
	}
	for (size_t i = 0; i < failures.size() && i < 20; ++i) {
		std::printf("  %s\n", failures[i].c_str());
	}
	return failures.empty() ? 0 : 1;
}

void write_to_stream(void* context, void* data, int size) {
	static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
}

bool write_png(const Image& image, const std::string& out_path) {
	std::ofstream file(out_path, std::ios::binary);
	return stbi_write_png_to_func(write_to_stream, &file, image.width, image.height, 4, image.rgba.data(),
			   image.width * 4) &&
		file;
}

// Texto blanco sobre fondo oscuro, para comprobar las fuentes.
int render_text(const CprArchive& archive, const std::string& font_path, float height, const std::string& text,
	const std::string& out_path) {
	std::string error;
	auto font = p3::data::FontAtlas::load(archive, font_path, height, &error);
	if (!font) {
		std::fprintf(stderr, "%s: %s\n", font_path.c_str(), error.c_str());
		return 1;
	}
	constexpr int kMargin = 6;
	Image canvas;
	canvas.width = font->measure(text) + 2 * kMargin;
	canvas.height = font->line_height() + 2 * kMargin;
	canvas.rgba.resize(size_t(canvas.width) * canvas.height * 4);
	for (size_t i = 0; i < canvas.rgba.size(); i += 4) {
		canvas.rgba[i] = 40;
		canvas.rgba[i + 1] = 32;
		canvas.rgba[i + 2] = 24;
		canvas.rgba[i + 3] = 255;
	}
	const Image& atlas = font->image();
	for (const auto& q : font->layout(text, kMargin, kMargin)) {
		for (int y = 0; y < q.height; ++y) {
			for (int x = 0; x < q.width; ++x) {
				const int dx = q.dst_x + x;
				const int dy = q.dst_y + y;
				if (dx < 0 || dy < 0 || dx >= canvas.width || dy >= canvas.height) {
					continue;
				}
				const int alpha = atlas.rgba[(size_t(q.src_y + y) * atlas.width + size_t(q.src_x + x)) * 4 + 3];
				uint8_t* dst = &canvas.rgba[(size_t(dy) * canvas.width + dx) * 4];
				for (int c = 0; c < 3; ++c) {
					dst[c] = uint8_t((255 * alpha + dst[c] * (255 - alpha)) / 255);
				}
			}
		}
	}
	if (!write_png(canvas, out_path)) {
		std::fprintf(stderr, "error al escribir %s\n", out_path.c_str());
		return 1;
	}
	std::printf("%s a %.0f px: %dx%d, atlas %dx%d\n", font_path.c_str(), height, canvas.width, canvas.height,
		atlas.width, atlas.height);
	return 0;
}

int export_png(const CprArchive& archive, const std::string& path, const std::string& out_path) {
	const CprEntry* entry = archive.find(path);
	if (!entry) {
		std::fprintf(stderr, "no existe en el archivo: %s\n", path.c_str());
		return 1;
	}
	Image image;
	std::string error;
	if (!decode(archive, *entry, image, error)) {
		std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
		return 1;
	}
	std::ofstream file(out_path, std::ios::binary);
	if (!stbi_write_png_to_func(write_to_stream, &file, image.width, image.height, 4, image.rgba.data(),
			image.width * 4) ||
		!file) {
		std::fprintf(stderr, "error al escribir %s\n", out_path.c_str());
		return 1;
	}
	std::printf("%s: %dx%d -> %s\n", path.c_str(), image.width, image.height, out_path.c_str());
	return 0;
}

}  // namespace

int main(int argc, char** argv) {
	if (argc < 3) {
		return usage();
	}
	const std::string command = argv[1];

	std::string error;
	auto archive = CprArchive::open(argv[2], &error);
	if (!archive) {
		std::fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
		return 1;
	}

	if (command == "check" && argc <= 4) {
		return check(*archive, argc == 4 ? p3::data::normalize_path(argv[3]) : "");
	}
	if (command == "digest" && argc <= 4) {
		return digest(*archive, argc == 4 ? p3::data::normalize_path(argv[3]) : "");
	}
	if (command == "png" && argc == 5) {
		return export_png(*archive, argv[3], argv[4]);
	}
	if (command == "text" && argc == 7) {
		return render_text(*archive, argv[3], float(std::atof(argv[4])), argv[5], argv[6]);
	}
	return usage();
}
