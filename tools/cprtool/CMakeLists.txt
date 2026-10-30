add_executable(cprtool main.cpp)

target_link_libraries(cprtool PRIVATE p3data)
target_compile_options(cprtool PRIVATE -Wall -Wextra)
