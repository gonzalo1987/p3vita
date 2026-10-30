add_executable(compare_prices compare_prices.cpp)

target_link_libraries(compare_prices PRIVATE p3data)
target_compile_options(compare_prices PRIVATE -Wall -Wextra)
