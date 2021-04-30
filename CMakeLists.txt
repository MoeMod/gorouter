cmake_minimum_required(VERSION 3.10.6)
project(gorouter)

set(CMAKE_CXX_STANDARD 20)

find_package(asio CONFIG REQUIRED)
add_definitions(-DASIO_HAS_STD_COROUTINE -DASIO_HAS_CO_AWAIT)

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    add_compile_options(-fcoroutines)
endif()

if(MSVC)
    add_compile_options("/utf-8")
endif()

option(ENABLE_STEAM_SUPPORT "SteamAPI support" OFF)

if(ENABLE_STEAM_SUPPORT)
    add_library(steam_api INTERFACE)
    target_include_directories(steam_api INTERFACE "${CMAKE_SOURCE_DIR}/steam/public")
    link_directories(BEFORE "${CMAKE_SOURCE_DIR}/steam/lib/")
    target_link_libraries(steam_api INTERFACE steam_api.lib)
endif()


add_executable(gorouter main.cpp TSourceEngineQuery.cpp net_buffer.cpp munge.cpp)
set_target_properties(gorouter PROPERTIES OUTPUT_NAME hlds)
target_link_libraries(gorouter PUBLIC asio)
if(ENABLE_STEAM_SUPPORT)
    target_compile_definitions(gorouter PUBLIC -DENABLE_STEAM_SUPPORT=1)
    target_link_libraries(gorouter PRIVATE steam_api)
endif()