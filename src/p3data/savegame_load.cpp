#include "p3data/savegame_load.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace p3::data {

namespace {

constexpr uint32_t kShipNameCount = 0x6DDB78;  // u16 [+0xD8] of the names object
constexpr uint32_t kTradeRoutesFree = 0x6DD728;  // u16 [+0x0] of 0x6DD728
constexpr uint32_t kTradeRouteCount = 0x6DD72A;  // u16 [+0x2]

// 0x6C0CB4: the name of a ship without one, by its type.
const char* const kTypeName[4] = {"snaikka", "crayer", "cog", "hulk"};

// 0x5131B0: the names of the original game's tutorial ships (indexes
// 0x12C..0x12F, after the 300 ship names) and the name each one becomes.
struct Special {
	uint16_t index;
	const char* name;  // Latin-1
	uint16_t becomes;
};
const Special kSpecial[] = {{0x12C, "Adler der See", 2}, {0x12C, "Altes Fass", 9}, {0x12C, "Willeharm", 0x124},
	{0x12C, "Adler der See", 2}, {0x12C, "Morgengru\xDF", 0xA3}, {0x12D, "Sturmwind", 0x107},
	{0x12D, "Windgurke", 0x125}, {0x12D, "Nepumuk", 2}, {0x12E, "Klabauter", 0x88}, {0x12E, "Seepott", 0xE1},
	{0x12F, "Susanna", 0x108}};

// The name of [+0x160] (32 bytes; the game would read past them without a 0).
std::string ship_name(const ShipMemory& m) {
	const char* p = reinterpret_cast<const char*>(m.bytes.data() + 0x160);
	return std::string(p, strnlen(p, 32));
}

// strcpy into [+0x160] (the bytes after the 0 stay).
void set_ship_name(ShipMemory& m, const std::string& name) {
	const size_t n = std::min<size_t>(name.size() + 1, 32);
	std::memcpy(m.bytes.data() + 0x160, name.c_str(), n);
}

// 0x5131B0 (on the names object): the ship's name index [+0x15E] and name.
// With a version (its low byte not 0, as in every save p3vita reads), an
// index of the game's names takes its name; one of the tutorial ships of the
// original game, if the name is its own, the index of that name; any other
// 0x7FFF − type and the name of its type. Then the name is looked for: in
// those cases it is always the one of its index, and the search after (in
// the game's names and in the ship names of the save, `custom`, which
// 0x512D40 makes longer) is only reached by a version 0.
void register_ship_name(const SaveGame& save, ShipMemory& m, std::vector<std::string>& custom, int version) {
	const std::vector<std::string>& base = save.ship_names;
	const size_t names = std::min<size_t>(save.globals.u16(kShipNameCount), base.size());
	const uint8_t type = m.u8(0xE) & 3;
	uint16_t& index = m.u16(0x15E);
	if (uint8_t(version) != 0) {
		if (index >= names) {
			const std::string name = ship_name(m);
			bool found = false;
			for (const Special& s : kSpecial) {
				if (s.index == index && name == s.name) {
					index = s.becomes;
					found = true;
					break;
				}
			}
			if (!found) {
				index = uint16_t(0x7FFF - type);
			}
		}
		set_ship_name(m, index < names ? base[index] : std::string(kTypeName[type]));
	}
	const std::string name = ship_name(m);
	if (index < names) {
		if (name == base[index]) {
			return;
		}
	} else if (index < names + custom.size()) {
		if (name == custom[index - names]) {
			return;
		}
	} else if (name == kTypeName[type]) {
		index = uint16_t(0x7FFF - type);
		return;
	}
	for (size_t k = 0; k < names; ++k) {
		if (name == base[k]) {
			index = uint16_t(k);
			return;
		}
	}
	for (size_t k = 0; k < custom.size(); ++k) {
		if (name == custom[k]) {
			index = uint16_t(names + k);
			return;
		}
	}
	if (strnlen(reinterpret_cast<const char*>(m.bytes.data() + 0x160), 32) < 32) {
		// 0x512D40: a new ship name of the save (not checked with the oracle:
		// only a version 0 gets here).
		custom.push_back(name);
		index = uint16_t(names + custom.size() - 1);
		return;
	}
	index = uint16_t(0x7FFF - type);
	set_ship_name(m, kTypeName[type]);
}

// 0x4D60B0 (on 0x6DD728): the trade routes neither in the free list nor
// used by a ship ([+0x132] of each, and the chain of its next routes [+0x0]
// until it comes back to the first) go to the free list.
void check_trade_routes(SaveGame& save) {
	GlobalMemory& g = save.globals;
	std::vector<GameMemory<0xDC>>& routes = save.opaque.trade_routes;
	const uint32_t count = std::min<uint32_t>(g.u16(kTradeRouteCount), uint32_t(routes.size()));
	std::vector<uint8_t> used(count, 0);
	for (uint32_t i = g.u16(kTradeRoutesFree); i < count; i = routes[i].u16(0)) {
		used[i] = 1;
	}
	for (size_t k = 0; k < save.ships.size() && k < g.u16(global::kShipCount); ++k) {
		const uint32_t first = save.ships[k].memory.u16(0x132);
		for (uint32_t i = first; i < count;) {
			used[i] = 1;
			i = routes[i].u16(0);
			if (i == first) {
				i = 0xFFFF;
			}
		}
	}
	for (uint32_t k = 0; k < count; ++k) {
		if (!used[k]) {
			routes[k].u16(0) = g.u16(kTradeRoutesFree);
			g.u16(kTradeRoutesFree) = uint16_t(k);
		}
	}
}

}  // namespace

void finish_load(SaveGame& save) {
	// 0x549F40 (0x54A1CD): the ships of each merchant, from the last one, and
	// of each pirate; the ship names of the save only last until 0x5121C0 at
	// the end of the load undoes them.
	std::vector<std::string> custom = save.opaque.save_ship_names;
	auto names_of = [&](uint32_t first) {
		for (uint32_t i = first; i < save.ships.size() && i < save.globals.u16(global::kShipCount);) {
			ShipMemory& m = save.ships[i].memory;
			i = m.u16(0x4);
			register_ship_name(save, m, custom, save.version);
		}
	};
	for (size_t k = save.merchants.size(); k-- > 0;) {
		names_of(save.merchants[k].memory.u16(0xE));
	}
	for (const std::optional<GameMemory<0x18>>& pirate : save.pirates) {
		if (pirate) {
			names_of(pirate->u16(0xC));
		}
	}
	check_trade_routes(save);
}

}  // namespace p3::data
