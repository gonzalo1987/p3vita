// The end of a sea battle (docs/cloud/battles-2.md §4): method +0x1C
// (0x60D299), the battle's destructor (method +0x4, 0x611740 → 0x603BC0) and
// the battles object's table of ships that met (0x612BD0, 0x612A20).

#include <array>
#include <cstring>

#include "p3data/game_texts.hpp"
#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data {

namespace tick::merchants_detail {
uint8_t fn_4f9430(const SaveGame& save, int merchant);  // town_tick/merchants.cpp: a merchant's title
}  // namespace tick::merchants_detail

namespace {

constexpr uint32_t kTick = 0x6DE4B4;  // u32
constexpr uint32_t kPlayer = 0x6DFC14;  // u32
constexpr uint32_t kNetwork = 0x6DF31C;  // u32

// The music level of a battle ([+0x1E94]: level << 10 | a fraction of
// 1000) after a ship's end worth `worth` (0x60D299).
void music(BattleGraph& g, size_t b, int32_t worth) {
	const uint16_t v = g.get<uint16_t>(b, 0x1E94);
	const int32_t keep = ((1000 - int32_t(v & 0x3FF)) * (1000 - worth) + 500) / 1000;
	g.set<uint16_t>(b, 0x1E94, uint16_t(((v >> 10) << 10) + (1000 - keep)));
}

}  // namespace

// 0x611309 (on a sea battle, a count; operation 0x9F): unless [+0xAD2] = 3,
// the music level [+0x1E94] goes down as at a ship's end once for each bit
// set in `ships`, with the factor 950/1000 for bit 0 and each next one the
// square of the one before (/1000, rounded). The game's loop compares the
// bit with `ships` in 16 bits, so with bit 15 set it never ends (it would
// need 32768 ships: the chase 0x50B250 queues the number of ships that
// join); after bit 15 nothing changes, so here it stops.
void fn_611309(BattleGraph& g, size_t b, uint16_t ships) {
	if (g.get<uint8_t>(b, 0xAD2) == 3) {
		return;
	}
	uint32_t factor = 950;
	for (uint32_t bit = 1; bit <= 0x8000; bit <<= 1) {
		if (ships & bit) {
			music(g, b, int32_t(1000 - factor));
		}
		if (uint16_t(bit << 1) > ships) {
			return;
		}
		factor = (factor * factor + 500) / 1000;
	}
}

// 0x612A20 (on the battles object; two ships, a value): ship j and the value
// in ship i's list of the table [+0xC30] (records of 12 bytes: the list, its
// room and its count; [+0xC34] records, grown by hundreds; each list grown
// by 10 entries of 8 bytes: u32 the ship and u8 the value).
void Sailing::fn_612a20(uint32_t i, uint32_t j, uint8_t value) {
	BattleGraph& g = save_.battles;
	const uint32_t records = g.get<uint32_t>(0, 0xC34);
	if (i >= records) {
		uint32_t more = i - records + 0x64;
		more -= more % 100;
		g.set<uint32_t>(0, 0xC34, records + more);
		const size_t table = g.realloc_at(0, 0xC30, size_t(records + more) * 12);
		for (uint32_t k = records; k < records + more; ++k) {
			g.set<uint32_t>(table, 12 * size_t(k) + 0x8, 0);
			g.set<uint32_t>(table, 12 * size_t(k) + 0x4, 0);
			g.set<uint32_t>(table, 12 * size_t(k), 0);
		}
	} else {
		const size_t table = size_t(g.ptr(0, 0xC30));
		const uint32_t count = g.get<uint32_t>(table, 12 * size_t(i) + 0x8);
		const int list = g.ptr(table, 12 * size_t(i));
		for (uint32_t k = 0; k < count && list >= 0; ++k) {
			if (g.get<uint32_t>(size_t(list), 8 * size_t(k)) == j) {
				g.set<uint8_t>(size_t(list), 8 * size_t(k) + 4, value);
				return;
			}
		}
	}
	const size_t table = size_t(g.ptr(0, 0xC30));
	const size_t at = 12 * size_t(i);
	if (g.get<uint32_t>(table, at + 0x8) == g.get<uint32_t>(table, at + 0x4)) {
		g.set<uint32_t>(table, at + 0x4, g.get<uint32_t>(table, at + 0x4) + 10);
		g.realloc_at(table, at, size_t(g.get<uint32_t>(table, at + 0x4)) * 8);
	}
	const size_t list = size_t(g.ptr(table, at));
	const uint32_t count = g.get<uint32_t>(table, at + 0x8);
	g.set<uint32_t>(list, 8 * size_t(count), j);
	g.set<uint8_t>(list, 8 * size_t(count) + 4, value);
	g.set<uint32_t>(table, at + 0x8, count + 1);
}

// 0x612BD0 (on the battles object; two convoys): each ship of one met each
// of the other (0x612A20 both ways, 0xFF).
void Sailing::fn_612bd0(uint16_t a, uint16_t b) {
	const uint16_t ships = save_.globals.u16(global::kShipCount);
	for (uint32_t i = save_.convoys[a].u16(0xA); i < ships; i = save_.ships[i].memory.u16(0x6)) {
		for (uint32_t j = save_.convoys[b].u16(0xA); j < ships; j = save_.ships[j].memory.u16(0x6)) {
			fn_612a20(i, j, 0xFF);
			fn_612a20(j, i, 0xFF);
		}
	}
}

// 0x60D299 (method +0x1C of a sea battle; whether side 1 has a ship with
// sailors): the end of the battle, once [+0x15C] is 0 or above 0xF000. A
// third party of the defender's convoy (owners of neither side) may stay
// for another battle: one of them, by the battle's generator weighted by
// hold ([+0x10]), into [+0x130C] (unless side 1 has sailors or [+0x12E8]).
// Without [+0x12E8], the owners of the two convoys' ships that are of
// neither side are added as sides (only in the arrays: [+0x558] stays; the
// game counts them in its report). Per ship (side 0's list, then side 1's;
// those with a hull): the report's counts by the side it started on
// ([+0x142]) and the music ([+0x1E94], if its team is [+0xAD2] − 1) by its
// end (sunk 100, taken over 200, else 50). A pirate side that won with a
// guide with a captain makes the news (the battles object's [+0x408] the
// tick, unless the loser is the player). Then [+0x130E] the flag, the battle
// over ([+0x5C]) and [+0x15C] 1 (automatic) or 250.
//
// The report goes as a letter 0x29 (0x4D6530) to each side of the computer
// (a human side gets the battle's window: part 7), a pirate's win makes the
// news (a letter 0x2A to the chronicle), and the letters 0x72 tell each side
// what it sank, took and plundered. The news ticker and the window are the
// interface.
void Sailing::fn_60d299(size_t b, bool sailors) {
	BattleGraph& g = save_.battles;
	auto u8 = [&](size_t o) { return g.get<uint8_t>(b, o); };
	auto u16 = [&](size_t o) { return g.get<uint16_t>(b, o); };
	{
		const uint16_t left = u16(0x15C);
		if (left != 0 && left < 0xF000) {
			return;
		}
	}
	const uint16_t ships = save_.globals.u16(global::kShipCount);
	const uint16_t convoys = save_.convoy_count();
	auto owner = [&](uint32_t s) { return uint16_t(save_.ships[s].memory.u8(0x0)); };
	uint16_t stay = 0xFFFF;
	if (!sailors && u8(0x12E8) == 0 && u16(0x660) < convoys) {
		const uint16_t first = save_.convoys[u16(0x660)].u16(0xA);
		if (first < ships) {
			int32_t total = 0;
			for (uint32_t s = first; s < ships; s = save_.ships[s].memory.u16(0x6)) {
				if (owner(s) != u16(0x35C) && owner(s) != u16(0x35A)) {
					total += save_.ships[s].memory.s32(0x10);
				}
			}
			if (total > 0) {
				int32_t r = int32_t(battle_rand(g, b) % uint32_t(total)) + 1;
				uint32_t s = first;
				for (;;) {
					stay = uint16_t(s);
					if (owner(s) != u16(0x35C) && owner(s) != u16(0x35A)) {
						r -= save_.ships[s].memory.s32(0x10);
					}
					if (r <= 0) {
						break;
					}
					s = save_.ships[s].memory.u16(0x6);
				}
			}
		}
	}
	uint32_t sides = u8(0x558);  // With the ones added below (0x60D420..0x60D5F4).
	if (u8(0x12E8) == 0) {
		for (const auto& [convoy_at, side_at, team] :
			{std::array<size_t, 3>{0x65E, 0x35A, 0}, std::array<size_t, 3>{0x660, 0x35C, 1}}) {
			if (u16(convoy_at) >= convoys || u16(side_at) == 0xFE) {
				continue;
			}
			for (uint32_t s = save_.convoys[u16(convoy_at)].u16(0xA); s < ships; s = save_.ships[s].memory.u16(0x6)) {
				bool known = false;
				for (uint32_t k = 0; k < sides; ++k) {
					if (owner(s) == u16(0x35A + 2 * size_t(k))) {
						known = true;
					}
				}
				if (!known) {
					g.set<uint16_t>(b, 0x35A + 2 * size_t(sides), owner(s));
					g.set<uint8_t>(b, 0x5D + size_t(sides), 1);
					g.set<uint8_t>(b, 0x559 + size_t(sides), uint8_t(team));
					++sides;
				}
			}
		}
	}
	// The report (0x60D5FA..0x60DB2F): a line per ship with a hull (side
	// 0's list, then side 1's), by the side it started on ([+0x142]): its
	// type, name and hull in % in the game's texts (0x6A226C..0x6A2278,
	// 0x6AA180); a ship taken over gets «captured» there and a plain line in
	// the other side's «captured ships». The counts: ships, kept (not sunk
	// nor taken) and kept that were boarded ([+0x12A] & 0x60), and sunk,
	// taken and plundered; the music by the ship's end.
	auto text = [&](GameText id) { return texts_ ? texts_->raw(id) : std::string(); };
	auto ship_line = [&](GameText id, uint32_t s) {
		const ShipMemory& m = save_.ships[s].memory;
		std::string name;
		for (size_t i = 0x160; i < 0x180 && m.u8(i) != 0; ++i) {
			name += char(m.u8(i));
		}
		const std::string type = text(GameText(int(GameText::BattleShipSnaikka) + (m.u8(0xE) & 3)));
		const int32_t hull = m.s32(0x14) != 0 ? m.s32(0x18) * 100 / m.s32(0x14) : 0;
		return GameTexts::sprintf_raw(text(id), {type, name, std::to_string(hull)});
	};
	std::string lines[2], taken_lines[2];  // [the side it started on]
	uint8_t on[2] = {0, 0}, kept[2] = {0, 0}, kept_taken[2] = {0, 0};
	uint16_t sunk[2] = {0, 0}, taken[2] = {0, 0}, plundered[2] = {0, 0};
	const size_t next = size_t(g.ptr(b, 0xC));
	for (const size_t head : {size_t(0x10), size_t(0x65C)}) {
		for (uint16_t k = u16(head); k != 0xFFFF; k = g.get<uint16_t>(next, 2 * size_t(k))) {
			const size_t e = battle_entry(b, k);
			const uint32_t s = g.get<uint32_t>(e, 0x4);
			if (scene_ship(e).u32(0x14) == 0) {
				continue;
			}
			const uint8_t fl = g.get<uint8_t>(e, 0x12A);
			const size_t first = g.get<uint8_t>(e, 0x142) == 0 ? 0 : 1;
			std::string line, taken_line;
			int32_t worth = 0x32;
			uint8_t end = 0;
			if (fl & 0x4) {
				line = ship_line(GameText::BattleReportSunk, s);
				worth = 0x64;
				end = 0x4;
			} else if (g.get<uint8_t>(e, 0x13D) != g.get<uint8_t>(e, 0x142)) {
				line = ship_line(GameText::BattleReportCaptured, s);
				taken_line = ship_line(GameText::BattleReportShip, s);
				worth = 0xC8;
				end = 0x8;
			} else {
				if (fl & 0x2) {
					line = ship_line(GameText::BattleReportFled, s);
				} else if (fl & 0x40) {
					line = ship_line(GameText::BattleReportPlundered, s);
					end = 0x40;
				} else {
					line = ship_line(GameText::BattleReportShip, s);
				}
				++kept[first];
				if (g.get<uint8_t>(e, 0x12A) & 0x60) {
					++kept_taken[first];
				}
			}
			++on[first];
			lines[first] += line;
			taken_lines[first] += taken_line;
			if (u8(0xAD2) == (first == 0 ? 2 : 1)) {
				music(g, b, worth);
			}
			if (end == 0x4) {
				++sunk[first];
			} else if (end == 0x8) {
				++taken[first];
			} else if (end == 0x40) {
				++plundered[first];
			}
		}
	}
	// The two reports (0x60DB2F..0x60DCB9), for the team 0 and the team 1:
	// the result, «Own ships:» and the team's lines, «Captured ships:» and
	// the other team's ships it took (if any), «Enemy ships:» and the other
	// team's lines. The result: if side 1 has no ship with sailors (it lost),
	// whether it escaped (twice its kept ships without the boarded ones are
	// more than its ships); else, whether side 0 fled.
	std::string report[2];
	if (!sailors) {
		const bool escaped = 2 * (int32_t(kept[1]) - int32_t(kept_taken[1])) > int32_t(on[1]);
		report[0] = text(escaped ? GameText::BattleReportEnemyEscaped : GameText::BattleReportAttackSucceeded);
		report[1] = text(escaped ? GameText::BattleReportEscaped : GameText::BattleReportLost);
	} else {
		const bool fled = 2 * (int32_t(kept[0]) - int32_t(kept_taken[0])) > int32_t(on[0]);
		report[0] = text(fled ? GameText::BattleReportAttackFled : GameText::BattleReportAttackLost);
		report[1] = text(fled ? GameText::BattleReportPutToFlight : GameText::BattleReportDefeatedEnemy);
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
	// The letters 0x29 (0x60DD01..0x60DD93), if side 0 had a ship: to each
	// side of the computer ([+0x5D]), with its team's report (without the
	// final 0; the size a u16) and its merchant's home town in [+0x5]. A
	// human side gets the battle's window instead (part 7).
	const uint16_t merchants = save_.globals.u16(global::kWorldMerchantCount);
	if (on[0] != 0) {
		for (uint32_t k = 0; k < sides; ++k) {
			const uint16_t m = u16(0x35A + 2 * size_t(k));
			if (m >= merchants || u8(0x5D + size_t(k)) == 0) {
				continue;
			}
			Letter letter(0x29);
			letter.entry.u8(5) = save_.merchants[m].memory.u8(0x19);
			const std::string& body = report[u8(0x559 + size_t(k)) == 0 ? 0 : 1];
			letter.entry.u32(0xC) = uint32_t(body.size() & 0xFFFF);
			letter.buffer.assign(body.begin(), body.end());
			send_letter(save_, m, letter);
		}
	}
	const size_t loser = sailors ? 0 : 1;  // The side whose ships are counted as lost.
	uint16_t lost = uint16_t(on[loser] - kept[loser]);
	bool news = false;
	const uint32_t tick = save_.globals.u32(kTick);
	if (g.get<uint32_t>(0, 0x408) + 0x1E00 < tick) {
		news = true;
	} else {
		const uint16_t m = u16(0x35C - 2 * size_t(sailors ? 1 : 0));
		if (m < merchants && save_.merchants[m].memory.u16(0x8) == 0) {
			news = true;
		}
	}
	if (lost == 0 && news && kept_taken[loser] != 0) {
		lost = 0xFF;
	}
	const size_t winner = sailors ? 1 : 0;
	if (u16(0x35A + 2 * winner) == 0xFF && lost > 0) {
		uint16_t guide = 0xFFFF;
		if (winner == 1) {
			guide = u16(0x660) < save_.convoys.size() ? save_.convoys[u16(0x660)].u16(0x10) : 0xFFFF;
			if (guide >= convoys) {
				guide = 0xFFFF;
			}
		} else {
			guide = u16(0x65E) < save_.convoys.size() ? save_.convoys[u16(0x65E)].u16(0x10) : 0xFFFF;
		}
		if (guide < save_.ships.size() && save_.ships[guide].memory.u32(0x18) != 0 &&
			save_.ships[guide].memory.u16(0x42) < save_.globals.u16(global::kCaptainCount)) {
			// The news for everybody (0x60EAC1..0x60EB42): a pirate beat the
			// loser's merchant: his home town, [+0x1A], title (0x4F9430), rank
			// at home, [+0x1C], [+0x1D], the battle's [+0x1E90], [+0x1E91] and
			// the ships lost. The window (0x469380, 0x469AF0) is the interface.
			const uint16_t m = u16(0x35C - 2 * winner);
			if (m < merchants) {
				const MerchantMemory& lm = save_.merchants[m].memory;
				Letter letter(0x2A);
				letter.entry.u8(5) = lm.u8(0x19);
				letter.entry.u8(8) = lm.u8(0x1A);
				letter.entry.u8(9) = tick::merchants_detail::fn_4f9430(save_, m);
				letter.entry.u8(0xA) = lm.u8(0x39C + lm.u8(0x19));
				letter.entry.u8(0xB) = lm.u8(0x1C);
				letter.entry.u8(0xC) = lm.u8(0x1D);
				letter.entry.u8(0xD) = u8(0x1E90);
				letter.entry.u8(0xE) = u8(0x1E91);
				letter.entry.u8(0xF) = uint8_t(lost);
				send_letter(save_, kLetterToChronicle, letter);
			}  // else: sin comprobar con el oráculo (the game reads past the merchants).
			if (save_.globals.u32(kPlayer) != m) {
				g.set<uint32_t>(0, 0x408, tick);
			}
		}
	}
	// The letters 0x72 (0x60EBB6..0x60EC83): to each side's merchant (not a
	// pirate), how many of the other side's ships were sunk, taken and
	// plundered, if any, with the other side's merchant.
	for (size_t side = 0; side < 2; ++side) {
		const uint16_t m = u16(0x35A + 2 * side), other = u16(0x35A + 2 * (1 - side));
		const size_t o = 1 - side;
		if (m == 0xFF || (sunk[o] == 0 && taken[o] == 0 && plundered[o] == 0)) {
			continue;
		}
		// Side 0 (the attacker, 0x60EC1B) is a merchant only when a merchant
		// attacks (the Hanse fleet, the player's battle: part 7): sin
		// comprobar con el oráculo; side 1 (0x60EC83), checked.
		Letter letter(0x72);
		letter.entry.u8(5) = 0;
		letter.entry.u16(8) = other;
		letter.entry.u16(0xA) = sunk[o];
		letter.entry.u16(0xC) = taken[o];
		letter.entry.u16(0xE) = plundered[o];
		send_letter(save_, m, letter);
	}
	g.set<uint8_t>(b, 0x130E, sailors ? 1 : 0);
	g.set<uint16_t>(b, 0x130C, stay);
	g.set<uint8_t>(b, 0x5C, 1);
	g.set<uint16_t>(b, 0x15C, u8(0xAC8) != 0 ? 1 : 0xFA);
}

// 0x611740 (method +0x4 of a sea battle; free): its destructor 0x603BC0.
// Its slot in the battles object cleared (and the count [+0x404] down to
// the last used); the [+0x3FD] of a battle on screen; back to the normal
// time with a human side (0x612D20). When it ended ([+0x5C], [+0x15C] 0): a
// third party that stays ([+0x130C]) starts a new battle (0x611CD0,
// 0x605420) against side 0's first ship, with the music; else the two
// convoys' ships met (0x612BD0), maybe a convoy's trophy by the music
// (0x50CB80), a Hanse convoy in state 9 sent home (0x50C9E0), and both
// convoys back to their ways (0x50BFD0). Its blocks freed (they are no
// longer reachable from the battles object).
void Sailing::fn_611740(size_t b, bool free) {
	(void)free;
	BattleGraph& g = save_.battles;
	auto u8 = [&](size_t o) { return g.get<uint8_t>(b, o); };
	auto u16 = [&](size_t o) { return g.get<uint16_t>(b, o); };
	const uint16_t slot = u16(0x662);
	if (g.get<uint32_t>(0, 4 * size_t(slot)) == 0) {
		return;
	}
	g.set<uint32_t>(0, 4 * size_t(slot), 0);
	uint8_t count = g.get<uint8_t>(0, 0x404);
	while (count != 0 && g.get<uint32_t>(0, 4 * size_t(count - 1)) == 0) {
		--count;
	}
	g.set<uint8_t>(0, 0x404, count);
	if (u8(0x12EA) != 0) {
		g.set<uint8_t>(0, 0x3FD, 0);
	}
	if (save_.globals.u32(kNetwork) == 0) {
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
	if (u8(0x5C) != 0 && u16(0x15C) == 0) {
		const uint16_t ships = save_.globals.u16(global::kShipCount);
		if (u16(0x130C) < ships) {
			const uint8_t s = fn_611cd0();
			const int nb = g.battle(s);
			const uint32_t first = g.get<uint32_t>(battle_entry(b, u16(0x10)), 0x4);
			const bool flag = save_.ships[u16(0x130C)].memory.u8(0x0) == u16(0x35C);
			if (nb >= 0) {
				fn_605420(size_t(nb), uint16_t(first), u16(0x130C), 1, flag ? 1 : 0);
				g.set<uint16_t>(size_t(nb), 0x1E94, u16(0x1E94));
			}
		} else {
			if (u16(0x65E) != 0xFFFF && u16(0x660) != 0xFFFF) {
				fn_612bd0(u16(0x65E), u16(0x660));
				const uint8_t side = u8(0xAD2);
				if (side == 1 || side == 2) {
					if (battle_rand(g, b) % 1000u < uint32_t(u16(0x1E94) & 0x3FF)) {
						const uint32_t r = battle_rand(g, b);
						if (side == 1) {
							fn_50cb80(u16(0x65E), u16(0x35C), r, uint8_t(u16(0x1E94) >> 10));
						} else {
							fn_50cb80(u16(0x660), u16(0x35A), r, uint8_t(u16(0x1E94) >> 10));
						}
					}
				}
			}
			if (u16(0x660) != 0xFFFF && save_.convoys[u16(0x660)].u16(0x12) == 9) {
				fn_50c9e0(u16(0x660), u8(0x130E) == 0);
				g.set<uint16_t>(b, 0x660, 0xFFFF);
			}
			const uint16_t a = u16(0x65E), d = u16(0x660);
			if (a == 0xFFFF) {
				if (d != 0xFFFF) {
					fn_50bfd0(d, 0, 0xFF);
				}
			} else if (d == 0xFFFF) {
				fn_50bfd0(a, 0, 0xFF);
			} else {
				fn_50bfd0(a, 0, uint8_t(u16(0x35C)));
				fn_50bfd0(d, 0, uint8_t(u16(0x35A)));
			}
		}
	}
	const size_t next = size_t(g.ptr(b, 0xC));
	for (const auto& [count_at, head_at] : {std::array<size_t, 2>{0x658, 0x10}, std::array<size_t, 2>{0x65A, 0x65C}}) {
		if (u16(count_at) == 0) {
			continue;
		}
		for (uint16_t k = u16(head_at); k != 0xFFFF; k = g.get<uint16_t>(next, 2 * size_t(k))) {
			const int e = g.ptr(size_t(g.ptr(b, 0x4)), 4 * size_t(k));
			if (e >= 0 && g.get<uint8_t>(size_t(e), 0x160) != 0) {
				g.set<uint8_t>(0, 0x3FD, 0);  // 0x61A6D0: the ship's window was open.
			}
		}
	}
}

}  // namespace p3::data

namespace p3::data {

// 0x50CB80 (on the ships object; a convoy, the other side's owner, a value
// of the battle's generator, the music level): an event 5 (0x4D8CF0, at the
// tick + the convoy + 0x500) about the convoy's merchant: u8 [+0x8] it,
// [+0x9] and [+0xA] (its [+0x19] and the other owner; above level 12 the
// other owner and 0xFF), [+0xB] its [+0x19], [+0xC] the tick, [+0x10] the
// level, [+0x12] the value, [+0x16] [+0x15E] of the guide (or the first
// ship). Nothing if the convoy's owner isn't a merchant.
void Sailing::fn_50cb80(uint16_t convoy, uint16_t other, uint32_t value, uint8_t level) {
	const GameMemory<0x3C>& c = save_.convoys[convoy];
	const uint8_t owner = c.u8(0x0);
	if (owner >= save_.globals.u16(global::kWorldMerchantCount)) {
		return;
	}
	const uint32_t tick = save_.globals.u32(kTick);
	GameMemory<0x18>& e = save_.events[tick::events_detail::fn_4d8cf0(save_, tick + convoy + 0x500)];
	e.u16(0x6) = 5;
	e.u8(0x8) = owner;
	if (level < 0xD) {
		e.u8(0x9) = save_.merchants[owner].memory.u8(0x19);
		e.u8(0xA) = uint8_t(other);
	} else {
		e.u8(0xA) = 0xFF;
		e.u8(0x9) = uint8_t(other);
	}
	e.u8(0xB) = save_.merchants[e.u8(0x8)].memory.u8(0x19);
	e.u8(0x10) = level;
	e.u32(0xC) = tick;
	e.u8(0x11) = 0;
	e.u16(0x12) = uint16_t(value);
	e.u16(0x14) = 0;
	const uint16_t ships = save_.globals.u16(global::kShipCount);
	if (c.u16(0x10) < ships) {
		e.u16(0x16) = save_.ships[c.u16(0x10)].memory.u16(0x15E);
	} else if (c.u16(0xA) < ships) {
		e.u16(0x16) = save_.ships[c.u16(0xA)].memory.u16(0x15E);
	} else {
		e.u16(0x16) = 0;
	}
}

// 0x50C9E0 (on the ships object; a convoy, a flag): a Hanse convoy (state 9)
// after its battle. Its event (if 0x24; with the flag it ends: [+0x15] 5,
// [+0x16] 0xFFFF and its town's [+0x2C8] without 0x600) or none; its sunk
// ships (hull ≤ 0) leave it for the list [+0xEA] (state 0x11), the others
// for [+0xE8] (state 3) without an event. The convoy, empty, is freed (0xFF,
// to [+0xE4]), else waits in state 9 ([+0xEC]).
//
// Not rewritten (not run in the saves): the ships of a Hanse convoy whose
// event stays (0x6132C0 on 0x6E6220, then state 9 and [+0x138] 0x20).
void Sailing::fn_50c9e0(uint16_t convoy, bool flag) {
	GlobalMemory& gl = save_.globals;
	GameMemory<0x3C>& c = save_.convoys[convoy];
	bool hanse = false;
	if (c.u16(0x2) < gl.u16(0x6DD748)) {
		GameMemory<0x18>& e = save_.events[c.u16(0x2)];
		if (e.u16(0x6) == 0x24) {
			hanse = !flag;
			if (flag) {
				e.u8(0x15) = 5;
				e.u16(0x16) = 0xFFFF;
				if (e.u8(0x14) < gl.u32(0x6DE4B0)) {
					TownMemory& t = save_.towns[e.u8(0x14)].memory;
					t.u32(0x2C8) = t.u32(0x2C8) & ~0x600u;
				}
			}
		}
	}
	const uint16_t ships = gl.u16(global::kShipCount);
	uint16_t* link = &c.u16(0xA);
	while (*link < ships) {
		ShipMemory& s = save_.ships[*link].memory;
		const uint16_t index = *link;
		if (s.s32(0x18) <= 0) {
			s.u16(0x8) = 0xFFFF;
			*link = s.u16(0x6);
			s.u16(0x134) = 0x11;
			s.u16(0x6) = gl.u16(global::kShips + 0xEA);
			gl.u16(global::kShips + 0xEA) = index;
		} else if (!hanse) {
			s.u16(0x8) = 0xFFFF;
			*link = s.u16(0x6);
			s.u16(0x134) = 3;
			s.u16(0x6) = gl.u16(global::kShips + 0xE8);
			gl.u16(global::kShips + 0xE8) = index;
			s.u16(0x138) = 0;
		} else {
			// Not rewritten: 0x6132C0.
			s.u16(0x134) = 9;
			s.u16(0x138) = 0x20;
			link = &s.u16(0x6);
		}
	}
	if (c.u16(0xA) >= ships) {
		c.bytes.fill(0xFF);
		c.u16(0x8) = gl.u16(global::kShips + 0xE4);
		gl.u16(global::kShips + 0xE4) = convoy;
	} else {
		c.u16(0x12) = 9;
		c.u16(0x8) = gl.u16(global::kShips + 0xEC);
		gl.u16(global::kShips + 0xEC) = convoy;
	}
}

}  // namespace p3::data

namespace p3::data {

namespace {

constexpr uint32_t kEventHead = 0x6DD744;  // u16
constexpr uint32_t kEventCount = 0x6DD748;  // u16
constexpr int16_t kSunkFame = -512;  // 0x672DDE (s16)

}  // namespace

// 0x4EC864 (on the events object; a ship, the other side's owner): the
// pirate ship's event (0x32 with [+0xC] 2 and the ship at [+0x10]) after
// the ship sank: [+0xC] 5 if its owner [+0xD] is no merchant; else [+0x16]
// 1 if that owner sank it and its captain is the event's ([+0x12], [+0x14]:
// its [+0x2], [+0x3]), otherwise 2.
void Sailing::fn_4ec864(uint16_t ship, uint8_t other) {
	GlobalMemory& gl = save_.globals;
	for (uint32_t i = gl.u16(kEventHead); i < gl.u16(kEventCount); i = save_.events[i].u16(0x4)) {
		GameMemory<0x18>& e = save_.events[i];
		if (e.u16(0x6) != 0x32 || e.u8(0xC) != 2 || e.u16(0x10) != ship) {
			continue;
		}
		const uint8_t owner = e.u8(0xD);
		if (owner >= gl.u16(global::kWorldMerchantCount)) {
			e.u8(0xC) = 5;
			return;
		}
		uint8_t v = 2;
		const uint16_t c = save_.ships[ship].memory.u16(0x42);
		if (owner == other && c < gl.u16(global::kCaptainCount) && save_.captains[c].u8(0x2) == e.u16(0x12) &&
			save_.captains[c].u8(0x3) == e.u16(0x14)) {
			v = 1;
		}
		e.u8(0x16) = v;
		return;
	}
}

// 0x4F8D90 (on a merchant; a flag): fame − 512 (0x672DDE; at least
// −0x8000); with the flag each town's [+0x120] (float, 12 bytes apart) of the
// merchant + its [+0x464].
void Sailing::fn_4f8d90(uint8_t merchant, bool towns) {
	MerchantMemory& m = save_.merchants[merchant].memory;
	const int32_t fame = m.s16(0x16);
	if (kSunkFame > 0) {
		m.s16(0x16) = fame < 0x7FFF - kSunkFame ? int16_t(fame + kSunkFame) : int16_t(0x7FFF);
	} else {
		m.s16(0x16) = fame + 0x8000 > -kSunkFame ? int16_t(fame + kSunkFame) : int16_t(-0x8000);
	}
	if (towns) {
		for (uint32_t t = 0; t < save_.globals.u32(0x6DE4B0); ++t) {
			float a, b;
			std::memcpy(&a, m.bytes.data() + 0x120 + 12 * size_t(t), 4);
			std::memcpy(&b, m.bytes.data() + 0x464, 4);
			const float sum = float(double(a) + double(b));
			std::memcpy(m.bytes.data() + 0x120 + 12 * size_t(t), &sum, 4);
		}
	}
}

// The letters of 0x50BFD0 when a merchant's convoy sinks or keeps a pirate
// ship with a captain: 0x54 to the merchant (in [+0x5] the world's [+0x8E];
// u16 [+0x8] the captain's names, [+0x2] << 8 | [+0x3]; [+0xA] the captain;
// [+0xC] the ship) and, when it sank, the news 0x2A for everybody: the
// merchant's home town, [+0x1A], title (0x4F9430), rank at home, [+0x1C],
// [+0x1D], the captain's two names and 0.
void Sailing::pirate_letters(uint16_t merchant, uint16_t ship, uint16_t captain, bool news) {
	const CaptainMemory& c = save_.captains[captain];
	Letter letter(0x54);
	letter.entry.u8(5) = save_.globals.u8(0x6DE52E);
	letter.entry.u16(8) = uint16_t((c.u8(2) << 8) + c.u8(3));
	letter.entry.u16(0xA) = captain;
	letter.entry.u16(0xC) = ship;
	send_letter(save_, merchant, letter);
	if (!news) {
		return;
	}
	const MerchantMemory& m = save_.merchants[merchant].memory;
	letter.entry.u8(4) = 0x2A;
	letter.entry.u8(5) = m.u8(0x19);
	letter.entry.u8(8) = m.u8(0x1A);
	letter.entry.u8(9) = tick::merchants_detail::fn_4f9430(save_, merchant);
	letter.entry.u8(0xA) = m.u8(0x39C + m.u8(0x19));
	letter.entry.u8(0xB) = m.u8(0x1C);
	letter.entry.u8(0xC) = m.u8(0x1D);
	letter.entry.u8(0xD) = c.u8(2);
	letter.entry.u8(0xE) = c.u8(3);
	letter.entry.u8(0xF) = 0;
	send_letter(save_, kLetterToChronicle, letter);
}

// 0x503B00 (on a convoy; its index): whether it is the Hanse fleet of its
// event (0x34 from phase 7, [+0x16] the convoy).
bool Sailing::fn_503b00(uint16_t convoy) const {
	const uint16_t e = save_.convoys[convoy].u16(0x2);
	if (e >= save_.globals.u16(kEventCount)) {
		return false;
	}
	const GameMemory<0x18>& v = save_.events[e];
	return v.u16(0x6) == 0x34 && v.u8(0x15) >= 7 && v.u16(0x16) == convoy;
}

// 0x504970 (on the ships object): each town's wares on the way ([0x670])
// again: the cargo of the ships at sea ([0xEA]) by their destination [+0x38],
// and of the convoys in state 0xF ([0xEC]) by theirs [+0x1F].
void Sailing::fn_504970() {
	GlobalMemory& gl = save_.globals;
	const uint32_t towns = gl.u32(0x6DE4B0);
	for (uint32_t t = 0; t < towns; ++t) {
		for (int w = 0; w < 24; ++w) {
			save_.towns[t].in_transit(w) = 0;
		}
	}
	const uint16_t ships = gl.u16(global::kShipCount);
	auto add = [&](uint32_t town, const ShipMemory& s) {
		for (int w = 0; w < 24; ++w) {
			save_.towns[town].in_transit(w) += s.s32(0x54 + 4 * size_t(w));
		}
	};
	for (uint32_t i = gl.u16(global::kShips + 0xEA); i < ships; i = save_.ships[i].memory.u16(0x6)) {
		const ShipMemory& s = save_.ships[i].memory;
		if (s.u8(0x38) < towns) {
			add(s.u8(0x38), s);
		}
	}
	for (uint32_t c = gl.u16(global::kShips + 0xEC); c < save_.convoy_count(); c = save_.convoys[c].u16(0x8)) {
		const GameMemory<0x3C>& v = save_.convoys[c];
		if (v.u16(0x12) != 0xF || v.u8(0x1F) >= towns) {
			continue;
		}
		for (uint32_t i = v.u16(0xA); i < ships; i = save_.ships[i].memory.u16(0x6)) {
			add(v.u8(0x1F), save_.ships[i].memory);
		}
	}
}

// 0x50BFD0 (on the ships object; a convoy, a flag, the other side's owner):
// the convoy after its battle (no longer fighting: [+0x14] without 0x20).
// A convoy of no merchant goes on in state 0x12 ([+0x16] 0xA00) unless it
// lost its guide for good (0x5017F0); a merchant's convoy is broken up (the
// flag, a convoy without a guide with a captain, 0x10, or none) or, as the
// Hanse fleet (0x503B00, state 0x16, its event 0x34 [+0x15] 0xA if broken
// up or beaten by pirates), or as any other (state 0xF), goes on.
// - Going on, [+0x1C] 0xFF: of no merchant, its ships without the battle's
//   marks ([+0x136] & 0xFC, [+0x3C], [+0x39]) lose their trade route
//   ([+0x132]); the sunk ones (0x4EC864) leave it and are deleted (0x507FB0;
//   the winner's fame down, 0x4F8D90); the others lose their route (but the
//   guide) and get state 0x12. A merchant's sunk ships leave it (state
//   0x11; deleted unless the human player's); the others take its state and
//   [+0xC]. Then 0x5017F0 (a new guide), at sea ([0xEC], [+0x18] −1, the
//   guide in the list by x after [0xEA]), or deleted when empty (0x50A3D0).
// - Broken up: each ship alone at sea ([0xEA], in the list by x), with the
//   convoy's state; its owner gets the convoy's money [+0x20] if it is his;
//   without a captain [+0x13D] | 0x10; a merchant's ship heads for its
//   destination [+0x38] (or its owner's home [+0x19] for an AI merchant's
//   without one), the rest lose their trade route; the sunk ones as above.
//   The convoy is deleted (0x50A3D0).
// The ships lost lower the loser's [+0x1B] (and raise the winner's, at most
// 250; fame down if the loser is no merchant); the towns' wares on the way
// again (0x504970) if the winner is a merchant.
//
// Not reproduced: the trade routes [+0x132] (0x4D4DD0 on 0x6DD728, which
// SaveGame does not have), the Hanse event's list of sunk ships (0x4F3980,
// 0x4F3AD0; p3vita doesn't keep it), the letters (0x4D6530) and the UI.
void Sailing::fn_50bfd0(uint16_t convoy, uint8_t flag, uint8_t other) {
	GlobalMemory& gl = save_.globals;
	GameMemory<0x3C>& c = save_.convoys[convoy];
	const uint16_t ships = gl.u16(global::kShipCount);
	const uint16_t merchants = gl.u16(global::kWorldMerchantCount);
	const uint16_t captains = gl.u16(global::kCaptainCount);
	const uint32_t towns = gl.u32(0x6DE4B0);
	c.u8(0x14) = uint8_t(c.u8(0x14) & 0xDF);
	uint32_t lost = 0;
	bool no_guide = true;
	const uint8_t owner = c.u8(0x0);
	bool broken = flag != 0;
	bool going_on = false;
	if (owner >= merchants) {
		broken = !fn_5017f0(convoy);
		c.u16(0x12) = 0x12;
		c.u16(0x16) = 0xA00;
	} else {
		if ((c.u8(0x14) & 0x10) == 0x10 || !fn_5017f0(convoy) || (c.u8(0x14) & 0x10) == 0x10) {
			broken = true;
		}
		if (fn_503b00(convoy)) {
			GameMemory<0x18>& e = save_.events[c.u16(0x2)];
			c.u16(0x12) = 0x16;
			if (broken) {
				e.u8(0x15) = 0xA;
				e.u8(0xE) = 3;  // 0x67285C
			} else if (other >= merchants) {
				if (e.u8(0xE) == 0) {
					e.u8(0xE) = 2;  // 0x67285B
				}
				e.u8(0x15) = 0xA;
			}
			going_on = true;  // The game goes on with the convoy even when broken up.
		} else {
			c.u16(0x12) = 0xF;
		}
	}
	auto drop_trade_route = [&](ShipMemory& s) {
		s.u8(0x136) = uint8_t(s.u8(0x136) & 0xFC);
		// 0x4D4DD0 on 0x6DD728 when [+0x132] is one: not reproduced.
		if (s.u16(0x132) < gl.u16(0x6DD72A)) {
			s.u16(0x132) = 0xFFFF;
		}
	};
	if (going_on || !broken) {
		c.u16(0x1C) = 0xFF;
		if (owner >= merchants) {
			uint16_t* link = &c.u16(0xA);
			while (*link < ships) {
				const uint16_t index = *link;
				SavedShip& ship = save_.ships[index];
				ShipMemory& s = ship.memory;
				s.u8(0x136) = uint8_t(s.u8(0x136) & 0xFC);
				s.u16(0x3C) = 0xFFFF;
				s.u8(0x39) = 0xFF;
				if (s.u16(0x132) < gl.u16(0x6DD72A)) {
					s.u16(0x132) = 0xFFFF;  // 0x4D4DD0: not reproduced.
				}
				if (s.s32(0x18) < 1) {
					fn_4ec864(index, other);
					*link = s.u16(0x6);
					if (index == c.u16(0x10)) {
						no_guide = true;
					}
					clear_ship_route(ship);
					fn_507fb0(index);
					if (other < merchants) {
						fn_4f8d90(other, s.u16(0x42) < captains);
						if (s.u16(0x42) < captains) {
							// 0x50C211..0x50C326: the letter 0x54 to the winner
							// and the news 0x2A for everybody (pirate_letters).
							// Sin comprobar con el oráculo (no save nor scenario
							// reaches it; 0x50C853 checks the 0x54).
							pirate_letters(other, index, s.u16(0x42), true);
						}
					}
					++lost;
				} else {
					if (index != c.u16(0x10)) {
						clear_ship_route(ship);
					} else {
						no_guide = false;
					}
					s.u16(0x134) = 0x12;
					recalculate_cargo(ship);
					link = &s.u16(0x6);
				}
			}
			c.u8(0x14) = uint8_t(c.u8(0x14) | 0x2);
			if (c.u16(0xA) >= ships) {
				fn_50a3d0(convoy);
				return;
			}
			if (!no_guide) {
				fn_5017f0(convoy);  // The game asks again only when its guide is still there.
			}
		} else {
			uint16_t* link = &c.u16(0xA);
			while (*link < ships) {
				const uint16_t index = *link;
				SavedShip& ship = save_.ships[index];
				ShipMemory& s = ship.memory;
				s.u8(0x39) = 0xFF;
				s.u16(0x3C) = 0xFFFF;
				if (s.s32(0x18) < 1) {
					clear_ship_route(ship);
					// The Hanse event's list of sunk ships (0x4F3980): not kept in p3vita.
					*link = s.u16(0x6);
					s.u16(0x8) = 0xFFFF;
					s.u16(0x50) = 0xFFFF;
					s.u16(0x52) = 0xFFFF;
					s.u16(0x134) = 0x11;
					if (s.u8(0x0) >= merchants || save_.merchants[s.u8(0x0)].memory.u16(0x8) != 0) {
						fn_507fb0(index);
					}
				} else {
					if (index != c.u16(0x10)) {
						clear_ship_route(ship);
					} else {
						no_guide = false;
					}
					s.u16(0x134) = c.u16(0x12);
					recalculate_cargo(ship);
					s.u32(0x30) = c.u32(0xC);
					link = &s.u16(0x6);
				}
			}
			if (no_guide) {
				c.u16(0x10) = 0xFFFF;
			}
			fn_5017f0(convoy);
		}
		c.u16(0x8) = gl.u16(global::kShips + 0xEC);
		c.u32(0x18) = 0xFFFFFFFF;
		gl.u16(global::kShips + 0xEC) = convoy;
		if (c.u16(0x10) < ships) {
			insert_x(c.u16(0x10), gl.u16(global::kShips + 0xEA));
			// The player's: 0x44F940 (UI).
		}
	} else {
		// Broken up (0x494A80 on *0x6CE6D0 first: UI).
		while (c.u16(0xA) < ships) {
			const uint16_t index = c.u16(0xA);
			SavedShip& ship = save_.ships[index];
			ShipMemory& s = ship.memory;
			c.u16(0xA) = s.u16(0x6);
			s.u16(0x3C) = 0xFFFF;
			s.u16(0x8) = 0xFFFF;
			if (s.s32(0x18) > 0) {
				if (c.s32(0x20) > 0) {
					if (c.u8(0x0) >= merchants) {
						c.u32(0x20) = 0;
					} else if (s.u8(0x0) == c.u8(0x0)) {
						save_.merchants[c.u8(0x0)].memory.u32(0x0) += c.u32(0x20);
						c.u32(0x20) = 0;
					}
				}
				s.u16(0x134) = c.u16(0x12);
				insert_x(index, gl.u16(global::kShips + 0xEA));
				recalculate_cargo(ship);
				s.u8(0x39) = 0xFF;
				s.u16(0x6) = gl.u16(global::kShips + 0xEA);
				gl.u16(global::kShips + 0xEA) = index;
				if (s.u16(0x42) >= captains) {
					s.u8(0x3D) = uint8_t(s.u8(0x3D) | 0x10);
					s.u8(0x15C) = 0;
				}
				if (s.u8(0x0) < merchants) {
					const MerchantMemory& m = save_.merchants[s.u8(0x0)].memory;
					if (m.u16(0x8) != 0 && s.u8(0x38) >= towns) {
						s.u8(0x38) = m.u8(0x19);
						s.u8(0x13A) = m.u8(0x19);
						clear_ship_route(ship);
					}
					if (ship.route == nullptr) {
						if (s.u8(0x38) < towns) {
							set_destination(ship, s.u8(0x38), 0xFFFF, 0xFFFF);
						} else {
							s.u32(0x30) = 0;
						}
					} else {
						s.u32(0x30) = (speed(ship) << 6) / 115 + 4;
					}
				} else {
					drop_trade_route(s);
				}
			} else {
				if (other < merchants && s.u8(0x0) >= merchants) {
					fn_4f8d90(other, s.u16(0x42) < captains);
					if (s.u16(0x42) < captains) {
						pirate_letters(other, index, s.u16(0x42), false);  // 0x50C7EC..0x50C853
					}
				}
				fn_4ec864(index, other);
				s.u16(0x6) = 0xFFFF;
				if (s.u8(0x0) >= merchants || save_.merchants[s.u8(0x0)].memory.u16(0x8) != 0) {
					fn_507fb0(index);
				} else {
					s.u16(0x134) = 0x11;
				}
				++lost;
			}
		}
		fn_50a3d0(convoy);
	}
	if (lost > 0) {
		if (owner < merchants) {
			MerchantMemory& m = save_.merchants[owner].memory;
			m.u8(0x1B) = m.u8(0x1B) > lost ? uint8_t(m.u8(0x1B) - lost) : 0;
		}
		if (other < merchants) {
			MerchantMemory& m = save_.merchants[other].memory;
			m.u8(0x1B) = 0xFAu - m.u8(0x1B) > lost ? uint8_t(m.u8(0x1B) + lost) : 0xFA;
			if (owner >= merchants) {
				fn_4f8d90(other, false);
			}
		}
	}
	if (other < merchants) {
		fn_504970();
	}
}

}  // namespace p3::data
