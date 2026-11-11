#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "p3data/savegame.hpp"

namespace p3::data {

class Sailing;

// The automatic trade routes of Patrician3.exe (phase 6), rewritten literally
// by offset.
//
// The stops live in a pool (*0x6DD728): SaveGame::trade_routes, records of
// 0xDC bytes, as many as the u16 [0x6DD72A] of the globals; the u16
// [0x6DD728] is the head of the free list (ended by the count). A stop:
//   u16 [+0x0]        the next stop of its route (a route is a circular
//                     list) or, when free, the next free record;
//   u8  [+0x2]        the town (0xFF in a record never used);
//   u8  [+0x3]        the action's bits (0x4: the route's first stop);
//   u8  [+0x4 + k]    the order of the 24 wares (the k-th ware handled);
//   s32 [+0x1C + 4w]  ware w's price: > 0 sell up to it, < 0 buy up to −price,
//                     0 a transfer with the office;
//   s32 [+0x7C + 4w]  ware w's amount: with price 0, > 0 office → ship and
//                     < 0 ship → office (1 000 000 000 is «max»).
// A ship's u16 [+0x132] is its current stop (a stop index below the count, or
// 0xFFFF), and the bit 0 of its u8 [+0x136] says the route is on.

using TradeStop = GameMemory<0xDC>;

// 0x4D4C90: a new stop, alone in its route (its next is itself), at town 0,
// with the default prices, no amounts and the default order. When no record
// is free the pool grows by 150.
uint16_t fn_4d4c90(SaveGame& save);
// 0x4D4DD0 (stop, ships): releases the route of `stop`; with `ships`, every
// ship on one of its stops loses it first.
void fn_4d4dd0(SaveGame& save, uint32_t stop, bool ships);
// 0x4D4E80: takes one stop out of its route and frees it.
void fn_4d4e80(SaveGame& save, uint32_t stop);
// 0x4D4F60 (&head, a, b, first): moves stop `a` of the route of `head` (a
// ship's [+0x132]) after stop `b`; if `b` isn't in that route, `a` is removed.
// With `first`, `a` becomes the route's first stop (also when a == b).
void fn_4d4f60(SaveGame& save, uint16_t& head, uint32_t a, uint32_t b, bool first);
// 0x4D5AB0: whether the ship's current stop has something to do (it rounds
// the ship's cargo, 0x5182B0, on the way).
bool fn_4d5ab0(SaveGame& save, int ship);
// 0x509030 (on the ship object): every ship on stop `from` goes to `to`.
void fn_509030(SaveGame& save, uint16_t from, uint16_t to);
// 0x51A3C0 / 0x51A450 (on a ship): a convoy guide's cargo gets 1 of each ware
// another member carries and he hasn't / those 1s back to 0.
void fn_51a3c0(SaveGame& save, int ship);
void fn_51a450(SaveGame& save, int ship);
// 0x5171E0 (on a ship, its index): it stops where it is (its sea route
// dropped).
void fn_5171e0(SavedShip& ship);

// 0x500EC0 (on an office): what it can give of a ware: its stock, less what
// its administrator keeps of the wares he looks after ([+0x3B4], [+0x354]).
int32_t fn_500ec0(const SaveGame& save, const OfficeMemory& o, uint32_t ware);
// 0x4D5200 (on the pool: the ship, its owner's office in the town or −1): the
// ship trades at its stop: sells in the stop's order (each ware up to its
// price, and a unit more while it pays), puts into the office, sends the
// report at the route's first stop (the letter 0x07), then buys (with the
// captain's discount, cut to the money) and takes from the office.
void fn_4d5200(SaveGame& save, int ship, int office);
// 0x4D5900 (on the pool: the ship, its index): a ship on its route before a
// closed port sails to the next stop with something to do, else home, else
// the nearest town with a shipyard.
void fn_4d5900(SaveGame& save, Sailing* sailing, int ship);
// What 0x4D5900 chooses (its ship's stop moved on the way): the town, or one
// out of the world for none (then it stays where it is).
uint32_t next_route_town(SaveGame& save, int ship);
// 0x518860 (on a ship, its index): a docked ship of a human on its route.
void fn_518860(SaveGame& save, Sailing* sailing, int ship);
// 0x503230 (on a convoy): a docked convoy of a human on its guide's route.
void fn_503230(SaveGame& save, Sailing* sailing, int convoy);

// The player's operations on the routes (the executor 0x535760); `op` is the
// operation's 0x14 bytes. 0x68 and 0x6C write into theirs, as the game.
void fn_53df00(SaveGame& save, Sailing* sailing, uint8_t* op);  // 0x68: start or stop a ship's route
void fn_53e480(SaveGame& save, const uint8_t* op);  // 0x69: a ware of a stop
void fn_53e610(SaveGame& save, const uint8_t* op);  // 0x6A: insert, remove or change a stop
void fn_53e7c0(SaveGame& save, const uint8_t* op);  // 0x6B: move a stop
void fn_53e890(SaveGame& save, uint8_t* op);  // 0x6C: a new route for a ship
void fn_53e9c0(SaveGame& save, const uint8_t* op);  // 0x6D: a stop's action

// --- The route files (the route page's «File»: save\AutoRoute\NAME.rou) ---

// The queue object 0x6DF2F0's fields for them: its game mode ([+0x0]: & 0xC a
// network game), its queue's count ([+0x56], 0x34 at most), the stops loaded
// ([+0x930]: 0x4D5EE0's buffer, non-zero while they wait; p3vita keeps them in
// SaveGame::loaded_route) and the ship they're for ([+0x934]).
constexpr uint32_t kQueueObject = 0x6DF2F0;
constexpr uint32_t kQueueCount = 0x6DF346;
constexpr uint32_t kLoadedRoute = 0x6DFC20;
constexpr uint32_t kLoadedRouteShip = 0x6DFC24;
// 0x4D5C50 (on the pool: a stop, the name): the file of the route through
// `stop`: its stops from the first (the bit 0x4) along +0x0, each with +0x0 at
// 0, compressed (0x54C410: lz_compress). Nothing when `stop` is out of the pool
// or the route takes as many stops as the pool has (the game gives up).
std::optional<std::vector<uint8_t>> fn_4d5c50(const SaveGame& save, uint16_t stop);
// 0x4D5EE0 (the name): a file's stops: the header's size (& 0x3FFFFFFF) in
// whole stops of 0xDC bytes, decompressed (0x54C480) and linked 0 → 1 → … →
// the last → 0. Nothing on any failure (and for a size of 0, where the game
// would write before its buffer).
std::optional<std::vector<TradeStop>> fn_4d5ee0(const std::vector<uint8_t>& file);
// The route page's «Load» (0x48C92E): the queue object gets the menu's ship and
// the stops (none when the file failed); the next end of its queue's loop
// applies them (fn_5492d0).
void load_route_file(SaveGame& save, int ship, const std::optional<std::vector<TradeStop>>& stops);
// 0x5492D0 (on the queue object, at the end of its loop 0x546870 while it holds
// stops): the stops at towns out of the world, or without a deep port for a big
// ship (the ship's type, or a convoy's biggest, ≥ 2), get town 0xFF, the others
// lose their office transfers where the ship's owner has no office. Then, alone
// (not a network game): the ship's (or its guide's) route off, a new one of as
// many stops as are left (0x6C, run at once) and those stops copied into it. In
// a network game it queues instead: 0x68 off if the route is on, then 0x6C when
// the route's length differs (the stops kept for the next call), else 0x6A,
// 0x6D and 0x69 for what differs, stop by stop from the ship's own; with the
// queue full (its count 0x34) it stops there and goes on the next time.
void fn_5492d0(SaveGame& save, Sailing* sailing);

}  // namespace p3::data
