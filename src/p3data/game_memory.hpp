#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace p3::data {

// Objetos del juego con la misma disposición en memoria que en
// Patrician3.exe. La simulación de las ciudades toca cientos de campos, muchos
// sin identificar, y se reproduce leyendo el ejecutable: así el código usa los
// mismos offsets que el original y el oráculo puede comparar el objeto entero
// byte a byte (tools/oracle/tick_probe.cpp).
//
// Los campos se leen y escriben por offset: `town.u32(0x2C8) |= 2`. Los
// punteros del juego que contienen no se usan (los datos a los que apuntan
// van aparte, p. ej. SavedTown::records); conservan el valor que tenían en el
// emulador o 0.
//
// p3data se compila con -fno-strict-aliasing para poder acceder a los bytes
// con referencias de otros tipos; los campos del juego están alineados a su
// tamaño (MSVC), lo que también exige la Vita para los float.
template <size_t N>
struct GameMemory {
	static constexpr size_t kSize = N;

	alignas(8) std::array<uint8_t, N> bytes{};

	template <class T>
	T& at(size_t offset) {
		assert(offset + sizeof(T) <= N);
		return *reinterpret_cast<T*>(bytes.data() + offset);
	}
	template <class T>
	const T& at(size_t offset) const {
		assert(offset + sizeof(T) <= N);
		return *reinterpret_cast<const T*>(bytes.data() + offset);
	}

	uint8_t& u8(size_t o) { return at<uint8_t>(o); }
	int8_t& s8(size_t o) { return at<int8_t>(o); }
	uint16_t& u16(size_t o) { return at<uint16_t>(o); }
	int16_t& s16(size_t o) { return at<int16_t>(o); }
	uint32_t& u32(size_t o) { return at<uint32_t>(o); }
	int32_t& s32(size_t o) { return at<int32_t>(o); }
	float& f32(size_t o) { return at<float>(o); }
	double& f64(size_t o) { return at<double>(o); }

	uint8_t u8(size_t o) const { return at<uint8_t>(o); }
	int8_t s8(size_t o) const { return at<int8_t>(o); }
	uint16_t u16(size_t o) const { return at<uint16_t>(o); }
	int16_t s16(size_t o) const { return at<int16_t>(o); }
	uint32_t u32(size_t o) const { return at<uint32_t>(o); }
	int32_t s32(size_t o) const { return at<int32_t>(o); }
	float f32(size_t o) const { return at<float>(o); }
	double f64(size_t o) const { return at<double>(o); }
};

// Variables globales del juego entre 0x6DD000 y 0x6E0000, por su dirección
// absoluta: el mundo (0x6DE4A0: fecha, número de ciudades, tick...), el
// objeto de los barcos (0x6DD7A0), el jugador (0x6DFC14)...
struct GlobalMemory {
	static constexpr uint32_t kBegin = 0x6DD000;
	static constexpr uint32_t kEnd = 0x6E0000;

	GameMemory<kEnd - kBegin> memory;

	uint8_t& u8(uint32_t a) { return memory.u8(offset(a)); }
	int8_t& s8(uint32_t a) { return memory.s8(offset(a)); }
	uint16_t& u16(uint32_t a) { return memory.u16(offset(a)); }
	int16_t& s16(uint32_t a) { return memory.s16(offset(a)); }
	uint32_t& u32(uint32_t a) { return memory.u32(offset(a)); }
	int32_t& s32(uint32_t a) { return memory.s32(offset(a)); }
	float& f32(uint32_t a) { return memory.f32(offset(a)); }

	uint8_t u8(uint32_t a) const { return memory.u8(offset(a)); }
	int8_t s8(uint32_t a) const { return memory.s8(offset(a)); }
	uint16_t u16(uint32_t a) const { return memory.u16(offset(a)); }
	int16_t s16(uint32_t a) const { return memory.s16(offset(a)); }
	uint32_t u32(uint32_t a) const { return memory.u32(offset(a)); }
	int32_t s32(uint32_t a) const { return memory.s32(offset(a)); }
	float f32(uint32_t a) const { return memory.f32(offset(a)); }

private:
	static size_t offset(uint32_t a) {
		assert(a >= kBegin && a < kEnd);
		return a - kBegin;
	}
};

// Direcciones de globales que usa la simulación.
namespace global {
constexpr uint32_t kWorld = 0x6DE4A0;  // Objeto del mundo (ver savegame.md §3).
constexpr uint32_t kWorldDay = 0x6DE4A0;  // u8 día del mes (1..31).
constexpr uint32_t kWorldMonth = 0x6DE4A1;  // u8 mes (0 = enero).
constexpr uint32_t kWorldYear = 0x6DE4A2;  // u16
constexpr uint32_t kWorldDayOfYear = 0x6DE4A4;  // u16
constexpr uint32_t kWorldOfficeCount = 0x6DE4A8;  // u16
constexpr uint32_t kWorldMerchantCount = 0x6DE4AA;  // u16
constexpr uint32_t kWorldTownCount = 0x6DE4B0;  // u32
constexpr uint32_t kWorldTick = 0x6DE4B4;  // u32: 256 por día.
constexpr uint32_t kShips = 0x6DD7A0;  // Objeto de los barcos.
constexpr uint32_t kCaptainCount = 0x6DD892;  // u16 ([+0xF2] del objeto de los barcos).
constexpr uint32_t kShipCount = 0x6DD894;  // u16 ([+0xF4]).
constexpr uint32_t kPlayer = 0x6DFC14;  // u32 comerciante del jugador.
}  // namespace global

using TownMemory = GameMemory<0x9F8>;  // struct town
using OfficeMemory = GameMemory<0x44C>;  // Oficina (kontor) de un comerciante.
using MerchantMemory = GameMemory<0x650>;  // struct merchant
using CaptainMemory = GameMemory<0x10>;  // Capitanes y administradores (*0x6DD7A0).
using SiegeMemory = GameMemory<0x1024>;  // Objeto de cada ciudad en *0x6DF270 (asedios).
using SiegeArmyMemory = GameMemory<0x650>;  // Ejército que sitia una ciudad ([+0x8] de su asedio, 0x627880).
using SettingsMemory = GameMemory<0x4C>;  // Ajustes de la partida (*0x6CC3E8).

}  // namespace p3::data
