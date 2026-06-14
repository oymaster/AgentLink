export interface AgentLinkConfig {
  registryUrl: string;
  messagesUrl: string;
  redisUrl: string;
  requestTimeoutMs: number;
}

function trimTrailingSlash(value: string): string {
  return value.endsWith("/") ? value.slice(0, -1) : value;
}

function resolveRedisUrl(env: NodeJS.ProcessEnv): string {
  if (env.AGENTLINK_REDIS_URL) {
    return env.AGENTLINK_REDIS_URL;
  }
  // Interop with the C++ side, which uses AGENTLINK_REDIS_HOST/PORT.
  const host = env.AGENTLINK_REDIS_HOST ?? "127.0.0.1";
  const port = env.AGENTLINK_REDIS_PORT ?? "6379";
  return `redis://${host}:${port}`;
}

export function loadConfig(env: NodeJS.ProcessEnv = process.env): AgentLinkConfig {
  const gatewayUrl = trimTrailingSlash(env.AGENTLINK_GATEWAY_URL ?? "http://localhost:5002");
  const registryUrl = trimTrailingSlash(env.AGENTLINK_REGISTRY_URL ?? "http://localhost:8500");
  const messagesUrl = env.AGENTLINK_MESSAGES_URL ?? `${gatewayUrl}/v1/messages`;
  const requestTimeoutMs = Number.parseInt(env.AGENTLINK_TIMEOUT_MS ?? "10000", 10);

  return {
    registryUrl,
    messagesUrl,
    redisUrl: resolveRedisUrl(env),
    requestTimeoutMs: Number.isFinite(requestTimeoutMs) ? requestTimeoutMs : 10000
  };
}
