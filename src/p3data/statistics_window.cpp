#include "p3data/statistics_window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "p3data/game_fixes.hpp"
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

// A Latin-1 letter with an accent as its plain letter (ö as o, Ñ as N...), for the alphabetical order of the fix.
uint8_t plain_letter(uint8_t c) {
	static constexpr char kUpper[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs";  // 0xC0..0xDF
	static constexpr char kLower[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";  // 0xE0..0xFF
	if (c >= 0xC0 && c <= 0xDF) {
		return c == 0xD7 ? c : uint8_t(kUpper[c - 0xC0]);
	}
	if (c >= 0xE0) {
		return c == 0xF7 ? c : uint8_t(kLower[c - 0xE0]);
	}
	return c;
}

// The window's name orders: the game compares the bytes as signed chars (the lists, 0x4720D0, 0x471FF0, 0x471F10:
// the letters with an accent before every other) or with strcmp (the residents' table: after every other); with
// fix_game_bugs(), as a dictionary does, each accented letter as its plain letter (then the bytes, to break ties).
int compare_names(const std::string& a, const std::string& b, bool game_signed) {
	const bool fixed = fix_game_bugs();
	for (int pass = fixed ? 0 : 1; pass < 2; ++pass) {
		for (size_t k = 0;; ++k) {
			uint8_t x = k < a.size() ? uint8_t(a[k]) : 0;
			uint8_t y = k < b.size() ? uint8_t(b[k]) : 0;
			if (pass == 0) {
				x = plain_letter(x);
				y = plain_letter(y);
			}
			if (x != y) {
				if (pass == 1 && game_signed && !fixed) {
					return static_cast<int8_t>(x) < static_cast<int8_t>(y) ? -1 : 1;
				}
				return x < y ? -1 : 1;
			}
			if (x == 0) {
				break;
			}
		}
	}
	return 0;
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
		out.colours[s] = kStatisticsColours[s];
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

namespace {

// Each entry's kind of graph (0x66FC0C): its scale's floor and its y axis' unit and step.
constexpr uint8_t kEntryKind[kStatisticsEntries] = {1, 5, 5, 5, 0, 0, 5, 1, 3, 0, 0, 2, 2, 0, 0, 6, 2, 0, 6, 0, 0, 0, 1,
	1, 3, 4, 4, 6};

uint8_t entry_kind(int entry) {
	return entry >= 0 && entry < kStatisticsEntries ? kEntryKind[entry] : 0xFF;
}

}  // namespace

float statistics_scale_floor(int entry) {
	const uint8_t kind = entry_kind(entry);
	if (kind == 0) {
		return 2000.0f;
	}
	if (kind == 2) {
		return 1000.0f;
	}
	if (entry == 1) {
		return 110.0f;
	}
	if (entry == 2) {
		return 150.0f;
	}
	return 1.0f;
}

// --- The competition ---

std::string statistics_first_name(const SaveGame& save, uint32_t merchant) {
	if (merchant >= save.merchants.size()) {
		return {};
	}
	const MerchantMemory& m = save.merchants[merchant].memory;
	return game_first_name(save, m.u8(0x1A) ? (m.u8(0x1C) & 0x7Fu) : (m.u8(0x1C) | 0x80u));
}

std::string statistics_family_name(const SaveGame& save, uint32_t merchant) {
	return merchant < save.merchants.size() ? game_family_name(save, save.merchants[merchant].memory.u8(0x1D))
											: std::string();
}

void statistics_default_merchants(const SaveGame& save, StatisticsMerchants& slots) {
	const GlobalMemory& g = save.globals;
	const uint16_t count = g.u16(global::kWorldMerchantCount);
	if (slots[0] >= count) {
		slots[0] = uint8_t(g.u32(global::kPlayer));
	}
	for (size_t k = 1; k < 4; ++k) {
		if (slots[k] < count) {
			continue;
		}
		float best = -1000000.0f;  // 0xC9742400
		for (const MerchantStatistics& object : save.statistics.merchants) {
			const GameMemory<kObjectSize>& o = object.memory;
			const float value = o.f32(kDays + kRecord * o.u16(0x2));  // Today's company value.
			if (!(value > best)) {
				continue;
			}
			if (fix_game_bugs()) {
				// A merchant already shown is skipped: the slot keeps the best so far (the game empties it, 0xFF, when
				// the shown one comes after the best: with the player the richest, every empty slot ends empty).
				bool shown = false;
				for (size_t j = 0; j < 4; ++j) {
					shown = shown || (j != k && slots[j] == uint8_t(o.u16(0x0)));
				}
				if (shown) {
					continue;
				}
			}
			slots[k] = uint8_t(o.u16(0x0));
			for (size_t j = 0; j < 4; ++j) {
				if (slots[j] == slots[k] && j != k) {
					slots[k] = 0xFF;
				}
			}
			if (slots[k] == o.u16(0x0)) {
				best = value;
			}
		}
	}
}

StatisticsGraph statistics_competition(const SaveGame& save, const GameTexts& texts, const StatisticsMerchants& slots,
	int entry, int period) {
	StatisticsGraph out;
	const GlobalMemory& g = save.globals;
	const uint16_t count = g.u16(global::kWorldMerchantCount);
	period = statistics_competition_period(period);
	// The points: 31 days (not reached: the period 0 is gone), 24 weeks or months, or the years recorded (0x54E250
	// counts the towns' years list, and keeps the count in [0x6DFC98]), at most 128 and at least 2.
	uint32_t available = 0;
	switch (period) {
	case 0:
		out.points = 31;
		available = g.u16(kStatistics + 0x0);
		break;
	case 1:
		out.points = 24;
		available = g.u16(kStatistics + 0x2);
		break;
	case 2:
		out.points = 24;
		available = g.u16(kStatistics + 0x4);
		break;
	default:
		out.points = std::min<uint32_t>(uint16_t(save.statistics.towns[3].size()), 0x80);
		available = out.points;
		if (out.points < 2) {
			return out;
		}
		break;
	}
	out.missing = available < out.points ? out.points - available : 0;
	uint32_t shown = 0;
	for (uint8_t slot : slots) {
		shown += slot < count ? 1 : 0;
	}
	if (shown == 0) {
		return out;
	}
	out.series = shown;
	for (std::string& button : out.buttons) {
		button = " ";
	}

	// The averages over every statistics object, from its current week's record.
	int32_t sums[6] = {};
	for (const MerchantStatistics& object : save.statistics.merchants) {
		const Records r{object.memory};
		const size_t W = kWeeks + kRecord * object.memory.u8(0x4);
		++sums[0];
		// fld; fmul [0x66FD58] = 1/64; __ftol: the value in 64ths (shifted back after the average).
		sums[1] = int32_t(uint32_t(sums[1]) + uint32_t(game_ftol(double(r.f32(W + 0x0)) * 0.015625)));
		sums[2] = int32_t(uint32_t(sums[2]) + object.memory.u16(W + 0x54));
		sums[3] = int32_t(uint32_t(sums[3]) + uint32_t(int32_t(r.u32(W + 0x50)) >> 3));
		sums[4] = int32_t(uint32_t(sums[4]) + object.memory.u16(W + 0x5A));
		sums[5] = int32_t(uint32_t(sums[5]) + object.memory.u16(W + 0x56));
	}
	if (sums[0] > 0) {
		for (int k = 1; k < 6; ++k) {
			sums[k] /= sums[0];
		}
	}
	sums[1] = int32_t(uint32_t(sums[1]) << 6);
	sums[3] = int32_t(uint32_t(sums[3]) << 3) / 2000;
	constexpr uint32_t kCompetitionTexts = 0x6C1FCC;  // (label, format) × 6
	for (uint32_t k = 0; k < 6; ++k) {
		out.texts.push_back(GameTexts::sprintf_raw(texts.raw_at(kCompetitionTexts + 4 + 8 * k), {number(sums[k])}));
		out.kinds.push_back(k == 1 ? 1 : 0);  // [+0xF5] = 1: the coins before the company value
		out.labels.push_back(kCompetitionTexts + 8 * k);
	}

	// The series: one per shown merchant, in his slot's colour, the oldest point first.
	uint32_t s = 0;
	for (size_t k = 0; k < 4 && s < shown; ++k) {
		const uint8_t merchant = slots[k];
		if (merchant >= count) {
			continue;
		}
		out.colours[s] = kStatisticsColours[k];
		out.merchants[s] = merchant;
		const std::string first = statistics_first_name(save, merchant);
		const std::string family = statistics_family_name(save, merchant);
		out.buttons[k] = first.empty() ? family : std::string(1, first[0]) + ". " + family;
		std::vector<float>& values = out.values[s];
		values.assign(out.points, 0.0f);
		const MerchantStatistics* object = merchant_statistics(save, merchant);
		if (object) {
			const Records r{object->memory};
			auto point = [&](const auto& f32, const auto& s32) -> float {
				switch (entry) {
				case kStatisticsCompanies: {
					const float v = f32(0x0);
					return v > 0.0f ? v : 0.0f;
				}
				case kStatisticsCapacity:
					return float(s32(0x50));  // fild; fstp
				case kStatisticsPopularity: {
					const float v = f32(0x68);
					return v > 0.0f ? v : 0.0f;
				}
				default:
					return 0.0f;
				}
			};
			if (period == 3) {
				// From the years' head along their links, at most the object's own years ([+0x6]); the older points 0.
				const uint32_t n = std::min<uint32_t>(object->memory.u16(0x6), out.points);
				for (uint32_t i = 0; i < n && i < object->years.size(); ++i) {
					const GameMemory<0x70>& y = object->years[i];
					values[out.points - 1 - i] = point([&](size_t o) { return y.f32(o); }, [&](size_t o) { return y.s32(o); });
				}
			} else {
				size_t at = period == 0 ? kDays + kRecord * object->memory.u16(0x2)
					: period == 1		? kWeeks + kRecord * object->memory.u8(0x4)
										: kMonths + kRecord * object->memory.u8(0x5);
				for (uint32_t i = 0; i < out.points; ++i) {
					values[out.points - 1 - i] =
						point([&](size_t o) { return r.f32(at + o); }, [&](size_t o) { return int32_t(r.u32(at + o)); });
					at = r.u32(at + kLink);
				}
			}
		}
		++s;
	}
	return out;
}

StatisticsChoice statistics_competition_choice(const SaveGame& save, const StatisticsMerchants& slots, int slot,
	int entry) {
	StatisticsChoice out;
	if (entry < kStatisticsCompanies || entry > kStatisticsPopularity) {
		return out;
	}
	const uint16_t count = save.globals.u16(global::kWorldMerchantCount);
	out.kind = entry - kStatisticsCompanies + 4;
	for (int m = int(count) - 1; m >= 0; --m) {
		if (size_t(m) >= save.merchants.size()) {
			continue;
		}
		const MerchantMemory& M = save.merchants[size_t(m)].memory;
		if (M.u8(0x8) & 4) {
			continue;
		}
		// Shown in another slot (the first slot holding him decides): not listed.
		int j = 0;
		while (j < 4 && slots[size_t(j)] != m) {
			++j;
		}
		if (j < 4 && j != slot) {
			if (entry == kStatisticsPopularity) {
				out.none = true;
			}
			continue;
		}
		int32_t key = 0;
		if (entry == kStatisticsCompanies) {
			key = M.s32(0x46C);  // The float's bits.
		} else if (entry == kStatisticsCapacity) {
			key = M.s32(0x470);
		} else if (const MerchantStatistics* object = merchant_statistics(save, uint32_t(m))) {
			key = game_ftol(object->memory.f32(0xE70));
		}
		out.keys.push_back(key);
		out.merchants.push_back(uint8_t(m));
	}
	if (entry != kStatisticsPopularity) {
		for (int j = 0; j < 4; ++j) {
			if (j != slot && slots[size_t(j)] < count) {
				out.none = true;
			}
		}
	}
	return out;
}

void sort_choice_by_name(const SaveGame& save, StatisticsChoice& choice) {
	// strcmp with signed chars (with the fix, as a dictionary): a < b, a > b or equal.
	auto compare = [](const std::string& a, const std::string& b) { return compare_names(a, b, true); };
	const size_t n = choice.merchants.size();
	if (n == 0) {
		return;
	}
	for (size_t pass = n - 1; pass > 0; --pass) {
		bool swapped = false;
		for (size_t i = 0; i < pass; ++i) {
			const uint32_t a = choice.merchants[i];
			const uint32_t b = choice.merchants[i + 1];
			int c = compare(statistics_family_name(save, a), statistics_family_name(save, b));
			if (c == 0) {
				c = compare(statistics_first_name(save, a), statistics_first_name(save, b));
			}
			if (c > 0) {
				if (i + 1 < choice.keys.size()) {
					std::swap(choice.keys[i], choice.keys[i + 1]);
				}
				std::swap(choice.merchants[i], choice.merchants[i + 1]);
				swapped = true;
			}
		}
		if (!swapped) {
			break;
		}
	}
}

void sort_choice_by_key(StatisticsChoice& choice) {
	const size_t n = std::min(choice.merchants.size(), choice.keys.size());
	if (n == 0) {
		return;
	}
	// The company value's key is its float's bits, compared as ints: the values below 0 come in the reverse order
	// (−5000 before −100). With fix_game_bugs(), compared as the floats they are.
	const bool floats = fix_game_bugs() && choice.mode == 2 && choice.kind == 4;
	auto smaller = [&](int32_t a, int32_t b) {
		if (!floats) {
			return a < b;
		}
		float x = 0.0f;
		float y = 0.0f;
		std::memcpy(&x, &a, 4);
		std::memcpy(&y, &b, 4);
		return x < y;
	};
	for (size_t pass = n - 1; pass > 0; --pass) {
		bool swapped = false;
		for (size_t i = 0; i < pass; ++i) {
			if (smaller(choice.keys[i], choice.keys[i + 1])) {
				std::swap(choice.keys[i], choice.keys[i + 1]);
				std::swap(choice.merchants[i], choice.merchants[i + 1]);
				swapped = true;
			}
		}
		if (!swapped) {
			break;
		}
	}
}

// --- The towns' pages ---

namespace {

constexpr uint32_t kTownIds = 0x6DE4B8;  // u8 per town index: its number (StadtDaten.ini)
constexpr uint32_t kWareNames = 0x6C0BB0;  // + 4 · ware (24)
constexpr uint32_t kResidentsText = 0x6C1E90;  // «%.0f inhabitants»
constexpr uint32_t kComparisonTexts = 0x6C1E88;  // + 20 · (entry − 7): the largest's label, the smallest's, 3 formats
constexpr uint32_t kClassTexts = 0x6C1D38;  // + 8k: the four classes' labels (rich, wealthy, poor, beggars) and «%i»
constexpr uint32_t kClassNames = 0x6C1AF8;  // + 4k: the four classes' names, the town data's series (its buttons)
constexpr uint32_t kZeroLine = 0xFFFFFFFF;  // The colour of the town data's zero line.
// 0x672C14: the wares counted in barrels (the others, below 20, in loads; 20..23 the weapons, in pieces).
constexpr uint8_t kBarrelWare[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};

// The bubble sorts of the list (0x4720D0, 0x471FF0, 0x471F10): a swap when the name is larger, the keys swapped too.
template <typename Compare>
void bubble_sort(StatisticsChoice& choice, Compare larger) {
	const size_t n = choice.merchants.size();
	if (n == 0) {
		return;
	}
	for (size_t pass = n - 1; pass > 0; --pass) {
		bool swapped = false;
		for (size_t i = 0; i < pass; ++i) {
			if (larger(choice.merchants[i], choice.merchants[i + 1])) {
				if (i + 1 < choice.keys.size()) {
					std::swap(choice.keys[i], choice.keys[i + 1]);
				}
				std::swap(choice.merchants[i], choice.merchants[i + 1]);
				swapped = true;
			}
		}
		if (!swapped) {
			break;
		}
	}
}

// The towns' records of the period's list (the days, weeks, months or years of 0x6DFC90, newest first) and how
// many points a graph has, how many are missing (not recorded yet).
struct TownPeriod {
	const std::vector<TownStatistics>* list = nullptr;
	uint32_t points = 0;
	uint32_t missing = 0;
	bool stop = false;  // The years with fewer than 2: the game returns.
};

TownPeriod town_period(const SaveGame& save, int period) {
	const GlobalMemory& g = save.globals;
	const auto& lists = save.statistics.towns;
	TownPeriod out;
	uint32_t available = 0;
	switch (period) {
	case 0:
		out.list = &lists[0];
		out.points = 30;
		available = g.u16(kStatistics + 0x0);
		break;
	case 1:
		out.list = &lists[1];
		out.points = 24;
		available = g.u16(kStatistics + 0x2);
		break;
	case 2:
		out.list = &lists[2];
		out.points = 24;
		available = g.u16(kStatistics + 0x4);
		break;
	default:
		// 0x54E250 counts the years (and keeps the count in [0x6DFC98]).
		out.list = &lists[3];
		out.points = std::min<uint32_t>(uint16_t(lists[3].size()), 0x80);
		out.stop = out.points < 2;
		return out;
	}
	out.missing = available < out.points ? out.points - available : 0;
	return out;
}

// 0x4A2AB0: the comparison's texts beside its graph, the largest and the smallest town over every town of the
// period's records.
void comparison_texts(const SaveGame& save, const GameTexts& texts, const std::vector<std::string>& names,
	const StatisticsTownChoices& choices, int entry, int period, StatisticsGraph& out) {
	const uint32_t i = uint32_t(entry - kStatisticsTownResidents);
	const auto& lists = save.statistics.towns;
	const std::vector<TownStatistics>* list = &lists[2];
	uint32_t n = 24;
	if (period == 0) {
		list = &lists[0];
		n = 31;
	} else if (period == 1) {
		list = &lists[1];
	} else if (period != 2) {
		const uint32_t years = uint16_t(lists[3].size());
		if (years >= 1) {
			list = &lists[3];
			n = std::min<uint32_t>(years, 0x80);
		}
	}
	const uint8_t ware = choices.ware;
	int32_t largest = 0;
	int32_t smallest = list->empty() ? 0 : 0x7FFFFFFF;
	uint32_t largest_town = 0;
	uint32_t smallest_town = 0;
	for (uint32_t k = 0; k < n && k < list->size(); ++k) {
		const TownStatistics& record = (*list)[k];
		for (uint32_t t = 0; t < record.size(); ++t) {
			const GameMemory<0x204>& r = record[t];
			int32_t v = 0;
			switch (entry) {
			case kStatisticsTownResidents:
				v = int32_t(r.u32(0x1E0) + r.u32(0x1E4) + r.u32(0x1EC) + r.u32(0x1E8));
				break;
			case kStatisticsShipMovements:
				v = r.s32(0x1FC);
				break;
			case kStatisticsTownProduction:
				v = ware < 24 ? r.s32(0x60 + 4 * size_t(ware)) : 0;
				break;
			default:
				v = ware < 24 ? r.s32(0xC0 + 4 * size_t(ware)) : 0;
				break;
			}
			// The production's smallest only above 0.
			if (v < smallest && (entry != kStatisticsTownProduction || v > 0)) {
				smallest = v;
				smallest_town = t;
			}
			if (v > largest) {
				largest = v;
				largest_town = t;
			}
		}
	}
	uint32_t kind = 2;  // The format: the entry's third (a plain number or loads), barrels the fourth, pieces the fifth.
	float high = float(largest);
	float low = float(smallest);
	if (entry == kStatisticsTownProduction) {
		if (smallest == 0x7FFFFFFF) {
			smallest = 0;
		}
		// fild; fstp a float; fld; fdiv the float constant; fstp a float.
		const double unit = ware >= 20 ? 10.0 : kBarrelWare[ware] ? 200.0 : 2000.0;
		kind = ware >= 20 ? 4 : kBarrelWare[ware] ? 3 : 2;
		high = float(double(float(largest)) / unit);
		low = float(double(float(smallest)) / unit);
	} else if (entry == kStatisticsTownStock) {
		const int32_t unit = ware >= 20 ? 10 : kBarrelWare[ware] ? 200 : 2000;
		kind = ware >= 20 ? 4 : kBarrelWare[ware] ? 3 : 2;
		high = float(largest / unit);
		low = float(smallest / unit);
	}
	const std::string& format = texts.raw_at(kComparisonTexts + 4 * (kind + 5 * i));
	out.texts = {statistics_town_name(save, names, largest_town), game_sprintf_float(format, high),
		statistics_town_name(save, names, smallest_town), game_sprintf_float(format, low)};
	out.kinds.assign(4, 0);
	// The second and fourth labels point at a zero byte of the globals (0x6DE3C6): empty.
	out.labels = {kComparisonTexts + 20 * i, 0, kComparisonTexts + 4 + 20 * i, 0};
}

// 0x4A3FA0's texts for the town data (entries 0..5): the newest week's four classes of the town [+0xBD] (the weeks'
// head *0x6DFCA0 whatever the period; empty without it or without the town in it).
void town_data_texts(const SaveGame& save, const GameTexts& texts, uint8_t town, StatisticsGraph& out) {
	const std::vector<TownStatistics>& weeks = save.statistics.towns[1];
	for (uint32_t k = 0; k < 4; ++k) {
		std::string text;
		if (!weeks.empty() && town < weeks.front().size()) {
			const int32_t v = int32_t(weeks.front()[town].u32(0x1E0 + 4 * size_t(k)));
			text = GameTexts::sprintf_raw(texts.raw_at(kClassTexts + 4 + 8 * k), {number(v)});
		}
		out.texts.push_back(text);
		out.labels.push_back(kClassTexts + 8 * k);
	}
	out.kinds.assign(4, 0);
}

// 0x4A3FA0's handlers 0..3 (0x4A42C2, 0x4A43C4, 0x4A44ED, 0x4A4659) and their fills (0x4A4D3A, 0x4A4E40, 0x4A4FE1,
// 0x4A5182, 0x4A54A4, 0x4A5386) for the town data, each point from the period's newest record backwards (past the
// list's end, or with the town missing in a record, 0).
void town_data_graph(const SaveGame& save, const GameTexts& texts, const std::vector<std::string>& names,
	StatisticsTownChoices& choices, int entry, const TownPeriod& p, StatisticsGraph& out) {
	const uint8_t town = choices.town;
	out.title_end = statistics_town_name(save, names, town);  // The title += the town's name; the button [+0x214] too.
	out.choice = out.title_end;
	// With fix_game_bugs(), a space before the name when the title ends without one (the English exe's population,
	// «population of the townLubeck»; the other titles end in « in », the Spanish one in « de »).
	if (fix_game_bugs()) {
		const std::string& title = statistics_title(texts, entry);
		if (!title.empty() && title.back() != ' ') {
			out.title_end = " " + out.title_end;
		}
	}
	uint32_t s = 0;
	if (entry <= kStatisticsQualityOfLife) {
		// Four series (0x820 bytes): only their colours are set (the slots'; 1 and 2 the fourth 0xFFFFFFFF). The
		// hidden buttons [+0xC28] get the classes' names (1 and 2: three, and « », 0x6BADC8, the fourth).
		s = 4;
		for (uint32_t k = 0; k < 4; ++k) {
			out.colours[k] = kStatisticsColours[k];
			out.buttons[k] = texts.raw_at(kClassNames + 4 * k);
		}
		if (entry != kStatisticsPopulation) {
			out.colours[3] = kZeroLine;
			out.buttons[3] = " ";
		}
	} else {
		// A series per ware of the four slots, in its slot's colour, its button the ware's name (all four shown);
		// 3 with one more after them (0xA28 bytes: room for five), the zero line {0xFFFFFFFF, 0}.
		for (size_t k = 0; k < 4; ++k) {
			const uint8_t ware = choices.wares[k];
			if (ware >= 24) {
				continue;
			}
			out.colours[s] = kStatisticsColours[k];
			out.merchants[s] = ware;
			out.buttons[k] = texts.raw_at(kWareNames + 4 * uint32_t(ware));
			++s;
		}
		if (entry == kStatisticsExportBalance) {
			out.colours[s] = kZeroLine;
			out.merchants[s] = 0;
			++s;
		}
	}
	out.series = s;
	const uint32_t n = out.points;
	for (uint32_t k = 0; k < s; ++k) {
		out.values[k].assign(n, 0.0f);
	}
	const std::vector<TownStatistics>& list = *p.list;
	// The record of the point `i` from the newest (i = 0), or nullptr: past the list, or without the town.
	auto record = [&](uint32_t i) -> const GameMemory<0x204>* {
		return i < list.size() && town < list[i].size() ? &list[i][town] : nullptr;
	};
	switch (entry) {
	case kStatisticsPopulation:
		// It also sets the comparison's first town past the towns (0xFF, «none» of its list) to 0.
		if (choices.towns[0] > save.globals.u32(global::kWorldTownCount)) {
			choices.towns[0] = 0;
		}
		for (uint32_t i = 0; i < n; ++i) {
			if (const GameMemory<0x204>* r = record(i)) {
				for (size_t k = 0; k < 4; ++k) {
					out.values[k][n - 1 - i] = float(r->u32(0x1E0 + 4 * k));  // fild of the u32 as a u64
				}
			}
		}
		break;
	case kStatisticsRequirement:
	case kStatisticsQualityOfLife: {
		const size_t at = entry == kStatisticsRequirement ? 0x1F6 : 0x1F0;
		float low = 0.0f;
		for (uint32_t i = 0; i < n; ++i) {
			if (const GameMemory<0x204>* r = record(i)) {
				for (size_t k = 0; k < 3; ++k) {
					const float v = float(int32_t(r->s16(at + 2 * k)));  // movsx; fild
					out.values[k][n - 1 - i] = v;
					if (v < low) {
						low = v;
					}
				}
			}
		}
		if (low < 0.0f) {
			// Every point raised by the smallest, and the fourth series its zero line.
			const float up = -low;
			for (size_t k = 0; k < 3; ++k) {
				for (float& v : out.values[k]) {
					v = up + v;
				}
			}
			out.values[3].assign(n, up);
		} else {
			out.series = 3;
		}
		break;
	}
	case kStatisticsExportBalance: {
		float low = 0.0f;
		float high = 0.0f;
		for (uint32_t i = 0; i < n; ++i) {
			if (const GameMemory<0x204>* r = record(i)) {
				// Every series, the zero line too (its ware 0).
				for (uint32_t k = 0; k < s; ++k) {
					const float v = float(r->s32(0x120 + 4 * size_t(out.merchants[k])));
					out.values[k][n - 1 - i] = v;
					if (v < low) {
						low = v;
					} else if (v > high) {
						high = v;
					}
				}
			}
		}
		// fld; fmul the float 0.1 ([0x66FD5C]); fstp a float.
		const float up = low < 0.0f ? -low : high > 0.0f ? float(double(high) * double(0.1f)) : 100.0f;
		for (uint32_t i = 0; i < n; ++i) {
			for (uint32_t k = 0; k < s; ++k) {
				out.values[k][i] = up + out.values[k][i];
			}
			out.values[s - 1][i] = up;
		}
		break;
	}
	default: {
		const size_t at = entry == kStatisticsTownDataProduction ? 0x60 : 0xC0;
		for (uint32_t i = 0; i < n; ++i) {
			if (const GameMemory<0x204>* r = record(i)) {
				for (uint32_t k = 0; k < s; ++k) {
					out.values[k][n - 1 - i] = float(r->s32(at + 4 * size_t(out.merchants[k])));
				}
			}
		}
		break;
	}
	}
}

}  // namespace

std::string game_sprintf_float(const std::string& format, double value) {
	const size_t at = format.find('%');
	if (at == std::string::npos) {
		return format;
	}
	size_t j = at + 1;
	int digits = 6;
	if (j < format.size() && format[j] == '.') {
		digits = 0;
		for (++j; j < format.size() && format[j] >= '0' && format[j] <= '9'; ++j) {
			digits = digits * 10 + (format[j] - '0');
		}
	}
	if (j >= format.size() || format[j] != 'f') {
		return format;
	}
	std::string number;
	if (std::isnan(value) || std::isinf(value)) {
		number = game_format_float(value);
	} else {
		// The exact decimal digits, then the half away from zero at the precision.
		char exact[512];
		std::snprintf(exact, sizeof(exact), "%.*f", digits + 40, std::fabs(value));
		const std::string all(exact);
		const size_t dot = all.find('.');
		std::string kept = all.substr(0, dot);
		if (digits > 0) {
			kept += "." + all.substr(dot + 1, size_t(digits));
		}
		if (all[dot + 1 + size_t(digits)] >= '5') {
			int k = int(kept.size()) - 1;
			for (; k >= 0; --k) {
				if (kept[size_t(k)] == '.') {
					continue;
				}
				if (kept[size_t(k)] == '9') {
					kept[size_t(k)] = '0';
				} else {
					++kept[size_t(k)];
					break;
				}
			}
			if (k < 0) {
				kept = "1" + kept;
			}
		}
		number = (std::signbit(value) ? "-" : "") + kept;
	}
	return format.substr(0, at) + number + format.substr(j + 1);
}

std::string statistics_town_name(const SaveGame& save, const std::vector<std::string>& by_number, uint32_t town) {
	if (fix_game_bugs() && town >= save.globals.u32(global::kWorldTownCount)) {
		return {};
	}
	const uint32_t number = fix_game_bugs() ? save.globals.u8(kTownIds + town) : town;
	return number < by_number.size() ? by_number[number] : std::string();
}

void statistics_default_towns(const SaveGame& save, StatisticsTownChoices& choices) {
	const GlobalMemory& g = save.globals;
	if (choices.towns[0] >= g.u32(global::kWorldTownCount)) {
		const uint32_t player = g.u32(global::kPlayer);
		choices.towns[0] = player < g.u16(global::kWorldMerchantCount) && player < save.merchants.size()
			? save.merchants[player].memory.u8(0x19)
			: 0;
	}
	if (choices.wares[0] >= 24) {
		choices.wares[0] = 0;
	}
}

StatisticsResidents statistics_residents(const SaveGame& save, const GameTexts& texts,
	const std::vector<std::string>& town_names) {
	StatisticsResidents out;
	const uint32_t towns = std::min<uint32_t>(save.globals.u32(global::kWorldTownCount), uint32_t(save.towns.size()));
	std::vector<int32_t> residents(towns, 0);
	const std::vector<TownStatistics>& weeks = save.statistics.towns[1];
	if (!weeks.empty()) {
		// The newest week's four classes. (The towns past its record keep what the game's stack held: 0 here.)
		const TownStatistics& week = weeks.front();
		for (size_t t = 0; t < week.size() && t < residents.size(); ++t) {
			const GameMemory<0x204>& r = week[t];
			const int32_t v = int32_t(r.u32(0x1E0) + r.u32(0x1E4) + r.u32(0x1EC) + r.u32(0x1E8));
			residents[t] = v < 0 ? 0 : v;
		}
	} else {
		for (uint32_t t = 0; t < towns; ++t) {
			residents[t] = save.towns[t].memory.s32(0x2D4);
		}
	}
	// The rows by name: each time the first town left with the smallest name (strcmp), then it is used (−1).
	std::vector<std::string> names(towns);
	for (uint32_t t = 0; t < towns; ++t) {
		names[t] = statistics_town_name(save, town_names, t);
	}
	const std::string& format = texts.raw_at(kResidentsText);
	for (uint32_t k = 0; k < towns; ++k) {
		uint32_t best = 0;
		while (best < towns && residents[best] < 0) {
			++best;
		}
		if (best >= towns) {
			break;
		}
		for (uint32_t t = best + 1; t < towns; ++t) {
			if (residents[t] >= 0 && compare_names(names[t], names[best], false) < 0) {
				best = t;
			}
		}
		out.towns.push_back(best);
		out.texts.push_back(game_sprintf_float(format, double(float(residents[best]))));  // fild; fstp a float
		residents[best] = -1;
	}
	uint32_t total = 0;
	for (uint32_t t = 0; t < towns; ++t) {
		total += save.towns[t].memory.u32(0x2D4);
	}
	out.total = int32_t(total);
	return out;
}

StatisticsGraph statistics_towns(const SaveGame& save, const GameTexts& texts, const std::vector<std::string>& town_names,
	StatisticsTownChoices& choices, int entry, int period) {
	StatisticsGraph out;
	const bool data = entry >= kStatisticsPopulation && entry <= kStatisticsTownDataStock;
	if (!data && (entry < kStatisticsTownResidents || entry > kStatisticsTownStock)) {
		return out;
	}
	period = statistics_competition_period(period);  // The towns' graphs have no «4 weeks» either.
	const TownPeriod p = town_period(save, period);
	out.points = p.points;
	out.missing = p.missing;
	if (p.stop) {
		return out;
	}
	if (data) {
		town_data_texts(save, texts, choices.town, out);
	} else {
		comparison_texts(save, texts, town_names, choices, entry, period, out);
	}
	const uint32_t towns = save.globals.u32(global::kWorldTownCount);
	bool any_town = false;
	bool any_ware = false;
	for (size_t k = 0; k < 4; ++k) {
		any_town = any_town || choices.towns[k] < towns;
		any_ware = any_ware || choices.wares[k] < 24;
	}
	if (!any_town || !any_ware) {
		return out;
	}
	if (data) {
		town_data_graph(save, texts, town_names, choices, entry, p, out);
		return out;
	}
	if (entry == kStatisticsTownProduction || entry == kStatisticsTownStock) {
		out.choice = texts.raw_at(kWareNames + 4 * uint32_t(choices.ware));
		out.title_end = out.choice;
	}
	uint32_t s = 0;
	for (size_t k = 0; k < 4; ++k) {
		if (choices.towns[k] >= towns) {
			continue;
		}
		out.colours[s] = kStatisticsColours[k];
		out.merchants[s] = choices.towns[k];
		out.buttons[k] = statistics_town_name(save, town_names, choices.towns[k]);
		++s;
	}
	out.series = s;
	const uint8_t ware = choices.ware;
	for (uint32_t k = 0; k < s; ++k) {
		out.values[k].assign(out.points, 0.0f);
	}
	// Each point from the newest record backwards; past the list's end, 0.
	for (uint32_t i = 0; i < out.points && i < p.list->size(); ++i) {
		const TownStatistics& record = (*p.list)[i];
		for (uint32_t k = 0; k < s; ++k) {
			const uint32_t t = out.merchants[k];
			if (t >= record.size()) {
				continue;
			}
			const GameMemory<0x204>& r = record[t];
			float v = 0.0f;
			switch (entry) {
			case kStatisticsTownResidents:  // fild of the u32 as a u64
				v = float(r.u32(0x1E0) + r.u32(0x1E4) + r.u32(0x1E8) + r.u32(0x1EC));
				break;
			case kStatisticsShipMovements:
				v = float(r.u32(0x1FC));
				break;
			case kStatisticsTownProduction:  // fild of the s32
				v = ware < 24 ? float(r.s32(0x60 + 4 * size_t(ware))) : 0.0f;
				break;
			default:
				v = ware < 24 ? float(r.s32(0xC0 + 4 * size_t(ware))) : 0.0f;
				break;
			}
			out.values[k][out.points - 1 - i] = v;
		}
	}
	return out;
}

StatisticsChoice statistics_town_choice(const SaveGame& save, const StatisticsTownChoices& choices, int slot, int entry) {
	StatisticsChoice out;
	out.mode = 1;
	if (entry >= kStatisticsExportBalance && entry <= kStatisticsTownDataStock) {
		// 0x49E906: the wares not in another slot of the four wares (then «none»), no keys.
		out.mode = 0;
		for (uint8_t w = 0; w < 24; ++w) {
			bool shown = false;
			for (int j = 0; j < 4 && !shown; ++j) {
				shown = j != slot && choices.wares[size_t(j)] == w;
			}
			if (shown) {
				out.none = true;
				continue;
			}
			out.merchants.push_back(w);
		}
		return out;
	}
	if (entry < kStatisticsTownResidents || entry > kStatisticsTownStock) {
		return out;
	}
	const uint32_t towns = std::min<uint32_t>(save.globals.u32(global::kWorldTownCount), uint32_t(save.towns.size()));
	for (uint32_t t = 0; t < towns; ++t) {
		bool shown = false;  // In another slot: not listed, and the extra row «none».
		for (int j = 0; j < 4 && !shown; ++j) {
			shown = j != slot && choices.towns[size_t(j)] == t;
		}
		if (shown) {
			out.none = true;
			continue;
		}
		out.merchants.push_back(uint8_t(t));
	}
	out.kind = entry - kStatisticsTownResidents;
	const std::vector<TownStatistics>& weeks = save.statistics.towns[1];
	const uint8_t ware = choices.ware;
	for (uint8_t t : out.merchants) {
		int32_t key = 0;
		if (entry == kStatisticsTownResidents) {
			key = save.towns[t].memory.s32(0x2D4);
		} else if (!weeks.empty() && t < weeks.front().size()) {
			const GameMemory<0x204>& r = weeks.front()[t];
			key = entry == kStatisticsShipMovements ? r.s32(0x1FC)
				: entry == kStatisticsTownProduction ? r.s32(0x60 + 4 * size_t(ware))
													 : r.s32(0xC0 + 4 * size_t(ware));
		}
		out.keys.push_back(key);
	}
	return out;
}

StatisticsChoice statistics_ware_choice() {
	StatisticsChoice out;
	out.mode = 0;
	for (uint8_t w = 0; w < 24; ++w) {
		out.merchants.push_back(w);
	}
	return out;
}

StatisticsChoice statistics_town_list(const SaveGame& save) {
	StatisticsChoice out;
	out.mode = 1;
	const uint32_t towns = save.globals.u32(global::kWorldTownCount);
	for (uint32_t t = 0; t < towns; ++t) {
		out.merchants.push_back(uint8_t(t));
	}
	return out;
}

void sort_choice_by_name(const SaveGame& save, const GameTexts& texts, const std::vector<std::string>& town_names,
	StatisticsChoice& choice) {
	if (choice.mode == 2) {
		sort_choice_by_name(save, choice);
	} else if (choice.mode == 1) {
		bubble_sort(choice, [&](uint32_t a, uint32_t b) {
			return compare_names(statistics_town_name(save, town_names, a), statistics_town_name(save, town_names, b), true) > 0;
		});
	} else {
		bubble_sort(choice, [&](uint32_t a, uint32_t b) {
			return compare_names(texts.raw_at(kWareNames + 4 * a), texts.raw_at(kWareNames + 4 * b), true) > 0;
		});
	}
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

// --- The window's drawing ---

int statistics_balance_row_y(int row) {
	// 0x4A0C30: from DrawYmin; a header half a row (8) lower, its rows the font 0's height and half a row under it.
	int y = kStatsTitleY;
	for (int r = 0; r < kBalanceRows; ++r) {
		const bool header = balance_header(r);
		if (header) {
			y += kStatsRowStep / 2;
		}
		if (r == row) {
			return y;
		}
		y += header ? kStatsRowHeight + kStatsRowStep / 2 : kStatsRowStep;
	}
	return y;
}

StatisticsScale statistics_scale(int entry, const StatisticsGraph& g) {
	// 0x4A5F3D..0x4A609B: the floor, raised by every point of the series filled ([+0x2084] series of [+0x2088] points).
	float largest = statistics_scale_floor(entry);
	for (uint32_t s = 0; s < g.series && s < g.values.size(); ++s) {
		for (uint32_t p = 0; p < g.points && p < g.values[s].size(); ++p) {
			if (largest < g.values[s][p]) {
				largest = g.values[s][p];
			}
		}
	}
	StatisticsScale out;
	if (largest > 0.0f) {
		// fld [+0xE8]; fmul [0x66FD60] (0.95f); fdiv the largest.
		out.scale = float(double(float(kStatsPlotH)) * double(0.95f) / double(largest));
		out.scaled = true;
	} else {
		out.scale = largest;
	}
	return out;
}

float statistics_x_step(uint32_t points) {
	// 0x4A5F25: the points less one as an unsigned 64-bit integer, to a float.
	return float(double(float(kStatsPlotW)) / double(float(uint32_t(points - 1))));
}

std::vector<StatisticsXLabel> statistics_x_labels(const SaveGame& save, const GameTexts* texts, int period,
	uint32_t points, int week_width, int year_width) {
	std::vector<StatisticsXLabel> out;
	if (points == 0) {
		return out;
	}
	const float step = statistics_x_step(points);
	const int day = save.globals.u8(global::kWorldDay);
	const int month = save.globals.u8(global::kWorldMonth);  // 0 January
	char text[8] = {};
	int number = 0;  // esi: the day, the week or the month
	int month1 = 0;  // ebx: the days' month, 1..12
	int room = 0;  // [esp+0x14]: the room since the last label (the weeks', the years')
	int room_step = 0;  // [esp+0x18]: the step's ftol
	int width = 0;  // the label's width
	switch (period) {
	case 0:
		number = day;
		month1 = month + 1;
		break;
	case 1: {
		const uint32_t weekday = (save.tick() >> 8) % 7;
		const int32_t week = (int32_t(save.globals.u16(global::kWorldDayOfYear)) - int32_t(weekday)) / 7;
		text[0] = char('0' + week / 10);
		text[1] = char('0' + week % 10);
		text[2] = '.';
		room = width = week_width;
		room_step = game_ftol(step);
		break;
	}
	case 2: number = month; break;
	case 3:
		std::snprintf(text, sizeof(text), "%i", int(save.globals.u16(global::kWorldYear)));
		room = width = year_width;
		room_step = game_ftol(step);
		break;
	default: break;
	}
	// The last point's x: fild (points − 1); fstp; fld; fmul the step; fadd the plot's left; fstp.
	float x = float(double(float(uint32_t(points - 1))) * double(step) + double(float(kStatsPlotX)));
	// Whether a week's or a year's label fits since the last one (0x4A00FD).
	auto spaced = [&]() {
		if (width > room) {
			room += room_step;
			return false;
		}
		room = room_step;
		return true;
	};
	for (uint32_t k = 0; k < points; ++k) {
		std::string label;
		switch (period) {
		case 0:
			if (--number < 1) {
				// 0x49FF58: the month before, its days the current month's previous one's (0x672D92 + 2 · month): twice
				// back (from March's 1st and 2nd, past February into January) the wrong one's, fixed behind
				// fix_game_bugs.
				if (fix_game_bugs()) {
					month1 = month1 > 1 ? month1 - 1 : 12;
					number = kMonthDays[month1 - 1];
				} else if (month > 0) {
					--month1;
					number = kMonthDays[month - 1];
				} else {
					month1 = 12;
					number = kMonthDays[11];
				}
			}
			if (number % 5 == 0) {
				label = std::to_string(number) + "." + std::to_string(month1) + ".";
			}
			break;
		case 1:
			// The week before: «01.» wraps to «52.».
			if (text[1] > '1') {
				--text[1];
			} else if (text[0] > '0') {
				if (text[1] == '1') {
					text[1] = '0';
				} else {
					--text[0];
					text[1] = '9';
				}
			} else {
				text[0] = '5';
				text[1] = '2';
			}
			if (spaced()) {
				label = text;
			}
			break;
		case 2: {
			if (--number < 0) {
				number = 11;
			}
			const std::string name = texts ? texts->raw_at(kMonthNames + 4 + 4 * uint32_t(number)) : std::string();
			label = name.empty() ? std::string() : name.substr(0, 1);
			break;
		}
		case 3:
			// The year before, digit by digit from the last.
			for (int d = 3; d >= 0; --d) {
				if (--text[d] >= '0' || d == 0) {
					break;
				}
				text[d] = '9';
			}
			if (spaced()) {
				label = text;
			}
			break;
		default: break;
		}
		out.push_back({x, label});
		x = float(double(x) - double(step));
	}
	return out;
}

StatisticsYAxis statistics_y_axis(const GameTexts* texts, int entry, const StatisticsGraph& g, const StatisticsScale& s,
	int half_line) {
	StatisticsYAxis out;
	const uint8_t kind = entry_kind(entry);
	const float bottom = float(kStatsPlotBottom);
	const float top = float(kStatsPlotY);
	float base = 1.0f;  // [esp+0x14]: the step
	int digits = 14;  // [esp+0x18]: where the counter's units are
	int series = -1;  // ebp: the zero line's series (the kinds 5 and 6)
	switch (kind) {
	case 0: base = 2000.0f; break;
	case 1:
	case 3:
	case 4: break;
	case 2:
		base = 1000.0f;
		digits = 13;
		break;
	case 5:
		series = int(g.series) - 1;
		base = 1.0e7f;
		break;
	case 6:
		series = 0xFFFF;
		base = 1.0e7f;
		break;
	default: return out;
	}
	if (!(s.scale > 0.0f)) {
		return out;  // (The game's scale is never at or below 0 here: its floor is at least 1.)
	}
	// The step grows (×2, ×2.5, ×2: 1, 2, 5, 10...) until it is over three half lines on the plot.
	const float least = float(half_line) * 3.0f;
	char increment = 1;
	for (int cycle = 0;;) {
		if (float(double(base) * double(s.scale)) > least) {
			break;
		}
		if (cycle == 0) {
			base += base;
			increment = 2;
			cycle = 1;
		} else if (cycle == 1) {
			base = float(double(base) * 2.5);
			increment = 5;
			cycle = 2;
		} else {
			base += base;
			increment = 1;
			--digits;
			cycle = 0;
		}
	}
	char counter[16];
	std::memset(counter, ' ', 15);
	for (int k = std::max(0, digits); k < 15; ++k) {
		counter[k] = '0';
	}
	if (kind == 2) {
		counter[14] = 't';
	}
	counter[15] = 0;
	auto at = [&](double y) { return game_ftol(y); };
	auto word = [&](uint32_t slot) { return texts ? texts->raw_at(slot) : std::string(); };
	const float factor = s.scaled ? s.scale : 1.0f;
	const bool zero = series >= 0 && uint32_t(series) < g.series && size_t(series) < g.values.size() &&
		!g.values[size_t(series)].empty();
	const double zero_y = zero ? double(bottom) - double(float(double(factor) * double(g.values[size_t(series)][0]))) : 0.0;
	if (kind < 5) {
		out.labels.push_back({at(bottom) - half_line, "0"});
		out.labels.push_back({at(top) - half_line, word(0x6C1AAC + 4 * uint32_t(kind))});
	} else if (entry < kStatisticsExportBalance) {
		if (zero) {
			out.labels.push_back({at(zero_y) - half_line, word(0x6C2274)});  // «min.»
		}
		out.labels.push_back({at(top) - half_line, word(0x6C2268)});  // «max.»
	} else if (entry == kStatisticsExportBalance) {
		out.labels.push_back({at(top) - half_line, word(0x6C225C)});  // «exp.»
		if (zero) {
			out.labels.push_back({at(zero_y) - half_line, "0"});
		}
		out.labels.push_back({at(double(bottom) - double(float(half_line))), word(0x6C2250)});  // «imp.»
	} else if (zero) {
		out.labels.push_back({at(zero_y) - half_line, "0"});
	}
	// The steps up the plot while over its top and a line.
	const float stop = float(double(float(2 * half_line)) + double(top));
	float value = 0.0f;
	for (int guard = 0; guard < 1000; ++guard) {
		int p = std::clamp(digits, 0, 14);
		counter[p] = char(counter[p] + increment);
		while (counter[p] > '9' && p > 0) {
			counter[p] = '0';
			--p;
			counter[p] = counter[p] == ' ' ? '1' : char(counter[p] + 1);
		}
		value = float(double(value) + double(base));
		const float y = float(double(bottom) - double(value) * double(s.scale));
		if (!(y > stop)) {
			break;
		}
		const char* first = counter;
		while (*first == ' ') {
			++first;
		}
		out.labels.push_back({game_ftol(y) - half_line, first});
		out.grid.push_back(y);
	}
	return out;
}

}  // namespace p3::data
