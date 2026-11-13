#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "p3data/player_actions.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// The arsenal (.let 0x0B, the window *0x6E552C: its update 0x5B0930, its drawing 0x5B0CC0): the town's militia, only
// for the town's mayor ([+0x6F1] the player; else the warning 0x69DD1C and nothing else). For each type k (the town
// watch, archers, crossbowmen, musketeers) the soldiers on duty [+0x998 + k], those in training [+0x99C + k] and
// their weekly cost; «Form unit» (0x41) with a type chosen, «Dismiss unit» (0x40) with soldiers of it on duty.
enum class ArsenalMessage : uint8_t {
	NoWeapons = 0,   // 0x6AA450: the office (or, without one, the town) has fewer than 50 of the type's weapons
	NoBeggars = 1,   // 0x69DD24: fewer than 20 beggars in the town ([+0x2E4])
	NoCouncil = 2,   // 0x6AA448: with [+0x2C8] & 0xE0, the units at the council's total [+0x9A0]
	Authorised = 3,  // 0x6AA44C: «the town council has authorised a total of %u units» ([+0x9A0])
	None = 0xFF,
};

constexpr uint32_t kArsenalTypes = 4;

// Whether the player is the town's mayor (the window's [+0x38A]).
bool arsenal_mayor(const SaveGame& save, uint32_t town);
// A type's weekly cost: (on duty + in training) · 0x672DB0[k] (33, 26, 40, 46) · 7.
int32_t arsenal_cost(const SaveGame& save, uint32_t town, uint32_t type);
// The message when the window opens: Authorised with [+0x2C8] & 0xE0, else None.
ArsenalMessage arsenal_open_message(const SaveGame& save, uint32_t town);
// «Form unit» (0x5B0AD1): the weapons (the player's office in the town, else the town, at least 50: NoWeapons), the
// beggars (at least 20: NoBeggars, over it), with [+0x2C8] & 0xE0 the units under the council's total (NoCouncil);
// with none of them the operation 0x41 {0x41, type, town} (`op`) and then Authorised with [+0x2C8] & 0xE0. Whether
// it queues.
bool arsenal_form(const SaveGame& save, uint32_t town, uint32_t type, ArsenalMessage& message, OperationBytes& op);
// «Dismiss unit» (0x5B0BF3): 0x40 {0x40, type, town}, the message Authorised with [+0x2C8] & 0xE0, else None.
OperationBytes arsenal_dismiss(const SaveGame& save, uint32_t town, uint32_t type, ArsenalMessage& message);

// The town hall (.let 0x03): its side menu *0x6E5500 (0x5A8AF0) opens with its 8 buttons the pages of the window
// *0x6E558C (0x5E0BA0; the update 0x5E0850 and the drawing 0x5E0960 by the page [+0x18E4]; the window's town
// [+0x18E8]). The page 0, the notices (0x5E7730, 0x5E1010): the world's orders (0x6DE530, 12 bytes each; the event
// 0x22 makes them) one at a time with two arrows, and an entry 7, the prince's decree, while the town's siege object
// has wares the world lacks.
struct TownOrder {
	uint8_t town = 0;       // [+0x0]: the town that orders
	uint8_t ware = 0;       // [+0x1]
	bool bonus = false;     // [+0x2]: the price paid when all is delivered (a «bonus»), else at each delivery
	uint8_t count = 0;      // [+0x3]: barrels or loads
	uint32_t deadline = 0;  // [+0x4]: a tick
	uint32_t price = 0;     // [+0x8]: of one
};
// The world's order k (0..6) when its town is in the world.
std::optional<TownOrder> town_order(const SaveGame& save, uint32_t k);
// The prince's decree (with the town's siege object's [+0x1010] not 0xFF): the four wares the world lacks most (0xFF
// none past the first) and the ware of a supply contract the prince offers (0x635660: of those three months in the
// list, by (the town + [0x6DE4A1]) % their count; 0xFF none), or 0xFF when the player has promised deliveries there
// already (0x636590: his node in [+0x101C]).
struct PrinceDecree {
	std::array<uint8_t, 4> wares{};
	uint8_t contract = 0xFF;
};
std::optional<PrinceDecree> prince_decree(const SaveGame& save, uint32_t town);
// «Accept order» (0x5E7730) on the order k: the player's standing (s16 [+0x22]) below 20, refused (false); else the
// operation 0x3D {0x3D, town, ware, the client's first name, family name (the order's town's [+0x6E8 + c] and
// [+0x6EC + c], c = (the window's town + ware + count) & 3), the deadline | 0xFF, u16 count, u16 count, u16 price,
// the player, the bonus}.
bool accept_order(const SaveGame& save, uint32_t town, uint32_t k, OperationBytes& op);

}  // namespace p3::data
