// Muestra el contenido de una partida guardada: ciudades y su almacén,
// comerciantes y barcos.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/font.hpp"
#include "p3data/game_fixes.hpp"
#include "p3data/game_texts.hpp"
#include "p3data/letter_picture.hpp"
#include "p3data/letters.hpp"
#include "p3data/prices.hpp"
#include "p3data/lz.hpp"
#include "p3data/navigation.hpp"
#include "p3data/new_game.hpp"
#include "p3data/sailing.hpp"
#include "p3data/seamap.hpp"
#include "p3data/statistics.hpp"
#include "p3data/savegame.hpp"
#include "p3data/savegame_write.hpp"
#include "p3data/simulation.hpp"
#include "p3data/lz_compress.hpp"
#include "p3data/wares.hpp"
#include "stb_image_write.h"

using namespace p3::data;

namespace {

constexpr const char* kShipTypes[] = {"Snaikka", "Craier", "Kogge", "Holk"};

int usage() {
	std::fprintf(stderr,
		"uso: p3save <archivo.cpr> <partida> [ciudad | comerciantes | barcos | raw <salida>]\n"
		"       p3save <archivo.cpr> <partida> write <out.pat> [<out.raw>]\n"
		"       (--fix-game-bugs anywhere: with the fixes to the game's bugs, game_fixes.hpp)\n"
		"       p3save compress <in> <out>\n"
		"       p3save newgame <archivo.cpr> <out.raw> [--seed N] [--year Y] [--home I] [--settings-from SAVE.raw]\n"
		"         [--stop ops|advance|player] [--ships N] [--female] [--first NAME] [--family NAME]\n"
		"         [--window [--level L] [--start 1|2] [--speed S] [--goal G] [--goal-time T] [--write-settings F]]\n"
		"         [--map FILE.gmf]\n"
		"       p3save texts-cache <Patrician3.exe> <cache>\n"
		"       p3save texts <Patrician3.exe> [FIRST]\n"
		"       p3save <archivo.cpr> <partida> letters <Patrician3.exe> [merchant] [offers]\n"
		"       p3save <archivo.cpr> <partida> chronicle <Patrician3.exe>\n"
		"       p3save <archivo.cpr> <partida> letter-picture <entry> <out.png>\n"
		"       p3save <archivo.cpr> <partida> ticker <Patrician3.exe> <ticker_oracle.py's output>\n"
		"\n"
		"Sin más argumentos, lista las ciudades de la partida (p. ej. missav/1.pat).\n"
		"Con el número de una ciudad, muestra su almacén, producción y consumo.\n"
		"comerciantes y barcos los listan; raw guarda la partida descomprimida.\n"
		"write saves it again as the game does (version 0x85; with <out.raw>, also uncompressed),\n"
		"and the .pst of its statistics next to <out.pat>.\n"
		"<partida> is read from the archive or, if it isn't there, from the disk.\n"
		"compress compresses any file as the game does (lz_compress).\n"
		"texts-cache writes the viewer's texts' cache of the exe and checks that it reads back the same.\n"
		"texts prints the game's texts p3vita reads from the exe (game_texts.hpp), from FIRST on.\n"
		"letters prints the letters of the merchant's chain (by default the player's) as\n"
		"p3vita composes them (letters.hpp) with the exe's texts: entry, type and the\n"
		"text in hex; - without a text, ? for a type not rewritten yet; with offers, five offers\n"
		"made first (offer_recipe: their letters 0x1E and 0x1F aren't in a save)\n"
		"(tools/oracle/letter_oracle.py prints the game's).\n"
		"chronicle prints the chronicle (the news of the list -2) as p3vita composes it, in\n"
		"hex (tools/oracle/chronicle_oracle.py prints the game's).\n"
		"letter-picture writes the picture of the letter 0x46 or 0x6E at that entry of the\n"
		"mailbox (letter_picture.hpp, 0x51AC60) as a PNG.\n");
	return 2;
}

std::string town_name(const std::vector<std::string>& names, int id) {
	return id >= 0 && id < int(names.size()) ? names[size_t(id)] : "ciudad " + std::to_string(id);
}

// Nombre de la ciudad por su índice en la partida.
std::string town_at(const SaveGame& save, const std::vector<std::string>& names, int index) {
	return index >= 0 && size_t(index) < save.towns.size() ? town_name(names, save.towns[size_t(index)].town_id())
															 : "-";
}

void list_towns(const SaveGame& save, const std::vector<std::string>& names) {
	static constexpr const char* kDifficulty[] = {"baja", "normal", "alta"};
	std::printf("versión %#x, año %d, mes %d, %d comerciantes, %zu ciudades, dificultad del comercio %s\n",
		save.version, save.year(), save.month() + 1, save.merchant_count, save.towns.size(),
		kDifficulty[int(save.trade_difficulty)]);
	for (size_t i = 0; i < save.towns.size(); ++i) {
		const SavedTown& t = save.towns[i];
		std::printf("%3zu  %-16s id %2d  plantilla %3d  %6u habitantes  %3zu registros de edificio\n", i,
			town_name(names, t.town_id()).c_str(), t.town_id(), t.template_map(), t.citizens(), t.records.size());
	}
}

void show_town(const SavedTown& t, const std::vector<std::string>& names, TradeDifficulty difficulty) {
	std::printf("%s: %u habitantes\n\n", town_name(names, t.town_id()).c_str(), t.citizens());
	std::printf("%-14s %8s %8s %8s %8s %8s   %s\n", "mercancía", "almacén", "prod.", "consumo", "compra", "venta",
		"umbrales t0..t3");
	for (int w = 0; w < kWareCount; ++w) {
		const size_t i = size_t(w);
		const int32_t unit = ware_scale(kWares[i].unit);
		const double scale = unit;
		const int32_t buy = buy_price(w, t.market(w), unit);
		const int32_t sell = sell_price(w, t.market(w), unit, difficulty);
		const auto& th = t.price_thresholds(i);
		std::printf("%-14s %8.1f %8.2f %8.2f %8s %8d   %.1f %.1f %.1f %.1f\n", kWares[i].name, t.stock(i) / scale,
			t.production(i) / scale, (t.business_consumption(i) + t.citizen_consumption(i)) / scale,
			buy ? std::to_string(buy).c_str() : "-", sell, th[0] / scale, th[1] / scale, th[2] / scale,
			th[3] / scale);
	}
}

void list_merchants(const SaveGame& save, const std::vector<std::string>& towns, const NameLists& names) {
	for (size_t i = 0; i < save.merchants.size(); ++i) {
		const SavedMerchant& m = save.merchants[i];
		int ships = 0;
		for (uint16_t s = m.first_ship(); s < save.ships.size() && ships < 1000; s = save.ships[s].next_of_owner()) {
			++ships;
		}
		int offices = 0;
		for (const SavedOffice& office : save.offices) {
			offices += office.owner() == i;
		}
		std::printf("%3zu %s %-24s %9d monedas  natal %-12s  %d barcos  %d oficinas  tipo %u\n", i,
			int(i) == save.player ? "*" : " ", merchant_name(save, names, int(i)).c_str(), m.money(),
			town_at(save, towns, m.hometown()).c_str(), ships, offices, m.kind());
	}
	std::printf("\n* = jugador\n");
}

void list_ships(const SaveGame& save, const std::vector<std::string>& towns) {
	for (size_t i = 0; i < save.ships.size(); ++i) {
		const SavedShip& s = save.ships[i];
		if (!s.in_use()) {
			continue;
		}
		std::string cargo;
		for (int w = 0; w < kWareCount; ++w) {
			const int32_t amount = s.cargo(size_t(w));
			if (amount != 0) {
				char buffer[64];
				std::snprintf(buffer, sizeof(buffer), " %s %g", kWares[size_t(w)].name,
					double(amount) / ware_scale(kWares[size_t(w)].unit));
				cargo += buffer;
			}
		}
		const std::string place = s.status() == 0x0F ? "→ " + town_at(save, towns, s.destination())
												   : town_at(save, towns, s.town());
		std::printf("%3zu %-18s %-7s dueño %3u  estado %#04x  %-14s  bodega %5.1f/%5.1f  salud %3d%%  |%s\n", i,
			s.name().c_str(), kShipTypes[s.type() & 3], s.owner(), s.status(), place.c_str(), s.used() / 2000.0,
			(s.capacity() - s.equipment()) / 2000.0, s.max_health() ? s.health() * 100 / s.max_health() : 0, cargo.c_str());
	}
	std::printf("\nbodega en cargas (2000 unidades); la capacidad ya descuenta artillería y tripulación\n");
}

bool write_file(const char* path, const std::vector<uint8_t>& data) {
	FILE* out = std::fopen(path, "wb");
	const bool ok = out && std::fwrite(data.data(), 1, data.size(), out) == data.size();
	if (out) {
		std::fclose(out);
	}
	if (!ok) {
		std::fprintf(stderr, "can't write %s\n", path);
	}
	return ok;
}

// A save from the archive or, if it isn't there, from the disk, decompressed.
bool read_any_savegame_data(const CprArchive& archive, const std::string& path, std::vector<uint8_t>& data,
	std::string* error) {
	if (archive.find(path)) {
		return read_savegame_data(archive, path, data, error);
	}
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f) {
		*error = "can't read " + path;
		return false;
	}
	std::vector<uint8_t> packed;
	uint8_t buffer[1 << 16];
	for (size_t n; (n = std::fread(buffer, 1, sizeof(buffer), f)) > 0;) {
		packed.insert(packed.end(), buffer, buffer + n);
	}
	std::fclose(f);
	return lz_decompress_file(packed.data(), packed.size(), data, error);
}

bool load_any_savegame(const CprArchive& archive, const std::string& path, SaveGame& save, std::string* error) {
	if (archive.find(path)) {
		return load_savegame(archive, path, save, error);  // with its .pst, if the archive has it
	}
	std::vector<uint8_t> data;
	if (!read_any_savegame_data(archive, path, data, error) || !parse_savegame(data.data(), data.size(), save, error)) {
		return false;
	}
	load_game_scripts(archive, save);
	// The .pst next to it on the disk, if there is one.
	if (path.size() > 4 && path.compare(path.size() - 4, 4, ".pat") == 0) {
		std::vector<uint8_t> pst;
		if (FILE* f = std::fopen((path.substr(0, path.size() - 4) + ".pst").c_str(), "rb")) {
			for (int c; (c = std::fgetc(f)) != EOF;) {
				pst.push_back(uint8_t(c));
			}
			std::fclose(f);
			load_statistics_file(&pst, save);
		} else {
			load_statistics_file(nullptr, save);
		}
	}
	return true;
}

// letters … offers: the offers between merchants aren't in a save (SaveGame::offers), so their letters (0x1E, 0x1F)
// can't be composed from one. These five are made with the engine's 0x5F before composing, the same ones
// tools/oracle/letter_oracle.py --offers makes in the game (scripts/letters-check.sh with P3_OFFERS=1). From the docked
// list ([0x6DD888], next [+0x6]): the player's first ship there (its town T) and the first ship there of a merchant
// past the towns' own and not the player (that merchant, M): to M from the player money (5001, price 500, 1000), beer
// (5002, 800, 2 barrels) and that ship (5003, 1500); to the player from M money (5004, 300, 700) and grain (5005, 0, 1
// load). The player's chain gets the memos 0x1E of the first three and the offers 0x1F of the other two (an AI
// merchant keeps no letters). Nothing if the list has no such ships.
std::vector<std::array<uint8_t, 0x14>> offer_recipe(const SaveGame& save, std::string& note) {
	std::vector<std::array<uint8_t, 0x14>> ops;
	const uint32_t ships = save.globals.u16(global::kShipCount);
	const uint32_t towns = save.globals.u32(global::kWorldTownCount);
	const uint32_t player = save.globals.u32(global::kPlayer);
	int own = -1;
	int other = -1;
	for (uint32_t i = save.globals.u16(global::kShips + 0xE8), seen = 0; i < ships && i < save.ships.size() && seen < ships;
		 i = save.ships[i].memory.u16(0x6), ++seen) {
		const uint8_t owner = save.ships[i].memory.u8(0);
		if (own < 0 && owner == player) {
			own = int(i);
		} else if (other < 0 && owner != player && owner >= towns) {
			other = int(owner);
		}
	}
	if (own < 0 || other < 0) {
		note = "no ship of the player and of another merchant in the docked list: no offers";
		return ops;
	}
	const uint8_t town = save.ships[size_t(own)].memory.u8(0x39);
	auto offer = [&](uint32_t number, int32_t price, uint32_t a, uint32_t b, uint8_t kind, uint32_t what) {
		std::array<uint8_t, 0x14> op{};
		const uint32_t code = 0x5F;
		std::memcpy(op.data(), &code, 4);
		std::memcpy(op.data() + 4, &number, 4);
		std::memcpy(op.data() + 8, &price, 4);
		op[0xC] = uint8_t(a);
		op[0xD] = uint8_t(b);
		op[0xE] = town;
		op[0xF] = kind;
		std::memcpy(op.data() + 0x10, &what, 4);
		ops.push_back(op);
	};
	offer(5001, 500, player, uint32_t(other), 2, 1000);
	offer(5002, 800, player, uint32_t(other), 0, 3 | 2 << 16);
	offer(5003, 1500, player, uint32_t(other), 1, uint32_t(own));
	offer(5004, 300, uint32_t(other), player, 2, 700);
	offer(5005, 0, uint32_t(other), player, 0, 0 | 1 << 16);
	note = "offers: the player " + std::to_string(player) + ", merchant " + std::to_string(other) + ", ship " +
		std::to_string(own) + ", town " + std::to_string(town);
	return ops;
}

}  // namespace

int main(int argc, char** argv) {
	// --fix-game-bugs anywhere: the fixes to the game's bugs (off by default,
	// as the game: what the writer's checks compare).
	int kept = 1;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--fix-game-bugs") == 0) {
			p3::data::set_fix_game_bugs(true);
		} else {
			argv[kept++] = argv[i];
		}
	}
	argc = kept;
	if (argc == 4 && std::strcmp(argv[1], "compress") == 0) {
		FILE* f = std::fopen(argv[2], "rb");
		if (!f) {
			std::fprintf(stderr, "can't read %s\n", argv[2]);
			return 1;
		}
		std::vector<uint8_t> data;
		uint8_t buffer[1 << 16];
		for (size_t n; (n = std::fread(buffer, 1, sizeof(buffer), f)) > 0;) {
			data.insert(data.end(), buffer, buffer + n);
		}
		std::fclose(f);
		return write_file(argv[3], lz_compress(data.data(), data.size())) ? 0 : 1;
	}
	if (argc >= 4 && std::strcmp(argv[1], "newgame") == 0) {
		// p3save newgame <archivo.cpr> <out.raw> [--seed N] [--year Y] [--home I] [--settings-from SAVE.raw]
		//   [--stop ops|advance|player] [--ships N] [--female] [--first NAME] [--family NAME]
		//   [--window [--level L] [--start 1|2] [--speed S] [--goal G] [--goal-time T] [--write-settings F]]
		//   [--map FILE.gmf]
		// A new game as the game's window starts it (new_game.hpp), on the archive's map
		// Standard24, with 1.pat's settings by default; its uncompressed .pat to <out.raw>, to
		// compare with tools/oracle/newgame_oracle.py's. With --window, the settings and the
		// player are the window's from its choices (new_game_settings, new_game_player) over
		// those settings: the year, home town, sex, names, level (its details), start (the
		// last detail), speed and goal given; --write-settings writes them as a save's head
		// (the version and the 0x4C bytes) for the oracle's --settings-from.
		std::string error;
		std::unique_ptr<CprArchive> archive = CprArchive::open(argv[2], &error);
		std::unique_ptr<NavData> nav = archive ? NavData::load(*archive, &error) : nullptr;
		if (!nav) {
			std::fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
			return 1;
		}
		static const char kSettings[] =
			"14050000050000000f00010c01030000000000000000000000010d0514050901012f50500301014cfc6265636b"
			"00000000000000000000000000008000000000000000000000000000000000";
		std::vector<uint8_t> settings;
		for (size_t i = 0; i + 1 < sizeof(kSettings); i += 2) {
			settings.push_back(uint8_t(std::strtoul(std::string(kSettings + i, 2).c_str(), nullptr, 16)));
		}
		uint32_t seed = 1;
		int year = -1;
		int home = -1;
		NewGameStop stop = NewGameStop::Player;
		NewGamePlayer player;
		player.first_name = "Johann";
		player.family_name = "Kaufmann";
		bool window = false;
		NewGameChoices choices;
		choices.first_name = player.first_name;
		choices.family_name = player.family_name;
		std::string settings_out;
		std::string map_file;  // --map: a .gmf on the disk instead of the archive's Standard24.
		auto flag = [&](int i) {
			return i < argc && (std::strcmp(argv[i], "--female") == 0 || std::strcmp(argv[i], "--window") == 0);
		};
		for (int i = 4; i + 1 < argc || flag(i); ++i) {
			const std::string option = argv[i];
			if (option == "--female") {
				player.male = false;
				choices.female = true;
				continue;
			}
			if (option == "--window") {
				window = true;
				continue;
			}
			const char* value = argv[++i];
			if (option == "--seed") {
				seed = uint32_t(std::strtoul(value, nullptr, 0));
			} else if (option == "--year") {
				year = int(std::strtol(value, nullptr, 0));
			} else if (option == "--home") {
				home = int(std::strtol(value, nullptr, 0));
			} else if (option == "--ships") {
				player.ships = uint8_t(std::strtoul(value, nullptr, 0));
			} else if (option == "--first") {
				player.first_name = value;
				choices.first_name = value;
			} else if (option == "--family") {
				player.family_name = value;
				choices.family_name = value;
			} else if (option == "--level") {
				choices.level = uint32_t(std::strtoul(value, nullptr, 0));
				choices.details = kNewGameLevelDetails[std::min<size_t>(choices.level, 4)];
			} else if (option == "--start") {
				choices.details[7] = uint8_t(std::strtoul(value, nullptr, 0));
			} else if (option == "--speed") {
				choices.speed = uint32_t(std::strtoul(value, nullptr, 0));
			} else if (option == "--goal") {
				choices.goal = uint32_t(std::strtoul(value, nullptr, 0));
			} else if (option == "--goal-time") {
				choices.goal_time = uint32_t(std::strtoul(value, nullptr, 0));
			} else if (option == "--write-settings") {
				settings_out = value;
			} else if (option == "--map") {
				map_file = value;
			} else if (option == "--stop") {
				stop = std::strcmp(value, "ops") == 0 ? NewGameStop::Operations
					: std::strcmp(value, "advance") == 0 ? NewGameStop::Advance : NewGameStop::Player;
			} else if (option == "--settings-from") {
				FILE* f = std::fopen(value, "rb");
				uint8_t block[4 + 0x4C] = {};
				if (!f || std::fread(block, 1, sizeof(block), f) != sizeof(block)) {
					std::fprintf(stderr, "can't read %s\n", value);
					return 1;
				}
				std::fclose(f);
				settings.assign(block + 4, block + sizeof(block));
			} else {
				return usage();
			}
		}
		SaveGame save;
		std::memcpy(save.settings.bytes.data(), settings.data(), 0x4C);
		std::memset(save.settings.bytes.data() + 0x3C, 0, 0x10);
		GameMap map;
		if (map_file.empty() ? !load_game_map(*archive, "maps/Standard24.gmf", map, &error)
							 : !load_game_map_file(map_file, map, &error)) {
			std::fprintf(stderr, "%s\n", error.c_str());
			return 1;
		}
		if (window) {
			choices.year = year >= 0 ? uint32_t(year) : 1300;
			choices.home = home >= 0 ? uint32_t(home) : 9;
			save.settings = new_game_settings(choices, map, save.settings);
			player = new_game_player(choices);
			if (!settings_out.empty()) {
				std::vector<uint8_t> head = {0x82, 0, 0, 0};
				head.insert(head.end(), save.settings.bytes.begin(), save.settings.bytes.end());
				if (!write_file(settings_out.c_str(), head)) {
					return 1;
				}
			}
		} else {
			if (year >= 0) {
				save.settings.u32(0x0) = uint32_t(year);
			}
			if (home >= 0) {
				save.settings.u8(0x1E) = uint8_t(home);
			}
		}
		// What the game's start leaves (the world's constructor 0x52F520: [+0x88]
		// 0xFF, the town of the AI's next feast) and its data (0x5121C0...).
		save.globals.u8(global::kWorld + 0x88) = 0xFF;
		load_game_scripts(*archive, save);
		load_statistics_file(nullptr, save);
		Sailing sailing(save, *nav, load_sea_map_towns(*archive));
		sailing.set_pirates(true);
		sailing.set_battles(true);
		start_new_game(save, sailing, map, player, seed, stop);
		prepare_save(save);
		const std::vector<uint8_t> raw = write_savegame(save);
		if (!write_file(argv[3], raw)) {
			return 1;
		}
		std::printf("%zu towns, %zu merchants, %zu ships; %zu bytes in %s\n", save.towns.size(), save.merchants.size(),
			save.ships.size(), raw.size(), argv[3]);
		return 0;
	}
	if ((argc == 3 || argc == 4) && std::strcmp(argv[1], "texts") == 0) {
		// The texts p3vita reads from the exe (GameText), from FIRST on: index and text.
		std::string error;
		std::unique_ptr<GameTexts> texts = GameTexts::load(argv[2], &error);
		if (!texts) {
			std::fprintf(stderr, "%s\n", error.c_str());
			return 1;
		}
		const int first = argc == 4 ? std::atoi(argv[3]) : 0;
		for (int i = first; i < int(GameText::Count); ++i) {
			std::printf("%d\t%s\n", i, texts->get(GameText(i)).c_str());
		}
		return 0;
	}
	if (argc == 4 && std::strcmp(argv[1], "texts-cache") == 0) {
		// The viewer's texts' cache: read from the exe, written, read back and compared.
		std::string error;
		std::unique_ptr<GameTexts> texts = GameTexts::load(argv[2], &error);
		if (!texts) {
			std::fprintf(stderr, "%s\n", error.c_str());
			return 1;
		}
		if (!texts->save_cache(argv[3])) {
			std::fprintf(stderr, "can't write %s\n", argv[3]);
			return 1;
		}
		std::unique_ptr<GameTexts> cached = GameTexts::load_cache(argv[3], &error);
		if (!cached) {
			std::fprintf(stderr, "can't read the cache back: %s\n", error.c_str());
			return 1;
		}
		size_t addresses = 0;
		const bool same = texts->same_as(*cached, &addresses);
		std::printf("%s build: %s cache (%zu texts by address)\n", texts->build().c_str(),
			same ? "same texts from the" : "DIFFERENT texts from the", addresses);
		return same ? 0 : 1;
	}
	if (argc < 3 || argc > 6) {
		return usage();
	}
	std::string error;
	auto archive = CprArchive::open(argv[1], &error);
	if (!archive) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	const std::string command = argc > 3 ? argv[3] : "";
	if (command == "raw") {
		std::vector<uint8_t> data;
		if (argc != 5 || !read_any_savegame_data(*archive, argv[2], data, &error)) {
			std::fprintf(stderr, "%s\n", argc != 5 ? "falta el fichero de salida" : error.c_str());
			return 1;
		}
		FILE* out = std::fopen(argv[4], "wb");
		if (!out || std::fwrite(data.data(), 1, data.size(), out) != data.size()) {
			std::fprintf(stderr, "no se puede escribir %s\n", argv[4]);
			return 1;
		}
		std::fclose(out);
		return 0;
	}

	SaveGame save;
	if (!load_any_savegame(*archive, argv[2], save, &error)) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	if (command == "ticker") {
		// The pairs of tools/oracle/ticker_oracle.py's output (kind, argument),
		// with p3vita's lines in the same format.
		if (argc < 6) {
			return usage();
		}
		std::unique_ptr<GameTexts> texts = GameTexts::load(argv[4], &error);
		std::FILE* in = std::fopen(argv[5], "r");
		if (!texts || !in) {
			std::fprintf(stderr, "%s\n", texts ? argv[5] : error.c_str());
			return 1;
		}
		unsigned kind = 0;
		unsigned e = 0;
		char line_text[8192];
		while (std::fgets(line_text, sizeof(line_text), in) && std::sscanf(line_text, "%x %u", &kind, &e) == 2) {
			const std::optional<std::string> line = compose_ticker(save, *texts, uint8_t(kind), uint16_t(e));
			std::printf("0x%02x\t%u\t", kind, e);
			if (!line) {
				std::printf("-");
			}
			for (unsigned char ch : line.value_or(std::string())) {
				std::printf("%02x", unsigned(ch));
			}
			std::printf("\n");
		}
		std::fclose(in);
		return 0;
	}
	if (command == "chronicle") {
		if (argc < 5) {
			return usage();
		}
		std::unique_ptr<GameTexts> texts = GameTexts::load(argv[4], &error);
		if (!texts) {
			std::fprintf(stderr, "%s\n", error.c_str());
			return 1;
		}
		for (unsigned char ch : compose_chronicle(save, *texts)) {
			std::printf("%02x", unsigned(ch));
		}
		// What showing it writes (chronicle_viewed): the merchants' [+0x1C] changed.
		SaveGame viewed = save;
		chronicle_viewed(viewed, *texts);
		std::printf("\nchanged");
		for (size_t m = 0; m < save.merchants.size(); ++m) {
			const uint8_t v = viewed.merchants[m].memory.u8(0x1C);
			if (v != save.merchants[m].memory.u8(0x1C)) {
				std::printf(" %zu:0x%02x", m, unsigned(v));
			}
		}
		std::printf("\n");
		return 0;
	}
	if (command == "letter-picture") {
		if (argc < 6) {
			return usage();
		}
		const size_t entry = size_t(std::strtoul(argv[4], nullptr, 0));
		Image picture;
		if (entry >= save.mail.size() || !letter_map_picture(*archive, save.mail[entry], picture)) {
			std::fprintf(stderr, "no picture for the entry %zu\n", entry);
			return 1;
		}
		FILE* out = std::fopen(argv[5], "wb");
		const bool written = out && stbi_write_png_to_func(
										[](void* file, void* data, int size) {
											std::fwrite(data, 1, size_t(size), static_cast<FILE*>(file));
										},
										out, picture.width, picture.height, 4, picture.rgba.data(), picture.width * 4);
		if (out) {
			std::fclose(out);
		}
		if (!written) {
			std::fprintf(stderr, "can't write %s\n", argv[5]);
			return 1;
		}
		const GameMemory<0x10>& e = save.mail[entry];
		std::printf("entry %zu, type %#04x: pieces %#06x, place (%u, %u); %d x %d in %s\n", entry, e.u8(4), e.u16(0xA),
			e.u16(0xC), e.u16(0xE), picture.width, picture.height, argv[5]);
		return 0;
	}
	if (command == "letters") {
		if (argc < 5) {
			return usage();
		}
		std::unique_ptr<GameTexts> texts = GameTexts::load(argv[4], &error);
		if (!texts) {
			std::fprintf(stderr, "%s\n", error.c_str());
			return 1;
		}
		int merchant = save.player;
		for (int k = 5; k < argc; ++k) {
			if (std::strcmp(argv[k], "offers") == 0) {
				std::string note;
				for (const std::array<uint8_t, 0x14>& op : offer_recipe(save, note)) {
					run_operation(save, nullptr, op.data());
				}
				std::fprintf(stderr, "%s\n", note.c_str());
			} else {
				merchant = std::atoi(argv[k]);
			}
		}
		if (merchant < 0 || size_t(merchant) >= save.merchants.size()) {
			std::fprintf(stderr, "no merchant %d\n", merchant);
			return 1;
		}
		size_t seen = 0;
		for (uint16_t i = save.merchants[size_t(merchant)].memory.u16(0xA); i < save.mail.size() && seen < save.mail.size();
			 i = save.mail[i].u16(6), ++seen) {
			const uint8_t type = save.mail[i].u8(4);
			std::printf("%u\t0x%02x\t", unsigned(i), unsigned(type));
			if (!letter_has_text(type)) {
				std::printf("-\n");
			} else if (!letter_type_rewritten(type)) {
				std::printf("?\n");
			} else {
				const std::optional<std::string> text = compose_letter(save, *texts, i);
				for (unsigned char ch : text ? *text : std::string()) {
					std::printf("%02x", unsigned(ch));
				}
				std::printf("\n");
			}
		}
		return 0;
	}
	if (command == "write") {
		if (argc < 5) {
			return usage();
		}
		prepare_save(save);
		const std::vector<uint8_t> raw = write_savegame(save);
		const std::vector<uint8_t> packed = lz_compress(raw.data(), raw.size());
		if (!write_file(argv[4], packed) || (argc == 6 && !write_file(argv[5], raw))) {
			return 1;
		}
		std::printf("%zu bytes (%zu uncompressed) in %s\n", packed.size(), raw.size(), argv[4]);
		// The .pst next to it, as the game saves both: with empty statistics.
		std::string pst = argv[4];
		if (pst.size() > 4 && pst.compare(pst.size() - 4, 4, ".pat") == 0) {
			pst.replace(pst.size() - 4, 4, ".pst");
			if (!write_file(pst.c_str(), statistics_file(save))) {
				return 1;
			}
			std::printf("statistics in %s\n", pst.c_str());
		}
		return 0;
	}
	const std::vector<std::string> names = load_town_names(*archive);
	if (command.empty()) {
		list_towns(save, names);
	} else if (command == "comerciantes") {
		list_merchants(save, names, load_name_lists(*archive));
	} else if (command == "barcos") {
		list_ships(save, names);
	} else {
		const size_t index = size_t(std::atoi(argv[3]));
		if (index >= save.towns.size()) {
			std::fprintf(stderr, "la partida tiene %zu ciudades\n", save.towns.size());
			return 1;
		}
		show_town(save.towns[index], names, save.trade_difficulty);
	}
	return 0;
}
