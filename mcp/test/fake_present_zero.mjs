// Node-only frame-gate control. All other behavior stays in the existing fake.
// No game/native executable or input API is used.
import { appendFileSync } from "node:fs";
const argv=process.argv.slice(2),at=argv.indexOf("--after-frames");
if(argv.includes("capture")&&at>=0&&Number(argv[at+1])>0){
 if(process.env.VINPUT_FAKE_LOG)appendFileSync(process.env.VINPUT_FAKE_LOG,JSON.stringify({argv,t:Date.now()})+"\n");
 process.stdout.write(JSON.stringify({ok:false,error:"not enough target Present calls before capture timeout"})+"\n");
 process.exit(1);
}
await import("./fake_vinput.mjs");
