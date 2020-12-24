cmake_minimum_required(VERSION 3.10.6)
project(gorouter)

set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED coroutine)

add_executable(gorouter main.cpp)

target_link_libraries(gorouter PRIVATE Boost::boost Boost::coroutine)