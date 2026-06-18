import { AgentLinkClient, summarizeAgents } from "./agentlinkClient.js";
import { loadConfig } from "./config.js";

const config = loadConfig();
const client = new AgentLinkClient(config);
const skill = process.env.AGENTLINK_SMOKE_SKILL ?? "math";
const message = process.env.AGENTLINK_SMOKE_MESSAGE ?? "21 * 2";

try {
  const agents = await client.findAgents({ skill });
  console.log(JSON.stringify({
    step: "find_agents",
    gatewayUrl: config.gatewayUrl,
    count: agents.length,
    agents: summarizeAgents(agents)
  }, null, 2));

  if (agents.length === 0) {
    console.error(`No agents found for skill: ${skill}`);
    process.exit(2);
  }

  const response = await client.callAgent({ skill, message });
  console.log(JSON.stringify({
    step: "call_agent",
    selectedAgent: summarizeAgents([response.selectedAgent])[0],
    answer: response.answer
  }, null, 2));
} catch (error) {
  console.error(error);
  process.exit(1);
}
