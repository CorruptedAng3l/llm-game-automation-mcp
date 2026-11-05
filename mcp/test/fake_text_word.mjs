// Reference one-OCR-token CLI for wrapper wiring, not a native matcher substitute.
import { appendFileSync, readFileSync } from "node:fs";
const argv=process.argv.slice(2), rest=[];
for(let i=0;i<argv.length;i++){
  if(argv[i]==="--json")continue;
  if(["--pid","--owner"].includes(argv[i])){i++;continue;}
  rest.push(argv[i]);
}
const command=rest[0];
if(!["help","click-text"].includes(command)){await import("./fake_vinput.mjs");}
else{
  const scenario=JSON.parse(readFileSync(process.env.VINPUT_FAKE_SCENARIO,"utf8"));
  if(process.env.VINPUT_FAKE_LOG)appendFileSync(process.env.VINPUT_FAKE_LOG,JSON.stringify({argv,t:Date.now()})+"\n");
  const say=(data)=>{process.stdout.write(JSON.stringify(data)+"\n");process.exit(data.ok===false?1:0);};
  if(command==="help")say(scenario.replies?.help??{ok:true,text_word_boundary_version:1});
  const exact=rest.includes("--exact"), whole=exact||rest.includes("--whole-word=true");
  const normalize=(s)=>{const value=s.replace(/\s/g,"");return exact?value:value.toLowerCase();};
  const query=normalize(rest[1]),words=scenario.words??["Player"];
  const matches=words.filter((s)=>whole?normalize(s)===query:normalize(s).includes(query));
  const metadata={whole_word:whole,case_sensitive:exact,text_word_boundary_version:1,match_count:matches.length};
  if(!matches.length){
    if(rest.includes("--optional"))say({ok:true,clicked:false,skipped:true,...metadata});
    say({ok:false,clicked:false,error:"text not found",...metadata});
  }
  const at=rest.indexOf("--nth"),nth=at<0?0:Number(rest[at+1]);
  if((!nth&&matches.length>1)||nth>matches.length)say({ok:false,clicked:false,ambiguous:true,candidates:matches.map((text,index)=>({text,index:index+1})),...metadata});
  const dry=rest.includes("--dry-run");
  say({ok:true,clicked:!dry,dry_run:dry,match:{text:matches[(nth||1)-1],index:nth||1},...metadata});
}
