#include "p3data/thresholds.hpp"

#include <algorithm>

namespace p3::data {

namespace {

// Bits of the town's field 0x2C8.
constexpr uint32_t kCrisisFlags = 0x4000A10;  // Siege, blockade, pirates or frozen port.
constexpr uint32_t kFireFlag = 0x8000;
constexpr uint32_t kFamineFlag = 0x1000;
constexpr uint32_t kPlagueFlag = 0x8;
constexpr uint32_t kArmedFlags = 0x810;  // They double the demand for weapons.
// Wares in more demand in each situation, a bit per ware (0x672920..0x67292C).
constexpr uint32_t kCrisisWares = 0x8FF;  // From grain to wine, and timber.
constexpr uint32_t kFireWares = 0x8FF;
constexpr uint32_t kFamineWares = 0xFF;
constexpr uint32_t kPlagueWares = 0x7F;

enum Ware { kGrain = 0, kWine = 7, kCloth = 8, kTimber = 11, kTools = 12, kPitch = 15, kHemp = 17, kBricks = 19 };

}  // namespace

std::array<PriceThresholds, kWareCount> compute_price_thresholds(const ThresholdInputs& in,
	const std::array<PriceThresholds, kWareCount>& previous) {
	std::array<PriceThresholds, kWareCount> t = previous;
	auto at = [&](int ware, int i) -> int32_t& { return reinterpret_cast<int32_t&>(t[size_t(ware)][size_t(i)]); };

	uint32_t demand = 0;
	int32_t t1_days = 14;
	int32_t materials = 1;  // Factor of the building materials.
	if (in.flags & kCrisisFlags) {
		demand = kCrisisWares;
		t1_days = 28;
		materials = 2;
	} else if (in.flags & kFireFlag) {
		demand = kFireWares;
	} else if (in.flags & kFamineFlag) {
		demand = kFamineWares;
	} else if (in.flags & kPlagueFlag) {
		demand = kPlagueWares;
	}

	// t0 and t1 in days of consumption: 7 (14 with more demand) and t1_days more.
	for (int w = 0; w < kBricks; ++w, demand >>= 1) {
		const int32_t t0_days = demand & 1 ? 14 : 7;
		at(w, 0) = t0_days;
		at(w, 1) = t0_days + t1_days;
	}
	for (int w = kBricks; w < kWareCount; ++w) {
		at(w, 0) = 0;
		at(w, 1) = 0;
	}
	at(kGrain, 1) += t1_days;
	for (int w = 0; w < kTradeWareCount; ++w) {
		const int32_t consumption =
			int32_t(in.business_consumption[size_t(w)] + in.citizen_consumption[size_t(w)]) + 1;
		at(w, 0) *= consumption;
		at(w, 1) *= consumption;
	}

	// During a siege, the pitch its army uses (0x5281A0).
	at(kPitch, 0) = int32_t(uint32_t(at(kPitch, 0)) + 7 * in.siege_pitch);
	at(kPitch, 1) = int32_t(uint32_t(at(kPitch, 1)) + 14 * in.siege_pitch);

	// Harvest: from September to December more grain and wine are taken.
	static constexpr int kHarvestDivisor[4][2] = {{3, 20}, {3, 10}, {1, 5}, {1, 10}};
	if (in.month >= 8 && in.month <= 11) {
		const int* f = kHarvestDivisor[in.month - 8];
		at(kGrain, 1) += at(kGrain, 1) * f[0] / f[1];
		at(kWine, 1) += at(kWine, 1) * f[0] / f[1];
	}

	// Building materials.
	at(kBricks, 0) += 80000 * materials;
	at(kPitch, 0) += 1800;
	at(kTimber, 0) += 42000;
	at(kTimber, 1) += 84000;
	at(kBricks, 1) += 160000 * materials;
	at(kPitch, 1) += 3600;
	at(kHemp, 0) += 5000 * materials;
	at(kHemp, 1) += 10000 * materials;
	at(kTools, 0) += 2000 * materials;
	at(kTools, 1) += 4000 * materials;

	// Weapons: by the inhabitants, the consumption and the production of each.
	int32_t armed = int32_t(in.citizens) * 10 / 200;
	if (in.flags & kArmedFlags) {
		armed *= 2;
	}
	for (int w = kTradeWareCount; w < kWareCount; ++w) {
		const int32_t base = std::max(int32_t(in.citizen_consumption[size_t(w)]) * 7, 10);
		at(w, 0) = base + armed;
		at(w, 1) = std::max(3 * base + 2 * armed, 50);
		at(w, 2) = at(w, 1) + 10 * (int32_t(in.production[size_t(w)]) + 1);
		at(w, 3) = at(w, 2) + base + armed;
	}

	for (int w = 0; w < kTradeWareCount; ++w) {
		at(w, 2) = at(w, 1) + 10 * int32_t(in.supply[size_t(w)]);
		at(w, 3) = at(w, 2) + at(w, 0);
	}
	if (int32_t(in.production[kPitch]) > 0) {
		at(kPitch, 3) += 3600;
	}
	if (int32_t(in.production[kBricks]) > 0) {
		at(kBricks, 3) += 160000;
	}
	at(kCloth, 0) += 400;
	at(kCloth, 1) += 800;
	at(kCloth, 2) += 800;
	at(kCloth, 3) += 1600;

	if (in.brickworks > 300) {
		at(kBricks, 2) = at(kBricks, 1) + int32_t(in.supply[kBricks]);
		at(kBricks, 3) = at(kBricks, 2) + int32_t(in.supply[kBricks]);
	} else if (at(kBricks, 2) > 2 * at(kBricks, 1)) {
		at(kBricks, 2) = 2 * at(kBricks, 1);
		at(kBricks, 3) = 3 * at(kBricks, 1);
	}

	// Minimums: each threshold over the one before and at least 5, 10, 15 and
	// 20 barrels (or loads) apart.
	for (int w = 0; w < kTradeWareCount; ++w) {
		const int32_t minimum = kWares[size_t(w)].unit == WareUnit::Load ? 10000 : 1000;
		if (at(w, 0) < minimum) {
			at(w, 0) = minimum;
		}
		int32_t floor = 2 * minimum;
		for (int i = 1; i < 4; ++i, floor += minimum) {
			if (at(w, i) < floor) {
				at(w, i) = floor;
			}
			if (at(w, i) <= at(w, i - 1)) {
				at(w, i) = at(w, i - 1) + minimum;
			}
		}
	}
	return t;
}

}  // namespace p3::data
