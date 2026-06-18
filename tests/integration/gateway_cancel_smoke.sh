#!/usr/bin/env bash
set -u

REGISTRY="$1"
AGENT="$2"
GATEWAY="$3"
BASE_PORT="$((23000 + ($$ % 1000) * 3))"
REGISTRY_PORT="$BASE_PORT"
AGENT_PORT="$((BASE_PORT + 1))"
GATEWAY_PORT="$((BASE_PORT + 2))"
WORK_DIR="${TMPDIR:-/tmp}/agentlink-gateway-cancel-$$"
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

"$REGISTRY" "$REGISTRY_PORT" >"$WORK_DIR/registry.log" 2>&1 &
PIDS="$! $PIDS"
sleep 0.3
AGENTLINK_CLI_MOCK_DELAY_MS=3000 "$AGENT" slow-agent "$AGENT_PORT" \
  "http://127.0.0.1:$REGISTRY_PORT" slow "slow cancellable test agent" mock \
  >"$WORK_DIR/agent.log" 2>&1 &
PIDS="$! $PIDS"
sleep 0.8
AGENTLINK_GATEWAY_PORT="$GATEWAY_PORT" AGENTLINK_REGISTRY_URL="http://127.0.0.1:$REGISTRY_PORT" \
  "$GATEWAY" >"$WORK_DIR/gateway.log" 2>&1 &
PIDS="$! $PIDS"

for _ in $(seq 1 50); do
  curl --noproxy "*" -sf "http://127.0.0.1:$GATEWAY_PORT/healthz" >/dev/null && break
  sleep 0.1
done

created="$(curl --noproxy "*" -sf -X POST "http://127.0.0.1:$GATEWAY_PORT/v1/tasks" \
  -H 'Content-Type: application/json' -d '{"skill":"slow","message":"wait"}')" || exit 1
task_id="$(printf '%s' "$created" | sed -n 's/.*"taskId":"\([^"]*\)".*/\1/p')"
[ -n "$task_id" ] || exit 1
curl --noproxy "*" -sf -X POST "http://127.0.0.1:$GATEWAY_PORT/v1/tasks/$task_id/cancel" >/dev/null || exit 1
sleep 0.3
events="$(curl --noproxy "*" -sf "http://127.0.0.1:$GATEWAY_PORT/v1/tasks/$task_id/events?since=0")" || exit 1
failed_count="$(printf '%s' "$events" | grep -o '"type":"task.failed"' | wc -l | tr -d ' ')"
completed_count="$(printf '%s' "$events" | grep -o '"type":"task.completed"' | wc -l | tr -d ' ')"
[ "$failed_count" = "1" ] || { echo "expected one failed event, got: $events"; exit 1; }
[ "$completed_count" = "0" ] || { echo "unexpected completed event: $events"; exit 1; }
echo "gateway_cancel_smoke passed"
