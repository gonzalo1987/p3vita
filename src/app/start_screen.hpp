#pragma once

// The game's main menu as the viewer's start screen (the user's choice,
// 2026-11-11; roadmap, phase 2, «Partida nueva desde cero»): the book
// (images/options/Hauptmenu.jpg, in each archive's language) with its entries
// lit under the finger or the cursor (images/options/p_hmenu01.bmp, magenta its
// key; their frames: TEX14 of scripts/textures.ini; where they go: a table in
// each exe, 0x6970D8 in the English one and 0x696978 in the Spanish one) and
// where each entry is (images/options/hmenu_colormap.bmp: 1..8, the rest
// magenta).

#include <SDL.h>

#include <cstdint>
#include <vector>

namespace p3::data {
class CprArchive;
}

namespace p3::app {

// The book's entries, in its order.
enum class StartEntry { Tutorial, Campaign, Single, Multiplayer, Load, Options, Credits, Exit, Count };
constexpr int kStartEntries = int(StartEntry::Count);

class StartScreen {
public:
	// `spanish`: the Spanish archive is mounted (its book and the Spanish exe's places).
	StartScreen(SDL_Renderer* renderer, const p3::data::CprArchive& archive, bool spanish, int screen_width,
		int screen_height);
	~StartScreen();
	StartScreen(const StartScreen&) = delete;
	StartScreen& operator=(const StartScreen&) = delete;

	bool ok() const { return book_ != nullptr; }
	// The book over the whole screen (scaled to its height, in the middle), the
	// entry `lit` (or none: -1) lit.
	void draw(SDL_Renderer* renderer, int lit) const;
	// The entry under a point of the screen, or -1.
	int entry_at(int x, int y) const;

private:
	SDL_Texture* book_ = nullptr;
	SDL_Texture* glow_ = nullptr;
	std::vector<uint8_t> map_;  // 800 · 600: each pixel's entry + 1, 0 for none.
	int map_width_ = 0;
	int map_height_ = 0;
	SDL_Rect frames_[kStartEntries]{};
	SDL_Point places_[kStartEntries]{};
	SDL_Rect dest_{};
	float scale_ = 1.0f;
	int screen_width_ = 0;
	int screen_height_ = 0;
};

}  // namespace p3::app
