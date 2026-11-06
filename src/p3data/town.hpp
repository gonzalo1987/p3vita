#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "p3data/cpr.hpp"

namespace p3::data {

// Geometría de la rejilla isométrica escalonada de las ciudades: rombos de
// 34x21 y filas cada 11 píxeles, con las impares desplazadas media celda.
constexpr int kTileWidth = 34;
constexpr int kTileHeight = 21;
constexpr int kRowStep = 11;

inline int cell_screen_x(int x, int y) {
	return x * kTileWidth + (y & 1) * (kTileWidth / 2);
}

inline int cell_screen_y(int y) {
	return y * kRowStep;
}

// Franja vertical de un tile, relativa a su rectángulo en el atlas. Cada franja
// se dibuja en una celda distinta de la huella para ordenar bien la profundidad.
struct TileSlice {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

// Tile isométrico (.let, ver docs/formats/town.md).
struct TileDef {
	std::string name;  // Nombre del fichero sin carpeta ni extensión.
	uint16_t id = 0;
	uint32_t type = 0;  // Clase de objeto (la "NewBuildingId" de P3Modding).
	// Parámetros tras la cabecera con los que el juego elige el tile de un
	// edificio: variante, orientación, región... (docs/formats/savegame.md).
	std::vector<uint32_t> params;
	int src_x = 0;  // Rectángulo dentro del atlas.
	int src_y = 0;
	int width = 0;
	int height = 0;
	int cols = 1;
	int rows = 1;
	std::vector<TileSlice> slices;  // cols + rows - 1 franjas.
	size_t front_slice = 0;  // La que cubre la celda delantera, que es la más baja.
	std::string atlas;  // Ruta del AIM dentro del CPR.

	// Nodos de rutas (tipo 48) y puntos de aparición (tipo 49): solo los ve el editor.
	bool is_editor_marker() const { return type == 48 || type == 49; }
};

bool parse_let(const uint8_t* data, size_t size, TileDef& out, std::string* error = nullptr);

struct TownCell {
	uint16_t tile = 0;  // 0 = vacía.
	uint16_t aux = 0;  // Byte bajo: franja del tile que va en esta celda (0xFF = ninguna).
};

// Mapa de una ciudad (.town): capa de suelo y capa de objetos.
struct TownMap {
	int width = 0;
	int height = 0;
	std::vector<TownCell> ground;
	std::vector<TownCell> objects;
};

bool parse_town(const uint8_t* data, size_t size, TownMap& out, std::string* error = nullptr);

// Trozo de sprite que hay que dibujar, en coordenadas de píxel de la ciudad.
struct TownSprite {
	const TileDef* tile = nullptr;
	int src_x = 0;  // Dentro del atlas.
	int src_y = 0;
	int width = 0;
	int height = 0;
	int dst_x = 0;
	int dst_y = 0;
	int cell_x = 0;  // The cell it stands on.
	int cell_y = 0;
	bool object = false;  // Of the objects' layer (not the ground's).
};

using TileSet = std::unordered_map<uint16_t, TileDef>;

// Carga todos los iso/let/*.let del archivo. Once ids están repetidos; gana el
// último del archivo, que es lo que encaja con los mapas (p. ej. el 570 es un
// borde de calle en el suelo, no el árbol con el que comparte id).
TileSet load_tileset(const CprArchive& archive);

// Ids de las ciudades del archivo (iso/towns/<id>.town), en orden numérico.
std::vector<int> list_town_ids(const CprArchive& archive);

// Mapa iso/towns/<id>.town. Con `with_buildings`, la versión .editor, que
// tiene el mismo suelo y además los edificios en la capa de objetos.
bool load_town(const CprArchive& archive, int id, TownMap& out, std::string* error = nullptr,
	bool with_buildings = false);

// Lista de dibujado en orden: todo el suelo y después los objetos, cada capa
// fila a fila de atrás hacia delante. Se omiten las celdas con tiles
// desconocidos y los marcadores del editor.
std::vector<TownSprite> build_town_sprites(const TownMap& town, const TileSet& tiles);

}  // namespace p3::data
