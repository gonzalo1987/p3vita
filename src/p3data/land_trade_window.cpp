#include "p3data/land_trade_window.hpp"

#include <algorithm>
#include <cstring>

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kTownIds = 0x6DE4B8;  // u8 per town index: its number
// The 7 roads (0x672BF8): the two towns' numbers, -, the days' sixteenths.
constexpr uint8_t kRoads[7][4] = {{3, 4, 80, 32}, {9, 10, 130, 52}, {11, 13, 30, 12}, {12, 14, 40, 16},
	{28, 31, 120, 48}, {36, 37, 60, 24}, {38, 39, 90, 36}};
constexpr uint8_t kInBarrels[20] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0};  // 0x672C14
constexpr uint32_t kSteps[4] = {1, 5, 50, 0x7FFFFFFF};  // 0x67A18C
// The wares' value per unit (0x673A18; floats).
constexpr float kValue[20] = {0.055000003427267075f, 0.4785500168800354f, 0.22005000710487366f, 0.17399999499320984f,
	0.14249999821186066f, 0.550000011920929f, 1.399999976158142f, 1.100000023841858f, 1.034000039100647f,
	3.382499933242798f, 0.4124999940395355f, 0.027500001713633537f, 1.277999997138977f, 1.1200000047683716f,
	0.4400000274181366f, 0.27799999713897705f, 0.4400000274181366f, 0.2200000137090683f, 0.8549999594688416f,
	0.039900004863739014f};
constexpr float kPastValue = 10.0f;  // 0x673A68, read as the 21st value by the send's sort

uint32_t unit_of(uint32_t ware) {
	return kInBarrels[ware] ? 200 : 2000;
}

int office_of(const SaveGame& save, uint32_t town) {
	return tick::facilities_detail::fn_5308a0(save, save.globals.u32(global::kPlayer), town);
}

// ftol(float(units) · value + 0.5).
uint32_t value_of(uint32_t units, float value) {
	return uint32_t(tick::ftol(double(float(units)) * double(value) + 0.5));
}

// The cost of an escort (or of the danger's level): (slots + 9) / 10 · 50, 200, 800.
uint32_t tier_cost(uint32_t level, uint8_t slots) {
	const int32_t tens = (int32_t(slots) + 9) / 10;
	static constexpr int32_t kTier[3] = {50, 200, 800};
	return uint32_t(tens * kTier[std::min<uint32_t>(level, 2)]);
}

}  // namespace

uint8_t fn_525630(const SaveGame& save, uint32_t town) {
	const GlobalMemory& g = save.globals;
	const TownMemory& t = save.towns[town].memory;
	if (!(t.u32(0x76C) & 0x8000)) {
		return 0xFF;
	}
	const uint8_t number = g.u8(kTownIds + t.u8(0x2C0));
	uint8_t other = 0xFF;
	for (const auto& road : kRoads) {
		if (road[0] == number) {
			other = road[1];
			break;
		}
		if (road[1] == number) {
			other = road[0];
			break;
		}
	}
	if (other >= 0x28) {
		return 0xFF;
	}
	for (uint32_t i = 0; i < g.u32(global::kWorldTownCount) && i < 0x100; ++i) {
		if (g.u8(kTownIds + i) == other) {
			return uint8_t(i);
		}
	}
	return 0xFF;
}

uint8_t fn_533490(const SaveGame& save, uint32_t from, uint32_t to) {
	const GlobalMemory& g = save.globals;
	const uint8_t a = g.u8(kTownIds + (from & 0xFF));
	const uint8_t b = g.u8(kTownIds + (to & 0xFF));
	uint32_t sixteenths = 0x10;
	for (const auto& road : kRoads) {
		if ((road[0] == a && road[1] == b) || (road[0] == b && road[1] == a)) {
			sixteenths *= road[3];
			break;
		}
	}
	return uint8_t((sixteenths + 0x287) >> 8);
}

uint8_t fn_5336b0(const SaveGame& save, uint32_t town, uint8_t& partner) {
	const GlobalMemory& g = save.globals;
	town &= 0xFF;
	const uint32_t count = g.u32(global::kWorldTownCount);
	if (partner >= count) {
		const uint8_t number = g.u8(kTownIds + town);
		uint8_t other = 0xFF;
		for (const auto& road : kRoads) {
			if (road[0] == number) {
				other = road[1];
				break;
			}
			if (road[1] == number) {
				other = road[0];
				break;
			}
		}
		if (other >= 0x28) {
			return 5;
		}
		partner = 0;
		while (g.u8(kTownIds + partner) != other) {
			++partner;
			if (partner >= count) {
				return 5;
			}
		}
	}
	// A barn (0x27) among the town's buildings (its list [+0x764], 0x51F1F0 each).
	auto barn = [&](uint32_t t) {
		const SavedTown& st = save.towns[t];
		const uint32_t records = st.memory.u16(0x776);
		for (uint32_t i = st.memory.u16(0x764); i < records && i < st.records.size(); i = st.records[i].next) {
			if (st.records[i].type == 0x27) {
				return true;
			}
		}
		return false;
	};
	if (!barn(town)) {
		return 1;
	}
	// The game looks in the town's own list again (the town's offset it kept, not the partner's): the partner's barn is
	// never checked, a game bug; fixed behind fix_game_bugs() at the user's word (docs/roadmap.md).
	if (!barn(fix_game_bugs() ? partner : town)) {
		return 2;
	}
	if (save.towns[town].memory.u32(0x2C8) & 0x10) {
		return 3;
	}
	return (save.towns[partner].memory.u32(0x2C8) & 0x10) ? 4 : 0;
}

void land_trade_open(const SaveGame& save, uint32_t town, LandTradeWindow& w) {
	w.page = 0;
	w.from = uint8_t(town);
	w.escorts = 0;
	w.to = fn_525630(save, town);
	w.offices = office_of(save, w.from) >= 0 && w.to < save.towns.size() && office_of(save, w.to) >= 0;
}

void land_trade_update(const SaveGame& save, LandTradeWindow& w) {
	const int from = office_of(save, w.from);
	// The offices with the other end as it was; the other end again after the wares' buttons.
	if (!w.offices && from >= 0 && w.to < save.towns.size() && office_of(save, w.to) >= 0) {
		w.offices = true;
	}
	w.to = fn_525630(save, w.from);
	w.slots = 0;
	w.value = 0;
	for (uint32_t ware = 0; ware < 20; ++ware) {
		const uint32_t unit = unit_of(ware);
		w.left[ware] = from >= 0 ? uint32_t(save.offices[size_t(from)].memory.s32(4 + 4 * ware)) / unit : 0;
		if (w.loaded[ware] > w.left[ware]) {
			w.loaded[ware] = uint8_t(w.left[ware]);
			w.left[ware] = 0;
		} else {
			w.left[ware] -= w.loaded[ware];
		}
		w.slots = uint8_t(w.slots + uint32_t(w.loaded[ware]) * unit / 200);
		w.value += value_of(uint32_t(w.loaded[ware]) * unit, kValue[ware]);
	}
}

void land_trade_load(LandTradeWindow& w, uint32_t ware) {
	uint32_t n = std::min(kSteps[w.step & 3], w.left[ware]);
	const uint32_t room = 100 - w.used;
	if (kInBarrels[ware]) {
		n = std::min(n, room);
		w.used += n;
	} else {
		if (n * 10 > room) {
			n = room / 10;
		}
		w.used += n * 10;
	}
	w.left[ware] -= n;
	w.loaded[ware] = uint8_t(w.loaded[ware] + n);
}

void land_trade_unload(LandTradeWindow& w, uint32_t ware) {
	const int32_t n = std::min<int32_t>(int32_t(kSteps[w.step & 3]), w.loaded[ware]);
	w.left[ware] += uint32_t(n);
	w.loaded[ware] = uint8_t(w.loaded[ware] - n);
	w.used -= kInBarrels[ware] ? uint32_t(n) : uint32_t(n) * 10;
}

void land_trade_costs(const SaveGame& save, LandTradeWindow& w) {
	w.danger = w.value <= uint32_t(w.slots) * 100 ? 0 : w.value <= uint32_t(w.slots) * 400 ? 1 : 2;
	w.cost = tier_cost(w.escorts, w.slots);
	w.days = fn_533490(save, w.from, w.to);
}

std::vector<OperationBytes> land_trade_send(SaveGame& save, LandTradeWindow& w) {
	std::array<uint8_t, 20> lost{};
	bool ambush = false;
	if (w.escorts < w.danger) {
		const uint32_t gap = uint32_t(w.danger - w.escorts);
		if ((tick::game_rand(save) & 3) < gap) {
			ambush = true;
			uint32_t left = (tier_cost(w.danger, w.slots) - w.cost) * 4 / gap;
			// The wares by value, the highest first (an insertion; each one's value read at the place the one before
			// went to: the game's index, 20 for the first).
			std::array<uint8_t, 20> order{};
			std::array<uint32_t, 20> keys{};
			uint32_t at = 20;
			for (uint32_t ware = 0; ware < 20; ++ware) {
				const float v = at < 20 ? kValue[at] : kPastValue;
				const uint32_t k = uint32_t(tick::ftol(double(v) * 2000.0 + 0.5));
				uint32_t pos = ware;
				while (pos > 0 && k > keys[pos - 1]) {
					keys[pos] = keys[pos - 1];
					order[pos] = order[pos - 1];
					--pos;
				}
				order[pos] = uint8_t(ware);
				keys[pos] = k;
				at = pos;
			}
			for (uint32_t rank = 0; rank < 20; ++rank) {
				const uint32_t ware = order[rank];
				const uint32_t per = value_of(unit_of(ware), kValue[rank]);  // The rank's value: the game's index.
				uint32_t take = per ? (left + per - 1) / per : 0;
				uint8_t got = uint8_t(take);
				if (got > w.loaded[ware]) {
					got = w.loaded[ware];
				}
				lost[ware] = got;
				const uint32_t worth = uint32_t(got) * per;
				if (left <= worth) {
					break;
				}
				left -= worth;
			}
		}
	}
	std::vector<OperationBytes> ops;
	bool told = !ambush;
	uint16_t cost = uint16_t(w.cost);
	for (uint32_t ware = 0; ware < 20; ++ware) {
		if (w.loaded[ware] == 0) {
			continue;
		}
		if (ambush && lost[ware] != 0) {
			told = true;
			ambush = false;
		}
		OperationBytes op{};
		op[0] = 0x3A;
		op[4] = w.from;
		op[5] = w.to;
		op[6] = uint8_t(save.globals.u32(global::kPlayer));
		op[7] = told ? 1 : 0;
		const uint16_t amount = w.loaded[ware];
		const uint16_t loss = lost[ware];
		std::memcpy(op.data() + 8, &amount, 2);
		std::memcpy(op.data() + 0xA, &cost, 2);
		std::memcpy(op.data() + 0xC, &loss, 2);
		op[0xE] = uint8_t(ware);
		op[0xF] = w.escorts;
		ops.push_back(op);
		cost = 0;
		told = false;
		w.loaded[ware] = 0;
	}
	w.used = 0;
	return ops;
}

}  // namespace p3::data
