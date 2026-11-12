#include "p3data/smithy_window.hpp"

#include <cstring>

#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

constexpr uint8_t kPriceSteps[6] = {2, 3, 5, 8, 11, 14};               // on the stack of 0x5285B0
constexpr int32_t kCutlassPrice = 100;                                 // the byte 0x673A78
constexpr int32_t kArtilleryWeight[6] = {1000, 1000, 2000, 2000, 2000, 1000};  // 0x672CB4
constexpr float kHandWeaponBase[4] = {10.0f, 8.0f, 12.0f, 14.0f};       // 0x673A68
constexpr double kHandWeaponFactor = -9.25;                            // 0x67A3F8

bool valid_town(const SaveGame& save, int town) {
	return town >= 0 && uint32_t(town) < save.globals.u32(global::kWorldTownCount) && size_t(town) < save.towns.size();
}

// 0x52E030 on the memory of a town or an office (the same fields).
template <typename Memory>
int32_t artillery_of(const Memory& m, uint32_t kind) {
	if (kind >= 6) {
		return m.s32(0x2BC);
	}
	return m.s32(0x124 + 4 * kind) / kArtilleryWeight[kind];
}

OperationBytes words(std::initializer_list<uint32_t> fields) {
	OperationBytes op{};
	size_t at = 0;
	for (uint32_t f : fields) {
		std::memcpy(op.data() + at, &f, 4);
		at += 4;
	}
	return op;
}

int office_of(const SaveGame& save, uint32_t player, int town) {
	return tick::facilities_detail::fn_5308a0(save, player, uint32_t(town));
}

}  // namespace

int32_t artillery_price(const SaveGame& save, uint32_t town, uint32_t kind) {
	if (kind >= 6) {
		return kCutlassPrice;
	}
	if (town >= save.towns.size()) {
		return 0;
	}
	const int32_t sum = int32_t(save.towns[town].memory.u8(0x2C0)) + int32_t(save.globals.u16(global::kWorldDayOfYear));
	return sum % 5 * 3 + 163 * int32_t(kPriceSteps[kind]);
}

int32_t town_artillery(const SaveGame& save, uint32_t town, uint32_t kind) {
	return town < save.towns.size() ? artillery_of(save.towns[town].memory, kind) : 0;
}

int32_t office_artillery(const SaveGame& save, uint32_t office, uint32_t kind) {
	return office < save.offices.size() ? artillery_of(save.offices[office].memory, kind) : 0;
}

int32_t hand_weapon_price(const SaveGame& save, uint32_t town, uint32_t kind) {
	if (kind >= 4) {
		return 0;
	}
	const uint32_t rest = ((save.globals.u32(global::kWorldTick) >> 8) + town) * 3 % 5;
	return int32_t(rest) - int32_t(double(kHandWeaponBase[kind]) * kHandWeaponFactor);
}

SmithyPage smithy_artillery(const SaveGame& save, uint32_t player, int town) {
	SmithyPage page;
	if (!valid_town(save, town)) {
		return page;
	}
	const int office = office_of(save, player, town);
	page.office = office >= 0;
	for (uint32_t k = 0; k < kArtilleryRows; ++k) {
		SmithyRow row;
		row.town = town_artillery(save, uint32_t(town), k);
		row.office = office >= 0 ? uint16_t(office_artillery(save, uint32_t(office), k)) : 0;
		row.price = artillery_price(save, uint32_t(town), k);
		row.sell = row.price / 2;
		page.rows.push_back(row);
	}
	return page;
}

SmithyPage smithy_hand_weapons(const SaveGame& save, uint32_t player, int town) {
	SmithyPage page;
	if (!valid_town(save, town)) {
		return page;
	}
	const TownMemory& t = save.towns[size_t(town)].memory;
	const int office = office_of(save, player, town);
	page.office = office >= 0;
	page.may_choose = t.u8(0x6F1) == player;
	const uint8_t chosen = uint8_t(t.u32(0x6D0) - 0x14);
	page.priority = chosen <= 3 ? int(chosen) : -1;
	for (uint32_t k = 0; k < kHandWeaponRows; ++k) {
		SmithyRow row;
		row.town = t.s32(0x54 + 4 * k) / 10;
		row.office = office >= 0 ? uint16_t(save.offices[size_t(office)].memory.s32(0x54 + 4 * k) / 10) : 0;
		row.price = hand_weapon_price(save, uint32_t(town), k);
		page.rows.push_back(row);
	}
	return page;
}

int smithy_stopped(const SaveGame& save, int town) {
	if (!valid_town(save, town)) {
		return -1;
	}
	const uint8_t bits = save.towns[size_t(town)].memory.u8(0x775);
	for (int k = 0; k < 5; ++k) {
		if (bits & (1u << k)) {
			return k;
		}
	}
	return -1;
}

OperationBytes smithy_artillery_operation(const SaveGame& save, uint32_t player, uint32_t town, uint32_t kind,
	bool sell) {
	const uint32_t price = uint32_t(artillery_price(save, town, kind));
	return words({0x56, player, kind, sell ? price >> 1 : price, sell ? town + 0x100 : town});
}

OperationBytes smithy_hand_weapon_operation(const SaveGame& save, uint32_t player, uint32_t town, uint32_t kind) {
	OperationBytes op = words({0x50, uint32_t(hand_weapon_price(save, town, kind)), 1});
	op[0xC] = uint8_t(town);
	op[0xD] = uint8_t(player);
	op[0xE] = uint8_t(kind);
	op[0xF] = 1;
	op[0x10] = 0xFF;
	op[0x11] = 0xFF;
	op[0x12] = 0;
	return op;
}

OperationBytes smithy_priority_operation(uint32_t town, int row) {
	return words({0x57, town, uint32_t(row < 0 ? 0xFF : row) + 0x14});
}

}  // namespace p3::data
