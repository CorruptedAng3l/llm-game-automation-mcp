export const normalizedConditionText=(s:string)=>s.normalize('NFKC').toLowerCase().replace(/&/g,' and ').replace(/[^\p{L}\p{N}\s]/gu,'').replace(/\s+/g,' ').trim();
export function conditionTextMatch(actual:string,wanted:string,exact=false,fuzzy?:number):boolean{
 const a=normalizedConditionText(actual),b=normalizedConditionText(wanted);if(!b)return false;
 if(exact?a===b:a.includes(b))return true;if(fuzzy===undefined)return false;
 if(!Number.isFinite(fuzzy)||fuzzy<=0||fuzzy>1||a.length>16384||b.length>1024)throw new Error('fuzzy text bounds exceeded');
 let previous=Array.from({length:a.length+1},(_,i)=>exact?i:0);
 for(let i=1;i<=b.length;++i){const next=[i];for(let j=1;j<=a.length;++j)next[j]=Math.min(previous[j]+1,next[j-1]+1,previous[j-1]+(b[i-1]===a[j-1]?0:1));previous=next;}
 const distance=exact?previous[a.length]:Math.min(...previous);return 1-distance/b.length>=fuzzy;
}
