#pragma once

// The arithmetic the battle code shares (battle_step.cpp, ship_step.cpp):
// the game's tables of the arctangent and the sine, its 32-bit products and
// its square root.

#include <cmath>
#include <cstdint>

namespace p3::data::battle_math {

// 0x67AA28: the arctangent for tan from 0 to 1 in 64 steps, in 256ths of a turn.
inline constexpr uint8_t kAtan[66] = {0, 1, 1, 2, 3, 3, 4, 4, 5, 6, 6, 7, 8, 8, 9, 9, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15,
	16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24, 25, 25, 25, 26, 26, 27, 27, 27, 28, 28,
	29, 29, 29, 30, 30, 30, 31, 31, 31, 32, 32, 0};

// 0x67ABB0: the sine of a quarter turn in 64 steps (65536 = 1).
inline constexpr int32_t kSin[65] = {0, 1608, 3216, 4821, 6424, 8022, 9616, 11204, 12785, 14359, 15924, 17479, 19024, 20557,
	22078, 23586, 25080, 26558, 28020, 29466, 30893, 32303, 33692, 35062, 36410, 37736, 39040, 40320, 41576, 42806,
	44011, 45190, 46341, 47464, 48559, 49624, 50660, 51665, 52639, 53581, 54491, 55368, 56212, 57022, 57798, 58538,
	59244, 59914, 60547, 61145, 61705, 62228, 62714, 63162, 63572, 63944, 64277, 64571, 64827, 65043, 65220, 65358,
	65457, 65516, 65536};

// The sine of a direction in 256ths of a turn, as the battle code reads kSin.
inline int32_t sine(uint32_t a) {
	a &= 0xFF;
	if (a < 0x80) {
		return a <= 0x40 ? kSin[a] : kSin[0x80 - a];
	}
	const uint32_t b = a - 0x80;
	return -(b <= 0x40 ? kSin[b] : kSin[0x100 - a]);
}
inline int32_t cosine(uint32_t a) {
	return sine(a + 0x40);
}

// 32-bit products as the game's imul.
inline int32_t mul(int32_t a, int32_t b) {
	return int32_t(uint32_t(a) * uint32_t(b));
}

// ftol (0x639C28) of the square root (0x63AAE4) of an int: a negative one
// gives NaN, whose integer indefinite has 0 in its low 32 bits.
inline int32_t root(int32_t v) {
	return v < 0 ? 0 : int32_t(std::sqrt(double(v)));
}

}  // namespace p3::data::battle_math
