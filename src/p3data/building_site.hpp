#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace p3::data {

// A building's site in the town view (0x550430: the 0x10 bytes at the tile object's [+0x4] of the classes made by
// 0x552B50, the buildings): what its works have come to, as the view shows it, and its fire's four flames (the fire:
// the living town's part 8). The engine sets it through the view (0x5891C0 → the object's +0x68, 0x5530B0 → 0x550550):
// when the town is shown, for every record whose [+0x6] isn't 0 (0x521310), then each day of its works (0x5204D7).
struct BuildingSite {
	uint16_t cur = 0;  // [+0x4]: its works' days, less a tenth of `total` (0x550550)
	uint16_t total = 0;  // [+0x6]
	uint32_t next = 0;  // [+0x8]: the flames' next step (ms)
	std::array<int8_t, 4> flames = {-2, -2, -2, -2};  // [+0xC]: −2 no fire (0x550430), −1 burnt out, else a frame

	// Drawn as a site, not as the building (0x550770, 0x550A70, 0x550CE0's first test).
	bool active() const { return cur != total || flames[2] >= -1; }
	// Under works (the site's picture and the building growing from the bottom): active without a fire.
	bool works() const { return active() && flames[2] < -1; }
};

// 0x550550 without a fire (its fifth argument 0): `cur` days of the works' `total`. Nothing when both are the ones
// it has; else kept, and while it is a site `cur` less a tenth of `total` (0 at least): the building shows a tenth less
// than its works have done, and whole only once they end (`cur` = `total`).
void change_site(BuildingSite& site, uint16_t cur, uint16_t total);

// The fire's flames (0x6E11E8 for a site's first two flames, 0x6E1200 for its last two: the `.let` variants of kinds 3,
// `Feuer_Haus`, and 4, `Feuer_Haus_klein`, of a house, read by 0x54E310; only `armenhausb_s1` has them): their frames'
// count ([+0x14]), the first of their loop (their parameter 1) and the ms a frame (their parameter 0; the first
// table's for all four, 0x550C87).
struct FlameTable {
	int frames = 0;
	int loop = 0;
	uint32_t ms = 0;
};
using FlameTables = std::array<FlameTable, 2>;
inline const FlameTable& flame_table(const FlameTables& t, int flame) {
	return t[flame < 2 ? 0 : 1];
}

// 0x550550 with a fire (its fifth argument 1): the fire's life `cur` (the burning record's [+0x6], 49 down to 0) of
// `total` (50). Nothing when both are the ones it has; else kept; a site not burning yet catches fire: with 50 or
// more its flames from their first frame (and `cur` less 50 above it), else each at a frame of its loop at random
// (rand() % (frames − loop) + loop, `rand_state` the viewer's CRT-like random); a burning one with 0 left burns out
// (its flames −1: the ruin). No tenth taken away: the building shows its life's share of its height.
void change_site_fire(BuildingSite& site, uint16_t cur, uint16_t total, const FlameTables& tables,
	uint32_t& rand_state);

// 0x550A70's flames (a burning site's step, the view's each frame for its cells in view): past their time ([+0x8]) each
// flame with a point (`with_point`: the site picture's packed parameter not 0) a frame on (from its loop's first − 1
// to one of the loop at random; past the last back to the loop's first), then a rand() `r` each: a smoke when its frame
// is a multiple of 13 − r % 4 (`smoke`: the flame and its r, for its place); the next time the first table's ms on.
// False when it isn't burning (then 0x550A70 does nothing more).
struct FlameSmoke {
	int flame = 0;
	int r = 0;
};
bool step_site_flames(BuildingSite& site, uint32_t now, const FlameTables& tables, const std::array<bool, 4>& with_point,
	uint32_t& rand_state, std::vector<FlameSmoke>& smoke);

// 0x550770 (and 0x550CE0): of a slice of a building under works (its rows `slice_y` .. `slice_y` + `slice_h` of the
// building's picture, `height` rows high), how many rows from its bottom are drawn whole: the works' share of the
// picture's height from its bottom (`cur` · `height` / `total`), less the rows below the slice. May be below 0 or
// above `slice_h`.
int site_rows(const BuildingSite& site, int height, int slice_y, int slice_h);

// The rows above those, fading out (0x550770's loop, in floats as the game's x87 code stores them): from the lowest,
// each one's alpha, `alpha` less a 22nd of it a row while at least 1 (21 rows from 255). The normal drawing starts
// from the constant colour's alpha; the fog pass (0x550CE0) from its alpha / 5 · 4.
std::vector<uint8_t> site_fade(uint8_t alpha);

// The fog pass's first alpha of that fade (0x550CE0: the colour's alpha / 5 · 4, in a byte).
inline uint8_t site_fog_fade_alpha(uint8_t alpha) {
	return uint8_t(alpha / 5 * 4);
}

}  // namespace p3::data
