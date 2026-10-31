// Oficinas de los comerciantes en la ciudad: su tick diario (0x4FFF20), con
// las empresas del comerciante, sus salarios y el administrador que compra y
// vende con la ciudad, y el recuento de edificios y trabajadores (0x4FFD40).
//
// Campos de la oficina (OfficeMemory, 0x44C bytes) que se usan aquí:
//   [+0x0..0x2C0]  struct storage, como el de la ciudad: [+0] bandera de falta
//                  de materias primas, existencias [+4 + 4·w], consumo de las
//                  empresas [+0x64 + 4·w], producción del día [+0xC4 + 4·w],
//                  series de producción [+0x13C + 16·w + 2·día] (día = tick/256 mod 8)
//   [+0x2C4] u16 dueño (comerciante)        [+0x2C6] u8 ciudad
//   [+0x2C7] u8 gasto diario extra (×10)    [+0x2CA] u16 siguiente oficina de la ciudad
//   [+0x2CC] u16 primera empresa (registro de 0x14 del mundo; siguen por su [+8])
//   [+0x2CE] u16 primer edificio de la oficina en la ciudad (registros [0x75C])
//   [+0x2D2] u16 edificios terminados       [+0x2D6] u16 órdenes del administrador:
//            0x10 mantener existencias (IA), 2 comprar lo que consumen las
//            empresas, 1 comprar y vender a precio fijo
//   [+0x2DE] u16 ×3 y [+0x2E4] u8 ×3: tres grupos (¿guardias?) con su número y nivel
//   [+0x2E8] u32 se suma a las existencias al cobrar el alquiler
//   [+0x2EC] u16 capacidad del almacén en cargas (de 2000 unidades): lo que no
//            paga alquiler; los puestos [+0x44A] son 2 + capacidad / 200
//   [+0x2EE] u16 capacidad en obras         [+0x2F0] u16 días que le faltan a la obra
//            (una oficina nueva empieza con 0 y 50 en obras)
//   [+0x2F2] u16 administrador (índice de capitán, en SaveGame::captains)
//   [+0x2F4 + 4·w] s32 precio del administrador: > 0 vende por encima, < 0 compra por debajo
//   [+0x354 + 4·w] s32 existencias que quiere tener
//   [+0x3B8 + 4·w] float coste medio de la mercancía en la oficina
//   [+0x418] gastos de los grupos de [+0x2DE]  [+0x420] dinero movido por el administrador
//   [+0x428] salarios   [+0x430] mantenimiento   [+0x448] u16 empleados   [+0x44A] u16 puestos
//
// Las empresas de las oficinas son registros de 0x14 bytes del mundo
// (SaveGame::world_records) con los mismos campos que una instalación de la
// ciudad (struct facility): [+0] eficiencia, [+4] u16 trabajadores, [+6] tipo
// (4..20), [+7] ciudad, y además [+8] u16 siguiente, [+0xA] u16 puestos en
// edificios con la celda >= 0x80, [+0xC] u16 puestos en edificios terminados,
// [+0xE] u16 puestos en todos y [+0x10] float fracción de puestos que ocupar.
// Las empresas producen con las mismas funciones que las de la ciudad
// (0x50E690..., businesses.cpp) sobre el almacén de la oficina, y contratan y
// despiden con 0x4D4A40 y 0x50E380 (también de businesses.cpp).

#include <cstring>

#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

namespace {

constexpr uint32_t kWorldRecordCount = 0x6DE4A6;  // u16 registros de 0x14 del mundo.
constexpr uint32_t kTradeDifficultyFactor = 0x6DE3D8 + 0x64;  // float del objeto de precios (2,2 / 2,0 / 1,8).

// Puestos de trabajo por edificio de cada tipo de instalación (0x6729D0).
constexpr uint8_t kJobsPerBuilding[21] = {50, 40, 30, 15, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
	15, 15, 30};
// Salario diario por trabajador ×3 de cada tipo: para la IA (0x6729E8) y para
// los jugadores humanos (0x672A00; el último, 0x672A15, es el extra de las
// pescaderías cuando la ciudad tiene [0x2CC]).
constexpr int8_t kWagesAi[21] = {30, 0, 36, 26, 56, 30, 16, 36, 16, 6, 39, 6, 33, 9, 36, 36, 36, 26, 3, 0, 6};
constexpr int8_t kWages[22] = {30, 0, 36, 26, 68, 32, 22, 48, 17, 7, 45, 8, 40, 9, 44, 44, 38, 32, 3, 3, 7, 24};
// Coste de los tres grupos de [+0x2DE] de la oficina (0x672A38) y factor de su
// nivel [+0x2E4] (0x6734F0; los niveles vistos van de 0 a 4).
constexpr uint16_t kGroupCost[3] = {274, 141, 54};
constexpr uint8_t kGroupLevel[8] = {0x00, 0x40, 0x80, 0x8E, 0xA6, 0x00, 0x00, 0x00};
// Mercancías por barriles (1) o por cargas (0) en las funciones inversas de
// los precios (0x672C14).
constexpr uint8_t kBarrelWare[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
// Peso de cada clase de habitantes (ricos, acomodados, pobres) en la reputación
// por mercancía (0x672E10, tres floats por mercancía). Las mercancías 20..23
// leen lo que sigue a la tabla en el ejecutable (otras constantes).
constexpr uint32_t kClassWeightBits[72] = {
	0x3faaaaab, 0x402aaaab, 0x40aaaaab, 0x40800000, 0x402aaaab, 0x402aaaab, 0x3faaaaab, 0x40800000, 0x40800000,
	0x3faaaaab, 0x40800000, 0x40800000, 0x3faaaaab, 0x3faaaaab, 0x3faaaaab, 0x40000000, 0x3faaaaab, 0x3f2aaaab,
	0x40000000, 0x3faaaaab, 0x3f2aaaab, 0x40800000, 0x3faaaaab, 0x00000000, 0x40800000, 0x402aaaab, 0x3faaaaab,
	0x40000000, 0x3faaaaab, 0x00000000, 0x40000000, 0x402aaaab, 0x402aaaab, 0x402aaaab, 0x402aaaab, 0x40800000,
	0x3faaaaab, 0x3faaaaab, 0x3faaaaab, 0x3faaaaab, 0x3faaaaab, 0x402aaaab, 0x3faaaaab, 0x40800000, 0x402aaaab,
	0x3f2aaaab, 0x3eaaaaab, 0x3eaaaaab, 0x3faaaaab, 0x3faaaaab, 0x3faaaaab, 0x3faaaaab, 0x3faaaaab, 0x3faaaaab,
	0x40000000, 0x3faaaaab, 0x402aaaab, 0x3faaaaab, 0x3faaaaab, 0x3faaaaab, 0x42700000, 0x42200000, 0x41400000,
	0x40e00000, 0x42180000, 0x3727c5ac, 0x47c35000, 0x00000000, 0xbc6a7efa, 0x3f689374, 0x3fc00000, 0x399d4952};

// Constantes float del ejecutable.
constexpr float kThree = 3.0f;  // 0x66FD54
constexpr float kRound = 0.4f;  // 0x671FC0: redondeo de los puestos; parte del pescado que es aceite
constexpr float kLeatherShare = 0.49f;  // 0x671FC4
constexpr float kTwenty = 20.0f;  // 0x671FC8
constexpr float kFive = 5.0f;  // 0x66BC74
constexpr float kTen = 10.0f;  // 0x66DCD8
constexpr float kFour = 4.0f;  // 0x66F4A0
constexpr float kHalf = 0.5f;  // 0x66BC58
constexpr float kReputationBoost = 1.5f;  // 0x672F28
constexpr float kAdminMargin = 1.3f;  // 0x6734F8
constexpr float kBuyFactor[5] = {4.0f, 1.5f, 1.0f, 0.8f, 0.6f};  // 0x6739E8..0x6739F8
constexpr float kSellFactor[4] = {1.4f, 1.0f, 0.7f, 0.5f};  // 0x673A08..0x673A14
constexpr float kTaxFactor = 1.15f;  // 0x673A7C
constexpr float kPriceOffset = 0.9f;  // 0x673A80
constexpr float kQualityScale = 2560.0f;  // 0x673A84
constexpr float kLoadUnit = 2000.0f;  // 0x66A918
constexpr float kBarrelUnit = 200.0f;  // 0x66A91C

int32_t imul(int32_t a, int32_t b) {
	return int32_t(uint32_t(a) * uint32_t(b));
}

int32_t sub(int32_t a, int32_t b) {
	return int32_t(uint32_t(a) - uint32_t(b));
}

// __ftol (0x639C28): trunca a 64 bits y devuelve los 32 bajos en eax (fuera de
// rango o NaN, el entero indefinido 0x8000000000000000, es decir, 0).
int32_t ftol32(double v) {
	if (!(v > -9223372036854775808.0 && v < 9223372036854775808.0)) {
		return 0;
	}
	return int32_t(uint32_t(uint64_t(int64_t(v))));
}

float class_weight(int ware, int cls) {
	float v;
	std::memcpy(&v, &kClassWeightBits[3 * ware + cls], sizeof(v));
	return v;
}

// Hueco de la serie de producción de hoy (tick / 256 mod 8).
uint32_t day_slot(const SaveGame& save) {
	return (save.globals.u32(global::kWorldTick) >> 8) & 7;
}

// --- Estadísticas del comerciante al comerciar con una ciudad ---

// Lo que pesa una unidad de la mercancía en la reputación: sus habitantes de
// cada clase ([0x2D8 + 4·k]) por el peso de la clase.
float class_demand(const TownMemory& t, uint32_t ware) {
	double x = double(float(t.s32(0x2E0))) * double(class_weight(int(ware), 2));
	x = x + double(float(t.s32(0x2DC))) * double(class_weight(int(ware), 1));
	x = x + double(float(t.s32(0x2D8))) * double(class_weight(int(ware), 0));
	return float(x);
}

// Suma saturada a 0x7FFFFFFF de un total positivo (si no lo es, lo sustituye).
void add_total(MerchantMemory& m, size_t field, int32_t money) {
	const int32_t total = m.s32(field);
	if (total <= 0) {
		m.s32(field) = money;
	} else if (0x7FFFFFFF - total >= money) {
		m.s32(field) = total + money;
	} else {
		m.s32(field) = 0x7FFFFFFF;  // Checked with the oracle: scenario office_business (saves 3 and 5, direct calls).
	}
}

// Reputación del comerciante en la ciudad por lo que mueve de una mercancía:
// la cantidad (como mucho `room`) / umbral / habitantes · [0x464] del
// comerciante, ×2 con [0x2C8] & 0x1000 y `boost`, ×1,5 con [0x2C8] & 0x8A18.
float trade_reputation(const TownMemory& t, const MerchantMemory& m, int32_t amount, int32_t room, int32_t threshold,
	bool boost) {
	float a = float(amount < room ? amount : room);
	a = float(double(a) / double(float(threshold)));
	a = float(double(a) / double(float(t.s32(0x2D4))));
	a = float(double(a) * double(m.f32(0x464)));
	const uint32_t flags = t.u32(0x2C8);
	if ((flags & 0x1000) && boost) {
		a = float(double(a) + double(a));
	}
	if (flags & 0x8A18) {
		a = float(double(a) * double(kReputationBoost));
	}
	return a;
}

// Factor de precio por unidad de `price` para la mercancía (precio / precio base / escala).
double price_factor(uint32_t ware, double price) {
	const float unit = kBarrelWare[ware] ? kBarrelUnit : kLoadUnit;
	return price / (double(ware_base_price(int(ware))) * double(unit));
}

// Existencias a las que el factor `r` cae dentro del tramo [hi, lo) de la recta
// entre los umbrales `from` y `to`.
int32_t factor_stock(float r, float hi, float lo, int32_t from, int32_t to) {
	const float width = float(sub(to, from));
	return int32_t(uint32_t(ftol32((double(hi) - double(r)) * double(width) / (double(hi) - double(lo)))) +
		uint32_t(from));
}

}  // namespace

namespace offices_detail {

// 0x4D49D0: puestos de trabajo de la empresa según los edificios de su tipo
// (`buildings`: todos, terminados y terminados con la celda >= 0x80).
void fn_4d49d0(SaveGame& save, int record, const uint16_t buildings[3]) {
	GameMemory<0x14>& r = save.world_records[size_t(record)];
	const uint16_t jobs = kJobsPerBuilding[r.u8(6)];
	r.u16(0xA) = uint16_t(buildings[2] * jobs);
	r.u16(0xC) = uint16_t(buildings[1] * jobs);
	r.u16(0xE) = uint16_t(buildings[0] * jobs);
}

// 0x4D3670: ajusta los trabajadores de la empresa a la fracción [+0x10] de sus
// puestos [+0xC] (como mucho [+0xA]): pide los que faltan a la lista de
// contrataciones con el identificador ~`id` o despide los que sobran (0x50E380).
void fn_4d3670(SaveGame& save, int record, uint32_t id) {
	GameMemory<0x14>& r = save.world_records[size_t(record)];
	const float jobs = float(int32_t(r.u16(0xC)));
	int32_t wanted = ftol32(double(jobs) * double(r.f32(0x10)) + double(kRound));
	if (wanted > int32_t(r.u16(0xA))) {
		wanted = r.u16(0xA);
	}
	const int32_t change = wanted - int32_t(r.u16(4));
	if (change > 0) {
		fn_4d4a40(save, uint16_t(~id), uint16_t(change));
	} else if (change < 0) {
		fn_50e380(save, facility_of(r), uint32_t(-change), true);  // Checked with the oracle: scenario office_business (saves 3 and 5, direct calls).
	}
}

// 0x4D4400: coste medio [0x3B8 + 4·w] de lo que ha producido la empresa: el
// coste del día (alquiler de los puestos [+0xC] según [0x2C6] de la ciudad,
// salarios y las materias primas a su coste medio) repartido entre lo que
// había y lo producido (`made`, y `made2` de la segunda mercancía).
void fn_4d4400(SaveGame& save, int record, int office, int32_t made, int32_t made2) {
	if (made2 == 0 && made == 0) {
		return;
	}
	const GameMemory<0x14>& r = save.world_records[size_t(record)];
	OfficeMemory& o = save.offices[size_t(office)].memory;
	const uint32_t type = r.u8(6);
	const TownMemory& t = save.towns[r.u8(7)].memory;
	if (type - 4 > 0x10) {
		return;  // Unreachable: the offices only have types 4..20 (and with 0x15 or more the
		// game divides by 0 first, 0x4D444C). Checked with the oracle: scenario office_business (saves 3 and 5, direct calls).
	}
	auto avg = [&o](size_t ware) -> float& { return o.f32(0x3B8 + 4 * ware); };
	const int32_t buildings = int32_t(r.u16(0xC)) / int32_t(kJobsPerBuilding[type]);
	float cost = float(imul(buildings, t.u16(0x2C6)));
	const uint32_t workers = r.u16(4);
	const float wages = float(imul(kWages[type], int32_t(workers)));
	cost = float(double(wages) / double(kThree) + double(cost));
	// Coste de las materias primas de `amount` unidades: su coste medio · amount / div.
	auto input = [&](int32_t amount, size_t ware, float div) {
		const float p = float(amount);
		cost = float(double(p) * double(avg(ware)) / double(div) + double(cost));
	};
	size_t ware = 0;
	switch (type) {
	case 4:
		input(made, 12, kTwenty);
		input(made, 17, kTwenty);
		ware = 9;
		break;
	case 5:
		if (t.u32(0x2CC) > 0) {
			// Pescado y aceite: el 40 % del coste es del aceite.
			double x = double(float(int32_t(kWages[21]))) / double(kThree);
			const int32_t oil = o.s32(0x2C);
			x = x * double(float(int32_t(workers)));
			x = x + double(cost);
			cost = float(x);
			const float oil_cost = float(double(cost) * double(kRound));
			cost = float(double(cost) - double(oil_cost));
			double v = double(sub(oil, made2)) * double(avg(10));
			v = double(oil_cost) + v;
			avg(10) = oil > 0 ? float(v / double(oil)) : 0.0f;
			const int32_t fish = o.s32(0xC);
			v = double(sub(fish, made)) * double(avg(2));
			v = double(cost) + v;
			const float p = float(made);
			v = double(p) * double(avg(4)) / double(kFive) + v;
			v = double(p) * double(avg(17)) / double(kTen) + v;
			avg(2) = fish > 0 ? float(v / double(fish)) : 0.0f;  // Without fish. Checked with the oracle: scenario office_business (saves 3 and 5, direct calls).
			return;
		}
		input(made, 4, kFive);
		input(made, 17, kTen);
		ware = 2;
		break;
	case 6:
		input(made, 0, kFour);
		input(made, 11, kTen);
		ware = 3;
		break;
	case 7: {
		const float p = float(made);
		cost = float(double(p) * double(avg(16)) + double(cost));
		cost = float(double(p) * double(avg(11)) + double(cost));
		ware = 12;
		break;
	}
	case 8:
		ware = 5;
		break;
	case 9:
		ware = 0;
		break;
	case 10: {
		// Carne y cuero: el 49 % del coste es del cuero.
		const float leather_cost = float(double(cost) * double(kLeatherShare));
		cost = float(double(cost) - double(leather_cost));
		const int32_t leather = o.s32(0x38);
		double v = double(sub(leather, made2)) * double(avg(13));
		const float q = float(made2);
		v = double(q) * double(avg(11)) / double(kFive) + v;
		v = double(q) * double(avg(4)) / double(kTen) + v;
		v = double(leather_cost) + v;
		avg(13) = leather > 0 ? float(v / double(leather)) : 0.0f;  // Without leather. Checked with the oracle: scenario office_business (saves 3 and 5, direct calls).
		const int32_t meat = o.s32(8);
		v = double(sub(meat, made)) * double(avg(1));
		const float p = float(made);
		v = double(p) * double(avg(4)) / double(kFour) + v;
		v = double(cost) + v;
		avg(1) = meat > 0 ? float(v / double(meat)) : 0.0f;  // Without meat. Checked with the oracle: scenario office_business (saves 3 and 5, direct calls).
		return;
	}
	case 11:
		ware = 11;
		break;
	case 12: {
		const float p = float(made);
		cost = float(double(p) * double(avg(14)) + double(cost));
		ware = 8;
		break;
	}
	case 13:
	case 17:
	case 19: {
		const float p = float(made);
		cost = float(double(p) * double(avg(11)) + double(cost));
		ware = type == 13 ? 4 : type == 17 ? 18 : 15;
		break;
	}
	case 14:
		ware = 16;
		break;
	case 15:
		ware = 14;
		break;
	case 16:
		ware = 7;
		break;
	case 18: {
		const float p = float(made);
		cost = float(double(p) * double(avg(11)) * double(kHalf) + double(cost));
		ware = 19;
		break;
	}
	case 20:
		ware = 17;
		break;
	}
	if (made <= 0) {
		return;
	}
	const int32_t stock = o.s32(4 + 4 * ware);
	double v = double(sub(stock, made)) * double(avg(ware));
	v = double(cost) + v;
	avg(ware) = float(v / double(stock));
}

// 0x4D3780: tick de una empresa de la oficina: produce según su tipo, apunta
// el coste medio (0x4D4400) y, si tiene trabajadores y ha producido, marca su
// tipo en `active` ([0x40 + 4·ciudad] del comerciante).
void fn_4d3780(SaveGame& save, int record, int office, uint32_t& active) {
	FacilityMemory& f = facility_of(save.world_records[size_t(record)]);
	StorageMemory& s = storage_of(save.offices[size_t(office)].memory);
	bool working = f.u16(4) != 0;
	const uint32_t type = f.u8(6);
	const uint32_t jobs = save.world_records[size_t(record)].u16(0xE);
	int32_t made = 0;
	int32_t made2 = 0;
	// Lo producido es lo que ha subido el campo `field`; si no sube, no cuenta como activa.
	auto grown = [&](size_t field, int32_t before) {
		made = sub(s.s32(field), before);
		if (s.s32(field) <= before) {
			working = false;
		}
	};
	switch (type) {
	// Tipos 0..3 (milicia, astillero...): no apuntan el coste medio.
	// Unreachable: the offices never have businesses of types 0..3 (their
	// buildings of those types go to the town, 0x51FF30).
	case 0:
		fn_50e500(save, s, f);
		break;
	case 1:
		fn_50e570(save, s, f);
		break;
	case 2:
		fn_50e610(save, s, f);
		break;
	case 3:
		fn_50f6e0(save, s, f, 0x14);
		break;
	case 4: {
		const int32_t before = s.s32(0x28);
		fn_50ed70(save, s, f, jobs);
		grown(0x28, before);
		break;
	}
	case 5: {
		const int32_t fish = s.s32(0xC);
		const int32_t oil = s.s32(0x2C);
		fn_50e690(save, s, f, uint16_t(jobs), false);
		if (s.s32(0xC) <= fish && s.s32(0x2C) <= oil) {
			working = false;
		}
		made = sub(s.s32(0xC), fish);
		made2 = sub(s.s32(0x2C), oil);
		break;
	}
	case 6: {
		const int32_t before = s.s32(0x10);
		fn_50e8b0(save, s, f, 0, uint16_t(jobs));
		grown(0x10, before);
		break;
	}
	case 7: {
		const int32_t before = s.s32(0x34);
		fn_50fbc0(save, s, f, jobs);
		grown(0x34, before);
		break;
	}
	case 8:
		fn_50ea00(save, s, f, jobs);
		made = s.s32(0xD8);  // Producción del día.
		break;
	case 9:
		fn_50ead0(save, s, f, jobs);
		made = s.s32(0xC4);
		break;
	case 10: {
		const int32_t meat = s.s32(8);
		const int32_t leather = s.s32(0x38);
		fn_50f460(save, s, f, jobs, true);
		if (s.s32(8) <= meat) {
			working = false;
		}
		made = sub(s.s32(8), meat);
		made2 = sub(s.s32(0x38), leather);
		break;
	}
	case 11:
		fn_50ecd0(save, s, f, jobs);
		made = s.s32(0xF0);
		break;
	case 12: {
		const int32_t before = s.s32(0x24);
		fn_50eeb0(save, s, f, jobs);
		grown(0x24, before);
		break;
	}
	case 13: {
		const int32_t before = s.s32(0x14);
		fn_50ef50(save, s, f, 0, uint16_t(jobs));
		grown(0x14, before);
		break;
	}
	case 14:
		fn_50f650(save, s, f, jobs);
		made = s.s32(0x104);
		break;
	case 15:
		fn_50f060(save, s, f, jobs);
		made = s.s32(0xFC);
		break;
	case 16:
		fn_50f100(save, s, f, jobs);
		made = s.s32(0xE0);
		break;
	case 17: {
		const int32_t before = s.s32(0x4C);
		fn_50f220(save, s, f, 0, uint16_t(jobs));
		grown(0x4C, before);
		break;
	}
	case 18: {
		const int32_t before = s.s32(0x50);
		fn_50f2e0(save, s, f, jobs);
		grown(0x50, before);
		break;
	}
	case 19: {
		const int32_t before = s.s32(0x40);
		fn_50f390(save, s, f, 0, uint16_t(jobs));
		grown(0x40, before);
		break;
	}
	case 20:
		fn_50ebf0(save, s, f, jobs);
		made = s.s32(0x108);
		break;
	}
	if (type >= 4 && type <= 20) {
		fn_4d4400(save, record, office, made, made2);
	}
	if (working) {
		active |= 1u << type;
	}
}

// 0x4D3560: salarios diarios de una empresa: los paga el comerciante
// ([0x480] y [0x484]) y se apuntan en la oficina ([0x428]), con sus empleados
// [0x448] y puestos [0x44A]. Las pescaderías de una ciudad con [0x2CC] pagan más.
void fn_4d3560(SaveGame& save, int record, int merchant, int office) {
	const GameMemory<0x14>& r = save.world_records[size_t(record)];
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	OfficeMemory& o = save.offices[size_t(office)].memory;
	const uint32_t type = r.u8(6);
	const int32_t workers = r.u16(4);
	const bool human = m.u16(8) == 0;
	int32_t wages = imul(human ? kWages[type] : kWagesAi[type], workers) / 3;
	if (type == 5 && save.towns[r.u8(7)].memory.u32(0x2CC) != 0) {
		// With a human player: Checked with the oracle: scenario office_business (saves 3 and 5, direct calls).
		wages += human ? imul(kWages[21], workers) / 3 : imul(workers, 7);
	}
	m.u32(0x480) += uint32_t(wages);
	m.u32(0x484) += uint32_t(wages);
	o.u16(0x448) = uint16_t(o.u16(0x448) + r.u16(4));
	o.u16(0x44A) = uint16_t(o.u16(0x44A) + r.u16(0xC));
	o.u32(0x428) += uint32_t(wages);
}

// 0x4F8690 (thiscall sobre el comerciante): el comerciante ha vendido `amount`
// de `ware` a la ciudad por `money`: lo suma a sus ventas ([0x4B0] o, sin
// `flag`, [0x49C]) y, si la ciudad tenía menos que el umbral 1, sube su
// reputación en ella ([0x124 + 12·ciudad]).
void fn_4f8690(SaveGame& save, int merchant, uint32_t town, uint32_t ware, int32_t money, int32_t amount, bool flag) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const TownMemory& t = save.towns[town].memory;
	add_total(m, flag ? 0x4B0 : 0x49C, money);  // Without flag: Checked with the oracle: scenario office_business (saves 3 and 5, direct calls).
	if (ware >= 0x14) {
		return;  // Checked with the oracle: scenario office_business (saves 3 and 5, direct calls).
	}
	const int32_t threshold = t.s32(0x4F4 + 16 * ware);
	const int32_t room = sub(threshold, t.s32(4 + 4 * ware));
	if (room <= 0) {
		return;
	}
	const float a = trade_reputation(t, m, amount, room, threshold, ware == 0);
	const float demand = class_demand(t, ware);
	float& reputation = m.f32(0x124 + 12 * town);
	reputation = float(double(demand) * double(a) + double(reputation));
}

// 0x4F8840 (thiscall sobre el comerciante): el comerciante ha comprado
// `amount` de `ware` a la ciudad por `money`: lo suma a sus compras ([0x4B4] o
// [0x4A0]) y, si la ciudad tenía menos que la mitad del umbral 1, baja su
// reputación en ella.
void fn_4f8840(SaveGame& save, int merchant, uint32_t town, uint32_t ware, int32_t money, int32_t amount, bool flag) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const TownMemory& t = save.towns[town].memory;
	add_total(m, flag ? 0x4B4 : 0x4A0, money);  // Without flag: Checked with the oracle: scenario office_business (saves 3 and 5, direct calls).
	const int32_t threshold = t.s32(0x4F4 + 16 * ware) >> 1;
	const int32_t room = sub(threshold, t.s32(4 + 4 * ware));
	if (room <= 0) {
		return;
	}
	const float a = trade_reputation(t, m, amount, room, threshold, ware <= 2);  // ware <= 2: sin comprobar.
	const float demand = class_demand(t, ware);
	float& reputation = m.f32(0x124 + 12 * town);
	reputation = float(double(reputation) - double(demand) * double(a));
}

// --- Funciones inversas de los precios (el objeto de precios 0x6DE3D8) ---

// 0x52EA80 (thiscall sobre 0x6DE3D8): existencias de la ciudad a partir de las
// cuales vender `ware` le reporta menos de `price` por unidad (0x7FFFFFFF si
// nunca baja de ahí; sin mercado, 0 o 0x7FFFFFFF según el signo de `price`).
// Only the sale of 0x4FF780 calls it (no save sells). Checked with the oracle:
// scenarios reverse_prices (every interval, saves 3 and 5, direct calls) and
// office_orders (a sale).
int32_t fn_52ea80(const SaveGame& save, uint32_t ware, int town, int32_t price) {
	const TownMemory& t = save.towns[size_t(town)].memory;
	if (!(t.u32(0x76C) & 0x100)) {
		return price > 0 ? 0 : 0x7FFFFFFF;
	}
	const float p = float(price);
	const float r = float(price_factor(ware, double(p) - double(kPriceOffset)));
	auto threshold = [&t, ware](size_t i) { return t.s32(0x4F0 + 16 * ware + 4 * i); };
	if (!(r > kSellFactor[3]) && !(r != r)) {
		return 0x7FFFFFFF;
	}
	if (r < kSellFactor[2]) {
		return factor_stock(r, kSellFactor[2], kSellFactor[3], threshold(2), threshold(3));
	}
	if (r < kSellFactor[1]) {
		return factor_stock(r, kSellFactor[1], kSellFactor[2], threshold(1), threshold(2));
	}
	if (r < kSellFactor[0]) {
		return factor_stock(r, kSellFactor[0], kSellFactor[1], threshold(0), threshold(1));
	}
	const float difficulty = save.globals.f32(kTradeDifficultyFactor);
	if (r < difficulty) {
		const float width = float(threshold(0));
		return ftol32((double(difficulty) - double(r)) * double(width) / (double(difficulty) - double(kSellFactor[0])));
	}
	return 0;
}

// 0x52EC50: existencias de la ciudad por debajo de las cuales comprarle `ware`
// cuesta más de `price` por unidad (con `quality` ≠ 0 el precio se corrige
// por 2560 / (2560 − quality); con la mercancía con recargo, ×1,15). Sin
// mercado, sus existencias.
int32_t fn_52ec50(const SaveGame& save, uint32_t ware, int town, int32_t price, uint32_t quality) {
	const TownMemory& t = save.towns[size_t(town)].memory;
	if (!(t.u32(0x76C) & 0x100)) {
		return t.s32(4 + 4 * ware);  // Checked with the oracle: scenario reverse_prices (saves 3 and 5, direct calls).
	}
	const float p = float(price);
	float r = float(price_factor(ware, double(p) + double(kPriceOffset)));
	if (quality != 0) {
		// Checked with the oracle: scenario reverse_prices (saves 3 and 5, direct calls).
		const float q = float(quality & 0xFF);
		const float rest = float(double(kQualityScale) - double(q));
		r = float(double(kQualityScale) / double(rest) * double(r));
	}
	if (ware == t.u8(0x6F0)) {
		r = float(double(r) * double(kTaxFactor));  // Checked with the oracle: scenario reverse_prices (saves 3 and 5, direct calls).
	}
	auto threshold = [&t, ware](size_t i) { return t.s32(0x4F0 + 16 * ware + 4 * i); };
	if (!(r > kBuyFactor[4]) && !(r != r)) {
		return 0x7FFFFFFF;  // Checked with the oracle: scenario reverse_prices (saves 3 and 5, direct calls).
	}
	if (r < kBuyFactor[3]) {
		return factor_stock(r, kBuyFactor[3], kBuyFactor[4], threshold(2), threshold(3));  // Checked with the oracle: scenario reverse_prices (saves 3 and 5, direct calls).
	}
	if (r < kBuyFactor[2]) {
		return factor_stock(r, kBuyFactor[2], kBuyFactor[3], threshold(1), threshold(2));  // Checked with the oracle: scenario reverse_prices (saves 3 and 5, direct calls).
	}
	if (r < kBuyFactor[1]) {
		return factor_stock(r, kBuyFactor[1], kBuyFactor[2], threshold(0), threshold(1));  // Checked with the oracle: scenario reverse_prices (saves 3 and 5, direct calls).
	}
	if (r < kBuyFactor[0]) {
		// Checked with the oracle: scenario reverse_prices (saves 3 and 5, direct calls).
		const float width = float(threshold(0));
		return ftol32((double(kBuyFactor[0]) - double(r)) * double(width) / (double(kBuyFactor[0]) - double(kBuyFactor[1])));
	}
	return 0;
}

// --- Administradores ---

// 0x4FFA30 (órdenes 0x10, solo la IA): mantiene en la oficina las existencias
// [0x354 + 4·w] de las 20 mercancías: compra lo que falta a la ciudad (sin
// bajarla del umbral 0) o le vende lo que sobra: a precio de mercado hasta su
// umbral 1 y el resto al precio base (la ciudad solo se queda con lo que le
// cabe hasta su umbral 3).
void fn_4ffa30(SaveGame& save, int office) {
	OfficeMemory& o = save.offices[size_t(office)].memory;
	const int town = o.u8(0x2C6);
	TownMemory& t = save.towns[size_t(town)].memory;
	const int merchant = o.u16(0x2C4);
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	for (uint32_t w = 0; w < 0x14; ++w) {
		const int32_t wanted = o.s32(0x354 + 4 * w);
		const int32_t have = o.s32(4 + 4 * w);
		if (wanted > have) {
			int32_t amount = sub(wanted, have);
			const int32_t spare = sub(t.s32(4 + 4 * w), t.s32(0x4F0 + 16 * w));
			if (amount > spare) {
				amount = spare;
				if (amount <= 0) {
					continue;
				}
			}
			const int32_t price = game_buy_price(save, int(w), town, amount);
			o.u32(0x420) += uint32_t(price);
			m.u32(0x4B4) += uint32_t(price);
			m.u32(0) -= uint32_t(price);
			fn_4f8840(save, merchant, uint32_t(town), w, price, amount, true);
			o.u32(4 + 4 * w) += uint32_t(amount);
			t.u32(4 + 4 * w) -= uint32_t(amount);
			continue;
		}
		int32_t rest = sub(have, wanted);
		const int32_t stock = t.s32(4 + 4 * w);
		const int32_t threshold1 = t.s32(0x4F4 + 16 * w);
		int32_t sold = 0;
		int32_t money = 0;
		if (threshold1 > stock) {
			sold = threshold1 - stock;
			if (sold > rest) {
				sold = rest;
				rest = 0;
			} else {
				rest -= sold;
			}
			money = game_sell_price(save, int(w), town, sold);
		}
		const float r = float(rest);
		money = int32_t(uint32_t(money) + uint32_t(ftol32(double(r) * double(ware_base_price(int(w))))));
		const int32_t total = rest + sold;
		const int32_t now = t.s32(4 + 4 * w);
		int32_t taken = sub(t.s32(0x4FC + 16 * w), now);
		if (taken > 0) {
			if (taken > total) {
				taken = total;
			}
			t.s32(4 + 4 * w) = now + taken;
		}
		o.u32(4 + 4 * w) -= uint32_t(total);
		m.u32(0) += uint32_t(money);
		m.u32(0x4B0) += uint32_t(money);
		fn_4f8690(save, merchant, uint32_t(town), w, money, total, true);
	}
}

// 0x4FFC20 (órdenes 2): compra a la ciudad lo que les falta a las empresas de
// la oficina para su consumo del día, si no cuesta más de 1,3 veces el precio
// base y el comerciante tiene el dinero (lo paga todo aunque la ciudad no lo tenga).
// No office of the 12 saves has this order. Checked with the oracle: scenario
// office_orders (saves 3 and 5, direct calls).
void fn_4ffc20(SaveGame& save, int office) {
	OfficeMemory& o = save.offices[size_t(office)].memory;
	const int town = o.u8(0x2C6);
	TownMemory& t = save.towns[size_t(town)].memory;
	const int merchant = o.u16(0x2C4);
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	for (uint32_t w = 24; w-- > 0;) {
		int32_t need = o.s32(0x64 + 4 * w);
		const int32_t have = o.s32(4 + 4 * w);
		if (need <= have) {
			continue;
		}
		need -= have;
		const int32_t price = game_buy_price(save, int(w), town, need);
		const float paid = float(price);
		const float units = float(need);
		const double fair = double(units) * double(ware_base_price(int(w))) * double(kAdminMargin);
		if (fair < double(paid) || m.s32(0) < price) {
			continue;
		}
		o.u32(0x420) += uint32_t(price);
		m.u32(0x4B4) += uint32_t(price);
		m.u32(0) -= uint32_t(price);
		fn_4f8840(save, merchant, uint32_t(town), w, price, need, true);
		const int32_t stock = t.s32(4 + 4 * w);
		if (need > stock) {
			need = stock;
			t.s32(4 + 4 * w) = 0;
		} else {
			t.s32(4 + 4 * w) = stock - need;
		}
		o.u32(4 + 4 * w) += uint32_t(need);
	}
}

// 0x4FF780 (órdenes 1): el administrador vende a la ciudad lo que pasa de las
// existencias [0x354 + 4·w] mientras le paguen al menos el precio [0x2F4 + 4·w]
// (si es > 0), o compra hasta ellas mientras cueste como mucho −precio (si es
// < 0), con un margen según la habilidad del capitán [0xA] (sin él, 100 %).
void fn_4ff780(SaveGame& save, int office) {
	OfficeMemory& o = save.offices[size_t(office)].memory;
	const int town = o.u8(0x2C6);
	TownMemory& t = save.towns[size_t(town)].memory;
	const int merchant = o.u16(0x2C4);
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	int32_t margin = 100;  // Without an administrator: Checked with the oracle: scenario office_orders (saves 3 and 5, direct calls).
	const uint32_t admin = o.u16(0x2F2);
	if (admin < save.globals.u16(global::kCaptainCount)) {
		margin = uint8_t((50 - int32_t(save.captains[admin].u8(0xA)) / 43) * 2);
	}
	for (uint32_t w = 24; w-- > 0;) {
		const int32_t price = o.s32(0x2F4 + 4 * w);
		if (price > 0) {
			// No save sells. Checked with the oracle: scenario office_orders (saves 3 and 5, direct calls).
			const int32_t spare = sub(o.s32(4 + 4 * w), o.s32(0x354 + 4 * w));
			if (spare <= 0) {
				continue;
			}
			int32_t amount = sub(fn_52ea80(save, w, town, price), t.s32(4 + 4 * w));
			if (amount <= 0) {
				continue;
			}
			if (amount > spare) {
				amount = spare;
			}
			const int32_t money = game_sell_price(save, int(w), town, amount);
			t.u32(4 + 4 * w) += uint32_t(amount);
			o.u32(4 + 4 * w) -= uint32_t(amount);
			m.u32(0) += uint32_t(money);
			o.u32(0x420) -= uint32_t(money);
			fn_4f8690(save, merchant, uint32_t(town), w, money, amount, true);
		} else if (price < 0) {
			const int32_t wanted = sub(o.s32(0x354 + 4 * w), o.s32(4 + 4 * w));
			if (wanted <= 0) {
				continue;
			}
			int32_t amount = sub(t.s32(4 + 4 * w), fn_52ec50(save, w, town, -price, 0));
			if (amount <= 0) {
				continue;
			}
			if (amount > wanted) {
				amount = wanted;
			}
			if (t.s32(4 + 4 * w) < amount) {
				amount = t.s32(4 + 4 * w);  // Checked with the oracle: scenario office_orders (saves 3 and 5, direct calls).
			}
			const int32_t cost_town = game_buy_price(save, int(w), town, amount);
			// 0x1000000 or more: checked with the oracle, scenario office_orders.
			const int32_t cost = cost_town >= 0x1000000 ? imul(cost_town / 100, margin)
				: imul(cost_town, margin) / 100;
			if (m.s32(0) < cost) {
				continue;  // Checked with the oracle: scenario office_orders (saves 3 and 5, direct calls).
			}
			t.u32(4 + 4 * w) -= uint32_t(amount);
			const int32_t old = o.s32(4 + 4 * w);
			const int32_t now = int32_t(uint32_t(old) + uint32_t(amount));
			o.s32(4 + 4 * w) = now;
			double v = double(o.f32(0x3B8 + 4 * w)) * double(old);
			v = double(cost) + v;
			o.f32(0x3B8 + 4 * w) = float(v / double(now));
			o.u32(0x420) += uint32_t(cost);
			m.u32(0) -= uint32_t(cost);
			fn_4f8840(save, merchant, uint32_t(town), w, cost, amount, true);
		}
	}
}

}  // namespace offices_detail

using namespace offices_detail;

// 0x4FFD40: cuenta los edificios de la oficina en la ciudad por tipo de
// instalación ([0x2D2]: los terminados), pone los puestos de trabajo de sus
// empresas (0x4D49D0) y ajusta sus trabajadores (0x4D3670). Devuelve (en ax)
// los trabajadores de sus fundiciones (tipo 14).
uint32_t fn_4ffd40(SaveGame& save, int office) {
	OfficeMemory& o = save.offices[size_t(office)].memory;
	SavedTown& town = save.towns[o.u8(0x2C6)];
	const TownMemory& t = town.memory;
	// Por tipo: todos, terminados y terminados en una celda >= 0x80 de la rejilla.
	// Con la crisis [0x2C8] & 0x10 (¿asedio?) solo trabajan los de las celdas
	// >= 0x80 (¿dentro de la muralla?), salvo las pescaderías.
	uint16_t buildings[21][3] = {};
	const bool crisis = t.u8(0x2C8) & 0x10;
	o.u16(0x2D2) = 0;
	for (uint32_t i = o.u16(0x2CE); i < t.u16(0x776);) {
		const BuildingRecord& b = town.records[i];
		i = b.next;
		if (b.type >= 0x15) {
			if (b.pending == 0) {
				++o.u16(0x2D2);
			}
			continue;
		}
		++buildings[b.type][0];
		if (b.pending != 0) {
			continue;
		}
		++o.u16(0x2D2);
		++buildings[b.type][1];
		if (crisis && town.grid[t.u32(0x7AC) * b.x + b.y] >= 0x80) {
			++buildings[b.type][2];
		}
	}
	if (!crisis) {
		for (size_t type = 4; type <= 20; ++type) {
			buildings[type][2] = buildings[type][1];
		}
	} else {
		buildings[5][2] = buildings[5][1];
	}
	uint16_t smelters = 0;
	for (uint32_t i = o.u16(0x2CC); i < save.globals.u16(kWorldRecordCount);) {
		GameMemory<0x14>& r = save.world_records[i];
		const uint8_t type = r.u8(6);
		fn_4d49d0(save, int(i), buildings[type]);
		// Una segunda empresa del mismo tipo se queda sin edificios.
		buildings[type][0] = buildings[type][1] = buildings[type][2] = 0;
		fn_4d3670(save, int(i), i);
		if (r.u8(6) == 0xE) {
			smelters = uint16_t(smelters + r.u16(4));
		}
		i = r.u16(8);
	}
	return smelters;
}

// 0x4FFF20: tick diario de la oficina: pone a cero el consumo y la producción
// del día, cobra los gastos de los grupos de [0x2DE], hace trabajar sus
// empresas (0x4D3780) y paga sus salarios (0x4D3560), el mantenimiento de los
// edificios con el administrador y el gasto [0x2C7], las órdenes del
// administrador, el alquiler de lo que pasa de la capacidad [0x2EC] y avanza
// la obra de la oficina o de su ampliación. `day_of_year` no se usa.
void fn_4fff20(SaveGame& save, int office, uint32_t day_of_year) {
	(void)day_of_year;
	OfficeMemory& o = save.offices[size_t(office)].memory;
	const uint8_t town = o.u8(0x2C6);
	// 0x51A850 y 0x51A880 crean y liberan el objeto del mensaje.
	std::memset(&o.u8(0xC4), 0, 0x60);
	std::memset(&o.u8(0x64), 0, 0x60);
	const int merchant = o.u16(0x2C4);
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	uint32_t& active = m.u32(0x40 + 4 * size_t(town));  // Tipos de empresa que producen en la ciudad.
	active = 0;
	for (size_t k = 0; k < 3; ++k) {
		const uint32_t cost = uint32_t(kGroupLevel[o.u8(0x2E4 + k)]) * kGroupCost[k] * o.u16(0x2DE + 2 * k);
		const int32_t daily = int32_t(cost + 15000) >> 15;
		m.u32(0x48C) += uint32_t(daily);
		m.u32(0x488) += uint32_t(daily);
		o.u32(0x418) += uint32_t(daily);
	}
	const uint32_t day = day_slot(save);
	for (size_t j = 0; j < 24; ++j) {
		o.u16(0x13C + 2 * day + 16 * j) = 0;
	}
	o.u16(0x448) = 0;
	o.u16(0x44A) = uint16_t(int32_t(o.u16(0x2EC)) / 200 + 2);
	for (uint32_t i = o.u16(0x2CC); i < save.globals.u16(kWorldRecordCount);) {
		GameMemory<0x14>& r = save.world_records[i];
		r.u8(7) = town;
		fn_4d3780(save, int(i), office, active);
		fn_4d3560(save, int(i), merchant, office);
		i = r.u16(8);
	}
	// Mantenimiento: edificios terminados más [0xC] del administrador.
	const uint32_t admin = o.u16(0x2F2);
	if (admin < save.globals.u16(global::kCaptainCount)) {
		const int32_t upkeep = int32_t(o.u16(0x2D2)) + int32_t(save.captains[admin].u16(0xC));
		if (upkeep > 0) {
			m.u32(0x480) += uint32_t(upkeep);
			m.u32(0x484) += uint32_t(upkeep);
			++o.u16(0x448);
			o.u32(0x430) += uint32_t(upkeep);
		}
	}
	if (o.u8(0x2C7) > 0) {
		const int32_t extra = o.u8(0x2C7) * 10;
		m.u32(0x480) += uint32_t(extra);
		m.u32(0x484) += uint32_t(extra);
		o.u32(0x428) += uint32_t(extra);
		o.u16(0x448) = uint16_t(o.u16(0x448) + o.u8(0x2C7));
	}
	const uint16_t orders = o.u16(0x2D6);
	if (orders & 0x10) {
		if (m.u16(8) == 0) {
			o.u8(0x2D6) &= 0xEF;  // The human players don't have it. Checked with the oracle: scenario office_orders (saves 3 and 5, direct calls).
		} else {
			fn_4ffa30(save, office);
		}
	} else {
		if (orders & 2) {
			fn_4ffc20(save, office);  // Checked with the oracle: scenario office_orders (saves 3 and 5, direct calls).
		}
		if (o.u8(0x2D6) & 1) {
			fn_4ff780(save, office);
		}
	}
	// Alquiler: una moneda por cada carga de 2000 unidades (redondeando hacia
	// arriba) por encima de la capacidad [0x2EC].
	uint32_t stored = uint32_t(o.u16(0x2EC)) * uint32_t(-2000);
	for (size_t w = 24; w-- > 0;) {
		stored += o.u32(4 + 4 * w);
	}
	stored += o.u32(0x2E8);
	if (int32_t(stored) > 0) {
		const int32_t rent = int32_t(stored + 1999) / 2000;
		m.u32(0x4CC) += uint32_t(rent);
		m.u32(0x4D0) += uint32_t(rent);
	}
	if (o.u16(0x2F0) != 0) {
		if (--o.u16(0x2F0) == 0) {
			// Termina la obra. 0x4D6530 (mensaje 0x60 a los comerciantes con la
			// ciudad) no se traduce.
			o.u16(0x2EC) = uint16_t(o.u16(0x2EC) + 50);
			o.u16(0x2EE) = uint16_t(o.u16(0x2EE) - 50);
		}
		// 0x5891C0 (interfaz: la barra de la obra si el jugador está viendo la
		// ciudad, con el objeto de 0x6E51AC) no se traduce.
	}
}

}  // namespace p3::data::tick
