cmake_minimum_required(VERSION 3.14)
project(royale CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only game logic: match, bots, storm, loot, protocol, server, client, loopback network.
add_library(royale_core INTERFACE)
target_include_directories(royale_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../shared ${CMAKE_CURRENT_SOURCE_DIR}/../client
    ${CMAKE_CURRENT_SOURCE_DIR}/../mod/Royale)

# Real UDP transport. ENet is plain C on Winsock / BSD sockets, so this builds for Windows and the Android NDK.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/enet ${CMAKE_CURRENT_BINARY_DIR}/enet EXCLUDE_FROM_ALL)
add_library(royale_net STATIC ${CMAKE_CURRENT_SOURCE_DIR}/../shared/enet_transport.cpp)
target_link_libraries(royale_net PUBLIC royale_core enet)
if(WIN32)
    # ENet's own CMake only links these for MinGW, so MSVC consumers must do it themselves.
    target_link_libraries(royale_net PUBLIC ws2_32 winmm)
endif()
target_include_directories(royale_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/enet/include)

enable_testing()
add_executable(royale_war_table_tests tests/war_table_tests.cpp)
target_link_libraries(royale_war_table_tests PRIVATE royale_core)
add_test(NAME royale_war_table_tests COMMAND royale_war_table_tests)
foreach(t tests net_tests enet_tests session_tests music_tests fish_tests maya_tests lobby_pets_tests)
    add_executable(royale_${t} tests/${t}.cpp)
    target_link_libraries(royale_${t} royale_net)
    if(MSVC)
        target_compile_options(royale_${t} PRIVATE /W4)
    else()
        target_compile_options(royale_${t} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME royale_${t} COMMAND royale_${t})
endforeach()

# Headless balance simulator (not a test): plays bot-only matches and prints how they went. Run: royale_balance 12 normal 0 32
add_executable(royale_balance tools/balance_sim.cpp)
target_link_libraries(royale_balance royale_net)

# Placement preview: lays a map out as a match does and dumps it; tools/maps/placement_preview.py draws it. Run: royale_placement_preview 5 1 out.txt
add_executable(royale_placement_preview tools/placement_preview.cpp)
target_link_libraries(royale_placement_preview royale_net)

add_executable(royale_graphics_tests tests/graphics_tests.cpp)
target_link_libraries(royale_graphics_tests PRIVATE royale_core)
add_test(NAME royale_graphics_tests COMMAND royale_graphics_tests)

add_executable(royale_graphics_layers_tests tests/graphics_layers_tests.cpp)
target_link_libraries(royale_graphics_layers_tests PRIVATE royale_core)
add_test(NAME royale_graphics_layers_tests COMMAND royale_graphics_layers_tests)

add_executable(royale_shadow_tests tests/shadow_tests.cpp)
target_link_libraries(royale_shadow_tests PRIVATE royale_core)
add_test(NAME royale_shadow_tests COMMAND royale_shadow_tests)

add_executable(royale_water_tests tests/water_tests.cpp)
target_link_libraries(royale_water_tests PRIVATE royale_core)
add_test(NAME royale_water_tests COMMAND royale_water_tests)

# Authored map collision and navigation must be tested by CI, not only manually.
foreach(t convergence_tests kingdom_tests)
    add_executable(royale_${t} tests/${t}.cpp)
    target_link_libraries(royale_${t} PRIVATE royale_core)
    if(MSVC)
        target_compile_options(royale_${t} PRIVATE /bigobj)
    endif()
    add_test(NAME royale_${t} COMMAND royale_${t})
endforeach()

# Keep assert-based checks active in optimized local test builds too.
foreach(t war_table_tests tests net_tests enet_tests session_tests music_tests fish_tests maya_tests lobby_pets_tests graphics_tests graphics_layers_tests shadow_tests water_tests convergence_tests kingdom_tests)
    if(MSVC)
        target_compile_options(royale_${t} PRIVATE /UNDEBUG)
    else()
        target_compile_options(royale_${t} PRIVATE -UNDEBUG)
    endif()
endforeach()
