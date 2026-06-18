# AgentLink MCP Server

本 stdio MCP Server 是 Claude Code/Codex 接入 AgentLink 的轻量 Bridge。

```text
Claude Code / Codex
  → agentlink-mcp-server
  → AgentLink C++ Gateway
  → 本地或远程 Agent
```

Bridge 只负责 MCP→HTTP 协议翻译，不直接连接 Registry、Agent 或 Redis。

## 构建

```bash
cd tools/agentlink-mcp-server
npm install
npm run build
```

## 环境变量

```bash
export AGENTLINK_GATEWAY_URL=http://localhost:5002
export AGENTLINK_MESSAGES_URL=http://localhost:5002/v1/messages
export AGENTLINK_TIMEOUT_MS=10000
export AGENTLINK_CALL_TIMEOUT_MS=600000
```

`AGENTLINK_MESSAGES_URL` 可以覆盖 Gateway `/v1/messages` 地址。该接口在当前版本是返回 501 的兼容占位。

## 工具列表

同步工具：

- `agentlink_list_agents`：列出 Registry 中的 Agent。
- `agentlink_find_agents`：按 `skill` 或 `tag` 查找 Agent。
- `agentlink_send_message`：兼容占位，当前返回 `not_implemented`。
- `agentlink_call_agent`：按 skill 选择 Agent，并调用 A2A `message/send`。

异步任务工具：

- `agentlink_create_task`：后台调度任务并立即返回 `task_id`。
- `agentlink_get_task`：按 `task_id` 获取当前状态和结果。
- `agentlink_stream_task`：返回有序生命周期事件，支持 `sinceSequence` 增量读取。
- `agentlink_cancel_task`：尽力取消执行中的任务；对终态任务为 no-op。

任务状态存储在共享 Redis 事件日志中，因此 MCP Bridge 重启后仍可读取历史任务。

## Smoke 测试

构建 C++ 目标、启动 Redis，然后运行完整 Mock 演示：

```bash
AGENTLINK_BUILD_DIR="$PWD/build" bash examples/multi_agent_demo/run_local_claude_codex_agents.sh
```

Gateway 已经运行时，可以分别执行：

```bash
cd tools/agentlink-mcp-server
AGENTLINK_GATEWAY_URL=http://localhost:5002 npm run smoke
AGENTLINK_GATEWAY_URL=http://localhost:5002 npm run mcp:smoke
npm run mcp:smoke:error
# 异步任务生命周期（需要 Redis）：create → get → stream
AGENTLINK_GATEWAY_URL=http://localhost:5002 npm run async:smoke
```

期望输出包含：

```text
"answer": "21 * 2 = 42"
```

`npm run smoke` 验证 AgentLink HTTP Client；`npm run mcp:smoke` 启动 stdio MCP Server，验证 `tools/list` 和 `tools/call`。

`npm run mcp:smoke:error` 使用 Mock HTTP Server 验证以下结构化错误：

- Registry 不可用；
- skill 不存在；
- Agent 返回非法 JSON；
- `/v1/messages` 兼容占位返回 `not_implemented`。

## Claude Code/Codex 配置

使用已经构建的 Server 入口。可直接复制的模板位于 `config/mcp-config.example.json`：

```json
{
  "mcpServers": {
    "agentlink": {
      "command": "node",
      "args": ["/absolute/path/to/AgentLink/tools/agentlink-mcp-server/dist/index.js"],
      "env": {
        "AGENTLINK_GATEWAY_URL": "http://localhost:5002"
      }
    }
  }
}
```
