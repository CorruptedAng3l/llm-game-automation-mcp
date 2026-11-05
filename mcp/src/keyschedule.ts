export type KeySchedule={at_ms:number[];key:string;hold_ms?:number};
/** Build absolute native script lines; the native parser validates chord overhead. */
export function keyScheduleLines(schedule:KeySchedule):{lines:string[];error?:undefined}|{error:string;lines?:undefined}{
 const {at_ms,key}=schedule,hold=schedule.hold_ms??20;
 if(!key||/\s/.test(key)||key.length>512)return {error:'schedule key must be one bounded key/chord without whitespace'};
 if(!Array.isArray(at_ms)||!at_ms.length||at_ms.length>1024||!Number.isSafeInteger(hold)||hold<1||hold>118000)return {error:'schedule needs1..1024 offsets and hold_ms1..118000'};
 for(let i=0;i<at_ms.length;i++)if(!Number.isSafeInteger(at_ms[i])||at_ms[i]<0||at_ms[i]+hold>118000||(i>0&&(at_ms[i]<=at_ms[i-1]||at_ms[i]<at_ms[i-1]+hold)))return {error:'schedule offsets must increase without overlapping holds and end within118000ms'};
 const lines=at_ms.map(at=>`at ${at} key ${key} ${hold}`);
 if(Buffer.byteLength(lines.join('\n')+'\n','utf8')>32768)return {error:'schedule script exceeds32768 bytes'};
 return {lines};
}
