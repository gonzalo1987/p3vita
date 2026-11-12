#include "p3data/offer_window.hpp"

#include <cstring>

#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace tick::events_towns_detail {
std::vector<uint16_t> fn_531470(const SaveGame& save, uint32_t town);  // town_tick/events_towns.cpp
}  // namespace tick::events_towns_detail

namespace {

constexpr uint8_t kInBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};  // 0x672C14
constexpr uint32_t kOfferWares = 20;  // 0x14 rows; the table 0x698538 is the identity

bool valid_town(const SaveGame& save, int town) {
	return town >= 0 && uint32_t(town) < save.globals.u32(global::kWorldTownCount) && size_t(town) < save.towns.size();
}

// The ware's stock the player has in the town: his office's and his ships' there not marked [+0x136] & 1, whatever
// their state (0x5BDBA4..0x5BDC31, again 0x5BE2ED..0x5BE38E).
int32_t ware_held(const SaveGame& save, uint32_t player, int town, uint32_t ware) {
	int32_t total = 0;
	const int office = tick::facilities_detail::fn_5308a0(save, player, uint32_t(town));
	if (office >= 0) {
		total = save.offices[size_t(office)].memory.s32(4 + 4 * ware);
	}
	const uint32_t ships = save.globals.u16(global::kShipCount);
	for (uint32_t i = save.merchants[player].memory.u16(0xE); i < ships; i = save.ships[i].memory.u16(0x4)) {
		const ShipMemory& s = save.ships[i].memory;
		if (s.u8(0x39) == uint32_t(town) && !(s.u8(0x136) & 1)) {
			total = int32_t(uint32_t(total) + uint32_t(s.s32(0x54 + 4 * ware)));
		}
	}
	return total;
}

}  // namespace

std::vector<uint16_t> offer_merchants(const SaveGame& save, int town) {
	return town < 0 ? std::vector<uint16_t>() : tick::events_towns_detail::fn_531470(save, uint32_t(town));
}

std::vector<OfferChoice> offer_choices(const SaveGame& save, uint32_t player, int town) {
	std::vector<OfferChoice> list;
	if (player >= save.globals.u16(global::kWorldMerchantCount) || player >= save.merchants.size() ||
		!valid_town(save, town)) {
		return list;
	}
	const int32_t money = save.merchants[player].memory.s32(0x0);
	if (money > 0) {
		list.push_back({2, 0, money});
	}
	for (uint32_t w = 0; w < kOfferWares; ++w) {
		const int32_t held = ware_held(save, player, town, w);
		const int32_t unit = kInBarrels[w] ? 200 : 2000;
		if (uint32_t(held) / uint32_t(unit) != 0) {
			list.push_back({0, uint16_t(w), held / unit});
		}
	}
	for (uint16_t ship : window_ships(save, int(player), town, ShipListMode::Docked)) {
		list.push_back({1, ship, 0});
	}
	return list;
}

bool can_offer(const OfferChoice& choice, int32_t amount) {
	return choice.kind == 1 || amount > 0;
}

OperationBytes offer_operation(uint32_t number, int32_t price, uint32_t player, uint32_t merchant, int town,
	const OfferChoice& choice, int32_t amount) {
	OperationBytes op{};
	const uint32_t code = 0x5F;
	std::memcpy(op.data(), &code, 4);
	std::memcpy(op.data() + 4, &number, 4);
	std::memcpy(op.data() + 8, &price, 4);
	op[0xC] = uint8_t(player);
	op[0xD] = uint8_t(merchant);
	op[0xE] = uint8_t(town);
	op[0xF] = choice.kind;
	if (choice.kind == 2) {
		std::memcpy(op.data() + 0x10, &amount, 4);
	} else {
		const uint16_t what = choice.what;
		const uint16_t count = choice.kind == 0 ? uint16_t(amount) : 0;
		std::memcpy(op.data() + 0x10, &what, 2);
		std::memcpy(op.data() + 0x12, &count, 2);
	}
	return op;
}

bool offer_from_ships_only(const SaveGame& save, uint32_t player, int town, const OfferChoice& choice) {
	return choice.kind == 0 && valid_town(save, town) &&
		tick::facilities_detail::fn_5308a0(save, player, uint32_t(town)) < 0;
}

}  // namespace p3::data
