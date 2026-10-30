#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "p3data/navigation.hpp"
#include "p3data/savegame.hpp"
#include "p3data/seamap.hpp"

namespace p3::data {

// Navegación de los barcos de una partida: reproducción de cómo
// Patrician3.exe les da una ruta, los mueve tick a tick, los hace salir y
// entrar en puerto, y obedece la orden de zarpar. Ver
// docs/formats/navigation.md §4 y §6.
//
// De la actualización de barcos de cada tick (0x506720) se reproducen los
// estados 0x0F (navegando), 2 (saliendo del puerto), 3 (entrando), 1
// (esperando en puerto) y 0 (atracado) de los barcos del jugador, con los
// avisos a la interfaz [+0x3C], la lista de los barcos en el mar ordenada
// por x ([+0xA], [+0xC]) y los contadores del objeto de los barcos. Las
// decisiones de la IA (town_tick/ship_ai.cpp), los piratas, los convoyes, el
// astillero, los encuentros y las batallas no se reproducen todavía.
// Comprobado con el oráculo (tools/oracle/sail_oracle.py y, el mundo entero,
// town_oracle.py --barcos --sin-ia con tick_probe; ver docs/oracle.md).

constexpr uint32_t kTicksPerDay = 256;

struct GameDate {
	int day = 1;  // 1..31
	int month = 0;  // 0 = enero.
	int year = 0;
};

// Fecha de un tick (0x5310D0): años de 365 días, sin bisiestos.
GameDate date_from_tick(uint32_t tick);

// 0x516840: quita la ruta al barco y vuelve al primer punto.
void clear_ship_route(SavedShip& ship);

// 0x5310D0: la fecha del mundo (día, mes, año y día del año) a partir de su
// tick. El juego la recalcula al empezar cada día.
void update_world_date(GlobalMemory& globals);

// Lo que le ha pasado a un barco durante un tick.
struct ShipEvent {
	enum Kind {
		Arrived,  // Ha atracado en `town` al terminar de entrar en el puerto.
		Stopped,  // Ha llegado al final de su ruta en el mar.
		Sunk,
	};
	Kind kind;
	int ship;
	int town;
};

class Sailing {
public:
	// `map_towns`: las 40 ciudades de scripts/StadtDaten.ini (load_sea_map_towns).
	Sailing(SaveGame& save, const NavData& nav, std::vector<SeaMapTown> map_towns);

	// Lo que hace el juego al leer los barcos (0x516520): los que navegan de
	// una ciudad a otra reciben la ruta de la matriz. Llamar una vez, tras cargar.
	void assign_loaded_routes();

	// Con false, en puerto solo se ejecutan los estados 1, 2, 3 y el 0 de los
	// jugadores humanos, como en tools/oracle/sail_oracle.py y en
	// town_oracle.py --sin-ia (para probar la navegación sin la IA).
	void set_ai(bool enabled) { ai_ = enabled; }

	// Un tick de advance_time (0x530E80) solo con la fecha y los barcos: lo
	// mismo que begin_tick y update_ships. El tick completo, con las ciudades,
	// es advance_game_tick (simulation.hpp).
	void advance_tick();

	// Principio de un tick: sube el tick y, al empezar el día, la fecha.
	void begin_tick();
	// La actualización de los barcos (0x506720), al final de cada tick.
	void update_ships();

	// Lo ocurrido en el último tick.
	const std::vector<ShipEvent>& events() const { return events_; }

	// Operación 0x00 (0x537670): el barco zarpa hacia la ciudad `town` o, si
	// town no es una ciudad de la partida, hacia el punto (x, y) del mapa.
	// Devuelve false si el juego no haría nada (no puede zarpar, ya está ahí...)
	// o si es un caso que p3vita no reproduce (convoyes, barcos en el astillero).
	bool set_sail(int ship, int town, int x = 0, int y = 0);

	// Lo que set_sail comprueba antes de zarpar un barco que está en puerto
	// (0x519BA0), en ese orden.
	enum class SailCheck {
		Ok,
		FewSailors,  // Menos marineros que el mínimo del tipo de barco.
		Tired,  // La tripulación no ha descansado (byte alto de readiness ≤ 0).
		Overloaded,  // Más carga que capacidad.
		Damaged,  // Con capitán y con el casco por debajo del 20 % (sin capitán no se mira).
		Busy,  // En un estado en el que no puede zarpar (astillero...).
		PortClosed,  // La ciudad está bloqueada o sitiada.
	};
	SailCheck sail_check(const SavedShip& ship) const;
	bool can_sail(const SavedShip& ship) const { return sail_check(ship) == SailCheck::Ok; }

	// Ciudad de la partida en un punto del mapa (0x446D50): la primera cuyo
	// símbolo lo contiene o cuyo punto de llegada es ese; -1 si ninguna.
	int town_at(MapPoint point) const;
	MapPoint approach(int town) const;

	// Ruta entre los puntos de llegada de dos ciudades (la matriz que el
	// juego calcula al cargar con 0x4482C0; aquí, la primera vez que se pide).
	std::shared_ptr<SeaRoute> town_route(int from, int to);

	// Velocidad de un barco según su tipo, su carga y su casco (0x5170F0).
	static uint32_t speed(const SavedShip& ship);

private:
	bool ai_ = true;

	struct Assigned {
		std::shared_ptr<SeaRoute> route;
		bool own = false;
	};

	Assigned route_for(MapPoint start, MapPoint end);
	uint32_t travel_time(const SavedShip& ship) const;
	bool merchant(const SavedShip& ship) const { return ship.owner() < save_.merchant_count; }
	bool human(const SavedShip& ship) const;
	bool player(const SavedShip& ship) const;  // Del comerciante del jugador ([0x6DFC14]).
	uint32_t town_flags(int town) const;

	// Lista de los barcos en el mar ordenada por x ([+0xA], [+0xC]).
	void unlink_x(uint16_t index);  // 0x50D040
	void sort_x(uint16_t index);  // 0x50D0C0
	void insert_x(uint16_t index, uint32_t after);  // 0x50CE50

	void update_docked();
	void waiting(SavedShip& ship);
	bool leaving(SavedShip& ship, uint16_t index, uint16_t& link);
	void arriving(SavedShip& ship, uint16_t index);
	void update_sailing();
	bool sail(SavedShip& ship, uint16_t index);
	bool pursue(SavedShip& ship);
	void arrive(SavedShip& ship, uint16_t index, uint16_t& link);
	void sink(SavedShip& ship, uint16_t index, uint16_t& link);
	void dock(SavedShip& ship, uint16_t index);
	void start_voyage(SavedShip& ship, uint16_t index);
	void leave_port(SavedShip& ship);
	void set_destination(SavedShip& ship, int town, int x, int y);
	void rest(SavedShip& ship) const;

	SaveGame& save_;
	const NavData& nav_;
	std::vector<SeaMapTown> map_towns_;
	std::vector<std::shared_ptr<SeaRoute>> matrix_;  // n × n, se rellena al pedirla.
	std::vector<ShipEvent> events_;
};

}  // namespace p3::data
