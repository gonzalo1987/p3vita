// The missions' list, their start and the interpreter's waits (p3data/missions.hpp, mission_script.hpp), without game
// data: a made-up ini and programs. Checked against the game by the oracle (the scenario mission_start and the loads
// with --misiones, docs/oracle.md); this keeps the rules: the groups and numbers apart by spaces as 0x5110E0 and
// 0x511270 split them (an empty token's atoi reads the next number), the sections that set a variable, 0xFD and 0x00
// at the start (a passed tick drops the script and lets go of its program), the waits 0x00, 0xFD, 0xFE and 0xFF;
// and 0x532420's competitors with its bug (the merchant 0); and the fixes of the game's bugs (fix_game_bugs).

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "p3data/game_fixes.hpp"
#include "p3data/mission_script.hpp"
#include "p3data/missions.hpp"
#include "p3data/savegame.hpp"

using namespace p3::data;
using namespace p3::data::missions_detail;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

constexpr uint32_t kNow = 0x10000;

// A program from its sections (each one instruction's bytes) and its number of variables.
MissionProgram program(uint16_t variables, const std::vector<std::vector<uint8_t>>& sections) {
	std::vector<uint8_t> p(4 + 4 * sections.size());
	p[0] = uint8_t(sections.size());
	p[2] = uint8_t(variables);
	for (size_t i = 0; i < sections.size(); ++i) {
		const uint32_t at = uint32_t(p.size());
		std::memcpy(p.data() + 4 + 4 * i, &at, 4);
		p.insert(p.end(), sections[i].begin(), sections[i].end());
	}
	return std::make_shared<const std::vector<uint8_t>>(std::move(p));
}

std::vector<uint8_t> set(uint8_t variable, uint32_t value) {
	return {0x0A, variable, uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)};
}

// An empty event queue (its head past the end) at kNow, the mode 3 (Einzelspiel).
SaveGame world() {
	SaveGame save;
	save.globals.u32(global::kWorldTick) = kNow;
	save.globals.u16(0x6DD744) = 0xFFFF;
	save.settings.u8(0xD) = 3;
	return save;
}

}  // namespace

int main() {
	{
		// 0x511270 with 1: «A  B» gives the groups A, "" (no section) and B; «5 5  7 7» the 5 twice and the 7 three
		// times (the empty token between the two spaces reads « 7»); the 9 has no file.
		SaveGame save = world();
		MissionData& m = save.missions;
		m.ini["einzelspiel"]["includegroup"] = "  A  B";
		m.ini["groupa"]["include"] = "5 5  7 7";
		m.ini["groupb"]["include"] = "9";
		m.ini["missions"]["5"] = "Five.p2m";
		m.ini["missions"]["7"] = "seven.p2m";
		m.ini["missions"]["9"] = "nine.p2m";
		m.files["five.p2m"] = program(2, {set(0, 0x300), set(1, 7), {0xFD, 0x00, 0x09, 0x00}, {0xFF}});
		m.files["seven.p2m"] = program(1, {set(0, 0x100), {0x00, 0x00}, {0xFF}});
		m.exists = true;
		fn_511270(save, true);
		check(m.list.size() == 3, "three missions in the list");
		check(m.list.size() == 3 && m.list[0].number == 5 && m.list[0].count == 2 && m.list[0].program,
			"the 5: two scripts, its program");
		check(m.list.size() == 3 && m.list[1].number == 7 && m.list[1].count == 1 && m.list[1].program,
			"the 7: its tick has passed; the loop counts down as it drops them, so it stops after two: count 1");
		check(m.list.size() == 3 && m.list[2].number == 9 && m.list[2].count == 0 && !m.list[2].program,
			"the 9: no file, count 0");
		check(m.scripts.size() == 2, "two scripts");
		int at_wait = 0;
		for (const auto& [i, s] : m.scripts) {
			const GameMemory<0x18>& e = save.events[i];
			at_wait += e.u16(6) == 0x1B && e.u32(0) == kNow + 0x300 && e.u16(0x14) == 3 && e.u16(0x10) == 5 &&
				e.u16(0x12) == 2 && s.variables.size() == 2 && s.variables[0] == 0x300 && s.variables[1] == 7;
		}
		check(at_wait == 2, "each an event 0x1B at now + 0x300, from the section after the 0xFD (not its own target)");

		// The interpreter at the 0xFF: the end, the program let go (one script left on the 5).
		const uint16_t first = m.scripts.begin()->first;
		save.globals.u32(global::kWorldTick) = kNow + 0x300;
		check(run_mission_script(save, first) == 0, "0xFF ends the script");
		check(m.list[0].count == 1 && m.list[0].program, "the 5's count one less, its program kept");
		check(save.events[first].u32(0xC) == 0 && m.scripts[first].variables.empty(), "its variables gone");

		// 0x511270 with 0 (a load): the list without counts or programs; 0x511690 loads one and counts it.
		fn_511270(save, false);
		check(m.list.size() == 3 && m.list[0].count == 0 && !m.list[0].program, "a load's list: nothing started");
		check(fn_511690(save, 5) == m.list[0].program && m.list[0].program && m.list[0].count == 1,
			"0x511690 loads the program and counts it");
		check(!fn_511690(save, 9) && m.list[2].count == 1, "a mission without a file: null, counted all the same");
		check(!fn_511690(save, 11), "a number not in the list: null");
	}
	{
		// The waits: 0x00 to a tick still to come (and one that has passed), 0xFE so many days from the section 0,
		// 0xFD to its section; an instruction without a handler (0x30) ends the script.
		SaveGame save = world();
		MissionData& m = save.missions;
		const MissionProgram p = program(3, {{0xFE, 0x01}, {0x00, 0x00}, {0xFD, 0x02, 0x00, 0x00}, {0x30}});
		m.list.push_back({p, 1, 1});
		save.events.resize(1);
		GameMemory<0x18>& e = save.events[0];
		e.u16(6) = 0x1B;
		e.u32(8) = 1;
		e.u32(0xC) = 1;
		e.u16(0x10) = 1;
		e.u16(0x12) = 3;
		m.scripts[0] = {p, {kNow + 0x500, 2, 0x40}};
		e.u16(0x14) = 1;
		check(run_mission_script(save, 0) == 1 && e.u32(0) == kNow + 0x500 && e.u16(0x14) == 2,
			"0x00: waits for its tick");
		e.u16(0x14) = 0;
		check(run_mission_script(save, 0) == 1 && e.u32(0) == kNow + 0x200 && e.u16(0x14) == 0,
			"0xFE: two days, section 0");
		e.u16(0x14) = 2;
		check(run_mission_script(save, 0) == 1 && e.u32(0) == kNow + 0x40 && e.u16(0x14) == 0,
			"0xFD: 0x40 ticks, its section");
		m.scripts[0].variables[0] = kNow + 0x50;  // within the day's grace: passed
		e.u16(0x14) = 1;
		check(run_mission_script(save, 0) == 0 && m.list[0].count == 0 && !m.list[0].program,
			"0x00 to a passed tick: the end");
		m.list[0] = {p, 1, 1};
		m.scripts[0] = {p, {0, 0, 0}};
		e.u32(8) = 1;
		e.u32(0xC) = 1;
		e.u16(0x14) = 3;
		check(run_mission_script(save, 0) == 0 && m.list[0].count == 0, "an instruction without a handler ends it");
		e.u32(8) = 0;
		e.u32(0xC) = 1;
		m.scripts[0] = {nullptr, {1, 2, 3}};
		check(run_mission_script(save, 0) == 0 && e.u32(0xC) == 0 && m.scripts[0].variables.empty(),
			"without a program: its variables go");
	}
	{
		// 0x532420: the merchant 0 (kind 7), 1 (kind 1, at home in the town 2) and 2 (human, at home in the town 2);
		// two competitors: the merchant 1's place goes to the merchant 0 (the game's bug), then the merchant 1 by the
		// loop of the kind 1. The Alderman's town: the Hanse towns 0 (number 10) and 1 (15), not the human's 2.
		SaveGame save = world();
		GlobalMemory& g = save.globals;
		g.u16(global::kWorldMerchantCount) = 3;
		g.u32(global::kWorldTownCount) = 3;
		save.merchants.resize(3);
		save.towns.resize(3);
		save.merchants[0].memory.u16(8) = 7;
		save.merchants[1].memory.u16(8) = 0x8000;
		save.merchants[1].memory.u8(0x19) = 2;
		save.merchants[2].memory.u8(0x19) = 2;
		for (SavedTown& t : save.towns) {
			t.memory.u32(0x2C8) = 0x40;
		}
		g.u8(0x6DE4B8) = 10;
		g.u8(0x6DE4B9) = 15;
		g.u8(0x6DE4BA) = 14;
		fn_532420(save, 2);
		check(save.merchants[1].memory.u16(8) == 9 && save.merchants[1].memory.u32(0x464) == 0x40000000,
			"the merchant 1: its bit 0x8000 to 1, then a competitor (bit 8, factor 2)");
		check(save.merchants[0].memory.u16(8) == 0xF && save.merchants[0].memory.u32(0x464) == 0x40000000,
			"the merchant 0: the competitor of the human's town (the game's bug)");
		check(save.merchants[2].memory.u32(0x464) == 0x3F800000, "the human: factor 1");
		check(g.u8(0x6DE52E) == 1, "the Alderman's town: the number nearest to 14 without a human at home");
		check(g.u32(0x6DE4AC) == kNow, "[0x6DE4AC] the tick");
	}
	{
		// The fixes (fix_game_bugs): the seven's three scripts of a passed tick all dropped and its program gone; the
		// file the ini misspells found; 0x532420's competitor the merchant it found and the human's factor kept.
		set_fix_game_bugs(true);
		SaveGame save = world();
		MissionData& m = save.missions;
		m.ini["einzelspiel"]["includegroup"] = "A";
		m.ini["groupa"]["include"] = "7 7 7 8";
		m.ini["missions"]["7"] = "seven.p2m";
		m.ini["missions"]["8"] = "Kampagne3_Entfuehrung.p2m";
		m.files["seven.p2m"] = program(1, {set(0, 0x100), {0x00, 0x00}, {0xFF}});
		m.respelt["kampagne3_entfuehrung.p2m"] = program(1, {set(0, 0x40), {0xFD, 0x00, 0x00, 0x00}, {0xFF}});
		fn_511270(save, true);
		check(m.list.size() == 2 && m.list[0].count == 0 && !m.list[0].program,
			"fixed: the seven's three scripts dropped, its program gone");
		check(m.list.size() == 2 && m.list[1].count == 1 && m.list[1].program && m.scripts.size() == 1,
			"fixed: the misspelt file found, its script started");
		set_fix_game_bugs(false);
		check(!mission_file(save, 8), "without the fix the misspelt file isn't found");

		set_fix_game_bugs(true);
		GlobalMemory& g = save.globals;
		g.u16(global::kWorldMerchantCount) = 3;
		g.u32(global::kWorldTownCount) = 0;
		save.merchants.assign(3, SavedMerchant());
		save.merchants[0].memory.u16(8) = 7;
		save.merchants[1].memory.u16(8) = 1;
		save.merchants[1].memory.u8(0x19) = 2;
		save.merchants[2].memory.u8(0x19) = 2;
		save.merchants[2].memory.u32(0x464) = 0x3FA66666;  // 1.3, the easy difficulty's
		fn_532420(save, 1);
		check(save.merchants[1].memory.u16(8) == 9 && save.merchants[1].memory.u32(0x464) == 0x40000000,
			"fixed: the competitor the merchant of the kind 1 at the human's home");
		check(save.merchants[0].memory.u16(8) == 7 && save.merchants[0].memory.u32(0x464) == 0x3F800000,
			"fixed: the merchant 0 untouched");
		check(save.merchants[2].memory.u32(0x464) == 0x3FA66666, "fixed: the human's factor kept");
		set_fix_game_bugs(false);
	}
	if (failures == 0) {
		std::printf("missions: ok\n");
	}
	return failures == 0 ? 0 : 1;
}
