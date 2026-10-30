// El tick de ciudad (0x51BA10) y lo que cuelga de él salvo lo de los otros ficheros.

#include <cstring>

#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

namespace core_detail {

// Globales que no están en game_memory.hpp.
constexpr uint32_t kWorldRecordCount = 0x6DE4A6;  // u16 registros de 0x14 del mundo ([+6] del mundo).
constexpr uint32_t kWorld8A = 0x6DE52A;  // u8 [+0x8A] del mundo: si no es 0, la ciudad no construye (0x51E070, 0x51D870).
constexpr uint32_t kWorldChurchFactor = 0x6DE52C;  // u8 [+0x8C] del mundo: encarece las ampliaciones de la iglesia.
constexpr uint32_t kGlobal6DF31C = 0x6DF31C;  // u32: si no es 0, tampoco construye.
// Lista de u16 (número en 0x6DD628, entradas desde 0x6DD62C) que llenan las
// instalaciones (0x51086C) y lee 0x51CBE0; el tick la vacía antes.
constexpr uint32_t kFacilityList = 0x6DD628;

constexpr size_t kChurch = 0x794;  // La iglesia de la ciudad (0x10 bytes; ver fn_4fe877).

// 0x672860: consumo diario de cada 100 habitantes por mercancía y clase
// (ricos, acomodados, pobres, mendigos; los mendigos no se usan).
constexpr int16_t kCitizenConsumption[kWareCount][4] = {
	{90, 120, 150, 120},  // Grano
	{110, 87, 12, 5},  // Carne
	{40, 80, 100, 110},  // Pescado
	{65, 130, 65, 75},  // Cerveza
	{1, 1, 1, 1},  // Sal
	{50, 25, 5, 2},  // Miel
	{4, 2, 2, 0},  // Especias
	{150, 38, 0, 0},  // Vino
	{50, 35, 15, 1},  // Paño
	{60, 30, 0, 0},  // Pieles
	{50, 35, 10, 0},  // Aceite de ballena
	{80, 80, 40, 20},  // Madera
	{100, 75, 25, 0},  // Herramientas
	{44, 35, 5, 0},  // Cuero
	{10, 40, 20, 5},  // Lana
	{0, 0, 0, 0},  // Brea
	{0, 0, 0, 0},  // Hierro en bruto
	{5, 3, 2, 3},  // Cáñamo
	{30, 18, 12, 1},  // Cerámica
	{1, 1, 0, 0},  // Ladrillos
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
};

// 0x672C14: 1 si la mercancía va por barriles (200 unidades internas), 0 si por cargas (2000).
constexpr uint8_t kByBarrel[kWareCount] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};

// 0x672C2C: mercancía que produce cada tipo de instalación (0..20, más tres ceros).
constexpr uint8_t kFacilityWare[24] = {0x06, 0x06, 0x06, 0x14, 0x09, 0x02, 0x03, 0x0C, 0x05, 0x00, 0x01, 0x0B, 0x08,
	0x04, 0x10, 0x0E, 0x07, 0x12, 0x13, 0x0F, 0x11, 0x00, 0x00, 0x00};

// Tablas por mercancía de lo escasa que es cuando la ciudad no tiene nada (0x51BDB0).
constexpr uint8_t kEmptyWeightA[kWareCount] = {8, 4, 6, 6, 4, 0, 0, 0, 0, 0, 4, 6, 0, 4, 4, 0, 0, 0, 4, 0, 0, 0, 0, 0};  // 0x672968
constexpr uint8_t kEmptyWeightB[kWareCount] = {4, 4, 6, 6, 2, 2, 0, 2, 4, 2, 4, 4, 2, 2, 6, 0, 0, 0, 2, 0, 0, 0, 0, 0};  // 0x672950
constexpr uint8_t kEmptyWeightC[kWareCount] = {2, 5, 2, 2, 2, 3, 3, 5, 5, 3, 3, 3, 2, 2, 2, 0, 0, 0, 3, 0, 0, 0, 0, 0};  // 0x672938

// Iglesia (0x4FE877): dinero de cada ampliación (0x6734B4, por nivel 0..2), las
// tres mercancías que pide (0x6734BC: madera, herramientas, ladrillos) y
// cuántos barriles o cargas de cada una (0x6734C0, [mercancía][nivel]).
constexpr uint16_t kChurchCost[4] = {20000, 40000, 60000, 0};
constexpr uint8_t kChurchWare[3] = {0x0B, 0x0C, 0x13};
constexpr uint8_t kChurchWareUnits[3][4] = {{20, 20, 30, 50}, {20, 20, 30, 50}, {50, 50, 60, 80}};
constexpr uint8_t kChurchPending = 0xFA;  // 0x672BD3: [+6] que se pone al registro de la iglesia al ampliarla.

constexpr float kHundredth = 0x1.47ae14p-7f;  // 0x672838: 0,01f (0x3C23D70A)
constexpr float kOne = 1.0f;  // 0x66BC80
constexpr float kMinusOne = -1.0f;  // 0x66BC70
constexpr float kThirtieth = 0x1.1111p-5f;  // 0x6737A0: 0x3D088880 (≈ 1/30)
constexpr float kThirty = 30.0f;  // 0x6737A4
constexpr float kFiveHundred = 500.0f;  // 0x6737A8

uint16_t office_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldOfficeCount);
}

uint16_t merchant_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldMerchantCount);
}

// 0x51CD80: los pobres [0x2E0] no bajan de los que hacen falta para trabajar:
// 4 por empleado de las instalaciones de la ciudad, menos 4 por cada uno de
// las empresas de los comerciantes (registros del mundo desde [0x2CC] de cada
// oficina) y menos los ricos y los acomodados.
void fn_51cd80(SaveGame& save, int town) {
	TownMemory& t = save.towns[size_t(town)].memory;
	uint32_t needed = 0;
	for (size_t k = 0; k < 21; ++k) {
		// Instalación k: [+4] empleados y [+0xA].
		needed += 4 * (uint32_t(t.u16(0x840 + 16 * k + 0xA)) + t.u16(0x840 + 16 * k + 4));
	}
	for (uint32_t o = t.u16(0x784); o < office_count(save); o = save.offices[o].memory.u16(0x2CA)) {
		const OfficeMemory& office = save.offices[o].memory;
		for (uint16_t r = office.u16(0x2CC); r < save.globals.u16(kWorldRecordCount);
			r = save.world_records[r].u16(8)) {
			needed -= 4 * uint32_t(save.world_records[r].u16(4));
		}
	}
	// Ricos [0x2D8] y acomodados [0x2DC].
	const int32_t poor = int32_t(needed - (t.u32(0x2DC) + t.u32(0x2D8)));
	if (t.s32(0x2E0) < poor) {
		t.s32(0x2E0) = poor;
	}
}

// 0x527CD0: media móvil de lo que baja el almacén cada día: [0x430 + 4·w] =
// (13·[0x430 + 4·w] − existencias + existencias de ayer [0x3D0 + 4·w]) / 14.
void fn_527cd0(SaveGame& save, int town) {
	TownMemory& t = save.towns[size_t(town)].memory;
	for (size_t w = 0; w < kWareCount; ++w) {
		const uint32_t v = 13 * t.u32(0x430 + 4 * w) - t.u32(4 + 4 * w) + t.u32(0x3D0 + 4 * w);
		t.s32(0x430 + 4 * w) = int32_t(v) / 14;
	}
}

// 0x527D40: consumo diario de los habitantes (P3Modding
// towns/population/consumption.md). Por clase (pobres, acomodados y ricos),
// la tabla 0x672860 por los habitantes / 100; en invierno ([0x2C8] & 2)
// +1/5 de madera y pieles, y sin grano bastante, +3/10 de carne, pescado y
// paño. Se apunta en [0x310] y se quita del almacén y de [0x3D0]. Lleva
// también la cuenta de la hambruna ([0x2C3], bandera 0x1000 de [0x2C8]).
void fn_527d40(SaveGame& save, int town) {
	TownMemory& t = save.towns[size_t(town)].memory;
	int32_t demand[kWareCount];
	for (int c = 2; c >= 0; --c) {
		const int32_t citizens = t.s32(0x2D8 + 4 * size_t(c));
		if (citizens <= 0) {
			continue;
		}
		for (size_t w = 0; w < kWareCount; ++w) {
			demand[w] = int32_t(uint32_t(int32_t(kCitizenConsumption[w][c])) * uint32_t(citizens));
		}
		if (t.u32(0x2C8) & 2) {  // Invierno.
			demand[11] = int32_t(uint32_t(demand[11]) + uint32_t(demand[11] / 5));
			demand[9] = int32_t(uint32_t(demand[9]) + uint32_t(demand[9] / 5));
		}
		if (t.s32(4) < demand[0] / 100) {  // Falta grano.
			demand[1] = int32_t(13 * uint32_t(demand[1])) / 10;
			demand[2] = int32_t(13 * uint32_t(demand[2])) / 10;
			demand[8] = int32_t(13 * uint32_t(demand[8])) / 10;
		}
		for (size_t w = 0; w < kWareCount; ++w) {
			if (demand[w] <= 0) {
				continue;
			}
			const int32_t daily = demand[w] / 100;
			demand[w] = daily;
			t.u32(0x310 + 4 * w) += uint32_t(daily);
			const int32_t stock = t.s32(4 + 4 * w);
			if (stock < daily) {
				demand[w] = stock;
				t.s32(4 + 4 * w) = 0;
			} else {
				t.s32(4 + 4 * w) = stock - daily;
			}
			t.u32(0x3D0 + 4 * w) -= uint32_t(demand[w]);
		}
	}

	// Hambruna: [0x2C3] sube sin grano (y más sin carne o pescado) y baja 8
	// con grano; pasando de 80 empieza (0x1000) y acaba al bajar de 10.
	if (t.s32(4) > 0) {
		t.u8(0x2C3) = t.u8(0x2C3) > 7 ? uint8_t(t.u8(0x2C3) - 8) : 0;
		if ((t.u32(0x2C8) & 0x1000) && t.u8(0x2C3) < 10) {
			t.u32(0x2C8) &= ~0x1000u;
		}
		return;
	}
	const uint8_t counter = uint8_t(t.u8(0x2C3) + 1);
	t.u8(0x2C3) = counter;
	if (t.s32(8) < t.s32(0x504)) {  // Carne por debajo de su t1.
		t.u8(0x2C3) = uint8_t(counter + 1);
	}
	if (t.s32(0xC) < t.s32(0x510)) {  // Pescado por debajo de su t0.
		t.u8(0x2C3) = uint8_t(t.u8(0x2C3) + 1);
	}
	if (t.u8(0x2C3) > 0x50) {
		t.u8(0x2C3) = 0x50;
		if (!(t.u32(0x2C8) & 0x1000)) {
			t.u32(0x2C8) |= 0x1000;
			// Después el juego avisa a los comerciantes (0x4D6530, mensaje 0x35)
			// y, de mayor a menor, a cada humano ([+8] == 0) de esta ciudad
			// natal ([+0x19]): carta (0x548CA0) y, al jugador, la ventana de
			// la hambruna (0x469380, 0x469AF0 sobre la interfaz *0x6CC7E8). No
			// es simulación: no se traduce.
		}
	}
}

// 0x527D20: copia el almacén a [0x3D0] (las existencias de ayer para 0x527CD0).
void fn_527d20(SaveGame& save, int town) {
	TownMemory& t = save.towns[size_t(town)].memory;
	std::memcpy(&t.u8(0x3D0), &t.u8(4), 4 * kWareCount);
}

// 0x4FA6F0 sobre town_class1 ([0x7A4]; el juego le pasa [0x792] y [0x790]):
// cuenta las celdas de calle de la rejilla, 0x80..0x83 en [0x792] (calles
// totales) y 0x80/0x81 en [0x790] (calles construidas).
void fn_4fa6f0(SaveGame& save, int town) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	t.u16(0x790) = 0;
	t.u16(0x792) = 0;
	const uint32_t cells = uint32_t(t.s32(0x7A8 + 4)) * uint32_t(t.s32(0x7A8));  // alto × ancho
	for (uint32_t i = cells; i > 0; --i) {
		const uint8_t cell = st.grid[i - 1];
		if (cell < 0x80) {
			continue;
		}
		if (cell < 0x84) {
			++t.u16(0x792);
		}
		if (cell == 0x81 || cell == 0x80) {
			++t.u16(0x790);
		}
	}
}

// 0x50FE40 sobre un nodo de la lista [0x9B4]: lo que llega a la ciudad
// [+0x11] de la mercancía de la instalación [+0x10]. [+8] es el consumo
// diario de todas las ciudades (empresas y habitantes); [+4], lo que entra
// hoy: ese consumo por un factor en /1024 que sigue al de las existencias
// (1024 por debajo de t0 + 3/5·(t1 − t0), 0 desde t2), que [+0xE] persigue
// subiendo 10 al día hasta [+0xC] o bajando 2 hasta 4.
void fn_50fe40(SaveGame& save, int town, int node) {
	TownNode& n = save.towns[size_t(town)].nodes[size_t(node)];
	const size_t ware = kFacilityWare[n.u8(0x10)];
	int64_t consumption = 0;
	const uint32_t towns = save.globals.u32(global::kWorldTownCount);
	for (uint32_t j = 0; j < towns; ++j) {
		const TownMemory& o = save.towns[j].memory;
		consumption += int32_t(o.u32(0x310 + 4 * ware) + o.u32(0x64 + 4 * ware));
	}
	TownMemory& t = save.towns[n.u8(0x11)].memory;
	const int32_t stock = t.s32(4 + 4 * ware);
	const int32_t t0 = t.s32(0x4F0 + 16 * ware);
	const int32_t t1 = t.s32(0x4F4 + 16 * ware);
	const int32_t t2 = t.s32(0x4F8 + 16 * ware);
	const int32_t middle = int32_t(uint32_t(int32_t(3 * uint32_t(t1 - t0)) / 5) + uint32_t(t0));
	int32_t factor;
	if (stock >= t2) {
		factor = 0;
	} else if (stock <= middle) {
		factor = 0x400;
	} else {
		factor = int32_t(uint32_t(middle - stock) << 10) / (t2 - middle) + 0x400;
	}
	const uint16_t level = n.u16(0xE);
	if (factor < int32_t(level)) {
		n.u16(0xE) = uint16_t(level - 2);
		if (n.u16(0xE) < 4) {
			n.u16(0xE) = 4;
		}
		if (factor < 4) {
			factor = 4;
		}
	} else {
		n.u16(0xE) = uint16_t(level + 10);
		if (n.u16(0xE) > n.u16(0xC)) {
			n.u16(0xE) = n.u16(0xC);
		}
		factor = n.u16(0xE);
	}
	const uint32_t low = uint32_t(uint64_t(consumption));
	const int32_t amount = int32_t(low * uint32_t(factor)) >> 10;
	n.u32(8) = low & 0x7FFFFFFF;
	n.s32(4) = amount;
	t.u32(4 + 4 * ware) += uint32_t(amount);
}

// 0x51BDB0: la mercancía más escasa de las 20 de comercio, para los bits
// 17..22 de [0x2C8]: entre las que tienen menos de un barril o una carga,
// la de menor existencias / t0 (sin nada, −1 menos tres pesos por mercancía).
// Devuelve 0x20000 | mercancía << 18, o 0x7C0000 si no hay ninguna.
uint32_t fn_51bdb0(const SaveGame& save, int town) {
	const TownMemory& t = save.towns[size_t(town)].memory;
	uint32_t scarcest = 0x1F;
	float lowest = 400.0f;
	for (size_t w = 0; w < 20; ++w) {
		const int32_t stock = t.s32(4 + 4 * w);
		const int32_t unit = kByBarrel[w] ? 200 : 2000;
		if (stock >= unit) {
			continue;
		}
		float ratio;
		if (stock == 0) {
			ratio = float(double(kMinusOne) - double(float(kEmptyWeightA[w])) - double(float(kEmptyWeightB[w])) -
				double(float(kEmptyWeightC[w])));
		} else {
			const float have = float(stock);
			const float t0 = float(t.s32(0x4F0 + 16 * w));
			ratio = float(double(have) / double(t0));
		}
		if (ratio < lowest) {
			lowest = ratio;
			scarcest = uint32_t(w);
		}
	}
	if (int32_t(scarcest) >= 0x14) {
		return 0x7C0000;
	}
	return scarcest << 18 | 0x20000;
}

// 0x525390: la iglesia ampliada vuelve a construirse: su registro (tipo
// 0x1F) sale de la lista de edificios [0x764], queda en obras ([+6] =
// 0xFA) y sin dueño, y entra en la lista [0x762] detrás de la tercera
// entrada seguida de tipo >= 0x30 o, si no la hay, al final. Comprobado con
// el oráculo solo en partidas retocadas (en las 12 no se amplía ninguna iglesia).
void fn_525390(SaveGame& save, int town) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	std::vector<BuildingRecord>& records = st.records;
	uint16_t* link = &t.u16(0x764);
	uint16_t i = *link;
	for (;;) {
		if (i >= t.u16(0x776)) {
			return;
		}
		if (records[i].type == 0x1F) {
			break;
		}
		link = &records[i].next;
		i = records[i].next;
	}
	BuildingRecord& church = records[i];
	const uint16_t self = *link;
	*link = church.next;
	church.pending = kChurchPending;
	church.owner = 0xFF;
	const uint16_t count = t.u16(0x776);
	link = &t.u16(0x762);
	uint16_t run = 0;
	for (uint16_t j = *link; j < count; j = records[j].next) {
		if (records[j].type < 0x30) {
			run = 0;
		} else if (++run >= 3) {
			church.next = records[j].next;
			records[j].next = self;
			return;
		}
		link = &records[j].next;
	}
	*link = self;
	church.next = 0xFFFF;
	// Si es la ciudad que se ve (*0x6E51AC), el juego la redibuja (0x590680,
	// 0x5891C0): no es simulación.
}

// 0x4FE877 sobre la iglesia ([0x794] de la ciudad `church_town`), con el
// índice de la ciudad como argumento. La iglesia: [+0] baja 100 al día,
// [+4] dinero reunido para la ampliación, [+8] días, [+0xC..0xE] barriles o
// cargas entregados de madera, herramientas y ladrillos, [+0xF] nivel. Con
// el dinero bastante, toma del almacén de la ciudad lo que pase de t0/2 de
// cada mercancía; con todo entregado, sube de nivel y empieza la obra (0x525390).
void fn_4fe877(SaveGame& save, int church_town, uint8_t town_index) {
	TownMemory& c = save.towns[size_t(church_town)].memory;
	c.u32(kChurch + 8) += 1;
	if (c.s32(kChurch) >= 100) {
		c.s32(kChurch) -= 100;
	} else {
		c.s32(kChurch) = 0;
	}
	const uint8_t level = c.u8(kChurch + 0xF);
	uint32_t extra = save.globals.u8(kWorldChurchFactor);
	switch (level) {
	case 0:
		extra *= 10000;
		break;
	case 1:
		extra *= 15000;
		break;
	default:
		extra *= 20000;
		break;
	}
	if (level >= 3 || c.s32(kChurch + 4) < int32_t(kChurchCost[level] + extra)) {
		return;
	}
	// De aquí al final, comprobado con el oráculo solo en partidas retocadas
	// (dinero de la iglesia, nivel y almacén): en las 12 partidas no se amplía.
	bool done = true;
	TownMemory& t = save.towns[town_index].memory;
	for (size_t k = 0; k < 3; ++k) {
		const uint8_t have = c.u8(kChurch + 0xC + k);
		const uint8_t needed = kChurchWareUnits[k][c.u8(kChurch + 0xF)];
		if (have >= needed) {
			continue;
		}
		const size_t ware = kChurchWare[k];
		const int32_t spare = t.s32(4 + 4 * ware) - (t.s32(0x4F0 + 16 * ware) >> 1);
		if (spare <= 0) {
			done = false;
			continue;
		}
		const int32_t unit = kByBarrel[ware] ? 200 : 2000;
		int32_t units = spare / unit;
		const int32_t missing = int32_t(needed) - int32_t(have);
		if (units < missing) {
			done = false;
		} else {
			units = missing;
		}
		c.u8(kChurch + 0xC + k) = uint8_t(have + units);
		t.u32(4 + 4 * ware) -= uint32_t(units * unit);
	}
	if (!done) {
		return;
	}
	c.u32(kChurch + 4) = 0;
	c.u32(kChurch + 8) = 0;
	c.u8(kChurch + 0xE) = 0;
	c.u8(kChurch + 0xD) = 0;
	c.u8(kChurch + 0xC) = 0;
	c.u8(kChurch + 0xF) = uint8_t(c.u8(kChurch + 0xF) + 1);
	fn_525390(save, town_index);
}

// 0x525F30: estados de la ciudad que dependen de la cerveza, los mendigos y
// las oficinas. [0x835]: días sin cerveza (hasta 9). [0x83C]: con 3 oficinas
// o más, 0xFF unos días de cada 30 según los mendigos [0x2E4], [0x998] y los
// habitantes; 0 si no. [0x83F]: comerciante (0 al caducar [0x838]). [0x83E]:
// 0xFF con mercado si (índice + día) ^ pobres es múltiplo de 4.
void fn_525f30(SaveGame& save, int town) {
	TownMemory& t = save.towns[size_t(town)].memory;
	const uint8_t dry = t.u8(0x835);
	if (t.s32(0x10) > 0) {  // Cerveza.
		if (dry != 0) {
			t.u8(0x835) = uint8_t(dry - 1);
		}
	} else if (dry < 9) {
		t.u8(0x835) = uint8_t(dry + 1);
	}

	int offices = 0;
	for (uint32_t o = t.u16(0x784); o < office_count(save); o = save.offices[o].memory.u16(0x2CA)) {
		++offices;
	}
	if (offices < 3) {
		t.u8(0x83C) = 0;
	} else if (t.u8(0x83C) == 0 || t.u8(0x83C) == 0xFF) {
		const float beggars = float(t.s32(0x2E4));
		const float b = float(save.towns[t.u8(0x2C0)].memory.u8(0x998));
		const float citizens = float(t.s32(0x2D4));
		const double base = double(beggars) * double(kHundredth) + double(kOne);
		const double share = double(b) * double(kFiveHundred) / double(citizens);
		const float x = float(base - share);
		if (!(x < kThirty) && !(x != x)) {
			t.u8(0x83C) = 0xFF;
		} else {
			const float y = float(double(x) * double(kThirtieth));
			const float day = float(int32_t(save.globals.u8(global::kWorldDay)));
			const float z = float(double(day) * double(y));
			const float whole = float(int32_t(ftol(z)));
			t.u8(0x83C) = double(z) - double(whole) < double(y) ? 0xFF : 0;
		}
	}

	if (t.u8(0x83F) >= merchant_count(save) && t.u32(0x838) < save.globals.u32(global::kWorldTick) + 0x900) {
		t.u8(0x83F) = 0;
	}
	const uint8_t e = t.u8(0x83E);
	if (e == 0xFF || e == 0) {
		const uint8_t mix = uint8_t(uint8_t(t.u8(0x2C0) + save.globals.u8(global::kWorldDay)) ^ t.u8(0x2E0));
		t.u8(0x83E) = (mix & 3) == 0 && (t.u32(0x76C) & 0x100) ? 0xFF : 0;
	}
}

// 0x528630: consumo del excedente (P3Modding towns/excess_consumption.md):
// con [0x2C8] & 0xE0, de las 20 mercancías de comercio que la ciudad no
// produce, quita (existencias − t1) · 16 · consumo / 10 / t3 sin bajar de t1.
void fn_528630(SaveGame& save, int town) {
	TownMemory& t = save.towns[size_t(town)].memory;
	if (!(t.u8(0x2C8) & 0xE0)) {
		return;
	}
	for (size_t w = 0; w < 20; ++w) {
		if (t.u32(0xC4 + 4 * w) != 0) {
			continue;
		}
		const int32_t stock = t.s32(4 + 4 * w);
		const int32_t t1 = t.s32(0x4F4 + 16 * w);
		if (stock <= t1) {
			continue;
		}
		const int32_t consumption = int32_t((t.u32(0x64 + 4 * w) + t.u32(0x310 + 4 * w)) << 4);
		const int64_t product = int64_t(int32_t(uint32_t(stock) - uint32_t(t1))) * int64_t(consumption);
		const int64_t removed = product / 10 / int64_t(t.s32(0x4FC + 16 * w));  // _alldiv dos veces
		const int32_t low = int32_t(uint32_t(uint64_t(removed)));
		// Quitarlo todo y el mínimo de t1 solo salen en partidas retocadas.
		t.s32(4 + 4 * w) = stock > low ? stock - low : 0;
		if (t.s32(4 + 4 * w) < t1) {
			t.s32(4 + 4 * w) = t1;
		}
	}
}

// 0x52A170: sin alcalde comerciante ([0x6F1] >= número de comerciantes), el
// dinero de la ciudad [0x700] se queda entre índice · 16 + día del año +
// habitantes / 16 y 1 000 000 − 50 · índice − 10 · día del año − ([0x31C] & 0x3FF).
void fn_52a170(SaveGame& save, int town) {
	TownMemory& t = save.towns[size_t(town)].memory;
	if (uint16_t(t.u8(0x6F1)) < merchant_count(save)) {
		return;
	}
	const uint32_t day = save.globals.u16(global::kWorldDayOfYear);
	const uint32_t index = t.u8(0x2C0);
	const int32_t high = int32_t(((0x4E20 - index) * 5 - day) * 10 - (t.u32(0x31C) & 0x3FF));
	const int32_t low = int32_t(index * 16 + day + uint32_t(t.s32(0x2D4) >> 4));
	const int32_t money = t.s32(0x700);
	if (high < money) {
		t.s32(0x700) = high;
	} else if (low > money) {
		t.s32(0x700) = low;
	}
}

}  // namespace core_detail

using namespace core_detail;

// 0x51BA10: el tick diario de una ciudad.
void fn_51ba10(SaveGame& save, int town, uint32_t day_of_year) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	const uint8_t month = save.globals.u8(global::kWorldMonth);
	uint32_t workers = t.u16(0x924);
	if (month >= 2 && month <= 10) {
		t.u32(0x2C8) &= ~2u;
	} else {
		t.u32(0x2C8) |= 2;  // Invierno (diciembre a febrero).
	}

	// El barco auslieger de la ciudad ([0x994]) cuesta [0x990] cada 7 días:
	// sin dinero bastante en [0x700], el barco queda libre (estado 3); si no,
	// se paga a su dueño ([+0] dinero y [+0x4C8]) como gasto 7 de la ciudad.
	// Ninguna ciudad de las 12 partidas tiene auslieger: comprobado con el
	// oráculo solo en partidas retocadas.
	const uint16_t ship = t.u16(0x994);
	if (ship < save.globals.u16(global::kShipCount) && (save.globals.u32(global::kWorldTick) >> 8) % 7 == 0) {
		if (t.u32(0x990) > t.u32(0x700)) {
			save.ships[ship].status = 3;
			t.u16(0x994) = 0xFFFF;
		} else if (save.ships[ship].owner < merchant_count(save)) {
			MerchantMemory& owner = save.merchants[save.ships[ship].owner].memory;
			owner.u32(0) += t.u32(0x990);
			owner.u32(0x4C8) += t.u32(0x990);
			fn_529f90(save, town, int32_t(t.u32(0x990)), 7);
		}
	}

	for (size_t w = 0; w < kWareCount; ++w) {
		const int32_t stock = t.s32(4 + 4 * w);
		t.u32(0x64 + 4 * w) = 0;  // Consumo de las empresas.
		t.u32(0xC4 + 4 * w) = 0;  // Producción.
		t.u32(0x310 + 4 * w) = 0;  // Consumo de los habitantes.
		if (stock < 0) {
			t.s32(4 + 4 * w) = 0;
		}
	}
	fn_51cd80(save, town);
	std::memcpy(&t.u8(0x2E8), &t.u8(0x2D8), 0x10);  // Habitantes por clase de ayer.
	fn_527cd0(save, town);
	fn_527d40(save, town);
	save.globals.u32(kFacilityList) = 0;
	std::memset(&t.u8(0x490), 0, 4 * kWareCount);
	for (int k = 0; k < 21; ++k) {
		fn_5101d0(save, town, k);
	}
	fn_527d20(save, town);

	for (uint32_t o = t.u16(0x784); o < office_count(save); o = save.offices[o].memory.u16(0x2CA)) {
		workers += fn_4ffd40(save, int(o)) & 0xFFFF;
	}
	if (int32_t(workers) > 0) {
		fn_524920(save, town, int32_t(workers));
	}
	if ((t.u8(0x2C0) & 0xF) == (save.globals.u8(global::kWorldDayOfYear) & 0xF)) {
		fn_4fa6f0(save, town);
	}
	fn_51c830(save, town);
	fn_51cbe0(save, town);

	// Las oficinas de la ciudad, y la última pasa a ser la primera.
	if (t.u16(0x784) < office_count(save)) {
		int previous = -1;  // Oficina cuyo [0x2CA] apunta a la actual; -1: [0x784] de la ciudad.
		auto link = [&]() -> uint16_t& {
			return previous < 0 ? t.u16(0x784) : save.offices[size_t(previous)].memory.u16(0x2CA);
		};
		int office;
		for (;;) {
			office = link();
			fn_4fff20(save, office, day_of_year);
			if (save.offices[size_t(office)].memory.u16(0x2CA) >= office_count(save)) {
				break;
			}
			previous = office;
		}
		const uint16_t first = t.u16(0x784);
		if (link() != first) {
			save.offices[size_t(office)].memory.u16(0x2CA) = first;
			t.u16(0x784) = link();
			link() = 0xFFFF;
		}
	}

	if (!(t.u32(0x2C8) & 0x10)) {  // Sin asedio.
		for (size_t k = 0; k < st.nodes.size(); ++k) {
			fn_50fe40(save, town, int(k));
		}
	}
	fn_51c650(save, town);
	fn_520bd0(save, town);
	if (!(t.u32(0x2C8) & 0x10)) {
		fn_51c0e0(save, town);
	}
	fn_528070(save, town, (t.u32(0x76C) >> 8) & 1);
	t.u32(0x2C8) &= 0xFF81FFFF;
	const uint32_t scarce = fn_51bdb0(save, town);
	t.u32(0x2C8) |= scarce;
	fn_4fe877(save, town, t.u8(0x2C0));
	fn_525f30(save, town);
	if (!(t.u32(0x2C8) & 0x10) && t.u32(0x76C) > 0x200) {
		fn_528630(save, town);
	}

	// Con un alcalde humano ([0x6F1], [+8] == 0) no se sigue; sin alcalde
	// comerciante, se ajusta el dinero de la ciudad.
	const uint8_t mayor = t.u8(0x6F1);
	if (mayor < merchant_count(save)) {
		if (save.merchants[mayor].memory.u16(8) == 0) {
			return;
		}
	} else {
		fn_52a170(save, town);
	}
	if (save.globals.u8(kWorld8A) == 0 && save.globals.u32(kGlobal6DF31C) == 0) {
		fn_51e070(save, town);
		fn_51d870(save, town);
	}
}

}  // namespace p3::data::tick
