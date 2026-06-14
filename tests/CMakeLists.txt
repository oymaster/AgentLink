add_executable(runtime_test
    runtime_test.cpp
)

target_link_libraries(runtime_test
    PRIVATE a2a
)

add_test(NAME runtime_test COMMAND runtime_test)

if(TARGET registry_server AND TARGET remote_math_agent AND TARGET remote_orchestrator)
    add_test(
        NAME remote_agent_smoke
        COMMAND
            bash
            ${CMAKE_CURRENT_SOURCE_DIR}/integration/remote_agent_smoke.sh
            $<TARGET_FILE:registry_server>
            $<TARGET_FILE:remote_math_agent>
            $<TARGET_FILE:remote_orchestrator>
    )
    set_tests_properties(remote_agent_smoke PROPERTIES
        LABELS "integration;redis;remote"
        SKIP_RETURN_CODE 77
        TIMEOUT 20
    )
endif()
