// Los asedios de cada ciudad (SaveGame::sieges): el tick diario del objeto de
// asedio (0x6349F0), que lleva la cuenta atrás hasta el próximo asedio y lo
// encola (operación 0x8E), y el de cada tick (0x633A90), que hace luchar al
// ejército sitiador contra las defensas de la ciudad hasta que se retira.
//
// Objeto de asedio de una ciudad (SiegeMemory, 0x1024 bytes, *0x6DF270 + 0x1024·i):
//   [+0x0]    u32 tick en que se pagó el último rescate (0x633DB0: el día << 8)
//   [+0x4]    u8 ciudad
//   [+0x8]    puntero al ejército sitiador (SiegeExtras::army; aquí 1 o 0)
//   [+0xC..+0x100F] (los gráficos del asedio: 0x6339C0 los pone a 0 al leer)
//   [+0x1010] 4×u8 las mercancías que más faltan en el mundo (0x635220; 0xFF: ninguna)
//   [+0x1014] 4×u8 meses seguidos que lleva cada una en la lista (hasta 3)
//   [+0x1018] u16 cuenta atrás hasta el próximo asedio (baja cada día; 0x3FFF al crear)
//   [+0x101A] u8 (2 al crear; no se usa aquí)
//   [+0x101B] u8 semanas que faltan para poder volver a vender a la ciudad lo
//             que falta (0x635740); 0xFF: la ciudad no se vende
//   [+0x101C] lista de entregas prometidas (SiegeExtras::list_101c; 0x635440)
//   [+0x1020] lista de pagos con fecha (SiegeExtras::list_1020; 0x636230)
//
// Ejército sitiador (SiegeArmyMemory, 0x650 bytes; lo crean la operación 0x8E
// y el lector, 0x627880 y 0x627AD0):
//   [+0x0]    u32 provisiones (se gastan cada tick; a 0, [+0x78] = 1)
//   [+0x4]    u32 munición (con ella se gastan 0x17 provisiones más por tick)
//   [+0x8]    u16 x, [+0xA] u16 y: la puerta que ataca (0x632780; 0xFFFF: ninguna)
//   [+0x3A]   u16 cuenta atrás de la retirada (al llegar a [+0x6A] o a 0, se va)
//   [+0x3C]   u16 cuenta atrás del rescate (a [+0x68] / 2 la ciudad paga, 0x633DB0)
//   [+0x42]   u16 cuenta atrás de la llegada; [+0x254 + 8·k] u16 el valor en el
//             que llega cada grupo (evento 0x26 con el registro u32 [+0x258 + 8·k])
//   [+0x6C]   u8 objetivo elegido de [+0x250] (0x62E810)
//   [+0x6D]   u8 puerta atacada de las tres [+0x244 + 4·k] (registros de edificio)
//   [+0x6E]   u8 grupos que llegan; [+0x6F] u8 el siguiente (0xFF: ninguno)
//   [+0x77]   u8 la vista de la ciudad está abierta (solo la pone 0x62B860, de los
//             gráficos: sin ella todo lo de los gráficos se salta)
//   [+0x78]   u8 los sitiadores ya no atacan (sin provisiones o derrotados)
//   [+0x79]   u8 cobrando el rescate
//   [+0x21C + 4·k] 5×u32 tropas sitiadoras de cada tipo, en 16.16 (el u16 alto, [+0x21E + 4·k], es el número)
//   [+0x230 + 4·k] 5×u32 defensores, igual; los cuatro primeros los copia cada
//             tick de [0x998 + k] de la ciudad (el u16 alto) y el quinto son
//             los objetivos: [+0x242] cuántos quedan de los de [+0x250]
//   [+0x250]  puntero a los objetivos (SiegeExtras::army_targets; aquí 1 o 0)
//   [+0x5D3]  u8 ciudad
//   [+0x61C]  u8 (si no es 0, tras el rescate o la retirada se llama a 0x634C50)
//   [+0x62C]  u8 fracción de brea gastada (en 1/256)
//
// The letters (0x4D6530) are sent as in the game (src/p3data/mail.cpp): the
// news 0x38 and 0x53 to the chronicle and the prince's payment 0x85. Not
// reproduced: the windows to the player (0x469380, 0x469AF0) and the
// graphics (0x6E51AC, [+0x77], the objects [+0x1F8], [+0x574], [+0x5DC]...),
// which don't change the simulation.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "p3data/game_fixes.hpp"
#include "p3data/mail.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {

namespace {

constexpr uint32_t kTownCount = global::kWorldTownCount;  // u32
constexpr uint32_t kOfficeCount = global::kWorldOfficeCount;  // u16
constexpr uint32_t kTick = global::kWorldTick;
constexpr uint32_t kNoSieges = 0x6DE52A;  // u8 ([+0x8A] del mundo): sin asedios.
constexpr uint32_t kAldermanBusy = 0x6DF31C;  // u32: si no es 0, no se encola el asedio.
// Tabla de las ciudades (scripts/StadtDaten.ini, 0x6DDB90 + 0x34·i): [+0x30]
// u8 región. El juego la indexa aquí con el índice de la ciudad en la partida.
constexpr uint32_t kTownRegion = 0x6DDB90 + 0x30;

// Mercancías que se cuentan en barriles (200 unidades; las demás, en cargas
// de 2000): 0x672C14.
constexpr uint8_t kBarrelWare[20] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0};

// Valor de una unidad de cada mercancía (0x673A18, floats).
constexpr uint32_t kWareValueBits[20] = {0x3d6147af, 0x3ef50482, 0x3e6154ca, 0x3e322d0e, 0x3e11eb85, 0x3f0ccccd,
	0x3fb33333, 0x3f8ccccd, 0x3f845a1d, 0x40587ae1, 0x3ed33333, 0x3ce147af, 0x3fa39581, 0x3f8f5c29, 0x3ee147af,
	0x3e8e5604, 0x3ee147af, 0x3e6147af, 0x3f5ae147, 0x3d236e30};

// Divisor de la escasez de cada mercancía (0x67B638).
constexpr uint32_t kShortageDivisor[24] = {0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe,
	0xe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};

// Tipos de tropa 1..3 (0x67B504, 7 u32 por tipo): [0] daño a los sitiadores
// que ya no atacan, [1] daño de los defensores, [2] provisiones que gasta
// cada sitiador, [3] y [6] daño a un objetivo de tipo 0x32 u otro, [4] daño
// sin sitiadores del tipo 0, [5] (no se usa aquí).
constexpr uint32_t kUnit[3][7] = {
	{0x18, 0x22, 0x2, 0x5, 0x7, 0x5, 0x2},
	{0x20, 0x2e, 0x2, 0x6, 0x9, 0x6, 0x3},
	{0x24, 0x33, 0x2, 0x7, 0xa, 0x7, 0x3},
};

// Ejército nuevo (0x629C60): sitiadores de cada tipo de tropa por unidad de
// fuerza, según el tipo de ejército 0..2 (0x67B5C8, 5 u32 por tipo; en
// 16.16), y el tope de cada uno (0x67B604, u16).
constexpr uint32_t kArmyTroops[3][5] = {
	{0x106, 0x83, 0x63, 0x0, 0x37},
	{0x106, 0x83, 0x0, 0x57, 0x37},
	{0x106, 0x0, 0x63, 0x57, 0x37},
};
constexpr uint16_t kArmyTroopCap[5] = {0x28, 0x16, 0x12, 0xF, 0x6};

constexpr uint32_t kTownIds = 0x6DE4B8;  // u8 por ciudad ([+0x18] del mundo): su número de StadtDaten.ini.

// Direcciones (0x677928 + k: 0x20·k).
constexpr uint8_t kDirection1 = 0x20;  // 0x677929
constexpr uint8_t kDirection3 = 0x60;  // 0x67792B
constexpr uint8_t kDirection5 = 0xa0;  // 0x67792D
constexpr uint8_t kDirection7 = 0xe0;  // 0x67792F

float ware_value(uint32_t ware) {
	float v;
	std::memcpy(&v, &kWareValueBits[ware], sizeof(v));
	return v;
}

// 0x51F1F0: registro de edificio i de la ciudad. El juego devuelve 0 si no
// existe y quien lo llama lo usaría igual; aquí, un registro a 0.
const BuildingRecord& record(const SavedTown& st, uint32_t i) {
	static const BuildingRecord kNone{};
	return i < st.memory.u16(0x776) && i < st.records.size() ? st.records[i] : kNone;
}

// Puertas, torres y portones de la muralla (tipos de registro).
bool wall_gate(uint8_t type) {
	return type == 0x37 || type == 0x34;
}
bool wall_gate_other_side(uint8_t type) {
	return type == 0x38 || type == 0x35;
}

// a² + b² en 32 bits, como el juego (imul).
int32_t square_sum(int32_t a, int32_t b) {
	return int32_t(uint32_t(a) * uint32_t(a) + uint32_t(b) * uint32_t(b));
}

// sqrt de la CRT (0x63AAE4) más 0,5 (0x66A7D0) y __ftol: la distancia
// redondeada. Con un negativo (desbordado) el juego pasa por el error de la
// CRT y __ftol de NaN deja 0. Unreachable: the distances of a 256 × 256 grid
// don't overflow.
int32_t rounded_root(int32_t v) {
	return v < 0 ? 0 : int32_t(ftol(std::sqrt(double(v)) + 0.5));
}

// La menor de las distancias de (x, y) a las cuatro esquinas del mapa de la
// ciudad (0x7F, 0xE1), con la x al doble.
int32_t corner_distance(int32_t x, int32_t y) {
	int32_t best = rounded_root(square_sum(2 * x, y));
	best = std::min(best, rounded_root(square_sum(2 * (0x7F - x), y)));
	best = std::min(best, rounded_root(square_sum(2 * x, 0xE1 - y)));
	return std::min(best, rounded_root(square_sum(2 * (0x7F - x), 0xE1 - y)));
}

// Entrada i de [+0x250] del ejército. El juego no comprueba el tamaño: lee
// lo que haya detrás (aquí, una entrada a 0) y escribe fuera de lo
// reservado (aquí, el vector crece).
const GameMemory<0xC>& target_at(const SiegeExtras& ex, size_t i) {
	static const GameMemory<0xC> kNone{};
	return i < ex.army_targets.size() ? ex.army_targets[i] : kNone;
}
GameMemory<0xC>& target(SiegeExtras& ex, size_t i) {
	if (i >= ex.army_targets.size()) {
		ex.army_targets.resize(i + 1);
	}
	return ex.army_targets[i];
}

}  // namespace

namespace sieges_detail {

// 0x629C30: ¿el ejército sigue sitiando? Con la retirada en marcha ([+0x3A]),
// mientras no haya bajado hasta [+0x6A]; si no, mientras tenga tropas del tipo 0.
bool fn_629c30(const SaveGame& save, int siege) {
	const SiegeArmyMemory& a = *save.siege_extras[size_t(siege)].army;
	const uint16_t retreat = a.u16(0x3A);
	if (retreat != 0) {
		return a.u16(0x6A) < retreat;
	}
	return 0 < a.u16(0x21E);
}

// 0x5252F0: qué lado de la muralla está más expuesto (0..2), 0xFF si ninguno.
// Con la puerta [0x766] de la ciudad, 0 si es de alguien (dueño ≥ 0xFE) o si
// en la lista [0x768] no hay ningún registro de dueño 0xFE (1 si lo hay); si
// no, el primero de los puntos 2, 1 y 0 de town_class1 que cae fuera de la
// rejilla (0x4FCFE0).
uint8_t fn_5252f0(const SaveGame& save, int town) {
	const SavedTown& st = save.towns[size_t(town)];
	const TownMemory& t = st.memory;
	const uint16_t count = t.u16(0x776);
	const uint16_t gate = t.u16(0x766);
	if (gate < count) {
		if (record(st, gate).owner >= 0xFE) {
			return 0;
		}
		for (uint32_t i = t.u16(0x768); i < count; i = record(st, i).next) {
			if (record(st, i).owner == 0xFE) {
				return 1;
			}
		}
		return 0;
	}
	if (!fn_4fcfe0(save, town, 2)) {
		return 2;
	}
	if (!fn_4fcfe0(save, town, 1)) {
		return 1;
	}
	return fn_4fcfe0(save, town, 0) ? 0xFF : 0;
}

// 0x632780: la posición del ejército ante la puerta atacada (el registro
// [+0x244 + 4·[+0x6D]]): [+0x8], [+0xA], [+0xC], [+0xE], [+0x5A2..+0x5A8] y
// su campamento [+0x5CA..+0x5D2], según la orientación de la puerta y la
// rejilla de la ciudad.
void fn_632780(SaveGame& save, int siege) {
	SiegeArmyMemory& a = *save.siege_extras[size_t(siege)].army;
	const SavedTown& st = save.towns[a.u8(0x5D3)];
	const TownMemory& t = st.memory;
	const uint32_t index = a.u32(0x244 + 4 * size_t(a.u8(0x6D)));
	if (index >= t.u16(0x776)) {
		return;
	}
	const BuildingRecord& r = record(st, index);
	const uint16_t y3 = uint16_t(r.y - 3);
	a.u16(0xA) = y3;
	if (wall_gate(r.type) && !(y3 & 1)) {
		a.u16(0x8) = uint16_t(r.x + 1);
	} else if (wall_gate_other_side(r.type) && (y3 & 1)) {
		a.u16(0x8) = uint16_t(r.x - 1);
	} else {
		a.u16(0x8) = r.x;
	}
	a.u16(0x5A2) = uint16_t(wall_gate(r.type) ? r.x + 2 : r.x - 2);
	a.u16(0x5A4) = uint16_t(r.y - 4);
	a.u16(0x5A6) = r.x;
	a.u16(0x5A8) = r.y;
	a.u16(0xC) = r.x;
	a.u16(0xE) = uint16_t(r.y - 4);
	const size_t cell = size_t(t.u32(0x7AC)) * a.u16(0xC) + a.u16(0xE);
	const uint8_t zone = cell < st.grid.size() ? st.grid[cell] : 0;
	if (!(zone & 0x80)) {
		const uint16_t x = a.u16(0x8);
		a.u16(0xE) = uint16_t(a.u16(0xE) + 3);
		a.u16(0xC) = uint16_t(wall_gate(r.type) ? x + 1 : x - 1);
		uint16_t left;
		uint16_t right;
		if (wall_gate(r.type)) {
			a.u8(0x5D2) = kDirection3;
			left = uint16_t(x - 1);
			right = uint16_t(x + 1);
		} else {
			a.u8(0x5D2) = kDirection5;
			left = uint16_t(x + 1);
			right = uint16_t(x - 1);
		}
		const uint16_t y = a.u16(0xA);
		a.u16(0x5CC) = right;
		a.u16(0x5CA) = left;
		a.u16(0x5CE) = y;
		a.u16(0x5D0) = uint16_t(y - 4);
		return;
	}
	if (wall_gate(r.type)) {
		a.u8(0x5D2) = kDirection7;
		a.u16(0x5CA) = uint16_t(r.x + 1);
		a.u16(0x5CC) = uint16_t(r.x + 3);
	} else {
		a.u8(0x5D2) = kDirection1;
		a.u16(0x5CA) = uint16_t(r.x - 1);
		a.u16(0x5CC) = uint16_t(r.x - 3);
	}
	a.u16(0x5CE) = r.y;
	a.u16(0x5D0) = uint16_t(r.y - 4);
}

// 0x62B2B0: los objetivos del ejército ([+0x250]): los edificios de tipo 0x32
// y 0x33 terminados de la lista [0x768] de la ciudad a distancia² ≤ 0x48 de
// la puerta atacada (con la x al doble). Solo si ya tenía alguno ([+0x242]).
void fn_62b2b0(SaveGame& save, int siege) {
	SiegeExtras& ex = save.siege_extras[size_t(siege)];
	SiegeArmyMemory& a = *ex.army;
	const SavedTown& st = save.towns[a.u8(0x5D3)];
	const TownMemory& t = st.memory;
	uint8_t count = 0;
	if (a.u16(0x242) == 0) {
		a.u16(0x242) = count;
		return;
	}
	// Solo lo llama el lector y en las partidas [+0x242] siempre es 0. realloc
	// a [+0x242] entradas. Game bug: it doesn't check that the targets it finds
	// fit; with more it writes past its heap block (p3vita grows the vector).
	// El reloj *0x6DCCF8 (de los gráficos, fuera de los globales) vale 0 en el
	// oráculo y aquí. Checked with the oracle: scenario siege_branches (saves 3
	// and 5, direct calls, with [+0x242] as many as it finds).
	ex.army_targets.resize(a.u16(0x242));
	a.u32(0x250) = 1;
	for (uint32_t i = t.u16(0x768); i < t.u16(0x776);) {
		const BuildingRecord& r = record(st, i);
		if (r.pending == 0 && (r.type == 0x32 || r.type == 0x33)) {
			const BuildingRecord& gate = record(st, a.u32(0x244 + 4 * size_t(a.u8(0x6D))));
			const int32_t dx = (int32_t(gate.x) - int32_t(r.x)) * 2;
			const int32_t dy = int32_t(gate.y) - int32_t(r.y) - 2;
			if (dy * dy + dx * dx <= 0x48) {
				GameMemory<0xC>& e = target(ex, count);
				e.u32(0x8) = i;
				e.u8(0x0) = count;
				e.u32(0x4) = 0;
				++count;
			}
		}
		i = r.next;
	}
	a.u16(0x242) = count;
}

// 0x62E810: elige el objetivo [+0x6C] entre los [+0x242] de [+0x250]: el
// ([0x2E0] de la ciudad % n)-ésimo de los de tipo 0x32 terminados (n, su
// número) o, si no hay ninguno, de los demás con tipo 0x33.
void fn_62e810(SaveGame& save, int siege) {
	SiegeExtras& ex = save.siege_extras[size_t(siege)];
	SiegeArmyMemory& a = *ex.army;
	const SavedTown& st = save.towns[a.u8(0x5D3)];
	const TownMemory& t = st.memory;
	const uint16_t n = a.u16(0x242);
	if (n == 0) {
		return;
	}
	// Con objetivos solo sale con estados retocados (desde 0x6281F0, al caer
	// uno; en las partidas, [+0x242] siempre es 0).
	uint8_t type32 = 0;
	uint8_t others = 0;
	for (uint8_t b = 0; b < n; ++b) {
		const BuildingRecord& r = record(st, target_at(ex, b).u32(0x8));
		if (r.pending == 0) {
			if (r.type == 0x32) {
				++type32;
			} else {
				++others;
			}
		}
	}
	const uint8_t type = type32 > 0 ? 0x32 : 0x33;
	const uint8_t divisor = type32 > 0 ? type32 : others;
	// El juego divide por 0 si no hay ninguno de los dos.
	a.u8(0x6C) = divisor ? uint8_t(t.s32(0x2E0) % int32_t(divisor)) : 0;
	for (uint8_t b = 0; b <= a.u8(0x6C); ++b) {
		const BuildingRecord& r = record(st, target_at(ex, b).u32(0x8));
		if (r.pending != 0 || r.type != type) {
			++a.u8(0x6C);  // Checked with the oracle: scenario siege_branches (saves 3 and 5, direct calls).
		}
	}
}

// 0x6326E0: brea que gasta el ejército al atacar una puerta de tipo 0x34 o
// 0x35 (0x190), si tiene munición, ataca y ya ha llegado.
uint32_t fn_6326e0(const SaveGame& save, int siege) {
	const SiegeArmyMemory& a = *save.siege_extras[size_t(siege)].army;
	if (a.u32(0x4) == 0 || a.u8(0x78) != 0 || a.u16(0x42) != 0) {
		return 0;
	}
	const SavedTown& st = save.towns[a.u8(0x5D3)];
	const uint32_t index = a.u32(0x244 + 4 * size_t(a.u8(0x6D)));
	if (index >= st.memory.u16(0x776)) {
		return 0;
	}
	const uint8_t type = record(st, index).type;
	return type == 0x34 || type == 0x35 ? 0x190 : 0;
}

// 0x51D5F0 (sobre la ciudad): se pierde una de las defensas [0x998 + k]: 15
// mendigos más, 20 habitantes menos de los pobres, acomodados y ricos (de
// la primera clase que los tenga, dejando al menos 1) y 5 menos en el u16
// [0x844] (la primera instalación).
void fn_51d5f0(SaveGame& save, int town, uint32_t k) {
	TownMemory& t = save.towns[size_t(town)].memory;
	const uint8_t n = t.u8(0x998 + k);
	if (n == 0) {
		return;
	}
	t.u8(0x998 + k) = uint8_t(n - 1);
	const uint32_t beggars = t.u32(0x2E4) + 0xF;
	t.u16(0x844) = uint16_t(t.u16(0x844) + 0xFFFB);
	t.u32(0x2E4) = beggars;
	t.u32(0x2D4) = beggars;
	int32_t left = 0x14;
	for (uint32_t o = 0x2E0; o >= 0x2D8; o -= 4) {
		const int32_t v = t.s32(o);
		if (v > left) {
			t.s32(o) = int32_t(uint32_t(v) - uint32_t(left));
			left = 0;
		} else {
			t.s32(o) = 1;  // Checked with the oracle: scenario siege_branches (saves 3 and 5, direct calls).
			left = int32_t(uint32_t(left) + (1u - uint32_t(v)));
		}
		t.u32(0x2D4) += t.u32(o);
	}
}

// 0x51D680 (sobre la ciudad): una defensa más en [0x998 + k]: 20 pobres más,
// 15 mendigos menos (hasta 0) y 5 más en el u16 [0x844].
void fn_51d680(SaveGame& save, int town, uint32_t k) {
	TownMemory& t = save.towns[size_t(town)].memory;
	const uint32_t poor = t.u32(0x2E0) + 0x14;
	t.u32(0x2E0) = poor;
	const int32_t beggars = t.s32(0x2E4);
	t.u32(0x2E4) = beggars < 0xF ? 0 : uint32_t(beggars) - 0xF;
	t.u32(0x2D4) = t.u32(0x2DC) + t.u32(0x2D8) + poor + t.u32(0x2E4);
	++t.u8(0x998 + k);
	t.u16(0x844) = uint16_t(t.u16(0x844) + 5);
}

// 0x634C50 (sobre el asedio): tras el rescate (`paid`) o la retirada. Con
// `paid`, marca ([+0x9] = 1) en las listas [+0x1020] de todos los asedios
// los pagos a esta ciudad ([+0x6]). Después vuelve a poner la cuenta atrás
// [+0x1018]: si la ciudad no se vende ([+0x101B] = 0xFF), [0x2D4] · tick %
// 0xA001 + 0x2800; si no, suma 0x1FFF (0x2FFF con `paid`) hasta 0x4FFF.
void fn_634c50(SaveGame& save, int siege, bool paid) {
	SiegeMemory& s = save.sieges[size_t(siege)];
	if (paid) {
		for (uint32_t j = 0; j < save.globals.u32(kTownCount) && j < save.siege_extras.size(); j = uint8_t(j + 1)) {
			for (GameMemory<0x10>& node : save.siege_extras[j].list_1020) {
				if (node.u8(0x6) == s.u8(0x4)) {
					node.u8(0x9) = 1;
				}
			}
		}
	}
	if (s.u8(0x101B) == 0xFF) {
		const uint32_t people = save.towns[s.u8(0x4)].memory.u32(0x2D4);
		s.u16(0x1018) = uint16_t((people * save.globals.u32(kTick)) % 0xA001 + 0x2800);
		return;
	}
	const uint16_t countdown = s.u16(0x1018);
	if (countdown > 0x4FFF) {
		return;
	}
	const uint32_t add = paid ? 0x2FFF : 0x1FFF;
	s.u16(0x1018) = uint16_t(countdown + add > 0x4FFF ? 0x4FFF : countdown + add);
}

// 0x633DB0 (sobre el asedio; desde el ejército con convoy = 0xFFFF): la
// ciudad paga el rescate si han pasado más de 14 días desde el último. El
// límite es 16 · (4 ricos + 2 acomodados + pobres); paga con su dinero (si
// tiene), luego con las mercancías de las oficinas de los jugadores humanos
// en la ciudad y por último con las suyas, de la más valiosa a la menos (las
// mercancías 0..19 de kWareValue, de 200 en 200 unidades, hasta 20000 en
// total). `amount` es el dinero que paga (solo lo apunta 0x529F90, como
// gasto 8) y `offices` las oficinas que ha mirado. Devuelve si ha pagado
// algo; entonces [+0x0] pasa a ser hoy.
//
// With a convoy (the town battle's end 0x5FE7E0, 0x5FEAC9, when the attackers
// won): the wares go into its ships, from the first one, as long as each has
// room (its hold [+0x10] − [+0x11C] − [+0x118]); a full ship hands over to the
// next one and the same office (or, for the town's wares, the whole list
// again) goes on with it. A merchant's convoy has no limit (0xFFFFFFFF); the
// offices of the ship's owner are left alone. The town's wares change the
// ship's average price ([+0xB4]), the offices' don't. Without a next ship the
// remaining offices are only counted, and the town keeps its wares (a full
// ship's room left is under a barrel). Then, if the guide's [+0x15D]
// is a merchant and the guide has a captain, that merchant gets the value of
// the wares (not the money: the caller gives it to the convoy's owner) and 65
// of fame (0x672DE4), and, with a share of 65 % of all, the letter 0x6A.
bool fn_633db0(SaveGame& save, int siege, uint32_t& amount, uint16_t& offices, uint16_t convoy) {
	SiegeMemory& s = save.sieges[size_t(siege)];
	GlobalMemory& g = save.globals;
	const uint32_t today = g.u32(kTick) >> 8;
	if (today <= (s.u32(0x0) >> 8) + 0xE) {
		amount = 0;
		return false;
	}
	const int town = s.u8(0x4);
	TownMemory& t = save.towns[size_t(town)].memory;
	uint32_t limit = (t.u32(0x2E0) + (t.u32(0x2DC) + t.u32(0x2D8) * 2) * 2) << 4;
	const int32_t money = t.s32(0x700);
	amount = money > 0 ? uint32_t(money) : 0;
	const uint16_t ships = g.u16(global::kShipCount);
	const bool with_convoy = convoy < save.convoy_count();
	SavedShip* ship = nullptr;  // edi: the ship that takes the wares.
	bool full = false, loaded = false;  // [esp+0x13], [esp+0x12]
	auto room = [](const ShipMemory& m) { return m.u32(0x10) - m.u32(0x11C) - m.u32(0x118); };
	uint32_t capacity = 20000;  // Sin convoy.
	if (with_convoy) {
		// The game takes the first ship without checking it (a battle's convoy has ships).
		const uint16_t first = save.convoys[convoy].u16(0xA);
		if (first < ships) {
			ship = &save.ships[first];
			if (ship->memory.u8(0x0) < g.u16(global::kWorldMerchantCount)) {
				limit = 0xFFFFFFFF;
			}
			capacity = room(ship->memory);
		}
	}
	// Mercancías de la más valiosa a la menos (inserción; las iguales, en su orden).
	uint8_t order[20];
	for (uint8_t w = 0; w < 20; ++w) {
		uint8_t pos = w;
		while (pos > 0) {
			const uint8_t before = order[pos - 1];
			if (!(ware_value(w) > ware_value(before))) {
				break;
			}
			order[pos] = before;
			--pos;
		}
		order[pos] = w;
	}
	uint32_t paid = 0;  // Valor de las mercancías.
	offices = 0;
	for (uint32_t o = t.u16(0x784); o < g.u16(kOfficeCount);) {
		OfficeMemory& office = save.offices[o].memory;
		if (with_convoy && !ship) {
			o = office.u16(0x2CA);  // No ship left: the offices are only counted.
			++offices;
			continue;
		}
		const bool own = ship && ship->memory.u8(0x0) == office.u16(0x2C4);
		if (!own && amount + paid < limit && save.merchants[office.u16(0x2C4)].memory.u16(0x8) == 0) {
			// Con estados retocados: en las partidas no sale un jugador humano
			// con oficina en una ciudad que pague.
			for (uint8_t w : order) {
				if (!kBarrelWare[w]) {
					continue;
				}
				uint32_t take = office.u32(0x4 + 4 * size_t(w));
				const float stock = float(int64_t(take));
				const uint32_t value = uint32_t(ftol(double(stock) * double(ware_value(w))));
				if (value + amount + paid > limit) {
					if (limit <= amount + paid) {
						take = 0;
					} else {
						const float rest = float(int64_t(limit - amount - paid));
						take = uint32_t(ftol(double(rest) / double(ware_value(w))));
					}
				}
				if (take > capacity) {
					take = capacity;
					full = ship != nullptr;
				}
				take -= take % 200;
				if (take == 0) {
					continue;
				}
				if (ship) {
					ship->memory.u32(0x54 + 4 * size_t(w)) += take;
				}
				capacity -= take;
				office.u32(0x4 + 4 * size_t(w)) -= take;
				loaded = true;
				const float taken = float(int64_t(take));
				paid += uint32_t(ftol(double(taken) * double(ware_value(w))));
			}
			if (ship && loaded) {
				recalculate_cargo(*ship);  // 0x5182B0
				loaded = false;
			}
		}
		if (ship && full) {
			// The next ship of the convoy takes over, with the same office.
			const uint16_t next = ship->memory.u16(0x6);
			if (next < ships) {
				loaded = false;
				full = false;
				ship = &save.ships[next];
				capacity = room(ship->memory);
				continue;
			}
			ship = nullptr;
			full = false;
		}
		o = office.u16(0x2CA);
		++offices;
	}
	while (amount + paid < limit) {
		for (uint8_t w : order) {
			uint32_t take = t.u32(0x4 + 4 * size_t(w));
			if (take == 0 || !kBarrelWare[w]) {
				continue;
			}
			const float stock = float(int64_t(take));
			const uint32_t value = uint32_t(ftol(double(stock) * double(ware_value(w))));
			if (value + amount + paid > limit) {
				if (limit <= amount + paid) {
					take = 0;  // Unreachable: here amount + paid only reaches the limit if a
					// ware's 200 · k units are worth an exact number, and none of wares 0..19 is (with
					// the capacity of 20000; the exact ones, 20..23, are not in the list).
				} else {
					const float rest = float(int64_t(limit - amount - paid));
					take = uint32_t(ftol(double(rest) / double(ware_value(w))));
				}
			}
			if (take > capacity) {
				take = capacity;
				full = ship != nullptr;
			}
			take -= take % 200;
			if (take == 0) {
				continue;
			}
			if (ship) {
				// The cargo and its average price, as floats stored at each step.
				ShipMemory& m = ship->memory;
				const uint32_t old = m.u32(0x54 + 4 * size_t(w));
				if (old == 0) {
					m.u32(0xB4 + 4 * size_t(w)) = 0;
					m.u32(0x54 + 4 * size_t(w)) = take;
				} else {
					const uint32_t now = old + take;
					m.u32(0x54 + 4 * size_t(w)) = now;
					float& price = m.f32(0xB4 + 4 * size_t(w));
					price = float(double(float(int32_t(old))) * double(price));
					price = float(double(price) / double(float(int32_t(now))));
				}
			}
			loaded = true;
			t.u32(0x4 + 4 * size_t(w)) -= take;
			capacity -= take;
			const float taken = float(int64_t(take));
			paid += uint32_t(ftol(double(taken) * double(ware_value(w))));
		}
		if (!ship) {
			break;
		}
		if (loaded) {
			recalculate_cargo(*ship);  // 0x5182B0 ([esp+0x12] stays set)
		}
		// A full ship hands over to the next one, and the whole list goes again
		// ([esp+0x13] stays set, so after the next one the one after takes over too).
		if (!full || ship->memory.u16(0x6) >= ships) {
			break;
		}
		loaded = false;
		ship = &save.ships[ship->memory.u16(0x6)];
		capacity = room(ship->memory);
	}
	if (with_convoy) {
		// The guide's [+0x15D] (the game reads the guide without checking it).
		const uint16_t guide = save.convoys[convoy].u16(0x10);
		const ShipMemory* gs = guide < ships ? &save.ships[guide].memory : nullptr;
		if (gs && gs->u8(0x15D) < g.u16(global::kWorldMerchantCount) && gs->u16(0x42) < g.u16(global::kCaptainCount)) {
			MerchantMemory& m = save.merchants[gs->u8(0x15D)].memory;
			const CaptainMemory& c = save.captains[gs->u16(0x42)];
			const uint32_t share = uint32_t((uint64_t((amount + paid) * 65) * 0x51EB851Fu) >> 32) >> 5;
			m.u32(0x0) += paid;
			m.u32(0x4B8) += paid;
			constexpr int16_t kRaidFame = 65;  // 0x672DE4 (s16)
			const int16_t fame = m.s16(0x16);
			m.s16(0x16) = fame < 0x7FFF - kRaidFame ? int16_t(fame + kRaidFame) : int16_t(0x7FFF);
			if (share > 0) {
				// The letter 0x6A (0x634576..0x6345D3): to that merchant, [+0x5] his
				// home, [+0x8], [+0x9] the captain's names, [+0xA] the town, [+0xB]
				// the guide's [+0x15E] << 2, u32 [+0xC] the share.
				Letter letter(0x6A);
				letter.entry.u8(0x5) = m.u8(0x19);
				letter.entry.u8(0x8) = c.u8(0x2);
				letter.entry.u8(0x9) = c.u8(0x3);
				letter.entry.u8(0xA) = uint8_t(town);
				letter.entry.u8(0xB) = uint8_t(gs->u8(0x15E) << 2);
				letter.entry.u32(0xC) = share;
				send_letter(save, gs->u8(0x15D), letter);
			}
		}
	}
	if (amount + paid == 0) {
		return false;  // Checked with the oracle: scenario siege_branches (saves 3 and 5, direct calls).
	}
	fn_529f90(save, town, int32_t(amount), 8);
	s.u32(0x0) = today << 8;
	return true;
}

// 0x6281F0: el tick del ejército sitiador (cada tick, desde 0x633A90): copia
// las defensas de la ciudad, se retira cuando acaba la cuenta atrás
// [+0x3A] (y la ciudad deja de estar sitiada: 0x10 de [0x2C8]), deja llegar
// a sus grupos ([+0x42]), cobra el rescate ([+0x3C]) y, si no, lucha: los
// defensores y los sitiadores se hacen bajas, los sitiadores gastan
// provisiones y la ciudad brea, y según las fuerzas de cada lado los
// sitiadores pasan a cobrar el rescate o se retiran.
void fn_6281f0(SaveGame& save, int siege) {
	SiegeExtras& ex = save.siege_extras[size_t(siege)];
	SiegeArmyMemory& a = *ex.army;
	const int town = a.u8(0x5D3);
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	GlobalMemory& g = save.globals;
	// (0x6E51AC, [+0x618]: gráficos.)
	for (size_t k = 0; k < 4; ++k) {
		a.u16(0x232 + 4 * k) = t.u8(0x998 + k);
	}
	if (!fn_629c30(save, siege)) {
		if (a.u16(0x3A) != 0) {
			a.u16(0x3A) = uint16_t(a.u16(0x3A) - 1);
		}
		if (a.u16(0x3A) == 0) {
			// Se va: libera lo suyo y deja a 0 sus tropas 1..4.
			a.u32(0x5D8) = 0;
			a.u16(0x590) = 0;
			a.u16(0x592) = 0;
			a.u32(0x59C) = 0;
			a.u8(0x77) = 0;
			ex.army_targets.clear();
			a.u32(0x250) = 0;
			for (size_t k = 0; k < 4; ++k) {
				a.u16(0x21E + 4 * k) = 0;
			}
			a.u8(0x79) = 0;
			for (size_t k = 0; k < 8; ++k) {
				a.u32(0x5DC + 4 * k) = 0;
			}
		}
		t.u32(0x2C8) &= ~0x10u;
		return;
	}
	// [+0x77]: gráficos (0x6283BF).
	if (a.u16(0x42) > 0) {
		// Con estados retocados: en las partidas no llega ningún grupo.
		a.u16(0x42) = uint16_t(a.u16(0x42) - 1);
		// [+0x77]: gráficos (0x6284E5).
		const uint8_t group = a.u8(0x6F);
		if (group == 0xFF || a.u16(0x42) != a.u16(0x254 + 8 * size_t(group))) {
			return;
		}
		GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, g.u32(kTick) + 1)];
		e.u16(0x6) = 0x26;
		e.u16(0x8) = uint16_t(a.u32(0x258 + 8 * size_t(a.u8(0x6F))));
		e.u8(0x10) = a.u8(0x5D3);
		e.u8(0x11) = 1;
		a.u8(0x6F) = uint8_t(a.u8(0x6F) + 1);
		if (a.u8(0x6F) == a.u8(0x6E)) {
			a.u8(0x6F) = 0xFF;
		}
		return;  // ([+0x77]: gráficos.)
	}
	if (a.u16(0x3C) > 0) {
		const uint16_t half = uint16_t(a.u16(0x68) >> 1);
		a.u16(0x3C) = uint16_t(a.u16(0x3C) - 1);
		if (a.u16(0x3C) == half) {
			// [+0x77]: gráficos. La ciudad deja de estar sitiada y paga.
			t.u32(0x2C8) &= ~0x10u;
			uint32_t amount = 0;
			uint16_t offices = 0;
			fn_633db0(save, a.u8(0x5D3), amount, offices);
			// The news 0x38 to the chronicle (0x6289FA..0x628A52), whether the
			// town paid or not: [+0x5] the town, u16 [+0x8] the siege's days
			// (today − [+0x584] + 1, in 16 bits), u32 [+0xA] the money paid
			// (0x633DB0's first argument: as two u16), u16 [+0xE] the groups
			// [+0x6E].
			Letter letter(0x38);
			letter.entry.u8(5) = a.u8(0x5D3);
			letter.entry.u16(8) = uint16_t((g.u32(kTick) >> 8) - a.u16(0x584) + 1);
			letter.entry.u16(0xA) = uint16_t(amount);
			letter.entry.u16(0xC) = uint16_t(amount >> 16);
			letter.entry.u16(0xE) = a.u8(0x6E);
			send_letter(save, kLetterToChronicle, letter);
			if (a.u8(0x61C) != 0) {
				fn_634c50(save, a.u8(0x5D3), true);
			}
		}
		// [+0x3C] < [+0x68] / 2 y [+0x77]: gráficos (0x628AAF).
		if (a.u16(0x3C) == 0) {
			a.u16(0x3A) = uint16_t(a.u16(0x6A) * 2);
		}
		return;
	}
	if (a.u16(0x3A) > 0) {
		a.u16(0x3A) = uint16_t(a.u16(0x3A) - 1);
		// == [+0x6A] y [+0x77]: gráficos (0x628D10).
	}
	// [+0x77]: gráficos (0x628D35).

	// La batalla: bajas de los sitiadores (lost_att, de [+0x21C + 4·k]) y de
	// los defensores (lost_def, de [+0x230 + 4·k]).
	uint32_t lost_att[5] = {};
	uint32_t lost_def[5] = {};
	uint32_t defenders = uint16_t(a.u16(0x236) + a.u16(0x23A) + a.u16(0x23E));  // [esp+0x10]
	uint32_t attackers = uint16_t(a.u16(0x222) + a.u16(0x226) + a.u16(0x22A));  // [esp+0x14]
	// Objetivos terminados: [+0x242] veces mira el objetivo número `valid` y,
	// si está terminado, pasa al siguiente (cuenta los primeros seguidos).
	uint8_t valid = 0;
	if (a.u32(0x250) != 0) {
		for (uint8_t b = 0; b < a.u16(0x242); ++b) {
			if (record(st, target_at(ex, valid).u32(0x8)).pending == 0) {
				++valid;
			}
		}
	}
	uint32_t attack_damage = 0;  // [esp+0x1C]: bajas que hacen los defensores.
	uint32_t defense_damage = 0;  // [esp+0x18]
	if (a.u8(0x79) == 0) {
		const uint8_t beaten = a.u8(0x78);
		if (beaten != 0) {
			if (a.u16(0x21E) > 0) {
				lost_att[0] += uint32_t(a.u16(0x232)) * 0x44;
			} else if (uint16_t(attackers) > 0) {
				attack_damage = uint32_t(a.u16(0x232)) * 0x61;
			} else {
				lost_att[4] += uint32_t(a.u16(0x232)) * 20;
			}
		}
		if (a.u16(0x3A) > 0) {
			// Retirándose: gasta munición.
			if (lost_att[4] != 0 && a.u16(0x22E) == 0) {
				const uint32_t use = uint32_t(a.u16(0x232)) * 14;
				const uint32_t ammo = a.u32(0x4);
				if (ammo > use) {
					a.u32(0x4) = ammo - use;
				} else if (ammo != 0) {
					a.u32(0x4) = 1;
				}
			}
		} else {
			uint32_t extra = 0;
			if (beaten == 0 && a.u16(0x42) == 0) {
				uint32_t sum = uint32_t(a.u16(0x23E)) + a.u16(0x23A) + a.u16(0x236);
				if (sum > a.u16(0x21E)) {
					sum = a.u16(0x21E);
				}
				extra = sum * 40;
			}
			uint32_t pitch_attack = fn_6326e0(save, siege);  // [esp+0x2C]
			const uint8_t fraction = a.u8(0x62C);
			const uint32_t total = pitch_attack + extra;
			uint32_t pitch = (fraction + total) >> 8;
			const uint32_t stock = t.u32(0x40);  // Brea (mercancía 15).
			if (pitch > stock) {
				// Sin brea suficiente.
				pitch = stock;
				extra = (stock << 8) - fraction + 0xFF;
				const uint32_t rest = extra % 40;
				a.u8(0x62C) = uint8_t(0xFF - rest);
				extra -= rest;
				if (extra < pitch_attack) {
					// Without pitch enough. Checked with the oracle: scenario siege_branches (saves
					// 3 and 5, direct calls).
					extra = 0;
					pitch_attack = 0;
				}
				extra -= pitch_attack;
			} else {
				a.u8(0x62C) = uint8_t(fraction + uint8_t(total));
			}
			t.u32(0x40) -= pitch;
			extra /= 40;
			const uint32_t all_defenders = uint16_t(defenders);  // [esp+0x34]
			uint32_t left = uint16_t(defenders);  // [esp+0x24]
			for (size_t i = 0; i < 3; ++i) {
				const uint32_t n = a.u16(0x236 + 4 * i);
				if (uint16_t(attackers) > 0) {
					uint32_t burnt = n * extra;
					if (burnt != 0) {
						burnt = ((left >> 1) + burnt) / left;
					}
					attack_damage += (n - burnt) * kUnit[i][1];
					left -= n;
					lost_att[0] += burnt * 0x44;
				} else if (a.u16(0x21E) > 0) {
					uint32_t burnt = n * extra;
					if (burnt != 0) {
						burnt = ((left >> 1) + burnt) / left;
					}
					left -= n;
					lost_att[0] += (n - burnt) * kUnit[i][0] + burnt * 0x44;
				} else {
					lost_att[4] += kUnit[i][4] * n;
				}
			}
			if (a.u16(0x22E) > 0) {
				lost_att[4] += uint32_t(valid) * 23;
			} else if (uint16_t(attackers) > 0) {
				attack_damage += uint32_t(valid) * 116;
			} else {
				lost_att[0] += uint32_t(valid) * 82;
			}
			if (pitch_attack != 0) {
				// Con estados retocados (en las partidas, 0x6326E0 siempre devuelve
				// 0: ninguna puerta atacada es de tipo 0x34 o 0x35).
				const uint32_t ammo = a.u32(0x4);
				if (ammo > 0x1B) {
					// With pitch. Checked with the oracle: scenario siege_branches (saves 3 and 5,
					// direct calls).
					a.u32(0x4) = ammo - 0x1B;
				} else if (ammo != 0) {
					a.u32(0x4) = 1;
				}
			}
			if (a.u8(0x78) != 0) {
				if (a.u16(0x232) > 0) {
					lost_def[0] += uint32_t(a.u16(0x21E)) * 0x44;
				}
			} else if (uint16_t(defenders) > 0) {
				const uint16_t first = a.u16(0x21E);
				defense_damage = uint16_t(defenders) > first ? uint32_t(first) * 0x44 : all_defenders * 0x44;
			}
			for (size_t i = 0; i < 3; ++i) {
				const uint32_t n = a.u16(0x222 + 4 * i);
				if (uint16_t(defenders) > 0) {
					defense_damage += kUnit[i][1] * n;
				} else if (a.u8(0x78) != 0) {
					lost_def[0] += n * kUnit[i][0];
				} else if (a.u32(0x250) != 0) {
					const BuildingRecord& r = record(st, target_at(ex, a.u8(0x6C)).u32(0x8));
					lost_def[4] += (r.type == 0x32 ? kUnit[i][3] : kUnit[i][6]) * n;
				} else {
					const uint32_t food = kUnit[i][2] * n;
					if (a.u32(0x0) > food) {
						a.u32(0x0) -= food;
					} else {
						a.u32(0x0) = 0;
						a.u8(0x78) = 1;  // ([+0x77]: gráficos.)
					}
				}
			}
			if (a.u16(0x242) > 0) {
				const BuildingRecord& r = record(st, target_at(ex, a.u8(0x6C)).u32(0x8));
				lost_def[4] += uint32_t(a.u16(0x22E)) * (r.type == 0x32 ? 12 : 5);
			} else if (uint16_t(defenders) > 0) {
				defense_damage += uint32_t(a.u16(0x22E)) * 87;
			} else if (a.u8(0x78) != 0) {
				lost_def[0] += uint32_t(a.u16(0x22E)) * 61;
			} else {
				const uint32_t food = uint32_t(a.u16(0x22E)) * 4;
				if (a.u32(0x0) > food) {
					a.u32(0x0) -= food;
				} else {
					a.u32(0x0) = 0;
					a.u8(0x78) = 1;  // ([+0x77]: gráficos.)
				}
			}
			if (a.u32(0x4) > 0 && a.u8(0x78) == 0) {
				if (a.u32(0x0) > 0x17) {
					a.u32(0x0) -= 0x17;
				} else {
					a.u32(0x0) = 0;
					a.u8(0x78) = 1;  // ([+0x77]: gráficos.)
				}
			}
		}
	}
	// Reparto de las bajas entre los tipos 1..3.
	for (size_t i = 0; i < 3; ++i) {
		if (uint16_t(attackers) > 0) {
			const uint16_t n = a.u16(0x222 + 4 * i);
			const uint32_t share = uint32_t(n) * attack_damage / uint16_t(attackers);
			lost_att[1 + i] += share;
			attack_damage -= share;
			attackers -= n;
		}
		if (uint16_t(defenders) > 0) {
			const uint16_t n = a.u16(0x236 + 4 * i);
			const uint32_t share = uint32_t(n) * defense_damage / uint16_t(defenders);
			lost_def[1 + i] += share;
			defense_damage -= share;
			defenders -= n;
		}
	}
	const uint16_t old_targets = a.u16(0x242);
	for (size_t k = 0; k < 5; ++k) {
		uint32_t& att = a.u32(0x21C + 4 * k);
		att = lost_att[k] < att ? att - lost_att[k] : 0;
		uint32_t& def = a.u32(0x230 + 4 * k);
		def = lost_def[k] < def ? def - lost_def[k] : 0;
		if (k < 4 && t.u8(0x998 + k) > a.u16(0x232 + 4 * k)) {
			fn_51d5f0(save, town, uint32_t(k));
		}
	}
	if (old_targets > a.u16(0x242)) {
		// Ha caído un objetivo ([+0x242] es el u16 alto de los defensores
		// del tipo 4, [+0x240]): evento 0x25 con su celda, su sitio lo
		// ocupa el último y se elige otro.
		const BuildingRecord& r = record(st, target_at(ex, a.u8(0x6C)).u32(0x8));
		const uint8_t x = r.x;
		const uint8_t y = r.y;
		GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, g.u32(kTick) + 1)];
		e.u16(0x6) = 0x25;
		e.u16(0x8) = 0xFFFF;
		e.u16(0xA) = 0;
		e.u16(0xC) = x;
		e.u16(0xE) = y;
		e.u8(0x10) = a.u8(0x5D3);
		e.u8(0x11) = 1;
		const GameMemory<0xC> last = target_at(ex, size_t(old_targets) - 1);
		target(ex, a.u8(0x6C)).u32(0x8) = last.u32(0x8);
		target(ex, a.u8(0x6C)).u32(0x4) = last.u32(0x4);
		fn_62e810(save, siege);
		a.u32(0x620) = 0xFFFFFFFF;
		a.u32(0x624) = 0;
		a.u32(0x628) = 0;  // (Gráficos.)
	}
	if (a.u16(0x232) == 0 && a.u16(0x21E) == 0) {
		a.u16(0x232) = 1;
		fn_51d680(save, town, 0);
	}
	// [+0x578] < [+0x20] y [+0x574]: objetos de los gráficos (sin la vista
	// de la ciudad [+0x20] y [+0x574] valen 0).
	const uint32_t attack = 72u * a.u16(0x21E) + 24u * a.u16(0x226) + 280u * a.u16(0x22E) + 27u * a.u16(0x22A) +
		18u * a.u16(0x222);
	uint32_t defense = 72u * a.u16(0x232) + 24u * a.u16(0x23A) + 27u * a.u16(0x23E) + 18u * a.u16(0x236);
	for (uint32_t j = 0; j < valid; ++j) {
		const BuildingRecord& r = record(st, target_at(ex, j).u32(0x8));
		if (r.pending == 0) {
			defense += r.type == 0x32 ? 0x1B0 : 0x438;
		}
	}
	const uint16_t retreat = a.u16(0x3A);
	if (retreat == 0) {
		if (a.u8(0x78) != 0 && (a.u16(0x232) == 0 || (a.u8(0x79) != 0 && a.u16(0x3C) == 0))) {
			// Pasa a cobrar el rescate.
			a.u16(0x3C) = a.u16(0x68);
			a.u8(0x79) = 1;  // ([+0x77]: gráficos.)
			a.u16(0x242) = 0;
			return;
		}
		if (attack * 3 < defense || a.u16(0x21E) == 0) {
			// Los defensores son más del triple: los sitiadores se retiran.
			a.u8(0x78) = 1;
			t.u32(0x2C8) &= ~0x10u;
			a.u16(0x3A) = uint16_t(a.u16(0x6A) * 2);
			// The news 0x38 to the chronicle (0x6299D1..0x629A2B), as the
			// ransom's with u32 [+0xA] 0xFFFFFFFF (nothing paid: they left).
			Letter letter(0x38);
			letter.entry.u8(5) = a.u8(0x5D3);
			letter.entry.u16(8) = uint16_t((g.u32(kTick) >> 8) - a.u16(0x584) + 1);
			letter.entry.u16(0xA) = 0xFFFF;
			letter.entry.u16(0xC) = 0xFFFF;
			letter.entry.u16(0xE) = a.u8(0x6E);
			send_letter(save, kLetterToChronicle, letter);
			for (size_t k = 0; k < 4; ++k) {
				t.u16(0x300 + 2 * k) = uint16_t(t.u16(0x300 + 2 * k) + 0x14);
			}
			if (a.u8(0x61C) != 0) {
				fn_634c50(save, a.u8(0x5D3), false);
			}
			a.u16(0x242) = 0;  // ([+0x77]: gráficos.)
			return;
		}
	}
	const uint8_t beaten = a.u8(0x78);
	if (beaten == 0 && uint32_t(a.u16(0x232)) * 72 == defense && defense > attack) {
		a.u8(0x78) = 1;  // ([+0x77]: gráficos.)
		return;
	}
	// [+0x77]: gráficos (0x629B43).
	if (a.u8(0x79) != 0) {
		return;
	}
	if (attack > defense * 2 && retreat == 0 && a.u16(0x3C) == 0 && beaten != 0) {
		a.u16(0x3C) = a.u16(0x68);
		a.u8(0x79) = 1;  // ([+0x77]: gráficos.)
		a.u16(0x242) = 0;
	}
}

// 0x532290 (sobre el mundo): suma en `stock` lo que tienen todas las
// ciudades de cada mercancía ([0x4 + 4·k]) y en `need` lo que consumen
// ([0x64 + 4·k] y [0x310 + 4·k] de cada ciudad y [0x64 + 4·k] de sus
// oficinas). Después quita de `stock` una reserva por ciudad en las
// mercancías 8, 11, 12, 15, 17 y 19 (hasta 0).
void fn_532290(const SaveGame& save, uint32_t (&stock)[24], uint32_t (&need)[24]) {
	const GlobalMemory& g = save.globals;
	std::memset(stock, 0, sizeof(stock));
	std::memset(need, 0, sizeof(need));
	const uint32_t towns = g.u32(kTownCount);
	for (uint32_t i = 0; i < towns; ++i) {
		const TownMemory& t = save.towns[i].memory;
		for (size_t k = 0; k < 24; ++k) {
			stock[k] += t.u32(0x4 + 4 * k);
			need[k] += t.u32(0x64 + 4 * k) + t.u32(0x310 + 4 * k);
		}
		for (uint32_t o = t.u16(0x784); o < g.u16(kOfficeCount);) {
			const OfficeMemory& office = save.offices[o].memory;
			o = office.u16(0x2CA);
			for (size_t k = 0; k < 24; ++k) {
				need[k] += office.u32(0x64 + 4 * k);
			}
		}
	}
	// Tabla de saltos 0x5323E4 para las mercancías 8..19.
	static constexpr uint32_t kReserve[12] = {0x320, 0, 0, 0x14820, 0xfa0, 0, 0, 0xe10, 0, 0x4e20, 0, 0x27100};
	for (size_t k = 8; k < 20; ++k) {
		if (kReserve[k - 8] == 0) {
			continue;
		}
		const uint32_t reserve = towns * kReserve[k - 8];
		stock[k] = reserve > stock[k] ? 0 : stock[k] - reserve;
	}
}

// 0x635220 (sobre el asedio, el día 1 de cada mes): las cuatro mercancías de
// las que hay menos en el mundo para lo que se consume ([+0x1010]) y los
// meses seguidos que lleva cada una en la lista ([+0x1014], hasta 3). La
// primera vez (las cuatro a 0xFF) entran las cuatro más escasas; después,
// solo con escasez > 0xFF.
void fn_635220(SaveGame& save, int siege) {
	SiegeMemory& s = save.sieges[size_t(siege)];
	const TownMemory& t = save.towns[s.u8(0x4)].memory;
	bool first = true;
	for (size_t b = 0; b < 4; ++b) {
		if (s.u8(0x1010 + b) != 0xFF) {
			first = false;
			break;
		}
	}
	uint32_t stock[24];
	uint32_t need[24];
	fn_532290(save, stock, need);
	uint8_t best[4] = {0xFF, 0xFF, 0xFF, 0xFF};  // [esp+0x18]
	uint32_t value[4] = {};  // [esp+0x24]
	for (uint8_t w = 0; w < 24; ++w) {
		uint32_t have = stock[w];
		uint32_t want = need[w];
		uint32_t shortage;
		if (have > 0x800000) {
			// Over 0x800000 in the towns. Checked with the oracle: scenario siege_branches (saves 3 and 5, direct calls).
			want >>= 9;
			if (want == 0) {
				shortage = have;
			} else {
				const uint32_t mix = t.u32(0x2D4) + have;
				have >>= 9;
				shortage = (mix % 0x66 + 0xCC) * have / want;
			}
		} else if (want == 0) {
			shortage = have;
		} else {
			const uint32_t mix = t.u32(0x2D4) + have;
			shortage = (mix % 0x66 + 0xCC) * have / want;
		}
		shortage /= kShortageDivisor[w];
		if (shortage <= 0xFF && !first) {
			continue;
		}
		if (shortage <= value[3]) {
			continue;
		}
		// Inserción en la lista, ordenada de mayor a menor.
		int k = 3;
		for (;;) {
			if (k < 3) {
				best[k + 1] = best[k];
				value[k + 1] = value[k];
			}
			best[k] = w;
			value[k] = shortage;
			if (k == 0 || shortage <= value[k - 1]) {
				break;
			}
			--k;
		}
	}
	if (first) {
		for (size_t k = 1; k < 4; ++k) {
			if (value[k] < 0x100) {
				best[k] = 0xFF;  // Checked with the oracle: scenario siege_branches (saves 3 and 5, direct calls).
			}
		}
	}
	// Meses: los que ya estaban suben 1 (hasta 3). Si una entrada queda vacía
	// (0xFF), el juego copia bytes de su pila sin inicializar: la variable del
	// bucle (0x18) en la primera y, en las demás, lo que haya (0 en el oráculo).
	uint8_t months[4] = {0x18, 0, 0, 0};
	for (size_t d = 0; d < 4; ++d) {
		if (best[d] == 0xFF) {
			continue;
		}
		months[d] = 1;
		for (size_t b = 0; b < 4; ++b) {
			if (best[d] == s.u8(0x1010 + b)) {
				months[d] = s.u8(0x1014 + b);
				if (months[d] < 3) {
					++months[d];
				}
			}
		}
	}
	for (size_t d = 0; d < 4; ++d) {
		s.u8(0x1010 + d) = best[d];
		s.u8(0x1014 + d) = months[d];
	}
}

// 0x635440 (sobre el asedio, los días 8 y 22): las entregas prometidas de
// la lista [+0x101C] (nodo: [+0x0] mercancía, [+0x1] entregas que faltan,
// [+0x2] hechas (0xFF: la primera vez solo se pone a 0), [+0x3]
// comerciante, u16 [+0x4] barriles o cargas, u16 [+0x6] precio de cada
// una). Si la oficina del comerciante en la ciudad tiene la mercancía, se
// la quita, le paga y la cuenta atrás [+0x1018] sube (hasta 0x4FFF); si no,
// baja 0x19A (hasta 1). Cuando no faltan más, el nodo sale de la lista.
void fn_635440(SaveGame& save, int siege) {
	SiegeMemory& s = save.sieges[size_t(siege)];
	std::vector<GameMemory<8>>& list = save.siege_extras[size_t(siege)].list_101c;
	const TownMemory& t = save.towns[s.u8(0x4)].memory;
	GlobalMemory& g = save.globals;
	// Con estados retocados (en las partidas la lista está vacía).
	for (size_t i = 0; i < list.size();) {
		GameMemory<8>& node = list[i];
		if (node.u8(0x2) == 0xFF) {
			node.u8(0x2) = 0;
			++i;
			continue;
		}
		int office = -1;
		for (uint32_t o = t.u16(0x784); o < g.u16(kOfficeCount); o = save.offices[o].memory.u16(0x2CA)) {
			if (save.offices[o].memory.u16(0x2C4) == node.u8(0x3)) {
				office = int(o);
				break;
			}
		}
		if (office >= 0) {
			OfficeMemory& m = save.offices[size_t(office)].memory;
			const uint8_t ware = node.u8(0x0);
			const uint32_t units = uint32_t(node.u16(0x4)) * (kBarrelWare[ware] ? 200 : 2000);
			if (m.u32(0x4 + 4 * size_t(ware)) < units) {
				const uint16_t countdown = s.u16(0x1018);
				s.u16(0x1018) = countdown > 0x19A ? uint16_t(countdown - 0x19A) : 1;
			} else {
				MerchantMemory& merchant = save.merchants[node.u8(0x3)].memory;
				const uint32_t pay = uint32_t(node.u16(0x6)) * node.u16(0x4);
				merchant.u32(0x0) += pay;
				merchant.u32(0x4C8) += pay;
				m.u32(0x4 + 4 * size_t(ware)) -= units;
				const uint16_t countdown = s.u16(0x1018);
				uint32_t gain = node.u16(0x4);
				if (countdown <= 0x4FFF) {
					if (!kBarrelWare[ware]) {
						gain *= 5;
					}
					if (gain >= 1000) {
						s.u16(0x1018) = 0x4FFF;
					} else {
						gain = (gain << 9) / 25;
						// The 0x4FFF cap: checked with the oracle, scenario siege_branches.
						s.u16(0x1018) = countdown + gain > 0x4FFF ? 0x4FFF : uint16_t(countdown + gain);
					}
				}
				++node.u8(0x2);
			}
		}
		node.u8(0x1) = uint8_t(node.u8(0x1) - 1);
		if (node.u8(0x1) != 0) {
			++i;
			continue;
		}
		list.erase(list.begin() + std::ptrdiff_t(i));
	}
	s.u32(0x101C) = list.empty() ? 0 : 1;
}

// 0x636230 (sobre el asedio, cada día): los pagos de la lista [+0x1020]
// cuyo tick ([+0xC]) ya ha pasado salen de ella. Si [+0x7] y [+0x8], el
// comerciante [+0x5] cobra el valor de lo que entregó (madera [+0x0],
// grano [+0x1], herramientas [+0x2], brea [+0x3] y cerveza [+0x4], en
// barriles o cargas), más si es para otra ciudad ([+0x6]) que pagó el
// rescate ([+0x9]) y menos si no; con [+0x7] sin [+0x8], la cuenta atrás
// [+0x1018] baja 0x19A (hasta 1).
void fn_636230(SaveGame& save, int siege) {
	SiegeMemory& s = save.sieges[size_t(siege)];
	std::vector<GameMemory<0x10>>& list = save.siege_extras[size_t(siege)].list_1020;
	const GlobalMemory& g = save.globals;
	// Con estados retocados (en las partidas la lista está vacía).
	while (!list.empty() && list.front().u32(0xC) < g.u32(kTick)) {
		const GameMemory<0x10>& node = list.front();
		if (node.u8(0x7) != 0) {
			if (node.u8(0x8) != 0) {
				auto part = [&](uint32_t ware, uint8_t count) {
					const float unit = float(int32_t(kBarrelWare[ware] ? 200 : 2000));
					const float n = float(int32_t(count));
					return double(unit) * double(n) * double(ware_value(ware));
				};
				const double v = part(0, node.u8(0x1)) + part(15, node.u8(0x3)) + part(12, node.u8(0x2)) +
					part(3, node.u8(0x4)) + part(11, node.u8(0x0));
				uint32_t pay = uint32_t(ftol(v));
				if (node.u8(0x6) != 0xFF) {
					const uint32_t people = save.towns[s.u8(0x4)].memory.u32(0x2D4);
					if (node.u8(0x9) != 0) {
						pay = (pay >> 1) + pay + people % (pay / 5);
					} else {
						pay -= people % ((pay * 3) / 10);
					}
				}
				MerchantMemory& merchant = save.merchants[node.u8(0x5)].memory;
				merchant.u32(0x0) += pay;
				merchant.u32(0x4B8) += pay;
				// The prince's letter 0x85 to the merchant (0x63648A..0x6364E9):
				// [+0x5] his home town ([+0x19]), u16 [+0x8] 1 if the town didn't
				// pay the ransom ([+0x9] 0), u16 [+0xA] the payment's low 16 bits
				// (the letter's text reads only those, 0x484650: a payment over
				// 65 535 shows wrong, a game bug; with fix_game_bugs() the high
				// half goes to [+0xC], 0 in the game, and the viewer reads u32).
				Letter letter(0x85);
				letter.entry.u8(5) = merchant.u8(0x19);
				letter.entry.u16(8) = node.u8(0x9) == 0 ? 1 : 0;
				letter.entry.u16(0xA) = uint16_t(pay);
				if (fix_game_bugs()) {
					letter.entry.u16(0xC) = uint16_t(pay >> 16);
				}
				send_letter(save, node.u8(0x5), letter);
			} else {
				const uint16_t countdown = s.u16(0x1018);
				s.u16(0x1018) = countdown > 0x19A ? uint16_t(countdown - 0x19A) : 1;
			}
		}
		list.erase(list.begin());
	}
	s.u32(0x1020) = list.empty() ? 0 : 1;
}

// 0x635740 (sobre el asedio, los días múltiplos de 7 si no está sitiada):
// si la ciudad se vende ([+0x101B] = 0) y la cuenta atrás [+0x1018] va por
// debajo de 0x2000, le compra lo que le sobre de las mercancías que faltan
// ([+0x1010]; por encima del segundo umbral, en barriles o cargas enteros)
// y la cuenta atrás sube (hasta 0x4FFF); después espera 12 semanas. El juego
// usa los umbrales de las mercancías 0..3 ([0x4F0 + 16·b]) y no los de la
// mercancía de la entrada b.
void fn_635740(SaveGame& save, int siege) {
	SiegeMemory& s = save.sieges[size_t(siege)];
	const uint8_t wait = s.u8(0x101B);
	if (wait != 0) {
		if (wait != 0xFF) {
			s.u8(0x101B) = uint8_t(wait - 1);
		}
		return;
	}
	if (s.u16(0x1018) >= 0x2000) {
		return;
	}
	TownMemory& t = save.towns[s.u8(0x4)].memory;
	for (size_t b = 0; b < 4; ++b) {
		const uint8_t ware = s.u8(0x1010 + b);
		if (ware == 0xFF) {
			continue;
		}
		const int32_t stock = t.s32(0x4 + 4 * size_t(ware));
		if (stock <= t.s32(0x4F0 + 16 * b + 4)) {
			continue;
		}
		const uint32_t surplus = uint32_t(stock) - t.u32(0x4F0 + 16 * b);
		// Barriles o cargas enteros.
		uint32_t units = kBarrelWare[ware] ? surplus / 200 : surplus / 2000;
		t.u32(0x4 + 4 * size_t(ware)) = uint32_t(stock) - units * (kBarrelWare[ware] ? 200 : 2000);
		const uint16_t countdown = s.u16(0x1018);
		if (countdown <= 0x4FFF) {
			if (!kBarrelWare[s.u8(0x1010 + b)]) {
				units *= 5;
			}
			if (units >= 1000) {
				s.u16(0x1018) = 0x4FFF;
			} else {
				const uint32_t gain = (units << 9) / 25;
				s.u16(0x1018) = countdown + gain > 0x4FFF ? 0x4FFF : uint16_t(countdown + gain);
			}
		}
		s.u8(0x101B) = 0xC;
	}
}

// 0x629C60 (sobre el ejército recién creado, desde 0x633AF0, con el tipo de
// ejército `kind` 0..2, `r` y `flag`): lo prepara para sitiar la ciudad.
//   Tropas: las de cada tipo según la fuerza ((dificultad + 2) · 765 + 2r) ·
//   habitantes / 2550 (0x67B5C8, con tope 0x67B604; al menos 3 del tipo 0);
//   los defensores, de [0x998 + k] de la ciudad.
//   Sin muralla (0x5252F0 da 0xFF): no ataca y cobra el rescate.
//   Con ella: de las tres primeras puertas del lado (como el lector), la que
//   tiene menos torres cerca (a distancia² ≤ 0x48; con empate, según los
//   habitantes), con esas torres de objetivos (0x62B2B0) y su posición
//   (0x632780). Los grupos que llegan ([+0x6E], [+0x258 + 8·k]): los edificios
//   de las oficinas de la ciudad fuera de la muralla (0x525220) más cerca de
//   esa puerta que de las otras, menos algunos según los ricos, ordenados
//   desde el más lejano siguiendo la dirección. De la longitud del camino
//   desde la esquina del mapa más cercana salen la llegada ([+0x42], [+0x66],
//   [+0x44] y la de cada grupo [+0x254 + 8·k]), la retirada [+0x6A] y el
//   rescate [+0x68]. Three game bugs (no room check for the groups, a gate not
//   found read from address 0, unfilled targets in the strength), fixed with
//   fix_game_bugs().
//   Al final, la fuerza de cada bando ([+0x57C], [+0x580] con los objetivos),
//   el objetivo (0x62E810), [+0x5D4] y el bit 0x10 de la situación de la
//   ciudad.
// Not reproduced: the window to the player (his home town or he is its
// mayor: 0x469380 and 0x469AF0) and the graphics (the objects [+0x5DC..]
// and, for the town in view, 0x62C730). The news 0x53 to the chronicle is
// sent.
void fn_629c60(SaveGame& save, int siege, uint8_t kind, uint8_t r, uint8_t flag) {
	SiegeExtras& ex = save.siege_extras[size_t(siege)];
	SiegeArmyMemory& a = *ex.army;
	const GlobalMemory& g = save.globals;
	if (a.u16(0x3A) != 0 || a.u16(0x21E) != 0) {
		return;  // The army is not new. Checked with the oracle: scenario siege_branches (saves 3 and 5, direct calls).
	}
	// Lo que tuviera de antes (en un ejército nuevo no hay nada).
	a.u16(0x590) = 0;
	a.u16(0x592) = 0;
	a.u8(0x77) = 0;
	a.u32(0x250) = 0;
	ex.army_targets.clear();
	a.u8(0x78) = 0;
	a.u8(0x61C) = flag;
	const int town = a.u8(0x5D3);
	const SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = save.towns[size_t(town)].memory;
	const uint8_t side = fn_5252f0(save, town);
	a.u32(0x584) = g.u32(kTick) >> 8;
	if (kind > 2) {
		kind = 2;  // No pasa: 0x6349F0 encola 0..2.
	}
	const uint32_t difficulty = save.settings.u8(0x16);
	const int32_t strength = int32_t(((difficulty + 2) * 765 + 2u * r) * t.u32(0x2D4)) / 2550;
	for (uint32_t k = 0; k < 5; ++k) {
		uint32_t troops = kArmyTroops[kind][k] * uint32_t(strength) + 0x7FFF;
		if ((troops >> 16) > kArmyTroopCap[k]) {
			troops = (troops & 0xFFFF) | (uint32_t(kArmyTroopCap[k]) << 16);
		}
		a.u32(0x21C + 4 * k) = (troops & 0xFFFF0000) | 0xFFFF;
		if (k < 4) {
			a.u16(0x232 + 4 * k) = t.u8(0x998 + k);
		}
		a.u16(0x230 + 4 * k) = 0xFFFF;
	}
	if (a.u16(0x21E) < 3) {
		a.u16(0x21E) = 3;
	}
	{
		// The news 0x53 to the chronicle (0x629F0E..0x629F63): [+0x5] the
		// town, u16 [+0x8] five times the troops of the types 0..3 (u16
		// [+0x21E + 4·k], summed in 16 bits).
		uint16_t troops = 0;
		for (size_t k = 0; k < 4; ++k) {
			troops = uint16_t(troops + a.u16(0x21E + 4 * k));
		}
		Letter letter(0x53);
		letter.entry.u8(5) = a.u8(0x5D3);
		letter.entry.u16(8) = uint16_t(troops * 5);
		send_letter(save, kLetterToChronicle, letter);
	}
	uint16_t targets = 0;
	if (side == 0xFF) {
		a.u32(0x4) = 0;
		a.u8(0x79) = 1;
		a.u8(0x78) = 1;
		a.u32(0x0) = 0;
		a.u16(0x22E) = 0;
		a.u16(0x242) = 0;
		a.u16(0x8) = 0xFFFF;
		a.u8(0x6E) = 0;
		a.u16(0x66) = 1;
		a.u16(0x42) = 0;
		a.u16(0x44) = 0;
		a.u16(0x6A) = 1;
		a.u16(0x68) = 0x1E00;
		a.u16(0x3C) = 0x1E00;
		a.u8(0x6D) = uint8_t(t.s32(0x2D4) % 3);
		a.u8(0x6F) = 0xFF;
	} else {
		a.u32(0x4) = 0x10000;
		a.u8(0x79) = 0;
		a.u32(0x0) = 0x10000;
		const uint32_t count = t.u16(0x776);
		// La puerta: de las tres primeras del lado, la de menos valor (1 si es
		// de tipo 0x34 o 0x35, más 2 por torre 0x32 y 3 por torre 0x33 cerca).
		a.u32(0x244) = t.u16(0x768);
		bool wrapped = false;
		uint16_t best = 0xFFFF;
		uint8_t ties = 0;
		uint32_t gates_found = 0;
		for (uint32_t k = 0; k < 3; ++k) {
			uint32_t& current = a.u32(0x244 + 4 * k);
			while (current < count) {
				const BuildingRecord& b = record(st, current);
				if ((wall_gate(b.type) || wall_gate_other_side(b.type)) && b.owner == 0xFF - side) {
					break;
				}
				if (!wrapped && b.next >= count) {
					wrapped = true;
					current = t.u16(0x762);
				} else {
					current = b.next;
				}
			}
			if (current >= count) {
				break;
			}
			++gates_found;
			const BuildingRecord& gate = record(st, current);
			uint32_t value = gate.type == 0x34 || gate.type == 0x35 ? 1 : 0;
			uint32_t near = 0;
			for (uint32_t i = t.u16(0x768); i < count;) {
				const BuildingRecord& b = record(st, i);
				if (b.type == 0x32 || b.type == 0x33) {
					const int32_t dx = (int32_t(gate.x) - int32_t(b.x)) * 2;
					const int32_t dy = int32_t(gate.y) - int32_t(b.y) - 2;
					if (dy * dy + dx * dx <= 0x48) {
						value += b.type == 0x32 ? 2 : 3;
						++near;
					}
				}
				i = b.next;
			}
			bool chosen = false;
			if (uint16_t(value) < best) {
				best = uint16_t(value);
				ties = 1;
				chosen = true;
			} else if (uint16_t(value) == best) {
				++ties;
				// Gates worth the same. Checked with the oracle: scenario siege_gates (saves 3 and
				// 5).
				chosen = t.s32(0x2D4) % int32_t(ties) == 0;
			}
			if (chosen) {
				targets = uint16_t(near);
				a.u8(0x6D) = uint8_t(k);
			}
			if (k == 2) {
				break;
			}
			if (!wrapped && gate.next >= count) {
				wrapped = true;
				a.u32(0x248 + 4 * k) = t.u16(0x762);
			} else {
				a.u32(0x248 + 4 * k) = gate.next;
			}
		}
		a.u16(0x242) = targets;
		fn_62b2b0(save, siege);
		fn_632780(save, siege);
		a.u8(0x6E) = 0;
		// 0x525220: los edificios de las oficinas de la ciudad que quedan fuera
		// de la muralla (sin el bit 0x80 en la rejilla). Se quedan los que
		// tienen más cerca la puerta elegida que las otras dos (y no son del
		// tipo 5).
		std::vector<uint16_t> outside;
		for (uint32_t o = t.u16(0x784); o < g.u16(kOfficeCount); o = save.offices[o].memory.u16(0x2CA)) {
			for (uint32_t i = save.offices[o].memory.u16(0x2CE); i < count; i = record(st, i).next) {
				const BuildingRecord& b = record(st, i);
				const size_t cell = size_t(b.x) * t.u32(0x7AC) + b.y;
				if (!((cell < st.grid.size() ? st.grid[cell] : 0) & 0x80)) {
					outside.push_back(uint16_t(i));
				}
			}
		}
		// The groups' records ([+0x258 + 8·k], u32; their arrival is the u16
		// [+0x254 + 8·k]). Game bug: the game doesn't check that they fit:
		// from the 101st on they run over the army's own fields ([+0x574] on:
		// a graphics object, [+0x578], the strengths [+0x57C] and [+0x580],
		// the town [+0x5D3]...) and from 0x7F on past the army, its heap
		// block. In the oracle (scenario siege_fixes, saves 3 and 5) the game
		// then works on what it has overwritten (it doesn't even mark its town
		// as besieged); p3vita doesn't reproduce that: it writes the groups
		// that fall inside the army there, as the game does, and keeps the
		// rest in `spill` for the removal below (afterwards at most
		// 4 + 63 + 1 are left). With fix_game_bugs() no more than 100 come.
		std::array<uint32_t, 256> spill{};
		auto group = [&](uint32_t k) -> uint32_t& {
			const size_t at = 0x258 + 8 * size_t(k);
			return at + 4 <= SiegeArmyMemory::kSize ? a.u32(at) : spill[k];
		};
		uint8_t pick = 0;
		for (const uint16_t i : outside) {
			const BuildingRecord& b = record(st, i);
			int32_t closest = 0x7FFFFFFF;
			pick = fix_game_bugs() ? 0xFF : 0;
			for (uint8_t k = 0; k < 3; ++k) {
				// Game bug: with fix_game_bugs() a gate not found is left out.
				if (fix_game_bugs() && k >= gates_found) {
					continue;
				}
				// Una puerta que no se ha encontrado: el juego lee del registro 0
				// (el que no existe, de la dirección 0). Checked with the oracle:
				// scenario siege_gates (saves 3 and 5; two gates on the side).
				// Game bug: on Windows reading address 0 fails. In the oracle
				// address 0 is what fs:0 points to, the SEH chain, which holds
				// the address of the latest handler on the stack while the
				// executor runs, 0x010FEF30: the gate is at (0x30, 0xEF), and
				// p3vita reads that too (scenario siege_fixes, saves 3 and 5: an
				// office building nearer it than the gates).
				BuildingRecord missing{};
				missing.x = 0x30;
				missing.y = 0xEF;
				const uint32_t found = a.u32(0x244 + 4 * size_t(k));
				const BuildingRecord& gate = found < count ? record(st, found) : missing;
				const int32_t dx = int32_t(gate.x) - int32_t(b.x);
				const int32_t dy = int32_t(gate.y) - int32_t(b.y);
				if (dy * dy + dx * dx < closest) {
					closest = dy * dy + dx * dx;
					pick = k;
				}
			}
			// With fix_game_bugs() no more than fit (up to [+0x574]).
			if (fix_game_bugs() && a.u8(0x6E) >= 100) {
				break;
			}
			if (pick == a.u8(0x6D) && b.type != 5) {
				group(a.u8(0x6E)) = i;
				++a.u8(0x6E);
			}
		}
		// Quita algunos: en el i-ésimo, si los ricos % (los que quedan − i)
		// no bajan de (los que se quedan − i), lo cambia por el último.
		if (a.u8(0x6E) > 0) {
			const int32_t rich = t.s32(0x2D8);
			const uint8_t n = a.u8(0x6E);
			uint8_t keep = uint8_t(rich % 5 + (n >> 2) + 1);
			if (keep > n) {
				keep = n;
			}
			for (uint8_t i = 0; i < a.u8(0x6E);) {
				const uint8_t last = a.u8(0x6E);
				if (rich % (int32_t(i) - int32_t(last)) >= int32_t(keep) - int32_t(i)) {
					a.u8(0x6E) = uint8_t(last - 1);
					group(i) = group(uint8_t(last - 1));
				} else {
					++i;
				}
			}
		}
		// Los ordena: primero el más lejano a la posición del ejército; después,
		// cada vez, el que más sigue la dirección del anterior (producto escalar).
		auto at_group = [&](uint32_t k) -> const BuildingRecord& { return record(st, a.u32(0x258 + 8 * k)); };
		int32_t previous_dx = 0x7FFFFFFF;
		int32_t previous_dy = 0;
		int32_t chosen_dx = 0;
		int32_t chosen_dy = 0;
		for (uint32_t j = 0; int32_t(j) < int32_t(a.u8(0x6E)) - 1; ++j) {
			int32_t top = INT32_MIN;
			for (uint32_t i = j; i < a.u8(0x6E); ++i) {
				const BuildingRecord& b = at_group(i);
				const int32_t dx = (int32_t(a.u16(0x8)) - int32_t(b.x)) * 2;
				const int32_t dy = int32_t(a.u16(0xA)) - int32_t(b.y);
				const int32_t v = previous_dx == 0x7FFFFFFF ? square_sum(dy, dx)
					: int32_t(uint32_t(previous_dy) * uint32_t(dy) + uint32_t(previous_dx) * uint32_t(dx));
				if (v > top) {
					top = v;
					pick = uint8_t(i);
					chosen_dx = dx;
					chosen_dy = dy;
				}
			}
			previous_dy = chosen_dy;
			previous_dx = chosen_dx;
			if (pick != j) {
				const uint32_t moved = a.u32(0x258 + 8 * size_t(pick));
				a.u32(0x258 + 8 * size_t(pick)) = a.u32(0x258 + 8 * size_t(j));
				a.u32(0x258 + 8 * size_t(j)) = moved & 0xFFFF;
			}
		}
		// La longitud del camino: desde la esquina más cercana al primer grupo,
		// de grupo en grupo y del último al ejército; sin grupos, desde la
		// esquina más cercana al ejército.
		const uint8_t groups = a.u8(0x6E);
		int32_t length = 0;
		int32_t first = 0;
		if (groups > 0) {
			const BuildingRecord& g0 = at_group(0);
			first = corner_distance(g0.x, g0.y);
			const BuildingRecord& last = at_group(groups - 1u);
			length = rounded_root(square_sum((int32_t(a.u16(0x8)) - last.x) * 2, int32_t(a.u16(0xA)) - last.y)) +
				first + 0x28;
			for (uint32_t k = 1; k < groups; ++k) {
				const BuildingRecord& b = at_group(k);
				const BuildingRecord& p = at_group(k - 1);
				length += rounded_root(square_sum((int32_t(b.x) - p.x) * 2, int32_t(b.y) - p.y)) + 0xA;
			}
		} else {
			length = corner_distance(a.u16(0x8), a.u16(0xA)) + 0x1E;
		}
		auto ticks = [](int32_t d) {
			const uint16_t v = uint16_t(int32_t(uint32_t(d) * 65u) >> 2);
			return v < 0x900 ? uint16_t(0x900) : v > 0x1300 ? uint16_t(0x1300) : v;
		};
		a.u16(0x66) = ticks(length);
		a.u16(0x42) = a.u16(0x66);
		a.u16(0x6A) = ticks(corner_distance(a.u16(0x8), a.u16(0xA)));
		a.u16(0x68) = uint16_t((side + 1) * 0xA00);
		const int32_t arrival = int32_t(a.u16(0x66)) - int32_t(a.u16(0x66)) * 30 / length;
		a.u16(0x44) = uint16_t(arrival);
		if (groups == 0) {
			a.u8(0x6F) = 0xFF;
		} else {
			// Cada grupo llega cuando el ejército ha recorrido su parte del camino.
			int32_t rest = length - 0x1E;
			int32_t current = arrival - int32_t(uint32_t(a.u16(0x44)) * uint32_t(first)) / rest;
			rest -= first;
			for (uint32_t k = 0; k < groups; ++k) {
				const BuildingRecord& b = at_group(k);
				const BuildingRecord& p = at_group(k == 0 ? 0 : k - 1);
				const int32_t d = rounded_root(square_sum((int32_t(b.x) - p.x) * 2, int32_t(b.y) - p.y)) + 0xA;
				const int32_t step = int32_t((uint32_t(current) & 0xFFFF) * uint32_t(d)) / rest;
				rest -= d;
				current -= step;
				a.u16(0x254 + 8 * k) = uint16_t(current);
			}
			a.u8(0x6F) = 0;
		}
	}
	// La fuerza de los sitiadores y la de los defensores (más 720 por torre
	// 0x32 terminada de los objetivos y 1800 por las demás).
	auto troops = [&](uint32_t k) { return uint32_t(a.u16(0x21E + 4 * k)); };
	auto defenders = [&](uint32_t k) { return uint32_t(a.u16(0x232 + 4 * k)); };
	a.u32(0x57C) = 120 * troops(0) + 30 * troops(1) + 40 * troops(2) + 45 * troops(3) + 378 * troops(4);
	a.u32(0x580) = 120 * defenders(0) + 30 * defenders(1) + 40 * defenders(2) + 45 * defenders(3);
	// Game bug: `targets` counts the towers near the gate in the works too,
	// which 0x62B2B0 doesn't make targets, so the last ones are entries it
	// didn't fill (record 0 here and in the oracle; in the game, whatever the
	// heap had). With fix_game_bugs() only the targets it filled ([+0x242]).
	const uint32_t counted = fix_game_bugs() ? a.u16(0x242) : targets;
	for (uint32_t i = 0; i < counted; ++i) {
		const BuildingRecord& b = record(st, target_at(ex, i).u32(0x8));
		if (b.pending == 0) {
			a.u32(0x580) += b.type == 0x32 ? 0x2D0 : 0x708;
		}
	}
	fn_62e810(save, siege);
	const uint8_t level = uint8_t(int32_t(troops(0) + troops(1) + troops(2) + troops(3)) / 6);
	a.u8(0x5D4) = level < 1 ? 1 : level > 6 ? 6 : level;
	t.u32(0x2C8) |= 0x10;
	a.u8(0x578) = 0xFF;
	a.u32(0x620) = 0xFFFFFFFF;
	a.u32(0x624) = 0;
	a.u8(0x62C) = 0xFF;
}

// 0x633AF0 (sobre el asedio de la ciudad; la operación 0x8E, con el tipo de
// ejército, r y flag): si la ciudad no tiene ya ejército, tiene el bit 0x100
// de [0x76C] y al menos tres puertas o torres de muralla (tipos ≥ 0x34 salvo
// 0x36 en la lista [0x768]; si no, también las 0x34 en obras [0x762]), crea el
// ejército (0x627880: todo a 0 salvo [+0x6F] = 0xFF) y lo prepara (0x629C60).
// Con flag, si el jugador tiene rango ≥ 5 en su ciudad natal, que no es esta
// pero es de su región, y no tiene un pago pendiente con ella ([+0x1020] del
// asedio de su ciudad natal), le pide ayuda: operación 0x8B {0x8B, 5 bytes al
// azar, ciudad natal, jugador, ciudad, u32 el día dentro de 21}.
void fn_633af0(SaveGame& save, int siege, uint8_t kind, uint8_t r, uint8_t flag) {
	SiegeMemory& s = save.sieges[size_t(siege)];
	SiegeExtras& ex = save.siege_extras[size_t(siege)];
	GlobalMemory& g = save.globals;
	const uint8_t town = s.u8(0x4);
	const SavedTown& st = save.towns[town];
	const TownMemory& t = st.memory;
	if (ex.army || !(t.u32(0x76C) & 0x100)) {
		return;
	}
	const uint32_t count = t.u16(0x776);
	uint32_t gates = 0;
	for (uint32_t i = t.u16(0x768); i < count; i = record(st, i).next) {
		const uint8_t type = record(st, i).type;
		if (type >= 0x34 && type != 0x36) {
			++gates;
		}
	}
	if (gates < 3) {
		for (uint32_t i = t.u16(0x762); i < count; i = record(st, i).next) {
			if (record(st, i).type == 0x34) {
				++gates;
			}
		}
		if (gates < 3) {
			return;
		}
	}
	s.u8(0x101A) = 2;
	SiegeArmyMemory& a = ex.army.emplace();
	a.u8(0x6F) = 0xFF;
	ex.army_targets.clear();
	s.u32(0x8) = 1;
	a.u8(0x5D3) = town;
	fn_629c60(save, siege, kind, r, flag);
	if (flag == 0) {
		return;
	}
	const uint32_t player = g.u32(global::kPlayer);
	if (player >= g.u16(global::kWorldMerchantCount)) {
		return;  // Unreachable: the human player's index is always a merchant's.
	}
	const MerchantMemory& m = save.merchants[player].memory;
	const uint8_t home = m.u8(0x19);
	if (m.u8(0x39C + home) < 5 || town == home ||
		g.u8(kTownRegion + 0x34 * uint32_t(g.u8(kTownIds + town))) !=
			g.u8(kTownRegion + 0x34 * uint32_t(g.u8(kTownIds + home)))) {
		return;
	}
	// From here (the player of the saves doesn't have rank 5): checked with the
	// oracle, scenario siege_ops (saves 3 and 5; operation 0x8B queued).
	for (const GameMemory<0x10>& node : save.siege_extras[home].list_1020) {
		if (node.u8(0x5) == uint8_t(player) && node.u8(0x7) != 0 && node.u8(0x8) == 0) {
			return;
		}
	}
	uint8_t op[0x14] = {};
	op[0] = 0x8B;
	auto roll = [&](uint32_t n, uint8_t base) { return uint8_t((int32_t(game_rand(save)) % int32_t(n) + base) * 5); };
	op[0x8] = roll(9, 8);
	op[0x6] = roll(9, 8);
	op[0x4] = roll(5, 4);
	op[0x7] = roll(9, 8);
	op[0x5] = roll(5, 4);
	op[0x9] = home;
	op[0xA] = uint8_t(player);
	op[0xB] = town;
	const uint32_t when = (g.u32(kTick) + 0x1500) & 0xFFFFFF00;
	std::memcpy(op + 0xC, &when, 4);
	enqueue_operation(save, op, sizeof(op));
}

}  // namespace sieges_detail

using namespace sieges_detail;

// 0x632690 (on the besieging army): the pitch it uses against the
// defenders, min(defenders of types 1..3, besiegers of type 0) · 40, if it
// still attacks ([+0x78] = 0) and has arrived ([+0x42] = 0). The army's tick
// (0x6281F0) has the same inline.
static uint32_t fn_632690(const SiegeArmyMemory& a) {
	if (a.u8(0x78) != 0 || a.u16(0x42) != 0) {
		return 0;
	}
	const uint32_t sum = uint32_t(a.u16(0x23E)) + a.u16(0x23A) + a.u16(0x236);
	return std::min<uint32_t>(sum, a.u16(0x21E)) * 40;
}

uint32_t fn_636920(const SaveGame& save, int siege) {
	const SiegeExtras& ex = save.siege_extras[size_t(siege)];
	if (!ex.army) {
		return 0;
	}
	return fn_632690(*ex.army) + fn_6326e0(save, siege);
}

// 0x6349F0 (sobre el asedio de la ciudad, en los ticks con t & 7 == 7, tras
// su tick): las listas del mes (día 1, 0x635220), las entregas (días 8 y 22,
// 0x635440), los pagos (0x636230) y, cada 7 días sin asedio, la venta
// (0x635740). Después baja la cuenta atrás [+0x1018] (más deprisa con más
// habitantes si la ciudad se vende, y según la dificultad [0x16] de los
// ajustes); al llegar a 0 busca una ciudad de la misma región (las que
// mire, una más que la dificultad) y, si no la encuentra o no hace mucho
// del último asedio, encola uno (operación 0x8E: {0x8E, ciudad, tipo, rand
// & 0xFF, 1}; el tipo según el año).
void fn_6349f0(SaveGame& save, int town) {
	SiegeMemory& s = save.sieges[size_t(town)];
	const SiegeExtras& ex = save.siege_extras[size_t(town)];
	GlobalMemory& g = save.globals;
	if (g.u8(kNoSieges) != 0) {
		return;
	}
	const uint8_t day = g.u8(global::kWorldDay);
	if (day == 1) {
		fn_635220(save, town);
	} else if (day == 8 || day == 22) {
		fn_635440(save, town);
	}
	fn_636230(save, town);
	if (g.u16(global::kWorldDayOfYear) % 7 == 0 && (!ex.army || !fn_629c30(save, town))) {
		fn_635740(save, town);
	}
	uint32_t step = 0x29;
	if (s.u8(0x101B) != 0xFF) {
		const int32_t people = save.towns[s.u8(0x4)].memory.s32(0x2D4);
		if (people > 0x320) {
			step = uint32_t((0x1019 - 0x190000 / people) / 50);
		}
	}
	const uint8_t difficulty = save.settings.u8(0x16);
	if (difficulty == 0) {
		step = (step * 7 + 5) / 10;
	} else if (difficulty == 2) {
		step = (step * 3 + 1) >> 1;
	}
	const uint16_t countdown = s.u16(0x1018);
	if (step < countdown) {
		s.u16(0x1018) = uint16_t(countdown - step);
		return;
	}
	if (countdown == 0) {
		return;
	}
	uint8_t tries = uint8_t(difficulty + 1);
	const uint32_t towns = g.u32(kTownCount);
	for (uint8_t j = 0; j < towns; ++j) {
		if (!ex.army && g.u32(kTick) - s.u32(0x0) > 0x1BFF) {
			continue;
		}
		if (g.u8(kTownRegion + 0x34 * uint32_t(j)) == g.u8(kTownRegion + 0x34 * uint32_t(s.u8(0x4))) || --tries == 0) {
			s.u16(0x1018) = 1;
			return;
		}
	}
	s.u16(0x1018) = 0;
	if (g.u32(kAldermanBusy) != 0) {
		return;
	}
	uint32_t op[5] = {0xFFFFFFFF, 0, 0, 0, 0};  // 0x535740
	op[0] = 0x8E;
	op[1] = s.u8(0x4);
	const uint16_t year = g.u16(global::kWorldYear);
	if (year < 0x546) {
		op[2] = 0;
	} else if (year < 0x578) {
		op[2] = uint32_t(int32_t(game_rand(save)) % 2);
	} else {
		op[2] = uint32_t(int32_t(game_rand(save)) % 3);
	}
	op[3] = game_rand(save) & 0xFF;
	op[4] = 1;
	uint8_t bytes[0x14];
	std::memcpy(bytes, op, sizeof(bytes));
	enqueue_operation(save, bytes, sizeof(bytes));
}

// 0x633A90 (sobre el asedio de la ciudad, al final de cada tick): el tick de
// su ejército (0x6281F0) y, si ya no sitia ni le queda retirada, lo deshace.
void fn_633a90(SaveGame& save, int town) {
	SiegeExtras& ex = save.siege_extras[size_t(town)];
	if (!ex.army) {
		return;
	}
	fn_6281f0(save, town);
	if (!fn_629c30(save, town) && ex.army->u16(0x3A) == 0) {
		// 0x6278E0 (0x627F70) y delete.
		ex.army.reset();
		ex.army_targets.clear();
		save.sieges[size_t(town)].u32(0x8) = 0;
	}
}

// La operación 0x8E (en 0x536A67): {0x8E, u32 ciudad, u32 tipo de ejército,
// u8 [+0xC] r, …, u32 [+0x10] flag}: un asedio a la ciudad (0x633AF0, sobre
// su objeto de asedio). La encola 0x6349F0.
void fn_536a67(SaveGame& save, const uint8_t* operation) {
	uint32_t town;
	uint32_t flag;
	std::memcpy(&town, operation + 4, 4);
	std::memcpy(&flag, operation + 0x10, 4);
	if (town >= save.globals.u32(kTownCount) || town >= save.sieges.size()) {
		return;  // Checked with the oracle: scenario siege_ops (saves 3 and 5).
	}
	fn_633af0(save, int(town), operation[8], operation[0xC], flag != 0 ? 1 : 0);
}

// The operation 0x8B (0x536974 → 0x635B80 on the siege object of the town [+0x9]): {0x8B, u8 timber, grain, tools,
// pitch, beer, the town [+0x9], the merchant [+0xA], the besieged town [+0xB], u32 [+0xC] the deadline's tick}: the
// prince of the player's home town asks him for goods (0x633AF0 queues it, above). A new node of the list [+0x1020]
// (the goods at [+0x0..+0x4], the merchant [+0x5], the besieged town [+0x6], [+0x7..+0x9] 0, the tick [+0xC]) goes
// after the last node from the head whose next one isn't later (the head itself is never compared: a node earlier
// than the head goes second), and the merchant gets the letter 0x64 («Prince»: [+0x5] the town, [+0x8] beer, [+0x9]
// tools, [+0xA] timber, [+0xB] pitch, [+0xC] grain, [+0xD] the deadline's day and [+0xE] month, [+0xF] the besieged
// town). Checked with the oracle: scenario prince_ops (saves 1, 3, 5, 7).
void fn_536974(SaveGame& save, const uint8_t* operation) {
	const uint32_t town = operation[0x9];
	if (town >= save.globals.u32(kTownCount) || town >= save.sieges.size()) {
		return;
	}
	const SiegeMemory& s = save.sieges[town];
	std::vector<GameMemory<0x10>>& list = save.siege_extras[town].list_1020;
	GameMemory<0x10> node;
	node.u8(0x0) = operation[0x4];
	node.u8(0x1) = operation[0x5];
	node.u8(0x2) = operation[0x6];
	node.u8(0x3) = operation[0x7];
	node.u8(0x4) = operation[0x8];
	node.u8(0x5) = operation[0xA];
	node.u8(0x6) = operation[0xB];
	std::memcpy(&node.u32(0xC), operation + 0xC, 4);
	if (list.empty()) {
		list.push_back(node);
	} else {
		size_t after = 0;
		while (after + 1 < list.size() && list[after + 1].u32(0xC) < node.u32(0xC)) {
			++after;
		}
		list.insert(list.begin() + long(after + 1), node);
	}
	save.sieges[town].u32(0x1020) = 1;
	uint8_t day = 0;
	uint8_t month = 0;
	uint16_t year = 0;
	events_mayor_detail::fn_5311d0(node.u32(0xC), day, month, year);
	Letter letter(0x64);
	letter.entry.u8(5) = s.u8(0x4);
	letter.entry.u8(8) = node.u8(0x4);
	letter.entry.u8(9) = node.u8(0x2);
	letter.entry.u8(0xA) = node.u8(0x0);
	letter.entry.u8(0xB) = node.u8(0x3);
	letter.entry.u8(0xC) = node.u8(0x1);
	letter.entry.u8(0xD) = day;
	letter.entry.u8(0xE) = month;
	letter.entry.u8(0xF) = node.u8(0x6);
	send_letter(save, operation[0xA], letter);
}

// The operation 0x8C (0x5369C7 → 0x635CD0 on the siege object of the town u32 [+0x4]): {0x8C, u32 the town, u32 the
// merchant, u32 the besieged town, u32 the letter's entry}: the player accepts the prince's request (the button of
// the letter 0x64, 0x4D6C90). The first node of the list [+0x1020] with that merchant [+0x5] and besieged town
// [+0x6] gets [+0x7] = 1, and the merchant the letter 0x65 (the meeting: as the 0x64 without [+0xF]); then the
// entry, if a letter 0x64, loses its button ([+0xF] = 0xFE), the node found or not.
// Checked with the oracle: scenario prince_ops (saves 1, 3, 5, 7).
void fn_5369c7(SaveGame& save, const uint8_t* operation) {
	uint32_t town;
	std::memcpy(&town, operation + 4, 4);
	if (town >= save.globals.u32(kTownCount) || town >= save.sieges.size()) {
		return;
	}
	const uint8_t merchant = operation[0x8];
	const uint8_t besieged = operation[0xC];
	for (GameMemory<0x10>& node : save.siege_extras[town].list_1020) {
		if (node.u8(0x5) != merchant || node.u8(0x6) != besieged) {
			continue;
		}
		node.u8(0x7) = 1;
		uint8_t day = 0;
		uint8_t month = 0;
		uint16_t year = 0;
		events_mayor_detail::fn_5311d0(node.u32(0xC), day, month, year);
		Letter letter(0x65);
		letter.entry.u8(5) = save.sieges[town].u8(0x4);
		letter.entry.u8(8) = node.u8(0x4);
		letter.entry.u8(9) = node.u8(0x2);
		letter.entry.u8(0xA) = node.u8(0x0);
		letter.entry.u8(0xB) = node.u8(0x3);
		letter.entry.u8(0xC) = node.u8(0x1);
		letter.entry.u8(0xD) = day;
		letter.entry.u8(0xE) = month;
		send_letter(save, merchant, letter);
		break;
	}
	// Then (0x5369F8, in the executor) the entry, if a letter 0x64, loses its button ([+0xF] = 0xFE).
	uint32_t entry;
	std::memcpy(&entry, operation + 0x10, 4);
	if (entry < save.globals.u16(kMailCount) && save.mail[entry].u8(4) == 0x64) {
		save.mail[entry].u8(0xF) = 0xFE;
	}
}

// Lo que hace 0x627AD0 tras leer los campos del ejército (desde 0x627D77).
void load_siege_army(SaveGame& save, int town) {
	SiegeExtras& ex = save.siege_extras[size_t(town)];
	SiegeArmyMemory& a = *ex.army;
	const uint16_t retreat = a.u16(0x3A);
	if (retreat != 0 || a.u16(0x21E) > 0) {
		const SavedTown& st = save.towns[a.u8(0x5D3)];
		const TownMemory& t = st.memory;
		const uint8_t side = fn_5252f0(save, a.u8(0x5D3));
		if (side == 0xFF) {
			a.u16(0x8) = 0xFFFF;
		} else {
			// Tres puertas seguidas del lado `side` (dueño 0xFF − side) desde
			// la lista [0x768], siguiendo por la [0x762] al acabarse.
			bool wrapped = false;
			a.u32(0x244) = t.u16(0x768);
			const BuildingRecord* r = nullptr;
			for (size_t k = 0; k < 3; ++k) {
				uint32_t& current = a.u32(0x244 + 4 * k);
				while (current < t.u16(0x776)) {
					r = &record(st, current);
					if ((wall_gate(r->type) || wall_gate_other_side(r->type)) && r->owner == 0xFF - side) {
						break;
					}
					if (!wrapped && r->next >= t.u16(0x776)) {
						wrapped = true;
						current = t.u16(0x762);
					} else {
						current = r->next;
					}
				}
				if (k == 2) {
					break;
				}
				// El juego lee el siguiente del último registro mirado (en la
				// primera puerta, de la dirección 0 si no ha mirado ninguno).
				static const BuildingRecord kNone{};
				const BuildingRecord& last = r ? *r : kNone;
				uint32_t next;
				if (!wrapped && last.next >= t.u16(0x776)) {
					wrapped = true;
					next = t.u16(0x762);
				} else {
					next = last.next;
				}
				a.u32(0x248 + 4 * k) = next;
			}
			fn_632780(save, town);
		}
		fn_62b2b0(save, town);
		fn_62e810(save, town);
	}
	if (a.u16(0x3C) == 0 && a.u16(0x3A) == 0) {
		a.u8(0x78) = 0;
		a.u8(0x79) = 0;
		a.u8(0x578) = 0xFF;
		return;
	}
	a.u8(0x78) = 1;
	const SiegeMemory& s = save.sieges[a.u8(0x5D3)];
	a.u8(0x79) = a.u16(0x3C) != 0 || s.u32(0x0) + 0x1E00 > save.globals.u32(kTick) ? 1 : 0;
	a.u8(0x578) = 0xFF;
}

}  // namespace p3::data::tick
