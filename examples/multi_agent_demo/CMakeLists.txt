cmake_minimum_required(VERSION 3.10)
project(multi_agent_demo)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

link_directories(${CMAKE_BINARY_DIR})

add_executable(registry_server registry_server.cpp)
target_link_libraries(registry_server 
    a2a
    CURL::libcurl
    Threads::Threads
)

# Local CLI-backed role agent prototype.
add_executable(cli_agent_server cli_agent_server.cpp)
target_link_libraries(cli_agent_server
    a2a
    CURL::libcurl
    Threads::Threads
)

install(TARGETS 
    registry_server
    cli_agent_server
    DESTINATION bin
)

install(FILES 
    http_server.hpp
    DESTINATION include/multi_agent_demo
)
