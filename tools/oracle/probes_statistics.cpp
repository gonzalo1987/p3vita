// Probes of src/p3data/statistics.cpp, and the regions S0..S4 of the
// statistics (town_oracle.py --statistics, see its regions()).

#include <cstring>

#include "p3data/statistics.hpp"
#include "probe_registry.hpp"

namespace p3::probe {

using namespace p3::data;

namespace {

uint32_t get_u32(const std::vector<uint8_t>& b, size_t o) {
	uint32_t v = 0;
	std::memcpy(&v, b.data() + o, 4);
	return v;
}

void put_u32(std::vector<uint8_t>& b, uint32_t v) {
	const size_t o = b.size();
	b.resize(o + 4);
	std::memcpy(b.data() + o, &v, 4);
}

template <size_t N>
void put_block(std::vector<uint8_t>& b, const GameMemory<N>& m) {
	b.insert(b.end(), m.bytes.begin(), m.bytes.end());
}

}  // namespace

std::vector<uint8_t> statistics_region(const SaveGame& save, int k) {
	std::vector<uint8_t> b;
	if (k < 4) {
		for (const TownStatistics& record : save.statistics.towns[size_t(k)]) {
			put_u32(b, uint32_t(record.size()));
			for (const GameMemory<0x204>& town : record) {
				put_block(b, town);
			}
		}
		return b;
	}
	for (const MerchantStatistics& s : save.statistics.merchants) {
		put_block(b, s.memory);
		put_u32(b, uint32_t(s.years.size()));
		for (const GameMemory<0x70>& year : s.years) {
			put_block(b, year);
		}
	}
	return b;
}

void read_statistics_region(SaveGame& save, int k, const std::vector<uint8_t>& b) {
	size_t o = 0;
	if (k < 4) {
		std::vector<TownStatistics>& list = save.statistics.towns[size_t(k)];
		list.clear();
		while (o + 4 <= b.size()) {
			TownStatistics record(get_u32(b, o));
			o += 4;
			for (GameMemory<0x204>& town : record) {
				std::memcpy(town.bytes.data(), b.data() + o, 0x204);
				o += 0x204;
			}
			list.push_back(std::move(record));
		}
		return;
	}
	save.statistics.merchants.clear();
	while (o + 0x2310 + 4 <= b.size()) {
		MerchantStatistics s;
		std::memcpy(s.memory.bytes.data(), b.data() + o, 0x2310);
		o += 0x2310;
		s.years.resize(get_u32(b, o));
		o += 4;
		for (GameMemory<0x70>& year : s.years) {
			std::memcpy(year.bytes.data(), b.data() + o, 0x70);
			o += 0x70;
		}
		save.statistics.merchants.push_back(std::move(s));
	}
}

void register_probes_statistics(ProbeRegistry& r) {
	// 0x54DE90: thiscall on 0x6DFC90, no arguments.
	r[0x54DE90] = [](SaveGame& save, const ProbeCall&) -> std::optional<uint32_t> {
		daily_statistics(save);
		return std::nullopt;
	};
	// 0x54E170: thiscall on 0x6DFC90, the merchant.
	r[0x54E170] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		add_merchant_statistics(save, int(c.args[0] & 0xFFFF));
		return std::nullopt;
	};
	// 0x54E1F0: thiscall on 0x6DFC90, the merchant.
	r[0x54E1F0] = [](SaveGame& save, const ProbeCall& c) -> std::optional<uint32_t> {
		remove_merchant_statistics(save, int(c.args[0] & 0xFFFF));
		return std::nullopt;
	};
}

}  // namespace p3::probe
