#pragma once

// The town view's animations (iso/lcr/*.lcr, docs/formats/lcr.md): what the
// game calls its creatures, from the town's people to the ships of a battle
// and their smoke. Each file names its images (AIM or BMP in the CPR) and
// gives, for each direction and frame, the rectangle to draw from them and
// the point of it that stands on the creature's place. Read: the ships'
// looks (class 0x0D), the effects (class 0x12) and the ships' pennant (class
// 0x1E, another layout).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace p3::data {

// A rectangle in one of the animation's images, in pixels.
struct LcrRect {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};

// A frame: its picture in the main image, the point of it on the creature's
// place (its anchor, in the main image's coordinates) and, with a shadow
// image, its shadow: a rectangle of that image whose corner goes at
// (shadow_x, shadow_y) of the main image's coordinates; and the vertical
// strips (at most 17 pixels wide, in the main image) by which the town view
// sorts it among the buildings: each strip goes with the cell its bottom
// middle stands on (0x5596C0); none in the files without them (the birds', the snow's...).
struct LcrFrame {
	int anchor_x = 0;
	int anchor_y = 0;
	LcrRect body;
	bool shadowed = false;
	LcrRect shadow;
	int shadow_x = 0;
	int shadow_y = 0;
	std::vector<LcrRect> strips;
};

// A sequence of the town people's animations (their classes' head, 0x558770): its frames first..last of each
// direction and the number the game asks it by (2 walking; 3, 4, 5... standing and their gestures; 6 running).
struct LcrSequence {
	int first = 0;
	int last = 0;
	int id = 0;
};

// A point in an animation's main image.
struct LcrPoint {
	int x = 0;
	int y = 0;
};

// A section of a ship's look of hull state 0 and sails 0 (0x56AFA0, each read into a page by 0x5852A0): its tag (0
// the chosen ships' mark *0x6E1920, 1 the shipyard's icon *0x6E18F0, 2 the auction's *0x6E1908, 3 the trade route's
// shield *0x6E18C0, 4 the pennant's points, 5 the white flag *0x6E18D8), its parameters (the mark's: how far it
// bobs and every how many ms), its frames (x, y, w, h in its image) and its image (empty: the look's own) and key.
struct LcrPage {
	uint32_t tag = 0;
	std::vector<uint32_t> params;
	std::vector<LcrRect> frames;
	std::string image;
	uint32_t key = 0;
};

struct LcrAnim {
	uint16_t id = 0;
	uint16_t kind = 0;  // The class: 0x0D a ship's look, 0x12 an effect...
	uint32_t key = 0;  // 0xFFFF00FF: its images' magenta is transparent; 0xFF000000: black; 0: their own alpha.
	// The head's u32 at 0x0C (the look's [+0x4], 0x558770): an effect's size among the looks of its number, which its
	// start asks by (0x57A150: the waves' welle_c 1, welle_b 2, welle_a 3; the ice floes' the same).
	uint32_t variant = 0;
	uint32_t fps = 0;  // Frames per second (0: one frame).
	uint32_t speed = 0;  // The head's u32 at 0x10 (the look's [+0xC]): a walker's speed, 140 the town's people.
	// The six words after the head: for an effect, [1, the number the game asks it by with its class, 1, 0,
	// its last frame, 2] (0x12/0x24 the burning ship's smoke, 0x12/4..7 a sinking ship's upper part...).
	uint32_t words[6] = {};
	std::vector<uint32_t> params;  // The head's parameters (an effect's number, a seller's 100 and 50...).
	std::vector<LcrSequence> sequences;  // Its sequences (the people's walking, standing, gestures...).
	std::string shadow_image;  // CPR paths, with '/' separators; no shadow: empty.
	std::string image;
	std::vector<std::vector<LcrFrame>> directions;  // Each direction's frames.
	// A ship's look of hull state 0 and sails 0: where its pennant goes in each direction, in the main image
	// (the game takes it less that direction's anchor for every look of the type, 0x56B052); empty otherwise.
	std::vector<LcrPoint> pennant;
	// Its sections (the same look; only `schnigge_schaden00_segel00.lcr` has more than the pennant's).
	std::vector<LcrPage> pages;
};

// False (and why in `error`) if the bytes aren't an animation of the layout
// read here: the ships', the effects', the pennant's and the town people's
// (their sequences after the head's byte at 0x1C, then the same frames).
bool parse_lcr(const uint8_t* data, size_t size, LcrAnim& out, std::string* error = nullptr);

// A ship's look (iso/lcr/<type>_schadenHH_segelSS.lcr): one per type 0..3
// (snaikka, craier, cog, hulk), hull state 0 (whole) .. 3 and sails 0
// (furled) .. 4; 32 directions of one frame, the first north and then
// clockwise (the game's frame of a direction d in 256ths is (d · 32 + 128)
// >> 8 & 31, 0x56C99F). Its path in the CPR.
std::string lcr_ship_path(int type, int hull, int sails);

}  // namespace p3::data
