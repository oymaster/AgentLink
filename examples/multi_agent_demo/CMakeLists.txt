cmake_minimum_required(VERSION 3.10)
project(multi_agent_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

link_directories(${CMAKE_BINARY_DIR})

# Redis EventStore implementation for task trace persistence.
add_library(redis_event_store STATIC redis_event_store.cpp)
target_link_libraries(redis_event_store
    a2a
    PkgConfig::HIREDIS
)

add_executable(registry_server registry_server.cpp)
target_link_libraries(registry_server 
    a2a
    CURL::libcurl
    Threads::Threads
)

# AgentLink remote HTTP runtime demo
add_executable(remote_math_agent remote_math_agent.cpp)
target_link_libraries(remote_math_agent
    a2a
    redis_event_store
    CURL::libcurl
    PkgConfig::HIREDIS
    Threads::Threads
)

add_executable(remote_orchestrator remote_orchestrator.cpp)
target_link_libraries(remote_orchestrator
    a2a
    redis_event_store
    CURL::libcurl
    PkgConfig::HIREDIS
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
    remote_math_agent
    remote_orchestrator
    cli_agent_server
    DESTINATION bin
)

install(FILES 
    http_server.hpp
    redis_event_store.hpp
    DESTINATION include/multi_agent_demo
)
