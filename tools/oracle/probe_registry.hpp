#pragma once

// Registro de las funciones que tick_probe sabe repetir: para cada dirección
// del juego, cómo llamar a su traducción en src/p3data/town_tick con los
// argumentos de la muestra. Cada parte de la simulación registra las suyas en
// probes_<parte>.cpp.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "p3data/savegame.hpp"

namespace p3::probe {

// Qué objeto del juego hay en una dirección del emulador.
struct Location {
	enum Kind { None, Global, Settings, Town, Office, Merchant, Ship, Captain, WorldRecord, Siege, Node, Records, Grid };
	Kind kind = None;
	int index = -1;  // Ciudad, oficina, comerciante... (en Node, Records y Grid, la ciudad).
	int item = -1;  // En Node, el nodo dentro de la lista de la ciudad; en Records, el registro.
	uint32_t offset = 0;  // Desde el principio del objeto (en Global, la dirección).
};

// Una llamada grabada por town_oracle.py.
struct ProbeCall {
	uint32_t function = 0;
	uint32_t tick = 0;
	uint32_t ecx = 0;
	uint32_t esp = 0;  // Al entrar: apunta a la dirección de retorno.
	std::array<uint32_t, 8> args{};  // La pila al entrar, sin la dirección de retorno.
	std::function<Location(uint32_t)> locate;
	// Bits de eax que se comparan: la sonda de una función que devuelve un u8 o
	// un u16 en al/ax (el resto de eax queda con basura) pone 0xFF o 0xFFFF.
	mutable uint32_t eax_mask = 0xFFFFFFFF;

	// El u32 de la pila del llamador en `address` si cae entre los argumentos
	// grabados (para punteros a variables locales del llamador).
	std::optional<uint32_t> stack(uint32_t address) const {
		if (address < esp + 4 || address >= esp + 4 + 4 * args.size() || (address - esp) % 4) {
			return std::nullopt;
		}
		return args[(address - esp - 4) / 4];
	}

	// Atajos: el objeto que empieza exactamente en `address` (o -1).
	int town(uint32_t address) const { return exact(address, Location::Town); }
	int office(uint32_t address) const { return exact(address, Location::Office); }
	int merchant(uint32_t address) const { return exact(address, Location::Merchant); }
	int ship(uint32_t address) const { return exact(address, Location::Ship); }
	// Instalación de una ciudad: {ciudad, instalación} o {-1, -1}.
	std::pair<int, int> facility(uint32_t address) const {
		const Location l = locate(address);
		if (l.kind != Location::Town || l.offset < 0x840 || l.offset >= 0x840 + 21 * 16 || (l.offset - 0x840) % 16) {
			return {-1, -1};
		}
		return {l.index, int(l.offset - 0x840) / 16};
	}

private:
	int exact(uint32_t address, Location::Kind kind) const {
		const Location l = locate(address);
		return l.kind == kind && l.offset == 0 ? l.index : -1;
	}
};

// Ejecuta la traducción. Devuelve el eax que debe comparar, si la función
// devuelve algo, o nullopt.
using ProbeFunction = std::function<std::optional<uint32_t>(data::SaveGame&, const ProbeCall&)>;
using ProbeRegistry = std::map<uint32_t, ProbeFunction>;

void register_probes_shared(ProbeRegistry& r);
void register_probes_core(ProbeRegistry& r);
void register_probes_population(ProbeRegistry& r);
void register_probes_businesses(ProbeRegistry& r);
void register_probes_facilities(ProbeRegistry& r);
void register_probes_offices(ProbeRegistry& r);
void register_probes_construction(ProbeRegistry& r);

}  // namespace p3::probe
