// Compara src/p3data/prices.cpp con los casos que genera price_oracle.py a
// partir del propio juego: deben coincidir todos al entero.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "p3data/prices.hpp"

using namespace p3::data;

int main(int argc, char** argv) {
	if (argc != 2) {
		std::fprintf(stderr, "uso: compare_prices <casos.csv>\n");
		return 2;
	}
	std::ifstream in(argv[1]);
	if (!in) {
		std::fprintf(stderr, "no se puede leer %s\n", argv[1]);
		return 1;
	}
	std::string line;
	std::getline(in, line);
	int total = 0;
	int mismatches = 0;
	while (std::getline(in, line)) {
		std::stringstream fields(line);
		std::string kind;
		std::getline(fields, kind, ',');
		long v[11];
		for (long& x : v) {
			std::string field;
			std::getline(fields, field, ',');
			x = std::stol(field);
		}
		const int ware = int(v[0]);
		MarketState market;
		market.open = v[2] != 0;
		market.taxed = v[3] != 0;
		market.stock = int32_t(v[4]);
		market.thresholds = {uint32_t(v[6]), uint32_t(v[7]), uint32_t(v[8]), uint32_t(v[9])};
		const int32_t amount = int32_t(v[5]);
		const int32_t ours = kind == "compra" ? buy_price(ware, market, amount)
											  : sell_price(ware, market, amount, TradeDifficulty(v[1]));
		total++;
		if (ours != v[10] && mismatches++ < 20) {
			std::printf("%s de %d (mercancía %d, almacén %ld): juego %ld, p3vita %d\n", kind.c_str(), amount, ware,
				v[4], v[10], ours);
		}
	}
	std::printf("casos: %d, distintos: %d\n", total, mismatches);
	return mismatches == 0 ? 0 : 1;
}
