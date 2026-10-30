#pragma once

#include <cstdint>

#include "p3data/prices.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// Comercio entre un barco y el mercado de una ciudad: reproducción de las
// operaciones 0x02 (comprar, 0x537450) y 0x01 (vender, 0x537340) de
// Patrician3.exe. Las cantidades van en unidades internas (barril = 200,
// carga = 2000) y los índices de barco y ciudad son los de la partida.
// De momento no se apuntan las estadísticas de comercio del comerciante
// (0x4F8840 y 0x4F8690), que no cambian ni precios ni existencias.

// Redondea la carga de cada mercancía a unidades enteras y recalcula la bodega
// ocupada y el peso del equipo (0x5182B0). Devuelve el espacio libre en
// unidades internas; puede ser negativo.
int32_t recalculate_cargo(SavedShip& ship);

struct TradeResult {
	int32_t amount = 0;  // Lo comprado o vendido.
	int32_t price = 0;  // Lo pagado o cobrado.
};

// Compra para el barco `amount` de una mercancía al mercado de la ciudad
// `town`, en la que tiene que estar atracado. El juego la limita al almacén de
// la ciudad, a la bodega libre y al dinero del dueño del barco.
TradeResult buy_to_ship(SaveGame& save, int ship, int town, int ware, int32_t amount);

// Vende a la ciudad `town` hasta `amount` de la carga del barco.
TradeResult sell_from_ship(SaveGame& save, int ship, int town, int ware, int32_t amount,
	TradeDifficulty difficulty = TradeDifficulty::Normal);

// Lo que harían buy_to_ship y sell_from_ship, sin cambiar nada.
TradeResult quote_buy(const SaveGame& save, int ship, int town, int ware, int32_t amount);
TradeResult quote_sell(const SaveGame& save, int ship, int town, int ware, int32_t amount,
	TradeDifficulty difficulty = TradeDifficulty::Normal);

}  // namespace p3::data
