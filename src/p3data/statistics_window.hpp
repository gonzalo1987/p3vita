#pragma once

// The game's statistics window: the side panel's menu SIDEMENU_STATISTIK (vtable 0x66F4C8) and its parchment (the
// window's class, vtable 0x66FC28), and what each of its entries computes from the statistics (statistics.hpp). As
// read in re/emu/sesion15/statistics_notes.md; checked against the game with the scenario `statistics_company`.
// The window keeps its numbers as the game's texts (sprintf «%i», «%.0f») and its graphs' points as floats.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "p3data/savegame.hpp"

namespace p3::data {

class GameTexts;

// The menu's pages ([+0xB4..+0xBA], 0x495C10): page p lists the entries [first[p], first[p + 1]). Its four tabs show
// the pages 0..3: town data, town comparison, company overview, competition (4 and 5 are «Debug A», «Debug B»).
constexpr std::array<int, 7> kStatisticsPageFirst = {0, 6, 11, 16, 19, 22, 28};
constexpr int kStatisticsPages = 4;
constexpr int kStatisticsEntries = 28;
// The company overview's entries (the manual's «Negocios en propiedad»).
constexpr int kStatisticsBalance = 11;
constexpr int kStatisticsWealth = 12;
constexpr int kStatisticsShips = 13;
constexpr int kStatisticsProduction = 14;
constexpr int kStatisticsSupply = 15;
// The town comparison's entries (the manual's «Comparativa de ciudades»): the towns' residents (a table), and the
// graphs of the residents, the incoming ships, a ware's production and its stock.
constexpr int kStatisticsResidents = 6;
constexpr int kStatisticsTownResidents = 7;
constexpr int kStatisticsShipMovements = 8;
constexpr int kStatisticsTownProduction = 9;
constexpr int kStatisticsTownStock = 10;
// The competition's entries (the manual's «Competencia»): each merchant's company value, loading capacity and
// popularity.
constexpr int kStatisticsCompanies = 16;
constexpr int kStatisticsCapacity = 17;
constexpr int kStatisticsPopularity = 18;

// An entry's button text and window title, raw (the table 0x6CC830 that 0x46C060 builds at start: the pairs of
// 0x6C18C8 in the slots 0..14 and 16.., the slot 15 «Goods Supply», «Goods supply in %s»); a page's title (0x6C151C).
const std::string& statistics_button(const GameTexts& texts, int entry);
const std::string& statistics_title(const GameTexts& texts, int entry);
const std::string& statistics_page_title(const GameTexts& texts, int page);

// The merchant's object of the statistics (the list [+0x1C] of 0x6DFC90 by its u16 [+0x0]), or nullptr.
const MerchantStatistics* merchant_statistics(const SaveGame& save, uint32_t merchant);

// --- Balance (entry 11, 0x4A2F60) ---
//
// The period [+0xA4]: 0 the last 30 days, 1, 2, 3 the last 3, 6 and 12 months.
struct StatisticsBalance {
	// The merchant's object exists; without it the game leaves its texts zeroed.
	bool found = false;
	// [+0x210], 20 texts of 0x20 bytes: 0 and 1 the company's value now and at the start («%.0f»), 2 building
	// construction, 3 ship building, 4..8 the income (rental income, goods sold «%.0f», automatic trading, bonuses,
	// miscellaneous), 9 its total, 10..15 the expenditure (wages payments, ship repairs, goods purchased «%.0f»,
	// trading tax, automatic trading, miscellaneous), 16 its total, 17 the total (income − expenditure); 18 and 19
	// the start's and the end's dates («on %i. %s %i»).
	std::array<std::string, 20> texts;
	// [+0x20C]: «from %i. %s %i to %i. %s %i».
	std::string period;
};
StatisticsBalance statistics_balance(const SaveGame& save, const GameTexts& texts, uint32_t merchant, int period);

// The balance's 21 rows as the draw lays them (0x4A0C30): its labels (0x6C21C0 + 4k; the row 2 has none) and the
// headers (the mask 0x00101021: rows 0, 5, 12, 20).
constexpr int kBalanceRows = 21;
const std::string& balance_label(const GameTexts& texts, int row);
constexpr bool balance_header(int row) { return (0x00101021u >> row) & 1; }

// --- The graphs of the company overview (entries 12, 13, 14: 0x4A22A0) ---
//
// The period [+0xC0]: 0 days (30 points), 1 weeks (24), 2 months (24), 3 years (all, at most 128).
struct StatisticsGraph {
	uint32_t series = 0;  // [+0x2084]: the series filled (4 headers are made before the merchant's object is looked for)
	uint32_t points = 0;  // [+0x2088]
	uint32_t missing = 0;  // [+0x208C]: the points with nothing recorded yet (the oldest), 0 for the years
	// [+0x2080]: each series' points, the oldest first, as the game fills them (before 0x4A5DD0 scales them).
	std::array<std::vector<float>, 4> values;
	// The texts beside the graph ([+0x118 + 0x20k], raw), their kinds ([+0xF4 + k]: the icon the draw puts before
	// them) and labels (the English exe's slots of [+0xFC + 4k], for GameTexts::raw_at).
	std::vector<std::string> texts;
	std::vector<uint8_t> kinds;
	std::vector<uint32_t> labels;
	// The buttons [+0xC28]'s texts: the series' names (slots), 0 for none.
	std::array<uint32_t, 4> names{};
	// The competition's (raw): « » for an empty slot, else the merchant's «F. Family».
	std::array<std::string, 4> buttons;
	// Each series' header (its first two u32): the colour ([+0x1F8 + 4k]: the company overview's by the series, the
	// competition's and the towns' by the slot) and the merchant or the town (0 in the company overview).
	std::array<uint32_t, 4> colours{};
	std::array<uint32_t, 4> merchants{};
	// The towns' pages: the title's end (a ware's or a town's name, raw, that the game appends) and the text of the
	// button [+0x214] (the ware or the town it shows; empty: hidden).
	std::string title_end;
	std::string choice;
};
StatisticsGraph statistics_graph(const SaveGame& save, uint32_t merchant, int entry, int period);

// --- The competition (entries 16..18: 0x4A19C0) ---
//
// The window's four merchants ([+0xB4..+0xB7]; 0xFF at start, 0x49E380): a slot at or past the merchants' count is
// empty. Each shown merchant is a series in its slot's colour, and a button with his name that opens the list.
using StatisticsMerchants = std::array<uint8_t, 4>;
constexpr StatisticsMerchants kNoStatisticsMerchants = {0xFF, 0xFF, 0xFF, 0xFF};
// When the window opens (0x49D380): an empty slot 0 gets the player; each other empty slot, the merchant whose
// company value today is the largest, walking the statistics objects in their order. A merchant already shown in
// another slot empties the slot again (0xFF) when he comes later in that walk than the best so far, as in the game.
void statistics_default_merchants(const SaveGame& save, StatisticsMerchants& slots);
// The competition has no «4 weeks» (its button is hidden): the game turns the window's period 0 into 1.
constexpr int statistics_competition_period(int period) { return period == 0 ? 1 : period; }
// The graph: the shown merchants' series (the oldest point first): the company value (only above 0), the loading
// capacity, the popularity (only above 0), by weeks (24), months (24) or years (all, at most 128: with fewer than 2
// years recorded the game stops before anything else); the six averages beside it over every statistics object (the
// current week's record: «%i traders», the company value, ships, loading capacity in loads, offices, businesses; the
// labels the English exe's slots 0x6C1FCC + 8k).
StatisticsGraph statistics_competition(const SaveGame& save, const GameTexts& texts, const StatisticsMerchants& slots,
	int entry, int period);

// The list a merchant's button opens (0x49E850, for the entries 16..18: the list 0x46F760 in its mode 2, «choose
// trader»): the merchants not shown in the other slots (but those whose [+0x8] has the bit 4), from the last to the
// first, each with its key to sort by. Choosing a row puts its merchant in the slot; the extra row («no trader»,
// only when another slot shows a merchant) empties it (0xFF).
struct StatisticsChoice {
	int mode = 2;  // [+0x106C]: 0 wares, 1 towns, 2 merchants
	std::vector<uint8_t> merchants;  // [+0x1074]: the merchants, towns or wares
	// [+0x1088]: the company value's float bits (16), the loading capacity (17), the current week's popularity
	// (ftol of f[+0xE70], the week ring's first record: 18), compared as signed ints.
	std::vector<int32_t> keys;
	bool none = false;  // [+0x108C]
	int kind = 0;  // [+0x1098]: its sort button's text, 0x6C1190 + 4 · kind (4 wealth, 5 capacity, 6 popularity)
};
StatisticsChoice statistics_competition_choice(const SaveGame& save, const StatisticsMerchants& slots, int slot, int entry);
// The list's sorts: by name (0x4720D0, when it opens: the family names, then the first names, as signed chars) and,
// with its button, by the key (0x471EA0, the largest first); both bubble sorts that swap only the larger.
void sort_choice_by_name(const SaveGame& save, StatisticsChoice& choice);
void sort_choice_by_key(StatisticsChoice& choice);
// --- The towns' pages: the town data (entries 0..5) and the town comparison (6..10) ---
//
// The window's choices for them ([+0xAC..+0xBD]; 0x49E380 sets these at start, the single ones are 0 as the global
// window): 4 wares (24 or more: none) and 4 towns (the graphs' series), a single ware ([+0xBC]: the comparison's
// production and stock) and a single town ([+0xBD]: the town data's).
struct StatisticsTownChoices {
	std::array<uint8_t, 4> wares = {0, 2, 3, 11};
	std::array<uint8_t, 4> towns = {0, 1, 2, 3};
	uint8_t ware = 0;
	uint8_t town = 0;
};
// When the window opens (0x49D380): the first town past the towns becomes the player's home town (0 without a player),
// the first ware past 23 becomes 0.
void statistics_default_towns(const SaveGame& save, StatisticsTownChoices& choices);
// A town's name as the window takes it: the table 0x6DDA00 by the town's index, though after a load the table goes by
// the town's number (as the letters' bug; the names `by_number`, raw); with fix_game_bugs(), by its number.
std::string statistics_town_name(const SaveGame& save, const std::vector<std::string>& by_number, uint32_t town);

// Entry 6, the towns' residents (0x4A3BB0): a table of the towns sorted by their names (strcmp, as the window names
// them), each with its residents of the newest week recorded (the four classes; 0 below 0) or, without weeks, the
// town's [+0x2D4], as «%.0f inhabitants»; and the total of every town's [+0x2D4] («Total population in the
// Hanseatic League: %d population»).
struct StatisticsResidents {
	std::vector<uint32_t> towns;  // [+0x210]: each row's town (+ 0x40 · row) ...
	std::vector<std::string> texts;  // ... and its text (+ 4)
	int32_t total = 0;  // [+0x29F0]
};
StatisticsResidents statistics_residents(const SaveGame& save, const GameTexts& texts,
	const std::vector<std::string>& town_names);

// The towns' graphs (0x4A3FA0; for now the comparison's, entries 7..10): one series per town of the choices, in its
// slot's colour, the oldest point first, along the period's records of the towns (24 weeks or months, or the years:
// with fewer than 2 the game stops at once; the period 0 is turned into 1 as in the competition): 7 the residents (the
// four classes), 8 the incoming ships, 9 the single ware's production, 10 its stock (a town missing in a record, 0).
// Beside them (0x4A2AB0) the largest and the smallest town over all the towns and the period (for the production
// the smallest above 0), with their values as «%.0f inhabitants», «%.0f ships» or the ware's unit (loads, barrels,
// pieces); the town buttons the towns' names; 9 and 10 name the ware on the title and the button [+0x214]. Nothing
// but those texts without a town or a ware of the choices.
StatisticsGraph statistics_towns(const SaveGame& save, const GameTexts& texts, const std::vector<std::string>& town_names,
	const StatisticsTownChoices& choices, int entry, int period);
// The list a town button of the comparison opens (0x49E850, the list in its mode 1, «choose town»): every town not in
// another slot (with the extra row «none» when one is skipped), with its key (7 the town's [+0x2D4], 8 the newest
// week's ships, 9 and 10 its production and stock of the single ware: kinds 0..3, the sort button's texts).
StatisticsChoice statistics_town_choice(const SaveGame& save, const StatisticsTownChoices& choices, int slot, int entry);
// The list of the comparison's ware button (0x49FC1C, mode 0, «choose goods»): the 24 wares.
StatisticsChoice statistics_ware_choice();
// The towns' and wares' sorts by name (0x471FF0 by the window's town names, 0x471F10 by the wares' names 0x6C0BB0;
// signed chars), as sort_choice_by_name for the merchants.
void sort_choice_by_name(const SaveGame& save, const GameTexts& texts, const std::vector<std::string>& town_names,
	StatisticsChoice& choice);

// The game's «%.Nf» in a format with one conversion (its CRT rounds the half away from zero).
std::string game_sprintf_float(const std::string& format, double value);

// A merchant's names as his [+0xE8] and [+0xE4] point at them (0x4F6420: the first name from the women's list unless
// [+0x1A]; 0x4F6450), in Latin-1.
std::string statistics_first_name(const SaveGame& save, uint32_t merchant);
std::string statistics_family_name(const SaveGame& save, uint32_t merchant);

// --- Suministro (entry 15: its title «Goods supply in %s» with the town; the table is the draw's, 0x4A0F10) ---
//
// The town it shows ([+0x2440]) starts as the merchant's home town ([+0x19], 0x49D380) and its buttons go to the
// next or the previous town where he has an office (0x5308A0), round the towns (0x49F350).
std::vector<int> statistics_supply_towns(const SaveGame& save, uint32_t merchant);
// The rows: the wares 0..19 (0x698538's order). The window keeps the raw numbers ([+0x253C], [+0x24EC], [+0x258C]
// + 4 · ware); it shows the office's and the town's stock in barrels or loads (/ 200 or / 2000, when above 0), the
// office's in red when its workshops need more ([+0x64 + 4 · ware]) and with «*» when they need any, and the price
// when above 0. Empty without his office in the town (the game would read through a null office).
struct SupplyRow {
	int ware = 0;
	int32_t office = 0;  // the office's stock
	int32_t needed = 0;  // what its workshops use a day
	int32_t town = 0;  // the town's stock
	int32_t price = 0;  // the buy price of a barrel or a load (0x52E430 with 200 or 2000)
};
std::vector<SupplyRow> statistics_supply(const SaveGame& save, uint32_t merchant, uint32_t town);

// The scale's floor of an entry's graph (0x4A5DD0, [+0x2094], before the largest point raises it): by the entry's
// kind (0x66FC0C) 2000 for 0 and 1000 for 2, else 2 · 55 for the entry 1 and 2 · (20 + 55) for the entry 2 (the bytes
// [0x672930] and [0x672980]), else 1.
float statistics_scale_floor(int entry);

// The parchment's colours of the series (parchment.ini [Parchment1] Farbe0..3, the window's [+0x1F8 + 4k]).
constexpr std::array<uint32_t, 4> kStatisticsColours = {0xCC0009, 0x7E009B, 0x006700, 0x009CAE};

// The game's «%.0f» (its CRT rounds the half away from zero), and its __ftol (truncating to 64 bits, the low half).
std::string game_format_float(double value);
int32_t game_ftol(double value);

}  // namespace p3::data
