# AgentLink Runtime Demo
add_executable(runtime_demo
    runtime_demo.cpp
)

target_link_libraries(runtime_demo
    PRIVATE a2a
)

# Multi-Agent Demo
add_subdirectory(multi_agent_demo)

# Installation
install(TARGETS runtime_demo
    RUNTIME DESTINATION bin/examples
)
