#include "p3data/sailing.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "p3data/mail.hpp"
#include "p3data/town_tick/internal.hpp"
#include "p3data/trade.hpp"

namespace p3::data::tick::events_rare_detail {
void fn_5098b0(SaveGame& save, uint16_t& captain);
}  // namespace p3::data::tick::events_rare_detail

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

// Avisos a la interfaz ([+0x3C], u16): el juego los pone con `or` y la
// interfaz los recoge.
constexpr uint16_t kNoticeRoute = 0x0008;  // Cambia el viaje: zarpa, llega a una ciudad, se queda en el mar...
constexpr uint16_t kNoticeStatus = 0x0020;  // Cambio de estado: zarpa, atraca, deja de esperar...
constexpr uint16_t kNoticeSpeed = 0x0080;  // Cambia la velocidad que se muestra ([+0x30]).
constexpr uint16_t kNoticeReadiness = 0x0200;  // Cambia el byte alto del descanso [+0x3E].
constexpr uint16_t kNoticeOrder = 0x0800;  // Nueva orden de zarpar (0x517770).
constexpr uint16_t kNoticeEntering = 0x1000;  // Empieza a entrar en el puerto.
constexpr uint16_t kNoticeMoved = 0x2000;  // Cambia la parte entera de la posición.

// Contadores u16 del objeto de los barcos (0x6DD7A0) que 0x506720 pone a 0
// al empezar y cuenta en cada tick: barcos del jugador en la lista de puerto
// (estados 0 a 3) y en el mar (0x0F), y convoyes del jugador en puerto y en
// el mar (los cuenta el recorrido de los convoyes, que no se reproduce).
constexpr uint32_t kPlayerShipsInPort = global::kShips + 0x100;
constexpr uint32_t kPlayerShipsAtSea = global::kShips + 0x102;
constexpr uint32_t kPlayerConvoysInPort = global::kShips + 0x104;
constexpr uint32_t kPlayerConvoysAtSea = global::kShips + 0x106;

// Lista doblemente enlazada de los barcos en el mar ordenada por la parte
// entera de x (0x50D0C0), para los encuentros: [+0xA] el anterior (x menor)
// y [+0xC] el siguiente. No tiene cabeza: se entra por cualquier barco.
uint16_t& x_prev(SavedShip& ship) {
	return ship.memory.u16(0xA);
}
uint16_t& x_next(SavedShip& ship) {
	return ship.memory.u16(0xC);
}
int32_t whole_x(const SavedShip& ship) {
	return int16_t(ship.memory.u16(0x1E));  // movsx
}

}  // namespace

void clear_ship_route(SavedShip& ship) {
	if (ship.route) {
		ship.set_route(nullptr);
		ship.own_route() = 0;
	}
	ship.route_index() = 0;
	ship.route_mark() = 0;
}

namespace {

void clear_route(SavedShip& ship) {
	clear_ship_route(ship);
}

}  // namespace

// Rumbo y desplazamiento por tick hacia `target` (0x517410): tantos ticks
// como 128 · distancia / velocidad, más uno, y el rumbo de 16 pasos según
// el cuadrante y la pendiente del desplazamiento.
void set_ship_course(SavedShip& ship, uint32_t speed, MapPoint target) {
	const int32_t dx = target.x - whole(ship.x());
	const int32_t dy = target.y - whole(ship.y());
	const int32_t steps = 1 - ftol(std::sqrt(double(dy * dy + dx * dx)) * -128.0 / double(int32_t(speed)));
	auto step = [steps](int32_t d) { return d == 0 ? 0 : (d * 65536 + (d > 0 ? 0x8000 : -0x7FFF)) / steps; };
	ship.dx() = step(dx);
	ship.dy() = step(dy);

	// Componentes del desplazamiento según el cuadrante: c, la que se compara
	// con e para ver cuánto se gira desde el rumbo de base.
	int base, c, e;
	if (ship.dx() > 0) {
		base = ship.dy() > 0 ? 9 : 5;
		c = ship.dy() > 0 ? ship.dx() : -ship.dy();
		e = ship.dy() > 0 ? ship.dy() : ship.dx();
	} else {
		base = ship.dy() > 0 ? 13 : 1;
		c = ship.dy() > 0 ? ship.dy() : -ship.dx();
		e = ship.dy() > 0 ? -ship.dx() : -ship.dy();
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
	ship.heading() = uint8_t((base + turn) & 0xF);
}

namespace {

void set_course(SavedShip& ship, uint32_t speed, MapPoint target) {
	set_ship_course(ship, speed, target);
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
	if (matrix_.size() != save_.towns.size() * save_.towns.size()) {
		// Creado antes de leer la partida (tools/oracle/tick_probe).
		matrix_.assign(save_.towns.size() * save_.towns.size(), nullptr);
	}
	std::shared_ptr<SeaRoute>& route = matrix_.at(size_t(from) * save_.towns.size() + size_t(to));
	if (!route) {
		route = std::make_shared<SeaRoute>(nav_.route(approach(from), approach(to)));
	}
	return route;
}

void Sailing::add_town() {
	const size_t n = save_.towns.size();
	const size_t old = n - 1;
	std::vector<std::shared_ptr<SeaRoute>> grown(n * n);
	if (matrix_.size() == old * old) {
		for (size_t r = 0; r < old; ++r) {
			for (size_t c = 0; c < old; ++c) {
				grown[r * n + c] = matrix_[r * old + c];
			}
		}
	}
	matrix_ = std::move(grown);
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
		if (ship.origin() < count && ship.destination() < count) {
			const Assigned assigned = route_for(approach(ship.origin()), approach(ship.destination()));
			ship.set_route(assigned.route);
			ship.own_route() = assigned.own;
		}
	}
	load_battles();  // 0x611AE0, in 0x549F40 after the events (battle/battle_save.cpp)
	check_loaded_routes();  // 0x5048C0, at the end of 0x549F40 (sailing_load.cpp)
}

uint32_t Sailing::speed(const SavedShip& ship) {
	const int32_t load = 614 * ship.used() / ship.capacity();
	const uint32_t base = uint32_t(0x1000 - load) * kBaseSpeed[ship.type() & 3] >> 12;
	uint32_t condition = uint32_t(ship.health()) * 130 / uint32_t(ship.max_health()) + 0xA5;
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
	return merchant(ship) && save_.merchants.at(ship.owner()).kind() == 0;
}

bool Sailing::player(const SavedShip& ship) const {
	return ship.owner() == save_.globals.u32(global::kPlayer);
}

// 0x50D040: saca el barco de la lista ordenada por x; sus vecinos solo se
// enlazan entre sí si le apuntaban a él.
void Sailing::unlink_x(uint16_t index) {
	const size_t count = save_.ships.size();
	SavedShip& ship = save_.ships[index];
	if (x_prev(ship) < count) {
		uint16_t& next = x_next(save_.ships[x_prev(ship)]);
		if (next == index) {
			next = x_next(ship);
		}
	}
	if (x_next(ship) < count) {
		uint16_t& prev = x_prev(save_.ships[x_next(ship)]);
		if (prev == index) {
			prev = x_prev(ship);
		}
	}
	x_prev(ship) = kNoIndex;
	x_next(ship) = kNoIndex;
}

// 0x50D0C0: recoloca el barco en la lista ordenada por x tras moverse. Lo
// pasa delante de los anteriores con x mayor o igual y, si no ha retrocedido
// ninguno, detrás de los siguientes con x menor. Si un vecino no le apunta a
// él, deja ese enlace a 0xFFFF y para.
void Sailing::sort_x(uint16_t index) {
	const size_t count = save_.ships.size();
	SavedShip& ship = save_.ships[index];
	const int32_t x = whole_x(ship);
	bool forward = true;
	while (x_prev(ship) < count) {
		SavedShip& prev = save_.ships[x_prev(ship)];
		if (x_next(prev) != index) {
			x_prev(ship) = kNoIndex;
			break;
		}
		if (whole_x(prev) < x) {
			break;
		}
		forward = false;
		x_next(prev) = x_next(ship);
		if (x_next(ship) < count) {
			x_prev(save_.ships[x_next(ship)]) = x_prev(ship);
		}
		x_next(ship) = x_prev(ship);
		x_prev(ship) = x_prev(save_.ships[x_next(ship)]);
		x_prev(save_.ships[x_next(ship)]) = index;
		if (x_prev(ship) < count) {
			x_next(save_.ships[x_prev(ship)]) = index;
		}
	}
	if (!forward) {
		return;
	}
	while (x_next(ship) < count) {
		SavedShip& next = save_.ships[x_next(ship)];
		if (x_prev(next) != index) {
			x_next(ship) = kNoIndex;
			return;
		}
		if (whole_x(next) >= x) {
			return;
		}
		x_prev(next) = x_prev(ship);
		if (x_prev(ship) < count) {
			x_next(save_.ships[x_prev(ship)]) = x_next(ship);
		}
		x_prev(ship) = x_next(ship);
		x_next(ship) = x_next(save_.ships[x_prev(ship)]);
		if (x_next(ship) < count) {
			x_prev(save_.ships[x_next(ship)]) = index;
		}
		x_next(save_.ships[x_prev(ship)]) = index;
	}
}

// 0x50CE50: mete el barco en la lista ordenada por x detrás de `after` (o,
// si no es un barco, del primero de la lista del mar; si no hay ninguno, no
// hace nada) y lo recoloca. Si ya estaba bien enlazado, solo lo recoloca.
void Sailing::insert_x(uint16_t index, uint32_t after) {
	const size_t count = save_.ships.size();
	if (after >= count) {
		after = save_.sailing_head();
		if (after >= count) {
			return;
		}
	}
	SavedShip& ship = save_.ships[index];
	SavedShip& other = save_.ships[after];
	if (x_prev(ship) < count) {
		const bool from_prev = x_next(save_.ships[x_prev(ship)]) == index;
		const bool from_next = x_next(ship) >= count || x_prev(save_.ships[x_next(ship)]) == index;
		if (from_prev && from_next) {
			sort_x(index);
			return;
		}
		// Los enlaces que le apuntan se cortan (los vecinos no se unen entre sí).
		uint16_t& next = x_next(save_.ships[x_prev(ship)]);
		if (next == index) {
			next = kNoIndex;
		}
		x_prev(ship) = kNoIndex;
		if (x_next(ship) < count) {
			uint16_t& prev = x_prev(save_.ships[x_next(ship)]);
			if (prev == index) {
				prev = kNoIndex;
			}
			x_next(ship) = kNoIndex;
		}
	}
	x_prev(ship) = uint16_t(after);
	x_next(ship) = x_next(other);
	x_next(other) = index;
	if (x_next(ship) < count) {
		x_prev(save_.ships[x_next(ship)]) = index;
	}
	sort_x(index);
}

uint32_t Sailing::town_flags(int town) const {
	return town >= 0 && size_t(town) < save_.towns.size() ? save_.towns[size_t(town)].situation() : 0;
}

Sailing::SailCheck Sailing::sail_check(const SavedShip& ship) const {
	const uint16_t s = ship.status();
	if (ship.sailors() < kMinSailors[ship.type() & 3]) {
		return SailCheck::FewSailors;
	}
	if (int8_t(ship.readiness() >> 8) <= 0) {
		return SailCheck::Tired;
	}
	if (ship.used() > ship.capacity()) {
		return SailCheck::Overloaded;
	}
	if (ship.captain() < save_.captain_count() && ship.max_health() > 5 * ship.health()) {
		return SailCheck::Damaged;
	}
	if (s == 7 || s == 9 || (s >= 12 && s <= 14) || s >= 17) {
		return SailCheck::Busy;
	}
	return town_flags(ship.town()) & kClosedPort ? SailCheck::PortClosed : SailCheck::Ok;
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

// 0x506720 (thiscall sobre el objeto de los barcos, con el tick): pone a 0
// los contadores, recorre la lista de puerto y la del mar y después los
// convoyes. Cuando el tick acaba en 0x6F, antes de las listas, llama a
// 0x514000 con cada uno de los cinco objetos de [+0xC] del objeto de los
// barcos que existan; y al final, cada 8 ticks, a 0x5053C0 (los encuentros).
// 0x514000 runs with set_pirates(true) (battle/pirate_objects.cpp), as the
// oracle with --pirates.
// 0x5053C0 (the storms, battle/storms.cpp) runs unless set_storms(false), as
// the oracle does unless --no-storms; without the convoys, the oracle skips
// it too.
void Sailing::update_ships() {
	GlobalMemory& g = save_.globals;
	g.u16(kPlayerShipsInPort) = 0;
	g.u16(kPlayerShipsAtSea) = 0;
	g.u16(kPlayerConvoysInPort) = 0;
	g.u16(kPlayerConvoysAtSea) = 0;
	if (pirates_ && (save_.tick() & 0xFF) == 0x6F) {
		// 0x514000 for each pirate object (battle/pirate_objects.cpp).
		for (size_t k = 0; k < save_.pirates.size(); ++k) {
			if (g.u32(global::kShips + 0xC + 4 * uint32_t(k)) != 0 && save_.pirates[k]) {
				fn_514000(k);
			}
		}
	}
	update_docked();
	update_sailing();
	if (convoys_) {
		update_convoys();
		if (storms_ && (save_.tick() & 7) == 0) {
			fn_5053c0();
		}
	}
}

// Descanso en puerto (0x5067E5, igual en el estado 1): pasado un día desde
// que atracó, sube un punto por tick hasta 0x700.
void Sailing::rest(SavedShip& ship) const {
	if (ship.moved_tick() + kTicksPerDay < save_.tick()) {
		++ship.readiness();
		if (int16_t(ship.readiness()) >= kMaxReadiness) {
			ship.readiness() = kMaxReadiness;
		} else if ((ship.readiness() & 0xFF) == 0) {
			ship.notices() |= kNoticeReadiness;
		}
	}
}

// Lista de barcos en puerto (primera mitad de 0x506720, tabla 0x507CB0). Se
// reproducen los estados 1, 2, 3 y el 0 de los jugadores humanos; el 0 de la
// IA (0x5067D0 → 0x518B80), el 4 (0x506A19) y el 5 (0x506B6A) los traduce
// town_tick/ship_ai.cpp. Los estados 6 a 0x11 tienen otros manejadores (el
// 0x11, 0x506AF2; los que no tienen, 0x507FB0, que borra el barco) que no
// se reproducen.
void Sailing::update_docked() {
	uint16_t* link = &save_.docked_head();
	while (*link < save_.ships.size()) {
		const uint16_t index = *link;
		SavedShip& ship = save_.ships[index];
		switch (ship.status()) {
		case kShipDocked:
			// Estado 0 (0x5067D0): descansa y, si es de la IA, decide qué hacer
			// (0x518B80, src/p3data/town_tick/ship_ai.cpp).
			if (!ai_ && !human(ship)) {
				break;
			}
			if (!tick::fn_5067d0(save_, index, link)) {
				continue;
			}
			break;
		case kShipToShipyard:
			// Estado 4 (0x506A19): pasa a la lista de reparación de su ciudad.
			if (!ai_) {
				break;
			}
			if (!tick::fn_506a19(save_, index, link)) {
				continue;
			}
			break;
		case kShipFromShipyard:
			// Estado 5 (0x506B6A): vuelve del astillero.
			if (!ai_) {
				break;
			}
			if (!tick::fn_506b6a(save_, index, link)) {
				continue;
			}
			break;
		case kShipWaiting:
			waiting(ship);
			break;
		case kShipLeaving:
			if (leaving(ship, index, *link)) {
				continue;  // Ha pasado a la lista del mar: *link ya es el siguiente.
			}
			break;
		case kShipArriving:
			arriving(ship, index);
			break;
		case kShipSunk:
			// State 0x11 (0x506AF2), with the pirates (battle/pirates.cpp).
			if (pirates_) {
				lost_in_port(index, *link);
				continue;
			}
			break;
		}
		link = &ship.next_in_list();
	}
}

// Estado 1, esperando (0x5068CE).
void Sailing::waiting(SavedShip& ship) {
	if (player(ship)) {
		++save_.globals.u16(kPlayerShipsInPort);
	}
	rest(ship);
	if (ship.timer() > 0) {
		--ship.timer();
	} else {
		ship.notices() |= kNoticeStatus;
		ship.status() = kShipDocked;
	}
}

// Estado 2, saliendo del puerto (0x506945). Devuelve true si el barco ha
// salido al mar: entonces deja `link` en el siguiente de la lista de puerto.
bool Sailing::leaving(SavedShip& ship, uint16_t index, uint16_t& link) {
	if (player(ship)) {
		++save_.globals.u16(kPlayerShipsInPort);
	}
	if (ship.timer() >= kHarbourTicks) {
		// Fuera del puerto (0x5090E0): pasa a la lista de los que navegan.
		start_voyage(ship, index);
		link = ship.next_in_list();
		ship.next_in_list() = save_.sailing_head();
		save_.sailing_head() = index;
		return true;
	}
	if (ship.timer()++ == 0) {
		if (town_flags(ship.town()) & kClosedPort) {
			ship.notices() |= kNoticeStatus;
			ship.status() = kShipWaiting;
			ship.timer() = kBlockadeWait;
			clear_route(ship);
		} else {
			// Antes, la escena del puerto (0x6135C0).
			ship.notices() |= kNoticeSpeed;
			ship.speed() = int32_t(speed(ship) >> 1);
		}
	}
	return false;
}

// Estado 3, entrando en el puerto (0x506A86).
void Sailing::arriving(SavedShip& ship, uint16_t index) {
	if (player(ship)) {
		++save_.globals.u16(kPlayerShipsInPort);
	}
	if (ship.timer() >= kHarbourTicks) {
		dock(ship, index);
	} else if (ship.timer()++ == 0) {
		ship.notices() |= kNoticeEntering;
		ship.raid() = false;
		ship.speed() = int32_t(speed(ship) >> 1);
		// Después, la escena del puerto (0x6132C0).
	}
}

// Lista de barcos en el mar (segunda mitad de 0x506720, tabla 0x507CF0).
// Solo se reproduce el estado 0x0F; los demás (0x0A, 0x0B y 0x16: hundido
// por 0x5070CF; 0x11, 0x12 y los convoyes) no.
void Sailing::update_sailing() {
	const bool every_32 = (save_.tick() & 0x1F) == 0;
	uint16_t* link = &save_.sailing_head();
	while (*link < save_.ships.size()) {
		const uint16_t index = *link;
		SavedShip& ship = save_.ships[index];
		if (ship.status() != kShipSailing) {
			// States 0x11 and 0x12 (0x507022, 0x507099), with the pirates
			// (battle/pirates.cpp): the ship leaves the list (*link is the next).
			if (pirates_ && (ship.status() == kShipSunk || ship.status() == kShipPirate)) {
				if (ship.status() == kShipSunk) {
					lost_at_sea(index, *link);
				} else {
					pirate_at_sea(index, *link);
				}
				continue;
			}
			link = &ship.next_in_list();
			continue;
		}
		if (player(ship)) {
			++save_.globals.u16(kPlayerShipsAtSea);
		}
		// Navegando, la tripulación se cansa: un punto cada 32 ticks.
		if (every_32) {
			--ship.readiness();
			if ((ship.readiness() & 0xFF) == 0xFF) {
				ship.notices() |= kNoticeReadiness;
				if (int8_t(ship.readiness() >> 8) < -1) {
					ship.readiness() |= 0xFF00;
				}
			}
		}
		if (sail(ship, index)) {
			// Persecución de otro barco (0x50B030): si devuelve true, el barco
			// ha salido de la lista y *link ya es el siguiente.
			if (ship.target() != kNoIndex && pursue(ship)) {
				continue;
			}
			sort_x(index);
			link = &ship.next_in_list();
		} else if (ship.health() <= 0) {
			sink(ship, index, *link);
		} else {
			// Tanto si entra en el puerto (sale de la lista) como si se queda
			// fuera (el juego vuelve a pasar por él en este mismo tick), el
			// recorrido sigue por *link.
			arrive(ship, index, *link);
		}
	}
}

// 0x50B030: un barco que navega persigue a [+0x3A]. Devuelve true si ha
// salido de la lista del mar (al empezar una batalla, que no se reproduce).
bool Sailing::pursue(SavedShip& ship) {
	const size_t count = save_.ships.size();
	const uint16_t target = ship.target();
	if (target >= count) {
		ship.target() = kNoIndex;
		return false;
	}
	// Sin comprobar con el oráculo (ningún barco persigue a otro en las 12
	// partidas): de aquí en adelante.
	const uint16_t status = ship.status();
	if (status != 0x16 && status != kShipSailing && status != 0x10) {
		ship.target() = kNoIndex;
		return false;
	}
	SavedShip& other = save_.ships[target];
	const uint16_t other_status = other.status();
	if (ship.raid()) {
		if (other.town() < save_.towns.size()) {
			ship.target() = kNoIndex;
			clear_route(ship);
			return false;
		}
		if (other_status != kShipSailing && other_status != 0x10 && other_status != kShipPirate) {
			ship.target() = kNoIndex;
			return false;
		}
	} else if (other_status != kShipPirate && (other_status != kShipSailing || !other.raid())) {
		ship.target() = kNoIndex;
		return false;
	}
	// Un pirata (0x12) que no está en la lista ordenada por x no se persigue.
	if (other_status == kShipPirate && x_prev(other) >= count && x_next(other) >= count) {
		ship.target() = kNoIndex;
		return false;
	}
	const int32_t dx = int16_t(other.memory.u16(0x1E)) - int16_t(ship.memory.u16(0x1E));
	const int32_t dy = int16_t(other.memory.u16(0x22)) - int16_t(ship.memory.u16(0x22));
	const int32_t distance = dy * dy + dx * dx;
	if (distance > int32_t(save_.globals.u32(global::kShips + 0xFC) & 0xFFFF)) {
		if (status != 0x16) {
			ship.target() = kNoIndex;
		}
		return false;
	}
	if (distance < 0x190) {
		// Lo alcanza: batalla (0x50B250, que saca los barcos de la lista y
		// devuelve true). No se reproduce: el barco sigue en la lista.
		ship.target() = kNoIndex;
		return false;
	}
	if (ship.type() > 1) {
		// Las kogges y las holks dejan la persecución en los bajos de la
		// tabla 0x673564 (x0, y0, x1, y1).
		constexpr int16_t kShallows[3][4] = {{442, 1521, 658, 1691}, {2265, 147, 2332, 453}, {1497, 1263, 1578, 1444}};
		const int16_t x = int16_t(ship.memory.u16(0x1E));
		const int16_t y = int16_t(ship.memory.u16(0x22));
		for (const auto& r : kShallows) {
			if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]) {
				ship.target() = kNoIndex;
				clear_route(ship);
				return false;
			}
		}
	}
	if ((save_.tick() & 7) == 0) {
		// Rumbo hacia donde estará el otro barco dentro de 4 ticks (0x5172A0).
		const int32_t x = int32_t((uint32_t(other.x()) + uint32_t(other.dx()) * 4) >> 16);
		const int32_t y = int32_t((uint32_t(other.y()) + uint32_t(other.dy()) * 4) >> 16);
		set_destination(ship, kNoTown, x, y);
	}
	return false;
}

// Un tick de un barco que navega (0x516CC0). Devuelve false si ha llegado a
// una ciudad (o se ha hundido); el llamador lo hace entrar en el puerto.
bool Sailing::sail(SavedShip& ship, uint16_t index) {
	const uint16_t old_x = ship.memory.u16(0x1E);
	const uint16_t old_y = ship.memory.u16(0x22);
	if (merchant(ship) && (save_.tick() & 3) == 0) {
		--ship.health();  // El casco se desgasta en el mar.
	}
	if (ship.health() <= 0) {
		return false;
	}
	if (!ship.route) {
		// Un barco de la IA sin ruta pide cada 64 ticks una orden de zarpar
		// (operación 0x00, 0x54AA70) hacia su destino o, si iba a un punto
		// del mar, hacia su ciudad natal (0x516D3B).
		if (merchant(ship) && !human(ship)) {
			ship.raid() = false;
			if (((uint32_t(index) ^ save_.tick()) & 0x3F) == 0 && save_.globals.u32(0x6DF31C) == 0) {
				const int town = ship.destination() < save_.towns.size() ? ship.destination()
																		 : save_.merchants[ship.owner()].hometown();
				const MapPoint p = approach(town);
				std::vector<uint8_t> op(0x14);
				const int32_t fields[5] = {0, index, town, p.x, p.y};
				std::memcpy(op.data(), fields, sizeof(fields));
				save_.queued_operations.push_back(std::move(op));
			}
		}
		return true;
	}
	ship.x() = int32_t(uint32_t(ship.x()) + uint32_t(ship.dx()));
	ship.y() = int32_t(uint32_t(ship.y()) + uint32_t(ship.dy()));
	const int16_t x = whole(ship.x());
	const int16_t y = whole(ship.y());
	// El juego compara la parte entera de antes sin signo con la de ahora con signo.
	if (uint32_t(old_x) != uint32_t(int32_t(x)) || uint32_t(old_y) != uint32_t(int32_t(y))) {
		ship.notices() |= kNoticeMoved;
	}
	const SeaRoute& route = *ship.route;
	if (ship.route_index() >= route.size()) {
		return true;
	}
	if (ship.route_mark() > 0 && ship.route_mark() < route.size()) {
		const MapPoint mark = route[ship.route_mark()];
		if (std::abs(mark.x - x) > 0x48 || std::abs(mark.y - y) > 0x48) {
			ship.route_mark() = 0;
		}
	}
	if (route[ship.route_index()] != MapPoint{x, y}) {
		return true;
	}

	// En el punto de la ruta: hacia el siguiente, desde el centro del píxel.
	if (++ship.route_index() < route.size()) {
		ship.x() = with_fraction(ship.x(), kCentre);
		ship.y() = with_fraction(ship.y(), kCentre);
		const uint32_t v = speed(ship);
		set_course(ship, v, route[ship.route_index()]);
		// Compara la velocidad que se mostraba con la nueva sin escalar.
		if ((uint32_t(ship.speed()) ^ v) & 0xFFFFFFF0) {
			ship.notices() |= kNoticeSpeed;
		}
		ship.speed() = int32_t(v) * 64 / 115 + 4;
		return true;
	}

	// Fin de la ruta.
	ship.notices() |= kNoticeSpeed;
	ship.dx() = ship.dy() = ship.speed() = 0;
	const int town = town_at({x, y});
	if (town >= 0) {
		ship.notices() |= kNoticeRoute | kNoticeStatus;
		if (ship.raid()) {
			if (route.size() > 1) {
				ship.status() = 0x14;
				ship.town() = uint8_t(town);
				return false;
			}
			ship.town() = ship.destination() = kNoTown;
			return true;
		}
		const MapPoint here = approach(town);
		ship.x() = int32_t(uint32_t(uint16_t(here.x)) << 16);
		ship.y() = int32_t(uint32_t(uint16_t(here.y)) << 16);
		ship.town() = uint8_t(town);
		return false;
	}
	// Se queda en el mar sin ruta; a diferencia de 0x516840, el índice de la ruta no vuelve a 0.
	ship.set_route(nullptr);
	ship.own_route() = 0;
	ship.arrival_tick() = 0;
	ship.notices() |= kNoticeRoute;
	if (human(ship)) {
		send_ticker(save_, ship.owner(), 8, index);  // «The X has reached its destination.» (0x5170D1)
	}
	events_.push_back({ShipEvent::Stopped, index, -1});
	return true;
}

// Ha llegado a la ciudad ship.town() (0x506D8E): entra en el puerto, salvo
// que esté cerrado.
void Sailing::arrive(SavedShip& ship, uint16_t index, uint16_t& link) {
	const uint32_t flags = town_flags(ship.town());
	if ((flags & kClosedPort) || ((flags & kClosedToRoutes) && merchant(ship) && (ship.orders() & 1))) {
		clear_route(ship);
		if (ship.raid() && !(flags & kRaidBlocked)) {
			// Piratas ante un puerto bloqueado: sale de las listas y, salvo
			// que 0x50BA80 (con la cola de eventos y los convoyes, que no se
			// reproduce) lo una a otro ataque, vuelve el primero de la lista
			// del mar. Sin comprobar con el oráculo.
			unlink_x(index);
			link = ship.next_in_list();
			ship.next_in_list() = kNoIndex;
			insert_x(index, save_.sailing_head());
			ship.next_in_list() = save_.sailing_head();
			save_.sailing_head() = index;
			ship.town() = kNoTown;
			return;
		}
		// Se queda fuera sin ruta (la IA busca otro destino con 0x51A2C0 y
		// las rutas automáticas del jugador con 0x4D5900, que no se
		// reproducen), y el juego vuelve a pasar por él en este mismo tick.
		ship.origin() = ship.town();
		ship.town() = kNoTown;
		ship.status() = kShipSailing;
		return;
	}
	clear_route(ship);
	unlink_x(index);
	link = ship.next_in_list();
	ship.next_in_list() = kNoIndex;
	if (ship.raid() && ship.status() == 0x14) {
		return;  // Ataque pirata a la ciudad (0x5062F0...): no se reproduce.
	}
	ship.notices() |= kNoticeStatus;
	ship.status() = kShipArriving;
	ship.timer() = 0;
	ship.next_in_list() = save_.docked_head();
	save_.docked_head() = index;
}

// 0x507FB0 (on the ship object, with a ship): deletes the ship. Its route is
// cleared (0x516840), its captain goes back to the free list (0x5098B0) and
// [+0x42] = 0xFFFF, it leaves the list by x (0x50D040), and a trade route
// [+0x132] is released (0x4D4DD0 on 0x6DD728, which SaveGame does not have:
// not reproduced) and set to 0xFFFF. If it is already in the free list
// [0xE2], nothing else. Then state 0xFF; out of its owner's list ([+0xE]) and
// out of the owner's 11 ship lists ([+0x60C] counts, [+0x624] heap arrays:
// 0x4F5900, not reproduced when they hold ships); a ship of no merchant
// leaves the lists of the 5 pirates (0x515F00); owner 0xFF, [+0x4] = 0xFFFF.
// It leaves the first list it is in: in port ([0xE8]), at sea ([0xEA]) or
// the two shipyard lists of a town ([0x81C], [0x81E]; for a ship of no
// merchant the game starts at town 5), and its convoy's members (the game
// checks the convoy against the number of ships; then 0x501BE0). Last, the
// free list [0xE6] if [+0x137] > 0, otherwise [0xE2].
void Sailing::fn_507fb0(uint16_t index) {
	GlobalMemory& g = save_.globals;
	const uint32_t count = g.u16(global::kShipCount);
	if (index >= count) {
		return;  // Sin comprobar con el oráculo.
	}
	SavedShip& ship = save_.ships[index];
	ShipMemory& s = ship.memory;
	clear_ship_route(ship);
	tick::events_rare_detail::fn_5098b0(save_, s.u16(0x42));
	s.u16(0x42) = 0xFFFF;
	unlink_x(index);
	if (s.u16(0x132) < g.u16(0x6DD72A)) {  // The trade routes' count ([+0x2] of 0x6DD728).
		s.u16(0x132) = 0xFFFF;  // Sin comprobar con el oráculo.
	}
	for (uint32_t i = g.u16(global::kShips + 0xE2); i < count; i = save_.ships[i].memory.u16(6)) {
		if (i == index) {
			return;  // Sin comprobar con el oráculo.
		}
	}
	s.u16(0x134) = 0xFF;
	uint32_t first_town = 0;
	if (s.u8(0) < save_.merchant_count) {
		MerchantMemory& m = save_.merchants[s.u8(0)].memory;
		for (uint16_t* link = &m.u16(0xE); *link < count;) {
			if (*link == index) {
				*link = s.u16(4);
			} else {
				link = &save_.ships[*link].memory.u16(4);
			}
		}
		s.u8(0) = 0xFF;
		for (size_t k = 0; k < 11; ++k) {
			if (m.u16(0x60C + 2 * k) & 0x7FFF) {
				// Sin comprobar con el oráculo: the heap array is not in SaveGame.
			}
		}
	} else {
		// Sin comprobar con el oráculo.
		for (auto& pirate : save_.pirates) {
			if (!pirate) {
				continue;
			}
			for (uint16_t* link = &pirate->u16(0xC); *link < count; link = &save_.ships[*link].memory.u16(4)) {
				if (*link == index) {
					*link = s.u16(4);
					s.u16(4) = 0xFFFF;
					break;
				}
			}
		}
		s.u8(0) = 0xFF;
		first_town = 5;
	}
	s.u16(4) = 0xFFFF;
	auto unlink = [&](uint16_t& head) {
		for (uint16_t* link = &head; *link < count; link = &save_.ships[*link].memory.u16(6)) {
			if (*link == index) {
				*link = s.u16(6);
				return true;
			}
		}
		return false;
	};
	if (!unlink(g.u16(global::kShips + 0xE8)) && !unlink(g.u16(global::kShips + 0xEA))) {
		bool found = false;
		for (uint32_t t = first_town; t < save_.towns.size() && !found; ++t) {
			TownMemory& town = save_.towns[t].memory;
			found = unlink(town.u16(0x81C));
			found = unlink(town.u16(0x81E)) || found;
		}
	}
	const uint16_t convoy = s.u16(8);
	if (convoy < count && convoy < save_.convoys.size() && unlink(save_.convoys[convoy].u16(0xA)) &&
		convoy < save_.convoy_count()) {
		tick::ship_ai_detail::fn_501be0(save_, convoy);
	}
	uint16_t& free = s.u8(0x137) > 0 ? g.u16(global::kShips + 0xE6) : g.u16(global::kShips + 0xE2);
	s.u16(6) = free;
	free = index;
}

// Casco a cero (0x506CD6). Los barcos de la IA se borran después (0x507FB0),
// lo que no se reproduce.
void Sailing::sink(SavedShip& ship, uint16_t index, uint16_t& link) {
	clear_route(ship);
	unlink_x(index);
	link = ship.next_in_list();
	ship.next_in_list() = kNoIndex;
	ship.notices() |= kNoticeStatus;
	ship.status() = kShipSunk;
	ship.health() = 0;
	if (merchant(ship)) {
		ship.memory.u16(0x50) = kNoIndex;
		ship.memory.u16(0x52) = kNoIndex;
		// 0x519D90 only tells the interface; to a human owner, the ticker line
		// «Your ship X has sunk.» (0x506D78: kind 0x16, the ship's name [+0x15E]).
		if (human(ship)) {
			send_ticker(save_, ship.owner(), 0x16, ship.memory.u16(0x15E));
		}
	}
	events_.push_back({ShipEvent::Sunk, index, -1});
}

// Termina de entrar en el puerto (0x519C90).
void Sailing::dock(SavedShip& ship, uint16_t index) {
	ship.notices() |= kNoticeStatus;
	ship.status() = kShipDocked;
	ship.speed() = 0;
	ship.moved_tick() = save_.tick();
	if (ship.town() < save_.towns.size()) {
		++save_.towns[ship.town()].memory.u16(0x996);  // Barcos que han atracado en la ciudad.
	}
	if (ship.destination() < save_.towns.size()) {
		SavedTown& town = save_.towns[ship.destination()];
		for (int w = 0; w < kWareCount; ++w) {
			town.in_transit(w) -= ship.cargo(w);
			town.incoming(w) -= ship.cargo(w);
			if (town.in_transit(w) < 0) {
				town.in_transit(w) = 0;
			}
		}
	}
	// A human owner's ship alone (not in a convoy, without the bit 1 of
	// [+0x136]): the ticker line «The snaikka X has docked in Y.» (0x519D72:
	// 0x548CA0 with kind 5 and the ship).
	if (human(ship) && !(ship.memory.u8(0x136) & 1) && ship.memory.u16(8) >= save_.convoy_count()) {
		send_ticker(save_, ship.owner(), 5, index);
	}
	ship.timer() = 0;
	events_.push_back({ShipEvent::Arrived, index, ship.town()});
}

// Sale del puerto al mar (0x5090E0).
void Sailing::start_voyage(SavedShip& ship, uint16_t index) {
	ship.moved_tick() = save_.tick();
	recalculate_cargo(ship);
	leave_port(ship);
	ship.notices() |= kNoticeRoute | kNoticeStatus;
	ship.status() = kShipSailing;
	ship.timer() = 0;
	ship.town() = kNoTown;
	int32_t total = 0;
	if (ship.destination() < save_.towns.size()) {
		SavedTown& town = save_.towns[ship.destination()];
		for (int w = 0; w < kWareCount; ++w) {
			const int32_t cargo = ship.cargo(w);
			if (cargo > 0) {
				town.in_transit(w) += cargo;
				town.incoming(w) += cargo;
				total += cargo;
			}
		}
	}
	insert_x(index, save_.sailing_head());
	if (merchant(ship)) {
		// Estadística de la bodega llena al zarpar, en el comerciante y en el barco.
		const float ratio = float(double(float(total)) / double(float(ship.capacity())));
		SavedMerchant& owner = save_.merchants[ship.owner()];
		owner.memory.f32(0x540) = float(double(ratio) + double(owner.memory.f32(0x540)));
		++owner.memory.u16(0x544);
		ship.load_factor_sum() = float(double(ratio) + double(ship.load_factor_sum()));
		++ship.voyages();
	} else {
		ship.status() = kShipPirate;
	}
}

// Coloca el barco en el punto de llegada de su ciudad para empezar la ruta
// (0x516BC0) y, si aún no tiene, le da una hacia su destino.
void Sailing::leave_port(SavedShip& ship) {
	ship.dx() = ship.dy() = 0;
	ship.origin() = ship.town();
	if (ship.town() < save_.towns.size()) {
		const MapPoint here = approach(ship.town());
		ship.x() = int32_t(uint32_t(uint16_t(here.x)) << 16);
		ship.y() = int32_t(uint32_t(uint16_t(here.y)) << 16);
		if (!ship.route && ship.destination() < save_.towns.size()) {
			const Assigned assigned = route_for(here, approach(ship.destination()));
			ship.set_route(assigned.route);
			ship.own_route() = assigned.own;
			ship.arrival_tick() = save_.tick() + travel_time(ship);
		}
	}
	ship.route_index() = 0;
	ship.route_mark() = 0;
	ship.heading() = ship.heading() < 8 ? uint8_t((ship.heading() - 4) & 7) : 2;
}

// Nueva ruta desde donde está el barco (0x5172A0).
void Sailing::set_destination(SavedShip& ship, int town, int x, int y) {
	const MapPoint start = {whole(ship.x()), whole(ship.y())};
	MapPoint end;
	if (town >= 0 && size_t(town) < save_.towns.size()) {
		end = approach(town);
		ship.destination() = uint8_t(town);
	} else {
		end = {int16_t(x), int16_t(y)};
		ship.destination() = kNoTown;
	}
	ship.x() = with_fraction(ship.x(), kCentre);
	ship.y() = with_fraction(ship.y(), kCentre);
	ship.dx() = ship.dy() = 0;
	clear_route(ship);
	const Assigned assigned = route_for(start, end);
	ship.set_route(assigned.route);
	ship.own_route() = assigned.own;
	ship.arrival_tick() = save_.tick() + travel_time(ship);
}

bool Sailing::set_sail(int index, int town, int x, int y) {
	if (index < 0 || size_t(index) >= save_.ships.size()) {
		return false;
	}
	SavedShip& ship = save_.ships[size_t(index)];
	ship.target() = kNoIndex;
	// En el astillero (6, en una lista de su ciudad) o en el estado 8, el
	// juego hace antes otras cosas que no se reproducen.
	if (ship.status() == 6 || ship.status() == 8) {
		return false;
	}
	ship.orders() &= 0xFC;
	// 0x517770: aviso de la nueva orden, también en el convoy ([+0x18]).
	ship.notices() |= kNoticeOrder;
	if (ship.convoy() < save_.convoy_count()) {
		save_.convoys[ship.convoy()].u32(0x18) |= kNoticeOrder;
	}
	const uint16_t status = ship.status();
	if (status >= 4 && status != kShipSailing && status != 0x10) {
		return false;
	}
	// En un convoy: el guía lo lleva ([+0x16] = 0); otro barco sale de él
	// (0x509F80, 0x501BE0) y, sin capitán, se queda sin ataque pendiente.
	int convoy = -1;
	if (ship.convoy() < save_.convoy_count()) {
		convoy = ship.convoy();
		GameMemory<0x3C>& c = save_.convoys[size_t(convoy)];
		if (c.u16(0x10) == index) {
			c.u16(0x16) = 0;
		} else {
			fn_509f80(uint16_t(index));
			tick::ship_ai_detail::fn_501be0(save_, convoy);
			if (ship.captain() >= save_.globals.u16(global::kCaptainCount)) {
				ship.notices() |= 0x1000;
				ship.raid() = 0;
			}
		}
	}
	if (status < 4 && !can_sail(ship)) {
		return false;
	}
	const bool to_town = town >= 0 && size_t(town) < save_.towns.size();
	auto at_origin = [&] {
		const MapPoint p = ship.origin() < save_.towns.size() ? approach(ship.origin()) : MapPoint{-1, -1};
		return p.x == x && p.y == y;
	};
	if (status == kShipLeaving) {
		if (to_town ? town == ship.origin() : at_origin()) {
			// Vuelve al puerto del que está saliendo; con su convoy, todo él.
			if (ship.convoy() < save_.convoy_count()) {
				GameMemory<0x3C>& c = save_.convoys[ship.convoy()];
				c.u16(0x12) = kShipArriving;
				c.u16(0x16) = uint16_t(kHarbourTicks - ship.timer());
				c.u8(0x1F) = ship.origin();
				const uint32_t ships = save_.globals.u16(global::kShipCount);
				for (uint32_t s = c.u16(0xA); s < ships; s = save_.ships[s].next_in_list()) {
					SavedShip& m = save_.ships[s];
					const uint16_t timer = uint16_t(kHarbourTicks - m.timer());
					m.notices() |= kNoticeStatus;
					m.status() = kShipArriving;
					m.timer() = timer;
					m.destination() = m.origin();
					m.arrival_tick() = save_.tick() - timer + kHarbourTicks;
					clear_route(m);
				}
				return true;
			}
			ship.notices() |= kNoticeStatus;
			ship.status() = kShipArriving;
			ship.timer() = uint16_t(kHarbourTicks - ship.timer());
			ship.destination() = ship.origin();
			ship.arrival_tick() = save_.tick() - ship.timer() + kHarbourTicks;
			clear_route(ship);
			return true;
		}
	} else if (status == kShipArriving) {
		if (to_town ? town == ship.destination() : at_origin()) {
			return false;
		}
		// Da la vuelta mientras entraba (con su convoy).
		ship.status() = kShipLeaving;
		ship.notices() |= kNoticeRoute | kNoticeStatus;
		ship.timer() = uint16_t(kHarbourTicks - ship.timer());
		ship.origin() = ship.town();
		if (ship.convoy() < save_.convoy_count()) {
			GameMemory<0x3C>& c = save_.convoys[ship.convoy()];
			c.u32(0x18) |= kNoticeRoute | kNoticeStatus;
			c.u16(0x1C) = ship.town();
			c.u16(0x12) = kShipLeaving;
			c.u16(0x16) = ship.timer();
		}
	} else if (status < 4) {
		if (to_town && town == ship.town()) {
			return false;
		}
		ship.notices() |= kNoticeStatus;
		ship.origin() = ship.town();
		ship.timer() = 0;
		ship.status() = kShipLeaving;
	}
	// El convoy con una expedición sin empezar ([+0x38] < 3) devuelve su
	// dinero al dueño del barco.
	if (convoy >= 0) {
		GameMemory<0x3C>& c = save_.convoys[size_t(convoy)];
		if (c.u16(0x38) < 3) {
			if (ship.owner() < save_.globals.u16(global::kWorldMerchantCount)) {
				save_.merchants[ship.owner()].memory.s32(0) += c.s32(0x20);
			}
			c.s32(0x20) = 0;
			c.u16(0x38) = 0xFFFF;
		}
	}
	set_destination(ship, to_town ? town : kNoTown, x, y);
	if (ship.destination() < save_.towns.size()) {
		ship.memory.u8(0x13B) = ship.destination();  // Última ciudad de destino.
	}
	ship.speed() = 1;
	if (ship.convoy() < save_.convoy_count()) {
		GameMemory<0x3C>& c = save_.convoys[ship.convoy()];
		c.u16(0x38) = 0xFFFF;
		c.u8(0x1F) = uint8_t(town);
		if (ship.status() == 0) {
			ship.status() = 1;
		}
		tick::ship_ai_detail::fn_501be0(save_, ship.convoy());
	}
	return true;
}

}  // namespace p3::data
