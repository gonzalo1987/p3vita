// The statistics window's drawing (p3data/statistics_window.hpp), without game data: the balance's rows (0x4A0C30), the
// x axis' labels (0x49FD70) and the y axis' scale and labels (0x4A5DD0, 0x4A0190), against the game's drawing traced in
// the emulator (re/emu/sesion43/README.md: statswintrace.sh on save 3, 1 April 1362, with the tracer's fonts: every
// letter 7 wide).

#include <cmath>
#include <cstdio>
#include <string>

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/statistics_window.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

SaveGame dated(int day, int month, int year, int day_of_year, uint32_t tick) {
	SaveGame save;
	save.globals.u8(global::kWorldDay) = uint8_t(day);
	save.globals.u8(global::kWorldMonth) = uint8_t(month);
	save.globals.u16(global::kWorldYear) = uint16_t(year);
	save.globals.u16(global::kWorldDayOfYear) = uint16_t(day_of_year);
	save.tick() = tick;
	return save;
}

}  // namespace

int main() {
	// The balance's rows: its headers 8 lower, their rows 23 under them, the others 17 apart (traced: 40, 63, 80, 97,
	// 114, 139, 162 .. 247, 272, 295 .. 397, 422).
	{
		const int expected[kBalanceRows] = {40, 63, 80, 97, 114, 139, 162, 179, 196, 213, 230, 247, 272, 295, 312, 329,
			346, 363, 380, 397, 422};
		bool same = true;
		for (int r = 0; r < kBalanceRows; ++r) {
			same = same && statistics_balance_row_y(r) == expected[r];
		}
		check(same, "the balance's rows as traced");
	}

	// The days (Riqueza, 30 points, on 1 April): from yesterday (the 31st of March: no label) back, «D.M.» on the days
	// divisible by 5, at the traced x.
	{
		const SaveGame save = dated(1, 3, 1362, 90, 0);
		const std::vector<StatisticsXLabel> labels = statistics_x_labels(save, nullptr, 0, 30, 28, 42);
		const int x[30] = {486, 471, 456, 441, 427, 412, 397, 383, 368, 353, 339, 324, 309, 295, 280, 265, 250, 236, 221,
			206, 192, 177, 162, 148, 133, 118, 104, 89, 74, 59};
		check(labels.size() == 30, "30 days");
		bool at = labels.size() == 30;
		for (size_t k = 0; at && k < 30; ++k) {
			at = game_ftol(labels[k].x) == x[k];
		}
		check(at, "the days' places as traced (251.0 drawn at 250)");
		check(labels.size() == 30 && labels[0].text.empty() && labels[1].text == "30.3." && labels[6].text == "25.3." &&
				labels[16].text == "15.3." && labels[26].text == "5.3." && labels[29].text.empty(),
			"the days' labels");
		check(std::fabs(statistics_x_step(30) - 426.0f / 29.0f) < 1e-5f, "the step");
	}

	// Going back from the 1st of March: February's 28th (k = 0) .. 1st (k = 27), then January's days as February's (the
	// game's slip: its 28th, 27th) or, fixed behind fix_game_bugs, its 31st and 30th.
	{
		const SaveGame save = dated(1, 2, 1362, 59, 0);
		const std::vector<StatisticsXLabel> original = statistics_x_labels(save, nullptr, 0, 30, 28, 42);
		check(original.size() == 30 && original[0].text.empty() && original[3].text == "25.2." &&
				original[23].text == "5.2." && original[29].text.empty(),
			"back over February, January by February's days");
		set_fix_game_bugs(true);
		const std::vector<StatisticsXLabel> fixed = statistics_x_labels(save, nullptr, 0, 30, 28, 42);
		set_fix_game_bugs(false);
		check(fixed.size() == 30 && fixed[3].text == "25.2." && fixed[29].text == "30.1.", "January's 30th when fixed");
	}

	// The weeks (the competition, 24 points, the tracer's « 48 » 28 wide): the week before (11) and every other one,
	// «01.» wrapping to «52.».
	{
		const SaveGame save = dated(1, 3, 1362, 90, 6 * 256);  // (90 − 6) / 7 = 12
		const std::vector<StatisticsXLabel> labels = statistics_x_labels(save, nullptr, 1, 24, 28, 42);
		check(labels.size() == 24 && labels[0].text == "11." && labels[1].text.empty() && labels[2].text == "09." &&
				labels[10].text == "01." && labels[12].text == "51." && labels[22].text == "41.",
			"the weeks' labels as traced");
		check(labels.size() == 24 && game_ftol(labels[2].x) == 448 && game_ftol(labels[4].x) == 411 &&
				game_ftol(labels[22].x) == 78,
			"the weeks' places as traced");
	}

	// The years: «%i» back from the year before, spaced by « 1888 ».
	{
		const SaveGame save = dated(1, 3, 1362, 90, 0);
		const std::vector<StatisticsXLabel> labels = statistics_x_labels(save, nullptr, 3, 3, 28, 42);
		check(labels.size() == 3 && labels[0].text == "1361" && labels[1].text == "1360" && labels[2].text == "1359",
			"the years");
	}

	// The scale and the y axis (Riqueza, kind 2, its largest point 391600, the font 1's half line 7): the step 50000
	// (1000 · 2 · 2.5 · 2 · 2 · 2.5) with its «t», its labels and lines as traced.
	{
		StatisticsGraph g;
		g.series = 1;
		g.points = 3;
		g.values[0] = {1000.0f, 391600.0f, 200.0f};
		const StatisticsScale s = statistics_scale(kStatisticsWealth, g);
		check(s.scaled && std::fabs(s.scale - 263.0f * 0.95f / 391600.0f) < 1e-9f, "Riqueza's scale");
		const StatisticsYAxis axis = statistics_y_axis(nullptr, kStatisticsWealth, g, s, 7);
		// «0» at the bottom, the unit at the top, then 50t .. 350t.
		const int y[9] = {310, 47, 278, 246, 214, 182, 150, 118, 86};
		const char* text[9] = {"0", "", "50t", "100t", "150t", "200t", "250t", "300t", "350t"};
		bool same = axis.labels.size() == 9 && axis.grid.size() == 7;
		for (size_t k = 0; same && k < 9; ++k) {
			same = axis.labels[k].y == y[k] && axis.labels[k].text == text[k];
		}
		check(same, "Riqueza's y axis as traced");
		// (The traced ones at 285.1 .. 93.6: its largest point a little under this one's.)
		check(axis.grid.size() == 7 && std::fabs(axis.grid[0] - 285.1f) < 0.2f && std::fabs(axis.grid[6] - 93.6f) < 0.2f &&
				axis.grid[6] == float(double(kStatsPlotBottom) - 350000.0 * double(s.scale)),
			"its grid lines");
		// Below the floor (1000 gold): the scale from the floor.
		StatisticsGraph low;
		low.series = 1;
		low.points = 1;
		low.values[0] = {10.0f};
		check(statistics_scale(kStatisticsWealth, low).scale == float(double(263.0f) * double(0.95f) / 1000.0),
			"the floor");
	}

	// Loads (kind 0, Barcos' capacity in 2000 units): the step from 2000, 14 digits.
	{
		StatisticsGraph g;
		g.series = 1;
		g.points = 2;
		g.values[0] = {0.0f, 40000.0f};  // 20 loads
		const StatisticsYAxis axis = statistics_y_axis(nullptr, kStatisticsShips, g, statistics_scale(kStatisticsShips, g), 7);
		// 2000 · scale = 12.5 ≤ 21 → 4000 (25 px): labels 2, 4, 6 .. while over 68.
		check(axis.labels.size() >= 3 && axis.labels[2].text == "2" && axis.labels[3].text == "4", "the loads' steps");
	}

	// The export balance (kind 5): «exp.», «0» at its zero line (the last series' first point), «imp.» at the bottom less
	// a half line (not raised); no numbers.
	{
		StatisticsGraph g;
		g.series = 2;
		g.points = 2;
		g.values[0] = {50.0f, 100.0f};
		g.values[1] = {50.0f, 50.0f};  // the zero line
		const StatisticsScale s = statistics_scale(kStatisticsExportBalance, g);
		const StatisticsYAxis axis = statistics_y_axis(nullptr, kStatisticsExportBalance, g, s, 7);
		const int zero = game_ftol(double(kStatsPlotBottom) - double(float(double(s.scale) * 50.0))) - 7;
		check(axis.labels.size() == 3 && axis.labels[0].y == 47 && axis.labels[1].y == zero && axis.labels[1].text == "0" &&
				axis.labels[2].y == 310 && axis.grid.empty(),
			"the export balance's axis");
	}

	if (failures) {
		std::fprintf(stderr, "%d failed\n", failures);
		return 1;
	}
	std::printf("statistics_window: ok\n");
	return 0;
}
