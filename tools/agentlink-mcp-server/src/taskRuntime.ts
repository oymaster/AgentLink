import { AgentLinkClient, AgentLinkError, summarizeAgents } from "./agentlinkClient.js";
import { RedisEventLog, TaskEvent, newTaskIds } from "./taskEvents.js";

export interface CreateTaskInput {
  skill: string;
  message: string;
  tag?: string;
}

export interface TaskHandle {
  taskId: string;
  traceId: string;
  contextId: string;
  state: string;
}

export interface TaskView {
  taskId: string;
  state: string;
  eventCount: number;
  answer?: string;
  error?: string;
  latestEvent?: string;
  events?: TaskEvent[];
}

const SOURCE_AGENT = "agentlink-mcp-bridge";

// The MCP bridge acts as a local task runtime: create_task dispatches in the
// background and persists lifecycle events to the shared Redis event store, so
// the handle is non-blocking and recoverable across bridge restarts (events
// live in Redis, not in process memory).
export class TaskRuntime {
  private readonly cancelRequested = new Set<string>();

  constructor(
    private readonly client: AgentLinkClient,
    private readonly events: RedisEventLog
  ) {}

  async createTask(input: CreateTaskInput): Promise<TaskHandle> {
    const { taskId, traceId, contextId } = newTaskIds();

    await this.events.append({
      type: "task.created",
      traceId,
      taskId,
      contextId,
      sourceAgentId: SOURCE_AGENT,
      payload: { skill: input.skill, message: input.message, tag: input.tag ?? null }
    });

    // Fire-and-forget: do not await, so the handle returns immediately.
    void this.dispatch(input, taskId, traceId, contextId);

    return { taskId, traceId, contextId, state: "created" };
  }

  private async dispatch(
    input: CreateTaskInput,
    taskId: string,
    traceId: string,
    contextId: string
  ): Promise<void> {
    const base = { traceId, taskId, contextId, sourceAgentId: SOURCE_AGENT };
    try {
      if (this.cancelRequested.has(taskId)) {
        return this.markCancelled(taskId, traceId, contextId);
      }

      const agents = await this.client.findAgents({ skill: input.skill });
      const selected = this.client.selectAgent(agents, input.tag);
      if (!selected) {
        await this.events.append({
          ...base,
          type: "task.failed",
          payload: { error: "agent_not_found", skill: input.skill, tag: input.tag ?? null }
        });
        return;
      }

      const targetId = selected.agent_id ?? selected.id ?? selected.address;
      await this.events.append({
        ...base,
        type: "task.dispatched",
        targetAgentId: targetId,
        payload: { agent: summarizeAgents([selected])[0], address: selected.address }
      });

      if (this.cancelRequested.has(taskId)) {
        return this.markCancelled(taskId, traceId, contextId);
      }

      await this.events.append({
        ...base,
        type: "task.running",
        targetAgentId: targetId,
        payload: { address: selected.address }
      });

      const { answer } = await this.client.invokeAgent(selected, input.message);

      if (this.cancelRequested.has(taskId)) {
        return this.markCancelled(taskId, traceId, contextId);
      }

      await this.events.append({
        ...base,
        type: "task.completed",
        targetAgentId: targetId,
        payload: { answer }
      });
    } catch (error) {
      const payload =
        error instanceof AgentLinkError
          ? { error: error.code, message: error.message }
          : { error: "internal_error", message: (error as Error).message };
      await this.events.append({ ...base, type: "task.failed", payload });
    } finally {
      this.cancelRequested.delete(taskId);
    }
  }

  private async markCancelled(taskId: string, traceId: string, contextId: string): Promise<void> {
    await this.events.append({
      type: "task.failed",
      traceId,
      taskId,
      contextId,
      sourceAgentId: SOURCE_AGENT,
      payload: { cancelled: true }
    });
  }

  async getTask(taskId: string): Promise<TaskView> {
    const events = await this.events.list(taskId);
    return this.toView(taskId, events, false);
  }

  async streamTask(taskId: string, sinceSequence = 0): Promise<TaskView> {
    const all = await this.events.list(taskId);
    const events = all.filter((event) => event.sequence > sinceSequence);
    return this.toView(taskId, all, true, events);
  }

  // Best-effort cancel: signal the in-flight dispatch. Terminal tasks are a no-op.
  async cancelTask(taskId: string): Promise<TaskView> {
    const events = await this.events.list(taskId);
    const latest = events[events.length - 1];
    const terminal = latest && (latest.type === "task.completed" || latest.type === "task.failed");
    if (events.length > 0 && !terminal) {
      this.cancelRequested.add(taskId);
    }
    return this.toView(taskId, events, false);
  }

  private toView(
    taskId: string,
    events: TaskEvent[],
    includeEvents: boolean,
    eventsSlice?: TaskEvent[]
  ): TaskView {
    const latest = events[events.length - 1];
    const state = latest ? latest.type.replace("task.", "") : "unknown";
    const completed = events.find((event) => event.type === "task.completed");
    const failed = events.find((event) => event.type === "task.failed");

    const view: TaskView = {
      taskId,
      state,
      eventCount: events.length,
      latestEvent: latest?.type
    };

    if (completed && isRecord(completed.payload) && typeof completed.payload.answer === "string") {
      view.answer = completed.payload.answer;
    }
    if (failed && isRecord(failed.payload)) {
      view.error = JSON.stringify(failed.payload);
    }
    if (includeEvents) {
      view.events = eventsSlice ?? events;
    }
    return view;
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}
