#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"

namespace p3::data {

// Mapa del mar: la imagen de fondo y lo que se dibuja encima. Las posiciones
// de los barcos de la partida (x, y en 16.16) y las de las ciudades están en
// píxeles de esta imagen.
constexpr char kSeaMapImage[] = "images/Vollansichtskarte.bmp";  // 2400 × 1800 (TEX16036)
constexpr char kTownSymbols[] = "images/p_global_stadt01.tga";  // 3 × 3 símbolos de 26 × 26 (TEX30021)
constexpr int kTownSymbolSize = 26;
// Barcos vistos desde arriba: 16 rumbos de 52 × 56 en una rejilla de 4 × 4,
// en el orden del byte de rumbo del barco (1 oeste, 5 norte, 9 este, 13 sur).
constexpr const char* kShipSprites[4] = {"images/Schiffe/page_mschnigge01.tga", "images/Schiffe/page_mkraier01.tga",
	"images/Schiffe/page_mkogge01.tga", "images/Schiffe/page_mholk01.tga"};
constexpr int kShipSpriteWidth = 52;
constexpr int kShipSpriteHeight = 56;
constexpr char kShipMarker[] = "images/Schiffe/schiffmarker01.tga";  // 16 × 22 (TEX30008)

// Posiciones de una ciudad en el mapa (scripts/StadtDaten.ini; el juego las
// guarda en 0x6DDB90 + id · 0x34).
struct SeaMapTown {
	int symbol_x = 0;  // StadtSymbolXPos/YPos.
	int symbol_y = 0;
	int name_x = 0;  // NamenXPos/YPos.
	int name_y = 0;
	bool name_right_aligned = false;  // NameLinksOderRechtsBuendig=r.
	int approach_x = 0;  // AnfahrtXPos/YPos: donde atracan los barcos; las rutas empiezan y acaban aquí.
	int approach_y = 0;
};

// Las 40 ciudades en el orden de sus ids; vacío si no se encuentra el fichero.
std::vector<SeaMapTown> load_sea_map_towns(const CprArchive& archive);

}  // namespace p3::data
