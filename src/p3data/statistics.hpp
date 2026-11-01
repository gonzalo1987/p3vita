#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

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
// head of the list (0x54CA20). The game calls it when an AI merchant goes
// bankrupt and starts again (0x4F5D80, the operation 0x65) and from the
// operations 0xA7 (0x541D70) and 0xB3 (0x542A30), after 0x4F7BB0; p3vita
// doesn't make those calls yet (docs/cloud/statistics.md, Decisions).
void add_merchant_statistics(SaveGame& save, int merchant);

// 0x54E1F0 (on 0x6DFC90): if the merchant is below the number of merchants
// (u16 0x6DE4AA), takes the merchant's object out of the list and frees it
// with its years (0x54CB40). The game calls it when an AI merchant goes
// bankrupt (0x4F5D80), before 0x54E170 gives the merchant a new one.
void remove_merchant_statistics(SaveGame& save, int merchant);

// 0x54DDF0 (on 0x6DFC90): empties the statistics (u16 [+0x0..+0x8] at 0).
void clear_statistics(SaveGame& save);

// The uncompressed .pst as the game writes it (0x54D7F0 and 0x54D870,
// version 0xB): u32 0xAFFE000B, the 6 u16 of the header, the four lists of the
// towns (a byte 1 before each record: u32 number and its blocks; 0 at the end
// or alone if empty), each followed by u32 0xAFFEAFFE, the merchants' objects
// (u16, u16, u8, u8, u16 years, their 80 records of the rings and their
// years, 0x6C bytes each) and 0xAFFEAFFE. Like the game's size function, it
// sets [+0xA] of the header to the number of objects and [+0x6] of each one
// to its years; and like the game's save function, it adds one byte (0) when
// there are months (the size function counts one too many).
std::vector<uint8_t> write_statistics(SaveGame& save);

// 0x54DA60: reads a .pst into the statistics. Of another version (the
// Tut*.pst of missav/ are 8) it empties them (0x54DDF0) and reads nothing.
// GAME BUG (looks like one): it builds each merchant's object with 0x54CA20
// for the merchant 0, whose record of the day clears the counters of the
// merchant 0, before reading the object over it; with fix_game_bugs() the
// counters stay. False if it is cut short.
bool read_statistics(const uint8_t* data, size_t size, SaveGame& save);

}  // namespace p3::data
