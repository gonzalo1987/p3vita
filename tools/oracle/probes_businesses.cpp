// Sondas de src/p3data/town_tick/businesses.cpp.
//
// Las empresas (0x50E690...) se llaman también desde el tick de las oficinas
// (0x4D3780): entonces el almacén es una oficina y la instalación, un
// registro del mundo. Las sondas aceptan los dos casos.

#include <cstdio>

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

// Las funciones internas de businesses.cpp.
namespace p3::data::tick::businesses_detail {

uint32_t fn_510120(const SaveGame& save, const FacilityMemory& fc);
void fn_5109b0(SaveGame& save, FacilityMemory& fc, uint32_t count);
void fn_5108e0(SaveGame& save, FacilityMemory& fc, uint32_t& count);

}  // namespace p3::data::tick::businesses_detail

namespace p3::probe {

using namespace p3::data;
using namespace p3::data::tick;

namespace {

// La instalación de ecx: una de las de una ciudad o un registro del mundo.
FacilityMemory* facility(SaveGame& s, const ProbeCall& c) {
	const auto [town, k] = c.facility(c.ecx);
	if (town >= 0) {
		return &facility_of(s.towns[size_t(town)].memory, k);
	}
	const Location l = c.locate(c.ecx);
	if (l.kind == Location::WorldRecord && l.offset == 0) {
		return &facility_of(s.world_records[size_t(l.index)]);
	}
	std::fprintf(stderr, "0x%x: ecx %08x no es una instalación\n", c.function, c.ecx);
	return nullptr;
}

// El almacén del primer argumento: una ciudad o una oficina.
StorageMemory* storage(SaveGame& s, const ProbeCall& c) {
	if (const int town = c.town(c.args[0]); town >= 0) {
		return &storage_of(s.towns[size_t(town)].memory);
	}
	if (const int office = c.office(c.args[0]); office >= 0) {
		return &storage_of(s.offices[size_t(office)].memory);
	}
	std::fprintf(stderr, "0x%x: %08x no es una ciudad ni una oficina\n", c.function, c.args[0]);
	return nullptr;
}

// Empresa con un argumento (el máximo de trabajadores).
template <void (*F)(SaveGame&, StorageMemory&, FacilityMemory&, uint32_t)>
std::optional<uint32_t> business(SaveGame& s, const ProbeCall& c) {
	FacilityMemory* f = facility(s, c);
	StorageMemory* st = storage(s, c);
	if (f && st) {
		F(s, *st, *f, c.args[1]);
	}
	return std::nullopt;
}

// Empresa con los trabajadores de reserva y el máximo (dos u16).
template <void (*F)(SaveGame&, StorageMemory&, FacilityMemory&, uint16_t, uint16_t)>
std::optional<uint32_t> business_reserve(SaveGame& s, const ProbeCall& c) {
	FacilityMemory* f = facility(s, c);
	StorageMemory* st = storage(s, c);
	if (f && st) {
		F(s, *st, *f, uint16_t(c.args[1]), uint16_t(c.args[2]));
	}
	return std::nullopt;
}

// Empresa con el máximo y un byte.
template <class Max, void (*F)(SaveGame&, StorageMemory&, FacilityMemory&, Max, bool)>
std::optional<uint32_t> business_flag(SaveGame& s, const ProbeCall& c) {
	FacilityMemory* f = facility(s, c);
	StorageMemory* st = storage(s, c);
	if (f && st) {
		F(s, *st, *f, Max(c.args[1]), (c.args[2] & 0xFF) != 0);
	}
	return std::nullopt;
}

}  // namespace

void register_probes_businesses(ProbeRegistry& r) {
	r[0x5101D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const auto [town, k] = c.facility(c.ecx);
		if (town < 0 || c.town(c.args[0]) != town) {
			std::fprintf(stderr, "0x5101d0: ecx %08x no es una instalación de la ciudad %08x\n", c.ecx, c.args[0]);
			return std::nullopt;
		}
		if (s.towns[size_t(town)].memory.u8(0x840 + 16 * size_t(k) + 6) != k) {
			std::fprintf(stderr, "0x5101d0: la instalación %d de la ciudad %d es de otro tipo\n", k, town);
		}
		fn_5101d0(s, town, k);
		return std::nullopt;
	};
	r[0x510120] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		FacilityMemory* f = facility(s, c);
		return f ? businesses_detail::fn_510120(s, *f) : 0;
	};
	r[0x5109B0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		if (FacilityMemory* f = facility(s, c)) {
			businesses_detail::fn_5109b0(s, *f, c.args[0]);
		}
		return std::nullopt;
	};
	// El argumento es un puntero a una variable de 0x5101D0 en la pila, que el
	// oráculo no guarda; como el tick de ciudad siempre se llama con la misma
	// pila, es una de las dos variables locales que quedan en la pila tras los
	// registros guardados: args[3] (necesidad) o args[4] (sitio). El valor que
	// deja en ella no se compara (se comprueba con 0x5101D0).
	r[0x5108E0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		std::optional<uint32_t> count = c.stack(c.args[0]);
		if (!count) {
			std::fprintf(stderr, "0x5108e0: puntero %08x fuera de la pila grabada\n", c.args[0]);
			return std::nullopt;
		}
		if (FacilityMemory* f = facility(s, c)) {
			businesses_detail::fn_5108e0(s, *f, *count);
		}
		return std::nullopt;
	};
	r[0x50E380] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		if (FacilityMemory* f = facility(s, c)) {
			fn_50e380(s, *f, c.args[0], (c.args[1] & 0xFF) != 0);
		}
		return std::nullopt;
	};
	r[0x4D4A40] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_4d4a40(s, uint16_t(c.args[0]), uint16_t(c.args[1]));
		return std::nullopt;
	};

	r[0x50ED70] = business<fn_50ed70>;
	r[0x50E690] = business_flag<uint16_t, fn_50e690>;
	r[0x50E8B0] = business_reserve<fn_50e8b0>;
	r[0x50FBC0] = business<fn_50fbc0>;
	r[0x50EA00] = business<fn_50ea00>;
	r[0x50EAD0] = business<fn_50ead0>;
	r[0x50F460] = business_flag<uint32_t, fn_50f460>;
	r[0x50ECD0] = business<fn_50ecd0>;
	r[0x50EEB0] = business<fn_50eeb0>;
	r[0x50EF50] = business_reserve<fn_50ef50>;
	r[0x50F650] = business<fn_50f650>;
	r[0x50F060] = business<fn_50f060>;
	r[0x50F100] = business<fn_50f100>;
	r[0x50F220] = business_reserve<fn_50f220>;
	r[0x50F2E0] = business<fn_50f2e0>;
	r[0x50F390] = business_reserve<fn_50f390>;
	r[0x50EBF0] = business<fn_50ebf0>;
}

}  // namespace p3::probe
