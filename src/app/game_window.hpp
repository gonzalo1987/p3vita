#pragma once

// The buildings' windows as the game draws them. The interior view (vtable 0x66E688, *0x6CC7E0; animation.ini read
// at 0x4653A0, drawn at 0x465BA0): the picture of the building's interior k ([Anim k] BGID, 425 × 510) centred in the
// town view (0x466320), alone when the building is entered (0x466430) and with the parchment (BackTexID, TEX31105)
// over it at alpha 160 (0xA0FFFFFF) while a page is open (0x4665D0), inside its wooden frame (TEX31002: the corners,
// the top and bottom 17 times, the sides 34 times) and hung from two chains (Kette.aim); the building's window (the
// town hall's *0x6E558C, 425 × 510) writes its page on it. The viewer draws the whole window at the game's size into a
// texture of its own (begin, end) and fits it to the room under the top bar (the chains left out: no room).
// The parchments' buttons: buttons.ini's pill (TEX16032: its left end, middle and right end, in the rows standard,
// pressed and disabled; black text, highlighted blue) and its close button (TEX16023).

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace p3::data {
class CprArchive;
}

namespace p3::app {

class TextRenderer;

constexpr int kInteriorWidth = 425;
constexpr int kInteriorHeight = 510;
// The frame around the picture (its corners: 12 × 12, 14 × 12, 14 × 15, 12 × 15), and the whole window.
constexpr int kFrameLeft = 12;
constexpr int kFrameTop = 12;
constexpr int kWindowWidth = kInteriorWidth + kFrameLeft + 14;
constexpr int kWindowHeight = kInteriorHeight + kFrameTop + 15;
constexpr Uint8 kParchmentOverPicture = 160;
// The interiors of animation.ini, as the side menus pick them (0x466430's argument).
namespace interiors {
enum Interior {
	kNoInterior = -1,
	kTavernInterior = 0,
	kOfficeInterior = 1,
	kShipyardInterior = 2,
	kChurchInterior = 3,
	kLenderInterior = 4,
	kMarketHallInterior = 5,
	kGuildInterior = 6,
	kTownHallInterior = 7,
	kBathHouseInterior = 8,
	kSmithyInterior = 9,
	kTownGateInterior = 10,
	kBarnInterior = 11,
};
}  // namespace interiors
using namespace interiors;

// Buttons' texts (buttons.ini TextCL and TextHL: black, and blue for the highlighted one).
constexpr SDL_Color kButtonInk = {0, 0, 0, 255};
constexpr SDL_Color kButtonHighlight = {0x0C, 0x2A, 0xE0, 255};

enum class PillState { Standard, Pressed, Disabled };

// The tavern's people with a portrait on their pages (the tavern window's 0x5CDC60 and 0x5CF138, by the person).
enum class Portrait { None, Sailors, Captain, Informer, Traveller, WeaponsDealer, Burglar, Pirate, Client };

class GameWindowArt {
public:
	GameWindowArt(SDL_Renderer* renderer, const p3::data::CprArchive& archive);
	~GameWindowArt();
	GameWindowArt(const GameWindowArt&) = delete;
	GameWindowArt& operator=(const GameWindowArt&) = delete;

	// The interior k of animation.ini (kNoInterior: none, the parchment alone), its picture and animations read now if
	// it isn't the one loaded (one at a time: the other is freed), and the parchment and buttons the first time; a new
	// one starts the animations its building's side menu starts when it opens (opened()).
	void set_interior(int k, uint32_t now);
	// Starts drawing the window into its texture at the game's size (kWindowWidth × kWindowHeight, the interior's
	// top left at (kFrameLeft, kFrameTop)): the picture, the parchment over it if `parchment` (at alpha 160 over a
	// picture, whole without one) and the frame; what is drawn until end() goes on it, in those coordinates. Without
	// the texture, the renderer's viewport and scale do the same.
	// The parchment from the picture's line `parchment_top` down (the interior's [+0xEC], each page's: the 160 lines
	// above it fading in, as 0x465DC0..0x465EAD), none if negative. The animations move on to `now` (ms) first.
	void begin(const SDL_Rect& on_screen, int parchment_top, uint32_t now);
	// The window put on the screen in `on_screen` (begin's), smoothly scaled.
	void end();
	// A pill of the parchments, `r`'s width, its own height (18) centred in `r`.
	void draw_pill(const SDL_Rect& r, PillState state) const;
	// The close button (32 × 18), centred in `r`.
	void draw_close(const SDL_Rect& r, bool pressed) const;
	// A page's title as 0x420C70 draws it on a window of width `w` at (x, y): the banner (TEX31006, 292 × 50) centred
	// at its top (y + 4), and the title over it in the game's font 3 (ttffonts.ini: Elgreco, 22 × 20) centred 9 px
	// right of the middle at y + 12, black over its light shadow (0xEFD7C0) a pixel lower and to the right. Without
	// that font, in `fallback`.
	void draw_title(TextRenderer& fallback, int x, int y, int w, const std::string& title) const;
	// The sheet of paper some pages write on (0x5E8E90): `cols` × `rows` cells of 32 at (x, y), the dialog paper
	// (TEX16001: its top and bottom edges, sides and middle by the cell's column and row modulo 3) with the folded
	// corners (TEX16058) and the seal with the cog (TEX16059) at its lower right; the room for the text inside it.
	SDL_Rect draw_paper(int x, int y, int cols, int rows) const;
	// The game's font 1 (ttffonts.ini: Tiepolo bold, 13 × 15), the one its pages and buttons are laid out for; null
	// without it.
	TextRenderer* page_text() const { return page_.get(); }
	// The titles' font (the game's font 3); null without it.
	TextRenderer* title_text() const { return title_.get(); }
	// The side menus' buttons' font (the game's font 2); null without it.
	TextRenderer* menu_text() const { return menu_.get(); }

	// The interior's animations ([Anim k] of animation.ini: AnimN the TEX with its frames, AnimNX/Y its place on the
	// picture, AnimNTime the loop's ms, AnimNFrames), as the interior view runs them: 0x466490 starts one (back and
	// forth or not), 0x466610 stops it at its loop's end, 0x4666B0 stops it, 0x4667A0 starts `next` when `k` ends;
	// 0x466800 moves them on each frame.
	void start_animation(int k, bool back_and_forth, uint32_t now);
	void stop_at_loop_end(int k);
	void stop_animation(int k);
	void chain_animation(int k, int next);
	bool animation_on(int k) const;

	// A person's portrait on a page of the tavern (0x5CDC60): its picture (280 × 160, TEX31007..31011, 31099, 31100) at
	// (w/2 − 140, 55), the talking face over it (the object *0x6CC7F4: animation.ini [GebaeudeAni], its start once and
	// then its loop, a frame each FrameTime ms; read a frame or two at a time, as 0x46B110 reads one) and the torn paper
	// frame round it (0x46B960: TEX31013 and TEX31014). In the window's coordinates, between begin() and end().
	void draw_portrait(Portrait p, uint32_t now);
	// The picture TEX`tex` of textures.ini (its frame `frame`) at (x, y), read the first time; its size.
	SDL_Point draw_tex(uint32_t tex, int frame, int x, int y);
	SDL_Point tex_size(uint32_t tex, int frame);

private:
	struct Face {
		int animation = -1;  // [GebaeudeAni]'s, 0..18.
		std::vector<SDL_Texture*> frames;  // Read so far.
		int count = 0;
		int frame = 0;  // [+0x1F]
		uint32_t next = 0;  // [+0x8]: when the next frame shows.
	};
	void set_face(int animation, uint32_t now);
	void free_face();
	struct Animation {
		SDL_Texture* texture = nullptr;
		std::vector<SDL_Rect> frames;
		int x = 0;
		int y = 0;
		int time = 0;
		int count = 0;
		bool on = false;  // [+0xD0]
		bool stop_at_end = false;  // [+0xD4]
		int next = -1;  // [+0xD8]
		bool back_and_forth = false;  // [+0xDC]
		bool forward = true;  // [+0xE0]
		int frame = 0;  // [+0xBC]
		uint32_t start = 0;  // [+0xC4]
	};

	SDL_Texture* load(uint32_t tex, SDL_Rect* frames, int count);
	SDL_Texture* load_frames(uint32_t tex, std::vector<SDL_Rect>& frames);
	void free_animations();
	void ensure_common();  // textures.ini, animation.ini, the parchment, buttons and fonts, the first time.
	void load_interior(uint32_t now);
	void opened(uint32_t now);
	void tick(uint32_t now);
	void update_animations(uint32_t now);
	// The side menus' chances: rand() / 32768 as the game's (0x639C03 and 0x66CB34), from a generator of the viewer's
	// own (the game's uses the CRT's, the simulation's; the viewer's engine mustn't see these).
	double chance();

	void draw_frame() const;

	SDL_Renderer* renderer_;
	const p3::data::CprArchive& archive_;
	std::string textures_ini_;
	std::string animation_ini_;
	bool common_ = false;  // The parchment and the buttons read.
	int interior_ = kNoInterior;
	SDL_Texture* picture_ = nullptr;
	SDL_Rect picture_frame_{};
	std::vector<Animation> animations_;
	uint32_t timer_ = 0;  // The town hall's side menu's [+0x8F0]: when its animation 4 plays again.
	Portrait portrait_ = Portrait::None;
	SDL_Texture* portrait_picture_ = nullptr;
	SDL_Rect portrait_frame_{};
	SDL_Texture* torn_across_ = nullptr;  // TEX31013: the frame's top (284 × 8) and bottom (284 × 12).
	SDL_Rect torn_across_frames_[2] = {};
	SDL_Texture* torn_down_ = nullptr;  // TEX31014: its left (7 × 144) and right (12 × 144).
	SDL_Rect torn_down_frames_[2] = {};
	Face face_;
	struct Tex {
		SDL_Texture* texture = nullptr;
		std::vector<SDL_Rect> frames;
	};
	std::vector<std::pair<uint32_t, Tex>> texs_;  // draw_tex's.
	const Tex& tex(uint32_t id);
	uint32_t seed_ = 1;
	SDL_Texture* parchment_ = nullptr;
	SDL_Rect parchment_frame_{};
	SDL_Texture* pill_ = nullptr;
	SDL_Rect pill_frames_[9] = {};
	SDL_Texture* close_ = nullptr;
	SDL_Rect close_frames_[3] = {};
	SDL_Texture* frame_ = nullptr;
	SDL_Rect frame_frames_[8] = {};
	SDL_Texture* target_ = nullptr;
	bool target_failed_ = false;
	SDL_Rect on_screen_{};
	bool drawing_into_target_ = false;
	SDL_Texture* banner_ = nullptr;
	SDL_Rect banner_frame_{};
	SDL_Texture* paper_ = nullptr;
	SDL_Rect paper_frames_[39] = {};
	SDL_Texture* corners_ = nullptr;
	SDL_Rect corner_frames_[4] = {};
	SDL_Texture* seal_ = nullptr;
	SDL_Rect seal_frame_{};
	std::unique_ptr<TextRenderer> title_;
	std::unique_ptr<TextRenderer> page_;
	std::unique_ptr<TextRenderer> menu_;
};

}  // namespace p3::app
