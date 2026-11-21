// The campaigns' start (campaigns.hpp).

#include "p3data/campaigns.hpp"

#include <cstring>

#include "p3data/battle/pirate_nests.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/missions.hpp"
#include "p3data/new_game.hpp"
#include "p3data/savegame.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

using namespace global;

constexpr uint32_t kLocalPlayer = 0x6DF31C;  // The network session's local player (0 alone).

uint16_t merchant_count(const SaveGame& save) {
	const uint16_t n = save.globals.u16(kWorldMerchantCount);
	return n < save.merchants.size() ? n : uint16_t(save.merchants.size());
}

// A key of an ini section (lower case names), or `fallback`.
std::string ini_value(const MissionIni& ini, const std::string& section, const std::string& key,
	const std::string& fallback) {
	auto s = ini.find(section);
	if (s == ini.end()) {
		return fallback;
	}
	auto k = s->second.find(key);
	return k == s->second.end() ? fallback : k->second;
}

// GetPrivateProfileInt's: a sign and the leading digits, 0 without them.
long ini_number(const std::string& v) {
	size_t i = 0;
	const bool minus = !v.empty() && v[0] == '-';
	i = minus ? 1 : 0;
	long n = 0;
	for (; i < v.size() && v[i] >= '0' && v[i] <= '9' && n < 0x7FFFFFFF / 10; ++i) {
		n = n * 10 + (v[i] - '0');
	}
	return minus ? -n : n;
}

// The ini's «\n» made line breaks (CString::Replace, 0x64CE80).
std::string line_breaks(std::string text) {
	for (size_t at = text.find("\\n"); at != std::string::npos; at = text.find("\\n", at + 1)) {
		text.replace(at, 2, "\n");
	}
	return text;
}

}  // namespace

std::vector<CampaignEntry> read_campaigns(const CprArchive& archive) {
	const MissionIni ini = read_missions_ini(archive);
	std::vector<CampaignEntry> list;
	const long count = ini_number(ini_value(ini, "general", "campaigncount", "0")) - 1;
	for (long n = 1; n <= count; ++n) {
		const std::string section = "campaign_" + std::to_string(n);
		CampaignEntry e;
		e.name = ini_value(ini, section, "name", "");
		e.text = line_breaks(ini_value(ini, section, "text", "Default"));
		const long texture = ini_number(ini_value(ini, section, "texture", "-1"));
		e.texture = texture > 0 ? uint32_t(texture) : 0;
		list.push_back(std::move(e));
	}
	return list;
}

std::vector<CampaignEntry> read_tutorial_chapters(const CprArchive& archive, std::string* title) {
	// 0x4AB420 with the campaign 0.
	const MissionIni ini = read_missions_ini(archive);
	if (title) {
		*title = ini_value(ini, "campaign_0", "name", "");
	}
	std::vector<CampaignEntry> list;
	const long count = ini_number(ini_value(ini, "campaign_0", "questcount", "0"));
	for (long k = 0; k < count; ++k) {
		const std::string n = std::to_string(k);
		CampaignEntry e;
		e.name = ini_value(ini, "campaign_0", "questname_" + n, "");
		e.text = line_breaks(ini_value(ini, "campaign_0", "questtext_" + n, "Default"));
		const long texture = ini_number(ini_value(ini, "campaign_0", "questtexture_" + n, "-1"));
		e.texture = texture > 0 ? uint32_t(texture) : 0;
		list.push_back(std::move(e));
	}
	return list;
}

std::vector<TutorialChapter> read_tutorial(const CprArchive& archive) {
	const MissionIni ini = read_game_ini(archive, "scripts/tutorial_spa.ini", "scripts/tutorial_eng.ini");
	std::vector<TutorialChapter> chapters;
	const long count = ini_number(ini_value(ini, "gametutorialbase", "tutorialcount", "0"));
	for (long k = 0; k < count; ++k) {
		const std::string section = "gametutorialbase" + std::to_string(k);
		TutorialChapter c;
		c.save = ini_value(ini, section, "savegame", "");
		const long windows = ini_number(ini_value(ini, section, "windowcnt", "0"));
		for (long i = 0; i < windows; ++i) {
			const std::string n = std::to_string(i);
			TutorialWindow w;
			w.text = line_breaks(ini_value(ini, section, "windowtext" + n, ""));
			w.speech = ini_value(ini, section, "windowmp3_" + n, "");
			// 0x6372F5..0x63740B: «Rect» found, the four numbers after it (strtok and atoi).
			const std::string code = ini_value(ini, section, "windowcode" + n, "");
			const size_t at = code.find("Rect");
			if (at != std::string::npos) {
				int32_t v[4] = {};
				size_t pos = at + 4;
				for (int j = 0; j < 4; ++j) {
					while (pos < code.size() && (code[pos] == ' ' || code[pos] == ',')) {
						++pos;
					}
					v[j] = int32_t(ini_number(code.substr(pos)));
					while (pos < code.size() && code[pos] != ',' && code[pos] != ' ') {
						++pos;
					}
				}
				w.has_rect = true;
				w.x = v[0];
				w.y = v[1];
				w.w = v[2];
				w.h = v[3];
			}
			c.windows.push_back(std::move(w));
		}
		chapters.push_back(std::move(c));
	}
	return chapters;
}

void fn_5421e0(SaveGame& save, const uint8_t* operation) {
	uint32_t local = 0;
	uint32_t merchant = 0;
	std::memcpy(&local, operation + 4, 4);
	std::memcpy(&merchant, operation + 8, 4);
	if (merchant >= merchant_count(save)) {
		return;
	}
	save.merchants[merchant].memory.u16(0x8) = 0;
	// (0x542208: in a network game ([0x6DF2F0] & 0xE) the lobby's seats, 0x43F660..0x43E760: not in p3vita.)
	if (local == save.globals.u32(kLocalPlayer)) {
		save.globals.u32(kPlayer) = merchant;
		save.player = int(merchant);
		// (0x5422E2: the town view's marks of his offices: the interface.)
	}
}

void after_game_load(SaveGame& save, uint8_t mode, bool from_screen, const LoadOptions& options) {
	const uint16_t merchants = merchant_count(save);
	// 0x547985 (0x5476E0, after 0x549F40): the humans from the 20th merchant on wait for their players.
	for (uint16_t i = 0x14; i < merchants; ++i) {
		if (save.merchants[i].memory.u16(0x8) == 0) {
			save.merchants[i].memory.u16(0x8) = 0x8001;
		}
	}
	// 0x433747: the mode and the options (0x433752..0x43378B).
	save.settings.u8(0xD) = mode;
	save.settings.u8(0x1F) = options.resolution;
	save.settings.u8(0x24) = options.details;
	save.settings.u8(0x20) = options.colour_depth;
	// (0x43378E..0x4337CF: the humans still 0 from the 20th on counted for 0x54B230, the session's copy of the
	// settings, and its flag 0x10: none of the world.)
	if (!from_screen) {
		return;
	}
	// 0x4337E5: the first merchant from the 20th on with [+0x8] ≠ 0 and its bit 15.
	uint16_t taken = 0xFFFF;
	for (uint16_t i = 0x14; i < merchants; ++i) {
		const uint16_t kind = save.merchants[i].memory.u16(0x8);
		if (kind != 0 && (kind & 0x8000)) {
			taken = i;
			break;
		}
	}
	// 0x433820: the operation 0xA8 run at once (0x535760 → 0x5421E0) with the local player and him. Without one, the
	// game passes the screen's own address, past the merchants: nothing.
	uint8_t a8[0x14] = {0xA8};
	const uint32_t local = save.globals.u32(kLocalPlayer);
	const uint32_t who = taken < merchants ? taken : 0xFFFFFFFF;
	std::memcpy(a8 + 4, &local, 4);
	std::memcpy(a8 + 8, &who, 4);
	fn_5421e0(save, a8);
	// 0x433842: the operation 0xC8 queued (0x54AA70): the time's mode 0, the speed unchanged (−1).
	uint8_t c8[0x14];
	std::memset(c8, 0xFF, sizeof(c8));
	c8[0] = 0xC8;
	c8[1] = c8[2] = c8[3] = 0;
	std::memset(c8 + 0xC, 0, 4);
	tick::enqueue_operation(save, c8, sizeof(c8));
}

void start_campaign(SaveGame& save, uint8_t campaign, uint32_t speed) {
	// 0x43097C..0x4309D2.
	fn_4d64b0(save);
	tick::events_detail::fn_4d8e70(save, 0x1B);
	save.settings.u8(0xD) = 5;
	save.settings.u8(0x14) = campaign;
	save.settings.u32(0x4) = speed;
	missions_detail::fn_511270(save, true);
	campaigns_detail::fn_54a480(save);
}

void start_tutorial(SaveGame& save) {
	// 0x4ABF30.
	save.settings.u8(0xD) = 4;
	save.settings.u8(0x14) = 0;
}

namespace campaigns_detail {

void fn_54a480(SaveGame& save) {
	GlobalMemory& g = save.globals;
	// (0x54A4A3: [0x6DF2F0] & 0xC, a network game: nothing. p3vita has none.)
	fn_52e1a0(save, save.settings.u8(0x10));
	const uint32_t player = g.u32(kPlayer);
	if (player < merchant_count(save)) {
		// 0x4F9550 (on the merchant, the settings' [+0x11]): a human's factor by table 0x672E04.
		static constexpr uint32_t kCareful[3] = {0x3FA66666, 0x3F800000, 0x3F333333};  // 1.3, 1.0, 0.7
		MerchantMemory& m = save.merchants[player].memory;
		if (m.u16(0x8) == 0) {
			m.u32(0x464) = kCareful[save.settings.u8(0x11) % 3];
		}
	}
	const uint8_t level = save.settings.u8(0x13);
	const uint32_t made = 2u * level + 1u;
	for (uint32_t k = 0; k < 5; ++k) {
		const bool exists = save.pirates[k].has_value();
		if (k < made) {
			if (!exists) {
				fn_513720(save, k);
				const uint32_t number = tick::game_rand(save);
				const uint32_t town = tick::game_rand(save);
				fn_513d80(save, k, town, level, number);
			} else {
				save.pirates[k]->u8(0x14) = uint8_t(level & 3);
			}
		} else if (exists) {
			save.pirates[k]->u8(0x15) = 1;
		}
	}
}

}  // namespace campaigns_detail

}  // namespace p3::data
