# AgentLink

AgentLink is a C++17 runtime for engineering-agent communication. It focuses on two practical paths:

- Local agent interoperability: connect coding agents, local scripts, IDE agents, and tool agents through MCP or A2A-style HTTP calls.
- Local-to-remote delegation: dispatch build, test, diagnosis, log analysis, and device-side tasks to specialized remote agents with discoverable capabilities and durable task events.

```text
Local Agent / MCP Client
  -> AgentLink MCP Bridge
  -> Registry / A2A-style HTTP
  -> Local or Remote Agent Runtime
  -> Redis Event Store
```

## What Is Included

- `include/a2a` and `src`: core protocol models, JSON-RPC helpers, HTTP client, task manager, runtime router, and in-memory stores.
- `examples/runtime_demo.cpp`: minimal in-process runtime flow.
- `examples/multi_agent_demo`: focused demos for Registry, Redis-backed task events, remote math agent, orchestrator, and CLI-backed role agent.
- `tools/agentlink-mcp-server`: Node/TypeScript MCP bridge that exposes AgentLink as MCP tools.
- `tests`: runtime unit test and Redis-backed remote-agent smoke test.

Older exploratory examples were intentionally removed from this clean repository.

## Dependencies

- C++17 compiler
- CMake 3.15+
- libcurl
- hiredis
- Redis, for remote-agent event persistence and integration tests
- Node.js 18+, for the MCP bridge

macOS:

```bash
brew install cmake curl hiredis redis node
```

Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev libhiredis-dev redis-server nodejs npm
```

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

If Redis is not running, the Redis-backed integration test is skipped.

## Run The Minimal Runtime Demo

```bash
./build/examples/runtime_demo
```

This validates the in-process path:

```text
AgentDescriptor -> AgentRouter -> TaskEnvelope -> TaskEvent -> MemoryEventStore
```

## Run The Remote-Agent Demo

Start Redis first:

```bash
redis-server
```

In separate terminals:

```bash
./build/examples/multi_agent_demo/registry_server 8500
./build/examples/multi_agent_demo/remote_math_agent math-1 5011 http://localhost:8500
./build/examples/multi_agent_demo/remote_orchestrator http://localhost:8500 "21 * 2"
```

The orchestrator discovers the remote agent by skill, sends an A2A-style request, and then reads the task lifecycle from Redis.

## Build The MCP Bridge

```bash
cd tools/agentlink-mcp-server
npm ci
npm run build
```

Example MCP config:

```json
{
  "mcpServers": {
    "agentlink": {
      "command": "node",
      "args": ["/absolute/path/to/AgentLink/tools/agentlink-mcp-server/dist/index.js"],
      "env": {
        "AGENTLINK_REGISTRY_URL": "http://localhost:8500",
        "AGENTLINK_MESSAGES_URL": "http://localhost:5002/v1/messages",
        "AGENTLINK_REDIS_URL": "redis://127.0.0.1:6379"
      }
    }
  }
}
```

## CLI-Backed Role Agent Demo

After building C++ targets and the MCP bridge:

```bash
AGENTLINK_BUILD_DIR="$PWD/build" bash examples/multi_agent_demo/run_cli_role_agents_demo.sh
```

By default this demo uses mock CLI commands, so it can validate the registration and MCP call path without requiring external API keys.

## License

Apache-2.0 for AgentLink project code. Third-party files keep their original license notices.
