add_executable(compare_prices compare_prices.cpp)

target_link_libraries(compare_prices PRIVATE p3data)
target_compile_options(compare_prices PRIVATE -Wall -Wextra)

add_executable(compare_save compare_save.cpp)

target_link_libraries(compare_save PRIVATE p3data)
target_compile_options(compare_save PRIVATE -Wall -Wextra)

add_executable(compare_sail compare_sail.cpp)

target_link_libraries(compare_sail PRIVATE p3data)
target_compile_options(compare_sail PRIVATE -Wall -Wextra)

add_executable(tick_probe tick_probe.cpp probes_shared.cpp probes_core.cpp probes_population.cpp
	probes_businesses.cpp probes_facilities.cpp probes_offices.cpp probes_construction.cpp probes_ship_ai.cpp
	probes_ship_trade.cpp probes_convoys_sea.cpp probes_convoys_members.cpp probes_convoys_port.cpp probes_merchants.cpp probes_daily.cpp probes_sieges.cpp
	probes_events.cpp probes_events_big.cpp probes_events_towns.cpp probes_events_mayor.cpp probes_events_rare.cpp
	probes_alderman.cpp probes_battle.cpp probes_unchecked.cpp)

target_link_libraries(tick_probe PRIVATE p3data)
target_compile_options(tick_probe PRIVATE -Wall -Wextra)
