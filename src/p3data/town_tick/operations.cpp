// Manejadores pequeños del ejecutor de operaciones (0x535760, tabla 0x537030;
// docs/formats/world_tick.md §7) que no son de las obras ni de los encargos
// de la Hansa.

#include <cstring>

#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

namespace {

constexpr uint32_t kConvoyCount = global::kShips + 0xF6;  // u16 (0x6DD896)
constexpr uint32_t kExpeditionMark = 0x6DE52B;  // u8 ([+0x8B] del mundo)

uint16_t half(const uint8_t* operation, size_t offset) {
	uint16_t v;
	std::memcpy(&v, operation + offset, 2);
	return v;
}

// El convoy de la operación, si existe, tiene guía y, en puerto (estado <
// 0xF), su guía puede zarpar (0x519BA0); si no, −1.
int expedition_convoy(const SaveGame& save, uint16_t convoy) {
	if (convoy >= save.globals.u16(kConvoyCount)) {
		return -1;  // Sin comprobar con el oráculo.
	}
	const GameMemory<0x3C>& c = save.convoys[convoy];
	const uint16_t lead = c.u16(0x10);
	if (lead >= save.globals.u16(global::kShipCount)) {
		return -1;  // Sin comprobar con el oráculo.
	}
	if (c.u16(0x12) < 0xF && !ship_ai_detail::fn_519ba0(save, lead)) {
		return -1;  // Sin comprobar con el oráculo.
	}
	return convoy;
}

uint32_t field(const uint8_t* operation, size_t offset) {
	uint32_t v;
	std::memcpy(&v, operation + offset, 4);
	return v;
}

}  // namespace

// La operación 0x7B (0x5365E3): {0x7B, 6 bytes, …, u32 [+0x10] ciudad (byte
// bajo) | hueco << 8 (bits 8..10)}: los 6 bytes van al hueco k (0..5) de la
// ciudad, [0x9C6 + 6·k]: los tres capitanes y los tres administradores que
// buscan trabajo en la taberna. La encola el evento 0x19.
void fn_5365e3(SaveGame& save, const uint8_t* operation) {
	const uint32_t f = field(operation, 0x10);
	const uint32_t town = f & 0xFF;
	const uint32_t slot = (f >> 8) & 7;
	if (town >= save.globals.u32(global::kWorldTownCount) || slot >= 6) {
		return;  // Sin comprobar con el oráculo.
	}
	std::memcpy(save.towns[town].memory.bytes.data() + 0x9C6 + 6 * slot, operation + 4, 6);
}

// La operación 0x79 (0x536535): ocho parejas (ciudad, valor) en [+4]: el
// valor va al byte [0x82C] de cada ciudad que exista. La encola la dieta de
// la Hansa (evento 0x21).
void fn_536535(SaveGame& save, const uint8_t* operation) {
	for (size_t i = 0; i < 16; i += 2) {
		const uint32_t town = operation[4 + i];
		if (town < save.globals.u32(global::kWorldTownCount)) {
			save.towns[town].memory.u8(0x82C) = operation[5 + i];
		}
	}
}

// La operación 0x7A (0x536570): {0x7A, u32 ciudad A, u32 ciudad B, u32, u16}:
// si la ciudad A no tiene ya un comerciante en [0x83F], [0x83F] = 0xFF,
// [0x838] el u32, [0x836] el u16 y [0x834] la ciudad B. La encola la dieta
// de la Hansa (evento 0x21).
void fn_536570(SaveGame& save, const uint8_t* operation) {
	const uint32_t a = field(operation, 4);
	const uint32_t b = field(operation, 8);
	const uint32_t towns = save.globals.u32(global::kWorldTownCount);
	if (a >= towns || b >= towns) {
		return;  // Sin comprobar con el oráculo.
	}
	TownMemory& t = save.towns[a].memory;
	const uint8_t current = t.u8(0x83F);
	if (current != 0 && current < save.globals.u16(global::kWorldMerchantCount)) {
		return;  // Sin comprobar con el oráculo.
	}
	t.u8(0x83F) = 0xFF;
	t.u32(0x838) = field(operation, 0xC);
	uint16_t v;
	std::memcpy(&v, operation + 0x10, 2);
	t.u16(0x836) = v;
	t.u8(0x834) = operation[8];
}

// La operación 0x1E (0x539B50): {0x1E, u32 dinero, u16 convoy, u16
// vuelta}: el convoy sale de expedición: su guía zarpa hacia (0x76, 0x640),
// el borde oeste del mapa (0x537670), los puntos de su ruta ([+0x24], 4 de 4
// bytes) a 0xFF y sus bytes ([+0x34]) a 0, [+0x38] = la vuelta, y lleva
// [+0x20] el dinero pedido, que sale del dueño (como mucho el que tiene; el
// global [0x6DE52B] pasa a ser al menos 0x76: el juego compara el byte de la
// operación ya cambiado). La encolan los convoyes de la IA (0x503B50).
void fn_539b50(SaveGame& save, Sailing* sailing, const uint8_t* operation) {
	const int convoy = expedition_convoy(save, half(operation, 8));
	if (convoy < 0) {
		return;
	}
	int32_t money = int32_t(field(operation, 4));
	const uint16_t back = half(operation, 0xA);
	const uint16_t lead = save.convoys[size_t(convoy)].u16(0x10);
	if (sailing) {
		sailing->set_sail(lead, 0xFF, 0x76, 0x640);
	}
	GameMemory<0x3C>& c = save.convoys[size_t(convoy)];
	c.u16(0x38) = back;
	std::memset(c.bytes.data() + 0x24, 0xFF, 0x10);
	std::memset(c.bytes.data() + 0x34, 0, 4);
	const uint8_t owner = c.u8(0);
	if (owner < save.globals.u16(global::kWorldMerchantCount)) {
		MerchantMemory& m = save.merchants[owner].memory;
		const int32_t has = m.s32(0);
		if (money > has) {
			money = has > 0 ? has : 0;  // Sin comprobar con el oráculo.
		}
		m.s32(0) = has - money;
		if (save.globals.u8(kExpeditionMark) < 0x76) {
			save.globals.u8(kExpeditionMark) = 0x76;
		}
	}
	c.s32(0x20) += money;
}

// La operación 0x1F (0x539C60): {0x1F, u16 convoy, u16 k, u16 x, u16 y, u8}:
// el punto k de la ruta de expedición del convoy ([+0x24 + 4·k]) y su byte
// ([+0x34 + k]). La encolan los convoyes de la IA (0x503B50).
void fn_539c60(SaveGame& save, const uint8_t* operation) {
	const int convoy = expedition_convoy(save, half(operation, 4));
	if (convoy < 0) {
		return;
	}
	const uint16_t k = half(operation, 6);
	if (k > 5) {
		return;  // El juego no lo mira (escribiría fuera del convoy).
	}
	GameMemory<0x3C>& c = save.convoys[size_t(convoy)];
	c.u16(0x24 + 4 * k) = half(operation, 8);
	c.u16(0x26 + 4 * k) = half(operation, 0xA);
	c.u8(0x34 + k) = operation[0xC];
}

}  // namespace p3::data::tick
