add_executable(p3save main.cpp)

target_link_libraries(p3save PRIVATE p3data)
target_compile_options(p3save PRIVATE -Wall -Wextra)
