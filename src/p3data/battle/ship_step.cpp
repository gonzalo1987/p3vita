// The step of a scene's ship (method +0x0 of a scene's ship, 0x61A9E0) and
// what it calls (docs/cloud/battles-2.md §4): its movement (0x6264C0, on
// the part of the scene's ship that starts at [+0x4]), its turn (0x6271D7).
// Offsets here are from the start of the scene's ship: its ship [+0x4], its
// place in 16.16 ([+0x8] x, [+0xC] y), its heading in 8.8 ([+0x10], the
// byte [+0x11]), its last move ([+0x14], [+0x18]), its speed [+0x1C], where
// it heads ([+0x20] x, or < 0 for nowhere, [+0x22] y), its sails [+0x26],
// its 256 directions [+0x27] (how much each is to be avoided, 0x60BCDA).

#include <algorithm>
#include <cstdlib>

#include "p3data/battle/battle_math.hpp"
#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/trade.hpp"

namespace p3::data {

namespace tick::merchants_detail {
uint8_t fn_4f9430(const SaveGame& save, int merchant);  // town_tick/merchants.cpp: a merchant's title
}  // namespace tick::merchants_detail

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
	if (g.get<uint8_t>(e, 0x14A) == 0) {
		battle_sound(b);  // 0x61F1C8: the boarding's sound 0xEABA on the screen (the first of the pair)
	}
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

namespace p3::data {

// 0x611649 (on the battle; a level): the battle's music [+0x1E94] >> 10
// raised to the level. With method +0x28 (1 for a town battle, 0 for a sea
// battle) the levels 9, 11 and 12 become 14.
void Sailing::fn_611649(size_t b, uint8_t level) {
	BattleGraph& g = save_.battles;
	if (g.get<uint32_t>(b, 0) == kTownBattleVtable && (level == 9 || level == 11 || level == 12)) {
		level = 0xE;
	}
	const uint16_t v = g.get<uint16_t>(b, 0x1E94);
	if (level > (v >> 10)) {
		g.set<uint16_t>(b, 0x1E94, uint16_t((uint32_t(level) << 10) + (v & 0x3FF)));
	}
}

// 0x620F0B (on a scene's ship; a side, a distance, two outputs the battle
// step doesn't ask for): the damage of a broadside: per gun of the side (the
// slots 0, 1, 4, 5... for side 0, 2, 3, 6, 7... for side 1; [+0x13C] of its
// ship), its two factors less by the distance over 6 times its time, then
// · 28 / 64, by its captain ([+0xB]) and fighting [+0x3F] (as the boarding),
// and the battle's generator (85 to 115 %); at least 1, or 0 without guns.
int32_t Sailing::fn_620f0b(size_t e, uint8_t side, uint16_t distance) {
	BattleGraph& g = save_.battles;
	const ShipMemory& s = scene_ship(e);
	int32_t total = 0;
	uint32_t guns = 0;
	const uint8_t first = uint8_t(side * 2);
	if (first < 0x18) {
		for (uint32_t slot = first; slot < 0x18; slot += 4) {
			for (uint32_t k = slot; k < slot + 2; ++k) {
				const uint8_t gun = s.u8(0x13C + k);
				if (gun >= 6) {
					continue;
				}
				const int32_t a = int32_t(kGunA[gun]) * kGunB[gun];
				int32_t v = a + mul(int32_t(distance), a) / (-6 * int32_t(kGunTime[gun]));
				if (v < 0) {
					v = 0;
				}
				total += v;
				++guns;
			}
		}
	}
	if (guns == 0) {
		return 0;
	}
	int32_t damage = mul(total >> 6, 2800) / 100;
	const uint16_t captains = save_.globals.u16(global::kCaptainCount);
	const uint16_t captain = s.u16(0x42);
	if (captain < captains && captain < save_.captains.size()) {
		const int32_t skill = save_.captains[captain].u8(0xB);
		damage = mul(mul(skill, 6) / 17 + 0x64, damage) / 100;
	}
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
	if (fight < 1) {
		damage += mul(int32_t(fight) - 1, damage) / 20;
	} else if (fight > 2) {
		damage += mul(int32_t(fight) - 2, damage) / 20;
	}
	const uint32_t r = battle_rand(g, scene_battle(e)) % 31u + 0x55u;
	const uint32_t d = uint32_t(mul(int32_t(r), damage)) / 100u;
	return d == 0 ? 1 : int32_t(d);
}

// 0x61FAC8..0x61FC80: a pirate's ship (owner 0xFF) with a captain sinks
// and the other side ([+0x35A] by 1 − [+0x13D]) isn't a pirate: the news 0x2A
// for everybody, with that side's merchant (home town, [+0x1A], title,
// rank at home 0x4F3760, [+0x1C], [+0x1D]; one past the merchants: the
// world's [+0x8E], 0, 0, 0, 0xFF, 0xFF) and the captain's two names.
void Sailing::sunk_pirate_news(size_t e) {
	BattleGraph& g = save_.battles;
	const size_t b = scene_battle(e);
	const ShipMemory& s = save_.ships[g.get<uint32_t>(e, 0x4)].memory;
	if (s.u8(0x0) != 0xFF) {
		return;
	}
	const int32_t other = 1 - int32_t(g.get<uint8_t>(e, 0x13D));
	const uint16_t m = g.get<uint16_t>(b, size_t(0x35A + 2 * other));
	if (m == 0xFF || s.u16(0x42) >= save_.globals.u16(global::kCaptainCount)) {
		return;
	}
	const CaptainMemory& c = save_.captains[s.u16(0x42)];
	Letter letter(0x2A);
	if (m < save_.globals.u16(global::kWorldMerchantCount)) {
		const MerchantMemory& mm = save_.merchants[m].memory;
		letter.entry.u8(5) = mm.u8(0x19);
		letter.entry.u8(8) = mm.u8(0x1A);
		letter.entry.u8(9) = tick::merchants_detail::fn_4f9430(save_, m);
		letter.entry.u8(0xA) = mm.u8(0x39C + mm.u8(0x19));
		letter.entry.u8(0xB) = mm.u8(0x1C);
		letter.entry.u8(0xC) = mm.u8(0x1D);
	} else {
		// Unreachable: a side's owner is its ships' (battle_setup.cpp), a
		// merchant or 0xFF.
		letter.entry.u8(5) = save_.globals.u8(0x6DE52E);
		letter.entry.u8(0xB) = 0xFF;
		letter.entry.u8(0xC) = 0xFF;
	}
	letter.entry.u8(0xD) = c.u8(2);
	letter.entry.u8(0xE) = c.u8(3);
	letter.entry.u8(0xF) = 0;
	send_letter(save_, kLetterToChronicle, letter);
}

// 0x61F96F (on a scene's ship; damage, sailors lost or −1, the guns' side
// or 0): a hit. Nothing once the battle is over ([+0x5C]). Sailors lost by
// the damage against the hull (the battle's generator; fractions in
// [+0x13A]) when −1. Its ship [+0x3C] | 0x100; sunk (0x2: [+0x18] 0, the
// scene's ship 0x4 alone, its items freed, the battle's [+0xAC4] 1 and
// [+0xAC6] its place) when the damage reaches its hull, else less hull.
// Then maybe a gun lost (the generator, by the damage over 28 000; side 3:
// either at random) and the sailors (the captain too when all). 2 if sunk,
// 1 if a gun was lost, else 0.
//
// When a pirate's ship with a captain sinks, the news for everybody
// (sunk_pirate_news).
uint8_t Sailing::fn_61f96f(size_t e, int32_t damage, int16_t sailors, uint8_t side) {
	BattleGraph& g = save_.battles;
	const size_t b = scene_battle(e);
	if (g.get<uint8_t>(b, 0x5C) != 0) {
		return 0;
	}
	SavedShip& ship = save_.ships[g.get<uint32_t>(e, 0x4)];
	ShipMemory& s = ship.memory;
	if (sailors < 0) {
		if (damage > 0) {
			const uint32_t r = battle_rand(g, b);
			uint32_t v = uint32_t(mul(mul(int32_t(r & 0x400) + 0xC00, damage), int32_t(s.u16(0x40)) + 1));
			const uint32_t hull = uint32_t(s.s32(0x14) >> 4);
			v = hull != 0 ? v / hull : 0;  // The game divides by 0 for a hull under 16.
			v += g.get<uint16_t>(e, 0x13A);
			g.set<uint16_t>(e, 0x13A, uint16_t(v));
			sailors = int16_t(int32_t(v) >> 16);
		} else {
			sailors = 0;
		}
	}
	uint8_t result = 0;
	if (!(g.get<uint8_t>(e, 0x12A) & 0x6) && damage > 0) {
		s.u16(0x3C) = uint16_t(s.u16(0x3C) | 0x100);
		if (damage >= s.s32(0x18)) {
			sunk_pirate_news(e);
			s.u32(0x18) = 0;
			g.set<uint8_t>(e, 0x12A, 0x4);
			g.set<uint8_t>(b, 0xAC4, 1);
			const uint16_t place = g.get<uint16_t>(e, 0x138);
			g.set<uint16_t>(b, 0xAC6, place);
			fn_619560(b, g.get<uint16_t>(size_t(g.ptr(b, 0xA74)), 2 * size_t(place)));
			fn_619560(b, g.get<uint16_t>(size_t(g.ptr(b, 0xA78)), 2 * size_t(place)));
			result = 2;
		} else {
			s.s32(0x18) -= damage;
		}
		if (side > 0 && battle_rand(g, b) % 0x6D60u < uint32_t(damage)) {
			side = side == 3 ? uint8_t((battle_rand(g, b) & 1) + 1) : uint8_t(3 - side);
			uint32_t guns = 0;
			for (uint32_t k = uint32_t(side) * 2; k < 0x18; k += 4) {
				guns += s.u8(0x13C + k) < 6 ? 1 : 0;
				guns += s.u8(0x13D + k) < 6 ? 1 : 0;
			}
			if (guns > 0) {
				uint32_t n = battle_rand(g, b) % guns;
				for (uint32_t k = uint32_t(side) * 2; k < 0x18; k += 4) {
					if (s.u8(0x13C + k) < 6) {
						if (n == 0) {
							if (result == 0) {
								result = 1;
							}
							s.u8(0x13C + k) = 0xFF;
							if (s.u8(0x13D + k) == 6) {
								s.u8(0x13D + k) = 0xFF;
							}
							s.u16(0x3C) = uint16_t(s.u16(0x3C) | 0x10);
							recalculate_cargo(ship);
							break;
						}
						--n;
					}
					if (s.u8(0x13D + k) < 6) {
						if (n == 0) {
							if (result == 0) {
								result = 1;
							}
							s.u8(0x13D + k) = 0xFF;
							s.u16(0x3C) = uint16_t(s.u16(0x3C) | 0x10);
							recalculate_cargo(ship);
							break;
						}
						--n;
					}
				}
			}
		}
	}
	if (sailors > 0) {
		s.u16(0x3C) = uint16_t(s.u16(0x3C) | 0x2);
		if (int32_t(sailors) > int32_t(s.u16(0x40))) {
			s.u16(0x42) = 0xFFFF;
			s.u16(0x40) = 0;
		} else {
			s.u16(0x40) = uint16_t(s.u16(0x40) - sailors);
		}
	}
	return result;
}

// 0x61DD4F (method +0x8 of a scene's ship; a flag): it fires. Not without
// arms, when out, taken, fleeing or grappling (0x2E), or with both sides
// loading ([+0x130], [+0x131]). Where each ship will be (4 steps ahead by
// its last move), its direction from the ship's heading: its friends near
// a broadside (0x37 to 0x49 starboard, 0xB7 to 0xC9 port, wider when close)
// make that side keep to enemies closer than them (by 0x50); the closest
// enemy within 480 on each side is its target (without the flag, an AI
// ship leaves fleeing enemies without [+0x136] & 0x40 and grappling ones
// with fewer than half the sailors of the ship they grapple, and a human
// side only fires at its own target; none it would board, 0x61F02F). In an
// automatic battle each side with a target and loaded fires at once: the
// damage (0x620F0B) hits it (0x61F96F, guns of either side), the side loads
// (0x96), its team's mode [+0xACA] one more (0xFF if the target fled), and
// the music (0x611649: 0xB when it sank, else 9) if the team is [+0xAD2] − 1.
//
// With a human side ([+0xAC8] 0, 0x61E628) each side with a target and
// loaded fires a broadside of cannon balls at where it will be (0x61E8EF,
// battle/balls.cpp); and with the order to fire (`flag`) and no target on
// either side, at the closest enemy (not out) on each side by its heading,
// only the closer side when both are loaded.
void Sailing::fn_61dd4f(size_t e, bool flag) {
	BattleGraph& g = save_.battles;
	const ShipMemory& s = scene_ship(e);
	if (s.u32(0x120) == 0 || (g.get<uint8_t>(e, 0x12A) & 0x2E) ||
		(g.get<uint8_t>(e, 0x130) != 0 && g.get<uint8_t>(e, 0x131) != 0)) {
		return;
	}
	const size_t b = scene_battle(e);
	const bool automatic = g.get<uint8_t>(b, 0xAC8) != 0;
	const uint8_t team = g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(e, 0x13D)));
	const size_t next = size_t(g.ptr(b, 0xC));
	const int32_t ex = g.get<int16_t>(e, 0xA), ey = g.get<int16_t>(e, 0xE);
	const uint8_t heading = g.get<uint8_t>(e, 0x11);
	// Where ship f will be: 4 steps of its last move by how far it is.
	auto ahead = [&](size_t f, int32_t& px, int32_t& py, int32_t& last) {
		const int32_t dx = g.get<int16_t>(f, 0xA) - ex, dy = g.get<int16_t>(f, 0xE) - ey;
		px = dx;
		py = dy;
		for (int i = 0; i < 4; ++i) {
			last = mul(py, py) + mul(px, px);
			const int32_t q = int32_t(uint32_t(root(last)) << 8) / 1328;
			int32_t vx = g.get<int32_t>(f, 0x14), vy = g.get<int32_t>(f, 0x18);
			if (automatic) {
				vx >>= 4;
				vy >>= 4;
			}
			py = (mul(vy, q) >> 16) + dy;
			px = (mul(vx, q) >> 16) + dx;
		}
	};
	int32_t starboard = 0x7FFFFFFF, port = 0x7FFFFFFF;
	uint16_t starboard_target = 0xFFFF, port_target = 0xFFFF;
	int32_t starboard_x = 0, starboard_y = 0, port_x = 0, port_y = 0;  // Where each target will be.
	for (uint16_t k = g.get<uint16_t>(b, team == 0 ? 0x10 : 0x65C); k != 0xFFFF;
		 k = g.get<uint16_t>(next, 2 * size_t(k))) {
		if (k == g.get<uint16_t>(e, 0x138)) {
			continue;
		}
		const size_t f = battle_entry(b, k);
		if (g.get<uint8_t>(f, 0x12A) & 0x6) {
			continue;
		}
		int32_t px = 0, py = 0, last = 0;
		ahead(f, px, py, last);
		const int32_t rel = uint8_t(fn_6029b0(int16_t(px), int16_t(py)) - heading);
		const int32_t d2 = mul(py, py) + mul(px, px);
		const int32_t spread = d2 < 0x559 ? 0x37 : 2000 / root(d2);
		if (rel + spread > 0x37 && rel - spread < 0x49) {
			if (d2 < starboard) {
				starboard = d2 - 0x50;
			}
		} else if (rel + spread > 0xB7 && rel - spread < 0xC9 && d2 < port) {
			port = d2 - 0x50;
		}
	}
	const bool ai = g.get<uint8_t>(b, 0x5D + size_t(g.get<uint8_t>(e, 0x13D))) != 0;
	for (uint16_t k = g.get<uint16_t>(b, team == 0 ? 0x65C : 0x10); k != 0xFFFF;
		 k = g.get<uint16_t>(next, 2 * size_t(k))) {
		const size_t f = battle_entry(b, k);
		const uint8_t flags = g.get<uint8_t>(f, 0x12A);
		if (flags & 0x6) {
			continue;
		}
		if (!flag) {
			if (ai && (flags & 0x20) && !(scene_ship(f).u8(0x136) & 0x40)) {
				continue;
			}
			if (ai && (flags & 0x8)) {
				const uint32_t theirs = scene_ship(battle_entry(b, g.get<uint16_t>(f, 0x128))).u16(0x40);
				if (int32_t(2u * scene_ship(f).u16(0x40)) < int32_t(theirs)) {
					continue;
				}
			}
			if (k != g.get<uint16_t>(e, 0x128) && !ai) {
				continue;
			}
			if (fn_61f02f(e, f, true)) {
				continue;
			}
		}
		int32_t px = 0, py = 0, last = 0;
		ahead(f, px, py, last);
		const int32_t rel = uint8_t(fn_6029b0(int16_t(px), int16_t(py)) - heading);
		const int32_t spread = last < 0x64 ? 0x37 : 500 / root(last);
		const int32_t d2 = mul(py, py) + mul(px, px);
		if (rel + spread > 0x37 && rel - spread < 0x49) {
			if (d2 < starboard && d2 <= 0x38400) {
				starboard = d2;
				starboard_target = k;
				starboard_x = px;
				starboard_y = py;
			}
		} else if (rel + spread > 0xB7 && rel - spread < 0xC9 && d2 < port && d2 <= 0x38400) {
			port = d2;
			port_target = k;
			port_x = px;
			port_y = py;
		}
	}
	if (!automatic) {
		// 0x61E628..0x61E8E3.
		auto broadside = [&](uint8_t side, int32_t x, int32_t y) {
			fn_61e8ef(e, side, uint16_t(g.get<uint16_t>(e, 0xA) + uint16_t(x)), uint16_t(g.get<uint16_t>(e, 0xE) + uint16_t(y)));
		};
		if (port_target != 0xFFFF && g.get<uint8_t>(e, 0x130) == 0) {
			broadside(0, port_x, port_y);
		}
		if (starboard_target != 0xFFFF && g.get<uint8_t>(e, 0x131) == 0) {
			broadside(1, starboard_x, starboard_y);
		}
		if (!flag || port_target != 0xFFFF || starboard_target != 0xFFFF) {
			return;
		}
		for (uint16_t k = g.get<uint16_t>(b, team == 0 ? 0x65C : 0x10); k != 0xFFFF;
			 k = g.get<uint16_t>(next, 2 * size_t(k))) {
			const size_t f = battle_entry(b, k);
			if (g.get<uint8_t>(f, 0x12A) & 0x6) {
				continue;
			}
			const int32_t dx = g.get<int16_t>(f, 0xA) - ex, dy = g.get<int16_t>(f, 0xE) - ey;
			const int32_t d2 = mul(dy, dy) + mul(dx, dx);
			const uint8_t rel = uint8_t(fn_6029b0(int16_t(dx), int16_t(dy)) - heading);
			if (rel < 0x80) {
				if (d2 < starboard) {
					starboard = d2;
					starboard_target = k;
					starboard_x = dx;
					starboard_y = dy;
				}
			} else if (d2 < port) {
				port = d2;
				port_target = k;
				port_x = dx;
				port_y = dy;
			}
		}
		if (g.get<uint8_t>(e, 0x130) != 0) {
			port_target = 0xFFFF;
		}
		if (g.get<uint8_t>(e, 0x131) != 0) {
			starboard_target = 0xFFFF;
		}
		if (port_target != 0xFFFF) {
			if (starboard_target == 0xFFFF || port < starboard) {
				broadside(0, port_x, port_y);
				return;
			}
		}
		if (starboard_target != 0xFFFF) {
			broadside(1, starboard_x, starboard_y);
		}
		return;
	}
	auto fire = [&](uint16_t target, int32_t d2, uint8_t side, size_t loading) {
		const int32_t damage = fn_620f0b(e, side, uint16_t(root(d2)));
		const size_t t = battle_entry(b, target);
		const uint8_t result = fn_61f96f(t, damage, -1, 3);
		g.set<uint8_t>(e, loading, 0x96);
		const uint8_t own = g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(e, 0x13D)));
		if (g.get<uint8_t>(b, 0xACA + size_t(own)) < 0x7F) {
			g.set<uint8_t>(b, 0xACA + size_t(own), uint8_t(g.get<uint8_t>(b, 0xACA + size_t(own)) + 1));
		}
		if (g.get<uint8_t>(t, 0x12A) & 0x20) {
			g.set<uint8_t>(b, 0xACA + size_t(own), 0xFF);
		}
		if (uint32_t(own) + 1 == g.get<uint8_t>(b, 0xAD2)) {
			fn_611649(b, result == 2 ? 0xB : 9);
		}
	};
	if (port_target != 0xFFFF && g.get<uint8_t>(e, 0x130) == 0) {
		fire(port_target, port, 0, 0x130);
	}
	if (starboard_target != 0xFFFF && g.get<uint8_t>(e, 0x131) == 0) {
		fire(starboard_target, starboard, 1, 0x131);
	}
}

}  // namespace p3::data

namespace p3::data {

namespace {

// 0x67AF7C (s16 by type): half the size of a ship's box in the scene.
constexpr int32_t kBoxHalf[4] = {55, 66, 70, 84};

}  // namespace

// 0x61A9E0 (method +0x0 of a scene's ship; the wind's direction [+0x670]):
// the ship's sub-step (docs/cloud/battles-2.md §4). In order:
// - sinking (0x4): its heading to the nearest 32nd, its speed down by 0x179
//   (0x1790 automatic) until it stops, adrift (0x6264C0 without sails);
//   out (0x2 or 0x4): nothing more;
// - out of the area (10 to 0x21F6, 0x1DFA): out (0x2), the battle's [+0xAC4]
//   0, [+0xAC6] its place, its items freed;
// - the counters [+0x130], [+0x131] (loading), [+0x161] down (by 0xF more in
//   an automatic battle); a fled ship whose enemies' mode is 0xFF fights on;
// - grappling (0x8): over if the other is out; else the lower place fights
//   the boarding (0x61F1BF), they turn side by side and slow down; adrift;
// - fled or without sailors: adrift;
// - a human side without a target: its course [+0x24] and sails [+0x148];
// - an AI side (without [+0x161]) decides whether to flee (0x1), by the
//   teams' sums ([+0xA90]...) and its arms and hull; fleeing, it heads away
//   from the enemies' centre (or the area's) to the edge; without a target
//   it looks for one (method +0x4, 0x61DBBF); a target out, of its team, fled
//   and harmless, or grappling a stronger one is dropped; else it heads to
//   where the target will be (4 steps ahead), keeps off the edges, and when
//   close (300²/360², or 420²/480² with [+0x13C]) turns broadside (0x10) to
//   the freest side, or boards;
// - its boxes in the scene ([+0xA74], [+0xA78]: hull and circle), its
//   directions cleared, and it fires (method +0x8, 0x61DD4F) if it has a
//   target or is an AI's.
// Its sails [+0x26] change at most once per 0x4B sub-steps ([+0x149]).
//
// The top edge is tested with x, not y (see «Game bugs» in
// docs/cloud/battles-2.md): reproduced. Not rewritten: the UI object
// [+0x134] (0 in the emulator).
void Sailing::fn_61a9e0(size_t e, uint8_t wind) {
	BattleGraph& g = save_.battles;
	const size_t b = scene_battle(e);
	const bool automatic = g.get<uint8_t>(b, 0xAC8) != 0;
	auto u8 = [&](size_t o) { return g.get<uint8_t>(e, o); };
	auto set8 = [&](size_t o, uint32_t v) { g.set<uint8_t>(e, o, uint8_t(v)); };
	auto flags = [&]() { return g.get<uint8_t>(e, 0x12A); };
	const uint8_t side = u8(0x13D);
	const uint8_t team = g.get<uint8_t>(b, 0x559 + size_t(side));
	const bool ai = g.get<uint8_t>(b, 0x5D + size_t(side)) != 0;
	const uint16_t place = g.get<uint16_t>(e, 0x138);
	const uint8_t strength = g.get<uint8_t>(b, 0x671);
	auto item = [&](size_t at) { return g.get<uint16_t>(size_t(g.ptr(b, at)), 2 * size_t(place)); };
	// The boxes of its two items: far away when out (with `check`), else
	// around it (its hull) and 0x168 wider (its circle).
	auto boxes = [&](bool check) {
		if (check && (flags() & 0x6)) {
			const uint16_t far = uint16_t(-100 * int32_t(place));
			fn_6197a0(b, item(0xA74), far, 0xFF9C, far, 0xFF9C);
			fn_6197a0(b, item(0xA78), far, 0xFF9C, far, 0xFF9C);
			return;
		}
		const int32_t h = kBoxHalf[scene_ship(e).u8(0xE) & 3];
		const int32_t x = g.get<int16_t>(e, 0xA), y = g.get<int16_t>(e, 0xE);
		fn_6197a0(b, item(0xA74), uint16_t(x - h), uint16_t(y - h), uint16_t(x + h), uint16_t(y + h));
		fn_6197a0(b, item(0xA78), uint16_t(x - h - 0x168), uint16_t(y - h - 0x168), uint16_t(x + h + 0x168),
			uint16_t(y + h + 0x168));
	};
	auto clear = [&]() {
		for (size_t k = 0; k < 0x100; ++k) {
			g.set<uint8_t>(e, 0x27 + k, 0);
		}
	};
	// Sails down one, once per 0x4B sub-steps.
	auto lower = [&]() {
		if (u8(0x26) > 0 && u8(0x149) == 0) {
			set8(0x26, u8(0x26) - 1);
			set8(0x149, 0x4B);
		}
	};
	// After moving: a change of sails only once per 0x4B sub-steps.
	auto keep = [&](uint8_t before) {
		if (before != u8(0x26)) {
			if (u8(0x149) != 0) {
				set8(0x26, before);
			} else {
				set8(0x149, 0x4B);
			}
		}
	};
	auto adrift = [&](bool check) {
		g.set<uint16_t>(e, 0x20, 0xFFFF);
		fn_6264c0(e, wind, strength, false);
		boxes(check);
		clear();
	};
	{
		const uint8_t h5 = uint8_t((u8(0x11) + 0x10) & 0x1F);
		if ((flags() & 0x4) && (h5 & 0x18)) {
			set8(0x11, h5 < 0x10 ? u8(0x11) - 1 : u8(0x11) + 1);
		}
	}
	if ((flags() & 0x4) && g.get<int32_t>(e, 0x1C) != 0) {
		const int32_t slow = automatic ? 0x1790 : 0x179;
		if (g.get<int32_t>(e, 0x1C) <= slow) {
			g.set<int32_t>(e, 0x1C, 0);
			return;
		}
		g.set<int32_t>(e, 0x1C, g.get<int32_t>(e, 0x1C) - slow);
		g.set<uint16_t>(e, 0x20, 0xFFFF);
		const uint8_t sails = u8(0x26);
		set8(0x26, 0);
		fn_6264c0(e, wind, strength, false);
		set8(0x26, sails);
	}
	if (flags() & 0x6) {
		return;
	}
	if (u8(0x149) != 0) {
		set8(0x149, u8(0x149) - 1);
	}
	{
		const int32_t x = g.get<int16_t>(e, 0xA), y = g.get<int16_t>(e, 0xE);
		if (x < 10 || y < 10 || x > 0x21F6 || y > 0x1DFA) {
			set8(0x12A, flags() | 0x2);
			g.set<uint8_t>(b, 0xAC4, 0);
			g.set<uint16_t>(b, 0xAC6, place);
			g.set<uint32_t>(e, 0x134, 0);  // A UI object (released by the game; none in the emulator).
			fn_619560(b, item(0xA74));
			fn_619560(b, item(0xA78));
			return;
		}
	}
	for (const size_t o : {0x130, 0x131, 0x161}) {
		if (u8(o) != 0) {
			set8(o, u8(o) - 1);
		}
	}
	if (automatic) {
		for (const size_t o : {0x130, 0x131, 0x161}) {
			set8(o, u8(o) < 0x10 ? 0 : u8(o) - 0xF);
		}
	}
	if ((flags() & 0x20) && g.get<uint8_t>(b, 0xACA + size_t(1 - team)) == 0xFF) {
		set8(0x12A, flags() & 0xDF);
	}
	if (flags() & 0x8) {
		if (g.get<uint8_t>(battle_entry(b, g.get<uint16_t>(e, 0x128)), 0x12A) & 0x6) {
			g.set<uint16_t>(e, 0x128, 0xFFFF);
			set8(0x12A, flags() & 0xF7);
		} else {
			if (place < g.get<uint16_t>(e, 0x128)) {
				fn_61f1bf(e);
			}
			if (g.get<uint16_t>(e, 0x128) != 0xFFFF) {
				lower();
				const size_t t = battle_entry(b, g.get<uint16_t>(e, 0x128));
				const int16_t dx = int16_t(g.get<uint16_t>(e, 0xA) - g.get<uint16_t>(t, 0xA));
				const int16_t dy = int16_t(g.get<uint16_t>(e, 0xE) - g.get<uint16_t>(t, 0xE));
				const uint8_t rel = uint8_t(u8(0x11) - fn_6029b0(dx, dy));
				if ((rel > 0 && rel < 0x48) || (rel > 0x80 && rel < 0xB8)) {
					set8(0x11, u8(0x11) + 2);
				} else if ((rel > 0x48 && rel < 0x80) || rel > 0xB8) {
					set8(0x11, u8(0x11) - 2);
				}
				g.set<int32_t>(e, 0x1C, g.get<int32_t>(e, 0x1C) - (g.get<int32_t>(e, 0x1C) >> 4));
			}
			adrift(true);
			return;
		}
	}
	const ShipMemory& s = scene_ship(e);
	if ((flags() & 0x20) || s.u16(0x40) == 0) {
		lower();
		adrift(true);
		return;
	}
	if (g.get<uint8_t>(b, 0x5C) == 0 && !ai && g.get<uint16_t>(e, 0x128) == 0xFFFF && !(flags() & 0x1)) {
		// A human side without a target: its course and sails.
		const int16_t course = g.get<int16_t>(e, 0x24);
		if (course >= 0) {
			if (u8(0x11) == uint8_t(course) && course < 0x100) {
				g.set<uint16_t>(e, 0x20, 0xFFFF);
			} else {
				g.set<uint16_t>(e, 0x20, uint16_t(g.get<int16_t>(e, 0xA) + (sine(uint32_t(course)) >> 10)));
				g.set<uint16_t>(e, 0x22, uint16_t(g.get<int16_t>(e, 0xE) - (cosine(uint32_t(course)) >> 10)));
			}
		}
		if (u8(0x149) == 0) {
			if (u8(0x148) < u8(0x26)) {
				set8(0x26, u8(0x26) - 1);
				set8(0x149, 0x4B);
			} else if (u8(0x148) > u8(0x26)) {
				set8(0x26, u8(0x26) + 1);
				set8(0x149, 0x4B);
			}
		}
		fn_6264c0(e, wind, strength, false);
		boxes(true);
		clear();
		return;
	}
	if (team == 1 && (flags() & 0x20) && g.get<uint8_t>(b, 0xACA) == 0xFF) {
		set8(0x12A, flags() & 0xDF);
	}
	if (u8(0x161) == 0 && ai) {
		fn_61a9e0_flee(e);
	}
	if (flags() & 0x1) {
		// Fleeing: away from the enemies' centre (or the area's, near the
		// edge), to the edge of the area.
		const int32_t x = g.get<int16_t>(e, 0xA), y = g.get<int16_t>(e, 0xE);
		int32_t dx, dy;
		if (x < 0x190 || x > 0x2070 || y < 0x190 || y > 0x1C74) {
			dx = x - 0x1100;
			dy = y - 0xF02;
		} else {
			dx = x - g.get<int32_t>(b, 0xAB4 + 4 * size_t(1 - team));
			dy = y - g.get<int32_t>(b, 0xABC + 4 * size_t(1 - team));
		}
		if (dx < 0) {
			dy = mul(-x, dy) / dx;
			dx = -x;
		} else if (dx > 0) {
			dy = mul(0x2200 - x, dy) / dx;
			dx = 0x2200 - x;
		} else {
			dy = dy < 0 ? -y : 0x1E04 - y;
		}
		if (dy < -y) {
			dx = mul(dx, -y) / dy;
			dy = -y;
		} else if (dy > 0x1E04 - y) {
			dx = mul(0x1E04 - y, dx) / dy;
			dy = 0x1E04 - y;
		}
		g.set<uint16_t>(e, 0x20, uint16_t(x + dx));
		g.set<uint16_t>(e, 0x22, uint16_t(y + dy));
		const uint8_t before = u8(0x26);
		fn_6264c0(e, wind, strength, true);
		keep(before);
	} else if (g.get<uint16_t>(e, 0x128) == 0xFFFF) {
		g.set<uint16_t>(e, 0x20, 0xFFFF);
		if (ai) {
			fn_61dbbf(e);
		}
		const uint8_t before = u8(0x26);
		fn_6264c0(e, wind, strength, ai);
		keep(before);
		boxes(false);
		clear();
		return;
	} else {
		const size_t t = battle_entry(b, g.get<uint16_t>(e, 0x128));
		const uint8_t tflags = g.get<uint8_t>(t, 0x12A);
		const uint8_t tteam = g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(t, 0x13D)));
		bool drop = (tflags & 0x6) != 0;
		if (!drop && ai) {
			if (tteam == team) {
				drop = true;
			} else if ((tflags & 0x20) && !fn_6212fb(e, t, false) && fn_61efdf(t) == 0) {
				drop = true;
			} else if (tflags & 0x8) {
				const uint32_t theirs = scene_ship(battle_entry(b, g.get<uint16_t>(t, 0x128))).u16(0x40);
				if (int32_t(scene_ship(t).u16(0x40)) > int32_t(2 * theirs)) {
					drop = true;
				}
			}
		}
		if (drop) {
			g.set<uint16_t>(e, 0x20, 0xFFFF);
			if (ai) {
				fn_61dbbf(e);
			} else {
				g.set<uint16_t>(e, 0x128, 0xFFFF);
				set8(0x148, u8(0x26));
				g.set<uint16_t>(e, 0x24, u8(0x11));
			}
			const uint8_t before = u8(0x26);
			fn_6264c0(e, wind, strength, ai || g.get<uint8_t>(b, 0x5C) != 0);
			keep(before);
			boxes(false);
			clear();
			return;
		}
		g.set<uint16_t>(e, 0x20, g.get<uint16_t>(t, 0xA));
		g.set<uint16_t>(e, 0x22, g.get<uint16_t>(t, 0xE));
		const int32_t x = g.get<int16_t>(e, 0xA), y = g.get<int16_t>(e, 0xE);
		const int32_t dx0 = g.get<int16_t>(e, 0x20) - x, dy0 = g.get<int16_t>(e, 0x22) - y;
		int32_t dx = dx0, dy = dy0;
		for (int i = 0; i < 4; ++i) {
			const int32_t q = int32_t(uint32_t(root(mul(dx, dx) + mul(dy, dy))) << 8) / 0x530;
			int32_t vx = g.get<int32_t>(t, 0x14), vy = g.get<int32_t>(t, 0x18);
			if (automatic) {
				vx >>= 4;
				vy >>= 4;
			}
			dx = dx0 + (mul(vx, q) >> 16);
			dy = dy0 + (mul(vy, q) >> 16);
		}
		const uint32_t d2 = uint32_t(mul(dx, dx) + mul(dy, dy));
		if (tteam != team) {
			// The edges of the area are to be avoided, unless boarding the
			// target that way.
			auto boarding = [&]() { return fn_61f02f(e, t, false); };
			if (x < 0x190 && !(boarding() && dx <= 0)) {
				const uint8_t v = uint8_t(std::min(0x190 - x, 0xFF));
				for (size_t k = 0; k < 0x80; ++k) {
					g.set<uint8_t>(e, 0x27 + 0x80 + k, v);
				}
			}
			if (x < 0x190 && !(boarding() && dy <= 0)) {  // The game tests x (y meant).
				for (size_t k = 0; k < 0x80; ++k) {
					g.set<uint8_t>(e, 0x27 + ((k + 0xC0) & 0xFF), 0xFF);
				}
			}
			if (x > 0x2070 && !(boarding() && dx >= 0)) {
				for (size_t k = 0; k < 0x80; ++k) {
					g.set<uint8_t>(e, 0x27 + k, 0xFF);
				}
			}
			if (y > 0x1C74 && !(boarding() && dy >= 0)) {
				for (size_t k = 0; k < 0x80; ++k) {
					g.set<uint8_t>(e, 0x27 + 0x40 + k, 0xFF);
				}
			}
		}
		const bool wide = u8(0x13C) != 0;
		const uint32_t near = wide ? 0x15F90 : 0x2B110, far = wide ? 0x1FA40 : 0x38400;
		if (d2 < near) {
			if (tteam == team) {
				lower();
				g.set<uint16_t>(e, 0x20, 0xFFFF);
			} else {
				set8(0x12A, flags() | 0x10);
			}
		}
		if (d2 < far && (flags() & 0x10) && !fn_61f02f(e, t, true)) {
			// Broadside: across the line to the target, the freest side.
			g.set<uint16_t>(e, 0x20, 0xFFFF);
			uint8_t dir = fn_6029b0(int16_t(dy), -int32_t(int16_t(dx)));
			const uint8_t h = u8(0x11);
			if (uint8_t(h - dir + 0x40) > 0x80) {
				dir = uint8_t(dir + 0x80);
			}
			auto avoid = [&](uint8_t k) { return g.get<uint8_t>(e, 0x27 + size_t(k)); };
			if (avoid(dir) > 0x80 && avoid(uint8_t(dir + 0x80)) < avoid(dir)) {
				dir = uint8_t(dir + 0x80);
			}
			if (avoid(dir) != 0) {
				uint8_t k = uint8_t(dir - h);
				k = (k & 0x80) ? uint8_t((k >> 1) + 0x80) : uint8_t(k >> 1);
				uint8_t best = uint8_t(h + k);
				uint8_t i = 1, least = 0xFF;
				for (;;) {
					if (avoid(uint8_t(dir + i)) == 0 || avoid(uint8_t(dir - i)) == 0) {
						break;
					}
					++i;
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
			if (int32_t(dir) - int32_t(h) > 1 || int32_t(h) - int32_t(dir) > 1) {
				fn_6271d7(e, dir);
			}
			if (u8(0x149) == 0 && u8(0x26) < 4) {
				set8(0x26, u8(0x26) + 1);
				set8(0x149, 0x4B);
			}
			fn_6264c0(e, wind, strength, false);
		} else {
			set8(0x12A, flags() & 0xEF);
			const uint8_t before = u8(0x26);
			fn_6264c0(e, wind, strength, true);
			keep(before);
		}
	}
	boxes(true);
	clear();
	if (g.get<uint16_t>(e, 0x128) != 0xFFFF || ai) {
		fn_61dd4f(e, false);
	}
}

// The decision to flee of an AI ship (part of 0x61A9E0, at 0x61BD39): by
// the enemy's best arms, its hull, and the teams' sums of the sub-step
// (strength [+0xA90], sailors [+0xA98], fighters [+0xA9C], ships [+0xAB0];
// team 0 at the first of each pair). Team 0 flees (0x1, and the battle's
// [+0xAC9]) when far weaker, its hull low or unarmed and outnumbered,
// unless its owner [+0x35A] is 0xFE and its ship's [+0x0] is [+0x35C]; team
// 1 likewise, or (a pirate team, 0x611149) heads off (0x20).
void Sailing::fn_61a9e0_flee(size_t e) {
	BattleGraph& g = save_.battles;
	const size_t b = scene_battle(e);
	const uint8_t side = g.get<uint8_t>(e, 0x13D);
	const uint8_t team = g.get<uint8_t>(b, 0x559 + size_t(side));
	const ShipMemory& s = scene_ship(e);
	auto flag = [&](uint8_t bit) { g.set<uint8_t>(e, 0x12A, uint8_t(g.get<uint8_t>(e, 0x12A) | bit)); };
	auto unflag = [&]() { g.set<uint8_t>(e, 0x12A, uint8_t(g.get<uint8_t>(e, 0x12A) & 0xFE)); };
	uint32_t limit = uint32_t(mul(g.get<int32_t>(b, 0xAA0 + 4 * size_t(1 - team)), 0x8C));
	if (g.get<uint8_t>(b, 0xAB0 + size_t(team)) > 1) {
		if (uint32_t(mul(int32_t(limit), 3)) > s.u32(0x14)) {
			limit = uint32_t(s.s32(0x14) / 3);
		}
	} else if (uint32_t(mul(int32_t(limit), 3)) > 2 * s.u32(0x14)) {
		limit = uint32_t(mul(s.s32(0x14), 2) / 3);
	}
	if (uint32_t(mul(int32_t(limit), 4)) < s.u32(0x14)) {
		limit = uint32_t(s.s32(0x14) >> 2);
	}
	const uint32_t a90 = g.get<uint32_t>(b, 0xA90), a94 = g.get<uint32_t>(b, 0xA94);
	const int32_t a98 = g.get<uint16_t>(b, 0xA98), a9a = g.get<uint16_t>(b, 0xA9A);
	const int32_t a9c = g.get<uint16_t>(b, 0xA9C), a9e = g.get<uint16_t>(b, 0xA9E);
	const uint8_t n0 = g.get<uint8_t>(b, 0xAB0), n1 = g.get<uint8_t>(b, 0xAB1);
	if (team == 0) {
		if (g.get<uint16_t>(b, 0x35A + 2 * size_t(side)) == 0xFF) {
			return;
		}
		if (g.get<uint32_t>(e, 0x144) == s.u32(0x18) && g.get<uint8_t>(b, 0xAC9) == 0) {
			return;
		}
		auto flee = [&]() {
			flag(0x1);
			g.set<uint8_t>(b, 0xAC9, 1);
		};
		if (uint32_t(mul(int32_t(a90), n0)) * 2 < a94 && a9c * n0 < a9a * 2) {
			flee();
		} else if (uint32_t(mul(int32_t(a90), n0)) * 4 < a94) {
			flee();
		} else if (a9c * n0 * 3 < a9a) {
			flee();
		} else if (s.u32(0x18) < limit) {
			flee();
		} else if (s.u32(0x120) == 0 && int32_t(s.u16(0x40)) < a9a * 2) {
			flee();
		} else if (g.get<uint16_t>(b, 0x35A) != 0xFE || s.u8(0x0) != g.get<uint16_t>(b, 0x35C)) {
			unflag();
		}
		return;
	}
	if (a9e * n1 * 3 < a98) {
		flag(0x1);
	} else if (uint32_t(mul(int32_t(a94), n1)) * 4 < a90) {
		const bool away = g.get<uint16_t>(b, 0x35A + 2 * size_t(side)) != 0xFF &&
			fn_612930(uint16_t(g.get<uint32_t>(e, 0x4))) * 10 <= g.get<uint32_t>(b, 0xAA8) * 9 &&
			g.get<uint32_t>(e, 0x144) != s.u32(0x18) && g.get<uint8_t>(b, 0xACA) != 0xFF &&
			g.get<uint8_t>(b, 0xACB) == 0 && fn_611149(b, side);
		flag(away ? 0x20 : 0x1);
	} else if (uint32_t(mul(int32_t(a94), n1)) * 2 < a90 && a9e * n1 < a98 * 2) {
		flag(0x1);
	} else if (s.u32(0x18) < limit) {
		flag(0x1);
	} else if (s.u32(0x120) == 0 && int32_t(s.u16(0x40)) < a98 * 2) {
		flag(0x1);
	} else if (g.get<uint16_t>(b, 0x35C) != 0xFE || s.u8(0x0) != g.get<uint16_t>(b, 0x35A)) {
		unflag();
	}
}

}  // namespace p3::data

namespace p3::data {

// 0x611149 (on the battle; a side): whether a ship of team 1's side may head
// off: a side of team 1 with its mode [+0xACB] at most 3, while side 0's list
// ([+0x10]) has a ship not out, fled nor taken (0x66).
bool Sailing::fn_611149(size_t b, uint8_t side) const {
	const BattleGraph& g = save_.battles;
	if (g.get<uint8_t>(b, 0x559 + size_t(side)) == 0 || g.get<uint8_t>(b, 0xACB) > 3) {
		return false;
	}
	const size_t next = size_t(g.ptr(b, 0xC));
	for (uint16_t k = g.get<uint16_t>(b, 0x10); k != 0xFFFF; k = g.get<uint16_t>(next, 2 * size_t(k))) {
		if (!(g.get<uint8_t>(battle_entry(b, k), 0x12A) & 0x66)) {
			return true;
		}
	}
	return false;
}

}  // namespace p3::data
