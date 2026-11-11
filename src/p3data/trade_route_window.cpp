#include "p3data/trade_route_window.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "p3data/town_tick/internal.hpp"
#include "p3data/trade_routes.hpp"

namespace p3::data {

namespace {

using namespace global;

constexpr uint32_t kStopCount = 0x6DD72A;  // u16: the stops' records (trade_routes.hpp)
constexpr int32_t kMost = 1000000000;  // «Máx.»
// 0x672C14: the wares in barrels (200 units); the others in loads (2000).
constexpr uint8_t kInBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
constexpr int32_t kArtilleryWeight[6] = {1000, 1000, 2000, 2000, 2000, 1000};  // 0x672CB4
constexpr int32_t kLeastSailors[4] = {10, 16, 30, 24};  // 0x673664
constexpr int32_t kTopPrice = 10000;  // 0x2710: the price box's limit

OperationBytes operation(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
	OperationBytes op{};
	const uint32_t f[5] = {a, b, c, d, e};
	std::memcpy(op.data(), f, sizeof(f));
	return op;
}

uint32_t stop_count(const SaveGame& save) {
	return save.globals.u16(kStopCount);
}

bool valid_stop(const SaveGame& save, uint32_t stop) {
	return stop < stop_count(save) && stop < save.trade_routes.size();
}

uint16_t next_stop(const SaveGame& save, uint32_t stop) {
	return valid_stop(save, stop) ? save.trade_routes[stop].u16(0x0) : 0xFFFF;
}

// The route's first stop from `stop` (the bit 0x4; with none, `stop` itself), as the page's loops find it.
uint16_t first_stop(const SaveGame& save, uint16_t stop) {
	uint16_t s = stop;
	for (size_t n = 0; n <= save.trade_routes.size(); ++n) {
		if (!valid_stop(save, s)) {
			return stop;
		}
		if (save.trade_routes[s].u8(0x3) & 4) {
			return s;
		}
		s = next_stop(save, s);
		if (s == stop) {
			return stop;
		}
	}
	return stop;
}

int32_t unit_of(int ware) {
	return kInBarrels[ware] ? 200 : 2000;
}

// 0x518460 (on a ship, a unit): its room for cargo in that unit: the capacity less the guns, the hand weapons and
// the sailors over its type's least.
uint32_t ship_room(const ShipMemory& s, uint32_t unit) {
	int32_t room = s.s32(0x10);
	for (int k = 0; k < 24; ++k) {
		const uint8_t gun = s.u8(0x13C + k);
		if (gun < 6) {
			room -= kArtilleryWeight[gun];
		}
	}
	room -= 10 * int32_t(s.u16(0x154));
	const int32_t least = kLeastSailors[s.u8(0xE) & 3];
	if (int32_t(s.u16(0x40)) > least) {
		room += (least - int32_t(s.u16(0x40))) * 400;
	}
	return uint32_t(room) / unit;
}

// 0x50A2B0 (the ship object: a convoy, a unit): the room of the local player's ships in it.
uint32_t convoy_room(const SaveGame& save, uint32_t convoy, uint32_t unit) {
	if (convoy >= save.convoy_count() || convoy >= save.convoys.size()) {
		return 0;
	}
	uint32_t sum = 0;
	const uint32_t ships = save.globals.u16(kShipCount);
	for (uint32_t i = save.convoys[convoy].u16(0xA); i < ships && i < save.ships.size();
		 i = save.ships[i].memory.u16(0x6)) {
		const ShipMemory& m = save.ships[i].memory;
		if (m.u8(0x0) == save.globals.u32(kPlayer)) {
			sum += ship_room(m, unit);
		}
	}
	return sum;
}

bool has_office(const SaveGame& save, const StopWindow& w) {
	if (!valid_stop(save, w.stop)) {
		return false;
	}
	return tick::facilities_detail::fn_5308a0(save, save.globals.u32(kPlayer), save.trade_routes[w.stop].u8(0x2)) >= 0;
}

uint16_t last_price(const SaveGame& save, const StopWindow& w, int ware, bool buy) {
	if (w.owner >= save.merchants.size()) {
		return 0;
	}
	return save.merchants[w.owner].memory.u16((buy ? 0x548 : 0x54A) + 4 * ware);
}

// 0x407CA0's cases: the state a ware goes to from `from`, with its price box set as the game sets it.
void next_direction(StopWindow& w, const SaveGame& save, int ware, StopDirection from) {
	switch (from) {
	case StopDirection::None:  // case 4
		w.state[ware] = StopDirection::Buy;
		w.price[ware] = last_price(save, w, ware, true);
		break;
	case StopDirection::Buy:  // case 3
		w.state[ware] = StopDirection::Sell;
		w.price[ware] = last_price(save, w, ware, false);
		break;
	case StopDirection::Sell:  // case 1
		w.state[ware] = has_office(save, w) ? StopDirection::FromOffice : StopDirection::None;
		break;
	case StopDirection::FromOffice:  // case 2
		if (has_office(save, w)) {
			w.state[ware] = StopDirection::ToOffice;
		} else {
			// Only if the office went while the window was open (this state needs one): the game leaves the price
			// out of its operation here, so 0.
			w.state[ware] = StopDirection::Sell;
			w.price[ware] = 0;
		}
		break;
	case StopDirection::ToOffice:  // case 0
		w.state[ware] = StopDirection::None;
		break;
	}
}

}  // namespace

int route_ship(const SaveGame& save, int ship) {
	const ShipMemory& s = save.ships[size_t(ship)].memory;
	if (s.u16(0x8) < save.convoy_count() && s.u16(0x8) < save.convoys.size()) {
		return save.convoys[s.u16(0x8)].u16(0x10);
	}
	return ship;
}

std::vector<RouteRow> route_rows(const SaveGame& save, int ship) {
	std::vector<RouteRow> rows;
	const int r = route_ship(save, ship);
	if (r < 0 || size_t(r) >= save.ships.size()) {
		return rows;
	}
	const uint16_t own = save.ships[size_t(r)].memory.u16(0x132);
	if (!valid_stop(save, own)) {
		return rows;
	}
	const uint16_t first = first_stop(save, own);
	uint16_t s = first;
	do {
		const TradeStop& t = save.trade_routes[s];
		RouteRow row;
		row.stop = s;
		row.town = t.u8(0x2);
		row.mode = (t.u8(0x3) & 8) ? 2 : (t.u8(0x3) & 1) ? 1 : 0;
		row.current = s == own;
		rows.push_back(row);
		s = next_stop(save, s);
	} while (s != first && valid_stop(save, s) && rows.size() < 20);
	return rows;
}

std::vector<uint8_t> route_town_choices(const SaveGame& save, int ship) {
	const ShipMemory& s = save.ships[size_t(ship)].memory;
	int type = s.u8(0xE);
	if (s.u16(0x8) < save.convoy_count() && s.u16(0x8) < save.convoys.size()) {
		const uint32_t ships = save.globals.u16(kShipCount);
		for (uint32_t i = save.convoys[s.u16(0x8)].u16(0xA); i < ships && i < save.ships.size();
			 i = save.ships[i].memory.u16(0x6)) {
			type = std::max<int>(type, save.ships[i].memory.u8(0xE));
		}
	}
	std::vector<uint8_t> towns;
	const uint32_t n = save.globals.u32(kWorldTownCount);
	for (uint32_t t = 0; t < n && t < save.towns.size(); ++t) {
		if (type >= 2 && (save.towns[t].memory.u32(0x2C8) & 1)) {
			continue;
		}
		towns.push_back(uint8_t(t));
	}
	return towns;
}

std::optional<OperationBytes> route_town_operation(const SaveGame& save, int ship, int row, uint8_t town) {
	const int r = route_ship(save, ship);
	const uint32_t towns = save.globals.u32(kWorldTownCount);
	const uint16_t own = save.ships[size_t(r)].memory.u16(0x132);
	if (!(own < stop_count(save))) {
		if (town >= towns) {
			return std::nullopt;
		}
		return operation(0x6C, uint32_t(r), town, 1, 0);
	}
	const uint16_t first = first_stop(save, own);
	uint16_t s = first;
	uint16_t at = first;
	int steps = 0;
	for (;;) {
		at = s;
		if (steps == row) {
			break;
		}
		s = next_stop(save, s);
		++steps;
		if (s == first || !valid_stop(save, s)) {
			break;
		}
	}
	const bool wrapped = s == first;
	const uint32_t insert = (wrapped && steps != 0) ? 1 : 0;
	if (!(town < towns || !wrapped || row == 0)) {
		return std::nullopt;  // removing past the end
	}
	return operation(0x6A, at, insert, town, uint32_t(r));
}

std::optional<OperationBytes> route_mode_operation(const SaveGame& save, int ship, int row) {
	const int r = route_ship(save, ship);
	const uint16_t own = save.ships[size_t(r)].memory.u16(0x132);
	if (!valid_stop(save, own)) {
		return std::nullopt;
	}
	const uint16_t first = first_stop(save, own);
	uint16_t s = first;
	for (int k = 0; k != row;) {
		s = next_stop(save, s);
		++k;
		if (s == first || !valid_stop(save, s)) {
			break;
		}
	}
	if (!valid_stop(save, s)) {
		return std::nullopt;
	}
	// The next mode: «X» → «R» → «-» → «X» (0x4936A5..0x4936E7).
	const uint8_t action = save.trade_routes[s].u8(0x3);
	uint8_t next;
	if (action & 8) {
		next = action & 0xF6;
	} else if (action & 1) {
		next = action | 8;
	} else {
		next = uint8_t((action & 0xF7) | 1);
	}
	return operation(0x6D, s, next, 0, 0);
}

std::optional<OperationBytes> route_move_operation(const SaveGame& save, int ship, int row, int target) {
	if (target == row) {
		return std::nullopt;
	}
	const int r = route_ship(save, ship);
	const uint16_t own = save.ships[size_t(r)].memory.u16(0x132);
	if (!valid_stop(save, own)) {
		return std::nullopt;
	}
	const uint16_t first = first_stop(save, own);
	uint32_t a = 0;
	uint32_t b = 0;
	uint16_t last = first;
	uint16_t s = first;
	int k = 0;
	do {
		if (k == row) {
			a = s;
		}
		if (k == target) {
			b = s;
		}
		last = s;
		++k;
		s = next_stop(save, s);
	} while (s != first && valid_stop(save, s));
	uint32_t top = 0;
	if (target == -1) {
		b = last;
		top = 1;
	}
	return operation(0x6B, uint32_t(r), a, b, top);
}

uint16_t route_file_stop(const SaveGame& save, int ship) {
	const ShipMemory& s = save.ships[size_t(ship)].memory;
	const uint16_t convoy = s.u16(0x8);
	if (convoy < save.globals.u16(kShipCount) && convoy < save.convoys.size()) {
		const uint16_t guide = save.convoys[convoy].u16(0x10);
		return guide < save.ships.size() ? save.ships[guide].memory.u16(0x132) : uint16_t(0xFFFF);
	}
	return s.u16(0x132);
}

bool route_active(const SaveGame& save, int ship) {
	return (save.ships[size_t(route_ship(save, ship))].memory.u8(0x136) & 1) != 0;
}

std::optional<OperationBytes> route_active_operation(const SaveGame& save, int ship) {
	const int r = route_ship(save, ship);
	const ShipMemory& s = save.ships[size_t(r)].memory;
	if (s.u8(0x136) & 1) {
		return operation(0x68, uint32_t(ship), 0, 0, 0);
	}
	// (The game first checks the selected ships with its interface, 0x454F80, which shows its own messages.)
	if (s.u16(0x42) < save.captain_count() || s.u16(0x8) < save.convoy_count()) {
		return operation(0x68, uint32_t(r), 1, 0, 0);
	}
	return std::nullopt;
}

StopWindow open_stop_window(const SaveGame& save, int ship, uint16_t stop, bool convoy) {
	StopWindow w;
	w.ship = ship;
	w.stop = stop;
	w.convoy = convoy;
	const ShipMemory& s = save.ships[size_t(ship)].memory;
	w.owner = s.u8(0x0);
	TradeStop t;
	if (stop < save.trade_routes.size()) {
		t = save.trade_routes[stop];
	}
	uint32_t barrels;
	uint32_t loads;
	if (convoy) {
		barrels = convoy_room(save, s.u16(0x8), 200);
		loads = convoy_room(save, s.u16(0x8), 2000);
	} else {
		barrels = ship_room(s, 200);
		loads = uint32_t(int32_t(barrels * 200) / 2000);
	}
	for (int k = 0; k < 20; ++k) {
		w.order[k] = t.u8(0x4 + k);
	}
	for (int ware = 0; ware < 20; ++ware) {
		const int32_t amount = t.s32(0x7C + 4 * ware);
		const int32_t price = t.s32(0x1C + 4 * ware);
		if (amount == 0) {
			w.state[ware] = StopDirection::None;
		} else if (price < 0) {
			w.state[ware] = StopDirection::Buy;
		} else if (price > 0) {
			w.state[ware] = StopDirection::Sell;
		} else if (amount > 0) {
			w.state[ware] = StopDirection::FromOffice;
		} else {
			w.state[ware] = StopDirection::ToOffice;
		}
		const int32_t magnitude = std::abs(amount);
		w.units[ware] = magnitude == kMost ? -1 : magnitude / unit_of(ware);
		w.price[ware] = std::abs(price);
		w.most[ware] = int32_t(kInBarrels[ware] ? barrels : loads);
	}
	w.saved_order = w.order;
	w.saved_state = w.state;
	w.saved_units = w.units;
	w.saved_price = w.price;
	return w;
}

int stop_window_row(const StopWindow& w, int ware) {
	for (int k = 0; k < 20; ++k) {
		if (w.order[k] == ware) {
			return k;
		}
	}
	return 20;
}

OperationBytes stop_window_operation(const StopWindow& w, int ware) {
	int32_t amount = w.units[ware] == -1 ? kMost : w.units[ware] * unit_of(ware);
	int32_t price = 0;
	switch (w.state[ware]) {
	case StopDirection::ToOffice: amount = -amount; break;
	case StopDirection::Sell: price = w.price[ware]; break;
	case StopDirection::Buy: price = -w.price[ware]; break;
	case StopDirection::FromOffice: break;
	case StopDirection::None: amount = 0; break;
	}
	OperationBytes op{};
	const uint16_t head[4] = {w.stop, w.owner, uint16_t(ware), uint16_t(stop_window_row(w, ware))};
	const uint32_t code = 0x69;
	std::memcpy(op.data(), &code, 4);
	std::memcpy(op.data() + 4, head, 8);
	std::memcpy(op.data() + 0xC, &amount, 4);
	std::memcpy(op.data() + 0x10, &price, 4);
	return op;
}

OperationBytes press_stop_direction(StopWindow& w, const SaveGame& save, int ware) {
	next_direction(w, save, ware, w.state[ware]);
	return stop_window_operation(w, ware);
}

std::vector<OperationBytes> press_stop_direction_all(StopWindow& w, const SaveGame& save) {
	// The bottom button's own state (0x405703..0x405A08): every ware goes the way it shows, as one.
	std::vector<OperationBytes> ops;
	const StopDirection from = w.all;
	for (int ware = 0; ware < 20; ++ware) {
		next_direction(w, save, ware, from);
		ops.push_back(stop_window_operation(w, ware));
	}
	switch (from) {
	case StopDirection::None: w.all = StopDirection::Buy; break;
	case StopDirection::Buy: w.all = StopDirection::Sell; break;
	case StopDirection::Sell: w.all = has_office(save, w) ? StopDirection::FromOffice : StopDirection::None; break;
	case StopDirection::FromOffice: w.all = has_office(save, w) ? StopDirection::ToOffice : StopDirection::Sell; break;
	case StopDirection::ToOffice: w.all = StopDirection::None; break;
	}
	return ops;
}

bool stop_amount_step_enabled(const StopWindow& w, int ware, int delta) {
	if (w.state[ware] == StopDirection::None) {
		return false;
	}
	return delta > 0 ? w.units[ware] != w.most[ware] : w.units[ware] != -1;
}

bool stop_price_step_enabled(const StopWindow& w, int ware, int delta) {
	if (w.state[ware] != StopDirection::Sell && w.state[ware] != StopDirection::Buy) {
		return false;
	}
	return delta > 0 ? w.price[ware] < kTopPrice : w.price[ware] > 1;
}

std::optional<OperationBytes> step_stop_amount(StopWindow& w, int ware, int delta) {
	if (!stop_amount_step_enabled(w, ware, delta)) {
		return std::nullopt;
	}
	int32_t units;
	if (delta > 0) {
		units = w.units[ware] == -1 ? 0 : w.units[ware] + 1;  // from «Máx.», 0
	} else {
		units = w.units[ware] - 1;  // from 0, «Máx.»
	}
	if (units != -1 && units > w.most[ware]) {
		units = w.most[ware];  // the game cuts the amount to the room
	}
	w.units[ware] = units;
	return stop_window_operation(w, ware);
}

std::optional<OperationBytes> step_stop_price(StopWindow& w, int ware, int delta) {
	if (!stop_price_step_enabled(w, ware, delta)) {
		return std::nullopt;
	}
	int32_t price = w.price[ware] + (delta > 0 ? 1 : -1);
	if (price > kTopPrice) {
		price = kTopPrice;
	}
	if (price < 1) {
		price = 1;
	}
	w.price[ware] = price;
	return stop_window_operation(w, ware);
}

OperationBytes set_stop_amount(StopWindow& w, int ware, int32_t units) {
	w.units[ware] = units < -1 ? -1 : units;
	return stop_window_operation(w, ware);
}

OperationBytes set_stop_price(StopWindow& w, int ware, int32_t price) {
	w.price[ware] = price < 1 ? 1 : price > kTopPrice ? kTopPrice : price;
	return stop_window_operation(w, ware);
}

std::vector<OperationBytes> stop_amounts_all(StopWindow& w, bool most) {
	std::vector<OperationBytes> ops;
	const int32_t to = most ? -1 : 0;
	for (int ware = 0; ware < 20; ++ware) {
		if (w.state[ware] == StopDirection::None || w.units[ware] == to) {
			continue;
		}
		w.units[ware] = to;
		ops.push_back(stop_window_operation(w, ware));
	}
	return ops;
}

std::vector<OperationBytes> undo_stop_window(StopWindow& w) {
	w.order = w.saved_order;
	w.state = w.saved_state;
	w.units = w.saved_units;
	w.price = w.saved_price;
	w.all = StopDirection::None;
	std::vector<OperationBytes> ops;
	for (int ware = 0; ware < 20; ++ware) {
		ops.push_back(stop_window_operation(w, ware));
	}
	return ops;
}

std::optional<OperationBytes> move_stop_row(StopWindow& w, int from, int to) {
	if (from == to || from < 0 || from >= 20 || to < 0 || to >= 20) {
		return std::nullopt;
	}
	const uint8_t ware = w.order[from];
	if (from < to) {
		for (int k = from; k < to; ++k) {
			w.order[k] = w.order[k + 1];
		}
	} else {
		for (int k = from; k > to; --k) {
			w.order[k] = w.order[k - 1];
		}
	}
	w.order[to] = ware;
	if (ware >= 20) {
		return std::nullopt;
	}
	return stop_window_operation(w, ware);
}

}  // namespace p3::data
