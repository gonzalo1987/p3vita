#pragma once

#include <cstdint>

#include "p3data/sailing.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// El paso del tiempo del juego: advance_time (0x530E80), con lo que
// reproduce p3vita. Cada tick sube el tick del mundo y, al empezar el día,
// la fecha (0x5310D0); en los ticks 8·i + 7 del día hace el tick de la ciudad
// i (0..31) y en los 8·(i − 32) + 3 el de las ciudades 32..39 (0x51BA10, ver
// town_tick.hpp); al final, la actualización de los barcos (Sailing).
//
// No se reproducen todavía el tick de cada comerciante (0x4F7BB0), los de los
// 15 objetos del mundo (0x52D7D0), los asedios (0x6349F0, 0x633A90), las
// tareas programadas (0x535450) ni lo que hace el juego cada día y cada
// semana (0x5315C0, 0x54DE90).

// La ciudad (índice) que hace su tick en este tick del juego, o -1.
int town_for_tick(const SaveGame& save, uint32_t tick);

// El tick de ciudad (0x51BA10) de la ciudad `town`.
void town_tick(SaveGame& save, int town);

// Un tick completo. Las operaciones que encolan las ciudades se quedan en
// SaveGame::queued_operations.
void advance_game_tick(SaveGame& save, Sailing& sailing);

}  // namespace p3::data
