// Repite con src/p3data/town_tick las llamadas que grabó town_oracle.py y
// compara el resultado con el del juego, byte a byte, en la memoria de las
// ciudades, oficinas, comerciantes, barcos, capitanes, globales...
//
// uso: tick_probe <fichero.probe> [--continuo] [--cpr p2arch0_eng.cpr] [--solo FUNCIÓN] [--max N]
//        tick_probe <fichero.probe> --carga <partida.raw> [--continuo [--cpr p2arch0_eng.cpr [--sin-ia]
//          [--sin-convoyes] [--no-storms]]] [--max N] [--write OUT]
// --no-storms: without the storms (0x5053C0), as town_oracle.py --no-storms.
// --pirates: the pirate convoys move (battle/pirates.cpp), as town_oracle.py --pirates.
// --no-disband: with --pirates, a convoy at its nest is not disbanded, as town_oracle.py --no-disband.
// --storms (on by default now) is still accepted.
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
// (506720) y las operaciones encoladas de una grabación hecha con
// town_oracle.py --barcos: el mundo entero, tick a tick (--sin-ia y
// --sin-convoyes, como en el oráculo). Las operaciones, grabando el
// ejecutor (535760, que lleva la operación en la pila) o solo las órdenes de
// zarpar (537670), pero no las dos.
//
// With --write OUT, at the end it writes the uncompressed .pat of the state
// reached as the game's save function does (savegame_write.hpp), to compare
// with town_oracle.py --write (tools/oracle/compare_written.py).
//
// Con --cpr, las funciones de los barcos que necesitan la navegación (las de
// los convoyes, la actualización de barcos) se pueden repetir también una a
// una: las sondas la tienen en probe_sailing(). Las rutas de los barcos van en
// la región Q de la grabación.

#include <algorithm>
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
#include "p3data/savegame_write.hpp"
#include "p3data/seamap.hpp"
#include "probe_registry.hpp"

using namespace p3::data;
using namespace p3::probe;

namespace {

Sailing* g_sailing = nullptr;

}  // namespace

Sailing* p3::probe::probe_sailing() {
	return g_sailing;
}

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

// Lo que cuelga de los objetos de asedio (regiones Ji, Ui, Li y Hi).
void load_siege_extras(const State& state, SaveGame& save) {
	save.siege_extras.assign(save.sieges.size(), SiegeExtras());
	for (size_t i = 0; i < save.sieges.size(); ++i) {
		SiegeExtras& extras = save.siege_extras[i];
		const std::string n = std::to_string(i);
		if (const Region* j = find(state, "J" + n); j && j->bytes.size() == SiegeArmyMemory::kSize) {
			std::memcpy(extras.army.emplace().bytes.data(), j->bytes.data(), SiegeArmyMemory::kSize);
		}
		if (const Region* u = find(state, "U" + n)) {
			copy_blocks(*u, extras.army_targets);
		}
		if (const Region* l = find(state, "L" + n)) {
			copy_blocks(*l, extras.list_101c);
		}
		if (const Region* h = find(state, "H" + n)) {
			copy_blocks(*h, extras.list_1020);
		}
	}
}

void store_siege_extras(const SaveGame& save, State& s) {
	for (size_t i = 0; i < save.siege_extras.size(); ++i) {
		const SiegeExtras& extras = save.siege_extras[i];
		const std::string n = std::to_string(i);
		if (s.count("J" + n)) {
			std::vector<uint8_t>& b = s["J" + n].bytes;
			b.clear();
			if (extras.army) {
				b.assign(extras.army->bytes.begin(), extras.army->bytes.end());
			}
		}
		if (s.count("U" + n)) {
			store_blocks(extras.army_targets, s["U" + n]);
		}
		if (s.count("L" + n)) {
			store_blocks(extras.list_101c, s["L" + n]);
		}
		if (s.count("H" + n)) {
			store_blocks(extras.list_1020, s["H" + n]);
		}
	}
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
		load_siege_extras(state, save);
	}
	if (const Region* e = find(state, "E")) {
		copy_blocks(*e, save.events);
	}
	if (const Region* m = find(state, "B")) {
		copy_blocks(*m, save.mail);
	}
	if (const Region* l = find(state, "L")) {
		// The letters' heap records: u16 letter and 16 bytes, for each.
		save.mail_records.assign(save.mail.size(), std::nullopt);
		const std::vector<uint8_t>& b = l->bytes;
		for (size_t o = 0; o + 0x12 <= b.size(); o += 0x12) {
			const uint16_t i = get<uint16_t>(b.data(), o);
			if (i < save.mail_records.size()) {
				std::memcpy(save.mail_records[i].emplace().bytes.data(), b.data() + o + 2, 0x10);
			}
		}
	}
	if (const Region* y = find(state, "Y")) {
		save.town_travel = y->bytes;
	}
	if (const Region* v = find(state, "V")) {
		copy_blocks(*v, save.convoys);
	}
	if (const Region* q = find(state, "Q")) {
		// Rutas: u16 barco, u32 nº de puntos y los puntos. En el juego el
		// barco tiene un puntero ([+0x24]); en p3vita, su copia de la ruta.
		const std::vector<uint8_t>& b = q->bytes;
		for (size_t o = 0; o + 6 <= b.size();) {
			const uint16_t ship = get<uint16_t>(b.data(), o);
			const uint32_t count = get<uint32_t>(b.data(), o + 2);
			auto route = std::make_shared<SeaRoute>(count);
			std::memcpy(route->data(), b.data() + o + 6, 4 * size_t(count));
			o += 6 + 4 * size_t(count);
			if (ship < save.ships.size()) {
				const uint32_t pointer = save.ships[ship].memory.u32(0x24);
				save.ships[ship].set_route(route);
				save.ships[ship].memory.u32(0x24) = pointer;
			}
		}
	}
	for (size_t i = 0; i < save.player_towns.size(); ++i) {
		if (const Region* k = find(state, "K" + std::to_string(i))) {
			save.player_towns[i] = k->bytes;
		}
	}
	for (size_t i = 0; i < save.pirates.size(); ++i) {
		if (const Region* z = find(state, "Z" + std::to_string(i)); z && z->bytes.size() == 0x18) {
			std::memcpy(save.pirates[i].emplace().bytes.data(), z->bytes.data(), 0x18);
		} else if (z) {
			save.pirates[i].reset();  // No pirate (an empty region): not the one of an earlier sample.
		}
		// The map of the nest, its cells and the pool (battle/pirate_nests.hpp).
		PirateNest& nest = save.pirate_nests[i];
		nest = PirateNest{};
		if (const Region* m = find(state, "ZM" + std::to_string(i)); m && m->bytes.size() == 0x6C) {
			std::memcpy(nest.map.emplace().bytes.data(), m->bytes.data(), 0x6C);
		}
		if (const Region* d = find(state, "ZD" + std::to_string(i))) {
			nest.cells = d->bytes;
		}
		if (const Region* p = find(state, "ZP" + std::to_string(i))) {
			copy_blocks(*p, nest.pool);
		}
	}
	for (size_t i = 0; i < save.world_object_lists.size(); ++i) {
		if (const Region* f = find(state, "F" + std::to_string(i))) {
			copy_blocks(*f, save.world_object_lists[i]);
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
	store_siege_extras(save, s);
	if (s.count("E")) {
		store_blocks(save.events, s["E"]);
	}
	if (s.count("B")) {
		store_blocks(save.mail, s["B"]);
	}
	if (s.count("L")) {
		std::vector<uint8_t>& b = s["L"].bytes;
		b.clear();
		for (size_t i = 0; i < save.mail_records.size() && i < save.mail.size(); ++i) {
			const uint8_t kind = save.mail[i].u8(4);
			if (save.mail_records[i] && ((kind >= 0x3C && kind <= 0x40) || kind == 0x71)) {
				const size_t o = b.size();
				b.resize(o + 0x12);
				put<uint16_t>(b.data(), o, uint16_t(i));
				std::memcpy(b.data() + o + 2, save.mail_records[i]->bytes.data(), 0x10);
			}
		}
	}
	if (s.count("Y")) {
		s["Y"].bytes = save.town_travel;
	}
	if (s.count("V")) {
		store_blocks(save.convoys, s["V"]);
	}
	if (s.count("Q")) {
		std::vector<uint8_t>& b = s["Q"].bytes;
		b.clear();
		for (size_t i = 0; i < save.ships.size(); ++i) {
			if (const std::shared_ptr<SeaRoute>& route = save.ships[i].route) {
				const size_t o = b.size();
				b.resize(o + 6 + 4 * route->size());
				put<uint16_t>(b.data(), o, uint16_t(i));
				put<uint32_t>(b.data(), o + 2, uint32_t(route->size()));
				std::memcpy(b.data() + o + 6, route->data(), 4 * route->size());
			}
		}
	}
	for (size_t i = 0; i < save.player_towns.size(); ++i) {
		const std::string k = "K" + std::to_string(i);
		if (s.count(k)) {
			s[k].bytes = save.player_towns[i];
		}
	}
	for (size_t i = 0; i < save.pirates.size(); ++i) {
		const std::string z = "Z" + std::to_string(i);
		if (s.count(z)) {
			std::vector<uint8_t>& b = s[z].bytes;
			b.clear();
			if (save.pirates[i]) {
				b.assign(save.pirates[i]->bytes.begin(), save.pirates[i]->bytes.end());
			}
		}
		const PirateNest& nest = save.pirate_nests[i];
		if (auto m = s.find("ZM" + std::to_string(i)); m != s.end()) {
			m->second.bytes.clear();
			if (nest.map) {
				m->second.bytes.assign(nest.map->bytes.begin(), nest.map->bytes.end());
			}
		}
		if (auto d = s.find("ZD" + std::to_string(i)); d != s.end()) {
			d->second.bytes = nest.cells;
		}
		if (auto p = s.find("ZP" + std::to_string(i)); p != s.end()) {
			store_blocks(nest.pool, p->second);
		}
	}
	for (size_t i = 0; i < save.world_object_lists.size(); ++i) {
		const std::string f = "F" + std::to_string(i);
		if (s.count(f)) {
			store_blocks(save.world_object_lists[i], s[f]);
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

// Con --cpr, p3vita lee también lo de scripts/ (load_game_scripts) y se comparan
// los números de nombres, la tabla de las ciudades (sin su CString) y el número
// de puntos de las expediciones.
bool g_scripts_loaded = false;

bool script_global(uint32_t a) {
	return g_scripts_loaded && ((a >= 0x6DDB70 && a < 0x6DDB7C) || (a >= 0x6DF2E4 && a < 0x6DF2E8) ||
		(a >= 0x6DDB90 && a < 0x6DDB90 + 40 * 0x34 && ((a - 0x6DDB90) % 0x34 < 0x28 || (a - 0x6DDB90) % 0x34 >= 0x2C)));
}

// Globales que llena el lector de partidas: cabecera del mundo, del objeto de
// los barcos, el jugador, el factor de la dificultad del comercio, el objeto
// 0x6DE588 (sin sus punteros), los 15 objetos del mundo en [+0xB10] (sin el
// puntero a su lista, con su número en [+0x28]) y los contadores del buzón de
// los avisos (0x6DD734..0x6DD73B).
bool loaded_global(uint32_t a) {
	return script_global(a) ||
		(a >= global::kWorld && a < global::kWorld + 0xE4) || (a >= global::kShips + 0xE0 && a < global::kShips + 0x100) ||
		(a >= 0x6DE588 && a < 0x6DE588 + 0xA08) ||
		(a >= global::kPlayer && a < global::kPlayer + 4) || (a >= 0x6DE43C && a < 0x6DE440) ||
		(a >= global::kWorld + 0xB10 && a < global::kWorld + 0xB10 + 15 * 0x2C &&
			((a - global::kWorld - 0xB10) % 0x2C < 0x1E || (a - global::kWorld - 0xB10) % 0x2C >= 0x28)) ||
		(a >= 0x6DD744 && a < 0x6DD74A) || (a >= 0x6DD734 && a < 0x6DD73C) ||
		(a >= global::kShips + 0x100 && a < global::kShips + 0x108) ||
		(a >= global::kShips + 0x20 && a < global::kShips + 0xE0);
}

// Compara y escribe las diferencias. Devuelve cuántas hay. Con only_loaded,
// de los globales solo compara los que llena el lector de partidas.
// The first index from i on where a and b differ (n if none). Nearly all the
// state is equal after each call, so equal stretches are skipped 64 bytes at
// a time: comparing byte by byte was 87% of tick_probe's time.
size_t next_difference(const uint8_t* a, const uint8_t* b, size_t i, size_t n) {
	while (i + 64 <= n && std::memcmp(a + i, b + i, 64) == 0) {
		i += 64;
	}
	while (i < n && a[i] == b[i]) {
		++i;
	}
	return i;
}

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
			i = next_difference(a.data(), b.data(), i, a.size());
			if (i == a.size()) {
				break;
			}
			if (only_loaded && name == "G" && !loaded_global(GlobalMemory::kBegin + uint32_t(i))) {
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
	bool convoys = true;
	bool storms = true;
	bool pirates = false;
	bool disband = true;
	std::string load;
	std::string cpr;
	uint32_t only = 0;
	int budget = 60;
	std::string write_path;  // --write: the save of the state reached (savegame_write.hpp).
	for (int i = 2; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--continuo") {
			continuous = true;
		} else if (arg == "--carga" && i + 1 < argc) {
			load = argv[++i];
		} else if (arg == "--sin-ia") {
			ai = false;
		} else if (arg == "--sin-convoyes") {
			convoys = false;
		} else if (arg == "--storms") {
			storms = true;
		} else if (arg == "--no-storms") {
			storms = false;
		} else if (arg == "--pirates") {
			pirates = true;
		} else if (arg == "--no-disband") {
			disband = false;
		} else if (arg == "--cpr" && i + 1 < argc) {
			cpr = argv[++i];
		} else if (arg == "--solo" && i + 1 < argc) {
			only = uint32_t(std::strtoul(argv[++i], nullptr, 16));
		} else if (arg == "--max" && i + 1 < argc) {
			budget = std::atoi(argv[++i]);
		} else if (arg == "--write" && i + 1 < argc) {
			write_path = argv[++i];
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
	register_probes_convoys_sea(registry);
	register_probes_convoys_members(registry);
	register_probes_convoys_port(registry);
	register_probes_merchants(registry);
	register_probes_daily(registry);
	register_probes_sieges(registry);
	register_probes_events(registry);
	register_probes_events_big(registry);
	register_probes_events_towns(registry);
	register_probes_events_mayor(registry);
	register_probes_events_rare(registry);
	register_probes_alderman(registry);
	register_probes_battle(registry);

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
	if (!cpr.empty()) {
		archive = CprArchive::open(cpr, &error);
		nav = archive ? NavData::load(*archive, &error) : nullptr;
		if (!nav) {
			std::fprintf(stderr, "%s: %s\n", cpr.c_str(), error.c_str());
			return 1;
		}
		sailing = std::make_unique<Sailing>(save, *nav, load_sea_map_towns(*archive));
		sailing->set_ai(ai);
		sailing->set_convoys(convoys);
		sailing->set_storms(storms);
		sailing->set_pirates(pirates);
		sailing->set_pirate_disbanding(disband);
		g_sailing = sailing.get();
		registry[0x506720] = [&](SaveGame&, const ProbeCall&) -> std::optional<uint32_t> {
			sailing->update_ships();
			return std::nullopt;
		};
	}
	if (!load.empty()) {
		std::ifstream f(load, std::ios::binary);
		const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		if (!parse_savegame(data.data(), data.size(), save, &error)) {
			std::fprintf(stderr, "%s: %s\n", load.c_str(), error.c_str());
			return 1;
		}
		if (archive) {
			load_game_scripts(*archive, save);
			g_scripts_loaded = true;
		}
		if (!continuous) {
			// Las rutas de los barcos las da Sailing después de leer la partida.
			State loaded = game;
			loaded.erase("Q");
			const int d = compare(dump_save(save, loaded), loaded, budget, "carga de " + load + ":", true);
			std::printf("carga: %d diferencias\n", d);
			return d ? 1 : 0;
		}
		// Con --continuo, la simulación parte de lo que ha leído p3vita.
		if (sailing) {
			sailing->assign_loaded_routes();
			// Las órdenes de zarpar, que el oráculo ejecuta al final de cada
			// tick (las de las ciudades no).
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
	// Los puntos de las expediciones, que la grabación no guarda: de los datos.
	std::vector<std::array<uint16_t, 2>> expeditions;
	std::vector<std::string> ship_names;  // Tampoco los nombres de barco.
	std::array<std::vector<std::string>, 2> first_names;  // Nor the first names.
	std::vector<std::vector<uint8_t>> town_templates;  // Ni las plantillas de las ciudades nuevas.
	if (archive) {
		SaveGame scripts;
		load_game_scripts(*archive, scripts);
		expeditions = scripts.expeditions;
		ship_names = scripts.ship_names;
		first_names = scripts.first_names;
		town_templates = scripts.town_templates;
		if (continuous && load.empty()) {
			save.expeditions = expeditions;
			save.ship_names = ship_names;
			save.first_names = first_names;
			save.town_templates = town_templates;
		}
	}
	std::map<uint32_t, std::pair<int, int>> results;  // función → {bien, mal}
	std::map<uint32_t, int> skipped;  // función → muestras que la sonda no puede repetir
	int missing = 0;
	// Con --continuo: el [+0x8B] del mundo que advance_time baja tras 0x5315C0.
	bool countdown_pending = false;
	uint32_t countdown_tick = 0;
	uint32_t countdown_esp = 0;
	bool executor_seen = false;
	bool sail_seen = false;
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
		if (continuous && ((call.function == 0x535760 && call.args[5] == 0x504F3350) || call.function == 0x537670)) {
			// El ejecutor llama a 0x537670 para la orden de zarpar: grabadas las
			// dos, la orden se repetiría (las llamadas al ejecutor del oráculo
			// llevan la marca «P3OP» tras la operación).
			(call.function == 0x535760 ? executor_seen : sail_seen) = true;
			if (executor_seen && sail_seen) {
				std::fprintf(stderr, "con --continuo, graba 535760 (el ejecutor) o 537670, no las dos\n");
				return 2;
			}
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
			// Tras lo diario (0x5315C0) y las tareas programadas (0x535450),
			// advance_time baja 1 el [+0x8B] del mundo si no es 0 (como
			// advance_game_tick y el oráculo con --resto 5315c0): antes de la
			// siguiente llamada de primer nivel que no sea 0x535450.
			if (countdown_pending &&
				(call.tick != countdown_tick || (call.esp >= countdown_esp && call.function != 0x535450))) {
				uint8_t& countdown = save.globals.u8(global::kWorld + 0x8B);
				if (countdown > 0) {
					--countdown;
				}
				countdown_pending = false;
			}
		} else {
			build_save(before, save);
			save.expeditions = expeditions;
			save.ship_names = ship_names;
			save.first_names = first_names;
			save.town_templates = town_templates;
		}
		save.queued_operations.clear();
		const std::optional<uint32_t> ours = fn->second(save, call);
		if (continuous && call.function == 0x5315C0) {
			countdown_pending = true;
			countdown_tick = call.tick;
			countdown_esp = call.esp;
		}
		if (call.skip) {
			++skipped[call.function];
			continue;
		}
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
				for (size_t k = 0; k < std::max(queued.size(), save.queued_operations.size()); ++k) {
					auto hex = [](const std::vector<std::vector<uint8_t>>& ops, size_t i) {
						std::string s;
						for (size_t b = 0; i < ops.size() && b < ops[i].size() && b < 0x14; ++b) {
							char t[4];
							std::snprintf(t, sizeof(t), "%02x", ops[i][b]);
							s += t;
						}
						return s;
					};
					const std::string a = hex(queued, k);
					const std::string b = hex(save.queued_operations, k);
					if (a != b) {
						std::printf("      %zu: juego %s\n         p3vita %s\n", k, a.c_str(), b.c_str());
					}
				}
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
	for (const auto& [f, n] : skipped) {
		std::printf("0x%x: %d llamadas saltadas (desde dentro del juego)\n", f, n);
	}
	if (missing) {
		std::printf("%d llamadas a funciones sin registrar\n", missing);
	}
	if (!write_path.empty()) {
		// The uncompressed .pat of the state reached, as the game's save
		// function writes it (compare with town_oracle.py --write).
		prepare_save(save);
		const std::vector<uint8_t> raw = write_savegame(save);
		std::ofstream out(write_path, std::ios::binary);
		out.write(reinterpret_cast<const char*>(raw.data()), std::streamsize(raw.size()));
		std::printf("%zu bytes in %s\n", raw.size(), write_path.c_str());
	}
	return bad ? 1 : 0;
}
