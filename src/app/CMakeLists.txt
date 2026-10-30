add_executable(p3vita main.cpp)

target_link_libraries(p3vita PRIVATE p3data)
target_compile_options(p3vita PRIVATE -Wall -Wextra)

if(VITA)
	target_link_libraries(p3vita PRIVATE SDL2::SDL2-static)

	set(P3_VITA_TITLEID "PTRC00003")
	set(P3_VITA_VERSION "00.01")

	include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
	vita_create_self(p3vita.self p3vita)
	vita_create_vpk(p3vita.vpk ${P3_VITA_TITLEID} p3vita.self
		VERSION ${P3_VITA_VERSION}
		NAME "Patrician III"
	)
else()
	target_link_libraries(p3vita PRIVATE SDL2::SDL2)
endif()
