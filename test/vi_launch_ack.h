#pragma once
#include <windows.h>
#include <cstdio>
#include <string>
namespace vi_launch_ack {
inline DWORD WINAPI Deadline(void*){Sleep(120000);ExitProcess(119);}
inline bool ArmDeadline(){HANDLE thread=CreateThread(nullptr,0,Deadline,nullptr,0,nullptr);if(!thread)return false;CloseHandle(thread);return true;}
inline std::string Escape(const wchar_t* text){std::string value;for(;*text;++text){if(*text==L'\\'||*text==L'"')value+='\\';value+=*text<128?(char)*text:'?';}return value;}
inline bool Wait(FILE* out,const wchar_t* role){
 wchar_t nonce[40]{};const auto nonceSize=GetEnvironmentVariableW(L"VI_LAUNCH_CASE_NONCE",nonce,40);if(nonceSize!=32)return false;
 for(unsigned i=0;i<32;++i)if(!((nonce[i]>=L'0'&&nonce[i]<=L'9')||(nonce[i]>=L'a'&&nonce[i]<=L'f')))return false;
 wchar_t prefix[160]{};const auto count=GetEnvironmentVariableW(L"VI_LAUNCH_ACK_PREFIX",prefix,160);if(!count||count>=160)return false;
 wchar_t name[240]{};swprintf_s(name,L"%s_%s_%lu",prefix,role,GetCurrentProcessId());
 SetLastError(0);HANDLE event=CreateEventW(nullptr,TRUE,FALSE,name);if(!event)return false;if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(event);return false;}
 FILETIME created{},exited{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)){CloseHandle(event);return false;}
 ULARGE_INTEGER stamp{};stamp.LowPart=created.dwLowDateTime;stamp.HighPart=created.dwHighDateTime;
 fprintf(out,"{\"event\":\"guard_wait\",\"pid\":%lu,\"creation_filetime\":%llu,\"role\":\"%s\",\"ack_name\":\"%s\",\"case_nonce\":\"%s\",\"deadline_ms\":5000}\n",GetCurrentProcessId(),stamp.QuadPart,Escape(role).c_str(),Escape(name).c_str(),Escape(nonce).c_str());fflush(out);
 const DWORD result=WaitForSingleObject(event,5000);CloseHandle(event);
 fprintf(out,"{\"event\":\"guard_ack\",\"pid\":%lu,\"ok\":%s}\n",GetCurrentProcessId(),result==WAIT_OBJECT_0?"true":"false");fflush(out);return result==WAIT_OBJECT_0;
}
inline LRESULT CALLBACK NoActivation(int code,WPARAM w,LPARAM l){if(code<0)return CallNextHookEx(nullptr,code,w,l);return code==HCBT_ACTIVATE||code==HCBT_SETFOCUS?1:0;}
inline bool ArmNoActivation(){return SetWindowsHookExW(WH_CBT,NoActivation,nullptr,GetCurrentThreadId())!=nullptr;}
}
