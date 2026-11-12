#include "p3data/navigation.hpp"

#include <array>
#include <cmath>

namespace p3::data {

namespace {

constexpr int kScale = 3536;  // 0x66E0D0: milésimas de píxel por celda.
constexpr int kBorder = 0x44;  // Margen del mapa antes de la primera celda.
constexpr int kWaterRadius = 50;
constexpr double kMaxCorner = 10.0;  // 0x66D750: radio máximo del recorte de una esquina.
constexpr double kPi = 3.14159265358979;  // 0x66D688, tal cual.

// Barreras que la visión no atraviesa aunque haya agua (estrechos y pasos que
// la rejilla no cierra): verticales x, y0..y1 (0x66D6E4, 0x66D704, 0x66D724) y
// horizontales y, x0..x1 (0x66D6C8, 0x66D690, 0x66D6AC), en celdas.
struct Barrier {
	int16_t at;
	int16_t from;
	int16_t to;
};
constexpr std::array<Barrier, 15> kVertical = {{{187, 357, 471}, {214, 0, 132}, {232, 316, 471}, {221, 200, 354},
	{281, 249, 296}, {282, 304, 319}, {308, 328, 471}, {322, 0, 274}, {350, 280, 289}, {406, 320, 471},
	{407, 168, 204}, {463, 153, 180}, {468, 0, 78}, {471, 187, 471}, {621, 22, 471}}};
constexpr std::array<Barrier, 13> kHorizontal = {{{80, 0, 50}, {82, 167, 420}, {132, 0, 56}, {133, 472, 639},
	{141, 452, 472}, {202, 294, 387}, {234, 190, 248}, {271, 260, 291}, {274, 0, 49}, {285, 346, 355},
	{285, 236, 256}, {346, 0, 73}, {409, 107, 639}}};

bool read(const CprArchive& archive, const char* path, std::vector<uint8_t>& out) {
	const CprEntry* entry = archive.find(path);
	return entry && archive.read(*entry, out);
}

uint16_t u16(const std::vector<uint8_t>& d, size_t o) {
	return uint16_t(d[o] | d[o + 1] << 8);
}

uint32_t u32(const std::vector<uint8_t>& d, size_t o) {
	return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}

int sign(int v) {
	return v < 0 ? -1 : v > 0 ? 1 : 0;
}

// Conversión a entero de MSVC (__ftol): trunca; NaN o fuera de rango da 0x80000000.
int32_t ftol(double v) {
	if (!(v > -2147483649.0 && v < 2147483648.0)) {
		return INT32_MIN;
	}
	return int32_t(v);
}

// Recorta la esquina B entre A y C (0x446F20), con las cuentas en float y
// double y las comparaciones de la FPU x87 del juego. Añade a la ruta A (si
// `first`: el quinto argumento del juego a 0) y B si la esquina es recta, o
// los dos puntos del recorte si no.
void corner(std::vector<MapPoint>& route, MapPoint a, MapPoint b, MapPoint c, bool first) {
	const float bx = float(b.x);
	const float by = float(b.y);
	const float m1x = float(double(a.x + b.x) / 2.0);
	const float m1y = float(double(a.y + b.y) / 2.0);
	const float m2x = float((double(float(c.x)) + double(bx)) / 2.0);
	const float m2y = float((double(float(c.y)) + double(by)) / 2.0);
	const float v1x = float(double(bx) - double(m1x));
	const float v1y = float(double(by) - double(m1y));
	const float v2x = float(double(m2x) - double(bx));
	const float v2y = float(double(m2y) - double(by));
	const double len1 = std::sqrt(double(v1y) * double(v1y) + double(v1x) * double(v1x));
	const double len2 = std::sqrt(double(v2y) * double(v2y) + double(v2x) * double(v2x));
	const double a1 = std::acos(double(v1x) / len1) * 180.0 / kPi;
	const double a2 = std::acos(double(v2x) / len2) * 180.0 / kPi;
	const double turn = 180.0 - a1 - a2;
	if (std::fabs(turn) < 0.5) {
		if (first) {
			route.push_back(a);
		}
		route.push_back(b);
		return;
	}
	const float t = float(std::fabs(turn / 180.0));
	const float s1 = v1x != 0.0f ? float(double(v1y) / double(v1x)) : 0.0f;
	const float s2 = v2x != 0.0f ? float(double(v2y) / double(v2x)) : 0.0f;
	double r = len1 < len2 ? double(t) * len1 : double(t) * len2;
	if (r > kMaxCorner) {
		r = kMaxCorner;
	}
	const double r2 = r * r;
	auto cut = [&](float slope, float vx, float mx, float my) {
		double q = std::sqrt(r2 / (double(slope) * double(slope) + 1.0));
		const double w = r2 - q * q;
		double p = w < 0 ? 0.0 : std::sqrt(w);
		if (vx == 0.0f) {
			q = 0.0;
			p = r;
		}
		const int32_t x = ftol((mx > bx ? double(bx) + q : double(bx) - q) + 0.5);
		const int32_t y = ftol((my > by ? double(by) + p : double(by) - p) + 0.5);
		return MapPoint{int16_t(x), int16_t(y)};
	};
	const MapPoint p1 = cut(s1, v1x, m1x, m1y);
	const MapPoint p2 = cut(s2, v2x, m2x, m2y);
	route.push_back(p1);
	if (p2 != p1) {
		route.push_back(p2);
	}
}

// Final de 0x445D50: quita los puntos repetidos seguidos. Como el juego, tras
// quitar uno no compara el siguiente.
void remove_repeats(std::vector<MapPoint>& route) {
	MapPoint previous{-1, -1};
	for (size_t i = 0; i < route.size(); ++i) {
		if (route[i] == previous) {
			route.erase(route.begin() + std::ptrdiff_t(i) - 1);
		} else {
			previous = route[i];
		}
	}
}

}  // namespace

std::unique_ptr<NavData> NavData::load(const CprArchive& archive, std::string* error) {
	std::vector<uint8_t> matrix, vec, cells;
	if (!read(archive, "navdata/nav_matrix.dat", matrix) || !read(archive, "navdata/nav_vec.dat", vec) ||
		!read(archive, "navdata/matrix_int.dat", cells) || matrix.size() < 4 || vec.size() < 4) {
		if (error) {
			*error = "no se encuentran los datos de navegación (navdata/)";
		}
		return nullptr;
	}
	auto nav = std::unique_ptr<NavData>(new NavData());
	nav->width_ = u16(matrix, 0);
	nav->height_ = u16(matrix, 2);
	const size_t count = u16(vec, 0);
	if (matrix.size() < 4 + size_t(nav->width_) * size_t(nav->height_) || vec.size() < 4 + count * 4 ||
		cells.size() < count * count * 6) {
		if (error) {
			*error = "datos de navegación truncados";
		}
		return nullptr;
	}
	nav->grid_.assign(matrix.begin() + 4, matrix.begin() + 4 + std::ptrdiff_t(nav->width_ * nav->height_));
	nav->points_.resize(count);
	for (size_t i = 0; i < count; ++i) {
		nav->points_[i] = {int16_t(u16(vec, 4 + i * 4)), int16_t(u16(vec, 6 + i * 4))};
	}
	nav->distance_.resize(count * count);
	nav->next_.resize(count * count);
	for (size_t i = 0; i < count * count; ++i) {
		nav->distance_[i] = u32(cells, i * 6);
		nav->next_[i] = int16_t(u16(cells, i * 6 + 4));
	}
	return nav;
}

int16_t NavData::to_grid(int pixel) {
	return int16_t(((pixel - kBorder) * 1000 + kScale / 2) / kScale);
}

int16_t NavData::to_pixel(int cell) {
	return int16_t((cell * kScale + 500) / 1000 + kBorder);
}

bool NavData::line_of_sight(MapPoint a, MapPoint b) const {
	const int x1 = b.x, y1 = b.y, x2 = a.x, y2 = a.y;
	if (x1 == x2 && y1 == y2) {
		return true;
	}
	// ¿Cruza el segmento alguna barrera? El juego no distingue el sentido.
	auto crosses = [](const Barrier& bar, int u1, int v1, int u2, int v2) {
		if ((bar.at < u1 && bar.at < u2) || (bar.at > u1 && bar.at > u2)) {
			return false;
		}
		if ((v1 < bar.from && v2 < bar.from) || (v1 > bar.to && v2 > bar.to)) {
			return false;
		}
		const int du = u2 - u1;
		const int t = (bar.at - u1) * (v2 - v1) + (du >> 1);
		return t >= (bar.from - v1) * du && t <= (bar.to - v1) * du;
	};
	if (x1 != x2) {
		for (const Barrier& bar : kVertical) {
			if (crosses(bar, x1, y1, x2, y2)) {
				return false;
			}
		}
	}
	if (x1 == x2 || y1 != y2) {
		for (const Barrier& bar : kHorizontal) {
			if (crosses(bar, y1, x1, y2, x2)) {
				return false;
			}
		}
	}

	// Bresenham desde b; en las esquinas exactas, si la celda intermedia es
	// agua no se mira la diagonal. Ni el primer punto ni el último se miran.
	const int dx = x2 - x1, dy = y2 - y1;
	const int ax = std::abs(dx), ay = std::abs(dy);
	const int sx = sign(dx), sy = sign(dy);
	int x = x1, y = y1;
	const bool x_major = ax >= ay;
	const int major = x_major ? ax : ay;
	const int minor = x_major ? ay : ax;
	int error = major;
	for (int step = 0; step < major - 1; ++step) {
		(x_major ? x : y) += x_major ? sx : sy;
		error += 2 * minor;
		if (error >= 2 * major) {
			error -= 2 * major;
			const bool skip = error == 0 && water(x, y);
			(x_major ? y : x) += x_major ? sy : sx;
			if (skip) {
				continue;
			}
		}
		if (!water(x, y)) {
			return false;
		}
	}
	return true;
}

// 0x448450: la celda si es agua y, si no, el agua más cercana buscando en
// anillos cuadrados de radio creciente.
bool NavData::find_water(MapPoint& cell, int radius) const {
	const int x = cell.x, y = cell.y;
	// El juego acepta también x == ancho o y == alto, fuera de la rejilla; aquí no.
	if (x < 0 || y < 0 || x >= width_ || y >= height_) {
		return false;
	}
	if (water(x, y)) {
		return true;
	}
	int best = INT32_MAX;
	MapPoint found{};
	for (int r = 1;;) {
		for (int u = -r; u <= r; ++u) {
			for (int v = -r; v <= r;) {
				const int cx = x + u, cy = y + v;
				if (cy >= 0 && cy < height_ && cx >= 0 && cx < width_ && water(cx, cy) && u * u + v * v < best) {
					best = u * u + v * v;
					found = {int16_t(cx), int16_t(cy)};
				}
				v = (u != -r && u != r && v != r) ? r : v + 1;
			}
		}
		++r;
		if (best <= r * r || (r > radius && best < INT32_MAX)) {
			cell = found;
			return true;
		}
		if (r > radius) {
			return false;
		}
	}
}

// Puntos de navegación visibles desde una celda, con su distancia en 16.16
// redondeada, ordenados como los deja el juego (0x444800).
std::vector<NavData::Candidate> NavData::visible_points(MapPoint cell) const {
	std::vector<Candidate> list;
	for (size_t k = 0; k < points_.size(); ++k) {
		const MapPoint p = points_[k];
		if (!line_of_sight(cell, p)) {
			continue;
		}
		const int64_t dx = cell.x - p.x, dy = cell.y - p.y;
		const int32_t d = ftol(std::sqrt(double((dx * dx + dy * dy) << 32)) + 0.5);
		size_t i = 0;
		while (i < list.size() && !(list[i].distance < d)) {
			++i;
		}
		list.insert(list.begin() + std::ptrdiff_t(i), Candidate{d, int16_t(k)});
	}
	return list;
}

// El par de puntos de navegación con menor recorrido total; a igualdad, el de
// menor tramo por la matriz.
void NavData::best_pair(MapPoint start, MapPoint end, int& from, int& to) const {
	const std::vector<Candidate> a = visible_points(start);
	const std::vector<Candidate> b = visible_points(end);
	int32_t best = INT32_MAX;
	uint32_t best_middle = INT32_MAX;
	from = to = 0;
	for (const Candidate& ca : a) {
		for (const Candidate& cb : b) {
			const uint32_t middle = distance(ca.point, cb.point);
			const int32_t total = int32_t(uint32_t(cb.distance) + uint32_t(ca.distance) + middle);
			if (total > best || (total == best && int32_t(best_middle) < int32_t(middle))) {
				continue;
			}
			best = total;
			best_middle = middle;
			from = ca.point;
			to = cb.point;
		}
	}
}

std::vector<MapPoint> NavData::route(MapPoint start, MapPoint end) const {
	MapPoint gs{to_grid(start.x), to_grid(start.y)};
	MapPoint ge{to_grid(end.x), to_grid(end.y)};
	if (!find_water(gs, kWaterRadius) || !find_water(ge, kWaterRadius)) {
		return {};
	}
	std::vector<MapPoint> route;
	if (line_of_sight(gs, ge)) {
		route = {start, end};
	} else {
		int current = 0, target = 0;
		best_pair(gs, ge, current, target);
		auto pixel = [&](int point) {
			return MapPoint{to_pixel(points_[size_t(point)].x), to_pixel(points_[size_t(point)].y)};
		};
		route.push_back(start);
		MapPoint previous = start;
		MapPoint here = pixel(current);
		while (current != target) {
			const int following = next(current, target);
			const MapPoint there = pixel(following);
			corner(route, previous, here, there, true);
			previous = here;
			here = there;
			current = following;
		}
		corner(route, previous, here, end, true);
		route.push_back(end);
	}
	remove_repeats(route);
	return route;
}

bool NavData::route_through(const MapPoint* points, size_t count, std::vector<MapPoint>& route, MapPoint& failed) const {
	auto pixel = [&](int point) {
		return MapPoint{to_pixel(points_[size_t(point)].x), to_pixel(points_[size_t(point)].y)};
	};
	// The stretches, their corners not cut yet.
	std::vector<MapPoint> raw{points[0]};
	for (size_t i = 1; i < count; ++i) {
		const MapPoint a = points[i - 1];
		const MapPoint b = points[i];
		MapPoint ga{to_grid(a.x), to_grid(a.y)};
		MapPoint gb{to_grid(b.x), to_grid(b.y)};
		if (!find_water(ga, kWaterRadius) || !find_water(gb, kWaterRadius)) {
			failed = a;
			return false;
		}
		if (!line_of_sight(ga, gb)) {
			int current = 0, target = 0;
			best_pair(ga, gb, current, target);
			raw.push_back(pixel(current));
			while (current != target) {
				current = next(current, target);
				raw.push_back(pixel(current));
			}
		}
		raw.push_back(b);
	}
	// The corners (0x446F20), from the first point different from the first; as the game, the third point taken is
	// always the raw route's third, wherever that second one was.
	route.clear();
	route.push_back(raw[0]);
	MapPoint a = raw[0];
	size_t k = 1;
	while (k < raw.size() && raw[k] == a) {
		++k;
	}
	// The game's last point is the last one the loop read: with every point the same (four or more clicks on the
	// ship itself), a word left on its stack; here the raw route's last.
	MapPoint last = raw.back();
	if (k < raw.size()) {
		MapPoint b = raw[k];
		for (size_t c = 2; c < raw.size(); ++c) {
			const MapPoint p = raw[c];
			last = p;
			if (p == b) {
				continue;
			}
			corner(route, a, b, p, false);  // the game's flag 1: A not added again
			a = b;
			b = p;
		}
	}
	route.push_back(last);
	remove_repeats(route);
	return true;
}

}  // namespace p3::data
