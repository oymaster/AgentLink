import { AgentLinkClient } from "./agentlinkClient.js";
import { loadConfig } from "./config.js";
import { RedisEventLog } from "./taskEvents.js";
import { TaskRuntime } from "./taskRuntime.js";

// Exercises the async task lifecycle end to end:
//   create_task -> poll get_task until terminal -> stream_task
// Requires a running registry + remote agent + Redis (same as the C++ demo).
const config = loadConfig();
const client = new AgentLinkClient(config);
const eventLog = new RedisEventLog(config.redisUrl);
const runtime = new TaskRuntime(client, eventLog);

const skill = process.env.AGENTLINK_SMOKE_SKILL ?? "math";
const message = process.env.AGENTLINK_SMOKE_MESSAGE ?? "21 * 2";

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

try {
  const handle = await runtime.createTask({ skill, message });
  console.log(JSON.stringify({ step: "create_task", ...handle }, null, 2));

  let view = await runtime.getTask(handle.taskId);
  for (let i = 0; i < 50 && view.state !== "completed" && view.state !== "failed"; i += 1) {
    await sleep(100);
    view = await runtime.getTask(handle.taskId);
  }

  console.log(JSON.stringify({ step: "get_task", ...view }, null, 2));

  const stream = await runtime.streamTask(handle.taskId, 0);
  console.log(
    JSON.stringify(
      {
        step: "stream_task",
        eventCount: stream.eventCount,
        sequence: (stream.events ?? []).map((event) => `${event.sequence}:${event.type}`)
      },
      null,
      2
    )
  );

  await eventLog.close();

  if (view.state !== "completed") {
    console.error(`Task did not complete, final state: ${view.state}`);
    process.exit(1);
  }
} catch (error) {
  console.error(error);
  await eventLog.close().catch(() => undefined);
  process.exit(1);
}
