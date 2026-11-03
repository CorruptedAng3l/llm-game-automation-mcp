// Fix 4a: no silent hangs. launch / capture notice "nothing presented + fullscreen refusals piling up" and say what to do.
import { cmdOf, fullStatus, makeChecker, startFake } from "./lib.mjs";
const { state, check } = makeChecker();
const pid = process.pid;
const stuck = fullStatus({ frames: 0 }, { present: 0, present_ours: 0, fs_refused: 60 });
const quiet = fullStatus({ frames: 0 }, { present: 0, present_ours: 0, fs_refused: 0 });
const healthy = fullStatus();
const ok = { launch: { ok: true, pid, injected: true, foreground_restored: true } };
const t = await startFake({ replies: { ...ok, status: stuck } });
const statusCalls = () => t.calls().filter((c) => cmdOf(c)[0] === "status").length;
try {
  // ---- launch
  const L = await t.call("launch", { exe: "C:/g/game.exe" });
  const warn = (L.json?.warnings ?? []).join(" ");
  check(!L.isError && L.json?.ok === true && L.json?.pid === pid, "launch with refusals still returns ok + pid (the process runs)", L.text.slice(0, 300));
  check(/windowed|borderless/i.test(warn) && /fullscreen/i.test(warn) && /settings|INI/i.test(warn), "launch warns: fullscreen refused, set windowed/borderless in the game's settings or INI", warn);
  check(L.json?.fullscreen_refusals === 60 && L.json?.nothing_presented === true, "launch reports fullscreen_refusals and nothing_presented", L.text.slice(0, 300));

  t.setScenario({ replies: { ...ok, status: healthy } });
  const H = await t.call("launch", { exe: "C:/g/game.exe" });
  check(!H.json?.warnings && !H.json?.nothing_presented, "launch of a healthy game adds no warnings", H.text.slice(0, 300));

  t.setScenario({ replies: { ...ok, status: quiet } });
  const Q = await t.call("launch", { exe: "C:/g/game.exe" });
  check(Q.json?.nothing_presented === true && !Q.json?.warnings && /DirectDraw|PrintWindow|loading/i.test(JSON.stringify(Q.json?.notes ?? "")),
    "present==0 with no refusals: a note (loading / GDI-DirectDraw games are fine), not a warning", Q.text.slice(0, 400));

  // ---- capture
  const cap = { ok: true, method: "printwindow", w: 1280, h: 720, note: "native failed" };
  t.setScenario({ replies: { capture: cap, status: stuck } });
  const C = await t.call("capture", { pid });
  check(C.isError === true && /fullscreen/i.test(C.text) && /windowed|borderless/i.test(C.text) && !C.image, "capture on a refusal loop: error naming the cause and the fix, no black image", C.text.slice(0, 400));
  check(C.json?.fullscreen_refusals === 60, "capture error carries fullscreen_refusals");

  t.setScenario({ replies: { capture: { ok: false, error: "no frame (Present / SwapBuffers / vkQueuePresentKHR) for the game window within timeout" }, status: stuck } });
  const F = await t.call("capture", { pid });
  check(F.isError && /fullscreen/i.test(F.text) && /no frame/.test(F.text), "a failed capture also gets the diagnosis, keeping vinput's own error", F.text.slice(0, 400));

  t.setScenario({ replies: { capture: cap, status: healthy } });
  const P = await t.call("capture", { pid });
  check(!P.isError && !!P.image, "PrintWindow capture on a game that does present: image returned", P.text.slice(0, 300));

  t.setScenario({ replies: { capture: cap, status: quiet } });
  const N = await t.call("capture", { pid });
  check(!N.isError && !!N.image && N.json?.nothing_presented === true, "PrintWindow capture, nothing presented, no refusals: image + nothing_presented flag", N.text.slice(0, 300));

  t.setScenario({ replies: { capture: { ok: true, method: "swapchain", w: 1, h: 1 }, status: stuck } });
  t.clearCalls();
  const G = await t.call("capture", { pid });
  check(!G.isError && !!G.image && statusCalls() === 0, "a native capture costs no extra status call", String(statusCalls()));
} finally { await t.close(); }
console.log(state.failures ? `diag: FAIL (${state.failures})` : `diag: PASS (${state.passes})`);
process.exit(state.failures ? 1 : 0);
