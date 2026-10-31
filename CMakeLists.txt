cmake_minimum_required(VERSION 3.16)

project(p3vita LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Without a build type CMake doesn't optimize at all, and tick_probe (which
# repeats every recorded call with p3data) runs 5 times slower: see
# docs/cloud/oracle-speed.md. So the default is -O2, keeping assert() (no
# NDEBUG). -fwrapv and -fno-strict-aliasing keep what -O0 does with signed
# overflow (it wraps, as in the game's x86 code) and with type punning, so the
# optimizer can't change a result. The Vita build sets its own build type.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES AND NOT MSVC)
	add_compile_options(-O2 -fwrapv -fno-strict-aliasing)
endif()

add_subdirectory(third_party)
add_subdirectory(src/p3data)

find_package(SDL2 CONFIG QUIET)
if(SDL2_FOUND)
	add_subdirectory(src/app)
else()
	message(STATUS "SDL2 no encontrado: no se compila la aplicación")
endif()

# Las herramientas de datos y los tests solo tienen sentido en el PC.
if(NOT VITA)
	add_subdirectory(tools/cprtool)
	add_subdirectory(tools/oracle)
	add_subdirectory(tools/p3img)
	add_subdirectory(tools/p3map)
	add_subdirectory(tools/p3save)
	add_subdirectory(tools/p3town)

	enable_testing()
	add_subdirectory(tests)
endif()
