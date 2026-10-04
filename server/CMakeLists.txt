cmake_minimum_required(VERSION 3.14)
project(royale CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only game logic: match, bots, storm, loot, protocol, server, client, loopback network.
add_library(royale_core INTERFACE)
target_include_directories(royale_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../shared ${CMAKE_CURRENT_SOURCE_DIR}/../client)

# Real UDP transport. ENet is plain C on Winsock / BSD sockets, so this builds for Windows and the Android NDK.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/enet ${CMAKE_CURRENT_BINARY_DIR}/enet EXCLUDE_FROM_ALL)
add_library(royale_net STATIC ${CMAKE_CURRENT_SOURCE_DIR}/../shared/enet_transport.cpp)
target_link_libraries(royale_net PUBLIC royale_core enet)
target_include_directories(royale_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/enet/include)

enable_testing()
foreach(t tests net_tests enet_tests)
    add_executable(royale_${t} tests/${t}.cpp)
    target_link_libraries(royale_${t} royale_net)
    if(MSVC)
        target_compile_options(royale_${t} PRIVATE /W4)
    else()
        target_compile_options(royale_${t} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME royale_${t} COMMAND royale_${t})
endforeach()
