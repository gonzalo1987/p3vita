// La cola de eventos (events.cpp): los tipos 0x03 (0x4DDA40), 0x01 (0x4DB4E0),
// 0x2A (0x4DE4F0), 0x2B (0x4DE790), 0x27 (0x4DDC00), 0x07 (0x4E23A4), 0x1D
// (0x4E9564) and 0x15 (0x4E7C74).
//
//   0x01  deuda de un comerciante ([+0x11]) con otro ([+0x10]; 0xFF: con la
//         ciudad), u32 [+0x8] lo que debe, [+0x14] la ciudad: cada 10 días
//         cobra lo que puede (0x4F6570: dinero, mercancía de su oficina o de
//         sus barcos en la ciudad).
//   0x03  cada 10 días: lo de los capitanes (0x4DCEA0), la mercancía que viene
//         en barcos (0x504970) y operaciones para el juego: especialidades de
//         las ciudades (0x79), rumores (0x7A), plagas (0x7C), incendios (0x7D),
//         ofertas a los ricos (0x81), fiestas (0x61), subastas de barcos (0x38),
//         torneos (0x84) y robos en las oficinas del jugador (0x64).
//   0x2A  un comerciante de la IA rico abre una oficina (0x5308A0 con create).
//   0x2B  el comerciante de la IA con menos barcos encarga uno (operación 0x2C,
//         0x52B2C0).
//   0x07  la fiesta de un comerciante en una ciudad (la operación 0x61, que
//         encola la fiesta del evento 0x03, 0x5316E0).
//   0x1D  un incendio en una ciudad (la operación 0x7D, que encola 0x4DCBF0 en
//         el evento 0x03): se extiende a los edificios cercanos y los borra.
//   0x15  a merchant's wedding (operation 0x60, queued only by the mission
//         interpreter and the interface): the dowry ships and the feast.
//
// Comprobado con el oráculo (town_oracle.py --barcos --cpr --resto 4d85c0) en
// las 12 partidas y 60 días. Lo que no sale en ellas se ha comprobado con
// unos pocos estados retocados (marcado «Comprobado solo con estados
// retocados») o va marcado «Sin comprobar con el oráculo». Los mensajes no
// se traducen.

#include <cstring>
#include <vector>

#include "p3data/prices.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {

// Traducciones de events.cpp que se usan aquí.
namespace events_detail {
uint16_t fn_4d8cf0(SaveGame& save, uint32_t tick);
void fn_4fe190(CaptainMemory& c);
int16_t fn_4f9580(const SaveGame& save, int merchant, uint32_t town);
}  // namespace events_detail
namespace events_mayor_detail {
uint16_t fn_507d60(SaveGame& save, uint16_t& out, uint32_t town, uint32_t mode);
void fn_52a8d0(SaveGame& save, int town, int ship, uint32_t type);
void fn_5181d0(SaveGame& save, int ship);
void fn_509a50(SaveGame& save, int ship);
}  // namespace events_mayor_detail
namespace diet_detail {
void fn_4f9120(SaveGame& save, uint32_t merchant, uint32_t town, uint32_t hull);
}  // namespace diet_detail
namespace events_rare_detail {
void fn_4db100(SaveGame& save, uint32_t town, uint16_t record);
}  // namespace events_rare_detail
namespace alderman_detail {
void fn_51a750(SaveGame& save, int ship, uint8_t n);
void fn_51a4e0(SaveGame& save, int ship, uint32_t strength, uint8_t level);
}  // namespace alderman_detail
namespace facilities_detail {
uint32_t fn_52bb00(const SaveGame& save, int town);
}  // namespace facilities_detail
namespace merchants_detail {
uint8_t fn_4f9430(const SaveGame& save, int merchant);
}  // namespace merchants_detail

namespace {

constexpr uint32_t kEvents = 0x6DD73C;
constexpr uint32_t kEventBusy = kEvents + 0x4;  // u32 1 mientras 0x4D85C0 atiende la cola.
constexpr uint32_t kEventHead = kEvents + 0x8;  // u16
constexpr uint32_t kEventCount = kEvents + 0xC;  // u16

constexpr uint32_t kTownCount = global::kWorldTownCount;  // u32
constexpr uint32_t kMerchantCount = global::kWorldMerchantCount;  // u16
constexpr uint32_t kOfficeCount = global::kWorldOfficeCount;  // u16
constexpr uint32_t kOfficeFree = global::kWorld + 0x7E;  // u16 primera oficina libre (enlazadas por [+0x2C8])
constexpr uint32_t kWorldRecordCount = 0x6DE4A6;  // u16
constexpr uint32_t kTick = global::kWorldTick;  // u32
constexpr uint32_t kDayOfYear = global::kWorldDayOfYear;  // u16
constexpr uint32_t kShipCount = global::kShipCount;  // u16
constexpr uint32_t kCaptainCount = global::kCaptainCount;  // u16
// [0x6DE52A] u8 (del mundo, [+0x8A]): con él no se hace casi nada de la IA.
constexpr uint32_t kWorldFlag8A = 0x6DE52A;
constexpr uint32_t kAldermanBusy = 0x6DF31C;  // u32 (ver merchants.cpp)
constexpr uint32_t kOperations = 0x6DF2F0;  // u32 del objeto de las operaciones encoladas
constexpr uint32_t kOperationCount = 0x6DF346;  // u16 operaciones encoladas ([+0x56] de 0x6DF2F0)
// Buzón de los mensajes a los comerciantes (0x6DD730): u16 [+0x6] su número.
constexpr uint32_t kMailCount = 0x6DD736;
constexpr uint32_t kMailFree = 0x6DD734;  // u16 the mailbox's first free letter
// Del mundo: [+0x84] u32 tick y [+0x88] u8 ciudad de la próxima fiesta (0x5316E0).
constexpr uint32_t kFeastTick = global::kWorld + 0x84;
constexpr uint32_t kFeastTown = global::kWorld + 0x88;
// [0x6DE52D] u8 (del mundo, [+0x8D]): comerciante del torneo (0x4DD530).
constexpr uint32_t kTournamentMerchant = global::kWorld + 0x8D;
// Tabla de las 40 ciudades (StadtDaten.ini): 0x34 bytes por número de ciudad.
constexpr uint32_t kTownTable = 0x6DDB90;

using Event = GameMemory<0x18>;

uint32_t town_count(const SaveGame& save) {
	return save.globals.u32(kTownCount);
}

uint32_t merchant_count(const SaveGame& save) {
	return save.globals.u16(kMerchantCount);
}

uint32_t office_count(const SaveGame& save) {
	return save.globals.u16(kOfficeCount);
}

uint32_t ship_count(const SaveGame& save) {
	return save.globals.u16(kShipCount);
}

uint32_t captain_count(const SaveGame& save) {
	return save.globals.u16(kCaptainCount);
}

TownMemory& town_of(SaveGame& save, uint32_t i) {
	return save.towns[i].memory;
}

MerchantMemory& merchant_of(SaveGame& save, uint32_t i) {
	return save.merchants[i].memory;
}

ShipMemory& ship_of(SaveGame& save, uint32_t i) {
	return save.ships[i].memory;
}

// 0x530400: la oficina `index`, o -1 si no hay tantas.
int office_at(const SaveGame& save, uint32_t index) {
	return index < office_count(save) ? int(index) : -1;
}

// La región de la ciudad en la tabla de StadtDaten.ini ([+0x30] de su fila).
uint8_t town_region(const SaveGame& save, uint32_t town) {
	return save.globals.u8(kTownTable + 0x34 * save.towns[town].memory.u8(0x2C1) + 0x30);
}

// Mercancías en las que se cuenta por 200 unidades internas (en las demás,
// por 2000): 0x672C14.
constexpr uint8_t kFineUnit[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};

// Una operación para el juego como la deja 0x535740: código 0xFFFFFFFF y el resto a 0.
struct Operation {
	uint8_t bytes[0x14] = {0xFF, 0xFF, 0xFF, 0xFF};
	void u32(size_t o, uint32_t v) { std::memcpy(bytes + o, &v, 4); }
	void u16(size_t o, uint16_t v) { std::memcpy(bytes + o, &v, 2); }
	void u8(size_t o, uint8_t v) { bytes[o] = v; }
};

void enqueue(SaveGame& save, const Operation& op) {
	enqueue_operation(save, op.bytes, sizeof(op.bytes));
}

// rand() / 32768 · n, truncado (fild, fstp DWORD, fdiv 32768.0f, fmul DWORD n).
uint32_t rand_fraction(SaveGame& save, float n) {
	const float r = float(int32_t(game_rand(save)));
	return uint32_t(ftol(double(r) / 32768.0 * double(n)));  // 0x66CB34
}

}  // namespace

namespace events_towns_detail {

// --- Evento 0x01: deudas ---

// 0x4F6570 (thiscall sobre el comerciante `merchant`, con su índice `me` y la
// ciudad `town`): cobra `amount` de su dinero; si no le llega, vende a la
// ciudad la mercancía de su oficina en ella (de la 23 a la 0) o, si no tiene
// nada en la oficina, la de su primer barco que esté en la ciudad (estado <
// 0xC) que dé algo sin pasar de 65000 en total. Lo que sobra vuelve a su
// dinero. `amount` queda con lo que falta. No se traducen los mensajes.
void fn_4f6570(SaveGame& save, int merchant, int32_t& amount, uint8_t me, uint8_t town) {
	MerchantMemory& m = merchant_of(save, uint32_t(merchant));
	TownMemory& t = town_of(save, town);
	uint32_t paid = 0;  // [esp+0x30]
	uint8_t sold = 0;  // [esp+0x2E] (el objeto del mensaje, 0x51A850, lo deja a 0)
	if (amount <= 0) {
		return;
	}
	const int32_t money = m.s32(0);
	if (money > 0) {
		if (money >= amount) {
			m.s32(0) = int32_t(uint32_t(money) - uint32_t(amount));
			amount = 0;
			return;
		}
		amount = int32_t(uint32_t(amount) - uint32_t(money));
		paid = uint32_t(money) & 0xFFFF;
		m.s32(0) = 0;
	}
	const int office = facilities_detail::fn_5308a0(save, me, town);
	if (office >= 0) {
		OfficeMemory& o = save.offices[size_t(office)].memory;
		for (int w = 23; w >= 0; --w) {
			if (amount <= 0) {
				break;
			}
			const int32_t stock = o.s32(4 + 4 * w);
			if (stock <= 0) {
				continue;
			}
			const int32_t price = game_sell_price(save, w, town, stock);
			t.s32(4 + 4 * w) = int32_t(uint32_t(t.s32(4 + 4 * w)) + uint32_t(stock));
			o.s32(4 + 4 * w) = 0;
			paid += uint32_t(price);
			amount = int32_t(uint32_t(amount) - uint32_t(price));
			sold = 1;
		}
		if (sold == 1) {
			if (amount <= 0) {
				m.s32(0) = int32_t(uint32_t(m.s32(0)) - uint32_t(amount));
				amount = 0;
			}
			return;  // (con un jugador humano, mensajes)
		}
	}
	for (uint32_t s = m.u16(0xE); s < ship_count(save); s = ship_of(save, s).u16(4)) {
		ShipMemory& ship = ship_of(save, s);
		if (ship.u8(0x39) != town || ship.u16(0x134) >= 0xC) {
			continue;
		}
		for (int w = 23; w >= 0; --w) {
			if (amount <= 0) {
				break;
			}
			const int32_t cargo = ship.s32(0x54 + 4 * w);
			if (cargo <= 0) {
				continue;
			}
			const int32_t price = game_sell_price(save, w, town, cargo);
			const uint32_t total = paid + uint32_t(price);
			if (total >= 0xFDE8) {
				continue;  // Sin comprobar con el oráculo.
			}
			paid = total;
			amount = int32_t(uint32_t(amount) - uint32_t(price));
			sold = 2;
			t.s32(4 + 4 * w) = int32_t(uint32_t(t.s32(4 + 4 * w)) + uint32_t(cargo));
			ship.s32(0x54 + 4 * w) = 0;
		}
		if (sold == 2) {
			if (amount <= 0) {
				m.s32(0) = int32_t(uint32_t(m.s32(0)) - uint32_t(amount));
				amount = 0;
			}
			// (mensaje)
			ship_ai_detail::fn_517770(save, int(s), 4);
			recalculate_cargo(save.ships[s]);  // 0x5182B0
			return;
		}
	}
	(void)paid;  // (con un jugador humano y algo cobrado, mensajes)
}

// 0x4DB4E0 (evento 0x01): la deuda [+0x8] del comerciante [+0x11] con el
// [+0x10] (0xFF: la ciudad) en la ciudad [+0x14]. Con [+0x12], solo avisa
// (o, si no hay deudor, paga al acreedor si [+0xC] es 0). Si no, cobra
// (0x4F6570; si saca menos de un 10 %, también en su ciudad natal) y lo da al
// acreedor. Devuelve 1 si queda algo por cobrar (vuelve en 10 días con
// [+0x8] lo que falta; a los de la IA que deben a la ciudad no se les vuelve
// a pedir) y 0 si el evento se quita.
uint32_t fn_4db4e0(SaveGame& save, uint16_t event) {
	Event& e = save.events[event];
	if (e.u32(8) == 0) {
		return 0;  // Comprobado solo con estados retocados.
	}
	if (e.u8(0x10) == 0xFF && e.u8(0x11) == 0xFF) {
		return 0;  // Comprobado solo con estados retocados.
	}
	const uint32_t merchants = merchant_count(save);
	if (e.u8(0x12) != 0) {
		// Comprobado solo con estados retocados.
		if (e.u8(0x11) < merchants) {
			e.u8(0x12) = 0;  // (con un jugador humano, mensaje)
		} else {
			const uint32_t creditor = e.u8(0x10);
			if (creditor >= merchants) {
				return 0;  // El juego no lo mira (0x5303C0 no comprueba el índice).
			}
			if (e.u8(0xC) == 0) {
				MerchantMemory& c = merchant_of(save, creditor);
				c.s32(0) = int32_t(uint32_t(c.s32(0)) + e.u32(8));
				return 0;
			}
			e.u8(0x12) = 0;
		}
		e.u32(0) += 0xA00;
		return 1;
	}
	const uint32_t debtor = e.u8(0x11);
	if (debtor >= merchants) {
		return 0;  // Sin comprobar con el oráculo.
	}
	int32_t amount = int32_t(e.u32(8));  // [esp+0x10]
	e.u8(0x13) = 0;
	fn_4f6570(save, int(debtor), amount, uint8_t(debtor), e.u8(0x14));
	const MerchantMemory& d = merchant_of(save, debtor);
	if (save.events[event].u8(0x14) != d.u8(0x19) &&
		int32_t(uint32_t(amount) * 10) > int32_t(save.events[event].u32(8) * 9)) {
		// Comprobado solo con estados retocados.
		fn_4f6570(save, int(debtor), amount, uint8_t(debtor), d.u8(0x19));
	}
	Event& ev = save.events[event];
	const uint32_t creditor = ev.u8(0x10);
	if (creditor < merchants) {
		// Comprobado solo con estados retocados.
		MerchantMemory& c = merchant_of(save, creditor);
		c.s32(0) = int32_t(uint32_t(c.s32(0)) + ev.u32(8));
		if (amount <= 0) {
			return 0;
		}
		c.s32(0) = int32_t(uint32_t(c.s32(0)) - uint32_t(amount));
	} else if (d.u16(8) != 0) {
		return 0;
	}
	if (amount <= 0) {
		return 0;  // Comprobado solo con estados retocados.
	}
	// Comprobado solo con estados retocados: un jugador humano que no ha podido pagar.
	ev.u32(8) = uint32_t(amount);
	ev.u32(0) += 0xA00;
	return 1;
}

// --- Evento 0x03: cada 10 días ---

// How long each type of letter is kept (0x6C0198): 1 to 3, by the
// merchant's settings [+0x38], [+0x39], [+0x3A]; 4, fixed by type; 0, not at
// all. Past 0x87 the table runs into other data, which gives no 1 to 4.
constexpr uint8_t kLetterKind[0x88] = {
	2, 3, 3, 2, 2, 2, 4, 3, 2, 4, 1, 1, 4, 4, 4, 4, 4, 3, 2, 1, 1, 2, 2, 2,
	2, 1, 2, 2, 4, 2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 2, 1, 1, 1, 1,
	2, 1, 1, 2, 2, 4, 1, 1, 4, 4, 4, 1, 4, 1, 2, 3, 4, 1, 1, 2, 4, 4, 2, 1,
	1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 4, 4, 4, 4, 4, 4, 4, 3, 2, 1, 2, 1, 4,
	2, 2, 2, 3, 1, 1, 3, 3, 3, 3, 2, 1, 1, 1, 1, 1, 1, 4, 4, 2, 2, 2, 1, 1,
	1, 2, 2, 1, 2, 1, 1, 2, 1, 2, 1, 2, 1, 1, 0, 0,
};
// The keeping times of those settings (0x6C0220: never, 40, 20, 10, 5 days,
// none; past 5 the table runs into text).
constexpr uint32_t kLetterKeep[10] = {0xFFFFFFFF, 0x2800, 0x1400, 0xA00, 0x500, 0, 1702323527, 1650811506,
	540702049, 1701604449};
// First day of each month (0x672D78; the game masks the month with 0x7F).
constexpr uint16_t kLetterMonth[16] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365, 0, 31, 28};

// 0x4D6780 (on the mailbox 0x6DD730, with a merchant): his letters (the chain
// from [+0xA], next in [+0x6]; at most as many steps as letters) that are
// older than their keeping time go back to the mailbox's free list ([+0x4]),
// emptied (0x51AAB0, which also frees what hangs from them on the heap). The
// age: from the letter's date (day [+0x0] from 1, month [+0x1], year
// [+0x2]); the keeping time by type (kLetterKind): the merchant's setting,
// or fixed: never for 0x46, 7 days for 0x11, 0x0F and 0x10, 28 days for
// 0x72, never for the other fixed ones; 0x71 is kept until the tick [+0x4]
// of its heap record (SaveGame::mail_records). A letter of type 0x86 or more cuts the chain where it is (the link
// to it becomes 0xFFFF; the letter is still looked at, and the walk goes on
// from it); if that letter is removed, the game loses the free list (its
// head becomes 0xFFFF).
void fn_4d6780(SaveGame& save, int32_t merchant) {
	GlobalMemory& g = save.globals;
	if (merchant >= int32_t(merchant_count(save))) {
		return;  // Sin comprobar con el oráculo.
	}
	MerchantMemory& m = merchant_of(save, uint32_t(merchant));
	const uint32_t count = g.u16(kMailCount);
	const uint32_t now = g.u32(kTick);
	auto setting = [&](size_t o) {
		return m.u8(o) < 10 ? kLetterKeep[m.u8(o)] : 0;  // Past 9, sin comprobar con el oráculo.
	};
	uint16_t* link = &m.u16(0xA);
	uint32_t index = *link;
	for (uint32_t left = count; index < count && left > 0; --left) {
		GameMemory<0x10>& letter = save.mail[index];
		const uint8_t type = letter.u8(4);
		if (type >= 0x86) {
			*link = 0xFFFF;  // Sin comprobar con el oráculo.
		}
		const uint32_t month = letter.u8(1) & 0x7F;
		const uint32_t date = (month < 16 ? kLetterMonth[month] : 0) + uint32_t(letter.u16(2)) * 365 + letter.u8(0) - 1;
		const uint32_t written = date << 8;
		const uint32_t age = now > written ? now - written : 0;
		uint32_t keep = 0;
		switch (type < 0x88 ? kLetterKind[type] : 0) {
		case 1:
			keep = setting(0x38);
			break;
		case 2:
			keep = type == 0x46 ? 0xFFFFFFFF : setting(0x39);
			break;
		case 3:
			keep = type == 0x11 ? 0x700 : setting(0x3A);
			break;
		case 4:
			if (type == 0xF || type == 0x10) {
				keep = 0x700;
			} else if (type == 0x72) {
				keep = 0x1C00;
			} else if (type == 0x71 && index < save.mail_records.size() && save.mail_records[index]) {
				keep = now < save.mail_records[index]->u32(4) ? 0xFFFFFFFF : 0;
			} else {
				keep = 0xFFFFFFFF;  // Sin comprobar con el oráculo (0x71 without a record: the game reads [0 + 4]).
			}
			break;
		default:
			break;
		}
		if (age > keep) {
			const uint16_t removed = *link;
			*link = letter.u16(6);
			letter.u8(0) = 0;
			letter.u8(1) = 0;
			letter.u16(2) = 0xFFFF;
			letter.u8(4) = 0xFF;
			letter.u8(5) = 0;
			std::memset(letter.bytes.data() + 8, 0, 8);
			if (index < save.mail_records.size()) {
				save.mail_records[index].reset();  // 0x51AAB0 frees it.
			}
			letter.u16(6) = g.u16(kMailFree);
			g.u16(kMailFree) = removed;
		} else {
			link = &letter.u16(6);
		}
		index = *link;
	}
}

// 0x504970 (sobre el objeto de los barcos): la mercancía que viene de camino
// a cada ciudad ([0x670 + 4·w]): la carga de los barcos de la lista del mar
// ([+0xEA]) con su destino [+0x38] y la de los barcos de los convoyes
// ([+0xEC]) que navegan (estado [+0x12] 0xF) con el destino del convoy [+0x1F].
void fn_504970(SaveGame& save) {
	GlobalMemory& g = save.globals;
	for (uint32_t i = 0; i < town_count(save); ++i) {
		std::memset(town_of(save, i).bytes.data() + 0x670, 0, 0x60);
	}
	auto add = [&](uint32_t town, const ShipMemory& ship) {
		TownMemory& t = town_of(save, town);
		for (uint32_t w = 0; w < 24; ++w) {
			t.s32(0x670 + 4 * w) = int32_t(uint32_t(t.s32(0x670 + 4 * w)) + uint32_t(ship.s32(0x54 + 4 * w)));
		}
	};
	for (uint32_t s = g.u16(global::kShips + 0xEA); s < ship_count(save); s = ship_of(save, s).u16(6)) {
		const ShipMemory& ship = ship_of(save, s);
		if (ship.u8(0x38) < town_count(save)) {
			add(ship.u8(0x38), ship);
		}
	}
	for (uint32_t c = g.u16(global::kShips + 0xEC); c < g.u16(global::kShips + 0xF6); c = save.convoys[c].u16(8)) {
		const GameMemory<0x3C>& convoy = save.convoys[c];
		if (convoy.u16(0x12) != 0xF) {
			continue;
		}
		uint32_t s = convoy.u16(0xA);
		if (s >= ship_count(save) || convoy.u8(0x1F) >= town_count(save)) {
			continue;
		}
		do {
			add(convoy.u8(0x1F), ship_of(save, s));
			s = ship_of(save, s).u16(6);
		} while (s < ship_count(save));
	}
}

// 0x538A80 (operación 0x12, sobre la operación): el capitán del barco `ship`
// gana `add9` en [+0x9] y `add_ab` en [+0xA] y [+0xB] (el juego usa [+0xC] de
// la operación para los dos), sin pasar del tope de 0x673B34 que le toca por
// los bits 0-1, 2-3 y 4-5 de su número; y su [+0xC] se recalcula (0x4FE190).
// No se traduce el mensaje al jugador si [+0xC] sube.
void fn_538a80(SaveGame& save, uint32_t ship, uint8_t add9, uint8_t add_ab) {
	static constexpr uint8_t kCaps[4] = {0xFA, 0xC8, 0xFA, 0x96};  // 0x673B34
	if (ship >= ship_count(save)) {
		return;  // Sin comprobar con el oráculo.
	}
	const uint32_t index = ship_of(save, ship).u16(0x42);
	if (index >= captain_count(save)) {
		return;  // Sin comprobar con el oráculo.
	}
	CaptainMemory& c = save.captains[index];
	auto raise = [&](size_t field, uint8_t add, uint8_t cap) {
		const uint8_t old = c.u8(field);
		const uint8_t v = uint8_t(add + old);
		c.u8(field) = (v > cap || v < old) ? cap : v;
	};
	raise(9, add9, kCaps[index & 3]);
	raise(0xA, add_ab, kCaps[(index >> 2) & 3]);
	raise(0xB, add_ab, kCaps[(index >> 4) & 3]);
	events_detail::fn_4fe190(c);
}

// 0x4DCEA0 (thiscall sobre la cola, con el evento 0x03 de la cabeza): los
// capitanes de los barcos de cada comerciante (del último al primero), una
// octava parte cada vez (los de número & 7 = contador [+0x10] del evento & 7;
// el contador vuelve a 0 los 10 primeros días del año y, pasado de 31, no se
// hace nada). A los de la IA, +8 de habilidad (operación 0x12, que se ejecuta
// aquí: 0x535760 → 0x538A80); a los jugadores humanos se les pide (operación
// 0x12 al azar, si no están al tope de 0x672824). Los capitanes de más de 50
// años se retiran (evento 0x27 en la IA; operación 0x13 al jugador). Y los
// administradores de las oficinas del jugador (operación 0x67). Las
// operaciones para el jugador cuentan contra el hueco de la cola de operaciones
// (0x34 − [0x6DF346]).
void fn_4dcea0(SaveGame& save, uint16_t event) {
	static constexpr uint8_t kCaps[4] = {0xFA, 0xC8, 0xFA, 0x96};  // 0x672824
	GlobalMemory& g = save.globals;
	const uint32_t old_age = g.u32(kTick) - 0x474A00;  // 50 años
	const uint32_t ships = ship_count(save);
	const uint32_t captains = captain_count(save);
	Operation op;
	int32_t room = int32_t(0x34 - g.u16(kOperationCount));
	{
		Event& e = save.events[event];
		if (g.u16(kDayOfYear) < 0xA) {
			e.u32(0x10) = 0;
		} else if (++e.u32(0x10) > 0x1F) {
			return;
		}
	}
	const uint32_t phase = save.events[event].u32(0x10) & 7;
	for (uint32_t m = merchant_count(save); m > 0;) {
		--m;
		const MerchantMemory& merchant = merchant_of(save, m);
		const bool human = merchant.u16(8) == 0;
		for (uint32_t s = merchant.u16(0xE); s < ships;) {
			ShipMemory& ship = ship_of(save, s);
			const uint32_t captain = ship.u16(0x42);
			if (captain < captains && ship.u16(0x134) != 0x11) {
				CaptainMemory& c = save.captains[captain];
				if (c.u32(4) < old_age && c.u8(0xE) == 0) {
					// Comprobado solo con estados retocados.
					if (!human) {
						g.u32(kEventBusy) = 0;
						const uint16_t e = events_detail::fn_4d8cf0(save, g.u32(kTick) + 0x20);
						g.u32(kEventBusy) = 1;
						save.events[e].u16(6) = 0x27;
						save.events[e].u32(8) = s;
						save.events[e].u32(0xC) = ship.u16(0x42);
						save.captains[captain].u8(0xE) = 1;
					} else if (g.u32(kAldermanBusy) == 0) {
						const uint32_t years = (g.u32(kTick) - c.u32(4)) >> 13;
						const uint32_t r = game_rand(save) & 0x3FF;
						if ((uint32_t(r * years) & 0xFFFFFC00u) > 0x93000) {
							op.u32(0, 0x13);
							op.u32(4, s);
							op.u32(8, ship.u16(0x42));
							if (room-- > 0) {
								enqueue(save, op);
							}
						}
					}
				}
				if ((ship.u8(0x42) & 7) == phase) {
					op.u32(0, 0x12);
					op.u32(4, s);
					if (!human) {
						op.u32(8, 8);
						op.u32(0xC, 8);
						fn_538a80(save, s, 8, 8);  // 0x535760 con la operación 0x12
					} else if (g.u32(kAldermanBusy) == 0) {
						const CaptainMemory& cc = save.captains[ship.u16(0x42)];
						const uint8_t cap = kCaps[phase & 3];
						if (cc.u8(9) < cap || cc.u8(0xB) < cap || cc.u8(0xA) < cap) {
							op.u32(8, 0);
							op.u32(0xC, 0);
							const uint32_t r = game_rand(save) & 0x3FF;
							uint8_t skill;
							if (r <= 0x155) {
								op.u32(8, uint32_t(int32_t(game_rand(save)) % 0x33));
								skill = cc.u8(9);
							} else if (r < 0x2AA) {
								op.u32(0xC, uint32_t(int32_t(game_rand(save)) % 0x33));
								skill = cc.u8(0xA);
							} else {
								op.u32(0xC, uint32_t(int32_t(game_rand(save)) % 0x33));
								skill = cc.u8(0xB);
							}
							if (skill < cap && room-- > 0) {
								enqueue(save, op);
							}
						}
					}
				}
			}
			s = ship_of(save, s).u16(4);
		}
		if (!human || g.u32(kAldermanBusy) != 0) {
			continue;
		}
		for (uint32_t o = merchant.u16(0xC); o < office_count(save); o = save.offices[o].memory.u16(0x2C8)) {
			const OfficeMemory& office = save.offices[o].memory;
			const uint32_t administrator = office.u16(0x2F2);
			if (administrator >= captains || (administrator & 7) != phase) {
				continue;
			}
			if ((game_rand(save) & 0x3FF) >= 0x1B3 || room <= 0) {
				continue;
			}
			const uint8_t skill = save.captains[administrator].u8(0xA);
			const uint32_t raised = uint8_t(skill + 0x2B);
			op.u32(0x10, raised);
			if (raised <= skill) {
				continue;  // Sin comprobar con el oráculo.
			}
			op.u32(0, 0x67);
			op.u32(4, administrator);
			op.u32(8, m);
			op.u32(0xC, office.u8(0x2C6));
			enqueue(save, op);
			--room;
		}
	}
}

// Para cada mercancía (0x673730, 4 bytes): región de StadtDaten.ini en la que
// se busca primero su especialidad y región que se evita después.
constexpr uint8_t kSpecialtyRegions[20][2] = {{4, 3}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {1, 0}, {1, 2}, {1, 3},
	{1, 4}, {2, 0}, {2, 1}, {2, 3}, {2, 4}, {3, 0}, {3, 1}, {3, 2}, {3, 4}, {4, 0}, {4, 1}, {4, 2}};

// 0x4DC800: las especialidades de las ciudades ([0x82C], una mercancía < 20
// por ciudad, cada una en una sola): operación 0x79 con hasta 8 pares
// (ciudad, mercancía) desde [+0x4]: las ciudades que repiten mercancía (con
// 0xFF) y, para las mercancías que no tiene ninguna, una ciudad libre de su
// región o, si no hay, una al azar de la región (o de otra).
void fn_4dc800(SaveGame& save) {
	// La operación y, detrás en la pila, una marca por ciudad: el juego no
	// mira si se pasa de los 0x14 bytes de la operación.
	uint8_t frame[0x14 + 40];
	Operation op;
	std::memcpy(frame, op.bytes, 0x14);
	std::memset(frame + 4, 0xFF, 0x10);
	uint8_t* taken = frame + 0x14;  // [esp+0x40]
	uint8_t used[20] = {};  // [esp+0x18]
	size_t out = 4;  // esi
	auto put = [&](uint8_t v) {
		if (out < 0x14 + 40) {
			frame[out] = v;  // Más allá: la pila del juego (no sale en las partidas).
		}
		++out;
	};
	const uint32_t towns = town_count(save);  // edi
	int32_t pairs = 0;  // [esp+0x10]
	for (uint32_t i = towns; int32_t(i) > 0;) {
		--i;
		const uint32_t w = town_of(save, i).u8(0x82C);
		if (w >= 0x14) {
			taken[i] = 0;
		} else if (used[w]) {
			// Comprobado solo con estados retocados.
			++pairs;
			put(uint8_t(i));
			taken[i] = 1;
			put(0xFF);
		} else {
			used[w] = 1;
			taken[i] = 1;
		}
	}
	for (uint32_t w = 0; w < 0x14; ++w) {
		if (used[w]) {
			continue;
		}
		if (pairs >= 8) {
			break;
		}
		if (out < sizeof(frame)) {
			frame[out] = 0xFF;
		}
		const uint8_t region = kSpecialtyRegions[w][0];
		uint32_t town = 0;
		for (; town < towns; ++town) {
			if (town_region(save, town) == region && !taken[town]) {
				break;
			}
		}
		if (town >= towns) {
			// Ninguna libre en su región: una al azar de ella o, si no la hay
			// (o solo se encuentra dando la vuelta), una de fuera de la otra región.
			town = game_rand(save) % towns;
			bool found = town_region(save, town) == region;
			if (!found) {
				bool wrapped = false;
				for (;;) {
					if (++town >= towns) {
						if (wrapped) {
							break;
						}
						town = 0;
						wrapped = true;
					}
					if (town_region(save, town) == region) {
						found = !wrapped;
						break;
					}
				}
			}
			if (!found) {
				const uint8_t avoid = kSpecialtyRegions[w][1];
				town = game_rand(save) % towns;
				while (town_region(save, town) == avoid) {
					if (++town >= towns) {
						town = 0;
					}
				}
			}
		}
		put(uint8_t(town));
		taken[town] = 1;
		used[w] = 1;
		put(uint8_t(w));
		++pairs;
	}
	std::memcpy(op.bytes, frame, 0x14);
	op.u32(0, 0x79);
	enqueue(save, op);
}

// 0x506290 (sobre el objeto de los barcos): barcos de las listas de los 5
// objetos de [+0xC] (los piratas: cabeza u16 [+0xC], siguiente [+0x4] de cada barco).
uint16_t fn_506290(const SaveGame& save) {
	uint32_t n = 0;
	for (const std::optional<GameMemory<0x18>>& pirate : save.pirates) {
		if (!pirate) {
			continue;
		}
		for (uint32_t s = pirate->u16(0xC); s < ship_count(save); s = save.ships[s].memory.u16(4)) {
			++n;
		}
	}
	return uint16_t(n);
}

// 0x4DC6B0: un rumor (operación 0x7A): de una ciudad al azar a otra a más de
// 2 de viaje (las de [0x2C8] & 0xE00 cuentan doble y con & 0x400, triple), con
// una fecha [+0xC] en 11 a 14 días y [+0x10] el viaje (0x531B70) por (32 a
// 46) / 40 y por (32 + barcos de los piratas) / 32.
void fn_4dc6b0(SaveGame& save) {
	Operation op;
	const uint32_t from = game_rand(save) % town_count(save);  // ebp
	uint32_t list[3 * 40] = {};  // [esp+0x28] (el juego no la inicializa)
	int32_t n = 0;  // edi
	for (uint32_t i = town_count(save); int32_t(i) > 0;) {
		--i;
		if (ship_trade_detail::fn_531b70(save, from, i) <= 2) {
			continue;
		}
		list[n++] = i;
		const uint32_t flags = town_of(save, i).u32(0x2C8);
		if (flags & 0xE00) {
			list[n++] = i;
		}
		if (flags & 0x400) {
			list[n++] = i;
		}
	}
	const int32_t pick = int32_t((game_rand(save) & 0xFF) * uint32_t(n)) >> 8;
	const uint32_t to = list[pick];  // Sin ninguna, basura en el juego (no sale en las partidas).
	op.u32(0, 0x7A);
	op.u32(4, from);
	op.u32(8, to);
	op.u32(0xC, (((game_rand(save) & 3) + 0xB) << 8) + (save.globals.u32(kTick) | 0xFF));
	uint32_t travel = uint32_t(ship_trade_detail::fn_531b70(save, from, to)) << 8;
	const int32_t r = int32_t(game_rand(save)) % 15;
	travel = uint32_t((uint64_t(uint32_t((r + 0x20) * int32_t(travel))) * 0xCCCCCCCDull) >> 37);
	const uint32_t pirates = fn_506290(save);
	op.u32(0x10, ((pirates + 0x20) * travel) >> 5);
	enqueue(save, op);
}

// 0x4DCD90 (thiscall sobre la cola, con el evento 0x03): avanza el
// comerciante [+0xC] del evento (1 o 2 según [0x40] de la primera ciudad) y,
// si su compañía vale más de 100000, a veces (operación 0x81) una oferta.
void fn_4dcd90(SaveGame& save, uint16_t event) {
	Operation op;
	{
		Event& e = save.events[event];
		e.u32(0xC) += (town_of(save, 0).u32(0x40) & 1) + 1;
		e.u32(0xC) %= merchant_count(save);
	}
	const uint32_t m = save.events[event].u32(0xC);
	const MerchantMemory& merchant = merchant_of(save, m);
	if (merchant.s32(0x46C) <= 100000) {
		return;
	}
	const int32_t limit = int32_t(merchant_count(save) * 205) / 36;
	if (limit < int32_t(game_rand(save) & 0x3FF)) {
		return;
	}
	op.u32(0, 0x81);
	op.u32(4, m);
	op.u32(8, (game_rand(save) & 3) + 5);
	op.u32(0xC, (game_rand(save) & 0x3FF) + 0x100);
	op.u32(0x10, merchant.u8(0x19));
	enqueue(save, op);
}

// 0x4E2314 (thiscall sobre la cola): ¿hay un evento 0x07 de la ciudad `town`
// ([+0xC]) con [+0x10] = 2? Si lo hay, su [+0x8] (comerciante) y su tick.
bool fn_4e2314(const SaveGame& save, uint32_t town, uint8_t& merchant, uint32_t& tick) {
	const GlobalMemory& g = save.globals;
	const uint32_t count = g.u16(kEventCount);
	for (uint32_t i = g.u16(kEventHead); i < count; i = save.events[i].u16(4)) {
		const Event& e = save.events[i];
		if (e.u16(6) == 7 && e.u32(0xC) == (town & 0xFF) && e.u32(0x10) == 2) {
			// Comprobado solo con estados retocados.
			merchant = e.u8(8);
			tick = e.u32(0);
			return true;
		}
	}
	return false;
}

// 0x531470 (sobre el mundo): los comerciantes de las oficinas de la ciudad
// (en el orden de su lista [0x784]) sin el jugador y sin los de [+0x8] & 4.
// El juego devuelve una lista del montón (u16 número, los comerciantes y la
// suma de [+0x2D2] y [+0x2D4] de cada oficina); aquí, los comerciantes.
std::vector<uint16_t> fn_531470(const SaveGame& save, uint32_t town) {
	std::vector<uint16_t> list;
	if (town >= town_count(save)) {
		return list;  // Sin comprobar con el oráculo.
	}
	for (uint32_t o = save.towns[town].memory.u16(0x784); o < office_count(save);
		o = save.offices[o].memory.u16(0x2CA)) {
		const uint16_t m = save.offices[o].memory.u16(0x2C4);
		if (m >= merchant_count(save)) {
			continue;  // Sin comprobar con el oráculo.
		}
		if ((save.merchants[m].memory.u8(8) & 4) || m == save.globals.u32(global::kPlayer)) {
			continue;
		}
		list.push_back(m);
	}
	return list;
}

// 0x5316E0 (sobre el mundo): la fiesta de la ciudad [+0x88] del mundo
// cuando pasa el tick [+0x84]: invita (operación 0x61) un comerciante de la
// IA al azar de los de sus oficinas (con un evento 0x07 de la ciudad, no hay
// fiesta y se retrasa); la siguiente, en una ciudad al azar (0..19) en 13 a
// 32 días.
void fn_5316e0(SaveGame& save) {
	GlobalMemory& g = save.globals;
	auto next_feast = [&]() {
		g.u8(kFeastTown) = uint8_t(ftol(double(float(int32_t(game_rand(save)))) / 32768.0 * 20.0));  // 0x671FC8
		g.u32(kFeastTick) = g.u32(kTick) + ((uint32_t(g.u8(kFeastTown)) + 0xD) << 8);
	};
	for (uint32_t i = 0; i < town_count(save); ++i) {
		if (g.u8(kFeastTown) != i || g.u32(kTick) <= g.u32(kFeastTick)) {
			continue;
		}
		uint8_t who = uint8_t(g.u32(global::kPlayer));
		uint32_t when = 0;
		if (fn_4e2314(save, i, who, when)) {
			// Comprobado solo con estados retocados.
			g.u32(kFeastTick) = g.u32(kTick) + ((uint32_t(g.u8(kFeastTown)) + 0xD) << 8);
			return;
		}
		const std::vector<uint16_t> list = fn_531470(save, i);
		if (merchant_of(save, who).u16(8) == 0) {
			for (;;) {
				who = uint8_t(rand_fraction(save, float(list.size())));
				if (list.empty()) {
					next_feast();
					return;
				}
				if (list.size() == 1 && merchant_of(save, list[0]).u16(8) == 0) {
					next_feast();  // Comprobado solo con estados retocados.
					return;
				}
				who = uint8_t(list[who]);
				if (merchant_of(save, who).u16(8) != 0) {
					break;
				}
			}
		}
		when = (uint32_t(town_of(save, i).s32(0x2D4) % 11 + 5) << 9) + g.u32(kTick);
		Operation op;
		op.u32(0, 0x61);
		op.u32(4, who);
		op.u32(8, when);
		op.u32(0xC, i);
		op.u32(0x10, 0);
		enqueue(save, op);
		next_feast();
	}
}

// Lo que se come y se bebe en una fiesta, por invitado, en las 8 primeras
// mercancías (0x6734E8).
constexpr uint8_t kFeastWares[8] = {3, 2, 2, 2, 0, 1, 0, 2};

// 0x4F8EB0 (thiscall sobre el comerciante, con la ciudad y n): su reputación
// en la ciudad ([+0x120 + 12·ciudad], float) sube n · [+0x464] · 0,5
// (0x66BC58); con n = 0, baja 1.
void fn_4f8eb0(SaveGame& save, int merchant, uint32_t town, uint32_t n) {
	MerchantMemory& m = merchant_of(save, uint32_t(merchant));
	float r = -1.0f;
	if (n != 0) {
		r = float(double(float(n)) * double(m.f32(0x464)) * 0.5);
	}
	m.f32(0x120 + 12 * town) = float(double(r) + double(m.f32(0x120 + 12 * town)));
}

// 0x5004C0 (thiscall sobre la oficina, con n invitados): la fiesta gasta de
// la oficina, de cada una de las 8 mercancías de 0x6734E8, n veces lo de la
// tabla, el doble (el cuádruple si la ciudad tiene el bit 0x1000 de [0x2C8]),
// en unidades enteras hacia arriba (barriles de 200 o cargas de 2000) y como
// mucho lo que hay; devuelve lo que vale a precio de compra (0x52E430).
int32_t fn_5004c0(SaveGame& save, int office, uint16_t n) {
	OfficeMemory& o = save.offices[size_t(office)].memory;
	const uint32_t town = o.u8(0x2C6);
	const bool lavish = (town_of(save, town).u32(0x2C8) >> 12) & 1;
	int32_t total = 0;
	for (uint32_t w = 0; w < 8; ++w) {
		int32_t need = int32_t(kFeastWares[w]) * n;
		if (need <= 0) {
			continue;
		}
		need = lavish ? need << 2 : need * 2;
		const int32_t unit = kFineUnit[w] ? 200 : 2000;
		need = (need + unit - 1) / unit * unit;
		int32_t& stock = o.s32(4 + 4 * w);
		if (need <= stock) {
			total += game_buy_price(save, int(w), int(town), need);
			stock -= need;
		} else {
			total += game_buy_price(save, int(w), int(town), stock);
			stock = 0;
		}
	}
	return total;
}

// 0x4E23A4 (el evento 0x07, stdcall con el evento): la fiesta del
// comerciante [+0x8] en la ciudad [+0xC] (la crea la operación 0x61 con
// [+0x10] = 2). Sin oficina suya en la ciudad, nada. Si es de la IA, la
// ciudad apunta la fiesta ([0x2D0] = el tick; las semanas desde la última
// cuentan en la satisfacción, population.cpp). Si es humano, con k su
// reputación en la ciudad + 39 (de 0 a 99) y los invitados, habitantes · k /
// 100: c, las mercancías de la fiesta (0x6734E8) que su oficina tiene para
// todos; la oficina gasta para habitantes · (c · k / 6, de 2 a 99) / 100
// invitados (al menos 5; 0x5004C0); con c > 1 la ciudad apunta la fiesta; y
// su reputación sube c / 2 (0x4F8EB0). La carta 0x24 (0x4D6530) y, con c >=
// 2 y [+0x10] != 0, el mensaje y la ventana no se reproducen.
void fn_4e23a4(SaveGame& save, uint16_t event) {
	const Event& e = save.events[event];
	const int office = facilities_detail::fn_5308a0(save, e.u16(8), e.u16(0xC));
	if (office < 0) {
		return;  // Sin comprobar con el oráculo.
	}
	const uint32_t merchant = e.u32(8);
	const uint32_t town = e.u32(0xC);
	if (merchant_of(save, merchant).u16(8) != 0) {
		town_of(save, town).u32(0x2D0) = save.globals.u32(kTick);
		return;
	}
	// Comprobado solo con estados retocados.
	int32_t k = int32_t(events_detail::fn_4f9580(save, int(merchant), town)) + 0x27;
	if (k < 0) {
		k = 0;  // Sin comprobar con el oráculo.
	} else if (k > 0x63) {
		k = 0x63;
	}
	const int32_t people = town_of(save, town).s32(0x2D4);
	const int32_t guests = people * k / 100;
	const OfficeMemory& o = save.offices[size_t(office)].memory;
	uint32_t count = 0;
	for (uint32_t w = 0; w < 8; ++w) {
		if (kFeastWares[w] > 0 && int32_t(kFeastWares[w]) * guests <= o.s32(4 + 4 * w)) {
			++count;
		}
	}
	int32_t share = int32_t(count) * k / 6;
	if (share > 0x63) {
		share = 0x63;  // Sin comprobar con el oráculo.
	} else if (share < 2) {
		share = 2;
	}
	int32_t spent = people * share / 100;
	if (spent < 5) {
		spent = 5;  // Sin comprobar con el oráculo.
	}
	fn_5004c0(save, office, uint16_t(spent));
	if (count > 1) {
		town_of(save, town).u32(0x2D0) = save.globals.u32(kTick);
	}
	fn_4f8eb0(save, int(merchant), town, count >> 1);
}

// 0x4DCA90: una plaga (operación 0x7C) en una ciudad al azar sin [0x2C8] & 8,
// con más probabilidad si [0x790] · 100 < [0x792] · 75 y si ([0x771] +
// [0x773]) · 50000 < habitantes · 8.
void fn_4dca90(SaveGame& save) {
	Operation op;
	const uint8_t i = uint8_t(game_rand(save) % town_count(save));
	op.u8(0x12, i);
	const TownMemory& t = town_of(save, i);
	if (t.u32(0x2C8) & 8) {
		return;  // Comprobado solo con estados retocados.
	}
	int32_t risk = int32_t(t.u16(0x790)) * 100 < int32_t(t.u16(0x792)) * 75 ? 1 : 0;
	if (int32_t((uint32_t(t.u8(0x773)) + t.u8(0x771)) * 50000) < int32_t(t.u32(0x2D4) << 3)) {
		++risk;
	}
	if (int32_t(game_rand(save)) % 100 >= 2 * risk + 1) {
		return;
	}
	const uint32_t r = (game_rand(save) & 3) + 1;
	op.u16(0x10, uint16_t(save.globals.u32(kDayOfYear) + r));
	const int32_t d = int32_t(game_rand(save)) % 10;
	op.u32(8, (uint32_t(d + int32_t(r) + risk * 10) << 8) + save.globals.u32(kTick));
	for (size_t k = 0; k < 4; ++k) {
		op.u8(4 + k, uint8_t((game_rand(save) & 0x1F) + 0x31));
	}
	op.u16(0xC, 0);
	op.u16(0xE, uint16_t(t.s32(0x2D4) / 40));
	op.u32(0, 0x7C);
	enqueue(save, op);
}

// 0x4DCBF0: un incendio (operación 0x7D) en una ciudad al azar sin [0x2C8] &
// 0x8000 (5 veces más probable si [0x789] · 5000 < habitantes · 8), en el
// último de unos edificios (1 a 64 de los primeros registros) de tipo < 0x1E
// terminado y dentro de la rejilla.
void fn_4dcbf0(SaveGame& save) {
	Operation op;
	const uint8_t i = uint8_t(game_rand(save) % town_count(save));
	op.u8(0xE, i);
	const SavedTown& town = save.towns[i];
	const TownMemory& t = town.memory;
	if (t.u32(0x2C8) & 0x8000) {
		return;  // Sin comprobar con el oráculo.
	}
	const int32_t chance = int32_t(uint32_t(t.u8(0x789)) * 5000) < int32_t(t.u32(0x2D4) << 3) ? 5 : 1;
	if (int32_t(game_rand(save)) % 100 >= chance) {
		return;
	}
	op.u16(0xA, uint16_t(chance == 1 ? t.s32(0x2D4) / 300 : t.s32(0x2D4) / 800));  // Con 1, sin comprobar.
	op.u16(8, 0);
	op.u32(4, (game_rand(save) & 0xFFF) + save.globals.u32(kTick) + 0x200);
	const int32_t width = t.s32(0x7A8);
	const int32_t height = t.s32(0x7AC);
	int32_t tries = int32_t((game_rand(save) & 0x3F) + 1);
	op.u8(0xC, 0xFF);
	int32_t k = 0;
	while (tries > 0) {
		--tries;
		if (k >= int32_t(t.u16(0x776))) {
			break;
		}
		const BuildingRecord& b = town.records[size_t(k++)];
		if (b.type < 0x1E && int32_t(b.x) < width && int32_t(b.y) < height && b.pending == 0) {
			// Comprobado solo con estados retocados.
			op.u8(0xC, b.x);
			op.u8(0xD, b.y);
		}
	}
	if (op.bytes[0xC] == 0xFF) {
		return;
	}
	op.u32(0, 0x7D);  // Comprobado solo con estados retocados.
	enqueue(save, op);
}

// --- Event 0x15: a merchant's wedding (operation 0x60) ---

// 0x4D6FD0 (on the mailbox 0x6DD730, with a letter): clears the marks of the
// chain of letters that starts there (next in [+0x6]): [+0xE] of the 0x21
// ones and [+0xF] of the 0x22 ones; at most as many steps as letters.
void fn_4d6fd0(SaveGame& save, uint32_t index) {
	const uint32_t count = save.globals.u16(kMailCount);
	for (uint32_t left = count; index < count && left > 0; --left) {
		GameMemory<0x10>& letter = save.mail[index];
		if (letter.u8(4) == 0x21) {
			letter.u16(0xE) = 0;
		} else if (letter.u8(4) == 0x22) {
			letter.u8(0xF) = 0;
		}
		index = letter.u16(6);
	}
}

// Minimum crew of each ship type (0x673660).
constexpr uint8_t kMinCrew[4] = {5, 8, 10, 12};

// 0x50E000 (on the ship object, with a pointer to the type, `strength`, the
// town and the merchant): a ship given to the merchant in the town. A big
// type (bit 1) becomes a crayer (1, also in the caller's variable) in a town
// with [0x2C8] & 1. The ship (0x507D60, in port) gets its size from the
// town's shipyard (0x52A8D0); with `strength`, its hold (0x5182B0) and
// artillery of the town's level (0x52BB00, 0x51A4E0), going up a level
// (0x51A750) while its strength [+0x120] falls short. Then: full hull
// ([+0x18] = [+0x14]), owner, [+0x3D] |= 4, first of the merchant's list,
// home port [+0x13A] (a human's home town, otherwise this town), state 3
// (entering port; [+0x138] = 0x15 in network games), minimum crew, hold,
// artillery slots (0x5181D0), [+0x3E] = 0x700, a name from the list (as
// 0x509A50), built ((type + town + [0x31C] byte + 365 · (2 + index % 4))
// days ago and one more ship in the merchant's [+0x478]. Returns its index.
uint16_t fn_50e000(SaveGame& save, uint16_t& type, uint32_t strength, uint32_t town_arg, uint32_t merchant_arg) {
	const uint32_t town = town_arg & 0xFF;
	const uint32_t merchant = merchant_arg & 0xFF;
	const TownMemory& t = town_of(save, town);
	if ((t.u32(0x2C8) & 1) && (type & 2)) {
		type = 1;  // Sin comprobar con el oráculo.
	}
	uint16_t index = 0xFFFF;
	events_mayor_detail::fn_507d60(save, index, town, 1);
	events_mayor_detail::fn_52a8d0(save, int(town), index, type & 0xFF);
	if (strength != 0) {
		// Sin comprobar con el oráculo (the wedding gives ships without artillery).
		recalculate_cargo(save.ships[index]);
		const uint8_t level = uint8_t(facilities_detail::fn_52bb00(save, int(town)));
		alderman_detail::fn_51a4e0(save, index, strength, level);
		while (strength > ship_of(save, index).u32(0x120) && ship_of(save, index).u8(0xF) < 2) {
			alderman_detail::fn_51a750(save, index, 1);
			alderman_detail::fn_51a4e0(save, index, strength, level);
		}
	}
	MerchantMemory& m = merchant_of(save, merchant);
	ShipMemory& s = ship_of(save, index);
	s.u32(0x18) = s.u32(0x14);
	s.u8(0) = uint8_t(merchant_arg);
	s.u8(0x3D) |= 4;
	s.u16(4) = m.u16(0xE);
	m.u16(0xE) = index;
	s.u8(0x13A) = m.u16(8) == 0 ? m.u8(0x19) : uint8_t(town_arg);
	s.u16(0x134) = 3;
	if (save.globals.u8(kOperations) & 2) {
		s.u16(0x138) = 0x15;  // Sin comprobar con el oráculo (network games).
	}
	s.u16(0x40) = kMinCrew[s.u8(0xE) & 3];
	recalculate_cargo(save.ships[index]);
	events_mayor_detail::fn_5181d0(save, index);
	const uint32_t now = save.globals.u32(kTick);
	const int16_t wait = int16_t(uint16_t((now & 0xFF) + index + 0xFA0));
	ship_of(save, index).u16(0x3E) = wait > 0x700 ? 0x700 : uint16_t(wait);
	events_mayor_detail::fn_509a50(save, index);
	const uint32_t days = uint32_t(type) + (t.u32(0x31C) & 0xFF) + town + 365 * ((index & 3) + 2u);
	ship_of(save, index).u32(0x4C) = now - (days << 8);
	++m.u16(0x478);
	return index;
}

// The ships of a wedding by the groom's fleet (0x4E81FC; 0xFF: none).
constexpr uint8_t kDowry[12][2] = {{0, 0xFF}, {0, 0xFF}, {0, 0xFF}, {0, 0xFF}, {0, 0xFF}, {0, 0xFF}, {1, 0xFF},
	{2, 0xFF}, {1, 0}, {2, 0xFF}, {2, 0}, {2, 1}};

// 0x4E7C74 (event 0x15, created by operation 0x60): the wedding of merchant
// [+0x15]. Nothing (the event goes) unless his [+0x28] is still to come.
// The bride: [+0x28] = now − [+0xC] days (her birth), [+0x2C] = now, and
// [+0x27], [+0x30..+0x33] from [+0xF], [+0x10], [+0x12], [+0x13], [+0x14].
// The dowry: by his fleet (the holds [+0x10] / 40000 of his ships, minus
// [+0x13]; kDowry), one or two ships given in town [+0x12] (0x50E000), with
// hull ([+0x14] / 32) · (([+0x13] − [+0x14]) · 3 + 23), cargo of the ware
// [+0xE] & 0xF (only with room: ((index & 15) + 15) · 170 / its base price,
// nothing under 100, at most half the hold) and of [+0xE] >> 4 (tools, or 3
// if the first one is tools, when it is the same ware: ((tick & 15) + 15) ·
// 170 / its price, at most a quarter of the hold), whole barrels or loads,
// minimum crew and hold (0x5182B0). Then the event becomes the wedding feast
// (an event 0x07 for the merchant in his home town with [+0x10] = 0) and
// stays. The letters and the window are not reproduced. Returns whether it
// stays.
bool fn_4e7c74(SaveGame& save, uint16_t event) {
	const uint32_t merchant = save.events[event].u8(0x15);
	if (merchant >= merchant_count(save)) {
		return false;  // Sin comprobar con el oráculo.
	}
	const uint32_t now = save.globals.u32(kTick);
	MerchantMemory& m = merchant_of(save, merchant);
	if (m.u32(0x28) <= now) {
		return false;  // Sin comprobar con el oráculo.
	}
	Event& e = save.events[event];
	m.u32(0x28) = now - (uint32_t(e.u16(0xC)) << 8);
	m.u32(0x2C) = now;
	m.u8(0x27) = e.u8(0xF);
	m.u8(0x30) = e.u8(0x10);
	m.u8(0x31) = e.u8(0x12);
	m.u8(0x32) = e.u8(0x13);
	m.u8(0x33) = e.u8(0x14);
	int32_t fleet = 0;
	for (uint32_t s = m.u16(0xE); s < ship_count(save); s = ship_of(save, s).u16(4)) {
		fleet += ship_of(save, s).s32(0x10) / 40000;
	}
	fleet -= e.u8(0x13);
	if (fleet < 0) {
		fleet = 0;  // Sin comprobar con el oráculo.
	}
	const uint8_t* dowry = kDowry[fleet > 10 ? 11 : fleet];
	for (int c = 0; c < 2; ++c) {
		uint16_t type = dowry[c];
		if (type > 3) {
			continue;
		}
		const uint16_t index = fn_50e000(save, type, 0, e.u8(0x12), e.u8(0x15));
		ShipMemory& s = ship_of(save, index);
		s.s32(0x18) = s.s32(0x14) >> 5;
		s.s32(0x18) = ((int32_t(e.u8(0x13)) - int32_t(e.u8(0x14))) * 3 + 0x17) * s.s32(0x18);
		auto load = [&](uint32_t w, int32_t value, int32_t most) {
			int32_t amount = int32_t(ftol(double(float(value)) / double(ware_base_price(int(w)))));
			if (amount < 100) {
				amount = 0;
			} else if (amount > most) {
				amount = most;
			}
			const int32_t unit = kFineUnit[w] ? 200 : 2000;
			s.s32(0x54 + 4 * w) = amount / unit * unit;
		};
		const uint32_t first = e.u8(0xE) & 0xF;
		const int32_t room = recalculate_cargo(save.ships[index]);
		if (room <= 0) {
			s.s32(0x54 + 4 * first) = 0;  // Sin comprobar con el oráculo.
		} else {
			load(first, ((int32_t(index) & 0xF) + 0xF) * 170, s.s32(0x10) >> 1);
		}
		uint32_t second = e.u8(0xE) >> 4;
		if (second == first) {
			second = first == 0xC ? 3 : 0xC;
		}
		load(second, (int32_t(now & 0xF) + 0xF) * 170, s.s32(0x10) >> 2);
		s.u16(0x40) = kMinCrew[s.u8(0xE) & 3];
		recalculate_cargo(save.ships[index]);
	}
	Event& feast = save.events[event];
	feast.u16(6) = 7;
	feast.u32(8) = merchant;
	feast.u32(0xC) = m.u8(0x19);
	feast.u32(0x10) = 0;
	return true;
}

// --- Evento 0x1D: un incendio en la ciudad (la operación 0x7D) ---

// Los tipos de edificio que arden en un incendio de la ciudad (0x672B3C): las
// casas y empresas (4 a 0x1E), 0x29 y 0x2C.
constexpr uint8_t kFlammable[0x39] = {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// 0x500C30 (thiscall sobre la oficina): ha ardido un almacén suyo. Lo que se
// pierde: 200 si la oficina tiene [+0x2EC] · 2000 o más; si no, ((tick +
// [+0x2E2]) & 0x3F) · (existencias · 200 / almacenes) / 64, o 400000 si
// con eso quedaría más de [+0x2EC] · 2000 − 400000. Sale de las tres
// mercancías que más hay (en empate, la de índice menor; si hay menos de
// tres, la que menos): un tercio de lo que se pierde de la tercera, la
// mitad de lo que queda de la segunda y el resto de la primera, en unidades
// enteras (cargas o barriles; como mucho 200 y lo que hay). La carta 0x75 al
// comerciante no se reproduce.
void fn_500c30(SaveGame& save, int office) {
	OfficeMemory& o = save.offices[size_t(office)].memory;
	const int32_t stores = o.u16(0x2EC);
	const int32_t capacity = int32_t(uint32_t(stores) * 2000);
	int32_t stock = 0;
	for (uint32_t w = 0; w < 24; ++w) {
		stock = int32_t(uint32_t(stock) + uint32_t(o.s32(4 + 4 * w)));
	}
	int32_t loss = 200;
	if (stock < capacity) {
		const int32_t chance = int32_t((uint32_t(o.u16(0x2E2)) + save.globals.u32(kTick)) & 0x3F);
		loss = int32_t(uint32_t(chance) * uint32_t(int32_t(uint32_t(stock) * 200) / stores)) >> 6;
		if (int32_t(uint32_t(stock) - uint32_t(loss)) > int32_t(uint32_t(capacity) - 400000)) {
			loss = 400000;
		}
	}
	uint32_t least = 0;
	for (uint32_t w = 1; w < 24; ++w) {
		if (o.s32(4 + 4 * w) < o.s32(4 + 4 * least)) {
			least = w;
		}
	}
	uint32_t top[3] = {least, least, least};
	for (uint32_t w = 0; w < 24; ++w) {
		for (int k = 2; k >= 0 && o.s32(4 + 4 * top[k]) < o.s32(4 + 4 * w); --k) {
			if (k < 2) {
				top[k + 1] = top[k];
			}
			top[k] = w;
		}
	}
	for (int k = 2; k >= 0; --k) {
		const uint32_t w = top[k];
		int32_t share = loss / (k + 1);
		if (share >= o.s32(4 + 4 * w)) {
			share = o.s32(4 + 4 * w);
		}
		const int32_t unit = kFineUnit[w] ? 200 : 2000;
		int32_t units = share / unit;
		if (units > 0xC8) {
			units = 0xC8;
		}
		const int32_t amount = units * unit;
		if (amount > 0) {
			o.s32(4 + 4 * w) -= amount;
			loss -= amount;
		}
	}
}

// 0x4E9564 (el evento 0x1D, lo crea la operación 0x7D): un incendio en la
// ciudad [+0x12] que empieza en la celda ([+0x10], [+0x11]). Cada 64 ticks
// (el evento vuelve cada 8 y esto, con los bits 3..5 del tick a 0), mientras
// [+0xC] (los que han ardido) sea menor que [+0xE], el fuego pasa al
// edificio que arde (0x672B3C) más cercano a la celda (distancia² < 100; en
// empate, el primero), buscado en la lista de la ciudad [0x764] y en las de
// cada oficina de la ciudad ([+0x2D0] y [+0x2CE]); sin ninguno, deja de
// extenderse ([+0xE] = [+0xC]). Ese edificio sale de su lista (de un
// comerciante con oficina allí, la [+0x2D0] si se encontró en una de esas y
// si no la [+0x2CE]; si no es de un comerciante, la de la ciudad; si no está
// en ella, deja de extenderse) y pasa a la de lo que arde [0x76A] con 50 de
// vida ([+0x6]), la celda del fuego pasa a ser la suya, [+0xC] + 1, y se quita
// de las subastas (0x4DB100); la oficina de su dueño se recuenta (0x4FF2D0;
// si era un almacén, 0x500C30 antes). Cada 8 ticks, todo lo que arde salvo
// las torres pierde 51 de vida (con más de 50) o 1, y con 0 se borra
// (0x521240). Sin nada que extender ni que arda, la ciudad pierde el bit
// 0x8000 de [0x2C8] y se quita; si no, lo tiene (con el aviso 0x59 la primera
// vez, que no se reproduce) y vuelve. Los gráficos de la ciudad que se ve no
// se reproducen. Devuelve si sigue.
bool fn_4e9564(SaveGame& save, uint16_t event) {
	const uint32_t town = save.events[event].u8(0x12);
	SavedTown& st = save.towns[town];
	TownMemory& t = st.memory;
	const uint32_t records = t.u16(0x776);
	if (!(save.events[event].u8(0) & 0x38) && save.events[event].u16(0xE) > save.events[event].u16(0xC)) {
		Event& e = save.events[event];
		int32_t nearest = 100;
		uint32_t best = 0xFFFFF;
		bool in_houses = false;  // [esp+0x2C]: si se encontró en la lista [+0x2D0] de una oficina
		auto look = [&](uint32_t head, bool houses) {
			for (uint32_t r = head; r < records; r = st.records[r].next) {
				const BuildingRecord& b = st.records[r];
				const int32_t dx = int32_t(b.x) - int32_t(e.u8(0x10));
				const int32_t dy = int32_t(b.y) - int32_t(e.u8(0x11));
				const int32_t d = dx * dx + dy * dy;
				if (b.type < sizeof(kFlammable) && kFlammable[b.type] && d < nearest) {
					in_houses = houses;
					best = r;
					nearest = d;
				}
			}
		};
		look(t.u16(0x764), false);
		for (uint32_t o = t.u16(0x784); o < office_count(save); o = save.offices[o].memory.u16(0x2CA)) {
			look(save.offices[o].memory.u16(0x2D0), true);
			look(save.offices[o].memory.u16(0x2CE), false);
		}
		if (best >= records) {
			e.u16(0xE) = e.u16(0xC);
		} else {
			BuildingRecord& b = st.records[best];
			int office = -1;
			uint16_t* head = nullptr;
			if (b.owner >= merchant_count(save)) {
				head = &t.u16(0x764);
			} else if ((office = facilities_detail::fn_5308a0(save, b.owner, town)) >= 0) {
				OfficeMemory& o = save.offices[size_t(office)].memory;
				head = in_houses ? &o.u16(0x2D0) : &o.u16(0x2CE);
			}
			if (!head) {
				e.u16(0xE) = e.u16(0xC);  // Sin comprobar con el oráculo.
			} else {
				uint16_t* link = head;
				while (*link < records && *link != best) {
					link = &st.records[*link].next;
				}
				if (*link != best) {
					e.u16(0xE) = e.u16(0xC);  // Sin comprobar con el oráculo.
				} else {
					e.u8(0x10) = b.x;
					++e.u16(0xC);
					e.u8(0x11) = b.y;
					*link = b.next;
					b.next = t.u16(0x76A);
					t.u16(0x76A) = uint16_t(best);
					b.pending = 0x32;
					events_rare_detail::fn_4db100(save, town, uint16_t(best));
				}
				if (office >= 0) {
					if (st.records[best].type == 0x1E) {
						fn_500c30(save, office);  // Sin comprobar con el oráculo.
					}
					fn_4ff2d0(save, office);
				}
			}
		}
	}
	for (uint16_t* link = &t.u16(0x76A); *link < records;) {
		BuildingRecord& b = st.records[*link];
		if (b.type == 0x32 || b.type == 0x33) {
			link = &b.next;  // Sin comprobar con el oráculo.
			continue;
		}
		if (b.pending > 0x32) {
			b.pending = uint8_t(b.pending - 0x33);  // Sin comprobar con el oráculo.
		} else if (b.pending >= 1) {
			--b.pending;
		} else {
			const uint16_t index = *link;
			*link = b.next;
			b.next = 0xFFFF;
			construction_detail::fn_521240(save, int(town), index);
			continue;
		}
		link = &b.next;
	}
	Event& e = save.events[event];
	e.u32(0) += 8;
	if (e.u16(0xE) <= e.u16(0xC) && t.u16(0x76A) == 0xFFFF) {
		t.u32(0x2C8) &= ~0x8000u;
		return false;
	}
	t.u32(0x2C8) |= 0x8000;
	return true;
}

// 0x4DD400 (thiscall sobre la cola, los 10 primeros días del año): las
// subastas de barcos del año (operaciones 0x38 con su tick, cada 123 a 251
// días desde el último evento 0x0A de la cola) en ciudades con [0x2C8] & 0xC0.
void fn_4dd400(SaveGame& save) {
	Operation op;
	GlobalMemory& g = save.globals;
	uint32_t when = g.u32(kTick);
	const uint32_t end = (uint32_t(g.u16(global::kWorldYear)) + 1) * 365 * 256;
	for (uint32_t i = g.u16(kEventHead); i < g.u16(kEventCount); i = save.events[i].u16(4)) {
		if (save.events[i].u16(6) == 0xA) {
			when = save.events[i].u32(0);  // Comprobado solo con estados retocados.
		}
	}
	for (;;) {
		when += (game_rand(save) & 0x7FFF) + 0x7B00;
		if (when >= end) {
			break;
		}
		const uint32_t towns = town_count(save);
		op.u32(0, 0x38);
		op.u32(4, when);
		uint32_t i = game_rand(save) % towns;
		op.u32(8, i);
		while (!(town_of(save, i).u8(0x2C8) & 0xC0)) {
			// Comprobado solo con estados retocados.
			i = i == 0 ? towns - 1 : i - 1;
			op.u32(8, i);
		}
		enqueue(save, op);
	}
}

// 0x4DD530: un torneo (operación 0x84) para el comerciante [0x6DE52D] del
// mundo, si es un jugador humano, hay 17 ciudades o más y a veces (1/4 · 1/8):
// en una ciudad con mercado sin [0x2C8] & 0x40 (la n − 16 de ellas), con un
// rival al azar ([+0xA], [+0xB] su nombre) y el último de unos pocos alcaldes
// de las ciudades con [0x2C8] & 0x40 ([0x6F1]: de la IA o, si pasa del número de
// comerciantes, los de la ciudad [0x6D4..]).
void fn_4dd530(SaveGame& save) {
	Operation op;
	GlobalMemory& g = save.globals;
	const uint8_t player = g.u8(kTournamentMerchant);
	if (player >= merchant_count(save) || merchant_of(save, player).u16(8) != 0) {
		return;
	}
	// Comprobado solo con estados retocados (en las partidas nunca es un jugador humano).
	const uint32_t towns = town_count(save);
	if (towns < 0x11 || (game_rand(save) & 3) != 0) {
		return;
	}
	op.u32(0, 0x84);
	int32_t left = uint8_t(game_rand(save) % towns - 0x10);
	for (uint32_t i = 0;;) {
		const TownMemory& t = town_of(save, i);
		if (!(t.u8(0x2C8) & 0x40) && (t.u32(0x76C) & 0x100)) {
			op.u8(8, uint8_t(i));
			--left;
		}
		if (++i >= towns || left < 0) {
			break;
		}
	}
	op.u8(9, uint8_t((game_rand(save) & 1) + 5));
	uint32_t rival = game_rand(save) % towns;
	if (rival == op.bytes[8]) {
		++rival;
	}
	if (rival < merchant_count(save)) {  // El juego no mira el índice (0x5303C0).
		op.u8(0xA, merchant_of(save, rival).u8(0x1C));
		op.u8(0xB, merchant_of(save, rival).u8(0x1D));
	}
	left = uint8_t(int32_t(game_rand(save)) % 12);
	for (uint32_t i = 0;;) {
		const TownMemory& t = town_of(save, i);
		if (t.u8(0x2C8) & 0x40) {
			const uint8_t who = t.u8(0x6F1);
			if (who > merchant_count(save)) {
				const uint32_t k = ~uint32_t(who) & 3;
				op.u8(0xF, uint8_t(i));
				op.u8(0xD, t.u8(0x6D4 + k));
				op.u8(0xE, t.u8(0x6D8 + k));
				op.u8(0x10, t.u8(0x6E0 + k));
				op.u8(0x11, t.u8(0x6E4 + k));
				--left;
			} else if (who < merchant_count(save) && merchant_of(save, who).u16(8) != 0) {
				// (con who = número de comerciantes, el juego lee fuera de la tabla)
				const MerchantMemory& m = merchant_of(save, who);
				op.u8(0xF, uint8_t(i));
				op.u8(0xD, m.u8(0x1C));
				op.u8(0xE, m.u8(0x1D));
				op.u8(0x10, merchants_detail::fn_4f9430(save, who));
				op.u8(0x11, m.u8(0x39C + m.u8(0x19)));
				--left;
			}
		}
		if (++i >= towns || left < 0) {
			break;
		}
	}
	if ((op.bytes[9] == 5 || op.bytes[9] == 6) && (game_rand(save) & 7) != 0) {
		return;
	}
	const uint32_t tick = g.u32(kTick);
	op.u8(0xC, 6);
	op.u32(4, (((game_rand(save) & 7) + 5) << 8) + tick);
	enqueue(save, op);
}

// 0x4DD790: robos en las oficinas de los jugadores humanos (operación 0x64):
// en las que tienen menos guardias [0x2C7] que el nivel ([0x2EC] − 50) / 200
// + 1, con más de 100000 unidades en el almacén y a veces, hasta 6 mercancías
// ([+0x4]) con lo robado ([+0xC], en unidades de 200 o 2000).
void fn_4dd790(SaveGame& save) {
	Operation op;
	op.u32(0, 0x64);
	for (uint32_t m = merchant_count(save); int32_t(m) > 0;) {
		--m;
		const MerchantMemory& merchant = merchant_of(save, m);
		if (merchant.u16(8) != 0) {
			continue;
		}
		uint32_t o = merchant.u16(0xC);
		if (int32_t(o) >= int32_t(office_count(save))) {
			continue;
		}
		do {
			const OfficeMemory& office = save.offices[o].memory;
			const uint32_t next = office.u16(0x2C8);
			const int32_t level = int16_t(uint16_t((int32_t(office.u16(0x2EC)) - 0x32) / 0xC8 + 1));  // 0x6734E4, 0x6734E5
			const int32_t guards = office.u8(0x2C7);
			if (level > guards) {
				int32_t total = 0;
				for (uint32_t w = 0; w < 0x14; ++w) {
					total = int32_t(uint32_t(total) + uint32_t(office.s32(4 + 4 * w)));
				}
				const int32_t c = total / 2000 - 50;
				if (c > 0) {
					const int32_t x = int32_t(uint32_t(level - 1) * uint32_t(c)) / (guards + 1) - c;
					const int32_t r = int32_t(game_rand(save) & 0xFFF);
					if (r < int32_t(uint32_t(x) << 7) / 25) {
						op.u8(0xA, uint8_t(m));
						op.u8(0xB, office.u8(0x2C6));
						op.u16(0x12, uint16_t((game_rand(save) & 0x7FF) + 100));
						std::memset(op.bytes + 4, 0xFF, 6);
						std::memset(op.bytes + 0xC, 0, 6);
						uint32_t slot = 0;
						for (uint32_t w = 0; w < 0x13; ++w) {
							if (office.s32(4 + 4 * w) <= 0) {
								continue;
							}
							const uint32_t q = game_rand(save);
							int32_t v = int32_t(((q >> 4) & 7) + (q & 0xF) + 4);
							v = int32_t(uint32_t(v) * uint32_t(total));
							v = int32_t(uint32_t(v) * uint32_t(total));
							v = (v >> 8) / 10;
							v = kFineUnit[w] ? v / 200 : v / 2000;
							if (v > int32_t(op.bytes[0xC + slot])) {
								op.u8(0xC + slot, uint8_t(v));
								op.u8(4 + slot, uint8_t(w));
								slot = (slot + 1) % 6;
							}
						}
						enqueue(save, op);
					}
				}
			}
			o = next;
		} while (int32_t(o) < int32_t(office_count(save)));
	}
}

// 0x512A60 (on the names object) with a first name `index`: the length of
// its text as the game holds it (bit 7, a woman's): from the game's list,
// then the 20 written by the players (SaveGame::custom_names, in UTF-8: its
// characters, which are Latin-1 bytes in the game), and past them the
// list's first name.
uint8_t first_name_length(const SaveGame& save, uint8_t index) {
	const size_t list = (index & 0x80) ? 1 : 0;
	const uint32_t i = list ? (index & 0x7Fu) : index;
	const uint32_t count = save.globals.u16(list ? 0x6DDB72 : 0x6DDB70);
	const std::vector<std::string>& names = save.first_names[list];
	if (i < count) {
		return i < names.size() ? uint8_t(names[i].size()) : 0;
	}
	if (i >= count + 20) {
		return names.empty() ? 0 : uint8_t(names[0].size());  // Sin comprobar con el oráculo.
	}
	// Sin comprobar con el oráculo.
	const std::vector<std::string>& custom = save.custom_names[list];
	if (i - count >= custom.size()) {
		return 0;
	}
	uint8_t length = 0;
	for (const char c : custom[i - count]) {
		if ((uint8_t(c) & 0xC0) != 0x80) {
			++length;
		}
	}
	return length;
}

// 0x4F69B0 (on the merchant, with his index `me`): is a child born
// (operation 0x87)? Only to a married merchant ([+0x31] a town), after
// [+0x28] and 279 days after the wedding [+0x2C], with fewer children
// ([+0x36]) than (length of his first name % 3) + 2 and none of them (list
// [+0x34]) born less than 279 days ago. The child's name: a random byte; if
// a brother or sister has it (or it is a man's name past their number), the
// next man's name ((n + 1) % men) or, past 0x7F, a woman's one ((n & 0x7F) %
// women + 0x81: the game is one off). The 4 bytes, random & 0x7F.
bool fn_4f69b0(SaveGame& save, int merchant, uint8_t me) {
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const uint32_t tick = save.globals.u32(kTick);
	const uint8_t length = first_name_length(save, m.u8(0x1C));
	if (town_count(save) <= m.u8(0x31) || tick <= m.u32(0x28) || tick - 0x11700 < m.u32(0x2C)) {
		return false;
	}
	// Comprobado solo con estados retocados (nobody is married in the saves).
	if (int32_t(length % 3 + 2) <= int32_t(m.u8(0x36))) {
		return false;
	}
	const uint32_t men = save.globals.u16(0x6DDB70);
	const uint32_t women = save.globals.u16(0x6DDB72);
	uint8_t free[0x100];
	for (uint32_t k = 0; k < 0x80; ++k) {
		free[k] = k < men ? 1 : 0;
		free[0x80 + k] = 1;
	}
	for (uint32_t c = m.u16(0x34); c < captain_count(save); c = save.captains[c].u16(0)) {
		if (tick - 0x11700 < save.captains[c].u32(4)) {
			return false;
		}
		free[save.captains[c].u8(2)] = 0;
	}
	uint8_t name = uint8_t(game_rand(save));
	while (free[name] == 0) {
		if (name > 0x7F) {
			name = women ? uint8_t(int32_t(name & 0x7F) % int32_t(women) - 0x7F) : 0;  // Sin comprobar con el oráculo.
		} else {
			name = men ? uint8_t(int32_t(name + 1) % int32_t(men)) : 0;
		}
	}
	Operation op;
	op.u32(0, 0x87);
	op.u8(4, me);
	op.u8(5, name);
	for (size_t k = 6; k < 10; ++k) {
		op.u8(k, uint8_t(game_rand(save) & 0x7F));
	}
	enqueue(save, op);
	return true;
}

// 0x4DDA40 (evento 0x03, cada 10 días; el despachador le suma 0xA00 al
// tick): el buzón de un comerciante (0x4D6780), [+0x8] del evento cuenta 0,
// 1, 2; la mercancía de camino (0x504970), los capitanes (0x4DCEA0) y, si hay
// más comerciantes que ciudades y no está [0x6DF31C], lo de cada 10 días.
void fn_4dda40(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	const uint32_t merchants = merchant_count(save);
	if (merchants > 0) {
		fn_4d6780(save, int32_t(g.u16(kDayOfYear) % merchants));
	}
	{
		Event& e = save.events[event];
		if (++e.u32(8) > 2) {
			e.u32(8) = 0;
		}
	}
	fn_504970(save);
	fn_4dcea0(save, event);
	if (g.u32(kAldermanBusy) != 0 || merchant_count(save) <= town_count(save)) {
		return;  // Sin comprobar con el oráculo.
	}
	if (g.u8(kWorldFlag8A) != 1) {
		fn_4dc800(save);
		fn_4dc6b0(save);
		fn_4dcd90(save, event);
		fn_5316e0(save);
	}
	const uint32_t phase = save.events[event].u32(8);
	if (phase == 0) {
		fn_4dca90(save);
	} else if (phase == 1) {
		fn_4dcbf0(save);
	}
	if (g.u16(kDayOfYear) < 0xA) {
		fn_4dd400(save);
	}
	if (g.u8(kOperations) & 0xC) {
		// Comprobado solo con estados retocados (partidas en red).
		if (g.u8(kWorldFlag8A) != 0) {
			return;
		}
		Operation op;
		op.u32(0, 0xB8);
		op.u32(4, 0);
		op.u32(8, 0);
		enqueue(save, op);
	}
	if (g.u8(kWorldFlag8A) != 0) {
		return;  // Sin comprobar con el oráculo.
	}
	fn_4dd530(save);
	fn_4dd790(save);
	if (merchant_count(save) == 0) {
		return;  // Sin comprobar con el oráculo.
	}
	uint8_t n = uint8_t(int32_t(game_rand(save)) % int32_t(merchant_count(save)));
	uint8_t tries = uint8_t(game_rand(save) & 7);
	while (tries < 0xA) {
		++tries;
		if (fn_4f69b0(save, n, n)) {
			return;
		}
		n = uint8_t(int32_t(uint32_t(n) + 1) % int32_t(merchant_count(save)));
	}
}

// --- Evento 0x2A: un comerciante de la IA abre una oficina ---

// 0x4FEBD0 (thiscall sobre la oficina): la deja vacía. Con `has_offices` (el
// comerciante ya tenía alguna), [0x2EE] = 50 y [0x2F0] = [0x672A48]; si no,
// [0x2EC] = 50. No toca [0x2C0].
void fn_4febd0(OfficeMemory& o, bool has_offices) {
	o.u16(0x2C4) = 0xFFFF;
	o.u16(0x2C8) = 0xFFFF;
	o.u8(0x2C6) = 0xFF;
	o.u8(0x2C7) = 0;
	o.u16(0x2CA) = 0xFFFF;
	o.u16(0x2CC) = 0xFFFF;
	o.u16(0x2CE) = 0xFFFF;
	o.u16(0x2D0) = 0xFFFF;
	o.u16(0x2D2) = 0;
	o.u16(0x2D4) = 0;
	o.u32(0x2E8) = 0;
	for (size_t k = 0; k < 3; ++k) {
		o.u16(0x2D8 + 2 * k) = 0;
		o.u16(0x2DE + 2 * k) = 0;
	}
	std::memset(o.bytes.data() + 0x2E4, 2, 4);
	o.u16(0x2D6) = 0;
	if (has_offices) {
		o.u16(0x2EC) = 0;
		o.u16(0x2EE) = 0x32;
		o.u16(0x2F0) = 0x28;  // [0x672A48]
	} else {
		o.u16(0x2EC) = 0x32;  // Comprobado solo con estados retocados.
		o.u16(0x2EE) = 0;
		o.u16(0x2F0) = 0;
	}
	o.u16(0x2F2) = 0xFFFF;
	std::memset(o.bytes.data() + 0x2F4, 0, 0x44C - 0x2F4);
	std::memset(o.bytes.data(), 0, 0x2C0);
}

// 0x5308A0 con create = 1 (sobre el mundo): la oficina del comerciante en
// la ciudad o, si no la tiene, una nueva de la lista de libres ([0x6DE51E];
// si no queda, 0x20 más o 0x40 si no había ninguna), al principio de las
// listas del comerciante ([+0xC]) y de la ciudad ([0x784]), con [0x2C0] = el
// tick. Devuelve su índice, o -1. No se traduce lo de la interfaz (mensaje,
// lista de ciudades del jugador *0x6CBDD8, escena del puerto *0x6E51AC).
int fn_5308a0_create(SaveGame& save, uint32_t merchant, uint32_t town) {
	const int existing = facilities_detail::fn_5308a0(save, merchant, town);
	if (existing >= 0) {
		return existing;  // Comprobado solo con estados retocados.
	}
	merchant &= 0xFFFF;
	town &= 0xFFFF;
	if (town >= town_count(save) || merchant >= merchant_count(save)) {
		return -1;  // Comprobado solo con estados retocados.
	}
	GlobalMemory& g = save.globals;
	const uint16_t count = g.u16(kOfficeCount);
	if (g.u16(kOfficeFree) >= count) {
		// Comprobado solo con estados retocados (en las partidas siempre queda alguna).
		// Las nuevas, construidas (0x4FEBB0: 0x52DE70 y 0x4FEBD0 sin oficinas),
		// libres y enlazadas.
		g.u16(kOfficeFree) = count;
		const uint16_t grown = count ? uint16_t(count + 0x20) : uint16_t(0x40);
		g.u16(kOfficeCount) = grown;
		save.offices.resize(grown);
		for (uint32_t i = count; i < grown; ++i) {
			OfficeMemory& o = save.offices[i].memory;
			fn_4febd0(o, false);
			o.u16(0x2C4) = 0xFFFF;
			o.u16(0x2C8) = uint16_t(i + 1);
		}
	}
	const uint16_t index = g.u16(kOfficeFree);
	OfficeMemory& o = save.offices[index].memory;
	g.u16(kOfficeFree) = o.u16(0x2C8);
	MerchantMemory& m = merchant_of(save, merchant);
	fn_4febd0(o, m.u16(0xC) < g.u16(kOfficeCount));
	o.u16(0x2C4) = uint16_t(merchant);
	o.u16(0x2C8) = m.u16(0xC);
	m.u16(0xC) = index;
	o.u8(0x2C6) = uint8_t(town);
	TownMemory& t = town_of(save, town);
	o.u16(0x2CA) = t.u16(0x784);
	t.u16(0x784) = index;
	o.u32(0x2C0) = g.u32(kTick);
	return index;
}

// 0x4DE4F0 (evento 0x2A): el comerciante de la IA (sin [+0x8] & 4, con
// menos de 6 oficinas) con más dinero por oficina (de 40000 en adelante) abre
// una (0x5308A0) en la ciudad con mercado que más le conviene: más empresas
// con más de 1000 en [+0x8] (instalaciones 3..20) por oficina que haya,
// salvo si las empresas de los de la IA rentan menos de 1/7 por unidad / 10.
// Vuelve en 21 días si no hay comerciante, en 36 si no hay ciudad y si no en
// (ciudad + comerciante + 120) · 72 ticks.
void fn_4de4f0(SaveGame& save, uint16_t event) {
	uint32_t best_merchant = 0xFFFF;  // [esp+0x10]
	uint32_t best_value = 40000;  // ebp
	for (uint32_t m = merchant_count(save); m > 0;) {
		--m;
		const MerchantMemory& merchant = merchant_of(save, m);
		if (merchant.u32(0) < best_value || merchant.u16(8) == 0 || (merchant.u8(8) & 4)) {
			continue;
		}
		uint32_t offices = 0;
		for (int o = office_at(save, merchant.u16(0xC)); o >= 0;
			o = office_at(save, save.offices[size_t(o)].memory.u16(0x2C8))) {
			++offices;
		}
		if (offices >= 6) {
			continue;
		}
		const uint32_t value = offices ? merchant.u32(0) / offices : merchant.u32(0);
		if (value > best_value) {
			best_value = value;
			best_merchant = m;
		}
	}
	if (best_merchant >= merchant_count(save)) {
		save.events[event].u32(0) += 0x1500;  // Comprobado solo con estados retocados.
		return;
	}
	uint32_t best_town = 0xFFFF;  // [esp+0x1c]
	uint32_t best_score = 0;  // [esp+0x20]
	for (uint32_t i = 0; i < town_count(save); ++i) {
		const TownMemory& t = town_of(save, i);
		if (!(t.u32(0x76C) & 0x100)) {
			continue;
		}
		uint32_t rent = 0;  // esi
		uint32_t units = 0;  // ebx
		uint32_t offices = 0;  // [esp+0x14]
		for (int o = office_at(save, t.u16(0x784)); o >= 0;
			o = office_at(save, save.offices[size_t(o)].memory.u16(0x2CA))) {
			const OfficeMemory& office = save.offices[size_t(o)].memory;
			const uint32_t owner = office.u16(0x2C4);  // (el juego no mira el índice)
			if (owner < merchant_count(save) && merchant_of(save, owner).u16(8) != 0) {
				for (uint32_t w = office.u16(0x2CC); w < save.globals.u16(kWorldRecordCount);
					w = save.world_records[w].u16(8)) {
					rent += save.world_records[w].u16(4);
					units += save.world_records[w].u16(0xC);
				}
			}
			++offices;
		}
		if (units > 0 && rent / 7 < units / 10) {
			continue;
		}
		uint32_t score = 0;
		for (uint32_t k = 0; k < 0x12; ++k) {
			if (t.u16(0x878 + 0x10 * k) > 1000) {
				score += 1000;
			}
		}
		if (offices) {
			score /= offices;
		}
		if (best_score < score) {
			best_score = score;
			best_town = i;
		}
	}
	if (best_town >= town_count(save)) {
		save.events[event].u32(0) += 0x2400;  // Comprobado solo con estados retocados.
		return;
	}
	fn_5308a0_create(save, best_merchant, best_town);
	save.events[event].u32(0) += (best_town + best_merchant + 0x78) * 72;
}

// --- Evento 0x2B: un comerciante de la IA encarga un barco ---

// 0x52B0D0 (thiscall sobre la ciudad): ¿se pueden hacer barcos del tipo
// `type` (0..3) en su astillero ([0x824 + tipo] >= 0; los tipos 2 y 3 no en
// las ciudades con [0x2C8] & 1)?
bool fn_52b0d0(const SaveGame& save, int town, uint8_t type) {
	const TownMemory& t = save.towns[size_t(town)].memory;
	if (type > 3) {
		return false;  // Comprobado solo con estados retocados.
	}
	if ((t.u8(0x2C8) & 1) && (type & 2) == 2) {
		return false;
	}
	return t.s8(0x824 + type) >= 0;
}

// Lo que rellena 0x52B2C0 en la pila de quien lo llama (0x60 bytes; no lo
// guarda nadie: solo cuenta lo que devuelve).
struct ShipOrder {
	int32_t have[5];  // [+0x00] unidades de cada material (200 o 2000) en la oficina y los barcos
	int32_t used[5];  // [+0x14] las que se usan
	int32_t buy[5];  // [+0x28] las que hay que comprar a la ciudad (las que tiene, si no llega)
	int32_t price[5];  // [+0x3C] lo que cuestan (0x52E430)
	int32_t cost;  // [+0x50] coste total
	int32_t days;  // [+0x54]
	int32_t value;  // [+0x58] del astillero, según su nivel
	uint8_t b5c;  // [+0x5C]
	uint8_t b5d;  // [+0x5D]
	uint8_t level;  // [+0x5E]
};

// Materiales de los barcos (0x66DEB0 + 0x60 · tipo + 0x20 · nivel, u32 cada uno):
// unidades de madera, paño, herramientas, cáñamo y brea; [6] lo que cuesta.
constexpr int32_t kShipMaterials[4][3][8] = {
	{{7, 3, 3, 3, 20, 23, 7650, 11414}, {9, 3, 3, 3, 20, 20, 8200, 12074}, {11, 3, 3, 3, 20, 24, 8800, 12784}},
	{{12, 5, 5, 5, 30, 29, 18270, 24450}, {14, 5, 5, 5, 30, 32, 18720, 25010}, {16, 5, 5, 5, 30, 34, 19890, 26290}},
	{{18, 3, 4, 4, 40, 46, 16560, 22296}, {20, 3, 4, 4, 40, 50, 16500, 22346}, {22, 3, 4, 4, 40, 53, 17490, 23446}},
	{{30, 10, 10, 8, 50, 58, 22968, 34442}, {33, 10, 10, 8, 50, 64, 23040, 34679}, {36, 10, 10, 8, 50, 69, 24840, 36644}},
};
// Por tipo (0x673838): valor de los niveles 0, 1, 2 del astillero ([0x824 +
// tipo]) y tope; el juego indexa con el nivel sin limitarlo.
constexpr uint8_t kShipyardValues[16] = {0x0F, 0x13, 0x17, 0x19, 0x1C, 0x1F, 0x22, 0x23, 0x2D, 0x30, 0x34, 0x37,
	0x37, 0x3B, 0x41, 0x46};
// 0x672CF0: 6 bytes por tipo.
constexpr uint8_t kShipOrderBytes[24] = {0x00, 0x00, 0x04, 0x00, 0x04, 0x02, 0x00, 0x00, 0x04, 0x00, 0x02, 0x04,
	0x00, 0x04, 0x00, 0x06, 0x02, 0x08, 0x00, 0x06, 0x02, 0x08, 0x00, 0x0C};

// 0x52B2C0 (thiscall sobre la ciudad): ¿puede el comerciante encargar aquí
// un barco del tipo `type`? Hace falta astillero ([0x76C] & 0x20000) para el
// tipo (0x52B0D0) y, de cada material, lo que tenga en su oficina y en sus
// barcos en la ciudad (estado < 4, sin ruta automática) más lo que quede en
// la ciudad; a los jugadores humanos, además, el dinero. Rellena `out`.
bool fn_52b2c0(const SaveGame& save, int town, ShipOrder& out, uint32_t type_arg, uint32_t merchant) {
	static constexpr uint8_t kWares[5] = {0xB, 8, 0xC, 0x11, 0xF};  // madera, paño, herramientas, cáñamo, brea
	const TownMemory& t = save.towns[size_t(town)].memory;
	bool ok = true;
	if (uint16_t(merchant) >= merchant_count(save)) {
		return false;  // Comprobado solo con estados retocados.
	}
	const uint8_t type = uint8_t(type_arg);
	if (type >= 4) {
		return false;  // Comprobado solo con estados retocados.
	}
	const int8_t yard = t.s8(0x824 + type);
	if (yard < 0 || !(t.u32(0x76C) & 0x20000)) {
		return false;
	}
	if ((t.u8(0x2C8) & 1) && (type & 2) == 2) {
		return false;
	}
	const int32_t* materials = kShipMaterials[type][yard > 2 ? 2 : yard];
	const uint8_t here = t.u8(0x2C0);
	const int office = facilities_detail::fn_5308a0(save, merchant, here);
	for (size_t k = 0; k < 5; ++k) {
		out.used[k] = office >= 0 ? save.offices[size_t(office)].memory.s32(4 + 4 * kWares[k]) : 0;
	}
	const uint32_t m = merchant & 0xFFFF;
	for (uint32_t s = save.merchants[m].memory.u16(0xE); s < ship_count(save); s = save.ships[s].memory.u16(4)) {
		const ShipMemory& ship = save.ships[s].memory;
		if (ship.u8(0x39) != here || ship.u16(0x134) >= 4 || (ship.u8(0x136) & 1)) {
			continue;
		}
		for (size_t k = 0; k < 5; ++k) {
			out.used[k] = int32_t(uint32_t(out.used[k]) + uint32_t(ship.s32(0x54 + 4 * kWares[k])));
		}
	}
	for (size_t k = 0; k < 5; ++k) {
		const uint32_t w = kWares[k];
		const int32_t unit = kFineUnit[w] ? 200 : 2000;
		const int32_t units = out.used[k] / unit;
		out.used[k] = units;
		out.have[k] = units;
		const int32_t need = materials[k];
		if (units >= need) {
			out.used[k] = need;
			out.price[k] = 0;
			out.buy[k] = 0;
			continue;
		}
		out.buy[k] = need - units;
		out.price[k] = game_buy_price(save, int(w), here, out.buy[k] * unit);
		const int32_t in_town = t.s32(4 + 4 * w) / unit;
		if (out.buy[k] > in_town) {
			ok = false;
			out.buy[k] = in_town;
		}
	}
	// (Con un nivel de más de 3 − el tipo, el juego lee más allá de la tabla.)
	const size_t row = size_t(int32_t(yard) + 4 * int32_t(type));
	int32_t value = int32_t(row < 16 ? kShipyardValues[row] : 0) + t.u8(0x828 + type);
	if (value > kShipyardValues[4 * type + 3]) {
		value = kShipyardValues[4 * type + 3];  // Comprobado solo con estados retocados.
	}
	out.value = value;
	// (Con [0x854] = 0, el juego divide por cero.)
	out.days = t.u16(0x854) ? value * 28 / int32_t(t.u16(0x854)) + 0x1C : 0x1C;
	int32_t cost;
	if (value > 0x28) {
		cost = value > 0x32 ? (value > 0x3C ? 0x12C : 0x14A) : 0x168;
	} else {
		cost = value > 0x1E ? 0x186 : (value > 0x14 ? 0x1A4 : 0x1C2);
	}
	cost = int32_t(uint32_t(materials[6]) + uint32_t(cost * 2));
	cost = int32_t(uint32_t(int32_t(ftol(double(t.f32(0x818))))) + uint32_t(cost));
	for (size_t k = 0; k < 5; ++k) {
		cost = int32_t(uint32_t(cost) + uint32_t(out.price[k]));
	}
	out.cost = cost;
	out.level = uint8_t(t.u8(0x824 + type) > 2 ? 2 : t.u8(0x824 + type));
	out.b5c = kShipOrderBytes[6 * type + 1];
	out.b5d = kShipOrderBytes[6 * type];
	const MerchantMemory& mm = save.merchants[m].memory;
	if (mm.s32(0) < out.cost && mm.u16(8) == 0) {
		return false;  // Comprobado solo con estados retocados.
	}
	return ok;
}

// 0x52B2C0 sin lo que deja en la pila (para las sondas).
bool ship_order_possible(const SaveGame& save, int town, uint32_t type, uint32_t merchant) {
	ShipOrder order;
	return fn_52b2c0(save, town, order, type, merchant);
}

// Los nombres de los barcos nuevos de un jugador humano, por tipo (0x6C0CB4).
constexpr const char* kDefaultShipNames[4] = {"snaikka", "crayer", "cog", "hulk"};

// 0x52A360 (thiscall sobre la ciudad, con el comerciante, el tipo y `force`):
// el comerciante encarga al astillero un barco del tipo. Nada (devuelve
// false) sin astillero para el tipo ([0x824 + tipo] < 0), con un tipo grande
// (bit 1) en una ciudad con [0x2C8] & 1, si 0x52B2C0 no lo permite o, salvo
// con `force` = 1 (la simulación), si no le llega el dinero. Los materiales
// (madera, paño, herramientas, cáñamo y brea) salen primero de su oficina en
// la ciudad y después de sus barcos en ella (estado < 4; a diferencia de
// 0x52B2C0, también los de ruta automática), en unidades enteras y hasta lo
// que 0x52B2C0 dice que se usa de lo suyo; lo demás se compra a la ciudad (sin
// bajar de 0). El barco nuevo (0x507D60 en el astillero) con el tamaño que
// da el astillero (como 0x52A8D0), su artillería (0x5181D0) y su bodega
// (0x5182B0) va al principio de la lista del comerciante, con su ciudad natal
// en [+0x13A] y nombre: de la lista (0x509A50) si es de la IA; si es humano,
// el del tipo con [+0x15E] = 0x7FFF − tipo. Se cobra el coste (dinero,
// [+0x4C4] y [+0x48] del barco), [+0x4C] el tick y reputación (0x4F9120).
bool fn_52a360(SaveGame& save, int town, uint32_t merchant_arg, uint32_t type_arg, uint8_t force) {
	static constexpr uint8_t kWares[5] = {0xB, 8, 0xC, 0x11, 0xF};  // madera, paño, herramientas, cáñamo, brea
	TownMemory& t = town_of(save, uint32_t(town));
	const uint32_t merchant = merchant_arg & 0xFF;
	const uint32_t type = type_arg & 0xFF;
	if (t.s8(0x824 + type) < 0) {
		return false;  // Sin comprobar con el oráculo.
	}
	if ((type_arg & 2) && (t.u8(0x2C8) & 1)) {
		return false;  // Sin comprobar con el oráculo.
	}
	ShipOrder order;
	if (!fn_52b2c0(save, town, order, type_arg, merchant)) {
		return false;  // Sin comprobar con el oráculo.
	}
	MerchantMemory& m = merchant_of(save, merchant);
	if (order.cost > m.s32(0) && force != 1) {
		return false;  // Sin comprobar con el oráculo.
	}
	// Lo que hay en unidades enteras, como mucho lo que queda por usar de lo
	// suyo (con existencias negativas, el juego divide igual: las sube).
	auto take = [&](int32_t& stock, size_t k) {
		const int32_t unit = kFineUnit[kWares[k]] ? 200 : 2000;
		int32_t units = stock / unit;
		if (units > order.used[k]) {
			units = order.used[k];
			order.used[k] = 0;
		} else {
			order.used[k] -= units;
		}
		stock = int32_t(uint32_t(stock) - uint32_t(unit * units));
	};
	const uint8_t here = t.u8(0x2C0);
	const int office = facilities_detail::fn_5308a0(save, merchant, here);
	if (office >= 0) {
		for (size_t k = 0; k < 5; ++k) {
			take(save.offices[size_t(office)].memory.s32(4 + 4 * kWares[k]), k);
		}
	}
	for (uint32_t s = m.u16(0xE); s < ship_count(save);) {
		ShipMemory& ship = ship_of(save, s);
		const uint32_t next = ship.u16(4);
		if (ship.u8(0x39) == here && ship.u16(0x134) < 4) {
			bool changed = false;
			for (size_t k = 0; k < 5; ++k) {
				int32_t& cargo = ship.s32(0x54 + 4 * kWares[k]);
				if (cargo > 0) {
					take(cargo, k);
					changed = true;
				}
			}
			if (changed) {
				recalculate_cargo(save.ships[s]);
			}
		}
		s = next;
	}
	for (size_t k = 0; k < 5; ++k) {
		const int32_t unit = kFineUnit[kWares[k]] ? 200 : 2000;
		int32_t& stock = t.s32(4 + 4 * kWares[k]);
		stock = int32_t(uint32_t(stock) - uint32_t(order.buy[k] * unit));
	}
	for (size_t o = 0x60; o >= 4; o -= 4) {
		if (t.s32(o) < 0) {
			t.s32(o) = 0;
		}
	}
	uint16_t index = 0;
	events_mayor_detail::fn_507d60(save, index, here, 0);
	events_mayor_detail::fn_52a8d0(save, town, index, type_arg);
	events_mayor_detail::fn_5181d0(save, index);
	recalculate_cargo(save.ships[index]);
	ShipMemory& s = ship_of(save, index);
	s.u8(0) = uint8_t(merchant_arg);
	s.u16(4) = m.u16(0xE);
	m.u16(0xE) = index;
	s.u8(0x13A) = m.u8(0x19);
	if (m.u16(8) == 0) {
		// Sin comprobar con el oráculo.
		s.u16(0x15E) = uint16_t(0x7FFF - (s.u8(0xE) & 3));
		const char* name = kDefaultShipNames[type & 3];
		std::memcpy(s.bytes.data() + 0x160, name, std::strlen(name) + 1);
	} else {
		events_mayor_detail::fn_509a50(save, index);
	}
	s.u32(0x18) = 0;
	m.s32(0) = int32_t(uint32_t(m.s32(0)) - uint32_t(order.cost));
	m.s32(0x4C4) = int32_t(uint32_t(m.s32(0x4C4)) + uint32_t(order.cost));
	s.s32(0x48) = order.cost;
	s.u32(0x4C) = save.globals.u32(kTick);
	diet_detail::fn_4f9120(save, merchant, here, s.u32(0x14));
	return true;
}

// 0x4DE790 (evento 0x2B): si los barcos de todos los comerciantes (los de la
// IA, contados; los humanos, [+0x478]) por 400 no llegan a los habitantes,
// el de la IA (sin [+0x8] & 4) con menos barcos encarga uno (operación 0x2C:
// tipo, comerciante, ciudad, 1) en su ciudad natal o la primera siguiente en
// la que pueda (0x52B2C0): una coca (o, con poco dinero o en una ciudad con
// [0x2C8] & 1, un craier; uno menos si el astillero no los hace). Vuelve en
// 36 días (21 si no se encarga).
void fn_4de790(SaveGame& save, uint16_t event) {
	Operation op;
	const GlobalMemory& g = save.globals;
	uint32_t citizens = 0;  // ebx
	for (uint32_t i = 0; i < town_count(save); ++i) {
		citizens += town_of(save, i).u32(0x2D4);
	}
	uint32_t best = 0xFFFF;  // edi
	uint32_t fewest = 1000;  // [esp+0x2c]
	uint32_t ships = 0;  // [esp+0x10]
	for (uint32_t m = merchant_count(save); m > 0;) {
		--m;
		const MerchantMemory& merchant = merchant_of(save, m);
		uint32_t n = 0;
		if (merchant.u16(8) == 0 || (merchant.u8(8) & 4)) {
			n = merchant.u16(0x478);
		} else {
			for (uint32_t s = merchant.u16(0xE); s < ship_count(save); s = ship_of(save, s).u16(4)) {
				++n;
			}
			if (n < fewest) {
				best = m;
				fewest = n;
			}
		}
		ships += n;
	}
	if (ships * 400 >= citizens || best >= merchant_count(save)) {
		save.events[event].u32(0) += 0x1500;
		return;
	}
	save.events[event].u32(0) += 0x2400;
	if (g.u32(kAldermanBusy) != 0) {
		return;  // Sin comprobar con el oráculo.
	}
	const MerchantMemory& merchant = merchant_of(save, best);
	const uint32_t home = merchant.u8(0x19);
	uint32_t type = 1;
	if (merchant.s32(0) >= 40000 && !(town_of(save, home).u8(0x2C8) & 1)) {
		type = 3;
	}
	op.u32(0, 0x2C);
	op.u32(4, type);
	op.u32(8, best);
	op.u32(0xC, home);
	op.u32(0x10, 1);
	if (!fn_52b0d0(save, int(home), uint8_t(type))) {
		--type;
		op.u32(4, type);
	}
	uint32_t town = home;
	for (;;) {
		ShipOrder order;
		if (fn_52b2c0(save, int(town), order, type, best)) {
			enqueue(save, op);
			return;
		}
		if (++town >= town_count(save)) {
			town = 0;
		}
		op.u32(0xC, town);
		if (town == merchant.u8(0x19)) {
			return;
		}
	}
}

// 0x5098B0 (sobre el objeto de los barcos, con el hueco del capitán de un
// barco): el capitán vuelve a la lista de libres ([0x6DD880], encadenada por
// su u16 [+0x0]): en cabeza, y el hueco se queda con lo que tenía en [+0x0].
// Si ya estaba en la lista, el hueco pasa a 0xFFFF.
void fn_5098b0(SaveGame& save, uint16_t& slot) {
	GlobalMemory& g = save.globals;
	constexpr uint32_t kFreeCaptain = global::kShips + 0xE0;  // u16 (0x6DD880)
	const uint16_t count = g.u16(global::kCaptainCount);
	const uint16_t c = slot;
	if (c >= count) {
		return;
	}
	for (uint16_t f = g.u16(kFreeCaptain); f < count; f = save.captains[f].u16(0)) {
		if (f == c) {
			slot = 0xFFFF;  // Sin comprobar con el oráculo.
			return;
		}
	}
	slot = save.captains[c].u16(0);
	save.captains[c].u16(0) = g.u16(kFreeCaptain);
	g.u16(kFreeCaptain) = c;
}

// 0x4DDC00 (evento 0x27, lo crea 0x4DCEA0 en el evento 0x03): el capitán
// [+0xC] del barco [+0x8], de un comerciante de la IA, se retira, según el
// estado del barco ([+0x134]): en 0 y 1 deja el barco (si guía un convoy, lo
// deshace antes, 0x50A580); en 4..6, si guía un convoy, espera 0x20 ticks y,
// si no, lo deja; en 7 y 12..14 lo deja; en 8 (en el astillero) lo deja, el
// barco pasa al estado 0 y el astillero de la ciudad queda libre ([0x990],
// [0x994]); en 2, 9, 15, 16, 19 y 20 espera 0x40 ticks; en 3, lo que le
// falta (0x41 − [+0x138]); en los demás, nada. Si el capitán ya no está en
// el barco, lo busca en el primer barco de cada comerciante, del último al
// primero. Devuelve si sigue (con el tick ya cambiado). Comprobado con
// estados retocados (`retouch.py capitanes`) en los estados 1 (con el convoy
// que guía), 2, 3, 6, 15 y 19; los demás, sin comprobar con el oráculo.
bool fn_4ddc00(SaveGame& save, uint16_t event) {
	GlobalMemory& g = save.globals;
	constexpr uint32_t kConvoyCount = global::kShips + 0xF6;  // u16 (0x6DD896)
	Event& e = save.events[event];
	const uint32_t captain = e.u32(0xC);
	if (captain >= captain_count(save)) {
		return false;  // Sin comprobar con el oráculo.
	}
	uint32_t s = e.u32(0x8);
	if (s >= ship_count(save) || ship_of(save, s).u16(0x42) != captain) {
		// Sin comprobar con el oráculo.
		bool found = false;
		for (uint32_t m = merchant_count(save); m > 0 && !found;) {
			const uint32_t first = merchant_of(save, --m).u16(0xE);
			if (first >= ship_count(save)) {
				continue;
			}
			if (ship_of(save, first).u16(0x42) != captain) {
				// El juego se queda aquí en un bucle sin fin (0x4DDCB0 salta a
				// sí mismo): solo mira el primer barco. Aquí se quita el evento.
				return false;
			}
			s = first;
			e.u32(0x8) = s;
			found = true;
		}
		if (!found) {
			return false;
		}
	}
	ShipMemory& ship = ship_of(save, s);
	const uint16_t status = ship.u16(0x134);
	auto leads = [&] {
		const uint16_t convoy = ship.u16(0x8);
		return convoy < g.u16(kConvoyCount) && save.convoys[convoy].u16(0x10) == s;
	};
	auto release = [&] {
		uint16_t slot = ship.u16(0x42);
		fn_5098b0(save, slot);
		ship.u16(0x42) = slot;
	};
	switch (status) {
	case 0:
	case 1:
		if (leads()) {
			if (Sailing* sailing = event_sailing()) {
				sailing->fn_50a580(ship.u16(0x8));
			}
		}
		release();
		return false;
	case 2: case 9: case 15: case 16: case 19: case 20:
		e.u32(0) += 0x40;
		return true;
	case 3:
		e.u32(0) += 0x41 - ship.u16(0x138);
		return true;
	case 4: case 5: case 6:
		if (leads()) {
			e.u32(0) += 0x20;
			return true;
		}
		release();
		return false;
	case 7: case 12: case 13: case 14:
		release();
		return false;
	case 8: {
		release();
		ship.u16(0x134) = 0;
		TownMemory& t = save.towns[ship.u8(0x39)].memory;
		t.u16(0x994) = 0xFFFF;
		t.u32(0x990) = 0;
		return false;
	}
	default:
		return false;
	}
}

}  // namespace events_towns_detail

using namespace events_towns_detail;

// Los tipos de este grupo; lo que hace después el despachador (0x4D85C0).
int events_towns(SaveGame& save, uint16_t event, uint16_t type) {
	switch (type) {
	case 0x01:
		return fn_4db4e0(save, event) ? 1 : 0;
	case 0x03:
		fn_4dda40(save, event);
		save.events[event].u32(0) += 0xA00;
		return 1;
	case 0x2A:
		fn_4de4f0(save, event);
		return 1;
	case 0x2B:
		fn_4de790(save, event);
		return 1;
	case 0x27:
		return fn_4ddc00(save, event) ? 1 : 0;  // Un capitán de la IA se retira.
	case 0x07:
		fn_4e23a4(save, event);  // La fiesta de un comerciante.
		return 0;
	case 0x1D:
		return fn_4e9564(save, event) ? 1 : 0;  // Un incendio en la ciudad.
	case 0x15:
		return fn_4e7c74(save, event) ? 1 : 0;  // A merchant's wedding.
	default:
		return -1;
	}
}

}  // namespace p3::data::tick
