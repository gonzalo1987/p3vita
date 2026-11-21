#pragma once

// The town tiles' own animations (re/emu/sesion50/README.md, «Part 3»): a tile type's .let variants of kind 3 and up
// (TileDef::variants), in groups by kind (the generic reader 0x551380: kind k the group k − 3, each variant of it a
// page in the file's order), each group with a state chosen by its first page's parameters' count (0x552EF0): 2 a
// loop (0x551860), 4 a loop of a page chosen at each start (0x54EBD0), 5 with waits and a page at random (0x552200), 7
// the same by places (0x54EE30), 9 the same, each page repeated (0x552500). The states are the tile type's (its class
// object's [+0x14]), shared by all its buildings: the view steps them for each of their cells in view (0x592000 → the
// class's +0x38, 0x552C10 → 0x585900) and draws the frame of each over each of their slices (0x552C90 → 0x585970,
// 0x585550: clipped to the slice's columns). None of it is in the save; the game steps them with the CRT's rand(),
// the viewer with a random of its own.

#include <cstdint>
#include <vector>

#include "p3data/town.hpp"

namespace p3::data {

struct TileAnimGroup {
	std::vector<const TileVariant*> pages;
	int kind = 0;  // Its first page's parameters' count: 2, 4, 5, 7 or 9 (any other: no state, never shown).
	uint32_t time = 0;  // [+0x4]: when its next frame is due, or its wait ends (the view's milliseconds).
	uint8_t frame = 0;  // [+0x8]
	uint8_t mode = 0;  // [+0x9]: 0 nothing new, 1 a frame on, 2 ended, 3 started; 0x80 going backwards.
	uint8_t flags = 0xC0;  // [+0xC]: its page (& 0x3F), 0x40 hidden, 0x80 stopped.
	uint8_t place = 0;  // [+0x10] (7, 9): where it is (its pages go from their parameter 5 to their 6).
	uint8_t repeats = 1;  // [+0x11] (9): the rounds of its page left.

	// The page shown and its frame (in the page's atlas; drawn at the page's x, y in the tile's rectangle), or null
	// when hidden (unless `even_hidden`: the shipyard draws two of its groups so, 0x584F00).
	const TileSlice* shown(const TileVariant** page = nullptr, bool even_hidden = false) const;
};

// A tile type's animated groups (empty without variants of kind 3 or more).
std::vector<TileAnimGroup> tile_anim_groups(const TileDef& tile);

// One step of a group (for a cell of one of its type's buildings in view): `now` the view's milliseconds, `active`
// that building's (its class's +0x80), `rand_state` the viewer's random (MSVC's rand()).
void step_tile_anim(TileAnimGroup& g, uint32_t now, bool active, uint32_t& rand_state);

// Whether a workshop is at work for its animations (0x54F710 with its facility, P3Modding's FacilityId: 4 the
// hunting lodge, 6 the brewery, 7 the smithy, 8 the apiary, 0xA the cattle farm, 0xC the weaving mill, 0xD the
// saltworks, 0xF the sheep farm, 0x10 the vineyard, 0x11 the pottery, 0x12 the quarry, 0x13 the pitch maker; the
// animal farms keep each field's .let in a list of their class, 0x5531B0, but each building is drawn by its own
// .let): its owner (a merchant) has that business producing in the town (his [+0x40 + 4 · town], bit `facility`),
// and in a siege (the town's [0x2C8] & 0x10) only inside the walls (its cell's grid byte above 0x7F).
bool workshop_active(uint32_t producing, int facility, bool siege, uint8_t grid);

// The facility a tile class's animations wait for (0x54F710's argument in its +0x80), −1 if the town being shown is
// enough (0x54EA20), −2 if the class's animations aren't rewritten yet.
int tile_anim_facility(uint32_t type);

// A tile type's animations as its class keeps them: its groups (the generic reader's), and what some classes keep
// of their own.
struct TileAnims {
	uint32_t type = 0;
	std::vector<TileAnimGroup> groups;
	// The repair yard (1B, 0x584260): whether it has work ([+0x19]: a ship in one of the town's shipyard lists or a
	// convoy) and its ship's stage ([+0x18], 0 none).
	uint8_t busy = 0;
	uint8_t stage = 0;
	// Simple states of a class's own over pages it picks (`flags` & 0x3F the page, 0x3F none): the sawmill's woman
	// and carpenter (0x556860: [+0x18] and [+0x24], their pages [+0x30], [+0x31]), the foundry's kettle and smoke
	// (0x555190: [+0x18] and [+0x3C], the kettle's page [+0x24]).
	TileAnimGroup workers[2];
	// The sawmill's pages (its reader 0x556780: one by kind, the slots 0x6E14B0 + 0x18 · k: kinds 3, 4, 5, 8, 6, 7).
	std::vector<const TileVariant*> pages;
	// The grain and hemp farms (53: 0x553AB0, 0x5538D0; 54: 0x554560, 0x5543A0): the field's page (its kind 3), its
	// stage (0xFF none: [+0x34], [+0x1C]) and when it grows on ([+0x1C], [+0x20]: the world's tick; every days of the
	// .let's second parameter, ·256), whether it is the farm itself (the .let's third parameter 0: [+0x18] 0) or one of
	// its fields, its people's state (9 parameters: [+0x20], [+0x24]) over the farmer's wife's (kind 4) or the farmer's
	// pages (kind 5; the hemp's farmer is its kind 4), and whether it worked at its last step.
	const TileVariant* field = nullptr;
	uint32_t field_time = 0;
	uint32_t days = 0;
	bool main_farm = false;
	bool started = false;
	bool working = false;
	TileAnimGroup person;
	std::vector<const TileVariant*> wife;
	std::vector<const TileVariant*> farmer;
};

// A tile type's animations (`tile_anim_facility(tile.type)` not −2).
TileAnims make_tile_anims(const TileDef& tile);

// What a step reads of the town and the building.
struct TileAnimTown {
	bool shown = true;  // [view+0x4F0]: the town shown.
	bool active = true;  // The class's +0x80 (0x54EA20: shown; a workshop's workshop_active and shown).
	bool busy = false;  // The repair yard: the town's [0x81C] or [0x81E] below the ships' count, or [0x820] the convoys'.
	uint16_t employees = 0;  // The repair yard: the town's shipyard's employees (its facility 1's [+0x4], [0x854]).
	uint32_t tick = 0;  // The world's tick ([0x6DE4B4]): the farms' fields grow by it.
};

// One step of a tile type's animations for a cell of one of its buildings in view (its class's +0x38: the default
// 0x552C10, the repair yard's 0x584260, the sawmill's 0x556860, the foundry's 0x555190).
void step_tile_anims(TileAnims& a, uint32_t now, const TileAnimTown& town, uint32_t& rand_state);

// What a slice of one of its buildings shows over it, in order (0x585970, or the class's own drawing: the shipyard's
// 0x584F00, the repair yard's 0x5844E0, the sawmill's 0x5564F0, the foundry's 0x555010): each a page and its frame.
struct TileAnimFrame {
	const TileVariant* page;
	const TileSlice* frame;
};
std::vector<TileAnimFrame> tile_anim_frames(const TileAnims& a);

}  // namespace p3::data
