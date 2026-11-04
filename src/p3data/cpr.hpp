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
	size_t file = 0;  // Qué fichero montado la contiene.
};

// Archivo CPR de Ascaron (p2arch0_*.cpr, p2arch1_*.cpr).
//
// Solo se carga el índice en memoria; el contenido se lee bajo demanda,
// porque p2arch0 ocupa más de 200 MiB y la Vita tiene 512 MiB de RAM.
class CprArchive {
public:
	// Devuelve nullptr y describe el fallo en `error` si no se puede leer.
	static std::unique_ptr<CprArchive> open(const std::string& file_path, std::string* error = nullptr);

	// Monta otro CPR encima, como hace el juego con el de cada idioma sobre el
	// inglés: sus entradas sustituyen a las que tengan la misma ruta.
	bool mount(const std::string& file_path, std::string* error = nullptr);

	// Cadena de versión de la cabecera del primer fichero, por ejemplo "V0.9".
	const std::string& version() const { return version_; }
	// Una entrada por ruta: la del último fichero montado que la tenga.
	const std::vector<CprEntry>& entries() const { return entries_; }

	// Busca sin distinguir mayúsculas y aceptando '/' o '\' como separador.
	const CprEntry* find(std::string_view path) const;

	bool read(const CprEntry& entry, std::vector<uint8_t>& out) const;
	// Only `size` bytes of the entry from `offset` (the CPR doesn't compress:
	// a part of a big file without reading it all); false past its end.
	bool read_range(const CprEntry& entry, uint32_t offset, uint32_t size, std::vector<uint8_t>& out) const;

private:
	struct FileCloser {
		void operator()(std::FILE* f) const { std::fclose(f); }
	};

	using File = std::unique_ptr<std::FILE, FileCloser>;

	CprArchive() = default;

	// Lee el índice de un fichero; las entradas salen con `file` = `file_index`.
	static bool read_index(std::FILE* f, size_t file_index, std::string& version, std::vector<CprEntry>& entries,
		std::string* error);

	std::vector<File> files_;
	std::string version_;
	std::vector<CprEntry> entries_;
	std::unordered_map<std::string, size_t> index_by_path_;
};

// Clave de búsqueda: minúsculas (también las letras Latin-1) y separador '/'.
std::string normalize_path(std::string_view path);

std::string latin1_to_utf8(std::string_view text);
// UTF-8 to Latin-1, as the game keeps the names (latin1_to_utf8 undone).
std::string utf8_to_latin1(std::string_view text);

}  // namespace p3::data
