type Obj=Record<string,unknown>;
export function launcherFollowAction(value:unknown,claim:boolean,ttl?:number):Obj|undefined{
 if(!value||typeof value!=='object'||Array.isArray(value))return;
 const r=value as Obj;
 if(r.ok!==true||r.launch_state!=='launcher_ready'||typeof r.pid!=='number'||!Number.isSafeInteger(r.pid)||r.pid<1||r.pid>4294967295||typeof r.process_created!=='string'||!/^[1-9][0-9]{0,19}$/.test(r.process_created)||BigInt(r.process_created)>18446744073709551615n||typeof r.waiting_for!=='string'||!r.waiting_for.toLowerCase().endsWith('.exe')||/[\\/\x00-\x1f]/.test(r.waiting_for))return;
 return {tool:'launch',after:'Activate Play in this launcher, then run these arguments to follow its game without launching another copy.',arguments:{follow_pid:r.pid,follow_created:r.process_created,wait_exe:r.waiting_for,...(claim?{claim:true,claim_ttl_s:ttl??3600}:{})}};
}
