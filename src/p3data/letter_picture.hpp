#pragma once

#include "p3data/game_memory.hpp"
#include "p3data/image.hpp"

namespace p3::data {

class CprArchive;

// The picture of the letters 0x46 (the pieces of a map) and 0x6E (a pirate
// nest to destroy), as 0x51AC60 makes it for the letter window
// (docs/formats/mail.md §4): the sea map around a place in sepia, cut in 3 × 3
// pieces of a parchment's size; the pieces of u16 [+0xA] (bit 3·row + column)
// under the parchment (TEX21004), the others transparent, and a red cross
// (TEX30033's frame 7) on the place, u16 [+0xC], [+0xE] in the sea map's
// pixels. The pirate nest's letter has all nine pieces (0xFFFF).
constexpr char kLetterMapPiece[] = "images/Expedition/gef\xE4rbte_Karte.tga";  // TEX21004, 100 × 100
constexpr char kLetterMapCross[] = "images/Animationen_Seekarte/KreuzTP.bmp";  // TEX30033, 8 frames of 18 × 18

// From the decoded images: the sea map (kSeaMapImage, 2400 × 1800), the
// parchment (kLetterMapPiece) and the cross's strip (kLetterMapCross). The
// picture is 3 × the parchment's size (300 × 300), RGBA; empty if an image is
// too small for what the game reads of it.
Image letter_map_picture(const GameMemory<0x10>& letter, const Image& sea_map, const Image& piece, const Image& cross);

// The same with the images read from the game's archive; false if one is
// missing or can't be decoded.
bool letter_map_picture(const CprArchive& archive, const GameMemory<0x10>& letter, Image& out);

}  // namespace p3::data
