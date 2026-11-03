#include "p3data/savegame.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "p3data/lz.hpp"
#include "p3data/savegame_load.hpp"
#include "p3data/savegame_write.hpp"
#include "p3data/thresholds.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kMagic = 0xAFFE;
// The versions read: 0x82 (missav/) to 0x85 (what the game writes). The
// readers of the game only branch above 0x82 for the free ship slots
// ([+0x137] from 0x83, parse_ship).
constexpr int kOldestVersion = 0x82;
constexpr int kNewestVersion = 0x85;
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

	int32_t s32() { return int32_t(u32()); }

	float f32() {
		const uint32_t bits = u32();
		float v;
		std::memcpy(&v, &bits, sizeof(v));
		return v;
	}

	// Cadena de tamaño fijo terminada en 0.
	std::string text(size_t n) {
		if (!ensure(n)) {
			return {};
		}
		const char* p = reinterpret_cast<const char*>(data_ + pos_);
		const void* end = std::memchr(p, 0, n);
		pos_ += n;
		return std::string(p, end ? size_t(static_cast<const char*>(end) - p) : n);
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
	void s32s(std::array<int32_t, N>& out) {
		for (int32_t& v : out) {
			v = s32();
		}
	}

	template <size_t N>
	void bytes(std::array<uint8_t, N>& out) {
		if (ensure(N)) {
			std::memcpy(out.data(), data_ + pos_, N);
			pos_ += N;
		}
	}

	// Copia n bytes del fichero en la memoria de un objeto del juego.
	template <size_t N>
	void to(GameMemory<N>& memory, size_t offset, size_t n) {
		if (ensure(n) && offset + n <= N) {
			std::memcpy(memory.bytes.data() + offset, data_ + pos_, n);
			pos_ += n;
		}
	}

	// The bytes read since `start` (a position()), for SaveOpaque.
	std::vector<uint8_t> since(size_t start) const {
		return ok_ && start <= pos_ ? std::vector<uint8_t>(data_ + start, data_ + pos_) : std::vector<uint8_t>();
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

// --- La memoria del juego (game_memory.hpp) ---
//
// Segunda lectura de cada bloque que deja los objetos como los deja el
// cargador del juego: cada trozo del fichero va a su offset en memoria.

// Umbrales de precio que recalcula el juego al terminar la parte 1 de la
// ciudad (0x527CC1, ver parse_town).
void recompute_thresholds(TownMemory& m, int month) {
	std::array<PriceThresholds, kWareCount> t;
	for (size_t w = 0; w < kWareCount; ++w) {
		for (size_t i = 0; i < 4; ++i) {
			t[w][i] = m.u32(0x4F0 + 16 * w + 4 * i);
		}
	}
	if (m.s32(0x4F0) > 1) {
		ThresholdInputs in;
		in.flags = m.u32(0x2C8);
		in.citizens = m.u32(0x2D4);
		for (size_t w = 0; w < kWareCount; ++w) {
			in.business_consumption[w] = m.u32(0x64 + 4 * w);
			in.production[w] = m.u32(0xC4 + 4 * w);
			in.citizen_consumption[w] = m.u32(0x310 + 4 * w);
			in.supply[w] = m.u32(0x490 + 4 * w);
		}
		in.month = month;
		in.brickworks = m.u16(0x968);
		t = compute_price_thresholds(in, t);
	} else {
		for (size_t w = 0; w < size_t(kTradeWareCount) - 1; ++w) {
			t[w] = {1, 2, 3, 4};
		}
	}
	for (size_t w = 0; w < kWareCount; ++w) {
		for (size_t i = 0; i < 4; ++i) {
			m.u32(0x4F0 + 16 * w + 4 * i) = t[w][i];
		}
	}
}

struct Chunk {
	size_t offset;
	size_t size;
};

// Cabecera del mundo (0x52FB60) en el objeto del mundo (0x6DE4A0).
//
// Before it, what the world's constructor (0x52F520, one of the static
// initializers the CRT runs at the game's start) leaves and nothing else
// writes: [+0x88] = 0xFF, the town of the AI's next feast (its tick [+0x84]
// stays 0). The save doesn't have them, and neither the reader nor the reset
// of a game (0x530230) writes them; only the feast (0x5316E0) does, after
// holding one, which needs [+0x88] to be a town. So in the game the AI's
// feasts never happen (a possible game bug, left as it is even with
// fix_game_bugs: see the roadmap, «Corregir los fallos del juego»).
void load_world_header(Reader r, GlobalMemory& g) {
	g.u8(global::kWorld + 0x88) = 0xFF;
	auto to = [&](uint32_t offset, size_t n) {
		for (size_t i = 0; i < n; ++i) {
			g.u8(global::kWorld + offset + uint32_t(i)) = r.u8();
		}
	};
	for (Chunk c : {Chunk{0x0, 1}, {0x1, 1}, {0x2, 2}, {0x4, 2}, {0x10, 4}, {0x6, 2}, {0x8, 2}, {0xA, 2}}) {
		to(uint32_t(c.offset), c.size);
	}
	g.u8(global::kWorld + 0x89) = r.u8() != 0;
	to(0x8C, 1);
	g.u8(global::kWorld + 0x8A) = r.u8() == 0x7F;
	for (Chunk c : {Chunk{0x8B, 1}, {0xC, 4}, {0x14, 4}, {0x7C, 2}, {0x7E, 2}, {0x18, 40}, {0x40, 40}, {0x8D, 1},
			 {0x8E, 1}, {0x90, 7 * 12}}) {
		to(uint32_t(c.offset), c.size);
	}
}

// Capitán o administrador (0x4FDEB0), versión 0x82.
void load_captain(Reader& r, CaptainMemory& m) {
	for (Chunk c : {Chunk{0x0, 2}, {0x2, 1}, {0x3, 1}, {0x4, 4}, {0xC, 2}, {0x8, 1}, {0x9, 1}, {0xA, 1}, {0xB, 1}}) {
		r.to(m, c.offset, c.size);
	}
	m.u8(0xE) = r.u8() != 0;
	r.to(m, 0xF, 1);
}

// 0x51F100: saca un registro de edificio de la lista libre [0x760]; si no
// queda ninguno, la tabla crece en 0x80 registros libres (0xFF).
uint16_t take_free_record(SavedTown& town) {
	TownMemory& m = town.memory;
	if (m.u16(0x760) == m.u16(0x776)) {
		const uint16_t old = m.u16(0x776);
		const uint16_t count = uint16_t(old + 0x80);
		m.u16(0x776) = count;
		town.records.resize(count, BuildingRecord{0xFF, 0xFF, 0xFF, 0xFF, 0xFFFF, 0xFF, 0xFF});
		for (uint16_t i = uint16_t(count - 1);; --i) {
			town.records[i].next = uint16_t(i + 1);
			if (i <= m.u16(0x760)) {
				break;
			}
		}
	}
	const uint16_t index = m.u16(0x760);
	m.u16(0x760) = town.records[index].next;
	town.records[index].next = 0xFFFF;
	return index;
}

// Lo que hace el juego con los registros al terminar la parte 3 (0x51EC10):
// marca los libres con x = y = 0xFF y, si la ciudad tiene estatua
// ([0x76C] & 1) y no hay ningún registro del tipo 0x2D en la lista [0x764],
// añade uno al principio en su sitio ([0x800]).
void fix_loaded_records(SavedTown& town) {
	TownMemory& m = town.memory;
	const size_t count = town.records.size();
	for (size_t i = m.u16(0x760); i < count; i = town.records[i].next) {
		town.records[i].x = 0xFF;
		town.records[i].y = 0xFF;
	}
	for (size_t i = m.u16(0x764); i < count; i = town.records[i].next) {
		if (town.records[i].type == 0x2D) {
			return;
		}
	}
	if (!(m.u32(0x76C) & 1)) {
		return;
	}
	const uint16_t index = take_free_record(town);
	town.records[index] = {m.u8(0x800), m.u8(0x801), 0xFF, 0x2D, m.u16(0x764), 0, 0};
	m.u16(0x764) = index;
}

// Bloque de una ciudad (0x51B520), versión 0x82.
void load_town_memory(Reader r, int month, SavedTown& town, GlobalMemory& globals) {
	TownMemory& m = town.memory;
	r.skip(1);
	r.to(m, 0x000, 0x2C0);  // Almacén (0x52DF20).
	r.to(m, 0x2C0, 0x414);  // Parte 1 (0x527AC0).
	// El lector apunta en las tablas del mundo el id y la plantilla de la ciudad.
	globals.u8(global::kWorld + 0x18 + m.u8(0x2C0)) = m.u8(0x2C1);
	globals.u8(global::kWorld + 0x40 + m.u8(0x2C0)) = m.u8(0x2C2);
	recompute_thresholds(m, month);
	r.skip(1);
	r.to(m, 0x82C, 20);  // Parte 2 (0x525E30).
	r.skip(1);
	// Parte 3 (0x51EC10): cabecera, iglesia, registros y town_class1.
	for (Chunk c : {Chunk{0x776, 2}, {0x760, 2}, {0x762, 2}, {0x764, 2}, {0x76A, 2}, {0x768, 2}, {0x766, 2},
			 {0x774, 1}, {0x771, 1}, {0x770, 1}, {0x76C, 4}, {0x784, 2}, {0x773, 1}, {0x772, 1}, {0x789, 1},
			 {0x788, 1}, {0x78A, 1}, {0x78C, 2}, {0x78E, 2}, {0x786, 2}, {0x790, 2}, {0x792, 2}, {0x775, 1},
			 {0x78B, 1}, {0x778, 6}, {0x77E, 6}, {0x794, 16}}) {
		r.to(m, c.offset, c.size);
	}
	r.skip(size_t(m.u16(0x776)) * 8);  // Registros de edificio: SavedTown::records.
	m.u32(0x7A4) = 1;  // town_class1 (0x4F9770).
	r.to(m, 0x7A8, 4);
	r.to(m, 0x7AC, 4);
	r.to(m, 0x7B0, 0x5C);
	town.grid.resize(size_t(m.u32(0x7A8)) * m.u32(0x7AC));
	for (uint8_t& cell : town.grid) {
		cell = r.u8();
	}
	r.skip(1);
	// Parte 4 (0x52A2B0): los 2 bytes de 0x822 no se guardan.
	for (Chunk c : {Chunk{0x810, 4}, {0x814, 4}, {0x818, 4}, {0x81C, 2}, {0x81E, 2}, {0x820, 2}, {0x824, 4},
			 {0x828, 4}}) {
		r.to(m, c.offset, c.size);
	}
	r.skip(1);
	// Parte 5 (0x528870): dinero y sus 11 movimientos ([0x704], [0x730]).
	for (Chunk c : {Chunk{0x6D4, 0x14}, {0x6E8, 8}, {0x6F0, 4}, {0x6F4, 4}, {0x6F8, 4}, {0x6FC, 4}, {0x700, 4},
			 {0x704, 0x2C}, {0x730, 0x2C}}) {
		r.to(m, c.offset, c.size);
	}
	r.skip(1);
	for (size_t k = 0; k < 21; ++k) {
		r.to(m, 0x840 + 16 * k, 16);  // Instalaciones (0x510070).
	}
	r.to(m, 0x9A4, 16);
	r.to(m, 0x990, 4);
	r.to(m, 0x994, 2);
	r.to(m, 0x996, 2);
	for (size_t i = 0; i < 4; ++i) {
		r.to(m, 0x998 + i, 1);
		r.to(m, 0x99C + i, 1);
	}
	for (Chunk c : {Chunk{0x9A0, 2}, {0x9A2, 2}, {0x9B8, 4}, {0x9BC, 1}, {0x9BD, 1}, {0x9C0, 4}, {0x9C4, 1},
			 {0x9C6, 18}, {0x9D8, 18}}) {
		r.to(m, c.offset, c.size);
	}
	// Nodos (0x50FD40): cada uno se pone el primero de la lista [0x9B4].
	const uint16_t count = r.u16();
	town.nodes.assign(count, TownNode());
	for (size_t k = count; k-- > 0;) {
		TownNode& node = town.nodes[k];
		r.to(node, 4, 4);
		r.to(node, 8, 4);
		for (size_t o : {0xC, 0xE}) {
			const uint8_t high = r.u8();
			node.u16(o) = uint16_t(high << 8 | r.u8());
		}
		r.to(node, 0x10, 1);
		r.to(node, 0x11, 1);
		const int8_t high = int8_t(r.u8());
		node.s16(0x12) = int16_t(high * 256 + int8_t(r.u8()));
	}
}

// Oficina (0x4FEFF0), versión 0x82, sobre lo que deja su constructor (0x4FEBD0).
void load_office_memory(Reader r, SavedOffice& office) {
	OfficeMemory& m = office.memory;
	for (size_t o : {0x2C4, 0x2C8, 0x2CA, 0x2CC, 0x2CE, 0x2D0, 0x2F2}) {
		m.u16(o) = 0xFFFF;
	}
	m.u8(0x2C6) = 0xFF;
	m.u32(0x2E4) = 0x02020202;
	m.u16(0x2EC) = 0x32;
	r.to(m, 0x2C4, 2);
	if (m.u16(0x2C4) == 0xFFFF) {
		r.to(m, 0x2C8, 2);
		r.to(m, 0x2CA, 2);
		return;
	}
	r.to(m, 0x000, 0x2C0);  // Almacén (0x52DF20).
	for (Chunk c : {Chunk{0x2C0, 4}, {0x2C8, 2}, {0x2C6, 1}, {0x2C7, 1}, {0x2CA, 2}, {0x2CC, 2}, {0x2D2, 2},
			 {0x2D4, 2}, {0x2CE, 2}, {0x2DE, 6}, {0x2D8, 6}, {0x2E4, 4}, {0x2D0, 2}, {0x2D6, 2}, {0x2EC, 2},
			 {0x2EE, 2}, {0x2F0, 2}, {0x2F2, 2}, {0x2F4, 0x60}, {0x354, 0x60}, {0x3B8, 0x60}, {0x3B4, 4},
			 {0x418, 8}, {0x420, 8}, {0x428, 8}, {0x430, 8}, {0x438, 8}, {0x440, 8}, {0x448, 2}, {0x44A, 2},
			 {0x2E8, 4}}) {
		r.to(m, c.offset, c.size);
	}
}

// Comerciante (0x4F4100 → 0x4F6FA0 → 0x4F6240), versión 0x82. Las diez
// listas del final ([0x624], punteros) no van a la memoria.
void load_merchant_memory(Reader r, SavedMerchant& merchant) {
	MerchantMemory& m = merchant.memory;
	for (Chunk c : {Chunk{0x0, 4}, {0x4, 4}, {0x8, 2}, {0xA, 2}, {0xC, 2}, {0xE, 2}, {0x10, 4}, {0x14, 2},
			 {0x16, 2}}) {
		r.to(m, c.offset, c.size);
	}
	for (size_t o = 0x18; o <= 0x21; ++o) {
		r.to(m, o, 1);
	}
	const int high = int8_t(r.u8());
	m.u16(0x22) = uint16_t(high << 8 | r.u8());
	r.to(m, 0x24, 1);
	m.u8(0x25) = r.u8() != 0;
	for (Chunk c : {Chunk{0x26, 1}, {0x27, 1}, {0x28, 4}, {0x2C, 4}, {0x30, 1}, {0x31, 1}, {0x32, 1}, {0x37, 1},
			 {0x33, 1}, {0x36, 1}, {0x34, 2}, {0x38, 8}, {0xF0, 0x28}, {0x40, 0xA0}, {0xE0, 1}, {0xE1, 1}}) {
		r.to(m, c.offset, c.size);
	}
	const uint8_t low = r.u8();
	m.u16(0xE2) = uint16_t(low + (r.u8() << 8));
	for (Chunk c : {Chunk{0x118, 4}, {0x11C, 0x1E0}, {0x2FC, 0xA0}, {0x3C4, 0xA0}, {0x464, 4}, {0x39C, 0x28},
			 {0x468, 4}, {0x46C, 4}, {0x470, 4}, {0x474, 4}, {0x478, 2}, {0x47A, 2}}) {
		r.to(m, c.offset, c.size);
	}
	for (size_t o = 0x480; o <= 0x4D4; o += 4) {
		r.to(m, o, 4);
	}
	// 0x534830 sobre [0x4D8].
	for (Chunk c : {Chunk{0x538, 4}, {0x4D8, 0x30}, {0x508, 0x30}, {0x53C, 4}, {0x540, 4}, {0x544, 2}, {0x546, 1},
			 {0x547, 1}, {0x608, 4}, {0x548, 0x60}, {0x5A8, 0x60}, {0x60C, 0x14}}) {
		r.to(m, c.offset, c.size);
	}
	m.u16(0x620) = 0;
}

// Bloque de una ciudad. Los tamaños fijos corresponden a la versión 0x82.
bool parse_town(Reader& r, int month, SavedTown& town, GlobalMemory& globals, std::string* error) {
	const std::string where = " en la ciudad que empieza en " + std::to_string(r.position());
	load_town_memory(r, month, town, globals);

	// Partes 1 y 2: el almacén y los datos de la ciudad van a la memoria
	// (load_town_memory), incluidos los umbrales de precio que el juego
	// recalcula al terminar la parte 1.
	if (!r.separator()) {
		return fail(error, "separador inesperado" + where);
	}
	r.skip(0x2C0 + 0x414);
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
	r.skip(3 + 4 + 2 + 5 + 10 + 2 + 12);
	r.skip(16);  // Iglesia.
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
	fix_loaded_records(town);
	town.drawn_lists[1] = town.memory.u16(0x764);

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

// Objeto de asedio de la ciudad `town` (0x633730, versión 0x82). 0x6339C0
// deja a 0 lo que no se lee. Si lleva ejército, este (0x627880 y 0x627AD0)
// queda como lo deja el juego tras leerlo (tick::load_siege_army).
void parse_siege(Reader& r, SaveGame& out, size_t town) {
	SiegeMemory& m = out.sieges[town];
	SiegeExtras& extras = out.siege_extras[town];
	for (Chunk c : {Chunk{0x0, 4}, {0x4, 1}, {0x1018, 2}, {0x101A, 1}, {0x101B, 1}}) {
		r.to(m, c.offset, c.size);
	}
	for (size_t k = 0; k < 4; ++k) {
		r.to(m, 0x1010 + k, 1);
		r.to(m, 0x1014 + k, 1);
	}
	extras.list_101c.resize(r.u8());
	for (GameMemory<8>& node : extras.list_101c) {
		r.to(node, 0, 8);
	}
	extras.list_1020.resize(r.u8());
	for (GameMemory<0x10>& node : extras.list_1020) {
		r.to(node, 0, 0x10);
	}
	m.u32(0x101C) = extras.list_101c.empty() ? 0 : 1;
	m.u32(0x1020) = extras.list_1020.empty() ? 0 : 1;
	if (r.u8() == 0) {
		return;
	}
	// 0x627880 y 0x627F70: todo a 0 salvo [+0x6F] = 0xFF; [+0x5D3] la ciudad.
	SiegeArmyMemory& a = extras.army.emplace();
	a.u8(0x6F) = 0xFF;
	a.u8(0x5D3) = m.u8(0x4);
	m.u32(0x8) = 1;
	// 0x627AD0, versión 0x82.
	for (Chunk c : {Chunk{0x4, 4}, {0x0, 4}, {0x57C, 4}, {0x580, 4}, {0x584, 4}, {0x42, 2}, {0x66, 2}, {0x3C, 2},
			 {0x68, 2}, {0x3A, 2}, {0x6A, 2}, {0x44, 2}, {0x78, 1}, {0x6D, 1}, {0x5D4, 1}, {0x61C, 1}, {0x62C, 1}}) {
		r.to(a, c.offset, c.size);
	}
	for (size_t k = 0; k < 5; ++k) {
		r.to(a, 0x21C + 4 * k, 4);
		r.to(a, 0x230 + 4 * k, 4);
	}
	r.to(a, 0x6E, 1);
	r.to(a, 0x6F, 1);
	if (a.u8(0x6F) != 0xFF) {
		for (uint32_t b = a.u8(0x6F); b < a.u8(0x6E); ++b) {
			r.to(a, 0x254 + 8 * b, 8);
		}
	}
	tick::load_siege_army(out, int(town));
}

void parse_office(Reader& r, SavedOffice& office) {
	load_office_memory(r, office);
	r.skip(office.used() ? 1100 : 6);
}

void parse_merchant(Reader& r, SavedMerchant& merchant, std::array<std::vector<uint16_t>, 10>& kept_lists) {
	load_merchant_memory(r, merchant);
	r.skip(1532);
	// Diez listas de u16 al final; su longitud va en los 15 bits bajos.
	std::array<uint16_t, 10> lists{};
	for (uint16_t& count : lists) {
		count = r.u16();
	}
	// Kept for write_savegame (SaveOpaque::merchant_lists).
	for (size_t k = 0; k < lists.size(); ++k) {
		kept_lists[k].resize(lists[k] & 0x7FFF);
		for (uint16_t& v : kept_lists[k]) {
			v = r.u16();
		}
	}
}

// Barco (0x517990 y 0x516520), versión 0x82, en su memoria. Los huecos
// libres solo guardan su estado, el siguiente hueco libre y, desde la versión
// 0x83, [+0x137]; lo demás es lo que deja el constructor de SavedShip.
void parse_ship(Reader& r, int version, SavedShip& ship) {
	ShipMemory& m = ship.memory;
	r.to(m, 0x134, 2);
	if (!ship.in_use()) {
		r.to(m, 0x6, 2);
		if (version >= 0x83) {
			r.to(m, 0x137, 1);
		}
		return;
	}
	// 0x516520.
	for (Chunk c : {Chunk{0x4, 2}, {0x0, 1}, {0xE, 1}, {0xF, 1}, {0x1, 1}}) {
		r.to(m, c.offset, c.size);
	}
	// [+0x2]: dos bytes con signo, el alto primero.
	const int high = int8_t(r.u8());
	m.u16(0x2) = uint16_t((high << 8) + int8_t(r.u8()));
	for (Chunk c : {Chunk{0x10, 4}, {0x14, 4}, {0x6, 2}, {0x8, 2}, {0xA, 2}, {0xC, 2}, {0x18, 4}, {0x1C, 4},
			 {0x20, 4}, {0x24, 4}, {0x28, 4}, {0x2C, 4}, {0x30, 4}, {0x34, 2}, {0x36, 1}, {0x37, 1}, {0x38, 1},
			 {0x39, 1}}) {
		r.to(m, c.offset, c.size);
	}
	const int target_high = r.u8();
	m.u16(0x3A) = uint16_t(target_high << 8 | r.u8());
	for (Chunk c : {Chunk{0x3C, 2}, {0x3E, 2}, {0x40, 2}, {0x42, 2}, {0x44, 4}, {0x48, 4}, {0x4C, 4}, {0x50, 4}}) {
		r.to(m, c.offset, c.size);
	}
	// La ruta solo se guarda si es del barco. Si navega de una ciudad a otra, al
	// cargarlo el juego le da la de la matriz (0x445010, ver Sailing). El
	// puntero [+0x24] de la memoria no se mantiene: la ruta es `route`.
	if (m.u32(0x24) != 0 && ship.own_route()) {
		auto route = std::make_shared<SeaRoute>(r.u16());
		for (MapPoint& point : *route) {
			point.x = int16_t(r.u16());
			point.y = int16_t(r.u16());
		}
		ship.set_route(route);
	} else if (m.u32(0x24) != 0) {
		ship.town_route = true;
	}
	// 0x517990.
	for (Chunk c : {Chunk{0x54, 0x60}, {0xB4, 0x60}, {0x114, 4}, {0x124, 4}, {0x128, 4}, {0x12C, 4}, {0x130, 2},
			 {0x132, 2}, {0x136, 1}, {0x137, 1}, {0x13A, 1}, {0x13B, 1}, {0x138, 2}, {0x13C, 24}, {0x154, 2},
			 {0x156, 1}, {0x157, 1}}) {
		r.to(m, c.offset, c.size);
	}
	if (version >= 0x79) {
		r.to(m, 0x158, 1);
		r.to(m, 0x159, 1);
		r.to(m, 0x15A, 2);
	} else {
		m.u8(0x159) = 0xFF;
	}
	m.u8(0x15C) = r.u8() != 0;
	r.to(m, 0x15D, 1);
	const uint8_t b0 = r.u8();
	m.u16(0x15E) = uint16_t((b0 << 8) + r.u8());
	r.to(m, 0x160, 32);
}

// 0x5311D0 con la fecha de un evento: día [+0xF] y mes [+0x10], los dos desde 1.
void set_event_date(GameMemory<0x18>& e, uint32_t tick) {
	static constexpr uint16_t kMonthStart[14] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365, 0};
	const int32_t days = int32_t(tick) >> 8;
	const int32_t year = days / 365;
	const int32_t day = days - int32_t(uint16_t(year)) * 365;
	uint8_t month = 0;
	if (kMonthStart[0] <= day) {
		do {
			++month;
		} while (kMonthStart[month] <= day);
	}
	e.u8(0xF) = uint8_t(uint8_t(day) - uint8_t(kMonthStart[month - 1]) + 1);
	e.u8(0x10) = month;
}

// Buzón de los avisos a los comerciantes (0x4D6350 sobre el objeto 0x6DD730),
// justo tras los barcos: u32 versión, u16 número de avisos [0x6DD736], u16
// [0x6DD734], u16 [0x6DD738], u16 [0x6DD73A] (desde la versión 2) y los
// avisos (0x51A9B0): u8 día, u8 mes, u16 año, u8 tipo, u8, u16 siguiente (la
// lista de cada comerciante empieza en su [+0xA]) y 8 bytes. Los de los tipos
// 0x29, 0x47, 0x63 y 0x83 llevan detrás [+0xC] bytes y los 0x3C..0x40 y 0x71,
// 16 bytes y tantos como el primer u32 de esos: el juego los guarda aparte
// (punteros en [+0x8], y en [+0xC] los segundos); aquí los punteros quedan a 0
// and the 16 bytes go to SaveGame::mail_records; the rest goes to
// SaveGame::mail_buffers.
void load_mail(Reader r, SaveGame& out) {
	GlobalMemory& g = out.globals;
	const uint32_t version = r.u32() & 0xFFFF;
	g.u16(0x6DD736) = r.u16();
	g.u16(0x6DD734) = r.u16();
	g.u16(0x6DD738) = r.u16();
	g.u16(0x6DD73A) = version >= 2 ? r.u16() : 0;
	out.mail.assign(g.u16(0x6DD736), GameMemory<0x10>());
	out.mail_records.assign(out.mail.size(), std::nullopt);
	out.mail_buffers.assign(out.mail.size(), {});
	for (size_t i = 0; i < out.mail.size(); ++i) {
		GameMemory<0x10>& m = out.mail[i];
		r.to(m, 0, 8);
		const uint8_t type = m.u8(4);
		if (type == 0x29 || type == 0x47 || type == 0x63 || type == 0x83) {
			r.to(m, 8, 8);
			m.u32(8) = 0;
			const size_t start = r.position();
			r.skip(m.u32(0xC));
			out.mail_buffers[i] = r.since(start);
		} else if ((type >= 0x3C && type <= 0x40) || type == 0x71) {
			GameMemory<0x10>& record = out.mail_records[i].emplace();
			r.to(record, 0, 0x10);
			const size_t start = r.position();
			r.skip(record.u32(0));
			out.mail_buffers[i] = r.since(start);
		} else {
			r.to(m, 8, 8);
		}
	}
}

// Cola de eventos con fecha (0x4D81E0 sobre el objeto 0x6DD73C), versión
// 0x82: u16 cabeza [0x6DD744], u16 [0x6DD746], u16 número de registros
// [0x6DD748] y los registros de 0x18 bytes (u32 tick, u16 siguiente, u16 tipo,
// enlazados en orden de tick desde la cabeza; un índice ≥ número termina).
// Los de tipo 0x1B guardan después [+0x12] entradas de 16 bytes y los de tipo
// 0x34 con [+0x15] ≥ 7 una lista de nodos de 5 bytes; son objetos aparte que
// no se usan (sus punteros quedan a 0).
void load_events(Reader r, SaveGame& out) {
	GlobalMemory& g = out.globals;
	for (uint32_t a : {0x6DD744u, 0x6DD746u, 0x6DD748u}) {
		const uint16_t v = r.u16();
		g.u16(a) = v;
	}
	const uint16_t count = g.u16(0x6DD748);
	out.events.assign(count, GameMemory<0x18>());
	for (GameMemory<0x18>& e : out.events) {
		r.to(e, 0, 0x18);
	}
	out.opaque.event_extras.assign(count, {});
	for (size_t i = 0; i < out.events.size(); ++i) {
		GameMemory<0x18>& e = out.events[i];
		const size_t extra_start = r.position();
		if (e.u16(6) == 0x1B) {
			// 4 bytes an entry in the file (0x4D831B), in a buffer of 16 each.
			r.skip(size_t(e.u16(0x12)) * 4);
			e.u32(8) = 0;
			e.u32(0xC) = 0;
		} else if (e.u16(6) == 0x34 && e.u8(0x15) >= 7 && e.u32(8) != 0) {
			do {
				r.skip(4);
			} while (r.u8() == 0x7F && r.ok());
			e.u32(8) = 0;
		}
		out.opaque.event_extras[i] = r.since(extra_start);  // Kept for write_savegame.
	}
	// Los eventos de tipo 2 con fecha absurda (desde el año 12000) pasan al
	// mismo día del año, el primer año después del evento de la cabeza, y
	// vuelven a su sitio en la lista.
	uint16_t* link = &g.u16(0x6DD744);
	while (*link < count) {
		const uint16_t index = *link;
		GameMemory<0x18>& e = out.events[index];
		if (e.u16(6) != 2 || e.u32(0) < 0x42D56000) {
			link = &e.u16(4);
			continue;
		}
		uint16_t& head = g.u16(0x6DD744);
		*link = e.u16(4);
		const uint16_t day = uint16_t(int32_t(e.u16(8)) % 365);
		e.u16(8) = day;
		const uint32_t head_tick = out.events[head].u32(0);
		uint32_t tick = ((head_tick / (365 * 256)) * 365 + day) * 256 + 0xFA;
		while (tick <= out.events[head].u32(0)) {
			tick += 365 * 256;
		}
		e.u32(0) = tick;
		set_event_date(e, tick);
		e.u16(4) = head;
		head = index;
		tick::events_detail::fn_4d8dd0(out);
	}
}

// Lo que sigue a las ciudades dentro del mundo (0x52FB60).
bool parse_world_tail(Reader& r, size_t town_count, uint16_t record_count, uint16_t office_count,
	SaveGame& out, std::string* error) {
	out.town_travel.resize(town_count * town_count);  // [+0x6C] del mundo
	for (uint8_t& v : out.town_travel) {
		v = r.u8();
	}
	r.skip(4);
	// Los objetos de asedio de cada ciudad (*0x6DF270).
	out.sieges.assign(town_count, SiegeMemory());
	out.siege_extras.assign(town_count, SiegeExtras());
	for (size_t i = 0; i < town_count; ++i) {
		// 0x6334B0: [+0x4] y [+0x1010..+0x1013] a 0xFF, [+0x1018] = 0x3FFF,
		// [+0x101A] = 2; el lector del mundo pone en [+0x4] la ciudad.
		SiegeMemory& m = out.sieges[i];
		std::memset(&m.u8(0x1010), 0xFF, 4);
		m.u16(0x1018) = 0x3FFF;
		m.u8(0x101A) = 2;
		m.u8(0x4) = uint8_t(i);
		parse_siege(r, out, i);
	}
	out.world_records.resize(record_count);  // 0x4D32F0
	for (GameMemory<0x14>& record : out.world_records) {
		r.to(record, 0, 0x14);
	}

	out.offices.resize(office_count);
	for (SavedOffice& office : out.offices) {
		parse_office(r, office);
	}
	out.merchants.resize(size_t(out.merchant_count));
	out.opaque.merchant_lists.assign(out.merchants.size(), {});
	for (size_t i = 0; i < out.merchants.size(); ++i) {
		parse_merchant(r, out.merchants[i], out.opaque.merchant_lists[i]);
	}

	// 0x5351D0: el objeto 0x6DE588 (0xA00 bytes y los comerciantes de sus 8
	// huecos) y un registro de 40 bytes por hueco usado.
	for (uint32_t a = 0x6DE588; a < 0x6DE588 + 0xA08; ++a) {
		out.globals.u8(a) = r.u8();
	}
	for (size_t i = 0; i < out.player_towns.size(); ++i) {
		if (out.globals.u8(0x6DEF88 + uint32_t(i)) < 0xFF) {
			out.player_towns[i].resize(40);
			for (uint8_t& v : out.player_towns[i]) {
				v = r.u8();
			}
		}
	}
	// 0x52BBE0: los 15 objetos de 0x2C bytes del mundo en [+0xB10] (0x6DEFB0;
	// su tick es 0x52D7D0): 8 bytes, u32 [+0x8], 8 bytes en [+0xC] y en
	// [+0x14] y u16 [+0x1C] con el número de registros de 16 bytes de su
	// lista del montón ([+0x24], SaveGame::world_object_lists; vacías en las
	// partidas de missav/), que el juego lleva también en el u32 [+0x28]. Los
	// usan 0x534F10 (convoys_port.cpp) y el evento 0x12 (events_rare.cpp).
	for (uint32_t i = 0; i < 15; ++i) {
		const uint32_t object = global::kWorld + 0xB10 + 0x2C * i;
		for (uint32_t k = 0; k < 0x1C; ++k) {
			out.globals.u8(object + k) = r.u8();
		}
		const uint16_t entries = r.u16();
		out.globals.u16(object + 0x1C) = entries;
		out.globals.u32(object + 0x28) = entries;
		out.world_object_lists[i].resize(entries);
		for (GameMemory<0x10>& record : out.world_object_lists[i]) {
			r.to(record, 0, 0x10);
		}
	}
	if (r.u8() != 0) {
		return fail(error, "la partida tiene un objeto del mundo (0x533DA0) que no se sabe leer");
	}
	out.globals.u8(global::kWorld + 0x8F) = r.u8();
	out.globals.u16(global::kWorld + 0x80) = uint16_t(out.merchant_count);
	return r.ok() || fail(error, std::string(kTruncated) + " (fin del mundo)");
}

bool parse_ships(Reader& r, SaveGame& out, std::string* error) {
	const int version = r.u16();
	std::array<uint16_t, 16> header{};
	for (uint16_t& v : header) {
		v = r.u16();
	}
	const uint16_t ship_count = header[9];
	// En el objeto de los barcos (0x6DD7A0), en el orden del fichero.
	static constexpr uint32_t kHeaderFields[16] = {0xE0, 0xE2, 0xE4, 0xE8, 0xEA, 0xEC, 0xEE, 0xF0, 0xF2, 0xF4, 0xF6,
		0xE6, 0xFA, 0xFC, 0xFE, 0xF8};
	for (size_t i = 0; i < header.size(); ++i) {
		out.globals.u16(global::kShips + kHeaderFields[i]) = header[i];
	}
	out.captains.resize(out.captain_count());
	for (CaptainMemory& captain : out.captains) {
		load_captain(r, captain);
	}
	out.ships.resize(ship_count);
	for (SavedShip& ship : out.ships) {
		parse_ship(r, version, ship);
	}
	// Los convoyes (0x5014E0): en esta versión, sus 0x3A primeros bytes tal
	// como van en memoria.
	out.convoys.resize(header[10]);
	for (GameMemory<0x3C>& convoy : out.convoys) {
		r.to(convoy, 0, 0x3A);
	}
	// Los 16 huecos de 12 bytes del objeto de los barcos en [+0x20]
	// (0x6DD7C0: u16 barco, s16 x [+0x4], s16 y [+0x6], u16 [+0x8]): el lector
	// (0x5044F5) los copia tal cual y pone a 0 el u16 [+0xA] de cada uno.
	r.to(out.globals.memory, global::kShips + 0x20 - GlobalMemory::kBegin, 0xC0);
	for (uint32_t k = 0; k < 16; ++k) {
		out.globals.u16(global::kShips + 0x2A + 12 * k) = 0;
	}
	// Los 5 piratas de [+0xC] (0x5044F5: desde la versión 0x63, cada uno tras
	// un byte que dice si existe), como los lee 0x5138B0 con la versión 0x7F
	// o más. Los formatos anteriores (los [+0xF8] primeros, sin ese byte;
	// 0x513A40) no se leen. En los globales, el puntero [0x6DD7AC + 4·i] queda
	// a 1 si existe (en el juego, el objeto del montón) y a 0 si no: la cola de
	// eventos lo mira.
	constexpr uint32_t kPirates = global::kShips + 0xC;
	for (size_t i = 0; i < 5; ++i) {
		out.pirates[i].reset();
		out.globals.u32(kPirates + 4 * uint32_t(i)) = 0;
		out.opaque.pirate_maps[i].clear();
		out.opaque.pirate_records[i].clear();
	}
	for (size_t i = 0; i < 5 && version >= 0x7F; ++i) {
		if (r.u8() == 0) {
			continue;
		}
		out.globals.u32(kPirates + 4 * uint32_t(i)) = 1;
		GameMemory<0x18>& p = out.pirates[i].emplace();
		p.u16(0x8) = r.u16();
		p.u16(0xA) = r.u16();
		p.u16(0xC) = r.u16();
		p.u8(0xE) = r.u8();
		p.u8(0xF) = r.u8();
		p.u16(0x10) = r.u16();
		p.u16(0x12) = r.u16();
		p.u8(0x14) = r.u8();
		p.u8(0x15) = r.u8() != 0 ? 1 : 0;
		p.u16(0x16) = r.u16();
		if (r.u8() != 0) {
			// El objeto de 0x6C bytes de [+0x0] (0x4F9770, versión 0x7D o más):
			// u32 ancho, u32 alto, 0x5C bytes y ancho × alto bytes.
			p.u32(0x0) = 1;
			const size_t map_start = r.position();
			const uint32_t width = r.u32();
			const uint32_t height = r.u32();
			r.skip(0x5C);
			const int32_t cells = int32_t(width * height);
			if (cells > 0) {
				r.skip(size_t(cells));
			}
			out.opaque.pirate_maps[i] = r.since(map_start);  // Kept for write_savegame.
		}
		p.u32(0x4) = p.u16(0x8) ? 1 : 0;
		const size_t records_start = r.position();
		r.skip(size_t(p.u16(0x8)) * 8);
		out.opaque.pirate_records[i] = r.since(records_start);
	}
	load_pirate_nests(out);
	return r.ok() || fail(error, std::string(kTruncated) + " (barcos)");
}

// Nombres de las ciudades y nombres escritos por los jugadores (0x511E30). Es
// la última sección del fichero, entre los dos últimos separadores, así que se
// busca desde el final sin leer las secciones que van antes.
void parse_names(const uint8_t* data, size_t size, SaveGame& out) {
	auto separator_at = [&](size_t pos) {
		return data[pos] == 0xFE && data[pos + 1] == 0xAF && data[pos + 2] == 0 && data[pos + 3] == 0;
	};
	if (size < 8 || !separator_at(size - 4)) {
		return;
	}
	size_t start = size - 8;
	while (start > 0 && !separator_at(start)) {
		--start;
	}
	size_t pos = start + 4;
	auto next_string = [&](std::string& s) {
		const size_t end = pos;
		while (pos < size - 4 && data[pos] != 0) {
			++pos;
		}
		if (pos >= size - 4) {
			return false;
		}
		s.assign(reinterpret_cast<const char*>(data + end), pos - end);
		++pos;
		return true;
	};
	std::string s;
	for (int i = 0; i < 40; ++i) {
		if (!next_string(s)) {
			return;
		}
	}
	std::array<std::vector<std::string>, 3> lists;
	for (auto& list : lists) {
		while (pos < size - 4 && data[pos] != '#') {
			if (!next_string(s)) {
				return;
			}
			list.push_back(latin1_to_utf8(s));
		}
		++pos;
	}
	// Then the ship names of the save (u16 number, u16 bytes and the text),
	// which the game undoes at the end of the load (0x5121C0), and the final
	// separator.
	if (pos + 4 <= size - 4) {
		const size_t count = size_t(data[pos] | data[pos + 1] << 8);
		const size_t text = size_t(data[pos + 2] | data[pos + 3] << 8);
		if (pos + 4 + text == size - 4) {
			out.custom_names = std::move(lists);
			// The ship names of the save, for the load (savegame_load.hpp).
			out.opaque.save_ship_names.clear();
			for (size_t p = pos + 4, end = pos + 4 + text; p < end && out.opaque.save_ship_names.size() < count;) {
				const void* zero = std::memchr(data + p, 0, end - p);
				const size_t n = zero ? size_t(static_cast<const uint8_t*>(zero) - (data + p)) : end - p;
				out.opaque.save_ship_names.emplace_back(reinterpret_cast<const char*>(data + p), n);
				p += n + 1;
			}
		}
	}
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
	if (out.version < kOldestVersion || out.version > kNewestVersion) {
		return fail(error, "versión de partida no soportada: " + std::to_string(out.version));
	}

	// Ajustes de la partida. Al cargar, el juego aplica la dificultad del
	// comercio del byte +0x10 (0x54A03C → 0x52E1A0): 0 baja, 1 normal, 2 o más alta.
	// El juego copia los ajustes en *0x6CC3E8, borra sus 16 últimos bytes y
	// limita el byte 8 a 14 salvo que el byte 0xF valga 6 (0x549F40).
	if (size >= 4 + kSettingsSize) {
		std::memcpy(out.settings.bytes.data(), data + 4, kSettingsSize);
		std::fill(out.settings.bytes.begin() + 0x3C, out.settings.bytes.end(), 0);
		if (out.settings.u8(0xF) != 6 && out.settings.u8(8) > 14) {
			out.settings.u8(8) = 14;
		}
	}
	r.skip(0x10);
	out.trade_difficulty = TradeDifficulty(std::min<int>(r.u8(), 2));
	r.skip(kSettingsSize - 0x11);
	// [+0x8D4], [+0x8D8] and [+0x8DC] of the game object 0x6DF2F0 (0x549F40):
	// the ms per tick, a value the loader sets to 2 when it is above 4, and
	// one more. Only write_savegame uses them.
	for (uint32_t a : {0x6DFBC4u, 0x6DFBC8u, 0x6DFBCCu}) {
		out.globals.u32(a) = r.u32();
	}
	if (out.globals.u32(0x6DFBC8) > 4) {
		out.globals.u32(0x6DFBC8) = 2;
	}
	if (r.u32() != kMagic) {
		return fail(error, "falta el separador antes del mundo");
	}

	// Cabecera del mundo: fecha (el mes lo usan los umbrales de precio),
	// ciudades, contadores y el tiempo de juego.
	load_world_header(r, out.globals);
	r.skip(6);  // Fecha: en la memoria del mundo.
	const uint32_t town_count = r.u32();
	const uint16_t record_count = r.u16();  // Registros de 20 bytes sin identificar.
	const uint16_t office_count = r.u16();
	out.merchant_count = r.u16();
	r.skip(4 + 4);
	r.skip(4);  // Tick: en la memoria del mundo.
	r.skip(4);
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
		if (!parse_town(r, out.month(), town, out.globals, error)) {
			return false;
		}
		if (town.town_id() != town_ids[i]) {
			return fail(error, "la ciudad " + std::to_string(i) + " no coincide con la tabla del mundo");
		}
		if (town.template_map() != town_maps[i]) {
			return fail(error, "la plantilla de la ciudad " + std::to_string(i) + " no coincide con la tabla del mundo");
		}
	}

	if (!parse_world_tail(r, town_count, record_count, office_count, out, error)) {
		return false;
	}
	if (r.u32() != kMagic) {
		return fail(error, "falta el separador antes de los barcos");
	}
	if (!parse_ships(r, out, error)) {
		return false;
	}
	for (SavedShip& ship : out.ships) {
		if (ship.in_use()) {
			recalculate_cargo(ship);
		}
	}
	// El lector de barcos cuenta los de cada comerciante en [0x47C] (0x504652).
	for (SavedMerchant& merchant : out.merchants) {
		uint32_t count = 0;
		for (uint16_t i = merchant.memory.u16(0xE); i < out.ships.size(); i = out.ships[i].next_of_owner()) {
			++count;
		}
		merchant.memory.u32(0x47C) = count;
	}
	// Al cargar una partida de un jugador, el juego se queda con el último
	// comerciante humano (0x5469A1).
	for (size_t i = 0; i < out.merchants.size(); ++i) {
		if (out.merchants[i].kind() == 0) {
			out.player = int(i);
		}
	}
	out.globals.u32(global::kPlayer) = uint32_t(std::max(out.player, 0));
	// Factor de venta con poco almacén según la dificultad (0x52E1A0, tabla 0x6739FC).
	static constexpr float kSellFactors[3] = {2.2f, 2.0f, 1.8f};
	out.globals.f32(0x6DE3D8 + 0x64) = kSellFactors[int(out.trade_difficulty)];
	parse_names(data, size, out);
	// Tras los barcos van tres secciones (0x4D6350, 0x4D4BC0 y la cola de
	// eventos, 0x4D81E0), otra de 5 bytes (0x611AE0) y los nombres, separadas
	// por 0xAFFE: la de eventos es la cuarta empezando por el final y la del
	// buzón (0x4D6350), la sexta, justo tras los barcos (donde se ha quedado
	// `r` si se han leído los piratas).
	size_t separators = 0;
	size_t next_separator = size - 4;  // The one found before (after `pos` in the file).
	for (size_t pos = size - 4; pos >= r.position() && pos > 0; --pos) {
		if (data[pos] == 0xFE && data[pos + 1] == 0xAF && data[pos + 2] == 0 && data[pos + 3] == 0) {
			++separators;
			if (separators == 4 || separators == 6) {
				Reader section(data, size);
				section.skip(pos + 4);
				if (separators == 4) {
					load_events(section, out);
				} else {
					load_mail(section, out);
					break;
				}
			} else if (separators == 3) {
				// The battles (0x611AE0), kept as they are for write_savegame.
				out.opaque.battles.assign(data + pos + 4, data + next_separator);
			} else if (separators == 5) {
				// 0x4D4BC0 on 0x6DD728, the automatic trade routes: u16 [+0x0],
				// u16 [+0x2] and that many records of 0xDC bytes, which only
				// write_savegame uses.
				Reader section(data, size);
				section.skip(pos + 4);
				out.globals.u16(0x6DD728) = section.u16();
				out.globals.u16(0x6DD72A) = section.u16();
				out.opaque.trade_routes.assign(out.globals.u16(0x6DD72A), GameMemory<0xDC>());
				for (GameMemory<0xDC>& record : out.opaque.trade_routes) {
					section.to(record, 0, 0xDC);
				}
			}
			next_separator = pos;
		}
	}
	return true;
}

std::string SavedShip::name() const {
	const char* p = reinterpret_cast<const char*>(memory.bytes.data() + 0x160);
	size_t n = 0;
	while (n < 32 && p[n] != 0) {
		++n;
	}
	return latin1_to_utf8(std::string_view(p, n));
}

bool read_savegame_data(const CprArchive& archive, const std::string& path, std::vector<uint8_t>& out,
	std::string* error) {
	const CprEntry* entry = archive.find(path);
	std::vector<uint8_t> packed;
	if (!entry || !archive.read(*entry, packed)) {
		return fail(error, "no se puede leer " + path);
	}
	return lz_decompress_file(packed.data(), packed.size(), out, error);
}

bool load_savegame(const CprArchive& archive, const std::string& path, SaveGame& out, std::string* error) {
	std::vector<uint8_t> data;
	if (!read_savegame_data(archive, path, data, error) || !parse_savegame(data.data(), data.size(), out, error)) {
		return false;
	}
	load_game_scripts(archive, out);
	// The .pst next to it (0x5476E0 reads it after the .pat), if it is there.
	if (path.size() > 4 && path.compare(path.size() - 4, 4, ".pat") == 0) {
		std::vector<uint8_t> pst;
		const CprEntry* entry = archive.find(path.substr(0, path.size() - 4) + ".pst");
		load_statistics_file(entry && archive.read(*entry, pst) ? &pst : nullptr, out);
	}
	return true;
}

namespace {

// Un .ini de los datos del juego: sección (en minúsculas) → clave (en
// minúsculas) → texto.
using IniFile = std::map<std::string, std::map<std::string, std::string>>;

std::string lower(std::string s) {
	for (char& c : s) {
		if (c >= 'A' && c <= 'Z') {
			c = char(c - 'A' + 'a');
		}
	}
	return s;
}

std::string trim(const std::string& s) {
	size_t a = 0;
	size_t b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) {
		++a;
	}
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) {
		--b;
	}
	return s.substr(a, b - a);
}

bool read_ini(const CprArchive& archive, const char* path, IniFile& out) {
	const CprEntry* entry = archive.find(path);
	std::vector<uint8_t> data;
	if (!entry || !archive.read(*entry, data)) {
		return false;
	}
	const std::string text(data.begin(), data.end());
	std::map<std::string, std::string>* section = nullptr;
	for (size_t pos = 0; pos < text.size();) {
		size_t end = text.find('\n', pos);
		if (end == std::string::npos) {
			end = text.size();
		}
		const std::string line = trim(text.substr(pos, end - pos));
		pos = end + 1;
		if (!line.empty() && line[0] == '[') {
			section = &out[lower(line.substr(1, line.find(']') - 1))];
		} else if (section && line.find('=') != std::string::npos) {
			const size_t equals = line.find('=');
			const std::string key = lower(trim(line.substr(0, equals)));
			if (!section->count(key)) {
				(*section)[key] = trim(line.substr(equals + 1));
			}
		}
	}
	return true;
}

// Como 0x4BE0B0: la clave leída con sscanf("%i"), o `fallback`.
int32_t ini_int(const IniFile& ini, const std::string& section, const std::string& key, int32_t fallback) {
	auto s = ini.find(lower(section));
	if (s == ini.end()) {
		return fallback;
	}
	auto k = s->second.find(lower(key));
	if (k == s->second.end()) {
		return fallback;
	}
	const char* begin = k->second.c_str();
	char* end = nullptr;
	const long value = std::strtol(begin, &end, 0);
	return end == begin ? fallback : int32_t(value);
}

const std::string* ini_text(const IniFile& ini, const std::string& section, const std::string& key) {
	auto s = ini.find(lower(section));
	if (s == ini.end()) {
		return nullptr;
	}
	auto k = s->second.find(lower(key));
	return k == s->second.end() ? nullptr : &k->second;
}

// 0x5128D0: parte una lista de nombres del juego en `count` nombres. Son parte
// del nombre las letras (bytes >= 'A'), los espacios y los puntos; cualquier
// otro byte, con los que le siguen por debajo de 'A', termina el nombre. Los
// offsets son u16, como en el juego; los nombres que faltan son el primero.
std::vector<std::string> split_game_names(const std::vector<uint8_t>& data, uint16_t count) {
	std::vector<uint8_t> text(data.size() + 0x1E0 + 1, 0);
	std::vector<uint16_t> offsets(size_t(count) + 2, 0);
	auto put = [&](uint16_t at, uint8_t c) {
		if (at < text.size()) {
			text[at] = c;
		}
	};
	uint32_t k = 1;
	bool full = false;
	for (size_t p = 0; p < data.size();) {
		const uint8_t c = data[p];
		if (c >= 0x41 || c == 0x20 || c == 0x2E) {
			put(offsets[k]++, c);
			++p;
			continue;
		}
		for (++p; p < data.size() && data[p] < 0x41; ++p) {
		}
		put(offsets[k]++, 0);
		const uint16_t at = offsets[k];
		if (k >= count) {
			put(at, 0);
			full = true;
			break;
		}
		offsets[k + 1] = at;
		++k;
	}
	if (!full) {
		if (offsets[k] > 0 && offsets[k] - 1u < text.size() && text[offsets[k] - 1u] != 0) {
			put(offsets[k]++, 0);
		} else {
			--k;
		}
		const uint16_t at = offsets[k];
		offsets[count] = at;
		put(at, 0);
		for (uint32_t i = k; i < count; ++i) {
			offsets[i] = 0;
		}
	}
	std::vector<std::string> names(count);
	for (uint32_t i = 0; i < count; ++i) {
		for (size_t p = offsets[i]; p < text.size() && text[p] != 0; ++p) {
			names[i].push_back(char(text[p]));
		}
	}
	return names;
}

}  // namespace

void load_game_scripts(const CprArchive& archive, SaveGame& save) {
	GlobalMemory& globals = save.globals;
	// 0x5121C0: los números de nombres de cada lista.
	IniFile names;
	for (const char* path : {"scripts/Namen_eng.ini", "scripts/Namen_spa.ini", "scripts/Namen.ini"}) {
		if (read_ini(archive, path, names)) {
			break;
		}
	}
	const std::string people = "PERSONENNAMEN";
	const uint16_t families = uint16_t(ini_int(names, people, "Familiennamen", 0x50));
	globals.u16(0x6DDB70) = uint16_t(ini_int(names, people, "Maennernamen", 0x50));
	globals.u16(0x6DDB72) = uint16_t(ini_int(names, people, "Frauennamen", 0x50));
	globals.u16(0x6DDB74) = families;
	globals.u16(0x6DDB76) = uint16_t(families + ini_int(names, people, "Piratennamen", 6));
	globals.u16(0x6DDB78) = uint16_t(ini_int(names, people, "Schiffsnamen", 0x12C));
	globals.u16(0x6DDB7A) = globals.u16(0x6DDB78);
	// 0x5128D0: los nombres de barco.
	for (const char* path : {"scripts/NamenSchiffe_eng.txt", "scripts/NamenSchiffe_spa.txt", "scripts/NamenSchiffe.txt"}) {
		if (const CprEntry* entry = archive.find(path)) {
			std::vector<uint8_t> data;
			if (archive.read(*entry, data)) {
				save.ship_names = split_game_names(data, globals.u16(0x6DDB78));
				break;
			}
		}
	}
	// 0x5121C0: the men's and women's first names, split as the ship names.
	const char* const first_names[2] = {"scripts/NamenMaenner.txt", "scripts/NamenFrauen.txt"};
	for (size_t list = 0; list < 2; ++list) {
		save.first_names[list].clear();
		if (const CprEntry* entry = archive.find(first_names[list])) {
			std::vector<uint8_t> data;
			if (archive.read(*entry, data)) {
				save.first_names[list] = split_game_names(data, globals.u16(list ? 0x6DDB72 : 0x6DDB70));
			}
		}
	}
	save.family_names.clear();
	if (const CprEntry* entry = archive.find("scripts/NamenFamilien.txt")) {
		std::vector<uint8_t> data;
		if (archive.read(*entry, data)) {
			save.family_names = split_game_names(data, globals.u16(0x6DDB76));
		}
	}
	load_save_scripts(archive, save);  // For write_savegame (savegame_write.hpp).
	finish_load(save);  // The loader's last steps (savegame_load.hpp).
	// Las plantillas de mapa de las ciudades nuevas (0x4F9B40).
	save.town_templates.clear();
	for (unsigned i = 0; i < 100; ++i) {
		char path[40];
		std::snprintf(path, sizeof(path), "iso/towns/stadtplatz%.2u.tow", i);
		const CprEntry* entry = archive.find(path);
		if (!entry) {
			break;
		}
		save.town_templates.emplace_back();
		archive.read(*entry, save.town_templates.back());
	}
	// 0x51E440: la tabla de las ciudades.
	IniFile towns;
	read_ini(archive, "scripts/StadtDaten.ini", towns);
	static const char* const kInts[10] = {"StadtSymbolXPos", "StadtSymbolYPos", "NamenXPos", "NamenYPos",
		"EreignisSymbolXPos", "EreignisSymbolYPos", "SchiffAufLandXPos", "SchiffAufLandYPos", "AnfahrtXPos",
		"AnfahrtYPos"};
	for (uint32_t i = 0; i < 40; ++i) {
		const uint32_t t = 0x6DDB90 + 0x34 * i;
		const std::string section = "Stadt" + std::to_string(i);
		for (uint32_t k = 0; k < 10; ++k) {
			globals.s32(t + 4 * k) = ini_int(towns, section, kInts[k], 0);
		}
		globals.u16(t + 0x2C) = uint16_t(ini_int(towns, section, "SituationDerStadtAmFluss", 0));
		globals.u8(t + 0x2E) = uint8_t(ini_int(towns, section, "LandwegNach", 0xFF));
		globals.u8(t + 0x2F) = ini_int(towns, section, "AmFluss", 0) == 1;
		globals.u8(t + 0x30) = uint8_t(ini_int(towns, section, "Region", 0));
		globals.u8(t + 0x31) = uint8_t(ini_int(towns, section, "AusrichtungDesMeeres", 0));
		if (const std::string* align = ini_text(towns, section, "NameLinksOderRechtsBuendig"); align && !align->empty()) {
			globals.u8(t + 0x33) = uint8_t((*align)[0]);
		}
		globals.u8(t + 0x32) = uint8_t(ini_int(towns, section, "LautstaerkeDerMusik", 0x2D));
	}
	// 0x5346A0: los puntos de las expediciones (u16 x, u16 y), si aún no están.
	if (const CprEntry* entry = archive.find("scripts/Expeditionen.dat"); entry && save.expeditions.empty()) {
		std::vector<uint8_t> data;
		if (archive.read(*entry, data)) {
			const size_t count = (data.size() >> 2) & 0xFFFF;
			save.expeditions.resize(count);
			for (size_t i = 0; i < count; ++i) {
				save.expeditions[i] = {uint16_t(data[4 * i] | data[4 * i + 1] << 8),
					uint16_t(data[4 * i + 2] | data[4 * i + 3] << 8)};
			}
			globals.u32(0x6DF2E4) = uint32_t(count);
		}
	}
}

NameLists load_name_lists(const CprArchive& archive) {
	NameLists lists;
	const char* const paths[] = {"scripts/NamenMaenner.txt", "scripts/NamenFrauen.txt", "scripts/NamenFamilien.txt"};
	for (size_t i = 0; i < lists.size(); ++i) {
		const CprEntry* entry = archive.find(paths[i]);
		std::vector<uint8_t> data;
		if (!entry || !archive.read(*entry, data)) {
			continue;
		}
		const std::string text(data.begin(), data.end());
		for (size_t pos = 0; pos < text.size();) {
			size_t end = text.find('\n', pos);
			if (end == std::string::npos) {
				end = text.size();
			}
			std::string line = text.substr(pos, end - pos);
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
				line.pop_back();
			}
			if (!line.empty()) {
				lists[i].push_back(latin1_to_utf8(line));
			}
			pos = end + 1;
		}
	}
	return lists;
}

std::string merchant_name(const SaveGame& save, const NameLists& names, int merchant) {
	if (merchant < 0 || merchant >= int(save.merchants.size())) {
		return {};
	}
	const SavedMerchant& m = save.merchants[size_t(merchant)];
	auto pick = [&](size_t list, size_t index) -> std::string {
		const std::vector<std::string>& base = names[list];
		if (index < base.size()) {
			return base[index];
		}
		const std::vector<std::string>& custom = save.custom_names[list];
		index -= base.size();
		return index < custom.size() ? custom[index] : std::string();
	};
	// El juego marca con el bit 7 los nombres de mujer.
	const std::string first = pick(m.male() ? 0 : 1, m.first_name() & 0x7F);
	const std::string family = pick(2, m.family_name());
	return first.empty() ? family : family.empty() ? first : first + " " + family;
}

namespace {

// 0x512A60 and 0x512AE0 on one list: the game's names (count of them), then
// the 20 written by the players (its offsets table has count + 20 entries),
// and past them the start of its buffer, the list's first name.
std::string game_name(const std::vector<std::string>& names, uint32_t count, const std::vector<std::string>& custom,
	uint32_t i) {
	if (i >= count + 20) {
		return names.empty() ? std::string() : names[0];  // Not checked with the oracle.
	}
	if (i < count) {
		return i < names.size() ? names[i] : std::string();
	}
	// Not checked with the oracle.
	return i - count < custom.size() ? utf8_to_latin1(custom[i - count]) : std::string();
}

}  // namespace

std::string game_first_name(const SaveGame& save, uint8_t index) {
	const size_t list = (index & 0x80) ? 1 : 0;
	return game_name(save.first_names[list], save.globals.u16(list ? 0x6DDB72 : 0x6DDB70), save.custom_names[list],
		list ? (index & 0x7Fu) : index);
}

std::string game_family_name(const SaveGame& save, uint8_t index) {
	return game_name(save.family_names, save.globals.u16(0x6DDB76), save.custom_names[2], index);
}

}  // namespace p3::data
