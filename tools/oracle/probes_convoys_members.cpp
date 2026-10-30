// Sondas de src/p3data/convoys_members.cpp (funciones de Sailing: necesitan
// tick_probe --cpr, que crea la navegación de probe_sailing()).

#include "probe_registry.hpp"

namespace p3::data::tick::convoys_members_detail {
void fn_4f9050(SaveGame& save, int merchant, uint32_t ships, uint32_t arrived);
}  // namespace p3::data::tick::convoys_members_detail

namespace p3::probe {

using namespace p3::data;

void register_probes_convoys_members(ProbeRegistry& r) {
	r[0x5017F0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;  // Devuelve al.
		return probe_sailing()->fn_5017f0(c.convoy(c.ecx)) ? 1 : 0;
	};
	r[0x509F80] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		probe_sailing()->fn_509f80(uint16_t(c.args[0]));
		return std::nullopt;
	};
	r[0x50A580] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		probe_sailing()->fn_50a580(uint16_t(c.args[0]));
		return std::nullopt;
	};
	r[0x50A3D0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		probe_sailing()->fn_50a3d0(uint16_t(c.args[0]));
		return std::nullopt;
	};
	r[0x4F9050] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::convoys_members_detail::fn_4f9050(s, c.merchant(c.ecx), c.args[0], c.args[1]);
		return std::nullopt;
	};
}

}  // namespace p3::probe
