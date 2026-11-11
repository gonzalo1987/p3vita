// Convoyes: el tercer recorrido de la actualización de barcos (0x506720),
// sobre la lista de convoyes que empieza en el [+0xEC] del objeto de los
// barcos. Ver docs/formats/navigation.md §9.
//
// Un convoy es un registro de 0x3C bytes (SaveGame::convoys, *0x6DD7A8,
// tantos como [0x6DD896]); los crea 0x509B90 (town_tick/ship_ai.cpp):
//   [+0x0] u8 dueño del que lo guía; [+0x1] u8 tipos de sus barcos (OR);
//   [+0x2] u16; [+0x4] u32 capacidad de los barcos del dueño; [+0x8] u16
//   siguiente en su lista (libres o existentes); [+0xA] u16 primer barco (los
//   demás por el [+0x6] de cada barco); [+0xC] u32 velocidad del más lento;
//   [+0x10] u16 barco que lo guía (0xFFFF: ninguno); [+0x12] u16 estado (el de
//   los barcos); [+0x14] u16 (bit 1: todos del mismo dueño); [+0x16] u16 lo
//   que espera ([+0x138] de los barcos); [+0x18] u32 avisos; [+0x1C] u16
//   ciudad; [+0x1E] u8 origen; [+0x1F] u8 destino; [+0x20] s32 dinero que
//   cobra el dueño al llegar; [+0x24]..[+0x3A] lo pone 0x501680 al crearlo.
//
// Los convoyes de los piratas (el dueño no es un comerciante) se quedan
// quietos: sus ataques acaban en batallas, que no se reproducen (el oráculo
// tampoco las ejecuta).

#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"
#include "p3data/trade_routes.hpp"

namespace p3::data {

namespace {

// En el objeto de los barcos (0x6DD7A0).
constexpr uint32_t kConvoyHead = global::kShips + 0xEC;  // u16 primer convoy que existe
constexpr uint32_t kPlayerShipsInPort = global::kShips + 0x100;
constexpr uint32_t kPlayerConvoysInPort = global::kShips + 0x104;  // Convoyes del jugador en puerto en este tick.
constexpr uint32_t kPlayerConvoysAtSea = global::kShips + 0x106;
constexpr uint32_t kRouteCount = 0x6DD728 + 2;  // u16 rutas automáticas del jugador (objeto 0x6DD728)

// 0x519B50: un barco de la IA de un convoy que zarpa de una ciudad que no es
// su destino: como en 0x518B80, la ciudad a la que va a comerciar es la
// parada siguiente; reserva lo que falta (0x5193F0 si vuelve a su base,
// 0x5196B0 si no) y compra para ella (0x519920). No sale en las 12 partidas.
void fn_519b50(SaveGame& save, int ship) {
	ShipMemory& m = save.ships[size_t(ship)].memory;
	const bool home = m.u8(0x38) == m.u8(0x13A);
	m.u8(0x13B) = m.u8(0x38);
	if (home) {
		tick::fn_5193f0(save, ship);
	} else {
		tick::fn_5196b0(save, ship);
	}
	tick::fn_519920(save, ship, m.u8(0x13B));
}

}  // namespace

// Tercer recorrido de 0x506720: cada convoy de la lista según su estado
// [+0x12] (tabla 0x507D14). Antes, el convoy sin barcos se borra (0x50A3D0) y
// el que se ha quedado sin guía se deshace (0x50A580). Cuando un convoy sale
// de la lista, el recorrido sigue por el mismo enlace; en algunos casos
// (0x5017F0 con otro guía, la persecución) el juego vuelve a pasar por el
// mismo convoy en este tick.
void Sailing::update_convoys() {
	GlobalMemory& g = save_.globals;
	const size_t ships = save_.ships.size();
	const bool every_32 = (save_.tick() & 0x1F) == 0;
	const uint32_t player_index = g.u32(global::kPlayer);
	// El enlace del convoy actual: la cabeza de la lista o el [+0x8] del
	// anterior (por índice: las funciones pueden agrandar SaveGame::convoys).
	int previous = -1;
	auto link = [&]() -> uint16_t& {
		return previous < 0 ? g.u16(kConvoyHead) : save_.convoys[size_t(previous)].u16(0x8);
	};
	auto convoy = [&](uint16_t index) -> GameMemory<0x3C>& { return save_.convoys[index]; };
	auto ai = [&](const SavedShip& ship) { return save_.merchants[ship.owner()].kind() != 0; };
	// Un punto más de descanso por tick, hasta 0x700 (como en puerto, pero sin
	// esperar un día desde que atracó).
	auto rest_now = [](SavedShip& ship) {
		++ship.readiness();
		if (int16_t(ship.readiness()) >= 0x700) {
			ship.readiness() = 0x700;
		} else if ((ship.readiness() & 0xFF) == 0) {
			ship.notices() |= 0x200;
		}
	};
	while (link() < save_.convoy_count()) {
		const uint16_t index = link();
		GameMemory<0x3C>& c = convoy(index);
		if (c.u16(0xA) >= ships) {
			// Sin barcos: sale de la lista y se borra.
			link() = c.u16(0x8);
			c.u16(0x8) = kNoIndex;
			fn_50a3d0(index);
			continue;
		}
		const uint16_t leader = c.u16(0x10);
		if (leader >= ships) {
			fn_50a580(index);  // Sin guía: se deshace.
			continue;
		}
		if (c.u8(0x0) >= save_.merchant_count) {
			// Piratas: no se reproducen (ver la cabecera), salvo con
			// set_pirates(true) su estado 0x12 (battle/pirates.cpp; los demás
			// estados no salen en las partidas).
			if (pirates_ && c.u16(0x12) == 0x12) {
				if (pirate_convoy(index, link())) {
					continue;
				}
			}
			previous = index;
			continue;
		}
		switch (c.u16(0x12)) {
		case 0x0: {
			// 0x5071A4: en puerto.
			SavedShip& lead = save_.ships[leader];
			if (player(lead)) {
				++g.u16(kPlayerConvoysInPort);
			}
			if (c.u8(0x14) & 0x10) {
				fn_50a580(index);  // Checked with the oracle: scenario convoy_states (saves 1, 3, 5, 7).
				continue;
			}
			if (ai(lead)) {
				// El que lo guía decide como un barco de la IA atracado.
				tick::fn_518b80(save_, leader, index);
				if (lead.timer() < 0x200) {
					lead.timer() = 0x200;
				}
			} else if (lead.orders() & 1) {
				// The players have no convoys in the 12 saves. Checked with the oracle: scenario
				// convoy_states (saves 1, 3, 5, 7): without trade routes, the route is interrupted.
				if (lead.memory.u16(0x132) < g.u16(kRouteCount) && tick::ship_ai_detail::fn_519c40(save_, leader) &&
					tick::ship_ai_detail::fn_519ba0(save_, leader)) {
					fn_503230(save_, this, int(index));  // The convoy on its trade route (trade_routes.cpp).
				} else {
					// Deja la ruta automática, and the ticker line «X's trade route is
					// interrupted» (0x507283: kind 0x22, the convoy's guide).
					lead.notices() |= 0x800;
					lead.orders() &= 0xFC;
					send_ticker(save_, lead.owner(), 0x22, convoy(index).u16(0x10));
				}
			}
			GameMemory<0x3C>& now = convoy(index);
			if (lead.town() != lead.destination()) {
				// Zarpa hacia su destino: todos esperan (estado 1). Los barcos
				// de la IA que llegan a su destino dejan el convoy; los demás
				// reservan y compran para la parada siguiente.
				lead.origin() = lead.town();
				lead.status() = kShipWaiting;
				int before = -1;  // El enlace del barco: [+0xA] del convoy o el [+0x6] del anterior.
				auto ship_link = [&]() -> uint16_t& {
					return before < 0 ? convoy(index).u16(0xA) : save_.ships[size_t(before)].next_in_list();
				};
				while (ship_link() < ships) {
					const uint16_t i = ship_link();
					SavedShip& ship = save_.ships[i];
					if (player(ship)) {
						++g.u16(kPlayerShipsInPort);
					}
					if (ai(ship)) {
						ship.destination() = convoy(index).u8(0x1F);
						if (ship.destination() != ship.town()) {
							fn_519b50(save_, i);  // Checked with the oracle: scenario convoy_states (saves 1, 3, 5, 7).
						} else {
							fn_509f80(i);  // Sale del convoy: el enlace ya apunta al siguiente.
							continue;
						}
					} else {
						tick::ship_ai_detail::fn_535400(save_, ship.town(), ship.owner());
					}
					before = i;
				}
				tick::ship_ai_detail::fn_501be0(save_, index);
			} else {
				for (uint16_t i = now.u16(0xA); i < ships;) {
					SavedShip& ship = save_.ships[i];
					i = ship.next_in_list();
					rest(ship);
				}
			}
			previous = index;
			break;
		}
		case 0x1: {
			// 0x5073C0: esperando a zarpar. Cuenta atrás en cada barco; cuando
			// no queda ninguno esperando, sale (estado 2) si va a otra ciudad.
			if (player(save_.ships[leader])) {
				++g.u16(kPlayerConvoysInPort);
			}
			c.u16(0x16) = 0;
			for (uint16_t i = c.u16(0xA); i < ships;) {
				SavedShip& ship = save_.ships[i];
				ship.notices() |= 0x20;
				ship.status() = kShipWaiting;
				if (ship.timer() > 0) {
					--ship.timer();
				}
				if (ship.timer() > c.u16(0x16)) {
					c.u16(0x16) = ship.timer();
				}
				rest_now(ship);
				i = ship.next_in_list();
			}
			if (c.u16(0x16) == 0) {
				const uint16_t to_go = uint16_t(c.u8(0x1F) - c.u16(0x1C));
				c.u8(0x1E) = c.u8(0x1C);
				c.u16(0x12) = to_go ? kShipLeaving : kShipDocked;
				for (uint16_t i = c.u16(0xA); i < ships;) {
					SavedShip& ship = save_.ships[i];
					i = ship.next_in_list();
					ship.status() = c.u16(0x12);
				}
			}
			previous = index;
			break;
		}
		case 0x2: {
			// 0x5074DB: saliendo del puerto (64 ticks); después, al mar.
			if (c.u8(0x0) == player_index) {
				++g.u16(kPlayerConvoysInPort);
			}
			const uint16_t t = c.u16(0x16);
			if (t == 0) {
				fn_501eb0(index);  // Si devuelve true, la escena del puerto (0x6135C0).
				fn_503cf0(index);
				convoy(index).u32(0x18) = 0x20;
				previous = index;
				break;
			}
			c.u16(0x16) = uint16_t(t + 1);
			if (t < 0x40) {
				save_.ships[c.u16(0x10)].timer() = c.u16(0x16);
			} else {
				c.u16(0x12) = kShipSailing;
				c.u16(0x16) = 0;
				c.u16(0x1C) = 0xFF;
				c.u32(0x18) = 0xFFFFFFFF;
				insert_x(c.u16(0x10), save_.sailing_head());
				c.u8(0x14) |= 2;
				for (uint16_t i = c.u16(0xA); i < ships;) {
					SavedShip& ship = save_.ships[i];
					i = ship.next_in_list();
					recalculate_cargo(ship);
					ship.notices() |= 0x28;
					ship.status() = kShipSailing;
					ship.town() = 0xFF;
					if (ship.owner() != c.u8(0x0)) {
						c.u8(0x14) &= 0xFD;
					}
				}
				if (c.u8(0x14) & 2) {
					fn_503b50(index);
				}
			}
			previous = index;
			break;
		}
		case 0x3: {
			// 0x5076A0: entrando en el puerto (64 ticks); después atracan todos,
			// el dueño cobra [+0x20] y el convoy sigue (0x5017F0) o se deshace.
			if (c.u8(0x0) == player_index) {
				++g.u16(kPlayerConvoysInPort);
			}
			const uint16_t t = c.u16(0x16);
			if (t >= 0x40) {
				c.u16(0x12) = kShipDocked;
				for (uint16_t i = c.u16(0xA); i < ships;) {
					SavedShip& ship = save_.ships[i];
					dock(ship, i);
					i = ship.next_in_list();
				}
				const uint8_t owner = c.u8(0x0);
				if (owner < save_.merchant_count) {
					if (c.s32(0x20) > 0) {
						int32_t& money = save_.merchants[owner].memory.s32(0x0);
						money = int32_t(uint32_t(money) + uint32_t(c.s32(0x20)));
					}
					// To a human owner whose guide hasn't the bit 1 of [+0x136], the
					// ticker line «the convoy X has docked in Y.» (0x50777C: kind 3,
					// the guide).
					if (save_.merchants[owner].kind() == 0 && !(save_.ships[c.u16(0x10)].memory.u8(0x136) & 1)) {
						send_ticker(save_, owner, 3, c.u16(0x10));
					}
				}
				c.s32(0x20) = 0;
				save_.ships[c.u16(0x10)].timer() = 0;
				c.u16(0x16) = 0;
				const bool stays = fn_5017f0(index);
				const uint8_t mode = convoy(index).u8(0x14);
				if (!stays || !(mode & 2) || (mode & 0x10)) {
					fn_50a580(link());
					continue;
				}
			} else {
				c.u16(0x16) = uint16_t(t + 1);
				if (t == 0) {
					// Antes, la escena del puerto (0x6132C0).
					for (uint16_t i = c.u16(0xA); i < ships;) {
						SavedShip& ship = save_.ships[i];
						i = ship.next_in_list();
						ship.notices() |= 0x1000;
						ship.raid() = 0;
					}
				}
				save_.ships[c.u16(0x10)].timer() = c.u16(0x16);
			}
			previous = index;
			break;
		}
		case 0x4: {
			// 0x5075FC: al astillero de su ciudad: sale de la lista y entra en la de convoyes de la
			// ciudad ([0x820]) con el estado 6. Checked with the oracle: scenario convoy_states
			// (saves 1, 3, 5, 7): with a town and without.
			if (c.u16(0x1C) >= save_.towns.size()) {
				c.u16(0x12) = kShipSailing;
				previous = index;
				break;
			}
			c.u16(0x12) = 6;
			for (uint16_t i = c.u16(0xA); i < ships;) {
				SavedShip& ship = save_.ships[i];
				i = ship.next_in_list();
				ship.notices() |= 0x20;
				ship.status() = 6;
			}
			link() = c.u16(0x8);
			TownMemory& town = save_.towns[c.u16(0x1C)].memory;
			c.u16(0x8) = town.u16(0x820);
			town.u16(0x820) = index;
			break;
		}
		case 0x9:
			fn_503930(index);  // 0x507852. Checked with the oracle: scenario convoy_states (saves 1, 3, 5, 7).
			previous = index;
			break;
		case 0xA:
			// 0x507865. Checked with the oracle: scenario convoy_states (saves 1, 3, 5, 7).
			if (c.u16(0x16) != 0) {
				--c.u16(0x16);
			}
			previous = index;
			break;
		case 0xB:
		case 0x13:
			previous = index;  // 0x507873 y 0x507612: nada.
			break;
		case 0x12:
			// 0x507A65: piratas al acecho; los convoyes de los comerciantes
			// no llegan a este estado. No se reproduce.
			previous = index;
			break;
		case 0xF:
		case 0x16: {
			// 0x50787F: navegando.
			if (c.u8(0x0) == player_index) {
				++g.u16(kPlayerConvoysAtSea);
			}
			if (fn_502110(index, every_32)) {
				const uint16_t lead_index = convoy(index).u16(0x10);
				if (lead_index >= ships) {
					continue;  // El juego vuelve a pasar por este enlace.
				}
				SavedShip& lead = save_.ships[lead_index];
				if (lead.convoy() != link()) {
					fn_5017f0(index);  // Otro guía: vuelve a pasar por este convoy.
					continue;
				}
				if (lead.target() != kNoIndex) {
					if (pursue(lead)) {
						continue;
					}
					if (!lead.route || lead.target() >= ships) {
						lead.dx() = 0;
						lead.dy() = 0;
					}
				}
				sort_x(lead_index);
				previous = index;
				break;
			}
			GameMemory<0x3C>& now = convoy(index);
			const uint16_t lead_index = now.u16(0x10);
			if (lead_index >= ships) {
				continue;
			}
			if (now.u16(0x12) == 0x14) {
				// Raiders at a town (only the pirates): out of the lists; a sea battle against the Hanse fleet when it
				// guards the town (0x50BA80), else (0x50797E) 0x535400 for the convoy's owner and town [+0x1C], and
				// a town battle (the battles' piece 6: the one of the guide's town and owner, 0x6130B0 and
				// 0x60234C, or a new one against the convoy's [+0x1E], 0x611D80 and 0x5FBFE0).
				link() = now.u16(0x8);
				now.u16(0x8) = kNoIndex;
				unlink_x(lead_index);
				if (battles_ && !fn_50ba80(lead_index)) {
					tick::ship_ai_detail::fn_535400(save_, now.u16(0x1C), now.u8(0x0));
					town_attack(lead_index, now.u8(0x1E), true);
				}
				continue;
			}
			// Llega a una ciudad (estado 3): fuera de la lista ordenada por x.
			unlink_x(lead_index);
			for (uint16_t i = now.u16(0xA); i < ships;) {
				SavedShip& ship = save_.ships[i];
				if (ship.memory.u16(0xA) < ships || ship.memory.u16(0xC) < ships) {
					unlink_x(i);
				}
				i = ship.next_in_list();
			}
			previous = index;
			break;
		}
		default:
			// 0x507C7F: en cualquier otro estado, todos los barcos dejan el convoy y se borra.
			// Checked with the oracle: scenario convoy_states (saves 1, 3, 5, 7), with the state 5.
			// 0x509F80 only takes ships out in the states 0..5, 0xF and 0x12: with another state
			// here the game would loop forever; in a game only 5 comes (a convoy in 6 or 0x14 is
			// out of this list).
			do {
				fn_509f80(convoy(index).u16(0xA));
			} while (convoy(index).u16(0xA) < ships);
			fn_50a3d0(link());
			break;
		}
	}
}

}  // namespace p3::data
