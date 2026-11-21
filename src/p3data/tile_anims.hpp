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

// The groups a class draws over a slice and how (0x585970, or its own drawing): each group's index and whether
// even hidden; none if its first is hidden (the shipyard's, 0x584F00: its gate's gulls, then its posts' and the
// flying ones even hidden; the flying ones by day only: the viewer has no night).
struct TileAnimDraw {
	size_t group;
	bool even_hidden;
};
std::vector<TileAnimDraw> tile_anim_drawing(uint32_t type, const std::vector<TileAnimGroup>& groups);

}  // namespace p3::data
