// La cola de eventos (events.cpp): los tipos 0x02 (0x4DBB00, elección del
// alcalde 0x528E60), 0x0A y 0x36 (subastas, 0x4E2CD4), 0x20 (0x4DBBD0,
// elección del alderman) y 0x21 (0x4DC1F0, votación de la Hansa).
//
// Evento 0x02 (uno por ciudad, una vez al año; se recoloca siempre):
//   [+0x8] u16 día del año de la elección (>= 365: se saca del tick)
//   [+0xE] u8 ciudad   [+0xF] u8 día del mes y [+0x10] u8 mes (0x5311D0)
// Campos de la ciudad que usa la elección:
//   [0x6D4 + j], [0x6D8 + j] u8 nombre y apellido de los 4 candidatos que no
//   son comerciantes (los «ciudadanos» 0xFC..0xFF: j = ~candidato & 3)
//   [0x6DC + j] u8 comerciantes que apoyan a un candidato (3 votos cada uno;
//   0xFF tras contarlo; el alderman los borra todos)
//   [0x6E0 + j] u8 título y [0x6E4 + j] u8 rango de los ciudadanos (5, 5, 4,
//   4; 6 el que es alcalde y 7 si además es el alderman)
//   [0x6F1] u8 alcalde (comerciante o ciudadano)   [0x6F3] u8 reelecciones (hasta 250)
//
// Evento 0x20 (la elección del alderman, una vez al año). El byte bajo del
// tick es la fase: 0 la prepara (candidatos y votos por repartir) y sube el
// tick 10; hasta 0xBF se reparte un voto por tick al candidato tick & 3 si es
// el que más tiene por repartir; de 0xC0 a 0xFB, al candidato tick & 3; en
// 0xFC se cuenta y se pasa al año siguiente.
//   [+0x8 + k] u8 candidato k: comerciante | 0x80 o ciudad (la de su alcalde)
//   [+0xC + k] u8 votos   [+0x10 + k] u8 votos por repartir
//   [+0x14] u8 un bit por jugador humano que vota (rango >= 4 en su ciudad)
//   [+0x15] u8, [+0x16] u8 apoyos de comerciantes ([0x6DC] de las ciudades)
//   [+0x17] u8 ciudad del alderman
// Globales: [0x6DE52D] u8 el alderman (comerciante o ciudadano) y [0x6DE52E] u8 su ciudad.
//
// Evento 0x21 (una propuesta en la dieta de la Hansa). Fases como en el 0x20:
// 0 reparte los votos, hasta 0xE7 un voto por tick, 0xE8 cuenta lo que queda
// y después se decide; si sale que sí, según [+0xE]: 0, 1 y 2 cambian el
// evento por uno 0x34, 0x24 o 0x23 al día siguiente; 3, 4 y 5 hacen pagar a
// la ciudad 160000, 120000 u 80000 y lo reparten entre las demás.
//   [+0x9] u8 ciudad   [+0xB] u8, [+0xE] u8 propuesta   [+0xF] u8 apoyos
//   [+0x10], [+0x11] u8 votos por repartir (sí, no)   [+0x12], [+0x13] u8 sí, no
//   [+0x14], [+0x15] u8 abstenciones   [+0x16] u8 jugadores humanos que votan
//
// Eventos 0x0A (subasta de barcos) y 0x36 (de edificios: registro | 0x8000
// si es al 60 %). Fase en [+0x14]: 0 crea el barco que se subasta (solo el
// 0x0A), 1 espera [+0xE] ticks, 2 anuncia la subasta (una por ciudad: [0x9A2]
// de la ciudad es el evento que subasta), 3 pone el precio de salida, 4 a 6
// pujan los comerciantes de la IA (uno por tick), 7 vende y pasa al
// siguiente, 8 termina.
//   [+0x8], [+0xA], [+0xC] u16 lo que se subasta (barco o registro [0x75C])
//   [+0xE] u16 precio   [+0x10] u16 lo que sube cada puja   [+0x12] u16 ticks sin pujas
//   [+0x15] u8 ciudad   [+0x16] u8 jugadores avisados   [+0x17] u8 el que más puja
//   (empieza con el número de la ciudad)
//
// The letters (0x4D6530) are sent, with the texts of the elections' (the
// mayor's, 0x528E60, and the Alderman's, 0x5299F0). No se traducen los avisos (0x548CA0), las ventanas
// (0x469380, 0x469AF0, 0x42B4C0, 0x5499F0) ni la interfaz (0x5C6xxx).
//
// Comprobado con el oráculo (town_oracle.py --barcos --cpr --resto 4d85c0) en
// las 12 partidas durante 60 días (la 2, además, en su primera elección, el
// día 247) y con estados retocados para las ramas que no salen en ellas
// (alcaldes y alderman comerciantes, propuestas aprobadas, pujas de la IA por
// edificios, lotes de varios barcos o edificios...).

#include <cmath>
#include <cstring>

#include "p3data/mail.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {

// Traducciones de otras partes que se usan aquí.
namespace events_detail {
uint16_t fn_4d8cf0(SaveGame& save, uint32_t tick);
int16_t fn_4f73d0(const SaveGame& save, int merchant);
int16_t fn_4f9580(const SaveGame& save, int merchant, uint32_t town);
int32_t fn_4daf10(const SaveGame& save, uint32_t town, uint32_t record, bool flag);
}  // namespace events_detail
namespace merchants_detail {
uint8_t fn_4f9430(const SaveGame& save, int merchant);
void fn_4f75f0(SaveGame& save, int merchant, uint32_t town);
int32_t fn_52a7d0(const SaveGame& save, int ship);
}  // namespace merchants_detail
namespace events_towns_detail {
int fn_5308a0_create(SaveGame& save, uint32_t merchant, uint32_t town);
}  // namespace events_towns_detail

namespace {

constexpr uint32_t kEvents = 0x6DD73C;
constexpr uint32_t kEventBusy = kEvents + 0x4;  // u32 1 mientras 0x4D85C0 atiende la cola.
constexpr uint32_t kEventHead = kEvents + 0x8;  // u16
constexpr uint32_t kEventCount = kEvents + 0xC;  // u16

constexpr uint32_t kTownCount = global::kWorldTownCount;  // u32
constexpr uint32_t kMerchantCount = global::kWorldMerchantCount;  // u16
constexpr uint32_t kOfficeCount = global::kWorldOfficeCount;  // u16
constexpr uint32_t kWorldRecordCount = 0x6DE4A6;  // u16
constexpr uint32_t kTick = global::kWorldTick;  // u32
constexpr uint32_t kYear = global::kWorldYear;  // u16
constexpr uint32_t kDayOfYear = global::kWorldDayOfYear;  // u16
constexpr uint32_t kShipCount = global::kShipCount;  // u16
constexpr uint32_t kPlayer = global::kPlayer;  // u32
constexpr uint32_t kWorldFlag8A = 0x6DE52A;  // u8 (del mundo, [+0x8A]): sin él, la elección se avisa.
constexpr uint32_t kAlderman = 0x6DE52D;  // u8 el alderman (del mundo, [+0x8D]).
constexpr uint32_t kAldermanTown = 0x6DE52E;  // u8 su ciudad ([+0x8E]).
constexpr uint32_t kTownIds = 0x6DE4B8;  // u8 por ciudad: su número de StadtDaten.ini.
constexpr uint32_t kTownTable = 0x6DDB90;  // 0x34 bytes por número de ciudad (StadtDaten.ini).
constexpr uint32_t kShipNameCount = 0x6DDB78;  // u16 nombres de barco (Namen_eng.ini).
// En el objeto de los barcos (0x6DD7A0).
constexpr uint32_t kFreeShip = global::kShips + 0xE2;  // u16 primer hueco libre.
constexpr uint32_t kDockedHead = global::kShips + 0xE8;  // u16 primero de la lista de barcos en puerto.
constexpr uint32_t kShipNameIndex = global::kShips + 0xEE;  // u16 último nombre de barco.
constexpr uint32_t kShipNameStep = global::kShips + 0xF0;  // u16 paso entre nombres.
constexpr uint32_t kConvoyCount = global::kShips + 0xF6;  // u16
constexpr uint32_t kTradeRouteCount = 0x6DD72A;  // u16 del objeto 0x6DD728.

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

MerchantMemory& merchant_of(SaveGame& save, uint32_t i) {
	return save.merchants[i].memory;
}

ShipMemory& ship_of(SaveGame& save, uint32_t i) {
	return save.ships[i].memory;
}

// 0x530400: la oficina `index`, o -1 si no hay tantas.
int office_at(const SaveGame& save, uint32_t index) {
	return index < save.globals.u16(kOfficeCount) ? int(index) : -1;
}

// __ftol (0x639C28, fistp qword): lo que queda en eax. Fuera del rango de
// 64 bits (o NaN) la FPU da el entero indefinido 0x8000000000000000: eax = 0.
int32_t ftol32(double v) {
	if (!(v > -9.2e18 && v < 9.2e18)) {
		return 0;
	}
	return int32_t(uint32_t(uint64_t(int64_t(v))));
}

// Puntuación de los cuatro ciudadanos que se presentan a alcalde (0x6737B4,
// por ~candidato & 3).
constexpr uint8_t kCitizenScore[4] = {0x3B, 0x41, 0x46, 0x50};
// [0x6737B9]: los votos que reparte en la elección del alderman la ciudad
// cuyo alcalde es un ciudadano, según su población.
constexpr int32_t kAldermanTownVotes = 0xC8;

// 0x672D78: día del año en que empieza cada mes (el 13 es el final del año).
constexpr uint16_t kMonthStart[14] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365, 0};

// 0x673838: tamaño de un barco nuevo (en cargas) por tipo (4 bytes) y nivel
// del astillero de la ciudad ([0x824 + tipo]); el byte 3 es el máximo.
constexpr uint8_t kShipSize[16] = {0x0F, 0x13, 0x17, 0x19, 0x1C, 0x1F, 0x22, 0x23, 0x2D, 0x30, 0x34, 0x37, 0x37, 0x3B,
	0x41, 0x46};

// 0x672D08: huecos de artillería de cada tipo (& 3) y nivel (3 por tipo): 2
// bits por grupo de 4 huecos ([+0x13C + 4·g]).
constexpr uint16_t kArtillerySlots[12] = {0, 5, 29, 0, 5, 61, 240, 252, 1021, 252, 1021, 4095};

// Valor de un barco en la subasta (0x4DA860), por tipo (0, 1, 2, 3 o más):
// cargas por nivel (0x66E030, 0x66E048, 0x66E060, 0x66E078), umbrales de
// tamaño, precio base por debajo, entre y por encima de ellos (0x66DECC...) y
// precio por nivel (0x66E044, 0x66E05C, 0x66E074, 0x66E08C).
struct ShipValue {
	int32_t loads_per_level;
	int32_t small_below;  // Por debajo: small.
	int32_t large_above;  // Por encima: large.
	int32_t small, middle, large;
	int32_t per_level;
};
constexpr ShipValue kShipValue[4] = {
	{3, 0x13, 0x16, 0x2C96, 0x2F2A, 0x31F0, 0xA8C},
	{5, 0x1F, 0x21, 0x5F82, 0x61B2, 0x66B2, 0x1194},
	{8, 0x30, 0x33, 0x5718, 0x574A, 0x5B96, 0x1C20},
	{10, 0x3B, 0x40, 0x868A, 0x8777, 0x8F24, 0x2328},
};
constexpr double kShipValueFactor = 0x1.6666666666666p-1;  // 0x672830 (0,7)
constexpr double kHalf = 0.5;  // 0x66A7D0
constexpr double kThird = 0x1.5555551C112DAp-2;  // 0x672DC8 (casi 1/3)

// 0x672C2C: categoría de cada tipo de edificio (registros [0x75C]) para la
// puja de la IA: 1 empresa (mira su instalación [0x848 + 16·tipo]), 2 la de
// [0x2CC] de la ciudad, 0xFF ninguna.
constexpr uint8_t kBuildingCategory[64] = {0x06, 0x06, 0x06, 0x14, 0x09, 0x02, 0x03, 0x0C, 0x05, 0x00, 0x01, 0x0B,
	0x08, 0x04, 0x10, 0x0E, 0x07, 0x12, 0x13, 0x0F, 0x11, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x0C, 0x0C, 0x04, 0x00,
	0x10, 0xFF, 0xFF, 0x04, 0xFF, 0x0E, 0x0B, 0xFF, 0xFF, 0xFF, 0x0B, 0x0B, 0x0B, 0xFF, 0x00, 0x00, 0x00, 0x14, 0x03,
	0x0F, 0x0B, 0x13, 0x0C, 0x0C, 0x10, 0x0C, 0x11, 0x04, 0x11, 0x00, 0x0B, 0x0B, 0x10};
// 0x672A18: plazas de cada clase de casa (tipos 0x15..0x17, 0x18..0x1A, 0x1B..0x1D).
constexpr uint16_t kHousePlaces[3] = {80, 140, 280};

// Un u16 de la memoria de las ciudades seguidas (el juego lee [0x848 +
// 16·tipo] también con tipos que se salen de la ciudad). 0 fuera del array.
uint16_t town_array_u16(const SaveGame& save, uint32_t town, uint32_t offset) {
	const uint32_t at = town * uint32_t(TownMemory::kSize) + offset;
	const uint32_t t = at / uint32_t(TownMemory::kSize);
	const uint32_t o = at % uint32_t(TownMemory::kSize);
	if (t >= save.towns.size() || o + 2 > TownMemory::kSize) {
		return 0;
	}
	return save.towns[t].memory.u16(o);
}

}  // namespace

// Lo que se expone para las sondas (tools/oracle/probes_events_mayor.cpp).
namespace events_mayor_detail {

// --- Evento 0x02: la elección del alcalde ---

// 0x5311D0 (sobre el mundo) con la fecha de un evento: el día del mes en
// `day` y el mes (desde 1) en `month` del día del tick (año en un local).
void fn_5311d0(uint32_t tick, uint8_t& day, uint8_t& month) {
	const int32_t days = int32_t(tick) >> 8;
	const int32_t year = days / 365;
	const int32_t d = int32_t(uint32_t(days) - uint32_t(uint16_t(year)) * 365u);
	month = 0;
	if (int32_t(kMonthStart[0]) <= d) {
		do {
			++month;
		} while (month < 13 && int32_t(kMonthStart[month]) <= d);
	}
	// Con d < 0, el u16 de antes de la tabla (0x672D76: 0x918).
	const uint8_t start = month > 0 ? uint8_t(kMonthStart[month - 1]) : 0x18;
	day = uint8_t(uint8_t(d) - start + 1);
}

// The same with its third output, the year (a u16).
void fn_5311d0(uint32_t tick, uint8_t& day, uint8_t& month, uint16_t& year) {
	fn_5311d0(tick, day, month);
	year = uint16_t((int32_t(tick) >> 8) / 365);
}

// 0x4F9360 (sobre el comerciante): ¿se presenta a alcalde? El jugador humano
// según su [+0x118]; los de la IA, si no tienen [+0x8] & 4 (y lo apuntan en
// [+0x118]).
uint32_t fn_4f9360(SaveGame& save, int merchant) {
	MerchantMemory& m = merchant_of(save, uint32_t(merchant));
	if (m.u16(0x8) == 0) {
		return m.u32(0x118);
	}
	if (m.u8(0x8) & 4) {
		m.u32(0x118) = 0;
		return 0;
	}
	m.u32(0x118) = 1;
	return 1;
}

// 0x528CF0 (sobre la ciudad): los 4 candidatos a alcalde en `out`: los
// comerciantes que se presentan (0x4F9360), con derechos en la ciudad
// ([+0x468]), naturales de ella y con rango 5 o más, ordenados por su
// reputación en la ciudad (0x4F9580), por delante de los ciudadanos 0xFC..0xFF
// que no superan (80, 70, 65, 59). El juego llena también los títulos,
// rangos y nombres de cada uno ([+0x4..+0x13] de la salida), que solo usa el
// aviso: no se traducen.
void fn_528cf0(SaveGame& save, int town, uint8_t out[4]) {
	const TownMemory& t = town_of(save, uint32_t(town));
	int32_t best[5] = {};
	for (uint32_t k = 0; k < 4; ++k) {
		best[3 - k] = kCitizenScore[k];
		out[3 - k] = uint8_t(0xFF - k);
	}
	for (uint32_t m = merchant_count(save); m > 0;) {
		--m;
		if (fn_4f9360(save, int(m)) == 0) {
			continue;
		}
		const MerchantMemory& mm = save.merchants[m].memory;
		const uint8_t here = t.u8(0x2C0);
		if ((mm.u32(0x468) & (1u << (here & 31))) == 0 || mm.u8(0x19) != here ||
			mm.u8(0x39C + mm.u8(0x19)) < 5) {
			continue;
		}
		const int32_t reputation = events_detail::fn_4f9580(save, int(m), here);
		for (int k = 3; k >= 0; --k) {
			if (reputation <= best[k]) {
				break;
			}
			best[k + 1] = best[k];
			best[k] = reputation;
			if (k < 3) {
				out[k + 1] = out[k];
			}
			out[k] = uint8_t(m);
		}
	}
}

// 0x4F6420 (sobre el comerciante): su nombre de pila (0x512A60) en [+0xE8],
// de la lista de mujeres si [+0x1A] es 0 (bit 7 de [+0x1C]). El puntero no
// existe en p3vita: solo se lleva el bit.
void fn_4f6420(SaveGame& save, int merchant) {
	MerchantMemory& m = merchant_of(save, uint32_t(merchant));
	if (m.u8(0x1A) != 0) {
		m.u8(0x1C) &= 0x7F;
	} else {
		m.u8(0x1C) |= 0x80;
	}
}

// 0x529F20 (sobre la ciudad): rangos de los ciudadanos candidatos ([0x6E4 + j]
// = 5, 5, 4, 4) y, si el alcalde es uno de ellos, 6 (7 si es el alderman).
void fn_529f20(SaveGame& save, int town) {
	TownMemory& t = town_of(save, uint32_t(town));
	const uint8_t mayor = t.u8(0x6F1);
	t.u8(0x6E4) = 5;
	t.u8(0x6E5) = 5;
	t.u8(0x6E6) = 4;
	t.u8(0x6E7) = 4;
	if (mayor < merchant_count(save)) {
		return;
	}
	const uint32_t j = uint32_t(~mayor) & 3;
	if (save.globals.u8(kAldermanTown) == t.u8(0x2C0) && save.globals.u8(kAlderman) == j) {
		t.u8(0x6E4 + j) = 7;
	} else {
		t.u8(0x6E4 + j) = 6;
	}
}

// 0x528E60 (sobre la ciudad): elige alcalde [0x6F1]. Cada candidato (0x528CF0)
// recibe 3 votos por cada comerciante que lo apoya ([0x6DC + j]) y los demás
// de 120 en proporción a su puntuación (si no hay comerciantes, la de los
// ciudadanos con algo según el día); gana el primero con más votos. Si
// repite, cuenta la reelección [0x6F3] y, a la primera de un comerciante,
// encola un evento 0x38. Después, los rangos (0x4F75F0 del alcalde saliente y
// del nuevo) y los de los ciudadanos de todas las ciudades (0x529F20).
void fn_528e60(SaveGame& save, int town) {
	GlobalMemory& g = save.globals;
	uint8_t candidates[4];
	fn_528cf0(save, town, candidates);
	TownMemory& t = town_of(save, uint32_t(town));
	const uint32_t count = merchant_count(save);
	int32_t score[4];
	int32_t total = 0;
	bool merchants = false;
	for (uint32_t k = 0; k < 4; ++k) {
		const uint8_t c = candidates[k];
		if (c < count) {
			score[k] = events_detail::fn_4f9580(save, c, t.u8(0x2C0));
			merchants = true;
		} else {
			score[k] = kCitizenScore[uint32_t(~c) & 3];
		}
		total = int32_t(uint32_t(total) + uint32_t(score[k]));
	}
	if (!merchants) {
		const uint32_t day = g.u32(kTick) >> 8;
		for (uint32_t k = 0; k < 4; ++k) {
			int32_t extra;
			if (score[k] == 0x50) {
				extra = 0x1F;
			} else {
				extra = (int32_t(day * (k + 7)) % 0x43) & 0x1F;
			}
			score[k] += extra;
			total += extra;
		}
	}
	int32_t votes[4] = {};
	int32_t pool = 0x78;
	for (uint32_t j = 0; j < 4; ++j) {
		const uint8_t supporter = t.u8(0x6DC + j);
		if (supporter >= count) {
			continue;
		}
		for (uint32_t k = 0; k < 4; ++k) {
			if (candidates[k] == supporter) {
				votes[k] += 3;
				t.u8(0x6DC + j) = 0xFF;
				pool -= 3;
				break;
			}
		}
	}
	if (total == 0) {
		return;  // El juego fallaría (división por cero).
	}
	for (uint32_t k = 0; k < 4; ++k) {
		votes[k] += int32_t(uint32_t(score[k]) * uint32_t(pool)) / total;
	}
	int32_t best = votes[0];
	uint8_t winner = candidates[0];
	for (uint32_t k = 1; k < 4; ++k) {
		if (best < votes[k]) {
			winner = candidates[k];
			best = votes[k];
		}
	}
	if (winner == t.u8(0x6F1)) {
		const uint8_t terms = uint8_t(t.u8(0x6F3) + 1);
		t.u8(0x6F3) = terms > 0xFA ? 0xFA : terms;
		if (t.u8(0x6F3) == 1 && winner < count) {
			// En las partidas nunca sale un comerciante: comprobado con estados retocados.
			const uint32_t sum = uint32_t(votes[0]) + uint32_t(votes[1]) + uint32_t(votes[2]) + uint32_t(votes[3]);
			const uint32_t wait = ((uint32_t(best) * 93u) << 8) / (sum + 1);
			const uint16_t e = events_detail::fn_4d8cf0(save, g.u32(kTick) - wait + 0xBA00);
			Event& ev = save.events[e];
			ev.u16(0x6) = 0x38;
			ev.u8(0x8) = t.u8(0x2C0);
			ev.u8(0x9) = t.u8(0x6F1);
		}
	} else {
		t.u8(0x6F3) = 0;
	}
	const uint8_t outgoing = t.u8(0x6F1);
	t.u8(0x6F1) = winner;
	if (outgoing < count) {
		merchants_detail::fn_4f75f0(save, outgoing, t.u8(0x2C0));
	}
	for (uint32_t i = 0; i < town_count(save); ++i) {
		fn_529f20(save, int(i));
	}
	const uint8_t mayor = t.u8(0x6F1);
	if (mayor < count) {
		// Con [+0x37] = 0 y reputación >= 120, 0x4F75F0 llama a 0x5326E0
		// (candidatura a Gobernador, alderman.cpp: encola las operaciones
		// 0x86 y 0x85).
		merchants_detail::fn_4f75f0(save, mayor, t.u8(0x2C0));
		// Al jugador humano, la carta (0x548CA0) y, si es el jugador, la
		// ventana (0x469380 y 0x469AF0): no se traducen.
	}
	if (g.u8(kWorldFlag8A) != 0) {
		return;
	}
	// The election's letter 0x47 (0x529237..0x5294F7) to each human merchant
	// whose home town it is: [+0x5] the town, its buffer the text «\cLord
	// Mayor's election in <town>», the votes of each candidate («\l%s %s:\n\c%u
	// votes\n», his first and family names) and «Therefore, <the mayor> has
	// been elected as Lord Mayor.». A merchant's first name is the one 0x4F6420
	// keeps in his [+0xE8] (the game calls it on the mayor, which changes the
	// bit 7 of his [+0x1C]); a citizen's, from the town's [+0x6D4], [+0x6D8].
	// The town's name comes from the table by number with the index (a game
	// bug, letter_town_name).
	auto names = [&](uint8_t c, uint8_t& first, uint8_t& family) {
		if (c < count) {
			first = merchant_of(save, c).u8(0x1C);
			family = merchant_of(save, c).u8(0x1D);
		} else {
			const uint32_t j = uint8_t(~c) & 3;
			first = t.u8(0x6D4 + j);
			family = t.u8(0x6D8 + j);
		}
	};
	std::string text = letter_text(GameText::MayorElectionIn) + letter_town_name(save, t.u8(0x2C0)) +
		letter_text(GameText::MayorElectionVotes);
	for (uint32_t k = 0; k < 4; ++k) {
		uint8_t first = 0;
		uint8_t family = 0;
		names(candidates[k], first, family);
		text += GameTexts::sprintf_raw(letter_text(GameText::MayorElectionCandidate),
			{game_first_name(save, first), game_family_name(save, family), std::to_string(uint32_t(votes[k]))});
	}
	text += letter_text(GameText::MayorElectionTherefore);
	if (mayor < count) {
		fn_4f6420(save, mayor);
	}
	uint8_t first = 0;
	uint8_t family = 0;
	names(mayor, first, family);
	text += game_first_name(save, first) + " " + game_family_name(save, family) +
		letter_text(GameText::MayorElectionElected);
	text += '\0';
	Letter letter(0x47);
	letter.entry.u8(5) = t.u8(0x2C0);
	letter.entry.u32(0xC) = uint32_t(text.size());
	letter.buffer.assign(text.begin(), text.end());
	for (uint32_t i = 0; i < count; ++i) {
		const MerchantMemory& m = merchant_of(save, i);
		if (m.u16(8) == 0 && m.u8(0x19) == t.u8(0x2C0)) {
			send_letter(save, int32_t(i), letter);
		}
	}
	// The news 0x55 to the chronicle (0x529523..0x5295BD): [+0x5] the town;
	// u16 [+0x8], [+0xA] the mayor's first and family names, [+0xC] his
	// votes, [+0xE] all the votes.
	Letter news(0x55);
	news.entry.u8(5) = t.u8(0x2C0);
	news.entry.u16(8) = first;
	news.entry.u16(0xA) = family;
	news.entry.u16(0xC) = uint16_t(best);
	news.entry.u16(0xE) = uint16_t(votes[0] + votes[1] + votes[2] + votes[3]);
	send_letter(save, kLetterToChronicle, news);
}

// 0x4DBB00 (evento 0x02): el día de la elección ([+0x8], con la fecha en
// [+0xF] y [+0x10] si hay que sacarla del tick) elige alcalde en la ciudad
// [+0xE] (0x528E60, con la cola libre) y deja el evento en ese día del año
// siguiente (tick con el byte bajo 0xFA).
void fn_4dbb00(SaveGame& save) {
	GlobalMemory& g = save.globals;
	Event* e = &save.events[g.u16(kEventHead)];
	if (e->u16(0x8) >= 0x16D) {
		e->u16(0x8) = uint16_t((e->u32(0) >> 8) % 0x16D);
		fn_5311d0(e->u32(0), e->u8(0xF), e->u8(0x10));
	}
	uint32_t year;
	if (g.u16(kDayOfYear) < e->u16(0x8)) {
		year = g.u16(kYear);
	} else {
		if (g.u16(kDayOfYear) == e->u16(0x8)) {
			g.u32(kEventBusy) = 0;
			if (e->u8(0xE) < save.towns.size()) {
				fn_528e60(save, e->u8(0xE));
			}
			g.u32(kEventBusy) = 1;
			e = &save.events[g.u16(kEventHead)];
		}
		year = uint32_t(g.u16(kYear)) + 1;
	}
	e->u32(0) = ((year * 365 + e->u16(0x8)) << 8) | 0xFA;
}

// --- Evento 0x20: la elección del alderman ---

// 0x529700 (cdecl): los 4 candidatos a alderman: el actual (comerciante, o
// la ciudad de un ciudadano, con su reputación o 200) y las ciudades con
// [0x2C8] & 0x40 cuyo alcalde es un ciudadano (200; a igualdad, por
// población) y los comerciantes de esas ciudades con [+0x37] >= 4, por su
// reputación (0x4F73D0). `town[k]` es la ciudad del candidato y `merchant[k]`
// el comerciante o 0xFF. El juego da también el título, el rango y el nombre
// de cada uno, que la simulación no usa: no se traducen.
void fn_529700(const SaveGame& save, uint8_t town[4], uint8_t merchant[4]) {
	const GlobalMemory& g = save.globals;
	const uint32_t count = merchant_count(save);
	uint32_t towns[5], merchants[5];
	int32_t reputation[5], citizens[5];
	uint32_t first = 0;
	if (g.u8(kAldermanTown) < town_count(save)) {
		towns[0] = g.u8(kAldermanTown);
		const uint8_t alderman = g.u8(kAlderman);
		if (alderman < count) {
			reputation[0] = events_detail::fn_4f73d0(save, alderman);
			merchants[0] = alderman;
		} else {
			reputation[0] = 0xC8;
			merchants[0] = 0xFF;
		}
		citizens[0] = 0xFFFFFF;
		first = 1;
	}
	for (uint32_t k = first; k < 5; ++k) {
		towns[k] = 0xFF;
		reputation[k] = 0;
		citizens[k] = 0;
		merchants[k] = 0xFF;
	}
	auto shift = [&](int k) {
		reputation[k + 1] = reputation[k];
		towns[k + 1] = towns[k];
		citizens[k + 1] = citizens[k];
		merchants[k + 1] = merchants[k];
	};
	for (uint32_t i = 0; i < town_count(save); ++i) {
		const TownMemory& t = save.towns[i].memory;
		if ((t.u32(0x2C8) & 0x40) == 0 || i == g.u8(kAldermanTown) || t.u8(0x6F1) < count) {
			continue;
		}
		const int32_t people = t.s32(0x2D4);
		for (int k = 3; k >= 0; --k) {
			if (reputation[k] > 0xC8 || (reputation[k] == 0xC8 && people <= citizens[k])) {
				break;
			}
			shift(k);
			towns[k] = i;
			reputation[k] = 0xC8;
			citizens[k] = people;
			merchants[k] = 0xFF;
		}
	}
	for (uint32_t m = 0; m < count; ++m) {
		if (m == g.u8(kAlderman)) {
			continue;
		}
		const MerchantMemory& mm = save.merchants[m].memory;
		const uint32_t home = mm.u8(0x19);
		if ((save.towns[home].memory.u32(0x2C8) & 0x40) == 0 || mm.u8(0x37) < 4) {
			continue;
		}
		const int32_t r = events_detail::fn_4f73d0(save, int(m));
		for (int k = 3; k >= 0; --k) {
			if (r <= reputation[k]) {
				break;
			}
			shift(k);
			towns[k] = home;
			reputation[k] = r;
			citizens[k] = 0;
			merchants[k] = m;
		}
	}
	for (uint32_t k = 0; k < 4; ++k) {
		town[k] = uint8_t(towns[k]);
		merchant[k] = uint8_t(merchants[k]);
	}
}

// 0x5299F0 (cdecl, from 0x4DBBD0 with the event's candidates [+0x8..+0xB],
// their votes [+0xC..+0xF], the conference's town [+0x17] and the new
// Alderman's town [0x6DE52E]): the result of the Alderman's election. The
// letter 0x47 to each merchant: [+0x5] the conference's town, its buffer
// the text «\cHanseatic conference in <town>», each candidate's votes («\l%s
// %s from %s:\n\c%u votes\n»: a merchant's names and home town, bit 7 of the
// candidate, or the names of the citizen mayor of the candidate town) and
// «Therefore <the Alderman> has been elected.», ending with two 0 bytes
// (both counted); the towns' names from the table by number with the index
// (a game bug, letter_town_name). Then the news 0x57 to the chronicle:
// [+0x5] the conference's town; [+0x8] the Alderman's home town, [+0x9] his
// [+0x1A], [+0xA] his title (0x4F9430), [+0xB], [+0xC] his names, [+0xD]
// his rank at home; for a citizen, the Alderman's town, 1, and the
// councillor's [+0x6E0], [+0x6D4], [+0x6D8], [+0x6E4] there.
void fn_5299f0(SaveGame& save, const uint8_t candidates[4], const uint8_t votes[4], uint8_t town,
	uint8_t alderman_town) {
	const GlobalMemory& g = save.globals;
	const uint32_t count = merchant_count(save);
	std::string text = letter_text(GameText::ConferenceIn) + letter_town_name(save, town) +
		letter_text(GameText::ConferenceVotes);
	for (uint32_t k = 0; k < 4; ++k) {
		const uint8_t c = candidates[k];
		uint8_t first;
		uint8_t family;
		uint8_t home;
		if (c & 0x80) {
			const MerchantMemory& m = merchant_of(save, c & 0x7F);
			first = m.u8(0x1C);
			family = m.u8(0x1D);
			home = m.u8(0x19);
		} else {
			const TownMemory& t = town_of(save, c);
			const uint32_t j = uint8_t(~t.u8(0x6F1)) & 3;
			first = t.u8(0x6D4 + j);
			family = t.u8(0x6D8 + j);
			home = c;
		}
		text += GameTexts::sprintf_raw(letter_text(GameText::ConferenceCandidate),
			{game_first_name(save, first), game_family_name(save, family), letter_town_name(save, home),
				std::to_string(uint32_t(votes[k]))});
	}
	text += letter_text(GameText::ConferenceTherefore);
	const uint32_t alderman = g.u8(kAlderman);
	const TownMemory& at = town_of(save, alderman_town);
	const uint32_t j = uint8_t(0xFF - alderman) & 3;
	const bool merchant = alderman < count;
	const uint8_t first = merchant ? merchant_of(save, alderman).u8(0x1C) : at.u8(0x6D4 + j);
	const uint8_t family = merchant ? merchant_of(save, alderman).u8(0x1D) : at.u8(0x6D8 + j);
	text += game_first_name(save, first) + " " + game_family_name(save, family) +
		letter_text(GameText::ConferenceElected);
	text += '\0';
	text += '\0';
	Letter letter(0x47);
	letter.entry.u8(5) = town;
	letter.entry.u32(0xC) = uint32_t(text.size());
	letter.buffer.assign(text.begin(), text.end());
	send_letter(save, kLetterToEach, letter);
	Letter news(0x57);
	news.entry.u8(5) = town;
	if (merchant) {
		const MerchantMemory& m = merchant_of(save, alderman);
		news.entry.u8(8) = m.u8(0x19);
		news.entry.u8(9) = m.u8(0x1A);
		news.entry.u8(0xA) = merchants_detail::fn_4f9430(save, int(alderman));
		news.entry.u8(0xD) = m.u8(0x39C + m.u8(0x19));
	} else {
		news.entry.u8(8) = alderman_town;
		news.entry.u8(9) = 1;
		news.entry.u8(0xA) = at.u8(0x6E0 + j);
		news.entry.u8(0xD) = at.u8(0x6E4 + j);
	}
	news.entry.u8(0xB) = first;
	news.entry.u8(0xC) = family;
	send_letter(save, kLetterToChronicle, news);
}

// 0x4DBBD0 (evento 0x20): la elección del alderman, por fases según el byte
// bajo del tick (ver arriba). Al final, el ganador en [0x6DE52D] (su ciudad
// en [0x6DE52E]), los rangos del saliente y del nuevo (0x4F75F0) y los de los
// ciudadanos (0x529F20); el evento vuelve dentro de un año.
void fn_4dbbd0(SaveGame& save) {
	GlobalMemory& g = save.globals;
	Event& e = save.events[g.u16(kEventHead)];
	const uint32_t count = merchant_count(save);
	const uint32_t tick = e.u32(0);
	const uint32_t phase = tick & 0xFF;
	if (phase == 0) {
		e.u8(0x17) = g.u8(kAldermanTown);
		e.u8(0x14) = 0;
		uint8_t towns[4], merchants[4];
		fn_529700(save, towns, merchants);
		for (uint32_t k = 0; k < 4; ++k) {
			// El juego compara con <= el número de comerciantes.
			e.u8(0x8 + k) = merchants[k] <= count ? uint8_t(merchants[k] | 0x80) : towns[k];
			e.u8(0xC + k) = 0;
			e.u8(0x10 + k) = 0;
		}
		e.u8(0x16) = 0;
		for (uint32_t i = 0; i < town_count(save); ++i) {
			for (uint32_t j = 0; j < 4; ++j) {
				if (save.towns[i].memory.u8(0x6DC + j) < count) {
					++e.u8(0x16);
				}
			}
		}
		int32_t weight = e.u8(0x16);
		uint32_t humans = 0;
		for (uint32_t m = 0; m < count; ++m) {
			const MerchantMemory& mm = save.merchants[m].memory;
			if (mm.u16(0x8) != 0) {
				continue;
			}
			const int32_t rank = mm.u8(0x39C + mm.u8(0x19));
			if (rank >= 4) {
				e.u8(0x14) |= uint8_t(1u << (humans & 31));
				weight += rank < 6 ? 1 : rank == 6 ? 2 : 3;
				// The ticker line «Hanse Day in X» (0x4DBD14: kind 0xD, the Hanse town).
				send_ticker(save, m, 0xD, save.globals.u8(0x6DE52E));
			}
			++humans;
		}
		int32_t pool;
		if (weight < 0x26) {
			e.u8(0x15) = e.u8(0x16);
			pool = 0x26 - weight;
		} else {
			pool = 0;
			e.u8(0x15) = uint8_t(e.u8(0x16) - uint8_t(weight) + 0x26);
		}
		int32_t most = 0;
		int32_t least = 0x0FFFFFFF;
		for (uint32_t k = 0; k < 4; ++k) {
			const uint8_t c = e.u8(0x8 + k);
			if (c & 0x80) {
				continue;
			}
			const TownMemory& t = save.towns[c].memory;
			if (t.u8(0x6F1) < count) {
				continue;
			}
			const int32_t people = t.s32(0x2D4);
			if (most < people) {
				most = people;
			}
			if (least > people) {
				least = people;
			}
		}
		if (most < 0x40) {
			most = 0x40;  // Sin comprobar con el oráculo.
		}
		if (least + 1 >= most) {
			least = (most >> 1) - 1;  // Sin comprobar con el oráculo.
		} else if (least < 0x20) {
			least = 0x10;  // Sin comprobar con el oráculo.
		} else {
			least >>= 1;
		}
		int32_t weights[4];
		int32_t sum = 0;
		for (uint32_t k = 0; k < 4; ++k) {
			const uint8_t c = e.u8(0x8 + k);
			if (c & 0x80) {
				weights[k] = events_detail::fn_4f73d0(save, c & 0x7F);
			} else if (save.towns[c].memory.u8(0x6F1) < count) {
				weights[k] = kAldermanTownVotes >> 2;
			} else {
				const uint32_t above = uint32_t(save.towns[c].memory.s32(0x2D4)) - uint32_t(least);
				weights[k] = int32_t(above * uint32_t(kAldermanTownVotes)) / (most - least);
			}
			sum = int32_t(uint32_t(sum) + uint32_t(weights[k]));
		}
		if (sum == 0) {
			return;  // El juego fallaría (división por cero).
		}
		for (uint32_t k = 0; k < 4; ++k) {
			e.u8(0x10 + k) = uint8_t(int32_t(uint32_t(weights[k]) * uint32_t(pool)) / sum);
		}
		e.u32(0) = tick + 0xA;
		return;
	}
	if (phase < 0xFC) {
		const uint32_t k = phase & 3;
		if (phase < 0xC0) {
			for (uint32_t c = 0; c < 4; ++c) {
				if (e.u8(0x10 + k) < e.u8(0x10 + c)) {
					e.u32(0) = tick + 1;
					return;
				}
			}
		}
		if (e.u8(0x10 + k) > 0) {
			++e.u8(0xC + k);
			--e.u8(0x10 + k);
		}
		e.u32(0) = tick + 1;
		return;
	}
	for (uint32_t k = 0; k < 4; ++k) {
		e.u8(0xC + k) = uint8_t(e.u8(0xC + k) + e.u8(0x10 + k));
	}
	for (uint32_t i = 0; i < town_count(save); ++i) {
		save.towns[i].memory.u32(0x6DC) = 0xFFFFFFFF;
	}
	uint32_t best = 0;
	for (uint32_t k = 0; k < 4; ++k) {
		if (e.u8(0xC + best) < e.u8(0xC + k)) {
			best = k;
		}
	}
	const uint8_t c = e.u8(0x8 + best);
	uint32_t winner;
	if (c & 0x80) {
		winner = c & 0x7F;
		g.u8(kAldermanTown) = save.merchants[winner].memory.u8(0x19);
	} else {
		g.u8(kAldermanTown) = c;
		const uint8_t mayor = save.towns[c].memory.u8(0x6F1);
		winner = mayor < count ? g.u8(kAlderman) : mayor;
	}
	if (int32_t(winner) < int32_t(count) && save.merchants[winner].memory.u16(0x8) == 0) {
		// Al jugador humano, la carta (0x548CA0) y, si es el jugador, las
		// ventanas (0x469380, 0x469AF0) y 0x5499F0: no se traducen.
	}
	const uint8_t outgoing = g.u8(kAlderman);
	g.u8(kAlderman) = uint8_t(winner);
	if (outgoing < count) {
		for (uint32_t i = 0; i < town_count(save); ++i) {
			merchants_detail::fn_4f75f0(save, outgoing, i & 0xFF);
		}
	}
	const uint8_t alderman = g.u8(kAlderman);
	if (alderman < count) {
		merchants_detail::fn_4f75f0(save, alderman, 0);
	}
	for (uint32_t i = 0; i < town_count(save); ++i) {
		fn_529f20(save, int(i));
	}
	{
		const Event& head = save.events[g.u16(kEventHead)];
		uint8_t candidates[4];
		uint8_t votes[4];
		for (uint32_t k = 0; k < 4; ++k) {
			candidates[k] = head.u8(0x8 + k);
			votes[k] = head.u8(0xC + k);
		}
		fn_5299f0(save, candidates, votes, head.u8(0x17), g.u8(kAldermanTown));
	}
	Event& ev = save.events[g.u16(kEventHead)];
	ev.u32(0) += 0x16D00;
	ev.u8(0x17) = g.u8(kAldermanTown);
	ev.u8(0) = 0;
}

// --- Evento 0x21: la votación de la Hansa ---

// 0x4DC1F0 (evento 0x21, con su índice): ver arriba. Devuelve si el evento
// sigue (recolocado) o se quita.
uint32_t fn_4dc1f0(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	Event& e = save.events[g.u16(kEventHead)];
	const uint32_t count = merchant_count(save);
	const uint32_t phase = e.u32(0) & 0xFF;
	if (phase == 0) {
		const uint32_t town = e.u8(0x9);
		if (town < town_count(save) && (save.towns[town].memory.u32(0x2C8) & 0x40) != 0) {
			return 0;
		}
		e.u8(0x16) = 0;
		uint32_t weight = 0;
		uint32_t voters = 0;
		for (uint32_t m = 0; m < count; ++m) {
			const MerchantMemory& mm = save.merchants[m].memory;
			if (mm.u16(0x8) != 0) {
				continue;
			}
			const uint32_t rank = mm.u8(0x39C + mm.u8(0x19));
			if (rank < 4) {
				continue;
			}
			e.u8(0x16) |= uint8_t(1u << (voters & 31));
			// The ticker line «Hanse Day in X» (0x4DC2E8: kind 0xD, the Hanse town).
			send_ticker(save, m, 0xD, save.globals.u8(0x6DE52E));
			++voters;
			weight += rank < 6 ? 1 : rank == 6 ? 2 : 3;  // El 6, sin comprobar con el oráculo.
		}
		// Con algún jugador que vota, la ventana de la votación (0x469380 y
		// 0x469AF0, que el oráculo salta): no se traduce.
		e.u8(0xF) = 0;
		for (uint32_t i = 0; i < town_count(save); ++i) {
			for (uint32_t j = 0; j < 4; ++j) {
				if (save.towns[i].memory.u8(0x6DC + j) < count) {
					++e.u8(0xF);
				}
			}
		}
		const uint32_t n = weight + e.u8(0xF);
		const uint32_t pool = n < 0x1C ? 0x26 - n : 0xA;
		uint32_t stock = 0;
		for (uint32_t i = 0; i < town_count(save); ++i) {
			stock += save.towns[i].memory.u32(0x4);
		}
		int32_t yes = int32_t(e.u8(0xB)) * 2 - int32_t(e.u8(0xE)) * 2 + int32_t(stock) % 5 + 7;
		if (yes > 0x10) {
			yes = 0x10;
		}
		const uint8_t share = uint8_t((uint32_t(yes) * pool) >> 4);
		e.u8(0x11) = share;
		e.u8(0x10) = uint8_t(pool - share);
		e.u8(0x12) = 0;
		e.u8(0x13) = 0;
		e.u8(0x14) = 0;
		e.u8(0x15) = 0;
		e.u32(0) += 0xA;
		return 1;
	}
	if (phase < 0xE8) {
		const uint32_t b = phase & 1;
		const uint32_t limit = save.towns[0].memory.u32(0x4 + 4 * (phase & 0xF)) & (0xE8 - phase);
		if (int32_t(e.u8(0x10 + b)) > int32_t(limit)) {
			++e.u8(0x12 + b);
			--e.u8(0x10 + b);
		}
		if (e.u8(0x15) > 0) {
			// Sin comprobar con el oráculo: [+0x15] solo vale 0 tras la fase 0.
			++e.u8(0x14);
			--e.u8(0x15);
		}
		e.u32(0) += 1;
		return 1;
	}
	if (phase == 0xE8) {
		e.u8(0x13) = uint8_t(e.u8(0x13) + e.u8(0x11));
		e.u8(0x12) = uint8_t(e.u8(0x12) + e.u8(0x10));
		e.u8(0x14) = uint8_t(e.u8(0x14) + e.u8(0x15));
		e.u32(0) += 1;
		return 1;
	}
	// The result, the letter 0x0B to each merchant (0x4DC4F1..0x4DC53D): [+0x5]
	// the event's [+0x8]; u16 [+0x8] its [+0x9], [+0xA] [+0xE] << 8 + [+0xA],
	// [+0xC] the votes [+0x12], [+0xE] the votes [+0x13].
	{
		Letter letter(0x0B);
		letter.entry.u8(5) = e.u8(0x8);
		letter.entry.u16(8) = e.u8(0x9);
		letter.entry.u16(0xA) = uint16_t((e.u8(0xE) << 8) + e.u8(0xA));
		letter.entry.u16(0xC) = e.u8(0x12);
		letter.entry.u16(0xE) = e.u8(0x13);
		send_letter(save, kLetterToEach, letter);
	}
	if (e.u8(0x12) <= e.u8(0x13)) {
		return 0;
	}
	// En las partidas nunca sale que sí: comprobado con estados retocados
	// (propuestas 0 a 4; la 5 es la 4 con otra cantidad).
	const uint32_t town = e.u8(0x9);
	const uint32_t kind = e.u8(0xE);
	switch (kind) {
	case 0:
	case 1:
	case 2: {
		Event& ev = save.events[event];
		ev.u32(0) += 0x100;
		uint8_t value;
		if (kind == 0) {
			ev.u16(0x6) = 0x34;
			value = g.u8(kAldermanTown);
		} else {
			ev.u16(0x6) = kind == 1 ? 0x24 : 0x23;
			value = uint8_t(town);
		}
		ev.u8(0x14) = value;
		std::memset(ev.bytes.data() + 0x8, 0xFF, 8);
		ev.u32(0x10) = 0;
		ev.u8(0x15) = 0;
		ev.u16(0x16) = 0xFFFF;
		return 1;
	}
	case 3:
	case 4:
	case 5: {
		uint32_t amount = kind == 3 ? 160000 : kind == 4 ? 120000 : 80000;
		const uint32_t money = save.towns[town].memory.u32(0x700);
		if (amount > money) {
			amount = money;
		}
		fn_529f90(save, int(town), int32_t(amount), 0xA);
		uint32_t left = town_count(save);
		for (uint32_t i = 0; i < town_count(save); ++i) {
			if (i == town) {
				continue;
			}
			--left;
			const uint32_t share = (amount + uint32_t(int32_t(left) >> 1)) / left;
			fn_529f90(save, int(i), int32_t(share), 4);
			amount -= share;
		}
		return 0;
	}
	default:
		return 0;
	}
}

// --- Eventos 0x0A y 0x36: las subastas ---

// 0x517E00 (sobre el barco): lo deja como nuevo (sin dueño, listas ni carga,
// estado 0xFF, artillería a 7).
void fn_517e00(SaveGame& save, int ship) {
	SavedShip& ss = save.ships[size_t(ship)];
	ShipMemory& s = ss.memory;
	const uint32_t tick = save.globals.u32(kTick);
	s.u8(0x0) = 0xFF;
	s.u8(0x1) = 0;
	s.u16(0x2) = 0;
	for (size_t o : {0x4, 0x6, 0x8, 0xA, 0xC}) {
		s.u16(o) = 0xFFFF;
	}
	s.u8(0xE) = 0;
	s.u8(0xF) = 0;
	for (size_t o = 0x10; o <= 0x30; o += 4) {
		s.u32(o) = 0;
	}
	ss.route.reset();  // [+0x24]: sin ruta.
	s.u16(0x34) = 0;
	s.u8(0x36) = 0;
	s.u8(0x37) = 0;
	s.u8(0x38) = 1;
	s.u8(0x39) = 0xFF;
	s.u16(0x3A) = 0xFFFF;
	s.u16(0x3C) = 0xFFFF;
	s.u16(0x3E) = 0x3E8;
	s.u16(0x40) = 0;
	s.u16(0x42) = 0xFFFF;
	s.u32(0x44) = tick;
	s.u32(0x48) = 0;
	s.u32(0x4C) = tick;
	std::memset(s.bytes.data() + 0x50, 0xFF, 4);
	std::memset(s.bytes.data() + 0x54, 0, 0x60);
	std::memset(s.bytes.data() + 0xB4, 0, 0x60);
	for (size_t o = 0x114; o <= 0x12C; o += 4) {
		s.u32(o) = 0;
	}
	s.u16(0x130) = 0;
	s.u16(0x132) = 0xFFFF;
	s.u16(0x134) = 0xFF;
	s.u8(0x136) = 0;
	s.u16(0x138) = 0;
	s.u8(0x13A) = 0;
	s.u8(0x13B) = 0;
	std::memset(s.bytes.data() + 0x13C, 7, 0x18);
	s.u16(0x154) = 0;
	s.u8(0x156) = 0;
	s.u8(0x157) = 0;
	s.u8(0x158) = 0;
	s.u8(0x159) = 0xFF;
	s.u16(0x15A) = 0;
	s.u8(0x15C) = 0;
	s.u8(0x15D) = 0xFF;
	s.u16(0x15E) = 0xFFFF;
	std::memset(s.bytes.data() + 0x160, 0, 0x20);
}

// 0x516B10 (sobre el barco): lo pone en el puerto de la ciudad (posición de
// StadtDaten.ini, sin movimiento ni convoy).
void fn_516b10(SaveGame& save, int ship, uint32_t town) {
	SavedShip& ss = save.ships[size_t(ship)];
	ShipMemory& s = ss.memory;
	const GlobalMemory& g = save.globals;
	const uint32_t row = kTownTable + 0x34 * g.u8(kTownIds + (town & 0xFF));
	s.u16(0x8) = 0xFFFF;
	s.u16(0xA) = 0xFFFF;
	s.u16(0xC) = 0xFFFF;
	s.u32(0x1C) = 0;
	s.u32(0x20) = 0;
	s.u16(0x1E) = g.u16(row + 0x20);
	ss.route.reset();
	s.u32(0x24) = 0;
	s.u16(0x22) = g.u16(row + 0x24);
	s.u32(0x48) = 0;
	s.u32(0x28) = 0;
	s.u32(0x2C) = 0;
	s.u32(0x30) = 0;
	s.u16(0x34) = 0;
	s.u8(0x36) = 0;
	s.u8(0x37) = uint8_t(town);
	s.u8(0x38) = uint8_t(town);
	s.u8(0x39) = uint8_t(town);
	s.u16(0x3C) = 0xFFFF;
	s.u16(0x3A) = 0xFFFF;
	s.u16(0x40) = 0;
	s.u16(0x42) = 0xFFFF;
	s.u16(0x3E) = 0;
	s.u32(0x44) = g.u32(kTick);
	s.u32(0x50) = 0xFFFFFFFF;
}

// 0x507D60 (sobre el objeto de los barcos): un barco nuevo en un hueco libre
// ([0x6DD882]; si no queda, 0x40 huecos más) en el puerto de la ciudad, con
// el estado según `mode` (0 en el astillero [0x81C], 1 en puerto, 2 en
// subasta (0x0C), 3 y 4 fuera de las listas). Deja su índice en `out` y lo
// devuelve.
uint16_t fn_507d60(SaveGame& save, uint16_t& out, uint32_t town, uint32_t mode) {
	GlobalMemory& g = save.globals;
	if (g.u16(kFreeShip) >= g.u16(kShipCount)) {
		// Sin comprobar con el oráculo (en las partidas siempre hay huecos).
		// El juego copia los barcos a un array nuevo con 0x40 más; de los
		// nuevos solo inicializa estos campos (aquí, el resto a 0).
		const uint16_t old = g.u16(kShipCount);
		g.u16(kFreeShip) = old;
		g.u16(kShipCount) = uint16_t(old + 0x40);
		const uint16_t ships = g.u16(kShipCount);
		save.ships.resize(ships);
		for (uint32_t i = old; i < ships; ++i) {
			SavedShip& ss = save.ships[i];
			ss.memory = ShipMemory();
			ss.route.reset();
			ss.town_route = false;
			ShipMemory& s = ss.memory;
			s.u16(0x6) = uint16_t(i + 1);
			s.u16(0x8) = 0xFFFF;
			s.u16(0x4) = 0xFFFF;
			s.u16(0x134) = 0xFF;
		}
		// 0x45BF80 sobre *0x6CBDD8: interfaz.
	}
	const uint16_t index = g.u16(kFreeShip);
	g.u16(kFreeShip) = ship_of(save, index).u16(0x6);
	fn_517e00(save, index);
	ShipMemory& s = ship_of(save, index);
	s.u8(0x137) = 0;
	const uint32_t tick = g.u32(kTick);
	switch (mode & 0xFFFF) {
	case 0: {
		// Sin comprobar con el oráculo.
		s.u16(0x134) = 0xE;
		const uint16_t ships = g.u16(kShipCount);
		uint16_t* link = &town_of(save, town & 0xFFFF).u16(0x81C);
		while (*link < ships) {
			link = &ship_of(save, *link).u16(0x6);
		}
		*link = index;
		break;
	}
	case 1:
		// Sin comprobar con el oráculo.
		s.u16(0x134) = 0;
		s.u16(0x6) = g.u16(kDockedHead);
		g.u16(kDockedHead) = index;
		break;
	case 2:
		s.u16(0x134) = 0xC;
		s.u32(0x4C) = tick - (uint32_t(s.u8(0x4C)) << 8) - (uint32_t(index) << 8);
		s.u16(0x6) = g.u16(kDockedHead);
		g.u16(kDockedHead) = index;
		break;
	case 3:
		// Sin comprobar con el oráculo.
		s.u16(0x134) = 0x12;
		s.u32(0x4C) = tick - ((tick & 0xFF) << 10);
		s.u16(0x4) = 0xFFFF;
		s.u16(0x6) = 0xFFFF;
		break;
	case 4:
		// Sin comprobar con el oráculo.
		s.u16(0x134) = 0x15;
		s.u16(0x8) = 0xFFFF;
		s.u16(0x4) = 0xFFFF;
		s.u16(0x6) = 0xFFFF;
		break;
	default:
		break;
	}
	out = index;
	fn_516b10(save, index, town);
	ship_of(save, index).u8(0x13B) = uint8_t(town);
	return index;
}

// 0x52A8D0 (sobre la ciudad): tipo `type` y tamaño de un barco nuevo según el
// nivel del astillero de la ciudad para ese tipo ([0x824 + tipo]) y [0x828 +
// tipo]: capacidad (cargas · 2000) y casco (cargas · 2800).
void fn_52a8d0(SaveGame& save, int town, int ship, uint32_t type) {
	const TownMemory& t = town_of(save, uint32_t(town));
	ShipMemory& s = ship_of(save, uint32_t(ship));
	const uint32_t k = type & 0xFF;
	int32_t level = t.s8(0x824 + k);
	if (level < 0) {
		level = 0;  // Sin comprobar con el oráculo.
	}
	s.u8(0xE) = uint8_t(type);
	s.u8(0xF) = 0;
	const uint32_t at = uint32_t(level) + 4 * k;
	uint16_t loads = uint16_t((at < 16 ? kShipSize[at] : 0) + t.u8(0x828 + k));
	const uint16_t most = 4 * k + 3 < 16 ? kShipSize[4 * k + 3] : 0;
	if (loads > most) {
		loads = most;  // Sin comprobar con el oráculo.
	}
	s.s32(0x10) = int32_t(uint32_t(loads) * 2000);
	s.s32(0x14) = int32_t(uint32_t(loads) * 2800);
}

// 0x5181D0 (sobre el barco): los huecos de artillería que no permiten su tipo
// y nivel (0x672D08) quedan a 0xFF (6 en el par de los grandes). Con los
// barcos nuevos (todo a 7) solo se han comprobado los casos 0 y 2 sin el 6.
void fn_5181d0(SaveGame& save, int ship) {
	ShipMemory& s = ship_of(save, uint32_t(ship));
	const uint32_t at = (s.u8(0xE) & 3) * 3 + s.u8(0xF);
	uint32_t bits = at < 12 ? kArtillerySlots[at] : 0;
	auto clear = [&](size_t o) {
		const uint8_t v = s.u8(o);
		if (v > 1 && v != 5) {
			s.u8(o) = 0xFF;
		}
	};
	for (size_t o = 0x13C; o < 0x154; o += 4) {
		const uint32_t code = bits & 3;
		bits >>= 2;
		switch (code) {
		case 0:
			s.u8(o) = 7;
			s.u8(o + 1) = 7;
			s.u8(o + 2) = 7;
			s.u8(o + 3) = 7;
			break;
		case 1:
			clear(o);
			s.u8(o + 1) = 7;
			clear(o + 2);
			s.u8(o + 3) = 7;
			break;
		default:
			clear(o + 1);
			clear(o + 3);
			for (size_t p : {o, o + 2}) {
				const uint8_t v = s.u8(p);
				if (v > 1 && v != 5) {
					if (v < 6) {
						s.u8(p + 1) = 6;
					} else {
						s.u8(p) = 0xFF;
					}
				}
			}
			break;
		}
	}
}

// 0x509A50 (sobre el objeto de los barcos): nombre del barco: el siguiente
// de la lista ([0x6DD88E] + [0x6DD890], módulo 307, hasta que haya tantos) en
// [+0x15E] y su texto (31 caracteres como mucho) en [+0x160]; [+0x3C] |= 1.
void fn_509a50(SaveGame& save, int ship) {
	GlobalMemory& g = save.globals;
	ShipMemory& s = ship_of(save, uint32_t(ship));
	if (g.u16(kShipNameCount) == 0) {
		return;  // El juego no saldría del bucle.
	}
	uint16_t name;
	do {
		name = uint16_t(int32_t(uint16_t(g.u16(kShipNameIndex) + g.u16(kShipNameStep))) % 0x133);
		g.u16(kShipNameIndex) = name;
	} while (name >= g.u16(kShipNameCount));
	s.u16(0x15E) = name;
	size_t n = 0;
	if (name < save.ship_names.size()) {
		const std::string& text = save.ship_names[name];
		for (; n < text.size() && text[n] != '\0' && n + 1 < 0x20; ++n) {
			s.u8(0x160 + n) = uint8_t(text[n]);
		}
	}
	s.u8(0x160 + n) = 0;
	s.u8(0x3C) |= 1;
}

// 0x4DA860 (sobre la cola): precio de salida de un barco en subasta: el de su
// tipo según su tamaño y nivel, menos el daño del casco (300 por cada 2800),
// al 70 % y en centenas.
int32_t fn_4da860(const SaveGame& save, uint32_t ship) {
	const ShipMemory& s = save.ships[ship & 0xFFFF].memory;
	const uint8_t type = s.u8(0xE);
	const ShipValue& v = kShipValue[type < 3 ? type : 3];
	const int32_t level = s.u8(0xF);
	const int32_t size = s.s32(0x10) / 2000 + int32_t(uint32_t(level) * uint32_t(v.loads_per_level));
	int32_t price;
	if (size < v.small_below) {
		price = v.small;
	} else {
		price = size > v.large_above ? v.large : v.middle;
	}
	const int32_t per_level = int32_t(uint32_t(level) * uint32_t(v.per_level));
	const int64_t damage = int64_t(int32_t(uint32_t(s.s32(0x14)) - uint32_t(s.s32(0x18)))) * 300 / 2800;
	const int32_t value = int32_t(uint32_t(price) + uint32_t(per_level) - uint32_t(damage));
	const float f = float(value);
	const int32_t x = ftol32(double(f) * kShipValueFactor + kHalf);
	return x / 100 * 100;
}

// 0x4F4400 (sobre el comerciante): lo que puja la IA por un barco en la
// ciudad: nada el jugador humano, los de [+0x8] & 6 (o con la ciudad natal
// en [0x2C8] & 1) por los barcos grandes, ni fuera de su natal sin oficina;
// si no, el valor del barco (0x52A7D0) por un factor (/1024) según [+0x53C],
// [+0x470] (capacidad de sus barcos), [+0x546] y [+0x547].
int32_t fn_4f4400(const SaveGame& save, int merchant, uint32_t ship, uint32_t town) {
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const uint16_t kind = m.u16(0x8);
	if (kind == 0) {
		return 0;
	}
	const ShipMemory& s = save.ships[ship & 0xFFFF].memory;
	const uint8_t home = m.u8(0x19);
	if ((save.towns[home].memory.u8(0x2C8) & 1) || (kind & 6)) {
		if ((s.u8(0xE) & 2) == 2) {
			return 0;
		}
	}
	if (uint16_t(town) != home) {
		bool found = false;
		for (int o = office_at(save, m.u16(0xC)); o >= 0; o = office_at(save, save.offices[size_t(o)].memory.u16(0x2C8))) {
			if (save.offices[size_t(o)].memory.u8(0x2C6) == uint16_t(town)) {
				found = true;
				break;
			}
		}
		if (!found) {
			return 0;
		}
	}
	const int32_t wanted = m.s32(0x53C);
	if (wanted <= 0) {
		return 0;
	}
	const int32_t capacity = m.s32(0x470);
	int32_t factor;
	if (capacity <= 0) {
		factor = 0x4B0;  // Sin comprobar con el oráculo.
	} else if (wanted > capacity) {
		factor = int32_t(uint32_t(m.u8(0x546)) * 2u * uint32_t(wanted)) / capacity + 0x320;
		if (factor > 0x4E2) {
			factor = 0x4E2;
		}
	} else {
		factor = int32_t(uint32_t(0x1F4 - m.u8(0x547)) * uint32_t(wanted)) / capacity + m.u8(0x546) + 0x190;
	}
	return int32_t(uint32_t(merchants_detail::fn_52a7d0(save, int(ship & 0xFFFF))) * uint32_t(factor)) >> 10;
}

// 0x4F4550 (sobre el comerciante, con su índice `me`): lo que puja la IA por
// un edificio de la ciudad: nada el jugador humano, los de [+0x8] & 6, los
// edificios en obras o suyos, ni fuera de su natal sin oficina; si no, el
// doble de su precio (0x4DAF10) ajustado a su dinero y, para las casas, por
// los inquilinos de la ciudad ([0x2D8].. entre las plazas [0x2F8].. y las de
// la lista [0x762]); para los demás, si la ciudad tiene la empresa o su
// [0x2CC], por lo que producen las empresas de la IA en la ciudad.
int32_t fn_4f4550(const SaveGame& save, int merchant, uint32_t record, uint32_t town, uint32_t me) {
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const uint16_t kind = m.u16(0x8);
	if (kind == 0 || (kind & 6)) {
		return 0;
	}
	// En las partidas, la IA nunca tiene oficina en la ciudad de la subasta:
	// desde aquí, comprobado con estados retocados.
	const uint32_t t_index = town & 0xFF;
	const SavedTown& st = save.towns[t_index];
	const TownMemory& t = st.memory;
	if ((record & 0xFFFF) >= t.u16(0x776)) {
		return 0;  // El juego fallaría (0x51F1F0 da 0).
	}
	const BuildingRecord& rec = st.records[record & 0xFFFF];
	if (rec.pending != 0 || rec.owner == uint8_t(me)) {
		return 0;
	}
	if (uint8_t(town) != m.u8(0x19)) {
		bool found = false;
		for (int o = office_at(save, m.u16(0xC)); o >= 0; o = office_at(save, save.offices[size_t(o)].memory.u16(0x2C8))) {
			if (save.offices[size_t(o)].memory.u8(0x2C6) == uint8_t(town)) {
				found = true;
				break;
			}
		}
		if (!found) {
			return 0;
		}
	}
	const uint32_t cell = t.u32(0x7AC) * rec.x + rec.y;
	const bool flag = cell < st.grid.size() && (st.grid[cell] >> 7) != 0;
	int32_t value = int32_t(uint32_t(events_detail::fn_4daf10(save, town, record, flag)) * 2);
	const int32_t money = m.s32(0x0);
	if (money >= int32_t(uint32_t(value) * 10)) {
		value = int32_t(uint32_t(value) * 4) / 3;
	} else if (money != 0) {
		const double a = double(value);
		const double x = std::log(double(money) / a) * a;
		value = int32_t(uint32_t(value) + uint32_t(ftol32(x * kThird + kHalf)));
	}
	uint32_t a = 0;
	uint32_t b = 0;
	const uint8_t type = rec.type;
	if (type >= 0x15 && type <= 0x1D) {
		const uint32_t k = (type - 0x15) / 3;
		a = t.u32(0x2D8 + 4 * k);
		b = t.u16(0x2F8 + 2 * k);
		const uint8_t low = uint8_t(0x15 + 3 * k);
		for (uint32_t r = t.u16(0x762); r < t.u16(0x776); r = st.records[r].next) {
			const uint8_t rt = st.records[r].type;
			if (rt >= low && rt <= low + 2) {
				b += kHousePlaces[k];  // Sin comprobar con el oráculo para k = 1 y 2.
			}
		}
	} else {
		const uint8_t category = type < 64 ? kBuildingCategory[type] : 0xFF;
		const uint16_t facility = town_array_u16(save, t_index, 0x848 + 16 * uint32_t(type));
		uint8_t other = 0xFF;
		if (category == 1) {
			other = facility >= 1000 ? 0xD : 0xFF;
		} else if (category == 2 && t.u32(0x2CC) >= 1000) {
			other = 0xA;  // Sin comprobar con el oráculo.
		}
		if (!(facility >= 1000 && category != 0xFF) && other == 0xFF) {
			return 0;
		}
		int o = office_at(save, t.u16(0x784));
		if (o < 0) {
			return value;  // Sin comprobar con el oráculo.
		}
		for (; o >= 0; o = office_at(save, save.offices[size_t(o)].memory.u16(0x2CA))) {
			const OfficeMemory& om = save.offices[size_t(o)].memory;
			const uint16_t owner = om.u16(0x2C4);
			if (owner >= save.merchants.size() || save.merchants[owner].memory.u16(0x8) == 0) {
				continue;
			}
			for (uint32_t w = om.u16(0x2CC); w < save.globals.u16(kWorldRecordCount); w = save.world_records[w].u16(0x8)) {
				a += save.world_records[w].u16(0x4);
				b += save.world_records[w].u16(0xC);
			}
		}
	}
	if (b == 0) {
		return value;
	}
	return int32_t((a * uint32_t(value)) / b);
}

// 0x4F5900 (on the merchant): out of his 11 ship lists ([+0x60C + 2·k] the
// counts, bit 0x8000 kept; the u16 arrays at [+0x624 + 4·k], in p3vita
// SaveOpaque::merchant_lists) go the ships that are no longer his (owner
// [+0x0] ≠ merchant): each is replaced by the list's last one. The game keeps
// 11 lists; the save has 10 (the eleventh's array isn't in SaveGame).
void fn_4f5900(SaveGame& save, int merchant) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	for (size_t k = 0; k < 11; ++k) {
		const uint16_t word = m.u16(0x60C + 2 * k);
		uint16_t n = word & 0x7FFF;
		std::vector<uint16_t>* list = nullptr;
		if (k < 10 && size_t(merchant) < save.opaque.merchant_lists.size()) {
			list = &save.opaque.merchant_lists[size_t(merchant)][k];
		}
		if (n == 0) {
			continue;  // In the 12 saves every list is empty, and nothing p3vita runs fills them.
		}
		if (!list || list->size() < n) {
			continue;  // Sin comprobar con el oráculo: an array p3vita doesn't have (left as it is).
		}
		// No save has a ship in these lists. Checked with the oracle: scenario
		// player_pirate_lists (saves 3, 5 and 7; through 0x509250 and 0x507FB0).
		for (uint16_t i = 0; i < n;) {
			const uint16_t s = (*list)[i];
			if (s < save.ships.size() && save.ships[s].memory.u8(0x0) == uint8_t(merchant)) {
				++i;
				continue;
			}
			--n;
			(*list)[i] = (*list)[n];
		}
		if (list && list->size() > n) {
			list->resize(n);
		}
		m.u16(0x60C + 2 * k) = uint16_t(n | (word & 0x8000));
	}
}

// 0x509250 (sobre el objeto de los barcos): vende el barco al comerciante
// `winner` por `price` (el antiguo dueño cobra `price90`): pasa a su lista,
// con la ciudad natal (la siguiente sin [0x2C8] & 1 para los grandes) como
// base [+0x13A] y sin órdenes; el de la subasta (estado 0x0C) queda en puerto.
// With a winner that isn't a merchant (the operation 0x4E, a player's ship
// turning pirate) the ship goes to the pirates of the town `town`
// (fn_509250_pirates, battle/pirate_objects.cpp; its route with `sailing`).
void fn_509250(SaveGame& save, Sailing* sailing, uint32_t ship, uint32_t winner, int32_t price90, int32_t price,
	uint32_t town) {
	GlobalMemory& g = save.globals;
	if (ship >= ship_count(save)) {
		return;
	}
	ShipMemory& s = ship_of(save, ship);
	if (s.u16(0x134) >= 0x11) {
		return;
	}
	const uint32_t count = merchant_count(save);
	const uint8_t previous = s.u8(0x0);
	if (previous < count) {
		// The auctioned ships have no owner; a player's ship turning pirate does.
		// Checked with the oracle: scenario player_pirate (saves 1, 3, 5 and 7).
		MerchantMemory& m = merchant_of(save, previous);
		const uint16_t ships = g.u16(kShipCount);
		uint16_t* link = &m.u16(0xE);
		while (*link < ships) {
			if (*link == ship) {
				*link = s.u16(0x4);
				m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) + uint32_t(price90));
				m.s32(0x4B8) = int32_t(uint32_t(m.s32(0x4B8)) + uint32_t(price90));
				break;
			}
			link = &ship_of(save, *link).u16(0x4);
		}
		s.u8(0x15D) = winner >= count ? s.u8(0x0) : 0xFF;
	}
	// 0x494990 y 0x4778A0: avisos a la interfaz (el oráculo los salta).
	if (s.u8(0x0) < count) {
		const uint8_t owner = s.u8(0x0);
		s.u8(0x0) = 0xFF;
		fn_4f5900(save, owner);
	}
	if (winner >= count) {
		fn_509250_pirates(save, sailing, uint16_t(ship), town);
		return;
	}
	s.u8(0x0) = uint8_t(winner);
	MerchantMemory& w = merchant_of(save, winner);
	s.u16(0x4) = w.u16(0xE);
	w.u16(0xE) = uint16_t(ship);
	uint32_t home = w.u8(0x19) | (town & 0xFFFFFF00);
	if (s.u8(0xE) >= 2) {
		while (save.towns[home & 0xFF].memory.u8(0x2C8) & 1) {
			// Sin comprobar con el oráculo.
			home = (home & 0xFFFFFF00) | (((home & 0xFF) + 1) % town_count(save) & 0xFF);
		}
	}
	w.s32(0x0) = int32_t(uint32_t(w.s32(0x0)) - uint32_t(price));
	w.s32(0x4BC) = int32_t(uint32_t(w.s32(0x4BC)) + uint32_t(price));
	if (s.u16(0x8) < g.u16(kConvoyCount)) {
		// En un convoy: el juego cambia el dueño del convoy y, en el mar, lo
		// manda a la ciudad natal ejecutando en el acto una orden de zarpar
		// (0x535760). Sin traducir: en las partidas no pasa.
	} else if (s.u8(0x39) >= town_count(save) && s.u8(0x38) >= town_count(save)) {
		// En el mar sin ciudad: la orden de zarpar hacia la natal (0x535760).
		// Sin traducir: en las partidas no pasa.
	}
	s.u8(0x136) &= 0xFC;
	if (s.u16(0x132) < g.u16(kTradeRouteCount)) {
		// Sin comprobar con el oráculo. 0x4D4DD0 sobre 0x6DD728 (las rutas de
		// comercio, que SaveGame no tiene) suelta la ruta: no se traduce.
		s.u16(0x132) = 0xFFFF;
	}
	if (s.u16(0x134) == 0xC) {
		s.u16(0x134) = 0;
	}
	s.u16(0x3C) |= 0x420;
	s.u32(0x124) = 0;
	s.u32(0x128) = 0;
	s.u8(0x136) = 0;
	s.u8(0x13A) = uint8_t(home);
	s.u16(0x138) = 0;
}

// 0x5256F0 (sobre la ciudad): vende el edificio `record` a `winner` por
// `price` (el antiguo dueño cobra `price90`): sale de la lista de la oficina
// del antiguo dueño ([+0x2CE] los de tipo < 0x15, las empresas; [+0x2D0] los
// demás, las casas) o de la ciudad [0x764] y entra en la de la oficina del
// nuevo; recuenta las dos (0x4FF2D0).
void fn_5256f0(SaveGame& save, int town, uint32_t record, uint32_t winner, int32_t price, int32_t price90) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	if (record >= t.u16(0x776)) {
		return;  // El juego fallaría.
	}
	const uint32_t count = merchant_count(save);
	BuildingRecord& rec = st.records[record];
	if (rec.owner < count) {
		MerchantMemory& m = merchant_of(save, rec.owner);
		m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) + uint32_t(price90));
		m.s32(0x4B8) = int32_t(uint32_t(m.s32(0x4B8)) + uint32_t(price90));
	}
	const uint8_t buyer = uint8_t(winner);
	if (buyer < count) {
		MerchantMemory& m = merchant_of(save, buyer);
		m.s32(0x0) = int32_t(uint32_t(m.s32(0x0)) - uint32_t(price));
		m.s32(0x4BC) = int32_t(uint32_t(m.s32(0x4BC)) + uint32_t(price));
	}
	if (rec.owner == buyer) {
		return;
	}
	int old_office = -1;
	uint16_t* list;
	if (rec.owner < count) {
		old_office = facilities_detail::fn_5308a0(save, rec.owner, t.u8(0x2C0));
		if (old_office < 0) {
			return;  // Sin comprobar con el oráculo.
		}
		OfficeMemory& o = save.offices[size_t(old_office)].memory;
		list = rec.type < 0x15 ? &o.u16(0x2CE) : &o.u16(0x2D0);
	} else {
		list = &t.u16(0x764);
	}
	if (*list == record) {
		*list = rec.next;
	} else {
		const uint16_t n = t.u16(0x776);
		for (uint16_t* link = list; *link < n;) {
			BuildingRecord& node = st.records[*link];
			if (node.next == record) {
				node.next = rec.next;
				break;
			}
			link = &node.next;  // Sin comprobar con el oráculo (más de un paso).
		}
	}
	rec.owner = buyer;
	int new_office = -1;
	if (buyer < count) {
		// 0x5308A0 con create = 1: sin oficina en la ciudad, le crea una (sin
		// comprobar con el oráculo: en las partidas el comprador la tiene).
		new_office = events_towns_detail::fn_5308a0_create(save, buyer, t.u8(0x2C0));
		if (new_office >= 0) {
			OfficeMemory& o = save.offices[size_t(new_office)].memory;
			uint16_t& head = rec.type < 0x15 ? o.u16(0x2CE) : o.u16(0x2D0);
			rec.next = head;
			head = uint16_t(record);
		}
	}
	if (old_office >= 0) {
		fn_4ff2d0(save, old_office);
	}
	if (new_office >= 0) {
		fn_4ff2d0(save, new_office);
	}
	// Si es la ciudad que se ve (*0x6E51AC), los gráficos (0x521310,
	// 0x5891C0; el oráculo los salta): no se traducen.
}

// 0x4E2CD4 (eventos 0x0A y 0x36): un paso de la subasta según la fase
// [+0x14] (ver arriba). Devuelve si el evento sigue (recolocado) o se quita.
uint32_t fn_4e2cd4(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	Event& e = save.events[event];
	const bool ships = e.u16(0x6) == 0xA;
	switch (e.u8(0x14)) {
	case 0: {
		e.u16(0xE) = 0xE00;
		e.u8(0x14) = 1;
		e.u8(0x16) = 0;
		if (e.u8(0x15) >= town_count(save)) {
			// Sin comprobar con el oráculo.
			e.u8(0x15) = uint8_t(uint32_t(e.u16(0x0)) % town_count(save));
		}
		uint16_t out = e.u16(0x8);
		const uint16_t ship = fn_507d60(save, out, e.u8(0x15), 2);
		e.u16(0x8) = out;
		e.u16(0xA) = 0xFFFF;
		e.u16(0xC) = 0xFFFF;
		const uint32_t town = e.u8(0x15);
		const uint32_t tick = g.u32(kTick);
		const uint32_t type = (town_of(save, town).u8(0x2C8) & 1) ? tick & 1 : tick & 3;
		fn_52a8d0(save, int(town), ship, type);
		fn_5181d0(save, ship);
		recalculate_cargo(save.ships[ship]);
		ShipMemory& s = ship_of(save, ship);
		s.u8(0x3D) |= 4;
		s.u8(0x0) = 0xFF;
		s.u16(0x4) = 0xFFFF;
		const uint32_t health = ((((uint32_t(int32_t(tick) >> 8) + tick) & 0x7F) + 0x81) * uint32_t(s.s32(0x14)));
		s.s32(0x18) = int32_t(health) >> 8;
		fn_509a50(save, ship);
		e.u32(0) += 1;
		return 1;
	}
	case 1:
		e.u32(0) += e.u16(0xE);
		e.u16(0xE) = 0;
		e.u8(0x14) = 2;
		e.u8(0x0) = 0;
		return 1;
	case 2: {
		const uint32_t town = e.u8(0x15);
		if (ships) {
			const uint16_t n = g.u16(kShipCount);
			if (e.u16(0x8) >= n) {
				if (e.u16(0xA) < n) {
					e.u16(0x8) = e.u16(0xA);
					e.u16(0xA) = 0xFFFF;
				} else if (e.u16(0xC) < n) {
					// Sin comprobar con el oráculo.
					e.u16(0x8) = e.u16(0xC);
					e.u16(0xC) = 0xFFFF;
				}
				if (e.u16(0x8) >= n) {
					return 0;
				}
			}
		} else {
			if (town >= save.towns.size()) {
				return 0;  // El juego leería fuera de las ciudades.
			}
			const uint16_t n = town_of(save, town).u16(0x776);
			if ((e.u16(0x8) & 0x7FFF) >= n) {
				if ((e.u16(0xA) & 0x7FFF) < n) {
					e.u16(0x8) = e.u16(0xA);
					e.u16(0xA) = 0xFFFF;
				} else if ((e.u16(0xC) & 0x7FFF) < n) {
					// Sin comprobar con el oráculo.
					e.u16(0x8) = e.u16(0xC);
					e.u16(0xC) = 0xFFFF;
				}
			}
			if ((e.u16(0x8) & 0x7FFF) >= town_of(save, town).u16(0x776)) {
				return 0;  // Sin comprobar con el oráculo.
			}
		}
		if (town >= save.towns.size()) {
			return 0;  // El juego leería fuera de las ciudades.
		}
		TownMemory& t = town_of(save, town);
		if (t.u16(0x9A2) < g.u16(kEventCount)) {
			// Otra subasta en la ciudad: mañana.
			e.u32(0) += 0x100;
			e.u8(0x0) = 0;
			return 1;
		}
		t.u16(0x9A2) = g.u16(kEventHead);
		// To each human merchant of [+0x16] (a bit per human, in order): with
		// [0x6DFC1C] = 1 the ticker line «Tomorrow there will be an auction in
		// X.» (0x4E2F5C: kind 1, the town [+0x15]); otherwise the game writes
		// «An auction begins in X tomorrow.» (0x6C34CC) to its interface itself
		// (0x42B4C0), whoever the merchant (in single player, the player).
		for (uint32_t m = 0, bit = 1; m < g.u16(global::kWorldMerchantCount) && m < save.merchants.size(); ++m) {
			if (save.merchants[m].memory.u16(8) != 0) {
				continue;
			}
			if (e.u8(0x16) & bit) {
				if (g.u32(0x6DFC1C) == 1) {
					send_ticker(save, m, 1, e.u8(0x15));
				} else {
					send_ticker_text(save, m, 0x6C34CC, e.u8(0x15));
				}
			}
			bit = (bit + bit) & 0xFF;
		}
		e.u8(0x14) = 3;
		e.u32(0) += 0x100;
		e.u8(0x0) = 0;
		// 0x5C6C80: interfaz.
		return 1;
	}
	case 3: {
		e.u16(0x12) = 0;
		e.u32(0) += 1;
		e.u8(0x17) = e.u8(0x15);
		e.u8(0x14) = 4;
		uint16_t price;
		if (ships) {
			price = uint16_t(fn_4da860(save, e.u16(0x8)));
		} else {
			price = uint16_t(events_detail::fn_4daf10(save, e.u8(0x15), e.u16(0x8) & 0x7FFF, (e.u16(0x8) >> 15) != 0));
		}
		e.u16(0xE) = price;
		const int32_t p = price;
		uint16_t step = uint16_t((p / 5000 + 1) * 100);
		const int32_t target = p / 25;
		e.u16(0x10) = step;
		if (int32_t(step) < target) {
			uint32_t mode = 0;
			do {
				switch (mode) {
				case 0:
					step = uint16_t(step * 2);
					if (step < 0x4E20) {
						mode = 1;
					}
					break;
				case 1:
					step = uint16_t(step * 2 + (step >> 1));
					mode = 2;
					break;
				case 2:
					step = uint16_t(step * 2);
					mode = 0;
					break;
				default:
					break;
				}
			} while (int32_t(step) < target);
			e.u16(0x10) = step;
		}
		return 1;
	}
	case 4:
		// 0x5C6BA0: interfaz.
		[[fallthrough]];
	case 5:
	case 6: {
		const int32_t bid = int32_t(e.u16(0x10)) + int32_t(e.u16(0xE));
		const uint32_t tick = e.u32(0);
		uint32_t m = tick % merchant_count(save);
		e.u32(0) = tick + 1;
		int32_t tries = int32_t(e.u16(0x12) >> 1) + int32_t(e.u8(0x14)) - 0x40;
		for (;;) {
			if (uint16_t(e.u8(0x17)) != uint16_t(m)) {
				int32_t offer;
				if (ships) {
					offer = fn_4f4400(save, int(m), e.u16(0x8), e.u8(0x15));
				} else {
					offer = fn_4f4550(save, int(m), e.u16(0x8) & 0x7FFF, e.u8(0x15), m);
				}
				if (bid <= offer) {
					e.u16(0xE) = uint16_t(bid);
					e.u8(0x17) = uint8_t(m);
					e.u16(0x12) = 0;
					e.u8(0x14) = 4;
					// 0x5C6BA0: interfaz.
					return 1;
				}
			}
			++m;
			if (merchant_count(save) <= (m & 0xFFFF)) {
				m = 0;
			}
			if (tries-- <= 0) {
				break;
			}
		}
		++e.u16(0x12);
		if (e.u16(0x12) > 0x10) {
			e.u16(0x12) = 0;
			++e.u8(0x14);
		}
		return 1;
	}
	case 7: {
		if (e.u8(0x15) >= town_count(save)) {
			// Sin comprobar con el oráculo (en el 0x0A, solo si el barco existe).
			if (!ships || e.u16(0x8) < g.u16(kShipCount)) {
				e.u8(0x15) = uint8_t(e.u8(0x15) % town_count(save));
			}
		}
		const uint8_t winner = e.u8(0x17);
		const int32_t price = e.u16(0xE);
		const int32_t price90 = price * 9 / 10;
		if (ships) {
			if (e.u16(0x8) < g.u16(kShipCount)) {
				const uint16_t ship = e.u16(0x8);
				e.u16(0x8) = 0xFFFF;
				const uint32_t count = merchant_count(save);
				const uint8_t owner = ship_of(save, ship).u8(0x0);
				Letter letter;
				letter.entry.u8(5) = e.u8(0x15);
				if (winner < count) {
					// The letters (0x4E3230..0x4E3378): 0x2C to a human owner
					// (0x2D if he won it himself): [+0x8], [+0x9] the winner's
					// names, u16 [+0xA] the ship's name, [+0xC] the price · 9 /
					// 10, [+0xE] the price / 10. Then 0x5D to the winner if he
					// isn't the owner: u16 [+0x8] the seller's names ([+0x1C]
					// << 8 + [+0x1D]; without one, from the tick and the ship),
					// [+0xA] the ship's name, [+0xC] its type, [+0xE] the price
					// / 10.
					const ShipMemory& s = ship_of(save, ship);
					uint16_t seller;
					if (owner < count) {
						const MerchantMemory& w = merchant_of(save, winner);
						const MerchantMemory& o = merchant_of(save, owner);
						letter.entry.u8(4) = uint8_t(0x2C + (owner == winner ? 1 : 0));
						letter.entry.u8(8) = w.u8(0x1C);
						letter.entry.u8(9) = w.u8(0x1D);
						letter.entry.u16(0xA) = s.u16(0x15E);
						letter.entry.u16(0xC) = uint16_t(price90);
						letter.entry.u16(0xE) = uint16_t(price / 10);
						if (o.u16(8) == 0) {
							send_letter(save, owner, letter);
						}
						seller = uint16_t((o.u8(0x1C) << 8) + o.u8(0x1D));
					} else {
						// Sin comprobar con el oráculo.
						const uint32_t tick = g.u32(kTick);
						seller = uint16_t((((tick + ship) << 8) & 0x3F00) + (tick & 0x3F));
					}
					if (owner != winner) {
						letter.entry.u8(4) = 0x5D;
						letter.entry.u16(8) = seller;
						letter.entry.u16(0xA) = s.u16(0x15E);
						letter.entry.u16(0xC) = s.u8(0xE) & 3;
						letter.entry.u16(0xE) = uint16_t(price / 10);
						send_letter(save, winner, letter);
					}
					fn_509250(save, event_sailing(), ship, winner, price90, price, e.u8(0x15));
				} else {
					// Sin comprobar con el oráculo. Unsold: the letter 0x2E to the
					// owner (0x4E33B2..0x4E33FC): u16 [+0x8], [+0xA] his names,
					// [+0xC] the ship's name (the game doesn't look whether the
					// owner is a merchant).
					ShipMemory& s = ship_of(save, ship);
					if (owner < count) {
						const MerchantMemory& o = merchant_of(save, owner);
						letter.entry.u8(4) = 0x2E;
						letter.entry.u16(8) = o.u8(0x1C);
						letter.entry.u16(0xA) = o.u8(0x1D);
						letter.entry.u16(0xC) = s.u16(0x15E);
						send_letter(save, owner, letter);
					}
					s.u8(0x3C) |= 0x20;
					s.u16(0x134) = 0;
				}
				// 0x5C6740, 0x5C6BA0: interfaz.
			}
			const uint16_t n = g.u16(kShipCount);
			if (e.u16(0x8) >= n) {
				if (e.u16(0xA) < n) {
					e.u16(0x8) = e.u16(0xA);
					e.u16(0xA) = 0xFFFF;
				} else if (e.u16(0xC) < n) {
					e.u16(0x8) = e.u16(0xC);
					e.u16(0xC) = 0xFFFF;
				}
				if (e.u16(0x8) >= g.u16(kShipCount)) {
					e.u8(0x14) = 8;
					e.u32(0) |= 0xFF;
					return 1;
				}
			}
			// Otro barco del lote.
			e.u8(0x14) = 3;
			e.u32(0) += 0x19;
			return 1;
		}
		const uint32_t town = e.u8(0x15);
		SavedTown& st = save.towns[town];
		if ((e.u16(0x8) & 0x7FFF) < st.memory.u16(0x776)) {
			const uint32_t record = e.u16(0x8) & 0x7FFF;
			e.u16(0x8) = 0xFFFF;
			const uint32_t count = merchant_count(save);
			const uint8_t owner = st.records[record].owner;
			Letter letter;
			letter.entry.u8(5) = e.u8(0x15);
			if (winner < count) {
				// The letters (0x4E34FD..0x4E363A): to a merchant owner 0x76
				// (0x7D if he won it himself): [+0x8], [+0x9] his names,
				// [+0xA], [+0xB] the winner's (0x76), [+0xD] the building's
				// type, u16 [+0xE] the price / 100. Then 0x7F to the winner if
				// he isn't the owner: [+0x8], [+0x9] his names, [+0xA],
				// [+0xB] the owner's, [+0xD], [+0xE] the same.
				const MerchantMemory& w = merchant_of(save, winner);
				letter.entry.u8(0xD) = st.records[record].type;
				letter.entry.u16(0xE) = uint16_t(price / 100);
				if (owner < count) {
					const MerchantMemory& o = merchant_of(save, owner);
					letter.entry.u8(8) = o.u8(0x1C);
					letter.entry.u8(9) = o.u8(0x1D);
					if (owner != winner) {
						letter.entry.u8(4) = 0x76;
						letter.entry.u8(0xA) = w.u8(0x1C);
						letter.entry.u8(0xB) = w.u8(0x1D);
					} else {
						letter.entry.u8(4) = 0x7D;
					}
					send_letter(save, owner, letter);
				}
				if (owner != winner) {
					letter.entry.u8(4) = 0x7F;
					letter.entry.u8(8) = w.u8(0x1C);
					letter.entry.u8(9) = w.u8(0x1D);
					if (owner < count) {
						letter.entry.u8(0xA) = merchant_of(save, owner).u8(0x1C);
						letter.entry.u8(0xB) = merchant_of(save, owner).u8(0x1D);
					}  // Sin comprobar con el oráculo (else the game reads past the merchants).
					send_letter(save, winner, letter);
				}
				fn_5256f0(save, int(town), record, winner, price, price90);
			} else if (owner < count) {
				// Unsold: the letter 0x7E to the owner (0x4E366F..0x4E36B3):
				// [+0x8], [+0x9] his names.
				const MerchantMemory& o = merchant_of(save, owner);
				letter.entry.u8(4) = 0x7E;
				letter.entry.u8(8) = o.u8(0x1C);
				letter.entry.u8(9) = o.u8(0x1D);
				send_letter(save, owner, letter);
			}
			// 0x5C6740, 0x5C6BA0: interfaz.
		}
		const uint16_t n = st.memory.u16(0x776);
		if ((e.u16(0x8) & 0x7FFF) >= n) {
			if ((e.u16(0xA) & 0x7FFF) < n) {
				// Sin comprobar con el oráculo.
				e.u16(0x8) = e.u16(0xA);
				e.u16(0xA) = 0xFFFF;
			} else if ((e.u16(0xC) & 0x7FFF) < n) {
				e.u16(0x8) = e.u16(0xC);
				e.u16(0xC) = 0xFFFF;
			}
			if ((e.u16(0x8) & 0x7FFF) >= st.memory.u16(0x776)) {
				e.u8(0x14) = 8;
				e.u32(0) |= 0xFF;
				return 1;
			}
		}
		// Otro edificio del lote.
		e.u8(0x14) = 3;
		e.u32(0) += 0x19;
		return 1;
	}
	case 8: {
		TownMemory& t = town_of(save, e.u8(0x15));
		if (t.u16(0x9A2) == g.u16(kEventHead)) {
			t.u16(0x9A2) = 0xFFFF;
		}
		// 0x5C6BF0: interfaz.
		return 0;
	}
	default:
		return 0;  // Sin comprobar con el oráculo.
	}
}

}  // namespace events_mayor_detail

using namespace events_mayor_detail;

// Los tipos de este grupo (manejadores de la tabla 0x4D8A28): el 0x02 y el
// 0x20 se recolocan siempre; el 0x0A, el 0x36 y el 0x21, según lo que devuelve
// su manejador.
int events_mayor(SaveGame& save, uint16_t event, uint16_t type) {
	switch (type) {
	case 0x02:
		fn_4dbb00(save);
		return 1;
	case 0x0A:
	case 0x36:
		return fn_4e2cd4(save, event) ? 1 : 0;
	case 0x20:
		fn_4dbbd0(save);
		return 1;
	case 0x21:
		return fn_4dc1f0(save, event) ? 1 : 0;
	default:
		return -1;
	}
}

}  // namespace p3::data::tick
