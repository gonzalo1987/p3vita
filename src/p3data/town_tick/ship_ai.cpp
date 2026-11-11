// Barcos en puerto: los estados 0, 4 y 5 de la actualización de barcos (0x506720) y las
// decisiones de los barcos de la IA (0x518B80): adónde ir, cuándo zarpar, si
// reparar, cuántos marineros llevar y si ir en convoy.
//
// Campos del barco que se usan aquí (ShipMemory, ver savegame.hpp):
//   [+0x13A] u8  ciudad base de la ruta de la IA (a la que vuelve)
//   [+0x13B] u8  ciudad hacia la que va a comerciar; [+0x38] es la parada siguiente
//   [+0x136] u8  órdenes: bit 0 ruta automática (jugador; 2 en el astillero),
//                0x10 lleva marineros de más (contrata y arma), 0x20 se queda quieto
//   [+0x3C] u16 avisos a la interfaz (0x20 cambio de estado, 0x200 descanso...)
//   [+0x3E] u16 descanso de la tripulación; [+0x40] u16 marineros
// Del comerciante, el u8 [+0x547]: los ticks de más que espera en puerto un barco
// de la IA (y cuánto casco perdido aguanta antes de ir al astillero).
//
// Convoyes (SaveGame::convoys, 0x3C bytes; los crea 0x509B90):
//   [+0x0] u8 dueño del que lo guía; [+0x1] u8 tipos de sus barcos (OR);
//   [+0x2] u16; [+0x4] u32 capacidad de los barcos del dueño; [+0x8] u16
//   siguiente en su lista (libres o existentes); [+0xA] u16 primer barco (los
//   demás por el [+0x6] de cada barco); [+0xC] u32 velocidad del más lento;
//   [+0x10] u16 barco que lo guía (0xFFFF: ninguno); [+0x12] u16 estado (el de
//   los barcos); [+0x14] u16 (bit 1: todos del mismo dueño); [+0x16] u16 lo
//   que espera ([+0x138] de los barcos); [+0x18] u32 avisos; [+0x1C] u16
//   ciudad; [+0x1E] u8 origen; [+0x1F] u8 destino; [+0x20]..[+0x3A] lo pone
//   0x501680 al crearlo.
//
// Comprobado con el oráculo en las 12 partidas y, para las ramas que no salen
// en ellas (marcadas «solo en las pruebas sintéticas»), con llamadas al código
// del juego sobre barcos, convoyes y ciudades cambiados al azar
// (agents/ship_ai/synth_ai.py).

#include <cstring>

#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"
#include "p3data/trade_routes.hpp"

namespace p3::data::tick {

namespace {

// En el objeto de los barcos (0x6DD7A0).
constexpr uint32_t kDockedHead = global::kShips + 0xE8;  // u16 cabeza de la lista de barcos en puerto
constexpr uint32_t kConvoyFree = global::kShips + 0xE4;  // u16 primer convoy libre
constexpr uint32_t kConvoyHead = global::kShips + 0xEC;  // u16 primer convoy que existe
constexpr uint32_t kConvoyCount = global::kShips + 0xF6;  // u16 convoyes reservados
constexpr uint32_t kPlayerInPort = global::kShips + 0x100;  // u16 barcos del jugador en puerto en este tick
constexpr uint32_t kSellFactor = 0x6DE3D8 + 0x64;  // float: factor de venta con poco almacén (dificultad)
constexpr uint32_t kPlayerSlots = 0x6DE588 + 0xA00;  // u8 × 8: comerciante de cada hueco (SaveGame::player_towns)

constexpr uint8_t kMinSailors[4] = {5, 8, 10, 12};  // 0x673660, por tipo de barco
constexpr uint8_t kSailorsWanted[4] = {10, 16, 30, 24};  // 0x673664
constexpr int32_t kSailorsPerLevel[4] = {3, 5, 8, 10};  // 0x66E030 + 0x18 · tipo
constexpr uint16_t kBaseSpeed[4] = {635, 693, 520, 578};  // 0x67366C
// 0x672C14: 1 si la mercancía se mide en barriles, 0 si en cargas.
constexpr uint8_t kBarrelWare[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
// 0x673684: peso de cada mercancía al elegir adónde ir a comerciar (0x518FF0).
constexpr uint8_t kWareWeight[24] = {16, 9, 9, 9, 16, 9, 9, 9, 9, 9, 9, 11, 11, 9, 11, 8, 8, 9, 9, 8, 0, 0, 0, 0};

// Constantes de .rdata en float.
constexpr float k1_5 = 0x1.8p+0f;  // 0x6739EC
constexpr float k1_0 = 0x1.0p+0f;  // 0x6739F0 y 0x673A0C
constexpr float k0_8 = 0x1.99999ap-1f;  // 0x6739F4
constexpr float k0_6 = 0x1.333334p-1f;  // 0x6739F8
constexpr float k1_4 = 0x1.666666p+0f;  // 0x673A08
constexpr float k0_0 = 0.0f;  // 0x66BC6C

int32_t sub(int32_t a, int32_t b) {
	return int32_t(uint32_t(a) - uint32_t(b));
}

int32_t add(int32_t a, int32_t b) {
	return int32_t(uint32_t(a) + uint32_t(b));
}

TownMemory& town_of(SaveGame& save, uint32_t town) {
	return save.towns[town].memory;
}

uint32_t town_count(const SaveGame& save) {
	return save.globals.u32(global::kWorldTownCount);
}

uint16_t ship_count(const SaveGame& save) {
	return save.globals.u16(global::kShipCount);
}

uint16_t convoy_count(const SaveGame& save) {
	return save.globals.u16(kConvoyCount);
}

// 0x5303C0: el comerciante dueño del barco.
MerchantMemory& owner_of(SaveGame& save, const ShipMemory& m) {
	return save.merchants[m.u8(0x0)].memory;
}

}  // namespace

namespace ship_ai_detail {

// 0x531B70, 0x516AB0 y 0x52F030: las de ship_trade.cpp (comprobadas allí).
using ship_trade_detail::fn_516ab0;
using ship_trade_detail::fn_52f030;
using ship_trade_detail::fn_531b70;

// 0x518D60: ciudades a las que puede ir el barco a comerciar, un bit por
// índice: con mercado (0x100 de [0x76C]) y sin asedio ni bloqueo ([0x2C8] &
// 0x4000E00; los barcos grandes, tipos 2 y 3, tampoco donde está el bit 1),
// sin la suya.
uint32_t fn_518d60(const SaveGame& save, int ship) {
	const ShipMemory& m = save.ships[size_t(ship)].memory;
	const uint32_t closed = (m.u8(0xE) & 2) ? 0x4000E01 : 0x4000E00;
	const uint32_t n = town_count(save);
	uint32_t mask = 0;
	uint32_t bit = 1;
	for (uint32_t i = 0; i < n; ++i) {
		const TownMemory& t = save.towns[i].memory;
		if (!(t.u32(0x2C8) & closed) && (t.u32(0x76C) & 0x100)) {
			mask |= bit;
		}
		bit += bit;
	}
	return ~(1u << (m.u8(0x39) & 31)) & mask;
}

// 0x517770: avisa a la interfaz del barco (y de su convoy).
void fn_517770(SaveGame& save, int ship, uint32_t flags) {
	ShipMemory& m = save.ships[size_t(ship)].memory;
	m.u16(0x3C) |= uint16_t(flags);
	const uint16_t convoy = m.u16(0x8);
	if (convoy < convoy_count(save)) {
		// Solo en las pruebas sintéticas: no sale en las 12 partidas.
		save.convoys[convoy].u32(0x18) |= flags & 0xFFFF;
	}
}

// 0x518E00: el barco no puede zarpar. Va al astillero si tiene menos de un
// cuarto del casco; espera si la tripulación está cansada; si no, contrata
// los marineros que le falten (de los parados de la ciudad, [0x2E4]) o
// despide los que le sobren.
void fn_518e00(SaveGame& save, int ship) {
	ShipMemory& m = save.ships[size_t(ship)].memory;
	if ((m.s32(0x14) >> 2) > m.s32(0x18)) {
		// Solo en las pruebas sintéticas: no sale en las 12 partidas.
		m.u16(0x134) = 4;
		m.u16(0x138) = 0;
		return;
	}
	if (m.s8(0x3F) < 1) {
		m.u16(0x134) = 1;
		m.u16(0x138) = uint16_t(owner_of(save, m).u8(0x547) * 3);
		return;
	}
	const uint8_t min = kMinSailors[m.u8(0xE) & 3];
	const uint16_t sailors = m.u16(0x40);
	if (sailors < min) {
		// Solo en las pruebas sintéticas: no sale en las 12 partidas.
		TownMemory& t = town_of(save, m.u8(0x39));
		int32_t hire = int32_t(min) - int32_t(sailors);
		if (!(t.s32(0x2E4) > hire)) {
			hire = t.s32(0x2E4) - 1;
		}
		t.s32(0x2D4) = sub(t.s32(0x2D4), hire);  // habitantes
		t.s32(0x2E4) = sub(t.s32(0x2E4), hire);  // parados
		m.u16(0x40) = uint16_t(m.u16(0x40) + hire);
		fn_517770(save, ship, 2);
		const uint16_t convoy = m.u16(0x8);
		if (convoy < convoy_count(save)) {
			save.convoys[convoy].u32(0x18) |= 2;
		}
		return;
	}
	if ((m.u8(0x136) & 0x10) == 0x10 || sailors <= min) {
		return;
	}
	// Solo en las pruebas sintéticas: no sale en las 12 partidas.
	const int32_t extra = int32_t(sailors) - int32_t(min);
	m.u16(0x40) = uint16_t(sailors - extra);
	fn_517770(save, ship, 2);
	TownMemory& t = town_of(save, m.u8(0x39));
	t.s32(0x2D4) = add(t.s32(0x2D4), extra);
	t.s32(0x2E4) = add(t.s32(0x2E4), extra);
}

// 0x518FF0: elige la ciudad a la que ir a comerciar desde la base [+0x13A]:
// la de `mask` con más margen esperado (0x52F030) en las mercancías que
// valen la pena en un sentido o en el otro (al menos 5 barriles o 3 cargas),
// pesadas por kWareWeight. La pone en [+0x13B] y [+0x38] (o la ciudad en la
// que está, si ninguna da margen).
void fn_518ff0(SaveGame& save, int ship, uint32_t mask) {
	ShipMemory& m = save.ships[size_t(ship)].memory;
	uint32_t best_town = 0xFFFF;
	float best = 0.0f;
	uint32_t bit = 1;
	for (uint32_t t = 0; t < town_count(save); ++t, bit += bit) {
		const uint32_t base = m.u8(0x13A);
		if (t == base || (bit & mask) != bit) {
			continue;
		}
		const int32_t days = fn_516ab0(save, ship, t);
		float sum = 0.0f;
		for (uint32_t w = 0; w < 24; ++w) {
			float f;
			int32_t amount = fn_52f030(save, m.u8(0x13A), t, w, days, f);
			if (amount <= 0) {
				amount = fn_52f030(save, t, m.u8(0x13A), w, days, f);
			}
			if (amount < (kBarrelWare[w] ? 1000 : 6000)) {
				continue;
			}
			f = float(double(float(kWareWeight[w])) * double(f));
			sum = float(double(f) + double(sum));
		}
		if (sum > best) {
			best_town = t;
			best = sum;
		}
	}
	// Si ninguna da margen, la ciudad en la que está (solo en las pruebas sintéticas).
	const uint8_t town = best_town < town_count(save) ? uint8_t(best_town) : m.u8(0x39);
	m.u8(0x13B) = town;
	m.u8(0x38) = town;
}

// 0x519170: de camino a [+0x13B], una parada en otra ciudad de `mask` que
// no alargue el viaje más de un 20 %; si hay varias, se queda con la de
// mayor (habitantes + casco del barco) & 0xFF (la última si empatan). La
// pone en [+0x38].
void fn_519170(SaveGame& save, int ship, uint32_t mask) {
	ShipMemory& m = save.ships[size_t(ship)].memory;
	if (m.u8(0x39) == m.u8(0x13B)) {
		return;
	}
	const uint32_t direct = fn_531b70(save, m.u8(0x39), m.u8(0x13B));
	const uint32_t limit = direct + direct / 5;
	int32_t best = 0;
	uint32_t bit = 1;
	for (uint32_t t = 0; t < town_count(save); ++t, bit += bit) {
		if (!(bit & mask)) {
			continue;
		}
		const uint32_t via = uint32_t(fn_531b70(save, m.u8(0x39), t)) + fn_531b70(save, t, m.u8(0x13B));
		if (via >= limit) {
			continue;
		}
		const int32_t score = int32_t((save.towns[t].memory.u32(0x2D4) + uint32_t(m.s32(0x18))) & 0xFF);
		if (best > score) {
			continue;
		}
		best = score;
		m.u8(0x38) = uint8_t(t);
	}
}

// 0x5184F0: marineros de más que contrataría un barco que va armado: según
// su bodega libre (0x5182B0, uno por cada 400), hasta los que quiere su tipo
// (kSailorsWanted) y como mucho hasta capacidad/2000 + nivel · kSailorsPerLevel
// (al menos 20) menos los que lleva.
int32_t fn_5184f0(SaveGame& save, int ship) {
	SavedShip& s = save.ships[size_t(ship)];
	ShipMemory& m = s.memory;
	const int32_t free_space = recalculate_cargo(s);
	const int type = m.u8(0xE) & 3;
	int32_t most = m.s32(0x10) / 2000 + int32_t(m.u8(0xF)) * kSailorsPerLevel[type];
	if (most < 20) {
		most = 20;
	}
	const uint16_t sailors = m.u16(0x40);
	most -= sailors;
	if (free_space < 0) {
		return 0;
	}
	int32_t hire = free_space / 400;
	if (uint16_t(kSailorsWanted[type]) > sailors) {
		hire += int32_t(kSailorsWanted[type]) - int32_t(sailors);
	}
	return hire > most ? most : hire;
}

// 0x519FC0: la tripulación para el próximo viaje. Sin el bit 0x10 de las
// órdenes, la mínima del tipo; con él, contrata en la ciudad de destino lo
// que diga 0x5184F0 (sin dejarla sin parados) y al menos la mínima.
void fn_519fc0(SaveGame& save, int ship) {
	ShipMemory& m = save.ships[size_t(ship)].memory;
	const uint32_t destination = m.u8(0x38);
	if (destination > town_count(save)) {
		return;
	}
	const uint8_t min = kMinSailors[m.u8(0xE) & 3];
	if (!(m.u8(0x136) & 0x10)) {
		m.u8(0x3C) |= 2;
		m.u16(0x40) = min;
		return;
	}
	int32_t hire = fn_5184f0(save, ship);
	if (hire != 0) {
		// Solo en las pruebas sintéticas: no sale en las 12 partidas. Con el
		// destino igual al número de ciudades el juego leería fuera de la
		// tabla de ciudades.
		if (destination < save.towns.size()) {
			TownMemory& t = town_of(save, destination);
			const int32_t idle = t.s32(0x2E4);
			if (hire >= idle) {
				hire = idle - 1;
			}
			t.s32(0x2E4) = sub(idle, hire);
			t.s32(0x2D4) = sub(t.s32(0x2D4), hire);
			m.u16(0x40) = uint16_t(m.u16(0x40) + hire);
		}
	}
	if (m.u16(0x40) < min) {
		m.u16(0x40) = min;  // Solo en las pruebas sintéticas.
	}
	m.u8(0x3C) |= 2;
}

// 0x519BA0: ¿puede zarpar el barco? Marineros suficientes, tripulación
// descansada (byte alto de [+0x3E] > 0), sin más carga que capacidad, con el
// casco al menos al 20 % si tiene capitán, en un estado que lo permita y con
// el puerto abierto (sin bloqueo ni asedio: [0x2C8] & 0x4000200).
bool fn_519ba0(const SaveGame& save, int ship) {
	const ShipMemory& m = save.ships[size_t(ship)].memory;
	if (m.u16(0x40) < kMinSailors[m.u8(0xE) & 3] || m.s8(0x3F) <= 0 || m.s32(0x118) > m.s32(0x10)) {
		return false;
	}
	if (m.u16(0x42) < save.globals.u16(global::kCaptainCount) && m.s32(0x14) > 5 * m.s32(0x18)) {
		return false;
	}
	// The states 12 to 14 checked with the oracle: scenario ship_ai_branches (saves 1, 3, 5, 7).
	const uint16_t status = m.u16(0x134);
	if (status == 7 || status == 9 || (status >= 12 && status <= 14) || status >= 0x11) {
		return false;
	}
	const uint32_t town = m.u8(0x39);
	return !(town < town_count(save) && (save.towns[town].memory.u32(0x2C8) & 0x4000200));
}

// 0x519C40: ¿puede el barco guiar un convoy? Al menos 20 marineros, la mitad
// del casco si no va ya en uno, artillería ([+0x120] ≥ 0x43), capitán y que
// no sea pirata.
bool fn_519c40(const SaveGame& save, int ship) {
	const ShipMemory& m = save.ships[size_t(ship)].memory;
	if (m.u16(0x40) < 0x14) {
		return false;
	}
	if (!(m.u16(0x8) < convoy_count(save)) && m.s32(0x18) < (m.s32(0x14) >> 1)) {
		return false;
	}
	if (m.s32(0x120) < 0x43) {
		return false;
	}
	if (m.u16(0x42) >= save.globals.u16(global::kCaptainCount)) {
		return false;
	}
	return m.u8(0x15C) == 0;
}

// 0x509AE0 (objeto de los barcos): si no quedan convoyes libres, reserva 0x20
// más y los enlaza en la lista de libres. Solo en las pruebas sintéticas: no
// sale en las 12 partidas.
void fn_509ae0(SaveGame& save) {
	GlobalMemory& g = save.globals;
	const uint16_t count = g.u16(kConvoyCount);
	const uint16_t free = g.u16(kConvoyFree);
	if (free < count) {
		return;
	}
	const uint16_t grown = uint16_t(count + 0x20);
	g.u16(kConvoyCount) = grown;
	// El juego copia los [0x6DD884] primeros registros al array nuevo.
	std::vector<GameMemory<0x3C>> convoys(grown);
	for (size_t i = 0; i < free && i < save.convoys.size() && i < convoys.size(); ++i) {
		convoys[i] = save.convoys[i];
	}
	save.convoys = std::move(convoys);
	for (uint16_t i = g.u16(kConvoyFree); i < g.u16(kConvoyCount);) {
		GameMemory<0x3C>& c = save.convoys[i];
		c.u16(0x8) = ++i;
		c.u16(0x12) = 0xFFFF;
	}
}

// 0x501BE0: recalcula el convoy a partir de sus barcos: tipos, velocidad del
// más lento y, del que lo guía ([+0x10]), dueño, estado, ciudad, origen,
// destino y espera, que pasan a todos (con la capacidad de los del mismo
// dueño). Si el que lo guía está en el estado 0x15, sale de la lista; en el
// 0x0B o mayores de 0x16, la lista se corta ahí; en el 7, el 8, del 0xC al
// 0xE y el 0x11 (hundido), el convoy se queda sin guía.
void fn_501be0(SaveGame& save, int convoy) {
	GameMemory<0x3C>& c = save.convoys[size_t(convoy)];
	bool led = false;
	c.u8(0x1) = 0;
	c.u32(0x4) = 0;
	c.u32(0xC) = 0xFFFFFFFF;
	uint16_t* link = &c.u16(0xA);
	if (*link < ship_count(save)) {
		do {
			SavedShip& s = save.ships[*link];
			ShipMemory& m = s.memory;
			c.u8(0x1) |= m.u8(0xE);
			const uint32_t speed = Sailing::speed(s);
			if (speed < c.u32(0xC)) {
				c.u32(0xC) = speed;
			}
			bool next = true;
			if (*link == c.u16(0x10)) {
				c.u8(0x18) |= 0x20;
				// En las partidas solo sale el estado 1 (convoyes nuevos); los
				// demás, solo en las pruebas sintéticas.
				const uint16_t status = m.u16(0x134);
				bool lead = true;
				switch (status) {
				case 0x0: case 0x1: case 0x2: case 0x3: case 0x4: case 0x5: case 0x6: case 0xF: case 0x12: case 0x13:
				case 0x14:
					c.u16(0x12) = status;
					break;
				case 0x7: case 0x8: case 0xC: case 0xD: case 0xE:
					c.u16(0x12) = 0;
					c.u16(0x10) = 0xFFFF;
					lead = next = false;
					break;
				case 0x9:
					c.u16(0x12) = 9;
					break;
				case 0xA:
					c.u16(0x12) = 0xA;
					break;
				case 0x10:
					c.u16(0x12) = 0xF;
					m.u8(0x3C) |= 0x20;
					m.u16(0x134) = 0xF;
					break;
				case 0x11:
					c.u16(0x12) = 0xF;
					c.u16(0x10) = 0xFFFF;
					lead = next = false;
					break;
				case 0x15:
					*link = m.u16(0x6);
					lead = next = false;
					break;
				case 0x16:
					c.u16(0x12) = 0x16;
					break;
				default:  // 0x0B y mayores de 0x16
					*link = 0xFFFF;
					lead = next = false;
					break;
				}
				if (lead) {
					c.u8(0x0) = m.u8(0x0);
					led = true;
					c.u16(0x1C) = m.u8(0x39);
					c.u8(0x1E) = m.u8(0x37);
					c.u8(0x1F) = m.u8(0x38);
					c.u16(0x16) = m.u16(0x138);
				}
			}
			if (next) {
				link = &m.u16(0x6);
			}
		} while (*link < ship_count(save));
	}
	if (!led) {
		c.u16(0x10) = 0xFFFF;
		return;
	}
	for (uint32_t i = c.u16(0xA); i < ship_count(save); i = save.ships[i].memory.u16(0x6)) {
		ShipMemory& m = save.ships[i].memory;
		if (c.u8(0x0) == m.u8(0x0)) {
			c.s32(0x4) = add(c.s32(0x4), m.s32(0x10));
		} else {
			c.u16(0x14) &= 0xFFFD;
		}
		if (uint16_t(m.u8(0x39)) != c.u16(0x1C)) {
			m.u16(0x3C) |= 8;
			m.u8(0x39) = c.u8(0x1C);
		}
		if (c.u16(0x12) == 1) {
			if (c.u16(0x16) < m.u16(0x138)) {
				c.u16(0x16) = m.u16(0x138);  // Checked with the oracle: scenario ship_ai_branches (saves 1, 3, 5, 7).
			}
		} else {
			m.u16(0x138) = c.u16(0x16);
		}
		m.u8(0x37) = c.u8(0x1E);
		m.u8(0x38) = c.u8(0x1F);
		m.u16(0x134) = c.u16(0x12);
	}
}

// 0x501680: el convoy nuevo, guiado por `ship` (su único barco): `mode` va a [+0x14].
void fn_501680(SaveGame& save, int convoy, uint16_t ship, uint16_t mode) {
	GameMemory<0x3C>& c = save.convoys[size_t(convoy)];
	c.u16(0xA) = ship;
	c.u16(0x10) = ship;
	c.u16(0x14) = mode;
	fn_501be0(save, convoy);
	c.u32(0x20) = 0;
	std::memset(c.bytes.data() + 0x24, 0xFF, 16);
	std::memset(c.bytes.data() + 0x34, 0xFF, 4);
	c.u16(0x38) = 0xFFFF;
	c.u16(0x2) = 0xFFFF;
	c.u32(0x18) |= 0x4401;
}

// 0x509B90 (objeto de los barcos): el barco `ship`, atracado, funda un convoy
// si puede guiarlo (0x519C40): sale de la lista de barcos en puerto y espera
// (estado 1) a que se le unan otros; con `immediate`, sin esperar y hacia su
// propia ciudad; si no, `wait_days` días y hacia `destination`.
void fn_509b90(SaveGame& save, uint32_t ship, uint32_t immediate, uint32_t wait_days, uint32_t destination) {
	GlobalMemory& g = save.globals;
	if (ship >= ship_count(save)) {
		return;
	}
	ShipMemory& m = save.ships[ship].memory;
	if (!fn_519c40(save, int(ship)) || m.u16(0x134) > 1 || m.u8(0x39) >= town_count(save) ||
		m.u16(0x8) < convoy_count(save)) {
		return;
	}
	if (!(g.u16(kConvoyFree) < convoy_count(save))) {
		fn_509ae0(save);
	}
	uint16_t* link = &g.u16(kDockedHead);
	while (*link != ship) {
		if (*link >= ship_count(save)) {
			return;
		}
		link = &save.ships[*link].memory.u16(0x6);
	}
	*link = m.u16(0x6);
	m.u16(0x6) = 0xFFFF;
	if (immediate) {
		m.u16(0x138) = 0;
		m.u8(0x38) = m.u8(0x39);
	} else {
		m.u8(0x38) = uint8_t(destination);
		m.u16(0x138) = uint16_t(wait_days << 8);
	}
	m.u8(0x3C) |= 0x20;
	m.u8(0x37) = m.u8(0x39);
	m.u16(0x134) = 1;
	const uint16_t convoy = g.u16(kConvoyFree);
	m.u16(0x8) = convoy;
	GameMemory<0x3C>& c = save.convoys[convoy];
	g.u16(kConvoyFree) = c.u16(0x8);
	c.u16(0x8) = g.u16(kConvoyHead);
	g.u16(kConvoyHead) = m.u16(0x8);
	const uint16_t mode = !immediate && destination != m.u8(0x39) ? 0 : 3;
	fn_501680(save, convoy, uint16_t(ship), mode);
}

// 0x52AC80 (sobre la ciudad): pone el barco al final de la lista de barcos
// del astillero de reparación (cabeza u16 [0x81E], enlazada por el [+0x6]
// de cada barco), si no estaba ya.
void fn_52ac80(SaveGame& save, int town, uint16_t ship) {
	const uint16_t count = ship_count(save);
	uint16_t* link = &save.towns[size_t(town)].memory.u16(0x81E);
	for (uint16_t i = *link; i < count; i = *link) {
		if (i == ship) {
			return;
		}
		link = &save.ships[i].memory.u16(0x6);
	}
	*link = ship;
}

// 0x535630 (sobre 0x6DE588): en el registro de un hueco, cada ciudad baja a
// 1 + el tiempo de viaje (/16, en días) desde las que están a 0, y las demás
// se quedan en 15 como mucho.
void fn_535630(SaveGame& save, std::vector<uint8_t>& towns) {
	for (uint32_t b = 0; b < town_count(save); ++b) {
		if (towns[b] == 0) {
			for (uint32_t c = 0; c < town_count(save); ++c) {
				const uint8_t days = uint8_t((fn_531b70(save, b, c) >> 4) + 1);
				if (days < towns[c]) {
					towns[c] = days;
				}
			}
		} else if (towns[b] > 0xF) {
			towns[b] = 0xF;  // Solo en las pruebas sintéticas.
		}
	}
}

// 0x535400 (sobre 0x6DE588): un barco del comerciante `merchant` está en la
// ciudad `town`: la pone a 0 en los huecos de ese comerciante (0x535630).
void fn_535400(SaveGame& save, uint32_t town, uint32_t merchant) {
	if (town >= town_count(save)) {
		return;
	}
	for (uint32_t i = 0; i < 8; ++i) {
		std::vector<uint8_t>& towns = save.player_towns[i];
		if (save.globals.u8(kPlayerSlots + i) == merchant && !towns.empty()) {
			towns[town] = 0;
			fn_535630(save, towns);
		}
	}
}

// 0x519EB0: el barco de la IA que va a zarpar (estado 2) hacia [+0x38] busca
// compañía. Si hay en su ciudad un convoy esperando (estado 1, sin el bit 1
// de [+0x14]) hacia el mismo destino, se une a él; si no, y si no hay más de
// 10 convoyes esperando, intenta fundar uno (0x509B90). Devuelve true si el
// barco ha salido de la lista de barcos en puerto (`link` apunta ya al
// siguiente); si no, zarpa solo (estado 2).
bool fn_519eb0(SaveGame& save, int ship, uint16_t* link) {
	GlobalMemory& g = save.globals;
	ShipMemory& m = save.ships[size_t(ship)].memory;
	const uint16_t self = *link;
	uint32_t waiting = 0;
	uint16_t i = g.u16(kConvoyHead);
	if (i < convoy_count(save)) {
		do {
			GameMemory<0x3C>& c = save.convoys[i];
			if (!(c.u8(0x14) & 2) && c.u16(0x12) == 1) {
				if (c.u16(0x1C) == uint16_t(m.u8(0x39)) && c.u8(0x1F) == m.u8(0x38)) {
					*link = m.u16(0x6);
					m.u16(0x6) = c.u16(0xA);
					c.u16(0xA) = self;
					m.u8(0x3C) |= 0x60;
					m.u16(0x8) = i;
					m.u16(0x134) = 1;
					c.u32(0x18) |= 0x4002;
					clear_ship_route(save.ships[size_t(ship)]);  // 0x516840
					return true;
				}
				++waiting;
			}
			i = c.u16(0x8);
		} while (i < convoy_count(save));
		if (waiting > 10) {
			m.u16(0x134) = 2;
			return false;
		}
	}
	m.u16(0x134) = 0;
	fn_509b90(save, self, 0, 5, m.u8(0x38));
	if (self == *link) {
		m.u16(0x134) = 2;
		return false;
	}
	return true;
}

}  // namespace ship_ai_detail

using namespace ship_ai_detail;

// 0x518B80: un barco de la IA atracado (estado 0) decide qué hacer: quedarse
// quieto (bit 0x20 de las órdenes), zarpar (estado 2) hacia su próxima parada
// después de cargar, ir al astillero (estado 4) o esperar (estado 1). Si está
// en la ciudad de la que salió y va a otra, carga lo que le quepa y zarpa; si
// no, ha llegado: elige el próximo destino desde su base ([+0x13A]) y la
// parada intermedia, compra o vende (ship_trade.cpp) y espera.
void fn_518b80(SaveGame& save, int ship, uint32_t arg) {
	(void)arg;  // El juego no lo usa.
	SavedShip& s = save.ships[size_t(ship)];
	ShipMemory& m = s.memory;
	MerchantMemory& merchant = owner_of(save, m);
	const uint8_t orders = m.u8(0x136);
	if ((orders & 0x20) == 0x20) {
		// Solo en las pruebas sintéticas: no sale en las 12 partidas.
		m.u16(0x134) = 1;
		m.u16(0x138) = 0x80;
		m.u16(0x40) = 0;
		return;
	}
	if ((orders & 0x10) == 0x10) {
		fn_519280(save, ship);
		fn_51a070(save, ship, m.u8(0x39));
	}
	fn_519fc0(save, ship);
	if (m.u8(0x37) == m.u8(0x39) && m.u8(0x38) != m.u8(0x39)) {
		if (!fn_519ba0(save, ship)) {
			fn_518e00(save, ship);
			return;
		}
		fn_519920(save, ship, m.u8(0x13B));
		if (recalculate_cargo(s) <= (m.s32(0x10) >> 2)) {
			// Bodega casi llena: directo al destino.
			m.u16(0x134) = 2;
			m.u8(0x38) = m.u8(0x13B);
			return;
		}
		fn_519920(save, ship, m.u8(0x38));
		m.u16(0x134) = 2;
		return;
	}

	// Ha llegado (o vuelve a decidir).
	const uint32_t mask = fn_518d60(save, ship);
	bool at_target;
	if (m.u8(0x39) == m.u8(0x13B)) {
		at_target = true;
		const uint8_t base = m.u8(0x13A);
		if (m.u8(0x39) == base) {
			fn_518ff0(save, ship, mask);
		} else {
			m.u8(0x13B) = base;
			m.u8(0x38) = base;
		}
	} else {
		at_target = false;
		m.u8(0x38) = m.u8(0x13B);
	}
	if (m.u8(0x39) != m.u8(0x13A)) {
		fn_519170(save, ship, mask);
	}
	if (at_target) {
		fn_519280(save, ship);
	} else if (m.u8(0x13B) == m.u8(0x13A)) {
		fn_5193f0(save, ship);
	} else {
		fn_5196b0(save, ship);
	}
	const uint8_t town = m.u8(0x39);
	if (m.u8(0x37) != town) {
		m.u8(0x37) = town;
		// Al astillero si el casco ha bajado de (0x200 − [0x547]) / 0x200.
		const int32_t keep = int32_t(uint32_t(0x200 - int32_t(merchant.u8(0x547))) * uint32_t(m.s32(0x14))) >> 9;
		if (keep > m.s32(0x18)) {
			m.u16(0x134) = 4;
			m.u16(0x138) = 0;
			m.u8(0x38) = town;
			m.u8(0x37) = town;
		}
	}
	if (m.u16(0x134) != 4) {
		m.u16(0x134) = 1;
		m.u16(0x138) = uint16_t(merchant.u8(0x547) + 0x40);
	}
}

// Estado 0 de 0x506720 (desde 0x5067D0): atracado. La tripulación descansa;
// los barcos de la IA deciden (0x518B80) y, si van a zarpar, buscan convoy
// (0x519EB0); los de los jugadores avisan de que están en la ciudad (0x535400).
bool fn_5067d0(SaveGame& save, int ship, uint16_t* link, Sailing* sailing) {
	GlobalMemory& g = save.globals;
	ShipMemory& m = save.ships[size_t(ship)].memory;
	if (m.u8(0x0) == g.u32(global::kPlayer)) {
		++g.u16(kPlayerInPort);
	}
	if (m.u32(0x44) + 0x100 < g.u32(global::kWorldTick)) {
		// Pasado un día desde que atracó, descansa un punto por tick hasta 0x700.
		++m.u16(0x3E);
		if (m.s16(0x3E) >= 0x700) {
			m.u16(0x3E) = 0x700;
		} else if (m.u8(0x3E) == 0) {
			m.u8(0x3D) |= 2;
		}
	}
	if (owner_of(save, m).u16(0x8) != 0) {
		fn_518b80(save, ship, *link);
		if (m.u16(0x134) == 2) {
			return !fn_519eb0(save, ship, link);
		}
		return true;
	}
	if (m.u8(0x136) & 1) {
		fn_518860(save, sailing, ship);  // A human's ship on its trade route (trade_routes.cpp).
		return true;
	}
	const uint8_t town = m.u8(0x39);
	if (m.u8(0x37) != town) {
		// Solo en las pruebas sintéticas: no sale en las 12 partidas.
		fn_535400(save, town, m.u8(0x0));
		m.u8(0x37) = m.u8(0x39);
		return true;
	}
	if (g.u8(global::kWorldTick) == 0) {
		fn_535400(save, town, m.u8(0x0));
	}
	return true;
}

// Estado 4 de 0x506720 (0x506A19): va al astillero de reparación. Sale de la
// lista de barcos en puerto, pasa al estado 6 y entra en la lista del
// astillero de su ciudad (0x52AC80).
bool fn_506a19(SaveGame& save, int ship, uint16_t* link) {
	GlobalMemory& g = save.globals;
	ShipMemory& m = save.ships[size_t(ship)].memory;
	if (m.u8(0x0) == g.u32(global::kPlayer)) {
		++g.u16(kPlayerInPort);  // Checked with the oracle: scenario ship_ai_branches (saves 1, 3, 5, 7).
	}
	m.u16(0x138) = 0;
	const uint16_t self = *link;
	*link = m.u16(0x6);
	m.u16(0x6) = 0xFFFF;
	m.u16(0x134) = 6;
	if (m.u8(0x136) == 1) {
		m.u8(0x136) = 2;  // Solo en las pruebas sintéticas.
	}
	m.u8(0x3C) |= 0x20;
	fn_52ac80(save, m.u8(0x39), self);
	return false;
}

// Estado 5 de 0x506720 (0x506B6A): vuelve a estar atracado (la escena del
// puerto, 0x613920, no se traduce).
bool fn_506b6a(SaveGame& save, int ship, uint16_t* link) {
	(void)link;
	ShipMemory& m = save.ships[size_t(ship)].memory;
	m.u8(0x3C) |= 0x20;
	m.u16(0x138) = 0;
	m.u16(0x134) = 0;
	return true;
}

}  // namespace p3::data::tick
