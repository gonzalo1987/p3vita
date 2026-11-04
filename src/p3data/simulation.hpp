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
// barcos de la IA), construir (0x22, las obras de las ciudades y de los
// comerciantes, con sus materiales 0x27 y la muralla 0x23 y 0x24), los encargos de la Hansa (0x3B, 0x4B, 0x85 y 0x86, la
// candidatura a Gobernador), el alcalde de una ciudad nueva (0x46), las
// expediciones (0x1E y 0x1F), la dieta de la Hansa (0x79 y 0x7A), los
// capitanes (0x12, 0x7B), un barco nuevo (0x78), los robos en las oficinas
// (0x64), los asedios (0x8E), los préstamos (0x29), los encargos de la dieta
// (0x81) y los pagos por una ciudad (0x37); las demás (avisos...) se
// descartan.
//
// Además, al empezar el día, lo diario (0x5315C0) y las tareas programadas
// (0x535450); en los ticks 8·i + 5 el objeto i de los 15 del mundo
// (0x52D7D0); tras el tick de la ciudad i en los 8·i + 7, su asedio
// (0x6349F0), y al final la cola de eventos (0x4D85C0) y el asedio de cada
// ciudad (0x633A90) (ver town_tick/internal.hpp). The daily statistics
// (0x54DE90) run at the start of the day (statistics.hpp).

// La ciudad (índice) que hace su tick en este tick del juego, o -1.
int town_for_tick(const SaveGame& save, uint32_t tick);

// El comerciante (índice) que hace su tick en este tick del juego, o -1.
int merchant_for_tick(const SaveGame& save, uint32_t tick);

// El tick de ciudad (0x51BA10) de la ciudad `town`.
void town_tick(SaveGame& save, int town);

// Un tick completo.
void advance_game_tick(SaveGame& save, Sailing& sailing);

// The Sailing (and its navigation) the event queue and the town ticks use.
// advance_game_tick sets it in the middle of each tick, so what runs before
// that in the tick would still see the old one: call it when a new Sailing
// replaces the old one (another game loaded).
void use_sailing(Sailing& sailing);

// Ejecuta las operaciones encoladas y vacía la cola.
void run_queued_operations(SaveGame& save, Sailing& sailing);

// El ejecutor (0x535760): una operación encolada (0x14 bytes: u32 código y
// sus datos) según su código, de las que reproduce p3vita (0x00, 0x12, 0x1E,
// 0x1F, 0x22, 0x23, 0x24, 0x27, 0x29, 0x37, 0x3B, 0x3F, 0x46, 0x4B, 0x64, 0x78,
// 0x79, 0x7A, 0x7B, 0x81, 0x85, 0x86 y 0x8E); las demás no hacen nada. Sin
// `sailing`, la 0x00 tampoco.
void run_operation(SaveGame& save, Sailing* sailing, const uint8_t* operation);

}  // namespace p3::data
