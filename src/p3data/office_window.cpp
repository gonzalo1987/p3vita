#include "p3data/office_window.hpp"

#include <cmath>
#include <cstring>

#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kWorldRecordCount = 0x6DE4A6;  // u16
constexpr uint8_t kInBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};  // 0x672C14
constexpr int32_t kGuardsBase = 0x32;  // 0x6734E4
constexpr int32_t kGuardsStep = 0xC8;  // 0x6734E5

OperationBytes words(std::initializer_list<uint32_t> fields) {
	OperationBytes op{};
	size_t at = 0;
	for (uint32_t f : fields) {
		std::memcpy(op.data() + at, &f, 4);
		at += 4;
	}
	return op;
}

const OfficeMemory* office_at(const SaveGame& save, uint32_t office) {
	return office < save.globals.u16(global::kWorldOfficeCount) && office < save.offices.size()
		? &save.offices[office].memory
		: nullptr;
}

}  // namespace

OfficeStorage office_storage(const SaveGame& save, uint32_t office) {
	OfficeStorage s;
	const OfficeMemory* o = office_at(save, office);
	if (!o) {
		return s;
	}
	int32_t units = 0;
	for (size_t w = 0; w < 0x18; ++w) {
		const int32_t stock = o->s32(4 + 4 * w);
		units += stock - stock % (kInBarrels[w] ? 200 : 2000);
	}
	const int32_t extra = o->s32(0x2E8);
	s.used = int32_t((uint32_t(extra - extra % 200) + uint32_t(units) + 1800u) / 2000u);
	s.capacity = o->u16(0x2EC);
	s.over = uint32_t(s.used) > uint32_t(s.capacity) ? s.used - s.capacity : 0;
	s.guards = o->u8(0x2C7);
	s.max_guards = int16_t((s.capacity - kGuardsBase) / kGuardsStep + 1);
	if (uint32_t(s.used) > 50) {
		const int32_t per = int32_t(uint32_t(s.max_guards) * 1000u) / (s.guards + 1) - 1000;
		const int32_t x = int32_t(uint32_t(per) * uint32_t(s.used - 50)) / 200;
		s.risk = x <= 0 ? 0 : x <= 0x154 ? 1 : x > 0x29E ? 3 : 2;
	}
	return s;
}

OperationBytes guards_operation(uint32_t player, uint32_t town, uint32_t guards) {
	return words({0x63, player, town, guards});
}

bool has_administrator(const SaveGame& save, uint32_t office) {
	const OfficeMemory* o = office_at(save, office);
	return o && o->u16(0x2F2) < save.globals.u16(global::kCaptainCount);
}

std::vector<OperationBytes> administrator_operations(const SaveGame& save, uint32_t player, uint32_t town,
	uint32_t office) {
	std::vector<OperationBytes> ops;
	const bool dismiss = has_administrator(save, office);
	if (dismiss) {
		for (uint32_t w = 0; w < 20; ++w) {
			ops.push_back(administrator_order_operation(0, 0, office, w));
		}
	}
	ops.push_back(words({0x5E, player, town, dismiss ? 1u : 0u}));
	return ops;
}

OperationBytes administrator_order_operation(int32_t stock, int32_t price, uint32_t office, uint32_t ware) {
	return words({0x5B, uint32_t(stock), uint32_t(price), office, ware});
}

OperationBytes reserve_operation(uint32_t ware, uint32_t player, uint32_t town, bool on) {
	return words({0x66, ware, player, town, on ? 1u : 0u});
}

int32_t last_order_price(const SaveGame& save, uint32_t merchant, uint32_t ware, bool buying) {
	if (merchant >= save.merchants.size() || ware >= 0x18) {
		return 0;
	}
	return save.merchants[merchant].memory.u16((buying ? 0x5A8 : 0x5AA) + 4 * size_t(ware));
}

int house_class(uint8_t type) {
	const int k = int(type) - 0x18;
	return k >= 0 && k <= 2 ? 1 : k >= 3 && k <= 5 ? 2 : 0;
}

int house_rent(const SaveGame& save, uint32_t town, uint32_t merchant, uint8_t type) {
	const int office = tick::facilities_detail::fn_5308a0(save, merchant, town);
	if (office < 0) {
		return -1;
	}
	return save.offices[size_t(office)].memory.u8(0x2E4 + size_t(house_class(type)));
}

OperationBytes rent_operation(uint32_t player, uint32_t town, int level, int house_class) {
	return words({0x62, player, town, uint32_t(level), uint32_t(house_class)});
}

int business_share(const SaveGame& save, uint32_t town, uint32_t merchant, uint8_t type) {
	const int office = tick::facilities_detail::fn_5308a0(save, merchant, town);
	if (office < 0) {
		return -1;
	}
	const uint16_t count = save.globals.u16(kWorldRecordCount);
	for (uint16_t r = save.offices[size_t(office)].memory.u16(0x2CC); r < count && r < save.world_records.size();) {
		const GameMemory<0x14>& record = save.world_records[r];
		if (record.u8(6) == type) {
			return int(std::lround(double(record.f32(0x10)) * 100.0));
		}
		r = record.u16(8);
	}
	return -1;
}

OperationBytes business_share_operation(uint32_t merchant, uint32_t town, uint8_t type, int percent) {
	return words({0x5A, merchant, town, type, uint32_t(percent)});
}

}  // namespace p3::data
