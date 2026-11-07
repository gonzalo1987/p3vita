// Probes for functions that the task cloud/unchecked-branches calls directly
// in its retouched states (tools/oracle/retouch_unchecked.py) or records on
// their own, so that a difference points at them and not at their caller.

#include <cstring>
#include <optional>

#include "p3data/mail.hpp"
#include "p3data/game_texts.hpp"
#include "p3data/player_actions.hpp"
#include "p3data/sailing.hpp"
#include "p3data/statistics_window.hpp"
#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

// What alderman.cpp exposes for them.
namespace p3::data::tick::alderman_detail {

void fn_51a750(SaveGame& save, int ship, uint8_t n);
void fn_51a4e0(SaveGame& save, int ship, uint32_t strength, uint8_t level);
void fn_516170(SaveGame& save, uint32_t pirate, uint32_t type, bool captain, uint32_t strength);
void fn_5144a0(SaveGame& save, uint32_t pirate, uint32_t nest);

}  // namespace p3::data::tick::alderman_detail

// What construction.cpp exposes for them.
namespace p3::data::tick::construction_detail {

bool fn_4f9c80(const SaveGame& save, int town, uint8_t& x, uint8_t& y, uint32_t type, uint8_t param, bool flag);
void fn_4fa7f0(const SaveGame& save, int town, uint8_t& x, uint8_t& y);
void fn_5250d0(const SaveGame& save, int town, uint8_t& x, uint8_t& y);
bool fn_51f3a0(SaveGame& save, int town, GameMemory<0x44>& s);
void fn_4f8a50(SaveGame& save, int merchant, uint32_t town, bool count);
void fn_4f8c70(SaveGame& save, int merchant, uint32_t town);
void fn_4f8560(SaveGame& save, int merchant, uint32_t town, int32_t amount);

}  // namespace p3::data::tick::construction_detail

// What merchants.cpp exposes for them.
namespace p3::data::tick::merchants_detail {

void fn_4f68d0(SaveGame& save, MerchantMemory& m, uint32_t tick, uint32_t a1, uint8_t a3, uint8_t a4);

}  // namespace p3::data::tick::merchants_detail

// What sieges.cpp exposes for them.
namespace p3::data::tick::sieges_detail {

void fn_629c60(SaveGame& save, int siege, uint8_t kind, uint8_t r, uint8_t flag);

}  // namespace p3::data::tick::sieges_detail

// What new_town.cpp exposes for them.
namespace p3::data::tick::new_town_detail {

void fn_5462f0(SaveGame& save, int town, uint32_t k);

}  // namespace p3::data::tick::new_town_detail

// What events.cpp exposes for them (the sale of a building).
namespace p3::data::tick::events_detail {

void fn_4dafe0(const SaveGame& save, uint32_t town, uint32_t record, uint32_t& tick, int32_t& price);
void fn_4dab00(SaveGame& save, uint32_t town_arg, uint32_t record_arg);

}  // namespace p3::data::tick::events_detail

// What events_towns.cpp exposes for them (a ship given to a merchant).
namespace p3::data::tick::events_towns_detail {

uint16_t fn_50e000(SaveGame& save, uint16_t& type, uint32_t strength, uint32_t town_arg, uint32_t merchant_arg);

}  // namespace p3::data::tick::events_towns_detail

// What events_big.cpp exposes for them (where to look for a site).
namespace p3::data::tick::events_big_detail {

void fn_500f50(const SaveGame& save, int office, uint8_t& x, uint8_t& y, int32_t n, uint8_t type);

}  // namespace p3::data::tick::events_big_detail

// What events_rare.cpp exposes for them (a building out of the auctions).
namespace p3::data::tick::events_rare_detail {

void fn_4db100(SaveGame& save, uint32_t town, uint16_t record);

}  // namespace p3::data::tick::events_rare_detail

// What events_towns.cpp exposes for them (the dividends of company shares).
namespace p3::data::tick::events_towns_detail {

uint32_t fn_4e8804(SaveGame& save, uint16_t event);

}  // namespace p3::data::tick::events_towns_detail

namespace p3::probe {

using namespace p3::data;

namespace {

// The pirate object at `address` (*[0x6DD7AC + 4·k]): k, or -1.
int pirate_at(const SaveGame& s, uint32_t address) {
	for (uint32_t k = 0; k < 5; ++k) {
		if (address != 0 && s.globals.u32(0x6DD7AC + 4 * k) == address && s.pirates[k]) {
			return int(k);
		}
	}
	return -1;
}

// The siege whose army is at `address` (its [+0x8]), or -1.
int army_of(const SaveGame& s, uint32_t address) {
	for (size_t i = 0; i < s.sieges.size(); ++i) {
		if (address != 0 && s.sieges[i].u32(0x8) == address && i < s.siege_extras.size() && s.siege_extras[i].army) {
			return int(i);
		}
	}
	return -1;
}

// The town whose town_class1 ([0x7A4]) is at `address`, or -1.
int town_class1(const ProbeCall& c, uint32_t address) {
	const Location l = c.locate(address);
	return l.kind == Location::Town && l.offset == 0x7A4 ? l.index : -1;
}

// Pointer arguments to the caller's variables can't be repeated; the
// scenarios of retouch_unchecked.py point them at scratch globals
// (0x6DFF00..0x6DFFFF, which the game doesn't use), which the state holds.
bool in_globals(const ProbeCall& c, uint32_t address) {
	return c.locate(address).kind == Location::Global;
}

constexpr uint32_t kScratch = 0x6DFF00;
// The oracle's copier of 0x504AC0's list, in its sentinel page
// (retouch_unchecked.py WINDOW_SHIPS: SENTINEL + 0xF80).
constexpr uint32_t kWindowShips = 0x03000F80;
// Its copiers of the convoy window's choices (0x40C170) and of the market
// window's convoy (0x4AA4A0) (CONVOY_CHOICES, CONVOY_SUMMARY).
constexpr uint32_t kConvoyChoices = 0x03000E80;
constexpr uint32_t kConvoySummary = 0x03000E00;
// Its copiers of the moneylender's lists (LENDER_LISTS: SENTINEL + 0xC00 +
// 0x50·k): 0x4D9100, 0x4D9210, 0x4D9320, 0x4D9620.
constexpr uint32_t kLenderLists = 0x03000C00;
// Its copiers of the build window (BUILD_MENU, BUILD_FITS, BUILD_MARKS): its
// menu 0x47AB20, whether a building fits 0x47B8C0 and its marks 0x47B830, on a
// window of their own.
constexpr uint32_t kBuildMenu = 0x03000200;
constexpr uint32_t kBuildFits = 0x03000280;
constexpr uint32_t kBuildMarks = 0x03000300;

// Its copiers of the statistics window (STATS_BALANCE, STATS_GRAPH): 0x4A2F60 and 0x4A22A0 on a window of their own;
// the view's part per call (0xC0 bytes), then its FNV-1a and its length.
constexpr uint32_t kStatsBalance = 0x03000D00;
constexpr uint32_t kStatsGraph = 0x03000D40;
constexpr uint32_t kStatsSupply = 0x03000DC0;
// The competition's (STATS_COMPETITION, STATS_CHOICE, STATS_SLOTS): 0x4A19C0, 0x49E850 with the list's sorts
// 0x4720D0 and 0x471EA0, and the part of 0x49D380 that picks the merchants.
constexpr uint32_t kStatsCompetition = 0x03000EC0;
constexpr uint32_t kStatsChoice = 0x03000F00;
constexpr uint32_t kStatsSlots = 0x03000F40;
// The towns' pages' (STATS_TOWNS, STATS_RESIDENTS, STATS_WARES): 0x4A3FA0, 0x4A3BB0, and the comparison's ware
// button's list in the window's update.
constexpr uint32_t kStatsTowns = 0x03000F50;
constexpr uint32_t kStatsResidents = 0x03000F58;
constexpr uint32_t kStatsWares = 0x03000F60;
// The town data's town button's list (STATS_TOWN_LIST): in the window's update, 0x49FB41.
constexpr uint32_t kStatsTownList = 0x03000F68;
// The top bar's man, the citizens' window's update 0x40B570 (CITIZEN_STATS).
constexpr uint32_t kCitizenStats = 0x03000FC0;
constexpr size_t kStatsView = 0xC0;

StatisticsMerchants unpack_slots(uint32_t packed) {
	return {uint8_t(packed), uint8_t(packed >> 8), uint8_t(packed >> 16), uint8_t(packed >> 24)};
}

// The window's choices of the towns' pages from the copiers' arguments: wares, towns, single (ware | town << 8).
StatisticsTownChoices unpack_choices(uint32_t wares, uint32_t towns, uint32_t single) {
	StatisticsTownChoices c;
	c.wares = unpack_slots(wares);
	c.towns = unpack_slots(towns);
	c.ware = uint8_t(single);
	c.town = uint8_t(single >> 8);
	return c;
}


// A C string as the game's buffer holds it: its bytes up to `size`, zero-padded (no terminator past `size`).
void put_text(std::vector<uint8_t>& view, const std::string& text, size_t size) {
	for (size_t k = 0; k < size; ++k) {
		view.push_back(k < text.size() ? uint8_t(text[k]) : 0);
	}
}

// sprintf's texts one after another into zeroed slots of `slot` bytes from `at`: a longer one runs into the next
// slot, which the next text then overwrites, as the game's.
void put_texts(std::vector<uint8_t>& view, size_t at, const std::string* texts, size_t count, size_t slot) {
	for (size_t k = 0; k < count; ++k) {
		const std::string& t = texts[k];
		for (size_t i = 0; i <= t.size() && at + slot * k + i < view.size(); ++i) {
			view[at + slot * k + i] = i < t.size() ? uint8_t(t[i]) : 0;
		}
	}
}

// A graph's view as the copiers make it (STATS_GRAPH's, then with the competition and the towns the period after
// the call and each series' header and points; with the towns, the button [+0x214]'s text after the buttons' and the
// window's four towns after the period: `choices`).
std::vector<uint8_t> graph_view(const GameTexts& texts, const StatisticsGraph& g, int entry, int period,
	const StatisticsTownChoices* choices) {
	const bool towns = choices != nullptr;
	std::vector<uint8_t> view;
	auto u32 = [&](uint32_t v) {
		for (int k = 0; k < 4; ++k) {
			view.push_back(uint8_t(v >> (8 * k)));
		}
	};
	u32(g.series);
	u32(g.points);
	u32(g.missing);
	view.push_back(uint8_t(g.texts.size()));
	view.push_back(uint8_t(g.texts.size() >> 8));
	for (size_t k = 0; k < 8; ++k) {
		view.push_back(k < g.kinds.size() ? g.kinds[k] : 0);
	}
	for (size_t k = 0; k < 7; ++k) {
		put_text(view, k < g.labels.size() && g.labels[k] ? texts.raw_at(g.labels[k]) : std::string(), 0x20);
	}
	const size_t at = view.size();
	view.resize(at + 0xE0, 0);
	put_texts(view, at, g.texts.data(), g.texts.size(), 0x20);
	for (const std::string& button : g.buttons) {
		put_text(view, button, 0x20);
	}
	if (towns) {
		put_text(view, g.choice, 0x20);
	}
	put_text(view, g.series ? statistics_title(texts, entry) + g.title_end : std::string(), 0x40);
	const int now = statistics_competition_period(period);
	view.push_back(uint8_t(now));
	view.push_back(uint8_t(now >> 8));
	if (towns) {
		view.insert(view.end(), choices->towns.begin(), choices->towns.end());
	}
	for (uint32_t k = 0; k < g.series && k < 5; ++k) {
		u32(0xFF000000u | g.colours[k]);  // The copier's colours have the alpha.
		u32(g.merchants[k]);
		for (uint32_t p = 0; p < g.points; ++p) {
			uint32_t bits = 0;
			const float v = p < g.values[k].size() ? g.values[k][p] : 0.0f;
			std::memcpy(&bits, &v, 4);
			u32(bits);
		}
	}
	return view;
}

void write_view(SaveGame& s, const std::vector<uint8_t>& view, uint32_t part) {
	uint32_t h = 0x811C9DC5;
	for (uint8_t b : view) {
		h = (h ^ b) * 0x01000193;
	}
	for (size_t k = 0; k < kStatsView; ++k) {
		const size_t at = kStatsView * part + k;
		s.globals.u8(kScratch + uint32_t(k)) = at < view.size() ? view[at] : 0;
	}
	s.globals.u32(kScratch + 0xC0) = h;
	s.globals.u32(kScratch + 0xC4) = uint32_t(view.size());
}

// A loan's terms (16 bytes) or an entry in the scratch globals.
template <size_t N>
bool scratch_bytes(const ProbeCall& c, uint32_t address) {
	return in_globals(c, address) && in_globals(c, address + uint32_t(N) - 1);
}

template <size_t N>
std::array<uint8_t, N> read_scratch(const SaveGame& s, uint32_t address) {
	std::array<uint8_t, N> out{};
	for (size_t k = 0; k < N; ++k) {
		out[k] = s.globals.u8(address + uint32_t(k));
	}
	return out;
}

template <size_t N>
void write_scratch(SaveGame& s, uint32_t address, const uint8_t* bytes) {
	for (size_t k = 0; k < N; ++k) {
		s.globals.u8(address + uint32_t(k)) = bytes[k];
	}
}

}  // namespace

void register_probes_unchecked(ProbeRegistry& r) {
	// The market's prices, thiscall on the prices' object 0x6DE3D8 (ware,
	// town, amount): what buying (0x52E430) or selling (0x52E1D0) costs.
	r[0x52E430] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(tick::game_buy_price(s, int(c.args[0]), int(c.args[1]), int32_t(c.args[2])));
	};
	r[0x52E1D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(tick::game_sell_price(s, int(c.args[0]), int(c.args[1]), int32_t(c.args[2])));
	};
	namespace ad = tick::alderman_detail;
	r[0x51A4E0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the ship (strength, level).
		ad::fn_51a4e0(s, c.ship(c.ecx), c.args[0], uint8_t(c.args[1]));
		return std::nullopt;
	};
	r[0x51A750] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the ship (levels).
		ad::fn_51a750(s, c.ship(c.ecx), uint8_t(c.args[0]));
		return std::nullopt;
	};
	r[0x516170] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the pirate object (type, captain, strength); returns the
		// new ship's address, which isn't compared.
		const int k = pirate_at(s, c.ecx);
		if (k < 0) {
			c.skip = true;
			return std::nullopt;
		}
		ad::fn_516170(s, uint32_t(k), c.args[0], (c.args[1] & 0xFF) != 0, c.args[2] & 0xFFFF);
		return std::nullopt;
	};
	namespace cd = tick::construction_detail;
	r[0x4F9C80] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on town_class1 (&x, &y, type, param, flag); returns al.
		const int town = town_class1(c, c.ecx);
		if (town < 0 || !in_globals(c, c.args[0]) || !in_globals(c, c.args[1])) {
			c.skip = true;
			return std::nullopt;
		}
		c.eax_mask = 0xFF;
		uint8_t x = s.globals.u8(c.args[0]);
		uint8_t y = s.globals.u8(c.args[1]);
		const bool found = cd::fn_4f9c80(s, town, x, y, c.args[2], uint8_t(c.args[3]), (c.args[4] & 0xFF) != 0);
		s.globals.u8(c.args[0]) = x;
		s.globals.u8(c.args[1]) = y;
		return found ? 1 : 0;
	};
	r[0x4FA7F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on town_class1 (&x, &y).
		const int town = town_class1(c, c.ecx);
		if (town < 0 || !in_globals(c, c.args[0]) || !in_globals(c, c.args[1])) {
			c.skip = true;
			return std::nullopt;
		}
		uint8_t x = s.globals.u8(c.args[0]);
		uint8_t y = s.globals.u8(c.args[1]);
		cd::fn_4fa7f0(s, town, x, y);
		s.globals.u8(c.args[0]) = x;
		s.globals.u8(c.args[1]) = y;
		return std::nullopt;
	};
	r[0x5250D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the town (&x, &y).
		const int town = c.town(c.ecx);
		if (town < 0 || !in_globals(c, c.args[0]) || !in_globals(c, c.args[1])) {
			c.skip = true;
			return std::nullopt;
		}
		uint8_t x = s.globals.u8(c.args[0]);
		uint8_t y = s.globals.u8(c.args[1]);
		cd::fn_5250d0(s, town, x, y);
		s.globals.u8(c.args[0]) = x;
		s.globals.u8(c.args[1]) = y;
		return std::nullopt;
	};
	r[0x51F3A0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the town (pointer to the game's 0x44-byte structure); returns al.
		const int town = c.town(c.ecx);
		if (town < 0 || !in_globals(c, c.args[0]) || !in_globals(c, c.args[0] + 0x43)) {
			c.skip = true;
			return std::nullopt;
		}
		c.eax_mask = 0xFF;
		GameMemory<0x44> st;
		for (uint32_t k = 0; k < 0x44; ++k) {
			st.u8(k) = s.globals.u8(c.args[0] + k);
		}
		const bool can = cd::fn_51f3a0(s, town, st);
		for (uint32_t k = 0; k < 0x44; ++k) {
			s.globals.u8(c.args[0] + k) = st.u8(k);
		}
		return can ? 1 : 0;
	};
	r[0x4F8A50] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the merchant (town, count).
		cd::fn_4f8a50(s, c.merchant(c.ecx), c.args[0] & 0xFF, (c.args[1] & 0xFF) != 0);
		return std::nullopt;
	};
	r[0x4F8C70] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the merchant (town).
		cd::fn_4f8c70(s, c.merchant(c.ecx), c.args[0] & 0xFF);
		return std::nullopt;
	};
	r[0x4F8560] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the merchant (its index, for the letter; town, amount).
		cd::fn_4f8560(s, c.merchant(c.ecx), c.args[1], int32_t(c.args[2]));
		return std::nullopt;
	};
	r[0x4F68D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the merchant (tick, a1, the merchant, a3, a4): a new life.
		const int m = c.merchant(c.ecx);
		tick::merchants_detail::fn_4f68d0(s, s.merchants[size_t(m)].memory, c.args[0], c.args[1], uint8_t(c.args[3]),
			uint8_t(c.args[4]));
		return std::nullopt;
	};
	r[0x4F8CA0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the merchant (town): his reputation goes down.
		tick::diet_detail::fn_4f8ca0(s, uint32_t(c.merchant(c.ecx)), c.args[0] & 0xFF);
		return std::nullopt;
	};
	// grid.cpp: thiscall on town_class1 ([0x7A4] of the town).
	r[0x4FD680] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int town = town_class1(c, c.ecx);
		if (town < 0) {
			c.skip = true;
			return std::nullopt;
		}
		tick::fn_4fd680(s, town);
		return std::nullopt;
	};
	r[0x4FACD0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int town = town_class1(c, c.ecx);
		if (town < 0) {
			c.skip = true;
			return std::nullopt;
		}
		tick::fn_4facd0(s, town, c.args[0], c.args[1], c.args[2], c.args[3]);
		return std::nullopt;
	};
	r[0x4FD830] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int town = town_class1(c, c.ecx);
		if (town < 0) {
			c.skip = true;
			return std::nullopt;
		}
		tick::fn_4fd830(s, town, c.args[0], c.args[1], c.args[2]);
		return std::nullopt;
	};
	r[0x629C60] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the besieging army (kind, r, flag).
		const int siege = army_of(s, c.ecx);
		if (siege < 0) {
			c.skip = true;
			return std::nullopt;
		}
		tick::sieges_detail::fn_629c60(s, siege, uint8_t(c.args[0]), uint8_t(c.args[1]), uint8_t(c.args[2]));
		return std::nullopt;
	};
	// 0x633DB0 (the ransom; thiscall on the siege object): as the probe of
	// probes_sieges.cpp, but when the scenario points its two outputs (amount,
	// offices) at the scratch globals they are written there, as the game does.
	r[0x633DB0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;
		const Location l = c.locate(c.ecx);
		const int siege = l.kind == Location::Siege && l.offset == 0 ? l.index : -1;
		uint32_t amount = 0;
		uint16_t offices = 0;
		const bool paid = tick::sieges_detail::fn_633db0(s, siege, amount, offices, uint16_t(c.args[2]));
		if (in_globals(c, c.args[0]) && in_globals(c, c.args[1])) {
			s.globals.u32(c.args[0]) = amount;
			s.globals.u16(c.args[1]) = offices;
		}
		return paid ? 1 : 0;
	};
	r[0x5462F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the town (the facility k): a node at the front of [0x9B4].
		tick::new_town_detail::fn_5462f0(s, c.town(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x5144A0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the pirate object (its lair, u16).
		const int k = pirate_at(s, c.ecx);
		if (k < 0) {
			c.skip = true;
			return std::nullopt;
		}
		ad::fn_5144a0(s, uint32_t(k), c.args[0] & 0xFFFF);
		return std::nullopt;
	};
	namespace ed = tick::events_detail;
	r[0x4DAFE0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the event queue (town, record, &price, &tick): is the
		// building on sale? The two outputs in scratch globals.
		if (!in_globals(c, c.args[2]) || !in_globals(c, c.args[3])) {
			c.skip = true;
			return std::nullopt;
		}
		uint32_t tick = 0;
		int32_t price = 0;
		ed::fn_4dafe0(s, c.args[0], c.args[1], tick, price);
		s.globals.u32(c.args[2]) = uint32_t(price);
		s.globals.u32(c.args[3]) = tick;
		return std::nullopt;
	};
	r[0x4DAB00] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the event queue (town, record): the building on sale.
		ed::fn_4dab00(s, c.args[0], c.args[1]);
		return std::nullopt;
	};
	r[0x50E000] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the ship object (&type, strength, town, merchant): a ship
		// given to the merchant; the type (u16, changed for a crayer) in a
		// scratch global. Returns the ship's index.
		if (!in_globals(c, c.args[0])) {
			c.skip = true;
			return std::nullopt;
		}
		uint16_t type = s.globals.u16(c.args[0]);
		c.eax_mask = 0xFFFF;
		const uint16_t index = tick::events_towns_detail::fn_50e000(s, type, c.args[1], c.args[2], c.args[3]);
		s.globals.u16(c.args[0]) = type;
		return index;
	};
	r[0x4DB100] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the event queue (town, record): the building out of the
		// first auction of buildings (event 0x36) that has it.
		tick::events_rare_detail::fn_4db100(s, c.args[0] & 0xFF, uint16_t(c.args[1]));
		return std::nullopt;
	};
	r[0x500F50] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the office (&x, &y, n, type): where to start looking for
		// a site; x and y in scratch globals.
		const int office = c.office(c.ecx);
		if (office < 0 || !in_globals(c, c.args[0]) || !in_globals(c, c.args[1])) {
			c.skip = true;
			return std::nullopt;
		}
		uint8_t x = s.globals.u8(c.args[0]);
		uint8_t y = s.globals.u8(c.args[1]);
		tick::events_big_detail::fn_500f50(s, office, x, y, int32_t(c.args[2]), uint8_t(c.args[3]));
		s.globals.u8(c.args[0]) = x;
		s.globals.u8(c.args[1]) = y;
		return std::nullopt;
	};
	r[0x50B030] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the ships object (the ship): its chase; returns al.
		data::Sailing* sailing = probe_sailing();
		if (!sailing) {
			c.skip = true;
			return std::nullopt;
		}
		c.eax_mask = 0xFF;
		return sailing->fn_50b030(uint16_t(c.args[0])) ? 1 : 0;
	};
	r[0x507FB0] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the ships object (the ship): deletes it.
		data::Sailing* sailing = probe_sailing();
		if (!sailing) {
			c.skip = true;
			return std::nullopt;
		}
		sailing->fn_507fb0(uint16_t(c.args[0]));
		return std::nullopt;
	};
	r[0x518750] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on a ship: whether its menu's «Pirate» can be pressed (can_turn_pirate); returns al.
		const int ship = c.ship(c.ecx);
		data::Sailing* sailing = probe_sailing();
		if (ship < 0 || !sailing) {
			c.skip = true;
			return std::nullopt;
		}
		c.eax_mask = 0xFF;
		return can_turn_pirate(s, *sailing, ship) ? 1 : 0;
	};
	// The player's windows (player_actions.hpp): what they list and offer
	// (the tavern's captain, 0x5269A0, is probes_ship_trade.cpp's).
	r[0x4F6CA0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the merchant (town): the sailors he may hire there; returns al.
		const int merchant = c.merchant(c.ecx);
		if (merchant < 0) {
			c.skip = true;
			return std::nullopt;
		}
		c.eax_mask = 0xFF;
		return tick::fn_4f6ca0(s, s.merchants[size_t(merchant)].memory, c.args[0]);
	};
	r[kWindowShips] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// The oracle's copier (retouch_unchecked.py WINDOW_SHIPS): 0x504AC0
		// (merchant, town, mode, 0) and its list (a u16 count and the ships)
		// copied into the scratch globals 0x6DFF00, at most 126 ships.
		const std::vector<uint16_t> list = window_ships(s, int(c.args[0]), int(c.args[1]), ShipListMode(c.args[2]));
		s.globals.u16(kScratch) = uint16_t(list.size());
		for (size_t k = 0; k < list.size() && k < 126; ++k) {
			s.globals.u16(kScratch + 2 + 2 * uint32_t(k)) = list[k];
		}
		return std::nullopt;
	};
	r[kConvoyChoices] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// The oracle's copier (retouch_unchecked.py CONVOY_CHOICES; stdcall:
		// ship): the convoy window's choices for that ship alone (0x40C170), a
		// u32 count and the choices (−1, −2, convoys) in the scratch globals,
		// at most 63.
		if (c.args[0] >= s.ships.size()) {
			c.skip = true;
			return std::nullopt;
		}
		const std::vector<int32_t> choices = convoy_choices(s, {uint16_t(c.args[0])});
		s.globals.u32(kScratch) = uint32_t(choices.size());
		for (size_t k = 0; k < choices.size() && k < 63; ++k) {
			s.globals.s32(kScratch + 4 + 4 * uint32_t(k)) = choices[k];
		}
		return std::nullopt;
	};
	r[kConvoySummary] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// The oracle's copier (CONVOY_SUMMARY; stdcall: convoy): what the market
		// window keeps of the convoy in its mode 4 (0x4AA4A0): the 24 wares'
		// units, what it holds ([+0x5E8]) and its capacity ([+0x738]), 26 u32
		// in the scratch globals.
		if (c.args[0] >= s.convoys.size()) {
			c.skip = true;
			return std::nullopt;
		}
		const int convoy = int(c.args[0]);
		for (int w = 0; w < 24; ++w) {
			s.globals.s32(kScratch + 4 * uint32_t(w)) = market_convoy_units(s, convoy, w);
		}
		s.globals.s32(kScratch + 0x60) = market_convoy_used(s, convoy);
		s.globals.s32(kScratch + 0x64) = market_convoy_capacity(s, convoy);
		return std::nullopt;
	};
	r[0x52B670] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the town (&out in scratch globals, ship, levels): what
		// upgrading the ship costs (0x44 bytes) and whether it can be done.
		const int town = c.town(c.ecx);
		if (town < 0 || !in_globals(c, c.args[0]) || !in_globals(c, c.args[0] + 0x43)) {
			c.skip = true;
			return std::nullopt;
		}
		GameMemory<0x44> out;
		for (uint32_t k = 0; k < 0x44; ++k) {
			out.u8(k) = s.globals.u8(c.args[0] + k);
		}
		const bool ok = tick::fn_52b670(s, town, out, c.args[1], c.args[2]);
		for (uint32_t k = 0; k < 0x44; ++k) {
			s.globals.u8(c.args[0] + k) = out.u8(k);
		}
		c.eax_mask = 0xFF;
		return ok ? 1 : 0;
	};
	r[0x4D6530] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall on the mailbox (merchant or code, &letter): a letter without
		// a buffer or a record, in scratch globals. The game writes the date
		// into the caller's letter when it sends it.
		if (!in_globals(c, c.args[1]) || !in_globals(c, c.args[1] + 0xF)) {
			c.skip = true;
			return std::nullopt;
		}
		Letter letter;
		for (uint32_t k = 0; k < 0x10; ++k) {
			letter.entry.u8(k) = s.globals.u8(c.args[1] + k);
		}
		const int32_t merchant = int32_t(c.args[0]);
		send_letter(s, merchant, letter);
		if (letter.type() < 0x86 && merchant < int32_t(s.globals.u16(global::kWorldMerchantCount))) {
			s.globals.u8(c.args[1]) = s.globals.u8(global::kWorldDay);
			s.globals.u8(c.args[1] + 1) = uint8_t(s.globals.u8(global::kWorldMonth) | 0x80);
			s.globals.u16(c.args[1] + 2) = s.globals.u16(global::kWorldYear);
		}
		return std::nullopt;
	};
	// The moneylender's window (player_actions.hpp): 0x51CE80 and 0x51D160
	// (thiscall on the town: &buffer of 3 terms, &changed) and 0x51D270 (cdecl:
	// &terms), with their pointers in the scratch globals.
	r[0x51CE80] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int town = c.town(c.ecx);
		if (town < 0 || !scratch_bytes<0x30>(c, c.args[0]) || !in_globals(c, c.args[1])) {
			c.skip = true;
			return std::nullopt;
		}
		std::array<LoanTerms, 3> buffer;
		for (uint32_t k = 0; k < 3; ++k) {
			buffer[k] = read_scratch<0x10>(s, c.args[0] + 0x10 * k);
		}
		bool changed = false;
		const int32_t n = loan_offers(s, town, buffer, &changed);
		for (uint32_t k = 0; k < 3; ++k) {
			write_scratch<0x10>(s, c.args[0] + 0x10 * k, buffer[k].data());
		}
		s.globals.u8(c.args[1]) = changed ? 1 : 0;
		return uint32_t(n);
	};
	r[0x51D160] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int town = c.town(c.ecx);
		if (town < 0 || !scratch_bytes<0x30>(c, c.args[0]) || !in_globals(c, c.args[1])) {
			c.skip = true;
			return std::nullopt;
		}
		std::array<LoanTerms, 3> buffer;
		for (uint32_t k = 0; k < 3; ++k) {
			buffer[k] = read_scratch<0x10>(s, c.args[0] + 0x10 * k);
		}
		bool changed = false;
		const int32_t n = loan_requests(s, town, buffer, &s, &changed);
		for (uint32_t k = 0; k < 3; ++k) {
			write_scratch<0x10>(s, c.args[0] + 0x10 * k, buffer[k].data());
		}
		s.globals.u8(c.args[1]) = changed ? 1 : 0;
		return uint32_t(n);
	};
	r[0x51D270] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		if (!scratch_bytes<0x10>(c, c.args[0])) {
			c.skip = true;
			return std::nullopt;
		}
		LoanTerms terms = read_scratch<0x10>(s, c.args[0]);
		fn_51d270(terms, &s);
		write_scratch<0x10>(s, c.args[0], terms.data());
		return std::nullopt;
	};
	// The copiers of the window's lists (LENDER_LISTS; stdcall: the merchant,
	// for the first two): a u32 count and the entries in the scratch globals
	// from +4, at most 10 loans (copies of their 0x18 bytes, the index in
	// [+0x4]) or 20 shares (12 bytes).
	for (uint32_t k = 0; k < 4; ++k) {
		r[kLenderLists + 0x50 * k] = [k](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
			if (k < 2) {
				const std::vector<uint16_t> loans =
					k == 0 ? loans_owed(s, int(c.args[0])) : loans_granted(s, int(c.args[0]));
				s.globals.u32(kScratch) = uint32_t(loans.size());
				for (size_t n = 0; n < loans.size() && n < 10; ++n) {
					GameMemory<0x18> copy = s.events[loans[n]];
					copy.u16(4) = loans[n];
					write_scratch<0x18>(s, kScratch + 4 + 0x18 * uint32_t(n), copy.bytes.data());
				}
				return std::nullopt;
			}
			const std::vector<ShareEntry> shares = k == 2 ? shares_on_sale(s) : shares_to_sell(s);
			s.globals.u32(kScratch) = uint32_t(shares.size());
			for (size_t n = 0; n < shares.size() && n < 20; ++n) {
				write_scratch<0xC>(s, kScratch + 4 + 0xC * uint32_t(n), shares[n].data());
			}
			return std::nullopt;
		};
	}
	// The build window's copiers (stdcall), the window's player [0x6DFC14]:
	// the menu (town, category): the 8 types in [SCRATCH], the category it
	// shows in [+0x20], the reputation's text in [+0x24]; whether it fits
	// (town, x, y, type): al in [SCRATCH], the window's type after in [+0x4],
	// the picture asked for (0x5928A0's picture and key) in [+0x10], [+0x14]
	// or −1; the marks (town, type): how many cells, the sums of their
	// indices, of their squares and cubes (mod 2^32), and whether there is a
	// map, in [SCRATCH .. +0x14].
	r[kBuildMenu] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const BuildMenu menu = build_menu(s, int(c.args[0]), s.globals.u32(global::kPlayer), int(c.args[1]));
		for (uint32_t k = 0; k < 8; ++k) {
			s.globals.s32(kScratch + 4 * k) = menu.types[k];
		}
		s.globals.s32(kScratch + 0x20) = menu.category;
		s.globals.u32(kScratch + 0x24) = menu.reputation_text ? 1 : 0;
		return std::nullopt;
	};
	r[kBuildFits] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		int type = int(c.args[3]);
		BuildGhost ghost{0xFF, 0xFFFFFFFF};
		const bool fits = fn_47b8c0(s, c.args[0], uint8_t(c.args[1]), uint8_t(c.args[2]), type, ghost);
		s.globals.u32(kScratch) = fits ? 1 : 0;
		s.globals.s32(kScratch + 4) = type;
		s.globals.u32(kScratch + 0x10) = ghost.key == 0xFFFFFFFF && ghost.catalog == 0xFF
			? 0xFFFFFFFF : uint32_t(int32_t(int8_t(ghost.catalog)));
		s.globals.u32(kScratch + 0x14) = ghost.key;
		return std::nullopt;
	};
	r[kBuildMarks] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		if (c.args[0] >= s.towns.size()) {
			c.skip = true;
			return std::nullopt;
		}
		const std::vector<uint8_t> map = build_marks(s, c.args[0], int(c.args[1]));
		uint32_t sums[4] = {0, 0, 0, 0};
		for (size_t i = 0; i < map.size(); ++i) {
			if (map[i]) {
				const uint32_t k = uint32_t(i);
				sums[0] += 1;
				sums[1] += k;
				sums[2] += k * k;
				sums[3] += k * k * k;
			}
		}
		for (uint32_t k = 0; k < 4; ++k) {
			s.globals.u32(kScratch + 4 * k) = sums[k];
		}
		s.globals.u32(kScratch + 0x10) = map.empty() ? 0 : 1;
		return std::nullopt;
	};
	// 0x4E8804 (thiscall on the event queue, run directly on its head): an event 0x18.
	r[0x4E8804] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const uint16_t head = s.globals.u16(0x6DD744);
		if (head >= s.events.size()) {
			c.skip = true;
			return std::nullopt;
		}
		return tick::events_towns_detail::fn_4e8804(s, head);
	};
	// The statistics window's copiers (stdcall), the window's merchant [0x6DFC14]: STATS_BALANCE (period, part) and
	// STATS_GRAPH (entry, period, part): the part `part` (0xC0 bytes) of the window's view in [SCRATCH], its FNV-1a
	// in [+0xC0] and its length in [+0xC4] (retouch_unchecked.py statistics_copiers; statistics_window.hpp). The
	// labels and titles are compared as the English exe's texts: without them, skipped.
	r[kStatsBalance] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const GameTexts* texts = probe_sailing() ? probe_sailing()->texts() : nullptr;
		if (!texts) {
			c.skip = true;
			return std::nullopt;
		}
		const StatisticsBalance b = statistics_balance(s, *texts, s.globals.u32(global::kPlayer), int(c.args[0]));
		std::vector<uint8_t> view(0x2A0, 0);
		put_texts(view, 0, b.texts.data(), b.texts.size(), 0x20);
		put_text(view, b.period, 0x40);
		write_view(s, view, c.args[1]);
		return std::nullopt;
	};
	r[kStatsGraph] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const GameTexts* texts = probe_sailing() ? probe_sailing()->texts() : nullptr;
		if (!texts) {
			c.skip = true;
			return std::nullopt;
		}
		const int entry = int(c.args[0]);
		const StatisticsGraph g = statistics_graph(s, s.globals.u32(global::kPlayer), entry, int(c.args[1]));
		std::vector<uint8_t> view;
		auto u32 = [&](uint32_t v) {
			for (int k = 0; k < 4; ++k) {
				view.push_back(uint8_t(v >> (8 * k)));
			}
		};
		u32(g.series);
		u32(g.points);
		u32(g.missing);
		view.push_back(uint8_t(g.texts.size()));
		view.push_back(uint8_t(g.texts.size() >> 8));
		for (size_t k = 0; k < 8; ++k) {
			view.push_back(k < g.kinds.size() ? g.kinds[k] : 0);
		}
		for (size_t k = 0; k < 7; ++k) {
			put_text(view, k < g.labels.size() ? texts->raw_at(g.labels[k]) : std::string(), 0x20);
		}
		const size_t at = view.size();
		view.resize(at + 0xE0, 0);
		put_texts(view, at, g.texts.data(), g.texts.size(), 0x20);
		for (uint32_t name : g.names) {
			put_text(view, name ? texts->raw_at(name) : std::string(), 0x20);
		}
		// The title: none when the merchant is out (the game returns before it).
		put_text(view, g.series ? statistics_title(*texts, entry) : std::string(), 0x40);
		for (uint32_t k = 0; k < g.series && k < 4; ++k) {
			for (uint32_t p = 0; p < g.points; ++p) {
				uint32_t bits = 0;
				const float v = p < g.values[k].size() ? g.values[k][p] : 0.0f;
				std::memcpy(&bits, &v, 4);
				u32(bits);
			}
		}
		write_view(s, view, c.args[2]);
		return std::nullopt;
	};
	// STATS_COMPETITION (entry, period, merchants, part): 0x4A19C0 with the window's four merchants (a byte each);
	// its view as STATS_GRAPH's, the buttons' texts the merchants' names, then the period after the call and each
	// series' 0x208 bytes (colour, merchant, points).
	r[kStatsCompetition] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const GameTexts* texts = probe_sailing() ? probe_sailing()->texts() : nullptr;
		if (!texts) {
			c.skip = true;
			return std::nullopt;
		}
		const int entry = int(c.args[0]);
		const int period = int(c.args[1]);
		const StatisticsGraph g = statistics_competition(s, *texts, unpack_slots(c.args[2]), entry, period);
		write_view(s, graph_view(*texts, g, entry, period, nullptr), c.args[3]);
		return std::nullopt;
	};
	// STATS_TOWNS (entry, period, wares, towns, single, part): 0x4A3FA0 with the window's choices; its view as the
	// competition's, with the button [+0x214]'s text and the window's four towns after the call (entry 0 may change
	// the first). The towns' names are the save's by number (the table 0x6DDA00).
	r[kStatsTowns] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const GameTexts* texts = probe_sailing() ? probe_sailing()->texts() : nullptr;
		if (!texts) {
			c.skip = true;
			return std::nullopt;
		}
		const int entry = int(c.args[0]);
		const int period = int(c.args[1]);
		StatisticsTownChoices choices = unpack_choices(c.args[2], c.args[3], c.args[4]);
		const StatisticsGraph g = statistics_towns(s, *texts, s.opaque.town_names, choices, entry, period);
		write_view(s, graph_view(*texts, g, entry, period, &choices), c.args[5]);
		return std::nullopt;
	};
	// STATS_RESIDENTS (part): 0x4A3BB0; its view: each row's town and text (0x3C), the total, the title.
	r[kStatsResidents] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const GameTexts* texts = probe_sailing() ? probe_sailing()->texts() : nullptr;
		if (!texts) {
			c.skip = true;
			return std::nullopt;
		}
		const StatisticsResidents r = statistics_residents(s, *texts, s.opaque.town_names);
		std::vector<uint8_t> view;
		auto u32 = [&](uint32_t v) {
			for (int k = 0; k < 4; ++k) {
				view.push_back(uint8_t(v >> (8 * k)));
			}
		};
		for (size_t k = 0; k < r.towns.size(); ++k) {
			u32(r.towns[k]);
			put_text(view, r.texts[k], 0x3C);
		}
		u32(uint32_t(r.total));
		put_text(view, statistics_title(*texts, kStatisticsResidents), 0x40);
		write_view(s, view, c.args[0]);
		return std::nullopt;
	};
	// STATS_CHOICE (entry, slot, wares, towns, merchants, single, sort, part): the list of a town's (7..10) or a
	// merchant's (16..18) button (0x49E850) and its sorts; its view: the list's mode and count, the extra row, the
	// sort's kind, where the choice goes (the slot's byte), the call's result (1), the towns or merchants and their
	// keys.
	r[kStatsChoice] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const GameTexts* texts = probe_sailing() ? probe_sailing()->texts() : nullptr;
		if (!texts) {
			c.skip = true;
			return std::nullopt;
		}
		const int entry = int(c.args[0]);
		const int slot = int(c.args[1]);
		const bool towns = entry >= kStatisticsTownResidents && entry <= kStatisticsTownStock;
		const bool wares = entry >= kStatisticsExportBalance && entry <= kStatisticsTownDataStock;
		StatisticsChoice choice = towns || wares
			? statistics_town_choice(s, unpack_choices(c.args[2], c.args[3], c.args[5]), slot, entry)
			: statistics_competition_choice(s, unpack_slots(c.args[4]), slot, entry);
		if (c.args[6] >= 1) {
			sort_choice_by_name(s, *texts, s.opaque.town_names, choice);
		}
		if (c.args[6] == 2) {
			sort_choice_by_key(choice);
		}
		std::vector<uint8_t> view;
		auto put = [&](uint32_t v, int bytes) {
			for (int k = 0; k < bytes; ++k) {
				view.push_back(uint8_t(v >> (8 * k)));
			}
		};
		put(uint32_t(choice.mode), 2);
		put(uint32_t(choice.merchants.size()), 2);
		put(choice.none ? 1 : 0, 4);
		put(uint32_t(choice.kind), 4);
		put((wares ? 0xAC : towns ? 0xB0 : 0xB4) + uint32_t(slot), 4);
		put(1, 1);
		for (uint8_t m : choice.merchants) {
			view.push_back(m);
		}
		for (int32_t key : choice.keys) {
			put(uint32_t(key), 4);
		}
		write_view(s, view, c.args[7]);
		return std::nullopt;
	};
	// STATS_WARES (sort, part): the comparison's ware button's list (and its sort by name); its view: the mode (0),
	// the count, the extra row (none), the wares.
	r[kStatsWares] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const GameTexts* texts = probe_sailing() ? probe_sailing()->texts() : nullptr;
		if (!texts) {
			c.skip = true;
			return std::nullopt;
		}
		StatisticsChoice choice = statistics_ware_choice();
		if (c.args[0] >= 1) {
			sort_choice_by_name(s, *texts, s.opaque.town_names, choice);
		}
		std::vector<uint8_t> view = {uint8_t(choice.mode), 0, uint8_t(choice.merchants.size()), 0, 0, 0, 0, 0};
		view.insert(view.end(), choice.merchants.begin(), choice.merchants.end());
		write_view(s, view, c.args[1]);
		return std::nullopt;
	};
	// CITIZEN_STATS (town, part): the citizens' window (0x40B570, player_actions.hpp citizen_stats); its view, the
	// window's [+0xA4..+0xBF] (the moods, the residents, the arrows) and [+0x1AC] (the shortage).
	r[kCitizenStats] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const CitizenStats cs = citizen_stats(s, c.args[0]);
		std::vector<uint8_t> view;
		for (size_t k = 0; k < 4; ++k) {
			const uint16_t m = k < 3 ? uint16_t(cs.mood[k]) : 0;
			view.push_back(uint8_t(m));
			view.push_back(uint8_t(m >> 8));
		}
		for (uint32_t r : cs.residents) {
			for (int k = 0; k < 4; ++k) {
				view.push_back(uint8_t(r >> (8 * k)));
			}
		}
		view.insert(view.end(), cs.arrow.begin(), cs.arrow.end());
		view.push_back(0);
		view.push_back(cs.shortage ? 1 : 0);
		write_view(s, view, c.args[1]);
		return std::nullopt;
	};
	// 0x535300: thiscall on the towns' history 0x6DE588 (town): the town's situation as the local player knows it
	// (player_actions.hpp town_situation_known), what the sea map's symbols show.
	r[0x535300] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return town_situation_known(s, c.args[0]);
	};
	// STATS_TOWN_LIST (sort, part): the town data's town button's list (0x49FB41, and its sort by name 0x471FF0); its
	// view as STATS_WARES's, with the keys' pointer (null) after the extra row and where the choice goes ([+0xBD]).
	r[kStatsTownList] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const GameTexts* texts = probe_sailing() ? probe_sailing()->texts() : nullptr;
		if (!texts) {
			c.skip = true;
			return std::nullopt;
		}
		StatisticsChoice choice = statistics_town_list(s);
		if (c.args[0] >= 1) {
			sort_choice_by_name(s, *texts, s.opaque.town_names, choice);
		}
		const uint32_t count = uint32_t(choice.merchants.size());
		std::vector<uint8_t> view = {uint8_t(choice.mode), 0, uint8_t(count), uint8_t(count >> 8), uint8_t(choice.none), 0,
			0, 0, 0, 0, 0, 0, 0xBD, 0, 0, 0};
		view.insert(view.end(), choice.merchants.begin(), choice.merchants.end());
		write_view(s, view, c.args[1]);
		return std::nullopt;
	};
	// STATS_SLOTS (wares, towns, merchants, part): what the window fixes and picks when it opens (0x49D380); its view,
	// the wares, the towns and the merchants.
	r[kStatsSlots] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		StatisticsTownChoices choices = unpack_choices(c.args[0], c.args[1], 0);
		StatisticsMerchants merchants = unpack_slots(c.args[2]);
		statistics_default_towns(s, choices);
		statistics_default_merchants(s, merchants);
		std::vector<uint8_t> view(choices.wares.begin(), choices.wares.end());
		view.insert(view.end(), choices.towns.begin(), choices.towns.end());
		view.insert(view.end(), merchants.begin(), merchants.end());
		write_view(s, view, c.args[3]);
		return std::nullopt;
	};
	// STATS_SUPPLY (town, part): Suministro's draw 0x4A0F10; its view, the window's three arrays of 20 (the office's
	// stock [+0x253C], the town's [+0x24EC], the price [+0x258C]).
	r[kStatsSupply] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const std::vector<SupplyRow> rows = statistics_supply(s, s.globals.u32(global::kPlayer), c.args[0]);
		if (rows.empty()) {
			c.skip = true;  // The game reads through a null office.
			return std::nullopt;
		}
		std::vector<uint8_t> view(0xF0, 0);
		for (const SupplyRow& row : rows) {
			const int32_t values[3] = {row.office, row.town, row.price};
			for (size_t k = 0; k < 3; ++k) {
				std::memcpy(view.data() + 0x50 * k + 4 * size_t(row.ware), &values[k], 4);
			}
		}
		write_view(s, view, c.args[1]);
		return std::nullopt;
	};
}

}  // namespace p3::probe
