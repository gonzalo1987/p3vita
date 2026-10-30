add_executable(p3town main.cpp)

target_link_libraries(p3town PRIVATE p3data stb_image_write)
target_compile_options(p3town PRIVATE -Wall -Wextra)
