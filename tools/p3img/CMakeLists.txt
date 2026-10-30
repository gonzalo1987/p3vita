add_executable(p3img main.cpp)

target_link_libraries(p3img PRIVATE p3data stb_image_write)
target_compile_options(p3img PRIVATE -Wall -Wextra)
