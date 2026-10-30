#pragma once

#include <array>

namespace p3::data {

// Mercancías, en el orden de sus ids en el juego (WareId de P3Modding). Las 20
// primeras se comercian; las 4 últimas son armas de la milicia.
constexpr int kWareCount = 24;
constexpr int kTradeWareCount = 20;

enum class WareUnit { Barrel, Load, Piece };

struct WareInfo {
	const char* name;  // Como en la versión española del juego.
	WareUnit unit;
};

// El juego guarda las cantidades en unidades internas: un barril son 200, una
// carga 2000 y una pieza de armamento 10.
constexpr std::array<WareInfo, kWareCount> kWares = {{
	{"Grano", WareUnit::Load},
	{"Carne", WareUnit::Load},
	{"Pescado", WareUnit::Load},
	{"Cerveza", WareUnit::Barrel},
	{"Sal", WareUnit::Barrel},
	{"Miel", WareUnit::Barrel},
	{"Especias", WareUnit::Barrel},
	{"Vino", WareUnit::Barrel},
	{"Telas", WareUnit::Barrel},
	{"Pieles", WareUnit::Barrel},
	{"Aceite", WareUnit::Barrel},
	{"Madera", WareUnit::Load},
	{"Herramientas", WareUnit::Barrel},
	{"Cuero", WareUnit::Barrel},
	{"Lana", WareUnit::Load},
	{"Brea", WareUnit::Barrel},
	{"Hierro", WareUnit::Load},
	{"Cáñamo", WareUnit::Load},
	{"Alfarería", WareUnit::Barrel},
	{"Ladrillos", WareUnit::Load},
	{"Espadas", WareUnit::Piece},
	{"Arcos", WareUnit::Piece},
	{"Ballestas", WareUnit::Piece},
	{"Mosquetones", WareUnit::Piece},
}};

constexpr int ware_scale(WareUnit unit) {
	return unit == WareUnit::Barrel ? 200 : unit == WareUnit::Load ? 2000 : 10;
}

}  // namespace p3::data
