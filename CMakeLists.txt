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

file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(http_server ${SERVER_SOURCES})
target_include_directories(http_server PRIVATE include)

find_package(Threads REQUIRED)
target_link_libraries(http_server PRIVATE Threads::Threads)
