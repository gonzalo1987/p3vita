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
// decisiones de la IA en puerto están en town_tick/ship_ai.cpp y los convoyes
// de los comerciantes en convoys*.cpp; los piratas, los encuentros y las
// batallas no se reproducen todavía.
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

// 0x517410: desplazamiento por tick y rumbo del barco hacia `target` con
// esa velocidad.
void set_ship_course(SavedShip& ship, uint32_t speed, MapPoint target);

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
	// Con false no se recorren los convoyes (town_oracle.py --sin-convoyes).
	void set_convoys(bool enabled) { convoys_ = enabled; }

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
	// o si es un caso que p3vita no reproduce (barcos en el astillero, estados 6 y 8).
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
	const NavData& nav() const { return nav_; }
	MapPoint approach(int town) const;

	// Ruta entre los puntos de llegada de dos ciudades (la matriz que el
	// juego calcula al cargar con 0x4482C0; aquí, la primera vez que se pide).
	std::shared_ptr<SeaRoute> town_route(int from, int to);
	// Una ciudad más al final de SaveGame::towns (0x448040, al fundarla): la
	// matriz de rutas pasa a n × n conservando las de antes; las de la nueva
	// se calculan al pedirlas, con el mismo algoritmo que usa el juego.
	void add_town();

	// Velocidad de un barco según su tipo, su carga y su casco (0x5170F0).
	static uint32_t speed(const SavedShip& ship);

	// Convoyes (registros de 0x3C bytes en SaveGame::convoys; ver
	// docs/formats/navigation.md §9): el tercer recorrido de 0x506720 y las
	// funciones del juego que lo cuelgan, con el nombre de su dirección. Los
	// argumentos que en el juego son punteros a un convoy o a un barco son su
	// índice. Públicas para las sondas de tools/oracle.
	void update_convoys();  // convoys.cpp
	// convoys_sea.cpp: el convoy navega.
	bool fn_502110(int convoy, uint32_t every_32);  // Un tick en el mar del convoy; false si deja de navegar.
	void fn_505c30(uint16_t ship);  // (objeto de los barcos)
	void fn_50a240(uint32_t arrival, uint32_t speed, uint32_t convoy, uint8_t heading);  // (objeto de los barcos)
	void fn_501e10(int convoy, int ship);
	void fn_5029f0(int convoy);
	// town_tick/events_rare.cpp: la vuelta de una expedición (evento 0x12).
	void fn_502b20(int convoy, uint32_t departure);
	// convoys_members.cpp: los barcos entran y salen del convoy.
	bool fn_5017f0(int convoy);
	void fn_509f80(uint16_t ship);  // (objeto de los barcos) Saca el barco de su convoy.
	void fn_50a580(uint16_t convoy);  // (objeto de los barcos) Deshace el convoy.
	void fn_50a3d0(uint16_t convoy);  // (objeto de los barcos) Borra el convoy.
	// convoys_port.cpp: el convoy en puerto.
	bool fn_501eb0(int convoy);
	void fn_503b50(int convoy);
	void fn_503cf0(int convoy);
	void fn_503930(int convoy);

	// 0x50CE50: mete el barco en la lista por x (también la usa la operación 0x78).
	void insert_x(uint16_t index, uint32_t after);
	// 0x5172A0: nueva ruta del barco desde donde está, a la ciudad `town` o a
	// (x, y) (también la usa la flota de la Hansa, `diet.cpp`).
	void set_destination(SavedShip& ship, int town, int x, int y);

private:
	bool ai_ = true;
	bool convoys_ = true;

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
	void rest(SavedShip& ship) const;

	SaveGame& save_;
	const NavData& nav_;
	std::vector<SeaMapTown> map_towns_;
	std::vector<std::shared_ptr<SeaRoute>> matrix_;  // n × n, se rellena al pedirla.
	std::vector<ShipEvent> events_;
};

}  // namespace p3::data
