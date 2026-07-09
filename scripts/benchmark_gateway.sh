#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${AGENTLINK_BUILD_DIR:-$ROOT_DIR/build}"
REGISTRY_BIN="${REGISTRY_BIN:-$BUILD_DIR/examples/multi_agent_demo/registry_server}"
AGENT_BIN="${AGENT_BIN:-$BUILD_DIR/examples/multi_agent_demo/cli_agent_server}"
GATEWAY_BIN="${GATEWAY_BIN:-$BUILD_DIR/src/gateway/gateway_server}"
REQUESTS="${REQUESTS:-1000}"
CONCURRENCY="${CONCURRENCY:-100}"
AGENT_COUNT="${AGENT_COUNT:-4}"
CANCEL_RATIO="${CANCEL_RATIO:-0}"
AGENT_MOCK_DELAY_MS="${AGENT_MOCK_DELAY_MS:-0}"
MIXED_SKILLS="${MIXED_SKILLS:-0}"
BASE_PORT="${BASE_PORT:-$((32000 + ($$ % 1000) * 10))}"
REGISTRY_PORT="$BASE_PORT"
GATEWAY_PORT="$((BASE_PORT + 1))"
BENCHMARK_OUT_DIR="${BENCHMARK_OUT_DIR:-}"
if [ -n "$BENCHMARK_OUT_DIR" ]; then
  WORK_DIR="$BENCHMARK_OUT_DIR"
  KEEP_WORK_DIR=1
else
  WORK_DIR="${TMPDIR:-/tmp}/agentlink-benchmark-$$"
  KEEP_WORK_DIR=0
fi

mkdir -p "$WORK_DIR"

PIDS=""
cleanup() {
  for pid in $PIDS; do
    kill "$pid" >/dev/null 2>&1 || true
    wait "$pid" >/dev/null 2>&1 || true
  done
  if [ "$KEEP_WORK_DIR" -eq 0 ]; then
    rm -rf "$WORK_DIR"
  fi
}
trap cleanup EXIT INT TERM

require_file() {
  if [ ! -x "$1" ]; then
    echo "missing executable: $1" >&2
    exit 2
  fi
}

require_file "$REGISTRY_BIN"
require_file "$AGENT_BIN"
require_file "$GATEWAY_BIN"

if ! redis-cli ping 2>/dev/null | grep -q PONG; then
  echo "redis is required for AgentLink Gateway benchmarks" >&2
  exit 77
fi

redis-cli INFO >"$WORK_DIR/redis-before.info" 2>/dev/null || true

"$REGISTRY_BIN" "$REGISTRY_PORT" >"$WORK_DIR/registry.log" 2>&1 &
PIDS="$! $PIDS"
sleep 0.3

for i in $(seq 1 "$AGENT_COUNT"); do
  port="$((BASE_PORT + 1 + i))"
  skill="benchmark"
  if [ "$MIXED_SKILLS" = "1" ]; then
    skill="benchmark-$i"
  fi
  AGENTLINK_CLI_MOCK_DELAY_MS="$AGENT_MOCK_DELAY_MS" \
    "$AGENT_BIN" "benchmark-agent-$i" "$port" "http://127.0.0.1:$REGISTRY_PORT" \
    "$skill" "benchmark mock agent $i" mock >"$WORK_DIR/agent-$i.log" 2>&1 &
  PIDS="$! $PIDS"
done
sleep 0.8

AGENTLINK_GATEWAY_PORT="$GATEWAY_PORT" \
AGENTLINK_REGISTRY_URL="http://127.0.0.1:$REGISTRY_PORT" \
AGENTLINK_IO_THREADS="${AGENTLINK_IO_THREADS:-4}" \
"$GATEWAY_BIN" >"$WORK_DIR/gateway.log" 2>&1 &
GATEWAY_PID="$!"
PIDS="$! $PIDS"

(
  echo "timestamp,pid,cpu_pct,rss_kb,vsz_kb"
  while kill -0 "$GATEWAY_PID" >/dev/null 2>&1; do
    ps_line="$(ps -p "$GATEWAY_PID" -o pid=,pcpu=,rss=,vsz= 2>/dev/null || true)"
    if [ -n "$ps_line" ]; then
      read -r pid cpu rss vsz <<<"$ps_line"
      echo "$(date -Iseconds),$pid,$cpu,$rss,$vsz"
    fi
    sleep 1
  done
) >"$WORK_DIR/gateway-ps.csv" &
PIDS="$! $PIDS"

for _ in $(seq 1 100); do
  if curl --noproxy "*" -sf "http://127.0.0.1:$GATEWAY_PORT/healthz" >/dev/null; then
    break
  fi
  sleep 0.1
done

set +e
PY_ARGS=(
  --gateway-url "http://127.0.0.1:$GATEWAY_PORT"
  --requests "$REQUESTS"
  --concurrency "$CONCURRENCY"
  --cancel-ratio "$CANCEL_RATIO"
)
if [ "$MIXED_SKILLS" = "1" ]; then
  skills_csv=""
  for i in $(seq 1 "$AGENT_COUNT"); do
    if [ -n "$skills_csv" ]; then skills_csv="$skills_csv,"; fi
    skills_csv="${skills_csv}benchmark-$i"
  done
  PY_ARGS+=(--skills "$skills_csv")
fi

python3 "$ROOT_DIR/scripts/benchmark_gateway.py" \
  "${PY_ARGS[@]}" \
  | tee "$WORK_DIR/result.json"
BENCH_STATUS="${PIPESTATUS[0]}"
set -e

redis-cli INFO >"$WORK_DIR/redis-after.info" 2>/dev/null || true
echo "benchmark_output_dir=$WORK_DIR" >&2
exit "$BENCH_STATUS"
