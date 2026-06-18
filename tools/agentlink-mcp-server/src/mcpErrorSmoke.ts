import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { once } from "node:events";
import { createServer, type IncomingMessage, type Server, type ServerResponse } from "node:http";
import { createInterface } from "node:readline";

type JsonRpcResponse = {
  jsonrpc: "2.0";
  id?: number | string;
  result?: {
    isError?: boolean;
    content?: Array<{ type: string; text: string }>;
  };
  error?: unknown;
};

type McpSession = {
  child: ChildProcessWithoutNullStreams;
  send: (method: string, params?: unknown) => Promise<JsonRpcResponse>;
  notify: (method: string, params?: unknown) => void;
  close: () => Promise<void>;
};

type Handler = (request: IncomingMessage, response: ServerResponse, body: string) => void;

async function startServer(handler: Handler): Promise<{ server: Server; url: string }> {
  const server = createServer((request, response) => {
    let body = "";
    request.setEncoding("utf8");
    request.on("data", (chunk) => {
      body += chunk;
    });
    request.on("end", () => {
      handler(request, response, body);
    });
  });

  await new Promise<void>((resolve) => {
    server.listen(0, "127.0.0.1", resolve);
  });

  const address = server.address();
  if (typeof address !== "object" || address === null) {
    throw new Error("failed to bind mock server");
  }

  return {
    server,
    url: `http://127.0.0.1:${address.port}`
  };
}

async function stopServer(server: Server): Promise<void> {
  if (!server.listening) {
    return;
  }

  server.closeAllConnections();
  await new Promise<void>((resolve, reject) => {
    server.close((error) => {
      if (error) {
        reject(error);
      } else {
        resolve();
      }
    });
  });
}

async function createMcpSession(gatewayUrl: string): Promise<McpSession> {
  const child = spawn(process.execPath, ["dist/index.js"], {
    cwd: process.cwd(),
    env: {
      ...process.env,
      AGENTLINK_GATEWAY_URL: gatewayUrl,
      AGENTLINK_TIMEOUT_MS: "2000"
    },
    stdio: ["pipe", "pipe", "pipe"]
  });

  child.stderr.setEncoding("utf8");
  child.stderr.on("data", (chunk) => {
    process.stderr.write(chunk);
  });

  const rl = createInterface({ input: child.stdout });
  const pending = new Map<number | string, (message: JsonRpcResponse) => void>();

  rl.on("line", (line) => {
    const message = JSON.parse(line) as JsonRpcResponse;
    if (message.id !== undefined) {
      pending.get(message.id)?.(message);
      pending.delete(message.id);
    }
  });

  let nextId = 1;
  const send = (method: string, params?: unknown): Promise<JsonRpcResponse> => {
    const id = nextId++;
    const response = new Promise<JsonRpcResponse>((resolve) => {
      pending.set(id, resolve);
    });
    child.stdin.write(`${JSON.stringify({ jsonrpc: "2.0", id, method, params })}\n`);
    return response;
  };

  const notify = (method: string, params?: unknown): void => {
    child.stdin.write(`${JSON.stringify({ jsonrpc: "2.0", method, params })}\n`);
  };

  const initialized = await send("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: {
      name: "agentlink-mcp-error-smoke",
      version: "0.2.0"
    }
  });
  if (initialized.error) {
    throw new Error(`initialize failed: ${JSON.stringify(initialized.error)}`);
  }
  notify("notifications/initialized");

  return {
    child,
    send,
    notify,
    close: async () => {
      child.stdin.end();
      child.kill();
      await once(child, "exit").catch(() => undefined);
    }
  };
}

async function expectToolError(label: string, session: McpSession, params: unknown, expectedText: string): Promise<void> {
  const response = await session.send("tools/call", params);
  const text = response.result?.content?.[0]?.text ?? "";

  if (response.error || response.result?.isError !== true || !text.includes(expectedText)) {
    throw new Error(`${label} did not return expected tool error: ${JSON.stringify(response)}`);
  }

  console.log(JSON.stringify({
    step: label,
    ok: true,
    expectedText
  }, null, 2));
}

function jsonResponse(response: ServerResponse, status: number, payload: unknown): void {
  const body = JSON.stringify(payload);
  response.writeHead(status, {
    "Content-Type": "application/json",
    "Content-Length": Buffer.byteLength(body)
  });
  response.end(body);
}

async function registryUnavailableCase(): Promise<void> {
  const { server, url } = await startServer((_request, response) => {
    jsonResponse(response, 503, { error: "registry unavailable" });
  });
  const session = await createMcpSession(url);

  try {
    await expectToolError(
      "registry_unavailable",
      session,
      {
        name: "agentlink_list_agents",
        arguments: {}
      },
      "registry unavailable"
    );
  } finally {
    await session.close();
    await stopServer(server);
  }
}

async function skillMissingCase(): Promise<void> {
  const { server, url } = await startServer((_request, response) => {
    jsonResponse(response, 502, { error: "agent_not_found" });
  });
  const session = await createMcpSession(url);

  try {
    await expectToolError(
      "skill_missing",
      session,
      {
        name: "agentlink_call_agent",
        arguments: {
          skill: "missing",
          message: "hello"
        }
      },
      "agent_not_found"
    );
  } finally {
    await session.close();
    await stopServer(server);
  }
}

async function malformedAgentCase(): Promise<void> {
  const gateway = await startServer((_request, response) => {
    response.writeHead(200, { "Content-Type": "application/json" });
    response.end("{not-json");
  });
  const session = await createMcpSession(gateway.url);

  try {
    await expectToolError(
      "malformed_agent_response",
      session,
      {
        name: "agentlink_call_agent",
        arguments: {
          skill: "math",
          message: "21 * 2"
        }
      },
      "invalid_response"
    );
  } finally {
    await session.close();
    await stopServer(gateway.server);
  }
}

async function messagesNotImplementedCase(): Promise<void> {
  const gateway = await startServer((_request, response) => {
    jsonResponse(response, 501, { error: "not_implemented" });
  });
  const session = await createMcpSession(gateway.url);

  try {
    await expectToolError(
      "messages_not_implemented",
      session,
      {
        name: "agentlink_send_message",
        arguments: {
          message: "hello"
        }
      },
      "not_implemented"
    );
  } finally {
    await session.close();
    await stopServer(gateway.server);
  }
}

await registryUnavailableCase();
await skillMissingCase();
await malformedAgentCase();
await messagesNotImplementedCase();
