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

// 0x447580 (battle/storms.cpp): `route` goes round the storm at `storm`
// (a new route: the old one up to `current`, the ship's position `ship`, the
// corners of the octagon round the storm, the old one from `found`).
// 0x50E340 (battle/pirates.cpp): the point is in the shallows (table 0x673564).
bool fn_50e340(int16_t x, int16_t y);

bool fn_447580(const NavData& nav, std::shared_ptr<SeaRoute>& route, MapPoint storm, MapPoint ship, uint32_t found,
		uint32_t current, bool own);

// 0x6029B0 (battle/battle_step.cpp): the direction of (dx, dy), 256ths of a turn.
uint8_t fn_6029b0(int32_t dx, int32_t dy);

// 0x517170 (battle/pirates.cpp; on the ship): to the approach point of one of its towns.
void fn_517170(SaveGame& save, uint16_t ship);

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

struct BattleReader;  // battle/battle_save.cpp
class GameTexts;  // game_texts.hpp

class Sailing {
public:
	// `map_towns`: las 40 ciudades de scripts/StadtDaten.ini (load_sea_map_towns).
	Sailing(SaveGame& save, const NavData& nav, std::vector<SeaMapTown> map_towns);

	// Lo que hace el juego al leer los barcos (0x516520): los que navegan de
	// una ciudad a otra reciben la ruta de la matriz. Llamar una vez, tras cargar.
	// Then, as at the end of 0x549F40, check_loaded_routes.
	void assign_loaded_routes();
	// 0x5048C0 (sailing_load.cpp): fn_517c10 for each ship, from the last one.
	void check_loaded_routes();
	// 0x517C10: a ship no longer on its way to its next point gets a new route.
	void fn_517c10(uint16_t index);

	// Con false, en puerto solo se ejecutan los estados 1, 2, 3 y el 0 de los
	// jugadores humanos, como en tools/oracle/sail_oracle.py y en
	// town_oracle.py --sin-ia (para probar la navegación sin la IA).
	void set_ai(bool enabled) { ai_ = enabled; }
	// Con false no se recorren los convoyes (town_oracle.py --sin-convoyes).
	void set_convoys(bool enabled) { convoys_ = enabled; }
	// The storms (0x5053C0) run every 8 ticks after the convoys, as in the game
	// and the oracle. On by default (the whole world, 7 days in the 12 saves:
	// 0 differences); false matches town_oracle.py --no-storms.
	void set_storms(bool enabled) { storms_ = enabled; }

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
	// The player's convoy operations (docs/formats/operations.md), also in
	// convoys_members.cpp: a ship joins the convoy of `member` (0x509D10,
	// operation 0x18), a ship leaves its convoy or, its guide, disbands it
	// (0x539270, operation 0x17), a docked convoy waits for ships to a
	// destination (0x539CF0, operation 0x20).
	void fn_509d10(uint32_t member, uint32_t ship);
	void fn_539270(const uint8_t* operation);
	void fn_539cf0(const uint8_t* operation);
	// convoys_port.cpp: el convoy en puerto.
	bool fn_501eb0(int convoy);
	void fn_503b50(int convoy);
	void fn_503cf0(int convoy);
	void fn_503930(int convoy);

	// battle/pirates.cpp: the pirates at sea, with their sea battles
	// (set_battles); on by default, as in the game and town_oracle.py
	// (set_pirates(false) as town_oracle.py --no-pirates).
	void set_pirates(bool enabled) {
		pirates_ = enabled;
		battles_ = enabled;
	}
	bool pirates() const { return pirates_; }
	void fn_448780(int town);  // (navigation object) The waypoints round a town.
	const std::vector<uint32_t>& waypoints(int town);  // Those of 0x448780 (computed the first time).
	uint32_t fn_612930(uint16_t ship) const;  // (battles) The battle speed of a ship.
	bool fn_50d9e0(uint16_t ship);  // (ships object) A pirate's target.
	bool fn_515360(uint16_t ship);  // Whether a pirate attacks its target now.
	bool fn_505c90(uint16_t convoy, uint16_t& link);  // (ships object) A new course for a pirate convoy.
	void fn_514b80(size_t pirate, uint16_t convoy);  // (pirate object) The convoy disbanded at the nest.
	// battle/pirate_objects.cpp: the pirate objects' day (with set_pirates).
	void fn_514000(size_t pirate);  // (pirate object) Its daily update; may queue operation 0xB5.
	void fn_513ec0(size_t pirate);  // (pirate object) The pirate goes away.
	void fn_514d40(size_t pirate, uint16_t most, uint16_t strength);  // Operation 0xB5: a convoy leaves the nest.
	// battle/battles.cpp: the battles' clock (on with set_pirates, as the
	// oracle with --pirates; docs/cloud/battles-2.md §3).
	static constexpr uint32_t kBattleSteps = 84;
	void set_battles(bool enabled) { battles_ = enabled; }
	// The game's texts (from the installation's Patrician3.exe), for the
	// messages the battles write into their memory ([+0x1310]); without them
	// those messages are left as they were. tick_probe gives the English
	// ones, as the oracle's executable; the viewer, the installation's.
	void set_texts(const GameTexts* texts) { texts_ = texts; }
	// The same texts for the event queue's letters (the expedition's report).
	const GameTexts* texts() const { return texts_; }
	// The messages the game shows the player in a window, as the
	// executable's raw texts (with the game's control codes): the battle's
	// question when a battle against the player starts (the attack, 0x605420)
	// and the boarding's message (0x60C1C9). Only with the texts; nothing in
	// the game's memory. The viewer takes them after each tick.
	std::vector<std::string> take_player_messages() {
		std::vector<std::string> out;
		out.swap(player_messages_);
		return out;
	}
	bool battles() const { return battles_; }
	void step_battles();  // After each tick: operation 0x94 with kBattleSteps when there are battles.
	void fn_536bbf(uint32_t steps);  // Operation 0x94: the battles take their steps.
	void fn_612c90(bool network);  // (battles) Operation 0xC8 with 2 if a battle has a human side.
	void fn_612d20(bool network);  // (battles) Operation 0xC8 with 0 if none has.
	// Operation 0x96 (sea battle): a merchant's sides to the computer or back to the player.
	void fn_60fec9(size_t battle, uint16_t merchant, bool automatic);
	static constexpr size_t kSeaBattleSize = 0x1E98;
	static constexpr uint32_t kSeaBattleVtable = 0x67AD44;
	static constexpr uint32_t kSceneVtable = 0x67AF70;
	uint8_t fn_611cd0();  // (battles) A new sea battle; its slot or 0xFF.
	void fn_603a70(size_t block, uint8_t slot);  // The sea battle's constructor.
	void fn_618f10(size_t block);  // The scene's constructor.
	// battle/battle_setup.cpp: blocks of BattleGraph (save().battles).
	uint16_t fn_61a1d0(size_t scene);  // (scene) A free item, growing the arrays.
	uint16_t fn_619450(size_t scene, uint8_t a0, uint8_t a1, uint16_t a2, uint16_t a3, uint16_t a4, uint16_t a5);
	void fn_61a4e0(size_t entry, uint16_t ship, size_t battle, uint16_t index, uint8_t side);  // A scene's ship.
	int32_t fn_62125b(size_t entry) const;  // (scene's ship) Its strength.
	bool fn_61a750(size_t entry, size_t other) const;  // (scene's ship) Stronger than the other.
	bool fn_503ac0(uint16_t convoy) const;  // (convoy) Whether it is the Hanse fleet of its event 0x34.
	void fn_6197a0(size_t scene, uint16_t item, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);  // An item's box.
	void fn_6079d0(size_t battle);  // (sea battle, method +0x2C) The two sides in formation.
	void fn_60f479(size_t battle);  // (sea battle) The rocks and islets.
	void fn_608230(size_t battle, bool arg);  // (sea battle, method +0x18)
	void fn_605420(size_t battle, uint16_t a, uint16_t d, uint8_t c, uint32_t flag);  // (sea battle) Its setup.
	// 0x60659B..0x606AF5: the battle's message to the player on a side
	// (ships a and d), the raw text.
	std::string attack_message(size_t battle, uint16_t a, uint16_t d, size_t side) const;
	// battle/battle_step.cpp: the step of a battle.
	const ShipMemory& scene_ship(size_t entry) const;  // The ship of a scene's ship.
	size_t scene_battle(size_t entry) const;  // Its battle.
	bool fn_6212fb(size_t entry, size_t other, bool flag) const;  // (scene's ship) An enemy to attack.
	int32_t fn_61efdf(size_t entry) const;  // (scene's ship) Arms per hull.
	bool fn_61f02f(size_t entry, size_t other, bool flag) const;  // (scene's ship) Whether to board.
	void fn_619560(size_t battle, uint16_t item);  // (scene) An item freed.
	size_t battle_entry(size_t battle, uint16_t place) const;  // The scene's ship in a place.
	bool fn_609d40(size_t battle, uint16_t i, uint16_t j, int32_t& x, int32_t& y) const;  // Hulls crossing.
	// A ship's hull on a rock or an islet (item, place; the side's direction, or nullptr).
	bool fn_60b357(size_t battle, uint16_t item, uint16_t place, uint8_t* side) const;
	void fn_60adbf(size_t battle, uint16_t i, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t& x, int32_t& y,
		uint8_t& side) const;  // Where a segment meets a hull.
	bool fn_608f70(size_t battle, uint16_t i, uint16_t j);  // Two ships collide.
	void fn_609ba0(size_t battle, uint16_t i, uint16_t j);  // They collide and may grapple.
	bool fn_60bc5a(size_t battle, uint16_t item, uint16_t place) const;  // A ship inside an item's circle.
	void fn_60bcda(size_t battle, uint16_t item, uint16_t place, int32_t a, int32_t minus, uint8_t level);
	void fn_608850(size_t battle, uint16_t a, uint16_t b);  // (battle, method +0x0) Two items meet.
	void fn_6113c9(size_t battle);  // (battle) The wind of a sub-step.
	void fn_6197f0(size_t battle);  // (scene) The items that meet (method +0x0 for each pair).
	// battle/ship_step.cpp: the step of a scene's ship (blocks of BattleGraph).
	void fn_6271d7(size_t entry, uint8_t dir);  // (its [+0x4]) It turns towards a direction.
	void fn_6264c0(size_t entry, uint8_t wind, uint8_t strength, bool flag);  // (its [+0x4]) It moves.
	void fn_61dbbf(size_t entry);  // (scene's ship, method +0x4) A target.
	void fn_61f1bf(size_t entry);  // (scene's ship) A round of a boarding.
	void fn_60c1c9(size_t battle, uint16_t winner, uint16_t taken);  // (battle) A ship taken.
	// 0x60C3D6..0x60CA6F: the boarding's message to the player (and window).
	void boarding_message(size_t battle, uint16_t winner, uint16_t taken, bool attack);
	// The letters 0x54 (and the news 0x2A) of a pirate ship with a captain sunk or kept (0x50BFD0).
	void pirate_letters(uint16_t merchant, uint16_t ship, uint16_t captain, bool news);
	// The «of ...» of the owner of a battle's ship (0x60C430..0x60C612).
	std::string owner_phrase(size_t battle, size_t entry) const;
	void fn_540520(uint16_t winner, uint16_t taken);  // Operation 0x91: a ship takes another.
	// battle/battle_save.cpp: the battles in the save (piece 5).
	void load_battles();  // 0x611AE0: SaveGame::opaque::battles into SaveGame::battles.
	bool fn_604600(size_t battle, BattleReader& in);  // (sea battle, method +0x10) Its record.
	void fn_611649(size_t battle, uint8_t level);  // (battle) Its music raised.
	int32_t fn_620f0b(size_t entry, uint8_t side, uint16_t distance);  // (scene's ship) A broadside's damage.
	uint8_t fn_61f96f(size_t entry, int32_t damage, int16_t sailors, uint8_t side);  // (scene's ship) A hit.
	void sunk_pirate_news(size_t entry);  // The news when a pirate's ship with a captain sinks (0x61F96F).
	void fn_61dd4f(size_t entry, bool flag);  // (scene's ship, method +0x8) It fires.
	void fn_61a9e0(size_t entry, uint8_t wind);  // (scene's ship, method +0x0) Its sub-step.
	void fn_61a9e0_flee(size_t entry);  // (part of 0x61A9E0) An AI ship decides whether to flee.
	bool fn_611149(size_t battle, uint8_t side) const;  // (battle) Team 1 may head off.
	void fn_606cc0(size_t battle, uint16_t steps);  // (sea battle, method +0x14) Its step.
	bool human_ship_in_battle(size_t battle) const;  // (sea battle) [+0x1E92]: a ship of the player in a side's convoy.
	// battle/battle_end.cpp: the end of a sea battle.
	void fn_60d299(size_t battle, bool sailors);  // (sea battle, method +0x1C) The end.
	void fn_611740(size_t battle, bool free);  // (sea battle, method +0x4) Its destructor.
	void fn_612a20(uint32_t i, uint32_t j, uint8_t value);  // (battles) Ship j met by ship i.
	void fn_612bd0(uint16_t a, uint16_t b);  // (battles) Two convoys' ships met.
	void fn_50cb80(uint16_t convoy, uint16_t other, uint32_t value, uint8_t level);  // An event 5.
	void fn_50c9e0(uint16_t convoy, bool flag);  // A Hanse convoy after its battle.
	void fn_50bfd0(uint16_t convoy, uint8_t flag, uint8_t other);  // A convoy after its battle.
	void fn_4ec864(uint16_t ship, uint8_t other);  // (events) A pirate ship's event after it sank.
	void fn_4f8d90(uint8_t merchant, bool towns);  // (merchant) Fame down (a ship sunk).
	bool fn_503b00(uint16_t convoy) const;  // (convoy) The Hanse fleet of its event 0x34.
	void fn_504970();  // (ships object) The towns' wares on the way again.
	// battle/attack.cpp
	void fn_50bc40(uint16_t convoy);  // (ships object) A pirate convoy attacks: a sea battle.
	void fn_50b250(uint16_t chaser, uint16_t target);  // (ships object) A chase catches its target: a sea battle.
	bool fn_50ba80(uint16_t ship);  // (ships object) Pirates at a town the Hanse fleet guards: a sea battle.
	uint32_t chase_witnesses(uint16_t chaser);  // 0x50B250's count for the operation 0x9F.
	void chase_side(uint16_t ship);  // 0x50B250's side of a ship.
	void fn_50cfb0(uint16_t ship);  // (ships object) The player's ships stop targeting a ship.
	void fn_5176c0(uint16_t ship, uint16_t guide);  // (ship) Takes the guide's route if it has none.
	void fn_505b60(uint16_t ship);  // (ships object) A ship leaves the sea list.
	uint16_t fn_5063c0(uint16_t target, uint32_t kind);  // (ships object) The pirate of event 0x1E attacks a ship.
	// 0x5062F0 (ships object): a new convoy led by `ship`, at the head of the
	// list `head` (not inside SaveGame::convoys: it may grow); its index.
	uint16_t fn_5062f0(uint16_t& head, uint16_t ship);
	// The states 0x11 (a lost ship) and 0x12 (a pirate alone) of the walks of
	// 0x506720: the ship leaves the list (link takes the next one).
	void lost_in_port(uint16_t ship, uint16_t& link);  // 0x506AF2
	void lost_at_sea(uint16_t ship, uint16_t& link);  // 0x507022
	void pirate_at_sea(uint16_t ship, uint16_t& link);  // 0x507099
	// Off: a pirate convoy at its nest goes on as if it were elsewhere (the
	// third jump of the rule of piece 2; town_oracle.py --no-disband).
	void set_pirate_disbanding(bool enabled) { disband_ = enabled; }
	// 0x507A65: the state 0x12 of a pirate convoy in the walk of the convoys;
	// true if it left the list (the walk goes on from the same link).
	bool pirate_convoy(uint16_t convoy, uint16_t& link);

	// battle/storms.cpp: the storms.
	void fn_5053c0();  // (ships object) Every storm against the ships near it.
	void fn_505290(uint16_t ship, int32_t storm_x, int32_t storm_y);  // (ships object) The route round a storm.

	// 0x50CE50: mete el barco en la lista por x (también la usa la operación 0x78).
	void insert_x(uint16_t index, uint32_t after);
	// 0x507FB0 (on the ship object): deletes the ship (operation 0x0A).
	void fn_507fb0(uint16_t index);
	// 0x50B030 (on the ship object): the ship `index` chases its [+0x3A]
	// (pursue, below; for the oracle's probe).
	bool fn_50b030(uint16_t index);
	// 0x5172A0: nueva ruta del barco desde donde está, a la ciudad `town` o a
	// (x, y) (también la usa la flota de la Hansa, `diet.cpp`).
	void set_destination(SavedShip& ship, int town, int x, int y);

private:
	bool ai_ = true;
	bool convoys_ = true;
	bool storms_ = true;
	bool pirates_ = true;
	bool disband_ = true;
	bool battles_ = true;
	const GameTexts* texts_ = nullptr;
	std::vector<std::string> player_messages_;
	std::vector<std::vector<uint32_t>> waypoints_;  // 0x448780 per town ([+0x20], [+0x24] of the navigation)
	std::vector<bool> has_waypoints_;

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
