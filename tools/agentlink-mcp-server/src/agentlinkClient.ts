import type { AgentLinkConfig } from "./config.js";

export interface AgentSkill {
  name: string;
  description?: string;
  input_modes?: string[];
  output_modes?: string[];
}

export interface AgentRegistration {
  id?: string;
  agent_id?: string;
  name?: string;
  address: string;
  tags?: string[];
  skills?: AgentSkill[];
  health?: string;
  load?: number;
  capabilities?: Record<string, boolean>;
}

export interface CallAgentResult {
  selectedAgent: AgentRegistration;
  answer: string;
  raw: unknown;
}

export interface AgentSummary {
  id?: string;
  name?: string;
  address: string;
  tags: string[];
  skills: string[];
  health: string;
  load: number;
  capabilities: Record<string, boolean>;
}

export class AgentLinkError extends Error {
  constructor(message: string, public readonly code: string, public readonly details?: unknown) {
    super(message);
    this.name = "AgentLinkError";
  }
}

export class AgentLinkClient {
  constructor(private readonly config: AgentLinkConfig) {}

  async listAgents(): Promise<AgentRegistration[]> {
    const response = await this.requestJson<{ agents?: AgentRegistration[] }>(`${this.config.gatewayUrl}/v1/agents`, {
      method: "GET"
    });
    return response.agents ?? [];
  }

  async findAgents(query: { skill?: string; tag?: string }): Promise<AgentRegistration[]> {
    const response = await this.requestJson<{ agents?: AgentRegistration[] }>(`${this.config.gatewayUrl}/v1/agents/find`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(query)
    });
    return response.agents ?? [];
  }

  async sendMessage(input: { message: string; model?: string; system?: string; maxTokens?: number }): Promise<{ raw: unknown }> {
    const raw = await this.requestJson<unknown>(this.config.messagesUrl, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(input)
    });
    return { raw };
  }

  async callAgent(input: { skill: string; message: string; tag?: string; historyLength?: number }): Promise<CallAgentResult> {
    const raw = await this.requestJson<{ selectedAgent: AgentRegistration; answer: string }>(
      `${this.config.gatewayUrl}/v1/call`,
      {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(input)
      },
      this.config.callTimeoutMs
    );
    if (!isRecord(raw) || !isRecord(raw.selectedAgent) || typeof raw.answer !== "string") {
      throw new AgentLinkError("Gateway returned an invalid call response", "invalid_response", raw);
    }
    return { ...raw, raw };
  }

  async createTask(input: { skill?: string; message: string; tag?: string }): Promise<unknown> {
    return this.requestJson(`${this.config.gatewayUrl}/v1/tasks`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(input)
    });
  }

  async getTask(taskId: string): Promise<unknown> {
    return this.requestJson(`${this.config.gatewayUrl}/v1/tasks/${encodeURIComponent(taskId)}`, { method: "GET" });
  }

  async streamTask(taskId: string, sinceSequence: number): Promise<unknown> {
    return this.requestJson(
      `${this.config.gatewayUrl}/v1/tasks/${encodeURIComponent(taskId)}/events?since=${sinceSequence}`,
      { method: "GET" }
    );
  }

  async cancelTask(taskId: string): Promise<unknown> {
    return this.requestJson(`${this.config.gatewayUrl}/v1/tasks/${encodeURIComponent(taskId)}/cancel`, { method: "POST" });
  }

  private async requestJson<T>(url: string, init: RequestInit, timeoutMs = this.config.requestTimeoutMs): Promise<T> {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), timeoutMs);
    try {
      const response = await fetch(url, { ...init, signal: controller.signal });
      const text = await response.text();
      const details = text ? safeJson(text) : undefined;
      if (!response.ok) {
        const upstreamCode = isRecord(details) && typeof details.error === "string" ? details.error : undefined;
        const code = response.status === 501 ? "not_implemented" : upstreamCode ?? "http_error";
        throw new AgentLinkError(`HTTP ${response.status} from ${url}`, code, details);
      }
      return details as T;
    } catch (error) {
      if (error instanceof AgentLinkError) throw error;
      throw new AgentLinkError(`Failed to request ${url}: ${(error as Error).message}`, "request_failed", error);
    } finally {
      clearTimeout(timeout);
    }
  }
}

export function summarizeAgents(agents: AgentRegistration[]): AgentSummary[] {
  return agents.map((agent) => ({
    id: agent.agent_id ?? agent.id,
    name: agent.name,
    address: agent.address,
    tags: agent.tags ?? [],
    skills: (agent.skills ?? []).map((skill) => skill.name),
    health: agent.health ?? "unknown",
    load: agent.load ?? 0,
    capabilities: agent.capabilities ?? {}
  }));
}

function safeJson(text: string): unknown {
  try { return JSON.parse(text); } catch { return text; }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}
