#include "p3data/market_hall_window.hpp"

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

// 0x672C88: the business type making each ware (up to 3: none of the town's; 0xFF the whale oil).
constexpr std::array<uint8_t, kTradeWareCount> kWareBusiness = {0x09, 0x0A, 0x05, 0x06, 0x0D, 0x08, 0x00, 0x10, 0x0C,
	0x04, 0xFF, 0x0B, 0x07, 0x0A, 0x0F, 0x13, 0x0E, 0x14, 0x11, 0x12};

// __ftol (0x639C28): truncated toward zero, the low 32 bits.
int32_t ftol(double v) {
	return int32_t(uint32_t(int64_t(v)));
}

// A figure in the ware's unit: fild, stored as a float (fstp dword), divided by the float 200 or 2000.
double per_unit(int32_t value, int ware) {
	return double(float(value)) / double(float(ware_scale(kWares[size_t(ware)].unit)));
}

// Rounded as the updates 0x5DE960 and 0x5DEB70 do: + the double 0.5 (0x66A7D0), truncated.
int32_t rounded(double v) {
	return ftol(v + 0.5);
}

}  // namespace

bool market_hall_town_makes(const SaveGame& save, uint32_t town, int ware) {
	if (town >= save.towns.size() || ware < 0 || ware >= kTradeWareCount) {
		return false;
	}
	const TownMemory& t = save.towns[town].memory;
	const uint32_t type = kWareBusiness[size_t(ware)];
	if (type <= 3) {
		return false;
	}
	if (type < 0x15) {
		return t.u16(0x844 + 16 * type) != 0;
	}
	return type == 0xFF && t.u32(0x2CC) > 0;
}

std::array<MarketHallRow, kTradeWareCount> market_hall_rows(const SaveGame& save, uint32_t town, int page) {
	std::array<MarketHallRow, kTradeWareCount> rows{};
	if (town >= save.towns.size()) {
		return rows;
	}
	const TownMemory& t = save.towns[town].memory;
	const uint32_t offices = save.globals.u16(global::kWorldOfficeCount);
	for (int w = 0; w < kTradeWareCount; ++w) {
		MarketHallRow& r = rows[size_t(w)];
		const int32_t unit = ware_scale(kWares[size_t(w)].unit);
		const int32_t stock = t.s32(0x4 + 4 * size_t(w));
		if (page == 0) {
			// 0x5DE810: the stock truncated; the prices of one unit (0x52E430, 0x52E1D0 on the world's 0x6DE3D8).
			r.values[0] = ftol(per_unit(stock, w));
			r.values[1] = tick::game_buy_price(save, w, int(town), unit);
			r.values[2] = tick::game_sell_price(save, w, int(town), unit);
			r.shown = {r.values[0] != 0, r.values[1] != 0, r.values[2] != 0, false};
		} else if (page == 1) {
			// 0x5DE960: the offices' production · 7 summed as an int (0x5DE9DB), each office through 0x530400.
			uint32_t merchants = 0;
			int walked = 0;
			for (uint32_t k = t.u16(0x784); k < offices && k < save.offices.size() && walked < 0x10000; ++walked) {
				const OfficeMemory& o = save.offices[k].memory;
				merchants += o.u32(0xC4 + 4 * size_t(w)) * 7;
				k = o.u16(0x2CA);
			}
			r.values[0] = rounded(per_unit(stock, w));
			// The town's day not multiplied by 7 as the offices' are (a game bug, display only): behind fix_game_bugs, a week.
			const uint32_t town_made = t.u32(0xC4 + 4 * size_t(w)) * (fix_game_bugs() ? 7u : 1u);
			r.values[1] = rounded(per_unit(int32_t(town_made), w));
			r.values[2] = rounded(per_unit(int32_t(merchants), w));
			r.shown = {r.values[0] != 0, r.values[1] != 0 || market_hall_town_makes(save, town, w), r.values[2] != 0, false};
		} else {
			// 0x5DEB70: each quotient stored as a float (fstp dword) before its rounding.
			const float citizens = float(per_unit(int32_t(t.u32(0x310 + 4 * size_t(w)) * 7), w));
			const float businesses = float(per_unit(int32_t(t.u32(0x64 + 4 * size_t(w)) * 7), w));
			r.values[0] = rounded(double(float(per_unit(stock, w))));
			r.values[1] = rounded(double(citizens));
			r.values[2] = rounded(double(businesses));
			if (r.values[2] == 0 && businesses != 0.0f) {
				r.values[2] = 1;  // 0x5DECBE..0x5DECE5: something used shows at least 1.
			}
			r.values[3] = int32_t(uint32_t(r.values[1]) + uint32_t(r.values[2]));  // 0x5DE733 (in the drawing)
			r.shown = {r.values[0] != 0, r.values[1] != 0, r.values[2] != 0, r.values[3] != 0};
		}
	}
	return rows;
}

}  // namespace p3::data
