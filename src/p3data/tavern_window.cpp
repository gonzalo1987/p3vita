#include "p3data/tavern_window.hpp"

#include <algorithm>
#include <tuple>
#include <cstring>

#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data {

namespace tick::ship_trade_detail {
uint16_t fn_5269a0(const SaveGame& save, int town, uint16_t owner);
}  // namespace tick::ship_trade_detail
namespace tick::events_detail {
uint32_t fn_5261d0(const SaveGame& save, uint32_t town);
}  // namespace tick::events_detail

namespace {

constexpr uint32_t kEventCount = 0x6DD73C + 0xC;           // u16
constexpr uint32_t kDockedHead = global::kShips + 0xE8;     // u16 the first ship of the docked list (0x6DD888)
constexpr uint32_t kConvoyCount = global::kShips + 0xF6;    // u16
constexpr uint32_t kTraderWares = 0x47FAF;                  // the mask of 0x5265E0 and 0x526650
constexpr int32_t kTraderReserve = 40000;
constexpr int32_t kTownSellerMost = 30000;
constexpr uint8_t kInBarrels[20] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0};  // 0x672C14

uint32_t player_of(const SaveGame& save) {
	return save.globals.u32(global::kPlayer);
}

const TownMemory& town_memory(const SaveGame& save, uint32_t town) {
	return save.towns[town].memory;
}

bool valid_town(const SaveGame& save, uint32_t town) {
	return town < save.globals.u32(global::kWorldTownCount) && town < save.towns.size();
}

// A free or the player's place of the town: 0xFF (any byte from 0xFF up) or the player.
bool free_or_player(const SaveGame& save, uint8_t holder) {
	return holder == 0xFF || holder == player_of(save);
}

void put32(OperationBytes& op, size_t at, uint32_t v) {
	std::memcpy(op.data() + at, &v, 4);
}

// 0x5182B0's room, asked of a copy (it writes the hold).
int32_t room_of(const SaveGame& save, uint32_t ship) {
	SavedShip copy = save.ships[ship];
	return recalculate_cargo(copy);
}

bool has_office(const SaveGame& save, uint32_t town) {
	return tick::facilities_detail::fn_5308a0(save, player_of(save), town) >= 0;
}

}  // namespace

bool tavern_order_client(const SaveGame& save, uint32_t town) {
	const TownMemory& t = town_memory(save, town);
	const uint16_t events = save.globals.u16(kEventCount);
	for (size_t k = 0; k < 2; ++k) {
		const uint16_t e = t.u16(0x830 + 2 * k);
		if (e < events && e < save.events.size() && save.events[e].u8(0x16) == player_of(save)) {
			return true;
		}
	}
	return false;
}

bool tavern_trader(const SaveGame& save, uint32_t town) {
	const TownMemory& t = town_memory(save, town);
	const uint8_t holder = t.u8(0x83E);
	if (holder < save.globals.u16(global::kWorldMerchantCount) && holder != player_of(save)) {
		return false;
	}
	for (uint32_t ware = 0; ware < 20; ++ware) {
		if (((kTraderWares >> ware) & 1) &&
			t.s32(4 + 4 * ware) > int32_t(uint32_t(t.s32(0x4F4 + 0x10 * ware)) + uint32_t(kTraderReserve))) {
			return true;
		}
	}
	return false;
}

bool tavern_traveller(const SaveGame& save, uint32_t town) {
	return free_or_player(save, town_memory(save, town).u8(0x83F));
}

bool tavern_weapons_dealer(const SaveGame& save, uint32_t town) {
	const TownMemory& t = town_memory(save, town);
	if (!(t.u8(0x2C8) & 0x10) && town_memory(save, t.u8(0x2C0)).u16(0x874) > 0) {
		bool stocked = false;
		for (uint32_t k = 0; k < 4 && !stocked; ++k) {
			stocked = t.s32(0x54 + 4 * k) >= 10;
		}
		if (!stocked && t.u8(0x83D) != player_of(save)) {
			return false;
		}
	}
	return free_or_player(save, t.u8(0x83D));
}

bool tavern_burglar(const SaveGame& save, uint32_t town) {
	const TownMemory& t = town_memory(save, town);
	const uint8_t holder = t.u8(0x83C);
	if (holder != 0xFF) {
		return holder == player_of(save);
	}
	const uint16_t offices = save.globals.u16(global::kWorldOfficeCount);
	const uint16_t first = t.u16(0x784);
	if (first >= offices || first >= save.offices.size()) {
		return false;
	}
	return save.offices[first].memory.u16(0x2CA) < offices;
}

std::array<bool, kTavernPeople> tavern_people(const SaveGame& save, uint32_t town) {
	std::array<bool, kTavernPeople> there{};
	if (!valid_town(save, town)) {
		return there;
	}
	const uint32_t player = player_of(save);
	const uint16_t captains = save.globals.u16(global::kCaptainCount);
	there[int(TavernPerson::OrderClient)] = tavern_order_client(save, town);
	if (!there[int(TavernPerson::OrderClient)]) {
		there[int(TavernPerson::Trader)] = tavern_trader(save, town);
	}
	if (player < save.merchants.size()) {
		there[int(TavernPerson::Sailors)] = tick::fn_4f6ca0(save, save.merchants[player].memory, town) > 0;
	}
	there[int(TavernPerson::Captain)] =
		tick::ship_trade_detail::fn_5269a0(save, int(town), uint16_t(player)) < captains;
	there[int(TavernPerson::Informer)] = town_memory(save, town).u8(0x82C) < 0x14;
	there[int(TavernPerson::Traveller)] = tavern_traveller(save, town);
	there[int(TavernPerson::WeaponsDealer)] = tavern_weapons_dealer(save, town);
	there[int(TavernPerson::Burglar)] = tavern_burglar(save, town);
	there[int(TavernPerson::Pirate)] = tick::events_detail::fn_5261d0(save, town) < captains;
	return there;
}

OperationBytes tavern_talk_operation(SaveGame& save, uint32_t merchant, uint32_t town, uint32_t kind) {
	uint32_t draw = 0;
	switch (kind & 0xFF) {
	case 3:
		draw = tick::game_rand(save) << 16;
		draw += tick::ship_trade_detail::fn_5269a0(save, int(town), uint16_t(player_of(save)));
		break;
	case 8:
		draw = tick::game_rand(save) << 16;
		draw += tick::events_detail::fn_5261d0(save, town);
		break;
	case 6:
	case 7:
		draw = tick::game_rand(save);
		break;
	default:
		break;  // 9, the mission letter, is the mission interpreter's: never asked here.
	}
	OperationBytes op{};
	put32(op, 0, 0x52);
	put32(op, 4, draw);
	put32(op, 8, merchant);
	put32(op, 0xC, town);
	put32(op, 0x10, kind & 0xFF);
	return op;
}

OperationBytes tavern_leave_operation(uint32_t player, uint32_t town) {
	OperationBytes op{};
	put32(op, 0, 0x52);
	put32(op, 4, 0xFFFFFFFF);
	put32(op, 8, player);
	put32(op, 0xC, town);
	put32(op, 0x10, 10);
	return op;
}

std::pair<std::string, std::string> merchant_names(const SaveGame& save, uint32_t merchant) {
	if (merchant >= save.merchants.size()) {
		return {};
	}
	const MerchantMemory& m = save.merchants[merchant].memory;
	const uint32_t first = m.u8(0x1A) ? (m.u8(0x1C) & 0x7Fu) : (m.u8(0x1C) | 0x80u);
	return {game_first_name(save, first), game_family_name(save, m.u8(0x1D))};
}

int tavern_greeting(SaveGame& save) {
	// rand() / 32768.0f is exact (a power of two), and so is its product by 3.0 in the x87's precision.
	return int(tick::game_rand(save) * 3 / 32768);
}

int32_t TraderOffer::units() const {
	return int32_t(amount) * (ware < 20 && kInBarrels[ware] ? 200 : 2000);
}

std::optional<TraderOffer> trader_offer(const SaveGame& save, uint32_t town) {
	if (!valid_town(save, town)) {
		return std::nullopt;
	}
	const GlobalMemory& g = save.globals;
	const TownMemory& t = town_memory(save, town);
	int32_t best = 0;
	uint32_t ware = 0xFF;
	for (uint32_t w = 0; w < 20; ++w) {
		const int32_t surplus =
			int32_t(uint32_t(t.s32(4 + 4 * w)) - uint32_t(t.s32(0x4F4 + 0x10 * w)) - uint32_t(kTraderReserve));
		if (((kTraderWares >> w) & 1) && surplus > best) {
			best = surplus;
			ware = w;
		}
	}
	if (ware >= 0x14) {
		return std::nullopt;
	}
	const uint8_t here = t.u8(0x2C0);
	const uint32_t ships = g.u16(global::kShipCount);
	uint32_t next = g.u16(kDockedHead);
	uint8_t seller = 0;
	int32_t most = 0;
	while (next < ships) {
		const ShipMemory& s = save.ships[next].memory;
		next = s.u16(0x6);  // The game moves on before it looks at the ship.
		if (s.u8(0x39) == here && s.u8(0) < g.u16(global::kWorldMerchantCount)) {
			const MerchantMemory& m = save.merchants[s.u8(0)].memory;
			if (m.u8(0x19) != here && (m.u8(0x8) & 4)) {
				most = s.s32(0x10);
				seller = s.u8(0);
				break;
			}
		}
	}
	if (next >= ships) {
		// None, or the one found was the last of the list: the town's own merchant.
		seller = here;
		most = kTownSellerMost;
	}
	const int32_t has = int32_t(uint32_t(t.s32(4 + 4 * ware)) - uint32_t(t.s32(0x4F4 + 0x10 * ware)));
	most = std::min(most, has);
	TraderOffer offer;
	offer.seller = seller;
	offer.tick = g.u32(global::kWorldTick);
	offer.ware = uint8_t(ware);
	const int32_t unit = kInBarrels[ware] ? 200 : 2000;
	offer.amount = uint16_t(most / unit);
	if (offer.amount < 1) {
		offer.amount = 1;
	}
	const int32_t units = int32_t(offer.amount) * unit;
	const int32_t sell = tick::game_sell_price(save, int(ware), int(here), units);  // 0x52E1D0
	const int32_t buy = tick::game_buy_price(save, int(ware), int(here), units);    // 0x52E430
	offer.price = int32_t(uint32_t(sell) + uint32_t(buy)) >> 1;
	offer.buyer = uint8_t(player_of(save));
	offer.ship = 0xFFFF;
	return offer;
}

std::vector<uint16_t> trader_ships(const SaveGame& save, uint32_t town, int32_t units) {
	std::vector<uint16_t> list;
	const GlobalMemory& g = save.globals;
	const uint32_t player = player_of(save);
	if (player >= save.merchants.size()) {
		return list;
	}
	const uint32_t ships = g.u16(global::kShipCount);
	const uint32_t convoys = g.u16(kConvoyCount);
	for (uint32_t i = save.merchants[player].memory.u16(0xE); i < ships; i = save.ships[i].memory.u16(0x4)) {
		const ShipMemory& s = save.ships[i].memory;
		if (s.u8(0x39) != town || s.u16(0x134) >= 4) {
			continue;
		}
		if (s.u16(0x8) >= convoys) {
			if (room_of(save, i) >= units) {
				list.push_back(uint16_t(i));
			}
			continue;
		}
		const GameMemory<0x3C>& c = save.convoys[s.u16(0x8)];
		if (c.u16(0x10) != i) {
			continue;  // Only the guide.
		}
		int32_t room = 0;
		uint32_t j = c.u16(0xA);
		while (j < ships) {
			room += room_of(save, j);
			if (save.ships[j].memory.u8(0) != player) {
				break;
			}
			j = save.ships[j].memory.u16(0x6);
		}
		if (j > ships && room >= units) {
			list.push_back(uint16_t(i));
		}
	}
	return list;
}

void trader_open(const SaveGame& save, uint32_t town, TraderWindow& w) {
	if (!w.offer) {
		w.offer = trader_offer(save, town);
	}
	const std::vector<uint16_t> ships = trader_ships(save, town, w.offer ? w.offer->units() : 0);
	if (!ships.empty()) {
		w.ship = ships[0];
	}
	w.office = false;
	w.confirmed = false;
	w.placed = false;
	w.convoy = false;
	w.afforded = false;
	w.no_place = false;
	w.done = false;
}

void trader_refresh(const SaveGame&, uint32_t, TraderWindow& w, const std::vector<uint16_t>& before,
	const std::vector<uint16_t>& now) {
	if (before.size() != now.size() && before.empty() && !now.empty()) {
		w.ship = now[0];
	}
}

bool trader_arrows(const SaveGame& save, uint32_t town, const std::vector<uint16_t>& ships) {
	return (has_office(save, town) && ships.size() == 1) || ships.size() > 1;
}

void tavern_turn(const SaveGame& save, uint32_t town, bool& office, uint16_t& ship, const std::vector<uint16_t>& ships,
	int step) {
	const size_t n = ships.size();
	if (n == 0) {
		office = true;
		return;
	}
	const size_t at = size_t(std::find(ships.begin(), ships.end(), ship) - ships.begin());  // n: not there
	const uint16_t past = save.globals.u16(global::kShipCount);
	if (step < 0) {
		ship = at > 0 ? ships[at - 1] : ships[n - 1];
		if (at == 0) {
			if (has_office(save, town) && !office) {
				office = true;
				ship = past;
			} else {
				office = false;
				ship = ships[n - 1];
			}
		} else if (at == n) {
			office = false;
			ship = ships[n - 1];
		}
	} else {
		ship = at + 1 < n ? ships[at + 1] : ships[0];
		if (at == n - 1) {
			if (has_office(save, town) && !office) {
				office = true;
				ship = past;
			} else {
				office = false;
				ship = ships[0];
			}
		} else if (at == n) {
			office = false;
			ship = ships[0];
		}
	}
}

void trader_turn(const SaveGame& save, uint32_t town, TraderWindow& w, const std::vector<uint16_t>& ships, int step) {
	tavern_turn(save, town, w.office, w.ship, ships, step);
}

std::optional<OperationBytes> trader_press(const SaveGame& save, uint32_t town, TraderWindow& w,
	const std::vector<uint16_t>& ships) {
	if (!w.offer) {
		return std::nullopt;  // The game would read the offer of nothing.
	}
	const uint32_t player = player_of(save);
	const uint16_t past = save.globals.u16(global::kShipCount);
	const int32_t money = save.merchants[player].memory.s32(0);
	std::optional<OperationBytes> op;
	w.afforded = false;
	if (w.confirmed) {
		if (money >= w.offer->price) {
			w.afforded = true;
			op = trader_operation(*w.offer, player, town, w.office ? past : w.ship);
			w.offer.reset();
		}
		w.placed = true;
		w.done = true;
		return op;
	}
	if (money >= w.offer->price) {
		w.confirmed = true;
		w.afforded = true;
		w.no_place = true;
		if (has_office(save, town)) {
			w.office = true;
			w.placed = true;
			w.no_place = false;
			w.ship = past;
		}
		if (ships.size() > 1) {
			w.placed = false;
			w.no_place = false;
		} else if (ships.size() == 1) {
			w.placed = !w.placed;
			w.no_place = false;
		}
		if (w.placed) {
			op = trader_operation(*w.offer, player, town, w.office ? past : w.ship);
			w.offer.reset();
			if (!ships.empty()) {
				w.convoy = w.ship < past && save.ships[w.ship].memory.u16(0x8) < save.globals.u16(kConvoyCount);
			}
		}
	}
	w.done = w.no_place || w.placed || !w.afforded;
	return op;
}

OperationBytes trader_operation(const TraderOffer& offer, uint32_t player, uint32_t town, uint16_t ship) {
	OperationBytes op{};
	put32(op, 0, 0x4D);
	put32(op, 4, offer.tick);
	put32(op, 8, uint32_t(offer.price));
	std::memcpy(op.data() + 0xC, &offer.amount, 2);
	std::memcpy(op.data() + 0xE, &ship, 2);
	op[0x10] = offer.seller;
	op[0x11] = uint8_t(player);
	op[0x12] = offer.ware;
	op[0x13] = uint8_t(town);
	return op;
}

namespace {

constexpr uint8_t kSellTop[4] = {180, 144, 216, 252};    // 0x673780
constexpr uint8_t kSellFactor[4] = {80, 64, 96, 112};    // 0x673784
constexpr uint8_t kSellLeast[4] = {60, 48, 72, 84};      // 0x673788
constexpr uint16_t kBuyTop[4] = {350, 280, 420, 490};    // 0x67378C
constexpr uint16_t kBuyFactor[4] = {250, 200, 300, 350};  // 0x673794
constexpr uint8_t kBuyLeast[4] = {70, 56, 84, 98};       // 0x67379C

// (sum · factor) / 50 in a town below 500 people ([+0x2D4]), else · 10 / them; from the top, at least the least.
int32_t dealer_price(const TownMemory& t, int32_t sum, uint32_t factor, int32_t top, int32_t least) {
	const int32_t weighed = int32_t(uint32_t(sum) * factor);
	const int32_t people = t.s32(0x2D4);
	const int32_t off = people < 500 ? weighed / 50 : int32_t(uint32_t(weighed) * 10) / people;
	const int32_t price = top - off;
	return price < least ? least : price;
}

// 0x5263D0 (on the town: kind, units): the price of one bought.
int32_t fn_5263d0(const TownMemory& t, uint32_t kind, int32_t units) {
	kind &= 3;
	const int32_t stock = t.s32(0x54 + 4 * kind);
	if (stock < 10) {
		return 0;
	}
	const int32_t taken = units > stock ? stock : units;
	return dealer_price(t, stock - (taken >> 1), kBuyFactor[kind], kBuyTop[kind], kBuyLeast[kind]);
}

}  // namespace

DealerPrices fn_526470(const SaveGame& save, uint32_t town, uint32_t step, const std::array<int32_t, 4>& have) {
	DealerPrices p;
	const TownMemory& t = town_memory(save, town);
	for (uint32_t k = 0; k < 4; ++k) {
		int32_t sold = 0;
		int32_t bought = 0;
		switch (step) {
		case 0:
			sold = bought = 10;
			break;
		case 1:
			sold = bought = 50;
			break;
		case 2:
			sold = bought = 500;
			break;
		default:
			bought = t.s32(0x54 + 4 * k) / 10 * 10;
			sold = int32_t(uint32_t(have[k]) * 10);
			break;
		}
		p.town[k] = t.s32(0x54 + 4 * k) / 10;
		p.buy[k] = fn_5263d0(t, k, bought);
		p.sell[k] = sold > 0 ? dealer_price(t, t.s32(0x54 + 4 * k) + (sold >> 1), kSellFactor[k], kSellTop[k], kSellLeast[k])
							 : 0;
	}
	return p;
}

std::array<int32_t, 4> dealer_have(const SaveGame& save, uint32_t town, bool office, uint16_t ship) {
	std::array<int32_t, 4> have{};
	if (office) {
		const int o = tick::facilities_detail::fn_5308a0(save, player_of(save), town);
		if (o >= 0) {
			for (uint32_t k = 0; k < 4; ++k) {
				have[k] = save.offices[size_t(o)].memory.s32(0x54 + 4 * k) / 10;
			}
		}
	} else if (ship < save.ships.size()) {
		for (uint32_t k = 0; k < 4; ++k) {
			have[k] = save.ships[ship].memory.s32(0xA4 + 4 * k) / 10;
		}
	}
	return have;
}

OperationBytes dealer_operation(const SaveGame& save, uint32_t town, uint32_t kind, bool buy, uint32_t step, bool office,
	uint16_t ship) {
	const std::array<int32_t, 4> have = dealer_have(save, town, office, ship);
	const DealerPrices prices = fn_526470(save, town, step, have);
	kind &= 3;
	uint32_t price = 0;
	int32_t count = 0;
	if (buy) {
		price = uint32_t(prices.buy[kind]);
		static constexpr int32_t kSteps[3] = {1, 5, 50};
		count = step < 3 ? kSteps[step] : prices.town[kind];
	} else {
		price = have[kind] == 0 ? 0 : uint32_t(prices.sell[kind]);
		const int32_t now = dealer_have(save, town, office, ship)[kind];
		switch (step) {
		case 0:
			count = 1;
			break;
		case 1:
			count = uint32_t(now) > 5 ? 5 : now;
			break;
		case 2:
			count = uint32_t(now) > 50 ? 50 : now;
			break;
		default:
			count = now;
			break;
		}
	}
	OperationBytes op{};
	put32(op, 0, 0x50);
	put32(op, 4, price);
	put32(op, 8, uint32_t(count));
	op[0xC] = uint8_t(town);
	op[0xD] = uint8_t(player_of(save));
	op[0xE] = uint8_t(kind);
	op[0xF] = buy ? 1 : 0;
	const uint16_t place = office ? save.globals.u16(global::kShipCount) : ship;
	std::memcpy(op.data() + 0x10, &place, 2);
	return op;
}

TravellerJob traveller_job(const SaveGame& save, uint32_t town) {
	TravellerJob j;
	const TownMemory& t = town_memory(save, town);
	j.destination = t.u8(0x834);
	j.payment = t.u16(0x836);
	j.deadline = t.u32(0x838);
	tick::events_mayor_detail::fn_5311d0(j.deadline, j.day, j.month, j.year);
	return j;
}

std::vector<uint16_t> traveller_ships(const SaveGame& save, uint32_t town) {
	const uint8_t destination = town_memory(save, town).u8(0x834);
	const bool small = destination < save.towns.size() && (town_memory(save, destination).u32(0x2C8) & 1);
	return window_ships(save, int(player_of(save)), int(town), small ? ShipListMode::SmallShips : ShipListMode::Sailors);
}

void traveller_refresh(const SaveGame& save, uint32_t town, TravellerWindow& w, const std::vector<uint16_t>& before,
	const std::vector<uint16_t>& now) {
	if (before.size() != now.size() && before.empty() && !now.empty()) {
		w.ship = now[0];
	}
	if (!now.empty() && std::find(now.begin(), now.end(), w.ship) == now.end()) {
		w.ship = now[0];
	}
	if (!w.done && save.globals.u32(global::kWorldTick) > town_memory(save, town).u32(0x838)) {
		w.expired = true;
		w.done = true;
	}
}

void ship_turn(uint16_t& ship, const std::vector<uint16_t>& ships, int step) {
	const size_t n = ships.size();
	if (n < 2) {
		return;
	}
	const size_t at = size_t(std::find(ships.begin(), ships.end(), ship) - ships.begin());
	if (step < 0) {
		ship = at > 0 && at <= n ? ships[at - 1] : ships[n - 1];
	} else {
		ship = at + 1 < n ? ships[at + 1] : ships[0];
	}
}

void traveller_turn(TravellerWindow& w, const std::vector<uint16_t>& ships, int step) {
	ship_turn(w.ship, ships, step);
}

std::optional<OperationBytes> traveller_press(const SaveGame& save, uint32_t town, TravellerWindow& w) {
	const TownMemory& t = town_memory(save, town);
	std::optional<OperationBytes> op;
	if (t.u32(0x838) - save.globals.u32(global::kWorldTick) > 0x700) {
		OperationBytes o{};
		put32(o, 0, 0x53);
		std::memcpy(o.data() + 4, &w.ship, 2);
		const uint16_t payment = t.u16(0x836);
		std::memcpy(o.data() + 6, &payment, 2);
		put32(o, 8, t.u32(0x838));
		o[0xC] = uint8_t(town);
		o[0xD] = t.u8(0x834);
		op = o;
	} else {
		w.late = true;
	}
	w.done = true;
	return op;
}

std::vector<std::pair<uint16_t, uint16_t>> fn_531470(const SaveGame& save, uint32_t town) {
	std::vector<std::pair<uint16_t, uint16_t>> list;
	const GlobalMemory& g = save.globals;
	if (town >= g.u32(global::kWorldTownCount)) {
		return list;
	}
	const uint16_t offices = g.u16(global::kWorldOfficeCount);
	const uint16_t merchants = g.u16(global::kWorldMerchantCount);
	for (uint32_t i = town_memory(save, town).u16(0x784); i < offices; i = save.offices[i].memory.u16(0x2CA)) {
		const OfficeMemory& o = save.offices[i].memory;
		const uint16_t owner = o.u16(0x2C4);
		if (owner < merchants && !(save.merchants[owner].memory.u8(0x8) & 4) && owner != player_of(save)) {
			list.push_back({owner, uint16_t(o.u16(0x2D4) + o.u16(0x2D2))});
		}
	}
	return list;
}

namespace {

// _stricmp on two upper-cased names: ASCII letters without case, the rest by its byte.
int name_order(const std::string& a, const std::string& b) {
	auto low = [](char c) {
		const unsigned char u = static_cast<unsigned char>(c);
		return u >= 'A' && u <= 'Z' ? u + 32 : u;
	};
	for (size_t k = 0;; ++k) {
		const int x = k < a.size() ? low(a[k]) : 0;
		const int y = k < b.size() ? low(b[k]) : 0;
		if (x != y || x == 0) {
			return x - y;
		}
	}
}

}  // namespace

std::vector<BurglarVictim> burglar_victims(const SaveGame& save, uint32_t town) {
	struct Named {
		BurglarVictim v;
		std::string family;
		std::string first;
	};
	std::vector<Named> named;
	for (const auto& [merchant, sum] : fn_531470(save, town)) {
		(void)sum;  // The second half of 0x531470's list: not used by the window.
		const MerchantMemory& m = save.merchants[merchant].memory;
		Named n;
		n.v.merchant = merchant;
		const int32_t worth = m.s32(0x46C);
		n.v.price = worth < 0 ? 500 : uint16_t(std::min<int64_t>((int64_t(worth / 10000) + 1) * 500, 65000));
		std::tie(n.first, n.family) = merchant_names(save, merchant);
		named.push_back(std::move(n));
	}
	std::stable_sort(named.begin(), named.end(), [](const Named& a, const Named& b) {
		const int by_family = name_order(a.family, b.family);
		return by_family != 0 ? by_family < 0 : name_order(a.first, b.first) < 0;
	});
	std::vector<BurglarVictim> victims;
	for (const Named& n : named) {
		victims.push_back(n.v);
	}
	return victims;
}

std::optional<OperationBytes> burglar_press(SaveGame& save, uint32_t town, BurglarWindow& w, size_t victim) {
	if (victim >= w.victims.size()) {
		return std::nullopt;
	}
	const BurglarVictim& v = w.victims[victim];
	std::optional<OperationBytes> op;
	w.hired = false;
	if (int32_t(v.price) <= save.merchants[player_of(save)].memory.s32(0)) {
		OperationBytes o{};
		put32(o, 0, 0x4F);
		put32(o, 4, v.price);
		const uint16_t here = uint16_t(town);
		const uint16_t player = uint16_t(player_of(save));
		std::memcpy(o.data() + 0xA, &here, 2);
		std::memcpy(o.data() + 0xC, &player, 2);
		std::memcpy(o.data() + 0xE, &v.merchant, 2);
		const uint16_t draw = uint16_t(tick::game_rand(save));
		std::memcpy(o.data() + 8, &draw, 2);
		op = o;
		w.hired = true;
	} else {
		w.refusal = int(tick::game_rand(save) * 5 / 32768);
	}
	w.done = true;
	return op;
}

uint32_t tavern_pirate_captain(const SaveGame& save, uint32_t town) {
	return tick::events_detail::fn_5261d0(save, town);
}

uint32_t pirate_henchmen(float roll) {
	return uint32_t(tick::ftol(double(roll) * 40.0 + 10.5));  // fmul 40.0f (0x66DCD8); fadd 10.5 (0x679CA0); __ftol
}

uint32_t pirate_share(const SaveGame& save, uint32_t captain) {
	if (captain >= save.captains.size()) {
		return 0;
	}
	return ((uint32_t(save.captains[captain].u8(0x8)) >> 5) + 6) * 5;
}

std::optional<OperationBytes> pirate_press(const SaveGame& save, uint32_t town, PirateWindow& w, uint32_t henchmen,
	bool before_town) {
	w.done = true;
	w.wrecked = false;
	if (w.ship >= save.ships.size()) {
		return std::nullopt;
	}
	const ShipMemory& s = save.ships[w.ship].memory;
	if (s.s32(0x14) == 0 || int32_t(uint32_t(s.s32(0x18)) * 100) / s.s32(0x14) <= 20) {
		w.wrecked = true;
		return std::nullopt;
	}
	OperationBytes op{};
	put32(op, 0, 0x4E);
	put32(op, 4, tick::events_detail::fn_5261d0(save, town));
	put32(op, 8, w.ship);
	put32(op, 0xC, henchmen);
	put32(op, 0x10, before_town ? town : 0xFF);
	return op;
}

}  // namespace p3::data
