#include "p3data/game_texts.hpp"

#include <cstdio>
#include <cstring>

namespace p3::data {

namespace {

constexpr size_t kCount = size_t(GameText::Count);
constexpr size_t kMaxText = 4000;

// Where each text is in each known build: the address of its pointer, in the
// order of GameText. Found by matching, around each English pointer, the
// sequence of the texts' control codes and printf specifiers in the other
// build's tables (they are the same in every language). A few tables of the
// English build are filled at start (0x46BFC0: mov [slot], text): for them
// the address is that of the instruction's immediate (0x46BFE4...), which
// holds the text's address too; the Spanish build has them in its .data.
struct Build {
	uint32_t time_stamp;  // PE header.
	const char* name;
	std::array<uint32_t, kCount> slots;
};

const Build kBuilds[] = {
	{0x3F544D3C, "English GOG 1.1",
		{0x6A96E4, 0x6A974C, 0x6A966C, 0x6A9670, 0x6A9674, 0x6A9678, 0x6A967C, 0x6A9684, 0x6A9688, 0x6A968C,
			0x6A9690, 0x6A9694, 0x6A9698, 0x6A9658, 0x6A965C, 0x6A9660, 0x6A9664, 0x6A9668, 0x6A9630, 0x69EE90,
			0x69EE98, 0x69EED4, 0x69EED8, 0x69EF68, 0x69EF7C, 0x6C0DA0, 0x6C0DA4, 0x6C0DA8, 0x6C0DAC, 0x6C0DB0,
			0x6C0DB4, 0x6C0DB8, 0x6C0DBC, 0x6C0DC0, 0x6C0DC4, 0x6C0DC8, 0x6C0DCC,
			0x6C0BB0, 0x6C0BB4, 0x6C0BB8, 0x6C0BBC, 0x6C0BC0, 0x6C0BC4, 0x6C0BC8, 0x6C0BCC, 0x6C0BD0, 0x6C0BD4,
			0x6C0BD8, 0x6C0BDC, 0x6C0BE0, 0x6C0BE4, 0x6C0BE8, 0x6C0BEC, 0x6C0BF0, 0x6C0BF4, 0x6C0BF8, 0x6C0BFC,
			0x6C0CC4, 0x6C0CC8, 0x6C0CCC, 0x6C0CD0, 0x699880, 0x69988C, 0x699894, 0x6998A0, 0x6998AC, 0x6C1ECC,
			0x6C1ED0, 0x6C1ED4, 0x6C1EB8, 0x6C2560, 0x6C1E90, 0x69DA34, 0x699914, 0x699940, 0x6B28C8, 0x6A1238,
			0x6A123C, 0x6A1240, 0x6A1244, 0x69A934, 0x69A938, 0x69A93C, 0x69A940, 0x69A944, 0x69A948, 0x69A94C,
			0x6A1D78, 0x6A1D7C, 0x6AA178, 0x6AA17C, 0x6A16E4, 0x6A16F0, 0x6A16F8, 0x6A171C, 0x6A1720, 0x6A1724,
			0x6A1728, 0x6A175C, 0x6A1760, 0x6A1764, 0x6A1768, 0x6A16C0, 0x6A16C4, 0x6A16F4, 0x46BFE4, 0x46BFEE,
			0x46BFF8, 0x46C002, 0x46C00C, 0x46C020, 0x46C02A}},
	{0x3F8814FA, "Spanish",
		{0x6A9CBC, 0x6A9D24, 0x6A9C44, 0x6A9C48, 0x6A9C4C, 0x6A9C50, 0x6A9C54, 0x6A9C5C, 0x6A9C60, 0x6A9C64,
			0x6A9C68, 0x6A9C6C, 0x6A9C70, 0x6A9C30, 0x6A9C34, 0x6A9C38, 0x6A9C3C, 0x6A9C40, 0x6A9C08, 0x69EAA0,
			0x69EAA8, 0x69EAE4, 0x69EAE8, 0x69EB78, 0x69EB8C, 0x6C0F8C, 0x6C0F90, 0x6C0F94, 0x6C0F98, 0x6C0F9C,
			0x6C0FA0, 0x6C0FA4, 0x6C0FA8, 0x6C0FAC, 0x6C0FB0, 0x6C0FB4, 0x6C0FB8,
			0x6C0D70, 0x6C0D74, 0x6C0D78, 0x6C0D7C, 0x6C0D80, 0x6C0D84, 0x6C0D88, 0x6C0D8C, 0x6C0D90, 0x6C0D94,
			0x6C0D98, 0x6C0D9C, 0x6C0DA0, 0x6C0DA4, 0x6C0DA8, 0x6C0DAC, 0x6C0DB0, 0x6C0DB4, 0x6C0DB8, 0x6C0DBC,
			0x6C0E80, 0x6C0E84, 0x6C0E88, 0x6C0E8C, 0x698D10, 0x698D1C, 0x698D2C, 0x698D3C, 0x698D48, 0x6C249C,
			0x6C24A0, 0x6C24A4, 0x6C2488, 0x6C2C08, 0x6C2460, 0x69D4B4, 0x698DCC, 0x698DFC, 0x6B32B0, 0x6A0F88,
			0x6A0F8C, 0x6A0F90, 0x6A0F94, 0x69A04C, 0x69A050, 0x69A054, 0x69A058, 0x69A05C, 0x69A060, 0x69A064,
			0x6A1CA0, 0x6A1CA4, 0x6AA780, 0x6AA784, 0x6A15B4, 0x6A15C0, 0x6A15C8, 0x6A15EC, 0x6A15F0, 0x6A15F4,
			0x6A15F8, 0x6A162C, 0x6A1630, 0x6A1634, 0x6A1638, 0x6A1590, 0x6A1594, 0x6A15C4, 0x6A1654, 0x6A1658,
			0x6A165C, 0x6A1660, 0x6A1664, 0x6A166C, 0x6A1670}},
};

uint32_t u32(const std::vector<uint8_t>& d, size_t o) {
	uint32_t v = 0;
	if (o + 4 <= d.size()) {
		std::memcpy(&v, d.data() + o, 4);
	}
	return v;
}

uint16_t u16(const std::vector<uint8_t>& d, size_t o) {
	uint16_t v = 0;
	if (o + 2 <= d.size()) {
		std::memcpy(&v, d.data() + o, 2);
	}
	return v;
}

// A PE file's sections, to go from a virtual address to a file offset.
struct Sections {
	struct Section {
		uint32_t address;
		uint32_t size;
		uint32_t raw;
	};
	uint32_t base = 0;
	uint32_t time_stamp = 0;
	std::vector<Section> list;

	bool parse(const std::vector<uint8_t>& d) {
		if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z') {
			return false;
		}
		const size_t pe = u32(d, 0x3C);
		if (pe + 0x18 > d.size() || std::memcmp(d.data() + pe, "PE\0\0", 4) != 0) {
			return false;
		}
		time_stamp = u32(d, pe + 8);
		const size_t count = u16(d, pe + 6);
		const size_t optional = u16(d, pe + 20);
		base = u32(d, pe + 24 + 28);
		for (size_t i = 0; i < count; ++i) {
			const size_t o = pe + 24 + optional + 40 * i;
			const uint32_t virtual_size = u32(d, o + 8);
			const uint32_t raw_size = u32(d, o + 16);
			list.push_back({base + u32(d, o + 12), virtual_size < raw_size ? virtual_size : raw_size, u32(d, o + 20)});
		}
		return !list.empty();
	}

	// File offset of a virtual address, or 0.
	size_t offset(uint32_t va) const {
		for (const Section& s : list) {
			if (va >= s.address && va < s.address + s.size) {
				return s.raw + (va - s.address);
			}
		}
		return 0;
	}
};

// Windows-1252 bytes 0x80..0x9F as Unicode (0: unused, kept as '?').
constexpr uint16_t kCp1252[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
	0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161,
	0x203A, 0x0153, 0, 0x017E, 0x0178};

void append_utf8(std::string& out, uint32_t c) {
	if (c < 0x80) {
		out += char(c);
	} else if (c < 0x800) {
		out += char(0xC0 | (c >> 6));
		out += char(0x80 | (c & 0x3F));
	} else {
		out += char(0xE0 | (c >> 12));
		out += char(0x80 | ((c >> 6) & 0x3F));
		out += char(0x80 | (c & 0x3F));
	}
}

bool is_digit(char c) {
	return c >= '0' && c <= '9';
}

}  // namespace

std::string clean_game_text(const std::string& raw) {
	std::string out;
	for (size_t i = 0; i < raw.size(); ++i) {
		const uint8_t c = uint8_t(raw[i]);
		if (c == '\\' && i + 1 < raw.size()) {
			const char code = raw[i + 1];
			if (code == 'f' || code == 't') {
				// \fN_ (font) and \tNNNN_: the number and the '_', if there is one.
				size_t j = i + 2;
				while (j < raw.size() && is_digit(raw[j])) {
					++j;
				}
				if (j < raw.size() && raw[j] == '_') {
					++j;
				}
				i = j - 1;
				continue;
			}
			if ((code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z')) {
				// \c \l \r (alignment), \C (the coin symbol), \d, \B, \L, \S...
				++i;
				continue;
			}
			out += '\\';
			continue;
		}
		if (c >= 0x80 && c < 0xA0) {
			const uint16_t u = kCp1252[c - 0x80];
			append_utf8(out, u ? u : '?');
		} else {
			append_utf8(out, c);
		}
	}
	const size_t first = out.find_first_not_of(' ');
	const size_t last = out.find_last_not_of(' ');
	return first == std::string::npos ? std::string() : out.substr(first, last - first + 1);
}

std::unique_ptr<GameTexts> GameTexts::load(const std::string& path, std::string* error) {
	// stdio, not iostream: on the Vita iostream adds 300 KB to the VPK.
	std::FILE* file = std::fopen(path.c_str(), "rb");
	if (!file) {
		if (error) {
			*error = "can't open " + path;
		}
		return nullptr;
	}
	std::vector<uint8_t> exe;
	if (std::fseek(file, 0, SEEK_END) == 0) {
		const long size = std::ftell(file);
		if (size > 0 && std::fseek(file, 0, SEEK_SET) == 0) {
			exe.resize(size_t(size));
			exe.resize(std::fread(exe.data(), 1, exe.size(), file));
		}
	}
	std::fclose(file);
	return parse(exe, error);
}

std::unique_ptr<GameTexts> GameTexts::parse(const std::vector<uint8_t>& exe, std::string* error) {
	Sections sections;
	if (!sections.parse(exe)) {
		if (error) {
			*error = "not a Windows executable";
		}
		return nullptr;
	}
	const Build* build = nullptr;
	for (const Build& b : kBuilds) {
		if (b.time_stamp == sections.time_stamp) {
			build = &b;
		}
	}
	if (!build) {
		if (error) {
			char stamp[16];
			std::snprintf(stamp, sizeof(stamp), "%08X", unsigned(sections.time_stamp));
			*error = std::string("unknown build of Patrician3.exe (time stamp ") + stamp + ")";
		}
		return nullptr;
	}
	auto texts = std::make_unique<GameTexts>();
	texts->build_ = build->name;
	for (size_t i = 0; i < kCount; ++i) {
		const size_t slot = sections.offset(build->slots[i]);
		const size_t text = slot ? sections.offset(u32(exe, slot)) : 0;
		if (!text) {
			if (error) {
				*error = "a text of Patrician3.exe is out of place";
			}
			return nullptr;
		}
		size_t end = text;
		while (end < exe.size() && end - text < kMaxText && exe[end] != 0) {
			++end;
		}
		texts->raw_[i] = std::string(exe.begin() + long(text), exe.begin() + long(end));
		texts->texts_[i] = clean_game_text(texts->raw_[i]);
	}
	return texts;
}

std::string GameTexts::sprintf_raw(const std::string& text, const std::vector<std::string>& args) {
	std::string out;
	size_t next = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] != '%' || i + 1 >= text.size()) {
			out += text[i];
			continue;
		}
		if (text[i + 1] == '%') {
			out += '%';
			++i;
			continue;
		}
		size_t j = i + 1;
		size_t index = next;
		bool positional = false;
		if (text[j] == '$') {
			size_t k = j + 1;
			size_t n = 0;
			while (k < text.size() && is_digit(text[k])) {
				n = n * 10 + size_t(text[k] - '0');
				++k;
			}
			if (k < text.size() && text[k] == '$' && n > 0) {
				index = n - 1;
				j = k + 1;
				positional = true;
			}
		}
		if (j < text.size() && text[j] == 's') {
			if (index < args.size()) {
				out += args[index];
			}
			if (!positional) {
				++next;
			}
			i = j;
			continue;
		}
		out += text[i];
	}
	return out;
}

std::string GameTexts::format(const std::string& text, const std::vector<std::string>& args) {
	std::string out;
	size_t next = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] != '%' || i + 1 >= text.size()) {
			out += text[i];
			continue;
		}
		if (text[i + 1] == '%') {
			out += '%';
			++i;
			continue;
		}
		size_t j = i + 1;
		size_t index = next;
		if (text[j] == '$') {
			// Positional, as some texts have it: %$5$d is the fifth argument.
			size_t k = j + 1;
			size_t n = 0;
			while (k < text.size() && is_digit(text[k])) {
				n = n * 10 + size_t(text[k] - '0');
				++k;
			}
			if (k < text.size() && text[k] == '$' && n > 0) {
				index = n - 1;
				j = k + 1;
			}
		}
		while (j < text.size() && std::strchr("-+ #0123456789.l", text[j])) {
			++j;
		}
		if (j < text.size() && std::strchr("sdiuxXcf", text[j])) {
			out += index < args.size() ? args[index] : "";
			if (index == next) {
				++next;
			}
			i = j;
			continue;
		}
		out += text[i];
	}
	// The name parts left empty leave runs of spaces.
	std::string collapsed;
	for (const char c : out) {
		if (c == ' ' && !collapsed.empty() && collapsed.back() == ' ') {
			continue;
		}
		collapsed += c;
	}
	const size_t first = collapsed.find_first_not_of(' ');
	const size_t last = collapsed.find_last_not_of(' ');
	return first == std::string::npos ? std::string() : collapsed.substr(first, last - first + 1);
}

}  // namespace p3::data
