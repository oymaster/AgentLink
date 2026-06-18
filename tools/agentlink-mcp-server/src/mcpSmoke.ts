import { spawn } from "node:child_process";
import { once } from "node:events";
import { createInterface } from "node:readline";

type JsonRpcResponse = {
  jsonrpc: "2.0";
  id?: number | string;
  result?: unknown;
  error?: unknown;
};

const gatewayUrl = process.env.AGENTLINK_GATEWAY_URL ?? "http://localhost:5002";
const message = process.env.AGENTLINK_SMOKE_MESSAGE ?? "21 * 2";
const skill = process.env.AGENTLINK_SMOKE_SKILL ?? "math";

const child = spawn(process.execPath, ["dist/index.js"], {
  cwd: process.cwd(),
  env: {
    ...process.env,
    AGENTLINK_GATEWAY_URL: gatewayUrl
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

function send(method: string, params?: unknown): Promise<JsonRpcResponse> {
  const id = nextId++;
  const payload = {
    jsonrpc: "2.0",
    id,
    method,
    params
  };

  const response = new Promise<JsonRpcResponse>((resolve) => {
    pending.set(id, resolve);
  });
  child.stdin.write(`${JSON.stringify(payload)}\n`);
  return response;
}

function notify(method: string, params?: unknown): void {
  child.stdin.write(`${JSON.stringify({ jsonrpc: "2.0", method, params })}\n`);
}

function assertNoError(label: string, response: JsonRpcResponse): void {
  if (response.error) {
    throw new Error(`${label} failed: ${JSON.stringify(response.error)}`);
  }
}

try {
  const initialized = await send("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: {
      name: "agentlink-mcp-smoke",
      version: "0.2.0"
    }
  });
  assertNoError("initialize", initialized);
  notify("notifications/initialized");

  const tools = await send("tools/list", {});
  assertNoError("tools/list", tools);
  console.log(JSON.stringify({ step: "tools_list", result: tools.result }, null, 2));

  const call = await send("tools/call", {
    name: "agentlink_call_agent",
    arguments: {
      skill,
      message
    }
  });
  assertNoError("tools/call", call);
  console.log(JSON.stringify({ step: "tools_call", result: call.result }, null, 2));
} finally {
  child.stdin.end();
  child.kill();
  await once(child, "exit").catch(() => undefined);
}
