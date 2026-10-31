#pragma once

#include <cstdint>

#include "p3data/savegame.hpp"

namespace p3::data {

// The statistics of the game's graphs (docs/cloud/statistics.md): the object
// 0x6DFC90, its header in the globals (kStatistics) and its lists in
// SaveGame::statistics. They aren't only drawing: recording them resets
// counters of the towns and the merchants that the game sums over the day.

constexpr uint32_t kStatistics = 0x6DFC90;

// 0x54DE90 (on 0x6DFC90), every day after the daily work (0x5315C0): a record
// of the towns at the head of the days (0x54D1D0: it takes and clears [0x996]
// and the 24 s32 of [0x370] of each town); on day 1 of a month the average
// of the days of the month before at the head of the months, and on the
// first day of the year the average of 12 months at the head of the years;
// when the day number is 6 modulo 7, the average of 7 days at the head of the
// weeks. The first time, an object for each merchant without the bit 2 of
// its kind; then each merchant's object takes its record of the day
// (0x54C520: it takes and clears the merchant's counters [0x484..0x4CC]) and,
// on the same days, its averages. Last, the counters and flags of the header.
void daily_statistics(SaveGame& save);

// 0x54E170 (on 0x6DFC90): the object of the merchant if it hasn't one, at the
// head of the list (0x54CA20). The game calls it when a merchant starts again
// (0x4F5D80) and from the operations of 0x541FFB and 0x542DFB.
void add_merchant_statistics(SaveGame& save, int merchant);

// 0x54DDF0 (on 0x6DFC90): empties the statistics (u16 [+0x0..+0x8] at 0).
void clear_statistics(SaveGame& save);

}  // namespace p3::data
