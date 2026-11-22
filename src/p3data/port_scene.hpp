#pragma once

// The port's scene (the game's object *0x6E6220; re/emu/sesion50/README.md, «Part 4»): the ships of the town shown
// in its harbour, each on a slot, one of the town's tag 0 paths of `.nodes` (its lines from the sea's edge into the
// harbour), docked at its end or coming in and going out along it. This file: a slot's path as the scene keeps it and
// the place of its ship on it (0x613B20 → 0x617380 and its handlers), in the scene's units (the town's pixels, the y
// · 34 / 22). None of it is in the save: it is the view's.

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace p3::data {

// A slot's path as 0x6132C0 (coming in) or 0x6135C0 (going out) copy it: its points from the harbour's end (1) to the
// sea's (count − 1), an extra one at 0 (coming in: beyond the harbour's end, 2 · p1 − p2; going out: p2 again), the
// y · 34 / 22; their flags (2 at the harbour's end, 1 at the sea's); the path's length (0x6141D0: its segments'
// truncated square roots summed) and the ship's progress on it (0..0x800).
struct PortPath {
	std::array<int16_t, 8> x{};  // [+0x266 + 2 · (slot · 8 + k)]
	std::array<int16_t, 8> y{};  // [+0x1256 + ...]
	std::array<uint8_t, 8> flags{};  // [+0x2246 + slot · 8 + k]
	uint8_t count = 0;  // [+0x2A3E + slot]: the path's points + 1
	int16_t progress = 0;  // [+0x68 + 2 · slot]
	int32_t length = 0;  // [+0x2B40 + 4 · slot]
};

// A scene ship's place (its object's [+0x8..+0x11] and [+0x26]): x and y with their fractions, its heading (256ths of
// a turn, 0 north), its sails (0 furled .. 4 set).
struct PortPlace {
	uint16_t x_frac = 0;  // [+0x8]
	int16_t x = 0;  // [+0xA]
	uint16_t y_frac = 0;  // [+0xC]
	int16_t y = 0;  // [+0xE]
	uint8_t dir = 0;  // [+0x11]
	uint8_t sails = 0;  // [+0x26]
};

// The scene's y of a town's pixel y (or a difference): · 34 / 22 toward zero (0x2E8BA2E9's product).
int32_t port_scale_y(int32_t y);

// A slot's path from a tag 0 path of the town's `.nodes` (in the town's pixels, from the sea's edge to the harbour),
// as 0x6132C0 (`coming_in`) or 0x6135C0 copy it, with its length (0x6141D0).
PortPath port_path(const std::vector<std::array<int, 2>>& points, bool coming_in);

// 0x617380: the ship's place `r` along its path (0..0x800 of the path's length + 100, from the harbour's end),
// going out (`out`) or coming in; the handlers write `place` (its sails too: `place.sails` is read before, the game
// plays a sound when coming in they go from 4 to 3).
void port_place_on_path(const PortPath& path, int16_t r, bool out, PortPlace& place);

// 0x613B78: a docked ship's place, at its path's last point (the harbour's end; `points` the .nodes path, in the
// town's pixels), heading along its last stretch, the sails furled.
void port_place_docked(const std::vector<std::array<int, 2>>& points, PortPlace& place);

// 0x613B20: a scene ship's place by its scene state ([+0x12A]: 0 docked; 1 and 9 coming in, at 0x800 − its progress;
// 8 and 0x10 going out, at its progress; 0x18 going out held at 0x400; any other, left as it is).
void port_place(uint8_t state, const std::vector<std::array<int, 2>>& points, const PortPath& path, PortPlace& place);

// The port's wind as the scene keeps it ([+0x7EEC] the world's tick of its last change, a multiple of 4; [+0x7EF0]
// its direction, 256ths of a turn, 0 north; [+0x7EF1] its strength).
struct PortWind {
	uint32_t tick = 0;
	uint8_t dir = 0;
	uint8_t strength = 0;
};

// 0x618D10: the wind at the world's tick `tick` for the town of index `town` ([+0x5C]), `sea` its StadtDaten.ini's
// AusrichtungDesMeeres (in eighths): when 4 ticks or more passed, a direction by the day and the town in a half turn,
// a quarter turn on when it would blow from the sea's side (and another past its three eighths), its strength (4..7)
// by it; moved towards them from the last at most 8 and 1 for each 4 ticks, unless 16 or more of those passed.
void port_wind(PortWind& wind, uint32_t tick, int town, uint8_t sea);

struct SaveGame;

// A scene ship (the battles' class 0x61A4E0, what the port uses of it).
struct PortShip {
	uint16_t ship = 0;  // [+0x4]
	uint8_t state = 0;  // [+0x12A]
	int16_t base = 0;  // [+0x13A]: its progress at the last tick
	PortPlace place;
	// Its figure (class 0x0D, [+0x134]): the creature's number in the living town (the viewer's), 0 none, and the
	// heading it shows (kept while within 2 of its place's, 0x620C3A).
	uint16_t figure = 0;
	uint8_t figure_dir = 0;
	uint16_t pennant = 0;  // Its figure's pennant (the figure's [+0x50]: the living town's creature, 0 none).
	uint32_t colour = 0xFFFFFFFF;  // Its figure's colour, ARGB ([+0x34]): its pennant's and its mark's.
	uint8_t chosen = 0;  // Its figure's [+0x3A]: 1 its ship in the ship menu's selection (its mark shown), 0 not.
	uint8_t mark_alpha = 0xFF;  // [+0x48]: its mark's alpha.
	int8_t icon = -1;  // [+0x4A]: the icon over it (its page's tag: 1 the shipyard, 2 the auction, 3 the trade
	// route), −1 none.
	std::string label;  // Its name over it (0x56C010: the player's ships only), empty none.
};

// A sound the scene plays (0x59A090: the sound's session's TownSounds::ship_in_out), at a ship's place in the town's
// pixels; `kind` the game's third argument (0 going out from the quay, 1 its sails taken in coming in).
struct PortSound {
	int x = 0;
	int y = 0;
	int kind = 0;
};

// The port's scene (*0x6E6220) for the town shown: a slot for each tag 0 path of its `.nodes`, the ships of its port
// on them. The game's simulation tells it when a ship starts coming in (0x6132C0), docks (0x613920) or starts going out
// (0x6135C0, «create» 0): the viewer doesn't hook the engine, it compares each ship's state with the last tick's
// (`tick`), so ships changing in the same tick take their slots in the ships' order rather than the engine's calls'.
class PortScene {
public:
	// 0x6141D0: the town shown (`paths` its tag 0 paths), its ships put back on the slots the town's table keeps for
	// them while they are still there, then every ship of its port by its state; then a tick and a frame.
	void show(const SaveGame& save, int town, const std::vector<std::vector<std::array<int, 2>>>& paths, uint32_t now);
	// A world tick passed: the ships that started coming in, docked or started going out told, then 0x613DA0.
	void tick(const SaveGame& save, uint32_t now);
	// 0x614080 (each frame of the view): each ship's progress from the time since the tick, its place (0x613B20).
	void frame(const SaveGame& save, uint32_t now);
	// No town shown (0x6141D0 with 0x80): its ships taken away.
	void hide();
	// 0x618D10 (port_wind): the port's wind, what the town view reads each frame into its [+0x598] (the direction)
	// and [+0x599] (the strength) when no battle is shown (0x58B86A → 0x613070), for the town shown.
	void wind(const SaveGame& save, uint8_t& dir, uint8_t& strength);
	// The milliseconds a tick lasts as the viewer runs (the game reads its *0x6DFBC4); 0 the save's.
	void set_ms_per_tick(uint32_t ms) { ms_per_tick_ = ms; }
	int town() const { return town_; }
	// The slots in the list's order (head first), each with its ship.
	const std::vector<uint16_t>& order() const { return list_; }
	PortShip& at(uint16_t slot) { return *slots_[slot]; }
	const PortShip& at(uint16_t slot) const { return *slots_[slot]; }
	// The sounds asked since the viewer last took them.
	std::vector<PortSound> sounds;
	// A figure taken away (its slot freed): the viewer removes its creatures.
	std::vector<uint16_t> gone;

private:
	int town_ = 0x80;  // [+0x5C]
	std::vector<std::vector<std::array<int, 2>>> nodes_;
	std::vector<PortPath> paths_;  // [+0x266...], by slot
	std::vector<std::unique_ptr<PortShip>> slots_;  // [+0x4]
	std::vector<uint16_t> list_;  // [+0x10], [+0xC]
	std::map<int, std::array<uint16_t, 255>> table_;  // [+0x2F3C]
	uint32_t tick_ms_ = 0;  // [+0x64]
	uint32_t ms_per_tick_ = 0;
	std::map<uint16_t, uint16_t> last_state_;  // the town's ships' states at the last tick (the viewer's)
	PortWind wind_;  // [+0x7EEC], [+0x7EF0], [+0x7EF1]

	int find(uint16_t ship) const;
	uint16_t take_slot(const SaveGame& save, uint16_t ship);
	void remove(uint16_t slot);
	void coming_in(const SaveGame& save, uint16_t ship);
	void going_out(const SaveGame& save, uint16_t ship, bool create);
	void docked(const SaveGame& save, uint16_t ship);
	void scene_tick(const SaveGame& save, uint32_t now);
	std::array<uint16_t, 255>& table();
};

}  // namespace p3::data
