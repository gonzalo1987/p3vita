#pragma once

#include <array>
#include <cstdint>

#include "p3data/hanse_tasks.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// The town gate (.let 0x2F): its side menu *0x6E5514 (0x5AB1E0; its buttons' texts 0x6A5DA4.., its title 0x6A5DC4) and
// its window *0x6E55B8 (0x5EEC00, vtable 0x67A668: set page 0x5EFD30, layout 0x5EF020, update 0x5EF660, drawing
// 0x5EF8F0). The menu's buttons, in order, open the window's pages 0..7.
enum class GatePage : uint8_t {
	Presents = 0,
	GoodsForSale = 1,
	DeliveryAgreements = 2,
	Loan = 3,
	OverlandRoute = 4,
	Attack = 5,
	Surrender = 6,
	BuyFreedom = 7,
};
constexpr uint32_t kGatePages = 8;

// The menu's state each frame: which buttons can be pressed (0x5ABFA0) and the text its drawing writes under the title
// (0x5AB360: an English slot, 0 none).
struct TownGateMenu {
	std::array<bool, kGatePages> on{};
	uint32_t note = 0;
};
// «Overland route» always; the others only for the town's mayor ([+0x6F1]) or a citizen there of the rank 4 or more,
// and then: «Presents» for the mayor; «Goods for sale»; «Delivery agreements» with his deliveries promised to the
// prince (0x636590) or a supply contract the prince offers (0x635660); «Loan» with a loan of his to the prince
// (0x6365E0); «Attack» with the town watch on duty ([+0x998]) and, as «Surrender» and «Buy freedom», a siege going on
// (0x633AE0) whose army isn't leaving ([+0x3A], [+0x3C] 0), for the mayor. The note: besieged (the siege going on and
// its army not leaving, or the window's [+0x94] set as it queues 0x90: `closing`) by the prince's army ([+0x61C]) 0x6A70F8
// for the mayor, else 0x6A7024; by mercenaries 0x6A70F4 for the mayor, else 0x6A70F0; not besieged, a merchant not of
// the town nor its mayor 0x6A700C, else under the rank 4 at home 0x6A7010.
TownGateMenu town_gate_menu(const SaveGame& save, uint32_t town, bool closing = false);

// 0x636590 on the town's siege object: the merchant's deliveries promised to the prince (his node in [+0x101C]).
bool siege_deliveries_promised(const SaveGame& save, uint32_t town, uint32_t merchant);
// 0x635660 on it: the ware of the supply contract the prince offers (one of the decree's wares [+0x1010 + k] of the
// three months [+0x1014 + k] = 3, chosen by ([+0x4] + the year's day 0x6DE4A1) % the count so far), or 0xFF.
uint8_t siege_contract_ware(const SaveGame& save, uint32_t town);
// 0x6365E0(merchant, 0) on it: a loan of the merchant's to the prince (a node of [+0x1020] with [+0x5] the merchant and
// [+0x7] set).
bool siege_loan(const SaveGame& save, uint32_t town, uint32_t merchant);

// The page «Overland route» (update 0x5F2A20, drawing 0x5F0DF0): only while the player's Hanse task (0x4EC694) is a road
// (kind 1), else the page closes at once (0x5ABF70 lets the menu's buttons go, 0x5EFD30(−1)); with fix_game_bugs(), also
// off the road's two towns (the game's bug: shown there, its «->» hands nothing over, 0x53A3E0). In any town: what is
// still needed of each material, what was delivered (the task's counters), what the player has there (his office and
// his ships docked there, in loads or barrels) and how much of the road is built (the wood's).
struct RoadWindowPage {
	uint16_t task = 0xFFFF;  // The road's event 0x32; 0xFFFF: the page closes.
	RoadMaterials materials;
	std::array<int, 4> available{};
	bool home = false;  // The town is the player's home: the greeting 0x6A70CC, else 0x6A70D0.
	bool all_delivered = false;  // Nothing still needed: 0x6A70D4 instead of the greeting and 0x6A70D8.
	bool short_of_a_section = false;  // A material delivered short of a section's (0x672BF0): 0x6A7100 in red.
	// The «->» of a material: something still needed and something there.
	bool can_give(int k) const { return materials.needed(k) != 0 && available[size_t(k)] != 0; }
};
RoadWindowPage road_window_page(const SaveGame& save, uint32_t town);
// The material's «->» pressed with the amount chosen (1, 5, 50 or all: kRoadAmounts): the operation 0x3B
// (road_materials_operation with road_window_amount and the material's ware).
OperationBytes road_window_give(const SaveGame& save, uint32_t town, const RoadWindowPage& page, int k, uint32_t chosen);

}  // namespace p3::data
