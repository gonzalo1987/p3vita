add_executable(compare_prices compare_prices.cpp)

target_link_libraries(compare_prices PRIVATE p3data)
target_compile_options(compare_prices PRIVATE -Wall -Wextra)

add_executable(compare_save compare_save.cpp)

target_link_libraries(compare_save PRIVATE p3data)
target_compile_options(compare_save PRIVATE -Wall -Wextra)

add_executable(compare_sail compare_sail.cpp)

target_link_libraries(compare_sail PRIVATE p3data)
target_compile_options(compare_sail PRIVATE -Wall -Wextra)
