#pragma once

#include <array>
#include <cstdint>

#include "p3data/prices.hpp"
#include "p3data/wares.hpp"

namespace p3::data {

// Umbrales de precio de una ciudad: reproducción de update_town_price_thresholds
// (0x528070) de Patrician3.exe, que el juego ejecuta en cada tick de la ciudad y
// también al cargarla (ver docs/formats/savegame.md, §4.1).

// Lo que la función lee de la ciudad.
struct ThresholdInputs {
	uint32_t flags = 0;  // Situación de la ciudad (campo 0x2C8): asedio, incendio, hambruna...
	uint32_t citizens = 0;
	std::array<uint32_t, kWareCount> business_consumption{};
	std::array<uint32_t, kWareCount> citizen_consumption{};
	std::array<uint32_t, kWareCount> production{};
	// Array sin identificar de 0x490 (P3Modding: "weird prods"); se parece a la producción.
	std::array<uint32_t, kWareCount> supply{};
	int month = 0;  // 0 = enero.
	// u16 de la instalación 18 (campo 0x968); por encima de 300 cambian los
	// umbrales altos de los ladrillos. Al cargar una partida aún no está leído y vale 0.
	uint16_t brickworks = 0;
};

// Calcula los umbrales de las 24 mercancías. No incluye el aumento de la brea
// durante un asedio, que depende de un objeto del asedio que no se lee.
std::array<PriceThresholds, kWareCount> compute_price_thresholds(const ThresholdInputs& in,
	const std::array<PriceThresholds, kWareCount>& previous);

}  // namespace p3::data
