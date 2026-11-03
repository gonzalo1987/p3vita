// El tick de un comerciante (0x4F7BB0): su contabilidad semanal, el valor de
// la compañía, la reputación en cada ciudad y los ascensos de rango.
//
// Campos del comerciante (MerchantMemory, 0x650 bytes) que se usan aquí:
//   [+0x0]   s32 dinero                     [+0x8] u16 tipo (0 = jugador humano)
//   [+0xC]   u16 primera oficina (siguen por el [+0x2C8] de cada oficina)
//   [+0xE]   u16 primer barco (siguen por el [+0x4] de cada barco)
//   [+0x15]  u8 (se limita a 200 cada semana; 0x4F9430 lo usa para el título)
//   [+0x16]  s16 que se acerca a 0 de 11 en 11 cada día (oscila alrededor)
//   [+0x19]  u8 ciudad natal                [+0x1B] u8 (0x4F9430, título)
//   [+0x1E]  u8 que baja 1 cada semana hasta 0
//   [+0x1F]  u8 reputación entre los marineros: sube 1 al día hasta 20
//   [+0x22]  s16 que se acerca 1 al día a 20 × (rango en la ciudad natal
//            [7 → 8], menos el byte alto de [+0x16] si es positivo), en ±30000
//   [+0x24]  u8 ascensos del jugador fuera de su ciudad natal
//   [+0x25]  u8 se pone a 0 cada semana
//   [+0x31]  u8 ciudad del cónyuge (< nº de ciudades: casado)
//   [+0x32]  u8 reputación que da el cónyuge en la ciudad natal
//   [+0x37]  u8 ya ha optado a ser alderman (lo pone 0x4F75F0 al alcalde)
//   [+0x11C + 12·i] 3 floats por ciudad: componentes locales de la reputación
//            (los dos últimos bajan un 1 % al día)
//   [+0x2FC + 4·i] float reputación de hoy en la ciudad i
//   [+0x39C + i] u8 rango en la ciudad i (0 tendero ... 5 patricio, 6 alcalde, 7 alderman)
//   [+0x3C4 + 4·i] float reputación de ayer
//   [+0x464] float factor de la reputación base (1,0)
//   [+0x468] u32 un bit por ciudad (i & 31): sin él no se sube a los rangos 3,
//            4 y 5, y el alderman que deja de serlo vuelve al 2 y no al 5
//   [+0x46C] s32 valor de la compañía       [+0x470] s32 capacidad de sus barcos
//   [+0x474] u32 marineros                  [+0x478] u16 barcos que no están hundidos
//   [+0x47A] u16 estado medio de los barcos (‰)   [+0x47C] u32 barcos
//   [+0x480] gastos de la semana            [+0x488] ingresos de la semana
//   [+0x490] salarios de las tripulaciones de la semana   [+0x494] los mismos, acumulados
//   [+0x498] deudas (préstamos que debe)    [+0x4A8], [+0x4D0] otros gastos de la
//            semana; [+0x4D4] lo que de [+0x4A8] se avisa aparte
//
// Globales: [0x6DE52D] u8 el comerciante alderman (del mundo, [+0x8D]) y
// [0x6DE52E] u8 su ciudad (solo para el aviso); [0x6DF31C] u32 (si es 0,
// 0x4F75F0 llama a 0x5326E0, la candidatura a Gobernador, en alderman.cpp).
//
// No se traducen los avisos (0x4D6530, 0x548CA0), la interfaz (0x6CC7E8) ni
// los gráficos de la ciudad (0x6E51AC): no cambian la simulación. Las ramas
// que no salen en las 12 partidas (alderman, alcalde, rangos 6 y 7, cónyuge,
// barco de la ciudad, sin barcos ni oficinas...) se han comprobado con
// estados retocados al azar.

#include <cstring>
#include <vector>

#include "p3data/game_fixes.hpp"
#include "p3data/mail.hpp"
#include "p3data/statistics.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

namespace {

constexpr uint32_t kOfficeCount = global::kWorldOfficeCount;  // u16
constexpr uint32_t kTownCount = global::kWorldTownCount;  // u32
constexpr uint32_t kWorldRecordCount = 0x6DE4A6;  // u16 registros de 0x14 del mundo.
constexpr uint32_t kEventHead = 0x6DD73C + 0x8;  // u16 primer evento de la cola.
constexpr uint32_t kEventCount = 0x6DD73C + 0xC;  // u16 eventos.
constexpr uint32_t kAlderman = 0x6DE52D;  // u8 comerciante alderman (del mundo, [+0x8D]).
constexpr uint32_t kAldermanBusy = 0x6DF31C;  // u32 si no es 0, 0x4F75F0 no llama a 0x5326E0.

// Valor de una unidad interna de cada mercancía (0x673A18).
constexpr uint32_t kWareValueBits[24] = {0x3d6147af, 0x3ef50482, 0x3e6154ca, 0x3e322d0e, 0x3e11eb85, 0x3f0ccccd,
	0x3fb33333, 0x3f8ccccd, 0x3f845a1d, 0x40587ae1, 0x3ed33333, 0x3ce147af, 0x3fa39581, 0x3f8f5c29, 0x3ee147af,
	0x3e8e5604, 0x3ee147af, 0x3e6147af, 0x3f5ae147, 0x3d236e30, 0x41200000, 0x41000000, 0x41400000, 0x41600000};

// Reputación por inquilino según el alquiler de la oficina ([+0x2E4 + k]):
// 0x672DF0. Los niveles de alquiler van de 0 a 4; lo que sigue son otras
// constantes del ejecutable.
constexpr uint32_t kRentFactorBits[8] = {0x3f800000, 0x3f1eb852, 0x3ed70a3d, 0x3e6b851f, 0x0, 0x3fa66666,
	0x3f800000, 0x3f333333};

// Materiales de construcción de cada tipo de edificio (registros [0x75C] de
// la ciudad, tipos 0x00..0x38): madera (0x672A4C, cargas), herramientas de
// hierro (0x672A88, barriles), ladrillos (0x672AC4, cargas), cáñamo (0x672B00,
// cargas) y dinero (0x672B78, en centenas).
constexpr uint8_t kTimber[60] = {0x14, 0x1e, 0x01, 0x14, 0x0a, 0x05, 0x05, 0x14, 0x0a, 0x02, 0x14, 0x02, 0x14, 0x02,
	0x0a, 0x14, 0x14, 0x0a, 0x01, 0x01, 0x02, 0x14, 0x14, 0x14, 0x0a, 0x0a, 0x0a, 0x02, 0x02, 0x02, 0x14, 0x32, 0x32,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x02, 0x01, 0x1e, 0x32, 0x28, 0x1e, 0x00, 0x14, 0x0a, 0x14, 0x14, 0x14, 0x14,
	0x05, 0x05, 0x01, 0x02, 0x02, 0x00, 0x00, 0x00};
constexpr uint8_t kIronGoods[60] = {0x14, 0x14, 0x01, 0x14, 0x0a, 0x05, 0x05, 0x14, 0x05, 0x02, 0x14, 0x02, 0x14,
	0x02, 0x0a, 0x14, 0x14, 0x0a, 0x01, 0x01, 0x02, 0x14, 0x14, 0x14, 0x0a, 0x0a, 0x0a, 0x02, 0x02, 0x02, 0x14, 0x32,
	0x32, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x02, 0x01, 0x1e, 0x32, 0x28, 0x1e, 0x00, 0x14, 0x0a, 0x14, 0x14, 0x14,
	0x14, 0x05, 0x05, 0x01, 0x02, 0x02, 0x00, 0x00, 0x00};
constexpr uint8_t kBricks[60] = {0x32, 0x4b, 0x0a, 0x32, 0x28, 0x28, 0x28, 0x32, 0x28, 0x19, 0x32, 0x19, 0x32, 0x19,
	0x00, 0x32, 0x32, 0x28, 0x0a, 0x0a, 0x19, 0x32, 0x32, 0x32, 0x28, 0x28, 0x28, 0x19, 0x19, 0x19, 0x28, 0x50, 0x50,
	0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x19, 0x0a, 0x50, 0x50, 0x50, 0x50, 0x01, 0x32, 0x00, 0x14, 0x32, 0x14, 0x32,
	0x19, 0x19, 0x0a, 0x19, 0x19, 0x00, 0x00, 0x00};
constexpr uint8_t kHemp[60] = {0x00, 0x32, 0x00, 0x00, 0x0a, 0x14};  // El resto, 0.
constexpr uint8_t kMoney[60] = {0x82, 0x1e, 0x02, 0x28, 0x50, 0x32, 0x32, 0x50, 0x1e, 0x1e, 0x32, 0x1e, 0x50, 0x1e,
	0x64, 0x50, 0x50, 0x32, 0x0a, 0x0a, 0x1e, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x32, 0x32, 0x32, 0x64, 0x50, 0x50,
	0x28, 0x50, 0x28, 0x28, 0x50, 0x50, 0x32, 0x14, 0x46, 0xfa, 0x96, 0x14, 0x32, 0x28, 0x64, 0x19, 0x28, 0x19, 0x28,
	0x14, 0x14, 0x02, 0x04, 0x04, 0x00, 0x00, 0x00};

// Materiales para construir cada tipo de barco (0x66DEF0 + 0x60·tipo):
// [+0x0] madera, [+0x4] telas, [+0x8] herramientas de hierro, [+0xC] cáñamo,
// [+0x10] brea, [+0x14] (no se usa aquí), [+0x18] dinero.
constexpr int32_t kShipCost[4][7] = {
	{0xb, 3, 3, 3, 0x14, 0x18, 0x2260},
	{0x10, 5, 5, 5, 0x1e, 0x22, 0x4db2},
	{0x16, 3, 4, 4, 0x28, 0x35, 0x4452},
	{0x24, 0xa, 0xa, 8, 0x32, 0x45, 0x6108},
};

// Constantes float y double del ejecutable.
constexpr float kOne = 1.0f;  // 0x66BC80: reputación por poner el barco de la ciudad (outrigger)
constexpr float kFive = 5.0f;  // 0x66BC74
constexpr float kZero = 0.0f;  // 0x66BC6C
constexpr double kTenantRep = 0.003;  // 0x672F20 (double 0x3F689374BC6A7EFA)
constexpr float kEmploymentRep = 0.01f;  // 0x672838 (0x3C23D70A)
constexpr float kCapacityScale = 100000.0f;  // 0x672F18
constexpr float kValueRep = 1e-5f;  // 0x672F14 (0x3727C5AC)
constexpr float kDecay = 0.99f;  // 0x672DEC (0x3F7D70A4)
constexpr int16_t kStep16 = 11;  // 0x672DE6 (u16)
constexpr float kBarrel = 200.0f;  // 0x66A91C
constexpr float kLoad = 2000.0f;  // 0x66A918
constexpr float kMinusBarrel = -200.0f;  // 0x66F4BC
constexpr float kMinusLoad = -2000.0f;  // 0x66F4B8
// Reputación mínima para subir de cada rango (0..4): 0x672F0C, 0x672F08,
// 0x671FC8, 0x672F04 y 0x672F00.
constexpr float kRankReputation[5] = {7.0f, 12.0f, 20.0f, 40.0f, 60.0f};
// Valor de la compañía mínimo para subir de cada rango.
constexpr int32_t kRankValue[5] = {100000, 200000, 300000, 500000, 900000};
constexpr uint8_t kAldermanReputation = 120;  // 0x6737B8
constexpr double kTitleMoney = 200.0;  // 0x672F50
constexpr double kTitleFame = 120.0;  // 0x672F48

float bits_float(uint32_t bits) {
	float v;
	std::memcpy(&v, &bits, sizeof(v));
	return v;
}

float ware_value(int ware) {
	return bits_float(kWareValueBits[ware]);
}

// __ftol (0x639C28): trunca a 64 bits y devuelve los 32 bajos en eax (fuera de
// rango o NaN, el entero indefinido 0x8000000000000000, es decir, 0).
int32_t ftol32(double v) {
	if (!(v > -9223372036854775808.0 && v < 9223372036854775808.0)) {
		return 0;
	}
	return int32_t(uint32_t(uint64_t(int64_t(v))));
}

int32_t imul(int32_t a, int32_t b) {
	return int32_t(uint32_t(a) * uint32_t(b));
}

// Suma a un s32 de la memoria con desbordamiento (add de 32 bits).
void add32(int32_t& field, int32_t v) {
	field = int32_t(uint32_t(field) + uint32_t(v));
}

}  // namespace

// Lo que se expone para las sondas (tools/oracle/probes_merchants.cpp).
namespace merchants_detail {

// 0x4D9080 sobre la cola de eventos (0x6DD73C): recorre los eventos de tipo 1
// (préstamos: [+0x8] importe, [+0x10] u8 prestamista, [+0x11] u8 deudor) y
// deja en `value` lo prestado menos lo que debe y en `debt` lo que debe.
void fn_4d9080(SaveGame& save, uint32_t merchant, int32_t& value, int32_t& debt) {
	debt = 0;
	value = 0;
	uint32_t e = save.globals.u16(kEventHead);
	while (e < save.globals.u16(kEventCount)) {
		const GameMemory<0x18>& ev = save.events[e];
		if (ev.u16(0x6) == 1) {
			if (ev.u8(0x11) == merchant) {
				add32(debt, ev.s32(0x8));
				value = int32_t(uint32_t(value) - uint32_t(ev.s32(0x8)));
			} else if (ev.u8(0x10) == merchant) {
				add32(value, ev.s32(0x8));
			}
		}
		e = ev.u16(0x4);
	}
}

// 0x52A7D0: valor de un barco: lo que cuestan los materiales y el dinero de
// construirlo (tabla de su tipo) por su estado ([+0x18] / [+0x14]). La llama
// 0x4F7BB0 con ecx = la ciudad natal, que no se usa.
int32_t fn_52a7d0(const SaveGame& save, int ship) {
	const ShipMemory& s = save.ships[size_t(ship)].memory;
	const uint32_t type = s.u8(0xE);
	// Con un tipo > 3 el juego lee los campos del propio barco como tabla.
	auto cost = [&](size_t o) -> int32_t { return type <= 3 ? kShipCost[type][o / 4] : s.s32(o); };
	int64_t sum = imul(ftol32(double(ware_value(15)) * double(kBarrel)), cost(0x10));  // brea
	sum += imul(ftol32(double(ware_value(17)) * double(kLoad)), cost(0xC));  // cáñamo
	sum += imul(ftol32(double(ware_value(12)) * double(kBarrel)), cost(0x8));  // herramientas de hierro
	sum += imul(ftol32(double(ware_value(8)) * double(kBarrel)), cost(0x4));  // telas
	sum += imul(ftol32(double(ware_value(11)) * double(kLoad)), cost(0x0));  // madera
	sum += cost(0x18);  // dinero
	// _allmul y _alldiv (64 bits con signo).
	const int64_t product = int64_t(uint64_t(sum) * uint64_t(int64_t(s.s32(0x18))));
	const int64_t divisor = s.s32(0x14);
	if (divisor == 0) {
		return 0;  // El juego fallaría (división por cero).
	}
	return int32_t(uint32_t(uint64_t(product / divisor)));
}

// 0x517F70: salario diario de la tripulación de un barco: el del capitán
// ([+0xC] del capitán [+0x42]) más 2 por marinero ([+0x40]); sin capitán, 2
// más si lleva algún marinero.
uint32_t fn_517f70(const SaveGame& save, int ship) {
	const ShipMemory& s = save.ships[size_t(ship)].memory;
	const uint16_t captain = s.u16(0x42);
	if (captain < save.globals.u16(global::kCaptainCount)) {
		return uint32_t(save.captains[captain].u16(0xC)) + 2u * s.u16(0x40);
	}
	return (s.u16(0x40) > 0 ? 2u : 0u) + 2u * s.u16(0x40);
}

// 0x51F220 sobre la ciudad: valor de un edificio (registro [0x75C]): sus
// materiales de construcción y su dinero. 0 si el registro no existe.
int32_t fn_51f220(const SaveGame& save, int town, uint32_t record) {
	const SavedTown& t = save.towns[size_t(town)];
	if (record >= t.memory.u16(0x776)) {
		return 0;
	}
	const uint32_t type = t.records[record].type;
	uint32_t base;
	if (type == 5 && save.towns[t.memory.u8(0x2C0)].memory.u32(0x2CC) == 0) {
		// Tipo 5 en una ciudad con [0x2CC] = 0: la mitad del cáñamo y el
		// dinero ×60.
		const int32_t hemp = int32_t(uint32_t(kHemp[5]) * 2000u) >> 1;
		base = uint32_t(ftol32(double(float(hemp)) * double(ware_value(17)))) + uint32_t(kMoney[5]) * 60u;
	} else {
		base = uint32_t(kMoney[type]) * 100u;
		if (kHemp[type] > 0) {
			base += uint32_t(ftol32(double(float(int32_t(kHemp[type]) * 2000)) * double(ware_value(17))));
		}
	}
	const int32_t timber = ftol32(double(float(int32_t(kTimber[type]) * 2000)) * double(ware_value(11)));
	uint32_t sum = uint32_t(ftol32(double(float(int32_t(kBricks[type]) * 2000)) * double(ware_value(19)))) + base;
	const int32_t iron = ftol32(double(float(int32_t(kIronGoods[type]) * 200)) * double(ware_value(12)));
	sum += uint32_t(timber);
	return int32_t(uint32_t(iron) + sum);
}

// 0x500330 sobre una oficina: su valor para el comerciante: el del propio
// edificio de la oficina (salvo en la ciudad natal, `hometown`), el de sus
// edificios en la ciudad (listas [+0x2CE] y [+0x2D0] de registros [0x75C]) y
// el de sus existencias.
int32_t fn_500330(const SaveGame& save, int office, uint32_t hometown) {
	const OfficeMemory& o = save.offices[size_t(office)].memory;
	const uint32_t town = o.u8(0x2C6);
	uint32_t value;
	if (hometown == town) {
		value = 0;
	} else {
		// Los materiales del tipo 0 (la oficina), con constantes negativas.
		const int32_t iron = ftol32(double(float(int32_t(kIronGoods[0]))) * double(ware_value(12)) * double(kMinusBarrel));
		value = uint32_t(ftol32(double(float(int32_t(kTimber[0]))) * double(ware_value(11)) * double(kLoad))) -
			uint32_t(iron);
		value -= uint32_t(ftol32(double(float(int32_t(kBricks[0]))) * double(ware_value(19)) * double(kMinusLoad)));
		value += uint32_t(kMoney[0]) * 100u;
	}
	const SavedTown& t = save.towns[town];
	for (size_t list : {size_t(0x2CE), size_t(0x2D0)}) {
		uint32_t r = o.u16(list);
		do {
			value += uint32_t(fn_51f220(save, int(town), r));
			r = r < t.memory.u16(0x776) ? t.records[r].next : 0xFFFF;
		} while (r < 0xFFFF);
	}
	for (int w = 0; w < 24; ++w) {
		const int32_t stock = o.s32(0x4 + 4 * size_t(w));
		if (stock > 0) {
			value += uint32_t(ftol32(double(float(stock)) * double(ware_value(w))));
		}
	}
	return int32_t(value);
}

// 0x4F9430: título del comerciante en los avisos: según [+0x15] (más uno por
// cada 40000 de valor de la compañía) o [+0x1B], lo que pese más. Solo lo usan
// los avisos (que no se traducen).
uint8_t fn_4f9430(const SaveGame& save, int merchant) {
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	int32_t money = m.u8(0x15);
	const int32_t value = m.s32(0x46C);
	if (value >= 40000) {
		money += value / 40000;
	}
	const int32_t fame = m.u8(0x1B);
	if (!(double(fame) / kTitleFame <= double(money) / kTitleMoney)) {
		if (fame < 5) {
			return 0;
		}
		if (fame < 10) {
			return 8;
		}
		if (fame < 20) {
			return 9;
		}
		if (fame < 35) {
			return 10;
		}
		if (fame < 65) {
			return 11;
		}
		if (fame < 90) {
			return 12;
		}
		return fame >= 120 ? 14 : 13;
	}
	if (money < 10) {
		return 0;
	}
	if (money < 20) {
		return 1;
	}
	if (money < 30) {
		return 2;
	}
	if (money < 50) {
		return 3;
	}
	if (money < 100) {
		return 4;
	}
	if (money < 150) {
		return 5;
	}
	return money >= 200 ? 7 : 6;
}

// 0x4F7510: al jugador humano que no asciende en su ciudad natal por el valor
// de la compañía le avisa (mensajes 0x5E y 0x5F con el rango `rank`, 0x4D6530)
// si no tiene ya ese aviso en el buzón (0x4D7AC0 sobre 0x6DD730). Nada de esto
// cambia la simulación: solo comprueba las condiciones. `me` es el comerciante
// (el dueño de su primera oficina).
void fn_4f7510(SaveGame& save, int merchant, uint32_t me, uint32_t town, uint32_t rank) {
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	(void)me;
	(void)rank;
	if (town != m.u8(0x19) || m.u16(0x8) != 0) {
		return;
	}
	// El buzón de los avisos (0x4D7AC0 sobre 0x6DD730, que SaveGame no tiene)
	// y los avisos 0x5E y 0x5F no se traducen.
}

// 0x4F75F0: rango del comerciante en una ciudad (lo llama 0x4F7BB0 con la
// natal el día 1 de cada mes y cada día con otras dos): el alderman tiene
// el 7 en todas, el alcalde el 6 en la suya y, si no, sube un rango si tiene
// la reputación y el valor de la compañía que pide el siguiente.
void fn_4f75f0(SaveGame& save, int merchant, uint32_t town) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	// El comerciante, como dueño de su primera oficina (0 si no tiene).
	const uint16_t first = m.u16(0xC);
	const uint8_t me = first < save.globals.u16(kOfficeCount) ? save.offices[first].memory.u8(0x2C4) : 0;
	if (save.globals.u8(kAlderman) == me) {
		for (uint32_t i = 0; i < save.globals.u32(kTownCount); ++i) {
			m.u8(0x39C + i) = 7;
		}
		return;
	}
	const TownMemory& t = save.towns[town].memory;
	if (t.u8(0x6F1) == me) {
		// El alcalde de la ciudad ([0x6F1]).
		m.u8(0x39C + town) = 6;
		if (m.u8(0x37) != 0) {
			return;
		}
		// Reputación para optar a alderman: el doble de la de la ciudad 0
		// menos sus tres componentes, más el último componente de todas y los
		// otros dos de las que tienen [0x2C8] & 0xC0; hace falta 120 (0x6737B8).
		float f = m.f32(0x2FC);
		f = float(double(f) - double(m.f32(0x11C)));
		f = float(double(f) - double(m.f32(0x124)));
		f = float(double(f) - double(m.f32(0x120)));
		f = float(double(f) + double(f));
		const int32_t count = int32_t(save.globals.u32(kTownCount));
		for (int32_t i = count - 1; count > 0 && i >= 0; --i) {
			f = float(double(f) + double(m.f32(0x124 + 12 * size_t(i))));
			if (save.towns[size_t(i)].memory.u32(0x2C8) & 0xC0) {
				f = float(double(f) + double(m.f32(0x11C + 12 * size_t(i))));
				f = float(double(f) + double(m.f32(0x120 + 12 * size_t(i))));
			}
		}
		if (int16_t(kAldermanReputation) > int16_t(ftol32(f))) {
			return;
		}
		m.u8(0x37) = 1;
		if (save.globals.u32(kAldermanBusy) == 0) {
			// La candidatura (alderman.cpp): encola los encargos de la Hansa.
			fn_5326e0(save);
		}
		// Al jugador humano, el aviso 0x6D (con 0x4F9430 y [0x6DE52E]): no se traduce.
		return;
	}
	const bool rights = (m.u32(0x468) & (1u << (town & 31))) != 0;
	const uint8_t rank = m.u8(0x39C + town);
	switch (rank) {
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
		if (rank >= 2 && !rights) {
			return;
		}
		if (!(m.f32(0x2FC + 4 * size_t(town)) >= kRankReputation[rank])) {
			return;
		}
		if (m.s32(0x46C) < kRankValue[rank]) {
			fn_4f7510(save, merchant, me, town, rank + 1u);
			return;
		}
		m.u8(0x39C + town) = uint8_t(rank + 1);
		if (rank == 0 && town != m.u8(0x19) && m.u16(0x8) == 0) {
			// El jugador humano asciende a comerciante fuera de su ciudad
			// natal: se cuenta y se le avisa (0x82, con 0x4F9430; no se traduce).
			m.u8(0x24) = uint8_t(m.u8(0x24) + 1);
		}
		return;
	case 6:
		m.u8(0x39C + town) = 5;  // Ya no es alcalde.
		return;
	case 7:
		m.u8(0x39C + town) = rights ? 5 : 2;  // Ya no es alderman.
		return;
	default:
		return;
	}
}

}  // namespace merchants_detail

using namespace merchants_detail;

// 0x4F7BB0: tick diario de un comerciante: contabilidad semanal, valor de la
// compañía, reputación en cada ciudad y rangos.
void fn_4f7bb0(SaveGame& save, int merchant) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const uint32_t id = uint32_t(merchant);
	const uint32_t tick = save.globals.u32(global::kWorldTick);
	// La reputación de ayer y la de hoy, que se recalcula desde 0.
	std::memcpy(m.bytes.data() + 0x3C4, m.bytes.data() + 0x2FC, 0xA0);
	std::memset(m.bytes.data() + 0x2FC, 0, 0xA0);

	if ((tick >> 8) % 7 == 0) {
		// Cada semana: ingresos menos gastos al dinero.
		m.u8(0x25) = 0;
		if (m.u8(0x15) > 200) {
			m.u8(0x15) = 200;
		}
		const uint32_t balance = m.u32(0x488) - m.u32(0x480) - m.u32(0x490) - m.u32(0x4A8) - m.u32(0x4D0);
		m.u32(0x0) += balance;
		// A human merchant gets the week's accounts by letter (0x4D6530), each
		// with the home town in [+0x5]: 0x0F with [+0x490] and [+0x480] if
		// either is above 0, 0x10 with [+0x488] and [+0x4D0], 0x11 (the taxes)
		// with [+0x4A8] − [+0x4D4] and [+0x4D4] if [+0x4A8] is.
		if (m.u16(8) == 0) {
			auto accounts = [&](uint8_t type, uint32_t first, uint32_t second) {
				Letter letter(type);
				letter.entry.u8(5) = m.u8(0x19);
				letter.entry.u32(8) = first;
				letter.entry.u32(0xC) = second;
				send_letter(save, int32_t(id & 0xFFFF), letter);
			};
			if (m.s32(0x490) > 0 || m.s32(0x480) > 0) {
				accounts(0x0F, m.u32(0x490), m.u32(0x480));
			}
			if (m.s32(0x488) > 0 || m.s32(0x4D0) > 0) {
				accounts(0x10, m.u32(0x488), m.u32(0x4D0));
			}
			if (m.s32(0x4A8) > 0) {
				accounts(0x11, m.u32(0x4A8) - m.u32(0x4D4), m.u32(0x4D4));
			}
		}
		m.u32(0x480) = 0;
		m.u32(0x490) = 0;
		m.u32(0x488) = 0;
		m.u32(0x4A8) = 0;
		m.u32(0x4D0) = 0;
		m.u32(0x4D4) = 0;
	}

	// Valor de la compañía: préstamos, barcos con su carga, oficinas y dinero.
	fn_4d9080(save, id & 0xFFFF, m.s32(0x46C), m.s32(0x498));
	m.u32(0x470) = 0;
	m.u32(0x474) = 0;
	m.u16(0x478) = 0;
	m.u32(0x47C) = 0;
	uint32_t max_health = 0;
	uint32_t health = 0;
	for (uint32_t ship = m.u16(0xE); ship < save.globals.u16(global::kShipCount);
		ship = save.ships[ship].memory.u16(0x4)) {
		const ShipMemory& s = save.ships[ship].memory;
		m.u32(0x47C) += 1;
		const uint16_t status = s.u16(0x134);
		if (status == 0x11 || status == 0xE) {
			continue;  // Hundido (0x11) o 0xE.
		}
		m.u16(0x478) = uint16_t(m.u16(0x478) + 1);
		m.u32(0x470) += s.u32(0x10);
		health += s.u32(0x18);
		max_health += s.u32(0x14);
		m.u32(0x474) += s.u16(0x40);
		for (int w = 23; w >= 0; --w) {
			const int32_t cargo = s.s32(0x54 + 4 * size_t(w));
			if (cargo > 0) {
				add32(m.s32(0x46C), ftol32(double(float(cargo)) * double(ware_value(w))));
			}
		}
		add32(m.s32(0x46C), fn_52a7d0(save, int(ship)));
		const uint32_t wages = fn_517f70(save, int(ship));
		m.u32(0x490) += wages;
		m.u32(0x494) += wages;
	}
	// Estado medio de los barcos, en milésimas.
	m.u16(0x47A) = 0;
	int32_t total = int32_t(max_health);
	int32_t current = int32_t(health);
	while (total > 0x20C49B) {
		total /= 10;
		current /= 10;
	}
	if (total > 0) {
		m.u16(0x47A) = uint16_t(imul(current, 1000) / total);
	} else {
		m.u16(0x47A) = 1000;
	}

	const uint32_t towns = save.globals.u32(kTownCount);
	// Reputación por poner el barco de la ciudad ([0x994], el outrigger).
	for (uint32_t i = 0; i < towns; ++i) {
		const uint32_t ship = save.towns[i].memory.u16(0x994);
		if (ship < save.globals.u16(global::kShipCount) && uint16_t(save.ships[ship].memory.u8(0x0)) == uint16_t(id)) {
			float& rep = m.f32(0x2FC + 4 * size_t(i));
			rep = float(double(rep) + double(kOne));
		}
	}
	// Oficinas: su valor y la reputación por inquilinos y por empleados.
	for (uint32_t office = m.u16(0xC); int32_t(office) < int32_t(save.globals.u16(kOfficeCount));
		office = save.offices[office].memory.u16(0x2C8)) {
		const OfficeMemory& o = save.offices[office].memory;
		add32(m.s32(0x46C), fn_500330(save, int(office), m.u8(0x19)));
		float& rep = m.f32(0x2FC + 4 * size_t(o.u8(0x2C6)));
		for (size_t k = 0; k < 3; ++k) {
			// Inquilinos [+0x2DE + 2·k] (ricos, acomodados, pobres) según el
			// alquiler [+0x2E4 + k].
			double x = double(bits_float(kRentFactorBits[o.u8(0x2E4 + k)])) * double(o.u16(0x2DE + 2 * k));
			x = x * kTenantRep;
			rep = float(x + double(rep));
		}
		// Sus empresas (registros de 0x14 del mundo): trabajadores [+0x4].
		for (uint32_t r = o.u16(0x2CC); r < save.globals.u16(kWorldRecordCount); r = save.world_records[r].u16(0x8)) {
			const float workers = float(save.world_records[r].u16(0x4));
			rep = float(double(workers) * double(kEmploymentRep) + double(rep));
		}
	}
	add32(m.s32(0x46C), m.s32(0x0));
	const int32_t value = m.s32(0x46C);
	if (m.u8(0x1E) != 0 && (tick >> 8) % 7 == 0) {
		m.u8(0x1E) = uint8_t(m.u8(0x1E) - 1);
	}

	// Reputación en todas las ciudades por la capacidad de los barcos y el
	// valor de la compañía (cada una hasta 5).
	float capacity = float(double(float(m.s32(0x470))) / double(kCapacityScale));
	if (capacity > kFive) {
		capacity = 5.0f;
	}
	const float worth = float(double(float(value)) * double(kValueRep));
	float base = float(worth > kFive ? double(capacity) + double(kFive) : double(worth) + double(capacity));
	base = float(double(base) * double(m.f32(0x464)));
	for (uint32_t i = 0; i < towns; ++i) {
		float& rep = m.f32(0x2FC + 4 * size_t(i));
		rep = float(double(base) + double(rep));
	}
	// El cónyuge, en la ciudad natal.
	if (m.u8(0x31) < towns) {
		float& rep = m.f32(0x2FC + 4 * size_t(m.u8(0x19)));
		rep = float(double(float(m.u8(0x32))) + double(rep));
	}
	// Los componentes locales: los dos últimos bajan un 1 %; la reputación,
	// nunca negativa.
	for (uint32_t i = 0; i < towns; ++i) {
		float& rep = m.f32(0x2FC + 4 * size_t(i));
		const size_t c = 0x11C + 12 * size_t(i);
		m.f32(c + 4) = float(double(kDecay) * double(m.f32(c + 4)));
		const float last = float(double(kDecay) * double(m.f32(c + 8)));
		double x = double(last) + double(m.f32(c));
		m.f32(c + 8) = last;
		x = x + double(rep);
		x = x + double(m.f32(c + 4));
		rep = float(x);
		if (rep < kZero) {
			rep = 0.0f;
		}
	}

	if (save.globals.u8(global::kWorldDay) == 1) {
		// El día 1 de cada mes, el rango en la ciudad natal.
		const uint8_t before = m.u8(0x39C + m.u8(0x19));
		fn_4f75f0(save, merchant, m.u8(0x19));
		// If a human merchant goes up (to a rank below 6), the chronicle gets
		// the letter 0x06 (0x4D6530 to −2): the home town, the ranks before
		// and after, the merchant and his title (0x4F9430). The message to
		// the player (0x548CA0), the window (0x6CC7E8: 0x469380, 0x469AF0) and
		// the town's drawing (0x6E51AC: 0x5308A0, 0x590680, 0x5891C0) are the
		// interface.
		const uint8_t home = m.u8(0x19);
		const uint8_t after = m.u8(0x39C + home);
		if (m.u16(8) == 0 && before < after && after < 6) {
			Letter letter(0x06);
			letter.entry.u8(5) = home;
			letter.entry.u16(8) = before;
			letter.entry.u16(0xA) = after;
			letter.entry.u16(0xC) = uint16_t(id);
			letter.entry.u16(0xE) = merchants_detail::fn_4f9430(save, merchant);
			send_letter(save, kLetterToChronicle, letter);
		}
	}
	// Cada día, el rango en otras dos ciudades (enfrentadas en el orden).
	uint32_t other = (uint32_t(save.globals.u16(global::kWorldDayOfYear)) + (id & 0xFFFF)) % towns;
	if (other != m.u8(0x19)) {
		fn_4f75f0(save, merchant, other);
	}
	other += towns >> 1;
	if (!(towns > other)) {
		other -= towns;
	}
	if (other != m.u8(0x19)) {
		fn_4f75f0(save, merchant, other);
	}

	if (m.u8(0x1F) < 20) {
		m.u8(0x1F) = uint8_t(m.u8(0x1F) + 1);
	}
	const int16_t v = m.s16(0x16);
	m.s16(0x16) = int16_t(v > 0 ? uint16_t(v) - uint16_t(kStep16) : uint16_t(v) + uint16_t(kStep16));
	int32_t target = m.u8(0x39C + m.u8(0x19));
	if (target > 6) {
		target = target + target - 6;
	}
	if (m.s8(0x17) > 0) {
		target -= m.s8(0x17);
	}
	target *= 20;
	// Unreachable: |target| <= 20 × 127.
	if (target > 30000) {
		target = 30000;
	} else if (target < -30000) {
		target = -30000;
	}
	const int16_t w = m.s16(0x22);
	m.s16(0x22) = int16_t(int32_t(w) > target ? uint16_t(w) - 1u : uint16_t(w) + 1u);
}

namespace events_towns_detail {
void fn_4d6780(SaveGame& save, int32_t merchant);
}  // namespace events_towns_detail

namespace merchants_detail {

// 0x4F72C0 (on the merchant): his statistics and reputations start again:
// [+0x118] = 1, zeros in [+0x11C..+0x2FB], [+0x2FC..+0x39B], [+0x39C..+0x3C3],
// [+0x3C4..+0x463] and [+0x468..+0x4D7] (but [+0x47C] and [+0x47E]), and
// [+0x464] = 1.0.
void fn_4f72c0(MerchantMemory& m) {
	m.u32(0x118) = 1;
	std::memset(m.bytes.data() + 0x11C, 0, 0x1E0);
	std::memset(m.bytes.data() + 0x2FC, 0, 0xA0);
	std::memset(m.bytes.data() + 0x3C4, 0, 0xA0);
	std::memset(m.bytes.data() + 0x39C, 0, 0x28);
	for (size_t o = 0x468; o < 0x478; o += 4) {
		m.u32(o) = 0;
	}
	m.u16(0x478) = 0;
	m.u16(0x47A) = 0;
	for (size_t o = 0x480; o < 0x4D8; o += 4) {
		m.u32(o) = 0;
	}
	m.f32(0x464) = 1.0f;
}

// 0x4F68D0 (on the merchant, with the tick, a1, the merchant (unused), a3 and
// a4): a new life: born a1 years ago (a3 != 0) or (a1 & 31) + 25 (a3 = 0), at
// tick % 365 of that year ([+0x10], in ticks); [+0x18] = his home town;
// [+0x1A] = a4 != 0; unmarried ([+0x28], [+0x2C] = −1, [+0x27], [+0x30],
// [+0x31] = 0xFF, [+0x34] = 0xFFFF) and the rest of his person to defaults.
void fn_4f68d0(SaveGame& save, MerchantMemory& m, uint32_t tick, uint32_t a1, uint8_t a3, uint8_t a4) {
	uint16_t year = save.globals.u16(global::kWorldYear);
	if (a3 != 0) {
		// Checked with the oracle: scenario bankruptcy (saves 3 and 5, a direct call).
		year = uint16_t(year - uint16_t(a1));
	} else {
		year = uint16_t(year - (a1 & 0x1F) - 0x19);
	}
	m.u32(0x10) = (uint32_t(year) * 365 + tick % 365) << 8;
	m.u8(0x18) = m.u8(0x19);
	m.u8(0x1A) = a4 != 0 ? 1 : 0;
	m.u32(0x28) = 0xFFFFFFFF;
	m.u32(0x2C) = 0xFFFFFFFF;
	for (size_t o : {0x32, 0x33, 0x36, 0x25, 0x37, 0x1C, 0x1D, 0x1E, 0x1B, 0x20, 0x21, 0x24}) {
		m.u8(o) = 0;
	}
	m.u16(0x14) = 0;
	m.u16(0x16) = 0;
	m.u32(4) = 0;
	m.u8(0x27) = 0xFF;
	m.u8(0x30) = 0xFF;
	m.u8(0x31) = 0xFF;
	m.u16(0x34) = 0xFFFF;
	m.u8(0x1F) = 0xF;
	m.u16(0x22) = 1;
	m.u8(0xE0) = 0xFF;
	m.u8(0xE1) = 0xFF;
	m.u16(0xE2) = 0xFFFF;
}

// 0x4F5D80 (on the merchant; operation 0x65): an AI merchant goes bankrupt
// and becomes a new merchant (a human one, nothing). His debts (events 0x01):
// owed to him, now owed to the town ([+0x10] = 0xFF); owed by him, to nobody
// ([+0x11] = [+0xC] = 0xFF). Then: [+0x53C] = 800000, [+0x540], [+0x544],
// [+0x608] = 0, [+0x546] = the tick's low byte and [+0x547] = 7 times it,
// zeros in [+0x548..+0x607]; statistics (0x4F72C0) with [+0x464] = 2.0; no
// expeditions ([+0x4D8..+0x537], 0x534890); 1000000 coins; a new life
// (0x4F68D0); [+0x26] = 0 and [+0x38..+0x3F] = 5; his old letters (0x4D6780);
// a first name day % men's names ([0x6DDB70]) and a surname
// ([0x6DD888] + day) % (the surname index of the last merchant: the game
// divides by that and not by the number of surnames), going up past the
// ones in use (back to 0 after that divisor); and he stops being mayor of his
// home town (game bugs, all three fixed with fix_game_bugs(): see below). His
// ships, offices and buildings stay. The records for the
// graphs (0x54E1F0, 0x54E170: heap, from [0x6DFCAC]) are not reproduced.
void fn_4f5d80(SaveGame& save, int merchant) {
	GlobalMemory& g = save.globals;
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	if (m.u16(8) == 0) {
		return;  // Checked with the oracle: scenario bankruptcy (saves 3 and 5; operation 0x65).
	}
	remove_merchant_statistics(save, merchant);  // 0x54E1F0 (at 0x4F5DA5): out of the statistics
	const uint16_t me = uint16_t(merchant);
	for (uint32_t i = g.u16(0x6DD744); i < g.u16(0x6DD748);) {
		GameMemory<0x18>& e = save.events[i];
		i = e.u16(4);
		if (e.u16(6) != 1) {
			continue;
		}
		if (e.u8(0x10) == me) {
			e.u8(0x10) = 0xFF;
		} else if (e.u8(0x11) == me) {
			e.u8(0x11) = 0xFF;
			e.u8(0xC) = 0xFF;
		}
	}
	const uint32_t now = g.u32(global::kWorldTick);
	m.u32(0x53C) = 800000;
	m.u32(0x540) = 0;
	m.u16(0x544) = 0;
	m.u32(0x608) = 0;
	m.u8(0x546) = uint8_t(now);
	m.u8(0x547) = uint8_t(uint8_t(now) * 7);
	std::memset(m.bytes.data() + 0x548, 0, 0x60);
	std::memset(m.bytes.data() + 0x5A8, 0, 0x60);
	fn_4f72c0(m);
	std::memset(m.bytes.data() + 0x4D8, 0, 0x60);
	m.f32(0x464) = 2.0f;
	m.s32(0) = 1000000;
	fn_4f68d0(save, m, now, now & 3, 0, 1);
	m.u8(0x26) = 0;
	std::memset(m.bytes.data() + 0x38, 5, 8);
	events_towns_detail::fn_4d6780(save, merchant);
	const uint32_t day = g.u16(global::kWorldDayOfYear);
	m.u8(0x1C) = uint8_t(int32_t(day) % int32_t(g.u16(0x6DDB70)));
	const uint32_t surnames = g.u16(0x6DDB74);
	const uint32_t count = g.u16(global::kWorldMerchantCount);
	if (fix_game_bugs()) {
		// The fix: modulo the number of surnames (at most 256: [+0x1D] is a
		// byte), without counting the merchant himself (his surname is
		// already 0) and going round once at most (if all are taken, the
		// first one).
		const uint32_t n = surnames < 256 ? surnames : 256;
		if (n > 0) {
			std::vector<uint8_t> taken(n, 0);
			for (uint32_t k = 0; k < count; ++k) {
				const uint32_t other = save.merchants[k].memory.u8(0x1D);
				if (k != uint32_t(merchant) && other < n) {
					taken[other] = 1;
				}
			}
			const uint32_t first = (uint32_t(g.u16(0x6DD888)) + day) % n;
			uint32_t surname = first;
			for (uint32_t tries = 0; tries < n && taken[surname]; ++tries) {
				surname = (surname + 1) % n;
			}
			m.u8(0x1D) = uint8_t(taken[surname] ? first : surname);
		}
		add_merchant_statistics(save, merchant);  // 0x54E170 (at 0x4F5F8E): a new statistics object
		TownMemory& home = save.towns[m.u8(0x19)].memory;
		if (home.u8(0x6F1) == me) {
			home.u8(0x6F1) = 0xFF;
		}
		return;
	}
	std::vector<uint8_t> used(surnames, 0);
	uint32_t divisor = 0;
	for (uint32_t k = 0; k < count; ++k) {
		divisor = save.merchants[k].memory.u8(0x1D);
		if (divisor < surnames) {
			used[divisor] = 1;
		}
	}
	if (divisor == 0) {
		return;  // Game bug: the game divides by zero here and fails (scenario
		// bankruptcy_zero, saves 3 and 5: an exception at 0x4F5F57); p3vita can't fail, so
		// it returns.
	}
	uint32_t surname = (uint32_t(g.u16(0x6DD888)) + day) % divisor;
	m.u8(0x1D) = uint8_t(surname);
	// Game bug: the merchant's own surname is already 0 (0x4F68D0 above), so if
	// every surname from 0 to the divisor is taken the search never ends, in the
	// game and here (reproduced as it is).
	while (surname < surnames && used[surname]) {
		// (Past the number of surnames the game reads beyond its buffer.)
		surname = m.u8(0x1D) + 1u;
		m.u8(0x1D) = uint8_t(surname);
		if ((surname & 0xFF) > divisor) {
			m.u8(0x1D) = 0;  // Checked with the oracle: scenario bankruptcy (saves 3 and 5; operation 0x65).
		}
		surname = m.u8(0x1D);
	}
	add_merchant_statistics(save, merchant);  // 0x54E170 (at 0x4F5F8E): a new statistics object
	TownMemory& home = save.towns[m.u8(0x19)].memory;
	if (home.u8(0x6F1) == me) {
		home.u8(0x6F1) = 0xFF;  // Checked with the oracle: scenario bankruptcy (saves 3 and 5; operation 0x65).
	}
}

}  // namespace merchants_detail

}  // namespace p3::data::tick
