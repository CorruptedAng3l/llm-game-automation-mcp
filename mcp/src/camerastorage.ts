import {existsSync,mkdirSync,openSync,closeSync,readSync,fstatSync,writeFileSync,renameSync,rmSync} from 'node:fs';
import {dirname,join,resolve} from 'node:path';
import {randomUUID} from 'node:crypto';
import {resolveVinputExe} from './vinput.js';

export type AngleProfile={version:1;pid:number;created:string;scope:string;game?:string;yaw:number;pitch?:number;source:'caller_supplied'|'caller_observed_angle'};
const gain=(v:unknown):v is number=>typeof v==='number'&&Number.isFinite(v)&&v!==0&&Math.abs(v)<=10000;
export function cameraCreation(status:Record<string,unknown>):string|undefined{
 const p=status.presentation_window as Record<string,unknown>|undefined;
 const value=p?.version===1?p.process_creation_filetime:undefined;
 return typeof value==='string'&&/^[1-9][0-9]{0,19}$/.test(value)&&BigInt(value)<=18446744073709551615n?value:undefined;
}
function path(pid:number,created:string){
 if(!Number.isSafeInteger(pid)||pid<1||pid>4294967295||!/^[1-9][0-9]{0,19}$/.test(created)||BigInt(created)>18446744073709551615n)throw new Error('invalid camera profile process identity');
 const dir=process.env.VINPUT_CAMERA_PROFILE_DIR?.trim();
 return join(dir?resolve(dir):resolve(dirname(resolveVinputExe()),'run','camera_profiles'),`${pid}_${created}.json`);
}
export function saveAngleProfile(profile:AngleProfile){
 if(profile.version!==1||!gain(profile.yaw)||(profile.pitch!==undefined&&!gain(profile.pitch))||!profile.scope||profile.scope.length>2048||
   (profile.game!==undefined&&(!profile.game.trim()||profile.game.length>128))||!['caller_supplied','caller_observed_angle'].includes(profile.source))throw new Error('invalid bounded camera calibration');
 const file=path(profile.pid,profile.created);mkdirSync(dirname(file),{recursive:true});const temp=file+'.'+randomUUID()+'.tmp';
 try{writeFileSync(temp,JSON.stringify(profile),{encoding:'utf8',flag:'wx'});renameSync(temp,file);}finally{rmSync(temp,{force:true});}
 return file;
}
export function loadAngleProfile(pid:number,created:string,scope:string,game?:string):AngleProfile|undefined{
 const file=path(pid,created);if(!existsSync(file))return;
 const handle=openSync(file,'r');let p:AngleProfile;
 try{
  if(fstatSync(handle).size>8192)throw new Error('camera calibration file exceeds its bound');
  const bytes=Buffer.alloc(8193);let length=0;
  while(length<bytes.length){const count=readSync(handle,bytes,length,bytes.length-length,null);if(!count)break;length+=count;}
  if(length>8192)throw new Error('camera calibration file exceeds its bound');
  try{p=JSON.parse(bytes.subarray(0,length).toString('utf8'));}catch{throw new Error('camera calibration file is not valid JSON');}
 }finally{closeSync(handle);}
 if(!p||p.version!==1||p.pid!==pid||p.created!==created||p.scope!==scope||(game!==undefined&&p.game!==game)||!gain(p.yaw)||(p.pitch!==undefined&&!gain(p.pitch))||!['caller_supplied','caller_observed_angle'].includes(p.source))return;
 return p;
}
