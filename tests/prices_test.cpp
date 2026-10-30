// Comprueba los precios contra el propio juego: cada caso esperado se obtuvo
// ejecutando get_buy_price / get_sell_price de Patrician3.exe en un emulador de
// CPU (con la FPU a 53 bits, como la deja el runtime de MSVC). Los casos
// "P3Modding" son los de sus scripts buying-price.py y selling-price.py.

#include <cstdio>
#include <cstdlib>

#include "p3data/prices.hpp"

using namespace p3::data;

namespace {

int failures = 0;

struct BuyCase {
	int ware;
	int32_t stock;
	int32_t amount;
	PriceThresholds thresholds;
	bool open;
	bool taxed;
	int32_t expected;
};

struct SellCase {
	int ware;
	int32_t stock;
	int32_t amount;
	PriceThresholds thresholds;
	int difficulty;
	bool open;
	int32_t expected;
};

// clang-format off
const BuyCase kBuys[] = {
	{16, 2000, 2000, {20000, 60000, 70000, 80000}, true, false, 3410},  // P3Modding: hierro, 1 carga con poco almacén
	{16, 4000, 2000, {20000, 60000, 70000, 80000}, true, false, 3190},  // P3Modding
	{16, 18000, 2000, {20000, 60000, 70000, 80000}, true, false, 1649},  // P3Modding: 1649,99999 se trunca
	{16, 23000, 2000, {20000, 60000, 70000, 80000}, true, false, 1298},  // P3Modding: cruza t0
	{16, 23000, 10000, {20000, 60000, 70000, 80000}, true, false, 7922},  // P3Modding: 5 cargas
	{16, 55000, 2000, {20000, 60000, 70000, 80000}, true, false, 946},  // P3Modding: tramo 1
	{16, 65000, 2000, {20000, 60000, 70000, 80000}, true, false, 809},  // P3Modding: tramo 2
	{16, 75000, 2000, {20000, 60000, 70000, 80000}, true, false, 633},  // P3Modding: tramo 3
	{16, 110000, 100000, {20000, 60000, 70000, 80000}, true, false, 46310},  // P3Modding: 50 cargas, todos los tramos
	{16, 200000, 2000, {20000, 60000, 70000, 80000}, true, false, 528},  // por encima de t3
	{3, 17179, 200, {8204, 24612, 25612, 32816}, true, false, 42},  // cerveza de Lübeck, 1 barril
	{12, 13419, 200, {6844, 18532, 49732, 56576}, true, false, 312},  // herramientas de Lübeck, 1 barril
	{0, 79208, 2000, {13874, 69370, 113550, 127424}, true, false, 105},  // grano de Lübeck, 1 carga
	{0, 79208, 2000, {13874, 69370, 113550, 127424}, true, true, 121},  // grano con tasa (+15 %)
	{3, 1000, 1500, {8204, 24612, 25612, 32816}, true, false, 1017},  // más de lo que hay: el resto al factor 4
	{3, 199, 100, {8204, 24612, 25612, 32816}, true, false, 0},  // menos de un barril: no se vende
	{0, 1999, 1000, {13874, 69370, 113550, 127424}, true, false, 0},  // menos de una carga: no se vende
	{3, 17179, 200, {8204, 24612, 25612, 32816}, false, false, 0},  // sin mercado
};

const SellCase kSells[] = {
	{16, 0, 2000, {20000, 60000, 70000, 80000}, 0, true, 1900},  // P3Modding: almacén vacío, fácil
	{16, 0, 2000, {20000, 60000, 70000, 80000}, 1, true, 1733},  // P3Modding: almacén vacío, normal
	{16, 0, 2000, {20000, 60000, 70000, 80000}, 2, true, 1566},  // P3Modding: almacén vacío, difícil
	{16, 0, 100000, {20000, 60000, 70000, 80000}, 0, true, 47740},  // P3Modding: 50 cargas, fácil
	{16, 0, 100000, {20000, 60000, 70000, 80000}, 1, true, 46860},  // P3Modding: 50 cargas, normal
	{16, 0, 100000, {20000, 60000, 70000, 80000}, 2, true, 45980},  // P3Modding: 50 cargas, difícil
	{16, 17000, 2000, {20000, 60000, 70000, 80000}, 1, true, 1284},  // P3Modding: cerca de t0
	{16, 19000, 2000, {20000, 60000, 70000, 80000}, 1, true, 1236},  // P3Modding: cruza t0
	{16, 21000, 2000, {20000, 60000, 70000, 80000}, 1, true, 1214},  // P3Modding: tramo 1
	{16, 55000, 100000, {20000, 60000, 70000, 80000}, 2, true, 25135},  // P3Modding: 50 cargas desde el tramo 1
	{16, 57000, 2000, {20000, 60000, 70000, 80000}, 1, true, 897},  // P3Modding: tramo 1 alto
	{16, 77000, 2000, {20000, 60000, 70000, 80000}, 1, true, 475},  // P3Modding: tramo 3
	{16, 85000, 2000, {20000, 60000, 70000, 80000}, 1, true, 440},  // P3Modding: por encima de t3
	{3, 17179, 200, {8204, 24612, 25612, 32816}, 1, true, 41},  // cerveza de Lübeck, 1 barril
	{3, 17179, 200, {8204, 24612, 25612, 32816}, 1, false, 0},  // sin mercado
};
// clang-format on

}  // namespace

int main() {
	for (const BuyCase& c : kBuys) {
		MarketState market;
		market.stock = c.stock;
		market.thresholds = c.thresholds;
		market.open = c.open;
		market.taxed = c.taxed;
		const int32_t price = buy_price(c.ware, market, c.amount);
		if (price != c.expected) {
			std::fprintf(stderr, "FALLO: compra de %d (mercancía %d, almacén %d): %d en vez de %d\n", c.amount, c.ware,
				c.stock, price, c.expected);
			failures++;
		}
	}
	for (const SellCase& c : kSells) {
		MarketState market;
		market.stock = c.stock;
		market.thresholds = c.thresholds;
		market.open = c.open;
		const int32_t price = sell_price(c.ware, market, c.amount, TradeDifficulty(c.difficulty));
		if (price != c.expected) {
			std::fprintf(stderr, "FALLO: venta de %d (mercancía %d, almacén %d): %d en vez de %d\n", c.amount, c.ware,
				c.stock, price, c.expected);
			failures++;
		}
	}
	if (failures == 0) {
		std::printf("prices_test: todo correcto\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
