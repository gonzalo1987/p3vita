// The cannon balls of a battle with a human side (the battles' piece 7): the
// guns of a battle the player directs fire balls that fly and land, instead
// of an automatic battle's instant broadsides.
//
// A ball is a heap block of 0x38 bytes in the battle's list [+0x668] (its
// slots [+0xACC], 0x20 more at a time; the bitmap of the slots in use
// [+0x664], its words [+0xACE]): [+0x0], [+0x2] the point it flies to; [+0x4],
// [+0x8] its place in 16.16 (the whole x at [+0x6], y at [+0xA]); [+0xC],
// [+0x10] its speed; [+0x14] its damage; [+0x18] a byte (1); [+0x1C] its
// flight; [+0x20] its sprite (the interface's); [+0x24] the place of the ship
// that fired it (0xFFFF none); [+0x28] the battle; [+0x2C] its slot; [+0x2E]
// its item in the scene (kind 1); [+0x30] its direction; [+0x32] a time;
// [+0x34] 8.
//
// With the battle on the screen ([0x6E59CC]) the game plays sounds that take
// the world's rand() when the sound is on (battle_sound) and, where a ball
// hits a ship, smoke that always takes it.

#include <cstdlib>
#include <cstring>

#include "p3data/battle/battle_math.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

using namespace battle_math;

uint8_t fn_6029b0(int32_t dx, int32_t dy);  // battle_step.cpp

namespace {

constexpr size_t kBallSize = 0x38;

// The game's signed division by a constant, toward 0 (its magic products).
int32_t div0(int32_t a, int32_t b) {
	return b == 0 ? 0 : a / b;
}

}  // namespace

// Whether the battle is on the screen ([0x6E59CC], the battles' [+0x3FC]).
bool Sailing::on_screen(size_t b) const {
	const BattleGraph& g = save_.battles;
	return g.get<uint8_t>(0, 0x3FC) == g.get<uint16_t>(b, 0x662);
}

// A sound of the battle on the screen (0x443690 on *0x6CBDB8: its variants,
// then rand() % variants, 0x595B00 plays it on the town view): the world's
// rand() when the sound has variants (the sound on; set_battle_sound).
void Sailing::battle_sound(size_t b) {
	if (on_screen(b) && save_.battle_window.sound_variants != 0) {
		tick::game_rand(save_);
	}
}

// The town view's sprites of the battle on the screen (the interface's), for the viewer: kept only with its
// battle window and at most 1024 between two frames.
void Sailing::battle_effect(size_t b, BattleEffect effect) {
	if (!save_.battle_window.present || !on_screen(b) || battle_effects_.size() >= 1024) {
		return;
	}
	effect.slot = uint8_t(save_.battles.get<uint16_t>(b, 0x662));
	battle_effects_.push_back(effect);
}

// 0x6111B9 (on a battle): a free slot of the cannon balls ([+0x668], null),
// else 0x20 more ([+0xACC]; the bitmap [+0x664] one word more, [+0xACE];
// both reallocated, the new ones 0) and the first new one.
uint16_t Sailing::fn_6111b9(size_t b) {
	BattleGraph& g = save_.battles;
	const uint16_t n = g.get<uint16_t>(b, 0xACC);
	const int balls = g.ptr(b, 0x668);
	for (uint16_t i = 0; balls >= 0 && i < n; ++i) {
		if (g.get<uint32_t>(size_t(balls), 4 * size_t(i)) == 0) {
			return i;
		}
	}
	const uint16_t words = uint16_t(g.get<uint16_t>(b, 0xACE) + 1);
	g.set<uint16_t>(b, 0xACE, words);
	g.set<uint16_t>(b, 0xACC, uint16_t(n + 0x20));
	const size_t bits = g.realloc_at(b, 0x664, 4 * size_t(words));
	const size_t list = g.realloc_at(b, 0x668, 4 * size_t(uint16_t(n + 0x20)));
	g.set<uint32_t>(bits, 4 * (size_t(words) - 1), 0);
	for (uint16_t i = n; i < uint16_t(n + 0x20); ++i) {
		g.set<uint32_t>(list, 4 * size_t(i), 0);
	}
	return n;
}

// 0x602A90 (the ball's constructor: its slot, the point it flies to, where
// it starts, its damage, a byte, the battle, a flag, the ship's place that
// fired it, a word): in the bitmap; its speed (dx, dy) · 340000 / its
// flight, starting 30 along; its flight − 30; its direction; its item in the
// scene (kind 1, its slot, a box of ±0x50 in an automatic battle, else ±5);
// a time by the world's rand(): ((r & 31) + 0x60) · flight / 512; its damage
// · 1.5. On the screen also its sprite (the interface's).
void Sailing::fn_602a90(size_t ball, uint16_t slot, uint16_t tx, uint16_t ty, uint16_t sx, uint16_t sy, int32_t damage,
	uint8_t byte, size_t b, uint16_t shooter, uint16_t word) {
	BattleGraph& g = save_.battles;
	g.set<uint16_t>(ball, 0x4, 0);
	g.set<uint16_t>(ball, 0x8, 0);
	g.set<uint8_t>(ball, 0x18, byte);
	g.set<uint16_t>(ball, 0xA, sy);
	g.set<uint16_t>(ball, 0x34, word);
	g.set<uint16_t>(ball, 0x2C, slot);
	g.set<int32_t>(ball, 0x14, damage);
	g.set<uint16_t>(ball, 0x24, shooter);
	g.set<uint16_t>(ball, 0x0, tx);
	g.set<uint16_t>(ball, 0x2, ty);
	g.set<uint16_t>(ball, 0x6, sx);
	g.set<uint32_t>(ball, 0x28, BattleGraph::pointer(b));
	const int bits = g.ptr(b, 0x664);
	if (bits >= 0) {
		const size_t w = 4 * size_t(slot >> 5);
		g.set<uint32_t>(size_t(bits), w, g.get<uint32_t>(size_t(bits), w) | (1u << (slot & 0x1F)));
	}
	const int32_t dx = int32_t(int16_t(tx)) - int16_t(sx);
	const int32_t dy = int32_t(int16_t(ty)) - int16_t(sy);
	const int32_t d = root(mul(dy, dy) + mul(dx, dx));
	g.set<uint16_t>(ball, 0x6, uint16_t(g.get<uint16_t>(ball, 0x6) + uint16_t(div0(30 * dx, d))));
	g.set<uint16_t>(ball, 0xA, uint16_t(g.get<uint16_t>(ball, 0xA) + uint16_t(div0(30 * dy, d))));
	const int32_t vx = div0(mul(dx, 340000), d), vy = div0(mul(dy, 340000), d);
	g.set<int32_t>(ball, 0xC, vx);
	g.set<int32_t>(ball, 0x1C, d - 30);
	g.set<int32_t>(ball, 0x10, vy);
	g.set<uint8_t>(ball, 0x30, fn_6029b0(vx, vy));
	const uint16_t x = g.get<uint16_t>(ball, 0x6), y = g.get<uint16_t>(ball, 0xA);
	const uint16_t r = g.get<uint8_t>(b, 0xAC8) != 0 ? 0x50 : 5;
	g.set<uint16_t>(ball, 0x2E, fn_619450(b, 1, uint8_t(slot), uint16_t(x - r), uint16_t(y - r), uint16_t(x + r), uint16_t(y + r)));
	g.set<uint32_t>(ball, 0x20, 0);
	const int32_t time = int32_t((tick::game_rand(save_) & 0x1F) + 0x60) * g.get<int32_t>(ball, 0x1C);
	g.set<uint16_t>(ball, 0x32, uint16_t(time / 512));
	g.set<int32_t>(ball, 0x14, damage + (damage >> 1));
}

// A ball goes (0x6033..., 0x60383D..0x6038B3): out of the bitmap, its slot
// null, its item freed (0x619560), its sprite from the town view (the
// interface's).
void Sailing::ball_gone(size_t ball) {
	BattleGraph& g = save_.battles;
	const size_t b = size_t(g.ptr(ball, 0x28));
	const uint16_t slot = g.get<uint16_t>(ball, 0x2C);
	const int bits = g.ptr(b, 0x664);
	if (bits >= 0) {
		const size_t w = 4 * size_t(slot >> 5);
		g.set<uint32_t>(size_t(bits), w, g.get<uint32_t>(size_t(bits), w) & ~(1u << (slot & 0x1F)));
	}
	const int balls = g.ptr(b, 0x668);
	if (balls >= 0) {
		g.set<uint32_t>(size_t(balls), 4 * size_t(slot), 0);
	}
	fn_619560(b, g.get<uint16_t>(ball, 0x2E));
}

// 0x6214CB (on a scene's ship: the point, the damage, the side, its gun's
// index and count, a heavy gun): a ball. Its damage: damage >> 6 · 28; by
// its captain's [+0xB] (· (6c / 17 + 100) / 100); by its ship's [+0x3F]
// (0..4; a human merchant's ±2 by the game's difficulty, *0x6CC3E8 [+0x12]:
// below 1 or above 2, a twentieth more or less per step); by the battle's
// generator (· (85..115) / 100; at least 1). Spread across the line by the
// gun's index (8 a gun from the middle). In a free slot (0x6111B9), a new
// ball (0x602A90) from the ship to the point, both moved across.
void Sailing::fn_6214cb(size_t e, uint16_t x, uint16_t y, int32_t damage, uint8_t side, uint8_t index, uint8_t count,
	uint8_t heavy) {
	BattleGraph& g = save_.battles;
	(void)side;
	const size_t b = scene_battle(e);
	const ShipMemory& s = scene_ship(e);
	int32_t v = int32_t(uint32_t(damage >> 6) * 2800u) / 100;
	const uint16_t captain = s.u16(0x42);
	if (captain < save_.captain_count()) {
		const int32_t c = save_.captains[captain].u8(0xB);
		v = int32_t(uint32_t(c * 6 / 17 + 100) * uint32_t(v)) / 100;
	}
	int8_t morale = int8_t(s.u8(0x3F));
	if (morale < 0) {
		morale = 0;
	} else if (morale > 4) {
		morale = 4;
	}
	const uint8_t owner = s.u8(0x0);
	if (owner != 0xFF && owner < save_.merchants.size() && save_.merchants[owner].memory.u16(0x8) == 0) {
		const uint8_t level = save_.settings.u8(0x12);
		if (level == 0) {
			morale = int8_t(morale + 2);
		} else if (level == 2) {
			morale = int8_t(morale - 2);
		}
	}
	if (morale < 1) {
		v += int32_t(uint32_t(int32_t(morale) - 1) * uint32_t(v)) / 20;
	} else if (morale > 2) {
		v += int32_t(uint32_t(int32_t(morale) - 2) * uint32_t(v)) / 20;
	}
	const uint32_t r = battle_rand(g, b) % 31u + 0x55;
	uint32_t final_damage = (r * uint32_t(v)) / 100u;
	if (final_damage == 0) {
		final_damage = 1;
	}
	const int32_t dx = int16_t(uint16_t(x - g.get<uint16_t>(e, 0xA)));
	const int32_t ndy = int16_t(uint16_t(g.get<uint16_t>(e, 0xE) - y));
	const int32_t dist = root(mul(ndy, ndy) + mul(dx, dx));
	const int32_t spread = int16_t(uint16_t(uint16_t(index - (count >> 1)) << 3));
	const int32_t ox = div0(mul(spread, ndy), dist), oy = div0(mul(spread, dx), dist);
	const uint16_t slot = fn_6111b9(b);
	g.blocks.emplace_back(kBallSize);
	const size_t ball = g.blocks.size() - 1;
	fn_602a90(ball, slot, uint16_t(x + ox), uint16_t(y + oy), uint16_t(g.get<uint16_t>(e, 0xA) + ox),
		uint16_t(g.get<uint16_t>(e, 0xE) + oy), int32_t(final_damage), 1, b, g.get<uint16_t>(e, 0x138), 8);
	// 0x602D3B: on the screen, where the ball starts, a heavy gun's fire (creature 0x12/0x25) or the gun's smoke
	// (0x12/0x21), in the ball's direction ((d + 8) & 0xF0).
	BattleEffect shot;
	shot.kind = BattleEffect::Shot;
	shot.x = g.get<int16_t>(ball, 0x6);
	shot.y = g.get<int16_t>(ball, 0xA);
	shot.dir = g.get<uint8_t>(ball, 0x30);
	shot.heavy = heavy != 0;
	battle_effect(b, shot);
	g.set_ptr(size_t(g.ptr(b, 0x668)), 4 * size_t(slot), int(ball));
}

// 0x61E8EF (on a scene's ship: the side, the point): a broadside of balls.
// Aimed off the side's normal by more than 8, at its normal ± 8 (the
// distance kept); the range at most 480 + 1.35 · how far the wind is from
// the side (the point pulled in). One ball (0x6214CB) per gun of the side
// (its slots [+0x13C + 2 · side + 4k], two each, kinds below 6) with its
// damage by the gun and the distance; then the sound (on the screen) and
// the side loads ([+0x130 + side] 0x96). Without guns on the side, nothing.
void Sailing::fn_61e8ef(size_t e, uint8_t side, uint16_t x, uint16_t y) {
	BattleGraph& g = save_.battles;
	const size_t b = scene_battle(e);
	const ShipMemory& s = scene_ship(e);
	int32_t ox = int32_t(int16_t(x)) - g.get<int16_t>(e, 0xA);
	int32_t oy = int32_t(int16_t(y)) - g.get<int16_t>(e, 0xE);
	int32_t dist = root(mul(oy, oy) + mul(ox, ox));
	const uint8_t heading = g.get<uint8_t>(e, 0x11);
	const uint8_t dir = fn_6029b0(int16_t(ox), int16_t(oy));
	const uint8_t rel = side == 0 ? uint8_t(dir - heading + 0x40) : uint8_t(dir - heading - 0x40);
	if (rel > 8 && rel < 0xF8) {
		const int32_t a = int32_t(heading) + (rel < 0x80 ? 0x08 : 0xF8);
		const int32_t c = sine(uint32_t(a + 0x40) & 0xFF), sn = sine(uint32_t(a) & 0xFF);
		if (side == 0) {
			ox = int32_t(0u - uint32_t(mul(c, dist))) >> 16;
			oy = int32_t(0u - uint32_t(mul(sn, dist))) >> 16;
		} else {
			ox = mul(c, dist) >> 16;
			oy = mul(sn, dist) >> 16;
		}
	}
	const uint8_t wind = uint8_t(g.get<uint8_t>(b, 0x670) - heading);
	const int32_t t = wind < 0x80 ? int32_t(wind) - 0x40 : 0xC0 - int32_t(wind);
	const int32_t limit = t * 4320 / 3200 + 0x1E0;
	if (dist > limit) {
		ox = div0(mul(limit, ox), dist);
		oy = div0(mul(limit, oy), dist);
		dist = limit;
	}
	const uint16_t px = uint16_t(g.get<uint16_t>(e, 0xA) + ox), py = uint16_t(g.get<uint16_t>(e, 0xE) + oy);
	uint8_t count = 0;
	for (size_t k = 0; k < 6; ++k) {
		for (size_t j = 0; j < 2; ++j) {
			if (s.u8(0x13C + 2 * size_t(side) + 4 * k + j) < 6) {
				++count;
			}
		}
	}
	if (count == 0) {
		return;
	}
	uint8_t index = side != 0 ? uint8_t(count - 1) : 0;
	for (size_t k = 0; k < 6; ++k) {
		for (size_t j = 0; j < 2; ++j) {
			const uint8_t gun = s.u8(0x13C + 2 * size_t(side) + 4 * k + j);
			if (gun >= 6) {
				continue;
			}
			const int32_t ab = int32_t(kGunA[gun]) * int32_t(kGunB[gun]);
			int32_t damage = ab + div0(mul(ab, dist), -6 * int32_t(kGunTime[gun]));
			if (damage < 0) {
				damage = 0;
			}
			fn_6214cb(e, px, py, damage, side, index, count, gun >= 4 ? 1 : 0);
			index = side != 0 ? uint8_t(index - 1) : uint8_t(index + 1);
		}
	}
	battle_sound(b);  // 0xEA60
	g.set<uint8_t>(e, 0x130 + size_t(side), 0x96);
}

// 0x603130 (on a ball): its sub-step. Past the point it flies to (along its
// main axis, in its direction): it lands (on the screen a sound by the
// water or not under it and a splash: the interface's but the sound's
// rand()) and goes; else it moves (16 times as far in an automatic battle)
// and its box follows (±0x50 or ±5).
void Sailing::fn_603130(size_t ball) {
	BattleGraph& g = save_.battles;
	const size_t b = size_t(g.ptr(ball, 0x28));
	const int32_t vx = g.get<int32_t>(ball, 0xC), vy = g.get<int32_t>(ball, 0x10);
	int32_t next, goal, v;
	if (std::abs(vx) > std::abs(vy)) {
		v = vx;
		next = (v / 0x10000) + g.get<int16_t>(ball, 0x6);
		goal = g.get<int16_t>(ball, 0x0);
	} else {
		v = vy;
		next = (v / 0x10000) + g.get<int16_t>(ball, 0xA);
		goal = g.get<int16_t>(ball, 0x2);
	}
	if ((next > goal && v > 0) || (next < goal && v < 0)) {
		battle_sound(b);  // 0xEE98 or 0xEA92, by the town view's cell
		BattleEffect splash;  // 0x603347: the splash (creature 0x12/0 on water; 0x12/0x20 by the cell's class).
		splash.kind = BattleEffect::Splash;
		splash.x = g.get<int16_t>(ball, 0x6);
		splash.y = g.get<int16_t>(ball, 0xA);
		battle_effect(b, splash);
		ball_gone(ball);
		return;
	}
	const bool automatic = g.get<uint8_t>(b, 0xAC8) != 0;
	g.set<uint32_t>(ball, 0x4, g.get<uint32_t>(ball, 0x4) + (automatic ? uint32_t(vx) << 4 : uint32_t(vx)));
	g.set<uint32_t>(ball, 0x8, g.get<uint32_t>(ball, 0x8) + (automatic ? uint32_t(vy) << 4 : uint32_t(vy)));
	const uint16_t x = g.get<uint16_t>(ball, 0x6), y = g.get<uint16_t>(ball, 0xA);
	const uint16_t r = automatic ? 0x50 : 5;
	fn_6197a0(b, g.get<uint16_t>(ball, 0x2E), uint16_t(x - r), uint16_t(y - r), uint16_t(x + r), uint16_t(y + r));
}

// 0x603470 (on a ball: the place of the ship it hits, the side): the
// shooter's team ([+0x24], when known) is one more on its mode ([+0xACA],
// 0xFF if the ship hit fled) and raises the music (9) if it is [+0xAD2] − 1;
// the shooter's other balls lose it. The hit (0x61F96F with its damage): sunk
// (2) raises the music (0xB). On the screen: the sound and a splash or
// flames (the sounds' rand()), then smoke: [+0x14] / 0xAF0 puffs, one more
// when rand() % 0xAF0 is below the rest, each with a rand(). The ball goes.
void Sailing::fn_603470(size_t ball, uint16_t place, uint8_t side) {
	BattleGraph& g = save_.battles;
	const size_t b = size_t(g.ptr(ball, 0x28));
	const uint16_t shooter = g.get<uint16_t>(ball, 0x24);
	auto team_of = [&](uint16_t k) { return g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(battle_entry(b, k), 0x13D))); };
	if (shooter != 0xFFFF) {
		const size_t mode = 0xACA + size_t(team_of(shooter));
		if (g.get<uint8_t>(b, mode) < 0x7F) {
			g.set<uint8_t>(b, mode, uint8_t(g.get<uint8_t>(b, mode) + 1));
		}
		if (g.get<uint8_t>(battle_entry(b, place), 0x12A) & 0x20) {
			g.set<uint8_t>(b, mode, 0xFF);
		}
		if (uint32_t(team_of(shooter)) + 1 == g.get<uint8_t>(b, 0xAD2)) {
			fn_611649(b, 9);
		}
		const int balls = g.ptr(b, 0x668);
		for (uint16_t k = 0; balls >= 0 && k < g.get<uint16_t>(b, 0xACC); ++k) {
			const int other = g.ptr(size_t(balls), 4 * size_t(k));
			if (other >= 0 && g.get<uint16_t>(size_t(other), 0x24) == shooter) {
				g.set<uint16_t>(size_t(other), 0x24, 0xFFFF);
			}
		}
	}
	const uint8_t result = fn_61f96f(battle_entry(b, place), g.get<int32_t>(ball, 0x14), -1, side);
	if (result == 2) {
		if (shooter != 0xFFFF && uint32_t(team_of(shooter)) + 1 == g.get<uint8_t>(b, 0xAD2)) {
			fn_611649(b, 0xB);
		}
		battle_sound(b);  // 0xEA88
	} else if (result == 1 || result == 0) {
		battle_sound(b);  // 0xEA7E (with its splash) or 0xEA6A (flames)
	}
	if (on_screen(b)) {
		const int32_t damage = g.get<int32_t>(ball, 0x14);
		uint32_t puffs = uint32_t(damage / 0xAF0);
		if (int32_t(tick::game_rand(save_) % 0xAF0) < damage % 0xAF0) {
			++puffs;
		}
		for (uint32_t k = uint8_t(puffs); k > 0; --k) {
			tick::game_rand(save_);
		}
		// 0x603656..0x603835: an explosion (creature 0x12/0x11) when 2, else a puff (0x12/0x20), and the
		// splinters (0x13), each placed by its rand().
		BattleEffect hit;
		hit.kind = BattleEffect::Hit;
		hit.x = g.get<int16_t>(ball, 0x6);
		hit.y = g.get<int16_t>(ball, 0xA);
		hit.result = result;
		hit.puffs = uint8_t(puffs);
		battle_effect(b, hit);
	}
	ball_gone(ball);
}

// 0x60A73C (on a battle: a ball's slot, a ship's place): whether the ball's
// way this sub-step (its speed >> 16, one more in its direction; 16 times in
// an automatic battle) crosses the ship's hull (its five corners turned by
// its heading, 0x67AB30), each side from the last corner: the bits of the
// sides it crosses (0 and 4: 1; 1 and 2: 2; 3: 3).
// (The game takes the ship's type as the whole byte [+0xE]: beyond 3 it reads
// past the hulls; none is.)
uint8_t Sailing::fn_60a73c(size_t b, uint16_t slot, uint16_t place) {
	const BattleGraph& g = save_.battles;
	const size_t e = battle_entry(b, place);
	const size_t ball = size_t(g.ptr(size_t(g.ptr(b, 0x668)), 4 * size_t(slot)));
	const uint8_t heading = g.get<uint8_t>(e, 0x11);
	const int32_t sn = sine(heading), cs = sine(uint8_t(heading + 0x40));
	const int32_t bx = g.get<int16_t>(ball, 0x6), by = g.get<int16_t>(ball, 0xA);
	const int32_t vx = g.get<int32_t>(ball, 0xC), vy = g.get<int32_t>(ball, 0x10);
	int32_t mx = vx >> 16, my = vy >> 16;
	mx += vx > 0 ? 1 : vx < 0 ? -1 : 0;
	my += vy > 0 ? 1 : vy < 0 ? -1 : 0;
	if (g.get<uint8_t>(b, 0xAC8) != 0) {
		mx = int32_t(uint32_t(mx) << 4);
		my = int32_t(uint32_t(my) << 4);
	}
	const uint8_t type = scene_ship(e).u8(0xE) & 3;
	const int32_t ex = g.get<int16_t>(e, 0xA), ey = g.get<int16_t>(e, 0xE);
	auto corner = [&](size_t k, int32_t& x, int32_t& y) {
		const int32_t hx = kHull[type][k][0], hy = kHull[type][k][1];
		x = ((mul(cs, hx) - mul(sn, hy)) >> 16) + ex - bx;
		y = ((mul(cs, hy) + mul(sn, hx)) >> 16) + ey - by;
	};
	int32_t px = 0, py = 0;
	corner(4, px, py);
	uint8_t hit = 0;
	for (size_t k = 0; k < 5; ++k) {
		const int32_t qx = px, qy = py;
		corner(k, px, py);
		int32_t a = mul(mx, qy) - mul(my, qx);
		int32_t d = mul(mx, qy - py) - mul(my, qx - px);
		if (d < 0) {
			a = -a;
			d = -d;
		}
		if (a > d || a < 0) {
			continue;
		}
		a = mul(qx, py - qy) - mul(qy, px - qx);
		d = mul(mx, py - qy) - mul(my, px - qx);
		if (d < 0) {
			a = -a;
			d = -d;
		}
		if (a > d || a < 0) {
			continue;
		}
		if (k == 0 || k == 4) {
			hit |= 1;
		} else if (k == 3) {
			hit = 3;
		} else {
			hit |= 2;
		}
	}
	return hit;
}

}  // namespace p3::data
