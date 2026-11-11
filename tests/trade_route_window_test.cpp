// The trade route windows' rules (p3data/trade_route_window.hpp), without game data: the route page (its rows, the
// towns' list, the operations of its buttons) and the wares window of a stop (opening, the direction cycle, ±, the
// bottom buttons), each operation's bytes as the game's windows build them (re/emu/sesion25/trade_routes_notes.md).

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "p3data/trade_route_window.hpp"
#include "p3data/trade_routes.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

uint32_t field(const OperationBytes& op, int k) {
	uint32_t v;
	std::memcpy(&v, op.data() + 4 * k, 4);
	return v;
}

uint16_t half(const OperationBytes& op, int at) {
	uint16_t v;
	std::memcpy(&v, op.data() + at, 2);
	return v;
}

// Three towns (town 2 without a deep port), a merchant (the player, 0) with an office in town 1, one ship (a cog:
// type 2) with a captain and a route of three stops (towns 0, 1, 0), the second the first one (bit 0x4) and the
// third the ship's current stop.
SaveGame world() {
	SaveGame save;
	GlobalMemory& g = save.globals;
	g.u32(global::kWorldTownCount) = 3;
	g.u16(global::kWorldMerchantCount) = 1;
	g.u16(global::kShipCount) = 1;
	g.u16(global::kCaptainCount) = 1;
	g.u32(global::kPlayer) = 0;
	g.u16(global::kWorldOfficeCount) = 1;
	save.towns.resize(3);
	save.towns[2].memory.u32(0x2C8) = 1;
	for (auto& t : save.towns) {
		t.memory.u16(0x784) = 0xFFFF;
	}
	save.towns[1].memory.u16(0x784) = 0;
	save.offices.resize(1);
	save.offices[0].memory.u16(0x2C4) = 0;
	save.offices[0].memory.u16(0x2CA) = 0xFFFF;
	save.merchants.resize(1);
	for (int w = 0; w < 24; ++w) {
		save.merchants[0].memory.u16(0x548 + 4 * w) = uint16_t(100 + w);  // his last buying price
		save.merchants[0].memory.u16(0x54A + 4 * w) = uint16_t(200 + w);  // and selling
	}
	save.ships.resize(1);
	ShipMemory& s = save.ships[0].memory;
	s.u8(0x0) = 0;
	s.u8(0xE) = 2;
	s.u16(0x8) = 0xFFFF;
	s.u16(0x42) = 0;
	s.s32(0x10) = 100000;  // 500 barrels, 50 loads
	s.u16(0x40) = 5;
	for (int k = 0; k < 24; ++k) {
		s.u8(0x13C + k) = 0xFF;
	}
	s.u16(0x132) = 0xFFFF;
	uint8_t op[0x14] = {0x6C, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0};
	fn_53e890(save, op);  // stops 0 → 1 → 2 at town 0, the first 0
	save.trade_routes[1].u8(0x2) = 1;
	save.trade_routes[0].u8(0x3) = 0;
	save.trade_routes[1].u8(0x3) = 4 | 1;  // the first, with repairs
	save.trade_routes[2].u8(0x3) = 8;  // skipped
	s.u16(0x132) = 2;
	return save;
}

void route_page() {
	SaveGame save = world();
	const std::vector<RouteRow> rows = route_rows(save, 0);
	check(rows.size() == 3 && rows[0].stop == 1 && rows[1].stop == 2 && rows[2].stop == 0, "rows from the first");
	check(rows[0].mode == 1 && rows[1].mode == 2 && rows[2].mode == 0, "rows' modes R, -, X");
	check(rows[1].current && !rows[0].current, "the current stop");
	const std::vector<uint8_t> towns = route_town_choices(save, 0);
	check(towns.size() == 2 && towns[0] == 0 && towns[1] == 1, "a cog's towns: none without a deep port");

	// A town for each kind of row.
	auto op = route_town_operation(save, 0, 1, 2);
	check(op && field(*op, 0) == 0x6A && field(*op, 1) == 2 && field(*op, 2) == 0 && field(*op, 3) == 2 &&
		field(*op, 4) == 0, "row 1: its stop's town");
	op = route_town_operation(save, 0, 3, 1);
	check(op && field(*op, 1) == 0 && field(*op, 2) == 1, "past the end: inserted after the last");
	op = route_town_operation(save, 0, 0, 0xFF);
	check(op && field(*op, 1) == 1 && field(*op, 2) == 0 && field(*op, 3) == 0xFF, "row 0 removed");
	check(!route_town_operation(save, 0, 3, 0xFF), "nothing to remove past the end");

	// The modes: R → -, - → X, X → R.
	op = route_mode_operation(save, 0, 0);
	check(op && field(*op, 0) == 0x6D && field(*op, 1) == 1 && field(*op, 2) == (4 | 1 | 8), "R → -");
	op = route_mode_operation(save, 0, 1);
	check(op && field(*op, 1) == 2 && field(*op, 2) == 0, "- → X");
	op = route_mode_operation(save, 0, 2);
	check(op && field(*op, 1) == 0 && field(*op, 2) == 1, "X → R");

	// Moving rows.
	op = route_move_operation(save, 0, 2, 0);
	check(op && field(*op, 0) == 0x6B && field(*op, 1) == 0 && field(*op, 2) == 0 && field(*op, 3) == 1 &&
		field(*op, 4) == 0, "row 2 after row 0");
	op = route_move_operation(save, 0, 1, -1);
	check(op && field(*op, 2) == 2 && field(*op, 3) == 0 && field(*op, 4) == 1, "row 1 on top: after the last, first");
	check(!route_move_operation(save, 0, 1, 1), "dropped on itself");

	// «Activa».
	op = route_active_operation(save, 0);
	check(op && field(*op, 0) == 0x68 && field(*op, 1) == 0 && field(*op, 2) == 1, "on");
	save.ships[0].memory.u8(0x136) = 1;
	op = route_active_operation(save, 0);
	check(op && field(*op, 2) == 0, "off");
	save.ships[0].memory.u8(0x136) = 0;
	save.ships[0].memory.u16(0x42) = 0xFFFF;
	check(!route_active_operation(save, 0), "no captain: nothing");

	// No route: a route of one stop.
	save.ships[0].memory.u16(0x132) = 0xFFFF;
	op = route_town_operation(save, 0, 0, 1);
	check(op && field(*op, 0) == 0x6C && field(*op, 1) == 0 && field(*op, 2) == 1 && field(*op, 3) == 1, "a new route");
	check(route_rows(save, 0).empty(), "no rows without a route");
}

void wares_window() {
	SaveGame save = world();
	TradeStop& t = save.trade_routes[1];
	for (int w = 0; w < 24; ++w) {
		t.s32(0x7C + 4 * w) = 0;
	}
	t.s32(0x7C + 4 * 0) = 4000;  // grain (loads): office → ship, 2 loads
	t.s32(0x1C + 4 * 0) = 0;
	t.s32(0x7C + 4 * 3) = 1000000000;  // beer (barrels): sell «Máx.» down to 77
	t.s32(0x1C + 4 * 3) = 77;
	t.s32(0x7C + 4 * 5) = 600;  // 3 barrels, buy up to 88
	t.s32(0x1C + 4 * 5) = -88;
	t.s32(0x7C + 4 * 7) = -400;  // 2 barrels into the office
	t.s32(0x1C + 4 * 7) = 0;
	StopWindow w = open_stop_window(save, 0, 1, false);
	check(w.state[0] == StopDirection::FromOffice && w.units[0] == 2, "open: grain from the office");
	check(w.state[3] == StopDirection::Sell && w.units[3] == -1 && w.price[3] == 77, "open: beer sold, Máx.");
	check(w.state[5] == StopDirection::Buy && w.units[5] == 3 && w.price[5] == 88, "open: bought");
	check(w.state[7] == StopDirection::ToOffice && w.units[7] == 2, "open: into the office");
	check(w.state[1] == StopDirection::None, "open: nothing");
	check(w.most[3] == 500 && w.most[0] == 50, "the room: 500 barrels, 50 loads");

	OperationBytes op = stop_window_operation(w, 7);
	check(field(op, 0) == 0x69 && half(op, 4) == 1 && half(op, 6) == 0 && half(op, 8) == 7 && half(op, 10) == 7 &&
		int32_t(field(op, 3)) == -400 && field(op, 4) == 0, "0x69 into the office");

	// The cycle: nothing → buy → sell → from the office (town 1 has his office) → into it → nothing.
	op = press_stop_direction(w, save, 1);
	check(w.state[1] == StopDirection::Buy && int32_t(field(op, 4)) == -101 && field(op, 3) == 0, "→ buy");
	op = press_stop_direction(w, save, 1);
	check(w.state[1] == StopDirection::Sell && field(op, 4) == 201, "→ sell");
	op = press_stop_direction(w, save, 1);
	check(w.state[1] == StopDirection::FromOffice && field(op, 4) == 0, "→ from the office");
	op = press_stop_direction(w, save, 1);
	check(w.state[1] == StopDirection::ToOffice, "→ into the office");
	op = press_stop_direction(w, save, 1);
	check(w.state[1] == StopDirection::None && field(op, 3) == 0 && field(op, 4) == 0, "→ nothing");

	// ±.
	check(!stop_amount_step_enabled(w, 3, -1) && stop_amount_step_enabled(w, 3, 1), "Máx.: − off");
	auto step = step_stop_amount(w, 3, 1);
	check(step && w.units[3] == 0 && field(*step, 3) == 0, "Máx. + → 0");
	step = step_stop_amount(w, 3, 1);
	check(step && w.units[3] == 1 && field(*step, 3) == 200, "+1 barrel");
	step_stop_amount(w, 3, -1);
	step = step_stop_amount(w, 3, -1);
	check(step && w.units[3] == -1 && field(*step, 3) == 1000000000, "0 − → Máx.");
	step = step_stop_price(w, 5, 1);
	check(step && w.price[5] == 89 && int32_t(field(*step, 4)) == -89, "a buying price up");
	check(!stop_price_step_enabled(w, 0, 1), "no price from the office");

	// The bottom buttons.
	std::vector<OperationBytes> all = stop_amounts_all(w, false);
	check(all.size() == 4, "«0»: the four wares not in nothing");
	all = press_stop_direction_all(w, save);
	check(all.size() == 20 && w.state[0] == StopDirection::Buy && w.state[19] == StopDirection::Buy &&
		w.all == StopDirection::Buy, "«---»: everything bought");
	all = undo_stop_window(w);
	check(all.size() == 20 && w.state[3] == StopDirection::Sell && w.units[3] == -1, "«Deshaz»");

	// A row dragged.
	const uint8_t ware = w.order[4];
	auto moved = move_stop_row(w, 4, 1);
	check(moved && w.order[1] == ware && half(*moved, 8) == ware && half(*moved, 10) == 1, "a row moved up");
}

// The route files: written from any stop of the route (0x4D5C50: from the first, +0x0 at 0, compressed), read
// back (0x4D5EE0: linked 0 → 1 → 2 → 0) and applied alone (0x5492D0: a new route of as many stops, copied).
void route_files() {
	SaveGame save = world();
	save.trade_routes[1].s32(0x7C + 4 * 3) = 1000000000;
	save.trade_routes[1].s32(0x1C + 4 * 3) = 77;
	const std::optional<std::vector<uint8_t>> file = fn_4d5c50(save, 2);
	check(file && file->size() > 4, "a file written");
	check(!fn_4d5c50(save, 0xFFFF), "no route: no file");
	const std::optional<std::vector<TradeStop>> stops = file ? fn_4d5ee0(*file) : std::nullopt;
	check(stops && stops->size() == 3, "three stops read");
	if (!stops || stops->size() != 3) {
		return;
	}
	const TradeStop& first = (*stops)[0];
	check(first.u8(0x3) == (4 | 1) && first.u8(0x2) == 1 && first.s32(0x1C + 4 * 3) == 77, "from the first stop");
	check((*stops)[0].u16(0x0) == 1 && (*stops)[1].u16(0x0) == 2 && (*stops)[2].u16(0x0) == 0, "linked as the game");
	check((*stops)[1].u8(0x3) == 8, "the second: the skipped one");
	std::vector<uint8_t> bad = *file;
	bad[0] ^= 1;  // a size that isn't whole stops
	check(!fn_4d5ee0(bad), "a size of no whole stops: nothing");

	// Applied alone on the ship (its route off, a new one of three stops, the stops copied).
	save.ships[0].memory.u8(0x136) = 1;
	load_route_file(save, 0, stops);
	check(save.globals.u32(kLoadedRoute) != 0 && save.globals.u32(kLoadedRouteShip) == 0, "loaded for the ship");
	fn_5492d0(save, nullptr);
	check(save.globals.u32(kLoadedRoute) == 0 && save.globals.u32(kLoadedRouteShip) == 0xFFFF && save.loaded_route.empty(),
		"applied and freed");
	check(save.ships[0].memory.u8(0x136) == 0, "the route off");
	const std::vector<RouteRow> rows = route_rows(save, 0);
	check(rows.size() == 3 && rows[0].town == 1 && rows[0].mode == 1 && rows[1].mode == 2 && rows[2].town == 0,
		"the new route as the file's");
	check(save.trade_routes[rows[0].stop].s32(0x1C + 4 * 3) == 77, "its orders copied");
}

}  // namespace

int main() {
	route_page();
	wares_window();
	route_files();
	if (failures == 0) {
		std::printf("trade_route_window_test: all correct\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
