#include "p3data/cpr.hpp"

#include <cstring>

namespace p3::data {

namespace {

constexpr char kMagic[] = "ASCARON_ARCHIVE ";
constexpr size_t kMagicSize = sizeof(kMagic) - 1;
constexpr long kFirstChunk = 0x20;
constexpr uint32_t kChunkHeaderSize = 16;
constexpr uint32_t kEntryFixedSize = 12;

uint32_t read_u32_le(const uint8_t* p) {
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool fail(std::string* error, std::string message) {
	if (error) {
		*error = std::move(message);
	}
	return false;
}

}  // namespace

std::unique_ptr<CprArchive> CprArchive::open(const std::string& file_path, std::string* error) {
	std::unique_ptr<CprArchive> archive(new CprArchive());
	archive->file_.reset(std::fopen(file_path.c_str(), "rb"));
	std::FILE* f = archive->file_.get();
	if (!f) {
		fail(error, "no se puede abrir " + file_path);
		return nullptr;
	}

	if (std::fseek(f, 0, SEEK_END) != 0) {
		fail(error, "no se puede obtener el tamaño de " + file_path);
		return nullptr;
	}
	const long file_size = std::ftell(f);

	// Cabecera: 16 bytes de firma y la versión como cadena con relleno de ceros.
	uint8_t header[kFirstChunk];
	if (file_size < kFirstChunk || std::fseek(f, 0, SEEK_SET) != 0 ||
		std::fread(header, 1, sizeof(header), f) != sizeof(header)) {
		fail(error, "cabecera incompleta");
		return nullptr;
	}
	if (std::memcmp(header, kMagic, kMagicSize) != 0) {
		fail(error, "no es un archivo CPR (firma incorrecta)");
		return nullptr;
	}
	const char* version = reinterpret_cast<const char*>(header + kMagicSize);
	const size_t version_max = sizeof(header) - kMagicSize;
	const void* version_end = std::memchr(version, 0, version_max);
	archive->version_.assign(version,
		version_end ? static_cast<const char*>(version_end) - version : version_max);

	// Cada chunk: cabecera de 16 bytes, índice y datos. `index_size` incluye la
	// cabecera del chunk, así que los datos empiezan en chunk_pos + index_size.
	long chunk_pos = kFirstChunk;
	std::vector<uint8_t> index;
	while (chunk_pos < file_size) {
		uint8_t chunk_header[kChunkHeaderSize];
		if (std::fseek(f, chunk_pos, SEEK_SET) != 0 ||
			std::fread(chunk_header, 1, sizeof(chunk_header), f) != sizeof(chunk_header)) {
			fail(error, "cabecera de chunk incompleta en " + std::to_string(chunk_pos));
			return nullptr;
		}
		// El segundo campo parece ser los bytes usados del índice (cabecera
		// incluida); no hace falta para leerlo.
		const uint32_t index_size = read_u32_le(chunk_header + 0);
		const uint32_t files = read_u32_le(chunk_header + 8);
		const uint32_t data_size = read_u32_le(chunk_header + 12);

		if (index_size < kChunkHeaderSize || chunk_pos + long(index_size) + long(data_size) > file_size) {
			fail(error, "chunk con tamaños inválidos en " + std::to_string(chunk_pos));
			return nullptr;
		}

		index.resize(index_size - kChunkHeaderSize);
		if (std::fread(index.data(), 1, index.size(), f) != index.size()) {
			fail(error, "índice incompleto en " + std::to_string(chunk_pos));
			return nullptr;
		}

		size_t p = 0;
		for (uint32_t i = 0; i < files; ++i) {
			if (p + kEntryFixedSize >= index.size()) {
				fail(error, "entrada de índice fuera del chunk en " + std::to_string(chunk_pos));
				return nullptr;
			}
			CprEntry entry;
			entry.offset = read_u32_le(&index[p + 0]);
			entry.size = read_u32_le(&index[p + 4]);
			entry.unknown = read_u32_le(&index[p + 8]);
			p += kEntryFixedSize;

			const void* end = std::memchr(&index[p], 0, index.size() - p);
			if (!end) {
				fail(error, "ruta sin terminador en " + std::to_string(chunk_pos));
				return nullptr;
			}
			const size_t length = static_cast<const uint8_t*>(end) - &index[p];
			entry.path.assign(reinterpret_cast<const char*>(&index[p]), length);
			p += length + 1;

			if (long(entry.offset) + long(entry.size) > file_size) {
				fail(error, "la entrada " + latin1_to_utf8(entry.path) + " sale del archivo");
				return nullptr;
			}
			archive->index_by_path_.emplace(normalize_path(entry.path), archive->entries_.size());
			archive->entries_.push_back(std::move(entry));
		}

		chunk_pos += long(index_size) + long(data_size);
	}

	return archive;
}

const CprEntry* CprArchive::find(std::string_view path) const {
	auto it = index_by_path_.find(normalize_path(path));
	return it == index_by_path_.end() ? nullptr : &entries_[it->second];
}

bool CprArchive::read(const CprEntry& entry, std::vector<uint8_t>& out) const {
	out.resize(entry.size);
	std::FILE* f = file_.get();
	return std::fseek(f, long(entry.offset), SEEK_SET) == 0 &&
		std::fread(out.data(), 1, out.size(), f) == out.size();
}

std::string normalize_path(std::string_view path) {
	std::string key(path);
	for (char& c : key) {
		const uint8_t u = static_cast<uint8_t>(c);
		if (c == '\\') {
			c = '/';
		} else if ((u >= 'A' && u <= 'Z') || (u >= 0xC0 && u <= 0xDE && u != 0xD7)) {
			c = static_cast<char>(u + 0x20);
		}
	}
	return key;
}

std::string latin1_to_utf8(std::string_view text) {
	std::string out;
	out.reserve(text.size());
	for (char c : text) {
		const uint8_t u = static_cast<uint8_t>(c);
		if (u < 0x80) {
			out.push_back(c);
		} else {
			out.push_back(static_cast<char>(0xC0 | (u >> 6)));
			out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
		}
	}
	return out;
}

}  // namespace p3::data
