// The side panel's ship menu (player_actions.hpp): the firepower's cannonballs (0x4901C0) and the prices it
// shows of the cargo (0x5187F0), as read in the game's code (re/emu/sesion14/ship_menu_notes.md). Without
// the game's data: a guard against changing them by accident.

#include <cstdio>

#include "p3data/player_actions.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
	if (!ok) {
		std::printf("fails: %s\n", what);
		++failures;
	}
}

bool balls(int32_t firepower, int big, int small, bool half) {
	const FirepowerBalls b = firepower_balls(firepower);
	return b.big == big && b.small == small && b.half == half;
}

}  // namespace

int main() {
	expect(balls(0, 0, 0, false), "no firepower, no balls");
	expect(balls(5, 0, 0, false), "under 9: the game's sqrt of a negative draws none");
	expect(balls(9, 0, 1, false), "9: f = 1, n = 2, one small ball");
	expect(balls(11, 0, 2, true), "11: n = 5, two small balls and a half");
	expect(balls(13, 0, 3, false), "13: n = 6, three small balls");
	expect(balls(30, 1, 1, false), "30: n = 12, a big ball and a small one");
	expect(balls(100, 2, 2, false), "100: n = 24, two big balls and two small ones");
	expect(balls(1000, 5, 0, false), "1000: at most five big balls");

	SaveGame save;
	save.ships.resize(1);
	SavedShip& ship = save.ships[0];
	ship.average_price(3) = 0.5f;  // Beer, in barrels: 0.5 · 200 + 0.5 = 100.5.
	ship.average_price(0) = 0.058f;  // Grain, in loads: 0.058 · 2000 + 0.5.
	ship.average_price(20) = 12.34f;  // Swords, in pieces: 12.34 · 10 + 0.5.
	const std::array<int32_t, 24> prices = ship_cargo_prices(save, 0);
	expect(prices[3] == 100, "a barrel's price, truncated");
	expect(prices[0] == 116, "a load's price");
	expect(prices[20] == 123, "a weapon's price, by ten");
	expect(prices[5] == 0, "nothing paid, 0");

	if (failures == 0) {
		std::printf("ship menu: all right\n");
	}
	return failures == 0 ? 0 : 1;
}
