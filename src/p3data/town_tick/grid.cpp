// La rejilla de la ciudad (town_class1, [0x7A4] de la ciudad: [0x7A8] ancho,
// [0x7AC] alto, [0x80C] las celdas; la celda (x, y) es la x · alto + y):
// poner un edificio (0x4FB9C0) y lo que hace alrededor. Ocupa sus celdas,
// mantiene los bits de los tamaños de edificio que caben en cada celda
// (0x4FBEC0, 0x4FBFF0 con 0x4FB8F0, 0x4FC130) y abre calles hasta las que ya
// hay (0x4FBBA0, 0x4FC1D0, 0x4FC990, 0x4FC8C0, 0x4FC4A0, 0x4FC630).
//
// Una celda: & 0x3F su clase (0x0B ocupada; menos de 4, calle; 4, casi
// calle; desde 0x20, libre), 0x20 libre, los bits 0..4 los tamaños de
// edificio que caben con la esquina en ella, 0x40 la marca de la búsqueda
// de 0x4FC4A0 y 0x80 de la ciudad (dentro de la muralla). Los pasos en
// diagonal se deciden por la paridad del índice, como en el juego.

#include <cstdint>
#include <vector>

#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

namespace {

// 0x672F84 (s8): la clase de cada tipo de edificio.
constexpr int8_t kClass[64] = {3, 7, 0, 0, 4, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 10, 10, 3, 2, 3, 0, 0, 3, 0, 5, 3, 2, 2, 3, 6, 7, 6, 6, 6, 6, 6, 6, 8, 9, 8, 9, 0, 0, 0, 0x2C, 0x1B,
	0x0B, 0x08};
// 0x672F74 y 0x672F64 (s8): ancho y fondo de un edificio de cada clase.
constexpr int8_t kWidth[16] = {3, 2, 4, 3, 5, 1, 2, 6, 6, 2, 4, 5, 3, 1, 1, 1};
constexpr int8_t kDepth[16] = {2, 3, 3, 4, 4, 1, 2, 4, 2, 6, 6, 5, 3, 1, 1, 1};
// 0x672FFC (u16): dónde acaban en 0x673008 las celdas de cada clase (0..5;
// las clases mayores no llegan a 0x4FBFF0).
constexpr uint16_t kAroundEnd[6] = {228, 468, 594, 792, 1080, 1080};
// 0x673008: ternas (tamaño k, dy1, dy2) de las celdas alrededor de un
// edificio de cada clase.
constexpr int8_t kAround[1080] = {
	0, -4, -1, 0, -4, 0, 0, -4, 1, 0, -2, -1, 0, -2, 0, 0, -2, 1, 0, 2, -1, 0, 2, 0,
	0, 2, 1, 0, 4, -1, 0, 4, 0, 0, 4, 1, 0, -1, -5, 0, -1, -3, 0, -1, 3, 0, -1, 5,
	0, 0, -5, 0, 0, -3, 0, 0, 3, 0, 0, 5, 0, 1, -5, 0, 1, -3, 0, 1, 3, 0, 1, 5,
	1, -4, -1, 1, -4, 0, 1, -2, -1, 1, -2, 0, 1, 3, -1, 1, 3, 0, 1, 5, -1, 1, 5, 0,
	1, 0, -5, 1, 0, -3, 1, 0, 2, 1, 0, 4, 1, 1, -5, 1, 1, -3, 1, 1, 2, 1, 1, 4,
	3, -4, -1, 3, -4, 0, 3, -4, 1, 3, -2, -1, 3, -2, 0, 3, -2, 1, 3, 4, -1, 3, 4, 0,
	3, 4, 1, 3, 6, -1, 3, 6, 0, 3, 6, 1, 3, 0, -5, 3, 0, -3, 3, 0, 3, 3, 0, 5,
	3, 2, -5, 3, 2, -3, 3, 2, 3, 3, 2, 5, 4, -4, 0, 4, -4, 2, 4, -2, 0, 4, -2, 2,
	4, 4, 0, 4, 4, 2, 4, 6, 0, 4, 6, 2, 4, 0, -5, 4, 0, -3, 4, 0, 5, 4, 0, 7,
	4, 2, -5, 4, 2, -3, 4, 2, 5, 4, 2, 7, 0, -4, -1, 0, -4, 0, 0, -2, -1, 0, -2, 0,
	0, 3, -1, 0, 3, 0, 0, 5, -1, 0, 5, 0, 0, -1, -4, 0, -1, -2, 0, -1, 3, 0, -1, 5,
	0, 0, -4, 0, 0, -2, 0, 0, 3, 0, 0, 5, 1, -5, -1, 1, -5, 0, 1, -5, 1, 1, -3, -1,
	1, -3, 0, 1, -3, 1, 1, 3, -1, 1, 3, 0, 1, 3, 1, 1, 5, -1, 1, 5, 0, 1, 5, 1,
	1, -1, -4, 1, -1, -2, 1, -1, 2, 1, -1, 4, 1, 0, -4, 1, 0, -2, 1, 0, 2, 1, 0, 4,
	1, 1, -4, 1, 1, -2, 1, 1, 2, 1, 1, 4, 3, -5, 0, 3, -5, 1, 3, -3, 0, 3, -3, 1,
	3, 4, 0, 3, 4, 1, 3, 6, 0, 3, 6, 1, 3, 0, -4, 3, 0, -2, 3, 0, 3, 3, 0, 5,
	3, 1, -4, 3, 1, -2, 3, 1, 3, 3, 1, 5, 4, -5, 0, 4, -5, 1, 4, -5, 2, 4, -5, 3,
	4, -3, 0, 4, -3, 1, 4, -3, 2, 4, -3, 3, 4, 4, 0, 4, 4, 1, 4, 4, 2, 4, 4, 3,
	4, 6, 0, 4, 6, 1, 4, 6, 2, 4, 6, 3, 4, 0, -4, 4, 0, -2, 4, 0, 5, 4, 0, 7,
	4, 1, -4, 4, 1, -2, 4, 1, 5, 4, 1, 7, 0, 0, 5, 0, 0, 3, 0, 0, -4, 0, 0, 6,
	0, -1, 5, 0, -1, 3, 0, -1, -4, 0, -1, 6, 0, -5, 0, 0, -3, 0, 0, 2, 0, 0, 4, 0,
	0, -5, -1, 0, -3, -1, 0, 2, -1, 0, 4, -1, 1, 0, 4, 1, 0, 2, 1, 0, -4, 1, 0, -6,
	1, 3, 0, 1, 3, -2, 1, -3, 0, 1, -3, -2, 1, 5, 0, 1, 5, -2, 1, -5, 0, 1, -5, -2,
	2, 3, 2, 2, 3, -2, 2, -3, 2, 2, -3, -2, 2, 5, 2, 2, 5, -2, 2, -5, 2, 2, -5, -2,
	2, -1, -6, 2, 0, -6, 2, 1, -6, 2, -1, 6, 2, 0, 6, 2, 1, 6, 0, -6, 0, 0, -4, 0,
	0, 2, 0, 0, 4, 0, 0, 0, -5, 0, 0, -3, 0, 0, 3, 0, 0, 5, 0, -2, -5, 0, -2, -3,
	0, -2, 3, 0, -2, 5, 1, -6, -1, 1, -6, 0, 1, -4, -1, 1, -4, 0, 1, 3, -1, 1, 3, 0,
	1, 5, -1, 1, 5, 0, 1, -1, -5, 1, -1, -3, 1, -1, 5, 1, -1, 7, 1, 0, -5, 1, 0, -3,
	1, 0, 5, 1, 0, 7, 3, 2, 3, 3, -2, 3, 3, 2, -3, 3, -2, -3, 3, 2, 5, 3, -2, 5,
	3, 2, -5, 3, -2, -5, 3, -6, -1, 3, -6, 0, 3, -6, 1, 3, 6, -1, 3, 6, 0, 3, 6, 1,
	4, -6, 0, 4, -6, 1, 4, -6, 2, 4, -4, 0, 4, -4, 1, 4, -4, 2, 4, 4, 0, 4, 4, 1,
	4, 4, 2, 4, 6, 0, 4, 6, 1, 4, 6, 2, 4, -2, -5, 4, -2, -3, 4, -2, 5, 4, -2, 7,
	4, 0, -5, 4, 0, -3, 4, 0, 5, 4, 0, 7, 4, 2, -5, 4, 2, -3, 4, 2, 5, 4, 2, 7,
	0, -6, -2, 0, -6, -1, 0, -6, 0, 0, -4, -2, 0, -4, -1, 0, -4, 0, 0, 2, -2, 0, 2, -1,
	0, 2, 0, 0, 4, -2, 0, 4, -1, 0, 4, 0, 0, -2, -7, 0, -2, -5, 0, -2, 3, 0, -2, 5,
	0, -1, -7, 0, -1, -5, 0, -1, 3, 0, -1, 5, 0, 0, -7, 0, 0, -5, 0, 0, 3, 0, 0, 5,
	1, -6, -3, 1, -6, -2, 1, -6, -1, 1, -6, 0, 1, -4, -3, 1, -4, -2, 1, -4, -1, 1, -4, 0,
	1, 3, -3, 1, 3, -2, 1, 3, -1, 1, 3, 0, 1, 5, -3, 1, 5, -2, 1, 5, -1, 1, 5, 0,
	1, -1, -7, 1, -1, -5, 1, -1, 2, 1, -1, 4, 1, 0, -7, 1, 0, -5, 1, 0, 2, 1, 0, 4,
	3, -6, -2, 3, -6, -1, 3, -6, 0, 3, -4, -2, 3, -4, -1, 3, -4, 0, 3, 4, -2, 3, 4, -1,
	3, 4, 0, 3, 6, -2, 3, 6, -1, 3, 6, 0, 3, -2, -7, 3, -2, -5, 3, -2, 3, 3, -2, 5,
	3, 0, -7, 3, 0, -5, 3, 0, 3, 3, 0, 5, 3, 2, -7, 3, 2, -5, 3, 2, 3, 3, 2, 5,
	4, -6, -2, 4, -6, 0, 4, -6, 2, 4, -4, -2, 4, -4, 0, 4, -4, 2, 4, 4, -2, 4, 4, 0,
	4, 4, 2, 4, 6, -2, 4, 6, 0, 4, 6, 2, 4, -2, -7, 4, -2, -5, 4, -2, 5, 4, -2, 7,
	4, 0, -7, 4, 0, -5, 4, 0, 5, 4, 0, 7, 4, 2, -7, 4, 2, -5, 4, 2, 5, 4, 2, 7,
};
constexpr uint8_t kNorthWest = 0xE0;  // 0x67792F
constexpr uint8_t kBusy = 0x0B;  // Clase de una celda ocupada por un edificio.

// Las celdas por su índice. Fuera de la rejilla el juego lee y escribe la
// memoria de al lado; aquí se lee 0 y no se escribe.
struct Grid {
	SavedTown& st;
	int32_t w;
	int32_t h;
	explicit Grid(SavedTown& town) : st(town), w(town.memory.s32(0x7A8)), h(town.memory.s32(0x7AC)) {}
	uint8_t get(int64_t i) const { return i >= 0 && i < int64_t(st.grid.size()) ? st.grid[size_t(i)] : 0; }
	void set(int64_t i, uint8_t v) {
		if (i >= 0 && i < int64_t(st.grid.size())) {
			st.grid[size_t(i)] = v;
		}
	}
	int64_t at(int32_t x, int32_t y) const { return int64_t(h) * x + y; }
};

// La huella de un edificio: `depth` filas de `width` celdas en diagonal desde
// (x, y), cada celda ocupada (& 0x8B | 0x0B).
void occupy(Grid& g, int32_t x, int32_t y, int32_t width, int32_t depth) {
	for (int32_t r = depth; r > 0; --r) {
		int32_t cx = x;
		int32_t cy = y;
		--y;
		x -= y & 1;
		for (int32_t c = width; c > 0; --c) {
			const int64_t i = g.at(cx, cy);
			g.set(i, uint8_t((g.get(i) & 0x8B) | kBusy));
			cx += cy & 1;
			--cy;
		}
	}
}

// 0x4FB8F0: ¿cabe un edificio de tamaño k con la esquina en (x, y)? Dentro
// de la rejilla y con el bit 0x20 en todas sus celdas.
bool fn_4fb8f0(const Grid& g, int32_t x, int32_t y, uint32_t k) {
	const int32_t depth = kDepth[k];
	const int32_t width = kWidth[k];
	int32_t odd = y & 1;
	const int32_t twice = odd + 2 * x;
	if (depth > twice + 1 || 2 * g.w < twice + width || width + depth > y + 2) {
		return false;
	}
	for (int32_t r = depth - 1; r >= 0; --r) {
		int64_t i = g.at(x, y);
		for (int32_t c = width - 1; c >= 0; --c) {
			if (!(g.get(i) & 0x20)) {
				return false;
			}
			if (odd) {
				i += g.h - 1;
				odd = 0;
			} else {
				i -= 1;
				odd = 1;
			}
		}
		--y;
		odd = y & 1;
		x -= odd;
	}
	return true;
}

// 0x4FBEC0: alrededor de un edificio de la clase `cls` en (x, y), las celdas
// libres pierden el bit k (0..4) donde ya no cabe uno de tamaño k.
void fn_4fbec0(Grid& g, int32_t x, int32_t y, uint32_t cls) {
	cls &= 0xFF;
	for (int32_t k = 0; k < 5; ++k) {
		const int32_t a = kWidth[k];
		const int32_t b = kDepth[k];
		const int32_t col = b + a + y - 2;
		int64_t start = int64_t(((y & 1) - a + 2 * x + b) >> 1) * g.h + col;
		uint32_t parity = ~uint32_t(col) & 1;
		const int32_t rows = b + kDepth[cls] - 1;
		const int32_t outer = kWidth[cls] + a - 1;
		const uint8_t keep = uint8_t(~(1u << k));
		for (int32_t o = outer; o > 0; --o) {
			int64_t i = start;
			uint32_t step = parity;
			for (int32_t r = rows; r > 0; --r) {
				const uint8_t c = g.get(i);
				if (c & 0x20) {
					g.set(i, uint8_t(c & keep));
				}
				i += step ? -1 - g.h : -1;
				step = step ? 0 : 1;
			}
			start += parity ? -1 : g.h - 1;
			parity = parity ? 0 : 1;
		}
	}
}

// 0x4FBBA0 (edificios de la clase 4): de 8 sitios alrededor de (x, y), el
// primero de 0, 2, 4 y 6 con los bits 0x30 y los dos vecinos de un lado
// ocupados: ocupa allí 5 × 4 celdas, ajusta los bits de alrededor (0x4FBEC0
// con la clase 4) y devuelve su índice (los 16 bits de abajo); si no, −1.
uint32_t fn_4fbba0(Grid& g, int32_t x, int32_t y) {
	const int32_t p = y & 1;
	const int32_t px[8] = {x + 2 + p, x + 4 + p, x + 2, x - 1 + p, x - 3 + p, x - 5 + p, x - 2, x + p};
	const int32_t py[8] = {y - 5, y - 1, y + 4, y + 9, y + 5, y + 1, y - 4, y - 9};
	int64_t at[8];
	for (int k = 0; k < 8; ++k) {
		at[k] = int64_t(int32_t(uint32_t(px[k]) * uint32_t(g.h)) + py[k]);
	}
	auto busy = [&](int32_t j) { return (g.get(at[j & 7]) & 0x3F) == kBusy; };
	for (int32_t i = 0, j = -2; j < 6; i += 2, j += 2) {
		if ((g.get(at[i]) & 0x30) != 0x30) {
			continue;
		}
		if (!(busy(j + 1) && busy(j)) && !(busy(j + 3) && busy(j - 4))) {
			continue;
		}
		occupy(g, px[i], py[i], 5, 4);
		fn_4fbec0(g, px[i], py[i], 4);
		return uint16_t(uint32_t(at[i]));
	}
	return 0xFFFFFFFF;
}

// 0x4FBFF0: tras un edificio de la clase `cls` en (x, y), las celdas de
// alrededor de 0x673008 con los bits 0x80 de la del edificio donde cabe uno
// de tamaño k (0x4FB8F0) recuperan el bit k.
void fn_4fbff0(Grid& g, int32_t x, int32_t y, uint32_t cls) {
	cls &= 0xFF;
	if (cls > 5) {
		return;  // No llega: el juego leería fuera de 0x672FFC.
	}
	uint32_t from = cls ? kAroundEnd[cls - 1] : 0;
	const uint32_t to = kAroundEnd[cls];
	const uint8_t flag = g.get(g.at(x, y)) & 0x80;
	while (from < to) {
		const uint32_t k = uint8_t(kAround[from++]);
		int32_t cx = x;
		int32_t cy = y;
		const int32_t d1 = kAround[from++];
		if (d1 != 0) {
			cx = d1 > 0 ? x + (d1 & cy & 1) : x - ((cy - 1) & d1 & 1);
			cx += d1 / 2;
			cy += d1;
		}
		const int32_t d2 = kAround[from++];
		if (d2 != 0) {
			cx = d2 > 0 ? cx - ((cy - 1) & d2 & 1) : cx + (d2 & cy & 1);
			cx -= d2 / 2;
			cy += d2;
		}
		const int64_t i = g.at(cx, cy);
		if ((g.get(i) & flag) == flag && fn_4fb8f0(g, cx, cy, k)) {
			g.set(i, uint8_t(g.get(i) | (1u << k)));
		}
	}
}

// 0x4FC130: en una franja de columnas junto a x, las celdas con 0x21 cuya
// vecina (x − 1, y − 2) también lo tiene ganan el bit 8, y las de 0x22 con
// la (x + 1, y − 2), el 4.
void fn_4fc130(Grid& g, int32_t x) {
	int32_t n = g.w - x;
	n = n >= 12 ? 9 : n - 3;
	int32_t first = 3;
	if (x < 10) {
		n += x - 4;
	} else {
		first = x - 7;
		n += 6;
	}
	int64_t i = int64_t(g.h) * first;
	for (int64_t c = int64_t(g.h) * n; c > 0; --c, ++i) {
		uint8_t v = g.get(i);
		if ((v & 0x21) == 0x21 && (g.get(i - g.h - 2) & 0x21) == 0x21) {
			g.set(i, uint8_t(v | 8));
		}
		v = g.get(i);
		if ((v & 0x22) == 0x22 && (g.get(i + g.h - 2) & 0x22) == 0x22) {
			g.set(i, uint8_t(v | 4));
		}
	}
}

// 0x4FC4A0: búsqueda en anchura sobre `buf`, parejas (celda, padre: el índice
// en palabras de la pareja de la que sale; −1 las primeras), desde las
// `tail` primeras: pasa a las cuatro vecinas libres (bit 0x20 sin la marca
// 0x40, que se les pone) o de clase 4 hasta que una es calle (clase ≤ 3).
// Devuelve el índice en palabras de la pareja que la toca, o −1 (sin camino
// o sin sitio en `buf`). Al acabar quita las marcas.
int32_t fn_4fc4a0(Grid& g, std::vector<int16_t>& buf, size_t tail) {
	const size_t limit = buf.size() / 2;
	auto unmark = [&]() {
		while (tail > 0) {
			--tail;
			const int64_t c = buf[2 * tail];
			g.set(c, uint8_t(g.get(c) & 0xBF));
		}
	};
	for (size_t k = 0; k < tail; ++k) {
		const int64_t c = buf[2 * k];
		g.set(c, uint8_t(g.get(c) | 0x40));
	}
	int32_t gen = 0;
	for (size_t head = 0; head < limit; ++head, gen += 2) {
		if (head >= tail) {
			break;
		}
		const int32_t c = buf[2 * head];
		const int32_t n0 = c + 1;
		const int32_t n1 = c - 1;
		const int32_t n2 = (n0 & 1) ? n1 - g.h : n1 + g.h;
		const int32_t next[4] = {n0, n1, n2, n2 + 2};
		for (int32_t nb : next) {
			const uint8_t v = g.get(nb);
			const uint32_t cls = v & 0x7F;
			if (cls <= 3) {
				unmark();
				return gen;
			}
			if ((cls & 0x60) == 0x20) {
				g.set(nb, uint8_t(v | 0x40));
			} else if (cls != 4) {
				continue;
			}
			buf[2 * tail] = int16_t(uint16_t(nb));
			buf[2 * tail + 1] = int16_t(uint16_t(gen));
			if (++tail >= limit) {
				unmark();
				return -1;
			}
		}
	}
	unmark();
	return -1;
}

// 0x4FC630: una calle junto a la celda c. Puntúa los cuatro bloques de 2 × 2
// que la tocan por sus celdas de alrededor (libre +1, clase menor que 5 +3,
// lo demás −5; el juego cuenta dos veces la de (c ± …) + 1 y no mira otra),
// en el mejor (el primero si empatan) pasa a calle (& 0xC2 | 0x02) cada celda
// libre o de clase 4 y ajusta los bits de alrededor (0x4FBEC0, clase 6).
void fn_4fc630(Grid& g, int32_t c) {
	const int32_t h = g.h;
	const int32_t p = c & 1;
	auto score = [&](int64_t i) {
		const uint8_t v = g.get(i) & 0x3F;
		return v >= 0x20 ? 1 : v < 5 ? 3 : -5;
	};
	const int32_t l2 = p ? c + h - 1 : c - 1;
	const int32_t l4 = p ? c + h + 1 : c + 1;
	const int32_t l6 = p ? c + 1 : c - h + 1;
	const int32_t scores[4] = {score(c - 2) + score(l2) + score(l6), score(l2) + score(c + h) + score(l4),
		score(l4) + score(c + 2) + score(l6), score(l6) + score(c - h) + score(l6)};
	const int32_t targets[4] = {c, h * p + c + 1, c + 2, c - (1 - p) * h + 1};
	int32_t best = scores[0];
	int32_t t = targets[0];
	for (int k = 1; k < 4; ++k) {
		if (best < scores[k]) {
			best = scores[k];
			t = targets[k];
		}
	}
	auto street = [&](int64_t i) {
		const uint8_t v = g.get(i);
		const uint8_t cls = v & 0x3F;
		if (cls >= 0x20 || cls == 4) {
			g.set(i, uint8_t((v & 0xC2) | 2));
		}
	};
	street(t);
	street(t - 1);
	street(t - 2);
	const int32_t d = (t & 1) ? h + t - 1 : t - h - 1;
	street(d);
	const int32_t x = uint8_t(d / h);
	fn_4fbec0(g, x, d - x * h, 6);
}

// Después de 0x4FC4A0: hace calle cada celda del camino hallado (0x4FC630),
// desde la anterior a la que toca la calle hacia atrás y esa la última.
void open_path(Grid& g, const std::vector<int16_t>& buf, int32_t r) {
	if (r < 0) {
		return;
	}
	const int32_t last = buf[size_t(r)];
	for (int32_t e = buf[size_t(r) + 1]; e >= 0; e = buf[size_t(e) + 1]) {
		fn_4fc630(g, buf[size_t(e)]);
	}
	if (last >= 0 && last < g.h * g.w) {
		fn_4fc630(g, last);
	}
}

// 0x4FC8C0: si la celda i está libre, abre una calle desde ella hasta las que
// hay (0x4FC4A0 y 0x4FC630) y la celda pasa a calle (& 0x80 | 0x02).
void fn_4fc8c0(Grid& g, int32_t i) {
	if (!(g.get(i) & 0x20)) {
		return;
	}
	std::vector<int16_t> buf(2000);
	buf[0] = int16_t(uint16_t(i));
	buf[1] = -1;
	open_path(g, buf, fn_4fc4a0(g, buf, 1));
	const uint8_t v = g.get(i);
	if ((v & 0x20) == 0x20) {
		g.set(i, uint8_t((v & 0x80) | 2));
	}
}

// 0x4FC990: salvo la clase 5, el anillo de celdas alrededor de un edificio
// de la clase `cls` en (x, y) (en 16 bits, como el juego); si ninguna es ya
// calle (clase menor que 5), abre una calle desde él hasta las que hay.
void fn_4fc990(Grid& g, int32_t x, int32_t y, uint32_t cls) {
	if (cls == 5) {
		return;
	}
	std::vector<int16_t> buf(2000);
	const uint16_t h = uint16_t(g.h);
	size_t n = 0;
	auto push = [&](uint32_t c) {
		buf[2 * n] = int16_t(uint16_t(c));
		buf[2 * n + 1] = -1;
		++n;
	};
	auto last = [&]() { return uint16_t(buf[2 * (n - 1)]); };
	uint16_t first = uint16_t(uint16_t(h * uint16_t(x)) + uint32_t(y));
	if (!(y & 1)) {
		first = uint16_t(first - h);
	}
	push(uint16_t(first + 1));
	const int32_t depth = int16_t(kDepth[cls] - 1);
	const int32_t width = int16_t(kWidth[cls] - 1);
	for (int32_t k = depth; k > 0; --k) {
		uint16_t v = last();
		if (!(v & 1)) {
			v = uint16_t(v - h);
		}
		push(v - 1u);
	}
	push(last() - 2u);
	for (int32_t k = width; k > 0; --k) {
		const uint16_t v = last();
		push((v & 1) ? h + v - 1u : v - 1u);
	}
	push(uint32_t(h) + last());
	for (int32_t k = depth; k > 0; --k) {
		const uint16_t v = last();
		push((v & 1) ? h + v + 1u : v + 1u);
	}
	push(last() + 2u);
	for (int32_t k = width; k > 0; --k) {
		uint16_t v = last();
		if (!(v & 1)) {
			v = uint16_t(v - h);
		}
		push(v + 1u);
	}
	for (size_t k = 0; k < n; ++k) {
		if ((g.get(buf[2 * k]) & 0x7F) < 5) {
			return;
		}
	}
	open_path(g, buf, fn_4fc4a0(g, buf, n));
}

// 0x4FC1D0: calles junto a los lados del edificio. Por las dos diagonales de
// su borde mide los tramos de celdas ocupadas (0x0B) y, si llegan al mínimo
// de su clase, abre calle (0x4FC8C0) en las celdas libres de los extremos.
void fn_4fc1d0(Grid& g, int32_t x, int32_t y, uint32_t cls) {
	// Tablas en la pila del juego (las clases 2 y 5, nunca).
	static constexpr uint8_t kRunA[6] = {1, 4, 0xFF, 1, 3, 0xFF};
	static constexpr uint8_t kRunB[6] = {4, 1, 0xFF, 4, 1, 0xFF};
	if (cls > 5) {
		return;  // No llega: el juego leería su pila.
	}
	const int32_t h = g.h;
	const int32_t depth = kDepth[cls];
	const int32_t width = kWidth[cls];
	const int32_t xp = x + (y & 1);
	auto open_ends = [&](int64_t a, int64_t a_next, int64_t b, int64_t b_next) {
		if ((g.get(a) & 0x20) && (g.get(a_next) & 0x20)) {
			fn_4fc8c0(g, int32_t(a));
		}
		if ((g.get(b) & 0x20) && (g.get(b_next) & 0x20)) {
			fn_4fc8c0(g, int32_t(b));
		}
	};
	int64_t s1 = int64_t(xp - 1) * h + y + 1;
	int64_t s2 = int64_t((width >> 1) + x + (width & y & 1)) * h - width + y;
	for (int32_t r = depth; r > 0; --r) {
		int64_t a = s1;
		int64_t b = s2;
		if (--s1 & 1) {
			s1 -= h;
		}
		if (--s2 & 1) {
			s2 -= h;
		}
		int32_t run = 0;
		while ((g.get(a) & 0x3F) == kBusy) {
			++run;
			if (++a & 1) {
				a -= h;
			}
		}
		while ((g.get(b) & 0x3F) == kBusy) {
			++run;
			const bool odd = b & 1;
			--b;
			if (odd) {
				b += h;
			}
		}
		if (run < int32_t(kRunA[cls])) {
			continue;
		}
		int64_t a1 = a + 1;
		if (a1 & 1) {
			a1 -= h;
		}
		open_ends(a, a1, b, (b & 1) ? b - 1 + h : b - 1);
	}
	int64_t t1 = int64_t(h) * xp + y + 1;
	int64_t t2 = int64_t(x - ((y - 1) & depth & 1) - (depth >> 1)) * h - depth + y;
	for (int32_t r = width; r > 0; --r) {
		int64_t a = t1;
		int64_t b = t2;
		t1 = (t1 & 1) ? t1 - 1 + h : t1 - 1;
		t2 = (t2 & 1) ? t2 - 1 + h : t2 - 1;
		int32_t run = 0;
		while ((g.get(a) & 0x3F) == kBusy) {
			++run;
			a = (a & 1) ? a + 1 + h : a + 1;
		}
		while ((g.get(b) & 0x3F) == kBusy) {
			++run;
			if (--b & 1) {
				b -= h;
			}
		}
		if (run < int32_t(kRunB[cls])) {
			continue;
		}
		int64_t b2 = b - 1;
		if (b2 & 1) {
			b2 -= h;
		}
		open_ends(a, (a & 1) ? a + 1 + h : a + 1, b, b2);
	}
}

}  // namespace

// 0x4FB9C0 (town_class1): pone en (x, y) un edificio de tipo `type` si la
// celda tiene los bits de su clase (0x672F84): los tipos 3 y 5, su clase de
// celda; la estatua (0x2D), una celda 0x13, que pasa a 0x8B y nada más; los
// de antes de las casas, el bit de su clase y 0x20; las casas, 0xA1 (0xA2 y
// la clase 1 mirando al noroeste); los demás, el bit y 0xA0 (con la clase 8
// o más, la máscara pasa de 8 bits y no se pone nunca). Ocupa la huella,
// ajusta los bits de alrededor (0x4FBEC0) y, salvo el tipo 5, un hueco más
// para la clase 4 (0x4FBBA0), las calles (0x4FC1D0, 0x4FC990) y los bits
// que vuelven (0x4FBFF0, 0x4FC130); [0x7A4] = 1.
void fn_4fb9c0(SaveGame& save, int town, uint32_t x, uint32_t y, uint8_t type, uint8_t param) {
	SavedTown& st = save.towns[size_t(town)];
	Grid g(st);
	int32_t cls = type < 64 ? kClass[type] : 0;
	const int32_t cx = int32_t(x);
	const int32_t cy = int32_t(y);
	const int64_t at = int64_t(int32_t(uint32_t(g.h) * x + y));
	uint32_t mask = 0;
	if (type == 5) {
		mask = 0x0E;
	} else if (type == 3) {
		mask = 0x1D;
	} else if (type == 0x2D) {
		if ((g.get(at) & 0x3F) == 0x13) {
			g.set(at, 0x8B);
		}
		return;
	} else if (type < 0x15) {
		mask = (1u << (uint32_t(cls) & 31)) | 0x20;
	} else if (type <= 0x1E) {
		if (param == kNorthWest) {
			cls = 1;
			mask = 0xA2;
		} else {
			mask = 0xA1;
		}
	} else {
		mask = (1u << (uint32_t(cls) & 31)) | 0xA0;
	}
	if ((uint32_t(g.get(at)) & mask) != mask) {
		return;
	}
	occupy(g, cx, cy, kWidth[cls], kDepth[cls]);
	fn_4fbec0(g, cx, cy, uint32_t(cls));
	if (type != 5) {
		const uint32_t extra = cls == 4 ? fn_4fbba0(g, cx, cy) : 0xFFFFFFFF;
		fn_4fc1d0(g, cx, cy, uint32_t(cls));
		fn_4fc990(g, cx, cy, uint32_t(cls));
		fn_4fbff0(g, cx, cy, uint32_t(cls));
		fn_4fc130(g, cx);
		if (extra < 0x10000) {
			g.set(extra, uint8_t((g.get(extra) & 0xB0) | 0x30));
		}
	}
	st.memory.u32(0x7A4) = 1;
}

// 0x4FBDB0 (town_class1): la huella de una torre (2 × 2 celdas desde (x, y)),
// los bits de alrededor (0x4FBEC0 con la clase 6) y [0x7A4] = 1.
void fn_4fbdb0(SaveGame& save, int town, uint32_t x, uint32_t y) {
	SavedTown& st = save.towns[size_t(town)];
	Grid g(st);
	occupy(g, int32_t(x), int32_t(y), 2, 2);
	fn_4fbec0(g, int32_t(x), int32_t(y), 6);
	st.memory.u32(0x7A4) = 1;
}

}  // namespace p3::data::tick
