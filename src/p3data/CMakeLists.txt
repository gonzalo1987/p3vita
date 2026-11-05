add_library(p3data STATIC
	aim.cpp
	battle/attack.cpp
	battle/battle_setup.cpp
	battle/battle_step.cpp
	battle/ship_step.cpp
	battle/capture.cpp
	battle/battle_end.cpp
	battle/battle_save.cpp
	battle/battles.cpp
	battle/pirate_nests.cpp
	battle/pirate_objects.cpp
	battle/pirates.cpp
	battle/storms.cpp
	convoys.cpp
	convoys_members.cpp
	convoys_port.cpp
	convoys_sea.cpp
	cpr.cpp
	font.cpp
	game_fixes.cpp
	game_texts.cpp
	hanse_tasks.cpp
	player_actions.cpp
	image.cpp
	letter_picture.cpp
	letters.cpp
	lz.cpp
	mail.cpp
	navigation.cpp
	prices.cpp
	sailing.cpp
	sailing_load.cpp
	savegame.cpp
	savegame_write.cpp
	savegame_load.cpp
	statistics.cpp
	lz_compress.cpp
	seamap.cpp
	simulation.cpp
	town.cpp
	town_buildings.cpp
	thresholds.cpp
	trade.cpp
	town_tick/alderman.cpp
	town_tick/businesses.cpp
	town_tick/construction.cpp
	town_tick/grid.cpp
	town_tick/daily.cpp
	town_tick/events.cpp
	town_tick/events_big.cpp
	town_tick/events_mayor.cpp
	town_tick/events_rare.cpp
	town_tick/events_towns.cpp
	town_tick/core.cpp
	town_tick/facilities.cpp
	town_tick/merchants.cpp
	town_tick/new_town.cpp
	town_tick/operations.cpp
	town_tick/diet.cpp
	town_tick/offices.cpp
	town_tick/population.cpp
	town_tick/sieges.cpp
	town_tick/votes.cpp
	town_tick/shared.cpp
	town_tick/ship_ai.cpp
	town_tick/ship_operations.cpp
	town_tick/ship_trade.cpp
)

target_include_directories(p3data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(p3data PRIVATE stb_image stb_truetype)
target_compile_options(p3data PRIVATE -Wall -Wextra)
# Los precios reproducen bit a bit las cuentas en coma flotante del juego:
# sin fusionar multiplicaciones y sumas, que cambiarían los redondeos.
target_compile_options(p3data PRIVATE -ffp-contract=off)
# La simulación de las ciudades lee y escribe los objetos del juego por offset
# (game_memory.hpp).
target_compile_options(p3data PUBLIC -fno-strict-aliasing)
# Signed overflow wraps, as in the game's x86 code. The PC builds checked with
# the oracle already use it (top-level CMakeLists.txt); here it reaches the
# Vita's Release build too, so the Vita computes what the oracle checked.
target_compile_options(p3data PUBLIC -fwrapv)
