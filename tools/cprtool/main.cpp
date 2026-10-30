// Lista y extrae el contenido de los archivos CPR de Patrician III.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"

namespace fs = std::filesystem;
using p3::data::CprArchive;
using p3::data::CprEntry;

namespace {

int usage() {
	std::fprintf(stderr,
		"uso: cprtool info    <archivo.cpr>\n"
		"     cprtool list    <archivo.cpr> [filtro]\n"
		"     cprtool extract <archivo.cpr> <directorio> [filtro]\n"
		"\n"
		"El filtro es un fragmento de ruta; no distingue mayúsculas ni separadores.\n");
	return 2;
}

bool matches(const CprEntry& entry, const std::string& filter) {
	return filter.empty() || p3::data::normalize_path(entry.path).find(filter) != std::string::npos;
}

int info(const CprArchive& archive) {
	uint64_t total = 0;
	std::map<std::string, std::pair<size_t, uint64_t>> by_extension;
	std::map<uint32_t, size_t> by_unknown;
	for (const CprEntry& entry : archive.entries()) {
		total += entry.size;
		const std::string key = p3::data::normalize_path(entry.path);
		const size_t dot = key.rfind('.');
		auto& ext = by_extension[dot == std::string::npos ? "(sin extensión)" : key.substr(dot)];
		ext.first++;
		ext.second += entry.size;
		by_unknown[entry.unknown]++;
	}

	std::printf("versión:  %s\n", archive.version().c_str());
	std::printf("archivos: %zu\n", archive.entries().size());
	std::printf("tamaño:   %.1f MiB\n\n", total / 1048576.0);
	std::printf("%-18s %8s %12s\n", "extensión", "archivos", "KiB");
	for (const auto& [ext, stats] : by_extension) {
		std::printf("%-18s %8zu %12.0f\n", ext.c_str(), stats.first, stats.second / 1024.0);
	}
	std::printf("\nvalores del campo desconocido de las entradas:\n");
	for (const auto& [value, count] : by_unknown) {
		std::printf("  0x%08x: %zu\n", value, count);
	}
	return 0;
}

int list(const CprArchive& archive, const std::string& filter) {
	for (const CprEntry& entry : archive.entries()) {
		if (matches(entry, filter)) {
			std::printf("%10u  %s\n", entry.size, p3::data::latin1_to_utf8(entry.path).c_str());
		}
	}
	return 0;
}

int extract(const CprArchive& archive, const fs::path& out_dir, const std::string& filter) {
	size_t count = 0;
	std::vector<uint8_t> data;
	for (const CprEntry& entry : archive.entries()) {
		if (!matches(entry, filter)) {
			continue;
		}
		std::string relative = p3::data::latin1_to_utf8(entry.path);
		for (char& c : relative) {
			if (c == '\\') {
				c = '/';
			}
		}
		const fs::path target = out_dir / fs::path(relative).lexically_normal();
		// Ninguna ruta del archivo debe poder escribir fuera del directorio de salida.
		const fs::path rel = target.lexically_relative(out_dir);
		if (rel.empty() || rel.is_absolute() || *rel.begin() == "..") {
			std::fprintf(stderr, "ruta insegura, se omite: %s\n", relative.c_str());
			continue;
		}

		if (!archive.read(entry, data)) {
			std::fprintf(stderr, "error al leer %s\n", relative.c_str());
			return 1;
		}
		fs::create_directories(target.parent_path());
		std::ofstream file(target, std::ios::binary);
		file.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
		if (!file) {
			std::fprintf(stderr, "error al escribir %s\n", target.c_str());
			return 1;
		}
		count++;
	}
	std::printf("%zu archivos extraídos en %s\n", count, out_dir.c_str());
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

	if (command == "info" && argc == 3) {
		return info(*archive);
	}
	if (command == "list" && argc <= 4) {
		return list(*archive, argc == 4 ? p3::data::normalize_path(argv[3]) : "");
	}
	if (command == "extract" && (argc == 4 || argc == 5)) {
		return extract(*archive, argv[3], argc == 5 ? p3::data::normalize_path(argv[4]) : "");
	}
	return usage();
}
