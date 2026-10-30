// Sondas de src/p3data/town_tick/events_towns.cpp.

#include <vector>

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

// Lo que events_towns.cpp expone para las sondas.
namespace p3::data::tick::events_towns_detail {

void fn_4f6570(SaveGame& save, int merchant, int32_t& amount, uint8_t me, uint8_t town);
uint32_t fn_4db4e0(SaveGame& save, uint16_t event);
void fn_4d6780(SaveGame& save, int32_t merchant);
void fn_504970(SaveGame& save);
void fn_538a80(SaveGame& save, uint32_t ship, uint8_t add9, uint8_t add_ab);
void fn_4dcea0(SaveGame& save, uint16_t event);
void fn_4dc800(SaveGame& save);
uint16_t fn_506290(const SaveGame& save);
void fn_4dc6b0(SaveGame& save);
void fn_4dcd90(SaveGame& save, uint16_t event);
bool fn_4e2314(const SaveGame& save, uint32_t town, uint8_t& merchant, uint32_t& tick);
std::vector<uint16_t> fn_531470(const SaveGame& save, uint32_t town);
void fn_5316e0(SaveGame& save);
void fn_4dca90(SaveGame& save);
void fn_4dcbf0(SaveGame& save);
void fn_4dd400(SaveGame& save);
void fn_4dd530(SaveGame& save);
void fn_4dd790(SaveGame& save);
bool fn_4f69b0(SaveGame& save, int merchant, uint8_t me);
void fn_4dda40(SaveGame& save, uint16_t event);
void fn_4febd0(OfficeMemory& o, bool has_offices);
int fn_5308a0_create(SaveGame& save, uint32_t merchant, uint32_t town);
void fn_4de4f0(SaveGame& save, uint16_t event);
bool fn_52b0d0(const SaveGame& save, int town, uint8_t type);
void fn_4de790(SaveGame& save, uint16_t event);
// 0x52B2C0 sin lo que deja en la pila: solo lo que devuelve.
bool ship_order_possible(const SaveGame& save, int town, uint32_t type, uint32_t merchant);

}  // namespace p3::data::tick::events_towns_detail

namespace p3::probe {

using namespace p3::data;
namespace et = tick::events_towns_detail;

namespace {

constexpr uint32_t kEvents = 0x6DD73C;
constexpr uint32_t kEventHead = 0x6DD744;

// Índice del evento al que apunta `address` (en el array *0x6DD73C del juego).
uint16_t event_index(const SaveGame& s, uint32_t address) {
	return uint16_t((address - s.globals.u32(kEvents)) / 0x18);
}

// El evento que atiende el despachador (la cabeza de la cola).
uint16_t head(const SaveGame& s) {
	return s.globals.u16(kEventHead);
}

}  // namespace

void register_probes_events_towns(ProbeRegistry& r) {
	// Evento 0x01.
	r[0x4DB4E0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return et::fn_4db4e0(s, event_index(s, c.args[0]));
	};
	// thiscall sobre el comerciante (int* lo que debe, en la pila del
	// llamador; su índice; la ciudad). Lo que queda por cobrar se comprueba con 0x4DB4E0.
	r[0x4F6570] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const std::optional<uint32_t> amount = c.stack(c.args[0]);
		if (amount) {
			int32_t a = int32_t(*amount);
			et::fn_4f6570(s, c.merchant(c.ecx), a, uint8_t(c.args[1]), uint8_t(c.args[2]));
		}
		return std::nullopt;
	};
	// Evento 0x03.
	r[0x4DDA40] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_4dda40(s, head(s));
		return std::nullopt;
	};
	r[0x4D6780] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		et::fn_4d6780(s, int32_t(c.args[0]));
		return std::nullopt;
	};
	r[0x504970] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_504970(s);
		return std::nullopt;
	};
	// 0x538A80 lee la operación de la pila de 0x4DCEA0, que no se graba: se
	// comprueba con 0x4DCEA0.
	r[0x4DCEA0] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_4dcea0(s, head(s));
		return std::nullopt;
	};
	r[0x4DC800] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_4dc800(s);
		return std::nullopt;
	};
	r[0x506290] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFFFF;
		return et::fn_506290(s);
	};
	r[0x4DC6B0] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_4dc6b0(s);
		return std::nullopt;
	};
	r[0x4DCD90] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_4dcd90(s, head(s));
		return std::nullopt;
	};
	r[0x4E2314] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		uint8_t merchant = 0;
		uint32_t tick = 0;
		return et::fn_4e2314(s, c.args[0], merchant, tick) ? 1 : 0;
	};
	// Devuelve una lista del montón: no cambia nada y no se puede comparar eax.
	r[0x531470] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		et::fn_531470(s, c.args[0]);
		return std::nullopt;
	};
	r[0x5316E0] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_5316e0(s);
		return std::nullopt;
	};
	r[0x4DCA90] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_4dca90(s);
		return std::nullopt;
	};
	r[0x4DCBF0] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_4dcbf0(s);
		return std::nullopt;
	};
	r[0x4DD400] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_4dd400(s);
		return std::nullopt;
	};
	r[0x4DD530] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_4dd530(s);
		return std::nullopt;
	};
	r[0x4DD790] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_4dd790(s);
		return std::nullopt;
	};
	r[0x4F69B0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;
		return et::fn_4f69b0(s, c.merchant(c.ecx), uint8_t(c.args[0])) ? 1 : 0;
	};
	// Evento 0x2A.
	r[0x4DE4F0] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_4de4f0(s, head(s));
		return std::nullopt;
	};
	r[0x4FEBD0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int office = c.office(c.ecx);
		if (office >= 0) {
			et::fn_4febd0(s.offices[size_t(office)].memory, (c.args[0] & 0xFF) != 0);
		}
		return std::nullopt;
	};
	// 0x5308A0 devuelve un puntero (no se compara eax). Con create = 0 no cambia
	// nada (facilities_detail::fn_5308a0); con create, crea la oficina.
	r[0x5308A0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		if (c.args[2] == 0) {
			tick::facilities_detail::fn_5308a0(s, c.args[0], c.args[1]);
		} else {
			et::fn_5308a0_create(s, c.args[0], c.args[1]);
		}
		return std::nullopt;
	};
	// Evento 0x2B.
	r[0x4DE790] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		et::fn_4de790(s, head(s));
		return std::nullopt;
	};
	r[0x52B0D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;
		return et::fn_52b0d0(s, c.town(c.ecx), uint8_t(c.args[0])) ? 1 : 0;
	};
	// thiscall sobre la ciudad (lo que rellena en la pila del llamador, el
	// tipo, el comerciante): solo se compara lo que devuelve.
	r[0x52B2C0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;
		return et::ship_order_possible(s, c.town(c.ecx), c.args[1], c.args[2]) ? 1 : 0;
	};
}

}  // namespace p3::probe
