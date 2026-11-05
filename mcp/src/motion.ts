type Obj=Record<string,unknown>;
export type MotionSample={w:number;h:number;rgb:Uint8Array};
export function motionSample(value:unknown):MotionSample|undefined{
 if(!value||typeof value!=='object'||Array.isArray(value))return;const o=value as Obj,w=o.motion_sample_w,h=o.motion_sample_h;
 if(o.motion_sample_version!==1||typeof w!=='number'||typeof h!=='number'||!Number.isInteger(w)||!Number.isInteger(h)||w<1||w>16||h<1||h>16||typeof o.motion_sample_rgb!=='string'||o.motion_sample_rgb.length!==w*h*6||!/^[0-9a-f]+$/i.test(o.motion_sample_rgb))return;
 return {w,h,rgb:Uint8Array.from(Buffer.from(o.motion_sample_rgb,'hex'))};
}
export function motionDifference(a:MotionSample|undefined,b:MotionSample|undefined,tolerance=8):{changed_pct:number;mean_abs_rgb_diff_pct:number}|undefined{
 if(!a||!b||a.w!==b.w||a.h!==b.h||a.rgb.length!==b.rgb.length||!Number.isInteger(tolerance)||tolerance<0||tolerance>255)return;
 let changed=0,sum=0;for(let i=0;i<a.rgb.length;i+=3){let high=0;for(let c=0;c<3;c++){const d=Math.abs(a.rgb[i+c]-b.rgb[i+c]);high=Math.max(high,d);sum+=d;}if(high>tolerance)changed++;}
 return {changed_pct:changed*100/(a.w*a.h),mean_abs_rgb_diff_pct:sum*100/(255*a.rgb.length)};
}
