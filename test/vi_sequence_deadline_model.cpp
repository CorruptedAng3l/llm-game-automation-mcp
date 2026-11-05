// Exact extracted production scheduler; QPC and input endpoints are finite fakes.
// No target DLL, window, GUI/GPU, physical input or foreground operation.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
static std::uint64_t clockTick=100000;
static BOOL WINAPI ModelQpc(LARGE_INTEGER* n){n->QuadPart=(LONGLONG)clockTick;return TRUE;}
static BOOL WINAPI ModelFrequency(LARGE_INTEGER* n){n->QuadPart=1000;return TRUE;}
#define QueryPerformanceCounter ModelQpc
#define QueryPerformanceFrequency ModelFrequency
#include "../src/vi_qpc.h"
#undef QueryPerformanceCounter
#undef QueryPerformanceFrequency
#include "../src/vi_key_sequence.h"
#include "../src/vi_sequence_edges.h"
#include "../src/vi_sequence_frame.h"
struct InputJob{
 std::vector<vi_key_sequence::Step> sequence;size_t sequenceStep=0;int index=0,phase=0;unsigned transitions=0;bool done=false;
 ULONGLONG next=0,maxLate=0,duration=0;std::uint64_t qpcFrequency=1000,qpcAnchor=0,qpcNext=0,qpcLast=0,qpcLastDeadline=0,qpcMaxLate=0,qpcFinished=0;
 std::uint64_t qpcNativeStart=0,qpcFirst=0;unsigned latenessLimit=0;
 std::string state,reason;
 vi_sequence_edges::Trace sequenceEdges;
};
static void JobSequenceRecordEdge(InputJob& job,int vk,bool down,std::uint64_t qpc){vi_sequence_edges::Record(job.sequenceEdges,(unsigned)job.sequenceStep,job.sequence[job.sequenceStep].line,(unsigned)vk,down,qpc,clockTick/16);}
struct Edge{int key;bool down;std::uint64_t at;};static std::vector<Edge> edges;
static void KeyEvent(int key,bool down){edges.push_back({key,down,clockTick});}
static void JobFinish(InputJob& job,const char* state,const char* reason){job.done=true;job.state=state;job.reason=reason;job.qpcFinished=clockTick;}
#include "actual_sequence.inl"
#include "../src/vi_sequence_anchor.h"
static unsigned passed=0,failed=0;
static void Check(bool b,const char* what){printf("%s %s\n",b?"PASS":"FAIL",what);b?++passed:++failed;}
static InputJob Run(const std::string& script,unsigned quantum){
 vi_key_sequence::Plan plan;std::string error;if(!vi_key_sequence::Parse(script,plan,error)){Check(false,"model plan must parse");return {};}
 InputJob job;job.sequence=plan.steps;job.duration=plan.duration;job.next=clockTick;edges.clear();const auto limit=clockTick+20000;
 do{JobRunSequence(job,clockTick);if(!job.done)clockTick+=quantum;}while(!job.done&&clockTick<limit);return job;
}
int main(){
 using namespace vi_sequence_frame;
 Check(Evaluate(12,11,100,200,50,true)==Waiting&&Evaluate(12,12,100,200,50,false)==Waiting,"future frame and worker-only equality never start frame anchored input");
 Check(Evaluate(12,12,100,200,50,true)==Fire&&Evaluate(12,13,100,200,50,true)==Missed,"only exact native frame checkpoint fires; missed target refuses");
 Check(Evaluate(12,12,200,200,50,true)==Expired&&Evaluate(12,12,100,200,0,true)==Invalid&&Evaluate(0,1,100,200,50,true)==Invalid,"deadline wins at boundary and unavailable frame clocks refuse");
 const auto first=clockTick;std::string script;for(unsigned i=0;i<50;i++)script+="wait 10\n";script+="key space 80\n";
 auto job=Run(script,7);
 Check(job.done&&job.state=="completed"&&edges.size()==2,"fifty waits and one tap complete through actual scheduler");
 Check(edges.size()==2&&edges[0].down&&!edges[1].down&&edges[0].key==VK_SPACE,"exact expected input edge order");
 Check(job.sequenceEdges.edges.size()==2&&job.sequenceEdges.edges[0].qpc==edges[0].at&&job.sequenceEdges.edges[0].present==edges[0].at/16&&job.sequenceEdges.edges[0].step==50&&job.sequenceEdges.edges[0].vk==VK_SPACE&&job.sequenceEdges.edges[0].down,"per-edge trace retains actual scheduled press QPC, frame witness and step index");
 Check(edges.size()==2&&edges[0].at-first>=500&&edges[0].at-first<=507,"wait overshoot never accumulates into press deadline");
 Check(job.qpcFinished-first>=580&&job.qpcFinished-first<=594,"completion drift stays within two poll quanta");
 Check(job.qpcAnchor==first&&job.qpcLastDeadline==first+580&&job.qpcLast>=job.qpcLastDeadline,"receipt preserves original anchor and final declared deadline");
 Check(job.qpcMaxLate<=14&&job.qpcLast-job.qpcLastDeadline<=14,"absolute maximum and final cumulative lateness bounded independently");
 clockTick=200000;const auto waitStart=clockTick;script.clear();for(unsigned i=0;i<100;i++)script+="wait 1\n";job=Run(script,7);
 Check(job.done&&edges.empty()&&job.transitions==0,"wait-only sequence completes without an input edge");
 Check(job.qpcFinished-waitStart>=100&&job.qpcFinished-waitStart<=107,"overdue no-op waits coalesce without re-anchoring or per-step delay");
 clockTick=300000;job=Run("hold ctrl+a 20\n",7);
 Check(job.done&&edges.size()==4&&edges[0].key==VK_CONTROL&&edges[1].key=='A'&&edges[2].key=='A'&&edges[3].key==VK_CONTROL,"chord modifier ordering is retained");
 Check(edges.size()==4&&edges[0].at==300000&&edges[1].at>=300015&&edges[1].at<=300021&&edges[2].at>=300035&&edges[2].at<=300042&&edges[3].at>=300050&&edges[3].at<=300056,"all chord edges use one absolute timeline");
 Check(vi_qpc::Milliseconds(118000,10000000)==1180000000ULL&&vi_qpc::DeltaMs(9000,10000,1000)==-1000,"shared production QPC conversion and signed drift preserve units");
 clockTick=400000;job=Run("at 50 key space 20\nat 100 key space 10\n",7);
 Check(job.done&&edges.size()==4&&edges[0].at>=400050&&edges[0].at<=400056&&edges[2].at>=400100&&edges[2].at<=400107,"absolute schedule preserves initial delay and later offsets");
 std::uint64_t parsed=0;Check(vi_sequence_anchor::Parse("18446744073709551615",parsed)&&parsed==~std::uint64_t(0)&&!vi_sequence_anchor::Parse("18446744073709551616",parsed)&&!vi_sequence_anchor::Parse("01",parsed)&&!vi_sequence_anchor::Parse("0",parsed),"event anchor strictly parses exact nonzero uint64 decimal without precision loss");
 Check(vi_sequence_anchor::Current(1000,1000,7000,1000)&&!vi_sequence_anchor::Current(1000,1000,7001,1000)&&!vi_sequence_anchor::Current(1001,1000,1000,1000)&&!vi_sequence_anchor::Current(1000,999,1000,1000),"event anchor has inclusive6000ms age bound and refuses future/mismatched frequency");
 vi_key_sequence::Plan plan;std::string error;vi_key_sequence::Parse("at 50 key space 20\n",plan,error);job={};job.sequence=plan.steps;job.duration=plan.duration;job.qpcAnchor=500000;job.qpcNext=500050;clockTick=500030;edges.clear();JobRunSequence(job,clockTick);
 Check(edges.empty()&&job.qpcNativeStart==500030,"event anchor retains initial offset even when first worker starts later");clockTick=500050;JobRunSequence(job,clockTick);clockTick=500070;JobRunSequence(job,clockTick);
 Check(job.done&&job.qpcAnchor==500000&&job.qpcFirst==500050&&edges.size()==2,"event-based deadlines and first-apply receipt retain observed QPC timeline");
 job={};job.sequence=plan.steps;job.duration=plan.duration;job.qpcAnchor=600000;job.qpcNext=600050;job.latenessLimit=10;clockTick=600061;edges.clear();JobRunSequence(job,clockTick);
 Check(job.done&&job.state=="cancelled"&&job.reason=="sequence_max_lateness_exceeded"&&edges.empty(),"overdue edge aborts the whole sequence explicitly before any press");
 clockTick=700000;job=Run("retap w+shift shift 1500 300 10\n",1);
 const auto retapCount=std::count_if(edges.begin(),edges.end(),[](const Edge& e){return e.key==VK_SHIFT;});
 const auto wCount=std::count_if(edges.begin(),edges.end(),[](const Edge& e){return e.key=='W';});
 Check(job.done&&job.state=="completed"&&edges.size()==12&&job.transitions==12&&retapCount==10&&wCount==2,"retap repeats only the selected key while movement has one down/up pair");
 Check(edges.size()==12&&edges[2].key==VK_SHIFT&&!edges[2].down&&edges[2].at==700315&&edges[3].down&&edges[3].at==700325&&edges[10].key==VK_SHIFT&&!edges[10].down&&edges[10].at==701515&&edges[11].key=='W'&&edges[11].at==701530,"retap gaps and chord release retain one exact anchored timeline");
 Check(job.sequenceEdges.edges.size()==12&&job.sequenceEdges.dropped==0,"every re-tap edge retains the standard finite native timing receipt");
 vi_sequence_edges::Trace limited;for(unsigned n=0;n<vi_sequence_edges::Limit+2;n++)vi_sequence_edges::Record(limited,n,1,VK_SPACE,true,n,0);
 Check(limited.edges.size()==vi_sequence_edges::Limit&&limited.dropped==2,"edge telemetry has a finite cap with exact dropped count");
 printf("sequence deadline model: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
