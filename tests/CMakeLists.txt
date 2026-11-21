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

add_executable(mail_test mail_test.cpp)
target_link_libraries(mail_test PRIVATE p3data)
target_compile_options(mail_test PRIVATE -Wall -Wextra)

add_test(NAME mail COMMAND mail_test)

add_executable(image_test image_test.cpp)
target_link_libraries(image_test PRIVATE p3data)
target_compile_options(image_test PRIVATE -Wall -Wextra)

add_test(NAME image COMMAND image_test)

add_executable(ship_menu_test ship_menu_test.cpp)
target_link_libraries(ship_menu_test PRIVATE p3data)
target_compile_options(ship_menu_test PRIVATE -Wall -Wextra)

add_test(NAME ship_menu COMMAND ship_menu_test)

add_executable(new_game_test new_game_test.cpp)
target_link_libraries(new_game_test PRIVATE p3data)
target_compile_options(new_game_test PRIVATE -Wall -Wextra)

add_test(NAME new_game COMMAND new_game_test)

add_executable(trade_route_window_test trade_route_window_test.cpp)
target_link_libraries(trade_route_window_test PRIVATE p3data)
target_compile_options(trade_route_window_test PRIVATE -Wall -Wextra)

add_test(NAME trade_route_window COMMAND trade_route_window_test)

add_executable(letter_window_test letter_window_test.cpp)
target_link_libraries(letter_window_test PRIVATE p3data)
target_compile_options(letter_window_test PRIVATE -Wall -Wextra)

add_test(NAME letter_window COMMAND letter_window_test)

add_executable(building_windows_test building_windows_test.cpp)
target_link_libraries(building_windows_test PRIVATE p3data)
target_compile_options(building_windows_test PRIVATE -Wall -Wextra)

add_test(NAME building_windows COMMAND building_windows_test)

add_executable(guild_window_test guild_window_test.cpp)
target_link_libraries(guild_window_test PRIVATE p3data)
target_compile_options(guild_window_test PRIVATE -Wall -Wextra)

add_test(NAME guild_window COMMAND guild_window_test)

add_executable(office_window_test office_window_test.cpp)
target_link_libraries(office_window_test PRIVATE p3data)
target_compile_options(office_window_test PRIVATE -Wall -Wextra)

add_test(NAME office_window COMMAND office_window_test)

add_executable(offer_window_test offer_window_test.cpp)
target_link_libraries(offer_window_test PRIVATE p3data)
target_compile_options(offer_window_test PRIVATE -Wall -Wextra)

add_test(NAME offer_window COMMAND offer_window_test)

add_executable(smithy_window_test smithy_window_test.cpp)
target_link_libraries(smithy_window_test PRIVATE p3data)
target_compile_options(smithy_window_test PRIVATE -Wall -Wextra)

add_test(NAME smithy_window COMMAND smithy_window_test)

add_executable(tavern_window_test tavern_window_test.cpp)
target_link_libraries(tavern_window_test PRIVATE p3data)
target_compile_options(tavern_window_test PRIVATE -Wall -Wextra)

add_test(NAME tavern_window COMMAND tavern_window_test)

add_executable(land_trade_window_test land_trade_window_test.cpp)
target_link_libraries(land_trade_window_test PRIVATE p3data)
target_compile_options(land_trade_window_test PRIVATE -Wall -Wextra)

add_test(NAME land_trade_window COMMAND land_trade_window_test)

add_executable(ship_pair_window_test ship_pair_window_test.cpp)
target_link_libraries(ship_pair_window_test PRIVATE p3data)
target_compile_options(ship_pair_window_test PRIVATE -Wall -Wextra)

add_test(NAME ship_pair_window COMMAND ship_pair_window_test)

add_executable(town_hall_window_test town_hall_window_test.cpp)
target_link_libraries(town_hall_window_test PRIVATE p3data)
target_compile_options(town_hall_window_test PRIVATE -Wall -Wextra)

add_test(NAME town_hall_window COMMAND town_hall_window_test)

add_executable(town_gate_window_test town_gate_window_test.cpp)
target_link_libraries(town_gate_window_test PRIVATE p3data)
target_compile_options(town_gate_window_test PRIVATE -Wall -Wextra)

add_test(NAME town_gate_window COMMAND town_gate_window_test)

add_executable(market_hall_window_test market_hall_window_test.cpp)
target_link_libraries(market_hall_window_test PRIVATE p3data)
target_compile_options(market_hall_window_test PRIVATE -Wall -Wextra)

add_test(NAME market_hall_window COMMAND market_hall_window_test)

add_executable(ship_list_window_test ship_list_window_test.cpp)
target_link_libraries(ship_list_window_test PRIVATE p3data)
target_compile_options(ship_list_window_test PRIVATE -Wall -Wextra)

add_test(NAME ship_list_window COMMAND ship_list_window_test)

add_executable(ship_menu_window_test ship_menu_window_test.cpp)
target_link_libraries(ship_menu_window_test PRIVATE p3data)
target_compile_options(ship_menu_window_test PRIVATE -Wall -Wextra)

add_test(NAME ship_menu_window COMMAND ship_menu_window_test)

add_executable(statistics_window_test statistics_window_test.cpp)
target_link_libraries(statistics_window_test PRIVATE p3data)
target_compile_options(statistics_window_test PRIVATE -Wall -Wextra)

add_test(NAME statistics_window COMMAND statistics_window_test)

add_executable(missions_test missions_test.cpp)
target_link_libraries(missions_test PRIVATE p3data)
target_compile_options(missions_test PRIVATE -Wall -Wextra)

add_test(NAME missions COMMAND missions_test)

add_executable(sound_test sound_test.cpp)
target_link_libraries(sound_test PRIVATE p3data)
target_compile_options(sound_test PRIVATE -Wall -Wextra)

add_test(NAME sound COMMAND sound_test)

add_executable(town_life_test town_life_test.cpp)
target_link_libraries(town_life_test PRIVATE p3data)
target_compile_options(town_life_test PRIVATE -Wall -Wextra)

add_test(NAME town_life COMMAND town_life_test)
