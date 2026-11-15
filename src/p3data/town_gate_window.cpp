#include "p3data/town_gate_window.hpp"

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"

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
