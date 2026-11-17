// The ship list window's rules (ship_list_window.hpp; re/emu/sesion40/README.md).

#include "p3data/ship_list_window.hpp"

#include <algorithm>
#include <utility>

#include "p3data/game_memory.hpp"
#include "p3data/player_actions.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kConvoyCount = global::kShipCount + 2;  // 0x6DD896, u16 ([+0xF6] of the ships' object).

uint32_t ship_count(const SaveGame& save) {
	return std::min<uint32_t>(save.globals.u16(global::kShipCount), uint32_t(save.ships.size()));
}

uint32_t convoy_count(const SaveGame& save) {
	return std::min<uint32_t>(save.globals.u16(kConvoyCount), uint32_t(save.convoys.size()));
}

const ShipMemory& ship_at(const SaveGame& save, uint16_t ship) {
	return save.ships[ship].memory;
}

// A ship's name as the game compares it: its Latin-1 bytes at [+0x160] (32, to the first zero).
const char* name_of(const SaveGame& save, uint16_t ship) {
	return reinterpret_cast<const char*>(ship_at(save, ship).bytes.data() + 0x160);
}

// strcmp (0x639CF0) on two names, bounded by their 32 bytes.
int compare_names(const SaveGame& save, uint16_t a, uint16_t b) {
	const unsigned char* x = reinterpret_cast<const unsigned char*>(name_of(save, a));
	const unsigned char* y = reinterpret_cast<const unsigned char*>(name_of(save, b));
	for (int k = 0; k < 32; ++k) {
		if (x[k] != y[k]) {
			return x[k] < y[k] ? -1 : 1;
		}
		if (x[k] == 0) {
			return 0;
		}
	}
	return 0;
}

// The exchange sort the window's sorts share: for each entry, every later one that `before` it is swapped in.
template <class Before>
void exchange_sort(std::vector<uint16_t>& v, uint32_t begin, uint32_t end, Before before) {
	for (uint32_t i = begin; i < end; ++i) {
		for (uint32_t j = i + 1; j < end; ++j) {
			if (before(v[j], v[i])) {
				std::swap(v[i], v[j]);
			}
		}
	}
}

// The runs of entries with the same `key` from `begin` to `end`, each sorted by name (the second pass of 0x477DA0,
// 0x477F70, 0x478120).
template <class Key>
void names_in_runs(const SaveGame& save, std::vector<uint16_t>& v, uint32_t begin, uint32_t end, Key key) {
	uint32_t at = begin;
	while (at < end) {
		const uint32_t start = at;
		const auto k = key(v[at]);
		while (at < end && key(v[at]) == k) {
			++at;
		}
		exchange_sort(v, start, at, [&](uint16_t a, uint16_t b) { return compare_names(save, a, b) < 0; });
	}
}

// 100 · [+0x18] / [+0x14] (signed; 0 for a ship without a hull's top, where the game would divide by zero).
int32_t exact_hull(const ShipMemory& s) {
	const int32_t top = s.s32(0x14);
	return top != 0 ? s.s32(0x18) * 100 / top : 0;
}

// What the others' figures are off by (0x495340..0x495540): ((n % k) − k / 2) % of the figure, as the game works it:
// the figure << 16 times that (32 bits), divided by 100 (the compiler's 0x51EB851F), shifted back by 16.
int32_t figure_offset(int32_t figure, uint16_t ship, int32_t k) {
	const int32_t off = int32_t(ship % uint32_t(k)) - k / 2;
	const int32_t product = int32_t(uint32_t(figure) << 16) * off;
	const int32_t hundredth = int32_t((int64_t(product) * 0x51EB851FLL) >> 37) + (product < 0 ? 1 : 0);
	return hundredth >> 16;
}

}  // namespace

ShipList merchant_ship_list(const SaveGame& save, uint32_t merchant) {
	ShipList list;
	if (merchant >= save.merchants.size()) {
		return list;
	}
	const uint32_t ships = ship_count(save);
	size_t walked = 0;
	for (uint32_t s = save.merchants[merchant].first_ship(); s < ships && walked < save.ships.size();
		 s = ship_at(save, uint16_t(s)).u16(0x4), ++walked) {
		if (ship_at(save, uint16_t(s)).u16(0x134) != SavedShip::kFreeShipStatus) {
			list.ships.push_back(uint16_t(s));
		}
	}
	list.own = uint32_t(list.ships.size());
	return list;
}

ShipList port_ship_list(const SaveGame& save, uint32_t merchant, uint32_t town, bool others) {
	ShipList list;
	const uint32_t ships = ship_count(save);
	auto add = [&](uint32_t m) {
		size_t walked = 0;
		uint32_t n = 0;
		for (uint32_t s = save.merchants[m].first_ship(); s < ships && walked < save.ships.size();
			 s = ship_at(save, uint16_t(s)).u16(0x4), ++walked) {
			const ShipMemory& ship = ship_at(save, uint16_t(s));
			if (ship.u8(0x39) == town && ship.u16(0x134) < 4) {
				list.ships.push_back(uint16_t(s));
				++n;
			}
		}
		return n;
	};
	if (merchant < save.merchants.size()) {
		list.own = add(merchant);
	}
	const uint32_t merchants =
		std::min<uint32_t>(save.globals.u16(global::kWorldMerchantCount), uint32_t(save.merchants.size()));
	for (uint32_t m = merchants; others && m-- > 0;) {
		if (m != merchant) {
			list.others += add(m);
		}
	}
	return list;
}

ShipList given_ship_list(const SaveGame& save, uint32_t merchant, const std::vector<uint16_t>& ships) {
	ShipList list;
	for (const uint16_t s : ships) {
		if (s < save.ships.size()) {
			list.ships.push_back(s);
			(ship_at(save, s).u8(0x0) == merchant ? list.own : list.others)++;
		}
	}
	return list;
}

int ship_list_sort_kind(int sort) {
	static constexpr int kKinds[kShipListSorts] = {2, 0, 3, 4, 1};
	return sort >= 0 && sort < kShipListSorts ? kKinds[sort] : 0;
}

void sort_ship_list(const SaveGame& save, ShipList& list, int sort) {
	std::vector<uint16_t>& v = list.ships;
	const uint32_t tables[2][2] = {{0, list.own}, {list.own, list.own + list.others}};
	const uint32_t convoys = convoy_count(save);
	const uint32_t ships = ship_count(save);
	for (const auto& t : tables) {
		const uint32_t begin = t[0];
		const uint32_t end = std::min<uint32_t>(t[1], uint32_t(v.size()));
		switch (ship_list_sort_kind(sort)) {
		case 0:
			exchange_sort(v, begin, end, [&](uint16_t a, uint16_t b) { return compare_names(save, a, b) < 0; });
			break;
		case 1:
			exchange_sort(v, begin, end, [&](uint16_t a, uint16_t b) { return shown_hull(save, a) < shown_hull(save, b); });
			names_in_runs(save, v, begin, end, [&](uint16_t s) { return exact_hull(ship_at(save, s)); });
			break;
		case 2: {
			// 0x478120: a ship in a convoy goes before one out of any; two in convoys by their guides' names.
			auto guide = [&](uint16_t convoy) {
				const uint16_t g = save.convoys[convoy].u16(0x10);
				return g < ships ? g : uint16_t(0);
			};
			exchange_sort(v, begin, end, [&](uint16_t a, uint16_t b) {
				const uint16_t ca = ship_at(save, a).u16(0x8);
				const uint16_t cb = ship_at(save, b).u16(0x8);
				if (ca >= convoys) {
					return false;
				}
				if (cb >= convoys) {
					return true;
				}
				return compare_names(save, guide(ca), guide(cb)) < 0;
			});
			names_in_runs(save, v, begin, end, [&](uint16_t s) { return ship_at(save, s).u16(0x8); });
			break;
		}
		case 3: {
			// 0x478340: by type; then the run of each type 0..3 from where the last one ended, by name.
			exchange_sort(v, begin, end,
				[&](uint16_t a, uint16_t b) { return ship_at(save, a).u8(0xE) < ship_at(save, b).u8(0xE); });
			uint32_t at = begin;
			for (uint32_t type = 0; type <= 3; ++type) {
				const uint32_t start = at;
				while (at < end && ship_at(save, v[at]).u8(0xE) == type) {
					++at;
				}
				exchange_sort(v, start, at, [&](uint16_t a, uint16_t b) { return compare_names(save, a, b) < 0; });
			}
			break;
		}
		case 4:
			exchange_sort(v, begin, end,
				[&](uint16_t a, uint16_t b) { return shown_firepower(save, a) < shown_firepower(save, b); });
			names_in_runs(save, v, begin, end, [&](uint16_t s) { return ship_at(save, s).s32(0x120); });
			break;
		}
	}
}

bool ship_list_convoy_line(const SaveGame& save, const ShipList& list, uint32_t row, int sort) {
	if (ship_list_sort_kind(sort) != 2) {
		return false;
	}
	const uint32_t end = row < list.own ? list.own : list.own + list.others;
	if (row + 1 >= end || row + 1 >= list.ships.size()) {
		return false;
	}
	return ship_at(save, list.ships[row]).u16(0x8) != ship_at(save, list.ships[row + 1]).u16(0x8);
}

int32_t shown_hull(const SaveGame& save, uint16_t ship) {
	const ShipMemory& s = ship_at(save, ship);
	int32_t hull = exact_hull(s);
	if (s.u8(0x0) == save.globals.u32(global::kPlayer)) {
		return hull;
	}
	hull -= figure_offset(hull, ship, 20);
	return hull > 100 ? 100 : hull < 0 ? 0 : hull;
}

int32_t shown_cargo(const SaveGame& save, uint16_t ship) {
	const ShipMemory& s = ship_at(save, ship);
	const int32_t cargo = s.s32(0x118);
	if (s.u8(0x0) == save.globals.u32(global::kPlayer)) {
		return cargo;
	}
	const int32_t shown = cargo - 200 * figure_offset(cargo / 200, ship, 70);
	return shown < 0 ? 0 : shown;
}

uint32_t shown_sailors(const SaveGame& save, uint16_t ship) {
	const ShipMemory& s = ship_at(save, ship);
	const int32_t sailors = s.u16(0x40);
	if (s.u8(0x0) == save.globals.u32(global::kPlayer)) {
		return uint32_t(sailors);
	}
	const int32_t shown = sailors - figure_offset(sailors, ship, 40);
	if (shown < 1) {
		return 1;
	}
	const uint32_t most = ship_most_sailors(save, ship);
	return uint32_t(shown) > most ? most : uint32_t(shown);
}

int32_t shown_firepower(const SaveGame& save, uint16_t ship) {
	const ShipMemory& s = ship_at(save, ship);
	const int32_t firepower = s.s32(0x120);
	if (s.u8(0x0) == save.globals.u32(global::kPlayer)) {
		return firepower;
	}
	const int32_t shown = firepower - figure_offset(firepower, ship, 20);
	return shown < 0 ? 0 : shown;
}

bool ship_list_hull_red(const SaveGame& save, uint16_t ship) {
	const uint16_t state = ship_at(save, ship).u16(0x134);
	return uint32_t(shown_hull(save, ship)) < 40 && state != 0x13 && state != 0xE && state != 0xD && state != 0xC;
}

ShipListStatus ship_list_status(const SaveGame& save, uint16_t ship) {
	const ShipMemory& s = ship_at(save, ship);
	ShipListStatus st;
	switch (s.u16(0x134)) {
	case 0x00:
	case 0x01:
	case 0x04:
	case 0x05:
		st = {16042, ShipListWords::Town, false};
		break;
	case 0x02:
	case 0x03:
	case 0x0F:
	case 0x10:
	case 0x15:
		if (s.u8(0x38) < save.globals.u32(global::kWorldTownCount)) {
			st = {16043, ShipListWords::Destination, false};
		} else {
			// 0x473EB8: at sea, at anchor unless it (or its convoy's guide) has a route.
			st = {s.u32(0x24) != 0 ? 16043u : 16042u, ShipListWords::AtSea, false};
			const uint16_t convoy = s.u16(0x8);
			if (st.tex == 16042 && convoy < convoy_count(save)) {
				const uint16_t guide = save.convoys[convoy].u16(0x10);
				if (guide < ship_count(save) && ship_at(save, guide).u32(0x24) != 0) {
					st.tex = 16043;
				}
			}
		}
		break;
	case 0x06: st = {16015, ShipListWords::Town, false}; break;
	case 0x07: st = {16017, ShipListWords::Town, false}; break;
	case 0x08:
	case 0x09:
	case 0x0A:
	case 0x0B: st = {16062, ShipListWords::Town, false}; break;
	case 0x0C: st = {16025, ShipListWords::Auction, true}; break;
	case 0x0D: st = {16024, ShipListWords::Offered, true}; break;
	case 0x0E: st = {16016, ShipListWords::Town, true}; break;
	case 0x11: st = {16050, ShipListWords::Sunk, false}; break;
	case 0x13: st = {16043, ShipListWords::Expedition, true}; break;
	case 0x14: st = {16043, ShipListWords::SeaBattle, false}; break;
	case 0x16: st = {16062, ShipListWords::HanseFleet, false}; break;
	default: break;
	}
	return st;
}

int ship_list_coins(int32_t value) {
	if (value == 0) {
		return 0;
	}
	static constexpr int32_t kSteps[] = {0xCB2, 0x2422, 0x5302, 0x9952, 0xF712, 0x16C4B, 0x1F8E2};
	int coins = 1;
	for (const int32_t step : kSteps) {
		if (value < step) {
			return coins;
		}
		++coins;
	}
	return 8;
}

ShipListMarks ship_list_marks(const SaveGame& save, uint16_t ship) {
	const ShipMemory& s = ship_at(save, ship);
	ShipListMarks m;
	const uint16_t c = s.u16(0x8);
	const bool convoy = c < convoy_count(save);
	const uint32_t ships = ship_count(save);
	if (s.u8(0x136) & 1) {
		m.trade_route = true;
	} else if (convoy) {
		const uint16_t guide = save.convoys[c].u16(0x10);
		m.trade_route = guide < ships && (ship_at(save, guide).u8(0x136) & 1);
	}
	const bool pirate = s.u16(0x134) == 0x12 || s.u8(0x15C) != 0;
	if (convoy && !(save.convoys[c].u8(0x14) & 0x10)) {
		if (pirate) {
			m.colour = 0xFF282828;
		} else {
			// 0x503AC0 on the convoy: the Hanse's fleet of its event 0x34 ([+0x2] the event, whose [+0x16] is it).
			const uint16_t e = save.convoys[c].u16(0x2);
			const bool hanse = e < save.globals.u16(0x6DD748) && e < save.events.size() &&
							   save.events[e].u16(0x6) == 0x34 && save.events[e].u16(0x16) == c;
			if (hanse) {
				m.colour = 0xFFFFFF00;
			} else if (s.u8(0x0) == save.globals.u32(global::kPlayer)) {
				m.colour = 0xFF0000FF;
			} else {
				const uint8_t owner = s.u8(0x0);
				m.colour = owner < save.merchants.size() && save.merchants[owner].kind() != 0 ? 0xFFFFFFFF : 0xFFFF0000;
			}
		}
		m.tex = save.convoys[c].u16(0x10) == ship ? 16002 : 16004;
	} else if (pirate) {
		m.tex = 16003;
	}
	return m;
}

ShipListAction ship_list_action(const SaveGame& save, uint16_t ship) {
	switch (ship_at(save, ship).u16(0x134)) {
	case 0x13: return ShipListAction::None;
	case 0x11: return ShipListAction::Delete;
	case 0x0C:
	case 0x0D:
	case 0x0E: return ShipListAction::GoToOnly;
	default: return ShipListAction::GoTo;
	}
}

bool ship_list_can_choose(const SaveGame& save, uint16_t ship) {
	const uint16_t state = ship_at(save, ship).u16(0x134);
	return state != 0x0C && state != 0x0D && state != 0x0E && state != 0x13;
}

int ship_list_column_x(int column) {
	int x = 0;
	for (int k = 0; k < column && k < kShipListColumns; ++k) {
		x += kShipListColumnWidth[k] + kShipListColumnSpacing[k];
	}
	return x;
}

ShipListLayout ship_list_layout(int view_x, int view_y, int view_w, int view_h, uint32_t own, uint32_t others,
	int scroll_bar_width) {
	constexpr int kTile = kShipListTile;
	constexpr int kRow = kShipListRowHeight + kShipListRowSpacing;
	constexpr int kCloseY = -40;
	ShipListLayout l;
	int h0 = kShipListChartY - kCloseY + kRow * int(own) + kShipListHeadHeight;
	if (others != 0) {
		h0 += kRow + kRow * int(others) + kShipListHeadHeight + kShipListHeadHeight;
	}
	l.w = (kTile + kShipListRowButton + kShipListChartWidth + scroll_bar_width) / kTile * kTile;
	if (h0 < view_h) {
		l.h = (kTile + h0) / kTile * kTile;
		l.rows1 = int(own);
		l.rows2 = int(others);
	} else {
		l.h = view_h / kTile * kTile;
		if (others != 0 && own != 0) {
			const int rows = (kCloseY - kShipListChartY + l.h - 2 * kShipListHeadHeight - kRow) / kRow;
			const int half = rows / 2;
			if (int(own) < half) {
				l.rows1 = int(own);
				l.rows2 = rows - int(own);
			} else if (int(others) < half) {
				l.rows1 = rows - int(others);
				l.rows2 = int(others);
			} else {
				l.rows1 = half;
				l.rows2 = rows - half;
			}
		} else {
			const int rows = (kCloseY - kShipListChartY + l.h - kShipListHeadHeight) / kRow;
			l.rows1 = others != 0 ? 0 : rows;
			l.rows2 = others != 0 ? rows : 0;
		}
	}
	l.x = (view_w - l.w) / 2 + view_x;
	if (l.x < 0) {
		l.x = 0;
	}
	const int d = (view_h - l.h) / 2;
	l.y = view_y + (d < kTile / 2 ? d : kTile / 2);  // 0x475504: at most half a tile from the top.
	l.x += l.x % 4;
	// The second chart under the first one's rows shown (0x4756F4..0x475714: its height, then a row's height more).
	l.chart2_y = kShipListChartY;
	if (own != 0) {
		const int height = kShipListRowHeight * l.rows1 + kShipListRowSpacing * (l.rows1 - 1) + kShipListHeadHeight;
		l.chart2_y += height + kShipListRowHeight;
	}
	return l;
}

}  // namespace p3::data
