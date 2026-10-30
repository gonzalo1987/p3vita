#pragma once

#include <array>
#include <cstdint>

namespace p3::data {

// Precios del mercado de una ciudad: reproducción exacta de get_buy_price
// (0x52E430) y get_sell_price (0x52E1D0) de Patrician3.exe, incluidas sus
// operaciones en float de 32 bits y el truncado final. Cantidades en unidades
// internas (barril = 200, carga = 2000); precios en monedas.

// Umbrales t0 < t1 < t2 < t3 de una mercancía: las existencias a partir de las
// cuales el precio baja de tramo. Las partidas los guardan ya calculados.
using PriceThresholds = std::array<uint32_t, 4>;

// Dificultad de comercio de la partida: cambia el precio de venta con poco
// almacén.
enum class TradeDifficulty { Low = 0, Normal = 1, High = 2 };

// Lo que las funciones del juego leen de la ciudad para una mercancía.
struct MarketState {
	int32_t stock = 0;
	PriceThresholds thresholds{};
	bool open = true;  // La ciudad tiene mercado (bandera 0x100 de la ciudad).
	bool taxed = false;  // Es la mercancía con recargo de la ciudad (+15 % al comprar).
};

// Lo que cuesta comprar `amount` a la ciudad. 0 si no tiene mercado o no le
// queda ni una unidad (un barril o una carga).
int32_t buy_price(int ware, const MarketState& market, int32_t amount);

// Lo que paga la ciudad por `amount`. 0 si no tiene mercado.
int32_t sell_price(int ware, const MarketState& market, int32_t amount,
	TradeDifficulty difficulty = TradeDifficulty::Normal);

// Precio base por unidad interna (tabla 0x673A18 del ejecutable).
float ware_base_price(int ware);

}  // namespace p3::data
