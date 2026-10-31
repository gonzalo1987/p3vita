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
// build's tables (they are the same in every language).
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
			0x6C0DB4, 0x6C0DB8, 0x6C0DBC, 0x6C0DC0, 0x6C0DC4, 0x6C0DC8, 0x6C0DCC}},
	{0x3F8814FA, "Spanish",
		{0x6A9CBC, 0x6A9D24, 0x6A9C44, 0x6A9C48, 0x6A9C4C, 0x6A9C50, 0x6A9C54, 0x6A9C5C, 0x6A9C60, 0x6A9C64,
			0x6A9C68, 0x6A9C6C, 0x6A9C70, 0x6A9C30, 0x6A9C34, 0x6A9C38, 0x6A9C3C, 0x6A9C40, 0x6A9C08, 0x69EAA0,
			0x69EAA8, 0x69EAE4, 0x69EAE8, 0x69EB78, 0x69EB8C, 0x6C0F8C, 0x6C0F90, 0x6C0F94, 0x6C0F98, 0x6C0F9C,
			0x6C0FA0, 0x6C0FA4, 0x6C0FA8, 0x6C0FAC, 0x6C0FB0, 0x6C0FB4, 0x6C0FB8}},
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
	return out;
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
		texts->texts_[i] = clean_game_text(std::string(exe.begin() + long(text), exe.begin() + long(end)));
	}
	return texts;
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
		if (j < text.size() && std::strchr("sdiuxXc", text[j])) {
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
