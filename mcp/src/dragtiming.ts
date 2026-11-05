type Obj=Record<string,unknown>;
type Options={ms?:number;hold_start_ms?:number;hold_end_ms?:number;timing_profile?:'auto'|'default'|'unity'};
const object=(v:unknown):Obj|undefined=>v&&typeof v==='object'&&!Array.isArray(v)?v as Obj:undefined;
export function dragTiming(a:Options,pid:number,status?:unknown){
  const s=object(status),f=object(s?.framework_input);
  const freshUnity=s?.ok===true&&s.pid===pid&&f?.version===1&&f.class==='UnityWndClass'&&typeof f.hwnd==='string'&&/^0x[0-9a-f]{1,16}$/i.test(f.hwnd)&&BigInt(f.hwnd)>0n&&typeof f.window_generation==='string'&&/^[1-9][0-9]{0,19}$/.test(f.window_generation)&&BigInt(f.window_generation)<=18446744073709551615n;
  const unity=a.timing_profile==='unity'||((a.timing_profile??'auto')==='auto'&&freshUnity);
  return {ms:a.ms??(unity?800:300),holdStart:a.hold_start_ms??(unity?400:150),holdEnd:a.hold_end_ms??(unity?500:250),
    timing_profile_used:{profile:unity?'unity':'default',source:a.timing_profile==='unity'?'caller':unity?'fresh_owned_window_class':a.timing_profile==='default'?'caller':'unrecognized_or_unavailable_window',class:unity&&freshUnity?'UnityWndClass':null,
      note:'Timing is a bounded input preset, not proof of press consumption or game acceptance; explicit timing fields override it'}};
}
