// Lo que compran y venden los barcos de la IA en puerto (las llama 0x518B80).
//
// Un barco de la IA va y viene entre su ciudad base [+0x13A] y la ciudad a la
// que va a comerciar [+0x13B]. En puerto descarga en las oficinas de su dueño
// lo que les falta hasta sus existencias deseadas ([0x354 + 4·w] de la
// oficina), vende el resto al mercado (0x519280, 0x5193F0, 0x5196B0), compra
// lo que más ganancia promete en la ciudad a la que va (0x519920) y se arma
// (0x51A070). La IA cobra 1,2 veces lo que paga la ciudad y paga 5/6 de lo que
// cuesta.
//
// Comprobado con el oráculo en las 12 partidas y, para las ramas que no salen
// en ellas (marcadas «solo en las pruebas sintéticas»), con llamadas al código
// del juego sobre barcos de la IA con la carga, las ciudades, las oficinas y
// la artillería cambiadas al azar.

#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick {

namespace ship_trade_detail {

namespace {

constexpr uint32_t kTradeDifficultyFactor = 0x6DE43C;  // float [+0x64] del objeto de precios 0x6DE3D8.

// 0x672C14: la mercancía se comercia por barriles (1) o por cargas (0). Las
// 4 últimas (armas) siguen en el ejecutable y también valen 1.
constexpr uint8_t kBarrelWare[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
// 0x673684: peso de cada mercancía en la ganancia que espera la IA (0x519920, 0x518FF0).
constexpr uint8_t kTradeWeight[24] = {16, 9, 9, 9, 16, 9, 9, 9, 9, 9, 9, 11, 11, 9, 11, 8, 8, 9, 9, 8, 0, 0, 0, 0};
// 0x67366C (u16): velocidad de cada tipo de barco para los días de viaje (0x516AB0;
// la matriz de 0x531B70 va a la de la snaikka, 635).
constexpr int32_t kShipTypeSpeed[4] = {635, 693, 520, 578};
// 0x672CB4 (u16): peso de cada tipo de pieza de artillería.
constexpr int32_t kArtilleryWeight[6] = {1000, 1000, 2000, 2000, 2000, 1000};
// Factores de precio en los umbrales (prices.cpp): de compra 0x6739EC..0x6739F8 y de
// venta 0x673A08, 0x673A0C; 0x66BC6C es 0.
constexpr float kBuyFactor1 = 1.5f;  // 0x6739EC
constexpr float kBuyFactor2 = 1.0f;  // 0x6739F0
constexpr float kBuyFactor3 = 0x1.99999ap-1f;  // 0x6739F4 (0x3F4CCCCD, 0,8)
constexpr float kBuyFactor4 = 0x1.333334p-1f;  // 0x6739F8 (0x3F19999A, 0,6)
constexpr float kSellFactor1 = 0x1.666666p+0f;  // 0x673A08 (0x3FB33333, 1,4)
constexpr float kSellFactor2 = 1.0f;  // 0x673A0C

int32_t add(int32_t a, int32_t b) { return int32_t(uint32_t(a) + uint32_t(b)); }
int32_t sub(int32_t a, int32_t b) { return int32_t(uint32_t(a) - uint32_t(b)); }

// Lo que recibe la IA al vender por `price`: 6·price/5 (en 32 bits, truncando).
int32_t ai_sale(int32_t price) { return int32_t(uint32_t(price) * 6u) / 5; }

// Unidad a la que redondean la descarga en la oficina 0x519280 y 0x5193F0: las
// armas de diez en diez (solo en las pruebas sintéticas: en las 12 partidas
// los barcos de la IA no llevan armas).
int32_t office_unit(uint32_t w) {
	return w >= 0x14 ? 10 : kBarrelWare[w] ? 200 : 2000;
}

// Redondeo hacia cero a barriles o cargas según 0x672C14.
int32_t round_to_unit(uint32_t w, int32_t amount) {
	return kBarrelWare[w] ? amount / 200 * 200 : amount / 2000 * 2000;
}

// Lo que le falta a la oficina hasta sus existencias deseadas (puede ser negativo).
int32_t office_shortfall(const OfficeMemory& o, uint32_t w) {
	return sub(o.s32(0x354 + 4 * w), o.s32(4 + 4 * w));
}

}  // namespace

// 0x531B70 (thiscall sobre el mundo 0x6DE4A0): lo que se tarda de la ciudad
// `a` a la `b` (en dieciseisavos de día, en una snaikka) según la matriz
// [+0x6C] (SaveGame::town_travel); 0xFF si no hay matriz o alguna no es una ciudad.
uint8_t fn_531b70(const SaveGame& save, uint32_t a, uint32_t b) {
	const uint32_t n = save.globals.u32(global::kWorldTownCount);
	if (save.town_travel.empty() || a >= n || b >= n) {
		return 0xFF;  // Solo en las pruebas sintéticas: no sale en las 12 partidas.
	}
	return save.town_travel[size_t(a) * n + b];
}

// 0x516AB0: días que tardaría el barco de su ciudad [+0x39] a la ciudad
// `town`: el tiempo de 0x531B70 en 256 avos de día, escalado por 635 / la
// velocidad del tipo de barco (0x67366C), redondeado hacia arriba; como poco 1.
int32_t fn_516ab0(const SaveGame& save, int ship, uint32_t town) {
	const ShipMemory& s = save.ships[size_t(ship)].memory;
	int32_t t = int32_t(fn_531b70(save, s.u8(0x39), town)) << 4;
	const uint8_t type = s.u8(0xE);
	if (type & 3) {
		t = t * 635 / kShipTypeSpeed[type & 3];  // Los tipos son 0..3.
	}
	if (t <= 0) {
		return 1;  // Solo en las pruebas sintéticas: no sale en las 12 partidas.
	}
	return (t + 0xFF) >> 8;
}

// 0x52F030 (thiscall sobre el objeto de precios 0x6DE3D8): cuánto de `ware`
// conviene llevar de la ciudad `from` a la `to` si se tarda `days` días. Solo
// si `from` tiene más que la media de sus umbrales 0 y 1 y lo que tendrá `to`
// al llegar (almacén + de camino − consumo neto · días) no pasa de su umbral 1.
// En `factor` deja el margen: factor de precio de venta en `to` menos el de
// compra en `from`. La cantidad sale de igualar el almacén de las dos ciudades
// sobre sus umbrales; 0 si no conviene.
int32_t fn_52f030(const SaveGame& save, uint32_t from, uint32_t to, uint32_t ware, int32_t days, float& factor) {
	const TownMemory& a = save.towns[from].memory;
	const TownMemory& b = save.towns[to].memory;
	const size_t w = ware;
	factor = 0.0f;
	const int32_t stock_a = a.s32(4 + 4 * w);
	const int32_t t0a = a.s32(0x4F0 + 16 * w);
	const int32_t t1a = a.s32(0x4F4 + 16 * w);
	const int32_t middle_a = add(t0a, t1a) >> 1;
	if (middle_a >= stock_a) {
		return 0;
	}
	// Consumo de los habitantes − producción + consumo de las empresas, por día.
	const int32_t net = add(sub(b.s32(0x310 + 4 * w), b.s32(0xC4 + 4 * w)), b.s32(0x64 + 4 * w));
	int32_t stock_b = add(sub(b.s32(0x670 + 4 * w), int32_t(uint32_t(net) * uint32_t(days))), b.s32(4 + 4 * w));
	if (stock_b < 0) {
		stock_b = 0;
	}
	const int32_t t1b = b.s32(0x4F4 + 16 * w);
	if (t1b < stock_b) {
		return 0;
	}
	const int32_t t0b = b.s32(0x4F0 + 16 * w);
	const int32_t step = sub(t1b, t0b) >> 3;
	const int32_t limit_b = add(t0b, int32_t(uint32_t(step) * 3));  // t0 + 3/8 de (t1 − t0)
	// Factor de venta en `to` con su almacén al llegar.
	if (stock_b < t0b) {
		const double difficulty = save.globals.f32(kTradeDifficultyFactor);
		double v = difficulty - double(kSellFactor1);
		v *= double(float(stock_b));
		v /= double(float(t0b));
		factor = float(difficulty - v);
	} else {
		const float slope = float(double(kSellFactor1) - double(kSellFactor2));
		const float x = float(double(float(sub(stock_b, t0b))) * double(slope));
		const float y = float(double(x) / double(float(sub(t1b, t0b))));
		factor = float(double(kSellFactor1) - double(y));
	}
	// Menos el factor de compra en `from`.
	int32_t left_a = stock_a;
	int32_t total = 0;
	const int32_t surplus = sub(stock_a, t1a);
	if (surplus > 0) {
		const int32_t t2a = a.s32(0x4F8 + 16 * w);
		if (stock_a < t2a) {
			const float slope = float(double(kBuyFactor2) - double(kBuyFactor3));
			const float x = float(double(float(sub(t2a, stock_a))) * double(slope));
			const float y = float(double(x) / double(float(sub(t2a, t1a))));
			factor = float(double(factor) - (double(kBuyFactor3) + double(y)));
		} else {
			const int32_t t3a = a.s32(0x4FC + 16 * w);
			if (stock_a < t3a) {
				const float slope = float(double(kBuyFactor3) - double(kBuyFactor4));
				const float x = float(double(float(sub(t3a, stock_a))) * double(slope));
				const float y = float(double(x) / double(float(sub(t3a, t2a))));
				factor = float(double(factor) - (double(kBuyFactor4) + double(y)));
			} else {
				factor = float(double(factor) - double(kBuyFactor4));
			}
		}
		// Lo que pasa del umbral 1 de `from` se lleva entero mientras `to` no pase de su umbral 1.
		const int32_t room_b = sub(t1b, stock_b);
		if (room_b < surplus) {
			return room_b;
		}
		stock_b = add(stock_b, surplus);
		total = surplus;
		left_a = sub(left_a, surplus);
	} else {
		const float slope = float(double(kBuyFactor1) - double(kBuyFactor2));
		// El juego suma la pendiente en vez de multiplicar por ella.
		const float x = float(double(float(sub(t1a, stock_a))) + double(slope));
		const float y = float(double(x) / double(float(sub(t1a, t0a))));
		factor = float(double(factor) - (double(kBuyFactor2) + double(y)));
		if (factor <= 0.0f) {  // 0x66BC6C < factor (o NaN) sigue.
			return 0;
		}
	}
	// Hasta t0 + 3/8 de `to`, lo que tenga `from` sobre la media de sus umbrales 0 y 1.
	const int32_t gap = sub(limit_b, stock_b);
	if (gap >= 0) {
		const int32_t rest = sub(left_a, middle_a);
		if (gap >= rest) {
			return add(total, rest);
		}
		left_a = sub(left_a, gap);
		total = add(total, gap);
		stock_b = add(stock_b, gap);
	}
	// Después, hasta donde se cruzan las rectas de los dos almacenes.
	const int32_t num = int32_t(uint32_t(add(sub(sub(stock_b, limit_b), middle_a), left_a)) * uint32_t(sub(t1b, limit_b)));
	const int32_t den = add(sub(sub(t1b, limit_b), middle_a), t1a);
	if (den == 0 || (num == INT32_MIN && den == -1)) {
		return total;  // Sin comprobar con el oráculo: el juego fallaría (idiv).
	}
	const int32_t meet = add(num / den, limit_b);
	if (meet < stock_b) {
		return total;
	}
	return add(sub(meet, stock_b), total);
}

// 0x5269A0 (thiscall sobre la ciudad): busca en la lista de capitanes de la
// ciudad ([0x82E], enlazados por su u16 [+0]) uno libre ([+8] <= 0x20; 0x4FE150)
// cuyo [+0xF] sea `owner` o, si no hay, uno con [+0xF] = 0xFF. 0xFFFF si no hay.
uint16_t fn_5269a0(const SaveGame& save, int town, uint16_t owner) {
	const TownMemory& t = save.towns[size_t(town)].memory;
	auto count = [&save] { return uint32_t(save.globals.u16(global::kCaptainCount)); };
	auto busy = [](const CaptainMemory& c) { return c.u8(8) > 0x20; };  // 0x4FE150
	for (uint32_t i = t.u16(0x82E); i < count(); i = save.captains[i].u16(0)) {
		const CaptainMemory& c = save.captains[i];
		if (!busy(c) && c.u8(0xF) == owner) {
			return uint16_t(i);  // Comprobado en las pruebas sintéticas.
		}
	}
	for (uint32_t i = t.u16(0x82E); i < count(); i = save.captains[i].u16(0)) {
		const CaptainMemory& c = save.captains[i];
		if (!busy(c) && c.u8(0xF) == 0xFF) {
			return uint16_t(i);
		}
	}
	return 0xFFFF;
}

// 0x526AA0 (thiscall sobre la ciudad): saca al capitán `captain` de la lista
// de capitanes de la ciudad. false si no está.
bool fn_526aa0(SaveGame& save, int town, uint32_t captain) {
	const uint16_t count = save.globals.u16(global::kCaptainCount);
	uint16_t* link = &save.towns[size_t(town)].memory.u16(0x82E);
	if (*link >= count) {
		return false;  // Sin comprobar con el oráculo.
	}
	for (;;) {
		const uint32_t i = *link;
		CaptainMemory& c = save.captains[i];
		if (i == captain) {
			*link = c.u16(0);
			c.u16(0) = 0xFFFF;
			return true;
		}
		if (c.u16(0) >= count) {
			return false;  // Sin comprobar con el oráculo.
		}
		link = &c.u16(0);  // Solo en las pruebas sintéticas: no sale en las 12 partidas.
	}
}

}  // namespace ship_trade_detail

using namespace ship_trade_detail;

// 0x519280: el barco llega a la ciudad a la que iba a comerciar: deja en la
// oficina de su dueño en la ciudad base [+0x13A] lo que le falta (en
// unidades enteras) y vende el resto a la ciudad en la que está.
void fn_519280(SaveGame& save, int ship) {
	ShipMemory& s = save.ships[size_t(ship)].memory;
	const int merchant = s.u8(0);
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const int office = facilities_detail::fn_5308a0(save, s.u8(0), s.u8(0x13A));
	if (office >= 0) {
		OfficeMemory& o = save.offices[size_t(office)].memory;
		for (uint32_t w = 0; w < 24; ++w) {
			int32_t cargo = s.s32(0x54 + 4 * w);
			if (cargo <= 0) {
				s.s32(0x54 + 4 * w) = 0;
				continue;
			}
			int32_t room = office_shortfall(o, w);
			room -= room % office_unit(w);
			if (room <= 0) {
				continue;
			}
			if (room > cargo) {
				room = cargo;
			}
			s.s32(0x54 + 4 * w) = cargo - room;
			o.u32(4 + 4 * w) += uint32_t(room);
		}
	}
	const int town = s.u8(0x39);
	for (uint32_t w = 0; w < 24; ++w) {
		const int32_t cargo = s.s32(0x54 + 4 * w);
		if (cargo > 0) {
			const int32_t price = game_sell_price(save, int(w), town, cargo);
			s.u32(0x124) += uint32_t(price);  // [+0x124] Saldo del comercio del barco.
			m.s32(0) = add(m.s32(0), ai_sale(price));
			offices_detail::fn_4f8690(save, merchant, uint32_t(town), w, price, cargo, true);
			save.towns[size_t(town)].memory.u32(4 + 4 * w) += uint32_t(cargo);
		}
		s.s32(0x54 + 4 * w) = 0;
	}
	s.u8(0x3C) |= 4;
}

// 0x5193F0: el barco está en una ciudad de paso y vuelve a su ciudad base
// [+0x13A]. De cada mercancía reserva lo que le falta a la ciudad base hasta
// su umbral 1 y a las oficinas de su dueño en la ciudad base y en la de
// destino [+0x38]; con el resto llena la oficina de la ciudad en la que está
// y vende a la ciudad, en barriles, hasta su umbral 1.
void fn_5193f0(SaveGame& save, int ship) {
	ShipMemory& s = save.ships[size_t(ship)].memory;
	const uint32_t owner = s.u8(0);
	const int home = s.u8(0x13A);
	const int here = s.u8(0x39);
	const int home_office = facilities_detail::fn_5308a0(save, owner, uint32_t(home));
	const int here_office = facilities_detail::fn_5308a0(save, owner, uint32_t(here));
	MerchantMemory& m = save.merchants[owner].memory;
	const int destination_office = s.u8(0x38) != s.u8(0x13A) ? facilities_detail::fn_5308a0(save, owner, s.u8(0x38)) : -1;
	const TownMemory& h = save.towns[size_t(home)].memory;
	TownMemory& t = save.towns[size_t(here)].memory;
	for (uint32_t w = 0; w < 24; ++w) {
		const int32_t cargo = s.s32(0x54 + 4 * w);
		int32_t spare = cargo;
		const int32_t t1 = h.s32(0x4F4 + 16 * w);
		const int32_t stock = h.s32(4 + 4 * w);
		if (t1 > stock) {
			spare = add(spare, sub(stock, t1));
		}
		if (home_office >= 0) {
			const OfficeMemory& o = save.offices[size_t(home_office)].memory;
			if (o.s32(0x354 + 4 * w) > o.s32(4 + 4 * w)) {
				spare = add(spare, sub(o.s32(4 + 4 * w), o.s32(0x354 + 4 * w)));
			}
		}
		if (here_office >= 0 && spare > 0) {
			OfficeMemory& o = save.offices[size_t(here_office)].memory;
			int32_t room = office_shortfall(o, w);
			room -= room % office_unit(w);
			if (room > 0) {
				// Solo en las pruebas sintéticas: no sale en las 12 partidas.
				if (room > spare) {
					room = spare;
				}
				s.s32(0x54 + 4 * w) = cargo - room;
				spare -= room;
				o.u32(4 + 4 * w) += uint32_t(room);
			}
		}
		if (destination_office >= 0) {
			// Solo en las pruebas sintéticas: no sale en las 12 partidas.
			const OfficeMemory& o = save.offices[size_t(destination_office)].memory;
			if (o.s32(0x354 + 4 * w) > o.s32(4 + 4 * w)) {
				spare = add(spare, sub(o.s32(4 + 4 * w), o.s32(0x354 + 4 * w)));
			}
		}
		const int32_t wanted = sub(t.s32(0x4F4 + 16 * w), t.s32(4 + 4 * w));
		if (wanted <= 0) {
			continue;
		}
		if (wanted < spare) {
			spare = wanted;
		}
		if (spare <= 0) {
			continue;
		}
		const int32_t amount = spare / 200 * 200;
		const int32_t price = game_sell_price(save, int(w), here, amount);
		s.u32(0x124) += uint32_t(price);
		m.s32(0) = add(m.s32(0), ai_sale(price));
		offices_detail::fn_4f8690(save, int(owner), uint32_t(here), w, price, amount, true);
		t.u32(4 + 4 * w) += uint32_t(amount);
		s.u32(0x54 + 4 * w) -= uint32_t(amount);
	}
	s.u8(0x3C) |= 4;
}

// 0x5196B0: el barco está en una ciudad de paso camino de la ciudad a la que
// va a comerciar [+0x13B]. Reserva lo que le falta a la oficina de su dueño
// allí (dos veces si [+0x38] no es esa ciudad: el juego vuelve a pedir la
// oficina de [+0x13B] y no la de [+0x38]), llena la oficina de la ciudad en la
// que está y vende el resto, en barriles o cargas. En las 12 partidas solo
// sale con la bodega vacía: lo demás, solo en las pruebas sintéticas.
void fn_5196b0(SaveGame& save, int ship) {
	ShipMemory& s = save.ships[size_t(ship)].memory;
	const uint32_t owner = s.u8(0);
	MerchantMemory& m = save.merchants[owner].memory;
	int32_t spare[24];
	for (uint32_t w = 0; w < 24; ++w) {
		spare[w] = s.s32(0x54 + 4 * w);
	}
	auto reserve = [&](int office) {
		if (office < 0) {
			return;
		}
		const OfficeMemory& o = save.offices[size_t(office)].memory;
		for (uint32_t w = 0; w < 24; ++w) {
			const int32_t missing = office_shortfall(o, w);
			if (missing > 0) {
				spare[w] = sub(spare[w], missing);
			}
		}
	};
	reserve(facilities_detail::fn_5308a0(save, owner, s.u8(0x13B)));
	const int here = s.u8(0x39);
	const int here_office = facilities_detail::fn_5308a0(save, owner, uint32_t(here));
	if (here_office >= 0) {
		OfficeMemory& o = save.offices[size_t(here_office)].memory;
		for (uint32_t w = 0; w < 24; ++w) {
			const int32_t left = spare[w];
			if (left <= 0) {
				continue;
			}
			int32_t room = office_shortfall(o, w);
			if (room <= 0) {
				continue;
			}
			room = round_to_unit(w, room);
			if (room <= left) {
				spare[w] = left - room;
			} else {
				room = left;
				spare[w] = 0;
			}
			s.u32(0x54 + 4 * w) -= uint32_t(room);
			o.u32(4 + 4 * w) += uint32_t(room);
		}
	}
	if (s.u8(0x38) != s.u8(0x13B)) {
		reserve(facilities_detail::fn_5308a0(save, owner, s.u8(0x13B)));
	}
	TownMemory& t = save.towns[size_t(here)].memory;
	for (uint32_t w = 0; w < 24; ++w) {
		if (spare[w] <= 0) {
			continue;
		}
		spare[w] = round_to_unit(w, spare[w]);
		const int32_t amount = spare[w];
		const int32_t price = game_sell_price(save, int(w), here, amount);
		s.u32(0x124) += uint32_t(price);
		m.s32(0) = add(m.s32(0), ai_sale(price));
		offices_detail::fn_4f8690(save, int(owner), uint32_t(here), w, price, amount, true);
		t.u32(4 + 4 * w) += uint32_t(amount);
		s.u32(0x54 + 4 * w) -= uint32_t(amount);
	}
	s.u8(0x3C) |= 4;
}

// 0x519920: compra en la ciudad en la que está lo que más ganancia promete
// llevar a `town` (0x52F030 por el peso de 0x673684), mercancía a mercancía de
// más a menos, mientras quede bodega libre.
void fn_519920(SaveGame& save, int ship, uint32_t town) {
	SavedShip& saved = save.ships[size_t(ship)];
	ShipMemory& s = saved.memory;
	const uint32_t to = town & 0xFFFF;
	const int merchant = s.u8(0);
	MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	const int32_t days = fn_516ab0(save, ship, to);
	int32_t amount[24];
	float gain[24];
	for (uint32_t w = 0; w < 24; ++w) {
		amount[w] = fn_52f030(save, s.u8(0x39), to, w, days, gain[w]);
		if (amount[w] > 0) {
			// Menos de 5 barriles o 3 cargas no compensa.
			if (amount[w] < (kBarrelWare[w] ? 1000 : 6000)) {
				amount[w] = 0;
				gain[w] = 0.0f;
			}
			gain[w] = float(double(float(kTradeWeight[w])) * double(gain[w]));
		} else {
			gain[w] = 0.0f;
			amount[w] = 0;
		}
	}
	int32_t free = recalculate_cargo(saved);
	const int here = s.u8(0x39);
	TownMemory& t = save.towns[size_t(here)].memory;
	while (free > 0) {
		uint32_t best = 0;
		for (uint32_t k = 1; k < 24; ++k) {
			if (gain[best] < gain[k]) {
				best = k;
			}
		}
		if (amount[best] <= 0) {
			break;
		}
		if (amount[best] > free) {
			amount[best] = free;
		}
		if (t.s32(4 + 4 * best) < amount[best]) {
			amount[best] = t.s32(4 + 4 * best);  // Sin comprobar con el oráculo (0x52F030 no pide más del almacén).
		}
		amount[best] = round_to_unit(best, amount[best]);
		const int32_t bought = amount[best];
		if (bought <= 0) {
			gain[best] = 0.0f;
			continue;
		}
		s.u32(0x54 + 4 * best) += uint32_t(bought);
		free -= bought;
		// El precio se calcula ya con el almacén de la ciudad descontado.
		t.u32(4 + 4 * best) -= uint32_t(bought);
		const int32_t price = game_buy_price(save, int(best), here, bought);
		m.s32(0) = add(m.s32(0), int32_t(0u - uint32_t(price) * 5u) / 6);
		s.u32(0x124) -= uint32_t(price);
		offices_detail::fn_4f8840(save, merchant, uint32_t(here), best, price, bought, true);
		amount[best] = 0;
		gain[best] = 0.0f;
	}
	s.u8(0x3C) |= 4;
}

// 0x51A070: el barco se arma en puerto (los de tipo 0 no: solo pierden la orden
// 0x10 de [+0x136]). Monta la artillería que haya en el almacén de armas de
// `town` ([0x124 + 4·k] de la ciudad, del tipo 5 al 0) en los huecos libres
// (0xFF): las piezas de 2000 en dos huecos seguidos libres (el segundo queda a
// 6) y las de 1000 en huecos con el siguiente a 7 o en cualquier hueco libre;
// contrata un capitán en la ciudad [+0x39] si no tiene y compra allí espadas
// (mercancía 20, 10 unidades por marinero) para los marineros que no tienen.
void fn_51a070(SaveGame& save, int ship, int town) {
	SavedShip& saved = save.ships[size_t(ship)];
	ShipMemory& s = saved.memory;
	if (s.u8(0xE) == 0) {
		s.u8(0x136) &= 0xEF;
		return;  // Solo en las pruebas sintéticas: no sale en las 12 partidas.
	}
	TownMemory& armory = save.towns[size_t(town)].memory;
	uint32_t big = 0;  // Siguiente par de huecos para las piezas de 2000.
	uint32_t pair = 0;  // Siguiente par (0xFF, 7) para las de 1000.
	uint32_t single = 0;  // Siguiente hueco suelto para las de 1000.
	for (int k = 5; k >= 0; --k) {
		const int32_t weight = kArtilleryWeight[k];
		int32_t& stock = armory.s32(0x124 + 4 * size_t(k));
		if (stock < weight) {
			continue;
		}
		if (weight == 2000) {
			for (; big < 24; big += 2) {
				if (s.u8(0x13C + big) == 0xFF && s.u8(0x13D + big) == 0xFF) {
					s.u8(0x13C + big) = uint8_t(k);
					s.u8(0x13D + big) = 6;
					stock = sub(stock, weight);
					if (stock < weight) {
						break;
					}
				}
			}
			continue;
		}
		for (; pair < 24; pair += 2) {
			if (s.u8(0x13C + pair) == 0xFF && s.u8(0x13D + pair) == 7) {
				// Solo en las pruebas sintéticas: no sale en las 12 partidas.
				s.u8(0x13C + pair) = uint8_t(k);
				stock = sub(stock, weight);
				if (stock < weight) {
					break;  // El juego sigue con los huecos sueltos y aún puede montar una pieza más.
				}
			}
		}
		for (; single < 24; ++single) {
			if (s.u8(0x13C + single) == 0xFF) {
				s.u8(0x13C + single) = uint8_t(k);
				stock = sub(stock, weight);
				if (stock < weight) {
					break;
				}
			}
		}
	}
	const int here = s.u8(0x39);
	const uint16_t captains = save.globals.u16(global::kCaptainCount);
	if (s.u16(0x42) >= captains) {
		const uint16_t captain = fn_5269a0(save, here, save.globals.u16(global::kWorldMerchantCount));
		s.u16(0x42) = captain;
		if (captain < captains) {
			fn_526aa0(save, here, captain);
		}
	}
	const uint16_t weapons = s.u16(0x154);
	const uint16_t sailors = s.u16(0x40);
	if (weapons < sailors) {
		const int32_t need = (int32_t(sailors) - int32_t(weapons)) * 10;
		int32_t& swords = save.towns[size_t(here)].memory.s32(0x54);
		if (swords >= need) {
			swords -= need;
			s.u16(0x154) = sailors;
		} else {
			s.u16(0x154) = uint16_t(swords / 10 + weapons);
			swords %= 10;
		}
	}
	recalculate_cargo(saved);
}

}  // namespace p3::data::tick
