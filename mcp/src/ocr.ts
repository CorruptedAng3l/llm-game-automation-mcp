import type { CliResult } from './vinput.js';
/** Compact read-only OCR without dropping capture/QPC provenance or inventing confidence. */
export function presentOcr(result:CliResult,textOnly:boolean,retryEmpty:boolean):CliResult{
  if(!result.ok||!result.json||typeof result.json!=='object'||Array.isArray(result.json))return result;
  const native=result.json as Record<string,unknown>;
  const json:Record<string,unknown>=textOnly?Object.fromEntries([
    'ok','text','line_count','lang','ocr_empty_retry_version','ocr_compact_version','retry_empty',
    'ocr_scale','ocr_pass','ocr_passes','ocr_retry_errors','capture_scope','timing_clock','qpc_frequency','capture_qpc_ticks','frame_qpc_ticks',
  ].filter(k=>k in native).map(k=>[k,native[k]])):{...native};
  if(textOnly){json.text_only=true;json.ocr_confidence=null;}
  if(retryEmpty&&native.ocr_empty_retry_version!==1)
    json.ocr_retry_warning='This CLI did not confirm same-capture empty-result retries; update VINPUT_EXE for that capability.';
  return {...result,json};
}
