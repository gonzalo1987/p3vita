// A ship taken in a sea battle (0x60C1C9, called by the boarding 0x61F1BF)
// and the pirates' share of the loot (0x515F70); docs/cloud/battles-2.md §4.

#include <algorithm>
#include <string>
#include <vector>

#include "p3data/battle/battle_math.hpp"
#include "p3data/battle/pirate_nests.hpp"
#include "p3data/game_texts.hpp"
#include "p3data/mail.hpp"
#include "p3data/sailing.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick::alderman_detail {
// town_tick/alderman.cpp
uint8_t fn_51a470(const SaveGame& save, int ship);
void fn_516170(SaveGame& save, uint32_t pirate, uint32_t type, bool captain, uint32_t strength);
}  // namespace p3::data::tick::alderman_detail
namespace p3::data::tick::merchants_detail {
int32_t fn_52a7d0(const SaveGame& save, int ship);  // town_tick/merchants.cpp: a ship's worth.
}  // namespace p3::data::tick::merchants_detail
namespace p3::data::tick::events_rare_detail {
void fn_5098b0(SaveGame& save, uint16_t& captain);  // town_tick/events_rare.cpp: a captain freed.
}  // namespace p3::data::tick::events_rare_detail

namespace p3::data {

namespace tick::merchants_detail {
uint8_t fn_4f9430(const SaveGame& save, int merchant);  // town_tick/merchants.cpp: a merchant's title
}  // namespace tick::merchants_detail

using namespace battle_math;

namespace {

constexpr uint32_t kTick = 0x6DE4B4;  // u32
constexpr uint16_t kBuildingWorth = 200;  // 0x672A2E (u16)
constexpr int16_t kLootFame = 65;  // 0x672DE2 (s16)
// 0x672C14 (u8 by ware < 0x14): 1 for a ware counted in barrels (200), else
// in loads (2000); the others by 10.
constexpr uint8_t kBarrels[20] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0};
// 0x673A18 (float by ware): what a unit of each ware is worth to the pirates.
constexpr float kWorth[24] = {0.055000003f, 0.47855002f, 0.22005001f, 0.174f, 0.1425f, 0.55f, 1.4f, 1.1f, 1.034f,
	3.3825f, 0.41249999f, 0.027500002f, 1.278f, 1.12f, 0.44000003f, 0.278f, 0.44000003f, 0.22000001f, 0.85499996f,
	0.039900005f, 10.0f, 8.0f, 12.0f, 14.0f};
// 0x673658 (by [+0x14] of a pirate): its ships at the nest.
constexpr uint8_t kNestShips[4] = {4, 8, 16, 18};

// The game's generator 0x46C350 (seeded with the tick).
uint32_t next(uint32_t& x) {
	x = 0xA205B063u - x * 0x44BF19B3u;
	x = (x & 1) ? (x >> 1) | 0x80000000u : x >> 1;
	return x;
}

}  // namespace

// 0x515F70 (on pirate k): its share of a rich loot, when it has no convoy at
// sea: [+0x16] + 200 and a building planned (0x515DA0); by a generator
// seeded with the tick, sometimes (by its [+0x14]) a new ship at the nest
// (0x516170, a wreck: hull 1) if it has fewer than 1.5 · 0x673658 ships
// there, and sometimes a building of the nest (entries 1 to 3 of its pool,
// outside its lists, of kind 0x30 or 0x31) back in the list as 0x30 with a
// countdown of 100; or the first 0x30 of the list becomes 0x31.
void fn_515f70(SaveGame& save, size_t k) {
	GameMemory<0x18>& p = *save.pirates[k];
	if (p.u16(0x10) < save.convoy_count()) {
		return;
	}
	p.u16(0x16) = uint16_t(p.u16(0x16) + kBuildingWorth);
	fn_515da0(save, k);
	uint32_t x = save.globals.u32(kTick);
	const uint16_t ships = save.globals.u16(global::kShipCount);
	if (p.u8(0x14) >= (next(x) & 0xF)) {
		const uint8_t most = p.u8(0x14) < 4 ? kNestShips[p.u8(0x14)] : 0;
		uint8_t left = uint8_t(most + (most >> 1));
		for (uint32_t s = p.u16(0xC); s < ships && left != 0; s = save.ships[s].memory.u16(0x4)) {
			if (save.ships[s].memory.u8(0x15D) != 0) {
				--left;
			}
		}
		if (left != 0) {
			const uint32_t type = next(x) & 3;
			tick::alderman_detail::fn_516170(save, uint32_t(k), type, false, uint32_t(p.u8(0x14)) * 40 + 0x32);
			save.ships[save.pirates[k]->u16(0xC)].memory.u32(0x18) = 1;
		}
	}
	if (p.u8(0x14) < (next(x) & 0x1F)) {
		return;
	}
	std::vector<GameMemory<8>>& pool = save.pirate_nests[k].pool;
	const uint16_t size = std::min<uint16_t>(p.u16(0x8), uint16_t(pool.size()));
	bool used[4] = {false, false, false, false};
	if (size > 0 && pool[0].u16(0x4) < size) {
		uint16_t i = 0;
		for (;;) {
			if (i < 4) {
				used[i] = true;
			}
			if (pool[i].u8(0x3) == 0x30) {
				pool[i].u8(0x3) = 0x31;
				return;
			}
			const uint16_t n = pool[i].u16(0x4);
			if (n >= size || pool[n].u16(0x4) >= size) {
				break;
			}
			i = n;
		}
	}
	for (uint16_t f = p.u16(0xA); f < size; f = pool[f].u16(0x4)) {
		if (f < 4) {
			used[f] = true;
		}
	}
	for (uint16_t c = 1; c < 4 && c < size; ++c) {
		if (!used[c] && (pool[c].u8(0x3) == 0x31 || pool[c].u8(0x3) == 0x30)) {
			pool[c].u8(0x3) = 0x30;
			pool[c].u8(0x6) = 0x64;
			pool[c].u16(0x4) = pool[0].u16(0x4);
			pool[0].u16(0x4) = c;
			return;
		}
	}
}

// The «of ...» of the owner of the ship of a battle's entry (0x60C430..
// 0x60C612 for the taken ship, 0x60C708..0x60C906 for the winner): when its
// side is the pirates' team (the bit 1 << team of [+0xAD2]), «of the pirate
// %s %s» with the pirate's first and family names ([+0x1E90], [+0x1E91])
// if its side's owner is a pirate with a captain (0xFF) and the guide of its
// team's convoy ([+0x65E] or [+0x660]) has [+0x18] and a captain, or else in a
// town battle (method +0x28) at a pirate's nest ([+0x1E98] out of the towns),
// else «of the unknown pirate»; otherwise «of %s
// %s» with the first and family names of the merchant who owns the ship
// ([+0x0]: [+0x1C], [+0x1D] of his record, 0x5303C0).
std::string Sailing::owner_phrase(size_t b, size_t e) const {
	const BattleGraph& g = save_.battles;
	const size_t side = g.get<uint8_t>(e, 0x13D);
	const uint8_t team = g.get<uint8_t>(b, 0x559 + side);
	if (g.get<uint8_t>(b, 0xAD2) & uint8_t(1u << (team & 31))) {
		if (g.get<uint16_t>(b, 0x35A + 2 * side) == 0xFF) {
			// The game reads the convoy's guide without checking the convoy's
			// index (0xFFFF for a ship alone); here no guide then.
			const uint16_t convoy = g.get<uint16_t>(b, team == 0 ? 0x65E : 0x660);
			const uint16_t guide = convoy < save_.convoy_count() ? save_.convoys[convoy].u16(0x10) : 0xFFFF;
			if (guide < save_.globals.u16(global::kShipCount) && save_.ships[guide].memory.u32(0x18) != 0 &&
				save_.ships[guide].memory.u16(0x42) < save_.captain_count()) {
				return GameTexts::sprintf_raw(texts_->raw(GameText::BattleOfPirate),
					{game_first_name(save_, g.get<uint8_t>(b, 0x1E90)), game_family_name(save_, g.get<uint8_t>(b, 0x1E91))});
			}
		}
		if (g.get<uint32_t>(b, 0) == kTownBattleVtable && g.get<uint8_t>(b, 0x1E98) >= save_.globals.u32(0x6DE4B0)) {
			// Checked with the oracle: scenario nest_attack_branches (saves 3 and 5).
			return GameTexts::sprintf_raw(texts_->raw(GameText::BattleOfPirate),
				{game_first_name(save_, g.get<uint8_t>(b, 0x1E90)), game_family_name(save_, g.get<uint8_t>(b, 0x1E91))});
		}
		return texts_->raw(GameText::BattleOfUnknownPirate);
	}
	const uint8_t owner = save_.ships[g.get<uint32_t>(e, 0x4)].memory.u8(0x0);
	if (owner >= save_.merchants.size()) {
		// The game reads the record of that index without checking it.
		return GameTexts::sprintf_raw(texts_->raw(GameText::BattleOfMerchant), {});
	}
	const MerchantMemory& m = save_.merchants[owner].memory;
	return GameTexts::sprintf_raw(texts_->raw(GameText::BattleOfMerchant),
		{game_first_name(save_, m.u8(0x1C)), game_family_name(save_, m.u8(0x1D))});
}

// 0x60C3D6..0x60CA6F (in 0x60C1C9): when the local player ([0x6DFC14])
// owns the winner's side, the message «The crew of the "%s" has captured
// (attack: Boarding battle) / plundered (Seizure) %s "%s" %s.» with the
// winner's ship's name ([+0x160]), the taken ship's type as the one taken
// (0x6A171C), its name and its owner (owner_phrase); else, when the player
// owns the taken one's side, «Your ship "%s" lost the boarding fight
// against / was plundered by %s "%s" %s.» with the taken ship's name and
// the winner's type (0x6A175C), name and owner. Written into the battle's
// [+0x1310] (sprintf 0x64D410) in the language of the texts (Sailing::
// set_texts; nothing without them). Then, if the player's side is human
// ([+0x5D + side] 0), the window's description at [+0x12EC] (the text) and
// the window (0x41D9B0 on *0x6CBB10; SaveGame::battle_window).
void Sailing::boarding_message(size_t b, uint16_t winner, uint16_t taken, bool attack) {
	BattleGraph& g = save_.battles;
	const size_t w = battle_entry(b, winner), t = battle_entry(b, taken);
	const uint32_t player = save_.globals.u32(0x6DFC14);
	const uint8_t wside = g.get<uint8_t>(w, 0x13D), tside = g.get<uint8_t>(t, 0x13D);
	auto ship = [&](size_t e) -> const ShipMemory& { return save_.ships[g.get<uint32_t>(e, 0x4)].memory; };
	auto name = [&](size_t e) {
		std::string s;
		for (size_t i = 0x160; i < 0x180 && ship(e).u8(i) != 0; ++i) {
			s += char(ship(e).u8(i));
		}
		return s;
	};
	uint8_t side;
	GameText text = GameText::BattleBoardingCaptured;
	std::vector<std::string> args;
	if (g.get<uint16_t>(b, 0x35A + 2 * size_t(wside)) == player) {
		side = wside;
		if (texts_) {
			text = attack ? GameText::BattleBoardingCaptured : GameText::BattleSeizurePlundered;
			args = {name(w), texts_->raw(GameText(int(GameText::BattleTakenSnaikka) + (ship(t).u8(0xE) & 3))), name(t),
				owner_phrase(b, t)};
		}
	} else if (g.get<uint16_t>(b, 0x35A + 2 * size_t(tside)) == player) {
		side = tside;
		if (texts_) {
			text = attack ? GameText::BattleBoardingLost : GameText::BattleSeizureWasPlundered;
			args = {name(t), texts_->raw(GameText(int(GameText::BattleWinnerSnaikka) + (ship(w).u8(0xE) & 3))), name(w),
				owner_phrase(b, w)};
		}
	} else {
		return;
	}
	if (texts_) {
		const std::string message = GameTexts::sprintf_raw(texts_->raw(text), args);
		const size_t room = g.blocks[b].size() > 0x1310 ? g.blocks[b].size() - 0x1310 : 0;
		for (size_t i = 0; i <= message.size() && i < room; ++i) {
			g.set<uint8_t>(b, 0x1310 + i, i < message.size() ? uint8_t(message[i]) : 0);
		}
	}
	if (side >= 0xFF || g.get<uint8_t>(b, 0x5D + size_t(side)) != 0) {
		// The computer's side: the game shows nothing; the viewer tells it (take_player_messages). (With the player's
		// own side the window below shows it: only once.)
		if (texts_) {
			player_messages_.push_back(GameTexts::sprintf_raw(texts_->raw(text), args));
		}
		return;
	}
	// The window's description (0x60C9D6..0x60CA34: the text, no button, as
	// the end's), then the window: waiting ([+0x12E9] 1) while another one
	// is open ([0x6E59CD], the battles' [+0x3FD]), else opened (0x60CA4E:
	// 0x41D9B0, [+0x12E9] 0, [0x6E59CD] 1, [+0x12EA] 1). Without the window
	// (the checks) it isn't opened, as the oracle (BATTLE_WINDOW_JUMPS,
	// 0x60CA4E -> 0x60CA6F).
	battle_window_description(b, false);
	if (g.get<uint8_t>(0, 0x3FD) != 0) {
		g.set<uint8_t>(b, 0x12E9, 1);
	} else if (save_.battle_window.present) {
		open_battle_window(b);
		g.set<uint8_t>(b, 0x12E9, 0);
		g.set<uint8_t>(0, 0x3FD, 1);
		g.set<uint8_t>(b, 0x12EA, 1);
	}
}

// 0x60C1C9 (on the battle; the winner's place, the taken one's): the ship
// in the second place is taken by the first's. Both stop grappling and
// heading anywhere ([+0x8], [+0x128], [+0x24] its heading, [+0x20]); the
// battle's [+0xAC4] = 2, [+0xAC6] the taken place. Unless the winner attacks
// it (0x6212FB with the flag), the taken ship is plundered (0x40 | 0x20):
// the winner takes as many of its reserve ([+0x154]) as fit (10 each), then
// its wares, the most worth first (0x673A18), as fit, whole barrels or loads
// (the loser's rounded down too); both holds again (0x5182B0) and [+0x3C] |
// 0x14. The worth of the loot · 65 / 100 (at most 60 000) goes to the
// merchant of the winner's convoy's guide ([+0x15D], with a captain), with
// fame + 65; a pirate with 5 000 or more gets its share (0x515F70). The
// winner's team, when [+0xAD2] is it + 1, raises [+0x1E94] >> 10 to 10.
//
// When the winner attacks it (0x6212FB with the flag) it takes it over
// instead (operation 0x91, 0x540520). The letters (0x4D6530): a pirate's
// ship with a captain taken over (0x2A, 0x54) and the share of the loot
// (0x30). Not rewritten: a side of the player (its window, part 7).
void Sailing::fn_60c1c9(size_t b, uint16_t winner, uint16_t taken) {
	BattleGraph& g = save_.battles;
	const size_t w = battle_entry(b, winner), t = battle_entry(b, taken);
	const bool attack = fn_6212fb(w, t, true);
	{
		// 0x60C252..0x60C397: a pirate's ship with a captain taken over: the
		// news 0x2A for everybody (the winner's merchant: home town, [+0x1A],
		// title, rank at home, [+0x1C], [+0x1D]; the captain's two names) and
		// the letter 0x54 to the winner's merchant (the captain's names, the
		// captain, the ship; [+0xE] still the second name, [+0xF] 0).
		const uint32_t ws = g.get<uint32_t>(w, 0x4), ts = g.get<uint32_t>(t, 0x4);
		const uint16_t captain = save_.ships[ts].memory.u16(0x42);
		if (save_.ships[ts].memory.u8(0x0) == 0xFF && attack &&
			captain < save_.globals.u16(global::kCaptainCount)) {
			const CaptainMemory& c = save_.captains[captain];
			const uint8_t owner = save_.ships[ws].memory.u8(0x0);
			Letter letter(0x2A);
			// Past the merchants the game reads past them. Unreachable: the
			// side that takes a pirate's ship is a merchant's.
			if (owner < save_.merchants.size()) {
				const MerchantMemory& m = save_.merchants[owner].memory;
				letter.entry.u8(5) = m.u8(0x19);
				letter.entry.u8(8) = m.u8(0x1A);
				letter.entry.u8(9) = tick::merchants_detail::fn_4f9430(save_, owner);
				letter.entry.u8(0xA) = m.u8(0x39C + m.u8(0x19));
				letter.entry.u8(0xB) = m.u8(0x1C);
				letter.entry.u8(0xC) = m.u8(0x1D);
			}
			letter.entry.u8(0xD) = c.u8(2);
			letter.entry.u8(0xE) = c.u8(3);
			letter.entry.u8(0xF) = 0;
			send_letter(save_, kLetterToChronicle, letter);
			letter.entry.u8(4) = 0x54;
			letter.entry.u8(5) = 0;
			letter.entry.u16(8) = uint16_t((c.u8(2) << 8) + c.u8(3));
			letter.entry.u16(0xA) = captain;
			letter.entry.u16(0xC) = uint16_t(ts);
			send_letter(save_, owner, letter);
		}
	}
	g.set<uint8_t>(t, 0x148, 0);
	g.set<uint8_t>(w, 0x148, 0);
	g.set<uint16_t>(b, 0xAC6, taken);
	g.set<uint8_t>(b, 0xAC4, 2);
	boarding_message(b, winner, taken, attack);
	for (const size_t x : {w, t}) {
		g.set<uint8_t>(x, 0x12A, uint8_t(g.get<uint8_t>(x, 0x12A) & 0xF7));
		g.set<uint16_t>(x, 0x128, 0xFFFF);
		g.set<uint16_t>(x, 0x24, g.get<uint8_t>(x, 0x11));
		g.set<uint16_t>(x, 0x20, 0xFFFF);
	}
	if (attack) {
		// The winner takes it over: no sailors, the winner's side, not fled
		// nor taken; operation 0x91 at once (0x540520); the team's guide
		// [+0xA84] moves on if it was this ship; from one side's list to the
		// other, after the winner ([+0x658], [+0x65A] the counts; [+0x8]
		// previous, [+0xC] next); no captain; the music up to 12.
		const uint32_t taken_ship = g.get<uint32_t>(t, 0x4);
		save_.ships[taken_ship].memory.u16(0x40) = 0;
		g.set<uint8_t>(t, 0x13D, g.get<uint8_t>(w, 0x13D));
		g.set<uint8_t>(t, 0x12A, uint8_t(g.get<uint8_t>(t, 0x12A) & 0xDE));
		fn_540520(uint16_t(g.get<uint32_t>(w, 0x4)), uint16_t(taken_ship));
		const uint8_t tt = g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(t, 0x13D)));
		if (g.get<uint32_t>(b, 0xA84 + 4 * size_t(tt)) == taken_ship) {
			g.set<uint32_t>(b, 0xA84 + 4 * size_t(tt), save_.ships[taken_ship].memory.u16(0x6));
		}
		const size_t prev = size_t(g.ptr(b, 0x8)), next = size_t(g.ptr(b, 0xC));
		auto pv = [&](uint16_t k) { return g.get<uint16_t>(prev, 2 * size_t(k)); };
		auto nx = [&](uint16_t k) { return g.get<uint16_t>(next, 2 * size_t(k)); };
		const uint8_t wt = g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(w, 0x13D)));
		const size_t from_head = wt == 0 ? 0x65C : 0x10;
		g.set<uint16_t>(b, 0x658, uint16_t(g.get<uint16_t>(b, 0x658) + (wt == 0 ? 1 : -1)));
		g.set<uint16_t>(b, 0x65A, uint16_t(g.get<uint16_t>(b, 0x65A) + (wt == 0 ? -1 : 1)));
		if (pv(taken) == 0xFFFF) {
			g.set<uint16_t>(b, from_head, nx(taken));
		} else {
			g.set<uint16_t>(next, 2 * size_t(pv(taken)), nx(taken));
		}
		if (nx(taken) != 0xFFFF) {
			g.set<uint16_t>(prev, 2 * size_t(nx(taken)), pv(taken));
		}
		g.set<uint16_t>(next, 2 * size_t(taken), nx(winner));
		g.set<uint16_t>(prev, 2 * size_t(taken), winner);
		g.set<uint16_t>(next, 2 * size_t(winner), taken);
		if (nx(taken) != 0xFFFF) {
			g.set<uint16_t>(prev, 2 * size_t(nx(taken)), taken);
		}
		save_.ships[taken_ship].memory.u16(0x42) = 0xFFFF;
		if (uint32_t(wt) + 1 == g.get<uint8_t>(b, 0xAD2)) {
			// The level 0xC, or 0xE with method +0x28 (a town battle).
			const uint16_t v = g.get<uint16_t>(b, 0x1E94);
			const uint32_t level = g.get<uint32_t>(b, 0) == kTownBattleVtable ? 0xE : 0xC;
			if (level > uint32_t(v >> 10)) {
				g.set<uint16_t>(b, 0x1E94, uint16_t((level << 10) + (v & 0x3FF)));
			}
		}
		return;
	}
	SavedShip& sw = save_.ships[g.get<uint32_t>(w, 0x4)];
	SavedShip& st = save_.ships[g.get<uint32_t>(t, 0x4)];
	ShipMemory& mw = sw.memory;
	ShipMemory& mt = st.memory;
	g.set<uint8_t>(t, 0x12A, uint8_t(g.get<uint8_t>(t, 0x12A) | 0x60));
	bool changed = false;
	uint32_t room = mw.u32(0x10) - mw.u32(0x11C) - mw.u32(0x118);
	const uint16_t reserve = mt.u16(0x154);
	if (reserve > 0) {
		if (room < uint32_t(reserve) * 10) {
			const uint32_t n = room / 10;
			if (n != 0) {
				mw.u16(0x154) = uint16_t(mw.u16(0x154) + n);
				mt.u16(0x154) = uint16_t(mt.u16(0x154) - n);
				room -= 10 * n;
				changed = true;
			}
		} else {
			room -= uint32_t(reserve) * 10;
			mw.u16(0x154) = uint16_t(mw.u16(0x154) + reserve);
			mt.u16(0x154) = 0;
			changed = true;
		}
	}
	uint8_t order[24];
	uint8_t count = 0;
	for (uint8_t ware = 0; ware < 24; ++ware) {
		if (mt.u32(0x54 + 4 * size_t(ware)) == 0) {
			continue;
		}
		uint8_t i = count;
		while (i > 0 && kWorth[ware] > kWorth[order[i - 1]]) {
			order[i] = order[i - 1];
			--i;
		}
		order[i] = ware;
		++count;
	}
	uint32_t worth = 0;
	for (uint8_t i = 0; i < count; ++i) {
		const uint8_t ware = order[i];
		const size_t at = 0x54 + 4 * size_t(ware);
		const int32_t have = mt.s32(at);
		uint32_t take = std::min(uint32_t(have), room);
		const int32_t unit = ware < 0x14 ? (kBarrels[ware] ? 200 : 2000) : 10;
		take -= take % uint32_t(unit);
		mt.s32(at) = have - have % unit;
		if (take == 0) {
			continue;
		}
		changed = true;
		mw.u32(at) = mw.u32(at) + take;
		mt.u32(at) = mt.u32(at) - take;
		room -= take;
		// fild, fstp to a float, fmul by the float: the product of two floats
		// is exact in a double, as in the x87.
		const double v = double(static_cast<float>(int64_t(take))) * double(kWorth[ware]);
		worth += uint32_t(int64_t(v));
	}
	if (changed) {
		recalculate_cargo(sw);
		recalculate_cargo(st);
		mw.u16(0x3C) = uint16_t(mw.u16(0x3C) | 0x14);
		mt.u16(0x3C) = uint16_t(mt.u16(0x3C) | 0x14);
	}
	const uint16_t ships = save_.globals.u16(global::kShipCount);
	const uint16_t convoy = mw.u16(0x8);
	const uint16_t guide = convoy < save_.convoys.size() ? save_.convoys[convoy].u16(0x10) : 0xFFFF;
	if (guide < ships) {
		const ShipMemory& gs = save_.ships[guide].memory;
		const uint8_t owner = gs.u8(0x15D);
		if (owner < save_.globals.u16(global::kWorldMerchantCount) &&
			gs.u16(0x42) < save_.globals.u16(global::kCaptainCount)) {
			MerchantMemory& m = save_.merchants[owner].memory;
			worth = worth * 65u / 100u;
			if (worth > 60000) {
				worth = 60000;
			}
			m.u32(0x0) += worth;
			m.u32(0x4B8) += worth;
			int16_t fame = m.s16(0x16);
			if (kLootFame > 0) {
				fame = int32_t(fame) < 0x7FFF - kLootFame ? int16_t(fame + kLootFame) : int16_t(0x7FFF);
			} else {
				fame = int32_t(fame) + 0x8000 > -int32_t(kLootFame) ? int16_t(fame + kLootFame) : int16_t(-0x8000);
			}
			m.s16(0x16) = fame;
			if (worth != 0) {
				// 0x60D0C8..0x60D189: the letter 0x30 to that merchant: the
				// guide's captain's names (the second | 0x80), the plundered
				// ship's owner's [+0x1C], [+0x1D] (a pirate's: the two places'
				// ship & 0x3F), the share, and the guide's [+0x15E] << 2 plus
				// the plundered ship's type.
				const CaptainMemory& c = save_.captains[gs.u16(0x42)];
				Letter letter(0x30);
				letter.entry.u8(5) = m.u8(0x19);
				letter.entry.u8(8) = c.u8(2);
				letter.entry.u8(9) = uint8_t(c.u8(3) | 0x80);
				if (mt.u8(0x0) != 0xFF) {
					// Unreachable otherwise: a ship's owner is a merchant or 0xFF.
					if (mt.u8(0x0) < save_.merchants.size()) {
						const MerchantMemory& o = save_.merchants[mt.u8(0x0)].memory;
						letter.entry.u8(0xA) = o.u8(0x1C);
						letter.entry.u8(0xB) = o.u8(0x1D);
					}
				} else {
					letter.entry.u8(0xA) = uint8_t(g.get<uint8_t>(w, 0x4) & 0x3F);
					letter.entry.u8(0xB) = uint8_t(g.get<uint8_t>(t, 0x4) & 0x3F);
				}
				letter.entry.u16(0xC) = uint16_t(worth);
				letter.entry.u16(0xE) = uint16_t((gs.u16(0x15E) << 2) + (mt.u8(0xE) & 3));
				send_letter(save_, owner, letter);
			}
		}
	}
	if (worth >= 5000) {
		const uint8_t k = tick::alderman_detail::fn_51a470(save_, int(g.get<uint32_t>(w, 0x4)));
		if (k < 5 && save_.pirates[k]) {
			fn_515f70(save_, k);
		}
	}
	const uint8_t team = g.get<uint8_t>(b, 0x559 + size_t(g.get<uint8_t>(w, 0x13D)));
	if (uint32_t(team) + 1 == g.get<uint8_t>(b, 0xAD2)) {
		const uint16_t v = g.get<uint16_t>(b, 0x1E94);
		if ((v >> 10) < 0xA) {
			g.set<uint16_t>(b, 0x1E94, uint16_t((v & 0x3FF) + 0x2800));
		}
	}
}

}  // namespace p3::data

namespace p3::data {

namespace {

constexpr uint8_t kMinCrew[4] = {5, 8, 10, 12};  // 0x673660, by type
// The sailors of two ships of the same owner shared by their minimum crews
// (at least 2 in all; the second at least 1, at most its minimum).
void share_crew(ShipMemory& a, ShipMemory& t) {
	uint16_t n = uint16_t(a.u16(0x40) + t.u16(0x40));
	if (n < 2) {
		n = 2;
	}
	const uint8_t mt = kMinCrew[t.u8(0xE) & 3], ma = kMinCrew[a.u8(0xE) & 3];
	uint16_t k = uint16_t(int32_t(n) * mt / (int32_t(mt) + ma));
	if (k < 1) {
		k = 1;
	} else if (k > mt) {
		k = mt;
	}
	t.u16(0x40) = k;
	a.u16(0x40) = uint16_t(n - k);
}

}  // namespace

// Operation 0x91 (0x540520; ships w and t): w takes t. Of the same owner they
// only share their sailors. Otherwise t leaves the list by x, its event 5
// about a merchant's ship lost to a pirate ([+0x15D] then 0xFF), its pirate
// event (0x4EC864), its owner (its merchant's list and counts, [+0x1B]; or
// the pirates' lists) and its convoy (a new guide, 0x5017F0, 0x501BE0). A
// merchant w's owner gets it (list [+0xE], counts, [+0x1B], fame: +0x672DE0
// from a merchant, else 0x4F8D90), its captains freed (0x5098B0), w's
// [+0x1C], [+0x20], destination by w's convoy (state 0x14); a pirate w's
// guide's merchant gets its worth (0x52A7D0 and its wares by 0x673A18, 65 %,
// at most 60 000; fame + 65), it joins w's pirate list, the pirate's share
// (0x515F70; state 0x12). Then t has w's owner, [+0x15C], marks [+0x3C] |
// 0x1420, w's convoy, no trade route [+0x136], the sailors shared, [+0x3A]
// 0xFFFF.
//
// The letters (0x4D6530): 0x54 for each captain of a pirate's ship, 0x30
// with the share. The diet's pirate hunt (town_tick/diet.cpp): a ship of its
// fleet taken is a notice «seized» at the head of its event's list, a ship
// the fleet takes one «taken», and the hunted pirate's (the last captain's
// names those of the event's [+0xC], [+0xD]) its result [+0xE] 1. Not
// reproduced: the UI (0x494990, 0x4778A0).
void Sailing::fn_540520(uint16_t wi, uint16_t ti) {
	GlobalMemory& gl = save_.globals;
	const uint16_t ships = gl.u16(global::kShipCount);
	if (wi >= ships || ti >= ships) {
		return;
	}
	SavedShip& wsh = save_.ships[wi];
	SavedShip& tsh = save_.ships[ti];
	ShipMemory& W = wsh.memory;
	ShipMemory& T = tsh.memory;
	const uint16_t merchants = gl.u16(global::kWorldMerchantCount);
	const uint16_t captains = gl.u16(global::kCaptainCount);
	if (T.u16(0xA) < ships || T.u16(0xC) < ships) {
		unlink_x(ti);
	}
	const uint8_t towner = T.u8(0x0);
	if (W.u8(0x0) == towner) {
		share_crew(W, T);
		return;
	}
	const uint32_t tick = gl.u32(0x6DE4B4);
	if (T.u8(0x15D) < merchants) {
		if (towner >= merchants && W.u8(0x0) < merchants && W.u8(0x0) != T.u8(0x15D)) {
			GameMemory<0x18>& e = save_.events[tick::events_detail::fn_4d8cf0(save_, tick + 0x300)];
			e.u16(0x6) = 5;
			e.u8(0x8) = T.u8(0x15D);
			e.u8(0x9) = save_.merchants[T.u8(0x15D)].memory.u8(0x19);
			e.u8(0xA) = W.u8(0x0);
			e.u8(0xB) = e.u8(0x9);
			e.u32(0xC) = tick;
			e.u8(0x10) = 4;
			e.u8(0x11) = 0;
			e.u16(0x16) = T.u16(0x15E);
		}
		T.u8(0x15D) = 0xFF;
	}
	if (T.u16(0x134) == 0x12) {
		fn_4ec864(ti, W.u8(0x0));
	}
	T.u8(0x3D) = uint8_t(T.u8(0x3D) | 0x4);
	if (towner < merchants) {
		clear_ship_route(tsh);
		MerchantMemory& m = save_.merchants[towner].memory;
		uint16_t* link = &m.u16(0xE);
		while (*link != ti) {
			if (*link >= ships) {
				T.u32(0x18) = 0;
				return;
			}
			link = &save_.ships[*link].memory.u16(0x4);
		}
		*link = T.u16(0x4);
		m.u32(0x47C) = m.u32(0x47C) > 1 ? m.u32(0x47C) - 1 : 0;
		m.u16(0x478) = m.u16(0x478) > 1 ? uint16_t(m.u16(0x478) - 1) : 0;
		if (m.u8(0x1B) > 0) {
			m.u8(0x1B) = uint8_t(m.u8(0x1B) - 1);
		}
	} else {
		for (auto& p : save_.pirates) {  // 0x5066F0 → 0x515F00
			if (!p) {
				continue;
			}
			for (uint16_t* link = &p->u16(0xC); *link < ships; link = &save_.ships[*link].memory.u16(0x4)) {
				if (*link == ti) {
					*link = T.u16(0x4);
					T.u16(0x4) = 0xFFFF;
					break;
				}
			}
		}
	}
	const uint16_t convoys = save_.convoy_count();
	if (T.u16(0x8) < convoys) {
		const uint16_t ci = T.u16(0x8);
		GameMemory<0x3C>& c = save_.convoys[ci];
		if (fn_503b00(ci)) {
			// 0x540836..0x5408AA: the Hanse fleet's ship seized (0x672855), its owner and its name [+0x15E].
			tick::diet_detail::add_hunt_notice(save_, c.u16(0x2), tick::diet_detail::kHuntSeized, T.u8(0x0),
				T.u16(0x15E));
		}
		uint16_t* link = &c.u16(0xA);
		while (*link < ships) {
			if (*link == ti) {
				*link = T.u16(0x6);
			} else {
				link = &save_.ships[*link].memory.u16(0x6);
			}
		}
		if (c.u16(0x10) == ti) {
			fn_5017f0(ci);
			tick::ship_ai_detail::fn_501be0(save_, ci);
		}
	}
	if (W.u8(0x0) < merchants) {
		MerchantMemory& m = save_.merchants[W.u8(0x0)].memory;
		T.u16(0x4) = m.u16(0xE);
		m.u32(0x47C) += 1;
		m.u16(0x478) = uint16_t(m.u16(0x478) + 1);
		m.u16(0xE) = ti;
		if (T.u8(0x0) < merchants) {
			const int16_t fame = 65;  // 0x672DE0 (s16)
			const int32_t f = m.s16(0x16);
			if (fame > 0) {
				m.s16(0x16) = f < 0x7FFF - fame ? int16_t(f + fame) : int16_t(0x7FFF);
			} else {
				m.s16(0x16) = f + 0x8000 > -fame ? int16_t(f + fame) : int16_t(-0x8000);
			}
		} else {
			fn_4f8d90(W.u8(0x0), T.u16(0x42) < captains);
		}
		if (m.u8(0x1B) < 0xFA) {
			m.u8(0x1B) = uint8_t(m.u8(0x1B) + 1);
		}
		T.u16(0x134) = 0x14;
		// [esp+0x13], [esp+0x12]: the names of the last of a pirate's ship's captains (0xFF, 0xFF without one).
		uint8_t captain_first = 0xFF, captain_family = 0xFF;
		while (T.u16(0x42) < captains) {
			if (T.u8(0x0) >= merchants) {
				// 0x540A1C..0x540A8C: the letter 0x54 to the winner's merchant
				// about the pirate's captain (his names, him, the ship).
				const CaptainMemory& c = save_.captains[T.u16(0x42)];
				captain_first = c.u8(2);
				captain_family = c.u8(3);
				Letter letter(0x54);
				letter.entry.u8(5) = 0;
				letter.entry.u16(8) = uint16_t((c.u8(2) << 8) + c.u8(3));
				letter.entry.u16(0xA) = T.u16(0x42);
				letter.entry.u16(0xC) = ti;
				send_letter(save_, W.u8(0x0), letter);
			}
			tick::events_rare_detail::fn_5098b0(save_, T.u16(0x42));
		}
		T.u32(0x1C) = W.u32(0x1C);
		T.u32(0x20) = W.u32(0x20);
		clear_ship_route(tsh);
		if (W.u16(0x8) < convoys && fn_503b00(W.u16(0x8))) {
			// 0x540AD6..0x540B97: the Hanse fleet took it: a notice «taken» (0x672857), the taker's owner and the
			// ship's name; the hunted pirate's captain (the event's [+0xC], [+0xD]): the result «success» (0x67285A).
			const uint16_t event = save_.convoys[W.u16(0x8)].u16(0x2);
			tick::diet_detail::add_hunt_notice(save_, event, tick::diet_detail::kHuntTaken, W.u8(0x0), T.u16(0x15E));
			GameMemory<0x18>& e = save_.events[event];
			if (captain_family == e.u8(0xD) && captain_first == e.u8(0xC)) {
				e.u8(0xE) = 1;
			}
		}
		if (W.u16(0x8) < convoys) {
			const GameMemory<0x3C>& wc = save_.convoys[W.u16(0x8)];
			T.u8(0x38) = wc.u8(0x1F);
			if (T.u8(0x38) >= gl.u32(0x6DE4B0) && (wc.u8(0x14) & 0x10)) {
				T.u8(0x38) = m.u8(0x19);
			}
		} else {
			T.u8(0x38) = m.u8(0x19);
		}
	} else {
		// The guide of w's convoy (the game reads it without checking).
		const uint16_t guide = W.u16(0x8) < save_.convoys.size() ? save_.convoys[W.u16(0x8)].u16(0x10) : 0xFFFF;
		if (guide < save_.ships.size() && save_.ships[guide].memory.u8(0x15D) < merchants &&
			save_.ships[guide].memory.u16(0x42) < captains) {
			const ShipMemory& gs = save_.ships[guide].memory;
			MerchantMemory& m = save_.merchants[gs.u8(0x15D)].memory;
			int32_t worth = tick::merchants_detail::fn_52a7d0(save_, ti);
			for (int ware = 0; ware < 24; ++ware) {
				const float units = float(T.s32(0x54 + 4 * size_t(ware)));
				worth += int32_t(int64_t(double(units) * double(kWorth[ware])));
			}
			int32_t pay = int32_t(uint32_t(worth) * 65u) / 100;
			if (pay > 0xEA60) {
				pay = 0xEA60;
			}
			m.u32(0x0) += uint32_t(pay);
			m.u32(0x4B8) += uint32_t(pay);
			const int32_t f = m.s16(0x16);
			m.s16(0x16) = f < 0x7FFF - kLootFame ? int16_t(f + kLootFame) : int16_t(0x7FFF);
			if (pay > 0) {
				// 0x540D3E..0x540DDF: the letter 0x30 to that merchant, as in 0x60C1C9 but the
				// second name without 0x80 and, for a pirate's ship (an owner past the merchants),
				// the two ships & 0x3F. A privateer that takes a ship over. Checked with the
				// oracle: scenario battle_branches (saves 3, 5, 7), the operation 0x91
				// (retouch_mail.py privateer reaches the plunder's, 0x60D189).
				const CaptainMemory& c = save_.captains[gs.u16(0x42)];
				Letter letter(0x30);
				letter.entry.u8(5) = m.u8(0x19);
				letter.entry.u8(8) = c.u8(2);
				letter.entry.u8(9) = c.u8(3);
				if (T.u8(0x0) < merchants) {
					const MerchantMemory& o = save_.merchants[T.u8(0x0)].memory;
					letter.entry.u8(0xA) = o.u8(0x1C);
					letter.entry.u8(0xB) = o.u8(0x1D);
				} else {
					letter.entry.u8(0xA) = uint8_t(wi & 0x3F);
					letter.entry.u8(0xB) = uint8_t(ti & 0x3F);
				}
				letter.entry.u16(0xC) = uint16_t(pay);
				letter.entry.u16(0xE) = uint16_t((gs.u16(0x15E) << 2) + (T.u8(0xE) & 3));
				send_letter(save_, gs.u8(0x15D), letter);
			}
		}
		T.u16(0x4) = W.u16(0x4);
		W.u16(0x4) = ti;
		while (T.u16(0x42) < captains) {
			tick::events_rare_detail::fn_5098b0(save_, T.u16(0x42));
		}
		const uint8_t k = tick::alderman_detail::fn_51a470(save_, int(wi));
		if (k < 5 && save_.pirates[k]) {
			fn_515f70(save_, k);
		}
		T.u16(0x134) = 0x12;
	}
	T.u8(0x0) = W.u8(0x0);
	T.u8(0x15C) = W.u8(0x15C);
	T.u16(0x3C) = uint16_t(T.u16(0x3C) | 0x1420);
	if (W.u16(0x8) < convoys) {
		T.u16(0x8) = W.u16(0x8);
		GameMemory<0x3C>& wc = save_.convoys[W.u16(0x8)];
		T.u16(0x6) = wc.u16(0xA);
		wc.u16(0xA) = ti;
	}
	T.u8(0x136) = 0;
	share_crew(W, T);
	T.u16(0x3A) = 0xFFFF;
}

}  // namespace p3::data
