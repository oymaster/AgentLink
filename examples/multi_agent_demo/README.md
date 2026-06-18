# 多 Agent 演示

本目录包含用于验证正式 Gateway 的外部进程：

- `registry_server`：按 skill、tag 和健康状态提供本地服务发现。
- `cli_agent_server`：把本地 CLI 命令包装成已注册的角色 Agent。

任务路由、调度、取消和生命周期事件写入只在 `src/gateway/task_engine.cpp` 中实现。示例目录不包含另一套 orchestrator。

## 构建

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON
cmake --build build
```

## CLI 角色 Agent 流程

先构建 MCP Bridge：

```bash
cd tools/agentlink-mcp-server
npm ci
npm run build
cd ../..
```

运行 Mock 角色 Agent 演示：

```bash
AGENTLINK_BUILD_DIR="$PWD/build" bash examples/multi_agent_demo/run_cli_role_agents_demo.sh
```

脚本会启动两个 Mock CLI 角色 Agent，并通过 MCP Bridge 调用它们。若要包装真实非交互 CLI，可以把最后的 `mock` 替换成该 CLI 支持的命令字符串。

## 本地 Claude Code + Codex Agent

下面的演示会把 Claude Code 注册为 architecture Agent，把 Codex 注册为 coding Agent：

```bash
AGENTLINK_BUILD_DIR="$PWD/build" bash examples/multi_agent_demo/run_local_claude_codex_agents.sh
```

默认使用 Mock 命令，验证 Registry 注册、Gateway 路由、同步 MCP 调用和异步 architecture→coding 事件流程。使用真实本地 CLI：

```bash
AGENTLINK_USE_REAL_CLI=1 \
AGENTLINK_BUILD_DIR="$PWD/build" \
  bash examples/multi_agent_demo/run_local_claude_codex_agents.sh
```

真实模式只执行两阶段异步 handoff，并定期输出等待状态和耗时。Codex 默认使用只读沙箱。

如果需要保持进程运行，让 MCP 客户端重复调用：

```bash
AGENTLINK_HOLD=1 AGENTLINK_BUILD_DIR="$PWD/build" \
  bash examples/multi_agent_demo/run_local_claude_codex_agents.sh
```

随后可以使用 `agentlink_call_agent`：`skill=architecture` 选择 `claude-architect`，`skill=coding` 选择 `codex-coder`。
