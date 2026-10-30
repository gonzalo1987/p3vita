#pragma once

#include <cstdint>

#include "p3data/sailing.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// El paso del tiempo del juego: advance_time (0x530E80), con lo que
// reproduce p3vita. Cada tick sube el tick del mundo y, al empezar el día,
// la fecha (0x5310D0); en los ticks 8·i + 7 del día hace el tick de la ciudad
// i (0..31) y en los 8·(i − 32) + 3 el de las ciudades 32..39 (0x51BA10, ver
// town_tick.hpp); en los 8·i + 1 el del comerciante i (0..31) y en los
// 8·(i − 32) + 6 el de los comerciantes 32..63 (0x4F7BB0); al final, la
// actualización de los barcos (Sailing), con los convoyes.
//
// Después ejecuta las operaciones que se han encolado en el tick, en su orden
// (run_operation, el ejecutor 0x535760): el juego ejecuta su cola entre dos
// llamadas a advance_time, que hace hasta 8 ticks según el tiempo real; aquí,
// tras cada tick. Se reproducen las órdenes de zarpar (0x00, las encolan los
// barcos de la IA) y los encargos de la Hansa (0x85 y 0x86, la candidatura a
// Gobernador); las demás (las obras de las ciudades...) se descartan.
//
// Además, al empezar el día, lo diario (0x5315C0) y las tareas programadas
// (0x535450); en los ticks 8·i + 5 el objeto i de los 15 del mundo
// (0x52D7D0); tras el tick de la ciudad i en los 8·i + 7, su asedio
// (0x6349F0), y al final la cola de eventos (0x4D85C0) y el asedio de cada
// ciudad (0x633A90) (ver town_tick/internal.hpp). No se reproducen las
// estadísticas de cada día (0x54DE90).

// La ciudad (índice) que hace su tick en este tick del juego, o -1.
int town_for_tick(const SaveGame& save, uint32_t tick);

// El comerciante (índice) que hace su tick en este tick del juego, o -1.
int merchant_for_tick(const SaveGame& save, uint32_t tick);

// El tick de ciudad (0x51BA10) de la ciudad `town`.
void town_tick(SaveGame& save, int town);

// Un tick completo.
void advance_game_tick(SaveGame& save, Sailing& sailing);

// Ejecuta las operaciones encoladas y vacía la cola.
void run_queued_operations(SaveGame& save, Sailing& sailing);

// El ejecutor (0x535760): una operación encolada (0x14 bytes: u32 código y
// sus datos) según su código, de las que reproduce p3vita (0x00, 0x85 y
// 0x86); las demás no hacen nada. Sin `sailing`, la 0x00 tampoco.
void run_operation(SaveGame& save, Sailing* sailing, const uint8_t* operation);

}  // namespace p3::data
