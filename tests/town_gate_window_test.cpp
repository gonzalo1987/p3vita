// The town gate's window (p3data/town_gate_window.hpp), without game data: its side menu's buttons as 0x5ABFA0 enables
// them and the note its drawing 0x5AB360 writes under the title; the page «Overland route» as its update 0x5F2A20
// reads the player's road (shown or closed, the materials, the greeting, the red warning) and the operation 0x3B its
// «->» queues (the engine's 0x3B checked by the oracle: the scenario jugador_materiales, docs/oracle.md).

#include <cstdio>
#include <cstring>

#include "p3data/game_fixes.hpp"
#include "p3data/town_gate_window.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

// Two towns (0, 1), the player (merchant 0) living in the town 1, no siege objects' lists filled.
SaveGame world() {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 2;
	g.u16(global::kWorldMerchantCount) = 1;
	g.u32(global::kPlayer) = 0;
	save.towns.resize(2);
	save.merchants.resize(1);
	save.sieges.resize(2);
	save.siege_extras.resize(2);
	for (SavedTown& t : save.towns) {
		t.memory.u8(0x6F1) = 0xFF;
	}
	save.merchants[0].memory.u8(0x19) = 1;
	return save;
}

}  // namespace

int main() {
	{
		// The menu (0x5ABFA0) and its note (0x5AB5C9..).
		SaveGame save = world();
		MerchantMemory& m = save.merchants[0].memory;
		TownGateMenu menu = town_gate_menu(save, 0);
		bool others = false;
		for (uint32_t k = 0; k < kGatePages; ++k) {
			others = others || (k != 4 && menu.on[k]);
		}
		check(menu.on[4] && !others, "another town: «Overland route» alone");
		check(menu.note == 0x6A700C, "another town, not its mayor: 0x6A700C");
		menu = town_gate_menu(save, 1);
		check(menu.on[4] && !menu.on[1] && menu.note == 0x6A7010, "at home under the rank 4: 0x6A7010, «Goods» off");
		m.u8(0x39C + 1) = 4;
		menu = town_gate_menu(save, 1);
		check(menu.on[1] && !menu.on[0] && !menu.on[2] && !menu.on[3] && !menu.on[5] && !menu.on[6] && !menu.on[7] &&
				  menu.note == 0,
			"at home of the rank 4: «Goods for sale» on, «Presents» off (not the mayor), no note");
		save.towns[0].memory.u8(0x6F1) = 0;
		menu = town_gate_menu(save, 0);
		check(menu.on[0] && menu.on[1] && menu.note == 0, "the mayor of another town: «Presents», no note (his rank at home 4)");
		m.u8(0x39C + 1) = 3;
		check(town_gate_menu(save, 0).note == 0x6A7010, "the mayor elsewhere, under the rank 4 at home: 0x6A7010");
		// «Delivery agreements»: his deliveries promised (0x636590), or a supply contract (0x635660).
		GameMemory<8> node;
		node.u8(0x3) = 0;
		save.siege_extras[0].list_101c.push_back(node);
		check(town_gate_menu(save, 0).on[2], "his deliveries promised: «Delivery agreements» on");
		save.siege_extras[0].list_101c.clear();
		check(!town_gate_menu(save, 0).on[2], "none: off");
		save.sieges[0].u8(0x1010) = 7;
		save.sieges[0].u8(0x1011) = 9;
		save.sieges[0].u8(0x1015) = 3;
		check(siege_contract_ware(save, 0) == 9 && town_gate_menu(save, 0).on[2], "a contract offered: the ware 9, on");
		// «Loan»: a node of [+0x1020] of his with [+0x7] (0x6365E0).
		GameMemory<0x10> loan;
		loan.u8(0x5) = 0;
		save.siege_extras[0].list_1020.push_back(loan);
		check(!town_gate_menu(save, 0).on[3], "a loan node without [+0x7]: off");
		save.siege_extras[0].list_1020[0].u8(0x7) = 1;
		check(town_gate_menu(save, 0).on[3], "his loan: «Loan» on");
		// A siege (0x633AE0: an army whose [+0x21E] is above 0, not leaving).
		save.sieges[0].u32(8) = 1;
		save.siege_extras[0].army = SiegeArmyMemory();
		SiegeArmyMemory& army = *save.siege_extras[0].army;
		army.u16(0x21E) = 5;
		menu = town_gate_menu(save, 0);
		check(!menu.on[5] && menu.on[6] && menu.on[7], "besieged, no town watch: «Attack» off, «Surrender» and «Buy freedom» on");
		check(menu.note == 0x6A70F4, "besieged by mercenaries, the mayor: 0x6A70F4");
		save.towns[0].memory.u8(0x998) = 2;
		check(town_gate_menu(save, 0).on[5], "the town watch on duty: «Attack» on");
		army.u8(0x61C) = 1;
		check(town_gate_menu(save, 0).note == 0x6A70F8, "the prince's army, the mayor: 0x6A70F8");
		save.towns[0].memory.u8(0x6F1) = 0xFF;
		m.u8(0x39C + 0) = 0;
		check(town_gate_menu(save, 0).note == 0x6A7024, "the prince's army, not the mayor: 0x6A7024");
		army.u8(0x61C) = 0;
		check(town_gate_menu(save, 0).note == 0x6A70F0, "mercenaries, not the mayor: 0x6A70F0");
		save.towns[0].memory.u8(0x6F1) = 0;
		army.u16(0x3C) = 1;
		menu = town_gate_menu(save, 0);
		check(!menu.on[5] && !menu.on[6] && !menu.on[7], "the army's [+0x3C]: the three off");
		army.u16(0x3C) = 0;
		army.u16(0x3A) = 10;
		army.u16(0x6A) = 3;
		menu = town_gate_menu(save, 0);
		check(!menu.on[6] && menu.note == 0x6A7010, "the army leaving ([+0x3A]): off, the usual note (his rank at home 3)");
		check(town_gate_menu(save, 0, true).note == 0x6A70F4, "the window closing the gate ([+0x94]): besieged again");
	}
	{
		// The page «Overland route» (0x5F2A20, 0x5F0DF0) on a road between the towns of the numbers 0x09 and 0x0A
		// (0x672BF8: 0x82 sections).
		SaveGame save = world();
		GlobalMemory& g = save.globals;
		save.towns[0].memory.u8(0x2C1) = 0x09;
		save.towns[1].memory.u8(0x2C1) = 0x0A;
		check(road_window_page(save, 0).task == 0xFFFF, "no task: the page closes");
		save.merchants[0].memory.u8(0x37) = 1;
		g.u16(global::kShipCount) = 0;
		save.merchants[0].memory.u16(0xE) = 0xFFFF;
		save.towns[0].memory.u16(0x784) = 0xFFFF;  // (No office.)
		save.towns[1].memory.u16(0x784) = 0xFFFF;
		g.u16(0x6DD73C + 0x8) = 0;
		g.u16(0x6DD73C + 0xC) = 1;
		save.events.resize(1);
		GameMemory<0x18>& e = save.events[0];
		e.u16(4) = 0xFFFF;
		e.u16(6) = 0x32;
		e.u8(0xC) = 4;  // A town to grow: not a road.
		e.u8(0xD) = 0;
		check(road_window_page(save, 0).task == 0xFFFF, "another kind of task: the page closes");
		e.u8(0xC) = 1;
		e.u8(0x10) = 0;
		e.u8(0x11) = 1;
		e.u8(0x12) = 5;  // Five sections left.
		e.u8(0x14) = 2;  // Wood 2, bricks 4 (a section's), tools 1, hemp 2.
		e.u8(0x15) = 4;
		e.u8(0x16) = 1;
		e.u8(0x17) = 2;
		RoadWindowPage page = road_window_page(save, 0);
		check(page.task == 0, "the road: shown");
		check(page.materials.needed(0) == 13 && page.materials.needed(1) == 16 && page.materials.needed(2) == 4 &&
				  page.materials.needed(3) == 8,
			"needed: a section's (3, 4, 1, 2) · 5 less what was delivered");
		check(page.materials.percent() == 96, "built: (0x82 − 5) · 100 / 0x82 of the wood");
		check(!page.home && page.short_of_a_section && !page.all_delivered, "another town, the wood short of a section's");
		check(road_window_page(save, 1).home, "the player's home: the greeting 0x6A70CC");
		// A third town, not on the road: the page as in its two towns (the game's), closed with fix_game_bugs() (the
		// executor 0x53A3E0 takes only the road's towns).
		g.u32(global::kWorldTownCount) = 3;
		save.towns.resize(3);
		save.towns[2].memory.u16(0x784) = 0xFFFF;
		check(road_window_page(save, 2).task == 0, "another town: shown (the game's)");
		set_fix_game_bugs(true);
		check(road_window_page(save, 2).task == 0xFFFF && road_window_page(save, 0).task == 0,
			"fixed: closed off the road, shown in its towns");
		set_fix_game_bugs(false);
		e.u8(0x14) = 3;
		check(!road_window_page(save, 0).short_of_a_section, "every material a section's: no red warning");
		check(!page.can_give(0), "nothing there: «->» off");
		page.available = {7, 0, 0, 0};
		check(page.can_give(0) && !page.can_give(1), "7 loads of wood there: its «->» on");
		const OperationBytes op = road_window_give(save, 0, page, 0, 5);
		uint16_t event = 0xFFFF;
		uint16_t amount = 0;
		std::memcpy(&event, op.data() + 4, 2);
		std::memcpy(&amount, op.data() + 6, 2);
		check(op[0] == 0x3B && event == 0 && amount == 5 && op[8] == 0 && op[9] == 0 && op[0xA] == 0x0B,
			"«->» with 5: {0x3B, the event, 5, the town, the player, the wood 0x0B}");
		std::memcpy(&amount, road_window_give(save, 0, page, 0, 0xFFFFFFFF).data() + 6, 2);
		check(amount == 7, "Max: what there is (7, under the 13 needed)");
		e.u8(0x12) = 0;
		e.u8(0x14) = 0;
		e.u8(0x15) = 0;
		e.u8(0x16) = 0;
		e.u8(0x17) = 0;
		check(road_window_page(save, 0).all_delivered, "no sections left, nothing kept: all delivered (0x6A70D4)");
	}
	{
		// The siege pages' rules (0x5F3510, 0x5EFF1D, 0x5F3750, 0x5F2F90, 0x5F30B0, 0x5F31C0).
		SaveGame save = world();
		GlobalMemory& g = save.globals;
		g.u8(0x6DE4A1) = 100;  // The year's day.
		g.u16(0x6DE4A2) = 0x546;  // The year.
		SiegeMemory& s = save.sieges[1];
		for (uint32_t k = 0; k < 4; ++k) {
			s.u8(0x1010 + k) = 0xFF;
		}
		s.u8(0x1010) = 0x10;  // Pig iron: worth 0x3EE147AF (0.44000003…) a unit, by loads: ftol(… · 2000) = 880.
		s.u8(0x1012) = 3;     // Beer: 0x3E322D0E · 200 = 34.
		const std::vector<GateWare> wares = gate_goods_for_sale(save, 1);
		check(wares.size() == 2 && wares[0].ware == 0x10 && wares[1].ware == 3, "the decree's two wares, in order");
		check(wares[0].price == 880 + (101 * 0x546) % (880 * 3 / 10), "pig iron: 880 + (1 + 100) · 1350 % 264");
		check(wares[1].price == 34 + (101 * 0x546) % (34 * 3 / 10), "beer: 34 + … % 10");
		s.u8(0x1014) = 3;  // The pig iron three months: the contract offered.
		const GateContractOffer o = gate_contract_offer(save, 1);
		check(o.ware == 0x10 && o.price == 880 + 88 + (101 * 0x546) % 88, "the offer: pig iron at 880 + 88 + … % 88");
		g.u32(global::kWorldTick) = (31 + 28 + 2) << 8;  // The 3rd of March: 10 days on, the 13th: the 21st.
		check(gate_contract_offer(save, 1).day == 21 && gate_contract_offer(save, 1).month == 2, "the first delivery");
		check(gate_delivery_top(0x10) == 100 && gate_delivery_step(0x10) == 10 && gate_delivery_top(3) == 500 &&
				  gate_delivery_step(3) == 50,
			"per delivery: 100 loads by 10, 500 barrels by 50");
		const OperationBytes c = gate_contract_operation(save, 1, gate_contract_offer(save, 1), 40);
		check(c[0] == 0x8A && c[4] == 40 && c[0xC] == 0x10 && c[0xE] == 1 && c[0xF] == 1,
			"0x8A: 40, the ware, the town; the next delivery from today (the 7th) not the first's (the 21st)");
		s.u16(0x1018) = 0x2FFF;
		check(gate_prince_mood(save, 1) == 2, "the countdown 0x2FFF: the mood 2");
		s.u16(0x1018) = 0x5000;
		check(gate_prince_mood(save, 1) == 4, "above 0x3FFF: 4");
		OperationBytes op{};
		check(gate_attack(save, 1, op) == 9 && op[0] == 0, "no siege: «already over», nothing queued");
		save.sieges[1].u32(8) = 1;
		save.siege_extras[1].army = SiegeArmyMemory();
		SiegeArmyMemory& army = *save.siege_extras[1].army;
		army.u16(0x21E) = 10;
		army.u16(0x42) = 3;
		check(gate_attack(save, 1, op) == 5 && op[0] == 0, "the army still arriving: «can't attack yet»");
		army.u16(0x42) = 0;
		check(gate_attack(save, 1, op) == 3 && op[0] == 0x90 && op[8] == 1 && op[0xC] == 1, "the attack: 0x90 +0xC 1");
		check(gate_surrender(save, 1, op) == 3 && op[0xB] == 1, "the surrender: 0x90 +0xB 1");
		save.towns[1].memory.u32(0x2D4) = 3000;
		army.u16(0x66) = 7000;
		check(gate_freedom_price(save, 1) == 30000 + (7000 * 2) % 15000, "the ransom: 30000 + 14000 % 15000");
		save.sieges[1].u8(0x101A) = 2;
		uint32_t amount = 0;
		check(gate_buy_freedom(save, 1, 44000, op) == 3 && (std::memcpy(&amount, op.data() + 4, 4), amount == 44000),
			"enough: 0x90 with the amount");
		check(gate_buy_freedom(save, 1, 22000, op) == 5 && op[9] == 1, "half of it: 0x90 +9 1");
		check(gate_buy_freedom(save, 1, 3000, op) == 1 && op[0xA] == 1, "less: 0x90 +0xA 1");
		army.u8(0x79) = 1;
		check(gate_buy_freedom(save, 1, 44000, op) == 0x11 && op[0] == 0, "the ransom being collected: 0x11");
	}
	if (failures) {
		std::fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	std::puts("town_gate_window: ok");
	return 0;
}
