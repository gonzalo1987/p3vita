#pragma once

// The game's statistics window: the side panel's menu SIDEMENU_STATISTIK (vtable 0x66F4C8) and its parchment (the
// window's class, vtable 0x66FC28), and what each of its entries computes from the statistics (statistics.hpp). As
// read in re/emu/sesion15/statistics_notes.md; checked against the game with the scenarios `statistics_company`,
// `statistics_competition`, `statistics_comparison` and `statistics_town_data`.
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
// The town data's entries (the manual's «Ciudades»), graphs of one town: its residents by class, how their needs are
// covered, their quality of life, the export balance, the production and the stock of up to four wares.
constexpr int kStatisticsPopulation = 0;
constexpr int kStatisticsRequirement = 1;
constexpr int kStatisticsQualityOfLife = 2;
constexpr int kStatisticsExportBalance = 3;
constexpr int kStatisticsTownDataProduction = 4;
constexpr int kStatisticsTownDataStock = 5;
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
	// [+0x2080]: each series' points, the oldest first, as the game fills them (before 0x4A5DD0 scales them). Up to
	// 4 series, 5 in the town data's export balance (its zero line after the four wares).
	std::array<std::vector<float>, 5> values;
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
	// competition's and the towns' by the slot; 0xFFFFFFFF for the town data's zero line) and the merchant, the town
	// or the ware (0 in the company overview; the town data's first three entries leave it unset: 0 here).
	std::array<uint32_t, 5> colours{};
	std::array<uint32_t, 5> merchants{};
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
// another slot empties the slot again (0xFF) when he comes later in that walk than the best so far, as in the game;
// with fix_game_bugs() he is skipped, and the slot keeps the best so far.
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
// The list's sorts: by name (0x4720D0, when it opens: the family names, then the first names, as signed chars; with
// fix_game_bugs(), as a dictionary, an accented letter as its plain letter) and, with its button, by the key
// (0x471EA0, the largest first; with fix_game_bugs() the company values compared as floats, not as their bits); both
// bubble sorts that swap only the larger.
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
// them; with fix_game_bugs(), as a dictionary), each with its residents of the newest week recorded (the four classes; 0 below 0) or, without weeks, the
// town's [+0x2D4], as «%.0f inhabitants»; and the total of every town's [+0x2D4] («Total population in the
// Hanseatic League: %d population»).
struct StatisticsResidents {
	std::vector<uint32_t> towns;  // [+0x210]: each row's town (+ 0x40 · row) ...
	std::vector<std::string> texts;  // ... and its text (+ 4)
	int32_t total = 0;  // [+0x29F0]
};
StatisticsResidents statistics_residents(const SaveGame& save, const GameTexts& texts,
	const std::vector<std::string>& town_names);

// The towns' graphs (0x4A3FA0), along the period's records of the towns (24 weeks or months, or the years: with fewer
// than 2 the game stops at once; the period 0 is turned into 1 as in the competition), the oldest point first, a town
// missing in a record 0. Nothing but the texts beside them without a town of the comparison's four ([+0xB0..+0xB3])
// or a ware of the four wares ([+0xAC..+0xAF]), on both pages.
// - The comparison (7..10): one series per town of the choices, in its slot's colour: 7 the residents (the four
//   classes), 8 the incoming ships, 9 the single ware's production, 10 its stock. Beside them (0x4A2AB0) the largest
//   and the smallest town over all the towns and the period (for the production the smallest above 0), with their
//   values as «%.0f inhabitants», «%.0f ships» or the ware's unit (loads, barrels, pieces); the town buttons the
//   towns' names; 9 and 10 name the ware on the title and the button [+0x214].
// - The town data (0..5), of the single town ([+0xBD], named on the title and the button [+0x214]): 0 its four
//   classes' residents; 1 and 2 the three classes' s16 [+0x1F6 + 2k] and [+0x1F0 + 2k] (with a point below 0 every
//   series raised by it, and a fourth series, colour 0xFFFFFFFF, its zero line); 3 the export balance [+0x120 + 4w],
//   4 the production and 5 the stock of each ware of the four wares, a series each in its slot's colour (3 with one
//   more, the zero line: every series raised by the smallest point below 0, else by a tenth of the largest above 0,
//   else by 100; the game also reads the ware 0's balance for that line before overwriting it, so it counts in the
//   smallest and the largest). Beside them the newest week's four classes of the town (empty without the record).
//   Entry 0 also sets the comparison's first town to 0 when it is past the towns (0xFF: «none»), as the game.
StatisticsGraph statistics_towns(const SaveGame& save, const GameTexts& texts, const std::vector<std::string>& town_names,
	StatisticsTownChoices& choices, int entry, int period);
// The list a button of the towns' pages opens (0x49E850): the comparison's town buttons (7..10, the list in its mode
// 1, «choose town»: every town not in another slot, with the extra row «none» when one is skipped, and its key: 7 the
// town's [+0x2D4], 8 the newest week's ships, 9 and 10 its production and stock of the single ware: kinds 0..3, the
// sort button's texts) and the town data's ware buttons (3..5, mode 0, «choose goods»: every ware not in another
// slot, «none» when one is skipped, no keys). The other entries have none (0..2: an empty list).
StatisticsChoice statistics_town_choice(const SaveGame& save, const StatisticsTownChoices& choices, int slot, int entry);
// The list of the comparison's ware button (0x49FC1C, mode 0, «choose goods»): the 24 wares.
StatisticsChoice statistics_ware_choice();
// The list of the town data's town button (0x49FB41, mode 1, «choose town»): every town, no keys, no «none».
StatisticsChoice statistics_town_list(const SaveGame& save);
// The towns' and wares' sorts by name (0x471FF0 by the window's town names, 0x471F10 by the wares' names 0x6C0BB0;
// signed chars, or with fix_game_bugs() as a dictionary), as sort_choice_by_name for the merchants.
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

// --- The window's drawing (0x4A14D0 and its parts, read and traced in the emulator: re/emu/sesion43/README.md) ---
//
// The window (the menu's [+0xC0]): 16 × 17 tiles of 32 (its show 0x49D380: *0x6CBB00 · 16 and · 17), the dialogs' paper
// with the rolls' ends (0x41C5A0(w, h, 1): the window 12 wider, its tiles 6 in), centred in the view left of the side
// panel and in its height; parchment.ini [Parchment1]: DrawXmin 26, DrawYmin 32 (its key is written «DrawYmin32»: the
// default), LinePosX 60, FontGr 0 (the heads and the legend), FontKl 1 (the rows and the axes). Its places, in its own
// coordinates (its left the rolls' end's), are the code's with the fonts 0 and 1 15 high (ttffonts.ini's Tiepolo).
constexpr int kStatsWindowW = 16 * 32 + 12;
constexpr int kStatsWindowH = 17 * 32;
constexpr int kStatsDrawX = 26;  // [+0xCA]
constexpr int kStatsTitleY = 32;  // [+0xCE]: the title's line (the font 3, centred)
constexpr int kStatsRight = 486;  // [+0xCC]: 512 − DrawXmin
constexpr int kStatsRowHeight = 15;  // A row's band: the font 1's height
constexpr int kStatsRowStep = 17;  // [+0xF0]: a button's height / 4 / 2 and the font 1's height
constexpr int kStatsPlotX = 60;  // [+0xDC]: LinePosX
constexpr int kStatsPlotY = 54;  // [+0xD4]: DrawYmin and the font 0's height and a half
constexpr int kStatsPlotW = 426;  // [+0xE4]: [+0xCC] − LinePosX
constexpr int kStatsPlotH = 263;  // [+0xE8]: down to the period buttons less the font 1's height and a half
constexpr int kStatsPlotBottom = kStatsPlotY + kStatsPlotH;
constexpr int kStatsDateY = kStatsPlotBottom + 4;  // The x axis' labels (0x49FD70); its lines to the plot's bottom + 2
// The y axis' labels, right-aligned at [+0xDA]: LinePosX less the width of «a» in the font 1 (0x6BAC22).
// The buttons (their styles of buttons.ini): the graphs' periods [+0x784 + 0xE8 k] (Button3, 96 × 18), the balance's
// [+0x3E4 + 0xE8 k] (Button3), the four under the graph [+0xC28 + 0xE8 k] (Button4, 128 × 18; k = 0 the top one), the
// ware's or the town's [+0x214] (Button4), the close button [+0x2FC] (32 × 18); Suministro's «<» [+0x20A0] (Button2,
// 48), «Choose town» [+0x2270] (Button4) and «>» [+0x2188] (Button2) centred on the close button's line, and the town's
// view [+0x2358] (parchment.ini [Button13], 32 × 18) at its left.
constexpr int kStatsPeriodY = 339;
constexpr int kStatsGraphPeriodX[4] = {26, 147, 268, 390};  // The last at [+0xCC] − 96, the others a third apart.
constexpr int kStatsBalancePeriodY = 472;  // The close button's line less 22.
constexpr int kStatsBalancePeriodX[4] = {26, 147, 268, 389};  // From DrawXmin, (460 − 96) / 3 apart.
constexpr int kStatsSlotX = 26;
constexpr int kStatsSlotY = 369;  // The first; the others 22 under it.
constexpr int kStatsSlotStep = 22;
constexpr int kStatsChoiceY = 468;  // [+0x214], at kStatsSlotX.
constexpr int kStatsCloseX = 454;
constexpr int kStatsCloseY = 494;
constexpr int kStatsSupplyX[4] = {26, 139, 192, 325};  // The town's view, «<», «Choose town», «>».
// The legend (0x4A0690): rows from ([+0xEC], [+0xEE]) every [+0xF0] with a band to [+0xCC]; [+0xEC] = DrawXmin − 4 +
// Button4's width + the width of «M» in the font 1 (0x6BAC20), [+0xEE] the top slot button's y. Each row's label in
// the font 0 at its left and its value right-aligned at [+0xCC] (kind 0) or 26 (the load's picture's width) before it,
// after it its picture by the kind: 1 the coin (TEX16014) 4 to the right, 2 the barrel (TEX16030), 3 the load
// (TEX16031), 4 «%» (left-aligned there); 5 none.
constexpr int kStatsLegendBase = kStatsDrawX - 4 + 128;
constexpr int kStatsLegendValueX = kStatsRight - 26;
// The towns' residents (entry 6, 0x4A0980): the rows a twelfth of the window's width in from DrawXmin and [+0xCC],
// from DrawYmin and twice the font 1's height, every [+0xF0] down to 22 rows ([+0x29EC]); the scroll bar at [+0xCC] − 4
// (0x49DD9C); the total line half a row under the last.
constexpr int kStatsResidentsX = kStatsDrawX + kStatsWindowW / 12;
constexpr int kStatsResidentsRight = kStatsRight - kStatsWindowW / 12;
constexpr int kStatsResidentsY = kStatsTitleY + 2 * kStatsRowHeight;
constexpr int kStatsResidentsRows = 22;
constexpr int kStatsResidentsBarX = kStatsRight - 4;
// Suministro (0x4A0F10): the heads in the font 0 at DrawYmin + 40, the rows every [+0xF0] under them; the columns'
// right ends (the heads') and their pictures 5 to the right of each figure.
constexpr int kStatsSupplyHeadY = kStatsTitleY + 40;
constexpr int kStatsSupplyStar = kStatsDrawX + 0x5A;
constexpr int kStatsSupplyColumn[3] = {kStatsDrawX + 180, kStatsDrawX + 280, kStatsDrawX + 380};  // pictures
constexpr int kStatsSupplyHead[3] = {kStatsDrawX + 206, kStatsDrawX + 306, kStatsDrawX + 396};  // heads' right ends
// The balance (0x4A0C30): its 21 rows from DrawYmin + 8, every [+0xF0], a header in the font 0 8 under the row before
// and its rows 8 under its line (the font 0's height); the others a band (DrawXmin, y, 460, 15), the label at DrawXmin and
// the value right-aligned at [+0xCC].
constexpr int kStatsBalanceY = kStatsTitleY + 8;
// The balance's rows' tops (each of the 21 rows of balance_label).
int statistics_balance_row_y(int row);

// The scale of a graph's points (0x4A5DD0, [+0x2094]): its floor (statistics_scale_floor) or its largest point (every
// series' points, the empty ones too), and if that is above 0 the plot's height · 0.95 over it, every point multiplied
// by it (`scaled`); else the largest point itself, the points as they are.
struct StatisticsScale {
	float scale = 1.0f;
	bool scaled = false;
};
StatisticsScale statistics_scale(int entry, const StatisticsGraph& g);
// The x step between points ([+0x2090]: the plot's width over the points less one).
float statistics_x_step(uint32_t points);

// The x axis (0x49FD70): a line at each point from the plot's top to its bottom + 2 (0x1F000000) and its label in the
// font 1 centred at the date line, from the last point leftwards. By the period ([+0xC0], the competition's and the
// towns' turned from 0 to 1): the days from yesterday back, «D.M.» on the days divisible by 5 (going back past the 1st
// the month's days are the current month's previous one's: twice back, a wrong month's length; fixed behind
// fix_game_bugs), the weeks' «NN.» (the week of the year (day of the year less the week's day) / 7, back from the
// one before, 01 wrapping to 52), the months' first letter, the years' «%i»; the weeks and the years only where the
// room since the last label reaches their width («␣48␣», «␣1888␣» in the font 1: `week_width`, `year_width`).
struct StatisticsXLabel {
	float x = 0.0f;  // The point's x (its line); the label at its ftol.
	std::string text;  // Raw (the game's own text); empty without one.
};
std::vector<StatisticsXLabel> statistics_x_labels(const SaveGame& save, const GameTexts* texts, int period,
	uint32_t points, int week_width, int year_width);

// The y axis (0x4A0190): its labels right-aligned (the font 1) and the grid's lines (0x1F000000, from the plot's left
// less 2 to its right). By the entry's kind (0x66FC0C): kinds 0..4 «0» at the bottom and the unit at the top (the slots
// 0x6C1AAC + 4 · kind: load, people, gold, ships), then the steps; the needs and the quality of life «min.» at their
// zero line and «max.» at the top; the export balance «exp.» at the top, «0» at its zero line and «imp.» at the bottom;
// the others «0» at their zero line (with one). The step: from the kind's (2000 loads, 1000 gold, 1, 10⁷ for the kinds
// 5 and 6) doubled, × 2.5, doubled... (1, 2, 5, 10) until it is over three half lines on the plot; its labels a counter
// of 15 characters (the kind's digits, «t» at the end for the gold's thousands), up the plot while over its top and a
// line.
struct StatisticsYLabel {
	int y = 0;  // Its text's top.
	std::string text;  // Raw.
};
struct StatisticsYAxis {
	std::vector<StatisticsYLabel> labels;
	std::vector<float> grid;  // The grid lines' y.
};
// `g`'s points as the window holds them (before the scale), `half_line` the font 1's height / 2. Without `texts` (the
// tests) the words are left empty, as the months' letters of statistics_x_labels.
StatisticsYAxis statistics_y_axis(const GameTexts* texts, int entry, const StatisticsGraph& g, const StatisticsScale& s,
	int half_line);

}  // namespace p3::data
