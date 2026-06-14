#!/usr/bin/env bash
set -u

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 <registry_server> <remote_math_agent> <remote_orchestrator>" >&2
  exit 2
fi

REGISTRY_SERVER="$1"
REMOTE_AGENT="$2"
ORCHESTRATOR="$3"

REDIS_HOST="${AGENTLINK_REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${AGENTLINK_REDIS_PORT:-6379}"
BASE_PORT="${AGENTLINK_TEST_BASE_PORT:-$((29000 + RANDOM % 1000))}"
REGISTRY_PORT="$BASE_PORT"
AGENT_PORT="$((BASE_PORT + 1))"
WORK_DIR="${TMPDIR:-/tmp}/agentlink-remote-smoke-$$"
REGISTRY_LOG="$WORK_DIR/registry.log"
AGENT_LOG="$WORK_DIR/agent.log"
ORCH_LOG="$WORK_DIR/orchestrator.log"
TASK_ID=""
TRACE_ID=""
REGISTRY_PID=""
AGENT_PID=""

mkdir -p "$WORK_DIR"

cleanup() {
  if [ -n "$AGENT_PID" ] && kill -0 "$AGENT_PID" >/dev/null 2>&1; then
    kill "$AGENT_PID" >/dev/null 2>&1 || true
    wait "$AGENT_PID" >/dev/null 2>&1 || true
  fi
  if [ -n "$REGISTRY_PID" ] && kill -0 "$REGISTRY_PID" >/dev/null 2>&1; then
    kill "$REGISTRY_PID" >/dev/null 2>&1 || true
    wait "$REGISTRY_PID" >/dev/null 2>&1 || true
  fi
  if [ -n "$TASK_ID" ]; then
    redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" DEL "a2a:events:task:$TASK_ID" >/dev/null 2>&1 || true
  fi
  if [ -n "$TRACE_ID" ]; then
    redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" DEL "a2a:events:trace:$TRACE_ID" >/dev/null 2>&1 || true
  fi
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT INT TERM

fail() {
  echo "remote_agent_smoke failed: $*" >&2
  echo "--- registry.log ---" >&2
  [ -f "$REGISTRY_LOG" ] && cat "$REGISTRY_LOG" >&2
  echo "--- agent.log ---" >&2
  [ -f "$AGENT_LOG" ] && cat "$AGENT_LOG" >&2
  echo "--- orchestrator.log ---" >&2
  [ -f "$ORCH_LOG" ] && cat "$ORCH_LOG" >&2
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

assert_contains() {
  pattern="$1"
  file="$2"
  if ! grep -q "$pattern" "$file"; then
    fail "expected '$pattern' in $file"
  fi
}

if ! command -v redis-cli >/dev/null 2>&1; then
  echo "SKIP remote_agent_smoke: redis-cli not found"
  exit 77
fi

if ! redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" ping >/dev/null 2>&1; then
  echo "SKIP remote_agent_smoke: Redis unavailable at $REDIS_HOST:$REDIS_PORT"
  exit 77
fi

"$REGISTRY_SERVER" "$REGISTRY_PORT" >"$REGISTRY_LOG" 2>&1 &
REGISTRY_PID="$!"
wait_for_log "HTTP Server listening" "$REGISTRY_LOG" 5 || fail "registry did not start"

AGENTLINK_REDIS_HOST="$REDIS_HOST" \
AGENTLINK_REDIS_PORT="$REDIS_PORT" \
AGENTLINK_REMOTE_STEP_DELAY_MS=100 \
  "$REMOTE_AGENT" math-smoke "$AGENT_PORT" "http://localhost:$REGISTRY_PORT" >"$AGENT_LOG" 2>&1 &
AGENT_PID="$!"
wait_for_log "listening at http://localhost:$AGENT_PORT" "$AGENT_LOG" 5 || fail "remote agent did not start"
sleep 0.2

AGENTLINK_REDIS_HOST="$REDIS_HOST" \
AGENTLINK_REDIS_PORT="$REDIS_PORT" \
AGENTLINK_EVENT_POLL_MS=50 \
  "$ORCHESTRATOR" "http://localhost:$REGISTRY_PORT" "21 * 2" >"$ORCH_LOG" 2>&1
ORCH_STATUS="$?"
[ "$ORCH_STATUS" -eq 0 ] || fail "orchestrator exited with $ORCH_STATUS"

cat "$ORCH_LOG"

assert_contains "selected_agent=math-smoke" "$ORCH_LOG"
assert_contains "answer=21 \\* 2 = 42" "$ORCH_LOG"
assert_contains "event_count=6" "$ORCH_LOG"
assert_contains "latest_event=task.completed" "$ORCH_LOG"
assert_contains "stream_event=task.created sequence=1" "$ORCH_LOG"
assert_contains "stream_event=task.dispatched sequence=2" "$ORCH_LOG"
assert_contains "stream_event=task.running sequence=3" "$ORCH_LOG"
assert_contains "stream_event=task.message sequence=4" "$ORCH_LOG"
assert_contains "stream_event=task.message sequence=5" "$ORCH_LOG"
assert_contains "stream_event=task.completed sequence=6" "$ORCH_LOG"

TASK_ID="$(sed -n 's/^task_id=//p' "$ORCH_LOG" | tail -1)"
TRACE_ID="$(sed -n 's/^trace_id=//p' "$ORCH_LOG" | tail -1)"
[ -n "$TASK_ID" ] || fail "task_id missing from orchestrator output"

REDIS_EVENTS="$(redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" LRANGE "a2a:events:task:$TASK_ID" 0 -1)"
REDIS_EVENT_COUNT="$(printf '%s\n' "$REDIS_EVENTS" | sed '/^$/d' | wc -l | tr -d ' ')"
[ "$REDIS_EVENT_COUNT" = "6" ] || fail "expected 6 Redis events, got $REDIS_EVENT_COUNT"
printf '%s\n' "$REDIS_EVENTS" | grep -q '"type":"task.completed"' || fail "completed event missing from Redis"

echo "remote_agent_smoke passed"
