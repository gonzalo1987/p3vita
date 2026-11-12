#include "p3data/letters.hpp"

#include <algorithm>
#include <cstdlib>

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/mail.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kTownIds = 0x6DE4B8;  // u8 per town index: its number (scripts/StadtDaten.ini).
constexpr uint32_t kShipNameCount = 0x6DDB78;  // u16 [+0xD8] of the names object; [+0xDA] after it.
constexpr uint32_t kShipCount = 0x6DD894;  // u16 [+0xF4] of the ships object 0x6DD7A0: the ships.

// The types of the composer's default case (0x4865E5): no letter text.
constexpr uint8_t kNoText[] = {0x06, 0x08, 0x09, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x1C, 0x23, 0x35, 0x38, 0x39, 0x3A,
	0x3C, 0x40, 0x44, 0x45, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5F, 0x71, 0x72};

// The key of a text only the Spanish code uses (game_text_addresses.inc).
constexpr uint32_t kSpanishKey = 0x40000000;

// The Spanish composer's case 0x03 (0x47F7AC): the building's gender, 1..3,
// by its type (the byte table 0x486960 into the jump table 0x486954; 3 past
// type 0x38). It picks one of three words for «new» (the German original's
// neuer, neue, neues); the Spanish exe has «Nuevo » in all three.
constexpr uint8_t kSpanishGender[0x39] = {1, 2, 3, 2, 2, 3, 1, 2, 3, 3, 2, 1, 2, 2, 2, 2, 1, 2, 2, 3, 3, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 1, 1};

std::string num(uint32_t v) {
	return std::to_string(v);
}

// The town table 0x6DDA00 (by number, the game's language) read with
// `town`: the composer gives it the town's index, a game bug (the same as in
// the letters' buffers: tick::letter_town_name); fixed with fix_game_bugs().
std::string table_town(const SaveGame& save, uint32_t town, const std::vector<std::string>& table) {
	if (fix_game_bugs() && town >= save.globals.u32(global::kWorldTownCount)) {
		return std::string();  // Not a town: no name (the game reads past the table).
	}
	const uint32_t named = fix_game_bugs() ? save.globals.u8(kTownIds + town) : town;
	return named < table.size() ? table[named] : std::string();
}
std::string table_town(const SaveGame& save, uint32_t town) {
	return table_town(save, town, fix_game_bugs() ? save.opaque.town_names : save.opaque.town_name_table);
}

// The composer's state: the text so far and the letter.
struct Composer {
	const SaveGame& save;
	const GameTexts& t;
	const GameMemory<0x10>& e;
	uint16_t entry;
	std::string out;

	const std::string& text(uint32_t english) const { return t.raw_at(english); }
	// A text of the Spanish code by its Spanish address (empty with the English exe).
	const std::string& spanish_text(uint32_t spanish) const { return t.raw_at(kSpanishKey | spanish); }
	// Whether the player's exe is the Spanish one: its composer differs from
	// the English one in some cases, not only in its texts.
	bool spanish() const { return t.spanish(); }
	// An element of a table of texts ([k * 4 + base] in the game).
	const std::string& table(uint32_t base, uint32_t k) const { return t.raw_at(base + 4 * k); }
	std::string last;  // The game's local buffer: the last sprintf (some cases append it again).
	void add(const std::string& s) { out += s; }
	void add(uint32_t english) { out += t.raw_at(english); }
	void format(uint32_t english, const std::vector<std::string>& args) { format(t.raw_at(english), args); }
	void format(const std::string& text, const std::vector<std::string>& args) {
		last = GameTexts::sprintf_raw(text, args);
		out += last;
	}
	// [i * 4 + 0x6DDA00]: i is a town's index (see table_town).
	std::string town(uint32_t i) const { return table_town(save, i); }
	// [i * 4 + 0x6DD8C0], the town's tavern, by number like the names (the same bug).
	std::string tavern(uint32_t i) const { return table_town(save, i, save.opaque.town_taverns); }
	std::string first(uint32_t i) const { return game_first_name(save, i); }  // 0x512A60
	std::string family(uint32_t i) const { return game_family_name(save, i); }  // 0x512AE0
	// 0x512B20 (on the names object 0x6DDAA0): a name of the game's ship
	// names ([+0xD8] of them), then of the players' (up to [+0xDA]: none
	// after a load, 0x5121C0 undoes them; the operations 0x2D and 0x2E add
	// them, SaveGame::player_ship_names), and past them a ship type's word by
	// (~n) & 3 (0x6C0CB4: «snaikka», «crayer», «cog», «hulk»).
	std::string list_ship_name(uint32_t n) const {
		const uint32_t index = n & 0xFFFF;
		const uint32_t base = save.globals.u16(kShipNameCount);
		if (index < base) {
			return index < save.ship_names.size() ? save.ship_names[index] : std::string();
		}
		if (index < save.globals.u16(kShipNameCount + 2)) {
			return index - base < save.player_ship_names.size() ? save.player_ship_names[index - base] : std::string();
		}
		return t.raw_at(0x6C0CB4 + 4 * (~n & 3));
	}
	// The name in a ship ([+0x160]); the game doesn't check the index.
	std::string ship_name(uint32_t ship) const {
		if (ship >= save.ships.size()) {
			return std::string();
		}
		const ShipMemory& m = save.ships[ship].memory;
		std::string name;
		for (size_t k = 0x160; k < 0x180 && m.u8(k) != 0; ++k) {
			name += char(m.u8(k));
		}
		return name;
	}
	// A captain or manager's 16 bytes (*0x6DD7A0 + 16 i); the game doesn't check the index.
	uint8_t captain(uint32_t i, size_t offset) const {
		return i < save.captains.size() ? save.captains[i].u8(offset) : 0;
	}
	// The list row's date (0x47C520): «DD.MM.YYYY».
	std::string row_date() const {
		const uint32_t day = e.u8(0);
		const uint32_t month = (e.u8(1) & 0xFu) + 1;
		const uint32_t year = e.u16(2);
		std::string s;
		for (uint32_t d : {day / 10 % 10, day % 10}) {
			s += char('0' + d);
		}
		s += '.';
		for (uint32_t d : {month / 10 % 10, month % 10}) {
			s += char('0' + d);
		}
		s += '.';
		for (uint32_t d : {year / 1000 % 10, year / 100 % 10, year / 10 % 10, year % 10}) {
			s += char('0' + d);
		}
		return s;
	}
	// The letter's buffer ([+0x8], [+0xC] bytes), 0 bytes included.
	void add_buffer() {
		if (entry < save.mail_buffers.size()) {
			const std::vector<uint8_t>& b = save.mail_buffers[entry];
			out.append(b.begin(), b.end());
		}
	}
	// The first letter of type `type` in the chain of the local player
	// (0x6DFC14; its head [+0xA] by 0x5303C0), or nullptr.
	const GameMemory<0x10>* first_of_type(uint8_t type) const {
		if (save.player < 0 || size_t(save.player) >= save.merchants.size()) {
			return nullptr;
		}
		size_t seen = 0;
		for (uint32_t i = save.merchants[size_t(save.player)].memory.u16(0xA); i < save.mail.size() && seen < save.mail.size();
			 i = save.mail[i].u16(6), ++seen) {
			if (save.mail[i].u8(4) == type) {
				return &save.mail[i];
			}
		}
		return nullptr;
	}
	// The local player (0x6DFC14) or merchant m's memory (0x5303C0), nullptr if none.
	const MerchantMemory* merchant(int m) const {
		return m >= 0 && size_t(m) < save.merchants.size() ? &save.merchants[size_t(m)].memory : nullptr;
	}
	// 0x4F6420: a merchant's first name, from the women's list unless [+0x1A]
	// (the game also sets that bit 0x80 of [+0x1C]: letter_viewed).
	std::string merchant_first(const MerchantMemory* m) const {
		if (!m) {
			return std::string();
		}
		return first(m->u8(0x1A) ? (m->u8(0x1C) & 0x7Fu) : (m->u8(0x1C) | 0x80u));
	}
	std::string merchant_family(const MerchantMemory* m) const { return m ? family(m->u8(0x1D)) : std::string(); }  // 0x4F6450
	// The auctions' letters' start (0x2C..0x2E): «\cAuction in », the local
	// player's title by its sex ([+0x1A] & 1) and its names ([+0x1C] as it is).
	void auction_header() {
		const MerchantMemory* m = merchant(save.player);
		add(0x6B5E80);
		add(m && (m->u8(0x1A) & 1) ? 0x6B5E84 : 0x6B5E88);
		add(m ? first(m->u8(0x1C)) : std::string());
		add(" ");
		add(merchant_family(m));
	}
	// 0x636620: the prince's goods list from the letter's [+0x8..+0xC]:
	// grain [+0xC] in loads, wares 15 [+0xB] in barrels, 11 [+0xA] in loads,
	// 12 [+0x9] and 3 [+0x8] in barrels; each new one before the list so far,
	// joined by « and » the first time and «, » after (CString::Format with
	// a format for each ware and count).
	std::string goods_list() const {
		struct Item {
			uint32_t field;
			uint32_t ware;
			uint32_t formats[3];  // with 0, 1 and 2+ items before (the first has only the first)
		};
		static constexpr Item kItems[5] = {
			{0xC, 0, {0x6C78FC, 0, 0}},
			{0xB, 15, {0x6C7904, 0x6C790C, 0}},
			{0xA, 11, {0x6C7918, 0x6C7920, 0x6C792C}},
			{0x9, 12, {0x6C7938, 0x6C7940, 0x6C794C}},
			{0x8, 3, {0x6C7958, 0x6C7960, 0x6C796C}},
		};
		std::string list;
		uint32_t count = 0;
		for (const Item& item : kItems) {
			const uint8_t amount = e.u8(item.field);
			if (amount == 0) {
				continue;
			}
			const uint32_t format = item.formats[count < 2 ? count : 2];
			if (format == 0) {
				++count;  // The game counts it but writes nothing.
				continue;
			}
			std::vector<std::string> args = {num(amount), ware(item.ware)};
			if (count > 0) {
				args.push_back(t.raw_at(count == 1 ? 0x6A70B0 : 0x6A70B4));
				args.push_back(list);
			}
			list = GameTexts::sprintf_raw(t.raw_at(format), args);
			++count;
		}
		return list;
	}
	// A month's short name (0x6C0D00 + 4 m: «Jan» for 1; 0 isn't one).
	const std::string& month(uint32_t m) const { return t.raw_at(0x6C0D00 + 4 * m); }
	// A ware's name (0x6C0BB0: «grain»...).
	const std::string& ware(uint32_t w) const { return t.raw_at(0x6C0BB0 + 4 * w); }
	uint32_t ship_count() const { return save.globals.u16(global::kShips + 0xF4); }  // [0x6DD894]
};

// 0x672D92: the days of each month (0 for 0; the game doesn't check the index).
constexpr uint16_t kMonthDays[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// 0x672D78: the days before each month (u16), and past it the days of each
// month (the game reads it by a month's 4 bits).
constexpr uint16_t kDaysBefore[16] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365, 0, 31, 28};

// 0x672C14: a byte per ware for 0x1A's two texts (the same in both exes).
constexpr uint8_t kWareText[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1};

}  // namespace

void letter_viewed(SaveGame& save, uint16_t entry) {
	if (entry >= save.mail.size()) {
		return;
	}
	const uint8_t type = save.mail[entry].u8(4);
	if ((type == 0x21 || type == 0x22) && save.player >= 0 && size_t(save.player) < save.merchants.size()) {
		// 0x4F6420 on a woman player: the women's list's bit in [+0x1C].
		MerchantMemory& m = save.merchants[size_t(save.player)].memory;
		if (m.u8(0x1A) == 0) {
			m.u8(0x1C) |= 0x80;
		}
	}
	if (type != 0x12 && type != 0x26) {
		return;
	}
	GameMemory<0x10>& e = save.mail[entry];
	const uint16_t m = e.u16(0xC);
	if (m == 0) {
		e.u16(0xC) = 1;
		e.u16(0xE) = 1;
	} else if (e.u16(0xE) == (m < 13 ? kMonthDays[m] : 0u) + 1) {
		e.u16(0xE) = 1;
		++e.u16(0xC);
	}
}

bool letter_has_text(uint8_t type) {
	return type <= 0x85 && std::find(std::begin(kNoText), std::end(kNoText), type) == std::end(kNoText);
}

bool letter_type_rewritten(uint8_t type) {
	switch (type) {
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x04:
	case 0x05:
	case 0x07:
	case 0x0A:
	case 0x0B:
	case 0x11:
	case 0x12:
	case 0x13:
	case 0x14:
	case 0x15:
	case 0x16:
	case 0x17:
	case 0x18:
	case 0x19:
	case 0x1A:
	case 0x1B:
	case 0x1D:
	case 0x20:
	case 0x21:
	case 0x22:
	case 0x24:
	case 0x25:
	case 0x26:
	case 0x27:
	case 0x28:
	case 0x29:
	case 0x2A:
	case 0x2B:
	case 0x2C:
	case 0x2D:
	case 0x2E:
	case 0x2F:
	case 0x30:
	case 0x31:
	case 0x32:
	case 0x33:
	case 0x34:
	case 0x36:
	case 0x37:
	case 0x3B:
	case 0x3D:
	case 0x3E:
	case 0x3F:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x46:
	case 0x47:
	case 0x48:
	case 0x49:
	case 0x4A:
	case 0x4B:
	case 0x4C:
	case 0x4D:
	case 0x4E:
	case 0x4F:
	case 0x50:
	case 0x51:
	case 0x52:
	case 0x5A:
	case 0x5B:
	case 0x5C:
	case 0x5D:
	case 0x5E:
	case 0x60:
	case 0x61:
	case 0x62:
	case 0x63:
	case 0x64:
	case 0x65:
	case 0x66:
	case 0x67:
	case 0x68:
	case 0x69:
	case 0x6A:
	case 0x6B:
	case 0x6C:
	case 0x6D:
	case 0x6E:
	case 0x6F:
	case 0x70:
	case 0x73:
	case 0x74:
	case 0x75:
	case 0x76:
	case 0x77:
	case 0x78:
	case 0x79:
	case 0x7A:
	case 0x7B:
	case 0x7C:
	case 0x7D:
	case 0x7E:
	case 0x7F:
	case 0x80:
	case 0x81:
	case 0x82:
	case 0x83:
	case 0x84:
	case 0x85:
		return true;
	default:
		return !letter_has_text(type);
	}
}

std::optional<std::string> compose_letter(const SaveGame& save, const GameTexts& texts, uint16_t entry) {
	if (entry >= save.mail.size()) {
		return std::nullopt;
	}
	const GameMemory<0x10>& e = save.mail[entry];
	const uint8_t type = e.u8(4);
	if (!letter_has_text(type) || !letter_type_rewritten(type)) {
		return std::nullopt;
	}
	Composer c{save, texts, e, entry, {}, {}};
	// The header (0x47F509..0x47F5D6): «\n\r\f3_», the town («%s,  »; only
	// for an index below the number of towns) and the date («%u. %u. %u\n\n\l»,
	// the month's low 4 bits + 1). Classes 0 and 3 in font 1.
	c.add(0x6B9CAC);
	if (e.u8(5) < save.globals.u32(global::kWorldTownCount)) {
		c.format(0x6B9CB4, {table_town(save, e.u8(5))});
	}
	c.format(0x6B9CBC, {num(e.u8(0)), num((e.u8(1) & 0xF) + 1u), num(e.u16(2))});
	const uint8_t letter_class_ = letter_class(type);
	if ((letter_class_ == 0 || letter_class_ == 3) && c.out.size() > 5) {
		c.out[5] = '1';
	}
	switch (type) {
	case 0x63:  // The expedition's report (0x48115F; and the window's width).
	case 0x29:  // The battle report (0x481167): font 1.
		if (c.out.size() > 5) {
			c.out[5] = '1';
		}
		[[fallthrough]];
	case 0x47:  // The elections, the impounding (0x48116F): the buffer's bytes as they are.
	case 0x83:
		// GAME BUG (0x481172): with a buffer of 0xF3C bytes or more the game
		// allocates the buffer's size but copies to that block + the text's
		// length + 0x64 and later frees that pointer (a write past the block
		// and a bad free). p3vita just appends.
		c.add_buffer();
		break;
	case 0x00:  // A ship launched (0x47F5F1): the town, the ship's type («a snaikka», 0x6C0E80).
		c.format(0x6B2E90, {c.town(e.u8(5)), c.table(0x6C0E80, e.u16(0xA))});
		break;
	case 0x01:  // A repair (0x47F625): the town, the ship's type, its condition, '%', the bill.
		c.format(0x6B2F20, {c.town(e.u8(5)), c.table(0x6C0E80, e.u16(8)), num(e.u16(0xA)), "%",
			std::to_string(e.s32(0xC))});
		break;
	case 0x02:  // No repair (0x47F666): the type («The snaikka», 0x6C0E38), the name (0x512B20 of [+0x8]).
		// The sender (operation 0x03, 0x52AE8A) puts in [+0x8] the ship's name
		// number [+0x15E] and in [+0xA] its type, 4 for a ship in a convoy:
		// «The convoy» in English; the Spanish table 0x6C1350 has no 5th text
		// (GAME BUG of the Spanish exe: its 5th element is the bytes «una », a
		// bad pointer; p3vita writes nothing there).
		if (e.s32(0xC) <= 0) {
			// GAME BUG (0x47F684): the town is pushed first and the name in the
			// ship [+0x8] after it, so «%s» of the shipyard's town gets the
			// name of the ship whose number is the name's number (another ship,
			// or past them whatever the memory holds; p3vita: nothing). Fixed
			// with fix_game_bugs(): the town.
			c.format(0x6B3048, {c.table(0x6C0E38, e.u16(0xA)), c.list_ship_name(e.u16(8)),
				fix_game_bugs() ? c.town(e.u8(5)) : c.ship_name(e.u16(8))});
		} else {
			c.format(0x6B304C, {c.table(0x6C0E38, e.u16(0xA)), c.list_ship_name(e.u16(8)), c.town(e.u8(5)),
				std::to_string(e.s32(0xC))});
		}
		break;
	case 0x03: {  // Works done (0x47F70C): the town, the row's date, «A new», the building (0x6A57C8).
		c.out[5] = '1';
		const uint16_t building = e.u16(8);
		if (c.spanish()) {
			// The Spanish code (0x47F7AC): the row's date, the town, «Nuevo » by
			// the building's gender, the building.
			const uint32_t gender = building < sizeof(kSpanishGender) ? kSpanishGender[building] : 3;
			c.format(c.spanish_text(0x6B3B94), {c.row_date(), c.town(e.u8(5)), c.spanish_text(0x6B3B94 + 4 * gender),
				c.table(0x6A57C8, building)});
		} else {
			c.format(0x6B30D8, {c.town(e.u8(5)), c.row_date(), c.text(0x6B30DC), c.table(0x6A57C8, building)});
		}
		break;
	}
	case 0x04: {  // A new captain (0x47F76F): his names, the town, the ship's name.
		c.out[5] = '1';
		const uint32_t captain = e.u16(8);
		c.format(0x6B317C, {c.first(c.captain(captain, 2)), c.family(c.captain(captain, 3)), c.town(e.u8(5)),
			c.ship_name(e.u16(0xA))});
		break;
	}
	case 0x05: {  // A captain retires (0x47F7F2): his names, the town, the ship's name.
		const uint32_t captain = e.u16(8);
		c.format(0x6B31E0, {c.first(c.captain(captain, 2)), c.family(c.captain(captain, 3)), c.town(e.u8(5)),
			c.ship_name(e.u16(0xA))});
		break;
	}
	case 0x07: {  // A convoy or ship back from its route (0x47F866).
		const uint16_t ship = e.u16(8);
		if (ship >= 0x8000) {
			c.format(0x6B33A0, {c.list_ship_name(ship & 0x7FFFu), c.town(e.u8(5))});
		} else {
			c.format(0x6B339C, {c.list_ship_name(ship), c.town(e.u8(5))});
		}
		const int32_t profit = e.s32(0xC);
		if (profit == 0) {
			c.format(0x6B33AC, {});
		} else if (profit > 0) {
			c.format(0x6B33A4, {std::to_string(profit)});
		} else {
			c.format(0x6B33A8, {std::to_string(-int64_t(profit))});
		}
		c.format(0x6B9CCC, {c.first(e.u8(0xB)), c.family(e.u8(0xA))});
		break;
	}
	case 0x0A: {  // A charge (0x47F980): the names, the reason.
		// The reason [+0xC] is the kind 5..8 of the merchant's task that sent
		// it (events_big.cpp, start_letter): 8 has its own text (0x6AE5C8), the
		// others index the table 0x6B4A0C, whose texts are 5..7 (0..4 fall on
		// the bytes before it and on the two texts of this letter).
		c.add(c.first(e.u16(8)));
		c.add(" ");
		c.add(c.family(e.u16(0xA)));
		c.add(0x6B4A18);
		const uint16_t reason = e.u16(0xC);
		c.add(reason == 8 ? c.text(0x6AE5C8) : c.table(0x6B4A0C, reason));
		c.add(0x6B4A1C);
		break;
	}
	case 0x0B: {  // A Hanse vote's result (0x47FA4C), in font 3.
		c.out[5] = '3';
		const uint16_t other = e.u16(8);
		if (other < save.globals.u32(global::kWorldTownCount)) {
			if (c.spanish()) {
				// The Spanish code (0x47FB1B): the town twice, then the other and the reason.
				c.format(0x6B70F4, {c.town(e.u8(5)), c.town(e.u8(5)), c.town(other), c.table(0x6ADD54, e.u8(0xA))});
			} else {
				c.format(0x6B70F4, {c.town(e.u8(5)), c.town(other), c.table(0x6ADD54, e.u8(0xA)), c.town(e.u8(5))});
			}
		} else {
			c.format(0x6ADDDC, {c.town(e.u8(5)), c.town(e.u8(5))});
		}
		// The result (0x6ADD10 by [+0xA] >> 7: even when [+0xC] <= [+0xE], odd
		// after; 2 and up name the town [+0x8]).
		const uint32_t kind = uint32_t(e.u16(0xA)) >> 7;
		const uint32_t yes = e.u16(0xC);
		const uint32_t no = e.u16(0xE);
		const bool rejected = yes <= no;
		const uint32_t k = rejected ? (kind & 0xFE) : ((kind & 0xFF) | 1);
		std::vector<std::string> args = rejected ? std::vector<std::string>{num(no), num(yes)}
												 : std::vector<std::string>{num(yes), num(no)};
		const std::string& result = c.table(0x6ADD10, k);
		if (k >= 2 && c.spanish()) {
			// The Spanish code (0x47FBFA, 0x47FC48): the town third when
			// rejected, second when carried (its texts' %$N$s).
			args.insert(rejected ? args.end() : args.begin() + 1, c.town(other));
		} else if (k >= 2) {
			// GAME BUG (0x47FB67, English): the town goes first also to the text
			// of k = 2 («The request for a blockade was defeated by %u votes to
			// %u.»), which has no %s: its first %u prints the address of the
			// town's name, the second the votes against, and the votes for are
			// lost. p3vita writes «?» for the address. Fixed with
			// fix_game_bugs(): the town only for a text with a %s (or %$N$s).
			if (result.find("%s") != std::string::npos || result.find("$s") != std::string::npos) {
				args.insert(args.begin(), c.town(other));
			} else if (!fix_game_bugs()) {
				args.insert(args.begin(), "?");
			}
		}
		c.format(result, args);
		break;
	}
	case 0x11: {
		// The weekly bill (0x47FB96), in font 1: the taxes [+0x8], then from
		// the first 0x0F and 0x10 of the local player's chain (the week's
		// other two letters) the offices' wages, the warehouses' rent, the
		// sailors' wages and the houses' rent with this letter's property
		// tax [+0xC]; each only if not 0.
		c.out[5] = '1';
		const GameMemory<0x10>* wages = c.first_of_type(0x0F);
		const GameMemory<0x10>* rents = c.first_of_type(0x10);
		if (e.u32(8) > 0) {
			c.format(0x6B39F0, {num(e.u32(8))});
		}
		if (wages && wages->u32(0xC) > 0) {
			c.format(0x6B38C0, {num(wages->u32(0xC))});
		}
		if (rents && rents->u32(0xC) > 0) {
			c.format(0x6B3950, {num(rents->u32(0xC))});
		}
		if (wages && wages->u32(8) > 0) {
			c.format(0x6B38BC, {num(wages->u32(8))});
		}
		if (rents && rents->u32(8) > 0) {
			c.format(0x6B39F4, {num(rents->u32(8)), num(e.u32(0xC))});
		}
		break;
	}
	case 0x12: {
		// A loan due (0x47FDB3): the amount, the date it was taken (day [+0xE],
		// month [+0xC], year [+0xA]), the town. The game first moves a day
		// past its month's end to the next month's 1st (and month 0 to 1.1.)
		// in the letter itself: letter_viewed does that to the save.
		uint32_t m = e.u16(0xC);
		uint32_t d = e.u16(0xE);
		if (m == 0) {
			m = d = 1;
		} else if (d == (m < 13 ? kMonthDays[m] : 0u) + 1) {
			d = 1;
			++m;
		}
		if (c.spanish()) {
			// The Spanish code (0x47FE9A): the town first, for its %$N$.
			c.format(0x6B3AA0, {c.town(e.u8(5)), num(e.u16(8)), num(d & 0xFFFF), num(m & 0xFFFF), num(e.u16(0xA))});
		} else {
			c.format(0x6B3AA0, {num(e.u16(8)), num(d & 0xFFFF), num(m & 0xFFFF), num(e.u16(0xA)), c.town(e.u8(5))});
		}
		c.add(0x6B3AA4);
		break;
	}
	case 0x13: {  // An impounding (0x47FE79) by [+0xA]: money, goods in the town, goods in a ship.
		const std::string town = c.town(e.u8(5));
		switch (e.u8(0xA)) {
		case 0:
			c.format(0x6B3D70, {town, num(e.u32(0xC))});
			break;
		case 1:
			c.format(0x6B3D74, {town, num(e.u32(0xC))});
			break;
		case 2:
			c.format(0x6B3D78, {town, c.list_ship_name(e.u16(8)), num(e.u32(0xC))});
			break;
		default:
			c.add(c.last);  // The game appends its buffer as it is: the header's date (no sender does it).
			break;
		}
		if (e.u8(0xB) == 0) {
			c.add(0x6B3D7C);  // «However, your debt could not be repaid by it.»
		}
		break;
	}
	case 0x14: {
		// A ship's order cancelled (0x47FF50): the day ([+0xD] >> 2), the month
		// and year ([+0xE] as year · 12 + month: «Jan» for 0), the type
		// ([+0xD] & 3, «a snaikka»), the town and, if [+0x8] >= 0, what is paid back.
		const uint32_t when = e.u16(0xE);
		const uint32_t day_type = e.u8(0xD);
		std::vector<std::string> args = {num(day_type >> 2), c.month(when % 12 + 1), num(when / 12),
			c.table(0x6C0E80, day_type & 3), c.town(e.u8(5))};
		if (e.s32(8) >= 0) {
			args.push_back(std::to_string(e.s32(8)));
			c.format(0x6B3E94, args);
		} else {
			c.format(0x6B3E98, args);
		}
		break;
	}
	case 0x15: {
		// A burglary (0x47FFF4) at the merchant of names [+0x9], [+0x8]: the
		// share of the gold [+0xE] and, with goods [+0xA] (a bit per ware) worth
		// [+0xC], their names.
		const std::string first = c.first(e.u8(9));
		const std::string family = c.family(e.u8(8));
		if (e.u16(0xA) != 0 && e.u16(0xC) != 0) {
			if (e.u16(0xE) == 0) {
				c.format(0x6B4180, {first, family, num(e.u16(0xC))});
			} else {
				c.format(0x6B4184, {first, family, num(e.u16(0xE)), num(e.u16(0xC))});
			}
			for (uint32_t w = 0; w < 16; ++w) {
				if (e.u16(0xA) & (1u << w)) {
					c.add(" ");
					c.add(c.ware(w));
					c.add(",");
				}
			}
			if (!c.out.empty()) {
				c.out.pop_back();  // The last comma (or, without a ware's bit, the text's last byte).
			}
		} else if (e.u16(0xE) == 0) {
			c.format(0x6B4178, {first, family});
		} else {
			c.format(0x6B417C, {first, family, num(e.u16(0xE))});
		}
		break;
	}
	case 0x16:  // A burglary that failed (0x480164): the names [+0x8], [+0xA].
		c.format(0x6B3F44, {c.first(e.u16(8)), c.family(e.u16(0xA))});
		break;
	case 0x17: {
		// A traveller (0x4801A1): from the town [+0x5], the ship [+0xE] (past
		// the ships, the first one's name), by the day and month of [+0xA]
		// (day & 0x1F, month >> 5), to the town [+0x8], paying [+0xC].
		const uint16_t ship = e.u16(0xE);
		c.format(0x6B41F8, {c.town(e.u8(5)), c.ship_name(ship < c.ship_count() ? ship : 0u), num(e.u16(0xA) & 0x1Fu),
			c.month(e.u16(0xA) >> 5), c.town(e.u16(8)), num(e.u16(0xC))});
		break;
	}
	case 0x18:  // The traveller's thanks (0x480221): one of 4 texts by [+0x8] & 3, the town [+0xC], [+0xA].
		c.format(c.table(0x6B43D0, e.u8(8) & 3u), {c.town(e.u16(0xC)), num(e.u16(0xA))});
		break;
	case 0x19: {
		// A charge at a merchant's task's start (0x480256; event 0x05 kind
		// [+0xA], start_letter): the names [+0x8], [+0x9], then the kind's
		// three texts (0x6B48B8 + 12 k, from 8 on 0x6AE5C8 + 12 (k - 8)): the
		// first, the second formatted by the kind (the town and ship of
		// [+0xB], [+0xC], [+0xD]) and the third, and a closing text.
		const uint32_t k = e.u8(0xA);
		const uint32_t base = k < 8 ? 0x6B48B8 + 12 * k : 0x6AE5C8 + 12 * (k - 8);
		c.add(c.first(e.u8(8)));
		c.add(" ");
		c.add(c.family(e.u8(9)));
		c.add(base);
		const std::string& middle = c.text(base + 4);
		switch (k) {
		case 0:  // A tavern of the town: «%s in the town of %s with lawless elements,».
			c.format(middle, {c.tavern(e.u8(0xB)), c.town(e.u8(0xB))});
			break;
		case 1:
			c.format(middle, {c.town(e.u8(0xB))});
			break;
		case 2:
		case 9:
		case 10:
		case 11:
		case 12:
			c.format(middle, {c.list_ship_name(e.u16(0xC))});
			break;
		case 3:
			c.format(middle, {c.town(e.u8(0xB)), c.first(e.u8(0xC)), c.family(e.u8(0xD))});
			break;
		case 4:
		case 8:
			c.format(middle, {});
			break;
		case 13:
		case 14:
		case 15:
			c.format(middle, {c.town(e.u8(0xB)), c.list_ship_name(e.u16(0xC))});
			break;
		default:
			// 5..7 (the sender writes 0x0A for them) and past 15 append the
			// game's buffer as it is (the header's date).
			c.add(c.last);
			break;
		}
		c.add(base + 8);
		c.add(0x6B48F4);
		break;
	}
	case 0x1A: {
		// A small transport order (0x480581): by the date [+0xE] (day & 0x1F,
		// then months after the letter's year: >> 5), meet the merchant of
		// names [+0x9], [+0x8] at the tavern of the town [+0x5] to carry [+0xC]
		// of the ware [+0xA] % 24 (two texts by the ware, 0x672C14).
		const uint32_t months = uint32_t(e.u16(0xE)) >> 5;
		const uint32_t ware = e.u16(0xA) % 24;
		const std::string day = num(e.u16(0xE) & 0x1Fu);
		const std::string year = num((e.u16(2) + months / 12) & 0xFFFF);
		const std::vector<std::string> rest = {c.first(e.u8(9)), c.family(e.u8(8)), c.tavern(e.u8(5)), c.town(e.u8(5)),
			num(e.u16(0xC)), c.ware(ware)};
		std::vector<std::string> args;
		if (c.spanish()) {
			// The Spanish code (0x480668): the day, then the month's name (0x6C0F8C, «enero»).
			args = {day, c.spanish_text(0x6C0F8C + 4 * (months % 12)), year};
		} else {
			args = {num(months % 12 + 1), day, year};
		}
		args.insert(args.end(), rest.begin(), rest.end());
		c.format(kWareText[ware] ? 0x6B4BB0 : 0x6B4BB4, args);
		break;
	}
	case 0x1B: {
		// A ship sunk in a storm (0x480694): its type ([+0x8] & 3, «The snaikka»),
		// its name ([+0xC] & 0x3FFF), the sailors [+0xF], and its captain's
		// names ([+0xA] >> 11, [+0xE]; 0xFF: none). Two pairs of texts by
		// [+0x8] & 1. After it, with the sea map's object (*0x6CBDD8) and
		// [+0x8] > 3, the game marks the wreck there (0x44F490): no text.
		const bool odd = e.u8(8) & 1;
		const std::vector<std::string> ship = {c.table(0x6C0E38, e.u8(8) & 3u), c.list_ship_name(e.u16(0xC) & 0x3FFFu),
			num(e.u8(0xF))};
		if (e.u8(0xE) != 0xFF) {
			std::vector<std::string> args = ship;
			args.push_back(c.first(uint32_t(e.u16(0xA)) >> 11));
			args.push_back(c.family(e.u8(0xE)));
			c.format(odd ? 0x6B4D34 : 0x6B4D2C, args);
		} else {
			c.format(odd ? 0x6B4D30 : 0x6B4D28, ship);
		}
		break;
	}
	case 0x20: {
		// A burglary in the player's office (0x4809C5), in font 1: the town,
		// the gold stolen [+0xE], and the goods destroyed [+0xC] with their
		// names (a bit per ware in [+0xA]).
		c.out[5] = '1';
		c.format(0x6B50C8, {c.town(e.u8(5))});
		if (e.u16(0xE) > 0) {
			c.format(0x6B50CC, {num(e.u16(0xE))});
		}
		if (e.u16(0xA) > 0 && e.u16(0xC) > 0) {
			c.format(0x6B50D0, {num(e.u16(0xC))});
			for (uint32_t w = 0; w < 16; ++w) {
				if (e.u16(0xA) & (1u << w)) {
					c.add(" ");
					c.add(c.ware(w));
				}
			}
		}
		break;
	}
	case 0x21: {
		// A suitor (0x480ADE) to the local player (by its [+0x1A]: «Dear Mr.»
		// with the family name, «Dear Miss» with both), the sum [+0xC], and
		// the go-between's names [+0x8], [+0xA]. While the offer stands
		// (till the day [+0xE] of the letter's year) the window has a button.
		const MerchantMemory* m = c.merchant(save.player);
		if (m && m->u8(0x1A)) {
			c.format(0x6B5298, {c.merchant_family(m), num(e.u16(0xC))});
		} else {
			c.format(0x6B529C, {c.merchant_first(m), c.merchant_family(m), num(e.u16(0xC))});
		}
		c.format(0x6B52A4, {c.first(e.u16(8)), c.family(e.u16(0xA))});
		break;
	}
	case 0x22: {
		// A marriage proposal (0x480BF2) to the local player: the other's
		// names [+0xC], [+0xD], the town [+0xB] (modulo the towns), the dowry
		// 35000 + [+0xE]; the answer's date, 14 days after the letter's; the
		// go-between's names [+0x8], [+0x9].
		const MerchantMemory* m = c.merchant(save.player);
		const uint32_t towns = save.globals.u32(global::kWorldTownCount);
		const std::string town = towns ? c.town(e.u8(0xB) % towns) : std::string();
		const std::vector<std::string> rest = {c.first(e.u8(0xC)), c.family(e.u8(0xD)), town, num(e.u8(0xE) + 0x88B8u)};
		std::vector<std::string> args;
		uint32_t text;
		if (m && m->u8(0x1A)) {
			args = {c.merchant_family(m)};
			text = 0x6B5420;
		} else {
			args = {c.merchant_first(m), c.merchant_family(m)};
			text = 0x6B5424;
		}
		args.insert(args.end(), rest.begin(), rest.end());
		c.format(text, args);
		uint32_t day = e.u8(0) + 14u;
		uint32_t month = e.u8(1) & 0xFu;  // 0..11 (and a day past it moves it on)
		const uint32_t days = month + 1 < 13 ? kMonthDays[month + 1] : 0u;
		if (day > days) {
			day -= days;
			++month;
		}
		if (month < 12) {
			c.format(c.text(0x6B9CDC), {num(day), num(month + 1), num(e.u16(2))});
		} else {
			c.format(c.text(0x6B9CE8), {num(day), num(month - 11), num(e.u16(2) + 1u)});
		}
		c.format(0x6B5428, {c.first(e.u8(8)), c.family(e.u8(9))});
		break;
	}
	case 0x24:  // A feast's result (0x480DF8): one of 4 texts by [+0xC] % 4, the guests [+0xA], the town.
		c.format(c.table(0x6B57FC, e.u16(0xC) % 4u), {num(e.u16(0xA)), c.town(e.u8(5))});
		break;
	case 0x25: {
		// A dowry (0x480E38): the ship of type [+0x8] («a snaikka») with its
		// goods [+0xC], and the ship [+0xA] with its goods [+0xE] if its type
		// is 0..3 (« and %s», two texts by whether the first had goods); the
		// goods' text by the type's parity (0x6B58D4, 0x6B58E0); a final «.».
		auto goods = [&](uint32_t type, uint32_t amount) {
			if (type <= 3) {
				c.format((type & 1) ? 0x6B58E0 : 0x6B58D4, {num(amount)});
			} else {
				c.add(c.last);  // Past the jump table: the game appends its buffer again.
			}
		};
		c.format(0x6B58D0, {c.table(0x6C0E80, e.u16(8))});
		uint32_t second = 0x6B58D8;
		if (e.u16(0xC) > 0) {
			goods(e.u16(8), e.u16(0xC));
			second = 0x6B58DC;
		}
		if (e.u16(0xA) <= 3) {
			c.format(second, {c.table(0x6C0E80, e.u16(0xA))});
			if (e.u16(0xE) > 0) {
				goods(e.u16(0xA), e.u16(0xE));
			}
		}
		c.add(".");
		break;
	}
	case 0x26: {
		// A loan repaid to the player (0x480FA5): the amount [+0x8], the day
		// [+0xE], month [+0xC] («Jan») and year [+0xA] it was given (the date
		// moved on as 0x12's, also in the letter: letter_viewed), the town.
		uint32_t m = e.u16(0xC);
		uint32_t d = e.u16(0xE);
		if (m == 0) {
			m = d = 1;
		} else if (d == (m < 13 ? kMonthDays[m] : 0u) + 1) {
			d = 1;
			++m;
		}
		c.format(0x6B597C, {num(e.u16(8)), num(d & 0xFFFF), c.month(m & 0xFFFF), num(e.u16(0xA)), c.town(e.u8(5))});
		break;
	}
	case 0x27: {
		// A company's value (0x48102A): the merchant [+0x8], the change [+0xC]
		// (s32) and, for a rise, the dividend for the shares [+0xA] (shares ·
		// rise / 400).
		const MerchantMemory* m = c.merchant(e.u16(8));
		const int32_t change = e.s32(0xC);
		if (change >= 0) {
			c.format(0x6B5A84, {c.merchant_first(m), c.merchant_family(m), num(uint32_t(change))});
			const int32_t dividend = int32_t(e.u16(0xA)) * change / 400;
			if (c.spanish()) {
				// The Spanish code (0x481170): the shares first, for its %$2$u ... %$1$u.
				c.format(0x6B5A88, {num(e.u16(0xA)), num(uint32_t(dividend))});
			} else {
				c.format(0x6B5A88, {num(uint32_t(dividend)), num(e.u16(0xA))});
			}
		} else {
			c.format(0x6B5A8C, {c.merchant_first(m), c.merchant_family(m), num(uint32_t(-change))});
			c.format(0x6B5A90, {num(e.u16(0xA))});
		}
		break;
	}
	case 0x28:  // A dividend not paid (0x48111C): [+0x8], [+0xA] (the date of [+0xC] isn't in the text).
		c.format(0x6B5B10, {num(e.u16(8)), num(e.u16(0xA))});
		break;
	case 0x2A: {
		// A pirate's news (0x481780): «The» by the sex ([+0x8] != 0), the
		// merchant's adjective [+0x9] (0x69EE48) and rank [+0xA] (0x6A0608, 8
		// more for a man), names [+0xB], [+0xC], the pirate's [+0xD], [+0xE];
		// a text by [+0xF]: 0 beaten, 0xFF plundered, 1 a ship lost, else
		// that many ships lost.
		const uint32_t man = e.u8(8) != 0;
		std::vector<std::string> args = {c.table(0x6C0EF8, man), c.table(0x69EE48, e.u8(9)),
			c.table(0x6A0608, e.u8(0xA) + 8 * man), c.first(e.u8(0xB)), c.family(e.u8(0xC)), c.first(e.u8(0xD)),
			c.family(e.u8(0xE))};
		const uint8_t outcome = e.u8(0xF);
		uint32_t text = 0x6C14CC;
		if (outcome == 0) {
			text = 0x6C14C4;
		} else if (outcome == 0xFF) {
			text = 0x6AA184;
		} else if (outcome == 1) {
			text = 0x6C14C8;
		} else {
			args.push_back(num(outcome));
		}
		c.format(text, args);
		break;
	}
	case 0x2C: {
		// A ship sold at an auction (0x48120D), to the local player: its
		// title by its sex ([+0x1A] & 1) and names (its [+0x1C] as it is),
		// the town, the ship's name [+0xA], the price [+0xC], the buyer's
		// names [+0x8], [+0x9], the guild's fee [+0xE].
		c.auction_header();
		c.add(0x6B5E8C);
		c.add(c.town(e.u8(5)));
		c.add(0x6B5E90);
		c.add(c.list_ship_name(e.u16(0xA)));
		c.add(0x6B5E94);
		c.format(c.text(0x6B9CF4), {num(e.u16(0xC))});
		c.add(0x6B5E98);
		c.add(c.first(e.u8(8)));
		c.add(" ");
		c.add(c.family(e.u8(9)));
		c.add(0x6B5E9C);
		c.format(c.text(0x6B9CF8), {num(e.u16(0xE))});
		c.add(0x6B5EA0);
		break;
	}
	case 0x2D:  // The player bought his own ship (0x481472): its name [+0xA], the price [+0xC] + [+0xE], the fee [+0xE].
		c.auction_header();
		c.add(0x6B5EB0);
		c.add(c.list_ship_name(e.u16(0xA)));
		c.add(0x6B5EB4);
		c.format(c.text(0x6B9CFC), {num(uint32_t(e.u16(0xC)) + e.u16(0xE))});
		c.add(0x6B5EB8);
		c.format(c.text(0x6B9D00), {num(e.u16(0xE))});
		c.add(0x6B5EBC);
		break;
	case 0x2E:  // The ship not sold (0x48162F): its name [+0xC], the town.
		c.auction_header();
		c.add(0x6B5EA4);
		c.add(c.list_ship_name(e.u16(0xC)));
		c.add(0x6B5EA8);
		c.add(c.town(e.u8(5)));
		c.add(0x6B5EAC);
		break;
	case 0x2F: {
		// The town wall's bill (0x4819B2): to the merchant of names [+0x9],
		// [+0x8] & 0xFF («Lady» with [+0x8] & 0x8000), from the mayor of names
		// [+0xB], [+0xA]: [+0xC] · 100 and the town.
		c.add(0x6B6064);
		const uint16_t who = e.u16(8);
		c.format((who & 0x8000) ? 0x6B606C : 0x6B6068, {c.first(e.u8(9)), c.family(who & 0xFFu)});
		c.add(0x6B6070);
		c.add(c.first(e.u8(0xB)));
		c.add(" ");
		c.add(c.family(e.u8(0xA)));
		c.format(0x6B6074, {num(uint32_t(e.u16(0xC)) * 100), c.town(e.u8(5))});
		break;
	}
	case 0x30: {
		// A pirate's share of a boarding (0x481B02), to the local player
		// (its names as 0x4F6420 and 0x4F6450 keep them in its [+0xE8],
		// [+0xE4]): the ship's type [+0xE] & 3, the victim («matron» for a
		// woman's first name, [+0xA] > 0x7F) of names [+0xA], [+0xB], the
		// share [+0xC] (two texts by [+0x9]'s sign), the pirate captain's
		// names [+0x8], [+0x9] & 0x7F and the ship's name [+0xE] >> 2.
		const MerchantMemory* m = c.merchant(save.player);
		c.add(0x6B612C);
		c.add(c.merchant_first(m));
		c.add(" ");
		c.add(c.merchant_family(m));
		c.add("?");
		c.add(0x6B6130);
		c.add(c.table(0x6C0E80, e.u8(0xE) & 3u));
		c.add(e.u8(0xA) > 0x7F ? 0x6B6138 : 0x6B6134);
		c.add(c.first(e.u8(0xA)));
		c.add(" ");
		c.add(c.family(e.u8(0xB)));
		c.add((e.u8(9) & 0x80) ? 0x6AA188 : 0x6B613C);
		c.format(0x6B6140, {num(e.u16(0xC)), c.first(e.u8(8)), c.family(e.u8(9) & 0x7Fu),
			c.list_ship_name(e.u16(0xE) >> 2)});
		break;
	}
	case 0x31:  // An escort's bonus (0x481CA2): the orlog ship's name [+0xE], the town, the bonus [+0x8].
		c.add(0x6B61E8);
		c.add(c.list_ship_name(e.u16(0xE)));
		c.add(0x6B61EC);
		c.add(c.town(e.u8(5)));
		c.add(0x6B61F0);
		c.format(0x6B61F4, {num(e.u16(8))});
		break;
	case 0x32:
		// A convoy's arrival (0x481D5D): its leader's rank ([+0xD] & 7, 8 more
		// for [+0xC] & 1) and names [+0xF], [+0xE], the town, the sum [+0x8].
		c.add(0x6B62A4);
		c.add(c.table(0x6A0608, (e.u8(0xD) & 7u) + 8 * (e.u8(0xC) & 1u)));
		c.add(" ");
		c.add(c.first(e.u8(0xF)));
		c.add(" ");
		c.add(c.family(e.u8(0xE)));
		c.add(0x6B62A8);
		c.add(c.town(e.u8(5)));
		c.add(0x6B62AC);
		c.format(0x6B62B0, {num(e.u16(8))});
		break;
	case 0x33:
		// A wedding's date (0x481E82): the names [+0xB], [+0xA], the day [+0xD],
		// the month [+0xC] («January» for 0) and year [+0xE], the town [+0x8].
		c.add(0x6B63B0);
		c.format(0x6B63B4, {c.first(e.u8(0xB)), c.family(e.u8(0xA)), num(e.u8(0xD)), c.table(0x6C0DA0, e.u8(0xC)),
			num(e.u16(0xE))});
		c.format(0x6B63B8, {c.town(e.u16(8))});
		break;
	case 0x34: {
		// A ship lost on the expedition (0x481F3B), in font 1: its type
		// ([+0xA] & 3, «The snaikka»), its name [+0x8], and the date of the
		// tick [+0xC] (0x5311D0; «January» for 1).
		c.out[5] = '1';
		uint8_t day = 0;
		uint8_t month = 0;
		uint16_t year = 0;
		tick::events_mayor_detail::fn_5311d0(e.u32(0xC), day, month, year);
		c.format(0x6B6444, {c.table(0x6C0E38, e.u8(0xA) & 3u), c.list_ship_name(e.u16(8)), num(day),
			c.text(0x6C0D9C + 4 * month), num(year)});
		break;
	}
	case 0x36:
		// The wall's extension paid (0x481FBD): the names [+0x8] (past 0x80,
		// a woman's: the second text), [+0xA], the sum ([+0xC] << 16) + [+0xE],
		// the town.
		c.add(0x6B65E0);
		c.format(e.u16(8) > 0x80 ? 0x6B65E8 : 0x6B65E4, {c.first(e.u16(8)), c.family(e.u16(0xA))});
		c.add(0x6B65EC);
		c.format(0x6B65F0, {num((uint32_t(e.u16(0xC)) << 16) + e.u16(0xE)), c.town(e.u8(5))});
		break;
	case 0x37: {
		// A special tax (0x48208F), in font 3: to the local player (its title
		// by [+0x1A] & 1, its names in [+0xE8], [+0xE4]), the town twice, the tax [+0x8].
		c.out[5] = '3';
		const MerchantMemory* m = c.merchant(save.player);
		c.add(0x6B674C);
		c.format(c.text(0x6B9D04),
			{c.table(0x6C3048, m ? (m->u8(0x1A) & 1u) : 0u), c.merchant_first(m), c.merchant_family(m)});
		c.add(0x6B6750);
		c.add(c.town(e.u8(5)));
		c.add(0x6B6754);
		c.format(c.text(0x6B9D10), {num(e.u32(8))});
		c.add(0x6B6758);
		c.add(c.town(e.u8(5)));
		break;
	}
	case 0x3B:  // A coward (0x4821E0): two texts by [+0x8] != 0, the names [+0xA], [+0xC].
		c.format(e.u16(8) != 0 ? 0x6B6F48 : 0x6B6F4C, {c.first(e.u16(0xA)), c.family(e.u16(0xC))});
		c.add(0x6B6F50);
		break;
	case 0x3D:
	case 0x3E:
	case 0x3F:
		// A mission's letter (0x482296): its strings [+0xC] are a title, the
		// text and the buttons' labels (the record [+0x8]'s [+0xB] of them,
		// while its deadline [+0x4] is ahead). The text is the second.
		if (entry < save.mail_buffers.size()) {
			const std::vector<uint8_t>& b = save.mail_buffers[entry];
			size_t from = 0;
			while (from < b.size() && b[from] != 0) {
				++from;
			}
			size_t to = ++from;
			while (to < b.size() && b[to] != 0) {
				++to;
			}
			if (from < b.size()) {
				c.add(std::string(b.begin() + long(from), b.begin() + long(to)));
			}
		}
		break;
	case 0x41:
		// A pirate hunt (0x48238B): the town [+0x8]; the names [+0xC], [+0xE]
		// (the first text for a first name past 0x7F, the second else), the
		// ship's name [+0xA], the town [+0x8] twice and the town [+0x5].
		c.format(0x6B74EC, {c.town(e.u16(8))});
		c.format(e.u16(0xC) > 0x7F ? 0x6B74F0 : 0x6B74F4, {c.first(e.u16(0xC)), c.family(e.u16(0xE))});
		c.add(0x6B74F8);
		c.add(c.list_ship_name(e.u16(0xA)));
		c.add(0x6B74FC);
		c.add(c.town(e.u16(8)));
		c.add(0x6B7500);
		c.add(c.town(e.u16(8)));
		c.add(0x6B7504);
		c.add(c.town(e.u8(5)));
		break;
	case 0x42:  // The hunt ended (0x482531): the town [+0x8], the names [+0xC], [+0xE], the reward [+0xA], the town [+0x5].
		c.format(0x6B7600, {c.town(e.u16(8))});
		c.format(0x6B7608, {c.first(e.u16(0xC)), c.family(e.u16(0xE))});
		c.add(0x6B760C);
		c.add(c.town(e.u16(8)));
		c.add(0x6B7610);
		c.format(c.text(0x6B9D14), {num(e.u16(0xA))});
		// GAME BUG (0x482653): the slot 0x6B7614 is null in both exes (the
		// text before the town's name is missing) and the game copies from
		// address 0: on Windows it crashes there (the emulator copies its
		// page 0's bytes). p3vita adds nothing, with fix_game_bugs() or not.
		c.add(c.town(e.u8(5)));
		break;
	case 0x43:  // An office plundered (0x48269B): the town [+0x8] twice.
		c.format(0x6B76F8, {c.town(e.u16(8)), c.town(e.u16(8))});
		break;
	case 0x48: {
		// A Hanse mission (0x482705) by [+0xB]: 0 a boycott, 1 a blockade of
		// the town [+0xC], 2 a fleet against the pirates led by [+0xE], [+0xF];
		// the rank and names as 0x4C's; the ships asked for ([+0xD]) and by
		// when: the letter's date + 9 days (0x5311D0). A closing text.
		uint8_t day = 0;
		uint8_t month = 0;
		uint16_t year = 0;
		const uint32_t days = kDaysBefore[e.u8(1) & 0xF] + 365u * e.u16(2) + e.u8(0) + 9;
		tick::events_mayor_detail::fn_5311d0(days << 8, day, month, year);
		const std::string town = c.town(e.u8(0xC));
		const std::vector<std::string> by = {num(day), num(month), num(year), town};
		const uint8_t kind = e.u8(0xB);
		const uint8_t ships = e.u8(0xD);
		c.format(c.table(0x6AC490, kind), {town});
		c.add(c.table(e.u8(9) > 0x7F ? 0x6A0608 : 0x6A0628, e.u8(8) & 7u));
		c.add(" ");
		c.add(c.first(e.u8(9)));
		c.add(" ");
		c.add(c.family(e.u8(0xA)));
		c.add("\n\n");
		if (kind == 0) {
			c.format(0x6AC4DC, {town});
			if (ships != 0) {
				c.format(0x6AC4C8, {town});
				c.format(0x6AC4CC, by);
			}
		} else if (kind == 1) {
			c.format(0x6AC49C, {town});
			c.add(0x6AC4A0);
			if (ships > 1) {
				c.add(0x6AC4D4);
				c.format(0x6AC4D8, by);
			} else if (ships == 1) {
				c.add(0x6AC4A4);
				c.format(0x6AC4A8, by);
			}
		} else if (kind == 2) {
			c.add(0x6AC4AC);
			c.format(0x6AC4B0, {c.first(e.u8(0xE)), c.family(e.u8(0xF))});
			if (ships == 1) {
				c.add(0x6AC4B4);
				c.format(0x6AC4BC, by);
			} else if (ships > 1) {
				c.add(0x6AC4B8);
				c.format(0x6AC4C0, by);
			}
		}
		c.add(0x6AC4D0);
		break;
	}
	case 0x4A: {
		// An acquittal (0x482D8D): «\l», a title by [+0x8] & 0x8000 (0x6C3050,
		// else 0x6C3054), the names [+0x9], [+0x8] & 0xFF; by the charge
		// [+0xE]: 0 with the tavern and the town [+0xC], 1 with the town, 2,
		// 3 with the names [+0xB], [+0xA], 4..7 a text of 0x6B7CBC, 8 one of
		// its own, past 8 the 2's; and a closing text by the charge.
		c.add("\\l");
		c.add((e.u16(8) & 0x8000) ? 0x6C3050 : 0x6C3054);
		c.add(c.first(e.u8(9)));
		c.add(" ");
		c.add(c.family(e.u8(8)));
		const uint16_t charge = e.u16(0xE);
		switch (charge) {
		case 0:
			c.add(0x6B7CBC);
			c.add(c.tavern(e.u16(0xC)));
			c.add(0x6B7CDC);
			c.add(c.town(e.u16(0xC)));
			c.add(0x6B7CE0);
			break;
		case 1:
			c.add(0x6B7CC0);
			c.add(c.town(e.u16(0xC)));
			c.add(0x6B7CE4);
			break;
		case 3:
			c.add(0x6B7CC8);
			c.add(c.first(e.u8(0xB)));
			c.add(" ");
			c.add(c.family(e.u8(0xA)));
			c.add(0x6B7CE8);
			break;
		case 4:
		case 5:
		case 6:
		case 7:
			c.add(c.table(0x6B7CBC, charge));
			break;
		case 8:
			c.add(0x6AE648);
			break;
		default:  // 2, and past 8
			c.add(0x6B7CC4);
			break;
		}
		c.add(charge >= 5 && charge <= 7 ? 0x6B7CF0 : 0x6B7CEC);
		break;
	}
	case 0x4B: {
		// A verdict (0x48308F): the names [+0xD], [+0xE]; the charge [+0xC]'s
		// text (0x6B8080 by it; 8 and past it, two of their own) and more by
		// it (0: the tavern and town [+0xF], 1: the town, 3: the names [+0x9],
		// [+0xF], 5..7: a text); the sentence by [+0x8]: 0 a fine [+0xA] · 500
		// (the game also counts 10 days, for nothing), 1 goods [+0xB] of the
		// ware [+0xA] confiscated, 2 a ban from the town [+0xA] for [+0xB] weeks.
		c.add(c.first(e.u8(0xD)));
		c.add(" ");
		c.add(c.family(e.u8(0xE)));
		const uint8_t charge = e.u8(0xC);
		c.add(charge < 8 ? c.table(0x6B8080, charge) : c.text(charge == 8 ? 0x6AE634 : 0x6AE638));
		switch (charge) {
		case 0:
			c.add(c.tavern(e.u8(0xF)));
			c.add(0x6B80A0);
			c.add(c.town(e.u8(0xF)));
			c.add(0x6B80A4);
			break;
		case 1:
			c.add(c.town(e.u8(0xF)));
			c.add(0x6B80A8);
			break;
		case 3:
			c.add(c.first(e.u8(9)));
			c.add(" ");
			c.add(c.family(e.u8(0xF)));
			c.add(0x6B80AC);
			break;
		case 5:
		case 6:
		case 7:
			c.add(0x6B80B0);
			break;
		default:
			break;
		}
		switch (e.u8(8)) {
		case 0:
			c.add(0x6B80B4);
			c.format(0x6B80B8, {num(uint32_t(e.u16(0xA)) * 500)});
			break;
		case 1: {
			c.add(0x6AE63C);
			const uint8_t ware = e.u8(0xA);
			const bool barrels = ware < sizeof(kWareText) ? kWareText[ware] : false;
			c.format(0x6AE640, {num(e.u8(0xB)), c.text(barrels ? 0x6B9D2C : 0x6B9D30), c.ware(ware)});
			break;
		}
		case 2:
			c.add(0x6AE63C);
			c.format(0x6AE644, {c.town(e.u8(0xA)), num(e.u8(0xB))});
			break;
		default:
			c.add(c.last);  // The game appends its buffer as it is (the header's date).
			break;
		}
		break;
	}
	case 0x4D:  // A captain's experience (0x483566): his names [+0x8], [+0xA], the ship's name [+0xC], the wage [+0xE].
		c.add(0x6B8194);
		c.add(c.first(e.u16(8)));
		c.add(" ");
		c.add(c.family(e.u16(0xA)));
		c.add(0x6B8198);
		c.add(c.list_ship_name(e.u16(0xC)));
		c.add(0x6B819C);
		c.format(0x6B81A0, {num(e.u16(0xE))});
		break;
	case 0x4E:  // A captain who stays (0x483656): the player's names [+0xF], [+0xE]; his [+0x8], [+0xA]; the ship [+0xC].
		c.add(0x6B8288);
		c.add(c.first(e.u8(0xF)));
		c.add(" ");
		c.add(c.family(e.u8(0xE)));
		c.add(0x6B828C);
		c.add(c.first(e.u16(8)));
		c.add(" ");
		c.add(c.family(e.u16(0xA)));
		c.add(0x6B8290);
		c.add(c.list_ship_name(e.u16(0xC)));
		break;
	case 0x4F:  // A miser (0x48378A): the player's names [+0xC], [+0xE]; the captain's [+0x8], [+0xA].
		c.add(0x6B8324);
		c.add(c.first(e.u16(0xC)));
		c.add(" ");
		c.add(c.family(e.u16(0xE)));
		c.add(0x6B8328);
		c.format(c.text(0x6B9D34), {c.first(e.u16(8)), c.family(e.u16(0xA))});
		break;
	case 0x50:  // A captain leaving (0x483854): the player's names [+0xF], [+0xE]; the ship [+0xC]; his [+0x8], [+0xA].
		c.add(c.first(e.u8(0xF)));
		c.add(" ");
		c.add(c.family(e.u8(0xE)));
		c.add(0x6B83D0);
		c.add(c.list_ship_name(e.u16(0xC)));
		c.add(0x6B83D4);
		c.format(c.text(0x6B9D3C), {c.first(e.u16(8)), c.family(e.u16(0xA))});
		break;
	case 0x51:  // A captain hired (0x483946): the player's names [+0xC], [+0xE]; the tavern and town; his [+0x8], [+0xA].
		c.add(0x6B845C);
		c.add(c.first(e.u16(0xC)));
		c.add(" ");
		c.add(c.family(e.u16(0xE)));
		c.add(0x6B8460);
		c.add(c.tavern(e.u8(5)));
		c.add(0x6B8464);
		c.add(c.town(e.u8(5)));
		c.add(0x6B8468);
		c.format(c.text(0x6B9D44), {c.first(e.u16(8)), c.family(e.u16(0xA))});
		break;
	case 0x52:
		// A captain retiring (0x483A8C): an opening by [+0x8] & 1, the
		// player's names [+0xF], [+0xE]; by [+0xA] & 1 the ship [+0xC] and the
		// next port, or his age; his names [+0x8], [+0xA] and the ship.
		c.add(c.table(0x6B8748, e.u8(8) & 1u));
		c.add(c.first(e.u8(0xF)));
		c.add(" ");
		c.add(c.family(e.u8(0xE)));
		if (e.u8(0xA) & 1) {
			c.add(0x6B8750);
			c.add(c.list_ship_name(e.u16(0xC)));
			c.add(0x6B8754);
		} else {
			c.add(0x6B8758);
		}
		c.format(0x6B875C, {c.first(e.u16(8)), c.family(e.u16(0xA)), c.list_ship_name(e.u16(0xC))});
		break;
	case 0x5A:
	case 0x5B: {
		// A delivery for the town done (0x483BC0) or not (0x483DA9): «Dear»
		// by [+0xF] & 0x80, the names [+0xF], [+0xE], the amount of the ware
		// [+0xA] in barrels or loads (0x672C14), for 0x5A [+0xC] and the
		// bonus [+0xC] · [+0x8] if positive, for 0x5B [+0x8]; the town.
		const bool done = type == 0x5A;
		const uint16_t ware = e.u16(0xA);
		const bool barrels = ware < sizeof(kWareText) ? kWareText[ware] : false;
		c.add((e.u8(0xF) & 0x80) ? (done ? 0x6B8D4C : 0x6B8E5C) : (done ? 0x6B8D48 : 0x6B8E58));
		c.add(c.first(e.u8(0xF)));
		c.add(" ");
		c.add(c.family(e.u8(0xE)));
		c.add(done ? 0x6B8D50 : 0x6B8E60);
		const uint32_t units = done ? (barrels ? 0x6B9D4C : 0x6B9D54) : (barrels ? 0x6B9D60 : 0x6B9D68);
		c.format(c.text(units), {num(done ? e.u16(0xC) : e.u16(8)), c.ware(ware)});
		if (done) {
			c.add(0x6B8D54);
			const int32_t bonus = int32_t(e.u16(0xC)) * int32_t(e.u16(8));
			if (bonus > 0) {
				c.add(0x6B8D58);
				c.format(c.text(0x6B9D5C), {num(uint32_t(bonus))});
				c.add(0x6B8D5C);
			}
			c.add(0x6B8D60);
		} else {
			c.add(0x6B8E64);
		}
		c.add(c.town(e.u8(5)));
		break;
	}
	case 0x5C:
		// A boycott or blockade ended (0x483EED): the names [+0x8] (two texts
		// by > 0x7F), [+0xA]; the town [+0xC] (from the number of towns on,
		// a blockade's, & 0xFF); the thanks if [+0xE] (a ship lent).
		c.format(e.u16(8) > 0x7F ? 0x6B8FBC : 0x6B8FB8, {c.first(e.u16(8)), c.family(e.u16(0xA))});
		if (e.u16(0xC) < save.globals.u32(global::kWorldTownCount)) {
			c.format(0x6B8FC0, {c.town(e.u16(0xC))});
		} else {
			c.format(0x6B8FC4, {c.town(e.u16(0xC) & 0xFFu)});
		}
		if (e.u16(0xE) > 0) {
			c.add(0x6B8FC8);
		}
		break;
	case 0x5D:
		// A ship bought at an auction (0x483FFE), in font 1: the town, the
		// price [+0xE] · 10, its type («the snaikka», [+0xC] & 3), its name
		// [+0xA], the seller's names [+0x9], [+0x8].
		c.out[5] = '1';
		c.add(0x6B9008);
		c.add(c.town(e.u8(5)));
		c.format(0x6B900C, {num(uint32_t(e.u16(0xE)) * 10), c.table(0x6A171C, e.u8(0xC) & 3u),
			c.list_ship_name(e.u16(0xA)), c.first(e.u8(9)), c.family(e.u8(8))});
		break;
	case 0x5E:  // Respect enough for the next rank, but... (0x4840AB): the town.
		c.add(0x6B9088);
		c.add(c.town(e.u8(5)));
		c.add(0x6B908C);
		break;
	case 0x60:  // An office ready (0x484117), in font 1: the town.
		c.out[5] = '1';
		c.format(0x6B9168, {c.town(e.u8(5))});
		break;
	case 0x61:  // An office's capacity (0x484144), in font 1: the town, [+0x8].
		c.out[5] = '1';
		c.format(0x6B916C, {c.town(e.u8(5)), num(e.u16(8))});
		break;
	case 0x62: {
		// The player's company's value (0x484178; event 0x18): the rise
		// ([+0xA] << 16) + [+0xC], the shareholder (0xFFFF in [+0x8]: the
		// lender; else first name [+0x9], family name [+0x8], «, the» by
		// [+0x9] & 0x80), his shares [+0xE] and dividend (shares · rise / 400).
		const uint32_t rise = (uint32_t(e.u16(0xA)) << 16) + e.u16(0xC);
		c.add(0x6B9224);
		c.format(c.text(0x6B9D70), {num(rise)});
		c.add(0x6B9228);
		if (e.u16(8) == 0xFFFF) {
			c.add(0x6B9234);
		} else {
			c.add(c.first(e.u8(9)));
			c.add(" ");
			// GAME BUG (0x48425C): the family name [+0x8] (the merchant's
			// [+0x1D], 0x4E8936) is read as a first name. Fixed with
			// fix_game_bugs(): the family name.
			c.add(fix_game_bugs() ? c.family(e.u8(8)) : c.first(e.u8(8)));
			c.add((e.u8(9) & 0x80) ? 0x6B9230 : 0x6B922C);
		}
		c.format(c.text(0x6B9D74), {std::to_string(int32_t(e.u16(0xE)))});
		c.add(0x6B9238);
		c.format(c.text(0x6B9D78), {num(uint32_t((uint64_t(uint32_t(e.u16(0xE)) * rise) * 0x51EB851Fu) >> 39))});
		c.add(0x6B923C);
		break;
	}
	case 0x64:
	case 0x65: {
		// The prince's request (0x48435A) or meeting (0x4844E9), to the local
		// player: its title by [+0x1A] (0x6A99FC, else 0x6A9A00) and names
		// ([+0xE8], [+0xE4]), the goods list (0x636620), the day [+0xD] and
		// month [+0xE] («Jan»); 0x65 has the list last. (0x64 also has a
		// button while the letter's date + 13 days hasn't passed.)
		const MerchantMemory* m = c.merchant(save.player);
		std::vector<std::string> args = {c.text(m && m->u8(0x1A) ? 0x6A99FC : 0x6A9A00), c.merchant_first(m),
			c.merchant_family(m)};
		const std::string list = c.goods_list();
		if (type == 0x64) {
			args.insert(args.end(), {list, num(e.u8(0xD)), c.month(e.u8(0xE))});
			c.format(0x6A99F4, args);
		} else {
			args.insert(args.end(), {num(e.u8(0xD)), c.month(e.u8(0xE)), list});
			c.format(0x6A99F8, args);
		}
		break;
	}
	case 0x85: {
		// The prince's payment (0x4845F5): a text by [+0x8] (0x6A9A08), the
		// local player's title and names, the payment [+0xA].
		const MerchantMemory* m = c.merchant(save.player);
		// GAME BUG (0x636230, the sender): only the payment's low 16 bits are
		// kept in [+0xA]. With fix_game_bugs() the sender puts the high half
		// in [+0xC] (0 in the game), read here too.
		const uint32_t paid = fix_game_bugs() ? (uint32_t(e.u16(0xC)) << 16 | e.u16(0xA)) : e.u16(0xA);
		c.format(c.table(0x6A9A08, e.u16(8)),
			{c.text(m && m->u8(0x1A) ? 0x6A99FC : 0x6A9A00), c.merchant_first(m), c.merchant_family(m), num(paid)});
		break;
	}
	case 0x66:  // A cart convoy arrived (0x484683): the towns [+0x8], [+0xA].
		c.format(0x6AADD8, {c.town(e.u16(8)), c.town(e.u16(0xA))});
		break;
	case 0x67:  // A cart convoy stopped (0x4846C0): the towns [+0x8], [+0xA].
		c.format(0x6AADDC, {c.town(e.u16(8)), c.town(e.u16(0xA))});
		break;
	case 0x68:  // An ambush (0x4846F5): two texts by [+0x8] == [+0xA], the towns.
		c.format(e.u16(8) == e.u16(0xA) ? 0x6AADE8 : 0x6AADE0, {c.town(e.u16(8)), c.town(e.u16(0xA))});
		break;
	case 0x69:  // The prince's troops (0x48473E): the town.
		c.format(0x6AADE4, {c.town(e.u8(5))});
		break;
	case 0x6A: {
		// A town plundered by the player's pirate (0x484764): the local
		// player's names ([+0xE8], [+0xE4]), the town [+0xA], the share [+0xC]
		// (s32), the captain's names [+0x8], [+0x9] & 0x7F, the ship [+0xB].
		const MerchantMemory* m = c.merchant(save.player);
		c.add(0x6B612C);
		c.add(c.merchant_first(m));
		c.add(" ");
		c.add(c.merchant_family(m));
		c.add(0x6B6130);
		c.add(0x6AA18C);
		c.add(c.town(e.u8(0xA)));
		c.add(0x6AA190);
		c.format(0x6B6140, {std::to_string(e.s32(0xC)), c.first(e.u8(8)), c.family(e.u8(9) & 0x7Fu),
			c.list_ship_name(e.u8(0xB))});
		break;
	}
	case 0x6B:
		// The council's decision (0x484899): carried if [+0xC] < [+0xD], by
		// the proposal [+0xE]: 0 an extra tax [+0x8] (u32), 1 the walls, 2 a
		// unit, 3 the tax rate [+0x8] (raised if above the old [+0x9]); else
		// its refusal (0x6AB250 by the proposal); the town.
		c.add(0x6AB238);
		if (e.u8(0xC) < e.u8(0xD)) {
			switch (e.u16(0xE)) {
			case 0:
				c.format(0x6AB24C, {num(e.u32(8))});
				break;
			case 1:
				c.format(0x6AB23C, {});
				break;
			case 2:
				c.format(0x6AB240, {});
				break;
			case 3:
				c.format(e.u8(8) > e.u8(9) ? 0x6AB244 : 0x6AB248, {num(e.u8(8))});
				break;
			default:
				c.add(c.last);  // The game appends its buffer as it is (the header's date).
				break;
			}
		} else {
			c.add(c.table(0x6AB250, e.u16(0xE)));
		}
		c.format(0x6AB260, {c.town(e.u8(5))});
		break;
	case 0x6C:
		// The extra tax collected (0x4849C0): the town, the total [+0xC], the
		// player's part [+0x8] & 0x7FFFFFFF, and the shortfall's text if its sign bit is set.
		c.add(0x6AB32C);
		c.add(c.town(e.u8(5)));
		c.add(0x6AB330);
		c.format(c.text(0x6B9D7C), {num(e.u32(0xC))});
		c.add(0x6AB334);
		c.format(c.text(0x6B9D80), {num(e.u32(8) & 0x7FFFFFFFu)});
		c.add(0x6AB338);
		if (e.s32(8) < 0) {
			c.add(0x6AB33C);
		}
		break;
	case 0x7A:  // A child born (0x485F3B): its first name [+0x8] (a girl with its bit 0x80: the first text).
		c.format((e.u8(8) & 0x80) ? 0x6AED88 : 0x6AED8C, {c.first(e.u16(8))});
		break;
	case 0x7B:
		// A monument (0x485F91): «\n\n\l\d», a text by [+0xA] >> 7 (a woman's
		// first name), the adjective [+0x8] (0x6A0C18), the names [+0xA],
		// [+0xC]; then the town.
		c.add(c.text(0x6B9DC4));
		c.format(c.table(0x6B1D6C, uint32_t(e.u16(0xA)) >> 7),
			{c.table(0x6A0C18, e.u16(8)), c.first(e.u16(0xA)), c.family(e.u16(0xC))});
		c.format(0x6B1D74, {c.town(e.u8(5))});
		break;
	case 0x7C:  // A manager's experience (0x48635B): the town, [+0x8].
		c.format(0x6B1E04, {c.town(e.u8(5)), num(e.u16(8))});
		break;
	case 0x81:  // The hulk can be built (0x4861BF): the town, twice.
		c.format(0x6B19E8, {c.town(e.u8(5))});
		c.format(0x6B19EC, {c.town(e.u8(5))});
		break;
	case 0x73:
	case 0x75: {
		// A theft (0x485175) or a fire (0x4854BA) in the player's store: the
		// names [+0xF], [+0xE], the town (0x73), and the goods lost: three
		// u16 [+0x8], [+0xA], [+0xC], each an amount (low byte) of a ware
		// (high byte, below 24), «%u load(s)/barrel(s) ware», after a heading.
		const bool theft = type == 0x73;
		c.add(c.first(e.u8(0xF)));
		c.add(" ");
		c.add(c.family(e.u8(0xE)));
		if (theft) {
			c.add(0x6ABB78);
			c.add(c.town(e.u8(5)));
			c.add(0x6ABB7C);
		} else {
			c.add(0x6ABDB4);
		}
		uint32_t lost = 0;
		for (size_t at = 8; at <= 0xC; at += 2) {
			if (e.u16(at) >= 0x1800) {
				continue;
			}
			if (lost++ == 0) {
				c.add(theft ? 0x6ABB80 : 0x6ABDB8);
			}
			const uint8_t ware = e.u8(at + 1);
			c.format(c.text(theft ? 0x6B9D9C : 0x6B9DA8),
				{num(e.u8(at)), c.table(0x6C0DEC, kWareText[ware]), c.ware(ware)});
		}
		if (theft) {
			c.add(lost != 0 ? 0x6ABB84 : 0x6ABB88);
		} else if (lost == 0) {
			c.add(0x6ABDBC);
		}
		break;
	}
	case 0x74: {
		// A break-in caught (0x485303): the rank [+0xA] (women's for a first
		// name [+0x9] with 0x80, men's 8 later), the names [+0x9], [+0x8], the
		// town; the culprit's employer (a text by [+0xA] past 0xFF) with «the»,
		// his adjective [+0xF] (0x69EF2C), rank [+0xE] and names [+0xD],
		// [+0xC] & 0xFF (a man below 0x8000 in [+0xC]).
		c.add(c.table((e.u8(9) & 0x80) ? 0x6A0608 : 0x6A0628, e.u8(0xA)));
		c.add(" ");
		c.add(c.first(e.u8(9)));
		c.add(" ");
		c.add(c.family(e.u8(8)));
		c.format(0x6ABD0C, {c.town(e.u8(5))});
		c.add(e.u16(0xA) > 0xFF ? 0x6ABD10 : 0x6ABD1C);
		const uint32_t man = e.u16(0xC) < 0x8000;
		c.format(0x6ABD14, {c.table(0x6C0EE8, man), c.table(0x69EF2C, e.u8(0xF)), c.table(0x6A0608, e.u8(0xE) + 8 * man),
			c.first(e.u8(0xD)), c.family(e.u8(0xC))});
		c.add(0x6ABD18);
		break;
	}
	case 0x80: {
		// An overland route to build (0x48604E): to the merchant [+0xC] (his
		// [+0x1C] as it is), between the towns [+0xA], [+0xB], by the day
		// [+0xD], month and year of [+0xE] (year · 12 + month); signed by the
		// Alderman («Alderman» or the women's, [+0x8] >> 7) of names [+0x8], [+0x9].
		const MerchantMemory* m = c.merchant(e.u8(0xC));
		c.format(c.text(0x6B9DCC), {m ? c.first(m->u8(0x1C)) : std::string(), c.merchant_family(m)});
		c.format(0x6B23A0, {c.town(e.u8(0xA)), c.town(e.u8(0xB))});
		c.add(0x6B23A4);
		c.format(0x6B23A8, {num(e.u8(0xD)), num(e.u16(0xE) % 12u + 1), num(e.u16(0xE) / 12u)});
		c.format(c.text(0x6B9DD8), {c.text((e.u8(8) & 0x80) ? 0x6A0624 : 0x6A0644), c.first(e.u8(8)), c.family(e.u8(9))});
		break;
	}
	case 0x82: {
		// A respected trader (0x486222): a text by the sex (a woman's first
		// name [+0xC] & 0x80), the adjective [+0x8] (0x6A0C54, women's
		// 0x6A0C18), the rank [+0xA] (men's, women's), the names [+0xC],
		// [+0xE]; the town twice.
		const bool woman = e.u8(0xC) & 0x80;
		c.add(0x6B19F0);
		c.format(c.table(0x6B19FC, woman), {c.table(woman ? 0x6A0C18 : 0x6A0C54, e.u16(8)),
			c.table(woman ? 0x6A0608 : 0x6A0628, e.u16(0xA)), c.first(e.u16(0xC)), c.family(e.u16(0xE))});
		c.format(0x6B19F4, {c.town(e.u8(5))});
		c.add(0x6B19F8);
		c.add(c.town(e.u8(5)));
		break;
	}
	case 0x84: {
		// A Hanse order (0x486387): «Honourable» (two by [+0xA] & 0x8000, a
		// woman), the adjective [+0x8] & 0x1F, the rank [+0x9], the names
		// [+0xB], [+0xA] & 0xFF; by when: the day [+0xC] & 0x3F, the month
		// [+0xD] («January» for 0) and year [+0xE].
		const bool woman = e.u16(0xA) & 0x8000;
		c.add(woman ? 0x6AB638 : 0x6AB630);
		c.add(c.table(woman ? 0x6A0C18 : 0x6A0C54, e.u8(8) & 0x1Fu));
		c.add(" ");
		c.add(c.table(woman ? 0x6A0608 : 0x6A0628, e.u8(9)));
		c.add(" ");
		c.add(c.first(e.u8(0xB)));
		c.add(" ");
		c.add(c.family(e.u8(0xA)));
		c.add(0x6AAFC0);
		c.format(0x6AAFC4, {num(e.u8(0xC) & 0x3Fu), c.table(0x6C0DA0, e.u8(0xD)), num(e.u16(0xE))});
		c.add(0x6AAFC8);
		break;
	}
	case 0x76:
	case 0x7D:
	case 0x7E:
	case 0x7F: {
		// A building at the guild's auction (0x485604): «Honourable» (two by
		// a woman's first name [+0x8] & 0x80), the names [+0x8], [+0x9]; then
		// by the type: 0x76 sold, 0x7D the player's own bought back, 0x7E not
		// sold, 0x7F bought; the town, the building [+0xD] (0x6A57C8), the
		// price [+0xE] (in hundreds) and the other side's names [+0xA], [+0xB].
		c.format((e.u8(8) & 0x80) ? 0x6AE924 : 0x6AE920, {c.first(e.u8(8)), c.family(e.u8(9))});
		const std::string town = c.town(e.u8(5));
		const std::string building = c.table(0x6A57C8, e.u8(0xD));
		const std::string price = num(e.u16(0xE));
		if (type == 0x76) {
			c.format(0x6AE928, {town, building, price, c.first(e.u8(0xA)), c.family(e.u8(0xB)), price});
		} else if (type == 0x7D) {
			c.format(0x6AE930, {town, building, price, price});
		} else if (type == 0x7E) {
			c.format(0x6AE92C, {building, town});
		} else {
			c.format(0x6AE934, {town, building, price, c.first(e.u8(0xA)), c.family(e.u8(0xB))});
		}
		break;
	}
	case 0x6D: {
		// The Hanse council's invitation (0x484AE8) to the mayor: «Honourable»,
		// the adjective [+0xB] (women's 0x6A0C18 for a first name [+0xC] past
		// 0x7F, men's 0x6A0C54) and «Lady Mayoress»/«Lord Mayor», the names
		// [+0xC], [+0xD], the town; signed «the» adjective [+0x8] (0x69EE48)
		// «Alderman» (two pairs by [+0x9] past 0x7F) and names [+0x9], [+0xA].
		const bool woman = e.u8(0xC) >= 0x80;
		c.add(woman ? 0x6AB638 : 0x6AB630);
		c.add(c.table(woman ? 0x6A0C18 : 0x6A0C54, e.u8(0xB)));
		c.add(woman ? 0x6AB63C : 0x6AB634);
		c.add(c.first(e.u8(0xC)));
		c.add(" ");
		c.add(c.family(e.u8(0xD)));
		c.add(0x6AB640);
		c.add(c.town(e.u8(5)));
		c.add(0x6AB644);
		const bool alderwoman = e.u8(9) >= 0x80;
		c.add(alderwoman ? 0x6AB650 : 0x6AB648);
		c.add(c.table(0x69EE48, e.u8(8)));
		c.add(alderwoman ? 0x6AB654 : 0x6AB64C);
		c.add(c.first(e.u8(9)));
		c.add(" ");
		c.add(c.family(e.u8(0xA)));
		break;
	}
	case 0x6F:
	case 0x70: {
		// A Hanse task done (0x6F) or failed (0x70) (0x484DC9): «Honourable»
		// (a woman's first name [+0xA] & 0x80: 0x6AB638) and the names [+0xA],
		// [+0xB]; the task [+0x8] & 0x7F (0x70: [+0x8]): 0 found the town
		// [+0xE], 1 the road between [+0xE] and [+0xF], 2 stop the pirate
		// [+0xE], [+0xF], 3 his hideout, 4 a town's prosperity; 0x6F thanks
		// and, with [+0x8] & 0x80, the candidacy; 0x70 the failure; signed
		// « Alderman» (two by [+0xC] & 0x80) and names [+0xC], [+0xD].
		const bool woman = e.u8(0xA) & 0x80;
		c.format(c.text(0x6B9D90), {c.text(woman ? 0x6AB638 : 0x6AB630), c.first(e.u8(0xA)), c.family(e.u8(0xB))});
		const bool done = type == 0x6F;
		const uint32_t task = done ? (e.u8(8) & 0x7Fu) : e.u8(8);
		if (!done) {
			c.add(0x6ABA94);
		}
		// The hideout's town: the names object's list by number ([+0x8 + 4 i]
		// of 0x6DDAA0; past 40, its buffer's first name).
		auto hideout_town = [&]() {
			const uint32_t i = e.u8(0xE);
			if (fix_game_bugs()) {
				return c.town(i);
			}
			const std::vector<std::string>& names = save.opaque.town_name_table;
			return i < 40 && i < names.size() ? names[i] : (names.empty() ? std::string() : names[0]);
		};
		switch (task) {
		case 0:
			c.format(done ? 0x6ABA78 : 0x6ABA98, {c.town(e.u8(0xE))});
			break;
		case 1:
			c.format(done ? 0x6ABA7C : 0x6ABA9C, {c.town(e.u8(0xE)), c.town(e.u8(0xF))});
			break;
		case 2:
			// GAME BUG (0x48502D, 0x70 only): the pirate's family name is read
			// from [+0xE] too. Fixed with fix_game_bugs(): [+0xF], as 0x6F.
			c.format(done ? 0x6ABA80 : 0x6ABAA0,
				{c.first(e.u8(0xE)), c.family(done || fix_game_bugs() ? e.u8(0xF) : e.u8(0xE))});
			break;
		case 3:
			c.format(done ? 0x6ABA84 : 0x6ABAA4, {hideout_town()});
			break;
		case 4:
			c.format(done ? 0x6ABA88 : 0x6ABAA8, {c.town(e.u8(0xE))});
			break;
		default:
			break;
		}
		if (done) {
			c.add(0x6ABA8C);
			if (e.u8(8) & 0x80) {
				c.add(0x6ABA90);
			}
		} else {
			c.add(0x6ABAAC);
		}
		c.add((e.u8(0xC) & 0x80) ? 0x6AB654 : 0x6AB64C);
		c.add(c.first(e.u8(0xC)));
		c.add(" ");
		c.add(c.family(e.u8(0xD)));
		break;
	}
	case 0x77: {
		// The Hanse leader's matters (0x4857D8) by [+0xE]: 2 a Hanse Day not
		// called, 1 one demanded in the town, 0 an issue [+0x9]: from 5 on the
		// Alderman's letter («Honourable Alderman», two by [+0xC] & 0x80, the
		// names [+0xC], [+0xD], a text of 0x6ADDB4 with the town [+0x8] three
		// times, signed [+0xA], [+0xB]); below 5 the issue in the town [+0x8]
		// (past the towns, the pirates) and the council's closing (the game
		// strcpy's those two texts, 0x63ADD0, into its buffer).
		const uint8_t kind = e.u8(0xE);
		if (kind == 2) {
			c.add(0x6ADDD8);
			break;
		}
		if (kind == 1) {
			c.format(0x6ADDD4, {c.town(e.u8(5))});
			break;
		}
		if (kind != 0) {
			c.add(c.last);
			break;
		}
		const uint8_t issue = e.u8(9);
		if (issue >= 5) {
			c.format((e.u8(0xC) & 0x80) ? 0x6ADDC4 : 0x6ADDC0, {c.first(e.u8(0xC)), c.family(e.u8(0xD))});
			const std::string town = c.town(e.u8(8));
			c.format(c.table(0x6ADDB4, issue), {town, town, town});
			c.format(0x6ADDD0, {c.first(e.u8(0xA)), c.family(e.u8(0xB))});
			break;
		}
		if (e.u8(8) >= save.globals.u32(global::kWorldTownCount)) {
			c.add(0x6ADD90);
			c.add(0x6ADDA8);
		} else {
			const std::string town = c.town(e.u8(8));
			c.format(c.table(0x6ADD84, issue), {town});
			switch (issue) {
			case 0:
				c.format(0x6ADD98, {town, c.ware(e.u8(0xA))});
				break;
			case 1:
				c.format(0x6ADD9C, {town});
				break;
			case 2:
				c.format(0x6ADDA0, {town, c.family(e.u8(0xA)), town});
				break;
			case 3:
				c.add(c.last);
				break;
			default:
				c.format(0x6ADDA4, {town});
				break;
			}
		}
		c.add(c.table(0x6ADDAC, e.u8(0xB) & 3u));
		c.add(0x6ADDBC);
		break;
	}
	case 0x78: {
		// An extraordinary Hanse Day (0x485AE3): in the town on the day [+0x8]
		// of the month [+0x9] («Jan» for 1), this year or the next (if that
		// date is before the letter's); the subject: with [+0xB] >> 4 == 0 a
		// pirate fleet, else that subject (0x6A95B4 - 4 k) against the town
		// [+0xA] for the reason [+0xB] & 0xF (an export tax of the ware
		// [+0xC], an extra tax, pirates, plundering); signed by the Alderman
		// (the women's text for a first name [+0xE] & 0x80), his adjective
		// [+0xD] and names [+0xE], [+0xF].
		c.add(0x6ADD70);
		uint32_t year = e.u16(2);
		const uint8_t month = e.u8(9);
		const uint8_t letter_month = e.u8(1) & 0x7F;
		if (month < letter_month || (month == letter_month && e.u8(8) < e.u8(0))) {
			++year;
		}
		const std::string town = c.town(e.u8(5));
		if (c.spanish()) {
			// The Spanish code (0x485BD1): the day, month and year, then the town.
			c.format(0x6ADD74, {num(e.u8(8)), c.month(month), num(year & 0xFFFF), town});
		} else {
			c.format(0x6ADD74, {town, num(e.u8(8)), c.month(month), num(year & 0xFFFF)});
		}
		const uint8_t subject = e.u8(0xB) >> 4;
		if (subject == 0) {
			c.format(0x6ADD78, {c.text(0x6A96AC), c.text(0x6CE6CA), c.text(0x6A96D0)});
		} else {
			std::string reason;
			switch (e.u8(0xB) & 0xF) {
			case 0:
				reason = GameTexts::sprintf_raw(c.text(0x6A95B8), {c.ware(e.u8(0xC))});
				break;
			case 1:
				reason = c.text(0x6A95BC);
				break;
			case 2:
			case 5:
				reason = c.text(0x6A95C0);
				break;
			case 4:
			case 6:
				reason = c.text(0x6A95C4);
				break;
			default:
				break;
			}
			c.format(0x6ADD78, {c.text(0x6A95B4 - 4 * subject), c.town(e.u8(0xA)), reason});
		}
		c.format((e.u8(0xE) & 0x80) ? 0x6ADD80 : 0x6ADD7C,
			{c.table(0x69EE48, e.u8(0xD)), c.first(e.u8(0xE)), c.family(e.u8(0xF))});
		break;
	}
	case 0x79: {
		// A debtor who can't pay (0x485D0A): «Honoured» (a woman's first name
		// [+0xE] & 0x80: 0x6C3050), the player's names [+0xE], [+0xF]; the
		// debtor's rank [+0xC] & 0x2F, family name [+0xD], the town, the debt
		// (([+0xC] & 0xC0) << 10) + [+0x8]; two pairs of texts by the entry's
		// bit 1 (the list row's first byte). (The window also gets two buttons
		// for the event [+0xA], or queues operation 0x77: no text.)
		c.format(c.text(0x6B9DB4),
			{c.text((e.u8(0xE) & 0x80) ? 0x6C3050 : 0x6C3054), c.first(e.u8(0xE)), c.family(e.u8(0xF))});
		const uint32_t debt = ((uint32_t(e.u8(0xC)) & 0xC0) << 10) + e.u16(8);
		const uint32_t variant = entry & 2;
		c.format(0x6AEC04 + 4 * variant,
			{c.table(0x6A0628, e.u8(0xC) & 0x2Fu), c.family(e.u8(0xD)), c.town(e.u8(5)), num(debt)});
		c.add(0x6AEC08 + 4 * variant);
		c.add(0x6AEC14);
		c.add(c.town(e.u8(5)));
		break;
	}
	case 0x49:
		// The town guards' bill (0x482C57): to the mayor («Mayoress» for a
		// first name [+0x8] past 0x7F) of names [+0x8], [+0xA], the share [+0xC] (s32), the town.
		c.add(0x6B77CC);
		c.add(e.u16(8) > 0x7F ? 0x6B77D4 : 0x6B77D0);
		c.add(c.first(e.u16(8)));
		c.add(" ");
		c.add(c.family(e.u16(0xA)));
		c.add(0x6B77D8);
		c.format(c.text(0x6B9D28), {std::to_string(e.s32(0xC))});
		c.add(0x6B77DC);
		c.add(c.town(e.u8(5)));
		break;
	case 0x4C:
		// A Hanse mission not joined (0x4833EF): the rank [+0x8] & 7 (women's
		// for a first name [+0x9] past 0x7F, men's 8 later), the names [+0x9],
		// [+0xA], then by [+0xB]: 0 a boycott, 1 a blockade of the town [+0xC],
		// 2 no ships there (two texts by [+0xD] > 1).
		c.add(c.table(e.u8(9) > 0x7F ? 0x6A0608 : 0x6A0628, e.u8(8) & 7u));
		c.add(" ");
		c.add(c.first(e.u8(9)));
		c.add(" ");
		c.add(c.family(e.u8(0xA)));
		c.add("\n\n");
		switch (e.u8(0xB)) {
		case 0:
		case 1:
			c.format(e.u8(0xB) == 0 ? 0x6AC7C8 : 0x6ADD4C, {c.town(e.u8(0xC)), c.town(e.u8(0xC))});
			break;
		case 2:
			c.format(e.u8(0xD) > 1 ? 0x6AC7CC : 0x6AC7D0, {c.town(e.u8(0xC))});
			c.add(0x6AC7D4);
			break;
		default:
			c.add(c.last);  // The game appends its buffer as it is (the header's date).
			break;
		}
		break;
	case 0x46:  // A map's pieces (0x4826C3).
	case 0x6E:  // A pirate nest to destroy (0x484D7A).
		// The window frees the picture of the last letter (0x4BB490 on its
		// [+0xB8]) and 0x51AC60 makes this one's (letter_map_picture); the text
		// is only the picture, «\n\n\c\h%u_» with its number. (0x6E also
		// keeps [+0x5] in a local nobody reads.)
		c.format(type == 0x46 ? 0x6B9D1C : 0x6B9D84, {num(uint32_t(kLetterMapPicture))});
		break;
	case 0x2B:  // Too late (0x4811CA): [+0xC], the names [+0x8], [+0xA].
		c.format(0x6B5BAC, {num(e.u16(0xC)), c.first(e.u16(8)), c.family(e.u16(0xA))});
		break;
	case 0x1D:  // An expedition's letter (0x480849), in font 1: the game's placeholder «XXX» (0x6B9CD8).
		c.out[5] = '1';
		c.format(c.text(0x6B9CD8), {});
		break;
	default:
		return std::nullopt;
	}
	return c.out;
}

namespace {

// 0x47DD40 with, in `named`, the merchants whose first name it takes with
// 0x4F6420 (which writes their [+0x1C]).
std::string chronicle(const SaveGame& save, const GameTexts& t, std::vector<uint32_t>* named) {
	const GlobalMemory& g = save.globals;
	const uint32_t merchants = g.u16(global::kWorldMerchantCount);
	const uint32_t count = std::min<uint32_t>(g.u16(kMailCount), uint32_t(save.mail.size()));
	auto raw = [&](uint32_t english) -> const std::string& { return t.raw_at(english); };
	auto table = [&](uint32_t base, uint32_t k) -> const std::string& { return t.raw_at(base + 4 * k); };
	auto print = [](const std::string& format, const std::vector<std::string>& args) {
		return GameTexts::sprintf_raw(format, args);
	};
	// The title: today's date with the month's short name (0x6C11D0, 0x6C0D04).
	std::string local = print(raw(0x6C11D0), {num(g.u8(global::kWorldDay)), table(0x6C0D04, g.u8(global::kWorldMonth)),
												num(g.u16(global::kWorldYear))});
	std::string out = "\\c\\f2_" + local + "\n\\l";
	// The date of the last header: no real date at first.
	uint32_t last_day = 0x2C;
	uint32_t last_month = 0x2D;
	uint32_t last_year = 0x2D;
	bool first_header = true;
	// The game follows the list without a limit of steps.
	for (uint32_t i = g.u16(kMailChronicle), left = count; i < count && left > 0; i = save.mail[i].u16(6), --left) {
		const GameMemory<0x10>& e = save.mail[i];
		const Composer c{save, t, e, uint16_t(i), {}, {}};
		const uint32_t day = e.u8(0);
		const uint32_t month = e.u8(1) & 0x1Fu;
		const uint32_t year = e.u16(2);
		if (day != last_day || month != last_month || year != last_year) {
			last_day = day;
			last_month = month;
			last_year = year;
			local = print(raw(0x6B9C90), {num(day), num(month + 1), num(year)});  // «%u. %u. %u», in the local buffer
			if (!first_header) {
				out += "\n\n";
			}
			first_header = false;
			out += "\\c\\f3_" + local;
		}
		out += "\n\n\\l\\f0_";
		// A merchant of the news (0x5303C0) and his names (0x4F6420, 0x4F6450).
		auto merchant = [&](uint32_t m) { return c.merchant(int(m)); };
		auto first_of = [&](uint32_t m) {
			if (named && m < save.merchants.size()) {
				named->push_back(m);
			}
			return c.merchant_first(merchant(m));
		};
		auto family_of = [&](uint32_t m) { return c.merchant_family(merchant(m)); };
		auto sex_of = [&](uint32_t m) -> uint32_t { return merchant(m) ? merchant(m)->u8(0x1A) : 0u; };
		const std::string town5 = c.town(e.u8(5));
		// Each case makes the local buffer's text, appended after it; some
		// append to the text directly first. A case that leaves the local buffer
		// as it was appends the previous sprintf again (the date's, or the last
		// news's): the game's.
		switch (e.u8(4)) {
		case 0x06: {  // A promotion (0x47EB0A).
			const uint32_t m = e.u16(0xC);
			if (m >= merchants) {
				break;
			}
			const uint32_t sex = sex_of(m);
			const std::string family = family_of(m);
			const std::string first = first_of(m);
			const std::string& title = table(0x6A0608, e.u16(0xA) + sex * 8);
			// The Spanish code passes the town before the title (its %$6$s, %$5$s).
			local = print(table(0x6B3274, sex & 1), {table(0x6C0F08, sex & 1), table(0x69EF2C, e.u16(0xE) % 14), first,
														family, c.spanish() ? town5 : title, c.spanish() ? title : town5});
			break;
		}
		case 0x0B:  // An extraordinary Hanse day (0x47EFAF).
			local = print(raw(0x6B8AB8), {c.town(e.u16(8))});
			break;
		case 0x0C: {  // A guild entry (0x47EBAD).
			const uint32_t m = e.u16(8);
			if (m >= merchants) {
				local.clear();
				break;
			}
			const uint32_t sex = sex_of(m);
			const std::string family = family_of(m);
			const std::string first = first_of(m);
			local = print(raw(0x6B3678), {table(0x6C0EF8, sex & 1), table(0x69EE48, e.u16(0xA)),
											 table(0x6A0608, e.u16(0xC) + sex * 8), first, family, town5});
			break;
		}
		case 0x0D: {  // A new office (0x47EC37): [+0xE] his title (low 4 bits) and rank.
			const uint32_t m = e.u16(8);
			if (m >= merchants) {
				local.clear();
				break;
			}
			const uint32_t f = e.u16(0xE);
			const uint32_t sex = sex_of(m);
			const std::string family = family_of(m);
			const std::string first = first_of(m);
			local = print(raw(sex ? 0x6B37C0 : 0x6B37C4),
				{table(0x69EE48, (f & 0xF) % 14), table(0x6A0608, ((f & 0xFF) >> 4 & 7) + sex * 8), first, family, town5,
					num(e.u16(0xA))});
			break;
		}
		case 0x0E: {  // A move (0x47DFF8): from [+0xA] to [+0x5]; a new company with no old town.
			const uint32_t m = e.u16(8);
			if (m >= merchants) {
				break;
			}
			const uint32_t f = e.u16(0xC);
			const uint32_t from = e.u16(0xA);
			const uint32_t sex = sex_of(m);
			if (from >= g.u32(global::kWorldTownCount)) {
				local = print(raw(0x6C1254), {family_of(m), town5});
				break;
			}
			const std::string family = family_of(m);
			const std::string first = first_of(m);
			local = print(raw(sex ? 0x6B3848 : 0x6B384C), {table(0x69EE48, (f >> 3) % 14),
															  table(0x6A0608, (f & 7) + sex * 8), first, family,
															  c.town(from), town5});
			break;
		}
		case 0x1C: {  // A discovery (0x47E150): the names the merchant's [+0xE8], [+0xE4] point at.
			const uint32_t m = e.u8(8);
			if (m >= merchants) {
				break;
			}
			const MerchantMemory* mm = merchant(m);
			const uint32_t sex = sex_of(m);
			const uint32_t rank = mm ? mm->u8(0x39C + mm->u8(0x19)) : 0;
			local = print(raw(e.u8(9) < 0x1E ? 0x6C12AC : 0x6C12F8),
				{table(0x6C0EF8, sex), table(0x69EE48, tick::merchants_detail::fn_4f9430(save, int(m))),
					table(0x6A0608, rank + sex * 8), c.merchant_first(mm), c.merchant_family(mm)});
			break;
		}
		case 0x23: {  // A wedding (0x47ED50), the merchant the man or the woman.
			const uint32_t m = e.u16(8);
			const MerchantMemory* mm = merchant(m);
			const std::string family = family_of(m);
			const std::string first = first_of(m);
			const std::string home = c.town(mm ? mm->u8(0x18) : 0);
			const std::string spouse_first = c.first(e.u8(0xB));
			const std::string spouse_family = c.family(e.u8(0xA));
			const std::string spouse_town = c.town(e.u16(0xC));
			const std::string age = num(e.u16(0xE));
			local = sex_of(m) ? print(raw(0x6B54A0), {town5, first, family, home, age, spouse_first, spouse_family, spouse_town})
							  : print(raw(0x6B54A0), {town5, spouse_first, spouse_family, spouse_town, age, first, family, home});
			break;
		}
		case 0x2A: {  // A pirate's win or loss (0x47E262): [+0xF] the ships lost.
			const uint32_t sex = e.u8(8) != 0;
			const uint8_t lost = e.u8(0xF);
			std::vector<std::string> args = {table(0x6C0EF8, sex), table(0x69EE48, e.u8(9)),
				table(0x6A0608, e.u8(0xA) + sex * 8), c.first(e.u8(0xB)), c.family(e.u8(0xC)), c.first(e.u8(0xD)),
				c.family(e.u8(0xE))};
			uint32_t format = 0x6C14CC;
			if (lost == 0) {
				format = 0x6C14C4;
			} else if (lost == 0xFF) {
				format = 0x6AA184;
			} else if (lost == 1) {
				format = 0x6C14C8;
			} else {
				args.push_back(num(lost));
			}
			local = print(raw(format), args);
			break;
		}
		case 0x35:  // A famine (0x47E466): appended as it is, nothing after it.
			out += raw(0x6C138C) + town5 + raw(0x6C1390);
			local.clear();
			break;
		case 0x36:  // A town wall (0x47E5B0).
			local = print(raw(0x6C13CC), {town5});
			break;
		case 0x38: {  // A siege's end (0x47E5D6): [+0xA] the money (0xFFFFFFFF: beaten back), [+0xE] buildings.
			const std::string days = num(e.u16(8));
			if (e.u16(0xA) == 0xFFFF && e.u16(0xC) == 0xFFFF) {
				local = print(raw(0x6B6C44), {days, town5});
				break;
			}
			const std::string money = std::to_string(int32_t((uint32_t(e.u16(0xC)) << 16) + e.u16(0xA)));
			// The Spanish code passes the days first (its texts have %$N$).
			if (e.u16(0xE) != 0) {
				local = c.spanish() ? print(raw(0x6B6C40), {days, town5, money, num(e.u16(0xE))})
									: print(raw(0x6B6C40), {town5, days, money, num(e.u16(0xE))});
			} else {
				local = c.spanish() ? print(raw(0x6B6C48), {days, town5, money}) : print(raw(0x6B6C48), {town5, days, money});
			}
			break;
		}
		case 0x39:  // A pirate attack beaten off (0x47E694), or a lair not taken.
			if (e.u8(0xD) == 0) {
				out += print(raw(0x6B6D34), {town5, town5});
				if (e.u8(0xA) == 0xFF && e.u8(0xB) == 0xFF) {
					out += raw(0x6B6D40);
				} else {
					const bool man = e.u8(0xA) <= 0x7F;
					out += print(raw(man ? 0x6B6D38 : 0x6B6D3C), {table(0x69EF2C, e.u8(8)),
																	  table(man ? 0x6A0738 : 0x6A0718, e.u8(9)),
																	  c.first(e.u8(0xA)), c.family(e.u8(0xB))});
				}
				local.clear();
			} else {
				const bool man = e.u8(0xA) <= 0x7F;
				local = print(raw(man ? 0x6AA1B8 : 0x6AA1BC), {table(0x69EE48, e.u8(8)), table(man ? 0x6A0628 : 0x6A0608, e.u8(9)),
																   c.first(e.u8(0xA)), c.family(e.u8(0xB)),
																   c.first(e.u8(0xC)), c.family(e.u8(0xD))});
			}
			break;
		case 0x3A: {  // A town plundered by pirates (0x47E8A2), or a lair destroyed ([+0xC] & 0x80).
			const uint8_t f = e.u8(0xC);
			const bool man = e.u8(0xD) <= 0x7F;
			if (!(f & 0x80)) {
				local = print(raw(0x6B6E0C), {town5, num(e.u32(8))});
				if (e.u8(0xD) == 0xFF || e.u8(0xE) == 0xFF) {
					break;
				}
				out += local;
				local = print(raw(man ? 0x6B6E10 : 0x6B6E14), {table(0x69EF2C, f >> 4 & 7),
																   table(man ? 0x6A0628 : 0x6A0608, f & 0xF),
																   c.first(e.u8(0xD)), c.family(e.u8(0xE))});
			} else {
				local = print(raw(man ? 0x6AA1B0 : 0x6AA1B4),
					{table(0x69EE48, f >> 4 & 7), table(man ? 0x6A0628 : 0x6A0608, f & 0xF), c.first(e.u8(0xD)),
						c.family(e.u8(0xE)), c.first(e.u8(0xB)), c.family(e.u8(0xF)), num(e.u32(8) & 0xFFFFFF)});
			}
			break;
		}
		case 0x53:  // A siege starts (0x47EADD).
			local = print(raw(0x6B8860), {town5, num(e.u16(8))});
			break;
		case 0x55:  // A mayor's election (0x47EE65).
			local = print(raw(0x6B7304), {town5, c.first(e.u16(8)), c.family(e.u16(0xA)), num(e.u16(0xC)), num(e.u16(0xE))});
			break;
		case 0x56: {  // A new country found (0x47EFD6); the town by the merchant's index (unused by the text).
			const uint32_t m = e.u16(8);
			const uint32_t sex = sex_of(m);
			const std::string family = family_of(m);
			const std::string first = first_of(m);
			local = print(raw(0x6B88C8), {table(c.spanish() ? 0x6C0EF8 : 0x6C0ED8, sex), table(0x69EE48, e.u16(0xA) % 14),
											 table(0x6A0608, (e.u8(0xC) & 7) + sex * 8), first, family, c.town(m)});
			break;
		}
		case 0x57: {  // A new Alderman (0x47EEBC).
			const uint32_t sex = e.u8(9);
			local = print(raw(sex ? 0x6B8A68 : 0x6B8A6C), {table(0x69EE48, e.u8(0xA)), table(0x6A0608, e.u8(0xD) + sex * 8),
															  c.first(e.u8(0xB)), c.family(e.u8(0xC)), c.town(e.u8(8)),
															  town5});
			break;
		}
		case 0x58:  // The plague (0x47F070).
			local = print(raw(0x6B8B60), {town5});
			break;
		case 0x59:  // A town's fire (0x47F095).
			local = print(raw(0x6B8BE0), {town5});
			break;
		case 0x6F: {  // A Hanse task done (0x47F0BB): «%s %s %s %s %s» and what he did, by [+0x8].
			const bool man = e.u8(0xA) <= 0x7F;
			const uint8_t f = e.u8(9);
			// The adjective from 0x69EE38 by [+0x9] % 31 (his title + rank << 5): four
			// slots before the adjectives' table, and the rank added, a game bug
			// (fixed: 0x69EE48 by the title).
			const std::string& adjective = fix_game_bugs() ? table(0x69EE48, f & 0x1F) : table(0x69EE38, f % 31);
			local = print(raw(0x6B9C9C), {raw(man ? 0x69C038 : 0x69C03C), adjective, table(0x6A0608, (f >> 5) + (man ? 8 : 0)),
											 c.first(e.u8(0xA)), c.family(e.u8(0xB))});
			out += local;
			switch (e.u8(8) & 0x7F) {
			case 0:
				local = print(raw(0x6AB73C), {c.town(e.u8(0xE))});
				break;
			case 1:
				local = print(raw(0x6AB740), {c.town(e.u8(0xE)), c.town(e.u8(0xF))});
				break;
			case 2:
				// The pirate's family name from [+0xE], his first name's: a game bug
				// (0x4EC354 puts it in [+0xF]; fixed).
				local = print(raw(0x6AB748), {c.first(e.u8(0xE)), c.family(e.u8(fix_game_bugs() ? 0xF : 0xE))});
				break;
			case 3:
				local = print(raw(0x6AB744), {});
				break;
			case 4:
				local = print(raw(0x6AB74C), {c.town(e.u8(0xE))});
				break;
			default:
				break;  // The names again.
			}
			break;
		}
		case 0x7B: {  // A monument (0x47F234).
			out += print(table(0x6B1D6C, e.u16(0xA) >> 7u), {table(0x69EF2C, e.u16(8)), c.first(e.u16(0xA)), c.family(e.u16(0xC))});
			local = print(raw(0x6B1D74), {town5});
			break;
		}
		default:  // No line (0x47E4C0).
			local.clear();
			break;
		}
		out += local;
		out += "\n\n\\c\\t16063_";
	}
	out += ' ';
	return out;
}

}  // namespace

std::string compose_chronicle(const SaveGame& save, const GameTexts& texts) {
	return chronicle(save, texts, nullptr);
}

void chronicle_viewed(SaveGame& save, const GameTexts& texts) {
	std::vector<uint32_t> named;
	chronicle(save, texts, &named);
	for (const uint32_t m : named) {
		MerchantMemory& mm = save.merchants[m].memory;
		mm.u8(0x1C) = mm.u8(0x1A) ? uint8_t(mm.u8(0x1C) & 0x7F) : uint8_t(mm.u8(0x1C) | 0x80);
	}
}

std::optional<std::string> compose_ticker(const SaveGame& save, const GameTexts& t, uint8_t kind, uint16_t e) {
	// 0x5483D0's cases by kind (the byte table 0x548834 into 0x5487E8).
	static constexpr uint8_t kCase[0x2B] = {0, 1, 2, 3, 4, 5, 6, 6, 7, 18, 6, 8, 18, 1, 8, 2, 18, 2, 18, 2, 2, 1, 9, 2,
		2, 18, 2, 1, 1, 10, 2, 2, 1, 1, 11, 12, 13, 14, 2, 2, 15, 16, 17};
	// Cases 11..15: the trade route's texts (0x6B04F4 by 6, 7, 8, 10, 11).
	static constexpr uint8_t kRouteText[5] = {6, 7, 8, 10, 11};
	const GameMemory<0x10> none;
	const Composer c{save, t, none, 0, {}, {}};
	const GlobalMemory& g = save.globals;
	const uint32_t ships = std::min<uint32_t>(g.u16(kShipCount), uint32_t(save.ships.size()));
	const uint32_t towns = g.u32(global::kWorldTownCount);
	auto raw = [&](uint32_t english) -> const std::string& { return t.raw_at(english); };
	auto print = [](const std::string& format, const std::vector<std::string>& args) {
		return GameTexts::sprintf_raw(format, args);
	};
	auto ship = [&](uint32_t s) -> const ShipMemory& { return save.ships[s].memory; };
	const std::string& by_kind = t.raw_at(0x6B28A8 + 4u * kind);
	switch (kind <= 0x2A ? kCase[kind] : 18) {
	case 0:  // A ship's captain stops its trade route; past the ships, the text as it is («%s ...»).
		return e < ships ? print(raw(0x6B23F0), {c.ship_name(e)}) : raw(0x6B28A8);
	case 1:  // A town's (0x6DDA00 by e).
		return print(by_kind, {c.town(e)});
	case 2:
		return std::nullopt;
	case 3:  // A convoy docked in its town ([+0x39]), or at its destination.
		if (e >= ships) {
			return std::string();
		}
		if (ship(e).u8(0x39) < towns) {
			return print(raw(0x6B28BC), {raw(0x6C3638), c.ship_name(e), c.town(ship(e).u8(0x39))});
		}
		return print(raw(0x6B28B4), {c.ship_name(e)});
	case 4:  // A convoy back from an expedition (past the ships, the name is a buffer of the
			 // real-time object, 0x6DFC8A: not reproduced, empty).
		return print(raw(0x6B28B8), {e < ships ? c.ship_name(e) : std::string()});
	case 5:  // A ship docked in [+0x38], «The snaikka X ...» by its type.
		if (e >= ships || ship(e).u8(0x38) >= towns) {
			return std::string();
		}
		return print(raw(0x6B28BC), {t.raw_at(0x6C0E38 + 4u * (ship(e).u8(0xE) & 3)), c.ship_name(e),
										c.town(ship(e).u8(0x38))});
	case 6:  // The ship's town ([+0x39]).
		return e < ships ? print(by_kind, {c.town(ship(e).u8(0x39))}) : std::string();
	case 7:
		return e < ships ? print(raw(0x6B28C8), {c.ship_name(e)}) : std::string();
	case 8:  // The ship's name.
		return e < ships ? print(by_kind, {c.ship_name(e)}) : std::string();
	case 9:  // A ship sunk: a name of the ship names' lists (0x512B20).
		return print(raw(0x6B2900), {c.list_ship_name(e)});
	case 10:  // Ships lost on an expedition.
		return e < 2 ? raw(0x6C3594) : print(raw(0x6B291C), {num(e)});
	case 11:
	case 12:
	case 13:
	case 14:
	case 15:  // A trade route's ship.
		return e < ships ? print(t.raw_at(0x6B04F4 + 4u * kRouteText[kCase[kind] - 11]), {c.ship_name(e)})
						 : std::string();
	case 16:  // A land transport: from the town in the low byte to the high byte's.
		return towns ? print(raw(0x6B294C), {c.town((e & 0xFFu) % towns), c.town((e >> 8u) % towns)}) : std::string();
	case 17:
		// The Hanse's blockade: 0x548766 shows it only while [+0x928] of the
		// real-time object is set, which is when the game isn't running (the
		// calls then wait in [+0x948]): during the game, never. A game bug
		// (the test the other way round); fixed.
		if (!fix_game_bugs() || !towns) {
			return std::nullopt;
		}
		return print(raw(0x6B2950), {c.town(e % towns)});
	default:  // The kind's text as it is (0x548798).
		return by_kind;
	}
}

std::optional<std::string> compose_ticker(const SaveGame& save, const GameTexts& t, const SaveGame::TickerLine& line) {
	if (line.text != 0) {
		return GameTexts::sprintf_raw(t.raw_at(line.text), {table_town(save, line.e)});
	}
	return compose_ticker(save, t, line.kind, line.e);
}

std::vector<uint16_t> letter_rows(const SaveGame& save, uint8_t tab) {
	std::vector<uint16_t> rows;
	if (save.player < 0 || size_t(save.player) >= save.merchants.size()) {
		return rows;
	}
	const uint32_t count = std::min<uint32_t>(save.globals.u16(kMailCount), uint32_t(save.mail.size()));
	// 0x47C520 counts the chain first and stops after that many rows.
	for (uint32_t i = save.merchants[size_t(save.player)].memory.u16(0xA), left = count; i < count && left > 0;
		 i = save.mail[i].u16(6), --left) {
		if (letter_class(save.mail[i].u8(4)) == tab) {
			rows.push_back(uint16_t(i));
		}
	}
	return rows;
}

LetterRow letter_row(const SaveGame& save, const GameTexts& texts, uint16_t entry) {
	LetterRow row;
	if (entry >= save.mail.size()) {
		return row;
	}
	const GameMemory<0x10>& e = save.mail[entry];
	const Composer c{save, texts, e, entry, {}, {}};
	row.town = c.town(e.u8(5));
	row.date = c.row_date();
	row.unread = (e.u8(1) & 0x80) != 0;
	const uint8_t type = e.u8(4);
	if (type >= 0x3C && type <= 0x40) {
		if (entry < save.mail_buffers.size()) {
			const std::vector<uint8_t>& b = save.mail_buffers[entry];
			row.title.assign(b.begin(), std::find(b.begin(), b.end(), uint8_t(0)));
		}
	} else if (type < 0x86) {
		row.title = texts.raw_at(0x6A52D0 + 4u * type);
	}
	return row;
}

const std::string& letter_tab_name(const GameTexts& texts, uint8_t tab) {
	return texts.raw_at(0x6B2DD8 + 4u * (tab & 3));
}

bool letter_notice_wanted(const SaveGame& save, uint16_t entry) {
	if (entry >= save.mail.size() || save.player < 0 || size_t(save.player) >= save.merchants.size()) {
		return false;
	}
	const uint8_t cls = letter_class(save.mail[entry].u8(4));
	const uint8_t bits = save.merchants[size_t(save.player)].memory.u8(0x26);
	return cls >= 1 && cls <= 3 && (bits & (8u << (cls - 1))) != 0;
}

std::string letter_notice(const SaveGame& save, const GameTexts& texts, uint16_t entry) {
	if (entry >= save.mail.size()) {
		return std::string();
	}
	const uint8_t cls = letter_class(save.mail[entry].u8(4));
	if (cls < 1 || cls > 3) {
		return std::string();
	}
	return GameTexts::sprintf_raw(texts.raw_at(0x6A4570 + 4u * (cls - 1)), {letter_row(save, texts, entry).title});
}

std::vector<LetterParagraph> letter_paragraphs(const std::string& raw) {
	std::vector<LetterParagraph> paragraphs(1);
	LetterParagraph::Align align = LetterParagraph::Left;
	uint8_t font = 0;
	auto digits_and_underscore = [&](size_t j) {
		while (j < raw.size() && raw[j] >= '0' && raw[j] <= '9') {
			++j;
		}
		return j < raw.size() && raw[j] == '_' ? j + 1 : j;
	};
	for (size_t i = 0; i < raw.size() && raw[i] != 0; ++i) {
		const uint8_t c = uint8_t(raw[i]);
		LetterParagraph& p = paragraphs.back();
		if (c == '\n') {
			LetterParagraph next;
			next.align = align;
			next.font = font;
			paragraphs.push_back(next);
			continue;
		}
		if (c == '\r') {
			continue;
		}
		if (c == '_') {
			p.text += kLetterSoftHyphen;
			continue;
		}
		if (c != '\\' || i + 1 >= raw.size()) {
			append_cp1252(p.text, c);
			continue;
		}
		const char code = raw[i + 1];
		switch (code) {
		case 'l':
		case 'r':
		case 'c':
			align = code == 'l' ? LetterParagraph::Left : code == 'r' ? LetterParagraph::Right : LetterParagraph::Center;
			p.align = align;
			++i;
			break;
		case 'f': {
			const size_t end = digits_and_underscore(i + 2);
			font = uint8_t(std::atoi(raw.substr(i + 2, end - (i + 2)).c_str()));
			if (p.text.empty()) {
				p.font = font;
			}
			i = end - 1;
			break;
		}
		case 't':
		case 'h': {
			const size_t end = digits_and_underscore(i + 2);
			const std::string number = raw.substr(i + 2, end - (i + 2));
			p.picture = std::atoi(number.c_str());
			p.made_picture = code == 'h';
			if (code == 't') {
				size_t digits = 0;
				while (digits < number.size() && number[digits] >= '0' && number[digits] <= '9') {
					++digits;
				}
				p.text += kTextPicture;
				p.text += number.substr(0, digits);
				p.text += kTextPictureEnd;
			}
			i = end - 1;
			break;
		}
		case 'd':
			++i;  // The drop cap: the letter after it stays.
			break;
		case 'C':
			p.text += kLetterCoin;
			++i;
			break;
		case 'B':
			p.text += kLetterBarrel;
			++i;
			break;
		case 'L':
			p.text += kLetterLoad;
			++i;
			break;
		default:
			append_cp1252(p.text, c);  // A backslash the box shows as it is.
			break;
		}
	}
	return paragraphs;
}

}  // namespace p3::data
