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
