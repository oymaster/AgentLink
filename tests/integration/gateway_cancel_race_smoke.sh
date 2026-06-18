#!/usr/bin/env bash
set -u

REGISTRY="$1"
AGENT="$2"
GATEWAY="$3"
BASE_PORT="$((29000 + ($$ % 1000) * 3))"
REGISTRY_PORT="$BASE_PORT"
AGENT_PORT="$((BASE_PORT + 1))"
GATEWAY_PORT="$((BASE_PORT + 2))"
WORK_DIR="${TMPDIR:-/tmp}/agentlink-gateway-race-$$"
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
AGENTLINK_CLI_MOCK_DELAY_MS=60 "$AGENT" race-agent "$AGENT_PORT" \
  "http://127.0.0.1:$REGISTRY_PORT" race "cancel complete race agent" mock \
  >"$WORK_DIR/agent.log" 2>&1 & PIDS="$! $PIDS"
sleep 0.8
AGENTLINK_GATEWAY_PORT="$GATEWAY_PORT" AGENTLINK_REGISTRY_URL="http://127.0.0.1:$REGISTRY_PORT" \
AGENTLINK_IO_THREADS=4 "$GATEWAY" >"$WORK_DIR/gateway.log" 2>&1 & PIDS="$! $PIDS"

for _ in $(seq 1 50); do
  curl --noproxy "*" -sf "http://127.0.0.1:$GATEWAY_PORT/healthz" >/dev/null && break
  sleep 0.1
done

create_task() {
  created="$(curl --noproxy "*" -sf -X POST "http://127.0.0.1:$GATEWAY_PORT/v1/tasks" \
    -H 'Content-Type: application/json' -d '{"skill":"race","message":"race"}')" || return 1
  printf '%s' "$created" | sed -n 's/.*"taskId":"\([^"]*\)".*/\1/p'
}

wait_terminal() {
  task_id="$1"
  for _ in $(seq 1 100); do
    view="$(curl --noproxy "*" -sf "http://127.0.0.1:$GATEWAY_PORT/v1/tasks/$task_id")" || return 1
    state="$(printf '%s' "$view" | sed -n 's/.*"state":"\([^"]*\)".*/\1/p')"
    if [ "$state" = "completed" ] || [ "$state" = "failed" ]; then
      return 0
    fi
    sleep 0.02
  done
  return 1
}

assert_single_terminal() {
  task_id="$1"
  events="$(curl --noproxy "*" -sf "http://127.0.0.1:$GATEWAY_PORT/v1/tasks/$task_id/events?since=0")" || return 1
  completed="$(printf '%s' "$events" | grep -o '"type":"task.completed"' | wc -l | tr -d ' ')"
  failed="$(printf '%s' "$events" | grep -o '"type":"task.failed"' | wc -l | tr -d ' ')"
  [ $((completed + failed)) -eq 1 ] || {
    echo "task $task_id has completed=$completed failed=$failed: $events" >&2
    return 1
  }
}

# Deterministic cancel-before-complete phase.
for _ in $(seq 1 10); do
  task_id="$(create_task)" || exit 1
  curl --noproxy "*" -sf -X POST "http://127.0.0.1:$GATEWAY_PORT/v1/tasks/$task_id/cancel" >/dev/null || exit 1
  wait_terminal "$task_id" || exit 1
  assert_single_terminal "$task_id" || exit 1
done

# Deterministic complete-before-cancel phase: cancelling a terminal task is a no-op.
for _ in $(seq 1 10); do
  task_id="$(create_task)" || exit 1
  wait_terminal "$task_id" || exit 1
  curl --noproxy "*" -sf -X POST "http://127.0.0.1:$GATEWAY_PORT/v1/tasks/$task_id/cancel" >/dev/null || exit 1
  assert_single_terminal "$task_id" || exit 1
done

# Boundary stress with deterministic pseudo-random phases around the 60 ms agent delay.
for i in $(seq 1 80); do
  task_id="$(create_task)" || exit 1
  delay_ms=$(( (i * 37) % 90 ))
  sleep "0.$(printf '%03d' "$delay_ms")"
  curl --noproxy "*" -sf -X POST "http://127.0.0.1:$GATEWAY_PORT/v1/tasks/$task_id/cancel" >/dev/null || exit 1
  wait_terminal "$task_id" || exit 1
  assert_single_terminal "$task_id" || exit 1
done

echo "gateway_cancel_race_smoke passed tasks=100"
