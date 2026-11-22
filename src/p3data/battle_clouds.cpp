#include "p3data/battle_clouds.hpp"

#include <algorithm>
#include <cstdlib>

#include "p3data/battle/battle_math.hpp"
#include "p3data/game_fixes.hpp"

namespace p3::data {

namespace {

using battle_math::cosine;
using battle_math::mul;
using battle_math::sine;

// The high word of a signed 64-bit product (imul's edx).
int32_t high(int32_t a, int32_t b) {
	return int32_t((int64_t(a) * int64_t(b)) >> 32);
}

// A division by 10000 as the game's (0x68DB8BAD, >> 12 after the high word, toward zero).
int32_t by_10000(int32_t v) {
	const int32_t d = high(v, 0x68DB8BAD) >> 12;
	return d + int32_t(uint32_t(d) >> 31);
}

}  // namespace

int32_t battle_cloud_wind(uint8_t dir, uint8_t strength) {
	const int32_t s = strength;
	const int32_t across = mul(mul(s, sine(dir)), 54) >> 16;
	const int32_t v = mul(mul(mul(s, cosine(dir)), 33), 36);
	int32_t down = high(v, 0x78787879) >> 4;
	down += int32_t(uint32_t(down) >> 31);
	return int32_t(uint32_t(across) - (uint32_t(down) & 0xFFFF0000u));
}

void battle_cloud_place(int k, bool at_town, const CloudRand& rand, int32_t& x, int32_t& y) {
	const int row = at_town ? k >> 3 : k >> 4;
	const int col = at_town ? k & 7 : k & 15;
	y = rand() % 0x119 + 563 * row + 0x8D;
	x = rand() % 0x116 + 556 * col + 0x8B;
}

void battle_cloud_start(BattleCloud& c, int32_t x, int32_t y, int32_t packed, const std::vector<uint32_t>& variants,
	int details, const CloudRand& rand) {
	c.look = 0;
	const uint8_t level = uint8_t(details + 1);
	uint8_t seen = 0;
	for (size_t k = 0; k < variants.size() && k < 256; ++k) {
		const uint32_t v = variants[k];
		const bool fits = (v == 1 && level == 1) || (v == 2 && level == 2) || (v == 3 && level > 1);
		if (!fits) {
			continue;
		}
		if (seen == 0 || rand() % seen == 0) {
			c.look = uint8_t(k);
		}
		++seen;
	}
	c.tx = int16_t(uint16_t(packed));
	c.ty = packed / 65536;
	c.vx = (rand() & 63) + c.tx - 0x20;
	c.vy = rand() % 0x2C + c.ty - 0x16;
	c.x = int32_t(uint32_t(x) << 16);
	c.y = int32_t(uint32_t(y) << 16);
}

void battle_cloud_step(BattleCloud& c, uint32_t elapsed, const CloudRand& rand) {
	const int32_t e = int32_t(std::min<uint32_t>(elapsed, 10000));
	// The game compares the speed across twice (0x580583: [+0x24] − [+0x2C] again), never the speed up and down: fixed
	// behind fix_game_bugs() (docs/roadmap.md, «Los fallos del juego encontrados»).
	const int32_t second = fix_game_bugs() ? c.vy - c.ty : c.vx - c.tx;
	if (std::abs(c.vx - c.tx) <= 0x20 && std::abs(second) <= 0x16) {
		const int32_t rest = 10000 - e;
		int32_t v = mul(c.tx, e) + mul(c.vx, rest);
		if (e > 0) {
			v += rand() % (e << 6) - (e << 5);
		}
		c.vx = by_10000(v);
		v = mul(c.ty, e) + mul(c.vy, rest);
		if (e > 0) {
			v += rand() % (44 * e) - 22 * e;
		}
		c.vy = by_10000(v);
	} else {
		c.vx = (rand() & 63) + c.tx - 0x20;
		c.vy = rand() % 0x2C + c.ty - 0x16;
	}
	const int32_t f = int32_t((uint64_t(uint32_t(elapsed << 16)) * 0xD1B71759u) >> 45);
	const int32_t nx = int32_t(uint32_t(c.x) + uint32_t(mul(c.vx, f)));
	const int32_t ny = int32_t(uint32_t(c.y) + uint32_t(mul(c.vy, f)));
	const bool moved = (uint32_t(nx) >> 16) != uint32_t(int32_t(int16_t(uint32_t(c.x) >> 16))) ||
		(uint32_t(ny) >> 16) != uint32_t(int32_t(int16_t(uint32_t(c.y) >> 16)));
	c.x = nx;
	c.y = ny;
	if (!moved) {
		return;
	}
	auto wrap = [](int32_t& p, uint16_t size) {
		const int16_t whole = int16_t(uint32_t(p) >> 16);
		int16_t w = whole;
		if (whole > int32_t(size)) {
			w = int16_t(whole - size);
		} else if (whole < 0) {
			w = int16_t(size + whole);
		}
		p = int32_t((uint32_t(uint16_t(w)) << 16) | (uint32_t(p) & 0xFFFF));
	};
	wrap(c.x, c.width);
	wrap(c.y, c.height);
}

std::vector<BattleCloud> make_battle_clouds(bool at_town, uint8_t dir, uint8_t strength,
	const std::vector<uint32_t>& variants, int details, const CloudRand& rand) {
	std::vector<BattleCloud> out(at_town ? 0x38 : 0xE0);
	for (size_t k = 0; k < out.size(); ++k) {
		int32_t x = 0;
		int32_t y = 0;
		battle_cloud_place(int(k), at_town, rand, x, y);
		battle_cloud_start(out[k], x, y, battle_cloud_wind(dir, strength), variants, details, rand);
		out[k].width = at_town ? 0x11C8 : 0x22C8;
		out[k].height = at_town ? 0xFCA : 0x1ECC;
	}
	return out;
}

}  // namespace p3::data
