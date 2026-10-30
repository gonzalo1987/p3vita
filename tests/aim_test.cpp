// Comprueba el decodificador AIM con datos sintéticos, sin datos del juego.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "p3data/aim.hpp"
#include "p3data/lz.hpp"

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

// Escribe el flujo de bits LSB primero que espera el descompresor.
class LzWriter {
public:
	explicit LzWriter(int32_t output_size) { put_u32(bytes_, uint32_t(output_size)); }

	void literal(uint8_t value) {
		bits(0, 1);
		bits(value, 8);
	}

	void match(uint32_t distance, uint32_t length) {
		bits(1, 1);
		const uint32_t d = distance - 1;
		uint32_t c = 0;
		while (d >= (4u << c) - 2) {
			c++;
		}
		bits(c, 3);
		bits(d - ((2u << c) - 2), int(c) + 1);
		uint32_t rest = length - 2;
		for (int width = 2;; ++width) {
			const uint32_t all_ones = (1u << width) - 1;
			if (rest < all_ones) {
				bits(rest, width);
				break;
			}
			bits(all_ones, width);
			rest -= all_ones;
		}
	}

	std::vector<uint8_t> finish() {
		if (count_ % 8 != 0) {
			bytes_.push_back(pending_);
		}
		return bytes_;
	}

private:
	void bits(uint32_t value, int count) {
		for (int i = 0; i < count; ++i) {
			pending_ |= uint8_t(((value >> i) & 1) << (count_ % 8));
			if (++count_ % 8 == 0) {
				bytes_.push_back(pending_);
				pending_ = 0;
			}
		}
	}

	std::vector<uint8_t> bytes_;
	uint8_t pending_ = 0;
	int count_ = 0;
};

std::string decompress(const std::vector<uint8_t>& blob, size_t capacity, bool* ok = nullptr) {
	std::vector<uint8_t> out(capacity);
	size_t produced = 0;
	const bool result = p3::data::lz_decompress(blob.data(), blob.size(), out.data(), out.size(), produced);
	if (ok) {
		*ok = result;
	}
	return std::string(out.begin(), out.begin() + long(produced));
}

void test_lz() {
	LzWriter pattern(8);
	pattern.literal('a');
	pattern.literal('b');
	pattern.match(2, 6);
	check(decompress(pattern.finish(), 8) == "abababab", "coincidencia solapada repite el patrón");

	// 20 bytes: longitud 19 = 2 + 3 + 7 + 7, encadena campos de 2, 3 y 4 bits.
	LzWriter run(20);
	run.literal('x');
	run.match(1, 19);
	check(decompress(run.finish(), 20) == std::string(20, 'x'), "longitud con campos encadenados");

	// Distancia de la clase más alta (255..510).
	LzWriter far(302);
	for (int i = 0; i < 300; ++i) {
		far.literal(uint8_t(i));
	}
	far.match(300, 2);
	const std::string far_out = decompress(far.finish(), 302);
	check(far_out.size() == 302 && far_out[300] == 0 && far_out[301] == 1, "distancia larga");

	std::vector<uint8_t> stored;
	put_u32(stored, uint32_t(-3));
	stored.insert(stored.end(), {'x', 'y', 'z'});
	check(decompress(stored, 3) == "xyz", "bloque almacenado");

	std::vector<uint8_t> empty;
	put_u32(empty, 0);
	bool ok = false;
	check(decompress(empty, 4, &ok).empty() && ok, "bloque vacío");

	LzWriter before_start(4);
	before_start.literal('a');
	before_start.match(2, 3);
	decompress(before_start.finish(), 4, &ok);
	check(!ok, "rechaza una referencia anterior al inicio");

	LzWriter too_long(3);
	too_long.literal('a');
	too_long.match(1, 5);
	decompress(too_long.finish(), 3, &ok);
	check(!ok, "rechaza una coincidencia que se sale del bloque");

	LzWriter truncated(10);
	truncated.literal('a');
	decompress(truncated.finish(), 10, &ok);
	check(!ok, "rechaza un flujo truncado");
}

// Bloque {u32 N; blob} con los datos almacenados sin comprimir.
void put_stored_block(std::vector<uint8_t>& out, const std::vector<uint8_t>& data) {
	put_u32(out, uint32_t(4 + data.size()));
	put_u32(out, uint32_t(-int32_t(data.size())));
	out.insert(out.end(), data.begin(), data.end());
}

void test_indexed_file() {
	// Capa tipo 34 de 3x2 con stride 4: la columna de relleno debe ignorarse.
	std::vector<uint8_t> layer = {34, 0, 0, 0, 0, 8, 8, 8, 8};
	put_u32(layer, 3);
	put_u32(layer, 2);
	put_u32(layer, 4);
	std::vector<uint8_t> palette(1024, 0);
	const uint8_t red[4] = {0x00, 0x00, 0xFF, 0xFF};      // B, G, R, A
	const uint8_t magenta[4] = {0xFF, 0x00, 0xFF, 0x00};
	std::memcpy(&palette[1 * 4], red, 4);
	std::memcpy(&palette[2 * 4], magenta, 4);
	put_stored_block(layer, palette);
	put_stored_block(layer, {1, 2, 1, 9, 2, 1, 2, 9});
	const uint32_t layer_size = uint32_t(layer.size());
	std::memcpy(&layer[1], &layer_size, 4);

	std::vector<uint8_t> file = {'A', 'I', 'M', 0, 3};
	put_u32(file, uint32_t(0x25 + layer.size()));
	const char name[32] = "lala";
	file.insert(file.end(), name, name + 32);
	file.insert(file.end(), layer.begin(), layer.end());

	p3::data::Image image;
	std::string error;
	const bool ok = p3::data::decode_aim(file.data(), file.size(), image, &error);
	check(ok, "decodifica un AIM tipo 34 sintético");
	if (ok) {
		check(image.width == 3 && image.height == 2, "dimensiones del tipo 34");
		const uint8_t expected_red[4] = {0xFF, 0x00, 0x00, 0xFF};
		const uint8_t expected_magenta[4] = {0xFF, 0x00, 0xFF, 0x00};
		check(std::memcmp(&image.rgba[0], expected_red, 4) == 0, "paleta B,G,R,A pasa a R,G,B,A");
		check(std::memcmp(&image.rgba[4], expected_magenta, 4) == 0, "conserva el alfa de la paleta");
		check(std::memcmp(&image.rgba[3 * 4], expected_magenta, 4) == 0, "salta el relleno de fila");
	}

	file[0] = 'X';
	check(!p3::data::decode_aim(file.data(), file.size(), image), "rechaza una firma incorrecta");
}

}  // namespace

int main() {
	test_lz();
	test_indexed_file();
	if (failures == 0) {
		std::printf("aim_test: todo correcto\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
