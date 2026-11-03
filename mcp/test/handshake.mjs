// MCP handshake test: start the BUILT server over stdio, initialize, tools/list, and check every tool has a schema
// and a description. Needs no game and no vinput binaries. Exit 0 = PASS.
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const server = resolve(here, "..", "build", "index.js");
const expected = ["launch", "attach", "list_targets", "status", "capture", "ocr", "find_text", "click_text", "wait_text",
  "find_image", "click_image", "wait_image", "pixel", "wait_change", "click", "move", "move_relative", "drag", "scroll",
  "key", "key_down", "key_up", "type_text", "pad", "list_keys", "run_script", "release", "detach",
  "config", "log", "trace"];

const transport = new StdioClientTransport({ command: process.execPath, args: [server], stderr: "pipe" });
let stderr = "";
transport.stderr?.on("data", (d) => { stderr += d; });
const client = new Client({ name: "handshake-test", version: "1.0.0" });
let failures = 0;
const check = (cond, what) => { console.log(`${cond ? "PASS" : "FAIL"} ${what}`); if (!cond) failures++; };
try {
  await client.connect(transport);
  const info = client.getServerVersion();
  check(info?.name === "llm-game-automation-mcp", `initialize: server name ${info?.name} ${info?.version}`);
  check(typeof client.getInstructions() === "string" && client.getInstructions().length > 50, "initialize: server instructions present");
  const { tools } = await client.listTools();
  const names = tools.map((t) => t.name);
  console.log(`tools/list: ${names.length} tools: ${names.join(", ")}`);
  for (const n of expected) check(names.includes(n), `tool ${n} listed`);
  for (const t of tools) {
    check(typeof t.description === "string" && t.description.length >= 30, `tool ${t.name} has a description`);
    check(t.inputSchema?.type === "object", `tool ${t.name} has an object input schema`);
  }
  const launch = tools.find((t) => t.name === "launch");
  check(!!launch?.inputSchema?.properties?.exe && !!launch?.inputSchema?.properties?.early, "launch schema exposes exe and early");
  const click = tools.find((t) => t.name === "click");
  check((click?.inputSchema?.required ?? []).includes("pid"), "click schema requires pid");
  // an argument error must come back as an explicit error, not silently
  const bad = await client.callTool({ name: "launch", arguments: {} });
  check(bad.isError === true && JSON.parse(bad.content[0].text).ok === false, "launch with no target returns isError + ok:false");
} catch (e) {
  failures++; console.log(`FAIL exception: ${e.stack ?? e}`); console.log(stderr);
} finally {
  await client.close().catch(() => {});
}
console.log(failures ? `handshake: FAIL (${failures})` : "handshake: PASS");
process.exit(failures ? 1 : 0);
