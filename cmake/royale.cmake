# Pulled into Shipwright's soh/CMakeLists.txt by patches/0002-royale-cmake-hook.patch.
# Builds ENet and our UDP transport, makes the Royale headers visible to the game, and links it all into the game target.
set(ROYALE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

if(WIN32)
    # The game links the static MSVC runtime (/MT); a library built with the default /MD fails to link. ENet's own CMake is old
    # enough to ignore MSVC_RUNTIME_LIBRARY unless this policy is forced on first.
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT TARGET enet)
    add_subdirectory("${ROYALE_ROOT}/third_party/enet" "${CMAKE_BINARY_DIR}/royale-enet" EXCLUDE_FROM_ALL)
endif()

add_library(royale_net STATIC "${ROYALE_ROOT}/shared/enet_transport.cpp")
set_target_properties(royale_net PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(royale_net PUBLIC
    "${ROYALE_ROOT}/shared" "${ROYALE_ROOT}/server" "${ROYALE_ROOT}/client" "${ROYALE_ROOT}/mod/Royale"
    "${ROYALE_ROOT}/third_party/enet/include")
target_link_libraries(royale_net PUBLIC enet)
if(WIN32)
    set_target_properties(royale_net PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # ENet's own CMake only links these for MinGW.
    target_link_libraries(royale_net PUBLIC ws2_32 winmm)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE royale_net)
