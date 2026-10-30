// Sondas de src/p3data/convoys_port.cpp (funciones de Sailing: necesitan
// tick_probe --cpr, que crea la navegación de probe_sailing()).
//
// 0x535740 y 0x534F10 solo escriben en la pila del llamador (la operación que
// 0x503B50 va a encolar), que no se graba: sus sondas solo comprueban que no
// tocan nada más. Lo que escriben se compara en la sonda de 0x503B50, en los
// bytes de las operaciones 0x1E y 0x1F encoladas. 0x535740 la llama también
// 0x516CC0 en cada tick de cada barco que navega: no conviene grabarla.
//
// Las ramas que no salen en las 12 partidas se comprueban con las mismas
// sondas sobre muestras sintéticas (agents/port/synth_port.py: llamadas al
// código del juego sobre convoyes cambiados a propósito).

#include <cstdint>

#include "probe_registry.hpp"

namespace p3::data::tick::convoys_port_detail {
void fn_535740(uint8_t* operation);
void fn_534f10(const SaveGame& save, int merchant, uint16_t& x, uint16_t& y, uint32_t near, uint32_t seed);
}  // namespace p3::data::tick::convoys_port_detail

namespace p3::probe {

using namespace p3::data;
using namespace p3::data::tick::convoys_port_detail;

void register_probes_convoys_port(ProbeRegistry& r) {
	r[0x501EB0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;  // devuelve al
		return probe_sailing()->fn_501eb0(c.convoy(c.ecx)) ? 1 : 0;
	};
	r[0x503B50] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		probe_sailing()->fn_503b50(c.convoy(c.ecx));
		return std::nullopt;
	};
	r[0x503CF0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		probe_sailing()->fn_503cf0(c.convoy(c.ecx));
		return std::nullopt;
	};
	r[0x503930] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		probe_sailing()->fn_503930(c.convoy(c.ecx));
		return std::nullopt;
	};
	r[0x535740] = [](SaveGame&, const ProbeCall&) -> std::optional<uint32_t> {
		uint8_t operation[0x14];
		fn_535740(operation);
		return std::nullopt;
	};
	r[0x534F10] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// ecx: el [+0x4D8] del comerciante.
		const Location l = c.locate(c.ecx);
		if (l.kind != Location::Merchant || l.offset != 0x4D8) {
			return 0xDEAD;
		}
		uint16_t x = 0, y = 0;
		fn_534f10(s, l.index, x, y, c.args[2], c.args[3]);
		return std::nullopt;
	};
}

}  // namespace p3::probe
