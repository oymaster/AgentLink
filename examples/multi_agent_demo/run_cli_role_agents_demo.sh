#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${AGENTLINK_BUILD_DIR:-$ROOT_DIR/build}"
MCP_DIR="$ROOT_DIR/tools/agentlink-mcp-server"
BASE_PORT="${AGENTLINK_CLI_DEMO_BASE_PORT:-28650}"
REGISTRY_PORT="$BASE_PORT"
CODEX_PORT="$((BASE_PORT + 1))"
CLAUDE_PORT="$((BASE_PORT + 2))"
WORK_DIR="${TMPDIR:-/tmp}/agentlink-cli-role-demo-$$"
REGISTRY_LOG="$WORK_DIR/registry.log"
CODEX_LOG="$WORK_DIR/codex-agent.log"
CLAUDE_LOG="$WORK_DIR/claude-agent.log"

mkdir -p "$WORK_DIR"

REGISTRY_PID=""
CODEX_PID=""
CLAUDE_PID=""

cleanup() {
  for pid in "$CLAUDE_PID" "$CODEX_PID" "$REGISTRY_PID"; do
    if [ -n "$pid" ] && kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
      wait "$pid" >/dev/null 2>&1 || true
    fi
  done
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT INT TERM

fail() {
  echo "cli_role_agents_demo failed: $*" >&2
  echo "--- registry.log ---" >&2
  [ -f "$REGISTRY_LOG" ] && cat "$REGISTRY_LOG" >&2
  echo "--- codex-agent.log ---" >&2
  [ -f "$CODEX_LOG" ] && cat "$CODEX_LOG" >&2
  echo "--- claude-agent.log ---" >&2
  [ -f "$CLAUDE_LOG" ] && cat "$CLAUDE_LOG" >&2
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
[ -f "$MCP_DIR/dist/mcpSmoke.js" ] || fail "MCP server dist missing, run: cd $MCP_DIR && npm run build"

"$BUILD_DIR/examples/multi_agent_demo/registry_server" "$REGISTRY_PORT" >"$REGISTRY_LOG" 2>&1 &
REGISTRY_PID="$!"
wait_for_log "HTTP Server listening" "$REGISTRY_LOG" 5 || fail "registry did not start"

"$BUILD_DIR/examples/multi_agent_demo/cli_agent_server" \
  codex-architect "$CODEX_PORT" "http://localhost:$REGISTRY_PORT" \
  architecture "你是 Codex 架构师 agent，只负责拆解方案、风险和接口边界。" \
  mock >"$CODEX_LOG" 2>&1 &
CODEX_PID="$!"
wait_for_log "codex-architect listening" "$CODEX_LOG" 5 || fail "codex role agent did not start"

"$BUILD_DIR/examples/multi_agent_demo/cli_agent_server" \
  claude-implementer "$CLAUDE_PORT" "http://localhost:$REGISTRY_PORT" \
  coding "你是 Claude Code 实现 agent，只负责根据方案写代码、指出改动文件和验证命令。" \
  mock >"$CLAUDE_LOG" 2>&1 &
CLAUDE_PID="$!"
wait_for_log "claude-implementer listening" "$CLAUDE_LOG" 5 || fail "claude role agent did not start"

sleep 0.2

echo "== registered agents =="
curl --noproxy "*" -s "http://localhost:$REGISTRY_PORT/v1/agents"
echo

echo "== call codex-architect via MCP =="
(
  cd "$MCP_DIR" || exit 1
  AGENTLINK_REGISTRY_URL="http://localhost:$REGISTRY_PORT" \
  AGENTLINK_SMOKE_SKILL=architecture \
  AGENTLINK_SMOKE_MESSAGE="请为 AgentLink 增加 CLI agent adapter 设计一个最小方案。" \
    node dist/mcpSmoke.js
) || fail "MCP call to codex-architect failed"

echo "== call claude-implementer via MCP =="
(
  cd "$MCP_DIR" || exit 1
  AGENTLINK_REGISTRY_URL="http://localhost:$REGISTRY_PORT" \
  AGENTLINK_SMOKE_SKILL=coding \
  AGENTLINK_SMOKE_MESSAGE="请根据架构方案实现 CLI agent adapter，并列出验证命令。" \
    node dist/mcpSmoke.js
) || fail "MCP call to claude-implementer failed"

echo "cli_role_agents_demo passed"
