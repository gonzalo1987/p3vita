// La cola de eventos (events.cpp): los tipos 0x2C (0x4DEA20, 0x4DEBA0), 0x05 (0x4E5B84) y 0x16 (0x4E8234).
//
// Evento 0x2C (uno en cada partida; el despachador lo atiende así): si han
// pasado 0x5D00 ticks desde el último recálculo ([+0x10]), 0x4DEA20 rehace
// su intervalo [+0xC]; si [0x6DF31C] es 0, la IA busca una empresa que
// construir (0x4DEBA0: encola la operación 0x22); el tick avanza [+0xC] y
// el evento vuelve a su sitio.
//   [+0x8]  u32 lo que valía el mayor de los jugadores humanos (0x4DEA20)
//   [+0xC]  u32 intervalo en ticks
//   [+0x10] u32 tick del último recálculo
//
// Evento 0x05 (encargos a un comerciante; 0x4E5B84 decide si sigue):
//   [+0x8]  u8 comerciante            [+0x9]  u8 otra ciudad
//   [+0xA]  u8 otro comerciante (en los avisos del tipo 3)
//   [+0xB]  u8 ciudad del encargo     [+0xC]  u32 un tick (de cuando se creó)
//   [+0x10] u8 tipo de encargo (0..15) [+0x11] u8 estado: 0 nuevo, 1 en
//           curso, 2 pagado en mercancía, 3 castigo (tipo 15)
//   [+0x12] u16, [+0x14] u16 al azar al empezar; [+0x16] u16 (los avisos)
// La primera vez lo anuncia y lo aplaza de 8 a 15 días; la segunda, si el
// comerciante no está en los huecos [0x6DC..0x6DF] de las dos ciudades (o
// solo en uno y [+0x12] > 20000; los suyos se vacían), baja su [0x120 +
// 12·ciudad] (0x4F8F10) y calcula la paga: el evento se convierte en el 0x01
// que le da el dinero; o, si a su ciudad natal le falta una mercancía y su
// oficina de la ciudad [+0xB] tiene lo que vale la paga, se le quita de allí
// (estado 2, se quita el evento); o, en el tipo 15, se convierte en el 0x37
// que le prohíbe comerciar en la ciudad [+0x9] unas semanas. Si no se
// cumple, se quita.
//
// Evento 0x16 (0x4E8234; se recoloca siempre): a cada jugador humano que
// cumple las condiciones le manda el aviso 0x21 (salvo que tenga en su buzón,
// 0x6DD730, un 0x21 o 0x22 aún vigente: entonces solo se aplaza el evento).
//   [+0x8], [+0xA], [+0xC] u16 semillas del aviso; [+0xE] u8 contador 1..8
//
// Los avisos (0x4D6530, 0x548CA0) y sus textos (0x51A850, 0x51A880) no se
// traducen. Lo que el despachador hace con cada uno, en events_big().

#include <cassert>
#include <cstring>

#include "p3data/mail.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

// Traducciones de otras partes que se usan aquí.
namespace construction_detail {
bool fn_4f9c80(const SaveGame& save, int town, uint8_t& x, uint8_t& y, uint32_t type, uint8_t param, bool flag);
}  // namespace construction_detail
namespace events_detail {
int16_t fn_4f73d0(const SaveGame& save, int merchant);
}  // namespace events_detail

namespace {

constexpr uint32_t kEvents = 0x6DD73C;
constexpr uint32_t kEventHead = kEvents + 0x8;  // u16
constexpr uint32_t kMail = 0x6DD730;  // Buzón de los avisos (SaveGame::mail).
constexpr uint32_t kMailCount = kMail + 0x6;  // u16
constexpr uint32_t kWorldRecordCount = 0x6DE4A6;  // u16
// [0x6DE52C] u8 (del mundo, [+0x8C]): acorta el intervalo del evento 0x2C.
constexpr uint32_t kWorld8C = 0x6DE52C;
// Objeto de las operaciones encoladas (0x6DF2F0): su u8 [+0x0] & 0xC y su
// u32 [+0x2C] (0x6DF31C, ver merchants.cpp) cambian lo que hacen los eventos.
constexpr uint32_t kOperations = 0x6DF2F0;
constexpr uint32_t kOperations2C = 0x6DF31C;

using Event = GameMemory<0x18>;

// Una operación para el juego como la deja 0x535740: código 0xFFFFFFFF y el resto a 0.
struct Operation {
	uint8_t bytes[0x14] = {0xFF, 0xFF, 0xFF, 0xFF};
	void u32(size_t o, uint32_t v) { std::memcpy(bytes + o, &v, 4); }
};

void enqueue(SaveGame& save, const Operation& op) {
	enqueue_operation(save, op.bytes, sizeof(op.bytes));
}

uint32_t town_count(const SaveGame& save) {
	return save.globals.u32(global::kWorldTownCount);
}

uint32_t merchant_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldMerchantCount);
}

uint32_t office_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldOfficeCount);
}

// Mercancía de cada tipo de instalación (0x672C2C): lo que cuenta 0x4DEBA0
// como producción de los nodos [0x9B4].
constexpr uint8_t kFacilityWare[24] = {0x06, 0x06, 0x06, 0x14, 0x09, 0x02, 0x03, 0x0C, 0x05, 0x00, 0x01, 0x0B,
	0x08, 0x04, 0x10, 0x0E, 0x07, 0x12, 0x13, 0x0F, 0x11, 0x00, 0x00, 0x00};
// Empresa que produce cada mercancía (0x672C88); la 10 (aceite de ballena)
// va aparte (0xFF).
constexpr uint8_t kWareFacility[20] = {0x09, 0x0A, 0x05, 0x06, 0x0D, 0x08, 0x00, 0x10, 0x0C, 0x04, 0xFF, 0x0B,
	0x07, 0x0A, 0x0F, 0x13, 0x0E, 0x14, 0x11, 0x12};
// Materia prima de cada tipo de empresa (0x672C44; 0xFF: ninguna).
constexpr uint8_t kFacilityInput[21] = {0xFF, 0xFF, 0xFF, 0x0C, 0x0C, 0x04, 0x00, 0x10, 0xFF, 0xFF, 0x04, 0xFF,
	0x0E, 0x0B, 0xFF, 0xFF, 0xFF, 0x0B, 0x0B, 0x0B, 0xFF};
// 0x672C14: 1 si la mercancía va por barriles (200 unidades internas), 0 si por cargas (2000).
constexpr uint8_t kBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
// Valor de una unidad interna de cada mercancía (0x673A18).
constexpr uint32_t kWareValueBits[24] = {0x3d6147af, 0x3ef50482, 0x3e6154ca, 0x3e322d0e, 0x3e11eb85, 0x3f0ccccd,
	0x3fb33333, 0x3f8ccccd, 0x3f845a1d, 0x40587ae1, 0x3ed33333, 0x3ce147af, 0x3fa39581, 0x3f8f5c29, 0x3ee147af,
	0x3e8e5604, 0x3ee147af, 0x3e6147af, 0x3f5ae147, 0x3d236e30, 0x41200000, 0x41000000, 0x41400000, 0x41600000};
// Día del año en que empieza cada mes (0x672D78, u16; se indexa con & 0xF).
constexpr uint16_t kMonthStart[16] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365, 0, 31, 28};

float as_float(uint32_t bits) {
	float f;
	std::memcpy(&f, &bits, 4);
	return f;
}

// 0x4F3610 (sobre la ciudad): ¿el bit 0x10 de [0x2C8] (asedio)?
bool besieged(const TownMemory& t) {
	return (t.u32(0x2C8) & 0x10) == 0x10;
}

// 0x4FCBA0 (town_class1 [0x7A4]): ¿la celda (a, b) de la rejilla tiene los
// bits 0x30? Índice a·alto + b. Las búsquedas de 0x500F50 leen a veces fuera
// de la rejilla: el juego lee la memoria de al lado; aquí cuenta como 0.
bool site_cell(const SavedTown& st, uint8_t a, uint8_t b) {
	const int64_t i = int64_t(st.memory.s32(0x7AC)) * a + b;
	const uint8_t cell = i >= 0 && i < int64_t(st.grid.size()) ? st.grid[size_t(i)] : 0;
	return (cell & 0x30) == 0x30;
}

}  // namespace

namespace events_big_detail {

// 0x4DEA20 (sobre la cola; evento 0x2C cada 0x5D00 ticks): el mayor, entre
// los jugadores humanos, de [+0x474] más los [+0x4] de los registros del
// mundo de sus oficinas; con él y los habitantes de todas las ciudades, el
// intervalo [+0xC] del evento (0x6978000 / habitantes, más corto si el mayor
// ha crecido desde el anterior [+0x8] o con [0x6DE52C]).
void fn_4dea20(SaveGame& save, uint16_t event) {
	const GlobalMemory& g = save.globals;
	uint32_t best = 0;
	for (uint32_t i = 0; i < merchant_count(save); ++i) {
		const MerchantMemory& m = save.merchants[i].memory;
		if (m.u16(0x8) != 0) {
			continue;
		}
		uint32_t value = m.u32(0x474);
		uint32_t office = m.u16(0xC);
		while (office < office_count(save)) {
			const OfficeMemory& o = save.offices[office].memory;
			office = o.u16(0x2C8);  // Siguiente oficina del comerciante.
			for (uint32_t r = o.u16(0x2CC); r < g.u16(kWorldRecordCount);) {
				const GameMemory<0x14>& w = save.world_records[r];
				r = w.u16(0x8);
				value += w.u16(0x4);
			}
		}
		if (value > best) {
			best = value;
		}
	}
	Event& e = save.events[event];
	e.u32(0xC) = 0x6978000;
	uint32_t people = 1;
	for (uint32_t t = 0; t < town_count(save); ++t) {
		people += save.towns[t].memory.u32(0x2D4);
	}
	const uint32_t interval = e.u32(0xC) / people;
	const uint32_t growth = (best << 10) / (e.u32(0x8) + 1);
	e.u32(0xC) = interval;
	e.u32(0x8) = best;
	const uint32_t level = growth + uint32_t(g.u8(kWorld8C)) * 0x66;
	if (level > 0x400) {
		const uint32_t scaled = interval << 10;
		e.u32(0xC) = scaled;
		e.u32(0xC) = level > 0x600 ? scaled / 0x600 : scaled / level;
	}
}

// 0x50FFB0 (sobre un nodo de [0x9B4]): [+0x8] por [+0xE] / 1024 (0 si
// [+0xE] es 0; [+0x8] entero desde 0x400).
int32_t fn_50ffb0(const TownNode& node) {
	const uint16_t level = node.u16(0xE);
	if (level < 1) {
		return 0;
	}
	const int32_t value = node.s32(0x8);
	if (level >= 0x400) {
		return value;
	}
	return int32_t(uint64_t((int64_t(value) * level) >> 10));
}

// 0x500F50 (sobre la oficina): el punto de partida para buscar solar a una
// empresa de tipo `type` (< 0x15) del comerciante: junto a una de las suyas
// del mismo tipo (lista [0x2CE] de la oficina) que tenga en uno de los cuatro
// lados (empezando por uno que depende del tick) una celda con los bits
// 0x30; `n` (el día del mes & 3) cuántas de esas salta. Si no hay ninguna,
// junto a una puerta de la muralla (lista [0x768] de la ciudad); si tampoco,
// junto a la última de sus empresas de ese tipo. Si no encuentra nada, (x, y)
// no cambian. Con `type` >= 0x15, la (n + 2)-ésima casa de ese tipo (lista
// [0x2D0]) o la última.
void fn_500f50(const SaveGame& save, int office, uint8_t& x, uint8_t& y, int32_t n, uint8_t type) {
	const OfficeMemory& o = save.offices[size_t(office)].memory;
	const SavedTown& st = save.towns[o.u8(0x2C6)];
	const TownMemory& t = st.memory;
	if (type >= 0x15) {
		// Sin comprobar con el oráculo (0x4DEBA0 solo pide empresas).
		for (uint32_t r = o.u16(0x2D0); r < t.u16(0x776);) {
			const BuildingRecord& rec = st.records[r];
			if (rec.type == type) {
				x = rec.x;
				y = rec.y;
				if (n-- < 0) {
					return;
				}
			}
			r = rec.next;
		}
		return;
	}
	uint8_t found[4] = {0xFF, 0xFF, 0xFF, 0xFF};  // (x, y) con sitio al lado y (x, y) sin él.
	for (uint32_t r = o.u16(0x2CE); r < t.u16(0x776);) {
		const BuildingRecord& rec = st.records[r];
		r = rec.next;
		if (rec.type != type) {
			continue;
		}
		uint32_t side = rec.x + save.globals.u32(global::kWorldTick);
		bool hit = false;
		for (uint32_t k = 0; k < 4; ++k) {
			uint8_t a;
			uint8_t b;
			switch (side++ & 3) {
			case 0:
				a = uint8_t(rec.x + 2);
				b = uint8_t(rec.y + 4);
				break;
			case 1:
				a = uint8_t((rec.y & 1) + rec.x + 2);
				b = uint8_t(rec.y - 5);
				break;
			case 2:
				a = uint8_t(rec.x - 2);
				b = uint8_t(rec.y - 4);
				break;
			default:
				a = uint8_t((rec.y & 1) + rec.x - 3);
				b = uint8_t(rec.y + 5);
				break;
			}
			hit = site_cell(st, a, b);
			if (hit) {
				if (n-- > 0) {
					found[0] = rec.x;
					found[1] = rec.y;
				} else {
					x = rec.x;
					y = rec.y;
				}
				break;
			}
		}
		if (!hit) {
			found[2] = rec.x;
			found[3] = rec.y;
		}
	}
	if (found[0] == 0xFF) {
		// Junto a las puertas y portones de la muralla.
		for (uint32_t r = t.u16(0x768); r < t.u16(0x776);) {
			const BuildingRecord& rec = st.records[r];
			r = rec.next;
			const uint8_t rx = rec.x;
			const uint8_t ry = rec.y;
			const uint8_t odd = ry & 1;
			uint8_t cand[4][2];
			if (rec.type == 0x38 || rec.type == 0x35) {
				cand[0][0] = uint8_t(rx + 3), cand[0][1] = ry;
				cand[1][0] = uint8_t(rx - 1), cand[1][1] = uint8_t(ry - 8);
				cand[2][0] = uint8_t(odd + rx - 2), cand[2][1] = uint8_t(ry + 9);
				cand[3][0] = uint8_t(odd + rx - 6), cand[3][1] = uint8_t(ry + 1);
			} else if (rec.type == 0x37 || rec.type == 0x34) {
				cand[0][0] = uint8_t(rx + 5), cand[0][1] = ry;
				cand[1][0] = uint8_t(rx + 1), cand[1][1] = uint8_t(ry - 8);
				cand[2][0] = uint8_t(odd + rx - 4), cand[2][1] = uint8_t(ry + 1);
				cand[3][0] = uint8_t(odd + rx), cand[3][1] = uint8_t(ry + 9);
			} else {
				continue;
			}
			for (const auto& c : cand) {
				if (site_cell(st, c[0], c[1])) {
					x = c[0];
					y = c[1];
					return;
				}
			}
		}
	}
	for (int k = 0; k < 2; ++k) {
		if (found[2 * k] != 0xFF) {
			x = found[2 * k];
			y = found[2 * k + 1];
			return;
		}
	}
}

// 0x4DEBA0 (sobre la cola; evento 0x2C): la IA busca una empresa que
// construir. Si algún comerciante desde el 20 tiene más de 40000: suma en
// todas las ciudades el consumo (habitantes [0x310] y empresas [0x64], más el
// de las oficinas), la producción ([0x490] y la de los nodos) y las
// existencias de cada mercancía; cada tipo de empresa vale lo que falta de
// su mercancía (consumo / (producción / 100)), 0 si sobra o si la ciudad ya
// tiene una obra de ese tipo. Ordena las ciudades por habitantes (con algo
// de azar; 0 las que tienen las empresas de la IA con menos del 85 % de los
// trabajadores) y, para el aceite de ballena (una del tipo 5 de la ciudad
// misma) o los cinco tipos que más valen, busca en cada ciudad una oficina de
// un comerciante de la IA con más de 40000 y un solar (0x500F50, 0x4F9C80):
// la primera que lo encuentra encola la operación 0x22 (construir).
void fn_4deba0(SaveGame& save) {
	const GlobalMemory& g = save.globals;
	Operation op;
	const uint32_t merchants = merchant_count(save);
	uint32_t i = 0x14;
	for (; i < merchants; ++i) {
		if (save.merchants[i].memory.s32(0x0) > 0x9C40) {
			i = merchants + 0x3E8;
		}
	}
	if (i <= merchants) {
		return;
	}

	const uint32_t towns = town_count(save);
	uint32_t need[24] = {};  // Consumo diario de cada mercancía.
	uint32_t made[24] = {};  // Producción diaria.
	uint32_t stock[24] = {};
	for (uint32_t t = 0; t < towns; ++t) {
		const SavedTown& st = save.towns[t];
		const TownMemory& m = st.memory;
		for (size_t k = 0; k < 20; ++k) {
			need[k] = m.u32(0x310 + 4 * k) + m.u32(0x64 + 4 * k) + need[k];
			made[k] += m.u32(0x490 + 4 * k);
			stock[k] += m.u32(0x4 + 4 * k);
		}
		for (uint32_t o = m.u16(0x784); o < office_count(save); o = save.offices[o].memory.u16(0x2CA)) {
			for (size_t k = 0; k < 20; ++k) {
				need[k] += save.offices[o].memory.u32(0x64 + 4 * k);
			}
		}
		for (const TownNode& node : st.nodes) {
			assert(node.u8(0x10) < 24);  // El juego no lo comprueba.
			made[kFacilityWare[node.u8(0x10)]] += uint32_t(fn_50ffb0(node));
		}
	}
	// Grano, miel, vino y cáñamo: 1/8 más; en invierno (diciembre a febrero),
	// nada de grano, miel ni cáñamo.
	for (size_t k : {0, 17, 7, 5}) {
		need[k] += uint32_t(int32_t(need[k]) >> 3);
	}
	const uint8_t month = g.u8(global::kWorldMonth);
	if (month < 2 || month >= 11) {
		need[0] = 0;
		need[17] = 0;
		need[5] = 0;
	}

	int32_t value[21] = {};  // Lo que vale construir cada tipo de empresa.
	int32_t whale = 0;  // Lo mismo para el aceite de ballena (mercancía 10).
	for (size_t k = 20; k-- > 0;) {
		const int32_t p = int32_t(made[k]);
		const int32_t margin = int32_t(uint32_t(p >> 5) + uint32_t(p) + uint32_t(p >> 4));
		const int32_t n = int32_t(need[k]);
		int32_t v;
		if (n < (int32_t(stock[k]) >> 5) || n < margin) {
			v = 0;
		} else if (p > 100) {
			v = n / (p / 100);
		} else {
			v = 100;
		}
		if (k == 10) {
			whale = v;
		} else {
			value[kWareFacility[k]] = v;
		}
	}

	int32_t score[40] = {};  // Por ciudad.
	for (uint32_t t = 0; t < towns; ++t) {
		const SavedTown& st = save.towns[t];
		const TownMemory& m = st.memory;
		score[t] = m.s32(0x2D4) > 100000 ? int32_t(200000 - uint32_t(m.s32(0x2D4))) : 100000;
		score[t] = int32_t(uint32_t(score[t]) - (game_rand(save) & 0xFFFF));
		if (!(m.u32(0x2C8) & 0xE0)) {
			continue;
		}
		// Las obras en curso ([0x762]) de empresas: ese tipo no vale.
		for (uint32_t r = m.u16(0x762); r < m.u16(0x776);) {
			const BuildingRecord& rec = st.records[r];
			r = rec.next;
			if (rec.type < 0x15) {
				value[rec.type] = 0;
			}
		}
		// Trabajadores ([+0x4]) y puestos ([+0xC]) de las empresas de la IA.
		uint32_t places = 0;
		uint32_t workers = 0;
		for (uint32_t o = m.u16(0x784); o < office_count(save); o = save.offices[o].memory.u16(0x2CA)) {
			const OfficeMemory& office = save.offices[o].memory;
			if (save.merchants[office.u16(0x2C4)].memory.u16(0x8) == 0) {
				continue;
			}
			for (uint32_t r = office.u16(0x2CC); r < g.u16(kWorldRecordCount);) {
				const GameMemory<0x14>& w = save.world_records[r];
				workers += w.u16(0x4);
				places += w.u16(0xC);
				r = w.u16(0x8);
			}
		}
		if (places > 0 && workers * 20 < places * 17) {
			score[t] = 0;
		}
	}

	// Los cinco tipos (desde el 4) que más valen, de mayor a menor.
	value[0] = 0;
	uint32_t top[6] = {};  // El sexto es un hueco que el juego escribe y no lee.
	for (uint32_t f = 4; f < 0x15; ++f) {
		for (int32_t j = 5; j > 0; --j) {
			if (value[f] > value[top[j - 1]]) {
				top[j] = top[j - 1];
				top[j - 1] = f;
			} else {
				j = 0;
			}
		}
	}
	// Las ciudades, de más a menos puntos.
	uint8_t order[40];
	for (uint32_t t = 0; t < towns; ++t) {
		order[t] = uint8_t(t);
	}
	for (uint32_t a = 0; a < towns; ++a) {
		for (uint32_t b = a + 1; b < towns; ++b) {
			if (score[order[a]] < score[order[b]]) {
				const uint8_t swap = order[a];
				order[a] = order[b];
				order[b] = swap;
			}
		}
	}

	// Prueba un solar para la empresa `type` del comerciante `owner` en la
	// ciudad `town` (`place` es la que va en la operación): desde la estatua
	// ([0x800], [0x801]) más un poco de azar o junto a lo suyo (0x500F50).
	// Si lo encuentra, encola la operación 0x22: u8 [+0x4] comerciante, [+0x5]
	// ciudad, [+0x6] x, [+0x7] y, [+0x8] tipo, [+0x9] 0.
	auto try_build = [&](uint32_t town, int office, uint8_t owner, uint8_t place, uint8_t type) {
		const TownMemory& m = save.towns[town].memory;
		op.u32(0, 0x22);
		op.bytes[0x8] = type;
		op.bytes[0x5] = place;
		op.bytes[0x4] = owner;
		op.bytes[0x9] = 0;
		uint8_t& x = op.bytes[0x6];
		uint8_t& y = op.bytes[0x7];
		x = uint8_t(m.u8(0x800) + (game_rand(save) & 3));
		y = uint8_t(m.u8(0x801) + (game_rand(save) & 3));
		if (office >= 0) {
			fn_500f50(save, office, x, y, g.u8(global::kWorldDay) & 3, type);
		}
		if (!construction_detail::fn_4f9c80(save, int(town), x, y, type, 0, besieged(m))) {
			return false;
		}
		enqueue(save, op);
		return true;
	};

	if (whale > value[top[0]]) {
		// En las partidas el aceite de ballena nunca vale más que el primero de
		// los cinco: comprobado solo con estados retocados.
		for (uint32_t k = 0; k < towns; ++k) {
			const uint32_t t = order[k];
			if (score[t] <= 0) {
				k = 0x28;
				continue;
			}
			if (towns <= t || save.towns[t].memory.u32(0x2CC) < 0x384) {
				continue;
			}
			// La oficina del comerciante de índice `t` en la ciudad (así lo hace el juego).
			const int office = facilities_detail::fn_5308a0(save, t, t);
			if (try_build(t, office, uint8_t(t), uint8_t(t), 5)) {
				return;
			}
		}
	}

	for (uint32_t s = 0; s < 5; ++s) {
		const uint32_t type = top[s];
		if (type == 0 || value[type] <= 0) {
			return;
		}
		const uint32_t input = kFacilityInput[type];
		for (uint32_t k = 0; k < towns; ++k) {
			const uint32_t t = order[k];
			if (score[t] < 1) {
				k = 0x28;
				continue;
			}
			const TownMemory& m = save.towns[t].memory;
			if (m.u16(0x848 + 16 * type) < 1000) {
				continue;
			}
			// Oficina de un comerciante de la IA ([+0x8] & 7 = 1 en la primera
			// pasada, > 1 en la segunda) que no tenga de la materia prima más
			// del doble de lo que tiene en el almacén ([0x354]).
			auto spare = [&](const OfficeMemory& o) {
				return !(input < 0x18 && o.s32(0x354 + 4 * input) > 0 &&
					o.s32(0x354 + 4 * input) > int32_t(uint32_t(o.s32(0x4 + 4 * input)) << 1));
			};
			uint32_t first = m.u16(0x784);
			// Si a la ciudad le falta la materia prima, la primera pasada no.
			if (input < 0x18 && m.s32(0x4 + 4 * input) < m.s32(0x4F0 + 16 * input)) {
				first = 0xFFFF;
			}
			for (uint32_t o = first; o < office_count(save); o = save.offices[o].memory.u16(0x2CA)) {
				const OfficeMemory& office = save.offices[o].memory;
				const bool ok = spare(office);
				const MerchantMemory& owner = save.merchants[office.u16(0x2C4)].memory;
				if (ok && owner.s32(0x0) >= 0x9C40 && (owner.u16(0x8) & 7) == 1 &&
					try_build(t, int(o), office.u8(0x2C4), m.u8(0x2C0), uint8_t(type))) {
					return;
				}
			}
			for (uint32_t o = m.u16(0x784); o < office_count(save); o = save.offices[o].memory.u16(0x2CA)) {
				const OfficeMemory& office = save.offices[o].memory;
				const bool ok = spare(office);
				const MerchantMemory& owner = save.merchants[office.u16(0x2C4)].memory;
				if (owner.s32(0x0) > 0x9C40 && (owner.u16(0x8) & 7) > 1 && ok &&
					try_build(t, int(o), office.u8(0x2C4), m.u8(0x2C0), uint8_t(type))) {
					return;
				}
			}
		}
	}
}

// 0x4F8F10 (sobre el comerciante): baja su [0x120 + 12·ciudad] (float) en
// lo que corresponde al tipo de encargo: 2, 4, 1, 6 u 8 (nada en los 5..8).
void fn_4f8f10(SaveGame& save, int merchant, uint32_t town, uint32_t kind) {
	if (kind > 15) {
		return;
	}
	static constexpr uint8_t kCase[16] = {0, 1, 2, 3, 3, 5, 5, 5, 5, 0, 0, 1, 3, 0, 3, 4};  // 0x4F8FC4
	// 0x66F4A4, 0x66F4A0, 0x66BC80, 0x672F38 y 0x672F34.
	static constexpr float kDrop[5] = {2.0f, 4.0f, 1.0f, 6.0f, 8.0f};
	const uint8_t c = kCase[kind];
	if (c == 5) {
		return;
	}
	float& v = save.merchants[size_t(merchant)].memory.f32(0x120 + 12 * size_t(town));
	v = float(double(v) - double(kDrop[c]));
}

// The letter at the start of a merchant's task (event 0x05, 0x4E5C0E..
// 0x4E5D4D), to a human merchant [+0x8]: [+0x5] the town [+0xB]; by the kind
// [+0x10] (0x4E638C): 0, 1, 4: 0x19 with his [+0x1C], [+0x1D] in [+0x8],
// [+0x9], the kind in [+0xA] and the town [+0x9] in [+0xB]; 2, 9..12: the
// same with [+0xB] the town [+0xB] and u16 [+0xC] the event's [+0x16]; 3:
// as 0, 1, 4 with the [+0x1C], [+0x1D] of the merchant [+0xA] (the town
// [+0x9] as one if [+0xA] isn't) in [+0xC], [+0xD]; 5..8: 0x0A with u16
// [+0x8], [+0xA], [+0xC] his [+0x1C], [+0x1D] and the kind; 13..15: as 0, 1,
// 4 with u16 [+0xC] the event's [+0x16].
void start_letter(SaveGame& save, const Event& e, const MerchantMemory& m) {
	if (m.u16(0x8) != 0) {
		return;
	}
	const uint8_t kind = e.u8(0x10);
	Letter letter(0x19);
	letter.entry.u8(5) = e.u8(0xB);
	switch (kind) {
	case 5:
	case 6:
	case 7:
	case 8:
		letter.entry.u8(4) = 0x0A;
		letter.entry.u16(8) = m.u8(0x1C);
		letter.entry.u16(0xA) = m.u8(0x1D);
		letter.entry.u16(0xC) = kind;
		break;
	default:
		letter.entry.u8(8) = m.u8(0x1C);
		letter.entry.u8(9) = m.u8(0x1D);
		letter.entry.u8(0xA) = kind;
		letter.entry.u8(0xB) = e.u8(0x9);
		if (kind == 2 || (kind >= 9 && kind <= 12)) {
			letter.entry.u8(0xB) = e.u8(0xB);
			letter.entry.u16(0xC) = e.u16(0x16);
		} else if (kind >= 13) {
			letter.entry.u16(0xC) = e.u16(0x16);
		} else if (kind == 3) {
			const uint8_t who = e.u8(0xA) < merchant_count(save) ? e.u8(0xA) : e.u8(0x9);
			if (who < merchant_count(save)) {
				letter.entry.u8(0xC) = save.merchants[who].memory.u8(0x1C);
				letter.entry.u8(0xD) = save.merchants[who].memory.u8(0x1D);
			}  // Else sin comprobar con el oráculo (the game reads past the merchants).
		}
		break;
	}
	send_letter(save, e.u8(0x8), letter);
}

// 0x4E5B84 (evento 0x05, con su índice): el encargo del comerciante [+0x8].
// Devuelve 1 si el evento sigue (recolocado) y 0 si se quita.
uint32_t fn_4e5b84(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	Event& e = save.events[event];
	const uint32_t towns = town_count(save);
	if (e.u8(0xB) >= towns || e.u8(0x9) >= towns || e.u8(0x8) >= merchant_count(save)) {
		return 0;
	}
	const uint32_t merchant = e.u8(0x8);
	MerchantMemory& m = save.merchants[merchant].memory;
	if (e.u8(0x11) == 0) {
		// Empieza: el aviso al comerciante humano (no se traduce) y la espera.
		e.u8(0x11) = 1;
		if (e.u8(0x10) > 15) {
			return 0;  // Comprobado solo con estados retocados.
		}
		start_letter(save, e, m);
		if (g.u8(kOperations) & 0xC) {
			// Comprobado solo con estados retocados.
			if (g.u32(kOperations2C) == 0) {
				Operation op;
				op.u32(0x0, 0x80);
				op.u32(0x4, g.u16(kEventHead));
				op.u32(0x8, game_rand(save));
				op.u32(0xC, game_rand(save));
				enqueue(save, op);
			}
			e.u16(0x12) = 0xFFFF;
			e.u16(0x14) = 0xFFFF;
		} else {
			e.u16(0x12) = uint16_t(game_rand(save));
			e.u16(0x14) = uint16_t(game_rand(save));
		}
		const uint32_t t = e.u32(0x0);
		e.u32(0x0) = ((((t & 7) + 8) << 8) + t) | 0x80;
		return 1;
	}

	// Se cumple si el comerciante no está en los huecos [0x6DC..0x6DF] de la
	// ciudad [+0xB] ni de la [+0x9] (los suyos se vacían), o solo en uno y
	// [+0x12] > 20000.
	uint32_t marks = 0;
	TownMemory* t = &save.towns[e.u8(0xB)].memory;
	for (uint32_t k = 0; k < 8; ++k) {
		if (k == 4) {
			t = &save.towns[e.u8(0x9)].memory;
			if (marks >= 2) {
				break;
			}
		}
		uint8_t& slot = t->u8(0x6DC + (k & 3));
		if (slot == e.u8(0x8)) {
			slot = 0xFF;
			++marks;
		}
	}
	if (marks >= 2 || (marks == 1 && e.u16(0x12) <= 0x4E20)) {
		// Failed (comprobado solo con estados retocados): a human merchant
		// gets the letter 0x4A (0x4E5E8B..0x4E5F2A): [+0x5] the town [+0xB];
		// u16 [+0x8] his [+0x1C] << 8 | [+0x1D]; [+0xA] the same of the
		// merchant [+0xA] for the kind 3, else 0; [+0xC] the town [+0x9];
		// [+0xE] the kind, or 2 from the kind 9 on.
		if (m.u16(0x8) == 0) {
			Letter letter(0x4A);
			letter.entry.u8(5) = e.u8(0xB);
			letter.entry.u16(8) = uint16_t((m.u8(0x1C) << 8) + m.u8(0x1D));
			if (e.u8(0x10) == 3) {
				const MerchantMemory& o = save.merchants[e.u8(0xA) < merchant_count(save) ? e.u8(0xA) : 0].memory;
				letter.entry.u16(0xA) = uint16_t((o.u8(0x1C) << 8) + o.u8(0x1D));
			}
			letter.entry.u16(0xC) = e.u8(0x9);
			letter.entry.u16(0xE) = e.u8(0x10) < 9 ? e.u8(0x10) : 2;
			send_letter(save, e.u8(0x8), letter);
		}
		return 0;
	}

	const uint32_t kind = e.u8(0x10);
	if (kind > 15) {
		return 0;  // Comprobado solo con estados retocados.
	}
	// Días ([+0x14] al azar) y ciudades en las que baja [0x120 + 12·i].
	const uint32_t r = e.u16(0x14);
	uint32_t days;
	uint32_t town_a = e.u8(0xB);
	uint32_t town_b = 0xFF;
	switch (kind) {
	case 5:
	case 6:
	case 7:
		days = r % 7 + 2;
		town_a = 0xFF;
		break;
	// En las partidas solo salen los tipos 3, 5, 6 y 7; los casos de los demás,
	// comprobados con estados retocados (0, 1, 2, 4, 8, 9, 13 y 15).
	case 8:
		days = r % 21 + 0x1E;
		town_b = m.u8(0x19);
		break;
	case 2:
		days = r % 7 + 2;
		break;
	case 9:
	case 10:
		days = r % 11 + 10;
		break;
	case 0:
	case 13:
		days = r % 11 + 10;
		town_b = e.u8(0x9);
		break;
	case 1:
	case 11:
		days = r % 21 + 0x1E;
		break;
	case 4:
	case 12:
		days = r % 21 + 0x3C;
		break;
	case 3:
	case 14:
		days = r % 21 + 0x3C;
		town_b = e.u8(0x9);
		break;
	default:  // 15: castigo.
		e.u8(0x11) = 3;
		days = (e.u8(0x14) & 7) + 10;
		town_b = e.u8(0x9);
		break;
	}
	if (town_a < towns) {
		fn_4f8f10(save, int(merchant), town_a, kind);
	}
	if (town_b < towns) {
		fn_4f8f10(save, int(merchant), town_b, e.u8(0x10));
	}
	// La paga: max([+0x46C], 5000) · días / 500000 + 1, en quinientos, hasta 32750000.
	const int32_t worth = m.s32(0x46C);
	const int64_t product = worth < 0x1388 ? int64_t(int32_t(days)) * 0x1388 : int64_t(worth) * int32_t(days);
	int32_t pay = int32_t((uint32_t(int64_t(product / 0x7A120)) + 1) * 500);
	if (pay > 0x1F3B9B0) {
		pay = 0x1F3B9B0;
	}
	uint32_t amount = uint32_t(pay);
	uint32_t paid_ware = days;  // ebx in the game: the days, or the ware paid in.
	if (e.u8(0x11) == 1) {
		// Si a su ciudad natal le falta una mercancía ([0x2C8] & 0x20000, la
		// mercancía en los bits 18..22), la paga en ella si su oficina de la
		// ciudad [+0xB] tiene bastante: se le quita de allí (estado 2).
		const uint32_t flags = save.towns[m.u8(0x19)].memory.u32(0x2C8);
		if (flags & 0x20000) {
			const uint32_t ware = uint32_t(int32_t(flags) >> 18) & 0x1F;
			assert(ware < 24);  // El juego no lo comprueba.
			const int office = facilities_detail::fn_5308a0(save, merchant, town_a);
			const float money = float(int32_t(amount));
			uint32_t units = uint32_t(ftol(double(money) / double(as_float(kWareValueBits[ware]))));
			const uint32_t unit = kBarrels[ware] ? 200 : 2000;
			units -= units % unit;
			if (office >= 0) {
				// En las partidas, la oficina nunca tiene bastante: comprobado solo
				// con estados retocados.
				uint32_t& stock = save.offices[size_t(office)].memory.u32(0x4 + 4 * ware);
				paid_ware = ware;
				if (stock >= units) {
					e.u8(0x11) = 2;
					stock -= units;
					amount = units / unit;
				}
			}
		}
	}
	if (m.u16(0x8) == 0) {
		// A human merchant gets the result, the letter 0x4B (0x4E61AF..
		// 0x4E6286): [+0x5] the town [+0xB]; [+0x8] the state − 1; by the
		// state, 1: u16 [+0xA] the pay / 500; 2: [+0xA] the ware, [+0xB] the
		// loads; 3: [+0xA] the town [+0x9], [+0xB] the days; then [+0xC] the
		// kind, [+0xD], [+0xE] his [+0x1C], [+0x1D], and [+0xF] the town [+0x9]
		// (kinds 0 and 1) or, for the kind 3, the merchant [+0xA] (or the
		// town [+0x9] as one) with his [+0x1C] in [+0x9] and [+0x1D] here.
		Letter letter(0x4B);
		letter.entry.u8(5) = e.u8(0xB);
		letter.entry.u8(8) = uint8_t(e.u8(0x11) - 1);
		switch (e.u8(0x11)) {
		case 1:
			letter.entry.s16(0xA) = int16_t(int32_t(amount) / 500);
			break;
		case 2:
			letter.entry.u8(0xA) = uint8_t(paid_ware);
			letter.entry.u8(0xB) = uint8_t(amount);
			break;
		case 3:
			letter.entry.u8(0xA) = e.u8(0x9);
			letter.entry.u8(0xB) = uint8_t(paid_ware);
			break;
		default:
			break;
		}
		const uint8_t kind = e.u8(0x10);
		letter.entry.u8(0xC) = kind;
		letter.entry.u8(0xD) = m.u8(0x1C);
		letter.entry.u8(0xE) = m.u8(0x1D);
		if (kind == 0 || kind == 1) {
			letter.entry.u8(0xF) = e.u8(0x9);
		} else if (kind == 3) {
			const uint8_t who = e.u8(0xA) < merchant_count(save) ? e.u8(0xA) : e.u8(0x9);
			if (who < merchant_count(save)) {
				letter.entry.u8(9) = save.merchants[who].memory.u8(0x1C);
				letter.entry.u8(0xF) = save.merchants[who].memory.u8(0x1D);
			}  // Else sin comprobar con el oráculo (the game reads past the merchants).
		}
		send_letter(save, e.u8(0x8), letter);
	}
	switch (e.u8(0x11)) {
	case 1: {
		// El evento pasa a ser el 0x01 que le paga dentro de 20 días.
		const uint8_t who = e.u8(0x8);
		const uint8_t where = e.u8(0xB);
		Event& h = save.events[g.u16(kEventHead)];
		h.u32(0x0) += 0x1400;
		h.u16(0x6) = 0x01;
		h.u32(0x8) = amount & 0x7FFFFFFF;
		h.u32(0xC) = g.u32(global::kWorldTick);
		h.u8(0xC) = 0;
		h.u8(0x10) = 0xFF;
		h.u8(0x11) = who;
		h.u8(0x12) = 0;
		h.u8(0x13) = 0;
		h.u8(0x14) = where;
		h.u8(0x15) = 0;
		h.u8(0x16) = 0;
		h.u8(0x17) = 0;
		return 1;
	}
	case 3: {
		// Comprobado solo con estados retocados. No puede comerciar en la ciudad [+0x9]
		// durante `days` semanas: el evento pasa a ser el 0x37 que se lo devuelve.
		const uint32_t who = e.u8(0x8);
		const uint32_t where = e.u8(0x9);
		save.merchants[who].memory.u32(0x608) |= 1u << (where & 31);
		e.u16(0x6) = 0x37;
		e.u32(0x8) = where;
		e.u32(0xC) = who;
		e.u32(0x0) += days * 0x700;
		return 1;
	}
	default:
		return 0;
	}
}

// 0x4E8234 (evento 0x16, con su índice): a cada jugador humano sin [+0x31]
// (>= número de ciudades), con [+0x28] y [+0x2C] por llegar y bastantes
// puntos ([+0x46C] / 10000 hasta 30, más su reputación 0x4F73D0 hasta 30,
// más o menos según [+0x17]) frente a un umbral sacado de cuatro ciudades,
// el aviso 0x21 (no se traduce) con el contador [+0xE]; si en su buzón hay un
// 0x21 de hace menos de 42 días o un 0x22 aún vigente, solo se aplaza el
// evento. Después, el evento pasa a dentro de 0x5A00 ticks o, si es antes,
// al principio del año.
void fn_4e8234(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	Event& e = save.events[event];
	const uint32_t tick = g.u32(global::kWorldTick);
	for (uint32_t i = 0; i < merchant_count(save); ++i) {
		const MerchantMemory& m = save.merchants[i].memory;
		if (town_count(save) > m.u8(0x31) || tick >= m.u32(0x2C) || tick >= m.u32(0x28)) {
			continue;
		}
		int32_t points = m.s32(0x46C) / 10000;
		if (points > 0x1E) {
			points = 0x1E;
		} else if (points < 1) {
			continue;
		}
		const int32_t reputation = events_detail::fn_4f73d0(save, int(i));
		points += reputation > 0x1E ? 0x1E : reputation;
		if (points < 0x14) {
			continue;
		}
		const int8_t mood = m.s8(0x17);
		if (mood > 0x14) {
			continue;
		}
		if (mood < -0x14) {
			points += 10;
		} else if (mood >= 0x14) {
			continue;
		} else if (mood >= 10) {
			points -= mood;
		} else {
			points += 10;
		}
		if (m.u16(0x8) != 0) {
			// La IA: el juego salta por una constante distinta de 0 lo que sigue
			// (otra versión de lo mismo con operaciones 0x60).
			continue;
		}
		// El umbral, casi al azar: los 4 bits bajos del u8 [0x304] de la ciudad
		// 9, del consumo de cerveza de los habitantes [0x31C] de la 7, de
		// [0x2E0] de la 3 y de la sal del almacén [0x14] de la 0.
		assert(save.towns.size() >= 10);  // El juego no lo comprueba.
		const int32_t threshold = int32_t((save.towns[9].memory.u8(0x304) & 0xF) +
			(save.towns[7].memory.u32(0x31C) & 0xF) + (save.towns[3].memory.u32(0x2E0) & 0xF) +
			(save.towns[0].memory.u32(0x14) & 0xF));
		if (points < threshold) {
			continue;
		}
		// Su buzón (lista desde [+0xA]).
		const int32_t mail_count = g.u16(kMailCount);
		for (int32_t k = m.u16(0xA); k < mail_count;) {
			assert(size_t(k) < save.mail.size());
			const GameMemory<0x10>& a = save.mail[size_t(k)];
			k = a.u16(0x6);
			const uint32_t date = kMonthStart[a.u8(0x1) & 0xF] + uint32_t(a.u16(0x2)) * 365 + a.u8(0x0);
			if (a.u8(0x4) == 0x21) {
				if (tick > (date + 0x2A) << 8) {
					break;
				}
				e.u32(0x0) += 0x6400;
				return;
			}
			if (a.u8(0x4) == 0x22) {
				// Comprobado solo con estados retocados.
				if (tick <= (date + a.u8(0xF) + 0x12) << 8) {
					e.u32(0x0) += 0x5000;
					return;
				}
				break;
			}
		}
		// The letter 0x21 to the player (0x4E8560..0x4E8613) with the counter
		// c (1..8): [+0x5] [+0x8]·c % the towns, u16 [+0x8] [+0xA]·c % the
		// first names [0x6DDB70], [+0xA] [+0xC]·c % the family names
		// [0x6DDB74], [+0xC] 1000 · ((tick & 7) · [+0x8] % 6 + 3), [+0xE] the
		// day of the year + 14.
		uint8_t& counter = e.u8(0xE);
		++counter;
		if (counter > 8) {
			counter = 1;
		}
		const uint32_t c = counter;
		Letter letter(0x21);
		letter.entry.u8(5) = uint8_t((uint32_t(e.u16(0x8)) * c) % g.u32(global::kWorldTownCount));
		letter.entry.u16(8) = uint16_t((uint32_t(e.u16(0xA)) * c) % g.u16(0x6DDB70));
		letter.entry.u16(0xA) = uint16_t((uint32_t(e.u16(0xC)) * c) % g.u16(0x6DDB74));
		letter.entry.u16(0xC) = uint16_t(1000 * (((tick & 7) * uint32_t(e.u16(0x8))) % 6 + 3));
		letter.entry.u16(0xE) = uint16_t(g.u32(global::kWorldDayOfYear) + 0xE);
		send_letter(save, int32_t(i), letter);
	}
	const uint32_t year_start = uint32_t(g.u16(global::kWorldYear)) * 365 * 256;
	if (year_start > e.u32(0x0)) {
		e.u32(0x0) = year_start;  // Comprobado solo con estados retocados.
	} else {
		e.u32(0x0) += 0x5A00;
	}
}

}  // namespace events_big_detail

using namespace events_big_detail;

// Lo que hace el despachador (0x4D85C0) con los tipos de este grupo.
int events_big(SaveGame& save, uint16_t event, uint16_t type) {
	GlobalMemory& g = save.globals;
	switch (type) {
	case 0x2C: {
		Event& e = save.events[event];
		if (g.u32(global::kWorldTick) - e.u32(0x10) >= 0x5D00) {
			fn_4dea20(save, event);
			e.u32(0x10) = g.u32(global::kWorldTick);
		}
		if (g.u32(kOperations2C) == 0) {
			fn_4deba0(save);
		}
		e.u32(0x0) += e.u32(0xC);
		return 1;
	}
	case 0x05:
		return fn_4e5b84(save, event) ? 1 : 0;
	case 0x16:
		fn_4e8234(save, event);
		return 1;
	default:
		return -1;
	}
}

}  // namespace p3::data::tick
