add_executable(cpr_test cpr_test.cpp)
target_link_libraries(cpr_test PRIVATE p3data)
target_compile_options(cpr_test PRIVATE -Wall -Wextra)

add_test(NAME cpr COMMAND cpr_test)

add_executable(aim_test aim_test.cpp)
target_link_libraries(aim_test PRIVATE p3data)
target_compile_options(aim_test PRIVATE -Wall -Wextra)

add_test(NAME aim COMMAND aim_test)

add_executable(town_test town_test.cpp)
target_link_libraries(town_test PRIVATE p3data)
target_compile_options(town_test PRIVATE -Wall -Wextra)

add_test(NAME town COMMAND town_test)

add_executable(buildings_test buildings_test.cpp)
target_link_libraries(buildings_test PRIVATE p3data)
target_compile_options(buildings_test PRIVATE -Wall -Wextra)

add_test(NAME buildings COMMAND buildings_test)

add_executable(prices_test prices_test.cpp)
target_link_libraries(prices_test PRIVATE p3data)
target_compile_options(prices_test PRIVATE -Wall -Wextra)

add_test(NAME prices COMMAND prices_test)

add_executable(trade_test trade_test.cpp)
target_link_libraries(trade_test PRIVATE p3data)
target_compile_options(trade_test PRIVATE -Wall -Wextra)

add_test(NAME trade COMMAND trade_test)
