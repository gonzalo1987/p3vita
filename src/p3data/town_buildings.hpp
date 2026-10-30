#pragma once

#include <cstddef>
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
size_t place_saved_buildings(const SavedTown& saved, int merchant_count, int region, const TileSet& tiles,
	const TownMap* editor, TownMap& town);

// Carga la plantilla de la ciudad `index` de la partida y coloca sus edificios.
bool load_saved_town(const CprArchive& archive, const SaveGame& save, size_t index, const TileSet& tiles,
	const std::vector<int>& regions, TownMap& out, std::string* error = nullptr);

}  // namespace p3::data
