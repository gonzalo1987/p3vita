// The missions' list and their start (docs/formats/missions.md): the object *0x6DD8B4 (0x511000) and what fills it,
// 0x511270, 0x5110E0, 0x5116F0, 0x511690, 0x4F37E0; the operation 0xC0 (0x543810). The scripts themselves run in
// mission_script.cpp (0x4ECF64).

#include "p3data/missions.hpp"

#include <cstdio>
#include <cstring>

#include "p3data/cpr.hpp"
#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/savegame.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

std::string lower(std::string s) {
	for (char& c : s) {
		if (c >= 'A' && c <= 'Z') {
			c = char(c - 'A' + 'a');
		}
	}
	return s;
}

std::string trim(const std::string& s) {
	size_t a = 0;
	size_t b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) {
		++a;
	}
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) {
		--b;
	}
	return s.substr(a, b - a);
}

// The game's ini reader (0x4BE060 on 0x6DD524): the key's value, or "" (the defaults 0x6DD8B8..0x6DD8BA are empty).
std::string ini_value(const SaveGame& save, const std::string& section, const std::string& key) {
	auto s = save.missions.ini.find(lower(section));
	if (s == save.missions.ini.end()) {
		return {};
	}
	auto k = s->second.find(lower(key));
	return k == s->second.end() ? std::string() : k->second;
}

// MFC's CString::TrimLeft (0x64E2F0): the leading characters isspace() takes (the C locale's: 9..13 and the space).
std::string trim_left(const std::string& s) {
	size_t a = 0;
	while (a < s.size() && (s[a] == ' ' || (s[a] >= 9 && s[a] <= 13))) {
		++a;
	}
	return s.substr(a);
}

// MSVC's atoi (0x639CDA): the white space isspace() takes, a sign, the digits; no overflow check.
int32_t game_atoi(const char* p) {
	while (*p == ' ' || (*p >= 9 && *p <= 13)) {
		++p;
	}
	const char sign = *p;
	if (sign == '-' || sign == '+') {
		++p;
	}
	uint32_t v = 0;
	while (*p >= '0' && *p <= '9') {
		v = v * 10 + uint32_t(*p - '0');
		++p;
	}
	return int32_t(sign == '-' ? 0u - v : v);
}

// The game's 0x4D8CF0 (events.cpp).
uint16_t new_event(SaveGame& save, uint32_t tick) {
	return tick::events_detail::fn_4d8cf0(save, tick);
}

uint16_t program_u16(const std::vector<uint8_t>& p, size_t at) {
	return at + 2 <= p.size() ? uint16_t(p[at] | p[at + 1] << 8) : 0;
}

uint32_t program_u32(const std::vector<uint8_t>& p, size_t at) {
	uint32_t v = 0;
	if (at + 4 <= p.size()) {
		std::memcpy(&v, p.data() + at, 4);
	}
	return v;
}

// A program's byte, 0 past its end (the game reads whatever follows).
uint8_t program_u8(const std::vector<uint8_t>& p, size_t at) {
	return at < p.size() ? p[at] : 0;
}

// The offset of a program's section `s` (its table from [+0x4]).
uint32_t section_offset(const std::vector<uint8_t>& p, uint32_t s) {
	return program_u32(p, 4 + 4 * size_t(s));
}

}  // namespace

MissionIni read_missions_ini(const CprArchive& archive) {
	MissionIni ini;
	std::vector<uint8_t> data;
	for (const char* path : {"scripts/missions_spa.ini", "scripts/missions_eng.ini"}) {
		const CprEntry* entry = archive.find(path);
		if (entry && archive.read(*entry, data)) {
			break;
		}
		data.clear();
	}
	const std::string text(data.begin(), data.end());
	std::map<std::string, std::string>* section = nullptr;
	for (size_t pos = 0; pos < text.size();) {
		size_t end = text.find('\n', pos);
		if (end == std::string::npos) {
			end = text.size();
		}
		const std::string line = trim(text.substr(pos, end - pos));
		pos = end + 1;
		if (!line.empty() && line[0] == '[') {
			section = &ini[lower(line.substr(1, line.find(']') - 1))];
		} else if (section && line.find('=') != std::string::npos) {
			const size_t equals = line.find('=');
			const std::string key = lower(trim(line.substr(0, equals)));
			if (!section->count(key)) {
				(*section)[key] = trim(line.substr(equals + 1));
			}
		}
	}
	return ini;
}

void load_mission_files(const CprArchive& archive, SaveGame& save) {
	MissionData& m = save.missions;
	m.ini = read_missions_ini(archive);
	m.files.clear();
	auto missions = m.ini.find("missions");
	if (missions == m.ini.end()) {
		return;
	}
	for (const auto& [key, file] : missions->second) {
		const std::string name = lower(file);
		if (name.empty() || m.files.count(name)) {
			continue;
		}
		const CprEntry* entry = archive.find("missions_addon/" + file);
		std::vector<uint8_t> image;
		if (entry && archive.read(*entry, image)) {
			m.files[name] = std::make_shared<const std::vector<uint8_t>>(std::move(image));
			continue;
		}
		// GAME BUG: the ini's name spelt «ue» where the file has «ü» (308=Kampagne3_Entfuehrung.p2m, the file
		// Kampagne3_Entführung.p2m: the campaign 3's abduction never loads). Kept apart for fix_game_bugs
		// (mission_file).
		std::string umlaut = file;
		for (size_t at = umlaut.find("ue"); at != std::string::npos; at = umlaut.find("ue", at + 1)) {
			umlaut.replace(at, 2, std::string(1, char(0xFC)));  // (the archive's names are Latin-1)
		}
		entry = umlaut != file ? archive.find("missions_addon/" + umlaut) : nullptr;
		if (entry && archive.read(*entry, image)) {
			m.respelt[name] = std::make_shared<const std::vector<uint8_t>>(std::move(image));
		}
	}
}

MissionProgram mission_file(const SaveGame& save, uint16_t number) {
	const std::string file = ini_value(save, "Missions", std::to_string(number));
	auto f = file.empty() ? save.missions.files.end() : save.missions.files.find(lower(file));
	if (f != save.missions.files.end()) {
		return f->second;
	}
	if (fix_game_bugs() && !file.empty()) {
		// The file its name misspells (load_mission_files: the campaign 3's abduction).
		auto r = save.missions.respelt.find(lower(file));
		if (r != save.missions.respelt.end()) {
			return r->second;
		}
	}
	return nullptr;
}

void attach_mission_programs(SaveGame& save) {
	// 0x4D81E0 makes the list (0x4D82C0) and fills it with 0x511270 (0); then each event 0x1B, by its index, takes
	// its program (0x511690 with its mission [+0x10]).
	save.missions.exists = true;
	missions_detail::fn_511270(save, false);
	for (size_t i = 0; i < save.events.size(); ++i) {
		GameMemory<0x18>& e = save.events[i];
		if (e.u16(6) != 0x1B) {
			continue;
		}
		MissionData::Script& s = save.missions.scripts[uint16_t(i)];
		s.program = missions_detail::fn_511690(save, e.u16(0x10));
		e.u32(8) = s.program ? 1 : 0;
		e.u32(0xC) = e.u16(0x12) != 0 ? 1 : 0;
	}
}

void detach_mission_programs(SaveGame& save) {
	save.missions.list.clear();
	for (auto& [index, script] : save.missions.scripts) {
		script.program.reset();
		if (index < save.events.size()) {
			save.events[index].u32(8) = 0;
		}
	}
}

namespace missions_detail {

void fn_5110e0(SaveGame& save, const std::string& name, bool start) {
	std::vector<MissionData::Entry>& list = save.missions.list;
	const std::string text = trim_left(ini_value(save, "Group" + name, "Include"));
	for (size_t pos = 0;;) {
		size_t space = text.find(' ', pos);
		if (space == std::string::npos) {
			space = text.size();
			if (pos >= space) {
				break;
			}
		}
		const uint16_t number = uint16_t(game_atoi(text.c_str() + pos));
		size_t i = 0;
		while (i < list.size() && list[i].number != number) {
			++i;
		}
		if (i < list.size()) {
			if (start) {
				++list[i].count;
			}
		} else {
			MissionData::Entry e;
			e.number = number;
			e.count = start ? 1 : 0;
			list.push_back(e);
		}
		pos = space + 1;
	}
}

bool fn_5116f0(SaveGame& save, uint16_t number, size_t index) {
	// Without its name (0x4BE060 gives 0), not in the archive (0x402760 fails), or empty (0x402860 gives 0): false.
	const MissionProgram program = mission_file(save, number);
	if (!program || program->empty()) {
		return false;
	}
	if (index < save.missions.list.size()) {
		save.missions.list[index].program = program;
	}
	return true;
}

void fn_511270(SaveGame& save, bool start) {
	MissionData& m = save.missions;
	m.list.clear();
	// The mode's section (the settings' [+0xD], table 0x511674).
	std::string section;
	const uint8_t mode = save.settings.u8(0xD);
	if (mode == 0) {
		section = "Hotseat";
	} else if (mode == 1 || mode == 2) {
		section = "Multiplay";
	} else if (mode == 3) {
		section = "Einzelspiel";
	} else if (mode == 4 || mode == 5) {
		section = "Kampagne" + std::to_string(save.settings.u8(0x14));
	}
	const std::string groups = trim_left(ini_value(save, section, "IncludeGroup"));
	for (size_t pos = 0;;) {
		size_t space = groups.find(' ', pos);
		if (space == std::string::npos) {
			space = groups.size();
			if (pos >= space) {
				break;
			}
		}
		fn_5110e0(save, groups.substr(pos, space - pos), start);
		pos = space + 1;
	}
	if (!start) {
		return;
	}
	for (size_t i = 0; i < m.list.size(); ++i) {
		fn_5116f0(save, m.list[i].number, i);
	}
	const uint32_t now = save.globals.u32(global::kWorldTick);
	for (size_t i = 0; i < m.list.size(); ++i) {
		if (!m.list[i].program) {
			m.list[i].count = 0;
			continue;
		}
		// GAME BUG: the loop counts up to the entry's count while each script dropped (its tick passed) counts it
		// down, so of three such scripts only two go and the program keeps one too many (none of the game's missions
		// that repeat waits for a fixed tick: no effect with its data). Fixed: as many turns as it had.
		const uint32_t scripts = m.list[i].count;
		for (uint32_t k = 0; fix_game_bugs() ? k < scripts : k < m.list[i].count; ++k) {
			if (!m.list[i].program) {
				break;  // (fix_game_bugs: all of them dropped and the program gone)
			}
			const MissionProgram program = m.list[i].program;
			const std::vector<uint8_t>& p = *program;
			std::vector<uint32_t> variables(program_u16(p, 2), 0);
			// The first sections that set a variable (0x0A: u8 variable, u32 value) run; the first other one waits.
			uint8_t s = 0;
			uint8_t op = program_u8(p, section_offset(p, s));
			while (op != 0 && op < 0xFD) {
				if (op == 0x0A) {
					const uint32_t at = section_offset(p, s);
					const uint8_t v = program_u8(p, at + 1);
					if (v < variables.size()) {
						variables[v] = program_u32(p, at + 2);
					}
					++s;
				}
				op = program_u8(p, section_offset(p, s));
			}
			const uint8_t v = program_u8(p, section_offset(p, s) + 1);
			const uint32_t value = v < variables.size() ? variables[v] : 0;
			uint32_t tick = 0;
			if (op == 0xFD) {
				tick = value + now;  // 0xFD: wait so many ticks.
			} else if (op == 0) {
				// 0x00: wait for a tick; one that has passed (with a day's grace) drops the script.
				if (value - 0x100 < now) {
					fn_4f37e0(save, program);
					continue;
				}
				tick = value;
			} else {
				continue;  // 0xFE, 0xFF: nothing (the game leaks the variables).
			}
			const uint16_t index = new_event(save, tick);
			GameMemory<0x18>& e = save.events[index];
			e.u16(6) = 0x1B;
			e.u16(0x14) = uint16_t(s + 1);
			e.u32(0xC) = 1;  // (the game's new of 0 bytes is not null either)
			e.u32(8) = 1;
			e.u16(0x10) = m.list[i].number;
			e.u16(0x12) = program_u16(p, 2);
			m.scripts[index] = MissionData::Script{program, std::move(variables)};
		}
	}
}

MissionProgram fn_511690(SaveGame& save, uint16_t number) {
	std::vector<MissionData::Entry>& list = save.missions.list;
	for (size_t i = 0; i < list.size(); ++i) {
		if (list[i].number == number) {
			if (!list[i].program) {
				fn_5116f0(save, number, i);
			}
			++list[i].count;
			return list[i].program;
		}
	}
	return nullptr;
}

void fn_4f37e0(SaveGame& save, const MissionProgram& program) {
	if (!program) {
		return;
	}
	std::vector<MissionData::Entry>& list = save.missions.list;
	for (MissionData::Entry& e : list) {
		if (e.program == program) {
			if (--e.count == 0) {
				e.program.reset();
			}
			return;
		}
	}
}

void fn_532420(SaveGame& save, uint32_t competitors) {
	GlobalMemory& g = save.globals;
	const uint32_t merchants = g.u16(global::kWorldMerchantCount);
	constexpr uint32_t kOne = 0x3F800000;  // 1.0f
	constexpr uint32_t kTwo = 0x40000000;  // 2.0f
	for (uint32_t i = merchants; i > 0;) {
		MerchantMemory& m = save.merchants[--i].memory;
		const uint16_t flags = m.u16(0x8);
		if (flags & 0x8000) {
			m.u16(0x8) = uint16_t(flags & 0x7FFF);
			m.u8(0x8) |= 1;
		}
		// GAME BUG: a human's factor too, which 0xA7 has just set by the difficulty (0x4F9550: 1.3, 1 or 0.7) and
		// nothing sets again (0x4F42B0, from the event 0x04, only the AI's): the difficulty's factor is lost in a game
		// of one player (checked: re/emu/sesion45/ngfactor.sh, the player at 1 after a new game set to 1.3). Fixed:
		// a human's kept.
		if (!(fix_game_bugs() && m.u16(0x8) == 0)) {
			m.u32(0x464) = kOne;
		}
	}
	// For each human merchant (from the last), a merchant of the kind 1 at home in the same town (from the last) makes
	// one of the competitors: GAME BUG, the factor and the bit go to the merchant 0 (0x532500: the array's base, not
	// that merchant).
	for (uint32_t i = merchants; i > 0;) {
		const MerchantMemory& human = save.merchants[--i].memory;
		if (human.u16(0x8) != 0) {
			continue;
		}
		bool found = false;
		uint32_t j = merchants;
		while (j > 0 && !found) {
			const MerchantMemory& other = save.merchants[--j].memory;
			found = other.u8(0x19) == human.u8(0x19) && (other.u8(0x8) & 7) == 1;
		}
		if (!found) {
			continue;
		}
		if (competitors == 0) {
			break;
		}
		// Fixed: the merchant found (fix_game_bugs).
		MerchantMemory& rival = save.merchants[fix_game_bugs() ? j : 0].memory;
		rival.u32(0x464) = kTwo;
		rival.u8(0x8) |= 8;
		--competitors;
	}
	for (uint32_t i = 0; competitors > 0 && i < merchants; ++i) {
		MerchantMemory& m = save.merchants[i].memory;
		if ((m.u8(0x8) & 7) == 1 && m.u32(0x464) == kOne) {
			m.u16(0x8) = uint16_t(m.u16(0x8) | 8);
			m.u32(0x464) = kTwo;
			--competitors;
		}
	}
	// (0x532578 walks the event queue to its first event 3 and keeps nothing.)
	// The Alderman's town: the Hanse town (flags 0x40) without a human's home whose number is nearest to 14.
	uint8_t& alderman_town = g.u8(0x6DE52E);
	alderman_town = 0xFF;
	const uint32_t towns = g.u32(global::kWorldTownCount);
	auto distance = [&](uint32_t town) {
		const uint32_t number = g.u8(0x6DE4B8 + town);
		return number > 0xE ? number - 0xE : 0xE - number;
	};
	for (uint32_t t = 0; t < towns; ++t) {
		if ((save.towns[t].memory.u32(0x2C8) & 0x40) != 0x40) {
			continue;
		}
		bool home = false;
		for (uint32_t i = 0; i < merchants && !home; ++i) {
			const MerchantMemory& m = save.merchants[i].memory;
			home = m.u16(0x8) == 0 && m.u8(0x19) == t;
		}
		if (home) {
			continue;
		}
		if (alderman_town == 0xFF || int32_t(distance(t)) < int32_t(distance(alderman_town))) {
			alderman_town = uint8_t(t);
		}
	}
	if (alderman_town == 0xFF) {
		alderman_town = 0;
	}
	g.u8(0x6DE52A) = 0;
	g.u32(0x6DE4AC) = g.u32(global::kWorldTick);
}

void fn_543810(SaveGame& save) {
	fn_532420(save, uint32_t(save.settings.u8(0xE)) + 1);
	for (uint32_t t = save.globals.u32(global::kWorldTownCount); t > 0;) {
		tick::sieges_detail::fn_635170(save, int(--t));
	}
	// The old list goes (0x511080: its programs freed; the scripts that still pointed into them would read freed
	// memory in the game: here they keep theirs), and a new one starts the mode's missions.
	save.missions.list.clear();
	save.missions.exists = true;
	fn_511270(save, true);
}

}  // namespace missions_detail

}  // namespace p3::data
