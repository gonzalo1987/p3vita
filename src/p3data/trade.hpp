#pragma once

#include <cstdint>

#include "p3data/prices.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// Trade between a ship and a town's market: operations 0x02 (buy, 0x537450)
// and 0x01 (sell, 0x537340) of Patrician3.exe, rewritten literally (by
// offset) with the merchant's trade statistics (0x4F8840 and 0x4F8690).
// Amounts in internal units (barrel = 200, load = 2000); ship and town
// indices as in the save. The viewer queues the operations
// (player_actions.hpp) and run_operation runs them, as the game's market
// window and executor do.

// Rounds the cargo of each ware down to whole units and computes the hold
// used and the equipment's weight again (0x5182B0). Returns the free room in
// internal units; it may be negative.
int32_t recalculate_cargo(SavedShip& ship);

// 0x5180C0 (on the ship): `amount` of a ware into its hold at `price` a unit
// (the average price weighed as the game does). Nothing for an amount ≤ 0.
void fn_5180c0(SaveGame& save, int ship, uint32_t ware, int32_t amount, float price);
// 0x4FF6F0 (on the office): `amount` of a ware into the office at `price` a
// unit. Nothing for an amount ≤ 0.
void fn_4ff6f0(OfficeMemory& o, uint32_t ware, int32_t amount, float price);
// 0x5007A0 (on the office): `amount` of a ware into the office for `price` in all (its average price again, unless
// the new stock isn't positive).
void fn_5007a0(OfficeMemory& o, uint32_t ware, int32_t amount, int32_t price);

struct TradeResult {
	int32_t amount = 0;  // What was bought or sold.
	int32_t price = 0;  // What was paid or got.
};

// The handlers, on the operation's 0x14 bytes ({code, s32 amount, u16 ware,
// u16 ship, u16 (the player), u16 town}). Like the game, they write the
// amount they clamp back into the operation's [+4]. They return what changed
// (nothing: {}).
TradeResult fn_537450(SaveGame& save, uint8_t* operation);  // 0x02, buy for the ship.
TradeResult fn_537340(SaveGame& save, uint8_t* operation);  // 0x01, sell from the ship.
// The player's office: 0x08 wares between a ship and the office in its town
// ({0x08, s32 amount, u16 ship, u16 ware, u16 (the player), u8 direction: 1
// from the office, 0 into it}); 0x58 the office sells to the town and 0x59 it
// buys ({code, s32 amount, u16 ware, u16 merchant, u16 (the player), u16
// town}; they write the amount back into [+4]).
void fn_538100(SaveGame& save, const uint8_t* operation);  // 0x08
// 0x09: artillery (or the hand weapons) between a ship and the office in its
// town ({0x09, type, town with the bit 31 «from the ship», ship, count}).
void fn_538210(SaveGame& save, const uint8_t* operation);
TradeResult fn_53d2b0(SaveGame& save, uint8_t* operation);  // 0x58, sell from the office.
TradeResult fn_53d3c0(SaveGame& save, uint8_t* operation);  // 0x59, buy for the office.
// A convoy's trade: 0x19 sells (`sell`) and 0x1A buys with the merchant's
// ships of a convoy in the town ({code, s32 amount, u16 ware, u16 convoy, u16
// merchant, u16 town}); 0x1B moves wares between them and his office there
// ({0x1B, s32 amount, u16 convoy, u16 ware, u16 merchant, u8 direction: 1
// from the office, 0 into it}).
TradeResult fn_539300(SaveGame& save, const uint8_t* operation, bool sell);
// 0x4D (0x53B5E0): the tavern's trader's wares bought into a ship (and its convoy) or the office ({0x4D, -, s32
// price, u16 amount in barrels or loads, u16 ship (past the ships: the office), u8 seller, u8 buyer, u8 ware, u8
// town}).
void fn_53b5e0(SaveGame& save, const uint8_t* operation);
// The weaponsmith's window (.let 0x08, *0x6E55A8; its update 0x5ECFA0, smithy_window.hpp) and the tavern's weapons
// dealer: 0x50 (0x53BC90) hand weapons bought from the town into a ship (and its convoy) or the office, or sold
// ({0x50, u32 price of one, s32 count, u8 town, u8 merchant, u8 kind, u8 buy, u16 ship}); 0x56 (0x53CAF0) a piece of
// artillery between the office and the town ({0x56, merchant, kind, price, town | 0x100 sell}); 0x57 (0x536186,
// inline in the executor) the hand weapon the town's weaponsmith makes first ({0x57, town, ware}: the town's [+0x6D0]
// the ware if 0x14..0x17, else 0; the window offers it to the town's mayor).
void fn_53bc90(SaveGame& save, const uint8_t* operation);
void fn_53caf0(SaveGame& save, const uint8_t* operation);
void fn_536186(SaveGame& save, const uint8_t* operation);
void fn_5396b0(SaveGame& save, const uint8_t* operation);

// The same handlers on an operation made here (for the tests and the
// oracle's trade comparison, compare_save): buy for the ship `amount` of a
// ware from the market of `town`, where it has to be docked; the game clamps
// it to the town's store, the free hold and the owner's money.
TradeResult buy_to_ship(SaveGame& save, int ship, int town, int ware, int32_t amount);
// Sell to `town` up to `amount` of the ship's cargo, at the save's trade
// difficulty.
TradeResult sell_from_ship(SaveGame& save, int ship, int town, int ware, int32_t amount);

// What buy_to_ship and sell_from_ship would do now, without changing
// anything (what the viewer shows before trading).
TradeResult quote_buy(const SaveGame& save, int ship, int town, int ware, int32_t amount);
TradeResult quote_sell(const SaveGame& save, int ship, int town, int ware, int32_t amount,
	TradeDifficulty difficulty = TradeDifficulty::Normal);

}  // namespace p3::data
