// Las misiones que aprueba la dieta de la Hansa (evento 0x21, events_mayor.cpp):
// con las propuestas 0, 1 y 2 su registro pasa a ser un evento 0x34, 0x24 o
// 0x23 al día siguiente, que atiende 0x4E09D4 con la flota de la Hansa
// (0x50AC40: uno o dos barcos de guerra nuevos de los comerciantes de la IA de
// las ciudades de la Hansa, en un convoy).
//
//   0x34  la caza de los piratas desde la ciudad del alderman: la flota sale
//         al mar, persigue a los barcos piratas y vuelve.
//   0x24  la ciudad [+0x14] queda protegida ([0x2C8] |= 0x600) mientras su
//         satisfacción [0x304] no baje de −20.
//   0x23  con un solo barco, la ciudad [+0x14] ([0x2C8] |= 0x400) da
//         encargos (operación 0x81) a los jugadores humanos con un barco en
//         ella, hasta un plazo o mientras la satisfacción no baje de −10.
//
// Campos del evento: [+0x8..+0xF] los comerciantes que participan (0xFF
// ninguno; en el 0x34, desde la fase 2, [+0x8] u32 la lista de avisos de la
// caza, SaveGame::hunt_notices, [+0xC], [+0xD] los nombres del capitán pirata
// buscado y [+0xE] el resultado), u32 [+0x10] un plazo, u8 [+0x14] la
// ciudad, u8 [+0x15] la fase (0..10), u16 [+0x16] el convoy de la flota. The
// letters 0x48 (phase 0), 0x4C (phase 2), 0x5C (phase 5) and the hunt's last
// one, 0x3D (phase 10, 0x4F3AF0), are sent as in the game (src/p3data/
// mail.cpp). Not reproduced: the notices and windows to the player (0x548CA0
// with kind 0x2A and 0x469380, 0x469AF0, phase 1 of the 0x24).
//
// The hunt's list (0x4F3980..0x4F3CE0): from phase 7, while the fleet is out
// (0x503B00), a notice goes to its head for each of its ships sunk (0x50C436,
// battle_end.cpp fn_50bfd0), seized (0x54086F, capture.cpp fn_540520) or sunk
// in a storm (0x5022E0, convoys_sea.cpp), and for each ship its crews seize
// (0x540B3D, fn_540520: with the hunted pirate's captain, [+0xE] 1 the
// success). The result [+0xE]: 0 he wasn't found (phase 2), 1 success, 2 he
// fled, 3 the fleet beaten (0x50C09A, 0x50C0C6: fn_50bfd0).

#include <cstring>
#include <vector>

#include "p3data/game_fixes.hpp"
#include "p3data/game_texts.hpp"
#include "p3data/letters.hpp"
#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {

namespace events_mayor_detail {
uint16_t fn_507d60(SaveGame& save, uint16_t& out, uint32_t town, uint32_t mode);
void fn_52a8d0(SaveGame& save, int town, int ship, uint32_t type);
void fn_5181d0(SaveGame& save, int ship);
void fn_509a50(SaveGame& save, int ship);
}  // namespace events_mayor_detail
namespace events_detail {
uint16_t fn_5097c0(SaveGame& save, bool keep);
void fn_4fe190(CaptainMemory& c);
}  // namespace events_detail
namespace ship_ai_detail {
int32_t fn_5184f0(SaveGame& save, int ship);
}  // namespace ship_ai_detail

namespace {

using Event = GameMemory<0x18>;

constexpr uint32_t kEvents = 0x6DD73C;
constexpr uint32_t kEventHead = kEvents + 0x8;  // u16
constexpr uint32_t kTick = global::kWorldTick;
constexpr uint32_t kTownIds = 0x6DE4B8;  // u8 por ciudad: su número de StadtDaten.ini.
constexpr uint32_t kTownTable = 0x6DDB90;  // 0x34 bytes por número: [+0x20], [+0x24] x, y en el mapa.
constexpr uint32_t kAldermanBusy = 0x6DF31C;  // u32
constexpr uint32_t kHanseTown = 0x6DE52E;  // u8 ([+0x8E] of the world): the town of the Hanse's leader.
constexpr uint32_t kFreeConvoy = global::kShips + 0xE4;  // u16 primer convoy libre.
constexpr uint32_t kDockedHead = global::kShips + 0xE8;  // u16 lista de barcos en puerto.
constexpr uint32_t kSeaHead = global::kShips + 0xEA;  // u16 (0x6DD88A)
constexpr uint32_t kConvoyHead = global::kShips + 0xEC;  // u16 lista de convoyes.
constexpr uint32_t kConvoyCount = global::kShips + 0xF6;  // u16

uint32_t town_count(const SaveGame& save) {
	return save.globals.u32(global::kWorldTownCount);
}

uint32_t merchant_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldMerchantCount);
}

uint32_t ship_count(const SaveGame& save) {
	return save.globals.u16(global::kShipCount);
}

uint32_t convoy_count(const SaveGame& save) {
	return save.globals.u16(kConvoyCount);
}

// ¿Está el convoy en la lista de convoyes ([0x6DD88C], por [+0x8])?
bool listed(const SaveGame& save, uint16_t convoy) {
	for (uint32_t c = save.globals.u16(kConvoyHead);; c = save.convoys[c].u16(0x8)) {
		if (c == convoy) {
			return true;
		}
		if (c >= convoy_count(save)) {
			return false;
		}
	}
}

// Un campo de la fila de la ciudad en la tabla de StadtDaten.ini (por su número).
uint32_t table(const SaveGame& save, uint32_t town, uint32_t field) {
	return save.globals.u32(kTownTable + 0x34 * uint32_t(save.globals.u8(kTownIds + town)) + field);
}

}  // namespace

namespace diet_detail {

// 0x4F92B0 (sobre el comerciante, con la ciudad y `armed`): cuántos barcos
// pondría en una misión de la Hansa, uno por cada cinco de los suyos con
// casco, en puerto o navegando (estados < 4, 0xF, 0x10), sin la marca 2 de
// [+0xE] si la ciudad tiene el bit 1 de [0x2C8] y, con `armed`, que puedan ir
// armados (0x519C40).
uint16_t fn_4f92b0(const SaveGame& save, uint32_t merchant, uint32_t town, bool armed) {
	const uint8_t skip = (save.towns[town].memory.u8(0x2C8) & 1) ? 2 : 0;
	uint32_t n = 0;
	for (uint32_t s = save.merchants[merchant].memory.u16(0xE); s < ship_count(save); s = save.ships[s].memory.u16(0x4)) {
		const ShipMemory& ship = save.ships[s].memory;
		const uint16_t status = ship.u16(0x134);
		if (ship.s32(0x18) <= 0 || !(status < 4 || status == 0xF || status == 0x10) || (ship.u8(0xE) & skip)) {
			continue;
		}
		if (armed && !ship_ai_detail::fn_519c40(save, int(s))) {
			continue;
		}
		++n;
	}
	return uint16_t(n / 5);
}

// 0x4F8CA0 (sobre el comerciante, con la ciudad): su reputación baja 3
// (0x66FD54) en ella o, con una ciudad que no existe, en todas las de la
// Hansa (bit 0x40 de [0x2C8]); [+0x22] baja 60 y [+0x1E], 1 (si no es 0).
void fn_4f8ca0(SaveGame& save, uint32_t merchant, uint32_t town) {
	MerchantMemory& m = save.merchants[merchant].memory;
	auto lower = [&](uint32_t t) {
		float& r = m.f32(0x120 + 12 * t);
		r = float(double(r) - 3.0);
	};
	if (town < town_count(save)) {
		lower(town);  // Only the battles pass a town. Checked with the oracle: scenario diet_branches
		// (saves 3 and 5, a direct call).
	} else {
		for (uint32_t t = 0; t < town_count(save); ++t) {
			if (save.towns[t].memory.u8(0x2C8) & 0x40) {
				lower(t);
			}
		}
	}
	m.u16(0x22) = uint16_t(m.u16(0x22) + 0xFFC4);
	if (m.u8(0x1E) != 0) {
		--m.u8(0x1E);
	}
}

// 0x4F9120 (sobre el comerciante, con la ciudad y el casco del barco que
// pone): su reputación en la ciudad sube casco · [+0x464] · 0,05 / 2800
// (0x672DC0, 0x66BC64).
void fn_4f9120(SaveGame& save, uint32_t merchant, uint32_t town, uint32_t hull) {
	static constexpr uint32_t kShare = 0x3D4CCCCD;  // 0,05f
	float share;
	std::memcpy(&share, &kShare, sizeof(share));
	MerchantMemory& m = save.merchants[merchant].memory;
	const float gain = float(double(float(double(hull))) * double(m.f32(0x464)) * double(share) / 2800.0);
	float& r = m.f32(0x120 + 12 * town);
	r = float(double(r) + double(gain));
}

// 0x50AC40 (sobre el objeto de los barcos): la flota de la Hansa para el
// evento `event` de la ciudad `town`: un convoy libre ([0x6DD884]; 0x509AE0
// si no queda) con un barco nuevo (con `two`, dos) de los dos comerciantes
// de la IA de ciudades de la Hansa (bit 0x40 de [0x2C8] de su ciudad natal)
// con más [+0x478]: 0x507D60 en la ciudad, tipo 1 o 2 (0x52A8D0, con el bit 1
// de [0x2C8] de la ciudad), nivel 2, 5 u 8 cargas de casco más y de bodega
// menos (0x66E048, 0x66E060), reputación (0x4F9120), al principio de la lista
// del dueño, nombre (0x509A50), artillería (0x5181D0, los huecos que no puede
// llevar con 3/6 o 0), marineros (0x5184F0), capitán (0x5097C0, con su sueldo
// 0x4FE190) y bodega (0x5182B0). El convoy (0x501680 con 0x80) va al
// principio de la lista de convoyes, con el estado 9 (0xA para la caza de
// piratas: `hunt`, y 0x900 en [+0x16]) y [+0x2] el evento. Devuelve el convoy.
uint16_t fn_50ac40(SaveGame& save, uint32_t town, uint16_t event, bool two, bool hunt) {
	GlobalMemory& g = save.globals;
	const uint16_t convoy = g.u16(kFreeConvoy);
	ship_ai_detail::fn_509ae0(save);
	if (convoy >= convoy_count(save)) {
		return 0xFFFF;  // Unreachable: 0x509AE0 grows the table when no convoy is free, so the one
		// taken before exists (the game would go on with it as it is).
	}
	g.u16(kFreeConvoy) = save.convoys[convoy].u16(0x8);
	save.convoys[convoy].u16(0x8) = 0xFFFF;
	save.convoys[convoy].u16(0xA) = 0xFFFF;
	uint8_t chosen[2] = {0, 0};  // el que más y el segundo
	uint16_t best = 0;
	uint16_t second = 0;
	for (uint32_t m = 0; m < merchant_count(save); ++m) {
		const MerchantMemory& mm = save.merchants[m].memory;
		if (mm.u16(8) == 0 || !(save.towns[mm.u8(0x19)].memory.u8(0x2C8) & 0x40)) {
			continue;
		}
		const uint16_t v = mm.u16(0x478);
		if (second > v) {
			continue;
		}
		if (best > v) {
			second = v;
			chosen[1] = uint8_t(m);
		} else {
			second = best;
			chosen[1] = chosen[0];
			chosen[0] = uint8_t(m);
			best = v;
		}
	}
	const uint16_t status = hunt ? 0xA : 9;
	for (uint32_t k = two ? 2 : 1; k > 0; --k) {
		const uint8_t owner = chosen[k - 1];
		uint16_t index = 0;
		events_mayor_detail::fn_507d60(save, index, town, 3);
		const bool small = save.towns[town].memory.u8(0x2C8) & 1;
		const uint32_t extra = small ? 5 : 8;
		events_mayor_detail::fn_52a8d0(save, int(town), index, small ? 1 : 2);
		ShipMemory& s = save.ships[index].memory;
		s.u8(0xF) = 2;
		s.u32(0x14) += 2800 * extra;
		s.u32(0x18) = s.u32(0x14);
		s.u32(0x10) -= 2000 * extra;
		fn_4f9120(save, owner, town, s.u32(0x14));
		MerchantMemory& m = save.merchants[owner].memory;
		s.u8(0x0) = owner;
		s.u16(0x4) = m.u16(0xE);
		m.u16(0xE) = index;
		events_mayor_detail::fn_509a50(save, index);
		events_mayor_detail::fn_5181d0(save, index);
		const uint16_t sailors = uint16_t(ship_ai_detail::fn_5184f0(save, index));
		ShipMemory& t = save.ships[index].memory;
		t.u16(0x40) = sailors;
		t.u16(0x154) = sailors;
		const uint16_t captain = events_detail::fn_5097c0(save, false);
		save.ships[index].memory.u16(0x42) = captain;
		events_detail::fn_4fe190(save.captains[captain]);
		ShipMemory& u = save.ships[index].memory;
		for (size_t o = 0x13C; o < 0x154; o += 2) {
			if (u.u8(o) > 7) {
				if (u.u8(o + 1) > 7) {
					u.u8(o) = 3;
					u.u8(o + 1) = 6;
				} else {
					u.u8(o) = 0;
				}
			}
		}
		recalculate_cargo(save.ships[index]);
		GameMemory<0x3C>& c = save.convoys[convoy];
		u.u16(0x8) = convoy;
		u.u16(0x6) = c.u16(0xA);
		u.u16(0x134) = status;
		c.u16(0xA) = index;
	}
	ship_ai_detail::fn_501680(save, convoy, save.convoys[convoy].u16(0xA), 0x80);
	GameMemory<0x3C>& c = save.convoys[convoy];
	c.u16(0x8) = g.u16(kConvoyHead);
	g.u16(kConvoyHead) = convoy;
	c.u16(0x12) = status;
	c.u16(0x16) = 0;
	for (uint32_t s = c.u16(0xA); s < ship_count(save); s = save.ships[s].memory.u16(0x6)) {
		ShipMemory& ship = save.ships[s].memory;
		ship.u8(0x3C) |= 0x20;
		ship.u16(0x134) = status;
		ship.u16(0x138) = 0;
	}
	c.u16(0x2) = event;
	if (hunt) {
		c.u16(0x16) = 0x900;
	}
	return convoy;
}

void add_hunt_notice(SaveGame& save, uint16_t event, uint8_t kind, uint8_t merchant, uint16_t name) {
	std::vector<SaveGame::HuntNotice>& list = save.hunt_notices[event];
	if (save.events[event].u32(0x8) == 0) {
		list.clear();  // (The game's new node points to [+0x8]: no list before it.)
	}
	SaveGame::HuntNotice n;
	n.kind = kind;
	n.merchant = merchant;
	n.name = name;
	list.insert(list.begin(), n);
	save.events[event].u32(0x8) = 1;
}

void free_hunt_notices(SaveGame& save, uint16_t event) {
	save.hunt_notices.erase(event);
	save.events[event].u32(0x8) = 0;
}

namespace {

using HuntNotices = std::vector<SaveGame::HuntNotice>;

// 0x4F3CE0 (on the k-th notice of a hunt's list; a merchant, a flag, the text):
// the notices for the merchant's letter, appended to `out`. Each notice of
// his of the kind 3 (a ship his crew took) its line «The %s was seized by
// crew in your service.» (0x6B1360) on the way down the list; the notices of
// the kinds 0..2 (the fleet's ships lost), whoever's they are, on the way back
// up, so the list's last first: the last one, if a notice of his of those
// kinds came before it (the flag), the heading «The following ships of yours
// did not see the end of the hunt:» (0x6B135C), its name and the kind's
// sentence (0x6B136C + 4 · kind) as it is, «%s» and all; each one before it
// then its name and « (Sunk)» (0x6B1364) for the kind 0, but the sentence
// 0x6B136C as it is for the kinds 1 and 2; without the flag, the last one and
// each one before it the kind's sentence with the name (sprintf), until one
// is the last of a list as above. The ships' names by 0x512B20. Whether it
// wrote the heading. (The list form looks like a game bug, but it needs a
// notice of the merchant's own kinds 0..2, and the fleet's ships are AI
// merchants' (0x50AC40): the human merchants, who get the letters, get the
// sentences.)
bool fn_4f3ce0(const SaveGame& save, const GameTexts& t, const HuntNotices& list, size_t k, uint8_t merchant,
	bool flag, std::string& out) {
	const SaveGame::HuntNotice& n = list[k];
	bool mine = flag;
	if (n.merchant == merchant) {
		if (n.kind == kHuntTaken) {
			out += GameTexts::sprintf_raw(t.raw_at(0x6B1360), {ship_list_name(save, t, n.name)});
		} else if (n.kind < kHuntTaken) {
			mine = true;
		}
	}
	const bool after = k + 1 < list.size() && fn_4f3ce0(save, t, list, k + 1, merchant, mine, out);
	if (n.kind >= kHuntTaken) {
		return after;
	}
	const std::string name = ship_list_name(save, t, n.name);
	if (after) {
		out += name;
		out += t.raw_at(n.kind == kHuntSeized ? 0x6B136C : 0x6B1364 + 4u * n.kind);
		return true;
	}
	if (flag) {
		out += t.raw_at(0x6B135C);
		out += name;
		out += t.raw_at(0x6B136C + 4u * n.kind);
		return true;
	}
	out += GameTexts::sprintf_raw(t.raw_at(0x6B136C + 4u * n.kind), {name});
	return false;
}

// 0x4F3AF0 (on a hunt's list; a merchant, the pirate's first name and family
// name, the hunt's result): the letter 0x3D to the merchant, a mission
// letter's kind (as the interpreter's 0xF7): [+0x5] the Hanse's town; its
// record [+0x0] the buffer's bytes, [+0x4] 0, u16 [+0x8] 0xFFFF, [+0xA..+0xC]
// 0, [+0xD] the Hanse's town, [+0xE], [+0xF] 0xFF; its buffer the title by the
// result (0x6B1348 + 4 · (result & 3): «Flop», «Victory», «Success», «Abject
// Failure») with its NUL, the text and 4 NULs. The text: the result's (0x6B1338
// + 4 · (result & 3)) with the pirate's names (0x512A60, 0x512AE0), the
// notices (0x4F3CE0) and «The ships will shortly be returning home to the port
// of %s...» (0x6B1358) with the Hanse's town (the table 0x6DDA00 by its index:
// letter_town_name).
void fn_4f3af0(SaveGame& save, const HuntNotices& list, uint8_t merchant, uint8_t first, uint8_t family,
	uint8_t result) {
	const GameTexts* t = letter_texts();
	const uint8_t hanse = save.globals.u8(kHanseTown);
	const uint32_t k = result & 3u;
	std::string text, title;
	if (t) {
		text = GameTexts::sprintf_raw(t->raw_at(0x6B1338 + 4 * k),
			{game_first_name(save, first), game_family_name(save, family)});
		if (!list.empty()) {
			fn_4f3ce0(save, *t, list, 0, merchant, false, text);
		}
		text += GameTexts::sprintf_raw(t->raw_at(0x6B1358), {letter_town_name(save, hanse)});
		title = t->raw_at(0x6B1348 + 4 * k);
	}
	Letter letter(0x3D);
	letter.entry.u8(5) = hanse;
	letter.buffer.assign(title.begin(), title.end());
	letter.buffer.push_back(0);
	letter.buffer.insert(letter.buffer.end(), text.begin(), text.end());
	letter.buffer.insert(letter.buffer.end(), 4, 0);
	GameMemory<0x10> record{};
	record.u32(0) = uint32_t(letter.buffer.size());
	record.u16(8) = 0xFFFF;
	record.u8(0xD) = hanse;
	record.u8(0xE) = 0xFF;
	record.u8(0xF) = 0xFF;
	letter.record = record;
	send_letter(save, merchant, letter);
}

// El final de la caza de piratas (0x4E1B18): la lista de avisos de [+0x8] se libera.
int finish(SaveGame& save, uint16_t event) {
	diet_detail::free_hunt_notices(save, event);
	return 0;
}

// Fase 3 (y la 6 mientras dure): la ciudad con 0x400 y la operación 0x81 a
// cada jugador humano con un barco en ella en el estado 3.
int encargos(SaveGame& save, Event& e, uint32_t town) {
	GlobalMemory& g = save.globals;
	save.towns[town].memory.u32(0x2C8) |= 0x400;
	bool any = false;
	for (uint32_t m = 0; m < merchant_count(save); ++m) {
		if (save.merchants[m].memory.u16(8) != 0) {
			continue;
		}
		for (uint32_t s = save.merchants[m].memory.u16(0xE); s < ship_count(save); s = save.ships[s].memory.u16(0x4)) {
			const ShipMemory& ship = save.ships[s].memory;
			if (ship.u8(0x39) == town && ship.u16(0x134) == 3) {
				if (g.u32(kAldermanBusy) == 0) {
					uint8_t op[0x14];
					const uint32_t fields[5] = {0x81, m, 1, 0x100, town};
					std::memcpy(op, fields, sizeof(op));
					enqueue_operation(save, op, sizeof(op));
				}
				any = true;
				break;
			}
		}
	}
	e.u32(0) += 0x45;
	if (any) {
		e.u32(0) = (e.u32(0) | 0xFF) + 1;
	}
	if (e.u32(0) <= e.u32(0x10) || e.u8(0x15) == 6) {
		return 1;
	}
	e.u8(0x15) = 6;
	e.u32(0x10) = e.u32(0) + 0x1C00;
	return 1;
}

// La flota se deshace (o termina la protección): fase 5.
int disband(SaveGame& save, Event& e, uint32_t town) {
	GlobalMemory& g = save.globals;
	const uint16_t convoy = e.u16(0x16);
	if (convoy < convoy_count(save) && g.u16(kConvoyHead) != convoy) {
		bool found = false;
		for (uint32_t c = g.u16(kConvoyHead); c < convoy_count(save);) {
			c = save.convoys[c].u16(0x8);
			if (c == convoy) {
				found = true;
				break;
			}
		}
		if (!found) {
			e.u32(0) += 0x20;  // Checked with the oracle: scenario diet_branches (saves 3 and 5).
			return 1;
		}
	}
	if (town < town_count(save)) {
		// The letter 0x5C to each human merchant (0x4E128F..0x4E1371):
		// [+0x5] the Hanse town, u16 [+0xC] the event's town + 0x100 if it
		// was protected ([0x2C8] & 0x200), u16 [+0x8], [+0xA] the merchant's
		// names [+0x1C], [+0x1D], u16 [+0xE] his ships in the fleet's convoy.
		TownMemory& t = save.towns[town].memory;
		Letter letter(0x5C);
		letter.entry.u8(5) = g.u8(kHanseTown);
		letter.entry.u16(0xC) = uint16_t(town + ((t.u32(0x2C8) & 0x200) ? 0x100 : 0));
		t.u32(0x2C8) &= ~0x600u;
		for (uint32_t m = 0; m < merchant_count(save); ++m) {
			const MerchantMemory& mm = save.merchants[m].memory;
			if (mm.u16(8) != 0) {
				continue;
			}
			letter.entry.u16(8) = mm.u8(0x1C);
			letter.entry.u16(0xA) = mm.u8(0x1D);
			uint16_t ships = 0;
			if (convoy < convoy_count(save)) {
				for (uint32_t s = save.convoys[convoy].u16(0xA); s < ship_count(save); s = save.ships[s].memory.u16(0x6)) {
					if (save.ships[s].memory.u8(0) == m) {
						++ships;
					}
				}
			}
			letter.entry.u16(0xE) = ships;
			send_letter(save, int32_t(m), letter);
		}
	}
	if (convoy >= convoy_count(save)) {
		return 0;  // Checked with the oracle: scenario diet_branches (saves 3 and 5).
	}
	GameMemory<0x3C>& c = save.convoys[convoy];
	while (c.u16(0xA) < ship_count(save)) {
		const uint16_t s = c.u16(0xA);
		ShipMemory& ship = save.ships[s].memory;
		c.u16(0xA) = ship.u16(0x6);
		ship.u8(0x3C) |= 0x60;
		ship.u16(0x6) = 0xFFFF;
		ship.u16(0x8) = 0xFFFF;
		ship.u16(0x134) = 0;
		ship.u16(0x3C) |= 0x20;
		ship.u16(0x6) = g.u16(kDockedHead);
		g.u16(kDockedHead) = s;
		ship.u16(0x138) = 0x20;
	}
	return 0;
}

}  // namespace

// 0x4E09D4 (eventos 0x23, 0x24 y 0x34 de la cabeza): la misión de la Hansa
// por fases ([+0x15], tabla 0x4E1B54). Devuelve 1 si sigue (con el tick ya
// cambiado) o 0 si se quita.
int fn_4e09d4(SaveGame& save, uint16_t event, Sailing* sailing) {
	GlobalMemory& g = save.globals;
	Event& e = save.events[event];
	const uint16_t type = e.u16(6);
	const uint32_t town = e.u8(0x14);
	if (town >= town_count(save) || e.u8(0x15) > 10) {
		// A town out of the world or a phase past 10. Checked with the oracle: scenario
		// diet_branches (saves 3 and 5).
		return 0;
	}
	TownMemory& t = save.towns[town].memory;
	switch (e.u8(0x15)) {
	case 0: {
		// Los que participan: cada jugador humano con barcos para ello
		// (0x4F92B0) y, si pone más de uno y tiene [+0x478] > 10, otra vez con
		// la mitad de eso; de más a menos (burbuja), los 2 primeros (8 en el
		// 0x34 y el 0x24) en [+0x8..+0xF]. En el 0x23, cada uno una vez.
		e.u8(0x15) = 1;
		e.u32(0) = (e.u32(0) + 0x100) & 0xFFFFFF00;
		struct Entry {
			uint32_t merchant;
			uint32_t value;
		};
		std::vector<Entry> list(2 * merchant_count(save) + 2);
		uint32_t n = 0;
		for (uint32_t m = 0; m < merchant_count(save); ++m) {
			const MerchantMemory& mm = save.merchants[m].memory;
			if (mm.u16(8) != 0) {
				continue;
			}
			const uint32_t v = fn_4f92b0(save, m, town, type != 0x23);
			list[n] = {m, v};
			if (v <= 1) {
				n += v;
			} else if (mm.u16(0x478) > 10) {
				++n;
				list[n] = {m, uint32_t(mm.u16(0x478) >> 1)};
				++n;
			}
		}
		for (uint32_t i = 1; n > 1 && i < n; ++i) {
			bool swapped = false;
			for (uint32_t j = n - 1; j > i; --j) {
				if (list[j - 1].value < list[j].value) {
					std::swap(list[j - 1], list[j]);
					swapped = true;
				}
			}
			if (!swapped) {
				break;
			}
		}
		uint32_t take = type != 0x23 ? 8 : 2;
		if (take > n) {
			take = n;
		}
		for (uint32_t k = 0; k < 8; ++k) {
			e.u8(0x8 + k) = k < take ? uint8_t(list[k].merchant) : 0xFF;
		}
		// The letter 0x48 to each human merchant, whether he takes part or
		// not (0x4E0BC6..0x4E0CEE): [+0x5] the Hanse town, [+0xB] the
		// mission (0 for 0x23, 1 for 0x24, 2 for 0x34), [+0xC] the event's
		// town, and for the 0x34 [+0xE], [+0xF] the names of the captain of
		// the first ship with a captain of the pirates (as phase 2's: the
		// game ends both loops setting the pirate index, edx, to 0xFFFF);
		// then for each merchant [+0x8] his rank at home, [+0x9], [+0xA]
		// his names and [+0xD] how many times he is in the list.
		Letter letter(0x48);
		letter.entry.u8(5) = g.u8(kHanseTown);
		if (type == 0x34) {
			letter.entry.u8(0xB) = 2;
			bool found = false;
			for (uint32_t k = 0; k < save.pirates.size() && k < 5 && !found; ++k) {
				if (!save.pirates[k]) {
					continue;
				}
				for (uint32_t s = save.pirates[k]->u16(0xC); s < ship_count(save); s = save.ships[s].memory.u16(0x4)) {
					const uint16_t captain = save.ships[s].memory.u16(0x42);
					if (captain < g.u16(global::kCaptainCount)) {
						letter.entry.u8(0xE) = save.captains[captain].u8(2);
						letter.entry.u8(0xF) = save.captains[captain].u8(3);
						found = true;
						break;
					}
				}
			}
		} else {
			letter.entry.u8(0xB) = uint8_t(type - 0x23);
		}
		letter.entry.u8(0xC) = e.u8(0x14);
		for (uint32_t m = 0; m < merchant_count(save); ++m) {
			const MerchantMemory& mm = save.merchants[m].memory;
			if (mm.u16(8) != 0) {
				continue;
			}
			letter.entry.u8(8) = mm.u8(0x39C + mm.u8(0x19));
			letter.entry.u8(9) = mm.u8(0x1C);
			letter.entry.u8(0xA) = mm.u8(0x1D);
			uint8_t seen = 0;
			for (uint32_t k = 0; k < 8; ++k) {
				if (e.u8(0x8 + k) != m) {
					continue;
				}
				if (type == 0x23 && seen != 0) {
					e.u8(0x8 + k) = 0xFF;
				} else {
					++seen;
				}
			}
			letter.entry.u8(0xD) = seen;
			send_letter(save, int32_t(m), letter);
		}
		return 1;
	}
	case 1: {
		// La flota (con un barco en el 0x23 y en caza en el 0x34).
		if (type != 0x34) {
			t.u32(0x2C8) |= 0x400;
		}
		if (type == 0x24) {
			// The ticker line «The Hanseatic League is blockading X» (kind 0x2A,
			// the town [+0x14]) to the local player (0x4E0DB4); in a network game
			// once per human merchant, but always to merchant 0 (0x4E0D86: a game
			// bug; the line itself shows only with fix_game_bugs, compose_ticker).
			if (g.u32(0x6DFC18) != 0) {
				for (uint32_t m = 0; m < g.u16(global::kWorldMerchantCount) && m < save.merchants.size(); ++m) {
					if (save.merchants[m].memory.u16(8) == 0) {
						send_ticker(save, fix_game_bugs() ? m : 0, 0x2A, uint16_t(town));
					}
				}
			} else {
				send_ticker(save, g.u32(0x6DFC14), 0x2A, uint16_t(town));
			}
		}
		const uint16_t head = g.u16(kEventHead);
		const uint16_t convoy = fn_50ac40(save, town, head, type != 0x23, type == 0x34);
		Event& f = save.events[event];
		f.u16(0x16) = convoy;
		f.u32(0) += 0x900;
		f.u8(0x15) = 2;
		return 1;
	}
	case 2: {
		// Los que participan pierden reputación en las ciudades de la Hansa
		// (0x4F8CA0), una vez por cada vez que salen en la lista, y la lista
		// se vacía. Each gets the letter 0x4C (0x4E0E57..0x4E0F4C): [+0x5]
		// the Hanse town, [+0xB] the mission (as 0x48's), [+0xC] the event's
		// town, [+0x8] his rank at home, [+0x9], [+0xA] his names and [+0xD]
		// how many times he was in the list.
		Letter letter(0x4C);
		letter.entry.u8(5) = g.u8(kHanseTown);
		letter.entry.u8(0xB) = type == 0x34 ? 2 : uint8_t(type - 0x23);
		letter.entry.u8(0xC) = e.u8(0x14);
		for (uint32_t k = 0; k < 8; ++k) {
			const uint8_t m = e.u8(0x8 + k);
			if (m < merchant_count(save)) {
				fn_4f8ca0(save, m, 0xFF);
				const MerchantMemory& mm = save.merchants[m].memory;
				letter.entry.u8(8) = mm.u8(0x39C + mm.u8(0x19));
				letter.entry.u8(9) = mm.u8(0x1C);
				letter.entry.u8(0xA) = mm.u8(0x1D);
				letter.entry.u8(0xD) = 1;
				for (uint32_t j = k + 1; j < 8; ++j) {
					if (e.u8(0x8 + j) == m) {
						++letter.entry.u8(0xD);
						e.u8(0x8 + j) = 0xFF;
						fn_4f8ca0(save, m, 0xFF);
					}
				}
				send_letter(save, m, letter);
			}
			e.u8(0x8 + k) = 0xFF;
		}
		if (type == 0x34) {
			free_hunt_notices(save, event);  // [+0x8] 0: the list to come (none yet: it starts in phase 7).
			e.u32(0) += 1;
			e.u8(0xE) = 0;  // 0x672859: the result, «not found»
			e.u8(0x15) = 7;
			// El pirata buscado: los nombres ([+0x2], [+0x3]) del capitán del
			// primer barco con capitán de los piratas.
			for (uint32_t k = 0; k < save.pirates.size() && k < 5; ++k) {
				if (!save.pirates[k]) {
					continue;
				}
				for (uint32_t s = save.pirates[k]->u16(0xC); s < ship_count(save); s = save.ships[s].memory.u16(0x4)) {
					const uint16_t captain = save.ships[s].memory.u16(0x42);
					if (captain < g.u16(global::kCaptainCount)) {
						e.u8(0xC) = save.captains[captain].u8(2);
						e.u8(0xD) = save.captains[captain].u8(3);
						return 1;
					}
				}
			}
			return 1;
		}
		if (type == 0x24) {
			t.u32(0x2C8) |= 0x200;
			e.u32(0) += 0xA00;
			e.u8(0x15) = 4;
			return 1;
		}
		e.u32(0) += 0x40;
		e.u8(0x15) = 3;
		e.u32(0x10) = g.u32(kTick) + 0xA00;
		return 1;
	}
	case 3:
		return encargos(save, e, town);
	case 6:
		if (e.u32(0x10) < e.u32(0) || int16_t(t.u16(0x304)) < -10) {
			t.u8(0x6F0) = 0xFF;
			e.u32(0) += 5;
			e.u8(0x15) = 5;
			return 1;
		}
		return encargos(save, e, town);
	case 4:
		if (int16_t(t.u16(0x304)) >= -20) {
			e.u32(0) += 0x100;
			return 1;
		}
		t.u8(0x6F0) = 0xFF;
		e.u32(0) += 1;
		e.u8(0x15) = 5;
		return 1;
	case 5:
		return disband(save, e, town);
	case 7: {
		// La flota de caza se prepara: el convoy al estado 0xB y cuenta 0x40.
		const uint16_t convoy = e.u16(0x16);
		if (convoy >= convoy_count(save)) {
			return finish(save, event);  // Checked with the oracle: scenario diet_branches (saves 3 and 5).
		}
		GameMemory<0x3C>& c = save.convoys[convoy];
		uint32_t value = 1;
		if (c.u16(0x12) != 0xB) {
			value = c.u32(0xC) >> 1;
			c.u16(0x12) = 0xB;
			c.u32(0x18) |= 0x20;
			c.u16(0x16) = 0;
		} else if (++c.u16(0x16) >= 0x40) {
			e.u8(0x15) = 8;
		}
		for (uint32_t s = c.u16(0xA); s < ship_count(save);) {
			ShipMemory& ship = save.ships[s].memory;
			s = ship.u16(0x6);
			ship.u16(0x138) = c.u16(0x16);
			if (ship.u16(0x134) != c.u16(0x12)) {
				ship.u16(0x3C) |= 0xA0;
				ship.u16(0x134) = c.u16(0x12);
				ship.u32(0x30) = value;
			}
		}
		e.u32(0) += 1;
		return 1;
	}
	case 8: {
		// Sale al mar (estado 0x16) desde la posición de la ciudad.
		const uint16_t convoy = e.u16(0x16);
		if (convoy >= convoy_count(save)) {
			return finish(save, event);  // Checked with the oracle: scenario diet_branches (saves 3 and 5).
		}
		GameMemory<0x3C>& c = save.convoys[convoy];
		const uint16_t lead_index = c.u16(0x10);
		if (lead_index >= ship_count(save)) {
			return finish(save, event);  // Unreachable: the game doesn't look, and a convoy of the fleet always has
			// its leading ship.
		}
		c.u8(0x1E) = c.u8(0x1C);
		c.u32(0x18) |= 0x20;
		c.u16(0x1C) = 0xFF;
		c.u16(0x16) = 0;
		c.u16(0x12) = 0x16;
		ShipMemory& lead = save.ships[lead_index].memory;
		const uint16_t first = c.u16(0xA);
		lead.u16(0x1E) = uint16_t(table(save, town, 0x20));
		lead.u16(0x1C) = 0;
		lead.u16(0x22) = uint16_t(table(save, town, 0x24));
		lead.u16(0x20) = 0;
		lead.u32(0x30) = 0;
		if (sailing) {
			sailing->insert_x(lead_index, g.u16(kSeaHead));
		}
		for (uint32_t s = first; s < ship_count(save);) {
			ShipMemory& ship = save.ships[s].memory;
			ship.u16(0x138) = 0;
			ship.u32(0x1C) = lead.u32(0x1C);
			ship.u32(0x20) = lead.u32(0x20);
			s = ship.u16(0x6);
			ship.u16(0x3C) |= 0x20A8;
			ship.u16(0x134) = c.u16(0x12);
			ship.u32(0x30) = lead.u32(0x30);
			ship.u8(0x39) = c.u8(0x1C);
		}
		e.u8(0x15) = 9;
		lead.u16(0x3A) = 0xFFFF;
		e.u32(0x10) = uint32_t(e.u16(0x16)) * 75 + e.u32(0) + 0x1F00;
		[[fallthrough]];
	}
	case 9: {
		// La caza: hasta el plazo [+0x10], tras el primer barco pirata que guíe
		// un convoy; sin él, hacia la ciudad.
		if (e.u32(0x10) < g.u32(kTick)) {
			e.u8(0x15) = 10;
			e.u32(0) += 1;
			return 1;
		}
		const uint16_t convoy = e.u16(0x16);
		if (convoy >= convoy_count(save)) {
			return finish(save, event);  // Checked with the oracle: scenario diet_branches (saves 3 and 5).
		}
		GameMemory<0x3C>& c = save.convoys[convoy];
		const uint16_t lead_index = c.u16(0x10);
		if (lead_index >= ship_count(save)) {
			return finish(save, event);  // Unreachable: the game doesn't look, and a convoy of the fleet always has
			// its leading ship.
		}
		if (c.u16(0x12) == 0x14) {
			// The convoy fighting (state 0x14). Checked with the oracle: scenario diet_branches
			// (saves 3 and 5).
			e.u32(0) += 0x40;
			return 1;
		}
		if (!listed(save, convoy)) {
			e.u8(0x15) = 10;  // Out of the list. Checked with the oracle: scenario diet_branches (saves 3 and 5).
			e.u32(0) += 1;
			return 1;
		}
		SavedShip& lead = save.ships[lead_index];
		const uint32_t ships = ship_count(save);
		uint16_t target = lead.memory.u16(0x3A);
		if (target >= ships) {
			if (g.u32(kTick) < e.u32(0x10)) {
				for (uint32_t k = 0; k < save.pirates.size() && k < 5 && target >= ships; ++k) {
					if (!save.pirates[k]) {
						continue;
					}
					for (uint32_t s = save.pirates[k]->u16(0xC); s < ships; s = save.ships[s].memory.u16(0x4)) {
						const ShipMemory& p = save.ships[s].memory;
						const uint16_t pc = p.u16(0x8);
						if (pc < convoy_count(save) && (p.u16(0xA) < ships || p.u16(0xC) < ships) &&
							save.convoys[pc].u16(0x10) == s) {
							target = uint16_t(s);
							lead.memory.u16(0x3A) = target;
							break;
						}
					}
				}
			}
		} else {
			const ShipMemory& p = save.ships[target].memory;
			if (p.u16(0x134) != 0x12 || p.u16(0x8) >= convoy_count(save)) {
				target = 0xFFFF;
				lead.memory.u16(0x3A) = 0xFFFF;
			}
		}
		if (lead.memory.u16(0x3A) >= ships) {
			if (sailing) {
				sailing->set_destination(lead, int(town), 0, 0);
			}
			e.u32(0) += 0x20;
			return 1;
		}
		const SavedShip& prey = save.ships[target];
		int32_t x = int16_t(prey.memory.u16(0x1E));
		int32_t y = int16_t(prey.memory.u16(0x22));
		if (prey.memory.u32(0x24) != 0 && prey.route) {
			const uint32_t k = g.u32(kTick) & 7;
			if (k < prey.route->size()) {
				x = (*prey.route)[k].x;
				y = (*prey.route)[k].y;
			}
		}
		if (sailing) {
			sailing->set_destination(lead, 0xFF, x, y);
		}
		const int32_t dx = int32_t(int16_t(prey.memory.u16(0x1E))) - int32_t(int16_t(lead.memory.u16(0x1E)));
		if (dx > 0) {
			e.u32(0) += dx > 0x6B ? 0x6B : uint32_t(dx + 1);
		} else if (dx < -0x46) {
			e.u32(0) += 0x46;
		} else {
			e.u32(0) += uint32_t(1 - dx);
		}
		return 1;
	}
	case 10: {
		// La vuelta: rumbo a la ciudad; al llegar (a menos de 10), la carta 0x3D
		// a cada jugador humano con lo visto, y el convoy y sus barcos pasan del
		// estado 0x16 al 0xF y se acaba.
		const uint16_t convoy = e.u16(0x16);
		if (convoy >= convoy_count(save)) {
			return finish(save, event);  // Checked with the oracle: scenario diet_branches (saves 3 and 5).
		}
		if (!listed(save, convoy)) {
			e.u32(0) += 0x40;  // Out of the list. Checked with the oracle: scenario diet_branches (saves 3 and 5).
			return 1;
		}
		GameMemory<0x3C>& c = save.convoys[convoy];
		if (c.u16(0x12) != 0x16 || c.u16(0x10) >= ship_count(save)) {
			return finish(save, event);  // Checked with the oracle: scenario diet_branches (saves 3 and 5).
		}
		SavedShip& lead = save.ships[c.u16(0x10)];
		lead.memory.u16(0x3A) = 0xFFFF;
		if (sailing) {
			sailing->set_destination(lead, int(town), 0, 0);
		}
		int32_t dx = int32_t(table(save, town, 0x20)) - int32_t(int16_t(lead.memory.u16(0x1E)));
		if (dx < 0) {
			dx = -dx;
		}
		const int32_t tx = int32_t(table(save, town, 0x24));
		const int32_t ly = int16_t(lead.memory.u16(0x22));
		const int32_t distance = (dx + (tx > ly ? tx - ly : ly - tx)) >> 1;
		if (distance > 0x46) {
			e.u32(0) += 0x46;
			return 1;
		}
		if (distance > 10) {
			e.u32(0) += uint32_t(distance - 7);
			return 1;
		}
		// 0x4E19F1..0x4E1AB3: without a list, one of a notice of no merchant and no kind (0x4F3980, 0x4F3AD0 with
		// 0xFF, [0x672858] 0xFF, 0, 0); the letter (0x4F3AF0) to each human merchant (from 0 to [0x6DE4AA], the
		// u8 counter, [+0x8] 0) with the pirate's names [+0xC], [+0xD] and the result [+0xE]; the list freed.
		if (e.u32(0x8) == 0) {
			add_hunt_notice(save, event, 0xFF, 0xFF, 0);
		}
		const HuntNotices list = save.hunt_notices[event];
		for (uint32_t m = 0; m < merchant_count(save) && m < 0x100; ++m) {
			if (save.merchants[m].memory.u16(8) == 0) {
				fn_4f3af0(save, list, uint8_t(m), e.u8(0xC), e.u8(0xD), e.u8(0xE));
			}
		}
		free_hunt_notices(save, event);
		c.u32(0x18) |= 0x20;
		c.u16(0x12) = 0xF;
		for (uint32_t s = c.u16(0xA); s < ship_count(save); s = save.ships[s].memory.u16(0x6)) {
			ShipMemory& ship = save.ships[s].memory;
			if (ship.u16(0x134) == 0x16) {
				ship.u16(0x3C) |= 0x20;
				ship.u16(0x134) = 0xF;
			}
		}
		return 0;
	}
	default:
		return 0;
	}
}

}  // namespace diet_detail

}  // namespace p3::data::tick
