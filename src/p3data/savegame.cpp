#include "p3data/savegame.hpp"

#include <cstring>

#include "p3data/lz.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kMagic = 0xAFFE;
constexpr int kSupportedVersion = 0x82;
constexpr size_t kSettingsSize = 0x4C;
constexpr uint8_t kPartSeparator = 0x7F;
constexpr size_t kTownTableSize = 40;
constexpr uint32_t kMapWidth = 128;
constexpr uint32_t kMapHeight = 226;

const char kTruncated[] = "partida truncada";

bool fail(std::string* error, const std::string& message) {
	if (error) {
		*error = message;
	}
	return false;
}

// Lectura secuencial con comprobación de límites.
class Reader {
public:
	Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

	size_t position() const { return pos_; }
	bool ok() const { return ok_; }

	uint8_t u8() { return ensure(1) ? data_[pos_++] : 0; }

	uint16_t u16() {
		if (!ensure(2)) {
			return 0;
		}
		const uint16_t v = uint16_t(data_[pos_] | data_[pos_ + 1] << 8);
		pos_ += 2;
		return v;
	}

	uint32_t u32() {
		if (!ensure(4)) {
			return 0;
		}
		const uint8_t* p = data_ + pos_;
		pos_ += 4;
		return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
	}

	void skip(size_t n) {
		if (ensure(n)) {
			pos_ += n;
		}
	}

	template <size_t N>
	void u32s(std::array<uint32_t, N>& out) {
		for (uint32_t& v : out) {
			v = u32();
		}
	}

	template <size_t N>
	void bytes(std::array<uint8_t, N>& out) {
		if (ensure(N)) {
			std::memcpy(out.data(), data_ + pos_, N);
			pos_ += N;
		}
	}

	// El juego salta sin mirar un byte entre las partes de cada ciudad; en las
	// partidas de esta versión siempre vale 0x7F y sirve para comprobar el recorrido.
	bool separator() { return u8() == kPartSeparator && ok_; }

private:
	bool ensure(size_t n) {
		if (!ok_ || size_ - pos_ < n) {
			ok_ = false;
			return false;
		}
		return true;
	}

	const uint8_t* data_;
	size_t size_;
	size_t pos_ = 0;
	bool ok_ = true;
};

// Bloque de una ciudad. Los tamaños fijos corresponden a la versión 0x82.
bool parse_town(Reader& r, SavedTown& town, std::string* error) {
	const std::string where = " en la ciudad que empieza en " + std::to_string(r.position());

	// Parte 1: almacén (0x2C0 bytes, struct storage de P3Modding) y datos
	// generales de la ciudad (struct town desde 0x2C0).
	if (!r.separator()) {
		return fail(error, "separador inesperado" + where);
	}
	r.skip(4);
	r.u32s(town.wares);
	r.u32s(town.business_consumption);
	r.u32s(town.production);
	r.skip(0x2C0 - 4 - 3 * kWareCount * 4);
	r.skip(1);  // Índice en la tabla de ciudades.
	town.town_id = r.u8();
	r.skip(2 + 2 + 2);
	r.skip(4 + 4 + 4);  // Flags, sin identificar y fecha de la última fiesta.
	town.citizens = r.u32();
	r.skip(16 + 16 + 8 + 8 + 8);  // Habitantes por clase, anteriores, viviendas y satisfacción.
	r.u32s(town.citizen_consumption);  // [0x310]
	r.skip(4 * 0x60 + 0x180 + 0x60 + 4);

	// Parte 2: 20 bytes sin identificar.
	if (!r.separator()) {
		return fail(error, "separador inesperado (parte 2)" + where);
	}
	r.skip(20);

	// Parte 3: cabecera de 52 bytes, iglesia y registros de edificio.
	if (!r.separator()) {
		return fail(error, "separador inesperado (parte 3)" + where);
	}
	const uint16_t record_count = r.u16();
	r.skip(2);  // Lista de registros libres.
	town.drawn_lists[0] = r.u16();
	town.drawn_lists[1] = r.u16();  // Edificios.
	town.drawn_lists[3] = r.u16();
	town.drawn_lists[2] = r.u16();  // Murallas, puertas y torres.
	r.skip(2);  // Lista que el juego no dibuja (murallas antiguas).
	r.skip(3);
	town.flags = r.u32();
	r.skip(2 + 5 + 10 + 2 + 12);
	r.skip(15);  // Iglesia: el byte 16 es su nivel.
	town.church_level = r.u8();
	town.records.resize(record_count);
	for (BuildingRecord& record : town.records) {
		record.x = r.u8();
		record.y = r.u8();
		record.owner = r.u8();
		record.type = r.u8();
		record.next = r.u16();
		record.pending = r.u8();
		record.param = r.u8();
	}

	// Posiciones fijas y rejilla de zonas de la ciudad.
	const uint32_t width = r.u32();
	const uint32_t height = r.u32();
	if (r.ok() && (width != kMapWidth || height != kMapHeight)) {
		return fail(error, "rejilla de ciudad de tamaño inesperado" + where);
	}
	r.bytes(town.fixed.walls);
	r.bytes(town.fixed.lighthouses);
	r.bytes(town.fixed.cranes);
	r.bytes(town.fixed.market_stalls);
	r.bytes(town.fixed.stall_models);
	r.bytes(town.fixed.sites);
	r.bytes(town.fixed.shipyards);
	r.skip(size_t(width) * height);

	// Partes 4 y 5, instalaciones y nodos: aún no se usan.
	if (!r.separator()) {
		return fail(error, "separador inesperado (parte 4)" + where);
	}
	r.skip(26);
	if (!r.separator()) {
		return fail(error, "separador inesperado (parte 5)" + where);
	}
	r.skip(136);
	if (!r.separator()) {
		return fail(error, "separador inesperado (instalaciones)" + where);
	}
	r.skip(21 * 16);
	r.skip(83);
	const uint16_t node_count = r.u16();
	r.skip(size_t(node_count) * 16);
	return r.ok() || fail(error, kTruncated + where);
}

}  // namespace

bool parse_savegame(const uint8_t* data, size_t size, SaveGame& out, std::string* error) {
	Reader r(data, size);
	const uint32_t tag = r.u32();
	if (!r.ok() || (tag & 0xFFFF) != kMagic) {
		return fail(error, "no es una partida .pat");
	}
	out = SaveGame();
	out.version = int(tag >> 16);
	if (out.version != kSupportedVersion) {
		return fail(error, "versión de partida no soportada: " + std::to_string(out.version));
	}

	r.skip(kSettingsSize + 3 * 4);
	if (r.u32() != kMagic) {
		return fail(error, "falta el separador antes del mundo");
	}

	// Cabecera del mundo.
	r.skip(2);
	out.start_year = r.u16();
	r.skip(2);
	const uint32_t town_count = r.u32();
	r.skip(4);  // Registros sin identificar y oficinas.
	out.merchant_count = r.u16();
	r.skip(4 + 8 + 4);
	std::array<uint8_t, kTownTableSize> town_ids{};
	std::array<uint8_t, kTownTableSize> town_maps{};
	r.bytes(town_ids);
	r.bytes(town_maps);
	r.skip(2 + 7 * 12);
	if (!r.ok() || town_count > kTownTableSize) {
		return fail(error, "cabecera del mundo inválida");
	}

	out.towns.resize(town_count);
	for (size_t i = 0; i < town_count; ++i) {
		SavedTown& town = out.towns[i];
		if (!parse_town(r, town, error)) {
			return false;
		}
		if (town.town_id != town_ids[i]) {
			return fail(error, "la ciudad " + std::to_string(i) + " no coincide con la tabla del mundo");
		}
		town.template_map = town_maps[i];
	}
	return true;
}

bool load_savegame(const CprArchive& archive, const std::string& path, SaveGame& out, std::string* error) {
	const CprEntry* entry = archive.find(path);
	std::vector<uint8_t> packed;
	std::vector<uint8_t> data;
	if (!entry || !archive.read(*entry, packed)) {
		return fail(error, "no se puede leer " + path);
	}
	if (!lz_decompress_file(packed.data(), packed.size(), data, error)) {
		return false;
	}
	return parse_savegame(data.data(), data.size(), out, error);
}

}  // namespace p3::data
