// Actual vi_job_reads.inl + ChannelTrace + extracted GetAsyncKeyState handler.
// Finite in-memory endpoints only: no Init, HWND, device, worker or input API.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
#include "vi_trace.h"
#include "vi_input_read_observation.h"
static std::string JEsc(const std::string& in){std::string out;for(char c:in){if(c=='"'||c=='\\')out+='\\';out+=c;}return out;}
#include "actual_receipt_jout.inl"
struct InputJob{enum Kind{Keys,Pulse,Pad,Ramp,Timeline,Placement,Sequence}kind=Keys;bool pad=false,done=false;unsigned long long id=42;std::vector<int> keys;vi_input_reads::State readObservation;};
static KeyTrace g_keyTrace[256];static std::mutex g_padStateMx;static unsigned long long g_padReadOwner=0,g_padReadReturns=0;static bool g_padReadValid=true;
static ULONGLONG modelNow=100;
static ULONGLONG HousekeepingNow(){return modelNow;}
struct Counters{unsigned long long gaks=0;};static Counters C;
static std::atomic<unsigned char> g_vk[256]{},g_vkPressed[256]{};
enum{ReaderAsyncKey=0};static bool ReaderMask(int){return false;}
static unsigned originalCalls=0;static SHORT WINAPI FiniteAsync(int){++originalCalls;return 0;}
static SHORT(WINAPI* o_GetAsyncKeyState)(int)=FiniteAsync;
#pragma warning(push)
// Unchanged production handler deliberately casts Win32 bit15 to signed SHORT;
// its normal /W3 build does not diagnose that /W4-only constant bit pattern.
#pragma warning(disable:4310)
#include "actual_receipt_gaks.inl"
#pragma warning(pop)
#include "vi_job_reads.inl"
static unsigned passed=0,failed=0;
static void Check(bool good,const char* name){printf("%s %s\n",good?"PASS":"FAIL",name);good?++passed:++failed;}
static bool Has(const std::string& text,const std::string& part){return text.find(part)!=std::string::npos;}
static InputJob Start(std::vector<int> keys){InputJob job;job.keys=std::move(keys);++modelNow;JobReadsStart(job,modelNow);return job;}
static std::string Finish(InputJob& job){++modelNow;JobReadsSample(job,true);job.done=true;JOut out;JobReadFields(out,job);return out.done();}
static std::string Read(InputJob& job){JOut out;JobReadFields(out,job);return out.done();}
int main(){
 auto messages=Start({'W'});g_keyTrace['W'].delivery(true);auto receipt=Finish(messages);printf("RECEIPT message_only %s\n",receipt.c_str());
 Check(Has(receipt,"\"version\":2")&&Has(receipt,"\"outcome\":\"observed\"")&&Has(receipt,"\"keys_observed\":1"),"message-only exact-key subclass dispatch is observed");
 Check(Has(receipt,"\"polling_outcome\":\"not_observed\"")&&Has(receipt,"\"all_requested_polling_observed\":false")&&Has(receipt,"\"poll_reads\":\"0\",\"down_returns\":\"0\""),"message dispatch never invents a polling return");
 Check(Has(receipt,"\"observed_channels\":[\"WindowMessageDispatch\"]")&&Has(receipt,"\"application_response\":null")&&Has(receipt,"does not prove application consumption"),"actual serializer names dispatch and preserves unknown application consumption");
 g_keyTrace['W'].delivery(true);g_keyTrace['W'].gaks.record(0x8000);Check(Read(messages)==receipt,"later dispatch/polling cannot rewrite a frozen receipt");
 auto posted=Start({'A'});g_keyTrace['A'].posting(true);g_keyTrace['A'].post_result(true,true,0);auto r=Finish(posted);
 Check(Has(r,"\"outcome\":\"not_observed\"")&&Has(r,"\"message_dispatches\":\"0\""),"PostMessage acceptance without subclass dispatch is not observed input");
 g_keyTrace['D'].delivery(true);auto historical=Start({'D'});g_keyTrace[VK_SPACE].delivery(true);r=Finish(historical);
 Check(Has(r,"\"outcome\":\"not_observed\""),"historical and other-key dispatches cannot prove the requested key");
 auto polling=Start({'S'});g_vk['S']=1;const auto high=h_GetAsyncKeyState('S');g_vk['S']=0;r=Finish(polling);printf("RECEIPT gaks %s\n",r.c_str());
 Check((high&0x8000)!=0&&originalCalls==1&&C.gaks==1&&o_GetAsyncKeyState==FiniteAsync,"actual GetAsyncKeyState handler records virtual high through a finite original endpoint");
 Check(Has(r,"\"outcome\":\"observed\"")&&Has(r,"\"polling_outcome\":\"observed\"")&&Has(r,"\"observed_channels\":[\"GetAsyncKeyState\"]"),"actual GetAsyncKeyState high remains observed and correctly named");
 auto both=Start({'S'});g_vk['S']=1;h_GetAsyncKeyState('S');g_vk['S']=0;g_keyTrace['S'].delivery(true);r=Finish(both);
 Check(Has(r,"\"observed_channels\":[\"GetAsyncKeyState\",\"WindowMessageDispatch\"]")&&Has(r,"\"keys_observed\":1"),"two observed channels for one key count one requested key");
 auto chord=Start({'A','D'});g_keyTrace['A'].delivery(true);g_keyTrace['D'].gks.record(0x8000);r=Finish(chord);
 Check(Has(r,"\"all_requested_observed\":true")&&Has(r,"\"keys_observed\":2")&&Has(r,"\"polling_outcome\":\"partial\""),"message/poll mixed chord is observed while polling-only chord stays partial");
 auto partial=Start({'A','D'});g_keyTrace['A'].delivery(true);r=Finish(partial);
 Check(Has(r,"\"outcome\":\"partial\"")&&Has(r,"\"keys_observed\":1"),"one dispatched chord member cannot establish all requested keys");
 auto low=Start({'S'});h_GetAsyncKeyState('S');r=Finish(low);
 Check(Has(r,"\"poll_reads\":\"1\",\"down_returns\":\"0\"")&&Has(r,"\"outcome\":\"not_observed\""),"actual low-only GetAsyncKeyState read is not a virtual high observation");
 auto busy=Start({'W'});g_keyTrace['W'].gaks.writing=1;g_keyTrace['W'].delivery(true);r=Finish(busy);g_keyTrace['W'].gaks.writing=0;printf("RECEIPT busy_poll_message %s\n",r.c_str());
 Check(Has(r,"\"outcome\":\"observed\"")&&Has(r,"\"polling_outcome\":\"unavailable\"")&&Has(r,"\"poll_reads\":null,\"down_returns\":null")&&Has(r,"\"message_dispatches\":\"1\""),"busy poll snapshots do not erase independent exact dispatch evidence");
 auto badMessage=Start({'W'});g_keyTrace['W'].delivered_down=0;g_keyTrace['W'].gaks.record(0x8000);r=Finish(badMessage);
 Check(Has(r,"\"outcome\":\"observed\"")&&Has(r,"\"polling_outcome\":\"observed\"")&&Has(r,"\"message_dispatches\":null"),"invalid dispatch counters do not erase valid polling-high evidence");
 auto invalid=Start({'W'});g_keyTrace['W'].gaks.writing=1;r=Finish(invalid);g_keyTrace['W'].gaks.writing=0;
 Check(Has(r,"\"outcome\":\"unavailable\"")&&!Has(r,"\"outcome\":\"not_observed\""),"unavailable polling with no positive dispatch is uncertainty, not absence");
 auto alias=Start({VK_LSHIFT});g_keyTrace[VK_SHIFT].gaks.record(0x8000);r=Finish(alias);
 Check(Has(r,"\"modifier_alias_ambiguous\":true")&&Has(r,"\"outcome\":\"unavailable\""),"modifier alias-only high does not become exact-key input evidence");
 auto aliasMessage=Start({VK_LSHIFT});g_keyTrace[VK_SHIFT].gaks.record(0x8000);g_keyTrace[VK_LSHIFT].delivery(true);r=Finish(aliasMessage);
 Check(Has(r,"\"outcome\":\"observed\"")&&Has(r,"\"polling_outcome\":\"unavailable\"")&&Has(r,"\"modifier_alias_ambiguous\":true"),"exact modifier dispatch is observed while related polling ambiguity remains explicit");
 auto mixedUnknown=Start({'S',VK_LCONTROL});g_vk['S']=1;h_GetAsyncKeyState('S');g_vk['S']=0;g_keyTrace[VK_CONTROL].gaks.record(0x8000);r=Finish(mixedUnknown);printf("RECEIPT mixed_unknown %s\n",r.c_str());
 Check(Has(r,"\"outcome\":\"unavailable\"")&&Has(r,"\"keys_observed\":1")&&Has(r,"\"observed_channels\":[\"GetAsyncKeyState\"]"),"a different key's unavailable alias evidence cannot erase known GetAsyncKeyState positives");
 auto partialChannels=Start({'S'});g_vk['S']=1;h_GetAsyncKeyState('S');g_vk['S']=0;g_keyTrace['S'].raw_buffer.writing=1;r=Finish(partialChannels);g_keyTrace['S'].raw_buffer.writing=0;printf("RECEIPT partial_channels %s\n",r.c_str());
 Check(Has(r,"\"outcome\":\"observed\"")&&Has(r,"\"poll_reads\":null,\"down_returns\":null")&&Has(r,"\"GetAsyncKeyState\":{\"measured\":true,\"reads\":\"1\",\"down_returns\":\"1\"}")&&Has(r,"\"observed_channels\":[\"GetAsyncKeyState\"]"),"busy unrelated raw channel cannot erase a coherent actual GetAsyncKeyState high");
 InputJob queued;queued.keys={'W'};r=Read(queued);Check(Has(r,"\"outcome\":\"pending\"")&&Has(r,"\"observed_channels\":[]"),"queued/unstarted jobs do not reuse counters or announce observed channels");
 auto wait=Start({});r=Finish(wait);Check(Has(r,"\"outcome\":\"not_applicable\""),"wait-only job has no reader requirement");
 InputJob pad;pad.pad=true;pad.id=42;JobReadsStart(pad,++modelNow);g_padReadOwner=42;g_padReadReturns=2;r=Finish(pad);
 Check(Has(r,"\"pad_returns\":\"2\"")&&Has(r,"\"observed_channels\":[\"XInput\"]")&&Has(r,"\"outcome\":\"observed\""),"job-owned pad returns retain the XInput channel and unknown application response");
 InputJob foreign;foreign.pad=true;foreign.id=43;JobReadsStart(foreign,++modelNow);r=Finish(foreign);
 Check(Has(r,"\"pad_returns\":\"0\"")&&Has(r,"\"outcome\":\"not_observed\""),"another pad publication owner cannot establish this job's returns");
 Check(originalCalls==5,"GetAsyncKeyState positives and low control never call a physical reader");
 printf("native input receipts: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
