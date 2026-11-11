// The new game window's choices (p3data/new_game.hpp), without game data: the
// settings and the player the window gives (0x499291..0x499505, 0x499600..),
// the level from the details, the Hanse towns round the map and P2.CFG's
// limits, as read in the game's code (re/emu/sesion24/new_game_notes.md).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "p3data/new_game.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

// missav/1.pat's settings (the test saves'), as the game loads them: the byte
// 8 (0x0F in the file) limited to 14 (0x549F40).
SettingsMemory first_save_settings() {
	static const char kSettings[] =
		"14050000050000000e00010c01030000000000000000000000010d0514050901012f50500301014cfc6265636b"
		"00000000000000000000000000008000000000000000000000000000000000";
	SettingsMemory s;
	for (size_t i = 0; i < SettingsMemory::kSize; ++i) {
		s.u8(i) = uint8_t(std::strtoul(std::string(kSettings + 2 * i, 2).c_str(), nullptr, 16));
	}
	std::memset(s.bytes.data() + 0x3C, 0, 0x10);
	return s;
}

// A map like Standard24 where it matters here: 24 towns, the index 9 the
// number 14 (Lübeck, in Latin-1), the Hanse towns at 2, 9, 12 and 20.
GameMap test_map() {
	GameMap map;
	map.count = 24;
	for (uint32_t i = 0; i < 40; ++i) {
		map.numbers[i] = uint8_t((i + 5) % 40);
		map.names[i] = "Town" + std::to_string(i);
	}
	map.names[14] = "L\xfc" "beck";
	for (uint32_t i : {2u, 9u, 12u, 20u}) {
		map.flags[i] = 0x40;
	}
	return map;
}

// 1.pat's settings come out of the window with its choices: 1300, the speed 5,
// no goal, the first level with every detail 1, the map's productions, the
// home town 9; what the window doesn't write ([0x14], [0x15], [0x18], the
// date, the PC's options) stays as it was.
void first_save() {
	NewGameChoices c;
	c.year = 1300;
	c.speed = 5;
	c.goal = 0;
	c.level = 0;
	c.details = {1, 1, 1, 1, 1, 1, 1, 1};
	c.production = 1;
	c.home = 9;
	const SettingsMemory expected = first_save_settings();
	SettingsMemory before = expected;
	// What the window writes, spoiled first.
	for (size_t at : {0x0, 0x4, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x10, 0x11, 0x12, 0x13, 0x16, 0x17, 0x19, 0x1E,
			 0x25, 0x27, 0x28}) {
		before.u8(at) ^= 0x5A;
	}
	const SettingsMemory s = new_game_settings(c, test_map(), before, true);
	check(std::memcmp(s.bytes.data(), expected.bytes.data(), SettingsMemory::kSize) == 0, "1.pat's settings");
	const NewGamePlayer p = new_game_player(c);
	check(p.ships == 2 && p.male && p.first_name == "Georg" && p.family_name == "Stromberg", "1.pat's player");
}

// Another set of choices over the game's start: each byte where the window puts it.
void other_choices() {
	NewGameChoices c;
	c.first_name = "Anna";
	c.family_name = "Hansen";
	c.female = true;
	c.year = 1375;
	c.level = 3;
	c.details = {1, 2, 3, 1, 2, 3, 3, 2};
	c.goal = 4;
	c.goal_time = 7;
	c.speed = 2;
	c.production = 0;
	c.home = 12;
	const SettingsMemory before = initial_game_settings();
	const SettingsMemory s = new_game_settings(c, test_map(), before, false);
	check(s.u32(0x0) == 1375 && s.u32(0x4) == 2, "year and speed");
	check(s.u8(0x8) == 7 && s.u8(0xF) == 4, "goal 4 after 7");
	check(s.u8(0x9) == 2 && s.u8(0xA) == 2, "capital and start");
	check(s.u8(0xB) == 12 && s.u8(0xC) == 1 && s.u8(0xD) == 3 && s.u8(0xE) == 3, "merchants, one player, level");
	check(s.u8(0x10) == 0 && s.u8(0x11) == 1 && s.u8(0x17) == 2 && s.u8(0x12) == 0 && s.u8(0x13) == 1 &&
			s.u8(0x16) == 2,
		"details");
	check(s.u8(0x14) == 0 && s.u8(0x15) == 0 && s.u8(0x18) == 0, "the bytes the window leaves");
	check(s.u8(0x19) == 0 && s.u8(0x25) == 0 && s.u8(0x26) == 1, "production, tooltips, videos");
	check(s.u8(0x1E) == 12 && std::strcmp(reinterpret_cast<const char*>(s.bytes.data() + 0x27), "Town17") == 0,
		"home town and its name");
	const NewGamePlayer p = new_game_player(c);
	check(p.ships == 1 && !p.male && p.first_name == "Anna" && p.family_name == "Hansen", "the player");
	// The goals' other branches.
	c.goal = 0;
	check(new_game_settings(c, test_map(), before).u8(0x8) == 0xE, "no goal");
	c.goal = 2;
	c.goal_time = 14;
	check(new_game_settings(c, test_map(), before).u8(0x8) == 0xE, "goal 2, time 14");
	c.goal = 6;
	c.goal_time = 2;
	check(new_game_settings(c, test_map(), before).u8(0x8) == 0x12, "goal 6 (the Hanse's population), time 2");
	c.goal_time = 3;
	SettingsMemory kept = before;
	kept.u8(0x8) = 0x33;
	check(new_game_settings(c, test_map(), kept).u8(0x8) == 0x33, "a time the window doesn't give");
}

void levels() {
	for (uint32_t k = 0; k < 5; ++k) {
		check(new_game_level_of(kNewGameLevelDetails[k]) == k, "each level's details give it");
	}
	check(new_game_level_of({2, 2, 2, 2, 2, 2, 3, 2}) == 2, "17: the third level");
	check(new_game_level_of({1, 1, 1, 1, 1, 2, 2, 2}) == 1, "11: nearer 12");
	check(new_game_level_of({1, 1, 1, 1, 1, 2, 2, 1}) == 0, "10: nearer 9");
	check(new_game_level_of({1, 1, 1, 1, 2, 2, 2, 2}) == 1, "12");
	check(new_game_level_of({2, 2, 2, 2, 2, 2, 2, 0}) == 1, "14: as near 12 as 16, the first");
}

void hanse_towns() {
	const GameMap map = test_map();
	check(next_hanse_town(map, 9) == 12 && next_hanse_town(map, 20) == 2 && next_hanse_town(map, 0) == 2,
		"the next Hanse town");
	check(previous_hanse_town(map, 9) == 2 && previous_hanse_town(map, 2) == 20 && previous_hanse_town(map, 0) == 20,
		"the previous Hanse town");
	check(next_hanse_town(map, 23) == 2 && previous_hanse_town(map, 23) == 20, "from a town that isn't");
	check(game_map_town_name(map, 9) == "L\xfc" "beck" && game_map_town_name(map, 30) == "Town5", "the names");
}

// The wares the home town's window lists (0x49C410).
void town_wares() {
	GameMap map = test_map();
	map.efficient[3] = 0x1 | 0x40 | 0x10000;  // bits 0 (skins), 6 (meat, and leather), 16 (hemp)
	map.inefficient[3] = 0x20000 | 0x8;  // bit 17 (whale oil and fish), bit 3 (iron goods)
	check(game_map_town_wares(map, 3, true) == std::vector<uint8_t>({9, 1, 17, 13}), "the efficient ones");
	check(game_map_town_wares(map, 3, false) == std::vector<uint8_t>({12, 10, 2}), "the inefficient ones");
	check(game_map_town_wares(map, 30, true).empty(), "past the towns");
}

void config_limits() {
	NewGameChoices c;
	c.year = 1299;
	c.level = 5;
	c.goal = 1;
	c.goal_time = 3;
	c.speed = 6;
	c.details = {0, 4, 3, 2, 1, 9, 3, 3};
	clamp_new_game_choices(c, 4);
	check(c.year == 1350 && c.level == 2 && c.goal == 0 && c.goal_time == 0 && c.speed == 4, "out of range");
	check(c.details[0] == 1 && c.details[1] == 3 && c.details[2] == 3 && c.details[5] == 3 && c.details[7] == 2,
		"the details");
	c.year = 1347;
	c.goal = 6;
	c.goal_time = 5;
	clamp_new_game_choices(c, 0);
	check(c.year == 1345 && c.goal == 6 && c.goal_time == 2 && c.speed == 4, "a year of 5 and goal 6's time");
	c.goal = 4;
	c.goal_time = 13;
	clamp_new_game_choices(c, 0);
	check(c.goal_time == 13, "goal 4's time 13 stays");
	c.goal = 7;
	clamp_new_game_choices(c, 0);
	check(c.goal == 0 && c.goal_time == 0, "goal 7 (capacity) isn't");
}

// The window's balls (0x498760): each one's rule, and when it can be pressed.
void balls() {
	using B = NewGameBall;
	NewGameWindowState w;
	NewGameChoices& c = w.choices;
	auto press = [&](B ball, int times = 1) {
		for (int k = 0; k < times; ++k) {
			press_new_game_ball(w, ball);
		}
	};
	auto on = [&](B ball) { return new_game_ball_enabled(c, ball); };
	// The sex.
	check(!on(B::GenderLeft) && on(B::GenderRight), "male: only →");
	press(B::GenderRight);
	check(c.female && on(B::GenderLeft) && !on(B::GenderRight), "female: only ←");
	press(B::GenderLeft);
	check(!c.female, "male again");
	// The year, by 5 between 1300 and 1400.
	c.year = 1345;
	press(B::YearMinus, 3);
	check(c.year == 1330 && on(B::YearMinus), "1345 − 15");
	press(B::YearMinus, 9);
	check(c.year == 1300 && !on(B::YearMinus) && on(B::YearPlus), "1300 at the most");
	press(B::YearPlus, 30);
	check(c.year == 1400 && !on(B::YearPlus), "1400 at the least");
	// The level sets its details.
	c.details = {3, 1, 3, 1, 3, 1, 3, 1};
	press(B::LevelPlus);
	check(c.level == 1 && c.details == kNewGameLevelDetails[1], "level 1 and its details");
	press(B::LevelPlus, 5);
	check(c.level == 4 && c.details == kNewGameLevelDetails[4] && !on(B::LevelPlus), "level 4 at the most");
	press(B::LevelMinus, 6);
	check(c.level == 0 && c.details == kNewGameLevelDetails[0] && !on(B::LevelMinus), "level 0");
	// The goal: no «Councillor» (1) going up or down, the time within each goal's limit.
	check(!new_game_ball_shown(c, B::TimeMinus) && !new_game_ball_shown(c, B::TimePlus), "no goal: no time");
	press(B::GoalRight);
	check(c.goal == 2 && c.goal_time == 0 && new_game_ball_shown(c, B::TimePlus), "→ from none: Lord Mayor");
	check(!on(B::TimeMinus) && on(B::TimePlus), "time 0 with a goal");
	press(B::TimePlus, 20);
	check(c.goal_time == 14 && !on(B::TimePlus) && on(B::TimeMinus), "goal 2's time: 14");
	press(B::GoalRight, 2);
	check(c.goal == 4 && c.goal_time == 13, "goal 4: the time down to 13");
	press(B::GoalRight);
	check(c.goal == 5 && c.goal_time == 13, "goal 5");
	press(B::GoalRight);
	check(c.goal == 6 && c.goal_time == 0 && w.saved_time == 13 && !on(B::GoalRight), "goal 6 puts the time aside");
	press(B::TimePlus, 3);
	check(c.goal_time == 2 && !on(B::TimePlus), "goal 6's time: 2");
	press(B::GoalRight);
	check(c.goal == 6 && c.goal_time == 0 && w.saved_time == 2, "→ at 6: stays, its time aside again");
	press(B::GoalLeft);
	check(c.goal == 5 && c.goal_time == 2, "back to 5: the time put aside");
	press(B::GoalLeft, 3);
	check(c.goal == 2 && c.goal_time == 2, "goal 2");
	press(B::GoalLeft);
	check(c.goal == 0 && c.goal_time == 0 && !on(B::GoalLeft), "← from Lord Mayor: none");
	press(B::TimePlus);
	check(c.goal_time == 0, "no goal, no time");
	press(B::TimeMinus);
	check(c.goal_time == 0, "the time not below 0");
	// The speed: «slowdown» (5) below «very slow» (0), «very fast» (4) the top.
	c.speed = 0;
	check(on(B::SpeedMinus) && on(B::SpeedPlus), "very slow: both");
	press(B::SpeedMinus);
	check(c.speed == 5 && !on(B::SpeedMinus) && on(B::SpeedPlus), "slowdown");
	press(B::SpeedPlus);
	check(c.speed == 0, "+ from slowdown: very slow");
	press(B::SpeedPlus, 6);
	check(c.speed == 4 && !on(B::SpeedPlus), "very fast at the most");
	press(B::SpeedMinus, 2);
	check(c.speed == 2, "normal");
	// The time put aside starts as the choices' at the first ball.
	NewGameWindowState first;
	first.choices.goal = 4;
	first.choices.goal_time = 9;
	press_new_game_ball(first, B::SpeedPlus);
	check(first.saved && first.saved_time == 9, "the time aside at the first press");
	press_new_game_ball(first, B::GoalLeft);
	press_new_game_ball(first, B::GoalRight);
	check(first.choices.goal == 4 && first.choices.goal_time == 9, "4 → 3 → 4 keeps the time");
}

}  // namespace

int main() {
	first_save();
	other_choices();
	levels();
	hanse_towns();
	town_wares();
	config_limits();
	balls();
	if (failures == 0) {
		std::printf("new game window: OK\n");
	}
	return failures == 0 ? 0 : 1;
}
