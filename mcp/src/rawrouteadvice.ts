type Obj=Record<string,unknown>;
const object=(v:unknown):Obj|undefined=>v!==null&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
/** Read-only hint for an explicit mode conflict on the actual mouse destination.
 * A global GetRawInputBuffer counter, another thread, or absent registration is
 * insufficient. Never change an explicit mode or replay input automatically. */
export function rawRouteAdvice(value:unknown):Obj|undefined{
 const s=object(value),routing=object(s?.raw_buffer_routing),registration=object(s?.raw_registration),delivery=object(s?.raw_delivery);
 if(s?.ok!==true||s.rawmode!==1||s.raw_mouse!==true||routing?.version!==1||routing.mouse_recent_buffer_reader!==true||delivery?.buffer_supported!==true||registration?.query_ok!==true)return;
 const thread=routing.mouse_thread;if(typeof thread!=='number'||!Number.isInteger(thread)||thread<1||thread>4294967295)return;
 const owned=registration.mouse_target_owned===true||(registration.mouse_target==='0x0'&&registration.fallback_target_owned===true);if(!owned)return;
 return {kind:'explicit_post_with_recent_mouse_buffer_reader',configured_mode:'post',destination_thread:thread,
  message:'Explicit raw:post sends WM_INPUT although this owned mouse destination recently called GetRawInputBuffer. Test config raw:auto or raw:buffer deliberately on this x64 target; no mode was changed or input repeated.',
  mode_changed:false,input_replayed:false,application_consumption_measured:false,
  scope:'recent intercepted reader activity on the registered/fallback mouse destination thread; this does not prove the selected game backend or camera response'};
}
