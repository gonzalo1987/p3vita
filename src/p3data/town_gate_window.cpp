#include "p3data/town_gate_window.hpp"

#include <cstring>

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick::events_detail {  // events.cpp
bool fn_633ae0(const SaveGame& save, uint32_t town);
}  // namespace p3::data::tick::events_detail

namespace p3::data {

namespace {

// The siege's army ([+0x8] of the town's siege object), or nullptr.
const SiegeArmyMemory* siege_army(const SaveGame& save, uint32_t town) {
	if (town >= save.sieges.size() || town >= save.siege_extras.size() || save.sieges[town].u32(8) == 0 ||
		!save.siege_extras[town].army) {
		return nullptr;
	}
	return &*save.siege_extras[town].army;
}

}  // namespace

bool siege_deliveries_promised(const SaveGame& save, uint32_t town, uint32_t merchant) {
	if (town >= save.siege_extras.size()) {
		return false;
	}
	for (const GameMemory<8>& node : save.siege_extras[town].list_101c) {
		if (node.u8(0x3) == uint8_t(merchant)) {
			return true;
		}
	}
	return false;
}

uint8_t siege_contract_ware(const SaveGame& save, uint32_t town) {
	if (town >= save.sieges.size()) {
		return 0xFF;
	}
	const SiegeMemory& s = save.sieges[town];
	uint8_t ware = 0xFF;
	uint8_t seen = 0;
	for (uint32_t k = 0; k < 4; ++k) {
		if (s.u8(0x1014 + k) != 3) {
			continue;
		}
		++seen;
		if ((int32_t(s.u8(0x4)) + save.globals.u8(0x6DE4A1)) % seen == 0) {
			ware = s.u8(0x1010 + k);
		}
	}
	return ware;
}

bool siege_loan(const SaveGame& save, uint32_t town, uint32_t merchant) {
	if (town >= save.siege_extras.size()) {
		return false;
	}
	for (const GameMemory<0x10>& node : save.siege_extras[town].list_1020) {
		if (node.u8(0x5) == uint8_t(merchant) && node.u8(0x7) != 0) {
			return true;
		}
	}
	return false;
}

TownGateMenu town_gate_menu(const SaveGame& save, uint32_t town, bool closing) {
	TownGateMenu menu;
	const GlobalMemory& g = save.globals;
	const uint32_t player = g.u32(global::kPlayer);
	if (town >= g.u32(global::kWorldTownCount) || town >= save.towns.size() || player >= save.merchants.size()) {
		return menu;
	}
	const TownMemory& t = save.towns[town].memory;
	const MerchantMemory& m = save.merchants[player].memory;
	const bool mayor = t.u8(0x6F1) == player;
	const uint32_t home = m.u8(0x19);
	const bool besieged = tick::events_detail::fn_633ae0(save, town);
	const SiegeArmyMemory* army = siege_army(save, town);
	// 0x5ABFA0.
	for (uint32_t k = 0; k < kGatePages; ++k) {
		if (k == uint32_t(GatePage::OverlandRoute)) {
			menu.on[k] = true;
			continue;
		}
		if (!mayor && !(home == town && m.u8(0x39C + town) >= 4)) {
			continue;
		}
		switch (GatePage(k)) {
		case GatePage::Presents: menu.on[k] = mayor; break;
		case GatePage::GoodsForSale: menu.on[k] = true; break;
		case GatePage::DeliveryAgreements:
			menu.on[k] = siege_deliveries_promised(save, town, player) || siege_contract_ware(save, town) != 0xFF;
			break;
		case GatePage::Loan: menu.on[k] = siege_loan(save, town, player); break;
		case GatePage::Attack:
		case GatePage::Surrender:
		case GatePage::BuyFreedom:
			if (GatePage(k) == GatePage::Attack && t.u8(0x998) == 0) {
				break;
			}
			menu.on[k] = besieged && army && army->u16(0x3C) == 0 && army->u16(0x3A) == 0 && mayor;
			break;
		default: break;
		}
	}
	// 0x5AB479..0x5AB683.
	if ((besieged && army && army->u16(0x3A) == 0) || closing) {
		if (!army) {
			return menu;  // (The game would read its army's [+0x61C] from no army.)
		}
		if (army->u8(0x61C) != 0) {
			menu.note = mayor ? 0x6A70F8 : 0x6A7024;
		} else {
			menu.note = mayor ? 0x6A70F4 : 0x6A70F0;
		}
	} else if (town != home && !mayor) {
		menu.note = 0x6A700C;
	} else if (home >= save.towns.size() || m.u8(0x39C + home) < 4) {
		menu.note = 0x6A7010;
	}
	return menu;
}

namespace {

constexpr uint8_t kInBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};  // 0x672C14
// 0x673A18 (floats): a unit's worth of each ware.
constexpr uint32_t kWorthBits[24] = {0x3D6147AF, 0x3EF50482, 0x3E6154CA, 0x3E322D0E, 0x3E11EB85, 0x3F0CCCCD,
	0x3FB33333, 0x3F8CCCCD, 0x3F845A1D, 0x40587AE1, 0x3ED33333, 0x3CE147AF, 0x3FA39581, 0x3F8F5C29, 0x3EE147AF,
	0x3E8E5604, 0x3EE147AF, 0x3E6147AF, 0x3F5AE147, 0x3D236E30, 0x41200000, 0x41000000, 0x41400000, 0x41600000};

// ftol(0x673A18[w] · (200 for barrels, 2000 for loads)) (x87: the float times the float constant, truncated).
int32_t ware_base_price(uint8_t ware) {
	if (ware >= 24) {
		return 0;
	}
	float worth;
	std::memcpy(&worth, &kWorthBits[ware], 4);
	return int32_t(double(worth) * (kInBarrels[ware] ? 200.0 : 2000.0));
}

// 0x5F36F0 on a day and month (1..12): the next delivery day, the 7th or the 21st (past the 21st the 7th of the month
// after). Here the month 0..11.
void next_delivery(int& day, int& month) {
	if (day <= 7) {
		day = 7;
	} else if (day <= 21) {
		day = 21;
	} else {
		day = 7;
		month = (month + 1) % 12;
	}
}

const SiegeArmyMemory* army_of(const SaveGame& save, uint32_t town) {
	if (town >= save.sieges.size() || town >= save.siege_extras.size() || save.sieges[town].u32(8) == 0 ||
		!save.siege_extras[town].army) {
		return nullptr;
	}
	return &*save.siege_extras[town].army;
}

OperationBytes siege_window_operation(uint32_t amount, uint32_t town, uint8_t b9, uint8_t ba, uint8_t bb, uint8_t bc) {
	OperationBytes op{};
	const uint32_t code = 0x90;
	std::memcpy(op.data(), &code, 4);
	std::memcpy(op.data() + 4, &amount, 4);
	op[8] = uint8_t(town);
	op[9] = b9;
	op[0xA] = ba;
	op[0xB] = bb;
	op[0xC] = bc;
	return op;
}

}  // namespace

bool gate_active_siege(const SaveGame& save, uint32_t town) {
	const SiegeArmyMemory* army = army_of(save, town);
	return army && tick::events_detail::fn_633ae0(save, town) && army->u16(0x3A) == 0;
}

bool gate_prince_army(const SaveGame& save, uint32_t town) {
	const SiegeArmyMemory* army = army_of(save, town);
	return army && army->u8(0x61C) != 0;
}

uint8_t gate_prince_mood(const SaveGame& save, uint32_t town) {
	if (town >= save.sieges.size()) {
		return 0;
	}
	const uint16_t countdown = save.sieges[town].u16(0x1018);
	return countdown > 0x3FFF ? 4 : uint8_t(countdown >> 12);
}

int32_t gate_treasury_top(const SaveGame& save, uint32_t town) {
	if (town >= save.towns.size()) {
		return 0;
	}
	const int32_t treasury = save.towns[town].memory.s32(0x700);
	return treasury > 0 ? treasury : 0;
}

OperationBytes gate_presents_operation(uint32_t town, uint32_t amount) {
	OperationBytes op{};
	const uint32_t fields[3] = {0x88, town, amount};
	std::memcpy(op.data(), fields, sizeof fields);
	return op;
}

std::vector<GateWare> gate_goods_for_sale(const SaveGame& save, uint32_t town) {
	std::vector<GateWare> out;
	if (town >= save.sieges.size()) {
		return out;
	}
	const GlobalMemory& g = save.globals;
	for (uint32_t k = 0; k < 4; ++k) {
		const uint8_t ware = save.sieges[town].u8(0x1010 + k);
		if (ware == 0xFF) {
			continue;
		}
		const int32_t base = ware_base_price(ware);
		const int32_t spread = int32_t(uint32_t(uint16_t(base)) * 3) / 10;
		const int32_t mixed = (int32_t(town) + g.u8(0x6DE4A1)) * int32_t(g.u16(0x6DE4A2));
		GateWare w;
		w.ware = ware;
		w.price = uint16_t(spread != 0 ? mixed % spread + uint16_t(base) : uint16_t(base));
		out.push_back(w);
	}
	return out;
}

int32_t gate_player_stock(const SaveGame& save, uint32_t town, uint8_t ware) {
	const uint32_t player = save.globals.u32(global::kPlayer);
	if (ware >= 24 || player >= save.merchants.size()) {
		return 0;
	}
	const uint32_t unit = kInBarrels[ware] ? 200 : 2000;
	uint32_t n = 0;
	const int office = tick::facilities_detail::fn_5308a0(save, player, town);
	if (office >= 0) {
		n = save.offices[size_t(office)].memory.u32(0x4 + 4 * ware) / unit;
	}
	const uint32_t ships = save.globals.u16(global::kShipCount);
	size_t walked = 0;
	for (uint32_t s = save.merchants[player].memory.u16(0xE); s < ships && s < save.ships.size() && walked < save.ships.size();
		 s = save.ships[s].memory.u16(0x4), ++walked) {
		const ShipMemory& ship = save.ships[s].memory;
		if (ship.u8(0x39) == town && !(ship.u8(0x136) & 1)) {
			n += ship.u32(0x54 + 4 * ware) / unit;
		}
	}
	return int32_t(n);
}

OperationBytes gate_sell_operation(const SaveGame& save, uint32_t town, const GateWare& w, uint32_t amount) {
	OperationBytes op{};
	const uint32_t stock = uint32_t(gate_player_stock(save, town, w.ware));
	const uint32_t fields[3] = {0x89, amount < stock ? amount : stock, w.price};
	std::memcpy(op.data(), fields, sizeof fields);
	op[0xC] = w.ware;
	op[0xD] = uint8_t(save.globals.u32(global::kPlayer));
	op[0xE] = uint8_t(town);
	return op;
}

GateContractOffer gate_contract_offer(const SaveGame& save, uint32_t town) {
	GateContractOffer o;
	o.ware = siege_contract_ware(save, town);
	if (o.ware >= 24) {
		o.ware = 0xFF;
		return o;
	}
	const GlobalMemory& g = save.globals;
	const int32_t base = int32_t(uint16_t(ware_base_price(o.ware)));
	const int32_t tenth = base / 10;
	const int32_t mixed = (int32_t(g.u8(0x6DE4A1)) + int32_t(town)) * int32_t(g.u16(0x6DE4A2));
	o.price = uint16_t((tenth != 0 ? mixed % tenth : 0) + base + tenth);
	const GameDate d = date_from_tick((g.u32(global::kWorldTick) + 0xA00) & ~0xFFu);
	o.day = d.day;
	o.month = d.month;
	next_delivery(o.day, o.month);
	return o;
}

int32_t gate_delivery_top(uint8_t ware) {
	return ware < 24 && kInBarrels[ware] ? 500 : 100;
}

int32_t gate_delivery_step(uint8_t ware) {
	return ware < 24 && kInBarrels[ware] ? 50 : 10;
}

OperationBytes gate_contract_operation(const SaveGame& save, uint32_t town, const GateContractOffer& offer, uint32_t amount) {
	OperationBytes op{};
	const uint32_t code = 0x8A;
	std::memcpy(op.data(), &code, 4);
	const uint16_t n = uint16_t(amount);
	std::memcpy(op.data() + 4, &n, 2);
	std::memcpy(op.data() + 8, &offer.price, 2);
	op[0xC] = offer.ware;
	op[0xD] = uint8_t(save.globals.u32(global::kPlayer));
	op[0xE] = uint8_t(town);
	const GameDate today = date_from_tick(save.globals.u32(global::kWorldTick));
	int day = today.day;
	int month = today.month;
	next_delivery(day, month);
	op[0xF] = day != offer.day ? 1 : 0;
	return op;
}

GateContract gate_contract(const SaveGame& save, uint32_t town) {
	GateContract c;
	const uint32_t player = save.globals.u32(global::kPlayer);
	if (town >= save.siege_extras.size()) {
		return c;
	}
	for (const GameMemory<8>& node : save.siege_extras[town].list_101c) {
		if (node.u8(0x3) != uint8_t(player)) {
			continue;
		}
		c.exists = true;
		c.ware = node.u8(0x0);
		c.left = node.u8(0x1);
		c.count = node.u8(0x2);
		c.amount = node.u16(0x4);
		c.price = node.u16(0x6);
		const GameDate today = date_from_tick(save.globals.u32(global::kWorldTick));
		c.day = today.day;
		c.month = today.month;
		next_delivery(c.day, c.month);
		if (c.count == 0xFF) {
			++c.day;
			next_delivery(c.day, c.month);
		}
		c.words = c.left == 6 ? 0x6A7088 : (6 - int(c.left)) * 10 < int(c.count) * 13 ? 0x6A708C : 0x6A7090;
		break;
	}
	return c;
}

uint8_t gate_attack(const SaveGame& save, uint32_t town, OperationBytes& op) {
	op = OperationBytes{};
	if (!gate_active_siege(save, town)) {
		return 9;
	}
	const SiegeArmyMemory& army = *army_of(save, town);
	if (army.u8(0x78) != 0) {
		return 1;
	}
	if (army.u16(0x42) != 0) {
		return 5;
	}
	op = siege_window_operation(0, town, 0, 0, 0, 1);
	return 3;
}

uint8_t gate_surrender(const SaveGame& save, uint32_t town, OperationBytes& op) {
	op = OperationBytes{};
	if (!gate_active_siege(save, town)) {
		return 9;
	}
	if (army_of(save, town)->u8(0x79) != 0) {
		return 1;
	}
	op = siege_window_operation(0, town, 0, 0, 1, 0);
	return 3;
}

uint32_t gate_freedom_price(const SaveGame& save, uint32_t town) {
	const SiegeArmyMemory* army = army_of(save, town);
	if (!army || town >= save.towns.size()) {
		return 0;
	}
	uint32_t people = save.towns[town].memory.u32(0x2D4);
	if (people > 10000) {
		people = 10000;
	}
	const uint32_t base = people * 10;
	const uint32_t half = base >> 1;
	return base + (half != 0 ? (uint32_t(army->u16(0x66)) * (town + 1)) % half : 0);
}

uint8_t gate_buy_freedom(const SaveGame& save, uint32_t town, uint32_t amount, OperationBytes& op) {
	op = OperationBytes{};
	if (!gate_active_siege(save, town)) {
		return 9;
	}
	if (army_of(save, town)->u8(0x79) != 0) {
		return 0x11;
	}
	const uint32_t price = gate_freedom_price(save, town);
	if (amount >= price) {
		op = siege_window_operation(amount, town, 0, 0, 0, 0);
		return 3;
	}
	if (amount * 2 >= price && save.sieges[town].u8(0x101A) >= 2) {
		op = siege_window_operation(0, town, 1, 0, 0, 0);
		return 5;
	}
	op = siege_window_operation(0, town, 0, 1, 0, 0);
	return 1;
}

namespace {

GateLoan loan_of(const GameMemory<0x10>& node) {
	GateLoan l;
	for (uint32_t k = 0; k < 5; ++k) {
		l.goods[k] = node.u8(k);
	}
	l.besieged = node.u8(6);
	l.delivered = node.u8(8) != 0;
	l.tick = node.u32(0xC);
	return l;
}

}  // namespace

bool gate_pending_loan(const SaveGame& save, uint32_t town, GateLoan& out) {
	const uint32_t player = save.globals.u32(global::kPlayer);
	if (town >= save.siege_extras.size()) {
		return false;
	}
	for (const GameMemory<0x10>& node : save.siege_extras[town].list_1020) {
		if (node.u8(5) == uint8_t(player) && node.u8(7) != 0 && node.u8(8) == 0) {
			out = loan_of(node);
			return true;
		}
	}
	return false;
}

std::vector<GateLoan> gate_loans(const SaveGame& save, uint32_t town) {
	std::vector<GateLoan> out;
	const uint32_t player = save.globals.u32(global::kPlayer);
	if (town >= save.siege_extras.size()) {
		return out;
	}
	for (const GameMemory<0x10>& node : save.siege_extras[town].list_1020) {
		if (node.u8(5) == uint8_t(player) && node.u8(7) != 0) {
			out.push_back(loan_of(node));
		}
	}
	return out;
}

bool gate_loan_ready(const SaveGame& save, uint32_t town, const GateLoan& loan) {
	const uint32_t player = save.globals.u32(global::kPlayer);
	const int office = tick::facilities_detail::fn_5308a0(save, player, town);
	if (office < 0) {
		return false;
	}
	const OfficeMemory& o = save.offices[size_t(office)].memory;
	// (The game's order: beer, tools, timber, pitch, grain; signed.)
	static constexpr struct {
		uint8_t ware;
		uint8_t at;
		int32_t unit;
	} kGoods[5] = {{3, 4, 200}, {12, 2, 200}, {11, 0, 2000}, {15, 3, 200}, {0, 1, 2000}};
	for (const auto& g : kGoods) {
		if (o.s32(0x4 + 4 * g.ware) < int32_t(loan.goods[g.at]) * g.unit) {
			return false;
		}
	}
	return true;
}

OperationBytes gate_loan_operation(SaveGame& save, uint32_t town, const GateLoan& loan) {
	OperationBytes op{};
	const uint32_t player = save.globals.u32(global::kPlayer);
	const uint32_t now = save.globals.u32(global::kWorldTick);
	const uint32_t when = (now + 0x2A00 + uint32_t(int32_t(tick::game_rand(save)) % 0x1C00)) & ~0xFFu;
	const uint32_t fields[5] = {0x8D, town, player, loan.besieged, when};
	std::memcpy(op.data(), fields, sizeof fields);
	return op;
}

std::vector<std::pair<uint8_t, uint8_t>> gate_loan_wares(const GateLoan& loan) {
	std::vector<std::pair<uint8_t, uint8_t>> out;
	const std::pair<uint8_t, uint8_t> order[5] = {{0, loan.goods[1]}, {15, loan.goods[3]}, {11, loan.goods[0]},
		{12, loan.goods[2]}, {3, loan.goods[4]}};
	for (const auto& w : order) {
		if (w.second != 0) {
			out.push_back(w);
		}
	}
	return out;
}

RoadWindowPage road_window_page(const SaveGame& save, uint32_t town) {
	RoadWindowPage page;
	const uint32_t player = save.globals.u32(global::kPlayer);
	if (town >= save.towns.size() || player >= save.merchants.size()) {
		return page;
	}
	const uint16_t task = merchant_hanse_task(save, int(player));
	if (task == 0xFFFF) {
		return page;
	}
	const HanseTask t = hanse_task(save, task);
	if (t.type != uint8_t(HanseTaskType::Road)) {
		return page;
	}
	if (fix_game_bugs() && t.data[0] != town && t.data[1] != town) {
		// Game bug fixed (the user's word): the page shows in any town's gate, but the 0x3B's executor (0x53A3E0) only
		// takes the road's two towns, so elsewhere its «->» hands nothing over. Fixed: it closes there as without a road.
		return page;
	}
	page.task = task;
	road_materials(save, task, int(player), page.materials);
	page.available = available_road_materials(save, int(player), int(town));
	page.home = save.merchants[player].memory.u8(0x19) == town;
	page.all_delivered = true;
	for (int k = 0; k < 4; ++k) {
		if (page.materials.needed(k) != 0) {
			page.all_delivered = false;
		}
		if (uint16_t(page.materials.delivered[size_t(k)]) < kRoadSection[size_t(k)]) {
			page.short_of_a_section = true;
		}
	}
	return page;
}

OperationBytes road_window_give(const SaveGame& save, uint32_t town, const RoadWindowPage& page, int k, uint32_t chosen) {
	const uint32_t player = save.globals.u32(global::kPlayer);
	const uint16_t amount = road_window_amount(chosen, uint32_t(page.available[size_t(k)]), page.materials.needed(k));
	return road_materials_operation(page.task, amount, uint8_t(town), uint8_t(player), kRoadWares[size_t(k)]);
}

}  // namespace p3::data
