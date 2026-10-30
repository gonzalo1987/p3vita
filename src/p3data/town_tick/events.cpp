// La cola de eventos con fecha (objeto 0x6DD73C, SaveGame::events): 0x4D85C0
// y lo que cuelga de él.
//
// Objeto 0x6DD73C:
//   [+0x0] puntero a los registros (SaveGame::events)
//   [+0x4] u32 1 mientras se atiende la cola (0x6DD740)
//   [+0x8] u16 cabeza (0x6DD744): la lista va en orden de tick
//   [+0xA] u16 primer registro libre (0x6DD746)
//   [+0xC] u16 registros reservados (0x6DD748); un índice >= él termina la lista
// Registro de 0x18 bytes: [+0x0] u32 tick, [+0x4] u16 siguiente, [+0x6] u16
// tipo (0xFFFF: libre) y, desde [+0x8], los datos de cada tipo.
//
// 0x4D85C0 atiende, en orden, los eventos con tick <= el actual: cada tipo
// tiene su manejador (tabla de saltos 0x4D8A28, tipos 0x00..0x39); según el
// tipo, el evento se quita de la lista (vuelve a la de libres) o se le cambia
// el tick y se lleva a su sitio (0x4D8DD0).
//
// Traducidos y comprobados con el oráculo (town_oracle.py --barcos --cpr
// --resto 4d85c0, 60 días en las 12 partidas): 0x00 (grano, 0x4DB330), 0x04
// (IA de cada comerciante, 0x4DFC94), 0x06 (puertos, 0x4E2144), 0x08
// (capitanes en las tabernas, 0x4E2634), 0x0D (estación, hielo y tormentas,
// 0x4E4984), 0x19 (capitanes y administradores que buscan trabajo,
// 0x4E8E24), 0x1A (0x4F6C10 de cada comerciante), 0x1B (solo sin programa:
// se quita) y 0x22 (pedidos de mercancía, 0x4DB7C0); 0x37 sin comprobar.
// Los demás tipos van por grupos en events_big.cpp (0x2C, 0x05, 0x16),
// events_towns.cpp (0x03, 0x01, 0x2A, 0x2B), events_mayor.cpp (0x02, con la
// elección del alcalde, 0x0A/0x36 subastas, 0x20, 0x21) y events_rare.cpp
// (0x0E, 0x12, 0x1C, 0x1E, 0x1F, 0x2E, 0x30, 0x32, 0x35). Los mensajes al
// jugador y la interfaz no se traducen.

#include <cstdlib>
#include <cstring>

#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

// Traducciones de otras partes que se usan aquí.
namespace construction_detail {
bool fn_4f9c80(const SaveGame& save, int town, uint8_t& x, uint8_t& y, uint32_t type, uint8_t param, bool flag);
}  // namespace construction_detail
namespace merchants_detail {
int32_t fn_51f220(const SaveGame& save, int town, uint32_t record);
}  // namespace merchants_detail
namespace ship_trade_detail {
uint16_t fn_5269a0(const SaveGame& save, int town, uint16_t owner);
}  // namespace ship_trade_detail

namespace {

constexpr uint32_t kEvents = 0x6DD73C;
constexpr uint32_t kEventBusy = kEvents + 0x4;  // u32 1 mientras 0x4D85C0 atiende la cola.
constexpr uint32_t kEventHead = kEvents + 0x8;  // u16
constexpr uint32_t kEventFree = kEvents + 0xA;  // u16
constexpr uint32_t kEventCount = kEvents + 0xC;  // u16

constexpr uint32_t kTownCount = global::kWorldTownCount;  // u32
constexpr uint32_t kMerchantCount = global::kWorldMerchantCount;  // u16
constexpr uint32_t kOfficeCount = global::kWorldOfficeCount;  // u16
constexpr uint32_t kWorldRecordCount = 0x6DE4A6;  // u16
constexpr uint32_t kTick = global::kWorldTick;  // u32
constexpr uint32_t kDayOfYear = global::kWorldDayOfYear;  // u16
constexpr uint32_t kShipCount = global::kShipCount;  // u16
// [0x6DE52A] u8 (del mundo, [+0x8A]): con él, la IA no compra ni construye y
// el comerciante sin dinero recibe 15000.
constexpr uint32_t kWorldFlag8A = 0x6DE52A;
constexpr uint32_t kAldermanBusy = 0x6DF31C;  // u32 (ver merchants.cpp)
constexpr uint32_t kOperations = 0x6DF2F0;  // u32 del objeto de las operaciones encoladas

using Event = GameMemory<0x18>;

uint32_t town_count(const SaveGame& save) {
	return save.globals.u32(kTownCount);
}

uint32_t merchant_count(const SaveGame& save) {
	return save.globals.u16(kMerchantCount);
}

uint32_t ship_count(const SaveGame& save) {
	return save.globals.u16(kShipCount);
}

TownMemory& town_of(SaveGame& save, uint32_t i) {
	return save.towns[i].memory;
}

ShipMemory& ship_of(SaveGame& save, uint32_t i) {
	return save.ships[i].memory;
}

// 0x530400: la oficina `index`, o -1 si no hay tantas.
int office_at(const SaveGame& save, uint32_t index) {
	return index < save.globals.u16(kOfficeCount) ? int(index) : -1;
}

float as_float(uint32_t bits) {
	float f;
	std::memcpy(&f, &bits, 4);
	return f;
}

// __ftol a 32 bits.
int32_t ftol32(double v) {
	return int32_t(int64_t(v));
}

// Una operación para el juego como la deja 0x535740: código 0xFFFFFFFF y el resto a 0.
struct Operation {
	uint8_t bytes[0x14] = {0xFF, 0xFF, 0xFF, 0xFF};
	void u32(size_t o, uint32_t v) { std::memcpy(bytes + o, &v, 4); }
	void u16(size_t o, uint16_t v) { std::memcpy(bytes + o, &v, 2); }
	void u8(size_t o, uint8_t v) { bytes[o] = v; }
};

void enqueue(SaveGame& save, const Operation& op) {
	enqueue_operation(save, op.bytes, sizeof(op.bytes));
}

const NavData* g_navigation = nullptr;

}  // namespace

namespace events_detail {

// 0x4D8DD0: lleva el evento de la cabeza a su sitio en la lista, detrás de
// los que tienen un tick menor o igual.
void fn_4d8dd0(SaveGame& save) {
	GlobalMemory& g = save.globals;
	const uint16_t count = g.u16(kEventCount);
	const uint16_t first = g.u16(kEventHead);
	Event& e = save.events[first];
	const uint16_t next = e.u16(4);
	if (next >= count || e.u32(0) < save.events[next].u32(0)) {
		return;
	}
	g.u16(kEventHead) = next;
	uint16_t* link = &save.events[next].u16(4);
	while (*link < count) {
		if (e.u32(0) < save.events[*link].u32(0)) {
			e.u16(4) = *link;
			*link = first;
			return;
		}
		link = &save.events[*link].u16(4);
	}
	e.u16(4) = 0xFFFF;
	*link = first;
}

// 0x4D8CF0: toma un registro libre (si no queda ninguno, reserva 0x80 más),
// lo pone en la cabeza con el tick `tick` y lo lleva a su sitio (0x4D8DD0).
// Devuelve su índice.
uint16_t fn_4d8cf0(SaveGame& save, uint32_t tick) {
	GlobalMemory& g = save.globals;
	const uint16_t free = g.u16(kEventFree);
	if (free >= g.u16(kEventCount)) {
		// Sin comprobar con el oráculo (en las partidas siempre queda alguno
		// libre). El juego copia los `free` primeros registros a un array nuevo
		// de memoria sin inicializar (aquí, a 0) y enlaza los nuevos como libres.
		const uint16_t count = uint16_t(g.u16(kEventCount) + 0x80);
		g.u16(kEventCount) = count;
		std::vector<Event> events(count);
		for (size_t i = 0; i < free && i < save.events.size() && i < events.size(); ++i) {
			events[i] = save.events[i];
		}
		save.events = std::move(events);
		for (uint16_t i = free; i < g.u16(kEventCount);) {
			Event& e = save.events[i];
			e.u16(4) = ++i;
		}
	}
	const uint16_t index = g.u16(kEventFree);
	Event& e = save.events[index];
	g.u16(kEventFree) = e.u16(4);
	e.u16(4) = g.u16(kEventHead);
	g.u16(kEventHead) = index;
	e.u32(0) = tick;
	fn_4d8dd0(save);
	return index;
}

// 0x4F6C10 (thiscall sobre el comerciante; evento 0x1A, cada 64 ticks):
// limita [+0x1F] (reputación entre los marineros) a 20 y, en cada ciudad i,
// sube el byte [+0xF0 + i] en [+0x1F] / 4 (el de antes de limitarlo) sin
// pasar de [+0x1F] · min([0x2E4] de la ciudad, 100) / 20.
void fn_4f6c10(SaveGame& save, int merchant) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const uint8_t step = uint8_t(m.u8(0x1F) >> 2);
	if (m.u8(0x1F) > 0x14) {
		m.u8(0x1F) = 0x14;
	}
	for (uint32_t i = 0; i < town_count(save); ++i) {
		const uint32_t level = save.towns[i].memory.u32(0x2E4);
		const uint32_t limit = level > 0x64 ? uint32_t(m.u8(0x1F)) * 100 : uint32_t(m.u8(0x1F)) * level;
		uint8_t& value = m.u8(0xF0 + i);
		value = uint8_t(value + step);
		if (limit / 20 < value) {
			value = uint8_t(limit / 20);
		}
	}
}

// --- Evento 0x04: la IA de cada comerciante ---

// 0x4F73D0 (thiscall sobre el comerciante): valor de su reputación (s16):
// 2 · ([+0x2FC] − [+0x11C] − [+0x124] − [+0x120]) más, en cada ciudad i (de
// la última a la primera), [+0x124 + 12·i] y, si la ciudad tiene [0x2C8] &
// 0xC0, también [+0x11C + 12·i] y [+0x120 + 12·i].
int16_t fn_4f73d0(const SaveGame& save, int merchant) {
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	float x = m.f32(0x2FC);
	x = float(double(x) - double(m.f32(0x11C)));
	x = float(double(x) - double(m.f32(0x124)));
	x = float(double(x) - double(m.f32(0x120)));
	x = float(double(x) + double(x));
	for (uint32_t i = town_count(save); i > 0;) {
		--i;
		x = float(double(x) + double(m.f32(0x124 + 12 * i)));
		if (save.towns[i].memory.u32(0x2C8) & 0xC0) {
			x = float(double(x) + double(m.f32(0x11C + 12 * i)));
			x = float(double(x) + double(m.f32(0x120 + 12 * i)));
		}
	}
	return int16_t(ftol32(x));
}

// 0x4F42B0 (thiscall sobre el comerciante; los de [+0x8] & 8): ajusta su
// factor [+0x464] a la reputación del mejor jugador humano (el jugador si
// [0x6DF2F0] & 0xE es 0): cociente entre las dos, entre 0,7 y 1,2 más 0,05 ·
// [0x6DE52C] (del mundo, [+0x8C]).
void fn_4f42b0(SaveGame& save, int merchant) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const float a = float(save.globals.u8(0x6DE52C));
	const float p = float(double(a) * double(as_float(0x3D4CCCCD)));  // 0x672DC0
	const float high = float(double(p) + double(as_float(0x3F99999A)));  // 0x672DBC
	const float low = float(double(p) + double(as_float(0x3F333333)));  // 0x672DB8
	int32_t best = 0;
	if ((save.globals.u32(kOperations) & 0xE) == 0) {
		best = fn_4f73d0(save, int(save.globals.u32(global::kPlayer)));
	} else {
		// Sin comprobar con el oráculo.
		for (uint32_t i = 0; i < merchant_count(save); ++i) {
			if (save.merchants[i].memory.u16(8) == 0) {
				const int32_t v = fn_4f73d0(save, int(i));
				if (v > best) {
					best = v;
				}
			}
		}
	}
	const int32_t own = fn_4f73d0(save, merchant);
	if (own <= (best >> 1)) {
		m.f32(0x464) = high;
		return;
	}
	m.f32(0x464) = float(best);
	if (own > 1) {
		m.f32(0x464) = float(double(float(best)) / double(float(own)));
	}
	if (m.f32(0x464) > high) {
		m.f32(0x464) = high;
	} else if (m.f32(0x464) < low) {
		m.f32(0x464) = low;
	}
}

// Valor por unidad de cada mercancía para la reserva de las oficinas: en
// las que tienen 1, redondeo a 200; en las demás, a 2000 (0x672C14).
constexpr uint8_t kFineReserve[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};

// 0x4F4F80 (thiscall sobre el comerciante; cada 4 eventos 0x04): en cada
// oficina, la reserva [+0x354 + 4·w] de cada mercancía = consumo [+0x64 +
// 4·w] por 14 días (28 con la ciudad en [0x2C8] & 0x4000018, 35 con & 0xE00),
// redondeada hacia arriba; y [+0x2D6] |= 0x10.
void fn_4f4f80(SaveGame& save, int merchant) {
	int office = office_at(save, save.merchants[size_t(merchant)].memory.u16(0xC));
	while (office >= 0) {
		OfficeMemory& o = save.offices[size_t(office)].memory;
		const uint32_t flags = save.towns[o.u8(0x2C6)].memory.u32(0x2C8);
		int32_t days;
		if (flags & 0xE00) {
			days = 0x23;
		} else {
			days = (flags & 0x4000018) ? 0x1C : 0xE;
		}
		for (uint32_t w = 0; w < 24; ++w) {
			const int32_t use = o.s32(0x64 + 4 * w);
			if (use <= 0) {
				o.s32(0x354 + 4 * w) = 0;
				continue;
			}
			const int32_t x = int32_t(uint32_t(use) * uint32_t(days));
			o.s32(0x354 + 4 * w) = kFineReserve[w] ? int32_t(uint32_t(x / 200 + 1) * 200u) : int32_t(uint32_t(x / 2000 + 1) * 2000u);
		}
		o.u8(0x2D6) |= 0x10;
		office = office_at(save, o.u16(0x2C8));
	}
}

// 0x4F5080 (thiscall sobre el comerciante, de los de [+0x8] & 6; cada 31
// eventos 0x04): si menos de la cuarta parte de sus barcos tienen [+0x136] &
// 0x10, se lo pone al primero que no lo tiene y no es una snaikka ([+0xE] & 3)
// y le sube el nivel [+0xF] hasta 2.
void fn_4f5080(SaveGame& save, int merchant) {
	const uint32_t first = save.merchants[size_t(merchant)].memory.u16(0xE);
	const uint32_t count = ship_count(save);
	int32_t ships = 0;
	int32_t flagged = 0;
	for (uint32_t s = first; s < count; s = ship_of(save, s).u16(4)) {
		++ships;
		if (ship_of(save, s).u8(0x136) & 0x10) {
			++flagged;
		}
	}
	if (ships / 4 <= flagged) {
		return;
	}
	for (uint32_t s = first; s < count;) {
		ShipMemory& ship = ship_of(save, s);
		s = ship.u16(4);
		if ((ship.u8(0xE) & 3) == 0 || (ship.u8(0x136) & 0x10)) {
			continue;
		}
		ship.u8(0x136) |= 0x10;
		if (ship.u8(0xF) < 2) {
			++ship.u8(0xF);
		}
		return;
	}
}

// 0x4F5130 (thiscall sobre el comerciante; de los demás, cada 31 eventos
// 0x04): reparte sus barcos entre sus ciudades ([+0x13A] de cada barco): 2
// por oficina y 4 en la ciudad natal; primero los grandes ([+0xE] & 7 >= 2,
// sin las ciudades con [0x2C8] & 1) y después las snaikkas y craiers.
void fn_4f5130(SaveGame& save, int merchant) {
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	int16_t slots[40] = {};
	for (int o = office_at(save, m.u16(0xC)); o >= 0; o = office_at(save, save.offices[size_t(o)].memory.u16(0x2C8))) {
		slots[save.offices[size_t(o)].memory.u8(0x2C6)] = 2;
	}
	slots[m.u8(0x19)] = 4;
	auto blocked = [&](uint32_t town) { return (save.towns[town].memory.u32(0x2C8) & 1) != 0; };
	const uint32_t towns = town_count(save);
	bool changed = false;
	for (uint32_t s = m.u16(0xE); s < ship_count(save);) {
		ShipMemory& ship = ship_of(save, s);
		s = ship.u16(4);
		const uint32_t home = ship.u8(0x13A);
		if ((ship.u8(0xE) & 7) < 2) {
			if (home >= towns) {
				ship.u8(0x13A) = 0;
			}
		} else if (home < towns && slots[home] > 0 && !blocked(home)) {
			--slots[home];
		} else {
			ship.u8(0x13A) = 0xFF;
			changed = true;
		}
	}
	if (changed) {
		for (uint32_t s = m.u16(0xE); s < ship_count(save);) {
			ShipMemory& ship = ship_of(save, s);
			s = ship.u16(4);
			if (ship.u8(0x13A) != 0xFF) {
				continue;
			}
			const uint8_t hometown = m.u8(0x19);
			ship.u8(0x13A) = blocked(hometown) ? 0 : hometown;
			for (uint32_t i = towns; i > 0;) {
				const int16_t v = slots[i - 1];
				--i;
				const uint32_t current = ship.u8(0x13A);
				// El juego mira si está cerrada la ciudad de ahora, no la nueva.
				if (v > slots[current] && !blocked(current)) {
					ship.u8(0x13A) = uint8_t(i);
				}
			}
			--slots[ship.u8(0x13A)];
		}
	}
	bool small = false;
	for (uint32_t s = m.u16(0xE); s < ship_count(save);) {
		ShipMemory& ship = ship_of(save, s);
		s = ship.u16(4);
		if ((ship.u8(0xE) & 7) >= 2) {
			continue;
		}
		const uint32_t home = ship.u8(0x13A);
		if (home < towns && slots[home] > 0) {
			--slots[home];
		} else {
			ship.u8(0x13A) = 0xFF;
			small = true;
		}
	}
	if (!small) {
		return;
	}
	for (uint32_t s = m.u16(0xE); s < ship_count(save);) {
		ShipMemory& ship = ship_of(save, s);
		s = ship.u16(4);
		if (ship.u8(0x13A) != 0xFF) {
			continue;
		}
		ship.u8(0x13A) = m.u8(0x19);
		for (uint32_t i = towns; i > 0;) {
			const int16_t v = slots[i - 1];
			--i;
			if (v > slots[ship.u8(0x13A)]) {
				ship.u8(0x13A) = uint8_t(i);
			}
		}
		--slots[ship.u8(0x13A)];
	}
}

// 0x633AE0 (thiscall sobre el asedio de la ciudad): ¿hay un asedio en
// marcha? Con [+0x8] = 0 (siempre, en las partidas), no.
bool fn_633ae0(const SaveGame& save, uint32_t town) {
	if (town >= save.sieges.size() || save.sieges[town].u32(8) == 0) {
		return false;
	}
	// Por traducir: 0x629C30 sobre el objeto de [+0x8] (no sale en las partidas).
	return false;
}

// 0x4DAF10 (thiscall sobre la cola): precio de venta del edificio `record`
// de la ciudad: el 40 % de su valor (0x51F220; el 60 % con `flag`),
// redondeado, a la mitad si la ciudad está sitiada (0x633AE0), menos su resto
// entre 100. -1 si no existe.
int32_t fn_4daf10(const SaveGame& save, uint32_t town, uint32_t record, bool flag) {
	town &= 0xFF;
	if (town >= town_count(save)) {
		return -1;  // Sin comprobar con el oráculo.
	}
	if (uint16_t(record) >= save.towns[town].memory.u16(0x776)) {
		return -1;  // Sin comprobar con el oráculo.
	}
	const int32_t value = merchants_detail::fn_51f220(save, int(town), record & 0xFFFF);
	int32_t price;
	if (flag) {
		// Sin comprobar con el oráculo.
		price = int32_t(uint32_t(value) * 6 + 5) / 10;
		return price - price % 100;
	}
	price = int32_t(uint32_t(value) * 4 + 5) / 10;
	if (fn_633ae0(save, town)) {
		price >>= 1;  // Sin comprobar con el oráculo.
	}
	return price - price % 100;
}

// 0x4DAFE0 (thiscall sobre la cola): si el edificio `record` de la ciudad
// está en venta (un evento 0x36 de la ciudad, [+0x15], lo tiene entre sus 3
// edificios [+0x8]), su tick de fin de subasta (con 1 día más si [+0x14] <=
// 2, y [+0xE] días más si es 1) y su precio (0x4DAF10); -1 y -1 si no.
void fn_4dafe0(const SaveGame& save, uint32_t town, uint32_t record, uint32_t& tick, int32_t& price) {
	price = -1;
	tick = 0xFFFFFFFF;
	town &= 0xFF;
	if (town >= town_count(save) || uint16_t(record) >= save.towns[town].memory.u16(0x776)) {
		return;  // Sin comprobar con el oráculo.
	}
	const GlobalMemory& g = save.globals;
	const uint32_t count = g.u16(kEventCount);
	for (uint32_t i = g.u16(kEventHead); i < count; i = save.events[i].u16(4)) {
		const Event& e = save.events[i];
		if (e.u16(6) != 0x36 || e.u8(0x15) != uint8_t(town)) {
			continue;
		}
		for (uint32_t k = 0; k < 3; ++k) {
			if ((e.u16(8 + 2 * k) & 0x7FFF) != uint16_t(record)) {
				continue;
			}
			tick = e.u32(0);
			if (e.u8(0x14) <= 2) {
				tick += 0x100;
				if (e.u8(0x14) == 1) {
					tick += e.u16(0xE);  // Sin comprobar con el oráculo.
				}
			}
			price = fn_4daf10(save, town, record, (e.u16(8 + 2 * k) >> 15) & 1);
			return;
		}
	}
}

// 0x4DB220 (thiscall sobre la cola): ¿hay en venta (evento 0x36) algún
// edificio del comerciante `owner`? Sin comprobar con el oráculo.
bool fn_4db220(const SaveGame& save, uint16_t owner) {
	const GlobalMemory& g = save.globals;
	for (uint32_t i = g.u16(kEventHead); i < g.u16(kEventCount); i = save.events[i].u16(4)) {
		const Event& e = save.events[i];
		if (e.u16(6) != 0x36) {
			continue;
		}
		const TownMemory& t = save.towns[e.u8(0x15)].memory;
		for (uint32_t k = 0; k < 3; ++k) {
			const uint32_t record = e.u16(8 + 2 * k) & 0x7FFF;
			if (uint16_t(record) < t.u16(0x776) && save.towns[e.u8(0x15)].records[record].owner == owner) {
				return true;
			}
		}
	}
	return false;
}

// 0x4F5A20 (thiscall sobre el comerciante, con su índice en `me`): con
// deudas, pone en venta (operación 0x28) el edificio de sus oficinas, en las
// ciudades de [+0x468], que menos renta da por unidad (las casas de [+0x2CE]
// con la renta [+0x4] y las unidades [+0xC] de su empresa en los registros
// del mundo; las empresas de [+0x2D0] con [+0x2DE + 2·k] de la oficina y
// 80/140/280 unidades) y, a igualdad, el más barato. Devuelve si lo ha hecho.
//
// La renta del mejor ([esp+0x2C]) no se inicializa en el juego: queda lo que
// hubiera en la pila. En el oráculo vale 0 (nunca se elige nada) y aquí
// también: con 0, ningún edificio mejora al primero y no se pone nada en venta.
bool fn_4f5a20(SaveGame& save, int merchant, uint32_t me) {
	static constexpr uint16_t kBusinessUnits[3] = {80, 140, 280};  // 0x672A18
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	uint32_t best_value = 0;  // [esp+0x1C]
	uint32_t best_record = 0;  // [esp+0x34]
	uint32_t best_rent = 0;  // [esp+0x2C], sin inicializar en el juego
	uint32_t best_units = 0;  // [esp+0x20]
	uint8_t best_town = 0xFF;  // [esp+0x30]
	uint32_t rent = me;  // esi: lo que quede de la casa anterior
	int office = office_at(save, m.u16(0xC));
	if (office < 0) {
		return false;
	}
	for (; office >= 0; office = office_at(save, save.offices[size_t(office)].memory.u16(0x2C8))) {
		const OfficeMemory& o = save.offices[size_t(office)].memory;
		const uint8_t town = o.u8(0x2C6);
		if (!(m.u32(0x468) & (1u << (town & 31)))) {
			continue;
		}
		const SavedTown& t = save.towns[town];
		const uint32_t records = t.memory.u16(0x776);
		auto choose = [&](uint32_t record, uint32_t r, uint32_t u, uint32_t value) {
			const uint32_t lhs = r * best_units;
			const uint32_t rhs = best_rent * u;
			if (lhs < rhs || (lhs == rhs && value < best_value)) {
				best_record = record;  // Sin comprobar con el oráculo.
				best_rent = r;
				best_units = u;
				best_value = value;
				best_town = town;
			}
		};
		auto sale_value = [&](uint32_t record) {
			const BuildingRecord& b = t.records[record];
			const uint8_t cell = t.grid[size_t(t.memory.u32(0x7AC)) * b.x + b.y];
			return uint32_t(fn_4daf10(save, town, record, (cell >> 7) != 0));
		};
		for (uint32_t record = o.u16(0x2CE); record < records; record = t.records[record].next) {
			uint32_t tick;
			int32_t price;
			fn_4dafe0(save, town, record, tick, price);
			if (price != -1) {
				continue;  // Ya está en venta.
			}
			const uint32_t value = sale_value(record);
			uint32_t units = 0;  // ebp
			for (uint32_t w = o.u16(0x2CC); w < save.globals.u16(kWorldRecordCount); w = save.world_records[w].u16(8)) {
				const GameMemory<0x14>& r = save.world_records[w];
				if (r.u8(6) == t.records[record].type) {
					rent = r.u16(4);
					units = r.u16(0xC);
					break;
				}
			}
			choose(record, rent, units, value);
		}
		for (uint32_t record = o.u16(0x2D0); record < records; record = t.records[record].next) {
			// Sin comprobar con el oráculo.
			uint32_t tick;
			int32_t price;
			fn_4dafe0(save, town, record, tick, price);
			if (price != -1) {
				continue;
			}
			const uint32_t value = sale_value(record);
			const int32_t kind = (int32_t(t.records[record].type) - 0x15) / 3;
			choose(record, o.u16(0x2DE + 2 * size_t(kind)), kBusinessUnits[kind], value);
		}
	}
	if (best_value == 0) {
		return false;
	}
	// Sin comprobar con el oráculo.
	const BuildingRecord& b = save.towns[best_town].records[best_record];
	Operation op;
	op.u32(0, 0x28);
	op.u32(4, best_town);
	op.u32(8, me & 0xFF);
	op.u32(0xC, b.x);
	op.u32(0x10, b.y);
	enqueue(save, op);
	return true;
}

// 0x4F4E50 (thiscall sobre el comerciante, con su índice `me`; con menos
// de 5000 de dinero): con [0x6DE52A], 15000 de regalo. Si no, con menos de
// −50000, pone algo en venta (0x4F5A20) o pide un préstamo de 50000
// (operación 0x29) o, si no puede, se declara en quiebra (operación 0x65); con
// más, a veces pone algo en venta y pide (10000 − dinero) / 8 · 7.
void fn_4f4e50(SaveGame& save, int merchant, uint32_t me) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	if (save.globals.u8(kWorldFlag8A)) {
		m.s32(0) = int32_t(uint32_t(m.s32(0)) + 0x3A98);  // Sin comprobar con el oráculo.
		return;
	}
	if (save.globals.u32(kAldermanBusy) != 0) {
		return;
	}
	Operation op;
	op.u32(0, 0x29);
	uint16_t word = 0;  // [esp+0x14]: el u16 de [+0xC]
	if (m.s32(0) < -50000) {
		// Sin comprobar con el oráculo.
		if (!fn_4f5a20(save, merchant, me) && !fn_4db220(save, uint16_t(me & 0xFF))) {
			op.u32(0, 0x65);
			op.u32(4, me & 0xFF);
			enqueue(save, op);
			return;
		}
		word = 0xC350;
		op.u32(4, 0xC350);
	} else {
		const int32_t r = int32_t(game_rand(save)) % 30000;
		if (r < -m.s32(0)) {
			fn_4f5a20(save, merchant, me);
		}
		word = uint16_t(0x2710 - uint16_t(m.s32(0)));
		op.u32(4, uint32_t(word >> 3) * 7);
	}
	op.u32(8, 0);
	op.u16(0xC, word);
	op.u8(0xE, 0xC);
	op.u8(0xF, 0);
	op.u8(0x10, 0xFF);
	op.u8(0x11, uint8_t(me));
	op.u8(0x12, m.u8(0x19));
	op.u8(0x13, 0xF0);
	enqueue(save, op);
}

// 0x4F9580 (thiscall sobre el comerciante): su reputación en la ciudad
// ([+0x2FC + 4·ciudad], truncada; 0x7FFF por encima de 32758; 0 si no existe).
int16_t fn_4f9580(const SaveGame& save, int merchant, uint32_t town) {
	if (town >= town_count(save)) {
		return 0;  // Sin comprobar con el oráculo.
	}
	const float rep = save.merchants[size_t(merchant)].memory.f32(0x2FC + 4 * town);
	if (as_float(0x46FFEC00) < rep) {  // 0x672F58
		return 0x7FFF;  // Sin comprobar con el oráculo.
	}
	return int16_t(ftol32(rep));
}

// 0x51B7E0 (thiscall sobre la ciudad): lo que pide la ciudad al comerciante
// por la licencia ([+0x468]): 20000 en su ciudad natal y 30000 en las demás,
// menos 3000 por punto de reputación (hasta 15000), más el valor de la
// compañía / 500 · 50 (hasta 60000).
int32_t fn_51b7e0(const SaveGame& save, int town, int merchant) {
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const uint8_t index = save.towns[size_t(town)].memory.u8(0x2C0);
	const int32_t base = m.u8(0x19) != index ? 0x7530 : 0x4E20;
	const int32_t rep = fn_4f9580(save, merchant, index);
	int32_t discount = rep >= 0 ? rep * 3000 : -rep * 3000;
	if (discount > 0x3A98) {
		discount = 0x3A98;
	}
	int32_t value = 0;
	if (m.s32(0x46C) > 0) {
		value = m.s32(0x46C) / 500 * 50;
		if (value > 0xEA60) {
			value = 0xEA60;
		}
	}
	return value - discount + base;
}

// 0x4F4A20 (thiscall sobre el comerciante, con su índice `me`; de la IA):
// pide la licencia (operación 0x37) de la ciudad de sus oficinas sin ella en
// [+0x468] en la que tiene más reputación (más de 1), si tiene cuatro veces
// lo que cuesta (0x51B7E0); ofrece la mitad.
void fn_4f4a20(SaveGame& save, int merchant, uint32_t me) {
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	if (save.globals.u8(kWorldFlag8A) || (m.u8(8) & 4)) {
		return;  // Sin comprobar con el oráculo.
	}
	uint32_t town = 100;
	float best = 1.0f;
	for (int o = office_at(save, m.u16(0xC)); o >= 0; o = office_at(save, save.offices[size_t(o)].memory.u16(0x2C8))) {
		const uint8_t t = save.offices[size_t(o)].memory.u8(0x2C6);
		if (m.u32(0x468) & (1u << (t & 31))) {
			continue;
		}
		const float f = float(fn_4f9580(save, merchant, t));
		if (f > best) {
			best = f;
			town = t;
		}
	}
	if (town >= town_count(save)) {
		return;
	}
	const int32_t price = fn_51b7e0(save, int(town), merchant);
	if ((m.s32(0) >> 2) < price) {
		return;
	}
	Operation op;
	op.u32(0, 0x37);
	op.u32(4, me & 0xFFFF);
	op.u32(8, town);
	op.u32(0xC, uint32_t(price) >> 1);
	op.u32(0x10, 0);
	enqueue(save, op);
}

// 0x4F4B40 (thiscall sobre el comerciante, con su índice `me`; de la IA):
// construye una casa (operación 0x22) en la ciudad de sus oficinas que más
// la necesita: para cada clase k (tipos 0x15 + 3·k a 0x17 + 3·k), las de
// las ciudades sin casas de esa clase en la lista [0x762], con [0x2D8 + 4·k] ·
// 100 / [0x2F8 + 2·k] (si este pasa de 10) por encima de 98. Junto a sus
// casas o empresas de esa clase (0x4F9C80) o, sin oficina, sin sitio.
void fn_4f4b40(SaveGame& save, int merchant, uint32_t me) {
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	Operation op;
	if (save.globals.u8(kWorldFlag8A)) {
		return;  // Sin comprobar con el oráculo.
	}
	int32_t best = 0x62;
	op.u8(4, uint8_t(me));
	op.u8(5, 0xFF);
	op.u8(8, 0xFF);
	for (int o = office_at(save, m.u16(0xC)); o >= 0; o = office_at(save, save.offices[size_t(o)].memory.u16(0x2C8))) {
		const uint8_t t = save.offices[size_t(o)].memory.u8(0x2C6);
		const SavedTown& town = save.towns[t];
		int32_t need[3] = {100, 100, 100};
		for (uint32_t r = town.memory.u16(0x762); r < town.memory.u16(0x776); r = town.records[r].next) {
			const uint8_t type = town.records[r].type;
			if (type >= 0x15 && type < 0x1E) {
				need[(type - 0x15) / 3] = 0;
			}
		}
		for (uint32_t k = 0; k < 3; ++k) {
			need[k] = int32_t(uint32_t(need[k]) * uint32_t(town.memory.s32(0x2D8 + 4 * k)));
			const uint16_t div = town.memory.u16(0x2F8 + 2 * k);
			if (div > 10) {
				need[k] /= int32_t(div);
			}
			if (best < need[k]) {
				best = need[k];
				op.u8(8, uint8_t((k + 7) * 3));
				op.u8(5, t);
			}
		}
	}
	const uint32_t t = op.bytes[5];
	if (t >= town_count(save)) {
		return;
	}
	const SavedTown& town = save.towns[t];
	const int office = facilities_detail::fn_5308a0(save, me, t);
	op.u8(9, town.memory.s8(0x774) > 0 ? 0xE0 : 0x20);  // 0x67792F / 0x677929
	if (office >= 0) {
		const OfficeMemory& o = save.offices[size_t(office)].memory;
		const uint8_t type = op.bytes[8];
		op.u8(6, town.memory.u8(0x800));
		op.u8(7, town.memory.u8(0x801));
		auto near = [&](uint32_t head) {
			for (uint32_t r = head; r < town.memory.u16(0x776);) {
				const BuildingRecord& b = town.records[r];
				r = b.next;
				if (b.type >= type && uint32_t(b.type) <= uint32_t(type) + 2) {
					op.u8(6, b.x);
					op.u8(7, b.y);
					break;
				}
			}
		};
		near(o.u16(0x2D0));
		if (op.bytes[6] == town.memory.u8(0x800)) {
			near(town.memory.u16(0x764));
		}
		uint8_t x = op.bytes[6];
		uint8_t y = op.bytes[7];
		const bool flag = (town.memory.u32(0x2C8) & 0x10) != 0;
		const bool ok = construction_detail::fn_4f9c80(save, int(t), x, y, type, op.bytes[9], flag);
		op.u8(6, x);
		op.u8(7, y);
		if (!ok) {
			return;
		}
	}
	op.u32(0, 0x22);
	enqueue(save, op);
}

// 0x4F4940 (thiscall sobre el comerciante, con el día del año y el evento
// 0x04): un paso de su IA. Cada 4 veces (contador [+0x10] del evento) las
// reservas de sus oficinas (0x4F4F80); cada 31 ([+0x11]) sus barcos (0x4F5080
// o 0x4F5130); con poco dinero, préstamos (0x4F4E50); y los de la IA sin
// [0x6DF31C], casas (0x4F4B40, si el contador [+0xE] no da la vuelta) y
// licencias (0x4F4A20). Devuelve los ticks hasta el siguiente: 0x100 +
// [+0x547].
uint32_t fn_4f4940(SaveGame& save, int merchant, uint32_t day, uint16_t event) {
	(void)day;
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	{
		Event& e = save.events[event];
		if (++e.u8(0x10) > 3) {
			e.u8(0x10) = 0;
			fn_4f4f80(save, merchant);
		}
	}
	{
		Event& e = save.events[event];
		if (++e.u8(0x11) > 0x1E) {
			e.u8(0x11) = 0;
			if (m.u8(8) & 6) {
				fn_4f5080(save, merchant);
			} else {
				fn_4f5130(save, merchant);
			}
		}
	}
	const uint32_t next = uint32_t(m.u8(0x547)) + 0x100;
	if (m.s32(0) < 0x1388) {
		fn_4f4e50(save, merchant, save.events[event].u8(8));
	}
	if ((m.u8(8) & 6) || save.globals.u32(kAldermanBusy) != 0) {
		return next;
	}
	Event& e = save.events[event];
	if (++e.u8(0xE) != 0) {
		fn_4f4b40(save, merchant, e.u16(8));
		save.events[event].u8(0xE) = 0;
	}
	fn_4f4a20(save, merchant, save.events[event].u16(8));
	return next;
}

// 0x4DFC94 (evento 0x04, con su índice): la IA del comerciante [+0x8]. A
// los jugadores humanos solo se les vuelve a mirar en 31 días; los de [+0x8]
// & 8 ajustan antes su reputación (0x4F42B0).
uint32_t fn_4dfc94(SaveGame& save, uint16_t event) {
	const uint32_t merchant = save.events[event].u16(8);
	if (merchant >= merchant_count(save)) {
		return 0;  // Sin comprobar con el oráculo.
	}
	const uint16_t kind = save.merchants[merchant].memory.u16(8);
	if (kind == 0) {
		save.events[event].u32(0) += 0x1F00;
	} else {
		if ((kind & 8) == 8) {
			fn_4f42b0(save, int(merchant));
		}
		const uint32_t delta = fn_4f4940(save, int(merchant), save.globals.u16(kDayOfYear), event);
		save.events[event].u32(0) += delta;
	}
	const uint32_t now = save.globals.u32(kTick);
	if (save.events[event].u32(0) <= now) {
		save.events[event].u32(0) = now + 1;
	}
	return 1;
}

// --- Evento 0x0D: lo diario (estación, hielo, tormentas) ---

// Generador de números del juego para el hielo (0x46C320 crea, 0x46C340
// siembra, 0x46C380 da el siguiente módulo n). Va en la pila: no es rand().
struct GameRandom {
	uint32_t seed = 0x499602D2;
	uint32_t next(uint32_t n) {
		uint32_t x = 0xA205B063u - seed * 0x44BF19B3u;
		x = (x & 1) ? (x >> 1) | 0x80000000u : x >> 1;
		seed = x;
		return x % n;
	}
};

// Byte de la memoria de las ciudades seguidas, por offset desde la primera
// (el juego lee así campos de ciudades concretas).
const uint8_t* town_bytes(const SaveGame& save, uint32_t offset) {
	return save.towns[offset / TownMemory::kSize].memory.bytes.data() + offset % TownMemory::kSize;
}

// Los 16 huecos de 12 bytes del objeto de los barcos en 0x6DD7C0: u16 barco
// (0xFFFF libre), s16 x [+0x4], s16 y [+0x6], u16 [+0x8].
constexpr uint32_t kShipSlots = 0x6DD7C0;

// 0x4F34A4 (día 1 de cada mes fuera del invierno): la estación de cada
// ciudad ([0x9BC], [0x9BD]): 0 en mayo, 1 en octubre y 6 (y [0x9BD] = 1) en marzo.
void fn_4f34a4(SaveGame& save) {
	if (save.globals.u8(global::kWorldDay) != 1) {
		return;
	}
	const uint32_t month = save.globals.u8(global::kWorldMonth) + 1u;
	for (uint32_t i = 0; i < town_count(save); ++i) {
		TownMemory& t = town_of(save, i);
		if (month == 5) {
			t.u8(0x9BC) = 0;
		} else if (month == 10) {
			t.u8(0x9BC) = 1;
		} else if (month == 3) {
			t.u8(0x9BC) = 6;
			t.u8(0x9BD) = 1;
		}
	}
}

// 0x4F3264: marca en [0x9C4] & 2 las ciudades con un hueco de 0x6DD7C0 en uso
// a 150 píxeles o menos de su punto de atraque (tabla de las ciudades, +0x20
// y +0x24) con [+0x8] = 0.
void fn_4f3264(SaveGame& save) {
	GlobalMemory& g = save.globals;
	for (uint32_t i = 0; i < town_count(save); ++i) {
		const uint32_t row = 0x6DDB90 + 0x34 * g.u8(0x6DE4B8 + i);
		const int32_t x = g.s32(row + 0x20);
		const int32_t y = g.s32(row + 0x24);
		TownMemory& t = town_of(save, i);
		bool near = false;
		for (uint32_t k = 0; k < 16; ++k) {
			const uint32_t slot = kShipSlots + 12 * k;
			if (g.u16(slot) == 0xFFFF) {
				continue;
			}
			const int32_t dx = int32_t(uint32_t(x) - uint32_t(int32_t(g.s16(slot + 4))));
			const int32_t dy = int32_t(uint32_t(y) - uint32_t(int32_t(g.s16(slot + 6))));
			if (std::abs(dx) > 0x96 || std::abs(dy) > 0x96 || g.u16(slot + 8) != 0) {
				continue;
			}
			near = true;
			break;
		}
		if (near) {
			t.u8(0x9C4) |= 2;
		} else {
			t.u8(0x9C4) &= 0xFD;
		}
	}
}

// 0x4F3354 (cada 4 días): fuera del invierno, una ciudad al azar distinta
// de la anterior ([0x6DE52F], del mundo [+0x8F]) con [0x9C4] & 1; en
// invierno, ninguna.
void fn_4f3354(SaveGame& save) {
	GlobalMemory& g = save.globals;
	const uint16_t day = g.u16(kDayOfYear);
	const uint32_t towns = town_count(save);
	if (day < 0x3A || day >= 0x14E) {
		const uint32_t old = g.u8(0x6DE52F);
		if (old < towns) {
			town_of(save, old).u8(0x9C4) &= 0xFE;
			g.u8(0x6DE52F) = 0xFF;
		}
		return;
	}
	GameRandom r;
	r.seed = town_of(save, 0).u32(0x124);
	const uint8_t old = g.u8(0x6DE52F);
	if (old != 0xFF && old < towns) {
		town_of(save, old).u8(0x9C4) &= 0xFE;
	}
	uint8_t chosen;
	do {
		chosen = uint8_t(r.next(towns));
	} while (chosen == old);
	g.u8(0x6DE52F) = chosen;
	town_of(save, chosen).u8(0x9C4) |= 1;
}

// 0x4E45C4 (en invierno): el hielo de cada ciudad. [0x9B8] hielo (crece con
// lo al norte y al este que está la ciudad; en primavera se funde), [0x9BD]
// nivel (hielo / 512 + 2; con más de 5, bit 7: puerto helado), [0x9BC]
// nivel con el que se cierra, [0x9C0] ticks que quedan cerrado ([0x9C4] & 4).
// Con el puerto helado, a veces la ciudad se queda sin entrar ni salir
// ([0x2C8] |= 0x4000000) hasta un evento 0x35. No se traduce el mensaje.
void fn_4e45c4(SaveGame& save) {
	GlobalMemory& g = save.globals;
	GameRandom r;
	r.seed = uint32_t(int32_t(*reinterpret_cast<const int16_t*>(town_bytes(save, 0x5CBC)))) +
		*reinterpret_cast<const uint32_t*>(town_bytes(save, 0x48E4)) +
		*reinterpret_cast<const uint32_t*>(town_bytes(save, 0x20C8)) +
		*reinterpret_cast<const uint32_t*>(town_bytes(save, 0x14));
	const uint16_t day = g.u16(kDayOfYear);
	const bool winter = day >= 0x14D || day < 0x3A;
	for (uint32_t i = 0; i < town_count(save); ++i) {
		TownMemory& t = town_of(save, i);
		bool frozen = false;
		t.u8(0x9BC) = t.u8(0x9BD);
		if (t.s8(0x9BD) < 0) {
			t.u8(0x9BC) = 5;
		}
		const uint32_t row = 0x6DDB90 + 0x34 * g.u8(0x6DE4B8 + i);
		const uint32_t north = (uint32_t(g.s32(row + 0x20)) - 2 * uint32_t(g.s32(row + 0x24)) + 0xE10) / 50;
		bool grow = false;
		if (day > 0x39) {
			if (day < 0x14D) {
				// Sin comprobar con el oráculo.
				t.u32(0x9B8) = 0;
				t.u32(0x9C0) = 0;
				t.u8(0x9C4) &= 0xFB;
			} else {
				grow = true;
			}
		} else if (day < 0x20) {
			grow = true;
		} else if (t.u32(0x9B8) != 0) {
			const uint32_t v = r.next(north);
			const int32_t d = ftol32(double(north) * -2.5);  // 0x672848
			const uint32_t melt = (v - uint32_t(d)) / 3;
			t.s32(0x9B8) = int32_t(t.u32(0x9B8) - melt);
			if (t.s32(0x9B8) < 0) {
				t.u32(0x9B8) = 0;  // Sin comprobar con el oráculo.
			}
		}
		if (grow) {
			const uint32_t v = r.next(0x3C);
			t.u32(0x9B8) += (v + north) / 3;
		}
		if (t.u32(0x9C0) != 0) {
			t.u32(0x9C0) -= 0x100;
			if (t.s32(0x9C0) <= 0) {
				t.u32(0x9C0) = 0;
				t.u8(0x9C4) &= 0xFB;
			} else {
				t.u8(0x9C4) |= 4;
			}
		}
		const uint32_t ice = t.u32(0x9B8);
		if (ice == 0 && !winter) {
			t.u8(0x9BD) = 1;
			continue;
		}
		t.u8(0x9C4) &= 0xFE;
		uint8_t level = uint8_t((ice >> 9) + 2);
		t.u8(0x9BD) = level;
		if (level > 5) {
			level |= 0x80;
			frozen = true;
			t.u8(0x9BD) = level;
		}
		if ((t.u8(0x9BD) & 0x7F) > t.u8(0x9BC)) {
			t.u32(0x9C0) = r.next(100) + north * 4 + 0x100;
			const int32_t speed = save.settings.s32(4);
			if (speed > 2 && speed < 5) {
				// Sin comprobar con el oráculo: según la velocidad del juego (0x66DE7C).
				static constexpr uint32_t kSpeeds[8] = {0x78, 0x5A, 0x3C, 0x1E, 0x14, 0x384, 0, 0x1000000};
				uint32_t days = t.u32(0x9C0) >> 8;
				if (uint8_t(days) != 0) {
					++days;
				}
				t.u32(0x9C0) = (0x3Cu * days / kSpeeds[speed]) << 8;  // [0x66DE84] = 0x3C
			}
			t.u8(0x9C4) |= 4;
		}
		if (!frozen || (t.u32(0x2C8) & 0x4000000)) {
			continue;
		}
		if (r.next(0x6400) >= 0x180) {
			continue;
		}
		const uint32_t when = g.u32(kTick) + ((uint32_t(t.u8(0x9BD) & 0x7F) + 1) << 8);
		t.u32(0x2C8) |= 0x4000000;
		g.u32(kEventBusy) = 0;
		const uint16_t e = fn_4d8cf0(save, when);
		save.events[e].u16(6) = 0x35;
		save.events[e].u32(8) = i;
		g.u32(kEventBusy) = 1;
	}
}

// 0x4474D0 (sobre el objeto de navegación): ¿hay agua en el punto del mapa?
bool fn_4474d0(int16_t x, int16_t y) {
	const NavData* nav = event_navigation();
	const int32_t scale = 0xDD0;  // [0x66E0D0]
	const int32_t cx = (int32_t(x) * 1000 + scale / 2) / scale;
	const int32_t cy = (int32_t(y) * 1000 + scale / 2) / scale;
	if (!nav || cy >= nav->height() || cx >= nav->width() || y < 0 || x < 0 || cy < 0 || cx < 0) {
		return false;
	}
	return nav->is_water(cx, cy);
}

// ¿Hay un barco de los huecos de 0x6DD7C0 a menos de √5000 del punto?
bool ship_slot_near(const SaveGame& save, int16_t x, int16_t y) {
	const GlobalMemory& g = save.globals;
	for (uint32_t k = 0; k < 16; ++k) {
		const uint16_t s = g.u16(kShipSlots + 12 * k);
		if (s >= ship_count(save)) {
			continue;
		}
		const ShipMemory& ship = save.ships[s].memory;
		const int32_t dx = int32_t(x) - ship.s16(0x1E);
		const int32_t dy = int32_t(y) - ship.s16(0x22);
		if (int32_t(uint32_t(dy) * uint32_t(dy) + uint32_t(dx) * uint32_t(dx)) < 0x1388) {
			return true;
		}
	}
	return false;
}

// rand() · k / 32768 truncando, como lo hace el juego (cdq; and 0x7FFF; sar 15).
int32_t rand_scaled(SaveGame& save, int32_t k) {
	const int32_t v = int32_t(uint32_t(game_rand(save)) * uint32_t(k));
	return v / 32768;
}

// Duración de una tormenta: 0xA00 + rand() · 1280 / 32767 (división por la
// multiplicación mágica 0x80010003).
uint32_t storm_length(SaveGame& save) {
	const int32_t v = int32_t(game_rand(save) * 1280);
	int32_t q = int32_t((int64_t(v) * int64_t(int32_t(0x80010003u))) >> 32) + v;
	q >>= 14;
	q += int32_t(uint32_t(q) >> 31);
	return uint32_t(q + 0xA00);
}

// Zonas de tormenta (0x6726E0 y 0x672758): x, y, ancho, alto, en celdas.
constexpr int16_t kStormAreasA[15][4] = {{74, 312, 117, 38}, {72, 352, 39, 47}, {184, 136, 87, 47}, {240, 184, 46, 150},
	{288, 280, 158, 40}, {56, 80, 112, 47}, {56, 128, 128, 182}, {320, 320, 38, 22}, {344, 248, 94, 30},
	{392, 136, 54, 110}, {425, 81, 45, 55}, {472, 81, 38, 14}, {504, 57, 38, 22}, {0, 0, 167, 80}, {392, 0, 30, 54}};
constexpr int16_t kStormAreasB[15][4] = {{0, 0, 167, 80}, {392, 0, 30, 54}, {56, 80, 112, 47}, {56, 128, 128, 182},
	{320, 320, 38, 22}, {344, 248, 94, 30}, {392, 136, 54, 110}, {425, 81, 45, 55}, {472, 81, 38, 14},
	{504, 57, 38, 22}, {74, 312, 117, 38}, {72, 352, 39, 47}, {184, 136, 87, 47}, {240, 184, 46, 150},
	{288, 280, 158, 40}};

// Un punto de la tormenta en (x, y) del mapa, si hay agua y ningún barco de
// los huecos cerca: operación 0x78 (tormenta): [+0x4] clase, [+0x8] duración,
// [+0xC] x, [+0x10] y.
void enqueue_storm(SaveGame& save, uint32_t kind, uint32_t length, int16_t x, int16_t y) {
	Operation op;
	op.u32(0, 0x78);
	op.u32(4, kind);
	op.u32(8, length);
	op.u32(0xC, uint32_t(int32_t(x)));
	op.u32(0x10, uint32_t(int32_t(y)));
	enqueue(save, op);
}

// 0x4E3F44 (cada 3 días fuera del invierno): a veces, una tormenta en una
// zona al azar (0x6726E0 o 0x672758 según un bit del día · 1/9), si hay
// menos de 0x30 en [0x6DF346].
void fn_4e3f44(SaveGame& save) {
	const int32_t pick = int32_t(uint32_t(game_rand(save)) * 14) / 32768;
	const float ninth = float(double(float(save.globals.u16(kDayOfYear))) * double(as_float(0x3DE38E39)));  // 0x672844
	uint32_t bits;
	std::memcpy(&bits, &ninth, 4);
	uint32_t kind;
	const int16_t (*areas)[4];
	float chance;
	if (bits & 0xF0) {
		kind = 0;
		areas = kStormAreasA;
		if (pick < 5) {
			chance = as_float(0x3EAA7EFA);
		} else if (pick < 0xD) {
			chance = 0.5f;
		} else if (pick < 0xF) {
			chance = 1.0f;
		} else {
			return;
		}
	} else {
		kind = 1;
		areas = kStormAreasB;
		if (pick < 2) {
			chance = as_float(0x3EAA7EFA);
		} else if (pick < 0xA) {
			chance = 0.5f;
		} else if (pick < 0xF) {
			chance = 1.0f;
		} else {
			return;
		}
	}
	if (uint32_t(0x34 - save.globals.u16(0x6DF346)) < 5) {
		return;
	}
	const float r = float(game_rand(save));
	if (double(chance) * double(as_float(0x46FFFE00)) < double(r)) {  // 0x66DB64
		return;
	}
	const int16_t* area = areas[pick];
	const uint32_t length = storm_length(save);
	for (int32_t tries = 1; tries <= 5; ++tries) {
		int16_t x = area[0];
		int16_t y = area[1];
		x = int16_t(x + rand_scaled(save, area[2]));
		y = int16_t(y + rand_scaled(save, area[3]));
		const int32_t scale = 0xDD0;  // [0x66E0D0]
		const int16_t px = int16_t((int32_t(x) * scale + 0x1F4) / 1000 + 0x44);
		const int16_t py = int16_t((int32_t(y) * scale + 0x1F4) / 1000 + 0x44);
		if (!fn_4474d0(int16_t(px - 0x44), int16_t(py - 0x44))) {
			continue;
		}
		if (ship_slot_near(save, px, py)) {
			continue;
		}
		enqueue_storm(save, kind, length, px, py);
		return;
	}
}

// Las zonas de tormenta del invierno (0x6727D0, s16): probabilidad, días
// en que no hay, x, y, ancho, alto. El juego avanza de 2 en 2 bytes y no
// de registro en registro.
constexpr int16_t kWinterStorms[16] = {30, 90, 330, 0, 0, 152, 36, 36, 90, 364, 69, 37, 78, 124, 11, 59};

// 0x4E43E4 (cada 3 días en invierno): tormentas en las zonas de invierno.
void fn_4e43e4(SaveGame& save) {
	for (uint32_t k = 2; k < 8; ++k) {
		const int16_t* p = kWinterStorms + k;  // ebp = 0x6727D0 + 2·k
		const int32_t chance = int32_t(p[-2]) * 0x7FFF;
		if (chance < int32_t(game_rand(save) << 10)) {
			continue;
		}
		const int32_t day = save.globals.u16(kDayOfYear);
		if (day > p[-1] && day < p[0]) {
			return;
		}
		for (int32_t tries = 1;; ++tries) {
			int16_t x = int16_t(p[1] * 3);
			int16_t y = int16_t(p[2] * 3);
			if (tries >= 0x19) {
				return;
			}
			x = int16_t(x + int32_t(uint32_t(game_rand(save)) * uint32_t(int32_t(p[3])) * 3) / 32768);
			y = int16_t(y + int32_t(uint32_t(game_rand(save)) * uint32_t(int32_t(p[4])) * 3) / 32768);
			if (!fn_4474d0(x, y)) {
				continue;
			}
			const int16_t px = int16_t(x + 0x44);
			const int16_t py = int16_t(y + 0x44);
			if (ship_slot_near(save, px, py)) {
				continue;  // Sin comprobar con el oráculo.
			}
			const uint32_t length = storm_length(save);
			enqueue_storm(save, 2, length, px, py);
			break;
		}
	}
}

// 0x4E4984 (evento 0x0D, una vez al día): la estación (0x4F34A4) o el
// hielo (0x4E45C4); cada 4 días la ciudad de 0x4F3354; sin [0x6DF31C],
// tormentas cada 3 días; las ciudades cerca de los huecos de 0x6DD7C0
// (0x4F3264). El evento vuelve en un día y cuenta los días en [+0x8].
void fn_4e4984(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	const uint16_t day = g.u16(kDayOfYear);
	if (day > 0x3A && day < 0x14D) {
		fn_4f34a4(save);
	} else {
		fn_4e45c4(save);
	}
	if ((g.u16(kDayOfYear) & 3) == 0) {
		fn_4f3354(save);
	}
	const uint32_t d = g.u16(kDayOfYear);
	if (g.u32(kAldermanBusy) == 0) {
		if (d >= 0x5A && d < 0x14E) {
			if (d % 3 == 0) {
				fn_4e3f44(save);
			}
		} else if ((d + 2) % 3 == 0) {
			fn_4e43e4(save);
		}
	}
	fn_4f3264(save);
	Event& e = save.events[event];
	e.u32(0) += 0x100;
	e.u32(8) += 1;
}
// --- Evento 0x19: capitanes y administradores que buscan trabajo ---

// 0x4E8CA4 (thiscall sobre la cola): una persona al azar en los 0x10 bytes
// de `out` (desde [+0x4] de una operación 0x7B): [+0x1] nombre (de hombre,
// u16 [0x6DDB70] nombres) y [+0x2] apellido ([0x6DDB76], con los de los
// piratas); u16 [+0x4] sueldo: ([0x2DC] / 2 + [0x2D8]) de la ciudad, hasta
// 2000, más un tanto según la clase [+0xC] >> 8 (0..5); [+0x3] edad y [+0x0]
// habilidad (sueldo / 5000 + rand() % 5 − 2, entre 0 y 7).
void fn_4e8ca4(SaveGame& save, uint8_t* out, const TownMemory& town) {
	const GlobalMemory& g = save.globals;
	out[1] = uint8_t(int32_t(game_rand(save)) % int32_t(g.u32(0x6DDB70) & 0xFFFF));
	const uint16_t families = g.u16(0x6DDB76);
	out[2] = uint8_t(int32_t(game_rand(save)) % int32_t(families));
	uint16_t pay = uint16_t((town.s32(0x2DC) >> 1) + town.u16(0x2D8));
	if (pay > 0x7D0) {
		pay = 0x7D0;
	}
	uint32_t kind;
	std::memcpy(&kind, out + 0xC, 4);
	kind >>= 8;
	switch (kind) {
	case 0:
		pay = uint16_t(pay + (game_rand(save) & 0x3FF) + 0x7D0);
		out[3] = uint8_t((game_rand(save) & 3) + 6);
		break;
	case 1:
		pay = uint16_t(pay << 1);
		pay = uint16_t(pay + (game_rand(save) & 0x7FF) + 0xFA0);
		out[3] = uint8_t((game_rand(save) & 7) + 0xC);
		break;
	case 2:
		pay = uint16_t(pay << 2);
		pay = uint16_t(pay + (game_rand(save) & 0xFFF) + 0x1F40);
		out[3] = uint8_t((game_rand(save) & 0xF) + 0x18);
		break;
	case 3:
	case 4:
	case 5:
		pay = uint16_t(pay + (game_rand(save) & 0x1000));
		pay = uint16_t(pay + (game_rand(save) & 0x3FFF) + 0x7D0);
		out[3] = uint8_t((game_rand(save) & 0x1F) + 6);
		break;
	default:
		pay = 0;  // Sin comprobar con el oráculo.
		out[3] = 0;
		break;
	}
	std::memcpy(out + 4, &pay, 2);
	const int32_t skill = int32_t(pay) / 5000 + int32_t(game_rand(save)) % 5 - 2;
	out[0] = skill < 1 ? 0 : skill >= 8 ? 7 : uint8_t(skill);
}

// 0x4E8E24 (evento 0x19, cada 0x80 ticks más uno por persona): cuenta las
// personas que buscan trabajo en las ciudades (u16 [0x9CA + 6·k] y [0x9DC +
// 6·k], k = 0..2) y, si hay hueco (como mucho 0x34 − [0x6DF346]), pone más
// (operación 0x7B: [+0x10] = clase << 8 | ciudad) en las ciudades siguientes
// a [+0x8] del evento.
void fn_4e8e24(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	int32_t room = 0x34 - int32_t(g.u16(0x6DF346));
	Operation op;
	Event& e = save.events[event];
	e.u32(0) += 0x80;
	const uint32_t base = e.u32(0);
	int32_t people = 0;
	for (uint32_t i = 0; i < town_count(save); ++i) {
		const TownMemory& t = town_of(save, i);
		for (uint32_t k = 0; k < 3; ++k) {
			if (t.u16(0x9CA + 6 * k) > 0) {
				++people;
			}
			if (t.u16(0x9DC + 6 * k) > 0) {
				++people;
			}
		}
	}
	if (people > 0x5A) {
		e.u32(0) = base + uint32_t(people) + 0x80;
		if (room > 0xC) {
			room = 0xC;
		}
	} else if (people > 0x3C) {
		e.u32(0) = base + uint32_t(people) + 0x40;  // Sin comprobar con el oráculo.
		if (room > 8) {
			room = 8;
		}
	} else {
		e.u32(0) = base + uint32_t(people) + 0x10;
		if (room > 0x78 - people) {
			room = 0x78 - people;  // Sin comprobar con el oráculo.
		}
	}
	if (merchant_count(save) <= town_count(save)) {
		return;  // Sin comprobar con el oráculo.
	}
	if (g.u32(kAldermanBusy) != 0) {
		e.u8(8) = 0;  // Sin comprobar con el oráculo.
		return;
	}
	if (g.u8(kWorldFlag8A)) {
		return;  // Sin comprobar con el oráculo.
	}
	op.u32(0, 0x7B);
	auto offer = [&](uint32_t kind_town) {
		--room;
		op.u32(0x10, kind_town);
		fn_4e8ca4(save, op.bytes + 4, town_of(save, save.events[event].u8(8)));
		enqueue(save, op);
	};
	while (room > 0) {
		Event& ev = save.events[event];
		if (++ev.u8(8) >= town_count(save)) {
			ev.u8(8) = 0;
		}
		const uint32_t town = ev.u8(8);
		const TownMemory& t = town_of(save, town);
		if (people <= 0x3C) {
			for (uint32_t k = 0; k < 3; ++k) {
				if (t.u16(0x9CA + 6 * k) == 0) {
					offer((k << 8) + town);
					break;
				}
			}
			for (uint32_t k = 0; k < 3; ++k) {
				if (t.u16(0x9DC + 6 * k) == 0) {
					offer(((k + 3) << 8) + town);
					break;
				}
			}
		} else {
			const int32_t r = int32_t(game_rand(save)) % 0x600;
			offer((uint32_t(r) & 0x700) + town);
		}
	}
}

// --- Evento 0x06: lo semanal de los puertos ---

// Lo que se necesita para cada nivel de [0x824 + k] de la ciudad (0x673818,
// en unidades de 2800).
constexpr uint16_t kPortLevels[16] = {0, 100, 300, 1050, 0, 100, 600, 900, 0, 200, 400, 800, 300, 500, 600, 1200};

// 0x4E2144 (evento 0x06, cada semana): en cada ciudad, [0x818] = media de
// la capacidad de los barcos de su lista [0x81C] / 2000 más [0x814] / 19600
// (que se suma a [0x810] y vuelve a 0); los niveles [0x824 + k] suben uno
// si [0x810] pasa del siguiente de 0x673818. No se traduce el mensaje.
void fn_4e2144(SaveGame& save) {
	for (uint32_t i = 0; i < town_count(save); ++i) {
		TownMemory& t = town_of(save, i);
		int32_t capacity = 0;
		for (uint32_t s = t.u16(0x81C); s < ship_count(save); s = ship_of(save, s).u16(6)) {
			capacity = int32_t(uint32_t(capacity) + uint32_t(ship_of(save, s).s32(0x10)));
		}
		const uint32_t week = t.u32(0x814);
		const int32_t value = int32_t(week / 19600 + uint32_t(capacity / 2000));
		t.u32(0x814) = 0;
		t.u32(0x810) += week;
		const float f = float(value);
		t.f32(0x818) = float(double(f) / double(as_float(0x41D00000)) + double(t.f32(0x818)) * double(as_float(0x3F762762)));
		for (uint32_t k = 0; k < 4; ++k) {
			const int32_t next = int32_t(t.s8(0x824 + k)) + 1;
			if (next >= 4) {
				continue;
			}
			if (t.u32(0x810) > uint32_t(kPortLevels[next + 4 * int32_t(k)]) * 2800) {
				t.u8(0x824 + k) = uint8_t(next);
			}
		}
	}
}

// --- Evento 0x08: capitanes en las tabernas ---

// 0x4FE150 (sobre el capitán): ¿[+0x8] pasa de 0x20?
bool fn_4fe150(const CaptainMemory& c) {
	return c.u8(8) > 0x20;
}

// 0x4FE190 (sobre el capitán): [+0xC] = ([+0x9] + [+0xA] + [+0xB]) / 50 +
// [+0x8] % 11 + 10.
void fn_4fe190(CaptainMemory& c) {
	const int32_t sum = int32_t(c.u8(0xB)) + c.u8(0xA) + c.u8(9);
	c.u16(0xC) = uint16_t(sum / 50 + int32_t(c.u8(8)) % 11 + 10);
}

// El byte bajo de los bits del float `v` (el juego guarda así, sin
// convertirlo a entero, las habilidades del capitán).
uint8_t float_low_byte(float v) {
	uint32_t bits;
	std::memcpy(&bits, &v, 4);
	return uint8_t(bits);
}

// 0x4FDF50 (sobre el capitán, con su índice): un capitán nuevo, con nombre
// y edad según la fecha y el índice. Con `young` (el juego lo pasa si no se
// pide otra cosa), [+0xC] = 0 y [+0x8] % 11.
void fn_4fdf50(SaveGame& save, CaptainMemory& c, uint32_t index, bool young) {
	const GlobalMemory& g = save.globals;
	const uint32_t day = g.u16(kDayOfYear);
	const uint32_t tick8 = g.u32(kTick) & 0xFF;
	const uint32_t mday = g.u8(global::kWorldDay);
	c.u8(2) = uint8_t((tick8 + day + index) % (g.u32(0x6DDB70) & 0xFFFF));
	c.u8(3) = uint8_t((mday + tick8 + (index >> 1) + day) % (g.u32(0x6DDB74) & 0xFFFF));
	const uint32_t years = ((save.towns[day & 0xF].memory.u32(0x2E0) + mday + tick8 + day) & 0x1F) + 0x30;
	const uint32_t age = years * 365 * 128 + ((mday + day + index) & 0x1F) * 7 * 256;
	const uint32_t tick = g.u32(kTick);
	c.u32(4) = age < tick ? tick - age : 0;
	const float base = float(double(uint32_t(tick8 + index)));
	c.u8(9) = float_low_byte(float(double(base) * double(as_float(0x3E0B0C89))));  // 0x6734AC
	uint32_t left = 600 - c.u8(9);
	c.u8(0xA) = float_low_byte(float(double(float(double(uint32_t(index - tick8)))) * double(as_float(0x451A41EC))));
	if (c.u8(0xA) > left) {
		c.u8(0xA) = uint8_t(left);  // Sin comprobar con el oráculo.
	}
	left -= c.u8(0xA);
	c.u8(0xB) = float_low_byte(float(double(float(double(uint32_t(day - index)))) * double(as_float(0x4640E4CD))));
	if (c.u8(0xB) > left) {
		c.u8(0xB) = uint8_t(left);
	}
	c.u8(0xF) = 0xFF;
	c.u8(0xE) = 0;
	c.u8(8) = float_low_byte(float(double(base) * double(as_float(0x3DFCD35B))));  // 0x6734A0
	if (young) {
		c.u16(0xC) = 0;
		c.u8(8) = uint8_t(int32_t(c.u8(8)) % 11);
	} else {
		fn_4fe190(c);  // Sin comprobar con el oráculo.
	}
}

// 0x5097C0 (sobre el objeto de los barcos): un capitán libre (si no queda,
// 0x40 más; los nuevos a 0xFF y enlazados por [+0x0] en la lista de libres
// [0x6DD880]), nuevo (0x4FDF50). Devuelve su índice.
uint16_t fn_5097c0(SaveGame& save, bool keep) {
	GlobalMemory& g = save.globals;
	constexpr uint32_t kFree = global::kShips + 0xE0;
	if (g.u16(kFree) >= g.u16(global::kCaptainCount)) {
		const uint16_t count = uint16_t(g.u16(global::kCaptainCount) + 0x40);
		g.u16(global::kCaptainCount) = count;
		std::vector<CaptainMemory> captains(count);
		for (CaptainMemory& c : captains) {
			c.bytes.fill(0xFF);
		}
		for (size_t i = 0; i < g.u16(kFree) && i < save.captains.size(); ++i) {
			captains[i] = save.captains[i];
		}
		save.captains = std::move(captains);
		for (uint16_t i = g.u16(kFree); i < count; ++i) {
			save.captains[i].u16(0) = uint16_t(i + 1);
		}
	}
	const uint16_t index = g.u16(kFree);
	CaptainMemory& c = save.captains[index];
	g.u16(kFree) = c.u16(0);
	c.u16(0) = 0xFFFF;
	fn_4fdf50(save, c, index, !keep);
	return index;
}

// 0x526A50 (sobre la ciudad): un capitán nuevo (0x5097C0) al principio de
// la lista de la taberna [0x82E]; sin `keep`, con [+0xC] calculado (0x4FE190).
void fn_526a50(SaveGame& save, uint32_t town, bool keep) {
	const uint16_t index = fn_5097c0(save, keep);
	CaptainMemory& c = save.captains[index];
	TownMemory& t = town_of(save, town);
	c.u16(0) = t.u16(0x82E);
	if (!keep) {
		fn_4fe190(c);
	}
	t.u16(0x82E) = index;
}

// 0x5261D0 (sobre la ciudad): el primer capitán de la taberna con [+0x8] >
// 0x20 que sea del jugador ([+0xF]) o, si no hay, de nadie; 0xFFFF si no.
uint32_t fn_5261d0(const SaveGame& save, uint32_t town) {
	const GlobalMemory& g = save.globals;
	const uint32_t count = g.u16(global::kCaptainCount);
	const uint16_t head = save.towns[town].memory.u16(0x82E);
	for (uint32_t i = head; i < count; i = save.captains[i].u16(0)) {
		const CaptainMemory& c = save.captains[i];
		if (fn_4fe150(c) && c.u8(0xF) == g.u32(global::kPlayer)) {
			return i;
		}
	}
	for (uint32_t i = head; i < count; i = save.captains[i].u16(0)) {
		const CaptainMemory& c = save.captains[i];
		if (fn_4fe150(c) && c.u8(0xF) == 0xFF) {
			return i;
		}
	}
	return 0xFFFF;
}

// 0x509930 (sobre el objeto de los barcos): barcos no hundidos de los
// jugadores humanos (ships[0]) y de la IA (ships[1]), y los que tienen capitán
// (captained[]).
void fn_509930(const SaveGame& save, uint32_t captained[2], uint32_t ships[2]) {
	captained[0] = captained[1] = ships[0] = ships[1] = 0;
	for (uint32_t m = merchant_count(save); m > 0;) {
		--m;
		const uint32_t ai = save.merchants[m].memory.u16(8) != 0 ? 1 : 0;
		for (uint32_t s = save.merchants[m].memory.u16(0xE); s < ship_count(save); s = save.ships[s].memory.u16(4)) {
			const ShipMemory& ship = save.ships[s].memory;
			if (ship.u16(0x134) == 0x11) {
				continue;
			}
			++ships[ai];
			if (ship.u16(0x42) < save.globals.u16(global::kCaptainCount)) {
				++captained[ai];
			}
		}
	}
}

// 0x4E2634 (evento 0x08): pone capitanes nuevos en las tabernas si faltan:
// en una ciudad sin capitán libre (0x5269A0), salvo que haya ya 8 o los
// suficientes para los barcos sin capitán (cada 2 días; si no, en 7 o 4).
void fn_4e2634(SaveGame& save, uint16_t event) {
	uint8_t flags[40];
	int32_t free_towns = 0;
	int32_t good = 0;
	const uint32_t towns = town_count(save);
	const uint32_t captains = save.globals.u16(global::kCaptainCount);
	for (uint32_t i = 0; i < towns; ++i) {
		if (ship_trade_detail::fn_5269a0(save, int(i), uint16_t(merchant_count(save))) < captains) {
			flags[i] = 1;
			++free_towns;
		} else {
			flags[i] = 0;
		}
		if (captains >= fn_5261d0(save, i)) {
			flags[i] |= 2;
			++good;
		}
	}
	for (uint32_t i = towns; i < 40; ++i) {
		flags[i] = 0xFF;
	}
	Event& e = save.events[event];
	if (good < 3) {
		// Sin comprobar con el oráculo.
		e.u32(0x14) = (e.u32(0x14) + 3) % 0x17 + 1;
		uint32_t t = 0;
		for (uint32_t k = 0; k < 0x28; ++k) {
			e.u32(0x10) = (e.u32(0x10) + e.u32(0x14)) % 0x25;
			t = e.u32(0x10);
			if (!(flags[t] & 2)) {
				break;
			}
		}
		fn_526a50(save, t, true);
	}
	if (free_towns >= 8) {
		save.events[event].u32(0) += 0x700;
		return;
	}
	uint32_t captained[2];
	uint32_t ships[2];
	fn_509930(save, captained, ships);
	const int32_t needed = int32_t(ships[1]) / 5 + (int32_t(ships[0] + 1) >> 1) - int32_t(captained[1]) -
		int32_t(captained[0]);
	if (free_towns > needed && free_towns > 1) {
		save.events[event].u32(0) += 0x400;
		return;
	}
	Event& ev = save.events[event];
	uint32_t t = ev.u32(8) % towns;
	ev.u32(0xC) = (ev.u32(0xC) + 3) % 0x1D;
	if (ev.u32(0xC) == 0) {
		ev.u32(0xC) = 3;  // Sin comprobar con el oráculo.
	} else if (ev.u32(0xC) == 0x14) {
		ev.u32(0xC) = 2;
	}
	do {
		t = (ev.u32(0xC) + t) % 0x25;
	} while (flags[t] & 1);
	ev.u32(8) = t;
	fn_526a50(save, t, false);
	save.events[event].u32(0) += 0x200;
}

// --- Evento 0x00: el grano de la ciudad de [+0x10] ---

// 0x4DB330 (evento 0x00): suma a la ciudad [+0x10] del evento [+0xC] de la
// mercancía 6 (grano) y un aviso ([0x996]); después [+0xC] = lo que le falta al
// mundo (40 veces el consumo de los habitantes, [0x328], menos lo que hay,
// [0x1C], de todas las ciudades), la siguiente fecha [+0x8] en 10 a 14 días y,
// como ciudad, una al azar de las cuatro más cercanas al punto (0, 1500)
// (tabla de las ciudades, +0x20 y +0x24). Devuelve 0 si el evento se quita.
uint32_t fn_4db330(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	{
		Event& e = save.events[event];
		const uint32_t town = e.u8(0x10);
		if (town < town_count(save)) {
			TownMemory& t = town_of(save, town);
			++t.u16(0x996);
			t.s32(0x1C) = int32_t(uint32_t(t.s32(0x1C)) + e.u32(0xC));
		}
	}
	if (g.u32(kAldermanBusy) != 0) {
		return 0;  // Sin comprobar con el oráculo.
	}
	int32_t stock = 0;
	int32_t use = 0;
	for (uint32_t i = town_count(save); i > 0;) {
		--i;
		stock = int32_t(uint32_t(stock) + uint32_t(town_of(save, i).s32(0x1C)));
		use = int32_t(uint32_t(use) + uint32_t(town_of(save, i).s32(0x328)));
	}
	Event& e = save.events[event];
	const int32_t need = int32_t(uint32_t(use) * 40);
	e.u32(0xC) = need > stock ? uint32_t(need - stock) : 0;
	e.u32(8) = e.u32(0) + 0xA00;
	e.u32(8) += game_rand(save) & 0x3FF;
	uint32_t dist[5] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0};
	uint32_t index[5] = {0, 0, 0, 0, 0};
	for (uint32_t i = town_count(save); i > 0;) {
		--i;
		const uint32_t row = 0x6DDB90 + 0x34 * g.u8(0x6DE4B8 + i);
		const uint32_t x = uint32_t(g.s32(row + 0x20));
		const uint32_t dy = 0x5DC - uint32_t(g.s32(row + 0x24));
		const uint32_t d = x * x + dy * dy;
		for (int32_t k = 3; k >= 0; --k) {
			if (dist[k] > d) {
				dist[k + 1] = dist[k];
				index[k + 1] = index[k];
				index[k] = i;
				dist[k] = d;
			}
		}
	}
	save.events[event].u8(0x10) = uint8_t(index[game_rand(save) & 3]);
	if (g.u8(kOperations) & 0xC) {
		// Sin comprobar con el oráculo (partidas en red): operación 0x83.
		Event& ev = save.events[event];
		Operation op;
		op.u32(0, 0x83);
		op.u32(4, ev.u32(8));
		op.u32(8, ev.u32(0xC));
		op.u32(0xC, ev.u8(0x10));
		enqueue(save, op);
		return 0;
	}
	save.events[event].u32(0) = save.events[event].u32(8);
	return 1;
}

// --- Evento 0x22: los pedidos de mercancía del mundo ---

// Valor de una unidad interna de cada mercancía (0x673A18).
constexpr uint32_t kWareValueBits[24] = {0x3d6147af, 0x3ef50482, 0x3e6154ca, 0x3e322d0e, 0x3e11eb85, 0x3f0ccccd,
	0x3fb33333, 0x3f8ccccd, 0x3f845a1d, 0x40587ae1, 0x3ed33333, 0x3ce147af, 0x3fa39581, 0x3f8f5c29, 0x3ee147af,
	0x3e8e5604, 0x3ee147af, 0x3e6147af, 0x3f5ae147, 0x3d236e30, 0x41200000, 0x41000000, 0x41400000, 0x41600000};

// 0x4DB7C0 (evento 0x22): rehace los 7 pedidos de 12 bytes de 0x6DE530 (del
// mundo, [+0x90]): [+0x0] ciudad, [+0x1] mercancía, [+0x2] 1 si se paga más,
// [+0x3] cantidad (hasta 250), u32 [+0x4] fecha límite, u32 [+0x8] precio.
// Recorre ciudades (de [+0xC] en [+0xC] del evento, módulo 0x35) buscando
// una mercancía de las 8 de [0x2C4] (bits que pone al final: las que no le
// quedan) sin existencias ni producción y con el umbral [0x4F0 + 4·w] alto.
// El evento vuelve en 7 a 10 días.
void fn_4db7c0(SaveGame& save, uint16_t event) {
	static constexpr uint8_t kWares[8] = {0, 3, 2, 0xB, 0xE, 1, 7, 8};
	GlobalMemory& g = save.globals;
	constexpr uint32_t kOrders = 0x6DE530;
	for (uint32_t k = 0; k < 7; ++k) {
		g.u8(kOrders + 12 * k) = 0xFF;
		g.u8(kOrders + 12 * k + 1) = 0xFF;
	}
	uint32_t count = 0;
	for (uint32_t round = 0; round < 0x17; ++round) {
		uint32_t town;
		do {
			Event& e = save.events[event];
			e.u32(8) = (e.u32(0xC) + e.u32(8)) % 0x35;
			town = e.u32(8);
		} while (town >= town_count(save));
		const TownMemory& t = town_of(save, town);
		const uint32_t mask = t.u8(0x2C4);
		for (uint32_t j = 0; j < 8; ++j) {
			if (!(mask & (1u << j))) {
				continue;
			}
			const uint32_t w = kWares[j];
			if (t.s32(4 + 4 * w) != 0 || t.s32(0xC4 + 4 * w) != 0) {
				continue;
			}
			const bool fine = kFineReserve[w] != 0;
			const int32_t threshold = t.s32(0x4F0 + 4 * w);
			if (fine ? threshold <= 0x2710 : threshold <= 0x4E20) {
				continue;
			}
			const uint32_t order = kOrders + 12 * count;
			g.u8(order) = uint8_t(town);
			g.u8(order + 1) = uint8_t(w);
			g.u8(order + 2) = int8_t(t.u32(0x310) & 3) < int8_t((g.u32(kDayOfYear) + town) & 3) ? 1 : 0;
			int32_t base = int32_t((t.u32(0x314) & 0xF) + (t.u32(0x2D4) & 0xF) + (w & 3) + (t.u32(0x310) & 7) + 0xB4) * 25;
			base = fine ? base * 8 : base * 80;
			const float f = float(base);
			int32_t price = ftol32(double(f) * double(as_float(kWareValueBits[w])) * double(as_float(0x3C23D70A)));
			if (price < 1) {
				price = 1;  // Sin comprobar con el oráculo.
			}
			int32_t amount;
			if (count >= 5) {
				g.u32(order + 8) = uint32_t(uint64_t(uint32_t(price) * 170u) / 220);
				amount = t.s32(0x4F0 + 16 * w) << 2;
			} else {
				g.u32(order + 8) = uint32_t(price);
				amount = t.s32(0x4F0 + 16 * w);
				amount += amount >> 1;
			}
			int32_t units = fine ? amount / 200 : amount / 2000;
			if (units > 0xFA) {
				units = 0xFA;
			}
			g.u8(order + 3) = uint8_t(units);
			g.u32(order + 4) = (((w + town) & 3) + (round & 3) + 9) * 256 + g.u32(kTick);
			++count;
			if (count >= 7) {
				round = 100;
			}
			break;
		}
	}
	for (uint32_t i = 0; i < town_count(save); ++i) {
		TownMemory& t = town_of(save, i);
		uint8_t bits = 0;
		for (uint32_t j = 0; j < 8; ++j) {
			if (t.s32(4 + 4 * kWares[j]) <= 0) {
				bits = uint8_t(bits | (1u << j));
			}
		}
		t.u16(0x2C4) = bits;
	}
	save.events[event].u32(0) += ((town_of(save, 0).u32(0x2D4) & 3) + 7) << 8;
}

}  // namespace events_detail

using namespace events_detail;

void set_event_navigation(const NavData* nav) {
	g_navigation = nav;
}

const NavData* event_navigation() {
	return g_navigation;
}

// 0x4D85C0 (thiscall sobre la cola, 0x6DD73C): atiende los eventos con tick
// <= `tick`.
void fn_4d85c0(SaveGame& save, uint32_t tick) {
	GlobalMemory& g = save.globals;
	if (g.u16(kEventHead) > g.u16(kEventCount)) {
		return;
	}
	g.u32(kEventBusy) = 1;
	// El juego no mira si la cabeza termina la lista: siempre hay eventos
	// lejanos (los del tipo 2, una vez al año).
	while (g.u16(kEventHead) < save.events.size() && save.events[g.u16(kEventHead)].u32(0) <= tick) {
		const uint16_t head = g.u16(kEventHead);
		Event& e = save.events[head];
		enum { kRemove, kResort, kByResult } next = kRemove;
		uint32_t result = 0;
		switch (e.u16(6)) {
		case 0x00:
			result = fn_4db330(save, head);
			next = kByResult;
			break;
		case 0x04:
			// La IA del comerciante: con la cola marcada como libre.
			g.u32(kEventBusy) = 0;
			result = fn_4dfc94(save, head);
			g.u32(kEventBusy) = 1;
			next = kByResult;
			break;
		case 0x06:
			fn_4e2144(save);
			save.events[head].u32(0) += 0x700;
			next = kResort;
			break;
		case 0x08:
			fn_4e2634(save, head);
			next = kResort;
			break;
		case 0x0D:
			fn_4e4984(save, head);
			next = kResort;
			break;
		case 0x19:
			fn_4e8e24(save, head);
			next = kResort;
			break;
		case 0x1A:
			// Los comerciantes, del último al primero; cada 64 ticks.
			for (uint32_t m = merchant_count(save); m > 0;) {
				fn_4f6c10(save, int(--m));
			}
			e.u32(0) = (e.u32(0) + 0x40) & ~0xFu;
			next = kResort;
			break;
		case 0x1B:
			// Guion de una misión ([+0x8] el programa, [+0xC] sus variables). En
			// las partidas siempre llega sin programa: se libera y se quita.
			// Por traducir: el intérprete de 0x4ECF64 con programa.
			e.u32(0xC) = 0;
			result = 0;
			next = kByResult;
			break;
		case 0x22:
			fn_4db7c0(save, head);
			next = kResort;
			break;
		case 0x37: {
			// Vuelve a dejar comerciar al comerciante [+0xC] en la ciudad [+0x8].
			// Sin comprobar con el oráculo.
			MerchantMemory& m = save.merchants[e.u32(0xC)].memory;
			m.u32(0x608) &= ~(1u << (e.u32(8) & 31));
			next = kRemove;
			break;
		}
		default: {
			// Los demás tipos, en events_*.cpp. Los que no traduce ninguno se
			// quitan sin hacer nada (el juego haría lo de su manejador), así que
			// a partir de ahí p3vita se aparta.
			const uint16_t type = e.u16(6);
			int action = -1;
			for (auto group : {events_big, events_towns, events_mayor, events_rare}) {
				if ((action = group(save, head, type)) >= 0) {
					break;
				}
			}
			next = action == 1 ? kResort : kRemove;
			break;
		}
		}
		if (next == kByResult) {
			next = result ? kResort : kRemove;
		}
		if (next == kResort) {
			fn_4d8dd0(save);
			continue;
		}
		// Quita el evento de la cabeza: vuelve a la lista de libres.
		Event& first = save.events[g.u16(kEventHead)];
		if (first.u16(6) == 0x1B) {
			first.u32(0xC) = 0;
		}
		const uint16_t index = g.u16(kEventHead);
		g.u16(kEventHead) = first.u16(4);
		first.u16(4) = g.u16(kEventFree);
		first.u16(6) = 0xFFFF;
		g.u16(kEventFree) = index;
	}
	g.u32(kEventBusy) = 0;
}

}  // namespace p3::data::tick
