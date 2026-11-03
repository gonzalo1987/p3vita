#include "p3data/letters.hpp"

#include <algorithm>

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/mail.hpp"

namespace p3::data {

namespace {

constexpr uint32_t kTownIds = 0x6DE4B8;  // u8 per town index: its number (scripts/StadtDaten.ini).
constexpr uint32_t kShipNameCount = 0x6DDB78;  // u16 [+0xD8] of the names object; [+0xDA] after it.

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
std::string table_town(const SaveGame& save, uint32_t town) {
	const uint32_t named = fix_game_bugs() ? save.globals.u8(kTownIds + town) : town;
	return named < save.opaque.town_names.size() ? save.opaque.town_names[named] : std::string();
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
	void add(const std::string& s) { out += s; }
	void add(uint32_t english) { out += t.raw_at(english); }
	void format(uint32_t english, const std::vector<std::string>& args) {
		out += GameTexts::sprintf_raw(t.raw_at(english), args);
	}
	void format(const std::string& text, const std::vector<std::string>& args) {
		out += GameTexts::sprintf_raw(text, args);
	}
	// [i * 4 + 0x6DDA00]: i is a town's index (see table_town).
	std::string town(uint32_t i) const { return table_town(save, i); }
	std::string first(uint32_t i) const { return game_first_name(save, i); }  // 0x512A60
	std::string family(uint32_t i) const { return game_family_name(save, i); }  // 0x512AE0
	// 0x512B20 (on the names object 0x6DDAA0): a name of the game's ship
	// names ([+0xD8] of them), then of the players' (up to [+0xDA]: none
	// after a load, 0x5121C0 undoes them, so not reproduced), and past them
	// a ship type's word by (~n) & 3 (0x6C0CB4: «snaikka», «crayer», «cog»,
	// «hulk»).
	std::string list_ship_name(uint32_t n) const {
		const uint32_t index = n & 0xFFFF;
		if (index < save.globals.u16(kShipNameCount)) {
			return index < save.ship_names.size() ? save.ship_names[index] : std::string();
		}
		if (index < save.globals.u16(kShipNameCount + 2)) {
			return std::string();
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
};

}  // namespace

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
	case 0x29:
	case 0x47:
	case 0x63:
	case 0x83:
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
	Composer c{save, texts, e, entry, {}};
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
	default:
		return std::nullopt;
	}
	return c.out;
}

}  // namespace p3::data
