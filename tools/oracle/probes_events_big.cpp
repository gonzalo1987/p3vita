// Sondas de src/p3data/town_tick/events_big.cpp.

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

// Lo que events_big.cpp expone para las sondas.
namespace p3::data::tick::events_big_detail {

void fn_4dea20(SaveGame& save, uint16_t event);
void fn_4deba0(SaveGame& save);
int32_t fn_50ffb0(const TownNode& node);
void fn_4f8f10(SaveGame& save, int merchant, uint32_t town, uint32_t kind);
uint32_t fn_4e5b84(SaveGame& save, uint16_t event);
void fn_4e8234(SaveGame& save, uint16_t event);

}  // namespace p3::data::tick::events_big_detail

namespace p3::probe {

using namespace p3::data;
namespace eb = tick::events_big_detail;

namespace {

constexpr uint32_t kEvents = 0x6DD73C;
constexpr uint32_t kEventHead = kEvents + 0x8;

// Índice del evento al que apunta `address` (en el array *0x6DD73C del juego).
uint16_t event_index(const SaveGame& s, uint32_t address) {
	return uint16_t((address - s.globals.u32(kEvents)) / 0x18);
}

}  // namespace

void register_probes_events_big(ProbeRegistry& r) {
	// Evento 0x2C: thiscall sobre la cola, sobre el evento de la cabeza.
	r[0x4DEA20] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		eb::fn_4dea20(s, s.globals.u16(kEventHead));
		return std::nullopt;
	};
	r[0x4DEBA0] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		eb::fn_4deba0(s);
		return std::nullopt;
	};
	// thiscall sobre un nodo de la lista [0x9B4] de una ciudad.
	r[0x50FFB0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const Location l = c.locate(c.ecx);
		if (l.kind != Location::Node || l.offset != 0) {
			return std::nullopt;
		}
		return uint32_t(eb::fn_50ffb0(s.towns[size_t(l.index)].nodes[size_t(l.item)]));
	};
	// Evento 0x05.
	r[0x4E5B84] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return eb::fn_4e5b84(s, event_index(s, c.args[0]));
	};
	r[0x4F8F10] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		eb::fn_4f8f10(s, c.merchant(c.ecx), c.args[0], c.args[1]);
		return std::nullopt;
	};
	// Evento 0x16.
	r[0x4E8234] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		eb::fn_4e8234(s, event_index(s, c.args[0]));
		return std::nullopt;
	};
}

}  // namespace p3::probe
