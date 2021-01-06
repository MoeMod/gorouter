cmake_minimum_required(VERSION 3.10.6)
project(gorouter)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED)

add_executable(gorouter main.cpp TSourceEngineQuery.cpp net_buffer.cpp)

target_link_libraries(gorouter PRIVATE Boost::boost)