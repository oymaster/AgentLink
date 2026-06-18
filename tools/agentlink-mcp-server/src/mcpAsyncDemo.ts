import { AgentLinkClient } from "./agentlinkClient.js";
import { loadConfig } from "./config.js";
import { TaskRuntime, type TaskView } from "./taskRuntime.js";

const runtime = new TaskRuntime(new AgentLinkClient(loadConfig()));

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function waitForTask(taskId: string, skill: string): Promise<TaskView> {
  const startedAt = Date.now();
  let nextProgressAt = 5_000;
  let view = await runtime.getTask(taskId);
  for (let attempt = 0; attempt < 600 && !["completed", "failed"].includes(view.state); attempt += 1) {
    await sleep(100);
    view = await runtime.getTask(taskId);
    const elapsedMs = Date.now() - startedAt;
    if (elapsedMs >= nextProgressAt) {
      console.error(JSON.stringify({ step: "waiting", skill, taskId, state: view.state, elapsedMs }));
      nextProgressAt += 5_000;
    }
  }
  if (view.state !== "completed") throw new Error(`task ${taskId} ended in ${view.state}: ${JSON.stringify(view.error)}`);
  return view;
}

async function runStage(skill: string, message: string): Promise<TaskView> {
  const startedAt = Date.now();
  const handle = await runtime.createTask({ skill, message });
  console.log(JSON.stringify({ step: "created", skill, ...handle }));
  const view = await waitForTask(handle.taskId, skill);
  const stream = await runtime.streamTask(handle.taskId, 0);
  console.log(JSON.stringify({
    step: "events",
    skill,
    elapsedMs: Date.now() - startedAt,
    events: (stream.events ?? []).map((event) => ({ sequence: event.sequence, type: event.type }))
  }));
  return view;
}

const architecture = await runStage(
  "architecture",
  process.env.AGENTLINK_ARCHITECTURE_PROMPT ?? "为一个线程安全的 C++ LRU Cache 给出最小架构、接口和验证方案。"
);
const coding = await runStage(
  "coding",
  `根据下面的架构方案给出实现和测试步骤： ${(architecture.answer ?? "").replace(/\s+/g, " ")}`
);
console.log(JSON.stringify({ step: "completed", architecture: architecture.answer, coding: coding.answer }, null, 2));
