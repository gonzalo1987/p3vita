#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace p3::data {

// Compresión LZ de Ascaron (docs/formats/aim.md, §3). La usan las imágenes AIM
// por bloques y, como blob único, las partidas guardadas (.pat) y los mapas (.gmf).

// Descomprime un blob (s32 de cabecera y flujo de bits) en `out`, sin escribir
// más de `capacity` bytes. Devuelve en `produced` los bytes escritos.
bool lz_decompress(const uint8_t* blob, size_t blob_size, uint8_t* out, size_t capacity, size_t& produced,
	std::string* error = nullptr);

// Like lz_decompress, but a blob bigger than `capacity` is decoded only up to
// it (the start of a save, for its header): `produced` is what was written.
bool lz_decompress_prefix(const uint8_t* blob, size_t blob_size, uint8_t* out, size_t capacity, size_t& produced);

// Descomprime un fichero que es un único blob, reservando lo que declara su cabecera.
bool lz_decompress_file(const uint8_t* data, size_t size, std::vector<uint8_t>& out, std::string* error = nullptr);

}  // namespace p3::data
