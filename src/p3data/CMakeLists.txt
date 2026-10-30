add_library(p3data STATIC
	aim.cpp
	cpr.cpp
	font.cpp
	image.cpp
	lz.cpp
	prices.cpp
	savegame.cpp
	town.cpp
	town_buildings.cpp
	thresholds.cpp
	trade.cpp
)

target_include_directories(p3data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(p3data PRIVATE stb_image stb_truetype)
target_compile_options(p3data PRIVATE -Wall -Wextra)
# Los precios reproducen bit a bit las cuentas en coma flotante del juego:
# sin fusionar multiplicaciones y sumas, que cambiarían los redondeos.
target_compile_options(p3data PRIVATE -ffp-contract=off)
