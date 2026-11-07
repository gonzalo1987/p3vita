#include "p3data/aim.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include "p3data/lz.hpp"
#include "stb_image.h"

namespace p3::data {

namespace {

constexpr uint8_t kRootChunk = 3;
constexpr size_t kRootHeaderSize = 0x25;  // Tipo, tamaño y nombre de 32 bytes.
constexpr size_t kChunkHeaderSize = 5;
constexpr size_t kLzBlockSize = 0x10000;
constexpr size_t kPaletteSize = 256 * 4;
// Límite de cordura para no reservar memoria absurda con datos dañados.
constexpr uint32_t kMaxDimension = 16384;

const char kTruncated[] = "datos AIM truncados";

bool fail(std::string* error, const std::string& message) {
	if (error) {
		*error = message;
	}
	return false;
}

uint32_t read_u32_le(const uint8_t* p) {
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// Lectura secuencial con comprobación de límites dentro de una capa.
class ByteReader {
public:
	ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

	size_t remaining() const { return size_ - pos_; }

	bool skip(size_t n) {
		if (remaining() < n) {
			return false;
		}
		pos_ += n;
		return true;
	}

	bool u8(uint8_t& value) {
		if (remaining() < 1) {
			return false;
		}
		value = data_[pos_++];
		return true;
	}

	bool u32(uint32_t& value) {
		if (remaining() < 4) {
			return false;
		}
		value = read_u32_le(data_ + pos_);
		pos_ += 4;
		return true;
	}

	bool take(size_t n, const uint8_t*& p) {
		if (remaining() < n) {
			return false;
		}
		p = data_ + pos_;
		pos_ += n;
		return true;
	}

private:
	const uint8_t* data_;
	size_t size_;
	size_t pos_ = 0;
};

bool valid_size(uint32_t width, uint32_t height) {
	return width > 0 && height > 0 && width <= kMaxDimension && height <= kMaxDimension;
}

void init_image(Image& out, uint32_t width, uint32_t height) {
	out.width = int(width);
	out.height = int(height);
	out.rgba.assign(size_t(width) * height * 4, 0);
}

// Un bloque {u32 N; blob[N]} que debe producir exactamente `expected` bytes.
bool read_lz_block(ByteReader& reader, uint8_t* out, size_t expected, std::string* error) {
	uint32_t blob_size = 0;
	const uint8_t* blob = nullptr;
	if (!reader.u32(blob_size) || !reader.take(blob_size, blob)) {
		return fail(error, kTruncated);
	}
	size_t produced = 0;
	if (!lz_decompress(blob, blob_size, out, expected, produced, error)) {
		return false;
	}
	if (produced != expected) {
		return fail(error, "un bloque LZ no tiene el tamaño esperado");
	}
	return true;
}

// Las imágenes grandes van troceadas en bloques independientes de 64 KiB de salida.
bool read_lz_blocks(ByteReader& reader, uint8_t* out, size_t total, std::string* error) {
	for (size_t offset = 0; offset < total; offset += kLzBlockSize) {
		if (!read_lz_block(reader, out + offset, std::min(kLzBlockSize, total - offset), error)) {
			return false;
		}
	}
	return true;
}

// Tipo 34: índices de 8 bits y paleta de 256 colores B,G,R,A, ambos con LZ.
bool decode_indexed(ByteReader& reader, Image& out, std::string* error) {
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t stride = 0;
	// Los 4 bytes de bits por canal solo los usa el escritor.
	if (!reader.skip(4) || !reader.u32(width) || !reader.u32(height) || !reader.u32(stride)) {
		return fail(error, kTruncated);
	}
	if (!valid_size(width, height) || stride < width || stride > kMaxDimension) {
		return fail(error, "dimensiones de imagen indexada inválidas");
	}

	uint8_t palette[kPaletteSize];
	std::vector<uint8_t> indices(size_t(stride) * height);
	if (!read_lz_block(reader, palette, kPaletteSize, error) ||
		!read_lz_blocks(reader, indices.data(), indices.size(), error)) {
		return false;
	}

	init_image(out, width, height);
	uint8_t* dst = out.rgba.data();
	for (uint32_t y = 0; y < height; ++y) {
		const uint8_t* row = indices.data() + size_t(y) * stride;
		for (uint32_t x = 0; x < width; ++x, dst += 4) {
			const uint8_t* bgra = palette + row[x] * 4;
			dst[0] = bgra[2];
			dst[1] = bgra[1];
			dst[2] = bgra[0];
			dst[3] = bgra[3];
		}
	}
	return true;
}

// Tipo 22: 24 bits sin comprimir.
bool decode_rgb24(ByteReader& reader, Image& out, std::string* error) {
	uint8_t encoding = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t stride = 0;
	if (!reader.u8(encoding) || !reader.u32(width) || !reader.u32(height) || !reader.u32(stride)) {
		return fail(error, kTruncated);
	}
	if (!valid_size(width, height) || stride < width * 3 || stride > kMaxDimension * 3) {
		return fail(error, "dimensiones de imagen de 24 bits inválidas");
	}
	const uint8_t* pixels = nullptr;
	if (!reader.take(size_t(stride) * height, pixels)) {
		return fail(error, kTruncated);
	}

	// Codificación 1: R,G,B en memoria; cualquier otra: B,G,R.
	const int r = encoding == 1 ? 0 : 2;
	const int b = 2 - r;
	init_image(out, width, height);
	uint8_t* dst = out.rgba.data();
	for (uint32_t y = 0; y < height; ++y) {
		const uint8_t* src = pixels + size_t(y) * stride;
		for (uint32_t x = 0; x < width; ++x, src += 3, dst += 4) {
			dst[0] = src[r];
			dst[1] = src[1];
			dst[2] = src[b];
			dst[3] = 0xFF;
		}
	}
	return true;
}

// Tipo 33: 32 bits con LZ, entrelazado o separado en cuatro planos.
bool decode_bgra32(ByteReader& reader, Image& out, std::string* error) {
	uint8_t flags = 0;
	const uint8_t* bits = nullptr;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t stride = 0;
	if (!reader.u8(flags) || !reader.take(4, bits) || !reader.u32(width) || !reader.u32(height) ||
		!reader.u32(stride)) {
		return fail(error, kTruncated);
	}
	if (!valid_size(width, height) || stride < width * 4 || stride % 4 != 0 || stride > kMaxDimension * 4) {
		return fail(error, "dimensiones de imagen de 32 bits inválidas");
	}

	const bool interleaved = flags & 4;
	const bool rgba_order = flags & 1;
	std::vector<uint8_t> pixels(size_t(stride) * height);
	if (interleaved) {
		if (!read_lz_blocks(reader, pixels.data(), pixels.size(), error)) {
			return false;
		}
	} else {
		// Plano p = byte p de cada píxel; el troceado en bloques se reinicia en cada plano.
		const size_t plane_size = pixels.size() / 4;
		std::vector<uint8_t> block(std::min(plane_size, kLzBlockSize));
		for (size_t plane = 0; plane < 4; ++plane) {
			for (size_t offset = 0; offset < plane_size; offset += kLzBlockSize) {
				const size_t count = std::min(kLzBlockSize, plane_size - offset);
				if (!read_lz_block(reader, block.data(), count, error)) {
					return false;
				}
				for (size_t k = 0; k < count; ++k) {
					pixels[plane + 4 * (offset + k)] = block[k];
				}
			}
		}
	}

	// Ampliación a 8 bits de los canales guardados con menos profundidad: bits[0]
	// corresponde al byte 2 del píxel, bits[1] al 1, bits[2] al 0 y bits[3] al 3.
	const uint8_t byte_bits[4] = {bits[2], bits[1], bits[0], bits[3]};
	for (int j = 0; j < 4; ++j) {
		const int depth = byte_bits[j];
		if (depth == 8) {
			continue;
		}
		if (depth > 8) {
			return fail(error, "profundidad de canal inválida");
		}
		for (size_t k = size_t(j); k < pixels.size(); k += 4) {
			if (j == 3 && depth == 0) {
				pixels[k] = 0xFF;
			} else {
				uint8_t v = uint8_t(pixels[k] << (8 - depth));
				if (v & 0x80) {
					v |= uint8_t(0xFF >> depth);
				}
				pixels[k] = v;
			}
		}
	}

	const int r = rgba_order ? 0 : 2;
	const int b = 2 - r;
	init_image(out, width, height);
	uint8_t* dst = out.rgba.data();
	for (uint32_t y = 0; y < height; ++y) {
		const uint8_t* src = pixels.data() + size_t(y) * stride;
		for (uint32_t x = 0; x < width; ++x, src += 4, dst += 4) {
			dst[0] = src[r];
			dst[1] = src[1];
			dst[2] = src[b];
			dst[3] = src[3];
		}
	}
	return true;
}

void rgb565(uint16_t c, int rgb[3]) {
	// Como la DLL: sin replicar los bits altos (0x1F da 248, no 255).
	rgb[0] = (c >> 11) << 3;
	rgb[1] = ((c >> 5) & 0x3F) << 2;
	rgb[2] = (c & 0x1F) << 3;
}

// Bloques DXT3 de 4x4 a RGBA, con la misma interpolación que la DLL.
void decode_dxt3_blocks(const uint8_t* src, uint32_t width, uint32_t height, Image& out) {
	init_image(out, width, height);
	for (uint32_t by = 0; by < height; by += 4) {
		for (uint32_t bx = 0; bx < width; bx += 4, src += 16) {
			const uint16_t c0 = uint16_t(src[8] | src[9] << 8);
			const uint16_t c1 = uint16_t(src[10] | src[11] << 8);
			const uint32_t indices = read_u32_le(src + 12);

			int colors[4][3];
			rgb565(c0, colors[0]);
			rgb565(c1, colors[1]);
			for (int i = 0; i < 3; ++i) {
				if (c0 > c1) {
					colors[2][i] = (2 * colors[0][i] + colors[1][i]) / 3;
					colors[3][i] = (colors[0][i] + 2 * colors[1][i]) / 3;
				} else {
					colors[2][i] = (colors[0][i] + colors[1][i]) >> 1;
				}
			}
			if (c0 <= c1) {
				// Color fijo de la DLL para el índice 3 (no aparece en los datos).
				colors[3][0] = 0xFF;
				colors[3][1] = 0xFF;
				colors[3][2] = 0x00;
			}

			for (uint32_t y = 0; y < 4; ++y) {
				const uint16_t alpha_row = uint16_t(src[2 * y] | src[2 * y + 1] << 8);
				const uint32_t index_row = (indices >> (8 * y)) & 0xFF;
				uint8_t* dst = out.rgba.data() + ((size_t(by) + y) * width + bx) * 4;
				for (uint32_t x = 0; x < 4; ++x, dst += 4) {
					const int* color = colors[(index_row >> (2 * x)) & 3];
					dst[0] = uint8_t(color[0]);
					dst[1] = uint8_t(color[1]);
					dst[2] = uint8_t(color[2]);
					dst[3] = uint8_t(((alpha_row >> (4 * x)) & 0xF) * 0x11);
				}
			}
		}
	}
}

// Tipos 24 a 28: DXT1 a DXT5. Patrician III solo usa DXT3 (tipo 26).
bool decode_dxt(uint8_t layer_type, ByteReader& reader, Image& out, std::string* error) {
	uint8_t flags = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	if (!reader.u8(flags) || !reader.u32(width) || !reader.u32(height)) {
		return fail(error, kTruncated);
	}
	if (layer_type != 26) {
		return fail(error, "capa DXT tipo " + std::to_string(layer_type) +
			" no soportada (no aparece en los datos de Patrician III)");
	}
	if (!valid_size(width, height) || width % 4 != 0 || height % 4 != 0) {
		return fail(error, "dimensiones DXT3 inválidas");
	}
	const size_t needed = size_t(width) * height;  // DXT3: 16 bytes por bloque de 4x4.

	std::vector<uint8_t> blocks;
	const uint8_t* src = nullptr;
	if (flags & 1) {
		// Comprimido: tamaño total y bloques LZ que avanzan lo que declara cada uno.
		uint32_t total = 0;
		if (!reader.u32(total)) {
			return fail(error, kTruncated);
		}
		if (total != needed) {
			return fail(error, "tamaño DXT3 inesperado");
		}
		blocks.resize(total);
		for (size_t offset = 0; offset < total;) {
			uint32_t blob_size = 0;
			const uint8_t* blob = nullptr;
			if (!reader.u32(blob_size) || !reader.take(blob_size, blob)) {
				return fail(error, kTruncated);
			}
			size_t produced = 0;
			if (!lz_decompress(blob, blob_size, blocks.data() + offset, total - offset, produced, error)) {
				return false;
			}
			if (produced == 0) {
				return fail(error, "bloque LZ vacío en datos DXT3");
			}
			offset += produced;
		}
		src = blocks.data();
	} else if (!reader.take(needed, src)) {
		return fail(error, kTruncated);
	}

	decode_dxt3_blocks(src, width, height, out);
	return true;
}

// Tipo 30: JPEG completo tras una cabecera propia.
bool decode_jpeg(ByteReader& reader, Image& out, std::string* error) {
	uint8_t flags = 0;
	const uint8_t* thresholds = nullptr;
	uint32_t width = 0;
	uint32_t height = 0;
	// Tras los flags: calidad (informativa) y 4 bytes de umbrales.
	if (!reader.u8(flags) || !reader.skip(1) || !reader.take(4, thresholds) || !reader.u32(width) ||
		!reader.u32(height)) {
		return fail(error, kTruncated);
	}
	const size_t jpeg_size = reader.remaining();
	const uint8_t* jpeg = nullptr;
	if (!valid_size(width, height) || !reader.take(jpeg_size, jpeg)) {
		return fail(error, "cabecera JPEG inválida");
	}

	int jpeg_width = 0;
	int jpeg_height = 0;
	int channels = 0;
	stbi_uc* rgb = stbi_load_from_memory(jpeg, int(jpeg_size), &jpeg_width, &jpeg_height, &channels, 3);
	if (!rgb) {
		return fail(error, std::string("JPEG inválido: ") + stbi_failure_reason());
	}
	if (uint32_t(jpeg_width) != width || uint32_t(jpeg_height) != height) {
		stbi_image_free(rgb);
		return fail(error, "el JPEG no coincide con las dimensiones de la capa");
	}

	// Con el bit 0 el escritor guardó el JPEG con rojo y azul cruzados.
	const bool swapped = flags & 1;
	const bool use_thresholds = read_u32_le(thresholds) != 0;
	init_image(out, width, height);
	const uint8_t* src = rgb;
	uint8_t* dst = out.rgba.data();
	for (size_t i = 0; i < size_t(width) * height; ++i, src += 3, dst += 4) {
		uint8_t r = swapped ? src[2] : src[0];
		uint8_t g = src[1];
		uint8_t b = swapped ? src[0] : src[2];
		// Umbrales sobre B, G y R: lo que quede por debajo pasa a 0.
		if (use_thresholds) {
			b = b < thresholds[0] ? 0 : b;
			g = g < thresholds[1] ? 0 : g;
			r = r < thresholds[2] ? 0 : r;
		}
		dst[0] = r;
		dst[1] = g;
		dst[2] = b;
		dst[3] = 0xFF;
	}
	stbi_image_free(rgb);
	return true;
}

bool decode_layer(const uint8_t* data, size_t size, Image& out, std::string* error) {
	if (size < kChunkHeaderSize) {
		return fail(error, "el fichero AIM no contiene ninguna capa");
	}
	const uint8_t type = data[0];
	const uint32_t layer_size = read_u32_le(data + 1);
	if (layer_size < kChunkHeaderSize || layer_size > size) {
		return fail(error, "capa AIM con tamaño inválido");
	}

	ByteReader reader(data, layer_size);
	reader.skip(kChunkHeaderSize);
	switch (type) {
	case 22:
		return decode_rgb24(reader, out, error);
	case 24:
	case 25:
	case 26:
	case 27:
	case 28:
		return decode_dxt(type, reader, out, error);
	case 30:
		return decode_jpeg(reader, out, error);
	case 33:
		return decode_bgra32(reader, out, error);
	case 34:
		return decode_indexed(reader, out, error);
	default:
		return fail(error, "capa AIM tipo " + std::to_string(type) +
			" no soportada (no aparece en los datos de Patrician III)");
	}
}

}  // namespace

bool decode_aim_indices(const uint8_t* data, size_t size, uint32_t& width, uint32_t& height, uint32_t& pitch,
	std::vector<uint8_t>& indices, std::string* error) {
	if (size < 4 || std::memcmp(data, "AIM", 3) != 0) {
		return fail(error, "no es un fichero AIM");
	}
	for (size_t pos = 4; pos < size;) {
		if (size - pos < kChunkHeaderSize) {
			return fail(error, kTruncated);
		}
		const uint32_t chunk_size = read_u32_le(data + pos + 1);
		if (chunk_size < kChunkHeaderSize || chunk_size > size - pos) {
			return fail(error, "chunk AIM con tamaño inválido");
		}
		if (data[pos] != kRootChunk) {
			pos += chunk_size;
			continue;
		}
		if (chunk_size < kRootHeaderSize + kChunkHeaderSize) {
			return fail(error, kTruncated);
		}
		const uint8_t* layer = data + pos + kRootHeaderSize;
		const uint32_t layer_size = read_u32_le(layer + 1);
		if (layer[0] != 34) {
			return fail(error, "la capa AIM no es indexada (tipo 34)");
		}
		if (layer_size < kChunkHeaderSize || layer_size > chunk_size - kRootHeaderSize) {
			return fail(error, "capa AIM con tamaño inválido");
		}
		ByteReader reader(layer, layer_size);
		reader.skip(kChunkHeaderSize);
		uint32_t stride = 0;
		if (!reader.skip(4) || !reader.u32(width) || !reader.u32(height) || !reader.u32(stride)) {
			return fail(error, kTruncated);
		}
		if (!valid_size(width, height) || stride < width || stride > kMaxDimension) {
			return fail(error, "dimensiones de imagen indexada inválidas");
		}
		uint8_t palette[kPaletteSize];
		std::vector<uint8_t> rows(size_t(stride) * height);
		if (!read_lz_block(reader, palette, kPaletteSize, error) ||
			!read_lz_blocks(reader, rows.data(), rows.size(), error)) {
			return false;
		}
		pitch = (width + 3) & ~3u;
		indices.assign(size_t(pitch) * height, 0);
		for (uint32_t y = 0; y < height; ++y) {
			std::memcpy(indices.data() + size_t(y) * pitch, rows.data() + size_t(y) * stride, width);
		}
		return true;
	}
	return fail(error, "el fichero AIM no contiene ninguna imagen");
}

bool decode_aim(const uint8_t* data, size_t size, Image& out, std::string* error) {
	// La DLL solo compara las tres letras de la firma.
	if (size < 4 || std::memcmp(data, "AIM", 3) != 0) {
		return fail(error, "no es un fichero AIM");
	}

	for (size_t pos = 4; pos < size;) {
		if (size - pos < kChunkHeaderSize) {
			return fail(error, kTruncated);
		}
		const uint8_t type = data[pos];
		const uint32_t chunk_size = read_u32_le(data + pos + 1);
		if (chunk_size < kChunkHeaderSize || chunk_size > size - pos) {
			return fail(error, "chunk AIM con tamaño inválido");
		}
		if (type == kRootChunk) {
			// La capa de imagen va justo después de la cabecera de la raíz.
			if (chunk_size < kRootHeaderSize) {
				return fail(error, kTruncated);
			}
			return decode_layer(data + pos + kRootHeaderSize, chunk_size - kRootHeaderSize, out, error);
		}
		if (type == 4 || type == 5) {
			return fail(error, "raíz AIM tipo " + std::to_string(type) +
				" no soportada (no aparece en los datos de Patrician III)");
		}
		pos += chunk_size;
	}
	return fail(error, "el fichero AIM no contiene ninguna imagen");
}

}  // namespace p3::data
