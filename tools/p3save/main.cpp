// Muestra el contenido de una partida guardada: ciudades y su almacén.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/font.hpp"
#include "p3data/prices.hpp"
#include "p3data/savegame.hpp"
#include "p3data/wares.hpp"

using namespace p3::data;

namespace {

int usage() {
	std::fprintf(stderr,
		"uso: p3save <archivo.cpr> <partida> [ciudad]\n"
		"\n"
		"Sin ciudad, lista las ciudades de la partida (p. ej. missav/1.pat).\n"
		"Con el número de una ciudad, muestra su almacén, producción y consumo.\n");
	return 2;
}

std::string town_name(const std::vector<std::string>& names, int id) {
	return id >= 0 && id < int(names.size()) ? names[size_t(id)] : "ciudad " + std::to_string(id);
}

void list_towns(const SaveGame& save, const std::vector<std::string>& names) {
	std::printf("versión %#x, año %d, %d comerciantes, %zu ciudades\n", save.version, save.start_year,
		save.merchant_count, save.towns.size());
	for (size_t i = 0; i < save.towns.size(); ++i) {
		const SavedTown& t = save.towns[i];
		std::printf("%3zu  %-16s id %2d  plantilla %3d  %6u habitantes  %3zu registros de edificio\n", i,
			town_name(names, t.town_id).c_str(), t.town_id, t.template_map, t.citizens, t.records.size());
	}
}

void show_town(const SavedTown& t, const std::vector<std::string>& names) {
	std::printf("%s: %u habitantes\n\n", town_name(names, t.town_id).c_str(), t.citizens);
	std::printf("%-14s %8s %8s %8s %8s %8s   %s\n", "mercancía", "almacén", "prod.", "consumo", "compra", "venta",
		"umbrales t0..t3");
	for (int w = 0; w < kWareCount; ++w) {
		const size_t i = size_t(w);
		const int32_t unit = ware_scale(kWares[i].unit);
		const double scale = unit;
		const int32_t buy = buy_price(w, t.market(w), unit);
		const int32_t sell = sell_price(w, t.market(w), unit);
		const auto& th = t.price_thresholds[i];
		std::printf("%-14s %8.1f %8.2f %8.2f %8s %8d   %.1f %.1f %.1f %.1f\n", kWares[i].name, t.wares[i] / scale,
			t.production[i] / scale, (t.business_consumption[i] + t.citizen_consumption[i]) / scale,
			buy ? std::to_string(buy).c_str() : "-", sell, th[0] / scale, th[1] / scale, th[2] / scale,
			th[3] / scale);
	}
}

}  // namespace

int main(int argc, char** argv) {
	if (argc != 3 && argc != 4) {
		return usage();
	}
	std::string error;
	auto archive = CprArchive::open(argv[1], &error);
	SaveGame save;
	if (!archive || !load_savegame(*archive, argv[2], save, &error)) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	const std::vector<std::string> names = load_town_names(*archive);
	if (argc == 3) {
		list_towns(save, names);
		return 0;
	}
	const size_t index = size_t(std::atoi(argv[3]));
	if (index >= save.towns.size()) {
		std::fprintf(stderr, "la partida tiene %zu ciudades\n", save.towns.size());
		return 1;
	}
	show_town(save.towns[index], names);
	return 0;
}
