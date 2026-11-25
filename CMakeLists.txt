cmake_minimum_required(VERSION 3.16)
project(cpp_http_server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(HTTP_SERVER_BUILD_TESTS "Build the unit tests" ON)

# Everything except main.cpp goes into a library, so the server executable
# and the unit tests share the same compiled code.
file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM SERVER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

find_package(Threads REQUIRED)

add_library(http_core STATIC ${SERVER_SOURCES})
target_include_directories(http_core PUBLIC include)
target_link_libraries(http_core PUBLIC Threads::Threads)

add_executable(http_server src/main.cpp)
target_link_libraries(http_server PRIVATE http_core)

if(HTTP_SERVER_BUILD_TESTS)
    enable_testing()

    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
    add_executable(unit_tests ${TEST_SOURCES})
    target_include_directories(unit_tests PRIVATE tests)
    target_link_libraries(unit_tests PRIVATE http_core)

    add_test(NAME unit_tests COMMAND unit_tests)
endif()
