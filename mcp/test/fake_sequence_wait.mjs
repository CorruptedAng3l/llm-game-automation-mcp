// Read-only protocol model for successive job-status observations. Other commands
// reuse the established fake CLI; no native target/input API is invoked.
import { appendFileSync, readFileSync } from "node:fs";
const argv = process.argv.slice(2), path = process.env.VINPUT_FAKE_SCENARIO, log = process.env.VINPUT_FAKE_LOG;
const scenario = JSON.parse(readFileSync(path, "utf8"));
if (argv.includes("job-status") && Array.isArray(scenario.sequence_observations)) {
  const before = readFileSync(log, "utf8").split("\n").filter(Boolean).map((s) => JSON.parse(s));
  const n = before.filter((r) => r.argv.includes("job-status")).length;
  appendFileSync(log, JSON.stringify({ argv, t: Date.now() }) + "\n");
  const r = scenario.sequence_observations[Math.min(n, scenario.sequence_observations.length - 1)] ?? { ok: false, error: "no modeled observation" };
  if (r._delay_ms) await new Promise((resolve) => setTimeout(resolve, r._delay_ms));
  const { _delay_ms, _exit, ...out } = r;
  process.stdout.write(JSON.stringify(out) + "\n");
  process.exit(_exit ?? (out.ok === false ? 1 : 0));
} else {
  await import("./fake_vinput.mjs");
}
