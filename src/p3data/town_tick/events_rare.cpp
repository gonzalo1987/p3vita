// La cola de eventos (events.cpp): los tipos 0x0E (0x4E4A44), 0x12 (0x4E6A54), 0x1C (0x4E9094), 0x1E (0x4DFD44), 0x1F (0x4E0334), 0x25 (0x4E1B84), 0x26 (0x4E1E54), 0x2E (0x4E9A94), 0x30 (0x4EA594), 0x31 (0x4EAB94), 0x32 (0x4EAFE4), 0x33 (0x4EC934), 0x35 (0x4E94A4) y 0x38 (0x4ECB14).
//
// Lo que hace el despachador (0x4D85C0) con cada uno tras su manejador: los
// tipos 0x0E, 0x12, 0x1C, 0x1E, 0x1F, 0x25, 0x26, 0x2E, 0x30, 0x32 y 0x38 deciden por lo que
// devuelve el manejador (0: quitarlo, 1: llevarlo a su sitio por su tick ya
// cambiado); el 0x31, el 0x33 y el 0x35 se quitan siempre. Los avisos (0x548CA0, las
// ventanas de *0x6CC7E8 y *0x6CBB40) no se traducen; the letters (0x4D6530) are sent (mail.cpp).
//
//   0x0E  el final de la partida: el objetivo [+0x8] (1..6) con el plazo
//         [+0xA] (años o habitantes, 0x4E55FC). Al cumplirse o acabarse,
//         la ventana del final (interfaz: solo [0x6DFC04] = 0) y se quita.
//   0x12  un convoy ([+0x8], *0x6DD7A8) a puntos fuera del mapa (una
//         expedición; [+0xC] el tick en que salió): con [+0xA] = 0xFFFF
//         calcula cuándo vuelve por su ruta y pasa a [+0xA] = 4. Al llegar,
//         en cada punto de su ruta apunta lo visto, encuentra y comercia con
//         los 15 objetos del mundo (0x52BE90, 0x52D0B0, 0x52BFE0, 0x52C6C0 y
//         sus encargos), el viaje desgasta los barcos, puede haber piratas o
//         una tormenta (el generador 0x46C320..0x46C380) y el convoy vuelve
//         por el oeste (0x502B20, miembro de Sailing). Ver fn_4e6a54.
//   0x1C  la peste en la ciudad [+0x16]: mueren pobres (trabajadores de las
//         empresas) y mendigos; el bit 0x8 de la situación.
//   0x1E  piratas ante la ciudad [+0x11] (estados [+0x10] 0..4; los crea el 0x1F).
//   0x1F  la temporada de piratas (marzo a octubre): elige ciudades
//         ([+0x8..+0xF]), crea sus eventos 0x1E y encola la operación 0x84;
//         desde noviembre encola la 0x7E y se quita.
//   0x25, 0x26  los incendios del asedio: arde un objetivo caído o un
//         edificio de fuera de la muralla (ver fn_4e1b84 y fn_4e1e54).
//   0x2E  la votación del consejo de la ciudad [+0xC] (propuesta [+0xD] 0..3).
//   0x31  el impuesto extraordinario que aprueba el consejo (ver fn_4eab94).
//   0x38  el monumento al alcalde reelegido (ver fn_4ecb14).
//   0x30  el encargo del jefe de la Hansa ([0x6DE52D]): sin jefe humano, la
//         reunión (evento 0x21 en [0x6DE52E]); con él, plazos y reputación.
//   0x32  los encargos de la Hansa para un comerciante de la IA ([+0xD]); el
//         del comerciante no está traducido (ver fn_4eafe4).
//   0x33  un robo en una oficina: hasta tres mercancías pasan de ella al
//         mercado de la ciudad (ver fn_4ec934).
//   0x35  quita el bit 0x4000000 de la situación de la ciudad [+0x8].
//
// Los 15 objetos del mundo ([+0xB10] del mundo, 0x6DEFB0, 0x2C bytes): u8
// [+0x0], [+0x1] las mercancías que vende (0xFF ninguna; < 0x18), u8 [+0x2],
// [+0x3] las que compra, u8 [+0x4] su sitio (tabla 0x673858; >= 0x7F no
// existe), u8 [+0x5] tipo (0..2; el 2 no vende), u8 [+0x6] el comerciante que
// lo encontró (0xFF nadie), u32 [+0x8] tick desde el que la ruta tiene que
// pasar por su sitio exacto (0xFFFFFFFF hasta que alguien llega; antes basta
// con estar cerca), s32 [+0xC], [+0x10] lo que le queda de lo que vende, s32
// [+0x14], [+0x18] lo que tiene de lo que compra (le caben 100000, 200000 o
// 60000 según el tipo, 0x673984), u16 [+0x1C] y u32 [+0x28] el número de
// encargos de su lista [+0x24] (SaveGame::world_object_lists).
// Del convoy, además de navigation.md §9: [+0x24..+0x33] la ruta de la
// expedición (4 puntos u16 x, u16 y; x >= 0x7FFF sin punto), u8 [+0x34 + k]
// qué compra en el punto k (bit 0 la [+0x0] del objeto, con el bit 1 solo
// con la mitad del sitio; bit 1 la [+0x1]), u16 [+0x38] el modo de la
// expedición (0 la IA: va más lenta, desgasta dos tercios y busca más lejos;
// 1 precios con flag; 2 crea encargos; 0xFFFF fuera de una expedición).
// Del comerciante: [+0x464] float (el factor de la reputación de
// merchants.cpp) lo que sube su reputación en cada ciudad al encontrar un
// objeto, [+0x468] (bits de ciudad de merchants.cpp) distinto de 0: lo visto
// se apunta también en 0x6DF278, [+0x4B0] y [+0x4B4] suman lo vendido y lo
// comprado en los objetos, [+0x4D8..+0x53B] los bits de expediciones (como
// 0x6DF278).
//
// Campos de la ciudad que se usan aquí: [+0x2C0] u8 índice, [+0x2C6] u16,
// [+0x2C8] u32 situación (bit 0x8 peste, 0x10, 0x40 sin piratas, 0x600 (con
// él los piratas del 0x1E pueden pasar al estado 4), 0x2000000, 0x4000000), [+0x2D4] u32
// habitantes (suma de [+0x2D8], [+0x2DC], [+0x2E0] y [+0x2E4]: ricos,
// acomodados, pobres y mendigos), [+0x300..+0x30C] s16 satisfacción de cada
// clase, [+0x430 + 4·m] s32 lo que falta de la mercancía m, [+0x6DC..+0x6DF]
// concejales, [+0x6F0] u8 (el 0x1F apunta la mercancía que falta o 0; el 0x1E lo deja a 0xFF),
// [+0x6F1] u8 alcalde, [+0x6F4] u32 y [+0x6F8] u8 impuestos, [+0x766] u16
// obras pendientes, [+0x76C] u32, [+0x784] u16 primera oficina, [+0x78C],
// [+0x78E] u16, [+0x7A4] town_class1 ([+0x7B0 + 2·i] puntos de la muralla),
// [+0x840 + 16·k] instalaciones ([+0x4] u16 trabajadores, [+0x8] u16;
// el 0x1F mira si pasa de 950), [+0x9A0] u16.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "p3data/game_fixes.hpp"
#include "p3data/game_texts.hpp"
#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {

// Traducciones de events.cpp que se usan aquí.
namespace events_detail {
void fn_4d8dd0(SaveGame& save);
uint16_t fn_4d8cf0(SaveGame& save, uint32_t tick);
int16_t fn_4f73d0(const SaveGame& save, int merchant);
bool fn_4fe150(const CaptainMemory& c);
}  // namespace events_detail
namespace merchants_detail {
uint8_t fn_4f9430(const SaveGame& save, int merchant);
}  // namespace merchants_detail
namespace construction_detail {
void fn_4d9fc0(const SaveGame& save, uint32_t& tick, uint32_t town);
uint16_t fn_51f100(SaveGame& save, int town);
}  // namespace construction_detail

namespace {

using events_detail::fn_4d8cf0;

constexpr uint32_t kEvents = 0x6DD73C;
constexpr uint32_t kEventBusy = kEvents + 0x4;  // u32 1 mientras 0x4D85C0 atiende la cola.
constexpr uint32_t kEventHead = kEvents + 0x8;  // u16
constexpr uint32_t kEventCount = kEvents + 0xC;  // u16

constexpr uint32_t kTownCount = global::kWorldTownCount;  // u32
constexpr uint32_t kMerchantCount = global::kWorldMerchantCount;  // u16
constexpr uint32_t kOfficeCount = global::kWorldOfficeCount;  // u16 (u32 [0x6DE4A8] & 0xFFFF)
constexpr uint32_t kWorldRecordCount = 0x6DE4A6;  // u16
constexpr uint32_t kTick = global::kWorldTick;  // u32
constexpr uint32_t kDayOfYear = global::kWorldDayOfYear;  // u16
constexpr uint32_t kMonth = global::kWorldMonth;  // u8
constexpr uint32_t kStartTick = 0x6DE4AC;  // u32 ([+0xC] del mundo): tick del principio de la partida.
constexpr uint32_t kTownIds = 0x6DE4B8;  // u8 por ciudad ([+0x18] del mundo): su número de StadtDaten.ini.
constexpr uint32_t kTownTable = 0x6DDB90;  // 0x34 bytes por número de ciudad: [+0x20], [+0x24] s32 x, y en el mapa.
constexpr uint32_t kShipCount = global::kShipCount;  // u16
constexpr uint32_t kCaptainCount = global::kCaptainCount;  // u16
constexpr uint32_t kFreeCaptain = global::kShips + 0xE0;  // u16 primer capitán libre.
constexpr uint32_t kPirateRange = global::kShips + 0xFC;  // u16 (0x6DD89C): radio² / 64 de 0x1E.
constexpr uint32_t kAldermanBusy = 0x6DF31C;  // u32 (ver merchants.cpp)
constexpr uint32_t kGameOver = 0x6DFC04;  // u32: el final de la partida (0x0E) lo pone a 0.
constexpr uint32_t kPirates = global::kShips + 0xC;  // 0x6DD7AC: 5 punteros a los objetos piratas (0x18 bytes).
constexpr uint32_t kHanseLeader = 0x6DE52D;  // u8 ([+0x8D] del mundo): el jefe de la Hansa (comerciante).
constexpr uint32_t kHanseTown = 0x6DE52E;  // u8 ([+0x8E] del mundo): la ciudad de la reunión de la Hansa.

using Event = GameMemory<0x18>;

uint32_t town_count(const SaveGame& save) {
	return save.globals.u32(kTownCount);
}

uint32_t merchant_count(const SaveGame& save) {
	return save.globals.u16(kMerchantCount);
}

uint32_t office_count(const SaveGame& save) {
	return save.globals.u16(kOfficeCount);
}

uint32_t ship_count(const SaveGame& save) {
	return save.globals.u16(kShipCount);
}

Event& head_event(SaveGame& save) {
	return save.events[save.globals.u16(kEventHead)];
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

float as_float(uint32_t bits) {
	float f;
	std::memcpy(&f, &bits, 4);
	return f;
}

// __ftol a 32 bits.
int32_t ftol32(double v) {
	return int32_t(int64_t(v));
}

// Campos del objeto pirata `k` (*[0x6DD7AC + 4·k], que el llamador ya ha
// visto que no es nulo). Sin sus datos en SaveGame::pirates, 0xFF y 0.
uint8_t pirate_u8(const SaveGame& save, uint32_t k, size_t offset) {
	return save.pirates[k] ? save.pirates[k]->u8(offset) : 0xFF;
}

uint16_t pirate_u16(const SaveGame& save, uint32_t k, size_t offset) {
	return save.pirates[k] ? save.pirates[k]->u16(offset) : 0;
}

// 0x515AE0 (on the pirate object `k`, with two outputs): the names of the
// captain of its first ship (the chain from [+0xC], next [+0x4]) with a
// captain ([+0x42]) that 0x4FE150 accepts: his [+0x2] in `first` and [+0x3]
// in `family`, and true. With none, 0x2D and the number of family names
// ([0x6DDB74]), and false.
bool fn_515ae0(const SaveGame& save, uint32_t k, uint8_t& first, uint8_t& family) {
	const GlobalMemory& g = save.globals;
	for (uint32_t s = pirate_u16(save, k, 0xC); s < ship_count(save); s = save.ships[s].memory.u16(4)) {
		const uint16_t c = save.ships[s].memory.u16(0x42);
		if (c < g.u16(kCaptainCount) && events_detail::fn_4fe150(save.captains[c])) {
			first = save.captains[c].u8(2);
			family = save.captains[c].u8(3);
			return true;
		}
	}
	first = 0x2D;
	family = g.u8(0x6DDB74);
	return false;
}

// x, y en el mapa de la ciudad `town` (tabla de StadtDaten.ini por su número).
int32_t town_x(const SaveGame& save, uint32_t town) {
	return save.globals.s32(kTownTable + 0x20 + 0x34 * uint32_t(save.globals.u8(kTownIds + town)));
}

int32_t town_y(const SaveGame& save, uint32_t town) {
	return save.globals.s32(kTownTable + 0x24 + 0x34 * uint32_t(save.globals.u8(kTownIds + town)));
}

// ¿Cuenta el comerciante para el final de la partida? (ajustes [+0xD] == 3,
// o un jugador humano).
bool plays_goal(const SaveGame& save, const MerchantMemory& m) {
	return save.settings.u8(0xD) == 3 || m.u16(8) == 0;
}

}  // namespace

namespace events_rare_detail {

// 0x5098B0 (sobre el objeto de los barcos): devuelve a la lista de libres
// ([0x6DD880]) el capitán *`captain` (si es válido y no está ya en ella; si
// lo está, *`captain` = 0xFFFF). *`captain` se queda con el [+0x0] que tenía el capitán.
void fn_5098b0(SaveGame& save, uint16_t& captain) {
	GlobalMemory& g = save.globals;
	const uint16_t count = g.u16(kCaptainCount);
	const uint16_t c = captain;
	if (c >= count) {
		return;
	}
	for (uint16_t i = g.u16(kFreeCaptain); i < count; i = save.captains[i].u16(0)) {
		if (i == c) {
			captain = 0xFFFF;
			return;
		}
	}
	captain = save.captains[c].u16(0);
	save.captains[c].u16(0) = g.u16(kFreeCaptain);
	g.u16(kFreeCaptain) = c;
}

// --- Evento 0x0E: el final de la partida ---

// Plazos del objetivo según [+0xA] (0x4E4A7C): años en ticks (365 · 256 por
// año) para 0..13, habitantes para 16..18; 14, 15 y > 18 no tienen.
constexpr uint32_t kGoalLimit[19] = {0x16D00, 0x2DA00, 0x44700, 0x5B400, 0x72100, 0x88E00, 0x9FB00, 0xB6800,
	0xCD500, 0xE4200, 0x156300, 0x1C8400, 0x2AC600, 0x474A00, 0, 0, 0x11170, 0x186A0, 0x30D40};
// Días de cada mes (0x672D94, u16).
constexpr uint16_t kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// 0x4E4A44 (evento 0x0E): el objetivo de la partida [+0x8] con el plazo
// [+0xA]. Mientras no se cumpla ni se acabe el plazo, vuelve a mirar más
// tarde (devuelve 1 con el tick nuevo); si se cumple o se acaba, el juego
// abre la ventana del final (interfaz: no se traduce; solo pone [0x6DFC04]
// a 0) y devuelve 0.
uint32_t fn_4e4a44(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	Event& e = save.events[event];
	const uint32_t index = e.u16(0xA);
	// Without a limit (14, 15, > 18) the game uses an uninitialized local ([esp+0x10]; here 0).
	// Checked with the oracle with the function's frame zeroed: scenario game_end_branches (saves
	// 1, 3, 5, 7); the goals 2 and 3 don't use it then.
	const bool no_limit = index > 0x12 || kGoalLimit[index] == 0;
	const uint32_t limit = no_limit ? 0 : kGoalLimit[index];
	const uint32_t start = g.u32(kStartTick);
	// The end: the ticker line «The predetermined playing time has elapsed.»
	// to the local player (kind 0xC, before each of the 9 ends: 0x4E4C53...)
	// and [0x6DFC04] = 0 (the end window, the interface's).
	const auto game_over = [&]() {
		send_ticker(save, g.u32(0x6DFC14), 0xC, 0);
		g.u32(kGameOver) = 0;
		return 0u;
	};
	switch (uint32_t(e.u16(8)) - 1) {
	case 0: {
		// To be of rank 4 in one's home town. Checked with the oracle: scenario game_end_branches
		// (saves 1, 3, 5, 7).
		if (e.u32(0) >= start + limit) {
			return game_over();
		}
		uint32_t i = 0x14;
		for (; i < merchant_count(save); ++i) {
			const MerchantMemory& m = save.merchants[i].memory;
			if (plays_goal(save, m) && m.u8(0x39C + m.u8(0x19)) == 4) {
				return game_over();
			}
		}
		// El juego suma el tick del evento de índice i (el contador del bucle).
		const uint32_t other = i < save.events.size() ? save.events[i].u32(0) : 0;
		e.u32(0) = (uint32_t(kMonthDays[g.u8(kMonth) % 12]) << 8) + other;
		if (e.u32(0) > start + limit) {
			e.u32(0) = start + limit;
		}
		return 1;
	}
	case 1: {
		// To be a town's mayor. Checked with the oracle: scenario game_end_branches (saves 1, 3, 5,
		// 7).
		if (!(e.u32(0) < start + limit || no_limit)) {
			return game_over();
		}
		bool keep = true;
		if (merchant_count(save) > 0x14) {
			for (uint32_t i = 0x14; i < merchant_count(save); ++i) {
				const MerchantMemory& m = save.merchants[i].memory;
				if (!plays_goal(save, m)) {
					continue;
				}
				uint32_t k = 0;
				const uint32_t n = town_count(save);
				while (k < n && save.towns[k].memory.u8(0x6F1) != i) {
					++k;
				}
				if (k != n) {
					send_ticker(save, g.u32(0x6DFC14), 0xC, 0);
					g.u32(kGameOver) = 0;
					keep = false;
					break;
				}
			}
			if (!keep) {
				return 0;
			}
		}
		// Un día después del evento de tipo 2 (en el orden de la lista).
		for (uint32_t i = g.u16(kEventHead); i < g.u16(kEventCount); i = save.events[i].u16(4)) {
			if (save.events[i].u16(6) == 2) {
				e.u32(0) = save.events[i].u32(0) + 1;
				break;
			}
		}
		if (!no_limit && e.u32(0) > start + limit) {
			e.u32(0) = start + limit;
		}
		return 1;
	}
	case 2: {
		// To be the Hanse's head ([0x6DE52D]). Checked with the oracle: scenario game_end_branches
		// (saves 1, 3, 5, 7).
		if (!(e.u32(0) < start + limit || no_limit)) {
			return game_over();
		}
		for (uint32_t i = 0x14; i < merchant_count(save); ++i) {
			if (plays_goal(save, save.merchants[i].memory) && g.u8(0x6DE52D) == i) {
				return game_over();
			}
		}
		// A tick after the first event 0x20 of the array. Unreachable without
		// one (the game would read the address 0): the Gobernador's election
		// (events_mayor.cpp) is never taken out of the queue.
		uint32_t found = 0;
		for (uint32_t i = 0; i < g.u16(kEventCount); ++i) {
			if (save.events[i].u16(6) == 0x20) {
				found = save.events[i].u32(0);
				break;
			}
		}
		e.u32(0) = found + 1;
		if (!no_limit && e.u32(0) > start + limit) {
			e.u32(0) = start + limit;
		}
		return 1;
	}
	case 3:
		// Llegar al plazo.
		if (e.u32(0) < start + limit) {
			e.u32(0) = start + limit;
			return 1;
		}
		return game_over();  // Checked with the oracle: scenario game_end_branches (saves 1, 3, 5, 7).
	case 4:
		// At the limit, the one with the most [+0x46C] wins (the end window's). Checked with the
		// oracle: scenario game_end_branches (saves 1, 3, 5, 7).
		if (e.u32(0) < start + limit) {
			e.u32(0) = start + limit;
			return 1;
		}
		return game_over();
	case 5: {
		// The towns reaching so many people. Checked with the oracle: scenario game_end_branches
		// (saves 1, 3, 5, 7).
		uint32_t people = 0;
		for (uint32_t i = 0; i < town_count(save); ++i) {
			people += save.towns[i].memory.u32(0x2D4);
		}
		if (people < limit) {
			e.u32(0) = (g.u32(kTick) + 0x100) & ~0xFFu;
			return 1;
		}
		return game_over();
	}
	default:
		return 0;  // Checked with the oracle: scenario game_end_branches (saves 1, 3, 5, 7).
	}
}

// --- Evento 0x1C: la peste ---

// 0x4E9094 (evento 0x1C de la cabeza, sobre la cola): la peste en la ciudad
// [+0x16]. El evento vuelve a salir tras [+0x8 + (tick & 3)] + 1 ticks; si
// así pasa a otro día, [+0x10] (muertos) vuelve a 0 y [+0x12] (los que
// morirán) a habitantes / 200. Cada vez mueren hasta ([+0x12] / 4 + 4) & ~3
// (sin pasar de [+0x12] − [+0x10]): primero trabajadores de las empresas de
// las oficinas y de la ciudad (4 pobres por cada uno), después mendigos.
// Hasta el tick [+0xC] pone el bit 0x8 de la situación (y avisa a los
// jugadores, interfaz) y devuelve 1; después quita el bit y devuelve 0.
uint32_t fn_4e9094(SaveGame& save) {
	GlobalMemory& g = save.globals;
	Event& e = head_event(save);
	const uint32_t phase = g.u32(kTick) & 3;
	const uint32_t town = e.u8(0x16);
	if (town >= town_count(save)) {
		return 0;  // Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7).
	}
	TownMemory& t = save.towns[town].memory;
	const uint32_t old = e.u32(0) & 3;
	e.u32(0) = e.u32(0) + uint32_t(e.u8(8 + old)) + 1;
	if ((e.u32(0) & 0xFF) < e.u8(8 + old)) {
		e.u16(0x10) = 0;
		e.u16(0x12) = uint16_t(t.s32(0x2D4) / 200);
	}
	int32_t left = int32_t(((uint32_t(e.u16(0x12)) >> 2) + 4) & 0xFFFC);
	if (int32_t(e.u16(0x10)) + left >= int32_t(e.u16(0x12))) {
		left = int32_t(e.u16(0x12)) - int32_t(e.u16(0x10));
	}
	if (t.s32(0x2E4) < 4) {
		t.u32(0x2E4) += 4;
		left += 4;
		if (e.u16(0x10) < 4) {
			e.u16(0x12) = uint16_t(e.u16(0x12) + 4);
		} else {
			e.u16(0x10) = uint16_t(e.u16(0x10) - 4);
		}
	}
	// Las empresas de las oficinas de la ciudad (registros del mundo de la
	// lista [+0x2CC] de cada oficina, encadenados por su [+0x8]).
	uint32_t office = t.u16(0x784);
	if (int32_t(office) < int32_t(office_count(save))) {
		while (left > 3) {
			OfficeMemory& o = save.offices[office].memory;
			const uint32_t next = o.u16(0x2CA);
			uint32_t r = o.u16(0x2CC);
			if (int32_t(r) < int32_t(g.u16(kWorldRecordCount))) {
				while (left > 3) {
					GameMemory<0x14>& rec = save.world_records[r];
					r = rec.u16(8);
					// El juego mira el índice del siguiente (& 3), no el del registro.
					if ((r & 3) == phase && rec.u16(4) > 1 && t.s32(0x2E0) > 4) {
						rec.u16(4) = uint16_t(rec.u16(4) - 1);
						left -= 4;
						e.u16(0x10) = uint16_t(e.u16(0x10) + 4);
						t.u32(0x2E0) -= 4;
					}
					if (!(int32_t(r) < int32_t(g.u16(kWorldRecordCount)))) {
						break;
					}
				}
			}
			// Cada 128 ticks (según el índice de la oficina siguiente) se va el
			// administrador de la oficina.
			if (((g.u32(kTick) ^ next) & 0x7F) == 0 && o.u16(0x2F2) < g.u16(kCaptainCount)) {
				// Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7).
				fn_5098b0(save, o.u16(0x2F2));
				o.u8(0x2D6) &= 0xFC;
			}
			office = next;
			if (!(int32_t(office) < int32_t(office_count(save)))) {
				break;
			}
		}
	}
	// Las instalaciones de la ciudad, desde la (tick & 3) + 1, de una en una o
	// de dos en dos según el bit 0 de [+0x8 + (tick & 3)].
	for (uint32_t k = phase + 1; k < 0x15 && left > 3;) {
		uint16_t& workers = t.u16(0x844 + 16 * k);
		if (workers > 1) {
			if (t.s32(0x2E0) <= 4) {
				break;
			}
			--workers;
			left -= 4;
			e.u16(0x10) = uint16_t(e.u16(0x10) + 4);
			t.u32(0x2E0) -= 4;
		}
		k = k + (e.u8(8 + phase) & 1) + 1;
	}
	if (left > 0) {
		if (left < t.s32(0x2E4)) {
			t.u32(0x2E4) -= uint32_t(left);
			e.u16(0x10) = uint16_t(e.u16(0x10) + uint16_t(left));
		} else {
			e.u16(0x10) = uint16_t(e.u16(0x10) + uint16_t(t.u16(0x2E4) - 1));
			t.u32(0x2E4) = 1;
		}
	}
	t.u32(0x2D4) = 0;
	for (uint32_t k = 0; k < 4; ++k) {
		t.u32(0x2D4) += t.u32(0x2D8 + 4 * k);
	}
	if (e.u32(0) > e.u32(0xC)) {
		t.u32(0x2C8) &= ~8u;
		return 0;
	}
	if ((t.u32(0x2C8) & 8) == 0) {
		// The news 0x58 to the chronicle (0x4E9355..0x4E9389): [+0x5] and
		// u16 [+0x8] the town ([+0x2C0]). Then, if it is the local player's
		// home town, the notices to each human merchant (0x548CA0 with kind
		// 0x13, and the window 0x469380/0x469AF0 to the local player): the
		// interface's.
		Letter letter(0x58);
		letter.entry.u8(5) = t.u8(0x2C0);
		letter.entry.u16(8) = t.u8(0x2C0);
		send_letter(save, kLetterToChronicle, letter);
		t.u32(0x2C8) |= 8;
	}
	return 1;
}

// --- Evento 0x31 ---

// 0x4EAB94 (evento 0x31, lo crea el 0x2E un día después de aprobar la
// propuesta 0): el impuesto extraordinario de la ciudad [+0xC], de [+0x8].
// Sin oficinas en ella, nada. La ciudad lo cobra (0x529F90, ingreso 2) y lo
// pagan los comerciantes con oficina en ella: la mitad a partes iguales y la
// otra mitad según la oficina ([+0x2D2] + [+0x2D4] + 1); cada parte suma a su
// [+0x4BC] y sale de su dinero (si lo tiene, positivo); lo que no puede
// pagar queda como deudas con la ciudad (eventos 0x01 a 7 días, de 250000
// como mucho). Si alguno de los que pagan no es de la ciudad ni su alcalde y
// la ciudad tiene el bit 0x100 de [0x76C] (y no el 0x40 de [0x2C8]), una
// queja a la Hansa: un evento 0x30 al día siguiente, de gravedad 5 −
// impuesto / 50000 (al menos 1), con el que preside la primera ciudad con el
// bit 0x40 (su alcalde de la IA o, sin alcalde comerciante, un concejal).
// Each payer gets the letter 0x6C. Se quita siempre.
void fn_4eab94(SaveGame& save) {
	GlobalMemory& g = save.globals;
	const uint32_t amount = head_event(save).u32(0x8);
	const uint8_t town = head_event(save).u8(0xC);
	if (town >= town_count(save)) {
		return;  // Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7).
	}
	TownMemory& t = save.towns[town].memory;
	uint32_t offices = 0;
	uint32_t size = 0;
	for (uint32_t o = t.u16(0x784); o < office_count(save); o = save.offices[o].memory.u16(0x2CA)) {
		const OfficeMemory& office = save.offices[o].memory;
		size += uint32_t(office.u16(0x2D2)) + office.u16(0x2D4) + 1;
		++offices;
	}
	if (offices == 0) {
		return;
	}
	fn_529f90(save, town, int32_t(amount), 2);
	const uint32_t half = amount >> 1;
	const uint32_t even = half / offices;
	bool complaint = false;
	for (uint32_t o = t.u16(0x784); o < office_count(save);) {
		const OfficeMemory& office = save.offices[o].memory;
		const uint32_t next = office.u16(0x2CA);
		uint32_t share = (uint32_t(office.u16(0x2D4)) + office.u16(0x2D2) + 1) * half / size + even;
		const uint16_t owner = office.u16(0x2C4);
		if (owner >= merchant_count(save)) {
			o = next;
			// Unreachable: a town's list only holds offices with a merchant (a freed office leaves
			// it). The game doesn't check (it would write past the merchants).
			continue;
		}
		MerchantMemory& m = save.merchants[owner].memory;
		m.u32(0x4BC) += share;
		// The letter 0x6C to the owner (0x4EAC80..0x4EAE01): [+0x5] the town,
		// u32 [+0xC] the tax, u32 [+0x8] his share, with the bit 0x80000000
		// when he couldn't pay it all (debts).
		Letter letter(0x6C);
		letter.entry.u8(5) = town;
		letter.entry.u32(0xC) = amount;
		letter.entry.u32(8) = share;
		const int32_t money = m.s32(0);
		bool paid = false;
		if (money > 0) {
			if (uint32_t(money) > share) {
				m.s32(0) = int32_t(uint32_t(money) - share);
				paid = true;
			} else {
				share -= uint32_t(money);
				m.s32(0) = 0;
			}
		}
		while (!paid && share > 0) {
			g.u32(kEventBusy) = 0;
			const uint16_t n = fn_4d8cf0(save, g.u32(kTick) + 0x700);
			g.u32(kEventBusy) = 1;
			Event& d = save.events[n];
			d.u16(6) = 1;
			d.u8(0x12) = 0;
			d.u8(0x15) = 0;
			d.u8(0x16) = 0;
			d.u8(0x17) = 0;
			const uint32_t part = share > 250000 ? 250000 : share;
			d.u32(0x8) = part;
			share -= part;
			d.u8(0x13) = 0;
			d.u8(0x10) = 0xFF;
			d.u8(0x11) = office.u8(0x2C4);
			d.u32(0xC) = g.u32(kTick);
			d.u8(0xC) = 0;
			d.u8(0x14) = t.u8(0x2C0);
			letter.entry.u32(8) |= 0x80000000;
		}
		send_letter(save, int32_t(owner), letter);
		complaint |= m.u8(0x19) != town && owner != t.u8(0x6F1) && (t.u32(0x76C) & 0x100);
		o = next;
	}
	if (!complaint || (t.u32(0x2C8) & 0x40)) {
		return;
	}
	g.u32(kEventBusy) = 0;
	const uint16_t n = fn_4d8cf0(save, g.u32(kTick) + 0x100);
	g.u32(kEventBusy) = 1;
	Event& c = save.events[n];
	c.u16(6) = 0x30;
	c.u32(0x8) = g.u32(kTick);
	c.u8(0x12) = t.u8(0x2C0);
	c.u8(0x13) = 1;
	c.u8(0x14) = 0xFF;
	c.u8(0x15) = 0;
	const uint32_t steps = amount / 50000;
	c.u8(0xC) = steps >= 5 ? 1 : uint8_t(5 - steps);
	for (uint32_t i = 0; i < town_count(save); ++i) {
		const TownMemory& h = save.towns[i].memory;
		if (!(h.u32(0x2C8) & 0x40)) {
			continue;
		}
		const uint8_t mayor = h.u8(0x6F1);
		if (mayor >= merchant_count(save)) {
			const uint32_t k = uint8_t(~mayor) & 3;
			c.u8(0xD) = h.u8(0x6D4 + k);
			c.u8(0xE) = h.u8(0x6D8 + k);
			c.u8(0xF) = uint8_t(i);
			c.u8(0x10) = h.u8(0x6E0 + k);
			c.u8(0x11) = h.u8(0x6E4 + k);
			return;
		}
		const MerchantMemory& m = save.merchants[mayor].memory;
		if (m.u16(8) != 0) {
			c.u8(0xD) = m.u8(0x1C);
			c.u8(0xE) = m.u8(0x1D);
			c.u8(0xF) = uint8_t(i);
			c.u8(0x10) = merchants_detail::fn_4f9430(save, mayor);
			c.u8(0x11) = m.u8(0x39C + m.u8(0x19));
			return;
		}
	}
}

// --- Eventos 0x25 y 0x26: los incendios del asedio ---

// 0x4DB100 (sobre la cola): quita el edificio `record` de la primera subasta
// de edificios (evento 0x36) de la ciudad que lo tenga entre sus tres lotes
// ([+0x8], [+0xA], [+0xC], con el bit 0x8000): los de detrás se corren y el
// último queda en 0xFFFF; si la subasta se queda sin lotes, se quita el
// evento (vuelve a la lista de libres).
void fn_4db100(SaveGame& save, uint32_t town, uint16_t record) {
	GlobalMemory& g = save.globals;
	if (town >= town_count(save) || record >= save.towns[town].memory.u16(0x776)) {
		// Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7), called
		// directly: its callers, the fires 0x26 and 0x1D, give a town and one of its records.
		return;
	}
	uint16_t* link = &g.u16(kEventHead);
	while (*link < g.u16(kEventCount)) {
		Event& e = save.events[*link];
		if (e.u16(6) == 0x36 && e.u8(0x15) == town) {
			bool found = false;
			for (uint32_t k = 0; k < 3; ++k) {
				const uint16_t lot = e.u16(0x8 + 2 * k);
				if (found) {
					e.u16(0x6 + 2 * k) = lot;
				} else if ((lot & 0x7FFF) == record) {
					found = true;
				}
			}
			if (found) {
				// Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7): an
				// auction kept and one removed.
				e.u16(0xC) = 0xFFFF;
				if ((e.u16(0x8) & 0x7FFF) >= save.towns[town].memory.u16(0x776)) {
					const uint16_t index = *link;
					*link = e.u16(0x4);
					e.u16(0x6) = 0xFFFF;
					e.u16(0x4) = g.u16(kEvents + 0xA);
					g.u16(kEvents + 0xA) = index;
				}
				return;
			}
		}
		link = &e.u16(0x4);
	}
}

namespace {

// Lo que resta al fuego un edificio en obras: −50 · obras / sus días de obra.
int16_t unfinished(const BuildingRecord& b) {
	const uint8_t days = construction_detail::build_days(b.type);
	return days ? int16_t(-50 * int32_t(b.pending) / int32_t(days)) : 0;  // El juego divide por 0 sin días.
}

// El fuego avanza: la vida [+0xA] baja 51 (1 desde 50) y se ve en las obras
// del edificio; dentro de 5 ticks, otra vez. Devuelve false si ya no le queda.
bool burn(SavedTown& st, Event& e) {
	int16_t life = int16_t(e.u16(0xA));
	if (life <= 0) {
		return false;
	}
	life = life > 50 ? int16_t(life - 51) : int16_t(life - 1);
	e.u16(0xA) = uint16_t(life);
	if (e.u16(0x8) < st.records.size()) {
		st.records[e.u16(0x8)].pending = uint8_t(life);
	}
	e.u32(0) += 5;
	return true;
}

// El enlace de la lista que empieza en `head` que apunta a `record`, o
// nullptr si no está.
uint16_t* find_link(SavedTown& st, uint16_t& head, uint16_t record) {
	const uint32_t count = st.memory.u16(0x776);
	for (uint16_t* link = &head; *link < count && *link < st.records.size(); link = &st.records[*link].next) {
		if (*link == record) {
			return link;
		}
	}
	return nullptr;
}

}  // namespace

// 0x4E1B84 (evento 0x25, lo crea el tick del asedio 0x6281F0 al caer un
// objetivo): el fuego en el edificio de la celda ([+0xC], [+0xE]) de la
// ciudad [+0x10]. La primera vez ([+0x11]), lo saca de la lista de murallas
// [0x768] (si no está, se quita el evento) y lo pasa a la de lo que arde
// [0x76A], con 100 de vida (menos si estaba en obras). Mientras le quede,
// arde (ver burn). Al acabar: una torre (0x32, 0x33) queda en ruinas (0x36,
// sin obras, otra vez en [0x768]); lo demás se borra (0x521240). Con una
// ciudad que no existe (0xFE − k) es la guarida pirata k (0x515C00), de las
// batallas: por reescribir con ellas. Los gráficos (0x5891C0, 0x599040 y
// 0x590680 si es la ciudad que se ve) no se reproducen. Devuelve si sigue.
// Comprobado con estados retocados (`retouch.py incendio`, también el 0x26).
bool fn_4e1b84(SaveGame& save, uint16_t event) {
	Event& e = save.events[event];
	const uint32_t town = e.u8(0x10);
	if (town >= town_count(save)) {
		return false;  // Por reescribir con las batallas (0x515C00).
	}
	SavedTown& st = save.towns[town];
	TownMemory& t = st.memory;
	if (e.u8(0x11)) {
		e.u8(0x11) = 0;
		e.u16(0xA) = 100;
		e.u16(0x8) = 0xFFFF;
		const uint32_t count = t.u16(0x776);
		for (uint16_t* link = &t.u16(0x768); *link < count && *link < st.records.size();) {
			BuildingRecord& b = st.records[*link];
			if (b.x == e.u16(0xC) && b.y == e.u16(0xE)) {
				e.u16(0x8) = *link;
				*link = b.next;
				b.next = t.u16(0x76A);
				t.u16(0x76A) = e.u16(0x8);
				if (b.pending > 0) {
					// Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7).
					e.u16(0xA) = uint16_t(e.u16(0xA) + unfinished(b));
				}
				break;
			}
			link = &b.next;
		}
		if (e.u16(0x8) >= count) {
			return false;  // Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7).
		}
	}
	if (burn(st, e)) {
		return true;
	}
	uint16_t* link = find_link(st, t.u16(0x76A), e.u16(0x8));
	if (!link) {
		// Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7): an empty
		// burning list and one without it.
		return false;
	}
	BuildingRecord& b = st.records[e.u16(0x8)];
	*link = b.next;
	if (b.type == 0x32 || b.type == 0x33) {
		b.type = 0x36;
		b.pending = 0;
		b.next = t.u16(0x768);
		t.u16(0x768) = e.u16(0x8);
		return false;
	}
	b.next = 0xFFFF;
	construction_detail::fn_521240(save, int(town), e.u16(0x8));
	return false;
}

// 0x4E1E54 (evento 0x26, lo crea el tick del asedio al llegar un grupo): el
// fuego en el edificio [+0x8] de la ciudad [+0x10], de fuera de la muralla.
// La primera vez ([+0x11]): nada con los tipos 0x28, 0x32 y 0x33; apunta su
// celda en [+0xC], [+0xE], lo quita de las subastas (0x4DB100) y de su lista
// (las de la oficina de su dueño, [+0x2CE] y [+0x2D0]; las de la ciudad, de
// la ciudad o del tipo 0x29, [0x764]) y lo pasa a la de lo que arde [0x76A],
// con 100 de vida (menos si estaba en obras). Con el bit 0x8000 de [0x2C8],
// se acaba ahí. Mientras le quede, arde (ver burn); al acabar, se borra
// (0x521240). Los gráficos no se reproducen. Devuelve si sigue.
bool fn_4e1e54(SaveGame& save, uint16_t event) {
	Event& e = save.events[event];
	const uint32_t town = e.u8(0x10);
	if (town >= town_count(save)) {
		return false;  // Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7).
	}
	SavedTown& st = save.towns[town];
	TownMemory& t = st.memory;
	const uint32_t count = t.u16(0x776);
	const uint16_t index = e.u16(0x8);
	if (e.u8(0x11)) {
		e.u8(0x11) = 0;
		e.u16(0xA) = 100;
		if (index >= count || index >= st.records.size()) {
			// Unreachable: the siege's groups (0x6281F0) are buildings of the town, records under
			// the count, which never goes down. The game would read the address 0 (0x51F1F0 gives 0
			// past the count).
			return false;
		}
		BuildingRecord& b = st.records[index];
		if (b.type == 0x28 || b.type == 0x32 || b.type == 0x33) {
			return false;  // Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7): the three types.
		}
		e.u16(0xC) = b.x;
		e.u16(0xE) = b.y;
		fn_4db100(save, town, index);
		uint16_t* link = nullptr;
		if (b.owner < merchant_count(save) && b.type != 0x29) {
			const int office = facilities_detail::fn_5308a0(save, b.owner, town);
			if (office < 0) {
				return false;  // Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7).
			}
			OfficeMemory& o = save.offices[size_t(office)].memory;
			link = find_link(st, o.u16(0x2CE), index);
			if (!link) {
				link = find_link(st, o.u16(0x2D0), index);
			}
		} else {
			// If the game doesn't find it, it looks again in the same list. Checked with the
			// oracle: scenario siege_fire_branches (saves 1, 3, 5, 7): a building of type 0x29 with
			// a merchant, and one of no merchant out of the list.
			link = find_link(st, t.u16(0x764), index);
		}
		if (!link) {
			// Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7): not in its
			// office's lists, and not in [0x764].
			return false;
		}
		*link = b.next;
		b.next = t.u16(0x76A);
		t.u16(0x76A) = index;
		if (b.pending > 0) {
			// Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7).
			e.u16(0xA) = uint16_t(e.u16(0xA) + unfinished(b));
		}
	}
	if (t.u32(0x2C8) & 0x8000) {
		return false;  // Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7).
	}
	if (burn(st, e)) {
		return true;
	}
	uint16_t* link = find_link(st, t.u16(0x76A), index);
	if (!link) {
		return false;  // Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7).
	}
	*link = st.records[index].next;
	st.records[index].next = 0xFFFF;
	construction_detail::fn_521240(save, int(town), index);
	return false;
}

// --- Evento 0x38 ---

// 0x4ECB14 (evento 0x38, lo crea la elección de alcalde 0x528E60 a la
// primera reelección de un comerciante): el monumento al alcalde [+0x9] de
// la ciudad [+0x8]. Nada si ya no es su alcalde, si hay otro 0x38 suyo en la
// cola o si la ciudad no tiene un edificio de tipo 0x2D de la ciudad (el
// primero de ese tipo en la lista [0x764]). Si no, se aplaza 0x3E00 ticks más
// 31 · ([+0x1C] & 0x3FF) / 2 del barco [0x6DD88A] y, mientras la muralla no
// esté (0x4FCFE0 del punto 1, o del 2 con piezas pendientes en [0x766]),
// espera; después el monumento pasa a ser del alcalde ([+0x2]), con la
// variante [+0x7] (tick + ciudad + comerciante) % 3, 3 más con rango > 7
// (0x4F9430), and the news 0x7B. Los gráficos (0x590680 si es la ciudad que
// se ve) no se reproducen. Devuelve si sigue (con el tick ya cambiado). Comprobado
// con estados retocados (`retouch.py monumento`: esperar a la muralla, el
// monumento y otro comerciante que ya no es alcalde).
bool fn_4ecb14(SaveGame& save, uint16_t event) {
	const GlobalMemory& g = save.globals;
	Event& e = save.events[event];
	const uint32_t town = e.u8(0x8);
	const uint8_t merchant = e.u8(0x9);
	if (town >= town_count(save) || merchant >= merchant_count(save)) {
		return false;  // Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7).
	}
	SavedTown& st = save.towns[town];
	const TownMemory& t = st.memory;
	if (merchant != t.u8(0x6F1)) {
		return false;
	}
	for (uint32_t i = e.u16(0x4); i < g.u16(kEventCount); i = save.events[i].u16(0x4)) {
		if (save.events[i].u16(6) == 0x38 && save.events[i].u8(0x9) == merchant) {
			return false;  // Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7).
		}
	}
	const uint32_t count = t.u16(0x776);
	uint32_t monument = 0xFFFF;
	for (uint32_t i = t.u16(0x764); i < count; i = st.records[i].next) {
		if (st.records[i].type == 0x2D) {
			if (st.records[i].owner >= merchant_count(save)) {
				monument = i;
			}
			break;
		}
	}
	if (monument >= count) {
		return false;  // Checked with the oracle: scenario siege_fire_branches (saves 1, 3, 5, 7).
	}
	e.u32(0) += 0x3E00;
	const uint16_t ship = g.u16(global::kShips + 0xEA);  // 0x6DD88A
	if (ship < ship_count(save)) {
		const uint32_t x = save.ships[ship].memory.u32(0x1C) & 0x3FF;
		e.u32(0) += uint32_t(int32_t((x * 31) << 8) >> 9);
	}
	if (fn_4fcfe0(save, int(town), 1) || (fn_4fcfe0(save, int(town), 2) && t.u16(0x766) < count)) {
		return true;
	}
	BuildingRecord& r = st.records[monument];
	r.owner = merchant;
	r.param = uint8_t((g.u32(kTick) + town + merchant) % 3);
	if (merchants_detail::fn_4f9430(save, merchant) > 7) {
		r.param = uint8_t(r.param + 3);
	}
	// The news 0x7B (0x4ECD71..0x4ECDBF), to the chronicle and to each
	// merchant: [+0x5] the town; u16 [+0x8] the mayor's title (0x4F9430),
	// [+0xA] his [+0x1C], [+0xC] his [+0x1D].
	const MerchantMemory& m = save.merchants[merchant].memory;
	Letter letter(0x7B);
	letter.entry.u8(5) = uint8_t(town);
	letter.entry.u16(8) = merchants_detail::fn_4f9430(save, merchant);
	letter.entry.u16(0xA) = m.u8(0x1C);
	letter.entry.u16(0xC) = m.u8(0x1D);
	send_letter(save, kLetterToChronicle, letter);
	send_letter(save, kLetterToEach, letter);
	return false;
}

// --- Evento 0x35 ---

// 0x4E94A4 (evento 0x35 de la cabeza): quita el bit 0x4000000 de la
// situación de la ciudad [+0x8] (u32). El aviso de la interfaz (0x64DF80,
// 0x42B4C0 sobre *0x6CBB40) no se traduce.
void fn_4e94a4(SaveGame& save) {
	const uint32_t town = head_event(save).u32(8);
	if (town < town_count(save)) {
		save.towns[town].memory.u32(0x2C8) &= 0xFBFFFFFF;
	}
}

// --- Eventos 0x1F y 0x1E: los piratas ---

// 0x4DFD44 (evento 0x1E): piratas ante la ciudad [+0x11]. [+0x8] y [+0xC]
// ticks límite, [+0x10] estado, [+0x12] u16, [+0x14] s8 (baja 1 cada vez
// que la ciudad tiene la situación & 0x600), [+0x15], [+0x16], [+0x17].
// Sigue (`keep`) mientras no pase [+0xC], o hasta [+0x8] si la satisfacción
// de las tres primeras clases ([+0x300..+0x304]) suma más de −30 y [+0x14] > 0.
//   0, 1  esperan (cada 3 días); al acabar, el 1 devuelve [+0x2C6] y quita
//         el bit 0x2000000 de la ciudad; los dos, con la situación & 0x600
//         y la satisfacción [+0x308..+0x30C] de las otras clases > −10,
//         pasan al estado 4 en 5 ticks.
//   2     cada 0x80 ticks busca un barco en el mar de un jugador humano cerca
//         de la ciudad (distancia² < [0x6DD89C] · 64): lo ataca (0x5063C0) y
//         pasa al estado 3 ([+0x16] el barco, [+0x12] su [+0x15E]); sigue
//         mientras [+0x15] no llegue a 1. El ataque no está traducido.
//   3     cada 0x129 ticks, con [+0x16] como comerciante: si su [+0x1B] ha
//         subido 4 desde [+0x17], le da 4000 de dinero y se quita; si no,
//         vuelve a atacar a uno de sus barcos cerca (0x5063C0, sin traducir).
//   4     las oficinas de los jugadores humanos en la ciudad le dan sus
//         mercancías y armas; [+0x15] baja cuando la última mercancía era
//         bastante (el juego solo mira la última) y [+0x8] se adelanta.
uint32_t fn_4dfd44(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	Event& e = save.events[event];
	const uint32_t town = e.u8(0x11);
	TownMemory& t = save.towns[town].memory;
	int32_t mood = 0;
	for (uint32_t k = 0; k < 3; ++k) {
		mood += t.s16(0x300 + 2 * k);
	}
	if (t.u32(0x2C8) & 0x600) {
		e.s8(0x14) = int8_t(e.s8(0x14) - 1);
	}
	const uint32_t now = g.u32(kTick);
	bool keep = e.u32(0xC) > now || (e.u32(8) > now && mood > -0x1E && e.s8(0x14) > 0);
	const auto pirates_win = [&]() -> uint32_t {
		if ((t.u32(0x2C8) & 0x600) == 0) {
			return 0;
		}
		if (t.s16(0x308) > -10 && t.s16(0x30A) > -10 && t.s16(0x30C) > -10) {
			e.u8(0x10) = 4;
			e.u32(0) += 5;
			return 1;
		}
		return 0;
	};
	switch (e.u8(0x10)) {
	case 0:
		if (keep) {
			e.u32(0) += 0x300;
			return 1;
		}
		t.u8(0x6F0) = 0xFF;  // Checked with the oracle: scenario pirate_event_branches (saves 3, 5, 7).
		return pirates_win();
	case 1:
		// Checked with the oracle: scenario pirate_event_branches (saves 3, 5, 7).
		if (keep) {
			e.u32(0) += 0x300;
			return 1;
		}
		t.u16(0x2C6) = e.u16(0x12);
		t.u32(0x2C8) &= 0xFDFFFFFF;
		return pirates_win();
	case 2: {
		if (!keep) {
			return 0;  // Checked with the oracle: scenario pirate_event_branches (saves 3, 5, 7).
		}
		const int32_t x = town_x(save, town);
		const int32_t y = town_y(save, town);
		e.u32(0) += 0x80;
		for (uint32_t m = merchant_count(save); int32_t(m) > 0;) {
			--m;
			const MerchantMemory& mm = save.merchants[m].memory;
			if (mm.u16(8) != 0) {
				continue;
			}
			for (uint32_t s = mm.u16(0xE); s < ship_count(save); s = save.ships[s].memory.u16(4)) {
				const ShipMemory& sm = save.ships[s].memory;
				if (sm.u16(0x134) != 0xF) {
					continue;
				}
				const int32_t dx = x - sm.s16(0x1E);
				const int32_t dy = y - sm.s16(0x22);
				const int32_t d2 = int32_t(uint32_t(dy) * uint32_t(dy) + uint32_t(dx) * uint32_t(dx));
				if (d2 < int32_t(uint32_t(g.u16(kPirateRange)) << 6)) {
					e.u8(0x10) = 3;
					e.u8(0x16) = uint8_t(s);
					e.u16(0x12) = sm.u16(0x15E);
					// The attack on the ship (0x5063C0 with 1, on the ships
					// object: battle/pirate_objects.cpp), with the pirates on
					// (docs/cloud/battles-2.md §2.4). It may add a ship: `sm`
					// isn't used after it. Checked with retouch_pirates.py
					// event_attack; the 12 saves never get here.
					if (Sailing* sailing = event_sailing(); sailing && sailing->pirates()) {
						sailing->fn_5063c0(uint16_t(s), 1);
					}
					const uint8_t left = e.u8(0x15);
					e.u32(0) += 0x200;
					e.u8(0x15) = uint8_t(left - 1);
					return left > 1 ? 1 : 0;
				}
			}
		}
		return 1;
	}
	case 3: {
		// Checked with retouch_mail.py pirate_hunt (0x41, 0x42). [+0x16] es aquí un comerciante (el
		// estado 2 deja en él el índice del barco).
		e.u32(0) += 0x129;
		const uint32_t m = e.u8(0x16);
		if (m >= merchant_count(save)) {
			return 0;
		}
		MerchantMemory& mm = save.merchants[m].memory;
		if (e.u16(0x12) < 0xFFFF) {
			// The letter 0x41 to the merchant (0x4E0026..0x4E0065): [+0x5]
			// his home town [+0x19]; u16 [+0x8] the town [+0x11], [+0xA]
			// [+0x12] (the ship's [+0x15E]), [+0xC] his [+0x1C], [+0xE] his
			// [+0x1D].
			Letter letter(0x41);
			letter.entry.u8(5) = mm.u8(0x19);
			letter.entry.u16(8) = e.u8(0x11);
			letter.entry.u16(0xA) = e.u16(0x12);
			letter.entry.u16(0xC) = mm.u8(0x1C);
			letter.entry.u16(0xE) = mm.u8(0x1D);
			send_letter(save, int32_t(m), letter);
			e.u16(0x12) = 0xFFFF;
			e.u8(0x17) = mm.u8(0x1B);
		}
		if (int32_t(e.u8(0x17)) + 4 <= int32_t(mm.u8(0x1B))) {
			// The letter 0x42 (0x4E008B..0x4E00CF: as 0x41 with u16 [+0xA]
			// the reward, 4000) and 4000 of money.
			Letter letter(0x42);
			letter.entry.u8(5) = mm.u8(0x19);
			letter.entry.u16(8) = e.u8(0x11);
			letter.entry.u16(0xA) = 0xFA0;
			letter.entry.u16(0xC) = mm.u8(0x1C);
			letter.entry.u16(0xE) = mm.u8(0x1D);
			send_letter(save, int32_t(m), letter);
			mm.u32(0) += 0xFA0;
			mm.u32(0x4C8) += 0xFA0;
			return 0;
		}
		const int32_t x = town_x(save, town);
		const int32_t y = town_y(save, town);
		for (uint32_t s = mm.u16(0xE); s < ship_count(save); s = save.ships[s].memory.u16(4)) {
			const ShipMemory& sm = save.ships[s].memory;
			if (sm.u16(0x134) != 0xF) {
				continue;
			}
			const int32_t dx = x - sm.s16(0x1E);
			const int32_t dy = y - sm.s16(0x22);
			const int32_t d2 = int32_t(uint32_t(dy) * uint32_t(dy) + uint32_t(dx) * uint32_t(dx));
			if (d2 < int32_t(uint32_t(g.u16(kPirateRange)) << 6)) {
				// Another attack, 0x5063C0 with 0 on the ship `s`, with the
				// pirates on (as in state 2).
				if (Sailing* sailing = event_sailing(); sailing && sailing->pirates()) {
					sailing->fn_5063c0(uint16_t(s), 0);
				}
				break;
			}
		}
		return keep ? 1 : 0;
	}
	case 4: {
		// Checked with retouch_mail.py pirate_hunt (0x43). Las oficinas de los jugadores humanos
		// en la ciudad le dan sus mercancías y armas.
		e.u32(0xC) = 0;
		static constexpr uint8_t kSmall[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};  // 0x672C14
		for (uint32_t o = t.u16(0x784); int32_t(o) < int32_t(office_count(save));) {
			OfficeMemory& of = save.offices[o].memory;
			const uint32_t next = of.u16(0x2CA);
			const uint32_t owner = of.u16(0x2C4);
			if (save.merchants[owner].memory.u16(8) == 0) {
				bool last = false;  // El juego solo se queda con la última mercancía.
				for (uint32_t k = 0; k < 0x18; ++k) {
					const int32_t amount = of.s32(4 + 4 * k);
					last = amount >= (kSmall[k] ? 0xC8 : 0x7D0);
					t.u32(4 + 4 * k) += uint32_t(amount);
					of.u32(4 + 4 * k) = 0;
				}
				for (uint32_t k = 0; k < 6; ++k) {
					t.u32(0x124 + 4 * k) += of.u32(0x124 + 4 * k);
					of.u32(0x124 + 4 * k) = 0;
				}
				if (last) {
					// The letter 0x43 to the owner (0x4E0278..0x4E02B6):
					// [+0x5] his home town [+0x19], u16 [+0x8] the town
					// [+0x11]; the rest as the constructor leaves it.
					Letter letter(0x43);
					letter.entry.u8(5) = save.merchants[owner].memory.u8(0x19);
					letter.entry.u16(8) = e.u8(0x11);
					send_letter(save, int32_t(owner), letter);
					const uint8_t left = e.u8(0x15);
					e.u8(0x15) = uint8_t(left - 1);
					if (left < 2) {
						keep = false;
					}
					if (now + 0x700 < e.u32(8)) {
						e.u32(8) = now + 0x700;
					}
				}
			}
			o = next;
		}
		// A game bug: this state doesn't move the event's tick, so if it is
		// still in force (keep) the queue runs it again in the same tick,
		// forever (seen with a retouched state, retouch_mail.py pirate_hunt).
		// Fixed: the event ends, as it does in the game when it isn't in force
		// any more (the usual way here).
		if (fix_game_bugs()) {
			return 0;
		}
		return keep ? 1 : 0;
	}
	default:
		return 0;  // Checked with the oracle: scenario pirate_event_branches (saves 3, 5, 7).
	}
}

// 0x4E0334 (evento 0x1F, con su índice): la temporada de piratas. [+0x8 +
// i] (i < 8) ciudades elegidas (0xFF hecha o ninguna) y [+0x10 + i] un byte
// al azar de cada una. De marzo a octubre toma la primera ciudad válida sin
// la situación & 0x40 y decide el tipo de ataque (`kind`): 4 si las seis
// satisfacciones son muy bajas; 2 si las tres primeras son altas y un
// objeto pirata (*0x6DD7AC) pasa de 500 en [+0x16]; 1 si una empresa (tipos
// 4..20) de más de 950 de productividad tiene en la ciudad más de 150
// trabajadores y más de la vigésima parte de los de todas; 0 si falta una de
// las mercancías 1, 5, 7, 9, 0xC, 0xE, 0x12. Con tipo crea un evento 0x1E de
// la ciudad (y la operación 0x84 si [0x6DF31C] es 0) y retrasa las demás.
// Fuera de temporada o sin más ciudades, la operación 0x7E (si [0x6DF31C]
// es 0) y devuelve 0.
uint32_t fn_4e0334(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	if (g.u16(kDayOfYear) < 59) {
		// Checked with the oracle: scenario pirate_event_branches (saves 3, 5, 7). As the game does
		// it: with the tick 0x94 the event comes back at once.
		save.events[event].u32(0) = 0x94;
		return 1;
	}
	uint32_t slot = 0;
	uint32_t empty = 0;
	for (; slot < 8; ++slot) {
		Event& e = save.events[event];
		const uint32_t town = e.u8(8 + slot);
		if (town >= town_count(save)) {
			if (e.u8(0x10 + slot) == 0) {
				++empty;
			}
			continue;
		}
		if ((save.towns[town].memory.u32(0x2C8) & 0x40) == 0x40) {
			e.u8(8 + slot) = 0xFF;
			continue;
		}
		break;
	}
	if (g.u16(kDayOfYear) >= 304 || slot >= 8 || empty > 1) {
		if (g.u32(kAldermanBusy) == 0) {
			Operation op;
			op.u32(0, 0x7E);
			for (uint32_t k = 0; k < 4; ++k) {
				op.u32(4 + 4 * k, game_rand(save));
			}
			enqueue(save, op);
		}
		return 0;
	}
	const uint32_t town = save.events[event].u8(8 + slot);
	TownMemory& t = save.towns[town].memory;
	int32_t kind = -1;
	uint8_t value = 0;  // [esp+0x70]: el objeto pirata, la empresa o la mercancía.
	uint8_t pirate = 0;  // [esp+0xC]
	if (t.s16(0x300) < -20 && t.s16(0x302) < -20 && t.s16(0x304) < -20 && t.s16(0x308) < -20 &&
		t.s16(0x30A) < -22 && t.s16(0x30C) < -24) {
		kind = 4;  // Checked with the oracle: scenario pirate_event_branches (saves 3, 5, 7).
		pirate = 0;
		value = 0;
	} else if (t.s16(0x300) > 10 && t.s16(0x302) > 20 && t.s16(0x304) > 20) {
		// A pirate object of more than 500 of strength. Checked with the oracle: scenario
		// pirate_event_branches (saves 3, 5, 7).
		pirate = 5;
		for (uint32_t i = 0; i < 5; ++i) {
			if (g.u32(kPirates + 4 * i) != 0 && pirate_u16(save, i, 0x16) > 0x1F4) {
				kind = 2;
				value = uint8_t(i);
				pirate = 0xFF;
				break;
			}
		}
	}
	if (g.u16(kWorldRecordCount) > 0) {
		const uint32_t records = g.u16(kWorldRecordCount);
		for (uint32_t k = 4; k < 0x15; ++k) {
			if (t.u16(0x848 + 16 * k) <= 0x3B6) {
				continue;
			}
			int32_t all = 0;
			int32_t here = 0;
			for (uint32_t r = records; r > 0;) {
				--r;
				const GameMemory<0x14>& rec = save.world_records[r];
				if (rec.u8(6) == k) {
					all += rec.u16(4);
					if (rec.u8(7) == t.u8(0x2C0)) {
						here += rec.u16(4);
					}
				}
			}
			if (here > 0x96 && int32_t(uint32_t(here) * 20) > all) {
				// Checked with the oracle: scenario pirate_event_branches (saves 3, 5, 7).
				kind = 1;
				t.u8(0x6F0) = 0;  // [0x682C2B] (.rdata) = 0.
				value = 0;
				pirate = 0;
				break;
			}
		}
	}
	static constexpr uint8_t kWares[7] = {1, 5, 7, 9, 0xC, 0xE, 0x12};
	for (uint32_t j = 0; j < 7; ++j) {
		const uint32_t ware = kWares[j];
		if (t.s32(0x430 + 4 * ware) <= 0) {
			continue;
		}
		// El juego suma tantas veces como ciudades haya los datos de la ciudad
		// j (no los de cada ciudad) y los de sus oficinas.
		int32_t made = 0;
		int32_t used = 0;
		const TownMemory& tj = save.towns[j].memory;
		for (uint32_t i = 0; i < town_count(save); ++i) {
			made = int32_t(uint32_t(made) + tj.u32(0xC4 + 4 * ware));
			used = int32_t(uint32_t(used) + tj.u32(0x64 + 4 * ware) + tj.u32(0x310 + 4 * ware));
			for (uint32_t o = tj.u16(0x784); int32_t(o) < int32_t(office_count(save));) {
				const OfficeMemory& of = save.offices[o].memory;
				used = int32_t(uint32_t(used) + of.u32(0x64 + 4 * ware));
				made = int32_t(uint32_t(made) + of.u32(0xC4 + 4 * ware));
				o = of.u16(0x2CA);
			}
		}
		if (int32_t(t.u32(0xC4 + 4 * ware) * 20) > made && int32_t(t.u32(0x430 + 4 * ware) * 20) > used) {
			t.u8(0x6F0) = uint8_t(ware);
			kind = 0;
			value = uint8_t(ware);
			pirate = 0;
			break;
		}
	}
	if (kind >= 0) {
		g.u32(kEventBusy) = 0;
		const uint32_t tick = g.u32(kTick) + save.events[event].u8(0x10 + slot) + 0x100;
		const uint16_t n = fn_4d8cf0(save, tick);
		g.u32(kEventBusy) = 1;
		Event& e = save.events[event];
		Event& p = save.events[n];
		p.u16(6) = 0x1E;
		p.u8(0x10) = uint8_t(kind);
		p.u8(0x11) = e.u8(8 + slot);
		p.u32(0xC) = p.u32(0) + 0x1500;
		p.u32(8) = p.u32(0) + 0x4300;
		p.u16(0x12) = kind == 1 ? t.u16(0x2C6) : 0xFFFF;
		const uint8_t r = e.u8(0x10 + slot);
		p.u8(0x14) = uint8_t(((r >> 3) & 3) + (r & 7) + 5);
		p.u8(0x16) = 0xFF;
		p.u8(0x17) = 0;
		p.u8(0x15) = uint8_t((e.u8(0x17) & 7) + 2);
		if (g.u32(kAldermanBusy) == 0) {
			// The operation 0x84: a town at random (from rand() % towns, the
			// first with the situation & 0x40 without a human mayor: a citizen
			// or an AI merchant, whose names it takes). With every such town a
			// human's the game would loop forever; it can't happen in a game
			// (a merchant can only be the mayor of his home town, 0x528CF0,
			// and the Hanse has a dozen).
			Operation op;
			op.u32(0, 0x84);
			const uint32_t towns = town_count(save);
			uint32_t i = game_rand(save) % towns;
			int merchant = -1;
			for (;;) {
				const TownMemory& ti = save.towns[i].memory;
				if ((ti.u32(0x2C8) & 0x40) == 0x40) {
					const uint32_t mayor = ti.u8(0x6F1);
					if (mayor >= merchant_count(save)) {
						break;
					}
					// Checked with the oracle: scenario pirate_event_branches (saves 3, 5, 7: the
					// human mayors skipped, an AI one taken); until then p3vita had it the other
					// way round.
					if (save.merchants[mayor].memory.u16(8) != 0) {
						merchant = int(mayor);
						break;
					}
				}
				if (++i >= towns) {
					i = 0;
				}
			}
			op.u8(0xF, uint8_t(i));
			if (merchant < 0) {
				const TownMemory& ti = save.towns[i].memory;
				const uint32_t d = (0xFFFFFFFFu - ti.u8(0x6F1)) & 3;
				op.u8(0xD, ti.u8(0x6D4 + d));
				op.u8(0xE, ti.u8(0x6D8 + d));
				op.u8(0x10, ti.u8(0x6E0 + d));
				op.u8(0x11, ti.u8(0x6E4 + d));
			} else {
				const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
				op.u8(0xD, m.u8(0x1C));
				op.u8(0xE, m.u8(0x1D));
				op.u8(0x10, merchants_detail::fn_4f9430(save, merchant));
				op.u8(0x11, m.u8(0x39C + m.u8(0x19)));
			}
			op.u8(0xC, uint8_t((game_rand(save) & 3) + 2));
			op.u8(0x8, e.u8(8 + slot));
			op.u8(0x9, uint8_t(kind));
			op.u8(0xA, value);
			op.u8(0xB, pirate);
			op.u32(4, ((game_rand(save) & 7) + 5) * 256 + g.u32(kTick));
			enqueue(save, op);
		}
		if (slot < 6) {
			e.u8(9 + slot) = 0xFF;
			e.u32(0) += 0x1E00;
		}
		if (slot < 5) {
			e.u8(0xA + slot) = 0xFF;
			e.u32(0) += 0x1E00;
		}
	}
	Event& e = save.events[event];
	const uint8_t r = e.u8(0x10 + slot);
	e.u8(8 + slot) = 0xFF;
	e.u32(0) += ((uint32_t(r) & 7) + 0x14) << 8;
	e.u32(0) += e.u8(0x10 + slot);
	if (kind >= 0) {
		e.u8(0x10 + slot) = 0;
	}
	return 1;
}


// --- Evento 0x30: el encargo de la Hansa ---

// 0x4F91C0 (sobre el comerciante): en cada ciudad con la situación & 0xC0,
// [+0x120 + 12·i] (float) baja 1,5 (0x672F28).
void fn_4f91c0(SaveGame& save, int merchant) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	for (uint8_t i = 0; i < town_count(save); ++i) {
		if (save.towns[i].memory.u8(0x2C8) & 0xC0) {
			float& v = m.f32(0x120 + 12 * size_t(i));
			v = float(double(v) - double(as_float(0x3FC00000)));
		}
	}
}

// 0x4EA594 (evento 0x30 de la cabeza): el encargo del jefe de la Hansa
// ([0x6DE52D], comerciante) con la reunión en [0x6DE52E] (ciudad). [+0x8]
// tick del encargo (0: nada), [+0xC], [+0x12], [+0x13] tipo (0..7), [+0x14],
// [+0x15]. Con un jefe humano: avisos a los 10 días, a los 20 (el evento se
// queda en [+0x8] + 0x1500 con el byte bajo a 0) y, pasados, se quita y el
// jefe pierde reputación (0x4F91C0) salvo con [+0xC] == 6 o los tipos 5 y 6.
// Sin jefe humano (tipos < 5): un día después de [+0x8] crea el evento 0x21
// de la reunión (a 10 días, ajustado con 0x4D9FC0) y se quita.
// The letters: 0x77 (the task, to each merchant or to the leader) and 0x78
// (the meeting, to each merchant); the ticker line of the first notice
// (0x548CA0, kind 0x21) is the interface's.
uint32_t fn_4ea594(SaveGame& save) {
	GlobalMemory& g = save.globals;
	Event& e = head_event(save);
	if (e.u32(8) == 0) {
		return 0;  // Checked with the oracle: scenario pirate_event_branches (saves 3, 5, 7).
	}
	const uint32_t leader = g.u8(kHanseLeader);
	const bool human = leader < merchant_count(save) && save.merchants[leader].memory.u16(8) == 0;
	if (human) {
		// Checked with retouch_mail.py hanse_task (the three phases and the kinds 0, 2, 5, 7).
		const MerchantMemory& lm = save.merchants[leader].memory;
		if (e.u32(0) < e.u32(8) + 0xA00) {
			e.u32(0) = e.u32(8) + 0xA00;
			// The task's letter 0x77 (0x4EA610..0x4EA77F): [+0x5] the
			// meeting's town; [+0x8] [+0x12], [+0x9] the kind [+0x13], [+0xE]
			// 0, and by the kind: 0 [+0xA] [+0x14]; 2 [+0xA] 0x19; 5 and 6
			// [+0xA] [+0x14], [+0xB] [+0x15], [+0xC], [+0xD] the leader's
			// [+0x1C], [+0x1D], to the leader alone; 7 the same, to each
			// merchant; the others to each merchant as they are.
			Letter letter(0x77);
			letter.entry.u8(5) = g.u8(kHanseTown);
			letter.entry.u8(8) = e.u8(0x12);
			letter.entry.u8(9) = e.u8(0x13);
			switch (e.u8(0x13)) {
			case 0:
				letter.entry.u8(0xA) = e.u8(0x14);
				break;
			case 2:
				// El primer objeto pirata que exista (*0x6DD7AC).
				e.u8(0x14) = uint8_t(e.u8(0x14) % 5);
				if (g.u32(kPirates + 4 * uint32_t(e.u8(0x14))) == 0) {
					for (uint8_t i = 0; i < 5; ++i) {
						if (g.u32(kPirates + 4 * uint32_t(i)) != 0) {
							e.u8(0x14) = i;
							break;
						}
					}
				}
				// A game bug (0x4EA6A8): the test is the wrong way round.
				// With the pirate object the family name of its captain
				// (0x515AE0, as without a human leader below) is never asked
				// and [+0xA] is 0x19; with no pirate object at all, the game
				// calls 0x515AE0 on a null pointer (it would crash; here
				// [+0xA] stays 0). Fixed: the captain's family name, and 0x19
				// without a pirate object.
				if (fix_game_bugs()) {
					const uint32_t k = e.u8(0x14);
					if (g.u32(kPirates + 4 * k) != 0) {
						uint8_t first = 0;
						fn_515ae0(save, k, first, letter.entry.u8(0xA));
					} else {
						letter.entry.u8(0xA) = 0x19;
					}
				} else if (g.u32(kPirates + 4 * uint32_t(e.u8(0x14))) != 0) {
					letter.entry.u8(0xA) = 0x19;
				}
				break;
			case 5:
			case 6:
				letter.entry.u8(0xA) = e.u8(0x14);
				letter.entry.u8(0xB) = e.u8(0x15);
				letter.entry.u8(0xC) = lm.u8(0x1C);
				letter.entry.u8(0xD) = lm.u8(0x1D);
				send_letter(save, int32_t(leader), letter);
				return 1;
			case 7:
				letter.entry.u8(0xA) = e.u8(0x14);
				letter.entry.u8(0xB) = e.u8(0x15);
				letter.entry.u8(0xC) = lm.u8(0x1C);
				letter.entry.u8(0xD) = lm.u8(0x1D);
				break;
			default:
				break;
			}
			send_letter(save, kLetterToEach, letter);
			// «X has disobeyed the orders of the Hanseatic League.» to the
			// leader (0x4EA7A5: kind 0x21, [+0x12] as a town).
			send_ticker(save, leader, 0x21, e.u8(0x12));
			return 1;
		}
		const bool exempt = e.u8(0xC) == 6 || e.u8(0x13) == 5 || e.u8(0x13) == 6;
		if (e.u32(0) < e.u32(8) + 0x1400) {
			if (!exempt) {
				// The reminder 0x77 to the leader (0x4EA7EE..0x4EA825): [+0x5]
				// the meeting's town, [+0xE] 1.
				Letter letter(0x77);
				letter.entry.u8(5) = g.u8(kHanseTown);
				letter.entry.u8(0xE) = 1;
				send_letter(save, int32_t(leader), letter);
			}
			e.u32(0) = e.u32(8) + 0x1500;
			e.u8(0) = 0;
			return 1;
		}
		if (exempt) {
			return 0;
		}
		// The task failed, 0x77 to the leader (0x4EA879..0x4EA8B0): [+0x5]
		// the meeting's town, [+0xE] 2.
		Letter letter(0x77);
		letter.entry.u8(5) = g.u8(kHanseTown);
		letter.entry.u8(0xE) = 2;
		send_letter(save, int32_t(leader), letter);
		fn_4f91c0(save, int(leader));
		return 0;
	}
	if (e.u8(0x13) >= 5) {
		return 0;  // Checked with the oracle: scenario pirate_event_branches (saves 3, 5, 7).
	}
	if (e.u32(0) < e.u32(8) + 0x100) {
		// Checked with retouch_mail.py hanse_task_ai (kinds 0, 1, 2). The task's letter 0x77 to each
		// merchant (0x4EA909..0x4EA979): [+0x5] the meeting's town, [+0x8]
		// [+0x12], [+0x9] the kind, [+0xE] 0; for the kind 0 [+0xA] [+0x14],
		// for the kind 2 [+0xA] the family name of the captain of the
		// pirate object [+0x14] (0x515AE0; the game doesn't check the
		// object: a missing one or [+0x14] from 5 on would read a null or
		// wrong pointer; here [+0xA] stays 0).
		Letter letter(0x77);
		letter.entry.u8(5) = g.u8(kHanseTown);
		letter.entry.u8(8) = e.u8(0x12);
		letter.entry.u8(9) = e.u8(0x13);
		if (e.u8(0x13) == 0) {
			letter.entry.u8(0xA) = e.u8(0x14);
		} else if (e.u8(0x13) == 2) {
			const uint32_t k = e.u8(0x14);
			if (k < 5 && g.u32(kPirates + 4 * k) != 0) {
				uint8_t first = 0;
				fn_515ae0(save, k, first, letter.entry.u8(0xA));
			}
		}
		send_letter(save, kLetterToEach, letter);
		e.u32(0) = e.u32(8) + 0x100;
		return 1;
	}
	uint32_t tick = g.u32(kTick) + 0xA00;
	construction_detail::fn_4d9fc0(save, tick, g.u8(kHanseTown));
	g.u32(kEventBusy) = 0;
	const uint16_t n = fn_4d8cf0(save, tick);
	g.u32(kEventBusy) = 1;
	Event& p = save.events[n];
	const Event& h = head_event(save);
	p.u16(6) = 0x21;
	p.u8(8) = g.u8(kHanseTown);
	p.u8(9) = h.u8(0xC) != 0 ? h.u8(0x12) : 0xFF;
	p.u8(0xA) = h.u8(0x13);
	p.u8(0xB) = h.u8(0xC);
	p.u8(0xC) = h.u8(0x14);
	p.u8(0xD) = h.u8(0x15);
	p.u8(0xE) = h.u8(0xC);
	for (uint32_t k = 0xF; k <= 0x16; ++k) {
		p.u8(k) = 0;
	}
	// The meeting's letter 0x78 to each merchant (0x4EAA54..0x4EAB45):
	// [+0x5] the meeting's town; [+0x8] the day and [+0x9] the month of the
	// new event (0x5311D0); [+0xA] its [+0x9]; [+0xB] its [+0xE] << 4 + its
	// [+0xA]; for the kind 0 [+0xC] its [+0xC]; [+0xD] the leader's title
	// (0x4F9430), [+0xE], [+0xF] his [+0x1C], [+0x1D]. Without a leader, the
	// title and names of the councillor ((0xFF − the mayor [+0x6F1]) & 3) of
	// the meeting's town ([+0x6E0], [+0x6D4], [+0x6D8]).
	Letter letter(0x78);
	letter.entry.u8(5) = g.u8(kHanseTown);
	if (leader < merchant_count(save)) {
		const MerchantMemory& lm = save.merchants[leader].memory;
		letter.entry.u8(0xE) = lm.u8(0x1C);
		letter.entry.u8(0xF) = lm.u8(0x1D);
		letter.entry.u8(0xD) = merchants_detail::fn_4f9430(save, int(leader));
	} else {
		const TownMemory& t = save.towns[g.u8(kHanseTown)].memory;
		const uint32_t k = uint8_t(0xFF - t.u8(0x6F1)) & 3;
		letter.entry.u8(0xE) = t.u8(0x6D4 + k);
		letter.entry.u8(0xF) = t.u8(0x6D8 + k);
		letter.entry.u8(0xD) = t.u8(0x6E0 + k);
	}
	events_mayor_detail::fn_5311d0(p.u32(0), letter.entry.u8(8), letter.entry.u8(9));
	letter.entry.u8(0xA) = p.u8(9);
	letter.entry.u8(0xB) = uint8_t((p.u8(0xE) << 4) + p.u8(0xA));
	if (p.u8(0xA) == 0) {
		letter.entry.u8(0xC) = p.u8(0xC);
	}
	send_letter(save, kLetterToEach, letter);
	return 0;
}

// --- Evento 0x2E: la votación del consejo ---

// 0x4FAC70 (sobre town_class1, [+0x7A4] de la ciudad): de las celdas de la
// rejilla con el bit 0x80, el porcentaje de las que tienen el bit 0x20 entre
// ellas y las que (sin él) tienen en la celda siguiente la clase (& 0x1F)
// 0xB o 0x12..0x1E; 100 si no hay ninguna.
uint32_t fn_4fac70(const SaveGame& save, int town) {
	const SavedTown& st = save.towns[size_t(town)];
	const int32_t n = int32_t(st.memory.u32(0x7AC) * st.memory.u32(0x7A8));
	uint32_t all = 0;
	uint32_t marked = 0;
	const auto cell = [&](int64_t i) -> uint8_t {
		// La última celda mira la siguiente, fuera de la rejilla: aquí 0.
		return i >= 0 && i < int64_t(st.grid.size()) ? st.grid[size_t(i)] : 0;
	};
	int64_t i = 0;
	for (int32_t k = n; k > 0; --k) {
		const uint8_t c = cell(i);
		++i;
		if (!(c & 0x80)) {
			continue;
		}
		if (c & 0x20) {
			++marked;
			++all;
			continue;
		}
		const uint32_t type = cell(i) & 0x1F;
		if (type == 0xB || (type > 0x11 && type <= 0x1E)) {
			++all;
		}
	}
	return all ? marked * 100 / all : 100;
}

// 0x52A150 (sobre la ciudad): el impuesto por cabeza [+0x6F8] = `rate`, o 10 si pasa de 20.
void fn_52a150(SaveGame& save, int town, uint8_t rate) {
	save.towns[size_t(town)].memory.u8(0x6F8) = rate > 0x14 ? 0xA : rate;
}

// Una pieza de muralla de 0x4FA1D0: celda (x, y), tipo (0x36 tramo, 0x37 y
// 0x38 esquinas o torres) y orientación.
struct WallPiece {
	uint8_t x = 0;
	uint8_t y = 0;
	uint8_t type = 0;
	uint8_t param = 0;
};

// 0x4FA1D0 (sobre town_class1, [+0x7A4] de la ciudad): recorre la muralla
// planeada (celdas 0xA de la rejilla) desde el punto `side` ([+0xC + 2·side]
// de town_class1, [0x7B0] de la ciudad) y apunta sus piezas: cada celda un
// tramo (0x36), y las celdas 7, 8 y 9 que encuentra al girar, una pieza 0x37
// o 0x38 más. Marca con 0x40 las celdas que deja atrás si no son 0xA.
// `corners` es el [+0x4] de la lista del juego.
void fn_4fa1d0(SaveGame& save, int town, uint32_t side, std::vector<WallPiece>& out, uint32_t& corners) {
	SavedTown& st = save.towns[size_t(town)];
	const TownMemory& t = st.memory;
	out.clear();
	corners = 0;
	const int32_t w = t.s32(0x7A8);
	const int32_t h = t.s32(0x7AC);
	const int32_t n = int32_t(uint32_t(w) * uint32_t(h));
	// Orientación de cada pieza según la dirección de llegada (0..4) y la
	// búsqueda que encuentra la siguiente (0..3, en su orden) o 4 al
	// terminar: palabras de la pila de 0x4FA1D0, con los bytes de
	// 0x677928..0x67792F.
	static constexpr uint8_t kParam[5][5] = {
		{0xE0, 0x80, 0xE0, 0x40, 0x61},
		{0x80, 0x20, 0xC0, 0x20, 0xA1},
		{0xE0, 0xC0, 0xE0, 0x00, 0xE1},
		{0x40, 0x20, 0x00, 0x20, 0x21},
		{0x61, 0xA1, 0xE1, 0x21, 0x20},
	};
	const uint8_t sx = t.u8(0x7B0 + 2 * side);
	if (sx == 0xFF) {
		return;
	}
	const auto at = [&](int32_t i) -> uint8_t& {
		assert(i >= 0 && size_t(i) < st.grid.size());
		return st.grid[size_t(i)];
	};
	int32_t p = int32_t(uint32_t(sx) * uint32_t(h)) + t.u8(0x7B1 + 2 * side);
	int32_t prev = -1;
	uint32_t dir = 4;
	if (at(p) != 0xA) {
		return;
	}
	size_t count = 0;
	const auto entry = [&](size_t k) -> WallPiece& {
		if (out.size() <= k) {
			out.resize(k + 1);
		}
		return out[k];
	};
	const auto mark = [&](int32_t i) {
		if (at(i) != 0xA) {
			at(i) |= 0x40;
		}
	};
	for (;;) {
		if (at(p) != 0xA) {
			at(prev) |= 0x40;
			at(p) |= 0x40;
		}
		entry(count).type = 0x36;
		entry(count).x = uint8_t(p / h);
		entry(count).y = uint8_t(p % h);
		int32_t q = p - h - 2;
		if (q >= 0 && q != prev) {
			const uint8_t c = at(q);
			if (c == 0xA) {
				entry(count++).param = kParam[dir][0];
				dir = 2;
				prev = p;
				p = q;
				continue;
			}
			if (c == 8) {
				entry(count++).param = kParam[dir][0];
				dir = 2;
				++corners;
				WallPiece& c2 = entry(count);
				c2.type = 0x38;
				c2.param = 0xE0;  // 0x67792F
				c2.x = uint8_t(q / h);
				c2.y = uint8_t(q % h);
				++count;
				const int32_t d = (h + 2) * 3;
				prev = p - d;
				p = q - d;
				mark(p);
				continue;
			}
		}
		q = p - h + 2;
		if (q >= 0 && q != prev) {
			const uint8_t c = at(q);
			if (c == 0xA) {
				entry(count++).param = kParam[dir][3];
				dir = 1;
				prev = p;
				p = q;
				continue;
			}
			if (c == 9) {
				entry(count++).param = kParam[dir][3];
				dir = 1;
				const int32_t r = q + 2 * (2 - h);
				prev = r;
				++corners;
				WallPiece& c2 = entry(count);
				c2.type = 0x37;
				c2.param = 0x20;  // 0x677929
				c2.x = uint8_t(r / h);
				c2.y = uint8_t(r % h);
				++count;
				p = r - h + 2;
				mark(p);
				continue;
			}
		}
		q = p + h - 2;
		if (q < n && q != prev) {
			const uint8_t c = at(q);
			if (c == 0xA) {
				entry(count++).param = kParam[dir][1];
				dir = 3;
				prev = p;
				p = q;
				continue;
			}
			if (c == 7) {
				entry(count++).param = kParam[dir][1];
				dir = 3;
				++corners;
				WallPiece& c2 = entry(count);
				c2.type = 0x37;
				c2.param = 0x20;  // 0x677929
				c2.x = uint8_t(q / h);
				c2.y = uint8_t(q % h);
				++count;
				const int32_t d = (h - 2) * 3;
				prev = d + p;
				p = d + q;
				mark(p);
				continue;
			}
		}
		q = p + h + 2;
		if (q < n && q != prev) {
			const uint8_t c = at(q);
			if (c == 0xA) {
				entry(count++).param = kParam[dir][2];
				dir = 0;
				prev = p;
				p = q;
				continue;
			}
			if (c == 9) {
				entry(count++).param = kParam[dir][2];
				dir = 0;
				const int32_t r = q + 2 * h + 4;
				++corners;
				prev = r;
				WallPiece& c2 = entry(count);
				c2.type = 0x38;
				c2.param = 0xE0;  // 0x67792F
				c2.x = uint8_t(r / h);
				c2.y = uint8_t(r % h);
				++count;
				p = r + h + 2;
				mark(p);
				continue;
			}
		}
		entry(count++).param = kParam[dir][4];
		break;
	}
	out.resize(count);
}

// 0x522B20 (sobre la ciudad): si no hay obras pendientes ([+0x766] =
// 0xFFFF) y el punto de muralla `side` está en la rejilla, mete en la lista
// de obras pendientes las piezas de la muralla (0x4FA1D0), de la ciudad
// (dueño 0xFF − side) y con su tiempo de obra (0x672BB4), y borra el punto.
void fn_522b20(SaveGame& save, int town, uint32_t side) {
	if (save.towns[size_t(town)].memory.u16(0x766) < 0xFFFF || !fn_4fcfe0(save, town, side)) {
		return;
	}
	// 0x672BB4: tiempo de obra de cada tipo de edificio.
	static constexpr uint8_t kBuildTime[64] = {0xC8, 0xFA, 0x19, 0xC8, 0x64, 0x64, 0x64, 0xC8, 0x64, 0x32, 0xC8,
		0x32, 0xC8, 0x32, 0x32, 0xC8, 0xC8, 0x64, 0x19, 0x19, 0x32, 0xC8, 0xC8, 0xC8, 0x64, 0x64, 0x64, 0x32, 0x32,
		0x32, 0x64, 0xFA, 0xFA, 0xC8, 0xC8, 0xC8, 0xC8, 0x96, 0xC8, 0x64, 0x19, 0xC8, 0xFA, 0xC8, 0xC8, 0x05, 0x00,
		0x32, 0xC8, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x19, 0x32, 0x32, 0x00, 0x00, 0x00, 0x03, 0x04, 0x01, 0x02};
	std::vector<WallPiece> pieces;
	uint32_t corners = 0;
	fn_4fa1d0(save, town, side, pieces, corners);
	for (const WallPiece& w : pieces) {
		const uint16_t r = construction_detail::fn_51f100(save, town);
		SavedTown& s = save.towns[size_t(town)];
		BuildingRecord& rec = s.records[r];
		rec.x = w.x;
		rec.y = w.y;
		rec.owner = uint8_t(0xFF - side);
		rec.type = w.type;
		rec.next = s.memory.u16(0x766);
		rec.pending = w.type < 64 ? kBuildTime[w.type] : 0;
		rec.param = w.param;
		fn_4fb9c0(save, town, rec.x, rec.y, rec.type, 0);
		s.memory.u16(0x766) = r;
	}
	TownMemory& t = save.towns[size_t(town)].memory;
	t.u8(0x7B0 + 2 * side) = 0xFF;
	t.u8(0x7B1 + 2 * side) = 0xFF;
}

// 0x4E9A94 (evento 0x2E de la cabeza): la votación del consejo de la ciudad
// [+0xC] sobre la propuesta [+0xD] (0 el impuesto extra, 1 la muralla, 2
// [+0x9A0], 3 el impuesto por cabeza [+0x14]). Al empezar el día (tick &
// 0xFF == 0) reparte los 22 votos ([+0xF], menos uno por cada concejal
// comerciante [+0x6DC..+0x6DF] y los de los jugadores humanos de rango 4, 5
// o 6 en la ciudad, que se apuntan con un bit en [+0x13]) y decide cuántos
// van a favor ([+0xE]); cada 4 ticks (cada uno a partir del 0xDC) sale un
// voto, a favor ([+0x10]) o en contra ([+0x11]). Al acabar el día, si ganan
// los de a favor, se hace lo propuesto: 0 un evento 0x31 un día después, 1
// la muralla (0x522B20 por el lado 1 o el 2), 2 [+0x9A0] += 10, 3 0x52A150.
// The result goes by letter (0x6B) to the mayor and to [+0x16]; the notice
// to a voting player (0x548CA0) is the interface's.
uint32_t fn_4e9a94(SaveGame& save) {
	GlobalMemory& g = save.globals;
	const uint32_t t8 = g.u32(kTick) & 0xFF;
	Event& e = head_event(save);
	const uint32_t town = e.u8(0xC);
	if (town >= town_count(save)) {
		return 0;  // Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
	}
	TownMemory& t = save.towns[town].memory;
	if (t8 == 0) {
		e.u8(0x10) = 0;
		e.u8(0x11) = 0;
		e.u8(0x12) = 0;
		e.u8(0xE) = 0;
		e.u8(0xF) = 0x16;
		e.u8(0x13) = 0;
		for (uint32_t k = 0; k < 4; ++k) {
			if (t.u8(0x6DC + k) < merchant_count(save)) {
				e.u8(0xF) = uint8_t(e.u8(0xF) - 1);
			}
		}
		uint32_t human = 0;
		for (uint32_t m = 0; m < merchant_count(save); ++m) {
			const MerchantMemory& mm = save.merchants[m].memory;
			if (mm.u16(8) != 0) {
				continue;
			}
			const uint32_t rank = mm.u8(0x39C + town);
			if (rank >= 4) {
				// «A meeting of the town council is taking place in X.» (0x4E9B90:
				// kind 0x20, the town).
				send_ticker(save, m, 0x20, uint16_t(town));
				const uint8_t votes = e.u8(0xF);
				e.u8(0xF) = uint8_t(rank == 4 ? votes - 1 : rank == 5 ? votes - 2 : votes - 3);
				e.u8(0x13) = uint8_t(e.u8(0x13) | uint8_t(1u << (human & 31)));
			}
			++human;
		}
		if (e.s8(0xF) < 4) {
			e.u8(0xF) = 4;
		}
		const uint8_t vb = e.u8(0xF);
		const int32_t votes = int8_t(vb);
		int32_t yes = 0;
		switch (e.u8(0xD)) {
		case 0: {
			// The extra tax: fewer votes for the more has been paid ([+0x8], and the town's
			// [+0x6F4] if it was recent). Checked with the oracle: scenario council_task_branches
			// (saves 1, 3, 5, 7): recent, the share between, over 90 and under 10.
			uint32_t paid = e.u32(8);
			if (g.u32(kTick) < (t.u32(0x6F8) & 0xFFFFFF00u) + 0x5400) {
				paid += t.u32(0x6F4);
			}
			const double root = std::sqrt(double(t.s32(0x2D4)));
			paid -= uint32_t(ftol32(root * 100.0));  // 0x66DE48
			int32_t share = 0x5A - int32_t(paid) / 1500;
			if (share > 0x5A) {
				share = 0x5A;
			} else if (share < 0xA) {
				share = 0xA;
			}
			yes = int32_t(uint32_t(votes) * uint32_t(share)) / 100;
			break;
		}
		case 1:
			if (e.u8(0x15) != 0) {
				// Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
				yes = int32_t(uint32_t(votes) * 9) / 10;
			} else if (t.u16(0x766) < t.u16(0x776)) {
				yes = int8_t(vb) >> 4;  // Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
			} else if (t.u16(0x78C) < t.u16(0x78E)) {
				yes = int32_t(uint32_t(votes) * 4) / 5;
			} else {
				// Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
				yes = int32_t((uint32_t(0x64 - fn_4fac70(save, int(town))) * uint32_t(votes)) / 100);
			}
			break;
		case 2: {
			uint32_t people = t.u32(0x2D4);
			if ((t.u32(0x2C8) & 0x10) == 0x10) {
				people *= 2;  // Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
			}
			const int32_t weight = int32_t(people + 0x200 - 203 * uint32_t(t.u16(0x9A0)));
			if (weight < 0x64) {
				yes = votes / 10;  // Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
			} else if (weight > 0x398) {
				// Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
				yes = int32_t(uint32_t(votes) * 9) / 10;
			} else {
				yes = int32_t(uint32_t(votes) * uint32_t(weight)) >> 10;
			}
			break;
		}
		case 3:
			// The tax per head. Checked with the oracle: scenario council_task_branches (saves 1,
			// 3, 5, 7): under and over the town's rate.
			if (e.u8(0x14) < t.u8(0x6F8)) {
				yes = int32_t(uint32_t(votes) * 9) / 10;
			} else {
				const int32_t a = int32_t(t.u8(0x6F8)) - int32_t(e.u8(0x14)) + 0x32;
				const int32_t b = 0x19 - int32_t(e.u8(0x14));
				yes = int32_t(uint32_t(votes) * (uint32_t(a) * uint32_t(b) * 10)) / 10000;
			}
			break;
		default:
			return 0;  // Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
		}
		e.u8(0xE) = uint8_t(yes);
		e.u8(0xF) = uint8_t(vb - uint8_t(yes));
		e.u32(0) += 4;
	} else {
		const uint8_t no = e.u8(0xF);
		if (no == 0) {
			if (e.s8(0xE) > 0) {
				e.u8(0xE) = uint8_t(e.u8(0xE) - 1);
				e.u8(0x10) = uint8_t(e.u8(0x10) + 1);
			}
		} else if (e.u8(0xE) == 0) {
			e.u8(0xF) = uint8_t(no - 1);
			e.u8(0x11) = uint8_t(e.u8(0x11) + 1);
		} else {
			// Sale el voto del lado con más peso: 3 · pendientes + emitidos.
			const int32_t yes = 3 * int32_t(e.s8(0xE)) + e.u8(0x10);
			const int32_t against = 3 * int32_t(int8_t(no)) + e.u8(0x11);
			if (yes > against) {
				e.u8(0xE) = uint8_t(e.u8(0xE) - 1);
				e.u8(0x10) = uint8_t(e.u8(0x10) + 1);
			} else {
				e.u8(0xF) = uint8_t(no - 1);
				e.u8(0x11) = uint8_t(e.u8(0x11) + 1);
			}
		}
		e.u32(0) += int32_t(t8) < 0xDC ? 4 : 1;
	}
	if (t8 < (e.u32(0) & 0xFF)) {
		return 1;
	}
	// The result, the letter 0x6B (0x4E9EE8..0x4E9FBD) to the mayor
	// [+0x6F1] and to [+0x16] if it isn't him (each if it is a merchant):
	// [+0x5] the town; for the proposal 3, u16 [+0x8] the town's [+0x6F8]
	// << 8 + [+0x14], else u32 [+0x8] the event's [+0x8]; u16 [+0xC] the
	// votes for << 8 + against, [+0xE] the proposal.
	{
		const uint32_t mayor = t.u8(0x6F1);
		const uint32_t asked = e.u8(0x16);
		if (mayor < merchant_count(save) || asked < merchant_count(save)) {
			Letter letter(0x6B);
			letter.entry.u8(5) = e.u8(0xC);
			if (e.u8(0xD) == 3) {
				// Checked with retouch_mail.py council.
				letter.entry.u16(8) = uint16_t((t.u8(0x6F8) << 8) + e.u8(0x14));
			} else {
				letter.entry.u32(8) = e.u32(8);
			}
			letter.entry.u16(0xC) = uint16_t((e.u8(0x10) << 8) + e.u8(0x11));
			letter.entry.u16(0xE) = e.u8(0xD);
			if (mayor < merchant_count(save)) {
				send_letter(save, int32_t(mayor), letter);
			}
			if (asked < merchant_count(save) && asked != mayor) {
				send_letter(save, int32_t(asked), letter);
			}
		}
	}
	if (e.u8(0x10) <= e.u8(0x11)) {
		return 0;
	}
	switch (e.u8(0xD)) {
	case 0: {
		// Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
		g.u32(kEventBusy) = 0;
		const uint16_t n = fn_4d8cf0(save, head_event(save).u32(0) + 0x100);
		g.u32(kEventBusy) = 1;
		Event& p = save.events[n];
		const Event& h = head_event(save);
		p.u16(6) = 0x31;
		p.u32(8) = h.u32(8);
		p.u8(0xC) = h.u8(0xC);
		break;
	}
	case 1:
		if (fn_4fcfe0(save, int(town), 1)) {
			fn_522b20(save, int(town), 1);
		} else if (fn_4fcfe0(save, int(town), 2)) {
			// Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
			fn_522b20(save, int(town), 2);
		}
		break;
	case 2:
		// Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7): under 0x8000
		// and at it.
		if (t.u16(0x9A0) < 0x8000) {
			t.u16(0x9A0) = uint16_t(t.u16(0x9A0) + 0xA);
		}
		break;
	case 3:
		// Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
		fn_52a150(save, int(town), e.u8(0x14));
		break;
	default:
		break;
	}
	return 0;
}


// --- Evento 0x32: los encargos del jefe de la Hansa ---

// 0x532C50 (sobre el mundo, con el índice de un evento): ¿sigue en pie el
// encargo del evento 0x32? Si no es un 0x32 o su comerciante [+0xD] no pasa
// del número de comerciantes, sí. Según el tipo [+0xC]: 0 que ninguna
// ciudad ni guarida pirata tenga el número de ciudad [+0x14]; 1 que ni la
// ciudad [+0x10] ni la [+0x11] tengan el bit 0x8000 de [+0x76C]; 2 que el barco [+0x10]
// sea pirata (estado 0x12) con un capitán de nombre [+0x12], [+0x14]; 3 que
// el objeto pirata [+0x10] tenga la guarida [+0x11]; 4 que la ciudad [+0x12]
// tenga menos de [+0x10] / 2 habitantes.
bool fn_532c50(const SaveGame& save, uint32_t index) {
	const GlobalMemory& g = save.globals;
	if (index >= g.u16(kEventCount)) {
		return true;  // Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
	}
	const Event& e = save.events[index];
	if (e.u16(6) != 0x32 || e.u8(0xD) <= merchant_count(save)) {
		// Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7): another kind,
		// and the merchant the count.
		return true;
	}
	switch (e.u8(0xC)) {
	case 0:
		for (uint32_t i = 0; i < town_count(save); ++i) {
			if (g.u8(kTownIds + i) == e.u8(0x14)) {
				return false;
			}
		}
		for (uint32_t k = 0; k < 5; ++k) {
			if (g.u32(kPirates + 4 * k) != 0 && pirate_u8(save, k, 0xE) == e.u8(0x14)) {
				return false;
			}
		}
		return true;
	case 1: {
		const uint32_t a = e.u8(0x10);
		if (a >= town_count(save)) {
			return false;  // Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
		}
		if (save.towns[a].memory.u32(0x76C) & 0x8000) {
			return false;  // Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
		}
		return (save.towns[e.u8(0x11)].memory.u32(0x76C) & 0x8000) == 0;
	}
	case 2: {
		// Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7): a ship out,
		// not a pirate's, without a captain, the captain's names and another first or family name.
		const uint32_t s = e.u16(0x10);
		if (s >= ship_count(save)) {
			return false;
		}
		const ShipMemory& ship = save.ships[s].memory;
		if (ship.u16(0x134) != 0x12) {
			return false;
		}
		const uint32_t c = ship.u16(0x42);
		if (c >= g.u16(kCaptainCount)) {
			return false;
		}
		const CaptainMemory& cap = save.captains[c];
		return cap.u8(2) == e.u16(0x12) && cap.u8(3) == e.u16(0x14);
	}
	case 3: {
		const uint32_t k = e.u8(0x10);
		if (k >= 5 || g.u32(kPirates + 4 * k) == 0) {
			// Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7): the
			// object 5 and a missing one.
			return false;
		}
		return pirate_u8(save, k, 0xE) == e.u8(0x11);
	}
	case 4: {
		const uint32_t town = e.u8(0x12);
		if (town >= town_count(save)) {
			return false;  // Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
		}
		return save.towns[town].memory.s32(0x2D4) < int32_t(e.u16(0x10) >> 1);
	}
	default:
		return true;  // Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7).
	}
}

// 0x4EAFE4 (evento 0x32 de la cabeza): un encargo del jefe de la Hansa
// ([+0xC] tipo, < 5) para un comerciante [+0xD]. Sin comerciante, mientras
// el encargo siga en pie (0x532C50), cada 2 días sube [+0xE] en 2 hasta
// 0x3F; entonces se lo da al de índice más bajo de la IA con [+0x37] de 1 a
// 3 y reputación (0x4F73D0) positiva, y vuelve en [día del año] + 3 días.
uint32_t fn_4eafe4(SaveGame& save) {
	GlobalMemory& g = save.globals;
	const uint16_t head = g.u16(kEventHead);
	Event& e = save.events[head];
	if (e.u8(0xD) < merchant_count(save)) {
		// El encargo de un comerciante (0x4EB0EB..0x4EBF0F): alderman.cpp.
		return fn_4eb0eb(save);
	}
	if (e.u8(0xC) >= 5 || !fn_532c50(save, head)) {
		return 0;
	}
	if (e.u8(0xE) < 0x3F) {
		e.u8(0xE) = uint8_t(e.u8(0xE) + 2);
		e.u32(0) += 0x200;
		return 1;
	}
	// Checked with the oracle: scenario council_task_branches (saves 1, 3, 5, 7): given to the
	// lowest of two, and to none.
	for (uint32_t m = merchant_count(save); m > 0;) {
		--m;
		const MerchantMemory& mm = save.merchants[m].memory;
		if (mm.u16(8) != 0 && mm.u8(0x37) > 0 && mm.u8(0x37) < 4 && events_detail::fn_4f73d0(save, int(m)) > 0) {
			e.u8(0xD) = uint8_t(m);
		}
	}
	e.u8(0xF) = 0;
	e.u32(0) += uint32_t(g.u16(kDayOfYear)) + 0x300;
	return e.u8(0xD) < merchant_count(save) ? 1 : 0;
}


// --- Evento 0x12: los convoyes de camino a puntos lejanos ---

// Un punto del mapa (u16 x, u16 y) de un convoy ([+0x24 + 4·k]).
struct MapPoint {
	uint16_t x = 0;
	uint16_t y = 0;
};

MapPoint convoy_point(const GameMemory<0x3C>& c, uint32_t k) {
	return {c.u16(0x24 + 4 * k), c.u16(0x26 + 4 * k)};
}

// ftol(raíz(d2) · f).
int32_t scaled_root(int32_t d2, double f) {
	return ftol32(std::sqrt(double(d2)) * f);
}

// 0x52D950 (cdecl): la distancia por mar desde el punto `p` hasta la salida
// del mapa, por las zonas de 0x52DB50: de la boca (0xE2, 0xD) si y <= (260
// − x) · 5 / 2; si no, por (0xD8, 0x46) y, si y <= 15x / 11 − 380, por
// (0x145, 0x40). En 29 avos de casilla (0x6739E0 y 0x6739D8: ±29).
int32_t fn_52d950(MapPoint p) {
	constexpr double k29 = 29.0;  // 0x6739E0
	constexpr double kMinus29 = -29.0;  // 0x6739D8
	int32_t d = ftol32(std::sqrt(569.0) * k29);
	const int32_t x = p.x;
	const int32_t y = p.y;
	if (y <= (0x104 - x) * 5 / 2) {
		const int32_t dy = y - 0xD;
		const int32_t dx = x - 0xE2;
		const int32_t d2 = int32_t(uint32_t(dy) * uint32_t(dy) + uint32_t(dx) * uint32_t(dx));
		if (p.x > 0xD8) {
			return d - scaled_root(d2, kMinus29);
		}
		return d - 2 * scaled_root(d2, kMinus29);
	}
	d += ftol32(std::sqrt(3349.0) * k29);
	if (y > (x * 15) / 11 - 0x17C) {
		// El juego mide aquí la distancia del punto a sí mismo: 0.
		return d - scaled_root(0, kMinus29);
	}
	d += ftol32(std::sqrt(11917.0) * k29);
	const int32_t dy = y - 0x40;
	const int32_t dx = x - 0x145;
	const int32_t d2 = int32_t(uint32_t(dy) * uint32_t(dy) + uint32_t(dx) * uint32_t(dx));
	return d - scaled_root(d2, kMinus29);
}

// La zona de un punto para 0x52DB50: 0 al oeste de x = 0xD8; si no, 1 si y
// <= (260 − x) · 5 / 2, 3 si y <= 15x / 11 − 380 y 2 si no.
uint32_t sea_zone(MapPoint p) {
	if (p.x < 0xD8) {
		return 0;
	}
	const int32_t x = p.x;
	const int32_t y = p.y;
	if (y <= (0x104 - x) * 5 / 2) {
		return 1;
	}
	return y <= (x * 15) / 11 - 0x17C ? 3 : 2;
}

// 0x52DB50 (cdecl): la distancia por mar entre dos puntos, pasando por los
// puntos de paso de las zonas que haya entre ellas ((0xE2, 0xD), (0xD8,
// 0x46), (0x145, 0x40)); el primer tramo cuenta tantas veces como la zona
// de salida (dos desde la 0). En 29 avos de casilla.
int32_t fn_52db50(MapPoint a, MapPoint b) {
	uint32_t lo = sea_zone(a);
	uint32_t hi = sea_zone(b);
	MapPoint from = a;
	MapPoint to = b;
	if (lo > hi) {
		std::swap(lo, hi);
		std::swap(from, to);
	}
	int32_t weight = int32_t(lo);
	if (lo == 0) {
		weight = 2;
		if (hi > 1) {
			lo = 1;
		}
	}
	int32_t sum = 0;
	MapPoint via{};
	for (uint32_t k = lo; k <= hi; ++k) {
		switch (k) {
		case 0:
			via = {0xE2, 0xD};
			break;
		case 1:
			via = {0xD8, 0x46};
			break;
		case 2:
			via = {0x145, 0x40};
			break;
		default:
			break;
		}
		if (k == hi) {
			via = to;
		}
		const int32_t dx = int32_t(from.x) - int32_t(via.x);
		const int32_t dy = int32_t(from.y) - int32_t(via.y);
		const int32_t d2 = int32_t(uint32_t(dy) * uint32_t(dy) + uint32_t(dx) * uint32_t(dx));
		sum = int32_t(uint32_t(sum) + uint32_t(scaled_root(d2, 29.0)) * uint32_t(weight));
		weight = 1;
		from = via;
	}
	return sum;
}

// --- Evento 0x12: la llegada del convoy ---

// Los 15 objetos de 0x2C bytes del mundo ([+0xB10], 0x6DEFB0): sitios fuera
// del mapa a los que llegan las expediciones (ver la cabecera).
constexpr uint32_t kWorldObjects = global::kWorld + 0xB10;
constexpr uint32_t kWorldObjectSize = 0x2C;
constexpr uint32_t kWorldObjectCount = 15;
// Objeto 0x6DF278 (lo crea 0x5346A0), igual que el [+0x4D8] de cada
// comerciante: u32 [+0x0..+0x2F] un bit por punto de Expeditionen.dat al
// que ha llegado una expedición, [+0x30..+0x5F] los mismos cuando llega con
// el [+0x38] del convoy a 0, u32 [+0x60] un bit por objeto del mundo al que
// ha llegado; [+0x68] los puntos (SaveGame::expeditions) y u32 [+0x6C] su número.
constexpr uint32_t kExpeditionBits = 0x6DF278;
constexpr uint32_t kExpeditionCount = 0x6DF2E4;
constexpr uint32_t kMerchantExpeditionBits = 0x4D8;
// u8 ([+0x89] del mundo): una expedición ya ha llegado a uno de los 100
// primeros puntos de Expeditionen.dat (0x5319D0).
constexpr uint32_t kNewWorld = global::kWorld + 0x89;

// 0x673858 (u16 x, u16 y): el sitio de cada objeto del mundo por su [+0x4]
// (< 0x7F). Los 30 primeros son los de 0x534F10 (convoys_port.cpp); después,
// lo que sigue en .rdata (los objetos 12..14 de las partidas usan 0x1E..0x2A).
constexpr uint16_t kObjectPlaces[0x7F][2] = {
	{248, 22}, {236, 29}, {217, 40}, {216, 51}, {217, 31}, {227, 61}, {249, 46}, {259, 37}, {273, 28}, {280, 27},
	{286, 23}, {294, 28}, {301, 36}, {310, 41}, {314, 49}, {305, 56}, {309, 59}, {287, 47}, {335, 54}, {323, 33},
	{358, 53}, {341, 42}, {347, 69}, {341, 61}, {343, 55}, {394, 74}, {371, 90}, {289, 60}, {261, 64}, {345, 84},
	{93, 8}, {88, 16}, {80, 19}, {73, 22}, {66, 25}, {61, 31}, {56, 36}, {45, 42}, {40, 45}, {38, 51}, {31, 53},
	{32, 64}, {26, 68}, {19, 72}, {12, 77}, {4, 84}, {1, 96}, {3, 110}, {17, 137}, {40, 144}, {4615, 2055},
	{4616, 4615}, {4614, 2054}, {3843, 777}, {2319, 3845}, {3081, 3845}, {1292, 3087}, {3845, 1804}, {7, 0}, {256, 0},
	{256, 0}, {0, 257}, {257, 1}, {1, 1}, {258, 2}, {2, 258}, {2, 3}, {259, 3}, {259, 3}, {4, 260}, {4, 4}, {260, 5},
	{261, 5}, {261, 5}, {518, 0}, {34464, 1}, {3392, 3}, {60000, 0}, {34464, 1}, {3392, 3}, {0, 0}, {0, 16256},
	{0, 16384}, {26214, 16230}, {39322, 16345}, {0, 16320}, {0, 16448}, {246, 0}, {52429, 16204}, {0, 0}, {0, 0},
	{0, 16446}, {0, 0}, {0, 16452}, {0, 0}, {0, 16457}, {0, 0}, {0, 49213}, {0, 0}, {0, 16445}, {0, 16512},
	{0, 16320}, {0, 16256}, {52429, 16204}, {39322, 16153}, {52429, 16396}, {0, 16384}, {26214, 16358},
	{13107, 16307}, {0, 16256}, {13107, 16179}, {0, 16128}, {18351, 15713}, {1154, 16117}, {21706, 15969},
	{11534, 15922}, {60293, 15889}, {52429, 16140}, {13107, 16307}, {52429, 16268}, {23069, 16260}, {31457, 16472},
	{13107, 16083}, {18351, 15585}, {38273, 16291}, {23593, 16271}, {18351, 16097},
};
// 0x673A18 (float por mercancía): precio base de lo que se compra y se vende
// en los objetos del mundo.
constexpr uint32_t kObjectWarePrice[24] = {0x3d6147af, 0x3ef50482, 0x3e6154ca, 0x3e322d0e, 0x3e11eb85, 0x3f0ccccd,
	0x3fb33333, 0x3f8ccccd, 0x3f845a1d, 0x40587ae1, 0x3ed33333, 0x3ce147af, 0x3fa39581, 0x3f8f5c29, 0x3ee147af,
	0x3e8e5604, 0x3ee147af, 0x3e6147af, 0x3f5ae147, 0x3d236e30, 0x41200000, 0x41000000, 0x41400000, 0x41600000};
// 0x672C14 (u8 por mercancía): 1 si se cuenta por barriles (200 unidades
// internas), 0 por cargas (2000).
constexpr uint8_t kBarrelWare[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
// 0x673984 (s32 por tipo de objeto): lo que admite de cada una de las dos
// mercancías que compra ([+0x14] y [+0x18] dicen lo que tiene). Desde
// 0x673990 ([3..]) es el tope de 0x52D7D0, que usa 0x52D6B0 por tipo; lo
// demás es lo que sigue en .rdata (los tipos que existen son 0..2).
constexpr int32_t kObjectLimit[15] = {0x186a0, 0x30d40, 0xea60, 0x186a0, 0x30d40, 0x0, 0x3f800000, 0x40000000,
	0x3f666666, 0x3fd9999a, 0x3fc00000, 0x40400000, 0xf6, 0x3f4ccccd, 0x0};
constexpr int16_t kReputationFound = 384;  // 0x672DD4: un objeto del mundo nuevo (0x4F8BD0).
constexpr int16_t kReputationNewWorld = 2560;  // 0x672DD2: el primero de los 100 primeros puntos (0x4F8B50).
constexpr int16_t kReputationBack = 51;  // 0x672DD0: la vuelta de la expedición (0x502B20).
constexpr uint32_t kArtilleryBase = 9;  // Byte bajo de 0x672CC8 (la primera pieza de 0x5182B0).

// A ware's name (0x6C0BB0, by ware: the 20 trade wares and the 4 weapons).
std::string ware_text(uint32_t ware) {
	if (ware < 20) {
		return letter_text(GameText(int(GameText::WareGrain) + int(ware)));
	}
	return ware < 24 ? letter_text(GameText(int(GameText::WareSword) + int(ware - 20))) : std::string();
}

// A place of the new world (0x6B1F20, by a world object's [+0x4]). Past the
// 30 names the game reads the bytes after the table as pointers (an object
// of the Indian villages; they only use this table through 0x52C6C0, whose
// contracts are only for the trading posts).
std::string place_text(uint32_t place) {
	return place < 30 ? letter_text(GameText(int(GameText::ExpeditionPlace0) + int(place))) : std::string();
}

// 0x5311D0 with the year too ((tick >> 8) / 365 into a u16).
void report_date(uint32_t tick, uint8_t& day, uint8_t& month, uint16_t& year) {
	events_mayor_detail::fn_5311d0(tick, day, month, year);
}

// Generador de números del juego (0x46C320 crea, 0x46C340 siembra, 0x46C380
// da el siguiente módulo n), como en events.cpp (0x0D). Va en la pila.
struct GameRandom {
	uint32_t seed = 0x499602D2;
	uint32_t next(uint32_t n) {
		uint32_t x = 0xA205B063u - seed * 0x44BF19B3u;
		x = (x & 1) ? (x >> 1) | 0x80000000u : x >> 1;
		seed = x;
		return x % n;
	}
};

// __ftol a 64 bits (lo que deja en edx:eax): NaN o fuera de rango da 0x8000000000000000.
int64_t ftol64(double v) {
	if (!(v > -9223372036854775808.0 && v < 9223372036854775808.0)) {
		return INT64_MIN;
	}
	return int64_t(v);
}

uint32_t object_base(uint32_t object) {
	return kWorldObjects + kWorldObjectSize * object;
}

// El punto j de Expeditionen.dat (*0x6DF2E0). Fuera de lo leído, (0, 0) (en
// el oráculo la tabla es un bloque a cero).
MapPoint expedition_point(const SaveGame& save, uint32_t j) {
	if (j < save.expeditions.size()) {
		return {save.expeditions[j][0], save.expeditions[j][1]};
	}
	return {};
}

// Redondea hacia cero una cantidad de la mercancía `ware` a barriles o cargas.
int32_t whole_units(uint32_t ware, int32_t amount) {
	return ware < 24 && kBarrelWare[ware] ? amount / 200 * 200 : amount / 2000 * 2000;
}

// El u16 [+0x14] del comerciante (reputación, 0..0xC800) sube o baja `delta`
// sin salirse (como lo hacen 0x4F8B50, 0x4F8BD0 y 0x502B20).
void add_reputation(MerchantMemory& m, int16_t delta) {
	const uint16_t r = m.u16(0x14);
	if (delta > 0) {
		m.u16(0x14) = int32_t(r) > 0xC800 - int32_t(delta) ? uint16_t(0xC800) : uint16_t(r + uint16_t(delta));
	} else {
		m.u16(0x14) = int32_t(r) > -int32_t(delta) ? uint16_t(r + uint16_t(delta)) : uint16_t(0);
	}
}

// Los bits de expediciones ([+0x0] del objeto) de 0x6DF278 si `merchant` < 0
// o los del comerciante ([+0x4D8]).
uint32_t& expedition_bits(SaveGame& save, int merchant, uint32_t offset) {
	return merchant < 0 ? save.globals.u32(kExpeditionBits + offset)
		: save.merchants[size_t(merchant)].memory.u32(kMerchantExpeditionBits + offset);
}

// 0x534990 (sobre unos bits de expediciones, ver expedition_bits): el punto
// de Expeditionen.dat más cercano a (x, y) (el primero de los que están a la
// menor distancia) se marca en [+0x0..] y, con `flag`, en [+0x30..].
// Devuelve si es uno de los 100 primeros. Después avisa a la interfaz
// (*0x6E5558, [+0x12F8] = 1), que no se traduce.
bool fn_534990(SaveGame& save, int merchant, int16_t x, int16_t y, uint8_t flag) {
	const uint32_t count = save.globals.u32(kExpeditionCount);
	uint32_t nearest = 0xFFFFFFFF;
	int32_t best = 0x7FFFFFFF;
	for (uint32_t j = 0; j < count; ++j) {
		const MapPoint e = expedition_point(save, j);
		const uint32_t dx = uint32_t(int32_t(x) - int32_t(e.x));
		const uint32_t dy = uint32_t(int32_t(y) - int32_t(e.y));
		const int32_t d = int32_t(dy * dy + dx * dx);
		if (d < best) {
			best = d;
			nearest = j;
		}
	}
	if (nearest == 0xFFFFFFFF) {
		return false;  // Sin puntos el juego escribe fuera del objeto. No puede pasar.
	}
	const uint32_t mask = 1u << (nearest & 31);
	const uint32_t word = 4 * (nearest >> 5);
	expedition_bits(save, merchant, word) |= mask;
	if (flag != 0) {
		expedition_bits(save, merchant, 0x30 + word) |= mask;
	}
	return nearest < 100;
}

// 0x4F93C0 (sobre el comerciante): marca el punto de Expeditionen.dat más
// cercano a (x, y) en 0x6DF278 (si su [+0x468] no es 0) y en sus bits
// ([+0x4D8]); devuelve lo de los suyos.
bool fn_4f93c0(SaveGame& save, int merchant, int16_t x, int16_t y, uint8_t flag) {
	if (save.merchants[size_t(merchant)].memory.u32(0x468) != 0) {
		fn_534990(save, -1, x, y, flag);
	}
	return fn_534990(save, merchant, x, y, flag);
}

// 0x534960 (sobre unos bits de expediciones): marca el objeto del mundo
// `object` en [+0x60] (y avisa a la interfaz, *0x6E5558).
void fn_534960(SaveGame& save, int merchant, uint32_t object) {
	expedition_bits(save, merchant, 0x60) |= 1u << (object & 31);
}

// 0x4F9400 (sobre el comerciante): marca el objeto del mundo en 0x6DF278 (si
// su [+0x468] no es 0) y en sus bits.
void fn_4f9400(SaveGame& save, int merchant, uint32_t object) {
	if (save.merchants[size_t(merchant)].memory.u32(0x468) != 0) {
		fn_534960(save, -1, object);
	}
	fn_534960(save, merchant, object);
}

// 0x4F8B50 (sobre el comerciante): +2560 de reputación ([+0x14]) y la
// segunda componente de la de cada ciudad ([+0x120 + 12·i]) sube [+0x464].
void fn_4f8b50(SaveGame& save, int merchant) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	add_reputation(m, kReputationNewWorld);
	for (uint32_t i = 0; i < 40; ++i) {
		m.f32(0x120 + 12 * i) = float(double(m.f32(0x120 + 12 * i)) + double(m.f32(0x464)));
	}
}

// 0x4F8BD0 (sobre el comerciante): la primera componente de la reputación en
// cada ciudad ([+0x11C + 12·i]) sube [+0x464] / 2 y la suya ([+0x14]), 384.
void fn_4f8bd0(SaveGame& save, int merchant) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const float step = float(double(m.f32(0x464)) * 0.5);  // 0x66BC58
	for (uint32_t i = 0; i < 40; ++i) {
		m.f32(0x11C + 12 * i) = float(double(step) + double(m.f32(0x11C + 12 * i)));
	}
	add_reputation(m, kReputationFound);
}

// 0x5319D0 (sobre el mundo, con el convoy): la primera expedición que llega
// a uno de los 100 primeros puntos de Expeditionen.dat ([+0x89] del mundo a
// 0): su dueño gana reputación (0x4F8B50) y [+0x89] pasa a 1. El mensaje al
// jugador y su ventana (*0x6CC7E8) no se traducen; the news 0x56 goes to the chronicle.
void fn_5319d0(SaveGame& save, int convoy) {
	GlobalMemory& g = save.globals;
	if (g.u8(kNewWorld) != 0) {
		return;
	}
	const uint8_t owner = save.convoys[size_t(convoy)].u8(0);
	// El juego no mira que sea un comerciante (lo es: el llamador lo ha visto).
	if (owner < save.merchants.size()) {
		fn_4f8b50(save, owner);
	}
	g.u8(kNewWorld) = 1;
	// The news 0x56 to the chronicle (0x531AB7..0x531B46): [+0x5] the
	// owner's home town [+0x19]; u16 [+0x8] the owner, [+0xA] his title
	// (0x4F9430), [+0xC] his rank in his home town ([+0x39C + town]).
	if (owner < save.merchants.size()) {
		const MerchantMemory& m = save.merchants[owner].memory;
		Letter letter(0x56);
		letter.entry.u8(5) = m.u8(0x19);
		letter.entry.u16(8) = owner;
		letter.entry.u16(0xA) = merchants_detail::fn_4f9430(save, owner);
		letter.entry.u16(0xC) = m.u8(0x39C + m.u8(0x19));
		send_letter(save, kLetterToChronicle, letter);
	}
}

// 0x52BE90 (sobre el objeto `object` de los 15 del mundo): ¿está el punto
// `p` de la ruta de un convoy en el sitio del objeto (kObjectPlaces por su
// [+0x4], que tiene que ser < 0x7F)? Hasta su tick [+0x8] (0xFFFFFFFF: hasta
// que alguien llega) basta con que el punto de Expeditionen.dat más cercano a
// `p` esté a menos de 7 casillas del sitio (5 sin `near`); después, `p` tiene
// que ser el sitio.
bool fn_52be90(const SaveGame& save, uint32_t object, MapPoint p, uint8_t near) {
	const GlobalMemory& g = save.globals;
	const uint32_t base = object_base(object);
	const uint8_t place = g.u8(base + 4);
	if (place >= 0x7F) {
		return false;
	}
	const uint16_t px = kObjectPlaces[place][0];
	const uint16_t py = kObjectPlaces[place][1];
	if (g.u32(base + 8) <= g.u32(kTick)) {
		return p.x == px && p.y == py;
	}
	const uint32_t count = g.u32(kExpeditionCount);
	uint32_t nearest = 0xFFFFFFFF;
	int32_t best = 0x7FFFFFFF;
	for (uint32_t j = 0; j < count; ++j) {
		const MapPoint e = expedition_point(save, j);
		const uint32_t dx = uint32_t(p.x) - uint32_t(e.x);
		const uint32_t dy = uint32_t(p.y) - uint32_t(e.y);
		const int32_t d = int32_t(dy * dy + dx * dx);
		if (d < best) {
			best = d;
			nearest = j;
		}
	}
	if (nearest == 0xFFFFFFFF) {
		return false;  // Sin puntos el juego lee el anterior a la tabla. No puede pasar.
	}
	const MapPoint e = expedition_point(save, nearest);
	const uint32_t dx = uint32_t(e.x) - uint32_t(px);
	const uint32_t dy = uint32_t(e.y) - uint32_t(py);
	const uint32_t d = dy * dy + dx * dx;
	const uint32_t radius = near != 0 ? 7 : 5;
	return d < radius * radius;
}

// 0x52D0B0 (sobre el objeto del mundo, con el convoy): si nadie lo ha
// encontrado aún ([+0x6] no es un comerciante) y el convoy tiene un guía con
// capitán, el dueño del convoy lo encuentra: [+0x6] = dueño, [+0x8] = tick, y
// gana reputación (0x4F8BD0); the news 0x1C to each merchant and the chronicle. Los
// avisos (0x548CA0 y la ventana *0x6CC7E8 de un jugador humano) no se traducen. Devuelve si lo ha encontrado.
bool fn_52d0b0(SaveGame& save, uint32_t object, uint16_t convoy) {
	GlobalMemory& g = save.globals;
	const uint32_t base = object_base(object);
	if (g.u8(base + 4) >= 0x7F || convoy >= save.convoy_count() ||
		uint16_t(g.u8(base + 6)) < merchant_count(save)) {
		return false;
	}
	const GameMemory<0x3C>& c = save.convoys[convoy];
	const uint16_t leader = c.u16(0x10);
	if (leader >= ship_count(save) || save.ships[leader].captain() >= g.u16(kCaptainCount)) {
		return false;
	}
	const uint8_t owner = c.u8(0);
	g.u8(base + 6) = owner;
	g.u32(base + 8) = g.u32(kTick);
	// El juego no mira que sea un comerciante (0x5303C0 sin límite).
	if (owner < save.merchants.size()) {
		fn_4f8bd0(save, owner);
	}
	// The news 0x1C, to each merchant and to the chronicle
	// (0x52D18A..0x52D1F8): [+0x5] the guide's [+0x13A] % the towns; [+0x8]
	// the owner, [+0x9] the object's [+0x4], [+0xA] and [+0xB] the captain's
	// names [+0x2] and [+0x3], [+0xC..+0xF] the object's [+0x0..+0x3].
	const CaptainMemory& cap = save.captains[save.ships[leader].captain()];
	Letter letter(0x1C);
	letter.entry.u8(5) = uint8_t(save.ships[leader].memory.u8(0x13A) % town_count(save));
	letter.entry.u8(8) = owner;
	letter.entry.u8(9) = g.u8(base + 4);
	letter.entry.u8(0xA) = cap.u8(2);
	letter.entry.u8(0xB) = cap.u8(3);
	for (uint32_t k = 0; k < 4; ++k) {
		letter.entry.u8(0xC + k) = g.u8(base + k);
	}
	send_letter(save, kLetterToEach, letter);
	send_letter(save, kLetterToChronicle, letter);
	return true;
}

// 0x517FE0 (sobre el barco): suma a su carga `amount` de la mercancía `ware`,
// redondeada a barriles (200 unidades internas; las armas, 0x14.., a 10) o
// cargas (2000), y su precio medio pasa a (lo que costaba lo que había +
// `price`) / la carga nueva. Avisa a la interfaz (0x517770 con 4).
void fn_517fe0(SaveGame& save, int ship, uint32_t ware, int32_t amount, int32_t price) {
	if (amount <= 0) {
		return;
	}
	ship_ai_detail::fn_517770(save, ship, 4);
	SavedShip& s = save.ships[size_t(ship)];
	const int32_t old = s.cargo(int(ware));
	int32_t cargo = int32_t(uint32_t(old) + uint32_t(amount));
	const double total = double(price) + double(s.average_price(int(ware))) * double(old);
	if (ware < 24 && kBarrelWare[ware]) {
		cargo = ware < 0x14 ? cargo / 200 * 200 : cargo / 10 * 10;
	} else {
		cargo = cargo / 2000 * 2000;
	}
	s.cargo(int(ware)) = cargo;
	s.average_price(int(ware)) = float(total / double(cargo));
}

// Reparte entre los barcos del convoy con casco `amount` de la mercancía
// `ware` que han costado `cost` en total, por orden y hasta llenar cada uno
// (lo que cabe redondeado a barriles o cargas), con la parte del coste
// redondeada hacia arriba (como 0x52BFE0 y 0x52C6C0). `free` (si no es
// nulo) baja lo repartido.
void share_out(SaveGame& save, const GameMemory<0x3C>& c, uint32_t ware, int32_t amount, int32_t cost, int32_t* free) {
	for (uint32_t s = c.u16(0xA); s < ship_count(save) && amount > 0;) {
		SavedShip& ship = save.ships[s];
		const uint16_t next = ship.next_in_list();
		if (ship.health() > 0) {
			const int32_t room = whole_units(ware, recalculate_cargo(ship));
			if (room >= amount) {
				fn_517fe0(save, int(s), ware, amount, cost);
				if (free) {
					*free = int32_t(uint32_t(*free) - uint32_t(amount));
				}
				amount = 0;
			} else if (room > 0) {
				const int64_t part = (int64_t(room) * int64_t(cost) + int64_t(amount - 1)) / int64_t(amount);
				fn_517fe0(save, int(s), ware, room, int32_t(part));
				cost = int32_t(uint32_t(cost) - uint32_t(part));
				if (free) {
					*free = int32_t(uint32_t(*free) - uint32_t(room));
				}
				amount -= room;
			}
		}
		s = next;
	}
}

// El precio de 0x52D4F0 y 0x52D6B0 (en 32.32, del que se queda la parte
// entera): entre a y b veces el precio de la mercancía (los dos en 32.32,
// redondeados a float) según `level` sobre `limit`, por `amount`.
int32_t object_price(uint32_t ware, int64_t a, int64_t b, int32_t limit, int32_t level, int32_t amount) {
	const double price = double(as_float(kObjectWarePrice[ware]));
	const int64_t pa = ftol64(double(float(double(a))) * price);
	const int64_t pb = ftol64(double(float(double(b))) * price);
	if (limit == 0) {
		return 0;  // El juego divide por cero (tipos que no existen).
	}
	const int64_t slope = int64_t(uint64_t(pa) - uint64_t(pb)) / int64_t(limit);
	const int64_t unit = int64_t(uint64_t(pa) - uint64_t(slope) * uint64_t(int64_t(level)));
	return int32_t(uint32_t((uint64_t(unit) * uint64_t(int64_t(amount))) >> 32));
}

// 0x52D4F0 (sobre el objeto del mundo): el objeto compra de *`cargo` de su
// mercancía [+0x2 + side] lo que admite (kObjectLimit por su tipo menos lo
// que tiene, [+0x14 + 4·side], que no cambia); *`cargo` se queda con lo que
// sobra, redondeado a barriles o cargas. Devuelve lo que paga: entre 1,5 y
// 1,2 veces el precio de la mercancía (1,6 y 1,3 con `flag`; 2 y 1,5 con el
// tipo 2) según lo que tiene más la mitad de lo que compra.
int32_t fn_52d4f0(SaveGame& save, uint32_t object, uint32_t side, int32_t& cargo, uint8_t flag) {
	const GlobalMemory& g = save.globals;
	const uint32_t base = object_base(object);
	side &= 1;
	const uint8_t type = g.u8(base + 5);
	if (type >= 15) {
		return 0;  // Fuera de la tabla. Los tipos que existen son 0..2.
	}
	const int32_t have = g.s32(base + 0x14 + 4 * side);
	int32_t room = int32_t(uint32_t(kObjectLimit[type]) - uint32_t(have));
	const uint8_t ware = g.u8(base + 2 + side);
	if (room > cargo) {
		room = cargo;
		cargo = 0;
	} else {
		cargo = whole_units(ware, int32_t(uint32_t(cargo) - uint32_t(room)));
	}
	int64_t a = 1;
	int64_t b = 1;
	if (type > 1) {
		if (type == 2) {
			a = 0x2'00000000;
			b = 0x1'80000000;
		}
	} else if (flag == 0) {
		a = 0x1'80000000;
		b = 0x1'33333333;
	} else {
		a = 0x1'99999999;
		b = 0x1'4CCCCCCC;
	}
	if (ware >= 24) {
		return 0;  // Fuera de la tabla de precios. No sale (los objetos con mercancía venden la suya).
	}
	return object_price(ware, a, b, kObjectLimit[type], int32_t(uint32_t(room / 2) + uint32_t(have)), room);
}

// 0x52D6B0 (sobre el objeto del mundo): lo que cuesta comprarle `amount` de
// su mercancía [+side]: entre 1,3 y 1 vez su precio (1,2 y 0,9 con `flag`)
// según lo que le queda en [+0xC + 4·side] menos la mitad de lo que se compra.
int32_t fn_52d6b0(const SaveGame& save, uint32_t object, uint32_t side, int32_t amount, uint8_t flag) {
	const GlobalMemory& g = save.globals;
	const uint32_t base = object_base(object);
	const uint8_t type = g.u8(base + 5);
	int64_t a = 1;
	int64_t b = 1;
	if (type > 1) {
		if (type == 2) {
			a = 0;
			b = 0;
		}
	} else if (flag == 0) {
		a = 0x1'4CCCCCCC;
		b = 0x1'00000000;
	} else {
		a = 0x1'33333333;
		b = 0x0'E6666666;
	}
	const uint8_t ware = g.u8(base + side);
	if (ware >= 24 || type + 3 >= 15) {
		return 0;  // Fuera de las tablas. No sale (el llamador mira la mercancía).
	}
	const int32_t level = int32_t(uint32_t(g.s32(base + 0xC + 4 * side)) - uint32_t(amount / 2));
	return object_price(ware, a, b, kObjectLimit[type + 3], level, amount);
}

// 0x52C6C0 (sobre el objeto del mundo, con el convoy): los encargos del
// objeto (SaveGame::world_object_lists, uno por comerciante). Sin encargo
// del dueño del convoy, con `mode` ([+0x38] del convoy) 2, un objeto de los
// tipos 0 y 1 y `may_create`, crea uno: cantidad (en barriles) según los
// habitantes de todas las ciudades, que caduca en 180 días; el objeto
// compra ([+0x3] distinto de 0) su mercancía [+0x2 + lado] o vende la suya
// [+lado], a un precio fijo. Con encargo: si compra y `deliver`, los barcos
// le entregan lo que llevan de esa mercancía; si vende y sin `deliver`, los
// barcos le compran lo que les cabe y pueden pagar. El encargo cumplido se
// quita. What was traded and the new contract go into `text` (the game's
// CString, for the expedition's report; nothing with nullptr). Devuelve si
// lo ha creado.
uint8_t fn_52c6c0(SaveGame& save, uint32_t object, uint16_t convoy, uint16_t mode, uint8_t may_create,
	uint8_t deliver, std::string* text) {
	GlobalMemory& g = save.globals;
	const uint32_t base = object_base(object);
	if (g.u8(base + 4) >= 0x7F) {
		return 0;
	}
	GameMemory<0x3C>& c = save.convoys[convoy];
	const uint8_t owner = c.u8(0);
	MerchantMemory& m = save.merchants[owner < merchant_count(save) ? owner : 0].memory;
	std::vector<GameMemory<0x10>>& list = save.world_object_lists[object];
	size_t found = list.size();
	for (size_t i = 0; i < list.size(); ++i) {
		if (list[i].u16(0) == uint16_t(owner)) {
			found = i;
			break;
		}
	}
	uint8_t created = 0;
	if (found == list.size()) {
		if (mode == 2 && g.u8(base + 5) < 2 && may_create != 0) {
			created = 1;
			GameRandom r;
			r.seed = g.u32(kTick);
			GameMemory<0x10> order;
			order.u16(0x0) = owner;
			order.u8(0x2) = uint8_t(r.next(2));
			int64_t people = 0;
			for (uint32_t t = 0; t < town_count(save); ++t) {
				people += save.towns[t].memory.s32(0x2D4);
			}
			const uint32_t k = r.next(0x8000) + 0x8000;
			const int64_t amount = int64_t(uint64_t(k) * uint64_t(people)) / 50 >> 16;
			order.u32(0x4) = uint32_t(amount) + 500;
			order.u32(0xC) = g.u32(kTick) + 0xB400;
			const uint8_t buys = uint8_t(r.next(2));
			order.u8(0x3) = buys;
			uint32_t base_price;
			uint8_t ware;
			if (buys != 0) {
				base_price = r.next(0x4E20) + 0x27100;
				ware = g.u8(base + 2 + order.u8(0x2));
			} else {
				base_price = r.next(0x7530) + 0x13880;
				ware = g.u8(base + order.u8(0x2));
			}
			const float f = float(double(int64_t(base_price)));
			const double unit = ware < 24 ? double(as_float(kObjectWarePrice[ware])) : 0.0;  // Siempre < 24.
			const int64_t price = ftol64(double(f) * unit * double(as_float(0x43480000)));  // 0x66A91C: 200
			order.u32(0x8) = uint32_t(price / 100000);
			list.push_back(order);
			++g.u32(base + 0x28);
			++g.u16(base + 0x1C);
			if (text) {
				// The new contract (0x52CF48..0x52D03A): «A trader in %s has
				// promised to %s %d\%c %s by %d.%d.%d at a price of %d\C.»
				// with the place, buy or sell, the barrels, the ware, the
				// deadline and the price. The Spanish build passes them in
				// another order (0x52D487: the place, the date, the barrels,
				// the ware, the price, «unidad comprada» or «vendida»).
				uint8_t day = 0;
				uint8_t month = 0;
				uint16_t year = 0;
				report_date(order.u32(0xC), day, month, year);
				const std::string place = place_text(g.u8(base + 4));
				const std::string what = letter_text(buys != 0 ? GameText::ContractBuy : GameText::ContractSell);
				const std::string amount_text = std::to_string(int32_t(order.u32(0x4)));
				const std::string price_text = std::to_string(int32_t(order.u32(0x8)));
				const std::string d = std::to_string(day), mo = std::to_string(month), y = std::to_string(year);
				const GameTexts* texts = letter_texts();
				const std::vector<std::string> args = texts && texts->spanish()
					? std::vector<std::string>{place, d, mo, y, amount_text, "B", ware_text(ware), price_text, what}
					: std::vector<std::string>{place, what, amount_text, "B", ware_text(ware), d, mo, y, price_text};
				*text += GameTexts::sprintf_raw(letter_text(GameText::ContractPromised), args);
			}
		}
		return created;
	}
	GameMemory<0x10>& order = list[found];
	const uint32_t side = order.u8(0x2);
	const bool buys = order.u8(0x3) != 0;
	bool done = false;
	uint32_t delivered[2] = {0, 0};  // [esp+0x40]: what the ships gave it, by side
	int32_t taken[2] = {0, 0};  // [esp+0x50]: what they took from it
	if (buys && deliver != 0) {
		const uint8_t ware = g.u8(base + 2 + side);
		for (uint32_t s = c.u16(0xA); s < ship_count(save) && !done;) {
			SavedShip& ship = save.ships[s];
			const uint16_t next = ship.next_in_list();
			if (ship.health() > 0) {
				int32_t& cargo = ship.cargo(ware);
				if (cargo > 0) {
					uint32_t q = uint32_t(cargo);
					const uint32_t limit = order.u32(0x4) * 200;
					if (q > limit) {
						q = limit;
					}
					const uint32_t units = q / 200;
					const uint32_t money = order.u32(0x8) * units;
					c.u32(0x20) += money;
					m.u32(0x4B0) += money;
					cargo = int32_t(uint32_t(cargo) - q);
					delivered[side & 1] += q;
					order.u32(0x4) -= units;
					if (order.u32(0x4) == 0) {
						done = true;
					}
				} else {
					cargo = 0;
				}
			}
			recalculate_cargo(ship);
			s = next;
		}
	} else if (!buys && deliver == 0) {
		int32_t free = 0;
		for (uint32_t s = c.u16(0xA); s < ship_count(save);) {
			SavedShip& ship = save.ships[s];
			const uint16_t next = ship.next_in_list();
			if (ship.health() > 0) {
				free = int32_t(uint32_t(free) + uint32_t(recalculate_cargo(ship) / 200 * 200));
			}
			s = next;
		}
		const uint8_t ware = g.u8(base + side);
		int32_t want = free;
		const uint32_t limit = order.u32(0x4) * 200;
		if (uint32_t(free) > limit) {
			want = int32_t(limit);
		}
		const bool barrel = ware < 24 && kBarrelWare[ware];
		int32_t amount;
		int32_t cost;
		for (;;) {
			amount = barrel ? want / 200 * 200 : want / 2000 * 2000;
			const int32_t units = barrel ? amount / 200 : amount / 2000;
			cost = int32_t(uint32_t(units) * order.u32(0x8));
			if (cost == 0 || cost <= c.s32(0x20)) {
				break;
			}
			want = int32_t((int64_t(c.s32(0x20)) * int64_t(amount)) / int64_t(cost));
		}
		c.s32(0x20) = int32_t(uint32_t(c.s32(0x20)) - uint32_t(cost));
		m.u32(0x4B4) += uint32_t(cost);
		order.u32(0x4) += uint32_t(-(amount / 200));
		if (order.u32(0x4) == 0) {
			done = true;
		}
		taken[side & 1] += amount;
		share_out(save, c, ware, amount, cost, nullptr);
	}
	if (text && (delivered[0] || delivered[1] || taken[0] || taken[1])) {
		// The contract's trade (0x52CDA9..0x52CF1B): «%$1$d\%$2$c %$3$s was
		// %$6$s at a price of %$4$d\C in %$5$s.» with the barrels (the amount
		// / 200 whatever the ware), the ware, the price, the place and «sold»
		// (the object buys) or «bought», then what is left to deliver by the
		// deadline or «The quantity required by the contract was therefore
		// met.».
		const int32_t amount = buys ? int32_t(delivered[side & 1]) : taken[side & 1];
		const uint8_t ware = buys ? g.u8(base + 2 + side) : g.u8(base + side);
		uint8_t day = 0;
		uint8_t month = 0;
		uint16_t year = 0;
		report_date(order.u32(0xC), day, month, year);
		std::string line = GameTexts::sprintf_raw(letter_text(GameText::ContractTraded),
			{std::to_string(amount / 200), "B", ware_text(ware), std::to_string(int32_t(order.u32(0x8))),
				place_text(g.u8(base + 4)), letter_text(buys ? GameText::ContractSold : GameText::ContractBought)});
		if (order.u32(0x4) != 0) {
			line += GameTexts::sprintf_raw(letter_text(GameText::ContractMustDeliver),
				{std::to_string(int32_t(order.u32(0x4))), "B", std::to_string(day), std::to_string(month),
					std::to_string(year)});
		} else {
			line += letter_text(GameText::ContractMet);
		}
		*text += line;
	}
	if (done) {
		list.erase(list.begin() + std::ptrdiff_t(found));
		--g.u32(base + 0x28);
		--g.u16(base + 0x1C);
	}
	return 0;
}

// 0x52BFE0 (sobre el objeto del mundo, con el convoy, el punto `point` de su
// ruta, `mode` ([+0x38] del convoy) y `first`): el comercio del convoy en el
// objeto. Primero los encargos que compra (0x52C6C0); después los barcos con
// casco le venden todo lo que llevan de sus dos mercancías [+0x2], [+0x3]
// (0x52D4F0) y se miran los encargos que vende o se crea uno (0x52C6C0 con
// `first`). Al final compran con lo que les cabe sus mercancías [+0x0] y
// [+0x1], según los bits del punto ([+0x34 + punto] del convoy: el 1 para
// [+0x0], la mitad del sitio si tiene también el 2; el 2 para [+0x1]; y otra
// vez [+0x0]), hasta lo que tiene el objeto ([+0xC], [+0x10], que bajan) y
// el dinero del convoy ([+0x20]). Lo vendido suma al [+0x20] del convoy y al
// [+0x4B0] del dueño; lo comprado resta del [+0x20] y suma al [+0x4B4]. For
// the expedition's report, what was sold and bought goes into `trades` and
// the contracts into `contracts` (the game's CStrings; nothing with
// nullptr). Devuelve si 0x52C6C0 ha creado un encargo.
uint8_t fn_52bfe0(SaveGame& save, uint32_t object, uint16_t convoy, uint32_t point, uint16_t mode, uint8_t first,
	std::string* trades, std::string* contracts) {
	GlobalMemory& g = save.globals;
	const uint32_t base = object_base(object);
	if (g.u8(base + 4) >= 0x7F) {
		return 0;
	}
	GameMemory<0x3C>& c = save.convoys[convoy];
	const uint8_t owner = c.u8(0);
	MerchantMemory& m = save.merchants[owner < merchant_count(save) ? owner : 0].memory;
	// What the ships sold and bought, by side ([esp+0x38], [esp+0x40],
	// [esp+0x48], [esp+0x50]: the amounts and the money).
	int32_t sold[2] = {0, 0};
	int32_t earned[2] = {0, 0};
	int32_t bought[2] = {0, 0};
	int32_t spent[2] = {0, 0};
	uint32_t last_ware = 0;  // [esp+0x1C]: the ware of the last pass that got past its bit
	fn_52c6c0(save, object, convoy, mode, 0, 1, contracts);
	const uint8_t flag = mode == 1 ? 1 : 0;
	for (uint32_t s = c.u16(0xA); s < ship_count(save);) {
		SavedShip& ship = save.ships[s];
		const uint16_t next = ship.next_in_list();
		if (ship.health() > 0) {
			for (uint32_t side = 0; side < 2; ++side) {
				const uint8_t ware = g.u8(base + 2 + side);
				if (ware >= 24) {
					continue;  // El juego leería fuera de la carga. No sale (todos tienen mercancía).
				}
				int32_t& cargo = ship.cargo(ware);
				if (cargo > 0) {
					const int32_t before = cargo;
					const int32_t money = fn_52d4f0(save, object, side, cargo, flag);
					c.u32(0x20) += uint32_t(money);
					m.u32(0x4B0) += uint32_t(money);
					sold[side] = int32_t(uint32_t(sold[side]) + uint32_t(before) - uint32_t(cargo));
					earned[side] = int32_t(uint32_t(earned[side]) + uint32_t(money));
				} else {
					cargo = 0;
				}
			}
			recalculate_cargo(ship);
		}
		s = next;
	}
	const uint8_t created = fn_52c6c0(save, object, convoy, mode, first, 0, contracts);
	int32_t free = 0;
	for (uint32_t s = c.u16(0xA); s < ship_count(save);) {
		SavedShip& ship = save.ships[s];
		const uint16_t next = ship.next_in_list();
		if (ship.health() > 0) {
			free = int32_t(uint32_t(free) + uint32_t(recalculate_cargo(ship) / 200 * 200));
		}
		s = next;
	}
	for (uint32_t pass = 0; pass < 3; ++pass) {
		if (free < 200) {
			break;
		}
		const uint32_t side = pass & 1;
		const uint8_t bits = c.u8(0x34 + (point & 0xFFFF));
		if (!(bits & (1u << side))) {
			continue;
		}
		const uint8_t ware = g.u8(base + side);
		last_ware = ware;
		if (ware >= 0x18) {
			continue;
		}
		int32_t want = pass == 0 && (bits & 2) ? free / 2 : free;
		const int32_t stock = g.s32(base + 0xC + 4 * side);
		if (stock < want) {
			want = stock;
		}
		int32_t amount;
		int32_t cost;
		for (;;) {
			amount = whole_units(ware, want);
			cost = fn_52d6b0(save, object, side, amount, flag);
			if (cost == 0 || cost <= c.s32(0x20)) {
				break;
			}
			want = int32_t((int64_t(c.s32(0x20)) * int64_t(amount)) / int64_t(cost));
		}
		c.s32(0x20) = int32_t(uint32_t(c.s32(0x20)) - uint32_t(cost));
		m.u32(0x4B4) += uint32_t(cost);
		g.s32(base + 0xC + 4 * side) = int32_t(uint32_t(g.s32(base + 0xC + 4 * side)) - uint32_t(amount));
		bought[side] = int32_t(uint32_t(bought[side]) + uint32_t(amount));
		spent[side] = int32_t(uint32_t(spent[side]) + uint32_t(cost));
		share_out(save, c, ware, amount, cost, &free);
	}
	for (uint32_t s = c.u16(0xA); s < ship_count(save);) {
		SavedShip& ship = save.ships[s];
		const uint16_t next = ship.next_in_list();
		if (ship.health() > 0) {
			recalculate_cargo(ship);
		}
		s = next;
	}
	if (trades && (sold[0] || sold[1] || bought[0] || bought[1])) {
		// The trades (0x52C4D2..0x52C687): «\nIn <place>» and «the following
		// transaction was made:» (took place: odd points), or «\nWith the
		// Indian tribe» for a village (its [+0x5] from 2 on); then a line
		// «%u\%c %s sold at %u\C (total: %u\C)» for each ware sold and
		// «... bought at ...» for each bought: the barrels (\B) or loads
		// (\L), the ware, the rounded price of one and the total.
		std::string& t = *trades;
		if (g.u8(base + 5) < 2) {
			t += letter_text(GameText::ExpeditionIn);
			t += place_text(g.u8(base + 4));
			t += letter_text((point & 1) ? GameText::ExpeditionTransactionTook : GameText::ExpeditionTransactionMade);
		} else {
			t += letter_text(GameText::ExpeditionIndianTribe);
			t += letter_text(GameText::ExpeditionTransactionMade);
		}
		auto line = [&](GameText id, int32_t amount, int32_t money, bool barrel, uint32_t ware) {
			const int32_t units = barrel ? amount / 200 : amount / 2000;
			const int32_t price = ((units >> 1) + money) / units;
			t += GameTexts::sprintf_raw(letter_text(id), {std::to_string(uint32_t(units)), barrel ? "B" : "L",
				ware_text(ware), std::to_string(uint32_t(price)), std::to_string(uint32_t(money))});
		};
		for (uint32_t side = 0; side < 2; ++side) {
			if (sold[side] > 0) {
				const uint8_t ware = g.u8(base + 2 + side);
				const bool barrel = kBarrelWare[ware];
				if ((barrel ? sold[side] / 200 : sold[side] / 2000) == 0) {
					// Not reproduced: a game bug, less than a barrel or a load sold and the game
					// divides by 0 (idiv at 0x52C5B6: it would crash).
					continue;
				}
				line(GameText::ExpeditionSold, sold[side], earned[side], barrel, ware);
			}
		}
		for (uint32_t side = 0; side < 2; ++side) {
			// A game bug: barrels or loads by the last ware looked at, not
			// by this side's (the same when both are of a kind). Fixed: by
			// this side's ware.
			const uint32_t kind = fix_game_bugs() ? g.u8(base + side) : last_ware;
			const bool barrel = kind < 24 && kBarrelWare[kind];
			if (bought[side] > 0 && (barrel ? bought[side] / 200 : bought[side] / 2000) > 0) {
				line(GameText::ExpeditionBought, bought[side], spent[side], barrel, g.u8(base + side));
			}
		}
	}
	return created;
}

// 0x4E6A54 (evento 0x12, con el evento): el convoy [+0x8] (*0x6DD7A8) va
// por su ruta ([+0x24..+0x33] del convoy: hasta 4 puntos, x < 0x7FFF) a
// puntos fuera del mapa. Con [+0xA] = 0xFFFF acaba de salir: el evento se
// retrasa lo que tarda (la distancia de ida y vuelta · 128 / [+0xC] del
// convoy, la velocidad, y la mitad más con [+0x38] a 0) y pasa a [+0xA] = 4.
// Si no, llega ([+0xC] del evento: el tick en que salió): en cada punto de
// la ruta el dueño apunta el punto de Expeditionen.dat más cercano (0x4F93C0;
// con uno de los 100 primeros, 0x5319D0) y, en el primero de los 15 objetos
// del mundo que esté ahí (0x52BE90), apunta el objeto (0x4F9400), lo
// encuentra si es el primero (0x52D0B0) y comercia (0x52BFE0). Después el
// casco de cada barco baja 640 por día de viaje (dos tercios de los días con
// [+0x38] a 0) y, con el generador del juego sembrado con el tick, 1 de cada
// 11 veces piratas (daño según la artillería [+0x120] de los barcos y quizá
// se llevan dinero del convoy) y otra 1 de cada 11 una tormenta. Al final el
// convoy vuelve al mapa (0x502B20) y el evento se quita. A human owner gets
// the report by letter (0x63), with what was found, traded and signed.
uint32_t fn_4e6a54(SaveGame& save, uint16_t event) {
	Event& e = save.events[event];
	const uint32_t convoy = e.u16(8);
	if (convoy >= save.convoy_count()) {
		return 0;  // Checked with the oracle: scenario expedition_branches (saves 7, tut0, tut1, tut3, tut4).
	}
	GameMemory<0x3C>& c = save.convoys[convoy];
	if (e.u16(0xA) == 0xFFFF) {
		int32_t distance = fn_52d950(convoy_point(c, 0));
		uint32_t k = 1;
		for (; k < 4 && convoy_point(c, k).x < 0x7FFF; ++k) {
			distance = int32_t(uint32_t(distance) + uint32_t(fn_52db50(convoy_point(c, k - 1), convoy_point(c, k))));
		}
		distance = int32_t(uint32_t(distance) + uint32_t(fn_52d950(convoy_point(c, k - 1))));
		const uint32_t time = (uint32_t(distance) << 7) / c.u32(0xC);
		e.u32(0) += time;
		if (c.u16(0x38) == 0) {
			e.u32(0) += time >> 1;
		}
		e.u16(0xA) = 4;
		return 1;
	}
	const int merchant = c.u8(0) < merchant_count(save) ? int(c.u8(0)) : -1;
	const uint8_t near = c.u16(0x38) == 0 ? 1 : 0;
	uint8_t first = 1;  // Byte bajo de [esp+0x3C]: hasta que se crea un encargo.
	// For the report (the letter 0x63): the convoy's money before trading
	// ([esp+0x44]), the places found ([esp+0x48..+0x4B]: the [+0x4] of the
	// trading posts, 0xFF a village, by the order they were found), how many
	// were found and how many points found nothing, whether a contract was
	// signed, and the three CStrings: the trades, the contracts and the
	// voyage's mishap.
	const uint32_t start_money = c.u32(0x20);
	uint8_t places[4] = {0xFF, 0xFF, 0xFF, 0xFF};
	uint8_t found = 0;
	uint8_t empty = 0;
	bool signed_contract = false;
	std::string trades;
	std::string contracts = letter_text(GameText::ContractsHeader);
	for (uint32_t k = 0; k < 4; ++k) {
		const MapPoint p = convoy_point(c, k);
		if (p.x >= 0x7FFF) {
			continue;
		}
		if (merchant >= 0 && fn_4f93c0(save, merchant, int16_t(p.x), int16_t(p.y), near)) {
			fn_5319d0(save, int(convoy));
		}
		bool here = false;
		for (uint32_t i = 0; i < kWorldObjectCount; ++i) {
			if (!fn_52be90(save, i, p, near)) {
				continue;
			}
			here = true;
			if (merchant >= 0) {
				fn_4f9400(save, merchant, i);
			}
			if (fn_52d0b0(save, i, uint16_t(convoy))) {
				if (save.globals.u8(object_base(i) + 5) < 2) {
					places[found & 3] = save.globals.u8(object_base(i) + 4);
				}
				++found;
			}
			if (fn_52bfe0(save, i, uint16_t(convoy), k, c.u16(0x38), first, &trades, &contracts)) {
				first = 0;
				signed_contract = true;
			}
			break;
		}
		if (!here) {
			++empty;
		}
	}
	if (!signed_contract) {
		contracts += letter_text(GameText::ContractNoNew);
	}
	bool storm = false;  // [esp+0x12]
	bool pirates = false;  // [esp+0x13]
	uint32_t loss = 0;  // [esp+0x14]: what the pirates took
	// El desgaste del viaje: 640 de casco por día.
	uint32_t elapsed = e.u32(0) - e.u32(0xC);
	if (near) {
		elapsed = elapsed * 100 / 150;
	}
	const uint32_t wear = (elapsed >> 8) * 640;
	uint32_t ships = 0;
	for (uint32_t s = c.u16(0xA); s < ship_count(save); s = save.ships[s].next_in_list()) {
		int32_t& hull = save.ships[s].health();
		hull = int32_t(uint32_t(hull) - wear);
		if (hull < 0) {
			hull = 0;
		}
		++ships;
	}
	GameRandom r;
	r.seed = save.globals.u32(kTick);
	const uint32_t roll = r.next(11);
	if (roll > 9) {
		// Piratas: cada barco pierde del 30 % al 60 % de su casco máximo menos
		// lo que defiende la artillería media del convoy ([+0x120] de cada barco).
		uint32_t firepower = 0;
		for (uint32_t s = c.u16(0xA); s < ship_count(save); s = save.ships[s].next_in_list()) {
			firepower += save.ships[s].memory.u32(0x120);
		}
		if ((ships & 0xFFFF) == 0) {
			return 0;  // El juego divide por cero (un convoy sin barcos). No puede pasar.
		}
		const uint32_t average = firepower / (ships & 0xFFFF);
		const uint64_t power = uint64_t(average - kArtilleryBase) * 0x3456700u;
		const int64_t q = int64_t(power) / 5;
		const uint32_t defence = uint32_t(ftol64(std::sqrt(double(q)))) + 0xAF0;
		pirates = true;
		for (uint32_t s = c.u16(0xA); s < ship_count(save); s = save.ships[s].next_in_list()) {
			SavedShip& ship = save.ships[s];
			const uint32_t k = r.next(0x4CCC) + 0x4CCC;
			const int64_t hit = (int64_t(ship.max_health()) * int64_t(k)) >> 16;
			const int64_t damage = hit - int64_t(defence);
			const uint32_t d = damage < 0 ? 0 : uint32_t(damage);
			int32_t& hull = ship.health();
			hull = int32_t(uint32_t(hull) - d);
			if (hull < 0) {
				hull = 0;
			}
		}
		// Y a veces se llevan dinero del convoy (con poca artillería, más a menudo).
		const uint32_t roll2 = r.next(11);
		if ((roll2 > 9 || (roll2 > 5 && average < 12)) && c.s32(0x20) > 5000) {
			const int32_t v = int32_t(c.u32(0x20) * 2500u);
			const uint32_t root = uint32_t(ftol64(std::sqrt(double(v))));
			loss = root * (r.next(0x1E) + 0x14) / 50;
		}
		if (loss > c.u32(0x20)) {
			loss = c.u32(0x20);
		}
		c.u32(0x20) -= loss;
	} else if (roll > 8) {
		// Tormenta: del 20 % al 40 % del casco máximo (del 10 % al 20 % los tipos 2 y 3).
		for (uint32_t s = c.u16(0xA); s < ship_count(save); s = save.ships[s].next_in_list()) {
			SavedShip& ship = save.ships[s];
			const uint32_t k = ship.type() == 0 || ship.type() == 1 ? r.next(0x3333) + 0x3333 : r.next(0x1999) + 0x1999;
			const int64_t hit = (int64_t(ship.max_health()) * int64_t(k)) >> 16;
			int32_t& hull = ship.health();
			hull = int32_t(uint32_t(hull) - uint32_t(hit));
			if (hull < 0) {
				hull = 0;
			}
		}
		storm = true;
	}
	// Los que quedan vuelven (con casco o sin ninguno, que se hunden).
	Sailing* sailing = event_sailing();
	bool alive = false;
	for (uint32_t s = c.u16(0xA); s < ship_count(save) && !alive; s = save.ships[s].next_in_list()) {
		alive = save.ships[s].health() > 0;
	}
	if (!alive) {
		if (sailing) {
			sailing->fn_502b20(int(convoy), e.u32(0xC));
		}
		return 0;
	}
	// The report, the letter 0x63 to a human owner (0x4E70E0..0x4E79A3): its
	// buffer (4000 bytes in the game) is the text; [+0xC] the bytes the game
	// counts. «\n\lThe convoy led by the <guide's name> has successfully
	// returned from an expedition and is now heading for <town>.».
	std::string text;
	uint32_t size = 0;
	auto add = [&](const std::string& s) {
		text += s;
		size += uint32_t(s.size());
	};
	add(letter_text(GameText::ExpeditionLedBy));
	const uint16_t guide = c.u16(0x10);
	if (guide < ship_count(save)) {
		std::string name;
		const ShipMemory& sm = save.ships[guide].memory;
		for (size_t i = 0x160; i < 0x180 && sm.u8(i) != 0; ++i) {
			name += char(sm.u8(i));
		}
		add(name);
	} else {
		// 0x512B20 with 0xFFFF: the text of [0x6C0CB4]. Checked with the oracle: scenario
		// expedition_branches (saves 7, tut0, tut1, tut3, tut4); in a game a convoy always has a
		// guide when it arrives.
		add(letter_text(GameText::ShipUnnamed));
	}
	add(letter_text(GameText::ExpeditionReturned));
	// A game bug: the town names' table 0x6DDA00 goes by town number and the
	// game takes it by the town's index (as when saving, savegame_write.cpp):
	// the report names another town when they differ. Fixed: by the town's
	// number (0x6DE4B8).
	const uint32_t town = c.u8(0x1F) < town_count(save) ? c.u8(0x1F) : c.u8(0x1E) % town_count(save);
	add(letter_town_name(save, town));
	add(letter_text(GameText::ExpeditionDot));
	const uint32_t end_money = c.u32(0x20);
	if (sailing) {
		sailing->fn_502b20(int(convoy), e.u32(0xC));
	}
	if (merchant < 0 || save.merchants[size_t(merchant)].memory.u16(8) != 0) {
		return 0;
	}
	if (found == 0) {
		if (empty != 0) {
			add(letter_text(GameText::ExpeditionNoCentres));
		}
	} else {
		// The trading posts found go to the front (0x4E7258..0x4E72AF).
		uint8_t valid = 0;
		for (uint32_t k = 0; k < 4; ++k) {
			if (places[k] < 0x1E) {
				++valid;
				continue;
			}
			for (uint32_t j = k + 1; j < 4; ++j) {
				if (places[j] < 0x1E) {
					places[k] = places[j];
					places[j] = 0xFF;
					++valid;
					break;
				}
			}
		}
		// A game bug: the «, » between the places is written without being
		// counted, so the letter's copy (0x51AB20, [+0xC] bytes) loses the
		// text's last 2 or 4 bytes (its final 0 among them). Fixed: counted.
		auto comma = [&]() {
			text += ", ";
			if (fix_game_bugs()) {
				size += 2;
			}
		};
		switch (valid) {
		case 4:
			add(letter_text(GameText::ExpeditionTwoNew));
			add(place_text(places[0]));
			comma();
			add(place_text(places[1]));
			comma();
			add(place_text(places[2]));
			add(letter_text(GameText::ExpeditionAnd));
			add(place_text(places[3]));
			add(letter_text(GameText::ExpeditionFour));
			add(letter_text(GameText::ExpeditionOpportunities));
			break;
		case 3:
			add(letter_text(GameText::ExpeditionTwoNew));
			add(place_text(places[0]));
			comma();
			add(place_text(places[1]));
			add(letter_text(GameText::ExpeditionAnd));
			add(place_text(places[2]));
			add(letter_text(GameText::ExpeditionThree));
			add(letter_text(GameText::ExpeditionOpportunities));
			break;
		case 2:
			add(letter_text(GameText::ExpeditionTwoNew));
			add(place_text(places[0]));
			add(letter_text(GameText::ExpeditionAnd));
			add(place_text(places[1]));
			add(letter_text(GameText::ExpeditionCouldBeDiscovered));
			break;
		case 1:
			add(letter_text(GameText::ExpeditionDiscoveryOf));
			add(place_text(places[0]));
			add(letter_text(GameText::ExpeditionDiscoveryEnd));
			break;
		default:
			break;
		}
		if (found > valid) {
			// The Indian villages (0x4E7641..0x4E7743); the last text's
			// pointer (0x6B9634) goes to zeroed data in both builds: nothing.
			add(letter_text(GameText::ExpeditionNewWorld));
			const uint32_t villages = uint32_t(found - valid);
			if (villages == 1) {
				add(letter_text(GameText::ExpeditionOneVillage));
			} else if (villages == 2) {
				add(letter_text(GameText::ExpeditionTwoVillages));
			} else {
				add(letter_text(GameText::ExpeditionFoundInNewWorld));
				add(letter_text(villages == 3 ? GameText::ExpeditionThree : GameText::ExpeditionFour));
				add(letter_text(GameText::ExpeditionVillagesFound));
			}
		}
	}
	// The voyage's mishap, then the trades, the contracts and it.
	std::string mishap;
	if (storm) {
		mishap = letter_text(GameText::ExpeditionStorm);
	} else if (pirates) {
		mishap = loss == 0 ? letter_text(GameText::ExpeditionPiratesFailed)
		                   : GameTexts::sprintf_raw(letter_text(GameText::ExpeditionPiratesStole), {std::to_string(int32_t(loss))});
	}
	add(trades);
	add(contracts);
	add(mishap);
	if (start_money > 0 || end_money > 0) {
		// «\nYou have given your captain <first and family name> <money> to
		// take with him on his voyage. He returns with <money> .» (the
		// captain of the guide after 0x502B20; the format «%u\C», 0x6C02F4
		// and 0x6C02FC, the same in both builds).
		add(letter_text(GameText::ExpeditionGivenCaptain));
		const uint16_t lead = save.convoys[convoy].u16(0x10);
		if (lead < ship_count(save) && save.ships[lead].captain() < save.globals.u16(kCaptainCount)) {
			const CaptainMemory& cap = save.captains[save.ships[lead].captain()];
			add(game_first_name(save, cap.u8(2)));
			add(" ");
			add(game_family_name(save, cap.u8(3)));
			add(" ");
		}
		add(std::to_string(start_money) + "\\C");
		add(letter_text(GameText::ExpeditionTakeWith));
		add(std::to_string(end_money) + "\\C");
		add(letter_text(GameText::ExpeditionEnd));
	}
	text += '\0';
	++size;
	Letter letter(0x63);
	letter.entry.u8(5) = save.merchants[size_t(merchant)].memory.u8(0x19);
	letter.entry.u32(0xC) = size;
	letter.buffer.assign(text.begin(), text.begin() + std::ptrdiff_t(std::min<size_t>(size, text.size())));
	// Past 4000 bytes the game would write beyond its buffer (sin comprobar
	// con el oráculo: a report that long is not seen).
	send_letter(save, save.convoys[convoy].u8(0), letter);
	return 0;
}

// --- Evento 0x33 ---

// 0x4EC934 (evento 0x33, lo crea la operación 0x64 con lo que encola
// 0x4DD790): un robo en la oficina del comerciante [+0xE] en la ciudad
// [+0xF]. De las mercancías [+0x8 + k] (k = 0..5; >= 0x18 ninguna), con su
// cantidad [+0x10 + k] en cargas o barriles, se lleva hasta tres: lo que
// haya en la oficina, como mucho esa cantidad (si hay menos, [+0x10 + k]
// pasa a lo que había), y lo pone en el mercado de la ciudad. Sin oficina,
// nada. The merchant gets the letter 0x73 with what was taken. El
// despachador lo quita siempre.
void fn_4ec934(SaveGame& save, uint16_t event) {
	Event& e = save.events[event];
	const uint32_t town = e.u8(0xF);
	const int office = facilities_detail::fn_5308a0(save, e.u8(0xE), town);
	if (office < 0) {
		return;
	}
	TownMemory& t = save.towns[town].memory;
	OfficeMemory& o = save.offices[size_t(office)].memory;
	// The letter 0x73 to the merchant if something was taken
	// (0x4EC9B7..0x4ECAEC): [+0x5] the town; u16 [+0x8], [+0xA], [+0xC] each
	// ware taken << 8 + the loads or barrels (0xFFFF the unused ones); u16
	// [+0xE] his [+0x1C] << 8 | [+0x1D].
	Letter letter(0x73);
	letter.entry.u8(5) = e.u8(0xF);
	std::memset(letter.entry.bytes.data() + 8, 0xFF, 8);
	const MerchantMemory& m = save.merchants[e.u8(0xE)].memory;
	letter.entry.u16(0xE) = uint16_t((m.u8(0x1C) << 8) + m.u8(0x1D));
	uint32_t stolen = 0;
	for (uint32_t k = 0; k < 6 && stolen < 3; ++k) {
		const uint32_t ware = e.u8(0x8 + k);
		if (ware >= 0x18) {
			continue;
		}
		const int32_t unit = kBarrelWare[ware] ? 200 : 2000;
		const int32_t amount = int32_t(e.u8(0x10 + k)) * unit;
		int32_t& stock = o.s32(0x4 + 4 * ware);
		if (amount > stock) {
			e.u8(0x10 + k) = uint8_t(stock / unit);
			t.s32(0x4 + 4 * ware) += stock;
			stock = 0;
		} else {
			stock -= amount;
			t.s32(0x4 + 4 * ware) += amount;
		}
		if (e.u8(0x10 + k) > 0) {
			letter.entry.u16(8 + 2 * stolen) = uint16_t((ware << 8) + e.u8(0x10 + k));
			++stolen;
		}
	}
	if (stolen > 0) {
		send_letter(save, e.u8(0xE), letter);
	}
}

}  // namespace events_rare_detail

using namespace events_rare_detail;

int events_rare(SaveGame& save, uint16_t event, uint16_t type) {
	switch (type) {
	case 0x0E:
		return int(fn_4e4a44(save, event));
	case 0x12:
		return int(fn_4e6a54(save, event));
	case 0x1C:
		return int(fn_4e9094(save));
	case 0x1E:
		return int(fn_4dfd44(save, event));
	case 0x1F:
		return int(fn_4e0334(save, event));
	case 0x2E:
		return int(fn_4e9a94(save));
	case 0x2F:
		return int(fn_4ea0d4(save));  // El desarrollo de una ciudad nueva (new_town.cpp).
	case 0x30:
		return int(fn_4ea594(save));
	case 0x32:
		return int(fn_4eafe4(save));
	case 0x23:
	case 0x24:
	case 0x34:
		return diet_detail::fn_4e09d4(save, event, event_sailing());  // Las misiones de la dieta (diet.cpp).
	case 0x25:
		return fn_4e1b84(save, event) ? 1 : 0;  // Arde un objetivo del asedio.
	case 0x26:
		return fn_4e1e54(save, event) ? 1 : 0;  // Arde un edificio de fuera de la muralla.
	case 0x31:
		fn_4eab94(save);  // El impuesto extraordinario de una ciudad.
		return 0;
	case 0x38:
		return fn_4ecb14(save, event) ? 1 : 0;  // El monumento al alcalde reelegido.
	case 0x33:
		fn_4ec934(save, event);
		return 0;
	case 0x39:
		return int(fn_4ecdf4(save));  // La muralla de una ciudad nueva (new_town.cpp).
	case 0x35:
		fn_4e94a4(save);
		return 0;
	default:
		return -1;
	}
}

namespace {
Sailing* g_event_sailing = nullptr;
}  // namespace

void set_event_sailing(Sailing* sailing) {
	g_event_sailing = sailing;
}

Sailing* event_sailing() {
	return g_event_sailing;
}

const GameTexts* letter_texts() {
	const Sailing* sailing = event_sailing();
	return sailing ? sailing->texts() : nullptr;
}

std::string letter_text(GameText id) {
	const GameTexts* texts = letter_texts();
	return texts ? texts->raw(id) : std::string();
}

std::string letter_town_name(const SaveGame& save, uint32_t town) {
	const uint32_t named = fix_game_bugs() ? save.globals.u8(kTownIds + town) : town;
	return named < save.opaque.town_names.size() ? save.opaque.town_names[named] : std::string();
}

}  // namespace p3::data::tick

namespace p3::data {

// 0x502B20 (sobre el convoy, con el tick en que salió; solo la llama el
// evento 0x12, por eso está aquí): el convoy de una expedición vuelve al
// mapa por el oeste. Los barcos con casco pasan a (0x76, 0x640) navegando
// (0x0F, [+0x15C] a 0); los demás salen del convoy hundidos (0x11, con sus
// enlaces a 0xFFFF; si guiaba el convoy, el barco se queda con el [+0x0] de
// su capitán, que pasa a 0xFFFF). Sin barcos, el convoy se borra (0x50A3D0).
// Si no, sin guía se elige otro (0x5017F0), que se queda con el capitán del
// hundido si no tiene (si tiene, ese capitán vuelve a la lista de libres,
// 0x5098B0) y quita el bit 0x10 de [+0x14]. El dueño cobra [+0x20] (que
// vuelve a 0), [+0x38] pasa a 0xFFFF y el convoy navega hacia su origen
// [+0x1E] (la ruta del guía, 0x5172A0; [+0x1F]; 0x501BE0; el guía entra en
// la lista por x, 0x50CE50). El dueño gana 51 de reputación. Each sunk ship
// goes by letter (0x34) to a human owner; the ticker line (0x548CA0) is the interface's.
void Sailing::fn_502b20(int convoy, uint32_t departure) {
	GlobalMemory& g = save_.globals;
	const uint32_t count = g.u16(global::kShipCount);
	const uint16_t captains = g.u16(global::kCaptainCount);
	const uint8_t owner = save_.convoys[size_t(convoy)].u8(0);
	const bool merchant = owner < g.u16(global::kWorldMerchantCount);
	uint16_t last = 0xFFFF;  // [+0x8] del último barco visto (el convoy).
	uint16_t captain = 0xFFFF;
	int previous = -1;  // El barco cuyo [+0x6] enlaza el siguiente (-1: el [+0xA] del convoy).
	auto link = [&]() -> uint16_t& {
		return previous < 0 ? save_.convoys[size_t(convoy)].u16(0xA) : save_.ships[size_t(previous)].memory.u16(0x6);
	};
	bool dated = false;  // A ship seen before (the first loss is dated a day ago).
	uint32_t lost = 0;  // The ships sunk ([esp+0x24]).
	while (link() < count) {
		const uint16_t index = link();
		SavedShip& s = save_.ships[index];
		last = s.convoy();
		if (s.health() > 0) {
			s.memory.u16(0x1E) = 0x76;
			s.memory.u16(0x22) = 0x640;
			s.status() = kShipSailing;
			s.raid() = 0;
			previous = index;
			dated = true;
			continue;
		}
		GameMemory<0x3C>& c = save_.convoys[size_t(convoy)];
		if (index == c.u16(0x10)) {
			c.u16(0x10) = 0xFFFF;
			captain = s.captain();
			if (captain < captains) {
				s.captain() = save_.captains[captain].u16(0x0);
				save_.captains[captain].u16(0x0) = 0xFFFF;
			}
		}
		link() = s.next_in_list();
		s.next_in_list() = 0xFFFF;
		s.convoy() = 0xFFFF;
		s.memory.u16(0x50) = 0xFFFF;
		s.memory.u16(0x52) = 0xFFFF;
		s.status() = kShipSunk;
		++lost;
		// The loss, a letter 0x34 to a human owner (0x502C53..0x502CDF):
		// [+0x5] his home town (0 without a merchant); u16 [+0x8] the ship's
		// name [+0x15E], [+0xA] its [+0xE]; u32 [+0xC] when it sank: a day
		// before today for the first ship if no ship came before it, else
		// the departure + (index + 10) · (today − departure) / (ships + 10).
		Letter letter(0x34);
		letter.entry.u8(5) = merchant ? save_.merchants[owner].memory.u8(0x19) : 0;
		const uint32_t since = g.u32(global::kWorldTick) - departure;
		if (dated) {
			letter.entry.u32(0xC) = (uint32_t(index) + 10) * since / (count + 10) + departure;
		} else {
			dated = true;
			letter.entry.u32(0xC) = since - 0x100 + departure;
		}
		letter.entry.u16(8) = s.memory.u16(0x15E);
		letter.entry.u16(0xA) = s.memory.u8(0xE);
		// Without a merchant the game reads through a null pointer. Unreachable: the convoys
		// 0x501680 makes have no expedition route ([+0x24] 0xFFFF); only a merchant's expedition
		// gives one.
		if (merchant && save_.merchants[owner].memory.u16(8) == 0) {
			send_letter(save_, owner, letter);
		}
	}
	if (lost > 0) {
		// «N of your ships have been sunk whilst on expedition.» (0x502D20: kind 0x1D).
		send_ticker(save_, owner, 0x1D, uint16_t(lost));
	}
	if (save_.convoys[size_t(convoy)].u16(0xA) >= count) {
		fn_50a3d0(last);
		return;
	}
	if (save_.convoys[size_t(convoy)].u16(0x10) >= count) {
		fn_5017f0(convoy);
		GameMemory<0x3C>& c = save_.convoys[size_t(convoy)];
		const uint16_t lead = c.u16(0x10);
		if (lead < count) {
			if (captain < captains) {
				if (save_.ships[lead].captain() < captains) {
					tick::events_rare_detail::fn_5098b0(save_, captain);
				} else {
					save_.ships[lead].captain() = captain;
				}
			}
			c.u8(0x14) &= 0xEF;
		}
	}
	GameMemory<0x3C>& c = save_.convoys[size_t(convoy)];
	if (merchant) {
		// El juego no lo mira (sin comerciante no puede pasar).
		save_.merchants[owner].memory.u32(0x0) += c.u32(0x20);
	}
	c.u16(0x12) = kShipSailing;
	c.u16(0x38) = 0xFFFF;
	c.u32(0x20) = 0;
	const uint32_t town = uint32_t(c.u8(0x1E)) % g.u32(global::kWorldTownCount);
	if (c.u16(0x10) < count) {
		set_destination(save_.ships[c.u16(0x10)], int(town), 0, 0);  // 0x5172A0
	}
	c.u8(0x1F) = uint8_t(town);
	tick::ship_ai_detail::fn_501be0(save_, convoy);
	const uint16_t lead = save_.convoys[size_t(convoy)].u16(0x10);
	if (lead < count) {
		insert_x(lead, 0xFFFFFFFF);  // 0x50CE50
	}
	// El dueño de ahora (0x5017F0 puede haberlo cambiado; el dinero ha ido al de antes).
	const uint8_t now = save_.convoys[size_t(convoy)].u8(0);
	if (now < g.u16(global::kWorldMerchantCount)) {
		// To a human owner, the ticker line «Convoy X returns from an
		// expedition.» (0x502E63: kind 4, the guide).
		if (save_.merchants[now].memory.u16(8) == 0) {
			send_ticker(save_, now, 4, save_.convoys[size_t(convoy)].u16(0x10));
		}
		tick::events_rare_detail::add_reputation(save_.merchants[now].memory, tick::events_rare_detail::kReputationBack);
	}
}

}  // namespace p3::data
