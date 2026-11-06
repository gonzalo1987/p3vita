#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/savegame.hpp"
#include "p3data/town.hpp"

namespace p3::data {

// Región de cada ciudad (0..39) según scripts/StadtDaten.ini. De ella depende
// el estilo de murallas, casas, torres e iglesias.
std::vector<int> load_town_regions(const CprArchive& archive);

// Coloca en la capa de objetos de `town` (el .town de la plantilla) los edificios
// y objetos fijos de una ciudad guardada, quitando los objetos naturales que pisen
// (docs/formats/savegame.md, §8). `editor`, el .editor de la plantilla si existe,
// solo da el modelo de las grúas. Devuelve cuántos objetos no tienen tile.
//
// The buildings come from the town's current state, as the game draws them
// (0x521580): the record lists [0x762], [0x764], [0x768] and [0x76A] of its
// memory, the fixed points [0x7B0..0x80B], the flags [0x76C] and the church
// [0x7A3]. So whatever the simulation has built since loading is drawn too.
// The kontors drawn at the fixed points e[2..5] (savegame.md §8.5): the
// player's variant (0x4F74F0: his rank in his home town, 0..4), -1 without an
// office of his in the town, and whether another merchant has one.
struct KontorView {
	int player_variant = -1;
	bool rival = false;
};
KontorView kontor_view(const SaveGame& save, size_t town);

// With `owners`, each cell's building owner as the objects drawn on it (a record's owner byte: a merchant, or the
// town at the merchants' count and above; kRivalOwner the rival kontor; kNoBuildingOwner no building), for the
// viewer's shading of the competition's buildings (the top bar's house).
constexpr uint8_t kNoBuildingOwner = 0xFF;
constexpr uint8_t kRivalOwner = 0xFE;
// `office_heads`: the heads of the town's offices' lists (office_list_heads), drawn after the town's.
size_t place_saved_buildings(const SavedTown& saved, int merchant_count, int region, const TileSet& tiles,
	const TownMap* editor, TownMap& town, const KontorView* kontors = nullptr, std::vector<uint8_t>* owners = nullptr,
	const std::vector<uint16_t>& office_heads = {});
// The heads of the record lists of the town's offices (0x521580: along the town's [0x784] and each office's
// [+0x2CA], its [+0x2CE] and [+0x2D0]), in the game's drawing order.
std::vector<uint16_t> office_list_heads(const SaveGame& save, size_t town);

// A key of everything place_saved_buildings reads from the town: when it
// changes, the town has to be drawn again (the simulation has built,
// finished, changed or burnt something). With the save, also its kontors.
uint64_t drawn_buildings_key(const SavedTown& saved, const std::vector<uint16_t>& office_heads = {});
uint64_t drawn_buildings_key(const SaveGame& save, size_t index);

// The template of a saved town (its .town, its .editor if there is one, and
// its region), on which its buildings are placed again whenever they change.
struct SavedTownTemplate {
	TownMap town;
	TownMap editor;
	bool has_editor = false;
	int region = 0;
};

bool load_saved_town_template(const CprArchive& archive, const SavedTown& saved, const std::vector<int>& regions,
	SavedTownTemplate& out, std::string* error = nullptr);

// The template with the buildings of the town `index` of the save as they are now.
void place_saved_town(const SavedTownTemplate& base, const SaveGame& save, size_t index, const TileSet& tiles,
	TownMap& out, std::vector<uint8_t>* owners = nullptr);

// Carga la plantilla de la ciudad `index` de la partida y coloca sus edificios.
bool load_saved_town(const CprArchive& archive, const SaveGame& save, size_t index, const TileSet& tiles,
	const std::vector<int>& regions, TownMap& out, std::string* error = nullptr);

}  // namespace p3::data
