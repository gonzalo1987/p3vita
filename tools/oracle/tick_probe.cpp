// Repite con src/p3data/town_tick las llamadas que grabó town_oracle.py y
// compara el resultado con el del juego, byte a byte, en la memoria de las
// ciudades, oficinas, comerciantes, barcos, capitanes, globales...
//
// uso: tick_probe <fichero.probe> [--continuo] [--cpr p2arch0_eng.cpr] [--solo FUNCIÓN] [--max N]
//        tick_probe <fichero.probe> --carga <partida.raw> [--continuo [--cpr p2arch0_eng.cpr [--sin-ia]
//          [--sin-convoyes] [--no-storms]]] [--max N] [--write OUT]
// --no-storms: without the storms (0x5053C0), as town_oracle.py --no-storms.
// --no-pirates: without the pirates (battle/pirates.cpp) and their battles, as town_oracle.py
// --no-pirates; they are on by default (--pirates is still accepted).
// --no-disband: with the pirates, a convoy at its nest is not disbanded, as town_oracle.py --no-disband.
// --battles: kept for older scripts; the battles go with the pirates now (Sailing::set_pirates), as in
// town_oracle.py.
// --fix-game-bugs: with the fixes to the game's bugs (game_fixes.hpp), off by default.
// --storms (on by default now) is still accepted.
// --exe PATH: the game's executable whose texts the battles write into their memory (Sailing::set_texts);
// by default the Patrician3.exe next to the --cpr file, the oracle's English one.
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
// reached as the game's save function does (savegame_write.hpp), and its .pst
// to OUT.pst, to compare with town_oracle.py --write
// (tools/oracle/compare_written.py). With --carga and --pst FILE it reads an
// uncompressed .pst after the save (statistics.hpp).
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

#include "p3data/game_fixes.hpp"
#include "p3data/game_texts.hpp"
#include "p3data/navigation.hpp"
#include "p3data/sailing.hpp"
#include "p3data/savegame.hpp"
#include "p3data/savegame_write.hpp"
#include "p3data/statistics.hpp"
#include "p3data/seamap.hpp"
#include "probe_registry.hpp"

using namespace p3::data;
using namespace p3::probe;

namespace {
std::vector<uint32_t> g_battle_addresses;  // region BA: the emulator's address of each block of BT
}  // namespace

int p3::probe::battle_block(uint32_t address) {
	for (size_t i = 0; i < g_battle_addresses.size(); ++i) {
		if (g_battle_addresses[i] == address && address != 0) {
			return int(i);
		}
	}
	return -1;
}

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
	for (int k = 0; k < 5; ++k) {  // The statistics (town_oracle.py --statistics).
		if (const Region* r = find(state, "S" + std::to_string(k))) {
			read_statistics_region(save, k, r->bytes);
		}
	}
	if (const Region* ba = find(state, "BA")) {
		g_battle_addresses.assign(ba->bytes.size() / 4, 0);
		std::memcpy(g_battle_addresses.data(), ba->bytes.data(), g_battle_addresses.size() * 4);
	}
	if (const Region* bt = find(state, "BT")) {
		// The battles: u32 number of blocks and, for each, u32 size and the bytes.
		const std::vector<uint8_t>& b = bt->bytes;
		save.battles.blocks.clear();
		size_t o = 4;
		const uint32_t n = b.size() >= 4 ? get<uint32_t>(b.data(), 0) : 0;
		for (uint32_t k = 0; k < n && o + 4 <= b.size(); ++k) {
			const uint32_t size = get<uint32_t>(b.data(), o);
			o += 4;
			save.battles.blocks.emplace_back(b.begin() + std::ptrdiff_t(o), b.begin() + std::ptrdiff_t(o + size));
			o += size;
		}
		if (save.battles.blocks.empty()) {
			save.battles = BattleGraph{};
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
	for (int k = 0; k < 5; ++k) {  // The statistics (town_oracle.py --statistics).
		if (s.count("S" + std::to_string(k))) {
			s["S" + std::to_string(k)].bytes = statistics_region(save, k);
		}
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
	if (auto bt = s.find("BT"); bt != s.end()) {
		std::vector<uint8_t>& b = bt->second.bytes;
		const BattleGraph graph = save.battles.normalized();  // in the oracle's order
		b.assign(4, 0);
		put<uint32_t>(b.data(), 0, uint32_t(graph.blocks.size()));
		for (const std::vector<uint8_t>& block : graph.blocks) {
			const size_t o = b.size();
			b.resize(o + 4);
			put<uint32_t>(b.data(), o, uint32_t(block.size()));
			b.insert(b.end(), block.begin(), block.end());
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

// Whether the word at `w` of the game's region `name` (bytes `game`, value
// `v`) is a pointer, which p3vita doesn't have or keeps elsewhere, so it is
// not compared. The globals (G) and the armies (J) have pointers in places
// not all known: there, every word that looks like one (looks_like_pointer:
// the oracle's stack, save file and heap). Elsewhere only the known pointer
// fields; the rest is compared even if it looks like a pointer (a building
// record, a grid row or an event's bytes often do).
bool pointer_field(const std::string& name, size_t w, const std::vector<uint8_t>& game, uint32_t v) {
	const char kind = name[0];
	if (name == "G" || kind == 'J') {
		return looks_like_pointer(v);
	}
	if (name == "T") {
		const size_t field = w % TownMemory::kSize;
		return field == 0x75C || field == 0x80C || field == 0x9B4;  // records, grid, nodes [0x9B4]
	}
	if (name == "M") {
		const size_t field = w % MerchantMemory::kSize;
		return field == 0xE4 || field == 0xE8;  // the name's strings (0x512A60/0x512AE0)
	}
	if (name == "P") {
		return w % ShipMemory::kSize == 0x24;  // the route, apart in p3vita
	}
	if (name == "X") {
		const size_t field = w % SiegeMemory::kSize;
		return field == 0x8 || field == 0x101C || field == 0x1020;  // the army and the two lists
	}
	if (kind == 'N') {
		return w % 0x14 == 0;  // each node's address
	}
	if (kind == 'Z' && name.size() == 2) {
		return w == 0x0 || w == 0x4;  // the nest's map and the pool (p3vita: 1 if there is one)
	}
	if (name.size() == 3 && name[0] == 'Z' && name[1] == 'M') {
		return w == 0x68;  // the nest map's cells (region ZDi)
	}
	if (name == "BT") {
		// The battles: their own pointers are BATTLE_POINTER | block << 16 |
		// offset and compared; what still looks like a pointer points
		// outside them (the navigation's points of a scene's ship...), but
		// for the fields that are numbers (BattleGraph::number_field).
		if (!looks_like_pointer(v)) {
			return false;
		}
		// The game's graph, to know the pages of bits (BattleGraph::bit_pages).
		BattleGraph graph;
		graph.blocks.clear();
		std::vector<size_t> starts;
		size_t at = 4;
		uint32_t n = 0;
		std::memcpy(&n, game.data(), std::min<size_t>(4, game.size()));
		for (uint32_t k = 0; k < n && at + 4 <= game.size(); ++k) {
			uint32_t size = 0;
			std::memcpy(&size, game.data() + at, 4);
			at += 4;
			starts.push_back(at);
			graph.blocks.emplace_back(game.begin() + std::ptrdiff_t(std::min(at, game.size())),
				game.begin() + std::ptrdiff_t(std::min<size_t>(at + size, game.size())));
			at += size;
		}
		const std::vector<bool> pages = graph.bit_pages();
		for (size_t k = 0; k < starts.size(); ++k) {
			if (w >= starts[k] && w < starts[k] + graph.blocks[k].size()) {
				return !pages[k] && !BattleGraph::number_field(graph.blocks[k], w - starts[k]);
			}
		}
		return true;
	}
	if (name == "B") {
		// The letters' buffers (load_mail: p3vita keeps 0 there).
		const size_t field = w % 0x10;
		const uint8_t type = game[w - field + 4];
		const bool buffer = type == 0x29 || type == 0x47 || type == 0x63 || type == 0x83;
		const bool record = (type >= 0x3C && type <= 0x40) || type == 0x71;
		return (field == 0x8 && (buffer || record)) || (field == 0xC && record);
	}
	if (name == "E") {
		// The events' objects (load_events: p3vita keeps 0 there).
		const size_t field = w % 0x18;
		const uint16_t type = uint16_t(game[w - field + 6] | game[w - field + 7] << 8);
		return (type == 0x1B && (field == 0x8 || field == 0xC)) || (type == 0x34 && field == 0x8);
	}
	return false;
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
		(a >= global::kShips + 0x20 && a < global::kShips + 0xE0) ||
		// The header of the statistics (0x6DFC90): the u16 counters and the
		// bytes [+0x20..+0x23], not its pointers (regions S0..S4).
		(a >= 0x6DFC90 && a < 0x6DFC9C) || (a >= 0x6DFCB0 && a < 0x6DFCB4);
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

// The first block of the battles' graph (region BT: u32 number of blocks and,
// for each, u32 size and the bytes) whose size differs.
std::string battle_blocks_difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
	auto sizes = [](const std::vector<uint8_t>& v) {
		std::vector<uint32_t> out;
		size_t at = 4;
		const uint32_t n = v.size() >= 4 ? get<uint32_t>(v.data(), 0) : 0;
		for (uint32_t i = 0; i < n && at + 4 <= v.size(); ++i) {
			out.push_back(get<uint32_t>(v.data(), at));
			at += 4 + out.back();
		}
		return out;
	};
	const std::vector<uint32_t> x = sizes(a);
	const std::vector<uint32_t> y = sizes(b);
	std::string text = "BT: " + std::to_string(x.size()) + " blocks in p3vita and " + std::to_string(y.size()) +
		" in the game";
	for (size_t i = 0; i < x.size() || i < y.size(); ++i) {
		const uint32_t p = i < x.size() ? x[i] : 0;
		const uint32_t q = i < y.size() ? y[i] : 0;
		if (p != q) {
			char tail[96];
			std::snprintf(tail, sizeof(tail), "; block %zu: 0x%x bytes in p3vita and 0x%x in the game", i, p, q);
			text += tail;
			break;
		}
	}
	return text;
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
			if (name == "BT") {
				report(battle_blocks_difference(a, b));
			}
			continue;
		}
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
			if (!pointer_field(name, w, b, vb)) {
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
	bool pirates = true;
	bool disband = true;
	bool battles = false;
	std::string load;
	std::string cpr;
	uint32_t only = 0;
	int budget = 60;
	std::string write_path;  // --write: the save of the state reached (savegame_write.hpp).
	std::string pst_path;  // --pst: with --carga, an uncompressed .pst read after it (statistics.hpp).
	std::string exe_path;  // --exe: the game's texts (by default next to the CPR).
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
		} else if (arg == "--no-pirates") {
			pirates = false;
		} else if (arg == "--no-disband") {
			disband = false;
		} else if (arg == "--battles") {
			battles = true;
		} else if (arg == "--fix-game-bugs") {
			// What the fixes to the game's bugs change (game_fixes.hpp): the
			// differences then show where a fix acts.
			p3::data::set_fix_game_bugs(true);
		} else if (arg == "--cpr" && i + 1 < argc) {
			cpr = argv[++i];
		} else if (arg == "--solo" && i + 1 < argc) {
			only = uint32_t(std::strtoul(argv[++i], nullptr, 16));
		} else if (arg == "--max" && i + 1 < argc) {
			budget = std::atoi(argv[++i]);
		} else if (arg == "--write" && i + 1 < argc) {
			write_path = argv[++i];
		} else if (arg == "--pst" && i + 1 < argc) {
			pst_path = argv[++i];
		} else if (arg == "--exe" && i + 1 < argc) {
			exe_path = argv[++i];
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
	register_probes_statistics(registry);
	register_probes_unchecked(registry);

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
	std::unique_ptr<GameTexts> texts;
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
		sailing->set_battles(battles || pirates);
		if (exe_path.empty()) {
			const size_t slash = cpr.find_last_of('/');
			exe_path = (slash == std::string::npos ? std::string() : cpr.substr(0, slash + 1)) + "Patrician3.exe";
		}
		texts = GameTexts::load(exe_path, &error);
		if (!texts) {
			std::fprintf(stderr, "%s: %s (the battles' messages are left out)\n", exe_path.c_str(), error.c_str());
		}
		sailing->set_texts(texts.get());
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
		if (!pst_path.empty()) {
			std::ifstream p(pst_path, std::ios::binary);
			const std::vector<uint8_t> pst((std::istreambuf_iterator<char>(p)), std::istreambuf_iterator<char>());
			if (!read_statistics(pst.data(), pst.size(), save)) {
				std::fprintf(stderr, "%s: cut short\n", pst_path.c_str());
				return 1;
			}
		}
		if (!continuous) {
			// Las rutas de los barcos las da Sailing después de leer la partida.
			if (sailing) {
				sailing->load_battles();  // 0x611AE0 (battle/battle_save.cpp)
			}
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
	std::vector<std::string> family_names;  // Nor the family names.
	std::vector<std::vector<uint8_t>> town_templates;  // Ni las plantillas de las ciudades nuevas.
	if (archive) {
		SaveGame scripts;
		load_game_scripts(*archive, scripts);
		expeditions = scripts.expeditions;
		ship_names = scripts.ship_names;
		first_names = scripts.first_names;
		family_names = scripts.family_names;
		town_templates = scripts.town_templates;
		if (continuous && load.empty()) {
			save.expeditions = expeditions;
			save.ship_names = ship_names;
			save.first_names = first_names;
			save.family_names = family_names;
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
			// Nor the statistics (0x54DE90), which run between 0x5315C0 and 0x535450.
			if (countdown_pending &&
				(call.tick != countdown_tick || (call.esp >= countdown_esp && call.function != 0x535450 &&
					call.function != 0x54DE90))) {
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
			save.family_names = family_names;
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
		const std::vector<uint8_t> pst = write_statistics(save);
		std::ofstream out_pst(write_path + ".pst", std::ios::binary);
		out_pst.write(reinterpret_cast<const char*>(pst.data()), std::streamsize(pst.size()));
		std::printf("%zu bytes in %s, %zu in %s.pst\n", raw.size(), write_path.c_str(), pst.size(), write_path.c_str());
	}
	return bad ? 1 : 0;
}
