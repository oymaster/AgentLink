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
  input_modes?: string[];
  output_modes?: string[];
  capabilities?: Record<string, boolean>;
  health?: string;
  load?: number;
  version?: string;
  platform?: string;
  runtime?: string;
}

export interface AgentListResponse {
  success?: boolean;
  agents?: AgentRegistration[];
  count?: number;
  error?: string;
}

export interface SendMessageResult {
  id?: string;
  type?: string;
  role?: string;
  model?: string;
  content?: unknown;
  raw: unknown;
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
  constructor(
    message: string,
    public readonly code: string,
    public readonly details?: unknown
  ) {
    super(message);
    this.name = "AgentLinkError";
  }
}

export class AgentLinkClient {
  constructor(private readonly config: AgentLinkConfig) {}

  async listAgents(): Promise<AgentRegistration[]> {
    const response = await this.requestJson<AgentListResponse>(`${this.config.registryUrl}/v1/agents`, {
      method: "GET"
    });

    if (response.success === false) {
      throw new AgentLinkError(response.error ?? "registry returned failure", "registry_error", response);
    }

    return response.agents ?? [];
  }

  async findAgents(query: { skill?: string; tag?: string }): Promise<AgentRegistration[]> {
    if (!query.skill && !query.tag) {
      return this.listAgents();
    }

    const path = query.skill ? "/v1/agent/find_by_skill" : "/v1/agent/find";
    const body = query.skill ? { skill: query.skill } : { tag: query.tag };
    const response = await this.requestJson<AgentListResponse>(`${this.config.registryUrl}${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body)
    });

    if (response.success === false) {
      throw new AgentLinkError(response.error ?? "registry returned failure", "registry_error", response);
    }

    return response.agents ?? [];
  }

  async sendMessage(input: {
    message: string;
    model?: string;
    system?: string;
    metadata?: Record<string, string>;
    maxTokens?: number;
  }): Promise<SendMessageResult> {
    const body = {
      model: input.model ?? "agentlink-mcp",
      system: input.system,
      max_tokens: input.maxTokens ?? 1024,
      metadata: input.metadata,
      messages: [
        {
          role: "user",
          content: input.message
        }
      ]
    };

    const raw = await this.requestJson<unknown>(this.config.messagesUrl, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body)
    });

    return {
      ...(typeof raw === "object" && raw !== null ? raw : {}),
      raw
    } as SendMessageResult;
  }

  async callAgent(input: {
    skill: string;
    message: string;
    tag?: string;
    historyLength?: number;
  }): Promise<CallAgentResult> {
    const agents = await this.findAgents({ skill: input.skill });
    const selected = this.selectAgent(agents, input.tag);
    if (!selected) {
      throw new AgentLinkError(`No healthy agent found for skill: ${input.skill}`, "agent_not_found", {
        skill: input.skill,
        tag: input.tag,
        agents
      });
    }

    const { answer, raw } = await this.invokeAgent(selected, input.message, input.historyLength);
    return { selectedAgent: selected, answer, raw };
  }

  // Send an A2A message/send to an already-selected agent.
  async invokeAgent(
    agent: AgentRegistration,
    message: string,
    historyLength?: number
  ): Promise<{ answer: string; raw: unknown }> {
    const messageId = `mcp-msg-${Date.now()}`;
    const requestId = `mcp-rpc-${Date.now()}`;
    const rpc = {
      jsonrpc: "2.0",
      id: requestId,
      method: "message/send",
      params: {
        message: {
          messageId,
          role: "user",
          parts: [
            {
              kind: "text",
              text: message
            }
          ]
        },
        historyLength: historyLength ?? 0
      }
    };

    const raw = await this.requestJson<unknown>(agent.address, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(rpc)
    });

    return { answer: extractA2AText(raw), raw };
  }

  selectAgent(agents: AgentRegistration[], tag?: string): AgentRegistration | undefined {
    const candidates = agents
      .filter((agent) => !tag || (agent.tags ?? []).includes(tag))
      .filter((agent) => !agent.health || agent.health === "healthy" || agent.health === "unknown")
      .sort((lhs, rhs) => (lhs.load ?? 0) - (rhs.load ?? 0));

    return candidates[0];
  }

  private async requestJson<T>(url: string, init: RequestInit): Promise<T> {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), this.config.requestTimeoutMs);

    try {
      const response = await fetch(url, {
        ...init,
        signal: controller.signal
      });
      const text = await response.text();

      if (!response.ok) {
        throw new AgentLinkError(`HTTP ${response.status} from ${url}`, "http_error", text);
      }

      if (!text) {
        return undefined as T;
      }

      return JSON.parse(text) as T;
    } catch (error) {
      if (error instanceof AgentLinkError) {
        throw error;
      }
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

function extractA2AText(raw: unknown): string {
  if (!isRecord(raw)) {
    return "";
  }

  const result = raw.result;
  if (!isRecord(result)) {
    return "";
  }

  const parts = result.parts;
  if (!Array.isArray(parts)) {
    return "";
  }

  const textPart = parts.find((part) => isRecord(part) && part.kind === "text");
  if (!isRecord(textPart) || typeof textPart.text !== "string") {
    return "";
  }

  return textPart.text;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}
