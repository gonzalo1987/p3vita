#include "p3data/prices.hpp"

#include <cstring>

#include "p3data/wares.hpp"

namespace p3::data {

// El juego hace estas cuentas con la FPU x87 a 53 bits de precisión y guarda
// muchos resultados intermedios en floats de 32 bits. Aquí cada operación en
// registro es una operación en double y cada guardado, una conversión a float,
// en el mismo orden; así los enteros finales coinciden también en las fronteras.

namespace {

// Floats de la tabla 0x673A18 del ejecutable, tal cual (patrón de bits).
constexpr uint32_t kBasePriceBits[kWareCount] = {
	0x3d6147af, 0x3ef50482, 0x3e6154ca, 0x3e322d0e, 0x3e11eb85, 0x3f0ccccd, 0x3fb33333, 0x3f8ccccd,
	0x3f845a1d, 0x40587ae1, 0x3ed33333, 0x3ce147af, 0x3fa39581, 0x3f8f5c29, 0x3ee147af, 0x3e8e5604,
	0x3ee147af, 0x3e6147af, 0x3f5ae147, 0x3d236e30, 0x41200000, 0x41000000, 0x41400000, 0x41600000};

// Factor de precio en cada umbral (constantes 0x6739E8.. y 0x673A08.. del ejecutable):
// baja en línea recta de un umbral al siguiente y es constante por encima de t3.
constexpr float kBuyFactor[5] = {4.0f, 1.5f, 1.0f, 0.8f, 0.6f};
constexpr float kSellFactor[5] = {0.0f, 1.4f, 1.0f, 0.7f, 0.5f};  // El primero es la dificultad.
constexpr float kSellDifficulty[3] = {2.2f, 2.0f, 1.8f};
constexpr float kTaxFactor = 1.15f;  // 0x673A7C

// Recta del factor dentro del tramo `interval`: factor = ordenada + pendiente * x.
struct Line {
	float slope;
	float intercept;
};

Line interval_line(const float factor[5], int interval, const PriceThresholds& t) {
	if (interval >= 4) {
		return {0.0f, factor[4]};
	}
	if (interval == 0) {
		const float width = float(int32_t(t[0]));
		return {float((double(factor[1]) - double(factor[0])) / double(width)), factor[0]};
	}
	const int32_t start = int32_t(t[size_t(interval - 1)]);
	const float width = float(int32_t(t[size_t(interval)]) - start);
	const float slope = float((double(factor[interval + 1]) - double(factor[interval])) / double(width));
	return {slope, float(double(factor[interval]) - double(float(start)) * double(slope))};
}

// Suma, tramo a tramo, cantidad × factor medio entre `from` y `to` (from < to),
// con el acumulador en float como el juego.
float weighted_amount(const float factor[5], int32_t from, int32_t to, const PriceThresholds& t) {
	int interval = 0;
	while (interval < 4 && from >= int32_t(t[size_t(interval)])) {
		interval++;
	}
	float total = 0.0f;
	for (int32_t lower = from;; ++interval) {
		const bool last = interval >= 4 || to <= int32_t(t[size_t(interval)]);
		const int32_t upper = last ? to : int32_t(t[size_t(interval)]);
		const Line line = interval_line(factor, interval, t);
		const float position = float(upper + lower);
		const float f = float(double(position) * double(line.slope) * 0.5 + double(line.intercept));
		total = float(double(float(upper - lower)) * double(f) + double(total));
		lower = upper;
		if (last) {
			return total;
		}
	}
}

// Conversión a entero del runtime de MSVC (__ftol): trunca hacia cero.
int32_t ftol(double value) {
	return int32_t(int64_t(value));
}

}  // namespace

float ware_base_price(int ware) {
	if (ware < 0 || ware >= kWareCount) {
		return 0;
	}
	float value = 0;
	std::memcpy(&value, &kBasePriceBits[ware], sizeof(value));
	return value;
}

int32_t buy_price(int ware, const MarketState& market, int32_t amount) {
	if (!market.open || ware < 0 || ware >= kWareCount) {
		return 0;
	}
	// Hace falta al menos una unidad: un barril (o una pieza de armamento) o una carga.
	const int32_t minimum = kWares[size_t(ware)].unit == WareUnit::Load ? 2000 : 200;
	if (market.stock < minimum) {
		return 0;
	}
	const double base = ware_base_price(ware);
	const PriceThresholds& t = market.thresholds;
	int32_t remaining = market.stock - amount;
	// Lo que se compra por encima de las existencias se cobra al factor máximo.
	float shortfall = 0.0f;
	if (remaining < 0) {
		shortfall = float(double(float(-remaining)) * double(kBuyFactor[0]));
		remaining = 0;
	}
	if (remaining >= int32_t(t[3])) {
		return ftol(double(float(market.stock - remaining)) * base * double(kBuyFactor[4]));
	}
	double price = (double(shortfall) + double(weighted_amount(kBuyFactor, remaining, market.stock, t))) * base;
	if (market.taxed) {
		price *= double(kTaxFactor);
	}
	return ftol(price);
}

int32_t sell_price(int ware, const MarketState& market, int32_t amount, TradeDifficulty difficulty) {
	if (!market.open || amount < 1 || ware < 0 || ware >= kWareCount) {
		return 0;
	}
	const double base = ware_base_price(ware);
	const PriceThresholds& t = market.thresholds;
	const int32_t stock = market.stock < 0 ? 0 : market.stock;
	const int32_t new_stock = market.stock + amount;
	if (stock >= int32_t(t[3])) {
		return ftol(double(float(new_stock - stock)) * base * double(kSellFactor[4]));
	}
	float factor[5] = {kSellDifficulty[int(difficulty)], kSellFactor[1], kSellFactor[2], kSellFactor[3],
		kSellFactor[4]};
	return ftol(double(weighted_amount(factor, stock, new_stock, t)) * base);
}

}  // namespace p3::data
