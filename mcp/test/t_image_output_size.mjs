import {imageOutputSize,resample} from '../build/imgtools.js';
import {makeChecker} from './lib.mjs';
const {state,check}=makeChecker();
for(const [w,h] of [[1,1],[8000,8000],[32768,1953]]){
 const size=imageOutputSize(w,h);
 check(size.width===w&&size.height===h&&size.bytes===w*h*4,`finite allowed output ${w}x${h} is checked without allocating`);
}
const invalid=[[0,1],[-1,1],[1,0],[NaN,1],[1,Infinity],[1.5,1],[32769,1],[8000,8001],[8192,8192],[65536,65536],[Number.MAX_SAFE_INTEGER+1,1]];
for(const [w,h] of invalid){
 let rejected=false;try{imageOutputSize(w,h);}catch(e){rejected=e instanceof RangeError&&/output size/.test(e.message);}
 check(rejected,`invalid or oversized output ${w}x${h} is rejected by pure arithmetic`);
}
// Exercise the actual resampler guard while preventing all output allocations, including accidental regressions.
const originalAlloc=Buffer.alloc;let allocations=0,rejected=0;
try{
 Buffer.alloc=()=>{allocations++;throw new Error('unexpected output allocation');};
 for(const [w,h] of invalid){try{resample({width:1,height:1,data:undefined},w,h);}catch(e){if(e instanceof RangeError)rejected++;}}
}finally{Buffer.alloc=originalAlloc;}
check(rejected===invalid.length&&allocations===0,'resample rejects every invalid size before any Buffer allocation');
console.log(`image output size: ${state.passes} PASS / ${state.failures} FAIL`);process.exitCode=state.failures?1:0;
