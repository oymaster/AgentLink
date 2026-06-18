#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${AGENTLINK_BUILD_DIR:-$ROOT_DIR/build}"
MCP_DIR="$ROOT_DIR/tools/agentlink-mcp-server"
BASE_PORT="${AGENTLINK_LOCAL_DEMO_BASE_PORT:-8666}"
REGISTRY_PORT="$BASE_PORT"
CLAUDE_PORT="$((BASE_PORT + 1))"
CODEX_PORT="$((BASE_PORT + 2))"
GATEWAY_PORT="${AGENTLINK_GATEWAY_PORT:-5002}"
USE_REAL_CLI="${AGENTLINK_USE_REAL_CLI:-0}"
HOLD="${AGENTLINK_HOLD:-0}"
WORK_DIR="${TMPDIR:-/tmp}/agentlink-local-claude-codex-$$"
REGISTRY_LOG="$WORK_DIR/registry.log"
CLAUDE_LOG="$WORK_DIR/claude-architect.log"
CODEX_LOG="$WORK_DIR/codex-coder.log"
GATEWAY_LOG="$WORK_DIR/gateway.log"

mkdir -p "$WORK_DIR"

REGISTRY_PID=""
CLAUDE_PID=""
CODEX_PID=""
GATEWAY_PID=""

cleanup() {
  for pid in "$GATEWAY_PID" "$CODEX_PID" "$CLAUDE_PID" "$REGISTRY_PID"; do
    if [ -n "$pid" ] && kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
      wait "$pid" >/dev/null 2>&1 || true
    fi
  done
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT INT TERM

fail() {
  echo "local_claude_codex_agents failed: $*" >&2
  echo "--- registry.log ---" >&2
  [ -f "$REGISTRY_LOG" ] && cat "$REGISTRY_LOG" >&2
  echo "--- claude-architect.log ---" >&2
  [ -f "$CLAUDE_LOG" ] && cat "$CLAUDE_LOG" >&2
  echo "--- codex-coder.log ---" >&2
  [ -f "$CODEX_LOG" ] && cat "$CODEX_LOG" >&2
  echo "--- gateway.log ---" >&2
  [ -f "$GATEWAY_LOG" ] && cat "$GATEWAY_LOG" >&2
  exit 1
}

wait_for_log() {
  pattern="$1"
  file="$2"
  timeout_seconds="$3"
  start_seconds="$(date +%s)"

  while true; do
    if [ -f "$file" ] && grep -q "$pattern" "$file"; then
      return 0
    fi

    if [ $(( $(date +%s) - start_seconds )) -ge "$timeout_seconds" ]; then
      return 1
    fi

    sleep 0.1
  done
}

[ -x "$BUILD_DIR/examples/multi_agent_demo/registry_server" ] || fail "registry_server not found, run: cmake --build $BUILD_DIR"
[ -x "$BUILD_DIR/examples/multi_agent_demo/cli_agent_server" ] || fail "cli_agent_server not found, run: cmake --build $BUILD_DIR"
[ -x "$BUILD_DIR/src/gateway/gateway_server" ] || fail "gateway_server not found, run: cmake --build $BUILD_DIR"
[ -f "$MCP_DIR/dist/mcpSmoke.js" ] || fail "MCP server dist missing, run: cd $MCP_DIR && npm run build"
command -v redis-cli >/dev/null 2>&1 || fail "redis-cli not found"
redis-cli ping 2>/dev/null | grep -q PONG || fail "Redis is required and did not answer PONG"

if [ "$USE_REAL_CLI" = "1" ]; then
  command -v claude >/dev/null 2>&1 || fail "claude CLI not found"
  command -v codex >/dev/null 2>&1 || fail "codex CLI not found"
  CLAUDE_COMMAND="${AGENTLINK_CLAUDE_COMMAND:-claude -p --permission-mode dontAsk --output-format text --no-session-persistence --strict-mcp-config --tools \"\"}"
  CODEX_COMMAND="${AGENTLINK_CODEX_COMMAND:-codex exec -C \"$ROOT_DIR\" --sandbox read-only --skip-git-repo-check --ephemeral --ignore-user-config --ignore-rules -}"
else
  CLAUDE_COMMAND="mock"
  CODEX_COMMAND="mock"
fi

"$BUILD_DIR/examples/multi_agent_demo/registry_server" "$REGISTRY_PORT" >"$REGISTRY_LOG" 2>&1 &
REGISTRY_PID="$!"
wait_for_log "HTTP Server listening" "$REGISTRY_LOG" 5 || fail "registry did not start"

"$BUILD_DIR/examples/multi_agent_demo/cli_agent_server" \
  claude-architect "$CLAUDE_PORT" "http://localhost:$REGISTRY_PORT" \
  architecture "你是 Claude Code 架构师 agent。只负责需求澄清、架构拆解、接口边界、风险和验证策略，不直接改代码。" \
  "$CLAUDE_COMMAND" >"$CLAUDE_LOG" 2>&1 &
CLAUDE_PID="$!"
wait_for_log "claude-architect listening" "$CLAUDE_LOG" 10 || fail "claude architect agent did not start"

"$BUILD_DIR/examples/multi_agent_demo/cli_agent_server" \
  codex-coder "$CODEX_PORT" "http://localhost:$REGISTRY_PORT" \
  coding "你是 Codex 编程 agent。根据明确方案进行最小代码修改，并返回改动文件、关键实现和验证命令。" \
  "$CODEX_COMMAND" >"$CODEX_LOG" 2>&1 &
CODEX_PID="$!"
wait_for_log "codex-coder listening" "$CODEX_LOG" 10 || fail "codex coder agent did not start"

AGENTLINK_GATEWAY_PORT="$GATEWAY_PORT" \
AGENTLINK_REGISTRY_URL="http://localhost:$REGISTRY_PORT" \
AGENTLINK_AGENT_TIMEOUT_MS=600000 \
  "$BUILD_DIR/src/gateway/gateway_server" >"$GATEWAY_LOG" 2>&1 &
GATEWAY_PID="$!"
wait_for_log "Gateway listening" "$GATEWAY_LOG" 10 || fail "gateway did not start"

sleep 0.2

echo "== registered agents =="
curl --noproxy "*" -s "http://localhost:$REGISTRY_PORT/v1/agents"
echo

if [ "$USE_REAL_CLI" != "1" ]; then
  echo "== call claude-architect via MCP =="
  (
    cd "$MCP_DIR" || exit 1
    AGENTLINK_GATEWAY_URL="http://localhost:$GATEWAY_PORT" \
    AGENTLINK_SMOKE_SKILL=architecture \
    AGENTLINK_SMOKE_MESSAGE="请为 AgentLink 本地 CLI agent 协作设计一条最小闭环。" \
      node dist/mcpSmoke.js
  ) || fail "MCP call to claude-architect failed"

  echo "== call codex-coder via MCP =="
  (
    cd "$MCP_DIR" || exit 1
    AGENTLINK_GATEWAY_URL="http://localhost:$GATEWAY_PORT" \
    AGENTLINK_SMOKE_SKILL=coding \
    AGENTLINK_SMOKE_MESSAGE="请基于架构方案列出最小实现步骤和验证命令。" \
      node dist/mcpSmoke.js
  ) || fail "MCP call to codex-coder failed"
else
  echo "== real CLI mode: skipping duplicate synchronous calls =="
fi

echo "== async architecture -> coding handoff =="
(
  cd "$MCP_DIR" || exit 1
  AGENTLINK_GATEWAY_URL="http://localhost:$GATEWAY_PORT" \
    node dist/mcpAsyncDemo.js
) || fail "async architecture-to-coding handoff failed"

echo "local_claude_codex_agents passed"
echo "gateway_url=http://localhost:$GATEWAY_PORT"
echo "registry_url=http://localhost:$REGISTRY_PORT"
echo "claude_architect=http://localhost:$CLAUDE_PORT skill=architecture"
echo "codex_coder=http://localhost:$CODEX_PORT skill=coding"

if [ "$HOLD" = "1" ]; then
  echo "holding processes; press Ctrl-C to stop"
  while true; do
    sleep 3600
  done
fi
