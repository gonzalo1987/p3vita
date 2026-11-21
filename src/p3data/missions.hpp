#pragma once

// The missions (docs/formats/missions.md): the programs of missions_addon/*.p2m, which scripts/missions_eng.ini
// (missions_spa.ini in Spanish) lists and groups by the game's mode; the game's list of the missions of a game (the
// object *0x6DD8B4, 0x511000) and the scripts that run them (the events 0x1B, whose program the interpreter 0x4ECF64
// runs: mission_script.hpp).

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace p3::data {

class CprArchive;
struct SaveGame;

// A mission's program: the whole .p2m (u16 sections, u16 variables, u32 offset of each section; each section one
// instruction; the texts after the last one). Shared, as the game's buffer is by every script of the mission.
using MissionProgram = std::shared_ptr<const std::vector<uint8_t>>;
// An ini of the game's: its sections (lower case) with their keys (lower case) and values.
using MissionIni = std::map<std::string, std::map<std::string, std::string>>;

struct MissionData {
	// From the game's data (load_mission_files): the ini's sections (lower case) with their keys (lower case) and
	// values, in Latin-1; and the .p2m of each file name of its [Missions] (lower case), as missions_addon/ has it
	// (the ones the archive lacks, absent).
	MissionIni ini;
	std::map<std::string, MissionProgram> files;
	// The files the ini misspells («ue» for the file's «ü»: the campaign 3's abduction), by the ini's name, for
	// fix_game_bugs (a game bug: without the fix they don't load).
	std::map<std::string, MissionProgram> respelt;
	// The game's list (*0x6DD8B4, 0x511000: [+0x4] its entries of 8 bytes, [+0x8] u16 their count): u32 [+0x0] the
	// program (0 until it is loaded, 0x5116F0), u16 [+0x4] the mission's number, u16 [+0x6] how many scripts use it
	// (at the start, how many to start). `exists`: the pointer *0x6DD8B4 (made by a load, 0x4D81E0, and by 0xC0).
	struct Entry {
		MissionProgram program;
		uint16_t number = 0;
		uint16_t count = 0;
	};
	bool exists = false;
	std::vector<Entry> list;
	// The script of each event 0x1B, by its record's index in SaveGame::events: its program (the event's [+0x8])
	// and its variables (the [+0x12] u32 at its [+0xC]). In the event, [+0x8] and [+0xC] hold 1 while there is one
	// and 0 when not (the game's pointers).
	struct Script {
		MissionProgram program;
		std::vector<uint32_t> variables;
	};
	std::map<uint16_t, Script> scripts;
};

// The missions' ini (scripts/missions_spa.ini when the Spanish archive is mounted, else missions_eng.ini): its
// sections (lower case) with their keys (lower case) and values (Latin-1; a key twice, its first).
MissionIni read_missions_ini(const CprArchive& archive);

// The game's data for the missions: scripts/missions_spa.ini when the Spanish archive is mounted (its own .p2m with
// the Spanish texts), else missions_eng.ini, and the .p2m its [Missions] names (missions_addon/). After it a load
// attaches the programs of the save's scripts (attach_mission_programs).
void load_mission_files(const CprArchive& archive, SaveGame& save);

// The program of the mission `number` as the data has it ([Missions] <number>=file, missions_addon/file); null
// without its name or its file.
MissionProgram mission_file(const SaveGame& save, uint16_t number);

// What a load does with the missions (0x4D81E0: 0x511270 with 0, then 0x511690 for each event 0x1B by its index):
// the list of the game's mode and each script's program. After parse_savegame and load_mission_files.
void attach_mission_programs(SaveGame& save);

// The oracle without the missions (its load skips 0x511270, as before 2026-11-19): an empty list and every script
// without its program (its variables kept, as the game's load reads them). The checks that compare with such a
// recording (tick_probe) call it after their own load.
void detach_mission_programs(SaveGame& save);

namespace missions_detail {
// 0x5110E0 (on the list): the missions of the group `name` (missions.ini [Group<name>] Include=, numbers apart by
// spaces): a number not in the list goes in with the count 1 if `start` (else 0); one in it gets one more if
// `start`.
void fn_5110e0(SaveGame& save, const std::string& name, bool start);
// 0x511270 (on the list): the list of the game's mode (the settings' [+0xD]: 0 Hotseat, 1 and 2 Multiplay, 3
// Einzelspiel, 4 and 5 Kampagne<[+0x14]>; its IncludeGroup=, groups apart by spaces) and, if `start`, each mission's
// program and its scripts: each one's first sections that set a variable (0x0A) run, and the first other one waits:
// 0xFD a time from now, 0x00 a tick (dropped if it has passed); each an event 0x1B.
void fn_511270(SaveGame& save, bool start);
// 0x5116F0 (on the list): the program of the mission `number` ([Missions] <number>=file, missions_addon/file) into the
// entry `index`; false (the entry unchanged) without its name or its file, or with an empty file.
bool fn_5116f0(SaveGame& save, uint16_t number, size_t index);
// 0x511690 (on the list): a script of the mission `number` takes its program (loaded first if the entry has none):
// one more on its count; null for a number not in the list.
MissionProgram fn_511690(SaveGame& save, uint16_t number);
// 0x4F37E0 (on the list): a script lets go of its program: one less on the count of its entry (by the program), and
// at 0 the program goes (its entry keeps 0, 0x5116F0 loads it again).
void fn_4f37e0(SaveGame& save, const MissionProgram& program);
// 0x543810, the operation 0xC0 (queued by a new game of one player and the network game's start): the merchants'
// competitors (0x532420), each town's lists of the world's scarcest wares (0x635170) and a new list with its
// missions started (0x511270 with 1).
void fn_543810(SaveGame& save);
// 0x532420 (on the world): every merchant's factor [+0x464] to 1 (the bit 0x8000 of [+0x8] becoming 0x1),
// `competitors` merchants of the kind 1 with the factor 2 and the bit 8 (a game bug: the one of a human's home town
// is written into the merchant 0; and a human's factor set by the difficulty is lost: both fixed behind
// fix_game_bugs), and the Alderman's town [0x6DE52E]: the Hanse town (flags 0x40) without a human's home whose number
// is nearest to 14.
void fn_532420(SaveGame& save, uint32_t competitors);
}  // namespace missions_detail

}  // namespace p3::data
