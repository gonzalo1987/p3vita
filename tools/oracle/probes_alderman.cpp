// Sondas de src/p3data/town_tick/alderman.cpp (la candidatura a Gobernador)
// y del ejecutor de las operaciones encoladas (0x535760, simulation.cpp).

#include "p3data/simulation.hpp"
#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

// Lo que alderman.cpp expone para las sondas.
namespace p3::data::tick::alderman_detail {

uint8_t fn_515a60(const SaveGame& save, uint32_t from);

}  // namespace p3::data::tick::alderman_detail

namespace p3::probe {

using namespace p3::data;

void register_probes_alderman(ProbeRegistry& r) {
	r[0x5326E0] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		// Sobre el mundo, sin argumentos.
		tick::fn_5326e0(s);
		return std::nullopt;
	};
	r[0x515A60] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;  // Devuelve un u8 en al.
		return tick::alderman_detail::fn_515a60(s, c.args[0]);
	};
	r[0x535760] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall sobre la operación, que está en el montón del emulador: el
		// oráculo la llama por un trampolín que deja en la pila sus 5 u32 y
		// la marca «P3OP» (town_oracle.py, EXECUTOR_MAGIC). Las llamadas del
		// propio juego (el evento 0x03, los barcos) no la llevan: las repite
		// la sonda de la función que las contiene.
		if (c.args[5] != 0x504F3350) {
			c.skip = true;
			return std::nullopt;
		}
		uint8_t op[0x14];
		for (size_t k = 0; k < 5; ++k) {
			for (size_t b = 0; b < 4; ++b) {
				op[4 * k + b] = uint8_t(c.args[k] >> (8 * b));
			}
		}
		run_operation(s, probe_sailing(), op);
		return std::nullopt;
	};
}

}  // namespace p3::probe
