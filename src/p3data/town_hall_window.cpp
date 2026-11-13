#include "p3data/town_hall_window.hpp"

#include <cstring>

#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

constexpr uint8_t kArsenalCost[kArsenalTypes] = {0x21, 0x1A, 0x28, 0x2E};  // 0x672DB0

OperationBytes militia_operation(uint32_t code, uint32_t type, uint32_t town) {
	OperationBytes op{};
	std::memcpy(op.data(), &code, 4);
	std::memcpy(op.data() + 4, &type, 4);
	std::memcpy(op.data() + 8, &town, 4);
	return op;
}

bool council_limits(const SaveGame& save, uint32_t town) {
	return (save.towns[town].memory.u32(0x2C8) & 0xE0) != 0;
}

}  // namespace

bool arsenal_mayor(const SaveGame& save, uint32_t town) {
	return save.towns[town].memory.u8(0x6F1) == save.globals.u32(global::kPlayer);
}

int32_t arsenal_cost(const SaveGame& save, uint32_t town, uint32_t type) {
	const TownMemory& t = save.towns[town].memory;
	const int32_t units = int32_t(t.u8(0x998 + type)) + t.u8(0x99C + type);
	return units * kArsenalCost[type] * 7;
}

ArsenalMessage arsenal_open_message(const SaveGame& save, uint32_t town) {
	return council_limits(save, town) ? ArsenalMessage::Authorised : ArsenalMessage::None;
}

bool arsenal_form(const SaveGame& save, uint32_t town, uint32_t type, ArsenalMessage& message, OperationBytes& op) {
	const TownMemory& t = save.towns[town].memory;
	message = ArsenalMessage::None;
	const int office = tick::facilities_detail::fn_5308a0(save, save.globals.u32(global::kPlayer), town);
	const int32_t weapons = office >= 0 ? save.offices[size_t(office)].memory.s32(0x54 + 4 * type) : t.s32(0x54 + 4 * type);
	if (weapons < 50) {
		message = ArsenalMessage::NoWeapons;
	}
	if (t.s32(0x2E4) < 20) {
		message = ArsenalMessage::NoBeggars;
	} else if (council_limits(save, town)) {
		int32_t units = 0;
		for (uint32_t k = 0; k < kArsenalTypes; ++k) {
			units += t.u8(0x998 + k) + t.u8(0x99C + k);
		}
		if (units >= int32_t(t.u16(0x9A0))) {
			message = ArsenalMessage::NoCouncil;
		}
	}
	if (message != ArsenalMessage::None) {
		return false;
	}
	op = militia_operation(0x41, type, town);
	if (council_limits(save, town)) {
		message = ArsenalMessage::Authorised;
	}
	return true;
}

OperationBytes arsenal_dismiss(const SaveGame& save, uint32_t town, uint32_t type, ArsenalMessage& message) {
	message = council_limits(save, town) ? ArsenalMessage::Authorised : ArsenalMessage::None;
	return militia_operation(0x40, type, town);
}

namespace {

constexpr uint32_t kWorldOrders = 0x6DE530;

}  // namespace

std::optional<TownOrder> town_order(const SaveGame& save, uint32_t k) {
	const GlobalMemory& g = save.globals;
	const uint32_t at = kWorldOrders + 12 * k;
	if (k >= 7 || g.u8(at) >= g.u32(global::kWorldTownCount)) {
		return std::nullopt;
	}
	TownOrder o;
	o.town = g.u8(at);
	o.ware = g.u8(at + 1);
	o.bonus = g.u8(at + 2) != 0;
	o.count = g.u8(at + 3);
	o.deadline = g.u32(at + 4);
	o.price = g.u32(at + 8);
	return o;
}

std::optional<PrinceDecree> prince_decree(const SaveGame& save, uint32_t town) {
	if (town >= save.sieges.size() || save.sieges[town].u8(0x1010) == 0xFF) {
		return std::nullopt;
	}
	const SiegeMemory& s = save.sieges[town];
	PrinceDecree d;
	for (uint32_t k = 0; k < 4; ++k) {
		d.wares[k] = s.u8(0x1010 + k);
	}
	const uint32_t player = save.globals.u32(global::kPlayer);
	for (const GameMemory<8>& node : save.siege_extras[town].list_101c) {
		if (node.u8(0x3) == uint8_t(player)) {
			return d;  // His deliveries promised (0x636590): no contract offered.
		}
	}
	uint8_t seen = 0;
	for (uint32_t k = 0; k < 4; ++k) {
		if (s.u8(0x1014 + k) != 3) {
			continue;
		}
		++seen;
		if ((int32_t(s.u8(0x4)) + save.globals.u8(0x6DE4A1)) % seen == 0) {
			d.contract = s.u8(0x1010 + k);
		}
	}
	return d;
}

bool accept_order(const SaveGame& save, uint32_t town, uint32_t k, OperationBytes& op) {
	const std::optional<TownOrder> order = town_order(save, k);
	const uint32_t player = save.globals.u32(global::kPlayer);
	if (!order || int16_t(save.merchants[player].memory.u16(0x22)) < 20) {
		return false;
	}
	const uint32_t client = (town + order->ware + order->count) & 3;
	const TownMemory& t = save.towns[order->town].memory;
	op = OperationBytes{};
	op[0] = 0x3D;
	op[4] = order->town;
	op[5] = order->ware;
	op[6] = t.u8(0x6E8 + client);
	op[7] = t.u8(0x6EC + client);
	const uint32_t deadline = order->deadline | 0xFF;
	std::memcpy(op.data() + 8, &deadline, 4);
	const uint16_t count = order->count;
	const uint16_t price = uint16_t(order->price);
	std::memcpy(op.data() + 0xC, &count, 2);
	std::memcpy(op.data() + 0xE, &count, 2);
	std::memcpy(op.data() + 0x10, &price, 2);
	op[0x12] = uint8_t(player);
	op[0x13] = order->bonus ? 1 : 0;
	return true;
}

}  // namespace p3::data
