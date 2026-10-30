// Lo que hace advance_time al empezar el día (lo diario, 0x5315C0, y las
// tareas programadas, 0x535450) y los 15 objetos del mundo (0x52D7D0).
//
// Campos que se usan aquí:
//   Ciudad: [+0x2C0] u8 índice, [+0x2C6] u16 lo que cuesta al día cada
//     edificio o puesto de una oficina (0x500820), [+0x2C8] u32 situación
//     (flags), [+0x2D4] u32 habitantes, [+0x2D8], [+0x2DC], [+0x2E0] u32
//     habitantes de tres clases (pagan 3, 2 y 1 de impuesto por cabeza),
//     [+0x6F1] u8 alcalde (comerciante), [+0x6F8] u8 impuesto por cabeza,
//     [+0x704 + 4·k] (k < 11) movimientos de dinero del mes (0x529F90) y
//     [+0x730 + 4·k] los del mes anterior, [+0x784] u16 primera oficina.
//   Oficina: [+0x0] u16 (se guarda en [+0x2] cada semana y vuelve a 0),
//     [+0x2C4] u16 dueño, [+0x2C6] u8 ciudad, [+0x2C8] u16 siguiente oficina
//     del comerciante, [+0x2CA] u16 siguiente oficina de la ciudad, [+0x2D2]
//     u16 edificios terminados, [+0x2D4] u16 (pagan el coste diario como los
//     edificios), y siete totales de la semana que se guardan en el u32
//     siguiente y vuelven a 0: [+0x418] (gastos de los grupos), [+0x420]
//     (administrador), [+0x428] (salarios), [+0x430] (mantenimiento),
//     [+0x438] (coste diario de edificios y puestos, 0x500820: la mitad va a
//     la ciudad como ingreso de tipo 0), [+0x440].
//   Comerciante: [+0x8] u16 tipo (0 = jugador humano), [+0xC] u16 primera
//     oficina, [+0x120 + 12·i] float segunda componente local de la
//     reputación en la ciudad i (merchants.cpp), [+0x4A4], [+0x4A8] gastos
//     (suman el coste diario de las oficinas), [+0x4D4] la parte de [+0x4A8]
//     de los puestos ([+0x2D4] de la oficina).
//   Objeto 0x6DE588 ([+0xE8] del mundo): [+0x40·c + 4·d] u32 la situación
//     ([+0x2C8]) de la ciudad c el día d (tick / 256 mod 16), sin el bit
//     0x20000 si el campo (situación & 0x7C0000) >> 18 es >= 0x18; 8 huecos,
//     uno por jugador humano, con su comerciante en el u8 [+0xA00 + i] (0xFF
//     libre) y un byte por ciudad en *[+0xA08 + 4·i] (SaveGame::player_towns).
//   Los 15 objetos de 0x2C bytes del mundo en [+0xB10] (0x6DEFB0): u8 [+0x5]
//     tipo (0..2), u32 [+0xC] y [+0x10] que suben cada 15 días 200 ×
//     raíz(habitantes de todas las ciudades) / (30, 40 o 50 según el tipo)
//     hasta 100000, 200000 o 0, y u32 [+0x14] y [+0x18] que bajan lo mismo
//     hasta 0; u16 [+0x1C] y la lista del montón de [+0x24] (con su tamaño en
//     [+0x28]; SaveGame::world_object_lists): encargos de 16 bytes con un
//     u32 [+0x4] (lo que falta) y un tick de caducidad u32 [+0xC] (los añade
//     0x52C6C0, que llama la cola de eventos desde 0x4E6F32).

#include <cmath>
#include <cstring>
#include <vector>

#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

namespace ship_ai_detail {
// ship_ai.cpp: 0x535630 sobre el registro de un hueco del objeto 0x6DE588.
void fn_535630(SaveGame& save, std::vector<uint8_t>& towns);
}  // namespace ship_ai_detail

namespace {

constexpr uint32_t kDayOfMonth = global::kWorldDay;  // u8 (1..31)
constexpr uint32_t kOfficeCount = global::kWorldOfficeCount;  // u16
constexpr uint32_t kMerchantCount = global::kWorldMerchantCount;  // u16
constexpr uint32_t kTownCount = global::kWorldTownCount;  // u32
constexpr uint32_t kTownHistory = 0x6DE588;  // [+0xE8] del mundo: u32 [+0x40·ciudad + 4·día]
constexpr uint32_t kSlotMerchant = 0x6DE588 + 0xA00;  // u8 × 8: comerciante de cada hueco (0xFF libre)
constexpr uint32_t kSlotPointer = 0x6DE588 + 0xA08;  // u32 × 8: punteros del juego (SaveGame::player_towns)
constexpr uint32_t kWorldObjects = global::kWorld + 0xB10;  // 15 objetos de 0x2C bytes
constexpr uint32_t kWorldObjectSize = 0x2C;

// 0x66DCD8 y 0x672F30 (float).
constexpr float k10 = 0x1.4p+3f;  // 10,0
constexpr float k1e_4 = 0x1.a36e2ep-14f;  // 0,0001

// 0x6739C0, 0x6739C8, 0x6739D0 (double): divisores de 0x52D7D0 por tipo.
constexpr double k30 = 30.0;
constexpr double k40 = 40.0;
constexpr double k50 = 50.0;

// 0x673990 (s32 por tipo de objeto): tope de [+0xC] y [+0x10] en 0x52D7D0.
// Los tipos que existen son 0..2; con otro el juego lee lo que sigue en
// .rdata (aquí hasta el 11; después empiezan los double de 0x6739C0).
constexpr uint32_t kObjectCap[12] = {0x186a0, 0x30d40, 0x0, 0x3f800000, 0x40000000, 0x3f666666, 0x3fd9999a,
	0x3fc00000, 0x40400000, 0xf6, 0x3f4ccccd, 0x0};

uint32_t town_count(const SaveGame& save) {
	return save.globals.u32(kTownCount);
}

}  // namespace

namespace daily_detail {

// 0x4F9220 (sobre el comerciante): baja la segunda componente local de su
// reputación en la ciudad `town` en amount / 10000 (10 si amount >= 1000000).
void fn_4f9220(SaveGame& save, int merchant, uint32_t town, uint32_t amount) {
	// No sale en las partidas (ningún alcalde es un comerciante): comprobado
	// con el oráculo con estados retocados.
	float& reputation = save.merchants[size_t(merchant)].memory.f32(0x120 + 12 * size_t(town));
	if (amount >= 1000000) {
		reputation = float(double(reputation) - double(k10));
		return;
	}
	const float a = float(double(amount));  // fild QWORD; fstp DWORD
	reputation = float(double(reputation) - double(a) * double(k1e_4));
}

// 0x52A020 (sobre la ciudad, el día 1 de cada mes): si el alcalde es un
// comerciante y los gastos de tipo 9 menos los ingresos de tipo 3 de este
// mes y del anterior son positivos, pierde reputación en la ciudad
// (0x4F9220) por la mitad; después, los movimientos del mes pasan a los del
// mes anterior y vuelven a 0.
void fn_52a020(SaveGame& save, int town) {
	TownMemory& t = save.towns[size_t(town)].memory;
	const uint8_t mayor = t.u8(0x6F1);
	if (uint16_t(mayor) < save.globals.u16(kMerchantCount)) {
		// No sale en las partidas ([0x6F1] >= nº de comerciantes en todas las
		// ciudades): comprobado con el oráculo con estados retocados.
		const uint32_t balance = t.u32(0x754) - t.u32(0x73C) - t.u32(0x710) + t.u32(0x728);
		if (int32_t(balance) > 0) {
			fn_4f9220(save, mayor, t.u8(0x2C0), balance >> 1);
		}
	}
	for (uint32_t k = 0; k < 11; ++k) {
		t.u32(0x730 + 4 * k) = t.u32(0x704 + 4 * k);
		t.u32(0x704 + 4 * k) = 0;
	}
}

// 0x500820 (sobre la oficina, cada día): el comerciante paga por cada
// edificio terminado y cada puesto de la oficina, más uno, lo que diga
// [+0x2C6] de la ciudad; se apunta en sus gastos y en [+0x438] de la oficina.
void fn_500820(SaveGame& save, int office) {
	OfficeMemory& o = save.offices[size_t(office)].memory;
	MerchantMemory& m = save.merchants[o.u16(0x2C4)].memory;
	const uint32_t rate = save.towns[o.u8(0x2C6)].memory.u16(0x2C6);
	const uint32_t cost = (uint32_t(o.u16(0x2D2)) + uint32_t(o.u16(0x2D4)) + 1) * rate;
	m.u32(0x4A4) += cost;
	m.u32(0x4A8) += cost;
	o.u32(0x438) += cost;
	m.u32(0x4D4) += uint32_t(o.u16(0x2D4)) * rate;
}

// 0x5006F0 (sobre la oficina, cada semana): los totales de la semana pasan a
// los de la semana anterior y vuelven a 0; la mitad del coste diario de la
// semana ([+0x438]) es un ingreso de la ciudad (tipo 0).
void fn_5006f0(SaveGame& save, int office) {
	OfficeMemory& o = save.offices[size_t(office)].memory;
	o.u32(0x41C) = o.u32(0x418);
	o.u32(0x42C) = o.u32(0x428);
	o.u32(0x434) = o.u32(0x430);
	o.u16(0x2) = o.u16(0x0);
	const int32_t cost = o.s32(0x438);
	o.s32(0x43C) = cost;
	o.u32(0x424) = o.u32(0x420);
	o.u32(0x444) = o.u32(0x440);
	const int32_t half = cost / 2;  // cdq; sub; sar: hacia cero
	o.s32(0x438) = half;
	if (half > 0) {
		fn_529f90(save, o.u8(0x2C6), half, 0);
	}
	o.u32(0x418) = 0;
	o.u32(0x420) = 0;
	o.u32(0x428) = 0;
	o.u32(0x430) = 0;
	o.u32(0x438) = 0;
	o.u32(0x440) = 0;
	o.u16(0x0) = 0;
}

}  // namespace daily_detail

using namespace daily_detail;

// 0x5315C0 (sobre el mundo, al empezar el día): el día 1 de cada mes, el
// cierre del mes de cada ciudad (0x52A020); cada día, el coste de las
// oficinas de cada ciudad (0x500820) y, cada semana, su cierre de la semana
// (0x5006F0) y el impuesto por cabeza de la ciudad (tipo 1).
void fn_5315c0(SaveGame& save, bool weekly) {
	GlobalMemory& g = save.globals;
	if (g.u8(kDayOfMonth) == 1) {
		for (uint32_t i = 0; i < town_count(save); ++i) {
			fn_52a020(save, int(i));
		}
	}
	for (uint32_t i = 0; i < town_count(save); ++i) {
		TownMemory& t = save.towns[i].memory;
		for (uint32_t o = t.u16(0x784); o < g.u16(kOfficeCount); o = save.offices[o].memory.u16(0x2CA)) {
			fn_500820(save, int(o));
			if (weekly) {
				fn_5006f0(save, int(o));
			}
		}
		if (weekly) {
			const uint32_t people = t.u32(0x2D8) * 3 + t.u32(0x2DC) * 2 + t.u32(0x2E0);
			const uint32_t tax = people * uint32_t(t.u8(0x6F8)) * 10 / 100;
			fn_529f90(save, int(i), int32_t(tax), 1);
		}
	}
}

// 0x535450 (sobre el objeto 0x6DE588, al empezar el día): guarda la
// situación de cada ciudad en el día tick / 256 mod 16 de su historia y
// actualiza los huecos de los jugadores humanos: en el de cada uno, cada
// ciudad se aleja un día salvo las que tienen una oficina suya, que vuelven
// a 0, y se recalcula (0x535630); los huecos de los que ya no son humanos se
// liberan y los humanos sin hueco reciben uno libre, con 15 en cada ciudad.
void fn_535450(SaveGame& save, uint32_t tick) {
	GlobalMemory& g = save.globals;
	const uint32_t day = uint32_t(int32_t(tick) >> 8) & 0xF;
	for (uint32_t i = 0; i < town_count(save); ++i) {
		const uint32_t flags = save.towns[i].memory.u32(0x2C8);
		uint32_t& history = g.u32(kTownHistory + 4 * day + 0x40 * i);
		history = flags;
		if (int32_t(flags & 0x7C0000) >= 0x600000) {
			history = flags & 0xFFFDFFFF;
		}
	}
	for (uint32_t k = 0; k < 8; ++k) {
		const uint8_t merchant = g.u8(kSlotMerchant + k);
		if (uint16_t(merchant) >= g.u16(kMerchantCount)) {
			continue;
		}
		std::vector<uint8_t>& towns = save.player_towns[k];
		if (save.merchants[merchant].memory.u16(0x8) == 0) {
			for (uint32_t c = 0; c < town_count(save); ++c) {
				++towns[c];
			}
			// 0x530400 da 0 (fin) con un índice >= nº de oficinas.
			for (uint32_t o = save.merchants[merchant].memory.u16(0xC); o < g.u16(kOfficeCount);
				o = save.offices[o].memory.u16(0x2C8)) {
				towns[save.offices[o].memory.u8(0x2C6)] = 0;
			}
			ship_ai_detail::fn_535630(save, towns);
		} else {
			// No sale en las partidas: comprobado con el oráculo con estados retocados.
			g.u8(kSlotMerchant + k) = 0xFF;
			towns.clear();  // operator delete
			g.u32(kSlotPointer + 4 * k) = 0;
		}
	}
	for (uint32_t b = 0; b < g.u16(kMerchantCount); ++b) {
		if (save.merchants[b].memory.u16(0x8) != 0) {
			continue;
		}
		uint32_t k = 0;
		while (k < 8 && g.u8(kSlotMerchant + k) != b) {
			++k;
		}
		if (k < 8) {
			continue;
		}
		const uint16_t count = g.u16(kMerchantCount);
		uint32_t free = 0;
		while (free < 8 && uint16_t(g.u8(kSlotMerchant + free)) < count) {
			++free;
		}
		if (free == 8) {
			continue;  // Sin huecos libres: no sale en las partidas (comprobado con estados retocados).
		}
		// operator new(0x28) y memset a 0x0F. El puntero del juego
		// ([kSlotPointer + 4·free]) no se reproduce: el registro es
		// SaveGame::player_towns.
		save.player_towns[free].assign(0x28, 0x0F);
		g.u8(kSlotMerchant + free) = uint8_t(b);
	}
}

// 0x52D7D0 (sobre el objeto `object` de los 15 del mundo, cada 15 días): sus
// [+0xC] y [+0x10] suben, y [+0x14] y [+0x18] bajan, 200 × raíz(habitantes
// de todas las ciudades) / (30, 40 o 50 según el tipo [+0x5]); después quita
// de su lista los registros caducados.
void fn_52d7d0(SaveGame& save, int object) {
	GlobalMemory& g = save.globals;
	const uint32_t base = kWorldObjects + kWorldObjectSize * uint32_t(object);
	uint32_t people = 0;
	for (uint32_t i = 0, n = town_count(save); i < n; ++i) {
		people += save.towns[i].memory.u32(0x2D4);
	}
	const uint8_t type = g.u8(base + 0x5);
	uint32_t growth = 0;
	switch (type) {
	case 0:
		growth = uint32_t(ftol(std::sqrt(double(people)) / k30));
		break;
	case 1:
		growth = uint32_t(ftol(std::sqrt(double(people)) / k40));
		break;
	case 2:
		growth = uint32_t(ftol(std::sqrt(double(people)) / k50));
		break;
	default:
		growth = 0;  // No sale en las partidas: comprobado con estados retocados (tipos 3..11).
		break;
	}
	const uint32_t step = growth * 200;
	for (uint32_t k = 0; k < 2; ++k) {
		int32_t& up = g.s32(base + 0xC + 4 * k);
		up = int32_t(uint32_t(up) + step);
		if (type < 12) {
			const int32_t cap = int32_t(kObjectCap[type]);
			if (up > cap) {
				up = cap;
			}
		}
		// Con un tipo >= 12 (no existe) el juego compara con otros datos de
		// .rdata: no se reproduce.
		int32_t& down = g.s32(base + 0x14 + 4 * k);
		down = int32_t(uint32_t(down) - step);
		if (down < 0) {
			down = 0;
		}
	}
	// Después quita de su lista (SaveGame::world_object_lists; [+0x28] y el
	// u16 [+0x1C] bajan 1, y avisa a la interfaz con 0x5C7810, que no se
	// traduce) los encargos con el tick de caducidad [+0xC] < tick del mundo
	// o sin nada que entregar ([+0x4] == 0). Las llena la llegada de una
	// expedición (0x52C6C0, events_rare.cpp); vacías en las 12 partidas.
	std::vector<GameMemory<0x10>>& list = save.world_object_lists[size_t(object)];
	const uint32_t tick = g.u32(global::kWorldTick);
	for (size_t i = 0; i < list.size();) {
		if (list[i].u32(0xC) < tick || list[i].u32(0x4) == 0) {
			list.erase(list.begin() + std::ptrdiff_t(i));
			--g.u32(base + 0x28);
			--g.u16(base + 0x1C);
		} else {
			++i;
		}
	}
}

}  // namespace p3::data::tick
