#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace p3::data {

// Entrada del índice de un archivo CPR (ver docs/formats/cpr.md).
struct CprEntry {
	std::string path;  // Ruta tal cual está en el archivo: Latin-1 con separadores '\'.
	uint32_t offset = 0;  // Desde el inicio del archivo CPR.
	uint32_t size = 0;
	uint32_t unknown = 0;
};

// Archivo CPR de Ascaron (p2arch0_*.cpr, p2arch1_*.cpr).
//
// Solo se carga el índice en memoria; el contenido se lee bajo demanda,
// porque p2arch0 ocupa más de 200 MiB y la Vita tiene 512 MiB de RAM.
class CprArchive {
public:
	// Devuelve nullptr y describe el fallo en `error` si no se puede leer.
	static std::unique_ptr<CprArchive> open(const std::string& file_path, std::string* error = nullptr);

	// Cadena de versión de la cabecera, por ejemplo "V0.9".
	const std::string& version() const { return version_; }
	const std::vector<CprEntry>& entries() const { return entries_; }

	// Busca sin distinguir mayúsculas y aceptando '/' o '\' como separador.
	const CprEntry* find(std::string_view path) const;

	bool read(const CprEntry& entry, std::vector<uint8_t>& out) const;

private:
	struct FileCloser {
		void operator()(std::FILE* f) const { std::fclose(f); }
	};

	CprArchive() = default;

	std::unique_ptr<std::FILE, FileCloser> file_;
	std::string version_;
	std::vector<CprEntry> entries_;
	std::unordered_map<std::string, size_t> index_by_path_;
};

// Clave de búsqueda: minúsculas (también las letras Latin-1) y separador '/'.
std::string normalize_path(std::string_view path);

std::string latin1_to_utf8(std::string_view text);

}  // namespace p3::data
