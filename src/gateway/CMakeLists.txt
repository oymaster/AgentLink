# AgentLink Gateway: long-running Boost.Asio + Beast coroutine service that owns
# the async task lifecycle (create/get/stream/cancel + capability routing).

set(GATEWAY_SOURCES
    main.cpp
    gateway_config.cpp
    beast_http_client.cpp
    registry_async_client.cpp
    event_store_executor.cpp
    task_engine.cpp
    http_server.cpp
    ${CMAKE_SOURCE_DIR}/src/routing/semantic_agent_router.cpp
    ${CMAKE_SOURCE_DIR}/src/server/redis_event_store.cpp
)

add_executable(gateway_server ${GATEWAY_SOURCES})

target_include_directories(gateway_server
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(gateway_server
    PRIVATE
        a2a
        Boost::headers
        PkgConfig::HIREDIS
        CURL::libcurl
        Threads::Threads
)

install(TARGETS gateway_server DESTINATION bin)
