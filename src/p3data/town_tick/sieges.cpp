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
// No se traducen los mensajes (0x51A850, 0x4D6530, 0x51A880) ni lo de los
// gráficos (0x6E51AC, [+0x77], los objetos [+0x1F8], [+0x574], [+0x5DC]...):
// no cambian la simulación.

#include <cstring>

#include "p3data/town_tick/internal.hpp"

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
// Cola de eventos con fecha (objeto 0x6DD73C; SaveGame::events).
constexpr uint32_t kEventHead = 0x6DD73C + 0x8;  // u16 primero de la lista, en orden de tick
constexpr uint32_t kEventFree = 0x6DD73C + 0xA;  // u16 primer registro libre
constexpr uint32_t kEventCount = 0x6DD73C + 0xC;  // u16 registros reservados

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

// 0x4D8DD0 (cola de eventos): lleva el evento de la cabeza a su sitio en la
// lista, detrás de los que tienen un tick menor o igual. Copia de la de
// convoys_sea.cpp (allí, en un namespace anónimo).
void fn_4d8dd0(SaveGame& save) {
	GlobalMemory& g = save.globals;
	const uint16_t count = g.u16(kEventCount);
	const uint16_t first = g.u16(kEventHead);
	GameMemory<0x18>& e = save.events[first];
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

// 0x4D8CF0 (cola de eventos): toma un registro libre (si no queda ninguno,
// reserva 0x80 más), lo pone en la lista con el tick `tick` y lo lleva a su
// sitio. Devuelve su índice. Copia de la de convoys_sea.cpp.
uint16_t fn_4d8cf0(SaveGame& save, uint32_t tick) {
	GlobalMemory& g = save.globals;
	const uint16_t free = g.u16(kEventFree);
	if (free >= g.u16(kEventCount)) {
		const uint16_t count = uint16_t(g.u16(kEventCount) + 0x80);
		g.u16(kEventCount) = count;
		std::vector<GameMemory<0x18>> events(count);
		for (size_t i = 0; i < free && i < save.events.size() && i < events.size(); ++i) {
			events[i] = save.events[i];
		}
		save.events = std::move(events);
		for (uint16_t i = free; i < g.u16(kEventCount);) {
			GameMemory<0x18>& e = save.events[i];
			e.u16(4) = ++i;
		}
	}
	const uint16_t index = g.u16(kEventFree);
	GameMemory<0x18>& e = save.events[index];
	g.u16(kEventFree) = e.u16(4);
	e.u16(4) = g.u16(kEventHead);
	g.u16(kEventHead) = index;
	e.u32(0) = tick;
	fn_4d8dd0(save);
	return index;
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
	// Sin comprobar con el oráculo: solo lo llama el lector y en las partidas
	// [+0x242] siempre es 0. realloc a [+0x242] entradas; el juego no comprueba que quepan las que
	// encuentra. El reloj *0x6DCCF8 (de los gráficos, fuera de los globales)
	// vale 0 en el oráculo y aquí.
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
			++a.u8(0x6C);  // Sin comprobar con el oráculo
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
			t.s32(o) = 1;  // Sin comprobar con el oráculo
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
// algo; entonces [+0x0] pasa a ser hoy. (Con convoy, desde la interfaz
// 0x5FEAC9, las mercancías van a sus barcos: no se traduce.)
bool fn_633db0(SaveGame& save, int siege, uint32_t& amount, uint16_t& offices) {
	SiegeMemory& s = save.sieges[size_t(siege)];
	GlobalMemory& g = save.globals;
	const uint32_t today = g.u32(kTick) >> 8;
	if (today <= (s.u32(0x0) >> 8) + 0xE) {
		amount = 0;
		return false;
	}
	const int town = s.u8(0x4);
	TownMemory& t = save.towns[size_t(town)].memory;
	const uint32_t limit = (t.u32(0x2E0) + (t.u32(0x2DC) + t.u32(0x2D8) * 2) * 2) << 4;
	const int32_t money = t.s32(0x700);
	amount = money > 0 ? uint32_t(money) : 0;
	uint32_t capacity = 20000;  // Sin convoy.
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
		if (amount + paid < limit && save.merchants[office.u16(0x2C4)].memory.u16(0x8) == 0) {
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
				}
				take -= take % 200;
				if (take == 0) {
					continue;
				}
				capacity -= take;
				office.u32(0x4 + 4 * size_t(w)) -= take;
				const float taken = float(int64_t(take));
				paid += uint32_t(ftol(double(taken) * double(ware_value(w))));
			}
		}
		o = office.u16(0x2CA);
		++offices;
	}
	if (amount + paid < limit) {
		for (uint8_t w : order) {
			uint32_t take = t.u32(0x4 + 4 * size_t(w));
			if (take == 0 || !kBarrelWare[w]) {
				continue;
			}
			const float stock = float(int64_t(take));
			const uint32_t value = uint32_t(ftol(double(stock) * double(ware_value(w))));
			if (value + amount + paid > limit) {
				if (limit <= amount + paid) {
					take = 0;  // Sin comprobar con el oráculo
				} else {
					const float rest = float(int64_t(limit - amount - paid));
					take = uint32_t(ftol(double(rest) / double(ware_value(w))));
				}
			}
			if (take > capacity) {
				take = capacity;
			}
			take -= take % 200;
			if (take == 0) {
				continue;
			}
			t.u32(0x4 + 4 * size_t(w)) -= take;
			capacity -= take;
			const float taken = float(int64_t(take));
			paid += uint32_t(ftol(double(taken) * double(ware_value(w))));
		}
	}
	if (amount + paid == 0) {
		return false;  // Sin comprobar con el oráculo
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
		GameMemory<0x18>& e = save.events[fn_4d8cf0(save, g.u32(kTick) + 1)];
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
			// Mensaje a los comerciantes (0x4D6530): no se traduce.
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
					extra = 0;  // Sin comprobar con el oráculo
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
					a.u32(0x4) = ammo - 0x1B;  // Sin comprobar con el oráculo
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
		GameMemory<0x18>& e = save.events[fn_4d8cf0(save, g.u32(kTick) + 1)];
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
			// Mensaje a los comerciantes (0x4D6530): no se traduce.
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
			// Sin comprobar con el oráculo
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
				best[k] = 0xFF;  // Sin comprobar con el oráculo
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
						// (El tope 0x4FFF, sin comprobar con el oráculo.)
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
				// Mensaje al comerciante (0x4D6530): no se traduce.
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

}  // namespace sieges_detail

using namespace sieges_detail;

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
