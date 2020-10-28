cmake_minimum_required(VERSION 3.16)
project(gorouter)

set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED coroutine)

add_library(json INTERFACE)
target_include_directories(json INTERFACE "${CMAKE_SOURCE_DIR}/3rdparty/json")

add_executable(gorouter main.cpp)

target_precompile_headers(gorouter PRIVATE stdafx.h)

target_link_libraries(gorouter PRIVATE Boost::boost Boost::coroutine)