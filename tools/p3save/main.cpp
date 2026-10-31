// Muestra el contenido de una partida guardada: ciudades y su almacén,
// comerciantes y barcos.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/font.hpp"
#include "p3data/game_fixes.hpp"
#include "p3data/prices.hpp"
#include "p3data/lz.hpp"
#include "p3data/savegame.hpp"
#include "p3data/savegame_write.hpp"
#include "p3data/lz_compress.hpp"
#include "p3data/wares.hpp"

using namespace p3::data;

namespace {

constexpr const char* kShipTypes[] = {"Snaikka", "Craier", "Kogge", "Holk"};

int usage() {
	std::fprintf(stderr,
		"uso: p3save <archivo.cpr> <partida> [ciudad | comerciantes | barcos | raw <salida>]\n"
		"       p3save <archivo.cpr> <partida> write <out.pat> [<out.raw>]\n"
		"       (--fix-game-bugs anywhere: with the fixes to the game's bugs, game_fixes.hpp)\n"
		"       p3save compress <in> <out>\n"
		"\n"
		"Sin más argumentos, lista las ciudades de la partida (p. ej. missav/1.pat).\n"
		"Con el número de una ciudad, muestra su almacén, producción y consumo.\n"
		"comerciantes y barcos los listan; raw guarda la partida descomprimida.\n"
		"write saves it again as the game does (version 0x85; with <out.raw>, also uncompressed),\n"
		"and the .pst of its statistics next to <out.pat>.\n"
		"<partida> is read from the archive or, if it isn't there, from the disk.\n"
		"compress compresses any file as the game does (lz_compress).\n");
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
