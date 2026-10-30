// Sondas de las funciones comunes (src/p3data/town_tick/shared.cpp).

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

namespace p3::probe {

using namespace p3::data;

void register_probes_shared(ProbeRegistry& r) {
	r[0x529F90] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_529f90(s, c.town(c.ecx), int32_t(c.args[0]), c.args[1]);
		return std::nullopt;
	};
	r[0x528070] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_528070(s, c.town(c.ecx), c.args[0] != 0);
		return std::nullopt;
	};
	r[0x4FCFE0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const Location l = c.locate(c.ecx);  // town_class1, en [0x7A4] de la ciudad
		return tick::fn_4fcfe0(s, l.index, c.args[0]) ? 1 : 0;
	};
}

}  // namespace p3::probe
