// The tavern (p3data/tavern_window.hpp), without game data: the trader's window as the game's update 0x5D6FC0 runs it
// (its arrows, the two presses of its button and what each queues), the operations 0x52 and 0x4D, the offer's units.
// What the town's functions decide (who is there, the offer, the ships) is checked with the oracle: the scenario
// tavern_window.

#include <cstdio>
#include <cstring>

#include "p3data/tavern_window.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

uint32_t u32_at(const OperationBytes& op, size_t at) {
	uint32_t v;
	std::memcpy(&v, op.data() + at, 4);
	return v;
}

uint16_t u16_at(const OperationBytes& op, size_t at) {
	uint16_t v;
	std::memcpy(&v, op.data() + at, 2);
	return v;
}

constexpr uint16_t kShips = 10;

// Two towns, the player (merchant 0) with an office in town 1 if `office`; ten ships, no convoys.
SaveGame world(bool office) {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 2;
	g.u16(global::kWorldMerchantCount) = 2;
	g.u16(global::kWorldOfficeCount) = 1;
	g.u16(global::kShipCount) = kShips;
	g.u32(global::kPlayer) = 0;
	save.towns.resize(2);
	save.merchants.resize(2);
	save.offices.resize(1);
	save.ships.resize(kShips);
	for (uint8_t i = 0; i < 2; ++i) {
		save.towns[i].memory.u8(0x2C0) = i;
		save.towns[i].memory.u16(0x784) = 0xFFFF;
	}
	if (office) {
		save.towns[1].memory.u16(0x784) = 0;
	}
	save.offices[0].memory.u16(0x2C4) = 0;
	save.offices[0].memory.u16(0x2CA) = 0xFFFF;
	save.merchants[0].memory.s32(0) = 10000;
	return save;
}

TraderWindow opened(int32_t price) {
	TraderWindow w;
	TraderOffer o;
	o.tick = 0x1234;
	o.price = price;
	o.amount = 7;
	o.ship = 0xFFFF;
	o.seller = 1;
	o.ware = 3;
	w.offer = o;
	return w;
}

}  // namespace

int main() {
	{
		// The order client (0x5268D0, 0x5D7760): the order in the town's second place, the player's stock there (his
		// office and his docked ships, summed before dividing), «Hand over supplies»' 0x51.
		SaveGame save = world(true);
		GlobalMemory& g = save.globals;
		g.u16(0x6DD73C + 0xC) = 3;
		save.events.resize(3);
		save.towns[1].memory.u16(0x830) = 0xFFFF;
		save.towns[1].memory.u16(0x832) = 1;
		save.events[1].u8(0x16) = 1;
		check(tavern_order(save, 1).event == 0xFFFF, "an order of another merchant: none");
		GameMemory<0x18>& e = save.events[1];
		e.u16(6) = 0x11;
		e.u8(0x16) = 0;
		e.u8(0x8) = 1;
		e.u8(0x9) = 3;  // In barrels.
		e.u8(0xA) = 7;
		e.u8(0xB) = 9;
		e.u32(0xC) = 0x123400;
		e.u16(0x10) = 12;
		e.u16(0x12) = 30;
		e.u16(0x14) = 250;
		e.u8(0x17) = 1;
		save.events[2].u8(0x16) = 1;
		const OrderClient o = tavern_order(save, 1);
		check(o.event == 1 && o.ware == 3 && o.first == 7 && o.family == 9 && o.deadline == 0x123400 && o.left == 12 &&
				  o.total == 30 && o.price == 250 && o.at_end,
			"the player's order: its fields");
		save.offices[0].memory.s32(0x4 + 4 * 3) = 350;
		save.merchants[0].memory.u16(0xE) = 4;
		for (uint16_t k = 0; k < kShips; ++k) {
			save.ships[k].memory.u16(0x4) = 0xFFFF;
		}
		save.ships[4].memory.u16(0x4) = 6;
		save.ships[6].memory.u16(0x4) = 8;
		save.ships[4].memory.u8(0x39) = 1;
		save.ships[4].memory.s32(0x54 + 4 * 3) = 150;
		save.ships[6].memory.u8(0x39) = 1;
		save.ships[6].memory.u8(0x136) = 1;  // Not docked: not counted.
		save.ships[6].memory.s32(0x54 + 4 * 3) = 5000;
		save.ships[8].memory.u8(0x39) = 0;  // Another town.
		save.ships[8].memory.s32(0x54 + 4 * 3) = 5000;
		check(order_client_stock(save, 1, 3) == 2, "350 + 150 units: 2 barrels (summed, then divided)");
		check(order_client_stock(save, 0, 3) == 25, "the other town: the ship there, no office");
		const OperationBytes op = order_delivery_operation(0, 1, 2);
		uint32_t f[5];
		std::memcpy(f, op.data(), sizeof f);
		check(f[0] == 0x51 && f[1] == 0 && f[2] == 0 && f[3] == 1 && f[4] == 2, "{0x51, player, 0, the event, 2}");
	}
	{
		const SaveGame save = world(true);
		const std::vector<uint16_t> ships = {5, 7};
		check(trader_arrows(save, 1, ships) && trader_arrows(save, 1, {5}) && !trader_arrows(save, 1, {}),
			"the arrows: an office and one ship, or two ships");
		check(!trader_arrows(world(false), 1, {5}) && trader_arrows(world(false), 1, ships), "without the office");
		TraderWindow w = opened(100);
		w.ship = 5;
		trader_turn(save, 1, w, ships, 1);
		check(w.ship == 7 && !w.office, "right: the next ship");
		trader_turn(save, 1, w, ships, 1);
		check(w.office && w.ship == kShips, "right from the last: the office");
		trader_turn(save, 1, w, ships, 1);
		check(!w.office && w.ship == 5, "right from the office: the first ship");
		trader_turn(save, 1, w, ships, -1);
		check(w.office && w.ship == kShips, "left from the first: the office");
		trader_turn(save, 1, w, ships, -1);
		check(!w.office && w.ship == 7, "left from the office: the last ship");
		trader_turn(save, 1, w, ships, -1);
		check(!w.office && w.ship == 5, "left: the one before");
		const SaveGame bare = world(false);
		trader_turn(bare, 1, w, ships, -1);
		check(!w.office && w.ship == 7, "without the office, left from the first: the last");
		trader_turn(bare, 1, w, ships, 1);
		check(!w.office && w.ship == 5, "and right from the last: the first");
	}
	{
		// The first press without the money: the page after it (finances), nothing queued.
		const SaveGame save = world(true);
		TraderWindow w = opened(10001);
		w.ship = 5;
		check(!trader_press(save, 1, w, {5}) && w.done && !w.afforded && !w.confirmed, "too dear");
	}
	{
		// The office alone: bought at once into it.
		const SaveGame save = world(true);
		TraderWindow w = opened(10000);
		const auto op = trader_press(save, 1, w, {});
		check(op && w.done && w.placed && w.office && !w.offer, "the office alone: at once");
		check(op && u32_at(*op, 0) == 0x4D && u32_at(*op, 4) == 0x1234 && u32_at(*op, 8) == 10000 &&
				  u16_at(*op, 0xC) == 7 && u16_at(*op, 0xE) == kShips && (*op)[0x10] == 1 && (*op)[0x11] == 0 &&
				  (*op)[0x12] == 3 && (*op)[0x13] == 1,
			"0x4D: tick, price, amount, the office (the ships' count), seller, buyer, ware, town");
	}
	{
		// One ship without an office: at once into it.
		const SaveGame save = world(false);
		TraderWindow w = opened(500);
		w.ship = 5;
		const auto op = trader_press(save, 1, w, {5});
		check(op && w.done && !w.office && u16_at(*op, 0xE) == 5 && !w.convoy, "one ship: at once");
	}
	{
		// One ship and the office: where to (the office first), then «Load goods» into the chosen place.
		const SaveGame save = world(true);
		TraderWindow w = opened(500);
		w.ship = 5;
		check(!trader_press(save, 1, w, {5}) && w.confirmed && !w.done && w.office, "a choice: no operation yet");
		trader_turn(save, 1, w, {5}, 1);
		const auto op = trader_press(save, 1, w, {5});
		check(op && w.done && u16_at(*op, 0xE) == 5, "then into the ship chosen");
	}
	{
		// Neither an office nor a ship: the page after it (no place).
		const SaveGame save = world(false);
		TraderWindow w = opened(500);
		check(!trader_press(save, 1, w, {}) && w.done && w.no_place && w.afforded, "no place");
	}
	{
		// Accepted, then the money gone: nothing queued, the page after it (finances).
		SaveGame save = world(false);
		TraderWindow w = opened(500);
		w.ship = 5;
		trader_press(save, 1, w, {5, 7});
		check(w.confirmed && !w.done, "two ships: where to");
		save.merchants[0].memory.s32(0) = 499;
		check(!trader_press(save, 1, w, {5, 7}) && w.done && !w.afforded, "the money gone");
	}
	{
		TraderOffer o;
		o.amount = 3;
		o.ware = 3;
		check(o.units() == 600, "barrels: 200 each");
		o.ware = 0;
		check(o.units() == 6000, "loads: 2000 each");
		const OperationBytes leave = tavern_leave_operation(4, 9);
		check(u32_at(leave, 0) == 0x52 && u32_at(leave, 4) == 0xFFFFFFFF && u32_at(leave, 8) == 4 &&
				  u32_at(leave, 0xC) == 9 && u32_at(leave, 0x10) == 10,
			"0x52 when the tavern closes: all of the player's left");
		SaveGame save = world(true);
		const OperationBytes talk = tavern_talk_operation(save, 2, 1, 1);
		check(u32_at(talk, 0) == 0x52 && u32_at(talk, 4) == 0 && u32_at(talk, 8) == 2 && u32_at(talk, 0xC) == 1 &&
				  u32_at(talk, 0x10) == 1,
			"0x52 for the trader: no draw");
		const OperationBytes none = tavern_talk_operation(save, 2, 1, 0xFF);
		check(u32_at(none, 0x10) == 0xFF, "0x52 leaving nobody (0x5A7A60's 0xFF)");
	}
	if (failures == 0) {
		std::printf("tavern_window: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
