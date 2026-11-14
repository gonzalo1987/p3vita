#include "p3data/guild_window.hpp"

#include <algorithm>
#include <cstring>

#include "p3data/game_memory.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace tick::events_detail {
int32_t fn_51b7e0(const SaveGame& save, int town, int merchant);  // events.cpp
int32_t fn_4daf10(const SaveGame& save, uint32_t town, uint32_t record, bool flag);  // events.cpp
int16_t fn_4f9580(const SaveGame& save, int merchant, uint32_t town);  // events.cpp
}  // namespace tick::events_detail
namespace tick::events_mayor_detail {
int32_t fn_4da860(const SaveGame& save, uint32_t ship);  // events_mayor.cpp
}  // namespace tick::events_mayor_detail

namespace {

constexpr uint32_t kEventHead = 0x6DD744;  // u16 ([+0x8] of the queue 0x6DD73C)
constexpr uint32_t kEventCount = 0x6DD748;  // u16 ([+0xC])

using Event = GameMemory<0x18>;

OperationBytes words(std::initializer_list<uint32_t> fields) {
	OperationBytes op{};
	size_t at = 0;
	for (uint32_t f : fields) {
		std::memcpy(op.data() + at, &f, 4);
		at += 4;
	}
	return op;
}

bool is_auction(const Event& e) {
	return e.u16(6) == 0xA || e.u16(6) == 0x36;
}

const Event* auction_at(const SaveGame& save, uint32_t event) {
	if (event >= save.globals.u16(kEventCount) || event >= save.events.size() || !is_auction(save.events[event])) {
		return nullptr;
	}
	return &save.events[event];
}

uint32_t merchant_count(const SaveGame& save) {
	return save.globals.u16(global::kWorldMerchantCount);
}

}  // namespace

bool guild_member(const SaveGame& save, uint32_t town, uint32_t merchant) {
	if (merchant >= merchant_count(save) || merchant >= save.merchants.size()) {
		return false;
	}
	return (save.merchants[merchant].memory.u32(0x468) >> (town & 31)) & 1;
}

int32_t guild_join_price(const SaveGame& save, uint32_t town, uint32_t merchant) {
	if (merchant >= merchant_count(save) || town >= save.towns.size()) {
		return 10000;
	}
	return tick::events_detail::fn_51b7e0(save, int(town), int(merchant));
}

bool can_join_guild(const SaveGame& save, uint32_t town, uint32_t merchant) {
	return merchant < save.merchants.size() &&
		save.merchants[merchant].memory.s32(0x0) >= guild_join_price(save, town, merchant);
}

OperationBytes join_guild_operation(uint32_t merchant, uint32_t town, int32_t price) {
	return words({0x37, merchant, town, uint32_t(price)});
}

std::vector<AuctionEntry> upcoming_auctions(const SaveGame& save) {
	std::vector<AuctionEntry> list;
	const GlobalMemory& g = save.globals;
	const uint32_t count = g.u16(kEventCount);
	const uint32_t towns = g.u32(global::kWorldTownCount);
	const uint16_t ships = g.u16(global::kShipCount);
	for (uint32_t i = g.u16(kEventHead); i < count && i < save.events.size() && list.size() < 3;
		i = save.events[i].u16(4)) {
		const Event& e = save.events[i];
		if (!is_auction(e)) {
			continue;
		}
		const uint8_t town = e.u8(0x15);
		if (town >= towns || town >= save.towns.size() || !(save.towns[town].memory.u8(0x2C8) & 0xC0)) {
			continue;
		}
		const uint8_t phase = e.u8(0x14);
		if (phase != 3 && phase != 2 && !(phase == 1 && save.settings.u8(0xD) == 0)) {
			continue;
		}
		AuctionEntry a;
		a.tick = e.u32(0);
		if (phase == 1) {
			a.tick += e.u16(0xE);
		}
		if (phase <= 2) {
			a.tick += 0x100;
		}
		a.town = town;
		a.event = i;
		const uint16_t records = save.towns[town].memory.u16(0x776);
		for (size_t k = 0; k < 3; ++k) {
			const uint16_t slot = e.u16(8 + 2 * k);
			a.lots[k] = 0xFFFF;
			if (e.u16(6) == 0xA) {
				if (slot < ships) {
					++a.types[save.ships[slot].memory.u8(0xE) & 3];
					a.lots[k] = slot;
				}
			} else if ((slot & 0x7FFF) < records) {
				a.types[0] = 0xFF;
				a.lots[k] = slot;
			}
		}
		list.push_back(a);
	}
	return list;
}

bool told_of_auction(const SaveGame& save, uint32_t event) {
	const Event* e = auction_at(save, event);
	if (!e) {
		return false;
	}
	const uint32_t player = save.globals.u32(global::kPlayer);
	uint32_t bit = 1;
	for (uint32_t m = 0; m < merchant_count(save) && m < save.merchants.size(); ++m) {
		if (m == player) {
			return (bit & e->u8(0x16)) == bit;
		}
		if (save.merchants[m].memory.u16(0x8) == 0) {
			bit += bit;
		}
	}
	return false;
}

OperationBytes tell_operation(const SaveGame& save, uint32_t player, uint32_t event) {
	return words({0x34, player, event, told_of_auction(save, event) ? 0u : 1u});
}

OperationBytes auction_ship_operation(uint32_t ship) {
	return words({0x36, ship});
}

int town_auction(const SaveGame& save, uint32_t town) {
	if (town >= save.globals.u32(global::kWorldTownCount) || town >= save.towns.size()) {
		return -1;
	}
	const uint16_t event = save.towns[town].memory.u16(0x9A2);
	const Event* e = auction_at(save, event);
	return e && e->u8(0x14) <= 7 ? int(event) : -1;
}

std::vector<uint16_t> auction_lots(const SaveGame& save, uint32_t event) {
	std::vector<uint16_t> lots;
	const Event* e = auction_at(save, event);
	if (!e) {
		return lots;
	}
	auto exists = [&](uint16_t slot) {
		if (e->u16(6) == 0xA) {
			return slot < save.globals.u16(global::kShipCount);
		}
		const uint8_t town = e->u8(0x15);
		return town < save.towns.size() && (slot & 0x7FFF) < save.towns[town].memory.u16(0x776);
	};
	for (size_t k = 0; k < 3 && exists(e->u16(8 + 2 * k)); ++k) {
		lots.push_back(e->u16(8 + 2 * k));
	}
	return lots;
}

int32_t lot_minimum_bid(const SaveGame& save, uint32_t event, uint16_t lot) {
	const Event* e = auction_at(save, event);
	if (!e) {
		return 0;
	}
	if (e->u16(6) == 0xA) {
		return lot < save.globals.u16(global::kShipCount) ? tick::events_mayor_detail::fn_4da860(save, lot) : 0;
	}
	const uint8_t town = e->u8(0x15);
	if (town >= save.towns.size() || (lot & 0x7FFF) >= save.towns[town].memory.u16(0x776)) {
		return 0;
	}
	return tick::events_detail::fn_4daf10(save, town, lot & 0x7FFF, (lot >> 15) != 0);
}

int ship_condition(const SaveGame& save, uint16_t ship) {
	if (ship >= save.ships.size()) {
		return 0;
	}
	const ShipMemory& s = save.ships[ship].memory;
	const int32_t most = s.s32(0x14);
	const int32_t percent = most != 0 ? s.s32(0x18) * 100 / most : 100;
	return percent < 40 ? 4 : percent < 60 ? 3 : percent < 80 ? 2 : percent < 90 ? 1 : 0;
}

bool bid_shown(const SaveGame& save, uint32_t event, uint32_t player) {
	const Event* e = auction_at(save, event);
	if (!e || e->u8(0x14) < 4 || e->u8(0x14) == 8) {
		return false;
	}
	return e->u16(6) == 0xA || tick::facilities_detail::fn_5308a0(save, player, e->u8(0x15)) >= 0;
}

bool can_bid(const SaveGame& save, uint32_t event, uint32_t player) {
	const Event* e = auction_at(save, event);
	if (!e || player >= save.merchants.size()) {
		return false;
	}
	const int32_t bid = int32_t(e->u16(0xE)) + int32_t(e->u16(0x10));
	return bid <= save.merchants[player].memory.s32(0x0) && e->u8(0x17) != player;
}

OperationBytes bid_operation(const SaveGame& save, uint32_t event, uint32_t player) {
	const Event* e = auction_at(save, event);
	const uint32_t bid = e ? uint32_t(e->u16(0xE)) + e->u16(0x10) : 0;
	return words({0x33, player, event, bid});
}

OperationBytes automatic_bid_operation(uint32_t top, uint32_t event, uint32_t lot, uint32_t player) {
	return words({0x35, top, event, lot, player});
}

uint32_t automatic_bid(const SaveGame& save, uint32_t event, uint32_t lot, uint32_t player) {
	const Event* e = auction_at(save, event);
	uint32_t top = 0;
	if (e) {
		tick::events_detail::fn_4d9000(save, player, e->u8(0x15), lot, top);
	}
	return top;
}

bool automatic_bid_shown(const SaveGame& save, uint32_t event, uint32_t player) {
	const Event* e = auction_at(save, event);
	return e && (e->u16(6) == 0xA || tick::facilities_detail::fn_5308a0(save, player, e->u8(0x15)) >= 0);
}

ShipAuction ship_auction(const SaveGame& save, uint16_t ship) {
	ShipAuction a;
	a.minimum = tick::events_mayor_detail::fn_4da860(save, ship);
	const uint32_t count = save.globals.u16(kEventCount);
	for (uint32_t i = save.globals.u16(kEventHead); i < count && i < save.events.size(); i = save.events[i].u16(4)) {
		const Event& e = save.events[i];
		if (e.u16(6) != 0xA || (e.u16(8) != ship && e.u16(0xA) != ship && e.u16(0xC) != ship)) {
			continue;
		}
		a.tick = e.s32(0);
		if (e.u8(0x14) <= 2) {
			a.tick += 0x100;
			if (e.u8(0x14) == 1) {
				a.tick += e.u16(0xE);
			}
		}
		break;
	}
	return a;
}

std::vector<uint16_t> guild_convoys(const SaveGame& save) {
	const GlobalMemory& g = save.globals;
	const uint32_t count = g.u16(global::kShips + 0xF6);
	const uint32_t merchants = g.u16(global::kWorldMerchantCount);
	std::vector<uint16_t> list;
	for (uint32_t c = g.u16(global::kShips + 0xEC); c < count && c < save.convoys.size(); c = save.convoys[c].u16(0x8)) {
		const GameMemory<0x3C>& m = save.convoys[c];
		if (m.u8(0x0) < merchants && (m.u8(0x14) & 0xA) == 0 && (m.u16(0x12) < 4 || m.u16(0x12) == 0xF)) {
			list.push_back(uint16_t(c));
		}
	}
	std::vector<uint16_t> out(list.size());
	for (size_t k = 0; k < list.size(); ++k) {
		const GameMemory<0x3C>& c = save.convoys[list[k]];
		if (c.u16(0x12) >= 2) {
			out[k] = list[k];
			continue;
		}
		size_t at = 0;
		while (at < k) {
			const GameMemory<0x3C>& d = save.convoys[out[at]];
			if (d.u16(0x12) < 2 || c.u16(0x16) < d.u16(0x16)) {
				break;
			}
			++at;
		}
		for (size_t j = k; j > at; --j) {
			out[j] = out[j - 1];
		}
		out[at] = list[k];
	}
	return out;
}

GuildConvoyLine guild_convoy_line(const SaveGame& save, uint16_t convoy) {
	GuildConvoyLine l;
	if (convoy >= save.convoys.size()) {
		return l;
	}
	const GameMemory<0x3C>& c = save.convoys[convoy];
	const uint32_t ships = save.globals.u16(global::kShipCount);
	l.guide = c.u16(0x10);
	l.from = c.u8(0x1E);
	l.to = c.u8(0x1F);
	const uint8_t owner = l.guide < save.ships.size() ? save.ships[l.guide].memory.u8(0x0) : 0xFF;
	int hulls = 0;
	for (uint32_t s = c.u16(0xA); s < ships && s < save.ships.size(); s = save.ships[s].memory.u16(0x6)) {
		const ShipMemory& m = save.ships[s].memory;
		if (m.u8(0x0) != owner) {
			continue;
		}
		hulls += m.s32(0x14) != 0 ? m.s32(0x18) * 100 / m.s32(0x14) : 0;
		l.crew += m.u16(0x40);
		l.firepower += m.s32(0x120);
		++l.ships;
	}
	l.hull = l.ships > 0 ? hulls / l.ships : 0;  // (The game divides by a count of 0 too: a guide of its owner's.)
	l.waiting = c.u16(0x12) < 2;
	l.departure = save.globals.u32(global::kWorldTick) + c.u16(0x16);
	return l;
}

std::vector<GuildMember> guild_members(const SaveGame& save, uint32_t town) {
	std::vector<GuildMember> members;
	const uint32_t bit = 1u << (town & 31);
	const uint32_t merchants = std::min<uint32_t>(save.globals.u16(global::kWorldMerchantCount), uint32_t(save.merchants.size()));
	const uint32_t towns = save.globals.u32(global::kWorldTownCount);
	for (uint32_t m = 0; m < merchants; ++m) {
		const MerchantMemory& mm = save.merchants[m].memory;
		if ((mm.u8(0x8) & 6) != 0 || (mm.u32(0x468) & bit) == 0) {
			continue;
		}
		GuildMember g;
		g.merchant = uint16_t(m);
		float best = 0.0f;  // 0x5C69C5: compared as floats, a town only above the best so far.
		for (uint32_t t = 0; t < towns; ++t) {
			const int office = tick::facilities_detail::fn_5308a0(save, m, t);
			if (office >= 0) {
				++g.offices;
				const uint32_t opened = save.offices[size_t(office)].memory.u32(0x2C0);
				if (g.first_tick > opened) {
					g.first_tick = opened;
					g.first_town = uint16_t(t);
				}
			}
			const float respect = float(tick::events_detail::fn_4f9580(save, int(m), t));
			if (best < respect) {
				best = respect;
				g.respected_town = uint16_t(t);
			}
		}
		members.push_back(g);
	}
	return members;
}

GuildMemberPage guild_member_page(const SaveGame& save, uint16_t merchant) {
	GuildMemberPage p;
	if (merchant >= save.merchants.size()) {
		return p;
	}
	const MerchantMemory& m = save.merchants[merchant].memory;
	p.sex = m.u8(0x1A) & 1u;
	p.title = tick::merchants_detail::fn_4f9430(save, int(merchant));
	p.home = m.u8(0x19);
	p.rank = m.u8(0x39C + size_t(p.home));
	p.age = (save.globals.u32(global::kWorldTick) - m.u32(0x10)) / 93440u;
	p.married = m.u8(0x31) < save.globals.u32(global::kWorldTownCount);
	p.spouse = m.u8(0x30);
	p.children = m.u8(0x36);
	p.ships = m.u16(0x478);
	return p;
}

namespace {

constexpr uint32_t kMembersFinds = 0x6DF278;  // The guild members' finds (0x5346A0 makes it).
constexpr uint32_t kMerchantFinds = 0x4D8;  // A merchant's own.
constexpr uint32_t kWorldObjects = 0x6DEFB0;  // The 15 world objects ([+0xB10] of the world), 0x2C bytes each.
constexpr int kMediterraneanX = 186;  // 0x673ADC
constexpr int kMediterraneanY = 16;  // 0x673ADE
constexpr int kMediterraneanScale = 53;  // 0x673AE0: 100 map pixels to so many of the whole map's.

// 0x673858 (u16 x, u16 y): the place of each world object on the whole map by its [+0x4]: the 30 of the
// Mediterranean's and the North Sea's trading posts, then 20 of America's. (Past them the game reads what follows in
// .rdata, as events_rare.cpp's copy keeps; no world object has such a place: not shown here.)
constexpr uint16_t kCentrePlaces[50][2] = {
	{248, 22}, {236, 29}, {217, 40}, {216, 51}, {217, 31}, {227, 61}, {249, 46}, {259, 37}, {273, 28}, {280, 27},
	{286, 23}, {294, 28}, {301, 36}, {310, 41}, {314, 49}, {305, 56}, {309, 59}, {287, 47}, {335, 54}, {323, 33},
	{358, 53}, {341, 42}, {347, 69}, {341, 61}, {343, 55}, {394, 74}, {371, 90}, {289, 60}, {261, 64}, {345, 84},
	{93, 8}, {88, 16}, {80, 19}, {73, 22}, {66, 25}, {61, 31}, {56, 36}, {45, 42}, {40, 45}, {38, 51}, {31, 53},
	{32, 64}, {26, 68}, {19, 72}, {12, 77}, {4, 84}, {1, 96}, {3, 110}, {17, 137}, {40, 144},
};

// An expeditions' finds object of the game's memory (0x64 bytes from `at`).
template <typename Memory>
void add_finds(ExpeditionFinds& f, const Memory& m, uint32_t at) {
	for (uint32_t k = 0; k < 12; ++k) {
		f.points[k] |= m.u32(at + 4 * k);
		f.wide[k] |= m.u32(at + 0x30 + 4 * k);
	}
	f.centres |= m.u32(at + 0x60);
}

// A brush of 0x534A40: `rows` rows of `stride` bytes from (−r, −r), each 255 − (dx² + dy²)·f, 0 past 255.
std::vector<uint8_t> brush(int r, int f, int rows, int stride) {
	std::vector<uint8_t> b(size_t(rows) * size_t(stride));
	for (int dy = -r; dy + r < rows; ++dy) {
		for (int dx = -r; dx + r < stride; ++dx) {
			const int v = (dx * dx + dy * dy) * f;
			b[size_t(dy + r) * size_t(stride) + size_t(dx + r)] = v > 255 ? 0 : uint8_t(255 - v);
		}
	}
	return b;
}

}  // namespace

ExpeditionFinds guild_map_finds(const SaveGame& save, uint32_t merchant) {
	ExpeditionFinds f;
	const bool player = merchant < merchant_count(save) && merchant < save.merchants.size();
	if (player) {
		add_finds(f, save.merchants[merchant].memory, kMerchantFinds);
	}
	if (!player || save.merchants[merchant].memory.u32(0x468) != 0) {
		// 0x6DF278: what the engine added to it, with every member's own finds as the load makes it (0x5348B0).
		add_finds(f, save.globals, kMembersFinds);
		for (uint32_t m = 0; m < merchant_count(save) && m < save.merchants.size(); ++m) {
			if (save.merchants[m].memory.u32(0x468) != 0) {
				add_finds(f, save.merchants[m].memory, kMerchantFinds);
			}
		}
	}
	return f;
}

TradingMap trading_map(const SaveGame& save, const ExpeditionFinds& finds) {
	TradingMap map;
	// No point of the first 100 (the bits of the first three words and the fourth's low four): the Mediterranean.
	map.mediterranean = finds.points[0] == 0 && finds.points[1] == 0 && finds.points[2] == 0 && (finds.points[3] & 0xF) == 0;
	const int r1 = map.mediterranean ? 9 : 5;
	const int f1 = map.mediterranean ? 3 : 8;
	const int r2 = map.mediterranean ? 13 : 7;
	const int f2 = map.mediterranean ? 2 : 5;
	const std::vector<uint8_t> narrow = brush(r1, f1, 16, 32);
	const std::vector<uint8_t> wide = brush(r2, f2, 24, 48);
	map.fog.assign(size_t(kTradingMapWidth) * kTradingMapHeight, 200);
	for (uint32_t j = 0; j < 384 && j < save.expeditions.size(); ++j) {
		const uint32_t bit = 1u << (j & 31);
		if (!(finds.points[j >> 5] & bit)) {
			continue;
		}
		const bool big = (finds.wide[j >> 5] & bit) != 0;
		const int r = big ? r2 : r1;
		const std::vector<uint8_t>& b = big ? wide : narrow;
		const int stride = big ? 48 : 32;
		int x = save.expeditions[j][0];
		int y = save.expeditions[j][1];
		if (map.mediterranean) {
			x = (x * 5 - kMediterraneanX * 5) * 20 / kMediterraneanScale;
			y = (y * 5 - kMediterraneanY * 5) * 20 / kMediterraneanScale;
		}
		// The brush's left column and its middle row.
		x = x < r ? 0 : x + r >= kTradingMapWidth ? (kTradingMapWidth / 2 - r) * 2 : x - r;
		y = y < r ? r : y + r > kTradingMapHeight ? kTradingMapHeight - r : y;
		// Its rows from the top and from the bottom together, the same brush row on both.
		for (int i = 0; i < r; ++i) {
			uint8_t* top = &map.fog[size_t(y - r + i) * kTradingMapWidth + size_t(x)];
			uint8_t* bottom = &map.fog[size_t(y + r - 1 - i) * kTradingMapWidth + size_t(x)];
			const uint8_t* row = &b[size_t(i) * size_t(stride)];
			for (int c = 0; c < 2 * r; ++c) {
				top[c] = top[c] > row[c] ? uint8_t(top[c] - row[c]) : 0;
				bottom[c] = bottom[c] > row[c] ? uint8_t(bottom[c] - row[c]) : 0;
			}
		}
	}
	map.centres = finds.centres;
	return map;
}

std::vector<TradingCentre> trading_centres(const SaveGame& save, const TradingMap& map) {
	std::vector<TradingCentre> out;
	const GlobalMemory& g = save.globals;
	for (uint32_t k = 0; k < 15; ++k) {
		const uint32_t o = kWorldObjects + 0x2C * k;
		if (!(map.centres & (1u << k)) || g.u32(o + 8) > g.u32(global::kWorldTick) || g.u8(o + 6) >= merchant_count(save)) {
			continue;
		}
		TradingCentre c;
		c.object = k;
		c.place = g.u8(o + 4);
		c.kind = g.u8(o + 5);
		c.finder = g.u8(o + 6);
		c.tick = g.u32(o + 8);
		c.offer = {g.u8(o + 0), g.u8(o + 1)};
		c.demand = {g.u8(o + 2), g.u8(o + 3)};
		if (c.place >= 50) {
			continue;
		}
		int16_t x = int16_t(kCentrePlaces[c.place][0]);
		int16_t y = int16_t(kCentrePlaces[c.place][1]);
		if (map.mediterranean) {
			// In 16 bits (0x5C64EF..0x5C6573): less the corner, times 100 as a short, divided by 53.
			x = int16_t(int16_t(int16_t(x - kMediterraneanX) * 100) / kMediterraneanScale);
			y = int16_t(int16_t(int16_t(y - kMediterraneanY) * 100) / kMediterraneanScale);
		}
		if (x < 0 || x >= kTradingMapWidth || y < 0 || y >= kTradingMapHeight) {
			continue;
		}
		c.x = x;
		c.y = y;
		out.push_back(c);
	}
	return out;
}

int trading_centre_at(const std::vector<TradingCentre>& centres, int x, int y, int limit) {
	int best = -1;
	int nearest = limit;
	for (size_t k = 0; k < centres.size(); ++k) {
		const int dx = x - centres[k].x;
		const int dy = y - centres[k].y;
		if (dx * dx + dy * dy < nearest) {
			nearest = dx * dx + dy * dy;
			best = int(k);
		}
	}
	return best;
}

}  // namespace p3::data
