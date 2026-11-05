// Shared helpers for the MCP-layer tests: start the built server against the fake vinput and call tools.
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { mkdtempSync, readFileSync, writeFileSync, existsSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
export const serverJs = process.env.VINPUT_TEST_SERVER ? resolve(process.env.VINPUT_TEST_SERVER) : resolve(here, "..", "build", "index.js");
export const fakeExe = resolve(here, "fake_vinput.mjs");
let latestFakeCall;

export function makeChecker() {
  const state = { failures: 0, passes: 0 };
  const check = (cond, what, detail = "") => {
    console.log(`${cond ? "PASS" : "FAIL"} ${what}${!cond && detail ? " :: " + String(detail).slice(0, 400) : ""}`);
    if (!cond && latestFakeCall) {
      // Diagnostics only: preserve the latest owned fixture reply without changing
      // assertions, transport timeouts, guard leases, input or retry behavior.
      try {
        const encoded = JSON.stringify(latestFakeCall());
        console.error("LATEST_FAKE_CALL " + (encoded.length <= 65536 ? encoded : JSON.stringify({ truncated: true, original_characters: encoded.length, diagnostic_json_prefix: encoded.slice(0, 65536) })));
      } catch (e) { console.error("LATEST_FAKE_CALL " + JSON.stringify({ diagnostic_unavailable: String(e) })); }
    }
    if (cond) state.passes++; else state.failures++;
    return cond;
  };
  return { state, check };
}

/** Start the server against the fake vinput. scenario = the fake's JSON scenario (rewrite it with setScenario). */
export async function startFake(scenario = {}, extraEnv = {}) {
  const dir = mkdtempSync(join(tmpdir(), "lgam-mcp-test-"));
  const logFile = join(dir, "calls.jsonl"), scenFile = join(dir, "scenario.json");
  writeFileSync(scenFile, JSON.stringify(scenario)); writeFileSync(logFile, "");
  const transport = new StdioClientTransport({
    command: process.execPath, args: [serverJs], stderr: "pipe",
    env: { ...process.env, VINPUT_EXE: fakeExe, VINPUT_FAKE_LOG: logFile, VINPUT_FAKE_SCENARIO: scenFile,
      VINPUT_CAPTURE_DIR: dir, VINPUT_OWNER: "mcp-test", ...extraEnv },
  });
  const client = new Client({ name: "mcp-unit", version: "1.0.0" });
  await client.connect(transport);
  const api = {
    dir, client,
    setScenario: (s) => writeFileSync(scenFile, JSON.stringify(s)),
    /** like calls() but with the time (ms) each call started; the exit-watcher helper (waitexit) is not a tool call */
    timedCalls() { return readFileSync(logFile, "utf8").split("\n").filter(Boolean).map((x) => JSON.parse(x)).filter((c) => !c.argv.includes("waitexit")); },
    calls() { return this.timedCalls().map((c) => c.argv); },
    /** text of the sequence file passed to the last `seq` call */
    lastSeqText() { return this.timedCalls().filter((c) => c.seqText !== undefined).at(-1)?.seqText; },
    /** recorded calls whose vinput command is `name` */
    callsOf(name) { return this.calls().filter((a) => cmdOf(a)[0] === name); },
    clearCalls: () => writeFileSync(logFile, ""),
    /** call a tool; returns {r, json, text, image, texts} */
    async call(name, args = {}) {
      const r = await client.callTool({ name, arguments: args }, undefined, { timeout: 60000 });
      const texts = r.content.filter((c) => c.type === "text").map((c) => c.text);
      let json = null; try { json = JSON.parse(texts[0] ?? ""); } catch { /* leave null */ }
      latestFakeCall = () => {
        const calls = api.timedCalls();
        return { scope: "latest owned fake-CLI tool call; asynchronous assertions may concern another in-flight call", name, arguments: args, isError: !!r.isError, texts, image_count: r.content.filter(c => c.type === "image").length, fixture_call_count: calls.length, fixture_log_tail: calls.slice(-64) };
      };
      return { r, json, text: texts.join("\n"), texts, image: r.content.find((c) => c.type === "image"), isError: !!r.isError };
    },
    async close() { await client.close().catch(() => {}); try { rmSync(dir, { recursive: true, force: true }); } catch { /* ignore */ } },
  };
  return api;
}
/** the vinput command (first non-flag word) and its args, from one recorded argv */
export const cmdOf = (argv) => {
  const a = argv.filter((x) => x !== "--json");
  const out = []; for (let i = 0; i < a.length; i++) { if (a[i] === "--owner" || a[i] === "--pid") { i++; continue; } out.push(a[i]); }
  return out;
};
export { existsSync };

/** A realistic full vinput status reply (shape taken from vinput_core.cpp StatusJson). */
export function fullStatus(over = {}, counters = {}) {
  return {
    ok: true, version: "x", pid: 4242, bits: 64, app_container: false, anticheat_disabled: false, hwnd: "0x1A2B", class: "GameWnd",
    title: "My Game", client_w: 1280, client_h: 720, subclassed: true, really_foreground: false, fakefocus: true, autoactivate: true,
    app_active: 1, isolate: true, blockfocus: true, nofullscreen: true, keepvisible: true, minimized: false, cursor_mode: 0, rawmode: 0,
    msgmode: 0, vcur_x: 10, vcur_y: 20, raw_kbd: false, raw_kbd_nolegacy: false, raw_mouse: true, raw_mouse_nolegacy: false,
    swapchain_w: 1280, swapchain_h: 720, swapchain_fmt: 28, swapchain_api: 11, frame_api: "d3d11", frames: 500,
    capture_path: "d3d11", frames_by_api: { d3d11: 500, d3d12: 0, gl: 0, vk: 0, foreign_dxgi: 0 },
    d3d12: { queue_source: 0, ecl_hook: false, submits: 0, releases: 0, leaks: 0, resizes: 0 }, gl_hooked: false,
    vk: { hooked: false, enabled: true, devices: 0, queues: 0, swapchains: 0, usage_added: 0, presents: 0, presents_other_window: 0, submits: 0, releases: 0 },
    virtual_down: [], hooks_ok: "Present", hooks_failed: "", uptime_ms: 12345, log_path: "C:\\x\\logs\\vinput_4242.log",
    claim: { active: true, owner: "mcp-test", expires_in_ms: 1000, ttl_ms: 3600000, held_ms: 5, refused: 0 },
    counters: { gaks: 0, gks: 0, gkbs: 0, gcur: 0, scur: 0, clip: 0, gfg: 0, setfg: 0, setfg_blocked: 0, rid: 0, rid_magic: 0, rib: 0,
      rib_injected: 0, reg_raw: 0, di_kbd: 0, di_mouse: 0, di_data_kbd: 0, di_data_mouse: 0, di_acq: 0, di_fake: 0, xinput: 0, wm_key: 0,
      wm_char: 0, wm_mouse: 0, wm_input: 0, wm_input_dropped: 0, wm_mouse_dropped: 0, act_swallowed: 0, act_synth: 0, min_blocked: 0,
      min_restored: 0, present: 500, present_ours: 500, captures: 0, posted: 0, raw_posted: 0, raw_queued: 0, fs_refused: 0, ...counters },
    ...over,
  };
}
