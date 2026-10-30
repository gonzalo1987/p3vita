// Convoyes en puerto: salir al mar y lo que hacen al zarpar.
// Ver docs/formats/navigation.md §9 y la cabecera de convoys.cpp.
//
// Las llama el recorrido de los convoyes de 0x506720 según el estado del
// convoy [+0x12]:
//   estado 2 (0x5074DB), saliendo: el primer tick ([+0x16] = 0), 0x501EB0
//     (todos los barcos pasan al estado 2 y el convoy empieza a contar) y
//     0x503CF0 (velocidad que se muestra); al terminar de salir, si todos
//     son del mismo dueño ([+0x14] bit 1), 0x503B50.
//   estado 9 (0x507852): 0x503930.
//
// Comprobado con el oráculo en las 12 partidas durante 60 días y, para las
// ramas que no salen en ellas (marcadas «solo en las pruebas sintéticas»),
// con llamadas al código del juego sobre convoyes cambiados a propósito (sin
// guía, guía sin marineros, ciudad cerrada, dueño humano o pirata, flags del
// mundo, todos los bits de [+0x4D8] del comerciante puestos, 40 ticks en el
// estado 9...). Lo que queda «Sin comprobar con el oráculo» se dice en cada sitio.

#include <cstring>

#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

using ConvoyMemory = GameMemory<0x3C>;

// Bits de [0x2C8] de la ciudad con los que no se sale del puerto (bloqueo o asedio).
constexpr uint32_t kClosedPort = 0x4000200;

constexpr uint32_t kWorldFlag52A = 0x6DE52A;  // u8 [+0x8A] del mundo
constexpr uint32_t kWorldFlag52B = 0x6DE52B;  // u8 [+0x8B] del mundo (lo pone 0x539C43)
constexpr uint32_t kNoQueue = 0x6DF31C;  // u32: con él, la IA no encola órdenes (también en 0x516D3B)

uint16_t ship_count(const SaveGame& save) {
	return save.globals.u16(global::kShipCount);
}

uint32_t town_count(const SaveGame& save) {
	return save.globals.u32(global::kWorldTownCount);
}

}  // namespace

namespace tick::convoys_port_detail {

// Los 15 objetos de 0x2C bytes del mundo en [+0xB10] (0x6DEFB0): u8 [+0x4]
// un punto de kObjectPoints (< 0x1E) y u32 [+0x8] un tick.
constexpr uint32_t kWorldObjects = 0x6DEFB0;
constexpr uint32_t kWorldObjectSize = 0x2C;
constexpr uint32_t kWorldObjectCount = 15;
// 0x673858: 30 puntos (u16 x, u16 y) que se eligen por el [+0x4] de esos objetos.
constexpr uint16_t kObjectPoints[30][2] = {
	{248, 22}, {236, 29}, {217, 40}, {216, 51}, {217, 31}, {227, 61}, {249, 46}, {259, 37}, {273, 28}, {280, 27},
	{286, 23}, {294, 28}, {301, 36}, {310, 41}, {314, 49}, {305, 56}, {309, 59}, {287, 47}, {335, 54}, {323, 33},
	{358, 53}, {341, 42}, {347, 69}, {341, 61}, {343, 55}, {394, 74}, {371, 90}, {289, 60}, {261, 64}, {345, 84},
};
// u32: número de puntos de SaveGame::expeditions (scripts/Expeditionen.dat,
// *0x6DF2E0).
constexpr uint32_t kExpeditionCount = 0x6DF2E4;
// [+0x4D8] del comerciante: un bit por punto de SaveGame::expeditions.
constexpr uint32_t kExpeditionBits = 0x4D8;

// 0x535740: inicializa una operación para la cola (0x14 bytes): tipo
// 0xFFFFFFFF y el resto a 0.
void fn_535740(uint8_t* operation) {
	const uint32_t none = 0xFFFFFFFF;
	std::memcpy(operation, &none, 4);
	std::memset(operation + 4, 0, 0x10);
}

// 0x534F10 (thiscall sobre el [+0x4D8] del comerciante `merchant`): elige un
// punto (u16 x, u16 y) para la operación 0x1F. Con `near` (su byte bajo),
// primero el de uno de los 15 objetos del mundo, empezando por (seed & 0xFF)
// % 15: uno que ya haya pasado su tick y tenga punto, dando la vuelta una
// vez; si no, uno que tenga punto, desde el mismo sin dar la vuelta. Si no,
// un punto de SaveGame::expeditions desde 100 + (seed & 0xFF) % (número −
// 100) cuyo bit no esté puesto en el comerciante, dando la vuelta una vez
// desde el 100; si no hay ninguno, 0xFFFF y 0xFFFF.
void fn_534f10(const SaveGame& save, int merchant, uint16_t& x, uint16_t& y, uint32_t near, uint32_t seed) {
	const GlobalMemory& g = save.globals;
	auto object_point = [&](uint32_t i) { return g.u8(kWorldObjects + kWorldObjectSize * i + 0x4); };
	if (uint8_t(near) != 0) {
		const uint32_t start = (seed & 0xFF) % kWorldObjectCount;
		const uint32_t tick = g.u32(global::kWorldTick);
		bool wrapped = false;
		uint32_t i = start;
		int found = -1;
		for (;;) {
			if (tick > g.u32(kWorldObjects + kWorldObjectSize * i + 0x8) && object_point(i) < 0x1E) {
				found = int(i);
				break;
			}
			if (++i < kWorldObjectCount) {
				continue;
			}
			if (!wrapped) {
				i = 0;
				wrapped = true;
				continue;
			}
			break;
		}
		if (found < 0) {
			// Este segundo recorrido no da la vuelta: la marca ya está puesta.
			for (i = start; i < kWorldObjectCount; ++i) {
				if (object_point(i) < 0x1E) {
					found = int(i);
					break;
				}
			}
		}
		if (found >= 0) {
			const uint8_t point = object_point(uint32_t(found));
			x = kObjectPoints[point][0];
			y = kObjectPoints[point][1];
			return;
		}
	}
	const uint32_t count = g.u32(kExpeditionCount);
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	// Con número = 100 el juego divide por cero.
	const uint32_t span = count - 100;
	uint32_t j = (span ? (seed & 0xFF) % span : 0) + 100;
	bool wrapped = false;
	for (;;) {
		if (!(m.u32(kExpeditionBits + 4 * (j >> 5)) & (1u << (j & 31)))) {
			// Fuera de SaveGame::expeditions (sin leer) el juego leería otra
			// memoria; en el oráculo la tabla es un bloque a cero.
			if (j < save.expeditions.size()) {
				x = save.expeditions[j][0];
				y = save.expeditions[j][1];
			} else {
				x = 0;
				y = 0;
			}
			return;
		}
		if (++j < count) {
			continue;
		}
		if (wrapped) {
			// Solo en las pruebas sintéticas.
			x = 0xFFFF;
			y = 0xFFFF;
			return;
		}
		wrapped = true;
		j = 100;
	}
}

}  // namespace tick::convoys_port_detail

using namespace tick::convoys_port_detail;
using tick::ship_ai_detail::fn_501be0;
using tick::ship_ai_detail::fn_519ba0;
using tick::ship_ai_detail::fn_519c40;

// 0x501EB0: el convoy empieza a salir del puerto (estado 2, primer tick).
// Si el que lo guía no puede guiarlo o no puede zarpar, el convoy vuelve al
// estado 0; con el puerto cerrado, espera (estado 1, 0x100 ticks). Si no, el
// que lo guía sale al punto de llegada de la ciudad con la ruta hacia el
// destino (0x516BC0); los barcos que no pueden zarpar salen del convoy
// (0x509F80) y se quedan atracados, y los demás pasan al estado 2 con su
// carga sumada al [0x670] del destino y al [0x370] de la ciudad de la que
// salen (no al del destino, como hace 0x5090E0 con un barco suelto), y con
// la llegada prevista del guía. Devuelve true si el convoy sale.
bool Sailing::fn_501eb0(int convoy) {
	ConvoyMemory& c = save_.convoys[size_t(convoy)];
	const uint16_t leader = c.u16(0x10);
	if (leader >= ship_count(save_)) {
		// Solo en las pruebas sintéticas.
		c.u16(0x12) = 0;
		return false;
	}
	SavedShip& lead = save_.ships[leader];
	ShipMemory& l = lead.memory;
	if (!fn_519c40(save_, leader) || !fn_519ba0(save_, leader)) {
		// Solo en las pruebas sintéticas.
		c.u32(0x18) |= 0x20;
		c.u16(0x12) = 0;
		l.u8(0x38) = l.u8(0x39);
		l.u16(0x134) = 0;
		l.u16(0x3C) = 0xA0;
		return false;
	}
	const uint32_t town = c.u16(0x1C);
	if (town < town_count(save_) && (save_.towns[town].memory.u32(0x2C8) & kClosedPort)) {
		// Solo en las pruebas sintéticas (con la ciudad del guía, [+0x39],
		// igual a la del convoy no puede pasar: 0x519BA0 ya ha mirado el puerto).
		c.u32(0x18) |= 0x20;
		c.u16(0x12) = 1;
		c.u16(0x16) = 0x100;
		l.u8(0x38) = c.u8(0x1C);
		l.u16(0x134) = 1;
		l.u16(0x3C) = 0xA0;
		l.u16(0x138) = 0x100;
		clear_ship_route(lead);  // 0x516840
		return false;
	}
	leave_port(lead);  // 0x516BC0
	// Si el convoy es del jugador, 0x44F940 (interfaz; solo en las pruebas sintéticas).
	c.u8(0x14) &= 0xEF;
	uint32_t i = c.u16(0xA);
	while (i < ship_count(save_)) {
		ShipMemory& m = save_.ships[i].memory;
		if (!fn_519ba0(save_, int(i))) {
			// Sin comprobar con el oráculo (no sale en las 12 partidas y
			// depende de 0x509F80, de convoys_members.cpp): no puede zarpar y
			// sale del convoy.
			m.u8(0x3C) |= 0x40;
			m.u8(0x38) = m.u8(0x39);
			const uint32_t self = i;
			i = m.u16(0x6);
			c.u32(0x18) |= 0x4020;
			fn_509f80(uint16_t(self));
			m.u16(0x134) = 0;
			continue;
		}
		const uint32_t destination = c.u8(0x1F);
		if (destination < town_count(save_)) {
			TownMemory& t = save_.towns[destination].memory;
			for (uint32_t w = 0; w < 24; ++w) {
				t.u32(0x670 + 4 * w) += m.u32(0x54 + 4 * w);  // de camino a la ciudad
			}
		}
		// El juego no comprueba la ciudad del convoy (de 0x501F03 se llega
		// aquí también con [+0x1C] fuera de la tabla).
		const uint32_t here = c.u16(0x1C);
		if (here < save_.towns.size()) {
			TownMemory& t = save_.towns[here].memory;
			for (uint32_t w = 0; w < 24; ++w) {
				t.u32(0x370 + 4 * w) += m.u32(0x54 + 4 * w);
			}
		}
		if (m.u8(0x0) != c.u8(0x0)) {
			c.u8(0x14) &= 0xFD;
		}
		m.u16(0x134) = 2;
		m.u8(0x37) = c.u8(0x1C);
		m.u8(0x3C) |= 0x20;
		m.u8(0x38) = c.u8(0x1F);
		m.u32(0x48) = l.u32(0x48);  // la llegada prevista del que lo guía
		i = m.u16(0x6);
	}
	c.u8(0x1E) = c.u8(0x1C);
	fn_501be0(save_, convoy);
	c.u16(0x12) = 2;
	c.u16(0x16) = 1;
	return true;
}

// 0x503B50: el convoy de la IA, todos del mismo dueño, ha salido del puerto.
// Quita el bit 1 de [+0x14] y, si todos sus barcos tienen al menos 15/16 del
// casco y como mucho 5000 de bodega ocupada, encola para el convoy del que lo
// guía la operación 0x1E (con 0x14 + rand & 0x1F) y cuatro 0x1F con los
// puntos 0..3 que elige 0x534F10.
void Sailing::fn_503b50(int convoy) {
	ConvoyMemory& c = save_.convoys[size_t(convoy)];
	const GlobalMemory& g = save_.globals;
	const uint8_t owner = c.u8(0x0);
	if (owner >= g.u16(global::kWorldMerchantCount)) {
		return;  // Solo en las pruebas sintéticas (los piratas se quedan quietos).
	}
	const MerchantMemory& merchant = save_.merchants[owner].memory;
	uint32_t ok = merchant.u16(0x8) != 0 ? 1 : 0;  // de la IA
	if (!ok) {
		return;  // Solo en las pruebas sintéticas.
	}
	c.u8(0x14) &= 0xFD;
	if (g.u8(kWorldFlag52A) != 0 || g.u8(kWorldFlag52B) != 0 || g.u32(kNoQueue) != 0) {
		return;
	}
	const uint16_t leader = c.u16(0x10);
	if (leader >= ship_count(save_)) {
		return;  // Solo en las pruebas sintéticas.
	}
	uint8_t op[0x14];
	fn_535740(op);
	uint8_t near = 1;
	const uint16_t leader_convoy = save_.ships[leader].memory.u16(0x8);
	for (uint32_t i = c.u16(0xA); i < ship_count(save_);) {
		const ShipMemory& m = save_.ships[i].memory;
		const int32_t max_health = m.s32(0x14);
		const int32_t healthy = int32_t(uint32_t(max_health) - uint32_t(max_health >> 4));
		if (m.s32(0x18) < healthy || m.s32(0x118) > 0x1388) {
			ok = 0;
		}
		i = m.u16(0x6);
		if (!ok) {
			return;
		}
	}
	if (!ok) {
		return;  // Sin comprobar con el oráculo (no puede pasar).
	}
	auto put32 = [&op](size_t o, uint32_t v) { std::memcpy(op + o, &v, 4); };
	auto put16 = [&op](size_t o, uint16_t v) { std::memcpy(op + o, &v, 2); };
	put32(0x0, 0x1E);
	put16(0x8, leader_convoy);
	put32(0x4, 0);
	put16(0xA, 0);
	op[0xC] = uint8_t((tick::game_rand(save_) & 0x1F) + 0x14);
	tick::enqueue_operation(save_, op, sizeof(op));  // 0x54AA70
	put32(0x0, 0x1F);
	for (uint16_t k = 0; k < 4; ++k) {
		put16(0x4, leader_convoy);
		put16(0x6, k);
		op[0xC] = 3;
		const uint32_t seed = tick::game_rand(save_);
		uint16_t x, y;
		fn_534f10(save_, owner, x, y, near, seed);
		put16(0x8, x);
		put16(0xA, y);
		tick::enqueue_operation(save_, op, sizeof(op));
		near = 0;
	}
}

// 0x503CF0: la velocidad que se muestra de cada barco del convoy ([+0x30]) es
// la mitad de la del convoy ([+0xC], la del más lento).
void Sailing::fn_503cf0(int convoy) {
	const ConvoyMemory& c = save_.convoys[size_t(convoy)];
	for (uint32_t i = c.u16(0xA); i < ship_count(save_); i = save_.ships[i].memory.u16(0x6)) {
		save_.ships[i].memory.u32(0x30) = c.u32(0xC) >> 1;
	}
}

// 0x503930: convoy en el estado 9. Los barcos que no están en él pasan al 9
// con [+0x138] a 0; [+0x138] sube hasta 0x20 y, al llegar, la velocidad que
// se muestra pasa a 0. No sale en las 12 partidas: solo en las pruebas
// sintéticas (40 llamadas seguidas sobre cada convoy).
void Sailing::fn_503930(int convoy) {
	const ConvoyMemory& c = save_.convoys[size_t(convoy)];
	for (uint32_t i = c.u16(0xA); i < ship_count(save_);) {
		SavedShip& s = save_.ships[i];
		ShipMemory& m = s.memory;
		if (m.u16(0x134) != 9) {
			m.u8(0x3C) |= 0x20;
			m.u16(0x134) = 9;
			m.u16(0x138) = 0;
		}
		const uint16_t timer = m.u16(0x138);
		if (timer < 0x20) {
			const uint16_t next = uint16_t(timer + 1);
			m.u16(0x138) = next;
			if (next == 0) {
				// No puede pasar (el juego compara con 0 después de sumar): la
				// mitad de la velocidad y la escena del puerto (0x6135C0).
				m.u16(0x3C) |= 0x80;
				m.u32(0x30) = speed(s) >> 1;
			} else if (next == 0x20) {
				m.u16(0x3C) |= 0x80;
				m.u32(0x30) = 0;
			}
		}
		i = m.u16(0x6);
	}
}

}  // namespace p3::data
