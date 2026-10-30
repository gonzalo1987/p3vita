// Sondas de src/p3data/town_tick/events.cpp.

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

// Lo que events.cpp expone para las sondas.
namespace p3::data::tick::events_detail {

void fn_4d8dd0(SaveGame& save);
uint16_t fn_4d8cf0(SaveGame& save, uint32_t tick);
void fn_4f6c10(SaveGame& save, int merchant);
int16_t fn_4f73d0(const SaveGame& save, int merchant);
void fn_4f42b0(SaveGame& save, int merchant);
void fn_4f4f80(SaveGame& save, int merchant);
void fn_4f5080(SaveGame& save, int merchant);
void fn_4f5130(SaveGame& save, int merchant);
int32_t fn_4daf10(const SaveGame& save, uint32_t town, uint32_t record, bool flag);
void fn_4dafe0(const SaveGame& save, uint32_t town, uint32_t record, uint32_t& tick, int32_t& price);
bool fn_4db220(const SaveGame& save, uint16_t owner);
bool fn_4f5a20(SaveGame& save, int merchant, uint32_t me);
void fn_4f4e50(SaveGame& save, int merchant, uint32_t me);
int16_t fn_4f9580(const SaveGame& save, int merchant, uint32_t town);
int32_t fn_51b7e0(const SaveGame& save, int town, int merchant);
void fn_4f4a20(SaveGame& save, int merchant, uint32_t me);
void fn_4f4b40(SaveGame& save, int merchant, uint32_t me);
uint32_t fn_4f4940(SaveGame& save, int merchant, uint32_t day, uint16_t event);
uint32_t fn_4dfc94(SaveGame& save, uint16_t event);
void fn_4f34a4(SaveGame& save);
void fn_4f3264(SaveGame& save);
void fn_4f3354(SaveGame& save);
void fn_4e45c4(SaveGame& save);
bool fn_4474d0(int16_t x, int16_t y);
void fn_4e3f44(SaveGame& save);
void fn_4e43e4(SaveGame& save);
void fn_4e4984(SaveGame& save, uint16_t event);
void fn_4e8ca4(SaveGame& save, uint8_t* out, const TownMemory& town);
void fn_4e8e24(SaveGame& save, uint16_t event);
void fn_4e2144(SaveGame& save);
void fn_4fdf50(SaveGame& save, CaptainMemory& c, uint32_t index, bool young);
uint16_t fn_5097c0(SaveGame& save, bool keep);
void fn_526a50(SaveGame& save, uint32_t town, bool keep);
uint32_t fn_5261d0(const SaveGame& save, uint32_t town);
void fn_4e2634(SaveGame& save, uint16_t event);
uint32_t fn_4db330(SaveGame& save, uint16_t event);
void fn_4db7c0(SaveGame& save, uint16_t event);

}  // namespace p3::data::tick::events_detail

namespace p3::probe {

using namespace p3::data;
namespace ed = tick::events_detail;

namespace {

constexpr uint32_t kEvents = 0x6DD73C;

// Índice del evento al que apunta `address` (en el array *0x6DD73C del juego).
uint16_t event_index(const SaveGame& s, uint32_t address) {
	return uint16_t((address - s.globals.u32(kEvents)) / 0x18);
}

// La navegación de tick_probe (--cpr) para lo que mira el agua del mapa.
void use_navigation() {
	if (const Sailing* sailing = probe_sailing()) {
		tick::set_event_navigation(&sailing->nav());
	}
}

}  // namespace

void register_probes_events(ProbeRegistry& r) {
	r[0x4D85C0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		use_navigation();
		tick::fn_4d85c0(s, c.args[0]);
		return std::nullopt;
	};
	r[0x4D8DD0] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		ed::fn_4d8dd0(s);
		return std::nullopt;
	};
	r[0x4F6C10] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		ed::fn_4f6c10(s, c.merchant(c.ecx));
		return std::nullopt;
	};
	// Evento 0x04.
	r[0x4F73D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFFFF;
		return uint32_t(uint16_t(ed::fn_4f73d0(s, c.merchant(c.ecx))));
	};
	r[0x4F42B0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		ed::fn_4f42b0(s, c.merchant(c.ecx));
		return std::nullopt;
	};
	r[0x4F4F80] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		ed::fn_4f4f80(s, c.merchant(c.ecx));
		return std::nullopt;
	};
	r[0x4F5080] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		ed::fn_4f5080(s, c.merchant(c.ecx));
		return std::nullopt;
	};
	r[0x4F5130] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		ed::fn_4f5130(s, c.merchant(c.ecx));
		return std::nullopt;
	};
	r[0x4DAF10] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(ed::fn_4daf10(s, c.args[0], c.args[1], (c.args[2] & 0xFF) != 0));
	};
	r[0x4F5A20] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;
		return ed::fn_4f5a20(s, c.merchant(c.ecx), c.args[0]) ? 1 : 0;
	};
	r[0x4F4E50] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		ed::fn_4f4e50(s, c.merchant(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x4F9580] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFFFF;
		return uint32_t(uint16_t(ed::fn_4f9580(s, c.merchant(c.ecx), c.args[0])));
	};
	r[0x51B7E0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(ed::fn_51b7e0(s, c.town(c.ecx), c.merchant(c.args[0])));
	};
	r[0x4F4A20] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		ed::fn_4f4a20(s, c.merchant(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x4F4B40] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		ed::fn_4f4b40(s, c.merchant(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x4F4940] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return ed::fn_4f4940(s, c.merchant(c.ecx), c.args[0] & 0xFFFF, uint16_t(c.args[1]));
	};
	r[0x4DFC94] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return ed::fn_4dfc94(s, uint16_t(c.args[0]));
	};
	// Eventos 0x00 y 0x22.
	r[0x4DB330] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return ed::fn_4db330(s, event_index(s, c.args[0]));
	};
	r[0x4DB7C0] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		ed::fn_4db7c0(s, s.globals.u16(0x6DD744));
		return std::nullopt;
	};
	// Eventos 0x06 y 0x08.
	r[0x4E2144] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		ed::fn_4e2144(s);
		return std::nullopt;
	};
	r[0x5261D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return ed::fn_5261d0(s, uint32_t(c.town(c.ecx)));
	};
	r[0x526A50] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		ed::fn_526a50(s, uint32_t(c.town(c.ecx)), (c.args[0] & 0xFF) != 0);
		return std::nullopt;
	};
	r[0x5097C0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFFFF;
		return ed::fn_5097c0(s, (c.args[0] & 0xFF) != 0);
	};
	r[0x4E2634] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		ed::fn_4e2634(s, event_index(s, c.args[0]));
		return std::nullopt;
	};
	// Evento 0x19.
	r[0x4E8E24] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		ed::fn_4e8e24(s, event_index(s, c.args[0]));
		return std::nullopt;
	};
	// Evento 0x0D.
	r[0x4F34A4] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		ed::fn_4f34a4(s);
		return std::nullopt;
	};
	r[0x4F3264] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		ed::fn_4f3264(s);
		return std::nullopt;
	};
	r[0x4F3354] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		ed::fn_4f3354(s);
		return std::nullopt;
	};
	r[0x4E45C4] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		ed::fn_4e45c4(s);
		return std::nullopt;
	};
	r[0x4474D0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall sobre la navegación con un puntero a (s16 x, s16 y) en la pila del llamador.
		use_navigation();
		c.eax_mask = 0xFF;
		const std::optional<uint32_t> xy = c.stack(c.args[0]);
		if (!xy) {
			return std::nullopt;
		}
		return ed::fn_4474d0(int16_t(*xy & 0xFFFF), int16_t(*xy >> 16)) ? 1 : 0;
	};
	r[0x4E3F44] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		use_navigation();
		ed::fn_4e3f44(s);
		return std::nullopt;
	};
	r[0x4E43E4] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		use_navigation();
		ed::fn_4e43e4(s);
		return std::nullopt;
	};
	r[0x4E4984] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		use_navigation();
		ed::fn_4e4984(s, event_index(s, c.args[0]));
		return std::nullopt;
	};
}

}  // namespace p3::probe
