// Sondas de src/p3data/town_tick/facilities.cpp.

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

namespace p3::data::tick::facilities_detail {
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
}  // namespace p3::data::tick::facilities_detail

namespace p3::probe {

using namespace p3::data;
namespace fd = tick::facilities_detail;

namespace {

// Registro del mundo en `address` (o -1).
int world_record(const ProbeCall& c, uint32_t address) {
	const Location l = c.locate(address);
	return l.kind == Location::WorldRecord && l.offset == 0 ? l.index : -1;
}

}  // namespace

void register_probes_facilities(ProbeRegistry& r) {
	// Instalaciones de la ciudad: ecx = la instalación ([0x840 + 16·k]).
	r[0x510C50] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const auto [town, k] = c.facility(c.ecx);
		tick::fn_510c50(s, town, k);
		return std::nullopt;
	};
	r[0x50E500] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const auto [town, k] = c.facility(c.ecx);
		tick::fn_50e500(s, town, k);
		return std::nullopt;
	};
	r[0x50E570] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const auto [town, k] = c.facility(c.ecx);
		tick::fn_50e570(s, town, k);
		return std::nullopt;
	};
	r[0x50E610] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const auto [town, k] = c.facility(c.ecx);
		return tick::fn_50e610(s, town, k);
	};
	r[0x510AA0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const auto [town, k] = c.facility(c.ecx);
		return tick::fn_510aa0(s, town, k);
	};
	r[0x50F6E0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const auto [town, k] = c.facility(c.ecx);
		tick::fn_50f6e0(s, town, k, c.args[1]);
		return std::nullopt;
	};
	// 0x50E380: instalación de una ciudad o empresa de una oficina (registro del mundo).
	r[0x50E380] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const bool all = (c.args[1] & 0xFF) != 0;
		const int record = world_record(c, c.ecx);
		if (record >= 0) {
			tick::fn_50e380(s, tick::facility_of(s.world_records[size_t(record)]), c.args[0], all);
		} else {
			const auto [town, k] = c.facility(c.ecx);
			tick::fn_50e380(s, tick::facility_of(s.towns[size_t(town)].memory, k), c.args[0], all);
		}
		return std::nullopt;
	};

	// Funciones internas sobre la ciudad (ecx).
	r[0x51D410] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fd::fn_51d410(s, c.town(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x51D4C0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fd::fn_51d4c0(s, c.town(c.ecx), c.args[0]);
		return std::nullopt;
	};
	// Devuelve al: los bits altos de eax no están definidos, así que no se
	// compara (no cambia nada; se comprueba con 0x51D4C0).
	r[0x5252F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fd::fn_5252f0(s, c.town(c.ecx));
		return std::nullopt;
	};
	r[0x51FF30] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fd::fn_51ff30(s, c.town(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x52BB00] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return fd::fn_52bb00(s, c.town(c.ecx));
	};

	// Astillero: ecx = el objeto de los barcos (0x6DD7A0); la ciudad es un índice.
	r[0x5083B0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fd::fn_5083b0(s, int(c.args[0] & 0xFFFF), int32_t(c.args[1]));
		return std::nullopt;
	};
	r[0x508AA0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fd::fn_508aa0(s, int(c.args[0]), int32_t(c.args[1]));
		return std::nullopt;
	};

	// Oficinas y registros del mundo.
	r[0x4FF2D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_4ff2d0(s, c.office(c.ecx));
		return std::nullopt;
	};
	r[0x4D36E0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fd::fn_4d36e0(s, uint32_t(world_record(c, c.ecx)), int32_t(c.args[0]));
		return std::nullopt;
	};
	r[0x4D3490] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fd::fn_4d3490(s, uint32_t(world_record(c, c.ecx)), c.args[0], c.args[1], int32_t(c.args[2]));
		return std::nullopt;
	};
	// Devuelve el índice en ax (los bits altos de eax no están definidos).
	r[0x5306B0] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		fd::fn_5306b0(s);
		return std::nullopt;
	};
	r[0x530780] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fd::fn_530780(s, c.args[0] & 0xFFFF);
		return std::nullopt;
	};
	// 0x5308A0 con create = 0 devuelve un puntero: no cambia nada y no se
	// puede comparar eax; se comprueba con 0x51D4C0 y 0x51FF30.
	r[0x5308A0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		if (c.args[2] == 0) {
			fd::fn_5308a0(s, c.args[0], c.args[1]);
		}
		return std::nullopt;
	};
}

}  // namespace p3::probe
