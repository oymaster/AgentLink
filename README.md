# AgentLink

基于 C++20 Coroutine 的 AI Agent 协作中间件，让 Claude Code、Codex 等本地 Agent 通过统一 Gateway 协作完成多阶段任务。

Gateway 作为唯一生命周期权威，负责 Agent 注册发现、任务路由分发、状态管理及事件追踪；MCP Bridge 让现有 MCP 客户端（如 Claude Code）以工具调用的方式接入。

## 架构

```text
Claude Code / Codex / MCP 客户端
              │
       TypeScript MCP Bridge        （协议适配，不参与运行时）
              │
       C++20 Agent Gateway          （唯一生命周期权威）
       /          |          \
  Registry    精确/语义路由    Redis 事件存储
       \          |          /
          本地或远程 Agent
```

Gateway 调用 Agent 走纯 HTTP + JSON-RPC，协议层不区分本地与远程 Agent。

## 快速开始

### 依赖

- C++20 编译器（AppleClang 16+ / GCC 13+ / Clang 16+）
- CMake 3.15+、Boost 1.81+、libcurl、hiredis、Redis
- Node.js 18+（构建 MCP Bridge）

**macOS**

```bash
brew install cmake boost curl hiredis redis node
brew services start redis
```

**Ubuntu 24.04**

```bash
sudo apt-get install -y build-essential cmake libboost-all-dev \
  libcurl4-openssl-dev libhiredis-dev redis-server nodejs npm pkg-config
sudo service redis-server start
```

### 构建与运行

```bash
git clone <repository-url> AgentLink
cd AgentLink

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON -DBUILD_GATEWAY=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

# Claude Code → Codex 协作演示（默认确定性 Mock，无需外部凭据）
AGENTLINK_BUILD_DIR="$PWD/build" \
  bash examples/multi_agent_demo/run_local_claude_codex_agents.sh
```

## HTTP API

```text
GET  /healthz
POST /v1/tasks                  创建任务，立即返回 202
GET  /v1/tasks/{id}             查询任务状态
GET  /v1/tasks/{id}/events      增量事件流（?since=N）
POST /v1/tasks/{id}/cancel      取消任务
GET  /v1/agents                 列出已注册 Agent
POST /v1/agents/find            按能力查找 Agent
POST /v1/call                   同步调用 Agent
```

```bash
curl -X POST http://localhost:5002/v1/tasks \
  -H 'Content-Type: application/json' \
  -d '{"message":"分析这个并发问题","skill":"architecture","tag":"local"}'
```

省略 `skill` 即启用语义路由；指定 `skill` 不会回退到语义选择。

## 仓库结构

| 路径 | 说明 |
| --- | --- |
| `include/a2a`、`src` | 协议模型、运行时组件、Gateway、路由与事件存储 |
| `src/gateway` | Gateway、任务状态机、HTTP 服务端/客户端、Registry Client、Redis Executor |
| `tools/agentlink-mcp-server` | 轻量 Node/TypeScript MCP→HTTP Bridge |
| `examples` | 进程内路由示例与多 Agent 集成演示 |
| `tests` | 运行时、语义路由、取消竞争与优雅退出测试 |

## 许可证

Apache-2.0
