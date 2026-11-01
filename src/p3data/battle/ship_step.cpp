// The step of a scene's ship (method +0x0 of a scene's ship, 0x61A9E0) and
// what it calls (docs/cloud/battles-2.md §4): its movement (0x6264C0, on
// the part of the scene's ship that starts at [+0x4]), its turn (0x6271D7).
// Offsets here are from the start of the scene's ship: its ship [+0x4], its
// place in 16.16 ([+0x8] x, [+0xC] y), its heading in 8.8 ([+0x10], the
// byte [+0x11]), its last move ([+0x14], [+0x18]), its speed [+0x1C], where
// it heads ([+0x20] x, or < 0 for nowhere, [+0x22] y), its sails [+0x26],
// its 256 directions [+0x27] (how much each is to be avoided, 0x60BCDA).

#include <cstdlib>

#include "p3data/battle/battle_math.hpp"
#include "p3data/sailing.hpp"

namespace p3::data {

using namespace battle_math;

namespace {

// 0x627863 (a jump table): the turn of each type of ship.
constexpr uint32_t kTurn[4] = {0x100, 0xE6, 0x100, 0xCD};

}  // namespace

// 0x6271D7 (on [+0x4] of a scene's ship; a direction): the ship turns
// towards the direction, by a rate from its type, its speed (against its
// battle speed, 0x612930), its captain ([+0x9] of the captain [+0x42] of its
// ship) and its hull ([+0x18] of [+0x14]): to it if it is closer than the
// rate; else the way round with less to avoid in its directions (each at
// least 8), or the short way when none is to be avoided.
void Sailing::fn_6271d7(size_t e, uint8_t dir) {
	BattleGraph& g = save_.battles;
	const ShipMemory& s = scene_ship(e);
	const uint32_t base = kTurn[s.u8(0xE) & 3];
	const uint32_t speed = fn_612930(uint16_t(g.get<uint32_t>(e, 0x4))) * 0x334u;
	uint32_t rate = base;
	if (speed != 0) {  // The game divides by 0 here (a ship with no battle speed).
		rate += (uint32_t(mul(int32_t(base), g.get<int32_t>(e, 0x1C))) / speed) & 0xFFFF;
	}
	rate = uint32_t(mul(mul(int32_t(rate), 10), 40)) / 1000u;
	const uint16_t captain = s.u16(0x42);
	if (captain < save_.globals.u16(global::kCaptainCount) && captain < save_.captains.size()) {
		rate = uint32_t(mul(int32_t(rate), int32_t(save_.captains[captain].u8(0x9)) + 0x352)) / 0x352u;
	}
	if (s.u32(0x14) != 0) {
		const uint8_t hull = uint8_t(mul(s.s32(0x18), 100) / s.s32(0x14));
		if (hull < 0x3C) {
			if (hull > 0x1E) {
				rate = uint32_t(mul(int32_t(rate), int32_t(hull) + 0x8C)) / 200u;
			} else {
				rate = uint32_t(mul(int32_t(rate), 17)) / 20u;
			}
		}
	}
	if (g.get<uint8_t>(scene_battle(e), 0xAC8) != 0) {
		rate <<= 4;
	}
	const uint16_t want = uint16_t((uint32_t(dir) << 8) + 0x80);
	const uint16_t heading = g.get<uint16_t>(e, 0x10);
	const uint16_t delta = uint16_t(want - heading);
	const uint8_t h = g.get<uint8_t>(e, 0x11);
	// The directions from `from` to `to`: the sum of each (at least 8) and the largest.
	auto scan = [&](uint8_t from, uint8_t to, uint16_t& sum, uint16_t& top) {
		sum = 0;
		top = 0;
		for (uint8_t k = from; k != uint8_t(to + 1); ++k) {
			const uint8_t a = g.get<uint8_t>(e, 0x27 + size_t(k));
			sum = uint16_t(sum + (a > 8 ? a : 8));
			if (a > top) {
				top = a;
			}
		}
	};
	// Whether the first way has no more to avoid than the second.
	auto first_way = [&](uint8_t from, uint8_t to) {
		uint16_t sum = 0, top = 0;
		scan(from, to, sum, top);
		if (top == 0) {
			return true;  // 0 against 0xFFFF
		}
		const uint16_t one = sum;
		scan(to, from, sum, top);
		return one <= (top > 0 ? sum : 0);
	};
	uint16_t next;
	if (delta < 0x8000) {
		if (delta < rate) {
			next = want;
		} else {
			next = first_way(h, dir) ? uint16_t(heading + rate) : uint16_t(heading - rate);
		}
	} else {
		if (0x10000u - delta < rate) {
			next = want;
		} else {
			next = first_way(dir, h) ? uint16_t(heading - rate) : uint16_t(heading + rate);
		}
	}
	g.set<uint16_t>(e, 0x10, next);
}

// 0x6264C0 (on [+0x4] of a scene's ship; the wind's direction, its
// strength, a flag): the ship moves. Its speed goes towards its battle speed
// (0x612930) less by the wind's angle, by its sails and the wind's strength,
// at most 0x179 a sub-step (0x1790 in an automatic battle); its ship's
// [+0x30] shows it (and [+0x3C] | 0x80 when it changes). It moves by it
// along its heading (16 times as far in an automatic battle). Then, if it
// heads somewhere ([+0x20] ≥ 0), it turns (0x6271D7) to the direction of
// that point, or the least avoided direction near it ([+0x27]), its sails
// set by how far and how much it has to turn. With the flag the wind counts:
// no direction closer than 0x5F to the wind's, and the sails up when the
// point is far or near the edge ([+0x127]: the area 0x2200 × 0x1E04, else
// half). The directions are cleared after.
void Sailing::fn_6264c0(size_t e, uint8_t wind, uint8_t strength, bool flag) {
	BattleGraph& g = save_.battles;
	const size_t b = scene_battle(e);
	const bool automatic = g.get<uint8_t>(b, 0xAC8) != 0;
	const uint16_t ship = uint16_t(g.get<uint32_t>(e, 0x4));
	uint8_t h = g.get<uint8_t>(e, 0x11);
	uint8_t rel = uint8_t(wind - h);
	if (rel > 0x80) {
		rel = uint8_t(-rel);
	}
	int32_t v = mul(int32_t(fn_612930(ship)), 0x334);
	v -= mul(mul(v >> 4, rel), rel) >> 10;
	v = mul(v, g.get<uint8_t>(e, 0x26));
	v >>= 2;
	v = mul(v, strength) / 5;
	const int32_t speed = g.get<int32_t>(e, 0x1C);
	const int32_t most = automatic ? 0x1790 : 0x179;
	if (v - speed > most) {
		v = speed + most;
	} else if (speed - v > most) {
		v = speed - most;
	}
	g.set<int32_t>(e, 0x1C, v);
	ShipMemory& s = save_.ships[ship].memory;
	const int32_t shown = int32_t(uint32_t(v / 0x334) << 6) / 0x73;
	if (s.s32(0x30) != shown) {
		s.s32(0x30) = shown;
		s.u16(0x3C) = uint16_t(s.u16(0x3C) | 0x80);
	}
	int32_t vx = mul(v >> 8, sine(h)) >> 8;
	int32_t vy = mul(-(v >> 8), cosine(h)) >> 8;
	if (automatic) {
		vx = int32_t(uint32_t(vx) << 4);
		vy = int32_t(uint32_t(vy) << 4);
	}
	g.set<int32_t>(e, 0x14, vx);
	g.set<int32_t>(e, 0x18, vy);
	g.set<int32_t>(e, 0x8, int32_t(uint32_t(g.get<int32_t>(e, 0x8)) + uint32_t(vx)));
	g.set<int32_t>(e, 0xC, int32_t(uint32_t(g.get<int32_t>(e, 0xC)) + uint32_t(vy)));
	const int32_t tx = g.get<int16_t>(e, 0x20), ty = g.get<int16_t>(e, 0x22);
	if (tx < 0) {
		return;
	}
	const int32_t dx = tx - g.get<int16_t>(e, 0xA), dy = ty - g.get<int16_t>(e, 0xE);
	uint8_t dir = fn_6029b0(int16_t(dx), int16_t(dy));
	const int32_t d2 = mul(dx, dx) + mul(dy, dy);
	auto avoid = [&](uint8_t k) { return g.get<uint8_t>(e, 0x27 + size_t(k)); };
	if (flag) {
		const bool wide = g.get<uint8_t>(e, 0x127) != 0;
		const int32_t width = wide ? 0x2200 : 0x1100, height = wide ? 0x1E04 : 0xF02;
		if (!(d2 < 0xAF90 && tx > 0x64 && ty > 0x64 && tx < width - 0x64 && ty < height - 0x64)) {
			if (g.get<uint8_t>(e, 0x26) < 4) {
				g.set<uint8_t>(e, 0x26, uint8_t(g.get<uint8_t>(e, 0x26) + 1));
			}
			const uint8_t r = uint8_t(dir - wind);
			if (r > 0x5F && r < 0xA1) {
				dir = uint8_t(h - wind) < 0x80 ? uint8_t(wind + 0x5F) : uint8_t(wind + 0xA1);
			}
		}
	}
	if (avoid(dir) != 0) {
		uint8_t k = uint8_t(h - dir);
		k = (k & 0x80) ? uint8_t((k >> 4) + 0xF0) : uint8_t(k >> 4);
		uint8_t best = uint8_t(dir + k);
		uint8_t i = 1, least = 0xFF;
		for (;;) {
			if (avoid(uint8_t(dir + i)) == 0 || avoid(uint8_t(dir - i)) == 0) {
				break;
			}
			++i;
			if (flag) {
				for (const uint8_t at : {uint8_t(dir + i), uint8_t(dir - i)}) {
					const uint8_t r = uint8_t(at - wind);
					if (r > 0x5F && r < 0xA1) {
						const uint8_t q = uint8_t(std::abs(0x80 - int32_t(r)) >> 1);
						if (int32_t(avoid(at)) < 0x20 - int32_t(q)) {
							g.set<uint8_t>(e, 0x27 + size_t(at), uint8_t(0x20 - q));
						}
					}
				}
			}
			if (avoid(uint8_t(dir + i)) < least) {
				least = avoid(uint8_t(dir + i));
				best = uint8_t(dir + i);
			}
			if (avoid(uint8_t(dir - i)) < least) {
				least = avoid(uint8_t(dir - i));
				best = uint8_t(dir - i);
			}
			if (i == 0x80) {
				break;
			}
		}
		dir = best;
	}
	if (flag && d2 < 0xAF90 && tx > 0x64 && ty > 0x64 && tx < 0x219C && ty < 0x1DA0) {
		uint8_t sails = g.get<uint8_t>(e, 0x26);
		auto toward = [&](uint8_t n) {
			if (sails > n) {
				--sails;
			} else if (sails < n) {
				++sails;
			}
		};
		if (d2 < 0x24) {
			if (sails > 0) {
				--sails;
			}
		} else {
			uint8_t a = uint8_t(dir - h);
			if (a > 0x80) {
				a = uint8_t(0x100 - a);
			}
			if (a > 0x40) {
				a = uint8_t(0x40 - a);
			}
			const int32_t c = sine(a) >> 8;
			if (c == 0) {
				if (sails < 4) {
					++sails;
				}
			} else {
				uint32_t t = (uint32_t(d2) << 8) / uint32_t(c);
				t = (t << 8) / uint32_t(c);
				const int32_t far = int32_t(t);
				if (far >= 0xAF90) {
					if (sails < 4) {
						++sails;
					}
				} else if (far >= 0x80AE) {
					toward(3);
				} else if (far >= 0x4DA9) {
					toward(2);
				} else if (far >= 0x1BE4) {
					toward(1);
				} else if (sails > 0) {
					--sails;
				}
			}
		}
		g.set<uint8_t>(e, 0x26, sails);
	}
	fn_6271d7(e, dir);
	for (size_t k = 0; k < 0x100; ++k) {
		g.set<uint8_t>(e, 0x27 + k, 0);
	}
}

}  // namespace p3::data

namespace p3::data {

// 0x61DBBF (method +0x4 of a scene's ship): a target, unless [+0x161]: an
// enemy ship (the other side's list: [+0x65C] for team 0, else [+0x10];
// next by [+0xC]) not out (0x6) chosen by the battle's generator, each as
// likely as its arms per hull (0x61EFDF); the first when none has any.
// [+0x128] the target's place, or 0xFFFF.
void Sailing::fn_61dbbf(size_t e) {
	BattleGraph& g = save_.battles;
	if (g.get<uint8_t>(e, 0x161) > 0) {
		return;
	}
	const size_t b = scene_battle(e);
	const uint8_t team = g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(e, 0x13D)));
	const uint16_t head = g.get<uint16_t>(b, team == 0 ? 0x65C : 0x10);
	const size_t next = size_t(g.ptr(b, 0xC));
	auto out = [&](uint16_t k) { return (g.get<uint8_t>(battle_entry(b, k), 0x12A) & 0x6) != 0; };
	int32_t total = 0;
	for (uint16_t k = head; k != 0xFFFF; k = g.get<uint16_t>(next, 2 * size_t(k))) {
		if (!out(k)) {
			total += fn_61efdf(battle_entry(b, k));
		}
	}
	if (total > 0) {
		total = int32_t(battle_rand(g, b) % uint32_t(total) + 1);
	}
	uint16_t k = head;
	while (k != 0xFFFF) {
		if (!out(k)) {
			total -= fn_61efdf(battle_entry(b, k));
			if (total <= 0) {
				break;
			}
		}
		k = g.get<uint16_t>(next, 2 * size_t(k));
	}
	g.set<uint16_t>(e, 0x128, k);
}

namespace {

// The boarding strength of a scene's ship: its sailors (one more with a
// captain) and its reserve ([+0x154]: twice the sailors if fewer, else
// added), by its fighting [+0x3F] (0 to 4; ±2 for a human merchant's ship by
// the difficulty, settings [+0x12]: 0 → +2, 2 → −2) and its captain's
// [+0xB].
struct Boarders {
	uint32_t sailors;
	uint32_t strength;
};

}  // namespace

// 0x61F1BF (on a scene's ship grappling another of a higher place,
// [+0x128]): a round of the boarding. Not while either has more than 2
// sails up. One that fled (0x20) and that the other doesn't attack
// (0x6212FB with the flag) gives up (0x60C1C9: taken by the other); else
// its team's mode [+0xACA] becomes 0xFF. Each side loses sailors by the
// other's strength against its own and the battle's generator (the
// fractions kept in [+0x13A]; 16 times as many in an automatic battle),
// each one lost may be one of the reserve (generator); a side that loses
// them all is taken (0x60C1C9, its sailors 1). Ships that lost some get
// [+0x3C] | 2. An AI side whose strength · 3 is below twice the other's
// sailors flees (0x20) unless its owner is 0xFF.
//
// The crash's sound at the start (0x443690, UI; none in the emulator, so
// [+0x14A] stays) is left out.
void Sailing::fn_61f1bf(size_t e) {
	BattleGraph& g = save_.battles;
	const size_t b = scene_battle(e);
	const uint16_t place = g.get<uint16_t>(e, 0x138), other = g.get<uint16_t>(e, 0x128);
	const size_t t = battle_entry(b, other);
	if (g.get<uint8_t>(e, 0x26) > 2 || g.get<uint8_t>(t, 0x26) > 2) {
		return;
	}
	auto team = [&](size_t x) { return g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(x, 0x13D))); };
	if (g.get<uint8_t>(e, 0x12A) & 0x20) {
		if (!fn_6212fb(t, e, true)) {
			fn_60c1c9(b, other, place);
			return;
		}
		g.set<uint8_t>(b, 0xACA + size_t(1 - team(e)), 0xFF);
	}
	if (g.get<uint8_t>(t, 0x12A) & 0x20) {
		if (!fn_6212fb(e, t, true)) {
			fn_60c1c9(b, place, other);
			return;
		}
		g.set<uint8_t>(b, 0xACA + size_t(team(e)), 0xFF);
	}
	const uint16_t captains = save_.globals.u16(global::kCaptainCount);
	auto boarders = [&](size_t x) {
		const ShipMemory& s = scene_ship(x);
		uint32_t n = s.u16(0x40);
		if (s.u16(0x42) < captains) {
			++n;
		}
		const uint32_t reserve = s.u16(0x154);
		const uint32_t fighters = reserve < n ? reserve + n : n + n;
		int8_t fight = int8_t(s.u8(0x3F));
		if (fight < 0) {
			fight = 0;
		} else if (fight > 4) {
			fight = 4;
		}
		const uint8_t owner = s.u8(0x0);
		if (owner != 0xFF && owner < save_.merchants.size() && save_.merchants[owner].memory.u16(0x8) == 0) {
			const uint8_t difficulty = save_.settings.u8(0x12);
			if (difficulty == 0) {
				fight = int8_t(fight + 2);
			} else if (difficulty == 2) {
				fight = int8_t(fight - 2);
			}
		}
		uint32_t strength = uint32_t(mul(int32_t(fight) + 0x12, int32_t(fighters)) + 10) / 20u;
		const uint16_t captain = s.u16(0x42);
		if (captain < captains && captain < save_.captains.size()) {
			const int32_t skill = save_.captains[captain].u8(0xB);
			strength = uint32_t(mul(mul(skill, 6) / 17 + 0x64, int32_t(strength))) / 100u;
		}
		return Boarders{n, strength};
	};
	Boarders me = boarders(e), them = boarders(t);
	if (me.strength == 0) {
		me.strength = 1;
	}
	if (them.strength == 0) {
		them.strength = 1;
	}
	const bool automatic = g.get<uint8_t>(b, 0xAC8) != 0;
	auto losses = [&](const Boarders& by, const Boarders& of, size_t x) {
		const uint32_t r = battle_rand(g, b) % 26u;
		const uint32_t ratio = by.strength * 100u / of.strength + (of.sailors + by.strength) * 3u;
		uint32_t lost = (r + 0x5C) * ratio * 40u / 1000u;
		if (automatic) {
			lost <<= 4;
		}
		const uint32_t sum = lost + g.get<uint16_t>(x, 0x13A);
		g.set<uint16_t>(x, 0x13A, uint16_t(sum));
		return sum >> 16;
	};
	uint32_t lost_them = losses(me, them, t);
	uint32_t lost_me = losses(them, me, e);
	// Each one lost may be one of the reserve.
	auto reserve = [&](size_t x, uint32_t lost, uint32_t sailors) {
		for (uint32_t i = lost; i > 0; --i) {
			ShipMemory& s = save_.ships[g.get<uint32_t>(x, 0x4)].memory;
			uint32_t r = s.u16(0x154);
			if (r > sailors) {
				r = sailors;
			}
			if (battle_rand(g, b) % (2 * sailors) < (r & 0xFFFF)) {
				s.u16(0x154) = uint16_t(s.u16(0x154) - 1);
			}
		}
	};
	if (lost_them != 0) {
		if (lost_them > them.sailors) {
			lost_them = them.sailors;
		}
		reserve(t, lost_them, them.sailors);
		if (lost_them == them.sailors) {
			save_.ships[g.get<uint32_t>(t, 0x4)].memory.u16(0x40) = 1;
			fn_60c1c9(b, place, other);
			return;
		}
	}
	ShipMemory& mine = save_.ships[g.get<uint32_t>(e, 0x4)].memory;
	ShipMemory& theirs = save_.ships[g.get<uint32_t>(t, 0x4)].memory;
	if (lost_me != 0) {
		if (lost_me > me.sailors) {
			lost_me = me.sailors;
		}
		reserve(e, lost_me, me.sailors);
		if (lost_me == me.sailors) {
			mine.u16(0x40) = 1;
			fn_60c1c9(b, other, place);
			return;
		}
		mine.u8(0x3C) = uint8_t(mine.u8(0x3C) | 0x2);
	}
	if (lost_them != 0) {
		theirs.u8(0x3C) = uint8_t(theirs.u8(0x3C) | 0x2);
	}
	mine.u16(0x40) = uint16_t(mine.u16(0x40) - lost_me);
	theirs.u16(0x40) = uint16_t(theirs.u16(0x40) - lost_them);
	auto flees = [&](size_t x, const Boarders& own, const Boarders& foe) {
		const uint8_t side = g.get<uint8_t>(x, 0x13D);
		return g.get<uint8_t>(b, 0x5D + size_t(side)) != 0 && own.strength * 3 < foe.sailors * 2 &&
			g.get<uint16_t>(b, 0x35A + 2 * size_t(side)) != 0xFF;
	};
	if (flees(e, me, them)) {
		g.set<uint8_t>(e, 0x12A, uint8_t(g.get<uint8_t>(e, 0x12A) | 0x20));
		return;
	}
	if (flees(t, them, me)) {
		g.set<uint8_t>(t, 0x12A, uint8_t(g.get<uint8_t>(t, 0x12A) | 0x20));
	}
}

}  // namespace p3::data
