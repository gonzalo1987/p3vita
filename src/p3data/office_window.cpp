#include "p3data/office_window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
	s.other_goods = extra / 200;
	s.extension = o->u16(0x2EE);
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

namespace {

// 0x672C2C: the ware each business type makes.
constexpr uint8_t kBusinessWare[24] = {0x06, 0x06, 0x06, 0x14, 0x09, 0x02, 0x03, 0x0C, 0x05, 0x00, 0x01, 0x0B, 0x08,
	0x04, 0x10, 0x0E, 0x07, 0x12, 0x13, 0x0F, 0x11, 0x00, 0x00, 0x00};
constexpr int32_t kPieceWeight[6] = {1000, 1000, 2000, 2000, 2000, 1000};  // 0x672CB4 (u16)

// x87 products of a float and a float constant, stored as a float (exact in double, then rounded once).
float times(float a, float b) {
	return float(double(a) * double(b));
}

}  // namespace

std::array<OfficeGoodsRow, 20> office_goods(const SaveGame& save, uint32_t office) {
	std::array<OfficeGoodsRow, 20> rows{};
	if (office >= save.offices.size()) {
		return rows;
	}
	const OfficeMemory& o = save.offices[office].memory;
	for (uint32_t w = 0; w < 20; ++w) {
		OfficeGoodsRow& r = rows[w];
		const bool barrels = kInBarrels[w] != 0;
		r.stock = o.s32(0x4 + 4 * w) / (barrels ? 200 : 2000);
		r.production = times(times(float(o.s32(0xC4 + 4 * w)), 7.0f), barrels ? 0.005f : 0.0005f);
		r.consumption = times(float(o.s32(0x64 + 4 * w)), barrels ? 0.035f : 0.0035f);
		r.short_of = float(r.stock) < r.consumption;
	}
	const uint32_t records = std::min<uint32_t>(save.globals.u16(kWorldRecordCount), uint32_t(save.world_records.size()));
	uint32_t walked = 0;
	for (uint32_t i = o.u16(0x2CC); i < records && walked < records; i = save.world_records[i].u16(8), ++walked) {
		const GameMemory<0x14>& b = save.world_records[i];
		const uint8_t type = b.u8(6);
		const bool lacking = b.u16(4) < b.u16(0xA);
		const uint8_t ware = type < 24 ? kBusinessWare[type] : 0;
		if (ware < 20) {
			rows[ware].workers = lacking;
		}
		if (type == 5) {
			rows[0x11].workers = lacking;  // 0x5DC0E4: the hemp, though a fishery's second ware is the whale oil.
		} else if (type == 0xA) {
			rows[0x0D].workers = lacking;
		}
	}
	return rows;
}

std::array<int32_t, 11> office_weapons(const SaveGame& save, uint32_t office) {
	std::array<int32_t, 11> out{};
	if (office >= save.offices.size()) {
		return out;
	}
	const OfficeMemory& o = save.offices[office].memory;
	for (uint32_t k = 0; k < 4; ++k) {
		out[k] = o.s32(0x4 + 4 * (20 + k)) / 10;
	}
	for (uint32_t k = 0; k < 6; ++k) {
		out[4 + k] = o.s32(0x124 + 4 * k) / kPieceWeight[k];
	}
	out[10] = o.s32(0x2BC);
	return out;
}

OfficeBalance office_balance(const SaveGame& save, uint32_t office) {
	OfficeBalance b;
	if (office >= save.offices.size()) {
		return b;
	}
	const OfficeMemory& o = save.offices[office].memory;
	const uint32_t now = save.globals.u32(global::kWorldTick);
	const int32_t town_day = int32_t(((now - uint32_t(o.u8(0x2C6)) * 8 - 7) >> 8) % 7);
	const int32_t world_day = int32_t((now >> 8) % 7);
	// The sum so far turned into the week's forecast (0x5DC368..), by the days gone `d`.
	auto forecast = [](int32_t sum, int32_t last, int32_t d, bool plus_one) {
		if (d != 0) {
			return int32_t(uint32_t(sum / (plus_one ? d + 1 : d)) * 7u);
		}
		return sum == 0 ? last : int32_t(uint32_t(sum) * 7u);
	};
	static const uint32_t kPairs[6] = {0x418, 0x428, 0x438, 0x420, 0x430, 0x440};
	for (int k = 0; k < 6; ++k) {
		int32_t sum = o.s32(kPairs[k]);
		int32_t last = o.s32(kPairs[k] + 4);
		if (k == 3) {
			sum = int32_t(0u - uint32_t(sum));
			last = int32_t(0u - uint32_t(last));
		}
		b.last[size_t(k)] = last;
		if (k == 4) {
			b.forecast[4] = sum;
		} else {
			b.forecast[size_t(k)] = forecast(sum, last, k == 2 ? world_day : town_day, k != 2);
		}
	}
	b.manager = o.u16(0x2F2) < save.globals.u16(global::kCaptainCount) && o.u16(0x2F2) < save.captains.size();
	const int32_t wage = b.manager ? int32_t(save.captains[o.u16(0x2F2)].u16(0xC)) + int32_t(o.u16(0x2D2)) : 0;
	if (b.manager) {
		b.forecast[4] = int32_t(uint32_t(b.forecast[4]) + uint32_t(wage) * uint32_t(6 - town_day));
		b.stage = int(save.captains[o.u16(0x2F2)].u8(0xA)) / 43;
	}
	b.wage = b.manager ? wage : int(o.u16(0x2D2)) + 10;
	auto add = [](int32_t a, int32_t v) { return int32_t(uint32_t(a) + uint32_t(v)); };
	auto take = [](int32_t a, int32_t v) { return int32_t(uint32_t(a) - uint32_t(v)); };
	b.forecast_total = take(take(take(add(add(0, b.forecast[0]), b.forecast[3]), b.forecast[1]), b.forecast[2]), b.forecast[5]);
	b.last_total = take(take(take(add(add(0, b.last[0]), b.last[3]), b.last[1]), b.last[2]), b.last[5]);
	b.from_tick = now - uint32_t(world_day + 7) * 256;
	b.to_tick = now - uint32_t(world_day + 1) * 256;
	b.businesses = o.u16(0x2D2);
	b.dwellings = o.u16(0x2D4);
	b.workers = o.u16(0x448);
	b.most_workers = o.u16(0x44A);
	b.tenants = o.u16(0x2DE) + o.u16(0x2E0) + o.u16(0x2E2);
	b.most_tenants = o.u16(0x2D8) + o.u16(0x2DA) + o.u16(0x2DC);
	b.short_of_materials = o.u16(0x2) != 0;
	return b;
}

uint32_t home_move_fees(const SaveGame& save, uint32_t merchant) {
	if (merchant >= save.merchants.size()) {
		return 0;
	}
	const uint32_t fees = uint32_t(int64_t(double(save.merchants[merchant].memory.s32(0x46C)) * 0.1327));
	return fees > 1000000u ? 1000000u : fees;
}

OperationBytes home_move_operation(uint32_t player, uint32_t town, uint32_t fees) {
	return words({0x3E, player, town, fees});
}

std::string game_tenths(double value) {
	// 17 significant digits, as MSVC's _cftof gets them, then rounded half up at the tenths.
	char digits[40];
	std::snprintf(digits, sizeof digits, "%.16e", value);
	const bool negative = digits[0] == '-';
	const char* p = digits + (negative ? 1 : 0);
	std::string mantissa;
	for (; *p && *p != 'e'; ++p) {
		if (*p != '.') {
			mantissa += *p;
		}
	}
	const int exponent = std::atoi(p + 1);
	// The value is 0.mantissa · 10^(exponent + 1); its tenths: the first exponent + 2 digits.
	const int keep = exponent + 2;
	std::string whole;  // The digits up to the tenths.
	if (keep <= 0) {
		whole = "0";
		if (keep == 0 && mantissa[0] >= '5') {
			whole = "1";
		}
	} else {
		whole = mantissa.substr(0, size_t(keep));
		while (int(whole.size()) < keep) {
			whole += '0';
		}
		if (size_t(keep) < mantissa.size() && mantissa[size_t(keep)] >= '5') {
			int k = int(whole.size()) - 1;
			for (; k >= 0 && whole[size_t(k)] == '9'; --k) {
				whole[size_t(k)] = '0';
			}
			if (k < 0) {
				whole.insert(whole.begin(), '1');
			} else {
				whole[size_t(k)]++;
			}
		}
	}
	if (whole.size() < 2) {
		whole.insert(whole.begin(), 2 - whole.size(), '0');
	}
	return (negative ? "-" : "") + whole.substr(0, whole.size() - 1) + "." + whole.substr(whole.size() - 1);
}

}  // namespace p3::data
