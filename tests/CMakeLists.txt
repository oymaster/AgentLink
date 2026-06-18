add_executable(runtime_test
    runtime_test.cpp
)

target_link_libraries(runtime_test
    PRIVATE a2a
)

add_test(NAME runtime_test COMMAND runtime_test)

if(BUILD_GATEWAY)
    add_executable(semantic_router_test
        semantic_router_test.cpp
        ${CMAKE_SOURCE_DIR}/src/routing/semantic_agent_router.cpp
    )
    target_link_libraries(semantic_router_test
        PRIVATE a2a Boost::headers CURL::libcurl Threads::Threads
    )
    add_test(NAME semantic_router_test COMMAND semantic_router_test)
    add_executable(semantic_router_eval
        semantic_router_eval.cpp
        ${CMAKE_SOURCE_DIR}/src/routing/semantic_agent_router.cpp
    )
    target_link_libraries(semantic_router_eval
        PRIVATE a2a Boost::headers CURL::libcurl Threads::Threads
    )
    target_compile_definitions(semantic_router_eval PRIVATE
        SEMANTIC_EVAL_DATA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/semantic_eval_queries.json"
    )
endif()

if(TARGET gateway_server AND TARGET registry_server AND TARGET cli_agent_server)
    add_test(
        NAME gateway_cancel_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/integration/gateway_cancel_smoke.sh
            $<TARGET_FILE:registry_server>
            $<TARGET_FILE:cli_agent_server>
            $<TARGET_FILE:gateway_server>
    )
    set_tests_properties(gateway_cancel_smoke PROPERTIES
        LABELS "integration;redis;gateway"
        SKIP_RETURN_CODE 77
        TIMEOUT 20
    )
    add_test(
        NAME gateway_concurrency_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/integration/gateway_concurrency_smoke.sh
            $<TARGET_FILE:registry_server>
            $<TARGET_FILE:cli_agent_server>
            $<TARGET_FILE:gateway_server>
    )
    set_tests_properties(gateway_concurrency_smoke PROPERTIES
        LABELS "integration;redis;gateway;concurrency"
        SKIP_RETURN_CODE 77
        TIMEOUT 60
    )
    add_test(
        NAME gateway_cancel_race_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/integration/gateway_cancel_race_smoke.sh
            $<TARGET_FILE:registry_server>
            $<TARGET_FILE:cli_agent_server>
            $<TARGET_FILE:gateway_server>
    )
    set_tests_properties(gateway_cancel_race_smoke PROPERTIES
        LABELS "integration;redis;gateway;race"
        SKIP_RETURN_CODE 77
        TIMEOUT 60
    )
    add_test(
        NAME gateway_shutdown_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/integration/gateway_shutdown_smoke.sh
            $<TARGET_FILE:registry_server>
            $<TARGET_FILE:cli_agent_server>
            $<TARGET_FILE:gateway_server>
    )
    set_tests_properties(gateway_shutdown_smoke PROPERTIES
        LABELS "integration;redis;gateway;shutdown"
        SKIP_RETURN_CODE 77
        TIMEOUT 30
    )
endif()
