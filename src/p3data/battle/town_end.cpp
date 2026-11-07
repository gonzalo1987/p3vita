// The end of a town battle (the battles' piece 6, part 4): method +0x1C
// (0x5FE7E0: the plunder, the news and the report), method +0x4 (0x602990:
// the destructor 0x5FB9F0, with the sea battle's 0x603BC0 inside: the convoys
// back to their ways) and, for a pirate's nest, what is left of it (0x514920)
// with operation 0xB4 (0x542E40: a new pirate when the nest is destroyed).
//
// Left out, as at sea until piece 7: a human side's window at the end
// ([+0x1310] and [+0x12EC], 0x41D9B0: only for a side the player directs) and
// the cannon balls (only fired with a human side).

#include <cstring>
#include <string>

#include "p3data/battle/pirate_nests.hpp"
#include "p3data/game_texts.hpp"
#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kTownCount = 0x6DE4B0;  // u32
constexpr uint32_t kNetwork = 0x6DF31C;  // u32
constexpr uint32_t kPirates = global::kShips + 0xC;  // 5 pointers to the pirate objects
constexpr uint32_t kEventHead = 0x6DD744;  // u16
constexpr uint32_t kEventCount = 0x6DD748;  // u16

// A merchant's rank in his home town ([+0x39C + [+0x19]]) with his title
// (0x4F9430) in the high nibble, as the town battle's news carry them.
uint8_t rank_and_title(const SaveGame& save, uint16_t merchant) {
	const MerchantMemory& m = save.merchants[merchant].memory;
	return uint8_t(m.u8(0x39C + m.u8(0x19)) | tick::merchants_detail::fn_4f9430(save, merchant) << 4);
}

}  // namespace

// 0x5FE7E0 (method +0x1C of a town battle; whether side A has no ship left):
// the end of the battle, once [+0x15C] is 0 or above 0xF000. The town is no
// longer attacked ([+0x2C8] without 0x800) and [+0x130E] keeps `lost`. The
// owners of the attacker's convoy's ships that are no side yet become sides
// of team 0 (only in the arrays, as at sea). Then one letter object, reused:
// - side A won: if a defender ship of a merchant fled, the letter 0x3B to
//   him (a coward: [+0x8], [+0xA], [+0xC] his [+0x1A], [+0x1C], [+0x1D]) and
//   his reputation there down (0x4F8CA0). A town is plundered (0x633DB0 with
//   the attacker's convoy: the wares go into its ships; the music by whether
//   it paid, 0x611649 with 0xF, else 0), a nest gives (r + 2 · its [+0x16]) ·
//   50 (r by the battle's generator, modulo its [+0x16]); the money goes to
//   the convoy's owner if he is a merchant ([+0x0] and [+0x4B8]), and the news
//   0x3A for everybody tells it (the town, the loot, the defender's merchant
//   or 0xFF, the attacker's owner; for a nest, [+0xC] | 0x80, the attacker's
//   merchant and the nest's captain);
// - side A lost (unless the battle never had an attacker, [+0xAC4] 3): the
//   news 0x39 (the town and the defender's merchant, or for a nest the
//   attacker's merchant and the nest's captain, [+0xD] 1).
// The report (as at sea, without the hulls' skip, the plundered ships and the
// music): a line per ship of side A's list then side D's, by the side it
// started on ([+0x142]), and the result of each team: the town's name (from
// the table 0x6DDA00 by the town's index, the letters' bug, fixed behind
// fix_game_bugs()) or the nest's captain, and the loot. Each side of the
// computer that is a merchant gets it as a letter 0x29 ([+0x5] the town, or
// his home for a nest). Then the battle over ([+0x5C]) and [+0x15C] 1
// (automatic) or 250.
void Sailing::fn_5fe7e0(size_t b, bool lost) {
	BattleGraph& g = save_.battles;
	GlobalMemory& gl = save_.globals;
	auto u8 = [&](size_t o) { return g.get<uint8_t>(b, o); };
	auto u16 = [&](size_t o) { return g.get<uint16_t>(b, o); };
	{
		const uint16_t left = u16(0x15C);
		if (left != 0 && left < 0xF000) {
			return;
		}
	}
	const uint32_t towns = gl.u32(kTownCount);
	const uint8_t town = u8(0x1E98);
	const bool is_town = town < towns;
	if (is_town) {
		save_.towns[town].memory.u32(0x2C8) &= ~0x800u;
	}
	g.set<uint8_t>(b, 0x130E, lost ? 1 : 0);
	const uint16_t ships = gl.u16(global::kShipCount);
	const uint16_t convoys = save_.convoy_count();
	const uint16_t merchants = gl.u16(global::kWorldMerchantCount);
	auto owner = [&](uint32_t s) { return uint16_t(save_.ships[s].memory.u8(0x0)); };
	uint32_t sides = u8(0x558);
	if (u16(0x65E) < convoys) {
		for (uint32_t s = save_.convoys[u16(0x65E)].u16(0xA); s < ships; s = save_.ships[s].memory.u16(0x6)) {
			bool known = false;
			for (uint32_t k = 0; k < sides; ++k) {
				if (owner(s) == u16(0x35A + 2 * size_t(k))) {
					known = true;
				}
			}
			if (!known) {
				g.set<uint16_t>(b, 0x35A + 2 * size_t(sides), owner(s));
				g.set<uint8_t>(b, 0x5D + size_t(sides), 1);
				g.set<uint8_t>(b, 0x559 + size_t(sides), 0);
				++sides;
			}
		}
	}
	const size_t next = size_t(g.ptr(b, 0xC));
	auto nx = [&](uint16_t k) { return g.get<uint16_t>(next, 2 * size_t(k)); };
	Letter letter;  // 0x51A850: one object for all the letters.
	uint32_t loot = 0;  // [esp+0x14]: never set when side A lost (the texts then don't print it).
	if (!lost) {
		if (is_town && u16(0x65A) > 0 && u16(0x35C) != 0xFF) {
			bool fled = false;
			for (uint16_t k = u16(0x65C); k != 0xFFFF && !fled; k = nx(k)) {
				fled = (g.get<uint8_t>(battle_entry(b, k), 0x12A) & 0x2) != 0;
			}
			if (fled) {
				const uint16_t d = u16(0x35C);
				const MerchantMemory& m = save_.merchants[d].memory;
				letter.entry.u8(0x4) = 0x3B;
				letter.entry.u8(0x5) = town;
				letter.entry.u16(0x8) = m.u8(0x1A);
				letter.entry.u16(0xA) = m.u8(0x1C);
				letter.entry.u16(0xC) = m.u8(0x1D);
				send_letter(save_, d, letter);
				tick::diet_detail::fn_4f8ca0(save_, d, town);
			}
		}
		// The attacker's convoy's owner (the game reads [+0x65E] without checking it).
		const uint16_t attacker = u16(0x65E) < convoys ? save_.convoys[u16(0x65E)].u8(0x0) : 0xFF;
		auto take = [&](uint32_t amount) {
			if (attacker < merchants) {
				MerchantMemory& m = save_.merchants[attacker].memory;
				m.u32(0x4B8) += amount;
				m.u32(0x0) += amount;
			}
		};
		if (is_town) {
			uint16_t offices = 0;
			const bool paid = town < save_.sieges.size() &&
				tick::sieges_detail::fn_633db0(save_, town, loot, offices, u16(0x65E));
			if (paid) {
				fn_611649(b, 0xF);
			} else {
				g.set<uint16_t>(b, 0x1E94, 0);
			}
			take(loot);
			letter.entry.u32(0x8) = loot;
			letter.entry.u8(0x4) = 0x3A;
			letter.entry.u8(0x5) = town;
			letter.entry.u8(0xF) = uint8_t(attacker);
			if (u16(0x35C) == 0xFF) {
				letter.entry.u8(0xD) = 0xFF;
				letter.entry.u8(0xE) = 0xFF;
			} else {
				const MerchantMemory& m = save_.merchants[u16(0x35C)].memory;
				letter.entry.u8(0xC) = rank_and_title(save_, u16(0x35C));
				letter.entry.u8(0xD) = m.u8(0x1C);
				letter.entry.u8(0xE) = m.u8(0x1D);
			}
		} else {
			const size_t k = size_t(0xFE - town);
			const uint32_t count = k < 5 && save_.pirates[k] ? save_.pirates[k]->u16(0x16) : 0;
			const uint32_t r = battle_rand(g, b);
			// With [+0x16] 0 the game divides by 0. Unreachable: the attack on a nest
			// (alderman.cpp) only goes to a pirate that is there.
			loot = count != 0 ? (r % count + 2 * count) * 50 : 0;
			if (attacker < merchants) {
				take(loot);
				letter.entry.u8(0x5) = save_.merchants[attacker].memory.u8(0x19);
			} else {
				letter.entry.u8(0x5) = gl.u8(0x6DE52E);
			}
			// The attacker's side is a merchant (the Hanse task's convoy; the game doesn't check it).
			const MerchantMemory& m = save_.merchants[u16(0x35A)].memory;
			letter.entry.u32(0x8) = loot | uint32_t(u8(0x1E90)) << 24;
			letter.entry.u8(0x4) = 0x3A;
			letter.entry.u8(0xC) = uint8_t(rank_and_title(save_, u16(0x35A)) | 0x80);
			letter.entry.u8(0xD) = m.u8(0x1C);
			letter.entry.u8(0xE) = m.u8(0x1D);
			letter.entry.u8(0xF) = u8(0x1E91);
		}
		send_letter(save_, kLetterToChronicle, letter);
	} else if (u8(0xAC4) != 3) {
		letter.entry.u8(0x4) = 0x39;
		if (is_town) {
			letter.entry.u8(0x5) = town;
			if (u16(0x35C) == 0xFF) {
				letter.entry.u8(0xA) = 0xFF;
				letter.entry.u8(0xB) = 0xFF;
			} else {
				const MerchantMemory& m = save_.merchants[u16(0x35C)].memory;
				letter.entry.u8(0x8) = tick::merchants_detail::fn_4f9430(save_, u16(0x35C));
				letter.entry.u8(0x9) = m.u8(0x39C + m.u8(0x19));
				letter.entry.u8(0xA) = m.u8(0x1C);
				letter.entry.u8(0xB) = m.u8(0x1D);
			}
			letter.entry.u8(0xD) = 0;
		} else {
			const MerchantMemory& m = save_.merchants[u16(0x35A)].memory;
			letter.entry.u8(0x5) = m.u8(0x19);
			letter.entry.u8(0x8) = tick::merchants_detail::fn_4f9430(save_, u16(0x35A));
			letter.entry.u8(0x9) = m.u8(0x39C + m.u8(0x19));
			letter.entry.u8(0xA) = m.u8(0x1C);
			letter.entry.u8(0xB) = m.u8(0x1D);
			letter.entry.u8(0xC) = u8(0x1E90);
			letter.entry.u8(0xD) = 1;
		}
		send_letter(save_, kLetterToChronicle, letter);
	}
	// The report (0x5FEEAF..0x5FF46D): its six buffers of ([+0x14] + 8) · 64
	// bytes are strings here.
	auto text = [&](GameText id) { return texts_ ? texts_->raw(id) : std::string(); };
	auto ship_line = [&](GameText id, uint32_t s) {
		const ShipMemory& m = save_.ships[s].memory;
		std::string name;
		for (size_t i = 0x160; i < 0x180 && m.u8(i) != 0; ++i) {
			name += char(m.u8(i));
		}
		const std::string type = text(GameText(int(GameText::BattleShipSnaikka) + (m.u8(0xE) & 3)));
		// With no hull the game divides by 0. Unreachable: every ship has one.
		const int32_t hull = m.s32(0x14) != 0 ? int32_t(uint32_t(m.s32(0x18)) * 100) / m.s32(0x14) : 0;
		return GameTexts::sprintf_raw(text(id), {type, name, std::to_string(hull)});
	};
	std::string lines[2], taken_lines[2];  // [the side it started on]: B3, B4 and B6, B5.
	uint8_t on0 = 0, kept0 = 0;  // Side 0's ships, and those not sunk nor taken.
	for (const size_t head : {size_t(0x10), size_t(0x65C)}) {
		for (uint16_t k = u16(head); k != 0xFFFF; k = nx(k)) {
			const size_t e = battle_entry(b, k);
			const uint32_t s = g.get<uint32_t>(e, 0x4);
			const uint8_t fl = g.get<uint8_t>(e, 0x12A);
			const bool first = g.get<uint8_t>(e, 0x142) == 0;
			std::string line, taken_line;
			if (fl & 0x4) {
				line = ship_line(GameText::BattleReportSunk, s);
			} else if (g.get<uint8_t>(e, 0x13D) != g.get<uint8_t>(e, 0x142)) {
				line = ship_line(GameText::BattleReportCaptured, s);
				taken_line = ship_line(GameText::BattleReportShip, s);
			} else {
				line = ship_line(fl & 0x2 ? GameText::BattleReportFled : GameText::BattleReportShip, s);
				if (first) {
					++kept0;
				}
			}
			if (first) {
				++on0;
			}
			lines[first ? 0 : 1] += line;
			taken_lines[first ? 0 : 1] += taken_line;
		}
	}
	const std::string town_name = is_town ? tick::letter_town_name(save_, town) : std::string();
	const std::string captain[2] = {game_first_name(save_, u8(0x1E90)), game_family_name(save_, u8(0x1E91))};
	const std::string amount = std::to_string(int32_t(loot));
	std::string report[2];  // B2 for team 0 (the attackers), B1 for team 1.
	if (!lost) {
		if (is_town) {
			report[0] = GameTexts::sprintf_raw(
				text(u8(0xAC4) == 3 ? GameText::TownReportUndefended : GameText::TownReportBrokeThrough), {town_name, amount});
			report[1] = GameTexts::sprintf_raw(text(GameText::TownReportPlundered), {town_name});
		} else {
			report[0] = GameTexts::sprintf_raw(text(GameText::NestReportRuined), {captain[0], captain[1], amount});
		}
	} else {
		const bool fled = 2 * int32_t(kept0) > int32_t(on0);
		if (is_town) {
			report[0] = GameTexts::sprintf_raw(
				text(fled ? GameText::TownReportAttackFled : GameText::TownReportAttackLost), {town_name});
			report[1] = GameTexts::sprintf_raw(text(fled ? GameText::TownReportProtected : GameText::TownReportSaved), {town_name});
		} else {
			report[0] = GameTexts::sprintf_raw(
				text(fled ? GameText::NestReportAttackFled : GameText::NestReportAttackLost), {captain[0], captain[1], amount});
		}
	}
	for (size_t t = 0; t < 2; ++t) {
		report[t] += text(GameText::BattleReportOwnShips);
		report[t] += lines[t];
		if (!taken_lines[1 - t].empty()) {
			report[t] += text(GameText::BattleReportCapturedShips);
			report[t] += taken_lines[1 - t];
		}
		report[t] += text(GameText::BattleReportEnemyShips);
		report[t] += lines[1 - t];
	}
	// The letters 0x29 (0x5FF4C5..0x5FF570) to each side of the computer that is
	// a merchant; a human side gets the battle's window instead (part 7).
	for (uint32_t k = 0; k < sides; ++k) {
		const uint16_t m = u16(0x35A + 2 * size_t(k));
		if (m >= merchants || u8(0x5D + size_t(k)) == 0) {
			continue;
		}
		letter.entry.u8(0x4) = 0x29;
		letter.entry.u8(0x5) = is_town ? town : save_.merchants[m].memory.u8(0x19);
		const std::string& body = report[u8(0x559 + size_t(k)) == 0 ? 0 : 1];
		letter.entry.u32(0x8) = 0;  // The buffer's pointer (p3vita keeps the buffer apart).
		letter.entry.u32(0xC) = uint32_t(body.size() & 0xFFFF);
		letter.buffer.assign(body.begin(), body.end());
		send_letter(save_, m, letter);
		letter.entry.u32(0x8) = 0;
		letter.entry.u32(0xC) = 0;
		letter.buffer.clear();
	}
	g.set<uint8_t>(b, 0x5C, 1);
	g.set<uint16_t>(b, 0x15C, u8(0xAC8) != 0 ? 1 : 0xFA);
}

// 0x602990 (method +0x4 of a town battle; free) → the destructor 0x5FB9F0:
// its slot in the battles object cleared (and the count [+0x404] down to
// the last used); the [+0x3FD] of a battle on screen; back to the normal time
// with a human side (0x612D20). The scene's ships go (0x61A6D0: [+0x3FD] 0 if
// the ship's window was open); when side A won at a nest, side D's ships lose
// their hull first. When it ended ([+0x5C], [+0x15C] 0): at a town, maybe an
// event 5 for the attacker's convoy by the music (0x50CB80, with the town as
// the other side); the defender's convoy (unless its owner is no merchant:
// 0xFF), the attacker's and those that joined ([+0x2F1C], [+0x2F20]) back to
// their ways (0x50BFD0, each with the other side's owner). A nest then sees
// what is left of it (0x514920). The sea battle's destructor 0x603BC0 finds
// the slot already empty and only frees the scene's blocks; the blocks are no
// longer reachable from the battles object.
void Sailing::fn_5fb9f0(size_t b) {
	BattleGraph& g = save_.battles;
	GlobalMemory& gl = save_.globals;
	auto u8 = [&](size_t o) { return g.get<uint8_t>(b, o); };
	auto u16 = [&](size_t o) { return g.get<uint16_t>(b, o); };
	const uint16_t slot = u16(0x662);
	g.set<uint32_t>(0, 4 * size_t(slot), 0);
	uint8_t count = g.get<uint8_t>(0, 0x404);
	while (count != 0 && g.get<uint32_t>(0, 4 * size_t(count - 1)) == 0) {
		--count;
	}
	g.set<uint8_t>(0, 0x404, count);
	if (u8(0x12EA) != 0) {
		g.set<uint8_t>(0, 0x3FD, 0);
	}
	if (gl.u32(kNetwork) == 0) {
		bool human = false;
		for (size_t i = 0; i < u8(0x558); ++i) {
			if (u8(0x5D + i) == 0) {
				human = true;
			}
		}
		if (human) {
			fn_612d20(false);
		}
	}
	const uint32_t towns = gl.u32(kTownCount);
	const uint8_t town = u8(0x1E98);
	const size_t next = size_t(g.ptr(b, 0xC));
	for (const auto& [count_at, head_at] : {std::pair<size_t, size_t>{0x658, 0x10}, std::pair<size_t, size_t>{0x65A, 0x65C}}) {
		if (u16(count_at) == 0) {
			continue;
		}
		for (uint16_t k = u16(head_at); k != 0xFFFF; k = g.get<uint16_t>(next, 2 * size_t(k))) {
			const int e = g.ptr(size_t(g.ptr(b, 0x4)), 4 * size_t(k));
			if (head_at == 0x65C && u8(0x130E) == 0 && town >= towns && e >= 0) {
				save_.ships[g.get<uint32_t>(size_t(e), 0x4)].memory.s32(0x18) = 0;
			}
			if (e >= 0 && g.get<uint8_t>(size_t(e), 0x160) != 0) {
				g.set<uint8_t>(0, 0x3FD, 0);  // 0x61A6D0: the ship's window was open.
			}
		}
	}
	// The battle on screen ([0x6E59CC] = the slot) with the interface's map:
	// the interface (*0x6CBDD8).
	if (u8(0x5C) != 0 && u16(0x15C) == 0) {
		const uint16_t convoys = save_.convoy_count();
		if (town < towns) {
			const uint16_t music = u16(0x1E94);
			if (battle_rand(g, b) % 1000u < uint32_t(music & 0x3FF)) {
				const uint32_t r = battle_rand(g, b);
				// The game reads the convoy without checking it (a battle's attacker has one).
				if (u16(0x65E) < convoys) {
					fn_50cb80(u16(0x65E), town, r, uint8_t(music >> 10));
				}
			}
		}
		// Both of the game's branches by [+0x130E] do the same. The convoys
		// without checking them (they exist while the battle runs).
		if (u16(0x35C) != 0xFF && u16(0x660) < convoys) {
			fn_50bfd0(u16(0x660), 0, uint8_t(u16(0x35A)));
		}
		if (u16(0x65E) < convoys) {
			fn_50bfd0(u16(0x65E), 0, uint8_t(u16(0x35C)));
		}
		const int list = g.ptr(b, 0x2F1C);
		for (uint32_t i = 0; i < u16(0x2F20) && list >= 0; ++i) {
			const uint16_t c = g.get<uint16_t>(size_t(list), 2 * size_t(i));
			if (c >= convoys) {
				continue;
			}
			const bool own = save_.convoys[c].u8(0x0) == u16(0x35A);
			fn_50bfd0(c, 0, uint8_t(own ? u16(0x35C) : u16(0x35A)));
		}
	}
	if (town >= towns) {
		const size_t k = size_t(0xFE - town);
		if (k < 5 && save_.pirates[k]) {
			fn_514920(k);
		}
	}
}

// 0x514920 (on pirate k's object): its nest after a town battle. The ships
// of its convoy [+0x10] leave it: those with a hull back to the nest's list
// [+0x12], the others deleted (0x507FB0); the convoy is deleted (0x50A3D0)
// and [+0x10] 0xFFFF. With no ship back and no tower standing (an entry
// 0x30 or 0x31 of its pool with a damage below 200) the nest is destroyed:
// [+0x15] 1 (the day of the pirate objects, 0x514000, takes it away), outside
// network games operation 0xB4 (a new pirate in the first free slot, with its
// [+0x14] and two numbers of rand()), the merchants whose [+0xE1] is its nest
// get 0xFF there, its map freed, and the first event 0x32 of the Hanse task of
// type 3 against it ([+0xC] 3, [+0x10] the pirate, [+0x11] its nest) is done
// ([+0x12] 2).
//
// The game calls 0x50A3D0 also without a convoy (0xFFFF): it writes past the
// convoys (not reproduced; unreachable while the nest attack of alderman.cpp
// isn't rewritten: a nest's battle is set up with its convoy as defender).
void Sailing::fn_514920(size_t k) {
	GlobalMemory& gl = save_.globals;
	GameMemory<0x18>& p = *save_.pirates[k];
	const uint16_t ships = gl.u16(global::kShipCount);
	bool alive = false;
	const uint16_t convoy = p.u16(0x10);
	if (convoy < save_.convoy_count()) {
		// 0x494A80 (the interface's convoys, *0x6CE6D0).
		GameMemory<0x3C>& c = save_.convoys[convoy];
		while (c.u16(0xA) < ships) {
			const uint16_t s = c.u16(0xA);
			ShipMemory& m = save_.ships[s].memory;
			c.u16(0xA) = m.u16(0x6);
			m.u16(0x8) = 0xFFFF;
			if (m.s32(0x18) > 0) {
				alive = true;
				m.u16(0x6) = p.u16(0x12);
				p.u16(0x12) = s;
			} else {
				m.u16(0x6) = 0xFFFF;
				fn_507fb0(s);
			}
		}
		fn_50a3d0(convoy);
	}
	p.u16(0x10) = 0xFFFF;
	const std::vector<GameMemory<8>>& pool = save_.pirate_nests[k].pool;
	const uint16_t entries = p.u16(0x8);
	for (uint32_t i = 0; entries > 0 && i < pool.size();) {
		const uint8_t kind = pool[i].u8(0x3);
		if ((kind == 0x30 || kind == 0x31) && pool[i].u8(0x6) < 0xC8) {
			alive = true;
		}
		i = pool[i].u16(0x4);
		if (i >= entries) {
			break;
		}
	}
	if (alive) {
		return;
	}
	p.u8(0x15) = 1;
	if (gl.u32(kNetwork) == 0) {
		uint32_t slot = 0;
		while (slot < 5 && gl.u32(kPirates + 4 * slot) != 0) {
			++slot;
		}
		const uint32_t first = tick::game_rand(save_);
		const uint32_t second = tick::game_rand(save_);
		const uint32_t fields[5] = {0xB4, slot, p.u8(0x14), first, second};
		uint8_t op[0x14];
		std::memcpy(op, fields, sizeof(op));
		tick::enqueue_operation(save_, op, sizeof(op));  // 0x54AA70
	}
	for (uint32_t m = 0; m < gl.u16(global::kWorldMerchantCount); ++m) {
		MerchantMemory& merchant = save_.merchants[m].memory;
		if (merchant.u8(0xE1) == p.u8(0xE)) {
			merchant.u8(0xE1) = 0xFF;
		}
	}
	if (p.u32(0x0) != 0) {
		save_.pirate_nests[k].map.reset();  // 0x4F9650 and operator delete
		save_.pirate_nests[k].cells.clear();
		p.u32(0x0) = 0;
	}
	for (uint32_t i = gl.u16(kEventHead); i < gl.u16(kEventCount);) {
		GameMemory<0x18>& e = save_.events[i];
		i = e.u16(0x4);
		if (e.u16(0x6) == 0x32 && e.u8(0xC) == 3 && e.u8(0x10) == k && e.u8(0x11) == p.u8(0xE)) {
			e.u8(0x12) = 2;
			break;
		}
	}
}

// Operation 0xB4 (0x536E7D → 0x542E40; {k, difficulty, town, number}): pirate
// k anew. Unless k is 5 or more: a new object in slot k (0x513720) or, if
// there is one, it goes away first (0x513EC0); then its nest and ships
// (0x513D80 with the town number, the difficulty and the number).
void Sailing::fn_542e40(uint32_t k, uint32_t difficulty, uint32_t town, uint32_t number) {
	if (k >= 5) {
		return;
	}
	if (save_.globals.u32(kPirates + 4 * k) == 0 || !save_.pirates[k]) {
		fn_513720(save_, k);
	} else {
		fn_513ec0(k);
	}
	fn_513d80(save_, k, town, difficulty, number);
}

}  // namespace p3::data
