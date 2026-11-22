#pragma once

// A sea battle's clouds: the town view's creatures of class 0x14 (0x580240, 0x38 bytes; looks `wolke.lcr` ..
// `wolke6.lcr`), the battle's slots [+0x1B10] filled by 0x6107B9 and drawn by 0x610709 from the view's drawing while a
// sea battle is shown (the view's [+0xC324] past the towns): their shadows, then the clouds. The town view has none.

#include <cstdint>
#include <functional>
#include <vector>

namespace p3::data {

// One cloud: its place in the view's pixels (16.16, [+0xC], [+0x10]), its speed in pixels a 10 seconds ([+0x24],
// [+0x28]), the wind's speed it keeps near ([+0x2C], [+0x30]), the area it wraps in ([+0x34], [+0x36]: the battle's
// area and 200 more) and its look ([+0x20]: one of the class's, in iso/lcr/files.lst's order).
struct BattleCloud {
	int32_t x = 0;
	int32_t y = 0;
	int32_t vx = 0;
	int32_t vy = 0;
	int32_t tx = 0;
	int32_t ty = 0;
	uint16_t width = 0;
	uint16_t height = 0;
	uint8_t look = 0;
};

// The random the view's creatures draw from (0..0x7FFF, as the CRT's rand(): the viewer's own).
using CloudRand = std::function<int()>;

// 0x6107EB..0x61088A: the battle's wind ([+0x670] its direction in 256ths, [+0x671] its strength) as the clouds'
// speed, packed in one word: 54 · strength · its sine (>> 16) less the whole part of 1188 / 34 · strength · its cosine
// in the high word.
int32_t battle_cloud_wind(uint8_t dir, uint8_t strength);
// The place of the cloud `k` (0x610980.., 0x6107DA.. at a town): a random point of its cell, 556 · 563 pixels from
// (139, 141) on, 16 cells a row (8 at a town): its y first.
void battle_cloud_place(int k, bool at_town, const CloudRand& rand, int32_t& x, int32_t& y);
// 0x5803B0 (its start, the packed wind its fifth argument): its look one of those of the variant for P2.cfg's DETAILS
// (`variants`, the class's looks' LCR head 0x0C: 1 for DETAILS 0, 2 for 1, 3 for 1 and above; among several one at
// random, the first if none), its place, the wind unpacked (its low word signed, the rest / 65536) and its speed that
// wind's ± 32 across and ± 22 up and down.
void battle_cloud_start(BattleCloud& c, int32_t x, int32_t y, int32_t packed, const std::vector<uint32_t>& variants,
	int details, const CloudRand& rand);
// 0x580520 (its step; `elapsed` the frame's ms, the view's [+0x414], at most 10000 for the speed's change): while its
// speed across is within 22 of the wind's (the game compares that twice, against 32 and 22, not the speed up and down:
// with fix_game_bugs() the second the speed up and down),
// its speed drawn towards the wind's by elapsed / 10000 and moved at random by up to ± 32 · elapsed (± 22 · elapsed) /
// 10000 (none at 0 ms: the game would divide by 0); else set again at random about the wind's; then moved by its speed ·
// elapsed / 10000 and wrapped in its area. Never gone.
void battle_cloud_step(BattleCloud& c, uint32_t elapsed, const CloudRand& rand);
// 0x6107B9: a battle's clouds, 56 at a town over 4552 · 4042, else 224 over 8904 · 7884, each placed and started.
std::vector<BattleCloud> make_battle_clouds(bool at_town, uint8_t dir, uint8_t strength,
	const std::vector<uint32_t>& variants, int details, const CloudRand& rand);

// 0x610709: the alpha of the cloud of slot `k`'s shadow (black, drawn 30 pixels right and 150 down: 0x5802F0, with
// DETAILS 2 or more) and of the cloud (white).
inline uint8_t battle_cloud_shadow_alpha(int k) { return uint8_t(50 - (((k & 7) * 25) >> 4)); }
inline uint8_t battle_cloud_alpha(int k) { return uint8_t(0x80 - (k & 7) * 4); }

}  // namespace p3::data
