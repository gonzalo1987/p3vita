// Sondas de src/p3data/town_tick/daily.cpp.

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

// Lo que daily.cpp expone para las sondas.
namespace p3::data::tick::daily_detail {

void fn_4f9220(SaveGame& save, int merchant, uint32_t town, uint32_t amount);
void fn_52a020(SaveGame& save, int town);
void fn_500820(SaveGame& save, int office);
void fn_5006f0(SaveGame& save, int office);

}  // namespace p3::data::tick::daily_detail

namespace p3::probe {

using namespace p3::data;
namespace dd = tick::daily_detail;

void register_probes_daily(ProbeRegistry& r) {
	r[0x5315C0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall sobre el mundo; el flag semanal es el byte bajo del argumento.
		tick::fn_5315c0(s, (c.args[0] & 0xFF) != 0);
		return std::nullopt;
	};
	r[0x535450] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall sobre el objeto 0x6DE588 ([+0xE8] del mundo), con el tick.
		tick::fn_535450(s, c.args[0]);
		return std::nullopt;
	};
	r[0x52D7D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall sobre uno de los 15 objetos de [+0xB10] del mundo (0x6DEFB0).
		const uint32_t first = global::kWorld + 0xB10;
		if (c.ecx < first || (c.ecx - first) % 0x2C || (c.ecx - first) / 0x2C >= 15) {
			return 0xDEAD;
		}
		tick::fn_52d7d0(s, int((c.ecx - first) / 0x2C));
		return std::nullopt;
	};
	r[0x52A020] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		dd::fn_52a020(s, c.town(c.ecx));
		return std::nullopt;
	};
	r[0x4F9220] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		dd::fn_4f9220(s, c.merchant(c.ecx), c.args[0], c.args[1]);
		return std::nullopt;
	};
	r[0x500820] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		dd::fn_500820(s, c.office(c.ecx));
		return std::nullopt;
	};
	r[0x5006F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		dd::fn_5006f0(s, c.office(c.ecx));
		return std::nullopt;
	};
}

}  // namespace p3::probe
