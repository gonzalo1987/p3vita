#include "p3data/savegame.hpp"

#include <algorithm>
#include <cstring>

#include "p3data/lz.hpp"
#include "p3data/thresholds.hpp"
#include "p3data/trade.hpp"

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
bool parse_town(Reader& r, int month, SavedTown& town, std::string* error) {
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
	town.situation = r.u32();  // [0x2C8]
	r.skip(4 + 4);  // Sin identificar y fecha de la última fiesta.
	town.citizens = r.u32();
	r.skip(16 + 16 + 8 + 8 + 8);  // Habitantes por clase, anteriores, viviendas y satisfacción.
	r.u32s(town.citizen_consumption);  // [0x310]
	r.s32s(town.incoming);  // [0x370]
	r.skip(2 * 0x60);
	r.u32s(town.supply);  // [0x490]
	for (PriceThresholds& thresholds : town.price_thresholds) {  // [0x4F0]
		r.u32s(thresholds);
	}
	r.s32s(town.in_transit);  // [0x670]
	r.skip(4);

	// Al terminar esta parte el juego recalcula los umbrales de precio
	// (0x527CC1). Lo que aún no ha leído de la ciudad, como las instalaciones,
	// conserva el valor inicial; por eso la fábrica de ladrillos cuenta como 0.
	if (int32_t(town.price_thresholds[0][0]) > 1) {
		ThresholdInputs in;
		in.flags = town.situation;
		in.citizens = town.citizens;
		in.business_consumption = town.business_consumption;
		in.citizen_consumption = town.citizen_consumption;
		in.production = town.production;
		in.supply = town.supply;
		in.month = month;
		town.price_thresholds = compute_price_thresholds(in, town.price_thresholds);
	} else {
		for (int w = 0; w < kTradeWareCount - 1; ++w) {
			town.price_thresholds[size_t(w)] = {1, 2, 3, 4};
		}
	}

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
	r.skip(0x6F0 - 0x6D4);  // La parte 5 es el struct town desde 0x6D4.
	town.taxed_ware = r.u8();
	r.skip(136 - (0x6F0 - 0x6D4) - 1);
	if (!r.separator()) {
		return fail(error, "separador inesperado (instalaciones)" + where);
	}
	r.skip(21 * 16);
	r.skip(83);
	const uint16_t node_count = r.u16();
	r.skip(size_t(node_count) * 16);
	return r.ok() || fail(error, kTruncated + where);
}

// Objeto opcional de cada ciudad (0x627AD0, 0x650 bytes en memoria; las
// partidas de asedio lo llevan). No se usa: solo se salta.
void skip_town_extra(Reader& r) {
	r.skip(5 * 4 + 7 * 2 + 5 + 5 * 8);
	const uint8_t count = r.u8();
	const uint8_t first = r.u8();
	if (first != 0xFF && first < count) {
		r.skip(size_t(count - first) * 8);
	}
}

void parse_office(Reader& r, SavedOffice& office) {
	office.owner = r.u16();
	if (!office.used()) {
		r.skip(2);  // Siguiente oficina libre.
		office.next_in_town = r.u16();
		return;
	}
	// Almacén, como el de las ciudades.
	r.skip(4);
	r.u32s(office.wares);
	r.skip(0x2C0 - 4 - kWareCount * 4);
	r.skip(4 + 2);
	office.town = r.u8();
	r.skip(1);
	office.next_in_town = r.u16();
	r.skip(1100 - 716);
}

void parse_merchant(Reader& r, SavedMerchant& merchant) {
	merchant.money = r.s32();
	r.skip(4);
	merchant.kind = r.u16();
	r.skip(2);
	merchant.first_office = r.u16();
	merchant.first_ship = r.u16();
	r.skip(4 + 2 + 2 + 1);
	merchant.hometown = r.u8();
	merchant.male = r.u8() != 0;
	r.skip(1);
	merchant.first_name = r.u8();
	merchant.family_name = r.u8();
	r.skip(1336 - 30);
	merchant.banned_towns = r.u32();
	r.skip(0x60 + 0x60);
	// Diez listas de u16 al final; su longitud va en los 15 bits bajos.
	std::array<uint16_t, 10> lists{};
	for (uint16_t& count : lists) {
		count = r.u16();
	}
	for (uint16_t count : lists) {
		r.skip(size_t(count & 0x7FFF) * 2);
	}
}

// Barco (0x517990 y 0x516520). Los huecos libres solo guardan su estado y un u16.
void parse_ship(Reader& r, int version, SavedShip& ship) {
	ship.status = r.u16();
	if (!ship.in_use()) {
		r.skip(version >= 0x83 ? 3 : 2);
		return;
	}
	ship.next_of_owner = r.u16();
	ship.owner = r.u8();
	ship.type = r.u8();
	ship.level = r.u8();
	ship.heading = r.u8();
	// [+0x2]: dos bytes con signo, el alto primero.
	const int high = int8_t(r.u8());
	ship.route_mark = uint16_t((high << 8) + int8_t(r.u8()));
	ship.capacity = r.s32();
	ship.max_health = r.s32();
	ship.next_in_list = r.u16();
	ship.convoy = r.u16();
	r.skip(2 * 2);  // [+0xA], [+0xC]: lista ordenada por x para los encuentros en el mar (0x50D0C0).
	ship.health = r.s32();
	ship.x = r.s32();
	ship.y = r.s32();
	const uint32_t route = r.u32();
	ship.dx = r.s32();
	ship.dy = r.s32();
	ship.speed = r.s32();
	ship.route_index = r.u16();
	ship.own_route = r.u8() != 0;
	ship.origin = r.u8();
	ship.destination = r.u8();
	ship.town = r.u8();
	const int target_high = r.u8();
	ship.target = uint16_t(target_high << 8 | r.u8());
	r.skip(2);  // [+0x3C]: avisos a la interfaz.
	ship.readiness = r.u16();
	ship.sailors = r.u16();
	ship.captain = r.u16();
	ship.moved_tick = r.u32();
	ship.arrival_tick = r.u32();
	r.skip(2 * 4);
	// La ruta solo se guarda si es del barco. Si navega de una ciudad a otra, al
	// cargarlo el juego le da la de la matriz (0x445010, ver Sailing).
	if (route != 0 && ship.own_route) {
		ship.route = std::make_shared<SeaRoute>(r.u16());
		for (MapPoint& point : *ship.route) {
			point.x = int16_t(r.u16());
			point.y = int16_t(r.u16());
		}
	} else if (route != 0) {
		ship.town_route = true;
	}
	r.s32s(ship.cargo);
	for (float& price : ship.average_price) {
		price = r.f32();
	}
	r.skip(3 * 4);
	ship.load_factor_sum = r.f32();
	ship.voyages = r.u16();
	r.skip(2);
	ship.orders = r.u8();
	r.skip(3);
	ship.timer = r.u16();
	r.bytes(ship.artillery);
	ship.hand_weapons = r.u16();
	ship.extra_loads = r.u8();
	r.skip(1);
	if (version >= 0x79) {
		r.skip(4);
	}
	ship.raid = r.u8() != 0;
	r.skip(1 + 2);
	ship.name = latin1_to_utf8(r.text(32));
}

// Lo que sigue a las ciudades dentro del mundo (0x52FB60).
bool parse_world_tail(Reader& r, size_t town_count, uint16_t record_count, uint16_t office_count,
	SaveGame& out, std::string* error) {
	r.skip(town_count * town_count);
	r.skip(4);
	for (size_t i = 0; i < town_count; ++i) {  // 0x633730
		r.skip(4 + 1 + 2 + 1 + 1 + 4 * 2);
		r.skip(size_t(r.u8()) * 8);
		r.skip(size_t(r.u8()) * 16);
		if (r.u8() != 0) {
			skip_town_extra(r);
		}
	}
	r.skip(size_t(record_count) * 20);  // 0x4D32F0

	out.offices.resize(office_count);
	for (SavedOffice& office : out.offices) {
		parse_office(r, office);
	}
	out.merchants.resize(size_t(out.merchant_count));
	for (SavedMerchant& merchant : out.merchants) {
		parse_merchant(r, merchant);
	}

	// 0x5351D0: un bloque fijo y hasta 8 registros de 40 bytes.
	r.skip(0xA00);
	std::array<uint8_t, 8> slots{};
	r.bytes(slots);
	for (uint8_t slot : slots) {
		if (slot != 0xFF) {
			r.skip(40);
		}
	}
	for (int i = 0; i < 15; ++i) {  // 0x52BBE0
		r.skip(8 + 4 + 8 + 8);
		r.skip(size_t(r.u16()) * 16);
	}
	if (r.u8() != 0) {
		return fail(error, "la partida tiene un objeto del mundo (0x533DA0) que no se sabe leer");
	}
	r.skip(1);
	return r.ok() || fail(error, std::string(kTruncated) + " (fin del mundo)");
}

bool parse_ships(Reader& r, SaveGame& out, std::string* error) {
	const int version = r.u16();
	std::array<uint16_t, 16> header{};
	for (uint16_t& v : header) {
		v = r.u16();
	}
	out.docked_head = header[3];
	out.sailing_head = header[4];
	out.captain_count = header[8];
	const uint16_t ship_count = header[9];
	out.convoy_count = header[10];
	r.skip(size_t(out.captain_count) * 16);
	out.ships.resize(ship_count);
	for (SavedShip& ship : out.ships) {
		parse_ship(r, version, ship);
	}
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
	// Tras las listas queda un u32 antes del separador final.
	if (pos + 4 == size - 4) {
		out.custom_names = std::move(lists);
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
	if (out.version != kSupportedVersion) {
		return fail(error, "versión de partida no soportada: " + std::to_string(out.version));
	}

	// Ajustes de la partida. Al cargar, el juego aplica la dificultad del
	// comercio del byte +0x10 (0x54A03C → 0x52E1A0): 0 baja, 1 normal, 2 o más alta.
	r.skip(0x10);
	out.trade_difficulty = TradeDifficulty(std::min<int>(r.u8(), 2));
	r.skip(kSettingsSize - 0x11 + 3 * 4);
	if (r.u32() != kMagic) {
		return fail(error, "falta el separador antes del mundo");
	}

	// Cabecera del mundo: fecha (el mes lo usan los umbrales de precio),
	// ciudades, contadores y el tiempo de juego.
	out.day = r.u8();
	out.month = r.u8();
	out.year = r.u16();
	r.skip(2);  // Día del año.
	const uint32_t town_count = r.u32();
	const uint16_t record_count = r.u16();  // Registros de 20 bytes sin identificar.
	const uint16_t office_count = r.u16();
	out.merchant_count = r.u16();
	r.skip(4 + 4);
	out.tick = r.u32();  // [+0x14]
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
		if (!parse_town(r, out.month, town, error)) {
			return false;
		}
		if (town.town_id != town_ids[i]) {
			return fail(error, "la ciudad " + std::to_string(i) + " no coincide con la tabla del mundo");
		}
		town.template_map = town_maps[i];
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
	// Al cargar una partida de un jugador, el juego se queda con el último
	// comerciante humano (0x5469A1).
	for (size_t i = 0; i < out.merchants.size(); ++i) {
		if (out.merchants[i].kind == 0) {
			out.player = int(i);
		}
	}
	parse_names(data, size, out);
	return true;
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
	return read_savegame_data(archive, path, data, error) && parse_savegame(data.data(), data.size(), out, error);
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
	const std::string first = pick(m.male ? 0 : 1, m.first_name & 0x7F);
	const std::string family = pick(2, m.family_name);
	return first.empty() ? family : family.empty() ? first : first + " " + family;
}

}  // namespace p3::data
