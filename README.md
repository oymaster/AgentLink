# AgentLink

当前版本：`v0.2.0`。

AgentLink 是一个面向 Linux 和 macOS 的 C++20 协程 Agent 控制平面。它让 Claude Code、Codex 以及其他本地或远程 Agent 通过统一运行时协作，由 Gateway 统一负责能力发现、精确/语义路由、异步任务生命周期、结构化取消和 Redis 持久化事件追踪。

**协议边界与远程支持**：Gateway 调用 Agent 走纯 HTTP + JSON-RPC，协议层不区分本地与远程 Agent，机制上 Gateway 可以跨机调用任意可达的 HTTP Agent。当前所有 demo 和集成测试都跑在 localhost；跨机部署所需的 mTLS、Agent 注册鉴权、Registry 持久化、网络抖动重试等配套是明确的后续工作，详见下方"已知边界"。

**目标落地场景**：核心 Runtime 选 C++ 而不是继续用 TypeScript，是因为目标场景是车端、机器人和家庭智能网关 —— 这类场景大多是 7×24 常驻进程、资源受限、运行环境不一定有 Node/Python runtime，C++ 在 footprint、长进程内存可控性、以及与 ROS2 / 车载中间件下游生态的对接上更直接。MCP Bridge 仍然用 TypeScript，只做协议转换，不参与运行时核心。

```text
Claude Code / Codex / MCP 客户端
              |
       TypeScript MCP Bridge       （只负责协议适配）
              |
       C++20 Agent Gateway         （唯一生命周期权威）
       /          |          \
  Registry    精确/语义路由    Redis 事件存储
       \          |          /
          本地或远程 Agent
```

MCP 是宿主到工具的边界；A2A 风格的 Agent Card 和 `message/send` 是 Gateway 到 Agent 的边界。TypeScript Bridge 不负责路由任务，不直接访问 Registry/Agent，也不写入 Redis 事件。

## 核心亮点

- 基于 C++20 协程实现 Boost.Asio/Beast HTTP 服务端和客户端。
- `POST /v1/tasks` 立即返回 HTTP 202，任务在后台继续执行。
- 使用 `asio::strand` 串行化终态提交，确保完成和取消不能同时获胜。
- 使用 Asio cancellation slot 中断执行中的 Beast 异步操作。
- hiredis 和 libcurl Embedding 等阻塞调用分别运行在独立单线程池，不阻塞 IO reactor。
- 精确 skill 路由使用 health/tag 硬过滤和最低负载选择；未指定 skill 时启用语义路由。
- Linux 使用 Asio 的 epoll 后端，macOS 使用 kqueue，业务代码无需平台分支。
- Mock 和真实 Claude Code → Codex 演示共用同一条 Gateway 链路。

## 仓库结构

- `include/a2a`、`src`：协议模型、运行时基础组件、Gateway、路由和事件存储。
- `src/gateway`：正式 Gateway、任务状态机、HTTP 服务端/客户端、Registry Client 和 Redis Executor。
- `examples/runtime_demo.cpp`：最小进程内路由与事件存储示例。
- `examples/multi_agent_demo`：集成演示使用的 Registry 和 CLI Agent 外部进程，不包含第二套 orchestrator。
- `tools/agentlink-mcp-server`：轻量 Node/TypeScript MCP→HTTP Bridge。
- `tests`：运行时、语义路由、取消、并发竞争和优雅退出测试。

## 依赖

- C++20 编译器：AppleClang 16+、GCC 13+ 或 Clang 16+
- CMake 3.15+
- Boost 1.81+
- libcurl、hiredis、Redis
- Node.js 18+，用于构建 MCP Bridge
- 可选：Claude Code 和 Codex CLI，用于真实 Agent 演示

macOS：

```bash
brew install cmake boost curl hiredis redis node
brew services start redis
```

Ubuntu 24.04：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libboost-all-dev \
  libcurl4-openssl-dev libhiredis-dev redis-server nodejs npm pkg-config
sudo service redis-server start
```

## 五分钟快速开始

```bash
git clone <repository-url> AgentLink
cd AgentLink

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=ON \
  -DBUILD_TESTS=ON \
  -DBUILD_GATEWAY=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

cd tools/agentlink-mcp-server
npm ci
npm run build
cd ../..

AGENTLINK_BUILD_DIR="$PWD/build" \
  bash examples/multi_agent_demo/run_local_claude_codex_agents.sh
```

Redis 不可用时，依赖 Redis 的 CTest 会以代码 77 标记为跳过。正式验收必须启动 Redis，不能把 `Skipped` 当成通过。

## HTTP API

```text
GET  /healthz
POST /v1/tasks
GET  /v1/tasks/{id}
GET  /v1/tasks/{id}/events?since=N
POST /v1/tasks/{id}/cancel
GET  /v1/agents
POST /v1/agents/find
POST /v1/call
POST /v1/messages                  兼容占位：HTTP 501
```

创建任务示例：

```bash
curl -X POST http://localhost:5002/v1/tasks \
  -H 'Content-Type: application/json' \
  -d '{"message":"分析这个并发问题","skill":"architecture","tag":"local"}'
```

省略 `skill` 即启用语义路由。明确指定 skill 的请求不会回退到语义选择。

## MCP 工具

Bridge 保留现有 8 个工具名称：

```text
agentlink_list_agents
agentlink_find_agents
agentlink_send_message
agentlink_call_agent
agentlink_create_task
agentlink_get_task
agentlink_stream_task
agentlink_cancel_task
```

`agentlink_send_message` 是本版本明确保留的兼容占位，会把 Gateway HTTP 501 映射为稳定的 MCP `not_implemented` 错误。受支持的流程应使用 `agentlink_call_agent` 或异步任务工具。

MCP 配置示例：

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

## Claude Code → Codex 演示

默认模式为确定性 Mock，不需要外部模型凭据：

```bash
AGENTLINK_BUILD_DIR="$PWD/build" \
  bash examples/multi_agent_demo/run_local_claude_codex_agents.sh
```

真实 CLI 模式只执行 architecture→coding 两阶段异步 handoff，并输出等待进度和耗时：

```bash
AGENTLINK_USE_REAL_CLI=1 \
AGENTLINK_BUILD_DIR="$PWD/build" \
  bash examples/multi_agent_demo/run_local_claude_codex_agents.sh
```

真实 Codex 默认使用只读沙箱，避免演示修改当前工作区。只有在专门用于修改的独立 worktree 中，才应覆盖 `AGENTLINK_CODEX_COMMAND` 开启写权限。

## 验证矩阵

| 验证目标 | 命令 | 期望结果 |
| --- | --- | --- |
| 运行时模型、路由和存储 | `ctest --test-dir build -R '^runtime_test$' --output-on-failure` | 通过 |
| 语义路由不变量 | `ctest --test-dir build -R '^semantic_router_test$' --output-on-failure` | 通过 |
| 语义质量报告 | `./build/tests/semantic_router_eval` | Fake 基线；可选 DashScope 结果 |
| 取消慢任务 | `ctest --test-dir build -R '^gateway_cancel_smoke$' --output-on-failure` | 恰好一个 failed 终态事件 |
| 取消与完成边界竞争 | `ctest --test-dir build -R '^gateway_cancel_race_smoke$' --output-on-failure` | 不出现重复终态 |
| 100 个并发任务 | `ctest --test-dir build -R '^gateway_concurrency_smoke$' --output-on-failure` | 100 个任务完成，50 次 health 检查成功 |
| 运行中优雅退出 | `ctest --test-dir build -R '^gateway_shutdown_smoke$' --output-on-failure` | 进程退出前任务进入终态 |
| MCP 构建和错误契约 | `cd tools/agentlink-mcp-server && npm run build && npm run mcp:smoke:error` | 通过 |
| Mock Claude→Codex handoff | `AGENTLINK_BUILD_DIR="$PWD/build" bash examples/multi_agent_demo/run_local_claude_codex_agents.sh` | 两阶段完成 |
| 真实 Claude→Codex handoff | 上一条命令增加 `AGENTLINK_USE_REAL_CLI=1` | 返回真实架构和编码响应 |

## 已知边界

- **跨机部署相关的配套尚未实现**：Agent 和 Registry 之间使用明文 HTTP，Registry 不校验注册 token，Gateway 调用 Agent 没有针对网络抖动的幂等重试，Registry 自身是单进程内存存储且重启不持久化。协议机制支持远程 Agent，但跨机投产需要的 mTLS / 鉴权 / 持久化 / 重试都属于后续优化。
- DashScope 的 HTTPS 由 libcurl 处理，与上一条无关。
- Gateway 重启后不恢复运行中的任务，但历史 Redis 事件仍然可读。
- Redis task/trace 双列表写入不是事务性的；sequence 正确性依赖单 Gateway 写入者和单 Redis worker。
- Gateway 取消会中断正在等待的 HTTP 操作并提交唯一终态，但远程 Agent 若不支持协作式取消，可能继续执行；当前 CLI adapter 不会终止已启动的 Claude/Codex 子进程。
- `stream_task` 当前读取每个任务的小型事件列表，再在内存中按 sequence 过滤。
- 语义路由使用内存线性扫描，这是针对小规模 Agent 目录的主动选择。

详细取舍见 [Gateway 关键决策](docs/hard_decisions.md)，最近一次本地证据见[验证结果](docs/verification_results.md)。

## 许可证

AgentLink 项目代码采用 Apache-2.0；第三方文件保留各自原始许可证声明。
