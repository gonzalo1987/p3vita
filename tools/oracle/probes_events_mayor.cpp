// Sondas de src/p3data/town_tick/events_mayor.cpp.

#include <cstring>

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

// Lo que events_mayor.cpp expone para las sondas.
namespace p3::data::tick::events_mayor_detail {

void fn_5311d0(uint32_t tick, uint8_t& day, uint8_t& month);
uint32_t fn_4f9360(SaveGame& save, int merchant);
void fn_528cf0(SaveGame& save, int town, uint8_t out[4]);
void fn_4f6420(SaveGame& save, int merchant);
void fn_529f20(SaveGame& save, int town);
void fn_528e60(SaveGame& save, int town);
void fn_4dbb00(SaveGame& save);
void fn_529700(const SaveGame& save, uint8_t town[4], uint8_t merchant[4]);
void fn_4dbbd0(SaveGame& save);
uint32_t fn_4dc1f0(SaveGame& save, uint16_t event);
void fn_517e00(SaveGame& save, int ship);
void fn_516b10(SaveGame& save, int ship, uint32_t town);
uint16_t fn_507d60(SaveGame& save, uint16_t& out, uint32_t town, uint32_t mode);
void fn_52a8d0(SaveGame& save, int town, int ship, uint32_t type);
void fn_5181d0(SaveGame& save, int ship);
void fn_509a50(SaveGame& save, int ship);
int32_t fn_4da860(const SaveGame& save, uint32_t ship);
int32_t fn_4f4400(const SaveGame& save, int merchant, uint32_t ship, uint32_t town);
int32_t fn_4f4550(const SaveGame& save, int merchant, uint32_t record, uint32_t town, uint32_t me);
void fn_509250(SaveGame& save, uint32_t ship, uint32_t winner, int32_t price90, int32_t price, uint32_t town);
void fn_5256f0(SaveGame& save, int town, uint32_t record, uint32_t winner, int32_t price, int32_t price90);
uint32_t fn_4e2cd4(SaveGame& save, uint16_t event);

}  // namespace p3::data::tick::events_mayor_detail

namespace p3::probe {

using namespace p3::data;
namespace md = tick::events_mayor_detail;

namespace {

constexpr uint32_t kEvents = 0x6DD73C;

// El byte de la cola de eventos (*0x6DD73C) al que apunta `address`, o nullptr.
uint8_t* event_byte(SaveGame& s, uint32_t address) {
	const uint32_t base = s.globals.u32(kEvents);
	if (address < base) {
		return nullptr;
	}
	const uint32_t index = (address - base) / 0x18;
	const uint32_t offset = (address - base) % 0x18;
	if (index >= s.events.size()) {
		return nullptr;
	}
	return s.events[index].bytes.data() + offset;
}

// Índice del evento al que apunta `address`.
uint16_t event_index(const SaveGame& s, uint32_t address) {
	return uint16_t((address - s.globals.u32(kEvents)) / 0x18);
}

}  // namespace

void register_probes_events_mayor(ProbeRegistry& r) {
	// Evento 0x02: la elección del alcalde.
	r[0x4DBB00] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		md::fn_4dbb00(s);
		return std::nullopt;
	};
	r[0x528E60] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		md::fn_528e60(s, c.town(c.ecx));
		return std::nullopt;
	};
	r[0x528CF0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// La salida va a la pila del llamador: solo se compara el estado.
		uint8_t out[4];
		md::fn_528cf0(s, c.town(c.ecx), out);
		return std::nullopt;
	};
	r[0x4F9360] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return md::fn_4f9360(s, c.merchant(c.ecx));
	};
	r[0x5311D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// (tick, &día, &mes, &año): los dos primeros, en un evento.
		uint8_t* day = event_byte(s, c.args[1]);
		uint8_t* month = event_byte(s, c.args[2]);
		uint8_t d = 0;
		uint8_t m = 0;
		md::fn_5311d0(c.args[0], day ? *day : d, month ? *month : m);
		return std::nullopt;
	};
	r[0x4F6420] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		md::fn_4f6420(s, c.merchant(c.ecx));  // eax es un puntero al nombre.
		return std::nullopt;
	};
	r[0x529F20] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		md::fn_529f20(s, c.town(c.ecx));
		return std::nullopt;
	};
	// Evento 0x20: la elección del alderman.
	r[0x4DBBD0] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		md::fn_4dbbd0(s);
		return std::nullopt;
	};
	r[0x529700] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		// cdecl con la salida en la pila del llamador: no cambia nada más.
		uint8_t towns[4];
		uint8_t merchants[4];
		md::fn_529700(s, towns, merchants);
		return std::nullopt;
	};
	// Evento 0x21: la votación de la Hansa.
	r[0x4DC1F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return md::fn_4dc1f0(s, uint16_t(c.args[0]));
	};
	// Eventos 0x0A y 0x36: las subastas.
	r[0x4E2CD4] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return md::fn_4e2cd4(s, event_index(s, c.args[0]));
	};
	r[0x507D60] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// (&índice, ciudad, modo): eax es el puntero al barco.
		uint8_t* out = event_byte(s, c.args[0]);
		uint16_t index = 0;
		if (out) {
			std::memcpy(&index, out, 2);
		}
		md::fn_507d60(s, index, c.args[1], c.args[2]);
		if (out) {
			std::memcpy(out, &index, 2);
		}
		return std::nullopt;
	};
	r[0x517E00] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		md::fn_517e00(s, c.ship(c.ecx));
		return std::nullopt;
	};
	r[0x516B10] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		md::fn_516b10(s, c.ship(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x52A8D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		md::fn_52a8d0(s, c.town(c.ecx), c.ship(c.args[0]), c.args[1]);
		return std::nullopt;
	};
	r[0x5181D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		md::fn_5181d0(s, c.ship(c.ecx));
		return std::nullopt;
	};
	r[0x509A50] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		md::fn_509a50(s, c.ship(c.args[0]));
		return std::nullopt;
	};
	r[0x4DA860] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(md::fn_4da860(s, c.args[0]));
	};
	r[0x4F4400] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(md::fn_4f4400(s, c.merchant(c.ecx), c.args[0], c.args[1]));
	};
	r[0x4F4550] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(md::fn_4f4550(s, c.merchant(c.ecx), c.args[0], c.args[1], c.args[2]));
	};
	r[0x509250] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		md::fn_509250(s, c.args[0], c.args[1], int32_t(c.args[2]), int32_t(c.args[3]), c.args[4]);
		return std::nullopt;
	};
	r[0x5256F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		md::fn_5256f0(s, c.town(c.ecx), c.args[0], c.args[1], int32_t(c.args[2]), int32_t(c.args[3]));
		return std::nullopt;
	};
}

}  // namespace p3::probe
