export interface AgentLinkConfig {
  gatewayUrl: string;
  messagesUrl: string;
  requestTimeoutMs: number;
  callTimeoutMs: number;
}

function trimTrailingSlash(value: string): string {
  return value.endsWith("/") ? value.slice(0, -1) : value;
}

export function loadConfig(env: NodeJS.ProcessEnv = process.env): AgentLinkConfig {
  const gatewayUrl = trimTrailingSlash(env.AGENTLINK_GATEWAY_URL ?? "http://localhost:5002");
  const messagesUrl = env.AGENTLINK_MESSAGES_URL ?? `${gatewayUrl}/v1/messages`;
  const requestTimeoutMs = Number.parseInt(env.AGENTLINK_TIMEOUT_MS ?? "10000", 10);
  const callTimeoutMs = Number.parseInt(env.AGENTLINK_CALL_TIMEOUT_MS ?? "600000", 10);

  return {
    gatewayUrl,
    messagesUrl,
    requestTimeoutMs: Number.isFinite(requestTimeoutMs) ? requestTimeoutMs : 10000,
    callTimeoutMs: Number.isFinite(callTimeoutMs) ? callTimeoutMs : 600000
  };
}
