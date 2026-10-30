add_executable(p3map main.cpp)

target_link_libraries(p3map PRIVATE p3data stb_image_write)
target_compile_options(p3map PRIVATE -Wall -Wextra)
