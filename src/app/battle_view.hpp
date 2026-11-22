#pragma once

// The battle on the screen (the battles' piece 7, part 3): what the game's
// town view draws when the battle the player directs is shown (the battle's
// method +0x20, 0x60ED39): the water with its ripples (the view's [+0x5A0]),
// the ships with their shadows (0x62018B, the town view's ship sprites
// 0x56B140), a sinking ship and the smoke of a burning one (0x62018B), the
// cannon balls (0x6038C0), the guns' smoke and fire, the splashes, the hits
// and their splinters (the engine's Sailing::BattleEffect), each ship's
// pennant in the wind (0x56CA0B, 0x568EA0), and the clouds of the wind
// (0x6107B9). A battle at a town (its method +0x20,
// 0x6018DB, then the sea's) is drawn over the town's battle map, which the
// viewer's town view draws (BattleTownLayer), with its towers' smoke and fire.
// The ships' labels are handed to the viewer, which has the fonts.

#include <SDL.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "p3data/battle_clouds.hpp"
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

// The town under a battle at a town: the viewer's town view showing the town's battle map (the town's harbour
// with the open sea before it, its buildings as they are now). Its parts are drawn by the viewer: the ground (the
// battle's water, animated, goes under it), then its objects row by row among the ships (the town view's
// order); `splash` says whether a ball falling at the area's screen pixel (x, y) splashes (the cell's kind
// water or sand bank, 0x60329B) or raises a puff; `minimap`, the town view's minimap of the battle map.
struct BattleTownLayer {
	std::function<void(int camera_x, int camera_y)> ground;
	std::function<void(int camera_x, int camera_y, int last_row)> objects;  // Up to that row, from the last call's.
	std::function<bool(int x, int y)> splash;
	std::function<bool(int x, int y)> water;  // Whether the area's screen pixel is the water's (for the ripples).
	SDL_Texture* minimap = nullptr;
};

class BattleView {
public:
	BattleView(SDL_Renderer* renderer, const p3::data::CprArchive& archive, const p3::data::TileSet& tiles);
	~BattleView();
	BattleView(const BattleView&) = delete;
	BattleView& operator=(const BattleView&) = delete;

	// The battle's area (ship_step.cpp: 0x2200 × 0x1E04 at sea; at a town 0x1100 × 0xF02, the town view's whole
	// map: 0x5FFC10's item 7), on the screen with y · 11 / 17 as the town view.
	static constexpr int kAreaWidth = 0x2200;
	static constexpr int kAreaHeight = 0x1E04;
	static constexpr int kTownAreaWidth = 0x1100;
	static constexpr int kTownAreaHeight = 0xF02;
	static int screen_y(int y) { return y * 11 / 17; }
	// Whether the battle is at a town (its class's vtable 0x67AAA0; at sea 0x67AD44).
	static bool at_town(const p3::data::SaveGame& save, size_t battle);
	static int width(const p3::data::SaveGame& save, size_t battle) {
		return at_town(save, battle) ? kTownAreaWidth : kAreaWidth;
	}
	static int height(const p3::data::SaveGame& save, size_t battle) {
		return screen_y(at_town(save, battle) ? kTownAreaHeight : kAreaHeight);
	}

	// The animations the battle needs now (its ships' looks, each type and hull state with its five sails; the
	// effects; a sinking ship's), added to what is to load; load_step decodes one each call (a frame), so that a
	// new one comes without a jerk. pending(): how many are left.
	void need(const p3::data::SaveGame& save, size_t battle);
	void load_step();
	size_t pending() const { return queue_.size(); }
	// A ship had no look at all to draw at the last draw (the battle's start, still loading): the viewer's notice.
	bool missing_looks() const { return missing_looks_; }
	// Everything freed (the battle left the screen).
	void clear();

	// The engine's effects of this frame (Sailing::take_battle_effects): those of the battle shown (`town`: the
	// town under a battle at a town, for where a ball splashes).
	void add_effects(const p3::data::SaveGame& save, size_t battle, const std::vector<p3::data::Sailing::BattleEffect>& effects,
		uint32_t now, const BattleTownLayer* town = nullptr);

	// The battle on the screen part (0, top) − (view_width, top + view_height), the camera on the area's
	// (camera_x, camera_y) (screen pixels: x and y · 11 / 17). `now`: the viewer's ms. The ships' labels go to
	// `labels`, in screen pixels. `selection`: the places of the ships chosen (a blue arrow over the player's).
	// `town`: a battle at a town, the town under it.
	void draw(const p3::data::SaveGame& save, size_t battle, int camera_x, int camera_y, int top, int view_width,
		int view_height, uint32_t now, const std::vector<uint16_t>& selection, std::vector<BattleLabel>& labels,
		const BattleTownLayer* town = nullptr);

	// The ship drawn under the screen's (x, y) at the last draw, a finger's width around its picture (the
	// nearest one's anchor if several): its place in the scene ([+0x138]) and whether it is the player's; -1.
	int ship_at(int x, int y, bool& own) const;
	// A town battle's standing tower whose picture is under the area's screen pixel (x, y) (the nearest one's
	// place if several): its k (its cell [+0x1EAE + k], [+0x1EB2 + k]), or -1.
	static int tower_at(const p3::data::SaveGame& save, size_t battle, int x, int y);
	// A marker where an order went (the area's screen pixels), fading for a moment: the touch's answer.
	void mark_order(int x, int y, uint32_t now);
	// The marker arrow over a chosen ship (markerpfeil01.aim, the manual's blue arrow), for the labels.
	SDL_Texture* arrow() const { return arrow_; }

	// The side panel's minimap of the battle in `rect`, as the game's town minimap (0x59C590, 0x59CA00): the sea, the
	// reefs (grey) and sand banks (yellow), or at a town the minimap of its battle map (`town`), the screen's part
	// (`camera`, the area's screen pixels) framed in grey, each ship a 3 · 3 dot of the town view's colour for its
	// owner (0x56C7B0: the local player's blue, a pirate's dark grey, another human's red, the others' white).
	void draw_minimap(const p3::data::SaveGame& save, size_t battle, const SDL_Rect& rect, const SDL_Rect& camera,
		SDL_Texture* town = nullptr) const;
	// The area's screen pixels under the minimap's (x, y); false outside it.
	static bool minimap_point(const p3::data::SaveGame& save, size_t battle, const SDL_Rect& rect, int x, int y,
		SDL_Point& at);
	// The state of the panel's battle buttons (0x610D69): bits 0x1 «Flee» on, 0x2 all the chosen fled, 0x10
	// «Capture» on, 0x20 all capture, 0x100 «Plunder» on, 0x200 all plunder, 0x1000 «White flag» on, 0x2000 all
	// with it, 0x8000 «Leave» (the enemy all gone: it takes «Flee»'s place). 0 with a chosen ship not his. At a
	// town never «Flee», «Plunder» nor «Leave».
	static uint16_t panel_buttons(const p3::data::SaveGame& save, size_t battle, int player);

	// Where the camera looks first: the middle of the player's ships in the battle (the area's screen pixels);
	// false while none is placed (before the battle's first step).
	static bool player_ships_centre(const p3::data::SaveGame& save, size_t battle, int player, SDL_Point& centre);
	// The player's ships in the battle that can take orders (not out, sunk or sinking: [+0x12A] & 6), in the
	// scene's order: their places.
	static std::vector<uint16_t> player_ships(const p3::data::SaveGame& save, size_t battle, int player);
	// A ship's place in the area's screen pixels; false if the place has no ship.
	static bool ship_point(const p3::data::SaveGame& save, size_t battle, uint16_t place, SDL_Point& point);
	// The scene's ship in that place (its block in the battles' graph), or -1.
	static int scene_entry(const p3::data::SaveGame& save, size_t battle, uint16_t place);
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
	// A tower of a town battle (0x6018DB, the four of [+0x1EC4 + 4k]): its next puff of smoke and its fire.
	struct TowerFire {
		uint32_t next_smoke = 0;
		int level = 0;  // 0 none, 1 small, 2 middling.
		uint32_t start = 0;
	};
	// A splinter of a hit (class 0x13, `brocken_a`/`_b`: a piece of wood turning, its 8 frames of 16 · 16; 0x580520
	// lets it drift a few pixels a second, its speed shaken at random): in the area's screen pixels.
	struct Splinter {
		float x = 0;
		float y = 0;
		float vx = 0;
		float vy = 0;
		uint32_t start = 0;
		uint32_t life = 0;
		int image = 0;
		int frame = 0;
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
	// The battle's reefs ([+0x872 + 8k]: x, y, w, h in the town view's cells, the front one; 0x5944E0: a tile of
	// type 0x12 of that size, `riffWxH`, over its ground `riffWxH_unten` of type 0x11, its parameter 1) and sand
	// banks ([+0x672 + 8k]; 0x594640: type 0x13, `sandbank…`, chosen by x · y among those of the size).
	void draw_reefs(const p3::data::SaveGame& save, size_t battle, int camera_x, int camera_y, int top, int view_width,
		int view_height);
	void draw_tile(const p3::data::TileDef* tile, int x, int y, int camera_x, int camera_y, int top);
	void draw_clouds(const p3::data::SaveGame& save, size_t battle, int camera_x, int camera_y, int top, int view_width,
		int view_height, uint32_t now);
	// The towers' smoke and fire (0x6018DB), by their strength.
	void draw_towers(const p3::data::SaveGame& save, size_t battle, int camera_x, int camera_y, int top, uint32_t now);
	void add_splinters(float x, float y, int count, uint32_t now);
	void draw_splinters(int camera_x, int camera_y, int top, uint32_t now);
	// The ripples (the town view's [+0x5A0]: a height map of the screen, 0x59EDF0 drops a point of 0xFF that spreads
	// and bends the water): here a cell each kRippleCell pixels over the screen's part and a margin, moved with the
	// camera; drops at the area's screen pixels; stepped at a steady pace; drawn as light and shade on the crests,
	// at a town only on its water.
	void ripple_drop(int x, int y, int strength);
	void ripple_step(int camera_x, int camera_y, int view_width, int view_height, uint32_t now, const BattleTownLayer* town);
	void ripple_draw(int camera_x, int camera_y, int top);
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
	bool missing_looks_ = false;
	SDL_Point order_mark_{};  // mark_order's point (the area's screen pixels) and when.
	uint32_t order_time_ = 0;
	bool order_marked_ = false;
	SDL_Texture* ball_ = nullptr;
	SDL_Texture* arrow_ = nullptr;  // The marker arrow over a chosen ship (markerpfeil01.aim), drawn blue.
	std::vector<const p3::data::TileDef*> rock_tiles_;  // Type 0x12 (riff…) and 0x13 (sandbank…), by id.
	std::vector<const p3::data::TileDef*> islet_tiles_;
	std::map<uint16_t, const p3::data::TileDef*> tile_ids_;
	std::vector<SDL_Texture*> pinned_;  // The reefs' atlases (kept when the looks are freed).
	// The clouds (battle_clouds.hpp): the class's looks (`wolke`..`wolke6`, iso/lcr/files.lst's order) and their
	// pictures, the battle's clouds and the time of their last step.
	std::vector<p3::data::LcrAnim> cloud_looks_;
	std::vector<SDL_Texture*> cloud_images_;
	std::vector<p3::data::BattleCloud> clouds_;
	uint32_t clouds_time_ = 0;
	std::vector<Effect> effects_;
	std::map<size_t, Sinking> sinking_;  // By the scene's ship.
	std::map<size_t, uint32_t> burning_;  // By the scene's ship: when it smokes next (0x620CDE).
	std::array<TowerFire, 4> towers_{};
	std::array<std::vector<SDL_Point>, 4> pennant_offsets_{};  // By type: each direction's pennant from the anchor.
	std::array<SDL_Texture*, 2> splinter_images_{};
	std::vector<Splinter> splinters_;
	static constexpr int kRippleCell = 4;
	static constexpr int kRippleMargin = 8;  // Cells beyond the screen's part each way.
	int ripple_w_ = 0;
	int ripple_h_ = 0;
	int ripple_x_ = 0;  // The field's first cell (the area's screen pixels / kRippleCell).
	int ripple_y_ = 0;
	std::vector<int16_t> ripple_now_;
	std::vector<int16_t> ripple_last_;
	std::vector<uint8_t> ripple_water_;  // 1 where the field may move (the water).
	std::vector<uint32_t> ripple_pixels_;
	SDL_Texture* ripple_texture_ = nullptr;
	uint32_t ripple_time_ = 0;
	bool ripple_masked_ = false;  // ripple_water_ made for the field's place.
	std::map<size_t, SDL_FPoint> last_place_;  // By the scene's ship: where it was at the last draw (its bow's drops).
	std::map<size_t, int> sink_frame_;  // By the scene's ship: the upper part's frame that dropped last.
	bool bow_side_ = false;  // The bow's drop goes to one side, then the other (0x56DA48: the clock's parity).
	uint32_t random_ = 12345;  // The viewer's own: the game's sprites take the world's rand() (left out of the engine).
};

}  // namespace p3::app
