#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "p3data/player_actions.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// What the game's trade route windows decide and the operations they queue
// (re/emu/sesion25/trade_routes_notes.md, «Part 3»), for the viewer: the
// route page of the ship menu (CSideMenu_Ship's page [+0xE4], 0x48B3E0) and
// the wares window of a stop (*0x6CBA74: 0x405A20, 0x404B50, 0x406D50,
// 0x407CA0). The operations themselves are trade_routes.hpp's 0x68..0x6D.

// --- The route page ---

// The ship whose route the page shows: the ship, or its convoy's guide.
int route_ship(const SaveGame& save, int ship);
// The route's stops from its first (the bit 0x4; with none, the ship's own
// stop), as the page lists them.
struct RouteRow {
	uint16_t stop = 0;
	uint8_t town = 0;
	// The stop's mode, its button's text: 0 «X» (normal), 1 «R» (repairs, the
	// bit 1), 2 «-» (skipped, the bit 8).
	uint8_t mode = 0;
	bool current = false;  // the ship's stop now (its number red)
};
std::vector<RouteRow> route_rows(const SaveGame& save, int ship);
// The towns the page's list offers for a stop (every town, without those
// without a deep port, [+0x2C8] & 1, when the ship or the biggest of its
// convoy has [+0xE] ≥ 2); the list adds one entry more, which removes the stop
// (town 0xFF).
std::vector<uint8_t> route_town_choices(const SaveGame& save, int ship);
// A town chosen for the page's row `row` (0x492CA0): the row's stop gets it
// (0x6A), a row past the end appends a stop (0x6A with insert), 0xFF removes
// the row's stop (nothing past the end); without a route, a route of one stop
// (0x6C). Nothing when there's nothing to do.
std::optional<OperationBytes> route_town_operation(const SaveGame& save, int ship, int row, uint8_t town);
// The row's mode button (0x48C4D6): the next mode, 0x6D.
std::optional<OperationBytes> route_mode_operation(const SaveGame& save, int ship, int row);
// A row dragged and dropped (0x492EE0): `target` is the row it goes after
// (−1: on top, it becomes the first). Nothing when it stays.
std::optional<OperationBytes> route_move_operation(const SaveGame& save, int ship, int row, int target);
// «Activa» (0x48CD31): the route on → off; off → on with a captain or in a
// convoy. Nothing otherwise.
std::optional<OperationBytes> route_active_operation(const SaveGame& save, int ship);
bool route_active(const SaveGame& save, int ship);
// The stop whose route «File» saves (0x48C9A0): the ship's, or its convoy's
// guide's, the game taking for a convoy any [+0x8] under the ships' count (not
// the convoys'); 0xFFFF: none (nothing is written).
uint16_t route_file_stop(const SaveGame& save, int ship);

// --- The wares window of a stop ---

// The state of a ware in it (its direction button).
enum class StopDirection : uint8_t {
	ToOffice = 0,  // ship → office
	Sell = 1,  // ship → town
	FromOffice = 2,  // office → ship
	Buy = 3,  // town → ship
	None = 4,
};

struct StopWindow {
	int ship = -1;  // the menu's ship
	uint16_t stop = 0;
	bool convoy = false;  // the menu shows a convoy (the most: the convoy's room)
	uint8_t owner = 0;  // the ship's owner (the operations' merchant, his last prices)
	std::array<uint8_t, 20> order{};  // [+0x6B8]: the ware of each row
	std::array<StopDirection, 20> state{};  // by ware
	std::array<int32_t, 20> units{};  // by ware: the amount in barrels or loads, −1 «Máx.»
	std::array<int32_t, 20> most{};  // by ware: the room
	std::array<int32_t, 20> price{};  // by ware: the price box
	StopDirection all = StopDirection::None;  // the bottom direction button ([+0x5ADC])
	// As opened, for «Deshaz».
	std::array<uint8_t, 20> saved_order{};
	std::array<StopDirection, 20> saved_state{};
	std::array<int32_t, 20> saved_units{};
	std::array<int32_t, 20> saved_price{};
};

// 0x405A20: the window for `stop` (from the page's row) of `ship`.
StopWindow open_stop_window(const SaveGame& save, int ship, uint16_t stop, bool convoy);
// The row of a ware (0..19; 20 when it isn't in the order).
int stop_window_row(const StopWindow& w, int ware);
// The 0x69 the window queues for ware `ware` as it is now.
OperationBytes stop_window_operation(const StopWindow& w, int ware);
// A ware's direction button (0x407CA0): its next state.
OperationBytes press_stop_direction(StopWindow& w, const SaveGame& save, int ware);
// The bottom direction button: the same for every ware (ware by ware).
std::vector<OperationBytes> press_stop_direction_all(StopWindow& w, const SaveGame& save);
// The amount's − and + (−1, +1) and the price's (0x406D50); nothing when the
// button is off (the price only in Sell and Buy; the amount's + at its most,
// its − at «Máx.»).
std::optional<OperationBytes> step_stop_amount(StopWindow& w, int ware, int delta);
std::optional<OperationBytes> step_stop_price(StopWindow& w, int ware, int delta);
bool stop_amount_step_enabled(const StopWindow& w, int ware, int delta);
bool stop_price_step_enabled(const StopWindow& w, int ware, int delta);
// A typed amount (units, −1 «Máx.») or price, committed (0x407235).
OperationBytes set_stop_amount(StopWindow& w, int ware, int32_t units);
OperationBytes set_stop_price(StopWindow& w, int ware, int32_t price);
// «Máx.» and «0»: every ware not in None whose amount differs (ware by ware).
std::vector<OperationBytes> stop_amounts_all(StopWindow& w, bool most);
// «Deshaz»: as opened (the 20 wares).
std::vector<OperationBytes> undo_stop_window(StopWindow& w);
// A row dragged to row `to`: the ware's 0x69 with its new row.
std::optional<OperationBytes> move_stop_row(StopWindow& w, int from, int to);

}  // namespace p3::data
