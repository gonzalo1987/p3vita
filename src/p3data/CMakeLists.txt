add_library(p3data STATIC
	aim.cpp
	cpr.cpp
	font.cpp
	image.cpp
	lz.cpp
	navigation.cpp
	prices.cpp
	sailing.cpp
	savegame.cpp
	seamap.cpp
	simulation.cpp
	town.cpp
	town_buildings.cpp
	thresholds.cpp
	trade.cpp
	town_tick/businesses.cpp
	town_tick/construction.cpp
	town_tick/core.cpp
	town_tick/facilities.cpp
	town_tick/offices.cpp
	town_tick/population.cpp
	town_tick/shared.cpp
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
