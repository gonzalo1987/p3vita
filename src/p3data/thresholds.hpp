#pragma once

#include <array>
#include <cstdint>

#include "p3data/prices.hpp"
#include "p3data/wares.hpp"

namespace p3::data {

// A town's price thresholds: update_town_price_thresholds (0x528070) of
// Patrician3.exe, which the game runs in each town tick and also when it
// loads the town (see docs/formats/savegame.md, §4.1).

// What the function reads of the town.
struct ThresholdInputs {
	uint32_t flags = 0;  // The town's situation (field 0x2C8): siege, fire, famine...
	uint32_t citizens = 0;
	std::array<uint32_t, kWareCount> business_consumption{};
	std::array<uint32_t, kWareCount> citizen_consumption{};
	std::array<uint32_t, kWareCount> production{};
	// Unidentified array at 0x490 (P3Modding: "weird prods"); it looks like the production.
	std::array<uint32_t, kWareCount> supply{};
	int month = 0;  // 0 = January.
	// u16 of facility 18 (field 0x968); over 300 the bricks' high thresholds
	// change. When a save is loaded it isn't read yet and is 0.
	uint16_t brickworks = 0;
	// The pitch the besieging army uses each tick (0x636920 on the town's
	// siege object), with the bit 0x10 of `flags` and the siege objects in
	// place (*0x6DF270): pitch's first two thresholds rise by 7 and 14 times
	// it (0x5281A0). 0 when a save is loaded (the objects don't exist yet).
	uint32_t siege_pitch = 0;
};

// The thresholds of the 24 wares.
std::array<PriceThresholds, kWareCount> compute_price_thresholds(const ThresholdInputs& in,
	const std::array<PriceThresholds, kWareCount>& previous);

}  // namespace p3::data
