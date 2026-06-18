#!/usr/bin/env bash
set -u

REGISTRY="$1"
AGENT="$2"
GATEWAY="$3"
BASE_PORT="$((32000 + ($$ % 1000) * 3))"
REGISTRY_PORT="$BASE_PORT"
AGENT_PORT="$((BASE_PORT + 1))"
GATEWAY_PORT="$((BASE_PORT + 2))"
WORK_DIR="${TMPDIR:-/tmp}/agentlink-gateway-shutdown-$$"
mkdir -p "$WORK_DIR"

PIDS=""
cleanup() {
  for pid in $PIDS; do
    kill "$pid" >/dev/null 2>&1 || true
    wait "$pid" >/dev/null 2>&1 || true
  done
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT INT TERM

redis-cli ping 2>/dev/null | grep -q PONG || exit 77
"$REGISTRY" "$REGISTRY_PORT" >"$WORK_DIR/registry.log" 2>&1 & PIDS="$! $PIDS"
sleep 0.3
AGENTLINK_CLI_MOCK_DELAY_MS=3000 "$AGENT" shutdown-agent "$AGENT_PORT" \
  "http://127.0.0.1:$REGISTRY_PORT" shutdown "shutdown test agent" mock \
  >"$WORK_DIR/agent.log" 2>&1 & PIDS="$! $PIDS"
sleep 0.8
AGENTLINK_GATEWAY_PORT="$GATEWAY_PORT" AGENTLINK_REGISTRY_URL="http://127.0.0.1:$REGISTRY_PORT" \
AGENTLINK_IO_THREADS=4 "$GATEWAY" >"$WORK_DIR/gateway.log" 2>&1 &
GATEWAY_PID="$!"
PIDS="$GATEWAY_PID $PIDS"

for _ in $(seq 1 50); do
  curl --noproxy "*" -sf "http://127.0.0.1:$GATEWAY_PORT/healthz" >/dev/null && break
  sleep 0.1
done

TASK_IDS=""
for i in $(seq 1 10); do
  created="$(curl --noproxy "*" -sf -X POST "http://127.0.0.1:$GATEWAY_PORT/v1/tasks" \
    -H 'Content-Type: application/json' -d "{\"skill\":\"shutdown\",\"message\":\"task $i\"}")" || exit 1
  task_id="$(printf '%s' "$created" | sed -n 's/.*"taskId":"\([^"]*\)".*/\1/p')"
  [ -n "$task_id" ] || exit 1
  TASK_IDS="$TASK_IDS $task_id"
done

kill -TERM "$GATEWAY_PID" || exit 1
for _ in $(seq 1 100); do
  kill -0 "$GATEWAY_PID" >/dev/null 2>&1 || break
  sleep 0.05
done
if kill -0 "$GATEWAY_PID" >/dev/null 2>&1; then
  echo "gateway did not exit after SIGTERM" >&2
  exit 1
fi
wait "$GATEWAY_PID" || exit 1

for task_id in $TASK_IDS; do
  events="$(redis-cli --raw LRANGE "a2a:events:task:$task_id" 0 -1)" || exit 1
  completed="$(printf '%s' "$events" | grep -o '"type":"task.completed"' | wc -l | tr -d ' ')"
  failed="$(printf '%s' "$events" | grep -o '"type":"task.failed"' | wc -l | tr -d ' ')"
  [ $((completed + failed)) -eq 1 ] || {
    echo "task $task_id has completed=$completed failed=$failed after shutdown" >&2
    exit 1
  }
done

echo "gateway_shutdown_smoke passed tasks=10"
