#pragma once

// The living town: the town view's creatures (the game's «creatures», docs/formats/lcr.md; re/emu/sesion50/README.md),
// as the game's view makes and steps them. None of it is in the save or changes the game: it is the view's. The game
// steps them with the CRT's rand(), the one the simulation uses too (so on the PC the town on screen changes the
// game's random numbers: roadmap, section 8); here they take a random of their own (TownLife::rand).
//
// Done: the citizens walking the streets (0x595CA0 makes them when a town is shown; each frame 0x568C80 steps them:
// their class's +0x14, 0x559EB0 → 0x55B6A0 → 0x55C820 the free walk), coming into the town and leaving it by its land
// roads (0x55B1D0, 0x55B6A0) as each class's clocks say (0x596EE0, and when the town is shown 0x5963F3..); the
// children, the market's sellers and the juggler, the official, the rats with the plague, the land trade's carts; the
// town's dogs and cats and the wild animals outside the walls (0x58B1B0 when the town is shown; 0x57C240, 0x57E840),
// the birds (0x58A920; 0x563EF0) and the fish (each frame 0x58C6CB; 0x569560), each made again as one goes (0x593D50);
// the fisherman, made by his hut at work (0x555B00 → 0x59A420; 0x569E10).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "p3data/lcr.hpp"
#include "p3data/savegame.hpp"
#include "p3data/town.hpp"

namespace p3::data {

// A town's points (iso/towns/N.spawn, the view's SpawnFormat, read by its town load 0x58EC.. into [+0xC688] and
// [+0xC69C]): groups of a tag (0 outside the walls, the wild animals'; 1 inside, the citizens' and the town's
// animals'; 2..4 to be seen), each group's cells. False if the bytes don't make whole groups.
using TownCell2 = std::array<int, 2>;
bool parse_town_spawn(const uint8_t* data, size_t size, std::vector<std::vector<TownCell2>>& out);

// A town's paths (iso/towns/N.nodes, NodesFormat, into [+0xC658] and [+0xC670]): groups of a tag, each a list of
// polylines in the town's pixels: tag 0 the ships' way in from the sea's edge, 1 the land roads into the town, 2
// out of it, 3 and 4 to be seen.
using TownPath = std::vector<TownCell2>;
bool parse_town_nodes(const uint8_t* data, size_t size, std::vector<std::vector<TownPath>>& out);

// What the creatures read of the town on screen.
struct LifeMap {
	int width = 0;  // 128 (the view's [+0xC314])
	int height = 0;  // 226 ([+0xC318])
	std::vector<uint8_t> ground;  // y · width + x: the ground tile's .let type ([+0xC308])
	std::vector<int16_t> object;  // the object's .let type there, −1 none ([+0xC30C], its whole footprint)
	std::vector<uint8_t> grid;  // the town's grid ([0x7A4] → [0x80C]: x · height + y; bit 0x80 inside the town)

	bool in(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
	bool inside(int x, int y) const {
		const size_t i = size_t(x) * size_t(height) + size_t(y);
		return in(x, y) && i < grid.size() && (grid[i] & 0x80) != 0;
	}
	uint8_t ground_at(int x, int y) const { return in(x, y) ? ground[size_t(y) * size_t(width) + size_t(x)] : 0; }
	int object_at(int x, int y) const { return in(x, y) ? object[size_t(y) * size_t(width) + size_t(x)] : -1; }
};

// What the citizens read of the town.
struct LifeTown {
	// Each group's residents (0x595CA0: the town's [0x2E4 − 4 · group]): the beggars, the poor, the middle class, the
	// rich; one walker for every 50.
	std::array<uint32_t, 4> residents{};
	std::vector<std::vector<TownCell2>> spawn;  // N.spawn
	// The market's first 8 stalls (the town's [0x7D0..0x7DF], x, y; 0xFF none: left out), where 0x55D9D0 sends them.
	std::vector<TownCell2> stalls;
	// The buildings 0x55D9D0 sends them to: the town's list [0x764], then each of its offices' [+0x2CE] (along
	// [0x784] by [+0x2CA]), in that order: each record's cell (x, y) and its type.
	std::vector<std::array<int, 3>> buildings;
	// Each group's other figure ([0x2F4 − 4 · group]): its walkers come in by the roads the more often the more its
	// residents pass it, and go the more often the more it passes them (0x596EE0).
	std::array<uint32_t, 4> passed{};
	std::vector<std::vector<TownPath>> nodes;  // N.nodes: the tag 1 roads in, the tag 2 roads out (3 each)
	bool plague = false;  // [0x2C8] & 8: one walker for every 100 residents, no children
	// The children walking (0x595F0F, 0x5978EC): each class's mood ([0x300 + 2k], s16) by its residents ([0x2D8 +
	// 4k]) summed, at least 0, + 9999, / 10000; none with the plague.
	uint32_t children = 0;
	uint32_t total = 0;  // [0x2D4]: with the plague, a rat for every 50
	bool official = false;  // [0x76C] & 2: the town's official comes out to read (0x5984E0)
	uint8_t official_face = 0x20;  // where he reads facing (0x677958 by the town's map)
	// The carts: the town's index, every town's place on the world map (0x6DDB90 + 0x34 · its number 0x6DE4B8:
	// StadtSymbolXPos, YPos) and the land trade's convoys (the event 0x0C) told ([+0xB] & 1) of this town: leaving it
	// (the state 1, its [+0x8]) or unloading in it (the states 6 and 7, its [+0x9]).
	int index = -1;
	std::vector<TownCell2> world;
	struct Convoy {
		uint16_t event = 0;
		uint8_t to = 0;  // [+0x9]
		uint8_t state = 0;  // [+0x15]
		uint8_t counter = 0;  // [+0x16]
		bool here = false;  // leaving this town (state 1) or arriving at it (6, 7)
	};
	std::vector<Convoy> convoys;
	bool feeding_poor = false;  // [0x2C8] & 0x800000: the church feeds the poor (the beggars go to it)
	bool feast = false;  // a feast at the market (0x55D9D0: the world's tick − [0x2D0] or an announced one within 0x300)
	// The fish (0x58C6D9): the fisherman's huts' productivity (the town's s16 [0x898], P3Modding's facility 5 [+0x8],
	// read unsigned): above 1000 15 fish, above 500 10, else 5; 40 off a town (index −1).
	uint16_t fishing = 0;
	bool frozen = false;  // [0x2C8] & 0x4000000: the port frozen (the ice floes, 0x58C886; 0x57A9B9)
};

// The map the creatures read: the town drawn (each cell's ground and object .let types, the editor's marks left
// out as the game's town files have none) and the town's grid.
LifeMap life_map(const TownMap& town, const TileSet& tiles, const std::vector<uint8_t>& grid);
// What the citizens read of the save's town, but its points (N.spawn of its template_map(), from the archive).
LifeTown life_town(const SaveGame& save, size_t town);

// The town's pixel at the middle of a cell (0x58CFB0: x · 34 + 17, + 17 on odd rows; y · 11 + 10).
TownCell2 life_cell_pixel(int x, int y);
// The cell under a town's pixel (0x58D030; the view's DetectBitmap taken as the nearest diamond).
TownCell2 life_pixel_cell(int x, int y);
// The cell next to (x, y) in a direction (0x58B740: 256ths, 0 north, clockwise; 0x20, 0x60, 0xA0, 0xE0 the grid's
// neighbours, the others two rows or a column away). False off the map.
bool life_neighbour(int& x, int& y, uint8_t direction, int width, int height);

// A creature of the town view.
struct Creature {
	int cls = 0;  // Its LCR class (0 the poor, 1 the middle class, 2 the rich, 3 a child, 4 a beggar, 5 a market
	// seller, 6 the juggler...).
	int group = -1;  // The citizens' group (0 the beggars, 1 the poor, 2 the middle class, 3 the rich), −1 none.
	const LcrAnim* look = nullptr;
	uint16_t id = 0;  // [+0x16]
	int32_t x = 0;  // [+0xC], [+0x10]: the town's pixels, 16.16
	int32_t y = 0;
	uint8_t dir = 0;  // [+0x14]: the direction it faces (256ths, 0 north, clockwise)
	uint8_t frame = 0;  // [+0x15]: the frame of its direction shown
	// [+0x20]: where it may walk (0x55A290).
	bool street_first = true;  // [+0x24]: keep to the streets while it is on one
	bool outside = false;  // [+0x25]: walks outside the town (the grid's bit 0x80 clear)
	bool cart_pass = false;  // [+0x26]
	uint8_t radius = 0;  // [+0x27]
	bool on_street = false;  // [+0x28]: it was on a street when tested (grass then barred)
	// [+0x2C]: its animation (0x557340).
	uint32_t frame_due = 0;  // [+0x30]: when its next frame is due
	uint8_t seq = 2;  // [+0x34]: the sequence playing (its id | a mode in the bits 0xE0)
	uint8_t next_seq = 2;  // [+0x35]: the one after it
	uint16_t done = 0;  // [+0x38]: the milliseconds walked of its step
	uint16_t total = 0;  // [+0x3A]: its step's milliseconds
	uint8_t target = 0;  // [+0x3C]: the direction it turns to
	uint8_t wish = 0;  // [+0x3D]: the direction towards its goal
	int32_t step_x = 0;  // [+0x40], [+0x44]: 16.16 pixels a millisecond
	int32_t step_y = 0;
	int32_t stuck = 0;  // [+0x48]
	uint16_t goal_x = 0;  // [+0x50], [+0x52]: its goal, the town's pixels
	uint16_t goal_y = 0;
	bool sidestep = false;  // [+0x54]
	uint8_t countdown = 0;  // [+0x55]
	uint8_t left_turns = 0;  // [+0x56]
	uint8_t right_turns = 0;  // [+0x57]
	uint32_t church = 0;  // [+0x4C]: the tick the church fed the poor (a beggar's)
	// [+0x58..+0x74]: a walker on a road.
	TownCell2 corner_a{};  // [+0x58], [+0x5C]: the corner's two cells' middles when the road turns a right angle
	TownCell2 corner_b{};  // [+0x60], [+0x64]
	uint8_t road = 0;  // [+0x68]: its road (of its mode's tag)
	uint8_t point = 0;  // [+0x69]: the road's point it walks to
	uint8_t mode = 0;  // [+0x6A]: 0 free in the town, 1 coming in by a road (tag 1), 2 leaving by one (tag 2); the
	// viewer's for the other classes: 3 a market seller or the juggler at his place, 4 the official, 5 a rat, 6 a cart,
	// 7 an animal, 8 a bird, 9 a fish, 10 the fisherman (his path of the tag 4 in `road`), 11 a figure moved from outside,
	// 12 a ship figure's pennant, 13 an effect on the water (class 0x12 of the water list: a wave 0x50, an ice floe 0xC7)
	uint8_t corner = 0xFF;  // [+0x6B]: 0xFF to be worked out, 0 none, 1 and 2 the corner's halves
	bool entered = false;  // [+0x6C]: coming in, it reached the town (it walks free at the next point)
	bool flee = false;  // [+0x6D]: hurrying from a siege (not yet)
	int32_t lane_x = 0;  // [+0x70], [+0x74]: aside from the carts on its road (0x570BA0; no carts yet)
	int32_t lane_y = 0;
	uint8_t counter = 0;  // The juggler's [+0x2C], the official's, the rat's and the animals' [+0x38]: down a frame
	uint8_t face = 0;  // The official's [+0x41]: where he reads facing
	bool back = false;  // The official's [+0x38]: walking back along his path
	uint8_t reads = 0;  // The official's [+0x40]: the rounds of his reading left
	uint8_t act = 2;  // An animal's [+0x3A]: what it is to do at its sequence's end (2 walk, 3 rest, 0xB run)
	uint8_t alt = 0;  // A bird's [+0x2C]: how high it flies (its picture that far up and a fifth of it to the left)
	uint8_t alpha = 0;  // A fish's [+0x3B]: how much it is seen (drawn at half that alpha, its colours by it); a
	// wave's [+0x30], its alpha
	bool appearing = true;  // A fish's [+0x3C]: coming up (false: fading away, then it goes)
	uint8_t turns_left = 0;  // A fish's [+0x3A]: its choices left (3 to 5) before it fades away
	// A cart (the coachman's [+0x38..+0x64]): its convoy's event (−1 none), whether it arrives, its convoy's progress
	// (0..255) and when it was read, its road's length in x pixels and how far it has gone, its last and next points
	// of the road's lane, the lane's points, its way (one of the diagonals).
	int event = -1;  // [+0x64]
	bool arriving = false;  // [+0x3A]
	uint8_t progress = 0;  // [+0x4C]
	uint32_t progress_at = 0;  // [+0x44]
	uint32_t length = 0;  // [+0x40]
	uint32_t gone = 0;  // [+0x48]
	uint8_t last_point = 0;  // [+0x39]
	uint8_t points = 0;  // [+0x3B]
	uint8_t way = 0;  // [+0x58]
	bool gone_away = false;  // [+0x5A]
	uint16_t owner = 0;  // A pennant's [+0x24]: its ship figure (it goes with it, 0x56CD50)
	// An effect's number's low byte (class 0x12: 0x50 a wave, 0xC7 an ice floe) and its fade (0x57AF40, 0x57A91C): the
	// alpha's speed in 1000ths a ms ([+0x34]), the ms before it starts ([+0x3C]) and its rest ([+0x38]).
	uint8_t effect = 0;
	int32_t fade_speed = 0;
	uint32_t fade_delay = 0;
	int16_t fade_rest = 0;
	std::vector<TownCell2> cells;  // The cells its strips stand on (0x593540): what it blocks, where each is drawn.

	int px() const { return int(x >> 16); }
	int py() const { return int(y >> 16); }
	// The direction of its look shown (0x557340: (dir · directions + 0x80) >> 8, wrapped).
	size_t direction_index() const;
	const LcrFrame* current() const;
};

class TownLife {
public:
	// The citizens' looks by class (the LCR files of iso/lcr/files.lst in its order: the class's static array, e.g. the
	// poor's *0x6E1770).
	std::array<std::vector<const LcrAnim*>, 0x20> people;
	// The waves' looks (the effects of class 0x12 numbered 0x50) and the ice floes' (0xC7) by their size
	// (LcrAnim::variant, 1..3).
	std::array<const LcrAnim*, 4> wave_looks{};
	std::array<const LcrAnim*, 4> floe_looks{};

	// A town shown (0x595CA0's first part): its walkers made again.
	void start(const LifeMap& map, const LifeTown& town, uint32_t now);
	// The buildings changed (the town drawn again): the map and the destinations, the walkers kept.
	void update_map(const LifeMap& map, const LifeTown& town) {
		map_ = map;
		town_ = town;
	}
	// One frame (0x568C80): `elapsed` the milliseconds since the last one (at most 1000), `view` the camera's
	// rectangle in the town's pixels (x, y, w, h: whether a walker is seen).
	void step(uint32_t now, uint32_t elapsed, const std::array<int, 4>& view);
	// The world's time for the carts: its tick (a new one reads the convoys again), the milliseconds a tick lasts
	// (*0x6DFBC4) and whether a battle sets its pace (3375 ms, *0x6DFC1C).
	void set_world_time(uint32_t tick, uint32_t ms_per_tick, bool battle_time) {
		world_tick_ = tick;
		ms_per_tick_ = ms_per_tick ? ms_per_tick : 1;
		battle_time_ = battle_time;
	}
	// The town read again at a new tick (its residents, plague, feast, convoys...; its points and paths kept).
	void update_town(const LifeTown& town) {
		std::vector<std::vector<TownCell2>> spawn = std::move(town_.spawn);
		std::vector<std::vector<TownPath>> nodes = std::move(town_.nodes);
		town_ = town;
		town_.spawn = std::move(spawn);
		town_.nodes = std::move(nodes);
		sync_carts(false);
	}
	// The walkers of a group in the town, coming in and leaving (the view's [+0xC5B8], [+0xC5C0], [+0xC5C8]).
	int count(int group, uint8_t mode) const;
	const std::vector<Creature>& creatures() const { return creatures_; }
	// The creature of the number `id` (the view's [+0x3FC][id]), null if none.
	const Creature* find(uint16_t id) const {
		for (const Creature& c : creatures_) {
			if (c.id == id) {
				return &c;
			}
		}
		return nullptr;
	}
	void clear() { creatures_.clear(); }
	// A fisherman's hut at work asks for its fisherman (0x59A420, from its step 0x555B00 with its cell's point, the
	// town's pixels): if a path of the tag 4 still free starts within 304 pixels of it, one made on the nearest free
	// path (0x569C20) and its number given; else 0.
	uint16_t spawn_fisherman(int x, int y);
	// A creature taken away (the hut's fisherman when it stops: the view's list's +0x4).
	void remove_creature(uint16_t id);
	// A figure some other piece of the view moves (mode 11: the port's ships, class 0x0D, 0x620BD0 and 0x56B140): made
	// at (x, y) facing `dir` with its look's frame 0, and put elsewhere (drawn again only when anything changed).
	uint16_t add_figure(int cls, const LcrAnim* look, int32_t x, int32_t y, uint8_t dir);
	void move_figure(uint16_t id, const LcrAnim* look, int32_t x, int32_t y, uint8_t dir);
	// A ship figure's pennant (class 0x1E, mode 12; the figure's start 0x56C7B0 makes it through 0x590610 at the
	// figure's place moved by its look's pennant point): 0x568DD0 starts it with a frame at random, the view's wind as
	// its direction and its first frame due within a frame's time; 0x568EA0 waves it; 0x568E40 moves it with its
	// figure (0x56B140); it goes with its figure (0x56CD50: remove_creature).
	uint16_t add_pennant(const LcrAnim* look, uint16_t owner, int32_t x, int32_t y);
	void move_pennant(uint16_t id, int32_t x, int32_t y);
	// The view's wind ([+0x598] its direction, 256ths of a turn, and [+0x599] its strength; 0x613070 each frame:
	// PortScene::wind), what the pennants turn to and how fast they wave.
	void set_wind(uint8_t dir, uint8_t strength) {
		wind_dir_ = dir;
		wind_strength_ = strength;
	}

	// The viewer's own random (MSVC's rand(): the game's walkers use the CRT's, the simulation's).
	uint32_t rand_state = 1;
	int rand();

private:
	LifeMap map_;
	LifeTown town_;
	std::vector<Creature> creatures_;
	uint16_t next_id_ = 1;
	uint32_t now_ = 0;
	uint32_t elapsed_ = 0;
	std::array<int, 4> view_{};
	std::array<uint32_t, 4> arrive_due_{};  // [+0xC5D0]: each group's next walker coming in by a road
	std::array<uint32_t, 4> leave_due_{};  // [+0xC5E0]: each group's next walker leaving
	uint32_t tick_due_ = 0;  // [+0xC5F0]: 0x596EE0's next round (every 1558 ms)

	bool roads() const;
	const TownPath* road_path(int tag, int index) const;
	Creature make_walker(int group, int32_t x, int32_t y, uint8_t dir, uint32_t mode_word);
	bool start_road(Creature& c, int road, bool arriving);
	bool road_walk(Creature& c);
	void set_steps(Creature& c, uint8_t dir) const;
	void prefill_roads();
	void life_tick();
	void place_performers();
	uint32_t official_due_ = 0;  // [+0xC598]: when the official comes out (0 out now)
	void official_target(Creature& c) const;
	bool official_walk(Creature& c);
	bool rat_walk(Creature& c);
	// The tag 4 paths free for a fisherman ([view+0xC59C]: each set at the town's load, 0x58EF26; a fisherman takes his,
	// 0x569DAD, and nothing gives it back: with fix_game_bugs() remove_creature does).
	std::vector<uint8_t> fisher_free_;
	void fisher_target(Creature& c) const;
	bool fisher_walk(Creature& c);
	uint8_t wind_dir_ = 0;
	uint8_t wind_strength_ = 0;
	bool pennant_wave(Creature& c);
	// The waves (0x58C345..0x58C6B5): when the next is due ([view+0xC62C]) and the two counters that walk the map's
	// 13 · 13 parts ([view+0xC630], [view+0xC631]); the frame's time unclamped ([view+0x414]).
	uint32_t wave_due_ = 0;
	uint8_t wave_col_ = 0;
	uint8_t wave_row_ = 0;
	uint32_t raw_elapsed_ = 0;
	void spawn_waves();
	// The ice floes made for the frozen port ([view+0xC63C]: 0x58C8A5, cleared when it thaws).
	bool floes_made_ = false;
	void spawn_floes();
	bool water_effect_step(Creature& c);
	bool spawn_rat(bool visible_check, bool no_grass);
	bool spawn_animal(int cls, bool out_of_sight, const LcrAnim* look, uint16_t id);
	bool spawn_bird(int32_t x, int32_t y, uint8_t dir, const LcrAnim* look, uint8_t alt, uint16_t id);
	bool bird_fly(Creature& c);
	TownCell2 out_of_sight_point(int margin_x, int margin_y);
	void spawn_fish();
	bool fish_swim(Creature& c);
	void replace(const Creature& gone);
	uint16_t new_id();
	bool animal_has(const Creature& c, uint8_t id) const;
	bool animal_walk(Creature& c);
	// The carts (the coachman, class 0x1F; 0x4D8F00 when the town is shown, 0x4E3A88 and 0x4E3CA9 as a convoy leaves or
	// unloads, 0x5708F0 at each tick, 0x570930 as it sets out or is done).
	uint32_t world_tick_ = 0xFFFFFFFF;
	uint32_t ms_per_tick_ = 234;
	bool battle_time_ = false;
	void sync_carts(bool shown);
	std::vector<std::pair<uint16_t, uint8_t>> carted_;  // The convoys (event, state) a cart was made for.
	void make_cart(const LifeTown::Convoy& convoy);
	TownCell2 lane_point(const Creature& c, int k) const;
	void cart_way(Creature& c) const;
	uint8_t lane_way(const Creature& c, int from, int step) const;
	bool cart_walk(Creature& c);
	uint16_t cart_distance(int x, int y, uint8_t road) const;
	bool perform(Creature& c);
	bool spawn_child(bool visible_check);

	bool spawn_cell(bool inside, bool visible_check, bool no_grass, TownCell2& out);
	bool cell_free(int x, int y, bool no_grass, uint16_t id) const;
	bool cell_walkable(const Creature& c, int x, int y, bool land, bool creatures) const;
	bool can_go(Creature& c, uint8_t dir, bool land, bool creatures) const;
	const Creature* walker_at(const Creature& c, int x, int y) const;
	bool visible(const Creature& c) const;
	void place(Creature& c);
	uint8_t anim_start(Creature& c, uint8_t request);
	uint8_t anim_step(Creature& c, uint8_t* counter = nullptr);
	void sequence_end(Creature& c);
	void choose_goal(Creature& c);
	uint8_t wish_dir(Creature& c) const;
	void begin_step(Creature& c, int cx, int cy);
	bool walk(Creature& c);
};

}  // namespace p3::data
