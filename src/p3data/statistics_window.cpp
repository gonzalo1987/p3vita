#include "p3data/statistics_window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "p3data/game_texts.hpp"
#include "p3data/statistics.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

// The texts' slots in the English exe (game_text_addresses.inc pairs them with the Spanish exe's).
constexpr uint32_t kEntryPairs = 0x6C18C8;  // The entries' (button, title) pairs (0x46C060 copies them).
constexpr uint32_t kSupplyButton = 0x6B2D5C;  // «Goods Supply» and «Goods supply in %s» (strings, 0x46C213).
constexpr uint32_t kSupplyTitle = 0x6B2D6C;
constexpr uint32_t kPageTitles = 0x6C151C;
constexpr uint32_t kBalanceLabels = 0x6C21C0;
constexpr uint32_t kBalancePeriod = 0x6C2240;  // «from %i. %s %i to %i. %s %i»
constexpr uint32_t kBalanceDate = 0x6C2244;  // «on %i. %s %i»
constexpr uint32_t kMonthNames = 0x6C0D00;  // + 4 · month (1..12)
constexpr uint32_t kWealthLabels = 0x6C1BB0;  // 5
constexpr uint32_t kWealthNames = 0x6C1B3C;  // 4: the series' names (the buttons [+0xC28])
constexpr uint32_t kShipsLabels = 0x6C1C38;  // 7
constexpr uint32_t kShipsName = 0x6C1BD0;
constexpr uint32_t kProductionLabels = 0x6C1CD8;  // 6
constexpr uint32_t kProductionName = 0x6C1C60;

// The rings of a merchant's object (statistics.cpp) and the link of each record to the one before.
constexpr size_t kRecord = 0x70;
constexpr size_t kDays = 0x8;
constexpr size_t kWeeks = 0xE08;
constexpr size_t kMonths = 0x1888;
constexpr size_t kLink = 0x6C;
constexpr size_t kObjectSize = 0x2310;

// 0x672D94 + 2 · month: the month's days (0 January); 0x672D92 + 2 · month the month before's (0 for January, which
// the code takes apart).
constexpr uint16_t kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

uint16_t days_before(int month) {
	return month >= 1 && month <= 12 ? kMonthDays[month - 1] : 0;
}

// A record of the object (an offset), read safely.
struct Records {
	const GameMemory<kObjectSize>& m;
	uint32_t u32(size_t at) const { return at + 4 <= kObjectSize ? m.u32(at) : 0; }
	float f32(size_t at) const { return at + 4 <= kObjectSize ? m.f32(at) : 0.0f; }
};

std::string number(int32_t v) {
	return std::to_string(v);
}

}  // namespace

std::string game_format_float(double value) {
	if (std::isnan(value)) {
		return "-1.#IND";
	}
	if (std::isinf(value)) {
		return value < 0 ? "-1.#INF" : "1.#INF";
	}
	const double magnitude = std::fabs(value);
	double whole = std::floor(magnitude);
	if (magnitude - whole >= 0.5) {  // Exact: the fraction of a double below 2^52.
		whole += 1.0;
	}
	char text[400];
	std::snprintf(text, sizeof(text), "%.0f", whole);
	return std::signbit(value) ? "-" + std::string(text) : std::string(text);
}

int32_t game_ftol(double value) {
	// __ftol (0x639C28) truncates with a 64-bit fistp and its callers take the low half: out of the 64-bit range (and
	// NaN) the «integer indefinite» 0x8000000000000000, whose low half is 0.
	if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0)) {
		return 0;
	}
	return int32_t(uint32_t(uint64_t(int64_t(value))));
}

const std::string& statistics_button(const GameTexts& texts, int entry) {
	if (entry == kStatisticsSupply) {
		return texts.raw_at(kSupplyButton);
	}
	return texts.raw_at(kEntryPairs + 8 * uint32_t(entry < kStatisticsSupply ? entry : entry - 1));
}

const std::string& statistics_title(const GameTexts& texts, int entry) {
	if (entry == kStatisticsSupply) {
		return texts.raw_at(kSupplyTitle);
	}
	return texts.raw_at(kEntryPairs + 4 + 8 * uint32_t(entry < kStatisticsSupply ? entry : entry - 1));
}

const std::string& statistics_page_title(const GameTexts& texts, int page) {
	return texts.raw_at(kPageTitles + 4 * uint32_t(page));
}

const std::string& balance_label(const GameTexts& texts, int row) {
	static const std::string kNone;
	// The row 2's slot points into the globals (0x6DE3C7): nothing to read, an empty label.
	return row == 2 || row < 0 || row >= kBalanceRows ? kNone : texts.raw_at(kBalanceLabels + 4 * uint32_t(row));
}

const MerchantStatistics* merchant_statistics(const SaveGame& save, uint32_t merchant) {
	for (const MerchantStatistics& object : save.statistics.merchants) {
		if (object.memory.u16(0x0) == merchant) {
			return &object;
		}
	}
	return nullptr;
}

StatisticsBalance statistics_balance(const SaveGame& save, const GameTexts& texts, uint32_t merchant, int period) {
	StatisticsBalance out;
	const MerchantStatistics* object = merchant_statistics(save, merchant);
	if (!object) {
		return out;
	}
	out.found = true;
	const Records r{object->memory};
	// The ring of the period: the days (cur = u16 [+0x2], the 30 records from cur + 3 to cur) or the months (cur =
	// u8 [+0x5], 3, 6 or 12 records up to cur).
	size_t base = kDays;
	int cur = object->memory.u16(0x2);
	int s = (cur + 3) & 31;
	int e = (cur + 1) & 31;
	if (period != 0) {
		base = kMonths;
		cur = object->memory.u8(0x5);
		e = (cur + 1) % 24;
		s = (e + (period == 1 ? 21 : period == 2 ? 18 : 12)) % 24;
	}
	auto at = [&](int k, size_t field) { return base + kRecord * size_t(k) + field; };
	auto next = [&](int k) { return period == 0 ? (k + 1) & 31 : (k + 1 >= 24 ? 0 : k + 1); };
	auto sum = [&](size_t field) {
		uint32_t total = 0;
		for (int k = s; k != e; k = next(k)) {
			total += r.u32(at(k, field));
		}
		return int32_t(total);
	};
	auto sum2 = [&](size_t a, size_t b) {
		uint32_t total = 0;
		for (int k = s; k != e; k = next(k)) {
			total += r.u32(at(k, a)) + r.u32(at(k, b));
		}
		return int32_t(total);
	};
	// fld the sum; fadd the record's float; fstp: each step rounded to a float.
	auto fsum = [&](size_t field) {
		float total = 0.0f;
		for (int k = s; k != e; k = next(k)) {
			total = float(double(total) + double(r.f32(at(k, field))));
		}
		return total;
	};
	std::array<std::string, 20>& t = out.texts;
	t[0] = game_format_float(r.f32(at(cur, 0x0)));
	t[1] = game_format_float(r.f32(at(s, 0x0)));
	t[2] = number(sum(0x28));
	t[3] = number(sum(0x2C));
	const int32_t rental = sum(0x20);
	t[4] = number(rental);
	const float sold = fsum(0x14);
	t[5] = game_format_float(sold);
	uint32_t income = uint32_t(game_ftol(sold)) + uint32_t(rental);
	for (size_t field : {0x3C, 0x30, 0x34}) {
		const int32_t v = sum(field);
		t[field == 0x3C ? 6 : field == 0x30 ? 7 : 8] = number(v);
		income += uint32_t(v);
	}
	t[9] = number(int32_t(income));
	const int32_t wages = sum2(0xC, 0x8);
	t[10] = number(wages);
	uint32_t spent = uint32_t(wages);
	const int32_t repairs = sum(0x24);
	t[11] = number(repairs);
	spent += uint32_t(repairs);
	const float bought = fsum(0x18);
	t[12] = game_format_float(bought);
	spent += uint32_t(game_ftol(bought));
	for (size_t field : {0x1C, 0x40}) {
		const int32_t v = sum(field);
		t[field == 0x1C ? 13 : 14] = number(v);
		spent += uint32_t(v);
	}
	const int32_t other = sum2(0x44, 0x38);
	t[15] = number(other);
	spent += uint32_t(other);
	t[16] = number(int32_t(spent));
	t[17] = number(int32_t(income - spent));

	// The dates (0x4A34FF and 0x4A3A93): the months 1..12 for their names.
	const GlobalMemory& g = save.globals;
	const int day = g.u8(global::kWorldDay);
	const int month = g.u8(global::kWorldMonth);  // 0 January
	const int year = g.u16(global::kWorldYear);
	int sd = 1;
	int sm = 0;
	int sy = year;
	int ed = 0;
	int em = 0;
	int ey = year;
	if (period == 0) {
		// From 30 days back to yesterday.
		sd = day - 30;
		sm = month;
		while (sd < 1) {
			if (sm > 0) {
				--sm;
				sd += kMonthDays[sm];
			} else {
				sd += 31;
				sm = 11;
				--sy;
			}
		}
		++sm;
		ed = day - 1;
		em = month;
		if (ed >= 1) {
			++em;
		} else if (em > 0) {
			ed += days_before(em);  // 0x672D92 + 2 · month: the month before's (em, 1-based, names it)
		} else {
			ed += 31;
			em = 12;
			--ey;
		}
	} else {
		// From the first of the month 2, 5 or 11 months before the last one to the last one's last day.
		if (month > 0) {
			ed = days_before(month);
			em = month;
		} else {
			ed = 31;
			em = 12;
			--ey;
		}
		sm = em - (period == 1 ? 2 : period == 2 ? 5 : 11);
		sy = ey;
		if (sm < 1) {
			sy = ey - 1;
			sm += 12;
		}
	}
	auto month_name = [&](int m) -> const std::string& { return texts.raw_at(kMonthNames + 4 * uint32_t(m)); };
	out.period = GameTexts::sprintf_raw(texts.raw_at(kBalancePeriod), {std::to_string(sd), month_name(sm),
		std::to_string(sy), std::to_string(ed), month_name(em), std::to_string(ey)});
	t[18] = GameTexts::sprintf_raw(texts.raw_at(kBalanceDate), {std::to_string(sd), month_name(sm), std::to_string(sy)});
	t[19] = GameTexts::sprintf_raw(texts.raw_at(kBalanceDate), {std::to_string(ed), month_name(em), std::to_string(ey)});
	return out;
}

namespace {

// 0x505A10 (on the ships 0x6DD7A0): the merchant's ships from `first` along their [+0x4], but the sunk ones (0x11)
// and 0xE: how many have a captain, their sailors, their workload (the average of ftol(f[+0x12C] · 100 / u16
// [+0x130])) and their hulls' condition (the health [+0x18] of those above 0 over their max health [+0x14] / 100).
struct ShipsSummary {
	int32_t captains = 0;
	int32_t sailors = 0;
	int32_t workload = 0;
	int32_t condition = 0;
};

ShipsSummary fn_505a10(const SaveGame& save, uint16_t first) {
	ShipsSummary out;
	const GlobalMemory& g = save.globals;
	const uint16_t ships = g.u16(global::kShipCount);
	int32_t n = 0;
	uint32_t workload = 0;
	uint32_t health = 0;
	uint32_t max_health = 0;
	size_t steps = 0;
	for (uint16_t k = first; k < ships && k < save.ships.size() && steps++ <= save.ships.size();) {
		const ShipMemory& s = save.ships[k].memory;
		k = s.u16(0x4);
		const uint16_t state = s.u16(0x134);
		if (state == 0x11 || state == 0xE) {
			continue;
		}
		++n;
		if (s.u16(0x42) < g.u16(global::kCaptainCount)) {
			++out.captains;
		}
		out.sailors = int32_t(uint32_t(out.sailors) + s.u16(0x40));
		// fld; fmul 100.0f; fild the u16 into a float; fdiv; __ftol.
		workload += uint32_t(game_ftol(double(s.f32(0x12C)) * 100.0 / double(float(s.u16(0x130)))));
		if (s.s32(0x18) > 0) {
			health += uint32_t(s.s32(0x18));
			max_health += uint32_t(s.s32(0x14));
		}
	}
	if (n < 1) {
		return out;
	}
	out.workload = int32_t(workload) / n;
	const int32_t hundredth = int32_t(max_health) / 100;
	// The game divides by 0 (a crash) if the ships' max health is under 100 together: not with its ships.
	out.condition = hundredth != 0 ? int32_t(health) / hundredth : 0;
	return out;
}

}  // namespace

StatisticsGraph statistics_graph(const SaveGame& save, uint32_t merchant, int entry, int period) {
	StatisticsGraph out;
	const GlobalMemory& g = save.globals;
	if (int32_t(merchant) >= int32_t(g.u16(global::kWorldMerchantCount)) || merchant >= save.merchants.size()) {
		return out;
	}
	const MerchantMemory& M = save.merchants[merchant].memory;
	out.series = 4;  // The 4 series' headers, before the object is looked for.
	const MerchantStatistics* object = merchant_statistics(save, merchant);
	if (!object) {
		return out;
	}
	const Records r{object->memory};
	const size_t R = kDays + kRecord * object->memory.u16(0x2);  // The current day's record.
	// Where the points start and how many.
	size_t start = R;
	bool years = false;
	uint32_t available = 0;
	switch (period) {
	case 0:
		out.points = 30;
		available = g.u16(kStatistics + 0x0);
		break;
	case 1:
		start = kWeeks + kRecord * object->memory.u8(0x4);
		out.points = 24;
		available = g.u16(kStatistics + 0x2);
		break;
	case 2:
		start = kMonths + kRecord * object->memory.u8(0x5);
		out.points = 24;
		available = g.u16(kStatistics + 0x4);
		break;
	default:
		years = true;
		out.points = std::min<uint32_t>(object->memory.u16(0x6), 0x80);
		break;
	}
	if (!years) {
		out.missing = available < out.points ? out.points - available : 0;
	}
	// The records, newest first, `points` of them along the links [+0x6C] (the rings go round).
	std::vector<std::array<float, 4>> walked;
	walked.reserve(out.points);
	auto values = [&](const auto& f32, const auto& s32) {
		std::array<float, 4> v{};
		auto positive = [](float x) { return x > 0.0f ? x : 0.0f; };
		if (entry == kStatisticsWealth) {
			v[0] = positive(f32(0x0));
			v[1] = positive(f32(0x10));
			// fild the sum of the two ints into a float, then fadd the two floats and fstp.
			const float sum = float(int32_t(uint32_t(s32(0x3C)) + uint32_t(s32(0x40))));
			v[2] = positive(float(double(double(sum) + double(f32(0x18))) + double(f32(0x14))));
			v[3] = positive(f32(0x4));
		} else if (entry == kStatisticsShips) {
			v[0] = float(s32(0x50));  // fild; fstp
		} else {
			v[0] = f32(0x48);
		}
		return v;
	};
	if (years) {
		for (uint32_t i = 0; i < out.points && i < object->years.size(); ++i) {
			const GameMemory<0x70>& y = object->years[i];
			walked.push_back(values([&](size_t o) { return y.f32(o); }, [&](size_t o) { return y.s32(o); }));
		}
	} else {
		size_t at = start;
		for (uint32_t i = 0; i < out.points; ++i) {
			walked.push_back(values([&](size_t o) { return r.f32(at + o); }, [&](size_t o) { return int32_t(r.u32(at + o)); }));
			at = r.u32(at + kLink);
		}
	}
	const uint32_t series = entry == kStatisticsWealth ? 4 : 1;
	for (uint32_t s = 0; s < series; ++s) {
		out.values[s].assign(out.points, 0.0f);
		for (size_t i = 0; i < walked.size(); ++i) {
			out.values[s][out.points - 1 - i] = walked[i][s];
		}
	}
	out.series = series;
	auto text = [&](std::string value, uint8_t kind) {
		out.texts.push_back(std::move(value));
		out.kinds.push_back(kind);
	};
	auto labels = [&](uint32_t table, uint32_t count) {
		for (uint32_t k = 0; k < count; ++k) {
			out.labels.push_back(table + 4 * k);
		}
	};
	if (entry == kStatisticsProduction) {
		text(game_format_float(double(r.f32(R + 0x48)) / 200.0), 2);  // [0x66A91C] = 200.0: in loads
		text(game_format_float(r.f32(R + 0x4C)), 1);
		text(number(object->memory.u16(R + 0x56)), 5);
		text(number(int32_t(r.u32(R + 0x1C))), 1);
		text(number(int32_t(r.u32(R + 0x5C))), 5);
		text(number(int32_t(r.u32(R + 0x8))), 1);
		labels(kProductionLabels, 6);
		out.names[0] = kProductionName;
	} else if (entry == kStatisticsShips) {
		const ShipsSummary ships = fn_505a10(save, M.u16(0xE));
		text(number(M.u16(0x478)), 5);
		text(number(M.s32(0x470) / 2000), 3);
		text(number(ships.sailors), 5);
		text(number(ships.captains), 5);
		text(number(int32_t(r.u32(R + 0xC))), 1);
		text(number(ships.workload), 4);
		text(number(ships.condition), 4);
		labels(kShipsLabels, 7);
		out.names[0] = kShipsName;
	} else if (entry == kStatisticsWealth) {
		const float value = r.f32(R + 0x0);
		text(value > 0.0f ? game_format_float(value) : std::string("0"), 1);
		text(number(object->memory.u16(R + 0x54)), 5);
		text(number(int32_t(r.u32(R + 0x60) + r.u32(R + 0x5C))), 5);
		text(number(object->memory.u16(R + 0x5A)), 5);
		text(number(object->memory.u16(R + 0x56)), 5);
		labels(kWealthLabels, 5);
		for (uint32_t k = 0; k < 4; ++k) {
			out.names[k] = kWealthNames + 4 * k;
		}
	}
	return out;
}

std::vector<int> statistics_supply_towns(const SaveGame& save, uint32_t merchant) {
	std::vector<int> out;
	const uint32_t towns = save.globals.u32(global::kWorldTownCount);
	for (uint32_t t = 0; t < towns && t < save.towns.size(); ++t) {
		if (tick::facilities_detail::fn_5308a0(save, merchant, t) >= 0) {
			out.push_back(int(t));
		}
	}
	return out;
}

std::vector<SupplyRow> statistics_supply(const SaveGame& save, uint32_t merchant, uint32_t town) {
	std::vector<SupplyRow> out;
	const int office = tick::facilities_detail::fn_5308a0(save, merchant, town);
	if (office < 0 || size_t(office) >= save.offices.size() || town >= save.towns.size()) {
		return out;
	}
	const OfficeMemory& o = save.offices[size_t(office)].memory;
	const TownMemory& t = save.towns[town].memory;
	static constexpr uint8_t kInBarrels[20] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0};
	for (int w = 0; w < 20; ++w) {
		SupplyRow row;
		row.ware = w;
		row.office = o.s32(0x4 + 4 * size_t(w));
		row.needed = o.s32(0x64 + 4 * size_t(w));
		row.town = t.s32(0x4 + 4 * size_t(w));
		row.price = tick::game_buy_price(save, w, int(town), kInBarrels[w] ? 200 : 2000);
		out.push_back(row);
	}
	return out;
}

}  // namespace p3::data
