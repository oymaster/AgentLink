# AgentLink Gateway Benchmark

This benchmark validates Gateway task lifecycle throughput and latency under
repeatable local or Linux-server load.

## Scope

- Starts one Registry, one Gateway, and N mock CLI agents.
- Exercises `/v1/tasks`, `/v1/tasks/{id}`, `/v1/tasks/{id}/events`, and optional `/cancel`.
- Reports per-operation count, error rate, average latency, p50, p95, p99, and max latency.
- Verifies every task has exactly one terminal lifecycle event.
- Uses an empty tag filter by default because the demo CLI agents register
  `cli`, `role-agent`, skill, and agent-id tags, but not `local`.

## Quick Run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON -DBUILD_GATEWAY=ON
cmake --build build -j

REQUESTS=1000 CONCURRENCY=100 AGENT_COUNT=4 \
  bash scripts/benchmark_gateway.sh
```

The script requires Redis to be running locally. It returns `77` when Redis is
not available, matching the integration-test skip convention.

Set `BENCHMARK_OUT_DIR=/tmp/agentlink-run` to retain `gateway.log`, agent logs,
`gateway-ps.csv`, Redis INFO snapshots, and `result.json`.

Use `AGENT_MOCK_DELAY_MS=500` to emulate slow local agents.

Set `MIXED_SKILLS=1` to register agents as `benchmark-1`, `benchmark-2`, ...
and send requests across those skills. This measures multi-agent fan-out. The
default same-skill mode measures exact-route behavior for one skill.

## Suggested Matrix

| Scenario | Command |
| --- | --- |
| Smoke | `REQUESTS=100 CONCURRENCY=20 bash scripts/benchmark_gateway.sh` |
| Baseline | `REQUESTS=1000 CONCURRENCY=100 bash scripts/benchmark_gateway.sh` |
| High concurrency | `REQUESTS=5000 CONCURRENCY=500 bash scripts/benchmark_gateway.sh` |
| Cancel mix | `REQUESTS=2000 CONCURRENCY=200 CANCEL_RATIO=0.2 bash scripts/benchmark_gateway.sh` |

## Record Template

```text
Host:
CPU:
Memory:
OS:
Compiler:
Redis:
AgentLink commit:
Command:
Result JSON:
Notes:
```
