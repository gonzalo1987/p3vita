// Tick de las instalaciones (0x5101D0) y producción de las empresas (tipos 4..20).
//
// Una instalación es un `struct facility` de 16 bytes; las de la ciudad están
// en [0x840 + 16·k] (k = 0..20, una por tipo) y las empresas de las oficinas
// en los registros del mundo (*0x6DE510), que producen con estas mismas
// funciones (0x4D3780, offices.cpp):
//   [+0x0] u32  eficiencia (1024 = 100 %; multiplica la producción por trabajador)
//   [+0x4] u16  trabajadores
//   [+0x6] u8   tipo (0 milicia, 1 astillero, 2, 3 armero, 4..20 empresas)
//   [+0x7] u8   ciudad (índice)
//   [+0x8] s16  (1024 o 768; no se usa aquí)
//   [+0xA] u16  trabajadores de reserva: despedidos que siguen ligados a la
//               instalación; se vuelven a contratar antes que a nadie
//   [+0xC] f32  en las ciudades, lo acumulado de la reserva que se va a la
//               ciudad (0x5109B0); en las oficinas, [+0xE] u16 son los
//               trabajadores como mucho
// Cada empresa produce una mercancía (0x672C2C) y algunas consumen otras del
// almacén (de la ciudad o de la oficina); si falta alguna, se produce menos y
// se pone a 1 el u16 [0x0] del almacén. Lo producido se apunta en la
// producción diaria [0xC4 + 4·w] y en la serie de 8 días [0x13C + 16·w +
// 2·día], y lo consumido en el consumo de las empresas [0x64 + 4·w]. En
// [0x490 + 4·w] de la ciudad [+7] (que el tick pone a cero antes de las
// instalaciones) se suma lo que produciría con `max_workers` trabajadores,
// que entra en los umbrales de precio.

#include "p3data/game_fixes.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

// Las funciones internas, con nombre para las sondas.
namespace businesses_detail {

uint32_t fn_510120(const SaveGame& save, const FacilityMemory& fc);
void fn_5109b0(SaveGame& save, FacilityMemory& fc, uint32_t count);
void fn_5108e0(SaveGame& save, FacilityMemory& fc, uint32_t& count);

}  // namespace businesses_detail

namespace {

using namespace businesses_detail;

// Mercancía que produce cada tipo de instalación (0x672C2C).
constexpr uint8_t kFacilityWare[24] = {0x06, 0x06, 0x06, 0x14, 0x09, 0x02, 0x03, 0x0C, 0x05, 0x00, 0x01, 0x0B, 0x08,
	0x04, 0x10, 0x0E, 0x07, 0x12, 0x13, 0x0F, 0x11, 0x00, 0x00, 0x00};
// Trabajadores como mucho por tipo de instalación (0x6735E8).
constexpr uint8_t kMaxWorkers[24] = {0xFA, 0x28, 0x19, 0x05, 0x48, 0x41, 0x44, 0x4E, 0x3C, 0x44, 0x3C, 0x30, 0x3C,
	0x36, 0x46, 0x32, 0x5D, 0x3A, 0x1B, 0x1A, 0x3E, 0x00, 0x00, 0x00};
constexpr float kReserveLeaving = 0.02f;  // 0x673600: parte de la reserva que se va cada vez que se despide.
constexpr uint32_t kWorkersList = 0x6DD628;  // Instalaciones que buscan trabajadores (0x4D4A40).

// Mercancías (wares.hpp).
constexpr int kGrain = 0x00, kMeat = 0x01, kFish = 0x02, kBeer = 0x03, kSalt = 0x04, kHoney = 0x05, kWine = 0x07,
	kCloth = 0x08, kSkins = 0x09, kWhaleOil = 0x0A, kTimber = 0x0B, kIronGoods = 0x0C, kLeather = 0x0D, kWool = 0x0E,
	kPitch = 0x0F, kPigIron = 0x10, kHemp = 0x11, kPottery = 0x12, kBricks = 0x13;

// Campos por mercancía del almacén y de la ciudad.
constexpr size_t stock(int w) { return 4 + 4 * size_t(w); }  // Existencias.
constexpr size_t consumption(int w) { return 0x64 + 4 * size_t(w); }  // Consumo diario de las empresas.
constexpr size_t production(int w) { return 0xC4 + 4 * size_t(w); }  // Producción diaria.
constexpr size_t full_production(int w) { return 0x490 + 4 * size_t(w); }  // De la ciudad, con todos los puestos.
constexpr size_t threshold(int w, int i) { return 0x4F0 + 16 * size_t(w) + 4 * size_t(i); }  // t0..t3 de precio.
constexpr size_t facility_offset(int k) { return 0x840 + 16 * size_t(k); }  // Instalación k de la ciudad.
constexpr size_t kShortage = 0x0;  // u16 del almacén: a 1 cuando a una empresa le falta material.

// Producción del día en la serie de 8 días de la mercancía.
void set_series(StorageMemory& s, int w, uint32_t day, int32_t amount) {
	s.u16(0x13C + 16 * size_t(w) + 2 * day) = uint16_t(amount);
}

// Día de la serie: (tick >> 8) & 7.
uint32_t series_day(const SaveGame& save) {
	return (save.globals.u32(global::kWorldTick) >> 8) & 7;
}

// Multiplicación de 32 bits del juego (imul): con desbordamiento.
int32_t mul(int32_t a, int32_t b) {
	return int32_t(uint32_t(a) * uint32_t(b));
}

// Reparto proporcional de las empresas cuando falta un material:
// (existencias · x + necesidad − 1) / necesidad, con idiv.
int32_t share(int32_t have, int32_t x, int32_t need) {
	return int32_t(uint32_t(mul(have, x)) + uint32_t(need) - 1) / need;
}

// La ciudad del byte [+7] de la instalación.
TownMemory& facility_town(SaveGame& save, const FacilityMemory& fc) {
	return save.towns[fc.u8(7)].memory;
}

}  // namespace

// 0x50ED70: cabaña de caza (tipo 4): pieles; consume herramientas y cáñamo (1/20 cada uno).
void fn_50ed70(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint32_t max_workers) {
	TownMemory& t = facility_town(save, fc);
	const uint32_t day = series_day(save);
	const int32_t k = mul(fc.s32(0), 20) / 3;
	t.u32(full_production(kSkins)) += uint32_t(k) * max_workers >> 10;
	const uint32_t workers = fc.u16(4);
	const int32_t skins = int32_t(uint32_t(int32_t(workers * 2) / 3) + workers * uint32_t(k)) >> 10;
	s.u32(production(kSkins)) += uint32_t(skins);
	int32_t tools = skins / 20;
	int32_t hemp = tools;
	int32_t made = skins;
	s.u32(consumption(kIronGoods)) += uint32_t(tools);
	s.u32(consumption(kHemp)) += uint32_t(hemp);
	if (tools > s.s32(stock(kIronGoods))) {
		tools = s.s32(stock(kIronGoods));
		s.u16(kShortage) = 1;
		made = mul(tools, 20);
		hemp = made / 20;
	}
	if (hemp > s.s32(stock(kHemp))) {
		hemp = s.s32(stock(kHemp));
		s.u16(kShortage) = 1;
		made = mul(hemp, 20);
		tools = made / 20;
	}
	s.u32(stock(kIronGoods)) -= uint32_t(tools);
	s.u32(stock(kHemp)) -= uint32_t(hemp);
	s.u32(stock(kSkins)) += uint32_t(made);
	set_series(s, kSkins, day, made);
}

// 0x50E690: cabaña de pescador (tipo 5): pescado y, según [0x2CC] de la
// ciudad, aceite de ballena; consume sal (1/5 del pescado) y cáñamo (1/10).
// Con `whale_oil_limit`, el aceite solo entra en el almacén si las
// existencias están por debajo de su t2.
void fn_50e690(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint16_t max_workers, bool whale_oil_limit) {
	TownMemory& t = facility_town(save, fc);
	const uint32_t day = series_day(save);
	const uint32_t whaling = t.u32(0x2CC);  // Aceite de ballena por trabajador (0 en las ciudades sin balleneros).
	t.u32(full_production(kFish)) += uint32_t(int32_t(fc.u32(0) * max_workers * 200) / 3 >> 10);
	t.u32(full_production(kWhaleOil)) += uint32_t(int32_t(whaling * max_workers * 27) >> 10);
	const uint32_t workers = fc.u16(4);
	int32_t fish = int32_t(workers * fc.u32(0) * 200) / 3 >> 10;
	s.u32(production(kFish)) += uint32_t(fish);
	int32_t whale_oil = int32_t(workers * whaling * 27 >> 10);
	s.u32(production(kWhaleOil)) += uint32_t(whale_oil);
	int32_t salt = fish / 5;
	int32_t hemp = fish / 10;
	s.u32(consumption(kSalt)) += uint32_t(salt);
	s.u32(consumption(kHemp)) += uint32_t(hemp);
	if (salt > s.s32(stock(kSalt))) {
		const int32_t have = s.s32(stock(kSalt));
		s.u16(kShortage) = 1;
		if (have == 0) {
			salt = fish = whale_oil = hemp = 0;
		} else {
			hemp = share(have, hemp, salt);
			whale_oil = share(s.s32(stock(kSalt)), whale_oil, salt);
			fish = share(s.s32(stock(kSalt)), fish, salt);
			salt = s.s32(stock(kSalt));
		}
	}
	if (hemp > s.s32(stock(kHemp))) {
		const int32_t have = s.s32(stock(kHemp));
		s.u16(kShortage) = 1;
		if (have == 0) {
			salt = fish = whale_oil = hemp = 0;
		} else {
			salt = share(have, salt, hemp);
			whale_oil = share(s.s32(stock(kHemp)), whale_oil, hemp);
			fish = share(s.s32(stock(kHemp)), fish, hemp);
			hemp = s.s32(stock(kHemp));
		}
	}
	if (!whale_oil_limit || t.s32(threshold(kWhaleOil, 2)) > t.s32(stock(kWhaleOil))) {
		s.u32(stock(kWhaleOil)) += uint32_t(whale_oil);
		set_series(s, kWhaleOil, day, whale_oil);
	}
	s.u32(stock(kFish)) += uint32_t(fish);
	set_series(s, kFish, day, fish);
	s.u32(stock(kSalt)) -= uint32_t(salt);
	s.u32(stock(kHemp)) -= uint32_t(hemp);
}

// 0x50E8B0: cervecería (tipo 6): cerveza; consume grano (1/4) y madera
// (1/10). El consumo apuntado incluye el de los trabajadores de reserva.
void fn_50e8b0(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint16_t reserve, uint16_t max_workers) {
	TownMemory& t = facility_town(save, fc);
	const uint32_t day = series_day(save);
	const uint32_t k = fc.u32(0) * 47;
	t.u32(full_production(kBeer)) += uint32_t(int32_t(max_workers * k) >> 10);
	int32_t beer = int32_t(fc.u16(4) * k) >> 10;
	s.u32(production(kBeer)) += uint32_t(beer);
	int32_t grain = beer / 4;
	int32_t timber = beer / 10;
	const int32_t idle = int32_t(reserve * fc.u32(0) * 47) >> 10;
	s.u32(consumption(kTimber)) += uint32_t(idle / 4 + timber);
	s.u32(consumption(kGrain)) += uint32_t(idle / 10 + grain);
	if (grain > s.s32(stock(kGrain))) {
		grain = s.s32(stock(kGrain));
		s.u16(kShortage) = 1;
		beer = mul(grain, 4);
		timber = beer / 10;
	}
	if (timber > s.s32(stock(kTimber))) {
		timber = s.s32(stock(kTimber));
		s.u16(kShortage) = 1;
		beer = mul(timber, 10);
		grain = beer / 4;
	}
	s.u32(stock(kTimber)) -= uint32_t(timber);
	s.u32(stock(kGrain)) -= uint32_t(grain);
	s.u32(stock(kBeer)) += uint32_t(beer);
	set_series(s, kBeer, day, beer);
}

// 0x50FBC0: taller (tipo 7): herramientas; consume madera y arrabio (una
// unidad de cada por unidad) y las existencias de madera no bajan de cero.
void fn_50fbc0(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint32_t max_workers) {
	TownMemory& t = facility_town(save, fc);
	const uint32_t eff = fc.u32(0);
	t.u32(full_production(kIronGoods)) += eff * max_workers * 20 >> 10;
	const int32_t tools = int32_t(fc.u16(4) * eff * 20) >> 10;
	s.u32(consumption(kTimber)) += uint32_t(tools);
	s.u32(consumption(kPigIron)) += uint32_t(tools);
	s.u32(production(kIronGoods)) += uint32_t(tools);
	const uint32_t day = series_day(save);
	int32_t made = tools;
	if (made > s.s32(stock(kTimber))) {
		made = s.s32(stock(kTimber));
		s.u16(kShortage) = 1;
	}
	if (s.s32(stock(kPigIron)) < made) {
		made = s.s32(stock(kPigIron));
		s.u16(kShortage) = 1;
	}
	s.u32(stock(kIronGoods)) += uint32_t(made);
	set_series(s, kIronGoods, day, made);
	s.u32(stock(kTimber)) -= uint32_t(made);
	s.u32(stock(kPigIron)) -= uint32_t(made);
	if (s.s32(stock(kTimber)) < 0) {
		// With what is made already cut to the timber there is, it can't go
		// negative but by an overflow (a negative stock of pig iron and the
		// timber at the top). Checked with the oracle: scenario business_staff
		// (saves 3 and 5, direct calls).
		s.u32(stock(kTimber)) = 0;
	}
}

// 0x50EA00: apicultor (tipo 8): miel, sin materiales. Factor 4 (2 en
// invierno, bit 0x2 de [0x2C8]), a la mitad con mala cosecha (0x2000) y
// triple con buena (0x4000). Las cosechas las pone un suceso programado
// (0x4E2898) que no sale en las partidas: comprobadas con partidas modificadas.
void fn_50ea00(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint32_t max_workers) {
	TownMemory& t = facility_town(save, fc);
	const uint32_t day = series_day(save);
	const uint32_t flags = t.u32(0x2C8);
	int32_t factor = (flags & 2) ? 2 : 4;
	if (flags & 0x2000) {
		factor >>= 1;
	} else if (flags & 0x4000) {
		factor *= 3;
	}
	t.u32(full_production(kHoney)) += fc.u32(0) * uint32_t(factor) * max_workers * 40 / 3 >> 12;
	const int32_t honey = int32_t(fc.u16(4) * fc.u32(0) * uint32_t(factor) * 40) / 3 >> 12;
	s.u32(stock(kHoney)) += uint32_t(honey);
	s.u32(production(kHoney)) += uint32_t(honey);
	set_series(s, kHoney, day, honey);
}

// 0x50EAD0: granja de grano (tipo 9): sin materiales. Factor 6 (4 en
// invierno), un tercio con mala cosecha y doble con buena. Sin trabajadores
// no hace nada.
void fn_50ead0(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint32_t max_workers) {
	const uint32_t day = series_day(save);
	if (fc.u16(4) == 0) {
		return;
	}
	TownMemory& t = facility_town(save, fc);
	const uint32_t flags = t.u32(0x2C8);
	int32_t factor = (flags & 2) ? 4 : 6;
	if (flags & 0x2000) {
		factor /= 3;
	} else if (flags & 0x4000) {
		factor *= 2;
	}
	t.u32(full_production(kGrain)) += fc.u32(0) * uint32_t(factor) * max_workers * 200 / 9 >> 11;
	const int32_t x = int32_t(fc.u16(4) * fc.u32(0) * uint32_t(factor));
	int32_t grain;
	if (x > 0x800000) {
		// Solo con eficiencias enormes: comprobado con partidas modificadas.
		grain = mul(x / 6144, 200) / 3;
	} else {
		grain = mul(x, 200) / 3 / 6144;
	}
	s.u32(stock(kGrain)) += uint32_t(grain);
	s.u32(production(kGrain)) += uint32_t(grain);
	set_series(s, kGrain, day, grain);
}

// 0x50F460: granja de ganado (tipo 10): carne y cuero (2/5 de la carne);
// consume sal (cuero/10 + carne/4) y madera (cuero/5). El último argumento
// (¿cuero por debajo de t3?) no se usa.
void fn_50f460(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint32_t max_workers, bool leather_below_t3) {
	(void)leather_below_t3;
	TownMemory& t = facility_town(save, fc);
	const uint32_t day = series_day(save);
	const uint32_t full = fc.u32(0) * max_workers * 50 / 3;
	t.u32(full_production(kMeat)) += uint32_t(int32_t(full) >> 10);
	t.u32(full_production(kLeather)) += uint32_t(int32_t(full * 40) / 100 >> 10);
	int32_t meat = int32_t(fc.u16(4) * fc.u32(0) * 50) / 3 >> 10;
	int32_t leather = mul(meat, 40) / 100;
	s.u32(production(kLeather)) += uint32_t(leather);
	s.u32(production(kMeat)) += uint32_t(meat);
	int32_t salt = leather / 10 + meat / 4;
	int32_t timber = leather / 5;
	s.u32(consumption(kSalt)) += uint32_t(salt);
	s.u32(consumption(kTimber)) += uint32_t(timber);
	if (salt > s.s32(stock(kSalt))) {
		const int32_t have = s.s32(stock(kSalt));
		s.u16(kShortage) = 1;
		if (have <= 0) {
			salt = timber = leather = meat = 0;
		} else {
			meat = share(have, meat, salt);
			leather = share(s.s32(stock(kSalt)), leather, salt);
			timber = share(s.s32(stock(kSalt)), timber, salt);
			salt = s.s32(stock(kSalt));
		}
	}
	if (timber > s.s32(stock(kTimber))) {
		const int32_t have = s.s32(stock(kTimber));
		s.u16(kShortage) = 1;
		if (have <= 0) {
			salt = timber = leather = meat = 0;
		} else {
			meat = share(have, meat, timber);
			leather = share(s.s32(stock(kTimber)), leather, timber);
			salt = share(s.s32(stock(kTimber)), salt, timber);
			timber = s.s32(stock(kTimber));
		}
	}
	s.u32(stock(kMeat)) += uint32_t(meat);
	set_series(s, kMeat, day, meat);
	s.u32(stock(kLeather)) += uint32_t(leather);
	set_series(s, kLeather, day, leather);
	s.u32(stock(kSalt)) -= uint32_t(salt);
	s.u32(stock(kTimber)) -= uint32_t(timber);
}

// 0x50ECD0: aserradero (tipo 11): madera, sin materiales.
void fn_50ecd0(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint32_t max_workers) {
	TownMemory& t = facility_town(save, fc);
	const int32_t timber = int32_t(fc.u16(4) * fc.u32(0) * 400) / 3 >> 10;
	t.u32(full_production(kTimber)) += fc.u32(0) * max_workers * 400 / 3 >> 10;
	const uint32_t day = series_day(save);
	s.u32(stock(kTimber)) += uint32_t(timber);
	s.u32(production(kTimber)) += uint32_t(timber);
	set_series(s, kTimber, day, timber);
}

// 0x50EEB0: tejeduría (tipo 12): paño; consume la misma cantidad de lana.
void fn_50eeb0(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint32_t max_workers) {
	TownMemory& t = facility_town(save, fc);
	const uint32_t k = fc.u32(0) * 20;
	const uint32_t day = series_day(save);
	t.u32(full_production(kCloth)) += k * max_workers >> 10;
	int32_t cloth = int32_t(fc.u16(4) * k) >> 10;
	s.u32(production(kCloth)) += uint32_t(cloth);
	s.u32(consumption(kWool)) += uint32_t(cloth);
	if (s.s32(stock(kWool)) < cloth) {
		s.u16(kShortage) = 1;
		cloth = s.s32(stock(kWool));
	}
	s.u32(stock(kWool)) -= uint32_t(cloth);
	s.u32(stock(kCloth)) += uint32_t(cloth);
	set_series(s, kCloth, day, cloth);
}

// 0x50EF50: salinas (tipo 13): sal; consume madera (4/5 de la sal). El
// consumo apuntado incluye el de los trabajadores de reserva.
void fn_50ef50(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint16_t reserve, uint16_t max_workers) {
	const int32_t k = mul(fc.s32(0), 167) / 5;
	const int32_t idle = mul(fc.u16(4) + reserve, k) >> 10;
	s.u32(consumption(kTimber)) += uint32_t(mul(idle, 8) / 10);
	const uint32_t day = series_day(save);
	TownMemory& t = facility_town(save, fc);
	t.u32(full_production(kSalt)) += uint32_t(mul(max_workers, k) >> 10);
	int32_t salt = mul(fc.u16(4), k) >> 10;
	s.u32(production(kSalt)) += uint32_t(salt);
	int32_t timber = mul(salt, 8) / 10;
	if (timber > s.s32(stock(kTimber))) {
		timber = s.s32(stock(kTimber));
		s.u16(kShortage) = 1;
		salt = mul(timber, 10) >> 3;
	}
	s.u32(stock(kTimber)) -= uint32_t(timber);
	s.u32(stock(kSalt)) += uint32_t(salt);
	set_series(s, kSalt, day, salt);
}

// 0x50F650: fundición (tipo 14): arrabio, sin materiales.
void fn_50f650(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint32_t max_workers) {
	TownMemory& t = facility_town(save, fc);
	const uint32_t day = series_day(save);
	const int32_t k = mul(fc.s32(0), 100) / 3;
	t.u32(full_production(kPigIron)) += uint32_t(k) * max_workers >> 10;
	const int32_t iron = mul(fc.u16(4), k) >> 10;
	s.u32(stock(kPigIron)) += uint32_t(iron);
	s.u32(production(kPigIron)) += uint32_t(iron);
	set_series(s, kPigIron, day, iron);
}

// 0x50F060: granja de ovejas (tipo 15): lana, sin materiales.
void fn_50f060(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint32_t max_workers) {
	TownMemory& t = facility_town(save, fc);
	const uint32_t day = series_day(save);
	t.u32(full_production(kWool)) += fc.u32(0) * max_workers * 167 / 5 >> 10;
	const int32_t wool = int32_t(fc.u16(4) * fc.u32(0) * 167) / 5 >> 10;
	s.u32(stock(kWool)) += uint32_t(wool);
	s.u32(production(kWool)) += uint32_t(wool);
	set_series(s, kWool, day, wool);
}

// 0x50F100: viñedo (tipo 16): vino, sin materiales; las cuentas en 64 bits
// (_allmul, _alldiv, _allshr). Factor 4 (2 en invierno), a la mitad con
// mala cosecha y doble con buena.
void fn_50f100(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint32_t max_workers) {
	TownMemory& t = facility_town(save, fc);
	const uint32_t day = series_day(save);
	const uint32_t flags = t.u32(0x2C8);
	int64_t factor = (flags & 2) ? 2 : 4;
	if (flags & 0x2000) {
		factor /= 2;
	} else if (flags & 0x4000) {
		factor *= 2;
	}
	const uint64_t k = uint64_t(int64_t(fc.s32(0))) * uint64_t(factor);
	auto amount = [k](uint64_t workers) {
		return uint32_t(int64_t(workers * k * 0x43) / 5 >> 12);
	};
	t.u32(full_production(kWine)) += amount(max_workers);
	const uint32_t wine = amount(fc.u16(4));
	s.u32(stock(kWine)) += wine;
	s.u32(production(kWine)) += wine;
	set_series(s, kWine, day, int32_t(wine));
}

// 0x50F220: alfarería (tipo 17): cerámica; consume madera (la mitad).
void fn_50f220(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint16_t reserve, uint16_t max_workers) {
	(void)reserve;  // Se pasa, pero no se usa.
	TownMemory& t = facility_town(save, fc);
	const uint32_t day = series_day(save);
	const int32_t k = mul(fc.s32(0), 67) / 5;
	t.u32(full_production(kPottery)) += uint32_t(mul(max_workers, k) >> 10);
	int32_t pottery = mul(fc.u16(4), k) >> 10;
	int32_t timber = pottery / 2;
	s.u32(consumption(kTimber)) += uint32_t(timber);
	if (timber > s.s32(stock(kTimber))) {
		timber = s.s32(stock(kTimber));
		s.u16(kShortage) = 1;
		pottery = mul(timber, 2);
	}
	s.u32(stock(kTimber)) -= uint32_t(timber);
	s.u32(stock(kPottery)) += uint32_t(pottery);
	s.u32(production(kPottery)) += uint32_t(pottery);
	set_series(s, kPottery, day, pottery);
}

// 0x50F2E0: ladrillar (tipo 18): ladrillos; consume madera (la mitad). La
// producción diaria se apunta antes de ver si hay madera.
void fn_50f2e0(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint32_t max_workers) {
	TownMemory& t = facility_town(save, fc);
	const uint32_t k = fc.u32(0) * 134;
	const uint32_t day = series_day(save);
	t.u32(full_production(kBricks)) += k * max_workers >> 10;
	int32_t bricks = int32_t(fc.u16(4) * k) >> 10;
	int32_t timber = bricks / 2;
	s.u32(consumption(kTimber)) += uint32_t(timber);
	s.u32(production(kBricks)) += uint32_t(bricks);
	if (timber > s.s32(stock(kTimber))) {
		timber = s.s32(stock(kTimber));
		s.u16(kShortage) = 1;
		bricks = mul(timber, 2);
	}
	s.u32(stock(kTimber)) -= uint32_t(timber);
	s.u32(stock(kBricks)) += uint32_t(bricks);
	set_series(s, kBricks, day, bricks);
}

// 0x50F390: pez (tipo 19): brea; consume la misma cantidad de madera. El
// consumo apuntado incluye el de los trabajadores de reserva.
void fn_50f390(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint16_t reserve, uint16_t max_workers) {
	const int32_t k = mul(fc.s32(0), 67) / 5;
	s.u32(consumption(kTimber)) += uint32_t(mul(fc.u16(4) + reserve, k) >> 10);
	const uint32_t day = series_day(save);
	TownMemory& t = facility_town(save, fc);
	t.u32(full_production(kPitch)) += uint32_t(mul(max_workers, k) >> 10);
	int32_t pitch = mul(fc.u16(4), k) >> 10;
	if (pitch > s.s32(stock(kTimber))) {
		pitch = s.s32(stock(kTimber));
		s.u16(kShortage) = 1;
	}
	s.u32(stock(kTimber)) -= uint32_t(pitch);
	s.u32(stock(kPitch)) += uint32_t(pitch);
	s.u32(production(kPitch)) += uint32_t(pitch);
	set_series(s, kPitch, day, pitch);
}

// 0x50EBF0: granja de cáñamo (tipo 20): sin materiales. Factor 6; 3 en
// invierno; si no, 4 con mala cosecha y 9 con buena.
void fn_50ebf0(SaveGame& save, StorageMemory& s, FacilityMemory& fc, uint32_t max_workers) {
	TownMemory& t = facility_town(save, fc);
	const uint32_t day = series_day(save);
	const uint32_t flags = t.u32(0x2C8);
	const uint32_t factor = (flags & 2) ? 3 : (flags & 0x2000) ? 4 : (flags & 0x4000) ? 9 : 6;
	const uint32_t k = fc.u32(0) * factor;
	t.u32(full_production(kHemp)) += k * max_workers * 50 / 9 >> 11;
	const int32_t hemp = int32_t(fc.u16(4) * k * 50) / 3 / 6144;
	s.u32(stock(kHemp)) += uint32_t(hemp);
	s.u32(production(kHemp)) += uint32_t(hemp);
	set_series(s, kHemp, day, hemp);
}

namespace businesses_detail {

// 0x510120: trabajadores que quiere la empresa: uno solo si sus existencias
// en la ciudad pasan de t3; si no, los que tiene más 1, 4, 8 o 16 según el
// tramo de precio de las existencias, hasta el máximo del tipo (0x48 para el
// pescador si [0x2CC] > 10).
uint32_t fn_510120(const SaveGame& save, const FacilityMemory& fc) {
	const TownMemory& t = save.towns[fc.u8(7)].memory;
	uint32_t wanted = fc.u16(4);
	const uint8_t type = fc.u8(6);
	const int w = kFacilityWare[type];
	uint32_t max = kMaxWorkers[type];
	if (type == 5 && t.u32(0x2CC) > 10) {
		max = 0x48;
	}
	const int32_t have = t.s32(stock(w));
	if (t.s32(threshold(w, 3)) < have) {
		return 1;
	}
	if (t.s32(threshold(w, 2)) < have) {
		wanted += 1;
	} else if (t.s32(threshold(w, 1)) < have) {
		wanted += 4;
	} else if (t.s32(threshold(w, 0)) < have) {
		wanted += 8;
	} else {
		wanted += 16;
	}
	return wanted > max ? max : wanted;
}

}  // namespace businesses_detail

// 0x50E380: quita `count` trabajadores a la instalación (sin `allow_all`,
// deja al menos uno) y pasa 4 habitantes por trabajador de las clases 0..2 a
// los mendigos [0x2E4], repartidos según (habitantes − 1) · (10 −
// satisfacción si es negativa); de lo que no se pueda repartir, cada 4
// vuelven a la instalación como un trabajador y el resto se quita de uno en
// uno, de la clase 2 a la 0.
void fn_50e380(SaveGame& save, FacilityMemory& fc, uint32_t count, bool allow_all) {
	TownMemory& t = facility_town(save, fc);
	const uint16_t workers = fc.u16(4);
	uint32_t n = count;
	if (allow_all) {
		// Only from other callers (0x4D36D3, 0x4FF64E). Checked with the oracle:
		// scenarios business_staff, office_business and office_recount (saves 3 and 5).
		if (n > workers) {
			n = workers;
		}
	} else if (n >= workers) {
		n = uint32_t(workers) - 1;  // Checked with the oracle: scenario business_staff (saves 3 and 5, direct calls).
	}
	if (n < 1) {
		return;
	}
	fc.u16(4) = uint16_t(workers - n);
	const int32_t people = int32_t(n << 2);
	int32_t weight[3];
	int32_t sum = 0;
	for (size_t i = 0; i < 3; ++i) {
		const int16_t satisfaction = t.s16(0x300 + 2 * i);
		weight[i] = satisfaction < 0 ? 10 - satisfaction : 10;
		weight[i] = mul(t.s32(0x2D8 + 4 * i) - 1, weight[i]);
		sum += weight[i];
	}
	while (sum > 0x1000) {
		sum >>= 4;
		for (int32_t& w : weight) {
			w >>= 4;
		}
	}
	int32_t left = people;
	for (size_t i = 0; i < 3; ++i) {
		int32_t moved = mul(weight[i], people) / sum;
		const int32_t citizens = t.s32(0x2D8 + 4 * i);
		if (moved >= citizens) {
			moved = citizens - 1;
		}
		if (moved > left) {
			moved = left;  // Unreachable: with the classes kept at 1 or more the weights
			// are >= 0 and each share is floor(weight · people / sum), so the shares never pass
			// the people left.
		}
		t.s32(0x2D8 + 4 * i) = citizens - moved;
		left -= moved;
		t.s32(0x2E4) += moved;
	}
	if (left > 3) {
		fc.u16(4) += uint16_t(left >> 2);
		left &= 3;
	}
	if (left > 0) {
		size_t i = 2;
		do {
			if (t.s32(0x2D8 + 4 * i) > 1) {
				--t.s32(0x2D8 + 4 * i);
				++t.s32(0x2E4);
				--left;
			}
			i = i == 0 ? 2 : i - 1;
		} while (left > 0);
	}
}

namespace businesses_detail {

// 0x5109B0: despide `count` trabajadores (dejando al menos uno). En las
// empresas pasan a la reserva [+0xA], el 2 % de la reserva se acumula en el
// float [+0xC] y su parte entera se va a la ciudad (0x50E380, como mendigos):
// nadie si trabajadores y reserva suman ya 30 o más y, si no, como mucho lo
// que falte para 30. Lo que se va se resta dos veces del acumulado, como en
// el juego (with fix_game_bugs(), once). En los tipos 0..3 los despedidos se
// van todos a la ciudad.
void fn_5109b0(SaveGame& save, FacilityMemory& fc, uint32_t count) {
	uint32_t n = count;
	const uint32_t workers = fc.u16(4);
	if (workers < n) {
		// 0x5101D0 never asks for so many. Checked with the oracle: scenario
		// business_staff (saves 3 and 5, direct calls).
		n = workers - 1;
	}
	if (fc.u8(6) > 3) {
		fc.u16(0xA) += uint16_t(n);
		fc.u16(4) = uint16_t(workers - n);
		const float reserve = float(int32_t(fc.u16(0xA)));
		const float leaving = float(double(reserve) * double(kReserveLeaving) + double(fc.f32(0xC)));
		fc.f32(0xC) = leaving;
		n = uint32_t(ftol(leaving));
		const uint16_t now = fc.u16(4);
		fc.f32(0xC) = float(double(fc.f32(0xC)) - double(float(double(n))));
		const uint32_t total = uint32_t(fc.u16(0xA)) + now;
		if (total + n > 30) {
			if (total >= 30) {
				return;
			}
			n = 30 - total;
		}
		fc.u16(4) = uint16_t(now + n);
		fc.u16(0xA) -= uint16_t(n);
		// Game bug: what leaves was already taken from the float; taking it
		// again leaves [+0xC] negative (and fewer leave the next times).
		// With fix_game_bugs() it keeps the fraction.
		if (!fix_game_bugs()) {
			fc.f32(0xC) = float(double(fc.f32(0xC)) - double(float(double(n))));
		}
	}
	if (n > 0) {
		fn_50e380(save, fc, n, false);
	}
}

// 0x5108E0: contrata hasta `count` trabajadores y resta de `count` los
// contratados: primero mendigos de la ciudad, de 4 en 4 (pasan a la clase 2),
// si la satisfacción de la clase 2 [0x304] es positiva; después los de
// reserva de las instalaciones 1..20.
void fn_5108e0(SaveGame& save, FacilityMemory& fc, uint32_t& count) {
	TownMemory& t = facility_town(save, fc);
	if (t.s16(0x304) > 0) {
		uint32_t n = uint32_t(t.s32(0x2E4) >> 2);
		if (count < n) {
			n = count;
			count = 0;
		} else {
			count -= n;
		}
		fc.u16(4) += uint16_t(n);
		t.u32(0x2E4) -= n << 2;
		t.u32(0x2E0) += n << 2;
	}
	for (int k = 1; count > 0 && k < 21; ++k) {
		const size_t reserve = facility_offset(k) + 0xA;
		const uint16_t spare = t.u16(reserve);
		if (spare == 0) {
			continue;
		}
		if (spare >= count) {
			t.u16(reserve) = uint16_t(spare - uint16_t(count));
			fc.u16(4) += uint16_t(count);
			count = 0;
		} else {
			count -= spare;
			fc.u16(4) += t.u16(reserve);
			t.u16(reserve) = 0;
		}
	}
}

}  // namespace businesses_detail

// 0x4D4A40 sobre la lista de 0x6DD628 (el tick de ciudad pone a 0 la cuenta
// antes de las instalaciones y la lee 0x51CBE0): apunta el tipo de una
// instalación a la que le faltan trabajadores y cuántos (de 1 a 12); con 0 o
// menos solo escribe el tipo en el siguiente hueco, sin contarlo. Como mucho
// 63: u32 cuenta, u16 tipos en +4 y u16 cantidades en +0x82.
void fn_4d4a40(SaveGame& save, uint16_t type, uint16_t count) {
	GlobalMemory& g = save.globals;
	const uint32_t n = g.u32(kWorkersList);
	if (n >= 0x3F) {
		return;
	}
	g.u16(kWorkersList + 4 + 2 * n) = type;
	const int16_t c = int16_t(count);
	if (c <= 0) {
		return;
	}
	g.u16(kWorkersList + 0x82 + 2 * n) = uint16_t(c < 12 ? c : 12);
	g.u32(kWorkersList) = n + 1;
}

// 0x5101D0: tick de una instalación. Sin trabajadores solo siguen la milicia
// y el astillero (este si la ciudad tiene el bit 0x80 de [0x76C]). Cada tipo
// hace lo suyo y dice cuántos trabajadores quiere; las empresas (4..20)
// producen y no dejan que sus existencias suban si ya pasaban de t3
// (llevando la cuenta en [0x3D0 + 4·w]). Después despide o contrata: primero
// los de reserva de la propia instalación y después (0x5108E0) hasta 30
// trabajadores (60 en la granja de grano, con la comparación al revés, como
// en el juego); lo que siga faltando se apunta en la lista de 0x6DD628.
void fn_5101d0(SaveGame& save, int town, int facility) {
	StorageMemory& s = storage_of(save.towns[size_t(town)].memory);
	FacilityMemory& fc = facility_of(save.towns[size_t(town)].memory, facility);
	TownMemory& t = facility_town(save, fc);
	if (fc.u16(4) == 0) {
		const uint8_t type = fc.u8(6);
		if (type >= 2) {
			return;
		}
		if (type == 1 && !(t.u32(0x76C) & 0x80)) {
			return;
		}
	}
	const uint8_t type = fc.u8(6);
	uint32_t wanted = 0;
	// Empresa: produce sin dejar subir sus existencias si ya pasaban de t3.
	auto business = [&](int w, auto produce) {
		const int32_t before = t.s32(stock(w));
		t.u32(0x3D0 + 4 * size_t(w)) -= uint32_t(before);
		produce();
		if (before > t.s32(threshold(w, 3))) {
			t.s32(stock(w)) = before;
		}
		t.u32(0x3D0 + 4 * size_t(w)) += t.u32(stock(w));
		wanted = fn_510120(save, fc);
	};
	switch (type) {
	case 0:
		fn_510c50(save, town, facility);
		wanted = fc.u16(4);
		fn_50e500(save, town, facility);
		break;
	case 1: {
		fn_50e570(save, town, facility);
		wanted = uint32_t(ftol(t.f32(0x818))) * 3 + 10;
		const uint32_t cap = uint32_t(fc.u16(4)) + 3;
		if (wanted > cap) {
			wanted = cap;
		}
		if (wanted > 0x28) {
			wanted = 0x28;
		}
		break;
	}
	case 2:
		wanted = fn_50e610(save, town, facility);
		if (wanted < 5) {
			wanted = 5;  // Unreachable: 0x50E610 gives 5 or more.
		}
		break;
	case 3: {
		const uint32_t r = fn_510aa0(save, town, facility);
		fn_50f6e0(save, town, facility, r);
		wanted = kMaxWorkers[type];
		break;
	}
	case 4:
		business(kSkins, [&] { fn_50ed70(save, s, fc, 0x48); });
		break;
	case 5: {
		const uint16_t max = t.u32(0x2CC) != 0 ? 0x48 : 0x41;
		business(kFish, [&] { fn_50e690(save, s, fc, max, true); });
		break;
	}
	case 6:
		business(kBeer, [&] { fn_50e8b0(save, s, fc, fc.u16(0xA), 0x44); });
		break;
	case 7:
		business(kIronGoods, [&] { fn_50fbc0(save, s, fc, 0x4E); });
		break;
	case 8:
		business(kHoney, [&] { fn_50ea00(save, s, fc, 0x3C); });
		break;
	case 9:
		business(kGrain, [&] { fn_50ead0(save, s, fc, 0x44); });
		break;
	case 10: {
		const bool below = t.s32(stock(kLeather)) < t.s32(threshold(kLeather, 3));
		business(kMeat, [&] { fn_50f460(save, s, fc, 0x3C, below); });
		break;
	}
	case 11:
		business(kTimber, [&] { fn_50ecd0(save, s, fc, 0x30); });
		break;
	case 12:
		business(kCloth, [&] { fn_50eeb0(save, s, fc, 0x3C); });
		break;
	case 13:
		business(kSalt, [&] { fn_50ef50(save, s, fc, fc.u16(0xA), 0x36); });
		break;
	case 14:
		business(kPigIron, [&] { fn_50f650(save, s, fc, 0x46); });
		break;
	case 15:
		business(kWool, [&] { fn_50f060(save, s, fc, 0x32); });
		break;
	case 16:
		business(kWine, [&] { fn_50f100(save, s, fc, 0x5D); });
		break;
	case 17:
		business(kPottery, [&] { fn_50f220(save, s, fc, fc.u16(0xA), 0x3A); });
		break;
	case 18:
		business(kBricks, [&] { fn_50f2e0(save, s, fc, 0x1B); });
		break;
	case 19:
		business(kPitch, [&] { fn_50f390(save, s, fc, fc.u16(0xA), 0x1A); });
		break;
	case 20:
		business(kHemp, [&] { fn_50ebf0(save, s, fc, 0x3E); });
		break;
	default:
		break;  // Unreachable: the facilities are 0..20.
	}

	const uint32_t workers = fc.u16(4);
	if (workers > wanted) {
		fn_5109b0(save, fc, workers - wanted);
		return;
	}
	if (workers == wanted) {
		return;
	}
	uint32_t need = wanted - workers;
	const uint16_t reserve = fc.u16(0xA);
	if (reserve > 0) {
		if (need < reserve) {
			fc.u16(0xA) = uint16_t(reserve - need);
			fc.u16(4) = uint16_t(workers + need);
			return;
		}
		need -= reserve;
		fc.u16(4) = uint16_t(workers + reserve);
		fc.u16(0xA) = 0;
	}
	const uint16_t now = fc.u16(4);
	const uint32_t limit = type == 9 ? 60 : 30;
	if (now < limit) {
		uint32_t room = limit - now;
		// En la granja de grano la comparación está al revés: si cabe lo que
		// falta, contrata hasta llenar el sitio y lo que queda por apuntar sale
		// negativo (0x4D4A40 no lo cuenta); si no cabe, contrata todo lo que
		// falta aunque pase de 60. Game bug: with fix_game_bugs() it compares as
		// the other types do.
		if (type == 9 && !fix_game_bugs() ? room >= need : room < need) {
			need -= room;
			fn_5108e0(save, fc, room);
			need += room;
		} else {
			fn_5108e0(save, fc, need);
		}
	}
	fn_4d4a40(save, type, uint16_t(need));
}

}  // namespace p3::data::tick
