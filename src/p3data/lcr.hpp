#pragma once

// The town view's animations (iso/lcr/*.lcr, docs/formats/lcr.md): what the
// game calls its creatures, from the town's people to the ships of a battle
// and their smoke. Each file names its images (AIM or BMP in the CPR) and
// gives, for each direction and frame, the rectangle to draw from them and
// the point of it that stands on the creature's place. Read: the ships'
// looks (class 0x0D) and the effects (class 0x12).

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
// (shadow_x, shadow_y) of the main image's coordinates. The vertical strips
// of 17 pixels the town view sorts by depth are left out.
struct LcrFrame {
	int anchor_x = 0;
	int anchor_y = 0;
	LcrRect body;
	bool shadowed = false;
	LcrRect shadow;
	int shadow_x = 0;
	int shadow_y = 0;
};

struct LcrAnim {
	uint16_t id = 0;
	uint16_t kind = 0;  // The class: 0x0D a ship's look, 0x12 an effect...
	uint32_t key = 0;  // 0xFFFF00FF: its images' magenta is transparent; 0xFF000000: black; 0: their own alpha.
	uint32_t fps = 0;  // Frames per second (0: one frame).
	// The six words after the head: for an effect, [1, the number the game asks it by with its class, 1, 0,
	// its last frame, 2] (0x12/0x24 the burning ship's smoke, 0x12/4..7 a sinking ship's upper part...).
	uint32_t words[6] = {};
	std::string shadow_image;  // CPR paths, with '/' separators; no shadow: empty.
	std::string image;
	std::vector<std::vector<LcrFrame>> directions;  // Each direction's frames.
};

// False (and why in `error`) if the bytes aren't an animation of the layout
// read here (the ships' and the effects'; the town people's with several
// sequences aren't).
bool parse_lcr(const uint8_t* data, size_t size, LcrAnim& out, std::string* error = nullptr);

// A ship's look (iso/lcr/<type>_schadenHH_segelSS.lcr): one per type 0..3
// (snaikka, craier, cog, hulk), hull state 0 (whole) .. 3 and sails 0
// (furled) .. 4; 32 directions of one frame, the first north and then
// clockwise (the game's frame of a direction d in 256ths is (d · 32 + 128)
// >> 8 & 31, 0x56C99F). Its path in the CPR.
std::string lcr_ship_path(int type, int hull, int sails);

}  // namespace p3::data
