#pragma once

#include <array>
#include <cstdint>

#include "p3data/savegame.hpp"
#include "p3data/wares.hpp"

namespace p3::data {

// What the market hall's window shows (display only; the viewer draws it its way): the building .let 0x00, its side
// menu *0x6E551C (0x5AC6E0; «Stock of goods», «Production», «Consumption») and its window *0x6E5584 (made by 0x5DD9E0,
// vtable 0x679DF0; its page [+0x19C] set by 0x5DDCD0, the town [+0x198]). Each frame the page's update (0x5DE810,
// 0x5DE960, 0x5DEB70) fills three figures a ware ([+0x1A0], [+0x1F0], [+0x240] + 4 · k, the wares in the order of the
// byte table 0x698538: 0..19) in barrels (200) or loads (2000) by the ware (0x672C14), and the drawing (0x5DDD80 with
// 0x5DDF50, 0x5DE1E0, 0x5DE4F0) shows each only when it isn't 0, with its unit's picture (the prices with the coin).
//
// Page 0, «Stock of goods»: the town's stock [+0x4 + 4·w] / unit (float, truncated: 0x5DE87F), what buying a unit
// from the town costs (0x52E430) and what selling one to it gives (0x52E1D0).
// Page 1, «Weekly production»: the stock (rounded: + 0.5, truncated), the town's production [+0xC4 + 4·w] (rounded;
// shown also as 0 when the town has workers in the ware's business: below), the merchants' offices' in the town (the
// list [+0x784], next [+0x2CA]: each office's [+0xC4 + 4·w] · 7 summed, rounded). The town's figure isn't multiplied
// by 7 though the field is a day's production as the offices' (the town tick clears both each day): a game bug,
// display only, reproduced (the town's column a seventh of the page's «weekly»); behind fix_game_bugs, · 7 too.
// Page 2, «Weekly consumption»: the stock (rounded), the citizens' [+0x310 + 4·w] · 7 (rounded), the businesses'
// [+0x64 + 4·w] · 7 (rounded; at least 1 when it isn't 0), and the last two summed.
constexpr int kMarketHallPages = 3;

struct MarketHallRow {
	std::array<int32_t, 4> values{};  // The page's columns (page 2's fourth the total; pages 0 and 1 three).
	std::array<bool, 4> shown{};  // Drawn: not 0 (page 1's town column also with the ware's business in the town).
};

// The window's figures for the page (0..2) of the town, a row per ware 0..19.
std::array<MarketHallRow, kTradeWareCount> market_hall_rows(const SaveGame& save, uint32_t town, int page);

// 0x5DE3xx: the business type making each ware (0x672C88: up to 3 none, 0xFF the whale oil, by the town's whalers
// [+0x2CC]); page 1 shows the town's production as 0 where the town has workers in it ([+0x844 + 16 · type]).
bool market_hall_town_makes(const SaveGame& save, uint32_t town, int ware);

}  // namespace p3::data
