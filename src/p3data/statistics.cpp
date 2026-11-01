#include "p3data/statistics.hpp"

#include <cstring>

#include "p3data/game_fixes.hpp"

namespace p3::data {

namespace tick::events_detail {
// town_tick/events.cpp: the merchant's reputation in a town and in all.
int16_t fn_4f9580(const SaveGame& save, int merchant, uint32_t town);
int16_t fn_4f73d0(const SaveGame& save, int merchant);
}  // namespace tick::events_detail

namespace {

constexpr uint32_t kTownCount = global::kWorldTownCount;  // u32
constexpr uint32_t kOfficeCount = global::kWorldOfficeCount;  // u16
constexpr uint32_t kWorldRecordCount = global::kWorld + 0x6;  // u16
constexpr uint32_t kCaptainCount = global::kCaptainCount;  // u16

// 0x672D92 + 2 · month: the days of the month before (0x672DAA, 31, for
// January).
constexpr uint16_t kDaysOfMonthBefore[12] = {31, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30};

// 0x673A18: what each ware is worth for the value of a merchant's
// production (0x54C520).
constexpr float kWareWorth[24] = {0.055000003f, 0.47855002f, 0.22005001f, 0.174f, 0.1425f, 0.55f, 1.4f, 1.1f,
	1.034f, 3.3825f, 0.4125f, 0.027500002f, 1.278f, 1.12f, 0.44000003f, 0.278f, 0.44000003f, 0.22000001f,
	0.85499996f, 0.039900005f, 10.0f, 8.0f, 12.0f, 14.0f};

// The rings of the records of a merchant's object.
constexpr size_t kRecord = 0x70;
constexpr size_t kDays = 0x8;  // 32 records
constexpr size_t kWeeks = 0xE08;  // 24
constexpr size_t kMonths = 0x1888;  // 24
constexpr size_t kLink = 0x6C;  // In each record: the previous one.

// --- The records of the towns ---

// 0x54D1D0: the record of the day of each town.
TownStatistics town_record(SaveGame& save) {
	GlobalMemory& g = save.globals;
	TownStatistics out(g.u32(kTownCount));
	for (size_t i = 0; i < out.size() && i < save.towns.size(); ++i) {
		GameMemory<0x204>& b = out[i];
		TownMemory& t = save.towns[i].memory;
		b.u32(0x1FC) = t.u16(0x996);
		t.u16(0x996) = 0;
		for (size_t w = 0; w < 24; ++w) {
			b.u32(4 * w) = t.u32(0x310 + 4 * w) + t.u32(0x64 + 4 * w);  // what is consumed
			b.u32(0x60 + 4 * w) = t.u32(0xC4 + 4 * w);  // produced
			b.u32(0xC0 + 4 * w) = t.u32(0x4 + 4 * w);  // in stock
			b.u32(0x180 + 4 * w) = t.u32(0x430 + 4 * w);
			b.u32(0x120 + 4 * w) = t.u32(0x370 + 4 * w);
			t.u32(0x370 + 4 * w) = 0;
		}
		// The offices in the town, from [0x784] through [0x2CA].
		for (uint32_t k = t.u16(0x784); k < g.u16(kOfficeCount) && k < save.offices.size();) {
			const OfficeMemory& o = save.offices[k].memory;
			k = o.u16(0x2CA);
			for (size_t w = 0; w < 24; ++w) {
				b.u32(4 * w) += o.u32(0x64 + 4 * w);
				b.u32(0x60 + 4 * w) += o.u32(0xC4 + 4 * w);
			}
		}
		b.u32(0x200) = t.u32(0x2C8);
		for (size_t k = 0; k < 4; ++k) {
			b.u32(0x1E0 + 4 * k) = t.u32(0x2D8 + 4 * k);
		}
		for (size_t k = 0; k < 3; ++k) {
			b.u16(0x1F0 + 2 * k) = t.u16(0x300 + 2 * k);
			b.u16(0x1F6 + 2 * k) = t.u16(0x308 + 2 * k);
		}
	}
	return out;
}

// 0x54D390: the average of the first `n` records of `list` (for as many
// towns as the first one has).
TownStatistics town_average(const std::vector<TownStatistics>& list, uint32_t n) {
	TownStatistics out;
	if (list.empty() || n == 0) {
		return out;
	}
	out.assign(list.front().size(), GameMemory<0x204>());
	int32_t count = 0;
	for (size_t k = 0; k < list.size() && n > 0; ++k, --n) {
		const TownStatistics& in = list[k];
		const size_t towns = std::min(out.size(), in.size());
		for (size_t i = towns; i-- > 0;) {
			GameMemory<0x204>& d = out[i];
			const GameMemory<0x204>& s = in[i];
			for (size_t o = 0; o < 0x1E0; o += 4) {
				d.u32(o) += s.u32(o);
			}
			d.u32(0x200) |= s.u32(0x200);
			for (size_t o = 0x1E0; o < 0x1F0; o += 4) {
				d.u32(o) += s.u32(o);
			}
			for (size_t o = 0x1F0; o < 0x1FC; o += 2) {
				d.u16(o) = uint16_t(d.u16(o) + s.u16(o));
			}
			d.u32(0x1FC) += s.u32(0x1FC);
		}
		++count;
	}
	for (GameMemory<0x204>& d : out) {
		for (size_t o = 0; o < 0x1E0; o += 4) {
			d.s32(o) /= count;
		}
		d.u32(0x200) &= 0xFBFFFFF9;
		for (size_t o = 0x1E0; o < 0x1F0; o += 4) {
			d.u32(o) /= uint32_t(count);
		}
		for (size_t o = 0x1F0; o < 0x1FC; o += 2) {
			d.s16(o) = int16_t(int32_t(d.s16(o)) / count);
		}
	}
	return out;
}

// 0x54D6C0 and 0x54D660: at the head of the list, keeping `keep` records (0
// for all).
void push_record(std::vector<TownStatistics>& list, TownStatistics record, size_t keep) {
	list.insert(list.begin(), std::move(record));
	if (keep && list.size() > keep) {
		list.resize(keep);
	}
}

// --- The records of a merchant (0x70 bytes, in its object or apart) ---

float& f32(uint8_t* r, size_t o) {
	return *reinterpret_cast<float*>(r + o);
}
int32_t& s32(uint8_t* r, size_t o) {
	return *reinterpret_cast<int32_t*>(r + o);
}
uint32_t& u32(uint8_t* r, size_t o) {
	return *reinterpret_cast<uint32_t*>(r + o);
}
uint16_t& u16(uint8_t* r, size_t o) {
	return *reinterpret_cast<uint16_t*>(r + o);
}

// fld a; fadd b; fstp: the sum of two floats rounded once.
float add(float a, float b) {
	return float(double(a) + double(b));
}

// 0x54C520: the merchant's record of the day in `r`. It takes and clears
// his counters of the day (without `clear`, it only takes them: not the
// game's, for the fix in read_statistics).
void merchant_record(SaveGame& save, int merchant, uint8_t* r, bool clear = true) {
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const GlobalMemory& g = save.globals;
	f32(r, 0x0) = float(m.s32(0x46C));
	if (m.s32(0x0) > 0) {
		f32(r, 0x4) = float(m.s32(0x0));
	} else {
		u32(r, 0x4) = 0;
	}
	struct Taken {
		size_t to;
		size_t from;
	};
	u32(r, 0x8) = m.u32(0x484);
	u32(r, 0xC) = m.u32(0x494);
	f32(r, 0x10) = float(m.s32(0x498));
	f32(r, 0x18) = float(m.s32(0x4A0));
	f32(r, 0x14) = float(m.s32(0x49C));
	for (Taken t : {Taken{0x1C, 0x4A4}, {0x20, 0x48C}, {0x24, 0x4AC}, {0x28, 0x4C0}, {0x2C, 0x4C4}, {0x30, 0x4C8},
			 {0x34, 0x4B8}, {0x38, 0x4BC}, {0x3C, 0x4B0}, {0x40, 0x4B4}, {0x44, 0x4CC}}) {
		u32(r, t.to) = m.u32(t.from);
	}
	for (size_t o : {0x484, 0x494, 0x4A0, 0x49C, 0x4A4, 0x48C, 0x4AC, 0x4C0, 0x4C4, 0x4C8, 0x4B8, 0x4BC, 0x4B0,
			 0x4B4, 0x4CC}) {
		if (clear) {
			m.u32(o) = 0;
		}
	}
	u32(r, 0x48) = 0;
	u32(r, 0x5C) = 0;
	u16(r, 0x56) = 0;
	u16(r, 0x58) = 0;
	u16(r, 0x5A) = 0;
	float worth = 0.0f;
	// His offices, from [0xC] through [0x2C8].
	for (uint32_t k = m.u16(0xC); k < g.u16(kOfficeCount) && k < save.offices.size();) {
		const OfficeMemory& o = save.offices[k].memory;
		k = o.u16(0x2C8);
		u16(r, 0x5A) = uint16_t(u16(r, 0x5A) + 1);
		for (size_t w = 0; w < 24; ++w) {
			const int32_t produced = o.s32(0xC4 + 4 * w);
			if (produced > 0) {
				f32(r, 0x48) = add(float(produced), f32(r, 0x48));
				// fld; fmul; fadd; fstp: one rounding to float at the end.
				worth = float(double(float(produced)) * double(kWareWorth[w]) + double(worth));
			}
		}
		uint32_t x = o.u16(0x2EC);
		if (o.u16(0x2F0) == 0) {
			x -= 0x32;  // 0x6734E4
		}
		x /= 0xC8;  // 0x6734E5
		const uint16_t limit = o.u16(0x2D2);
		if (x < limit) {
			u16(r, 0x56) = uint16_t(u16(r, 0x56) + uint16_t(limit - x));
		}
		u16(r, 0x58) = uint16_t(u16(r, 0x58) + o.u16(0x2D4));
		if (o.u16(0x2F2) < g.u16(kCaptainCount)) {
			++u32(r, 0x5C);
		}
		u32(r, 0x5C) += o.u8(0x2C7);
		for (uint32_t w = o.u16(0x2CC); w < g.u16(kWorldRecordCount) && w < save.world_records.size();) {
			const GameMemory<0x14>& record = save.world_records[w];
			u32(r, 0x5C) += record.u16(0x4);
			w = record.u16(0x8);
		}
	}
	f32(r, 0x4C) = worth;
	u32(r, 0x60) = m.u32(0x474);
	u16(r, 0x54) = m.u16(0x478);
	u32(r, 0x50) = m.u32(0x470);
	f32(r, 0x64) = float(int32_t(tick::events_detail::fn_4f9580(save, merchant, m.u8(0x19))));
	f32(r, 0x68) = float(int32_t(tick::events_detail::fn_4f73d0(save, merchant)));
}

// 0x54C810: `d` += `s`.
void add_record(uint8_t* d, const uint8_t* s_) {
	uint8_t* s = const_cast<uint8_t*>(s_);
	for (size_t o : {0x0, 0x4, 0x10, 0x14, 0x18, 0x48, 0x4C, 0x64, 0x68}) {
		f32(d, o) = add(f32(s, o), f32(d, o));
	}
	for (size_t o : {0x8, 0xC, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x50, 0x5C, 0x60}) {
		u32(d, o) += u32(s, o);
	}
	for (size_t o : {0x54, 0x56, 0x58, 0x5A}) {
		u16(d, o) = uint16_t(u16(d, o) + u16(s, o));
	}
}

// 0x54C940: the average of `n` records summed in `r`, rounded.
void average_record(uint8_t* r, uint32_t n) {
	n &= 0xFFFF;
	const uint32_t half = n >> 1;
	s32(r, 0x50) = int32_t(u32(r, 0x50) + half) / int32_t(n);
	const float inverse = float(1.0 / double(float(int32_t(n))));
	for (size_t o : {0x0, 0x4, 0x10, 0x48, 0x4C, 0x64, 0x68}) {
		f32(r, o) = float(double(inverse) * double(f32(r, o)));
	}
	u32(r, 0x5C) = (u32(r, 0x5C) + half) / n;
	u32(r, 0x60) = (u32(r, 0x60) + half) / n;
	for (size_t o : {0x54, 0x56, 0x58, 0x5A}) {
		u16(r, o) = uint16_t(int32_t(uint32_t(u16(r, o)) + half) / int32_t(n));
	}
}

// 0x54C4C0: clears the 0x6C bytes of data of a record.
void clear_record(uint8_t* r) {
	std::memset(r, 0, kLink);
}

// 0x54CA20: the object of a merchant, its rings filled with his record now
// (`clear`: as merchant_record).
MerchantStatistics new_merchant_statistics(SaveGame& save, int merchant, bool clear = true) {
	MerchantStatistics out;
	GameMemory<0x2310>& m = out.memory;
	uint8_t* base = m.bytes.data();
	m.u16(0x0) = uint16_t(merchant);
	clear_record(base + kDays);
	merchant_record(save, merchant, base + kDays, clear);
	auto ring = [&](size_t first, size_t count, bool own_first) {
		if (!own_first) {
			clear_record(base + first);
			add_record(base + first, base + kDays);
		}
		m.u32(first + kLink) = uint32_t(first + kRecord * (count - 1));
		for (size_t k = 1; k < count; ++k) {
			uint8_t* r = base + first + kRecord * k;
			clear_record(r);
			add_record(r, base + kDays);
			u32(r, 0) = 0;
			u32(r, kLink) = uint32_t(first + kRecord * (k - 1));
		}
	};
	ring(kDays, 32, true);
	ring(kWeeks, 24, false);
	ring(kMonths, 24, false);
	return out;
}

// 0x54CE20: the record of the day of the merchant of `s` and, on the days
// of a new week, month or year, their averages.
void merchant_day(SaveGame& save, MerchantStatistics& s, bool week, bool month, bool year) {
	GameMemory<0x2310>& m = s.memory;
	uint8_t* base = m.bytes.data();
	const int merchant = m.u16(0x0);
	m.u16(0x2) = uint16_t((m.u8(0x2) + 1) & 0x1F);
	const size_t today = kDays + kRecord * m.u16(0x2);
	if (size_t(merchant) < save.merchants.size()) {
		merchant_record(save, merchant, base + today);
	}
	auto sum = [&](uint8_t* into, size_t from, uint32_t n) {
		for (size_t r = from; n > 0; --n, r = m.u32(r + kLink)) {
			add_record(into, base + r);
		}
	};
	if (week) {
		m.u8(0x4) = uint8_t(m.u8(0x4) + 1);
		if (m.u8(0x4) > 0x17) {
			m.u8(0x4) = 0;
		}
		uint8_t* r = base + kWeeks + kRecord * m.u8(0x4);
		clear_record(r);
		sum(r, today, 7);
		average_record(r, 7);
	}
	if (month) {
		m.u8(0x5) = uint8_t(m.u8(0x5) + 1);
		if (m.u8(0x5) > 0x17) {
			m.u8(0x5) = 0;
		}
		uint8_t* r = base + kMonths + kRecord * m.u8(0x5);
		clear_record(r);
		const uint32_t days = kDaysOfMonthBefore[save.globals.u8(global::kWorldMonth) % 12];
		sum(r, today, days);
		average_record(r, days);
	}
	if (year) {
		GameMemory<0x70> y;
		m.u16(0x6) = uint16_t(m.u16(0x6) + 1);
		clear_record(y.bytes.data());
		sum(y.bytes.data(), kMonths + kRecord * m.u8(0x5), 12);
		average_record(y.bytes.data(), 12);
		s.years.insert(s.years.begin(), y);
	}
}

}  // namespace

void daily_statistics(SaveGame& save) {
	GlobalMemory& g = save.globals;
	Statistics& st = save.statistics;
	const int32_t weekday = int32_t(g.u32(global::kWorldTick) >> 8) % 7;
	push_record(st.towns[0], town_record(save), 32);
	bool month = false;
	bool year = false;
	if (g.u8(global::kWorldDay) == 1) {
		month = true;
		const uint8_t m = g.u8(global::kWorldMonth);
		push_record(st.towns[2], town_average(st.towns[0], m < 12 ? kDaysOfMonthBefore[m] : 0), 25);
		if (g.u16(global::kWorldDayOfYear) == 0) {
			year = true;
			push_record(st.towns[3], town_average(st.towns[2], 12), 0);
			g.u16(kStatistics + 0x8) = uint16_t(g.u16(kStatistics + 0x8) + 1);
		}
		if (g.u16(kStatistics + 0x4) < 0x18) {
			g.u16(kStatistics + 0x4) = uint16_t(g.u16(kStatistics + 0x4) + 1);
		}
	}
	if (weekday == 6) {
		push_record(st.towns[1], town_average(st.towns[0], 7), 25);
		if (g.u16(kStatistics + 0x2) < 0x18) {
			g.u16(kStatistics + 0x2) = uint16_t(g.u16(kStatistics + 0x2) + 1);
		}
	}
	// The first time, an object for each merchant (from the last one) without
	// the bit 2 of his kind.
	if (st.merchants.empty()) {
		for (int i = int(g.u16(global::kWorldMerchantCount)) - 1; i >= 0; --i) {
			if (size_t(i) < save.merchants.size() && !(save.merchants[size_t(i)].memory.u8(0x8) & 4)) {
				add_merchant_statistics(save, i);
			}
		}
	}
	for (MerchantStatistics& s : st.merchants) {
		merchant_day(save, s, weekday == 0, month, year);
	}
	g.u8(kStatistics + 0x21) = uint8_t(g.u8(kStatistics + 0x21) | (weekday == 0));
	g.u8(kStatistics + 0x22) = uint8_t(g.u8(kStatistics + 0x22) | month);
	g.u8(kStatistics + 0x23) = uint8_t(g.u8(kStatistics + 0x23) | year);
	g.u8(kStatistics + 0x20) = 1;
	g.u16(kStatistics + 0x0) = uint16_t((g.u16(kStatistics + 0x0) + 1) & 0xFFF);
}

void add_merchant_statistics(SaveGame& save, int merchant) {
	for (const MerchantStatistics& s : save.statistics.merchants) {
		if (s.memory.u16(0x0) == uint16_t(merchant)) {
			return;
		}
	}
	if (merchant < 0 || size_t(merchant) >= save.merchants.size()) {
		return;  // Not checked: the game reads past the merchants.
	}
	save.statistics.merchants.insert(save.statistics.merchants.begin(), new_merchant_statistics(save, merchant));
}

void remove_merchant_statistics(SaveGame& save, int merchant) {
	if (uint16_t(merchant) >= save.globals.u16(global::kWorldMerchantCount)) {
		return;
	}
	auto& list = save.statistics.merchants;
	for (auto it = list.begin(); it != list.end(); ++it) {
		if (it->memory.u16(0x0) == uint16_t(merchant)) {
			list.erase(it);  // 0x54CB40 frees its years, then the object
			return;
		}
	}
}

namespace {

void put_u8(std::vector<uint8_t>& out, uint8_t v) {
	out.push_back(v);
}
void put_u16(std::vector<uint8_t>& out, uint16_t v) {
	out.push_back(uint8_t(v));
	out.push_back(uint8_t(v >> 8));
}
void put_u32(std::vector<uint8_t>& out, uint32_t v) {
	put_u16(out, uint16_t(v));
	put_u16(out, uint16_t(v >> 16));
}
void put_bytes(std::vector<uint8_t>& out, const uint8_t* p, size_t n) {
	out.insert(out.end(), p, p + n);
}

constexpr uint32_t kPstTag = 0xAFFE000B;
constexpr uint32_t kPstSeparator = 0xAFFEAFFE;

// Sequential reading of the .pst with bounds.
class PstReader {
public:
	PstReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
	bool ok() const { return ok_; }
	const uint8_t* take(size_t n) {
		if (!ok_ || size_ - pos_ < n) {
			ok_ = false;
			return nullptr;
		}
		const uint8_t* p = data_ + pos_;
		pos_ += n;
		return p;
	}
	uint8_t u8() {
		const uint8_t* p = take(1);
		return p ? p[0] : 0;
	}
	uint16_t u16() {
		const uint8_t* p = take(2);
		return p ? uint16_t(p[0] | p[1] << 8) : 0;
	}
	uint32_t u32() {
		const uint8_t* p = take(4);
		return p ? uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24) : 0;
	}

private:
	const uint8_t* data_;
	size_t size_;
	size_t pos_ = 0;
	bool ok_ = true;
};

}  // namespace

std::vector<uint8_t> write_statistics(SaveGame& save) {
	GlobalMemory& g = save.globals;
	Statistics& st = save.statistics;
	// 0x54D7F0 and 0x54CB90: the number of objects and of the years of each.
	g.u16(kStatistics + 0xA) = uint16_t(st.merchants.size());
	for (MerchantStatistics& m : st.merchants) {
		m.memory.u16(0x6) = uint16_t(m.years.size());
	}
	std::vector<uint8_t> out;
	put_u32(out, kPstTag);
	for (uint32_t o = 0; o <= 0xA; o += 2) {
		put_u16(out, g.u16(kStatistics + o));
	}
	for (const std::vector<TownStatistics>& list : st.towns) {
		// 0x54D080.
		for (size_t k = 0; k < list.size(); ++k) {
			put_u8(out, 1);
			put_u32(out, uint32_t(list[k].size()));
			for (const GameMemory<0x204>& town : list[k]) {
				put_bytes(out, town.bytes.data(), 0x204);
			}
		}
		put_u8(out, 0);
		put_u32(out, kPstSeparator);
	}
	// 0x54CBD0 for each object.
	for (const MerchantStatistics& m : st.merchants) {
		const GameMemory<0x2310>& o = m.memory;
		put_u16(out, o.u16(0x0));
		put_u16(out, o.u16(0x2));
		put_u8(out, o.u8(0x4));
		put_u8(out, o.u8(0x5));
		put_u16(out, o.u16(0x6));
		for (auto [first, count] : {std::pair<size_t, size_t>{kDays, 32}, {kWeeks, 24}, {kMonths, 24}}) {
			for (size_t r = 0; r < count; ++r) {
				put_bytes(out, o.bytes.data() + first + kRecord * r, kLink);
			}
		}
		for (const GameMemory<0x70>& year : m.years) {
			put_bytes(out, year.bytes.data(), kLink);
		}
	}
	put_u32(out, kPstSeparator);
	// GAME BUG: 0x54D7F0 counts the months' list one byte too many (0x54D822:
	// `lea esi, [esi + eax + 1]` and then `inc esi`), and 0x5472F0 compresses
	// that size: the file has one byte more, whatever the buffer held (0 in
	// the emulator, and here).
	if (!st.towns[2].empty()) {
		put_u8(out, 0);
	}
	return out;
}

bool read_statistics(const uint8_t* data, size_t size, SaveGame& save) {
	GlobalMemory& g = save.globals;
	PstReader r(data, size);
	if (r.u32() != kPstTag) {
		clear_statistics(save);
		return r.ok();
	}
	for (uint32_t o = 0; o <= 0xA; o += 2) {
		g.u16(kStatistics + o) = r.u16();
	}
	Statistics& st = save.statistics;
	for (std::vector<TownStatistics>& list : st.towns) {
		// 0x54D100.
		list.clear();
		if (r.u8() == 1) {
			do {
				TownStatistics record(r.u32());
				for (GameMemory<0x204>& town : record) {
					if (const uint8_t* p = r.take(0x204)) {
						std::memcpy(town.bytes.data(), p, 0x204);
					}
				}
				list.push_back(std::move(record));
			} while (r.u8() != 0 && r.ok());
		}
		r.u32();
	}
	st.merchants.clear();
	for (uint32_t k = 0; k < g.u16(kStatistics + 0xA) && r.ok(); ++k) {
		// 0x54CA20 for the merchant 0, then 0x54CCC0. GAME BUG (looks like
		// one): the record of the day of 0x54CA20 clears the 15 counters of
		// the merchant 0 at each object read. The file then overwrites the
		// 0x6C bytes of every record of the rings, so the object doesn't
		// depend on that record: fixed, the counters stay (the object is the
		// same, byte for byte).
		MerchantStatistics m = new_merchant_statistics(save, 0, !fix_game_bugs());
		GameMemory<0x2310>& o = m.memory;
		o.u16(0x0) = r.u16();
		o.u16(0x2) = r.u16();
		o.u8(0x4) = r.u8();
		o.u8(0x5) = r.u8();
		o.u16(0x6) = r.u16();
		for (auto [first, count] : {std::pair<size_t, size_t>{kDays, 32}, {kWeeks, 24}, {kMonths, 24}}) {
			for (size_t i = 0; i < count; ++i) {
				if (const uint8_t* p = r.take(kLink)) {
					std::memcpy(o.bytes.data() + first + kRecord * i, p, kLink);
				}
			}
		}
		m.years.assign(o.u16(0x6), GameMemory<0x70>());
		for (GameMemory<0x70>& year : m.years) {
			if (const uint8_t* p = r.take(kLink)) {
				std::memcpy(year.bytes.data(), p, kLink);
			}
		}
		st.merchants.push_back(std::move(m));
	}
	r.u32();
	return r.ok();
}

void clear_statistics(SaveGame& save) {
	save.statistics = Statistics();
	for (uint32_t o = 0; o < 0xA; o += 2) {
		save.globals.u16(kStatistics + o) = 0;
	}
}

}  // namespace p3::data
