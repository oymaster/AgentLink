#!/usr/bin/env bash
set -u

REGISTRY="$1"
AGENT="$2"
GATEWAY="$3"
BASE_PORT="$((26000 + ($$ % 1000) * 3))"
REGISTRY_PORT="$BASE_PORT"
AGENT_PORT="$((BASE_PORT + 1))"
GATEWAY_PORT="$((BASE_PORT + 2))"
WORK_DIR="${TMPDIR:-/tmp}/agentlink-gateway-concurrency-$$"
mkdir -p "$WORK_DIR/tasks"

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
"$AGENT" concurrent-agent "$AGENT_PORT" "http://127.0.0.1:$REGISTRY_PORT" \
  concurrent "concurrent test agent" mock >"$WORK_DIR/agent.log" 2>&1 & PIDS="$! $PIDS"
sleep 0.8
AGENTLINK_GATEWAY_PORT="$GATEWAY_PORT" AGENTLINK_REGISTRY_URL="http://127.0.0.1:$REGISTRY_PORT" \
AGENTLINK_IO_THREADS=4 "$GATEWAY" >"$WORK_DIR/gateway.log" 2>&1 & PIDS="$! $PIDS"

for _ in $(seq 1 50); do
  curl --noproxy "*" -sf "http://127.0.0.1:$GATEWAY_PORT/healthz" >/dev/null && break
  sleep 0.1
done

CURL_PIDS=""
for i in $(seq 1 100); do
  curl --noproxy "*" -sf -X POST "http://127.0.0.1:$GATEWAY_PORT/v1/tasks" \
    -H 'Content-Type: application/json' -d "{\"skill\":\"concurrent\",\"message\":\"task $i\"}" \
    >"$WORK_DIR/tasks/$i.json" &
  CURL_PIDS="$! $CURL_PIDS"
done

for _ in $(seq 1 50); do
  curl --noproxy "*" -sf "http://127.0.0.1:$GATEWAY_PORT/healthz" >/dev/null || exit 1
done
for pid in $CURL_PIDS; do wait "$pid" || exit 1; done

for i in $(seq 1 100); do
  task_id="$(sed -n 's/.*"taskId":"\([^"]*\)".*/\1/p' "$WORK_DIR/tasks/$i.json")"
  [ -n "$task_id" ] || exit 1
  terminal=""
  for _ in $(seq 1 100); do
    view="$(curl --noproxy "*" -sf "http://127.0.0.1:$GATEWAY_PORT/v1/tasks/$task_id")" || exit 1
    terminal="$(printf '%s' "$view" | sed -n 's/.*"state":"\([^"]*\)".*/\1/p')"
    [ "$terminal" = "completed" ] || [ "$terminal" = "failed" ] && break
    sleep 0.05
  done
  [ "$terminal" = "completed" ] || { echo "task $task_id ended as $terminal"; exit 1; }
  events="$(curl --noproxy "*" -sf "http://127.0.0.1:$GATEWAY_PORT/v1/tasks/$task_id/events?since=0")" || exit 1
  completed_count="$(printf '%s' "$events" | grep -o '"type":"task.completed"' | wc -l | tr -d ' ')"
  failed_count="$(printf '%s' "$events" | grep -o '"type":"task.failed"' | wc -l | tr -d ' ')"
  [ "$completed_count" = "1" ] && [ "$failed_count" = "0" ] || exit 1
done

echo "gateway_concurrency_smoke passed tasks=100 health_checks=50"
