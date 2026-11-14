#include "p3data/building_windows.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace tick::events_towns_detail {
bool fn_4e2314(const SaveGame& save, uint32_t town, uint8_t& merchant, uint32_t& tick);  // town_tick/events_towns.cpp
}  // namespace tick::events_towns_detail

namespace {

constexpr uint32_t kChurchFactor = 0x6DE52C;  // u8 [+0x8C] of the world
constexpr size_t kChurch = 0x794;  // The town's church (0x10 bytes; town_tick/core.cpp)
constexpr uint16_t kChurchCost[4] = {20000, 40000, 60000, 0};  // 0x6734B4
constexpr uint8_t kInBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};  // 0x672C14

bool town_ok(const SaveGame& save, uint32_t town) {
	return town < save.globals.u32(global::kWorldTownCount) && town < save.towns.size();
}

OperationBytes words(std::initializer_list<uint32_t> fields) {
	OperationBytes op{};
	size_t at = 0;
	for (uint32_t f : fields) {
		std::memcpy(op.data() + at, &f, 4);
		at += 4;
	}
	return op;
}

}  // namespace

int church_feeding_level(const SaveGame& save, uint32_t town) {
	if (!town_ok(save, town)) {
		return 0;
	}
	const int32_t unit = (int32_t(save.globals.u8(kChurchFactor)) + 1) * 2000;
	const int32_t level = ((unit >> 1) + save.towns[town].memory.s32(kChurch)) / unit;
	return level > 5 ? 5 : level;
}

int church_extension_state(const SaveGame& save, uint32_t town) {
	if (!town_ok(save, town)) {
		return 2;
	}
	const TownMemory& c = save.towns[town].memory;
	if (c.u8(kChurch + 0xF) >= 3) {
		return 2;
	}
	if (c.s32(kChurch + 4) >= church_extension_cost(save, town)) {
		return 0;
	}
	return c.u32(kChurch + 8) > 100 ? -1 : 1;
}

int32_t church_extension_cost(const SaveGame& save, uint32_t town) {
	if (!town_ok(save, town)) {
		return 0;
	}
	const uint8_t level = save.towns[town].memory.u8(kChurch + 0xF);
	const int32_t factor = save.globals.u8(kChurchFactor);
	// 0x4FE390 (the window's) and 0x4FE420 (the operation's) add the same table's entry of the level (0..2 used).
	return factor * (level == 0 ? 10000 : level == 1 ? 15000 : 20000) + kChurchCost[level & 3];
}

uint8_t church_feeding_kind(const SaveGame& save, uint32_t town, const std::array<uint16_t, 5>& amounts) {
	if (!town_ok(save, town)) {
		return 0;
	}
	int32_t worth = 0;
	for (int k = 0; k < 5; ++k) {
		// The window prices its field k as the ware k (0x5CAF83): the fifth, wine, as salt (a game bug; fixed behind
		// fix_game_bugs(), the user's choice, 2026-11-12: priced as the wine it gives).
		const int ware = fix_game_bugs() ? kFeedingWares[size_t(k)] : k;
		const int32_t units = int32_t(amounts[size_t(k)]) * (kInBarrels[ware] ? 200 : 2000);
		worth = int32_t(uint32_t(worth) + uint32_t(tick::game_sell_price(save, ware, int(town), units)));
	}
	const TownMemory& t = save.towns[town].memory;
	const int32_t people = int32_t(uint32_t(int32_t(t.s16(0x304))) * t.u32(0x2D4)) / 18;
	const double root = std::sqrt(double(people));
	const int32_t divisor = int32_t(std::isnan(root) ? INT32_MIN : int32_t(root)) + 8;
	const int32_t kind = divisor != 0 ? worth / divisor : 0;
	return kind > 0xFF ? 0xFF : uint8_t(kind);
}

OperationBytes feeding_operation(const SaveGame& save, uint32_t player, uint32_t town,
	const std::array<uint16_t, 5>& amounts) {
	OperationBytes op{};
	op[0] = 0x30;
	op[4] = uint8_t(player);
	op[5] = uint8_t(town);
	op[6] = church_feeding_kind(save, town, amounts);
	// The rows into the operation's slots (0x5CB116..0x5CB19E): grain, beer, fish, meat, wine (0x6734CC).
	const uint16_t order[5] = {amounts[0], amounts[3], amounts[2], amounts[1], amounts[4]};
	std::memcpy(op.data() + 8, order, sizeof(order));
	return op;
}

OperationBytes church_donation_operation(uint32_t code, uint32_t player, uint32_t town, int32_t amount) {
	return words({code, player, town, uint32_t(amount)});
}

int bath_state(const SaveGame& save, uint32_t merchant) {
	if (merchant >= save.merchants.size()) {
		return 2;
	}
	const uint32_t since = save.globals.u32(global::kWorldTick) - save.merchants[merchant].memory.u32(0x4);
	if ((since & 0xFFFFFF00u) < 0x600) {
		return 0;
	}
	return (since & 0xFFFFFF00u) <= 0x1500 ? 1 : 2;
}

OperationBytes bath_operation(uint32_t player, uint32_t tick) {
	return words({0x39, player, tick});
}

uint32_t councillor_price(SaveGame& save, uint32_t town, uint32_t merchant, uint32_t slot) {
	static constexpr uint8_t kByRank[8] = {0, 1, 2, 3, 5, 7, 10, 15};  // 0x529E6E..0x529E9F
	if ((merchant & 0xFFFF) >= save.globals.u16(global::kWorldMerchantCount) || (slot & 0xFF) >= 4 ||
		!town_ok(save, town)) {
		return 0x80000000u;
	}
	const MerchantMemory& m = save.merchants[merchant & 0xFFFF].memory;
	const uint8_t rank = m.u8(0x39C + m.u8(0x19));
	const uint32_t f = rank < 8 ? kByRank[rank] : (merchant & 0xFF);
	const int32_t roll = int32_t(tick::game_rand(save)) % 11;
	uint32_t price = (uint32_t(roll) + f * 4 + 0x10) * 500;
	if (save.towns[town].memory.u8(0x6DC + (slot & 0xFF)) < save.globals.u16(global::kWorldMerchantCount)) {
		price *= 2;
	}
	return price;
}

OperationBytes bribe_operation(SaveGame& save, uint32_t town, uint32_t player, uint32_t slot, uint32_t offer,
	uint32_t* rolled) {
	const uint32_t price = councillor_price(save, town, player, slot);
	if (rolled) {
		*rolled = price;
	}
	if (price > offer) {
		return words({0x43, slot, player, 0, town});
	}
	return words({0x42, slot, player, offer, town});
}

int bribe_reaction(uint32_t price, uint32_t offer) {
	if (price > offer) {
		return 2;
	}
	// fild qword {price, 0}, fmul 1.5, __ftol: the low 32 bits.
	const uint32_t enticing = uint32_t(int64_t(double(price) * 1.5));
	return offer < enticing ? 0 : 1;
}

// --- A feast ---

std::vector<uint32_t> feast_delays(const SaveGame& save) {
	const uint32_t towns = std::min<uint32_t>(save.globals.u32(global::kWorldTownCount), uint32_t(save.towns.size()));
	std::vector<uint32_t> delays(towns);
	for (uint32_t t = 0; t < towns; ++t) {
		delays[t] = uint32_t((save.towns[t].memory.s32(0x2D4) % 11) + 5) << 9;  // idiv: the citizens' sign kept
	}
	return delays;
}

namespace {

uint32_t feast_delay(const std::vector<uint32_t>& delays, uint32_t town) {
	return town < delays.size() ? delays[town] : 0;
}

}  // namespace

FeastWindow feast_open(const SaveGame& save, uint32_t town, const std::vector<uint32_t>& delays) {
	FeastWindow w;
	w.town = town;
	w.merchant = uint8_t(save.globals.u32(global::kPlayer));
	w.date = save.globals.u32(global::kWorldTick) + feast_delay(delays, town);
	w.announced = tick::events_towns_detail::fn_4e2314(save, town, w.merchant, w.date);
	return w;
}

void feast_update(const SaveGame& save, FeastWindow& w, const std::vector<uint32_t>& delays) {
	w.office = town_ok(save, w.town) &&
		tick::facilities_detail::fn_5308a0(save, save.globals.u32(global::kPlayer), w.town) >= 0;
	w.announced = tick::events_towns_detail::fn_4e2314(save, w.town, w.merchant, w.date);
	const uint32_t now = save.globals.u32(global::kWorldTick);
	if (w.date < now) {
		w.date = now + feast_delay(delays, w.town);
	}
	w.cost = town_ok(save, w.town) ? save.towns[w.town].memory.s32(0x2D4) / 2 + 100 : 100;
}

bool feast_can_announce(const SaveGame& save, const FeastWindow& w) {
	return !w.announced && w.office && w.merchant < save.merchants.size() &&
		save.merchants[w.merchant].memory.u32(0) >= uint32_t(w.cost);
}

OperationBytes feast_operation(const FeastWindow& w, uint32_t player) {
	OperationBytes op{};
	const uint32_t fields[5] = {0x61, player, w.date, w.town, uint32_t(w.cost)};
	std::memcpy(op.data(), fields, sizeof fields);
	return op;
}

}  // namespace p3::data
