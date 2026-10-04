cmake_minimum_required(VERSION 3.14)
project(royale_server CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(royale_server INTERFACE)
target_include_directories(royale_server INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../shared)

enable_testing()
add_executable(royale_tests tests/tests.cpp)
target_link_libraries(royale_tests royale_server)
target_compile_options(royale_tests PRIVATE -Wall -Wextra)
add_test(NAME royale_tests COMMAND royale_tests)
