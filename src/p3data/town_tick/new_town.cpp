// Fundar una ciudad en plena partida (el encargo de la Hansa de tipo 0,
// alderman.cpp; docs/formats/world_tick.md §8): 0x531D50 sobre el mundo y lo
// que cuelga. Amplía las ciudades y sus asedios, construye la ciudad nueva
// (0x51B100) y la prepara: candidatos a alcalde (0x5289B0), capitanes
// (0x526A50), astillero y plantilla del mapa (0x545890:
// iso/towns/stadtplatz%.2u.tow), lo demás (0x5458D0), las rutas (0x448040,
// Sailing::add_town) y la matriz de tiempos de viaje (0x531BD0). Después, sus
// eventos: la muralla (0x39, 0x4ECDF4) y su desarrollo (0x2F, 0x4EA0D4: el
// alcalde, con la operación 0x46 si no tiene, 0x535FD6; su oficina, la
// población y el prestamista).
//
// Los punteros que el juego deja en los globales (los nombres de la ciudad,
// 0x5127E0: [0x6DD960 + 4·i] y [0x6DDA00 + 4·i]) no se reproducen.

#include <cstdlib>
#include <cstring>

#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

namespace events_detail {
uint16_t fn_5097c0(SaveGame& save, bool keep);
void fn_4fe190(CaptainMemory& c);
}  // namespace events_detail
namespace core_detail {
void fn_4fa6f0(SaveGame& save, int town);
}  // namespace core_detail
namespace facilities_detail {
uint8_t fn_5252f0(const SaveGame& save, int town);
}  // namespace facilities_detail
namespace events_rare_detail {
void fn_522b20(SaveGame& save, int town, uint32_t side);
}  // namespace events_rare_detail
namespace events_towns_detail {
int fn_5308a0_create(SaveGame& save, uint32_t merchant, uint32_t town);
}  // namespace events_towns_detail
namespace construction_detail {
uint32_t fn_51fab0(SaveGame& save, int town, uint8_t type, bool immediate);
}  // namespace construction_detail

namespace {

constexpr uint32_t kTownCount = global::kWorldTownCount;  // u32
constexpr uint32_t kMerchantCount = global::kWorldMerchantCount;  // u16
constexpr uint32_t kTick = global::kWorldTick;  // u32
constexpr uint32_t kAldermanBusy = 0x6DF31C;  // u32 (ver merchants.cpp)
constexpr uint32_t kEventHead = 0x6DD73C + 0x8;  // u16 primer evento de la cola.
constexpr uint32_t kYear = global::kWorldYear;  // u16
constexpr uint32_t kTownIds = 0x6DE4B8;  // u8 por ciudad ([+0x18] del mundo): su número de StadtDaten.ini.
constexpr uint32_t kTownMaps = 0x6DE4E0;  // u8 por ciudad ([+0x40] del mundo): su plantilla de mapa.
constexpr uint32_t kTownTable = 0x6DDB90;  // 0x34 bytes por número: [+0x20], [+0x24] s32 x, y.
constexpr uint32_t kMaleNames = 0x6DDB70;  // u16
constexpr uint32_t kFamilyNames = 0x6DDB74;  // u16
constexpr uint32_t kAlderman = 0x6DE52D;  // u8 el jefe de la Hansa.
constexpr uint32_t kAldermanTown = 0x6DE52E;  // u8 su ciudad.

// 0x673818: experiencia del astillero que pide cada nivel de cada tipo de
// barco (u16; 0x545BE0 la lee desde el nivel −1, por eso empieza aquí).
constexpr uint16_t kShipyardLevel[16] = {0, 100, 300, 1050, 0, 100, 600, 900, 0, 200, 400, 800, 300, 500, 600, 1200};

TownMemory& town_of(SaveGame& save, int town) {
	return save.towns[size_t(town)].memory;
}

}  // namespace

namespace new_town_detail {

// 0x5100F0 (sobre una instalación): vacía, del tipo `type`, de la ciudad `town`.
void fn_5100f0(FacilityMemory& f, uint8_t town, uint8_t type) {
	f.u8(0x6) = type;
	f.u32(0x0) = 0;
	f.u16(0x4) = 0;
	f.u8(0x7) = town;
	f.u16(0x8) = 0;
	f.u16(0xA) = 0;
	f.u32(0xC) = 0;
}

// 0x51B100: el constructor de la ciudad (sobre memoria a 0, como la deja el
// oráculo; en el juego, la del montón): 0x51E780 (0x5286E0, que llama a
// 0x527890, y las listas, town_class1 con 0x4F95C0 y 0x4F9930, la iglesia
// con 0x4FE2B0), los muelles (0x525EE0), el astillero y las 21 instalaciones.
void fn_51b100(SaveGame& save, int town) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	const GlobalMemory& g = save.globals;
	// 0x5286E0 (con 0x527890 y 0x52DE70).
	std::memset(t.bytes.data(), 0, 0x2C0);
	t.u32(0x2C8) = 0;
	t.u32(0x2D0) = 0;
	for (size_t w = 0; w < 24; ++w) {
		t.u32(0x4 + 4 * w) = 0;
		t.u32(0x370 + 4 * w) = 0;
		t.u32(0x3D0 + 4 * w) = 0;
		t.u32(0x430 + 4 * w) = 0;
		t.u32(0x490 + 4 * w) = 0;
		for (size_t k = 0; k < 4; ++k) {
			t.u32(0x4F0 + 16 * w + 4 * k) = 0;
		}
		t.u32(0x670 + 4 * w) = 0;
	}
	t.u32(0x6D0) = 0;
	t.u8(0x6E4) = 5;
	t.u8(0x6E5) = 5;
	t.u8(0x6F0) = 0xFF;
	t.u8(0x6F1) = 0xFF;
	t.u8(0x6F2) = 0xFF;
	t.u8(0x6F3) = 0;
	t.u32(0x6FC) = 0;
	for (size_t k = 0; k < 8; ++k) {
		t.u8(0x6D4 + k) = 0;
	}
	t.u8(0x6E6) = 4;
	t.u8(0x6E7) = 4;
	if (g.u16(kMerchantCount) <= 0xFF) {
		t.u8(0x6E4) = g.u8(kAldermanTown) == t.u8(0x2C0) && g.u8(kAlderman) == 0 ? 7 : 6;
	}
	// town_class1 (0x4F95C0).
	auto class1_fill = [&]() {
		std::memset(t.bytes.data() + 0x7B0, 0xFF, 8);
		std::memset(t.bytes.data() + 0x7B8, 0xFF, 8);
		std::memset(t.bytes.data() + 0x7C0, 0xFF, 0x10);
		std::memset(t.bytes.data() + 0x7D0, 0xFF, 0x20);
	};
	t.u32(0x7AC) = 0;
	t.u32(0x7A8) = 0;
	t.u32(0x80C) = 0;
	class1_fill();
	std::memset(t.bytes.data() + 0x7F0, 0, 0x10);
	std::memset(t.bytes.data() + 0x800, 0xFF, 0xC);
	t.u32(0x7A4) = 1;
	st.grid.clear();
	// Las listas de registros y lo demás de 0x51E780.
	t.u16(0x762) = 0xFFFF;
	t.u16(0x76A) = 0xFFFF;
	t.u16(0x764) = 0xFFFF;
	t.u16(0x768) = 0xFFFF;
	t.u16(0x766) = 0xFFFF;
	t.u16(0x784) = 0xFFFF;
	t.u32(0x75C) = 0;
	st.records.clear();
	t.u16(0x776) = 0;
	t.u16(0x760) = 0;
	t.u8(0x774) = 0;
	t.u32(0x76C) = 0;
	std::memset(t.bytes.data() + 0x778, 0, 6);
	std::memset(t.bytes.data() + 0x77E, 0, 6);
	for (size_t o : {0x770u, 0x771u, 0x772u, 0x773u, 0x789u, 0x788u, 0x78Au}) {
		t.u8(o) = 0;
	}
	t.u16(0x78C) = 1;
	t.u16(0x78E) = 1;
	t.u16(0x786) = 1;
	t.u16(0x790) = 0;
	t.u16(0x792) = 0;
	// town_class1 otra vez (0x4F9930): sin rejilla.
	t.u32(0x7A4) = 1;
	class1_fill();
	std::memset(t.bytes.data() + 0x800, 0xFF, 0xC);
	t.u32(0x80C) = 0;
	st.grid.clear();
	// La iglesia (0x4FE2B0).
	t.u32(0x794 + 0x8) = 0xFF;
	t.u32(0x794) = 0;
	t.u32(0x794 + 0x4) = 0;
	for (size_t k = 0xC; k < 0x10; ++k) {
		t.u8(0x794 + k) = 0;
	}
	// El resto de 0x51B100.
	t.u16(0x9A2) = 0xFFFF;
	t.u32(0x9B4) = 0;
	st.nodes.clear();
	t.u32(0x9EC) = 0;
	t.u32(0x9F0) = 0;
	t.u32(0x9F4) = 0;
	std::memset(t.bytes.data(), 0, 0x6D4);
	std::memset(t.bytes.data(), 0, 0x2C0);
	// 0x525EE0: los muelles.
	t.u8(0x83C) = 0;
	t.u8(0x83D) = 0xFF;
	t.u8(0x83E) = 0xFF;
	t.u8(0x83F) = 0;
	t.u8(0x82C) = 0xFF;
	t.u8(0x82D) = 0;
	t.u16(0x82E) = 0xFFFF;
	t.u16(0x830) = 0xFFFF;
	t.u16(0x832) = 0xFFFF;
	t.u8(0x834) = 0xFF;
	t.u8(0x835) = 0;
	std::memset(t.bytes.data(), 0, 0x2C0);
	std::memset(t.bytes.data() + 0x824, 0, 4);
	t.u32(0x810) = 0;
	t.u32(0x814) = 0;
	t.u32(0x818) = 0;
	t.u16(0x81C) = 0xFFFF;
	t.u16(0x81E) = 0xFFFF;
	t.u16(0x820) = 0xFFFF;
	std::memset(t.bytes.data() + 0x828, 0, 4);
	for (uint8_t k = 0; k < 21; ++k) {
		fn_5100f0(facility_of(t, k), 0, k);
	}
	t.u8(0x6F2) = 0xFF;
	t.u32(0x990) = 0;
	t.u16(0x994) = 0xFFFF;
	t.u16(0x996) = 0;
	for (size_t k = 0; k < 4; ++k) {
		t.u8(0x998 + k) = 0;
		t.u8(0x99C + k) = 0;
	}
	t.u16(0x9A2) = 0xFFFF;
	std::memset(t.bytes.data() + 0x9A4, 0, 0x10);
	std::memset(t.bytes.data() + 0x9C6, 0, 0x12);
	std::memset(t.bytes.data() + 0x9D8, 0, 0x12);
	t.u32(0x9B8) = 0;
	t.u8(0x9BC) = 0;
	t.u8(0x9BD) = 0;
	t.u32(0x9C0) = 0;
	t.u8(0x9C4) = 0;
}

// 0x6334B0: el constructor del objeto de asedio de una ciudad (con 0x6339C0,
// que lo vacía: sin ejército ni listas).
void fn_6334b0(SiegeMemory& s) {
	s.u8(0x4) = 0xFF;
	for (size_t o = 0x1010; o < 0x1014; ++o) {
		s.u8(o) = 0xFF;
	}
	s.u32(0x0) = 0;
	s.u32(0x8) = 0;
	s.u16(0x1018) = 0x3FFF;
	s.u8(0x101A) = 2;
	for (size_t o = 0x1014; o < 0x1018; ++o) {
		s.u8(o) = 0;
	}
	s.u32(0x101C) = 0;
	s.u32(0x1020) = 0;
	s.u8(0x101B) = 0;
	// 0x6339C0.
	s.u32(0xC) = 0;
	s.u32(0x10) = 0;
	s.u8(0x100E) = 0;
	s.u8(0x100F) = 0;
	std::memset(s.bytes.data() + 0x14, 0, 0x3FC);
	std::memset(s.bytes.data() + 0x410, 0, 0x3FC);
	std::memset(s.bytes.data() + 0x80C, 0, 0x3FC);
	std::memset(s.bytes.data() + 0xC08, 0, 0x3FC);
	s.u32(0x1004) = 0;
	s.u32(0x1008) = 0;
	s.u16(0x100C) = 0;
}

// 0x5289B0 (sobre la ciudad): los ciudadanos que se presentan a alcalde (sus
// nombres [0x6D4 + j] y apellidos [0x6D8 + j], distintos de los de las demás
// ciudades, y los nombres de [0x6E8] y [0x6EC]) con una serie que empieza en
// `seed` y sube cada vez lo que se ha añadido más `step`; títulos y rangos,
// el día [0x6FC], impuestos 10 y el dinero de la ciudad (250 000 con `flag`).
void fn_5289b0(SaveGame& save, int town, uint32_t flag, uint32_t seed, uint32_t step) {
	TownMemory& t = town_of(save, town);
	const GlobalMemory& g = save.globals;
	t.u32(0x6FC) = (seed & 0x3FFF) + g.u32(kTick);
	t.u8(0x6F3) = 0;
	uint32_t v = seed;
	// Nombres y apellidos distintos entre sí.
	auto pick = [&](size_t base, uint32_t count) {
		for (uint32_t i = 0; i < 4; ++i) {
			v %= count;
			for (uint32_t c = 0; c < i;) {
				if (t.u8(base + c) == uint8_t(v)) {
					if (++v >= count) {
						v = 0;  // Checked with the oracle: scenario mayor_names (saves 3 and 5, direct calls).
					}
					c = 0;
					continue;
				}
				++c;
			}
			t.u8(base + i) = uint8_t(v);
			v += i + step;
		}
	};
	pick(0x6D4, g.u16(kMaleNames));
	pick(0x6D8, g.u16(kFamilyNames));
	// Que no se llamen como los de otra ciudad.
	bool name_first = true;
	uint32_t checked = 0;
	for (uint32_t j = 0; j < 4; ++j) {
		for (int32_t other = int32_t(g.u32(kTownCount)); other > 0;) {
			--other;
			if (uint32_t(other) == t.u8(0x2C0)) {
				continue;
			}
			const TownMemory& o = save.towns[size_t(other)].memory;
			for (uint32_t k = 0; k < 4; ++k) {
				if (o.u8(0x6D8 + k) != t.u8(0x6D8 + j) || o.u8(0x6D4 + k) != t.u8(0x6D4 + j)) {
					continue;
				}
				// The same pair as another town's. Checked with the oracle: scenario
				// mayor_names (saves 3 and 5, direct calls).
				if (name_first) {
					t.u8(0x6D4 + j) = uint8_t(t.u8(0x6D4 + j) + 1);
					if (t.u8(0x6D4 + j) >= 0x50) {
						t.u8(0x6D4 + j) = 0;
					}
				} else {
					t.u8(0x6D8 + j) = uint8_t(t.u8(0x6D8 + j) + 1);
					if (g.u16(kFamilyNames) <= t.u8(0x6D8 + j)) {
						t.u8(0x6D8 + j) = 0;
					}
				}
				name_first = !name_first;
				other = int32_t(g.u32(kTownCount));
			}
			if (checked++ > 100000) {
				// Unreachable: each clash moves the name or the family on and the other
				// towns hold at most 100 pairs, so a draw makes at most about 2500 checks.
				j = 4;
				break;
			}
		}
	}
	t.u32(0x6DC) = 0xFFFFFFFF;
	t.u8(0x6E0) = 3;
	t.u8(0x6E1) = 2;
	t.u8(0x6E2) = 2;
	t.u8(0x6E3) = 2;
	// [0x6E8 + i]: se guarda el primero; los repetidos con [0x6E4 + c] (que
	// aún no tiene nada) solo cambian la serie, y vuelven a mirar desde el 1.
	for (uint32_t i = 0; i < 4; ++i) {
		v %= g.u16(kFamilyNames);
		t.u8(0x6E8 + i) = uint8_t(v);
		for (uint32_t c = 0; c < i;) {
			if (t.u8(0x6E4 + c) == uint8_t(v)) {
				if (++v >= g.u16(kMaleNames)) {
					v = 0;
				}
				c = 1;
				continue;
			}
			++c;
		}
		v += i + step;
	}
	// [0x6EC + i]: distinto de los apellidos y de los de [0x6E8] (al repetir,
	// vuelve a mirar desde el 1).
	for (uint32_t i = 0; i < 4; ++i) {
		const uint32_t count = g.u16(kFamilyNames);
		v %= count;
		for (uint32_t c = 0; c < i + 4;) {
			const uint8_t other = c < 4 ? t.u8(0x6D8 + c) : t.u8(0x6E8 + c);
			if (other == uint8_t(v)) {
				if (++v >= count) {
					v = 0;  // Checked with the oracle: scenario mayor_names (saves 3 and 5, direct calls).
				}
				c = 1;
				continue;
			}
			++c;
		}
		t.u8(0x6EC + i) = uint8_t(v);
		v += i + step;
	}
	t.u32(0x6F4) = 0;
	t.u8(0x6E4) = 5;
	t.u8(0x6E5) = 5;
	t.u8(0x6E6) = 5;
	t.u8(0x6E7) = 5;
	t.u32(0x6F8) = 10;
	t.u32(0x700) = flag ? 250000 : 0;
	std::memset(t.bytes.data() + 0x704, 0, 0x2C);
	std::memset(t.bytes.data() + 0x730, 0, 0x2C);
}

// 0x526A50 (sobre la ciudad): un capitán nuevo (0x5097C0) al principio de la
// lista de la ciudad [0x82E]; sin `flag`, 0x4FE190 lo prepara.
void fn_526a50(SaveGame& save, int town, bool flag) {
	const uint16_t c = events_detail::fn_5097c0(save, flag);
	TownMemory& t = town_of(save, town);
	CaptainMemory& captain = save.captains[c];
	captain.u16(0) = t.u16(0x82E);
	if (!flag) {
		events_detail::fn_4fe190(captain);
	}
	t.u16(0x82E) = c;
}

// 0x545BE0 (sobre la ciudad): el astillero según el año (0 para empezar de
// cero): la experiencia [0x810] (de 1350 a 1400, 28 000 por año) y el nivel
// de cada tipo de barco [0x824 + k] que da (0x673818).
void fn_545be0(SaveGame& save, int town, uint32_t year) {
	TownMemory& t = town_of(save, town);
	int32_t experience = 0;
	if (int32_t(year) >= 0x546) {
		experience = int32_t(year) > 0x578 ? 0x155CC0 : (int32_t(year) - 0x546) * 28000;
	}
	t.s32(0x810) = experience;
	t.u32(0x814) = 0;
	t.u32(0x818) = 0;
	t.u16(0x81C) = 0xFFFF;
	t.u16(0x81E) = 0xFFFF;
	for (int k = 3; k >= 0; --k) {
		int8_t level = -1;
		t.u8(0x828 + size_t(k)) = 0;
		while (t.u32(0x810) >= uint32_t(kShipyardLevel[4 * k + level + 1]) * 2800u) {
			if (++level >= 3) {
				break;
			}
		}
		t.u8(0x824 + size_t(k)) = uint8_t(level);
	}
}

// 0x51E8D0 (sobre la ciudad): sin registros y con la plantilla de mapa `map`
// (0x4F9B40: iso/towns/stadtplatz%.2u.tow, que lee 0x4F9770 en town_class1:
// ancho, alto, 0x5C bytes y la rejilla).
void fn_51e8d0(SaveGame& save, int town, uint32_t map) {
	SavedTown& st = save.towns[size_t(town)];
	TownMemory& t = st.memory;
	st.records.clear();
	t.u32(0x75C) = 0;
	t.u16(0x762) = 0xFFFF;
	t.u16(0x764) = 0xFFFF;
	t.u16(0x768) = 0xFFFF;
	t.u16(0x784) = 0xFFFF;
	t.u16(0x78C) = 1;
	t.u16(0x78E) = 1;
	t.u16(0x786) = 1;
	t.u16(0x760) = 0;
	t.u16(0x776) = 0;
	t.u8(0x774) = 0;
	t.u32(0x76C) = 0;
	for (size_t o : {0x770u, 0x771u, 0x772u, 0x773u, 0x789u, 0x788u, 0x78Au}) {
		t.u8(o) = 0;
	}
	t.u16(0x790) = 0;
	t.u16(0x792) = 0;
	const std::vector<uint8_t>* file = map < save.town_templates.size() ? &save.town_templates[map] : nullptr;
	if (!file || file->size() < 0x6C) {
		// Por reescribir (0x4F99B0): sin plantilla. Las 28 del juego existen.
		return;
	}
	const uint8_t* data = file->data();
	t.u32(0x7A4) = 1;
	std::memcpy(&t.u32(0x7A8), data, 4);
	std::memcpy(&t.u32(0x7AC), data + 4, 4);
	std::memcpy(t.bytes.data() + 0x7B0, data + 8, 0x5C);
	const int64_t cells = int64_t(int32_t(t.u32(0x7A8))) * int32_t(t.u32(0x7AC));
	st.grid.clear();
	t.u32(0x80C) = 0;
	if (cells > 0 && size_t(cells) <= file->size() - 0x64) {
		st.grid.assign(data + 0x64, data + 0x64 + cells);
	}
}

// 0x545890 (sobre la ciudad): el astillero (con el año si `flag`) y el mapa
// de la plantilla de la ciudad (mundo [+0x40 + i]).
void fn_545890(SaveGame& save, int town, bool flag) {
	fn_545be0(save, town, flag ? save.globals.u16(kYear) : 0);
	fn_51e8d0(save, town, save.globals.u8(kTownMaps + town_of(save, town).u8(0x2C0)));
}

// 0x545B20 (sobre la ciudad): las cuentas por mercancía a 1 (consumo de las
// empresas y de los habitantes, producción) y los umbrales de precio a 1..4,
// sin barco de la ciudad, 40 [0x2C6], las instalaciones vacías y la
// satisfacción a 50.
void fn_545b20(SaveGame& save, int town) {
	TownMemory& t = town_of(save, town);
	for (size_t w = 0; w < 24; ++w) {
		t.u32(0x64 + 4 * w) = 1;
		t.u32(0xC4 + 4 * w) = 1;
		t.u32(0x310 + 4 * w) = 1;
		for (uint32_t k = 0; k < 4; ++k) {
			t.u32(0x4F0 + 16 * w + 4 * k) = k + 1;
		}
	}
	t.u16(0x996) = 0;
	t.u16(0x994) = 0xFFFF;
	t.u16(0x2C6) = 0x28;
	std::memset(t.bytes.data() + 0x9A4, 0, 0x10);
	for (uint8_t k = 0; k < 21; ++k) {
		FacilityMemory& f = facility_of(t, k);
		f.u8(0x6) = k;
		f.u16(0xA) = 0;
		f.u32(0xC) = 0;
		f.u8(0x7) = t.u8(0x2C0);
	}
	t.u16(0x9A2) = 0xFFFF;
	t.u32(0x300) = 0x00320032;
	t.u32(0x304) = 0x00320032;
}

// 0x5462F0 (sobre la ciudad): un nodo nuevo (0x50FCA0) al principio de su
// lista [0x9B4], de la instalación `k` (4..20) de la ciudad, con [+0xC] y
// [+0xE] (0x50FE00, como mucho 1024) = 1024 / (3 · nº de ciudades / 16) por
// 200 (k = 4), 700 (0xD), 500 (0x10) o 100, en /1024; lo demás a 0.
void fn_5462f0(SaveGame& save, int town, uint32_t k) {
	if (k >= 0x15 || k <= 3) {
		return;  // Its only caller (0x5458D0) passes 4, 0xD and 0x10.
		// Checked with the oracle: scenario new_town_bits (saves 3 and 5, direct calls).
	}
	SavedTown& st = save.towns[size_t(town)];
	const uint32_t parts = (save.globals.u32(kTownCount) * 3) >> 4;
	if (parts == 0) {
		return;  // El juego divide por 0 con menos de 6 ciudades.
	}
	const uint32_t share = 0x400 / parts;
	const uint32_t factor = k == 0x10 ? 500 : k == 0xD ? 700 : k == 4 ? 200 : 100;
	const uint16_t amount = uint16_t((share * factor) >> 10);
	TownNode node;
	node.u8(0x10) = uint8_t(k);
	node.u8(0x11) = st.memory.u8(0x2C0);
	node.u16(0xC) = amount > 0x400 ? 0x400 : amount;
	node.u16(0xE) = node.u16(0xC);
	st.nodes.insert(st.nodes.begin(), node);
}

// 0x5458D0 (sobre la ciudad): lo demás de una ciudad nueva (0x545B20): la
// productividad de las instalaciones (1024; las empresas de las mercancías
// de `wares`, 1024; las de `other`, 768; las demás, 0), los nodos de
// 0x5462F0 por los bits 0x40000, 0x80000 y 0x100000 de `wares` y, sin `flag`, sin
// soldados ni trabajadores, la eficiencia de cada empresa (0x673C2C), el
// almacén vacío, 38 habitantes y las calles (0x4FA6F0).
void fn_5458d0(SaveGame& save, int town, bool flag, uint32_t wares, uint32_t other) {
	// 0x673C2C: la eficiencia de cada empresa (tipos 4..20) por su productividad, en /1024.
	static constexpr uint16_t kEfficiency[17] = {2048, 942, 1638, 2048, 1229, 1331, 1638, 1229, 1536, 1024, 1024,
		1229, 4096, 1024, 1024, 1024, 1024};
	fn_545b20(save, town);
	TownMemory& t = town_of(save, town);
	for (size_t k = 0; k < 4; ++k) {
		t.u16(0x848 + 16 * k) = 0x400;
	}
	for (uint32_t k = 4; k < 21; ++k) {
		const uint32_t bit = 1u << (k - 4);
		t.u16(0x848 + 16 * size_t(k)) = (wares & bit) ? 0x400 : (other & bit) ? 0x300 : 0;
	}
	if (wares & 0x20000) {
		// The founding never gives these bits (it passes other = 0, and
		// 0x532E30's mask has no bit above 0x40000). Each one is checked with the oracle, scenario
		// new_town_bits (saves 3 and 5, direct calls).
		t.u16(0x898) = 0x300;
		t.u32(0x2CC) = 0x400;
	} else if (other & 0x20000) {
		t.u16(0x898) = 0x300;  // Checked with the oracle: new_town_bits.
		t.u32(0x2CC) = 0x300;
	} else {
		t.u32(0x2CC) = 0;
	}
	// Al fundar solo sale el 0x40000: 0x532E30 lo pone cuando el aceite (0xA)
	// es de las mercancías que más faltan.
	if (wares & 0x40000) {
		fn_5462f0(save, town, 4);
	}
	if (wares & 0x80000) {
		fn_5462f0(save, town, 0x10);  // Checked with the oracle: new_town_bits.
	}
	if (wares & 0x100000) {
		fn_5462f0(save, town, 0xD);  // Checked with the oracle: new_town_bits.
	}
	if (wares & 0x200000) {
		t.u32(0x2C8) |= 0x8000000;  // Checked with the oracle: new_town_bits.
	}
	t.u16(0x9A0) = 10;
	if (flag) {
		// Por reescribir: 0x545CE0 y 0x545F50 (una ciudad que ya existe). Al
		// fundar, `flag` es 0.
		return;
	}
	std::memset(t.bytes.data() + 0x998, 0, 4);
	std::memset(t.bytes.data() + 0x99C, 0, 4);
	t.u16(0x844) = 0;
	t.u32(0x850) = 0x400;
	t.u32(0x860) = 0x400;
	t.u32(0x870) = 0x400;
	t.u16(0x854) = 0;
	t.u16(0x864) = 5;
	t.u16(0x874) = 2;
	for (size_t k = 4; k < 21; ++k) {
		FacilityMemory& f = facility_of(t, int(k));
		f.u16(0x4) = 0;
		f.s32(0x0) = int32_t(uint32_t(f.u16(0x8)) * kEfficiency[k - 4]) >> 10;
	}
	if (t.u32(0x2CC) > 0) {
		t.u32(0x890) = t.u16(0x898);  // Checked with the oracle: scenario new_town_bits (saves 3 and 5, direct calls).
	}
	std::memset(t.bytes.data() + 0x4, 0, 0x60);
	t.u32(0x2D4) = 0x26;
	t.u32(0x2D8) = 4;
	t.u32(0x2DC) = 8;
	t.u32(0x2E0) = 0x10;
	t.u32(0x2E4) = 10;
	core_detail::fn_4fa6f0(save, town);
}

// 0x516930: lo que se tarda por una ruta, en dieciseisavos de día a la
// velocidad de una snaikka: por tramo, 16 · el lado mayor + 7 · el menor.
uint32_t fn_516930(const SeaRoute& route) {
	int32_t sum = 0;
	for (size_t i = 1; i < route.size(); ++i) {
		const int32_t dx = std::abs(int32_t(route[i - 1].x) - int32_t(route[i].x));
		const int32_t dy = std::abs(int32_t(route[i - 1].y) - int32_t(route[i].y));
		sum += dx > dy ? dx * 16 + dy * 7 : dx * 7 + dy * 16;
	}
	const int64_t product = int64_t(int32_t(uint32_t(sum) << 5)) * 0x7E07E07F;
	const int32_t high = int32_t(uint64_t(product) >> 32) >> 8;
	const int32_t q = high + int32_t(uint32_t(high) >> 31);
	return uint32_t((q + 0x120) >> 8);
}

}  // namespace new_town_detail

// 0x531BD0 (sobre el mundo): la matriz de tiempos de viaje [+0x6C] pasa a
// n × n con la ciudad nueva (la última): lo de las demás se copia y lo suyo
// sale de la ruta de la matriz entre los puntos de llegada (0x445010: la de
// Sailing) con 0x516930.
void fn_531bd0(SaveGame& save) {
	const uint32_t n = save.globals.u32(kTownCount);
	std::vector<uint8_t> travel(size_t(n) * n, 0);
	const uint32_t old = n - 1;
	for (uint32_t r = 0; r < old; ++r) {
		for (uint32_t c = 0; c < old; ++c) {
			travel[size_t(r) * n + c] =
				size_t(r) * old + c < save.town_travel.size() ? save.town_travel[size_t(r) * old + c] : 0;
		}
	}
	save.town_travel = std::move(travel);
	Sailing* sailing = event_sailing();
	for (uint32_t town = old; town < n; ++town) {
		save.town_travel[size_t(n + 1) * town] = 0;
		for (uint32_t b = 0; b < town; ++b) {
			uint8_t days = 0;
			if (sailing) {
				std::shared_ptr<SeaRoute> route = sailing->town_route(int(town), int(b));
				if (!route->empty()) {
					route->front() = sailing->approach(int(town));
				}
				days = uint8_t(new_town_detail::fn_516930(*route));
			}
			save.town_travel[size_t(n) * town + b] = days;
			save.town_travel[size_t(n) * b + town] = days;
		}
	}
}

// 0x531D50 (sobre el mundo): la ciudad del registro `rec` (u32 [+0x0] su
// situación, u32 [+0x4] y [+0x8] las mercancías de sus empresas, u8 [+0xC]
// su número de StadtDaten.ini, u8 [+0xD] su plantilla de mapa, u8 [+0xE] su
// índice, u8 [+0xF] 0 al fundarla en plena partida). Si el índice es nuevo,
// amplía las ciudades (0x51B100) y sus asedios (0x6334B0; [+0x4] el índice);
// después la prepara y, al fundarla, un evento 0x2F a los 180 días, las rutas
// (0x448040: Sailing::add_town) y la matriz de tiempos de viaje (0x531BD0).
void fn_531d50(SaveGame& save, const GameMemory<0x10>& rec) {
	using namespace new_town_detail;
	GlobalMemory& g = save.globals;
	const uint32_t index = rec.u8(0xE);
	bool grown = false;
	if (index >= g.u32(kTownCount)) {
		const size_t old = save.towns.size();
		save.towns.resize(size_t(index) + 1);
		for (size_t i = old; i < save.towns.size(); ++i) {
			fn_51b100(save, int(i));
		}
		if (!save.sieges.empty()) {
			const size_t sieges = save.sieges.size();
			save.sieges.resize(size_t(index) + 1);
			save.siege_extras.resize(save.sieges.size());
			for (size_t i = sieges; i < save.sieges.size(); ++i) {
				fn_6334b0(save.sieges[i]);
			}
			for (size_t i = old; i <= index; ++i) {
				save.sieges[i].u8(0x4) = uint8_t(i);
			}
		}
		g.u32(kTownCount) = index + 1;
		grown = rec.u8(0xF) == 0;
	}
	const uint8_t number = rec.u8(0xC);
	TownMemory& t = town_of(save, int(index));
	t.u32(0x2C8) = rec.u32(0x0);
	t.u8(0x2C0) = uint8_t(index);
	g.u8(kTownIds + index) = number;
	t.u8(0x2C1) = number;
	// 0x5127E0: los punteros a su nombre en los globales (no se reproducen).
	g.u8(kTownMaps + index) = rec.u8(0xD);
	t.u8(0x2C2) = rec.u8(0xD);
	t.u32(0x2C8) = rec.u32(0x0);
	if (g.u8(kTownTable + 0x34 * uint32_t(number) + 0x2F)) {
		t.u32(0x2C8) |= 1;  // AmFluss. Checked with the oracle: scenario river_town (saves 3 and 5; the
		// numbers on a river are taken there, so the task's number gets the flag).
	}
	fn_5289b0(save, int(index), rec.u8(0xF), rec.u32(0x4) * (uint32_t(number) + 1),
		rec.u32(0x8) * (uint32_t(number) + 1));
	fn_526a50(save, int(index), false);
	fn_526a50(save, int(index), true);
	fn_545890(save, int(index), rec.u8(0xF) != 0);
	fn_5458d0(save, int(index), rec.u8(0xF) != 0, rec.u32(0x4), rec.u32(0x8));
	if ((rec.u32(0x0) & 0x40) == 0x40) {
		// Por reescribir: la elección de su alcalde (0x532063..0x5321C5). Al
		// fundar la ciudad la situación es 0.
	}
	if (rec.u8(0xF) == 0) {
		const uint16_t e = events_detail::fn_4d8cf0(save, g.u32(kTick) + 0xB400);
		GameMemory<0x18>& ev = save.events[e];
		ev.u16(0x6) = 0x2F;
		ev.u32(0x8) = 0;
		ev.u32(0xC) = index;
		ev.u32(0x10) = 0;
		ev.u32(0x14) = g.u32(kTick);
	}
	if (grown) {
		if (Sailing* sailing = event_sailing()) {
			sailing->add_town();
		}
		fn_531bd0(save);
	}
}

// 0x4ECDF4 (evento 0x39 de la cabeza, [+0x8] la ciudad nueva): su muralla.
// Sin la ciudad o con [0x2C8] & 0xE0, se quita. Con piezas de muralla
// pendientes ([0x766]), vuelve un día después por pieza; si no, según
// 0x5252F0: 0xFF (completa) vuelve a los 31 días; 0 o 1, si hay tramos de
// muralla (0x36) en obras, vuelve cuando acaben (4 por día de obra, a
// principio de día); si no, empieza el lado 1 o el 2 que falte (0x522B20) y
// vuelve a los 31 días; 2 o más, se quita.
uint32_t fn_4ecdf4(SaveGame& save) {
	GlobalMemory& g = save.globals;
	GameMemory<0x18>& e = save.events[g.u16(kEventHead)];
	const uint32_t town = e.u8(0x8);
	if (town >= g.u32(kTownCount)) {
		// Each branch: checked with the oracle, scenario new_town_events (saves 3
		// and 5; events 0x39 of existing towns).
		return 0;
	}
	const SavedTown& st = save.towns[town];
	if (st.memory.u8(0x2C8) & 0xE0) {
		return 0;  // Checked with the oracle: new_town_events.
	}
	const uint32_t count = st.memory.u16(0x776);
	if (st.memory.u16(0x766) < count) {
		// Checked with the oracle: new_town_events.
		for (uint32_t r = st.memory.u16(0x766); r < count; r = st.records[r].next) {
			e.u32(0) += 0x100;
		}
		return 1;
	}
	const uint8_t wall = facilities_detail::fn_5252f0(save, int(town));
	if (wall == 0xFF) {
		e.u32(0) += 0x1F00;
		return 1;
	}
	if (wall >= 2) {
		return 0;  // Checked with the oracle: new_town_events.
	}
	// Checked with the oracle: new_town_events.
	uint32_t days = 0;
	for (uint32_t r = st.memory.u16(0x762); r < count; r = st.records[r].next) {
		if (st.records[r].type == 0x36) {
			days += st.records[r].pending;
		}
	}
	if (days > 0) {
		e.u32(0) += days * 4;
		e.u8(0) = 0;
		if (e.u32(0) <= g.u32(kTick)) {
			e.u32(0) += 0x100;
		}
		return 1;
	}
	if (fn_4fcfe0(save, int(town), 1)) {
		events_rare_detail::fn_522b20(save, int(town), 1);
	} else if (fn_4fcfe0(save, int(town), 2)) {
		events_rare_detail::fn_522b20(save, int(town), 2);
	} else {
		return 0;
	}
	save.events[g.u16(kEventHead)].u32(0) += 0x1F00;
	return 1;
}

// 0x4EA0D4 (evento 0x2F de la cabeza, [+0xC] la ciudad nueva, [+0x10] sus
// fases, [+0x14] el tick en que empezó la última): el desarrollo de una
// ciudad fundada en plena partida; 0x531D50 lo crea a los 180 días. Mira los
// bits de [0x76C] de la ciudad que había al empezar. Sin la ciudad, se quita;
// si no, vuelve siempre:
// - Fase 0 (sin el bit 1): sin el bit 1 de [0x76C], a los 21 días. Con
//   alcalde, su oficina si es de la IA (0x5308A0), fase 1 y al dieciseisavo
//   de día. Sin alcalde (y sin [0x6DF31C]), la operación 0x46 que hace
//   alcalde al comerciante de la IA con más rango en su ciudad natal que no
//   lo sea de otra (0x535FD6), y a los 14 días.
// - Fase 1: con el bit 0x80, 4 pobres más y un trabajador en la instalación
//   1 si no tiene. Con el bit 0x100 de [0x76C], fase 2. Si no, según los
//   habitantes: desde 1000, la muralla (0x5252F0): 1 o 2, el bit 0x100 de
//   [0x76C] y 5 ticks; 0, si hay un tramo (tipo ≥ 0x36) de la ciudad en la
//   lista de obras, 14 días, y si no, el bit 0x100 y 28 días; más, 28 días.
//   De 500 a 1000, 0x88DB − 35 por habitante; menos de 500, 28 días.
// - Fase 2 (0x100): con el bit 0x800 de [0x76C], fase 3. Si no, con más de
//   1500 entre tres veces los pobres y los acomodados, la ciudad encarga un
//   prestamista (0x51FAB0, edificio 0x25; sin [0x6DF31C]) y vuelve a la
//   semana; si no, a los 28 días.
// - Fase 3 (0x800): a los 21 días.
uint32_t fn_4ea0d4(SaveGame& save) {
	GlobalMemory& g = save.globals;
	auto event = [&]() -> GameMemory<0x18>& { return save.events[g.u16(kEventHead)]; };
	const uint32_t town = event().u32(0xC);
	if (town >= g.u32(kTownCount)) {
		return 0;  // Checked with the oracle: scenario new_town_events (saves 3 and 5).
	}
	SavedTown& st = save.towns[town];
	TownMemory& t = st.memory;
	const uint32_t bits = t.u32(0x76C);
	const uint32_t merchants = g.u16(kMerchantCount);
	if ((event().u32(0x10) & 1) == 0) {
		if ((bits & 1) == 0) {
			event().u32(0) += 0x1500;
			return 1;
		}
		const uint32_t mayor = t.u8(0x6F1);
		if (mayor < merchants) {
			if (save.merchants[mayor].memory.u16(8) != 0) {
				events_towns_detail::fn_5308a0_create(save, mayor, town & 0xFFFF);
			}
			event().u32(0x10) |= 1;
			event().u32(0x14) = g.u32(kTick);
			event().u32(0) += 0x100;
			return 1;
		}
		if (g.u32(kAldermanBusy) == 0) {
			uint32_t best = 0;
			uint32_t chosen = 0xFF;
			for (uint32_t m = merchants; m > 0;) {
				--m;
				const MerchantMemory& mm = save.merchants[m].memory;
				if (mm.u16(8) == 0) {
					continue;
				}
				uint32_t other = 0;
				while (other < g.u32(kTownCount) && save.towns[other].memory.u8(0x6F1) != m) {
					++other;
				}
				if (other < g.u32(kTownCount)) {
					continue;
				}
				const uint32_t rank = mm.u8(0x39C + mm.u8(0x19));
				if (rank > best) {
					best = rank;
					chosen = m;
				}
			}
			if (chosen < merchants) {
				// 0x535740 y la operación: {0x46, comerciante, ciudad, 0xCDCDCDCD, 0xBADEAFFE}.
				uint8_t op[0x14];
				const uint32_t fields[5] = {0x46, chosen, town, 0xCDCDCDCD, 0xBADEAFFE};
				std::memcpy(op, fields, sizeof(op));
				enqueue_operation(save, op, sizeof(op));
			}
		}
		event().u32(0) += 0xE00;
		return 1;
	}
	const uint32_t phase = event().u32(0x10);
	if ((phase & 0x80) && t.u16(0x854) == 0) {
		t.u16(0x854) = 1;
		t.u32(0x2E0) += 4;
		t.u32(0x2D4) += 4;
	}
	if ((phase & 0x100) == 0) {
		if (bits & 0x100) {
			event().u32(0x10) = phase | 0x100;
			event().u32(0) += 0x100;
			return 1;
		}
		const int32_t population = t.s32(0x2D4);
		if (population < 500) {
			event().u32(0) += 0x1C00;
			return 1;
		}
		if (population < 1000) {
			event().u32(0) += uint32_t(0x88DB - population * 35);
			return 1;
		}
		const uint8_t wall = facilities_detail::fn_5252f0(save, int(town));
		if (wall == 1 || wall == 2) {
			t.u32(0x76C) |= 0x100;
			event().u32(0) += 5;
			return 1;
		}
		if (wall != 0) {
			event().u32(0) += 0x1C00;
			return 1;
		}
		const uint32_t count = t.u16(0x776);
		uint32_t r = t.u16(0x762);
		while (r < count && (st.records[r].type < 0x36 || st.records[r].owner != 0xFF)) {
			r = st.records[r].next;
		}
		if (r < count) {
			event().u32(0) += 0xE00;
			return 1;
		}
		t.u32(0x76C) |= 0x100;
		event().u32(0) += 0x1C00;
		return 1;
	}
	if (phase & 0x800) {
		event().u32(0) += 0x1500;
		return 1;
	}
	if (bits & 0x800) {
		event().u32(0x10) = phase | 0x800;
		event().u32(0x14) = g.u32(kTick);
		event().u32(0) += 0x100;
		return 1;
	}
	if (t.u32(0x2E0) * 3 + t.u32(0x2DC) > 0x5DC) {
		if (g.u32(kAldermanBusy) == 0) {
			construction_detail::fn_51fab0(save, int(town), 0x25, false);
		}
		event().u32(0) += 0x700;
		return 1;
	}
	event().u32(0) += 0x1C00;
	return 1;
}

// La operación 0x46 (0x535FD6, en el ejecutor 0x535760): {0x46, comerciante,
// ciudad, -, marca}. Si la ciudad no tiene alcalde, el comerciante pasa a
// serlo si tiene rango 5 o más en su ciudad natal o si la operación lleva la
// marca 0xBADEAFFE (la del evento 0x2F siempre la lleva).
void fn_535fd6(SaveGame& save, const uint8_t* operation) {
	const GlobalMemory& g = save.globals;
	uint32_t fields[5];
	std::memcpy(fields, operation, sizeof(fields));
	const uint32_t merchants = g.u16(kMerchantCount);
	if (fields[1] >= merchants || fields[2] >= g.u32(kTownCount)) {
		// Each check: checked with the oracle, scenario mayor_ops (saves 3 and 5).
		return;
	}
	TownMemory& t = town_of(save, int(fields[2]));
	if (t.u8(0x6F1) < merchants) {
		return;  // Checked with the oracle: mayor_ops.
	}
	const MerchantMemory& m = save.merchants[fields[1]].memory;
	if (m.u8(0x39C + m.u8(0x19)) < 5 && fields[4] != 0xBADEAFFE) {
		return;  // Checked with the oracle: mayor_ops.
	}
	t.u8(0x6F1) = uint8_t(fields[1]);
}

}  // namespace p3::data::tick
