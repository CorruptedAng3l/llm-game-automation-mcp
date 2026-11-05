#pragma once
#include "vi_ready_json.h"
#include <cstdint>
namespace vi_launch_live {
inline bool Boolean(const vi_ready_json::Reader& r,const char* key,bool wanted){bool value=false;return r.Bool(key,value)&&value==wanted;}
inline bool Version(const vi_ready_json::Reader& r,const char* key){auto i=r.fields.find(key);return i!=r.fields.end()&&i->second.type=='n'&&i->second.raw=="1";}
inline bool QueuePending(const std::string& text,std::uint32_t pid,std::uint64_t& hwnd){
 hwnd=0;vi_ready_json::Reader r{text,0,{}};if(!r.Parse()||!Version(r,"readiness_version")||!Version(r,"cursor_guard_version")||!Version(r,"keybd_event_guard_version")||
  !Boolean(r,"ok",false)||!Boolean(r,"hook_ready",true)||!Boolean(r,"window_ready",true)||!Boolean(r,"queue_ready",false)||!Boolean(r,"cursor_hooks_ready",true)||!Boolean(r,"keybd_event_hooks_ready",true))return false;
 auto identity=r.fields.find("pid"),reason=r.fields.find("readiness_reason"),handle=r.fields.find("hwnd"),generation=r.fields.find("window_generation");
 if(identity==r.fields.end()||identity->second.type!='n'||identity->second.raw!=std::to_string(pid)||reason==r.fields.end()||reason->second.raw!="\"queue_not_progressing\""||handle==r.fields.end()||handle->second.type!='"'||generation==r.fields.end()||generation->second.type!='n')return false;
 const auto& number=generation->second.raw;if(number.empty()||number[0]<'1'||number[0]>'9')return false;for(char c:number)if(c<'0'||c>'9')return false;
 std::string raw;vi_ready_json::Reader token{handle->second.raw,0,{}};if(!token.String(raw)||raw.size()<3||raw.size()>18||raw[0]!='0'||raw[1]!='x')return false;
 for(size_t i=2;i<raw.size();++i){const int digit=vi_ready_json::Reader::Hex(raw[i]);if(digit<0)return false;hwnd=(hwnd<<4)|(unsigned)digit;}return hwnd!=0;
}
inline bool UserTookForeground(bool active,bool observedTargetForeground,bool currentlyTarget){return active&&observedTargetForeground&&currentlyTarget;}
}
