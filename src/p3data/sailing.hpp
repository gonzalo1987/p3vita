#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <optional>
#include <utility>
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
	// The sea map's battle icons: when a ship of the player is in a sea battle ([+0x1E92]) the game tells its sea
	// map (0x457C90: the battle's slot and place) and clears the flag. The viewer takes them after each tick and
	// shows each icon while its battle lasts.
	struct BattleIcon {
		uint8_t slot;
		int16_t x, y;
	};
	std::vector<BattleIcon> take_sea_map_icons() {
		std::vector<BattleIcon> out;
		out.swap(sea_map_icons_);
		return out;
	}
	bool battles() const { return battles_; }
	void step_battles();  // After each tick: operation 0x94 with kBattleSteps when there are battles.
	void fn_536bbf(uint32_t steps);  // Operation 0x94: the battles take their steps.
	void fn_612c90(bool network);  // (battles) Operation 0xC8 with 2 if a battle has a human side.
	void fn_612d20(bool network);  // (battles) Operation 0xC8 with 0 if none has.
	// Operation 0xC8 (the real-time loop's 0x546DCF, {0xC8, -1, -1, mode, -1}): the time's mode ([0x6DF2F0 +
	// 0x92C]): 0 normal; 2 while a battle has a human side, when the world moves at most one tick each 3375 ms
	// ([0x673CF8]) whatever the speed, with no autosave. Not the world's: the viewer reads it.
	void set_time_mode(uint32_t mode) { time_mode_ = mode; }
	uint32_t time_mode() const { return time_mode_; }
	// Operation 0x96 (sea battle): a merchant's sides to the computer or back to the player.
	void fn_60fec9(size_t battle, uint16_t merchant, bool automatic);
	// 0x611E50 (on the battles, the local player): after a load (the game's interface, 0x43C5C7), the battle the
	// player directs goes on the screen ([+0x3FC]).
	void fn_611e50(uint8_t player);
	// 0x6102A9 (method +0x24 of a sea battle): the player takes it (the sea map's battle icon, its window's button).
	void fn_6102a9(size_t battle);
	// 0x6100D9 (on a battle): the player gives it back to the computer (he leaves its screen; the town step).
	void fn_6100d9(size_t battle);
	// battle/orders.cpp: the player's orders in a battle (the battles' piece 7): operations 0x92 (fire), 0x93 (a
	// target), 0x95 (a point to head for), 0x97 (board: capture or plunder), 0x98 (the sails), 0x9A (flee), 0x9B
	// (the white flag), each on its 0x14 bytes; 0xA0 (the enemy, fled, left: «Abandonar») on a battle.
	void fn_540f50(const uint8_t* op);
	void fn_541050(const uint8_t* op);
	void fn_541200(const uint8_t* op);
	void fn_541380(const uint8_t* op);
	void fn_5414e0(const uint8_t* op);
	void fn_5415e0(const uint8_t* op);
	void fn_541700(const uint8_t* op);
	void fn_6116a9(size_t battle, uint8_t merchant);
	void fn_6270c7(size_t entry, uint16_t x, uint16_t y);  // (scene's ship) Its goal and its heading to it.
	// battle/balls.cpp: the cannon balls (a human side's guns).
	bool on_screen(size_t battle) const;  // [0x6E59CC] is the battle.
	void battle_sound(size_t battle);  // A sound on the screen: the world's rand() with the sound on.
	void set_battle_sound(uint32_t variants) { save_.battle_window.sound_variants = variants; }
	uint16_t fn_6111b9(size_t battle);  // (battle) A free slot of the balls.
	void fn_602a90(size_t ball, uint16_t slot, uint16_t tx, uint16_t ty, uint16_t sx, uint16_t sy, int32_t damage,
		uint8_t byte, size_t battle, uint16_t shooter, uint16_t word);  // The ball's constructor.
	void ball_gone(size_t ball);  // Out of the battle's lists and the scene.
	void fn_6214cb(size_t entry, uint16_t x, uint16_t y, int32_t damage, uint8_t side, uint8_t index, uint8_t count,
		uint8_t heavy);  // (scene's ship) A ball.
	void fn_61e8ef(size_t entry, uint8_t side, uint16_t x, uint16_t y);  // (scene's ship) A broadside of balls.
	void fn_603130(size_t ball);  // (ball) Its sub-step.
	void fn_603470(size_t ball, uint16_t place, uint8_t side);  // (ball) It hits a ship.
	uint8_t fn_60a73c(size_t battle, uint16_t slot, uint16_t place);  // (battle) Whether a ball crosses a hull.
	// The interface's builders of the orders, from the battle's selection ([+0x66C], [+0xAD0]): 0x97 (plunder,
	// capture), 0x98, 0x9A, 0x9B, 0x93 (the target's place), 0x95 (with the lines' and the point's fields), 0x92
	// (fire), 0xA0 (abandon); the selection set by the viewer.
	void fn_6103e9(size_t battle, bool plunder, bool capture);
	void fn_610579(size_t battle, uint8_t sails);
	void fn_610c09(size_t battle);
	void fn_610fd9(size_t battle, bool on);
	void fn_608360(size_t battle, uint16_t target);
	void fn_608550(size_t battle, uint32_t x, uint32_t y, bool send);
	void queue_fire(size_t battle);
	void queue_abandon(size_t battle);
	void set_battle_selection(size_t battle, const std::vector<uint16_t>& places);
	std::vector<std::array<uint32_t, 2>> selection_marks(size_t battle, uint8_t skip) const;
	bool order_room() const;
	static constexpr size_t kSeaBattleSize = 0x1E98;
	static constexpr uint32_t kSeaBattleVtable = 0x67AD44;
	static constexpr uint32_t kSceneVtable = 0x67AF70;
	uint8_t fn_611cd0();  // (battles) A new sea battle; its slot or 0xFF.
	void fn_603a70(size_t block, uint8_t slot);  // The sea battle's constructor.
	// battle/town_battle.cpp: the town battles (piece 6).
	uint8_t fn_611d80();  // (battles) A new town battle; its slot or 0xFF.
	uint8_t fn_6130b0(uint8_t town, uint8_t owner) const;  // (battles) The running one of a town and attacker.
	void fn_5fbfe0(size_t battle, uint16_t ship, uint8_t town);  // (town battle) The setup.
	// 0x5FD102..0x5FD3DD: the town battle's message to the local player on a side (the attacker's ship `ship`),
	// the raw text; nothing written into the battle. town_end_message: 0x5FF5B6..0x5FF76D, the end's message to
	// the player's side `side` (empty when the game has none), with the loot of 0x5FE7E0.
	std::string town_attack_message(size_t battle, uint16_t ship, uint8_t town) const;
	std::string town_end_message(size_t battle, size_t side, bool lost, uint32_t loot) const;
	void fn_5ffc10(size_t battle);  // (town battle) Its map of the water.
	bool fn_5ffe20(size_t battle, int32_t x, int32_t y) const;  // (town battle) A cell out of the map or not water.
	void fn_600060(size_t battle);  // (town battle, method +0x2C) The formation.
	void fn_600660(size_t battle, bool arg);  // (town battle, method +0x18)
	void fn_60234c(size_t battle, uint16_t ship);  // (town battle) A ship (and its convoy) joins it.
	void fn_6027cc(size_t battle, uint16_t place);  // (town battle) A joining ship's start.
	// The town battle of a raider at a town (0x506F33, sailing.cpp), a raiding convoy at a town (0x50797E) and a
	// pirate convoy at its route's end (0x507C0B): in the battle of that town and owner (0x6130B0, 0x60234C) or
	// a new one (0x611D80, 0x5FBFE0). The slot.
	uint8_t town_attack(uint16_t ship, uint8_t town, bool join);
	// The attack on a nest of the Hanse task of type 3 (0x4EBA71, alderman.cpp): the ship's convoy (or the ship)
	// leaves the sea for the nest and joins or starts its battle. False if its convoy isn't at sea. `head`: the
	// task's look's list head (0xFFFF at first; a convoy that wasn't at sea).
	bool nest_attack(uint16_t ship, GameMemory<0x18>& task, uint16_t& head);
	void fn_50e280(uint16_t convoy, uint8_t town);  // (ships) A convoy nudged a pixel by the town's kind.
	bool fn_515d00(size_t pirate) const;  // (pirate) Its nest destroyed or its convoy out.
	void fn_514530(size_t pirate);  // (pirate) Its nest's map and defender convoy for an attack.
	// 0x506720's local [esp+0x18], never initialized: the list head 0x5062F0 gets for a raider's convoy at a
	// town (its first value whatever the stack held; the oracle sets 0xFFFF at the function's entry,
	// ATTACK_HEAD), then the battle slot of the last attack.
	uint16_t attack_head_ = 0xFFFF;
	uint16_t fn_50cc90(uint8_t town);  // (ships) The town's guard ship out to its battle; its convoy.
	uint32_t fn_515d30(size_t pirate, uint32_t entry) const;  // (pirate) A nest tower's strength.
	bool fn_515ae0(size_t pirate, uint8_t& first, uint8_t& family) const;  // (pirate) A captain's names.
	// A town battle's town map (a town's town_class1, a nest's map).
	struct TownBattleGrid {
		uint32_t width = 0, height = 0;
		const std::vector<uint8_t>* cells = nullptr;
	};
	TownBattleGrid town_battle_grid(size_t battle) const;
	// The town view's geometry (0x58CFB0 cell → point, 0x58D030 point → cell; SaveGame::TownView).
	std::pair<int32_t, int32_t> view_cell_point(int32_t x, int32_t y) const;
	bool view_point_cell(int32_t px, int32_t py, int32_t& cx, int32_t& cy) const;
	void fn_618f10(size_t block);  // The scene's constructor.
	// battle/town_step.cpp: the step of a town battle (piece 6, part 3).
	void fn_5fd510(size_t battle, uint16_t steps);  // (town battle, method +0x14) Its step.
	void fn_5fe080(size_t battle, uint8_t tower);  // (town battle) A tower's step.
	void fn_5ff8e0(size_t battle, uint16_t a, uint16_t b);  // (town battle, method +0x0) Two items meet.
	void fn_600770(size_t battle, uint16_t place);  // (town battle) A ship on the town's land.
	uint8_t fn_60114b(size_t battle, int32_t x, int32_t y) const;  // (town battle) Its map of the water.
	bool fn_601c7b(size_t battle, uint16_t place, uint16_t x, uint16_t y) const;  // A hull on a cell.
	void fn_60130b(size_t battle, uint8_t tower, int32_t damage);  // (town battle) A tower hit.
	// battle/town_ship_step.cpp: a town battle's scene ship (0x1168 bytes).
	void fn_621770(size_t entry, uint8_t wind);  // (method +0x0) Its sub-step.
	void fn_621770_flee(size_t entry);  // (part of 0x621770) An AI ship decides whether to flee.
	void fn_6247b0(size_t entry);  // (method +0x4) A target.
	void fn_624950(size_t entry, bool flag);  // (method +0x8) It fires.
	void fn_625bf0(size_t entry);  // Its directions marked round the land.
	void fn_6258a0(size_t entry, uint16_t x, uint16_t y);  // Its map of distances to a point.
	void fn_625b00(size_t entry, uint16_t& x, uint16_t& y) const;  // The nearest water of its map.
	uint8_t town_ship_map(size_t entry, uint32_t x, uint32_t y) const;  // Its map of distances at a cell.
	// battle/town_end.cpp: the end of a town battle (piece 6, part 4).
	void fn_5fe7e0(size_t battle, bool lost);  // (town battle, method +0x1C) The end.
	void fn_5fb9f0(size_t battle);  // (town battle, method +0x4 0x602990) Its destructor.
	void fn_514920(size_t pirate);  // (pirate object) Its nest after a town battle.
	void fn_542e40(uint32_t pirate, uint32_t difficulty, uint32_t town, uint32_t number);  // Operation 0xB4.
	static constexpr uint32_t kTownBattleVtable = 0x67AAA0;
	static constexpr uint32_t kTownShipVtable = 0x67AAD0;
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
	bool fn_5fbf30(size_t battle, BattleReader& in);  // (town battle, method +0x10) Its record.
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
	void tell_sea_map(size_t battle);  // (sea battle) [+0x1E92] set: the sea map's icon, the flag cleared.
	// The battles' window (SaveGame::battle_window; the game's *0x6CBB10): whether it is open (its method +0xC8),
	// closed with a result (0x41E680; its method +0x118 closes it with 0; 10 is its button), opened on a battle's
	// description [+0x12EC] (0x41D9B0). The description (0x20 bytes) with its text [+0x1310]: the question (one
	// button, 0x6B2BC0 «Steer manually») or the end (none).
	bool battle_window_open() const;
	void close_battle_window(uint32_t result = 0);
	void open_battle_window(size_t battle);
	void battle_window_description(size_t battle, bool question);
	void write_battle_text(size_t battle, const std::string& text);  // [+0x1310], as the game's sprintf.
	// battle/battle_end.cpp: the end of a sea battle.
	void fn_60d299(size_t battle, bool sailors);  // (sea battle, method +0x1C) The end.
	// 0x60DDA7..0x60E8C3: the end's message to the local player's human side (nothing: the old text stays).
	std::optional<std::string> sea_end_message(size_t battle, size_t side, bool sailors) const;
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
	bool fn_50a4e0(uint16_t convoy);  // (ships object) A convoy leaves the list of the convoys at sea.
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
	template <class F>
	void for_marked(size_t battle, uint8_t group, uint32_t low, uint32_t high, F f);  // battle/orders.cpp
	template <class F>
	void queue_marked(size_t battle, uint8_t skip, F bytes);  // battle/orders.cpp
	bool ai_ = true;
	bool convoys_ = true;
	bool storms_ = true;
	bool pirates_ = true;
	bool disband_ = true;
	bool battles_ = true;
	const GameTexts* texts_ = nullptr;
	std::vector<std::string> player_messages_;
	std::vector<BattleIcon> sea_map_icons_;
	uint32_t time_mode_ = 0;
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
