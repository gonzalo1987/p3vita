// Manejadores pequeños del ejecutor de operaciones (0x535760, tabla 0x537030;
// docs/formats/world_tick.md §7) que no son de las obras ni de los encargos
// de la Hansa.

#include <cstring>
#include <utility>

#include "p3data/game_fixes.hpp"
#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {

namespace events_detail {
void fn_4fe190(CaptainMemory& c);
uint16_t fn_5097c0(SaveGame& save, bool keep);
}  // namespace events_detail
namespace events_mayor_detail {
uint16_t fn_507d60(SaveGame& save, uint16_t& out, uint32_t town, uint32_t mode);
}  // namespace events_mayor_detail
namespace events_towns_detail {
bool fn_52a360(SaveGame& save, int town, uint32_t merchant_arg, uint32_t type_arg, uint8_t force);
void fn_538a80(SaveGame& save, uint32_t ship, uint8_t add9, uint8_t add_ab);
}  // namespace events_towns_detail
namespace events_rare_detail {
void fn_52a150(SaveGame& save, int town, uint8_t rate);
}  // namespace events_rare_detail
namespace construction_detail {
void fn_4d9fc0(const SaveGame& save, uint32_t& tick, uint32_t town);
}  // namespace construction_detail
namespace events_detail {
void fn_4dab00(SaveGame& save, uint32_t town_arg, uint32_t record_arg);
}  // namespace events_detail
namespace events_towns_detail {
void fn_4d6fd0(SaveGame& save, uint32_t index);
}  // namespace events_towns_detail
namespace merchants_detail {
void fn_4f5d80(SaveGame& save, int merchant);
}  // namespace merchants_detail
namespace ship_trade_detail {
bool fn_526aa0(SaveGame& save, int town, uint32_t captain);
}  // namespace ship_trade_detail
namespace ship_ai_detail {
int32_t fn_5184f0(SaveGame& save, int ship);
}  // namespace ship_ai_detail

namespace {

constexpr uint32_t kConvoyCount = global::kShips + 0xF6;  // u16 (0x6DD896)
constexpr uint32_t kExpeditionMark = 0x6DE52B;  // u8 ([+0x8B] del mundo)
constexpr uint32_t kEventCount = 0x6DD748;  // u16 registros de la cola ([+0xC] de 0x6DD73C)

uint16_t half(const uint8_t* operation, size_t offset) {
	uint16_t v;
	std::memcpy(&v, operation + offset, 2);
	return v;
}

// El convoy de la operación, si existe, tiene guía y, en puerto (estado <
// 0xF), su guía puede zarpar (0x519BA0); si no, −1.
int expedition_convoy(const SaveGame& save, uint16_t convoy) {
	if (convoy >= save.globals.u16(kConvoyCount)) {
		return -1;  // Checked with the oracle (0x1E and 0x1F): scenario operation_convoys (saves 1, 3, 5, 7).
	}
	const GameMemory<0x3C>& c = save.convoys[convoy];
	const uint16_t lead = c.u16(0x10);
	if (lead >= save.globals.u16(global::kShipCount)) {
		return -1;  // Checked with the oracle (0x1E and 0x1F): scenario operation_convoys (saves 1, 3, 5, 7).
	}
	if (c.u16(0x12) < 0xF && !ship_ai_detail::fn_519ba0(save, lead)) {
		return -1;  // Checked with the oracle (0x1E and 0x1F): scenario operation_convoys (saves 1, 3, 5, 7).
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
		return;  // Checked with the oracle (both, and the slot 4): scenario operation_guards (saves 1, 3, 5, 7).
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
		return;  // Checked with the oracle (each town): scenario operation_guards (saves 1, 3, 5, 7).
	}
	TownMemory& t = save.towns[a].memory;
	const uint8_t current = t.u8(0x83F);
	if (current != 0 && current < save.globals.u16(global::kWorldMerchantCount)) {
		return;  // Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
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
			// Checked with the oracle (has > 0 and < 0): scenario operation_convoys (saves 1, 3, 5,
			// 7).
			money = has > 0 ? has : 0;
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

// La operación 0x12 (0x538A80): {0x12, u32 barco, u8 a, …, u8 b}: el
// capitán del barco mejora: sus habilidades [+9] (más a), [+0xA] y [+0xB]
// (más b las dos), cada una con un tope según los bits del índice del
// capitán (0x673B34: 250, 200, 250, 150) y sin dar la vuelta; y su salario
// se recalcula (0x4FE190). If it rises, a human owner gets the letter 0x4D.
// The same as 0x4DCEA0 runs it (events_towns.cpp).
void fn_538a80(SaveGame& save, const uint8_t* operation) {
	events_towns_detail::fn_538a80(save, field(operation, 4), operation[8], operation[0xC]);
}

// La operación 0x64 (en 0x5363EE): {0x64, 16 bytes, …, u16 [+0x12] plazo}:
// un evento 0x33, un robo en una oficina (events_rare.cpp, fn_4ec934), a tick
// + plazo con los 16 bytes. La encola el evento 0x03 (0x4DD790) en las
// oficinas de los jugadores humanos con pocos guardias.
void fn_5363ee(SaveGame& save, const uint8_t* operation) {
	const uint32_t tick = save.globals.u32(global::kWorldTick) + half(operation, 0x12);
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, tick)];
	e.u16(6) = 0x33;
	std::memcpy(e.bytes.data() + 8, operation + 4, 0x10);
}

namespace {

// 0x4F8FE0 (sobre el comerciante, con la ciudad, el tipo y la cantidad):
// presta `amount` en la ciudad: se le resta y su reputación en ella
// ([+0x120 + 12·ciudad], float) sube (4 − tipo) · cantidad · 0,0000125
// (0x672F3C) · [+0x464].
void fn_4f8fe0(SaveGame& save, uint32_t merchant, uint32_t town, uint32_t kind, uint16_t amount) {
	static constexpr uint32_t kLoanReputation = 0x3751B717;  // 0x672F3C
	float factor;
	std::memcpy(&factor, &kLoanReputation, sizeof(factor));
	MerchantMemory& m = save.merchants[merchant].memory;
	const float weight = float(double(uint32_t(4 - kind)));
	m.s32(0) = int32_t(uint32_t(m.s32(0)) - amount);
	const float gain = float(double(weight) * double(float(int32_t(amount))) * double(factor));
	const float r = float(double(gain) * double(m.f32(0x464)));
	m.f32(0x120 + 12 * town) = float(double(m.f32(0x120 + 12 * town)) + double(r));
}

}  // namespace

// La operación 0x29 (0x539DC0): un préstamo {0x29, u32 [+0x4], u8 [+0x8],
// 3 bytes [+0x9], u16 [+0xC] la cantidad, u8 [+0xE] semanas, u8 [+0xF], u8
// [+0x10] el que presta (>= comerciantes: la ciudad, su prestamista), u8
// [+0x11] el que lo pide, u8 [+0x12] ciudad, u8 [+0x13] tipo (bits 0..3) y
// hueco (bits 4..7)}. Si el que presta es un comerciante, tiene que tener la
// cantidad. La deuda: un evento 0x01 (events_towns.cpp) dentro de las
// semanas, con [+0x8] los 4 bytes, [+0xC] el tick con el byte bajo [+0xF],
// [+0x10] y [+0x11] los dos, [+0x12] [+0x8], [+0x13] las semanas, [+0x14] la
// ciudad y [+0x15..+0x17] los 3 bytes. Un hueco < 6 vacía el de la taberna
// de la ciudad ([0x9CA + 6·hueco]); el que lo pide recibe la cantidad y el
// comerciante que presta la paga (0x4F8FE0, con el tipo como mucho 4). La
// encola el tick de los comerciantes (desde 0x4F4F6B: piden al prestamista
// de la ciudad).
void fn_539dc0(SaveGame& save, const uint8_t* operation) {
	GlobalMemory& g = save.globals;
	const uint32_t town = operation[0x12];
	if (town >= g.u32(global::kWorldTownCount)) {
		return;  // Checked with the oracle: scenario operation_loans (saves 1, 3, 5, 7).
	}
	const uint32_t merchants = g.u16(global::kWorldMerchantCount);
	const uint8_t lender = operation[0x10];
	const uint8_t borrower = operation[0x11];
	const uint16_t amount = half(operation, 0xC);
	if (lender < merchants && save.merchants[lender].memory.s32(0) < int32_t(amount)) {
		return;  // Checked with the oracle: scenario operation_loans (saves 1, 3, 5, 7).
	}
	const uint32_t tick = g.u32(global::kWorldTick);
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, tick + operation[0xE] * 0x700u)];
	e.u16(6) = 1;
	e.u32(0x8) = field(operation, 4);
	e.u32(0xC) = (tick & 0xFFFFFF00) | operation[0xF];
	e.u8(0x10) = lender;
	e.u8(0x11) = borrower;
	e.u8(0x12) = operation[0x8];
	e.u8(0x13) = operation[0xE];
	e.u8(0x14) = uint8_t(town);
	e.u8(0x15) = operation[0x9];
	e.u8(0x16) = operation[0xA];
	e.u8(0x17) = operation[0xB];
	const uint32_t slot = operation[0x13] >> 4;
	if (slot < 6) {
		// Checked with the oracle (slots 1 and 4): scenario operation_loans (saves 1, 3, 5, 7).
		save.towns[town].memory.u16(0x9CA + 6 * slot) = 0;
	}
	if (borrower < merchants) {
		MerchantMemory& m = save.merchants[borrower].memory;
		m.s32(0) = int32_t(uint32_t(m.s32(0)) + amount);
	}
	if (lender < merchants) {
		const uint32_t kind = operation[0x13] & 0xF;
		// A merchant lends (kinds 2 and 9): checked with the oracle, scenario operation_loans (saves 1, 3, 5, 7).
		fn_4f8fe0(save, lender, town, kind > 4 ? 4 : kind, amount);
	}
}

// The operation 0x2A (0x535C4B → 0x539F30): {0x2A, u32 merchant, u32 event}:
// the merchant pays off a loan of his before it falls due (the moneylender's
// window, 0x5B5B90, which checks first that he has the money). The record
// (0x4D8DB0) has to be a loan (event 0x01) whose debtor [+0x11] is the
// merchant: he pays its [+0x8] and the debtor becomes 0xFF; a creditor
// [+0x10] that is a merchant gets it and becomes 0xFF too. When the event
// falls due it finds neither and goes (0x4DB4E0).
void fn_539f30(SaveGame& save, const uint8_t* operation) {
	const uint32_t index = field(operation, 8);
	if (index >= save.globals.u16(kEventCount) || index >= save.events.size()) {
		// 0x4D8DB0 takes the record just past the array too (the index
		// equal to the count: what the heap has there), which the window
		// never sends; p3vita does nothing.
		return;
	}
	GameMemory<0x18>& e = save.events[index];
	const uint32_t merchant = field(operation, 4);
	if (e.u16(6) != 1 || merchant != e.u8(0x11)) {
		return;
	}
	const uint32_t merchants = save.globals.u16(global::kWorldMerchantCount);
	if (merchant < merchants) {
		MerchantMemory& m = save.merchants[merchant].memory;
		m.s32(0) = int32_t(uint32_t(m.s32(0)) - e.u32(8));
		e.u8(0x11) = 0xFF;
	}
	if (e.u8(0x10) < merchants) {
		MerchantMemory& c = save.merchants[e.u8(0x10)].memory;
		c.s32(0) = int32_t(uint32_t(c.s32(0)) + e.u32(8));
		e.u8(0x10) = 0xFF;
	}
}

namespace {

constexpr uint32_t kEventHead = 0x6DD744;  // u16 ([+0x8] of 0x6DD73C)

// The first record of the queue (from its head, next [+0x4]) of an event
// 0x18 for which `match` holds, or -1 (the game's walks in 0x4D98F0).
template <typename Match>
int find_share(const SaveGame& save, Match match) {
	const uint32_t count = save.globals.u16(kEventCount);
	for (uint32_t i = save.globals.u16(kEventHead); i < count && i < save.events.size(); i = save.events[i].u16(4)) {
		if (save.events[i].u16(6) == 0x18 && match(save.events[i])) {
			return int(i);
		}
	}
	return -1;
}

}  // namespace

// 0x4D98F0 (thiscall on the event queue, with the operation 0x2B's [+4]:
// {u8 company, u8 shares, u8 seller, u8 buyer, u32 price}): company shares,
// as the moneylender's window sends them (0x5B6400, 0x5B66C0: the 12 bytes
// of an entry of its lists, the last 4 what the player owns, unused here).
// The shares of a company (its owner, a merchant) are events 0x18 (0x4E8804,
// events_towns.cpp): [+0x10] the company, [+0x11] who holds them (0xFF: on
// sale, the company's pool), [+0x12] how many (in %), [+0x13] how many are
// offered, [+0x8] when they last changed, [+0xC] the company's value when
// they were bought or last paid. A merchant buys from the pool (the seller
// not a merchant): the pool loses them, he pays the price ([+0x4BC]) and,
// unless he is the owner (buying his own back), they join his holding or a
// new one (its [+0xC] the pool's). A holder sells to the pool (the buyer not a
// merchant): at most what he holds; the owner sells new ones: at most 45 in
// all the company's records; he gets the price ([+0x4B8]) and the pool (or a
// new one, its [+0xC] the owner's [+0x46C], not below 0) gets them.
void fn_4d98f0(SaveGame& save, const uint8_t* operation) {
	GlobalMemory& g = save.globals;
	const uint32_t merchants = g.u16(global::kWorldMerchantCount);
	const uint8_t company = operation[4];
	uint8_t shares = operation[5];  // The game writes it back into the operation.
	const uint8_t seller = operation[6];
	const uint8_t buyer = operation[7];
	const uint32_t price = field(operation, 8);
	const uint32_t now = g.u32(global::kWorldTick);
	if (buyer < merchants) {
		if (seller < merchants) {
			return;
		}
		MerchantMemory& b = save.merchants[buyer].memory;
		// Game bug: the money is compared unsigned, so a buyer in debt (his
		// money negative) buys. Fixed behind fix_game_bugs(): compared signed.
		if (fix_game_bugs() ? int64_t(b.s32(0)) < int64_t(price) : uint32_t(b.s32(0)) < price) {
			return;
		}
		const int pool = find_share(save, [&](const GameMemory<0x18>& e) {
			return e.u8(0x10) == company && e.u8(0x11) == 0xFF;
		});
		if (pool < 0) {
			return;
		}
		GameMemory<0x18>& p = save.events[size_t(pool)];
		if (p.u8(0x12) == 0) {
			return;
		}
		// Game bug: not cut down to what the pool has: the buyer gets `shares`
		// all the same, shares out of nothing (0x4E8804 offers at least 5 of a
		// pool with fewer). Fixed behind fix_game_bugs(): cut to the pool's, the
		// price in proportion.
		uint32_t paid = price;
		if (fix_game_bugs() && shares > p.u8(0x12)) {
			paid = uint32_t(uint64_t(price) * p.u8(0x12) / shares);
			shares = p.u8(0x12);
		}
		p.u8(0x12) = p.u8(0x12) > shares ? uint8_t(p.u8(0x12) - shares) : 0;
		p.u8(0x13) = p.u8(0x13) >= shares ? uint8_t(p.u8(0x13) - shares) : 0;
		p.u32(8) = now;
		const uint32_t value = p.u32(0xC);
		b.s32(0) = int32_t(uint32_t(b.s32(0)) - paid);
		b.u32(0x4BC) += paid;
		if (buyer == company) {
			return;
		}
		const int held = find_share(save, [&](const GameMemory<0x18>& e) {
			return e.u8(0x10) == company && e.u8(0x11) == buyer;
		});
		if (held >= 0) {
			GameMemory<0x18>& h = save.events[size_t(held)];
			h.u8(0x12) = uint8_t(h.u8(0x12) + shares);
			h.u8(0x13) = 0;
			return;
		}
		GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, now + 1)];
		e.u16(6) = 0x18;
		e.u32(8) = 0;
		e.u32(0xC) = value;
		e.u8(0x10) = company;
		e.u8(0x11) = buyer;
		e.u8(0x12) = shares;
		e.u8(0x13) = 0;
		return;
	}
	if (seller >= merchants) {
		return;
	}
	MerchantMemory& s = save.merchants[seller].memory;
	uint32_t value = 0;
	if (seller != company) {
		const int held = find_share(save, [&](const GameMemory<0x18>& e) {
			return e.u8(0x11) == seller && e.u8(0x10) == company;
		});
		if (held < 0) {
			return;
		}
		GameMemory<0x18>& h = save.events[size_t(held)];
		if (h.u8(0x12) == 0) {
			return;
		}
		if (h.u8(0x12) < shares) {
			shares = h.u8(0x12);
			h.u8(0x12) = 0;
		} else {
			h.u8(0x12) = uint8_t(h.u8(0x12) - shares);
		}
		h.u8(0x13) = h.u8(0x13) > shares ? uint8_t(h.u8(0x13) - shares) : 0;
		value = h.u32(0xC);
		h.u32(8) = now;
	} else {
		uint32_t sum = 0;  // All the company's shares.
		const uint32_t count = g.u16(kEventCount);
		for (uint32_t i = g.u16(kEventHead); i < count && i < save.events.size(); i = save.events[i].u16(4)) {
			const GameMemory<0x18>& e = save.events[i];
			if (e.u16(6) == 0x18 && e.u8(0x10) == company) {
				sum += e.u8(0x12);
			}
		}
		if (sum >= 0x2D) {
			return;
		}
		if (shares + sum > 0x2D) {
			shares = uint8_t(0x2D - sum);
		}
		value = s.s32(0x46C) > 0 ? s.u32(0x46C) : 0;
	}
	s.s32(0) = int32_t(uint32_t(s.s32(0)) + price);
	s.u32(0x4B8) += price;
	const int pool = find_share(save, [&](const GameMemory<0x18>& e) {
		return e.u8(0x10) == company && e.u8(0x11) == 0xFF;
	});
	if (pool >= 0) {
		GameMemory<0x18>& p = save.events[size_t(pool)];
		p.u8(0x12) = uint8_t(p.u8(0x12) + shares);
		return;
	}
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, now + 1)];
	e.u16(6) = 0x18;
	e.u32(8) = g.u32(global::kWorldTick);
	e.u32(0xC) = value;
	e.u8(0x10) = company;
	e.u8(0x11) = 0xFF;
	e.u8(0x12) = shares;
	e.u8(0x13) = 0;
}

// La operación 0x37 (en 0x535E04): {0x37, u32 comerciante, u32 ciudad, s32
// cantidad}: el comerciante paga por la ciudad (0x4F8560, como por el
// edificio 0x22: la primera vez en cada ciudad). La encola el tick de los
// comerciantes (desde 0x4F4B30).
void fn_535e04(SaveGame& save, const uint8_t* operation) {
	const uint32_t merchant = field(operation, 4);
	if (merchant >= save.globals.u16(global::kWorldMerchantCount)) {
		return;  // Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
	}
	construction_detail::fn_4f8560(save, int(merchant), field(operation, 8), int32_t(field(operation, 0xC)));
}

// La operación 0x2C (en 0x535C70): {0x2C, u8 tipo, …, u8 comerciante, …, u32
// ciudad, u8 force}: el comerciante encarga un barco al astillero de la
// ciudad (0x52A360, events_towns.cpp). La encola el evento 0x2B (0x4DE790)
// para el comerciante de la IA con menos barcos, con force = 1.
void fn_535c70(SaveGame& save, const uint8_t* operation) {
	const uint32_t town = field(operation, 0xC);
	if (town >= save.globals.u32(global::kWorldTownCount)) {
		return;  // Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
	}
	events_towns_detail::fn_52a360(save, int(town), operation[8], operation[4], operation[0x10]);
}

// La operación 0x38 (en 0x535E3B): {0x38, u32 tick, u32 ciudad}: una
// subasta de barco, el evento 0x0A (events_mayor.cpp) en ese tick con la
// fase [+0x14] = 0 (crear el barco) y la ciudad [+0x15] (módulo el número de
// ciudades), si es al menos 0x2800 ticks (40 días) más tarde. El resto del
// registro queda como estuviera. La encola 0x4DD400 (los primeros días del
// año, en el evento 0x03).
void fn_535e3b(SaveGame& save, const uint8_t* operation) {
	const GlobalMemory& g = save.globals;
	const uint32_t tick = field(operation, 4);
	if (tick < g.u32(global::kWorldTick) + 0x2800) {
		return;  // Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
	}
	const uint32_t town = field(operation, 8) % g.u32(global::kWorldTownCount);
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, tick)];
	e.u16(6) = 0xA;
	e.u8(0x14) = 0;
	e.u8(0x15) = uint8_t(town);
}

// La operación 0x7C (en 0x536642): {0x7C, 16 bytes, …, u16 [+0x10] día del
// año}: la peste, el evento 0x1C (events_rare.cpp) con los 16 bytes en
// [+0x8], ese día de este año ((año · 365 + día) · 256) o, si ya ha pasado,
// al empezar el día siguiente. La encola 0x4DCA90 (en el evento 0x03).
void fn_536642(SaveGame& save, const uint8_t* operation) {
	const GlobalMemory& g = save.globals;
	const uint16_t day = half(operation, 0x10);
	uint32_t tick;
	if (day < g.u16(global::kWorldDayOfYear)) {
		// Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
		tick = (g.u32(global::kWorldTick) | 0xFF) + 1;
	} else {
		tick = (uint32_t(g.u16(global::kWorldYear)) * 365 + day) << 8;
	}
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, tick)];
	e.u16(6) = 0x1C;
	std::memcpy(e.bytes.data() + 8, operation + 4, 0x10);
}

// Operation 0xB8 (0x542F10): {0xB8, u32 kind, u32 flag}: network games'
// sync checks. It queues operations that do nothing when run, with checksums
// of the state: kind 1, one 0xB9 per ship (index, [+0x34], [+0x1C],
// [+0x20], [+0x18]) and kind 2, one 0xBA per town (index, two flag bits of
// [0x2C8], the four classes of citizens, the sums of [0x310 + 4w], of the
// stock and of [0x64 + 4w]), both only with [0x6DF31C]; kind 3, a 0xBC with
// the event queue's and the ship object's counters; kind 4, a 0xBE per
// merchant (money, [+0x46C], [+0x22], index); kind 5, nothing. Any other
// kind: with flag 1 a 0xBB (tick, the merchants' money + [+0x46C] + [+0x22]
// + 100 in 30 bits, the citizens, the counters), and always [0x6DFBF8] = 0
// and a 0xD4 (stock of the towns in 22 bits each, tick | the positions of
// the 16 special ships, the positions of the merchants' and pirates' ships,
// the merchants' cargo). Queued by event 0x03 in network games (kind 0).
void fn_542f10(SaveGame& save, const uint8_t* operation) {
	GlobalMemory& g = save.globals;
	const uint32_t ships = g.u16(global::kShipCount);
	const uint32_t towns = g.u32(global::kWorldTownCount);
	const uint32_t merchants = g.u16(global::kWorldMerchantCount);
	struct Out {
		uint8_t bytes[0x14] = {};
		void u32(size_t o, uint32_t v) { std::memcpy(bytes + o, &v, 4); }
		void u16(size_t o, uint16_t v) { std::memcpy(bytes + o, &v, 2); }
		void u8(size_t o, uint8_t v) { bytes[o] = v; }
	};
	auto queue = [&](const Out& out) { enqueue_operation(save, out.bytes, sizeof(out.bytes)); };
	// The ship object's and the event queue's counters (u16 each).
	auto counters = [&](uint32_t a) { return uint32_t(g.u16(a)); };
	switch (field(operation, 4)) {
	case 1:
		if (g.u32(0x6DF31C) == 0) {
			return;
		}
		for (uint32_t i = 0; i < ships; ++i) {
			const ShipMemory& s = save.ships[i].memory;
			Out out;
			out.u32(0, 0xB9);
			out.u16(4, uint16_t(i));
			out.u16(6, s.u16(0x34));
			out.u32(8, s.u32(0x1C));
			out.u32(0xC, s.u32(0x20));
			out.u32(0x10, s.u32(0x18));
			queue(out);
		}
		return;
	case 2:
		if (g.u32(0x6DF31C) == 0) {
			return;
		}
		for (uint32_t i = 0; i < towns; ++i) {
			const TownMemory& t = save.towns[i].memory;
			Out out;
			out.u32(0, 0xBA);
			out.u8(4, uint8_t(i));
			out.u8(5, uint8_t(((t.s32(0x2C8) >> 7) & 0x3F) + (t.u8(0x2C8) & 0x3F)));
			out.u16(6, uint16_t(t.u16(0x2E4) + t.u16(0x2E0) + t.u16(0x2DC) + t.u16(0x2D8)));
			uint32_t a = 0, stock = 0, b = 0;
			for (uint32_t w = 0; w < 24; ++w) {
				stock += t.u32(4 + 4 * w);
				a += t.u32(0x310 + 4 * w);
				b += t.u32(0x64 + 4 * w);
			}
			out.u32(8, a);
			out.u32(0xC, stock);
			out.u32(0x10, b);
			queue(out);
		}
		return;
	case 3: {
		Out out;
		out.u32(0, 0xBC);
		out.u32(4, (counters(0x6DD746) << 16) + counters(0x6DD744));
		out.u32(8, (counters(0x6DD888) << 16) + counters(0x6DD88A));
		out.u32(0xC, (counters(0x6DD882) << 16) + counters(0x6DD880));
		out.u32(0x10, (counters(0x6DD884) << 16) + counters(0x6DD734));
		queue(out);
		return;
	}
	case 4:
		for (uint32_t i = 0; i < merchants; ++i) {
			const MerchantMemory& m = save.merchants[i].memory;
			Out out;
			out.u32(0, 0xBE);
			out.u32(4, m.u32(0));
			out.u32(8, m.u32(0x46C));
			out.u32(0xC, uint32_t(int32_t(m.s16(0x22))));
			out.u32(0x10, i);
			queue(out);
		}
		return;
	case 5:
		return;  // Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
	default:
		break;
	}
	if (field(operation, 8) == 1) {
		Out out;
		out.u32(0, 0xBB);
		out.u32(4, g.u32(global::kWorldTick));
		uint32_t money = 0;
		for (uint32_t i = merchants; i > 0;) {
			const MerchantMemory& m = save.merchants[--i].memory;
			money += m.u32(0) + m.u32(0x46C);
			money = (money + uint16_t(m.u16(0x22) + 0x64)) & 0x3FFFFFFF;
		}
		out.u32(8, money);
		uint32_t citizens = 0;
		for (uint32_t i = 0; i < towns; ++i) {
			citizens += save.towns[i].memory.u32(0x2D4);
		}
		out.u32(0xC, citizens);
		out.u32(0x10, counters(0x6DD746) + counters(0x6DD744) + counters(0x6DD888) + counters(0x6DD88A) +
			counters(0x6DD882) + counters(0x6DD880) + counters(0x6DD884));
		queue(out);
	}
	g.u32(0x6DFBF8) = 0;
	Out out;
	out.u32(0, 0xD4);
	uint32_t stock = 0;
	for (uint32_t i = 0; i < towns; ++i) {
		for (uint32_t w = 0; w < 24; ++w) {
			stock += save.towns[i].memory.u32(4 + 4 * w) & 0x3FFFFF;
		}
	}
	out.u32(4, stock);
	uint32_t special = 0;
	for (uint32_t k = 0; k < 16; ++k) {
		const uint32_t e = global::kShips + 0x20 + 12 * k;  // 0x6DD7C0
		if (g.u16(e) < uint16_t(ships)) {
			special += uint32_t(g.u16(e + 4)) + g.u16(e + 6);
		}
	}
	out.u32(8, ((special & 0xFFFF) << 16) + (g.u32(global::kWorldTick) & 0xFFFF));
	uint32_t places = 0;
	uint32_t cargo = 0;
	for (uint32_t i = merchants; i > 0;) {
		const MerchantMemory& m = save.merchants[--i].memory;
		for (uint32_t s = m.u16(0xE); s < ships; s = save.ships[s].memory.u16(4)) {
			const ShipMemory& ship = save.ships[s].memory;
			places += ship.u32(0x20) + ship.u32(0x1C);
			for (uint32_t w = 0; w < 24; ++w) {
				cargo += ship.u32(0x54 + 4 * w);
			}
		}
	}
	for (const auto& pirate : save.pirates) {
		if (!pirate) {
			continue;
		}
		for (uint32_t s = pirate->u16(0xC); s < ships; s = save.ships[s].memory.u16(4)) {
			// Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
			places += save.ships[s].memory.u32(0x20) + save.ships[s].memory.u32(0x1C);
		}
	}
	out.u32(0xC, places);
	out.u32(0x10, cargo);
	queue(out);
}

// Operation 0x0A (in 0x535866): {0x0A, u32 ship, u32 owner}: the ship is
// deleted (0x507FB0, Sailing::fn_507fb0) if it still belongs to that owner.
// Queued only by the interface (0x4768B0: scrapping a ship).
void fn_535866(SaveGame& save, Sailing* sailing, const uint8_t* operation) {
	const uint32_t ship = field(operation, 4);
	if (ship >= save.globals.u16(global::kShipCount) || save.ships[ship].memory.u8(0) != field(operation, 8)) {
		return;  // Comprobado solo con estados retocados.
	}
	if (sailing) {
		sailing->fn_507fb0(uint16_t(ship));
	}
}

// Operation 0x87 (0x53FEA0): {0x87, u8 merchant, u8 first name, 4 bytes}: a
// child is born to a married merchant ([+0x31] a town), after [+0x28] and at
// least 279 days (0x11700 ticks) after the wedding [+0x2C], if no child of
// his (the list [+0x34] of 16-byte records among the captains, next in
// [+0x0]) was born less than 279 days ago: a free record (0x5097C0) at the
// head of the list with [+0x2] the first name, [+0x3] his surname, [+0x4]
// the birth tick, [+0x8..+0xB] the 4 bytes and [+0xF] = 0xFF; [+0x36] counts
// the children. A human father gets the letter 0x7A. Queued by 0x4F69B0 (in
// event 0x03, events_towns.cpp).
void fn_53fea0(SaveGame& save, const uint8_t* operation) {
	const GlobalMemory& g = save.globals;
	const uint8_t merchant = operation[4];
	if (merchant >= g.u16(global::kWorldMerchantCount)) {
		return;  // Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
	}
	MerchantMemory& m = save.merchants[merchant].memory;
	const uint32_t now = g.u32(global::kWorldTick);
	if (m.u8(0x31) >= g.u32(global::kWorldTownCount) || now <= m.u32(0x28) || now < m.u32(0x2C) + 0x11700) {
		return;  // Comprobado solo con estados retocados.
	}
	const uint32_t captains = g.u16(global::kCaptainCount);
	for (uint32_t c = m.u16(0x34); c < captains; c = save.captains[c].u16(0)) {
		if (now - 0x11700 < save.captains[c].u32(4)) {
			return;  // Comprobado solo con estados retocados.
		}
	}
	const uint16_t child = events_detail::fn_5097c0(save, true);
	CaptainMemory& c = save.captains[child];
	c.u16(0) = m.u16(0x34);
	m.u16(0x34) = child;
	++m.u8(0x36);
	c.u8(2) = operation[5];
	c.u8(3) = m.u8(0x1D);
	c.u32(4) = now;
	std::memcpy(c.bytes.data() + 8, operation + 6, 4);
	c.u8(0xF) = 0xFF;
	if (m.u16(0x8) == 0) {
		// The letter 0x7A to the human father (0x53FFC7..0x53FFF4): [+0x5]
		// his home town, u16 [+0x8] the child's first name.
		Letter letter(0x7A);
		letter.entry.u8(5) = m.u8(0x19);
		letter.entry.u16(8) = operation[5];
		send_letter(save, merchant, letter);
	}
}

// Operation 0x65 (0x53DD70): {0x65, u32 merchant}: the merchant goes
// bankrupt (0x4F5D80, merchants.cpp). Queued by the AI of a merchant with
// less than −50000 who cannot borrow (0x4F4E50).
void fn_53dd70(SaveGame& save, const uint8_t* operation) {
	const uint32_t merchant = field(operation, 4);
	if (merchant >= save.globals.u16(global::kWorldMerchantCount)) {
		return;  // The game does not check it (0x5303C0).
	}
	merchants_detail::fn_4f5d80(save, int(merchant));
}

// Operation 0x60 (0x53DA90): {0x60, u32 tick, u16 [+0x8], 8 bytes [+0xA]}: a
// merchant's wedding is arranged: if the tick is still to come, event 0x15
// (events_towns.cpp, fn_4e7c74) on that day (the low byte of the tick, also
// in the operation, set to 0) with [+0x8] the tick, [+0xC] the u16 and
// [+0xE..+0x15] the 8 bytes ([+0x15] the merchant). If [+0x11] is a merchant,
// the marks of his letters are cleared (0x4D6FD0) and he gets the letter
// 0x33. Queued by the mission interpreter (0x4D6C90) and the interface: the
// branch of event 0x16 that would queue it for the AI is dead code.
void fn_53da90(SaveGame& save, const uint8_t* operation) {
	uint32_t tick = field(operation, 4);
	if (save.globals.u32(global::kWorldTick) >= tick) {
		return;  // Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
	}
	tick &= 0xFFFFFF00;
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, tick)];
	e.u16(6) = 0x15;
	e.u32(8) = tick;
	e.u16(0xC) = half(operation, 8);
	std::memcpy(e.bytes.data() + 0xE, operation + 0xA, 8);
	const uint8_t merchant = operation[0x11];
	if (merchant < save.globals.u16(global::kWorldMerchantCount)) {
		events_towns_detail::fn_4d6fd0(save, save.merchants[merchant].memory.u16(0xA));
		// The letter 0x33 to him (0x53DB58..0x53DBC7): [+0x5] the operation's
		// [+0xE], u16 [+0x8] his home town, [+0xA] the operation's [+0xC] << 8
		// + [+0xD], [+0xC] the wedding's day << 8 + month − 1 and [+0xE] its
		// year (0x5311D0 on the tick without its low byte).
		Letter letter(0x33);
		letter.entry.u8(5) = operation[0xE];
		letter.entry.u16(8) = save.merchants[merchant].memory.u8(0x19);
		letter.entry.u16(0xA) = uint16_t((operation[0xC] << 8) + operation[0xD]);
		uint8_t day = 0;
		uint8_t month = 0;
		uint16_t year = 0;
		events_mayor_detail::fn_5311d0(tick, day, month, year);
		letter.entry.u16(0xC) = uint16_t((day << 8) + month - 1);
		letter.entry.u16(0xE) = year;
		send_letter(save, merchant, letter);
	}
}

// La operación 0x13 (0x538C40): {0x13, u32 barco, u32 capitán}: el capitán
// del barco de un jugador humano se retira: el evento 0x27 (events_towns.cpp,
// fn_4ddc00) en el tick siguiente con el barco y el capitán, y [+0xE] del
// capitán a 1 (se está retirando). A human owner gets the letter 0x52. La
// encola 0x4DCEA0 (en el evento 0x03) para los barcos de los jugadores
// humanos; a los de la IA les crea el evento directamente.
void fn_538c40(SaveGame& save, const uint8_t* operation) {
	const GlobalMemory& g = save.globals;
	const uint32_t ship = field(operation, 4);
	const uint32_t captain = field(operation, 8);
	if (captain >= g.u16(global::kCaptainCount) || ship >= g.u16(global::kShipCount)) {
		return;  // Comprobado solo con estados retocados.
	}
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, g.u32(global::kWorldTick) + 1)];
	e.u16(6) = 0x27;
	e.u32(8) = ship;
	e.u32(0xC) = captain;
	save.captains[captain].u8(0xE) = 1;
	const ShipMemory& sh = save.ships[ship].memory;
	const uint8_t owner = sh.u8(0);
	if (owner >= g.u16(global::kWorldMerchantCount) || save.merchants[owner].memory.u16(0x8) != 0) {
		return;
	}
	// The letter 0x52 to the human owner (0x538CFB..0x538D76): [+0x5] the
	// ship's town [+0x39] (his home town [+0x19] if it is no town), u16
	// [+0x8], [+0xA] the captain's first name and surname, [+0xC] the ship's
	// name [+0x15E], [+0xE] the owner's names ([+0x1C] << 8 + [+0x1D]).
	const MerchantMemory& m = save.merchants[owner].memory;
	const CaptainMemory& c = save.captains[captain];
	Letter letter(0x52);
	const uint8_t town = sh.u8(0x39);
	letter.entry.u8(5) = town < g.u32(global::kWorldTownCount) ? town : m.u8(0x19);
	letter.entry.u16(8) = c.u8(2);
	letter.entry.u16(0xA) = c.u8(3);
	letter.entry.u16(0xC) = sh.u16(0x15E);
	letter.entry.u16(0xE) = uint16_t((m.u8(0x1C) << 8) + m.u8(0x1D));
	send_letter(save, owner, letter);
}

// La operación 0x7D (en 0x5366A0): {0x7D, u32 tick, 12 bytes}: un incendio
// en una ciudad, el evento 0x1D (events_towns.cpp) en ese tick (como pronto,
// ahora) con los 16 bytes en [+0x8]: [+0xC] los que han ardido (0), [+0xE]
// cuántos pueden arder, la celda ([+0x10], [+0x11]) y la ciudad [+0x12]. La
// encola 0x4DCBF0 (en el evento 0x03).
void fn_5366a0(SaveGame& save, const uint8_t* operation) {
	uint32_t tick = save.globals.u32(global::kWorldTick);
	if (tick < field(operation, 4)) {
		tick = field(operation, 4);
	}
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, tick)];
	e.u16(6) = 0x1D;
	std::memcpy(e.bytes.data() + 8, operation + 4, 0x10);
}

// La operación 0x7F (en 0x5366E8): {0x7F, u32 ciudad ×3, u32 tick}: si el
// tick es futuro, cada ciudad que exista pasa a tener el bit 0x4000000 de
// [0x2C8] y un evento 0x35 (que se lo quita, events_rare.cpp) en ese tick con
// la ciudad en [+0x8]. La encola el evento 0x0D (0x4E3F44).
void fn_5366e8(SaveGame& save, const uint8_t* operation) {
	const uint32_t tick = field(operation, 0x10);
	if (tick <= save.globals.u32(global::kWorldTick)) {
		return;  // Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
	}
	for (size_t k = 0; k < 3; ++k) {
		const uint32_t town = field(operation, 4 + 4 * k);
		if (town >= save.globals.u32(global::kWorldTownCount)) {
			continue;
		}
		save.towns[town].memory.u32(0x2C8) |= 0x4000000;
		GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, tick)];
		e.u16(6) = 0x35;
		e.u32(8) = town;
	}
}

// La operación 0x80 (en 0x536756): {0x80, u32, u16 [+0x8], …, u16 [+0xC]}:
// si la cola tiene más de 0x80 registros y el registro 0x80 (siempre ese:
// 0x4D8DB0 con 0x80) es un encargo (tipo 5, events_big.cpp), sus [+0x12] y
// [+0x14] pasan a ser los dos u16. La encola el evento 0x05.
// Game bug: the operation carries the record it is for in [+4] (the task
// that starts), but the handler always uses record 0x80. With
// fix_game_bugs() it uses [+4].
void fn_536756(SaveGame& save, const uint8_t* operation) {
	const uint32_t record = fix_game_bugs() ? field(operation, 4) : 0x80;
	if (save.globals.u16(kEventCount) <= record) {
		// Checked with the oracle (save 5's queue has 0x80 records): scenario operation_guards (saves
		// 1, 3, 5, 7).
		return;
	}
	GameMemory<0x18>& e = save.events[record];
	if (e.u16(6) != 5) {
		return;
	}
	e.u16(0x12) = half(operation, 8);
	e.u16(0x14) = half(operation, 0xC);
}

// La operación 0x83 (en 0x5367B5): {0x83, u32 tick, u32, u8}: un evento 0x00
// (grano, events.cpp) en ese tick con [+0x8] el tick, [+0xC] el u32 y
// [+0x10] el u8. La encola el evento 0x00.
void fn_5367b5(SaveGame& save, const uint8_t* operation) {
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, field(operation, 4))];
	e.u16(6) = 0;
	e.u32(8) = field(operation, 4);
	e.u32(0xC) = field(operation, 8);
	e.u8(0x10) = operation[0xC];
}

// La operación 0x84 (0x53FCA0): {0x84, u32 tick, 4 bytes [+0x8], 6 bytes
// [+0xC]}: el encargo del jefe de la Hansa, el evento 0x30 (events_rare.cpp)
// en ese tick (como pronto, dentro de 0x100) con [+0x8] el tick de ahora,
// [+0xC..+0x11] los 6 bytes y [+0x12..+0x15] los 4. La encolan el torneo
// (0x4DD530, en el evento 0x03) y la temporada de piratas (evento 0x1F).
void fn_53fca0(SaveGame& save, const uint8_t* operation) {
	const uint32_t now = save.globals.u32(global::kWorldTick);
	uint32_t tick = now + 0x100;
	if (field(operation, 4) > tick) {
		tick = field(operation, 4);
	}
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, tick)];
	e.u16(6) = 0x30;
	e.u32(8) = now;
	std::memcpy(e.bytes.data() + 0xC, operation + 0xC, 6);
	std::memcpy(e.bytes.data() + 0x12, operation + 8, 4);
}

// La operación 0x67 (0x53DDF0): {0x67, u32 administrador, u32 comerciante,
// u32 ciudad, u8 habilidad}: si el administrador sigue en la oficina del
// comerciante en la ciudad ([+0x2F2]), su habilidad [+0xA] pasa a ser la de
// la operación y su sueldo [+0xC] = habilidad / 43 · 20 + 10 (0x4FE160). The
// merchant gets the letter 0x7C. La encola 0x4DCEA0 (en el evento 0x03).
void fn_53ddf0(SaveGame& save, const uint8_t* operation) {
	const GlobalMemory& g = save.globals;
	const uint32_t administrator = field(operation, 4);
	const uint32_t merchant = field(operation, 8);
	const uint32_t town = field(operation, 0xC);
	if (merchant >= g.u16(global::kWorldMerchantCount) || town >= g.u32(global::kWorldTownCount) ||
		administrator >= g.u16(global::kCaptainCount)) {
		return;  // Comprobado solo con estados retocados.
	}
	const int office = facilities_detail::fn_5308a0(save, merchant & 0xFFFF, town & 0xFFFF);
	if (office < 0 || save.offices[size_t(office)].memory.u16(0x2F2) != administrator) {
		return;  // Comprobado solo con estados retocados.
	}
	CaptainMemory& c = save.captains[administrator];
	c.u8(0xA) = operation[0x10];
	c.u16(0xC) = uint16_t(c.u8(0xA) / 43 * 20 + 10);
	// The letter 0x7C to the merchant (0x53DE9D..0x53DED7): [+0x5] the town,
	// u16 [+0x8] the office's wages (0x500F10: the administrator's [+0xC]
	// plus the office's u16 [+0x2D2]), [+0xA], [+0xC] the administrator's
	// first name and surname.
	const OfficeMemory& o = save.offices[size_t(office)].memory;
	Letter letter(0x7C);
	letter.entry.u8(5) = operation[0xC];
	letter.entry.u16(8) = uint16_t(c.u16(0xC) + o.u16(0x2D2));
	letter.entry.u16(0xA) = c.u8(2);
	letter.entry.u16(0xC) = c.u8(3);
	send_letter(save, int32_t(merchant), letter);
}

// La operación 0x48 (0x53B010): {0x48, u32 tick, u32 [+0x8], u8 ciudad, u8
// tipo, 3 bytes [+0xE]}: una propuesta para la ciudad. Con consejo ([0x2C8] &
// 0xE0), la votación: el evento 0x2E (events_rare.cpp) en el tick ajustado
// por 0x4D9FC0 (el juego lo cambia en la operación) con [+0x8], la ciudad, el
// tipo, 0 en [+0xE..+0x13] y los 3 bytes en [+0x14]. Sin consejo se aplica ya:
// el tipo 0, el impuesto extraordinario (el evento 0x31 en el tick siguiente
// con [+0x8] y la ciudad); el tipo 3, el impuesto por cabeza [+0xE]
// (0x52A150); los demás, nada. La encolan las obras de la ciudad (0x51E070 y
// 0x51E270, construction.cpp).
void fn_53b010(SaveGame& save, const uint8_t* operation) {
	const uint8_t town = operation[0xC];
	if (town >= save.towns.size()) {
		return;  // El juego no lo mira (leería fuera de las ciudades).
	}
	if (save.towns[town].memory.u8(0x2C8) & 0xE0) {
		uint32_t tick = field(operation, 4);
		construction_detail::fn_4d9fc0(save, tick, town);
		GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, tick)];
		e.u16(6) = 0x2E;
		e.u32(8) = field(operation, 8);
		e.u8(0xC) = town;
		e.u8(0xD) = operation[0xD];
		std::memset(e.bytes.data() + 0xE, 0, 6);
		std::memcpy(e.bytes.data() + 0x14, operation + 0xE, 3);
		return;
	}
	// Comprobado solo con estados retocados.
	if (operation[0xD] == 0) {
		GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, save.globals.u32(global::kWorldTick) + 1)];
		e.u16(6) = 0x31;
		e.u32(8) = field(operation, 8);
		e.u8(0xC) = town;
	} else if (operation[0xD] == 3) {
		events_rare_detail::fn_52a150(save, town, operation[0xE]);
	}
}

// La operación 0x28 (0x53D190): {0x28, u32 ciudad, u32 comerciante, u32 x,
// u32 y}: el comerciante pone en venta su edificio de la celda (x, y) de la
// ciudad, si tiene oficina en ella (y, en una ciudad con [0x2C8] & 0x10, si
// la celda tiene el bit 7). Lo busca por su celda en las listas de su
// oficina ([+0x2D0] y [+0x2CE]) y en la de la ciudad ([0x762]); si lo
// encuentra, la subasta (0x4DAB00, events.cpp); si no, recuenta la oficina
// (0x4FF2D0). La encola la IA de un comerciante con deudas (0x4F5A20), que
// casi nunca lo hace por un fallo del juego (ver el roadmap).
void fn_53d190(SaveGame& save, const uint8_t* operation) {
	const GlobalMemory& g = save.globals;
	const uint32_t town = field(operation, 4);
	const uint32_t merchant = field(operation, 8);
	if (town >= g.u32(global::kWorldTownCount) || merchant >= g.u16(global::kWorldMerchantCount)) {
		return;  // Checked with the oracle (each): scenario operation_sale (saves 3, 5, 7).
	}
	const int office = facilities_detail::fn_5308a0(save, merchant & 0xFFFF, town & 0xFFFF);
	if (office < 0) {
		return;  // Checked with the oracle: scenario operation_sale (saves 3, 5, 7).
	}
	const SavedTown& st = save.towns[town];
	const uint32_t x = field(operation, 0xC);
	const uint32_t y = field(operation, 0x10);
	if (st.memory.u8(0x2C8) & 0x10) {
		// Checked with the oracle (the cell with the bit 0x80 and without it): scenario operation_sale (saves 3, 5, 7).
		// The game doesn't look whether the cell is in the grid (it reads past
		// it); p3vita doesn't read there.
		const size_t cell = size_t(st.memory.u32(0x7AC) * x + y);
		if (cell >= st.grid.size() || !(st.grid[cell] & 0x80)) {
			return;
		}
	}
	const OfficeMemory& o = save.offices[size_t(office)].memory;
	const uint32_t records = st.memory.u16(0x776);
	for (const uint16_t head : {o.u16(0x2D0), o.u16(0x2CE), st.memory.u16(0x762)}) {
		for (uint32_t r = head; r < records; r = st.records[r].next) {
			if (st.records[r].x == x && st.records[r].y == y) {
				events_detail::fn_4dab00(save, operation[4], r);
				return;
			}
		}
	}
	fn_4ff2d0(save, office);  // Checked with the oracle: scenario operation_sale (saves 3, 5, 7).
}

// La operación 0x7E (0x53F8B0): {0x7E, 4 u32 al azar}: la temporada de
// piratas que viene, el evento 0x1F (events_rare.cpp) el 1 de marzo (día 59,
// 0x672D7C) del año que toca (el siguiente desde ese día) más ([+4] & 0xF)
// días y ([+8] & 0xFF) ticks. En [+0x8 + k], las ciudades sin el bit 0x40 de
// [0x2C8], en orden (el juego no mira cuántas: con más de 16 escribiría en el
// registro siguiente), barajadas según los bits 0..2 del primer byte de cada
// u32 (cambia [+0x8 + c] con [+0xC + c], [+0x8 + 2c] con [+0x9 + 2c] y [+0x8
// + c] con [+0xF − c]); y en [+0x10..+0x17], los dos primeros bytes de cada
// u32. La encola el evento 0x1F al acabar la temporada.
void fn_53f8b0(SaveGame& save, const uint8_t* operation) {
	constexpr uint32_t kMarch = 59;  // 0x672D7C
	const GlobalMemory& g = save.globals;
	uint32_t year = g.u16(global::kWorldYear);
	if (g.u16(global::kWorldDayOfYear) >= kMarch) {
		++year;
	}
	const uint32_t tick = ((year * 365 + (field(operation, 4) & 0xF) + kMarch) << 8) + (field(operation, 8) & 0xFF);
	const uint16_t index = events_detail::fn_4d8cf0(save, tick);
	save.events[index].u16(6) = 0x1F;
	uint32_t k = 0;
	for (uint32_t i = 0; i < g.u32(global::kWorldTownCount); ++i) {
		if (save.towns[i].memory.u8(0x2C8) & 0x40) {
			continue;
		}
		const size_t at = size_t(index) * 0x18 + 8 + k++;
		if (at < save.events.size() * 0x18) {
			// Past [+0x17], into the next record: checked with the oracle, scenario operation_guards (saves 1, 3, 5, 7)
			// (18 towns; in save 5 the record is the last one and the game writes
			// past its heap block, which p3vita can't).
			save.events[at / 0x18].u8(at % 0x18) = uint8_t(i);
		}
	}
	GameMemory<0x18>& e = save.events[index];
	for (uint32_t c = 0; c < 4; ++c) {
		const uint8_t bits = operation[4 + 4 * c];
		if (!(bits & 1)) {
			std::swap(e.u8(8 + c), e.u8(0xC + c));
		}
		if (!(bits & 2)) {
			std::swap(e.u8(8 + 2 * c), e.u8(9 + 2 * c));
		}
		if (!(bits & 4)) {
			std::swap(e.u8(8 + c), e.u8(0xF - c));
		}
	}
	for (uint32_t c = 0; c < 4; ++c) {
		e.u8(0x10 + 2 * c) = operation[4 + 4 * c];
		e.u8(0x11 + 2 * c) = operation[5 + 4 * c];
	}
}

// La operación 0x61 (0x53DBF0): {0x61, u32 comerciante, u32 tick, u32
// ciudad, u32 precio}: la fiesta del comerciante en la ciudad, si tiene el
// precio (el juego compara sin signo: con deudas, también): el evento 0x07
// (events_towns.cpp, fn_4e23a4) en ese tick con [+0x8] el comerciante,
// [+0xC] la ciudad y [+0x10] = 2, y se le cobra el precio de su dinero y de
// [+0x4BC]. La encola 0x5316E0 (en el evento 0x03) con precio 0 para un
// comerciante de la IA; con precio, la interfaz.
void fn_53dbf0(SaveGame& save, const uint8_t* operation) {
	const uint32_t merchant = field(operation, 4);
	if (merchant >= save.globals.u16(global::kWorldMerchantCount)) {
		return;  // Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
	}
	MerchantMemory& m = save.merchants[merchant].memory;
	const uint32_t price = field(operation, 0x10);
	if (m.u32(0) < price) {
		return;  // Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
	}
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, field(operation, 8))];
	e.u16(6) = 7;
	e.u32(8) = merchant;
	e.u32(0xC) = field(operation, 0xC);
	e.u32(0x10) = 2;
	m.u32(0) -= price;
	m.u32(0x4BC) -= price;
}

// La operación 0x81 (0x53FA80): {0x81, u32 comerciante, u32 tipo, u32 plazo,
// u8 [+0x10]}: un encargo de la dieta de la Hansa al comerciante: el evento
// 0x05 (events_big.cpp) a tick + plazo + 256 con el byte bajo a 0x80 (a
// mitad de día), con [+0x8] el comerciante, [+0x9] el [+0x10], [+0xA] 0,
// [+0xB] su ciudad natal, [+0xC] el tick de ahora, [+0x10] el tipo (como
// mucho 7) y [+0x11] 0. La encola la dieta (evento 0x21, desde 0x4DCE97).
void fn_53fa80(SaveGame& save, const uint8_t* operation) {
	GlobalMemory& g = save.globals;
	const uint32_t merchant = field(operation, 4);
	if (merchant >= g.u16(global::kWorldMerchantCount)) {
		return;  // Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
	}
	const uint32_t tick = g.u32(global::kWorldTick);
	const uint32_t when = ((field(operation, 0xC) + tick + 0x100) & 0xFFFFFF00) | 0x80;
	GameMemory<0x18>& e = save.events[events_detail::fn_4d8cf0(save, when)];
	e.u16(6) = 5;
	e.u8(0x8) = uint8_t(merchant);
	e.u8(0x9) = operation[0x10];
	e.u8(0xA) = 0;
	e.u8(0xB) = save.merchants[merchant].memory.u8(0x19);
	e.u32(0xC) = tick;
	e.u8(0x10) = operation[0x8] > 7 ? 7 : operation[0x8];
	e.u8(0x11) = 0;
}

// La operación 0x78 (0x5051E0, sobre el objeto de los barcos): {0x78, u8 a,
// …, u16 b, u16 x, u16 y}: un barco nuevo (0x507D60 con 4) en el primer hueco
// libre de las 16 entradas de 12 bytes de [0x6DD7C0] (su índice, b, x, y, a
// y 0), en (x, y) y en la lista por x (0x50CE50). Sin hueco, nada. (La marca
// de la interfaz [*0x6CBDD8 + 0x2B22] no se reproduce.)
void fn_5051e0(SaveGame& save, Sailing* sailing, const uint8_t* operation) {
	GlobalMemory& g = save.globals;
	constexpr uint32_t kSpecial = global::kShips + 0x20;  // 0x6DD7C0
	const uint16_t ships = g.u16(global::kShipCount);
	int slot = -1;
	for (int i = 0; i < 16; ++i) {
		if (g.u16(kSpecial + 12 * uint32_t(i)) >= ships) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		return;  // Checked with the oracle: scenario operation_guards (saves 1, 3, 5, 7).
	}
	const uint32_t e = kSpecial + 12 * uint32_t(slot);
	g.u16(e + 8) = operation[4];
	g.u16(e + 2) = half(operation, 8);
	g.u16(e + 0xA) = 0;
	uint16_t index = 0;
	events_mayor_detail::fn_507d60(save, index, uint32_t(slot), 4);
	g.u16(e) = index;
	g.u16(e + 4) = half(operation, 0xC);
	g.u16(e + 6) = half(operation, 0x10);
	SavedShip& s = save.ships[index];
	s.memory.u16(0x1E) = half(operation, 0xC);
	s.memory.u16(0x22) = half(operation, 0x10);
	if (sailing) {
		sailing->insert_x(index, 0xFFFFFFFF);
	}
}

// The operation 0x4E (0x53B850): {0x4E, u32 [+4] captain, u32 [+8] ship,
// u32 [+0xC] sailors, u32 [+0x10] town}: a player's ship turns pirate (the
// window 0x5D5430 queues it). The ship must be a merchant's (without
// [+0x8] & 4), docked in a town (state ≤ 1), without a captain, and the
// captain in that town's list (0x526AA0 takes him out). It gets the captain,
// the merchant's [+0x16] goes up by 0x672DD8 (256, at most 0x7FFF), the ship
// goes to the pirates (0x509250 with no winner, price 0), takes on sailors
// (0x5184F0's, at most [+0xC] + 1), its cargo is counted (0x5182B0), its
// pirate town [+0x159] is [+0x10] (0x51A820) and [+0x4C] = the tick + (180 +
// rand % 180) days (the generator 0x46C320 seeded with the tick).
void fn_53b850(SaveGame& save, Sailing* sailing, const uint8_t* operation) {
	uint32_t f[5];
	std::memcpy(f, operation, sizeof(f));
	GlobalMemory& g = save.globals;
	if (f[2] >= g.u16(global::kShipCount)) {
		return;
	}
	ShipMemory& s = save.ships[f[2]].memory;
	const uint32_t merchants = g.u16(global::kWorldMerchantCount);
	const uint32_t towns = g.u32(global::kWorldTownCount);
	if (s.u8(0x0) >= merchants || s.u16(0x134) > 1 || s.u16(0x42) < g.u16(global::kCaptainCount) ||
		s.u8(0x39) >= towns) {
		return;
	}
	if ((save.merchants[s.u8(0x0)].memory.u8(0x8) & 4) == 4) {
		return;
	}
	if (!ship_trade_detail::fn_526aa0(save, s.u8(0x39), f[1])) {
		return;
	}
	s.u16(0x42) = uint16_t(f[1]);
	MerchantMemory& m = save.merchants[s.u8(0x0)].memory;
	constexpr int32_t kChange = 0x100;  // 0x672DD8 (s16)
	const int32_t r = m.s16(0x16);
	if (kChange > 0) {
		m.s16(0x16) = r < 0x7FFF - kChange ? int16_t(r + kChange) : int16_t(0x7FFF);
	} else {
		m.s16(0x16) = r + 0x8000 > -kChange ? int16_t(r + kChange) : int16_t(-0x8000);
	}
	events_mayor_detail::fn_509250(save, sailing, f[2], 0xFFFF, 0, 0, s.u8(0x39));
	const uint32_t hire = uint32_t(ship_ai_detail::fn_5184f0(save, int(f[2])));
	if (hire > f[3] + 1) {
		s.u16(0x40) = uint16_t(s.u16(0x40) + uint16_t(f[3]) + 1);
	} else {
		s.u16(0x40) = uint16_t(s.u16(0x40) + hire);
	}
	recalculate_cargo(save.ships[f[2]]);
	s.u8(0x159) = f[4] < towns && s.u8(0x0) >= merchants ? uint8_t(f[4]) : 0xFF;  // 0x51A820
	const uint32_t tick = g.u32(global::kWorldTick);
	uint32_t x = 0xA205B063u - tick * 0x44BF19B3u;  // 0x46C350
	x = (x & 1) ? (x >> 1) | 0x80000000u : x >> 1;
	s.u32(0x4C) = tick + ((x % 0xB4 + 0xB4) << 8);
}

}  // namespace p3::data::tick
