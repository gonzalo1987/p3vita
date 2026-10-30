cmake_minimum_required(VERSION 3.16)

project(p3vita LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
	add_subdirectory(tools/p3img)
	add_subdirectory(tools/p3save)
	add_subdirectory(tools/p3town)

	enable_testing()
	add_subdirectory(tests)
endif()
