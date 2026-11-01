// Convoyes: los barcos entran y salen, y los convoyes se deshacen o se borran.
// Ver docs/formats/navigation.md §9 y la cabecera de convoys.cpp.
//
// Campos del convoy (SaveGame::convoys, 0x3C bytes) que se usan aquí, además
// de los de la cabecera de convoys.cpp:
//   [+0x14] u16: bit 1 todos los barcos del mismo dueño (no se cobra ni se
//           gana reputación al deshacerlo); bit 4 ningún barco del dueño puede
//           guiarlo (0x5017F0)
//   [+0x18] u32 avisos: 0x4000 ha salido un barco, 0x400 cambia el dueño
// En el objeto de los barcos (0x6DD7A0): [+0xE4] primer convoy libre, [+0xEC]
// primer convoy que existe (enlazados por el [+0x8] de cada convoy).
// En la ciudad, el u16 [+0x820]: primera de una lista de convoyes (estado 6),
// enlazada también por [+0x8].
// En el barco: [+0x114] s32, lo que costó su carga (recalculate_cargo, en
// trade.cpp), con lo que se calcula lo que paga su dueño al que guía el
// convoy; [+0x132] u16 ruta automática del jugador (índice en la lista de
// 0x6DD728; 0xFFFF sin ella).
// En el comerciante: [+0x4BC] s32 gastos (lo pagado por ir en convoyes
// ajenos) y [+0x4C8] s32 ingresos (lo cobrado por guiarlos; también la paga
// del barco auslieger, core.cpp), estadísticas; [+0x120 + 12·i] float
// reputación en la ciudad i (del registro de 12 bytes de [+0x11C]); [+0x464]
// float factor de la reputación; [+0x22] u16 y [+0x20] s8 suben guiando
// convoyes que llegan y bajan con los que se deshacen antes (0x4F9050).
//
// Comprobado con el oráculo en las 12 partidas (town_oracle.py --barcos y
// tick_probe) y, para las ramas que no salen en ellas (marcadas «solo en las
// pruebas sintéticas»), con llamadas al código del juego sobre convoyes,
// barcos y comerciantes cambiados al azar (agents/members/synth_members.py).

#include <cmath>

#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick::events_detail {
// 0x5097C0 (town_tick/events*.cpp): a new captain.
uint16_t fn_5097c0(SaveGame& save, bool keep);
}  // namespace p3::data::tick::events_detail

namespace p3::data {

namespace {

// En el objeto de los barcos (0x6DD7A0).
constexpr uint32_t kDockedHead = global::kShips + 0xE8;  // u16 cabeza de la lista de barcos en puerto
constexpr uint32_t kSailingHead = global::kShips + 0xEA;  // u16 cabeza de la lista de barcos en el mar
constexpr uint32_t kConvoyFree = global::kShips + 0xE4;  // u16 primer convoy libre
constexpr uint32_t kConvoyHead = global::kShips + 0xEC;  // u16 primer convoy que existe
constexpr uint32_t kConvoyCount = global::kShips + 0xF6;  // u16 convoyes reservados

// Constantes de .rdata en float.
constexpr float k0_1 = 0x1.99999ap-4f;  // 0x66FD5C (0x3DCCCCCD)
constexpr float kMinus0_2 = -0x1.99999ap-3f;  // 0x672F40 (0xBE4CCCCD)

uint16_t ship_count(const SaveGame& save) {
	return save.globals.u16(global::kShipCount);
}

uint16_t convoy_count(const SaveGame& save) {
	return save.globals.u16(kConvoyCount);
}

uint16_t captain_count(const SaveGame& save) {
	return save.globals.u16(global::kCaptainCount);
}

uint16_t merchant_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldMerchantCount);
}

uint32_t town_count(const SaveGame& save) {
	return save.globals.u32(global::kWorldTownCount);
}

int32_t add(int32_t a, uint32_t b) {
	return int32_t(uint32_t(a) + b);
}

int32_t sub(int32_t a, uint32_t b) {
	return int32_t(uint32_t(a) - b);
}

}  // namespace

namespace tick::convoys_members_detail {

// 0x4F9050 (sobre el comerciante): reputación por un convoy que ha guiado,
// con `ships` barcos de otros comerciantes. Si ha llegado (`arrived`), suma
// 0,1 por barco (por [+0x464]) a la reputación de cada ciudad y 20 por barco
// a [+0x22]; si no, resta 0,2 y 30. [+0x20] (s8) suma los barcos, entre −127
// y 127.
void fn_4f9050(SaveGame& save, int merchant, uint32_t ships, uint32_t arrived) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const float count = float(double(ships));  // fild QWORD {ships, 0}
	float r;
	uint32_t points;
	if (arrived != 0) {
		r = float(double(count) * double(k0_1));
		points = ships * 20;
	} else {
		r = float(double(count) * double(kMinus0_2));
		points = 0u - (ships * 16 - ships) * 2;
	}
	r = float(double(r) * double(m.f32(0x464)));
	m.u16(0x22) = uint16_t(m.u16(0x22) + points);
	for (uint32_t i = 0; i < town_count(save); ++i) {
		float& reputation = m.f32(0x120 + 12 * size_t(i));
		reputation = float(double(r) + double(reputation));
	}
	const int32_t total = int32_t(uint32_t(int32_t(int8_t(m.u8(0x20)))) + ships);
	if (total > 0x7F) {
		m.u8(0x20) = 0x7F;
	} else if (total < -0x7F) {
		m.u8(0x20) = 0x81;  // Solo en las pruebas sintéticas.
	} else {
		m.u8(0x20) = uint8_t(total);
	}
}

}  // namespace tick::convoys_members_detail

using tick::convoys_members_detail::fn_4f9050;

// 0x5017F0 (sobre el convoy): comprueba el barco que lo guía ([+0x10]). Si
// sigue en el convoy con casco y marineros, devuelve true sin más. Si no
// (y si navegaba, lo saca de la lista ordenada por x), busca otro: en los de
// un comerciante, uno de su dueño que pueda guiarlo (0x519C40), que hereda la
// ruta automática; si no hay, uno de su dueño con capitán (true) o sin él
// (false, con el bit 4 de [+0x14]); si no queda ninguno de su dueño, el
// primero, que pasa a ser el dueño del convoy (false). El nuevo vuelve a la
// lista ordenada por x si el viejo estaba en ella. Sin barcos, sin guía y false.
bool Sailing::fn_5017f0(int convoy) {
	GameMemory<0x3C>& c = save_.convoys[size_t(convoy)];
	bool relink = false;  // [esp+0x10]: el viejo estaba en la lista ordenada por x.
	const uint16_t count = ship_count(save_);
	if (c.u16(0xA) >= count) {
		c.u16(0x10) = 0xFFFF;  // Solo en las pruebas sintéticas.
		return false;
	}
	if (c.u16(0x10) < count) {
		ShipMemory& old = save_.ships[c.u16(0x10)].memory;
		if (old.s32(0x18) > 0 && old.u16(0x40) > 0) {
			for (uint32_t i = c.u16(0xA); i < count; i = save_.ships[i].memory.u16(0x6)) {
				if (i == c.u16(0x10)) {
					return true;
				}
			}
		}
		// Lo que sigue, solo en las pruebas sintéticas: en las 12 partidas el que
		// guía sigue siempre en el convoy.
		const uint16_t status = old.u16(0x134);
		if (status == 0xF || status == 0x10 || status == 0x12) {
			relink = old.u16(0xA) < count || old.u16(0xC) < count;
			unlink_x(c.u16(0x10));  // 0x50D040
		}
	}
	if (uint16_t(c.u8(0x0)) >= merchant_count(save_)) {
		// A pirate convoy (0x5018E4): the first ship with hull and a captain
		// guides it; else the first with hull, which gets a new captain
		// (0x5097C0 with 1); either way the bit 0x10 of [+0x14] is cleared and
		// it goes back to the list by x if the old one was in it (true). If no
		// ship has hull, [+0x10] is left past the last one and false.
		const uint16_t first = c.u16(0xA);
		for (c.u16(0x10) = first; c.u16(0x10) < count; c.u16(0x10) = save_.ships[c.u16(0x10)].memory.u16(0x6)) {
			const ShipMemory& m = save_.ships[c.u16(0x10)].memory;
			if (m.s32(0x18) > 0 && m.u16(0x42) < captain_count(save_)) {
				c.u8(0x14) &= 0xEF;
				if (relink) {
					insert_x(c.u16(0x10), 0xFFFF);
				}
				return true;
			}
		}
		for (c.u16(0x10) = first; c.u16(0x10) < count; c.u16(0x10) = save_.ships[c.u16(0x10)].memory.u16(0x6)) {
			ShipMemory& m = save_.ships[c.u16(0x10)].memory;
			if (m.s32(0x18) > 0) {
				m.u16(0x42) = tick::events_detail::fn_5097c0(save_, true);
				save_.convoys[size_t(convoy)].u8(0x14) &= 0xEF;
				if (relink) {
					insert_x(save_.convoys[size_t(convoy)].u16(0x10), 0xFFFF);
				}
				return true;
			}
		}
		return false;
	}
	for (uint32_t i = c.u16(0xA); i < ship_count(save_); i = save_.ships[i].memory.u16(0x6)) {
		if (i == c.u16(0x10)) {
			continue;
		}
		ShipMemory& m = save_.ships[i].memory;
		if (m.u8(0x0) != c.u8(0x0) || !tick::ship_ai_detail::fn_519c40(save_, int(i))) {
			continue;
		}
		if (c.u16(0x10) < ship_count(save_)) {
			// Hereda la ruta automática del viejo. Si ya tenía una
			// ([+0x132] < [0x6DD72A]), el juego la borra antes con
			// 0x4D4DD0(ruta, 1) sobre la lista de rutas 0x6DD728, que no se
			// reproduce: sus registros (0xDC bytes en *[0x6DD72C]) no están en
			// SaveGame y solo las usan los barcos del jugador.
			ShipMemory& old = save_.ships[c.u16(0x10)].memory;
			m.u16(0x132) = old.u16(0x132);
			old.u16(0x132) = 0xFFFF;
			m.u16(0x134) |= old.u8(0x134) & 1;  // Así en el juego: el bit 0 del estado del viejo.
		}
		c.u16(0x10) = uint16_t(i);
		if (relink) {
			insert_x(uint16_t(i), 0xFFFF);  // 0x50CE50
		}
		return true;
	}
	c.u8(0x14) |= 0x10;
	const uint16_t first = c.u16(0xA);
	for (uint32_t i = first; i < ship_count(save_); i = save_.ships[i].memory.u16(0x6)) {
		const ShipMemory& m = save_.ships[i].memory;
		if (m.u8(0x0) == c.u8(0x0) && m.u16(0x42) < captain_count(save_)) {
			c.u16(0x10) = uint16_t(i);
			if (relink) {
				insert_x(uint16_t(i), 0xFFFF);
			}
			return true;
		}
	}
	for (uint32_t i = first; i < ship_count(save_); i = save_.ships[i].memory.u16(0x6)) {
		if (save_.ships[i].memory.u8(0x0) == c.u8(0x0)) {
			c.u16(0x10) = uint16_t(i);
			if (relink) {
				insert_x(uint16_t(i), 0xFFFF);
			}
			return false;
		}
	}
	c.u16(0x10) = first;
	c.u8(0x0) = save_.ships[first].memory.u8(0x0);
	c.u32(0x18) |= 0x400;
	return false;
}

// 0x509F80 (objeto de los barcos): saca el barco de su convoy, si está en
// los estados 0 a 5, 0x0F o 0x12. En puerto (0, 1, 4 y 5) vuelve a la lista
// de barcos en puerto, atracado en la ciudad del convoy con su estado (el 5
// pasa al 0); saliendo o entrando (2 y 3), a esa lista entrando en el puerto
// (el que lo guía, con el estado y el destino del convoy); en el mar, a la
// lista del mar (y a la ordenada por x, parado); los piratas buscan otro guía
// (0x5017F0). Si sale el que lo guía, salen también todos los demás.
void Sailing::fn_509f80(uint16_t ship) {
	GlobalMemory& g = save_.globals;
	bool all = false;  // [esp+0x10]: sale el que guía y con él todos.
	if (ship >= ship_count(save_)) {
		return;
	}
	ShipMemory& m = save_.ships[ship].memory;
	if (m.u16(0x8) >= convoy_count(save_)) {
		return;
	}
	const uint16_t convoy = m.u16(0x8);
	GameMemory<0x3C>& c = save_.convoys[convoy];
	const uint16_t status = c.u16(0x12);
	if (!(status <= 5 || status == 0xF || status == 0x12)) {
		return;  // Solo en las pruebas sintéticas.
	}
	for (uint16_t* link = &c.u16(0xA); *link < ship_count(save_); link = &save_.ships[*link].memory.u16(0x6)) {
		if (*link == ship) {
			*link = m.u16(0x6);
			break;
		}
	}
	m.u16(0x3C) |= 0x1068;
	m.u16(0x8) = 0xFFFF;
	if (m.u16(0x42) >= captain_count(save_)) {
		m.u8(0x15C) = 0;  // Sin capitán no hay ataque pirata.
	}
	switch (c.u16(0x12)) {
	case 0: case 1: case 4:
		m.u16(0x134) = c.u16(0x12);
		m.u16(0x138) = 0;
		m.u8(0x38) = m.u8(0x37) = m.u8(0x39) = c.u8(0x1C);
		m.u16(0x6) = g.u16(kDockedHead);
		g.u16(kDockedHead) = ship;
		return;
	case 5:
		// Solo en las pruebas sintéticas.
		m.u16(0x134) = 0;
		m.u16(0x138) = 0;
		m.u8(0x38) = m.u8(0x37) = m.u8(0x39) = c.u8(0x1C);
		m.u16(0x6) = g.u16(kDockedHead);
		g.u16(kDockedHead) = ship;
		return;
	case 2: case 3:
		// Solo en las pruebas sintéticas.
		if (ship == c.u16(0x10)) {
			m.u16(0x134) = c.u16(0x12);
			m.u8(0x37) = m.u8(0x39) = c.u8(0x1C);
			m.u8(0x38) = c.u8(0x1F);
			all = true;
		} else {
			m.u16(0x134) = 3;
			if (c.u16(0x12) == 3) {
				m.u16(0x138) = c.u16(0x16);
			} else if (c.u16(0x16) < 0x40) {
				m.u16(0x138) = uint16_t(0x40 - c.u16(0x16));
			} else {
				m.u16(0x138) = 0;
			}
			m.u8(0x38) = m.u8(0x37) = m.u8(0x39) = c.u8(0x1C);
		}
		m.u16(0x6) = g.u16(kDockedHead);
		g.u16(kDockedHead) = ship;
		break;
	case 0xF:
		// Solo en las pruebas sintéticas.
		if (ship == c.u16(0x10)) {
			all = true;
		} else {
			insert_x(ship, c.u16(0x10));  // 0x50CE50
			m.s32(0x30) = 0;
			m.s32(0x28) = 0;
			m.s32(0x2C) = 0;
		}
		m.u16(0x6) = g.u16(kSailingHead);
		g.u16(kSailingHead) = ship;
		m.u16(0x134) = 0xF;
		m.u8(0x39) = 0xFF;
		break;
	case 0x12:
		// Los piratas; solo en las pruebas sintéticas, con convoyes de comerciantes.
		fn_5017f0(convoy);
		break;
	}
	c.u32(0x18) |= 0x4000;
	if (all) {
		while (c.u16(0xA) < ship_count(save_)) {
			fn_509f80(c.u16(0xA));
		}
	}
}

// 0x50A580 (objeto de los barcos): deshace el convoy. Si es de un
// comerciante con barcos de otros y ha llegado a su destino ([+0x1C] =
// [+0x1F]) desde otra ciudad, cada uno de los otros comerciantes paga al
// dueño 10 · ⌊(⌊√Σ⌋ · ⌊√(20 · viaje)⌋ + 99) / 100⌋, con Σ lo que vale cada
// uno de sus barcos ([+0x114], 100 como poco) y el tiempo de viaje entre las
// dos ciudades (0x531B70), y el dueño gana reputación (0x4F9050) por los
// barcos del último que paga; si no ha llegado, la pierde por todos los
// barcos ajenos. Saca los barcos (0x509F80) y borra el convoy (0x50A3D0).
void Sailing::fn_50a580(uint16_t convoy) {
	GlobalMemory& g = save_.globals;
	if (convoy >= convoy_count(save_)) {
		return;  // Sin comprobar con el oráculo.
	}
	GameMemory<0x3C>& c = save_.convoys[convoy];
	uint32_t foreign = 0;  // [esp+0x10]: barcos ajenos.
	// 0x51A850 y 0x51A880 crean y liberan el mensaje; 0x494A80 (si existe el
	// objeto de interfaz *0x6CE6D0) solo es interfaz.
	if (c.u16(0x12) == 6) {
		// Convoy en la lista de su ciudad ([+0x820]): vuelve a la de los que
		// existen, atracado. Solo en las pruebas sintéticas.
		uint16_t* link = &save_.towns[c.u16(0x1C)].memory.u16(0x820);
		while (*link != convoy) {
			if (*link >= convoy_count(save_)) {
				return;  // No está en la lista: el juego no lo deshace.
			}
			link = &save_.convoys[*link].u16(0x8);
		}
		*link = c.u16(0x8);
		c.u16(0x8) = g.u16(kConvoyHead);
		g.u16(kConvoyHead) = convoy;
		c.u16(0x12) = 0;
		c.u16(0x16) = 0;
		for (uint32_t i = c.u16(0xA); i < ship_count(save_); i = save_.ships[i].memory.u16(0x6)) {
			ShipMemory& m = save_.ships[i].memory;
			m.u16(0x3C) |= 0x20;
			m.u16(0x134) = 0;
			m.u16(0x138) = 0;
		}
	}
	if (uint16_t(c.u8(0x0)) >= merchant_count(save_) || (c.u8(0x14) & 2)) {
		// Piratas, o todos del mismo dueño: solo salen los barcos. Solo en
		// las pruebas sintéticas.
		while (c.u16(0xA) < ship_count(save_)) {
			fn_509f80(c.u16(0xA));
		}
	} else if (const uint16_t town = c.u16(0x1C);
		town < town_count(save_) && town == uint16_t(c.u8(0x1F)) && c.u8(0x1E) != c.u8(0x1F)) {
		if (c.u8(0x1E) >= town_count(save_)) {
			c.u8(0x1E) = 0;  // Solo en las pruebas sintéticas.
		}
		const uint8_t days = tick::ship_trade_detail::fn_531b70(save_, c.u8(0x1E), town);
		const uint32_t distance = uint32_t(tick::ftol(std::sqrt(double(int32_t(uint32_t(days) * 20)))));
		uint32_t total = 0;  // [esp+0x14]: lo que cobra el dueño.
		// El [+0x15E] del que guía, el rango y el nombre del dueño solo van al
		// mensaje (0x4D6530, que no se traduce).
		while (c.u16(0xA) < ship_count(save_)) {
			const uint16_t first = c.u16(0xA);
			const uint8_t owner = save_.ships[first].memory.u8(0x0);
			if (owner == c.u8(0x0) || uint16_t(owner) >= merchant_count(save_)) {
				fn_509f80(first);
				continue;
			}
			// Salen todos los barcos de ese comerciante y paga por ellos.
			uint32_t fee = 0;
			foreign = 0;
			for (uint32_t i = first; i < ship_count(save_);) {
				const ShipMemory& m = save_.ships[i].memory;
				if (m.u8(0x0) != owner) {
					i = m.u16(0x6);
					continue;
				}
				fee += m.s32(0x114) < 0x64 ? 0x64 : uint32_t(m.s32(0x114));
				++foreign;
				fn_509f80(uint16_t(i));
				i = c.u16(0xA);
			}
			if (fee != 0) {
				const uint32_t root = uint32_t(tick::ftol(std::sqrt(double(fee))));
				const uint32_t x = root * distance + 0x63;
				fee = uint32_t((uint64_t(x) * 0x51EB851Fu) >> 37) * 10;  // x / 100 sin signo
			}
			MerchantMemory& payer = save_.merchants[owner].memory;
			payer.s32(0x0) = sub(payer.s32(0x0), fee);
			payer.s32(0x4BC) = add(payer.s32(0x4BC), fee);
			total += fee;
			// Si el que paga es un jugador humano ([+0x8] = 0), mensaje 0x32 (0x4D6530).
		}
		if (uint16_t(c.u8(0x0)) < merchant_count(save_)) {
			fn_4f9050(save_, c.u8(0x0), foreign, 1);
			MerchantMemory& leader = save_.merchants[c.u8(0x0)].memory;
			leader.s32(0x0) = add(leader.s32(0x0), total);
			leader.s32(0x4C8) = add(leader.s32(0x4C8), total);
			// Si el dueño es un jugador humano, mensaje 0x31 (0x4D6530).
		}
	} else {
		while (c.u16(0xA) < ship_count(save_)) {
			const uint16_t first = c.u16(0xA);
			if (save_.ships[first].memory.u8(0x0) != c.u8(0x0)) {
				++foreign;
			}
			fn_509f80(first);
		}
		if (uint16_t(c.u8(0x0)) < merchant_count(save_)) {
			fn_4f9050(save_, c.u8(0x0), foreign, 0);
		}
	}
	fn_50a3d0(convoy);
}

// 0x50A3D0 (objeto de los barcos): borra el convoy: lo saca de la lista de
// los que existen y lo pone el primero de la de libres (si no estaba ya),
// con el estado 0xFF.
void Sailing::fn_50a3d0(uint16_t convoy) {
	GlobalMemory& g = save_.globals;
	GameMemory<0x3C>& c = save_.convoys[convoy];
	// 0x494A80 (si existe el objeto de interfaz *0x6CE6D0) solo es interfaz.
	uint16_t ship = c.u16(0xA);
	c.u16(0xA) = 0xFFFF;
	while (ship < ship_count(save_)) {
		// Los barcos que quedan en el convoy se borran (0x507FB0), lo que no
		// se reproduce: en las 12 partidas el convoy llega siempre vacío.
		ship = save_.ships[ship].memory.u16(0x6);
	}
	for (uint16_t* link = &g.u16(kConvoyHead); *link < convoy_count(save_); link = &save_.convoys[*link].u16(0x8)) {
		if (*link == convoy) {
			*link = c.u16(0x8);
			break;
		}
	}
	const uint16_t free = g.u16(kConvoyFree);
	for (uint32_t i = free; i < convoy_count(save_); i = save_.convoys[i].u16(0x8)) {
		if (i == convoy) {
			c.u16(0x12) = 0xFF;  // Ya estaba libre. Solo en las pruebas sintéticas.
			return;
		}
	}
	c.u16(0x8) = free;
	g.u16(kConvoyFree) = convoy;
	c.u16(0x12) = 0xFF;
}

}  // namespace p3::data
