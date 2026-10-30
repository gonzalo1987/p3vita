// Sondas de src/p3data/town_tick/ship_trade.cpp.

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

// Lo que ship_trade.cpp expone para las sondas (y para 0x518FF0 de ship_ai.cpp).
namespace p3::data::tick::ship_trade_detail {

uint8_t fn_531b70(const SaveGame& save, uint32_t a, uint32_t b);
int32_t fn_516ab0(const SaveGame& save, int ship, uint32_t town);
int32_t fn_52f030(const SaveGame& save, uint32_t from, uint32_t to, uint32_t ware, int32_t days, float& factor);
uint16_t fn_5269a0(const SaveGame& save, int town, uint16_t owner);
bool fn_526aa0(SaveGame& save, int town, uint32_t captain);

}  // namespace p3::data::tick::ship_trade_detail

namespace p3::probe {

using namespace p3::data;
namespace st = tick::ship_trade_detail;

void register_probes_ship_trade(ProbeRegistry& r) {
	r[0x519280] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_519280(s, c.ship(c.ecx));
		return std::nullopt;
	};
	r[0x5193F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_5193f0(s, c.ship(c.ecx));
		return std::nullopt;
	};
	r[0x5196B0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_5196b0(s, c.ship(c.ecx));
		return std::nullopt;
	};
	r[0x519920] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_519920(s, c.ship(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x51A070] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const Location l = c.locate(c.args[0]);  // [0x124] de una ciudad
		tick::fn_51a070(s, c.ship(c.ecx), l.kind == Location::Town && l.offset == 0x124 ? l.index : -1);
		return std::nullopt;
	};
	r[0x516AB0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(st::fn_516ab0(s, c.ship(c.ecx), c.args[0]));
	};
	r[0x52F030] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// El float de salida va a la pila del llamador y no se graba: aquí solo
		// se compara eax (el float se comprobó aparte, grabándolo en lugar de eax).
		float factor = 0.0f;
		return uint32_t(st::fn_52f030(s, c.args[0], c.args[1], c.args[2], int32_t(c.args[3]), factor));
	};
	r[0x531B70] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;  // Devuelve un u8 en al.
		return st::fn_531b70(s, c.args[0], c.args[1]);
	};
	r[0x5269A0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFFFF;  // Devuelve un u16 en ax.
		return st::fn_5269a0(s, c.town(c.ecx), uint16_t(c.args[0]));
	};
	r[0x526AA0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;  // Devuelve un bool en al.
		return st::fn_526aa0(s, c.town(c.ecx), c.args[0]) ? 1 : 0;
	};
}

}  // namespace p3::probe
