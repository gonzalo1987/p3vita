#include "p3data/simulation.hpp"
#include "p3data/statistics.hpp"

#include <cstring>

#include "p3data/mail.hpp"
#include "p3data/new_game.hpp"
#include "p3data/trade.hpp"
#include "p3data/trade_routes.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/town_tick/votes.hpp"

namespace p3::data {

int town_for_tick(const SaveGame& save, uint32_t tick) {
	const uint32_t t = tick & 0xFF;
	int town = -1;
	if ((t & 7) == 7) {
		town = int(t >> 3);
	} else if ((t & 7) == 3) {
		town = int((t + 0xFF) >> 3);
	}
	return town >= 0 && size_t(town) < save.towns.size() ? town : -1;
}

int merchant_for_tick(const SaveGame& save, uint32_t tick) {
	const uint32_t t = tick & 0xFF;
	int merchant = -1;
	if ((t & 7) == 1) {
		merchant = int(t >> 3);
	} else if ((t & 7) == 6) {
		merchant = int((t + 0x100) >> 3);
	}
	return merchant >= 0 && merchant < save.globals.u16(global::kWorldMerchantCount) ? merchant : -1;
}

void town_tick(SaveGame& save, int town) {
	tick::fn_51ba10(save, town, save.globals.u16(global::kWorldDayOfYear));
}

void advance_game_tick(SaveGame& save, Sailing& sailing, bool run_queue) {
	sailing.begin_tick();
	const uint32_t t = save.tick() & 0xFF;
	if (t == 0) {
		// Al empezar el día.
		tick::fn_5315c0(save, save.tick() % 0x700 == 0);
		if (save.record_statistics) {
			daily_statistics(save);  // 0x54DE90 (statistics.hpp)
		}
		tick::fn_535450(save, save.tick());
		uint8_t& countdown = save.globals.u8(global::kWorld + 0x8B);
		if (countdown > 0) {
			--countdown;
		}
	}
	const int town = town_for_tick(save, save.tick());
	if (town >= 0) {
		town_tick(save, town);
		if ((t & 7) == 7 && size_t(town) < save.sieges.size()) {
			tick::fn_6349f0(save, town);
		}
	}
	if ((t & 7) == 5 && (t >> 3) < 15) {
		tick::fn_52d7d0(save, int(t >> 3));
	}
	const int merchant = merchant_for_tick(save, save.tick());
	if (merchant >= 0) {
		tick::fn_4f7bb0(save, merchant);
	}
	sailing.update_ships();
	tick::set_event_navigation(&sailing.nav());
	tick::set_event_sailing(&sailing);
	tick::fn_4d85c0(save, save.tick());
	for (size_t i = 0; i < save.sieges.size(); ++i) {
		tick::fn_633a90(save, int(i));
	}
	if (!run_queue) {
		return;
	}
	cap_merchant_money(save);
	run_queued_operations(save, sailing);
	sailing.step_battles();  // The battles' clock (battle/battles.cpp).
}

void cap_merchant_money(SaveGame& save) {
	for (uint32_t i = 0; i < save.globals.u16(global::kWorldMerchantCount) && i < save.merchants.size(); ++i) {
		int32_t& money = save.merchants[i].memory.s32(0x0);
		if (money > 999999999) {
			money = 999999999;
		}
	}
}

void use_sailing(Sailing& sailing) {
	tick::set_event_navigation(&sailing.nav());
	tick::set_event_sailing(&sailing);
}

void run_queued_operations(SaveGame& save, Sailing& sailing) {
	std::vector<std::vector<uint8_t>> operations;
	operations.swap(save.queued_operations);
	for (const std::vector<uint8_t>& op : operations) {
		run_operation(save, &sailing, op.data());
	}
	// The end of the queue's loop (0x547254): a route loaded from a file (trade_routes.hpp).
	if (save.globals.u32(kLoadedRoute) != 0) {
		fn_5492d0(save, &sailing);
	}
}

void run_operation(SaveGame& save, Sailing* sailing, const uint8_t* operation) {
	int32_t fields[5];
	std::memcpy(fields, operation, sizeof(fields));
	switch (uint32_t(fields[0])) {
	case 0x00:  // Zarpar (0x537670): {0, barco, ciudad, x, y}.
		if (sailing) {
			sailing->set_sail(fields[1], fields[2], fields[3], fields[4]);
		}
		break;
	case 0x01:  // Sell from a ship to a town's market (0x53578C → 0x537340; trade.cpp).
	case 0x02: {  // Buy for a ship from a town's market (0x53579B → 0x537450).
		uint8_t op[0x14];
		std::memcpy(op, operation, sizeof(op));  // The handlers write the amount back into it.
		if (fields[0] == 0x01) {
			fn_537340(save, op);
		} else {
			fn_537450(save, op);
		}
		break;
	}
	case 0x03:  // Repair a ship in its town's shipyard (0x5357AA → 0x52ACD0; town_tick/ship_operations.cpp).
		tick::fn_5357aa(save, operation);
		break;
	case 0x04:  // Hire sailors (0x53580C → 0x537C20; town_tick/ship_operations.cpp).
		tick::fn_537c20(save, operation);
		break;
	case 0x05:  // Dismiss sailors (0x53581B → 0x537DD0).
		tick::fn_537dd0(save, operation);
		break;
	case 0x06:  // Hire a captain from the town's list (0x53582A → 0x537EE0).
		tick::fn_537ee0(save, operation);
		break;
	case 0x07:  // Dismiss a ship's captain (0x535839 → 0x538000).
		tick::fn_538000(save, sailing, operation);
		break;
	case 0x0D:  // A ship's «Pirate» / «Civil» from its menu (0x5358D5 → 0x5386C0; town_tick/ship_operations.cpp).
		tick::fn_5386c0(save, operation);
		break;
	case 0x08:  // Wares between a ship and its owner's office (0x535848 → 0x538100; trade.cpp).
		fn_538100(save, operation);
		break;
	case 0x09:  // Artillery between a ship and its owner's office (0x535857 → 0x538210; trade.cpp).
		fn_538210(save, operation);
		break;
	case 0x58:  // The office sells to the town (0x5361E2 → 0x53D2B0; trade.cpp).
	case 0x59: {  // The office buys from the town (0x5361F1 → 0x53D3C0).
		uint8_t op[0x14];
		std::memcpy(op, operation, sizeof(op));  // The handlers write the amount back into it.
		if (fields[0] == 0x58) {
			fn_53d2b0(save, op);
		} else {
			fn_53d3c0(save, op);
		}
		break;
	}
	case 0x0A:  // A ship is deleted (0x535866, 0x507FB0).
		tick::fn_535866(save, sailing, operation);
		break;
	case 0x0B:  // The sea map's route by points (0x5358AC → 0x538DE0; town_tick/ship_operations.cpp).
		tick::fn_538de0(save, sailing, operation);
		break;
	case 0x0C:  // Upgrade a ship in its town's shipyard (0x5358BB → 0x508660; town_tick/ship_operations.cpp).
		tick::fn_508660(save, uint32_t(fields[1]), uint32_t(fields[2]));
		break;
	case 0x0E:  // A sailing ship takes another as its target (0x5358E4 → 0x538800; town_tick/ship_operations.cpp).
		tick::fn_538800(save, operation);
		break;
	case 0x0F:  // Two ships swap their captains (0x5358F3).
		tick::fn_5358f3(save, operation);
		break;
	case 0x10:  // Sailors from one ship to another (0x535953 → 0x538930).
		tick::fn_538930(save, operation);
		break;
	case 0x11:  // Hand weapons from one ship to another (0x535962 → 0x5389C0).
		tick::fn_5389c0(save, operation);
		break;
	case 0x12:  // El capitán de un barco mejora (0x538A80).
		tick::fn_538a80(save, operation);
		break;
	case 0x13:  // El capitán de un barco de un jugador humano se retira: el evento 0x27 (0x538C40).
		tick::fn_538c40(save, operation);
		break;
	case 0x15:  // Create a convoy (0x53598F → 0x509B90; town_tick/ship_ai.cpp): {0x15, ship, immediate, days, destination}.
		tick::ship_ai_detail::fn_509b90(save, uint32_t(fields[1]), uint32_t(fields[2]), uint32_t(fields[3]), uint32_t(fields[4]));
		break;
	case 0x16:  // Disband a convoy (0x5359B1 → 0x50A580; convoys_members.cpp): {0x16, convoy}.
		// Then the port scene is redrawn if it shows the convoy's town ([+0x1C]
		// read before: 0x6141D0 on 0x6E6220, UI).
		if (sailing && uint32_t(fields[1]) < save.globals.u16(global::kShips + 0xF6)) {
			sailing->fn_50a580(uint16_t(fields[1]));
		}
		break;
	case 0x17:  // A ship leaves its convoy, or its guide disbands it (0x535A16 → 0x539270; convoys_members.cpp).
		if (sailing) {
			sailing->fn_539270(operation);
		}
		break;
	case 0x18:  // A ship joins a convoy (0x535A25 → 0x509D10; convoys_members.cpp): {0x18, a ship of it, ship}.
		if (sailing) {
			sailing->fn_509d10(uint32_t(fields[1]), uint32_t(fields[2]));
		}
		break;
	case 0x19:  // A convoy sells to the town (0x535A3F → 0x539300; trade.cpp).
	case 0x1A:  // A convoy buys from the town (0x535A50 → 0x539300).
		fn_539300(save, operation, fields[0] == 0x19);
		break;
	case 0x1B:  // Wares between a convoy and its owner's office (0x535A61 → 0x5396B0; trade.cpp).
		fn_5396b0(save, operation);
		break;
	case 0x1C: {  // A convoy sets sail (0x535A70): {0x1C, u16 convoy, u16 wait, town, x, y}.
		uint16_t convoy, wait;
		std::memcpy(&convoy, operation + 4, 2);
		std::memcpy(&wait, operation + 6, 2);
		if (convoy >= save.globals.u16(global::kShips + 0xF6)) {
			break;
		}
		// Its guide [+0x10] in [+4], and 0x537670 on the operation.
		GameMemory<0x3C>& c = save.convoys[convoy];
		if (sailing) {
			sailing->set_sail(c.u16(0x10), fields[2], fields[3], fields[4]);
		}
		if (c.u16(0x16) < wait) {
			c.u16(0x16) = wait;
		}
		break;
	}
	case 0x1D:  // Repair a convoy (0x535AC0 → 0x539990; town_tick/ship_operations.cpp).
		tick::fn_539990(save, operation);
		break;
	case 0x1E:  // Un convoy sale de expedición (0x539B50).
		tick::fn_539b50(save, sailing, operation);
		break;
	case 0x1F:  // Un punto de la ruta de expedición de un convoy (0x539C60).
		tick::fn_539c60(save, operation);
		break;
	case 0x20:  // A docked convoy waits for ships to a destination (0x535AED → 0x539CF0; convoys_members.cpp).
		if (sailing) {
			sailing->fn_539cf0(operation);
		}
		break;
	case 0x21:  // Build, the interface's request (0x53CE30): the type by the table 0x672FC0, or an office.
		tick::construction_detail::fn_53ce30(save, operation, true);
		break;
	case 0x22:  // Construir (0x53CE30): las obras de las ciudades y de los comerciantes.
		tick::construction_detail::fn_53ce30(save, operation, false);
		break;
	case 0x23:  // Empezar la muralla de un lado (0x535B12).
		tick::construction_detail::fn_535b12(save, operation);
		break;
	case 0x24:  // La siguiente pieza de muralla pendiente (0x535B44, 0x522C70).
		tick::construction_detail::fn_535b44(save, operation);
		break;
	case 0x25:  // Streets: {0x25, town, merchant} (0x535B7B → 0x5244F0; town_tick/construction.cpp).
		if (uint32_t(fields[1]) < save.globals.u32(global::kWorldTownCount)) {
			tick::construction_detail::fn_5244f0(save, int(fields[1]), uint32_t(fields[2]));
		}
		break;
	case 0x26:  // A merchant demolishes a building of his (0x535BAD → 0x53CF50; town_tick/construction.cpp).
		tick::construction_detail::fn_53cf50(save, operation);
		break;
	case 0x27:  // Materiales para las obras de una ciudad (0x535BBC).
		tick::construction_detail::fn_535bbc(save, operation);
		break;
	case 0x28:  // Un comerciante pone en venta un edificio (0x53D190, 0x4DAB00).
		tick::fn_53d190(save, operation);
		break;
	case 0x29:  // A loan: the event 0x01 (0x539DC0).
		tick::fn_539dc0(save, operation);
		break;
	case 0x2A:  // A loan paid off before it falls due (0x535C4B → 0x539F30; town_tick/operations.cpp).
		tick::fn_539f30(save, operation);
		break;
	case 0x2B:  // Company shares bought or sold: events 0x18 (0x535C5A → 0x4D98F0; town_tick/operations.cpp).
		tick::fn_4d98f0(save, operation);
		break;
	case 0x2D:  // A ship's name, its first 12 characters (0x535CAA → 0x53CCE0; town_tick/ship_operations.cpp).
	case 0x2E:  // The next 12 characters of a ship's name (0x535CAA → 0x53CCE0).
		tick::fn_53cce0(save, operation);
		break;
	case 0x2F:  // Cancel a ship ordered at a town's shipyard (0x535CB9 → 0x52B110; town_tick/ship_operations.cpp).
		tick::fn_535cb9(save, sailing, operation);
		break;
	case 0x2C:  // Un barco encargado al astillero de una ciudad (0x535C70, 0x52A360).
		tick::fn_535c70(save, operation);
		break;
	case 0x30:  // The poor fed by a merchant (0x535CF0 → 0x4FE557).
		tick::fn_535cf0(save, operation);
		break;
	case 0x31:  // A donation for the church's extension (0x535D49 → 0x4FE420).
		tick::fn_535d49(save, operation);
		break;
	case 0x32:  // A donation to the church (0x535D85 → 0x4FE2D0).
		tick::fn_535d85(save, operation);
		break;
	case 0x33:  // A bid at an auction (0x535DC1 → 0x53A010; town_tick/operations.cpp).
		tick::fn_53a010(save, operation);
		break;
	case 0x34:  // Be told of an auction, or not (0x535DD0 → 0x53A0B0).
		tick::fn_53a0b0(save, operation);
		break;
	case 0x35:  // An automatic bid at an auction: the event 0x2D (0x535DDF → 0x53A170).
		tick::fn_53a170(save, operation);
		break;
	case 0x36:  // Auction a ship (0x535DEE → 0x4DA510; town_tick/events.cpp).
		tick::events_detail::fn_4da510(save, uint32_t(fields[1]));
		break;
	case 0x37:  // El comerciante paga por una ciudad (0x535E04, 0x4F8560).
		tick::fn_535e04(save, operation);
		break;
	case 0x38:  // Una subasta de barco: el evento 0x0A (0x535E3B).
		tick::fn_535e3b(save, operation);
		break;
	case 0x3F:  // The vote of a human player in an election or the diet (0x53AA10).
		tick::fn_53aa10(save, operation);
		break;
	case 0x39:  // A bath (0x535E7F): the merchant's [+0x4].
		tick::fn_535e7f(save, operation);
		break;
	case 0x3B:  // Materiales para la carretera de un encargo de la Hansa (0x53A3E0).
		tick::fn_53a3e0(save, operation);
		break;
	case 0x42:  // A bribe in the bath house that works (0x53AC50).
		tick::fn_53ac50(save, operation);
		break;
	case 0x43:  // A bribe in the bath house that fails (0x53AD10).
		tick::fn_53ad10(save, operation);
		break;
	case 0x46:  // Alcalde para una ciudad nueva (0x535FD6).
		tick::fn_535fd6(save, operation);
		break;
	case 0x48:  // Una propuesta para una ciudad: la votación o el impuesto (0x53B010).
		tick::fn_53b010(save, operation);
		break;
	case 0x4B:  // Un candidato coge un encargo de la Hansa (0x53B440).
		tick::fn_53b440(save, operation);
		break;
	case 0x4D:  // The tavern's trader's wares bought into a ship or the office (0x5360B1 → 0x53B5E0; trade.cpp).
		fn_53b5e0(save, operation);
		break;
	case 0x50:  // Hand weapons bought from or sold to the town for a ship, its convoy or the office (0x5360DE → 0x53BC90; trade.cpp).
		fn_53bc90(save, operation);
		break;
	case 0x56:  // A piece of artillery bought from or sold to the town for the office (0x536177 → 0x53CAF0; trade.cpp).
		fn_53caf0(save, operation);
		break;
	case 0x57:  // The hand weapon the town's weaponsmith makes first (0x536186, inline; trade.cpp).
		fn_536186(save, operation);
		break;
	case 0xBF:  // Another player's ship groups, set or added to (0x536FAB → 0x5436D0; town_tick/operations.cpp).
		tick::fn_5436d0(save, operation);
		break;
	case 0x52:  // The people in a town's tavern taken or left, a commission (0x5360FC → 0x53C2C0; town_tick/operations.cpp).
		tick::fn_53c2c0(save, operation);
		break;
	case 0x53:  // The tavern's traveller goes with a ship: the event 0x10, the letter 0x17 (0x53610B → 0x53C840; town_tick/tavern.cpp).
		tick::fn_53c840(save, operation);
		break;
	case 0x4C:  // The tavern's informer's news: paid, the region known (0x5360A2 → 0x53B4D0; town_tick/tavern.cpp).
		tick::fn_53b4d0(save, operation);
		break;
	case 0x4F:  // The tavern's burglar hired against another merchant: the event 0x0F (0x5360CF → 0x53BA40; town_tick/tavern.cpp).
		tick::fn_53ba40(save, operation);
		break;
	case 0x51:  // Wares delivered for an order taken, the event 0x11 (0x5360ED → 0x53C050; town_tick/town_hall.cpp).
		tick::fn_53c050(save, operation);
		break;
	case 0x3A:  // A cart convoy by land: the event 0x0C, the queue run at once (0x535EAC → 0x53A2C0; town_tick/land_transport.cpp).
		tick::fn_53a2c0(save, operation);
		break;
	case 0x4A:  // The Hanse tasks window's developers' tools: a task dropped or its meeting called (0x536084 → 0x53B1F0; events_rare.cpp).
		tick::events_rare_detail::fn_53b1f0(save, operation);
		break;
	case 0x45:  // A merchant stands for mayor or not: his [+0x118] (0x535FA4 → 0x4F9390, inline).
		tick::fn_535fa4(save, operation);
		break;
	case 0x5A:  // A business's share of jobs (0x536200; town_tick/operations.cpp).
		tick::fn_536200(save, operation);
		break;
	case 0x5B:  // The office administrator's order for a ware (0x536290 → 0x53D5C0).
		tick::fn_53d5c0(save, operation);
		break;
	case 0x5E:  // Hire or dismiss the office's administrator (0x5362F5 → 0x53D990).
		tick::fn_53d990(save, operation);
		break;
	case 0x62:  // The rent of a class of the office's houses (0x536331).
		tick::fn_536331(save, operation);
		break;
	case 0x63:  // The office's guards (0x53639B).
		tick::fn_53639b(save, operation);
		break;
	case 0x66:  // A ware's bit of the office's [+0x3B4] (0x53644B).
		tick::fn_53644b(save, operation);
		break;
	case 0x5F:  // An offer to another merchant: the offers' list, the event 0x13, the letters 0x1E and 0x1F (0x536304 → 0x53DA30; town_tick/offers.cpp).
		tick::fn_53da30(save, operation);
		break;
	case 0x75:  // The answer to an offer: accepted or not, removed, its letters deleted (0x536B65 → 0x53ECB0).
		tick::fn_53ecb0(save, operation);
		break;
	case 0x4E:  // A player's ship turns pirate (0x53B850; 0x509250 with no winner).
		tick::fn_53b850(save, sailing, operation);
		break;
	case 0x5D:  // A merchant opens an office (0x5362AE, 0x51B890).
		tick::construction_detail::fn_5362ae(save, operation);
		break;
	case 0x60:  // A merchant's wedding is arranged: event 0x15 (0x53DA90).
		tick::fn_53da90(save, operation);
		break;
	case 0x61:  // La fiesta de un comerciante: el evento 0x07 (0x53DBF0).
		tick::fn_53dbf0(save, operation);
		break;
	case 0x64:  // Un robo en una oficina: el evento 0x33 (0x5363EE).
		tick::fn_5363ee(save, operation);
		break;
	case 0x81:  // Un encargo de la dieta de la Hansa: el evento 0x05 (0x53FA80).
		tick::fn_53fa80(save, operation);
		break;
	case 0x87:  // A child is born to a married merchant (0x53FEA0).
		tick::fn_53fea0(save, operation);
		break;
	case 0x8B:  // The prince of a besieged town's neighbour asks the player for goods (0x536974 → 0x635B80).
		tick::fn_536974(save, operation);
		break;
	case 0x8C:  // The player accepts the prince's request (0x5369C7 → 0x635CD0).
		tick::fn_5369c7(save, operation);
		break;
	case 0x8E:  // Un asedio: el ejército sitiador (0x536A67, 0x633AF0).
		tick::fn_536a67(save, operation);
		break;
	case 0x68:  // Start or stop a ship's trade route (0x5364C5 → 0x53DF00; trade_routes.cpp).
	case 0x6C: {  // A new trade route for a ship (0x536501 → 0x53E890).
		uint8_t op[0x14];
		std::memcpy(op, operation, sizeof(op));  // The handlers write into it.
		if (fields[0] == 0x68) {
			fn_53df00(save, sailing, op);
		} else {
			fn_53e890(save, op);
		}
		break;
	}
	case 0x69:  // A ware of a trade route's stop (0x5364D4 → 0x53E480).
		fn_53e480(save, operation);
		break;
	case 0x6A:  // Insert, remove or change a trade route's stop (0x5364E3 → 0x53E610).
		fn_53e610(save, operation);
		break;
	case 0x6B:  // Move a stop of a ship's trade route (0x5364F2 → 0x53E7C0).
		fn_53e7c0(save, operation);
		break;
	case 0x6D:  // A trade route stop's action (0x536510 → 0x53E9C0).
		fn_53e9c0(save, operation);
		break;
	case 0x6E:  // The player has read a letter (0x536AC7 → 0x4D69D0): {0x6E, merchant, entry}.
		mark_letter_read(save, fields[1], uint32_t(fields[2]));
		break;
	case 0x6F:  // The player deletes a letter (0x536AE1 → 0x4D6A50): {0x6F, merchant, entry}.
		delete_letter(save, fields[1], uint32_t(fields[2]));
		break;
	case 0x70: {  // A merchant's letter settings (0x536AFB → 0x4D6AF0): u16 [+4] merchant, [+6..+0xD], [+0xE].
		uint16_t merchant = 0;
		std::memcpy(&merchant, operation + 4, 2);
		set_letter_settings(save, merchant, operation + 6, operation[0xE]);
		break;
	}
	case 0x71:  // The letters checked (0x536B1C): with [+8], the chains repaired (0x4D7960); the old ones removed (0x4D6780).
		if (fields[2] != 0) {
			repair_letter_chains(save);
		}
		expire_letters(save, fields[1]);
		break;
	case 0x72:  // The player answers a suitor's letter 0x21 (0x53EAC0): the event 0x17 or the letter 0x2B.
		tick::fn_53eac0(save, operation);
		break;
	case 0x77:  // The creditor answers a letter 0x79 (0x53EEC0): lenient, or the impounding (the letter 0x83).
		tick::fn_53eec0(save, operation);
		break;
	case 0x65:  // An AI merchant goes bankrupt (0x53DD70, 0x4F5D80).
		tick::fn_53dd70(save, operation);
		break;
	case 0x67:  // Un administrador mejora (0x53DDF0).
		tick::fn_53ddf0(save, operation);
		break;
	case 0x78:  // Un barco nuevo en las entradas de [0x6DD7C0] (0x5051E0).
		tick::fn_5051e0(save, sailing, operation);
		break;
	case 0x79:  // La dieta de la Hansa: un byte por ciudad (0x536535).
		tick::fn_536535(save, operation);
		break;
	case 0x7A:  // La dieta de la Hansa: [0x834..0x83F] de una ciudad (0x536570).
		tick::fn_536570(save, operation);
		break;
	case 0x7B:  // Capitanes y administradores que buscan trabajo (0x5365E3).
		tick::fn_5365e3(save, operation);
		break;
	case 0x7C:  // La peste: el evento 0x1C (0x536642).
		tick::fn_536642(save, operation);
		break;
	case 0x7D:  // Un incendio en una ciudad: el evento 0x1D (0x5366A0).
		tick::fn_5366a0(save, operation);
		break;
	case 0x7E:  // La temporada de piratas que viene: el evento 0x1F (0x53F8B0).
		tick::fn_53f8b0(save, operation);
		break;
	case 0x7F:  // Ciudades con el bit 0x4000000 y eventos 0x35 (0x5366E8).
		tick::fn_5366e8(save, operation);
		break;
	case 0x80:  // Dos campos del registro 0x80 de la cola, si es un encargo (0x536756).
		tick::fn_536756(save, operation);
		break;
	case 0x83:  // Un evento 0x00 (0x5367B5).
		tick::fn_5367b5(save, operation);
		break;
	case 0x84:  // El encargo del jefe de la Hansa: el evento 0x30 (0x53FCA0).
		tick::fn_53fca0(save, operation);
		break;
	case 0x85:  // Crear un encargo de la Hansa (0x53FD10).
		tick::fn_53fd10(save, operation);
		break;
	case 0x86:  // Quitar los encargos de la Hansa que ya no valen (en 0x536801).
		tick::fn_53fe20(save);
		break;
	case 0x91:  // A ship takes another (0x536B92 → 0x540520; battle/capture.cpp).
		if (sailing) {
			sailing->fn_540520(uint16_t(fields[1]), uint16_t(fields[2]));
		}
		break;
	case 0x94:  // The battles take their steps (0x536BBF; battle/battles.cpp).
		if (sailing) {
			sailing->fn_536bbf(uint32_t(fields[1]) & 0xFFFF);
		}
		break;
	case 0x9F:  // Ships join a sea battle: its music level (0x536CD5 → 0x611309; battle/battle_end.cpp).
		if (uint32_t(fields[1]) < 0xFF) {
			const int battle = save.battles.battle(uint32_t(fields[1]));
			if (battle >= 0) {
				fn_611309(save.battles, size_t(battle), uint16_t(fields[2]));
			}
		}
		break;
	case 0x96:  // A merchant's sides of a battle to the computer or back (0x536C52 → 0x60FEC9; battle/battles.cpp).
		if (sailing && uint32_t(fields[1]) < 0x100) {
			const int battle = save.battles.battle(uint32_t(fields[1]));
			if (battle >= 0) {
				sailing->fn_60fec9(size_t(battle), uint16_t(fields[2]), fields[3] != 0);
			}
		}
		break;
	case 0xC8:  // The time's mode (the real-time loop's 0x546DCF): [+0xC], unless -1 (battle/battles.cpp).
		if (sailing && fields[3] != -1) {
			sailing->set_time_mode(uint32_t(fields[3]));
		}
		break;
	case 0x92:  // The player's orders in a battle (0x536BA1..0x536CC6; battle/orders.cpp): fire,
	case 0x93:  // a target,
	case 0x95:  // a point to head for,
	case 0x97:  // board (capture or plunder),
	case 0x98:  // the sails,
	case 0x9A:  // flee,
	case 0x9B:  // the white flag.
		if (sailing) {
			switch (fields[0]) {
			case 0x92: sailing->fn_540f50(operation); break;
			case 0x93: sailing->fn_541050(operation); break;
			case 0x95: sailing->fn_541200(operation); break;
			case 0x97: sailing->fn_541380(operation); break;
			case 0x98: sailing->fn_5414e0(operation); break;
			case 0x9A: sailing->fn_5415e0(operation); break;
			default: sailing->fn_541700(operation); break;
			}
		}
		break;
	case 0xA0:  // The enemy, fled, leaves the battle (0x536D04 → 0x6116A9; battle/orders.cpp): {0xA0, slot, merchant}.
		if (sailing && uint32_t(fields[1]) < 0xFF) {
			const int battle = save.battles.battle(uint32_t(fields[1]));
			if (battle >= 0) {
				sailing->fn_6116a9(size_t(battle), uint8_t(fields[2]));
			}
		}
		break;
	case 0xB4:  // A new pirate, when a nest is destroyed (0x536E7D → 0x542E40; battle/town_end.cpp).
		if (sailing) {
			sailing->fn_542e40(uint32_t(fields[1]), uint32_t(fields[2]), uint32_t(fields[3]), uint32_t(fields[4]));
		}
		break;
	case 0xB5:  // A pirate convoy leaves the nest (0x536E8C → 0x514D40; battle/pirate_objects.cpp).
		if (sailing && uint32_t(fields[1]) < 5 && save.pirates[size_t(fields[1])] &&
			save.globals.u32(global::kShips + 0xC + 4 * uint32_t(fields[1])) != 0) {
			sailing->fn_514d40(size_t(fields[1]), uint16_t(fields[3]), uint16_t(fields[2]));
		}
		break;
	case 0xA7: {  // A human merchant, the player (0x536D63 → 0x541D70; new_game.cpp).
		uint8_t copy[0x14];
		std::memcpy(copy, operation, sizeof(copy));
		fn_541d70(save, sailing, copy);
		break;
	}
	case 0xAA:  // A merchant's first name, 12 characters more (0x536D90 → 0x512C50; new_game.cpp).
	case 0xAB:  // His family name.
		fn_536d90(save, operation);
		break;
	case 0xAC:  // The new game's goal and the mailbox (0x536DF2 → 0x542420; new_game.cpp).
		fn_542420(save, operation);
		break;
	case 0xAD:  // A world object of a new game (0x536E01 → 0x52BD80; new_game.cpp).
		fn_52bd80(save, operation);
		break;
	case 0xAF: {  // A town of a new game (0x536E3A → 0x531D50, the record at [+0x4]; town_tick/new_town.cpp).
		GameMemory<0x10> rec;
		std::memcpy(rec.bytes.data(), operation + 4, 0x10);
		tick::fn_531d50(save, rec);
		break;
	}
	case 0xB0:  // A town's name from a new game's map (0x536F1D → 0x543570; new_game.cpp).
		fn_543570(save, operation);
		break;
	case 0xB1:  // The new game's town merchants (0x536E50 → 0x542860; new_game.cpp).
		fn_542860(save, operation);
		break;
	case 0xB3: {  // The new game's AI merchants (0x536E6E → 0x542A30; new_game.cpp).
		uint8_t copy[0x14];
		std::memcpy(copy, operation, sizeof(copy));
		fn_542a30(save, copy);
		break;
	}
	case 0xB7:  // The new game starts: the resets and the first events (0x536F0E → 0x5434E0; new_game.cpp).
		fn_5434e0(save, operation);
		break;
	case 0xB8:  // Network games' sync checks (0x542F10).
		tick::fn_542f10(save, operation);
		break;
	default:  // Las demás aún no se reproducen.
		break;
	}
}

}  // namespace p3::data
