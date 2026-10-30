// Comprueba el comercio entre barco y ciudad (src/p3data/trade.cpp) con una
// partida mínima hecha a mano. La comparación exacta con el juego se hace con
// tools/oracle/save_oracle.py sobre las partidas reales (ver docs/oracle.md);
// aquí se vigilan los límites y el redondeo que reproduce el código.

#include <cstdio>
#include <cstdlib>

#include "p3data/trade.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
	if (!ok) {
		std::printf("falla: %s\n", what);
		++failures;
	}
}

constexpr int kBeer = 3;  // Por barriles (200 unidades).
constexpr int kGrain = 0;  // Por cargas (2000 unidades).

SaveGame make_save() {
	SaveGame save;
	SavedTown town;
	town.flags = 0x100;  // Con mercado.
	town.wares[kBeer] = 40 * 200;
	town.wares[kGrain] = 30 * 2000;
	town.price_thresholds[kBeer] = {8000, 24000, 25000, 32000};
	town.price_thresholds[kGrain] = {20000, 70000, 110000, 130000};
	save.towns.push_back(town);
	save.merchants.resize(1);
	save.merchants[0].money = 30000;
	save.player = 0;
	SavedShip ship;
	ship.status = 0;
	ship.owner = 0;
	ship.type = uint8_t(ShipType::Snaikka);
	ship.capacity = 30000;  // 15 cargas.
	ship.sailors = 10;
	ship.artillery.fill(0xFF);
	ship.town = 0;
	ship.name = "Prueba";
	save.ships.push_back(ship);
	return save;
}

void test_buy_and_sell() {
	SaveGame save = make_save();
	const int32_t money = save.merchants[0].money;
	const int32_t expected = buy_price(kBeer, save.towns[0].market(kBeer), 5 * 200);
	const TradeResult quote = quote_buy(save, 0, 0, kBeer, 5 * 200);
	const TradeResult bought = buy_to_ship(save, 0, 0, kBeer, 5 * 200);
	expect(quote.amount == bought.amount && quote.price == bought.price, "el presupuesto coincide con la compra");
	expect(bought.amount == 1000 && bought.price == expected, "compra de 5 barriles al precio del mercado");
	expect(save.merchants[0].money == money - expected, "se cobra la compra");
	expect(save.towns[0].wares[kBeer] == 35 * 200, "baja el almacén de la ciudad");
	expect(save.ships[0].cargo[kBeer] == 1000 && save.ships[0].used == 1000, "sube la carga del barco");
	expect(save.ships[0].average_price[kBeer] == float(double(expected) / 1000.0), "precio medio por unidad");

	const int32_t sell_expected = sell_price(kBeer, save.towns[0].market(kBeer), 400);
	const TradeResult sold = sell_from_ship(save, 0, 0, kBeer, 400);
	expect(sold.amount == 400 && sold.price == sell_expected, "venta de 2 barriles");
	expect(save.ships[0].cargo[kBeer] == 600 && save.towns[0].wares[kBeer] == 35 * 200 + 400, "la venta mueve la carga");
	const TradeResult all = sell_from_ship(save, 0, 0, kBeer, 1000 * 1000);
	expect(all.amount == 600 && save.ships[0].cargo[kBeer] == 0, "no se vende más de lo que se lleva");
}

void test_limits() {
	// La bodega: 15 cargas de grano caben, la 16.ª no (la ciudad tiene 30).
	SaveGame save = make_save();
	save.merchants[0].money = 10 * 1000 * 1000;
	const TradeResult full = buy_to_ship(save, 0, 0, kGrain, 20 * 2000);
	expect(full.amount == 15 * 2000, "la compra se limita a la bodega libre");
	expect(buy_to_ship(save, 0, 0, kGrain, 2000).amount == 0, "con la bodega llena no se compra");

	// El almacén de la ciudad.
	save = make_save();
	save.merchants[0].money = 10 * 1000 * 1000;
	save.towns[0].wares[kBeer] = 3 * 200 + 50;
	expect(buy_to_ship(save, 0, 0, kBeer, 10 * 200).amount == 3 * 200, "la compra se limita al almacén");

	// El dinero: lo más que se puede pagar, en barriles enteros.
	save = make_save();
	save.merchants[0].money = 100;
	const TradeResult partial = buy_to_ship(save, 0, 0, kBeer, 40 * 200);
	const MarketState before = make_save().towns[0].market(kBeer);
	expect(partial.amount > 0 && partial.amount % 200 == 0, "con poco dinero se compran barriles enteros");
	expect(partial.price <= 100 && buy_price(kBeer, before, partial.amount + 200) >= 100,
		"se compra todo lo que alcanza el dinero");

	// El barco tiene que estar en esa ciudad.
	save = make_save();
	save.ships[0].town = 0xFF;
	expect(buy_to_ship(save, 0, 0, kBeer, 200).amount == 0, "un barco en el mar no compra");
}

void test_recalculate() {
	SavedShip ship = make_save().ships[0];
	ship.cargo[kBeer] = 450;  // Se redondea a 2 barriles.
	ship.cargo[kGrain] = 2500;  // Y a 1 carga.
	ship.sailors = 14;  // 4 por encima del mínimo de una Snaikka.
	ship.artillery[0] = 2;  // Una pieza de 2000.
	const int32_t free = recalculate_cargo(ship);
	expect(ship.cargo[kBeer] == 400 && ship.cargo[kGrain] == 2000, "la carga se redondea a unidades enteras");
	expect(ship.equipment == 4 * 400 + 2000, "peso de marineros de más y artillería");
	expect(free == 30000 - 3600 - 2400 && ship.used == 2400, "espacio libre");
}

}  // namespace

int main() {
	test_buy_and_sell();
	test_limits();
	test_recalculate();
	if (failures) {
		std::printf("%d comprobaciones fallidas\n", failures);
		return EXIT_FAILURE;
	}
	std::printf("comercio: todo correcto\n");
	return EXIT_SUCCESS;
}
