import { AgentLinkClient } from "./agentlinkClient.js";

export interface CreateTaskInput { skill?: string; message: string; tag?: string; }
export interface TaskHandle { taskId: string; traceId: string; contextId: string; state: string; }
export interface TaskEvent {
  event_id: string; trace_id: string; task_id: string; context_id: string;
  source_agent_id: string; target_agent_id: string; type: string;
  sequence: number; timestamp_ms: number; payload: unknown; metadata: Record<string, string>;
}
export interface TaskView {
  taskId: string; state: string; eventCount: number; answer?: string;
  error?: unknown; latestEvent?: string; events?: TaskEvent[];
}

export class TaskRuntime {
  constructor(private readonly client: AgentLinkClient) {}
  async createTask(input: CreateTaskInput): Promise<TaskHandle> {
    return this.client.createTask(input) as Promise<TaskHandle>;
  }
  async getTask(taskId: string): Promise<TaskView> {
    return this.client.getTask(taskId) as Promise<TaskView>;
  }
  async streamTask(taskId: string, sinceSequence = 0): Promise<TaskView> {
    return this.client.streamTask(taskId, sinceSequence) as Promise<TaskView>;
  }
  async cancelTask(taskId: string): Promise<TaskView> {
    return this.client.cancelTask(taskId) as Promise<TaskView>;
  }
}
