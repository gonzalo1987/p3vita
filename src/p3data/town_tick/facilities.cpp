// Instalaciones de los tipos 0..3, que el tick de una instalación (0x5101D0)
// llama según su tipo [+6]: milicia (0), astillero (1), obras de la ciudad (2)
// y armero (3).
//
// Una instalación (struct facility, 16 bytes) está en [0x840 + 16·k] de su
// ciudad o, las de las oficinas, en un registro de 0x14 bytes del mundo
// (SaveGame::world_records). Campos:
//   [+0x0] s32  eficiencia (0x400 = 100 %)
//   [+0x4] u16  empleados
//   [+0x6] u8   tipo
//   [+0x7] u8   ciudad (índice)
//   [+0x8] u16  productividad de la ciudad para el tipo (la leen las de las oficinas)
//   [+0xC] u16  empleados que necesita (las de las oficinas)
// Las funciones reciben además un almacén (struct storage, al principio de la
// ciudad o de una oficina): existencias en [+0x4 + 4·m], consumo diario de las
// empresas en [+0x64 + 4·m] y producción diaria en [+0xC4 + 4·m].
//
// Campos de la ciudad que se usan aquí:
//   [0x124] 6 × s32  piezas de artillería en el almacén
//   [0x13C] 24 × 8 × u16  producción de los 8 últimos días por mercancía
//   [0x2BC] s32  armas de mano (en piezas)
//   [0x2C8] u32  situación: 0x10 asedio, 0x20..0x80 límite de soldados [0x9A0],
//                0x800 no se construye el tipo 0x30, 0xF10 se recluta de todo
//   [0x2D4] s32  habitantes; [0x2D8..0x2E4] 4 × s32 por clase (ricos,
//                acomodados, pobres, mendigos)
//   [0x300] 3 × s16  ánimo de las tres primeras clases
//   [0x6D0] u32  lo que fabrica el armero (mercancía 20..23, artillería 24..29, armas de mano 30)
//   [0x6F1] u8   comerciante alcalde: si es un jugador humano, la milicia no se lleva sola
//   [0x700] s32  dinero de la ciudad
//   [0x760] u16  lista de registros de edificio libres; [0x762] obras en curso;
//                [0x764] edificios de la ciudad; [0x768] murallas y torres
//   [0x76C] u32  objetos de la ciudad: 0x80 astillero, 0x20000 (tipo 0x2F terminado)
//   [0x775] u8   materiales que le faltan al armero (1 madera, 2 cuero,
//                4 cáñamo, 8 herramientas, 0x10 nada que fabricar)
//   [0x784] u16  primera oficina de la ciudad
//   [0x786] u16  defensas de la muralla (+1 por el tipo 0x36, +3 por 0x37 y 0x38)
//   [0x810] u32  experiencia del astillero (nivel = [0x810] / 420 000 + 2, 0x52BB00)
//   [0x814] s32  trabajo hecho por el astillero
//   [0x818] f32, [0x824] s8  (astillero; los usa el armero)
//   [0x81C] u16  barco en construcción (cabeza de una lista por [+0x6])
//   [0x81E] u16  barcos en reparación; [0x820] u16 convoyes en reparación
//   [0x835] u8   días sin poder pagar a la milicia (de 2 en 2 hasta 10)
//   [0x998] 4 × u8  soldados por tipo; [0x99C] 4 × u8 reclutas; [0x9A0] u16 soldados como mucho

#include <cassert>

#include "p3data/mail.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

// Funciones internas, visibles para las sondas (tools/oracle/probes_facilities.cpp).
namespace facilities_detail {
uint8_t fn_5252f0(const SaveGame& save, int town);
int fn_5308a0(const SaveGame& save, uint32_t merchant, uint32_t town);
void fn_51d410(SaveGame& save, int town, uint32_t type);
void fn_51d4c0(SaveGame& save, int town, uint32_t type);
void fn_5083b0(SaveGame& save, int town, int32_t work);
void fn_508aa0(SaveGame& save, int town, int32_t work);
void fn_51ff30(SaveGame& save, int town, uint32_t workers);
uint32_t fn_52bb00(const SaveGame& save, int town);
void fn_4d36e0(SaveGame& save, uint32_t record, int32_t n);
void fn_4d3490(SaveGame& save, uint32_t record, uint32_t town, uint32_t type, int32_t n);
uint16_t fn_5306b0(SaveGame& save);
void fn_530780(SaveGame& save, uint32_t record);
}  // namespace facilities_detail

namespace {

FacilityMemory& town_facility(SaveGame& save, int town, int facility) {
	assert(facility >= 0 && facility < 21);
	return facility_of(save.towns[size_t(town)].memory, facility);
}

StorageMemory& town_storage(SaveGame& save, int town) {
	return storage_of(save.towns[size_t(town)].memory);
}

// Ciudad de una instalación ([+0x7]).
TownMemory& town_of(SaveGame& save, const FacilityMemory& f) {
	return save.towns[f.u8(0x7)].memory;
}

// Aritmética de 32 bits que puede desbordar (en C++ es UB con signo).
int32_t add32(int32_t a, int32_t b) { return int32_t(uint32_t(a) + uint32_t(b)); }
int32_t sub32(int32_t a, int32_t b) { return int32_t(uint32_t(a) - uint32_t(b)); }
int32_t mul32(int32_t a, int32_t b) { return int32_t(uint32_t(a) * uint32_t(b)); }
// sar: desplazamiento aritmético (el de GCC y Clang con signo).
int32_t sar(int32_t v, int n) { return v >> n; }

uint16_t merchant_count(const SaveGame& save) { return save.globals.u16(global::kWorldMerchantCount); }
uint16_t ship_count(const SaveGame& save) { return save.globals.u16(global::kShipCount); }

// [+0xE8] del objeto de los barcos: cabeza de la lista de barcos en puerto.
constexpr uint32_t kDockedHead = global::kShips + 0xE8;
// [+0xF6]: número de convoyes.
constexpr uint32_t kConvoyCount = global::kShips + 0xF6;
// Registros de 0x14 bytes del mundo: número ([0x6DE4A6]) y primero libre ([0x6DE51C]).
constexpr uint32_t kWorldRecordCount = global::kWorld + 0x6;
constexpr uint32_t kWorldRecordFree = global::kWorld + 0x7C;

// 0x672DB0: paga diaria de cada tipo de soldado.
constexpr uint8_t kSoldierPay[4] = {0x21, 0x1A, 0x28, 0x2E};
// 0x6735E4: producción de cada arma (mercancías 20..23), s8.
constexpr int8_t kWeaponRate[4] = {0x20, 0x28, 0x1A, 0x14};
// 0x672CB4: peso de cada pieza de artillería (u16).
constexpr uint16_t kArtilleryWeight[6] = {1000, 1000, 2000, 2000, 2000, 1000};
// Materiales de cada pieza de artillería, en % (0x6735C4, 0x6735CC, 0x6735DC, 0x6735D4).
constexpr uint8_t kArtilleryTimber[6] = {0x32, 0x32, 0x32, 0x32, 0x19, 0x32};
constexpr uint8_t kArtilleryLeather[6] = {0x0A, 0x0A, 0x0A, 0x0A, 0x05, 0x0A};
constexpr uint8_t kArtilleryHemp[6] = {0x0A, 0x0A, 0x0A, 0x0A, 0x05, 0x0A};
constexpr uint8_t kArtilleryTools[6] = {0x0A, 0x0A, 0x0A, 0x0A, 0x1E, 0x3C};
// 0x67371C: tipos de empresa que se agrupan con sus vecinas (granjas, colmenar, viñedo...).
constexpr uint32_t kGroupedTypes = 0x118710;
// 0x672A18: plazas de una casa de cada clase (u16).
constexpr uint16_t kHouseCapacity[3] = {80, 140, 280};
// 0x6729D0: empleados que necesita cada tipo de empresa (u8).
constexpr uint8_t kBusinessWorkers[21] = {0x32, 0x28, 0x1E, 0x0F, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
	0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x0F, 0x0F, 0x1E};

// 0x520534 y 0x52050C: qué hace 0x51FF30 al terminar una obra con dueño, según
// el tipo de edificio - 1.
enum OwnedCompletion : uint8_t {
	kOwnedShipyard,  // 0x520127
	kOwnedTownList,  // 0x52010C: pasa a la ciudad, lista [0x764]
	kOwnedBusiness,  // 0x5200D4: lista [0x2CE] de la oficina
	kOwnedHouse,  // 0x5200F0: lista [0x2D0] de la oficina
	kOwnedOwnList,  // 0x520195: lista [0x764] con su dueño
	kOwnedWall,  // 0x5201AC: lista [0x768]
	kOwnedReplaceWall,  // 0x52020C
	kOwnedTower,  // 0x5201C7: lista [0x768] y [0x786] + 1
	kOwnedBigTower,  // 0x5201E9: lista [0x768] y [0x786] + 3
	kOwnedFree,  // 0x520298
};
constexpr uint8_t kOwnedCompletion[56] = {0x00, 0x09, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x02,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x01, 0x00, 0x01, 0x05,
	0x05, 0x05, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08};

// 0x520588 y 0x52056C: lo mismo sin dueño, según el tipo de edificio (0..0x38).
enum TownCompletion : uint8_t {
	kTownFree,  // 0x520324
	kTownShipyard,  // 0x520316
	kTownList,  // 0x520352: lista [0x764]
	kTownHouse,  // 0x5203BF: lista [0x764] y 0x520BD0
	kTownFlag,  // 0x520369
	kTownWall,  // 0x5203A8: lista [0x768]
	kTownReplaceWall,  // 0x5203DD
};
constexpr uint8_t kTownCompletion[57] = {0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x04, 0x02,
	0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x05, 0x05, 0x05};

// --- Milicia ---

// 0x50E500 sobre una instalación y un almacén: la milicia bebe cerveza
// (mercancía 3), salvo en asedio.
void militia_beer(SaveGame& save, FacilityMemory& f, StorageMemory& storage) {
	const TownMemory& t = town_of(save, f);
	if (t.u8(0x2C8) & 0x10) {
		return;
	}
	const int32_t x = mul32(mul32(int32_t(f.u16(0x4)), f.s32(0x0)), 60);
	const int32_t beer = sar(x / 100, 10);
	storage.s32(0x70) = add32(storage.s32(0x70), beer);
	if (storage.s32(0x10) < beer) {
		storage.s32(0x10) = 0;
	} else {
		storage.s32(0x10) -= beer;
	}
}

// --- Astillero ---

// 0x50E570 sobre una instalación y un almacén.
void shipyard(SaveGame& save, FacilityMemory& f, StorageMemory& storage) {
	const int town = f.u8(0x7);
	const uint32_t workers = f.u16(0x4);
	// Brea (mercancía 15): 71/32 por empleado + 67; el doble en asedio.
	int32_t pitch = int32_t(workers * 71 >> 5) + 0x43;
	if (save.towns[size_t(town)].memory.u8(0x2C8) & 0x10) {
		pitch += pitch;
	}
	storage.s32(0xA0) = add32(storage.s32(0xA0), pitch);
	if (storage.s32(0x40) > pitch) {
		storage.s32(0x40) -= pitch;
	} else {
		storage.s32(0x40) = 0;
	}
	const int32_t work = int32_t(workers * 100);
	facilities_detail::fn_5083b0(save, town, work);
	facilities_detail::fn_508aa0(save, town, work);
}

// --- Obras ---

// Deja libre un registro de edificio (0x5202B2/0x520324): a la lista [0x760].
void free_record(SavedTown& st, uint16_t index) {
	BuildingRecord& r = st.records[index];
	r.x = 0xFF;
	r.y = 0xFF;
	r.owner = 0xFF;
	r.type = 0xFF;
	r.next = st.memory.u16(0x760);
	r.pending = 0xFF;
	r.param = 0xFF;
	st.memory.u16(0x760) = index;
}

// Cambia la pieza de muralla 0x37/0x38 de la lista [0x768] que está en la
// misma celda por la 0x34/0x35 (0x52020C/0x5203DD).
void replace_wall(SavedTown& st, uint8_t x, uint8_t y) {
	const uint16_t count = st.memory.u16(0x776);
	uint32_t i = st.memory.u16(0x768);
	while (i < count) {
		BuildingRecord& w = st.records[i];
		uint32_t next = w.next;
		if (w.x == x && w.y == y && (w.type == 0x37 || w.type == 0x38)) {
			w.pending = 0;
			w.type = w.type == 0x37 ? 0x34 : 0x35;
			next = 0xFFFF;
		}
		i = next;
	}
}

// --- Armero ---

// float(entero de 32 bits sin signo) / float(entero), como lo hace 0x50F6E0:
// fild de cada uno, fstp a float, fdiv con 53 bits y fstp a float.
float stock_ratio(int32_t stock, int32_t need) {
	const float a = float(double(stock));
	const float b = float(double(uint32_t(need)));
	return float(double(a) / double(b));
}

// fild DWORD, fstp a float, fmul por el factor y __ftol.
int32_t scale(int32_t v, float factor) {
	const float a = float(double(v));
	return int32_t(uint32_t(ftol(double(a) * double(factor))));
}

// 0x50F6E0 sobre una instalación y un almacén.
void weaponsmith(SaveGame& save, FacilityMemory& f, StorageMemory& storage, uint32_t item) {
	TownMemory& t = town_of(save, f);
	const uint32_t kind = item - 0x14;
	const uint32_t day = (save.globals.u32(global::kWorldTick) >> 8) & 7;
	if (f.s32(0x0) == 0) {
		f.s32(0x0) = 0x400;  // Checked with the oracle: scenario weaponsmith (saves 3 and 5, direct calls).
	}
	// Producción diaria de las cuatro armas, fabrique lo que fabrique.
	for (size_t w = 0; w < 4; ++w) {
		const int32_t made = sar(mul32(mul32(kWeaponRate[w], f.u16(0x4)), f.s32(0x0)), 16);
		storage.s32(0x114 + 4 * w) = add32(storage.s32(0x114 + 4 * w), made);
	}
	t.u8(0x775) = 0;
	// Materiales: madera (11), cuero (13), cáñamo (17) y herramientas (12), y
	// lo que se fabrica.
	int32_t timber, leather, hemp, tools, out;
	bool weapon = false;
	if (kind < 4) {
		// Armas de la milicia (mercancías 20..23).
		weapon = true;
		out = sar(mul32(mul32(kWeaponRate[kind], f.u16(0x4)), f.s32(0x0)), 14);
		timber = mul32(out, 10);
		leather = hemp = tools = out;
	} else if (kind < 10) {
		// Artillería: no se fabrica si la ciudad tiene más de 20 piezas de peso.
		const uint32_t piece = kind - 4;
		if (t.s32(0x124 + 4 * piece) > int32_t(kArtilleryWeight[piece]) * 20) {
			return;
		}
		uint32_t workers = uint32_t(f.u16(0x4)) * 71;
		if (piece < 5) {
			workers += workers;
		}
		out = sar(mul32(f.s32(0x0), int32_t(workers)), 10);
		timber = mul32(kArtilleryTimber[piece], out) / 100;
		leather = mul32(kArtilleryLeather[piece], out) / 100;
		hemp = mul32(kArtilleryHemp[piece], out) / 100;
		tools = mul32(kArtilleryTools[piece], out) / 100;
	} else if (kind == 10) {
		// Armas de mano.
		out = sar(int32_t(uint32_t(mul32(f.u16(0x4), f.s32(0x0))) << 5), 14);
		timber = mul32(out, 10);
		leather = hemp = tools = out;
	} else {
		t.u8(0x775) = 0x10;
		return;
	}
	storage.s32(0x90) = add32(storage.s32(0x90), timber);
	storage.s32(0x98) = add32(storage.s32(0x98), leather);
	storage.s32(0xA8) = add32(storage.s32(0xA8), hemp);
	storage.s32(0x94) = add32(storage.s32(0x94), tools);
	// Si falta material, se fabrica en proporción al que más falta.
	float factor = 1.0f;
	bool no_tools = false;
	if (storage.s32(0x30) < timber) {
		if (storage.s32(0x30) > 0) {
			factor = stock_ratio(storage.s32(0x30), timber);
		} else {
			factor = 0;
			t.u8(0x775) |= 1;
		}
	}
	if (storage.s32(0x38) < leather) {
		if (storage.s32(0x38) > 0) {
			const float r = stock_ratio(storage.s32(0x38), leather);
			if (factor > r) {
				factor = r;
			}
		} else {
			factor = 0;
			t.u8(0x775) |= 2;
		}
	}
	if (storage.s32(0x48) < hemp) {
		if (storage.s32(0x48) > 0) {
			const float r = stock_ratio(storage.s32(0x48), hemp);
			if (factor > r) {
				factor = r;
			}
		} else {
			factor = 0;
			t.u8(0x775) |= 4;
		}
	}
	if (storage.s32(0x34) < tools) {
		if (storage.s32(0x34) > 0) {
			const float r = stock_ratio(storage.s32(0x34), tools);
			if (factor > r) {
				factor = r;
			}
		} else {
			factor = 0;
			t.u8(0x775) |= 8;
			no_tools = true;
		}
	}
	if (no_tools || factor < 1.0f) {
		timber = scale(timber, factor);
		leather = scale(leather, factor);
		hemp = scale(hemp, factor);
		tools = scale(tools, factor);
		out = scale(out, factor);
	}
	if (weapon) {
		const size_t ware = 0x14 + kind;
		storage.s32(0x4 + 4 * ware) = add32(storage.s32(0x4 + 4 * ware), out);
		storage.u16(0x13C + 2 * (day + 8 * ware)) = uint16_t(out);
	} else if (kind - 4 < 6) {
		t.s32(0x124 + 4 * (kind - 4)) = add32(t.s32(0x124 + 4 * (kind - 4)), out);
	} else {
		storage.s32(0x2BC) = add32(storage.s32(0x2BC), out / 10);
	}
	storage.s32(0x30) = sub32(storage.s32(0x30), timber);
	storage.s32(0x38) = sub32(storage.s32(0x38), leather);
	storage.s32(0x48) = sub32(storage.s32(0x48), hemp);
	storage.s32(0x34) = sub32(storage.s32(0x34), tools);
}

// --- Empleados de una instalación ---

// 0x4D36E0 sobre un registro del mundo (la empresa de una oficina): eficiencia
// según cuántas tiene la oficina y empleados que necesita.
void set_office_business(SaveGame& save, GameMemory<0x14>& r, int32_t n) {
	const uint32_t type = r.u8(0x6);
	const int32_t productivity = save.towns[r.u8(0x7)].memory.u16(0x840 + 16 * type + 0x8);
	int32_t efficiency = productivity;
	if (n >= 6) {
		// From 6 and from 9: checked with the oracle, scenario world_records (saves 3
		// and 5, direct calls).
		efficiency = n >= 9 ? productivity + productivity / 10 : productivity * 106 / 100;
	} else if (n >= 3) {
		efficiency = productivity * 103 / 100;
	}
	r.s32(0x0) = efficiency;
	r.u16(0xC) = uint16_t(uint32_t(kBusinessWorkers[type]) * uint32_t(n));
}

}  // namespace

namespace facilities_detail {

// 0x5252F0: nivel de la muralla, que limita los soldados de cada tipo a 10 << nivel.
uint8_t fn_5252f0(const SaveGame& save, int town) {
	const SavedTown& st = save.towns[size_t(town)];
	const TownMemory& t = st.memory;
	const uint16_t count = t.u16(0x776);
	const uint16_t head = t.u16(0x766);
	if (head < count) {
		if (st.records[head].owner >= 0xFE) {
			return 0;
		}
		for (uint32_t i = t.u16(0x768); i < count; i = st.records[i].next) {
			if (st.records[i].owner == 0xFE) {
				return 1;
			}
		}
		return 0;  // Checked with the oracle: scenario wall_level (saves 3 and 5, direct calls).
	}
	if (!fn_4fcfe0(save, town, 2)) {
		return 2;  // Checked with the oracle: scenario wall_level (saves 3 and 5, direct calls).
	}
	if (!fn_4fcfe0(save, town, 1)) {
		return 1;
	}
	return fn_4fcfe0(save, town, 0) ? 0xFF : 0;
}

// 0x5308A0 con create = 0: la oficina del comerciante en la ciudad, o -1.
int fn_5308a0(const SaveGame& save, uint32_t merchant, uint32_t town) {
	town &= 0xFFFF;
	merchant &= 0xFFFF;
	if (town >= save.globals.u32(global::kWorldTownCount) || merchant >= merchant_count(save)) {
		return -1;
	}
	const uint16_t offices = save.globals.u16(global::kWorldOfficeCount);
	for (uint32_t i = save.towns[town].memory.u16(0x784); i < offices; i = save.offices[i].memory.u16(0x2CA)) {
		if (save.offices[i].memory.u16(0x2C4) == merchant) {
			return int(i);
		}
	}
	return -1;
}

// 0x51D410: licencia un recluta (o, si no hay, un soldado) del tipo dado: 20
// personas vuelven a los mendigos y salen de las clases, de la tercera a la primera.
void fn_51d410(SaveGame& save, int town, uint32_t type) {
	TownMemory& t = save.towns[size_t(town)].memory;
	if (type >= 4 || t.u16(0x844) < 5) {
		return;
	}
	uint8_t& soldiers = t.u8(0x998 + type);
	uint8_t& recruits = t.u8(0x99C + type);
	if (soldiers == 0 && recruits == 0) {
		return;  // Only from operation 0x40 (the interface).
		// Checked with the oracle: scenario militia (saves 3 and 5, direct calls).
	}
	if (recruits > 0) {
		--recruits;
	} else {
		--soldiers;
	}
	t.s32(0x2E4) = add32(t.s32(0x2E4), 20);
	int32_t left = 20;
	for (int k = 2; k >= 0; --k) {
		int32_t& c = t.s32(0x2D8 + 4 * size_t(k));
		if (c > left) {
			c -= left;
			break;
		}
		left = sub32(add32(left, 1), c);  // Checked with the oracle: scenario militia (saves 3 and 5, direct calls).
		c = 1;
	}
	t.u16(0x844) = uint16_t(t.u16(0x844) - 5);
}

// 0x51D4C0: recluta un soldado del tipo dado (20 mendigos pasan a la tercera
// clase y se gastan 5 armas de la mercancía 20 + tipo, de la ciudad o, si el
// alcalde es un jugador humano, de su oficina).
void fn_51d4c0(SaveGame& save, int town, uint32_t type) {
	if (type >= 4) {
		return;
	}
	TownMemory& t = save.towns[size_t(town)].memory;
	const uint32_t level = fn_5252f0(save, town);
	const int32_t have = t.u8(0x998 + type) + t.u8(0x99C + type);
	if (have >= int32_t(10u << (level & 31))) {
		return;
	}
	if (t.u8(0x2C8) & 0xE0) {
		int32_t total = 0;
		for (size_t i = 0; i < 4; ++i) {
			total += t.u8(0x998 + i) + t.u8(0x99C + i);
		}
		if (total >= int32_t(t.u16(0x9A0))) {
			return;
		}
	}
	if (t.s32(0x2E4) < 20) {
		return;
	}
	int32_t* weapons = &t.s32(0x54 + 4 * type);
	const uint8_t mayor = t.u8(0x6F1);
	if (mayor < merchant_count(save) && save.merchants[mayor].memory.u16(0x8) == 0) {
		// A human mayor: his office's weapons. Checked with the oracle: scenario militia (saves 3 and 5, direct calls).
		const int office = fn_5308a0(save, mayor, t.u8(0x2C0));
		if (office < 0) {
			return;
		}
		weapons = &save.offices[size_t(office)].memory.s32(0x54 + 4 * type);
	}
	if (*weapons < 50) {
		return;
	}
	*weapons -= 50;
	t.u16(0x844) = uint16_t(t.u16(0x844) + 5);
	t.s32(0x2E4) = sub32(t.s32(0x2E4), 20);
	t.s32(0x2E0) = add32(t.s32(0x2E0), 20);
	t.u8(0x99C + type) += 1;
}

// 0x5083B0: trabajo del astillero en el barco que construye ([0x81C]). Al
// terminarlo pasa a la lista de barcos en puerto con el estado 5.
void fn_5083b0(SaveGame& save, int town, int32_t work) {
	TownMemory& t = save.towns[size_t(town & 0xFFFF)].memory;
	const uint16_t index = t.u16(0x81C);
	if (index >= ship_count(save)) {
		return;
	}
	SavedShip& ship = save.ships[index];
	ship.notices() |= 0x100;
	ship.health() = add32(ship.health(), work);
	t.s32(0x814) = add32(t.s32(0x814), work);
	if (ship.max_health() > ship.health()) {
		return;
	}
	const uint32_t tick = save.globals.u32(global::kWorldTick);
	ship.built_tick() = tick > 0x7FFFFFFF ? 0x7FFFFFFF : tick;
	ship.notices() |= 0x20;
	ship.health() = ship.max_health();
	ship.status() = 5;
	ship.timer() = 0;
	{
		// The letter 0x00 to the ship's owner (0x50847B..0x5084A5): [+0x5] the
		// town, u16 [+0x8] the ship, [+0xA] its type.
		Letter letter(0x00);
		letter.entry.u8(5) = uint8_t(town);
		letter.entry.u16(8) = index;
		letter.entry.u16(0xA) = ship.memory.u8(0xE);
		send_letter(save, ship.owner(), letter);
	}
	t.u16(0x81C) = ship.next_in_list();
	ship.next_in_list() = save.globals.u16(kDockedHead);
	save.globals.u16(kDockedHead) = index;
	if (ship.owner() < merchant_count(save)) {
		MerchantMemory& m = save.merchants[ship.owner()].memory;
		m.u16(0x478) = uint16_t(m.u16(0x478) + 1);  // Barcos construidos.
		// Si el dueño es un jugador humano: the ticker line «Launch in X»
		// (0x508514: kind 6, the ship) y, si es el jugador, la ventana del
		// barco nuevo (0x469380 y 0x469AF0, que puede encolar una operación;
		// the interface's, not reproduced).
		if (m.u16(8) == 0) {
			send_ticker(save, ship.owner(), 6, index);
		}
	}
	// 0x613920: escena del puerto (no se traduce).
}

// 0x508AA0: trabajo del astillero en los barcos que repara ([0x81E]): cada uno
// recibe `work` de casco y sale de la lista al quedar entero o si su orden
// [+0x136] es 1.
void fn_508aa0(SaveGame& save, int town, int32_t work) {
	TownMemory& t = save.towns[size_t(town)].memory;
	const uint16_t ships = ship_count(save);
	uint16_t* link = &t.u16(0x81E);
	while (*link < ships) {
		const uint16_t index = *link;
		SavedShip& ship = save.ships[index];
		const int32_t health = add32(ship.health(), work);
		const uint16_t readiness = ship.readiness();
		ship.notices() |= 0x100;
		const uint16_t notices = ship.notices();
		ship.health() = health;
		if (int16_t(readiness) < 0x700) {
			ship.readiness() = uint16_t(readiness + 0x100);
			ship.notices() = notices | 0x200;
		} else {
			ship.readiness() = 0x700;
		}
		if (health >= ship.max_health()) {
			t.s32(0x814) = add32(t.s32(0x814), add32(sub32(ship.max_health(), health), work));
			ship.health() = ship.max_health();
			*link = ship.next_in_list();
			// Si el dueño es un jugador humano y la orden no tiene los bits 0 y
			// 1: the ticker line «Repairs completed in X» (state 6, kind 7) or
			// «Ship improvements completed in X.» (kind 0xA) (0x508BC5, the ship).
			if (ship.owner() < merchant_count(save) && save.merchants[ship.owner()].memory.u16(8) == 0 &&
				!(ship.orders() & 3)) {
				send_ticker(save, ship.owner(), ship.status() == 6 ? 7 : 0xA, index);
			}
			ship.status() = 5;
			ship.timer() = 0;
			ship.next_in_list() = save.globals.u16(kDockedHead);
			if (ship.orders() == 2) {
				ship.orders() = 1;  // Checked with the oracle: scenario shipyard_repair (saves 3 and 5, direct calls).
			}
		} else if (ship.orders() == 1) {
			// Checked with the oracle: scenario shipyard_repair (saves 3 and 5, direct calls).
			t.s32(0x814) = add32(t.s32(0x814), work);
			*link = ship.next_in_list();
			ship.status() = 5;
			ship.timer() = 0;
			ship.next_in_list() = save.globals.u16(kDockedHead);
		} else {
			t.s32(0x814) = add32(t.s32(0x814), work);
			link = &ship.next_in_list();
			continue;
		}
		save.globals.u16(kDockedHead) = index;
		// 0x613920: escena del puerto (no se traduce).
	}
	// Sin traducir: los convoyes en reparación (lista [0x820] de la ciudad,
	// 0x508C86-0x508E8B; se hace si [0x820] < [0x6DD896]). Hacen falta los
	// convoyes (0x14 bytes en *0x6DD7A8), que SaveGame no tiene; [0x820] vale
	// 0xFFFF en todas las ciudades de las 12 partidas.
	(void)kConvoyCount;
}

// 0x51FF30: las obras de la ciudad avanzan con `workers` puntos (como mucho 5
// por obra y día); las que terminan pasan a su lista.
void fn_51ff30(SaveGame& save, int town, uint32_t workers) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	const bool siege = (t.u32(0x2C8) >> 4) & 1;
	const bool no_0x30 = (t.u32(0x2C8) >> 11) & 1;
	// [0x6E51AC] es la vista de la ciudad: si muestra esta, se redibujan las
	// obras (0x5891C0). Fuera de la simulación; aquí no hay vista.
	uint32_t left = workers;
	uint16_t* link = &t.u16(0x762);
	if (left == 0) {
		return;
	}
	do {
		const uint16_t index = *link;
		if (index >= t.u16(0x776)) {
			break;
		}
		BuildingRecord& r = st.records[index];
		// En asedio solo avanzan las obras de dentro de la muralla (celdas >= 0x80).
		if (siege && st.grid[size_t(r.x) * t.u32(0x7AC) + r.y] < 0x80) {
			link = &r.next;
			continue;
		}
		if (no_0x30 && r.type == 0x30) {
			link = &r.next;  // Checked with the oracle: scenario works_completion (saves 3 and 5, direct calls).
			continue;
		}
		if (r.pending > 5 || r.pending > left) {
			if (left < 5) {
				r.pending = uint8_t(r.pending - left);
				left = 0;
			} else {
				r.pending = uint8_t(r.pending - 5);
				left -= 5;
			}
			link = &r.next;
			continue;
		}
		// Terminada: sale de la lista de obras.
		left -= r.pending;
		r.pending = 0;
		*link = r.next;
		if (r.owner < merchant_count(save)) {
			const int office = fn_5308a0(save, r.owner, t.u8(0x2C0));
			uint8_t action = r.type - 1u < 0x38 ? kOwnedCompletion[r.type - 1] : uint8_t(kOwnedFree);
			if (office < 0 && (action == kOwnedBusiness || action == kOwnedHouse)) {
				// El juego escribiría en el puntero nulo; no puede pasar (quien
				// construye en una ciudad tiene oficina en ella).
				assert(false);
				action = kOwnedOwnList;
			}
			switch (action) {
			case kOwnedShipyard:
				// Checked with the oracle: scenario works_completion (saves 3 and 5, direct calls).
				t.u32(0x76C) |= 0x80;
				t.u16(0x854) = 1;  // Empleados del astillero (instalación 1).
				t.s32(0x2E0) = add32(t.s32(0x2E0), 4);
				t.s32(0x2D4) = add32(t.s32(0x2D4), 4);
				free_record(st, index);
				break;
			case kOwnedTownList:
				// Checked with the oracle: scenario works_completion (saves 3 and 5, direct calls).
				r.owner = 0xFF;
				r.next = t.u16(0x764);
				t.u16(0x764) = index;
				break;
			case kOwnedBusiness: {
				OfficeMemory& o = save.offices[size_t(office)].memory;
				r.next = o.u16(0x2CE);
				o.u16(0x2CE) = index;
				fn_4ff2d0(save, office);
				break;
			}
			case kOwnedHouse: {
				OfficeMemory& o = save.offices[size_t(office)].memory;
				r.next = o.u16(0x2D0);
				o.u16(0x2D0) = index;
				fn_4ff2d0(save, office);
				break;
			}
			case kOwnedOwnList:
				r.next = t.u16(0x764);
				t.u16(0x764) = index;
				break;
			case kOwnedWall:
				// Checked with the oracle: scenario works_completion (saves 3 and 5, direct calls).
				r.owner = 0xFF;
				r.next = t.u16(0x768);
				t.u16(0x768) = index;
				break;
			case kOwnedTower:
			case kOwnedBigTower:
				// Checked with the oracle: scenario works_completion (saves 3 and 5, direct calls).
				t.u16(0x786) = uint16_t(t.u16(0x786) + (action == kOwnedTower ? 1 : 3));
				r.owner = 0xFF;
				r.next = t.u16(0x768);
				t.u16(0x768) = index;
				break;
			case kOwnedReplaceWall:
				// Checked with the oracle: scenario works_completion (saves 3 and 5, direct calls).
				replace_wall(st, r.x, r.y);
				free_record(st, index);
				break;
			default:
				// Checked with the oracle: scenario works_completion (saves 3 and 5, direct calls).
				free_record(st, index);
				break;
			}
			{
				// The letter 0x03 to the record's owner (0x5202C1..0x5202EF; after
				// the cases that free it or give it to the town, 0xFF: nobody):
				// [+0x5] the town's [+0x2C0], u16 [+0x8] its type (0xFF when
				// freed), [+0xA] the record.
				const BuildingRecord& done = save.towns[size_t(town)].records[index];
				Letter letter(0x03);
				letter.entry.u8(5) = t.u8(0x2C0);
				letter.entry.u16(8) = done.type;
				letter.entry.u16(0xA) = index;
				send_letter(save, done.owner, letter);
			}
		} else if (r.type <= 0x38) {
			switch (kTownCompletion[r.type]) {
			case kTownFree:
				// Checked with the oracle: scenario works_completion (saves 3 and 5, direct calls).
				free_record(st, index);
				break;
			case kTownShipyard:
				// Checked with the oracle: scenario works_completion (saves 3 and 5, direct calls).
				t.u32(0x76C) |= 0x80;
				free_record(st, index);
				break;
			case kTownList:
				// Checked with the oracle: scenario works_completion (saves 3 and 5, direct calls).
				r.next = t.u16(0x764);
				t.u16(0x764) = index;
				break;
			case kTownHouse:
				r.next = t.u16(0x764);
				t.u16(0x764) = index;
				fn_520bd0(save, town);
				break;
			case kTownFlag:
				// Checked with the oracle: scenario works_completion (saves 3 and 5, direct calls).
				t.u32(0x76C) |= 0x20000;
				free_record(st, index);
				break;
			case kTownWall:
				r.next = t.u16(0x768);
				t.u16(0x768) = index;
				break;
			case kTownReplaceWall:
				// Checked with the oracle: scenario works_completion (saves 3 and 5, direct calls).
				replace_wall(st, r.x, r.y);
				free_record(st, index);
				break;
			}
		}
		// Con un tipo > 0x38 sin dueño el registro se queda fuera de toda lista.
	} while (left > 0);
}

// 0x52BB00: nivel del astillero (2..5) según su experiencia [0x810].
uint32_t fn_52bb00(const SaveGame& save, int town) {
	const uint32_t level = uint32_t((uint64_t(save.towns[size_t(town)].memory.u32(0x810)) * 0x9FC8735) >> 46) + 2;
	return level < 6 ? level : 5;
}

// 0x4D36E0 (registro del mundo `record`, n empresas del tipo en la oficina).
void fn_4d36e0(SaveGame& save, uint32_t record, int32_t n) {
	set_office_business(save, save.world_records[record], n);
}

// 0x4D3490: prepara un registro del mundo como empresa de una oficina.
void fn_4d3490(SaveGame& save, uint32_t record, uint32_t town, uint32_t type, int32_t n) {
	GameMemory<0x14>& r = save.world_records[record];
	r.u16(0xA) = 0;
	r.u16(0xC) = 0;
	r.u16(0xE) = 0;
	r.s32(0x0) = 0;
	r.u16(0x4) = 0;
	r.u8(0x7) = uint8_t(town);
	r.u8(0x6) = uint8_t(type);
	r.u16(0x8) = 0xFFFF;
	r.u32(0x10) = 0x3F800000;  // 1.0f
	set_office_business(save, r, n);
}

// 0x5306B0: toma un registro libre del mundo; si no queda ninguno, agranda la
// tabla (a 0x80 o 0x40 más) con registros llenos de 0xFF.
uint16_t fn_5306b0(SaveGame& save) {
	GlobalMemory& g = save.globals;
	if (g.u16(kWorldRecordFree) >= g.u16(kWorldRecordCount)) {
		const uint16_t old = g.u16(kWorldRecordCount);
		g.u16(kWorldRecordFree) = old;
		// Growing by 0x40: checked with the oracle, scenario world_records (saves 3 and 5, direct
		// calls).
		const uint16_t size = old < 0x14 ? 0x80 : uint16_t(old + 0x40);
		g.u16(kWorldRecordCount) = size;
		save.world_records.resize(size);
		for (uint32_t i = old; i < size; ++i) {
			save.world_records[i].bytes.fill(0xFF);
			save.world_records[i].u16(0x8) = uint16_t(i + 1);
		}
	}
	const uint16_t index = g.u16(kWorldRecordFree);
	GameMemory<0x14>& r = save.world_records[index];
	g.u16(kWorldRecordFree) = r.u16(0x8);
	r.u16(0x8) = 0xFFFF;
	return index;
}

// 0x530780: devuelve un registro del mundo a la lista libre. Si era una empresa
// con empleados, el doble salen de los pobres y de los acomodados.
void fn_530780(SaveGame& save, uint32_t record) {
	// Checked with the oracle: scenario world_records (saves 3 and 5, direct calls).
	GlobalMemory& g = save.globals;
	const uint16_t count = g.u16(kWorldRecordCount);
	const uint16_t head = g.u16(kWorldRecordFree);
	for (uint32_t i = head; i < count; i = save.world_records[i].u16(0x8)) {
		if (i == record) {
			return;
		}
	}
	GameMemory<0x14>& r = save.world_records[record];
	r.u16(0x8) = head;
	g.u16(kWorldRecordFree) = uint16_t(record);
	const uint32_t employees = r.u16(0x4);
	if (employees == 0 || r.u8(0x7) >= g.u32(global::kWorldTownCount)) {
		return;
	}
	TownMemory& t = save.towns[r.u8(0x7)].memory;
	r.u16(0x4) = uint16_t(employees * 2);
	const int32_t doubled = int32_t((employees * 2) & 0xFFFF);
	if (t.s32(0x2E0) < doubled) {
		r.u16(0x4) = uint16_t(employees * 4 - uint32_t(t.s32(0x2E0)));
		t.s32(0x2E0) = 0;
	} else {
		t.s32(0x2E0) -= doubled;
	}
	if (t.s32(0x2DC) < int32_t(r.u16(0x4))) {
		t.s32(0x2DC) = 0;
	} else {
		t.s32(0x2DC) -= r.u16(0x4);
	}
	t.s32(0x2D4) = 0;
	for (size_t k = 0; k < 4; ++k) {
		t.s32(0x2D4) = add32(t.s32(0x2D4), t.s32(0x2D8 + 4 * k));
	}
}

}  // namespace facilities_detail

using namespace facilities_detail;

// 0x510C50: la milicia. Cada semana un recluta de cada tipo pasa a soldado;
// los empleados son 5 por soldado; se paga a los soldados (si no llega el
// dinero se van licenciando) y se recluta o licencia según los habitantes y
// las defensas de la muralla, salvo si el alcalde es un jugador humano.
void fn_510c50(SaveGame& save, int town, int facility) {
	FacilityMemory& f = town_facility(save, town, facility);
	const int home = f.u8(0x7);
	TownMemory& t = save.towns[size_t(home)].memory;
	// 0x51A850/0x51A880: objeto de un mensaje que no se usa.
	const bool weekly = (save.globals.u32(global::kWorldTick) >> 8) % 7 == 0;
	int32_t soldiers = 0;
	for (size_t i = 0; i < 4; ++i) {
		if (weekly && t.u8(0x99C + i) > 0) {
			t.u8(0x99C + i) -= 1;
			t.u8(0x998 + i) += 1;
		}
		soldiers += t.u8(0x99C + i) + t.u8(0x998 + i);
	}
	const uint32_t employees = uint32_t(soldiers) * 5;
	if (f.u16(0x4) != employees) {
		// Checked with the oracle: scenario militia (saves 3 and 5, direct calls).
		const int32_t moved = int32_t((employees - f.u16(0x4)) << 2);
		f.u16(0x4) = uint16_t(employees);
		const int32_t beggars = sub32(t.s32(0x2E4), moved);
		t.s32(0x2E4) = beggars;
		if (beggars < 0) {
			t.s32(0x2E4) = 0;
			t.s32(0x2D4) = sub32(t.s32(0x2D4), beggars);
		}
		const int32_t poor = add32(t.s32(0x2E0), moved);
		t.s32(0x2E0) = poor;
		if (poor < 0) {
			t.s32(0x2E0) = add32(add32(poor, t.s32(0x2D8) - 1), t.s32(0x2DC) - 1);
			t.s32(0x2D8) = 1;
			t.s32(0x2DC) = 1;
		}
	}
	int32_t pay = 0;
	for (size_t i = 0; i < 4; ++i) {
		pay += (t.u8(0x998 + i) + t.u8(0x99C + i)) * kSoldierPay[i];
	}
	if (pay > t.s32(0x700)) {
		t.u8(0x835) = uint8_t(t.u8(0x835) + 2);
		if (t.u8(0x835) >= 10) {
			t.u8(0x835) = 0;
			// Licencia un recluta del tipo con más reclutas o, si no hay, un
			// soldado del tipo con más soldados.
			uint32_t most = 0;
			for (uint32_t i = 0; i < 4; ++i) {
				if (t.u8(0x99C + i) > t.u8(0x99C + most)) {
					most = i;
				}
			}
			if (!(t.u8(0x2C8) & 0x10)) {
				if (t.u8(0x99C + most) > 0) {
					fn_51d410(save, home, most);
				} else {
					most = 0;
					for (uint32_t i = 0; i < 4; ++i) {
						if (t.u8(0x998 + i) > t.u8(0x998 + most)) {
							most = i;
						}
					}
					if (t.u8(0x998 + most) > 0) {
						fn_51d410(save, home, most);
					}
				}
			}
		}
	}
	fn_529f90(save, home, pay, 5);
	const uint8_t mayor = t.u8(0x6F1);
	if (mayor < merchant_count(save) && save.merchants[mayor].memory.u16(0x8) == 0) {
		return;  // Checked with the oracle: scenario militia (saves 3 and 5, direct calls).
	}
	if (t.u32(0x2C8) & 0xF10) {
		for (uint32_t i = 0; i < 4; ++i) {
			fn_51d4c0(save, home, i);
		}
		return;
	}
	// Un soldado del tipo 0 por cada 80 habitantes.
	const int32_t wanted = t.s32(0x2D4) / 80;
	const int32_t guards = t.u8(0x998) + t.u8(0x99C);
	if (wanted > guards) {
		fn_51d4c0(save, home, 0);
	} else if (wanted < guards && wanted > 0) {
		fn_51d410(save, home, 0);  // Checked with the oracle: scenario militia (saves 3 and 5, direct calls).
	}
	// De los tipos 1..3, uno por cada 5 defensas de la muralla.
	const int32_t wanted_others = int32_t(t.u16(0x786)) / 5;
	int32_t others = 0;
	for (size_t i = 1; i < 4; ++i) {
		others += t.u8(0x998 + i) + t.u8(0x99C + i);
	}
	if (wanted_others > others) {
		fn_51d4c0(save, home, (save.globals.u32(global::kWorldDayOfYear) & 1) + 1);
	} else if (wanted_others < others) {
		// Checked with the oracle: scenario militia (saves 3 and 5, direct calls).
		if (t.u8(0x99D) + t.u8(0x999) > 0) {
			fn_51d410(save, home, 1);
		} else if (t.u8(0x99E) + t.u8(0x99A) > 0) {
			fn_51d410(save, home, 2);
		} else {
			fn_51d410(save, home, 3);
		}
	}
}

// 0x50E500: la milicia bebe cerveza del almacén (la ciudad).
void fn_50e500(SaveGame& save, int town, int facility) {
	militia_beer(save, town_facility(save, town, facility), town_storage(save, town));
}

void fn_50e500(SaveGame& save, StorageMemory& s, FacilityMemory& f) {
	militia_beer(save, f, s);  // Unreachable: the offices never have businesses of types 0..3 (their
	// buildings of those types go to the town, 0x51FF30), so 0x4D3780 never
	// calls this.
}

// 0x50E570: el astillero gasta brea del almacén (la ciudad) y trabaja en los
// barcos que construye y repara (100 de casco por empleado y día).
void fn_50e570(SaveGame& save, int town, int facility) {
	shipyard(save, town_facility(save, town, facility), town_storage(save, town));
}

void fn_50e570(SaveGame& save, StorageMemory& s, FacilityMemory& f) {
	shipyard(save, f, s);  // Unreachable: the offices never have businesses of types 0..3 (their
	// buildings of those types go to the town, 0x51FF30), so 0x4D3780 never
	// calls this.
}

// 0x50E610: obras de la ciudad. Avanzan con un punto por empleado (0x51FF30);
// devuelve los empleados que quiere: 5 · (habitantes / 5000 + 5), menos si los
// habitantes están descontentos, y 5 como mínimo.
uint32_t fn_50e610(SaveGame& save, int town, int facility) {
	return fn_50e610(save, town_storage(save, town), town_facility(save, town, facility));
}

uint32_t fn_50e610(SaveGame& save, StorageMemory& s, FacilityMemory& f) {
	(void)s;  // El almacén no se usa: la ciudad es la de la instalación.
	const int home = f.u8(0x7);
	const TownMemory& t = save.towns[size_t(home)].memory;
	int32_t wanted = (t.s32(0x2D4) / 5000 + 5) * 5;
	fn_51ff30(save, home, f.u16(0x4));
	const int32_t mood = int32_t(t.s16(0x304)) * 5 + int32_t(t.s16(0x302)) * 2 + t.s16(0x300);
	if (mood >= 0) {
		return uint32_t(wanted);
	}
	wanted += (mood + 7) / 8;
	return uint32_t(wanted < 5 ? 5 : wanted);
}

// 0x510AA0: qué fabrica el armero. Sigue con lo que fabricaba ([0x6D0]) si es
// un arma que sabe hacer; si no, lo que menos hay respecto a lo que se quiere:
// un arma (20..23, según el nivel del astillero), una pieza de artillería
// (24 + i), armas de mano (30) o nada (0xFF).
uint32_t fn_510aa0(SaveGame& save, int town, int facility) {
	const FacilityMemory& f = town_facility(save, town, facility);
	const TownMemory& t = town_of(save, f);
	uint32_t result = 0xFF;
	int32_t lowest = 0x400;
	int32_t level = int32_t(fn_52bb00(save, f.u8(0x7)));
	uint32_t last_weapon;
	if (level <= 1) {
		level = 2;  // Unreachable: 0x52BB00 gives 2..5.
		last_weapon = 0x15;
	} else if (level <= 2) {
		last_weapon = 0x15;
	} else {
		last_weapon = 0x16 + (level > 4 ? 1 : 0);
	}
	// Espadas (20) solo con hierro (mercancía 16) en el almacén.
	uint32_t ware = 0x14 + (t.s32(0x44) <= 0 ? 1 : 0);
	const uint32_t current = t.u32(0x6D0);
	if (current >= ware && current <= last_weapon) {
		return current;  // Checked with the oracle: scenario weaponsmith (saves 3 and 5, direct calls).
	}
	// Armas: existencias × 1024 / tercer umbral de precio.
	for (; ware <= last_weapon; ++ware) {
		const int32_t stock = t.s32(0x4 + 4 * ware);
		const int32_t threshold = t.s32(0x4F0 + 16 * ware + 8);
		if (stock <= threshold) {
			const int32_t ratio = int32_t(int64_t(int32_t(uint32_t(stock) << 10)) / threshold);
			if (ratio < lowest) {
				lowest = ratio;
				result = ware;
			}
		}
	}
	// Artillería: piezas × 512 / (peso · (habitantes / 512 + 5 + (2 - ciudad) % 4)).
	const int32_t demand = int32_t((0xFFFFFFFEu - f.u8(0x7)) & 3) + sar(t.s32(0x2D4), 9) + 5;
	for (int32_t i = 0; i <= level; ++i) {
		const int32_t divisor = mul32(kArtilleryWeight[i], demand);
		const int32_t ratio = int32_t(int64_t(int32_t(uint32_t(t.s32(0x124 + 4 * size_t(i))) << 9)) / divisor);
		if (ratio < lowest) {
			lowest = ratio;
			result = uint32_t(i) + 0x18;
		}
	}
	// Armas de mano.
	int32_t yard = 0;
	if (t.f32(0x818) > 0.0f) {
		yard = sar(int32_t(uint32_t(ftol(double(t.f32(0x818))))), 2);
		if (yard > 15) {
			yard = 15;
		}
	}
	const int32_t b = int32_t(t.s8(0x824)) + 2;
	const uint32_t divisor = uint32_t(add32(b, add32(yard, b * 4)));
	// Con divisor 0 el juego divide por cero.
	const uint32_t ratio = divisor ? (t.u32(0x2BC) << 9) / divisor : 0;
	if (int32_t(ratio) < lowest) {
		result = 0x1E;
	}
	return result & 0xFF;
}

// 0x50F6E0: el armero fabrica `item` (lo que eligió 0x510AA0) con madera,
// cuero, cáñamo y herramientas del almacén de la ciudad.
void fn_50f6e0(SaveGame& save, int town, int facility, uint32_t item) {
	weaponsmith(save, town_facility(save, town, facility), town_storage(save, town), item);
}

void fn_50f6e0(SaveGame& save, StorageMemory& s, FacilityMemory& f, uint32_t item) {
	weaponsmith(save, f, s, item);  // Unreachable: the offices never have businesses of types 0..3 (their
	// buildings of those types go to the town, 0x51FF30), so 0x4D3780 never
	// calls this.
}

// 0x4FF2D0: recuenta las empresas y casas de una oficina: almacenes ([0x2EC],
// [0x2EE]), empresas agrupadas con una vecina del mismo tipo ([+0x7] del
// registro), casas por clase ([0x2D8]) y una empresa de oficina (registro del
// mundo, lista [0x2CC]) por cada tipo que tiene.
void fn_4ff2d0(SaveGame& save, int office) {
	OfficeMemory& o = save.offices[size_t(office)].memory;
	const int town = o.u8(0x2C6);
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	const uint16_t count = t.u16(0x776);
	int16_t businesses[21] = {};
	o.u16(0x2D2) = 0;
	if (o.u16(0x2F0) > 0) {
		o.u16(0x2EC) = 0;  // Checked with the oracle: scenario office_recount (saves 3 and 5, direct calls).
		o.u16(0x2EE) = 0x32;
	} else {
		o.u16(0x2EC) = 0x32;
		o.u16(0x2EE) = 0;
	}
	// Almacenes (tipo 0x1E) en obras.
	for (uint32_t i = t.u16(0x762); i < count; i = st.records[i].next) {
		const BuildingRecord& r = st.records[i];
		if (r.owner == o.u16(0x2C4) && r.type == 0x1E) {
			o.u16(0x2EE) = uint16_t(o.u16(0x2EE) + 200);
		}
	}
	// Empresas (tipos < 0x15) y almacenes terminados.
	for (uint32_t i = o.u16(0x2CE); i < count; i = st.records[i].next) {
		BuildingRecord& r = st.records[i];
		if (r.type < 0x15) {
			++businesses[r.type];
		} else if (r.type == 0x1E) {
			if (r.pending > 0) {
				r.pending = 0;  // Checked with the oracle: scenario office_recount (saves 3 and 5, direct calls).
			}
			o.u16(0x2EC) = uint16_t(o.u16(0x2EC) + 200);
		}
		o.u16(0x2D2) = uint16_t(o.u16(0x2D2) + 1);
	}
	// Las de los tipos de kGroupedTypes se marcan si tienen una vecina del mismo
	// tipo más adelante en la lista: [+0x7] = siguiente de la lista & 0x7F.
	for (uint32_t i = o.u16(0x2CE); i < count;) {
		BuildingRecord& r = st.records[i];
		const uint32_t next = r.next;
		if ((uint32_t(1) << (r.type & 31)) & kGroupedTypes) {
			r.param = 0xFF;
			// En la lista solo hay tipos < 0x15 y 0x1E, que no está en kGroupedTypes.
			assert(r.type < 21);
			if (businesses[r.type] > 0) {
				// Las 20 celdas vecinas: 4 y, desde cada una, bajando en diagonal.
				uint8_t x[20], y[20];
				const uint8_t odd = r.y & 1;
				x[0] = uint8_t(r.x + 1);
				y[0] = uint8_t(r.y + 6);
				x[1] = uint8_t(r.x + odd - 2);
				y[1] = uint8_t(r.y + 7);
				x[2] = uint8_t(r.x - 3);
				y[2] = uint8_t(r.y - 2);
				x[3] = uint8_t(r.x + odd + 3);
				y[3] = uint8_t(r.y - 3);
				for (size_t k = 4; k < 20; ++k) {
					x[k] = x[k - 4];
					y[k] = uint8_t(y[k - 4] - 1);
					if (k % 2 == 0) {
						if (y[k - 4] & 1) {
							x[k] = uint8_t(x[k] + 1);
						}
					} else if (y[k] & 1) {
						x[k] = uint8_t(x[k] - 1);
					}
				}
				for (uint32_t j = next; j < count;) {
					const BuildingRecord& other = st.records[j];
					j = other.next;
					if (other.type != r.type) {
						continue;
					}
					for (size_t k = 0; k < 20; ++k) {
						if (other.y == y[k] && other.x == x[k]) {
							r.param = uint8_t(next & 0x7F);
							j = 0xFFFF;
							break;
						}
					}
				}
			}
		}
		i = next;
	}
	// Casas: plazas por clase y cuántas están terminadas.
	o.u16(0x2D4) = 0;
	o.u16(0x2DC) = 0;
	o.u16(0x2DA) = 0;
	o.u16(0x2D8) = 0;
	for (uint32_t i = o.u16(0x2D0); i < count; i = st.records[i].next) {
		BuildingRecord& r = st.records[i];
		r.owner = o.u8(0x2C4);
		if (r.pending == 0) {
			o.u16(0x2D4) = uint16_t(o.u16(0x2D4) + 1);
		}
		const int32_t house = (int32_t(r.type) - 0x15) / 3;
		// En la lista solo hay casas (tipos 0x15..0x1D).
		assert(house >= 0 && house < 3);
		o.u16(0x2D8 + 2 * size_t(house)) = uint16_t(o.u16(0x2D8 + 2 * size_t(house)) + kHouseCapacity[house]);
	}
	// Empresas de la oficina: se actualizan las de los tipos que tiene y se
	// quitan (despidiendo a todos) las de los que ya no tiene.
	uint16_t* link = &o.u16(0x2CC);
	while (*link < save.globals.u16(kWorldRecordCount)) {
		const uint16_t index = *link;
		GameMemory<0x14>& r = save.world_records[index];
		const uint32_t type = r.u8(0x6);
		assert(type < 21);
		const int16_t n = businesses[type];
		if (n > 0) {
			fn_4d36e0(save, index, n);
			businesses[type] = -1;
			link = &r.u16(0x8);
		} else {
			// A business whose buildings are gone. Checked with the oracle: scenario
			// office_recount (saves 3 and 5, direct calls).
			fn_50e380(save, facility_of(r), r.u16(0x4), true);
			*link = r.u16(0x8);
			r.u16(0x8) = 0xFFFF;
			fn_530780(save, index);
		}
	}
	// Y se crean las de los tipos nuevos (del 20 al 1).
	for (uint32_t type = 20; type >= 1; --type) {
		const int16_t n = businesses[type];
		if (n > 0) {
			const uint16_t index = fn_5306b0(save);
			fn_4d3490(save, index, o.u8(0x2C6), type, n);
			save.world_records[index].u16(0x8) = o.u16(0x2CC);
			o.u16(0x2CC) = index;
		}
	}
}

}  // namespace p3::data::tick
