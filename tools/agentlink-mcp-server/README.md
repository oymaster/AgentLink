# AgentLink MCP Server

Track A uses this stdio MCP server as a thin bridge from Claude Code / Codex into AgentLink.

```text
Claude Code / Codex
  -> agentlink-mcp-server
  -> AgentLink Registry / /v1/messages / A2A message/send
  -> Local or remote Agent
```

## Build

```bash
cd tools/agentlink-mcp-server
npm install
npm run build
```

## Environment

```bash
export AGENTLINK_REGISTRY_URL=http://localhost:8500
export AGENTLINK_GATEWAY_URL=http://localhost:5002
export AGENTLINK_MESSAGES_URL=http://localhost:5002/v1/messages
export AGENTLINK_REDIS_URL=redis://127.0.0.1:6379
export AGENTLINK_TIMEOUT_MS=10000
```

`AGENTLINK_MESSAGES_URL` overrides `AGENTLINK_GATEWAY_URL`. `AGENTLINK_REDIS_URL`
(or the C++-style `AGENTLINK_REDIS_HOST` / `AGENTLINK_REDIS_PORT`) points the async
task tools at the shared event store.

## Tools

Synchronous:

- `agentlink_list_agents`: list Registry agents.
- `agentlink_find_agents`: find agents by `skill` or `tag`.
- `agentlink_send_message`: call AgentLink `/v1/messages`.
- `agentlink_call_agent`: find an agent by skill and call A2A `message/send`.

Async task handles (non-blocking, durable via shared Redis event log):

- `agentlink_create_task`: dispatch to a remote agent in the background, return a `task_id` immediately.
- `agentlink_get_task`: read current state + result by `task_id` (recoverable across bridge restarts).
- `agentlink_stream_task`: ordered lifecycle events, with `sinceSequence` for incremental polling.
- `agentlink_cancel_task`: best-effort cancel of an in-flight task.

## Smoke Test

Start the C++ demo:

```bash
./build/examples/multi_agent_demo/registry_server 8500
./build/examples/multi_agent_demo/remote_math_agent math-1 5011 http://localhost:8500
```

Then run:

```bash
cd tools/agentlink-mcp-server
AGENTLINK_REGISTRY_URL=http://localhost:8500 npm run smoke
AGENTLINK_REGISTRY_URL=http://localhost:8500 npm run mcp:smoke
npm run mcp:smoke:error
# async task lifecycle (needs Redis): create -> get -> stream
AGENTLINK_REGISTRY_URL=http://localhost:8500 npm run async:smoke
```

Expected output contains:

```text
"answer": "21 * 2 = 42"
```

`npm run smoke` validates the AgentLink HTTP client. `npm run mcp:smoke` starts the stdio MCP server and validates `tools/list` plus `tools/call`.

`npm run mcp:smoke:error` uses mock HTTP servers to verify structured tool errors for:

- Registry unavailable.
- Skill not found.
- Agent returning malformed JSON.

## Claude Code / Codex Config Shape

Use the built server entrypoint. A ready-to-copy template is available at `config/mcp-config.example.json`.

```json
{
  "mcpServers": {
    "agentlink": {
      "command": "node",
      "args": ["/absolute/path/to/AgentLink/tools/agentlink-mcp-server/dist/index.js"],
      "env": {
        "AGENTLINK_REGISTRY_URL": "http://localhost:8500",
        "AGENTLINK_MESSAGES_URL": "http://localhost:5002/v1/messages"
      }
    }
  }
}
```
