#pragma once

// The sea battle on the screen (the battles' piece 7, part 3): what the game's
// town view draws when the battle the player directs is shown without a town
// (the battle's method +0x20, 0x60ED39): the water, the ships with their
// shadows (0x62018B, the town view's ship sprites 0x56B140), a sinking ship
// and the smoke of a burning one (0x62018B), the cannon balls (0x6038C0), the
// guns' smoke and fire, the splashes and the hits (the engine's
// Sailing::BattleEffect), and the clouds of the wind (0x6107B9). The ships'
// labels are handed to the viewer, which has the fonts.

#include <SDL.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/lcr.hpp"
#include "p3data/sailing.hpp"
#include "p3data/savegame.hpp"
#include "p3data/town.hpp"

namespace p3::app {

// A ship's label over it (the manual's figure 2-3: its name, sailors, hull and firepower).
struct BattleLabel {
	int x = 0;  // On the screen: the middle of the label's bottom.
	int y = 0;
	std::string name;
	int sailors = 0;
	int hull = 0;  // Percent.
	int32_t firepower = 0;  // The ship's [+0x120], shown as cannonballs (0x4901C0).
	bool own = false;  // The local player's.
	bool selected = false;  // His and chosen: the blue arrow over it.
};

class BattleView {
public:
	BattleView(SDL_Renderer* renderer, const p3::data::CprArchive& archive, const p3::data::TileSet& tiles);
	~BattleView();
	BattleView(const BattleView&) = delete;
	BattleView& operator=(const BattleView&) = delete;

	// The battle's area (ship_step.cpp: 0x2200 × 0x1E04), on the screen with y · 11 / 17 as the town view.
	static constexpr int kAreaWidth = 0x2200;
	static constexpr int kAreaHeight = 0x1E04;
	static int screen_y(int y) { return y * 11 / 17; }
	static int width() { return kAreaWidth; }
	static int height() { return screen_y(kAreaHeight); }

	// The animations the battle needs now (its ships' looks, each type and hull state with its five sails; the
	// effects; a sinking ship's), added to what is to load; load_step decodes one each call (a frame), so that a
	// new one comes without a jerk. pending(): how many are left.
	void need(const p3::data::SaveGame& save, size_t battle);
	void load_step();
	size_t pending() const { return queue_.size(); }
	// Everything freed (the battle left the screen).
	void clear();

	// The engine's effects of this frame (Sailing::take_battle_effects): those of the battle shown.
	void add_effects(const p3::data::SaveGame& save, size_t battle, const std::vector<p3::data::Sailing::BattleEffect>& effects,
		uint32_t now);

	// The battle on the screen part (0, top) − (view_width, top + view_height), the camera on the area's
	// (camera_x, camera_y) (screen pixels: x and y · 11 / 17). `now`: the viewer's ms. The ships' labels go to
	// `labels`, in screen pixels. `selection`: the places of the ships chosen (a blue arrow over the player's).
	void draw(const p3::data::SaveGame& save, size_t battle, int camera_x, int camera_y, int top, int view_width,
		int view_height, uint32_t now, const std::vector<uint16_t>& selection, std::vector<BattleLabel>& labels);

	// The ship drawn under the screen's (x, y) at the last draw, a finger's width around its picture (the
	// nearest one's anchor if several): its place in the scene ([+0x138]) and whether it is the player's; -1.
	int ship_at(int x, int y, bool& own) const;
	// A marker where an order went (the area's screen pixels), fading for a moment: the touch's answer.
	void mark_order(int x, int y, uint32_t now);
	// The marker arrow over a chosen ship (markerpfeil01.aim, the manual's blue arrow), for the labels.
	SDL_Texture* arrow() const { return arrow_; }

	// Where the camera looks first: the middle of the player's ships in the battle (the area's screen pixels);
	// false while none is placed (before the battle's first step).
	static bool player_ships_centre(const p3::data::SaveGame& save, size_t battle, int player, SDL_Point& centre);
	// The player's ships in the battle that can take orders (not out, sunk or sinking: [+0x12A] & 6), in the
	// scene's order: their places.
	static std::vector<uint16_t> player_ships(const p3::data::SaveGame& save, size_t battle, int player);
	// A ship's place in the area's screen pixels; false if the place has no ship.
	static bool ship_point(const p3::data::SaveGame& save, size_t battle, uint16_t place, SDL_Point& point);
	// The sails the pad's up or down asks for (method +0x20's keys, 0x60F3A6, 0x60F41A): the highest of the
	// selection's sails asked ([+0x148]) plus one, at most 4; or the lowest minus one, at least 0.
	static uint8_t sails_step(const p3::data::SaveGame& save, size_t battle, bool up);

private:
	struct Anim {
		p3::data::LcrAnim lcr;
		SDL_Texture* body = nullptr;  // Kept in textures_ by path.
		SDL_Texture* shadow = nullptr;
	};
	// An effect playing: an animation's direction once at its frames per second, from `start`; at the area's
	// screen pixels (x, y), drifting (vx, vy) pixels a second.
	struct Effect {
		std::string anim;
		int dir = 0;
		float x = 0;
		float y = 0;
		float vx = 0;
		float vy = 0;
		uint32_t start = 0;
		Uint8 alpha = 255;
	};
	// A sinking ship (its [+0x12A] bit 4): once its course is near one of 8 (0x6201A7), its upper and lower
	// parts instead of it (0x62028B), and the burning smoke while the upper one's first 10 frames play.
	struct Sinking {
		bool started = false;
		uint32_t start = 0;
		std::string upper;
		std::string lower;
		uint32_t smoke = 0;  // When it smoked last.
	};
	struct Cloud {
		float x = 0;  // In the area's screen pixels.
		float y = 0;
		int image = 0;
		Uint8 alpha = 0;
	};

	SDL_Texture* texture(const std::string& path, bool magenta);
	void make_water();
	void want(const std::string& path);
	const Anim* anim(const std::string& path) const;
	const Anim* ship_look(int type, int hull, int sails) const;
	void spawn(const std::string& path, int dir, float x, float y, uint32_t now, float vx = 0, float vy = 0,
		Uint8 alpha = 255);
	void smoke(const p3::data::SaveGame& save, size_t battle, float x, float y, uint32_t now);
	void draw_frame(const Anim& a, int dir, int frame, int x, int y, Uint8 alpha, bool shadow) const;
	void draw_clouds(const p3::data::SaveGame& save, size_t battle, int camera_x, int camera_y, int top, int view_width,
		int view_height, uint32_t now);
	uint32_t next_random();

	SDL_Renderer* renderer_;
	const p3::data::CprArchive& archive_;
	const p3::data::TileSet& tiles_;
	std::map<std::string, SDL_Texture*> textures_;
	std::array<SDL_Texture*, 9> water_{};  // The water's 9 frames, each a block of 102 × 66 that tiles.
	std::map<std::string, std::unique_ptr<Anim>> anims_;  // By path; an entry without its texture didn't load.
	std::vector<std::string> queue_;  // Animations to load.
	struct Hit {
		int place = 0;
		bool own = false;
		SDL_Rect rect{};  // On the screen.
		SDL_Point anchor{};
	};
	std::vector<Hit> hits_;  // The ships drawn at the last draw.
	SDL_Point order_mark_{};  // mark_order's point (the area's screen pixels) and when.
	uint32_t order_time_ = 0;
	bool order_marked_ = false;
	SDL_Texture* ball_ = nullptr;
	SDL_Texture* arrow_ = nullptr;  // The marker arrow over a chosen ship (markerpfeil01.aim), drawn blue.
	std::array<SDL_Texture*, 6> cloud_images_{};
	std::vector<Cloud> clouds_;
	uint32_t clouds_time_ = 0;
	std::vector<Effect> effects_;
	std::map<size_t, Sinking> sinking_;  // By the scene's ship.
	std::map<size_t, uint32_t> burning_;  // By the scene's ship: when it smokes next (0x620CDE).
	uint32_t random_ = 12345;  // The viewer's own: the game's sprites take the world's rand() (left out of the engine).
};

}  // namespace p3::app
