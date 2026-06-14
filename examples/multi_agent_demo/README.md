# Multi-Agent Demo

This directory contains the focused multi-process demos:

- `registry_server`: local service discovery by skill, tag, and health status.
- `remote_math_agent`: remote A2A-style HTTP agent with Redis-backed task events.
- `remote_orchestrator`: discovers an agent and follows its task event stream.
- `cli_agent_server`: wraps a local CLI command as a registered role agent.

## Build

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON
cmake --build build
```

## Remote Agent Flow

Start Redis first:

```bash
redis-server
```

Start the registry:

```bash
./build/examples/multi_agent_demo/registry_server 8500
```

Start a remote math agent:

```bash
./build/examples/multi_agent_demo/remote_math_agent \
  math-1 5011 http://localhost:8500
```

Run the orchestrator:

```bash
./build/examples/multi_agent_demo/remote_orchestrator \
  http://localhost:8500 "21 * 2"
```

Expected output includes:

```text
selected_agent=math-1
answer=21 * 2 = 42
latest_event=task.completed
```

## CLI Role-Agent Flow

Build the MCP bridge first:

```bash
cd tools/agentlink-mcp-server
npm ci
npm run build
cd ../..
```

Run the mock role-agent demo:

```bash
AGENTLINK_BUILD_DIR="$PWD/build" bash examples/multi_agent_demo/run_cli_role_agents_demo.sh
```

The script starts two mock CLI-backed role agents and calls them through the MCP bridge. To wrap a real non-interactive CLI, replace the final `mock` argument with a command string supported by that CLI.
