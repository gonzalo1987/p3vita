// Compara la lectura de comerciantes, oficinas y barcos de src/p3data, el
// comercio de src/p3data/trade.cpp y, si se le da el archivo CPR, las rutas
// entre ciudades de src/p3data/navigation.cpp con lo que obtiene el propio
// juego (save_oracle.py): todos los campos deben coincidir, y el precio medio
// de la carga, bit a bit.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/navigation.hpp"
#include "p3data/savegame.hpp"
#include "p3data/seamap.hpp"
#include "p3data/trade.hpp"

using namespace p3::data;

namespace {

std::vector<std::string> split(const std::string& line, size_t max_fields) {
	std::vector<std::string> fields;
	size_t pos = 0;
	while (fields.size() + 1 < max_fields) {
		const size_t comma = line.find(',', pos);
		if (comma == std::string::npos) {
			break;
		}
		fields.push_back(line.substr(pos, comma - pos));
		pos = comma + 1;
	}
	fields.push_back(line.substr(pos));
	return fields;
}

std::string bits(float v) {
	uint32_t b;
	std::memcpy(&b, &v, sizeof(b));
	char text[16];
	std::snprintf(text, sizeof(text), "%08x", b);
	return text;
}

class Checker {
public:
	explicit Checker(const std::string& line) : line_(line) {}

	template <typename T>
	void field(const char* name, const T& ours, const std::string& theirs) {
		std::string mine;
		if constexpr (std::is_same_v<T, std::string>) {
			mine = ours;
		} else {
			mine = std::to_string(ours);
		}
		if (mine != theirs) {
			if (differences_ == 0) {
				std::printf("%s\n", line_.c_str());
			}
			std::printf("    %s: juego %s, p3vita %s\n", name, theirs.c_str(), mine.c_str());
			++differences_;
		}
	}

	bool ok() const { return differences_ == 0; }

private:
	const std::string& line_;
	int differences_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
	if (argc != 3 && argc != 4) {
		std::fprintf(stderr, "uso: compare_save <partida.raw> <oráculo.csv> [archivo.cpr para las rutas]\n");
		return 2;
	}
	std::unique_ptr<NavData> nav;
	std::vector<SeaMapTown> map_towns;
	if (argc == 4) {
		std::string error;
		auto archive = CprArchive::open(argv[3], &error);
		nav = archive ? NavData::load(*archive, &error) : nullptr;
		if (!nav) {
			std::fprintf(stderr, "%s\n", error.c_str());
			return 1;
		}
		map_towns = load_sea_map_towns(*archive);
	}
	std::ifstream raw(argv[1], std::ios::binary);
	const std::vector<uint8_t> data((std::istreambuf_iterator<char>(raw)), std::istreambuf_iterator<char>());
	SaveGame save;
	std::string error;
	if (data.empty() || !parse_savegame(data.data(), data.size(), save, &error)) {
		std::fprintf(stderr, "%s: %s\n", argv[1], error.c_str());
		return 1;
	}
	std::ifstream in(argv[2]);
	if (!in) {
		std::fprintf(stderr, "no se puede leer %s\n", argv[2]);
		return 1;
	}

	int records = 0;
	int routes = 0;
	int trades = 0;
	int wrong = 0;
	std::string line;
	while (std::getline(in, line)) {
		const std::string kind = line.substr(0, line.find(','));
		Checker check(line);
		if (kind == "dificultad") {
			// Factor de venta que deja el juego: 2.2, 2.0 o 1.8 (0x6739FC).
			static const char* const kFactors[] = {"400ccccd", "40000000", "3fe66666"};
			check.field("factor de dificultad", std::string(kFactors[int(save.trade_difficulty)]), line.substr(11));
			++records;
		} else if (kind == "ciudad") {
			const auto f = split(line, 122);
			const SavedTown& t = save.towns.at(std::stoul(f[1]));
			for (int w = 0; w < kWareCount; ++w) {
				check.field(kWares[size_t(w)].name, t.stock(size_t(w)), f[2 + size_t(w)]);
				for (int k = 0; k < 4; ++k) {
					const std::string name = std::string("umbral ") + kWares[size_t(w)].name + " t" + std::to_string(k);
					check.field(name.c_str(), t.price_thresholds(size_t(w))[size_t(k)], f[26 + size_t(w) * 4 + size_t(k)]);
				}
			}
			++records;
		} else if (kind == "comerciante") {
			const auto f = split(line, 11);
			const SavedMerchant& m = save.merchants.at(std::stoul(f[1]));
			check.field("dinero", m.money(), f[2]);
			check.field("tipo", m.kind(), f[3]);
			check.field("primera oficina", m.first_office(), f[4]);
			check.field("primer barco", m.first_ship(), f[5]);
			check.field("natal", m.hometown(), f[6]);
			check.field("hombre", int(m.male()), f[7]);
			check.field("nombre", m.first_name() & 0x7F, f[8]);
			check.field("apellido", m.family_name(), f[9]);
			check.field("vetos", m.banned_towns(), f[10]);
			++records;
		} else if (kind == "oficina") {
			const auto f = split(line, 29);
			const SavedOffice& o = save.offices.at(std::stoul(f[1]));
			check.field("dueño", o.owner(), f[2]);
			check.field("ciudad", o.town(), f[3]);
			check.field("siguiente", o.next_in_town(), f[4]);
			for (int w = 0; w < kWareCount; ++w) {
				check.field(kWares[size_t(w)].name, o.stock(size_t(w)), f[5 + size_t(w)]);
			}
			++records;
		} else if (kind == "barco") {
			const auto f = split(line, 67);
			const SavedShip& s = save.ships.at(std::stoul(f[1]));
			check.field("estado", s.status(), f[2]);
			if (f.size() > 3) {
				check.field("dueño", s.owner(), f[3]);
				check.field("tipo", s.type(), f[4]);
				check.field("nivel", s.level(), f[5]);
				check.field("siguiente", s.next_of_owner(), f[6]);
				check.field("capacidad", s.capacity(), f[7]);
				check.field("salud", s.health(), f[8]);
				check.field("salud máxima", s.max_health(), f[9]);
				check.field("x", s.x(), f[10]);
				check.field("y", s.y(), f[11]);
				check.field("origen", s.origin(), f[12]);
				check.field("destino", s.destination(), f[13]);
				check.field("ciudad", s.town(), f[14]);
				check.field("marineros", s.sailors(), f[15]);
				check.field("ocupada", s.used(), f[16]);
				check.field("equipo", s.equipment(), f[17]);
				for (int w = 0; w < kWareCount; ++w) {
					check.field(kWares[size_t(w)].name, s.cargo(size_t(w)), f[18 + size_t(w)]);
					check.field("precio medio", bits(s.average_price(size_t(w))), f[42 + size_t(w)]);
				}
				check.field("nombre", s.name(), f[66]);
			}
			++records;
		} else if (kind == "ruta") {
			if (!nav) {
				continue;
			}
			std::vector<std::string> f;
			for (size_t pos = 0;;) {
				const size_t comma = line.find(',', pos);
				f.push_back(line.substr(pos, comma - pos));
				if (comma == std::string::npos) {
					break;
				}
				pos = comma + 1;
			}
			const SeaMapTown& from = map_towns.at(size_t(save.towns.at(std::stoul(f[1])).town_id()));
			const SeaMapTown& to = map_towns.at(size_t(save.towns.at(std::stoul(f[2])).town_id()));
			const std::vector<MapPoint> ours = nav->route({int16_t(from.approach_x), int16_t(from.approach_y)},
				{int16_t(to.approach_x), int16_t(to.approach_y)});
			std::string mine;
			for (const MapPoint& p : ours) {
				mine += (mine.empty() ? "" : ",") + std::to_string(p.x) + "," + std::to_string(p.y);
			}
			check.field("puntos", mine, line.substr(line.find(',', line.find(',', 5) + 1) + 1));
			++routes;
		} else if (kind == "dinero") {
			const auto f = split(line, 3);
			save.merchants.at(std::stoul(f[1])).money() = std::stoi(f[2]);
			continue;
		} else if (kind == "compra" || kind == "venta") {
			const auto f = split(line, 14);
			const int ship = std::stoi(f[1]);
			const int town = std::stoi(f[2]);
			const int ware = std::stoi(f[3]);
			const int32_t amount = std::stoi(f[5]);
			const TradeResult result = kind == "compra"
				? buy_to_ship(save, ship, town, ware, amount)
				: sell_from_ship(save, ship, town, ware, amount, TradeDifficulty(std::stoi(f[4])));
			const SavedShip& s = save.ships.at(size_t(ship));
			check.field("cantidad", result.amount, f[6]);
			check.field("precio", result.price, f[7]);
			check.field("dinero", save.merchants.at(s.owner()).money(), f[8]);
			check.field("existencias", int32_t(save.towns.at(size_t(town)).stock(size_t(ware))), f[9]);
			check.field("carga", s.cargo(size_t(ware)), f[10]);
			check.field("precio medio", bits(s.average_price(size_t(ware))), f[11]);
			check.field("ocupada", s.used(), f[12]);
			check.field("equipo", s.equipment(), f[13]);
			++trades;
		} else {
			continue;
		}
		wrong += !check.ok();
	}
	std::printf("registros: %d, rutas: %d, operaciones: %d, con diferencias: %d\n", records, routes, trades, wrong);
	return wrong == 0 ? 0 : 1;
}
