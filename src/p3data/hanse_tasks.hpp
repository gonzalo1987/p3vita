#pragma once

// The player's side of the path to Alderman, as the game's UI sees it: the
// candidacy, the Hanse tasks on offer (events 0x32), the player's task and
// the road materials, and the operations the UI queues (0x4B, 0x3B). The
// simulation side (the candidacy, the tasks, the operations' executors) is
// in town_tick/alderman.cpp; see docs/formats/world_tick.md §8 and
// docs/cloud/alderman-viewer.md. Everything here only reads the save, except
// the queue_* functions, which add an operation to SaveGame::queued_operations.

#include <array>
#include <cstdint>
#include <vector>

#include "p3data/savegame.hpp"

namespace p3::data {

// The kinds of Hanse task ([+0xC] of the event 0x32).
enum class HanseTaskType : uint8_t { FoundTown = 0, Road = 1, PirateShip = 2, PirateNest = 3, GrowTown = 4, Void = 5 };

// An event 0x32 of the queue (0x18 bytes; world_tick.md §8).
struct HanseTask {
	uint16_t event = 0xFFFF;  // Index in SaveGame::events.
	uint32_t deadline = 0;  // Tick ([+0x8]).
	uint8_t type = 0;  // HanseTaskType ([+0xC]).
	uint8_t merchant = 0xFF;  // [+0xD]; >= number of merchants: not given.
	bool started = false;  // [+0xF].
	std::array<uint8_t, 8> data{};  // [+0x10..+0x17], by type.
};

HanseTask hanse_task(const SaveGame& save, uint16_t event);

// 0x4EC5F4: up to `max` tasks not given yet (merchant >= number of
// merchants, type < 5), in queue order. The game's window asks for 5.
std::vector<uint16_t> offered_hanse_tasks(const SaveGame& save, size_t max = 5);

// 0x4EC694: the merchant's task (the first event 0x32 with it), or 0xFFFF
// (also when it isn't a candidate).
uint16_t merchant_hanse_task(const SaveGame& save, int merchant);

// The merchant as 0x4F75F0 sees it: the owner of its first office, or 0.
int merchant_as_owner(const SaveGame& save, int merchant);

// Towns whose mayor ([0x6F1]) is the merchant (as 0x4F75F0 sees it).
std::vector<int> mayor_towns(const SaveGame& save, int merchant);

// The «reputation to stand» of 0x4F75F0, with the same float steps; a mayor
// becomes a candidate with kCandidacyReputation.
int candidacy_reputation(const SaveGame& save, int merchant);
constexpr int kCandidacyReputation = 120;  // 0x6737B8

// [+0x37]: 0 not a candidate; 1..3 candidate with 0..2 tasks done; 4 can stand.
int candidacy_level(const SaveGame& save, int merchant);

// The Alderman (a merchant index, or >= number of merchants for a citizen).
int alderman(const SaveGame& save);

// The next Alderman election (event 0x20): its tick, or 0 if there is none.
uint32_t alderman_election_tick(const SaveGame& save);

// The road materials, in the order of the task's counters: wood, bricks,
// tools, hemp.
constexpr std::array<uint8_t, 4> kRoadWares = {0x0B, 0x13, 0x0C, 0x11};  // 0x672BF4
constexpr std::array<uint8_t, 4> kRoadSection = {3, 4, 1, 2};  // 0x672BF0, per section.

// 0x4EC704: per material, in loads or barrels: total for the whole road,
// done (used by the sections already built) and delivered (the task's
// counters). False if the event isn't a road task of the merchant.
struct RoadMaterials {
	std::array<int, 4> total{};
	std::array<int, 4> done{};
	std::array<int, 4> delivered{};
	int sections = 0;  // Of the pair of towns.
	int sections_left = 0;  // [+0x12].
	// What the road window shows as still needed (unsigned 16-bit, as the game).
	uint16_t needed(int k) const { return uint16_t(total[size_t(k)] - done[size_t(k)] - delivered[size_t(k)]); }
	// The road window's progress: done·100/total of the wood.
	int percent() const { return total[0] ? done[0] * 100 / total[0] : 0; }
};
bool road_materials(const SaveGame& save, uint16_t event, int merchant, RoadMaterials& out);

// The road window's «available»: per material, the merchant's office in the
// town plus its ships docked there, in loads or barrels.
std::array<int, 4> available_road_materials(const SaveGame& save, int merchant, int town);

// The amounts the road window offers (0x5EED40): 1, 5, 50 and all.
constexpr std::array<uint32_t, 4> kRoadAmounts = {1, 5, 50, 0xFFFFFFFF};

// The amount the road window sends: min(chosen, available), or the still
// needed value if that is smaller (0x5F2DA3..0x5F2DEB).
uint16_t road_window_amount(uint32_t chosen, uint32_t available, uint16_t needed);

// The operations, byte for byte as the game's UI builds them (0x14 bytes,
// 0x535740 and then the fields).
using OperationBytes = std::array<uint8_t, 0x14>;
// 0x4B (task window 0x5E87D0): {0x4B, player, event}.
OperationBytes take_task_operation(uint32_t player, uint16_t event);
// 0x3B (road window 0x5F2A20): {0x3B, u16 event, u16 amount, u8 town, u8
// player, u8 the material's ware}.
OperationBytes road_materials_operation(uint16_t event, uint16_t amount, uint8_t town, uint8_t player, uint8_t ware);

// 0x3F (Hanse window 0x5E8280): {0x3F, player, choice}: in the election the
// candidate 0..3, in a vote of the diet 0 yes, 1 no, 2 abstain.
OperationBytes vote_operation(uint32_t player, uint32_t choice);

// Today's election of the Alderman (event 0x20, 0x4D9C70), or 0xFFFF.
uint16_t election_today(const SaveGame& save);
// Whether the player can vote in it now (0x4D9D40).
bool player_can_vote(const SaveGame& save);
// The election's candidates ([+0x8 + k]): a merchant (| 0x80) or a town whose
// mayor stands; and the votes they have so far ([+0xC + k]).
struct ElectionCandidate {
	bool merchant = false;
	int index = -1;  // Merchant or town index; -1: none.
	int votes = 0;
};
std::array<ElectionCandidate, 4> election_candidates(const SaveGame& save, uint16_t event);

// Adds the operation to the queue; advance_game_tick runs it after the next
// tick, as the game runs its queue between two calls to advance_time.
void queue_operation(SaveGame& save, const OperationBytes& operation);

}  // namespace p3::data
