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

#include <cstring>

#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

namespace events_rare_detail {
bool fn_532c50(const SaveGame& save, uint32_t index);
}  // namespace events_rare_detail

namespace {

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
				return 0;  // Sin comprobar con el oráculo.
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
	for (uint8_t k = 0; k < 5; ++k) {  // Sin comprobar con el oráculo.
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
				continue;  // Sin comprobar con el oráculo.
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
				continue;  // Sin comprobar con el oráculo.
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
				continue;  // Sin comprobar con el oráculo.
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
		return;  // Sin comprobar con el oráculo.
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

}  // namespace p3::data::tick
