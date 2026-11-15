#pragma once

#include <array>
#include <cstdint>
#include <vector>

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

// The siege pages (re/emu/sesion37/notes_gate.md). «An active siege»: one going on (0x633AE0) whose army isn't leaving
// (its [+0x3A] 0); the army's words by its [+0x61C] (the prince's army, else mercenaries).
bool gate_active_siege(const SaveGame& save, uint32_t town);
bool gate_prince_army(const SaveGame& save, uint32_t town);

// «Presents» (update 0x5F1F00, drawing 0x5F0130): the Prince's mood 0..4 (0x5F3750: the siege object's countdown
// [+0x1018], above 0x3FFF 4, else >> 12); the amount's top the treasury ([+0x700] above 0, else 0; the presents' at most
// 999999), by 1000; «Give»: 0x88 {town, amount}, then the message 0x43 (an amount of 1: «insulted») or 0x1A («thanks»).
uint8_t gate_prince_mood(const SaveGame& save, uint32_t town);
int32_t gate_treasury_top(const SaveGame& save, uint32_t town);
OperationBytes gate_presents_operation(uint32_t town, uint32_t amount);

// «Goods for sale» (set page 0x5F3510, update 0x5F2200): the decree's wares ([+0x1010..+0x1013] not 0xFF), each its
// price: base = ftol(0x673A18[w] · 200 or 2000) and base + ((town + the year's day) · the year) % (base · 3 / 10); the
// player's stock there: his office's and each docked ship's barrels or loads (each divided apart); a row's button with
// the amount chosen (1, 5, 50, all): 0x89 {u32 min(chosen, stock), u32 price, ware, player, town}.
struct GateWare {
	uint8_t ware = 0;
	uint16_t price = 0;
};
std::vector<GateWare> gate_goods_for_sale(const SaveGame& save, uint32_t town);
int32_t gate_player_stock(const SaveGame& save, uint32_t town, uint8_t ware);
OperationBytes gate_sell_operation(const SaveGame& save, uint32_t town, const GateWare& w, uint32_t amount);

// «Delivery agreements» (set page 0x5EFF1D, update 0x5F2550, drawing 0x5F0680): the ware offered (0x635660; 0xFF none),
// its price base + base / 10 + ((the year's day + town) · the year) % (base / 10) and the first delivery: the next 7th or
// 21st (after the 21st the 7th of the month after) from the date 10 days on; the amount per delivery up to 500 barrels
// or 100 loads, by 50 or 10. «Confirm»: 0x8A {u16 amount, u16 price, ware, player, town, whether the next delivery
// day from today differs from the first's}.
struct GateContractOffer {
	uint8_t ware = 0xFF;
	uint16_t price = 0;
	int day = 0;    // The first delivery's (1..31).
	int month = 0;  // 0..11.
};
GateContractOffer gate_contract_offer(const SaveGame& save, uint32_t town);
int32_t gate_delivery_top(uint8_t ware);
int32_t gate_delivery_step(uint8_t ware);
OperationBytes gate_contract_operation(const SaveGame& save, uint32_t town, const GateContractOffer& offer, uint32_t amount);
// His contract (0x636590's node: [+0] ware, [+1] deliveries left, [+2] a count, u16 [+4] amount, u16 [+6] price) and its
// next delivery (the next 7th or 21st from today, one later while the node's [+2] is 0xFF); the Prince's words: 6 left
// 0x6A7088, else 0x6A708C when (6 − left) · 10 < [+2] · 13, else 0x6A7090.
struct GateContract {
	bool exists = false;
	uint8_t ware = 0;
	uint8_t left = 0;
	uint8_t count = 0;
	uint16_t amount = 0;
	uint16_t price = 0;
	int day = 0;
	int month = 0;
	uint32_t words = 0;
};
GateContract gate_contract(const SaveGame& save, uint32_t town);

// «Attack», «Surrender», «Buy freedom» pressed (0x5F2F90, 0x5F30B0, 0x5F31C0): the window's message (its [+0x1614]: 1,
// 3, 5, 9 or 0x11, the drawings' texts by it) and the operation 0x90 when one is queued (op[0] 0 when none):
// attack: no active siege 9; the army no longer attacking ([+0x78]) 1; still arriving ([+0x42]) 5; else 0x90 {0, town,
// +0xC 1} 3. Surrender: no active siege 9; collecting the ransom ([+0x79]) 1; else 0x90 {0, town, +0xB 1} 3 (the window
// closes the gate: [+0x94]). Buy freedom: no active siege 9; collecting 0x11; the amount at least the price
// (gate_freedom_price) 0x90 {amount, town} 3 (the gate closed); twice it at least the price and the siege object's
// [+0x101A] ≥ 2 0x90 {0, town, +9 1} 5; else 0x90 {0, town, +0xA 1} 1.
uint8_t gate_attack(const SaveGame& save, uint32_t town, OperationBytes& op);
uint8_t gate_surrender(const SaveGame& save, uint32_t town, OperationBytes& op);
uint8_t gate_buy_freedom(const SaveGame& save, uint32_t town, uint32_t amount, OperationBytes& op);
// min(the town's [+0x2D4], 10000) · 10 + (the army's u16 [+0x66] · (town + 1)) % (that / 2).
uint32_t gate_freedom_price(const SaveGame& save, uint32_t town);

// «Loan» (update 0x5F2E90, drawing 0x5F12A0): the goods the prince of the player's home asked him for and he accepted
// (a node of the town's siege list [+0x1020]: the goods [+0..+4] timber, grain, tools, pitch, beer in loads or barrels,
// [+5] the merchant, [+6] the besieged town, [+7] accepted, [+8] delivered, u32 [+0xC] the deadline, then the repayment).
struct GateLoan {
	std::array<uint8_t, 5> goods{};
	uint8_t besieged = 0;
	bool delivered = false;
	uint32_t tick = 0;
};
// 0x6365B0: his loan accepted and not delivered yet, if any.
bool gate_pending_loan(const SaveGame& save, uint32_t town, GateLoan& out);
// 0x6365E0 in turn: his loans accepted (delivered or not), in the list's order.
std::vector<GateLoan> gate_loans(const SaveGame& save, uint32_t town);
// «Good delivered» on (0x5F146D..0x5F14DB): his office in the town (0x5308A0) has each of the goods.
bool gate_loan_ready(const SaveGame& save, uint32_t town, const GateLoan& loan);
// «Good delivered» (0x5F2EA9): 0x8D {town, player, the besieged town, now + 0x2A00 + rand() % 0x1C00, its low byte 0}
// (the game's rand(), the world's: it moves it).
OperationBytes gate_loan_operation(SaveGame& save, uint32_t town, const GateLoan& loan);
// The wares of 0x636620's text, in its order: grain, pitch, timber, tools, beer (each before the ones already written,
// « and » before the first, «, » between the others), as {ware, amount}.
std::vector<std::pair<uint8_t, uint8_t>> gate_loan_wares(const GateLoan& loan);

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
