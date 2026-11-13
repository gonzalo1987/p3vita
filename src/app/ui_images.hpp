#pragma once

// The game's interface pictures from the archive, for the viewer's own windows
// (the start screen, the new game window): files, decoded images as textures
// and the frames of scripts/textures.ini.

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "p3data/image.hpp"

namespace p3::data {
class CprArchive;
}

namespace p3::app {

// A file of the archive; false if it isn't there.
bool read_cpr_file(const p3::data::CprArchive& archive, const char* path, std::vector<uint8_t>& out);
// A picture of the archive (BMP, TGA, JPG, PNG or the engine's AIM) decoded; false if it isn't there or can't be read.
bool read_cpr_image(const p3::data::CprArchive& archive, const char* path, p3::data::Image& out);
// The picture as a texture with its alpha blended, or nullptr.
SDL_Texture* make_texture(SDL_Renderer* renderer, const p3::data::Image& image);
// The frames «OffsetNSizeK=x y w h» of a section «[TEXn]» of textures.ini (the
// lines with a «;» before are left out); the others stay as they were.
void read_texture_frames(const std::vector<uint8_t>& ini, const char* section, SDL_Rect* frames, int count);
// The value of «key=» in the section «[section]» of an ini file's text (trimmed, without a «;» comment), or "".
std::string ini_value(const std::string& ini, const std::string& section, const char* key);

}  // namespace p3::app
