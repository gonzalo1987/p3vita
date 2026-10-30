// Sondas de src/p3data/convoys_sea.cpp (funciones de Sailing: necesitan
// tick_probe --cpr, que crea la navegación de probe_sailing()).

#include "probe_registry.hpp"

namespace p3::data::convoys_sea_detail {
int fn_517260(const Sailing& sailing, uint16_t x, uint16_t y);
}  // namespace p3::data::convoys_sea_detail

namespace p3::probe {

using namespace p3::data;

namespace {

constexpr uint32_t kBad = 0xDEAD;  // La muestra no se puede repetir (sin --cpr o sin el objeto).

}  // namespace

void register_probes_convoys_sea(ProbeRegistry& r) {
	// thiscall sobre el convoy, con el flag de cada 32 ticks; devuelve al.
	r[0x502110] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int convoy = c.convoy(c.ecx);
		if (!probe_sailing() || convoy < 0) {
			return kBad;
		}
		c.eax_mask = 0xFF;
		return probe_sailing()->fn_502110(convoy, c.args[0]) ? 1 : 0;
	};
	// thiscall sobre el objeto de los barcos: (llegada, velocidad, convoy, rumbo).
	r[0x50A240] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing()) {
			return kBad;
		}
		probe_sailing()->fn_50a240(c.args[0], c.args[1], c.args[2], uint8_t(c.args[3]));
		return std::nullopt;
	};
	// thiscall sobre el convoy, con el barco que lo guía.
	r[0x501E10] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int convoy = c.convoy(c.ecx);
		const int ship = c.ship(c.args[0]);
		if (!probe_sailing() || convoy < 0 || ship < 0) {
			return kBad;
		}
		probe_sailing()->fn_501e10(convoy, ship);
		return std::nullopt;
	};
	// thiscall sobre el convoy.
	r[0x5029F0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		const int convoy = c.convoy(c.ecx);
		if (!probe_sailing() || convoy < 0) {
			return kBad;
		}
		probe_sailing()->fn_5029f0(convoy);
		return std::nullopt;
	};
	// thiscall sobre el objeto de los barcos, con el índice del barco.
	r[0x505C30] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing()) {
			return kBad;
		}
		probe_sailing()->fn_505c30(uint16_t(c.args[0]));
		return std::nullopt;
	};
	// cdecl (x, y): la ciudad en el punto, o −1.
	r[0x517260] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!probe_sailing()) {
			return kBad;
		}
		return uint32_t(convoys_sea_detail::fn_517260(*probe_sailing(), uint16_t(c.args[0]), uint16_t(c.args[1])));
	};
}

}  // namespace p3::probe
