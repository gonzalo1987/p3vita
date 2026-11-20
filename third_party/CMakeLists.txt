# stb (https://github.com/nothings/stb), dominio público; commit en stb/COMMIT.
add_library(stb_image STATIC stb/stb_image.c)
target_include_directories(stb_image SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)

add_library(stb_truetype STATIC stb/stb_truetype.c)
target_include_directories(stb_truetype SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)

if(NOT VITA)
	add_library(stb_image_write STATIC stb/stb_image_write.c)
	target_include_directories(stb_image_write SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)
endif()

# dr_mp3 (https://github.com/mackron/dr_libs), public domain or MIT-0; its commit in dr_libs/COMMIT. The game's music.
add_library(dr_mp3 STATIC dr_libs/dr_mp3.c)
target_include_directories(dr_mp3 SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dr_libs)
