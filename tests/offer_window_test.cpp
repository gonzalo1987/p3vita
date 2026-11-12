// Another merchant's office (p3data/offer_window.hpp), without game data: the merchants it lists (0x531470), what the
// player can offer (0x5BDB17..0x5BDD78) and the bytes of «Offer» (0x5BE020..0x5BE16A).

#include <cstdio>
#include <cstring>

#include "p3data/offer_window.hpp"

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

// One town with three offices: the player's (merchant 0), merchant 1's and merchant 2's (his [+0x8] & 4: left out);
// the player's three ships there: 0 docked, 1 in the shipyard (state 6), 2 marked [+0x136] & 1.
SaveGame world() {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 1;
	g.u16(global::kWorldMerchantCount) = 3;
	g.u16(global::kWorldOfficeCount) = 3;
	g.u16(global::kShipCount) = 3;
	g.u32(global::kPlayer) = 0;
	save.towns.resize(1);
	save.merchants.resize(3);
	save.offices.resize(3);
	save.ships.resize(3);
	save.towns[0].memory.u16(0x784) = 0;
	for (uint16_t i = 0; i < 3; ++i) {
		OfficeMemory& o = save.offices[i].memory;
		o.u16(0x2C4) = i;
		o.u16(0x2CA) = uint16_t(i + 1);
	}
	save.merchants[2].memory.u8(0x8) = 4;
	save.merchants[0].memory.s32(0x0) = 5000;
	save.merchants[0].memory.u16(0xE) = 0;
	const uint16_t states[3] = {0, 6, 0};
	for (uint16_t i = 0; i < 3; ++i) {
		ShipMemory& s = save.ships[i].memory;
		s.u8(0x0) = 0;
		s.u16(0x4) = i + 1 < 3 ? uint16_t(i + 1) : 0xFFFF;
		s.u16(0x8) = 0xFFFF;
		s.u8(0x39) = 0;
		s.u16(0x134) = states[i];
	}
	save.ships[2].memory.u8(0x136) = 1;
	// Beer (in barrels of 200): the office 150, the docked ship 150, the ship in the shipyard 300 (counted too), the
	// marked one 2000 (not counted): 600, 3 barrels. Grain: the office 1999, no whole load.
	save.offices[0].memory.s32(4 + 4 * 3) = 150;
	save.ships[0].memory.s32(0x54 + 4 * 3) = 150;
	save.ships[1].memory.s32(0x54 + 4 * 3) = 300;
	save.ships[2].memory.s32(0x54 + 4 * 3) = 2000;
	save.offices[0].memory.s32(4 + 4 * 0) = 1999;
	return save;
}

}  // namespace

int main() {
	{
		const SaveGame save = world();
		const std::vector<uint16_t> merchants = offer_merchants(save, 0);
		check(merchants.size() == 1 && merchants[0] == 1, "the merchants: not the player nor one with [+0x8] & 4");
		const std::vector<OfferChoice> c = offer_choices(save, 0, 0);
		check(c.size() == 3, "money, a ware and a ship");
		check(c.size() > 0 && c[0].kind == 2 && c[0].most == 5000, "the money, up to what he has");
		check(c.size() > 1 && c[1].kind == 0 && c[1].what == 3 && c[1].most == 3,
			"beer: the office and the ships there in any state, not the marked one");
		check(c.size() > 2 && c[2].kind == 1 && c[2].what == 0, "the ships: docked and not marked (0x504F30)");
		check(!offer_from_ships_only(save, 0, 0, c[1]), "with an office, no warning");
	}
	{
		SaveGame save = world();
		save.merchants[0].memory.s32(0x0) = 0;
		save.offices[0].memory.u16(0x2C4) = 7;  // no office of his in the town
		const std::vector<OfferChoice> c = offer_choices(save, 0, 0);
		check(c.size() == 2 && c[0].kind == 0 && c[0].most == 2, "no money; beer from his ships only (450: 2)");
		check(offer_from_ships_only(save, 0, 0, c[0]), "wares without an office: the warning");
		check(offer_choices(save, 0, 1).empty() && offer_merchants(save, -1).empty(), "a town out of the world");
	}
	{
		const OfferChoice beer{0, 3, 3};
		const OperationBytes op = offer_operation(1234, 500, 0, 1, 0, beer, 2);
		check(u32_at(op, 0) == 0x5F && u32_at(op, 4) == 1234 && int32_t(u32_at(op, 8)) == 500, "0x5F, its number, price");
		check(op[0xC] == 0 && op[0xD] == 1 && op[0xE] == 0 && op[0xF] == 0, "A, B, town, the kind 0");
		check(u16_at(op, 0x10) == 3 && u16_at(op, 0x12) == 2, "the ware and how many");
		const OperationBytes money = offer_operation(1, 0, 0, 1, 0, OfferChoice{2, 0, 5000}, 4500);
		check(money[0xF] == 2 && int32_t(u32_at(money, 0x10)) == 4500, "money: s32 amount");
		const OperationBytes ship = offer_operation(1, 0, 0, 1, 0, OfferChoice{1, 9, 0}, 7);
		check(ship[0xF] == 1 && u16_at(ship, 0x10) == 9 && u16_at(ship, 0x12) == 0, "a ship: its number");
		check(can_offer(OfferChoice{1, 9, 0}, 0) && !can_offer(beer, 0) && can_offer(beer, 1),
			"«Offer»: a ship, or an amount over 0");
	}
	if (failures == 0) {
		std::printf("offer_window: OK\n");
	}
	return failures == 0 ? 0 : 1;
}
