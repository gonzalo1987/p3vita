#include "p3data/game_texts.hpp"

#include <algorithm>
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
			0x46BFF8, 0x46C002, 0x46C00C, 0x46C020, 0x46C02A, 0x6A2240, 0x6A2244, 0x6A2248, 0x6A224C, 0x6A2250,
			0x6A2254, 0x6A2258, 0x6A225C, 0x6A2260, 0x6A2264, 0x6A2268, 0x6A226C, 0x6A2270, 0x6A2274, 0x6A2278,
			0x6AA180, 0x6A174C, 0x6A1750, 0x6A1754, 0x6A1758,
			0x6A227C, 0x6A2280, 0x6A2284, 0x6A2288, 0x6A228C, 0x6A2290, 0x6A2294, 0x6AA1A4, 0x6AA1A8, 0x6AA1AC,
			0x6B95E0, 0x6B95E4, 0x6B95E8, 0x6B95EC, 0x6B95F0, 0x6B95F4, 0x6B95F8, 0x6B95FC, 0x6B9600, 0x6B9604,
			0x6B9608, 0x6B960C, 0x6B9610, 0x6B9614, 0x6B9618, 0x6B961C, 0x6B9620, 0x6B9624, 0x6B9628, 0x6B962C,
			0x6B9630, 0x6B1750, 0x6B1754, 0x6B1758, 0x6B175C, 0x6B1760, 0x6B1764, 0x6B1768, 0x6B176C, 0x6B1770,
			0x6B1774, 0x6B1778, 0x6B177C, 0x6B1780, 0x6B1784, 0x6B1788, 0x6B178C, 0x6B1790, 0x6B1794, 0x6B1F20,
			0x6B1F24, 0x6B1F28, 0x6B1F2C, 0x6B1F30, 0x6B1F34, 0x6B1F38, 0x6B1F3C, 0x6B1F40, 0x6B1F44, 0x6B1F48,
			0x6B1F4C, 0x6B1F50, 0x6B1F54, 0x6B1F58, 0x6B1F5C, 0x6B1F60, 0x6B1F64, 0x6B1F68, 0x6B1F6C, 0x6B1F70,
			0x6B1F74, 0x6B1F78, 0x6B1F7C, 0x6B1F80, 0x6B1F84, 0x6B1F88, 0x6B1F8C, 0x6B1F90, 0x6B1F94, 0x6C0C00,
			0x6C0C04, 0x6C0C08, 0x6C0C0C, 0x6C0CB4, 0x6B72DC, 0x6B72E0, 0x6B72E4, 0x6B72E8, 0x6B72EC, 0x6B72F0,
			0x6B72F4, 0x6B72F8, 0x6B72FC, 0x6B7300,
			0x69AE20, 0x69AE24, 0x69AE2C, 0x69AE30, 0x69AE48, 0x69AE4C, 0x69AE50, 0x69A5F8, 0x69B28C, 0x69B290,
			0x69B2A8, 0x69B2AC, 0x69B15C, 0x69B160, 0x69B164, 0x69B168, 0x69B16C, 0x69B170, 0x6B20E8, 0x6B20EC,
			0x69D8AC, 0x69D8B0, 0x69D8C8, 0x69D8CC, 0x69D8D0, 0x69D8D4, 0x69D8DC, 0x69D8E0, 0x69D900, 0x69D904,
			0x69D908, 0x69DA5C, 0x69C9E0, 0x69C9DC, 0x6999B4, 0x6999C8, 0x6999D4, 0x6A5CB0, 0x6A5CB4, 0x6A5CC4,
			0x6A5D00, 0x6A5D04, 0x6A5D0C, 0x6A57CC, 0x6A5848, 0x6A5854, 0x6A5858, 0x6A585C, 0x6A5880,
			0x699874, 0x6AA33C, 0x6AA340,
			0x699B80, 0x699B98, 0x699BAC, 0x699A78, 0x699AC0, 0x699B48, 0x699D00,
			0x6A5CB8, 0x6A5CBC, 0x6A5CC0, 0x6A5CC8, 0x6A5CCC, 0x6A5CD0, 0x6A5CD4, 0x6A5CD8, 0x6A5CDC, 0x6A5CE4,
			0x6A5CEC, 0x6A5CF0, 0x6A5CF4, 0x6A5CF8, 0x6A5CFC, 0x6A5D08, 0x6A5D10, 0x6A5D14, 0x6A5D18,
			0x6A1770, 0x6A1774, 0x6AA194, 0x6A1D98, 0x6A1D9C, 0x6A1DA0, 0x6A1DA4, 0x6A1DA8, 0x6A1DAC, 0x6A1DB0,
			0x6A1DB4, 0x6A1DB8, 0x6AA198, 0x6AA19C, 0x6AA1A0, 0x6AA1E4,
			0x69B688, 0x69B68C, 0x69B690, 0x69B694, 0x69B698, 0x69B69C, 0x69B6A0, 0x69B6A4, 0x69B6A8, 0x6B2A24,
			0x6B2A28, 0x6B2A2C,
			0x69A900, 0x69A904, 0x69A908, 0x69A90C, 0x69A910, 0x69A914, 0x69A918, 0x69A91C, 0x69A920, 0x69A924,
			0x69A928, 0x69A92C, 0x69A930, 0x69A950, 0x69A954, 0x69A6DC, 0x69A6E0, 0x69A6E4, 0x69A6E8, 0x69A6EC,
			0x69A6F0, 0x69A6F4, 0x69A6F8, 0x69A714, 0x69A718, 0x69A71C, 0x69A738, 0x69A73C, 0x69A740, 0x69A758,
			0x69A75C, 0x69A760, 0x69A77C, 0x69A780, 0x69A784, 0x69A79C, 0x69A7A0, 0x69A7A4, 0x69A7BC, 0x69A7C0,
			0x69A7C4, 0x69A7DC, 0x69A7E0, 0x69A7E4, 0x69A990, 0x69A994, 0x69A998, 0x69A99C, 0x69A9A0, 0x69AA10,
			0x69AA14, 0x69AA18, 0x69AA1C, 0x69AA20, 0x69AA24, 0x69AA28, 0x69AA2C, 0x69AA30, 0x69AAC0, 0x69AAC4,
			0x69AAC8, 0x69AACC, 0x69AAD0, 0x69AAD4, 0x69AAD8, 0x69AADC, 0x69AAE0, 0x69AAE4, 0x69A608,
			0x69B360, 0x69B364, 0x69B368, 0x69B294, 0x6B2D4C, 0x6AEF28, 0x6AEF2C, 0x6B2D3C, 0x6AEE34, 0x6AEE38,
			0x6AEE3C, 0x6AEE40, 0x6AEE44, 0x69AAE8, 0x69AAEC, 0x69AAF0, 0x69AAF4, 0x69AAF8, 0x69AC64,
			0x69AC68, 0x69AC6C, 0x6AEF18, 0x6AEF30, 0x6AEF34, 0x6AEF3C, 0x6AEF40, 0x6AEF44, 0x69B138}},
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
			0x6A165C, 0x6A1660, 0x6A1664, 0x6A166C, 0x6A1670, 0x6A21B0, 0x6A21B4, 0x6A21B8, 0x6A21BC, 0x6A21C0,
			0x6A21C4, 0x6A21C8, 0x6A21CC, 0x6A21D0, 0x6A21D4, 0x6A21D8, 0x6A21DC, 0x6A21E0, 0x6A21E4, 0x6A21E8,
			0x6AA788, 0x6A161C, 0x6A1620, 0x6A1624, 0x6A1628,
			0x6A21EC, 0x6A21F0, 0x6A21F4, 0x6A21F8, 0x6A21FC, 0x6A2200, 0x6A2204, 0x6AA7AC, 0x6AA7B0, 0x6AA7B4,
			0x6BA060, 0x6BA064, 0x6BA068, 0x6BA06C, 0x6BA070, 0x6BA074, 0x6BA078, 0x6BA07C, 0x6BA080, 0x6BA084,
			0x6BA088, 0x6BA08C, 0x6BA090, 0x6BA094, 0x6BA098, 0x6BA09C, 0x6BA0A0, 0x6BA0A4, 0x6BA0A8, 0x6BA0AC,
			0x6BA0B0, 0x6B2048, 0x6B204C, 0x6B2050, 0x6B2054, 0x6B2058, 0x6B205C, 0x6B2060, 0x6B2064, 0x6B2068,
			0x6B206C, 0x6B2070, 0x6B2074, 0x6B2078, 0x6B207C, 0x6B2080, 0x6B2084, 0x6B2088, 0x6B208C, 0x6B2860,
			0x6B2864, 0x6B2868, 0x6B286C, 0x6B2870, 0x6B2874, 0x6B2878, 0x6B287C, 0x6B2880, 0x6B2884, 0x6B2888,
			0x6B288C, 0x6B2890, 0x6B2894, 0x6B2898, 0x6B289C, 0x6B28A0, 0x6B28A4, 0x6B28A8, 0x6B28AC, 0x6B28B0,
			0x6B28B4, 0x6B28B8, 0x6B28BC, 0x6B28C0, 0x6B28C4, 0x6B28C8, 0x6B28CC, 0x6B28D0, 0x6B28D4, 0x6C0DC0,
			0x6C0DC4, 0x6C0DC8, 0x6C0DCC, 0x6C0E70, 0x6B7E48, 0x6B7E4C, 0x6B7E50, 0x6B7E54, 0x6B7E58, 0x6B7E5C,
			0x6B7E60, 0x6B7E64, 0x6B7E68, 0x6B7E6C,
			0x69A5B0, 0x69A5B4, 0x69A5BC, 0x69A5C0, 0x69A5D8, 0x69A5DC, 0x69A5E0, 0x699CD4, 0x69AA90, 0x69AA94,
			0x69AAB0, 0x69AAB4, 0x69A93C, 0x69A940, 0x69A944, 0x69A948, 0x69A94C, 0x69A950, 0x6B2A38, 0x6B2A3C,
			0x69D32C, 0x69D330, 0x69D348, 0x69D34C, 0x69D350, 0x69D354, 0x69D35C, 0x69D360, 0x69D380, 0x69D384,
			0x69D388, 0x69D4DC, 0x69C3AC, 0x69C3A8, 0x698E8C, 0x698EA4, 0x698EB0, 0x6A5D78, 0x6A5D7C, 0x6A5D8C,
			0x6A5DC8, 0x6A5DCC, 0x6A5DD4, 0x6A581C, 0x6A5898, 0x6A58A4, 0x6A58A8, 0x6A58AC, 0x6A58D0,
			0x698D00, 0x6AA948, 0x6AA94C,
			0x69909C, 0x6990B8, 0x6990D0, 0x698F84, 0x698FD4, 0x699060, 0x699220,
			0x6A5D80, 0x6A5D84, 0x6A5D88, 0x6A5D90, 0x6A5D94, 0x6A5D98, 0x6A5D9C, 0x6A5DA0, 0x6A5DA4, 0x6A5DAC,
			0x6A5DB4, 0x6A5DB8, 0x6A5DBC, 0x6A5DC0, 0x6A5DC4, 0x6A5DD0, 0x6A5DD8, 0x6A5DDC, 0x6A5DE0,
			0x6A1640, 0x6A1644, 0x6AA79C, 0x6A1CC0, 0x6A1CC4, 0x6A1CC8, 0x6A1CCC, 0x6A1CD0, 0x6A1CD4, 0x6A1CD8,
			0x6A1CDC, 0x6A1CE0, 0x6AA7A0, 0x6AA7A4, 0x6AA7A8, 0x6AA7EC,
			0x69AEE0, 0x69AEE4, 0x69AEE8, 0x69AEEC, 0x69AEF0, 0x69AEF4, 0x69AEF8, 0x69AEFC, 0x69AF00, 0x6B3430,
			0x6B3434, 0x6B3438,
			0x69A018, 0x69A01C, 0x69A020, 0x69A024, 0x69A028, 0x69A02C, 0x69A030, 0x69A034, 0x69A038, 0x69A03C,
			0x69A040, 0x69A044, 0x69A048, 0x69A068, 0x69A06C, 0x699DCC, 0x699DD0, 0x699DD4, 0x699DD8, 0x699DDC,
			0x699DE0, 0x699DE4, 0x699DE8, 0x699E04, 0x699E08, 0x699E0C, 0x699E28, 0x699E2C, 0x699E30, 0x699E4C,
			0x699E50, 0x699E54, 0x699E70, 0x699E74, 0x699E78, 0x699E94, 0x699E98, 0x699E9C, 0x699EB8, 0x699EBC,
			0x699EC0, 0x699EDC, 0x699EE0, 0x699EE4, 0x69A0B0, 0x69A0B4, 0x69A0B8, 0x69A0BC, 0x69A0C0, 0x69A124,
			0x69A128, 0x69A12C, 0x69A130, 0x69A134, 0x69A138, 0x69A13C, 0x69A140, 0x69A144, 0x69A1CC, 0x69A1D0,
			0x69A1D4, 0x69A1D8, 0x69A1DC, 0x69A1E0, 0x69A1E4, 0x69A1E8, 0x69A1EC, 0x69A1F0, 0x699CE8,
			0x69AB88, 0x69AB8C, 0x69AB90, 0x69AA98, 0x6B37A4, 0x6AF6E4, 0x6AF6E8, 0x6B3798, 0x6AF5C8, 0x6AF5CC,
			0x6AF5D0, 0x6AF5D4, 0x6AF5D8, 0x69A1F4, 0x69A1F8, 0x69A1FC, 0x69A200, 0x69A204, 0x69A3B0,
			0x69A3B4, 0x69A3B8, 0x6AF6D4, 0x6AF6EC, 0x6AF6F0, 0x6AF6F8, 0x6AF6FC, 0x6AF700, 0x69A918}},
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

// The static initializers that fill tables of text pointers at start (the
// CRT runs them before the game; in the file those slots are 0): English
// 0x46BD80 (0x6A0BC8...) and 0x46BFC0 (0x6A1778...), and the Spanish exe's.
// Their code is only «mov eax, [m]», «mov [m], eax», «mov dword [m], imm»
// and a ret: static_slots reads the slots they set.
struct Initializers {
	uint32_t time_stamp;
	uint32_t starts[2];
};
constexpr Initializers kInitializers[] = {
	{0x3F544D3C, {0x46BD80, 0x46BFC0}},
	{0x3F8814FA, {0x46BE60, 0x46C0A0}},
};

// The texts by address (GameTexts::raw_at): {english, spanish}; with the bit
// kDirect the string is at the address, without it the address holds a
// pointer to it.
struct AddressPair {
	uint32_t english;
	uint32_t spanish;
};
const AddressPair kTextAddresses[] = {
#include "p3data/game_text_addresses.inc"
};
constexpr uint32_t kDirect = 0x80000000;

// The texts' cache (GameTexts::save_cache): "P3TX", u32 version, u32 the
// build's PE time stamp, u32 a hash of p3vita's slots for that build, u32 the
// number of texts, and each text as u32 size and its raw bytes.
constexpr char kCacheMagic[4] = {'P', '3', 'T', 'X'};
constexpr uint32_t kCacheVersion = 21;  // 4: with the buttons' tooltips (the string table, GameTexts::resource); 5: the new game window's; 6: «Main menu»; 7: the window's sex, start, map and home town's; 8: the goals' times and the map choice's; 9: the trade route windows'; 10: the letter 0x83's (operation 0x77); 11: the bath house's; 12: the guild's; 13: the offers' window's; 14: the weaponsmith's; 15: the tavern's; 16: the arsenal's and the town hall's; 17: the town hall's side menu's; 18: the tavern pages' layout; 19: the tavern's captain hired, weapons dealer's page and burglar's list; the lender's side menu; 20: the lender's titles, the church's, guild's, weaponsmith's and shipyard's side menus; 21: the church's pages.

// The tooltips read from the string table: 20000.. (the scripts' «ToolTip=»; 20062 the last).
constexpr uint32_t kTooltipFirst = 20000;
constexpr uint32_t kTooltipCount = 64;
constexpr uint32_t kResourceKey = GameTexts::kResourceKey;

// The slots the build's static initializers set, and the pointer each gets.
std::vector<std::pair<uint32_t, uint32_t>> static_slots(const std::vector<uint8_t>& exe, const Sections& sections,
	uint32_t time_stamp) {
	std::vector<std::pair<uint32_t, uint32_t>> out;
	for (const Initializers& init : kInitializers) {
		if (init.time_stamp != time_stamp) {
			continue;
		}
		for (uint32_t start : init.starts) {
			size_t p = sections.offset(start);
			uint32_t eax = 0;
			for (int n = 0; p && n < 200 && p + 10 <= exe.size(); ++n) {
				const uint8_t op = exe[p];
				if (op == 0xA1) {  // mov eax, [m]
					const size_t from = sections.offset(u32(exe, p + 1));
					eax = from ? u32(exe, from) : 0;
					p += 5;
				} else if (op == 0xA3) {  // mov [m], eax
					out.emplace_back(u32(exe, p + 1), eax);
					p += 5;
				} else if (op == 0xC7 && exe[p + 1] == 0x05) {  // mov dword [m], imm
					out.emplace_back(u32(exe, p + 2), u32(exe, p + 6));
					p += 10;
				} else {
					break;  // the ret (or something this reader doesn't know)
				}
			}
		}
	}
	return out;
}

const Build* find_build(uint32_t time_stamp) {
	for (const Build& b : kBuilds) {
		if (b.time_stamp == time_stamp) {
			return &b;
		}
	}
	return nullptr;
}

// FNV-1a of the build's slots and texts by address: a cache made with other
// ones is not used.
uint32_t slots_hash(const Build& build) {
	uint32_t hash = 2166136261u;
	auto add = [&](uint32_t v) {
		for (int k = 0; k < 4; ++k) {
			hash ^= (v >> (8 * k)) & 0xFF;
			hash *= 16777619u;
		}
	};
	for (uint32_t slot : build.slots) {
		add(slot);
	}
	const bool spanish = std::strcmp(build.name, "Spanish") == 0;
	for (const AddressPair& pair : kTextAddresses) {
		add(spanish ? pair.spanish : pair.english);
	}
	return hash;
}

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
	for (int k = 0; k < 4; ++k) {
		out.push_back(uint8_t(v >> (8 * k)));
	}
}

// String `id` of the exe's string table (the resources' RT_STRING, blocks of 16: block id / 16 + 1, the first
// language), in Windows-1252 ('?' for what it hasn't); empty if it isn't there.
std::string resource_string(const std::vector<uint8_t>& exe, const Sections& sections, uint32_t id) {
	const size_t pe = u32(exe, 0x3C);
	const uint32_t rva = u32(exe, pe + 24 + 96 + 8 * 2);  // The data directory's resource table.
	const size_t base = rva ? sections.offset(sections.base + rva) : 0;
	if (!base) {
		return "";
	}
	// A directory's entry with that id (the high bit of its offset: another directory).
	auto find = [&](size_t dir, uint32_t want) -> uint32_t {
		const size_t count = size_t(u16(exe, base + dir + 12)) + u16(exe, base + dir + 14);
		for (size_t k = 0; k < count && base + dir + 16 + 8 * k + 8 <= exe.size(); ++k) {
			if (u32(exe, base + dir + 16 + 8 * k) == want) {
				return u32(exe, base + dir + 16 + 8 * k + 4);
			}
		}
		return 0;
	};
	const uint32_t types = find(0, 6);  // RT_STRING
	const uint32_t block = types & 0x80000000 ? find(types & 0x7FFFFFFF, id / 16 + 1) : 0;
	if (!(block & 0x80000000)) {
		return "";
	}
	const size_t languages = base + (block & 0x7FFFFFFF);
	if (u16(exe, languages + 12) + u16(exe, languages + 14) == 0) {
		return "";
	}
	const uint32_t leaf = u32(exe, languages + 16 + 4);
	size_t p = sections.offset(sections.base + u32(exe, base + (leaf & 0x7FFFFFFF)));
	if (!p) {
		return "";
	}
	for (uint32_t k = 0; k < id % 16 && p + 2 <= exe.size(); ++k) {
		p += 2 + 2 * size_t(u16(exe, p));
	}
	std::string out;
	const size_t n = u16(exe, p);
	for (size_t k = 0; k < n && p + 4 + 2 * k <= exe.size(); ++k) {
		const uint16_t c = u16(exe, p + 2 + 2 * k);
		uint8_t b = c < 0x100 && !(c >= 0x80 && c < 0xA0) ? uint8_t(c) : '?';
		for (int j = 0; j < 32 && c >= 0x100; ++j) {
			if (kCp1252[j] == c) {
				b = uint8_t(0x80 + j);
			}
		}
		out += char(b);
	}
	return out;
}

}  // namespace

void append_cp1252(std::string& out, uint8_t c) {
	if (c >= 0x80 && c < 0xA0) {
		const uint16_t u = kCp1252[c - 0x80];
		append_utf8(out, u ? u : '?');
	} else {
		append_utf8(out, c);
	}
}

std::string clean_game_text(const std::string& raw) {
	std::string out;
	for (size_t i = 0; i < raw.size(); ++i) {
		const uint8_t c = uint8_t(raw[i]);
		if (c == '\\' && i + 1 < raw.size()) {
			const char code = raw[i + 1];
			if (code == 'f' || code == 't' || code == 'h') {
				// \fN_ (font), \tNNNN_ (a picture by its number: kept) and \hN_ (a picture made for a letter): the
				// number and the '_', if there is one.
				size_t j = i + 2;
				while (j < raw.size() && is_digit(raw[j])) {
					++j;
				}
				if (code == 't' && j > i + 2) {
					out += kTextPicture;
					out.append(raw, i + 2, j - (i + 2));
					out += kTextPictureEnd;
				}
				if (j < raw.size() && raw[j] == '_') {
					++j;
				}
				i = j - 1;
				continue;
			}
			if (code == 'C' || code == 'B' || code == 'L') {
				out += code == 'C' ? kTextCoin : code == 'B' ? kTextBarrel : kTextLoad;
				++i;
				continue;
			}
			if ((code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z')) {
				// \c \l \r (alignment), \d, \S...
				++i;
				continue;
			}
			out += '\\';
			continue;
		}
		append_cp1252(out, c);
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
	texts->spanish_ = std::strcmp(build->name, "Spanish") == 0;
	texts->time_stamp_ = build->time_stamp;
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
	// The texts by address: one that can't be found stays empty; a slot with
	// a null pointer gives «(null)», as the game's sprintf prints it for %s;
	// a slot a static initializer sets, its pointer (static_slots).
	const std::vector<std::pair<uint32_t, uint32_t>> initialized = static_slots(exe, sections, build->time_stamp);
	for (const AddressPair& pair : kTextAddresses) {
		const uint32_t va = texts->spanish_ ? pair.spanish : pair.english;
		const size_t at = sections.offset(va & ~kDirect);
		uint32_t pointer = at && !(va & kDirect) ? u32(exe, at) : 0;
		for (const auto& [slot, value] : initialized) {
			if (!(va & kDirect) && slot == va) {
				pointer = value;
			}
		}
		const bool null_slot = at && !(va & kDirect) && pointer == 0;
		const size_t text = !at || null_slot ? 0 : (va & kDirect) ? at : sections.offset(pointer);
		std::string raw = null_slot ? "(null)" : "";
		if (text) {
			size_t end = text;
			while (end < exe.size() && end - text < kMaxText && exe[end] != 0) {
				++end;
			}
			raw.assign(exe.begin() + long(text), exe.begin() + long(end));
		}
		texts->by_address_.emplace_back(pair.english & ~kDirect, std::move(raw));
	}
	// The buttons' tooltips: the strings kTooltipFirst.. of the exe's string table (resources, RT_STRING), in
	// Windows-1252 as the other texts (the scripts' «ToolTip=20028»...).
	for (uint32_t id = kTooltipFirst; id < kTooltipFirst + kTooltipCount; ++id) {
		texts->by_address_.emplace_back(kResourceKey | id, resource_string(exe, sections, id));
	}
	std::sort(texts->by_address_.begin(), texts->by_address_.end());
	return texts;
}

bool GameTexts::same_as(const GameTexts& other, size_t* addresses) const {
	if (addresses) {
		*addresses = by_address_.size();
	}
	return build_ == other.build_ && time_stamp_ == other.time_stamp_ && spanish_ == other.spanish_ &&
		texts_ == other.texts_ && raw_ == other.raw_ && by_address_ == other.by_address_;
}

const std::string& GameTexts::raw_at(uint32_t english) const {
	static const std::string kNone;
	const auto it = std::lower_bound(by_address_.begin(), by_address_.end(), english,
		[](const std::pair<uint32_t, std::string>& entry, uint32_t key) { return entry.first < key; });
	return it != by_address_.end() && it->first == english ? it->second : kNone;
}

bool GameTexts::save_cache(const std::string& path) const {
	const Build* build = find_build(time_stamp_);
	if (!build) {
		return false;
	}
	std::vector<uint8_t> out(kCacheMagic, kCacheMagic + 4);
	put_u32(out, kCacheVersion);
	put_u32(out, time_stamp_);
	put_u32(out, slots_hash(*build));
	put_u32(out, uint32_t(kCount));
	for (const std::string& text : raw_) {
		put_u32(out, uint32_t(text.size()));
		out.insert(out.end(), text.begin(), text.end());
	}
	put_u32(out, uint32_t(by_address_.size()));
	for (const auto& [address, text] : by_address_) {
		put_u32(out, address);
		put_u32(out, uint32_t(text.size()));
		out.insert(out.end(), text.begin(), text.end());
	}
	// Written apart and then put in place: a cut write leaves no half cache.
	const std::string temporary = path + ".tmp";
	std::FILE* file = std::fopen(temporary.c_str(), "wb");
	if (!file) {
		return false;
	}
	const bool written = std::fwrite(out.data(), 1, out.size(), file) == out.size();
	if (std::fclose(file) != 0 || !written) {
		std::remove(temporary.c_str());
		return false;
	}
	std::remove(path.c_str());  // The Vita's rename doesn't replace.
	return std::rename(temporary.c_str(), path.c_str()) == 0;
}

std::unique_ptr<GameTexts> GameTexts::load_cache(const std::string& path, std::string* error) {
	auto fail = [&](const char* message) -> std::unique_ptr<GameTexts> {
		if (error) {
			*error = path + ": " + message;
		}
		return nullptr;
	};
	std::FILE* file = std::fopen(path.c_str(), "rb");
	if (!file) {
		return fail("can't open");
	}
	std::vector<uint8_t> data;
	uint8_t buffer[1 << 14];
	for (size_t n; (n = std::fread(buffer, 1, sizeof(buffer), file)) > 0;) {
		data.insert(data.end(), buffer, buffer + n);
	}
	std::fclose(file);
	if (data.size() < 20 || std::memcmp(data.data(), kCacheMagic, 4) != 0 || u32(data, 4) != kCacheVersion) {
		return fail("not a cache of the texts");
	}
	const Build* build = find_build(u32(data, 8));
	if (!build || u32(data, 12) != slots_hash(*build) || u32(data, 16) != kCount) {
		return fail("made for another version");
	}
	auto texts = std::make_unique<GameTexts>();
	size_t pos = 20;
	for (size_t i = 0; i < kCount; ++i) {
		const size_t size = u32(data, pos);
		if (pos + 4 + size > data.size() || size > kMaxText) {
			return fail("cut short");
		}
		texts->raw_[i] = std::string(data.begin() + long(pos + 4), data.begin() + long(pos + 4 + size));
		texts->texts_[i] = clean_game_text(texts->raw_[i]);
		pos += 4 + size;
	}
	const size_t by_address = pos + 4 <= data.size() ? u32(data, pos) : 0;
	// The texts by address and the tooltips of the string table (parse).
	if (by_address != sizeof(kTextAddresses) / sizeof(kTextAddresses[0]) + kTooltipCount) {
		return fail("made for another version");
	}
	pos += 4;
	for (size_t i = 0; i < by_address; ++i) {
		const uint32_t address = u32(data, pos);
		const size_t size = u32(data, pos + 4);
		if (pos + 8 + size > data.size() || size > kMaxText) {
			return fail("cut short");
		}
		texts->by_address_.emplace_back(address,
			std::string(data.begin() + long(pos + 8), data.begin() + long(pos + 8 + size)));
		pos += 8 + size;
	}
	texts->build_ = build->name;
	texts->spanish_ = std::strcmp(build->name, "Spanish") == 0;
	texts->time_stamp_ = build->time_stamp;
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
		if (j < text.size() && (text[j] == 's' || text[j] == 'd' || text[j] == 'i' || text[j] == 'u' || text[j] == 'c')) {
			if (index < args.size()) {
				out += args[index];
			}
			// The game's sprintf gives a conversion without %$N$ the argument
			// of its place among all the conversions, the positional ones
			// included (checked in the emulator: «(%$2$s) %$1$s %d00 %s %s»
			// takes the third, fourth and fifth).
			++next;
			(void)positional;
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
