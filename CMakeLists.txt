cmake_minimum_required(VERSION 3.10.6)
project(gorouter)

set(CMAKE_CXX_STANDARD 20)

find_package(asio CONFIG REQUIRED)
add_definitions(-DASIO_HAS_STD_COROUTINE -DASIO_HAS_CO_AWAIT)

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    add_compile_options(-fcoroutines)
endif()

add_executable(gorouter main.cpp TSourceEngineQuery.cpp net_buffer.cpp)
set_target_properties(gorouter PROPERTIES OUTPUT_NAME hlds)
target_link_libraries(gorouter PRIVATE asio)