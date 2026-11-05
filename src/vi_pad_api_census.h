#pragma once
#include <atomic>
#include <cstdint>
// Observations only. No clock, lock, payload access or application callback.
struct PadApiCalls {
 std::atomic<unsigned long long> entries{0},ok{0},error{0};
 template<class Result> Result Finish(Result result){if(result==0)++ok;else ++error;return result;}
};
PadApiCalls g_padApiState[8][5],g_padApiCapabilities[4][5],g_padApiJoy;
std::atomic<unsigned> g_padApiStateInstalled{0};
std::atomic<bool> g_padApiRawRegistrationInstalled{false},g_padApiRawDataInstalled{false};
struct PadApiStateCall {
 PadApiCalls& calls;
 PadApiStateCall(unsigned slot,std::uint32_t user):calls(g_padApiState[slot][user<4?user:4]){++calls.entries;}
 template<class Result> Result Finish(Result result){return calls.Finish(result);}
};
// Numeric result from this thread's current GetCapabilities call, never an object cache.
thread_local unsigned g_padApiLastDiType=~0u;
std::atomic<unsigned long long> g_padApiDiState[4]{},g_padApiDiBuffer[4]{};
void PadApiRecordDiRead(unsigned rawType,bool buffer){
 if(rawType==2||rawType==3||rawType==0x12||rawType==0x13)return;
 const unsigned kind=rawType==4||rawType==0x14?0:rawType==0x15?1:rawType==~0u?3:2;
 if(buffer)++g_padApiDiBuffer[kind];else ++g_padApiDiState[kind];
}
struct PadApiHidRegistration {std::atomic<unsigned long long> registered{0},removed{0},excluded{0};};
PadApiHidRegistration g_padApiHidRegistration[2];
std::atomic<unsigned long long> g_padApiHidInputReturns{0},g_padApiHidHeaderReturns{0},g_padApiHidHeaderUnknown{0};
void PadApiRecordHidRegistration(unsigned page,unsigned usage,bool removed,bool excluded){
 if(page!=1||(usage!=4&&usage!=5))return;
 auto& entry=g_padApiHidRegistration[usage-4];if(removed)++entry.removed;else if(excluded)++entry.excluded;else ++entry.registered;
}
void PadApiRecordHidReturn(bool input,bool header,bool hid){if(!hid)return;if(input)++g_padApiHidInputReturns;else if(header)++g_padApiHidHeaderReturns;}
