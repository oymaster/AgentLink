#!/usr/bin/env python3
"""Benchmark AgentLink Gateway task lifecycle endpoints.

The script uses only Python standard library modules so it can run on a fresh
Linux/macOS host after the C++ binaries are built.
"""

from __future__ import annotations

import argparse
import json
import statistics
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from typing import Any


@dataclass
class Sample:
    operation: str
    ok: bool
    latency_ms: float
    status: int
    error: str = ""


class Metrics:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.samples: list[Sample] = []

    def add(self, sample: Sample) -> None:
        with self._lock:
            self.samples.append(sample)


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = int((len(ordered) - 1) * pct)
    return ordered[index]


def request_json(
    method: str,
    url: str,
    payload: dict[str, Any] | None,
    timeout_s: float,
) -> tuple[int, Any]:
    body = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=body,
        method=method,
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=timeout_s) as response:
        content = response.read()
        if not content:
            return response.status, None
        return response.status, json.loads(content.decode("utf-8"))


def compact_error(value: str, limit: int = 180) -> str:
    value = " ".join(value.split())
    return value[:limit]


def measured(
    metrics: Metrics,
    operation: str,
    method: str,
    url: str,
    payload: dict[str, Any] | None,
    timeout_s: float,
) -> tuple[int, Any] | None:
    started = time.perf_counter()
    try:
        status, data = request_json(method, url, payload, timeout_s)
        metrics.add(Sample(operation, 200 <= status < 300, (time.perf_counter() - started) * 1000, status))
        return status, data
    except urllib.error.HTTPError as error:
        body = ""
        try:
            body = error.read().decode("utf-8", errors="replace")
        except Exception:
            body = ""
        detail = compact_error(body or str(error))
        metrics.add(Sample(operation, False, (time.perf_counter() - started) * 1000, error.code, detail))
    except Exception as error:  # Keep the benchmark running and report failures.
        metrics.add(Sample(operation, False, (time.perf_counter() - started) * 1000, 0, compact_error(str(error))))
    return None


def wait_terminal(
    base_url: str,
    task_id: str,
    timeout_s: float,
    poll_interval_s: float,
    metrics: Metrics,
    request_timeout_s: float,
) -> dict[str, Any]:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        result = measured(metrics, "get_task", "GET", f"{base_url}/v1/tasks/{task_id}", None, request_timeout_s)
        if result is not None:
            _, data = result
            state = data.get("state", "")
            if state in {"completed", "failed", "cancelled"}:
                return data
        time.sleep(poll_interval_s)
    return {"state": "timeout"}


def run_one(index: int, args: argparse.Namespace, metrics: Metrics) -> dict[str, Any]:
    skill = args.skill
    if args.skills:
        skill = args.skills[index % len(args.skills)]
    payload = {"skill": skill, "tag": args.tag, "message": f"{args.message_prefix} {index}"}
    result = measured(metrics, "create_task", "POST", f"{args.gateway_url}/v1/tasks", payload, args.timeout_s)
    if result is None:
        return {"ok": False, "state": "create_failed"}
    _, created = result
    task_id = created.get("taskId", "")
    if not task_id:
        return {"ok": False, "state": "missing_task_id"}

    expected_cancel = args.cancel_ratio > 0 and index % 100 < int(args.cancel_ratio * 100)
    if expected_cancel:
        time.sleep(args.cancel_delay_ms / 1000.0)
        measured(metrics, "cancel_task", "POST", f"{args.gateway_url}/v1/tasks/{task_id}/cancel", {}, args.timeout_s)

    final_view = wait_terminal(args.gateway_url, task_id, args.task_timeout_s, args.poll_interval_ms / 1000.0, metrics, args.timeout_s)
    state = final_view.get("state", "unknown")
    error = final_view.get("error", {})
    cancelled = bool(isinstance(error, dict) and error.get("cancelled"))
    events = measured(metrics, "stream_events", "GET", f"{args.gateway_url}/v1/tasks/{task_id}/events?since=0", None, args.timeout_s)
    terminal_count = 0
    if events is not None:
        _, event_view = events
        for item in event_view.get("events", []):
            if item.get("type") in {"task.completed", "task.failed"}:
                terminal_count += 1
    ok = terminal_count == 1 and ((not expected_cancel and state == "completed") or (expected_cancel and cancelled))
    error_kind = ""
    if isinstance(error, dict):
        error_kind = str(error.get("error", ""))
    return {
        "ok": ok,
        "state": state,
        "task_id": task_id,
        "expected_cancel": expected_cancel,
        "cancelled": cancelled,
        "error_kind": error_kind,
    }


def summarize(samples: list[Sample]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for operation in sorted({sample.operation for sample in samples}):
        group = [sample for sample in samples if sample.operation == operation]
        latencies = [sample.latency_ms for sample in group]
        ok = sum(1 for sample in group if sample.ok)
        errors: dict[str, int] = {}
        for sample in group:
            if sample.ok or not sample.error:
                continue
            key = f"{sample.status}:{sample.error}"
            errors[key] = errors.get(key, 0) + 1
        result[operation] = {
            "count": len(group),
            "ok": ok,
            "error": len(group) - ok,
            "error_rate": 0 if not group else round((len(group) - ok) / len(group), 6),
            "avg_ms": round(statistics.fmean(latencies), 3) if latencies else 0,
            "p50_ms": round(percentile(latencies, 0.50), 3),
            "p95_ms": round(percentile(latencies, 0.95), 3),
            "p99_ms": round(percentile(latencies, 0.99), 3),
            "max_ms": round(max(latencies), 3) if latencies else 0,
            "errors": errors,
        }
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gateway-url", default="http://127.0.0.1:5002")
    parser.add_argument("--requests", type=int, default=1000)
    parser.add_argument("--concurrency", type=int, default=100)
    parser.add_argument("--skill", default="benchmark")
    parser.add_argument("--skills", default="", help="comma-separated skill list for mixed routing")
    parser.add_argument("--tag", default="")
    parser.add_argument("--message-prefix", default="benchmark task")
    parser.add_argument("--cancel-ratio", type=float, default=0.0)
    parser.add_argument("--cancel-delay-ms", type=int, default=20)
    parser.add_argument("--timeout-s", type=float, default=5.0)
    parser.add_argument("--task-timeout-s", type=float, default=30.0)
    parser.add_argument("--poll-interval-ms", type=int, default=20)
    args = parser.parse_args()
    args.skills = [item.strip() for item in args.skills.split(",") if item.strip()]

    metrics = Metrics()
    started = time.perf_counter()
    results = []
    with ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        futures = [executor.submit(run_one, index, args, metrics) for index in range(args.requests)]
        for future in as_completed(futures):
            results.append(future.result())
    elapsed_s = time.perf_counter() - started

    completed = sum(1 for item in results if item["state"] == "completed")
    failed = sum(1 for item in results if item["state"] == "failed")
    cancelled = sum(1 for item in results if item.get("cancelled"))
    timed_out = sum(1 for item in results if item["state"] == "timeout")
    ok = sum(1 for item in results if item["ok"])
    error_kinds: dict[str, int] = {}
    for item in results:
        kind = item.get("error_kind", "")
        if kind:
            error_kinds[kind] = error_kinds.get(kind, 0) + 1
    report = {
        "gateway_url": args.gateway_url,
        "requests": args.requests,
        "concurrency": args.concurrency,
        "elapsed_s": round(elapsed_s, 3),
        "task_throughput_per_s": round(args.requests / elapsed_s, 3) if elapsed_s > 0 else 0,
        "tasks": {
            "ok": ok,
            "completed": completed,
            "failed": failed,
            "cancelled": cancelled,
            "timeout": timed_out,
            "other": args.requests - completed - failed - timed_out,
            "error_kinds": error_kinds,
        },
        "operations": summarize(metrics.samples),
    }
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if ok == args.requests else 1


if __name__ == "__main__":
    raise SystemExit(main())
