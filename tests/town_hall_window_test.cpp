// The town hall's windows (p3data/town_hall_window.hpp), without game data: the arsenal's rules as the game's update
// 0x5B0930 runs them (the mayor, the weekly costs, «Form unit» and its messages, «Dismiss unit») and the operations
// they queue (0x41, 0x40, checked in the engine by the oracle: the scenario militia_ops); the town hall's notices
// (0x5E7730): the world's orders, «Accept order» and its 0x3D (the scenario town_orders), the prince's decree.

#include <cstdio>
#include <cstring>

#include "p3data/town_hall_window.hpp"

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

// One town, the player (merchant 0) its mayor with an office there (`office`).
SaveGame world(bool office) {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 1;
	g.u16(global::kWorldMerchantCount) = 1;
	g.u16(global::kWorldOfficeCount) = 1;
	g.u32(global::kPlayer) = 0;
	save.towns.resize(1);
	save.merchants.resize(1);
	save.offices.resize(1);
	TownMemory& t = save.towns[0].memory;
	t.u8(0x6F1) = 0;
	t.u16(0x784) = office ? 0 : 0xFFFF;
	t.s32(0x2E4) = 100;
	save.offices[0].memory.u16(0x2C4) = 0;
	save.offices[0].memory.u16(0x2CA) = 0xFFFF;
	for (uint32_t k = 0; k < kArsenalTypes; ++k) {
		save.offices[0].memory.s32(0x54 + 4 * k) = 60;
		t.s32(0x54 + 4 * k) = 10;
	}
	return save;
}

}  // namespace

int main() {
	{
		SaveGame save = world(true);
		check(arsenal_mayor(save, 0), "the player the mayor");
		save.towns[0].memory.u8(0x6F1) = 5;
		check(!arsenal_mayor(save, 0), "another mayor");
		TownMemory& t = save.towns[0].memory;
		t.u8(0x998 + 1) = 3;
		t.u8(0x99C + 1) = 2;
		check(arsenal_cost(save, 0, 1) == 5 * 26 * 7, "the archers' weekly cost: 5 · 26 · 7");
		check(arsenal_cost(save, 0, 3) == 0, "no musketeers");
		check(arsenal_open_message(save, 0) == ArsenalMessage::None, "opened: no message");
		t.u32(0x2C8) = 0x20;
		check(arsenal_open_message(save, 0) == ArsenalMessage::Authorised, "opened with the council's limit");
	}
	{
		// «Form unit»: the office's weapons, the beggars, the council's total; queued.
		SaveGame save = world(true);
		TownMemory& t = save.towns[0].memory;
		ArsenalMessage message;
		OperationBytes op{};
		check(arsenal_form(save, 0, 2, message, op) && message == ArsenalMessage::None, "formed");
		check(u32_at(op, 0) == 0x41 && u32_at(op, 4) == 2 && u32_at(op, 8) == 0, "0x41 {type 2, town 0}");
		save.offices[0].memory.s32(0x54 + 4 * 2) = 49;
		check(!arsenal_form(save, 0, 2, message, op) && message == ArsenalMessage::NoWeapons, "49 weapons in the office");
		t.s32(0x2E4) = 19;
		check(!arsenal_form(save, 0, 2, message, op) && message == ArsenalMessage::NoBeggars, "19 beggars: over it");
		t.s32(0x2E4) = 20;
		save.offices[0].memory.s32(0x54 + 4 * 2) = 50;
		t.u32(0x2C8) = 0x40;
		t.u16(0x9A0) = 6;
		t.u8(0x998) = 4;
		t.u8(0x99C + 3) = 2;
		check(!arsenal_form(save, 0, 2, message, op) && message == ArsenalMessage::NoCouncil, "6 units of 6 authorised");
		t.u16(0x9A0) = 7;
		check(arsenal_form(save, 0, 2, message, op) && message == ArsenalMessage::Authorised, "6 of 7: formed, the total");
	}
	{
		// Without an office there, the town's weapons.
		SaveGame save = world(false);
		ArsenalMessage message;
		OperationBytes op{};
		check(!arsenal_form(save, 0, 0, message, op) && message == ArsenalMessage::NoWeapons, "the town's 10 weapons");
		save.towns[0].memory.s32(0x54) = 50;
		check(arsenal_form(save, 0, 0, message, op), "the town's 50");
	}
	{
		SaveGame save = world(true);
		ArsenalMessage message;
		const OperationBytes op = arsenal_dismiss(save, 0, 3, message);
		check(u32_at(op, 0) == 0x40 && u32_at(op, 4) == 3 && u32_at(op, 8) == 0 && message == ArsenalMessage::None,
			"0x40 {type 3, town 0}");
		save.towns[0].memory.u32(0x2C8) = 0x80;
		arsenal_dismiss(save, 0, 3, message);
		check(message == ArsenalMessage::Authorised, "dismissed with the council's limit");
	}
	{
		// The world's orders: one of town 2 (beer, 25 barrels, the bonus), the rest empty.
		SaveGame save = world(true);
		GlobalMemory& g = save.globals;
		g.u32(global::kWorldTownCount) = 3;
		save.towns.resize(3);
		for (uint32_t k = 0; k < 7; ++k) {
			g.u8(0x6DE530 + 12 * k) = 0xFF;
		}
		g.u8(0x6DE530 + 12 * 4) = 2;
		g.u8(0x6DE530 + 12 * 4 + 1) = 3;
		g.u8(0x6DE530 + 12 * 4 + 2) = 1;
		g.u8(0x6DE530 + 12 * 4 + 3) = 25;
		g.u32(0x6DE530 + 12 * 4 + 4) = 0x12345;
		g.u32(0x6DE530 + 12 * 4 + 8) = 70;
		check(!town_order(save, 0) && !town_order(save, 7), "no order in an empty entry nor past the seventh");
		const std::optional<TownOrder> o = town_order(save, 4);
		check(o && o->town == 2 && o->ware == 3 && o->bonus && o->count == 25 && o->deadline == 0x12345 && o->price == 70,
			"the order 4");
		// The client: c = (the window's town 1 + ware 3 + count 25) & 3 = 1.
		save.towns[2].memory.u8(0x6E8 + 1) = 0x11;
		save.towns[2].memory.u8(0x6EC + 1) = 0x22;
		OperationBytes op{};
		save.merchants[0].memory.u16(0x22) = 19;
		check(!accept_order(save, 1, 4, op), "refused below the standing 20");
		save.merchants[0].memory.u16(0x22) = 0xFFF0;
		check(!accept_order(save, 1, 4, op), "refused with a negative standing");
		check(!accept_order(save, 1, 3, op), "no order to accept");
		save.merchants[0].memory.u16(0x22) = 20;
		check(accept_order(save, 1, 4, op), "accepted at the standing 20");
		uint16_t count, left, price;
		std::memcpy(&count, op.data() + 0xC, 2);
		std::memcpy(&left, op.data() + 0xE, 2);
		std::memcpy(&price, op.data() + 0x10, 2);
		check(op[0] == 0x3D && op[4] == 2 && op[5] == 3 && op[6] == 0x11 && op[7] == 0x22 && u32_at(op, 8) == 0x123FF &&
				  count == 25 && left == 25 && price == 70 && op[0x12] == 0 && op[0x13] == 1,
			"0x3D {town 2, beer, the client, the deadline | 0xFF, 25, 25, 70, the player, the bonus}");
	}
	{
		// The prince's decree: wares 5 and 7 lacking, both three months in the list ([+0x1014 + k] 3).
		SaveGame save = world(true);
		save.sieges.resize(1);
		save.siege_extras.resize(1);
		SiegeMemory& s = save.sieges[0];
		s.u8(0x1010) = 0xFF;
		check(!prince_decree(save, 0), "no decree with [+0x1010] 0xFF");
		const uint8_t wares[4] = {5, 7, 0xFF, 0xFF};
		for (uint32_t k = 0; k < 4; ++k) {
			s.u8(0x1010 + k) = wares[k];
			s.u8(0x1014 + k) = k < 2 ? 3 : 0;
		}
		s.u8(0x4) = 2;
		save.globals.u8(global::kWorldMonth) = 1;
		// (2 + 1) % 1 == 0 picks the ware 5; (2 + 1) % 2 == 1 leaves it.
		std::optional<PrinceDecree> d = prince_decree(save, 0);
		check(d && d->wares[0] == 5 && d->wares[1] == 7 && d->wares[2] == 0xFF && d->contract == 5, "the contract of 5");
		save.globals.u8(global::kWorldMonth) = 2;
		// (2 + 2) % 1 == 0 picks 5, (2 + 2) % 2 == 0 then 7.
		d = prince_decree(save, 0);
		check(d && d->contract == 7, "the contract of 7");
		GameMemory<8> node{};
		node.u8(0x3) = 0;
		save.siege_extras[0].list_101c.push_back(node);
		d = prince_decree(save, 0);
		check(d && d->wares[0] == 5 && d->contract == 0xFF, "no contract with the player's deliveries promised");
	}
	if (failures) {
		std::fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	std::printf("town_hall_window: ok\n");
	return 0;
}
