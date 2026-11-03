#include "p3data/lz.hpp"

#include <cstring>

namespace p3::data {

namespace {

// Límite de cordura para la cabecera de un fichero dañado.
constexpr int32_t kMaxFileOutput = 64 * 1024 * 1024;

const char kTruncated[] = "datos LZ truncados";

bool fail(std::string* error, const std::string& message) {
	if (error) {
		*error = message;
	}
	return false;
}

uint32_t read_u32_le(const uint8_t* p) {
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// Flujo de bits LSB primero: los bytes en orden, cada uno del bit 0 al 7.
// Equivale a las palabras u32 que lee la DLL.
class BitReader {
public:
	BitReader(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}

	// `count` <= 32.
	bool get(int count, uint32_t& value) {
		while (available_ < count) {
			if (p_ == end_) {
				return false;
			}
			acc_ |= uint64_t(*p_++) << available_;
			available_ += 8;
		}
		value = uint32_t(acc_ & ((uint64_t(1) << count) - 1));
		acc_ >>= count;
		available_ -= count;
		return true;
	}

private:
	const uint8_t* p_;
	const uint8_t* end_;
	uint64_t acc_ = 0;
	int available_ = 0;
};

}  // namespace

// With `prefix`, a blob bigger than `capacity` is decoded only up to it.
static bool decompress(const uint8_t* blob, size_t blob_size, uint8_t* out, size_t capacity, size_t& produced,
	std::string* error, bool prefix) {
	produced = 0;
	if (blob_size < 4) {
		return fail(error, "bloque LZ sin cabecera");
	}

	// n == 0: vacío; n < 0: -n bytes almacenados; n > 0: n bytes comprimidos.
	const int32_t n = int32_t(read_u32_le(blob));
	if (n == 0) {
		return true;
	}
	if (n < 0) {
		size_t count = size_t(-int64_t(n));
		if (prefix && count > capacity) {
			count = capacity;
		}
		if (count > blob_size - 4 || count > capacity) {
			return fail(error, "bloque LZ almacenado fuera de límites");
		}
		std::memcpy(out, blob + 4, count);
		produced = count;
		return true;
	}

	const size_t total = size_t(n);
	if (total > capacity && !prefix) {
		return fail(error, "bloque LZ mayor que el búfer de salida");
	}
	const size_t wanted = total < capacity ? total : capacity;
	BitReader bits(blob + 4, blob_size - 4);
	size_t o = 0;
	while (o < wanted) {
		uint32_t is_match = 0;
		if (!bits.get(1, is_match)) {
			return fail(error, kTruncated);
		}
		if (!is_match) {
			uint32_t literal = 0;
			if (!bits.get(8, literal)) {
				return fail(error, kTruncated);
			}
			out[o++] = uint8_t(literal);
			continue;
		}

		// Distancia: clase c de 3 bits y c+1 bits extra; va de 1 a 510.
		uint32_t distance_class = 0;
		uint32_t extra = 0;
		if (!bits.get(3, distance_class) || !bits.get(int(distance_class) + 1, extra)) {
			return fail(error, kTruncated);
		}
		const size_t distance = extra + (size_t(2) << distance_class) - 1;

		// Longitud: 2 más campos de 2, 3, 4... bits; sigue mientras el campo vale todo unos.
		uint64_t length = 2;
		for (int width = 2;; ++width) {
			uint32_t value = 0;
			if (width > 32 || !bits.get(width, value)) {
				return fail(error, "longitud LZ inválida");
			}
			length += value;
			if (value != (uint32_t(0xFFFFFFFF) >> (32 - width))) {
				break;
			}
		}

		if (distance > o || length > total - o) {
			return fail(error, "referencia LZ fuera del bloque");
		}
		// Byte a byte: si la distancia es menor que la longitud, repite el patrón.
		const size_t end = o + size_t(length) < wanted ? o + size_t(length) : wanted;
		for (; o < end; ++o) {
			out[o] = out[o - distance];
		}
	}
	produced = wanted;
	return true;
}

bool lz_decompress(const uint8_t* blob, size_t blob_size, uint8_t* out, size_t capacity, size_t& produced,
	std::string* error) {
	return decompress(blob, blob_size, out, capacity, produced, error, false);
}

bool lz_decompress_prefix(const uint8_t* blob, size_t blob_size, uint8_t* out, size_t capacity, size_t& produced) {
	return decompress(blob, blob_size, out, capacity, produced, nullptr, true);
}

bool lz_decompress_file(const uint8_t* data, size_t size, std::vector<uint8_t>& out, std::string* error) {
	if (size < 4) {
		return fail(error, "fichero LZ sin cabecera");
	}
	const int32_t n = int32_t(read_u32_le(data));
	if (n < -kMaxFileOutput || n > kMaxFileOutput) {
		return fail(error, "tamaño LZ declarado inválido");
	}
	out.resize(size_t(n < 0 ? -int64_t(n) : n));
	size_t produced = 0;
	if (!lz_decompress(data, size, out.data(), out.size(), produced, error)) {
		return false;
	}
	if (produced != out.size()) {
		return fail(error, "el fichero LZ no tiene el tamaño declarado");
	}
	return true;
}

}  // namespace p3::data
