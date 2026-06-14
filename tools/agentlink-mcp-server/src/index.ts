#!/usr/bin/env node

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { AgentLinkClient, AgentLinkError, summarizeAgents } from "./agentlinkClient.js";
import { loadConfig } from "./config.js";
import { RedisEventLog } from "./taskEvents.js";
import { TaskRuntime } from "./taskRuntime.js";

const config = loadConfig();
const client = new AgentLinkClient(config);
const eventLog = new RedisEventLog(config.redisUrl);
const taskRuntime = new TaskRuntime(client, eventLog);

const server = new McpServer({
  name: "agentlink-mcp-server",
  version: "0.1.0"
});

function textResult(payload: unknown) {
  return {
    content: [
      {
        type: "text" as const,
        text: typeof payload === "string" ? payload : JSON.stringify(payload, null, 2)
      }
    ]
  };
}

function errorResult(error: unknown) {
  const payload = error instanceof AgentLinkError
    ? { error: error.code, message: error.message, details: error.details }
    : { error: "internal_error", message: (error as Error).message };

  return {
    isError: true,
    ...textResult(payload)
  };
}

server.tool(
  "agentlink_list_agents",
  "List agents registered in AgentLink Registry.",
  {},
  async () => {
    try {
      const agents = await client.listAgents();
      return textResult({
        registryUrl: config.registryUrl,
        count: agents.length,
        agents: summarizeAgents(agents)
      });
    } catch (error) {
      return errorResult(error);
    }
  }
);

server.tool(
  "agentlink_find_agents",
  "Find AgentLink agents by skill or tag.",
  {
    skill: z.string().optional().describe("Skill name, for example math."),
    tag: z.string().optional().describe("Tag name, for example remote.")
  },
  async ({ skill, tag }) => {
    try {
      const agents = await client.findAgents({ skill, tag });
      return textResult({
        registryUrl: config.registryUrl,
        query: { skill, tag },
        count: agents.length,
        agents: summarizeAgents(agents)
      });
    } catch (error) {
      return errorResult(error);
    }
  }
);

server.tool(
  "agentlink_send_message",
  "Send a Claude-compatible /v1/messages request to AgentLink Gateway.",
  {
    message: z.string().describe("User message text."),
    model: z.string().optional().describe("Optional model name passed to /v1/messages."),
    system: z.string().optional().describe("Optional system prompt."),
    maxTokens: z.number().int().positive().optional().describe("Maximum output tokens.")
  },
  async ({ message, model, system, maxTokens }) => {
    try {
      const response = await client.sendMessage({ message, model, system, maxTokens });
      return textResult({
        messagesUrl: config.messagesUrl,
        response: response.raw
      });
    } catch (error) {
      return errorResult(error);
    }
  }
);

server.tool(
  "agentlink_call_agent",
  "Find an AgentLink agent by skill and call it through A2A message/send.",
  {
    skill: z.string().describe("Skill name used for Registry lookup."),
    message: z.string().describe("User message sent to the selected agent."),
    tag: z.string().optional().describe("Optional tag filter applied after skill lookup."),
    historyLength: z.number().int().min(0).optional().describe("A2A historyLength value.")
  },
  async ({ skill, message, tag, historyLength }) => {
    try {
      const response = await client.callAgent({ skill, message, tag, historyLength });
      return textResult({
        selectedAgent: summarizeAgents([response.selectedAgent])[0],
        answer: response.answer,
        raw: response.raw
      });
    } catch (error) {
      return errorResult(error);
    }
  }
);

server.tool(
  "agentlink_create_task",
  "Create an async AgentLink task: dispatch to a remote agent in the background and return a task handle immediately (non-blocking). Use agentlink_get_task / agentlink_stream_task to follow progress.",
  {
    skill: z.string().describe("Skill name used for Registry lookup, e.g. math."),
    message: z.string().describe("User message sent to the selected agent."),
    tag: z.string().optional().describe("Optional tag filter applied after skill lookup.")
  },
  async ({ skill, message, tag }) => {
    try {
      const handle = await taskRuntime.createTask({ skill, message, tag });
      return textResult(handle);
    } catch (error) {
      return errorResult(error);
    }
  }
);

server.tool(
  "agentlink_get_task",
  "Get the current state and result of an AgentLink task by task_id (reads the durable Redis event log; survives bridge restarts).",
  {
    taskId: z.string().describe("Task id returned by agentlink_create_task.")
  },
  async ({ taskId }) => {
    try {
      return textResult(await taskRuntime.getTask(taskId));
    } catch (error) {
      return errorResult(error);
    }
  }
);

server.tool(
  "agentlink_stream_task",
  "Return ordered lifecycle events for a task. Pass sinceSequence to fetch only newer events (incremental polling).",
  {
    taskId: z.string().describe("Task id returned by agentlink_create_task."),
    sinceSequence: z.number().int().min(0).optional().describe("Only return events with sequence greater than this value.")
  },
  async ({ taskId, sinceSequence }) => {
    try {
      return textResult(await taskRuntime.streamTask(taskId, sinceSequence ?? 0));
    } catch (error) {
      return errorResult(error);
    }
  }
);

server.tool(
  "agentlink_cancel_task",
  "Best-effort cancel of an in-flight task. Terminal tasks (completed/failed) are a no-op.",
  {
    taskId: z.string().describe("Task id returned by agentlink_create_task.")
  },
  async ({ taskId }) => {
    try {
      return textResult(await taskRuntime.cancelTask(taskId));
    } catch (error) {
      return errorResult(error);
    }
  }
);

const transport = new StdioServerTransport();
await server.connect(transport);
