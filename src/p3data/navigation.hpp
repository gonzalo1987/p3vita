#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"

namespace p3::data {

// Navegación por el mar: reproducción del objeto de navegación de
// Patrician3.exe (*0x6CBDC8) y de cómo calcula una ruta entre dos puntos del
// mapa (0x445D50). Ver docs/formats/navigation.md.

// Punto del mapa del mar en píxeles (las coordenadas de barcos y ciudades).
struct MapPoint {
	int16_t x = 0;
	int16_t y = 0;

	bool operator==(const MapPoint& o) const { return x == o.x && y == o.y; }
	bool operator!=(const MapPoint& o) const { return !(*this == o); }
};

class NavData {
public:
	// Lee navdata/nav_matrix.dat (tierra y agua), nav_vec.dat (puntos de
	// navegación) y matrix_int.dat (distancia y siguiente punto entre ellos).
	static std::unique_ptr<NavData> load(const CprArchive& archive, std::string* error = nullptr);

	// Ruta de `start` a `end` como la calcula el juego: los dos puntos si se
	// ven, o el camino por los puntos de navegación con las esquinas recortadas.
	// Vacía si alguno de los dos no tiene agua cerca.
	std::vector<MapPoint> route(MapPoint start, MapPoint end) const;

	// Visión directa entre dos celdas de la rejilla (0x4445A0); el juego recorre
	// la línea desde `b` hacia `a`.
	bool line_of_sight(MapPoint a, MapPoint b) const;

	// Celda de la rejilla de un punto del mapa, y al revés.
	static int16_t to_grid(int pixel);
	static int16_t to_pixel(int cell);

	int width() const { return width_; }
	int height() const { return height_; }
	// ¿Agua en la celda (x, y) de la rejilla? Sin mirar los límites (0x4474D0 los mira antes).
	bool is_water(int x, int y) const { return water(x, y); }
	// 0x448450: the nearest water to `cell` within `radius` (the cell changes); false if none.
	bool find_water(MapPoint& cell, int radius) const;

private:
	struct Candidate {
		int32_t distance;
		int16_t point;
	};

	bool water(int x, int y) const { return grid_[size_t(y * width_ + x)] == 0; }
	std::vector<Candidate> visible_points(MapPoint cell) const;
	void best_pair(MapPoint start, MapPoint end, int& from, int& to) const;
	uint32_t distance(int a, int b) const { return distance_[size_t(a) * points_.size() + size_t(b)]; }
	int next(int a, int b) const { return next_[size_t(a) * points_.size() + size_t(b)]; }

	int width_ = 0;
	int height_ = 0;
	std::vector<uint8_t> grid_;  // 0 = agua.
	std::vector<MapPoint> points_;  // En celdas de la rejilla.
	std::vector<uint32_t> distance_;  // En 16.16.
	std::vector<int16_t> next_;
};

}  // namespace p3::data
