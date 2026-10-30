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

#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

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
	} else {  // Sin comprobar con el oráculo (0x524920 solo mete muelles, 0x2F).
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
		return false;  // Sin comprobar con el oráculo (las obras piden tipos <= 0x30).
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
	// 0x1F..0x21, 0x23, 0x24 y 0x30: los demás casos (de otros llamadores)
	// están sin comprobar con el oráculo.
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
		return true;  // Sin comprobar con el oráculo.
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
			return 0;  // Sin comprobar con el oráculo (las obras solo piden 0x01 y 0x03).
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
			if (skip == 0) {
				break;  // Sin comprobar con el oráculo.
			}
			skip -= 2;  // Sin comprobar con el oráculo.
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
			x = t.u8(0x806);  // Sin comprobar con el oráculo (las obras no piden 0x26).
			y = t.u8(0x807);
			break;
		case 0x2E:
			x = t.u8(0x808);  // Sin comprobar con el oráculo (las obras no piden 0x2E).
			y = t.u8(0x809);
			break;
		case kTypeTower:
			x = t.u8(0x800);
			y = t.u8(0x801);
			if (!immediate) {
				near_statue();
			} else {
				// Sin comprobar con el oráculo (las obras llaman con `immediate` = 0).
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
		// No traducido: construye ya el edificio (0x521900 para los tipos
		// < 0x30, 0x523830 hasta 0x33 y 0x522C70 para el resto). Lo hacen las
		// operaciones del juego; las obras de la ciudad (0x51D870) siempre
		// llaman con 0.
		assert(false);
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
		if (2 - d > int32_t(v)) {  // Para x, sin comprobar con el oráculo (para y, sí).
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
	const int64_t h = t.s32(0x7AC);
	auto occupy = [&](int64_t i) {
		assert(i >= 0 && i < int64_t(st.grid.size()));  // El juego no lo comprueba.
		if (i >= 0 && i < int64_t(st.grid.size())) {
			st.grid[size_t(i)] = 0x0B;
		}
	};
	occupy(h * x + y);
	occupy(h * x + y - 1);
	occupy(h * x + y - 2);
	occupy(h * ((y & 1) ? x + 1 : x - 1) + y - 1);
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
