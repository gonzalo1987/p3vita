// Compara la navegación de src/p3data/sailing.cpp con la del propio juego
// (sail_oracle.py): repite sobre la misma partida los ticks y las órdenes de
// zarpar que anotó el oráculo y comprueba el estado de cada barco, las rutas,
// las listas de barcos y la mercancía en camino de cada ciudad.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/navigation.hpp"
#include "p3data/sailing.hpp"
#include "p3data/savegame.hpp"
#include "p3data/seamap.hpp"

using namespace p3::data;

namespace {

std::vector<std::string> split(const std::string& line) {
	std::vector<std::string> fields;
	for (size_t pos = 0;;) {
		const size_t comma = line.find(',', pos);
		fields.push_back(line.substr(pos, comma - pos));
		if (comma == std::string::npos) {
			return fields;
		}
		pos = comma + 1;
	}
}

std::string hex(float v) {
	uint32_t b;
	std::memcpy(&b, &v, sizeof(b));
	char text[16];
	std::snprintf(text, sizeof(text), "%08x", b);
	return text;
}

constexpr int kMaxReported = 40;

class Checker {
public:
	Checker(const std::string& line, int& reported) : line_(line), reported_(reported) {}

	void field(const char* name, const std::string& ours, const std::string& theirs) {
		if (ours == theirs) {
			return;
		}
		if (reported_ < kMaxReported) {
			if (differences_ == 0) {
				std::printf("%s\n", line_.substr(0, 160).c_str());
			}
			std::printf("    %s: juego %s, p3vita %s\n", name, theirs.c_str(), ours.c_str());
		}
		++differences_;
	}

	template <typename T>
	void field(const char* name, T ours, const std::string& theirs) {
		field(name, std::to_string(ours), theirs);
	}

	bool ok() const { return differences_ == 0; }

	~Checker() {
		if (differences_ > 0) {
			++reported_;
		}
	}

private:
	const std::string& line_;
	int& reported_;
	int differences_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
	if (argc != 4) {
		std::fprintf(stderr, "uso: compare_sail <archivo.cpr> <partida.raw> <oráculo.csv>\n");
		return 2;
	}
	std::string error;
	auto archive = CprArchive::open(argv[1], &error);
	std::unique_ptr<NavData> nav = archive ? NavData::load(*archive, &error) : nullptr;
	if (!nav) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	std::ifstream raw(argv[2], std::ios::binary);
	const std::vector<uint8_t> data((std::istreambuf_iterator<char>(raw)), std::istreambuf_iterator<char>());
	SaveGame save;
	if (data.empty() || !parse_savegame(data.data(), data.size(), save, &error)) {
		std::fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
		return 1;
	}
	std::ifstream in(argv[3]);
	if (!in) {
		std::fprintf(stderr, "no se puede leer %s\n", argv[3]);
		return 1;
	}
	Sailing sailing(save, *nav, load_sea_map_towns(*archive));
	sailing.assign_loaded_routes();
	sailing.set_ai(false);  // El oráculo de navegación no ejecuta la IA en puerto
	sailing.set_convoys(false);  // ni los convoyes.
	auto advance = [&](uint32_t tick) {
		while (save.tick() < tick) {
			sailing.advance_tick();
		}
	};

	std::map<std::string, int> checked;
	int wrong = 0;
	int reported = 0;
	int orders = 0;
	int refused = 0;  // Órdenes que p3vita no cumple; el juego tampoco, si no hay diferencias.
	int stubs = 0;
	std::string line;
	while (std::getline(in, line)) {
		const std::vector<std::string> f = split(line);
		const std::string& kind = f[0];
		if (kind == "stub") {
			++stubs;
			continue;
		}
		if (f.size() < 2) {
			continue;
		}
		const uint32_t tick = uint32_t(std::stoul(f[1]));
		Checker check(line, reported);
		if (kind == "inicio") {
			check.field("tick", save.tick(), f[1]);
		} else if (kind == "listas") {
			advance(tick);
			check.field("en puerto", save.docked_head(), f[2]);
			check.field("en el mar", save.sailing_head(), f[3]);
		} else if (kind == "orden") {
			advance(tick);
			refused += !sailing.set_sail(std::stoi(f[2]), std::stoi(f[3]), std::stoi(f[4]), std::stoi(f[5]));
			++orders;
			continue;
		} else if (kind == "nav") {
			advance(tick);
			const SavedShip& s = save.ships.at(std::stoul(f[2]));
			const char* const names[] = {"estado", "x", "y", "dx", "dy", "velocidad", "punto", "marca", "rumbo",
				"casco", "origen", "destino", "ciudad", "ruta propia", "maniobra", "zarpó/atracó", "llegada prevista",
				"ataque", "descanso", "siguiente", "persigue", "carga media", "viajes", "puntos de la ruta"};
			const std::string ours[] = {std::to_string(s.status()), std::to_string(s.x()), std::to_string(s.y()),
				std::to_string(s.dx()), std::to_string(s.dy()), std::to_string(s.speed()), std::to_string(s.route_index()),
				std::to_string(s.route_mark()), std::to_string(s.heading()), std::to_string(s.health()),
				std::to_string(s.origin()), std::to_string(s.destination()), std::to_string(s.town()),
				std::to_string(int(s.own_route())), std::to_string(s.timer()), std::to_string(s.moved_tick()),
				std::to_string(s.arrival_tick()), std::to_string(int(s.raid())), std::to_string(s.readiness()),
				std::to_string(s.next_in_list()), std::to_string(s.target()), hex(s.load_factor_sum()),
				std::to_string(s.voyages()), s.route ? std::to_string(s.route->size()) : "-1"};
			for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
				check.field(names[i], ours[i], f.at(3 + i));
			}
		} else if (kind == "ruta") {
			advance(tick);
			const SavedShip& s = save.ships.at(std::stoul(f[2]));
			std::string mine;
			if (s.route) {
				for (const MapPoint& p : *s.route) {
					mine += (mine.empty() ? "" : ",") + std::to_string(p.x) + "," + std::to_string(p.y);
				}
			}
			check.field("puntos", mine, line.substr(line.find(',', line.find(',', 5) + 1) + 1));
		} else if (kind == "encamino") {
			advance(tick);
			const SavedTown& t = save.towns.at(std::stoul(f[2]));
			for (int w = 0; w < kWareCount; ++w) {
				check.field("[0x370]", t.incoming(size_t(w)), f.at(3 + size_t(w)));
				check.field("[0x670]", t.in_transit(size_t(w)), f.at(27 + size_t(w)));
			}
		} else {
			continue;
		}
		++checked[kind];
		wrong += !check.ok();
	}
	std::printf("tick final %u; órdenes %d (%d sin zarpar); llamadas de la IA saltadas %d\n", save.tick(), orders,
		refused, stubs);
	for (const auto& [kind, count] : checked) {
		std::printf("  %s: %d\n", kind.c_str(), count);
	}
	std::printf("con diferencias: %d\n", wrong);
	return wrong == 0 ? 0 : 1;
}
