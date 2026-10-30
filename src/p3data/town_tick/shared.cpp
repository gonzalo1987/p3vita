// Funciones pequeñas que usan varias partes de la simulación de las ciudades.

#include <algorithm>
#include <cstring>

#include "p3data/thresholds.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

uint32_t game_rand(SaveGame& save) {
	save.rand_state = save.rand_state * 0x343FD + 0x269EC3;
	return (save.rand_state >> 16) & 0x7FFF;
}

void enqueue_operation(SaveGame& save, const uint8_t* operation, size_t size) {
	std::vector<uint8_t> op(0x14);
	std::memcpy(op.data(), operation, std::min(size, op.size()));
	save.queued_operations.push_back(std::move(op));
}

void fn_529f90(SaveGame& save, int town, int32_t amount, uint32_t kind) {
	TownMemory& t = save.towns[size_t(town)].memory;
	if (kind >= 11) {
		return;
	}
	t.s32(0x704 + 4 * kind) += amount;
	if (kind >= 5) {
		t.s32(0x700) -= amount;
		return;
	}
	t.s32(0x700) += amount;
	if (kind != 2) {
		return;
	}
	const uint32_t last = t.u32(0x6F8);
	const uint32_t tick = save.globals.u32(global::kWorldTick);
	if (tick > last + 0x5400) {
		t.s32(0x6F4) = amount;
	} else {
		t.s32(0x6F4) += amount;
	}
	t.u32(0x6F8) = (tick & ~0xFFu) | (last & 0xFF);
}

bool fn_4fcfe0(const SaveGame& save, int town, uint32_t i) {
	const TownMemory& t = save.towns[size_t(town)].memory;
	if (i > 3) {
		return false;
	}
	return int32_t(t.u8(0x7A4 + 0xC + 2 * i)) < t.s32(0x7A4 + 4) && int32_t(t.u8(0x7A4 + 0xD + 2 * i)) < t.s32(0x7A4 + 8);
}

MarketState market_state(const SaveGame& save, int town, int ware) {
	const TownMemory& t = save.towns[size_t(town)].memory;
	MarketState m;
	m.stock = t.s32(4 + 4 * size_t(ware));
	for (size_t i = 0; i < 4; ++i) {
		m.thresholds[i] = t.u32(0x4F0 + 16 * size_t(ware) + 4 * i);
	}
	m.open = t.u32(0x76C) & 0x100;
	m.taxed = t.u8(0x6F0) == ware;
	return m;
}

int32_t game_buy_price(const SaveGame& save, int ware, int town, int32_t amount) {
	return buy_price(ware, market_state(save, town, ware), amount);
}

int32_t game_sell_price(const SaveGame& save, int ware, int town, int32_t amount) {
	return sell_price(ware, market_state(save, town, ware), amount, save.trade_difficulty);
}

void fn_528070(SaveGame& save, int town, bool compute) {
	TownMemory& t = save.towns[size_t(town)].memory;
	std::array<PriceThresholds, kWareCount> thresholds;
	for (size_t w = 0; w < kWareCount; ++w) {
		for (size_t i = 0; i < 4; ++i) {
			thresholds[w][i] = t.u32(0x4F0 + 16 * w + 4 * i);
		}
	}
	if (compute) {
		ThresholdInputs in;
		in.flags = t.u32(0x2C8);
		in.citizens = t.u32(0x2D4);
		for (size_t w = 0; w < kWareCount; ++w) {
			in.business_consumption[w] = t.u32(0x64 + 4 * w);
			in.production[w] = t.u32(0xC4 + 4 * w);
			in.citizen_consumption[w] = t.u32(0x310 + 4 * w);
			in.supply[w] = t.u32(0x490 + 4 * w);
		}
		in.month = save.globals.u8(global::kWorldMonth);
		in.brickworks = t.u16(0x968);
		thresholds = compute_price_thresholds(in, thresholds);
	} else {
		for (size_t w = 0; w < size_t(kTradeWareCount) - 1; ++w) {
			thresholds[w] = {1, 2, 3, 4};
		}
	}
	for (size_t w = 0; w < kWareCount; ++w) {
		for (size_t i = 0; i < 4; ++i) {
			t.u32(0x4F0 + 16 * w + 4 * i) = thresholds[w][i];
		}
	}
}

}  // namespace p3::data::tick
