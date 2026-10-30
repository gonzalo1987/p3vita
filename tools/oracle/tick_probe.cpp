// Repite con src/p3data/town_tick las llamadas que grabó town_oracle.py y
// compara el resultado con el del juego, byte a byte, en la memoria de las
// ciudades, oficinas, comerciantes, barcos, capitanes, globales...
//
// uso: tick_probe <fichero.probe> [--continuo] [--solo FUNCIÓN] [--max N]
//        tick_probe <fichero.probe> --carga <partida.raw> [--continuo [--cpr p2arch0_eng.cpr [--sin-ia]]] [--max N]
//
// Por omisión cada llamada empieza desde el estado del juego justo antes de
// ella, así que una diferencia apunta a esa función. Con --continuo el estado
// de p3vita evoluciona solo desde el principio (solo tiene sentido si se
// grabaron los ticks de ciudad enteros, 51ba10): antes de cada llamada se
// copian del juego la fecha y el tick, que cambian fuera del tick de ciudad.
//
// Con --carga compara el estado inicial de la grabación (el juego recién
// cargada la partida) con lo que lee src/p3data/savegame.cpp de la misma
// partida; de los globales, solo los que llena el lector. Con --carga y
// --continuo, la simulación de p3vita parte de esa lectura; con --cpr además
// crea la navegación (Sailing) y repite también la actualización de barcos
// (506720) y las órdenes de zarpar (537670) de una grabación hecha con
// town_oracle.py --barcos: el mundo entero, tick a tick.

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <deque>
#include <memory>

#include "p3data/navigation.hpp"
#include "p3data/sailing.hpp"
#include "p3data/savegame.hpp"
#include "p3data/seamap.hpp"
#include "probe_registry.hpp"

using namespace p3::data;
using namespace p3::probe;

namespace {

struct Region {
	uint32_t address = 0;
	std::vector<uint8_t> bytes;
};
using State = std::map<std::string, Region>;

class Input {
public:
	explicit Input(const std::string& path) : f_(path, std::ios::binary) {}
	bool ok() const { return bool(f_); }

	uint8_t u8() {
		char c = 0;
		f_.read(&c, 1);
		return uint8_t(c);
	}
	uint32_t u32() {
		uint8_t b[4] = {};
		f_.read(reinterpret_cast<char*>(b), 4);
		return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
	}
	std::string name() {
		std::string s(u8(), '\0');
		f_.read(s.data(), std::streamsize(s.size()));
		return s;
	}
	std::vector<uint8_t> bytes(size_t n) {
		std::vector<uint8_t> v(n);
		f_.read(reinterpret_cast<char*>(v.data()), std::streamsize(n));
		return v;
	}
	bool expect(const char* text) {
		std::string s(std::strlen(text), '\0');
		f_.read(s.data(), std::streamsize(s.size()));
		return s == text;
	}

private:
	std::ifstream f_;
};

void read_regions(Input& in, State& state) {
	const uint32_t n = in.u32();
	for (uint32_t i = 0; i < n; ++i) {
		const std::string name = in.name();
		Region& r = state[name];
		r.address = in.u32();
		r.bytes = in.bytes(in.u32());
	}
}

void apply_diff(Input& in, State& state) {
	const uint32_t n = in.u32();
	for (uint32_t i = 0; i < n; ++i) {
		const std::string name = in.name();
		const uint32_t address = in.u32();
		const uint32_t offset = in.u32();
		const std::vector<uint8_t> data = in.bytes(in.u32());
		Region& r = state[name];
		r.address = address;
		if (offset == 0xFFFFFFFF) {
			r.bytes = data;
		} else {
			std::memcpy(r.bytes.data() + offset, data.data(), data.size());
		}
	}
}

template <class T>
T get(const uint8_t* p, size_t o) {
	T v;
	std::memcpy(&v, p + o, sizeof(v));
	return v;
}
template <class T>
void put(uint8_t* p, size_t o, T v) {
	std::memcpy(p + o, &v, sizeof(v));
}

template <size_t N>
void copy_blocks(const Region& r, std::vector<GameMemory<N>>& out) {
	out.resize(r.bytes.size() / N);
	for (size_t i = 0; i < out.size(); ++i) {
		std::memcpy(out[i].bytes.data(), r.bytes.data() + i * N, N);
	}
}

template <size_t N>
void store_blocks(const std::vector<GameMemory<N>>& in, Region& r) {
	r.bytes.resize(in.size() * N);
	for (size_t i = 0; i < in.size(); ++i) {
		std::memcpy(r.bytes.data() + i * N, in[i].bytes.data(), N);
	}
}

const Region* find(const State& s, const std::string& name) {
	auto it = s.find(name);
	return it == s.end() ? nullptr : &it->second;
}

// El estado de p3vita a partir del del juego.
void build_save(const State& state, SaveGame& save) {
	save = SaveGame();
	const Region& g = state.at("G");
	std::memcpy(save.globals.memory.bytes.data(), g.bytes.data(), g.bytes.size());
	const Region& a = state.at("A");
	std::memcpy(save.settings.bytes.data(), a.bytes.data(), a.bytes.size());
	const float factor = save.globals.f32(0x6DE3D8 + 0x64);
	save.trade_difficulty = factor > 2.1f ? TradeDifficulty::Low : factor > 1.9f ? TradeDifficulty::Normal
		: TradeDifficulty::High;

	const Region& t = state.at("T");
	save.towns.resize(t.bytes.size() / TownMemory::kSize);
	for (size_t i = 0; i < save.towns.size(); ++i) {
		SavedTown& town = save.towns[i];
		std::memcpy(town.memory.bytes.data(), t.bytes.data() + i * TownMemory::kSize, TownMemory::kSize);
		const Region& rec = state.at("R" + std::to_string(i));
		town.records.resize(rec.bytes.size() / 8);
		std::memcpy(town.records.data(), rec.bytes.data(), rec.bytes.size());
		town.grid = state.at("D" + std::to_string(i)).bytes;
		const Region& nodes = state.at("N" + std::to_string(i));
		town.nodes.resize(nodes.bytes.size() / 0x14);
		for (size_t k = 0; k < town.nodes.size(); ++k) {
			std::memcpy(town.nodes[k].bytes.data(), nodes.bytes.data() + k * 0x14, 0x14);
		}
	}
	std::vector<OfficeMemory> offices;
	copy_blocks(state.at("O"), offices);
	save.offices.resize(offices.size());
	for (size_t i = 0; i < offices.size(); ++i) {
		save.offices[i].memory = offices[i];
	}
	std::vector<MerchantMemory> merchants;
	copy_blocks(state.at("M"), merchants);
	save.merchants.resize(merchants.size());
	save.merchant_count = int(merchants.size());
	for (size_t i = 0; i < merchants.size(); ++i) {
		save.merchants[i].memory = merchants[i];
	}
	const Region& p = state.at("P");
	save.ships.resize(p.bytes.size() / ShipMemory::kSize);
	for (size_t i = 0; i < save.ships.size(); ++i) {
		std::memcpy(save.ships[i].memory.bytes.data(), p.bytes.data() + i * ShipMemory::kSize, ShipMemory::kSize);
	}
	copy_blocks(state.at("C"), save.captains);
	copy_blocks(state.at("W"), save.world_records);
	if (const Region* x = find(state, "X")) {
		copy_blocks(*x, save.sieges);
	}
	if (const Region* e = find(state, "E")) {
		copy_blocks(*e, save.events);
	}
	if (const Region* y = find(state, "Y")) {
		save.town_travel = y->bytes;
	}
	if (const Region* v = find(state, "V")) {
		copy_blocks(*v, save.convoys);
	}
	for (size_t i = 0; i < save.player_towns.size(); ++i) {
		if (const Region* k = find(state, "K" + std::to_string(i))) {
			save.player_towns[i] = k->bytes;
		}
	}
	save.rand_state = get<uint32_t>(state.at("r").bytes.data(), 0);
	save.player = int(save.globals.u32(global::kPlayer));
}

// El estado del juego que debería haber según p3vita (con las direcciones y,
// en lo que no se compara, los bytes del juego).
State dump_save(const SaveGame& save, const State& game) {
	State s = game;
	std::memcpy(s["G"].bytes.data(), save.globals.memory.bytes.data(), s["G"].bytes.size());
	std::memcpy(s["A"].bytes.data(), save.settings.bytes.data(), s["A"].bytes.size());
	Region& t = s["T"];
	t.bytes.resize(save.towns.size() * TownMemory::kSize);
	for (size_t i = 0; i < save.towns.size(); ++i) {
		const SavedTown& town = save.towns[i];
		std::memcpy(t.bytes.data() + i * TownMemory::kSize, town.memory.bytes.data(), TownMemory::kSize);
		Region& rec = s["R" + std::to_string(i)];
		rec.bytes.resize(town.records.size() * 8);
		std::memcpy(rec.bytes.data(), town.records.data(), rec.bytes.size());
		s["D" + std::to_string(i)].bytes = town.grid;
		Region& nodes = s["N" + std::to_string(i)];
		nodes.bytes.resize(town.nodes.size() * 0x14);
		for (size_t k = 0; k < town.nodes.size(); ++k) {
			std::memcpy(nodes.bytes.data() + k * 0x14, town.nodes[k].bytes.data(), 0x14);
		}
	}
	std::vector<OfficeMemory> offices;
	for (const SavedOffice& o : save.offices) {
		offices.push_back(o.memory);
	}
	store_blocks(offices, s["O"]);
	std::vector<MerchantMemory> merchants;
	for (const SavedMerchant& m : save.merchants) {
		merchants.push_back(m.memory);
	}
	store_blocks(merchants, s["M"]);
	Region& p = s["P"];
	p.bytes.resize(save.ships.size() * ShipMemory::kSize);
	for (size_t i = 0; i < save.ships.size(); ++i) {
		std::memcpy(p.bytes.data() + i * ShipMemory::kSize, save.ships[i].memory.bytes.data(), ShipMemory::kSize);
	}
	store_blocks(save.captains, s["C"]);
	store_blocks(save.world_records, s["W"]);
	if (s.count("X")) {
		store_blocks(save.sieges, s["X"]);
	}
	if (s.count("E")) {
		store_blocks(save.events, s["E"]);
	}
	if (s.count("Y")) {
		s["Y"].bytes = save.town_travel;
	}
	if (s.count("V")) {
		store_blocks(save.convoys, s["V"]);
	}
	for (size_t i = 0; i < save.player_towns.size(); ++i) {
		const std::string k = "K" + std::to_string(i);
		if (s.count(k)) {
			s[k].bytes = save.player_towns[i];
		}
	}
	s["r"].bytes.resize(4);
	put<uint32_t>(s["r"].bytes.data(), 0, save.rand_state);
	return s;
}

Location locate(const State& state, uint32_t a) {
	Location l;
	auto in = [&](const char* name, uint32_t size, Location::Kind kind) {
		const Region* r = find(state, name);
		if (!r || a < r->address || a >= r->address + r->bytes.size()) {
			return false;
		}
		l.kind = kind;
		l.index = size ? int((a - r->address) / size) : 0;
		l.offset = size ? (a - r->address) % size : a - r->address;
		return true;
	};
	if (a >= GlobalMemory::kBegin && a < GlobalMemory::kEnd) {
		l.kind = Location::Global;
		l.offset = a;
		return l;
	}
	if (in("A", 0, Location::Settings) || in("T", TownMemory::kSize, Location::Town) ||
		in("O", OfficeMemory::kSize, Location::Office) || in("M", MerchantMemory::kSize, Location::Merchant) ||
		in("P", 0x180, Location::Ship) || in("C", 0x10, Location::Captain) || in("W", 0x14, Location::WorldRecord) ||
		in("X", SiegeMemory::kSize, Location::Siege) || in("V", 0x3C, Location::Convoy)) {
		return l;
	}
	for (const auto& [name, r] : state) {
		if (name[0] == 'N') {
			for (size_t k = 0; k * 0x14 < r.bytes.size(); ++k) {
				const uint32_t node = get<uint32_t>(r.bytes.data(), k * 0x14);
				if (a >= node && a < node + 0x14) {
					return {Location::Node, std::atoi(name.c_str() + 1), int(k), a - node};
				}
			}
		} else if ((name[0] == 'R' || name[0] == 'D') && a >= r.address && a < r.address + r.bytes.size()) {
			const bool records = name[0] == 'R';
			return {records ? Location::Records : Location::Grid, std::atoi(name.c_str() + 1),
				records ? int((a - r.address) / 8) : -1, records ? (a - r.address) % 8 : a - r.address};
		}
	}
	return l;
}

bool looks_like_pointer(uint32_t v) {
	return (v >= 0x0100'0000 && v < 0x0110'0000) || (v >= 0x0400'0000 && v < 0x0500'0000) ||
		(v >= 0x1000'0000 && v < 0x1800'0000);
}

std::string describe(const std::string& region, size_t offset) {
	char text[96];
	const char* name = region.c_str();
	const size_t sizes[] = {TownMemory::kSize, OfficeMemory::kSize, MerchantMemory::kSize, 0x180, 0x10, 0x14,
		SiegeMemory::kSize, 0x3C};
	const char* names = "TOMPCWXV";
	if (const char* k = std::strchr(names, name[0]); k && name[1] == '\0') {
		const size_t size = sizes[k - names];
		std::snprintf(text, sizeof(text), "%s[%zu]+0x%zx", name, offset / size, offset % size);
	} else if (region == "G") {
		std::snprintf(text, sizeof(text), "global 0x%zx", GlobalMemory::kBegin + offset);
	} else {
		std::snprintf(text, sizeof(text), "%s+0x%zx", name, offset);
	}
	return text;
}

// Globales que llena el lector de partidas: cabecera del mundo, del objeto de
// los barcos, el jugador, el factor de la dificultad del comercio y el objeto
// 0x6DE588 (sin sus punteros).
bool loaded_global(uint32_t a) {
	return (a >= global::kWorld && a < global::kWorld + 0xE4) || (a >= global::kShips + 0xE0 && a < global::kShips + 0x100) ||
		(a >= 0x6DE588 && a < 0x6DE588 + 0xA08) ||
		(a >= global::kPlayer && a < global::kPlayer + 4) || (a >= 0x6DE43C && a < 0x6DE440) ||
		(a >= 0x6DD744 && a < 0x6DD74A) || (a >= global::kShips + 0x100 && a < global::kShips + 0x108);
}

// Compara y escribe las diferencias. Devuelve cuántas hay. Con only_loaded,
// de los globales solo compara los que llena el lector de partidas.
int compare(const State& ours, const State& game, int& budget, const std::string& title, bool only_loaded = false) {
	int differences = 0;
	bool header = false;
	auto report = [&](const std::string& what) {
		++differences;
		if (budget <= 0) {
			return;
		}
		if (!header) {
			std::printf("%s\n", title.c_str());
			header = true;
		}
		std::printf("    %s\n", what.c_str());
		--budget;
	};
	for (const auto& [name, g] : game) {
		auto it = ours.find(name);
		if (it == ours.end()) {
			report(name + ": falta en p3vita");
			continue;
		}
		const std::vector<uint8_t>& a = it->second.bytes;
		const std::vector<uint8_t>& b = g.bytes;
		if (a.size() != b.size()) {
			report(name + ": tamaño " + std::to_string(a.size()) + " en p3vita y " + std::to_string(b.size()) +
				" en el juego");
			continue;
		}
		const bool town = name == "T";
		for (size_t i = 0; i < a.size(); ++i) {
			if (a[i] == b[i] ||
				(only_loaded && name == "G" && !loaded_global(GlobalMemory::kBegin + uint32_t(i)))) {
				continue;
			}
			// La palabra de 4 bytes alineada que lo contiene.
			const size_t w = i & ~size_t(3);
			const uint32_t va = w + 4 <= a.size() ? get<uint32_t>(a.data(), w) : a[i];
			const uint32_t vb = w + 4 <= b.size() ? get<uint32_t>(b.data(), w) : b[i];
			const size_t field = town ? w % TownMemory::kSize : w;
			// Punteros: los de la ciudad a sus registros, rejilla y nodos, los
			// del comerciante a su nombre (0x512A60/0x512AE0) y el siguiente de
			// cada nodo.
			const size_t merchant_field = w % MerchantMemory::kSize;
			// La ruta de un barco ([+0x24]) va aparte en p3vita.
			const bool pointer = looks_like_pointer(vb) || (name == "P" && w % ShipMemory::kSize == 0x24) ||
				(town && (field == 0x75C || field == 0x80C || field == 0x9B4)) || (name[0] == 'N' && w % 0x14 == 0) ||
				(name == "M" && (merchant_field == 0xE4 || merchant_field == 0xE8));
			if (!pointer) {
				char text[160];
				std::snprintf(text, sizeof(text), "%s: juego %08x, p3vita %08x", describe(name, w).c_str(), vb, va);
				report(text);
			}
			i = w + 3;
		}
	}
	return differences;
}

}  // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "uso: tick_probe <fichero.probe> [--continuo] [--solo FUNCIÓN] [--max N]\n");
		return 2;
	}
	bool continuous = false;
	bool ai = true;
	std::string load;
	std::string cpr;
	uint32_t only = 0;
	int budget = 60;
	for (int i = 2; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--continuo") {
			continuous = true;
		} else if (arg == "--carga" && i + 1 < argc) {
			load = argv[++i];
		} else if (arg == "--sin-ia") {
			ai = false;
		} else if (arg == "--cpr" && i + 1 < argc) {
			cpr = argv[++i];
		} else if (arg == "--solo" && i + 1 < argc) {
			only = uint32_t(std::strtoul(argv[++i], nullptr, 16));
		} else if (arg == "--max" && i + 1 < argc) {
			budget = std::atoi(argv[++i]);
		}
	}
	ProbeRegistry registry;
	register_probes_shared(registry);
	register_probes_core(registry);
	register_probes_population(registry);
	register_probes_businesses(registry);
	register_probes_facilities(registry);
	register_probes_offices(registry);
	register_probes_construction(registry);
	register_probes_ship_ai(registry);
	register_probes_ship_trade(registry);

	Input in(argv[1]);
	if (!in.ok() || !in.expect("P3PROBE1")) {
		std::fprintf(stderr, "%s no es un fichero de sondas\n", argv[1]);
		return 1;
	}
	State game;
	read_regions(in, game);
	SaveGame save;
	std::string error;
	std::unique_ptr<CprArchive> archive;
	std::unique_ptr<NavData> nav;
	std::unique_ptr<Sailing> sailing;
	std::deque<std::vector<uint8_t>> pending_sail;  // Órdenes de zarpar que esperan a 537670.
	if (!load.empty()) {
		std::ifstream f(load, std::ios::binary);
		const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		if (!parse_savegame(data.data(), data.size(), save, &error)) {
			std::fprintf(stderr, "%s: %s\n", load.c_str(), error.c_str());
			return 1;
		}
		if (!continuous) {
			// Los objetos de asedio (0x633730) aún no se leen.
			State loaded = game;
			loaded.erase("X");
			const int d = compare(dump_save(save, loaded), loaded, budget, "carga de " + load + ":", true);
			std::printf("carga: %d diferencias\n", d);
			return d ? 1 : 0;
		}
		// Con --continuo, la simulación parte de lo que ha leído p3vita y de
		// los objetos de asedio del juego.
		if (const Region* x = find(game, "X")) {
			copy_blocks(*x, save.sieges);
		}
		if (!cpr.empty()) {
			archive = CprArchive::open(cpr, &error);
			nav = archive ? NavData::load(*archive, &error) : nullptr;
			if (!nav) {
				std::fprintf(stderr, "%s: %s\n", cpr.c_str(), error.c_str());
				return 1;
			}
			sailing = std::make_unique<Sailing>(save, *nav, load_sea_map_towns(*archive));
			sailing->assign_loaded_routes();
			sailing->set_ai(ai);
			// La actualización de barcos y las órdenes de zarpar, que el oráculo
			// ejecuta al final de cada tick (las de las ciudades no).
			registry[0x506720] = [&](SaveGame&, const ProbeCall&) -> std::optional<uint32_t> {
				sailing->update_ships();
				return std::nullopt;
			};
			registry[0x537670] = [&](SaveGame&, const ProbeCall&) -> std::optional<uint32_t> {
				if (!pending_sail.empty()) {
					int32_t f[5];
					std::memcpy(f, pending_sail.front().data(), sizeof(f));
					pending_sail.pop_front();
					sailing->set_sail(f[1], f[2], f[3], f[4]);
				}
				return std::nullopt;
			};
		}
	} else if (continuous) {
		build_save(game, save);
	}
	std::map<uint32_t, std::pair<int, int>> results;  // función → {bien, mal}
	int missing = 0;
	while (in.ok()) {
		const uint8_t tag = in.u8();
		if (tag != 'S') {
			break;
		}
		ProbeCall call;
		call.function = in.u32();
		call.tick = in.u32();
		call.ecx = in.u32();
		call.esp = in.u32();
		for (uint32_t& a : call.args) {
			a = in.u32();
		}
		apply_diff(in, game);
		const uint32_t eax = in.u32();
		State before = game;
		apply_diff(in, game);
		const uint32_t operations = in.u32();
		std::vector<std::vector<uint8_t>> queued;
		for (uint32_t k = 0; k < operations; ++k) {
			queued.push_back(in.bytes(in.u32()));
		}
		if (only && call.function != only) {
			continue;
		}
		auto fn = registry.find(call.function);
		if (fn == registry.end()) {
			if (missing++ == 0) {
				std::printf("0x%x no está registrada en tick_probe\n", call.function);
			}
			continue;
		}
		call.locate = [&before](uint32_t a) { return locate(before, a); };
		if (continuous) {
			// Lo que cambia fuera del tick de ciudad: la fecha y el tick.
			std::memcpy(&save.globals.u8(global::kWorldDay), &before.at("G").bytes[global::kWorldDay - GlobalMemory::kBegin],
				6);
			save.globals.u32(global::kWorldTick) = get<uint32_t>(before.at("G").bytes.data(),
				global::kWorldTick - GlobalMemory::kBegin);
		} else {
			build_save(before, save);
		}
		save.queued_operations.clear();
		const std::optional<uint32_t> ours = fn->second(save, call);
		char title[128];
		std::snprintf(title, sizeof(title), "0x%x tick %u ecx %08x arg %08x %08x:", call.function, call.tick, call.ecx,
			call.args[0], call.args[1]);
		int d = compare(dump_save(save, game), game, budget, title, !load.empty());
		if (ours && ((*ours ^ eax) & call.eax_mask)) {
			if (budget-- > 0) {
				std::printf("%s\n    eax: juego %08x, p3vita %08x\n", title, eax, *ours);
			}
			++d;
		}
		if (save.queued_operations != queued) {
			if (budget-- > 0) {
				std::printf("%s\n    operaciones encoladas: juego %zu, p3vita %zu\n", title, queued.size(),
					save.queued_operations.size());
			}
			++d;
		}
		auto& r = results[call.function];
		(d ? r.second : r.first)++;
		for (const std::vector<uint8_t>& op : save.queued_operations) {
			if (op.size() >= 0x14 && get<uint32_t>(op.data(), 0) == 0) {
				pending_sail.push_back(op);
			}
		}
	}
	int bad = 0;
	for (const auto& [f, r] : results) {
		std::printf("0x%x: %d llamadas iguales, %d distintas\n", f, r.first, r.second);
		bad += r.second;
	}
	if (missing) {
		std::printf("%d llamadas a funciones sin registrar\n", missing);
	}
	return bad ? 1 : 0;
}
