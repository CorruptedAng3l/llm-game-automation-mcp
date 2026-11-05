import type {CliResult} from './vinput.js';
/** Native --any shares every captured frame/OCR pass across all alternatives. */
export function waitTextQuery(text:string|string[]):{argv:string[];queries?:string[];error?:string}{
 if(typeof text==='string')return {argv:[text]};
 if(!text.length||text.length>16||text.some(t=>!t.trim()||t.length>256||t.includes('|')||t.includes('\0')))return {argv:[],error:'text alternatives need1..16 nonempty strings of at most256 characters, without | or NUL'};
 const queries=[...new Set(text)];return {argv:['--any',queries.join('|')],queries};
}
export function presentWaitTextAny(result:CliResult,queries?:string[]):CliResult{
 if(!queries||!result.json||typeof result.json!=='object'||Array.isArray(result.json))return result;
 const native=result.json as Record<string,unknown>,match=native.match&&typeof native.match==='object'?native.match as Record<string,unknown>:undefined;
 const matched=typeof match?.query==='string'&&queries.includes(match.query)?match.query:null;
 if(result.ok&&native.found===true&&!matched)return {...result,ok:false,json:{...native,ok:false,error:'native any-text match did not identify a requested alternative',wait_text_input:native,queries,matched_text:null}};
 return {...result,json:{...native,queries,matched_text:matched,matching_mode:'any_of_shared_capture_ocr',scope:'one native wait-text poll captures once and applies all alternatives to the same OCR readings; fallback OCR passes may reuse that image'}};
}
