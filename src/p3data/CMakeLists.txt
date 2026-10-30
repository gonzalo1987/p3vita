add_library(p3data STATIC
	aim.cpp
	cpr.cpp
	font.cpp
	image.cpp
	lz.cpp
	savegame.cpp
	town.cpp
	town_buildings.cpp
)

target_include_directories(p3data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(p3data PRIVATE stb_image stb_truetype)
target_compile_options(p3data PRIVATE -Wall -Wextra)
