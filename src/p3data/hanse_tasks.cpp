#include "p3data/hanse_tasks.hpp"

#include <algorithm>
#include <cstring>

#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/town_tick/votes.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kEvents = 0x6DD73C;  // The event queue object.
constexpr uint32_t kEventHead = kEvents + 0x8;  // u16 first event.
constexpr uint32_t kEventCount = kEvents + 0xC;  // u16 records.
constexpr uint32_t kAlderman = 0x6DE52D;  // u8 ([+0x8D] of the world).
constexpr uint16_t kTaskEvent = 0x32;
constexpr uint16_t kElectionEvent = 0x20;

// 0x672BF8: the 7 pairs of towns (numbers of StadtDaten.ini) a road can join,
// and its number of sections (the days of the pair); the fourth byte isn't used
// here. The same table as in town_tick/alderman.cpp.
constexpr uint8_t kRoads[7][4] = {{0x03, 0x04, 0x50, 0x20}, {0x09, 0x0A, 0x82, 0x34}, {0x0B, 0x0D, 0x1E, 0x0C},
	{0x0C, 0x0E, 0x28, 0x10}, {0x1C, 0x1F, 0x78, 0x30}, {0x24, 0x25, 0x3C, 0x18}, {0x26, 0x27, 0x5A, 0x24}};

// 0x672C14: the wares that go in barrels (200 units); the others in loads (2000).
constexpr uint8_t kInBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};

uint32_t merchant_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldMerchantCount);
}

// __ftol (0x639C28): truncated to 64 bits, the low 32 (out of range or NaN: 0).
int32_t ftol32(double v) {
	if (!(v > -9223372036854775808.0 && v < 9223372036854775808.0)) {
		return 0;
	}
	return int32_t(uint32_t(uint64_t(int64_t(v))));
}

// Calls visit(index, event) for each event of the queue, in its order.
template <typename Visit>
void walk_events(const SaveGame& save, Visit visit) {
	const GlobalMemory& g = save.globals;
	const uint32_t count = std::min<uint32_t>(g.u16(kEventCount), uint32_t(save.events.size()));
	size_t walked = 0;
	for (uint32_t i = g.u16(kEventHead); i < count && walked < count; i = save.events[i].u16(4), ++walked) {
		if (!visit(uint16_t(i), save.events[i])) {
			return;
		}
	}
}

}  // namespace

HanseTask hanse_task(const SaveGame& save, uint16_t event) {
	HanseTask task;
	if (event >= save.events.size() || save.events[event].u16(6) != kTaskEvent) {
		return task;
	}
	const GameMemory<0x18>& e = save.events[event];
	task.event = event;
	task.deadline = e.u32(0x8);
	task.type = e.u8(0xC);
	task.merchant = e.u8(0xD);
	task.started = e.u8(0xF) != 0;
	for (size_t k = 0; k < task.data.size(); ++k) {
		task.data[k] = e.u8(0x10 + k);
	}
	return task;
}

std::vector<uint16_t> offered_hanse_tasks(const SaveGame& save, size_t max) {
	std::vector<uint16_t> out;
	if (max == 0) {
		return out;
	}
	const uint32_t merchants = merchant_count(save);
	walk_events(save, [&](uint16_t i, const GameMemory<0x18>& e) {
		if (e.u16(6) == kTaskEvent && e.u8(0xD) >= merchants && e.u8(0xC) < 5) {
			out.push_back(i);
		}
		return out.size() < max;
	});
	return out;
}

uint16_t merchant_hanse_task(const SaveGame& save, int merchant) {
	if (merchant < 0 || uint32_t(merchant) >= merchant_count(save) || size_t(merchant) >= save.merchants.size() ||
		save.merchants[size_t(merchant)].memory.u8(0x37) == 0) {
		return 0xFFFF;
	}
	uint16_t found = 0xFFFF;
	walk_events(save, [&](uint16_t i, const GameMemory<0x18>& e) {
		if (e.u16(6) == kTaskEvent && e.u8(0xD) == merchant) {
			found = i;
			return false;
		}
		return true;
	});
	return found;
}

int merchant_as_owner(const SaveGame& save, int merchant) {
	if (merchant < 0 || size_t(merchant) >= save.merchants.size()) {
		return 0;
	}
	const uint16_t first = save.merchants[size_t(merchant)].memory.u16(0xC);
	return first < save.globals.u16(global::kWorldOfficeCount) && first < save.offices.size()
		? save.offices[first].memory.u8(0x2C4)
		: 0;
}

std::vector<int> mayor_towns(const SaveGame& save, int merchant) {
	std::vector<int> towns;
	const int me = merchant_as_owner(save, merchant);
	for (size_t i = 0; i < save.towns.size(); ++i) {
		if (save.towns[i].memory.u8(0x6F1) == me) {
			towns.push_back(int(i));
		}
	}
	return towns;
}

int candidacy_reputation(const SaveGame& save, int merchant) {
	if (merchant < 0 || size_t(merchant) >= save.merchants.size()) {
		return 0;
	}
	// As 0x4F75F0 (town_tick/merchants.cpp), step by step in float.
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	float f = m.f32(0x2FC);
	f = float(double(f) - double(m.f32(0x11C)));
	f = float(double(f) - double(m.f32(0x124)));
	f = float(double(f) - double(m.f32(0x120)));
	f = float(double(f) + double(f));
	const int32_t count = int32_t(save.globals.u32(global::kWorldTownCount));
	for (int32_t i = count - 1; count > 0 && i >= 0 && size_t(i) < save.towns.size(); --i) {
		f = float(double(f) + double(m.f32(0x124 + 12 * size_t(i))));
		if (save.towns[size_t(i)].memory.u32(0x2C8) & 0xC0) {
			f = float(double(f) + double(m.f32(0x11C + 12 * size_t(i))));
			f = float(double(f) + double(m.f32(0x120 + 12 * size_t(i))));
		}
	}
	return int16_t(ftol32(f));
}

int candidacy_level(const SaveGame& save, int merchant) {
	return merchant >= 0 && size_t(merchant) < save.merchants.size() ? save.merchants[size_t(merchant)].memory.u8(0x37)
																	  : 0;
}

int alderman(const SaveGame& save) {
	return save.globals.u8(kAlderman);
}

uint32_t alderman_election_tick(const SaveGame& save) {
	uint32_t tick = 0;
	walk_events(save, [&](uint16_t, const GameMemory<0x18>& e) {
		if (e.u16(6) == kElectionEvent) {
			tick = e.u32(0);
			return false;
		}
		return true;
	});
	return tick;
}

bool road_materials(const SaveGame& save, uint16_t event, int merchant, RoadMaterials& out) {
	out = RoadMaterials();
	if (event >= save.events.size()) {
		return false;
	}
	const GameMemory<0x18>& e = save.events[event];
	if (e.u16(6) != kTaskEvent || e.u8(0xC) != uint8_t(HanseTaskType::Road) || e.u8(0xD) != merchant ||
		e.u8(0x10) >= save.towns.size() || e.u8(0x11) >= save.towns.size()) {
		return false;
	}
	const int a = save.towns[e.u8(0x10)].town_id();
	const int b = save.towns[e.u8(0x11)].town_id();
	for (const uint8_t* pair : kRoads) {
		if (pair[0] != a || pair[1] != b) {
			continue;
		}
		out.sections = pair[2];
		out.sections_left = e.u8(0x12);
		for (size_t k = 0; k < 4; ++k) {
			out.total[k] = kRoadSection[k] * out.sections;
			out.done[k] = kRoadSection[k] * (out.sections - out.sections_left);
			out.delivered[k] = int((((uint32_t(e.u8(0x13)) >> (2 * k)) & 3) << 8) + e.u8(0x14 + k));
		}
		return true;
	}
	return false;
}

std::array<int, 4> available_road_materials(const SaveGame& save, int merchant, int town) {
	std::array<int, 4> out{};
	if (merchant < 0 || size_t(merchant) >= save.merchants.size()) {
		return out;
	}
	// As the road window (0x5F2A5A, 0x5F2B47..0x5F2BFA): the office of
	// 0x5308A0 and unsigned divisions.
	const int office = tick::facilities_detail::fn_5308a0(save, uint32_t(merchant), uint32_t(town));
	const uint32_t ships = save.globals.u16(global::kShipCount);
	for (size_t k = 0; k < 4; ++k) {
		const uint8_t ware = kRoadWares[k];
		const uint32_t unit = kInBarrels[ware] ? 200 : 2000;
		uint32_t n = office >= 0 ? save.offices[size_t(office)].memory.u32(0x4 + 4 * ware) / unit : 0;
		size_t walked = 0;
		for (uint32_t s = save.merchants[size_t(merchant)].memory.u16(0xE); s < ships && s < save.ships.size() &&
			 walked < save.ships.size();
			 s = save.ships[s].memory.u16(0x4), ++walked) {
			const ShipMemory& ship = save.ships[s].memory;
			if (ship.u8(0x39) == town && !(ship.u8(0x136) & 1)) {
				n += ship.u32(0x54 + 4 * ware) / unit;
			}
		}
		out[k] = int(n);
	}
	return out;
}

uint16_t road_window_amount(uint32_t chosen, uint32_t available, uint16_t needed) {
	const uint32_t smaller = chosen < available ? chosen : available;
	return uint16_t(smaller < needed ? smaller : needed);
}

OperationBytes take_task_operation(uint32_t player, uint16_t event) {
	OperationBytes op{};
	const uint32_t fields[3] = {0x4B, player, event};
	std::memcpy(op.data(), fields, sizeof(fields));
	return op;
}

OperationBytes road_materials_operation(uint16_t event, uint16_t amount, uint8_t town, uint8_t player, uint8_t ware) {
	OperationBytes op{};
	const uint32_t code = 0x3B;
	std::memcpy(op.data(), &code, 4);
	std::memcpy(op.data() + 4, &event, 2);
	std::memcpy(op.data() + 6, &amount, 2);
	op[8] = town;
	op[9] = player;
	op[0xA] = ware;
	return op;
}

OperationBytes vote_operation(uint32_t player, uint32_t choice) {
	OperationBytes op{};
	const uint32_t fields[3] = {0x3F, player, choice};
	std::memcpy(op.data(), fields, sizeof(fields));
	return op;
}

uint16_t election_today(const SaveGame& save) {
	const uint16_t i = tick::fn_4d9c70(save);
	return i < save.events.size() && save.events[i].u16(6) == kElectionEvent ? i : 0xFFFF;
}

bool player_can_vote(const SaveGame& save) {
	return election_today(save) != 0xFFFF && tick::fn_4d9d40(save);
}

std::array<ElectionCandidate, 4> election_candidates(const SaveGame& save, uint16_t event) {
	std::array<ElectionCandidate, 4> out{};
	if (event >= save.events.size() || save.events[event].u16(6) != kElectionEvent) {
		return out;
	}
	const GameMemory<0x18>& e = save.events[event];
	for (size_t k = 0; k < 4; ++k) {
		const uint8_t c = e.u8(0x8 + k);
		ElectionCandidate& candidate = out[k];
		candidate.merchant = (c & 0x80) != 0;
		const int index = c & 0x7F;
		candidate.index = candidate.merchant ? (index < int(merchant_count(save)) ? index : -1)
											 : (index < int(save.towns.size()) ? index : -1);
		candidate.votes = e.u8(0xC + k);
	}
	return out;
}

void queue_operation(SaveGame& save, const OperationBytes& operation) {
	save.queued_operations.emplace_back(operation.begin(), operation.end());
}

}  // namespace p3::data
