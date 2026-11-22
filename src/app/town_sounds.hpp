#pragma once

// The town view's sounds (*0x6E51AC's [+0xC7D0..+0xCBA4]; docs/formats/sound.md, «The town's sounds»): its
// buildings' sounds by where they are from the view's middle, and the church bells by the PC's clock.
//
// Each frame the view steps every building cell in view (0x592000 → its class's +0x38), and those of the classes
// that have a sound offer it with their .let type (0x594C00): the nearest within GebaeudeMinDist of the view's middle
// take the 4 slots ([+0xC998], 0x18 bytes each), one type not again for GebaeudeTypLockTime and a slot not for
// GebaeudeKanalLockTime (both ± half, at random) after it ended. A slot's sound (0x594770): a variant of its type's
// list (GebaeudeSoundListBase + SoundsProGebaeude · type) at a volume by its distance (VolDist) and at a side by it
// (PanDist), kept as the view moves (0x5948D0). The game's random numbers here are the world's rand(); the viewer's
// its own (as the music's: roadmap, section 8).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace p3::app {

class GameSound;

// Whether a building of the .let type `type` offers its sound when its cell is stepped (its class's +0x38 is the
// default 0x552C10, or the sawmill's 0x556860, the foundry's 0x555190, the fisherman's hut's 0x555B00), when active.
// (The market stalls', the well's and the crane's classes have no step: their lists never sound.)
bool tile_offers_sound(uint32_t type);

class TownSounds {
public:
	explicit TownSounds(GameSound& sound) : sound_(sound) {}

	// iso.ini's «Sound Stuff» (0x588352): GebaeudeSoundListBase, SoundsProGebaeude, GebaeudeMinDist, PanDist, VolDist,
	// GebaeudeTypLockTime, GebaeudeKanalLockTime, BuildingCollapseList, ShipSelSoundID, ShipInOutSoundList,
	// ChurchSoundList.
	void configure(const std::string& iso_ini);
	// The town view made (0x5884A6..0x588812): the ships' in and out list, the church's, each building type's
	// (106), the collapse's and the battles' and sieges' (60000..61100).
	void load_lists();

	// The view in the town's pixels, each frame ([+0xC650], [+0xC654]; its size [+0x2C], [+0x30]).
	void set_view(int x, int y, int width, int height);
	// 0x594C00: a building cell's sound offered (`x`, `y` its point in the town's pixels, `type` its .let type).
	void offer(int x, int y, int type, uint32_t now);
	// 0x594770: the slots offered and not playing, started.
	void start(uint32_t now);
	// 0x5948D0: the slots whose sound ended (and their wait) freed; with the view moved, the others' volume and side.
	// (The 4 special slots [+0xCB30], 0x594AA0, have no caller but the battles': part 5.)
	void update(uint32_t now);
	// 0x59A090: a ship of the port coming in (`leaving` false) or going out, at its point: ShipInOutSoundList + 0
	// or 1 in one of the slots 14..16, unless that one has just started there (the port's ships, the living town).
	void ship_in_out(int x, int y, bool leaving, uint32_t now);
	// 0x5993AA..0x599415 (0x599040, a building taken away by its fire's end, the living town): with the town view on the
	// screen, a variant at random of BuildingCollapseList (9000, [+0xC994]) at the cell's point, by its volume and side
	// (0x594B50), not looped and not kept (0x443150), unless it is too far to be heard.
	void collapse(int x, int y);
	// 0x5990A2..0x59912F (0x599040's first part): the buildings' slots taken by a fire's sound (the type 0) at one of
	// the fallen building's cells (`points`: their points as offered), stopped, released and cleared.
	void stop_fire(const std::vector<std::pair<int, int>>& points);
	// 0x58B82E ([+0x56C], the view's periodic 0x58B7F0): whether the town shown has something burning (its list [0x76A]
	// not empty); the buildings' offers of another type then leave the last of their 4 slots to the fire's (0x594C67).
	void set_burning(bool burning) { burning_ = burning; }
	// 0x5958CD, 0x598AB9: a ship of the port chosen by a touch: ShipSelSoundID ([+0xCBA4]) at the effects' volume
	// (−1), in the middle (0x40), not looped, not kept.
	void ship_selected();
	// 0x58B945..0x58BA5D (the town view's frame, with a town): at half past each hour of the PC's clock one bell
	// (ChurchSoundList + 1), at each hour as many as its hour (1..12; none at midnight) one after another.
	void bells();
	// 0x59C010: the locks and the slots forgotten (every sample stopped: 0x443940, 0x4436E0).
	void reset();

private:
	struct Slot {
		int x = 0;
		int y = 0;
		uint32_t distance = 0;  // [+0x8]: its offer's squared distance from the view's middle.
		int channel = -1;  // [+0xC]: its sound's channel, −1 none.
		uint32_t end = 0;  // [+0x10]: when it may go (its sound's end and its wait).
		int type = 0;  // [+0x14]
		bool taken = false;  // [+0x16]
	};
	static constexpr int kSlots = 17;  // ([+0xC998]: the buildings' the first 4, the ships' 14..16.)
	static constexpr int kBuildingSlots = 4;
	static constexpr int kTypes = 106;

	// 0x594B50: the volume (0..127, by VolDist) and the side (0..127, by PanDist) of a point of the town.
	void volume_and_pan(int x, int y, int& volume, int& pan) const;
	int rand15();

	GameSound& sound_;
	int list_base_ = 10000;  // [+0xC7DC]
	int per_type_ = 10;  // [+0xC7E0]
	int min_distance_ = 300;  // [+0xC7D0]
	int pan_distance_ = 500;  // [+0xC7D4]
	int volume_distance_ = 750;  // [+0xC7D8]
	uint32_t type_wait_ = 6000;  // [+0xC7E4]
	uint32_t slot_wait_ = 4000;  // [+0xC7E8]
	int collapse_list_ = 9000;  // [+0xC994]
	int ship_select_ = 1000;  // [+0xCBA4]
	int ship_in_out_list_ = 13000;  // [+0xCBA0]
	int church_list_ = 12000;  // [+0xCB98]
	int view_x_ = 0, view_y_ = 0, view_w_ = 0, view_h_ = 0;
	int moved_x_ = 0, moved_y_ = 0;  // The view's place the volumes were last set for ([+0xC648], [+0xC64C]).
	Slot slots_[kSlots];
	uint32_t type_until_[kTypes] = {};  // [+0xC7EC]
	// The bells: the hour of the last half past rung ([+0xCB9D]) and of the last hour ([+0xCB9C]), the strikes left
	// ([+0xCB94]) and the bell's channel ([+0xCB90]).
	int half_hour_ = -1;
	int hour_ = -1;
	int strikes_ = 0;
	int bell_channel_ = -1;
	uint32_t seed_ = 1;
	bool burning_ = false;  // set_burning
};

}  // namespace p3::app
