// Comprueba la fecha de un tick y la velocidad de los barcos
// (src/p3data/sailing.cpp). El movimiento, la llegada a puerto y la orden de
// zarpar se comparan con el juego con tools/oracle/sail_oracle.py sobre las
// partidas reales (ver docs/oracle.md).

#include <cstdio>

#include "p3data/sailing.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
	if (!ok) {
		std::printf("falla: %s\n", what);
		++failures;
	}
}

SavedShip snaikka() {
	SavedShip ship;
	ship.status() = kShipDocked;
	ship.type() = uint8_t(ShipType::Snaikka);
	ship.capacity() = 30000;
	ship.health() = ship.max_health() = 42000;
	return ship;
}

}  // namespace

int main() {
	// missav/1.pat: 13 de mayo de 1300.
	const GameDate start = date_from_tick(121506024);
	expect(start.day == 13 && start.month == 4 && start.year == 1300, "fecha de 1.pat");
	const GameDate new_year = date_from_tick(1301u * 365 * kTicksPerDay);
	expect(new_year.day == 1 && new_year.month == 0 && new_year.year == 1301, "1 de enero");
	const GameDate last = date_from_tick(1301u * 365 * kTicksPerDay - 1);
	expect(last.day == 31 && last.month == 11 && last.year == 1300, "31 de diciembre");
	const GameDate march = date_from_tick((1300u * 365 + 59) * kTicksPerDay);
	expect(march.day == 1 && march.month == 2, "1 de marzo: sin bisiestos");

	// Una Snaikka vacía y sana: 256 · 635 >> 10.
	SavedShip ship = snaikka();
	expect(Sailing::speed(ship) == 158, "velocidad de una Snaikka vacía");
	// Llena: (4096 − 614) · 635 >> 12 = 539, por 256 >> 10.
	ship.used() = 30000;
	expect(Sailing::speed(ship) == 134, "velocidad de una Snaikka llena");
	// Con el casco por debajo de un tercio la velocidad baja hasta un 80 %.
	ship.used() = 0;
	ship.health() = 1;
	expect(Sailing::speed(ship) == 0xCC * 635u >> 10, "velocidad con el casco destrozado");
	ship.type() = uint8_t(ShipType::Craier);
	ship.health() = ship.max_health();
	expect(Sailing::speed(ship) == 173, "velocidad de un Craier vacío");

	if (failures == 0) {
		std::printf("navegación: todo bien\n");
	}
	return failures == 0 ? 0 : 1;
}
