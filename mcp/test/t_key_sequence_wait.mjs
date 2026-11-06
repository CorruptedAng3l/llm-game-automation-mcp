// SAME fake-only SDK contract on SF3 base and SF5 candidate.
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { cmdOf, makeChecker, startFake } from "./lib.mjs";
import { decodePng } from "../build/png.js";
const { state, check } = makeChecker(), pid = process.pid;
const steps = [{ action: "key", key: "w", ms: 150 }];
const idFor = (p) => `${p}:987654321:1`;
const initialFor = (p) => ({ ok: true, accepted: true, job_id: idFor(p), job_kind: "sequence", job_state: "queued", job_complete: false,
  owner: "mcp-test", requested_ms: 150, input_injected: null, transitions: 0 });
const jobFor = (p, job_state, extra = {}) => ({ ...initialFor(p), accepted: false, job_state,
  ok: job_state === "completed", job_complete: ["completed", "cancelled", "failed"].includes(job_state),
  job_reason: "", input_injected: job_state !== "queued", ...extra });
const scene = (extra = {}, p = pid) => ({ png: { w: 1280, h: 720, core_capture: true },
  replies: { status: { ok: true, pid: p, key_sequence_version: 1 }, jobs: { ok: true, jobs: [] }, seq: initialFor(p) },
  sequence_observations: ["queued", "running", "completed"].map((s) => ({ ok: true, job: jobFor(p, s) })), ...extra });
const env = { VINPUT_EXE: fileURLToPath(new URL("./fake_sequence_wait.mjs", import.meta.url)) };
const t = await startFake(scene(), env);
const set = (s) => { t.setScenario(s); t.clearCalls(); };
const requested = { pid, steps, wait: true, wait_timeout_ms: 2000, capture_after: 'immediate', max_width: 160 };
const exactInitial = (r, p = pid) => JSON.stringify(r.json?.sequence_input) === JSON.stringify(initialFor(p));
try {
  const p = (await t.client.listTools()).tools.find((x) => x.name === "key_sequence")?.inputSchema?.properties ?? {};
  check(p.wait?.type === "boolean" && p.wait_timeout_ms?.maximum === 120000 && p.wait_timeout_ms?.minimum === 1,
    "sequence advertises explicit wait and a finite 1..120000ms observation bound");
  let r = await t.call("key_sequence", { pid, steps });
  check(!r.isError && r.json?.job_state === "queued" && t.callsOf("seq").length === 1 && !t.callsOf("status").length &&
    !t.callsOf("jobs").length && !t.callsOf("job-status").length && !t.callsOf("capture").length,
    "default scheduling-only path is unchanged and adds no wait/probe/capture");

  set(scene()); r = await t.call("key_sequence", requested);
  const order = t.calls().map((a) => cmdOf(a)[0]);
  check(!r.isError && r.json?.job_state === "completed" && r.json.job_complete === true && r.json.sequence_wait?.outcome === "completed" &&
    r.json.sequence_wait.completed === true && r.json.accepted === undefined && r.json.note === undefined,
    "wait returns matching native completion without the stale scheduling hint", r.text);
  check(exactInitial(r) && r.json?.sequence_wait?.game_consumption_measured === false && r.json.transitions === 0,
    "original scheduling receipt is preserved and no game consumption is invented", r.text);
  check(order.join() === "status,jobs,seq,job-status,job-status,job-status,capture" && t.callsOf("seq").length === 1 &&
    t.callsOf("job-status").every((a) => cmdOf(a)[1] === idFor(pid) && a.includes("mcp-test")),
    "preflight then exact-ID polling completes before the single action image", JSON.stringify(order));
  let im; try { im = decodePng(Buffer.from(r.image.data, "base64")); } catch {}
  check(im?.width === 160 && im.height === 90 && r.json?.capture_after?.observation?.action_state === "native_sequence_completed_game_consumption_unmeasured",
    "post-completion capture preserves SF3 sizing and honest boundary label", r.text);

  set(scene());r=await t.call("key_sequence",{pid,steps:[{action:"retap",key:"w+shift",retap_key:"shift",ms:1500,every_ms:300,gap_ms:10}],capture_after:false});
  check(!r.isError&&t.lastSeqText()==="retap w+shift shift 1500 300 10\n"&&t.callsOf("seq").length===1,"retap serializes one bounded chord hold with exact selected key and gap");
  for(const badStep of [{action:"wait",ms:100,retap_key:"shift"},{action:"key",key:"w",ms:100,every_ms:50},{action:"retap",key:"w+shift",ms:1000,every_ms:300},{action:"retap",key:"w+shift",retap_key:"shift",ms:300,every_ms:300},{action:"retap",key:"w+shift",retap_key:"shift",ms:1000,every_ms:10,gap_ms:10}]){
    set(scene());r=await t.call("key_sequence",{pid,steps:[badStep]});check(r.isError&&!t.calls().length,"invalid retap options refuse before any CLI request");
  }

  set(scene()); r = await t.call("key_sequence", { pid, steps: [{action:"key",key:"down",ms:80},{action:"wait",ms:100},{action:"key",key:"enter",ms:80}],
    wait:true,capture_settle_ms:50,capture_after:'immediate',region:{x:100,y:40,w:320,h:180},max_width:160 });
  check(!r.isError&&t.lastSeqText()==="key down 80\nwait 100\nkey enter 80\n"&&t.callsOf("seq").length===1&&t.callsOf("capture").length===1&&
    r.json.capture_after.observation.action_state==="native_sequence_completed_game_consumption_unmeasured"&&r.json.capture_after.observation.settle_ms_elapsed>=40,
    "menu recipe sends one sequence, waits for native completion, settles and returns one cropped image without replay",r.text);

  set(scene({ sequence_observations: [{ ok: true, job: jobFor(pid, "completed") }] }));
  r = await t.call("key_sequence", { pid, steps, wait: true });
  check(!r.isError && r.json?.sequence_wait?.timeout_ms === 5150 && r.json.sequence_wait.outcome === "completed" && !t.callsOf("capture").length,
    "omitted budget derives from native requested duration; image remains opt-in", r.text);

  for (const [nativeState, reason, outcome] of [["cancelled", "explicit_cancel", "cancelled"], ["cancelled", "explicit_replacement", "replaced"], ["failed", "worker_exception", "failed"]]) {
    set(scene({ sequence_observations: [{ ok: true, job: jobFor(pid, nativeState, { job_reason: reason }) }] }));
    r = await t.call("key_sequence", requested);
    check(r.isError && exactInitial(r) && r.json?.job_state === nativeState && r.json.sequence_wait?.outcome === outcome &&
      r.json.sequence_wait.completed === false && r.json.sequence_wait.job_may_still_be_active === false && !t.callsOf("capture").length && !t.callsOf("job-cancel").length,
      `${outcome} is a terminal error with original receipt and no replay/capture/cancel`, r.text);
  }
  set(scene({ sequence_observations: [{ ok: true, job: jobFor(pid, "running"), _delay_ms: 250 }] }));
  r = await t.call("key_sequence", { ...requested, wait_timeout_ms: 50 });
  check(r.isError && exactInitial(r) && r.json?.sequence_wait?.outcome === "timeout" && r.json.sequence_wait.completed === false &&
    r.json.sequence_wait.job_may_still_be_active === true && t.callsOf("seq").length === 1 && !t.callsOf("job-cancel").length && !t.callsOf("capture").length,
    "timeout retains the admitted job and neither cancels nor replays input", r.text);

  for (const [label, patch] of [
    ["generation", { job_id: `${pid}:987654322:1` }], ["serial", { job_id: `${pid}:987654321:2` }],
    ["PID", { job_id: `${pid + 1}:987654321:1` }], ["owner", { owner: "another-owner" }], ["kind", { job_kind: "pad" }],
    ["complete type", { job_complete: "true" }], ["terminal contradiction", { job_complete: false }], ["nested ok contradiction", { ok: false }],
  ]) {
    set(scene({ sequence_observations: [{ ok: true, job: jobFor(pid, "completed", patch) }] }));
    r = await t.call("key_sequence", requested);
    check(r.isError && exactInitial(r) && r.json?.sequence_wait?.outcome === "invalid_status" && !t.callsOf("capture").length && t.callsOf("seq").length === 1,
      `mismatched ${label} never establishes completion`, r.text);
  }
  for (const [label, response] of [
    ["pruned job", { ok: false, error: "unknown job_id (history pruned)" }],
    ["transport failure", { ok: true, job: jobFor(pid, "completed"), _exit: 2 }],
  ]) {
    set(scene({ sequence_observations: [response] })); r = await t.call("key_sequence", requested);
    check(r.isError && exactInitial(r) && r.json?.sequence_wait?.outcome === "status_unavailable" && !t.callsOf("capture").length && t.callsOf("seq").length === 1,
      `${label} preserves initial input without fabricating completion`, r.text);
  }
  for (const [label, initial] of [
    ["missing generation", { ...initialFor(pid), job_id: "j1" }], ["wrong initial owner", { ...initialFor(pid), owner: "other" }],
    ["missing complete flag", { ...initialFor(pid), job_complete: undefined }],
  ]) {
    set(scene({ replies: { ...scene().replies, seq: initial } })); r = await t.call("key_sequence", requested);
    check(r.isError && r.json?.sequence_wait?.outcome === "invalid_ack" && r.json.sequence_input?.job_id === initial.job_id &&
      t.callsOf("seq").length === 1 && !t.callsOf("job-status").length && !t.callsOf("capture").length,
      `${label} is honest post-scheduling uncertainty, never a retry`, r.text);
  }
  for (const [label, initial, outcome, active] of [
    ["uncertain ACK", { ok: false, input_injected: null, delivery_unknown: true, may_have_started: true, reply_raw: "partial ACK", error: "ack unavailable" }, "ack_unavailable", true],
    ["explicit scheduling refusal", { ok: false, input_injected: false, error: "resource busy" }, "scheduling_refused", false],
  ]) {
    set(scene({ replies: { ...scene().replies, seq: initial } })); r = await t.call("key_sequence", requested);
    check(r.isError && JSON.stringify(r.json?.sequence_input) === JSON.stringify(initial) && r.json.sequence_wait?.outcome === outcome &&
      r.json.sequence_wait.job_may_still_be_active === active && t.callsOf("seq").length === 1 && !t.callsOf("job-status").length && !t.callsOf("job-cancel").length && !t.callsOf("capture").length,
      `${label} preserves the actual post-scheduling reply and uncertainty without retry`, r.text);
  }
  for (const [label, replies] of [
    ["missing version", { status: { ok: true, pid } }], ["string version", { status: { ok: true, pid, key_sequence_version: "1" } }],
    ["wrong target", { status: { ok: true, pid: pid + 1, key_sequence_version: 1 } }],
    ["old endpoint", { jobs: { ok: false, error: "unknown command jobs" } }], ["malformed endpoint", { jobs: { ok: true, jobs: {} } }],
  ]) {
    set(scene({ replies: { ...scene().replies, ...replies } })); r = await t.call("key_sequence", requested);
    check(r.isError && r.json?.input_injected === false && t.callsOf("seq").length === 0 && !t.callsOf("capture").length,
      `unsupported ${label} is refused before sequence scheduling`, r.text);
  }
  for (const options of [{ wait_timeout_ms: 100 }, { wait: false, wait_timeout_ms: 100 }, { wait: true, wait_timeout_ms: 0 },
    { wait: true, wait_timeout_ms: 120001 }, { wait: "true" }, { wait: true, capture_after: false, max_width: 160 }]) {
    set(scene()); r = await t.call("key_sequence", { pid, steps, ...options });
    check(r.isError && !t.calls().length, `invalid wait/image options fail before CLI I/O: ${JSON.stringify(options)}`, r.text);
  }
  set(scene({ sequence_observations: [{ ok: true, job: jobFor(pid, "running"), _delay_ms: 350 }] }));
  const pending = t.call("key_sequence", requested);
  for (let i = 0; i < 200 && !t.callsOf("job-status").length; i++) await new Promise((resolve) => setTimeout(resolve, 10));
  await t.call("detach", { pid }); r = await pending;
  check(r.isError && exactInitial(r) && r.json?.sequence_wait?.outcome === "attachment_changed" && t.callsOf("seq").length === 1 &&
    !t.callsOf("capture").length && !t.callsOf("job-cancel").length,
    "detach during polling ends observation with its original receipt and no cancellation", r.text);
} finally { await t.close(); }

// An owned finite Node model exits via its original IPC channel; no PID reopen,
// terminate/kill, native UI or input operation is used. Its5s fallback is finite.
for (const ackFailure of [false, true]) {
  const actor = spawn(process.execPath, ["-e", "process.on('message',m=>{if(m==='exit')process.exit(0)});setTimeout(()=>process.exit(0),5000)"],
    { windowsHide: true, stdio: ["ignore", "ignore", "ignore", "ipc"] });
  const actorExit = new Promise((resolve) => {
    const bound = setTimeout(() => resolve("exit_unobserved"), 6000);
    actor.once("exit", (code) => { clearTimeout(bound); resolve(code); });
    actor.once("error", () => { clearTimeout(bound); resolve("spawn_error"); });
  });
  const uncertain = { ok: false, input_injected: null, delivery_unknown: true, may_have_started: true, reply_raw: "retained partial ACK", error: "ack unavailable" };
  const s = scene({ sequence_observations: [{ ok: false, error: "target gone", _delay_ms: 350 }] }, actor.pid);
  if (ackFailure) s.replies.seq = { ...uncertain, _delay_ms: 350 };
  const exitTarget = await startFake(s, env);
  try {
    const pending = exitTarget.call("key_sequence", { pid: actor.pid, steps, wait: true, wait_timeout_ms: 4000 });
    const observed = ackFailure ? "seq" : "job-status";
    for (let i = 0; i < 200 && !exitTarget.callsOf(observed).length; i++) await new Promise((resolve) => setTimeout(resolve, 10));
    if (actor.connected) actor.send("exit", () => {});
    const r = await pending;
    await actorExit;
    const preserved = ackFailure ? JSON.stringify(r.json?.sequence_input) === JSON.stringify(uncertain) &&
      r.json.delivery_unknown === true && r.json.may_have_started === true && r.json.reply_raw === "retained partial ACK" : exactInitial(r, actor.pid);
    check(r.isError && preserved && r.json?.target_exited === true && r.json.exit_code === 0 &&
      r.json.sequence_wait?.outcome === (ackFailure ? "ack_unavailable" : "status_unavailable") && exitTarget.callsOf("seq").length === 1 &&
      !exitTarget.callsOf("job-cancel").length && !exitTarget.callsOf("capture").length,
      ackFailure ? "confirmed exit cannot discard native uncertain ACK fields" : "confirmed target exit cannot replace the already-scheduled sequence receipt", r.text);
  } finally { await exitTarget.close(); await actorExit; }
}
console.log(`key sequence wait: ${state.passes} PASS / ${state.failures} FAIL`);
process.exit(state.failures ? 1 : 0);
