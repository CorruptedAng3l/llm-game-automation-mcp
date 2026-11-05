// Reference OCR-line controller protocol, separate from the native matcher tests.
import {appendFileSync,readFileSync} from 'node:fs';
const argv=process.argv.slice(2),rest=[];
for(let i=0;i<argv.length;i++){if(argv[i]==='--json')continue;if(['--pid','--owner'].includes(argv[i])){i++;continue;}rest.push(argv[i]);}
if(!['help','click-text','wait-text'].includes(rest[0]))await import('./fake_vinput.mjs');
else{
 const scene=JSON.parse(readFileSync(process.env.VINPUT_FAKE_SCENARIO,'utf8'));
 appendFileSync(process.env.VINPUT_FAKE_LOG,JSON.stringify({argv,t:Date.now()})+'\n');
 const say=j=>{process.stdout.write(JSON.stringify(j)+'\n');process.exit(j.ok===false?1:0);};
 if(rest[0]==='help')say(scene.replies?.help??{ok:true,text_word_boundary_version:1,text_line_match_version:1});
 const exact=rest.includes('--exact'),whole=exact||rest.includes('--whole-word=true'),line=rest.includes('--exact-line=true');
 const normalize=s=>{const t=s.replace(/\s/g,'');return exact?t:t.toLowerCase();},query=normalize(rest[1]);
 const fi=rest.indexOf('--fuzzy'),fuzzy=fi<0?0:Number(rest[fi+1]);
 const edits=(a,b)=>{let row=Array.from({length:b.length+1},(_,i)=>i);for(let i=1;i<=a.length;i++){let next=[i];for(let j=1;j<=b.length;j++)next[j]=Math.min(row[j]+1,next[j-1]+1,row[j-1]+(a[i-1]===b[j-1]?0:1));row=next;}return row[b.length];};
 const found=[];
 for(const text of scene.lines??[]){const n=normalize(text),tokens=text.trim().split(/\s+/);let score=0;
   if(line){const d=edits(n,query);if(!d||fuzzy&&d<=fuzzy)score=1-d/query.length;}
   else if(whole){for(let a=0;a<tokens.length;a++)for(let b=a;b<tokens.length;b++)if(normalize(tokens.slice(a,b+1).join(' '))===query)score=1;}
   else if(n.includes(query))score=1;
   if(score>0)found.push({text,line:text,match_score:score,index:found.length+1});
 }
 const meta={exact_line:line,whole_word:whole,case_sensitive:exact,match_count:found.length,ocr_confidence:null,text_line_match_version:1};
 if(!found.length){if(rest.includes('--optional'))say({ok:true,clicked:false,skipped:true,...meta});say({ok:false,clicked:false,error:'text not found as complete line',...meta});}
 const ni=rest.indexOf('--nth'),nth=ni<0?0:Number(rest[ni+1]);
 if(!nth&&found.length>1)say({ok:false,clicked:false,ambiguous:true,candidates:found,...meta});
 if(nth>found.length)say({ok:false,clicked:false,error:'nth out of bounds',candidates:found,...meta});
 say({ok:true,clicked:rest[0]==='click-text'&&!rest.includes('--dry-run'),match:found[(nth||1)-1],candidates:found,...meta});
}
