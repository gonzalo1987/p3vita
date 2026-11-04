// Convoyes en el mar: un tick (0x502110) y lo que cuelga de él (0x50A240,
// 0x501E10, 0x5029F0, 0x505C30 y 0x517260). Ver docs/formats/navigation.md §9
// y la cabecera de convoys.cpp.
//
// Un convoy navega con el barco que lo guía ([+0x10]): solo ese barco tiene
// ruta, se mueve como un barco suelto (0x516CC0, §4) y los demás copian su
// posición, su velocidad que se muestra, su llegada prevista y su rumbo.
// Campos del convoy que se usan aquí (además de los de convoys.cpp):
//   [+0x2]  u16 evento de la cola (*0x6DD73C) del convoy; con el tipo 0x34,
//           [+0x15] ≥ 7 y [+0x16] = el convoy, apunta los barcos que pierde.
//   [+0xC]  u32 velocidad del más lento (0x501BE0): la de todo el convoy.
//   [+0x18] u32 avisos a la interfaz: 0x80 sin ruta o parado, 0x8 al llegar.
//   [+0x20] s32 dinero que se lleva un convoy de la IA que sale del mapa (0x5029F0).
//   [+0x24] u16 0xFFFF en los convoyes que crea 0x501680; con < 0x7FFF, el
//           convoy que acaba su ruta en la salida del oeste del mapa sale de él.
//
// Comprobado con el oráculo (tools/oracle/town_oracle.py --barcos) en las 12
// partidas y, para las ramas que no salen en ellas (marcadas «solo en las
// pruebas sintéticas»), con llamadas a 0x502110 del juego sobre convoyes que
// navegan cambiados al azar: barcos sin casco, sin ruta, al final de la ruta
// ante un puerto cerrado o atacado, en el mar o en la salida del oeste, de un
// jugador humano o de un pirata (agents/sea/synth_sea.py). Lo marcado «Sin
// comprobar con el oráculo» no ha salido ni en esas pruebas. Los convoyes de
// los piratas no se mueven en el oráculo (ver convoys.cpp).

#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kConvoyCount = global::kShips + 0xF6;  // u16 convoyes reservados
// Cola de eventos con fecha (objeto 0x6DD73C; SaveGame::events).
constexpr uint32_t kEventCount = 0x6DD73C + 0xC;  // u16 registros reservados

// Salida del oeste del mapa (0x502900): x < 0x82 y 0x578 < y < 0x708.
constexpr int16_t kExitX = 0x82;
constexpr int16_t kExitTop = 0x578;
constexpr int16_t kExitBottom = 0x708;

int32_t sub(int32_t a, int32_t b) {
	return int32_t(uint32_t(a) - uint32_t(b));
}

uint32_t ship_count(const SaveGame& save) {
	return save.globals.u16(global::kShipCount);
}

uint32_t merchant_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldMerchantCount);
}

uint32_t town_count(const SaveGame& save) {
	return save.globals.u32(global::kWorldTownCount);
}

// El comerciante del juego es de la IA (0x5303C0 y su [+0x8] distinto de 0).
bool ai_merchant(const SaveGame& save, uint32_t merchant) {
	return save.merchants[merchant].kind() != 0;
}

}  // namespace

namespace convoys_sea_detail {

// 0x517260 (cdecl): la ciudad de la partida en el punto (x, y) del mapa
// (0x446D50); −1 si no hay ninguna (0x446D50 da 0x28).
int fn_517260(const Sailing& sailing, uint16_t x, uint16_t y) {
	return sailing.town_at({int16_t(x), int16_t(y)});
}

}  // namespace convoys_sea_detail

// 0x502110 (thiscall sobre el convoy; `every_32`, 1 cada 32 ticks y 0 en los
// demás): un tick del convoy en el mar (estado 0x0F). El barco que lo guía
// avanza y los demás copian su posición; los cascos se desgastan, las
// tripulaciones se cansan y los barcos sin casco dejan el convoy. Si el que
// lo guía llega a un punto de su ruta, el convoy pone rumbo al siguiente; al
// final de la ruta, entra en el puerto de la ciudad de ese punto (estado 3),
// se queda fuera si está cerrado o sale del mapa por el oeste (0x5029F0).
// Devuelve false si el convoy deja de navegar (se queda sin guía, entra en
// el puerto o sale del mapa).
bool Sailing::fn_502110(int convoy, uint32_t every_32) {
	using convoys_sea_detail::fn_517260;
	GlobalMemory& g = save_.globals;
	const uint32_t tick = g.u32(global::kWorldTick);
	const uint16_t lead_index = save_.convoys[size_t(convoy)].u16(0x10);
	SavedShip& lead = save_.ships[lead_index];
	// Desgaste del casco en este tick: los barcos de los jugadores humanos
	// pierden 1 cada 4 ticks; los de la IA y los piratas, 1 en los ticks 0x28,
	// 0x50 y 0x78 de cada 128.
	const int32_t human_wear = (tick & 3) == 0 ? 1 : 0;
	const uint32_t t = tick & 0x7F;
	const int32_t other_wear = t == 0x28 || t == 0x50 || t == 0x78 ? 1 : 0;
	const uint16_t old_x = lead.memory.u16(0x1E);
	const uint16_t old_y = lead.memory.u16(0x22);
	if (lead.health() <= 0) {
		// A new guide (convoys_members.cpp). Checked with the oracle: scenario convoy_ship_branches (saves 1, 3, 5, 7).
		fn_5017f0(convoy);
	}
	GameMemory<0x3C>& c = save_.convoys[size_t(convoy)];
	const int32_t x = int32_t(uint32_t(lead.x()) + uint32_t(lead.dx()));
	lead.x() = x;
	const int32_t y = int32_t(uint32_t(lead.y()) + uint32_t(lead.dy()));
	lead.y() = y;
	// El juego compara la parte entera de antes sin signo con la de ahora con signo.
	uint16_t moved = 0;
	if (uint32_t(old_x) != uint32_t(int32_t(int16_t(lead.memory.u16(0x1E)))) ||
		uint32_t(old_y) != uint32_t(int32_t(int16_t(lead.memory.u16(0x22))))) {
		moved = 0x2000;
	}

	// Los barcos del convoy.
	uint16_t* link = &c.u16(0xA);
	while (*link < ship_count(save_)) {
		const uint16_t index = *link;
		SavedShip& s = save_.ships[index];
		s.notices() |= moved;
		s.x() = x;
		s.y() = y;
		// Los de los jugadores humanos y los que no son de un comerciante, solo
		// en las pruebas sintéticas.
		if (s.owner() < merchant_count(save_) && !ai_merchant(save_, s.owner())) {
			s.health() = sub(s.health(), human_wear);
		} else {
			s.health() = sub(s.health(), other_wear);
		}
		if (s.health() >= 1) {
			// Navegando, la tripulación se cansa: un punto cada 32 ticks.
			if (every_32) {
				--s.readiness();
				if ((s.readiness() & 0xFF) == 0xFF) {
					s.notices() |= 0x200;
					if (int8_t(s.readiness() >> 8) < -1) {
						s.readiness() |= 0xFF00;  // Solo en las pruebas sintéticas.
					}
				}
			}
			link = &s.next_in_list();
			continue;
		}
		// Solo en las pruebas sintéticas: sin casco, el barco deja el convoy y
		// vuelve a la lista del mar (0x505C30), donde se hundirá.
		*link = s.next_in_list();
		const uint16_t ship_convoy = s.convoy();
		const uint16_t event = c.u16(0x2);
		if (event < g.u16(kEventCount)) {
			const GameMemory<0x18>& e = save_.events[event];
			if (e.u16(6) == 0x34 && e.u8(0x15) >= 7 && e.u16(0x16) == ship_convoy) {
				// El juego añade al principio de la lista del evento ([+0x8]) un
				// nodo de 8 bytes (0x4F3980, 0x4F3AD0): {siguiente, u8 2 (de
				// 0x672856), u8 dueño, u16 [+0x15E] del barco}. Esas listas no
				// existen en p3vita (la lectura de la partida las descarta): not
				// reproduced (the diet's mission, event 0x34).
			}
		}
		if (index == c.u16(0x10)) {
			unlink_x(c.u16(0x10));
			c.u16(0x10) = kNoIndex;
		}
		const bool pirate = s.status() == kShipPirate;
		s.health() = 0;
		if (!pirate) {
			s.status() = kShipSailing;
		}
		s.notices() |= 0x160;
		s.convoy() = kNoIndex;
		fn_505c30(index);
	}
	if (c.u16(0x10) >= ship_count(save_)) {
		return false;  // Solo en las pruebas sintéticas.
	}

	if (!lead.route) {
		// Solo en las pruebas sintéticas: sin ruta, el convoy se para. Si es de
		// la IA, antes 0x51A2C0(guía, [+0x37] % ciudades) le busca otro
		// destino; el oráculo se la salta (como en Sailing::arrive).
		c.u32(0x18) |= 0x80;
		for (uint32_t i = c.u16(0xA); i < ship_count(save_); i = save_.ships[i].next_in_list()) {
			SavedShip& s = save_.ships[i];
			s.notices() |= 0x80;
			s.dx() = 0;
			s.dy() = 0;
			s.speed() = 0;
		}
		return true;
	}
	const SeaRoute& route = *lead.route;
	const uint16_t point = lead.route_index();
	// El juego no comprueba que [+0x34] esté dentro de la ruta (siempre lo
	// está: al pasar el último punto se le quita).
	if (point >= route.size() || uint16_t(route[point].x) != lead.memory.u16(0x1E) ||
		uint16_t(route[point].y) != lead.memory.u16(0x22)) {
		return true;
	}

	// En el punto de la ruta: hacia el siguiente, desde el centro del píxel.
	lead.dx() = 0;
	lead.dy() = 0;
	lead.memory.u16(0x1C) = 0x7FFF;
	lead.memory.u16(0x20) = 0x7FFF;
	lead.route_index() = uint16_t(point + 1);
	if (int32_t(lead.route_index()) < int32_t(route.size())) {
		tick::ship_ai_detail::fn_501be0(save_, convoy);
		const MapPoint next = (*lead.route)[lead.route_index()];
		const uint32_t speed = save_.convoys[size_t(convoy)].u32(0xC);
		set_ship_course(lead, speed, next);
		// Velocidad que se muestra, como en 0x516CC0 pero sin signo.
		fn_50a240(lead.arrival_tick(), uint32_t(speed << 6) / 115 + 4, lead.convoy(), lead.heading());
		return true;
	}

	// Fin de la ruta.
	const int town = fn_517260(*this, lead.memory.u16(0x1E), lead.memory.u16(0x22));
	if (c.u8(0x0) >= merchant_count(save_)) {
		// Solo en las pruebas sintéticas (solo los piratas): se queda en el mar,
		// en el estado 0x12; si ha llegado a otra ciudad, la apunta en [+0x1C].
		clear_ship_route(lead);
		lead.status() = kShipPirate;
		c.u16(0x12) = kShipPirate;
		if (town >= 0 && uint32_t(town) < town_count(save_) && c.u8(0x1E) != uint32_t(town)) {
			c.u16(0x1C) = uint16_t(town);
			return false;
		}
		c.u16(0x1C) = 0xFF;
		return true;
	}
	if (town < 0 || uint32_t(town) >= town_count(save_)) {
		// Sin ciudad en el último punto.
		if (lead.route_index() != route.size()) {
			// Unreachable: at the end of the route the index is the number of
			// points ([+0x34] = point + 1, and point < number).
			return true;
		}
		// Se queda sin ruta (a diferencia de 0x516840, [+0x2] no cambia).
		lead.set_route(nullptr);
		lead.own_route() = 0;
		lead.route_index() = 0;
		const int16_t lx = int16_t(lead.memory.u16(0x1E));
		const int16_t ly = int16_t(lead.memory.u16(0x22));
		if (c.u16(0x24) < 0x7FFF && lx < kExitX && ly > kExitTop && ly < kExitBottom) {
			fn_5029f0(convoy);
			// Si es del jugador, aviso a la interfaz (0x44F940).
			return false;
		}
		// Solo en las pruebas sintéticas: se para en el mar (y, si es de un
		// jugador humano, the ticker line «Convoy X reaches its destination.»,
		// 0x5029E3: kind 3, the guide).
		for (uint32_t i = c.u16(0xA); i < ship_count(save_); i = save_.ships[i].next_in_list()) {
			SavedShip& s = save_.ships[i];
			s.notices() |= 0x80;
			s.dx() = 0;
			s.dy() = 0;
			s.speed() = 0;
		}
		if (c.u8(0x0) < merchant_count(save_) && save_.merchants[c.u8(0x0)].kind() == 0) {
			send_ticker(save_, c.u8(0x0), 3, c.u16(0x10));
		}
		return true;
	}

	// Ha llegado a la ciudad `town`: el convoy se para.
	clear_ship_route(lead);
	fn_50a240(0, 0, lead.convoy(), lead.heading());
	const uint32_t situation = save_.towns[size_t(town)].situation();
	if (lead.raid()) {
		// Solo en las pruebas sintéticas (ataque a una ciudad: solo los piratas).
		if ((situation & 0x4000000) == 0x4000000) {
			// Antes, si es de un jugador humano, the ticker line (0x502639: kind
			// 3, the guide). El juego devuelve el byte bajo de every_32.
			if (c.u8(0x0) < merchant_count(save_) && save_.merchants[c.u8(0x0)].kind() == 0) {
				send_ticker(save_, c.u8(0x0), 3, c.u16(0x10));
			}
			return (every_32 & 0xFF) != 0;
		}
		c.u8(0x1E) = uint8_t(town);
		c.u16(0x12) = 0x14;
		c.u8(0x1F) = 0xFF;
		c.u16(0x1C) = kNoIndex;
		c.u16(0x1C) = situation & 0x200 ? uint16_t(uint8_t(town)) : 0xFF;
		for (uint32_t i = c.u16(0xA); i < ship_count(save_); i = save_.ships[i].next_in_list()) {
			SavedShip& s = save_.ships[i];
			s.notices() |= 0x28;
			s.town() = c.u8(0x1C);
			s.destination() = 0xFF;
			s.timer() = 0;
			s.status() = 0x14;
		}
		return false;
	}
	const bool closed = (situation & 0x4000200) ||
		((situation & 0x400) && lead.owner() < merchant_count(save_) && (lead.orders() & 1));
	if (!closed) {
		// Entra en el puerto (estado 3 del convoy y de sus barcos).
		c.u16(0x1C) = uint16_t(uint8_t(town));
		c.u16(0x12) = kShipArriving;
		c.u16(0x16) = 0;
		c.u8(0x1F) = uint8_t(town);
		for (uint32_t i = c.u16(0xA); i < ship_count(save_); i = save_.ships[i].next_in_list()) {
			SavedShip& s = save_.ships[i];
			s.notices() |= 0x28;
			s.town() = uint8_t(town);
			s.destination() = uint8_t(town);
			s.timer() = 0;
			s.status() = kShipArriving;
		}
		fn_501e10(convoy, lead_index);
		c.u32(0x18) |= 8;
		// Si es del jugador, aviso a la interfaz (0x44F940).
		return false;
	}
	// Solo en las pruebas sintéticas: el puerto está cerrado (bloqueo o asedio,
	// o 0x400 y ruta automática); se queda fuera, sin ruta.
	if (c.u8(0x0) >= merchant_count(save_)) {
		// Unreachable: the pirates' convoys returned before, at the end of the
		// route (above).
		set_destination(lead, 0xFF, 0x220, 0x368);
	} else if (!ai_merchant(save_, c.u8(0x0)) && (c.u16(0x14) & 2)) {
		// Jugador humano, todos sus barcos: con ruta automática, 0x4D5900 (el
		// oráculo se la salta); si iba a esta ciudad, the ticker line (0x50285C:
		// kind 3, the guide) y pierde las órdenes de ruta.
		if (lead.destination() == uint32_t(town)) {
			send_ticker(save_, c.u8(0x0), 3, c.u16(0x10));
			lead.orders() &= 0xFC;
		}
	} else {
		// La IA busca otro destino con 0x51A2C0(guía, ciudad): el oráculo se la salta.
	}
	c.u8(0x1E) = uint8_t(town);
	for (uint32_t i = c.u16(0xA); i < ship_count(save_); i = save_.ships[i].next_in_list()) {
		save_.ships[i].origin() = c.u8(0x1E);
	}
	c.u16(0x1C) = 0xFF;
	// Si es del jugador, aviso a la interfaz (0x44F940).
	return true;
}

// 0x505C30 (objeto de los barcos): el barco de un convoy pasa a ser el
// primero de la lista del mar, como un barco suelto: entra en la lista
// ordenada por x, recalcula su bodega (0x5182B0) y deja la ciudad.
void Sailing::fn_505c30(uint16_t ship) {
	insert_x(ship, save_.sailing_head());
	SavedShip& s = save_.ships[ship];
	recalculate_cargo(s);
	s.next_in_list() = save_.sailing_head();
	s.town() = 0xFF;
	save_.sailing_head() = ship;
}

// 0x50A240 (objeto de los barcos): los barcos del convoy reciben la velocidad
// que se muestra ([+0x30]), la llegada prevista ([+0x48]) y el rumbo ([+0x1]).
void Sailing::fn_50a240(uint32_t arrival, uint32_t speed, uint32_t convoy, uint8_t heading) {
	if (convoy >= save_.globals.u16(kConvoyCount)) {
		return;
	}
	uint32_t i = save_.convoys[convoy].u16(0xA);
	if (i >= ship_count(save_)) {
		return;
	}
	do {
		SavedShip& s = save_.ships[i];
		i = s.next_in_list();
		s.speed() = int32_t(speed);
		s.arrival_tick() = arrival;
		s.heading() = heading;
	} while (i < ship_count(save_));
}

// 0x501E10 (thiscall sobre el convoy, con el barco que lo guía): con [+0x14]
// & 7 = 1 recorre los barcos del convoy y pide el comerciante de los que no
// son del dueño del guía, pero no cambia nada.
void Sailing::fn_501e10(int convoy, int ship) {
	(void)convoy;
	(void)ship;
}

// 0x5029F0 (thiscall sobre el convoy): el convoy sale del mapa por el oeste.
// Apunta en la cola un evento de tipo 0x12 para ahora con el convoy, sale de
// la lista ordenada por x y sus barcos pasan, fuera del mapa (30000, 30000),
// al estado 0x13 (el convoy también). Si es de la IA, se lleva la cuarta
// parte del dinero del comerciante (hasta 100000) si tiene más de 40000, o
// apunta 10000 si no.
void Sailing::fn_5029f0(int convoy) {
	GlobalMemory& g = save_.globals;
	GameMemory<0x3C>& c = save_.convoys[size_t(convoy)];
	GameMemory<0x18>& e = save_.events[tick::events_detail::fn_4d8cf0(save_, g.u32(global::kWorldTick))];
	e.u16(0x6) = 0x12;
	e.u16(0xA) = kNoIndex;
	e.u16(0x8) = save_.ships[c.u16(0x10)].convoy();
	e.u32(0xC) = g.u32(global::kWorldTick);
	c.u16(0x12) = 0x13;
	unlink_x(c.u16(0x10));
	// Después, 0x4885A0 sobre el objeto de la interfaz *0x6CE6D0 solo lee.
	if (c.u8(0x0) < merchant_count(save_) && ai_merchant(save_, c.u8(0x0))) {
		SavedMerchant& m = save_.merchants[c.u8(0x0)];
		if (m.money() > 40000) {
			c.s32(0x20) = m.money() >> 2;
			if (c.s32(0x20) > 100000) {
				c.s32(0x20) = 100000;
			}
			m.money() = sub(m.money(), c.s32(0x20));
		} else {
			c.s32(0x20) = 10000;  // Solo en las pruebas sintéticas.
		}
	}
	for (uint32_t i = c.u16(0xA); i < ship_count(save_); i = save_.ships[i].next_in_list()) {
		SavedShip& s = save_.ships[i];
		s.memory.u16(0x1E) = 30000;
		s.memory.u16(0x22) = 30000;
		s.status() = 0x13;
	}
}

}  // namespace p3::data
