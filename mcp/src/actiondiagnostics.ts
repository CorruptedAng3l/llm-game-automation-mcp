import {mouseMessageHealth} from './status.js';
import type {CliResult} from './vinput.js';
export function presentMouseDiagnostics(r:CliResult):CliResult{
 const j=r.json as Record<string,unknown>|null;
 if(!j||typeof j!=='object'||Array.isArray(j)||j.mouse_message_health_version!==1)return r;
 const health=mouseMessageHealth({counters:j.mouse_message_counters});
 if(!health)return {...r,json:{...j,mouse_message_warning:'mouse message counters were incoherent or unavailable; no drop ratio was inferred'}};
 return {...r,json:{...j,mouse_messages:health,...(health.warning?{mouse_message_warning:health.warning}:{})}};
}
