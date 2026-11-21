#include "p3data/tavern_window.hpp"

#include <algorithm>
#include <cstdlib>
#include <tuple>
#include <cstring>

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/game_texts.hpp"
#include "p3data/mail.hpp"
#include "p3data/missions.hpp"
#include "p3data/navigation.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data {

namespace tick::ship_trade_detail {
uint16_t fn_5269a0(const SaveGame& save, int town, uint16_t owner);
}  // namespace tick::ship_trade_detail
namespace tick::events_detail {
uint32_t fn_5261d0(const SaveGame& save, uint32_t town);
}  // namespace tick::events_detail
namespace tick::alderman_detail {
uint16_t fn_4ec694(const SaveGame& save, uint32_t merchant);
}  // namespace tick::alderman_detail

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

OrderClient tavern_order(const SaveGame& save, uint32_t town) {
	OrderClient o;
	const TownMemory& t = town_memory(save, town);
	const uint16_t events = save.globals.u16(kEventCount);
	for (size_t k = 0; k < 2; ++k) {
		const uint16_t i = t.u16(0x830 + 2 * k);
		if (i < events && i < save.events.size() && save.events[i].u8(0x16) == player_of(save)) {
			const GameMemory<0x18>& e = save.events[i];
			o.event = i;
			o.town = e.u8(0x8);
			o.ware = e.u8(0x9);
			o.first = e.u8(0xA);
			o.family = e.u8(0xB);
			o.deadline = e.u32(0xC);
			o.left = e.u16(0x10);
			o.total = e.u16(0x12);
			o.price = e.u16(0x14);
			o.at_end = e.u8(0x17) != 0;
			break;
		}
	}
	return o;
}

int32_t order_client_stock(const SaveGame& save, uint32_t town, uint8_t ware) {
	const uint32_t player = player_of(save);
	if (ware >= 24 || player >= save.merchants.size()) {
		return 0;
	}
	int32_t sum = 0;
	const int office = tick::facilities_detail::fn_5308a0(save, player, town);
	if (office >= 0) {
		sum = save.offices[size_t(office)].memory.s32(0x4 + 4 * ware);
	}
	const uint32_t ships = save.globals.u16(global::kShipCount);
	size_t walked = 0;
	for (uint32_t s = save.merchants[player].memory.u16(0xE); s < ships && s < save.ships.size() && walked < save.ships.size();
		 s = save.ships[s].memory.u16(0x4), ++walked) {
		const ShipMemory& ship = save.ships[s].memory;
		if (ship.u8(0x39) == town && !(ship.u8(0x136) & 1)) {
			sum = int32_t(uint32_t(sum) + uint32_t(ship.s32(0x54 + 4 * ware)));
		}
	}
	static constexpr uint8_t kInBarrels[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};
	return sum / (kInBarrels[ware] ? 200 : 2000);
}

OperationBytes order_delivery_operation(uint32_t player, uint16_t event, uint32_t amount) {
	OperationBytes op{};
	const uint32_t fields[5] = {0x51, player, 0, event, amount};
	std::memcpy(op.data(), fields, sizeof fields);
	return op;
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

namespace {

const GameMemory<0x10>* side_room_record(const SaveGame& save, uint16_t entry) {
	if (entry >= save.mail.size() || entry >= save.mail_records.size() || !save.mail_records[entry]) {
		return nullptr;
	}
	return &*save.mail_records[entry];
}

}  // namespace

bool side_room_open(const SaveGame& save, uint16_t entry) {
	const GameMemory<0x10>* record = side_room_record(save, entry);
	if (!record) {
		return false;
	}
	const uint16_t event = record->u16(8);
	if (event >= save.events.size() || save.events[event].u16(6) != 0x1B) {
		return false;
	}
	const auto script = save.missions.scripts.find(event);
	if (script == save.missions.scripts.end() || record->u8(0xC) >= script->second.variables.size()) {
		return false;
	}
	const uint32_t v = script->second.variables[record->u8(0xC)];
	return v == uint32_t(player_of(save)) || v >= save.globals.u16(global::kWorldMerchantCount);
}

namespace {

// The open letters of the town from `start` along the chain (0x4D7900 from each next): the first, or 0xFFFF.
uint16_t first_open(const SaveGame& save, uint32_t town, uint16_t start) {
	uint16_t at = find_tavern_letter(save, start, uint16_t(town));
	for (size_t steps = 0; at < save.globals.u16(kMailCount) && at < save.mail.size() && steps <= save.mail.size();
		 ++steps) {
		if (side_room_open(save, at)) {
			return at;
		}
		at = find_tavern_letter(save, save.mail[at].u16(6), uint16_t(town));
	}
	return 0xFFFF;
}

uint16_t chain_head(const SaveGame& save) {
	const int player = player_of(save);
	return player >= 0 && size_t(player) < save.merchants.size() ? save.merchants[size_t(player)].memory.u16(0xA)
																 : uint16_t(0xFFFF);
}

}  // namespace

uint16_t side_room_letter(const SaveGame& save, uint32_t town, uint16_t kept) {
	const uint16_t count = save.globals.u16(kMailCount);
	if (kept < count && find_tavern_letter(save, kept, uint16_t(town)) == kept && side_room_open(save, kept)) {
		return kept;
	}
	return first_open(save, town, chain_head(save));
}

uint16_t side_room_next(const SaveGame& save, uint32_t town, uint16_t kept) {
	const uint16_t count = save.globals.u16(kMailCount);
	if (kept < count && kept < save.mail.size()) {
		const uint16_t after = first_open(save, town, save.mail[kept].u16(6));
		if (after < count) {
			return after;
		}
	}
	const uint16_t first = first_open(save, town, chain_head(save));
	return first == kept ? uint16_t(0xFFFF) : first;
}

SideRoomWords side_room_words(const SaveGame& save, uint16_t entry) {
	SideRoomWords w;
	const GameMemory<0x10>* record = side_room_record(save, entry);
	if (!record || entry >= save.mail_buffers.size()) {
		return w;
	}
	const std::vector<uint8_t>& b = save.mail_buffers[entry];
	size_t at = 0;
	auto next = [&]() {
		std::string out;
		while (at < b.size() && b[at] != 0) {
			out += char(b[at++]);
		}
		++at;
		return out;
	};
	w.title = next();
	const std::string text = next();
	const size_t bar = text.find('|');
	w.text = text.substr(0, bar);
	w.after = bar == std::string::npos ? std::string() : text.substr(bar + 1);
	if (record->u8(0xB) != 0) {
		w.accept = next();
		w.has_accept = true;
	}
	return w;
}

OperationBytes side_room_talk_operation(const SaveGame& save, uint16_t entry, uint32_t merchant, uint32_t town) {
	OperationBytes op{};
	const GameMemory<0x10>* record = side_room_record(save, entry);
	put32(op, 0, 0x52);
	put32(op, 4, record ? uint32_t(record->u16(8)) | uint32_t(record->u8(0xC)) << 16 : 0);
	put32(op, 8, merchant);
	put32(op, 0xC, town);
	put32(op, 0x10, 9);
	return op;
}

OperationBytes side_room_accept_operation(const SaveGame& save, uint16_t entry) {
	OperationBytes op{};
	const GameMemory<0x10>* record = side_room_record(save, entry);
	put32(op, 0, 0x76);
	put32(op, 4, record ? record->u16(8) : 0);
	put32(op, 8, record ? uint32_t(record->u8(0xC)) << 8 | record->u8(0xA) : 0);
	put32(op, 0xC, 0);
	put32(op, 0x10, uint32_t(player_of(save)));
	return op;
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
		break;  // 9, the side room's letter: side_room_talk_operation.
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

int32_t InformerNews::price() const {
	int32_t v;
	std::memcpy(&v, op.data() + 0xC, 4);
	return v;
}

namespace {

constexpr uint32_t kTownTable = 0x6DDB90;  // StadtDaten.ini: 0x34 bytes per town number ([+0x20], [+0x24] its place)
constexpr uint32_t kTownIds = 0x6DE4B8;    // u8 per town index: its number
constexpr uint32_t kHistory = 0x6DE588;    // the towns' history (0x535300, 0x5353C0)
constexpr uint8_t kSingular[20] = {1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0};  // 0x66E0D4
constexpr uint8_t kInformerTable[0x14][4] = {{4, 3, 12, 10}, {0, 1, 11, 11}, {0, 2, 18, 12}, {0, 3, 0, 13}, {0, 4, 7, 14},
	{1, 0, 20, 15}, {1, 2, 13, 16}, {1, 3, 4, 17}, {1, 4, 23, 18}, {2, 0, 12, 19}, {2, 1, 30, 20}, {2, 3, 1, 21},
	{2, 4, 2, 22}, {3, 0, 19, 23}, {3, 1, 12, 24}, {3, 2, 29, 25}, {3, 4, 25, 26}, {4, 0, 5, 27}, {4, 1, 27, 28},
	{4, 2, 31, 29}};  // 0x673730 (its 0x50 bytes)

// A town's name by its index in the table of names by number 0x6DDA00 (the game's index-for-number: the letters'
// game bug; the right name with fix_game_bugs).
std::string informer_town(const SaveGame& save, uint32_t town) {
	const bool fixed = fix_game_bugs();
	if (fixed && town >= save.globals.u32(global::kWorldTownCount)) {
		return std::string();
	}
	const uint32_t named = fixed ? save.globals.u8(kTownIds + town) : town;
	const std::vector<std::string>& table = fixed ? save.opaque.town_names : save.opaque.town_name_table;
	return named < table.size() ? table[named] : std::string();
}

uint8_t town_region(const SaveGame& save, uint32_t town) {
	return save.globals.u8(kTownTable + 0x34 * save.towns[town].memory.u8(0x2C1) + 0x30);
}

// 0x5353C0 (on the history, a town): how many days late the local player knows the town (his slot's table; 15
// without one).
uint32_t fn_5353c0(const SaveGame& save, uint32_t town) {
	const GlobalMemory& g = save.globals;
	for (uint32_t slot = 0; slot < 8; ++slot) {
		if (g.u8(kHistory + 0xA00 + slot) == g.u32(global::kPlayer)) {
			const std::vector<uint8_t>& lag = save.player_towns[slot];
			if (lag.empty()) {
				return 15;
			}
			return town < lag.size() ? lag[town] : 0;
		}
	}
	return 15;
}

// 0x515AE0 (the pirate k): his captain's names (the first ship of his chain [+0xC], next [+0x4], with a busy captain,
// 0x4FE150), or 0x2D and the families' count's low byte.
void pirate_names(const SaveGame& save, uint32_t k, uint8_t& first, uint8_t& family) {
	const GlobalMemory& g = save.globals;
	const uint32_t ships = g.u16(global::kShipCount);
	for (uint32_t s = save.pirates[k] ? save.pirates[k]->u16(0xC) : 0xFFFF; s < ships; s = save.ships[s].memory.u16(4)) {
		const uint16_t c = save.ships[s].memory.u16(0x42);
		if (c < g.u16(global::kCaptainCount) && save.captains[c].u8(8) > 0x20) {
			first = save.captains[c].u8(2);
			family = save.captains[c].u8(3);
			return;
		}
	}
	first = 0x2D;
	family = g.u8(0x6DDB74);
}

// 0x4D75D0 (on the mailbox: merchant, &letter, &piece): the merchant's first treasure map (the type 0x45, or 0x46 with
// [+0xA] below 0x1FF) and a piece (1 << k) of its nine not found ([+0xA]'s bits clear): the only one, or by rand().
bool fn_4d75d0(SaveGame& save, uint32_t merchant, uint16_t& letter, uint16_t& piece) {
	const GlobalMemory& g = save.globals;
	letter = 0xFFFF;
	piece = 0;
	if (merchant >= g.u16(global::kWorldMerchantCount)) {
		return false;
	}
	const uint32_t count = g.u16(kMailCount);
	int32_t left = int32_t(count);
	uint32_t i = save.merchants[merchant].memory.u16(0xA);
	bool found = false;
	while (int32_t(i) < int32_t(count) && left-- > 0) {
		const GameMemory<0x10>& e = save.mail[i];
		if (e.u8(4) == 0x45 || (e.u8(4) == 0x46 && e.u16(0xA) < 0x1FF)) {
			letter = uint16_t(i);
			found = true;
			break;
		}
		i = e.u16(6);
	}
	if (!found || letter >= count || left < 0 || save.mail[letter].u16(0xA) >= 0x1FF) {
		letter = 0xFFFF;
		return false;
	}
	const uint16_t bits = save.mail[letter].u16(0xA);
	uint16_t clear[9];
	int n = 0;
	for (int k = 0; k < 9; ++k) {
		if (!(bits & (1 << k))) {
			clear[n++] = uint16_t(1 << k);
		}
	}
	piece = n == 1 ? clear[0] : clear[(tick::game_rand(save) & 0xFF) * uint32_t(n) >> 8];
	return true;
}

void put_news(std::string& text, bool& first, const std::string& piece) {
	if (first) {
		first = false;
	} else {
		text += '\n';
	}
	text += piece;
}

}  // namespace

uint32_t fn_448610(const SaveGame& save, const NavData& nav, uint32_t ship) {
	const GlobalMemory& g = save.globals;
	const ShipMemory& s = save.ships[ship].memory;
	uint32_t best = 0xFFFFFFFF;
	uint32_t found = 0;
	for (uint32_t t = 0; t < g.u32(global::kWorldTownCount) && t < 0x100; ++t) {
		const uint32_t entry = kTownTable + 0x34 * g.u8(kTownIds + t);
		const MapPoint points[2] = {{int16_t(s.u16(0x1E)), int16_t(s.u16(0x22))},
			{int16_t(g.u16(entry + 0x20)), int16_t(g.u16(entry + 0x24))}};
		std::vector<MapPoint> route;
		MapPoint failed;
		nav.route_through(points, 2, route, failed);
		if (route.empty()) {
			continue;  // No route at all: the town isn't looked at.
		}
		uint32_t length = 0;
		for (size_t k = 1; k < route.size(); ++k) {
			const int32_t dy = std::abs(int32_t(route[k - 1].y) - int32_t(route[k].y));
			length += uint32_t(dy) * 29;
		}
		if (length < best) {
			best = length;
			found = t;
		}
	}
	return found & 0xFF;
}

void fn_526b40(SaveGame& save, const NavData* nav, const GameTexts& texts, uint32_t town, InformerNews& news) {
	GlobalMemory& g = save.globals;
	const TownMemory& t = town_memory(save, town);
	const uint32_t player = g.u32(global::kPlayer);
	const uint32_t towns = g.u32(global::kWorldTownCount);
	auto text = [&](uint32_t english) -> const std::string& { return texts.raw_at(english); };
	auto fmt = [&](uint32_t english, const std::vector<std::string>& args) { return GameTexts::format(text(english), args); };
	// The Spanish build's code (0x526FF0) gives some of them their arguments in another order (its sentences need it)
	// and says the plentiful ware in the singular or the plural (0x66E0D4).
	const bool spanish = texts.spanish();
	const MerchantMemory& m = save.merchants[player].memory;
	const uint32_t rank = m.u8(0x39C + m.u8(0x19));
	if (((g.u32(global::kWorldTick) >> 8) + t.u8(0x2C0)) % 3 == 0) {
		uint16_t letter = 0;
		uint16_t piece = 0;
		fn_4d75d0(save, player, letter, piece);
		if (letter < g.u16(kMailCount)) {
			put32(news.op, 0, 0x55);
			put32(news.op, 4, player);
			put32(news.op, 8, letter);
			const uint32_t draw = tick::game_rand(save);
			put32(news.op, 0xC, (draw % 11 + 30 * (rank + 1)) * 10);
			put32(news.op, 0x10, (uint32_t(piece) << 5) + t.u8(0x2C0));
			news.text = "\"" + text(0x6C29F8) + "\"";
			return;
		}
	}
	put32(news.op, 0, 0x4C);
	put32(news.op, 4, player);
	put32(news.op, 8, 0xFFFF);
	put32(news.op, 0xC, (rank + 1) * 100);
	put32(news.op, 0x10, 0xFFFF);
	news.first_name = 0;
	news.family_name = 0;
	news.text.clear();
	uint32_t region = 0;
	const uint8_t idx = t.u8(0x82C);
	if (idx < 0x14) {
		news.first_name = kInformerTable[idx][2];
		news.family_name = kInformerTable[idx][3];
		region = kInformerTable[idx][1];
	}
	auto add_price = [&](uint32_t more) {
		uint32_t v;
		std::memcpy(&v, news.op.data() + 0xC, 4);
		put32(news.op, 0xC, v + more);
	};
	bool first = true;
	const bool fixed = fix_game_bugs();
	// The towns' changes the player hasn't heard of.
	for (uint32_t i = 0; i < towns; ++i) {
		if (town_region(save, i) != region || fn_5353c0(save, i) <= 3) {
			continue;
		}
		const uint32_t now = save.towns[i].memory.u32(0x2C8);
		const uint32_t known = town_situation_known(save, i);
		const std::string name = informer_town(save, i);
		std::string piece;
		if ((now ^ known) & 0x10) {
			piece = fmt(now & 0x10 ? 0x6C25C8 : 0x6C25CC, {name});
		} else if ((now ^ known) & 0x8) {
			piece = fmt(now & 0x8 ? (fixed ? 0x6C2624 : 0x6C25C8) : (fixed ? 0x6C2628 : 0x6C25CC), {name});
		} else if ((now ^ known) & 0x8000) {
			piece = fmt(now & 0x8000 ? 0x6C26C0 : 0x6C26C4, {name});
		} else if ((now ^ known) & 0x1000) {
			piece = fmt(now & 0x1000 ? (fixed ? 0x6C2670 : 0x6C25C8) : (fixed ? 0x6C2674 : 0x6C25CC), {name});
		} else if ((now ^ known) & 0x7C0000) {
			const uint32_t ware = (now >> 18) & 0x1F;
			if (ware >= 0x14) {
				continue;
			}
			piece = fmt(kSingular[ware] ? 0x6C276C : 0x6C2770, {name, text(0x6C0BB0 + 4 * ware)});
		} else if (now & 0x2000) {
			piece = fmt(0x6C272C, {name});
		} else if (now & 0x4000) {
			piece = fmt(0x6C2730, {name});
		} else {
			continue;
		}
		put_news(news.text, first, piece);
		add_price(10);
	}
	if (news.text.empty()) {
		put32(news.op, 8, t.u8(0x2C0));
	}
	// The scarcest ware of the region.
	int32_t least = 0x100;
	uint32_t least_ware = 0xFF;
	uint32_t least_town = 0xFF;
	for (uint32_t i = towns; i-- > 0;) {
		if (town_region(save, i) != region) {
			continue;
		}
		const TownMemory& u = save.towns[i].memory;
		for (uint32_t w = 20; w-- > 0;) {
			const int32_t stock = u.s32(4 + 4 * w);
			const int32_t wanted = u.s32(0x4F0 + 0x10 * w);  // (Not the trader's [+0x4F4]: the field before it.)
			if (stock >= wanted) {
				continue;
			}
			int32_t ratio;
			if (stock > 0) {
				const int32_t scale = wanted >> 7;
				if (scale == 0) {
					continue;  // The game would divide by 0.
				}
				ratio = int32_t(uint32_t(stock) * 4) / scale;
			} else {
				ratio = -wanted;
			}
			if (ratio < least) {
				least = ratio;
				least_ware = w;
				least_town = i;
			}
		}
	}
	if (least_town < towns) {
		const std::string ware = text(0x6C0BB0 + 4 * least_ware);
		const std::string where = informer_town(save, least_town);
		put_news(news.text, first, spanish ? fmt(0x6C27E0, {where, ware}) : fmt(0x6C27E0, {ware, where}));
		add_price(10);
	}
	// The most plentiful.
	int32_t most = 0;
	uint32_t most_ware = 0xFF;
	uint32_t most_town = 0xFF;
	for (uint32_t i = towns; i-- > 0;) {
		const TownMemory& u = save.towns[i].memory;
		if (town_region(save, i) != region || !(u.u32(0x76C) & 0x100)) {
			continue;
		}
		for (uint32_t w = 0; w < 0x18; ++w) {
			if (!((0x4472Eu >> w) & 1)) {
				continue;
			}
			const int32_t low = u.s32(0x4F4 + 0x10 * w);
			const int32_t stock = u.s32(4 + 4 * w);
			if (stock <= int32_t(uint32_t(low) + 20000)) {
				continue;
			}
			const int32_t high = u.s32(0x4F8 + 0x10 * w);
			int32_t ratio;
			if (stock > high) {
				const int32_t span = int32_t(uint32_t(high) - uint32_t(low)) >> 7;
				ratio = span > 1 ? int32_t(uint32_t(stock) * 2) / span + 0x100 : int32_t(uint32_t(stock) + 0x100);
			} else {
				const int32_t scale = low >> 7;
				if (scale == 0) {
					continue;  // The game would divide by 0.
				}
				ratio = int32_t(uint32_t(stock) * 2) / scale;
			}
			if (ratio > most) {
				most = ratio;
				most_ware = w;
				most_town = i;
			}
		}
	}
	if (most_town < towns) {
		const std::string ware = text(0x6C0BB0 + 4 * most_ware);
		const std::string where = informer_town(save, most_town);
		if (spanish) {
			const std::string& plural = texts.raw_at(0x40000000 | 0x6C2F80);
			put_news(news.text, first,
				GameTexts::format(kSingular[most_ware] || plural.empty() ? text(0x6C28A8) : plural, {where, ware}));
		} else {
			put_news(news.text, first, fmt(0x6C28A8, {ware, where}));
		}
		add_price(10);
	}
	// The pirates seen.
	uint8_t pirate = 0;
	uint32_t chain = 0xFF00;  // The game's [esp+0x54] with its low byte cleared: the window's address (above the ships).
	uint8_t first_name = 0;
	uint8_t family_name = 0;
	uint32_t largest = 0;
	for (uint8_t k = 0; k < 5; ++k) {
		if (save.pirates[k] && save.pirates[k]->u16(0x16) > largest) {
			largest = save.pirates[k]->u16(0x16);
			pirate = k;
			pirate_names(save, k, first_name, family_name);
			chain = save.pirates[k]->u16(0xC);
		}
	}
	const uint16_t task = tick::alderman_detail::fn_4ec694(save, player);
	if (task != 0xFFFF && task < save.events.size()) {
		const GameMemory<0x18>& e = save.events[task];  // 0x4D8DB0's record: [+0xC] of it is the event's [+0xC]
		if (e.u8(0xC) == 2 && e.u8(0x16) == 0) {
			first_name = e.u8(0x12);
			chain = e.u16(0x10);
			family_name = e.u8(0x14);
		}
	}
	const uint32_t ships = g.u16(global::kShipCount);
	if (save.pirates[pirate] && nav) {
		for (uint32_t i = chain & 0xFFFF; i < ships; i = save.ships[i].memory.u16(4)) {
			const ShipMemory& s = save.ships[i].memory;
			const uint16_t c = s.u16(0x42);
			if (c >= g.u16(global::kCaptainCount) || save.captains[c].u8(2) != first_name ||
				save.captains[c].u8(3) != family_name) {
				continue;
			}
			const uint32_t near = fn_448610(save, *nav, i);
			const std::string his_first = game_first_name(save, first_name);
			const std::string his_family = game_family_name(save, family_name);
			const std::string where = informer_town(save, near);
			std::string piece;
			switch ((c + near) % 3) {
			case 0:
				piece = fmt(0x6B1460, {where, his_first, his_family});
				break;
			case 1:
				piece = fmt(0x6B1464, {his_first, his_family, where});
				break;
			default:
				piece = fmt(0x6B1468, {his_first, his_family, where});
				break;
			}
			put_news(news.text, first, piece);
			add_price(10);
		}
	}
	if (!news.text.empty()) {
		news.text = "\"" + news.text + "\"";
		return;
	}
	// Nothing to tell: a town of the region.
	uint32_t chosen = 0xFF;
	{
		int32_t n = int32_t(tick::game_rand(save) & 3) + 1;
		uint32_t i = 0;
		bool round = false;
		bool fallback = false;
		while (n > 0) {
			if (town_region(save, i) == region) {
				chosen = i;
				--n;
			}
			if (++i >= towns) {
				if (round) {
					fallback = true;
					break;
				}
				i = 0;
				round = true;
			}
		}
		if (fallback) {
			chosen = t.u8(0x82C) % towns;
		}
	}
	const TownMemory& c = save.towns[chosen].memory;
	const std::string where = informer_town(save, chosen);
	switch (t.u32(0x54) & 3) {
	case 0:
		news.text = fmt(0x6C29B8, {std::to_string(c.s32(0x2D4)), where, std::to_string(c.s32(0x2E4))});
		break;
	case 1:
		if (c.u8(0x6F1) < g.u16(global::kWorldMerchantCount)) {
			const auto [mayor_first, mayor_family] = merchant_names(save, c.u8(0x6F1));
			news.text = fmt(0x6C29BC, {mayor_first, mayor_family, where});
		} else {
			news.text = fmt(0x6C29BC, {game_first_name(save, c.u8(0x6D4)), game_family_name(save, c.u8(0x6D8)), where});
		}
		break;
	case 2:
		news.text = spanish ? fmt(0x6C29C0, {where, std::to_string(c.s32(0x31C))})
							: fmt(0x6C29C0, {std::to_string(c.s32(0x31C)), where});
		break;
	default:
		news.text = fmt(0x6C29C4, {std::to_string(c.u16(0x2C6)), where});
		break;
	}
	news.text = "\"" + news.text + "\"";
	put32(news.op, 0xC, (((t.u32(0x54) + chosen + region) & 3) + 7) * 10);
	put32(news.op, 8, t.u8(0x2C0));
}

}  // namespace p3::data
