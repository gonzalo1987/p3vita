#include "p3data/sailing.hpp"

#include <cmath>
#include <cstdlib>

#include "p3data/trade.hpp"

namespace p3::data {

namespace {

constexpr int kNoTown = 0xFF;
constexpr uint16_t kHarbourTicks = 0x40;  // Lo que tarda un barco en salir o entrar en el puerto.
constexpr uint16_t kBlockadeWait = 0x100;
constexpr int16_t kMaxReadiness = 0x700;
constexpr int32_t kCentre = 0x7FFF;  // Fracción con la que empieza cada tramo de la ruta.
// Bits de SavedTown::situation con los que los barcos no salen ni entran en
// el puerto (bloqueo o asedio); con 0x400 tampoco entran los que siguen una
// ruta automática.
constexpr uint32_t kClosedPort = 0x4000200;
constexpr uint32_t kClosedToRoutes = 0x400;
constexpr uint32_t kRaidBlocked = 0x4000000;

constexpr uint16_t kBaseSpeed[4] = {635, 693, 520, 578};  // 0x67366C, por tipo de barco.
constexpr uint8_t kMinSailors[4] = {5, 8, 10, 12};  // 0x673660
constexpr uint16_t kMonthStart[13] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};  // 0x672D78

// Conversión a entero de MSVC (__ftol): trunca; NaN o fuera de rango da 0x80000000.
int32_t ftol(double v) {
	if (!(v > -2147483649.0 && v < 2147483648.0)) {
		return INT32_MIN;
	}
	return int32_t(v);
}

int16_t whole(int32_t fixed) {
	return int16_t(uint32_t(fixed) >> 16);
}

// Cambia la fracción de una coordenada 16.16 y deja la parte entera.
int32_t with_fraction(int32_t fixed, uint16_t value) {
	return int32_t((uint32_t(fixed) & 0xFFFF0000) | value);
}

// Quita la ruta al barco (0x516840).
void clear_route(SavedShip& ship) {
	ship.route.reset();
	ship.own_route = false;
	ship.route_index = 0;
	ship.route_mark = 0;
}

// Rumbo y desplazamiento por tick hacia `target` (0x517410): tantos ticks
// como 128 · distancia / velocidad, más uno, y el rumbo de 16 pasos según
// el cuadrante y la pendiente del desplazamiento.
void set_course(SavedShip& ship, uint32_t speed, MapPoint target) {
	const int32_t dx = target.x - whole(ship.x);
	const int32_t dy = target.y - whole(ship.y);
	const int32_t steps = 1 - ftol(std::sqrt(double(dy * dy + dx * dx)) * -128.0 / double(int32_t(speed)));
	auto step = [steps](int32_t d) { return d == 0 ? 0 : (d * 65536 + (d > 0 ? 0x8000 : -0x7FFF)) / steps; };
	ship.dx = step(dx);
	ship.dy = step(dy);

	// Componentes del desplazamiento según el cuadrante: c, la que se compara
	// con e para ver cuánto se gira desde el rumbo de base.
	int base, c, e;
	if (ship.dx > 0) {
		base = ship.dy > 0 ? 9 : 5;
		c = ship.dy > 0 ? ship.dx : -ship.dy;
		e = ship.dy > 0 ? ship.dy : ship.dx;
	} else {
		base = ship.dy > 0 ? 13 : 1;
		c = ship.dy > 0 ? ship.dy : -ship.dx;
		e = ship.dy > 0 ? -ship.dx : -ship.dy;
	}
	constexpr int64_t kSteep = 0x96CC;  // tan 30° en 16.16
	constexpr int64_t kShallow = 0x3244;  // tan 11°
	int turn;
	if (c > e) {
		const int64_t limit = int64_t(e) * 65536;
		turn = c * kSteep < limit ? 2 : c * kShallow < limit ? 1 : 0;
	} else {
		const int64_t limit = int64_t(c) * 65536;
		turn = e * kSteep < limit ? 2 : e * kShallow < limit ? 3 : 4;
	}
	ship.heading = uint8_t((base + turn) & 0xF);
}

}  // namespace

void update_world_date(GlobalMemory& g) {
	const uint32_t tick = g.u32(global::kWorldTick);
	g.u16(global::kWorldYear) = uint16_t(tick / (365 * kTicksPerDay));
	const uint16_t day = uint16_t((tick >> 8) % 365);
	g.u16(global::kWorldDayOfYear) = day;
	// El mes se busca desde el que había.
	uint8_t month = g.u8(global::kWorldMonth);
	if (day >= kMonthStart[month + 1]) {
		do {
			++month;
		} while (day >= kMonthStart[month + 1]);
	} else {
		while (day < kMonthStart[month]) {
			--month;
		}
	}
	g.u8(global::kWorldMonth) = month;
	g.u8(global::kWorldDay) = uint8_t(uint8_t(day) - uint8_t(kMonthStart[month]) + 1);
}

GameDate date_from_tick(uint32_t tick) {
	GameDate date;
	date.year = int(tick / (365 * kTicksPerDay));
	const int day = int((tick / kTicksPerDay) % 365);
	while (date.month < 11 && day >= kMonthStart[date.month + 1]) {
		++date.month;
	}
	date.day = day - kMonthStart[date.month] + 1;
	return date;
}

Sailing::Sailing(SaveGame& save, const NavData& nav, std::vector<SeaMapTown> map_towns)
	: save_(save), nav_(nav), map_towns_(std::move(map_towns)), matrix_(save.towns.size() * save.towns.size()) {}

int Sailing::town_at(MapPoint point) const {
	for (size_t i = 0; i < save_.towns.size(); ++i) {
		const SeaMapTown& t = map_towns_.at(size_t(save_.towns[i].town_id()));
		const bool on_symbol = t.symbol_x < point.x && point.x < t.symbol_x + 22 && t.symbol_y < point.y &&
			point.y < t.symbol_y + 22;
		if (on_symbol || (t.approach_x == point.x && t.approach_y == point.y)) {
			return int(i);
		}
	}
	return -1;
}

MapPoint Sailing::approach(int town) const {
	const SeaMapTown& t = map_towns_.at(size_t(save_.towns.at(size_t(town)).town_id()));
	return {int16_t(t.approach_x), int16_t(t.approach_y)};
}

std::shared_ptr<SeaRoute> Sailing::town_route(int from, int to) {
	std::shared_ptr<SeaRoute>& route = matrix_.at(size_t(from) * save_.towns.size() + size_t(to));
	if (!route) {
		route = std::make_shared<SeaRoute>(nav_.route(approach(from), approach(to)));
	}
	return route;
}

// Ruta para ir de `start` a `end` (0x445010 con dos puntos). Entre dos
// ciudades es la de la matriz, compartida, a la que el juego cambia el primer
// punto por el de salida; si no, una ruta nueva que es del barco.
Sailing::Assigned Sailing::route_for(MapPoint start, MapPoint end) {
	const int from = town_at(start);
	const int to = town_at(end);
	if (from >= 0 && to >= 0) {
		std::shared_ptr<SeaRoute> route = town_route(from, to);
		if (!route->empty()) {
			route->front() = start;
		}
		return {route, false};
	}
	std::vector<MapPoint> points = nav_.route(start, end);
	if (points.empty()) {
		// Sin agua cerca de un extremo, el juego no da ruta, pero cambia el
		// primer punto del elemento 6 · n de la matriz: la ruta de la ciudad 6 a la 0.
		if (save_.towns.size() > 6) {
			std::shared_ptr<SeaRoute> other = town_route(6, 0);
			if (!other->empty()) {
				other->front() = start;
			}
		}
		return {};
	}
	return {std::make_shared<SeaRoute>(std::move(points)), true};
}

void Sailing::assign_loaded_routes() {
	const size_t count = save_.towns.size();
	for (SavedShip& ship : save_.ships) {
		if (!ship.in_use() || !ship.town_route) {
			continue;
		}
		ship.town_route = false;
		if (ship.origin < count && ship.destination < count) {
			const Assigned assigned = route_for(approach(ship.origin), approach(ship.destination));
			ship.route = assigned.route;
			ship.own_route = assigned.own;
		}
	}
}

uint32_t Sailing::speed(const SavedShip& ship) {
	const int32_t load = 614 * ship.used / ship.capacity;
	const uint32_t base = uint32_t(0x1000 - load) * kBaseSpeed[ship.type & 3] >> 12;
	uint32_t condition = uint32_t(ship.health) * 130 / uint32_t(ship.max_health) + 0xA5;
	condition = condition < 0xCC ? 0xCC : condition > 0x100 ? 0x100 : condition;
	return condition * base >> 10;
}

// Ticks previstos para recorrer la ruta (0x5169C0), con una distancia
// aproximada de 16 por píxel en el eje mayor y 7 en el menor.
uint32_t Sailing::travel_time(const SavedShip& ship) const {
	if (!ship.route) {
		return 0;
	}
	uint32_t length = 0;
	const SeaRoute& route = *ship.route;
	for (size_t i = 1; i < route.size(); ++i) {
		const uint32_t dx = uint32_t(std::abs(route[i - 1].x - route[i].x));
		const uint32_t dy = uint32_t(std::abs(route[i - 1].y - route[i].y));
		length += dx > dy ? 16 * dx + 7 * dy : 7 * dx + 16 * dy;
	}
	const uint32_t v = speed(ship);
	return v ? length * 8 / v : 0;
}

bool Sailing::human(const SavedShip& ship) const {
	return merchant(ship) && save_.merchants.at(ship.owner).kind() == 0;
}

uint32_t Sailing::town_flags(int town) const {
	return town >= 0 && size_t(town) < save_.towns.size() ? save_.towns[size_t(town)].situation() : 0;
}

Sailing::SailCheck Sailing::sail_check(const SavedShip& ship) const {
	const uint16_t s = ship.status;
	if (ship.sailors < kMinSailors[ship.type & 3]) {
		return SailCheck::FewSailors;
	}
	if (int8_t(ship.readiness >> 8) <= 0) {
		return SailCheck::Tired;
	}
	if (ship.used > ship.capacity) {
		return SailCheck::Overloaded;
	}
	if (ship.captain >= save_.captain_count() && ship.max_health > 5 * ship.health) {
		return SailCheck::Damaged;
	}
	if (s == 7 || s == 9 || (s >= 12 && s <= 14) || s >= 17) {
		return SailCheck::Busy;
	}
	return town_flags(ship.town) & kClosedPort ? SailCheck::PortClosed : SailCheck::Ok;
}

void Sailing::advance_tick() {
	begin_tick();
	update_ships();
}

void Sailing::begin_tick() {
	events_.clear();
	++save_.tick();
	if (save_.tick() % kTicksPerDay == 0) {
		update_world_date(save_.globals);
	}
}

void Sailing::update_ships() {
	update_docked();
	update_sailing();
}

// En puerto la tripulación descansa: pasado un día desde que atracó, sube un
// punto por tick hasta 0x700.
void Sailing::rest(SavedShip& ship) const {
	if (ship.moved_tick + kTicksPerDay < save_.tick()) {
		++ship.readiness;
		if (int16_t(ship.readiness) >= kMaxReadiness) {
			ship.readiness = kMaxReadiness;
		}
	}
}

// Lista de barcos en puerto (primera mitad de 0x506720).
void Sailing::update_docked() {
	uint16_t* link = &save_.docked_head();
	while (*link < save_.ships.size()) {
		const int index = *link;
		SavedShip& ship = save_.ships[size_t(index)];
		switch (ship.status) {
		case kShipDocked:
			// Los barcos de la IA deciden aquí su próximo viaje (0x518B80): no se reproduce.
			if (human(ship)) {
				rest(ship);
				// Bit 0 de las órdenes: ruta automática (0x518860), que tampoco se reproduce.
				if (!(ship.orders & 1) && ship.town != ship.origin) {
					ship.origin = ship.town;
				}
			}
			break;
		case kShipWaiting:
			rest(ship);
			if (ship.timer > 0) {
				--ship.timer;
			} else {
				ship.status = kShipDocked;
			}
			break;
		case kShipLeaving:
			if (ship.timer >= kHarbourTicks) {
				// Fuera del puerto: pasa a la lista de los que navegan (0x5090E0).
				start_voyage(ship);
				*link = ship.next_in_list;
				ship.next_in_list = save_.sailing_head();
				save_.sailing_head() = uint16_t(index);
				continue;
			}
			if (ship.timer++ == 0) {
				if (town_flags(ship.town) & kClosedPort) {
					ship.status = kShipWaiting;
					ship.timer = kBlockadeWait;
					clear_route(ship);
				} else {
					ship.speed = int32_t(speed(ship) >> 1);
				}
			}
			break;
		case kShipArriving:
			if (ship.timer >= kHarbourTicks) {
				dock(ship, index);
			} else if (ship.timer++ == 0) {
				ship.speed = int32_t(speed(ship) >> 1);
				ship.raid = false;
			}
			break;
		}
		link = &ship.next_in_list;
	}
}

// Lista de barcos en el mar (segunda mitad de 0x506720).
void Sailing::update_sailing() {
	const bool every_32 = save_.tick() % 32 == 0;
	uint16_t* link = &save_.sailing_head();
	while (*link < save_.ships.size()) {
		const int index = *link;
		SavedShip& ship = save_.ships[size_t(index)];
		if (ship.status != kShipSailing) {
			link = &ship.next_in_list;
			continue;
		}
		// Navegando, la tripulación se cansa: un punto cada 32 ticks.
		if (every_32) {
			--ship.readiness;
			if ((ship.readiness & 0xFF) == 0xFF && int8_t(ship.readiness >> 8) < -1) {
				ship.readiness |= 0xFF00;
			}
		}
		if (sail(ship, index)) {
			// Persecución de otro barco (0x50B030): solo se reproduce que la
			// olvida si el barco no existe.
			if (ship.target != kNoIndex && ship.target >= save_.ships.size()) {
				ship.target = kNoIndex;
			}
			link = &ship.next_in_list;
		} else if (ship.health <= 0) {
			sink(ship, index, *link);
		} else {
			bool again = false;
			arrive(ship, index, *link, again);
			if (!again && *link == index) {
				link = &ship.next_in_list;
			}
		}
	}
}

// Un tick de un barco que navega (0x516CC0). Devuelve false si ha llegado a
// una ciudad (o se ha hundido); el llamador lo hace entrar en el puerto.
bool Sailing::sail(SavedShip& ship, int index) {
	if (merchant(ship) && save_.tick() % 4 == 0) {
		--ship.health;  // El casco se desgasta en el mar.
	}
	if (ship.health <= 0) {
		return false;
	}
	if (!ship.route) {
		// Los barcos de la IA sin ruta piden una cada 64 ticks (0x54AA70): no se reproduce.
		if (merchant(ship) && !human(ship)) {
			ship.raid = false;
		}
		return true;
	}
	ship.x += ship.dx;
	ship.y += ship.dy;
	const SeaRoute& route = *ship.route;
	const int16_t x = whole(ship.x);
	const int16_t y = whole(ship.y);
	if (ship.route_index >= route.size()) {
		return true;
	}
	if (ship.route_mark > 0 && ship.route_mark < route.size()) {
		const MapPoint mark = route[ship.route_mark];
		if (std::abs(mark.x - x) > 0x48 || std::abs(mark.y - y) > 0x48) {
			ship.route_mark = 0;
		}
	}
	if (route[ship.route_index] != MapPoint{x, y}) {
		return true;
	}

	// En el punto de la ruta: hacia el siguiente, desde el centro del píxel.
	if (++ship.route_index < route.size()) {
		ship.x = with_fraction(ship.x, kCentre);
		ship.y = with_fraction(ship.y, kCentre);
		const uint32_t v = speed(ship);
		set_course(ship, v, route[ship.route_index]);
		ship.speed = int32_t(v) * 64 / 115 + 4;
		return true;
	}

	// Fin de la ruta.
	ship.dx = ship.dy = ship.speed = 0;
	const int town = town_at({x, y});
	if (town >= 0) {
		if (ship.raid) {
			if (route.size() > 1) {
				ship.status = 0x14;
				ship.town = uint8_t(town);
				return false;
			}
			ship.town = ship.destination = kNoTown;
			return true;
		}
		const MapPoint here = approach(town);
		ship.x = int32_t(uint32_t(uint16_t(here.x)) << 16);
		ship.y = int32_t(uint32_t(uint16_t(here.y)) << 16);
		ship.town = uint8_t(town);
		return false;
	}
	// Se queda en el mar sin ruta; a diferencia de 0x516840, el índice de la ruta no vuelve a 0.
	ship.route.reset();
	ship.own_route = false;
	ship.arrival_tick = 0;
	events_.push_back({ShipEvent::Stopped, index, -1});
	return true;
}

// Ha llegado a la ciudad ship.town (0x506D8E): entra en el puerto, salvo que esté cerrado.
void Sailing::arrive(SavedShip& ship, int index, uint16_t& link, bool& again) {
	const uint32_t flags = town_flags(ship.town);
	clear_route(ship);
	if ((flags & kClosedPort) || ((flags & kClosedToRoutes) && merchant(ship) && (ship.orders & 1))) {
		if (ship.raid && !(flags & kRaidBlocked)) {
			// Piratas ante un puerto bloqueado: no se reproduce.
			ship.town = kNoTown;
			return;
		}
		// Se queda fuera sin ruta, y el juego vuelve a pasar por él en este mismo tick.
		ship.origin = ship.town;
		ship.town = kNoTown;
		ship.status = kShipSailing;
		again = true;
		return;
	}
	link = ship.next_in_list;
	ship.next_in_list = kNoIndex;
	if (ship.raid && ship.status == 0x14) {
		return;  // Ataque pirata a la ciudad: no se reproduce.
	}
	ship.status = kShipArriving;
	ship.timer = 0;
	ship.next_in_list = save_.docked_head();
	save_.docked_head() = uint16_t(index);
}

// Casco a cero (0x506CD6).
void Sailing::sink(SavedShip& ship, int index, uint16_t& link) {
	clear_route(ship);
	link = ship.next_in_list;
	ship.next_in_list = kNoIndex;
	ship.status = kShipSunk;
	ship.health = 0;
	events_.push_back({ShipEvent::Sunk, index, -1});
}

// Termina de entrar en el puerto (0x519C90).
void Sailing::dock(SavedShip& ship, int index) {
	ship.status = kShipDocked;
	ship.speed = 0;
	ship.moved_tick = save_.tick();
	if (ship.destination < save_.towns.size()) {
		SavedTown& town = save_.towns[ship.destination];
		for (int w = 0; w < kWareCount; ++w) {
			town.in_transit(size_t(w)) -= ship.cargo[size_t(w)];
			town.incoming(size_t(w)) -= ship.cargo[size_t(w)];
			if (town.in_transit(size_t(w)) < 0) {
				town.in_transit(size_t(w)) = 0;
			}
		}
	}
	ship.timer = 0;
	events_.push_back({ShipEvent::Arrived, index, ship.town});
}

// Sale del puerto al mar (0x5090E0).
void Sailing::start_voyage(SavedShip& ship) {
	ship.moved_tick = save_.tick();
	recalculate_cargo(ship);
	leave_port(ship);
	ship.status = kShipSailing;
	ship.timer = 0;
	ship.town = kNoTown;
	int32_t total = 0;
	if (ship.destination < save_.towns.size()) {
		SavedTown& town = save_.towns[ship.destination];
		for (int w = 0; w < kWareCount; ++w) {
			const int32_t cargo = ship.cargo[size_t(w)];
			if (cargo > 0) {
				town.in_transit(size_t(w)) += cargo;
				town.incoming(size_t(w)) += cargo;
				total += cargo;
			}
		}
	}
	if (merchant(ship)) {
		// Estadística de la bodega llena al zarpar; la del comerciante
		// ([+0x540] y [+0x544]) no se reproduce.
		ship.load_factor_sum += float(total) / float(ship.capacity);
		++ship.voyages;
	} else {
		ship.status = kShipPirate;
	}
}

// Coloca el barco en el punto de llegada de su ciudad para empezar la ruta
// (0x516BC0) y, si aún no tiene, le da una hacia su destino.
void Sailing::leave_port(SavedShip& ship) {
	ship.dx = ship.dy = 0;
	ship.origin = ship.town;
	if (ship.town < save_.towns.size()) {
		const MapPoint here = approach(ship.town);
		ship.x = int32_t(uint32_t(uint16_t(here.x)) << 16);
		ship.y = int32_t(uint32_t(uint16_t(here.y)) << 16);
		if (!ship.route && ship.destination < save_.towns.size()) {
			const Assigned assigned = route_for(here, approach(ship.destination));
			ship.route = assigned.route;
			ship.own_route = assigned.own;
			ship.arrival_tick = save_.tick() + travel_time(ship);
		}
	}
	ship.route_index = 0;
	ship.route_mark = 0;
	ship.heading = ship.heading < 8 ? uint8_t((ship.heading - 4) & 7) : 2;
}

// Nueva ruta desde donde está el barco (0x5172A0).
void Sailing::set_destination(SavedShip& ship, int town, int x, int y) {
	const MapPoint start = {whole(ship.x), whole(ship.y)};
	MapPoint end;
	if (town >= 0 && size_t(town) < save_.towns.size()) {
		end = approach(town);
		ship.destination = uint8_t(town);
	} else {
		end = {int16_t(x), int16_t(y)};
		ship.destination = kNoTown;
	}
	ship.x = with_fraction(ship.x, kCentre);
	ship.y = with_fraction(ship.y, kCentre);
	ship.dx = ship.dy = 0;
	clear_route(ship);
	const Assigned assigned = route_for(start, end);
	ship.route = assigned.route;
	ship.own_route = assigned.own;
	ship.arrival_tick = save_.tick() + travel_time(ship);
}

bool Sailing::set_sail(int index, int town, int x, int y) {
	if (index < 0 || size_t(index) >= save_.ships.size()) {
		return false;
	}
	SavedShip& ship = save_.ships[size_t(index)];
	ship.target = kNoIndex;
	// En el astillero (6) o en el estado 8, y en convoy, el juego hace otras cosas.
	if (ship.status == 6 || ship.status == 8 || ship.convoy < save_.convoy_count()) {
		return false;
	}
	ship.orders &= 0xFC;
	const uint16_t status = ship.status;
	if (status >= 4 && status != kShipSailing && status != 0x10) {
		return false;
	}
	if (status < 4 && !can_sail(ship)) {
		return false;
	}
	const bool to_town = town >= 0 && size_t(town) < save_.towns.size();
	auto at_origin = [&] {
		const MapPoint p = ship.origin < save_.towns.size() ? approach(ship.origin) : MapPoint{-1, -1};
		return p.x == x && p.y == y;
	};
	if (status == kShipLeaving) {
		if (to_town ? town == ship.origin : at_origin()) {
			// Vuelve al puerto del que está saliendo.
			ship.status = kShipArriving;
			ship.timer = uint16_t(kHarbourTicks - ship.timer);
			ship.destination = ship.origin;
			ship.arrival_tick = save_.tick() - ship.timer + kHarbourTicks;
			clear_route(ship);
			return true;
		}
	} else if (status == kShipArriving) {
		if (to_town ? town == ship.destination : at_origin()) {
			return false;
		}
		// Da la vuelta mientras entraba.
		ship.status = kShipLeaving;
		ship.timer = uint16_t(kHarbourTicks - ship.timer);
		ship.origin = ship.town;
	} else if (status < 4) {
		if (to_town && town == ship.town) {
			return false;
		}
		ship.origin = ship.town;
		ship.timer = 0;
		ship.status = kShipLeaving;
	}
	set_destination(ship, to_town ? town : kNoTown, x, y);
	ship.speed = 1;
	return true;
}

}  // namespace p3::data
