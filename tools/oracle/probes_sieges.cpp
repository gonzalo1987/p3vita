// Sondas de src/p3data/town_tick/sieges.cpp.

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

namespace p3::data::tick::sieges_detail {
bool fn_629c30(const SaveGame& save, int siege);
uint8_t fn_5252f0(const SaveGame& save, int town);
void fn_632780(SaveGame& save, int siege);
void fn_62b2b0(SaveGame& save, int siege);
void fn_62e810(SaveGame& save, int siege);
uint32_t fn_6326e0(const SaveGame& save, int siege);
void fn_51d5f0(SaveGame& save, int town, uint32_t k);
void fn_51d680(SaveGame& save, int town, uint32_t k);
void fn_634c50(SaveGame& save, int siege, bool paid);
void fn_6281f0(SaveGame& save, int siege);
void fn_532290(const SaveGame& save, uint32_t (&stock)[24], uint32_t (&need)[24]);
void fn_635220(SaveGame& save, int siege);
void fn_635440(SaveGame& save, int siege);
void fn_636230(SaveGame& save, int siege);
void fn_635740(SaveGame& save, int siege);
}  // namespace p3::data::tick::sieges_detail

namespace p3::probe {

using namespace p3::data;
using namespace p3::data::tick::sieges_detail;

namespace {

// El asedio cuyo objeto empieza en `address` (o -1).
int siege_at(const ProbeCall& c, uint32_t address) {
	const Location l = c.locate(address);
	return l.kind == Location::Siege && l.offset == 0 ? l.index : -1;
}

// El asedio cuyo ejército está en `address` (su [+0x8]), o -1.
int army_at(const SaveGame& s, uint32_t address) {
	for (size_t i = 0; i < s.sieges.size(); ++i) {
		if (address != 0 && s.sieges[i].u32(0x8) == address && i < s.siege_extras.size() && s.siege_extras[i].army) {
			return int(i);
		}
	}
	return -1;
}

}  // namespace

void register_probes_sieges(ProbeRegistry& r) {
	r[0x6349F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_6349f0(s, siege_at(c, c.ecx));
		return std::nullopt;
	};
	r[0x633A90] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_633a90(s, siege_at(c, c.ecx));
		return std::nullopt;
	};
	r[0x6281F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_6281f0(s, army_at(s, c.ecx));
		return std::nullopt;
	};
	r[0x629C30] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return fn_629c30(s, army_at(s, c.ecx)) ? 1 : 0;
	};
	r[0x6326E0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return fn_6326e0(s, army_at(s, c.ecx));
	};
	r[0x632780] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_632780(s, army_at(s, c.ecx));
		return std::nullopt;
	};
	r[0x62B2B0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_62b2b0(s, army_at(s, c.ecx));
		return std::nullopt;
	};
	r[0x62E810] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_62e810(s, army_at(s, c.ecx));
		return std::nullopt;
	};
	r[0x5252F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;
		return fn_5252f0(s, c.town(c.ecx));
	};
	r[0x51D5F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_51d5f0(s, c.town(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x51D680] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_51d680(s, c.town(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x634C50] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_634c50(s, siege_at(c, c.ecx), (c.args[0] & 0xFF) != 0);
		return std::nullopt;
	};
	// Solo desde el ejército (convoy 0xFFFF); lo que devuelve en sus dos
	// punteros va a la pila del llamador: se compara al.
	r[0x633DB0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;
		uint32_t amount = 0;
		uint16_t offices = 0;
		return fn_633db0(s, siege_at(c, c.ecx), amount, offices, uint16_t(c.args[2])) ? 1 : 0;
	};
	// Sus dos tablas van a la pila del llamador (se comprueban con 0x635220).
	r[0x532290] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		uint32_t stock[24];
		uint32_t need[24];
		fn_532290(s, stock, need);
		return std::nullopt;
	};
	r[0x635220] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_635220(s, siege_at(c, c.ecx));
		return std::nullopt;
	};
	r[0x635440] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_635440(s, siege_at(c, c.ecx));
		return std::nullopt;
	};
	r[0x636230] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_636230(s, siege_at(c, c.ecx));
		return std::nullopt;
	};
	r[0x635740] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_635740(s, siege_at(c, c.ecx));
		return std::nullopt;
	};
}

}  // namespace p3::probe
