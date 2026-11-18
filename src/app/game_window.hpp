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
struct Image;
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

// The tavern's people with a portrait on their pages (the tavern window's 0x5CDC60 and 0x5CF138, by the person), and
// the church's priest (0x5C98FA: TEX31062, or TEX31063 with the altar when the town's poor are fed past level 4).
enum class Portrait { None, Sailors, Captain, Informer, Traveller, WeaponsDealer, Burglar, Pirate, Client, Priest, PriestAltar };

// What some interiors' animations hang on in the game, given by the viewer each frame with set_interior().
struct InteriorState {
	uint32_t town = 0;  // The town the building is in.
	uint32_t tick = 0;  // The world's tick ([0x6DE4B4], 256 a day).
	// The church: 0x4FE360 on the town's church ([+0x794]: how well its poor are fed, 0..5); its animation k plays while
	// this is over k (0x5A56A5 when it opens, its window's update 0x5C9652..0x5C97F1 each frame).
	int church_level = 0;
	// The town gate (the sieges' window's update 0x5EF7A0): the siege going on (0x633AE0, the army not retreating: its
	// [+0x3A] at 0) or the window's [+0x94] (set as it queues 0x90, 0x5F31A8 / 0x5F3424): the gate closed with its
	// armoured captain (3) alone; otherwise the guard (0) at the open gate with his gestures. (No view of the viewer
	// opens the town gate yet: the sieges' windows, group 5, are to set it.)
	bool gate_closed = false;
};

class GameWindowArt {
public:
	GameWindowArt(SDL_Renderer* renderer, const p3::data::CprArchive& archive);
	~GameWindowArt();
	GameWindowArt(const GameWindowArt&) = delete;
	GameWindowArt& operator=(const GameWindowArt&) = delete;

	// The interior k of animation.ini (kNoInterior: none, the parchment alone), its picture and animations read now if
	// it isn't the one loaded (one at a time: the other is freed), and the parchment and buttons the first time. Called
	// each frame while a building is open, with what its animations hang on: on entering (the first call after
	// leave(), or another interior) it starts the animations its building's side menu starts when it opens (opened();
	// the game keeps each interior's animations going between visits, as here while it stays loaded).
	void set_interior(int k, uint32_t now, const InteriorState& state = InteriorState());
	// The building left (its side menu closed): what its menu stops then (the bath house's 1..4, 0x5A0567; and the
	// guests in its bath kept for a day, 0x5A05A1).
	void leave();
	// The people in the bath house's bath when the player last entered it there: its side menu's [+0xAC + town] (bit 0
	// the player, bits 1..4 the town's councillors 0..3, each there with a chance of 7 in 100, 0x5A0907..0x5A09F7),
	// rolled on entering once the day after the last visit there has gone ([+0xD4 + 4 · town], 0x5A08F8). (The bath's
	// side menu has a button for each: the councillors to bribe.)
	uint8_t bath_guests(uint32_t town) const { return town < kTownSlots ? bath_mask_[town] : 1; }
	// Starts drawing the window into its texture at the game's size (kWindowWidth × kWindowHeight, the interior's
	// top left at (kFrameLeft, kFrameTop)): the picture, the parchment over it if `parchment` (at alpha 160 over a
	// picture, whole without one) and the frame; what is drawn until end() goes on it, in those coordinates. Without
	// the texture, the renderer's viewport and scale do the same.
	// The parchment from the picture's line `parchment_top` down (the interior's [+0xEC], each page's: the 160 lines
	// above it fading in, as 0x465DC0..0x465EAD), none if negative. The animations move on to `now` (ms) first.
	// Without `framed`: none of that (no picture, parchment nor frame), the texture left clear for a dialog window over
	// the town (draw_dialog).
	void begin(const SDL_Rect& on_screen, int parchment_top, uint32_t now, bool framed = true);
	// A window of its own size, `w` × `h` (the trade route stop's wares window: 428 × 564), drawn into a texture of that
	// size in its own coordinates and put on the screen in `on_screen` by end(), smoothly scaled (without the texture,
	// the renderer's viewport and scale). Nothing drawn on it yet.
	void begin_sized(const SDL_Rect& on_screen, int w, int h);
	// The window put on the screen in `on_screen` (begin's), smoothly scaled.
	void end();
	// A pill of the parchments, `r`'s width, its own height (18) centred in `r`.
	void draw_pill(const SDL_Rect& r, PillState state) const;
	// The pill for a label `label_w` wide on a button `r`: the button's (the game's style's) width, or, when the label
	// leaves it less than 6 each side, wider from its middle so that its rounded ends stay clear (a round 16 button stays)
	// (the user, 18-11-2026: the game's widths left «Máx.», «Deshaz»... touching the ends; adapted in every screen).
	SDL_Rect pill_for_label(const SDL_Rect& r, int label_w) const;
	int pill_end() const;  // A rounded end's width (bttn_standard.tga's first frame, 8).
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
	// The game's dialog windows over the town (their base class: 0x41C5A0 lays out the tiles, 0x41CAA0 draws them;
	// parchment.ini [DialogBG0]: TEX16001, TileSize 32): `cols` × `rows` tiles of 32 at (x, y). With `rolls` (the
	// third argument 1: the arsenal, another merchant's office, the selection lists) the rolls of paper at the top and
	// bottom (frames 4..8 and 25..29) and their ends (frames 0..3, 6 × 32, at the window's corners: the window 12 wider
	// than its tiles, which start 6 in); without them the plain edges (9..13, 20..24). Corners fixed, each edge and
	// middle tile one of three or nine at random (rand() % 3: the top's 5 + r, the bottom's 0x1A + r, the left's 0xE + r,
	// the right's 0x11 + r; the middle's 0x1E + rand() % 9), rolled when the window first gets its size; `key` keeps
	// each window's tiles, as each game window keeps its own: a new size with as many columns keeps the rows above the
	// last and the last row (0x41C420), rolling the new middle ones.
	void draw_dialog(int x, int y, int cols, int rows, bool rolls, int key);
	// The game's font 1 (ttffonts.ini: Tiepolo bold, 13 × 15), the one its pages and buttons are laid out for; null
	// without it. The fonts load with the common pieces the first time one is asked for (a page laid out before any
	// window is drawn: the ship menu's convoy question measures its buttons with it).
	TextRenderer* page_text() {
		ensure_common();
		return page_.get();
	}
	// The titles' font (the game's font 3); null without it.
	TextRenderer* title_text() {
		ensure_common();
		return title_.get();
	}
	// The tables' heads' font (the game's font 0, Tiepolo black); null without it.
	TextRenderer* head_text() {
		ensure_common();
		return head_.get();
	}
	// The game's font 2 (Tiepolo black, 16 × 17: the ship menu's heads, ShipInfoHeadFontID); null without it.
	TextRenderer* info_text() {
		ensure_common();
		return info_.get();
	}

	// The interior's animations ([Anim k] of animation.ini: AnimN the TEX with its frames, AnimNX/Y its place on the
	// picture, AnimNTime the loop's ms, AnimNFrames; as many as its Animations, 0x46563E: the office's lists five but
	// says 2), as the interior view runs them: 0x466490 starts one (back and forth or not), 0x466610 stops it at its
	// loop's end, 0x4666B0 stops it, 0x4667A0 starts `next` when `k` ends; 0x466800 moves them on each frame.
	void start_animation(int k, bool back_and_forth, uint32_t now);
	void stop_at_loop_end(int k);
	void stop_animation(int k);
	void chain_animation(int k, int next);
	bool animation_on(int k) const;

	// A person's portrait on a page of the tavern (0x5CDC60): its picture (280 × 160, TEX31007..31011, 31099, 31100) at
	// (w/2 − 140, 55), the talking face over it (the object *0x6CC7F4: animation.ini [GebaeudeAni], its start once and
	// then its loop, a frame each FrameTime ms; read a frame or two at a time, as 0x46B110 reads one) and the torn paper
	// frame round it (0x46B960: TEX31013 and TEX31014). In the window's coordinates, between begin() and end().
	// The priest's faces (the church window's update 0x5C94F0): his talking loop played back and forth (6, with the
	// altar 8), his thanks (7, 9) once when asked, from the loop's first frame, then the loop again.
	void draw_portrait(Portrait p, uint32_t now);
	// The church window's [+0x1D5F]: the priest thanks the player (set by a donation of goods or for the extension,
	// 0x5CB24D and 0x5CB839; cleared when the window opens, 0x5C937C, and at his thanks' last frame, 0x5C9621).
	void priest_thanks(bool on) { thanks_ = on; }
	// The picture TEX`tex` of textures.ini (its frame `frame`) at (x, y), read the first time; its size. With `width`,
	// only that much of the frame's left part (the half cannonball, 0x490428); with `alpha`, faded (the shipyard book's
	// ships, 0x5F8910); with `tint` (0xAARRGGBB), its colours multiplied by it (the game's constant colour, 0x4BB870: the
	// ship list's convoy flags, 0x490F80).
	SDL_Point draw_tex(uint32_t tex, int frame, int x, int y, int width = 0, uint8_t alpha = 255, uint32_t tint = 0xFFFFFFFF);
	// Only the part `part` of the frame (in the frame's own coordinates) at (x, y), tinted: the game's blit 0x4BB330 with
	// a source offset (the ship menu's barrel bands, 0x491A3D..0x491B36).
	void draw_tex_part(uint32_t tex, int frame, const SDL_Rect& part, int x, int y, uint32_t tint = 0xFFFFFFFF);
	SDL_Point tex_size(uint32_t tex, int frame);

private:
	struct Face {
		int animation = -1;  // [GebaeudeAni]'s, 0..18.
		std::vector<SDL_Texture*> frames;  // Read so far.
		int count = 0;
		int frame = 0;  // [+0x1F]
		uint32_t next = 0;  // [+0x8]: when the next frame shows.
		bool forward = true;  // [+0x3A]: back and forth (0x46B530's third argument), its way now.
	};
	void set_face(int animation, uint32_t now);
	void free_face();
	// A picture of textures.ini with its frames (Count, OffsetNSize): one texture, or a texture a frame when the
	// picture is wider or higher than a texture may be (the Vita's 4096: the strips of the lender, the market hall, the
	// bath house and the office, the guild's fire, the shipyard's water).
	struct Strip {
		SDL_Texture* texture = nullptr;
		std::vector<SDL_Rect> frames;
		std::vector<SDL_Texture*> pieces;  // Each frame's own texture when split.
		// textures.ini's Blendmode=multiply (the telescope's pirate TEX20028, the ship menu's backpics): drawn multiplying
		// what is under it, its constant colour's alpha the strength (dst · (a · src + 1 − a): SDL_BLENDMODE_MUL with the
		// colour scaled by a).
		bool multiply = false;
	};
	bool load_strip(uint32_t tex, Strip& strip);
	static void free_strip(Strip& strip);
	// Its frame `frame` at (x, y) (with `width`, only that much of its left part; with `alpha`, faded; with `tint`,
	// coloured); the size drawn.
	SDL_Point draw_strip(const Strip& strip, int frame, int x, int y, int width = 0, uint8_t alpha = 255,
		uint32_t tint = 0xFFFFFFFF) const;
	// A multiply strip's part drawn (Strip::multiply).
	void copy_multiplied(SDL_Texture* t, const SDL_Rect& src, const SDL_Rect& dst, uint8_t alpha, uint32_t tint) const;
	struct Animation {
		Strip strip;
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

	// The picture TEX`tex` of textures.ini as the game draws it: its file read, opaque when UseAlpha=false (the game
	// leaves the file's alpha out then: the palette AIMs' is 0 throughout), its ColorKey clear.
	bool read_tex_image(uint32_t tex, p3::data::Image& image);
	SDL_Texture* load(uint32_t tex, SDL_Rect* frames, int count);
	void free_animations();
	void ensure_common();  // textures.ini, animation.ini, the parchment, buttons and fonts, the first time.
	void load_interior();
	void opened(uint32_t now);
	void tick(uint32_t now);
	void town_hall_tick(uint32_t now);
	void tavern_tick(uint32_t now);
	void guild_tick(uint32_t now);
	void pair_tick(uint32_t now, bool always_roll);
	void market_hall_tick(uint32_t now);
	void church_tick(uint32_t now);
	void bath_tick(uint32_t now);
	void barn_tick(uint32_t now);
	void gate_tick(uint32_t now);
	void update_animations(uint32_t now);
	// The side menus' and windows' rand() (0x639C03, 0..32767; their chances rand() / 32768 with 0x66CB34) from a
	// generator of the viewer's own (the game's uses the CRT's, the simulation's; the viewer's engine mustn't see these).
	int menu_rand();
	double chance();

	void draw_frame() const;
	// A dialog window's tiles (draw_dialog): the frames of TEX16001, by row.
	struct DialogTiles {
		int cols = 0;
		int rows = 0;
		bool rolls = true;
		std::vector<uint8_t> tiles;
	};
	void roll_dialog_row(DialogTiles& d, int row);
	std::vector<std::pair<int, DialogTiles>> dialogs_;

	SDL_Renderer* renderer_;
	const p3::data::CprArchive& archive_;
	std::string textures_ini_;
	std::string animation_ini_;
	bool common_ = false;  // The parchment and the buttons read.
	int interior_ = kNoInterior;
	SDL_Texture* picture_ = nullptr;
	SDL_Rect picture_frame_{};
	std::vector<Animation> animations_;
	bool inside_ = false;  // The building entered (set_interior since the last leave()).
	InteriorState state_;
	// When the next gesture comes: the town hall's side menu's [+0x8F0] (its animation 4), the barn's window's
	// [+0x2E20] and the town gate's window's [+0x1618].
	uint32_t timer_ = 0;
	static constexpr uint32_t kTownSlots = 40;  // The bath house's side menu's arrays (0x59FCCF).
	uint8_t bath_mask_[kTownSlots];
	uint32_t bath_until_[kTownSlots];
	int bath_count_ = 1;  // The menu's [+0x6A2]: how many are in the bath (the player and the councillors there).
	uint32_t spit_at_ = 0;  // The tavern window's [+0x1BE0]: when the sailor may spit again.
	Portrait portrait_ = Portrait::None;
	SDL_Texture* portrait_picture_ = nullptr;
	SDL_Rect portrait_frame_{};
	SDL_Texture* torn_across_ = nullptr;  // TEX31013: the frame's top (284 × 8) and bottom (284 × 12).
	SDL_Rect torn_across_frames_[2] = {};
	SDL_Texture* torn_down_ = nullptr;  // TEX31014: its left (7 × 144) and right (12 × 144).
	SDL_Rect torn_down_frames_[2] = {};
	Face face_;
	bool thanks_ = false;  // priest_thanks().
	using Tex = Strip;
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
	SDL_Texture* sized_target_ = nullptr;  // begin_sized's, of its size.
	int sized_w_ = 0;
	int sized_h_ = 0;
	SDL_Texture* drawn_target_ = nullptr;  // The one being drawn into.
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
	std::unique_ptr<TextRenderer> head_;
	std::unique_ptr<TextRenderer> info_;
};

}  // namespace p3::app
