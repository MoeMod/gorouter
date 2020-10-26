cmake_minimum_required(VERSION 3.16)
project(gorouter)

set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED coroutine)

add_executable(gorouter main.cpp)

target_precompile_headers(gorouter PRIVATE stdafx.h)

target_link_libraries(gorouter PRIVATE Boost::boost Boost::coroutine)