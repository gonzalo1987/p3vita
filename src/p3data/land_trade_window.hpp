#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "p3data/player_actions.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// The land trade (the building .let 0x33, «landhandel», type 0x27; its side menu *0x6E5508 has one button, on with an
// office of the player's in the town, that opens the window *0x6E55A0): wares from the player's office to his office
// in the town at the other end of the town's road (the 7 roads 0x672BF8), carried by carts (the operation 0x3A and
// its event 0x0C, town_tick/land_transport.cpp). The window's fields, as the game keeps them:
struct LandTradeWindow {
	uint8_t from = 0xFF;              // [+0x2DA4] the town
	uint8_t to = 0xFF;                // [+0x2E0C] the road's other end (0x525630), each update
	bool offices = false;             // [+0x2E1D] an office in both
	int page = 0;                     // 0 the wares; 1 the escort and the cost; 2 set out
	uint8_t step = 0;                 // [+0x2D80] 0..3: 1, 5, 50, all (0x67A18C)
	std::array<uint8_t, 20> loaded{};   // [+0x2DA5 + w] in barrels or loads
	std::array<uint32_t, 20> left{};    // [+0x2DBC + 4w] what the office has besides
	uint32_t used = 0;                // [+0x2E2C] the carts' room used: a barrel 1, a load 10; 100 at most
	uint8_t slots = 0;                // [+0x2E16] the same summed from the loads
	uint32_t value = 0;               // [+0x2E18] Σ ftol(float(units) · 0x673A18[w] + 0.5)
	uint8_t escorts = 0;              // [+0x2E14] 0..2 (low, medium, high)
	uint8_t danger = 0;               // [+0x2E15] 0..2: the value against 100 and 400 · slots
	uint32_t cost = 0;                // [+0x2E10] (slots + 9) / 10 · 50, 200, 800 by the escorts
	uint8_t days = 0;                 // [+0x2E17] 0x533490
};

// 0x525630 (on the town): with [+0x76C] & 0x8000, the town (its index) at the other end of its road; else 0xFF.
uint8_t fn_525630(const SaveGame& save, uint32_t town);
// 0x533490 (from, to): the road's days, (16 · its [+3] (16 without one) + 0x287) >> 8.
uint8_t fn_533490(const SaveGame& save, uint32_t from, uint32_t to);
// 0x5336B0 (town, the partner in and out: when not a town's index, the road's other end, without 0x525630's flag):
// whether the side menu lets the window open: 0 yes; 1 the town's barn (a 0x27 in its list [+0x764]) not built; 2 its
// partner's (never in the game: it looks in the town's own list again; with fix_game_bugs() the partner's); 3 the
// town besieged ([+0x2C8] & 0x10); 4 the partner besieged; 5 no road, or its other end no town's. The side menu's
// warning is the slot 0x6AFA20 + 4 · it (the partner's name in 2 and 4).
uint8_t fn_5336b0(const SaveGame& save, uint32_t town, uint8_t& partner);

// The window opened (0x5EA280 with the page 0): the escort 0, the other end, whether both towns have his office.
void land_trade_open(const SaveGame& save, uint32_t town, LandTradeWindow& w);
// The page 0's update (0x5EAAE0): the office's stock again (the loads cut to it), the other end, the slots and the
// value.
void land_trade_update(const SaveGame& save, LandTradeWindow& w);
// A ware's buttons (0x5EAD0C): load the step's (at most what the office has, the carts' room: a barrel 1, a load 10 of
// 100) or unload it.
void land_trade_load(LandTradeWindow& w, uint32_t ware);
void land_trade_unload(LandTradeWindow& w, uint32_t ware);
// The page 1's update (0x5EB010): the danger by the value, the cost by the escort, the road's days.
void land_trade_costs(const SaveGame& save, LandTradeWindow& w);
// «Despatch» (0x5EB3C0; the window's act with the money at least the cost): an ambush when the escort is below the
// danger and rand() & 3 below their difference (the loss ((the danger's cost − the cost) · 4) / that difference,
// spread over the wares sorted by value, with the game's indices: each ware sorted by the value at the place the one
// before went to, each loss by the value at its rank); then a 0x3A for each ware loaded {from, to, player, told (the
// first, or the first ambushed), u16 amount, u16 the cost (the first), u16 the loss, ware, escorts}; the loads and the
// room emptied.
std::vector<OperationBytes> land_trade_send(SaveGame& save, LandTradeWindow& w);

}  // namespace p3::data
