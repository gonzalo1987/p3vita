// Satisfacción y población de la ciudad: satisfacción de los habitantes
// (0x51C830), mendigos que encuentran trabajo (0x51CBE0), ascensos, descensos
// y emigración de los pobres (0x51C650), reparto de las viviendas (0x520BD0) y
// llegada y marcha de mendigos (0x51C0E0).
//
// Campos de la ciudad que se usan aquí (además de los de game_memory.hpp):
//   [0x2C8] u32  situación: 0x8 peste, 0x10 asedio, 0x200 bloqueo, 0x400 boicot,
//                0x800 piratas, 0x1000 hambruna, 0x800000 asedio rechazado (más mendigos).
//   [0x2D0] u32  tick de la última fiesta (la escribe st_celebration, 0x4E23A4).
//   [0x2D4] u32  habitantes: suma de las cuatro clases [0x2D8].
//   [0x2D8] 4×s32 habitantes por clase: ricos, acomodados, pobres, mendigos.
//   [0x2F8] 3×u16 plazas de vivienda por clase (casas de la ciudad y de los comerciantes).
//   [0x2FE] u16  se pone a 0 al recontar las viviendas.
//   [0x300] 4×s16 satisfacción por clase (-40..80); [0x308] 4×s16 la que «debería» tener (/2).
//   [0x770] u8   capillas (0x2C) de la lista [0x762] más las de [0x764]; [0x771] solo las de [0x764].
//   [0x772] u8   hospitales (0x29), igual; [0x773] solo los de [0x764].
//   [0x778] 3×u16 ocupación de las casas de la ciudad; [0x77E] 3×u16 sus plazas.
//   [0x786] u16  defensa: torres 0x32/0x33 y muralla 0x36 suman 1, puertas 3 (0 = por contar).
//   [0x788] u8   torres (0x30..0x33).
//   [0x789] u8   pozos (0x28).
//   [0x78C] u16  edificios de tipo < 0x15 junto a la calle (+1); [0x78E] todos (+1).
//   [0x76C] u32  flags: 0x100 mercado, 0x200 sin límite de habitantes (todas las
//                ciudades de las partidas), 0x400 casa de la moneda, 0x1000 llegan más
//                mendigos; 0x2, 0x10, 0x20 y 0x80 cambian el límite (ver 0x51C0E0).
//   [0x994] u16  se compara con el número de barcos (0x6DD894): si es menor, +3 torres.
//   [0x998] u8   suma a la satisfacción como los pozos; [0x999..0x99B] u8, frente a la defensa.
//   [0x9A4] 8×u16 cambio diario del número de mendigos, por (tick >> 8) & 7.
//   [0x844 + 16·i] u16 trabajadores de la instalación i (0 milicia; la 9, granja de
//                cereales, conserva 60 al despedir y las demás 30); [0x84A + 16·i] u16
//                (field_A de P3Modding) los primeros que se despiden.
// De la oficina: [+0x2CC] primera empresa del comerciante en la ciudad (registros
// de 0x14 del mundo: [+4] u16 trabajadores, [+7] u8 ciudad, [+8] u16 siguiente),
// [+0x2CE] primera de sus fortificaciones y [+0x2D0] primera de sus casas (registros
// de edificio de la ciudad), [+0x2D4] u16 casas, [+0x2D8] 3×u16 plazas,
// [+0x2DE] 3×u16 ocupación y [+0x2E4] 3×u8 nivel de alquiler (0..4) por clase.

#include <cmath>

#include "p3data/game_fixes.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

namespace {

constexpr uint32_t kWorldRecordCount = 0x6DE4A6;  // u16 registros de 0x14 del mundo.
// Puestos de trabajo que ofrecen las instalaciones y las empresas en el tick de
// ciudad (0x4D4A40 los apunta; el tick lo vacía antes): s32 n, s16 quién [+4]
// (instalación 0..20 o ~registro del mundo) y u16 cuántos [+0x82].
constexpr uint32_t kJobOffers = 0x6DD628;

// 0x6736A0: satisfacción de base por nivel de necesidades (ricos, acomodados,
// pobres, relleno) y desde 0x6736AC, lo que sube y baja por día (subida, bajada).
// El nivel 3 lee como base los bytes de la tabla de pasos.
constexpr int8_t kSatisfactionTables[20] = {-7, -12, -20, 0, -13, -18, -27, 0, -20, -25, -32, 0, 3, 1, 2, 1, 1, 2, 1, 1};

// 0x672938: peso de cada mercancía en la satisfacción de ricos, acomodados y pobres.
constexpr uint8_t kWareWeights[3][24] = {
	{2, 5, 2, 2, 2, 3, 3, 5, 5, 3, 3, 3, 2, 2, 2, 0, 0, 0, 3, 0, 0, 0, 0, 0},
	{4, 4, 6, 6, 2, 2, 0, 2, 4, 2, 4, 4, 2, 2, 6, 0, 0, 0, 2, 0, 0, 0, 0, 0},
	{8, 4, 6, 6, 4, 0, 0, 0, 0, 0, 4, 6, 0, 4, 4, 0, 0, 0, 4, 0, 0, 0, 0, 0},
};

constexpr uint16_t kHouseCapacity[3] = {80, 140, 280};  // 0x672A18: plazas de una casa de cada clase.
// 0x672A20: ocupación a partir de la que una casa pasa al nivel siguiente.
constexpr uint16_t kHouseLevels[9] = {20, 50, 80, 35, 100, 140, 70, 200, 280};
constexpr uint8_t kHouseType[4] = {0x15, 0x18, 0x1B, 0x1E};  // Tipo de la casa de nivel 0 de cada clase.

// sqrt de la CRT (0x63AAE4) de un entero y __ftol. Con un negativo el juego
// pasa por el error de la CRT y __ftol de NaN deja 0 en eax. Sin comprobar con
// el oráculo (no ocurre).
int32_t root(int32_t v) {
	if (v < 0) {
		return 0;
	}
	return int32_t(ftol(std::sqrt(double(v))));
}

}  // namespace

namespace population_detail {

// 0x524B70: recorre una lista de edificios de la ciudad desde `index` y cuenta
// los de tipo < 0x15 (salvo el 5) junto a la calle ([0x78C]) y en total
// ([0x78E]), las torres ([0x788]) y la defensa ([0x786]).
void fn_524b70(SaveGame& save, int town, uint32_t index) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	const uint32_t count = t.u16(0x776);
	while (index < count) {
		const BuildingRecord& r = st.records[index];
		const uint8_t type = r.type;
		if (type == 5) {
		} else if (type < 0x15) {
			// La rejilla va por columnas: x · alto + y. Las celdas >= 0x80 son calle.
			if (st.grid[t.u32(0x7AC) * r.x + r.y] >= 0x80) {
				++t.u16(0x78C);
			}
			++t.u16(0x78E);
		} else if (type < 0x30) {
		} else if (type == 0x30 || type == 0x31) {
			++t.u8(0x788);
		} else if (type == 0x32 || type == 0x33) {
			++t.u8(0x788);
			++t.u16(0x786);
		} else if (type == 0x36) {
			++t.u16(0x786);
		} else {
			t.u16(0x786) += 3;
		}
		index = r.next;
	}
}

// 0x524C30: vuelve a contar las fortificaciones de la ciudad ([0x768]) y de
// cada oficina ([+0x2CE]); la defensa [0x786] queda al menos en 1.
void fn_524c30(SaveGame& save, int town) {
	TownMemory& t = save.towns[size_t(town)].memory;
	t.u8(0x788) = 0;
	t.u16(0x78C) = 1;
	t.u16(0x78E) = 1;
	t.u16(0x786) = 0;
	fn_524b70(save, town, t.u16(0x768));
	uint32_t office = t.u16(0x784);
	while (office < save.globals.u16(global::kWorldOfficeCount)) {
		const OfficeMemory& o = save.offices[office].memory;
		office = o.u16(0x2CA);  // Siguiente oficina de la ciudad.
		fn_524b70(save, town, o.u16(0x2CE));
	}
	if (t.u16(0x786) == 0) {
		t.u16(0x786) = 1;
	}
}

// 0x4D4A90: suma de los puestos ([+0x82 + 2·k]) de una lista como la de 0x6DD628.
uint32_t fn_4d4a90(const SaveGame& save, uint32_t list) {
	uint32_t sum = 0;
	for (int32_t k = save.globals.s32(list) - 1; k >= 0; --k) {
		sum += save.globals.u16(list + 0x82 + 2 * uint32_t(k));
	}
	return sum;
}

// 0x51C360: despide hasta `workers` trabajadores (en grupos de 4 habitantes)
// porque se van pobres de la ciudad: primero los que aún no han empezado en
// las instalaciones; después, en proporción, los que pasan del mínimo de cada
// instalación (30; 60 en la 9) y los de las empresas de los comerciantes.
// Devuelve cuántos ha despedido.
int32_t fn_51c360(SaveGame& save, int town, int32_t workers) {
	TownMemory& t = save.towns[size_t(town)].memory;
	const int32_t offices = save.globals.u16(global::kWorldOfficeCount);
	const int32_t records = save.globals.u16(kWorldRecordCount);
	int32_t fired = 0;
	int32_t excess = 0;
	for (uint32_t i = 1; i < 21; ++i) {
		const size_t f = 0x840 + 16 * i;
		const uint16_t starting = t.u16(f + 0xA);
		if (starting != 0) {
			if (int32_t(starting) >= workers) {
				t.u16(f + 0xA) = uint16_t(starting - workers);
				return workers + fired;
			}
			workers -= starting;
			fired += starting;
			t.u16(f + 0xA) = 0;
		}
		const uint16_t minimum = i == 9 ? 60 : 30;
		const uint16_t staff = t.u16(f + 4);
		if (staff > minimum) {
			excess = int32_t(uint32_t(excess) + staff - minimum);
		}
	}
	// Empresas de los comerciantes. El juego sigue después con el índice en el
	// que termina la lista de registros (y no con la siguiente oficina,
	// [+0x2CA]), así que en la práctica solo cuenta la primera oficina.
	// Game bug: with fix_game_bugs() it goes on with [+0x2CA], as the second
	// loop does, so every office's businesses count.
	int32_t office = t.u16(0x784);
	while (office < offices && workers >= 0) {
		const OfficeMemory& o = save.offices[size_t(office)].memory;
		int32_t record = o.u16(0x2CC);
		while (record < records) {
			const GameMemory<0x14>& r = save.world_records[size_t(record)];
			record = r.u16(8);
			excess = int32_t(uint32_t(excess) + r.u16(4));
		}
		office = fix_game_bugs() ? int32_t(o.u16(0x2CA)) : record;
	}
	int32_t ratio = excess / workers;
	if (ratio < 2) {
		ratio = 2;
	}
	int32_t sum = 0;
	for (uint32_t i = 1; i < 21; ++i) {
		const uint16_t minimum = i == 9 ? 60 : 30;
		uint16_t& staff = t.u16(0x844 + 16 * i);
		if (staff <= minimum) {
			continue;
		}
		const int32_t current = staff;
		sum += current - minimum;
		if (sum < ratio) {
			continue;
		}
		int32_t n = sum / ratio;
		if (workers < n) {
			n = workers;
		}
		if (n >= current) {
			// Unreachable: what is left of the sum is under the ratio (2 or more) and a
			// facility only adds its staff over the minimum (30 or 60), so sum / ratio never
			// reaches its staff.
			n = current - 1;
			staff = 1;
		} else {
			staff = uint16_t(staff - n);
		}
		workers -= n;
		sum = int32_t(uint32_t(sum) - uint32_t(n) * uint32_t(ratio));
		fired += n;
	}
	if (workers <= 0) {
		return fired;
	}
	office = t.u16(0x784);
	while (office < offices && workers > 0) {
		const OfficeMemory& o = save.offices[size_t(office)].memory;
		int32_t record = o.u16(0x2CC);
		while (record < records) {
			GameMemory<0x14>& r = save.world_records[size_t(record)];
			const uint16_t staff = r.u16(4);
			record = r.u16(8);
			if (staff == 0) {
				continue;
			}
			const int32_t current = staff;
			sum += current;
			if (sum < ratio) {
				continue;
			}
			int32_t n = sum / ratio;
			if (workers < n) {
				n = workers;
			}
			if (n > current) {
				// Unreachable: what is left of the sum is under the ratio (2 or more), so
				// sum / ratio never passes the staff just added.
				n = current;
				r.u16(4) = 0;
			} else {
				r.u16(4) = uint16_t(staff - n);
			}
			sum = int32_t(uint32_t(sum) - uint32_t(n) * uint32_t(ratio));
			workers -= n;
			fired += n;
		}
		office = o.u16(0x2CA);
	}
	return fired;
}

// 0x520840: reparte la ocupación `occupancy` (3 × u16, de la ciudad o de una
// oficina) entre las casas acabadas de la lista que empieza en `head` y pone
// a cada una el tipo del nivel que le toca (0x15 + 3·clase + nivel). Las que
// están en obras vuelven al nivel 0.
void fn_520840(SaveGame& save, int town, uint16_t* occupancy, uint32_t head) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	uint32_t houses[3] = {0, 0, 0};
	const uint32_t count = t.u16(0x776);
	for (uint32_t i = head; i < count; i = st.records[i].next) {
		const BuildingRecord& r = st.records[i];
		if (r.pending == 0) {
			const uint32_t k = (uint32_t(r.type) - 0x15) / 3;
			if (k < 3) {
				++houses[k];
			}
		}
	}
	// Ocupación de la primera casa y lo que sube de una a la siguiente: la
	// ocupación crece en progresión aritmética a lo largo de la lista.
	float start[3];
	float step[3];
	for (size_t k = 0; k < 3; ++k) {
		const uint32_t n = houses[k];
		houses[k] = 0;  // Desde aquí, el nivel de la última casa de la clase.
		if (n == 0) {
			occupancy[k] = 0;
			start[k] = 0;
			step[k] = 0;
			continue;
		}
		const uint32_t capacity = kHouseCapacity[k];
		const uint32_t total = n * capacity;
		const uint32_t occupied = occupancy[k];
		if (occupied >= total) {
			step[k] = 0;
			start[k] = float(capacity);
			continue;
		}
		const float occ = float(int32_t(occupied));
		const float houses_f = float(n);
		const float triangle = float((double(houses_f) + 1.0) * double(houses_f) * 0.5);
		const float capacity_f = float(capacity);
		const float half = float(double(capacity_f) * double(houses_f) * 0.5);
		if (half > occ) {
			start[k] = 0;
			step[k] = float(double(occ) / double(triangle));
		} else if (double(half) + double(half) <= double(occ)) {
			// Unreachable: 2·half is the total of places, exactly (no rounding at these
			// sizes), and a full class is handled above.
			start[k] = capacity_f;
			step[k] = 0;
		} else {
			const float total_f = float(total);
			const float first = float((double(occ) + double(occ) - double(total_f)) * double(capacity_f) / double(occ));
			start[k] = first;
			step[k] = float((double(occ) - double(first) * double(houses_f)) / double(triangle));
		}
		if (step[k] < 0.0f) {
			// Not reached: exactly the step is (total − occupancy)² / occupancy / triangle;
			// only float rounding could make it negative. Scenario housing (every town of saves
			// 3 and 5, each class 1, 2 and 3 places short of full: 144 calls equal) doesn't get
			// there.
			step[k] = 0;
		}
	}
	for (uint32_t i = head; i < uint32_t(t.u16(0x776)); i = st.records[i].next) {
		BuildingRecord& r = st.records[i];
		if (r.pending != 0) {
			if (r.type >= 0x15 && r.type < 0x1E) {
				r.type = uint8_t(((r.type - 0x15) / 3 + 7) * 3);
			}
			continue;
		}
		const uint32_t k = (uint32_t(r.type) - 0x15) / 3;
		if (k >= 3) {
			continue;
		}
		start[k] = float(double(start[k]) + double(step[k]));
		uint32_t& level = houses[k];
		while (start[k] > float(kHouseLevels[3 * k + level]) && level < 2) {
			++level;
		}
		// Si la ciudad es la que se está viendo (*0x6E51AC, [+0xC324]), el
		// juego solo cambia el tipo si es otro y además actualiza la escena
		// (0x590680, interfaz; no se traduce). El tipo queda igual.
		r.type = uint8_t(kHouseType[k] + level);
	}
}

}  // namespace population_detail

using namespace population_detail;

// 0x51C830 (update_citizen_satisfaction): mueve la satisfacción de ricos,
// acomodados y pobres un paso hacia el doble de la que les da el almacén de
// la ciudad, su situación y sus servicios. Sin mercado, todo a 40.
void fn_51c830(SaveGame& save, int town) {
	TownMemory& t = save.towns[size_t(town)].memory;
	// Ajuste «necesidades de los habitantes» (*0x6CC3E8, [0x17] & 3). Sin
	// ajustes el juego usa los de «normal»; aquí siempre los hay.
	const uint32_t needs = save.settings.u8(0x17) & 3;
	const uint16_t increment = uint8_t(kSatisfactionTables[12 + 2 * needs]);
	const uint16_t decrement = uint8_t(kSatisfactionTables[13 + 2 * needs]);
	if (!(t.u32(0x76C) & 0x100)) {
		for (size_t i = 0; i < 4; ++i) {
			t.s16(0x308 + 2 * i) = 40;
			t.s16(0x300 + 2 * i) = 40;
		}
		return;
	}
	// Lo que no depende de la clase. Solo cuentan sus 16 bits bajos.
	uint32_t modifiers;
	const int32_t citizens = t.s32(0x2D4);
	if (citizens < 1) {
		modifiers = 40;
	} else {
		const uint32_t n = uint32_t(citizens);
		uint32_t m = 0;
		const uint32_t situation = t.u32(0x2C8);
		if (situation & 0x10) {  // Asedio.
			m = uint32_t(-10);
		}
		if (situation & 0x800) {  // Piratas.
			m -= 8;
		}
		if (situation & 0x8) {  // Peste.
			m -= 10;
		}
		if (situation & 0x200) {  // Bloqueo.
			m -= 6;
		}
		if (situation & 0x400) {  // Boicot.
			m -= 4;
		}
		if (situation & 0x1000) {  // Hambruna.
			m -= 10;
		}
		auto add = [&m](uint32_t v) { m += v > 4 ? 4 : v; };
		add(uint32_t(t.u8(0x789)) * 2000 / n);  // Pozos.
		add((uint32_t(t.u8(0x773)) + t.u8(0x771)) * 20000 / n);  // Hospitales y capillas.
		if (t.u16(0x792) != 0) {
			add(uint32_t(t.u16(0x790)) * 4 / t.u16(0x792));  // Calles construidas / totales (0x4FA6F0).
		}
		// Semanas desde la última fiesta.
		const uint32_t weeks = (save.globals.u32(global::kWorldTick) - t.u32(0x2D0)) / (7 * 256);
		if (weeks < 4) {
			m += 4 - weeks;
		}
		add(uint32_t(t.u8(0x998)) * 2000 / n);
		add(uint32_t(int32_t(uint32_t(t.u16(0x78C)) * 4) / int32_t(t.u16(0x78E))));  // Edificios junto a la calle.
		uint32_t towers = t.u8(0x788);
		if (t.u16(0x994) < save.globals.u16(global::kShipCount)) {
			towers += 3;
		}
		m += towers / 3 > 3 ? 4 : towers / 3;
		if (t.u16(0x786) == 0) {
			fn_524c30(save, town);
		}
		const uint32_t guards = (uint32_t(t.u8(0x99B)) + t.u8(0x99A) + t.u8(0x999)) * 50 / t.u16(0x786);
		m += guards > 3 ? 4 : guards;
		const uint32_t tax = t.u8(0x6F8);  // Impuesto extra (%): resta su cuadrado / 20.
		modifiers = m + t.u8(0x7A3) - tax * tax / 20 + 9;  // [0x7A3]: nivel de la iglesia.
	}
	for (size_t c = 0; c < 3; ++c) {
		uint32_t s = uint32_t(int32_t(kSatisfactionTables[4 * needs + c]));
		for (size_t w = 0; w < 24; ++w) {
			if (t.s32(0x310 + 4 * w) <= 0) {  // Consumo diario de los habitantes.
				continue;
			}
			const int32_t stock = t.s32(4 + 4 * w);
			const int32_t threshold = t.s32(0x4F0 + 16 * w);  // Primer umbral de precio.
			const uint32_t weight = kWareWeights[c][w];
			if (stock < threshold) {
				s -= uint32_t(int32_t(weight * (uint32_t(threshold) - uint32_t(stock))) / threshold);
			} else if (stock < int32_t(uint32_t(threshold) * 2)) {
				s += uint32_t(int32_t(weight * (uint32_t(stock) - uint32_t(threshold))) / threshold);
			} else {
				s += weight;
			}
		}
		const int16_t target = int16_t(s + modifiers);
		const int16_t current = t.s16(0x300 + 2 * c);
		t.s16(0x308 + 2 * c) = target;
		if (2 * int32_t(target) > current) {
			const int16_t v = int16_t(current + increment);
			t.s16(0x300 + 2 * c) = v > 80 ? int16_t(80) : v;
		} else if (2 * int32_t(target) < current) {
			const int16_t v = int16_t(current - decrement);
			t.s16(0x300 + 2 * c) = v < -40 ? int16_t(-40) : v;
		}
	}
}

// 0x51CBE0: los mendigos que caben como pobres pasan a trabajar en los puestos
// que han ofrecido las instalaciones y las empresas en este tick (0x6DD628);
// cada trabajador son 4 habitantes.
void fn_51cbe0(SaveGame& save, int town) {
	TownMemory& t = save.towns[size_t(town)].memory;
	const int32_t beggars = t.s32(0x2E4);
	if (beggars <= 4) {
		return;
	}
	const int32_t poor = t.s32(0x2E0);
	uint32_t limit = uint32_t(beggars) - 1;
	const int32_t places = t.u16(0x2FC);  // Plazas para pobres.
	if (places <= int32_t(uint32_t(poor) + 3)) {
		return;
	}
	uint32_t room = uint32_t(places) - uint32_t(poor);
	if (limit > room) {
		limit = room;
	}
	if (!(t.u32(0x76C) & 0x200)) {
		room -= 40;
		if (room < 4) {
			return;
		}
		if (limit > room) {
			limit = room;
		}
	}
	limit >>= 2;
	const uint32_t offered = fn_4d4a90(save, kJobOffers);
	const uint32_t ratio = offered <= limit ? 1 : offered / limit;
	uint32_t sum = save.globals.u16(global::kWorldDayOfYear) & ratio;
	uint32_t hired = 0;
	for (uint32_t k = save.globals.u32(kJobOffers); k > 0;) {
		--k;
		const uint32_t wanted = save.globals.u16(kJobOffers + 0x82 + 2 * k);
		sum += wanted;
		if (sum < ratio) {
			continue;
		}
		uint32_t n = sum / ratio;
		if (n > limit) {
			n = limit;
		}
		if (n > wanted) {
			n = wanted;
		}
		const int16_t who = save.globals.s16(kJobOffers + 4 + 2 * k);
		uint16_t* staff;
		if (who < 0) {
			// Empresa de un comerciante: registro ~who del mundo, si es de esta ciudad.
			const uint32_t record = uint32_t(-1 - int32_t(who));
			if (record >= save.globals.u16(kWorldRecordCount)) {
				continue;  // Checked with the oracle: scenario job_offers (saves 3 and 5, a direct call).
			}
			GameMemory<0x14>& r = save.world_records[record];
			if (r.u8(7) != t.u8(0x2C0)) {
				continue;  // Checked with the oracle: scenario job_offers (saves 3 and 5, a direct call).
			}
			staff = &r.u16(4);
		} else {
			if (who >= 0x15) {
				continue;
			}
			staff = &t.u16(0x844 + 16 * size_t(who));
		}
		*staff = uint16_t(*staff + n);
		if (n != 0) {
			limit -= n;
			sum -= n * ratio;
			hired += n;
		}
	}
	t.u32(0x2E4) -= hired * 4;
	t.u32(0x2E0) += hired * 4;
}

// 0x51C650 (town_update_population_levels): pobres que ascienden a ricos y
// acomodados o que descienden de ellos según la satisfacción y las plazas, y
// pobres que se van (si están descontentos o no caben) y dejan su trabajo.
void fn_51c650(SaveGame& save, int town) {
	TownMemory& t = save.towns[size_t(town)].memory;
	int32_t target[2];
	const int32_t poor = t.s32(0x2E0);
	const int32_t rich = int32_t(uint32_t(t.s16(0x300) + 40) * uint32_t(poor));
	target[0] = (t.u32(0x76C) & 0x400) ? rich / 213 : rich / 320;  // 0x400: casa de la moneda.
	if (target[0] < 1) {
		target[0] = 1;
	}
	target[1] = int32_t(uint32_t(t.s16(0x302) + 40) * uint32_t(poor)) / 160;
	if (target[1] < 1) {
		target[1] = 1;
	}
	for (size_t k = 0; k < 2; ++k) {
		const uint16_t places = t.u16(0x2F8 + 2 * k);
		if (int32_t(places) < target[k]) {
			target[k] = places < 10 ? 10 : places;
		}
		const int32_t goal = target[k];
		int32_t& citizens = t.s32(0x2D8 + 4 * k);
		if (goal >= citizens) {
			const int32_t promoted = citizens != 0 ? int32_t(uint32_t(goal) * 2) / citizens + 1 : 1;
			if (t.s32(0x2E0) > promoted) {
				t.s32(0x2E0) -= promoted;
				citizens += promoted;
			}
		} else {
			int32_t demoted = int32_t(uint32_t(citizens) * 2) / goal + 1;
			if (demoted >= citizens) {
				demoted = citizens - 1;
			}
			t.s32(0x2E0) += demoted;
			citizens -= demoted;
		}
	}
	int32_t leaving = t.s16(0x304) < 0 ? t.s32(0x2E0) / 100 : 0;
	const int32_t now = t.s32(0x2E0);
	const int32_t homeless = int32_t(uint32_t(now) - t.u16(0x2FC));
	if (homeless > 0) {
		const int32_t n = (homeless >> 2) + 1;
		if (n > leaving) {
			leaving = n;
		}
	}
	if (leaving >= now) {
		leaving = now - 1;
	}
	if (leaving >= 4) {
		const int32_t fired = fn_51c360(save, town, leaving >> 2);
		t.u32(0x2E0) -= uint32_t(fired) * 4;
	}
	uint32_t total = 0;
	for (size_t i = 0; i < 4; ++i) {
		total += t.u32(0x2D8 + 4 * i);
	}
	t.u32(0x2D4) = total;
}

// 0x520BD0: recuenta los edificios de servicio y las casas de la ciudad y de
// los comerciantes, ajusta su ocupación, reparte entre ellas los habitantes de
// cada clase (más en las de alquiler bajo) y pone a cada casa su nivel.
void fn_520bd0(SaveGame& save, int town) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	// Plazas libres por clase y nivel de alquiler (5) y, a continuación, la
	// parte de los habitantes de más que va a cada nivel: en el juego son dos
	// tablas seguidas en la pila.
	int32_t frame[30] = {};
	int32_t* vacant = frame;
	int32_t* share = frame + 15;
	int32_t housed[3];
	int32_t surplus[3];
	auto cell = [&frame](uint32_t i) -> int32_t& { return frame[i < 30 ? i : 29]; };  // El juego usa niveles 0..4.

	t.u16(0x77E) = 0;
	t.u16(0x780) = 0;
	t.u16(0x782) = 0;
	t.u8(0x770) = 0;
	t.u8(0x771) = 0;
	t.u8(0x772) = 0;
	t.u8(0x773) = 0;
	t.u8(0x789) = 0;
	t.u8(0x788) = 0;
	t.u8(0x78A) = 0;
	t.u16(0x2FE) = 0;
	const int32_t count = t.u16(0x776);
	for (int32_t i = t.u16(0x762); i < count; i = st.records[size_t(i)].next) {
		switch (st.records[size_t(i)].type) {
		case 0x28:  // Pozo.
			++t.u8(0x789);
			break;
		case 0x29:  // Hospital.
			++t.u8(0x772);
			break;
		case 0x2C:  // Capilla.
			++t.u8(0x770);
			break;
		}
	}
	// Lista de edificios (salto por la tabla 0x52121C, tipos 0x15..0x2C).
	for (int32_t i = t.u16(0x764); i < count; i = st.records[size_t(i)].next) {
		const uint8_t type = st.records[size_t(i)].type;
		if (type >= 0x15 && type <= 0x17) {
			t.u16(0x77E) = uint16_t(t.u16(0x77E) + kHouseCapacity[0]);
		} else if (type >= 0x18 && type <= 0x1A) {
			t.u16(0x780) = uint16_t(t.u16(0x780) + kHouseCapacity[1]);
		} else if (type >= 0x1B && type <= 0x1D) {
			t.u16(0x782) = uint16_t(t.u16(0x782) + kHouseCapacity[2]);
		} else if (type == 0x28) {
			++t.u8(0x789);
		} else if (type == 0x29) {
			++t.u8(0x773);
			++t.u8(0x772);
		} else if (type == 0x2C) {
			++t.u8(0x771);
			++t.u8(0x770);
		}
	}
	// Casas de la ciudad: alquiler de nivel 2; si no están llenas pierden 4.
	for (size_t k = 0; k < 3; ++k) {
		const uint16_t places = t.u16(0x77E + 2 * k);
		uint16_t& occupied = t.u16(0x778 + 2 * k);
		if (places == 0) {
			occupied = 0;
		} else if (places <= occupied) {
			occupied = places;
		} else if (occupied > 4) {
			occupied = uint16_t(occupied - 4);
			vacant[5 * k + 2] = int32_t(places) - int32_t(occupied);
		} else {
			vacant[5 * k + 2] = places;
			occupied = 0;
		}
		t.u16(0x2F8 + 2 * k) = places;
		housed[k] = occupied;
	}
	// Casas de los comerciantes: se van tantos como el nivel del alquiler.
	const int32_t offices = save.globals.u16(global::kWorldOfficeCount);
	for (int32_t office = t.u16(0x784); office < offices;) {
		OfficeMemory& o = save.offices[size_t(office)].memory;
		office = o.u16(0x2CA);
		o.u32(0x2D8) = 0;
		o.u16(0x2DC) = 0;
		o.u16(0x2D4) = 0;
		for (int32_t i = o.u16(0x2D0); i < int32_t(t.u16(0x776)); i = st.records[size_t(i)].next) {
			// División con signo: los tipos 0x13 y 0x14 también dan 0.
			const int32_t k = (int32_t(st.records[size_t(i)].type) - 0x15) / 3;
			if (k >= 0 && k < 3) {
				o.u16(0x2D8 + 2 * size_t(k)) = uint16_t(o.u16(0x2D8 + 2 * size_t(k)) + kHouseCapacity[k]);
				++o.u16(0x2D4);
			}
		}
		for (size_t k = 0; k < 3; ++k) {
			const uint8_t rent = o.u8(0x2E4 + k);
			uint16_t& occupied = o.u16(0x2DE + 2 * k);
			occupied = rent >= occupied ? 0 : uint16_t(occupied - rent);
			const uint16_t places = o.u16(0x2D8 + 2 * k);
			if (occupied >= places) {
				occupied = places;
			} else {
				cell(uint32_t(5 * k + rent)) += int32_t(places) - int32_t(occupied);
			}
			housed[k] += occupied;
			t.u16(0x2F8 + 2 * k) = uint16_t(t.u16(0x2F8 + 2 * k) + places);
		}
	}
	// Habitantes que sobran (o faltan) en las casas y su reparto por niveles de
	// alquiler: con peso 5 para el más barato y 1 para el más caro.
	for (size_t k = 0; k < 3; ++k) {
		const int32_t diff = int32_t(t.u32(0x2D8 + 4 * k) - uint32_t(housed[k]));
		surplus[k] = diff;
		if (diff < 0) {
			for (size_t j = 0; j < 5; ++j) {
				share[5 * k + j] = diff;
			}
			continue;
		}
		uint32_t weight = 0;
		for (size_t j = 0; j < 5; ++j) {
			weight += uint32_t(vacant[5 * k + j]) * uint32_t(5 - j);
		}
		for (size_t j = 0; j < 5; ++j) {
			share[5 * k + j] = weight == 0 ? 1
				: int32_t(uint32_t(vacant[5 * k + j]) * uint32_t(5 - j) * uint32_t(diff)) / int32_t(weight);
		}
	}
	for (int32_t office = t.u16(0x784); office < offices;) {
		OfficeMemory& o = save.offices[size_t(office)].memory;
		office = o.u16(0x2CA);
		for (size_t k = 0; k < 3; ++k) {
			const uint8_t rent = o.u8(0x2E4 + k);
			const uint32_t slot = uint32_t(5 * k + rent);
			const int32_t part = cell(15 + slot);
			uint16_t& occupied = o.u16(0x2DE + 2 * k);
			if (part < 0) {
				if (occupied == 0) {
					continue;
				}
				// Se van en proporción a su ocupación, redondeando hacia arriba.
				const int32_t n = int32_t(uint32_t(housed[k]) - uint32_t(occupied) * uint32_t(part) - 1) / housed[k];
				if (-surplus[k] > n) {
					surplus[k] += n;
					occupied = uint16_t(occupied - n);
				} else {
					occupied = uint16_t(occupied + surplus[k]);
					surplus[k] = 0;
				}
			} else if (part > 0) {
				const uint16_t current = occupied;
				const int32_t room = int32_t(o.u16(0x2D8 + 2 * k)) - int32_t(current);
				if (room <= 0) {
					continue;
				}
				int32_t n = int32_t(uint32_t(part) * uint32_t(room)) / cell(slot);
				if (n >= room) {
					n = room;
				}
				if (n > surplus[k]) {
					// Unreachable: the truncated parts never add up to more than the surplus.
					occupied = uint16_t(surplus[k] + current);
					surplus[k] = 0;
				} else {
					occupied = uint16_t(current + n);
					surplus[k] -= n;
				}
			}
		}
		fn_520840(save, town, &o.u16(0x2DE), o.u16(0x2D0));
	}
	// Lo que queda va a las casas de la ciudad.
	for (size_t k = 0; k < 3; ++k) {
		uint16_t& occupied = t.u16(0x778 + 2 * k);
		const int32_t s = surplus[k];
		if (s > 0) {
			const uint16_t places = t.u16(0x77E + 2 * k);
			if (int32_t(places) - int32_t(occupied) < s) {
				surplus[k] = s - (int32_t(places) - int32_t(occupied));
				occupied = places;
			} else {
				occupied = uint16_t(s + occupied);
			}
		} else if (s < 0) {
			if (-s > int32_t(occupied)) {
				// Unreachable: it would need a negative number of inhabitants.
				surplus[k] = occupied + s;
				occupied = 0;
			} else {
				occupied = uint16_t(s + occupied);
			}
		}
	}
	fn_520840(save, town, &t.u16(0x778), t.u16(0x764));
}

// 0x51C0E0: llegan mendigos si su satisfacción pide más de los que hay (no
// con peste) o se van si sobran; tras un asedio ganado (0x800000) llegan más.
// Después limita los habitantes según el tamaño de la ciudad y apunta cuánto
// han cambiado los mendigos en el día.
void fn_51c0e0(SaveGame& save, int town) {
	TownMemory& t = save.towns[size_t(town)].memory;
	int32_t wanted = 8;
	const int16_t satisfaction = t.s16(0x306);
	const int32_t before = t.s32(0x2E4);
	if (satisfaction > 0) {
		wanted = root(int32_t(uint32_t(t.s32(0x2D4)) * uint32_t(int32_t(satisfaction))) >> 1) / 3 + 8;
	}
	int32_t step = root(t.s32(0x2D4));
	if (wanted > before) {
		if (!(t.u32(0x2C8) & 0x8)) {
			step = (step + 4) / 5;
			int32_t arriving = wanted - before;
			if (arriving > step) {
				arriving = step;
			}
			if ((t.u32(0x76C) & 0x1000) == 0x1000) {
				arriving = int32_t(uint32_t(arriving) * 13 + 9) / 10;
			}
			if (t.u16(0x844) == 0 && arriving + before < 24) {
				arriving = 24 - before;
			}
			t.u32(0x2E4) += uint32_t(arriving);
		}
	} else {
		step = (step + 9) / 10;
		const int32_t leaving = before - wanted;
		t.u32(0x2E4) -= uint32_t(leaving > step ? step : leaving);
	}
	if ((t.u32(0x2C8) & 0x800008) == 0x800000) {
		t.u32(0x2C8) &= ~0x800000u;
		int32_t extra = step / 6 + (wanted >> 1);
		const int32_t beggars = t.s32(0x2E4);
		if (beggars > 50) {
			const int32_t total = t.s32(0x2D4);
			if (int32_t((uint32_t(beggars) + uint32_t(extra)) << 2) > total) {
				extra = total / 4 - beggars;
				if (extra < 0) {
					extra = 0;
				}
			}
		}
		t.s32(0x2E4) = int32_t(uint32_t(beggars) + uint32_t(extra));
	}
	uint32_t total = 0;
	for (size_t i = 0; i < 4; ++i) {
		total += t.u32(0x2D8 + 4 * i);
	}
	t.u32(0x2D4) = total;
	// Habitantes como mucho según el tamaño de la ciudad ([0x76C]).
	const uint32_t flags = t.u32(0x76C);
	int32_t limit;
	if (flags & 0x200) {
		limit = 10000000;
	} else if (!(flags & 0x2)) {
		limit = 400;
	} else {
		limit = (flags & 0x10) ? 1000 : 500;
		if (flags & 0x20) {
			limit += 250;
		}
		if (flags & 0x80) {
			limit += 250;
		}
	}
	int32_t room = int32_t(uint32_t(limit) - total);
	if (room < 0) {
		const int32_t beggars = t.s32(0x2E4);
		if (-room >= beggars) {
			room = 1 - beggars;
		}
		t.u32(0x2E4) += uint32_t(room);
		t.u32(0x2D4) += uint32_t(room);
	}
	const uint32_t day = (save.globals.u32(global::kWorldTick) >> 8) & 7;
	t.u16(0x9A4 + 2 * day) = uint16_t(t.u16(0x2E4) - uint16_t(before));
}

}  // namespace p3::data::tick
