add_executable(p3vita main.cpp battle_view.cpp script.cpp debug_states.cpp settings.cpp savegames.cpp)

target_link_libraries(p3vita PRIVATE p3data)
target_compile_options(p3vita PRIVATE -Wall -Wextra)

if(VITA)
	target_link_libraries(p3vita PRIVATE SDL2::SDL2-static)
	# vita-elf-create puts its SCE data (about 2.7 KB) right after the code
	# segment and fails ("segment 1 overlaps") when the data segment starts
	# closer than that, which depends on the code size. VitaSDK's linker script
	# leaves __sce_headroom bytes before the data segment.
	target_link_options(p3vita PRIVATE -Wl,--defsym,__sce_headroom=0x1000)

	set(P3_VITA_TITLEID "PTRC00003")
	set(P3_VITA_VERSION "00.01")

	include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
	vita_create_self(p3vita.self p3vita)
	vita_create_vpk(p3vita.vpk ${P3_VITA_TITLEID} p3vita.self
		VERSION ${P3_VITA_VERSION}
		NAME "Patrician III"
	)
else()
	# On the PC the viewer can also run without a window and save screenshots (--steps).
	target_link_libraries(p3vita PRIVATE SDL2::SDL2 stb_image_write)
endif()
