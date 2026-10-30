# stb (https://github.com/nothings/stb), dominio público; commit en stb/COMMIT.
add_library(stb_image STATIC stb/stb_image.c)
target_include_directories(stb_image SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)

if(NOT VITA)
	add_library(stb_image_write STATIC stb/stb_image_write.c)
	target_include_directories(stb_image_write SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
endif()
