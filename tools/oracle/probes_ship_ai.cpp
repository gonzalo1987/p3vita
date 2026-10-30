// Sondas de src/p3data/town_tick/ship_ai.cpp.
//
// Los manejadores de los estados 0, 4 y 5 de 0x506720 no son funciones del
// juego: se registran en sus direcciones (0x5067D0, 0x506A19, 0x506B6A) para
// las muestras que graba agents/ship_ai/ship_oracle.py desde el salto por
// estado hasta 0x506BED, con ecx = el barco, arg 0 = la dirección del enlace
// (ebx) y eax = 1 si el recorrido sigue por el [+0x6] del barco. El mismo
// script graba en 0x52F031 el float que 0x52F030 deja en su último argumento.

#include <cstring>

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

namespace p3::data::tick::ship_ai_detail {
uint32_t fn_518d60(const SaveGame& save, int ship);
void fn_517770(SaveGame& save, int ship, uint32_t flags);
void fn_518e00(SaveGame& save, int ship);
void fn_518ff0(SaveGame& save, int ship, uint32_t mask);
void fn_519170(SaveGame& save, int ship, uint32_t mask);
int32_t fn_5184f0(SaveGame& save, int ship);
void fn_519fc0(SaveGame& save, int ship);
bool fn_519ba0(const SaveGame& save, int ship);
bool fn_519c40(const SaveGame& save, int ship);
void fn_509ae0(SaveGame& save);
void fn_501be0(SaveGame& save, int convoy);
void fn_501680(SaveGame& save, int convoy, uint16_t ship, uint16_t mode);
void fn_509b90(SaveGame& save, uint32_t ship, uint32_t immediate, uint32_t wait_days, uint32_t destination);
void fn_52ac80(SaveGame& save, int town, uint16_t ship);
void fn_535630(SaveGame& save, std::vector<uint8_t>& towns);
void fn_535400(SaveGame& save, uint32_t town, uint32_t merchant);
bool fn_519eb0(SaveGame& save, int ship, uint16_t* link);
}  // namespace p3::data::tick::ship_ai_detail

namespace p3::probe {

using namespace p3::data;
using namespace p3::data::tick::ship_ai_detail;

namespace {

// El u16 que enlaza un barco en la lista de barcos en puerto: la cabeza
// ([+0xE8] del objeto de los barcos) o el [+0x6] del anterior.
uint16_t* link_at(SaveGame& s, const ProbeCall& c, uint32_t address) {
	if (address == global::kShips + 0xE8) {
		return &s.docked_head();
	}
	const Location l = c.locate(address);
	if (l.kind == Location::Ship && l.offset == 6) {
		return &s.ships[size_t(l.index)].next_in_list();
	}
	return nullptr;
}

std::optional<uint32_t> handler(bool (*fn)(SaveGame&, int, uint16_t*), SaveGame& s, const ProbeCall& c) {
	uint16_t* link = link_at(s, c, c.args[0]);
	if (!link) {
		return 0xDEAD;
	}
	return fn(s, c.ship(c.ecx), link) ? 1 : 0;
}

uint32_t bits(float f) {
	uint32_t b;
	std::memcpy(&b, &f, sizeof(b));
	return b;
}

}  // namespace

void register_probes_ship_ai(ProbeRegistry& r) {
	r[0x5067D0] = [](SaveGame& s, const ProbeCall& c) { return handler(tick::fn_5067d0, s, c); };
	r[0x506A19] = [](SaveGame& s, const ProbeCall& c) { return handler(tick::fn_506a19, s, c); };
	r[0x506B6A] = [](SaveGame& s, const ProbeCall& c) { return handler(tick::fn_506b6a, s, c); };
	r[0x518B80] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_518b80(s, c.ship(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x52F031] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		float f;
		tick::ship_trade_detail::fn_52f030(s, c.args[0], c.args[1], c.args[2], int32_t(c.args[3]), f);
		return bits(f);
	};
	r[0x518D60] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return fn_518d60(s, c.ship(c.ecx));
	};
	r[0x517770] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_517770(s, c.ship(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x518E00] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_518e00(s, c.ship(c.ecx));
		return std::nullopt;
	};
	r[0x518FF0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_518ff0(s, c.ship(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x519170] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_519170(s, c.ship(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x5184F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(fn_5184f0(s, c.ship(c.ecx)));
	};
	r[0x519FC0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_519fc0(s, c.ship(c.ecx));
		return std::nullopt;
	};
	r[0x519BA0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;
		return fn_519ba0(s, c.ship(c.ecx)) ? 1 : 0;
	};
	r[0x519C40] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;
		return fn_519c40(s, c.ship(c.ecx)) ? 1 : 0;
	};
	r[0x509AE0] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		fn_509ae0(s);
		return std::nullopt;
	};
	r[0x501BE0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_501be0(s, c.convoy(c.ecx));
		return std::nullopt;
	};
	r[0x501680] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_501680(s, c.convoy(c.ecx), uint16_t(c.args[0]), uint16_t(c.args[1]));
		return std::nullopt;
	};
	r[0x509B90] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_509b90(s, c.args[0], c.args[1], c.args[2], c.args[3]);
		return std::nullopt;
	};
	r[0x52AC80] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_52ac80(s, c.town(c.ecx), uint16_t(c.args[0]));
		return std::nullopt;
	};
	r[0x535400] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		fn_535400(s, c.args[0], c.args[1]);
		return std::nullopt;
	};
	r[0x535630] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// El registro del hueco cuyo puntero ([0x6DEF90 + 4·i]) es el argumento.
		for (uint32_t i = 0; i < 8; ++i) {
			if (s.globals.u32(0x6DEF90 + 4 * i) == c.args[0]) {
				fn_535630(s, s.player_towns[i]);
				break;
			}
		}
		return std::nullopt;
	};
	r[0x519EB0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;
		uint16_t* link = link_at(s, c, c.args[0]);
		if (!link) {
			return 0xDEAD;
		}
		return fn_519eb0(s, c.ship(c.ecx), link) ? 1 : 0;
	};
}

}  // namespace p3::probe
