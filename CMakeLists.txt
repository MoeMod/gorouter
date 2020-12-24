cmake_minimum_required(VERSION 3.10.6)
project(gorouter)

set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED coroutine)

find_package(nlohmann_json CONFIG REQUIRED)

add_executable(gorouter main.cpp)

install(FILES serverlist.json DESTINATION .)
install(TARGETS gorouter DESTINATION .)

target_link_libraries(gorouter PRIVATE Boost::boost Boost::coroutine)
target_link_libraries(gorouter PRIVATE nlohmann_json nlohmann_json::nlohmann_json)