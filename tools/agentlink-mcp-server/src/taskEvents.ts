import { Redis } from "ioredis";

export type TaskEventType =
  | "task.created"
  | "task.dispatched"
  | "task.running"
  | "task.message"
  | "task.artifact"
  | "task.completed"
  | "task.failed";

// Mirrors the C++ a2a::TaskEvent JSON schema (see src/runtime/task_event.cpp)
// so events written by this bridge are interoperable with the C++ RedisEventStore.
export interface TaskEvent {
  event_id: string;
  trace_id: string;
  task_id: string;
  context_id: string;
  source_agent_id: string;
  target_agent_id: string;
  type: TaskEventType;
  sequence: number;
  timestamp_ms: number;
  payload: unknown;
  metadata: Record<string, string>;
}

function makeId(prefix: string): string {
  return `${prefix}-${Date.now()}-${Math.floor(Math.random() * 0xffffffffffff).toString(16)}`;
}

function taskKey(taskId: string): string {
  return `a2a:events:task:${taskId}`;
}

function traceKey(traceId: string): string {
  return `a2a:events:trace:${traceId}`;
}

export interface NewEventInput {
  type: TaskEventType;
  traceId: string;
  taskId: string;
  contextId?: string;
  sourceAgentId?: string;
  targetAgentId?: string;
  payload?: unknown;
  metadata?: Record<string, string>;
}

export class RedisEventLog {
  private readonly redis: Redis;

  constructor(redisUrl: string) {
    this.redis = new Redis(redisUrl, { lazyConnect: false, maxRetriesPerRequest: 2 });
  }

  // Append an event to the same Redis Lists the C++ RedisEventStore uses
  // (task + trace), assigning a 1-based sequence per task.
  async append(input: NewEventInput): Promise<TaskEvent> {
    const length = await this.redis.llen(taskKey(input.taskId));
    const event: TaskEvent = {
      event_id: makeId("evt"),
      trace_id: input.traceId,
      task_id: input.taskId,
      context_id: input.contextId ?? "",
      source_agent_id: input.sourceAgentId ?? "",
      target_agent_id: input.targetAgentId ?? "",
      type: input.type,
      sequence: length + 1,
      timestamp_ms: Date.now(),
      payload: input.payload ?? {},
      metadata: input.metadata ?? {}
    };

    const serialized = JSON.stringify(event);
    await this.redis.rpush(taskKey(input.taskId), serialized);
    await this.redis.rpush(traceKey(input.traceId), serialized);
    return event;
  }

  async list(taskId: string): Promise<TaskEvent[]> {
    const raw = await this.redis.lrange(taskKey(taskId), 0, -1);
    const events: TaskEvent[] = [];
    for (const item of raw) {
      try {
        events.push(JSON.parse(item) as TaskEvent);
      } catch {
        // skip malformed event, mirrors C++ RedisEventStore behaviour
      }
    }
    return events;
  }

  async latest(taskId: string): Promise<TaskEvent | undefined> {
    const events = await this.list(taskId);
    return events.length > 0 ? events[events.length - 1] : undefined;
  }

  async close(): Promise<void> {
    await this.redis.quit();
  }
}

export function newTaskIds(): { taskId: string; traceId: string; contextId: string } {
  return {
    taskId: makeId("mcp-task"),
    traceId: makeId("mcp-trace"),
    contextId: makeId("mcp-ctx")
  };
}
