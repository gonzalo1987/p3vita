// Obras de la ciudad: muelles (0x524920), avisos de ampliación (0x51E070) y
// las decisiones diarias de qué construir (0x51D870). Casi todo se resuelve
// encolando operaciones para el juego (0x54AA70): 0x22 construir un
// edificio, 0x24 seguir con las obras de la lista [0x766], 0x27 materiales
// para un edificio que no se puede levantar, 0x48 programar un evento 0x2E de
// la ciudad... Solo 0x524920 construye directamente (un tramo de muelle).
//
// Registros de edificio y listas: docs/formats/savegame.md §5; la rejilla, §7.
// Listas de la ciudad (cabezas u16): [0x760] libres, [0x762] obras en curso,
// [0x764] edificios, [0x766] obras pendientes (murallas), [0x768] murallas,
// puertas y torres.

#include <cassert>
#include <cstring>

#include "p3data/mail.hpp"
#include "p3data/game_fixes.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {

namespace events_rare_detail {
void fn_522b20(SaveGame& save, int town, uint32_t side);
}  // namespace events_rare_detail

namespace construction_detail {

// Funciones internas (las sondas de tools/oracle/probes_construction.cpp las
// llaman directamente).
uint32_t fn_4d9f60(const SaveGame& save, uint32_t town);
void fn_4d9fc0(const SaveGame& save, uint32_t& tick, uint32_t town);
void fn_4fa740(const SaveGame& save, int town, uint8_t& x, uint8_t& y, uint8_t& param);
uint16_t fn_51f100(SaveGame& save, int town);
void fn_5254c0(SaveGame& save, int town, uint32_t index);
bool fn_4f9c80(const SaveGame& save, int town, uint8_t& x, uint8_t& y, uint32_t type, uint8_t param, bool flag);
void fn_4fa7f0(const SaveGame& save, int town, uint8_t& x, uint8_t& y);
uint32_t fn_51fab0(SaveGame& save, int town, uint8_t type, bool immediate);
void fn_51e270(SaveGame& save, int town);
void fn_5250d0(const SaveGame& save, int town, uint8_t& x, uint8_t& y);
bool fn_4fcbd0(const SaveGame& save, int town, uint8_t x, uint8_t y, uint8_t type, uint8_t param);
bool fn_521900(SaveGame& save, int town, uint8_t owner, uint8_t x, uint8_t y, uint8_t type, uint8_t param);
void fn_523830(SaveGame& save, int town, uint8_t x, uint8_t y, uint8_t owner);
void fn_522c70(SaveGame& save, int town, uint32_t owner, uint32_t kind);
bool fn_51f3a0(SaveGame& save, int town, GameMemory<0x44>& s);
bool fn_524d60(SaveGame& save, int town, uint32_t x, uint32_t y);
void fn_524f00(SaveGame& save, int town);
void fn_524cd0(SaveGame& save, int town);
void fn_524e20(SaveGame& save, int town);

}  // namespace construction_detail

using namespace construction_detail;

namespace {

// Materiales por tipo de edificio (57 tipos, docs/formats/savegame.md §8.1).
// Ladrillos (mercancía 19, [0x50]) en cargas.
constexpr uint8_t kBricks[57] = {  // 0x672AC4
	0x32, 0x4B, 0x0A, 0x32, 0x28, 0x28, 0x28, 0x32, 0x28, 0x19, 0x32, 0x19, 0x32, 0x19, 0x00, 0x32,
	0x32, 0x28, 0x0A, 0x0A, 0x19, 0x32, 0x32, 0x32, 0x28, 0x28, 0x28, 0x19, 0x19, 0x19, 0x28, 0x50,
	0x50, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x19, 0x0A, 0x50, 0x50, 0x50, 0x50, 0x01, 0x32, 0x00,
	0x14, 0x32, 0x14, 0x32, 0x19, 0x19, 0x0A, 0x19, 0x19};
// Madera (mercancía 11, [0x30]) en cargas.
constexpr uint8_t kTimber[57] = {  // 0x672A4C
	0x14, 0x1E, 0x01, 0x14, 0x0A, 0x05, 0x05, 0x14, 0x0A, 0x02, 0x14, 0x02, 0x14, 0x02, 0x0A, 0x14,
	0x14, 0x0A, 0x01, 0x01, 0x02, 0x14, 0x14, 0x14, 0x0A, 0x0A, 0x0A, 0x02, 0x02, 0x02, 0x14, 0x32,
	0x32, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x02, 0x01, 0x1E, 0x32, 0x28, 0x1E, 0x00, 0x14, 0x0A,
	0x14, 0x14, 0x14, 0x14, 0x05, 0x05, 0x01, 0x02, 0x02};
// Herramientas (mercancía 12, [0x34]) en barriles.
constexpr uint8_t kTools[57] = {  // 0x672A88
	0x14, 0x14, 0x01, 0x14, 0x0A, 0x05, 0x05, 0x14, 0x05, 0x02, 0x14, 0x02, 0x14, 0x02, 0x0A, 0x14,
	0x14, 0x0A, 0x01, 0x01, 0x02, 0x14, 0x14, 0x14, 0x0A, 0x0A, 0x0A, 0x02, 0x02, 0x02, 0x14, 0x32,
	0x32, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x02, 0x01, 0x1E, 0x32, 0x28, 0x1E, 0x00, 0x14, 0x0A,
	0x14, 0x14, 0x14, 0x14, 0x05, 0x05, 0x01, 0x02, 0x02};
// Cáñamo (mercancía 17, [0x48]) en cargas: solo astillero de reparación,
// cazadero y pescador.
constexpr uint8_t kHemp[57] = {  // 0x672B00
	0x00, 0x32, 0x00, 0x00, 0x0A, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
// Bit de la clase de celda que busca 0x4F9C80 para empresas y edificios
// municipales con solar propio.
constexpr uint8_t kSiteBit[57] = {  // 0x672F84
	0x03, 0x07, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A,
	0x0A, 0x03, 0x02, 0x03, 0x00, 0x00, 0x03, 0x00, 0x05, 0x03, 0x02, 0x02, 0x03, 0x06, 0x07, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x08, 0x09, 0x08, 0x09};
constexpr uint8_t kPierBuildTime = 0x32;  // 0x672BE3: tabla 0x672BB4 (días de obra) para el tipo 0x2F.
constexpr uint8_t kRoadBuildTime = 0x64;  // 0x672BDB: la misma tabla para el tipo 0x27.
constexpr uint8_t kTypeRoad = 0x27;  // El edificio de la carretera (encargo de la Hansa, alderman.cpp).
constexpr uint8_t kRoadSite = 0x18;  // Su clase de celda: una (o ninguna) por ciudad.
constexpr uint32_t kPierWorkersFactor = 0x1E;  // 0x6729DE

constexpr uint8_t kTypePier = 0x2F;  // Muelle (maulwurf).
constexpr uint8_t kTypeWell = 0x28;
constexpr uint8_t kTypeWall = 0x36;
constexpr uint8_t kTypeTower = 0x30;  // Torre de cañones; 0x31 de bombardas.

// Orientaciones del parámetro de un edificio (byte 7 del registro): 0x677929...
constexpr uint8_t kNorthEast = 0x20;  // 0x677929
constexpr uint8_t kSouthEast = 0x60;  // 0x67792B
constexpr uint8_t kSouthWest = 0xA0;  // 0x67792D
constexpr uint8_t kNorthWest = 0xE0;  // 0x67792F

// Unidades internas: una carga son 2000 y un barril 200 (wares.hpp).
constexpr int32_t kLoad = 2000;
constexpr int32_t kBarrel = 200;

constexpr uint32_t kEvents = 0x6DD73C;  // Cola de eventos (SaveGame::events).
constexpr uint32_t kEventHead = kEvents + 8;  // 0x6DD744
constexpr uint32_t kEventCount = kEvents + 0xC;  // 0x6DD748
constexpr uint32_t kWorldNoTownWork = 0x6DE52A;  // u8; distinto de 0: sin obras ni avisos (también lo mira 0x51BA10).
constexpr uint32_t kTownIds = 0x6DE4B8;  // u8 por ciudad: su número de StadtDaten.ini.
constexpr uint32_t kConvoyCount = global::kShips + 0xF6;  // u16 (0x6DD896)
constexpr uint32_t kExplored = 0x6DF278;  // 0x64 bytes: lo que han marcado los comerciantes (0x534920).

// 0x672BB4: días de obra de cada tipo de edificio.
constexpr uint8_t kBuildDays[60] = {200, 250, 25, 200, 100, 100, 100, 200, 100, 50, 200, 50, 200, 50, 50, 200, 200,
	100, 25, 25, 50, 200, 200, 200, 100, 100, 100, 50, 50, 50, 100, 250, 250, 200, 200, 200, 200, 150, 200, 100, 25,
	200, 250, 200, 200, 5, 0, 50, 200, 0, 200, 0, 0, 0, 25, 50, 50, 0, 0, 0};
// 0x672B78: lo que cuesta construir cada tipo (× 100; a un comerciante, el
// tipo 5 sin [0x2CC] × 60), además de los materiales que falten.
constexpr uint8_t kBuildFee[60] = {130, 30, 2, 40, 80, 50, 50, 80, 30, 30, 50, 30, 80, 30, 100, 80, 80, 50, 10, 10, 30,
	80, 80, 80, 80, 80, 80, 50, 50, 50, 100, 80, 80, 40, 80, 40, 40, 80, 80, 50, 20, 70, 250, 150, 20, 50, 40, 100, 25,
	40, 25, 40, 20, 20, 2, 4, 4, 0, 0, 0};
// 0x673A64, 0x673A44, 0x673A48 y 0x673A5C (f32): lo que paga la ciudad por
// cada unidad interna de ladrillos, madera, herramientas y cáñamo que saca de
// la oficina de un comerciante para una torre.
constexpr float kTowerBricksPrice = 0x1.46dc6p-5f;  // 0,0399
constexpr float kTowerTimberPrice = 0x1.c28f5ep-6f;  // 0,0275
constexpr float kTowerToolsPrice = 0x1.472b02p+0f;  // 1,278
constexpr float kTowerHempPrice = 0x1.c28f5ep-3f;  // 0,22
// 0x672C14: la unidad de cada mercancía (0 cargas, 1 barriles, 2 piezas).
constexpr uint8_t kWareUnit[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
// 0x66FD5C y 0x672838 (f32): lo que da a la reputación un pozo y un 0x29/0x2C sin más.
constexpr float kWellReputation = 0.1f;
constexpr float kChapelReputation = 0.01f;

// Operación para el juego: 0x14 bytes (lo que copia 0x543F10). 0x535740 la
// crea con 0xFFFFFFFF en el primer u32 y el resto a 0.
struct Operation {
	GameMemory<0x14> m;

	Operation() { m.u32(0) = 0xFFFFFFFF; }
	void enqueue(SaveGame& save) const { enqueue_operation(save, m.bytes.data(), m.bytes.size()); }
};

uint32_t merchant_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldMerchantCount);
}

uint32_t office_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldOfficeCount);
}

// Registro i de la cola de eventos (0x4D8DB0; el juego no comprueba más que
// i <= número).
const GameMemory<0x18>& event(const SaveGame& save, uint32_t i) {
	assert(i < save.events.size());
	return save.events[i];
}

// Byte de la rejilla por índice x·alto + y. Las búsquedas leen a veces fuera
// de la rejilla (vecinos de una celda del borde): el juego lee entonces la
// memoria de al lado; aquí cuenta como 0, que no es ninguna de las clases que
// se buscan.
uint8_t cell(const SavedTown& town, int64_t i) {
	return i >= 0 && i < int64_t(town.grid.size()) ? town.grid[size_t(i)] : 0;
}

}  // namespace

namespace construction_detail {

// La cola de eventos (SaveGame::events) la lee la carga de la partida, pero
// el oráculo no: en las 12 partidas llega vacía a estas funciones. Sus
// recorridos se han comprobado con una cola sintética.

// 0x4D9F60 (cola de eventos 0x6DD73C): bits (1 << [+0xD]) de los eventos de
// tipo 0x2E de la ciudad `town` ([+0xC]) hasta el primero que caiga 0x1E00
// ticks o más después de ahora.
uint32_t fn_4d9f60(const SaveGame& save, uint32_t town) {
	const uint32_t limit = save.globals.u32(global::kWorldTick) + 0x1E00;
	const uint32_t n = save.globals.u16(kEventCount);
	uint32_t mask = 0;
	for (uint32_t i = save.globals.u16(kEventHead); i < n;) {
		const GameMemory<0x18>& e = event(save, i);
		if (e.u16(6) == 0x2E && e.u8(0xC) == town) {
			mask |= 1u << (e.u8(0xD) & 31);
		}
		if (e.u32(0) >= limit) {
			break;
		}
		i = e.u16(4);
	}
	return mask;
}

// 0x4D9FC0 (cola de eventos 0x6DD73C): ajusta el tick de un evento nuevo de
// la ciudad para que no caiga a menos de 0x100 ticks de los eventos 0x20, ni
// de los 0x21 ([+0x8]) o 0x2E ([+0xC]) de la ciudad: lo pasa a 0x200 después
// de cada uno que choque (con el byte bajo a 0).
void fn_4d9fc0(const SaveGame& save, uint32_t& tick, uint32_t town) {
	tick &= ~0xFFu;
	const uint32_t low = tick - 0x100;
	const uint32_t high = tick + 0x100;
	const uint32_t n = save.globals.u16(kEventCount);
	for (uint32_t i = save.globals.u16(kEventHead); i < n;) {
		const GameMemory<0x18>& e = event(save, i);
		const uint16_t type = e.u16(6);
		const uint32_t when = e.u32(0);
		if (type == 0x20 || (type == 0x21 && e.u8(8) == town) || (type == 0x2E && e.u8(0xC) == town)) {
			if (low <= when && when <= high) {
				tick = (when & ~0xFFu) + 0x200;
			}
		} else if (when > high) {
			break;
		}
		i = e.u16(4);
	}
}

// 0x4FA740 (town_class1 [0x7A4]): la celda de muelle (clase 0x0F) de índice
// más bajo de la rejilla y la orientación del tramo: 0xA0 si la celda
// (x + 1, y − 2) es 0x0D, 0x60 si no. Sin muelle, x = y = 0xFF.
void fn_4fa740(const SaveGame& save, int town, uint8_t& x, uint8_t& y, uint8_t& param) {
	const SavedTown& st = save.towns[size_t(town)];
	const TownMemory& t = st.memory;
	const int32_t h = t.s32(0x7AC);
	x = 0xFF;
	y = 0xFF;
	param = 0;
	// Recorre la rejilla del final al principio; la distancia a la estatua
	// ([0x800]) no se usa: solo descarta la celda si vale 0x7FFFFFFF.
	for (int64_t i = int64_t(int32_t(uint32_t(t.s32(0x7A8)) * uint32_t(h))); i > 0;) {
		--i;
		if (cell(st, i) != 0x0F) {
			continue;
		}
		const int32_t cx = int32_t(i) / h;
		const int32_t cy = int32_t(i) - cx * h;
		const int32_t dx = cx - t.u8(0x800);
		const int32_t dy = cy - t.u8(0x801);
		if (int32_t(uint32_t(dy) * uint32_t(dy) + uint32_t(dx) * uint32_t(dx)) >= 0x7FFFFFFF) {
			continue;
		}
		x = uint8_t(cx);
		y = uint8_t(cy);
		param = cell(st, i + h - 2) == 0x0D ? kSouthWest : kSouthEast;
	}
}

// 0x51F100: saca un registro de la lista de libres [0x760]; si está vacía
// ([0x760] = [0x776]), antes amplía el array en 0x80 registros libres (0xFF)
// encadenados. Devuelve su índice, con el siguiente a 0xFFFF.
uint16_t fn_51f100(SaveGame& save, int town) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	if (t.u16(0x760) == t.u16(0x776)) {
		const uint16_t old = t.u16(0x760);
		const uint16_t n = uint16_t(t.u16(0x776) + 0x80);
		t.u16(0x776) = n;
		assert(st.records.size() == old && n > old);
		const BuildingRecord free{0xFF, 0xFF, 0xFF, 0xFF, 0xFFFF, 0xFF, 0xFF};
		st.records.resize(n, free);
		for (uint16_t k = n; k > old;) {
			--k;
			st.records[k].next = uint16_t(k + 1);
		}
	}
	const uint16_t i = t.u16(0x760);
	assert(i < st.records.size());  // El juego no lo comprueba.
	t.u16(0x760) = st.records[i].next;
	st.records[i].next = 0xFFFF;
	return i;
}

// 0x5254C0: mete el registro `index` en la lista de obras en curso [0x762]:
// los tipos < 0x30, delante del segundo de dos tramos de muralla (0x36)
// seguidos, o al final; el resto, al final.
void fn_5254c0(SaveGame& save, int town, uint32_t index) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	std::vector<BuildingRecord>& records = st.records;
	const uint16_t n = t.u16(0x776);
	uint16_t* link = &t.u16(0x762);  // El enlace que apunta al registro actual.
	if (records[index].type < 0x30) {
		bool wall = false;
		while (*link < n) {
			BuildingRecord& r = records[*link];
			if (r.type == kTypeWall) {
				if (wall) {
					records[index].next = *link;
					*link = uint16_t(index);
					return;
				}
				wall = true;
			} else {
				wall = false;
			}
			link = &r.next;
		}
	// Towers (0x523830) and wall pieces (0x522C70). Checked with the oracle: scenario tower_ops and
	// wall_ops (saves 3 and 5).
	} else {
		while (*link < n) {
			link = &records[*link].next;
		}
	}
	*link = uint16_t(index);
	records[index].next = 0xFFFF;
}

// 0x4F9C80 (town_class1 [0x7A4]): busca un solar para un edificio de tipo
// `type` en una espiral de rombos alrededor de (x, y): la primera celda con
// (celda & máscara) = valor, según el tipo. Algunos tipos tienen un sitio
// fijo. `param` es la orientación (casas) y `flag` el bit 0x10 de [0x2C8] de
// la ciudad (empresas). Devuelve si lo ha encontrado; si no, (x, y) quedan
// como al entrar, salvo que caigan fuera de la rejilla (pasan a la estatua).
bool fn_4f9c80(const SaveGame& save, int town, uint8_t& x, uint8_t& y, uint32_t type, uint8_t param, bool flag) {
	const SavedTown& st = save.towns[size_t(town)];
	const TownMemory& t = st.memory;
	if (int32_t(type) >= 0x39) {
		// No caller asks for these types. Checked with the oracle: scenario site_search (saves 3 and 5, direct calls).
		return false;
	}
	const int32_t w = t.s32(0x7A8);
	const int32_t h = t.s32(0x7AC);
	if (int32_t(x) >= w) {
		x = t.u8(0x800);
	}
	if (int32_t(y) >= h) {
		y = t.u8(0x801);
	}
	// Clase de celda (rejilla, savegame.md §7) que sirve de solar. Las obras
	// de la ciudad solo piden los tipos 0x01, 0x03, 0x15, 0x18, 0x1B,
	// 0x1F..0x21, 0x23, 0x24 y 0x30. Every type (but 0x34 and 0x35, below),
	// with both orientations and both flags: checked with the oracle, scenario
	// site_search (saves 3 and 5, direct calls).
	uint32_t mask = 0x3F;
	uint32_t value = 0;
	switch (type) {
	case 0x01:  // Astillero de reparación: su sitio (town_class1 +0x66).
		x = t.u8(0x80A);
		y = t.u8(0x80B);
		return true;
	case 0x03: value = 0x1D; break;  // Herrería.
	case 0x05: value = 0x0E; break;  // Pescador.
	case 0x04: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
	case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:  // Empresas.
		mask = value = (1u << kSiteBit[type]) | (flag ? 0xA0 : 0x20);
		break;
	case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D:
	case 0x1E:  // Casas y almacén: zonas 0xA1 o 0xA2 según la orientación.
		mask = value = 0xA1 + (param == kNorthWest ? 1 : 0);
		break;
	case 0x1F: value = 0x19; break;  // Iglesia.
	case 0x20: value = 0x1B; break;  // Ayuntamiento.
	case 0x21: value = 0x14; break;  // Arsenal.
	case 0x22: value = 0x1A; break;  // Gremio.
	case 0x23: value = 0x16; break;  // Oficina municipal.
	case 0x24: value = 0x17; break;  // Taberna.
	case 0x25: value = 0x15; break;  // Prestamista.
	case 0x26: case 0x29: case 0x2A: case 0x2B: case 0x2C:  // Baño, hospital, ceca, escuela, capilla.
		mask = value = (1u << kSiteBit[type]) | 0xA0;
		break;
	case 0x27: value = 0x18; break;  // Comercio rural.
	case 0x28: mask = value = 0xA0; break;  // Pozo.
	case 0x2D:  // Estatua: su sitio (+0x5C).
		x = t.u8(0x800);
		y = t.u8(0x801);
		return true;
	case 0x2E:  // Astillero: su sitio (+0x64).
		x = t.u8(0x808);
		y = t.u8(0x809);
		return true;
	case 0x2F: value = 0x0F; break;  // Muelle.
	case 0x30: value = 0x1C; break;  // Torre.
	case 0x37: mask = 0xFF; value = 0x07; break;  // Puertas.
	case 0x38: mask = 0xFF; value = 0x08; break;
	case 0x34:
	case 0x35:
		// No traducido: el juego busca con la máscara y el valor sin
		// inicializar (lo que haya en la pila). Las obras de la ciudad no lo
		// llaman con estos tipos.
		assert(false);
		return false;
	default:  // 0x00, 0x02, 0x31..0x33, 0x36.
		return false;
	}

	const int32_t cx0 = x;
	const int32_t cy0 = y;
	if ((uint32_t(cell(st, int64_t(h) * cx0 + cy0)) & mask) == value) {
		return true;  // Checked with the oracle: scenario site_search (saves 3 and 5, direct calls).
	}
	// Anillos r = 1, 2...: cuatro lados (arriba, derecha, abajo, izquierda)
	// recortados a la rejilla. En la rejilla escalonada, un paso en y avanza
	// media columna, de ahí los pasos de 2 en y.
	int32_t y_top = cy0 - 1;  // Fila de arriba del anillo ([esp+0x24]).
	int32_t below = h - cy0;  // [esp+0x30]
	int32_t above = cy0 - 2;  // [esp+0x34]
	for (int32_t r = 1;; ++r, --y_top, ++below, --above) {
		int32_t cx = cx0 - ((cy0 - 1) & r & 1) - (r >> 1);
		int32_t side = r;  // Celdas de los lados derecho e izquierdo ([esp+0x1C]).
		int32_t row = r;  // Celdas de arriba y abajo (edi).
		bool right = true;
		if (cx + r >= w) {
			row = w - cx - 1;
			right = false;
		}
		bool left = true;
		if (cx < 0) {
			row += cx;
			left = false;
			cx = 0;
		}
		bool bottom = true;
		if (y_top + r * 2 >= h) {
			bottom = false;
			side = below >> 1;
		}
		bool top = true;
		int32_t cy = y_top;
		if (y_top < 0) {
			top = false;
			side += above / 2;
			cy = y_top & 1;
		}
		auto found = [&](int32_t fx, int32_t fy) {
			x = uint8_t(fx);
			y = uint8_t(fy);
			return true;
		};
		auto match = [&]() { return (uint32_t(cell(st, int64_t(h) * cx + cy)) & mask) == value; };
		if (top) {
			for (int32_t k = row; k > 0; --k, ++cx) {
				if (match()) {
					return found(cx, cy);
				}
			}
		} else {
			cx += row;
		}
		if (right) {
			for (int32_t k = side; k > 0; --k, cy += 2) {
				if (match()) {
					return found(cx, cy);
				}
			}
		} else {
			cy += side * 2;
		}
		if (bottom) {
			for (int32_t k = row; k > 0; --k, --cx) {
				if (match()) {
					return found(cx, cy);
				}
			}
		} else {
			cx -= row;
		}
		if (left) {
			for (int32_t k = side; k > 0; --k, cy -= 2) {
				if (match()) {
					return found(cx, cy);
				}
			}
		}
		if (!left && !right && !top && !bottom) {
			return false;
		}
	}
}

// 0x4FA7F0 (town_class1 [0x7A4]): busca sitio para un pozo en la misma
// espiral que 0x4F9C80 alrededor de (x, y): una celda 0x84 con otras dos 0x84
// al lado que formen el triángulo del pozo. Si no hay, (x, y) no cambian.
void fn_4fa7f0(const SaveGame& save, int town, uint8_t& x, uint8_t& y) {
	const SavedTown& st = save.towns[size_t(town)];
	const TownMemory& t = st.memory;
	constexpr uint8_t kWellSite = 0x84;
	const int32_t w = t.s32(0x7A8);
	const int32_t h = t.s32(0x7AC);
	const int32_t cx0 = x;
	const int32_t cy0 = y;
	// ¿Es `i` un solar de pozo? `odd`: la paridad con la que se mira (la del
	// índice en los dos primeros lados, la de y en los otros dos; con alto
	// par son iguales).
	auto site = [&](int64_t i, bool odd) {
		if (cell(st, i) != kWellSite) {
			return false;
		}
		const bool next = cell(st, i + 1) == kWellSite;
		const bool previous = cell(st, i - 1) == kWellSite;
		if (odd) {
			return (next && cell(st, i + h - 1) == kWellSite) || (previous && cell(st, i + h + 1) == kWellSite);
		}
		return (next && cell(st, i - h - 1) == kWellSite) || (previous && cell(st, i - h + 1) == kWellSite);
	};
	for (int32_t r = 1;; ++r) {
		int32_t side = r;  // [esp+0x20]
		int32_t row = r;  // [esp+0x1C]
		int32_t cy = cy0 - r;  // [esp+0x14]
		int32_t cx = cx0 - ((cy0 - 1) & r & 1) - (r >> 1);  // [esp+0x18]
		bool right = true;  // [esp+0x11]
		if (cx + r >= w) {
			row = w - cx - 1;
			right = false;
		}
		bool left = true;  // [esp+0x10]
		if (cx < 0) {
			row += cx;
			cx = 0;
			left = false;
		}
		bool bottom = true;  // [esp+0x13]
		if (cy + r * 2 >= h) {
			side = (h - cy - 1) >> 1;
			bottom = false;
		}
		bool top = true;  // [esp+0x12]
		if (cy < 0) {
			side += (cy - 1) / 2;
			cy &= 1;
			top = false;
		}
		int64_t i = int64_t(h) * cx + cy;
		auto found = [&]() {
			x = uint8_t(cx);
			y = uint8_t(cy);
		};
		if (top) {
			for (int32_t k = row; k > 0; --k, i += h, ++cx) {
				if (site(i, i & 1)) {
					return found();
				}
			}
		} else {
			cx += row;
			i = int64_t(h) * cx + cy;
		}
		if (right) {
			for (int32_t k = side; k > 0; --k, i += 2, cy += 2) {
				if (site(i, i & 1)) {
					return found();
				}
			}
		} else {
			cy += side * 2;
			i = int64_t(h) * cx + cy;
		}
		if (bottom) {
			for (int32_t k = row; k > 0; --k, i -= h, --cx) {
				if (site(i, cy & 1)) {
					return found();
				}
			}
		} else {
			cx -= row;
			i = int64_t(h) * cx + cy;
		}
		if (left) {
			for (int32_t k = side; k > 0; --k, i -= 2, cy -= 2) {
				if (site(i, cy & 1)) {
					return found();
				}
			}
		}
		if (!left && !right && !top && !bottom) {
			return;
		}
	}
}

// 0x51FAB0: busca solar para un edificio de tipo `type` y, si hay materiales
// en el almacén de la ciudad, encola su construcción (operación 0x22).
// Devuelve 1 si la encola, 0x37 si no hay solar y 0 si no se puede (tipo sin
// sitio o faltan materiales). Las casas (0x15..0x1D) se buscan junto a una
// casa de la lista de edificios, elegida con el día de la semana; si no hay,
// o para las torres (0x30), alrededor de la estatua desplazándose al azar.
uint32_t fn_51fab0(SaveGame& save, int town, uint8_t type, bool immediate) {
	TownMemory& t = save.towns[size_t(town)].memory;
	uint8_t x = 0;
	uint8_t y = 0;
	auto near_statue = [&]() {
		x = uint8_t(x + uint8_t((game_rand(save) & 0x1F) - 0x10));
		y = uint8_t(y + uint8_t((game_rand(save) & 0x1F) - 0x10));
	};
	if (type < 0x15) {
		if (type == 0x01) {
			x = t.u8(0x80A);
			y = t.u8(0x80B);
		} else if (type == 0x03) {
			x = t.u8(0x800);
			y = t.u8(0x801);
		} else {
			// The works only ask for 0x01 and 0x03. Checked with the oracle: scenario site_search (saves 3 and 5,
			// direct calls).
			return 0;
		}
	} else if (type < 0x1E) {
		uint32_t skip = save.globals.u32(global::kWorldDayOfYear) & 7;
		x = t.u8(0x800);
		y = t.u8(0x801);
		const std::vector<BuildingRecord>& records = save.towns[size_t(town)].records;
		const uint32_t n = t.u16(0x776);
		for (uint32_t i = t.u16(0x764); i < n; i = records[i].next) {
			const BuildingRecord& r = records[i];
			if (r.type < 0x15 || r.type >= 0x1E) {
				continue;
			}
			x = r.x;
			y = r.y;
			if (type != r.type) {
				--skip;
				continue;
			}
			// A house of the same type. Checked with the oracle: scenario site_search
			// (saves 3 and 5, direct calls).
			if (skip == 0) {
				break;
			}
			skip -= 2;
		}
		if (!immediate && x == t.u8(0x800)) {
			near_statue();
		}
	} else {
		switch (type) {
		case 0x1F: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x2D:
			x = t.u8(0x800);
			y = t.u8(0x801);
			break;
		case 0x26:  // Solar del baño.
			x = t.u8(0x806);  // Checked with the oracle: scenario site_search (saves 3 and 5, direct calls).
			y = t.u8(0x807);
			break;
		case 0x2E:
			x = t.u8(0x808);  // Checked with the oracle: scenario site_search (saves 3 and 5, direct calls).
			y = t.u8(0x809);
			break;
		case kTypeTower:
			x = t.u8(0x800);
			y = t.u8(0x801);
			if (!immediate) {
				near_statue();
			} else {
				// The works call with `immediate` = 0. Checked with the oracle: scenario site_search (saves 3
				// and 5, direct calls).
				x = uint8_t(x + 3 - (save.globals.u8(global::kWorldDayOfYear) & 7));
				y = uint8_t(y + 3 - (save.globals.u8(global::kWorldMonth) & 7));
			}
			break;
		default:
			return 0;
		}
	}
	if (int32_t(kBricks[type]) * kLoad > t.s32(0x50) || int32_t(kTimber[type]) * kLoad > t.s32(0x30) ||
		int32_t(kTools[type]) * kBarrel > t.s32(0x34) || int32_t(kHemp[type]) * kLoad > t.s32(0x48)) {
		return 0;
	}
	uint8_t param = int8_t(t.u8(0x774)) > 0 ? kNorthWest : kNorthEast;
	const bool flag = t.u32(0x2C8) & 0x10;
	if (!fn_4f9c80(save, town, x, y, type, param, flag)) {
		param = param == kNorthEast ? kNorthWest : kNorthEast;
		if (!fn_4f9c80(save, town, x, y, type, param, flag)) {
			return 0x37;
		}
	}
	if (immediate) {
		// Builds at once, as the town (0xFF): 0x521900 for the types < 0x30
		// (its result), 0x523830 up to 0x33 and 0x522C70 for the rest (no type
		// with a site gets there). Only the interface and 0x545F50 (a town that
		// already exists, not rewritten yet) call with it; the town's works
		// (0x51D870) and the event 0x2F always call with 0.
		// Checked with the oracle: scenario site_search (saves 3 and 5, direct calls).
		if (type < 0x30) {
			return fn_521900(save, town, 0xFF, x, y, type, param) ? 1 : 0;
		}
		if (type <= 0x33) {
			fn_523830(save, town, x, y, 0xFF);
			return 1;
		}
		fn_522c70(save, town, 0xFF, type != kTypeWall ? 1 : 0);
		return 1;
	}
	Operation op;
	op.m.u32(0) = 0x22;
	op.m.u8(4) = 0xFF;  // Propietario: la ciudad.
	op.m.u8(5) = t.u8(0x2C0);
	op.m.u8(6) = x;
	op.m.u8(7) = y;
	op.m.u8(8) = type;
	op.m.u8(9) = param;
	op.enqueue(save);
	return 1;
}

// 0x51E270: sin sitio para casas, programa la ampliación de la ciudad (un
// evento 0x2E de tipo 1 dentro de 0x700 ticks, operación 0x48) si no hay obras
// pendientes, el punto de muralla 2 está en la rejilla y no hay ya uno.
void fn_51e270(SaveGame& save, int town) {
	const TownMemory& t = save.towns[size_t(town)].memory;
	if (save.globals.u8(kWorldNoTownWork) != 0 || t.u16(0x766) < t.u16(0x776) || !fn_4fcfe0(save, town, 2)) {
		return;
	}
	for (uint32_t i = save.globals.u16(kEventHead); i < save.globals.u16(kEventCount);) {
		const GameMemory<0x18>& e = event(save, i);
		if (e.u16(6) == 0x2E && e.u8(0xC) == t.u8(0x2C0) && e.u8(0xD) == 1) {
			return;
		}
		i = e.u16(4);
	}
	Operation op;
	op.m.u32(0) = 0x48;
	op.m.u32(4) = save.globals.u32(global::kWorldTick) + 0x700;
	op.m.u32(8) = 0;
	op.m.u8(0xC) = t.u8(0x2C0);
	op.m.u8(0xD) = 1;
	op.m.u8(0xE) = 0;
	op.m.u8(0xF) = 1;
	op.m.u8(0x10) = 0xFF;
	fn_4d9fc0(save, op.m.u32(4), t.u8(0x2C0));
	op.enqueue(save);
}

// 0x5250D0: punto de partida de la búsqueda del pozo número [0x789] + 1: a
// 25, 33, 41... celdas de la estatua en una de ocho direcciones (dando la
// vuelta dentro de la rejilla si se sale), y de ahí 0x4FA7F0.
void fn_5250d0(const SaveGame& save, int town, uint8_t& x, uint8_t& y) {
	const TownMemory& t = save.towns[size_t(town)].memory;
	const uint8_t wells = t.u8(0x789);
	const int32_t k = wells < 1 ? 1 : int32_t(wells) - 1;
	x = t.u8(0x800);
	y = t.u8(0x801);
	int32_t dx;
	int32_t dy;
	if (k & 4) {
		dx = 1 - (k & 1) * 2;
		dy = 1 - (k & 2);
	} else if (k & 2) {
		dx = 1 - (k & 1) * 2;
		dy = 0;
	} else {
		dx = 0;
		dy = 1 - (k & 1) * 2;
	}
	const int32_t distance = (k >> 3) * 8 + 0x19;
	dx *= distance;
	dy *= distance;
	// Fuera de la rejilla (a menos de 2 celdas del borde) se toma un resto. El
	// juego lo repite para x y para y con el mismo código.
	auto place = [](uint8_t& v, int32_t d, int32_t size) {
		// For x: checked with the oracle, scenario site_search (saves 3 and 5, direct calls).
		if (2 - d > int32_t(v)) {
			v = uint8_t(int32_t(uint32_t(d) & 0xFF) % (size - 4) + 2);
		} else if (size < int32_t(v) + d + 2) {
			v = uint8_t(int32_t(uint8_t(uint8_t(d) * 3 + v)) % (size - 4) + 2);
		} else {
			v = uint8_t(v + d);
		}
	};
	place(x, dx, t.s32(0x7A8));
	place(y, dy, t.s32(0x7AC));
	fn_4fa7f0(save, town, x, y);
}

// 0x524D60: si en (x, y) hay un registro de la lista [0x768] y es un tramo de
// muralla (0x36), encola convertirlo en torre (operación 0x22, tipo 0x32).
// Devuelve si lo ha hecho; mira solo el primer registro en esa celda.
bool fn_524d60(SaveGame& save, int town, uint32_t x, uint32_t y) {
	const SavedTown& st = save.towns[size_t(town)];
	const TownMemory& t = st.memory;
	const uint32_t n = t.u16(0x776);
	for (uint32_t i = t.u16(0x768); i < n; i = st.records[i].next) {
		const BuildingRecord& r = st.records[i];
		if (r.x != x || r.y != y) {
			continue;
		}
		if (r.type != kTypeWall) {
			return false;
		}
		Operation op;
		op.m.u32(0) = 0x22;
		op.m.u8(4) = t.u8(0x6F1);
		op.m.u8(5) = t.u8(0x2C0);
		op.m.u8(6) = r.x;
		op.m.u8(7) = r.y;
		op.m.u8(8) = 0x32;
		op.m.u8(9) = 0;
		op.enqueue(save);
		return true;
	}
	return false;
}

// 0x524F00: refuerza la muralla con una torre: junto a una puerta (0x37,
// 0x38) del anillo exterior, a un tramo de muralla en (x ± 3, y − 6) o, si
// no, en (x ∓ 1, y + 2); si no hay, convierte en torre de bombardas (0x33) una
// torre 0x32. El anillo lo marca el propietario del registro (0xFF, 0xFE,
// 0xFD), según qué puntos de muralla estén en la rejilla.
void fn_524f00(SaveGame& save, int town) {
	const SavedTown& st = save.towns[size_t(town)];
	const std::vector<BuildingRecord>& records = st.records;
	const TownMemory& t = st.memory;
	uint32_t ring;
	if (fn_4fcfe0(save, town, 1)) {
		ring = 0xFF;
	} else {
		ring = fn_4fcfe0(save, town, 2) ? 0xFE : 0xFD;
	}
	const uint32_t n = t.u16(0x776);
	for (uint32_t i = t.u16(0x768); i < n; i = records[i].next) {
		const BuildingRecord r = records[i];
		if (r.owner > ring) {
			continue;
		}
		ring = r.owner;
		bool done;
		if (r.type == 0x37) {
			done = fn_524d60(save, town, uint32_t(r.x) + 3, uint32_t(r.y) - 6);
		} else if (r.type == 0x38) {
			done = fn_524d60(save, town, uint32_t(r.x) - 3, uint32_t(r.y) - 6);
		} else {
			continue;
		}
		if (done) {
			return;
		}
	}
	for (uint32_t i = t.u16(0x768); i < n; i = records[i].next) {
		const BuildingRecord r = records[i];
		if (r.owner > ring) {
			continue;
		}
		bool done;
		if (r.type == 0x37) {
			done = fn_524d60(save, town, uint32_t(r.x) - 1, uint32_t(r.y) + 2);
		} else if (r.type == 0x38) {
			done = fn_524d60(save, town, uint32_t(r.x) + 1, uint32_t(r.y) + 2);
		} else {
			continue;
		}
		if (done) {
			return;
		}
	}
	for (uint32_t i = t.u16(0x768); i < n; i = records[i].next) {
		const BuildingRecord& r = records[i];
		if (r.owner > ring || r.type != 0x32) {
			continue;
		}
		Operation op;
		op.m.u32(0) = 0x22;
		op.m.u8(4) = t.u8(0x6F1);
		op.m.u8(5) = t.u8(0x2C0);
		op.m.u8(6) = r.x;
		op.m.u8(7) = r.y;
		op.m.u8(8) = 0x33;
		op.m.u8(9) = 0;
		op.enqueue(save);
		return;
	}
}

// 0x524CD0: encola convertir en torre de bombardas (0x33) la primera torre de
// cañones (0x30) de la lista [0x768].
void fn_524cd0(SaveGame& save, int town) {
	const SavedTown& st = save.towns[size_t(town)];
	const TownMemory& t = st.memory;
	const uint32_t n = t.u16(0x776);
	for (uint32_t i = t.u16(0x768); i < n; i = st.records[i].next) {
		const BuildingRecord& r = st.records[i];
		if (r.type != kTypeTower) {
			continue;
		}
		Operation op;
		op.m.u32(0) = 0x22;
		op.m.u8(4) = t.u8(0x6F1);
		op.m.u8(5) = t.u8(0x2C0);
		op.m.u8(6) = r.x;
		op.m.u8(7) = r.y;
		op.m.u8(8) = 0x33;
		op.m.u8(9) = 0;
		op.enqueue(save);
		return;
	}
}

// 0x524E20: encola rehacer la primera puerta antigua (0x34, 0x35) de la lista
// [0x768] como puerta nueva (tipo − 3), a nombre del primer comerciante que
// sea humano o no sea de la ciudad.
void fn_524e20(SaveGame& save, int town) {
	const SavedTown& st = save.towns[size_t(town)];
	const TownMemory& t = st.memory;
	Operation op;
	op.m.u32(0) = 0x22;
	op.m.u8(5) = t.u8(0x2C0);
	op.m.u8(4) = 0xFF;
	for (uint32_t i = 0; op.m.u8(4) == 0xFF; ++i) {
		if (i >= merchant_count(save)) {
			return;
		}
		const MerchantMemory& m = save.merchants[i].memory;
		if (m.u16(8) == 0 || m.u8(0x19) != t.u8(0x2C0)) {  // [+0x8] tipo (0 humano), [+0x19] ciudad natal.
			op.m.u8(4) = uint8_t(i);
		}
	}
	const uint32_t n = t.u16(0x776);
	for (uint32_t i = t.u16(0x768); i < n; i = st.records[i].next) {
		const BuildingRecord& r = st.records[i];
		if (r.type != 0x34 && r.type != 0x35) {
			continue;
		}
		op.m.u8(6) = r.x;
		op.m.u8(7) = r.y;
		op.m.u8(8) = uint8_t(r.type - 3);
		op.m.u8(9) = r.param;
		op.enqueue(save);
		return;
	}
}

// 0x4FCBD0 (town_class1 [0x7A4]): ¿se puede construir un edificio de tipo
// `type` en (x, y)? Según el tipo (tablas 0x4FCFA4 y 0x4FCF58): los tipos 1
// y 0x2E, en su punto fijo ([0x80A], [0x808]); los edificios municipales y
// el 0x05, en la celda de su clase (& 0x3F); las casas, una celda con los
// bits 0xA1 (0xA2 mirando al noroeste); el pozo, la celda 0x84; las
// empresas y los demás, con el bit de su clase (0x672F84) y 0x20 (0xA0 los
// de la lista de edificios 0x26, 0x29..0x2C); el 0x2D, la celda 0x93 (sin el
// bit 0x40). Los demás (0, 2, más de 0x30), no.
bool fn_4fcbd0(const SaveGame& save, int town, uint8_t x, uint8_t y, uint8_t type, uint8_t param) {
	const SavedTown& st = save.towns[size_t(town)];
	const TownMemory& t = st.memory;
	const uint8_t c = cell(st, int64_t(t.s32(0x7AC)) * x + y);
	auto has = [&](uint8_t mask) { return (c & mask) == mask; };
	auto site = [&](uint8_t extra) { return has(uint8_t(uint8_t(1u << (kSiteBit[type] & 31)) | extra)); };
	switch (type) {
	case 0x01: return x == t.u8(0x80A) && y == t.u8(0x80B);
	case 0x2E: return x == t.u8(0x808) && y == t.u8(0x809);
	case 0x03: return (c & 0x3F) == 0x1D;
	case 0x05: return (c & 0x3F) == 0x0E;
	case 0x1F: return (c & 0x3F) == 0x19;
	case 0x20: return (c & 0x3F) == 0x1B;
	case 0x21: return (c & 0x3F) == 0x14;
	case 0x22: return (c & 0x3F) == 0x1A;
	case 0x23: return (c & 0x3F) == 0x16;
	case 0x24: return (c & 0x3F) == 0x17;
	case 0x25: return (c & 0x3F) == 0x15;
	case kTypeRoad: return (c & 0x3F) == kRoadSite;
	case kTypeTower: return (c & 0x3F) == 0x1C;
	case kTypeWell: return c == 0x84;
	case 0x2D: return (c & 0xBF) == 0x93;
	case 0x26: case 0x29: case 0x2A: case 0x2B: case 0x2C:
		return site(0xA0);
	default:
		if (type >= 0x15 && type <= 0x1E) {
			return has(param == kNorthWest ? 0xA2 : 0xA1);
		}
		if ((type >= 0x04 && type <= 0x14 && type != 0x05) || type == kTypePier) {
			return site(0x20);
		}
		return false;
	}
}

// 0x517770 (sobre el barco): los avisos `flags` en [+0x3C] y, si va en un
// convoy, también en el [+0x18] del convoy.
void fn_517770(SaveGame& save, SavedShip& ship, uint16_t flags) {
	ship.memory.u16(0x3C) |= flags;
	const uint16_t convoy = ship.memory.u16(0x8);
	if (convoy < save.globals.u16(kConvoyCount)) {
		save.convoys[convoy].u32(0x18) |= flags;
	}
}

// 0x5259D0 (sobre la ciudad): ¿tiene ya el comerciante `owner` un edificio de
// tipo `type` en alguno de 20 puntos junto a (x, y)? En la lista de su
// oficina en la ciudad ([+0x2CE], si la tiene) o, suyo, en las obras [0x762].
bool fn_5259d0(const SaveGame& save, int town, uint8_t x, uint8_t y, uint8_t type, uint8_t owner) {
	const SavedTown& st = save.towns[size_t(town)];
	const TownMemory& t = st.memory;
	const int office = facilities_detail::fn_5308a0(save, owner, uint32_t(town));
	// Los 20 puntos: 4 cerca y cada uno de los demás, el de 4 antes un paso en
	// diagonal (y − 1; en los pares, x + 1 si la y de antes era impar; en los
	// impares, x − 1 si la nueva es impar). En bytes, como el juego.
	uint8_t px[20];
	uint8_t py[20];
	const uint8_t side = uint8_t((y & 1) + x);
	px[0] = uint8_t(x + 1);
	py[0] = uint8_t(y + 6);
	px[1] = uint8_t(side - 2);
	py[1] = uint8_t(y + 7);
	px[2] = uint8_t(x - 3);
	py[2] = uint8_t(y - 2);
	px[3] = uint8_t(side + 3);
	py[3] = uint8_t(y - 3);
	for (uint32_t k = 4; k < 20; ++k) {
		px[k] = px[k - 4];
		py[k] = uint8_t(py[k - 4] - 1);
		if ((k & 1) == 0) {
			if (py[k - 4] & 1) {
				px[k] = uint8_t(px[k] + 1);
			}
		} else if (py[k] & 1) {
			px[k] = uint8_t(px[k] - 1);
		}
	}
	auto near = [&](const BuildingRecord& r) {
		for (uint32_t k = 0; k < 20; ++k) {
			if (r.y == py[k] && r.x == px[k]) {
				return true;
			}
		}
		return false;
	};
	const uint32_t count = t.u16(0x776);
	if (office >= 0) {
		for (uint32_t i = save.offices[size_t(office)].memory.u16(0x2CE); i < count;) {
			const BuildingRecord& r = st.records[i];
			i = r.next;
			if (r.type == type && near(r)) {
				return true;
			}
		}
	}
	for (uint32_t i = t.u16(0x762); i < count;) {
		const BuildingRecord& r = st.records[i];
		i = r.next;
		if (r.type == type && r.owner == owner && near(r)) {
			return true;
		}
	}
	return false;
}

// 0x500A70 (sobre la oficina): saca para una obra `amount` de la mercancía
// `ware`. Si el almacén de la oficina no llega, primero de los barcos del
// dueño atracados en su ciudad (estado < 4), dejando en cada uno lo que le
// queda redondeado a la unidad de la mercancía (en todos los que tienen,
// aunque ya no falte nada; 0x517770 con 4 y la bodega, 0x5182B0); después del
// almacén. Deja en `amount` lo que falta.
void fn_500a70(SaveGame& save, int office, uint32_t ware, int32_t& amount) {
	OfficeMemory& o = save.offices[size_t(office)].memory;
	if (o.s32(4 + 4 * ware) < amount) {
		const MerchantMemory& m = save.merchants[o.u16(0x2C4)].memory;
		const uint32_t unit = kWareUnit[ware] == 0 ? 2000 : kWareUnit[ware] == 1 ? 200 : 10;
		const uint32_t ships = save.globals.u16(global::kShipCount);
		for (uint32_t s = m.u16(0xE); s < ships;) {
			SavedShip& ship = save.ships[s];
			s = ship.memory.u16(0x4);
			int32_t& cargo = ship.memory.s32(0x54 + 4 * ware);
			if (ship.memory.u8(0x39) != o.u8(0x2C6) || ship.memory.u16(0x134) >= 4 || cargo <= 0) {
				continue;
			}
			if (amount > cargo) {
				amount -= cargo;
				cargo = 0;
			} else {
				cargo -= amount;
				amount = 0;
				cargo = int32_t(uint32_t(cargo) / unit * unit);
			}
			fn_517770(save, ship, 4);
			recalculate_cargo(ship);
		}
	}
	int32_t& stock = o.s32(4 + 4 * ware);
	if (stock >= amount) {
		stock -= amount;
		amount = 0;
	} else {
		amount -= stock;
		stock = 0;
	}
}

// 0x4F5450 (sobre el comerciante): sin oficina en la ciudad, saca `amount`
// de la mercancía `ware` de sus barcos atracados en ella (estado < 4 y no 2)
// hasta que no falte; cada barco que mira recalcula su bodega (0x5182B0).
void fn_4f5450(SaveGame& save, int merchant, uint32_t ware, uint32_t town, int32_t& amount) {
	if (amount <= 0) {
		return;
	}
	const uint32_t ships = save.globals.u16(global::kShipCount);
	for (uint32_t s = save.merchants[size_t(merchant)].memory.u16(0xE); s < ships;) {
		SavedShip& ship = save.ships[s];
		s = ship.memory.u16(0x4);
		const uint16_t status = ship.memory.u16(0x134);
		if (ship.memory.u8(0x39) != town || status >= 4 || status == 2) {
			continue;
		}
		int32_t& cargo = ship.memory.s32(0x54 + 4 * ware);
		if (cargo >= amount) {
			cargo -= amount;
			amount = 0;
			recalculate_cargo(ship);
			return;
		}
		amount -= cargo;
		cargo = 0;
		recalculate_cargo(ship);
	}
}

// 0x4F89E0 (sobre el comerciante): por un pozo nuevo en la ciudad, si hay
// menos de 5 por cada 2500 habitantes ([0x789] · 2500 / [0x2D4]), el primer
// componente de su reputación allí sube [+0x464] · 0,1.
void fn_4f89e0(SaveGame& save, int merchant, uint32_t town) {
	const TownMemory& t = save.towns[town].memory;
	const int32_t people = t.s32(0x2D4);
	if (people < 1 || int32_t(t.u8(0x789)) * 2500 / people >= 5) {
		return;
	}
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	float& r = m.f32(0x11C + 12 * town);
	r = float(double(m.f32(0x464)) * double(kWellReputation) + double(r));
}

// 0x4F8A50 (sobre el comerciante): por un edificio 0x29 o 0x2C, con `count`,
// si hay menos de uno por cada 5000 habitantes ([0x773] + [0x771]), el
// primer componente de su reputación sube [+0x464]; sin `count`, [+0x464] · 0,01.
void fn_4f8a50(SaveGame& save, int merchant, uint32_t town, bool count) {
	const TownMemory& t = save.towns[town].memory;
	const int32_t people = t.s32(0x2D4);
	// Each branch: checked with the oracle, scenarios chapel_reputation (direct
	// calls) and build_ops (saves 3 and 5). Without `count` only from the
	// operation 0x25 (0x5244F0), which the simulation doesn't queue.
	if (people < 1) {
		return;
	}
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	float& r = m.f32(0x11C + 12 * town);
	if (!count) {
		r = float(double(m.f32(0x464)) * double(kChapelReputation) + double(r));
	} else if ((int32_t(t.u8(0x773)) + t.u8(0x771)) * 5000 < people) {
		r = float(double(m.f32(0x464)) + double(r));
	}
}

// 0x4F8C70 (sobre el comerciante): el primer componente de su reputación en
// la ciudad sube [+0x464] (edificios 0x2A y 0x2B).
void fn_4f8c70(SaveGame& save, int merchant, uint32_t town) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	float& r = m.f32(0x11C + 12 * town);
	r = float(double(m.f32(0x464)) + double(r));  // Checked with the oracle: scenario build_ops (saves 3 and 5).
}

// 0x4F8560 (sobre el comerciante): paga `amount` por el edificio 0x22 de la
// ciudad la primera vez (bit de la ciudad en [+0x468]; sin [0x6DE52A]):
// [+0x4BC] lo suma, lo que ha marcado ([+0x4D8], 0x64 bytes) pasa al global
// 0x6DF278 (0x534920) y el segundo componente de su reputación sube
// [+0x464]. The news 0x0C goes to the chronicle (0x4D6530).
void fn_4f8560(SaveGame& save, int merchant, uint32_t town, int32_t amount) {
	GlobalMemory& g = save.globals;
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	// Each branch: checked with the oracle, scenario build_ops (saves 3 and 5;
	// operations 0x37 and 0x22).
	if (town >= g.u32(global::kWorldTownCount) || m.s32(0) < amount) {
		return;
	}
	const uint32_t bit = 1u << (town & 31);
	if ((m.u32(0x468) & bit) || g.u8(kWorldNoTownWork)) {
		return;
	}
	m.u32(0x468) |= bit;
	m.s32(0x4BC) += amount;
	m.s32(0) -= amount;
	{
		// The guild's entry in the chronicle (the letter 0x0C, 0x4F85F6..
		// 0x4F8639): [+0x5] the town, u16 [+0x8] the merchant, [+0xA] his
		// title (0x4F9430), [+0xC] his rank at home, [+0xE] 0.
		Letter letter(0x0C);
		letter.entry.u8(5) = uint8_t(town);
		letter.entry.u16(8) = uint16_t(merchant);
		letter.entry.u16(0xA) = merchants_detail::fn_4f9430(save, merchant);
		letter.entry.u16(0xC) = m.u8(0x39C + m.u8(0x19));
		letter.entry.u16(0xE) = 0;
		send_letter(save, kLetterToChronicle, letter);
	}
	for (uint32_t k = 0; k < 0x64; k += 4) {
		g.u32(kExplored + k) |= m.u32(0x4D8 + k);
	}
	float& r = m.f32(0x120 + 12 * town);
	r = float(double(m.f32(0x464)) + double(r));
}

// 0x521900 (sobre la ciudad): construye un edificio de tipo `type` (< 0x30)
// del dueño `owner` en (x, y) si el solar vale (0x4FCBD0). Sin dueño (la
// ciudad), salvo la carretera (0x27), exige ladrillos, madera, herramientas y
// cáñamo en su almacén. Según el tipo (tabla 0x522690): los edificios
// municipales, con los de antes ya hechos (bits de [0x76C]), marcan su bit y
// la celda (0x8B); las empresas (salvo en asedio, [0x2C8] & 0x10 fuera de la
// muralla) y los demás se ponen en la rejilla (0x4FB9C0; las casas como la
// primera de su grupo de 3); el ayuntamiento (0x23) pone además la estatua
// (0x2D) en la lista de edificios; el pozo (0x28), uno por cada 500
// habitantes. Después, un registro en las obras [0x762] (0x51F100, 0x5254C0)
// con sus días (0x672BB4). Un comerciante saca los materiales de su oficina
// y sus barcos (0x500A70, 0x4F5450), paga a la ciudad (0x672B78 más lo que
// le falte a precio de compra, 0x52E430) y, según el tipo, gana reputación;
// los edificios municipales que paga pasan a la ciudad (dueño 0xFF). La
// ciudad pierde del almacén los ladrillos, la madera y las herramientas que
// falten (sin bajar de 0). Las casas cuentan en [0x774]. No se reproduce la
// vista de la ciudad (0x590680, 0x5891C0). Devuelve si lo ha construido.
bool fn_521900(SaveGame& save, int town, uint8_t owner, uint8_t x, uint8_t y, uint8_t type, uint8_t param) {
	if (type >= 0x30) {
		// No caller passes it (0x53CE30 sends those types elsewhere).
		return false;  // Checked with the oracle: scenario build_ops (saves 3 and 5, a direct call).
	}
	TownMemory& t = save.towns[size_t(town)].memory;
	const int64_t at = int64_t(t.s32(0x7AC)) * x + y;
	auto grid = [&]() -> uint8_t { return cell(save.towns[size_t(town)], at); };
	auto set_grid = [&](uint8_t v) {
		SavedTown& st = save.towns[size_t(town)];
		if (at >= 0 && at < int64_t(st.grid.size())) {
			st.grid[size_t(at)] = v;
		}
	};
	const bool siege = (t.u8(0x2C8) & 0x10) && grid() < 0x80;
	if (!fn_4fcbd0(save, town, x, y, type, param)) {
		return false;
	}
	int32_t bricks = int32_t(kBricks[type]) * kLoad;
	int32_t timber = int32_t(kTimber[type]) * kLoad;
	int32_t tools = int32_t(kTools[type]) * kBarrel;
	int32_t hemp = int32_t(kHemp[type]) * kLoad;
	if (type == 5 && t.u32(0x2CC) == 0) {
		hemp >>= 1;  // Checked with the oracle: scenario build_ops (saves 3 and 5).
	}
	const bool merchant = owner < save.globals.u16(global::kWorldMerchantCount);
	int office = -1;
	if (merchant) {
		office = facilities_detail::fn_5308a0(save, owner, uint32_t(town));
	} else if (type != kTypeRoad &&
		(t.s32(0x50) < bricks || t.s32(0x30) < timber || t.s32(0x34) < tools || t.s32(0x48) < hemp)) {
		return false;
	}
	const uint32_t bits = t.u32(0x76C);
	// Un edificio municipal: su bit en [0x76C], la celda 0x8B y [0x7A4] = 1.
	auto municipal = [&](uint32_t mask, uint32_t want, uint32_t bit) {
		if ((bits & mask) != want) {
			return false;  // Checked with the oracle: scenario build_ops (saves 3 and 5).
		}
		t.u32(0x76C) = bits | bit;
		set_grid(0x8B);
		t.u32(0x7A4) = 1;
		return true;
	};
	// Las empresas: fuera de la muralla en un asedio, no.
	auto business = [&]() {
		if (siege) {
			return false;  // Checked with the oracle: scenario build_ops (saves 3 and 5).
		}
		if (grid() >= 0x80) {
			++t.u16(0x78C);
		}
		++t.u16(0x78E);
		return true;
	};
	switch (type) {
	case 0x01:
		if (!municipal(0x50, 0x10, 0x40)) {
			return false;
		}
		break;
	case 0x03:
		if (!municipal(0x6, 0x2, 0x4)) {
			return false;
		}
		break;
	case 0x04: case 0x08: case 0x09: case 0x0A: case 0x0F: case 0x10: case 0x14:
		if (!business()) {
			return false;
		}
		param = fn_5259d0(save, town, x, y, type, owner) ? uint8_t(t.u8(0x760) & 0x7F) : uint8_t(0xFF);
		fn_4fb9c0(save, town, x, y, type, param);
		break;
	case 0x06: case 0x07: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x11: case 0x12: case 0x13:
		if (!business()) {
			return false;
		}
		fn_4fb9c0(save, town, x, y, type, param);
		break;
	case 0x05: case 0x1E:
		fn_4fb9c0(save, town, x, y, type, param);
		break;
	case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D:
		type = uint8_t(0x15 + 3 * ((type - 0x15) / 3));
		fn_4fb9c0(save, town, x, y, type, param);
		break;
	case 0x1F:
		if (!municipal(0x27F, 0x7F, 0x200)) {
			return false;
		}
		break;
	case 0x20:
		if (!municipal(0x3, 0x1, 0x2)) {
			return false;
		}
		break;
	case 0x21:
		if (!municipal(0xC, 0x4, 0x8)) {
			return false;
		}
		if (fn_4fcfe0(save, town, 0)) {
			// Checked with the oracle: scenario build_ops (saves 3 and 5).
			events_rare_detail::fn_522b20(save, town, 0);
		}
		break;
	case 0x22:
		if (!municipal(0x2010, 0x10, 0x2000)) {
			return false;
		}
		if (merchant) {
			fn_4f8560(save, owner, uint32_t(town), 0);
		}
		break;
	case 0x23: {
		if (!municipal(0x1, 0x0, 0x1)) {
			return false;
		}
		// La estatua, al principio de la lista de edificios.
		const uint16_t r = fn_51f100(save, town);
		SavedTown& st = save.towns[size_t(town)];
		BuildingRecord& rec = st.records[r];
		rec.x = st.memory.u8(0x800);
		rec.y = st.memory.u8(0x801);
		rec.owner = 0xFF;
		rec.type = 0x2D;
		rec.next = st.memory.u16(0x764);
		rec.pending = 0;
		rec.param = 0;
		st.memory.u16(0x764) = r;
		break;
	}
	case 0x24:
		if (!municipal(0x30, 0x10, 0x20)) {
			return false;
		}
		break;
	case 0x25:
		if (!municipal(0x810, 0x10, 0x800)) {
			return false;
		}
		break;
	case 0x26:
		if ((bits & 0x4010) != 0x10) {
			return false;  // Checked with the oracle: scenario build_ops (saves 3 and 5).
		}
		t.u32(0x76C) = bits | 0x4000;
		fn_4fb9c0(save, town, x, y, 0x26, param);
		break;
	case kTypeRoad:
		set_grid(0x8B);
		t.u32(0x7A4) = 1;
		break;
	case kTypeWell:
		if (int32_t(t.u8(0x789)) * 500 > t.s32(0x2D4)) {
			return false;  // Checked with the oracle: scenario build_ops (saves 3 and 5).
		}
		++t.u8(0x789);
		set_grid(0x92);
		break;
	case 0x29:
		fn_4fb9c0(save, town, x, y, 0x29, param);
		++t.u8(0x772);
		break;
	case 0x2A:
		if ((bits & 0x600) != 0x200) {
			return false;  // Checked with the oracle: scenario build_ops (saves 3 and 5).
		}
		t.u32(0x76C) = bits | 0x400;
		fn_4fb9c0(save, town, x, y, 0x2A, param);
		break;
	case 0x2B:
		if ((bits & 0x1200) != 0x200) {
			return false;  // Checked with the oracle: scenario build_ops (saves 3 and 5).
		}
		t.u32(0x76C) = bits | 0x1000;
		fn_4fb9c0(save, town, x, y, 0x2B, param);
		break;
	case kTypePier:
		fn_4fb9c0(save, town, x, y, 0x2B, param);  // El código del 0x2B tras escribir [0x76C].
		break;
	case 0x2C:
		fn_4fb9c0(save, town, x, y, 0x29, param);
		++t.u8(0x770);
		break;
	case 0x2D:
		param = save.globals.u8(kTownIds + t.u8(0x2C0));
		fn_4fb9c0(save, town, x, y, 0x2D, param);
		break;
	case 0x2E:
		if (!municipal(0x10080, 0x80, 0x10000)) {
			return false;
		}
		break;
	default:  // 0x00 y 0x02.
		return false;
	}
	const uint16_t r = fn_51f100(save, town);
	{
		BuildingRecord& rec = save.towns[size_t(town)].records[r];
		rec.x = x;
		rec.y = y;
		rec.owner = owner;
		rec.type = type;
		rec.next = 0xFFFF;
		rec.pending = kBuildDays[type];
		rec.param = param;
	}
	fn_5254c0(save, town, r);
	// Las casas y el almacén: [0x774] baja si miran al noroeste y si no sube.
	auto houses = [&]() {
		const uint8_t p = save.towns[size_t(town)].records[r].param;
		t.u8(0x774) = uint8_t(p == kNorthWest ? t.u8(0x774) - 1 : t.u8(0x774) + 1);
	};
	if (merchant) {
		if (office >= 0) {
			fn_500a70(save, office, 0x13, bricks);
			fn_500a70(save, office, 0xB, timber);
			fn_500a70(save, office, 0xC, tools);
			fn_500a70(save, office, 0x11, hemp);
		} else {
			// Checked with the oracle: scenario build_ops (saves 3 and 5).
			fn_4f5450(save, owner, 0x13, uint32_t(town), bricks);
			fn_4f5450(save, owner, 0xB, uint32_t(town), timber);
			fn_4f5450(save, owner, 0xC, uint32_t(town), tools);
			fn_4f5450(save, owner, 0x11, uint32_t(town), hemp);
		}
		int32_t fee = int32_t(kBuildFee[type]) * (type == 5 && t.u32(0x2CC) == 0 ? 3 : 5) * 20;
		if (bricks > 0) {
			fee += game_buy_price(save, 0x13, town, bricks);
		}
		if (timber > 0) {
			fee += game_buy_price(save, 0xB, town, timber);
		}
		if (tools > 0) {
			fee += game_buy_price(save, 0xC, town, tools);
		}
		MerchantMemory& m = save.merchants[owner].memory;
		m.s32(0) -= fee;
		m.s32(0x4C0) += fee;
		if (type < 0x15) {
		} else if (type < 0x1E) {
			houses();
		} else if (type == 0x1E) {
			// The store: like the houses, and its office recounts (0x4FF2D0,
			// facilities.cpp). Checked with the oracle: scenario build_ops
			// (saves 3 and 5).
			houses();
			if (office >= 0) {
				fn_4ff2d0(save, office);
			}
		} else if (type == 0x29 || type == 0x2C) {
			fn_4f8a50(save, owner, uint32_t(town), true);
		} else if (type == kTypeWell) {
			fn_4f89e0(save, owner, uint32_t(town));
		} else if (type == 0x2A || type == 0x2B) {
			fn_4f8c70(save, owner, uint32_t(town));
		} else {
			save.towns[size_t(town)].records[r].owner = 0xFF;
		}
	} else if (type < 0x1E) {
		houses();
	}
	t.s32(0x50) -= bricks;
	t.s32(0x30) -= timber;
	t.s32(0x34) -= tools;
	for (const size_t o : {size_t(0x50), size_t(0x30), size_t(0x34)}) {
		if (t.s32(o) < 0) {
			t.s32(o) = 0;
		}
	}
	return true;
}

// 0x523830 (sobre la ciudad): una torre en (x, y) del dueño `owner`. En una
// celda de torre (0x1C), una torre nueva (0x30) a las obras (0x5254C0) y su
// huella (0x4FBDB0). Si no, mejora lo que haya allí en la lista de murallas
// [0x768], que sale de ella: un tramo (0x36) pasa a 0x32, al final de las
// obras; una torre 0x30 a 0x31 y una 0x32 a 0x33, que vuelven al principio
// de [0x768]. En un asedio ([0x2C8] & 0x10) el tramo vuelve tal cual y la
// 0x32 sin cambiar, y no pasa nada más. Con oficina del dueño en la ciudad,
// la ciudad saca de ella los materiales (0x500A70) y le paga un precio fijo
// por unidad (0x673A64...; [+0x0] y [+0x49C]); lo que falta sale de su
// almacén (sin bajar de 0) y apunta como gasto (0x529F90, 6) lo pagado, lo
// que cuesta (0x672B78 · 100) y lo que falta a precio de compra (0x52E430).
// El registro queda con (x, y) y sus días; la torre nueva, sin dueño.
// [0x788] + 1. No se reproduce la vista de la ciudad (0x590680, 0x5891C0).
void fn_523830(SaveGame& save, int town, uint8_t x, uint8_t y, uint8_t owner) {
	TownMemory& t = save.towns[size_t(town)].memory;
	const uint32_t count = t.u16(0x776);
	uint32_t r = 0;
	if ((cell(save.towns[size_t(town)], int64_t(t.s32(0x7AC)) * x + y) & 0x3F) == 0x1C) {
		r = fn_51f100(save, town);
		BuildingRecord& rec = save.towns[size_t(town)].records[r];
		rec.type = kTypeTower;
		rec.next = 0xFFFF;
		fn_5254c0(save, town, r);
		fn_4fbdb0(save, town, x, y);
	} else {
		std::vector<BuildingRecord>& records = save.towns[size_t(town)].records;
		uint16_t* link = &t.u16(0x768);
		r = 0xFFFFF;
		while (*link < count) {
			BuildingRecord& rec = records[*link];
			if (rec.x == x && rec.y == y && (rec.type == kTypeWall || rec.type == kTypeTower || rec.type == 0x32)) {
				r = *link;
				*link = rec.next;
				rec.next = 0xFFFF;
				break;
			}
			link = &rec.next;
		}
		if (r >= count) {
			return;  // Checked with the oracle: scenario tower_ops (saves 3 and 5).
		}
		BuildingRecord& rec = records[r];
		const bool siege = t.u8(0x2C8) & 0x10;
		if (rec.type == kTypeWall) {
			if (siege) {
				rec.next = t.u16(0x768);  // Checked with the oracle: scenario tower_ops (saves 3 and 5).
				t.u16(0x768) = uint16_t(r);
				return;
			}
			rec.type = 0x32;
			uint16_t* end = &t.u16(0x762);
			while (*end < count) {
				end = &records[*end].next;
			}
			*end = uint16_t(r);
		} else if (rec.type == kTypeTower) {
			rec.type = 0x31;
			rec.next = t.u16(0x768);
			t.u16(0x768) = uint16_t(r);
		} else {
			rec.next = t.u16(0x768);
			t.u16(0x768) = uint16_t(r);
			if (siege) {
				return;  // Checked with the oracle: scenario tower_ops_big (saves 3 and 5).
			}
			rec.type = 0x33;
		}
		t.u32(0x7A4) = 1;
	}
	const uint8_t type = save.towns[size_t(town)].records[r].type;
	int32_t bricks = int32_t(kBricks[type]) * kLoad;
	int32_t timber = int32_t(kTimber[type]) * kLoad;
	int32_t tools = int32_t(kTools[type]) * kBarrel;
	int32_t hemp = int32_t(kHemp[type]) * kLoad;
	int32_t paid = 0;
	if (owner < save.globals.u16(global::kWorldMerchantCount)) {
		const int office = facilities_detail::fn_5308a0(save, owner, uint32_t(town));
		if (office >= 0) {
			auto take = [&](uint32_t ware, int32_t& amount, float price) {
				if (amount <= 0) {
					return;
				}
				const int32_t before = amount;
				fn_500a70(save, office, ware, amount);
				paid += int32_t(ftol(double(float(before - amount)) * double(price)));
			};
			take(0x13, bricks, kTowerBricksPrice);
			take(0xB, timber, kTowerTimberPrice);
			take(0xC, tools, kTowerToolsPrice);
			take(0x11, hemp, kTowerHempPrice);
			MerchantMemory& m = save.merchants[owner].memory;
			m.s32(0) += paid;
			m.s32(0x49C) += paid;
		}
	}
	auto lose = [&](size_t o, int32_t amount) {
		t.s32(o) -= amount;
		if (t.s32(o) < 0) {
			t.s32(o) = 0;
		}
	};
	lose(0x50, bricks);
	lose(0x30, timber);
	lose(0x34, tools);
	lose(0x48, hemp);
	int32_t cost = paid + int32_t(kBuildFee[type]) * 100 + game_buy_price(save, 0x13, town, bricks);
	cost += game_buy_price(save, 0xB, town, timber);
	cost += game_buy_price(save, 0xC, town, tools);
	cost += game_buy_price(save, 0x11, town, hemp);
	fn_529f90(save, town, cost, 6);
	BuildingRecord& rec = save.towns[size_t(town)].records[r];
	rec.x = x;
	rec.y = y;
	rec.pending = kBuildDays[type];
	if (type == kTypeTower) {
		rec.owner = 0xFF;
		rec.param = 0;
	}
	++t.u8(0x788);
}

// 0x523D70 (sobre la ciudad; las operaciones de los tipos 0x34 y 0x35): la
// primera pieza de la lista de murallas [0x768] ya terminada en (x, y) baja
// su tipo en 3, con los costes del tipo 0x34. Como en 0x523830, salvo dos
// fallos del juego reproducidos: las herramientas se sacan de la oficina con
// lo que falta de madera (y las que se cuentan no bajan; lo que se paga por
// ellas es 1,278 · 0 = 0), y al comerciante no se le paga, aunque lo que
// valen cuenta como gasto de la ciudad. With fix_game_bugs() the costs are
// worked out as in 0x523830 (each material with its amount and price, and
// the merchant is paid). Sin la pieza, nada.
void fn_523d70(SaveGame& save, int town, uint8_t x, uint8_t y, uint8_t owner) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	const uint32_t count = t.u16(0x776);
	uint32_t r = t.u16(0x768);
	while (r < count && (st.records[r].pending > 0 || st.records[r].x != x || st.records[r].y != y)) {
		r = st.records[r].next;
	}
	if (r >= count) {
		return;
	}
	// Checked with the oracle: scenario tower_ops (saves 3 and 5; the merchant has an office there).
	constexpr uint8_t kGate = 0x34;
	int32_t bricks = int32_t(kBricks[kGate]) * kLoad;
	int32_t timber = int32_t(kTimber[kGate]) * kLoad;
	int32_t tools = int32_t(kTools[kGate]) * kBarrel;
	int32_t hemp = int32_t(kHemp[kGate]) * kLoad;
	int32_t paid = 0;
	if (owner < save.globals.u16(global::kWorldMerchantCount) && fix_game_bugs()) {
		// The fix: as 0x523830 does for the towers. Each material with its
		// own amount and price, and the merchant gets what is paid.
		const int office = facilities_detail::fn_5308a0(save, owner, uint32_t(town));
		if (office >= 0) {
			auto take = [&](uint32_t ware, int32_t& amount, float price) {
				if (amount <= 0) {
					return;
				}
				const int32_t before = amount;
				fn_500a70(save, office, ware, amount);
				paid += int32_t(ftol(double(float(before - amount)) * double(price)));
			};
			take(0x13, bricks, kTowerBricksPrice);
			take(0xB, timber, kTowerTimberPrice);
			take(0xC, tools, kTowerToolsPrice);
			take(0x11, hemp, kTowerHempPrice);
			MerchantMemory& m = save.merchants[owner].memory;
			m.s32(0) += paid;
			m.s32(0x49C) += paid;
		}
	} else if (owner < save.globals.u16(global::kWorldMerchantCount)) {
		const int office = facilities_detail::fn_5308a0(save, owner, uint32_t(town));
		if (office >= 0) {
			auto take = [&](uint32_t ware, int32_t& amount, float price) {
				const int32_t before = amount;
				fn_500a70(save, office, ware, amount);
				return int32_t(ftol(double(float(before - amount)) * double(price)));
			};
			if (bricks > 0) {
				paid = take(0x13, bricks, kTowerBricksPrice);
			}
			if (timber > 0) {
				paid += take(0xB, timber, kTowerTimberPrice);
			}
			if (tools > 0) {
				// Game bug: the tools with the timber's amount, and the
				// counter of the tools doesn't go down.
				fn_500a70(save, office, 0xC, timber);
			}
			if (hemp > 0) {
				paid += take(0x11, hemp, kTowerHempPrice);
			}
		}
	}
	auto lose = [&](size_t o, int32_t amount) {
		t.s32(o) -= amount;
		if (t.s32(o) < 0) {
			t.s32(o) = 0;
		}
	};
	lose(0x50, bricks);
	lose(0x30, timber);
	lose(0x34, tools);
	lose(0x48, hemp);
	int32_t cost = paid + int32_t(kBuildFee[kGate]) * 100 + game_buy_price(save, 0x13, town, bricks);
	cost += game_buy_price(save, 0xB, town, timber);
	cost += game_buy_price(save, 0xC, town, tools);
	cost += game_buy_price(save, 0x11, town, hemp);
	fn_529f90(save, town, cost, 6);
	save.towns[size_t(town)].records[r].type = uint8_t(save.towns[size_t(town)].records[r].type - 3);
	t.u32(0x7A4) = 1;
}

// 0x51F3A0 (sobre la ciudad): lo que cuesta construir el tipo s.u16(0x3C) al
// dueño s.u16(0x3E), en `s` (la estructura del juego, 0x44 bytes): [+0x00]
// lo que pide de ladrillos, madera, herramientas y cáñamo (0x673720), en
// cargas o barriles; [+0x10] lo que tiene el dueño en sus barcos atracados
// en la ciudad (estado < 4 y no 2) y en su oficina; [+0x20] lo que falta
// (que pone la ciudad: si no le llega, [+0x41] = 0 y lo que tiene); [+0x30]
// lo que falta a precio de compra (0x52E430) y, de 0x30 en adelante, lo que
// se le paga al dueño por lo suyo (0x673A18); [+0x34] eso más lo que cuesta
// (0x672B78); [+0x38] los días de obra / 5; [+0x40] si llega el dinero (el
// del dueño o, de 0x30 en adelante, el de la ciudad, [0x700]); [+0x42] si
// puede (con oficina, o es el alcalde, o el tipo 0); [+0x43] por qué no
// (edificio municipal ya hecho, sitio, pozos...). Sin dueño comerciante o
// con tipo 0x39 o más, falso. Devuelve si se puede.
bool fn_51f3a0(SaveGame& save, int town, GameMemory<0x44>& s) {
	// 0x673720: las mercancías de los materiales. 0x673A18: su precio (f32)
	// para lo que se saca de lo del dueño.
	static constexpr uint32_t kMaterials[4] = {0x13, 0xB, 0xC, 0x11};
	static constexpr float kOwnPrice[4] = {0x1.46dc6p-5f, 0x1.c28f5ep-6f, 0x1.472b02p+0f, 0x1.c28f5ep-3f};
	const uint32_t owner = s.u16(0x3E);
	const uint32_t type = s.u16(0x3C);
	if (owner >= save.globals.u16(global::kWorldMerchantCount) || type >= 0x39) {
		return false;
	}
	TownMemory& t = save.towns[size_t(town)].memory;
	int32_t unit[4];
	for (size_t k = 0; k < 4; ++k) {
		unit[k] = kWareUnit[kMaterials[k]] != 0 ? kBarrel : kLoad;
	}
	s.u8(0x43) = 0;
	s.s32(0x30) = 0;
	s.u32(0x0) = kBricks[type];
	s.u32(0x4) = kTimber[type];
	s.u32(0x8) = kTools[type];
	s.u32(0xC) = kHemp[type];
	if (type == 5 && t.u32(0x2CC) == 0) {
		s.s32(0xC) >>= 1;  // Checked with the oracle: scenario cost_fisherman (saves 3 and 5, direct calls).
	}
	for (size_t k = 0; k < 4; ++k) {
		s.s32(0x10 + 4 * k) = 0;
	}
	const uint32_t ships = save.globals.u16(global::kShipCount);
	for (uint32_t i = save.merchants[owner].memory.u16(0xE); i < ships;) {
		const SavedShip& ship = save.ships[i];
		i = ship.memory.u16(0x4);
		const uint16_t status = ship.memory.u16(0x134);
		if (ship.memory.u8(0x39) != t.u8(0x2C0) || status >= 4 || status == 2) {
			continue;
		}
		for (size_t k = 0; k < 4; ++k) {
			s.s32(0x10 + 4 * k) += ship.memory.s32(0x54 + 4 * kMaterials[k]) / unit[k];
		}
	}
	const int office = facilities_detail::fn_5308a0(save, owner, uint32_t(town));
	if (office < 0) {
		s.u8(0x42) = type == 0 || t.u8(0x6F1) == owner ? 1 : 0;
	} else {
		s.u8(0x42) = 1;
		const OfficeMemory& o = save.offices[size_t(office)].memory;
		for (size_t k = 0; k < 4; ++k) {
			s.s32(0x10 + 4 * k) += o.s32(4 + 4 * kMaterials[k]) / unit[k];
		}
	}
	for (size_t k = 0; k < 4; ++k) {
		const int32_t need = s.s32(4 * k);
		const int32_t have = s.s32(0x10 + 4 * k);
		s.s32(0x20 + 4 * k) = need > have ? need - have : 0;
	}
	if (type >= 0x30) {
		for (size_t k = 0; k < 4; ++k) {
			const int32_t have = s.s32(0x10 + 4 * k);
			if (have <= 0) {
				continue;
			}
			const int32_t need = s.s32(4 * k);
			const int32_t value = unit[k] * (need < have ? need : have);
			s.s32(0x30) += int32_t(ftol(double(float(value)) * double(kOwnPrice[k])));
		}
	}
	s.u8(0x41) = 1;
	for (size_t k = 0; k < 4; ++k) {
		if (s.s32(0x20 + 4 * k) <= 0) {
			continue;
		}
		const int32_t amount = s.s32(0x20 + 4 * k) * unit[k];
		s.s32(0x30) += game_buy_price(save, int(kMaterials[k]), town, amount);
		const int32_t stock = t.s32(4 + 4 * kMaterials[k]);
		if (amount > stock) {
			s.u8(0x41) = 0;
			s.s32(0x20 + 4 * k) = stock / unit[k];
		}
	}
	s.s32(0x34) = int32_t(kBuildFee[type]) * (type == 5 && t.u32(0x2CC) == 0 ? 60 : 100) + s.s32(0x30);
	if (type >= 0x30) {
		s.u8(0x40) = 1;
		if (t.s32(0x700) < s.s32(0x34)) {
			s.u8(0x43) = 4;
		}
	} else {
		s.u8(0x40) = save.merchants[owner].memory.s32(0) >= s.s32(0x34) ? 1 : 0;
	}
	s.s32(0x38) = (int32_t(kBuildDays[type]) + 4) / 5;
	const uint32_t bits = t.u32(0x76C);
	auto done = [&](uint32_t bit) {
		if (bits & bit) {
			s.u8(0x43) = 1;
		}
	};
	// Torres y la 0x29: con sitio para muralla al lado 1, o al 2 con piezas
	// pendientes, 2; si no, con pocos habitantes para otra más, 3.
	auto towers = [&](bool enough) {
		const bool wall = fn_4fcfe0(save, town, 1) ||
			(fn_4fcfe0(save, town, 2) && t.u16(0x766) < t.u16(0x776));
		if (wall || !enough) {
			s.u8(0x43) = 3;
		}
		if (s.u8(0x43) == 0 || s.u8(0x43) == 3) {
			if (wall) {
				s.u8(0x43) = 2;
			}
		}
	};
	switch (type) {
	case 0x00:
		if (office >= 0) {
			s.u8(0x43) = 1;
		}
		break;
	case 0x01: done(0x40); break;
	case 0x02:
		if (t.u16(0x790) >= t.u16(0x792)) {
			s.u8(0x43) = 5;
		}
		break;
	case 0x03: done(0x4); break;
	case 0x1F: done(0x200); break;
	case 0x20: done(0x2); break;
	case 0x21: done(0x8); break;
	case 0x22: done(0x2000); break;
	case 0x23: done(0x1); break;
	case 0x24: done(0x20); break;
	case 0x25: done(0x800); break;
	case 0x26: done(0x4000); break;
	case 0x2A: done(0x400); break;
	case 0x2B: done(0x1000); break;
	case 0x2C: towers((t.s32(0x2D4) + 5000) / 10000 > int32_t(t.u8(0x770))); break;
	case 0x29: towers(t.s32(0x2D4) / 10000 >= int32_t(t.u8(0x772))); break;
	case kTypeWell:
		if (int32_t(t.u8(0x789)) * 500 > t.s32(0x2D4)) {
			s.u8(0x43) = 6;
		}
		break;
	default:
		break;
	}
	return s.u8(0x40) && s.u8(0x41) && s.u8(0x42) && s.u8(0x43) == 0;
}

// 0x524B70 (sobre la ciudad): recuenta la lista que empieza en `r`: las
// empresas (tipos < 0x15 salvo el 5) en [0x78E] y, dentro de la ciudad
// (celda ≥ 0x80), en [0x78C]; las torres en [0x788] (0x30..0x33) y lo que
// vale la muralla en [0x786] (0x32, 0x33 y 0x36, 1; 0x34, 0x35 y de 0x37 en
// adelante, 3).
void fn_524b70(SaveGame& save, int town, uint32_t r) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	const uint32_t count = t.u16(0x776);
	while (r < count) {
		const BuildingRecord& rec = st.records[r];
		const uint8_t type = rec.type;
		if (type == 5) {
		} else if (type < 0x15) {
			if (cell(st, int64_t(t.s32(0x7AC)) * rec.x + rec.y) >= 0x80) {
				++t.u16(0x78C);
			}
			++t.u16(0x78E);
		} else if (type < 0x30) {
		} else if (type == 0x30 || type == 0x31) {
			++t.u8(0x788);
		} else if (type == 0x32 || type == 0x33) {
			++t.u8(0x788);
			++t.u16(0x786);
		} else if (type == kTypeWall) {
			++t.u16(0x786);
		} else {
			t.u16(0x786) = uint16_t(t.u16(0x786) + 3);
		}
		r = rec.next;
	}
}

// 0x522C70 (sobre la ciudad; la operación 0x24, {0x24, dueño, ciudad,
// clase}): la siguiente pieza de muralla pendiente ([0x766]) de la clase
// (0, tramos 0x36; 1, las demás) que se pueda pagar (0x51F3A0, con el dueño:
// sin comerciante con oficina en la ciudad no se hace nada) pasa a las obras
// (0x5254C0). Los materiales salen de la oficina y los barcos del dueño, que
// cobra un precio fijo por unidad (como en las torres, 0x523830); la ciudad
// apunta el gasto (0x529F90, 6) y pierde lo que faltaba. En la rejilla, la
// pieza (0x4FBD60; las 0x37/0x38, también dos al lado y lo que recuperan,
// 0x4FD830). En un asedio, nada. Si era la última pendiente, el cierre de la
// muralla: las piezas de ese lado ya hechas o en obras salen de sus listas
// (0x4FACD0 las quita de la rejilla) y se recuentan las demás; [0x76C] |=
// 0x10, el dentro de la ciudad (0x4FD680), los recuentos (0x524B70); las
// puertas 0x34/0x35 en obras sin su pieza se quitan; sin alcalde comerciante,
// cada oficina de la ciudad debe a la ciudad su parte (un evento 0x01 a 20
// días); y los bits de tamaño (0x4FBFF0, 0x4FC130). No se reproducen la
// carta del juego ni la vista de la ciudad.
void fn_522c70(SaveGame& save, int town, uint32_t owner, uint32_t kind) {
	GlobalMemory& g = save.globals;
	TownMemory& t = save.towns[size_t(town)].memory;
	if (t.u8(0x2C8) & 0x10) {
		return;  // Checked with the oracle: scenario wall_ops (saves 3 and 5).
	}
	int office = -1;
	if (owner < g.u16(global::kWorldMerchantCount)) {
		office = facilities_detail::fn_5308a0(save, owner, uint32_t(town));
		if (office < 0) {
			return;
		}
	}
	std::vector<BuildingRecord>& records = save.towns[size_t(town)].records;
	const uint32_t count = t.u16(0x776);
	uint16_t* link = &t.u16(0x766);
	while (*link < count && uint32_t(records[*link].type == kTypeWall) == kind) {
		link = &records[*link].next;
	}
	if (*link >= count) {
		return;
	}
	const uint32_t r = *link;
	GameMemory<0x44> s;
	s.u16(0x3C) = records[r].type;
	s.u16(0x3E) = uint16_t(owner);
	if (!fn_51f3a0(save, town, s)) {
		return;
	}
	*link = records[r].next;
	records[r].next = 0xFFFF;
	fn_5254c0(save, town, r);
	if (office >= 0) {
		int32_t paid = 0;
		auto take = [&](uint32_t ware, int32_t amount, float price) {
			if (amount <= 0) {
				return 0;
			}
			const int32_t before = amount;
			fn_500a70(save, office, ware, amount);
			return int32_t(ftol(double(float(before - amount)) * double(price)));
		};
		paid = take(0x13, s.s32(0x0) * kLoad, kTowerBricksPrice);
		paid += take(0xB, s.s32(0x4) * kLoad, kTowerTimberPrice);
		paid += take(0xC, s.s32(0x8) * kBarrel, kTowerToolsPrice);
		paid += take(0x11, s.s32(0xC) * kLoad, kTowerHempPrice);
		MerchantMemory& m = save.merchants[owner].memory;
		m.s32(0) += paid;
		m.s32(0x49C) += paid;
	}
	fn_529f90(save, town, s.s32(0x34), 6);
	auto lose = [&](size_t o, int32_t amount) {
		t.s32(o) -= amount;
		if (t.s32(o) < 0) {
			t.s32(o) = 0;
		}
	};
	lose(0x50, s.s32(0x20) * kLoad);
	lose(0x30, s.s32(0x24) * kLoad);
	lose(0x34, s.s32(0x28) * kBarrel);
	lose(0x48, s.s32(0x2C) * kLoad);
	const BuildingRecord piece = save.towns[size_t(town)].records[r];
	if (kind != 0) {
		if (piece.type == 0x38) {
			fn_4fbd60(save, town, piece.x - 1u, piece.y - 2u);
			fn_4fbd60(save, town, piece.x - 2u, piece.y - 4u);
		} else {
			fn_4fbd60(save, town, piece.x + 1u, piece.y - 2u);
			fn_4fbd60(save, town, piece.x + 2u, piece.y - 4u);
		}
		fn_4fd830(save, town, piece.x, piece.y, piece.type);
	}
	fn_4fbd60(save, town, piece.x, piece.y);
	if (t.u16(0x766) < t.u16(0x776)) {
		return;
	}
	// El cierre de la muralla (no sale en 10 días de las 12 partidas).
	// Checked with the oracle: scenario wall_ops (saves 3 and 5).
	std::vector<uint8_t> done;  // (x, y) de las piezas quitadas.
	const uint8_t side = piece.owner;  // 0xFF − lado (0x522B20).
	t.u16(0x786) = 0;
	{
		std::vector<BuildingRecord>& recs = save.towns[size_t(town)].records;
		uint16_t* l = &t.u16(0x768);
		while (*l < t.u16(0x776)) {
			const uint16_t i = *l;
			BuildingRecord& rec = recs[i];
			if (rec.type >= 0x32 && side < rec.owner) {
				fn_4facd0(save, town, rec.x, rec.y, rec.type, rec.param);
				done.push_back(rec.x);
				done.push_back(rec.y);
				*l = rec.next;
				rec.next = t.u16(0x760);
				t.u16(0x760) = i;
				continue;
			}
			if (rec.type >= 0x32) {
				t.u16(0x786) = uint16_t(t.u16(0x786) + (rec.type == kTypeWall || rec.type < 0x34 ? 1 : 3));
			}
			l = &rec.next;
		}
		if (t.u16(0x786) == 0) {
			t.u16(0x786) = 1;
		}
		l = &t.u16(0x762);
		while (*l < t.u16(0x776)) {
			const uint16_t i = *l;
			BuildingRecord& rec = recs[i];
			if (side < rec.owner && rec.type >= 0x32) {
				fn_4facd0(save, town, rec.x, rec.y, rec.type, rec.param);
				*l = rec.next;
				rec.next = t.u16(0x760);
				t.u16(0x760) = i;
				rec.type = 0xFF;
				continue;
			}
			l = &rec.next;
		}
		for (uint32_t i = t.u16(0x762); i < t.u16(0x776); i = recs[i].next) {
			if (recs[i].type >= 0x32) {
				t.u16(0x786) = uint16_t(t.u16(0x786) + (recs[i].type <= kTypeWall ? 1 : 3));
			}
		}
	}
	t.u32(0x76C) |= 0x10;
	fn_4fd680(save, town);
	t.u8(0x788) = 0;
	t.u16(0x78C) = 1;
	t.u16(0x78E) = 1;
	t.u16(0x786) = 0;
	fn_524b70(save, town, t.u16(0x768));
	const uint16_t offices = g.u16(global::kWorldOfficeCount);
	for (uint32_t o = t.u16(0x784); o < offices;) {
		const OfficeMemory& om = save.offices[o].memory;
		o = om.u16(0x2CA);
		fn_524b70(save, town, om.u16(0x2CE));
	}
	if (t.u16(0x786) == 0) {
		t.u16(0x786) = 1;
	}
	// Las puertas (0x34, 0x35) en obras sin su pieza (+ 3) en las murallas.
	{
		std::vector<BuildingRecord>& recs = save.towns[size_t(town)].records;
		uint16_t* l = &t.u16(0x762);
		while (*l < t.u16(0x776)) {
			const uint16_t i = *l;
			BuildingRecord& rec = recs[i];
			if (rec.type != 0x34 && rec.type != 0x35) {
				l = &rec.next;
				continue;
			}
			bool kept = false;
			for (uint32_t j = t.u16(0x768); j < t.u16(0x776); j = recs[j].next) {
				const BuildingRecord& w = recs[j];
				if (w.type >= 0x37 && w.type - rec.type == 3 && w.x == rec.x && w.y == rec.y) {
					kept = true;
					break;
				}
			}
			if (kept) {
				l = &rec.next;
				continue;
			}
			*l = rec.next;
			rec.next = t.u16(0x760);
			t.u16(0x760) = i;
			rec.x = 0xFF;
			rec.y = 0xFF;
			rec.owner = 0xFF;
			rec.type = 0xFF;
			rec.pending = 0;
			rec.param = 0;
		}
	}
	// Sin alcalde comerciante, cada oficina debe su parte de la muralla.
	if (t.u8(0x6F1) >= g.u16(global::kWorldMerchantCount)) {
		const std::vector<BuildingRecord>& recs = save.towns[size_t(town)].records;
		for (uint32_t o = t.u16(0x784); o < g.u16(global::kWorldOfficeCount);) {
			const OfficeMemory& om = save.offices[o].memory;
			o = om.u16(0x2CA);
			int32_t sum = 0;
			for (const size_t head : {size_t(0x2CE), size_t(0x2D0)}) {
				for (uint32_t i = om.u16(head); i < t.u16(0x776); i = recs[i].next) {
					sum += kBuildFee[recs[i].type < 60 ? recs[i].type : 0];
				}
			}
			const int32_t cap = int32_t(t.u16(0x786)) * 500;
			if (sum > cap) {
				sum = cap;
			}
			{
				// To a human owner with something to pay, the letter 0x2F
				// (0x5236A1..0x523702): [+0x5] the town's [+0x2C0]; u16 [+0x8] his
				// [+0x1C] << 8 | [+0x1D]; [+0xA] the same of the mayor, or of the
				// town's bytes [0x6D4 + k], [0x6D8 + k] (k = (−1 − mayor) & 3)
				// when he isn't a merchant; [+0xC] the sum.
				const uint16_t owner = om.u16(0x2C4);
				const MerchantMemory& pm = save.merchants[owner < save.merchants.size() ? owner : 0].memory;
				if (pm.u16(0x8) == 0 && sum > 0) {
					Letter letter(0x2F);
					letter.entry.u8(5) = t.u8(0x2C0);
					letter.entry.u16(8) = uint16_t((pm.u8(0x1C) << 8) + pm.u8(0x1D));
					const uint8_t mayor = t.u8(0x6F1);
					if (mayor < g.u16(global::kWorldMerchantCount)) {
						const MerchantMemory& mm = save.merchants[mayor].memory;
						letter.entry.u16(0xA) = uint16_t((mm.u8(0x1C) << 8) + mm.u8(0x1D));
					} else {
						const size_t k = size_t(uint32_t(-1 - int32_t(mayor)) & 3);
						letter.entry.u16(0xA) = uint16_t((t.u8(0x6D4 + k) << 8) + t.u8(0x6D8 + k));
					}
					letter.entry.u16(0xC) = uint16_t(sum);
					send_letter(save, owner, letter);
				}
			}
			const int32_t amount = sum * 100;
			if (amount <= 0) {
				continue;
			}
			const uint8_t debtor = uint8_t(om.u16(0x2C4));
			const uint16_t e = events_detail::fn_4d8cf0(save, g.u32(global::kWorldTick) + 0x1400);
			GameMemory<0x18>& ev = save.events[e];
			ev.u16(0x6) = 1;
			ev.u8(0x12) = 0;
			ev.u8(0x15) = 0;
			ev.u8(0x16) = 0;
			ev.u8(0x17) = 0;
			ev.s32(0x8) = amount;
			ev.u8(0x13) = 0;
			ev.u8(0x10) = 0xFF;
			ev.u8(0x11) = debtor;
			ev.u8(0x14) = t.u8(0x2C0);
			ev.u32(0xC) = g.u32(global::kWorldTick) & ~0xFFu;
			save.merchants[debtor].memory.s32(0x4BC) += amount;
			fn_529f90(save, town, amount, 2);
		}
	}
	for (size_t k = done.size(); k >= 2; k -= 2) {
		fn_4fbff0(save, town, done[k - 2], done[k - 1], 0);
	}
	for (int32_t x = t.s32(0x7A8) - 12; x > 12; x -= 18) {
		fn_4fc130(save, town, x);
	}
}

// La operación 0x27 (0x535BBC, en el ejecutor): {0x27, u32 ciudad (el byte
// bajo; con el bit 31, también 0x672B01 · 2000 de cáñamo), u32 ladrillos,
// u32 madera, u32 herramientas}: la ciudad los recibe en su almacén. La
// encolan las obras de la ciudad cuando le faltan (0x51D870).
void fn_535bbc(SaveGame& save, const uint8_t* operation) {
	uint32_t fields[5];
	std::memcpy(fields, operation, sizeof(fields));
	const uint32_t town = fields[1] & 0xFF;
	// Each branch: checked with the oracle, scenario build_ops (saves 3 and 5).
	if (town >= save.globals.u32(global::kWorldTownCount)) {
		return;
	}
	TownMemory& t = save.towns[town].memory;
	if (int32_t(fields[1]) < 0) {
		t.u32(0x48) += uint32_t(kHemp[1]) * uint32_t(kLoad);
	}
	t.u32(0x50) += fields[2];
	t.u32(0x30) += fields[3];
	t.u32(0x34) += fields[4];
}

// La operación 0x24 (0x535B44): {0x24, u32 dueño, u32 ciudad, u32 clase}:
// la siguiente pieza de muralla pendiente (0x522C70).
void fn_535b44(SaveGame& save, const uint8_t* operation) {
	uint32_t fields[4];
	std::memcpy(fields, operation, sizeof(fields));
	if (fields[2] >= save.globals.u32(global::kWorldTownCount)) {
		return;  // Checked with the oracle: scenario build_ops (saves 3 and 5).
	}
	fn_522c70(save, int(fields[2]), fields[1], fields[3]);
}

// La operación 0x23 (0x535B12): {0x23, u32 lado, u32 ciudad}: empieza la
// muralla de ese lado (0x522B20).
void fn_535b12(SaveGame& save, const uint8_t* operation) {
	uint32_t fields[3];
	std::memcpy(fields, operation, sizeof(fields));
	if (fields[2] >= save.globals.u32(global::kWorldTownCount)) {
		return;  // Checked with the oracle: scenario build_ops (saves 3 and 5).
	}
	events_rare_detail::fn_522b20(save, int(fields[2]), fields[1]);
}

// 0x53CE30 (la operación 0x22, construir; 0x21 con `request`): {0x22,
// [+4] u8 dueño, [+5] u8 ciudad, [+6] x, [+7] y, [+8] tipo, [+9] parámetro}.
// Según el tipo: hasta 0x2F, 0x521900; 0x30..0x33, las torres (0x523830);
// 0x34 y 0x35, 0x523D70; 0x36..0x39, la muralla de ese lado (0x522B20). La
// 0x21 (solo desde la interfaz) no se reproduce.
void fn_53ce30(SaveGame& save, const uint8_t* operation) {
	const uint8_t owner = operation[4];
	const uint32_t town = operation[5];
	const uint8_t x = operation[6];
	const uint8_t y = operation[7];
	const uint8_t type = operation[8];
	if (town >= save.globals.u32(global::kWorldTownCount)) {
		return;  // Checked with the oracle: scenario build_ops (saves 3 and 5).
	}
	if (type < 0x30) {
		fn_521900(save, int(town), owner, x, y, type, operation[9]);
	} else if (type <= 0x33) {
		fn_523830(save, int(town), x, y, owner);
	} else if (type <= 0x35) {
		fn_523d70(save, int(town), x, y, owner);
	} else if (type <= 0x39) {
		// Checked with the oracle: scenario build_ops (saves 3 and 5).
		events_rare_detail::fn_522b20(save, int(town), uint32_t(type - 0x36));
	}
}

// 0x521240 (sobre la ciudad): borra el edificio `record` y los que le siguen
// en su lista ([+0x4]; quien lo llama la corta antes): cada uno vuelve a la
// lista de libres [0x760], se quita de la rejilla (0x4FACD0) y queda con x, y,
// dueño y tipo a 0xFF y las obras y el parámetro a 0.
void fn_521240(SaveGame& save, int town, uint32_t record) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	for (uint32_t r = record; r < t.u16(0x776) && r < st.records.size();) {
		BuildingRecord& b = st.records[r];
		const uint16_t next = b.next;
		b.next = t.u16(0x760);
		t.u16(0x760) = uint16_t(r);
		fn_4facd0(save, town, b.x, b.y, b.type, b.param);
		b.x = 0xFF;
		b.y = 0xFF;
		b.owner = 0xFF;
		b.type = 0xFF;
		b.pending = 0;
		b.param = 0;
		r = next;
	}
}

// Días de obra del tipo de edificio (0x672BB4).
uint8_t build_days(uint8_t type) {
	return type < 60 ? kBuildDays[type] : 0;
}

}  // namespace construction_detail

// 0x524920: tramo nuevo de muelle (0x2F) si `workers` (la suma de lo que
// devuelve 0x4FFD40 para las oficinas de la ciudad) supera n · ((n + 1) / 2)
// · 30 para los n muelles de la lista de edificios y hay 10 cargas de madera
// y 10 barriles de herramientas: lo coloca en la celda de muelle (0x0F) de
// índice más bajo de la rejilla, lo mete en obras ([0x762]) y marca la
// rejilla. Sin sitio, pone el bit 0x1000000 de [0x2C8] y no lo vuelve a
// intentar.
void fn_524920(SaveGame& save, int town, int32_t workers) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	if ((t.u32(0x2C8) & 0x1000000) == 0x1000000) {
		return;
	}
	// En las 12 partidas todas las ciudades con oficinas tienen ya el bit
	// 0x1000000: lo que sigue solo se ha comprobado con el estado retocado.
	uint32_t piers = 0;
	const uint32_t n = t.u16(0x776);
	for (uint32_t i = t.u16(0x764); i < n; i = st.records[i].next) {
		if (st.records[i].type == kTypePier) {
			++piers;
		}
	}
	if (piers > 0) {
		piers *= ((piers + 1) >> 1) * kPierWorkersFactor;
	}
	if (piers >= uint32_t(workers)) {
		return;
	}
	if (t.s32(0x30) < int32_t(kTimber[kTypePier]) * kLoad || t.s32(0x34) < int32_t(kTools[kTypePier]) * kBarrel) {
		return;
	}
	uint8_t x;
	uint8_t y;
	uint8_t param;
	fn_4fa740(save, town, x, y, param);
	if (int32_t(x) >= t.s32(0x7A8)) {
		t.u32(0x2C8) |= 0x1000000;
		return;
	}
	const uint16_t k = fn_51f100(save, town);
	BuildingRecord& r = st.records[k];
	r.x = x;
	r.y = y;
	r.owner = 0xFF;
	r.type = kTypePier;
	r.next = 0xFFFF;
	r.pending = kPierBuildTime;
	r.param = param;
	fn_5254c0(save, town, k);
	// Celdas del muelle en obras: la del ancla, las dos de debajo y la de al
	// lado (rejilla escalonada).
	// Game bug: the game doesn't check the cells. With the pier in the last
	// column (and y odd) the fourth one is past the grid: the game writes
	// into whatever follows it, p3vita skips it; with y < 2 a cell above the
	// pier is the last ones of the previous column (inside the grid: written
	// as the game does). With fix_game_bugs() only the cells inside the grid
	// (column and row) are marked. Checked with the oracle: scenario
	// pier_edge (saves 3 and 5, direct calls).
	const int64_t h = t.s32(0x7AC);
	auto occupy = [&](int64_t column, int64_t row) {
		if (fix_game_bugs()) {
			if (column >= 0 && column < t.s32(0x7A8) && row >= 0 && row < h &&
				h * column + row < int64_t(st.grid.size())) {
				st.grid[size_t(h * column + row)] = 0x0B;
			}
			return;
		}
		const int64_t i = h * column + row;
		if (i >= 0 && i < int64_t(st.grid.size())) {
			st.grid[size_t(i)] = 0x0B;
		}
	};
	occupy(x, y);
	occupy(x, int64_t(y) - 1);
	occupy(x, int64_t(y) - 2);
	occupy((y & 1) ? int64_t(x) + 1 : int64_t(x) - 1, int64_t(y) - 1);
	// Después, si la vista de la ciudad (*0x6E51AC) es la de esta ciudad,
	// 0x590680 y 0x5891C0 crean el objeto del muelle en la vista isométrica:
	// solo dibujo, no cambian la simulación (el objeto del tipo de .let 0x5F,
	// 0x554AF0/0x554C70, no llama a rand) y no se traducen.
}

// 0x51E070: avisos a la ciudad (eventos 0x2E). Si no hay ya un evento 0x2E de
// la ciudad en los próximos 0x1E00 ticks, encola la operación 0x48 (evento
// 0x2E dentro de 0xE00 ticks) con el motivo más urgente ([+0xD]): 2 si
// ([0x9A0] − [0x999] − [0x99A] − [0x99B]) · 2000 / habitantes < 4; 1 si no
// hay obras pendientes ([0x766]) y [0x78C] va muy por detrás de [0x78E]; 3 si
// el byte bajo de [0x6F8] pasa de 10. Urgencia: 4 menos el cociente (el 3,
// byte² / 20 − 5).
void fn_51e070(SaveGame& save, int town) {
	const TownMemory& t = save.towns[size_t(town)].memory;
	const uint32_t tick = save.globals.u32(global::kWorldTick);
	if (tick < t.u32(0x6FC)) {
		return;
	}
	const uint8_t index = t.u8(0x2C0);
	if (fn_4d9f60(save, index) != 0) {
		return;
	}
	Operation op;
	op.m.u32(0) = 0x48;
	op.m.u32(4) = tick + 0xE00;
	op.m.u32(8) = 0;
	op.m.u8(0xC) = index;
	op.m.u8(0xD) = 0xFF;  // Motivo.
	op.m.u8(0xE) = 0;
	op.m.u8(0xF) = 0;
	op.m.u8(0x10) = 0xFF;
	uint32_t urgency = 0;
	const uint32_t citizens = t.u32(0x2D4);
	if (int32_t(citizens) > 0) {
		const uint32_t used = (uint32_t(t.u8(0x99B)) + t.u8(0x99A) + t.u8(0x999)) * kLoad;
		const uint32_t q = (uint32_t(t.u16(0x9A0)) * kLoad - used) / citizens;
		if (q < 4) {
			urgency = 4 - q;
			op.m.u8(0xD) = 2;
		}
	}
	if (t.u16(0x766) >= t.u16(0x776)) {
		const int32_t built = t.u16(0x78C);
		const int32_t total = t.u16(0x78E);
		if (total - built > int32_t(index) % 5 + 0xF) {
			const uint32_t q = uint32_t(built * 4 / total);
			if (q < 4 && 4 - q > urgency) {
				op.m.u8(0xD) = 1;
				urgency = 4 - q;
			}
		}
	}
	const uint32_t v = t.u8(0x6F8);
	const uint32_t s = v * v / 20;
	if (s > 5 && s - 5 > urgency) {
		op.m.u8(0xD) = 3;
		op.m.u8(0xE) = 0x0A;
	}
	if (op.m.u8(0xD) == 0xFF) {
		return;
	}
	fn_4d9fc0(save, op.m.u32(4), index);
	op.enqueue(save);
}

// 0x51D870: obras de la ciudad, una cosa según el día de la semana (día del
// año & 7): 0 las obras pendientes de la lista [0x766] (operación 0x24); 1, 3
// y 5 casas de ricos, acomodados o pobres si faltan; 4 edificios municipales;
// 6 pozos; 7 murallas, torres y puertas (solo un día de cada 16 por ciudad).
// El 2, nada.
void fn_51d870(SaveGame& save, int town) {
	SavedTown& st = save.towns[size_t(town)];
	const TownMemory& t = st.memory;
	const std::vector<BuildingRecord>& records = st.records;
	const uint32_t n = t.u16(0x776);
	Operation op;
	const uint32_t day = save.globals.u32(global::kWorldDayOfYear);  // Se lee el u32 entero.
	// ¿Hay un registro de tipo `type` en la lista que empieza en `head`?
	auto in_list = [&](uint32_t head, uint8_t type) {
		for (uint32_t i = head; i < n; i = records[i].next) {
			if (records[i].type == type) {
				return true;
			}
		}
		return false;
	};
	// Casas (0x51DD18): sin solar, programa la ampliación.
	auto build_houses = [&](uint8_t type) {
		if (fn_51fab0(save, town, type, false) == 0x37) {
			fn_51e270(save, town);
		}
	};
	// Pedido de materiales (operación 0x27) con un factor al azar `r`/32 · 200.
	auto order_materials = [&](uint8_t type, uint32_t r) {
		op.m.u32(8) = kBricks[type];
		op.m.u32(0xC) = kTimber[type];
		op.m.u32(0x10) = kTools[type];
		const uint32_t f = uint32_t(int32_t(r * 200) >> 5);
		op.m.u32(0x10) = op.m.u32(0x10) * f;
		op.m.u32(8) = op.m.u32(8) * (f * 10);
		op.m.u32(0xC) = op.m.u32(0xC) * (f * 10);
		op.enqueue(save);
	};
	const bool bit40 = t.u8(0x2C8) & 0x40;  // Bit 0x40 de [0x2C8] (significado sin aclarar).
	switch (day & 7) {
	case 0: {
		// Obras pendientes: la operación 0x24 a nombre del comerciante [0x6F1]
		// o del primero con oficina en la ciudad que tenga el bit 4 de [+0x8];
		// doce veces si la obra es un tramo de muralla.
		if (t.u16(0x766) >= n) {
			return;
		}
		op.m.u32(0) = 0x24;
		op.m.u32(8) = t.u8(0x2C0);
		if (t.u8(0x6F1) < merchant_count(save)) {
			op.m.u32(4) = t.u8(0x6F1);
		} else {
			op.m.u32(4) = 0xFF;
			for (uint32_t i = t.u16(0x784); i < office_count(save);) {
				const OfficeMemory& o = save.offices[i].memory;
				const uint16_t owner = o.u16(0x2C4);
				if (save.merchants[owner].memory.u8(8) & 4) {
					op.m.u32(4) = owner;
					break;
				}
				i = o.u16(0x2CA);
			}
		}
		const uint32_t once = records[t.u16(0x766)].type != kTypeWall;
		op.m.u32(0xC) = once;
		for (uint32_t k = 0; k < 12; ++k) {
			op.enqueue(save);
			if (once) {
				return;
			}
		}
		return;
	}
	case 1: {
		// Casas de ricos (0x15): [0x2D8] habitantes ricos (· 100 / 97 salvo
		// con el bit 0x40 de [0x2C8]) frente a [0x2F8] (u16 por clase, que
		// recalcula la población con las casas de cada clase).
		uint32_t need = t.u32(0x2D8);
		if (!bit40) {
			need = need * 100 / 97;
		}
		if (need <= t.u16(0x2F8) || in_list(t.u16(0x762), 0x15)) {
			return;
		}
		build_houses(0x15);
		return;
	}
	case 3: {
		// Casas de acomodados (0x18): [0x2DC] · 100 / 99 (bit 0x40) o / 96.
		const uint32_t need = t.u32(0x2DC) * 100 / (bit40 ? 99 : 96);
		if (need <= t.u16(0x2FA)) {
			return;
		}
		if (in_list(t.u16(0x762), 0x18)) {
			return;
		}
		build_houses(0x18);
		return;
	}
	case 5: {
		// Casas de pobres (0x1B): [0x2E0] · 100 / 98 (bit 0x40) o / 95.
		const uint32_t need = t.u32(0x2E0) * 100 / (bit40 ? 98 : 95);
		if (need <= t.u16(0x2FC) || in_list(t.u16(0x762), 0x1B)) {
			return;
		}
		build_houses(0x1B);
		return;
	}
	case 4: {
		// Edificios municipales, el primero que falte según [0x76C]; con el
		// bit 0x200 no se construye ninguno.
		const uint32_t built = t.u32(0x76C);
		if (built & 0x200) {
			return;
		}
		// Todas las ciudades de las 12 partidas tienen el bit 0x200: lo que
		// sigue solo se ha comprobado con el estado retocado.
		uint8_t type;
		if (!(built & 1)) {
			type = 0x23;  // Oficina municipal.
		} else if (!(built & 2)) {
			type = 0x20;  // Ayuntamiento.
		} else if (!(built & 4)) {
			type = 0x03;  // Herrería.
		} else if (!(built & 8)) {
			type = 0x21;  // Arsenal.
		} else if (!(built & 0x10)) {
			// Con el punto de muralla 0 en la rejilla, la operación 0x23; si
			// no, materiales para una puerta (0x37).
			if (fn_4fcfe0(save, town, 0)) {
				op.m.u32(0) = 0x23;
				op.m.u32(4) = 0;
				op.m.u32(8) = t.u8(0x2C0);
				op.enqueue(save);
				return;
			}
			op.m.u32(0) = 0x27;
			op.m.u32(4) = t.u8(0x2C0);
			order_materials(0x37, (game_rand(save) & 0x1F) + 0x25);
			return;
		} else if (!(built & 0x20)) {
			type = 0x24;  // Taberna.
		} else {
			type = (built & 0x40) ? 0x1F : 0x01;  // Iglesia o astillero de reparación.
		}
		if (fn_51fab0(save, town, type, false) != 0) {
			return;
		}
		// Sin solar no: faltan materiales; se piden (operación 0x27). El
		// código del juego tiene aquí una rama para el tipo 0x40 que no puede
		// darse.
		op.m.u32(0) = 0x27;
		op.m.u32(4) = t.u8(0x2C0);
		order_materials(type, (game_rand(save) & 0xF) + 0x18);
		return;
	}
	case 6: {
		// Pozos: uno por cada 500 habitantes (operación 0x22, tipo 0x28).
		if (bit40 || int32_t(t.u8(0x789) * 500) >= t.s32(0x2D4)) {
			return;
		}
		op.m.u32(0) = 0x22;
		const uint8_t owner = t.u8(0x6F1);
		op.m.u8(4) = owner < merchant_count(save) ? owner : t.u8(0x2C0);
		op.m.u8(5) = t.u8(0x2C0);
		op.m.u8(8) = kTypeWell;
		op.m.u8(9) = 0;
		fn_5250d0(save, town, op.m.u8(6), op.m.u8(7));
		op.enqueue(save);
		return;
	}
	case 7: {
		// Murallas: cada 16 días por ciudad ((índice & 0xF) · 8 = día & 0x78);
		// qué hacer sale de [0x2E0] % 7, o 0 si un jugador humano tiene aquí su
		// ciudad natal (con el bit 0x40 de [0x2C8]).
		int32_t what = t.s32(0x2E0) % 7;
		if ((t.u32(0x2C8) & 0x40) == 0x40) {
			for (uint32_t i = merchant_count(save); i > 0;) {
				--i;
				const MerchantMemory& m = save.merchants[i].memory;
				if (m.u16(8) == 0 && m.u8(0x19) == t.u8(0x2C0)) {
					what = 0;
				}
			}
		}
		if (((uint32_t(t.u8(0x2C0)) & 0xF) << 3) != (day & 0x78) || uint32_t(what) > 6) {
			return;
		}
		switch (what) {
		case 0:
		case 1:
			// Torres en la muralla, si no hay ya una 0x32 en obras.
			if ((t.u8(0x2C8) & 0x10) || in_list(t.u16(0x762), 0x32)) {
				return;
			}
			fn_524f00(save, town);
			return;
		case 2:
			// Lo mismo si ya hay alguna torre 0x32 en la muralla.
			if ((t.u8(0x2C8) & 0x10) || !in_list(t.u16(0x768), 0x32)) {
				return;
			}
			fn_524f00(save, town);
			return;
		case 3:
		case 4:
			// Torre de cañones (0x30).
			if (in_list(t.u16(0x762), kTypeTower)) {
				return;
			}
			fn_51fab0(save, town, kTypeTower, false);
			return;
		case 5:
			// Mejorar una torre de cañones, si no hay ya una 0x31.
			if (in_list(t.u16(0x768), 0x31)) {
				return;
			}
			fn_524cd0(save, town);
			return;
		case 6:
			// Rehacer una puerta antigua, si hay alguna (tipo 0x37 o más).
			for (uint32_t i = t.u16(0x768); i < n; i = records[i].next) {
				if (records[i].type >= 0x37) {
					fn_524e20(save, town);
					return;
				}
			}
			return;
		}
		return;
	}
	default:  // 2
		return;
	}
}

}  // namespace p3::data::tick
