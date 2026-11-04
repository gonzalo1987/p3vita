// La candidatura a Gobernador («Alderman» en la versión inglesa) y las
// operaciones que encola: 0x5326E0 (con 0x532E30, 0x515A60 y 0x51A470), la
// 0x85 (0x53FD10) y la 0x86 (0x53FE20, la misma que lleva dentro el ejecutor
// 0x535760 en 0x536801). Ver docs/formats/world_tick.md §7 y §8.
//
// Cuando un alcalde con [+0x37] = 0 llega a 120 de reputación para optar,
// 0x4F75F0 (merchants.cpp) le pone [+0x37] = 1 y llama a 0x5326E0, que
// encola una 0x86 y una 0x85 por cada tipo de encargo de la Hansa que
// encuentra. Un encargo son 16 bytes (los de la 0x85 tras el código y los
// [+0x8..+0x17] de su evento 0x32):
//   [+0x0] u32 plazo (tick)   [+0x4] u8 tipo (0..4)   [+0x5] u8 comerciante (0xFF: sin dar)
//   [+0x6], [+0x7] 0          [+0x8..+0xF] según el tipo:
//   0 fundar una ciudad: u32 máscara de mercancías, u8 número de ciudad
//     (StadtDaten.ini), u8 de 0x672D50 por ese número, u8 0xFF
//   1 carretera entre dos ciudades: u8 ciudad, u8 ciudad, u8 días
//   2 cazar un barco pirata: u16 barco, u16 nombre y u16 apellido del capitán
//   3 acabar con una guarida pirata: u8 pirata, u8 su guarida, u8 0
//   4 que crezca una ciudad: u16 habitantes, u8 ciudad
// El registro no se limpia entre un tipo y el siguiente: los bytes que un
// tipo no escribe se quedan con lo que dejó el anterior.
//
// No hay avisos: 0x5326E0 solo llama además a 0x4EC5F4 (la lista de los
// encargos sin dar), cuyo resultado no usa.
//
// Después, el encargo ya dado a un comerciante (la parte de 0x4EAFE4, evento
// 0x32, desde 0x4EB0EB; la llama fn_4eafe4 de events_rare.cpp): 0x4EBF54 lo
// empieza, cada tipo comprueba su objetivo hasta el plazo, 0x4EC354 lo da por
// cumplido ([+0x37] del comerciante + 1) y, si vence, 0x4F9170 castiga su
// reputación y el encargo pasa al tipo 5. El evento 0x32 (0x18 bytes):
//   [+0x0] u32 tick   [+0x8..+0x17] el encargo de arriba, y además
//   [+0xE] u8 madurez (sin comerciante)   [+0xF] u8 empezado
//   [+0x16] u8 tipo 0: la ciudad nueva; tipo 2: 1 cumplido, 2 fallido
// El jugador humano coge un encargo con la operación 0x4B (0x53B440).
// La carretera (tipo 1) gasta por tramos los materiales que aporta el
// jugador (operación 0x3B) y, al acabar, construye un edificio 0x27 en las
// dos ciudades (construction.cpp, 0x521900).
// La guarida pirata (tipo 3) espera a que un barco del comerciante se
// acerque (los piratas se refuerzan con barcos nuevos, 0x5144A0) y llegue a
// ella. Not rewritten yet: the attack on the nest (a battle: the battles'
// piece 6). The letters (0x4D6530) are sent; los avisos (0x548CA0) y las
// ventanas del jugador no cambian la simulación.

#include <cstring>

#include "p3data/game_fixes.hpp"
#include "p3data/mail.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {

namespace events_rare_detail {
bool fn_532c50(const SaveGame& save, uint32_t index);
}  // namespace events_rare_detail
namespace construction_detail {
bool fn_4f9c80(const SaveGame& save, int town, uint8_t& x, uint8_t& y, uint32_t type, uint8_t param, bool flag);
bool fn_521900(SaveGame& save, int town, uint8_t owner, uint8_t x, uint8_t y, uint8_t type, uint8_t param);
}  // namespace construction_detail
namespace events_mayor_detail {
uint16_t fn_507d60(SaveGame& save, uint16_t& out, uint32_t town, uint32_t mode);
void fn_52a8d0(SaveGame& save, int town, int ship, uint32_t type);
void fn_5181d0(SaveGame& save, int ship);
void fn_509a50(SaveGame& save, int ship);
}  // namespace events_mayor_detail
namespace events_detail {
uint16_t fn_5097c0(SaveGame& save, bool keep);
}  // namespace events_detail
namespace ship_ai_detail {
int32_t fn_5184f0(SaveGame& save, int ship);
}  // namespace ship_ai_detail
namespace facilities_detail {
uint32_t fn_52bb00(const SaveGame& save, int town);
}  // namespace facilities_detail

namespace {

constexpr uint32_t kEvents = 0x6DD73C;  // u32 the address of the queue's array (in the game's memory).
constexpr uint32_t kEventBusy = 0x6DD73C + 0x4;  // u32 1 mientras 0x4D85C0 atiende la cola.
constexpr uint32_t kEventHead = 0x6DD73C + 0x8;  // u16 primer evento de la cola.
constexpr uint32_t kEventCount = 0x6DD73C + 0xC;  // u16 eventos.
constexpr uint32_t kTownCount = global::kWorldTownCount;  // u32
constexpr uint32_t kMerchantCount = global::kWorldMerchantCount;  // u16
constexpr uint32_t kOfficeCount = global::kWorldOfficeCount;  // u16
constexpr uint32_t kShipCount = global::kShipCount;  // u16
constexpr uint32_t kCaptainCount = global::kCaptainCount;  // u16
constexpr uint32_t kTick = global::kWorldTick;  // u32
constexpr uint32_t kMonth = global::kWorldMonth;  // u8 (0 = enero)
constexpr uint32_t kYear = global::kWorldYear;  // u16
constexpr uint32_t kTownIds = 0x6DE4B8;  // u8 por ciudad: su número de StadtDaten.ini.
constexpr uint32_t kTownTable = 0x6DDB90;  // 0x34 bytes por número: [+0x20], [+0x24] s32 x, y; [+0x30] u8 región.
constexpr uint32_t kPirates = global::kShips + 0xC;  // 0x6DD7AC: 5 punteros a los objetos piratas.
constexpr uint32_t kHanseTown = 0x6DE52E;  // u8 ([+0x8E] del mundo): la ciudad del jefe de la Hansa.

// 0x672D78: día del año en que empieza cada mes (el 12 es el final del año)
// y lo que sigue en el ejecutable, que el plazo de la carretera lee al
// contar sus días como meses.
constexpr uint16_t kMonthStart[130] = {0x0000, 0x001F, 0x003B, 0x005A, 0x0078, 0x0097, 0x00B5, 0x00D4, 0x00F3, 0x0111,
	0x0130, 0x014E, 0x016D, 0x0000, 0x001F, 0x001C, 0x001F, 0x001E, 0x001F, 0x001E, 0x001F, 0x001F, 0x001E, 0x001F,
	0x001E, 0x001F, 0xFFFF, 0x0000, 0x1A21, 0x2E28, 0x0000, 0x4B00, 0x3333, 0x3F33, 0x999A, 0x3F99, 0xCCCD, 0x3D4C,
	0x0000, 0x0000, 0x12DA, 0x51C1, 0x5555, 0x3FD5, 0x0033, 0x0A00, 0x0180, 0x0012, 0x0100, 0x0100, 0x0019, 0xFE00,
	0x0041, 0x0041, 0x0041, 0x000B, 0x0000, 0x4B00, 0x70A4, 0x3F7D, 0x0000, 0x3F80, 0xB852, 0x3F1E, 0x0A3D, 0x3ED7,
	0x851F, 0x3E6B, 0x0000, 0x0000, 0x6666, 0x3FA6, 0x0000, 0x3F80, 0x3333, 0x3F33, 0xAAAB, 0x3FAA, 0xAAAB, 0x402A,
	0xAAAB, 0x40AA, 0x0000, 0x4080, 0xAAAB, 0x402A, 0xAAAB, 0x402A, 0xAAAB, 0x3FAA, 0x0000, 0x4080, 0x0000, 0x4080,
	0xAAAB, 0x3FAA, 0x0000, 0x4080, 0x0000, 0x4080, 0xAAAB, 0x3FAA, 0xAAAB, 0x3FAA, 0xAAAB, 0x3FAA, 0x0000, 0x4000,
	0xAAAB, 0x3FAA, 0xAAAB, 0x3F2A, 0x0000, 0x4000, 0xAAAB, 0x3FAA, 0xAAAB, 0x3F2A, 0x0000, 0x4080, 0xAAAB, 0x3FAA,
	0x0000, 0x0000, 0x0000, 0x4080, 0xAAAB, 0x402A, 0xAAAB, 0x3FAA};

// 0x672BF8: los 7 pares de ciudades (números de StadtDaten.ini) que puede
// unir una carretera y los días de obra; el cuarto byte no se usa aquí.
constexpr uint8_t kRoads[7][4] = {{0x03, 0x04, 0x50, 0x20}, {0x09, 0x0A, 0x82, 0x34}, {0x0B, 0x0D, 0x1E, 0x0C},
	{0x0C, 0x0E, 0x28, 0x10}, {0x1C, 0x1F, 0x78, 0x30}, {0x24, 0x25, 0x3C, 0x18}, {0x26, 0x27, 0x5A, 0x24}};

// 0x672BF0: los materiales de un tramo de carretera, en el orden de los
// contadores del encargo ([+0x14 + k], con dos bits más en [+0x13]).
constexpr uint8_t kRoadSegment[4] = {0x03, 0x04, 0x01, 0x02};

// El tipo de edificio de la carretera (registros [0x75C] de la ciudad).
constexpr uint8_t kRoadBuilding = 0x27;

// 0x672D50: un byte por número de ciudad para el encargo de fundarla.
constexpr uint8_t kNewTownByte[40] = {0x13, 0x1B, 0x14, 0x19, 0x0F, 0x0E, 0x1A, 0x18, 0x15, 0x10, 0x07, 0x16, 0x03,
	0x1B, 0x00, 0x06, 0x0C, 0x17, 0x0A, 0x0D, 0x0A, 0x0C, 0x0B, 0x0B, 0x19, 0x08, 0x02, 0x13, 0x01, 0x17, 0x05, 0x11,
	0x1A, 0x16, 0x0D, 0x04, 0x13, 0x12, 0x18, 0x09};

// 0x672C88: el bit (+ 3) de cada mercancía en la máscara de la ciudad nueva.
constexpr uint8_t kWareBit[24] = {0x09, 0x0A, 0x05, 0x06, 0x0D, 0x08, 0x00, 0x10, 0x0C, 0x04, 0xFF, 0x0B, 0x07, 0x0A,
	0x0F, 0x13, 0x0E, 0x14, 0x11, 0x12, 0x03, 0x03, 0x03, 0x03};

// La región que busca 0x532E30 según la mercancía que más falta (tabla de
// bytes 0x5331F0 con el salto de 0x5331D8); de la 0x14 en adelante, la 5.
constexpr uint8_t kRegionOfWare[20] = {2, 0, 1, 5, 2, 3, 5, 2, 5, 4, 1, 5, 5, 0, 0, 4, 1, 3, 2, 2};

using Operation = GameMemory<0x14>;

uint32_t town_count(const SaveGame& save) {
	return save.globals.u32(kTownCount);
}

bool has_pirate(const SaveGame& save, uint32_t k) {
	return save.globals.u32(kPirates + 4 * k) != 0 && save.pirates[k].has_value();
}

// El tick en que empieza el mes `month` (contado desde enero del año en
// curso) con una sola vuelta de año, como los tipos 1, 2 y 3.
uint32_t month_tick_once(const SaveGame& save, uint32_t month) {
	uint32_t t = uint32_t(save.globals.u16(kYear)) * 365u * 256u;
	if (month >= 13) {
		t += 0x16D00;
		month -= 12;
	}
	return t + (uint32_t(kMonthStart[month]) << 8);
}

}  // namespace

namespace alderman_detail {

// 0x515A60 (cdecl): el primer número de ciudad (StadtDaten.ini) desde
// `from` que no es de una ciudad ni la guarida de un pirata; al pasar de 40
// vuelve una vez a 0, y si no hay ninguno, 0.
uint8_t fn_515a60(const SaveGame& save, uint32_t from) {
	const GlobalMemory& g = save.globals;
	bool used[0x28] = {};
	for (uint32_t i = town_count(save); i > 0;) {
		--i;
		used[g.u8(kTownIds + i)] = true;
	}
	for (uint32_t k = 0; k < 5; ++k) {
		if (has_pirate(save, k) && save.pirates[k]->u8(0xE) < 0x28) {
			used[save.pirates[k]->u8(0xE)] = true;
		}
	}
	uint32_t n = from;
	bool wrapped = false;
	for (;;) {
		if (n >= 0x28) {
			if (wrapped) {
				// Unreachable: 0x5326E0 founds towns only while there are fewer
				// than 26, so with the 5 pirate lairs at most 31 of the 40
				// numbers are taken (0x515A60 is only called from 0x532E30).
				return 0;
			}
			wrapped = true;
			n = 0;
		}
		if (!used[n]) {
			return uint8_t(n);
		}
		++n;
	}
}

// 0x51A470 (sobre un barco): el pirata del barco ([+0x158]) si existe; si no,
// el primero que exista, o 0.
uint8_t fn_51a470(const SaveGame& save, int ship) {
	const uint8_t p = save.ships[size_t(ship)].memory.u8(0x158);
	if (p < 5 && save.globals.u32(kPirates + 4 * uint32_t(p)) != 0) {
		return p;
	}
	// The ship's pirate doesn't exist. Checked with the oracle: scenario candidacy_pirate_index (saves 1 and 3).
	for (uint8_t k = 0; k < 5; ++k) {
		if (save.globals.u32(kPirates + 4 * uint32_t(k)) != 0) {
			return k;
		}
	}
	return 0;
}

// 0x532E30 (sobre el mundo): el sitio de la ciudad nueva. Las tres
// mercancías con menos existencias por consumo en todas las ciudades y sus
// oficinas dan la máscara `mask` (bits de 0x672C88) y la que más falta, la
// región; entre los números de ciudad libres (0x515A60), `number` es el de
// esa región más alejado de las demás ciudades (la mayor suma de los
// cuadrados de la distancia a las 5 más cercanas) o, si no hay ninguno de
// esa región, el más alejado de todos.
void fn_532e30(const SaveGame& save, uint32_t& mask, uint8_t& number) {
	const GlobalMemory& g = save.globals;
	int32_t ratio[4] = {0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF};
	// Las mercancías de las tres menores en wares[1..3] (con [0] y [4] de
	// relleno, como en la pila del juego).
	uint32_t wares[5] = {0, 0xB, 0, 1, 3};
	uint32_t stock[20] = {};
	uint32_t use[20] = {};
	const uint32_t offices = g.u16(kOfficeCount);
	for (uint32_t i = 0; i < town_count(save); ++i) {
		const TownMemory& t = save.towns[i].memory;
		for (uint32_t w = 0; w < 20; ++w) {
			stock[w] += t.u32(0x670 + 4 * w) + t.u32(0x4 + 4 * w);
			use[w] += t.u32(0x310 + 4 * w) + t.u32(0x64 + 4 * w);
		}
		for (uint32_t o = t.u16(0x784); o < offices; o = save.offices[o].memory.u16(0x2CA)) {
			const OfficeMemory& office = save.offices[o].memory;
			for (uint32_t w = 0; w < 20; ++w) {
				stock[w] += office.u32(0x4 + 4 * w);
				use[w] += office.u32(0x64 + 4 * w);
			}
		}
	}
	for (uint32_t w = 0; w < 20; ++w) {
		if (int32_t(use[w]) < 0x400) {
			continue;
		}
		const int32_t r = int32_t(stock[w]) / (int32_t(use[w]) >> 10);
		for (int c = 3; c > 0; --c) {
			if (ratio[c - 1] <= r) {
				break;
			}
			ratio[c] = ratio[c - 1];
			ratio[c - 1] = r;
			wares[c + 1] = wares[c];
			wares[c] = w;
		}
	}
	mask = 0;
	uint32_t limit = 3;
	for (uint32_t j = 0; j < limit; ++j) {
		const uint32_t w = wares[j + 1];
		const uint32_t bit = (1u << ((uint32_t(kWareBit[w]) - 3) & 31)) & 0xFFFFFF;
		if (mask & bit) {
			limit = 4;  // Dos con el mismo bit: entra la cuarta.
		} else {
			mask |= bit;
		}
		if (w == 0xA) {
			mask |= 0x40020;
		}
	}
	const uint32_t region = wares[1] > 0x13 ? 5 : kRegionOfWare[wares[1]];

	number = 0xFF;
	int32_t best = 0;
	bool matched = false;  // Ya ha salido uno de la región: solo cuentan esos.
	uint32_t n = fn_515a60(save, 0);
	for (;;) {
		const bool same = g.u8(kTownTable + 0x30 + 0x34 * n) == region;
		bool score = true;
		if (matched) {
			score = same;
		} else if (same) {
			matched = true;
			best = 0;
		}
		if (score) {
			int32_t d[6];
			std::memset(d, 0x7F, sizeof(d));
			const int32_t x = g.s32(kTownTable + 0x20 + 0x34 * n);
			const int32_t y = g.s32(kTownTable + 0x24 + 0x34 * n);
			for (uint32_t i = 0; i < town_count(save); ++i) {
				const uint32_t id = g.u8(kTownIds + i);
				const uint32_t dx = uint32_t(x) - uint32_t(g.s32(kTownTable + 0x20 + 0x34 * id));
				const uint32_t dy = uint32_t(y) - uint32_t(g.s32(kTownTable + 0x24 + 0x34 * id));
				const int32_t d2 = int32_t(dy * dy + dx * dx);
				for (int c = 4; c >= 0; --c) {
					if (d[c] <= d2) {
						break;
					}
					d[c + 1] = d[c];
					d[c] = d2;
				}
			}
			uint32_t sum = 0;
			for (int c = 0; c < 5; ++c) {
				sum += uint32_t(d[c]);
			}
			if (int32_t(sum) > best) {
				best = int32_t(sum);
				number = uint8_t(n);
			}
		}
		const uint32_t previous = n;
		n = fn_515a60(save, n + 1);
		if (int32_t(previous) > int32_t(n)) {
			break;
		}
	}
}

}  // namespace alderman_detail

// 0x5326E0 (sobre el mundo, sin argumentos): la candidatura. Encola una
// operación 0x86 y, para cada tipo de encargo con objetivo, una 0x85.
void fn_5326e0(SaveGame& save) {
	GlobalMemory& g = save.globals;
	Operation op;
	op.u32(0) = 0x86;
	enqueue_operation(save, op.bytes.data(), op.bytes.size());

	// El encargo ([esp+0x4C..0x5B] del juego).
	GameMemory<0x10> rec;
	rec.u8(5) = 0xFF;
	const uint32_t towns = town_count(save);
	const uint32_t month = g.u8(kMonth);
	for (; rec.u8(4) < 5; ++rec.u8(4)) {
		switch (rec.u8(4)) {
		case 0: {
			// Fundar una ciudad, si hay menos de 26.
			if (towns >= 0x1A) {
				continue;  // Checked with the oracle: scenario candidacy_26_towns (save 3).
			}
			uint32_t m = month + 0x19;
			uint32_t t = uint32_t(g.u16(kYear)) * 365u * 256u;
			for (uint32_t years = (m - 13) / 12 + 1; years > 0; --years) {
				t += 0x16D00;
				m -= 12;
			}
			rec.u32(0) = t + (uint32_t(kMonthStart[m]) << 8);
			uint32_t mask = 0;
			uint8_t number = 0xFF;
			alderman_detail::fn_532e30(save, mask, number);
			rec.u32(8) = mask;
			rec.u8(0xC) = number;
			rec.u8(0xE) = 0xFF;
			// Con number = 0xFF el juego leería fuera de la tabla; no pasa.
			rec.u8(0xD) = number < 40 ? kNewTownByte[number] : 0;
			break;
		}
		case 1: {
			// La carretera: el par con mercado en las dos ciudades, sin el bit
			// 0x8000 y con más diferencia de habitantes.
			rec.u8(8) = 0xFF;
			rec.u8(9) = 0xFF;
			uint32_t best = 0;
			for (const uint8_t* pair : kRoads) {
				uint32_t a = 0xFF;
				uint32_t b = 0xFF;
				for (uint32_t i = 0; i < towns; ++i) {
					if ((save.towns[i].memory.u32(0x76C) & 0x8100) != 0x100) {
						continue;
					}
					const uint8_t id = g.u8(kTownIds + i);
					if (pair[0] == id) {
						a = i;
					} else if (pair[1] == id) {
						b = i;
					}
				}
				if (a == 0xFF || b == 0xFF) {
					continue;
				}
				const int32_t pa = save.towns[a].memory.s32(0x2D4);
				const int32_t pb = save.towns[b].memory.s32(0x2D4);
				const uint32_t diff = pa > pb ? uint32_t(pa) - uint32_t(pb) : uint32_t(pb) - uint32_t(pa);
				if (diff >= best) {
					rec.u8(9) = uint8_t(b);
					rec.u8(8) = uint8_t(a);
					rec.u8(0xA) = pair[2];
					best = diff;
				}
			}
			if (rec.u8(8) >= towns || rec.u8(9) >= towns) {
				continue;
			}
			// El juego cuenta los días como meses y solo da una vuelta de año.
			rec.u8(0xB) = 0;
			rec.u32(0) = month_tick_once(save, month + rec.u8(0xA));
			rec.u32(0xC) = 0;
			break;
		}
		case 2: {
			// Cazar un barco: del pirata con más fuerza (suma del [+0x120] de
			// sus barcos desde su [+0x16], más de 1000), su último barco con
			// capitán que no sea de un comerciante.
			rec.u16(8) = 0xFFFF;
			uint32_t best = 1000;
			const uint32_t ships = g.u16(kShipCount);
			for (uint32_t k = 0; k < 5; ++k) {
				if (!has_pirate(save, k)) {
					continue;
				}
				const GameMemory<0x18>& pirate = *save.pirates[k];
				uint32_t chosen = 0xFFFF;
				uint32_t sum = pirate.u16(0x16);
				for (uint32_t s = pirate.u16(0xC); s < ships; s = save.ships[s].memory.u16(0x4)) {
					const ShipMemory& ship = save.ships[s].memory;
					if (ship.u16(0x42) < g.u16(kCaptainCount) && ship.u8(0x15D) >= g.u16(kMerchantCount)) {
						chosen = s;
					}
					sum += ship.u32(0x120);
				}
				if (sum > best && chosen != 0xFFFF) {
					best = sum;
					rec.u16(8) = uint16_t(chosen);
				}
			}
			if (rec.u16(8) >= g.u16(kShipCount)) {
				continue;  // Checked with the oracle: scenario candidacy_no_targets (saves 1 and 3).
			}
			rec.u32(0) = month_tick_once(save, month + 3);
			const int ship = rec.u16(8);
			const CaptainMemory& captain = save.captains[save.ships[size_t(ship)].memory.u16(0x42)];
			rec.u16(0xA) = captain.u8(2);
			rec.u16(0xC) = captain.u8(3);
			const uint8_t p = alderman_detail::fn_51a470(save, ship);
			const uint8_t difficulty = save.settings.u8(0x13);
			if (difficulty < 2 && save.pirates[p]) {
				save.pirates[p]->u8(0x14) = uint8_t(difficulty + 2);
			}
			break;
		}
		case 3: {
			// La guarida del pirata con más barcos (más de 2).
			uint32_t most = 2;
			rec.u8(8) = 0xFF;
			const uint32_t ships = g.u16(kShipCount);
			for (uint32_t k = 0; k < 5; ++k) {
				if (!has_pirate(save, k)) {
					continue;
				}
				uint32_t n = 0;
				for (uint32_t s = save.pirates[k]->u16(0xC); s < ships; s = save.ships[s].memory.u16(0x4)) {
					++n;
				}
				if (n > most) {
					most = n;
					rec.u8(8) = uint8_t(k);
				}
			}
			if (rec.u8(8) >= 5) {
				continue;
			}
			rec.u32(0) = month_tick_once(save, month + 7);
			rec.u8(9) = save.pirates[rec.u8(8)]->u8(0xE);
			rec.u8(0xA) = 0;
			break;
		}
		case 4: {
			// La ciudad con mercado y menos habitantes (menos de 2001): que
			// llegue a los suyos + 2000, redondeado hacia abajo a 250.
			uint32_t chosen = 0xFF;
			uint32_t least = 0x7D1;
			rec.u8(0xA) = 0xFF;
			for (uint32_t i = 0; i < towns; ++i) {
				const TownMemory& t = save.towns[i].memory;
				if ((t.u32(0x76C) & 0x100) && t.u32(0x2D4) < least) {
					chosen = i & 0xFF;
					least = t.u32(0x2D4);
					rec.u8(0xA) = uint8_t(chosen);
				}
			}
			if (chosen == 0xFF) {
				continue;  // Checked with the oracle: scenario candidacy_no_targets (saves 1 and 3).
			}
			rec.u16(8) = uint16_t((least + 0x7D0) / 250 * 250);
			rec.u32(0) = ((uint32_t(g.u16(kYear)) + 1) * 365u + kMonthStart[month]) << 8;
			break;
		}
		}
		op.u32(0) = 0x85;
		std::memcpy(op.bytes.data() + 4, rec.bytes.data(), 0x10);
		enqueue_operation(save, op.bytes.data(), op.bytes.size());
	}
}

// 0x53FD10 (operación 0x85, sobre ella): crea el encargo de la Hansa de la
// operación (un evento 0x32 para mañana con sus 16 bytes en [+0x8..+0x17]),
// salvo que ya haya un 0x32 del mismo tipo y objetivo.
void fn_53fd10(SaveGame& save, const uint8_t* operation) {
	Operation op;
	std::memcpy(op.bytes.data(), operation, op.bytes.size());
	const uint8_t kind = op.u8(8);
	if (kind >= 5) {
		// Only the interface or a network game: 0x5326E0 queues kinds 0..4.
		return;  // Checked with the oracle: scenario hanse_ops (saves 3 and 5).
	}
	GlobalMemory& g = save.globals;
	for (uint32_t i = g.u16(kEventHead); i < g.u16(kEventCount); i = save.events[i].u16(4)) {
		const GameMemory<0x18>& e = save.events[i];
		if (e.u16(6) != 0x32 || e.u8(0xC) != kind) {
			continue;
		}
		switch (kind) {
		case 0:
			if (e.u8(0x14) == op.u8(0x10) || e.u32(0x10) == op.u32(0xC)) {
				return;
			}
			break;
		case 1:
			if (e.u8(0x10) == op.u8(0xC) || e.u8(0x11) == op.u8(0xD)) {
				return;
			}
			break;
		case 2:
			if (e.u16(0x10) == op.u16(0xC)) {
				return;
			}
			break;
		case 3:
			if (e.u8(0x10) == op.u8(0xC) || e.u8(0x11) == op.u8(0xD)) {
				return;
			}
			break;
		case 4:
			if (e.u8(0x12) == op.u8(0xE)) {
				return;
			}
			break;
		}
	}
	const uint16_t created = events_detail::fn_4d8cf0(save, g.u32(kTick) + 0x100);
	GameMemory<0x18>& e = save.events[created];
	e.u16(6) = 0x32;
	std::memcpy(e.bytes.data() + 8, op.bytes.data() + 4, 0x10);
}

// 0x53FE20 (operación 0x86; el ejecutor lleva la misma copia en 0x536801):
// los encargos de la Hansa sin comerciante que ya no valen (0x532C50) pasan
// al tipo 5, y su manejador los quita.
void fn_53fe20(SaveGame& save) {
	GlobalMemory& g = save.globals;
	for (uint32_t i = g.u16(kEventHead); i < g.u16(kEventCount); i = save.events[i].u16(4)) {
		GameMemory<0x18>& e = save.events[i];
		if (e.u16(6) == 0x32 && e.u8(0xD) >= g.u16(kMerchantCount) && e.u8(0xC) < 5 &&
			!events_rare_detail::fn_532c50(save, i)) {
			e.u8(0xC) = 5;
		}
	}
}

// --- El encargo con comerciante ---

namespace alderman_detail {

// 0x4F9170 (sobre el comerciante): el castigo de un encargo vencido: 1 menos
// en el primer componente de su reputación en cada ciudad con [0x2C8] & 0xC0.
void fn_4f9170(SaveGame& save, int merchant) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	for (uint32_t i = town_count(save); i > 0;) {
		--i;
		if (save.towns[i].memory.u8(0x2C8) & 0xC0) {
			float& f = m.f32(0x11C + 12 * size_t(i));
			f = float(double(f) - 1.0);  // 0x66BC80
		}
	}
}

// 0x4EC694 (sobre la cola de eventos): el primer encargo de la Hansa del
// comerciante, o 0xFFFF (también si no existe o no es candidato).
uint16_t fn_4ec694(const SaveGame& save, uint32_t merchant) {
	const GlobalMemory& g = save.globals;
	if (merchant >= g.u16(kMerchantCount) || save.merchants[merchant].memory.u8(0x37) == 0) {
		return 0xFFFF;
	}
	for (uint32_t i = g.u16(kEventHead); i < g.u16(kEventCount); i = save.events[i].u16(4)) {
		const GameMemory<0x18>& e = save.events[i];
		if (e.u16(6) == 0x32 && e.u8(0xD) == merchant) {
			return uint16_t(i);
		}
	}
	return 0xFFFF;
}

// 0x5333C0 (on the world, with two outputs): the Alderman's ([0x6DE52D])
// first and family names; for a citizen, those of the councillor of his town
// ([0x6DE52E]: [+0x6D4 + j], [+0x6D8 + j]) with j = (~Alderman) & 3, or 0
// when that town's mayor is a merchant.
void fn_5333c0(const SaveGame& save, uint8_t& first, uint8_t& family) {
	const GlobalMemory& g = save.globals;
	const uint32_t alderman = g.u8(0x6DE52D);
	const uint32_t count = g.u16(kMerchantCount);
	if (alderman < count) {
		first = save.merchants[alderman].memory.u8(0x1C);
		family = save.merchants[alderman].memory.u8(0x1D);
		return;
	}
	const TownMemory& t = save.towns[g.u8(kHanseTown)].memory;
	const uint32_t j = t.u8(0x6F1) < count ? 0 : uint8_t(~alderman) & 3;
	first = t.u8(0x6D4 + j);
	family = t.u8(0x6D8 + j);
}

// The letters about a Hanse task of a merchant, 0x70 (expired, 0x4EB16B..)
// and 0x6F (done, 0x4EC372..): [+0x5] the Hanse town; [+0x8] the task's kind
// [+0xC]; [+0x9] the merchant's title (0x4F9430) + his rank at home << 5;
// [+0xA], [+0xB] his names; [+0xC], [+0xD] the Alderman's (for a citizen,
// the councillor (~Alderman) & 3 of the Hanse town; unlike 0x5333C0 the game
// doesn't look at that town's mayor here); [+0xE], [+0xF] by kind: 0 the new
// town [+0x16]; 1 the road's towns [+0x10], [+0x11]; 2 [+0x12], [+0x14]; 3
// [+0x11]; 4 [+0x12].
void hanse_task_letter(const SaveGame& save, Letter& letter, const GameMemory<0x18>& e, uint32_t merchant) {
	const GlobalMemory& g = save.globals;
	const MerchantMemory& m = save.merchants[merchant].memory;
	letter.entry.u8(5) = g.u8(kHanseTown);
	letter.entry.u8(9) = uint8_t(merchants_detail::fn_4f9430(save, int(merchant)) + (m.u8(0x39C + m.u8(0x19)) << 5));
	letter.entry.u8(0xA) = m.u8(0x1C);
	letter.entry.u8(0xB) = m.u8(0x1D);
	const uint32_t alderman = g.u8(0x6DE52D);
	if (alderman < g.u16(kMerchantCount)) {
		letter.entry.u8(0xC) = save.merchants[alderman].memory.u8(0x1C);
		letter.entry.u8(0xD) = save.merchants[alderman].memory.u8(0x1D);
	} else {
		const TownMemory& t = save.towns[g.u8(kHanseTown)].memory;
		const uint32_t j = uint8_t(~alderman) & 3;
		letter.entry.u8(0xC) = t.u8(0x6D4 + j);
		letter.entry.u8(0xD) = t.u8(0x6D8 + j);
	}
	switch (e.u8(0xC)) {
	case 0:
		letter.entry.u8(0xE) = e.u8(0x16);
		break;
	case 1:
		letter.entry.u8(0xE) = e.u8(0x10);
		letter.entry.u8(0xF) = e.u8(0x11);
		break;
	case 2:
		letter.entry.u8(0xE) = e.u8(0x12);
		letter.entry.u8(0xF) = e.u8(0x14);
		break;
	case 3:
		letter.entry.u8(0xE) = e.u8(0x11);
		break;
	case 4:
		letter.entry.u8(0xE) = e.u8(0x12);
		break;
	default:
		break;
	}
}

// 0x4EBF54 (sobre la cola de eventos, con el índice del evento): empieza el
// encargo ([+0xF] = 1) y le pone el plazo según el tipo, con las cartas al
// comerciante: 0x80 (the road), 0x6E and 0x84 (the pirate nest).
void fn_4ebf54(SaveGame& save, uint32_t index) {
	GlobalMemory& g = save.globals;
	GameMemory<0x18>& e = save.events[index];
	if (e.u8(0xF) != 0) {
		// From the operation 0x4B on a task not given but started (the copy
		// of an expired road, 0x4EB0EB). Checked with the oracle: scenario hanse_ops (saves 3 and 5).
		return;
	}
	e.u8(0xF) = 1;
	const uint32_t now = g.u32(kTick);
	switch (e.u8(0xC)) {
	case 0: {
		// Funda la ciudad (new_town.cpp: 0x531D50) con el índice siguiente
		// ([+0x16]), su número [+0x14], las mercancías [+0x10] y la plantilla
		// [+0x15]; el comerciante es su alcalde, y un evento 0x39 para mañana.
		GameMemory<0x10> rec;
		rec.u32(0x4) = e.u32(0x10);
		rec.u8(0xC) = e.u8(0x14);
		rec.u8(0xD) = e.u8(0x15);
		const uint8_t town = uint8_t(g.u32(kTownCount));
		rec.u8(0xE) = town;
		e.u8(0x16) = town;
		fn_531d50(save, rec);
		save.towns[town].memory.u8(0x6F1) = save.events[index].u8(0xD);
		g.u32(kEventBusy) = 0;
		const uint16_t created = events_detail::fn_4d8cf0(save, g.u32(kTick) + 0x100);
		g.u32(kEventBusy) = 1;
		save.events[created].u16(0x6) = 0x39;
		save.events[created].u8(0x8) = town;
		break;
	}
	case 1: {
		// La carretera: el bit 0x8000 en las dos ciudades y (1,5 · días + 20)
		// días de plazo, con los días del par de 0x672BF8 (620 si no está).
		const uint32_t a = e.u8(0x10);
		const uint32_t b = e.u8(0x11);
		save.towns[a].memory.u32(0x76C) |= 0x8000;
		save.towns[b].memory.u32(0x76C) |= 0x8000;
		const uint8_t ida = g.u8(kTownIds + a);
		const uint8_t idb = g.u8(kTownIds + b);
		bool found = false;
		for (const uint8_t* pair : kRoads) {
			if ((pair[0] == ida && pair[1] == idb) || (pair[0] == idb && pair[1] == ida)) {
				const uint32_t days = pair[2];
				e.u32(8) = (((days >> 1) + days + 0x14) << 8) + now;
				found = true;
				break;
			}
		}
		if (!found) {
			e.u32(8) = now + 0x26C00;  // Checked with the oracle: scenario task_branches (saves 3 and 5).
		}
		// The letter 0x80 to the merchant (0x4EC115..0x4EC18F): [+0x5] the
		// Hanse town; [+0x8], [+0x9] the Alderman's names (0x5333C0); [+0xA],
		// [+0xB] the two towns, [+0xC] the merchant; [+0xD] the deadline's
		// day and u16 [+0xE] its year · 12 + month − 1.
		Letter letter(0x80);
		letter.entry.u8(5) = g.u8(kHanseTown);
		fn_5333c0(save, letter.entry.u8(8), letter.entry.u8(9));
		letter.entry.u8(0xA) = e.u8(0x10);
		letter.entry.u8(0xB) = e.u8(0x11);
		letter.entry.u8(0xC) = e.u8(0xD);
		uint8_t day = 0;
		uint8_t month = 0;
		uint16_t year = 0;
		events_mayor_detail::fn_5311d0(e.u32(8), day, month, year);
		letter.entry.u8(0xD) = day;
		letter.entry.u16(0xE) = uint16_t(year * 12 + month - 1);
		send_letter(save, e.u8(0xD), letter);
		break;
	}
	case 2:
		e.u32(8) = (now + 0x1E00) | 0xFF;
		break;
	case 3: {
		e.u32(8) = (now + 0x1E00) | 0xFF;
		e.u16(0x14) = 0xFFFF;
		// The letters 0x6E and 0x84 to the merchant (0x4EC1AD..0x4EC2B2): 0x6E
		// with [+0x5] the Hanse town, u16 [+0x8] 0, [+0xA] 0xFFFF, [+0xC],
		// [+0xE] the nest's x, y (the towns' table by the number [+0x11]);
		// then the same letter as 0x84 with u16 [+0x8] his rank at home << 8 +
		// his title, [+0xA] his [+0x1C] << 8 + [+0x1D], and the deadline:
		// [+0xC] its day, [+0xD] its month − 1, u16 [+0xE] its year.
		Letter letter(0x6E);
		letter.entry.u8(5) = g.u8(kHanseTown);
		letter.entry.u16(0xA) = 0xFFFF;
		letter.entry.u16(0xC) = uint16_t(g.u32(kTownTable + 0x20 + 0x34 * uint32_t(e.u8(0x11))));
		letter.entry.u16(0xE) = uint16_t(g.u32(kTownTable + 0x24 + 0x34 * uint32_t(e.u8(0x11))));
		send_letter(save, e.u8(0xD), letter);
		const uint32_t merchant = save.events[index].u8(0xD);
		const MerchantMemory& m = save.merchants[merchant].memory;
		letter.entry.u8(4) = 0x84;
		letter.entry.u16(8) = uint16_t((m.u8(0x39C + m.u8(0x19)) << 8) + merchants_detail::fn_4f9430(save, int(merchant)));
		letter.entry.u16(0xA) = uint16_t((m.u8(0x1C) << 8) + m.u8(0x1D));
		uint8_t day = 0;
		uint8_t month = 0;
		uint16_t year = 0;
		events_mayor_detail::fn_5311d0(save.events[index].u32(8), day, month, year);
		letter.entry.u8(0xC) = day;
		letter.entry.u8(0xD) = uint8_t(month - 1);
		letter.entry.u16(0xE) = year;
		send_letter(save, int32_t(merchant), letter);
		break;
	}
	case 4: {
		// Los habitantes que debe tener: los de 16 bits de ahora + 2000, en
		// centenas.
		const uint16_t people = uint16_t(save.towns[e.u8(0x12)].memory.u32(0x2D4) + 0x7D0);
		e.u32(8) = (now + 0x16D00) | 0xFF;
		e.u16(0x10) = uint16_t(people / 100 * 100);
		break;
	}
	default:
		break;  // Checked with the oracle: scenario task_branches (saves 3 and 5).
	}
}

// 0x4EC354 (sobre la cola de eventos: el evento de la cabeza): el encargo
// cumplido. El comerciante sube su [+0x37] (2 si ha fundado una ciudad), and
// the letter 0x6F (hanse_task_letter; [+0x8] with 0x80 when [+0x37] reaches
// 4) goes to the chronicle and to a human merchant; the ticker line (0x548CA0)
// is the interface's.
void fn_4ec354(SaveGame& save) {
	const GameMemory<0x18>& e = save.events[save.globals.u16(kEventHead)];
	const uint32_t merchant = e.u8(0xD);
	MerchantMemory& m = save.merchants[merchant].memory;
	Letter letter(0x6F);
	letter.entry.u8(8) = e.u8(0xC);
	if (++m.u8(0x37) == 4) {
		letter.entry.u8(8) |= 0x80;
	}
	hanse_task_letter(save, letter, e, merchant);
	if (e.u8(0xC) == 0) {
		// Checked with the oracle: scenario new_town_market (saves 3 and 5).
		if (++m.u8(0x37) == 4) {
			letter.entry.u8(8) |= 0x80;
		}
	}
	send_letter(save, kLetterToChronicle, letter);
	if (m.u16(8) == 0) {
		send_letter(save, int32_t(merchant), letter);
	}
}

// ¿Hay ya un edificio de la carretera entre los de la ciudad (lista [0x764],
// registros por 0x51F1F0)?
bool has_road_building(const SaveGame& save, uint32_t town) {
	const SavedTown& t = save.towns[town];
	const uint32_t count = t.memory.u16(0x776);
	for (uint32_t r = t.memory.u16(0x764); r < count; r = t.records[r].next) {
		if (t.records[r].type == kRoadBuilding) {
			return true;
		}
	}
	return false;
}

// Lo que el encargo de la carretera lleva de cada material ([+0x14 + k] y
// dos bits más en [+0x13]).
int32_t road_material(const GameMemory<0x18>& e, uint32_t k) {
	return int32_t((((uint32_t(e.u8(0x13)) >> (2 * k)) & 3) << 8) + e.u8(0x14 + k));
}

// 0x4EB44A..0x4EB718 (el encargo de la carretera, antes del plazo): mientras
// queden tramos ([+0x12]), cada día y medio, si el encargo tiene los
// materiales de uno (0x672BF0; los aporta el jugador con la operación 0x3B),
// los gasta y el tramo se hace; con el último se construye el edificio 0x27
// en las dos ciudades. Cerca del plazo, si tiene los materiales de lo que
// falta, el plazo se alarga 2 días. Sin tramos, cada 2 días mira si los dos
// edificios están terminados (el plazo se alarga lo que haga falta).
uint32_t road_progress(SaveGame& save, GameMemory<0x18>& e, uint32_t tick, uint32_t deadline) {
	const uint32_t a = e.u8(0x10);
	const uint32_t b = e.u8(0x11);
	if (a >= town_count(save) || b >= town_count(save)) {
		return 0;  // Checked with the oracle: scenario task_branches (saves 3 and 5).
	}
	if (e.u8(0x12) != 0) {
		e.u32(0) = tick + 0x180;
		for (uint32_t k = 0; k < 4; ++k) {
			if (road_material(e, k) < int32_t(kRoadSegment[k])) {
				return 1;
			}
		}
		for (uint32_t k = 0; k < 4; ++k) {
			const int32_t left = road_material(e, k) - int32_t(kRoadSegment[k]);
			e.u8(0x14 + k) = uint8_t(left);
			const uint8_t bits = uint8_t(~(3u << (2 * k)) & e.u8(0x13));
			e.u8(0x13) = uint8_t(bits | uint8_t((left & 0x300) >> (8 - 2 * k)));
		}
		--e.u8(0x12);
		if (e.u8(0x12) == 0) {
			// El edificio de la carretera en las dos ciudades, de la ciudad
			// (0xFF), en su solar (0x4F9C80, el resultado no se mira). El juego
			// empieza a buscar desde lo que haya en su pila y la segunda ciudad
			// desde lo que dejó la primera; como cada ciudad tiene una sola
			// celda de esa clase (o ninguna) y la espiral recorre toda la
			// rejilla, el resultado no depende de ello: aquí, desde la estatua.
			uint8_t x = 0xFF;
			uint8_t y = 0xFF;
			for (const uint32_t town : {a, b}) {
				construction_detail::fn_4f9c80(save, int(town), x, y, kRoadBuilding, 0, true);
				construction_detail::fn_521900(save, int(town), 0xFF, x, y, kRoadBuilding, 0);
			}
		}
		if (e.u32(0) >= e.u32(8)) {
			const int32_t segments = e.u8(0x12);
			for (uint32_t k = 0; k < 4; ++k) {
				if (road_material(e, k) < int32_t(kRoadSegment[k]) * segments) {
					return 1;
				}
			}
			e.u32(8) = e.u32(0) + 0x200;
		}
		return 1;
	}
	e.u32(0) = tick + 0x200;
	if (e.u32(0) >= deadline) {
		e.u32(8) = e.u32(0) + 1;
	}
	if (has_road_building(save, a) && has_road_building(save, b)) {
		fn_4ec354(save);
		return 0;
	}
	return 1;
}

// --- La guarida pirata (tipo 3) ---

// 0x51A750 (sobre el barco): sube `n` niveles al barco (sin pasar del 2):
// más casco ([+0x14]) y menos bodega ([+0x10]) por cada carga de su tipo
// (0x66E030 + 0x18 · tipo), y vuelve a calcular sus huecos de artillería
// (0x5181D0) y su bodega (0x5182B0).
void fn_51a750(SaveGame& save, int ship, uint8_t n) {
	static constexpr int32_t kLoadsPerLevel[4] = {3, 5, 8, 10};  // 0x66E030, 0x66E048, 0x66E060, 0x66E078
	ShipMemory& s = save.ships[size_t(ship)].memory;
	uint8_t levels = uint8_t(2 - s.u8(0xF));
	if (levels > n) {
		levels = n;
	}
	if (levels == 0) {
		// Not from 0x516170 (a new ship is at level 0) nor 0x50E000 (it stops
		// at 2); the game's other callers aren't rewritten yet.
		return;  // Checked with the oracle: scenario ship_artillery (saves 3 and 5, direct calls).
	}
	const int32_t loads = int32_t(levels) * kLoadsPerLevel[s.u8(0xE) & 3];
	s.u8(0xF) = uint8_t(s.u8(0xF) + levels);
	s.u16(0x3C) |= 0x104;
	s.s32(0x14) += loads * 2800;
	s.s32(0x10) -= loads * 2000;
	events_mayor_detail::fn_5181d0(save, ship);
	recalculate_cargo(save.ships[size_t(ship)]);
}

// 0x51A4E0 (sobre el barco): le pone artillería del nivel `level` (2..4: las
// piezas grandes, que ocupan dos huecos [+0x13C + 2i], marcado el segundo con
// 6; después las pequeñas, de nivel 1, en los huecos sueltos) mientras le
// quepa ([+0x10] − [+0x118] − [+0x11C], el peso en 0x672CB4) y su fuerza
// [+0x120] (0x672CC8) no llegue a `strength`. Con nivel < 2, nada.
void fn_51a4e0(SaveGame& save, int ship, uint32_t strength, uint8_t level) {
	static constexpr int32_t kWeight[6] = {1000, 1000, 2000, 2000, 2000, 1000};  // 0x672CB4 (u16)
	static constexpr uint32_t kPower[6] = {9, 10, 22, 24, 30, 18};  // 0x672CC8
	ShipMemory& s = save.ships[size_t(ship)].memory;
	int32_t space = s.s32(0x10) - s.s32(0x118) - s.s32(0x11C);
	if (strength <= s.u32(0x120) || space < kWeight[0] || level < 2) {
		return;  // Checked with the oracle: scenario ship_artillery (saves 3 and 5, direct calls).
	}
	if (level > 4) {
		// 0x52BB00 gives up to 5. Checked with the oracle: scenarios nest_reinforce and ship_artillery
		// (saves 3 and 5).
		level = 4;
	}
	auto add = [&](uint8_t piece) {
		s.s32(0x11C) += kWeight[piece];
		space -= kWeight[piece];
		s.u32(0x120) += kPower[piece];
	};
	auto remove = [&](uint8_t piece) {
		s.u32(0x120) -= kPower[piece];
		s.s32(0x11C) -= kWeight[piece];
		space += kWeight[piece];
	};
	// Las piezas grandes en los pares de huecos libres.
	for (uint32_t i = 0; i < 0x18; i += 2) {
		if (s.u8(0x13C + i) != 0xFF || s.u8(0x13D + i) != 0xFF) {
			continue;
		}
		if (kWeight[level] > space) {
			break;  // Checked with the oracle: scenario ship_artillery (saves 3 and 5, direct calls).
		}
		s.u8(0x13C + i) = level;
		s.u8(0x13D + i) = 6;
		add(level);
		if (strength <= s.u32(0x120)) {
			return;
		}
	}
	// Las piezas de menos nivel se cambian por una grande.
	for (uint32_t i = 0; i < 0x18; i += 2) {
		const uint8_t piece = s.u8(0x13C + i);
		if (piece >= level) {
			continue;
		}
		// Only for a ship that already has artillery: not from 0x516170 (a new
		// ship); the game's other callers aren't rewritten yet.
		// Checked with the oracle: scenario ship_artillery (saves 3 and 5, direct calls).
		const uint8_t second = s.u8(0x13D + i);
		if (second == 7 || (space < kWeight[0] && second > 7)) {
			continue;
		}
		remove(piece);
		if (level > second) {
			remove(second);
		}
		s.u8(0x13C + i) = level;
		s.u8(0x13D + i) = 6;
		add(level);
		if (strength <= s.u32(0x120)) {
			return;
		}
	}
	// Las pequeñas en los huecos sueltos.
	const uint8_t small = level > 1 ? 1 : level;
	for (uint32_t i = 0; i < 0x18; ++i) {
		if (s.u8(0x13C + i) != 0xFF) {
			continue;
		}
		if (kWeight[small] > space) {
			return;
		}
		s.u8(0x13C + i) = small;
		add(small);
		if (strength <= s.u32(0x120)) {
			return;
		}
	}
}

// 0x516170 (sobre el objeto pirata `pirate`): un barco pirata nuevo del tipo
// `type` (0x507D60, fuera de las listas) en su guarida, al principio de sus
// dos listas ([+0xC] por [+0x4] y [+0x12] por [+0x6]), del tamaño de la
// ciudad 0 (0x52A8D0), con nombre (0x509A50), un nivel más (0x51A750),
// artillería hasta `strength` del nivel de la ciudad del jefe de la Hansa
// (0x52BB00, 0x51A4E0), marineros (0x5184F0) y, con `captain`, capitán.
void fn_516170(SaveGame& save, uint32_t pirate, uint32_t type, bool captain, uint32_t strength) {
	GameMemory<0x18>& p = *save.pirates[pirate];
	uint16_t index = 0;
	events_mayor_detail::fn_507d60(save, index, 0, 3);
	ShipMemory& s = save.ships[index].memory;
	s.u16(0x4) = p.u16(0xC);
	p.u16(0xC) = index;
	s.u16(0x6) = p.u16(0x12);
	p.u16(0x12) = index;
	events_mayor_detail::fn_52a8d0(save, 0, index, type);
	events_mayor_detail::fn_5181d0(save, index);
	events_mayor_detail::fn_509a50(save, index);
	fn_51a750(save, index, 1);
	const uint32_t level = facilities_detail::fn_52bb00(save, save.globals.u8(kHanseTown));
	fn_51a4e0(save, index, strength & 0xFFFF, uint8_t(level));
	ShipMemory& m = save.ships[index].memory;
	const uint16_t sailors = uint16_t(ship_ai_detail::fn_5184f0(save, index));
	m.u16(0x40) = sailors;
	m.u16(0x154) = sailors;
	recalculate_cargo(save.ships[index]);
	m.u8(0x0) = 0xFF;
	m.u8(0x13A) = 0;
	m.u32(0x18) = m.u32(0x14);
	const uint32_t nest = p.u8(0xE);
	m.u16(0x1E) = uint16_t(save.globals.u32(kTownTable + 0x20 + 0x34 * nest));
	m.u16(0x22) = uint16_t(save.globals.u32(kTownTable + 0x24 + 0x34 * nest));
	if (captain) {
		const uint16_t c = events_detail::fn_5097c0(save, true);
		save.ships[index].memory.u16(0x42) = c;
	}
}

// 0x5144A0 (sobre el objeto pirata `pirate`, con su guarida `nest`): si es
// la suya y el pirata está en ella ([+0x0] = 0), completa sus barcos de la
// lista [+0x12] hasta los de la dificultad de los piratas (ajustes [0x13],
// 0x673658), con la fuerza de [+0x14] · 40 + 100.
void fn_5144a0(SaveGame& save, uint32_t pirate, uint32_t nest) {
	static constexpr uint8_t kPirateShips[4] = {4, 8, 16, 18};  // 0x673658
	GameMemory<0x18>& p = *save.pirates[pirate];
	if (uint32_t(p.u8(0xE)) != (nest & 0xFFFF) || p.u32(0) != 0) {
		// The other pirates, and the lair's pirate when it is out. Checked with the oracle: scenarios
		// nest_reinforce and nest_reinforce_away (saves 3 and 5).
		return;
	}
	const uint8_t difficulty = save.settings.u8(0x13);
	int32_t missing = difficulty < 4 ? kPirateShips[difficulty] : 0;
	const GlobalMemory& g = save.globals;
	for (uint32_t s = p.u16(0x12); s < g.u16(kShipCount); s = save.ships[s].memory.u16(0x6)) {
		--missing;
	}
	for (; missing > 0; --missing) {
		const uint32_t type = (uint32_t(g.u8(global::kWorldDayOfYear)) + uint32_t(missing)) & 3;
		fn_516170(save, pirate, type, true, uint32_t(save.pirates[pirate]->u8(0x14)) * 40 + 100);
	}
}

// La distancia² de un barco a la guarida `nest` (la posición de su número de
// StadtDaten.ini), en píxeles del mapa.
int32_t nest_distance(const SaveGame& save, const ShipMemory& ship, uint32_t nest) {
	const uint32_t dx = uint32_t(int32_t(ship.s16(0x1E))) - save.globals.u32(kTownTable + 0x20 + 0x34 * nest);
	const uint32_t dy = uint32_t(int32_t(ship.s16(0x22))) - save.globals.u32(kTownTable + 0x24 + 0x34 * nest);
	return int32_t(dy * dy + dx * dx);
}

// 0x4EB85D..0x4EBE50 (el encargo de la guarida, antes del plazo), por fases
// en [+0x12]: 0 espera a que un barco del comerciante se acerque a la
// guarida (distancia² < 200; las esperas, de 2 a 64 ticks según lo cerca que
// esté el más cercano) y entonces pasa a la 1, apunta la guarida en su
// [+0xE1] y los piratas se refuerzan (0x5144A0); 1 espera a que uno llegue
// justo a la guarida (de 1 a 31 ticks) y la ataca: una batalla (0x6130B0,
// 0x611D80, 0x5FBFE0), por reescribir; 2 (la guarida destruida, lo pone la
// batalla) cumplido; 3 o más, fallido. Sin el pirata o con otra guarida, la
// fase 3.
uint32_t nest_progress(SaveGame& save, GameMemory<0x18>& e, uint32_t tick) {
	const GlobalMemory& g = save.globals;
	const uint32_t merchant = e.u8(0xD);
	const uint32_t pirate = e.u8(0x10);
	const uint32_t nest = e.u8(0x11);
	const bool valid = pirate < 5 && g.u32(kPirates + 4 * pirate) != 0 && save.pirates[pirate] &&
		save.pirates[pirate]->u8(0xE) == nest;
	switch (e.u8(0x12)) {
	case 0: {
		if (!valid) {
			e.u32(0) = tick + 0x100;  // Checked with the oracle: scenario nest_branches (saves 3 and 5).
			e.u8(0x12) = 3;
			return 1;
		}
		int32_t wait = 0x40;
		for (uint32_t s = save.merchants[merchant].memory.u16(0xE); s < g.u16(kShipCount);) {
			const ShipMemory& ship = save.ships[s].memory;
			s = ship.u16(0x4);
			const int32_t d2 = nest_distance(save, ship, nest);
			if (d2 < 0xC8) {
				e.u8(0x12) = 1;
				save.merchants[merchant].memory.u8(0xE1) = uint8_t(nest);
				for (uint32_t k = 0; k < 5; ++k) {
					if (g.u32(kPirates + 4 * k) != 0 && save.pirates[k]) {
						fn_5144a0(save, k, nest);
					}
				}
			} else if (d2 < 0x1000) {
				const int32_t step = d2 > 0x400 ? 0x16 : d2 > 0x200 ? 0xC : d2 > 0x100 ? 6 : 2;
				if (step < wait) {
					wait = step;
				}
			}
		}
		e.u32(0) = tick + uint32_t(wait);
		return 1;
	}
	case 1: {
		if (!valid) {
			e.u32(0) = tick + 1;  // Checked with the oracle: scenario nest_branches (saves 3 and 5).
			e.u8(0x12) = 3;
			return 1;
		}
		// El barco que ataca ([+0x14]) ya no está en la guarida: ninguno.
		const uint32_t attacker = e.u16(0x14);
		if (attacker < g.u16(kShipCount)) {
			// [+0x14] is set by the attack (a battle, not rewritten).
			// Checked with the oracle: scenario nest_branches (saves 3 and 5).
			const ShipMemory& ship = save.ships[attacker].memory;
			if (int32_t(ship.s16(0x1E)) != g.s32(kTownTable + 0x20 + 0x34 * nest) ||
				int32_t(ship.s16(0x22)) != g.s32(kTownTable + 0x24 + 0x34 * nest)) {
				e.u16(0x14) = 0xFFFF;
			}
		}
		uint32_t wait = 0x1F;
		for (uint32_t s = save.merchants[merchant].memory.u16(0xE); s < g.u16(kShipCount);
			 s = save.ships[s].memory.u16(0x4)) {
			const ShipMemory& ship = save.ships[s].memory;
			if (s == e.u16(0x14) || ship.u16(0x134) != 0xF) {
				continue;
			}
			const int32_t d2 = nest_distance(save, ship, nest);
			if (d2 == 0) {
				// Por reescribir: el barco (o su convoy) ataca la guarida
				// (estado 0x14, 0x50A4E0/0x505B60, 0x50D040, 0x5062F0,
				// 0x50E280) y empieza una batalla (0x515D00, 0x6130B0,
				// 0x60234C o 0x514530, 0x611D80, 0x5FBFE0), que el emulador
				// no ejecuta; [+0x14] es el barco. El juego vuelve a mirar a
				// los 64 ticks.
				e.u32(0) = tick + 0x40;
				return 1;
			}
			if (d2 < 0x40) {
				wait = 1;
			} else if (d2 < 0xE1) {
				wait &= 3;
			} else if (d2 < 0x640) {
				wait &= 7;
			}
		}
		e.u32(0) = tick + wait;
		return 1;
	}
	case 2:
		save.merchants[merchant].memory.u8(0xE1) = 0xFF;
		fn_4ec354(save);
		return 0;
	default:
		// Fallido: al jugador humano, los avisos y la ventana (no se reescriben).
		save.merchants[merchant].memory.u8(0xE1) = 0xFF;
		return 0;
	}
}

}  // namespace alderman_detail

// La parte de 0x4EAFE4 (el evento 0x32 de la cabeza) con comerciante
// ([+0xD] < nº de comerciantes; desde 0x4EB0EB): devuelve lo que hace el
// despachador con el evento (0 quitarlo, 1 recolocarlo por su tick).
uint32_t fn_4eb0eb(SaveGame& save) {
	using namespace alderman_detail;
	GlobalMemory& g = save.globals;
	GameMemory<0x18>* e = &save.events[g.u16(kEventHead)];
	if (e->u8(0xF) == 0) {
		g.u32(kEventBusy) = 0;
		fn_4ebf54(save, g.u16(kEventHead));
		g.u32(kEventBusy) = 1;
		save.events[g.u16(kEventHead)].u32(0) += 0x100;
		return 1;
	}
	const uint32_t tick = e->u32(0);
	const uint32_t deadline = e->u32(8);
	if (tick >= deadline) {
		// Vencido: el castigo y, al jugador humano, los avisos; el encargo
		// pasa al tipo 5 y se quita a los 28 días.
		const uint32_t merchant = e->u8(0xD);
		fn_4f9170(save, int(merchant));
		if (save.merchants[merchant].memory.u16(8) == 0) {
			// The letter 0x70 to him (0x4EB16B..0x4EB32B: hanse_task_letter,
			// [+0x8] the kind), sent after what the kind does below.
			Letter letter(0x70);
			letter.entry.u8(8) = e->u8(0xC);
			hanse_task_letter(save, letter, *e, merchant);
			switch (e->u8(0xC)) {
			case 1: {
				// Un encargo nuevo igual, con la ciudad [+0x10] en [+0xD].
				// Game bug: [+0x10] is a town, not a merchant. With a town
				// index under the number of merchants the copy belongs to that
				// merchant: it never expires (the sections build themselves),
				// the road gets built and, when it is done, 0x4EC354 counts it
				// for that merchant's candidacy. With fix_game_bugs() the copy
				// has no merchant (0xFF): as the two towns keep the bit 0x8000
				// of the road, the game's own check (0x532C50) removes it on
				// its first turn, as with a town index past the merchants.
				g.u32(kEventBusy) = 0;
				const uint16_t created = events_detail::fn_4d8cf0(save, e->u32(0) + 1);
				g.u32(kEventBusy) = 1;
				e = &save.events[g.u16(kEventHead)];
				GameMemory<0x18>& c = save.events[created];
				c.u16(6) = e->u16(6);
				c.u32(8) = e->u32(0) + 0xBA00;
				c.u8(0xC) = 1;
				c.u8(0xD) = fix_game_bugs() ? 0xFF : e->u8(0x10);
				c.u8(0xE) = e->u8(0xE);
				c.u8(0xF) = e->u8(0xF);
				c.u8(0x10) = e->u8(0x10);
				c.u8(0x11) = e->u8(0x11);
				c.u8(0x12) = e->u8(0x12);
				c.u8(0x13) = 0xFF;
				c.u32(0x14) = 0xFFFFFFFF;
				break;
			}
			case 3:
				save.merchants[e->u8(0xD)].memory.u8(0xE1) = 0xFF;
				break;
			default:
				break;
			}
			send_letter(save, save.events[g.u16(kEventHead)].u8(0xD), letter);
			// The ticker line (0x548CA0) and, for the player, the window
			// (0x469380, 0x469AF0): the interface's.
		}
		e->u8(0xC) = 5;
		e->u32(0) = g.u32(kTick) + 0x1C00;
		e->u32(8) = e->u32(0) + 1;
		return 1;
	}
	switch (e->u8(0xC)) {
	case 0:
		// La ciudad nueva: se cumple cuando tiene mercado. (Mientras el
		// principio no cree la ciudad, [+0x16] no es una: el juego leería
		// fuera.)
		// Checked with the oracle: scenario new_town_market (saves 3 and 5).
		if (e->u8(0x16) < town_count(save) && (save.towns[e->u8(0x16)].memory.u32(0x76C) & 0x100)) {
			fn_4ec354(save);
			return 0;
		}
		e->u32(0) = tick + 0x200;
		return 1;
	case 1:
		return road_progress(save, *e, tick, deadline);
	case 2:
		// El barco pirata: cada 4 días, que siga siendo pirata con el mismo
		// capitán (si no, fallido). [+0x16] = 1 lo pone otra parte del juego
		// (0x4EC864, desde las batallas).
		if (e->u8(0x16) == 0) {
			bool same = false;
			const uint32_t s = e->u16(0x10);
			if (s < g.u16(kShipCount)) {
				const ShipMemory& ship = save.ships[s].memory;
				const uint32_t c = ship.u16(0x42);
				if (ship.u16(0x134) == 0x12 && c < g.u16(kCaptainCount)) {
					const CaptainMemory& captain = save.captains[c];
					same = captain.u8(2) == e->u16(0x12) && captain.u8(3) == e->u16(0x14);
				}
			}
			if (!same) {
				e->u8(0x16) = 2;
			}
			e->u32(0) = tick + 0x400;
			return 1;
		}
		if (e->u8(0x16) == 1) {
			fn_4ec354(save);  // Checked with the oracle: scenario task_branches (saves 3 and 5).
			return 0;
		}
		// Fallido: al jugador humano, los avisos y la ventana (no se reescriben).
		return 0;
	case 3:
		return nest_progress(save, *e, tick);
	case 4:
		// La ciudad: se cumple al llegar a los habitantes; si no, se mira
		// cada semana y el último día antes del plazo.
		if (save.towns[e->u8(0x12)].memory.s32(0x2D4) >= int32_t(e->u16(0x10))) {
			fn_4ec354(save);
			return 0;
		}
		if (tick + 0x700 <= deadline) {
			e->u32(0) = tick + 0x700;
			return 1;
		}
		e->u32(0) = tick + 1;
		if (tick + 1 < deadline) {
			e->u32(0) = deadline - 1;
		}
		return 1;
	default:
		// 5 (anulado o fallido): se quita (la interfaz, 0x6E558C, no).
		return 0;
	}
}

// 0x53A3E0 (operación 0x3B: u16 [+0x4] evento, u16 [+0x6] cantidad, u8 [+0x8]
// ciudad, u8 [+0x9] comerciante, u8 [+0xA] material 0..3 o mercancía): el
// comerciante aporta materiales a su encargo de la carretera con mercado en
// esa ciudad. La cantidad (en cargas o barriles, 0x672C14) sale primero de su
// oficina en la ciudad y después de sus barcos atracados en ella, y lo que
// consigue se suma al contador del material en el encargo.
// Game bug (reproduced): with a merchant out of the world the amount missing is
// the event's address (see below).
void fn_53a3e0(SaveGame& save, const uint8_t* operation) {
	Operation op;
	std::memcpy(op.bytes.data(), operation, op.bytes.size());
	const GlobalMemory& g = save.globals;
	const uint32_t index = op.u16(4);
	const uint32_t town = op.u8(8);
	// Each check: checked with the oracle, scenario hanse_ops (saves 3 and 5).
	if (index >= g.u16(kEventCount) || town >= town_count(save)) {
		return;
	}
	GameMemory<0x18>& e = save.events[index];
	if (e.u16(6) != 0x32 || e.u8(0xC) != 1 || e.u8(0xD) != op.u8(9) ||
		(e.u8(0x10) != town && e.u8(0x11) != town) || e.u8(0x12) == 0) {
		return;
	}
	// 0x672BF4: la mercancía de cada material (madera, ladrillos,
	// herramientas, cáñamo). [+0xA] es el material o su mercancía; si no es
	// ninguna, el último.
	static constexpr uint8_t kRoadWares[4] = {0x0B, 0x13, 0x0C, 0x11};
	// 0x672C14: las mercancías que van en barriles.
	static constexpr uint8_t kInBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
	uint32_t k = 0;
	uint32_t ware = 0;
	const uint8_t given = op.u8(0xA);
	if (given < 4) {
		k = given;
		ware = kRoadWares[given];
	} else {
		k = 0;
		ware = kRoadWares[3];
		while (k < 3 && kRoadWares[k] != given) {
			++k;
		}
		if (k < 3) {
			ware = given;
		}
	}
	const int32_t unit = kInBarrels[ware] ? 200 : 2000;
	const uint32_t merchant = op.u8(9);
	if (merchant >= g.u16(kMerchantCount)) {
		// Game bug: with a merchant out of the world (only the interface or a
		// network game could send one, on a road task not given yet, whose
		// [+0xD] is 0xFF) nothing is taken, and the game uses as the amount
		// still missing the address of the event (0x53A619: edi is still the
		// event pointer): the counter gets amount − address, its low 10 bits.
		// The address is the queue's array (*0x6DD73C) + 0x18 · index.
		// Checked with the oracle: scenario hanse_ops (saves 3 and 5).
		// With fix_game_bugs() nothing is given, so the task is left as it is.
		if (fix_game_bugs()) {
			return;
		}
		const uint32_t address = g.u32(kEvents) + 0x18 * index;
		const uint32_t total = uint32_t(alderman_detail::road_material(e, k)) + op.u16(6) - address;
		e.u8(0x14 + k) = uint8_t(total);
		e.u8(0x13) = uint8_t(((total & 0x300) >> (8 - 2 * k)) | (~(3u << (2 * k)) & e.u8(0x13)));
		return;
	}
	int32_t missing = op.u16(6);
	const int office = facilities_detail::fn_5308a0(save, merchant, town);
	if (office >= 0) {
		int32_t& stock = save.offices[size_t(office)].memory.s32(0x4 + 4 * ware);
		int32_t n = stock / unit;
		if (n > missing) {
			n = missing;
			missing = 0;
		} else {
			missing -= n;
		}
		stock -= n * unit;
	}
	for (uint32_t s = save.merchants[merchant].memory.u16(0xE); s < g.u16(kShipCount) && missing > 0;
		 s = save.ships[s].memory.u16(0x4)) {
		ShipMemory& ship = save.ships[s].memory;
		if (ship.u8(0x39) != town || (ship.u8(0x136) & 1) || ship.s32(0x54 + 4 * ware) <= 0) {
			continue;
		}
		int32_t& cargo = ship.s32(0x54 + 4 * ware);
		int32_t n = cargo / unit;
		if (n < missing) {
			missing -= n;
		} else {
			n = missing;
			missing = 0;
		}
		cargo -= n * unit;
		recalculate_cargo(save.ships[s]);
	}
	const int32_t total = alderman_detail::road_material(e, k) + int32_t(op.u16(6)) - missing;
	e.u8(0x14 + k) = uint8_t(total);
	e.u8(0x13) = uint8_t(((total & 0x300) >> (8 - 2 * k)) | (~(3u << (2 * k)) & e.u8(0x13)));
}

// 0x53B440 (operación 0x4B: {0x4B, comerciante, evento}): un candidato que
// aún no tiene encargo coge uno sin dar y lo empieza (0x4EBF54).
void fn_53b440(SaveGame& save, const uint8_t* operation) {
	using namespace alderman_detail;
	Operation op;
	std::memcpy(op.bytes.data(), operation, op.bytes.size());
	const GlobalMemory& g = save.globals;
	const uint32_t merchant = op.u32(4);
	const uint32_t index = op.u32(8);
	// Each check: checked with the oracle, scenario hanse_ops (saves 3 and 5).
	if (merchant >= g.u16(kMerchantCount) || index >= g.u16(kEventCount)) {
		return;
	}
	GameMemory<0x18>& e = save.events[index];
	if (e.u16(6) != 0x32 || e.u8(0xD) < g.u16(kMerchantCount) || e.u8(0xC) >= 5) {
		return;
	}
	if (save.merchants[merchant].memory.u8(0x37) == 0 || fn_4ec694(save, merchant) != 0xFFFF) {
		return;
	}
	e.u8(0xD) = uint8_t(merchant);
	fn_4ebf54(save, index);
}

}  // namespace p3::data::tick
