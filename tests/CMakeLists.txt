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

add_executable(sailing_test sailing_test.cpp)
target_link_libraries(sailing_test PRIVATE p3data)
target_compile_options(sailing_test PRIVATE -Wall -Wextra)

add_test(NAME sailing COMMAND sailing_test)

add_executable(lz_test lz_test.cpp)
target_link_libraries(lz_test PRIVATE p3data)
target_compile_options(lz_test PRIVATE -Wall -Wextra)

add_test(NAME lz COMMAND lz_test)

add_executable(hanse_tasks_test hanse_tasks_test.cpp)
target_link_libraries(hanse_tasks_test PRIVATE p3data)
target_compile_options(hanse_tasks_test PRIVATE -Wall -Wextra)

add_test(NAME hanse_tasks COMMAND hanse_tasks_test)

add_executable(game_fixes_test game_fixes_test.cpp)
target_link_libraries(game_fixes_test PRIVATE p3data)
target_compile_options(game_fixes_test PRIVATE -Wall -Wextra)

add_test(NAME game_fixes COMMAND game_fixes_test)

add_executable(game_texts_test game_texts_test.cpp)
target_link_libraries(game_texts_test PRIVATE p3data)
target_compile_options(game_texts_test PRIVATE -Wall -Wextra)

add_test(NAME game_texts COMMAND game_texts_test)
