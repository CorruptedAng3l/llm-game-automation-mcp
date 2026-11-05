import {mkdirSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {randomUUID} from 'node:crypto';
export type CaptureDestination={path?:string;save_path?:string;out_dir?:string};
export function destinationError(a:CaptureDestination):string|undefined{
 if([a.path,a.save_path,a.out_dir].filter(v=>v!==undefined).length>1)return 'give only one of path, save_path or out_dir';
 if([a.path,a.save_path,a.out_dir].some(v=>v!==undefined&&(!v.trim()||v.includes('\0'))))return 'capture destination must be a nonempty path without NUL';
}
export function captureDestination(a:CaptureDestination,pid:number,extension:string,fallback:()=>string):string{
 const error=destinationError(a);if(error)throw new Error(error);
 if(a.out_dir!==undefined){const dir=resolve(a.out_dir);mkdirSync(dir,{recursive:true});return join(dir,`pid${pid}_${Date.now()}_${randomUUID()}.${extension}`);}
 return resolve(a.path??a.save_path??fallback());
}
