cmake_minimum_required(VERSION 3.10.6)
project(gorouter)

set(CMAKE_CXX_STANDARD 20)

find_package(asio CONFIG REQUIRED)

if(MSVC)
	add_definitions(-DASIO_HAS_STD_COROUTINE -DASIO_HAS_CO_AWAIT)
endif()

add_executable(gorouter main.cpp TSourceEngineQuery.cpp net_buffer.cpp)

target_link_libraries(gorouter PRIVATE asio)