// Comprueba el lector de CPR con un archivo sintético, sin datos del juego.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"

using p3::data::CprArchive;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FALLO: %s\n", what);
		failures++;
	}
}

void put_u32(std::vector<uint8_t>& out, uint32_t value) {
	for (int i = 0; i < 4; ++i) {
		out.push_back(uint8_t(value >> (8 * i)));
	}
}

// Misma estructura que los archivos reales: cabecera de 32 bytes y un chunk
// con un índice de 4096 bytes seguido de los datos.
std::vector<uint8_t> build_archive(const std::vector<std::pair<std::string, std::string>>& files) {
	constexpr uint32_t kIndexSize = 0x1000;
	std::vector<uint8_t> out;
	const char header[] = "ASCARON_ARCHIVE V0.9";
	out.insert(out.end(), header, header + std::strlen(header));
	out.resize(0x20, 0);

	std::vector<uint8_t> entries;
	std::vector<uint8_t> data;
	const uint32_t data_start = 0x20 + kIndexSize;
	for (const auto& [path, content] : files) {
		put_u32(entries, data_start + uint32_t(data.size()));
		put_u32(entries, uint32_t(content.size()));
		put_u32(entries, 1);
		entries.insert(entries.end(), path.begin(), path.end());
		entries.push_back(0);
		data.insert(data.end(), content.begin(), content.end());
	}

	put_u32(out, kIndexSize);
	put_u32(out, 16 + uint32_t(entries.size()));
	put_u32(out, uint32_t(files.size()));
	put_u32(out, uint32_t(data.size()));
	out.insert(out.end(), entries.begin(), entries.end());
	out.resize(0x20 + kIndexSize, 0);
	out.insert(out.end(), data.begin(), data.end());
	return out;
}

std::string write_temp(const std::string& name, const std::vector<uint8_t>& bytes) {
	const std::string path = (std::filesystem::temp_directory_path() / name).string();
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
	return path;
}

}  // namespace

int main() {
	// "M\xFC" es "Mü" en Latin-1.
	const std::string path = write_temp("p3vita_cpr_test.cpr", build_archive({
		{"images\\Module_Stadtkarte\\Tile.aim", "AIM!"},
		{"musik\\M\xFC" "nchen.wav", "RIFF"},
	}));

	std::string error;
	auto archive = CprArchive::open(path, &error);
	check(archive != nullptr, "abre el archivo sintético");
	if (archive) {
		check(archive->version() == "V0.9", "lee la versión");
		check(archive->entries().size() == 2, "lee las dos entradas");

		const auto* tile = archive->find("IMAGES/module_stadtkarte/TILE.AIM");
		check(tile != nullptr, "find ignora mayúsculas y separadores");
		std::vector<uint8_t> content;
		check(tile && archive->read(*tile, content) && content.size() == 4 && std::memcmp(content.data(), "AIM!", 4) == 0,
			"read devuelve el contenido");

		check(archive->find("musik/M\xDC" "NCHEN.wav") != nullptr, "find ignora mayúsculas Latin-1");
		check(p3::data::latin1_to_utf8("M\xFC") == "M\xC3\xBC", "convierte Latin-1 a UTF-8");
		check(archive->find("no/existe") == nullptr, "find devuelve nullptr si no existe");
	}

	std::vector<uint8_t> bad = build_archive({{"a", "b"}});
	bad[0] = 'X';
	check(CprArchive::open(write_temp("p3vita_cpr_bad.cpr", bad)) == nullptr, "rechaza una firma incorrecta");

	std::vector<uint8_t> truncated = build_archive({{"a", "contenido"}});
	truncated.resize(truncated.size() - 4);
	check(CprArchive::open(write_temp("p3vita_cpr_trunc.cpr", truncated)) == nullptr, "rechaza un archivo truncado");

	if (failures == 0) {
		std::printf("cpr_test: todo correcto\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
