#include "p3data/town_hall_window.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/town_tick/votes.hpp"

namespace p3::data::tick {
namespace construction_detail {  // construction.cpp
uint32_t fn_4d9f60(const SaveGame& save, uint32_t town);
void fn_4d9fc0(const SaveGame& save, uint32_t& tick, uint32_t town);
}  // namespace construction_detail
namespace events_detail {  // events.cpp
bool fn_633ae0(const SaveGame& save, uint32_t town);
}  // namespace events_detail
namespace events_mayor_detail {  // events_mayor.cpp
uint32_t fn_4f9360(SaveGame& save, int merchant);
void fn_528cf0(SaveGame& save, int town, uint8_t out[4]);
}  // namespace events_mayor_detail
}  // namespace p3::data::tick

namespace p3::data {

namespace {

constexpr uint8_t kArsenalCost[kArsenalTypes] = {0x21, 0x1A, 0x28, 0x2E};  // 0x672DB0

OperationBytes militia_operation(uint32_t code, uint32_t type, uint32_t town) {
	OperationBytes op{};
	std::memcpy(op.data(), &code, 4);
	std::memcpy(op.data() + 4, &type, 4);
	std::memcpy(op.data() + 8, &town, 4);
	return op;
}

bool council_limits(const SaveGame& save, uint32_t town) {
	return (save.towns[town].memory.u32(0x2C8) & 0xE0) != 0;
}

}  // namespace

bool arsenal_mayor(const SaveGame& save, uint32_t town) {
	return save.towns[town].memory.u8(0x6F1) == save.globals.u32(global::kPlayer);
}

int32_t arsenal_cost(const SaveGame& save, uint32_t town, uint32_t type) {
	const TownMemory& t = save.towns[town].memory;
	const int32_t units = int32_t(t.u8(0x998 + type)) + t.u8(0x99C + type);
	return units * kArsenalCost[type] * 7;
}

ArsenalMessage arsenal_open_message(const SaveGame& save, uint32_t town) {
	return council_limits(save, town) ? ArsenalMessage::Authorised : ArsenalMessage::None;
}

bool arsenal_form(const SaveGame& save, uint32_t town, uint32_t type, ArsenalMessage& message, OperationBytes& op) {
	const TownMemory& t = save.towns[town].memory;
	message = ArsenalMessage::None;
	const int office = tick::facilities_detail::fn_5308a0(save, save.globals.u32(global::kPlayer), town);
	const int32_t weapons = office >= 0 ? save.offices[size_t(office)].memory.s32(0x54 + 4 * type) : t.s32(0x54 + 4 * type);
	if (weapons < 50) {
		message = ArsenalMessage::NoWeapons;
	}
	if (t.s32(0x2E4) < 20) {
		message = ArsenalMessage::NoBeggars;
	} else if (council_limits(save, town)) {
		int32_t units = 0;
		for (uint32_t k = 0; k < kArsenalTypes; ++k) {
			units += t.u8(0x998 + k) + t.u8(0x99C + k);
		}
		if (units >= int32_t(t.u16(0x9A0))) {
			message = ArsenalMessage::NoCouncil;
		}
	}
	if (message != ArsenalMessage::None) {
		return false;
	}
	op = militia_operation(0x41, type, town);
	if (council_limits(save, town)) {
		message = ArsenalMessage::Authorised;
	}
	return true;
}

OperationBytes arsenal_dismiss(const SaveGame& save, uint32_t town, uint32_t type, ArsenalMessage& message) {
	message = council_limits(save, town) ? ArsenalMessage::Authorised : ArsenalMessage::None;
	return militia_operation(0x40, type, town);
}

namespace {

constexpr uint32_t kWorldOrders = 0x6DE530;

}  // namespace

std::optional<TownOrder> town_order(const SaveGame& save, uint32_t k) {
	const GlobalMemory& g = save.globals;
	const uint32_t at = kWorldOrders + 12 * k;
	if (k >= 7 || g.u8(at) >= g.u32(global::kWorldTownCount)) {
		return std::nullopt;
	}
	TownOrder o;
	o.town = g.u8(at);
	o.ware = g.u8(at + 1);
	o.bonus = g.u8(at + 2) != 0;
	o.count = g.u8(at + 3);
	o.deadline = g.u32(at + 4);
	o.price = g.u32(at + 8);
	return o;
}

std::optional<PrinceDecree> prince_decree(const SaveGame& save, uint32_t town) {
	if (town >= save.sieges.size() || save.sieges[town].u8(0x1010) == 0xFF) {
		return std::nullopt;
	}
	const SiegeMemory& s = save.sieges[town];
	PrinceDecree d;
	for (uint32_t k = 0; k < 4; ++k) {
		d.wares[k] = s.u8(0x1010 + k);
	}
	const uint32_t player = save.globals.u32(global::kPlayer);
	for (const GameMemory<8>& node : save.siege_extras[town].list_101c) {
		if (node.u8(0x3) == uint8_t(player)) {
			return d;  // His deliveries promised (0x636590): no contract offered.
		}
	}
	uint8_t seen = 0;
	for (uint32_t k = 0; k < 4; ++k) {
		if (s.u8(0x1014 + k) != 3) {
			continue;
		}
		++seen;
		if ((int32_t(s.u8(0x4)) + save.globals.u8(0x6DE4A1)) % seen == 0) {
			d.contract = s.u8(0x1010 + k);
		}
	}
	return d;
}

bool accept_order(const SaveGame& save, uint32_t town, uint32_t k, OperationBytes& op) {
	const std::optional<TownOrder> order = town_order(save, k);
	const uint32_t player = save.globals.u32(global::kPlayer);
	if (!order || int16_t(save.merchants[player].memory.u16(0x22)) < 20) {
		return false;
	}
	const uint32_t client = (town + order->ware + order->count) & 3;
	const TownMemory& t = save.towns[order->town].memory;
	op = OperationBytes{};
	op[0] = 0x3D;
	op[4] = order->town;
	op[5] = order->ware;
	op[6] = t.u8(0x6E8 + client);
	op[7] = t.u8(0x6EC + client);
	const uint32_t deadline = order->deadline | 0xFF;
	std::memcpy(op.data() + 8, &deadline, 4);
	const uint16_t count = order->count;
	const uint16_t price = uint16_t(order->price);
	std::memcpy(op.data() + 0xC, &count, 2);
	std::memcpy(op.data() + 0xE, &count, 2);
	std::memcpy(op.data() + 0x10, &price, 2);
	op[0x12] = uint8_t(player);
	op[0x13] = order->bonus ? 1 : 0;
	return true;
}

namespace {

constexpr int32_t kLeastFirepower = 9;      // [0x672CC8]
constexpr uint32_t kEvents = 0x6DD73C;      // The event queue object.
constexpr uint32_t kEventHead = kEvents + 0x8;
constexpr uint32_t kEventCount = kEvents + 0xC;
constexpr uint32_t kAlderman = 0x6DE52D;
constexpr uint32_t kAldermanTown = 0x6DE52E;

// 0x4D9D10: the first event 0x20 among the queue's slots, in their order (not its chain); -1 none.
int election_slot(const SaveGame& save) {
	const uint32_t count = std::min<uint32_t>(save.globals.u16(kEventCount), uint32_t(save.events.size()));
	for (uint32_t i = 0; i < count; ++i) {
		if (save.events[i].u16(6) == 0x20) {
			return int(i);
		}
	}
	return -1;
}

// 0x4D9CD0: the first event 0x21 of the queue's chain (from [+0x8], next [+0x4]); -1 none.
int diet_event(const SaveGame& save) {
	const uint32_t count = std::min<uint32_t>(save.globals.u16(kEventCount), uint32_t(save.events.size()));
	for (uint32_t i = save.globals.u16(kEventHead), walked = 0; i < count && walked < count; i = save.events[i].u16(4), ++walked) {
		if (save.events[i].u16(6) == 0x21) {
			return int(i);
		}
	}
	return -1;
}

}  // namespace

TownHallMenu town_hall_menu(const SaveGame& save, uint32_t town) {
	TownHallMenu menu;
	const GlobalMemory& g = save.globals;
	if (town >= g.u32(global::kWorldTownCount) || town >= save.towns.size()) {
		return menu;
	}
	const uint32_t player = g.u32(global::kPlayer);
	const TownMemory& t = save.towns[town].memory;
	menu.on[0] = town < save.sieges.size() && save.sieges[town].u8(0x1010) != 0xFF;
	if (!menu.on[0] && fix_game_bugs()) {
		// Game bug fixed: the orders' loop 0x5A97F2 turns the button on for an order in the world and off again in the
		// same breath (it doesn't leave the loop), so the decree alone turns it on. Fixed: an order turns it on.
		for (uint32_t k = 0; k < 7 && !menu.on[0]; ++k) {
			menu.on[0] = town_order(save, k).has_value();
		}
	}
	menu.on[1] = menu.on[2] = menu.on[3] = menu.on[5] = true;
	if ((t.u32(0x2C8) & 0xE0) && player < save.merchants.size()) {
		const uint32_t now = g.u32(global::kWorldTick);
		if (save.merchants[player].memory.u8(0x39C + town) >= 4 &&
			tick::town_hall_detail::fn_4da060(save, uint16_t(town), uint16_t(player)).first < g.u16(kEventCount)) {
			menu.on[4] = true;
		} else if (town == g.u8(kAldermanTown)) {
			for (const int e : {election_slot(save), diet_event(save)}) {
				if (e >= 0 && ((save.events[size_t(e)].u32(0) ^ now) & 0xFFFFFF00) == 0 && tick::fn_4d9d40(save)) {
					menu.on[4] = true;
				}
			}
		}
	}
	menu.on[6] = t.u8(0x6F1) == player;
	menu.office = town == g.u8(kAldermanTown);
	if (menu.office) {
		menu.on[7] = (player < save.merchants.size() && save.merchants[player].memory.u8(0x37) != 0) || g.u8(kAlderman) == player;
	}
	return menu;
}

int32_t average_firepower(const SaveGame& save) {
	const uint32_t ships = save.globals.u16(global::kShipCount);
	int32_t sum = 0;
	int32_t count = 0;
	for (int m = int(save.globals.u16(global::kWorldMerchantCount)) - 1; m >= 0; --m) {
		if (size_t(m) >= save.merchants.size()) {
			continue;
		}
		for (uint32_t s = save.merchants[size_t(m)].memory.u16(0xE); s < ships && s < save.ships.size();
			 s = save.ships[s].memory.u16(0x4)) {
			const int32_t firepower = save.ships[s].memory.s32(0x120);
			if (firepower > 0) {
				sum += firepower;
				++count;
			}
		}
	}
	return count > 0 ? sum / count : 0;
}

GuardShipTerms guard_ship_terms(const SaveGame& save, uint32_t town) {
	uint32_t average = uint32_t(average_firepower(save));
	if (average < uint32_t(kLeastFirepower)) {
		average = kLeastFirepower;
	}
	const uint32_t v = average - kLeastFirepower;
	GuardShipTerms terms;
	// fild qword; fmul 5.6 (a double); sqrt; ftol; as a u16, + 2.
	terms.strength = uint16_t(int32_t(std::sqrt(double(v) * 5.6))) + 2u;
	if (terms.strength < 20) {
		terms.strength = 20;
	}
	const int32_t steps = int32_t(terms.strength) - 2;
	// fild qword; fstp float; fld; fdiv 5.6f; ftol.
	terms.firepower = uint32_t(int32_t(double(float(steps * steps)) / double(5.6f))) + kLeastFirepower + 1;
	const int32_t wealth = save.towns[town].memory.s32(0x2D4);
	terms.pay = uint16_t(wealth / 100 + int32_t(terms.strength * 5 + 50) * 2);
	return terms;
}

std::vector<uint32_t> guard_ship_strength_pictures(uint32_t firepower) {
	FirepowerBalls b = firepower_balls(int32_t(firepower));
	std::vector<uint32_t> pictures;
	for (;;) {
		if (b.big > 0) {
			if (b.small > 0) {
				pictures.push_back(31113);
				--b.small;
			} else if (b.half) {
				pictures.push_back(31112);
				b.half = false;
			} else {
				pictures.push_back(31111);
			}
			--b.big;
		} else if (b.small > 0) {
			pictures.push_back(31110);
			--b.small;
		} else if (b.half) {
			pictures.push_back(31109);
			b.half = false;
		} else {
			return pictures;
		}
	}
}

GuardShipPage guard_ship_page(const SaveGame& save, uint32_t town) {
	GuardShipPage page;
	page.terms = guard_ship_terms(save, town);
	page.least = page.terms.firepower;
	const uint32_t ships = save.globals.u16(global::kShipCount);
	const uint32_t player = save.globals.u32(global::kPlayer);
	page.guard = save.towns[town].memory.u16(0x994);
	if (page.guard >= ships || page.guard >= save.ships.size()) {
		page.kind = GuardShipCase::Wanted;
	} else {
		const ShipMemory& g = save.ships[page.guard].memory;
		const uint32_t firepower = g.u16(0x120);
		if (firepower < page.terms.firepower) {
			page.kind = GuardShipCase::Unsuitable;
		} else {
			page.kind = GuardShipCase::Kept;
			page.own = g.u8(0x0) == player;
			page.list = page.own;
			page.least = firepower + 1;
		}
	}
	if (!page.list) {
		return page;
	}
	page.ships = window_ships(save, int(player), int(town), ShipListMode::Guard, int32_t(page.least));
	return page;
}

bool council_town(const SaveGame& save, uint32_t town) {
	return town < save.towns.size() && (save.towns[town].memory.u32(0x2C8) & 0xE0) != 0;
}

namespace {

// A merchant or one of the town's citizens as the page names them (0x5E2A90.., 0x528DBF..).
TownPerson town_person(const SaveGame& save, uint32_t town, uint8_t who) {
	TownPerson p;
	p.who = who;
	const TownMemory& t = save.towns[town].memory;
	if (who < save.globals.u16(global::kWorldMerchantCount) && who < save.merchants.size()) {
		const MerchantMemory& m = save.merchants[who].memory;
		p.merchant = true;
		p.female = m.u8(0x1A) == 0;
		p.title = tick::merchants_detail::fn_4f9430(save, int(who));
		p.rank = m.u8(0x39C + m.u8(0x19));
		p.first = m.u8(0x1C);
		p.family = m.u8(0x1D);
		return p;
	}
	const uint32_t j = uint32_t(uint8_t(0xFF - who)) & 3;
	p.title = t.u8(0x6E0 + j);
	p.rank = t.u8(0x6E4 + j);
	p.first = t.u8(0x6D4 + j);
	p.family = t.u8(0x6D8 + j);
	return p;
}

}  // namespace

TownPerson town_mayor(const SaveGame& save, uint32_t town) {
	return town_person(save, town, save.towns[town].memory.u8(0x6F1));
}

std::array<TownPerson, 4> mayor_candidates(SaveGame& save, uint32_t town) {
	uint8_t who[4];
	tick::events_mayor_detail::fn_528cf0(save, int(town), who);
	std::array<TownPerson, 4> out;
	for (uint32_t k = 0; k < 4; ++k) {
		out[k] = town_person(save, town, who[k]);
	}
	return out;
}

TownDate next_mayor_election(const SaveGame& save, uint32_t town) {
	const GlobalMemory& g = save.globals;
	const uint8_t index = save.towns[town].memory.u8(0x2C0);
	const uint32_t count = std::min<uint32_t>(g.u16(kEventCount), uint32_t(save.events.size()));
	for (uint32_t i = g.u16(kEventHead), walked = 0; i < count && walked < count; i = save.events[i].u16(4), ++walked) {
		const auto& e = save.events[i];
		if (e.u16(6) == 2 && e.u8(0xE) == index) {
			return {e.u8(0xF), e.u8(0x10), uint16_t(g.u16(global::kWorldYear) + (g.u16(global::kWorldDayOfYear) > e.u16(8) ? 1 : 0))};
		}
	}
	return {31, 12, g.u16(global::kWorldYear)};
}

std::optional<CouncilVote> council_vote(const SaveGame& save, uint32_t town) {
	const GlobalMemory& g = save.globals;
	const uint32_t limit = g.u32(global::kWorldTick) + 0x1E00;
	const uint32_t count = std::min<uint32_t>(g.u16(kEventCount), uint32_t(save.events.size()));
	for (uint32_t i = g.u16(kEventHead), walked = 0; i < count && walked < count; i = save.events[i].u16(4), ++walked) {
		const auto& e = save.events[i];
		if (e.u16(6) == 0x2E && e.u8(0xC) == town) {
			return CouncilVote{uint16_t(i), e.u32(0), e.u8(0xD), e.s32(0x8), e.u8(0x14)};
		}
		if (e.u32(0) >= limit) {
			break;
		}
	}
	return std::nullopt;
}

bool standing_for_mayor(SaveGame& save, uint32_t merchant) {
	return tick::events_mayor_detail::fn_4f9360(save, int(merchant)) != 0;
}

OperationBytes candidacy_operation(uint32_t merchant, bool standing) {
	OperationBytes op{};
	const uint32_t words[3] = {0x45, merchant, standing ? 1u : 0u};
	std::memcpy(op.data(), words, sizeof words);
	return op;
}

std::array<bool, 4> proposals_open(const SaveGame& save, uint32_t town) {
	if (!council_town(save, town)) {
		return {false, false, true, true};
	}
	const TownMemory& t = save.towns[town].memory;
	const uint32_t mask = tick::construction_detail::fn_4d9f60(save, town);
	uint32_t units = 0;
	for (uint32_t k = 0; k < 4; ++k) {
		units += uint32_t(t.u8(0x998 + k)) + t.u8(0x99C + k);
	}
	return {!(mask & 2) && t.u16(0x766) >= t.u16(0x776) && tick::fn_4fcfe0(save, int(town), 2),
		!(mask & 4) && t.u16(0x9A0) <= uint16_t(units), !(mask & 8), !(mask & 1)};
}

ProposalAmount proposal_amount(const SaveGame& save, uint32_t town, uint32_t kind) {
	if (kind == 2) {
		return {save.towns[town].memory.u8(0x6F8), 0, 20, 1};
	}
	if (kind == 3) {
		return {0, 0, 999999, 5000};
	}
	return {};
}

int32_t proposal_step(const ProposalAmount& a, int32_t value, int step) {
	value += step * a.step;
	if (value >= a.high) {
		value = a.high;
	}
	if (value <= a.low) {
		value = a.low;
	}
	return value;
}

bool proposal_ok(const SaveGame& save, uint32_t town, uint32_t kind, int32_t value) {
	if (kind == 2) {
		return value != int32_t(save.towns[town].memory.u8(0x6F8));
	}
	return kind == 3 ? value > 0 : true;
}

OperationBytes council_proposal(const SaveGame& save, uint32_t town, uint32_t kind, int32_t value) {
	static const uint8_t kVoteKind[4] = {1, 2, 3, 0};
	uint32_t tick = save.globals.u32(global::kWorldTick) + 0xE00;
	tick::construction_detail::fn_4d9fc0(save, tick, town);
	OperationBytes op{};
	const uint32_t words[3] = {0x48, tick, kind == 3 ? uint32_t(value) : 0u};
	std::memcpy(op.data(), words, sizeof words);
	op[0xC] = uint8_t(town);
	op[0xD] = kVoteKind[kind & 3];
	op[0xE] = kind == 2 ? uint8_t(value) : 0;
	op[0xF] = 0;
	op[0x10] = uint8_t(save.globals.u32(global::kPlayer));
	return op;
}

TownPerson alderman_person(const SaveGame& save) {
	const GlobalMemory& g = save.globals;
	const uint8_t who = g.u8(kAlderman);
	const uint8_t town = g.u8(kAldermanTown);
	if (town >= save.towns.size()) {
		return {};
	}
	return town_person(save, town, who);  // (0x529DC0's citizen's title is town_person's [+0x6E0 + j].)
}

std::array<AldermanCandidate, 4> alderman_candidates(const SaveGame& save) {
	const GlobalMemory& g = save.globals;
	uint8_t towns[4];
	uint8_t merchants[4];
	tick::events_mayor_detail::fn_529700(save, towns, merchants);
	std::array<AldermanCandidate, 4> out;
	for (uint32_t k = 0; k < 4; ++k) {
		out[k].town = towns[k];
		if (towns[k] >= save.towns.size()) {
			continue;
		}
		if (merchants[k] < g.u16(global::kWorldMerchantCount)) {
			out[k].person = town_person(save, towns[k], merchants[k]);
			continue;
		}
		// 0x529977: a citizen of the town, the one its mayor's number gives (the Alderman's when the mayor is a
		// merchant), the rank 6 (7 in the Alderman's town).
		const TownMemory& t = save.towns[towns[k]].memory;
		uint8_t mayor = t.u8(0x6F1);
		if (mayor < g.u16(global::kWorldMerchantCount)) {
			mayor = g.u8(kAlderman);
		}
		TownPerson& p = out[k].person;
		const uint32_t j = uint32_t(uint8_t(0xFF - mayor)) & 3;
		p.who = merchants[k];
		p.title = t.u8(0x6E0 + j);
		p.rank = uint8_t(6 + (g.u8(kAldermanTown) == towns[k] ? 1 : 0));
		p.first = t.u8(0x6D4 + j);
		p.family = t.u8(0x6D8 + j);
	}
	return out;
}

std::optional<uint32_t> election_slot_tick(const SaveGame& save) {
	const int e = election_slot(save);
	if (e < 0) {
		return std::nullopt;
	}
	return save.events[size_t(e)].u32(0);
}

std::optional<HanseDay> hanse_day(const SaveGame& save) {
	const int e = diet_event(save);
	if (e < 0) {
		return std::nullopt;
	}
	const auto& d = save.events[size_t(e)];
	return HanseDay{d.u32(0), d.u8(0x9), d.u8(0xE), d.u8(0xA), d.u8(0xC)};
}

ChamberState chamber_state(const SaveGame& save, uint32_t town) {
	ChamberState s;
	const GlobalMemory& g = save.globals;
	if (!council_town(save, town)) {
		return s;
	}
	const uint32_t player = g.u32(global::kPlayer);
	if (player >= save.merchants.size()) {
		return s;
	}
	const MerchantMemory& m = save.merchants[player].memory;
	const uint32_t now = g.u32(global::kWorldTick);
	const bool at_home = m.u8(0x39C + m.u8(0x19)) > 3;
	auto today = [&](int e) { return e >= 0 && ((save.events[size_t(e)].u32(0) ^ now) & 0xFFFFFF00) == 0; };
	if (town == g.u8(kAldermanTown) && at_home) {
		const int election = election_slot(save);
		if (today(election)) {
			s.vote = ChamberVote::Election;
			s.event = uint16_t(election);
			s.may_vote = tick::fn_4d9d40(save);
			for (uint32_t k = 0; k < 4; ++k) {
				s.votes[k] = save.events[size_t(election)].u8(0xC + k);
			}
			return s;
		}
		const int diet = diet_event(save);
		if (today(diet)) {
			s.vote = ChamberVote::Diet;
			s.event = uint16_t(diet);
			s.may_vote = tick::fn_4d9d40(save);
			for (uint32_t k = 0; k < 3; ++k) {
				s.votes[k] = save.events[size_t(diet)].u8(0x12 + k);
			}
			return s;
		}
	}
	const auto [index, may] = tick::town_hall_detail::fn_4da060(save, uint16_t(town), uint16_t(player));
	if (index < g.u16(kEventCount) && index < save.events.size() && m.u8(0x39C + town) >= 4) {
		s.vote = ChamberVote::Council;
		s.event = index;
		s.may_vote = may;
		for (uint32_t k = 0; k < 3; ++k) {
			s.votes[k] = save.events[index].u8(0x10 + k);
		}
	}
	return s;
}

std::vector<uint32_t> vote_tally(uint32_t votes, uint32_t cap) {
	std::vector<uint32_t> marks;
	if (votes > cap) {
		marks.assign(cap / 5, 32010);
		return marks;
	}
	marks.assign(votes / 5, 32010);
	if (votes % 5 != 0) {
		marks.push_back(32006 + votes % 5 - 1);
	}
	return marks;
}

std::optional<OperationBytes> chamber_vote_operation(const SaveGame& save, uint32_t town, ChamberVote vote, uint32_t choice) {
	const uint32_t player = save.globals.u32(global::kPlayer);
	OperationBytes op{};
	if (vote == ChamberVote::Election || (vote == ChamberVote::Diet && choice < 3)) {
		const uint32_t words[3] = {0x3F, player, choice};
		std::memcpy(op.data(), words, sizeof words);
		return op;
	}
	if (vote == ChamberVote::Council && choice < 3) {
		const uint32_t words[4] = {0x49, player, town, choice};
		std::memcpy(op.data(), words, sizeof words);
		return op;
	}
	return std::nullopt;
}

uint32_t cellar_dish(const SaveGame& save, uint32_t town) {
	const uint32_t day = (save.globals.u32(global::kWorldTick) >> 8) % 31;
	return (day * (town + 1) + town + (town >> 1)) % 31;
}

std::array<int32_t, 8> coffers_balance(const SaveGame& save, uint32_t town, bool last_month) {
	const TownMemory& t = save.towns[town].memory;
	const uint32_t b = last_month ? 0x730 : 0x704;
	auto v = [&](uint32_t off) { return t.s32(b + off); };
	return {int32_t(uint32_t(v(0)) + uint32_t(v(4))), v(8), int32_t(uint32_t(v(0xC)) - uint32_t(v(0x24))), -v(0x14), -v(0x18),
		-v(0x1C), -v(0x20), -v(0x28)};
}

bool coffers_can_take(const SaveGame& save, uint32_t town) {
	return !tick::events_detail::fn_633ae0(save, town);
}

std::optional<OperationBytes> coffers_operation(const SaveGame& save, uint32_t town, bool donate, int32_t amount) {
	if (amount == 0) {
		return std::nullopt;
	}
	OperationBytes op{};
	const uint32_t words[5] = {0x47, save.globals.u32(global::kPlayer), town, donate ? 1u : 0u, uint32_t(amount)};
	std::memcpy(op.data(), words, sizeof words);
	return op;
}

OperationBytes guard_ship_operation(uint32_t town, uint32_t ship, uint32_t pay) {
	OperationBytes op{};
	const uint32_t words[4] = {0x44, town, ship, pay};
	std::memcpy(op.data(), words, sizeof words);
	return op;
}

}  // namespace p3::data
