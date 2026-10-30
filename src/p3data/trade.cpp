#include "p3data/trade.hpp"

#include <algorithm>
#include <array>

namespace p3::data {

namespace {

// Marineros que lleva cada tipo de barco sin ocupar bodega (tabla 0x673664).
constexpr std::array<int32_t, 4> kMinSailors = {10, 16, 30, 24};
constexpr int32_t kSailorWeight = 400;
// Peso de cada tipo de pieza de artillería (tabla 0x672CB4).
constexpr std::array<int32_t, 6> kArtilleryWeight = {1000, 1000, 2000, 2000, 2000, 1000};
// 0x672CC8: lo que suma cada tipo de pieza a [+0x120] del barco.
constexpr std::array<int32_t, 6> kArtilleryFirepower = {9, 10, 22, 24, 30, 18};
constexpr int32_t kHandWeaponWeight = 10;
constexpr int32_t kLoad = 2000;

// Unidad a la que se redondean las compras: carga o barril (tabla 0x672C14;
// las armas cuentan como barriles).
int32_t trade_unit(int ware) {
	return kWares[size_t(ware)].unit == WareUnit::Load ? 2000 : 200;
}

// Unidad a la que se redondea la carga del barco: aquí las armas van por piezas.
int32_t cargo_unit(int ware) {
	return ware_scale(kWares[size_t(ware)].unit);
}

// Añade la mercancía comprada a la bodega y actualiza el precio medio (0x517FE0).
void add_cargo(SavedShip& ship, int ware, int32_t amount, int32_t price) {
	if (amount <= 0) {
		return;
	}
	const size_t w = size_t(ware);
	const int32_t old = ship.cargo(w);
	const double total = double(ship.average_price(w)) * double(old) + double(price);
	int32_t cargo = old + amount;
	cargo -= cargo % cargo_unit(ware);
	ship.cargo(w) = cargo;
	ship.average_price(w) = float(total / double(cargo));
}

// Reduce la compra a lo que el comerciante puede pagar (0x52F3E0): primero
// escala la cantidad por dinero / precio y después la sube unidad a unidad
// mientras siga costando menos de lo que tiene.
void fit_to_money(int ware, const MarketState& market, int32_t money, int32_t& price, int32_t& amount) {
	if (money <= 0) {
		price = 0;
		amount = 0;
		return;
	}
	const int64_t first_price = price;
	const int32_t requested = amount;
	const int32_t unit = trade_unit(ware);
	int32_t candidate = amount;
	int32_t candidate_price = 0;
	do {
		candidate = int32_t(int64_t(candidate) * money / first_price);
		candidate = candidate / unit * unit;
		if (candidate == 0) {
			price = 0;
			amount = 0;
			return;
		}
		candidate_price = buy_price(ware, market, candidate);
	} while (candidate_price > money);

	int32_t best = candidate;
	price = candidate_price;
	for (;;) {
		candidate += unit;
		if (candidate > requested) {
			break;
		}
		candidate_price = buy_price(ware, market, candidate);
		if (candidate_price >= money) {
			break;
		}
		best = candidate;
		price = candidate_price;
	}
	amount = best;
}

bool valid(const SaveGame& save, int ship, int town, int ware) {
	return ship >= 0 && size_t(ship) < save.ships.size() && town >= 0 && size_t(town) < save.towns.size() &&
		ware >= 0 && ware < kWareCount && save.ships[size_t(ship)].owner() < save.merchants.size();
}

// El comerciante tiene prohibido comerciar en esa ciudad (campo 0x608).
bool banned(const SavedMerchant& merchant, int town) {
	return merchant.banned_towns() >> (town & 31) & 1;
}

}  // namespace

int32_t recalculate_cargo(SavedShip& ship) {
	int32_t used = 0;
	uint32_t value = 0;  // [+0x114] Lo que costó la carga: carga · precio medio, truncado.
	for (int w = kWareCount - 1; w >= 0; --w) {
		int32_t& cargo = ship.cargo(w);
		cargo -= cargo % cargo_unit(w);
		used += cargo;
		value += uint32_t(int64_t(double(float(cargo)) * double(ship.average_price(w))));
	}
	ship.memory.u32(0x114) = value;
	used += int32_t(ship.extra_loads()) * kLoad;

	const int32_t min_sailors = kMinSailors[ship.type() & 3];
	int32_t equipment = ship.sailors() > min_sailors ? (ship.sailors() - min_sailors) * kSailorWeight : 0;
	int32_t firepower = 0;  // [+0x120] Suma de la tabla 0x672CC8 por pieza de artillería.
	for (int slot = 0; slot < 24; ++slot) {
		const uint8_t piece = ship.artillery(slot);
		if (piece < kArtilleryWeight.size()) {
			equipment += kArtilleryWeight[piece];
			firepower += kArtilleryFirepower[piece];
		}
	}
	ship.memory.s32(0x120) = firepower;
	equipment += int32_t(ship.hand_weapons()) * kHandWeaponWeight;

	ship.equipment() = equipment;
	ship.used() = (used + 199) / 200 * 200;
	return ship.capacity() - equipment - used;
}

TradeResult quote_buy(const SaveGame& save, int ship_index, int town_index, int ware, int32_t amount) {
	if (!valid(save, ship_index, town_index, ware)) {
		return {};
	}
	SavedShip ship = save.ships[size_t(ship_index)];
	const SavedMerchant& merchant = save.merchants[ship.owner()];
	if (ship.town() != town_index || banned(merchant, town_index)) {
		return {};
	}
	const SavedTown& town = save.towns[size_t(town_index)];
	const int32_t stock = int32_t(town.stock(size_t(ware)));

	int32_t limit = recalculate_cargo(ship);
	if (stock < limit) {
		limit = stock;
	}
	if (limit < amount) {
		if (limit < 0) {
			return {};
		}
		amount = limit / trade_unit(ware) * trade_unit(ware);
	}
	const MarketState market = town.market(ware);
	int32_t price = buy_price(ware, market, amount);
	if (price > merchant.money()) {
		fit_to_money(ware, market, merchant.money(), price, amount);
	}
	return amount > 0 ? TradeResult{amount, price} : TradeResult{};
}

TradeResult buy_to_ship(SaveGame& save, int ship_index, int town_index, int ware, int32_t amount) {
	const TradeResult result = quote_buy(save, ship_index, town_index, ware, amount);
	if (result.amount <= 0) {
		return {};
	}
	SavedShip& ship = save.ships[size_t(ship_index)];
	save.merchants[ship.owner()].money() -= result.price;
	add_cargo(ship, ware, result.amount, result.price);
	recalculate_cargo(ship);
	save.towns[size_t(town_index)].stock(size_t(ware)) -= uint32_t(result.amount);
	return result;
}

TradeResult quote_sell(const SaveGame& save, int ship_index, int town_index, int ware, int32_t amount,
	TradeDifficulty difficulty) {
	if (!valid(save, ship_index, town_index, ware)) {
		return {};
	}
	const SavedShip& ship = save.ships[size_t(ship_index)];
	if (banned(save.merchants[ship.owner()], town_index)) {
		return {};
	}
	amount = std::min(amount, ship.cargo(size_t(ware)));
	const SavedTown& town = save.towns[size_t(town_index)];
	return {amount, sell_price(ware, town.market(ware), amount, difficulty)};
}

TradeResult sell_from_ship(SaveGame& save, int ship_index, int town_index, int ware, int32_t amount,
	TradeDifficulty difficulty) {
	// Se cobra con el almacén de antes de la venta.
	const TradeResult result = quote_sell(save, ship_index, town_index, ware, amount, difficulty);
	if (result.amount <= 0 && result.price == 0) {
		return {};
	}
	SavedShip& ship = save.ships[size_t(ship_index)];
	ship.cargo(size_t(ware)) -= result.amount;
	recalculate_cargo(ship);
	save.merchants[ship.owner()].money() += result.price;
	save.towns[size_t(town_index)].stock(size_t(ware)) += uint32_t(result.amount);
	return result;
}

}  // namespace p3::data
