// `npm test`: the handshake test, then every test/t_*.mjs (MCP-layer tests against the fake vinput). Fails if any fails.
import { spawnSync } from "node:child_process";
import { readdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const files = ["handshake.mjs", ...readdirSync(here).filter((f) => /^t_.*\.mjs$/.test(f)).sort()];
let bad = 0;
for (const f of files) {
  console.log(`\n=== ${f}`);
  const r = spawnSync(process.execPath, [resolve(here, f)], { stdio: "inherit" });
  if (r.status !== 0) { bad++; console.log(`>>> ${f} FAILED (exit ${r.status})`); }
}
console.log(bad ? `\nnpm test: FAIL (${bad} of ${files.length} files)` : `\nnpm test: PASS (${files.length} files)`);
process.exit(bad ? 1 : 0);
